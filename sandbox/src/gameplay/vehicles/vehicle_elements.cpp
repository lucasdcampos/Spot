#include "gameplay/vehicles/vehicle_elements.h"

#include "gameplay/hud/input_prompts.h"
#include "gameplay/vehicles/vehicle.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace Gameplay {

SPOT_COMPONENT(VehicleElement);

namespace {

// Seconds to fade the HUD's transient lines in or out.
constexpr float FadeTime = 0.18f;

const Vehicle* GetVehicle(const HudContext& context) {
    return dynamic_cast<const Vehicle*>(context.pawn);
}

} // namespace

bool VehicleElement::IsWanted(const HudContext& context) {
    const bool driving = GetVehicle(context) != nullptr;
    // Its name and controls show for a moment after getting in.
    if (driving && !m_wasShown) {
        m_nameLeft = nameTime;
    }
    m_wasShown = driving && IsShown();
    return driving;
}

void VehicleElement::Refresh(const HudContext& context) {
    m_wasShown = true;
    m_nameLeft = std::max(m_nameLeft - context.deltaTime, 0.0f);
    const Vehicle* vehicle = GetVehicle(context);
    if (!vehicle) {
        return;
    }
    const float itemOpacity = std::clamp(m_nameLeft / FadeTime, 0.0f, 1.0f);
    std::string title = vehicle->GetEntity().GetName();
    std::ranges::transform(title, title.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    SetText(name, title);
    if (auto* text = Get<Spot::UiText>(name)) {
        text->color = {accent.r, accent.g, accent.b, itemOpacity};
    }
    SetActive(icon, false);
    SetActive(reloadTrack, false);
    const std::vector<Gauge> gauges = vehicle->DescribeTelemetry();
    std::string side;
    for (std::size_t i = 0; i < gauges.size(); ++i) {
        const std::string& part = i == 0 ? gauges[i].unit : gauges[i].value;
        if (!part.empty()) {
            side += side.empty() ? part : "   " + part;
        }
    }
    SetText(count, gauges.empty() ? std::string() : gauges.front().value);
    if (auto* text = Get<Spot::UiText>(count)) {
        text->fontSize = countSize;
        text->color = textColor;
    }
    SetText(detail, side);
    SetText(hints, InputPrompts::Format(vehicle->DescribeHints()));
    const float intro = context.hud ? context.hud->GetIntro() : 0.0f;
    if (auto* text = Get<Spot::UiText>(hints)) {
        text->color.a = std::clamp(std::max(itemOpacity, intro), 0.0f, 1.0f);
    }
}

void VehicleElement::Serialize(Spot::Archive& archive) {
    HudElement::Serialize(archive);
    archive.Field("name", name);
    archive.Field("icon", icon);
    archive.Field("count", count);
    archive.Field("detail", detail);
    archive.Field("hints", hints);
    archive.Field("reloadTrack", reloadTrack);
    archive.Field("nameTime", nameTime, {.min = 0, .max = 10, .speed = .05f});
    archive.Field("accent", accent);
    archive.Field("textColor", textColor);
    archive.Field("countSize", countSize, {.min = 1, .max = 200});
}

} // namespace Gameplay
