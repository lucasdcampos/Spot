#include "car.h"
#include <spot/scene/physics/physics_world.h>

#include "explosive_barrel.h"
#include "player_health.h"
#include "robot.h"

#include <algorithm>
#include <cmath>

SPOT_COMPONENT(Car);

namespace {

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};
constexpr Spot::Vector3 Forward = {0.0f, 0.0f, -1.0f};
constexpr Spot::Vector3 Right = {1.0f, 0.0f, 0.0f};
// The tires couple the wheels' spin to the road stiffly, so it is stepped several times in each physics step.
constexpr int WheelSteps = 4;
// Slips are measured against at least this speed, as they would grow without bound toward a standstill.
constexpr float MinSlipSpeed = 1.0f;
constexpr float RpmPerRadianPerSecond = 30.0f / Spot::Pi;
constexpr float DrivetrainEfficiency = 0.9f;
// The revolutions the engine's loop was recorded at.
constexpr float EngineSoundRpm = 3000.0f;

// The tires' grip against how far they slip, as Pacejka's magic formula shapes it: rising steeply, peaking at a slip
// of 1 and falling to about 85% as they slide.
constexpr float ShapeFactor = 1.35f;
const float StiffnessFactor = std::tan(Spot::Pi / (2.0f * ShapeFactor));

float GetGrip(float slip) {
    return std::sin(ShapeFactor * std::atan(StiffnessFactor * slip));
}

float MoveToward(float value, float target, float step) {
    return value < target ? std::min(value + step, target) : std::max(value - step, target);
}

// Slows the wheel's spin by the amount, a brake's torque over a step, without turning it the other way.
void Brake(float& spin, float amount) {
    spin = std::abs(spin) <= amount ? 0.0f : spin - std::copysign(amount, spin);
}

} // namespace

void Car::SetDriver(Spot::EntityId driver) {
    if (driver == m_driver) {
        return;
    }
    m_driver = driver;
    m_controls = {};
    m_gear = m_nextGear = 1;
    m_shiftLeft = 0.0f;
    m_rpm = HasDriver() ? idleRpm : 0.0f;
    if (Spot::Entity* entity = GetScene().GetEntity(camera)) {
        entity->SetActive(HasDriver());
    }
    if (auto* body = GetEntity().GetComponent<Spot::Rigidbody>()) {
        body->WakeUp();
    }
    UpdateSounds();
}

Spot::Vector3 Car::GetSeatPosition() const {
    const Spot::Transform& transform = GetEntity().GetTransform();
    return transform.GetWorldPosition() + transform.GetWorldRotation() * seat;
}

Spot::Vector3 Car::GetWheelRest(const Wheel& wheel) const {
    Spot::Vector3 position = wheel.front ? frontWheel : rearWheel;
    position.x = wheel.left ? -std::abs(position.x) : std::abs(position.x);
    return position;
}

bool Car::IsDriven(const Wheel& wheel) const {
    return drive == Drive::All || (drive == Drive::Front) == wheel.front;
}

float Car::GetRatio(int gear) const {
    if (gear < 0) {
        return -reverseGear * finalDrive;
    }
    return gear > 0 && gear <= static_cast<int>(gears.size()) ? gears[static_cast<std::size_t>(gear - 1)] * finalDrive
                                                              : 0.0f;
}

float Car::GetEngineTorque(float rpm) const {
    // Falling away on both sides of its peak, less steeply toward the redline.
    const bool below = rpm < peakTorqueRpm;
    const float t = below ? (peakTorqueRpm - rpm) / std::max(peakTorqueRpm - idleRpm, 1.0f)
                          : (rpm - peakTorqueRpm) / std::max(redlineRpm - peakTorqueRpm, 1.0f);
    return peakTorque * std::max(1.0f - (below ? 0.45f : 0.3f) * t * t, 0.0f);
}

void Car::OnStart() {
    for (std::size_t i = 0; i < m_wheels.size(); ++i) {
        m_wheels[i].front = i < 2;
        m_wheels[i].left = i % 2 == 0;
    }
    // As the physics weighs the body's boxes, by their volume.
    float volume = 0.0f;
    Spot::Vector3 sum;
    for (const Spot::BoxCollider* box : GetEntity().GetComponents<Spot::BoxCollider>()) {
        const Spot::Vector3 size = box->GetSize();
        const float boxVolume = size.x * size.y * size.z;
        sum += box->GetCenter() * boxVolume;
        volume += boxVolume;
    }
    m_centerOfMass = volume > 0.0f ? sum / volume : Spot::Vector3{};
    if (const Spot::AudioSource* engine = GetSound(engineSound)) {
        m_engineVolume = engine->volume;
    }
    if (const Spot::AudioSource* tires = GetSound(tireSound)) {
        m_tireVolume = tires->volume;
    }
}

void Car::OnDisable() {
    for (const Spot::EntityId id : {engineSound, tireSound}) {
        if (Spot::AudioSource* source = GetSound(id)) {
            source->Stop();
        }
    }
}

void Car::OnCollisionEnter(const Spot::Collision& collision) {
    const float speed = std::max(-Spot::Dot(collision.relativeVelocity, collision.normal), 0.0f);
    Spot::Entity* entity = GetScene().GetEntity(collision.other);
    if (!entity || collision.other == m_driver || speed < 2.0f) {
        return;
    }
    const Spot::Vector3 point =
        collision.pointCount > 0 ? collision.points[0] : entity->GetTransform().GetWorldPosition();
    const Spot::Vector3 impulse = -collision.normal * std::max(collision.impulse, speed * 80.0f);
    Robots::Hit(GetScene(), collision.other, impulse, point);
    if (auto* barrel = entity->GetComponentInParent<ExplosiveBarrel>()) {
        barrel->Ignite();
    }
    if (auto* health = entity->GetComponentInParent<PlayerHealth>()) {
        health->Damage(speed * 10.0f);
    }
}

void Car::UpdateSteering(float deltaTime) {
    const float target = std::clamp(m_controls.steer, -1.0f, 1.0f);
    // The wheel comes back to the middle faster than it is turned, as it returns by itself.
    const bool returning = std::abs(target) < std::abs(m_steer) || target * m_steer < 0.0f;
    m_steer = MoveToward(m_steer, target, steerSpeed * (returning ? 2.0f : 1.0f) * deltaTime);
    // A turn around Y, positive to the left.
    const float angle = -m_steer * maxSteerAngle / (1.0f + std::abs(m_speed) / std::max(steerFalloffSpeed, 0.1f));
    const float wheelBase = std::max(rearWheel.z - frontWheel.z, 0.1f);
    const float track = 2.0f * std::abs(frontWheel.x);
    for (Wheel& wheel : m_wheels) {
        if (!wheel.front || std::abs(angle) < 1e-4f) {
            wheel.steer = wheel.front ? angle : 0.0f;
            continue;
        }
        // Both front wheels turn around the same point, on the line of the rear axle, so the inner one turns more.
        const float radius = wheelBase / std::tan(std::abs(angle));
        const bool inner = (angle > 0.0f) == wheel.left;
        wheel.steer = std::copysign(std::atan(wheelBase / (radius + (inner ? -0.5f : 0.5f) * track)), angle);
    }
}

void Car::UpdateGearbox(float deltaTime, float& brake) {
    // Parked, its wheels are held.
    brake = HasDriver() ? 0.0f : 1.0f;
    m_throttle = 0.0f;
    if (!HasDriver()) {
        return;
    }
    if (m_shiftLeft > 0.0f) {
        m_shiftLeft -= deltaTime;
        if (m_shiftLeft <= 0.0f) {
            m_gear = m_nextGear;
        }
    }
    const auto shift = [this](int gear) {
        m_nextGear = gear;
        m_shiftLeft = shiftTime;
    };
    const float pedals = std::clamp(m_controls.drive, -1.0f, 1.0f);
    // Into reverse and back only nearly stopped; until then, the other way brakes.
    if (m_shiftLeft <= 0.0f && m_gear > 0 && pedals < -0.1f && m_speed < 0.5f) {
        shift(-1);
    } else if (m_shiftLeft <= 0.0f && m_gear < 0 && pedals > 0.1f && m_speed > -0.5f) {
        shift(1);
    }
    const bool reverse = (m_shiftLeft > 0.0f ? m_nextGear : m_gear) < 0;
    m_throttle = std::max(reverse ? -pedals : pedals, 0.0f);
    brake = std::max(reverse ? pedals : -pedals, 0.0f);
    // With the pedals left alone, the brakes hold a car that has stopped, as on a hill.
    if (std::abs(pedals) < 0.05f && std::abs(m_speed) < 0.5f) {
        brake = std::max(brake, 0.4f);
    }
    if (m_gear <= 0 || m_shiftLeft > 0.0f) {
        return;
    }
    float spin = 0.0f;
    int driven = 0;
    for (const Wheel& wheel : m_wheels) {
        if (IsDriven(wheel)) {
            spin += wheel.spin;
            ++driven;
        }
    }
    const float rpm =
        std::abs(spin / static_cast<float>(std::max(driven, 1)) * GetRatio(m_gear)) * RpmPerRadianPerSecond;
    // Later the harder the engine is pushed, and down again as it slows, never so low that it would shift right back.
    const float upshift = std::lerp(2800.0f, redlineRpm * 0.93f, m_throttle);
    if (m_gear < static_cast<int>(gears.size()) && rpm > upshift) {
        shift(m_gear + 1);
    } else if (m_gear > 1 && rpm < std::lerp(1500.0f, 3600.0f, m_throttle) &&
               rpm * GetRatio(m_gear - 1) / GetRatio(m_gear) < upshift * 0.85f) {
        shift(m_gear - 1);
    }
}

void Car::OnFixedUpdate(float deltaTime) {
    auto* body = GetEntity().GetComponent<Spot::Rigidbody>();
    if (!body || body->IsKinematic() || deltaTime <= 0.0f || (!HasDriver() && body->IsSleeping())) {
        return;
    }
    auto& physics = GetScene().GetOrAddSystem<Spot::PhysicsWorld>();
    const Spot::Transform& transform = GetEntity().GetTransform();
    const Spot::Quaternion rotation = transform.GetWorldRotation();
    const Spot::Vector3 origin = transform.GetWorldPosition();
    const Spot::Vector3 up = rotation * Up;
    const Spot::Vector3 forward = rotation * Forward;
    const Spot::Vector3 center = origin + rotation * m_centerOfMass;
    const Spot::Vector3 velocity = body->GetLinearVelocity();
    const Spot::Vector3 angular = body->GetAngularVelocity();
    const Spot::Vector3 gravity = physics.GetGravity();
    const float g = std::max(Spot::Length(gravity), 0.01f);
    const float mass = body->GetMass();
    const auto velocityAt = [&](Spot::Vector3 point) { return velocity + Spot::Cross(angular, point - center); };
    m_speed = Spot::Dot(velocity, forward);

    UpdateSteering(deltaTime);
    float brake = 0.0f;
    UpdateGearbox(deltaTime, brake);

    // Each wheel's share of the weight at rest, by how far the center of mass is from its axle.
    const float wheelBase = std::max(rearWheel.z - frontWheel.z, 0.1f);
    const float frontShare = std::clamp((rearWheel.z - m_centerOfMass.z) / wheelBase, 0.0f, 1.0f);
    std::array<float, 4> restLoads{};

    // The springs: each wheel is cast down from the top of its travel, as a sphere as wide as its tire at its bottom.
    std::array<Spot::RaycastHit, 4> contacts{};
    std::array<float, 4> loads{};
    const float tire = wheelWidth * 0.5f;
    for (std::size_t i = 0; i < m_wheels.size(); ++i) {
        Wheel& wheel = m_wheels[i];
        restLoads[i] = mass * g * (wheel.front ? frontShare : 1.0f - frontShare) * 0.5f;
        const Spot::Vector3 top =
            origin + rotation * (GetWheelRest(wheel) + Up * bumpTravel) - up * (wheelRadius - tire);
        const auto hit = physics.SphereCast(top, tire, -up, bumpTravel + droopTravel, {.ignore = GetEntity().GetId()});
        wheel.grounded = hit.has_value();
        wheel.compression = hit ? bumpTravel - hit->distance : -droopTravel;
        if (!hit) {
            continue;
        }
        contacts[i] = *hit;
        // Springs are set to hold the car at rest where its wheels are placed.
        const float rate = -Spot::Dot(velocityAt(hit->point), up);
        loads[i] = restLoads[i] + springRate * wheel.compression + damping * rate;
        // The bump stop stiffens the last of the travel.
        const float stop = wheel.compression - bumpTravel * 0.75f;
        loads[i] += stop > 0.0f ? springRate * 8.0f * stop : 0.0f;
    }
    for (std::size_t axle = 0; axle < m_wheels.size(); axle += 2) {
        const float roll = antiRoll * (m_wheels[axle].compression - m_wheels[axle + 1].compression);
        loads[axle] += roll;
        loads[axle + 1] -= roll;
    }

    // The engine, through the clutch, which slips below the revolutions it takes up the drive at, and the gears.
    const float ratio = GetRatio(m_shiftLeft > 0.0f ? 0 : m_gear);
    int driven = 0;
    float drivenSpin = 0.0f;
    for (const Wheel& wheel : m_wheels) {
        if (IsDriven(wheel)) {
            drivenSpin += wheel.spin;
            ++driven;
        }
    }
    drivenSpin /= static_cast<float>(std::max(driven, 1));
    const float wheelRpm = std::abs(drivenSpin * ratio) * RpmPerRadianPerSecond;
    const float clutchRpm = idleRpm + m_throttle * 1600.0f;
    const bool coupled = ratio != 0.0f && wheelRpm > clutchRpm;
    float engineTorque = 0.0f;
    if (!HasDriver()) {
        m_rpm = 0.0f;
    } else if (ratio == 0.0f) {
        // Free while shifting, falling toward what the next gear will turn it at.
        const float next = std::abs(drivenSpin * GetRatio(m_nextGear)) * RpmPerRadianPerSecond;
        m_rpm += (std::max(next, clutchRpm) - m_rpm) * (1.0f - std::exp(-12.0f * deltaTime));
    } else {
        m_rpm = std::min(std::max(wheelRpm, clutchRpm), redlineRpm);
        // The limiter cuts the fuel at the redline; off the throttle, the engine holds the car back.
        engineTorque = m_rpm >= redlineRpm ? 0.0f : m_throttle * GetEngineTorque(m_rpm);
        if (coupled) {
            engineTorque -= std::copysign(engineBraking * (1.0f - m_throttle) * m_rpm / redlineRpm, drivenSpin * ratio);
        }
    }
    const float wheelDrive = engineTorque * ratio * DrivetrainEfficiency / static_cast<float>(std::max(driven, 1));
    const float drivenInertia =
        wheelInertia + (coupled ? engineInertia * ratio * ratio / static_cast<float>(std::max(driven, 1)) : 0.0f);

    // The wheels' forces, gathered around where the car is now, which its body may not have followed yet.
    Spot::Vector3 force = velocity * (-dragCoefficient * Spot::Length(velocity));
    Spot::Vector3 torque;
    const auto apply = [&](Spot::Vector3 push, Spot::Vector3 point) {
        force += push;
        torque += Spot::Cross(point - center, push);
    };

    // The tires, gripping by how far they slip along and across, together within the friction their load gives.
    const float step = deltaTime / static_cast<float>(WheelSteps);
    const float tanPeakAngle = std::tan(std::max(peakSlipAngle, 0.01f));
    const float peakRatio = std::max(peakSlipRatio, 0.01f);
    for (std::size_t i = 0; i < m_wheels.size(); ++i) {
        Wheel& wheel = m_wheels[i];
        wheel.load = wheel.grounded ? std::max(loads[i], 0.0f) : 0.0f;
        const bool isDriven = IsDriven(wheel);
        const float driveTorque = isDriven ? wheelDrive : 0.0f;
        const float inertia = isDriven ? drivenInertia : wheelInertia;
        const float pedal = brakeTorque * brake * (wheel.front ? brakeBias : 1.0f - brakeBias) * 0.5f;
        const float held = (!wheel.front && m_controls.handbrake ? handbrakeTorque : 0.0f) +
                           rollingResistance * wheel.load * wheelRadius;
        wheel.slip = 0.0f;
        if (!wheel.grounded) {
            for (int sub = 0; sub < WheelSteps; ++sub) {
                wheel.spin += driveTorque / inertia * step;
                Brake(wheel.spin, (pedal + held) / inertia * step);
            }
            continue;
        }
        const Spot::RaycastHit& contact = contacts[i];
        const Spot::Vector3 heading = Spot::Quaternion::FromAxisAngle(up, wheel.steer) * forward;
        const Spot::Vector3 along = Spot::Normalize(heading - contact.normal * Spot::Dot(heading, contact.normal));
        const Spot::Vector3 across = Spot::Cross(along, contact.normal);
        const Spot::Vector3 pointVelocity = velocityAt(contact.point);
        const float speedAlong = Spot::Dot(pointVelocity, along);
        const float speedAcross = Spot::Dot(pointVelocity, across);
        const float scale = std::max(std::abs(speedAlong), MinSlipSpeed);
        const float friction =
            grip * std::clamp(1.0f - loadSensitivity * (wheel.load / std::max(restLoads[i], 1.0f) - 1.0f), 0.4f, 1.3f) *
            wheel.load;
        // No more than the forces that would stop the tire sliding within the step, gravity along the ground
        // included, so they never push it back the other way, and a stopped car does not creep.
        const float share = wheel.load / g;
        const float holdAcross = share * std::abs(speedAcross / deltaTime + Spot::Dot(gravity, across));
        const float holdAlong = share * std::abs(speedAlong / deltaTime + Spot::Dot(gravity, along));
        // How much the force along changes with the spin, at most, which steps the spin implicitly, so stably.
        const float stiffness = friction * StiffnessFactor * ShapeFactor * wheelRadius / (peakRatio * scale);
        float forceAlong = 0.0f;
        float forceAcross = 0.0f;
        for (int sub = 0; sub < WheelSteps; ++sub) {
            const float slipAlong = (wheel.spin * wheelRadius - speedAlong) / scale / peakRatio;
            const float slipAcross = speedAcross / scale / tanPeakAngle;
            const float slip = std::hypot(slipAlong, slipAcross);
            const float total = friction * GetGrip(slip);
            float x = slip > 1e-6f ? total * slipAlong / slip : 0.0f;
            const float y = slip > 1e-6f ? std::clamp(-total * slipAcross / slip, -holdAcross, holdAcross) : 0.0f;
            // Anti-lock lets a brake that locks its wheel go, until it rolls again.
            const bool locking = antiLock && slipAlong < -1.5f && std::abs(speedAlong) > 2.0f;
            const float brakes = held + (locking ? pedal * 0.2f : pedal);
            // Stepped implicitly, the tire's force as it grips at the spin the wheel ends the step with. The brakes
            // hold the wheel still when they can, and otherwise slow it by all they have.
            const float response = 1.0f + step * stiffness * wheelRadius / inertia;
            const float holding =
                driveTorque - wheelRadius * (x - stiffness * wheel.spin) + inertia * wheel.spin / step;
            float spin = 0.0f;
            if (std::abs(holding) > brakes) {
                const float braked = driveTorque - x * wheelRadius - std::copysign(brakes, holding);
                spin = wheel.spin + step * braked / inertia / response;
            }
            x = std::clamp(x + stiffness * (spin - wheel.spin), -friction, friction);
            if (spin == 0.0f) {
                x = std::clamp(x, -holdAlong, holdAlong);
            }
            wheel.spin = spin;
            forceAlong += x;
            forceAcross += y;
            wheel.slip = std::max(wheel.slip, slip);
        }
        forceAlong /= static_cast<float>(WheelSteps);
        forceAcross /= static_cast<float>(WheelSteps);

        const Spot::Vector3 support = up * wheel.load;
        const Spot::Vector3 traction = along * forceAlong + across * forceAcross;
        apply(support, contact.point);
        apply(traction, contact.point + up * rollCenterHeight);
        // What the tire pushes, it pushes back, such as a prop it runs over.
        Spot::Entity* ground = GetScene().GetEntity(contact.entity);
        auto* other = ground ? ground->GetComponent<Spot::Rigidbody>() : nullptr;
        if (other && !other->IsKinematic()) {
            other->AddForceAtPosition(-(support + traction), contact.point);
        }
    }
    for (Wheel& wheel : m_wheels) {
        wheel.angle = std::fmod(wheel.angle + wheel.spin * deltaTime, 2.0f * Spot::Pi);
    }
    body->AddForce(force);
    body->AddTorque(torque);

    // Parked and still, the car sleeps, as bodies at rest do, until something moves it.
    const bool still = !HasDriver() && Spot::Length(velocity) < 0.05f && Spot::Length(angular) < 0.05f;
    m_still = still ? m_still + deltaTime : 0.0f;
    if (m_still > 1.0f) {
        body->Sleep();
        for (Wheel& wheel : m_wheels) {
            wheel.spin = 0.0f;
        }
    }
}

void Car::OnUpdate(float /*deltaTime*/) {
    UpdateSounds();
}

void Car::OnLateUpdate(float /*deltaTime*/) {
    const std::array<Spot::EntityId, 4> entities = {frontLeft, frontRight, rearLeft, rearRight};
    for (std::size_t i = 0; i < m_wheels.size(); ++i) {
        Spot::Entity* entity = GetScene().GetEntity(entities[i]);
        if (!entity) {
            continue;
        }
        const Wheel& wheel = m_wheels[i];
        Spot::Transform& transform = entity->GetTransform();
        transform.SetPosition(GetWheelRest(wheel) + Up * std::clamp(wheel.compression, -droopTravel, bumpTravel));
        // Rolling forward turns the top of the wheel forward; left wheels are the right one turned around.
        const Spot::Quaternion side = Spot::Quaternion::FromAxisAngle(Up, wheel.left ? Spot::Pi : 0.0f);
        transform.SetRotation(Spot::Quaternion::FromAxisAngle(Up, wheel.steer) *
                              Spot::Quaternion::FromAxisAngle(Right, -wheel.angle) * side);
    }
}

void Car::UpdateSounds() {
    if (Spot::AudioSource* engine = GetSound(engineSound)) {
        if (!HasDriver()) {
            engine->Stop();
        } else {
            if (!engine->IsPlaying()) {
                engine->Play();
            }
            engine->pitch = std::max(m_rpm, idleRpm) / EngineSoundRpm;
            engine->volume = m_engineVolume * (0.5f + 0.5f * m_throttle);
        }
    }
    if (Spot::AudioSource* tires = GetSound(tireSound)) {
        float slip = 0.0f;
        for (const Wheel& wheel : m_wheels) {
            slip = std::max(slip, wheel.grounded ? wheel.slip : 0.0f);
        }
        // Tires squeal sliding past their peak, and not while barely moving.
        const float squeal = std::clamp((slip - 1.0f) / 1.5f, 0.0f, 1.0f) * std::min(std::abs(m_speed) / 4.0f, 1.0f);
        if (squeal < 0.02f) {
            tires->Stop();
        } else {
            if (!tires->IsPlaying()) {
                tires->Play();
            }
            tires->volume = m_tireVolume * squeal;
            tires->pitch = 0.9f + 0.2f * squeal;
        }
    }
}

Spot::AudioSource* Car::GetSound(Spot::EntityId entity) const {
    Spot::Entity* found = GetScene().GetEntity(entity);
    return found ? found->GetComponent<Spot::AudioSource>() : nullptr;
}

void Car::Serialize(Spot::Archive& archive) {
    static constexpr std::array<std::string_view, 3> Drives = {"Front", "Rear", "All"};
    archive.Field("frontWheel", frontWheel,
                  {.tooltip = "The front right wheel's center at rest; the left one mirrors it"});
    archive.Field("rearWheel", rearWheel,
                  {.tooltip = "The rear right wheel's center at rest; the left one mirrors it"});
    archive.Field("wheelRadius", wheelRadius, {.min = 0.05f, .max = 2.0f, .speed = 0.005f});
    archive.Field("wheelWidth", wheelWidth, {.min = 0.02f, .max = 1.0f, .speed = 0.005f});
    archive.Field("wheelInertia", wheelInertia, {.min = 0.01f, .max = 100.0f, .speed = 0.01f, .tooltip = "kg m²"});
    archive.Field("seat", seat, {.tooltip = "Where the driver sits, from the car"});
    archive.Field("springRate", springRate, {.min = 0.0f, .max = 1000000.0f, .speed = 100.0f, .tooltip = "N/m"});
    archive.Field("damping", damping, {.min = 0.0f, .max = 100000.0f, .speed = 10.0f, .tooltip = "N s/m"});
    archive.Field("bumpTravel", bumpTravel, {.min = 0.0f, .max = 1.0f, .speed = 0.005f});
    archive.Field("droopTravel", droopTravel, {.min = 0.0f, .max = 1.0f, .speed = 0.005f});
    archive.Field("antiRoll", antiRoll, {.min = 0.0f, .max = 1000000.0f, .speed = 100.0f, .tooltip = "N/m"});
    archive.Field("rollCenterHeight", rollCenterHeight,
                  {.min = 0.0f, .max = 2.0f, .speed = 0.005f, .tooltip = "Where the tires' forces reach the body"});
    archive.Field("grip", grip, {.min = 0.0f, .max = 3.0f, .speed = 0.01f, .tooltip = "Peak friction coefficient"});
    archive.Field("loadSensitivity", loadSensitivity, {.min = 0.0f, .max = 1.0f, .speed = 0.005f});
    archive.Field("peakSlipRatio", peakSlipRatio, {.min = 0.01f, .max = 1.0f, .speed = 0.005f});
    archive.Field("peakSlipAngle", peakSlipAngle, {.min = 0.01f, .max = 0.5f, .speed = 0.002f, .angle = true});
    archive.Field("rollingResistance", rollingResistance, {.min = 0.0f, .max = 0.2f, .speed = 0.001f});
    archive.Field("maxSteerAngle", maxSteerAngle, {.min = 0.0f, .max = 1.2f, .speed = 0.005f, .angle = true});
    archive.Field("steerFalloffSpeed", steerFalloffSpeed,
                  {.min = 0.1f, .max = 200.0f, .speed = 0.1f, .tooltip = "Speed at which steering is halved"});
    archive.Field("steerSpeed", steerSpeed, {.min = 0.1f, .max = 20.0f, .speed = 0.05f});
    archive.Field("drive", drive, {.enumNames = Drives});
    archive.Field("peakTorque", peakTorque, {.min = 0.0f, .max = 5000.0f, .speed = 1.0f, .tooltip = "N m"});
    archive.Field("idleRpm", idleRpm, {.min = 100.0f, .max = 5000.0f, .speed = 10.0f});
    archive.Field("peakTorqueRpm", peakTorqueRpm, {.min = 100.0f, .max = 20000.0f, .speed = 10.0f});
    archive.Field("redlineRpm", redlineRpm, {.min = 500.0f, .max = 20000.0f, .speed = 10.0f});
    archive.Field("engineBraking", engineBraking, {.min = 0.0f, .max = 1000.0f, .speed = 1.0f, .tooltip = "N m"});
    archive.Field("engineInertia", engineInertia, {.min = 0.0f, .max = 10.0f, .speed = 0.005f, .tooltip = "kg m²"});
    archive.Field("gears", gears);
    archive.Field("reverseGear", reverseGear, {.min = 0.1f, .max = 10.0f, .speed = 0.01f});
    archive.Field("finalDrive", finalDrive, {.min = 0.1f, .max = 20.0f, .speed = 0.01f});
    archive.Field("shiftTime", shiftTime, {.min = 0.0f, .max = 2.0f, .speed = 0.01f});
    archive.Field("brakeTorque", brakeTorque,
                  {.min = 0.0f, .max = 100000.0f, .speed = 10.0f, .tooltip = "N m, for all four wheels"});
    archive.Field("brakeBias", brakeBias, {.min = 0.0f, .max = 1.0f, .speed = 0.01f, .tooltip = "The front's share"});
    archive.Field("handbrakeTorque", handbrakeTorque,
                  {.min = 0.0f, .max = 100000.0f, .speed = 10.0f, .tooltip = "N m, on each rear wheel"});
    archive.Field("antiLock", antiLock);
    archive.Field("dragCoefficient", dragCoefficient,
                  {.min = 0.0f, .max = 10.0f, .speed = 0.005f, .tooltip = "Half the air's density times Cd A"});
    archive.Field("frontLeft", frontLeft);
    archive.Field("frontRight", frontRight);
    archive.Field("rearLeft", rearLeft);
    archive.Field("rearRight", rearRight);
    archive.Field("camera", camera, {.tooltip = "Inactive until someone drives; its Camera follows the car"});
    archive.Field("engineSound", engineSound);
    archive.Field("tireSound", tireSound);
}
