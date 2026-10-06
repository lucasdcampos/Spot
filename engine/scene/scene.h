#pragma once

#include "scene/entity.h"

#include <concepts>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Spot {

class Application;
class AssetManager;
class Event;
class PhysicsWorld;
class RenderContext;

// How Scene::Instantiate() adds saved entities.
struct InstantiateOptions {
    // The saved entities without a parent among them become children of this entity of the scene, or roots when
    // null.
    Entity* parent = nullptr;
    // Where the first of those goes among its siblings, the others following it; after the existing ones when unset.
    std::optional<std::size_t> siblingIndex = std::nullopt;
    // Gives the entities their saved ids rather than new ones, such as to bring back deleted entities. Ids in use in
    // the scene are replaced by new ones.
    bool keepIds = false;
    // References to entities that were not saved along keep pointing at the scene's entities with those ids, as
    // copies of entities of the same scene should. Off, they load as EntityId::Invalid, since ids in text from
    // elsewhere mean nothing here.
    bool keepExternalReferences = false;
    // References to assets load through it, or through the running application's asset manager when null.
    AssetManager* assets = nullptr;
};

// Owns a hierarchy of entities and drives their components. A game usually derives a scene per level or screen, and
// builds its entities in OnLoad():
//
//     class Level final : public Spot::Scene {
//     protected:
//         void OnLoad() override { CreateEntity("Player").AddComponent<PlayerController>(); }
//     };
//
//     application.LoadScene<Level>();
//
// Scenes also work on their own, without an application, by calling Update() directly.
//
// Entities and components may be created, destroyed and removed from inside component hooks and ForEach callbacks.
// Destroyed entities and removed components leave queries at once, but while the scene runs Update() or a ForEach
// they are only freed once it returns, so references to them stay valid until then. Outside of those they are freed
// at once.
class Scene {
public:
    Scene();
    // Clears the scene. The application clears its scenes before destroying them, so derived scenes are still whole
    // while their components get OnDestroy().
    virtual ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    // Whether the scene is an application's current scene.
    bool IsLoaded() const { return m_application != nullptr; }
    // Only available while the scene is loaded.
    Application& GetApplication() const;

    // Creates an entity at the root of the hierarchy.
    Entity& CreateEntity(std::string name = {});
    // Destroys the entity and all its descendants.
    void DestroyEntity(Entity& entity);
    // Destroys every entity.
    void Clear();

    // Simulates the scene's colliders and rigidbodies.
    PhysicsWorld& GetPhysics() const { return *m_physics; }

    // Null when the entity was destroyed, or never belonged to this scene.
    Entity* GetEntity(EntityId id) const;
    // These find inactive entities too, but not destroyed ones. The first match is the oldest entity.
    Entity* FindEntity(std::string_view name) const;
    Entity* FindEntityWithTag(std::string_view tag) const;
    std::vector<Entity*> FindEntitiesWithTag(std::string_view tag) const;
    // The entities without a parent, in their sibling order.
    std::vector<Entity*> GetRootEntities() const;
    std::size_t GetEntityCount() const;

    // Saves every entity, with its name, tags, activity, transform, place in the hierarchy and registered
    // components, as JSON.
    std::string SaveToString() const;
    bool SaveToFile(const std::filesystem::path& path) const;
    // Adds the saved entities to the scene, next to the ones it has. Their components start on the next Update().
    // Returns false, having added nothing, when the text is not a saved scene; components of unknown types are
    // skipped with a warning.
    //
    // References to assets load through the given asset manager, or the running application's when null.
    bool LoadFromString(std::string_view text, AssetManager* assets = nullptr);
    bool LoadFromFile(const std::filesystem::path& path, AssetManager* assets = nullptr);

    // Adds saved entities like LoadFromString(), as the options say, and returns those that had no parent among
    // them, in order. Returns nothing, having added nothing, when the text is not a saved scene.
    std::vector<Entity*> Instantiate(std::string_view text, const InstantiateOptions& options = {});
    // Saves the entities, which must belong to this scene, with their descendants, in the format of SaveToString().
    // An entity below another of the list is saved once, with that one.
    std::string SaveEntitiesToString(std::span<Entity* const> entities) const;

    // The first component that is a T on any entity, active or not; null when there is none.
    template <std::derived_from<Component> T>
    T* FindComponent() const {
        for (const auto& entity : m_entities) {
            if (IsAlive(entity)) {
                if (T* found = entity->GetComponent<T>()) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    // Runs one frame of the scene:
    // 1. OnStart() for the active components that have not started yet, followed by OnEnable().
    // 2. The physics steps due since the last update, each after OnFixedUpdate() for every active component; see
    //    PhysicsWorld.
    // 3. The scene's own OnUpdate().
    // 4. OnUpdate() for every active component, then OnLateUpdate() for every active component.
    // Entities go in the order they were created, and components in the order they were added. Components added
    // while it runs may have to wait for the next Update() to start.
    void Update(float deltaTime);

    // Calls fn(Entity&) for each entity, active or not, in creation order. fn may create and destroy entities.
    template <typename F>
    void ForEachEntity(F&& fn) {
        const Detail::DeferRemovalsScope defer(*this);
        // By index, because fn may create entities.
        for (std::size_t i = 0; i < m_entities.size(); ++i) {
            if (IsAlive(m_entities[i])) {
                fn(*m_entities[i]);
            }
        }
    }

    // Calls fn(T&) for each enabled component that is a T on an entity active in the hierarchy, in creation order.
    // Components that have not started yet are included, so a new entity can be drawn before its first update.
    template <std::derived_from<Component> T, typename F>
    void ForEachComponent(F&& fn) {
        ForEachEntity([&fn](Entity& entity) {
            if (entity.IsActiveInHierarchy()) {
                entity.ForEachComponent<T>([&fn](T& component) {
                    if (component.IsEnabled()) {
                        fn(component);
                    }
                });
            }
        });
    }

protected:
    // Called when the scene becomes the application's current scene, before its first update.
    virtual void OnLoad() {}
    // Called when the application replaces the scene or stops, before its entities are destroyed.
    virtual void OnUnload() {}
    // Called by Update(), after components start and before they update.
    virtual void OnUpdate(float /*deltaTime*/) {}
    // Called by the application with each event, after Application::OnEvent().
    virtual void OnEvent(const Event&) {}
    // Called by the application on frames that can be drawn, before Application::OnRender().
    virtual void OnRender(RenderContext&) {}

private:
    friend class Application;
    friend class Entity;
    friend class Detail::DeferRemovalsScope;
    friend struct Detail::SceneSerializer;

    // Whether the slot holds an entity that is not destroyed. Slots are empty after their entity moved to another
    // scene, until removals are processed.
    static bool IsAlive(const std::unique_ptr<Entity>& entity);
    // Creates an entity with that id, or a new one when it is in use.
    Entity& CreateEntityWithId(EntityId id, std::string name);
    // Takes the entity out of the scene, leaving its slot empty, and gives it to another one.
    std::unique_ptr<Entity> Release(Entity& entity);
    void Adopt(std::unique_ptr<Entity> entity);
    void ProcessRemovals();

    // First, so it is destroyed last and components can reach it until the end.
    std::unique_ptr<PhysicsWorld> m_physics;
    std::vector<std::unique_ptr<Entity>> m_entities;
    std::unordered_map<EntityId, Entity*> m_entitiesById;
    // The entities without a parent, in sibling order, which can differ from m_entities' creation order.
    std::vector<Entity*> m_roots;
    Application* m_application = nullptr;
    // Set while the scene walks its entities, so removals asked for meanwhile wait until it is done.
    bool m_deferRemovals = false;
    bool m_hasPendingRemovals = false;
};

} // namespace Spot
