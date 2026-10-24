#pragma once

#include "spot/core/ids/asset_ref.h"
#include "spot/render/camera_2d.h"
#include "spot/render/render_stage.h"
#include "spot/scene/entity.h"
#include "spot/scene/scene_system.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace Spot {

class Application;
class Scene;
class AssetManager;
class Event;
class AudioSystem;
class Model;
class PhysicsWorld;
class PhysicsWorld2D;
class Prefab;
class RenderContext;
class InputState;
class UiSystem;
class UiInputSystem;

// A chunk finishes on Scene::Update(). Its root becomes null after destruction or scene clearing.
class ChunkLoad {
public:
    struct State {
        bool done = false;
        Scene* scene = nullptr;
        EntityId root = EntityId::Invalid;
    };
    ChunkLoad() = default;
    explicit ChunkLoad(std::shared_ptr<State> state) : m_state(std::move(state)) {}
    bool IsValid() const { return m_state != nullptr; }
    bool IsDone() const { return m_state && m_state->done; }
    Entity* GetRoot() const;

private:
    std::shared_ptr<State> m_state;
};

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
    // Gives the entities saved with these ids the ids they map to, which must be free, as tools do to renumber a
    // tree. The others get ids as keepIds says.
    const std::unordered_map<EntityId, EntityId>* ids = nullptr;
    // Keeps the components of types that are not registered as MissingComponents, so saving them again keeps them,
    // as tools that edit scenes ask for. Games skip them with a warning.
    bool keepUnknownComponents = false;
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

    // Draws at the stage of every frame the scene is drawn in, by a SceneRenderer or the editor's views, after the
    // hooks added before it. Components add theirs in OnEnable() and remove them in OnDisable(). A hook that throws
    // is logged and removed.
    RenderHookId AddRenderHook(RenderStage stage, RenderHook hook);
    void RemoveRenderHook(RenderHookId id);
    // Runs the hooks of the context's stage, as renderers do.
    void RunRenderHooks(const RenderStageContext& context);

    // Simulates the scene's colliders and rigidbodies; created on the first call.
    PhysicsWorld& GetPhysics();
    PhysicsWorld2D& GetPhysics2D();
    // Plays the scene's sounds where they are; created on the first call.
    AudioSystem& GetAudio();
    UiSystem& GetUi();
    UiInputSystem& GetUiInput();

    // Adds a system of the type, constructed with the arguments. A scene has one system of each type at most.
    template <std::derived_from<SceneSystem> T, typename... Args>
    T& AddSystem(Args&&... args) {
        // Not make_unique, so the engine's systems can keep their constructors to the scene.
        auto system = std::unique_ptr<T>(new T(std::forward<Args>(args)...));
        T& added = *system;
        AttachSystem(std::move(system));
        return added;
    }

    // The system that is a T, including types derived from T; null when there is none.
    template <std::derived_from<SceneSystem> T>
    T* GetSystem() const {
        for (const auto* systems : {&m_systems, &m_pendingSystems}) {
            for (const auto& system : *systems) {
                if (auto* found = dynamic_cast<T*>(system.get())) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    // The seconds each fixed step of Update() simulates: Component::OnFixedUpdate() and the physics run once per
    // step, so they behave the same at any frame rate.
    static constexpr float DefaultFixedTimeStep = 1.0f / 60.0f;
    // Fixed steps per Update() are capped, so a slow frame does not make the next ones slower still; the time beyond
    // is dropped and the simulation slows down instead.
    static constexpr int MaxFixedStepsPerUpdate = 8;
    float GetFixedTimeStep() const { return m_fixedTimeStep; }
    void SetFixedTimeStep(float seconds);

    // Null when the entity was destroyed, or never belonged to this scene.
    Entity* GetEntity(EntityId id) const;
    // These find inactive entities too, but not destroyed ones. The first match is the oldest entity.
    Entity* FindEntity(std::string_view name) const;
    Entity* FindEntityWithTag(std::string_view tag) const;
    std::vector<Entity*> FindEntitiesWithTag(std::string_view tag) const;
    // The entities without a parent, in their sibling order.
    std::vector<Entity*> GetRootEntities() const;
    std::size_t GetEntityCount() const;
    std::uint64_t GetUpdateCount() const { return m_updateCount; }

    // Saves every entity, with its name, tags, activity, transform, place in the hierarchy and registered
    // components, as JSON.
    // Prefab instances keep their links to their prefabs (see GetPrefabRootId()); expandPrefabs saves them as
    // ordinary entities. Asset files keep instances as their overrides, which the tools write.
    std::string SaveToString(bool expandPrefabs = false) const;
    bool SaveToFile(const std::filesystem::path& path) const;
    // Adds the saved entities to the scene, next to the ones it has. Their components start on the next Update().
    // Returns false, having added nothing, when the text is not a saved scene; components of types that are not
    // registered are skipped with a warning.
    //
    // References to assets load through the given asset manager, or the running application's when null.
    bool LoadFromString(std::string_view text, AssetManager* assets = nullptr);
    bool LoadFromFile(const std::filesystem::path& path, AssetManager* assets = nullptr);

    // Adds saved entities like LoadFromString(), as the options say, and returns those that had no parent among
    // them, in order. Returns nothing, having added nothing, when the text is not a saved scene.
    std::vector<Entity*> Instantiate(std::string_view text, const InstantiateOptions& options = {});
    // Reads a scene asset and its dependencies on workers, then creates its entities below a new root at the
    // start of Update(). Destroying that root unloads the chunk. The manager must outlive this scene.
    ChunkLoad LoadChunkAsync(AssetManager& assets, AssetId scene);
    // Completes ready chunks without advancing simulation, including while an application is paused.
    void FinishChunkLoads();

    // Creates a root with the given name and an entity per model node below it, keeping local transforms and the
    // hierarchy. Each drawable submesh gets a MeshRenderer on its node; meshes and materials remain shared.
    Entity& Instantiate(const Model& model, std::string name = {});
    // Creates the prefab's tree with new ids and remaps internal references; external entity references are cleared.
    // Uses the options' parent, sibling index and asset manager. Null, having added nothing, for an unloaded asset.
    Entity* Instantiate(const AssetRef<Prefab>& prefab, const InstantiateOptions& options = {});
    // Saves the entities, which must belong to this scene, with their descendants, in the format of SaveToString().
    // An entity below another of the list is saved once, with that one.
    std::string SaveEntitiesToString(std::span<Entity* const> entities, bool expandPrefabs = false) const;

    // The first component added that is a T, on any entity, active or not; null when there is none.
    template <std::derived_from<Component> T>
    T* FindComponent() const {
        Component* first = nullptr;
        for (const TypeList& list : m_componentsByType) {
            for (Component* component : list.components) {
                if (!component) {
                    continue;
                }
                if (!Detail::IsComponentOf<T>(*component)) {
                    break;
                }
                if (!component->m_removed && !component->m_entity->IsDestroyed()) {
                    if (!first || component->m_sequence < first->m_sequence) {
                        first = component;
                    }
                    break;
                }
            }
        }
        return static_cast<T*>(first);
    }

    // Runs one frame of the scene:
    // 1. OnStart() for the active components that have not started yet, followed by OnEnable().
    // 2. The systems' OnBeforeFixedSteps().
    // 3. The fixed steps due since the last update, each running OnFixedUpdate() for every active component and
    //    then for every system, which steps the physics; see PhysicsWorld.
    // 4. The systems' OnAfterFixedSteps().
    // 5. The scene's own OnUpdate().
    // 6. OnUpdate() for every active component, then for every system.
    // 7. OnLateUpdate() for every active component, then for every system.
    // Entities go in the order they were created, components in the order they were added, and systems by their
    // order. Components added while it runs may have to wait for the next Update() to start.
    void Update(float deltaTime);

    // Calls the systems' OnBeforeRender(). Whatever draws the scene calls it once per frame it is drawn, before
    // drawing it; the application does for its scenes.
    void PrepareToRender();

    // Changes whenever what ForEachComponent() gives may have: entities or components added, removed, enabled or
    // activated, entities moved in the hierarchy or to another layer. Systems that mirror components elsewhere, such
    // as the render world, go through all of them again only then.
    std::uint64_t GetStructureVersion() const { return m_structureVersion; }
    // Starts recording the entities that move, and those whose components tell they changed, for
    // TakeChangedEntities(). Recording costs nothing until something calls this.
    void TrackChanges();
    // Moves the entities recorded since the last call into entities, once or more each. False when more changed than
    // the scene kept, so the caller goes through every entity instead.
    bool TakeChangedEntities(std::vector<EntityId>& entities);

    // Calls OnDrawUi() for every active component, in the order Update() goes, to draw what goes over the view of
    // the scene, such as a HUD. Whatever shows the scene calls it once the scene is drawn, with the renderer begun
    // with ScreenCamera() for a view of the size:
    //
    //     renderer.Begin(pass, Spot::Scene::ScreenCamera(viewSize), viewSize);
    //     scene.DrawUi(renderer, viewSize);
    //     renderer.End();
    void DrawUi(Renderer2D& renderer, Vector2 viewSize);
    // Positions and view size are viewport pixels, with pointer Y growing down.
    bool HandleUiEvent(const Event& event, Vector2 viewSize, InputState* input = nullptr);
    // The camera that puts (0, 0) at the bottom-left corner of a view of the size, and one unit on each pixel.
    static Camera2D ScreenCamera(Vector2 viewSize);

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

    // Calls fn(T&) for each enabled component that is a T on an entity active in the hierarchy, in the order they were
    // added. Components that have not started yet are included, so a new entity can be drawn before its first update.
    // Components that fn adds are left for the next call.
    //
    // The scene keeps a list of the components of each type, so this goes through the components of the types that
    // are a T, not through every entity.
    template <std::derived_from<Component> T, typename F>
    void ForEachComponent(F&& fn) {
        const Detail::DeferRemovalsScope defer(*this);
        // The ends are taken now, so components that fn adds wait for the next call.
        struct Cursor {
            const std::vector<Component*>* components;
            std::size_t next;
            std::size_t end;
        };
        Cursor cursors[8];
        std::vector<Cursor> moreCursors;
        std::size_t cursorCount = 0;
        for (const TypeList& list : m_componentsByType) {
            const auto found = std::ranges::find_if(list.components, [](const Component* c) { return c != nullptr; });
            if (found == list.components.end() || !Detail::IsComponentOf<T>(**found)) {
                continue;
            }
            const Cursor cursor{&list.components, 0, list.components.size()};
            if (cursorCount < std::size(cursors)) {
                cursors[cursorCount++] = cursor;
            } else {
                moreCursors.push_back(cursor);
            }
        }
        if (cursorCount == 1) {
            const Cursor& only = cursors[0];
            for (std::size_t i = 0; i < only.end; ++i) {
                if (Component* component = (*only.components)[i]; component && IsIncluded(*component)) {
                    fn(static_cast<T&>(*component));
                }
            }
            return;
        }
        // Several types: their lists are merged by when each component was added.
        std::vector<Cursor> all(cursors, cursors + cursorCount);
        all.insert(all.end(), moreCursors.begin(), moreCursors.end());
        while (true) {
            Cursor* earliest = nullptr;
            for (Cursor& cursor : all) {
                while (cursor.next < cursor.end && !(*cursor.components)[cursor.next]) {
                    ++cursor.next;
                }
                if (cursor.next < cursor.end &&
                    (!earliest || (*cursor.components)[cursor.next]->m_sequence <
                                      (*earliest->components)[earliest->next]->m_sequence)) {
                    earliest = &cursor;
                }
            }
            if (!earliest) {
                return;
            }
            Component* component = (*earliest->components)[earliest->next++];
            if (IsIncluded(*component)) {
                fn(static_cast<T&>(*component));
            }
        }
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
    friend class Component;
    friend class Entity;
    friend class Transform;
    friend class Detail::DeferRemovalsScope;
    friend struct Detail::SceneSerializer;

    // Whether the slot holds an entity that is not destroyed. Slots are empty after their entity moved to another
    // scene, until removals are processed.
    static bool IsAlive(const std::unique_ptr<Entity>& entity);
    void MarkEntityChanged(const Entity& entity);
    void MarkStructureChanged() { ++m_structureVersion; }

    struct PendingChunk;
    std::vector<std::shared_ptr<PendingChunk>> m_chunkLoads;
    std::vector<std::weak_ptr<ChunkLoad::State>> m_chunkStates;
    void CancelChunkLoads();

    std::uint64_t m_structureVersion = 0;
    bool m_trackingChanges = false;
    bool m_changesOverflowed = false;
    std::vector<EntityId> m_changedEntities;
    // Whether ForEachComponent() gives the component: enabled, not removed, on a live entity active in the hierarchy.
    static bool IsIncluded(const Component& component);
    // What each stage of an update visits, so a stage only goes through the components it concerns rather than every
    // entity: those not started yet, those whose type overrides each update hook, and those with invokes or
    // coroutines. Each is in the order components were added; leaving one empties a slot until it is compacted.
    enum class HookList { Start, FixedUpdate, Update, LateUpdate, Scheduled, DrawUi, Count };
    struct ComponentList {
        std::vector<Component*> components;
        std::size_t emptySlots = 0;
    };
    void AddToList(HookList list, Component& component);
    void RemoveFromList(HookList list, Component& component);
    void CompactHookLists();
    // Starts the components due to, as Update() does first.
    void StartComponents();
    // Calls the update hook for the active components of the list, leaving out types found not to override it.
    template <typename F>
    void RunHook(HookList list, Detail::ComponentHook hook, std::string_view name, F&& call);
    // Puts a component that has invokes or coroutines in the list that updates them.
    void ScheduleComponent(Component& component) { AddToList(HookList::Scheduled, component); }

    // Puts the component in the list of its type, or takes it out, leaving its slot empty until the list is compacted.
    void ListComponent(Component& component);
    void UnlistComponent(Component& component);
    void CompactComponentLists();
    // Creates an entity with that id, or a new one when it is in use.
    Entity& CreateEntityWithId(EntityId id, std::string name);
    // Takes the entity out of the scene, leaving its slot empty, and gives it to another one.
    std::unique_ptr<Entity> Release(Entity& entity);
    // moved: the entity comes from another scene, with removals of its own that may be pending.
    void Adopt(std::unique_ptr<Entity> entity, bool moved = false);
    void AddRemovalCandidate(Entity& entity);
    // Removes the entity from the roots, looking from the newest, which are the ones most often freed.
    void RemoveRoot(const Entity& entity);
    void CompactSlots();
    void ProcessRemovals();
    void AttachSystem(std::unique_ptr<SceneSystem> system);
    // Calls fn(SceneSystem&) under the guard for each system that is not faulted, by order, profiled under the name.
    template <typename F>
    void RunSystems(std::string_view hook, std::string_view profileName, F&& fn);

    // First, so they are destroyed last and components can reach them until the end.
    std::vector<std::unique_ptr<SceneSystem>> m_systems;
    // Systems added while systems run, which join the others before the next hook.
    std::vector<std::unique_ptr<SceneSystem>> m_pendingSystems;
    PhysicsWorld* m_physics = nullptr;
    struct RenderHookEntry {
        RenderHookId id;
        RenderStage stage;
        RenderHook hook;
    };
    std::vector<RenderHookEntry> m_renderHooks;
    std::uint32_t m_nextRenderHook = 1;
    AudioSystem* m_audio = nullptr;
    // In creation order. Freed and moved entities leave empty slots, which are compacted once they are many.
    std::vector<std::unique_ptr<Entity>> m_entities;
    std::size_t m_emptySlots = 0;
    // Entities that are not destroyed.
    std::size_t m_liveCount = 0;
    // Entities destroyed, or with components removed, since removals were last processed: the only ones processing
    // them needs to visit.
    std::vector<Entity*> m_removalCandidates;
    // The components of each concrete type, by its type index, in the order they were added.
    struct TypeList {
        std::vector<Component*> components;
        std::size_t emptySlots = 0;
    };
    std::vector<TypeList> m_componentsByType;
    std::array<ComponentList, static_cast<std::size_t>(HookList::Count)> m_hookLists;
    std::uint64_t m_nextComponentSequence = 1;
    std::unordered_map<EntityId, Entity*> m_entitiesById;
    // The entities without a parent, in sibling order, which can differ from m_entities' creation order.
    std::vector<Entity*> m_roots;
    Application* m_application = nullptr;
    // Set while the scene walks its entities, so removals asked for meanwhile wait until it is done.
    bool m_deferRemovals = false;
    bool m_hasPendingRemovals = false;
    bool m_runningSystems = false;
    float m_fixedTimeStep = DefaultFixedTimeStep;
    float m_fixedTimeLeft = 0.0f;
    std::uint64_t m_updateCount = 0;
};

} // namespace Spot
