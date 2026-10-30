#include "spot/net/net_discovery.h"

#include "spot/net/bit_stream.h"

#include <algorithm>
#include <chrono>

namespace Spot {

namespace {

constexpr std::uint32_t Magic = 0x44544F53; // "SOTD"
constexpr std::uint32_t QueryKind = 1;
constexpr std::uint32_t AnswerKind = 2;
constexpr std::size_t MaxAnswer = 1000;

double Now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::vector<std::byte> Header(std::uint32_t protocol, std::uint32_t kind, std::uint32_t query,
                              std::span<const std::byte> payload = {}) {
    BitWriter writer;
    writer.WriteBits(Magic, 32);
    writer.WriteBits(protocol, 32);
    writer.WriteBits(kind, 8);
    writer.WriteBits(query, 32);
    writer.WriteBytes(payload);
    return writer.TakeBytes();
}

} // namespace

NetDiscoveryResponder::NetDiscoveryResponder(std::unique_ptr<NetDatagramSocket> socket, std::uint32_t protocol)
    : m_socket(std::move(socket)), m_protocol(protocol) {
}

bool NetDiscoveryResponder::Listen(std::uint16_t port) {
    return m_socket->Open(port);
}

void NetDiscoveryResponder::Update() {
    std::vector<NetDatagram> datagrams;
    m_socket->Receive(datagrams);
    for (const NetDatagram& datagram : datagrams) {
        BitReader reader(datagram.data);
        const std::uint32_t magic = reader.ReadBits(32);
        const std::uint32_t protocol = reader.ReadBits(32);
        const std::uint32_t kind = reader.ReadBits(8);
        const std::uint32_t query = reader.ReadBits(32);
        if (!reader.IsValid() || magic != Magic || protocol != m_protocol || kind != QueryKind) {
            continue;
        }
        std::vector<std::byte> payload = answer ? answer() : std::vector<std::byte>();
        if (payload.size() > MaxAnswer) {
            payload.resize(MaxAnswer);
        }
        m_socket->SendTo(datagram.from, Header(m_protocol, AnswerKind, query, payload));
    }
}

NetDiscovery::NetDiscovery(std::unique_ptr<NetDatagramSocket> socket, std::uint32_t protocol)
    : m_socket(std::move(socket)), m_protocol(protocol) {
}

bool NetDiscovery::Query(std::uint16_t port) {
    if (!m_open) {
        m_open = m_socket->Open(0);
        if (!m_open) {
            return false;
        }
    }
    ++m_query;
    m_queryTime = Now();
    m_socket->Broadcast(port, Header(m_protocol, QueryKind, m_query));
    return true;
}

void NetDiscovery::Update() {
    if (!m_open) {
        return;
    }
    std::vector<NetDatagram> datagrams;
    m_socket->Receive(datagrams);
    for (NetDatagram& datagram : datagrams) {
        BitReader reader(datagram.data);
        const std::uint32_t magic = reader.ReadBits(32);
        const std::uint32_t protocol = reader.ReadBits(32);
        const std::uint32_t kind = reader.ReadBits(8);
        const std::uint32_t query = reader.ReadBits(32);
        if (!reader.IsValid() || magic != Magic || protocol != m_protocol || kind != AnswerKind) {
            continue;
        }
        NetDiscoveryAnswer answer{
            .from = std::move(datagram.from),
            .data = reader.ReadBytes(reader.GetBitsLeft() / 8),
            // An answer to an older query says nothing of how long this one took.
            .roundTrip = query == m_query ? static_cast<float>(Now() - m_queryTime) : 0.0f,
        };
        const auto same = std::find_if(m_answers.begin(), m_answers.end(),
                                       [&](const NetDiscoveryAnswer& known) { return known.from == answer.from; });
        if (same == m_answers.end()) {
            m_answers.push_back(std::move(answer));
        } else {
            if (answer.roundTrip == 0.0f) {
                answer.roundTrip = same->roundTrip;
            }
            *same = std::move(answer);
        }
    }
}

} // namespace Spot
