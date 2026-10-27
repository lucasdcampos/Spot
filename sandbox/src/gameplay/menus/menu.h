#pragma once

#include <spot.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Gameplay {

class Menu;

// The colors menus draw the state of their controls in: the outline, label and icon of each, toward what its state
// calls for.
struct MenuStyle {
    // What is selected.
    Spot::Color accent = {1.0f, 0.68f, 0.22f, 1.0f};
    Spot::Color text = {0.8070f, 0.8228f, 0.8469f, 1.0f};
    Spot::Color textSecondary = {0.3231f, 0.3663f, 0.4233f, 1.0f};
    Spot::Color textMuted = {0.1274f, 0.1500f, 0.1845f, 1.0f};
    // The outlines of controls at rest, under the pointer and focused.
    Spot::Color line = {1.0f, 1.0f, 1.0f, 0.07f};
    Spot::Color lineHovered = {1.0f, 1.0f, 1.0f, 0.16f};
    Spot::Color lineFocused = {1.0f, 1.0f, 1.0f, 0.45f};

    void Serialize(Spot::Archive& archive);
};

// A part of a menu, on the menu's entity, such as the settings in the pause menu: the menu tells it what happens to it.
class MenuPart {
public:
    virtual ~MenuPart() = default;

    virtual void OnMenuInitialize(Menu& /*menu*/) {}
    virtual void OnMenuOpen(Menu& /*menu*/) {}
    virtual void OnMenuClose(Menu& /*menu*/) {}
    virtual void AnimateMenu(Menu& /*menu*/, float /*blend*/) {}
    // A raw event while the menu is open, before the menu's toggle action sees it.
    virtual void OnMenuRawEvent(Menu& /*menu*/, const Spot::Event& /*event*/, bool& /*consumed*/) {}
    // While busy, as waiting for a control to bind, the toggle action neither opens nor closes the menu.
    virtual bool IsBusy() const { return false; }
};

// The menus of a scene, for what has to know as one opens, as a tool that slows time.
class MenuSystem final : public Spot::SceneSystem {
public:
    static MenuSystem& Get(Spot::Scene& scene);

    // Before the menu opens and takes the input and, if it pauses, the time.
    Spot::Signal<Menu&> OnOpening;
};

// A menu over the game, on an authored canvas: its toggle action opens it while no other menu is open, and closes it
// again; the canvas's cancel closes it. One menu is open at a time. It eases in, its panels rising into place as its
// backdrop darkens, and it may pause the game while open. It lives on an entity that stays active, beside its canvas,
// with the MenuParts it is made of.
class Menu : public Spot::Component {
public:
    // The canvas shown while it is open; its children are the panels that ease in, and its UiImage the backdrop.
    Spot::EntityId canvas = Spot::EntityId::Invalid;
    std::string toggleAction;
    int playerIndex = 0;
    // The control that has the focus as it opens.
    Spot::EntityId firstFocus = Spot::EntityId::Invalid;
    // Stops time while it is open.
    bool pausesGame = false;
    Spot::AssetRef<Spot::Sound> clickSound;
    MenuStyle style;

    // The open menu of the scene; null when none is.
    static Menu* GetOpen(Spot::Scene& scene);
    static void CloseAll(Spot::Scene& scene);

    bool IsOpen() const { return m_open; }
    void Open();
    void Close();
    // Plays the click on the Interface bus, heard while the game is paused.
    void PlayClick() const;
    // The outline, label and icon of a control, toward its state: selected, focused, under the pointer or at rest,
    // where its label is the rest color.
    void StyleControl(Spot::EntityId control, bool selected, Spot::Color rest, float blend);
    void ConnectButton(Spot::EntityId id, std::function<void()> callback);
    // Has the UI find its controls again, as after controls were shown or hidden.
    void RefreshInput();

protected:
    // Once, before it is first used: builds what it makes from data.
    virtual void Initialize() {}
    virtual void OnOpen() {}
    virtual void OnClose() {}
    // Whether its toggle action opens or closes it now: not while a part is busy.
    virtual bool CanToggle() const;
    // Eases its controls toward their state, by the share of the way given.
    virtual void AnimateControls(float /*blend*/) {}
    // Gives a raw event to the menu before its toggle action sees it; set consumed to keep it from that.
    virtual void OnRawEvent(const Spot::Event& /*event*/, bool& /*consumed*/) {}

    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDisable() override;
    void OnDestroy() override;
    void Serialize(Spot::Archive& archive) override;

    void EnsureInitialized();

    template <typename T>
    T* Get(Spot::EntityId id) const {
        Spot::Entity* entity = GetScene().GetEntity(id);
        return entity ? entity->GetComponent<T>() : nullptr;
    }
    template <typename T>
    T* GetChild(Spot::EntityId id, std::string_view name) const {
        Spot::Entity* entity = GetScene().GetEntity(id);
        Spot::Entity* child = entity ? entity->FindChild(name) : nullptr;
        return child ? child->GetComponent<T>() : nullptr;
    }

    std::vector<Spot::SignalConnection> m_connections;

private:
    std::vector<MenuPart*> GetParts() const;
    void Animate(float elapsed);
    void HandleRawEvent(const Spot::Event& event, bool& consumed);
    bool Matches(const Spot::Event& event) const;

    bool m_open = false;
    bool m_initialized = false;
    bool m_paused = false;
    float m_previousTimeScale = 1.0f;
    float m_time = 0.0f;
    // Where its panels sit and how dark its backdrop is, as authored, which it eases in from.
    std::vector<std::pair<Spot::EntityId, Spot::Vector2>> m_offsets;
    float m_backdrop = 0.0f;
    bool m_measured = false;
};

} // namespace Gameplay
