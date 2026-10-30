#include "spot/scene/net/network_module.h"

#include "spot/core/debug/log.h"
#include "spot/core/debug/profiler.h"
#include "spot/runtime/application.h"
#include "spot/runtime/console.h"
#include "spot/scene/net/network_components.h"
#include "spot/scene/scene.h"
#include "spot/scene/scene_assets.h"

namespace Spot {

// Which scene the server runs, for its clients to load.
struct NetworkModule::LoadScene {
    static constexpr std::string_view NetName = "Spot.LoadScene";
    AssetId scene;

    void Serialize(Archive& archive) { archive.Field("scene", scene); }
};

NetworkModule::NetworkModule(NetworkModuleConfig config) : m_config(std::move(config)) {
}

NetworkModule::~NetworkModule() = default;

std::unique_ptr<NetHost> NetworkModule::MakeHost() {
    std::unique_ptr<NetTransport> inner = m_config.createTransport ? m_config.createTransport() : CreateNetTransport();
    auto transport = std::make_unique<ConditionedNetTransport>(std::move(inner), m_conditions);
    m_transport = transport.get();
    return std::make_unique<NetHost>(
        std::move(transport), NetHostConfig{.protocol = m_config.protocol, .channels = NetworkSession::ChannelCount});
}

bool NetworkModule::Host(std::uint16_t port, std::size_t maxClients) {
    Leave();
    std::unique_ptr<NetHost> host = MakeHost();
    if (!host->Listen(port, maxClients)) {
        m_transport = nullptr;
        return false;
    }
    Log::Info("Hosting a session on port {}", host->GetPort());
    return Start(std::move(host));
}

bool NetworkModule::Join(const NetAddress& address, std::span<const std::byte> hello) {
    Leave();
    std::unique_ptr<NetHost> host = MakeHost();
    if (host->Connect(address, hello) == NetPeerId::Invalid) {
        m_transport = nullptr;
        return false;
    }
    Log::Info("Joining the session at {}", address.ToString());
    return Start(std::move(host));
}

bool NetworkModule::Start(std::unique_ptr<NetHost> host) {
    m_session = std::make_shared<NetworkSession>(std::move(host));
    m_announcedScene = {};
    NetworkSession& session = *m_session;
    if (session.IsServer()) {
        m_connections.push_back(
            session.OnPeerConnected.Connect([this](NetPeerId peer, std::span<const std::byte>) { SendScene(peer); }));
    } else {
        session.GetMessages().On<LoadScene>([this](NetPeerId, LoadScene& message) {
            Application& application = GetApplication();
            const Scene* current = application.GetScene();
            if (current && current->GetSourceAsset() == message.scene) {
                return;
            }
            if (auto scene = InstantiateScene(application.GetAssets(), message.scene)) {
                application.LoadScene(std::move(scene));
            } else {
                Log::Error("The server runs scene {}, which this game does not have", message.scene.ToString());
                Leave();
            }
        });
    }
    BindScene();
    OnSessionStarted.Emit(session);
    return true;
}

void NetworkModule::Leave() {
    if (!m_session) {
        return;
    }
    NetHost& host = m_session->GetHost();
    for (const NetPeerId peer : host.GetPeers()) {
        host.Disconnect(peer, host.IsServer() ? "The server shut down" : "The player left");
    }
    m_session->Update();
    // The scene goes on alone: its system lets go of the session.
    if (Scene* scene = GetApplication().GetScene()) {
        if (auto* system = scene->GetSystem<NetworkSystem>()) {
            system->SetSession(nullptr);
        }
    }
    m_connections.clear();
    m_session.reset();
    m_transport = nullptr;
}

void NetworkModule::SetConditions(const NetConditions& conditions) {
    m_conditions = conditions;
    if (m_transport) {
        m_transport->SetConditions(conditions);
    }
}

void NetworkModule::SendScene(NetPeerId peer) {
    const Scene* scene = GetApplication().GetScene();
    if (m_session && scene && scene->GetSourceAsset().IsValid()) {
        m_session->Send(peer, LoadScene{scene->GetSourceAsset()});
    }
}

void NetworkModule::BindScene() {
    Scene* scene = GetApplication().GetScene();
    if (!m_session || !scene) {
        return;
    }
    if (auto* system = scene->GetSystem<NetworkSystem>()) {
        system->SetSession(m_session);
    } else {
        scene->AddSystem<NetworkSystem>(m_session, m_config.system);
    }
    if (m_session->IsServer() && scene->GetSourceAsset() != m_announcedScene) {
        m_announcedScene = scene->GetSourceAsset();
        for (const NetPeerId peer : m_session->GetHost().GetPeers()) {
            SendScene(peer);
        }
    }
}

void NetworkModule::OnStart() {
    RegisterNetworkComponents();
    Console& console = Console::Get();
    const auto variable = [&](std::string_view name, std::string_view help, float NetConditions::* field, float max) {
        m_console.push_back(console.RegisterVariable(
            std::string(name), std::string(help), ConsoleFlags::None, FormatConsoleValue(0.0f),
            [this, field] { return FormatConsoleValue(m_conditions.*field); },
            [this, field, max](std::string_view text) {
                float value = 0.0f;
                if (!ParseConsoleValue(text, value) || value < 0.0f || value > max) {
                    return false;
                }
                NetConditions conditions = m_conditions;
                conditions.*field = value;
                SetConditions(conditions);
                return true;
            }));
    };
    variable("net.lag", "Seconds what this end sends takes to arrive, to test a bad network", &NetConditions::latency,
             5.0f);
    variable("net.jitter", "Seconds more what this end sends may take, at random", &NetConditions::jitter, 5.0f);
    variable("net.loss", "Share of what this end sends unreliably that is lost, from 0 to 1", &NetConditions::loss,
             1.0f);
}

void NetworkModule::OnStop() {
    Leave();
    m_console.clear();
}

void NetworkModule::OnUpdate(float /*deltaTime*/) {
    if (!m_session) {
        return;
    }
    m_session->Update();
    if (!m_session) {
        return;
    }
    BindScene();
    if (Profiler::IsEnabled()) {
        const Scene* scene = GetApplication().GetScene();
        const NetworkSystem* system = scene ? scene->GetSystem<NetworkSystem>() : nullptr;
        Profiler::Count("Replicated objects", system ? static_cast<double>(system->GetStatistics().objects) : 0.0);
        Profiler::Count("Snapshot bytes",
                        system ? static_cast<double>(system->GetStatistics().lastSnapshotBytes) : 0.0);
    }
}

} // namespace Spot
