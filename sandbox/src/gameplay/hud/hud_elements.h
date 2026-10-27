#pragma once

#include "gameplay/hud/hud.h"

#include <spot.h>

#include <string>
#include <vector>

namespace Gameplay {

// The UiText of its entity says what the pawn's Interactor would do with what it looks at, with the key that does it:
// "[E]  Drive". A game that has other things to say there overrides GetPrompt().
class PromptElement : public HudElement {
protected:
    virtual std::string GetPrompt(const HudContext& context) const;
    void Refresh(const HudContext& context) override;
};

// The frame rate, the number of bodies and the time updating and rendering take, in its text, refreshed twice a
// second; its toggle action shows and hides it.
class StatisticsElement : public HudElement {
public:
    Spot::EntityId text = Spot::EntityId::Invalid;
    std::string toggleAction = "Statistics";

    bool IsToggled() const { return m_toggled; }

protected:
    bool IsWanted(const HudContext& context) override;
    void Refresh(const HudContext& context) override;
    void Serialize(Spot::Archive& archive) override;

private:
    void UpdateText();

    bool m_toggled = false;
    float m_frameRate = 0.0f;
    float m_sinceText = 0.0f;
    std::string m_text;
};

} // namespace Gameplay
