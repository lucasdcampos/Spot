#include "player_appearance.h"

#include "developer_mode.h"
#include "gameplay/character/character_visual.h"

#include <array>
#include <memory>
#include <random>
#include <string_view>

SPOT_COMPONENT(PlayerAppearance);

namespace {

constexpr std::array<Spot::Color, 8> SkinTones = {{{0.92f, 0.72f, 0.56f, 1},
                                                   {0.78f, 0.53f, 0.36f, 1},
                                                   {0.68f, 0.44f, 0.29f, 1},
                                                   {0.52f, 0.30f, 0.17f, 1},
                                                   {0.38f, 0.20f, 0.10f, 1},
                                                   {0.25f, 0.12f, 0.06f, 1},
                                                   {0.14f, 0.065f, 0.035f, 1},
                                                   {0.075f, 0.032f, 0.018f, 1}}};
constexpr std::array<Spot::Color, 8> HairTones = {{{0.012f, 0.009f, 0.008f, 1},
                                                   {0.035f, 0.020f, 0.012f, 1},
                                                   {0.11f, 0.052f, 0.025f, 1},
                                                   {0.25f, 0.13f, 0.045f, 1},
                                                   {0.62f, 0.43f, 0.16f, 1},
                                                   {0.32f, 0.075f, 0.025f, 1},
                                                   {0.30f, 0.28f, 0.26f, 1},
                                                   {0.75f, 0.72f, 0.67f, 1}}};

} // namespace

void PlayerAppearance::Apply() {
    if (m_seed == 0 || m_seed == m_appliedSeed) {
        return;
    }
    if (const auto* developer = GetEntity().GetComponent<DeveloperMode>(); developer && developer->IsUnlocked()) {
        return;
    }
    const auto* visual = GetEntity().GetComponent<Gameplay::CharacterVisual>();
    auto* body = visual ? GetScene().GetEntity(visual->modelRoot) : nullptr;
    if (!body) {
        return;
    }
    std::mt19937 random(m_seed);
    const auto clothing = [&](float minimum, float range) -> Spot::Color {
        const auto channel = [&]() { return minimum + range * static_cast<float>(random() >> 8) / 16777215.0f; };
        return {channel(), channel(), channel(), 1};
    };
    const std::array colors = {clothing(0.04f, 0.72f), clothing(0.025f, 0.22f), SkinTones[random() % SkinTones.size()],
                               HairTones[random() % HairTones.size()]};
    constexpr std::array<std::string_view, 4> Parts = {"Shirt", "Trousers", "Skin", "Hair"};
    for (std::size_t i = 0; i < Parts.size(); ++i) {
        auto* part = body->FindChild(Parts[i]);
        auto* renderer = part ? part->GetComponent<Spot::SkinnedMeshRenderer>() : nullptr;
        if (!renderer || !renderer->GetMaterial()) {
            return;
        }
        const auto& original = renderer->GetMaterial();
        auto material = std::make_shared<Spot::Material>(*original);
        material->baseColor = colors[i];
        // Keep the source id for checkpoints; the saved seed restores this instance's tint on load.
        renderer->SetMaterial({original.GetId(), std::move(material)});
    }
    m_appliedSeed = m_seed;
}

void PlayerAppearance::OnUpdate(float /*deltaTime*/) {
    if (m_seed == 0 && Spot::Network::IsServer(GetScene())) {
        std::random_device random;
        do {
            m_seed = random();
        } while (m_seed == 0);
    }
    Apply();
}

void PlayerAppearance::Serialize(Spot::Archive& archive) {
    archive.Field("seed", m_seed, {.hidden = true});
}

void PlayerAppearance::Replicate(Spot::Archive& archive) {
    archive.Field("seed", m_seed);
    if (archive.IsLoading()) {
        Apply();
    }
}
