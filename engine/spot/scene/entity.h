#pragma once

#include "spot/core/ids/asset_ref.h"
#include "spot/core/ids/entity_id.h"
#include "spot/scene/component.h"
#include "spot/scene/component_requirements.h"
#include "spot/scene/transform.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Spot {

class Prefab;
class Scene;

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

// An object in a scene: a name, tags, a layer, a transform, a place in the hierarchy and the components that give it
// behavior. Entities are created by Scene::CreateEntity() or CreateChild(), and stay at the same address until they
// are freed after Destroy().
class Entity {
public:
    // Layers sort entities for systems that pick what they deal with by layer, such as which bodies physics lets
    // collide (see PhysicsSettings) and what queries see. Games name them in their settings; 0 is the default one.
    static constexpr int LayerCount = 32;

    ~Entity() = default;

    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;

    Scene& GetScene() const { return *m_scene; }
    EntityId GetId() const { return m_id; }
    // The root of the prefab instance the entity belongs to, and its id in the prefab; invalid and zero for entities
    // of no instance. An instance keeps nothing else: the tools work out its overrides from these.
    EntityId GetPrefabRootId() const { return m_prefabRoot; }
    std::uint64_t GetPrefabLocalId() const { return m_prefabLocalId; }
    // On the root of a prefab instance, the prefab it was made from; empty elsewhere.
    const AssetRef<Prefab>& GetPrefabAsset() const { return m_prefabAsset; }
    AssetId GetPrefabAssetId() const { return m_prefabAsset.GetId(); }

    const std::string& GetName() const { return m_name; }
    void SetName(std::string name) { m_name = std::move(name); }

    // Tags group entities so they can be found with Scene::FindEntitiesWithTag(). Adding a tag twice keeps one.
    void AddTag(std::string_view tag);
    // Returns whether the entity had the tag.
    bool RemoveTag(std::string_view tag);
    bool HasTag(std::string_view tag) const;
    std::span<const std::string> GetTags() const { return m_tags; }

    // From 0 to LayerCount - 1. The entity's own, which its children do not inherit.
    int GetLayer() const { return m_layer; }
    void SetLayer(int layer);

    // An inactive entity, and every entity below it, has no active components: they get neither OnStart() nor
    // updates, and Scene::ForEachComponent() skips them.
    void SetActive(bool active);
    bool IsActiveSelf() const { return m_activeSelf; }
    // Whether this entity and all its ancestors are active.
    bool IsActiveInHierarchy() const;

    // Moves the entity under another one of the same scene, or to the root when null, after its new siblings. The
    // local transform is kept by default, so the entity moves along with its new parent; keepWorldPose keeps its
    // world position and rotation instead.
    void SetParent(Entity* parent, bool keepWorldPose = false);
    Entity* GetParent() const { return m_parent; }
    Entity& CreateChild(std::string name = {});
    // The children that are not destroyed, in order. It is a copy, so the hierarchy may change while going through
    // it.
    std::vector<Entity*> GetChildren() const;
    // Visits live children without allocating a copy; callers must not reorder the hierarchy during traversal.
    template <typename F>
    void ForEachChild(F&& fn) const {
        const Detail::DeferRemovalsScope defer(*m_scene);
        for (std::size_t i = 0; i < m_children.size(); ++i) {
            if (!m_children[i]->m_destroyed)
                fn(*m_children[i]);
        }
    }
    // The entity's place among its parent's children, or among the scene's root entities, counting from 0.
    // Destroyed siblings do not count. The order is the one hierarchies show and scenes save; it does not change the
    // order entities update in.
    std::size_t GetSiblingIndex() const;
    // Moves the entity to that place among its siblings, or after the last one when the index is past them.
    void SetSiblingIndex(std::size_t index);
    // The first direct child with this name; null when there is none.
    Entity* FindChild(std::string_view name) const;
    bool IsDescendantOf(const Entity& ancestor) const;

    Transform& GetTransform() { return m_transform; }
    const Transform& GetTransform() const { return m_transform; }

    // Moves the entity and all its descendants to another scene, keeping their ids, components and state. A child is
    // first detached from its parent, keeping its world position and rotation. When the other scene is in the middle
    // of an update, the moved components may run again in that update.
    void MoveToScene(Scene& scene);

    // Keeps this entity and its descendants when the application replaces its scene. A retained child of an
    // ordinary entity detaches with its world pose. Explicit unloading and application shutdown destroy it.
    void SetKeepOnSceneChange(bool keep = true) { m_keepOnSceneChange = keep; }
    bool IsKeptOnSceneChange() const { return m_keepOnSceneChange; }

    // Saves the entity and its descendants in the format of Scene::SaveToString(). Loading the text into a scene
    // creates a copy of them, so it also works as a template for entities made many times.
    std::string SaveToString(bool expandPrefabs = false) const;

    // Destroys the entity and all its descendants; same as Scene::DestroyEntity(*this).
    void Destroy();
    bool IsDestroyed() const { return m_destroyed; }

    // Constructs the component with the given arguments. An entity may hold several components of the same type.
    // Components the type requires (see SPOT_REQUIRE_COMPONENTS()) that the entity lacks are added first.
    template <std::derived_from<Component> T, typename... Args>
    T& AddComponent(Args&&... args) {
        Detail::DeclareComponentRequirements<T>();
        AddRequiredComponents(Detail::GetComponentTypeIndex<T>());
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T& added = *component;
        Attach(std::move(component));
        return added;
    }

    // Adds a component made elsewhere, such as by CreateComponent() from a type's registered name, at that place
    // among the entity's components, or last, after the components its type requires that the entity lacks.
    Component& AddComponent(std::unique_ptr<Component> component, std::optional<std::size_t> index = std::nullopt);

    // The component's place among the entity's components that are not being removed, which is the order they update
    // in and are saved in.
    std::size_t GetComponentIndex(const Component& component) const;
    // Moves the component to that place, or last when the index is past the others.
    void MoveComponent(Component& component, std::size_t index);

    // The first component that is a T, including types derived from T, enabled or not; null when there is none.
    template <std::derived_from<Component> T>
    T* GetComponent() const {
        for (const auto& component : m_components) {
            if (!component->m_removed && Detail::IsComponentOf<T>(*component)) {
                return static_cast<T*>(component.get());
            }
        }
        return nullptr;
    }

    template <std::derived_from<Component> T>
    std::vector<T*> GetComponents() const {
        std::vector<T*> found;
        for (const auto& component : m_components) {
            if (!component->m_removed && Detail::IsComponentOf<T>(*component)) {
                found.push_back(static_cast<T*>(component.get()));
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
    // A component another one of the entity requires, and the only one of its type, stays, with an error logged.
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
        const Detail::DeferRemovalsScope defer(*m_scene);
        // By index, because fn may add components.
        for (std::size_t i = 0; i < m_components.size(); ++i) {
            Component& component = *m_components[i];
            if (!component.m_removed && Detail::IsComponentOf<T>(component)) {
                fn(static_cast<T&>(component));
            }
        }
    }

private:
    friend class Component;
    friend class Scene;
    friend class Transform;
    friend struct Detail::SceneSerializer;

    Entity(Scene& scene, EntityId id, std::string name);

    void Attach(std::unique_ptr<Component> component, std::optional<std::size_t> index = std::nullopt);
    void AddRequiredComponents(Detail::ComponentTypeIndex type);
    // The type of a component of the entity that requires the given one, which is the only one of its kind; null
    // when none does.
    const Component* FindDependent(const Component& required) const;
    // The list the entity is ordered in: its parent's children, or the scene's roots.
    std::vector<Entity*>& GetSiblings() const;
    void MoveSubtree(Scene& scene);
    void MarkDestroyed();
    // Brings OnEnable() and OnDisable() up to date for the components of this entity and its descendants.
    void RefreshActivity();
    // Calls OnDestroy() on the started components being removed, or on all of them once the entity is destroyed, and
    // frees the removed components that are done.
    void ProcessRemovals();
    bool CanBeFreed() const;

    Scene* m_scene;
    // Its place among its scene's entities.
    std::size_t m_slot = 0;
    EntityId m_id;
    EntityId m_prefabRoot = EntityId::Invalid;
    std::uint64_t m_prefabLocalId = 0;
    AssetRef<Prefab> m_prefabAsset;
    std::string m_name;
    std::vector<std::string> m_tags;
    std::uint8_t m_layer = 0;
    Transform m_transform{*this};
    Entity* m_parent = nullptr;
    std::vector<Entity*> m_children;
    std::vector<std::unique_ptr<Component>> m_components;
    bool m_activeSelf = true;
    bool m_keepOnSceneChange = false;
    bool m_destroyed = false;
};

template <std::derived_from<Component> T, typename... Args>
T& Component::AddComponent(Args&&... args) {
    return GetEntity().AddComponent<T>(std::forward<Args>(args)...);
}

template <std::derived_from<Component> T>
T* Component::GetComponent() const {
    return GetEntity().GetComponent<T>();
}

namespace Detail {

template <typename... Required>
std::vector<ComponentRequirement> MakeComponentRequirements(RequiredComponentList<Required...>*) {
    (DeclareComponentRequirements<Required>(), ...);
    return {
        ComponentRequirement{.type = GetComponentTypeIndex<Required>(),
                             .isPresent = [](const Entity& entity) { return entity.HasComponent<Required>(); },
                             .matches = [](const Component& component) { return IsComponentOf<Required>(component); },
                             .create = []() -> std::unique_ptr<Component> { return std::make_unique<Required>(); }}...};
}

template <typename T>
void DeclareComponentRequirements() {
    if constexpr (ComponentRequirementsAccess::Declares<T>) {
        // Declared again whenever the table lacks it, as after the type was unregistered.
        if (!FindComponentRequirements(GetComponentTypeIndex<T>())) {
            using List = ComponentRequirementsAccess::Of<T>;
            SetComponentRequirements(GetComponentTypeIndex<T>(),
                                     MakeComponentRequirements(static_cast<List*>(nullptr)));
        }
    }
}

} // namespace Detail

} // namespace Spot
