#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Spot {

// Where a machine is reached: a host name or an IP address, and a port.
struct NetAddress {
    std::string host;
    std::uint16_t port = 0;

    // "host:port", "host" with the default port, or "[address]:port" for an address with colons. Nothing when the
    // host is empty or the port is not a number from 1 to 65535.
    static std::optional<NetAddress> Parse(std::string_view text, std::uint16_t defaultPort);
    std::string ToString() const;

    friend bool operator==(const NetAddress&, const NetAddress&) = default;
};

} // namespace Spot
