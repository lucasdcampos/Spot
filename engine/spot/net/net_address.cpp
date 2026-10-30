#include "spot/net/net_address.h"

#include <charconv>

namespace Spot {

std::optional<NetAddress> NetAddress::Parse(std::string_view text, std::uint16_t defaultPort) {
    constexpr std::string_view Space = " \t\r\n";
    const std::size_t first = text.find_first_not_of(Space);
    if (first == std::string_view::npos) {
        return std::nullopt;
    }
    text = text.substr(first, text.find_last_not_of(Space) - first + 1);

    std::string_view host = text;
    std::string_view port;
    if (text.starts_with('[')) {
        const std::size_t close = text.find(']');
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        host = text.substr(1, close - 1);
        const std::string_view rest = text.substr(close + 1);
        if (!rest.empty()) {
            if (!rest.starts_with(':')) {
                return std::nullopt;
            }
            port = rest.substr(1);
        }
    } else if (const std::size_t colon = text.rfind(':'); colon != std::string_view::npos) {
        // More than one colon without brackets is an IPv6 address without a port.
        if (text.find(':') == colon) {
            host = text.substr(0, colon);
            port = text.substr(colon + 1);
        }
    }
    if (host.empty()) {
        return std::nullopt;
    }

    NetAddress address{std::string(host), defaultPort};
    if (!port.empty() || text.ends_with(':')) {
        unsigned value = 0;
        const auto [end, error] = std::from_chars(port.data(), port.data() + port.size(), value);
        if (error != std::errc() || end != port.data() + port.size() || value == 0 || value > 65535) {
            return std::nullopt;
        }
        address.port = static_cast<std::uint16_t>(value);
    }
    if (address.port == 0) {
        return std::nullopt;
    }
    return address;
}

std::string NetAddress::ToString() const {
    if (host.find(':') != std::string::npos) {
        return "[" + host + "]:" + std::to_string(port);
    }
    return host + ":" + std::to_string(port);
}

} // namespace Spot
