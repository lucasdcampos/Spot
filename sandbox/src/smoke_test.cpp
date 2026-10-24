#include "smoke_test.h"

#include "day_cycle.h"
#include "particle_effects.h"
#include "player_controller.h"
#include "prop_spawner.h"
#include "robot.h"
#include "runtime_interface.h"
#include "weapon.h"
#include "weapon_holder.h"
#include "world_save.h"

#include <spot.h>

#include <cstdlib>
#include <filesystem>

namespace {

class SmokeTest final : public Spot::Application {
public:
    explicit SmokeTest(const std::filesystem::path& directory)
        : Application({.graphics = {.enabled = false},
                       .logFile = directory / "smoke.log",
                       .userDataDirectory = directory,
                       .maxFrameRate = 60,
                       .consoleOverlay = false}) {
        AddModule<Spot::UiModule>();
        AddModule<Spot::RenderModule>();
        AddModule<Spot::AudioModule>(Spot::AudioConfig{.enabled = false});
        AddModule<Spot::PhysicsModule>();
        AddModule<Spot::Physics2DModule>();
        AddModule<Spot::AnimationModule>();
        AddModule<Spot::ParticlesModule>();
        AddModule<Spot::ScriptingModule>();
    }
    bool IsPassed() const { return m_passed; }

protected:
    void OnUpdate(float) override {
        auto* scene = GetScene();
        if (++m_frame < 4) {
            return;
        }
        if (!Require(scene != nullptr, "No startup scene was loaded")) {
            return;
        }
        auto* spawner = scene->FindComponent<PropSpawner>();
        auto* ui = scene->FindComponent<RuntimeInterface>();
        auto* holder = scene->FindComponent<WeaponHolder>();
        auto* robot = scene->FindComponent<Robot>();
        if (!Require(spawner && ui && holder && robot, "Gameplay components or authored interface are missing")) {
            return;
        }
        switch (m_frame) {
        case 4: {
            if (!Require(scene->FindEntity("Workshop") && scene->FindComponent<DayCycle>() &&
                             scene->FindComponent<ParticleEffects>() &&
                             scene->FindEntitiesWithTag("SpawnArea").size() == 3,
                         "The finished level is missing")) {
                return;
            }
            const auto* player = scene->FindComponent<PlayerController>();
            const auto* camera = Spot::FindMainCamera(*scene);
            if (!Require(player && camera &&
                             Spot::Length(camera->GetCamera3D().GetForward() - player->GetAimDirection()) < .001f,
                         "The camera and gameplay aim disagree")) {
                return;
            }
            if (!Require(holder->GetWeapons().size() == 4 && spawner->GetSpawnables().size() >= 16,
                         "The shipped inventory or prefab catalog is incomplete")) {
                return;
            }
            ui->OpenSpawnMenu();
            if (!Require(!ui->GetSpawnButtons().empty(), "The spawn menu has no entries")) {
                return;
            }
            scene->GetEntity(ui->GetSpawnButtons().front())->GetComponent<Spot::UiButton>()->Click();
            ui->CloseMenus();
            break;
        }
        case 5:
            if (!Require(spawner->GetSpawnedCount() == 1, "The spawn menu did not create its prefab")) {
                return;
            }
            holder->Select(1);
            if (!Require(holder->GetWeapon() && holder->GetWeapon()->fireSound,
                         "The imported firearm or its sound is missing")) {
                return;
            }
            holder->Fire();
            if (!Require(scene->FindComponent<PlayerController>()->GetRecoil() > 0, "The firearm did not fire")) {
                return;
            }
            break;
        case 6: {
            const auto path = GetUserDataDirectory() / "world.json";
            if (!Require(static_cast<bool>(WorldSave::Save(*scene, path)), "The world checkpoint did not save")) {
                return;
            }
            ui->OpenPauseMenu();
            if (!Require(static_cast<bool>(WorldSave::Load(*scene, path)), "The world checkpoint did not load")) {
                return;
            }
            ui->CloseMenus();
            break;
        }
        case 7:
            spawner->Cleanup();
            if (!Require(spawner->GetSpawnedCount() == 0 && scene->FindEntity("Workshop"),
                         "Cleanup changed the permanent level")) {
                return;
            }
            robot->Hit({10, 0, 0}, robot->GetEntity().GetTransform().GetWorldPosition());
            break;
        case 12: {
            const auto* ragdoll = robot->GetEntity().GetComponent<Spot::Ragdoll>();
            if (!Require(robot->IsDead() && ragdoll && ragdoll->IsActive(), "The robot did not become a ragdoll")) {
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
