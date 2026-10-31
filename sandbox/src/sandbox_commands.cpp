#include "sandbox_commands.h"
#include "gameplay/core/players.h"
#include <spot/scene/physics/physics_world.h>

#include "prop_spawner.h"
#include "undo_list.h"

#include "gameplay/character/character.h"
#include "gameplay/character/character_movement.h"
#include "gameplay/core/pawn.h"
#include "gameplay/core/world_time.h"
#include "gameplay/inventory/inventory.h"
#include "gameplay/weapons/firearm.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

namespace {
std::string ItemKey(std::string_view name) {
    std::string key;
    for (const unsigned char c : name) {
        if (c != ' ' && c != '-' && c != '_') {
            key += static_cast<char>(std::tolower(c));
        }
    }
    return key;
}

std::string ItemName(std::span<const std::string> arguments) {
    std::string name;
    for (const auto& argument : arguments) {
        if (!name.empty()) {
            name += ' ';
        }
        name += argument;
    }
    return name;
}

const SpawnableItem* FindItem(const PropSpawner& spawner, std::string_view name) {
    const auto key = ItemKey(name);
    const auto items = spawner.GetSpawnables();
    const auto found = std::ranges::find_if(items, [&](const auto& item) { return ItemKey(item.name) == key; });
    return found == items.end() ? nullptr : &*found;
}

// A weapon in hand is out of the catalog's world.
void RemoveSpawnable(Spot::Entity& entity) {
    while (entity.RemoveComponent<Spawnable>()) {
    }
    for (auto* child : entity.GetChildren()) {
        RemoveSpawnable(*child);
    }
}
} // namespace

SandboxCommands::SandboxCommands(Spot::Console& console, Spot::Scene& scene) : m_scene(scene) {
    using Spot::ConsoleFlags;
    using Spot::ConsoleResult;
    m_registrations.push_back(console.Register(
        {"gravity", "Query/set gravity on Y (-1000..1000)", ConsoleFlags::Cheat,
         [this](std::span<const std::string> arguments) -> ConsoleResult {
             auto& physics = m_scene.GetOrAddSystem<Spot::PhysicsWorld>();
             if (arguments.empty()) {
                 return {true, std::format("Gravity: {}", physics.GetGravity().y)};
             }
             float value = 0.0f;
             if (arguments.size() != 1 || !Spot::ParseConsoleValue(arguments[0], value) || std::abs(value) > 1000.0f) {
                 return {false, "Usage: gravity <Y acceleration, -1000..1000>"};
             }
             auto gravity = physics.GetGravity();
             gravity.y = value;
             physics.SetGravity(gravity);
             return {true, std::format("Gravity: {}", value)};
         }}));
    m_registrations.push_back(
        console.Register({"timescale", "Query/set simulation speed (0..100)", ConsoleFlags::Cheat,
                          [this](std::span<const std::string> arguments) -> ConsoleResult {
                              if (arguments.empty()) {
                                  return {true, std::format("Time scale: {}", Spot::Time::GetTimeScale())};
                              }
                              float value = 0.0f;
                              if (arguments.size() != 1 || !Spot::ParseConsoleValue(arguments[0], value) ||
                                  value < 0.0f || value > 100.0f) {
                                  return {false, "Usage: timescale <0..100>"};
                              }
                              if (!Gameplay::WorldTime::IsScalingAllowed(m_scene)) {
                                  return {false, "Time scale cannot change in this game"};
                              }
                              Gameplay::WorldTime::Get(m_scene);
                              Spot::Time::SetTimeScale(value);
                              return {true, std::format("Time scale: {}", value)};
                          }}));
    m_registrations.push_back(console.Register(
        {"noclip", "Toggle flying, or set true/false", ConsoleFlags::Cheat,
         [this](std::span<const std::string> arguments) -> ConsoleResult {
             auto* movement = Gameplay::Players::FindComponent<Gameplay::CharacterMovement>(m_scene);
             if (!movement) {
                 return {false, "No player movement in this scene"};
             }
             bool enabled = !movement->IsFlying();
             if (arguments.size() > 1 || (!arguments.empty() && !Spot::ParseConsoleValue(arguments[0], enabled))) {
                 return {false, "Usage: noclip [true/false]"};
             }
             movement->SetFlying(enabled);
             return {true, enabled ? "Noclip enabled" : "Noclip disabled"};
         }}));
    m_registrations.push_back(console.Register(
        {"thirdperson", "Toggle the character inspection camera, or set true/false", ConsoleFlags::Cheat,
         [this](std::span<const std::string> arguments) -> ConsoleResult {
             auto* character = Gameplay::Players::FindComponent<Gameplay::Character>(m_scene);
             if (!character) {
                 return {false, "No player character in this scene"};
             }
             bool enabled = !character->IsThirdPerson();
             if (arguments.size() > 1 || (!arguments.empty() && !Spot::ParseConsoleValue(arguments[0], enabled))) {
                 return {false, "Usage: thirdperson [true/false]"};
             }
             if (!character->SetThirdPerson(enabled)) {
                 return {false, "The character has no inspection camera"};
             }
             return {true, enabled ? "Third person enabled" : "First person enabled"};
         }}));
    m_registrations.push_back(
        console.Register({"firstperson", "Return to the character's first-person camera", ConsoleFlags::Cheat,
                          [this](std::span<const std::string> arguments) -> ConsoleResult {
                              if (!arguments.empty()) {
                                  return {false, "Usage: firstperson"};
                              }
                              auto* character = Gameplay::Players::FindComponent<Gameplay::Character>(m_scene);
                              if (!character) {
                                  return {false, "No player character in this scene"};
                              }
                              character->SetThirdPerson(false);
                              return {true, "First person enabled"};
                          }}));
    m_registrations.push_back(console.Register({"cleanup", "Remove spawned props", ConsoleFlags::Cheat,
                                                [this](std::span<const std::string> arguments) -> ConsoleResult {
                                                    if (!arguments.empty()) {
                                                        return {false, "Usage: cleanup"};
                                                    }
                                                    auto* spawner =
                                                        Gameplay::Players::FindComponent<PropSpawner>(m_scene);
                                                    if (!spawner) {
                                                        return {false, "No prop spawner in this scene"};
                                                    }
                                                    spawner->Cleanup();
                                                    if (auto* undo = spawner->GetEntity().GetComponent<UndoList>()) {
                                                        undo->Clear();
                                                    }
                                                    return {true, "Spawned props removed"};
                                                }}));
    m_registrations.push_back(
        console.Register({"spawn", "Spawn a catalog item by name", ConsoleFlags::Cheat,
                          [this](std::span<const std::string> arguments) { return Spawn(arguments); }}));
    m_registrations.push_back(
        console.Register({"give", "Equip/refill a catalog weapon by name", ConsoleFlags::Cheat,
                          [this](std::span<const std::string> arguments) { return Give(arguments); }}));
    m_registrations.push_back(
        console.Register({"stress", "Spawn 1..2000 crates in a pile; cleanup removes them", ConsoleFlags::Cheat,
                          [this](std::span<const std::string> arguments) { return Stress(arguments); }}));
}

Spot::ConsoleResult SandboxCommands::Spawn(std::span<const std::string> arguments) {
    auto* spawner = Gameplay::Players::FindComponent<PropSpawner>(m_scene);
    if (!spawner) {
        return {false, "No prop spawner in this scene"};
    }
    if (arguments.empty()) {
        std::string names = "Usage: spawn <prop>. Available:";
        for (const auto& item : spawner->GetSpawnables()) {
            names += " " + item.name + ";";
        }
        return {false, names};
    }
    const auto* item = FindItem(*spawner, ItemName(arguments));
    if (!item) {
        return {false, "Unknown spawnable: " + ItemName(arguments)};
    }
    const auto index = static_cast<std::size_t>(item - spawner->GetSpawnables().data());
    if (!spawner->Spawn(index)) {
        return {false, "Could not instantiate " + item->name};
    }
    return {true, "Spawned " + item->name};
}

Spot::ConsoleResult SandboxCommands::Give(std::span<const std::string> arguments) {
    auto* spawner = Gameplay::Players::FindComponent<PropSpawner>(m_scene);
    auto* inventory = Gameplay::Players::FindComponent<Gameplay::Inventory>(m_scene);
    if (!inventory || !spawner) {
        return {false, "No player inventory or catalog in this scene"};
    }
    if (arguments.empty()) {
        return {false, "Usage: give <weapon>"};
    }
    const auto* item = FindItem(*spawner, ItemName(arguments));
    if (!item || item->category != Spawnable::Category::Weapon) {
        return {false, "Unknown weapon: " + ItemName(arguments)};
    }
    for (Gameplay::HeldItem* held : inventory->GetItems()) {
        auto* weapon = dynamic_cast<Gameplay::Firearm*>(held);
        if (weapon && (weapon->GetEntity().GetPrefabAssetId() == item->prefab.GetId() ||
                       ItemKey(weapon->displayName) == ItemKey(item->name))) {
            weapon->Refill();
            inventory->Select(*weapon);
            return {true, "Equipped and refilled " + item->name};
        }
    }
    Gameplay::HeldItem* added = inventory->AddFromPrefab(item->prefab);
    auto* weapon = dynamic_cast<Gameplay::Firearm*>(added);
    if (!weapon) {
        if (added) {
            added->GetEntity().Destroy();
        }
        return {false, "Catalog item contains no weapon"};
    }
    RemoveSpawnable(weapon->GetEntity());
    weapon->Refill();
    inventory->Select(*weapon);
    return {true, "Equipped and refilled " + item->name};
}

Spot::ConsoleResult SandboxCommands::Stress(std::span<const std::string> arguments) {
    int count = 0;
    if (arguments.size() != 1 || !Spot::ParseConsoleValue(arguments[0], count) || count < 1 || count > 2000) {
        return {false, "Usage: stress <1..2000>"};
    }
    auto* spawner = Gameplay::Players::FindComponent<PropSpawner>(m_scene);
    const auto* player = spawner ? Gameplay::Pawn::Of(spawner->GetEntity()) : nullptr;
    if (!spawner || !player) {
        return {false, "No player or prop spawner in this scene"};
    }
    const auto* item = FindItem(*spawner, "Crate");
    if (!item) {
        return {false, "No Crate in the spawn catalog"};
    }
    const auto index = static_cast<std::size_t>(item - spawner->GetSpawnables().data());
    const int side = static_cast<int>(std::ceil(std::cbrt(static_cast<double>(count))));
    const float spacing = std::max(1.1f, item->clearance * 2.0f + 0.1f);
    const Gameplay::ViewPoint view = player->GetViewPoint();
    const auto center = view.position + view.direction * 6.0f;
    for (int i = 0; i < count; ++i) {
        auto* prop = spawner->Spawn(index);
        if (!prop) {
            return {false, std::format("Spawn failed after {} props; cleanup removes them", i)};
        }
        const float x = (static_cast<float>(i % side) - static_cast<float>(side - 1) * 0.5f) * spacing;
        const float y = static_cast<float>(i / (side * side)) * spacing + 2.0f;
        const float z = (static_cast<float>((i / side) % side) - static_cast<float>(side - 1) * 0.5f) * spacing;
        prop->GetTransform().SetPosition(center + Spot::Vector3{x, y, z});
    }
    return {true, std::format("Spawned {} props in a pile; use cleanup to remove them", count)};
}
