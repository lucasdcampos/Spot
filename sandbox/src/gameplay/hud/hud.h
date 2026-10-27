#pragma once

#include "gameplay/core/pawn.h"

#include <spot.h>

#include <span>
#include <string>
#include <vector>

namespace Gameplay {

class Hud;
class PlayerController;

// What the HUD shows for, each frame: whose it is and what that player's pawn is doing.
struct HudContext {
    Hud* hud = nullptr;
    // Null while the scene has no such player, or it possesses nothing.
    PlayerController* player = nullptr;
    Pawn* pawn = nullptr;
    // The pawn riding in it, as the character in a vehicle's seat; null when there is none.
    Pawn* occupant = nullptr;
    // The pawn's states, which decide what shows.
    std::span<const std::string> states;
    // Unscaled, so the HUD goes on while the game is paused: seconds since the HUD started, and since last frame.
    float time = 0.0f;
    float deltaTime = 0.0f;
    // Whether the cursor is the game's, with no menu over it.
    bool inGame = true;

    // The component on the pawn's entity or, failing that, on its occupant's: what follows the player whichever pawn
    // they possess, as their health.
    template <typename T>
    T* Find() const {
        T* found = pawn ? pawn->GetEntity().GetComponent<T>() : nullptr;
        return found ? found : occupant ? occupant->GetEntity().GetComponent<T>() : nullptr;
    }
};

// One part of the HUD, on an authored UI entity: what it shows and when. It shows while the pawn has all the states
// of showWhen, none of hideWhen and IsWanted() says so; its entity is active while any element on it shows, so two
// elements can take turns at the same widgets, as what is in hand on foot and the vehicle's gauges in a vehicle.
// Elements live with the system they show; a game adds a situation with a state and elements that show on it.
class HudElement : public Spot::Component {
public:
    std::vector<std::string> showWhen;
    std::vector<std::string> hideWhen;

    // Whether it showed last frame.
    bool IsShown() const { return m_shown; }

protected:
    friend class Hud;

    // Whether it has something to show now, besides the states. It is asked every frame, shown or not, so it can keep
    // track of what it shows meanwhile, as a panel an action toggles.
    virtual bool IsWanted(const HudContext& /*context*/) { return true; }
    // Updates what it draws into, while it shows.
    virtual void Refresh(const HudContext& context) = 0;

    void Serialize(Spot::Archive& archive) override;

    // Helpers for the widgets elements draw into.
    template <typename T>
    T* Get(Spot::EntityId id) const {
        Spot::Entity* entity = GetScene().GetEntity(id);
        return entity ? entity->GetComponent<T>() : nullptr;
    }
    void SetText(Spot::EntityId id, std::string_view text) const;
    void SetActive(Spot::EntityId id, bool active) const;

private:
    bool m_shown = false;
};

// The HUD of a player: each frame, it shows the HudElements below it whose conditions hold and refreshes them, and
// hides the others. Without elements of its own below it, it makes its interface from the prefab.
class Hud : public Spot::Component {
public:
    // Made below the HUD's entity when none of its elements is there yet.
    Spot::AssetRef<Spot::Prefab> interface;
    // The font of the HUD's text, for what draws text outside the interface, such as developer tools.
    Spot::AssetRef<Spot::Font> font;
    // Seconds the HUD introduces the controls for as the game starts, which elements showing controls follow.
    float introTime = 12.0f;
    // The player it shows for, by index among the scene's (see Players).
    int playerIndex = 0;

    // How strongly the controls are introduced now: 1 early on, fading to 0 after introTime.
    float GetIntro() const;

protected:
    void OnStart() override;
    void OnEnable() override;
    void OnDisable() override;
    void OnDestroy() override;
    void OnLateUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;

private:
    HudContext MakeContext(float deltaTime);

    Spot::EntityId m_interface = Spot::EntityId::Invalid;
    float m_time = 0.0f;
};

} // namespace Gameplay
