#include "developer_mode.h"

#include "gameplay/character/character_movement.h"
#include "gameplay/character/character_visual.h"
#include "gameplay/core/player_name.h"
#include "gameplay/core/players.h"
#include "gameplay/effects/effect_library.h"
#include "gameplay/effects/flash.h"
#include "gameplay/health/health.h"
#include "gameplay/menus/settings_menu.h"
#include "gameplay/network/player_info.h"
#include "gameplay/network/remote_controller.h"
#include "gameplay/network/session.h"
#include "gameplay/vehicles/vehicle_seat.h"
#include "gameplay/weapons/firearm.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string_view>

SPOT_COMPONENT(DeveloperMode);

namespace {

struct DeveloperRequest {
    static constexpr std::string_view NetName = "Sandbox.DeveloperRequest";
    void Serialize(Spot::Archive&) {}
};

struct DeveloperPowerRequest {
    static constexpr std::string_view NetName = "Sandbox.DeveloperPowerRequest";
    DeveloperMode::Power power = DeveloperMode::Power::Smite;
    std::string playerName;

    void Serialize(Spot::Archive& archive) {
        archive.Field("power", power);
        archive.Field("playerName", playerName);
    }
};

struct SmiteStrike {
    static constexpr std::string_view NetName = "Sandbox.SmiteStrike";
    Spot::Vector3 position;
    void Serialize(Spot::Archive& archive) { archive.Field("position", position); }
};

void ShowSmite(Spot::Scene& scene, Spot::Vector3 position) {
    Gameplay::ParticleLayer layer;
    layer.settings.maxParticles = 256;
    layer.settings.speed = {0, 0};
    layer.settings.gravity = {};
    layer.settings.lifetime = {0.25f, 0.35f};
    layer.settings.size = {0.08f, 0.08f};
    layer.settings.startColor = {4, 6, 12, 1};
    layer.settings.blend = Spot::ParticleBlend::Additive;
    auto& pool = Gameplay::EffectPool::Get(scene);
    Spot::Vector3 from = position + Spot::Vector3{0, 24, 0};
    constexpr std::array<Spot::Vector2, 9> Offsets = {{{0.8f, 0.2f},
                                                       {-0.4f, -0.3f},
                                                       {0.6f, 0.1f},
                                                       {-0.9f, 0.4f},
                                                       {0.3f, -0.2f},
                                                       {-0.5f, 0.2f},
                                                       {0.4f, -0.3f},
                                                       {-0.2f, 0.1f},
                                                       {0, 0}}};
    for (std::size_t i = 0; i < Offsets.size(); ++i) {
        const float height = 24.0f * (1.0f - static_cast<float>(i + 1) / static_cast<float>(Offsets.size()));
        const auto to = position + Spot::Vector3{Offsets[i].x, height, Offsets[i].y};
        pool.Spawn(layer, (from + to) * 0.5f, to - from, 1, 192, Spot::Distance(from, to));
        from = to;
    }
    Gameplay::Flash::Spawn(scene, position, {0.6f, 0.8f, 1, 1}, 12, 20, 0.35f, 0.8f);
}

DeveloperMode* Sender(Spot::NetworkObject& object, Spot::NetPeerId from) {
    DeveloperMode* found = nullptr;
    object.GetScene().ForEachComponent<Gameplay::RemoteController>([&](auto& controller) {
        auto* body = controller.GetPlayerPawn();
        if (controller.GetPeer() == from && body && &body->GetEntity() == &object.GetEntity()) {
            found = body->GetEntity().GetComponent<DeveloperMode>();
        }
    });
    return found;
}

struct Target {
    Spot::Entity* entity = nullptr;
    std::string error;
};

Target FindTarget(Spot::Scene& scene, std::string_view name) {
    if (!Gameplay::IsValidPlayerName(name)) {
        return {nullptr, "Enter a player nickname without spaces"};
    }
    Target target;
    const auto infos = Gameplay::PlayerInfos::GetAll(scene);
    for (const auto* info : infos) {
        if (info->name != name) {
            continue;
        }
        if (target.entity) {
            return {nullptr, "More than one player has that nickname"};
        }
        target.entity = scene.GetEntity(info->pawn);
    }
    if (infos.empty() && !Spot::Network::IsClient(scene)) {
        const auto* settings = scene.FindComponent<Gameplay::SettingsMenu>();
        const std::string localName = settings ? settings->GetSettings().playerName : "Player";
        if (name == localName) {
            if (auto* pawn = Gameplay::Players::GetBody(scene)) {
                target.entity = &pawn->GetEntity();
            }
        }
    }
    if (!target.entity) {
        target.error = "Player not found: " + std::string(name);
    }
    return target;
}

Gameplay::PlayerInfo* FindInfo(Spot::Entity& character) {
    for (auto* info : Gameplay::PlayerInfos::GetAll(character.GetScene())) {
        if (info->pawn == character.GetId()) {
            return info;
        }
    }
    return nullptr;
}

} // namespace

void DeveloperMode::Bind() {
    auto* system = Spot::Network::Get(GetScene());
    if (system == m_system) {
        return;
    }
    m_system = system;
    if (system && system->IsClient()) {
        system->OnObjectMessage<SmiteStrike>([](Spot::NetworkObject& object, Spot::NetPeerId, SmiteStrike& strike) {
            ShowSmite(object.GetScene(), strike.position);
        });
    }
    if (system && system->IsServer()) {
        system->OnObjectMessage<DeveloperRequest>(
            [](Spot::NetworkObject& object, Spot::NetPeerId from, DeveloperRequest&) {
                // A request can unlock only the sender's own character, including while driving another pawn.
                if (auto* mode = Sender(object, from)) {
                    mode->Unlock();
                }
            });
        system->OnObjectMessage<DeveloperPowerRequest>(
            [](Spot::NetworkObject& object, Spot::NetPeerId from, DeveloperPowerRequest& request) {
                if (auto* mode = Sender(object, from)) {
                    const auto result = mode->UsePower(request.power, request.playerName);
                    if (!result) {
                        Spot::Log::Warning("Developer power refused: {}", result.message);
                    }
                }
            });
    }
}

bool DeveloperMode::ActivateLocal(Spot::Scene& scene) {
    auto* mode = Gameplay::Players::FindComponent<DeveloperMode>(scene);
    if (!mode) {
        return false;
    }
    if (auto* system = Spot::Network::Get(scene); system && system->IsClient()) {
        auto* object = Spot::Network::GetObject(mode->GetEntity());
        if (!object) {
            return false;
        }
        system->SendToServer(*object, DeveloperRequest{});
    } else {
        mode->Unlock();
    }
    return true;
}

Spot::ConsoleResult DeveloperMode::UseLocalPower(Spot::Scene& scene, Power power, std::string_view playerName) {
    auto* mode = Gameplay::Players::FindComponent<DeveloperMode>(scene);
    if (!mode || !mode->IsUnlocked()) {
        return {false, "Developer mode is required: imthegamedev"};
    }
    const auto target = FindTarget(scene, playerName);
    if (!target.entity) {
        return {false, target.error};
    }
    if (auto* system = Spot::Network::Get(scene); system && system->IsClient()) {
        auto* object = Spot::Network::GetObject(mode->GetEntity());
        if (!object) {
            return {false, "No network player character"};
        }
        system->SendToServer(*object, DeveloperPowerRequest{power, std::string(playerName)});
        return {true, "Developer power requested for " + std::string(playerName)};
    }
    return mode->UsePower(power, playerName);
}

Spot::ConsoleResult DeveloperMode::UsePower(Power power, std::string_view playerName) {
    if (!m_unlocked || !Spot::Network::IsServer(GetScene())) {
        return {false, "Developer mode is required"};
    }
    return Strike(GetScene(), power, playerName, &GetEntity());
}

Spot::ConsoleResult DeveloperMode::Strike(Spot::Scene& scene, Power power, std::string_view playerName,
                                          const Spot::Entity* caster) {
    if (!Spot::Network::IsServer(scene)) {
        return {false, "Only the server strikes"};
    }
    if (power != Power::Smite && power != Power::Yeet) {
        return {false, "Unknown developer power"};
    }
    const auto target = FindTarget(scene, playerName);
    if (!target.entity) {
        return {false, target.error};
    }
    auto* health = target.entity->GetComponent<Gameplay::Health>();
    if (!health || health->IsDead()) {
        return {false, "The player is not alive"};
    }
    auto* movement = target.entity->GetComponent<Gameplay::CharacterMovement>();
    if (power == Power::Yeet && !movement) {
        return {false, "The player has no character movement"};
    }
    // Launch the character, including when it was driving, rather than the vehicle it temporarily possessed.
    scene.ForEachComponent<Gameplay::VehicleSeat>([&](auto& seat) {
        auto* occupant = seat.GetOccupant();
        if (occupant && &occupant->GetEntity() == target.entity) {
            seat.Exit();
        }
    });
    const auto position = target.entity->GetTransform().GetWorldPosition();
    const Spot::EntityId by = caster ? caster->GetId() : Spot::EntityId::Invalid;
    if (power == Power::Smite) {
        ShowSmite(scene, position);
        if (auto* system = Spot::Network::Get(scene)) {
            if (auto* object = Spot::Network::GetObject(*target.entity)) {
                system->SendToClients(*object, SmiteStrike{position});
            }
        }
        Gameplay::Damage::Apply(scene, target.entity->GetId(),
                                {.amount = std::numeric_limits<float>::max(),
                                 .type = "Smite",
                                 .point = position,
                                 .impulse = {0, -200, 0},
                                 .instigator = by,
                                 .source = by});
    } else {
        // Away from whoever casts it; without one, as from a server's console, back from where the target looks.
        Spot::Vector3 direction = caster ? position - caster->GetTransform().GetWorldPosition() : Spot::Vector3{};
        direction.y = 0;
        if (Spot::LengthSquared(direction) < 0.01f) {
            const auto* pawn = Gameplay::Pawn::Of(caster ? *caster : *target.entity);
            direction = pawn ? pawn->GetViewPoint().direction * (caster ? 1.0f : -1.0f) : Spot::Vector3{0, 0, -1};
            direction.y = 0;
        }
        if (Spot::LengthSquared(direction) < 0.01f) {
            direction = {0, 0, -1};
        }
        movement->Push(Spot::Normalize(direction) * 45.0f + Spot::Vector3{0, 24, 0});
    }
    return {true, std::string(power == Power::Smite ? "Smote " : "Yeeted ") + std::string(playerName)};
}

void DeveloperMode::Unlock() {
    if (!Spot::Network::IsServer(GetScene())) {
        return;
    }
    m_unlocked = true;
    if (auto* info = FindInfo(GetEntity())) {
        info->developer = true;
    }
    Apply();
}

void DeveloperMode::Apply() {
    if (!m_unlocked) {
        return;
    }
    if (auto* effects = Gameplay::EffectLibrary::Find(GetScene()); effects && !effects->Get("ray_gun.tracer")) {
        for (const auto* source : {"muzzle", "tracer"}) {
            if (const auto* original = effects->Get(source)) {
                auto effect = *original;
                effect.name = std::string("ray_gun.") + source;
                effect.light.color = {0.15f, 1.0f, 0.04f, 1};
                for (auto& layer : effect.layers) {
                    layer.settings.startColor = {0.6f, 8.0f, 0.15f, 1};
                }
                effects->effects.push_back(std::move(effect));
            }
        }
    }
    if (auto* weapon = GetScene().GetEntity(rayGun); weapon && !weapon->IsActiveSelf()) {
        weapon->SetActive(true);
        if (auto* firearm = weapon->GetComponent<Gameplay::Firearm>();
            firearm && Spot::Network::IsServer(GetScene()) && firearm->GetAmmo() < 0) {
            firearm->Refill();
        }
    }
    const auto* visual = GetEntity().GetComponent<Gameplay::CharacterVisual>();
    auto* body = visual ? GetScene().GetEntity(visual->modelRoot) : nullptr;
    if (m_skinApplied || !body || !skin) {
        return;
    }
    for (const auto& part : skin->GetParts()) {
        auto* child = body->FindChild(part.name);
        if (!child) {
            child = &body->CreateChild(part.name);
        }
        child->SetLayer(body->GetLayer());
        auto* renderer = child->GetComponent<Spot::SkinnedMeshRenderer>();
        if (!renderer) {
            renderer = &child->AddComponent<Spot::SkinnedMeshRenderer>();
        }
        renderer->SetMesh(part.mesh);
        renderer->SetMaterial(part.material);
        renderer->SetSubmesh(part.submesh);
        renderer->SetAnimator(body->GetId());
        renderer->SetMeshModel(part.transform);
    }
    m_skinApplied = true;
}

void DeveloperMode::OnUpdate(float /*deltaTime*/) {
    Bind();
    if (Spot::Network::IsServer(GetScene())) {
        if (auto* info = FindInfo(GetEntity())) {
            if (info->developer || info->name == "lucasof" || m_unlocked) {
                m_unlocked = true;
                info->developer = true;
            }
        } else if (Gameplay::Players::FindComponent<DeveloperMode>(GetScene()) == this) {
            const auto* session = Gameplay::Session::Get();
            const auto* settings = GetScene().FindComponent<Gameplay::SettingsMenu>();
            const std::string name = session && session->IsActive() ? session->GetSettings().playerName
                                     : settings                     ? settings->GetSettings().playerName
                                                                    : std::string{};
            m_unlocked = m_unlocked || name == "lucasof";
        }
    }
    Apply();
}

void DeveloperMode::Replicate(Spot::Archive& archive) {
    archive.Field("unlocked", m_unlocked);
    if (archive.IsLoading()) {
        Apply();
    }
}

void DeveloperMode::Serialize(Spot::Archive& archive) {
    archive.Field("rayGun", rayGun);
    archive.Field("skin", skin);
    archive.Field("unlocked", m_unlocked, {.hidden = true});
}
