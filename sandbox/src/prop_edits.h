#pragma once

#include <spot.h>

#include <vector>

// What the tool gun changes on props, which the props do not replicate themselves: their paint and materials, their
// welds and ropes. In a session the server lists the props changed, with what they look like and how they are joined
// now, and the other machines make theirs the same, undone changes included. On the mode, with the sandbox's rules.
class PropEdits final : public Spot::Component, public Spot::Replicated {
public:
    // On the server: the prop was changed, so the others are shown it. Nothing without a list in the scene.
    static void Mark(Spot::Entity& prop);

    void Replicate(Spot::Archive& archive) override;

private:
    struct Joined {
        // A rope, or else a weld.
        bool rope = false;
        Spot::EntityId connected = Spot::EntityId::Invalid;
        Spot::Vector3 anchor;
        Spot::Vector3 connectedAnchor;
        float minDistance = 0.0f;
        float maxDistance = 0.0f;

        void Serialize(Spot::Archive& archive);
        friend bool operator==(const Joined&, const Joined&) = default;
    };
    struct Edit {
        Spot::EntityId prop = Spot::EntityId::Invalid;
        bool painted = false;
        Spot::Material material;
        std::vector<Joined> joints;

        void Serialize(Spot::Archive& archive);
    };

    // What the prop is now.
    static Edit Describe(Spot::Entity& prop);
    // Makes the prop as the edit says.
    void Apply(const Edit& edit);

    std::vector<Spot::EntityId> m_props;
};
