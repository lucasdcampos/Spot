#include "game_controls.h"

#include "hud.h"
#include "robot_spawner.h"
#include "runtime_interface.h"
#include "sandbox_commands.h"
#include "sound_bank.h"
#include "world_save.h"

SPOT_COMPONENT(GameControls);

GameControls::GameControls() = default;
GameControls::~GameControls() = default;

void GameControls::OnEnable() {
    auto& console = Spot::Console::Get();
    m_commands = std::make_unique<SandboxCommands>(console, GetScene());
    m_cheatsBefore = console.AreCheatsEnabled();
    if (cheats) {
        console.SetCheatsEnabled(true);
    }
}

void GameControls::OnDisable() {
    m_commands.reset();
    if (cheats) {
        Spot::Console::Get().SetCheatsEnabled(m_cheatsBefore);
    }
}

void GameControls::OnStart() {
    Spot::Input::SetCursorLocked(true);
    if (!GetScene().FindComponent<RobotSpawner>()) {
        auto population = robots;
        if (!population) {
            if (auto* assets = Spot::Assets::GetManager();
                assets && assets->FindByPath("prefabs/actors/robot_spawner.prefab")) {
                population = assets->Load<Spot::Prefab>("prefabs/actors/robot_spawner.prefab");
            }
        }
        if (population) {
            GetScene().Instantiate(population);
        }
    }
    if (GetScene().IsLoaded()) {
        if (const auto* hud = GetScene().FindComponent<Hud>()) {
            if (auto* tools = GetScene().GetApplication().GetModule<Spot::DevToolsModule>())
                tools->SetFont(hud->font);
        }
    }
}

void GameControls::OnUpdate(float /*deltaTime*/) {
    namespace Input = Spot::Input;

    const bool save = Input::IsActionPressed("Quick Save");
    const bool load = Input::IsActionPressed("Quick Load");
    if (save || load) {
        const auto directory = !m_saveDirectory.empty() ? m_saveDirectory
                               : GetScene().IsLoaded()  ? GetScene().GetApplication().GetUserDataDirectory()
                                                        : std::filesystem::path{};
        if (!directory.empty()) {
            const auto result = save ? WorldSave::Save(GetScene(), directory / "world.json")
                                     : WorldSave::Load(GetScene(), directory / "world.json");
            if (result) {
                Spot::Log::Info("World {}", save ? "saved" : "loaded");
            } else {
                Spot::Log::Warning("World {} failed: {}", save ? "save" : "load", result.message);
            }
        }
    }

    if (Input::IsActionPressed("Pause")) {
        SoundBank::PlayClick(GetScene());
        if (auto* ui = GetScene().FindComponent<RuntimeInterface>()) {
            ui->OpenPauseMenu();
        } else if (Input::IsCursorLocked()) {
            Input::SetCursorLocked(false);
        } else {
            GetScene().GetApplication().QuitGame();
        }
    }
    if (Input::IsActionPressed("Restart") && restartScene.IsValid()) {
        if (auto scene = Spot::InstantiateScene(*Spot::Assets::GetManager(), restartScene)) {
            GetScene().GetApplication().LoadScene(std::move(scene));
        }
    }
}

void GameControls::OnLateUpdate(float /*deltaTime*/) {
    if (Spot::Input::IsActionPressed("Fire") && !Spot::Input::IsCursorLocked()) {
        SoundBank::PlayClick(GetScene());
        Spot::Input::SetCursorLocked(true);
    }
}

void GameControls::Serialize(Spot::Archive& archive) {
    archive.Field("robots", robots,
                  {.tooltip = "The robot population prefab; the default population is used when empty"});
    archive.Field("cheats", cheats, {.tooltip = "Permits the console's cheat commands while the game runs"});
    archive.Field("restartScene", restartScene,
                  {.tooltip = "The scene the Restart action loads", .assetType = Spot::AssetType::Scene});
}
