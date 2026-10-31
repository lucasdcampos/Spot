#include "network_smoke_test.h"

#include "gameplay/camera/first_person_view.h"
#include "gameplay/core/pawn.h"
#include "gameplay/core/physics_drive.h"
#include "gameplay/core/players.h"
#include "gameplay/network/player_info.h"
#include "gameplay/network/session.h"
#include "main_menu.h"
#include "prop_spawner.h"
#include "session_config.h"

#include <spot.h>
#include <spot/scene/scripting/scripting_module.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {

using Clock = std::chrono::steady_clock;

struct Command {
    static constexpr std::string_view NetName = "Sandbox.SmokeCommand";
    int stage = 0;
    int crates = 0;
    void Serialize(Spot::Archive& archive) {
        archive.Field("stage", stage);
        archive.Field("crates", crates);
    }
};

struct Sample {
    Spot::EntityId pawn = Spot::EntityId::Invalid;
    Spot::Vector3 position;
    void Serialize(Spot::Archive& archive) {
        archive.Field("pawn", pawn);
        archive.Field("position", position);
    }
};

struct Report {
    static constexpr std::string_view NetName = "Sandbox.SmokeReport";
    std::vector<Sample> players;
    int crates = 0;
    void Serialize(Spot::Archive& archive) {
        archive.Field("players", players);
        archive.Field("crates", crates);
    }
};

Spot::ApplicationConfig Config(bool host, bool render, const std::filesystem::path& output) {
    return {.game = Spot::GameSettings::LoadBesideExecutable(),
            .window = {.title = host ? "Sandbox Room Test - Host" : "Sandbox Room Test - Guest",
                       .width = 900,
                       .height = 600,
                       .headless = !render},
            .graphics = {.enabled = render},
            .logFile = output / (host ? "host.log" : "guest.log"),
            .userDataDirectory = output / (host ? "host-data" : "guest-data"),
            .maxFrameRate = 60};
}

// Two normal game applications, driven through their actual menu buttons and input, over ENet. The client reports
// what its replicated scene shows; the server compares it with its authoritative scene before acknowledging success.
class NetworkSmoke final : public Spot::Application {
public:
    NetworkSmoke(bool host, std::uint16_t port, std::string map, std::filesystem::path output, bool render)
        : Application(Config(host, render, output)), m_host(host), m_port(port), m_map(std::move(map)),
          m_output(std::move(output)), m_render(render) {
        AddModule<Spot::UiModule>();
        AddModule<Spot::RenderModule>();
        AddModule<Spot::AudioModule>(Spot::AudioConfig{.enabled = false});
        AddModule<Spot::PhysicsModule>();
        AddModule<Spot::Physics2DModule>();
        AddModule<Spot::AnimationModule>();
        AddModule<Spot::ParticlesModule>();
        AddModule<Spot::NetworkModule>(SandboxNetworkConfig());
        AddModule<Gameplay::Session>();
        AddModule<Spot::ScriptingModule>();
        AddModule(Spot::CreateGameModule());
    }
    bool IsPassed() const { return m_passed; }

protected:
    void OnStart() override { m_started = Clock::now(); }

    void OnUpdate(float) override {
        auto* room = GetModule<Gameplay::Session>();
        if (Clock::now() - m_started > std::chrono::seconds(60)) {
            Fail("Timed out waiting for both room instances to synchronize");
            return;
        }
        if (room->GetState() == Gameplay::SessionState::Failed) {
            Fail(room->GetLastError());
            return;
        }
        Spot::Scene* scene = GetScene();
        if (!scene) {
            return;
        }
        if (!m_launched) {
            auto* menu = scene->FindComponent<MainMenu>();
            if (!menu || !menu->HasStarted() || menu->maps.empty()) {
                return;
            }
            const auto field = [&](Spot::EntityId id, std::string text) {
                scene->GetEntity(id)->GetComponent<Spot::UiInputField>()->SetText(std::move(text));
            };
            if (m_host) {
                menu->ShowPage(MainMenu::Page::Host);
                bool found = false;
                const auto id = GetAssets().FindByPath(m_map);
                for (std::size_t i = 0; i < menu->maps.size(); ++i) {
                    if (id && menu->maps[i].scene == *id) {
                        menu->SelectMap(i);
                        found = true;
                    }
                }
                if (!found) {
                    Fail("The requested map was not discovered by the main menu");
                    return;
                }
                field(menu->hostName, "Smoke Room");
                field(menu->hostPlayerName, "Host");
                field(menu->hostPort, std::to_string(m_port));
                field(menu->hostPlayers, "4");
                field(menu->hostPassword, "smoke");
                scene->GetEntity(menu->startButton)->GetComponent<Spot::UiButton>()->Click();
            } else {
                menu->ShowPage(MainMenu::Page::Join);
                field(menu->joinAddress, "127.0.0.1:" + std::to_string(m_port));
                field(menu->joinPlayerName, "Guest");
                field(menu->joinPassword, "smoke");
                scene->GetEntity(menu->joinConfirm)->GetComponent<Spot::UiButton>()->Click();
            }
            m_launched = true;
            return;
        }
        auto* network = GetModule<Spot::NetworkModule>()->GetSession();
        if (network && network != m_bound) {
            m_bound = network;
            network->GetMessages().On<Command>([this](Spot::NetPeerId, Command& command) {
                if (m_host && command.stage == 0) {
                    m_guestReady = true;
                } else if (!m_host && command.stage == 1) {
                    BeginMovement();
                    m_initialCrates = command.crates;
                } else if (!m_host && command.stage == 3) {
                    m_passed = true;
                    m_finished = Clock::now();
                    std::ofstream(m_output / "guest.result") << "PASS: both players moved and both props replicated\n";
                    Spot::Log::Info("Network smoke guest passed: two named players, movement and shared props");
                }
            });
            network->GetMessages().On<Report>([this](Spot::NetPeerId, Report& report) {
                m_report = std::move(report);
                m_received = true;
            });
        }
        if (room->GetState() != Gameplay::SessionState::Playing) {
            return;
        }
        if (m_host && !m_ready) {
            std::ofstream(m_output / "host.ready") << m_port;
            m_ready = true;
        }
        const auto players = Gameplay::PlayerInfos::GetAll(*scene);
        if (m_passed) {
            if (m_render && players.size() == 2) {
                GetInputState().OnEvent(Spot::KeyPressedEvent{Spot::Key::F2, false});
                auto* local = Gameplay::Players::GetPawn(*scene);
                for (const auto* player : players) {
                    const auto* other = scene->GetEntity(player->pawn);
                    if (local && other && !player->IsLocal()) {
                        const auto direction =
                            Spot::Normalize(other->GetTransform().GetWorldPosition() + Spot::Vector3{0.0f, 0.6f, 0.0f} -
                                            local->GetViewPoint().position);
                        if (auto* view = local->GetEntity().GetComponent<Gameplay::FirstPersonView>()) {
                            view->SetLook(Gameplay::PhysicsDrive::GetYaw(direction), std::asin(direction.y));
                        }
                    }
                }
            }
            const auto hold = m_render ? std::chrono::seconds(8) : std::chrono::seconds(1);
            if (Clock::now() - m_finished > hold) {
                if (!m_host || (network->GetHost().GetPeers().empty() && players.size() == 1)) {
                    if (m_host) {
                        std::ofstream(m_output / "host.result")
                            << "PASS: client scene matched, guest disconnected cleanly\n";
                    }
                    Quit();
                }
            }
            return;
        }
        if (players.size() != 2 || players[0]->pawn == Spot::EntityId::Invalid ||
            players[1]->pawn == Spot::EntityId::Invalid) {
            return;
        }
        if (players[0]->name != "Host" || players[1]->name != "Guest" || !Gameplay::PlayerInfos::GetLocal(*scene)) {
            Fail("The room did not replicate player names and local ownership");
            return;
        }
        const auto* local = Gameplay::Players::GetPawn(*scene);
        const auto* camera = Spot::FindMainCamera(*scene);
        if (!local || !camera || camera->GetEntity().GetId() != local->GetViewCamera()) {
            Fail("The main camera does not belong to the local player's pawn");
            return;
        }
        if (!m_host && !m_ready) {
            network->SendToServer(Command{0});
            m_ready = true;
        }
        if (m_host && !m_moving && m_guestReady) {
            for (const auto* player : players) {
                const auto* pawn = scene->GetEntity(player->pawn);
                if (!pawn) {
                    return;
                }
                m_starts.push_back({player->pawn, pawn->GetTransform().GetWorldPosition()});
            }
            BeginMovement();
            network->SendToAll(Command{1, m_initialCrates});
            SpawnCrate();
        }
        if (m_moving) {
            const double elapsed = std::chrono::duration<double>(Clock::now() - m_moveStarted).count();
            const Spot::Key key = m_host ? Spot::Key::W : Spot::Key::D;
            // A second real window may take focus; the smoke driver keeps its scripted control held.
            if (elapsed < 1.0) {
                GetInputState().OnEvent(Spot::KeyPressedEvent{key, false});
            } else {
                GetInputState().OnEvent(Spot::KeyReleasedEvent{key});
            }
            if (!m_host && elapsed > 1.3 && !m_spawned) {
                SpawnCrate();
                m_spawned = true;
            }
            if (!m_host && elapsed > 2.5 && !m_reported && CountCrates() >= m_initialCrates + 2) {
                Report report;
                for (const auto* player : players) {
                    const auto* pawn = scene->GetEntity(player->pawn);
                    if (!pawn) {
                        return;
                    }
                    report.players.push_back({player->pawn, pawn->GetTransform().GetWorldPosition()});
                }
                report.crates = CountCrates();
                network->SendToServer(std::move(report));
                m_reported = true;
            }
        }
        if (m_host && m_received && m_report.players.size() == 2) {
            for (std::size_t i = 0; i < m_report.players.size(); ++i) {
                const Sample& sample = m_report.players[i];
                const auto* pawn = scene->GetEntity(sample.pawn);
                if (!pawn || Spot::Distance(pawn->GetTransform().GetWorldPosition(), sample.position) > 0.5f ||
                    sample.pawn != m_starts[i].pawn || Spot::Distance(sample.position, m_starts[i].position) < 0.5f) {
                    const auto actual = pawn ? pawn->GetTransform().GetWorldPosition() : Spot::Vector3{};
                    Spot::Log::Error("Smoke player {}: id {} expected {}, start ({}, {}, {}), server ({}, {}, {}), "
                                     "client ({}, {}, {})",
                                     i, static_cast<std::uint64_t>(sample.pawn),
                                     static_cast<std::uint64_t>(m_starts[i].pawn), m_starts[i].position.x,
                                     m_starts[i].position.y, m_starts[i].position.z, actual.x, actual.y, actual.z,
                                     sample.position.x, sample.position.y, sample.position.z);
                    Fail("The client did not see both players move to their authoritative positions");
                    return;
                }
            }
            if (CountCrates() < m_initialCrates + 2 || m_report.crates < m_initialCrates + 2) {
                Fail("The server and client did not share both spawned props");
                return;
            }
            network->SendToAll(Command{3});
            m_passed = true;
            m_finished = Clock::now();
            Spot::Log::Info("Network smoke host passed: client positions match server, both props replicated");
        }
    }

private:
    void Fail(std::string_view reason) {
        Spot::Log::Error("Network smoke {} failed: {}", m_host ? "host" : "guest", reason);
        std::ofstream(m_output / (m_host ? "host.result" : "guest.result")) << "FAIL: " << reason << '\n';
        m_passed = false;
        Quit();
    }

    void BeginMovement() {
        m_moveStarted = Clock::now();
        m_moving = true;
        m_crate = GetAssets().FindByPath("prefabs/spawnables/props/crate.prefab").value_or(Spot::AssetId());
        m_initialCrates = CountCrates();
    }

    int CountCrates() const {
        int count = 0;
        if (Spot::Scene* scene = GetScene()) {
            scene->ForEachComponent<Spot::NetworkObject>([&](const Spot::NetworkObject& object) {
                if (object.GetEntity().GetPrefabAssetId() == m_crate) {
                    ++count;
                }
            });
        }
        return count;
    }

    void SpawnCrate() {
        auto* spawner = Gameplay::Players::FindComponent<PropSpawner>(*GetScene());
        if (!spawner) {
            Fail("The local player has no prop spawner");
            return;
        }
        const auto& items = spawner->GetSpawnables();
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].prefab.GetId() == m_crate) {
                spawner->Spawn(i);
                return;
            }
        }
        Fail("The prop catalog has no crate");
    }

    bool m_host;
    std::uint16_t m_port;
    std::string m_map;
    std::filesystem::path m_output;
    bool m_render;
    bool m_passed = false;
    bool m_launched = false;
    bool m_ready = false;
    bool m_guestReady = false;
    bool m_moving = false;
    bool m_spawned = false;
    bool m_reported = false;
    bool m_received = false;
    Spot::NetworkSession* m_bound = nullptr;
    Spot::AssetId m_crate;
    int m_initialCrates = 0;
    Report m_report;
    std::vector<Sample> m_starts;
    Clock::time_point m_started;
    Clock::time_point m_moveStarted;
    Clock::time_point m_finished;
};

} // namespace

int RunSandboxNetworkSmokePeer(int argc, char** argv) {
    if (argc != 7) {
        return EXIT_FAILURE;
    }
    int port = 0;
    if (!Spot::ParseConsoleValue(argv[3], port) || port < 1 || port > 65535) {
        return EXIT_FAILURE;
    }
    NetworkSmoke application(std::string_view(argv[2]) == "host", static_cast<std::uint16_t>(port), argv[4],
                             Spot::Utf8ToPath(argv[5]), std::string_view(argv[6]) == "render");
    const int result = application.Run();
    return result == EXIT_SUCCESS && application.IsPassed() ? EXIT_SUCCESS : EXIT_FAILURE;
}

int RunSandboxNetworkSmokeTest(bool render) {
    const auto output =
        Spot::GetExecutableDirectory() / ("network-smoke-" + std::to_string(Clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(output);
    const auto executable = Spot::PathToUtf8(Spot::GetExecutableDirectory() / "sandbox.exe");
    // Paths stay platform-independent, including when an installed sandbox has no .exe suffix.
    auto program = std::filesystem::path(executable);
#ifndef _WIN32
    program.replace_extension();
#endif
    for (const auto* map : {"empty", "playground"}) {
        std::uint16_t port = 0;
        {
            Spot::NetHost probe(Spot::CreateNetTransport());
            if (!probe.Listen(0, 1)) {
                return EXIT_FAILURE;
            }
            port = probe.GetPort();
        }
        const auto directory = output / map;
        std::filesystem::create_directories(directory);
        const auto arguments = [&](std::string role) {
            return std::vector<std::string>{Spot::PathToUtf8(program),
                                            "--network-smoke-peer",
                                            std::move(role),
                                            std::to_string(port),
                                            "scenes/" + std::string(map) + ".scene",
                                            Spot::PathToUtf8(directory),
                                            render ? "render" : "headless"};
        };
        auto host = Spot::Process::Start(arguments("host"));
        if (!host) {
            return EXIT_FAILURE;
        }
        std::ofstream hostOutput(directory / "host.stdout");
        const auto since = Clock::now();
        while (host->IsRunning() && !std::filesystem::exists(directory / "host.ready") &&
               Clock::now() - since < std::chrono::seconds(45)) {
            for (const auto& line : host->TakeOutput()) {
                hostOutput << line << '\n';
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        if (!host->IsRunning() || !std::filesystem::exists(directory / "host.ready")) {
            std::fprintf(stderr, "Network smoke host did not start; logs: %s\n", Spot::PathToUtf8(directory).c_str());
            return EXIT_FAILURE;
        }
        auto guest = Spot::Process::Start(arguments("guest"));
        if (!guest) {
            return EXIT_FAILURE;
        }
        std::ofstream guestOutput(directory / "guest.stdout");
        while ((host->IsRunning() || guest->IsRunning()) && Clock::now() - since < std::chrono::seconds(80)) {
            for (const auto& line : host->TakeOutput()) {
                hostOutput << line << '\n';
            }
            for (const auto& line : guest->TakeOutput()) {
                guestOutput << line << '\n';
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        if (host->IsRunning() || guest->IsRunning() || host->GetExitCode() != EXIT_SUCCESS ||
            guest->GetExitCode() != EXIT_SUCCESS) {
            std::fprintf(stderr, "Network smoke failed on %s; logs: %s\n", map, Spot::PathToUtf8(directory).c_str());
            return EXIT_FAILURE;
        }
        std::printf("Network smoke passed on %s: two processes joined through the menu, moved and shared props\n", map);
        std::fflush(stdout);
    }
    std::printf("Network smoke results: %s\n", Spot::PathToUtf8(output).c_str());
    return EXIT_SUCCESS;
}
