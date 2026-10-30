#include "gameplay/game/game_mode.h"

#include "gameplay/game/spawn_point.h"

#include "gameplay/core/controller.h"
#include "gameplay/core/pawn.h"
#include "gameplay/health/health.h"

#include <algorithm>
#include <limits>

namespace Gameplay {

SPOT_COMPONENT(GameMode);

GameMode* GameMode::Get(Spot::Scene& scene) {
    return scene.FindComponent<GameMode>();
}

void GameMode::TrackPlayers() {
    std::erase_if(m_players, [this](Player& player) {
        if (!GetScene().GetEntity(player.controller) || !GetScene().GetEntity(player.pawn)) {
            CancelInvoke(player.respawn);
            return true;
        }
        return false;
    });
    std::vector<Controller*> controllers;
    GetScene().ForEachComponent<Controller>([&](Controller& controller) {
        if (controller.IsPlayer()) {
            controllers.push_back(&controller);
        }
    });
    for (Controller* controller : controllers) {
        const auto id = controller->GetEntity().GetId();
        if (auto found = std::ranges::find(m_players, id, &Player::controller); found != m_players.end()) {
            const auto* entity = GetScene().GetEntity(found->pawn);
            auto* health = entity ? entity->GetComponent<Health>() : nullptr;
            if (health && !found->died.IsConnected()) {
                found->died = health->OnDied.Connect([this, id](const DamageInfo&) { ScheduleRespawn(id); });
                if (health->IsDead()) {
                    ScheduleRespawn(id);
                }
            }
            continue;
        }
        Pawn* pawn = controller->GetPawn();
        if (!pawn) {
            const auto* entity = GetScene().GetEntity(controller->pawn);
            pawn = entity ? entity->GetComponent<Pawn>() : nullptr;
        }
        if (!pawn) {
            pawn = SpawnPawn(*controller);
        }
        if (!pawn) {
            continue;
        }
        if (Pawn* occupant = pawn->GetOccupant()) {
            pawn = occupant;
        }
        Player player;
        player.controller = id;
        player.pawn = pawn->GetEntity().GetId();
        player.start = pawn->GetEntity().GetTransform().GetWorldPosition();
        if (auto* health = pawn->GetEntity().GetComponent<Health>()) {
            player.died = health->OnDied.Connect([this, id](const DamageInfo&) { ScheduleRespawn(id); });
        }
        m_players.push_back(std::move(player));
        if (!controller->GetPawn()) {
            controller->Possess(*pawn);
        }
        if (const auto* health = pawn->GetEntity().GetComponent<Health>(); health && health->IsDead()) {
            ScheduleRespawn(id);
        }
    }
}

void GameMode::ScheduleRespawn(Spot::EntityId controller) {
    for (auto& player : m_players) {
        if (player.controller == controller) {
            CancelInvoke(player.respawn);
            player.respawn =
                Invoke(std::max(respawnTime, 0.0f), [this, controller] { ReturnPlayer(controller, true); });
            return;
        }
    }
}

float GameMode::GetRespawnTimeLeft(const Controller& controller) const {
    for (const auto& player : m_players) {
        if (player.controller == controller.GetEntity().GetId()) {
            return GetInvokeTimeLeft(player.respawn);
        }
    }
    return 0.0f;
}

void GameMode::ReturnPlayer(Spot::EntityId id, bool restoreHealth) {
    for (auto& player : m_players) {
        if (player.controller != id) {
            continue;
        }
        auto* controllerEntity = GetScene().GetEntity(id);
        auto* pawnEntity = GetScene().GetEntity(player.pawn);
        auto* controller = controllerEntity ? controllerEntity->GetComponent<Controller>() : nullptr;
        auto* pawn = pawnEntity ? pawnEntity->GetComponent<Pawn>() : nullptr;
        if (restoreHealth) {
            player.respawn = Spot::InvokeId::Invalid;
        }
        if (controller && pawn) {
            const SpawnPoint* spawn = ChooseSpawn(*controller);
            const Spot::Vector3 start = spawn ? spawn->GetEntity().GetTransform().GetWorldPosition() : player.start;
            if (restoreHealth) {
                RespawnPlayer(*controller, *pawn, start);
            } else {
                pawn->GetEntity().GetTransform().SetWorldPosition(start);
                pawn->ResetMotion();
            }
        }
        return;
    }
}

void GameMode::RespawnPlayer(Controller& player, Pawn& pawn, Spot::Vector3 start) {
    pawn.GetEntity().GetTransform().SetWorldPosition(start);
    pawn.ResetMotion();
    if (auto* health = pawn.GetEntity().GetComponent<Health>()) {
        health->Restore();
    }
    player.Possess(pawn);
}

SpawnPoint* GameMode::ChooseSpawn(const Controller& player) {
    // Other players' pawns, to keep away from.
    std::vector<Spot::Vector3> others;
    for (const Player& tracked : m_players) {
        if (tracked.controller == player.GetEntity().GetId()) {
            continue;
        }
        if (const Spot::Entity* pawn = GetScene().GetEntity(tracked.pawn)) {
            others.push_back(pawn->GetTransform().GetWorldPosition());
        }
    }
    SpawnPoint* best = nullptr;
    float bestDistance = -1.0f;
    GetScene().ForEachComponent<SpawnPoint>([&](SpawnPoint& spawn) {
        if (!spawnTag.empty() && !spawn.tag.empty() && spawn.tag != spawnTag) {
            return;
        }
        const Spot::Vector3 position = spawn.GetEntity().GetTransform().GetWorldPosition();
        float distance = std::numeric_limits<float>::max();
        for (const Spot::Vector3& other : others) {
            distance = std::min(distance, Spot::Distance(position, other));
        }
        // The first of equally free ones, so a player alone always starts at the map's first spawn point.
        if (distance > bestDistance) {
            best = &spawn;
            bestDistance = distance;
        }
    });
    return best;
}

Pawn* GameMode::SpawnPawn(Controller& player) {
    if (!playerPrefab) {
        return nullptr;
    }
    Spot::Entity* root = GetScene().Instantiate(playerPrefab);
    Pawn* pawn = root ? root->GetComponent<Pawn>() : nullptr;
    if (!pawn) {
        if (root) {
            root->Destroy();
        }
        return nullptr;
    }
    if (const SpawnPoint* spawn = ChooseSpawn(player)) {
        const Spot::Transform& place = spawn->GetEntity().GetTransform();
        root->GetTransform().SetWorldPosition(place.GetWorldPosition());
        root->GetTransform().SetWorldRotation(place.GetWorldRotation());
    }
    player.pawn = root->GetId();
    return pawn;
}

void GameMode::Restart() {
    auto* assets = Spot::Assets::GetManager();
    if (restartScene.IsValid() && assets && GetScene().IsLoaded()) {
        if (auto scene = Spot::InstantiateScene(*assets, restartScene)) {
            Spot::Application::Get().LoadScene(std::move(scene));
        }
    }
}

void GameMode::OnStart() {
    if (Spot::Network::IsServer(GetScene())) {
        TrackPlayers();
    }
}

void GameMode::OnUpdate(float /*deltaTime*/) {
    if (!Spot::Network::IsServer(GetScene())) {
        return;
    }
    TrackPlayers();
    for (const auto& player : m_players) {
        const auto* entity = GetScene().GetEntity(player.pawn);
        const auto* pawn = entity ? entity->GetComponent<Pawn>() : nullptr;
        if (pawn && !pawn->HasState("Seated") && entity->GetTransform().GetWorldPosition().y < fallHeight) {
            ReturnPlayer(player.controller, false);
        }
    }
}

void GameMode::OnDisable() {
    for (auto& player : m_players) {
        CancelInvoke(player.respawn);
        player.respawn = Spot::InvokeId::Invalid;
        player.died.Disconnect();
    }
}

void GameMode::Serialize(Spot::Archive& archive) {
    archive.Field("respawnTime", respawnTime, {.min = 0.0f, .max = 60.0f, .speed = 0.1f});
    archive.Field("fallHeight", fallHeight, {.tooltip = "Below this height, a player's pawn returns to its start"});
    archive.Field("restartScene", restartScene, {.assetType = Spot::AssetType::Scene});
    archive.Field("playerPrefab", playerPrefab, {.tooltip = "The pawn of a player who has none"});
    archive.Field("spawnTag", spawnTag,
                  {.tooltip = "The spawn points it uses, with those without a tag; any when empty"});
}

} // namespace Gameplay
