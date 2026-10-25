#include "car_driver.h"
#include <spot/scene/physics/physics_world.h>

#include "car.h"
#include "physics_gun.h"
#include "player_controller.h"
#include "player_health.h"
#include "player_movement.h"
#include "prop_carrier.h"
#include "prop_spawner.h"
#include "view_model.h"
#include "weapon_holder.h"

#include <array>
#include <cmath>

SPOT_COMPONENT(CarDriver);

namespace {

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};

} // namespace

Car* CarDriver::GetCar() const {
    Spot::Entity* entity = GetScene().GetEntity(m_car);
    return entity ? entity->GetComponent<Car>() : nullptr;
}

Car* CarDriver::FindCar() const {
    const auto* player = GetEntity().GetComponent<PlayerController>();
    const auto hit = player ? GetScene().GetOrAddSystem<Spot::PhysicsWorld>().Raycast(player->GetEyePosition(),
                                                                                      player->GetAimDirection(), reach,
                                                                                      {.ignore = GetEntity().GetId()})
                            : std::nullopt;
    const Spot::Entity* entity = hit ? GetScene().GetEntity(hit->entity) : nullptr;
    Car* car = entity ? entity->GetComponentInParent<Car>() : nullptr;
    return car && car->IsActiveAndEnabled() && !car->HasDriver() ? car : nullptr;
}

void CarDriver::Enter(Car& car) {
    if (IsDriving() || car.HasDriver()) {
        return;
    }
    if (auto* movement = GetEntity().GetComponent<PlayerMovement>(); movement && movement->IsFlying()) {
        movement->SetFlying(false);
    }
    SetOnFoot(false);
    m_car = car.GetEntity().GetId();
    car.SetDriver(GetEntity().GetId());
    GetEntity().GetTransform().SetWorldPosition(car.GetSeatPosition());
}

void CarDriver::Exit() {
    if (!IsDriving()) {
        return;
    }
    Car* car = GetCar();
    m_car = Spot::EntityId::Invalid;
    Spot::Transform& transform = GetEntity().GetTransform();
    if (car) {
        car->SetDriver(Spot::EntityId::Invalid);
        transform.SetWorldPosition(FindExit(*car));
    }
    SetOnFoot(true);
    if (!car) {
        return;
    }
    const Spot::Vector3 forward = car->GetEntity().GetTransform().GetWorldRotation() * Spot::Vector3{0.0f, 0.0f, -1.0f};
    if (auto* controller = GetEntity().GetComponent<PlayerController>()) {
        controller->SetLook(std::atan2(-forward.x, -forward.z), 0.0f);
    }
    if (auto* movement = GetEntity().GetComponent<PlayerMovement>()) {
        if (const auto* body = car->GetEntity().GetComponent<Spot::Rigidbody>()) {
            movement->Push(body->GetLinearVelocity());
        }
    }
}

Spot::Vector3 CarDriver::FindExit(const Car& car) const {
    const Spot::Transform& transform = car.GetEntity().GetTransform();
    const Spot::Vector3 origin = transform.GetWorldPosition();
    const Spot::Quaternion rotation = transform.GetWorldRotation();
    const auto* controller = GetEntity().GetComponent<Spot::CharacterController>();
    const float radius = controller ? controller->GetRadius() : 0.4f;
    const float height = controller ? controller->GetHeight() : 1.8f;
    // Standing on the ground beside the car, a hand's width from it.
    const float side = std::abs(car.frontWheel.x) + 0.25f + radius;
    const float standing = height * 0.5f + 0.1f;
    const std::array<Spot::Vector3, 4> spots = {
        Spot::Vector3{std::copysign(side, car.seat.x), standing, car.seat.z},
        Spot::Vector3{-std::copysign(side, car.seat.x), standing, car.seat.z},
        Spot::Vector3{0.0f, standing, car.rearWheel.z + 1.1f + radius},
        Spot::Vector3{0.0f, standing, car.frontWheel.z - 1.1f - radius},
    };
    auto& physics = GetScene().GetOrAddSystem<Spot::PhysicsWorld>();
    const Spot::Vector3 halfway = Up * (height * 0.5f - radius);
    for (const Spot::Vector3& spot : spots) {
        const Spot::Vector3 position = origin + rotation * spot;
        if (physics.OverlapSphere(position + halfway, radius * 0.95f).empty() &&
            physics.OverlapSphere(position - halfway, radius * 0.95f).empty()) {
            return position;
        }
    }
    return origin + Up * (2.0f + height);
}

void CarDriver::SetOnFoot(bool onFoot) {
    Spot::Entity& player = GetEntity();
    const auto enable = [onFoot](Spot::Component* component) {
        if (component) {
            component->SetEnabled(onFoot);
        }
    };
    enable(player.GetComponent<Spot::CharacterController>());
    enable(player.GetComponent<PlayerMovement>());
    enable(player.GetComponent<PropCarrier>());
    enable(player.GetComponent<PhysicsGun>());
    enable(player.GetComponent<WeaponHolder>());
    enable(player.GetComponent<PropSpawner>());
    enable(player.GetComponent<ViewModel>());
    if (auto* controller = player.GetComponent<PlayerController>()) {
        enable(controller);
        if (Spot::Entity* head = GetScene().GetEntity(controller->head)) {
            enable(head->GetComponent<Spot::Camera>());
        }
    }
}

void CarDriver::OnDisable() {
    // Only lets the car go: what the player does on foot is not brought back by a driver being put away.
    if (Car* car = GetCar()) {
        car->SetDriver(Spot::EntityId::Invalid);
    }
    m_car = Spot::EntityId::Invalid;
}

void CarDriver::OnUpdate(float /*deltaTime*/) {
    namespace Input = Spot::Input;
    m_tookUse = false;
    const bool use = Input::IsActionPressed("Use");
    if (IsDriving()) {
        Car* car = GetCar();
        if (!car || !car->IsActiveAndEnabled()) {
            Exit();
            return;
        }
        const Spot::Vector2 move = Input::GetActionVector2("Move");
        car->SetControls({.drive = move.y, .steer = move.x, .handbrake = Input::IsActionDown("Jump")});
        if (use) {
            m_tookUse = true;
            Exit();
        }
        return;
    }
    const auto* health = GetEntity().GetComponent<PlayerHealth>();
    const auto* carrier = GetEntity().GetComponent<PropCarrier>();
    const auto* gun = GetEntity().GetComponent<PhysicsGun>();
    if (use && !(health && health->IsDead()) && !(carrier && carrier->IsHolding()) && !(gun && gun->IsGrabbing())) {
        if (Car* car = FindCar()) {
            m_tookUse = true;
            Enter(*car);
        }
    }
}

void CarDriver::OnLateUpdate(float /*deltaTime*/) {
    if (const Car* car = GetCar()) {
        GetEntity().GetTransform().SetWorldPosition(car->GetSeatPosition());
    }
}

void CarDriver::Serialize(Spot::Archive& archive) {
    archive.Field("reach", reach, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
}
