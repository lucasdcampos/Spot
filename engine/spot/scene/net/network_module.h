#pragma once

#include "spot/net/net_conditions.h"
#include "spot/net/net_host.h"
#include "spot/runtime/module.h"
#include "spot/scene/net/network_system.h"
#include "spot/scene/signal.h"

#include <functional>
#include <memory>
#include <vector>

namespace Spot {

class ConsoleRegistration;
class NetworkSession;

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
// bad network on what this end sends.
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

protected:
    void OnStart() override;
    void OnStop() override;
    void OnUpdate(float deltaTime) override;

private:
    struct LoadScene;

    bool Start(std::unique_ptr<NetHost> host);
    std::unique_ptr<NetHost> MakeHost();
    void BindScene();
    void SendScene(NetPeerId peer);

    NetworkModuleConfig m_config;
    NetConditions m_conditions;
    std::shared_ptr<NetworkSession> m_session;
    ConditionedNetTransport* m_transport = nullptr;
    AssetId m_announcedScene;
    std::vector<SignalConnection> m_connections;
    std::vector<ConsoleRegistration> m_console;
};

} // namespace Spot
