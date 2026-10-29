#include "gameplay/environment/day_cycle.h"
#include <spot/scene/scripting/script_bindings.h>

#include "gameplay/effects/effect_pool.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Gameplay {

SPOT_COMPONENT(DayCycle);

void DayCycle::SetTimeOfDay(float time) {
    if (!std::isfinite(time) || time < 0.0f || time >= 1.0f)
        throw std::invalid_argument("Time of day must be in [0, 1)");
    timeOfDay = time;
    UpdateSun();
    UpdateEffectLight();
}

void DayCycle::RegisterScriptBindings() {
    Spot::ScriptBindings<DayCycle>()
        .ReadOnly("timeOfDay", [](const DayCycle& cycle) { return cycle.timeOfDay; })
        .ReadOnly("daylight", &DayCycle::GetDaylight)
        .Method("GetDaylight", &DayCycle::GetDaylight)
        .Property(
            "dayLength", [](const DayCycle& cycle) { return cycle.dayLength; },
            [](DayCycle& cycle, float seconds) {
                if (seconds < 0.0f)
                    throw std::invalid_argument("Day length cannot be negative");
                cycle.dayLength = seconds;
            })
        .Method("SetTimeOfDay", &DayCycle::SetTimeOfDay);
}

void DayCycle::OnStart() {
    UpdateSun();
    UpdateEffectLight();
}

void DayCycle::OnUpdate(float deltaTime) {
    const float speed = Spot::Input::IsActionDown(fastForwardAction) ? fastForward : 1.0f;
    if (dayLength > 0.0f) {
        timeOfDay = std::fmod(timeOfDay + deltaTime * speed / dayLength, 1.0f);
    }
    UpdateSun();
    UpdateEffectLight();
}

void DayCycle::UpdateEffectLight() {
    // A little light is left at night, for the lamps and the moon.
    EffectPool::Get(GetScene()).SetLight(0.12f + 0.88f * std::clamp(m_daylight, 0.0f, 1.0f));
}

void DayCycle::Serialize(Spot::Archive& archive) {
    archive.Field("timeOfDay", timeOfDay,
                  {.min = 0.0f, .max = 1.0f, .speed = 0.001f, .tooltip = "From 0 at midnight to 1 at the next one"});
    archive.Field("dayLength", dayLength, {.min = 0.0f, .max = 100000.0f, .tooltip = "Seconds a whole day takes"});
    archive.Field("fastForward", fastForward,
                  {.min = 1.0f, .max = 1000.0f, .tooltip = "How many times faster time goes while held"});
    archive.Field("fastForwardAction", fastForwardAction, {.tooltip = "The input action that speeds time up"});
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

} // namespace

void DayCycle::UpdateAtmosphere(float sunHeight) {
    // 0 at night and 1 by day, and how near the horizon the sun is, for sunsets.
    const float day = SmoothStep(-0.08f, 0.25f, sunHeight);
    const float sunset = SmoothStep(0.35f, 0.0f, std::abs(sunHeight)) * SmoothStep(-0.2f, 0.0f, sunHeight);
    m_daylight = day;
    // The fog takes its color from the sky and the look its exposure; the air is hazier in the cool of dawn and dusk.
    if (auto* fog = GetEntity().GetComponent<Spot::Fog>()) {
        fog->density = 0.006f + (0.0025f - 0.006f) * day + sunset * 0.0015f;
    }
    // Eyes lose colors in the dark.
    if (auto* look = GetEntity().GetComponent<Spot::PostProcessing>()) {
        look->saturation = 0.85f + day * 0.2f;
    }
}

} // namespace Gameplay
