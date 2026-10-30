#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

std::span<const std::byte> Bytes(std::string_view text) {
    return std::as_bytes(std::span(text.data(), text.size()));
}

std::vector<std::byte> Copy(std::string_view text) {
    const auto bytes = Bytes(text);
    return {bytes.begin(), bytes.end()};
}

std::string Text(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

struct ChatMessage {
    static constexpr std::string_view NetName = "Tests.Chat";
    std::string text;
    int count = 0;
    Spot::Vector3 where;
    std::vector<float> values;
    Spot::EntityId target = Spot::EntityId::Invalid;

    void Serialize(Spot::Archive& archive) {
        archive.Field("text", text);
        archive.Field("count", count);
        archive.Field("where", where);
        archive.Field("values", values);
        archive.Field("target", target);
    }
};

struct PingMessage {
    static constexpr std::string_view NetName = "Tests.Ping";
    std::uint32_t sequence = 0;

    void Serialize(Spot::Archive& archive) { archive.Field("sequence", sequence); }
};

// Polls both ends until nothing more happens, gathering what each was told.
struct Pair {
    std::vector<Spot::NetEvent> serverEvents, clientEvents;

    void Pump(Spot::NetHost& server, Spot::NetHost& client, int rounds = 4) {
        for (int i = 0; i < rounds; ++i) {
            for (Spot::NetEvent& event : server.Poll()) {
                serverEvents.push_back(std::move(event));
            }
            for (Spot::NetEvent& event : client.Poll()) {
                clientEvents.push_back(std::move(event));
            }
        }
    }
};

bool Has(const std::vector<Spot::NetEvent>& events, Spot::NetEvent::Type type) {
    return std::any_of(events.begin(), events.end(),
                       [type](const Spot::NetEvent& event) { return event.type == type; });
}

} // namespace

TEST_CASE("Bits read back as they were written", "[net]") {
    Spot::BitWriter writer;
    writer.WriteBits(5, 3);
    writer.WriteBool(true);
    writer.WriteVarUInt(300);
    writer.WriteVarInt(-123456789);
    writer.WriteFloat(-2.5f);
    writer.WriteUInt64(0x0123456789ABCDEFull);
    writer.WriteString("hello");
    writer.WriteDouble(1.0 / 3.0);
    const std::size_t bits = writer.GetBitCount();
    const std::vector<std::byte> bytes = writer.TakeBytes();
    CHECK(bytes.size() == (bits + 7) / 8);

    Spot::BitReader reader(bytes);
    CHECK(reader.ReadBits(3) == 5);
    CHECK(reader.ReadBool());
    CHECK(reader.ReadVarUInt() == 300);
    CHECK(reader.ReadVarInt() == -123456789);
    CHECK(reader.ReadFloat() == -2.5f);
    CHECK(reader.ReadUInt64() == 0x0123456789ABCDEFull);
    CHECK(reader.ReadString() == "hello");
    CHECK(reader.ReadDouble() == 1.0 / 3.0);
    CHECK(reader.IsValid());

    SECTION("Reading past the end fails the reader instead of reading garbage") {
        reader.ReadBits(32);
        CHECK_FALSE(reader.IsValid());
        CHECK(reader.ReadVarUInt() == 0);
    }
    SECTION("A size larger than what is left fails") {
        Spot::BitWriter lying;
        lying.WriteVarUInt(1000000);
        const std::vector<std::byte> data = lying.TakeBytes();
        Spot::BitReader small(data);
        CHECK(small.ReadString().empty());
        CHECK_FALSE(small.IsValid());
    }
}

TEST_CASE("Quantized values stay within their precision", "[net]") {
    for (const float value : {-100.0f, -3.21f, 0.0f, 42.42f, 100.0f}) {
        Spot::BitWriter writer;
        Spot::NetQuantize::WriteFloat(writer, value, -100.0f, 100.0f, 16);
        const auto bytes = writer.TakeBytes();
        Spot::BitReader reader(bytes);
        CHECK(std::abs(Spot::NetQuantize::ReadFloat(reader, -100.0f, 100.0f, 16) - value) <= 200.0f / 65535.0f);
    }
    CHECK(Spot::NetQuantize::ToBits(1000.0f, 0.0f, 1.0f, 8) == 255);
    CHECK(Spot::NetQuantize::ToBits(-1.0f, 0.0f, 1.0f, 8) == 0);

    Spot::BitWriter angles;
    Spot::NetQuantize::WriteAngle(angles, -0.5f, 16);
    Spot::NetQuantize::WriteAngle(angles, 7.0f, 16);
    const auto angleBytes = angles.TakeBytes();
    Spot::BitReader angleReader(angleBytes);
    CHECK(Spot::NetQuantize::ReadAngle(angleReader, 16) == Catch::Approx(-0.5f).margin(0.001f));
    CHECK(Spot::NetQuantize::ReadAngle(angleReader, 16) == Catch::Approx(7.0f - 2.0f * 3.14159265f).margin(0.001f));

    for (const Spot::Quaternion rotation :
         {Spot::Quaternion::Identity(), Spot::Quaternion::FromEuler({0.3f, -2.0f, 1.1f}),
          Spot::Quaternion{-0.5f, 0.5f, -0.5f, -0.5f}}) {
        Spot::BitWriter writer;
        Spot::NetQuantize::WriteRotation(writer, rotation);
        CHECK(writer.GetBitCount() == 2 + 3 * 15);
        const auto bytes = writer.TakeBytes();
        Spot::BitReader reader(bytes);
        const Spot::Quaternion read = Spot::NetQuantize::ReadRotation(reader);
        // q and -q are the same rotation.
        const float dot =
            std::abs(read.x * rotation.x + read.y * rotation.y + read.z * rotation.z + read.w * rotation.w);
        CHECK(dot == Catch::Approx(1.0f).margin(1e-4f));
    }
}

TEST_CASE("Messages are described as saved data and reach their handlers", "[net]") {
    ChatMessage sent{
        .text = "hi", .count = -7, .where = {1.0f, 2.0f, 3.0f}, .values = {0.5f, 0.25f}, .target = Spot::EntityId{42}};
    const std::vector<std::byte> data = Spot::EncodeNetMessage(sent);

    Spot::NetMessageHandlers handlers;
    ChatMessage received;
    int pings = 0;
    handlers.On<ChatMessage>([&](Spot::NetPeerId, ChatMessage& message) { received = message; });
    handlers.On<PingMessage>([&](Spot::NetPeerId, PingMessage&) { ++pings; });

    CHECK(handlers.Dispatch(Spot::NetPeerId{1}, data));
    CHECK(received.text == "hi");
    CHECK(received.count == -7);
    CHECK(received.where == Spot::Vector3{1.0f, 2.0f, 3.0f});
    CHECK(received.values == std::vector<float>{0.5f, 0.25f});
    CHECK(received.target == Spot::EntityId{42});
    CHECK(pings == 0);

    SECTION("Truncated data calls nothing") {
        received = {};
        CHECK_FALSE(handlers.Dispatch(Spot::NetPeerId{1}, std::span(data).first(data.size() - 2)));
        CHECK(received.text.empty());
    }
    SECTION("Unknown messages are refused") {
        Spot::NetMessageHandlers none;
        CHECK_FALSE(none.Dispatch(Spot::NetPeerId{1}, data));
    }
    SECTION("Entities cross through a mapper") {
        struct Mapper final : Spot::NetEntityMapper {
            std::uint64_t ToWire(Spot::EntityId entity) const override {
                return static_cast<std::uint64_t>(entity) + 1000;
            }
            Spot::EntityId FromWire(std::uint64_t id) const override { return Spot::EntityId{id - 900}; }
        } mapper;
        const auto mapped = Spot::EncodeNetMessage(sent, &mapper);
        CHECK(handlers.Dispatch(Spot::NetPeerId{1}, mapped, &mapper));
        CHECK(received.target == Spot::EntityId{142});
    }
}

TEST_CASE("Addresses parse with and without ports", "[net]") {
    CHECK(Spot::NetAddress::Parse("example.com", 7777) == Spot::NetAddress{"example.com", 7777});
    CHECK(Spot::NetAddress::Parse(" 192.168.0.4:2000 ", 7777) == Spot::NetAddress{"192.168.0.4", 2000});
    CHECK(Spot::NetAddress::Parse("[::1]:3000", 7777) == Spot::NetAddress{"::1", 3000});
    CHECK(Spot::NetAddress::Parse("::1", 7777) == Spot::NetAddress{"::1", 7777});
    CHECK_FALSE(Spot::NetAddress::Parse("host:0", 7777));
    CHECK_FALSE(Spot::NetAddress::Parse("host:70000", 7777));
    CHECK_FALSE(Spot::NetAddress::Parse("host:", 7777));
    CHECK_FALSE(Spot::NetAddress::Parse(":2000", 7777));
    CHECK_FALSE(Spot::NetAddress::Parse("", 7777));
    CHECK(Spot::NetAddress{"::1", 3000}.ToString() == "[::1]:3000");
    CHECK(Spot::NetAddress{"10.0.0.1", 80}.ToString() == "10.0.0.1:80");
}

TEST_CASE("A client connects to a server over loopback and they exchange messages", "[net]") {
    Spot::LoopbackNetwork network;
    Spot::NetHost server(network.CreateTransport(), {.protocol = 7, .channels = 2});
    Spot::NetHost client(network.CreateTransport(), {.protocol = 7, .channels = 2});
    REQUIRE(server.Listen(4000, 4));
    std::string introducedAs;
    server.onConnecting = [&](Spot::NetPeerId, std::span<const std::byte> hello) {
        introducedAs = Text(hello);
        return std::string();
    };

    const Spot::NetPeerId toServer = client.Connect({"localhost", 4000}, Bytes("Ana"));
    REQUIRE(toServer != Spot::NetPeerId::Invalid);
    CHECK_FALSE(client.IsConnected(toServer));
    Pair pair;
    pair.Pump(server, client);
    CHECK(introducedAs == "Ana");
    REQUIRE(Has(pair.serverEvents, Spot::NetEvent::Type::Connected));
    REQUIRE(Has(pair.clientEvents, Spot::NetEvent::Type::Connected));
    CHECK(Text(pair.serverEvents[0].data) == "Ana");
    const Spot::NetPeerId toClient = pair.serverEvents[0].peer;
    CHECK(server.GetPeers() == std::vector{toClient});
    CHECK(client.IsConnected(toServer));

    pair = {};
    client.Send(toServer, 1, Bytes("up"), Spot::NetDelivery::Reliable);
    server.SendToAll(0, Bytes("down"), Spot::NetDelivery::Unreliable);
    pair.Pump(server, client);
    REQUIRE(pair.serverEvents.size() == 1);
    CHECK(pair.serverEvents[0].channel == 1);
    CHECK(Text(pair.serverEvents[0].data) == "up");
    REQUIRE(pair.clientEvents.size() == 1);
    CHECK(pair.clientEvents[0].channel == 0);
    CHECK(Text(pair.clientEvents[0].data) == "down");
    CHECK(server.GetStats(toClient).bytesReceived >= 2);

    SECTION("Kicking tells the client why") {
        pair = {};
        server.Disconnect(toClient, "Kicked for testing");
        pair.Pump(server, client);
        REQUIRE(pair.clientEvents.size() == 1);
        CHECK(pair.clientEvents[0].type == Spot::NetEvent::Type::Disconnected);
        CHECK(pair.clientEvents[0].reason == "Kicked for testing");
        CHECK(Has(pair.serverEvents, Spot::NetEvent::Type::Disconnected));
        CHECK(server.GetPeers().empty());
    }
    SECTION("A client leaving is seen by the server") {
        pair = {};
        client.Disconnect(toServer, "Bye");
        pair.Pump(server, client);
        REQUIRE(pair.serverEvents.size() == 1);
        CHECK(pair.serverEvents[0].type == Spot::NetEvent::Type::Disconnected);
        CHECK(pair.serverEvents[0].reason == "Bye");
    }
}

TEST_CASE("Servers refuse clients with the reason they give", "[net]") {
    Spot::LoopbackNetwork network;
    Spot::NetHost server(network.CreateTransport(), {.protocol = 7});
    REQUIRE(server.Listen(4000, 4));
    server.onConnecting = [](Spot::NetPeerId, std::span<const std::byte> hello) {
        return Text(hello) == "secret" ? std::string() : std::string("Wrong password");
    };

    SECTION("By the game's rule") {
        Spot::NetHost client(network.CreateTransport(), {.protocol = 7});
        client.Connect({"localhost", 4000}, Bytes("guess"));
        Pair pair;
        pair.Pump(server, client);
        CHECK(pair.serverEvents.empty());
        REQUIRE(pair.clientEvents.size() == 1);
        CHECK(pair.clientEvents[0].type == Spot::NetEvent::Type::Disconnected);
        CHECK(pair.clientEvents[0].reason == "Wrong password");
    }
    SECTION("By the protocol") {
        Spot::NetHost client(network.CreateTransport(), {.protocol = 8});
        client.Connect({"localhost", 4000}, Bytes("secret"));
        Pair pair;
        pair.Pump(server, client);
        CHECK(pair.serverEvents.empty());
        REQUIRE(pair.clientEvents.size() == 1);
        CHECK(pair.clientEvents[0].reason == "The server runs another version of the game");
    }
    SECTION("When nobody listens") {
        Spot::NetHost client(network.CreateTransport(), {.protocol = 7});
        client.Connect({"localhost", 4001});
        Pair pair;
        pair.Pump(server, client);
        REQUIRE(pair.clientEvents.size() == 1);
        CHECK(pair.clientEvents[0].reason == "Could not connect to the server");
    }
    SECTION("When the server is full") {
        Spot::NetHost small(network.CreateTransport());
        REQUIRE(small.Listen(4100, 1));
        Spot::NetHost first(network.CreateTransport()), second(network.CreateTransport());
        first.Connect({"localhost", 4100});
        Pair a;
        a.Pump(small, first);
        CHECK(Has(a.clientEvents, Spot::NetEvent::Type::Connected));
        second.Connect({"localhost", 4100});
        Pair b;
        b.Pump(small, second);
        REQUIRE(b.clientEvents.size() == 1);
        CHECK(b.clientEvents[0].type == Spot::NetEvent::Type::Disconnected);
    }
}

TEST_CASE("A server going away disconnects its clients", "[net]") {
    Spot::LoopbackNetwork network;
    auto server = std::make_unique<Spot::NetHost>(network.CreateTransport());
    Spot::NetHost client(network.CreateTransport());
    REQUIRE(server->Listen(4000, 4));
    client.Connect({"localhost", 4000});
    Pair pair;
    pair.Pump(*server, client);
    REQUIRE(Has(pair.clientEvents, Spot::NetEvent::Type::Connected));

    server.reset();
    const std::vector<Spot::NetEvent> events = client.Poll();
    REQUIRE(events.size() == 1);
    CHECK(events[0].type == Spot::NetEvent::Type::Disconnected);
    CHECK(events[0].reason == "The server shut down");
}

TEST_CASE("Simulated conditions delay, lose and keep reliable order", "[net]") {
    Spot::LoopbackNetwork network;
    double now = 0.0;
    const auto clock = [&now] { return now; };
    auto conditioned = std::make_unique<Spot::ConditionedNetTransport>(
        network.CreateTransport(), Spot::NetConditions{.latency = 0.1f, .jitter = 0.05f}, clock);
    Spot::ConditionedNetTransport* conditions = conditioned.get();
    Spot::NetHost client(std::move(conditioned));
    Spot::NetHost server(network.CreateTransport());
    REQUIRE(server.Listen(4000, 4));

    const Spot::NetPeerId toServer = client.Connect({"localhost", 4000});
    Pair pair;
    for (int step = 0; step < 40 && !client.IsConnected(toServer); ++step) {
        now += 0.01;
        pair.Pump(server, client, 1);
    }
    REQUIRE(client.IsConnected(toServer));

    pair = {};
    for (int i = 0; i < 20; ++i) {
        client.Send(toServer, 0, Bytes(std::to_string(i)), Spot::NetDelivery::Reliable);
    }
    pair.Pump(server, client, 1);
    CHECK(pair.serverEvents.empty());
    now += 0.2;
    pair.Pump(server, client, 2);
    REQUIRE(pair.serverEvents.size() == 20);
    for (int i = 0; i < 20; ++i) {
        CHECK(Text(pair.serverEvents[i].data) == std::to_string(i));
    }

    conditions->SetConditions({.loss = 1.0f});
    pair = {};
    client.Send(toServer, 0, Bytes("lost"), Spot::NetDelivery::Unreliable);
    client.Send(toServer, 0, Bytes("kept"), Spot::NetDelivery::Reliable);
    now += 0.01;
    pair.Pump(server, client, 2);
    REQUIRE(pair.serverEvents.size() == 1);
    CHECK(Text(pair.serverEvents[0].data) == "kept");
}

TEST_CASE("Servers are found on the local network by their protocol", "[net]") {
    Spot::LoopbackNetwork network;
    Spot::NetDiscoveryResponder first(network.CreateDatagramSocket(), 7);
    Spot::NetDiscoveryResponder second(network.CreateDatagramSocket(), 7);
    Spot::NetDiscoveryResponder other(network.CreateDatagramSocket(), 9);
    REQUIRE(first.Listen(5000));
    REQUIRE(second.Listen(5000));
    REQUIRE(other.Listen(5000));
    first.answer = [] { return Copy("first"); };
    second.answer = [] { return Copy("second"); };
    other.answer = [] { return Copy("other"); };

    Spot::NetDiscovery discovery(network.CreateDatagramSocket(), 7);
    REQUIRE(discovery.Query(5000));
    first.Update();
    second.Update();
    other.Update();
    discovery.Update();
    std::vector<std::string> found;
    for (const Spot::NetDiscoveryAnswer& answer : discovery.GetAnswers()) {
        found.push_back(Text(answer.data));
    }
    std::sort(found.begin(), found.end());
    CHECK(found == std::vector<std::string>{"first", "second"});

    // Asking again refreshes the answers instead of repeating them.
    REQUIRE(discovery.Query(5000));
    first.Update();
    second.Update();
    discovery.Update();
    CHECK(discovery.GetAnswers().size() == 2);
}

TEST_CASE("The platform transport connects over the local machine", "[net]") {
    Spot::NetHost server(Spot::CreateNetTransport(), {.protocol = 3});
    REQUIRE(server.Listen(0, 2));
    REQUIRE(server.GetPort() != 0);
    Spot::NetHost client(Spot::CreateNetTransport(), {.protocol = 3});
    const Spot::NetPeerId toServer = client.Connect({"127.0.0.1", server.GetPort()}, Bytes("hello"));
    REQUIRE(toServer != Spot::NetPeerId::Invalid);

    Pair pair;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!client.IsConnected(toServer) && std::chrono::steady_clock::now() < deadline) {
        pair.Pump(server, client, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(client.IsConnected(toServer));
    REQUIRE(Has(pair.serverEvents, Spot::NetEvent::Type::Connected));

    pair = {};
    client.Send(toServer, 0, Bytes("over udp"), Spot::NetDelivery::Reliable);
    while (pair.serverEvents.empty() && std::chrono::steady_clock::now() < deadline) {
        pair.Pump(server, client, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(pair.serverEvents.size() == 1);
    CHECK(Text(pair.serverEvents[0].data) == "over udp");
    CHECK(server.GetAddress(pair.serverEvents[0].peer)->host == "127.0.0.1");
}
