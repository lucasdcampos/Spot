#include "gameplay/vehicles/wheeled_vehicle.h"
#include <spot/scene/physics/physics_world.h>

#include "gameplay/vehicles/vehicle_wheel.h"

#include <algorithm>
#include <cmath>

namespace Gameplay {

SPOT_COMPONENT(WheeledVehicle);

namespace {

constexpr Spot::Vector3 Up = {0.0f, 1.0f, 0.0f};
constexpr Spot::Vector3 Forward = {0.0f, 0.0f, -1.0f};
// The tires couple the wheels' spin to the road stiffly, so it is stepped several times in each physics step.
constexpr int WheelSteps = 4;
// Slips are measured against at least this speed, as they would grow without bound toward a standstill.
constexpr float MinSlipSpeed = 1.0f;
constexpr float RpmPerRadianPerSecond = 30.0f / Spot::Pi;
constexpr float DrivetrainEfficiency = 0.9f;

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

void CollectWheels(const Spot::Entity& entity, std::vector<VehicleWheel*>& wheels) {
    entity.ForEachChild([&](const Spot::Entity& child) {
        if (auto* wheel = child.GetComponent<VehicleWheel>(); wheel && wheel->IsActiveAndEnabled()) {
            wheels.push_back(wheel);
        }
        CollectWheels(child, wheels);
    });
}

} // namespace

void Drivetrain::Serialize(Spot::Archive& archive) {
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
}

std::vector<VehicleWheel*> WheeledVehicle::GetWheels() const {
    std::vector<VehicleWheel*> wheels;
    CollectWheels(GetEntity(), wheels);
    return wheels;
}

std::vector<Gauge> WheeledVehicle::DescribeTelemetry() const {
    std::vector<Gauge> gauges = Vehicle::DescribeTelemetry();
    gauges.push_back({"Gear", m_gear < 0 ? std::string("R") : std::to_string(m_gear), ""});
    return gauges;
}

float WheeledVehicle::GetSlip() const {
    float slip = 0.0f;
    for (const VehicleWheel* wheel : GetWheels()) {
        slip = std::max(slip, wheel->IsGrounded() ? wheel->GetSlip() : 0.0f);
    }
    return slip;
}

void WheeledVehicle::Reset() {
    m_gear = m_nextGear = 1;
    m_shiftLeft = 0.0f;
    m_rpm = HasDriver() ? drivetrain.idleRpm : 0.0f;
    if (auto* body = GetEntity().GetComponent<Spot::Rigidbody>()) {
        body->WakeUp();
    }
}

void WheeledVehicle::OnPossess(Controller& controller) {
    Vehicle::OnPossess(controller);
    Reset();
    m_rpm = drivetrain.idleRpm;
}

void WheeledVehicle::OnUnpossess() {
    Vehicle::OnUnpossess();
    Reset();
    m_rpm = 0.0f;
}

Spot::Vector3 WheeledVehicle::GetRest(const VehicleWheel& wheel) const {
    const Spot::Entity& entity = wheel.GetEntity();
    if (entity.GetParent() == &GetEntity()) {
        return entity.GetTransform().GetPosition();
    }
    const Spot::Transform& transform = GetEntity().GetTransform();
    const Spot::Quaternion rotation = transform.GetWorldRotation();
    const Spot::Quaternion inverse{-rotation.x, -rotation.y, -rotation.z, rotation.w};
    return inverse * (entity.GetTransform().GetWorldPosition() - transform.GetWorldPosition());
}

float WheeledVehicle::GetRatio(int gear) const {
    if (gear < 0) {
        return -drivetrain.reverseGear * drivetrain.finalDrive;
    }
    const auto& gears = drivetrain.gears;
    return gear > 0 && gear <= static_cast<int>(gears.size())
               ? gears[static_cast<std::size_t>(gear - 1)] * drivetrain.finalDrive
               : 0.0f;
}

float WheeledVehicle::GetEngineTorque(float rpm) const {
    const Drivetrain& engine = drivetrain;
    // Falling away on both sides of its peak, less steeply toward the redline.
    const bool below = rpm < engine.peakTorqueRpm;
    const float t = below ? (engine.peakTorqueRpm - rpm) / std::max(engine.peakTorqueRpm - engine.idleRpm, 1.0f)
                          : (rpm - engine.peakTorqueRpm) / std::max(engine.redlineRpm - engine.peakTorqueRpm, 1.0f);
    return engine.peakTorque * std::max(1.0f - (below ? 0.45f : 0.3f) * t * t, 0.0f);
}

float WheeledVehicle::GetDrivenSpin(const std::vector<VehicleWheel*>& wheels) const {
    float spin = 0.0f;
    float shares = 0.0f;
    for (const VehicleWheel* wheel : wheels) {
        spin += wheel->m_spin * wheel->driveShare;
        shares += wheel->driveShare;
    }
    return shares > 0.0f ? spin / shares : 0.0f;
}

std::vector<float> WheeledVehicle::GetRestShares(const std::vector<VehicleWheel*>& wheels) const {
    // Shares that rise or fall evenly along the vehicle, adding up to the whole weight and balancing it about its
    // center of mass: for two axles, each takes what the lever of the other gives it.
    const float count = static_cast<float>(wheels.size());
    float sum = 0.0f;
    float squares = 0.0f;
    for (const VehicleWheel* wheel : wheels) {
        const float z = GetRest(*wheel).z;
        sum += z;
        squares += z * z;
    }
    const float determinant = count * squares - sum * sum;
    std::vector<float> shares(wheels.size(), count > 0.0f ? 1.0f / count : 0.0f);
    if (std::abs(determinant) < 1e-6f) {
        return shares;
    }
    const float slope = (count * m_centerOfMass.z - sum) / determinant;
    const float base = (1.0f - slope * sum) / count;
    float total = 0.0f;
    for (std::size_t i = 0; i < wheels.size(); ++i) {
        shares[i] = std::max(base + slope * GetRest(*wheels[i]).z, 0.0f);
        total += shares[i];
    }
    for (float& share : shares) {
        share = total > 0.0f ? share / total : 1.0f / count;
    }
    return shares;
}

void WheeledVehicle::OnStart() {
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
    for (VehicleWheel* wheel : GetWheels()) {
        wheel->m_left = GetRest(*wheel).x < 0.0f;
    }
}

void WheeledVehicle::UpdateSteering(float deltaTime, const std::vector<VehicleWheel*>& wheels) {
    const float target = std::clamp(GetInput().steer, -1.0f, 1.0f);
    // The wheel comes back to the middle faster than it is turned, as it returns by itself.
    const bool returning = std::abs(target) < std::abs(m_steer) || target * m_steer < 0.0f;
    m_steer = MoveToward(m_steer, target, steerSpeed * (returning ? 2.0f : 1.0f) * deltaTime);
    const float falloff = 1.0f + std::abs(m_speed) / std::max(steerFalloffSpeed, 0.1f);
    // Steered wheels turn around a point on the line of the unsteered ones, so the inner ones turn more.
    float fixedZ = 0.0f;
    int fixedCount = 0;
    for (const VehicleWheel* wheel : wheels) {
        if (wheel->maxSteer == 0.0f) {
            fixedZ += GetRest(*wheel).z;
            ++fixedCount;
        }
    }
    for (VehicleWheel* wheel : wheels) {
        // A turn around Y, positive to the left.
        const float angle = -m_steer * wheel->maxSteer / falloff;
        const Spot::Vector3 rest = GetRest(*wheel);
        const float base = fixedCount > 0 ? fixedZ / static_cast<float>(fixedCount) - rest.z : 0.0f;
        if (std::abs(angle) < 1e-4f || std::abs(base) < 0.1f) {
            wheel->m_steer = angle;
            continue;
        }
        // The point it turns around, beside the vehicle on the side it turns to: left is -X.
        const float radius = std::abs(base) / std::tan(std::abs(angle));
        const bool left = (angle > 0.0f) == (base > 0.0f);
        const float center = left ? -radius : radius;
        wheel->m_steer = std::copysign(std::atan(std::abs(base) / std::max(std::abs(rest.x - center), 0.01f)), angle);
    }
}

void WheeledVehicle::UpdateGearbox(float deltaTime, float& brake, const std::vector<VehicleWheel*>& wheels) {
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
        m_shiftLeft = drivetrain.shiftTime;
    };
    const float pedals = std::clamp(GetInput().drive, -1.0f, 1.0f);
    // Into reverse and back only nearly stopped; until then, the other way brakes.
    if (m_shiftLeft <= 0.0f && m_gear > 0 && pedals < -0.1f && m_speed < 0.5f) {
        shift(-1);
    } else if (m_shiftLeft <= 0.0f && m_gear < 0 && pedals > 0.1f && m_speed > -0.5f) {
        shift(1);
    }
    const bool reverse = (m_shiftLeft > 0.0f ? m_nextGear : m_gear) < 0;
    m_throttle = std::max(reverse ? -pedals : pedals, 0.0f);
    brake = std::max(reverse ? pedals : -pedals, 0.0f);
    // With the pedals left alone, the brakes hold a vehicle that has stopped, as on a hill.
    if (std::abs(pedals) < 0.05f && std::abs(m_speed) < 0.5f) {
        brake = std::max(brake, 0.4f);
    }
    if (m_gear <= 0 || m_shiftLeft > 0.0f) {
        return;
    }
    const int gears = static_cast<int>(drivetrain.gears.size());
    const float rpm = std::abs(GetDrivenSpin(wheels) * GetRatio(m_gear)) * RpmPerRadianPerSecond;
    // Later the harder the engine is pushed, and down again as it slows, never so low that it would shift right back.
    const float upshift = std::lerp(2800.0f, drivetrain.redlineRpm * 0.93f, m_throttle);
    if (m_gear < gears && rpm > upshift) {
        shift(m_gear + 1);
    } else if (m_gear > 1 && rpm < std::lerp(1500.0f, 3600.0f, m_throttle) &&
               rpm * GetRatio(m_gear - 1) / GetRatio(m_gear) < upshift * 0.85f) {
        shift(m_gear - 1);
    }
}

void WheeledVehicle::OnFixedUpdate(float deltaTime) {
    auto* body = GetEntity().GetComponent<Spot::Rigidbody>();
    if (!body || body->IsKinematic() || deltaTime <= 0.0f || (!HasDriver() && body->IsSleeping())) {
        return;
    }
    const std::vector<VehicleWheel*> wheels = GetWheels();
    if (wheels.empty()) {
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

    UpdateSteering(deltaTime, wheels);
    float brake = 0.0f;
    UpdateGearbox(deltaTime, brake, wheels);

    const std::size_t count = wheels.size();
    const std::vector<float> shares = GetRestShares(wheels);
    std::vector<float> restLoads(count);
    // The springs: each wheel is cast down from the top of its travel, as a sphere as wide as its tire at its bottom.
    std::vector<Spot::RaycastHit> contacts(count);
    std::vector<float> loads(count);
    for (std::size_t i = 0; i < count; ++i) {
        VehicleWheel& wheel = *wheels[i];
        restLoads[i] = mass * g * shares[i];
        const float tire = wheel.width * 0.5f;
        const Spot::Vector3 top =
            origin + rotation * (GetRest(wheel) + Up * wheel.bumpTravel) - up * (wheel.radius - tire);
        const auto hit =
            physics.SphereCast(top, tire, -up, wheel.bumpTravel + wheel.droopTravel, {.ignore = GetEntity().GetId()});
        wheel.m_grounded = hit.has_value();
        wheel.m_compression = hit ? wheel.bumpTravel - hit->distance : -wheel.droopTravel;
        if (!hit) {
            continue;
        }
        contacts[i] = *hit;
        // Springs are set to hold the vehicle at rest where its wheels are placed.
        const float rate = -Spot::Dot(velocityAt(hit->point), up);
        loads[i] = restLoads[i] + wheel.springRate * wheel.m_compression + wheel.damping * rate;
        // The bump stop stiffens the last of the travel.
        const float stop = wheel.m_compression - wheel.bumpTravel * 0.75f;
        loads[i] += stop > 0.0f ? wheel.springRate * 8.0f * stop : 0.0f;
    }
    // The anti-roll bars push each wheel of an axle toward the others' compression.
    for (std::size_t i = 0; i < count; ++i) {
        float compression = 0.0f;
        int sharing = 0;
        for (const VehicleWheel* other : wheels) {
            if (other->axle == wheels[i]->axle) {
                compression += other->m_compression;
                ++sharing;
            }
        }
        if (sharing > 1) {
            loads[i] += antiRoll * 2.0f * (wheels[i]->m_compression - compression / static_cast<float>(sharing));
        }
    }

    // The engine, through the clutch, which slips below the revolutions it takes up the drive at, and the gears.
    const float ratio = GetRatio(m_shiftLeft > 0.0f ? 0 : m_gear);
    float driveShares = 0.0f;
    for (const VehicleWheel* wheel : wheels) {
        driveShares += wheel->driveShare;
    }
    const float drivenSpin = GetDrivenSpin(wheels);
    const float wheelRpm = std::abs(drivenSpin * ratio) * RpmPerRadianPerSecond;
    const float clutchRpm = drivetrain.idleRpm + m_throttle * 1600.0f;
    const bool coupled = ratio != 0.0f && wheelRpm > clutchRpm;
    float engineTorque = 0.0f;
    if (!HasDriver()) {
        m_rpm = 0.0f;
    } else if (ratio == 0.0f) {
        // Free while shifting, falling toward what the next gear will turn it at.
        const float next = std::abs(drivenSpin * GetRatio(m_nextGear)) * RpmPerRadianPerSecond;
        m_rpm += (std::max(next, clutchRpm) - m_rpm) * (1.0f - std::exp(-12.0f * deltaTime));
    } else {
        m_rpm = std::min(std::max(wheelRpm, clutchRpm), drivetrain.redlineRpm);
        // The limiter cuts the fuel at the redline; off the throttle, the engine holds the vehicle back.
        engineTorque = m_rpm >= drivetrain.redlineRpm ? 0.0f : m_throttle * GetEngineTorque(m_rpm);
        if (coupled) {
            engineTorque -= std::copysign(
                drivetrain.engineBraking * (1.0f - m_throttle) * m_rpm / drivetrain.redlineRpm, drivenSpin * ratio);
        }
    }

    // The wheels' forces, gathered around where the vehicle is now, which its body may not have followed yet.
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
    for (std::size_t i = 0; i < count; ++i) {
        VehicleWheel& wheel = *wheels[i];
        const float radius = wheel.radius;
        wheel.m_load = wheel.m_grounded ? std::max(loads[i], 0.0f) : 0.0f;
        const float driveShare = driveShares > 0.0f ? wheel.driveShare / driveShares : 0.0f;
        const float driveTorque = engineTorque * ratio * DrivetrainEfficiency * driveShare;
        const float inertia = wheel.inertia + (coupled ? drivetrain.engineInertia * ratio * ratio * driveShare : 0.0f);
        const float pedal = brakeTorque * brake * wheel.brakeShare;
        const float held = (wheel.handbrake && GetInput().handbrake ? handbrakeTorque : 0.0f) +
                           rollingResistance * wheel.m_load * radius;
        wheel.m_slip = 0.0f;
        if (!wheel.m_grounded) {
            for (int sub = 0; sub < WheelSteps; ++sub) {
                wheel.m_spin += driveTorque / inertia * step;
                Brake(wheel.m_spin, (pedal + held) / inertia * step);
            }
            continue;
        }
        const Spot::RaycastHit& contact = contacts[i];
        const Spot::Vector3 heading = Spot::Quaternion::FromAxisAngle(up, wheel.m_steer) * forward;
        const Spot::Vector3 along = Spot::Normalize(heading - contact.normal * Spot::Dot(heading, contact.normal));
        const Spot::Vector3 across = Spot::Cross(along, contact.normal);
        const Spot::Vector3 pointVelocity = velocityAt(contact.point);
        const float speedAlong = Spot::Dot(pointVelocity, along);
        const float speedAcross = Spot::Dot(pointVelocity, across);
        const float scale = std::max(std::abs(speedAlong), MinSlipSpeed);
        const float friction =
            grip *
            std::clamp(1.0f - loadSensitivity * (wheel.m_load / std::max(restLoads[i], 1.0f) - 1.0f), 0.4f, 1.3f) *
            wheel.m_load;
        // No more than the forces that would stop the tire sliding within the step, gravity along the ground
        // included, so they never push it back the other way, and a stopped vehicle does not creep.
        const float share = wheel.m_load / g;
        const float holdAcross = share * std::abs(speedAcross / deltaTime + Spot::Dot(gravity, across));
        const float holdAlong = share * std::abs(speedAlong / deltaTime + Spot::Dot(gravity, along));
        // How much the force along changes with the spin, at most, which steps the spin implicitly, so stably.
        const float stiffness = friction * StiffnessFactor * ShapeFactor * radius / (peakRatio * scale);
        float forceAlong = 0.0f;
        float forceAcross = 0.0f;
        for (int sub = 0; sub < WheelSteps; ++sub) {
            const float slipAlong = (wheel.m_spin * radius - speedAlong) / scale / peakRatio;
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
            const float response = 1.0f + step * stiffness * radius / inertia;
            const float holding = driveTorque - radius * (x - stiffness * wheel.m_spin) + inertia * wheel.m_spin / step;
            float spin = 0.0f;
            if (std::abs(holding) > brakes) {
                const float braked = driveTorque - x * radius - std::copysign(brakes, holding);
                spin = wheel.m_spin + step * braked / inertia / response;
            }
            x = std::clamp(x + stiffness * (spin - wheel.m_spin), -friction, friction);
            if (spin == 0.0f) {
                x = std::clamp(x, -holdAlong, holdAlong);
            }
            wheel.m_spin = spin;
            forceAlong += x;
            forceAcross += y;
            wheel.m_slip = std::max(wheel.m_slip, slip);
        }
        forceAlong /= static_cast<float>(WheelSteps);
        forceAcross /= static_cast<float>(WheelSteps);

        const Spot::Vector3 support = up * wheel.m_load;
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
    for (VehicleWheel* wheel : wheels) {
        wheel->m_angle = std::fmod(wheel->m_angle + wheel->m_spin * deltaTime, 2.0f * Spot::Pi);
    }
    body->AddForce(force);
    body->AddTorque(torque);

    // Parked and still, it sleeps, as bodies at rest do, until something moves it.
    const bool still = !HasDriver() && Spot::Length(velocity) < 0.05f && Spot::Length(angular) < 0.05f;
    m_still = still ? m_still + deltaTime : 0.0f;
    if (m_still > 1.0f) {
        body->Sleep();
        for (VehicleWheel* wheel : wheels) {
            wheel->m_spin = 0.0f;
        }
    }
}

void WheeledVehicle::Serialize(Spot::Archive& archive) {
    Vehicle::Serialize(archive);
    archive.Field("antiRoll", antiRoll, {.min = 0.0f, .max = 1000000.0f, .speed = 100.0f, .tooltip = "N/m"});
    archive.Field("rollCenterHeight", rollCenterHeight,
                  {.min = 0.0f, .max = 2.0f, .speed = 0.005f, .tooltip = "Where the tires' forces reach the body"});
    archive.Field("grip", grip, {.min = 0.0f, .max = 3.0f, .speed = 0.01f, .tooltip = "Peak friction coefficient"});
    archive.Field("loadSensitivity", loadSensitivity, {.min = 0.0f, .max = 1.0f, .speed = 0.005f});
    archive.Field("peakSlipRatio", peakSlipRatio, {.min = 0.01f, .max = 1.0f, .speed = 0.005f});
    archive.Field("peakSlipAngle", peakSlipAngle, {.min = 0.01f, .max = 0.5f, .speed = 0.002f, .angle = true});
    archive.Field("rollingResistance", rollingResistance, {.min = 0.0f, .max = 0.2f, .speed = 0.001f});
    archive.Field("steerFalloffSpeed", steerFalloffSpeed,
                  {.min = 0.1f, .max = 200.0f, .speed = 0.1f, .tooltip = "Speed at which steering is halved"});
    archive.Field("steerSpeed", steerSpeed, {.min = 0.1f, .max = 20.0f, .speed = 0.05f});
    archive.Field("drivetrain", drivetrain);
    archive.Field("brakeTorque", brakeTorque,
                  {.min = 0.0f, .max = 100000.0f, .speed = 10.0f, .tooltip = "N m, for all the wheels"});
    archive.Field("handbrakeTorque", handbrakeTorque,
                  {.min = 0.0f, .max = 100000.0f, .speed = 10.0f, .tooltip = "N m, on each wheel it holds"});
    archive.Field("antiLock", antiLock);
    archive.Field("dragCoefficient", dragCoefficient,
                  {.min = 0.0f, .max = 10.0f, .speed = 0.005f, .tooltip = "Half the air's density times Cd A"});
}

} // namespace Gameplay
