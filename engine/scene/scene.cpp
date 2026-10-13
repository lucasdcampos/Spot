#include "scene/scene.h"

#include "audio/audio_system.h"
#include "core/assert.h"
#include "core/diagnostics.h"
#include "core/log.h"
#include "core/profiler.h"
#include "graphics/renderer_2d.h"
#include "input/input.h"
#include "physics/physics_world.h"
#include "physics2d/physics_world_2d.h"
#include "ui/canvas.h"
#include "ui/ui_input_system.h"
#include "ui/ui_system.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <typeinfo>
#include <utility>

namespace Spot {

namespace {

// Shared by every scene, so an entity keeps its id when it moves to another scene.
std::atomic<std::uint64_t> s_nextEntityId = 1;

} // namespace

Scene::Scene() = default;

Scene::~Scene() {
    Clear();
}

Application& Scene::GetApplication() const {
    SPOT_ASSERT(m_application, "The application is only available while the scene is loaded");
    return *m_application;
}

PhysicsWorld2D& Scene::GetPhysics2D() {
    if (auto* world = GetSystem<PhysicsWorld2D>())
        return *world;
    return AddSystem<PhysicsWorld2D>();
}

PhysicsWorld& Scene::GetPhysics() {
    if (!m_physics) {
        m_physics = &AddSystem<PhysicsWorld>();
    }
    return *m_physics;
}

RenderHookId Scene::AddRenderHook(RenderStage stage, RenderHook hook) {
    const auto id = static_cast<RenderHookId>(m_nextRenderHook++);
    m_renderHooks.push_back({id, stage, std::move(hook)});
    return id;
}

void Scene::RemoveRenderHook(RenderHookId id) {
    std::erase_if(m_renderHooks, [id](const RenderHookEntry& entry) { return entry.id == id; });
}

void Scene::RunRenderHooks(const RenderStageContext& context) {
    // By index and id, as a hook may add or remove hooks while it runs.
    for (std::size_t i = 0; i < m_renderHooks.size(); ++i) {
        if (m_renderHooks[i].stage != context.stage) {
            continue;
        }
        const RenderHookId id = m_renderHooks[i].id;
        try {
            const RenderHook hook = m_renderHooks[i].hook;
            hook(context);
        } catch (const std::exception& error) {
            Log::GetEngineLogger().Error("A render hook threw and was removed: {}", error.what());
            RemoveRenderHook(id);
            --i;
        } catch (...) {
            Log::GetEngineLogger().Error("A render hook threw and was removed");
            RemoveRenderHook(id);
            --i;
        }
    }
}

AudioSystem& Scene::GetAudio() {
    if (!m_audio) {
        m_audio = &AddSystem<AudioSystem>();
    }
    return *m_audio;
}

void Scene::SetFixedTimeStep(float seconds) {
    SPOT_ASSERT(seconds > 0.0f, "The fixed time step must be positive");
    m_fixedTimeStep = seconds;
}

void Scene::AttachSystem(std::unique_ptr<SceneSystem> system) {
    const std::type_info& type = typeid(*system);
    for (const auto* systems : {&m_systems, &m_pendingSystems}) {
        SPOT_ASSERT(std::ranges::none_of(*systems, [&type](const auto& other) { return typeid(*other) == type; }),
                    "The scene has a system of this type already");
    }
    system->m_scene = this;
    system->m_profileName = Profiler::Intern(Detail::GetTypeName(type));
    if (m_runningSystems) {
        m_pendingSystems.push_back(std::move(system));
        return;
    }
    // After the systems of the same order, so those run in the order they were added.
    const auto place =
        std::ranges::upper_bound(m_systems, system->m_order, {}, [](const auto& other) { return other->m_order; });
    m_systems.insert(place, std::move(system));
}

template <typename F>
void Scene::RunSystems(std::string_view hook, std::string_view profileName, F&& fn) {
    for (auto& pending : std::exchange(m_pendingSystems, {})) {
        AttachSystem(std::move(pending));
    }
    SPOT_PROFILE_SCOPE(profileName);
    const bool running = std::exchange(m_runningSystems, true);
    // By index: systems added meanwhile wait in m_pendingSystems, but a nested run would let them in.
    for (std::size_t i = 0; i < m_systems.size(); ++i) {
        SceneSystem& system = *m_systems[i];
        if (!system.m_faulted) {
            SPOT_PROFILE_SCOPE(system.m_profileName);
            system.InvokeHook(hook, [&fn, &system] { fn(system); });
        }
    }
    m_runningSystems = running;
}

void Scene::PrepareToRender() {
    const Detail::DeferRemovalsScope defer(*this);
    RunSystems("OnBeforeRender", "Systems::OnBeforeRender", [](SceneSystem& system) { system.OnBeforeRender(); });
}

Entity& Scene::CreateEntity(std::string name) {
    const auto id = static_cast<EntityId>(s_nextEntityId++);
    // Entity's constructor is private, so make_unique cannot reach it.
    Adopt(std::unique_ptr<Entity>(new Entity(*this, id, std::move(name))));
    return *m_entities.back();
}

Entity& Scene::CreateEntityWithId(EntityId id, std::string name) {
    if (id == EntityId::Invalid || m_entitiesById.contains(id)) {
        return CreateEntity(std::move(name));
    }
    // New ids must stay above every id given out, including restored ones, so none is ever given twice.
    const auto value = static_cast<std::uint64_t>(id);
    std::uint64_t next = s_nextEntityId.load();
    while (next <= value && !s_nextEntityId.compare_exchange_weak(next, value + 1)) {
    }
    Adopt(std::unique_ptr<Entity>(new Entity(*this, id, std::move(name))));
    return *m_entities.back();
}

void Scene::DestroyEntity(Entity& entity) {
    SPOT_ASSERT(entity.m_scene == this, "The entity belongs to another scene");
    if (entity.m_destroyed) {
        return;
    }
    const Detail::DeferRemovalsScope defer(*this);
    entity.MarkDestroyed();
    m_hasPendingRemovals = true;
    entity.RefreshActivity();
}

void Scene::Clear() {
    ForEachEntity([this](Entity& entity) {
        if (!entity.m_parent) {
            DestroyEntity(entity);
        }
    });
}

Entity* Scene::GetEntity(EntityId id) const {
    const auto found = m_entitiesById.find(id);
    return found != m_entitiesById.end() && !found->second->m_destroyed ? found->second : nullptr;
}

Entity* Scene::FindEntity(std::string_view name) const {
    for (const auto& entity : m_entities) {
        if (IsAlive(entity) && entity->m_name == name) {
            return entity.get();
        }
    }
    return nullptr;
}

Entity* Scene::FindEntityWithTag(std::string_view tag) const {
    for (const auto& entity : m_entities) {
        if (IsAlive(entity) && entity->HasTag(tag)) {
            return entity.get();
        }
    }
    return nullptr;
}

std::vector<Entity*> Scene::FindEntitiesWithTag(std::string_view tag) const {
    std::vector<Entity*> found;
    for (const auto& entity : m_entities) {
        if (IsAlive(entity) && entity->HasTag(tag)) {
            found.push_back(entity.get());
        }
    }
    return found;
}

std::vector<Entity*> Scene::GetRootEntities() const {
    std::vector<Entity*> roots;
    for (Entity* entity : m_roots) {
        if (!entity->m_destroyed) {
            roots.push_back(entity);
        }
    }
    return roots;
}

std::size_t Scene::GetEntityCount() const {
    return static_cast<std::size_t>(std::ranges::count_if(m_entities, &Scene::IsAlive));
}

void Scene::Update(float deltaTime) {
    SPOT_ASSERT(!m_deferRemovals, "Update() cannot be called while the scene walks its entities");
    ++m_updateCount;
    if (!std::isfinite(deltaTime) || deltaTime < 0.0f) {
        deltaTime = 0.0f;
    }

    SPOT_PROFILE_SCOPE("Scene::Update");
    const Detail::DeferRemovalsScope defer(*this);
    {
        SPOT_PROFILE_SCOPE("Components::OnStart");
        // By index, because components may create entities, and checking for empty slots left by entities that
        // moved to another scene.
        for (std::size_t i = 0; i < m_entities.size(); ++i) {
            if (m_entities[i]) {
                m_entities[i]->StartComponents();
            }
        }
    }
    RunSystems("OnBeforeFixedSteps", "Systems::OnBeforeFixedSteps",
               [deltaTime](SceneSystem& system) { system.OnBeforeFixedSteps(deltaTime); });

    const double accumulated = static_cast<double>(m_fixedTimeLeft) + deltaTime;
    const double due = std::floor(accumulated / m_fixedTimeStep);
    const int steps = static_cast<int>(std::min(due, static_cast<double>(MaxFixedStepsPerUpdate)));
    const double remainder = accumulated - static_cast<double>(steps) * m_fixedTimeStep;
    m_fixedTimeLeft = static_cast<float>(due > steps ? std::fmod(remainder, m_fixedTimeStep) : remainder);
    // Read once, so a hook that changes it does so from the next update.
    const float fixedTimeStep = m_fixedTimeStep;
    for (int step = 0; step < steps; ++step) {
        SPOT_PROFILE_SCOPE("Scene::FixedStep");
        {
            SPOT_PROFILE_SCOPE("Components::OnFixedUpdate");
            for (std::size_t i = 0; i < m_entities.size(); ++i) {
                if (m_entities[i]) {
                    m_entities[i]->FixedUpdateComponents(fixedTimeStep);
                }
            }
        }
        RunSystems("OnFixedUpdate", "Systems::OnFixedUpdate",
                   [fixedTimeStep](SceneSystem& system) { system.OnFixedUpdate(fixedTimeStep); });
    }
    RunSystems("OnAfterFixedSteps", "Systems::OnAfterFixedSteps",
               [deltaTime](SceneSystem& system) { system.OnAfterFixedSteps(deltaTime); });

    {
        SPOT_PROFILE_SCOPE("Components::Invoke");
        for (std::size_t i = 0; i < m_entities.size(); ++i) {
            if (m_entities[i]) {
                m_entities[i]->UpdateInvokes(deltaTime);
            }
        }
    }

    {
        SPOT_PROFILE_SCOPE("Components::Coroutine");
        for (std::size_t i = 0; i < m_entities.size(); ++i) {
            if (m_entities[i]) {
                m_entities[i]->UpdateCoroutines(deltaTime);
            }
        }
    }
    {
        SPOT_PROFILE_SCOPE("Scene::OnUpdate");
        OnUpdate(deltaTime);
    }
    {
        SPOT_PROFILE_SCOPE("Components::OnUpdate");
        for (std::size_t i = 0; i < m_entities.size(); ++i) {
            if (m_entities[i]) {
                m_entities[i]->UpdateComponents(deltaTime);
            }
        }
    }
    RunSystems("OnUpdate", "Systems::OnUpdate", [deltaTime](SceneSystem& system) { system.OnUpdate(deltaTime); });
    {
        SPOT_PROFILE_SCOPE("Components::OnLateUpdate");
        for (std::size_t i = 0; i < m_entities.size(); ++i) {
            if (m_entities[i]) {
                m_entities[i]->LateUpdateComponents(deltaTime);
            }
        }
    }
    RunSystems("OnLateUpdate", "Systems::OnLateUpdate",
               [deltaTime](SceneSystem& system) { system.OnLateUpdate(deltaTime); });
}

void Scene::DrawUi(Renderer2D& renderer, Vector2 viewSize) {
    SPOT_PROFILE_SCOPE("Scene::DrawUi");
    const Detail::DeferRemovalsScope defer(*this);
    auto& ui = GetUi();
    ui.Layout(viewSize);
    for (std::size_t i = 0; i < m_entities.size(); ++i) {
        if (m_entities[i] && !m_entities[i]->GetComponentInParent<Canvas>()) {
            m_entities[i]->DrawUiComponents(renderer, viewSize);
        }
    }
    for (const auto& entry : ui.GetEntries()) {
        if (auto* entity = GetEntity(entry.entity))
            entity->DrawUiComponents(renderer, viewSize);
    }
    renderer.SetScissor({0, 0, static_cast<std::int32_t>(viewSize.x), static_cast<std::int32_t>(viewSize.y)});
}

UiSystem& Scene::GetUi() {
    auto* ui = GetSystem<UiSystem>();
    return ui ? *ui : AddSystem<UiSystem>();
}
UiInputSystem& Scene::GetUiInput() {
    auto* ui = GetSystem<UiInputSystem>();
    return ui ? *ui : AddSystem<UiInputSystem>();
}

bool Scene::HandleUiEvent(const Event& event, Vector2 viewSize, InputState* input) {
    if (!FindComponent<Canvas>())
        return false;
    auto& ui = GetUiInput();
    ui.SetInputState(input ? input : Input::GetState());
    const bool consumed = ui.OnEvent(event, viewSize);
    if (consumed && input)
        input->ConsumeEvent(event);
    return consumed;
}

Camera2D Scene::ScreenCamera(Vector2 viewSize) {
    return {.position = viewSize * 0.5f};
}

bool Scene::IsAlive(const std::unique_ptr<Entity>& entity) {
    return entity && !entity->m_destroyed;
}

std::unique_ptr<Entity> Scene::Release(Entity& entity) {
    const auto slot = std::ranges::find_if(m_entities, [&entity](const auto& owned) { return owned.get() == &entity; });
    SPOT_ASSERT(slot != m_entities.end(), "The entity belongs to another scene");
    // The slot stays empty until removals are processed, so loops in progress keep their place.
    auto released = std::move(*slot);
    m_entitiesById.erase(entity.m_id);
    std::erase(m_roots, &entity);
    m_hasPendingRemovals = true;
    return released;
}

void Scene::Adopt(std::unique_ptr<Entity> entity) {
    entity->m_scene = this;
    m_entitiesById.emplace(entity->m_id, entity.get());
    if (!entity->m_parent) {
        m_roots.push_back(entity.get());
    }
    m_entities.push_back(std::move(entity));
}

void Scene::ProcessRemovals() {
    if (!m_hasPendingRemovals) {
        return;
    }
    SPOT_PROFILE_SCOPE("Scene::ProcessRemovals");
    const bool deferRemovals = std::exchange(m_deferRemovals, true);
    // OnDestroy() may ask for more removals, which are handled by another round.
    while (m_hasPendingRemovals) {
        m_hasPendingRemovals = false;
        for (std::size_t i = 0; i < m_entities.size(); ++i) {
            if (m_entities[i]) {
                m_entities[i]->ProcessRemovals();
            }
        }

        // Unlinks the entities about to be freed from the ones that stay, before any of them is freed.
        for (const auto& entity : m_entities) {
            if (!entity || !entity->CanBeFreed()) {
                continue;
            }
            if (Entity* parent = entity->m_parent; parent && !parent->CanBeFreed()) {
                std::erase(parent->m_children, entity.get());
            }
            for (Entity* child : entity->m_children) {
                child->m_parent = nullptr;
                child->m_transform.MarkWorldDirty();
            }
            m_entitiesById.erase(entity->m_id);
        }
        std::erase_if(m_roots, [](const Entity* entity) { return entity->CanBeFreed(); });
        std::erase_if(m_entities, [](const auto& entity) { return !entity || entity->CanBeFreed(); });
    }
    m_deferRemovals = deferRemovals;
}

namespace Detail {

DeferRemovalsScope::DeferRemovalsScope(Scene& scene)
    : m_scene(scene), m_outermost(!std::exchange(scene.m_deferRemovals, true)) {
}

DeferRemovalsScope::~DeferRemovalsScope() {
    if (m_outermost) {
        m_scene.m_deferRemovals = false;
        m_scene.ProcessRemovals();
    }
}

} // namespace Detail

} // namespace Spot
