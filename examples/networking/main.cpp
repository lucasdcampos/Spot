// Two machines keeping a scene in sync, without windows: a server moves a ball and counts the clients' greetings, and a
// client shows the ball where the server has it. By default both run in this process over a loopback network; run
// "--host <port>" in one terminal and "--join <address:port>" in another to use the real network.
#include <spot.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

constexpr std::uint32_t Protocol = 1;
constexpr Spot::AssetId LevelAsset{0x5370, 1};

// A message of the game's own: what a client says to the server's ball.
struct Greeting {
    static constexpr std::string_view NetName = "Example.Greeting";
    std::string text;

    void Serialize(Spot::Archive& archive) { archive.Field("text", text); }
};

// The level, as a file both machines load, so its objects are the same on each.
std::string MakeLevel() {
    Spot::Scene authoring;
    Spot::Entity& ball = authoring.CreateEntity("Ball");
    ball.AddComponent<Spot::NetworkObject>();
    ball.AddComponent<Spot::NetworkTransform>();
    return authoring.SaveToString();
}

struct Machine {
    std::shared_ptr<Spot::NetworkSession> session;
    Spot::Scene scene;
    Spot::NetworkSystem* system = nullptr;

    Machine(std::unique_ptr<Spot::NetHost> host, const std::string& level) {
        session = std::make_shared<Spot::NetworkSession>(std::move(host));
        scene.LoadFromString(level);
        scene.SetSourceAsset(LevelAsset);
        system = &scene.AddSystem<Spot::NetworkSystem>(session);
    }
};

std::unique_ptr<Spot::NetHost> MakeHost(std::unique_ptr<Spot::NetTransport> transport) {
    return std::make_unique<Spot::NetHost>(
        std::move(transport),
        Spot::NetHostConfig{.protocol = Protocol, .channels = Spot::NetworkSession::ChannelCount});
}

void MoveBall(Machine& server, float time) {
    if (Spot::Entity* ball = server.scene.FindEntity("Ball")) {
        ball->GetTransform().SetWorldPosition({std::cos(time) * 5.0f, 1.0f, std::sin(time) * 5.0f});
    }
}

Spot::Vector3 BallPosition(Machine& machine) {
    const Spot::Entity* ball = machine.scene.FindEntity("Ball");
    return ball ? ball->GetTransform().GetWorldPosition() : Spot::Vector3{};
}

// Both machines in this process; succeeds when the client's ball follows the server's.
int RunLocally(int frames) {
    Spot::RegisterNetworkComponents();
    const std::string level = MakeLevel();
    Spot::LoopbackNetwork network;

    auto serverHost = MakeHost(network.CreateTransport());
    if (!serverHost->Listen(7777, 4)) {
        return EXIT_FAILURE;
    }
    Machine server(std::move(serverHost), level);
    int greetings = 0;
    server.system->OnObjectMessage<Greeting>([&](Spot::NetworkObject&, Spot::NetPeerId, Greeting& greeting) {
        std::printf("The ball was told: %s\n", greeting.text.c_str());
        ++greetings;
    });

    auto clientHost = MakeHost(network.CreateTransport());
    clientHost->Connect({"localhost", 7777});
    Machine client(std::move(clientHost), level);

    constexpr float Step = 1.0f / 60.0f;
    bool greeted = false;
    for (int frame = 0; frame < frames; ++frame) {
        MoveBall(server, static_cast<float>(frame) * Step);
        server.scene.Update(Step);
        client.scene.Update(Step);
        if (!greeted && client.system->IsSynchronized()) {
            Spot::Entity* ball = client.scene.FindEntity("Ball");
            client.system->SendToServer(*ball->GetComponent<Spot::NetworkObject>(), Greeting{"hello"});
            greeted = true;
        }
        // At the pace of real frames, as the time objects are shown at is the server's clock.
        Spot::Platform::SleepFor(std::chrono::microseconds(16667));
    }
    // The client shows the ball a little in the past, so it is near where the server had it, not on it.
    const float apart = Spot::Distance(BallPosition(server), BallPosition(client));
    std::printf("The client's ball is %.2f m from the server's, %d greeting(s)\n", apart, greetings);
    return greeted && greetings == 1 && apart < 2.0f ? EXIT_SUCCESS : EXIT_FAILURE;
}

// One machine of two processes, until it is closed.
int RunOne(bool hosting, const std::string& where) {
    Spot::RegisterNetworkComponents();
    auto host = MakeHost(Spot::CreateNetTransport());
    if (hosting) {
        if (!host->Listen(static_cast<std::uint16_t>(std::atoi(where.c_str())), 8)) {
            return EXIT_FAILURE;
        }
    } else {
        const std::optional<Spot::NetAddress> address = Spot::NetAddress::Parse(where, 7777);
        if (!address || host->Connect(*address) == Spot::NetPeerId::Invalid) {
            return EXIT_FAILURE;
        }
    }
    Machine machine(std::move(host), MakeLevel());
    machine.session->OnPeerDisconnected.Connect([](Spot::NetPeerId, std::string_view reason) {
        std::printf("Disconnected: %.*s\n", static_cast<int>(reason.size()), reason.data());
    });
    constexpr float Step = 1.0f / 60.0f;
    for (int frame = 0;; ++frame) {
        if (hosting) {
            MoveBall(machine, static_cast<float>(frame) * Step);
        }
        machine.scene.Update(Step);
        if (frame % 60 == 0) {
            const Spot::Vector3 ball = BallPosition(machine);
            std::printf("Ball at %.2f %.2f %.2f\n", ball.x, ball.y, ball.z);
        }
        Spot::Platform::SleepFor(std::chrono::milliseconds(16));
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--host") {
        return RunOne(true, argv[2]);
    }
    if (argc == 3 && std::string_view(argv[1]) == "--join") {
        return RunOne(false, argv[2]);
    }
    // The tests run it for a number of frames: "--frames <count>".
    const int frames = argc == 3 && std::string_view(argv[1]) == "--frames" ? std::atoi(argv[2]) : 600;
    return RunLocally(frames);
}
