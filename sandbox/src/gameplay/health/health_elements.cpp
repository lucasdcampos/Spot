#include "gameplay/health/health_elements.h"

#include "gameplay/core/pawn.h"
#include "gameplay/health/damage.h"
#include "gameplay/health/health.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace Gameplay {

SPOT_COMPONENT(HealthElement);
SPOT_COMPONENT(DamageFlashElement);
SPOT_COMPONENT(HitMarkerElement);

namespace {

// Bars ease toward their value in about 120 milliseconds: three of these.
constexpr float StateTime = 0.04f;

} // namespace

Health* HealthElement::GetHealth(const HudContext& context) {
    return context.Find<Health>();
}

void HealthElement::Refresh(const HudContext& context) {
    const Health* health = GetHealth(context);
    const float value = health ? health->GetHealth() : 100.0f;
    const float most = health ? health->maxHealth : 100.0f;
    const float share = most > 0.0f ? std::clamp(value / most, 0.0f, 1.0f) : 0.0f;
    m_shown = m_shown < 0.0f || context.deltaTime <= 0.0f
                  ? share
                  : std::lerp(m_shown, share, 1.0f - std::exp(-context.deltaTime / StateTime));
    const bool low = health && !health->IsDead() && share <= lowShare;
    const Spot::Color tint = low ? lowColor : color;
    SetText(text, std::to_string(static_cast<int>(std::ceil(std::max(value, 0.0f)))));
    if (auto* widget = Get<Spot::UiText>(text)) {
        widget->color = tint;
    }
    if (auto* image = Get<Spot::UiImage>(icon)) {
        image->tint = tint;
    }
    if (auto* rect = Get<Spot::UiRect>(fill)) {
        rect->layout.sizeDelta.x = m_shown * fillWidth;
    }
    if (auto* image = Get<Spot::UiImage>(fill)) {
        image->tint = tint;
    }
}

void HealthElement::Serialize(Spot::Archive& archive) {
    HudElement::Serialize(archive);
    archive.Field("text", text);
    archive.Field("icon", icon);
    archive.Field("fill", fill);
    archive.Field("fillWidth", fillWidth, {.min = 0, .max = 2000, .tooltip = "The bar's full width"});
    archive.Field("lowShare", lowShare, {.min = 0.0f, .max = 1.0f, .speed = 0.01f});
    archive.Field("color", color);
    archive.Field("lowColor", lowColor);
}

bool DamageFlashElement::IsWanted(const HudContext& context) {
    const Health* health = HealthElement::GetHealth(context);
    if (!health) {
        m_strength = 0.0f;
        return false;
    }
    const bool low = !health->IsDead() && health->GetHealth() <= health->maxHealth * lowShare;
    // Hurts flash the edges of the screen; low health keeps them beating, slowly.
    const float beat = low ? 0.3f + 0.12f * std::sin(context.time * 5.0f) : 0.0f;
    m_strength = std::max(health->GetHurt() * 0.7f, beat);
    return m_strength > 0.01f;
}

void DamageFlashElement::Refresh(const HudContext& /*context*/) {
    if (auto* image = GetEntity().GetComponent<Spot::UiImage>()) {
        image->tint.a = std::clamp(m_strength, 0.0f, 1.0f);
    }
}

void DamageFlashElement::Serialize(Spot::Archive& archive) {
    HudElement::Serialize(archive);
    archive.Field("lowShare", lowShare, {.min = 0.0f, .max = 1.0f, .speed = 0.01f});
}

bool HitMarkerElement::IsWanted(const HudContext& context) {
    // Here rather than as it starts, which waits until it is first shown.
    if (!m_applied.IsConnected()) {
        m_applied = DamageSystem::Get(GetScene()).OnDamageApplied.Connect([this](const DamageApplied& applied) {
            const Spot::EntityId by = applied.damage.instigator;
            if (by != Spot::EntityId::Invalid && (by == m_pawn || by == m_occupant) &&
                (damageTypes.empty() || std::ranges::find(damageTypes, applied.damage.type) != damageTypes.end())) {
                m_strength = 1.0f;
            }
        });
    }
    m_pawn = context.pawn ? context.pawn->GetEntity().GetId() : Spot::EntityId::Invalid;
    m_occupant = context.occupant ? context.occupant->GetEntity().GetId() : Spot::EntityId::Invalid;
    m_strength = std::max(m_strength - context.deltaTime * Spot::Time::GetTimeScale() * fadeSpeed, 0.0f);
    return true;
}

void HitMarkerElement::Refresh(const HudContext& /*context*/) {
    if (auto* image = GetEntity().GetComponent<Spot::UiImage>()) {
        image->tint.a = std::clamp(m_strength, 0.0f, 1.0f);
    }
}

void HitMarkerElement::Serialize(Spot::Archive& archive) {
    HudElement::Serialize(archive);
    archive.Field("damageTypes", damageTypes, {.tooltip = "The types of damage that show it; any when empty"});
    archive.Field("fadeSpeed", fadeSpeed, {.min = 0.0f, .max = 100.0f, .speed = 0.1f});
}

} // namespace Gameplay
