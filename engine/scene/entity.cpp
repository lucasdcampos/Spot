#include "scene/entity.h"

#include "core/assert.h"
#include "physics/physics_world.h"
#include "scene/scene.h"

#include <algorithm>

namespace Spot {

Entity::Entity(Scene& scene, EntityId id, std::string name) : m_scene(&scene), m_id(id), m_name(std::move(name)) {
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

void Entity::SetLayer(int layer) {
    SPOT_ASSERT(layer >= 0 && layer < LayerCount, "Layers go from 0 to Entity::LayerCount - 1");
    if (layer == m_layer) {
        return;
    }
    m_layer = static_cast<std::uint8_t>(layer);
    // Its body, if any, moves to the layer.
    if (PhysicsWorld* physics = m_scene->GetSystem<PhysicsWorld>()) {
        physics->MarkChanged(m_id);
    }
}

void Entity::SetActive(bool active) {
    if (m_activeSelf == active) {
        return;
    }
    m_activeSelf = active;
    const Detail::DeferRemovalsScope defer(*m_scene);
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
        SPOT_ASSERT(parent->m_scene == m_scene, "The parent belongs to another scene");
        SPOT_ASSERT(!parent->m_destroyed, "The parent is destroyed");
        SPOT_ASSERT(parent != this && !parent->IsDescendantOf(*this), "An entity cannot be its own ancestor");
    }
    if (parent == m_parent) {
        return;
    }

    const Vector3 worldPosition = m_transform.GetWorldPosition();
    const Quaternion worldRotation = m_transform.GetWorldRotation();

    std::erase(GetSiblings(), this);
    m_parent = parent;
    GetSiblings().push_back(this);

    m_transform.MarkWorldDirty();
    if (keepWorldPose) {
        m_transform.SetWorldPosition(worldPosition);
        m_transform.SetWorldRotation(worldRotation);
    }

    const Detail::DeferRemovalsScope defer(*m_scene);
    RefreshActivity();
}

Entity& Entity::CreateChild(std::string name) {
    SPOT_ASSERT(!m_destroyed, "A destroyed entity cannot get children");
    Entity& child = m_scene->CreateEntity(std::move(name));
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

std::size_t Entity::GetSiblingIndex() const {
    std::size_t index = 0;
    for (const Entity* sibling : GetSiblings()) {
        if (sibling == this) {
            break;
        }
        index += sibling->m_destroyed ? 0 : 1;
    }
    return index;
}

void Entity::SetSiblingIndex(std::size_t index) {
    SPOT_ASSERT(!m_destroyed, "A destroyed entity cannot be moved in the hierarchy");
    std::vector<Entity*>& siblings = GetSiblings();
    std::erase(siblings, this);
    // Destroyed siblings waiting to be freed still hold slots, so the place is counted among the others.
    auto place = siblings.begin();
    for (std::size_t alive = 0; place != siblings.end(); ++place) {
        if (!(*place)->m_destroyed && alive++ == index) {
            break;
        }
    }
    siblings.insert(place, this);
}

std::vector<Entity*>& Entity::GetSiblings() const {
    return m_parent ? m_parent->m_children : m_scene->m_roots;
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

void Entity::MoveToScene(Scene& scene) {
    SPOT_ASSERT(!m_destroyed, "A destroyed entity cannot move to another scene");
    if (&scene == m_scene) {
        return;
    }
    if (m_parent) {
        SetParent(nullptr, true);
    }
    const Detail::DeferRemovalsScope deferSource(*m_scene);
    const Detail::DeferRemovalsScope deferTarget(scene);
    MoveSubtree(scene);
    // Components already removed but not yet destroyed are now the other scene's to process.
    scene.m_hasPendingRemovals = true;
}

void Entity::MoveSubtree(Scene& scene) {
    // Its body, if any, moves along with it. Without physics in its scene, it has none.
    if (PhysicsWorld* physics = m_scene->GetSystem<PhysicsWorld>()) {
        physics->MarkChanged(m_id);
        scene.GetPhysics().MarkChanged(m_id);
    }
    scene.Adopt(m_scene->Release(*this));
    for (Entity* child : m_children) {
        child->MoveSubtree(scene);
    }
}

void Entity::Destroy() {
    m_scene->DestroyEntity(*this);
}

void Entity::RemoveComponent(Component& component) {
    SPOT_ASSERT(component.m_entity == this, "The component belongs to another entity");
    if (component.m_removed) {
        return;
    }
    const Detail::DeferRemovalsScope defer(*m_scene);
    component.m_removed = true;
    m_scene->m_hasPendingRemovals = true;
    component.RefreshActivity();
}

Component& Entity::AddComponent(std::unique_ptr<Component> component, std::optional<std::size_t> index) {
    SPOT_ASSERT(component && !component->m_entity, "Only components that belong to no entity can be added");
    Component& added = *component;
    Attach(std::move(component), index);
    return added;
}

std::size_t Entity::GetComponentIndex(const Component& component) const {
    SPOT_ASSERT(component.m_entity == this, "The component belongs to another entity");
    std::size_t index = 0;
    for (const auto& owned : m_components) {
        if (owned.get() == &component) {
            break;
        }
        index += owned->m_removed ? 0 : 1;
    }
    return index;
}

void Entity::MoveComponent(Component& component, std::size_t index) {
    SPOT_ASSERT(component.m_entity == this, "The component belongs to another entity");
    const auto slot = std::ranges::find_if(m_components, [&](const auto& owned) { return owned.get() == &component; });
    std::unique_ptr<Component> moved = std::move(*slot);
    m_components.erase(slot);
    Attach(std::move(moved), index);
}

void Entity::Attach(std::unique_ptr<Component> component, std::optional<std::size_t> index) {
    SPOT_ASSERT(!m_destroyed, "Components cannot be added to a destroyed entity");
    component->m_entity = this;
    while (std::ranges::any_of(m_components, [&](const auto& existing) {
        return !existing->m_removed && existing->m_id == component->m_id;
    })) {
        component->m_id = ComponentId::Generate();
    }
    // Removed components waiting to be freed still hold slots, so the place is counted among the others.
    auto place = m_components.end();
    if (index) {
        place = m_components.begin();
        for (std::size_t kept = 0; place != m_components.end(); ++place) {
            if (!(*place)->m_removed && kept++ == *index) {
                break;
            }
        }
    }
    m_components.insert(place, std::move(component));
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
            if (component.InvokeHook("OnStart", [&component] { component.OnStart(); })) {
                component.RefreshActivity();
            }
        }
    }
}

void Entity::FixedUpdateComponents(float fixedDeltaTime) {
    for (std::size_t i = 0; i < m_components.size(); ++i) {
        if (Component& component = *m_components[i]; component.m_active) {
            component.InvokeHook("OnFixedUpdate", [&] { component.OnFixedUpdate(fixedDeltaTime); });
        }
    }
}

void Entity::UpdateComponents(float deltaTime) {
    for (std::size_t i = 0; i < m_components.size(); ++i) {
        if (Component& component = *m_components[i]; component.m_active) {
            component.InvokeHook("OnUpdate", [&] { component.OnUpdate(deltaTime); });
        }
    }
}

void Entity::DrawUiComponents(Renderer2D& renderer, Vector2 viewSize) {
    for (std::size_t i = 0; i < m_components.size(); ++i) {
        if (Component& component = *m_components[i]; component.m_active) {
            component.InvokeHook("OnDrawUi", [&] { component.OnDrawUi(renderer, viewSize); });
        }
    }
}

void Entity::LateUpdateComponents(float deltaTime) {
    for (std::size_t i = 0; i < m_components.size(); ++i) {
        if (Component& component = *m_components[i]; component.m_active) {
            component.InvokeHook("OnLateUpdate", [&] { component.OnLateUpdate(deltaTime); });
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
            component.InvokeHook("OnDestroy", [&component] { component.OnDestroy(); });
        }
    }
    std::erase_if(m_components, [](const auto& component) { return component->m_removed && !component->m_started; });
}

bool Entity::CanBeFreed() const {
    return m_destroyed &&
           std::ranges::none_of(m_components, [](const auto& component) { return component->m_started; });
}

} // namespace Spot
