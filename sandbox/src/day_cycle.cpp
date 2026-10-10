#include "day_cycle.h"

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
}
