#include "game_controls.h"

#include "npc_spawner.h"
#include "sandbox_commands.h"

#include "gameplay/hud/hud.h"

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
    if (spawnNpcs && !GetScene().FindComponent<NpcSpawner>()) {
        auto population = npcs;
        if (!population) {
            if (auto* assets = Spot::Assets::GetManager();
                assets && assets->FindByPath("prefabs/actors/npc_spawner.prefab")) {
                population = assets->Load<Spot::Prefab>("prefabs/actors/npc_spawner.prefab");
            }
        }
        if (population) {
            GetScene().Instantiate(population);
        }
    }
    if (GetScene().IsLoaded()) {
        if (const auto* hud = GetScene().FindComponent<Gameplay::Hud>()) {
            if (auto* tools = Spot::Application::Get().GetModule<Spot::DevToolsModule>())
                tools->SetFont(hud->font);
        }
    }
}

void GameControls::Serialize(Spot::Archive& archive) {
    archive.Field("npcs", npcs, {.tooltip = "The NPC population prefab; the default population is used when empty"});
    archive.Field("cheats", cheats, {.tooltip = "Permits the console's cheat commands while the game runs"});
    archive.Field("spawnNpcs", spawnNpcs, {.tooltip = "Adds the sandbox's default NPC population"});
}
