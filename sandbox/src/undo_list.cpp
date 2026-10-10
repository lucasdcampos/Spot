#include "undo_list.h"

SPOT_COMPONENT(UndoList);

void UndoList::PushSpawn(Spot::EntityId entity) {
    m_actions.push_back({.kind = Action::Kind::Spawn, .entity = entity});
}

void UndoList::PushJoint(Spot::EntityId entity, std::size_t componentIndex) {
    m_actions.push_back({.kind = Action::Kind::Joint, .entity = entity, .componentIndex = componentIndex});
}

void UndoList::PushRemoval(const Spot::Entity& entity) {
    m_actions.push_back({.kind = Action::Kind::Removal, .entity = entity.GetId(), .saved = entity.SaveToString()});
}

void UndoList::PushMaterial(Spot::EntityId entity, Spot::AssetRef<Spot::Material> previous) {
    m_actions.push_back({.kind = Action::Kind::Material, .entity = entity, .material = std::move(previous)});
}

bool UndoList::Undo() {
    // What was destroyed or changed meanwhile by other means cannot be taken back, and is skipped.
    while (!m_actions.empty()) {
        const Action action = std::move(m_actions.back());
        m_actions.pop_back();
        if (UndoAction(action)) {
            return true;
        }
    }
    return false;
}

bool UndoList::UndoAction(const Action& action) {
    Spot::Scene& scene = GetScene();
    switch (action.kind) {
    case Action::Kind::Spawn:
        if (Spot::Entity* entity = scene.GetEntity(action.entity)) {
            entity->Destroy();
            return true;
        }
        return false;
    case Action::Kind::Joint: {
        Spot::Entity* entity = scene.GetEntity(action.entity);
        const std::vector<Spot::Component*> components =
            entity ? entity->GetComponents<Spot::Component>() : std::vector<Spot::Component*>{};
        if (action.componentIndex < components.size()) {
            if (auto* joint = dynamic_cast<Spot::Joint*>(components[action.componentIndex])) {
                entity->RemoveComponent(*joint);
                return true;
            }
        }
        return false;
    }
    case Action::Kind::Removal:
        // With its id, and its references to the others as they were, since it comes back to the same scene.
        return !scene.Instantiate(action.saved, {.keepIds = true, .keepExternalReferences = true}).empty();
    case Action::Kind::Material: {
        Spot::Entity* entity = scene.GetEntity(action.entity);
        auto* renderer = entity ? entity->GetComponent<Spot::MeshRenderer>() : nullptr;
        if (renderer) {
            renderer->SetMaterial(action.material);
        }
        return renderer != nullptr;
    }
    }
    return false;
}

void UndoList::OnUpdate(float /*deltaTime*/) {
    if (Spot::Input::IsActionPressed("Undo")) {
        Undo();
    }
}
