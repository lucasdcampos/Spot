#include "prop_spawner.h"

#include "player_controller.h"
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

Spot::Entity* PropSpawner::Spawn(std::size_t index) {
    const auto* player = GetEntity().GetComponent<PlayerController>();
    if (index >= m_spawnables.size() || !player) {
        return nullptr;
    }
    const SpawnableItem& item = m_spawnables[index];
    // Where the player looks, on what it looks at, lifted so the prop rests on it; or in the air ahead.
    const Spot::Vector3 eye = player->GetEyePosition();
    const Spot::Vector3 aim = player->GetAimDirection();
    const auto hit = GetScene().GetPhysics().Raycast(eye, aim, reach, {.ignore = GetEntity().GetId()});
    const Spot::Vector3 position = hit ? hit->point + hit->normal * item.clearance : eye + aim * reach;

    Spot::Entity* entity = GetScene().Instantiate(item.prefab, {.assets = m_assets});
    if (!entity) {
        return nullptr;
    }
    entity->GetTransform().SetPosition(position);
    entity->GetTransform().SetRotation(Spot::Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, player->GetYaw()) *
                                       entity->GetTransform().GetRotation());
    m_spawned.push_back(entity->GetId());
    if (auto* undo = GetEntity().GetComponent<UndoList>()) {
        undo->PushSpawn(entity->GetId());
    }
    return entity;
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

void PropSpawner::Serialize(Spot::Archive& archive) {
    archive.Field("folder", folder);
    archive.Field("reach", reach, {.min = 0.0f, .max = 100.0f, .speed = 0.05f});
}
