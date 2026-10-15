#pragma once
#include "scene/physics/physics_query.h"
#include "scene/physics2d/collider_2d.h"
#include "scene/physics2d/collision_2d.h"
#include "scene/physics2d/rigidbody_2d.h"
#include "scene/scene.h"

#include <compare>
#include <optional>
#include <unordered_map>

namespace Spot {
struct RaycastHit2D {
    EntityId entity = EntityId::Invalid;
    Collider2D* collider = nullptr;
    Vector2 point = {};
    Vector2 normal = {};
    float distance = 0.0f;
};
class PhysicsWorld2D final : public SceneSystem {
public:
    static constexpr Vector2 DefaultGravity = {0.0f, -9.81f};
    Vector2 GetGravity() const { return m_gravity; }
    void SetGravity(Vector2 gravity);
    std::size_t GetBodyCount() const { return m_bodies.size(); }
    bool GetLayerCollision(int first, int second) const;
    void SetLayerCollision(int first, int second, bool collides);
    std::uint32_t GetLayerMask(int layer) const;
    void SetLayerMask(int layer, std::uint32_t mask);
    std::optional<RaycastHit2D> Raycast(Vector2 origin, Vector2 direction, float distance,
                                        const QueryFilter& filter = {});
    std::vector<RaycastHit2D> RaycastAll(Vector2 origin, Vector2 direction, float distance,
                                         const QueryFilter& filter = {});
    std::optional<RaycastHit2D> CircleCast(Vector2 origin, float radius, Vector2 direction, float distance,
                                           const QueryFilter& filter = {});
    std::vector<RaycastHit2D> OverlapCircle(Vector2 center, float radius, const QueryFilter& filter = {});
    std::vector<RaycastHit2D> OverlapBox(Vector2 center, Vector2 size, float angle = 0.0f,
                                         const QueryFilter& filter = {});

protected:
    void OnFixedUpdate(float deltaTime) override;

private:
    friend class Scene;
    friend class Rigidbody2D;
    PhysicsWorld2D();
    struct Body {
        BodyId2D id = BodyId2D::Invalid;
        std::size_t stamp = 0;
        Vector2 scale = {};
        Pose2D pose = {};
        BodyMotion2D motion = BodyMotion2D::Static;
        std::vector<ComponentId> colliders;
        bool seen = false;
    };
    struct Pair {
        EntityId first;
        EntityId second;
        bool trigger;
        bool firstTrigger;
        friend auto operator<=>(const Pair&, const Pair&) = default;
    };
    struct PairContact {
        Pair pair;
        Collision2D collision;
        bool active;
    };
    void Synchronize();
    BodyId2D FindBody(EntityId entity);
    QueryFilter2D MakeFilter(const QueryFilter& filter);
    std::vector<RaycastHit2D> ConvertHits();
    void DispatchContacts();
    void Dispatch(const PairContact& contact, int phase);
    Physics2D m_simulation;
    Vector2 m_gravity = DefaultGravity;
    std::array<std::uint32_t, 32> m_masks;
    std::unordered_map<EntityId, Body> m_bodies;
    std::unordered_map<BodyId2D, EntityId> m_owners;
    std::vector<Contact2D> m_contacts;
    std::vector<PairContact> m_previous;
    std::vector<PairContact> m_current;
    std::vector<QueryHit2D> m_hits;
};
} // namespace Spot
