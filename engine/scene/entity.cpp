#include "scene/entity.h"

#include "core/assert.h"
#include "scene/scene.h"

#include <algorithm>

namespace Spot {

Entity::Entity(Scene& scene, EntityId id, std::string name) : m_scene(scene), m_id(id), m_name(std::move(name)) {
}

void Entity::AddTag(std::string_view tag) {
    if (!HasTag(tag)) {
        m_tags.emplace_back(tag);
    }
}

bool Entity::RemoveTag(std::string_view tag) {
    return std::erase(m_tags, tag) > 0;
}

bool Entity::HasTag(std::string_view tag) const {
    return std::ranges::find(m_tags, tag) != m_tags.end();
}

void Entity::SetActive(bool active) {
    if (m_activeSelf == active) {
        return;
    }
    m_activeSelf = active;
    const Detail::DeferRemovalsScope defer(m_scene);
    RefreshActivity();
}

bool Entity::IsActiveInHierarchy() const {
    for (const Entity* entity = this; entity; entity = entity->m_parent) {
        if (!entity->m_activeSelf) {
            return false;
        }
    }
    return true;
}

void Entity::SetParent(Entity* parent, bool keepWorldPose) {
    SPOT_ASSERT(!m_destroyed, "A destroyed entity cannot be moved in the hierarchy");
    if (parent) {
        SPOT_ASSERT(&parent->m_scene == &m_scene, "The parent belongs to another scene");
        SPOT_ASSERT(!parent->m_destroyed, "The parent is destroyed");
        SPOT_ASSERT(parent != this && !parent->IsDescendantOf(*this), "An entity cannot be its own ancestor");
    }
    if (parent == m_parent) {
        return;
    }

    const Vector3 worldPosition = m_transform.GetWorldPosition();
    const Quaternion worldRotation = m_transform.GetWorldRotation();

    if (m_parent) {
        std::erase(m_parent->m_children, this);
    }
    m_parent = parent;
    if (m_parent) {
        m_parent->m_children.push_back(this);
    }

    m_transform.MarkWorldDirty();
    if (keepWorldPose) {
        m_transform.SetWorldPosition(worldPosition);
        m_transform.SetWorldRotation(worldRotation);
    }

    const Detail::DeferRemovalsScope defer(m_scene);
    RefreshActivity();
}

Entity& Entity::CreateChild(std::string name) {
    SPOT_ASSERT(!m_destroyed, "A destroyed entity cannot get children");
    Entity& child = m_scene.CreateEntity(std::move(name));
    child.SetParent(this);
    return child;
}

std::vector<Entity*> Entity::GetChildren() const {
    std::vector<Entity*> children;
    children.reserve(m_children.size());
    for (Entity* child : m_children) {
        if (!child->m_destroyed) {
            children.push_back(child);
        }
    }
    return children;
}

Entity* Entity::FindChild(std::string_view name) const {
    for (Entity* child : m_children) {
        if (!child->m_destroyed && child->m_name == name) {
            return child;
        }
    }
    return nullptr;
}

bool Entity::IsDescendantOf(const Entity& ancestor) const {
    for (const Entity* entity = m_parent; entity; entity = entity->m_parent) {
        if (entity == &ancestor) {
            return true;
        }
    }
    return false;
}

void Entity::Destroy() {
    m_scene.DestroyEntity(*this);
}

void Entity::RemoveComponent(Component& component) {
    SPOT_ASSERT(component.m_entity == this, "The component belongs to another entity");
    if (component.m_removed) {
        return;
    }
    const Detail::DeferRemovalsScope defer(m_scene);
    component.m_removed = true;
    m_scene.m_hasPendingRemovals = true;
    component.RefreshActivity();
}

void Entity::Attach(std::unique_ptr<Component> component) {
    SPOT_ASSERT(!m_destroyed, "Components cannot be added to a destroyed entity");
    component->m_entity = this;
    m_components.push_back(std::move(component));
}

void Entity::MarkDestroyed() {
    m_destroyed = true;
    for (Entity* child : m_children) {
        child->MarkDestroyed();
    }
}

void Entity::RefreshActivity() {
    // By index here and below, because hooks may add components and move entities in the hierarchy.
    for (std::size_t i = 0; i < m_components.size(); ++i) {
        m_components[i]->RefreshActivity();
    }
    for (std::size_t i = 0; i < m_children.size(); ++i) {
        m_children[i]->RefreshActivity();
    }
}

void Entity::StartComponents() {
    for (std::size_t i = 0; i < m_components.size() && !m_destroyed && IsActiveInHierarchy(); ++i) {
        Component& component = *m_components[i];
        if (!component.m_started && component.m_enabled && !component.m_removed) {
            component.m_started = true;
            component.OnStart();
            component.RefreshActivity();
        }
    }
}

void Entity::UpdateComponents(float deltaTime) {
    for (std::size_t i = 0; i < m_components.size(); ++i) {
        if (Component& component = *m_components[i]; component.m_active) {
            component.OnUpdate(deltaTime);
        }
    }
}

void Entity::LateUpdateComponents(float deltaTime) {
    for (std::size_t i = 0; i < m_components.size(); ++i) {
        if (Component& component = *m_components[i]; component.m_active) {
            component.OnLateUpdate(deltaTime);
        }
    }
}

void Entity::ProcessRemovals() {
    // In reverse order of addition, like destructors, so later components can still use earlier ones.
    for (std::size_t i = m_components.size(); i-- > 0;) {
        Component& component = *m_components[i];
        if ((m_destroyed || component.m_removed) && component.m_started) {
            component.RefreshActivity();
            component.m_started = false;
            component.OnDestroy();
        }
    }
    std::erase_if(m_components, [](const auto& component) { return component->m_removed && !component->m_started; });
}

bool Entity::CanBeFreed() const {
    return m_destroyed &&
           std::ranges::none_of(m_components, [](const auto& component) { return component->m_started; });
}

} // namespace Spot
