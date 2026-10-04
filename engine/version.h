#pragma once

#include <compare>
#include <string_view>

namespace Spot {

class Version {
public:
    constexpr Version(int major, int minor, int patch) : m_major(major), m_minor(minor), m_patch(patch) {}

    constexpr int GetMajor() const { return m_major; }
    constexpr int GetMinor() const { return m_minor; }
    constexpr int GetPatch() const { return m_patch; }

    friend constexpr auto operator<=>(const Version&, const Version&) = default;

private:
    int m_major;
    int m_minor;
    int m_patch;
};

Version GetVersion();
std::string_view GetVersionString();

} // namespace Spot
