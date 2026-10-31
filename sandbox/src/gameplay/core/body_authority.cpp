#include "gameplay/core/body_authority.h"

#include "gameplay/core/pawn.h"

#include <unordered_map>
#include <unordered_set>

namespace Gameplay {

namespace {

// The objects given to clients, by their entities.
class GivenBodies final : public Spot::SceneSystem {
public:
    std::unordered_map<Spot::EntityId, Spot::NetPeerId> given;
    std::unordered_set<Spot::EntityId> kept;

    // Forgets what is gone or no longer that client's, as when it left, giving the server back its say.
    void Sweep() {
        std::erase_if(kept, [this](Spot::EntityId id) { return !GetScene().GetEntity(id); });
        std::erase_if(given, [this](const auto& entry) {
            Spot::Entity* entity = GetScene().GetEntity(entry.first);
            auto* object = entity ? entity->GetComponent<Spot::NetworkObject>() : nullptr;
            if (object && object->GetOwner() == entry.second) {
                return false;
            }
            if (object) {
                object->authority = Spot::NetworkAuthority::Server;
            }
            return true;
        });
    }
};

// Whether a joint holds the entity's body, whose partner would be simulated apart from it.
bool IsJointed(const Spot::Entity& entity) {
    bool jointed = false;
    entity.GetScene().ForEachComponent<Spot::Joint>([&](const Spot::Joint& joint) {
        jointed = jointed || &joint.GetEntity() == &entity || joint.GetConnectedEntity() == entity.GetId();
    });
    return jointed;
}

Spot::NetworkObject* GetBodyObject(const Spot::Entity& body) {
    Spot::NetworkObject* object = Spot::Network::GetObject(body);
    if (!object || !object->GetEntity().GetComponent<Spot::Rigidbody>() || Pawn::Of(object->GetEntity())) {
        return nullptr;
    }
    return object;
}

} // namespace

namespace BodyAuthority {

bool Give(Spot::Entity& body, Spot::NetPeerId peer) {
    Spot::NetworkSystem* system = Spot::Network::Get(body.GetScene());
    Spot::NetworkObject* object = GetBodyObject(body);
    if (!system || !system->IsServer() || !object || peer == Spot::NetPeerId::Invalid) {
        return false;
    }
    auto& given = body.GetScene().GetOrAddSystem<GivenBodies>();
    given.Sweep();
    if (object->GetOwner() == peer && given.given.contains(object->GetEntity().GetId())) {
        return true;
    }
    if (object->GetOwner() != Spot::NetPeerId::Invalid || given.kept.contains(object->GetEntity().GetId()) ||
        IsJointed(object->GetEntity())) {
        return false;
    }
    object->authority = Spot::NetworkAuthority::Owner;
    system->SetOwner(*object, peer);
    given.given[object->GetEntity().GetId()] = peer;
    return true;
}

void TakeBack(Spot::Entity& body) {
    Spot::NetworkSystem* system = Spot::Network::Get(body.GetScene());
    auto* given = body.GetScene().GetSystem<GivenBodies>();
    Spot::NetworkObject* object = GetBodyObject(body);
    if (!system || !given || !object || !given->given.erase(object->GetEntity().GetId())) {
        return;
    }
    object->authority = Spot::NetworkAuthority::Server;
    system->SetOwner(*object, Spot::NetPeerId::Invalid);
}

Spot::NetPeerId GetHolder(const Spot::Entity& body) {
    auto* given = body.GetScene().GetSystem<GivenBodies>();
    const Spot::NetworkObject* object = GetBodyObject(body);
    if (!given || !object) {
        return Spot::NetPeerId::Invalid;
    }
    given->Sweep();
    const auto found = given->given.find(object->GetEntity().GetId());
    return found != given->given.end() ? found->second : Spot::NetPeerId::Invalid;
}

std::vector<Spot::EntityId> GetGiven(Spot::Scene& scene, Spot::NetPeerId peer) {
    std::vector<Spot::EntityId> entities;
    auto* given = scene.GetSystem<GivenBodies>();
    if (!given) {
        return entities;
    }
    given->Sweep();
    for (const auto& [entity, holder] : given->given) {
        if (holder == peer) {
            entities.push_back(entity);
        }
    }
    return entities;
}

void KeepOnServer(Spot::Entity& body, bool keep) {
    const Spot::NetworkObject* object = Spot::Network::GetObject(body);
    if (!object) {
        return;
    }
    auto& given = body.GetScene().GetOrAddSystem<GivenBodies>();
    if (keep) {
        TakeBack(body);
        given.kept.insert(object->GetEntity().GetId());
    } else {
        given.kept.erase(object->GetEntity().GetId());
    }
}

} // namespace BodyAuthority

} // namespace Gameplay
