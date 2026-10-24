#include "spot/scene/tween_system.h"

#include "spot/core/debug/diagnostics.h"
#include "spot/core/debug/log.h"
#include "spot/scene/component.h"
#include "spot/scene/entity.h"
#include "spot/scene/scene.h"

namespace Spot {
namespace {

Component* FindOwner(Scene& scene, EntityId entityId, ComponentId componentId) {
    Component* owner = nullptr;
    if (auto* entity = scene.GetEntity(entityId)) {
        entity->ForEachComponent<Component>([&](Component& component) {
            if (component.GetId() == componentId && !component.IsFaulted()) {
                owner = &component;
            }
        });
    }
    return owner;
}

} // namespace

TweenSystem& TweenSystem::Get(Scene& scene) {
    if (auto* system = scene.GetSystem<TweenSystem>()) {
        return *system;
    }
    return scene.AddSystem<TweenSystem>();
}

TweenId TweenSystem::Play(Tween tween, std::function<void()> onComplete) {
    return PlayOwned(std::move(tween), nullptr, std::move(onComplete));
}

TweenId TweenSystem::Play(Component& owner, Tween tween, std::function<void()> onComplete) {
    return PlayOwned(std::move(tween), &owner, std::move(onComplete));
}

TweenId TweenSystem::PlayOwned(Tween tween, Component* owner, std::function<void()> onComplete) {
    Scene* scene = &GetScene();
    TweenOptions options;
    options.onComplete = std::move(onComplete);
    options.onError = [](const std::exception_ptr& error) {
        Log::GetEngineLogger().Error("A tween threw and was cancelled: {}", Detail::DescribeException(error));
    };
    if (owner) {
        if (&owner->GetScene() != scene || owner->IsFaulted()) {
            return TweenId::Invalid;
        }
        const auto entityId = owner->GetEntity().GetId();
        const auto componentId = owner->GetId();
        options.isAlive = [scene, entityId, componentId] {
            return FindOwner(*scene, entityId, componentId) != nullptr;
        };
        options.canAdvance = [scene, entityId, componentId] {
            const auto* component = FindOwner(*scene, entityId, componentId);
            return component && component->IsActiveAndEnabled();
        };
    }
    return m_player.Play(std::move(tween), std::move(options));
}

Tween TweenSystem::MoveTo(Entity& entity, Vector3 position, float duration, Ease ease) {
    Scene* scene = &entity.GetScene();
    const auto id = entity.GetId();
    return Tween::Value(
        duration, entity.GetTransform().GetPosition(), position,
        [scene, id](Vector3 value) {
            if (auto* target = scene->GetEntity(id)) {
                target->GetTransform().SetPosition(value);
            }
        },
        ease);
}

Tween TweenSystem::RotateTo(Entity& entity, Quaternion rotation, float duration, Ease ease) {
    Scene* scene = &entity.GetScene();
    const auto id = entity.GetId();
    return Tween::Value(
        duration, entity.GetTransform().GetRotation(), rotation,
        [scene, id](const Quaternion& value) {
            if (auto* target = scene->GetEntity(id)) {
                target->GetTransform().SetRotation(value);
            }
        },
        ease);
}

Tween TweenSystem::ScaleTo(Entity& entity, Vector3 scale, float duration, Ease ease) {
    Scene* scene = &entity.GetScene();
    const auto id = entity.GetId();
    return Tween::Value(
        duration, entity.GetTransform().GetScale(), scale,
        [scene, id](Vector3 value) {
            if (auto* target = scene->GetEntity(id)) {
                target->GetTransform().SetScale(value);
            }
        },
        ease);
}

void TweenSystem::OnUpdate(float deltaTime) {
    m_player.Update(deltaTime);
}

} // namespace Spot
