#include "spot/scene/entity.h"
#include "spot/scene/ui/ui_widgets.h"

#include "spot/core/debug/assert.h"
#include "spot/core/debug/diagnostics.h"
#include "spot/core/debug/log.h"
#include "spot/scene/component_registry.h"
#include "spot/scene/physics/character_controller.h"
#include "spot/scene/physics/collider.h"
#include "spot/scene/physics/physics_world.h"
#include "spot/scene/physics/rigidbody.h"
#include "spot/scene/scene.h"

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
    m_scene->MarkStructureChanged();
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
    m_scene->MarkStructureChanged();
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
    m_scene->MarkStructureChanged();
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
}

void Entity::MoveSubtree(Scene& scene) {
    // Its body, if any, moves along with it. Without physics in its scene, it has none.
    if (PhysicsWorld* physics = m_scene->GetSystem<PhysicsWorld>()) {
        physics->MarkChanged(m_id);
        scene.GetPhysics().MarkChanged(m_id);
    }
    // Components already removed but not yet destroyed are now the other scene's to process.
    scene.Adopt(m_scene->Release(*this), true);
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
    if (const Component* dependent = FindDependent(component)) {
        const auto name = [](const Component& of) {
            std::string registered = GetComponentTypeName(of);
            return registered.empty() ? std::string(Detail::GetTypeName(typeid(of))) : registered;
        };
        Log::GetEngineLogger().Error("{} on '{}' is required by {} and stays", name(component), m_name,
                                     name(*dependent));
        return;
    }
    const Detail::DeferRemovalsScope defer(*m_scene);
    m_scene->MarkStructureChanged();
    component.m_removed = true;
    m_scene->AddRemovalCandidate(*this);
    // Validation and queries can create a body before OnStart(), so removal cannot rely only on OnDisable().
    if (dynamic_cast<Collider*>(&component) || dynamic_cast<Rigidbody*>(&component) ||
        dynamic_cast<CharacterController*>(&component)) {
        if (auto* physics = m_scene->GetSystem<PhysicsWorld>()) {
            physics->MarkChanged(m_id);
        }
    }
    component.RefreshActivity();
}

Component& Entity::AddComponent(std::unique_ptr<Component> component, std::optional<std::size_t> index) {
    SPOT_ASSERT(component && !component->m_entity, "Only components that belong to no entity can be added");
    AddRequiredComponents(Detail::GetComponentTypeIndex(typeid(*component)));
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
    component->m_typeIndex = Detail::GetComponentTypeIndex(typeid(*component));
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
    Component& added = *component;
    m_components.insert(place, std::move(component));
    m_scene->ListComponent(added);
}

void Entity::AddRequiredComponents(Detail::ComponentTypeIndex type) {
    const auto* requirements = Detail::FindComponentRequirements(type);
    if (!requirements) {
        return;
    }
    for (const Detail::ComponentRequirement& requirement : *requirements) {
        if (!requirement.isPresent(*this)) {
            // Through the public path, so what the required type itself requires comes first.
            AddComponent(requirement.create());
        }
    }
}

const Component* Entity::FindDependent(const Component& required) const {
    for (const auto& component : m_components) {
        if (component.get() == &required || component->m_removed) {
            continue;
        }
        const auto* requirements = Detail::FindComponentRequirements(component->m_typeIndex);
        if (!requirements) {
            continue;
        }
        for (const Detail::ComponentRequirement& requirement : *requirements) {
            if (!requirement.matches(required)) {
                continue;
            }
            // Another component of the required type can stand in for it.
            const bool replaced = std::ranges::any_of(m_components, [&](const auto& other) {
                return other.get() != &required && !other->m_removed && requirement.matches(*other);
            });
            if (!replaced) {
                return component.get();
            }
        }
    }
    return nullptr;
}

void Entity::MarkDestroyed() {
    if (!m_destroyed) {
        --m_scene->m_liveCount;
    }
    m_destroyed = true;
    m_scene->AddRemovalCandidate(*this);
    if (auto* physics = m_scene->GetSystem<PhysicsWorld>()) {
        physics->MarkChanged(m_id);
    }
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

void Entity::DrawUiComponents(Renderer2D& renderer, Vector2 viewSize) {
    for (std::size_t i = 0; i < m_components.size(); ++i) {
        DrawUiComponent(*m_components[i], renderer, viewSize);
    }
}

void Entity::DrawUiComponent(Component& component, Renderer2D& renderer, Vector2 viewSize) {
    // Widgets draw before they start, so an interface shows from its first frame.
    if (component.m_active || (Detail::IsComponentOf<UiWidget>(component) && component.IsActiveAndEnabled())) {
        component.InvokeHook("OnDrawUi", [&] { component.OnDrawUi(renderer, viewSize); });
    }
}

void Entity::ProcessRemovals() {
    // In reverse order of addition, like destructors, so later components can still use earlier ones.
    for (std::size_t i = m_components.size(); i-- > 0;) {
        Component& component = *m_components[i];
        if ((m_destroyed || component.m_removed) && component.m_started) {
            component.CancelInvokes();
            component.CancelCoroutines();
            component.RefreshActivity();
            component.m_started = false;
            component.InvokeHook("OnDestroy", [&component] { component.OnDestroy(); });
        }
    }
    std::erase_if(m_components, [this](const auto& component) {
        if (component->m_removed && !component->m_started) {
            m_scene->UnlistComponent(*component);
            return true;
        }
        return false;
    });
}

bool Entity::CanBeFreed() const {
    return m_destroyed &&
           std::ranges::none_of(m_components, [](const auto& component) { return component->m_started; });
}

} // namespace Spot
