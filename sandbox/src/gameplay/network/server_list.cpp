#include "gameplay/network/server_list.h"

#include <algorithm>

namespace Gameplay {

void ServerInfo::Serialize(Spot::Archive& archive) {
    archive.Field("name", name);
    archive.Field("map", map);
    archive.Field("mode", mode);
    archive.Field("players", players);
    archive.Field("maxPlayers", maxPlayers);
    archive.Field("password", password);
    std::uint32_t wide = port;
    archive.Field("port", wide);
    if (archive.IsLoading()) {
        port = wide <= 65535 ? static_cast<std::uint16_t>(wide) : 0;
    }
}

LanServers::LanServers(std::uint32_t protocol, std::uint16_t port, std::unique_ptr<Spot::NetDatagramSocket> socket)
    : m_discovery(socket ? std::move(socket) : Spot::CreateNetDatagramSocket(), protocol), m_port(port) {
}

void LanServers::Refresh() {
    m_available = m_discovery.Query(m_port);
}

void LanServers::Update() {
    m_discovery.Update();
    m_servers.clear();
    for (const Spot::NetDiscoveryAnswer& answer : m_discovery.GetAnswers()) {
        Spot::BitReader reader(answer.data);
        ServerEntry entry;
        if (!Spot::ReadArchived(reader, entry.info) || entry.info.port == 0) {
            continue;
        }
        entry.address = Spot::NetAddress{answer.from.host, entry.info.port}.ToString();
        entry.ping = answer.roundTrip;
        m_servers.push_back(std::move(entry));
    }
    std::ranges::sort(m_servers, [](const ServerEntry& a, const ServerEntry& b) {
        return a.info.name != b.info.name ? a.info.name < b.info.name : a.address < b.address;
    });
}

} // namespace Gameplay
