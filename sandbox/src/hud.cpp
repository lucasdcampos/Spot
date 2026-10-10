#include "hud.h"

#include "day_cycle.h"
#include "tool_gun.h"
#include "weapon.h"
#include "weapon_holder.h"

#include <chrono>
#include <format>
#include <string_view>

SPOT_COMPONENT(Hud);

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

void Hud::OnUpdate(float deltaTime) {
    ++m_frames;
    m_elapsed += deltaTime;
    if (m_elapsed < 1.0f) {
        return;
    }
    const DayCycle* day = GetScene().FindComponent<DayCycle>();
    const int minutes = day ? static_cast<int>(day->timeOfDay * 24.0f * 60.0f) : 0;
    m_status = std::format("{:02}:{:02} | {} FPS, {} bodies | update {:.2f} ms, render {:.2f} ms", minutes / 60,
                           minutes % 60, m_frames, GetScene().GetPhysics().GetBodyCount(),
                           GetAverageMilliseconds("Scene::Update"), GetAverageMilliseconds("Application::Render"));
    m_frames = 0;
    m_elapsed -= 1.0f;
}

std::string Hud::DescribeHand() const {
    const WeaponHolder* holder = GetScene().FindComponent<WeaponHolder>();
    if (!holder) {
        return {};
    }
    if (holder->IsToolGunSelected()) {
        const auto* tool = holder->GetEntity().GetComponent<ToolGun>();
        constexpr const char* Modes[] = {"Weld", "Rope", "Remover"};
        return tool ? std::format("Tool Gun: {}", Modes[static_cast<int>(tool->mode)]) : "Tool Gun";
    }
    const Weapon* weapon = holder->GetWeapon();
    if (!weapon) {
        return "Physics Gun";
    }
    if (weapon->IsReloading()) {
        return std::format("{}  reloading...", weapon->displayName);
    }
    return std::format("{}  {} / {}", weapon->displayName, weapon->GetAmmo(), weapon->GetReserve());
}

void Hud::OnDrawUi(Spot::Renderer2D& renderer, Spot::Vector2 viewSize) {
    const Spot::Vector2 center = viewSize * 0.5f;
    const Spot::Color crosshair{1.0f, 1.0f, 1.0f, 0.8f};
    renderer.DrawQuad(center, {14.0f, 2.0f}, crosshair);
    renderer.DrawQuad(center, {2.0f, 14.0f}, crosshair);
    if (!font) {
        return;
    }

    // Text goes down from the top-left corner.
    const float top = viewSize.y;
    const float line = font->GetLineHeight();
    const Spot::Color help = Spot::Color::FromSrgb(0.8f, 0.8f, 0.8f);
    renderer.DrawQuad({330.0f, top - 60.0f}, {660.0f, 120.0f}, Spot::Color{0.0f, 0.0f, 0.0f, 0.6f});
    renderer.DrawString(*font, m_status, {12.0f, top - 10.0f}, Spot::Color::FromSrgb(0.7f, 0.9f, 0.7f));
    renderer.DrawString(*font, "WASD: move | Shift: run | Space: jump | Ctrl: crouch | V: noclip | Backspace: restart",
                        {12.0f, top - 10.0f - line}, help, 0.8f);
    renderer.DrawString(
        *font, "F: flashlight | G: shadows | B: physics shapes | T: speed up time | Esc: free the mouse, then quit",
        {12.0f, top - 10.0f - 1.8f * line}, help, 0.8f);
    renderer.DrawString(*font,
                        "1: physics gun (hold click, wheel, E to turn, right click to freeze, U to unfreeze) | "
                        "2-5: weapons, R to reload | 6: tool gun, R for weld, rope or remover | E: pick up",
                        {12.0f, top - 10.0f - 2.6f * line}, help, 0.8f);
    renderer.DrawString(*font,
                        "F1-F8: spawn a crate, barrel, explosive barrel, ball, plank, beam, cone or wheel | "
                        "Z: undo | X: clean up",
                        {12.0f, top - 10.0f - 3.4f * line}, help, 0.8f);
    // What is in hand, at the bottom right.
    const std::string hand = DescribeHand();
    if (!hand.empty()) {
        renderer.DrawString(*font, hand, {viewSize.x - 12.0f - 10.0f * static_cast<float>(hand.size()), 40.0f},
                            Spot::Color::FromSrgb(1.0f, 0.95f, 0.7f));
    }
}

void Hud::Serialize(Spot::Archive& archive) {
    archive.Field("font", font);
}
