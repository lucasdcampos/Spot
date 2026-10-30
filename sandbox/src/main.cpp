#include "smoke_test.h"

#include <spot.h>
#include <spot/scene/scripting/scripting_module.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace {

// What the sandbox's machines must agree on to play together; raise it when what they send each other changes.
constexpr std::uint32_t Protocol = 1;
constexpr std::uint16_t DefaultPort = 27015;

// Hosts or joins a session as the game starts, as the command line asked.
class LaunchSession final : public Spot::Module {
public:
    LaunchSession(std::optional<std::uint16_t> host, std::optional<Spot::NetAddress> join)
        : m_host(host), m_join(join) {}

protected:
    void OnStart() override {
        auto* network = GetApplication().GetModule<Spot::NetworkModule>();
        if (m_host) {
            network->Host(*m_host, 15);
        } else if (m_join) {
            network->Join(*m_join);
        }
    }

private:
    std::optional<std::uint16_t> m_host;
    std::optional<Spot::NetAddress> m_join;
};

int Usage() {
    std::fprintf(stderr, "Usage: Sandbox [--smoke-test | --host [port] | --join <address[:port]>]\n");
    return EXIT_FAILURE;
}

} // namespace

// Runs the game from its startup scene, as the settings the tools write beside the executable say.
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--smoke-test") {
        return RunSandboxSmokeTest();
    }
    std::optional<std::uint16_t> host;
    std::optional<Spot::NetAddress> join;
    if (argc >= 2 && std::string_view(argv[1]) == "--host" && argc <= 3) {
        const int port = argc == 3 ? std::atoi(argv[2]) : DefaultPort;
        if (port <= 0 || port > 65535) {
            return Usage();
        }
        host = static_cast<std::uint16_t>(port);
    } else if (argc == 3 && std::string_view(argv[1]) == "--join") {
        join = Spot::NetAddress::Parse(argv[2], DefaultPort);
        if (!join) {
            return Usage();
        }
    } else if (argc != 1) {
        return Usage();
    }
    Spot::Application application({.game = Spot::GameSettings::LoadBesideExecutable()});
    application.AddModule<Spot::DevToolsModule>();
    application.AddModule<Spot::UiModule>();
    application.AddModule<Spot::RenderModule>();
    application.AddModule<Spot::AudioModule>();
    application.AddModule<Spot::PhysicsModule>();
    application.AddModule<Spot::Physics2DModule>();
    application.AddModule<Spot::AnimationModule>();
    application.AddModule<Spot::ParticlesModule>();
    application.AddModule<Spot::NetworkModule>(Spot::NetworkModuleConfig{.protocol = Protocol});
    application.AddModule<Spot::ScriptingModule>();
    application.AddModule(Spot::CreateGameModule());
    application.AddModule<LaunchSession>(host, join);
    return application.Run();
}
