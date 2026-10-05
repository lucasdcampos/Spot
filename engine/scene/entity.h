#pragma once

#include "math/matrix.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "scene/component.h"
#include "scene/transform.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Spot {

class Scene;

// Identifies an entity within its scene. Unlike a pointer, it can be kept after the entity is destroyed:
// Scene::GetEntity() then returns null. Ids are never reused within a scene.
enum class EntityId : std::uint64_t { Invalid = 0 };

namespace Detail {

// While one is alive, removals asked for in the scene wait until the outermost one ends, so loops over entities and
// components never see them freed under their feet.
class DeferRemovalsScope {
public:
    explicit DeferRemovalsScope(Scene& scene);
    ~DeferRemovalsScope();

    DeferRemovalsScope(const DeferRemovalsScope&) = delete;
    DeferRemovalsScope& operator=(const DeferRemovalsScope&) = delete;

private:
    Scene& m_scene;
    bool m_outermost;
};

} // namespace Detail

// An object in a scene: a name, tags, a transform, a place in the hierarchy and the components that give it behavior.
// Entities are created by Scene::CreateEntity() or CreateChild(), and stay at the same address until they are freed
// after Destroy().
class Entity {
public:
    ~Entity() = default;

    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;

    Scene& GetScene() const { return m_scene; }
    EntityId GetId() const { return m_id; }

    const std::string& GetName() const { return m_name; }
    void SetName(std::string name) { m_name = std::move(name); }

    // Tags group entities so they can be found with Scene::FindEntitiesWithTag(). Adding a tag twice keeps one.
    void AddTag(std::string_view tag);
    // Returns whether the entity had the tag.
    bool RemoveTag(std::string_view tag);
    bool HasTag(std::string_view tag) const;
    std::span<const std::string> GetTags() const { return m_tags; }

    // An inactive entity, and every entity below it, has no active components: they get neither OnStart() nor
    // updates, and Scene::ForEachComponent() skips them.
    void SetActive(bool active);
    bool IsActiveSelf() const { return m_activeSelf; }
    // Whether this entity and all its ancestors are active.
    bool IsActiveInHierarchy() const;

    // Moves the entity under another one of the same scene, or to the root when null. The local transform is kept
    // by default, so the entity moves along with its new parent; keepWorldPose keeps its world position and
    // rotation instead.
    void SetParent(Entity* parent, bool keepWorldPose = false);
    Entity* GetParent() const { return m_parent; }
    Entity& CreateChild(std::string name = {});
    // The children that are not destroyed, in the order they were attached. It is a copy, so the hierarchy may change
    // while going through it.
    std::vector<Entity*> GetChildren() const;
    // The first direct child with this name; null when there is none.
    Entity* FindChild(std::string_view name) const;
    bool IsDescendantOf(const Entity& ancestor) const;

    // The transform relative to the parent.
    Transform& GetTransform() { return m_transform; }
    const Transform& GetTransform() const { return m_transform; }
    Matrix4 GetWorldMatrix() const;
    Vector3 GetWorldPosition() const;
    void SetWorldPosition(Vector3 position);
    // Exact when the ancestors are scaled uniformly.
    Quaternion GetWorldRotation() const;
    void SetWorldRotation(const Quaternion& rotation);

    // Destroys the entity and all its descendants; same as Scene::DestroyEntity(*this).
    void Destroy();
    bool IsDestroyed() const { return m_destroyed; }

    // Constructs the component with the given arguments. An entity may hold several components of the same type.
    template <std::derived_from<Component> T, typename... Args>
    T& AddComponent(Args&&... args) {
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T& added = *component;
        Attach(std::move(component));
        return added;
    }

    // The first component that is a T, including types derived from T, enabled or not; null when there is none.
    template <std::derived_from<Component> T>
    T* GetComponent() const {
        for (const auto& component : m_components) {
            if (!component->m_removed) {
                if (auto* found = dynamic_cast<T*>(component.get())) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    template <std::derived_from<Component> T>
    std::vector<T*> GetComponents() const {
        std::vector<T*> found;
        for (const auto& component : m_components) {
            if (!component->m_removed) {
                if (auto* match = dynamic_cast<T*>(component.get())) {
                    found.push_back(match);
                }
            }
        }
        return found;
    }

    template <std::derived_from<Component> T>
    bool HasComponent() const {
        return GetComponent<T>() != nullptr;
    }

    // Looks in this entity first, then in its descendants, depth first.
    template <std::derived_from<Component> T>
    T* GetComponentInChildren() const {
        if (T* found = GetComponent<T>()) {
            return found;
        }
        for (const Entity* child : m_children) {
            if (!child->m_destroyed) {
                if (T* found = child->GetComponentInChildren<T>()) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    // Looks in this entity first, then in its ancestors, nearest first.
    template <std::derived_from<Component> T>
    T* GetComponentInParent() const {
        for (const Entity* entity = this; entity; entity = entity->m_parent) {
            if (T* found = entity->GetComponent<T>()) {
                return found;
            }
        }
        return nullptr;
    }

    // The component leaves queries and gets OnDisable() at once; it gets OnDestroy() and is freed as Scene describes.
    void RemoveComponent(Component& component);

    // Removes the first component that is a T; returns whether there was one.
    template <std::derived_from<Component> T>
    bool RemoveComponent() {
        if (T* component = GetComponent<T>()) {
            RemoveComponent(*component);
            return true;
        }
        return false;
    }

    // Calls fn(T&) for each component that is a T, enabled or not. fn may add and remove components.
    template <std::derived_from<Component> T, typename F>
    void ForEachComponent(F&& fn) {
        const Detail::DeferRemovalsScope defer(m_scene);
        // By index, because fn may add components.
        for (std::size_t i = 0; i < m_components.size(); ++i) {
            Component& component = *m_components[i];
            if (!component.m_removed) {
                if (auto* found = dynamic_cast<T*>(&component)) {
                    fn(*found);
                }
            }
        }
    }

private:
    friend class Component;
    friend class Scene;

    Entity(Scene& scene, EntityId id, std::string name);

    void Attach(std::unique_ptr<Component> component);
    void MarkDestroyed();
    // Brings OnEnable() and OnDisable() up to date for the components of this entity and its descendants.
    void RefreshActivity();
    void StartComponents();
    void UpdateComponents(float deltaTime);
    void LateUpdateComponents(float deltaTime);
    // Calls OnDestroy() on the started components being removed, or on all of them once the entity is destroyed, and
    // frees the removed components that are done.
    void ProcessRemovals();
    bool CanBeFreed() const;

    Scene& m_scene;
    EntityId m_id;
    std::string m_name;
    std::vector<std::string> m_tags;
    Transform m_transform;
    Entity* m_parent = nullptr;
    std::vector<Entity*> m_children;
    std::vector<std::unique_ptr<Component>> m_components;
    bool m_activeSelf = true;
    bool m_destroyed = false;
};

} // namespace Spot
