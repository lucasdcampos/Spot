#include "log_capture.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using Spot::Entity;
using Spot::EntityId;
using Spot::Scene;

namespace {

enum class Team { Red, Blue };

struct Waypoint {
    Spot::Vector2 position;
    float wait = 0.0f;

    void Serialize(Spot::Archive& archive) {
        archive.Field("position", position);
        archive.Field("wait", wait);
    }

    friend bool operator==(const Waypoint&, const Waypoint&) = default;
};

// Uses every kind of field the archive supports.
class Everything final : public Spot::Component {
public:
    bool flag = false;
    int count = 0;
    std::uint32_t mask = 0;
    std::int64_t big = 0;
    std::uint64_t huge = 0;
    float speed = 0.0f;
    double precise = 0.0;
    std::string label;
    Spot::Vector2 v2;
    Spot::Vector3 v3;
    Spot::Vector4 v4;
    Spot::Quaternion rotation;
    Spot::Color color;
    Team team = Team::Red;
    EntityId target = EntityId::Invalid;
    Waypoint home;
    std::vector<Waypoint> route;
    std::vector<std::string> names;
    int starts = 0;

protected:
    void OnStart() override { ++starts; }

    void Serialize(Spot::Archive& archive) override {
        archive.Field("flag", flag);
        archive.Field("count", count);
        archive.Field("mask", mask);
        archive.Field("big", big);
        archive.Field("huge", huge);
        archive.Field("speed", speed);
        archive.Field("precise", precise);
        archive.Field("label", label);
        archive.Field("v2", v2);
        archive.Field("v3", v3);
        archive.Field("v4", v4);
        archive.Field("rotation", rotation);
        archive.Field("color", color);
        archive.Field("team", team);
        archive.Field("target", target);
        archive.Field("home", home);
        archive.Field("route", route);
        archive.Field("names", names);
    }
};

class Speed final : public Spot::Component {
public:
    float value = 1.0f;

protected:
    void Serialize(Spot::Archive& archive) override { archive.Field("value", value); }
};

class Unregistered final : public Spot::Component {};

void RegisterTestComponents() {
    Spot::RegisterComponent<Everything>("Everything");
    Spot::RegisterComponent<Speed>("Speed");
}

} // namespace

TEST_CASE("Components register under a name", "[scene][serialization]") {
    RegisterTestComponents();

    CHECK(Spot::IsComponentRegistered<Speed>());
    CHECK_FALSE(Spot::IsComponentRegistered<Unregistered>());
}

TEST_CASE("Scenes save and load entities, hierarchy and components", "[scene][serialization]") {
    RegisterTestComponents();

    Scene original;
    Entity& player = original.CreateEntity("Player");
    player.AddTag("Player");
    player.AddTag("Hero");
    player.GetTransform().SetPosition({1.0f, 2.0f, 3.0f});
    player.GetTransform().SetRotation2D(0.5f);
    player.GetTransform().SetScale({2.0f, 2.0f, 1.0f});
    Entity& sword = player.CreateChild("Sword");
    sword.SetActive(false);
    Entity& enemy = original.CreateEntity("Enemy");

    auto& saved = player.AddComponent<Everything>();
    saved.flag = true;
    saved.count = -7;
    saved.mask = 0xF0F0u;
    saved.big = -5'000'000'000;
    saved.huge = 9'000'000'000'000'000'000u;
    saved.speed = 2.5f;
    saved.precise = 0.1;
    saved.label = "café";
    saved.v2 = {1.0f, 2.0f};
    saved.v3 = {3.0f, 4.0f, 5.0f};
    saved.v4 = {6.0f, 7.0f, 8.0f, 9.0f};
    saved.rotation = Spot::Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, 1.0f);
    saved.color = {0.1f, 0.2f, 0.3f, 0.4f};
    saved.team = Team::Blue;
    saved.target = enemy.GetId();
    saved.home = {{10.0f, 20.0f}, 1.5f};
    saved.route = {{{1.0f, 1.0f}, 0.0f}, {{2.0f, 2.0f}, 3.0f}};
    saved.names = {"a", "b"};
    sword.AddComponent<Speed>().value = 4.0f;
    sword.GetComponent<Speed>()->SetEnabled(false);

    const std::string text = original.SaveToString();

    Scene loaded;
    REQUIRE(loaded.LoadFromString(text));
    REQUIRE(loaded.GetEntityCount() == 3);

    Entity* loadedPlayer = loaded.FindEntity("Player");
    Entity* loadedSword = loaded.FindEntity("Sword");
    Entity* loadedEnemy = loaded.FindEntity("Enemy");
    REQUIRE(loadedPlayer);
    REQUIRE(loadedSword);
    REQUIRE(loadedEnemy);

    CHECK(loadedPlayer->HasTag("Player"));
    CHECK(loadedPlayer->HasTag("Hero"));
    CHECK(loadedSword->GetParent() == loadedPlayer);
    CHECK_FALSE(loadedSword->IsActiveSelf());
    CHECK(loadedPlayer->GetTransform().GetPosition() == Spot::Vector3{1.0f, 2.0f, 3.0f});
    CHECK(loadedPlayer->GetTransform().GetRotation() == player.GetTransform().GetRotation());
    CHECK(loadedPlayer->GetTransform().GetScale() == Spot::Vector3{2.0f, 2.0f, 1.0f});

    const auto* everything = loadedPlayer->GetComponent<Everything>();
    REQUIRE(everything);
    CHECK(everything->flag);
    CHECK(everything->count == -7);
    CHECK(everything->mask == 0xF0F0u);
    CHECK(everything->big == -5'000'000'000);
    CHECK(everything->huge == 9'000'000'000'000'000'000u);
    CHECK(everything->speed == 2.5f);
    CHECK(everything->precise == 0.1);
    CHECK(everything->label == "café");
    CHECK(everything->v2 == saved.v2);
    CHECK(everything->v3 == saved.v3);
    CHECK(everything->v4 == saved.v4);
    CHECK(everything->rotation == saved.rotation);
    CHECK(everything->color == saved.color);
    CHECK(everything->team == Team::Blue);
    CHECK(everything->home == saved.home);
    CHECK(everything->route == saved.route);
    CHECK(everything->names == saved.names);
    // References between saved entities point to the loaded copies.
    CHECK(everything->target == loadedEnemy->GetId());
    CHECK(everything->target != enemy.GetId());

    const auto* speed = loadedSword->GetComponent<Speed>();
    REQUIRE(speed);
    CHECK(speed->value == 4.0f);
    CHECK_FALSE(speed->IsEnabled());

    // Loaded components start on the next update, like any new component.
    CHECK(everything->starts == 0);
    loaded.Update(0.016f);
    CHECK(everything->starts == 1);
}

TEST_CASE("Saved scenes are readable JSON", "[scene][serialization]") {
    RegisterTestComponents();
    Scene scene;
    scene.CreateEntity("Player").AddComponent<Speed>().value = 3.0f;

    const std::string text = scene.SaveToString();

    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("\"format\": \"spot-scene\""));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("\"name\": \"Player\""));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("\"type\": \"Speed\""));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("\"value\": 3.0"));
}

TEST_CASE("Missing fields keep their defaults and wrong ones are skipped", "[scene][serialization]") {
    RegisterTestComponents();
    auto capture = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({capture});

    Scene scene;
    REQUIRE(scene.LoadFromString(R"({
        "format": "spot-scene",
        "version": 1,
        "entities": [
            {"name": "Bare"},
            {"name": "Odd", "position": "here", "components": [
                {"type": "Speed", "data": {"value": "fast"}},
                {"type": "Missing"}
            ]}
        ]
    })"));

    Entity* bare = scene.FindEntity("Bare");
    REQUIRE(bare);
    CHECK(bare->IsActiveSelf());
    CHECK(bare->GetTransform().GetScale() == Spot::Vector3{1.0f, 1.0f, 1.0f});
    CHECK(bare->GetParent() == nullptr);

    Entity* odd = scene.FindEntity("Odd");
    REQUIRE(odd);
    CHECK(odd->GetTransform().GetPosition() == Spot::Vector3{});
    REQUIRE(odd->GetComponent<Speed>());
    CHECK(odd->GetComponent<Speed>()->value == 1.0f);
    CHECK(odd->GetComponents<Spot::Component>().size() == 1);

    std::vector<std::string> warnings;
    for (const auto& record : capture->GetRecords()) {
        if (record.level == Spot::LogLevel::Warning) {
            warnings.push_back(record.message);
        }
    }
    REQUIRE(warnings.size() == 3);
    CHECK_THAT(warnings[0], Catch::Matchers::ContainsSubstring("'position'"));
    CHECK_THAT(warnings[1], Catch::Matchers::ContainsSubstring("'value'"));
    CHECK_THAT(warnings[2], Catch::Matchers::ContainsSubstring("'Missing'"));
}

TEST_CASE("Invalid scene text loads nothing", "[scene][serialization]") {
    auto capture = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({capture});
    Scene scene;

    CHECK_FALSE(scene.LoadFromString("not json"));
    CHECK_FALSE(scene.LoadFromString(R"({"format": "something else", "entities": []})"));
    CHECK_FALSE(scene.LoadFromString(R"({"format": "spot-scene", "version": 99, "entities": []})"));
    CHECK_FALSE(scene.LoadFromString(R"({"format": "spot-scene", "version": 1, "entities": [1, 2]})"));
    CHECK_FALSE(scene.LoadFromString(R"({"format": 3, "version": "1"})"));

    CHECK(scene.GetEntityCount() == 0);
    CHECK(capture->GetRecords().size() == 5);
}

TEST_CASE("Unregistered components are left out with a warning", "[scene][serialization]") {
    RegisterTestComponents();
    auto capture = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({capture});

    Scene scene;
    Entity& entity = scene.CreateEntity("Thing");
    entity.AddComponent<Unregistered>();
    entity.AddComponent<Speed>();

    Scene loaded;
    REQUIRE(loaded.LoadFromString(scene.SaveToString()));

    Entity* thing = loaded.FindEntity("Thing");
    REQUIRE(thing);
    CHECK(thing->HasComponent<Speed>());
    CHECK_FALSE(thing->HasComponent<Unregistered>());
    REQUIRE(capture->GetRecords().size() == 1);
    CHECK_THAT(capture->GetRecords()[0].message, Catch::Matchers::ContainsSubstring("not registered"));
}

TEST_CASE("A saved entity loads as many copies as needed", "[scene][serialization]") {
    RegisterTestComponents();
    Scene scene;
    Entity& parent = scene.CreateEntity("Parent");
    Entity& turret = parent.CreateChild("Turret");
    turret.CreateChild("Barrel").AddComponent<Speed>().value = 9.0f;
    turret.GetTransform().SetPosition({5.0f, 0.0f, 0.0f});

    const std::string text = turret.SaveToString();
    REQUIRE(scene.LoadFromString(text));
    REQUIRE(scene.LoadFromString(text));

    // The copies are roots, since their parent was not saved with them.
    std::vector<Entity*> turrets;
    for (Entity* root : scene.GetRootEntities()) {
        if (root->GetName() == "Turret") {
            turrets.push_back(root);
        }
    }
    REQUIRE(turrets.size() == 2);
    CHECK(turrets[0] != turrets[1]);
    for (Entity* copy : turrets) {
        CHECK(copy->GetTransform().GetPosition() == Spot::Vector3{5.0f, 0.0f, 0.0f});
        Entity* barrel = copy->FindChild("Barrel");
        REQUIRE(barrel);
        CHECK(barrel->GetComponent<Speed>()->value == 9.0f);
    }
}

TEST_CASE("Scenes save to and load from files", "[scene][serialization]") {
    RegisterTestComponents();
    const auto path = std::filesystem::temp_directory_path() / "spot_scene_test.json";

    Scene scene;
    scene.CreateEntity("Saved").AddComponent<Speed>().value = 6.0f;
    REQUIRE(scene.SaveToFile(path));

    Scene loaded;
    REQUIRE(loaded.LoadFromFile(path));
    std::filesystem::remove(path);
    REQUIRE(loaded.FindEntity("Saved"));
    CHECK(loaded.FindEntity("Saved")->GetComponent<Speed>()->value == 6.0f);

    auto capture = std::make_shared<Spot::Tests::CaptureSink>();
    const Spot::Tests::ScopedSinks sinks({capture});
    CHECK_FALSE(loaded.LoadFromFile(path));
}
