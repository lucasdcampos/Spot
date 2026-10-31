#include "spot/scene/net/network_module.h"

#include "spot/core/debug/log.h"
#include "spot/core/debug/profiler.h"
#include "spot/runtime/application.h"
#include "spot/runtime/console.h"
#include "spot/runtime/runtime_stats.h"
#include "spot/scene/net/network_components.h"
#include "spot/scene/scene.h"
#include "spot/scene/scene_assets.h"

#include <algorithm>
#include <format>

namespace Spot {

namespace {

const NetworkSystem* FindSystem(const Application& application, const NetworkSession& session) {
    const Scene* scene = application.GetScene();
    const NetworkSystem* system = scene ? scene->GetSystem<NetworkSystem>() : nullptr;
    return system && system->GetSession() == &session ? system : nullptr;
}

} // namespace

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
    m_ended.reset();
    m_announcedScene = {};
    NetworkSession& session = *m_session;
    if (session.IsServer()) {
        m_connections.push_back(
            session.OnPeerConnected.Connect([this](NetPeerId peer, std::span<const std::byte>) { SendScene(peer); }));
    } else {
        m_connections.push_back(session.OnPeerDisconnected.Connect(
            [this](NetPeerId, std::string_view reason) { m_ended = std::string(reason); }));
        session.GetMessages().On<LoadScene>([this](NetPeerId, LoadScene& message) {
            Application& application = GetApplication();
            const Scene* current = application.GetScene();
            if (current && current->GetSourceAsset() == message.scene) {
                return;
            }
            if (auto scene = InstantiateScene(application.GetAssets(), message.scene)) {
                // Bound before it starts, so its components know from the first frame that it is a client's.
                scene->AddSystem<NetworkSystem>(m_session, m_config.system);
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
    m_stats = std::make_unique<StatsSection>([this] { return DescribeSession(); });
}

void NetworkModule::OnStop() {
    Leave();
    m_console.clear();
    m_stats.reset();
}

void NetworkModule::MeasureFrame() {
    using Clock = std::chrono::steady_clock;
    if (!m_session) {
        m_measure = {};
        m_measured = {};
        return;
    }
    const Clock::time_point now = Clock::now();
    const NetHost& host = m_session->GetHost();
    std::uint64_t sent = 0;
    std::uint64_t received = 0;
    for (const NetPeerId peer : host.GetPeers()) {
        const NetPeerStats stats = host.GetStats(peer);
        sent += stats.bytesSent;
        received += stats.bytesReceived;
    }
    const NetworkSystem* system = FindSystem(GetApplication(), *m_session);
    const NetworkSystem::Statistics statistics = system ? system->GetStatistics() : NetworkSystem::Statistics{};
    const std::size_t snapshots = m_session->IsServer() ? statistics.snapshotsSent : statistics.snapshotsReceived;
    Measure& measure = m_measure;
    if (measure.session != m_session.get()) {
        measure = {.session = m_session.get(),
                   .start = now,
                   .bytesSent = sent,
                   .bytesReceived = received,
                   .snapshots = snapshots,
                   .lastSnapshots = snapshots,
                   .lastSnapshot = now};
        m_measured = {};
        return;
    }
    // Counts start again with a scene's new system.
    measure.snapshots = std::min(measure.snapshots, snapshots);
    if (snapshots != measure.lastSnapshots) {
        measure.longestGap =
            std::max(measure.longestGap, std::chrono::duration<double>(now - measure.lastSnapshot).count());
        measure.lastSnapshots = snapshots;
        measure.lastSnapshot = now;
    }
    measure.shown += statistics.shownObjects;
    measure.late += statistics.lateObjects;
    measure.peakLate = std::max(measure.peakLate, statistics.lateObjects);
    const double seconds = std::chrono::duration<double>(now - measure.start).count();
    if (seconds < 1.0) {
        return;
    }
    // Snapshots that stopped coming count as a wait too.
    const double waiting = std::chrono::duration<double>(now - measure.lastSnapshot).count();
    const std::size_t clients =
        m_session->IsServer() && system ? std::max<std::size_t>(system->GetReadyPeers().size(), 1) : 1;
    m_measured = {
        .bytesSent = static_cast<double>(sent - std::min(sent, measure.bytesSent)) / seconds,
        .bytesReceived = static_cast<double>(received - std::min(received, measure.bytesReceived)) / seconds,
        .snapshots = static_cast<double>(snapshots - measure.snapshots) / seconds / static_cast<double>(clients),
        .longestGap = std::max(measure.longestGap, waiting),
        .late = measure.shown > 0 ? static_cast<double>(measure.late) / static_cast<double>(measure.shown) : 0.0,
        .peakLate = measure.peakLate};
    measure = {.session = measure.session,
               .start = now,
               .bytesSent = sent,
               .bytesReceived = received,
               .snapshots = snapshots,
               .lastSnapshots = measure.lastSnapshots,
               .lastSnapshot = measure.lastSnapshot};
}

std::string NetworkModule::DescribeSession() const {
    if (!m_session) {
        return {};
    }
    const NetHost& host = m_session->GetHost();
    const bool server = m_session->IsServer();
    const std::vector<NetPeerId> peers = host.GetPeers();
    std::string text = server ? std::format("Network: server, {} client{}", peers.size(), peers.size() == 1 ? "" : "s")
                              : std::string(m_session->IsConnected() ? "Network: client" : "Network: connecting");
    if (!peers.empty()) {
        // On a server, the worst of its clients.
        NetPeerStats worst;
        for (const NetPeerId peer : peers) {
            const NetPeerStats stats = host.GetStats(peer);
            worst.roundTrip = std::max(worst.roundTrip, stats.roundTrip);
            worst.jitter = std::max(worst.jitter, stats.jitter);
            worst.loss = std::max(worst.loss, stats.loss);
            worst.throttle = std::min(worst.throttle, stats.throttle);
        }
        text += std::format(" | round trip {}{:.0f} ms, jitter {:.0f} | loss {:.0f}% | throttle {:.0f}%",
                            peers.size() > 1 ? "up to " : "", worst.roundTrip * 1000.0f, worst.jitter * 1000.0f,
                            worst.loss * 100.0f, worst.throttle * 100.0f);
    }
    const Measured& measured = m_measured;
    text += std::format("\n{:.0f} snapshots/s{}, longest gap {:.0f} ms | {:.1f} KiB/s in, {:.1f} KiB/s out",
                        measured.snapshots, server ? " to each" : "", measured.longestGap * 1000.0,
                        measured.bytesReceived / 1024.0, measured.bytesSent / 1024.0);
    if (const NetworkSystem* system = FindSystem(GetApplication(), *m_session)) {
        const NetworkSystem::Statistics& statistics = system->GetStatistics();
        text += std::format("\n{} objects", statistics.objects);
        if (server) {
            text += std::format(" | last snapshot {} B", statistics.lastSnapshotBytes);
        } else {
            text += std::format(" | shown {:.0f} ms back", system->GetShownDelay() * 1000.0);
        }
        text += std::format(" | {:.0f}% late, peak {}", measured.late * 100.0, measured.peakLate);
    }
    return text;
}

void NetworkModule::OnUpdate(float /*deltaTime*/) {
    if (!m_session) {
        MeasureFrame();
        return;
    }
    m_session->Update();
    MeasureFrame();
    if (m_ended) {
        const std::string reason = std::move(*m_ended);
        Log::Info("Left the session: {}", reason);
        Leave();
        OnSessionEnded.Emit(reason);
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
