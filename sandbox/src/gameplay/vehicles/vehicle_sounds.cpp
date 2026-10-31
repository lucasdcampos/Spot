#include "gameplay/vehicles/vehicle_sounds.h"

#include "gameplay/vehicles/vehicle.h"

#include <algorithm>
#include <cmath>

namespace Gameplay {

SPOT_COMPONENT(VehicleEngineSound);
SPOT_COMPONENT(VehicleTireSound);
SPOT_COMPONENT(VehicleHornSound);

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
    if (Spot::Network::HasAuthority(GetEntity())) {
        m_running = vehicle && vehicle->HasDriver() && vehicle->IsOperational();
        if (m_running) {
            m_pitch = std::max(vehicle->GetRpm(), vehicle->GetIdleRpm()) / std::max(recordedRpm, 1.0f);
            m_load = vehicle->GetThrottle();
        }
    }
    if (!m_running) {
        source->Stop();
        return;
    }
    if (!source->IsPlaying()) {
        source->Play();
    }
    source->pitch = m_pitch;
    source->volume = m_volume * (0.5f + 0.5f * m_load);
}

void VehicleEngineSound::Replicate(Spot::Archive& archive) {
    archive.Field("running", m_running);
    archive.Field("pitch", m_pitch);
    archive.Field("load", m_load);
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

void VehicleHornSound::OnUpdate(float /*deltaTime*/) {
    auto* source = GetEntity().GetComponent<Spot::AudioSource>();
    const Vehicle* vehicle = GetEntity().GetComponentInParent<Vehicle>();
    if (!source) {
        return;
    }
    if (Spot::Network::HasAuthority(GetEntity())) {
        m_sounding = vehicle && vehicle->IsOperational() && vehicle->GetInput().horn;
    }
    if (!m_sounding) {
        source->Stop();
    } else if (!source->IsPlaying()) {
        source->Play();
    }
}

void VehicleHornSound::Replicate(Spot::Archive& archive) {
    archive.Field("sounding", m_sounding);
}

void VehicleHornSound::OnDisable() {
    if (auto* source = GetEntity().GetComponent<Spot::AudioSource>()) {
        source->Stop();
    }
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
    if (Spot::Network::HasAuthority(GetEntity())) {
        m_squeal = vehicle ? std::clamp((vehicle->GetSlip() - 1.0f) / 1.5f, 0.0f, 1.0f) *
                                 std::min(std::abs(vehicle->GetForwardSpeed()) / 4.0f, 1.0f)
                           : 0.0f;
    }
    const float squeal = m_squeal;
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

void VehicleTireSound::Replicate(Spot::Archive& archive) {
    archive.Field("squeal", m_squeal);
}

void VehicleTireSound::OnDisable() {
    if (auto* source = GetEntity().GetComponent<Spot::AudioSource>()) {
        source->Stop();
    }
}

} // namespace Gameplay
