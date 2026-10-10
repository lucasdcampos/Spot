#pragma once

#include <spot.h>

#include <cstddef>
#include <string>
#include <vector>

// What the player made and took away, so Undo takes back the last of it: props spawned, joints the tool gun added and
// props it removed, which come back as they were, with their ids, so joints to them work again.
class UndoList final : public Spot::Component {
public:
    void PushSpawn(Spot::EntityId entity);
    // A joint added as the component at that place on the entity.
    void PushJoint(Spot::EntityId entity, std::size_t componentIndex);
    // An entity about to be removed, saved first.
    void PushRemoval(const Spot::Entity& entity);

    // Takes back the last thing that can still be taken back; false when there is nothing.
    bool Undo();
    std::size_t GetCount() const { return m_actions.size(); }

protected:
    void OnUpdate(float deltaTime) override;

private:
    struct Action {
        enum class Kind { Spawn, Joint, Removal } kind;
        Spot::EntityId entity = Spot::EntityId::Invalid;
        std::size_t componentIndex = 0;
        std::string saved = {};
    };

    bool UndoAction(const Action& action);

    std::vector<Action> m_actions;
};
