#pragma once

#include "gameplay/persistence/world_save.h"

#include <spot.h>

#include <cstddef>
#include <string>
#include <vector>

// What the player made and took away, so Undo takes back the last of it: props spawned, joints the tool gun added,
// props it removed, which come back as they were, with their ids, so joints to them work again, and materials it
// changed. Loading a save clears it, as what it would take back is gone.
class UndoList final : public Spot::Component, public Gameplay::SaveListener {
public:
    void PushSpawn(Spot::EntityId entity);
    // A joint added as the component at that place on the entity.
    void PushJoint(Spot::EntityId entity, std::size_t componentIndex);
    // An entity about to be removed, saved first.
    void PushRemoval(const Spot::Entity& entity);
    // The material an entity's MeshRenderer had before it was changed.
    void PushMaterial(Spot::EntityId entity, Spot::AssetRef<Spot::Material> previous);

    // Takes back the last thing that can still be taken back; false when there is nothing.
    bool Undo();
    std::size_t GetCount() const { return m_actions.size(); }
    void Clear() { m_actions.clear(); }

protected:
    void OnBeforeLoad() override { Clear(); }
    void OnUpdate(float deltaTime) override;

private:
    struct Action {
        enum class Kind { Spawn, Joint, Removal, Material } kind;
        Spot::EntityId entity = Spot::EntityId::Invalid;
        std::size_t componentIndex = 0;
        std::string saved = {};
        Spot::AssetRef<Spot::Material> material = {};
    };

    bool UndoAction(const Action& action);

    std::vector<Action> m_actions;
};
