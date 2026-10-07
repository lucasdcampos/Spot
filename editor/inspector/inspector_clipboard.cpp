#include "inspector/inspector_clipboard.h"

#include <nlohmann/json.hpp>

#include <array>

namespace Spot::Editor {

using nlohmann::json;

namespace {

constexpr const char* ComponentFormat = "spot-component";
constexpr const char* TransformFormat = "spot-transform";

// The clipboard's JSON, when it is in the format.
std::optional<json> Parse(std::string_view text, const char* format) {
    json parsed = json::parse(text, nullptr, false);
    if (!parsed.is_object() || parsed.value("format", std::string()) != format) {
        return std::nullopt;
    }
    return parsed;
}

template <std::size_t N>
std::optional<std::array<float, N>> ReadFloats(const json& object, const char* name) {
    const auto found = object.find(name);
    if (found == object.end() || !found->is_array() || found->size() != N) {
        return std::nullopt;
    }
    std::array<float, N> values{};
    for (std::size_t i = 0; i < N; ++i) {
        if (!(*found)[i].is_number()) {
            return std::nullopt;
        }
        values[i] = (*found)[i].get<float>();
    }
    return values;
}

} // namespace

std::string MakeComponentClipboardText(std::string_view component) {
    return json{{"format", ComponentFormat}, {"component", json::parse(component, nullptr, false)}}.dump();
}

std::optional<CopiedComponent> ReadComponentClipboardText(std::string_view text) {
    const std::optional<json> parsed = Parse(text, ComponentFormat);
    if (!parsed) {
        return std::nullopt;
    }
    const auto component = parsed->find("component");
    if (component == parsed->end() || !component->is_object() || !component->contains("type") ||
        !(*component)["type"].is_string()) {
        return std::nullopt;
    }
    return CopiedComponent{(*component)["type"].get<std::string>(), component->dump()};
}

std::string MakeTransformClipboardText(const Pose& pose) {
    return json{
        {"format", TransformFormat},
        {"position", {pose.position.x, pose.position.y, pose.position.z}},
        {"rotation", {pose.rotation.x, pose.rotation.y, pose.rotation.z, pose.rotation.w}},
        {"scale", {pose.scale.x, pose.scale.y, pose.scale.z}},
    }
        .dump();
}

std::optional<Pose> ReadTransformClipboardText(std::string_view text) {
    const std::optional<json> parsed = Parse(text, TransformFormat);
    if (!parsed) {
        return std::nullopt;
    }
    const auto position = ReadFloats<3>(*parsed, "position");
    const auto rotation = ReadFloats<4>(*parsed, "rotation");
    const auto scale = ReadFloats<3>(*parsed, "scale");
    if (!position || !rotation || !scale) {
        return std::nullopt;
    }
    return Pose{
        {(*position)[0], (*position)[1], (*position)[2]},
        {(*rotation)[0], (*rotation)[1], (*rotation)[2], (*rotation)[3]},
        {(*scale)[0], (*scale)[1], (*scale)[2]},
    };
}

} // namespace Spot::Editor
