#include "sandbox_hud.h"

#include "developer_mode.h"
#include "prop_spawner.h"

#include "gameplay/character/carrier.h"
#include "gameplay/core/pawn.h"
#include "gameplay/core/players.h"
#include "gameplay/hud/input_prompts.h"
#include "gameplay/network/player_info.h"

#include <spot/scene/physics/physics_world.h>

#include <algorithm>
#include <cmath>

SPOT_COMPONENT(SandboxPromptElement);

std::string SandboxPromptElement::GetPrompt(const Gameplay::HudContext& context) const {
    using Gameplay::InputHint;
    const Spot::Entity* pawn = context.pawn ? &context.pawn->GetEntity() : nullptr;
    const auto* spawner = pawn ? pawn->GetComponent<PropSpawner>() : nullptr;
    if (spawner && spawner->IsPlacing()) {
        const auto spawnables = spawner->GetSpawnables();
        const std::size_t index = *spawner->GetPlacingIndex();
        const InputHint hints[] = {
            {"Fire", "Place " + (index < spawnables.size() ? spawnables[index].name : std::string())},
            {"Aim", "Cancel"}};
        return Gameplay::InputPrompts::Format(hints);
    }
    if (const auto* carrier = pawn ? pawn->GetComponent<Gameplay::Carrier>() : nullptr;
        carrier && carrier->IsHolding()) {
        return Gameplay::InputPrompts::Format(carrier->GetHints());
    }
    return PromptElement::GetPrompt(context);
}

std::vector<PlayerLabel> GetPlayerLabels(Spot::Scene& scene, Spot::Vector2 viewSize) {
    std::vector<PlayerLabel> labels;
    const auto* camera = Spot::FindMainCamera(scene);
    const auto* viewer = Gameplay::Players::GetBody(scene);
    if (!camera || !viewer || viewSize.x <= 0 || viewSize.y <= 0) {
        return labels;
    }
    const auto view = camera->GetCamera3D();
    const auto projection = view.GetViewProjection(viewSize);
    auto* physics = scene.GetSystem<Spot::PhysicsWorld>();
    for (const auto* info : Gameplay::PlayerInfos::GetAll(scene)) {
        auto* entity = scene.GetEntity(info->pawn);
        const auto* pawn = entity ? entity->GetComponent<Gameplay::Pawn>() : nullptr;
        if (!pawn || info->IsLocal() || !pawn->IsActiveAndEnabled() || pawn->HasState("Dead")) {
            continue;
        }
        const float distance =
            Spot::Distance(viewer->GetTransform().GetWorldPosition(), entity->GetTransform().GetWorldPosition());
        if (distance >= 20.0f) {
            continue;
        }
        const auto eye = pawn->GetViewPoint().position;
        const auto toEye = eye - view.position;
        const float sightDistance = Spot::Length(toEye);
        if (physics && sightDistance > 0.01f) {
            const auto hit = physics->Raycast(view.position, toEye / sightDistance, sightDistance,
                                              {.ignore = viewer->GetEntity().GetId()});
            const auto* hitEntity = hit ? scene.GetEntity(hit->entity) : nullptr;
            if (hitEntity && Gameplay::Pawn::Of(*hitEntity) != pawn) {
                continue;
            }
        }
        const auto* mode = entity->GetComponent<DeveloperMode>();
        const float headroom = mode && mode->IsUnlocked() ? 0.55f : 0.35f;
        const auto head = eye + Spot::Vector3{0, headroom, 0};
        const auto clip = projection * Spot::Vector4{head.x, head.y, head.z, 1};
        if (clip.w <= 0 || clip.z < 0 || clip.z > clip.w || std::abs(clip.x) > clip.w || std::abs(clip.y) > clip.w) {
            continue;
        }
        labels.push_back({info->name,
                          {(clip.x / clip.w + 1) * viewSize.x * 0.5f, (clip.y / clip.w + 1) * viewSize.y * 0.5f},
                          std::clamp((20.0f - distance) / 4.0f, 0.0f, 1.0f),
                          mode ? mode->GetImmortalTime() : 0.0f,
                          mode && mode->IsAuthenticated()});
    }
    return labels;
}

namespace {

void DrawDeveloperBadge(Spot::Renderer2D& renderer, Spot::Vector2 center, float opacity) {
    constexpr float Height = 22.0f;
    constexpr float HalfWidth = 10.0f;
    // Scanlines keep the shield's tapered silhouette and blue gradient within the public quad renderer.
    for (int row = 0; row < 22; ++row) {
        const float y = static_cast<float>(row) + 0.5f;
        const float taper = y < 2.0f ? 0.85f + y * 0.075f : y < 11.0f ? 1.0f : std::pow((Height - y) / 11.0f, 0.65f);
        const float half = HalfWidth * taper;
        const auto position = center + Spot::Vector2{0, Height * 0.5f - y};
        const float shine = 1.0f - y / Height;
        renderer.DrawQuad(position + Spot::Vector2{0, -1.2f}, {half * 2 + 2, 1},
                          {0.005f, 0.025f, 0.08f, opacity * 0.7f});
        renderer.DrawQuad(position, {half * 2, 1}, {0.08f + shine * 0.15f, 0.38f + shine * 0.4f, 1, opacity});
        if (row >= 2 && row < 20 && half > 1.5f) {
            renderer.DrawQuad(position, {(half - 1.5f) * 2, 1},
                              {0.015f + shine * 0.02f, 0.08f + shine * 0.16f, 0.25f + shine * 0.28f, opacity});
        }
    }
    const auto stroke = [&](Spot::Vector2 from, Spot::Vector2 to) {
        const auto delta = to - from;
        renderer.DrawRotatedQuad(center + (from + to) * 0.5f + Spot::Vector2{0, 0.8f}, {Spot::Length(delta), 1.4f},
                                 std::atan2(delta.y, delta.x), {0.85f, 0.97f, 1, opacity});
    };
    stroke({-3.8f, 3.0f}, {-6.8f, 0.0f});
    stroke({-6.8f, 0.0f}, {-3.8f, -3.0f});
    stroke({3.8f, 3.0f}, {6.8f, 0.0f});
    stroke({6.8f, 0.0f}, {3.8f, -3.0f});
    stroke({-1.5f, -3.8f}, {1.5f, 3.8f});
    renderer.DrawQuad(center + Spot::Vector2{0, 8.5f}, {9.5f, 1}, {0.5f, 0.85f, 1, opacity * 0.7f});
}

} // namespace

void DrawPlayerLabels(Spot::Renderer2D& renderer, Spot::Scene& scene, Spot::Vector2 viewSize) {
    const auto* hud = scene.FindComponent<Gameplay::Hud>();
    const auto* font = hud ? hud->font.Get() : nullptr;
    if (!font) {
        return;
    }
    const float scale = 18.0f / font->GetPixelSize();
    for (const auto& label : GetPlayerLabels(scene, viewSize)) {
        const auto size = font->MeasureText(label.name) * scale;
        const auto position = label.position - Spot::Vector2{size.x * 0.5f, 0};
        renderer.DrawString(*font, label.name, position + Spot::Vector2{1, -1}, {0, 0, 0, label.opacity}, scale);
        renderer.DrawString(*font, label.name, position, {1, 1, 1, label.opacity}, scale);
        if (label.developerAuthenticated) {
            DrawDeveloperBadge(renderer, position + Spot::Vector2{-17.0f, -6.5f}, label.opacity);
        }
        if (label.immortalTime <= 0) {
            continue;
        }
        constexpr std::string_view Title = "Immortal Object";
        const auto titleSize = font->MeasureText(Title) * scale;
        const Spot::Vector2 center = label.position + Spot::Vector2{0, 25};
        constexpr float Height = 30.0f;
        const float width = titleSize.x + 24.0f;
        // Two diamonds and a rectangle form a stretched hexagon with opaque, seamless joins.
        const Spot::Color purple{0.48f, 0.12f, 0.78f, 1};
        const Spot::Vector2 diamond{Height / std::sqrt(2.0f), Height / std::sqrt(2.0f)};
        renderer.DrawRotatedQuad(center - Spot::Vector2{width * 0.5f, 0}, diamond, Spot::ToRadians(45.0f), purple);
        renderer.DrawRotatedQuad(center + Spot::Vector2{width * 0.5f, 0}, diamond, Spot::ToRadians(45.0f), purple);
        renderer.DrawQuad(center, {width, Height}, purple);
        renderer.DrawString(*font, Title, center + Spot::Vector2{-titleSize.x * 0.5f, titleSize.y * 0.5f},
                            Spot::Color::White, scale);
    }
}
