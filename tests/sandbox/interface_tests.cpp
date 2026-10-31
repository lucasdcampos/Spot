#include "../registered_assets.h"
#include "gameplay/core/players.h"
#include "interface_lookup.h"
#include "log_capture.h"
#include "main_menu.h"
#include "npc_spawner.h"
#include "playground_fixture.h"
#include "prop_spawner.h"
#include "spawnable.h"
#include "temp_directory.h"
#include "undo_list.h"

#include "gameplay/camera/first_person_view.h"
#include "gameplay/game/game_catalog.h"
#include "gameplay/game/game_mode.h"
#include "gameplay/game/map_info.h"
#include "gameplay/game/session_controls.h"
#include "gameplay/game/spawn_point.h"
#include "gameplay/inventory/inventory.h"
#include "gameplay/weapons/firearm.h"
#include <spot/scene/ui/ui_input_system.h>
#include <spot/scene/ui/ui_system.h>

#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace {
struct InterfaceGame {
    std::shared_ptr<Spot::Tests::CaptureSink> log = std::make_shared<Spot::Tests::CaptureSink>();
    Spot::Tests::ScopedSinks sinks{{log}};
    Spot::InputState input;
    Spot::InputActionMap actions;
    Spot::Tests::RegisteredAssetManager assets{SANDBOX_IMPORTED_DIR};
    Spot::InputState* previousInput = Spot::Input::GetState();
    Spot::InputActionMap* previousActions = Spot::Input::GetActionMap();
    Spot::AssetManager* previousAssets = Spot::Assets::GetManager();
    float previousScale = Spot::Time::GetTimeScale();
    Spot::Scene scene;
    explicit InterfaceGame(std::string_view scenePath = "scenes/playground.scene") {
        Spot::Time::SetTimeScale(1);
        Spot::Detail::LoadGameCode();
        Spot::Input::SetState(&input);
        Spot::Input::SetActionMap(&actions);
        Spot::Detail::SetAssetManager(&assets);
        const auto game =
            Spot::GameSettings::LoadFromFile(std::filesystem::path(SANDBOX_IMPORTED_DIR).parent_path() / "game.json");
        REQUIRE(game);
        game->sections.input.Apply(actions);
        actions.SetDefaults();
        const auto snapshot = Spot::Process::GetEnvironmentValue("SPOT_SANDBOX_TEST_ASSETS");
        const auto path = snapshot && !snapshot->empty() ? std::filesystem::path(*snapshot)
                                                         : std::filesystem::path(SANDBOX_ASSETS_DIR);
        REQUIRE(scene.LoadFromFile(path / scenePath, &assets));
        Step();
        Step();
        Step();
    }
    ~InterfaceGame() {
        scene.Clear();
        Spot::Time::SetTimeScale(previousScale);
        Spot::Input::SetState(previousInput);
        Spot::Input::SetActionMap(previousActions);
        Spot::Detail::SetAssetManager(previousAssets);
        for (const auto& record : log->GetRecords())
            CHECK(record.message.find(" threw in ") == std::string::npos);
    }
    SandboxTests::Interface GetUi() { return {scene}; }
    void Step() {
        actions.Update(input);
        scene.Update(1.0f / 60 * Spot::Time::GetTimeScale());
        input.BeginFrame();
    }
    bool Send(const Spot::Event& event) {
        const bool used = scene.GetOrAddSystem<Spot::UiInputSystem>().HandleUiEvent(event, {1280, 720}, &input);
        if (!used)
            input.OnEvent(event);
        return used;
    }
};
} // namespace

TEST_CASE("Sandbox main menu selects maps and navigates pages with keyboard mouse and gamepad", "[sandbox][ui][maps]") {
    InterfaceGame game("scenes/main_menu.scene");
    auto* menu = game.scene.FindComponent<MainMenu>();
    REQUIRE(menu);
    const auto settings =
        Spot::GameSettings::LoadFromFile(std::filesystem::path(SANDBOX_IMPORTED_DIR).parent_path() / "game.json");
    REQUIRE(settings);
    CHECK(settings->startupScene == game.assets.FindByPath("scenes/main_menu.scene").value());
    REQUIRE(menu->maps.size() == 2);
    CHECK(menu->maps[0].scene == game.assets.FindByPath("scenes/playground.scene").value());
    CHECK(menu->maps[1].scene == game.assets.FindByPath("scenes/empty.scene").value());
    CHECK_FALSE(Spot::Input::IsCursorLocked());
    CHECK(menu->IsOpen());
    CHECK(menu->GetPage() == MainMenu::Page::Home);
    auto& input = game.scene.GetOrAddSystem<Spot::UiInputSystem>();
    const auto click = [&](Spot::EntityId id) { game.scene.GetEntity(id)->GetComponent<Spot::UiButton>()->Click(); };
    CHECK(input.GetFocusedEntity() == menu->playButton);
    CHECK(game.Send(Spot::KeyPressedEvent{Spot::Key::Enter, false}));
    CHECK(game.Send(Spot::KeyReleasedEvent{Spot::Key::Enter}));
    CHECK(menu->GetPage() == MainMenu::Page::Play);
    CHECK(input.GetFocusedEntity() == menu->maps[0].button);
    CHECK_FALSE(game.scene.GetEntity(menu->homePanel)->IsActiveInHierarchy());
    CHECK(game.Send(Spot::KeyPressedEvent{Spot::Key::Down, false}));
    CHECK(game.Send(Spot::KeyReleasedEvent{Spot::Key::Down}));
    CHECK(input.GetFocusedEntity() == menu->maps[1].button);
    CHECK(game.Send(Spot::KeyPressedEvent{Spot::Key::Enter, false}));
    CHECK(game.Send(Spot::KeyReleasedEvent{Spot::Key::Enter}));
    CHECK(menu->GetSelectedMap() == 1);
    CHECK(game.scene.GetEntity(menu->mapTitle)->GetComponent<Spot::UiText>()->text == "Empty Map");
    // Selecting a map changes its details, and starting it is a separate action.
    CHECK(menu->IsOpen());
    CHECK(game.scene.GetEntity(menu->playPanel)->IsActiveInHierarchy());
    CHECK(game.Send(Spot::KeyPressedEvent{Spot::Key::Escape, false}));
    CHECK(game.Send(Spot::KeyReleasedEvent{Spot::Key::Escape}));
    CHECK(menu->GetPage() == MainMenu::Page::Home);
    CHECK(input.GetModalCanvas() == menu->canvas);
    click(menu->playButton);
    CHECK(input.GetFocusedEntity() == menu->maps[1].button);
    game.Send(Spot::GamepadButtonPressedEvent{Spot::GamepadId{0}, Spot::GamepadButton::DPadUp});
    CHECK(input.GetFocusedEntity() == menu->maps[0].button);
    game.Send(Spot::GamepadButtonPressedEvent{Spot::GamepadId{0}, Spot::GamepadButton::South});
    CHECK(menu->GetSelectedMap() == 0);
    const auto* entry = game.scene.GetOrAddSystem<Spot::UiSystem>().Find(menu->maps[1].button);
    REQUIRE(entry);
    const auto center = entry->bounds.GetCenter();
    game.Send(Spot::MouseMovedEvent{{center.x, 720 - center.y}, {}});
    CHECK(game.Send(Spot::MouseButtonPressedEvent{Spot::MouseButton::Left}));
    CHECK(game.Send(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Left}));
    CHECK(menu->GetSelectedMap() == 1);
    menu->SelectMap(menu->maps.size());
    CHECK(menu->GetSelectedMap() == 1);
    click(menu->backButton);
    click(menu->quitButton);
    CHECK(menu->GetPage() == MainMenu::Page::Quit);
    CHECK(input.GetFocusedEntity() == menu->cancelQuitButton);
    game.Send(Spot::GamepadButtonPressedEvent{Spot::GamepadId{0}, Spot::GamepadButton::East});
    CHECK(menu->GetPage() == MainMenu::Page::Home);
    CHECK(menu->IsOpen());
    CHECK(input.GetFocusedEntity() == menu->quitButton);
    CHECK(Spot::Time::GetTimeScale() == 1.0f);
}

TEST_CASE("Sandbox room pages expose editable settings and support keyboard cancellation", "[sandbox][ui][network]") {
    InterfaceGame game("scenes/main_menu.scene");
    auto* menu = game.scene.FindComponent<MainMenu>();
    REQUIRE(menu);
    const auto click = [&](Spot::EntityId id) { game.scene.GetEntity(id)->GetComponent<Spot::UiButton>()->Click(); };
    click(menu->hostButton);
    CHECK(menu->GetPage() == MainMenu::Page::Host);
    CHECK(game.scene.GetEntity(menu->hostPanel)->IsActiveInHierarchy());
    CHECK(game.scene.GetEntity(menu->playPanel)->IsActiveInHierarchy());
    for (const auto id :
         {menu->hostName, menu->hostPlayerName, menu->hostPort, menu->hostPlayers, menu->hostPassword}) {
        const auto* entity = game.scene.GetEntity(id);
        REQUIRE(entity);
        CHECK(entity->IsActiveInHierarchy());
        REQUIRE(entity->GetComponent<Spot::UiInputField>());
    }
    game.Send(Spot::KeyPressedEvent{Spot::Key::Escape, false});
    game.Send(Spot::KeyReleasedEvent{Spot::Key::Escape});
    CHECK(menu->GetPage() == MainMenu::Page::Home);
    click(menu->joinButton);
    CHECK(menu->GetPage() == MainMenu::Page::Join);
    CHECK(game.scene.GetEntity(menu->joinPanel)->IsActiveInHierarchy());
    auto* address = game.scene.GetEntity(menu->joinAddress)->GetComponent<Spot::UiInputField>();
    REQUIRE(address);
    CHECK(address->GetText() == "127.0.0.1:27015");
    CHECK(game.scene.GetOrAddSystem<Spot::UiInputSystem>().GetFocusedEntity() == menu->joinAddress);
    click(menu->joinBack);
    CHECK(menu->GetPage() == MainMenu::Page::Home);
    menu->ShowPage(MainMenu::Page::Connecting);
    CHECK(game.scene.GetEntity(menu->connectingPanel)->IsActiveInHierarchy());
    CHECK(game.scene.GetOrAddSystem<Spot::UiInputSystem>().GetFocusedEntity() == menu->cancelConnection);
    click(menu->cancelConnection);
    CHECK(menu->GetPage() == MainMenu::Page::Home);
}

TEST_CASE("Main menu settings save and rebinding Escape stays in settings", "[sandbox][ui][maps][settings]") {
    InterfaceGame game("scenes/main_menu.scene");
    auto* menu = game.scene.FindComponent<MainMenu>();
    REQUIRE(menu);
    auto* settings = menu->GetEntity().GetComponent<Gameplay::SettingsMenu>();
    auto* bindings = menu->GetEntity().GetComponent<Gameplay::BindingsMenu>();
    REQUIRE(settings);
    REQUIRE(bindings);
    TempDirectory directory;
    settings->SetSettingsDirectory(directory.GetPath());
    game.scene.GetEntity(menu->settingsButton)->GetComponent<Spot::UiButton>()->Click();
    CHECK(menu->GetPage() == MainMenu::Page::Settings);
    auto* slider = game.scene.GetEntity(settings->fieldOfViewSlider)->GetComponent<Spot::UiSlider>();
    REQUIRE(slider);
    slider->SetValue(92.0f, true);
    CHECK(settings->GetSettings().fieldOfView == 92.0f);
    REQUIRE_FALSE(bindings->GetRows().empty());
    game.scene.GetEntity(bindings->GetRows().front())->GetComponent<Spot::UiButton>()->Click();
    CHECK(bindings->IsRebinding());
    game.Send(Spot::KeyPressedEvent{Spot::Key::Escape, false});
    game.Send(Spot::KeyReleasedEvent{Spot::Key::Escape});
    CHECK_FALSE(bindings->IsRebinding());
    CHECK(menu->GetPage() == MainMenu::Page::Settings);
    game.scene.GetEntity(menu->settingsBackButton)->GetComponent<Spot::UiButton>()->Click();
    CHECK(menu->GetPage() == MainMenu::Page::Home);
    Gameplay::UserSettings saved;
    REQUIRE(saved.Load(directory.GetPath()));
    CHECK(saved.fieldOfView == 92.0f);
    CHECK(game.scene.GetOrAddSystem<Spot::UiInputSystem>().GetFocusedEntity() == menu->settingsButton);
}

TEST_CASE("Sandbox empty map has a collidable debug floor and working player tools", "[sandbox][ui][maps]") {
    InterfaceGame game("scenes/empty.scene");
    auto* floor = game.scene.FindEntity("Debug Floor");
    REQUIRE(floor);
    REQUIRE(floor->GetComponent<Spot::BoxCollider>());
    auto* renderer = floor->GetComponent<Spot::MeshRenderer>();
    REQUIRE(renderer);
    CHECK(renderer->GetMaterial().GetId() == game.assets.FindByPath("materials/debug_grid.material").value());
    CHECK_FALSE(game.scene.FindComponent<NpcSpawner>());
    CHECK_FALSE(game.scene.FindEntity("Workshop"));
    REQUIRE(game.scene.FindComponent<Gameplay::GameMode>());
    REQUIRE(Gameplay::MapInfo::Get(game.scene));
    CHECK(Gameplay::MapInfo::Get(game.scene)->GetRestartScene() ==
          game.assets.FindByPath("scenes/empty.scene").value());
    REQUIRE(game.scene.FindComponent<Gameplay::SessionControls>());
    CHECK(Gameplay::MapInfo::Get(game.scene)->checkpoint == "empty_world.json");
    auto* inventory = Gameplay::Players::FindComponent<Gameplay::Inventory>(game.scene);
    REQUIRE(inventory);
    CHECK(inventory->GetItems().size() == 6);
    for (int frame = 0; frame < 120; ++frame) {
        game.Step();
    }
    auto* player = game.scene.FindEntity("Player");
    REQUIRE(player);
    CHECK(player->GetTransform().GetWorldPosition().y > 0.5f);
    CHECK(player->GetTransform().GetWorldPosition().y < 2.0f);
    auto ui = game.GetUi();
    ui.pause.Open();
    CHECK(Spot::Time::GetTimeScale() == 0.0f);
    REQUIRE(game.scene.GetEntity(ui.pause.mainMenuButton)->GetComponent<Spot::UiButton>());
    CHECK(ui.pause.mainMenuScene == game.assets.FindByPath("scenes/main_menu.scene").value());
    game.scene.GetEntity(ui.pause.resumeButton)->GetComponent<Spot::UiButton>()->Click();
    CHECK(Spot::Time::GetTimeScale() == 1.0f);
}

TEST_CASE("The sandbox maps share Free rules and have sixteen distinct spawn points", "[sandbox][maps][session]") {
    const std::string_view path = GENERATE("scenes/playground.scene", "scenes/empty.scene");
    InterfaceGame game(path);
    Gameplay::MapInfo* map = Gameplay::MapInfo::Get(game.scene);
    REQUIRE(map);
    const auto free = game.assets.FindByPath("prefabs/modes/free.prefab");
    REQUIRE(free);
    CHECK(map->defaultMode.GetId() == *free);
    CHECK(map->SupportsMode(*free));
    CHECK(map->maxPlayers == 16);
    REQUIRE(Gameplay::GameMode::Get(game.scene));
    CHECK(Gameplay::GameMode::Get(game.scene)->GetEntity().GetPrefabAssetId() == *free);
    CHECK_FALSE(map->GetEntity().HasComponent<Gameplay::GameMode>());
    CHECK_FALSE(map->GetEntity().HasComponent<Gameplay::Hud>());
    CHECK_FALSE(map->GetEntity().HasComponent<Gameplay::SessionControls>());
    std::vector<Spot::Vector3> positions;
    game.scene.ForEachComponent<Gameplay::SpawnPoint>([&](Gameplay::SpawnPoint& spawn) {
        const auto position = spawn.GetTransform().GetWorldPosition();
        for (const auto& previous : positions) {
            CHECK(Spot::Distance(previous, position) > 1.5f);
        }
        positions.push_back(position);
    });
    CHECK(positions.size() >= static_cast<std::size_t>(map->maxPlayers));
    Gameplay::MapCatalog maps;
    Gameplay::ModeCatalog modes;
    maps.Refresh(game.assets);
    modes.Refresh(game.assets, "prefabs/modes");
    REQUIRE(maps.GetEntries().size() == 2);
    REQUIRE(modes.Find(*free));
    CHECK(modes.Find(*free)->name == "Free");
}

TEST_CASE("Sandbox authored HUD and spawn menu route Q and gamepad tabs", "[sandbox][ui]") {
    InterfaceGame game;
    auto ui = game.GetUi();
    CHECK(&ui.spawn.GetEntity() != &ui.pause.GetEntity());
    CHECK(game.scene.GetEntity(ui.handText)->GetComponent<Spot::UiText>()->text == "PHYSICS GUN");
    CHECK(game.scene.GetEntity(ui.statusText)->GetComponent<Spot::UiText>()->text.find("bodies") != std::string::npos);
    REQUIRE(ui.spawn.GetSpawnButtons().size() ==
            Gameplay::Players::FindComponent<PropSpawner>(game.scene)->GetSpawnables().size());
    CHECK(game.Send(Spot::KeyPressedEvent{Spot::Key::Q, false}));
    game.Step();
    CHECK(ui.spawn.IsOpen());
    CHECK(game.input.IsCaptured());
    game.Send(Spot::KeyReleasedEvent{Spot::Key::Q});
    const auto* first = game.scene.GetEntity(ui.spawn.GetSpawnButtons().front());
    REQUIRE(first);
    CHECK(first->GetComponent<Spot::UiButton>());
    CHECK(first->FindChild("Thumbnail")->GetComponent<Spot::UiImage>());
    game.scene.GetOrAddSystem<Spot::UiInputSystem>().Focus(ui.weaponsTab);
    game.Send(Spot::GamepadButtonPressedEvent{Spot::GamepadId{1}, Spot::GamepadButton::South});
    CHECK_FALSE(first->IsActiveInHierarchy());
    CHECK(game.scene.GetOrAddSystem<Spot::UiInputSystem>().GetFocusedEntity() == ui.weaponsTab);
    CHECK(game.Send(Spot::KeyPressedEvent{Spot::Key::Q, false}));
    game.Step();
    CHECK(ui.IsClosed());
    CHECK_FALSE(game.input.IsCaptured());
    CHECK(game.Send(Spot::KeyReleasedEvent{Spot::Key::Q}));
}

TEST_CASE("The sandbox HUD shows rounds, reloads, sights, the item's name and statistics like a game",
          "[sandbox][ui][hud]") {
    InterfaceGame game;
    auto ui = game.GetUi();
    const auto text = [&](Spot::EntityId id) { return game.scene.GetEntity(id)->GetComponent<Spot::UiText>()->text; };
    const auto active = [&](Spot::EntityId id) { return game.scene.GetEntity(id)->IsActiveInHierarchy(); };
    const auto nameOpacity = [&] { return game.scene.GetEntity(ui.handText)->GetComponent<Spot::UiText>()->color.a; };
    // Debugging statistics wait for their key; the name of what is in hand for a switch.
    CHECK_FALSE(active(ui.statsPanel));
    CHECK(nameOpacity() == 0.0f);
    CHECK(active(ui.crosshair));
    CHECK(text(ui.clockText).size() == 5);
    // By day the clock shows the sun.
    CHECK(game.scene.GetEntity(ui.clockIcon)->GetComponent<Spot::UiImage>()->texture == ui.sunIcon);

    auto& holder = *Gameplay::Players::FindComponent<Gameplay::Inventory>(game.scene);
    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::Digit2, false});
    game.Step();
    game.input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::Digit2});
    game.Step();
    REQUIRE(SandboxTests::GetFirearm(holder));
    CHECK(nameOpacity() == 1.0f);
    CHECK(text(ui.handText) == "PISTOL");
    CHECK(text(ui.countText) == std::to_string(SandboxTests::GetFirearm(holder)->GetAmmo()));
    CHECK(text(ui.detailText) == "/ " + std::to_string(SandboxTests::GetFirearm(holder)->GetReserve()));
    for (int i = 0; i < 180; ++i)
        game.Step();
    CHECK(nameOpacity() == 0.0f);

    // Down the sights, the crosshair gives way to them.
    game.input.OnEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Right});
    game.Step();
    CHECK_FALSE(active(ui.crosshair));
    game.input.OnEvent(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Right});
    game.Step();
    CHECK(active(ui.crosshair));

    // Reloading fills a bar.
    SandboxTests::Fire(holder);
    REQUIRE(SandboxTests::GetFirearm(holder)->StartReload());
    for (int i = 0; i < 30; ++i)
        game.Step();
    CHECK(active(ui.reloadTrack));
    CHECK(game.scene.GetEntity(ui.reloadFill)->GetComponent<Spot::UiRect>()->layout.sizeDelta.x > 0.0f);
    for (int i = 0; i < 120; ++i)
        game.Step();
    CHECK_FALSE(active(ui.reloadTrack));

    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::F11, false});
    game.Step();
    game.input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::F11});
    game.Step();
    CHECK(active(ui.statsPanel));
    CHECK(text(ui.statusText).find("FPS") != std::string::npos);
    CHECK(ui.statistics.IsToggled());
}

TEST_CASE("Sandbox menu clicks preview real prefabs to place and offer undo and cleanup", "[sandbox][ui]") {
    InterfaceGame game;
    auto ui = game.GetUi();
    ui.spawn.Open();
    auto& spawner = *Gameplay::Players::FindComponent<PropSpawner>(game.scene);
    auto* button = game.scene.GetEntity(ui.spawn.GetSpawnButtons().front())->GetComponent<Spot::UiButton>();
    REQUIRE(button);
    const auto* layout = game.scene.GetOrAddSystem<Spot::UiSystem>().Find(button->GetEntity().GetId());
    REQUIRE(layout);
    const auto center = layout->bounds.GetCenter();
    game.Send(Spot::MouseMovedEvent{{center.x, 720 - center.y}, {}});
    CHECK(game.Send(Spot::MouseButtonPressedEvent{Spot::MouseButton::Left}));
    CHECK(game.Send(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Left}));
    CHECK_FALSE(game.input.IsMouseButtonPressed(Spot::MouseButton::Left));
    // Choosing closes the menu and shows the object where it would go, marked in the menu as chosen.
    CHECK(ui.IsClosed());
    CHECK(spawner.IsPlacing());
    CHECK(ui.spawn.GetChosenSpawn() == std::optional<std::size_t>(0));
    CHECK(spawner.GetSpawnedCount() == 0);
    REQUIRE(spawner.ConfirmPlacement());
    CHECK(spawner.GetSpawnedCount() == 1);
    CHECK_FALSE(spawner.IsPlacing());
    ui.spawn.Open();
    game.scene.GetEntity(ui.undoSpawn)->GetComponent<Spot::UiButton>()->Click();
    CHECK(spawner.GetSpawnedCount() == 0);
    button->Click();
    REQUIRE(spawner.ConfirmPlacement());
    CHECK(spawner.GetSpawnedCount() == 1);
    ui.spawn.Open();
    game.scene.GetEntity(ui.cleanup)->GetComponent<Spot::UiButton>()->Click();
    CHECK(spawner.GetSpawnedCount() == 0);
}

TEST_CASE("The spawn preview follows the aim, places on Fire without firing and cancels on Aim", "[sandbox][ui]") {
    InterfaceGame game;
    auto& spawner = *Gameplay::Players::FindComponent<PropSpawner>(game.scene);
    auto& holder = *Gameplay::Players::FindComponent<Gameplay::Inventory>(game.scene);
    Spot::Input::SetCursorLocked(true);
    holder.Select(1);
    game.Step();
    REQUIRE(SandboxTests::GetFirearm(holder));
    SandboxTests::GetFirearm(holder)->Refill();
    const int rounds = SandboxTests::GetFirearm(holder)->GetAmmo();
    REQUIRE(spawner.BeginPlacement(0));
    game.Step();
    auto* preview = game.scene.GetEntity(spawner.GetPreview());
    REQUIRE(preview);
    // Meshes only, translucent, with no body to fall or be saved.
    bool translucent = true;
    int meshes = 0;
    game.scene.ForEachComponent<Spot::MeshRenderer>([&](Spot::MeshRenderer& mesh) {
        if (mesh.GetEntity().GetParent() != preview)
            return;
        ++meshes;
        translucent =
            translucent && mesh.GetMaterial()->alphaMode == Spot::AlphaMode::Transparent && !mesh.IsCastingShadows();
    });
    CHECK(meshes > 0);
    CHECK(translucent);
    CHECK_FALSE(preview->HasComponent<Spot::Rigidbody>());
    CHECK_FALSE(holder.CanUseItem());
    CHECK(game.GetUi().GetText(game.GetUi().promptText).find("Place") != std::string::npos);
    const Spot::Vector3 shown = preview->GetTransform().GetPosition();

    game.input.OnEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Left});
    game.Step();
    REQUIRE(spawner.GetSpawnedCount() == 1);
    CHECK_FALSE(spawner.IsPlacing());
    CHECK_FALSE(game.scene.GetEntity(spawner.GetPreview()));
    Spot::Entity* placed = nullptr;
    game.scene.ForEachComponent<Spawnable>([&](Spawnable& spawnable) {
        if (spawner.IsSpawned(spawnable.GetEntity().GetId()))
            placed = &spawnable.GetEntity();
    });
    REQUIRE(placed);
    CHECK(Spot::Length(placed->GetTransform().GetPosition() - shown) < 0.05f);
    // The press that placed it fired nothing, even held.
    game.Step();
    CHECK(SandboxTests::GetFirearm(holder)->GetAmmo() == rounds);
    game.input.OnEvent(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Left});
    game.Step();
    CHECK(holder.CanUseItem());

    REQUIRE(spawner.BeginPlacement(0));
    game.input.OnEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Right});
    game.Step();
    CHECK_FALSE(spawner.IsPlacing());
    CHECK(spawner.GetSpawnedCount() == 1);
    CHECK_FALSE(SandboxTests::GetFirearm(holder)->IsAiming());
    game.input.OnEvent(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Right});
    game.Step();

    // Opening the menu again puts the preview away.
    REQUIRE(spawner.BeginPlacement(0));
    game.GetUi().spawn.Open();
    CHECK_FALSE(spawner.IsPlacing());
    Gameplay::Menu::CloseAll(game.scene);
}

TEST_CASE("The spawn menu searches, counts each category and switches to a compact view", "[sandbox][ui]") {
    InterfaceGame game;
    auto ui = game.GetUi();
    const auto& spawnables = Gameplay::Players::FindComponent<PropSpawner>(game.scene)->GetSpawnables();
    ui.spawn.Open();
    const auto shown = [&] {
        std::vector<std::string> names;
        for (const auto id : ui.spawn.GetSpawnButtons()) {
            if (const auto* card = game.scene.GetEntity(id); card->IsActiveSelf())
                names.push_back(card->GetName());
        }
        return names;
    };
    const auto count = [&](Spot::EntityId tab) {
        return game.scene.GetEntity(tab)->FindChild("Count")->GetComponent<Spot::UiText>()->text;
    };
    const auto props = static_cast<std::size_t>(std::ranges::count_if(
        spawnables, [](const SpawnableItem& item) { return item.category == Spawnable::Category::Prop; }));
    CHECK(shown().size() == props);
    CHECK(count(ui.propsTab) == std::to_string(props));
    CHECK(game.scene.GetEntity(ui.spawnCount)->GetComponent<Spot::UiText>()->text ==
          std::to_string(props) + " OBJECTS");

    // Typed into the field, Q searches rather than closing the menu.
    auto& input = game.scene.GetOrAddSystem<Spot::UiInputSystem>();
    REQUIRE(input.Focus(ui.spawnSearch));
    CHECK(game.Send(Spot::KeyPressedEvent{Spot::Key::B, false}));
    CHECK(game.Send(Spot::TextInputEvent{"B"}));
    CHECK(game.Send(Spot::KeyPressedEvent{Spot::Key::Q, false}));
    CHECK(ui.spawn.IsOpen());
    game.Send(Spot::KeyPressedEvent{Spot::Key::Backspace, false});
    ui.spawn.Search("BARREL");
    const auto barrels = shown();
    REQUIRE(!barrels.empty());
    for (const auto& name : barrels)
        CHECK(name.find("arrel") != std::string::npos);
    CHECK(count(ui.weaponsTab) == "0");
    ui.spawn.Search("pistol");
    CHECK(shown().empty());
    CHECK(count(ui.weaponsTab) == "1");
    ui.spawn.SelectCategory(Spawnable::Category::Weapon);
    CHECK(shown() == std::vector<std::string>{"Pistol"});
    ui.spawn.Search("");
    CHECK(ui.spawn.GetCategory() == Spawnable::Category::Weapon);

    ui.spawn.SetCompactView(true);
    const auto* grid = game.scene.GetEntity(ui.spawnGrid)->GetComponent<Spot::UiLayoutGroup>();
    const Spot::Vector2 compact = grid->settings.cellSize;
    CHECK(ui.spawn.IsCompactView());
    game.scene.GetEntity(ui.gridView)->GetComponent<Spot::UiButton>()->Click();
    CHECK_FALSE(ui.spawn.IsCompactView());
    CHECK(grid->settings.cellSize.x > compact.x);
    CHECK(grid->settings.columns < 8);
    Gameplay::Menu::CloseAll(game.scene);
}

TEST_CASE("Help shows the controls from the action map, which a line names as the game starts", "[sandbox][ui]") {
    InterfaceGame game;
    auto ui = game.GetUi();
    const auto active = [&](Spot::EntityId id) { return game.scene.GetEntity(id)->IsActiveInHierarchy(); };
    CHECK_FALSE(active(ui.helpPanel));
    CHECK(active(ui.helpHint));
    CHECK(game.scene.GetEntity(ui.helpHint)->GetComponent<Spot::UiText>()->text == "[H]  Controls");
    const auto row = [&](std::string_view action) {
        const auto* entity = game.scene.GetEntity(ui.helpContent)->FindChild("Help " + std::string(action));
        REQUIRE(entity);
        return entity->FindChild("Key")->GetComponent<Spot::UiText>()->text;
    };
    CHECK(row("Move") == "WASD");
    CHECK(row("Fire") == "LMB");
    CHECK(row("Spawn Menu") == "Q");
    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::H, false});
    game.Step();
    game.input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::H});
    game.Step();
    CHECK(active(ui.helpPanel));
    CHECK_FALSE(active(ui.helpHint));
    // Rebinding renames the key it shows.
    game.actions.Rebind("Spawn Menu", 0, Spot::InputControl::Of(Spot::Key::G));
    game.Step();
    CHECK(row("Spawn Menu") == "G");
    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::H, false});
    game.Step();
    CHECK_FALSE(active(ui.helpPanel));
    // As the game goes on, the line goes.
    for (int i = 0; i < 60 * 13; ++i)
        game.Step();
    CHECK_FALSE(active(ui.helpHint));
}

TEST_CASE("Sandbox pause settings persist sensitivity FOV volumes quality and bindings", "[sandbox][ui]") {
    InterfaceGame game;
    TempDirectory directory;
    auto ui = game.GetUi();
    ui.settings.SetSettingsDirectory(directory.GetPath());
    Spot::Time::SetTimeScale(0.5f);
    ui.pause.Open();
    CHECK(Spot::Time::GetTimeScale() == 0);
    CHECK(game.input.IsCaptured());
    game.scene.GetEntity(ui.sensitivitySlider)->GetComponent<Spot::UiSlider>()->SetValue(0.004f, true);
    game.scene.GetEntity(ui.fovSlider)->GetComponent<Spot::UiSlider>()->SetValue(90, true);
    game.scene.GetEntity(ui.masterSlider)->GetComponent<Spot::UiSlider>()->SetValue(0.4f, true);
    game.scene.GetEntity(ui.qualityButton)->GetComponent<Spot::UiButton>()->Click();
    CHECK(ui.settings.GetSettings().graphicsQuality == 0);
    const auto* player = Gameplay::Players::FindComponent<Gameplay::FirstPersonView>(game.scene);
    REQUIRE(player);
    CHECK(player->lookSensitivity == Catch::Approx(0.004f));
    CHECK(Spot::ToDegrees(player->GetFieldOfView()) == Catch::Approx(90));
    auto* rows = game.scene.GetEntity(ui.bindingContent);
    REQUIRE(rows);
    Spot::UiButton* jump = nullptr;
    rows->ForEachChild([&](Spot::Entity& child) {
        if (!jump && child.GetName() == "Binding Jump")
            jump = child.GetComponent<Spot::UiButton>();
    });
    REQUIRE(jump);
    jump->Click();
    CHECK(game.Send(Spot::KeyPressedEvent{Spot::Key::J, false}));
    CHECK(game.actions.Find("Jump")->bindings.front().control == Spot::InputControl::Of(Spot::Key::J));
    CHECK_FALSE(game.input.IsKeyPressed(Spot::Key::J));
    game.Send(Spot::KeyPressedEvent{Spot::Key::Escape, false});
    CHECK(ui.IsClosed());
    CHECK(Spot::Time::GetTimeScale() == 0.5f);
    CHECK_FALSE(game.input.IsCaptured());
    Gameplay::UserSettings loaded;
    REQUIRE(loaded.Load(directory.GetPath()));
    CHECK(loaded.sensitivity == Catch::Approx(0.004f));
    CHECK(loaded.fieldOfView == 90);
    CHECK(loaded.masterVolume == Catch::Approx(0.4f));
    CHECK(loaded.graphicsQuality == 0);
    Spot::InputActionMap reloaded;
    reloaded.Add("Jump", Spot::InputActionType::Button).Bind(Spot::InputControl::Of(Spot::Key::Space));
    reloaded.SetDefaults();
    REQUIRE(reloaded.LoadRebindsFromString(loaded.rebinds));
    CHECK(reloaded.Find("Jump")->bindings.front().control == Spot::InputControl::Of(Spot::Key::J));
}

TEST_CASE("Interface settings validate data and preserve previous values on failed loads", "[sandbox][ui]") {
    TempDirectory directory;
    Gameplay::UserSettings settings;
    settings.sensitivity = std::numeric_limits<float>::quiet_NaN();
    settings.fieldOfView = 500;
    settings.masterVolume = -1;
    settings.graphicsQuality = 99;
    REQUIRE(settings.Save(directory.GetPath()));
    Gameplay::UserSettings loaded;
    REQUIRE(loaded.Load(directory.GetPath()));
    CHECK(std::isfinite(loaded.sensitivity));
    CHECK(loaded.fieldOfView == 120);
    CHECK(loaded.masterVolume == 0);
    CHECK(loaded.graphicsQuality == 2);
    loaded.fieldOfView = 80;
    Spot::SaveData store(directory.GetPath());
    REQUIRE(store.Save("interface.json", 99, [](Spot::Archive&, std::uint32_t) {}));
    CHECK_FALSE(loaded.Load(directory.GetPath()));
    CHECK(loaded.fieldOfView == 80);
}
