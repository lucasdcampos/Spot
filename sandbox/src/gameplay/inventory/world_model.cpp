#include "gameplay/inventory/world_model.h"

#include "gameplay/core/pawn.h"
#include "gameplay/core/players.h"
#include "gameplay/inventory/held_item.h"
#include "gameplay/inventory/inventory.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace Gameplay {

SPOT_COMPONENT(WorldModel);

namespace {

void SetModelLayer(Spot::Entity& entity, int layer) {
    entity.SetLayer(layer);
    entity.ForEachChild([layer](Spot::Entity& child) { SetModelLayer(child, layer); });
}

Spot::Entity* FindNode(Spot::Entity& entity, std::string_view name) {
    if (name.empty()) {
        return nullptr;
    }
    if (auto* child = entity.FindChild(name)) {
        return child;
    }
    Spot::Entity* found = nullptr;
    entity.ForEachChild([&](Spot::Entity& child) {
        if (!found) {
            found = FindNode(child, name);
        }
    });
    return found;
}

} // namespace

Spot::Entity* WorldModel::GetModelEntity() const {
    return GetScene().GetEntity(m_instance);
}

Spot::Vector3 WorldModel::GetMuzzlePosition() const {
    const auto* instance = GetModelEntity();
    if (instance && m_shown) {
        const auto nodes = m_shown->GetNodes();
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].name == muzzleNode) {
                const auto matrix = instance->GetTransform().GetWorldMatrix() * m_shown->GetNodeTransform(i);
                const auto point = matrix * Spot::Vector4{0, 0, 0, 1};
                return {point.x, point.y, point.z};
            }
        }
        return instance->GetTransform().GetWorldPosition();
    }
    const Pawn* pawn = Pawn::Of(GetEntity());
    return pawn ? pawn->GetViewPoint().position : GetEntity().GetTransform().GetWorldPosition();
}

int WorldModel::GetModelLayer() const {
    const Pawn* pawn = Pawn::Of(GetEntity());
    return renderLayer >= 0 && pawn && Players::IsPlayedLocally(*pawn) ? std::clamp(renderLayer, 0, 31)
                                                                       : GetEntity().GetLayer();
}

void WorldModel::Clear() {
    if (auto* instance = GetModelEntity()) {
        instance->Destroy();
    }
    m_instance = Spot::EntityId::Invalid;
    m_shown = {};
    if (auto* target = animator.Get(GetScene()); target && !poseLayer.empty()) {
        target->SetLayerWeight(poseLayer, 0);
    }
    Hold(nullptr);
}

void WorldModel::Place(Spot::Entity& instance, Spot::Entity* parent) const {
    instance.SetParent(parent);
    instance.GetTransform().SetPosition(Spot::Vector3{} - m_grip);
    instance.GetTransform().SetRotation({});
}

void WorldModel::Hold(Spot::Entity* instance) {
    if (auto* constraint = aim.Get(GetScene())) {
        if (poseMount) {
            constraint->SetAxis(Spot::Quaternion::FromAxisAngle({0, 1, 0}, instance ? m_mountYaw : 0.0f) * aimAxis);
        }
        constraint->SetWeight(
            instance ? (poseMount ? Spot::Lerp(m_readyAimWeight, 1.0f, m_aimBlend) * (1 - m_sprintBlend) : 1.0f)
                     : 0.0f);
    }
    for (const auto& [hand, node] : {std::pair{&rightHand, &rightHandNode}, std::pair{&leftHand, &leftHandNode}}) {
        if (auto* limb = hand->Get(GetScene())) {
            auto* target = instance ? FindNode(*instance, *node) : nullptr;
            if (target && hand == &leftHand && Spot::LengthSquared(m_supportOffset) > 0) {
                auto* contact = target->FindChild("World Support Grip");
                if (!contact) {
                    contact = &target->CreateChild("World Support Grip");
                }
                contact->GetTransform().SetPosition(m_supportOffset);
                target = contact;
            }
            limb->SetTarget(target ? target->GetId() : Spot::EntityId::Invalid);
            limb->SetWeight(target ? 1.0f : 0.0f);
        }
    }
}

void WorldModel::Synchronize() {
    const auto* inventory = GetEntity().GetComponent<Inventory>();
    const HeldItem* item = inventory && inventory->IsActiveAndEnabled() ? inventory->GetEquipped() : nullptr;
    const Pawn* pawn = Pawn::Of(GetEntity());
    auto* parent = GetScene().GetEntity(mount);
    const auto model =
        item && parent && !(pawn && pawn->HasAnyState(hiddenWhen)) ? item->model : Spot::AssetRef<Spot::Model>{};
    if (model != m_shown || (model && !GetModelEntity())) {
        Clear();
        if (model) {
            m_grip = {};
            const auto nodes = model->GetNodes();
            for (std::size_t i = 0; i < nodes.size(); ++i) {
                if (nodes[i].name == rightHandNode) {
                    const auto point = model->GetNodeTransform(i) * Spot::Vector4{0, 0, 0, 1};
                    m_grip = {point.x, point.y, point.z};
                }
            }
            auto& instance = GetScene().Instantiate(*model, "Held World Model");
            Place(instance, parent);
            m_layer = GetModelLayer();
            SetModelLayer(instance, m_layer);
            m_instance = instance.GetId();
            m_shown = model;
        }
    }
    const bool authored = item && item->worldPose.enabled;
    const ItemStance ready = authored ? item->worldPose.ready : ItemStance{readyPosition, {readyPitch, 0, 0}};
    const ItemStance aiming = authored ? item->worldPose.aiming : ItemStance{aimingPosition, {aimingPitch, 0, 0}};
    const ItemStance sprint = authored ? item->worldPose.sprint : ItemStance{sprintPosition, {sprintPitch, 0, 0}};
    m_supportOffset = authored ? item->worldPose.supportOffset : Spot::Vector3{};
    m_readyAimWeight = authored ? item->worldPose.readyAimWeight : readyAimWeight;
    m_mountYaw = Spot::Lerp(Spot::Lerp(ready.angles.y, aiming.angles.y, m_aimBlend), sprint.angles.y, m_sprintBlend);
    if (poseMount && parent) {
        if (auto* attachment = parent->GetComponent<Spot::BoneAttachment>()) {
            auto offset = attachment->GetOffset();
            offset.translation =
                Spot::Lerp(Spot::Lerp(ready.position, aiming.position, m_aimBlend), sprint.position, m_sprintBlend);
            offset.rotation = Spot::Slerp(Spot::Slerp(Spot::Quaternion::FromEuler(ready.angles),
                                                      Spot::Quaternion::FromEuler(aiming.angles), m_aimBlend),
                                          Spot::Quaternion::FromEuler(sprint.angles), m_sprintBlend);
            attachment->SetOffset(offset);
        }
    }
    if (auto* instance = GetModelEntity(); instance && instance->GetParent() != parent) {
        Place(*instance, parent);
    }
    if (auto* instance = GetModelEntity(); instance && GetModelLayer() != m_layer) {
        m_layer = GetModelLayer();
        SetModelLayer(*instance, m_layer);
    }
    if (auto* target = animator.Get(GetScene()); target && !poseLayer.empty()) {
        target->SetLayerWeight(poseLayer, model ? 1.0f : 0.0f);
    }
    Hold(GetModelEntity());
}

void WorldModel::OnUpdate(float deltaTime) {
    const auto* pawn = Pawn::Of(GetEntity());
    const bool sprinting = pawn && pawn->HasState(sprintState);
    const float target = pawn && pawn->HasState("Aiming") && !sprinting ? 1.0f : 0.0f;
    const float blend = 1 - std::exp(-std::max(poseRate, 0.0f) * std::max(deltaTime, 0.0f));
    m_sprintBlend = Spot::Lerp(m_sprintBlend, sprinting ? 1.0f : 0.0f, blend);
    m_aimBlend = Spot::Lerp(m_aimBlend, target, blend);
    Synchronize();
}

void WorldModel::OnDisable() {
    Clear();
}

void WorldModel::OnDestroy() {
    Clear();
}

void WorldModel::Serialize(Spot::Archive& archive) {
    archive.Field("mount", mount, {.tooltip = "Where the item's grip is attached"});
    archive.Field("animator", animator);
    archive.Field("poseLayer", poseLayer, {.tooltip = "Optional holding layer already configured on the animator"});
    archive.Field("poseMount", poseMount);
    archive.Field("readyPosition", readyPosition);
    archive.Field("aimingPosition", aimingPosition);
    archive.Field("sprintPosition", sprintPosition);
    archive.Field("sprintPitch", sprintPitch);
    archive.Field("sprintState", sprintState);
    archive.Field("readyPitch", readyPitch);
    archive.Field("aimingPitch", aimingPitch);
    archive.Field("poseRate", poseRate, {.min = 0.0f, .max = 50.0f});
    archive.Field("readyAimWeight", readyAimWeight, {.min = 0.0f, .max = 1.0f});
    archive.Field("muzzleNode", muzzleNode);
    archive.Field("rightHandNode", rightHandNode, {.tooltip = "The item's node held by the right hand, at the mount"});
    archive.Field("leftHandNode", leftHandNode);
    archive.Field("aimAxis", aimAxis);
    archive.Field("aim", aim, {.tooltip = "Optional aim constraint, on while an item is shown"});
    archive.Field("rightHand", rightHand, {.tooltip = "Optional constraint that reaches the right hand node"});
    archive.Field("leftHand", leftHand, {.tooltip = "Optional constraint that reaches the left hand node"});
    archive.Field("hiddenWhen", hiddenWhen);
    archive.Field("renderLayer", renderLayer, {.min = -1, .max = 31});
    archive.Field("instance", m_instance, {.hidden = true});
    archive.Field("shown", m_shown, {.hidden = true});
}

} // namespace Gameplay
