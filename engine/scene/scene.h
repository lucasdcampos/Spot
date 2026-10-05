#pragma once

#include "scene/entity.h"

#include <concepts>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Spot {

class Application;
class Event;
class RenderContext;

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
    Scene() = default;
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

    // Null when the entity was destroyed, or never belonged to this scene.
    Entity* GetEntity(EntityId id) const;
    // These find inactive entities too, but not destroyed ones. The first match is the oldest entity.
    Entity* FindEntity(std::string_view name) const;
    Entity* FindEntityWithTag(std::string_view tag) const;
    std::vector<Entity*> FindEntitiesWithTag(std::string_view tag) const;
    std::vector<Entity*> GetRootEntities() const;
    std::size_t GetEntityCount() const;

    // Saves every entity, with its name, tags, activity, transform, place in the hierarchy and registered
    // components, as JSON.
    std::string SaveToString() const;
    bool SaveToFile(const std::filesystem::path& path) const;
    // Adds the saved entities to the scene, next to the ones it has. Their components start on the next Update().
    // Returns false, having added nothing, when the text is not a saved scene; components of unknown types are
    // skipped with a warning.
    bool LoadFromString(std::string_view text);
    bool LoadFromFile(const std::filesystem::path& path);

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
    // 2. The scene's own OnUpdate().
    // 3. OnUpdate() for every active component, then OnLateUpdate() for every active component.
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
    // Takes the entity out of the scene, leaving its slot empty, and gives it to another one.
    std::unique_ptr<Entity> Release(Entity& entity);
    void Adopt(std::unique_ptr<Entity> entity);
    void ProcessRemovals();

    std::vector<std::unique_ptr<Entity>> m_entities;
    std::unordered_map<EntityId, Entity*> m_entitiesById;
    Application* m_application = nullptr;
    // Set while the scene walks its entities, so removals asked for meanwhile wait until it is done.
    bool m_deferRemovals = false;
    bool m_hasPendingRemovals = false;
};

} // namespace Spot
