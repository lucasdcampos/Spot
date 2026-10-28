#pragma once

#include <spot.h>
#include <spot/scene/animation/animation_event_listener.h>

#include "gameplay/core/pawn.h"
#include "gameplay/health/damage.h"

#include <optional>
#include <random>
#include <string>
#include <vector>

// The sandbox's humanoid, patrolling, fighting and falling as a recoverable ragdoll when its Health runs out. Its
// prefab supplies the model, animation controller, health and loadout.
//
// Its attitude says whom it fights: an enemy fights the players and their allies, an ally fights enemies and follows
// the player otherwise, and a neutral one fights nobody. Each fights back whoever hurts it, unless that is on its
// side. It shoots with the firearm it holds, or hits what is within reach without one.
class Npc final : public Gameplay::Pawn, public Spot::AnimationEventListener, public Gameplay::DamageReceiver {
public:
    enum class Attitude { Ally, Neutral, Enemy };

    Attitude attitude = Attitude::Neutral;
    Spot::Vector3 patrolAxis = {0, 0, 1};
    float patrolDistance = 3;
    float turnSpeed = 4;
    // How far it sees what it fights, and how far it shoots from; beyond, it walks closer.
    float sightRange = 30;
    float attackRange = 16;
    // Seconds from seeing what it fights to its first shot, and at least between shots.
    float reactionTime = 1.0f;
    float shotPause = 0.8f;
    // In radians, how far its shots may stray from where it aims.
    float aimError = 0.1f;
    // The share of its firearm's damage its shots take, so it does not take players down as fast as they do it.
    float damageScale = 0.5f;
    // Without a firearm: how close it hits, how hard and how often.
    float meleeRange = 1.3f;
    float meleeDamage = 10;
    float meleeInterval = 0.9f;
    // Damage of these types that does not kill it, as a car running it over, knocks it down as a ragdoll; it gets up
    // after knockdownTime seconds.
    std::vector<std::string> knockdownTypes = {"Impact"};
    float knockdownTime = 3.0f;
    // How close an ally keeps to the player.
    float followDistance = 4;
    // Within the near distance of the camera, the pose updates every frame; beyond it 30 times a second, and beyond
    // the far distance 12 times.
    float animationNear = 12;
    float animationFar = 30;
    // The kind of the SurfaceLibrary its footsteps sound like.
    std::string footstepSurface = "Concrete";
    std::vector<Spot::AssetRef<Spot::Prefab>> loadout;

    bool IsDead() const { return m_dead; }
    bool IsKnockedDown() const { return m_down > 0; }
    std::uint32_t GetFootsteps() const { return m_footsteps; }
    // What it fights now; invalid when nothing.
    Spot::EntityId GetTarget() const { return m_target; }
    // Whether it fights the entity's pawn: the player's, another NPC or whoever hurt it.
    bool IsHostileTo(const Spot::Entity& entity) const;
    // Kills it at once, falling as a ragdoll pushed by the impulse.
    void Hit(Spot::Vector3 impulse, Spot::Vector3 point);
    // Up again with full health, forgetting who hurt it.
    void Revive();

    // While it shoots, it looks where it aims.
    Gameplay::ViewPoint GetViewPoint() const override;

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnFixedUpdate(float deltaTime) override;
    void OnAnimationEvent(const Spot::AnimationEvent& event) override;
    void OnDamage(const Gameplay::DamageInfo& damage) override;
    void Serialize(Spot::Archive& archive) override;

private:
    struct Move {
        std::optional<Spot::Vector3> facing;
        float speed = 0;
    };
    void Die();
    void KnockDown(const Gameplay::DamageInfo& damage);
    void Think();
    bool IsAlive(const Spot::Entity& entity) const;
    bool CanSee(const Spot::Entity& target, Spot::Vector3 point) const;
    Move Patrol(float deltaTime);
    Move Fight(Spot::Entity& target);
    Move Follow();
    // Uses what it holds, firing it when asked, so it also reloads by itself.
    void UseItem(bool fire);

    bool m_dead = false;
    bool m_hasHome = false;
    bool m_positive = false;
    Spot::Vector3 m_home;
    float m_pause = 0;
    float m_fall = 0;
    // Seconds before it gets up again; 0 while standing.
    float m_down = 0;
    std::uint32_t m_footsteps = 0;
    // Who hurt it, which it fights too.
    std::vector<Spot::EntityId> m_provokers;
    Spot::EntityId m_target = Spot::EntityId::Invalid;
    Spot::Vector3 m_lastSeen;
    bool m_seesTarget = false;
    // Seconds since it last saw what it fights.
    float m_lost = 0;
    // Its Health killed it during this frame, pushing it already.
    bool m_killedNow = false;
    float m_think = 0;
    float m_attackDelay = 0;
    std::optional<Spot::Vector3> m_aim;
    Spot::SignalConnection m_died;
    std::mt19937 m_random{5678};
};
