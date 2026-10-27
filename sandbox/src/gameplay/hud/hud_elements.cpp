#include "gameplay/hud/hud_elements.h"
#include <spot/scene/physics/physics_world.h>

#include "gameplay/core/interactor.h"
#include "gameplay/core/pawn.h"
#include "gameplay/hud/input_prompts.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>

namespace Gameplay {

SPOT_COMPONENT(PromptElement);
SPOT_COMPONENT(StatisticsElement);

namespace {

// The average milliseconds per frame that the profiled frames spent in scopes of that name.
double GetAverageMilliseconds(std::string_view scope) {
    const auto frames = Spot::Profiler::GetFrames();
    if (frames.empty()) {
        return 0.0;
    }
    std::chrono::nanoseconds total{};
    for (const Spot::ProfileFrame* frame : frames) {
        for (const Spot::ProfileSample& sample : frame->samples) {
            total += sample.name == scope ? sample.duration : std::chrono::nanoseconds{};
        }
    }
    return std::chrono::duration<double, std::milli>(total).count() / static_cast<double>(frames.size());
}

} // namespace

std::string PromptElement::GetPrompt(const HudContext& context) const {
    const auto* interactor = context.pawn ? context.pawn->GetEntity().GetComponent<Interactor>() : nullptr;
    return interactor && interactor->HasFocus() ? InputPrompts::Format(interactor->action, interactor->GetPrompt())
                                                : std::string();
}

void PromptElement::Refresh(const HudContext& context) {
    if (auto* text = GetEntity().GetComponent<Spot::UiText>()) {
        if (std::string prompt = GetPrompt(context); text->text != prompt) {
            text->text = std::move(prompt);
        }
    }
}

bool StatisticsElement::IsWanted(const HudContext& context) {
    if (context.deltaTime > 0.0f) {
        m_frameRate =
            m_frameRate > 0.0f ? std::lerp(m_frameRate, 1.0f / context.deltaTime, 0.1f) : 1.0f / context.deltaTime;
    }
    if (Spot::Input::IsActionPressed(toggleAction)) {
        m_toggled = !m_toggled;
        UpdateText();
    }
    m_sinceText += context.deltaTime;
    if (m_sinceText >= 0.5f || m_text.empty()) {
        UpdateText();
    }
    // Written hidden too, so it is right the moment it shows.
    SetText(text, m_text);
    return m_toggled;
}

void StatisticsElement::UpdateText() {
    m_sinceText = 0.0f;
    m_text = std::format("{} FPS   {} bodies   update {:.2f} ms   render {:.2f} ms",
                         static_cast<int>(std::lround(m_frameRate)),
                         GetScene().GetOrAddSystem<Spot::PhysicsWorld>().GetBodyCount(),
                         GetAverageMilliseconds("Scene::Update"), GetAverageMilliseconds("Application::Render"));
}

void StatisticsElement::Refresh(const HudContext& /*context*/) {
}

void StatisticsElement::Serialize(Spot::Archive& archive) {
    HudElement::Serialize(archive);
    archive.Field("text", text);
    archive.Field("toggleAction", toggleAction, {.tooltip = "The input action that shows and hides it"});
}

} // namespace Gameplay
