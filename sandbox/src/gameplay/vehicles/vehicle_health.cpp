#include "gameplay/vehicles/vehicle_health.h"

#include "gameplay/core/controller.h"
#include "gameplay/vehicles/vehicle.h"
#include "gameplay/vehicles/vehicle_seat.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string_view>

namespace Gameplay {

SPOT_COMPONENT(VehicleHealth);

namespace {

// From the machine of the player driving it: a hard hit there, where the vehicle is simulated.
struct ImpactMessage {
    static constexpr std::string_view NetName = "Gameplay.VehicleImpact";
    float amount = 0.0f;
    Spot::Vector3 point;

    void Serialize(Spot::Archive& archive) {
        archive.Field("amount", amount);
        archive.Field("point", point);
    }
};

// On servers, hurts the vehicles their drivers' machines tell of hitting something.
class ImpactSystem final : public Spot::SceneSystem {
protected:
    void OnUpdate(float /*deltaTime*/) override {
        Spot::NetworkSystem* system = Spot::Network::Get(GetScene());
        if (!system || system == m_bound || !system->IsServer()) {
            return;
        }
        m_bound = system;
        system->OnObjectMessage<ImpactMessage>(
            [](Spot::NetworkObject& object, Spot::NetPeerId from, ImpactMessage& message) {
                auto* health = object.GetEntity().GetComponent<VehicleHealth>();
                if (!health || object.GetOwner() != from || !std::isfinite(message.amount)) {
                    return;
                }
                Damage::Apply(object.GetScene(), object.GetEntity().GetId(),
                              {.amount = std::clamp(message.amount, 0.0f, health->maxHealth),
                               .type = "Impact",
                               .point = message.point,
                               .pushesBody = false});
            });
    }

private:
    const Spot::NetworkSystem* m_bound = nullptr;
};

// Wrecks also go dark: no one can get in to switch their lights back on.
void Burn(Spot::Entity& entity, const Spot::AssetRef<Spot::Material>& material) {
    entity.ForEachComponent<Spot::MeshRenderer>([&](Spot::MeshRenderer& renderer) {
        renderer.SetMaterial(material);
        renderer.SetTextureSpace(Spot::TextureSpace::Object);
    });
    entity.ForEachComponent<Spot::Light>([](Spot::Light& light) { light.SetEnabled(false); });
    entity.ForEachChild([&](Spot::Entity& child) { Burn(child, material); });
}

} // namespace

VehicleHealth::VehicleHealth() {
    maxHealth = 300.0f;
    regenerationRate = 0.0f;
}

void VehicleHealth::OnStart() {
    Health::OnStart();
    GetScene().GetOrAddSystem<ImpactSystem>();
    m_died = OnDied.Connect([this](const DamageInfo& damage) { Explode(damage); });
    if (m_destroyed) {
        TakeDamage({.amount = maxHealth});
    } else if (m_savedHealth >= 0.0f) {
        TakeDamage({.amount = std::max(maxHealth - m_savedHealth, 0.0f)});
    }
}

void VehicleHealth::OnCollisionEnter(const Spot::Collision& collision) {
    const float speed = std::max(-Spot::Dot(collision.relativeVelocity, collision.normal), 0.0f);
    const Spot::Entity* other = GetScene().GetEntity(collision.other);
    if (m_destroyed || !other || other->IsDescendantOf(GetEntity()) || speed <= safeImpactSpeed || IsHarmless(*other)) {
        return;
    }
    const float amount = (speed - safeImpactSpeed) * damagePerSpeed;
    const Spot::Vector3 point = collision.pointCount > 0 ? collision.points[0] : GetTransform().GetWorldPosition();
    // Simulated on its driver's machine, which tells the server, whose the vehicle's health is.
    if (Spot::NetworkSystem* system = Spot::Network::Get(GetScene()); system && system->IsClient()) {
        if (Spot::NetworkObject* object = Spot::Network::GetObject(GetEntity())) {
            system->SendToServer(*object, ImpactMessage{.amount = amount, .point = point});
        }
        return;
    }
    Damage::Apply(GetScene(), GetEntity().GetId(),
                  {.amount = amount,
                   .type = "Impact",
                   .point = point,
                   .source = collision.other,
                   .pushesBody = false});
}

bool VehicleHealth::IsHarmless(const Spot::Entity& other) const {
    const auto* own = GetEntity().GetComponent<Spot::Rigidbody>();
    // Ragdoll bones are children of their character, so the whole chain up decides.
    for (const Spot::Entity* entity = &other; entity; entity = entity->GetParent()) {
        if (entity->GetComponent<Spot::CharacterController>()) {
            return true;
        }
        if (const auto* body = entity->GetComponent<Spot::Rigidbody>()) {
            return own && !body->IsKinematic() && body->GetMass() < own->GetMass() * harmlessMassShare;
        }
    }
    return false;
}

void VehicleHealth::ApplyWreck() {
    if (auto* vehicle = GetEntity().GetComponent<Vehicle>()) {
        vehicle->SetState("Destroyed", true);
        vehicle->SetInput({});
        if (auto* seat = vehicle->GetSeat()) {
            seat->Exit();
        }
        if (auto* controller = vehicle->GetController()) {
            controller->Unpossess();
        }
    }
    auto material = burnedMaterial;
    if (!material) {
        material = std::make_shared<const Spot::Material>(
            Spot::Material{.baseColor = {0.09f, 0.075f, 0.06f}, .metallic = 0.05f, .roughness = 0.98f});
    }
    Burn(GetEntity(), material);
}

void VehicleHealth::Explode(const DamageInfo& damage) {
    const bool exploded = m_destroyed;
    m_destroyed = true;
    ApplyWreck();
    // The blast is the server's, whose effects and shakes its clients are shown.
    if (!exploded && Spot::Network::IsServer(GetScene())) {
        if (auto* body = GetEntity().GetComponent<Spot::Rigidbody>(); body && !body->IsKinematic()) {
            const auto& transform = GetTransform();
            body->WakeUp();
            body->AddImpulseAtPosition(Spot::Vector3{0.0f, body->GetMass() * std::max(wreckLaunchSpeed, 0.0f), 0.0f},
                                       transform.GetWorldPosition() +
                                           transform.GetWorldRotation() * Spot::Vector3{0.2f, 0.5f, -0.6f});
        }
        // The static blast keeps the source entity and ignores it while damaging its surroundings.
        Explosive::Explode(GetScene(), GetTransform().GetWorldPosition() + Spot::Vector3{0.0f, 0.8f, 0.0f}, blast,
                           GetEntity().GetId(), damage.instigator);
    }
}

void VehicleHealth::Serialize(Spot::Archive& archive) {
    Health::Serialize(archive);
    archive.Field("blast", blast);
    archive.Field(
        "wreckLaunchSpeed", wreckLaunchSpeed,
        {.min = 0.0f, .max = 50.0f, .speed = 0.1f, .tooltip = "Upward speed added once when it explodes, in m/s"});
    archive.Field("burnedMaterial", burnedMaterial);
    archive.Field("safeImpactSpeed", safeImpactSpeed, {.min = 0.0f, .max = 100.0f, .speed = 0.1f});
    archive.Field("damagePerSpeed", damagePerSpeed, {.min = 0.0f, .max = 1000.0f, .speed = 0.5f});
    archive.Field("harmlessMassShare", harmlessMassShare,
                  {.min = 0.0f,
                   .max = 1.0f,
                   .speed = 0.01f,
                   .tooltip = "Lighter bodies than this share of the vehicle's mass do no impact damage"});
    if (archive.IsSaving() && HasStarted()) {
        m_savedHealth = GetHealth();
    }
    archive.Field("health", m_savedHealth, {.hidden = true});
    archive.Field("destroyed", m_destroyed, {.hidden = true});
}

} // namespace Gameplay
