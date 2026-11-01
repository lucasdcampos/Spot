#include "gameplay/health/health_elements.h"

#include "gameplay/core/pawn.h"
#include "gameplay/health/damage.h"
#include "gameplay/health/health.h"
#include "gameplay/inventory/held_item.h"
#include "gameplay/network/player_info.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace Gameplay {

SPOT_COMPONENT(HealthElement);
SPOT_COMPONENT(DamageFlashElement);
SPOT_COMPONENT(HitMarkerElement);
SPOT_COMPONENT(KillFeedElement);
SPOT_COMPONENT(KillFeedSubject);
SPOT_COMPONENT(KillFeedRow);

namespace {

// Bars ease toward their value in about 120 milliseconds: three of these.
constexpr float StateTime = 0.04f;

} // namespace

Health* HealthElement::GetHealth(const HudContext& context) {
    // In a vehicle the player's own health shows, not the vehicle's.
    if (context.occupant) {
        if (auto* health = context.occupant->GetEntity().GetComponent<Health>()) {
            return health;
        }
    }
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

KillFeedElement::Entry KillFeedElement::Describe(Spot::Scene& scene, const DamageApplied& death) {
    const auto* target = scene.GetEntity(death.target);
    const auto* pawn = target ? Pawn::Of(*target) : nullptr;
    if (!pawn || !pawn->GetEntity().HasComponent<KillFeedSubject>()) {
        return {};
    }
    // Whose pawn did it and what with: the weapon, or the kind of damage when it came straight from them.
    const auto nameOf = [&scene](Spot::EntityId id) -> std::string {
        const Spot::Entity* entity = scene.GetEntity(id);
        if (!entity) {
            return {};
        }
        const Pawn* pawn = Pawn::Of(*entity);
        const Spot::EntityId pawnId = pawn ? pawn->GetEntity().GetId() : id;
        for (const PlayerInfo* info : PlayerInfos::GetAll(scene)) {
            if (info->pawn == pawnId) {
                return info->name;
            }
        }
        return pawn ? pawn->GetEntity().GetName() : entity->GetName();
    };
    const std::string victim = nameOf(death.target);
    const auto* by = scene.GetEntity(death.damage.instigator);
    const Pawn* killerPawn = by ? Pawn::Of(*by) : nullptr;
    if (killerPawn && killerPawn->GetOccupant()) {
        killerPawn = killerPawn->GetOccupant();
    }
    const std::string killer = killerPawn && killerPawn->GetEntity().HasComponent<KillFeedSubject>()
                                   ? nameOf(killerPawn->GetEntity().GetId())
                                   : std::string{};
    const Spot::Entity* source = scene.GetEntity(death.damage.source);
    const auto* item = source ? source->GetComponent<HeldItem>() : nullptr;
    const auto icon = item ? item->icon : Spot::AssetRef<Spot::Texture>{};
    return {killerPawn == pawn ? std::string{} : killer, victim, icon};
}

void KillFeedRow::DrawWidget(Spot::Renderer2D& renderer, const Spot::UiLayoutEntry& entry) {
    if (!font || victim.empty() || opacity <= 0.0f) {
        return;
    }
    const float pixel = entry.scale;
    const float factor = fontSize / font->GetPixelSize() * pixel;
    // Keep a long nickname from covering the game or pushing the other name off screen.
    const auto shorten = [&](std::string text, float width) {
        while (!text.empty() && font->MeasureText(text).x * factor > width) {
            // Remove an entire UTF-8 code point, not just its last byte.
            std::size_t last = text.size() - 1;
            while (last > 0 && (static_cast<unsigned char>(text[last]) & 0xc0) == 0x80) {
                --last;
            }
            text.resize(last);
        }
        return text;
    };
    const float iconWidth = 48.0f * pixel;
    const float padding = 12.0f * pixel;
    const float nameWidth = std::max(0.0f, (entry.bounds.GetSize().x - iconWidth - padding * 4) * 0.5f);
    const auto killerText = shorten(killer, nameWidth);
    const auto victimText = shorten(victim, nameWidth);
    const auto killerSize = font->MeasureText(killerText) * factor;
    const auto victimSize = font->MeasureText(victimText) * factor;
    const float width = killerSize.x + victimSize.x + iconWidth + padding * (killerText.empty() ? 3 : 4);
    const float height = entry.bounds.GetSize().y;
    const auto right = entry.bounds.max;
    const Spot::Vector2 center{right.x - width * 0.5f, right.y - height * 0.5f};
    renderer.DrawQuad(center, {width, height}, {0.008f, 0.012f, 0.022f, opacity * 0.88f});
    renderer.DrawQuad({right.x - width + pixel, center.y}, {2 * pixel, height},
                      {killerColor.r, killerColor.g, killerColor.b, opacity});
    const auto drawName = [&](std::string_view text, Spot::Vector2 size, float x, Spot::Color color) {
        const Spot::Vector2 position{std::round(x), std::round(center.y + size.y * 0.5f)};
        for (const auto offset :
             {Spot::Vector2{-pixel, 0}, Spot::Vector2{pixel, 0}, Spot::Vector2{0, -pixel}, Spot::Vector2{0, pixel}}) {
            renderer.DrawString(*font, text, position + offset, {0, 0, 0, opacity}, factor);
        }
        color.a *= opacity;
        renderer.DrawString(*font, text, position, color, factor);
    };
    float x = right.x - width + padding;
    if (!killerText.empty()) {
        drawName(killerText, killerSize, x, killerColor);
        x += killerSize.x + padding;
    }
    if (icon) {
        const float aspect = static_cast<float>(icon->GetWidth()) / static_cast<float>(std::max(icon->GetHeight(), 1u));
        Spot::Vector2 size{iconWidth, iconWidth / aspect};
        const float fit = std::min(1.0f, (height - 12 * pixel) / size.y);
        size = size * fit;
        renderer.DrawQuad({x + iconWidth * 0.5f, center.y}, size, *icon, {1, 1, 1, opacity});
    }
    drawName(victimText, victimSize, x + iconWidth + padding, victimColor);
}

void KillFeedRow::Serialize(Spot::Archive& archive) {
    SerializeWidget(archive);
    archive.Field("font", font);
    archive.Field("fontSize", fontSize, {.min = 1, .max = 200});
}

void KillFeedElement::CauseIcon::Serialize(Spot::Archive& archive) {
    archive.Field("type", type);
    archive.Field("icon", icon);
}

bool KillFeedElement::IsWanted(const HudContext& context) {
    if (!m_killed.IsConnected()) {
        m_killed = DamageSystem::Get(GetScene()).OnKilled.Connect([this](const DamageApplied& death) {
            Entry entry = Describe(GetScene(), death);
            if (entry.victim.empty()) {
                return;
            }
            if (!entry.icon.GetId().IsValid()) {
                const auto cause = std::ranges::find(causeIcons, death.damage.type, &CauseIcon::type);
                entry.icon = cause != causeIcons.end() ? cause->icon : fallbackIcon;
            }
            const auto involved = [&](Spot::EntityId id) {
                return id != Spot::EntityId::Invalid && (id == m_pawn || id == m_occupant);
            };
            const Spot::Entity* by = GetScene().GetEntity(death.damage.instigator);
            const Pawn* killer = by ? Pawn::Of(*by) : nullptr;
            m_lines.insert(m_lines.begin(), {std::move(entry), 0.0f, killer && involved(killer->GetEntity().GetId()),
                                             involved(death.target)});
            if (m_lines.size() > rows.size()) {
                m_lines.resize(rows.size());
            }
        });
    }
    m_pawn = context.pawn ? context.pawn->GetEntity().GetId() : Spot::EntityId::Invalid;
    m_occupant = context.occupant ? context.occupant->GetEntity().GetId() : Spot::EntityId::Invalid;
    for (Line& line : m_lines) {
        line.age += context.deltaTime;
    }
    std::erase_if(m_lines, [this](const Line& line) { return line.age >= lifetime; });
    return true;
}

void KillFeedElement::Refresh(const HudContext& /*context*/) {
    for (std::size_t index = 0; index < rows.size(); ++index) {
        auto* row = Get<KillFeedRow>(rows[index]);
        if (!row) {
            continue;
        }
        if (index >= m_lines.size()) {
            row->killer.clear();
            row->victim.clear();
            row->icon = {};
            row->opacity = 0;
            continue;
        }
        const Line& line = m_lines[index];
        row->killer = line.entry.killer;
        row->victim = line.entry.victim;
        row->icon = line.entry.icon;
        row->killerColor = line.myKill ? accent : color;
        row->victimColor = line.myDeath ? accent : victimColor;
        row->opacity = std::clamp(lifetime - line.age, 0.0f, 1.0f);
    }
}

void KillFeedElement::Serialize(Spot::Archive& archive) {
    HudElement::Serialize(archive);
    archive.Field("rows", rows, {.tooltip = "The KillFeedRows of its lines, newest at the first"});
    archive.Field("lifetime", lifetime, {.min = 0.5f, .max = 60.0f, .speed = 0.1f, .tooltip = "Seconds a line stays"});
    archive.Field("color", color);
    archive.Field("victimColor", victimColor);
    archive.Field("accent", accent, {.tooltip = "For the local player's name"});
    archive.Field("causeIcons", causeIcons, {.tooltip = "Icons for damage without a held item source"});
    archive.Field("fallbackIcon", fallbackIcon);
}

} // namespace Gameplay
