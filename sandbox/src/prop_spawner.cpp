#include "prop_spawner.h"
#include <spot/scene/physics/physics_world.h>

#include "player_controller.h"
#include "player_health.h"
#include "prop_carrier.h"
#include "undo_list.h"

#include <algorithm>
#include <array>
#include <string_view>

SPOT_COMPONENT(PropSpawner);

void PropSpawner::OnStart() {
    RefreshSpawnables();
}

void PropSpawner::RefreshSpawnables(Spot::AssetManager* assets) {
    if (!assets) {
        assets = Spot::Assets::GetManager();
    }
    m_assets = assets;
    m_spawnables.clear();
    m_next = 0;
    if (!assets) {
        return;
    }
    for (const auto id : assets->GetAssetIds(Spot::AssetType::Prefab, folder)) {
        const auto prefab = assets->Load<Spot::Prefab>(id);
        if (!prefab) {
            continue;
        }
        Spot::Scene preview;
        const Spot::Entity* root = preview.Instantiate(prefab, {.assets = assets});
        const auto* spawnable = root ? root->GetComponent<Spawnable>() : nullptr;
        if (spawnable) {
            m_spawnables.push_back(
                {prefab, root->GetName(), spawnable->category, spawnable->order, std::max(0.0f, spawnable->clearance)});
        }
    }
    std::ranges::sort(m_spawnables, [](const SpawnableItem& a, const SpawnableItem& b) {
        if (a.order != b.order) {
            return a.order < b.order;
        }
        if (a.name != b.name) {
            return a.name < b.name;
        }
        return a.prefab.GetId().ToString() < b.prefab.GetId().ToString();
    });
}

std::optional<PropSpawner::Placement> PropSpawner::FindPlacement(const SpawnableItem& item) const {
    const auto* player = GetEntity().GetComponent<PlayerController>();
    if (!player) {
        return std::nullopt;
    }
    // Where the player looks, on what it looks at, lifted so the prop rests on it; or in the air ahead.
    const Spot::Vector3 eye = player->GetEyePosition();
    const Spot::Vector3 aim = player->GetAimDirection();
    const auto hit =
        GetScene().GetOrAddSystem<Spot::PhysicsWorld>().Raycast(eye, aim, reach, {.ignore = GetEntity().GetId()});
    return Placement{hit ? hit->point + hit->normal * item.clearance : eye + aim * reach,
                     Spot::Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, player->GetYaw())};
}

Spot::Entity* PropSpawner::Spawn(std::size_t index) {
    if (index >= m_spawnables.size()) {
        return nullptr;
    }
    const SpawnableItem& item = m_spawnables[index];
    const auto placement = FindPlacement(item);
    if (!placement) {
        return nullptr;
    }
    Spot::Entity* entity = GetScene().Instantiate(item.prefab, {.assets = m_assets});
    if (!entity) {
        return nullptr;
    }
    entity->GetTransform().SetPosition(placement->position);
    entity->GetTransform().SetRotation(placement->facing * entity->GetTransform().GetRotation());
    m_spawned.push_back(entity->GetId());
    if (auto* undo = GetEntity().GetComponent<UndoList>()) {
        undo->PushSpawn(entity->GetId());
    }
    return entity;
}

bool PropSpawner::BeginPlacement(std::size_t index) {
    CancelPlacement();
    if (index >= m_spawnables.size() || !GetEntity().GetComponent<PlayerController>()) {
        return false;
    }
    // Only the prop's meshes, from a copy of it in a scene of its own: the preview has no body and no behavior, and
    // world saves leave it out.
    Spot::Scene source;
    Spot::Entity* root = source.Instantiate(m_spawnables[index].prefab, {.assets = m_assets});
    if (!root) {
        return false;
    }
    root->GetTransform().SetPosition({});
    // It glows a little, so it reads in the dark too.
    Spot::Material material;
    material.baseColor = previewColor;
    material.emissive = {previewColor.r * 0.25f, previewColor.g * 0.25f, previewColor.b * 0.25f, 1.0f};
    material.alphaMode = Spot::AlphaMode::Transparent;
    material.doubleSided = true;
    auto& preview = GetScene().CreateEntity("Spawn Preview");
    source.ForEachComponent<Spot::MeshRenderer>([&](Spot::MeshRenderer& renderer) {
        if (!renderer.GetMesh()) {
            return;
        }
        const Spot::TransformComponents pose = Spot::Decompose(renderer.GetEntity().GetTransform().GetWorldMatrix());
        auto& part = preview.CreateChild(renderer.GetEntity().GetName());
        part.GetTransform().SetPosition(pose.translation);
        part.GetTransform().SetRotation(pose.rotation);
        part.GetTransform().SetScale(pose.scale);
        auto& mesh = part.AddComponent<Spot::MeshRenderer>(renderer.GetMesh(), material);
        mesh.SetSubmesh(renderer.GetSubmesh());
        mesh.SetCastingShadows(false);
    });
    m_placing = index;
    m_preview = preview.GetId();
    // The click that places it would throw what is carried.
    if (auto* carrier = GetEntity().GetComponent<PropCarrier>()) {
        carrier->Drop();
    }
    UpdatePreview();
    return true;
}

Spot::Entity* PropSpawner::ConfirmPlacement() {
    if (!m_placing) {
        return nullptr;
    }
    const std::size_t index = *m_placing;
    CancelPlacement();
    return Spawn(index);
}

void PropSpawner::CancelPlacement() {
    if (Spot::Entity* preview = GetScene().GetEntity(m_preview)) {
        preview->Destroy();
    }
    m_preview = Spot::EntityId::Invalid;
    m_placing.reset();
}

void PropSpawner::UpdatePreview() {
    Spot::Entity* preview = GetScene().GetEntity(m_preview);
    if (!preview || !m_placing) {
        return;
    }
    if (const auto placement = FindPlacement(m_spawnables[*m_placing])) {
        preview->GetTransform().SetPosition(placement->position);
        preview->GetTransform().SetRotation(placement->facing);
    }
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

bool PropSpawner::IsSpawned(Spot::EntityId id) const {
    return std::ranges::find(m_spawned, id) != m_spawned.end() && GetScene().GetEntity(id);
}

void PropSpawner::TrackSpawned(Spot::EntityId id) {
    if (GetScene().GetEntity(id) && !IsSpawned(id)) {
        m_spawned.push_back(id);
    }
}

void PropSpawner::OnUpdate(float /*deltaTime*/) {
    namespace Input = Spot::Input;
    m_holdingClick = m_holdingClick && (Input::IsActionDown("Fire") || Input::IsActionDown("Aim"));
    if (m_placing) {
        const auto* health = GetEntity().GetComponent<PlayerHealth>();
        if (*m_placing >= m_spawnables.size() || (health && health->IsDead())) {
            CancelPlacement();
        } else if (Input::IsActionPressed("Fire") && Input::IsCursorLocked()) {
            ConfirmPlacement();
            m_holdingClick = true;
        } else if (Input::IsActionPressed("Aim")) {
            CancelPlacement();
            m_holdingClick = true;
        }
    }
    static constexpr std::array<std::string_view, 8> Actions = {"Spawn 1", "Spawn 2", "Spawn 3", "Spawn 4",
                                                                "Spawn 5", "Spawn 6", "Spawn 7", "Spawn 8"};
    for (std::size_t i = 0; i < Actions.size(); ++i) {
        if (Input::IsActionPressed(Actions[i])) {
            Spawn(i);
        }
    }
    // One button for every prop, as gamepads have no buttons to spare: each press spawns the next.
    if (Input::IsActionPressed("Spawn") && !m_spawnables.empty()) {
        Spawn(m_next);
        m_next = (m_next + 1) % m_spawnables.size();
    }
    if (Input::IsActionPressed("Clean Up")) {
        Cleanup();
    }
}

void PropSpawner::OnLateUpdate(float /*deltaTime*/) {
    // After the player has turned this frame, so the preview keeps up with the aim.
    UpdatePreview();
}

void PropSpawner::OnDisable() {
    CancelPlacement();
    m_holdingClick = false;
}

void PropSpawner::Serialize(Spot::Archive& archive) {
    archive.Field("folder", folder);
    archive.Field("reach", reach, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
    archive.Field("previewColor", previewColor, {.tooltip = "The placement preview's color, and how opaque it is"});
}
