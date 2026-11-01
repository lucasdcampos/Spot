#include "../playground_fixture.h"

#include "developer_mode.h"
#include "gameplay/character/character_visual.h"
#include "gameplay/core/players.h"
#include "gameplay/inventory/view_model.h"
#include "player_appearance.h"

#include <array>
#include <memory>
#include <string_view>

namespace {

Spot::SkinnedMeshRenderer& Part(Spot::Entity& player, std::string_view name) {
    const auto* visual = player.GetComponent<Gameplay::CharacterVisual>();
    REQUIRE(visual);
    auto* body = player.GetScene().GetEntity(visual->modelRoot);
    REQUIRE(body);
    auto* part = body->FindChild(name);
    REQUIRE(part);
    auto* renderer = part->GetComponent<Spot::SkinnedMeshRenderer>();
    REQUIRE(renderer);
    REQUIRE(renderer->GetMaterial());
    return *renderer;
}

void CheckArmsSkin(Spot::Entity& player) {
    const auto* view = player.GetComponent<Gameplay::ViewModel>();
    REQUIRE(view);
    auto& skin = Part(player, "Skin");
    CHECK(view->armsMaterialSource.Get(player.GetScene()) == &skin);
    REQUIRE(view->GetArmEntities().size() == 4);
    for (const auto id : view->GetArmEntities()) {
        const auto* arm = player.GetScene().GetEntity(id);
        REQUIRE(arm);
        const auto* renderer = arm->GetComponent<Spot::MeshRenderer>();
        REQUIRE(renderer);
        CHECK(renderer->GetMaterial() == skin.GetMaterial());
    }
}

} // namespace

TEST_CASE("Player colors vary per character, survive checkpoints and leave shared assets and developer skins intact",
          "[sandbox][appearance]") {
    SandboxTests::Playground game;
    auto* player = Gameplay::Players::FindComponent<PlayerAppearance>(game.scene);
    REQUIRE(player);
    REQUIRE(player->GetSeed() != 0);
    auto& entity = player->GetEntity();
    constexpr std::array<std::string_view, 5> Parts = {"Shirt", "Trousers", "Skin", "Hair", "Details"};
    std::array<Spot::Color, Parts.size()> colors;
    std::array<Spot::AssetRef<Spot::Material>, Parts.size()> materials;
    for (std::size_t i = 0; i < Parts.size(); ++i) {
        const auto& material = Part(entity, Parts[i]).GetMaterial();
        colors[i] = material->baseColor;
        materials[i] = material;
        auto source = game.assets.Load<Spot::Material>(material.GetId());
        REQUIRE(source);
        if (i < 4) {
            CHECK(material.Get() != source.Get());
        } else {
            CHECK(material.Get() == source.Get());
        }
    }
    auto* other = game.scene.Instantiate(game.assets.Load<Spot::Prefab>("prefabs/actors/player.prefab"));
    REQUIRE(other);
    game.Run(3);
    CHECK(Part(*other, "Shirt").GetMaterial()->baseColor != colors[0]);
    for (std::size_t i = 0; i < Parts.size(); ++i) {
        CHECK(Part(entity, Parts[i]).GetMaterial() == materials[i]);
        CHECK(Part(entity, Parts[i]).GetMaterial()->baseColor == colors[i]);
    }
    const auto* view = entity.GetComponent<Gameplay::ViewModel>();
    REQUIRE(view);
    REQUIRE_FALSE(view->GetArmEntities().empty());
    for (const auto id : view->GetArmEntities()) {
        const auto* arm = game.scene.GetEntity(id);
        REQUIRE(arm);
        const auto* renderer = arm->GetComponent<Spot::MeshRenderer>();
        REQUIRE(renderer);
        REQUIRE(renderer->GetMaterial());
        CHECK(renderer->GetMaterial()->baseColor == colors[2]);
    }

    const auto seed = player->GetSeed();
    Spot::Scene restored;
    REQUIRE(restored.LoadFromString(game.scene.SaveToString(), &game.assets));
    restored.Update(0);
    Spot::Entity* loaded = nullptr;
    restored.ForEachComponent<PlayerAppearance>([&](PlayerAppearance& appearance) {
        if (appearance.GetSeed() == seed) {
            loaded = &appearance.GetEntity();
        }
    });
    REQUIRE(loaded);
    REQUIRE(loaded->GetComponent<PlayerAppearance>());
    CHECK(loaded->GetComponent<PlayerAppearance>()->GetSeed() == seed);
    for (std::size_t i = 0; i < Parts.size(); ++i) {
        CHECK(Part(*loaded, Parts[i]).GetMaterial()->baseColor == colors[i]);
    }
    CheckArmsSkin(*loaded);

    auto* developer = entity.GetComponent<DeveloperMode>();
    REQUIRE(developer);
    REQUIRE(developer->skin);
    developer->Unlock();
    game.Run(3);
    const auto* visual = entity.GetComponent<Gameplay::CharacterVisual>();
    auto* body = game.scene.GetEntity(visual->modelRoot);
    for (const auto& part : developer->skin->GetParts()) {
        REQUIRE(body->FindChild(part.name));
        CHECK(Part(entity, part.name).GetMaterial() == part.material);
    }
    CheckArmsSkin(entity);
}

TEST_CASE("First-person skin matches the body immediately after skin changes, weapon switches and re-enabling",
          "[sandbox][appearance][arms]") {
    SandboxTests::Playground game;
    auto* appearance = Gameplay::Players::FindComponent<PlayerAppearance>(game.scene);
    REQUIRE(appearance);
    auto& player = appearance->GetEntity();
    auto* inventory = player.GetComponent<Gameplay::Inventory>();
    auto* view = player.GetComponent<Gameplay::ViewModel>();
    REQUIRE(inventory);
    REQUIRE(view);
    CheckArmsSkin(player);
    appearance->SetEnabled(false);

    auto& skin = Part(player, "Skin");
    for (const auto color : {Spot::Color{0.075f, 0.032f, 0.018f, 1}, Spot::Color{0.92f, 0.72f, 0.56f, 1}}) {
        auto material = std::make_shared<Spot::Material>(*skin.GetMaterial());
        material->baseColor = color;
        skin.SetMaterial(material);
        game.Step(0);
        CheckArmsSkin(player);
        const auto items = inventory->GetItems();
        REQUIRE(items.size() >= 6);
        for (auto* item : items) {
            CAPTURE(item->displayName);
            inventory->Select(*item);
            game.Step(0);
            CheckArmsSkin(player);
        }
        view->SetEnabled(false);
        CHECK(view->GetArmEntities().empty());
        view->SetEnabled(true);
        game.Step(0);
        CheckArmsSkin(player);
    }
    auto* developer = player.GetComponent<DeveloperMode>();
    REQUIRE(developer);
    developer->Unlock();
    game.Step(0);
    CheckArmsSkin(player);
}
