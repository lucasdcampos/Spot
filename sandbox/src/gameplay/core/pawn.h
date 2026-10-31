#pragma once

#include <spot.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Gameplay {

class Controller;

// Where a pawn looks from, the unit vector it looks along, and the rotation that looks that way.
struct ViewPoint {
    Spot::Vector3 position;
    Spot::Vector3 direction = {0.0f, 0.0f, -1.0f};
    Spot::Quaternion rotation;
};

// How a pawn's body moves now, for what is drawn with it to move along, as a held item bobbing with the walk.
struct PawnMotion {
    // Steps taken, with the share of the current one: each step lands as it reaches a whole number.
    float stepPhase = 0.0f;
    // How much the walk bobs what moves with it: 0 standing still, 1 walking and more running.
    float bobWeight = 0.0f;
};

// A part of a pawn that magnifies its view, as a firearm aimed down its sights does.
class ViewZoom {
public:
    virtual ~ViewZoom() = default;

    // How many times it magnifies the view now; 1 for not at all.
    virtual float GetViewZoom() const = 0;
};

// Anything a controller can take over: the character on foot, a vehicle. Possession is the one switch between them,
// so no part turns another's components on and off; parts of a pawn read their input through contexts activated on
// its controller (see InputContext), and see none while it has no controller. In a session, the server decides its
// states, who rides it and whether it is possessed, and the other machines are shown them (see Spot::Replicated).
class Pawn : public Spot::Component, public Spot::Replicated {
public:
    // The entity it looks from, along its forward; the pawn's own when unset.
    Spot::EntityId eye = Spot::EntityId::Invalid;
    // The entity of the camera its controller shows while possessing it.
    Spot::EntityId viewCamera = Spot::EntityId::Invalid;
    // States each machine keeps for itself, which the server does not decide: how its own player sees the pawn, and
    // what each machine's parts work out themselves, as a firearm aiming, at once for its player.
    std::vector<std::string> localStates = {"ThirdPerson", "Placing", "Choosing", "Aiming"};

    // The pawn of the entity or of the nearest entity above it; null when there is none.
    static Pawn* Of(const Spot::Entity& entity);
    // The controller of that pawn; null when there is no pawn or nothing possesses it.
    static Controller* GetControllerOf(const Spot::Entity& entity);

    Controller* GetController() const;
    bool IsPossessed() const { return GetController() != nullptr; }
    // Whether the machine deciding the pawn has it possessed: on clients, as the server last said; elsewhere, whether
    // it is possessed.
    bool IsPossessedWhereDecided() const;
    // Runs a discrete action through this pawn's input listeners on the server. On its owning client, sends a
    // reliable request instead; NetworkPlayers validates that the sender still possesses this pawn.
    void Request(std::string_view name);
    // Aiming, interaction and whatever else looks where the pawn looks follow it.
    virtual ViewPoint GetViewPoint() const;
    virtual Spot::EntityId GetViewCamera() const { return viewCamera; }
    virtual PawnMotion GetMotion() const { return {}; }
    // How many times its parts magnify its view now (see ViewZoom).
    float GetZoom() const;
    // Puts it at the position, facing the direction, moving at the velocity, as on getting out of a vehicle.
    virtual void Place(Spot::Vector3 position, Spot::Vector3 forward, Spot::Vector3 velocity);
    // Clears motion when the game returns it to its start, preserving where it looks.
    virtual void ResetMotion();

    // The pawn riding in it, as a character in a vehicle's seat, for what follows a player whichever pawn they
    // possess, as their health on the HUD; null when there is none.
    Pawn* GetOccupant() const;
    void SetOccupant(Pawn* occupant);

    // What it is doing, for parts that need to know without knowing who said so: "Dead", "Aiming", "HandsBusy",
    // "Seated". Set by whoever knows it; transient, never saved.
    void SetState(std::string_view state, bool on);
    bool HasState(std::string_view state) const;
    // Whether it has any of the states.
    bool HasAnyState(std::span<const std::string> states) const;
    std::span<const std::string> GetStates() const { return m_states; }

    Spot::Signal<Controller&> OnPossessed;
    Spot::Signal<> OnUnpossessed;
    // A state set on or off, as it changes, for whatever must act on it at once.
    Spot::Signal<std::string_view, bool> OnStateChanged;

    void Replicate(Spot::Archive& archive) override;

protected:
    // As a controller takes it over, before OnPossessed, and as it lets it go, before OnUnpossessed.
    virtual void OnPossess(Controller& /*controller*/) {}
    virtual void OnUnpossess() {}

    void OnDestroy() override;
    void Serialize(Spot::Archive& archive) override;

private:
    friend class Controller;

    Spot::EntityId m_controller = Spot::EntityId::Invalid;
    Spot::EntityId m_occupant = Spot::EntityId::Invalid;
    std::vector<std::string> m_states;
    bool m_possessedOnServer = false;
};

} // namespace Gameplay
