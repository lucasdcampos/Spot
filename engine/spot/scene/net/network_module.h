#pragma once

#include "spot/net/net_conditions.h"
#include "spot/net/net_host.h"
#include "spot/runtime/module.h"
#include "spot/scene/net/network_system.h"
#include "spot/scene/signal.h"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Spot {

class ConsoleRegistration;
class NetworkSession;
class StatsSection;

struct NetworkModuleConfig {
    // What servers and clients must agree on (see NetHostConfig::protocol), such as the game's version.
    std::uint32_t protocol = 0;
    NetworkSystemConfig system;
    // Makes the transports of sessions; by default the platform's (CreateNetTransport()).
    std::function<std::unique_ptr<NetTransport>()> createTransport;
};

// Multiplayer for an application: hosts or joins a session, keeps it across scene changes, and binds the
// application's scene to it with a NetworkSystem. The server tells its clients which scene it runs, when they join
// and when it changes, and clients load it (Scene::GetSourceAsset()). Adds the net.* console settings, which simulate a
// bad network on what this end sends, and the session's numbers to the statistics overlay (`stats`), measured over
// each second.
class NetworkModule final : public Module {
public:
    explicit NetworkModule(NetworkModuleConfig config = {});
    ~NetworkModule() override;

    // Starts a server on the port; false when it cannot listen there.
    bool Host(std::uint16_t port, std::size_t maxClients);
    // Starts connecting to a server; the session's OnPeerConnected or OnPeerDisconnected tells how it went.
    bool Join(const NetAddress& address, std::span<const std::byte> hello = {});
    // Ends the session, telling the other machines; the scene goes on alone.
    void Leave();

    // Null while there is no session.
    NetworkSession* GetSession() const { return m_session.get(); }
    const NetworkModuleConfig& GetConfig() const { return m_config; }
    NetworkModuleConfig& GetConfig() { return m_config; }
    // The simulated network conditions of this end's sessions.
    const NetConditions& GetConditions() const { return m_conditions; }
    void SetConditions(const NetConditions& conditions);

    // A session started, with Host() or Join().
    Signal<NetworkSession&> OnSessionStarted;
    // A client's session ended, as the server refused it, shut down or could not be reached, with why; the scene goes
    // on alone.
    Signal<std::string_view> OnSessionEnded;

protected:
    void OnStart() override;
    void OnStop() override;
    void OnUpdate(float deltaTime) override;

private:
    struct LoadScene;
    // What the statistics overlay shows of the session, measured over a second.
    struct Measure {
        const NetworkSession* session = nullptr;
        std::chrono::steady_clock::time_point start;
        // The session's totals when the second started.
        std::uint64_t bytesSent = 0;
        std::uint64_t bytesReceived = 0;
        std::size_t snapshots = 0;
        // Frames that brought or sent snapshots: the latest count, when it changed, and the longest wait between.
        std::size_t lastSnapshots = 0;
        std::chrono::steady_clock::time_point lastSnapshot;
        double longestGap = 0.0;
        std::size_t shown = 0;
        std::size_t late = 0;
        std::size_t peakLate = 0;
    };
    struct Measured {
        double bytesSent = 0.0;
        double bytesReceived = 0.0;
        double snapshots = 0.0;
        double longestGap = 0.0;
        double late = 0.0;
        std::size_t peakLate = 0;
    };

    bool Start(std::unique_ptr<NetHost> host);
    std::unique_ptr<NetHost> MakeHost();
    void BindScene();
    void SendScene(NetPeerId peer);
    void MeasureFrame();
    std::string DescribeSession() const;

    NetworkModuleConfig m_config;
    NetConditions m_conditions;
    std::shared_ptr<NetworkSession> m_session;
    ConditionedNetTransport* m_transport = nullptr;
    AssetId m_announcedScene;
    // Why the client's connection ended, to end the session with once its update is over.
    std::optional<std::string> m_ended;
    std::vector<SignalConnection> m_connections;
    std::vector<ConsoleRegistration> m_console;
    std::unique_ptr<StatsSection> m_stats;
    Measure m_measure;
    Measured m_measured;
};

} // namespace Spot
