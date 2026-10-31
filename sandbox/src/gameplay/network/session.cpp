#include "gameplay/network/session.h"

#include "gameplay/core/players.h"
#include "gameplay/game/map_info.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace Gameplay {

namespace {

bool IsValidName(std::string_view name) {
    return !name.empty() && name.size() <= 64 && std::ranges::any_of(name, [](unsigned char c) { return c > 32; }) &&
           std::ranges::none_of(name, [](unsigned char c) { return c < 32 || c == 127; });
}

} // namespace

void SessionHello::Serialize(Spot::Archive& archive) {
    archive.Field("playerName", playerName);
    archive.Field("password", password);
    archive.Field("content", content);
}

std::uint64_t GetSessionContentSignature(const Spot::AssetManager& assets) {
    constexpr std::array Types = {
        Spot::AssetType::Scene,      Spot::AssetType::Prefab,        Spot::AssetType::Script,
        Spot::AssetType::Mesh,       Spot::AssetType::Model,         Spot::AssetType::Material,
        Spot::AssetType::Texture,    Spot::AssetType::Sound,         Spot::AssetType::Font,
        Spot::AssetType::Skeleton,   Spot::AssetType::AnimationClip, Spot::AssetType::AnimatorController,
        Spot::AssetType::SpriteAtlas};
    std::uint64_t hash = 14695981039346656037ull;
    const auto add = [&](std::span<const std::byte> data) {
        for (const std::byte value : data) {
            hash = (hash ^ std::to_integer<std::uint8_t>(value)) * 1099511628211ull;
        }
    };
    for (const Spot::AssetType type : Types) {
        for (const Spot::AssetId id : assets.GetAssetIds(type)) {
            const std::string name = id.ToString();
            add(std::as_bytes(std::span(name.data(), name.size())));
            const auto data = assets.ReadImportedData(id, type);
            if (!data) {
                return 0;
            }
            add(*data);
        }
    }
    return hash;
}

std::string ValidateSessionHello(const SessionSettings& settings, std::uint64_t content,
                                 std::span<const std::byte> bytes) {
    Spot::BitReader reader(bytes);
    SessionHello hello;
    if (!Spot::ReadArchived(reader, hello)) {
        return "Invalid room introduction. Update the game and try again.";
    }
    if (!IsValidName(hello.playerName)) {
        return "Choose a player name between 1 and 64 bytes, without control characters.";
    }
    if (hello.password != settings.password) {
        return "Incorrect room password.";
    }
    if (!content || hello.content != content) {
        return "Your game content differs from the host. Use the same build and imported assets.";
    }
    return {};
}

struct Session::Welcome {
    static constexpr std::string_view NetName = "Gameplay.RoomWelcome";
    std::string name;
    int maxPlayers = 16;
    Spot::AssetId map;
    Spot::AssetId mode;
    void Serialize(Spot::Archive& archive) {
        archive.Field("name", name);
        archive.Field("maxPlayers", maxPlayers);
        archive.Field("map", map);
        archive.Field("mode", mode);
    }
};

Session* Session::Get() {
    Spot::Application* application = Spot::Application::TryGet();
    return application ? application->GetModule<Session>() : nullptr;
}

bool Session::IsHost() const {
    return IsActive() && m_network->GetSession()->IsServer();
}

bool Session::IsActive() const {
    return m_network && m_network->GetSession();
}

std::string Session::GetPlayerName(Spot::NetPeerId peer) const {
    if (peer == Spot::NetPeerId::Invalid) {
        return m_settings.playerName;
    }
    const auto found = m_names.find(peer);
    return found != m_names.end() ? found->second : "Player " + std::to_string(static_cast<std::uint32_t>(peer));
}

void Session::SetState(SessionState state) {
    if (state != m_state) {
        m_state = state;
        OnStateChanged.Emit(state);
    }
}

void Session::OnStart() {
    m_network = GetApplication().GetModule<Spot::NetworkModule>();
    if (m_network) {
        m_ended = m_network->OnSessionEnded.Connect([this](std::string_view reason) {
            ClearConnections();
            m_error = std::string(reason);
            SetState(SessionState::Failed);
            ReturnToStartup();
        });
    }
}

void Session::ClearConnections() {
    m_connections.clear();
    m_names.clear();
    m_bound = nullptr;
}

void Session::OnStop() {
    ClearConnections();
    m_ended.Disconnect();
}

bool Session::Fail(std::string reason, bool returnToStartup) {
    ClearConnections();
    if (m_network) {
        m_network->Leave();
    }
    m_error = std::move(reason);
    SetState(SessionState::Failed);
    if (returnToStartup) {
        ReturnToStartup();
    }
    return false;
}

void Session::ReturnToStartup() {
    const auto* settings = GetApplication().GetGameSettings();
    if (settings && settings->startupScene.IsValid()) {
        if (auto scene = Spot::InstantiateScene(GetApplication().GetAssets(), settings->startupScene)) {
            GetApplication().LoadScene(std::move(scene));
        }
    }
}

bool Session::Host(SessionSettings settings) {
    if (!m_network) {
        return Fail("Networking is unavailable.");
    }
    if (!IsValidName(settings.playerName) || !IsValidName(settings.name) || settings.password.size() > 128) {
        return Fail("Enter a room name and player name; names must have 1 to 64 bytes.");
    }
    auto& assets = GetApplication().GetAssets();
    auto scene = Spot::InstantiateScene(assets, settings.map);
    MapInfo* map = scene ? MapInfo::Get(*scene) : nullptr;
    if (!map || settings.maxPlayers < (settings.dedicated ? 1 : 2) || settings.maxPlayers > map->maxPlayers) {
        return Fail("This map is unavailable or does not support the requested player count.");
    }
    if (!settings.mode.IsValid()) {
        settings.mode = map->defaultMode.GetId();
    }
    if (!map->StartMode(assets.Load<Spot::Prefab>(settings.mode), &assets)) {
        return Fail(map->GetLastError());
    }
    ClearConnections();
    m_content = GetSessionContentSignature(assets);
    if (!m_content) {
        return Fail("Could not read the game's imported content.");
    }
    if (settings.dedicated) {
        Players::RemoveAll(*scene);
    }
    m_settings = std::move(settings);
    // A host is one of the players.
    const int clients = m_settings.dedicated ? m_settings.maxPlayers : m_settings.maxPlayers - 1;
    if (!m_network->Host(m_settings.port, static_cast<std::size_t>(clients))) {
        return Fail("Could not host this port. Another room may already be using it.");
    }
    Bind(*m_network->GetSession());
    m_settings.port = m_bound->GetHost().GetPort();
    const SessionSettings admission = m_settings;
    const std::uint64_t content = m_content;
    m_bound->GetHost().onConnecting = [admission, content](Spot::NetPeerId, std::span<const std::byte> hello) {
        return ValidateSessionHello(admission, content, hello);
    };
    m_error.clear();
    m_since = std::chrono::steady_clock::now();
    SetState(SessionState::Loading);
    GetApplication().LoadScene(std::move(scene));
    return true;
}

bool Session::Join(std::string_view address, std::string playerName, std::string password) {
    if (!m_network) {
        return Fail("Networking is unavailable.");
    }
    const auto parsed = Spot::NetAddress::Parse(address, 27015);
    if (!parsed || !IsValidName(playerName) || password.size() > 128) {
        return Fail("Enter a valid server address and player name.");
    }
    ClearConnections();
    m_settings = {};
    m_settings.playerName = std::move(playerName);
    m_settings.password = std::move(password);
    m_settings.port = parsed->port;
    m_content = GetSessionContentSignature(GetApplication().GetAssets());
    if (!m_content) {
        return Fail("Could not read the game's imported content.");
    }
    SessionHello hello{m_settings.playerName, m_settings.password, m_content};
    Spot::BitWriter writer;
    Spot::WriteArchived(writer, hello);
    const auto bytes = writer.TakeBytes();
    if (!m_network->Join(*parsed, bytes)) {
        return Fail("Could not connect to this server address.");
    }
    Bind(*m_network->GetSession());
    m_error.clear();
    m_since = std::chrono::steady_clock::now();
    SetState(SessionState::Connecting);
    return true;
}

void Session::Bind(Spot::NetworkSession& session) {
    m_bound = &session;
    m_connections.push_back(
        session.OnPeerConnected.Connect([this](Spot::NetPeerId peer, std::span<const std::byte> bytes) {
            if (IsHost()) {
                Spot::BitReader reader(bytes);
                SessionHello hello;
                if (Spot::ReadArchived(reader, hello)) {
                    m_names[peer] = std::move(hello.playerName);
                }
                Spot::Log::Info("{} joined the room", GetPlayerName(peer));
                m_bound->Send(peer, Welcome{m_settings.name, m_settings.maxPlayers, m_settings.map, m_settings.mode});
            } else {
                SetState(SessionState::Loading);
            }
        }));
    m_connections.push_back(session.OnPeerDisconnected.Connect([this](Spot::NetPeerId peer, std::string_view reason) {
        if (IsHost()) {
            Spot::Log::Info("{} left the room: {}", GetPlayerName(peer), reason);
        }
        m_names.erase(peer);
    }));
    session.GetMessages().On<Welcome>([this](Spot::NetPeerId, Welcome& welcome) {
        if (!IsHost()) {
            m_settings.name = welcome.name;
            m_settings.maxPlayers = welcome.maxPlayers;
            m_settings.map = welcome.map;
            m_settings.mode = welcome.mode;
        }
    });
}

void Session::Leave() {
    ClearConnections();
    if (m_network) {
        m_network->Leave();
    }
    m_error.clear();
    SetState(SessionState::Offline);
    ReturnToStartup();
}

void Session::OnUpdate(float /*deltaTime*/) {
    if (!IsActive()) {
        if (m_bound) {
            ClearConnections();
            SetState(SessionState::Offline);
        }
        return;
    }
    if (m_state == SessionState::Connecting || m_state == SessionState::Loading) {
        Spot::Scene* scene = GetApplication().GetScene();
        const Spot::NetworkSystem* system = scene ? Spot::Network::Get(*scene) : nullptr;
        if (scene && system && scene->GetSourceAsset() == m_settings.map &&
            (m_settings.dedicated || Players::GetPawn(*scene)) && (IsHost() || system->IsSynchronized())) {
            SetState(SessionState::Playing);
        } else if (std::chrono::steady_clock::now() - m_since > std::chrono::seconds(15)) {
            Fail("The connection timed out. Check the address, port and host's firewall.", true);
        }
    }
}

} // namespace Gameplay
