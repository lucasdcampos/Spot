#pragma once

// Backend-internal: the contacts between bodies, as Jolt reports them.

#include "physics/jolt/jolt_common.h"

#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/ContactListener.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Spot::Detail {

// Keeps the contacts the bodies have, as Jolt reports them from its threads during a step. Jolt also reports contacts
// removed when a body falls asleep, which must not end them, so it decides when they end itself: after a step, a
// contact Jolt did not find again ends unless one of its bodies sleeps.
class ContactRecorder final : public JPH::ContactListener {
public:
    void OnContactAdded(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold,
                        JPH::ContactSettings& settings) override;
    void OnContactPersisted(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold,
                            JPH::ContactSettings& settings) override;

    // The contacts kept from now on were not found again.
    void BeginStep();
    void EndStep(const JPH::BodyInterface& bodies);
    void ForgetBody(JPH::BodyID body);
    void GetContacts(std::vector<PhysicsContact>& contacts) const;

private:
    // The two bodies and shapes of a contact, as Jolt tells them apart.
    struct Key {
        std::uint32_t first;
        std::uint32_t firstShape;
        std::uint32_t second;
        std::uint32_t secondShape;

        friend bool operator==(const Key&, const Key&) = default;
    };

    struct KeyHash {
        std::size_t operator()(const Key& key) const;
    };

    void Record(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold);

    std::mutex m_mutex;
    std::unordered_map<Key, PhysicsContact, KeyHash> m_contacts;
};

} // namespace Spot::Detail
