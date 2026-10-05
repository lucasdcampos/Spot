#include "scene/scene.h"

#include "core/assert.h"

#include <algorithm>
#include <utility>

namespace Spot {

Scene::~Scene() {
    Clear();
}

Application& Scene::GetApplication() const {
    SPOT_ASSERT(m_application, "The application is only available while the scene is loaded");
    return *m_application;
}

Entity& Scene::CreateEntity(std::string name) {
    const auto id = static_cast<EntityId>(m_nextId++);
    // Entity's constructor is private, so make_unique cannot reach it.
    m_entities.push_back(std::unique_ptr<Entity>(new Entity(*this, id, std::move(name))));
    Entity& entity = *m_entities.back();
    m_entitiesById.emplace(id, &entity);
    return entity;
}

void Scene::DestroyEntity(Entity& entity) {
    SPOT_ASSERT(&entity.m_scene == this, "The entity belongs to another scene");
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
        if (!entity->m_destroyed && entity->m_name == name) {
            return entity.get();
        }
    }
    return nullptr;
}

Entity* Scene::FindEntityWithTag(std::string_view tag) const {
    for (const auto& entity : m_entities) {
        if (!entity->m_destroyed && entity->HasTag(tag)) {
            return entity.get();
        }
    }
    return nullptr;
}

std::vector<Entity*> Scene::FindEntitiesWithTag(std::string_view tag) const {
    std::vector<Entity*> found;
    for (const auto& entity : m_entities) {
        if (!entity->m_destroyed && entity->HasTag(tag)) {
            found.push_back(entity.get());
        }
    }
    return found;
}

std::vector<Entity*> Scene::GetRootEntities() const {
    std::vector<Entity*> roots;
    for (const auto& entity : m_entities) {
        if (!entity->m_destroyed && !entity->m_parent) {
            roots.push_back(entity.get());
        }
    }
    return roots;
}

std::size_t Scene::GetEntityCount() const {
    return static_cast<std::size_t>(
        std::ranges::count_if(m_entities, [](const auto& entity) { return !entity->m_destroyed; }));
}

void Scene::Update(float deltaTime) {
    SPOT_ASSERT(!m_deferRemovals, "Update() cannot be called while the scene walks its entities");

    const Detail::DeferRemovalsScope defer(*this);
    // By index, because components may create entities.
    for (std::size_t i = 0; i < m_entities.size(); ++i) {
        m_entities[i]->StartComponents();
    }
    OnUpdate(deltaTime);
    for (std::size_t i = 0; i < m_entities.size(); ++i) {
        m_entities[i]->UpdateComponents(deltaTime);
    }
    for (std::size_t i = 0; i < m_entities.size(); ++i) {
        m_entities[i]->LateUpdateComponents(deltaTime);
    }
}

void Scene::ProcessRemovals() {
    const bool deferRemovals = std::exchange(m_deferRemovals, true);
    // OnDestroy() may ask for more removals, which are handled by another round.
    while (m_hasPendingRemovals) {
        m_hasPendingRemovals = false;
        for (std::size_t i = 0; i < m_entities.size(); ++i) {
            m_entities[i]->ProcessRemovals();
        }

        // Unlinks the entities about to be freed from the ones that stay, before any of them is freed.
        for (const auto& entity : m_entities) {
            if (!entity->CanBeFreed()) {
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
        std::erase_if(m_entities, [](const auto& entity) { return entity->CanBeFreed(); });
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
