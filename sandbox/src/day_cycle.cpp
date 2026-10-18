#include "day_cycle.h"

#include <algorithm>
#include <cmath>

SPOT_COMPONENT(DayCycle);

void DayCycle::OnStart() {
    UpdateSun();
}

void DayCycle::OnUpdate(float deltaTime) {
    const float speed = Spot::Input::IsActionDown("Fast Forward") ? fastForward : 1.0f;
    if (dayLength > 0.0f) {
        timeOfDay = std::fmod(timeOfDay + deltaTime * speed / dayLength, 1.0f);
    }
    UpdateSun();
}

void DayCycle::Serialize(Spot::Archive& archive) {
    archive.Field("timeOfDay", timeOfDay,
                  {.min = 0.0f, .max = 1.0f, .speed = 0.001f, .tooltip = "From 0 at midnight to 1 at the next one"});
    archive.Field("dayLength", dayLength, {.min = 0.0f, .max = 100000.0f, .tooltip = "Seconds a whole day takes"});
    archive.Field("fastForward", fastForward,
                  {.min = 1.0f, .max = 1000.0f, .tooltip = "How many times faster time goes while T is held"});
}

void DayCycle::OnValidate() {
    UpdateSun();
}

void DayCycle::UpdateSun() {
    const float angle = (timeOfDay - 0.25f) * 2.0f * Spot::Pi;
    const Spot::Vector3 toSun = Spot::Normalize(Spot::Vector3{std::cos(angle), std::sin(angle), 0.4f});
    GetEntity().GetTransform().SetRotation(Spot::Quaternion::LookRotation(-toSun));
    // The stars turn with the sun, around the same axis, a turn a day.
    if (auto* sky = GetScene().FindComponent<Spot::Sky>()) {
        sky->SetStarAxis({0.0f, 0.0f, 1.0f});
        sky->SetStarRotation(angle);
    }
    UpdateAtmosphere(toSun.y);
}

namespace {

float SmoothStep(float from, float to, float value) {
    const float t = std::clamp((value - from) / (to - from), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Spot::Color Mix(Spot::Color a, Spot::Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0f};
}

} // namespace

void DayCycle::UpdateAtmosphere(float sunHeight) {
    // 0 at night and 1 by day, and how near the horizon the sun is, for sunsets.
    const float day = SmoothStep(-0.08f, 0.25f, sunHeight);
    const float sunset = SmoothStep(0.35f, 0.0f, std::abs(sunHeight)) * SmoothStep(-0.2f, 0.0f, sunHeight);
    m_daylight = day;
    if (auto* fog = GetEntity().GetComponent<Spot::Fog>()) {
        const Spot::Color night{0.015f, 0.022f, 0.05f, 1.0f};
        const Spot::Color noon{0.62f, 0.68f, 0.78f, 1.0f};
        const Spot::Color dusk{0.95f, 0.55f, 0.32f, 1.0f};
        fog->color = Mix(Mix(night, noon, day), dusk, sunset * 0.7f);
        fog->density = 0.006f + (0.0025f - 0.006f) * day + sunset * 0.0015f;
    }
    if (auto* look = GetEntity().GetComponent<Spot::PostProcessing>()) {
        look->temperature = sunset * 0.35f - (1.0f - day) * 0.25f;
        look->exposure = 1.0f + (1.0f - day) * 0.8f;
        look->saturation = 0.85f + day * 0.2f;
    }
}
