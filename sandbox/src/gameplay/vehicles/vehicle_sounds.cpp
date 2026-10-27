#include "gameplay/vehicles/vehicle_sounds.h"

#include "gameplay/vehicles/vehicle.h"

#include <algorithm>
#include <cmath>

namespace Gameplay {

SPOT_COMPONENT(VehicleEngineSound);
SPOT_COMPONENT(VehicleTireSound);

void VehicleEngineSound::OnStart() {
    if (const auto* source = GetEntity().GetComponent<Spot::AudioSource>()) {
        m_volume = source->volume;
    }
}

void VehicleEngineSound::OnUpdate(float /*deltaTime*/) {
    auto* source = GetEntity().GetComponent<Spot::AudioSource>();
    const Vehicle* vehicle = GetEntity().GetComponentInParent<Vehicle>();
    if (!source) {
        return;
    }
    if (!vehicle || !vehicle->HasDriver()) {
        source->Stop();
        return;
    }
    if (!source->IsPlaying()) {
        source->Play();
    }
    source->pitch = std::max(vehicle->GetRpm(), vehicle->GetIdleRpm()) / std::max(recordedRpm, 1.0f);
    source->volume = m_volume * (0.5f + 0.5f * vehicle->GetThrottle());
}

void VehicleEngineSound::OnDisable() {
    if (auto* source = GetEntity().GetComponent<Spot::AudioSource>()) {
        source->Stop();
    }
}

void VehicleEngineSound::Serialize(Spot::Archive& archive) {
    archive.Field(
        "recordedRpm", recordedRpm,
        {.min = 100.0f, .max = 20000.0f, .speed = 10.0f, .tooltip = "The revolutions its sound was recorded at"});
}

void VehicleTireSound::OnStart() {
    if (const auto* source = GetEntity().GetComponent<Spot::AudioSource>()) {
        m_volume = source->volume;
    }
}

void VehicleTireSound::OnUpdate(float /*deltaTime*/) {
    auto* source = GetEntity().GetComponent<Spot::AudioSource>();
    const Vehicle* vehicle = GetEntity().GetComponentInParent<Vehicle>();
    if (!source) {
        return;
    }
    const float squeal = vehicle ? std::clamp((vehicle->GetSlip() - 1.0f) / 1.5f, 0.0f, 1.0f) *
                                       std::min(std::abs(vehicle->GetForwardSpeed()) / 4.0f, 1.0f)
                                 : 0.0f;
    if (squeal < 0.02f) {
        source->Stop();
        return;
    }
    if (!source->IsPlaying()) {
        source->Play();
    }
    source->volume = m_volume * squeal;
    source->pitch = 0.9f + 0.2f * squeal;
}

void VehicleTireSound::OnDisable() {
    if (auto* source = GetEntity().GetComponent<Spot::AudioSource>()) {
        source->Stop();
    }
}

} // namespace Gameplay
