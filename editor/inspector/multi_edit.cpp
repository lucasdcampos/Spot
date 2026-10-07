#include "inspector/multi_edit.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>

namespace Spot::Editor {

using nlohmann::json;

namespace {

void Compare(const json& a, const json& b, const std::string& pointer, std::set<std::string>& differing) {
    if (a.is_object() && b.is_object()) {
        for (const auto& [key, value] : a.items()) {
            const std::string child = pointer + "/" + EscapePointerToken(key);
            const auto found = b.find(key);
            if (found == b.end()) {
                differing.insert(child);
            } else {
                Compare(value, *found, child, differing);
            }
        }
        for (const auto& [key, value] : b.items()) {
            if (!a.contains(key)) {
                differing.insert(pointer + "/" + EscapePointerToken(key));
            }
        }
        return;
    }
    if (a.is_array() && b.is_array() && a.size() == b.size()) {
        for (std::size_t i = 0; i < a.size(); ++i) {
            Compare(a[i], b[i], pointer + "/" + std::to_string(i), differing);
        }
        return;
    }
    if (a != b) {
        differing.insert(pointer);
    }
}

} // namespace

std::string EscapePointerToken(std::string_view token) {
    std::string escaped;
    for (const char c : token) {
        if (c == '~') {
            escaped += "~0";
        } else if (c == '/') {
            escaped += "~1";
        } else {
            escaped += c;
        }
    }
    return escaped;
}

std::vector<std::string> FindMixedValues(std::string_view primary, std::span<const std::string> others) {
    const json parsedPrimary = json::parse(primary, nullptr, false);
    std::set<std::string> differing;
    for (const std::string& other : others) {
        Compare(parsedPrimary, json::parse(other, nullptr, false), {}, differing);
    }
    return {differing.begin(), differing.end()};
}

std::string CopyValues(std::string_view source, std::string_view target, std::span<const std::string> pointers) {
    const json parsedSource = json::parse(source, nullptr, false);
    json parsedTarget = json::parse(target, nullptr, false);
    if (parsedSource.is_discarded() || parsedTarget.is_discarded()) {
        return std::string(target);
    }
    for (const std::string& text : pointers) {
        try {
            const json::json_pointer pointer(text);
            if (!parsedSource.contains(pointer)) {
                continue;
            }
            // Fields missing from the target are added, but arrays are not lengthened.
            if (!parsedTarget.contains(pointer) &&
                (pointer.empty() || !parsedTarget.contains(pointer.parent_pointer()) ||
                 !parsedTarget[pointer.parent_pointer()].is_object())) {
                continue;
            }
            parsedTarget[pointer] = parsedSource[pointer];
        } catch (const json::exception&) {
            // Not a valid pointer; the target keeps its values.
        }
    }
    return parsedTarget.dump();
}

} // namespace Spot::Editor
