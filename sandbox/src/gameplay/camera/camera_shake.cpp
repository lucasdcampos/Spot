#include "gameplay/camera/camera_shake.h"

#include "gameplay/core/pawn.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace Gameplay {

SPOT_COMPONENT(CameraShake);

namespace {

// A shake a server started, for its clients' views.
struct ShakeMessage {
    static constexpr std::string_view NetName = "Gameplay.Shake";
    Spot::Vector3 center;
    float strength = 0.0f;
    float radius = 0.0f;

    void Serialize(Spot::Archive& archive) {
        archive.Field("center", center);
        archive.Field("strength", strength);
        archive.Field("radius", radius);
    }
};

// On clients, shakes their views as the server says.
class ShakeSystem final : public Spot::SceneSystem {
protected:
    void OnUpdate(float /*deltaTime*/) override {
        const Spot::NetworkSystem* system = Spot::Network::Get(GetScene());
        Spot::NetworkSession* session = system && system->IsClient() ? system->GetSession() : nullptr;
        if (!session || session == m_session) {
            return;
        }
        m_session = session;
        session->GetMessages().On<ShakeMessage>([this](Spot::NetPeerId, ShakeMessage& shake) {
            if (std::isfinite(shake.strength) && std::isfinite(shake.radius)) {
                CameraShake::Add(GetScene(), shake.center, shake.strength, shake.radius);
            }
        });
    }

private:
    const Spot::NetworkSession* m_session = nullptr;
};

} // namespace

void CameraShake::OnStart() {
    GetScene().GetOrAddSystem<ShakeSystem>();
}

void CameraShake::Add(Spot::Scene& scene, Spot::Vector3 center, float strength, float radius) {
    if (!(radius > 0.0f)) {
        return;
    }
    if (Spot::NetworkSystem* system = Spot::Network::Get(scene); system && system->IsServer()) {
        for (const Spot::NetPeerId peer : system->GetReadyPeers()) {
            system->GetSession()->Send(peer, ShakeMessage{center, strength, radius}, Spot::NetDelivery::Unreliable);
        }
    }
    scene.ForEachComponent<CameraShake>([&](CameraShake& shake) {
        const Pawn* pawn = Pawn::Of(shake.GetEntity());
        const Spot::Vector3 view =
            pawn ? pawn->GetViewPoint().position : shake.GetEntity().GetTransform().GetWorldPosition();
        const float closeness = 1.0f - Spot::Distance(view, center) / radius;
        if (closeness > 0.0f) {
            shake.Add(strength * closeness * closeness, 0.4f + 0.5f * closeness);
        }
    });
}

void CameraShake::Add(float radians, float seconds) {
    if (!std::isfinite(radians) || radians <= 0.0f || !std::isfinite(seconds) || seconds <= 0.0f) {
        return;
    }
    const float current = m_length > 0.0f ? m_strength * m_left / m_length : 0.0f;
    if (radians >= current) {
        m_strength = std::min(radians, maxAngle);
        m_length = m_left = seconds;
    }
}

Spot::Quaternion CameraShake::GetRotation() const {
    if (m_left <= 0.0f || m_length <= 0.0f) {
        return {};
    }
    // Dying down, quickly at first; waves out of step on each axis, so it jolts rather than sways.
    const float left = m_left / m_length;
    const float amount = m_strength * left * left;
    const float t = m_clock;
    const float pitch = amount * (0.6f * std::sin(t * 41.0f) + 0.4f * std::sin(t * 23.0f + 1.7f));
    const float yaw = amount * (0.6f * std::sin(t * 37.0f + 0.5f) + 0.4f * std::sin(t * 19.0f + 2.9f));
    const float roll = amount * 0.5f * std::sin(t * 31.0f + 4.1f);
    return Spot::Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, pitch) *
           Spot::Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, yaw) *
           Spot::Quaternion::FromAxisAngle({0.0f, 0.0f, 1.0f}, roll);
}

void CameraShake::OnUpdate(float deltaTime) {
    m_left = std::max(m_left - deltaTime, 0.0f);
    m_clock += deltaTime;
}

void CameraShake::OnDisable() {
    m_left = 0.0f;
}

void CameraShake::Serialize(Spot::Archive& archive) {
    archive.Field("maxAngle", maxAngle,
                  {.min = 0.0f, .max = 1.0f, .speed = 0.005f, .tooltip = "Radians it turns the view by at most"});
}

} // namespace Gameplay
