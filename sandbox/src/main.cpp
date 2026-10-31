#include "gameplay/core/user_settings.h"
#include "gameplay/network/session.h"
#include "network_smoke_test.h"
#include "session_config.h"
#include "smoke_test.h"

#include <spot.h>
#include <spot/scene/scripting/scripting_module.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace {

// What the sandbox's machines must agree on to play together; raise it when what they send each other changes.
constexpr std::uint16_t DefaultPort = 27015;

// What the command line asked for as the game starts.
struct Launch {
    enum class Kind { Play, Host, Join, Server };
    Kind kind = Kind::Play;
    std::uint16_t port = DefaultPort;
    std::optional<Spot::NetAddress> join;
    // For servers: the map and mode by name or asset path, the room's name and password, and how many may play.
    std::string map = "playground";
    std::string mode;
    std::string name = "Sandbox Server";
    std::string password;
    int players = 16;
};

// The asset a name stands for: its path, or the file of that name in the folder.
std::optional<Spot::AssetId> FindAsset(const Spot::AssetManager& assets, const std::string& name,
                                       std::string_view folder, std::string_view extension) {
    if (const auto found = assets.FindByPath(name)) {
        return found;
    }
    return assets.FindByPath(std::string(folder) + "/" + name + std::string(extension));
}

// Hosts or joins a session as the game starts, as the command line asked, and goes back to the main menu when a
// session it joined ends. A dedicated server quits when it cannot host.
class LaunchSession final : public Spot::Module {
public:
    explicit LaunchSession(Launch launch) : m_launch(std::move(launch)) {}

    bool HasFailed() const { return m_failed; }

protected:
    void OnStart() override {
        auto* session = GetApplication().GetModule<Gameplay::Session>();
        const Spot::AssetManager& assets = GetApplication().GetAssets();
        Gameplay::UserSettings preferences;
        preferences.Load(GetApplication().GetUserDataDirectory());
        if (m_launch.kind == Launch::Kind::Host) {
            Gameplay::SessionSettings settings;
            settings.port = m_launch.port;
            settings.playerName = preferences.playerName;
            if (const auto map = assets.FindByPath("scenes/playground.scene")) {
                settings.map = *map;
            }
            session->Host(settings);
        } else if (m_launch.kind == Launch::Kind::Join) {
            session->Join(m_launch.join->ToString(), preferences.playerName);
        } else if (m_launch.kind == Launch::Kind::Server) {
            Serve(*session, assets);
        }
    }

private:
    void Serve(Gameplay::Session& session, const Spot::AssetManager& assets) {
        const auto map = FindAsset(assets, m_launch.map, "scenes", ".scene");
        if (!map) {
            Stop("there is no map '" + m_launch.map + "'");
            return;
        }
        Gameplay::SessionSettings settings{.port = m_launch.port,
                                           .maxPlayers = m_launch.players,
                                           .name = m_launch.name,
                                           .password = m_launch.password,
                                           .playerName = "Server",
                                           .map = *map,
                                           .dedicated = true};
        if (!m_launch.mode.empty()) {
            const auto mode = FindAsset(assets, m_launch.mode, "prefabs/modes", ".prefab");
            if (!mode) {
                Stop("there is no mode '" + m_launch.mode + "'");
                return;
            }
            settings.mode = *mode;
        }
        if (!session.Host(settings)) {
            Stop(session.GetLastError());
            return;
        }
        Spot::Log::Info("Serving '{}' on port {}: map {}, up to {} players", m_launch.name, session.GetSettings().port,
                        m_launch.map, m_launch.players);
    }

    void Stop(const std::string& reason) {
        Spot::Log::Error("The server could not start: {}", reason);
        m_failed = true;
        GetApplication().Quit();
    }

    Launch m_launch;
    bool m_failed = false;
};

int Usage() {
    std::fprintf(stderr, "Usage: Sandbox [--smoke-test | --smoke-test-network [--render] | --host [port] |\n"
                         "                --join <address[:port]> |\n"
                         "                --server [--port <port>] [--map <name>] [--mode <name>] [--players <count>]\n"
                         "                         [--name <room name>] [--password <password>]]\n");
    return EXIT_FAILURE;
}

std::optional<std::uint16_t> ParsePort(const std::string& text) {
    const int port = std::atoi(text.c_str());
    return port > 0 && port <= 65535 ? std::optional(static_cast<std::uint16_t>(port)) : std::nullopt;
}

std::optional<Launch> ParseLaunch(int argc, char** argv) {
    Launch launch;
    if (argc == 1) {
        return launch;
    }
    const std::string_view first = argv[1];
    if (first == "--host" && argc <= 3) {
        launch.kind = Launch::Kind::Host;
        if (argc == 3) {
            const auto port = ParsePort(argv[2]);
            if (!port) {
                return std::nullopt;
            }
            launch.port = *port;
        }
        return launch;
    }
    if (first == "--join" && argc == 3) {
        launch.kind = Launch::Kind::Join;
        launch.join = Spot::NetAddress::Parse(argv[2], DefaultPort);
        return launch.join ? std::optional(launch) : std::nullopt;
    }
    if (first != "--server") {
        return std::nullopt;
    }
    launch.kind = Launch::Kind::Server;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string_view option = argv[i];
        const std::string value = argv[i + 1];
        if (option == "--port") {
            const auto port = ParsePort(value);
            if (!port) {
                return std::nullopt;
            }
            launch.port = *port;
        } else if (option == "--map") {
            launch.map = value;
        } else if (option == "--mode") {
            launch.mode = value;
        } else if (option == "--players") {
            launch.players = std::atoi(value.c_str());
        } else if (option == "--name") {
            launch.name = value;
        } else if (option == "--password") {
            launch.password = value;
        } else {
            return std::nullopt;
        }
    }
    // Options come in pairs.
    return argc % 2 == 0 ? std::optional(launch) : std::nullopt;
}

} // namespace

// Runs the game from its startup scene, as the settings the tools write beside the executable say.
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--smoke-test") {
        return RunSandboxSmokeTest();
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--smoke-test-network") {
        return RunSandboxNetworkSmokeTest(argc == 3 && std::string_view(argv[2]) == "--render");
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--network-smoke-peer") {
        return RunSandboxNetworkSmokePeer(argc, argv);
    }
    const std::optional<Launch> launch = ParseLaunch(argc, argv);
    if (!launch) {
        return Usage();
    }
    // A dedicated server shows nothing and plays no sound, keeping a steady pace without a screen to wait for.
    const bool server = launch->kind == Launch::Kind::Server;
    const auto settings = Spot::GameSettings::LoadBesideExecutable();
    const auto logs = Spot::GetUserDataDirectory("Spot Games", settings ? settings->name : "Sandbox") / "logs";
    const auto instance = std::chrono::system_clock::now().time_since_epoch().count();
    Spot::Application application(
        {.game = settings,
         .window = server ? Spot::WindowConfig{.title = "Sandbox Server", .headless = true} : Spot::WindowConfig{},
         .graphics = {.enabled = !server},
         .logFile = logs / ((server ? "server-" : "game-") + std::to_string(instance) + ".log"),
         .maxFrameRate = server ? 60 : 0});
    if (!server) {
        application.AddModule<Spot::DevToolsModule>();
    }
    application.AddModule<Spot::UiModule>();
    application.AddModule<Spot::RenderModule>();
    application.AddModule<Spot::AudioModule>(Spot::AudioConfig{.enabled = !server});
    application.AddModule<Spot::PhysicsModule>();
    application.AddModule<Spot::Physics2DModule>();
    application.AddModule<Spot::AnimationModule>();
    application.AddModule<Spot::ParticlesModule>();
    application.AddModule<Spot::NetworkModule>(SandboxNetworkConfig());
    application.AddModule<Gameplay::Session>();
    application.AddModule<Spot::ScriptingModule>();
    application.AddModule(Spot::CreateGameModule());
    const auto& launched = application.AddModule<LaunchSession>(*launch);
    const int code = application.Run();
    return launched.HasFailed() ? EXIT_FAILURE : code;
}
