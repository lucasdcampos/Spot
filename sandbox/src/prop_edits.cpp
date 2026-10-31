#include "prop_edits.h"

#include <algorithm>

SPOT_COMPONENT(PropEdits);

void PropEdits::Mark(Spot::Entity& prop) {
    PropEdits* edits = prop.GetScene().FindComponent<PropEdits>();
    if (!edits || !Spot::Network::IsServer(prop.GetScene()) ||
        std::ranges::find(edits->m_props, prop.GetId()) != edits->m_props.end()) {
        return;
    }
    edits->m_props.push_back(prop.GetId());
}

void PropEdits::Joined::Serialize(Spot::Archive& archive) {
    archive.Field("rope", rope);
    archive.Field("connected", connected);
    archive.Field("anchor", anchor);
    archive.Field("connectedAnchor", connectedAnchor);
    archive.Field("minDistance", minDistance);
    archive.Field("maxDistance", maxDistance);
}

void PropEdits::Edit::Serialize(Spot::Archive& archive) {
    archive.Field("prop", prop);
    archive.Field("painted", painted);
    archive.Field("material", material);
    archive.Field("joints", joints);
}

PropEdits::Edit PropEdits::Describe(Spot::Entity& prop) {
    Edit edit{.prop = prop.GetId()};
    if (const auto* renderer = prop.GetComponent<Spot::MeshRenderer>(); renderer && renderer->GetMaterial()) {
        edit.painted = true;
        edit.material = *renderer->GetMaterial();
    }
    for (const Spot::FixedJoint* joint : prop.GetComponents<Spot::FixedJoint>()) {
        edit.joints.push_back({.connected = joint->GetConnectedEntity(), .anchor = joint->GetAnchor()});
    }
    for (const Spot::DistanceJoint* joint : prop.GetComponents<Spot::DistanceJoint>()) {
        edit.joints.push_back({.rope = true,
                               .connected = joint->GetConnectedEntity(),
                               .anchor = joint->GetAnchor(),
                               .connectedAnchor = joint->GetConnectedAnchor(),
                               .minDistance = joint->GetMinDistance(),
                               .maxDistance = joint->GetMaxDistance()});
    }
    return edit;
}

void PropEdits::Apply(const Edit& edit) {
    Spot::Entity* prop = GetScene().GetEntity(edit.prop);
    if (!prop) {
        return;
    }
    if (auto* renderer = prop->GetComponent<Spot::MeshRenderer>();
        renderer && edit.painted && !(renderer->GetMaterial() && *renderer->GetMaterial() == edit.material)) {
        renderer->SetMaterial(edit.material);
    }
    if (Describe(*prop).joints == edit.joints) {
        return;
    }
    // Joined anew, as the server has it now.
    while (prop->RemoveComponent<Spot::FixedJoint>()) {
    }
    while (prop->RemoveComponent<Spot::DistanceJoint>()) {
    }
    for (const Joined& joined : edit.joints) {
        if (joined.rope) {
            auto& joint = prop->AddComponent<Spot::DistanceJoint>();
            joint.SetAnchor(joined.anchor);
            joint.SetConnectedEntity(joined.connected);
            joint.SetConnectedAnchor(joined.connectedAnchor);
            joint.SetDistance(joined.minDistance, joined.maxDistance);
        } else {
            auto& joint = prop->AddComponent<Spot::FixedJoint>();
            joint.SetAnchor(joined.anchor);
            joint.SetConnectedEntity(joined.connected);
        }
    }
}

void PropEdits::Replicate(Spot::Archive& archive) {
    std::vector<Edit> edits;
    if (!archive.IsLoading()) {
        std::erase_if(m_props, [this](Spot::EntityId id) { return !GetScene().GetEntity(id); });
        for (const Spot::EntityId id : m_props) {
            edits.push_back(Describe(*GetScene().GetEntity(id)));
        }
    }
    archive.Field("edits", edits);
    if (archive.IsLoading()) {
        for (const Edit& edit : edits) {
            Apply(edit);
        }
    }
}
