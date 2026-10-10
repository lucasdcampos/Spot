#include "prop_spawner.h"

#include "explosive_barrel.h"
#include "player_controller.h"
#include "undo_list.h"

#include <algorithm>
#include <array>
#include <string_view>

SPOT_COMPONENT(PropSpawner);

void SpawnableProp::Serialize(Spot::Archive& archive) {
    static constexpr std::array<std::string_view, 4> Shapes = {"Box", "Sphere", "Capsule", "ConvexHull"};
    archive.Field("name", name);
    archive.Field("mesh", mesh);
    archive.Field("material", material);
    archive.Field("shape", shape, {.enumNames = Shapes});
    archive.Field("size", size, {.speed = 0.01f});
    archive.Field("mass", mass, {.min = 0.001f, .max = 100000.0f, .speed = 0.1f, .tooltip = "In kilograms"});
    archive.Field("explosive", explosive);
}

Spot::Entity* PropSpawner::Spawn(std::size_t index) {
    const auto* player = GetEntity().GetComponent<PlayerController>();
    if (index >= props.size() || !player) {
        return nullptr;
    }
    const SpawnableProp& prop = props[index];
    // Where the player looks, on what it looks at, lifted so the prop rests on it; or in the air ahead.
    const Spot::Vector3 eye = player->GetEyePosition();
    const Spot::Vector3 aim = player->GetAimDirection();
    const auto hit = GetScene().GetPhysics().Raycast(eye, aim, reach, {.ignore = GetEntity().GetId()});
    const float lift = std::max({prop.size.x, prop.size.y, prop.size.z}) * 0.5f + 0.05f;
    const Spot::Vector3 position = hit ? hit->point + hit->normal * lift : eye + aim * reach;

    auto& entity = GetScene().CreateEntity(prop.name);
    entity.GetTransform().SetPosition(position);
    entity.GetTransform().SetRotation(Spot::Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, player->GetYaw()));
    entity.AddComponent<Spot::MeshRenderer>(prop.mesh, prop.material);
    switch (prop.shape) {
    case SpawnableProp::Shape::Box:
        entity.AddComponent<Spot::BoxCollider>(prop.size);
        break;
    case SpawnableProp::Shape::Sphere:
        entity.AddComponent<Spot::SphereCollider>(prop.size.x * 0.5f);
        break;
    case SpawnableProp::Shape::Capsule:
        entity.AddComponent<Spot::CapsuleCollider>(prop.size.x * 0.5f, prop.size.y);
        break;
    case SpawnableProp::Shape::ConvexHull: {
        auto& collider = entity.AddComponent<Spot::MeshCollider>();
        collider.SetConvex(true);
        collider.SetMesh(Spot::Assets::Load<Spot::MeshData>(prop.mesh.GetId()));
        break;
    }
    }
    entity.AddComponent<Spot::Rigidbody>().SetMass(prop.mass);
    if (prop.explosive) {
        entity.AddComponent<ExplosiveBarrel>();
    }
    m_spawned.push_back(entity.GetId());
    if (auto* undo = GetEntity().GetComponent<UndoList>()) {
        undo->PushSpawn(entity.GetId());
    }
    return &entity;
}

void PropSpawner::Cleanup() {
    for (const Spot::EntityId id : m_spawned) {
        if (Spot::Entity* entity = GetScene().GetEntity(id)) {
            entity->Destroy();
        }
    }
    m_spawned.clear();
}

std::size_t PropSpawner::GetSpawnedCount() const {
    return static_cast<std::size_t>(
        std::ranges::count_if(m_spawned, [this](Spot::EntityId id) { return GetScene().GetEntity(id) != nullptr; }));
}

void PropSpawner::OnUpdate(float /*deltaTime*/) {
    namespace Input = Spot::Input;
    static constexpr std::array<std::string_view, 8> Actions = {"Spawn 1", "Spawn 2", "Spawn 3", "Spawn 4",
                                                                "Spawn 5", "Spawn 6", "Spawn 7", "Spawn 8"};
    for (std::size_t i = 0; i < Actions.size(); ++i) {
        if (Input::IsActionPressed(Actions[i])) {
            Spawn(i);
        }
    }
    // One button for every prop, as gamepads have no buttons to spare: each press spawns the next.
    if (Input::IsActionPressed("Spawn") && !props.empty()) {
        Spawn(m_next);
        m_next = (m_next + 1) % props.size();
    }
    if (Input::IsActionPressed("Clean Up")) {
        Cleanup();
    }
}

void PropSpawner::Serialize(Spot::Archive& archive) {
    archive.Field("props", props);
    archive.Field("reach", reach, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
}
