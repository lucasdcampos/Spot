#include "gameplay/inventory/world_model.h"

#include "gameplay/core/pawn.h"
#include "gameplay/inventory/held_item.h"
#include "gameplay/inventory/inventory.h"

#include <algorithm>

namespace Gameplay {

SPOT_COMPONENT(WorldModel);

namespace {

void SetModelLayer(Spot::Entity& entity, int layer) {
    entity.SetLayer(layer);
    entity.ForEachChild([layer](Spot::Entity& child) { SetModelLayer(child, layer); });
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

void WorldModel::Clear() {
    if (auto* instance = GetModelEntity()) {
        instance->Destroy();
    }
    m_instance = Spot::EntityId::Invalid;
    m_shown = {};
    if (auto* target = animator.Get(GetScene()); target && !poseLayer.empty()) {
        target->SetLayerWeight(poseLayer, 0);
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
            auto& instance = GetScene().Instantiate(*model, "Held World Model");
            instance.SetParent(parent);
            SetModelLayer(instance, renderLayer < 0 ? GetEntity().GetLayer() : std::clamp(renderLayer, 0, 31));
            instance.GetTransform().SetPosition({});
            instance.GetTransform().SetRotation({});
            m_instance = instance.GetId();
            m_shown = model;
        }
    }
    if (auto* instance = GetModelEntity(); instance && instance->GetParent() != parent) {
        instance->SetParent(parent);
        instance->GetTransform().SetPosition({});
        instance->GetTransform().SetRotation({});
    }
    if (auto* target = animator.Get(GetScene()); target && !poseLayer.empty()) {
        target->SetLayerWeight(poseLayer, model ? 1.0f : 0.0f);
    }
}

void WorldModel::OnUpdate(float /*deltaTime*/) {
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
    archive.Field("muzzleNode", muzzleNode);
    archive.Field("hiddenWhen", hiddenWhen);
    archive.Field("renderLayer", renderLayer, {.min = -1, .max = 31});
    archive.Field("instance", m_instance, {.hidden = true});
    archive.Field("shown", m_shown, {.hidden = true});
}

} // namespace Gameplay
