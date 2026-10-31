#pragma once

#include <spot.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Gameplay {

// What a room tells those looking for one, as a hosting Session answers on the local network.
struct ServerInfo {
    std::string name;
    std::string map;
    std::string mode;
    int players = 0;
    int maxPlayers = 0;
    bool password = false;
    // The port its game listens on, which may differ from the one it answers on.
    std::uint16_t port = 0;

    void Serialize(Spot::Archive& archive);
};

// A room found, with the address to join it at ("host:port") and how long it took to answer, in seconds.
struct ServerEntry {
    ServerInfo info;
    std::string address;
    float ping = 0.0f;
};

// Where a game's menus find rooms to join: the local network (LanServers), and later others, as an internet master
// server, behind the same contract.
class ServerListSource {
public:
    virtual ~ServerListSource() = default;
    // Asks again; what was found stays until refreshed by the answers.
    virtual void Refresh() = 0;
    // Takes in what arrived; call it every frame while the list is shown.
    virtual void Update() = 0;
    virtual const std::vector<ServerEntry>& GetServers() const = 0;
};

// The rooms of the local network: a broadcast query on discoveryPort, which hosting sessions of the same protocol
// answer (SessionSettings::discoveryPort).
class LanServers final : public ServerListSource {
public:
    static constexpr std::uint16_t DefaultPort = 27016;

    // The platform's socket unless one is given, as a loopback one in tests.
    explicit LanServers(std::uint32_t protocol, std::uint16_t port = DefaultPort,
                        std::unique_ptr<Spot::NetDatagramSocket> socket = nullptr);

    void Refresh() override;
    void Update() override;
    const std::vector<ServerEntry>& GetServers() const override { return m_servers; }
    // False when the query could not be sent, as without a network.
    bool IsAvailable() const { return m_available; }

private:
    Spot::NetDiscovery m_discovery;
    std::uint16_t m_port;
    bool m_available = true;
    std::vector<ServerEntry> m_servers;
};

} // namespace Gameplay
