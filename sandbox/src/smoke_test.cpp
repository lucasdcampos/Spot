#include "smoke_test.h"
#include "gameplay/core/players.h"

#include "main_menu.h"
#include "npc.h"
#include "prop_spawner.h"
#include "spawn_menu.h"

#include "gameplay/camera/first_person_view.h"
#include "gameplay/effects/effect_library.h"
#include "gameplay/environment/day_cycle.h"
#include "gameplay/inventory/inventory.h"
#include "gameplay/menus/pause_menu.h"
#include "gameplay/persistence/world_save.h"
#include "gameplay/weapons/firearm.h"

#include <spot.h>
#include <spot/scene/scripting/scripting_module.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace {

class SmokeTest final : public Spot::Application {
public:
    explicit SmokeTest(const std::filesystem::path& directory)
        : Application({.game = Spot::GameSettings::LoadBesideExecutable(),
                       .graphics = {.enabled = false},
                       .logFile = directory / "smoke.log",
                       .userDataDirectory = directory,
                       .maxFrameRate = 60}) {
        AddModule<Spot::UiModule>();
        AddModule<Spot::RenderModule>();
        AddModule<Spot::AudioModule>(Spot::AudioConfig{.enabled = false});
        AddModule<Spot::PhysicsModule>();
        AddModule<Spot::Physics2DModule>();
        AddModule<Spot::AnimationModule>();
        AddModule<Spot::ParticlesModule>();
        AddModule<Spot::NetworkModule>();
        AddModule<Spot::ScriptingModule>();
        AddModule(Spot::CreateGameModule());
    }
    bool IsPassed() const { return m_passed; }

protected:
    void OnUpdate(float) override {
        auto* scene = GetScene();
        if (m_menuFrame < 6) {
            const int frame = m_menuFrame++;
            if (frame == 1 || frame == 5) {
                auto* menu = scene ? scene->FindComponent<MainMenu>() : nullptr;
                if (!Require(menu && !Spot::Input::IsCursorLocked(), "The map selection menu is missing")) {
                    return;
                }
                scene->GetEntity(menu->playButton)->GetComponent<Spot::UiButton>()->Click();
                menu->SelectMap(frame == 1 ? 1 : 0);
                scene->GetEntity(menu->startButton)->GetComponent<Spot::UiButton>()->Click();
            } else if (frame == 3) {
                auto* pause = scene ? scene->FindComponent<Gameplay::PauseMenu>() : nullptr;
                if (!Require(scene && scene->FindEntity("Debug Floor") && !scene->FindComponent<Npc>() && pause &&
                                 Spot::Input::IsCursorLocked(),
                             "The empty map did not start with its floor and player")) {
                    return;
                }
                pause->Open();
                scene->GetEntity(pause->mainMenuButton)->GetComponent<Spot::UiButton>()->Click();
                Require(Spot::Time::GetTimeScale() == 1.0f, "Returning to the menu left the game paused");
            }
            return;
        }
        if (++m_frame < 4) {
            return;
        }
        if (!Require(scene != nullptr, "No startup scene was loaded")) {
            return;
        }
        auto* spawner = Gameplay::Players::FindComponent<PropSpawner>(*scene);
        auto* ui = scene->FindComponent<SpawnMenu>();
        auto* inventory = Gameplay::Players::FindComponent<Gameplay::Inventory>(*scene);
        auto* npc = scene->FindComponent<Npc>();
        if (!Require(spawner && ui && inventory && npc, "Gameplay components or authored interface are missing")) {
            return;
        }
        switch (m_frame) {
        case 4: {
            if (!Require(scene->FindEntity("Workshop") && scene->FindComponent<Gameplay::DayCycle>() &&
                             scene->FindComponent<Gameplay::EffectLibrary>() &&
                             scene->FindEntitiesWithTag("SpawnArea").size() == 3,
                         "The finished level is missing")) {
                return;
            }
            const auto* player = Gameplay::Players::FindComponent<Gameplay::FirstPersonView>(*scene);
            const auto* camera = Spot::FindMainCamera(*scene);
            if (!Require(player && camera &&
                             Spot::Length(camera->GetCamera3D().GetForward() - player->GetAimDirection()) < .001f,
                         "The camera and gameplay aim disagree")) {
                return;
            }
            const auto items = inventory->GetItems();
            const auto firearms = std::ranges::count_if(items, [](const Gameplay::HeldItem* item) {
                return dynamic_cast<const Gameplay::Firearm*>(item) != nullptr;
            });
            if (!Require(firearms == 4 && spawner->GetSpawnables().size() >= 16,
                         "The shipped inventory or prefab catalog is incomplete")) {
                return;
            }
            ui->Open();
            if (!Require(!ui->GetSpawnButtons().empty(), "The spawn menu has no entries")) {
                return;
            }
            scene->GetEntity(ui->GetSpawnButtons().front())->GetComponent<Spot::UiButton>()->Click();
            if (!Require(spawner->IsPlacing() && spawner->ConfirmPlacement(),
                         "The spawn menu did not offer a placeable prefab")) {
                return;
            }
            Gameplay::Menu::CloseAll(*scene);
            break;
        }
        case 5: {
            if (!Require(spawner->GetSpawnedCount() == 1, "The spawn menu did not create its prefab")) {
                return;
            }
            inventory->Select(1);
            auto* firearm = dynamic_cast<Gameplay::Firearm*>(inventory->GetEquipped());
            if (!Require(firearm && firearm->fireSound, "The imported firearm or its sound is missing")) {
                return;
            }
            firearm->Fire();
            if (!Require(Gameplay::Players::FindComponent<Gameplay::FirstPersonView>(*scene)->GetRecoil() > 0,
                         "The firearm did not fire")) {
                return;
            }
            break;
        }
        case 6: {
            const auto path = GetUserDataDirectory() / "world.json";
            if (!Require(static_cast<bool>(Gameplay::WorldSave::Save(*scene, path)),
                         "The world checkpoint did not save")) {
                return;
            }
            if (auto* pause = scene->FindComponent<Gameplay::PauseMenu>()) {
                pause->Open();
            }
            if (!Require(static_cast<bool>(Gameplay::WorldSave::Load(*scene, path)),
                         "The world checkpoint did not load")) {
                return;
            }
            Gameplay::Menu::CloseAll(*scene);
            break;
        }
        case 7:
            spawner->Cleanup();
            if (!Require(spawner->GetSpawnedCount() == 0 && scene->FindEntity("Workshop"),
                         "Cleanup changed the permanent level")) {
                return;
            }
            npc->Hit({10, 0, 0}, npc->GetEntity().GetTransform().GetWorldPosition());
            break;
        case 12: {
            const auto* ragdoll = npc->GetEntity().GetComponent<Spot::Ragdoll>();
            if (!Require(npc->IsDead() && ragdoll && ragdoll->IsActive(), "The npc did not become a ragdoll")) {
                return;
            }
            m_passed = true;
            Spot::Log::Info(
                "Sandbox smoke test passed: level, camera, catalog, menu, weapon, checkpoint, cleanup and ragdoll");
            Quit();
            break;
        }
        default:
            break;
        }
    }

private:
    bool Require(bool condition, std::string_view message) {
        if (!condition) {
            Spot::Log::Error("Sandbox smoke test failed: {}", message);
            Quit();
        }
        return condition;
    }
    int m_frame = 0;
    int m_menuFrame = 0;
    bool m_passed = false;
};

} // namespace

int RunSandboxSmokeTest() {
    const auto directory =
        std::filesystem::temp_directory_path() / ("spot-sandbox-smoke-" + Spot::AssetId::Generate().ToString());
    int result = EXIT_FAILURE;
    bool passed = false;
    {
        SmokeTest application(directory);
        result = application.Run();
        passed = application.IsPassed();
    }
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    return result == EXIT_SUCCESS && passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
