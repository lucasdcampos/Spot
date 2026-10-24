#include "spot/scene/scene.h"
#include "spot/scene/prefab.h"

#include "spot/core/debug/assert.h"
#include "spot/core/debug/diagnostics.h"
#include "spot/core/debug/log.h"
#include "spot/core/debug/profiler.h"

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
    m_systemsByType[std::type_index(type)] = system.get();
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

template <typename F>
void Scene::NotifySystems(std::string_view hook, F&& fn) {
    const Detail::DeferRemovalsScope defer(*this);
    const bool running = std::exchange(m_runningSystems, true);
    // New systems wait until the next notification. Indices survive callbacks that grow the pending list.
    const std::size_t systemCount = m_systems.size();
    const std::size_t pendingCount = m_pendingSystems.size();
    const auto notify = [&](SceneSystem& system) {
        if (!system.m_faulted) {
            system.InvokeHook(hook, [&] { fn(system); });
        }
    };
    for (std::size_t i = 0; i < systemCount; ++i) {
        notify(*m_systems[i]);
    }
    for (std::size_t i = 0; i < pendingCount; ++i) {
        notify(*m_pendingSystems[i]);
    }
    m_runningSystems = running;
}

void Scene::NotifyComponentAdded(Component& component) {
    NotifySystems("OnComponentAdded", [&](SceneSystem& system) { system.OnComponentAdded(component); });
}

void Scene::NotifyComponentRemoved(Component& component) {
    NotifySystems("OnComponentRemoved", [&](SceneSystem& system) { system.OnComponentRemoved(component); });
}

void Scene::NotifyEntityLayerChanged(Entity& entity) {
    NotifySystems("OnEntityLayerChanged", [&](SceneSystem& system) { system.OnEntityLayerChanged(entity); });
}

void Scene::NotifyEntityDestroyed(Entity& entity) {
    NotifySystems("OnEntityDestroyed", [&](SceneSystem& system) { system.OnEntityDestroyed(entity); });
}

void Scene::NotifyEntityMoved(Entity& entity, Scene& source, Scene& destination) {
    NotifySystems("OnEntityMoved", [&](SceneSystem& system) { system.OnEntityMoved(entity, source, destination); });
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

void Scene::TrackChanges() {
    if (!m_trackingChanges) {
        m_trackingChanges = true;
        m_changesOverflowed = true;
    }
}

bool Scene::TakeChangedEntities(std::vector<EntityId>& entities) {
    const bool complete = !m_changesOverflowed;
    entities.insert(entities.end(), m_changedEntities.begin(), m_changedEntities.end());
    m_changedEntities.clear();
    m_changesOverflowed = false;
    return complete;
}

void Scene::MarkEntityChanged(const Entity& entity) {
    if (!m_trackingChanges || m_changesOverflowed) {
        return;
    }
    // Past as many as there are entities, going through them all is no slower, and the record stays bounded when
    // nothing takes it.
    if (m_changedEntities.size() >= std::max<std::size_t>(m_entities.size(), 1024)) {
        m_changesOverflowed = true;
        m_changedEntities.clear();
        return;
    }
    m_changedEntities.push_back(entity.GetId());
}

void Scene::DestroyEntity(Entity& entity) {
    SPOT_ASSERT(entity.m_scene == this, "The entity belongs to another scene");
    if (entity.m_destroyed) {
        return;
    }
    const Detail::DeferRemovalsScope defer(*this);
    MarkStructureChanged();
    entity.MarkDestroyed();
    entity.RefreshActivity();
}

void Scene::Clear() {
    CancelChunkLoads();
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
    return m_liveCount;
}

void Scene::Update(float deltaTime) {
    SPOT_ASSERT(!m_deferRemovals, "Update() cannot be called while the scene walks its entities");
    if (m_paused && !std::exchange(m_stepPending, false)) {
        return;
    }
    FinishChunkLoads();
    ++m_updateCount;
    if (!std::isfinite(deltaTime) || deltaTime < 0.0f) {
        deltaTime = 0.0f;
    }

    SPOT_PROFILE_SCOPE("Scene::Update");
    const Detail::DeferRemovalsScope defer(*this);
    StartComponents();
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
        RunHook(HookList::FixedUpdate, Detail::ComponentHook::FixedUpdate, "Components::OnFixedUpdate",
                [fixedTimeStep](Component& component) { component.OnFixedUpdate(fixedTimeStep); });
        RunSystems("OnFixedUpdate", "Systems::OnFixedUpdate",
                   [fixedTimeStep](SceneSystem& system) { system.OnFixedUpdate(fixedTimeStep); });
    }
    RunSystems("OnAfterFixedSteps", "Systems::OnAfterFixedSteps",
               [deltaTime](SceneSystem& system) { system.OnAfterFixedSteps(deltaTime); });

    {
        SPOT_PROFILE_SCOPE("Components::Invoke");
        const ComponentList& scheduled = m_hookLists[static_cast<std::size_t>(HookList::Scheduled)];
        for (std::size_t i = 0, end = scheduled.components.size(); i < end; ++i) {
            if (Component* component = scheduled.components[i]) {
                component->UpdateInvokes(deltaTime);
            }
        }
    }
    {
        SPOT_PROFILE_SCOPE("Components::Coroutine");
        const ComponentList& scheduled = m_hookLists[static_cast<std::size_t>(HookList::Scheduled)];
        for (std::size_t i = 0, end = scheduled.components.size(); i < end; ++i) {
            if (Component* component = scheduled.components[i]) {
                component->UpdateCoroutines(deltaTime);
            }
        }
    }
    {
        SPOT_PROFILE_SCOPE("Scene::OnUpdate");
        OnUpdate(deltaTime);
    }
    RunHook(HookList::Update, Detail::ComponentHook::Update, "Components::OnUpdate",
            [deltaTime](Component& component) { component.OnUpdate(deltaTime); });
    RunSystems("OnUpdate", "Systems::OnUpdate", [deltaTime](SceneSystem& system) { system.OnUpdate(deltaTime); });
    RunHook(HookList::LateUpdate, Detail::ComponentHook::LateUpdate, "Components::OnLateUpdate",
            [deltaTime](Component& component) { component.OnLateUpdate(deltaTime); });
    RunSystems("OnLateUpdate", "Systems::OnLateUpdate",
               [deltaTime](SceneSystem& system) { system.OnLateUpdate(deltaTime); });
}

bool Scene::IsAlive(const std::unique_ptr<Entity>& entity) {
    return entity && !entity->m_destroyed;
}

void Scene::AddToList(HookList list, Component& component) {
    std::size_t& slot = component.m_hookSlots[static_cast<std::size_t>(list)];
    if (slot != Component::NotInList) {
        return;
    }
    ComponentList& components = m_hookLists[static_cast<std::size_t>(list)];
    slot = components.components.size();
    components.components.push_back(&component);
}

void Scene::RemoveFromList(HookList list, Component& component) {
    std::size_t& slot = component.m_hookSlots[static_cast<std::size_t>(list)];
    if (slot == Component::NotInList) {
        return;
    }
    ComponentList& components = m_hookLists[static_cast<std::size_t>(list)];
    components.components[slot] = nullptr;
    ++components.emptySlots;
    slot = Component::NotInList;
}

void Scene::CompactHookLists() {
    for (std::size_t list = 0; list < m_hookLists.size(); ++list) {
        ComponentList& components = m_hookLists[list];
        if (components.emptySlots > 64 && components.emptySlots * 2 > components.components.size()) {
            std::erase(components.components, nullptr);
            for (std::size_t i = 0; i < components.components.size(); ++i) {
                components.components[i]->m_hookSlots[list] = i;
            }
            components.emptySlots = 0;
        }
    }
}

void Scene::StartComponents() {
    SPOT_PROFILE_SCOPE("Components::OnStart");
    ComponentList& pending = m_hookLists[static_cast<std::size_t>(HookList::Start)];
    // By index up to the current end, so components added by OnStart() start in the same update.
    for (std::size_t i = 0; i < pending.components.size(); ++i) {
        Component* component = pending.components[i];
        if (!component) {
            continue;
        }
        Entity& entity = *component->m_entity;
        if (component->m_started || component->m_removed || entity.m_destroyed) {
            RemoveFromList(HookList::Start, *component);
            continue;
        }
        // Disabled components and inactive entities stay in the list, and start once they are enabled and active.
        if (!component->m_enabled || !entity.IsActiveInHierarchy()) {
            continue;
        }
        RemoveFromList(HookList::Start, *component);
        component->m_started = true;
        if (component->InvokeHook("OnStart", [component] { component->OnStart(); })) {
            component->RefreshActivity();
        }
    }
}

template <typename F>
void Scene::RunHook(HookList list, Detail::ComponentHook hook, std::string_view name, F&& call) {
    SPOT_PROFILE_SCOPE(name);
    ComponentList& components = m_hookLists[static_cast<std::size_t>(list)];
    // Components added meanwhile wait for the next update.
    for (std::size_t i = 0, end = components.components.size(); i < end; ++i) {
        Component* component = components.components[i];
        if (!component) {
            continue;
        }
        if (Detail::IsComponentHookUnused(component->m_typeIndex, hook)) {
            RemoveFromList(list, *component);
            continue;
        }
        if (component->m_active) {
            component->InvokeHook(name.substr(name.find("::") + 2), [&call, component] { call(*component); });
        }
    }
}

void Scene::ListComponent(Component& component) {
    if (component.m_listed) {
        return;
    }
    MarkStructureChanged();
    if (component.m_typeIndex >= m_componentsByType.size()) {
        m_componentsByType.resize(component.m_typeIndex + 1);
    }
    std::vector<Component*>& list = m_componentsByType[component.m_typeIndex].components;
    component.m_typeSlot = list.size();
    component.m_sequence = m_nextComponentSequence++;
    component.m_listed = true;
    list.push_back(&component);
    if (!component.m_started) {
        AddToList(HookList::Start, component);
    }
    const std::pair<HookList, Detail::ComponentHook> hooks[] = {
        {HookList::FixedUpdate, Detail::ComponentHook::FixedUpdate},
        {HookList::Update, Detail::ComponentHook::Update},
        {HookList::LateUpdate, Detail::ComponentHook::LateUpdate}};
    for (const auto& [hookList, hook] : hooks) {
        if (!Detail::IsComponentHookUnused(component.m_typeIndex, hook)) {
            AddToList(hookList, component);
        }
    }
    if (component.m_timers || component.m_tasks) {
        AddToList(HookList::Scheduled, component);
    }
}

void Scene::UnlistComponent(Component& component) {
    if (!component.m_listed) {
        return;
    }
    MarkStructureChanged();
    TypeList& list = m_componentsByType[component.m_typeIndex];
    list.components[component.m_typeSlot] = nullptr;
    ++list.emptySlots;
    component.m_listed = false;
    for (std::size_t hookList = 0; hookList < m_hookLists.size(); ++hookList) {
        RemoveFromList(static_cast<HookList>(hookList), component);
    }
}

void Scene::CompactComponentLists() {
    for (TypeList& list : m_componentsByType) {
        if (list.emptySlots > 32 && list.emptySlots * 2 > list.components.size()) {
            std::erase(list.components, nullptr);
            for (std::size_t i = 0; i < list.components.size(); ++i) {
                list.components[i]->m_typeSlot = i;
            }
            list.emptySlots = 0;
        }
    }
}

bool Scene::IsIncluded(const Component& component) {
    const Entity* entity = component.m_entity;
    return !component.m_removed && component.IsEnabled() && entity && !entity->m_destroyed &&
           entity->IsActiveInHierarchy();
}

std::unique_ptr<Entity> Scene::Release(Entity& entity) {
    SPOT_ASSERT(entity.m_slot < m_entities.size() && m_entities[entity.m_slot].get() == &entity,
                "The entity belongs to another scene");
    // The slot stays empty until it is compacted, so loops in progress keep their place.
    auto released = std::move(m_entities[entity.m_slot]);
    ++m_emptySlots;
    if (!entity.m_destroyed) {
        --m_liveCount;
    }
    m_entitiesById.erase(entity.m_id);
    if (!entity.m_parent) {
        RemoveRoot(entity);
    }
    std::erase(m_removalCandidates, &entity);
    for (const auto& component : entity.m_components) {
        UnlistComponent(*component);
    }
    return released;
}

void Scene::Adopt(std::unique_ptr<Entity> entity, bool moved) {
    entity->m_scene = this;
    entity->m_slot = m_entities.size();
    m_entitiesById.emplace(entity->m_id, entity.get());
    if (!entity->m_parent) {
        m_roots.push_back(entity.get());
    }
    if (!entity->m_destroyed) {
        ++m_liveCount;
    }
    if (moved) {
        AddRemovalCandidate(*entity);
    }
    for (const auto& component : entity->m_components) {
        ListComponent(*component);
    }
    m_entities.push_back(std::move(entity));
}

void Scene::AddRemovalCandidate(Entity& entity) {
    m_removalCandidates.push_back(&entity);
    m_hasPendingRemovals = true;
}

void Scene::RemoveRoot(const Entity& entity) {
    const auto found = std::find(m_roots.rbegin(), m_roots.rend(), &entity);
    if (found != m_roots.rend()) {
        m_roots.erase(std::next(found).base());
    }
}

void Scene::CompactSlots() {
    std::erase_if(m_entities, [](const auto& entity) { return !entity; });
    for (std::size_t i = 0; i < m_entities.size(); ++i) {
        m_entities[i]->m_slot = i;
    }
    m_emptySlots = 0;
}

void Scene::ProcessRemovals() {
    if (!m_hasPendingRemovals) {
        return;
    }
    SPOT_PROFILE_SCOPE("Scene::ProcessRemovals");
    const bool deferRemovals = std::exchange(m_deferRemovals, true);
    // OnDestroy() may ask for more removals, which are handled by another round.
    std::vector<Entity*> freed;
    while (m_hasPendingRemovals) {
        m_hasPendingRemovals = false;
        std::vector<Entity*> candidates = std::exchange(m_removalCandidates, {});
        std::ranges::sort(candidates);
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
        // In creation order, as components of every entity were before.
        std::ranges::sort(candidates, {}, &Entity::m_slot);
        for (Entity* entity : candidates) {
            entity->ProcessRemovals();
        }

        // Unlinks the entities about to be freed from the ones that stay, before any of them is freed.
        freed.clear();
        for (Entity* entity : candidates) {
            if (entity->m_scene == this && entity->CanBeFreed()) {
                freed.push_back(entity);
            }
        }
        // Roots are told apart before unlinking, which leaves freed children without a parent too.
        for (const Entity* entity : freed) {
            if (!entity->m_parent) {
                RemoveRoot(*entity);
            }
        }
        for (Entity* entity : freed) {
            if (Entity* parent = entity->m_parent; parent && !parent->CanBeFreed()) {
                std::erase(parent->m_children, entity);
            }
            for (Entity* child : entity->m_children) {
                child->m_parent = nullptr;
                child->m_transform.MarkWorldDirty();
            }
            m_entitiesById.erase(entity->m_id);
        }
        for (Entity* entity : freed) {
            for (const auto& component : entity->m_components) {
                UnlistComponent(*component);
            }
            m_entities[entity->m_slot].reset();
            ++m_emptySlots;
        }
    }
    CompactComponentLists();
    CompactHookLists();
    if (m_emptySlots > 64 && m_emptySlots * 2 > m_entities.size()) {
        CompactSlots();
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
