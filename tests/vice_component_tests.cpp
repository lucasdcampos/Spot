#include "assets/asset_manifest.h"
#include "assets/asset_pak.h"
#include "log_capture.h"
#include "script_asset_fixture.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {

struct Console {
    std::shared_ptr<Spot::Tests::CaptureSink> sink = std::make_shared<Spot::Tests::CaptureSink>();
    Spot::Tests::ScopedSinks sinks{{sink}};

    std::vector<std::string> Messages() const {
        std::vector<std::string> messages;
        for (const auto& record : sink->GetRecords()) {
            if (record.loggerName == "ViceScript") {
                CHECK(record.level == Spot::LogLevel::Info);
                messages.push_back(record.message);
            }
        }
        return messages;
    }
};

constexpr auto CounterSource = R"(
    class Counter : Component {
        var frames = 0
        override func OnStart() { print("start", null) }
        override func OnUpdate(dt) { self.frames += 1; print(self.frames, dt) }
    }
)";

} // namespace

TEST_CASE("ViceScript components print through engine logs and follow the component lifecycle", "[vice][scene]") {
    Console console;
    Spot::Scene scene;
    auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    component.language = "vice";
    component.source = CounterSource;
    component.SetEnabled(false);
    scene.Update(0.25f);
    CHECK(console.Messages().empty());
    component.SetEnabled(true);
    scene.Update(0.25f);
    scene.Update(0.5f);
    CHECK(console.Messages() == std::vector<std::string>{"start null", "1 0.25", "2 0.5"});
    component.SetEnabled(false);
    scene.Update(0.25f);
    component.SetEnabled(true);
    scene.Update(0.25f);
    CHECK(console.Messages() == std::vector<std::string>{"start null", "1 0.25", "2 0.5", "3 0.25"});
    component.GetEntity().SetActive(false);
    scene.Update(0.25f);
    CHECK(console.Messages().size() == 4);
    component.GetEntity().Destroy();
    scene.Update(0.25f);
    CHECK(console.Messages().size() == 4);
}

TEST_CASE("ViceScript components keep independent variables", "[vice][scene]") {
    Console console;
    Spot::Scene scene;
    {
        auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
        component.language = "vice";
        component.source = CounterSource;
    }
    {
        auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
        component.language = "vice";
        component.source = CounterSource;
    }
    scene.Update(0.25f);
    scene.Update(0.25f);
    const auto messages = console.Messages();
    REQUIRE(messages.size() == 6);
    CHECK(messages[2] == "1 0.25");
    CHECK(messages[3] == "1 0.25");
    CHECK(messages[4] == "2 0.25");
    CHECK(messages[5] == "2 0.25");
}

TEST_CASE("ViceScript source round trips through scenes and resets runtime state", "[vice][scene]") {
    Console console;
    Spot::Scene scene;
    auto& component = scene.CreateEntity("Scripted").AddComponent<Spot::ScriptComponent>();
    component.language = "vice";
    component.source = CounterSource;
    component.sourceName = "counter.vice";
    scene.Update(0.25f);
    const std::string saved = scene.SaveToString();
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(saved));
    const auto* restored = loaded.GetRootEntities()[0]->GetComponent<Spot::ScriptComponent>();
    REQUIRE(restored);
    CHECK(restored->source == component.source);
    CHECK(restored->sourceName == "counter.vice");
    loaded.Update(0.25f);
    CHECK(console.Messages() == std::vector<std::string>{"start null", "1 0.25", "start null", "1 0.25"});
}

TEST_CASE("ViceScript failures stop one component and are logged once with script location", "[vice][scene]") {
    Console console;
    Spot::Scene scene;
    auto& bad = scene.CreateEntity("Broken").AddComponent<Spot::ScriptComponent>();
    bad.language = "vice";
    bad.sourceName = "broken.vice";
    bad.source =
        "\n\nclass TestComponent : Component {\n    override func OnUpdate(dt) {\n        print(missing)\n    }\n}\n";
    auto& good = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    good.language = "vice";
    good.source = "\n\nclass TestComponent : Component {\n    override func OnUpdate(dt) { print(\"alive\") }\n}\n";
    scene.Update(0.25f);
    scene.Update(0.25f);
    CHECK(bad.IsFaulted());
    CHECK_FALSE(good.IsFaulted());
    CHECK(console.Messages() == std::vector<std::string>{"alive", "alive"});
    std::size_t errors = 0;
    for (const auto& record : console.sink->GetRecords()) {
        if (record.level == Spot::LogLevel::Error) {
            ++errors;
            CHECK(record.message.find("broken.vice:5:15:") != std::string::npos);
            CHECK(record.message.find("Broken") != std::string::npos);
        }
    }
    CHECK(errors == 1);
}

TEST_CASE("ViceScript lifecycle signatures are checked before any script executes", "[vice][scene]") {
    for (const auto* source :
         {"class Invalid : Component { override func OnStart(arg) { print(arg) } }",
          "class Invalid : Component { override func OnUpdate() { print(1) } }",
          "class Invalid : Component { override func OnStart() { print(1) }; override func OnUpdate(a, b) {} }",
          "class Invalid : Component { override func OnStart() {"}) {
        INFO(source);
        Console console;
        Spot::Scene scene;
        auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
        component.language = "vice";
        component.source = source;
        scene.Update(0.25f);
        CHECK(component.IsFaulted());
        CHECK(console.Messages().empty());
    }
}

TEST_CASE("ViceScript lifecycle functions are optional", "[vice][scene]") {
    Console console;
    Spot::Scene scene;
    auto& empty = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    empty.language = "vice";
    empty.source = "class Empty : Component {}";
    auto& startOnly = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    startOnly.language = "vice";
    startOnly.source = "\n\nclass TestComponent : Component {\n    override func OnStart() { print(\"started\") }\n}\n";
    scene.Update(0.25f);
    scene.Update(0.25f);
    CHECK_FALSE(empty.IsFaulted());
    CHECK_FALSE(startOnly.IsFaulted());
    CHECK(console.Messages() == std::vector<std::string>{"started"});
}

TEST_CASE("ViceScript file assets are shared while component state stays independent", "[vice][assets]") {
    Console console;
    ScriptAssetFixture fixture;
    const auto id = fixture.Import("scripts/counter.vice", CounterSource);
    Spot::AssetManager assets(fixture.imported);
    Spot::Scene scene;
    for (int i = 0; i < 2; ++i) {
        auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
        component.language = "vice";
        component.script = assets.Load<Spot::ScriptSource>(id);
        REQUIRE(component.script);
        component.source =
            "\n\nclass TestComponent : Component {\n    override func OnStart() { print(\"embedded fallback\") }\n}\n";
    }
    scene.Update(0.25f);
    scene.Update(0.25f);
    CHECK(console.Messages() ==
          std::vector<std::string>{"start null", "start null", "1 0.25", "1 0.25", "2 0.25", "2 0.25"});
    const auto saved = scene.SaveToString();
    CHECK(saved.find(id.ToString()) != std::string::npos);
    CHECK(saved.find("var frames") == std::string::npos);
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(saved, &assets));
    const auto* component = loaded.GetRootEntities()[0]->GetComponent<Spot::ScriptComponent>();
    REQUIRE(component);
    REQUIRE(component->script);
    CHECK(component->script.GetId() == id);
    CHECK(component->script.Get() == assets.Load<Spot::ScriptSource>(id).Get());
}

TEST_CASE("ViceScript scenes execute file assets from a shipped pak", "[vice][assets]") {
    Console console;
    ScriptAssetFixture fixture;
    const auto id = fixture.Import(
        "scripts/shipped.vice",
        "\n\nclass TestComponent : Component {\n    override func OnStart() { print(\"shipped\", null) }\n}\n");
    {
        Spot::AssetManager assets(fixture.imported);
        Spot::Scene scene;
        {
            auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
            component.language = "vice";
            component.script = assets.Load<Spot::ScriptSource>(id);
        }
        fixture.Write("level.scene", scene.SaveToString());
    }
    REQUIRE(fixture.database.Refresh() == 1);
    const auto shipped = fixture.temp.GetPath() / "shipped";
    std::filesystem::create_directories(shipped);
    std::filesystem::copy_file(Spot::Detail::Manifest::GetPath(fixture.imported),
                               Spot::Detail::Manifest::GetPath(shipped));
    REQUIRE(Spot::Detail::WriteAssetPak(shipped / Spot::Detail::AssetPakFileName, fixture.imported));
    std::filesystem::rename(fixture.sources, fixture.temp.GetPath() / "unavailable_sources");
    Spot::AssetManager assets(shipped);
    auto scene = Spot::InstantiateScene(assets, "level.scene");
    REQUIRE(scene);
    scene->Update(0.25f);
    CHECK(console.Messages() == std::vector<std::string>{"shipped null"});
}

TEST_CASE("ViceScript asset errors use the relative file path in diagnostics", "[vice][assets]") {
    Console console;
    ScriptAssetFixture fixture;
    const auto id = fixture.Import(
        "scripts/broken.vice",
        "\n\nclass TestComponent : Component {\n    override func OnUpdate(dt) {\n        print(missing)\n    }\n}\n");
    Spot::AssetManager assets(fixture.imported);
    Spot::Scene scene;
    auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    component.language = "vice";
    component.script = assets.Load<Spot::ScriptSource>(id);
    component.sourceName = "unused embedded name";
    scene.Update(0.25f);
    REQUIRE(component.IsFaulted());
    REQUIRE_FALSE(console.sink->GetRecords().empty());
    CHECK(console.sink->GetRecords().back().message.find("scripts/broken.vice:5:15:") != std::string::npos);
}

TEST_CASE("ViceScript missing asset references fault instead of executing embedded fallback", "[vice][assets]") {
    Console console;
    Spot::Scene scene;
    auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    component.language = "vice";
    const auto missing = Spot::AssetId::Generate();
    component.script = Spot::AssetRef<Spot::ScriptSource>(missing, nullptr);
    component.source =
        "\n\nclass TestComponent : Component {\n    override func OnStart() { print(\"fallback\") }\n}\n";
    const auto saved = scene.SaveToString();
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(saved));
    auto* restored = loaded.GetRootEntities()[0]->GetComponent<Spot::ScriptComponent>();
    REQUIRE(restored);
    CHECK(restored->script.GetId() == missing);
    loaded.Update(0.25f);
    loaded.Update(0.25f);
    CHECK(restored->IsFaulted());
    CHECK(console.Messages().empty());
}

TEST_CASE("ViceScript reimported file assets are used by the next scene instance", "[vice][assets]") {
    Console console;
    ScriptAssetFixture fixture;
    const auto id =
        fixture.Import("scripts/version.vice",
                       "\n\nclass TestComponent : Component {\n    override func OnUpdate(dt) { print(\"old\") }\n}\n");
    Spot::AssetManager assets(fixture.imported);
    Spot::Scene scene;
    {
        auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
        component.language = "vice";
        component.script = assets.Load<Spot::ScriptSource>(id);
    }
    scene.Update(0.25f);
    const auto saved = scene.SaveToString();
    fixture.Write("scripts/version.vice",
                  "\n\nclass TestComponent : Component {\n    override func OnUpdate(dt) { print(\"new\") }\n}\n");
    REQUIRE(fixture.database.Refresh() == 1);
    assets.RefreshManifest();
    assets.Reload(fixture.database.GetChanged());
    Spot::Scene next;
    REQUIRE(next.LoadFromString(saved, &assets));
    next.Update(0.25f);
    scene.Update(0.25f);
    CHECK(console.Messages() == std::vector<std::string>{"old", "new", "old"});
}

TEST_CASE("ViceScript source modules load imported assets and share the explicit spot context",
          "[vice][assets][libraries]") {
    Console console;
    ScriptAssetFixture fixture;
    fixture.Import("scripts/combat.vice", R"(
        import spot
        var calls = 0
        func advance(amount) { calls += 1; spot.entity.Transform.position.x += amount }
    )");
    const auto root = fixture.Import("scripts/player.vice", R"(
        import combat
        import "./combat.vice" as Battle

        class TestComponent : Component {
            override func OnStart() { combat.advance(3) }
            override func OnUpdate(delta_time) {
                if combat != Battle { missing() }
                Battle.advance(delta_time)
                print(combat.calls)
            }
        }
    )");
    auto directory = fixture.imported;
    SECTION("Imported files") {
    }
    SECTION("Pak without sources or loose imported files") {
        directory = fixture.temp.GetPath() / "shipped";
        std::filesystem::create_directories(directory);
        std::filesystem::copy_file(Spot::Detail::Manifest::GetPath(fixture.imported),
                                   Spot::Detail::Manifest::GetPath(directory));
        REQUIRE(Spot::Detail::WriteAssetPak(directory / Spot::Detail::AssetPakFileName, fixture.imported));
        std::filesystem::rename(fixture.sources, fixture.temp.GetPath() / "unavailable_sources");
    }
    Spot::AssetManager assets(directory);
    struct AssetScope {
        Spot::AssetManager* previous = Spot::Assets::GetManager();
        ~AssetScope() { Spot::Detail::SetAssetManager(previous); }
    } scope;
    Spot::Detail::SetAssetManager(&assets);
    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Player");
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.script = assets.Load<Spot::ScriptSource>(root);
    scene.Update(0.25f);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(entity.GetTransform().GetPosition().x == 3.25f);
    scene.Update(0.25f);
    CHECK(entity.GetTransform().GetPosition().x == 3.5f);
    CHECK(console.Messages() == std::vector<std::string>{"2", "3"});
}

TEST_CASE("ViceScript engine modules require explicit imports and accept aliases", "[vice][scene][libraries]") {
    Spot::Scene scene;
    auto& entity = scene.CreateEntity();
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.language = "vice";
    SECTION("Alias") {
        script.source = "import spot as engine\n\nclass TestComponent : Component {\n    override func OnStart() { "
                        "engine.entity.Transform.position.x = 7 }\n}\n";
        scene.Update(0.1f);
        CHECK_FALSE(script.IsFaulted());
        CHECK(entity.GetTransform().GetPosition().x == 7);
    }
    SECTION("No implicit module global") {
        script.source = "\n\nclass TestComponent : Component {\n    override func OnStart() { print(spot) }\n}\n";
        scene.Update(0.1f);
        CHECK(script.IsFaulted());
    }
    SECTION("Free functions do not inherit implicit component members") {
        script.source = "import spot\nfunc Helper() { print(entity) }\n"
                        "class TestComponent : Component { override func OnStart() { Helper() } }";
        scene.Update(0.1f);
        CHECK(script.IsFaulted());
    }
}

TEST_CASE("ViceScript receives registered native engine libraries without adapter specific bindings",
          "[vice][libraries]") {
    class Combat final : public Spot::ScriptObject {
    public:
        Spot::ScriptValue Call(std::string_view name, std::span<const Spot::ScriptValue> arguments) override {
            if (name == "damage" && arguments.size() == 2)
                return std::get<double>(arguments[0]) - std::get<double>(arguments[1]);
            return ScriptObject::Call(name, arguments);
        }
    };
    Spot::RegisterScriptLibrary("combat", [] { return std::make_shared<Combat>(); });
    struct Cleanup {
        ~Cleanup() { Spot::UnregisterScriptLibrary("combat"); }
    } cleanup;
    Console console;
    Spot::Scene scene;
    auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    component.language = "vice";
    component.source = "import combat\n\nclass TestComponent : Component {\n    override func OnStart() { "
                       "print(combat.damage(10, 3)) }\n}\n";
    scene.Update(0.1f);
    CHECK_FALSE(component.IsFaulted());
    CHECK(console.Messages() == std::vector<std::string>{"7"});
}

TEST_CASE("ViceScript components run class instances and inherited engine object methods", "[vice][scene][classes]") {
    Console console;
    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Actor");
    auto& component = entity.AddComponent<Spot::ScriptComponent>();
    component.language = "vice";
    component.source = R"(
        import spot
        class Mover {
            var actor
            var speed
            func init(actor, speed) { self.actor = actor; self.speed = speed }
            func step(dt) { self.actor.Transform.position.x += self.speed * dt }
        }
        class FastMover extends Mover {
            override func step(dt) { super.step(dt * 2) }
        }
        var mover

        class TestComponent : Component {
            override func OnStart() { mover = FastMover(spot.entity, 3) }
            override func OnUpdate(dt) { mover.step(dt); print(spot.entity.Transform.position.x) }
        }
    )";
    scene.Update(0.5f);
    scene.Update(0.25f);
    CHECK(console.Messages() == std::vector<std::string>{"3", "4.5"});
}

TEST_CASE("ViceScript component classes dispatch the native lifecycle and preserve inherited state",
          "[vice][scene][classes]") {
    Console console;
    Spot::Scene scene;
    scene.SetFixedTimeStep(0.25f);
    auto& entity = scene.CreateEntity("Player");
    auto& component = entity.AddComponent<Spot::ScriptComponent>();
    component.language = "vice";
    component.source = R"(
        class ControllerBase : Component {
            var frames = 0
            override func OnStart() { super.OnStart(); print("start", self.GetEntity().GetName()) }
            override func OnEnable() { print("enable", self.frames) }
            override func OnDisable() { print("disable", self.frames, self.IsActiveAndEnabled()) }
            override func OnFixedUpdate(dt) { print("fixed", dt) }
            override func OnLateUpdate(dt) { print("late", dt) }
            override func OnDestroy() { print("destroy", self.GetEntity().GetName(), self.IsValid()) }
        }
        class PlayerController : ControllerBase {
            override func OnUpdate(dt) {
                self.frames += 1
                print("update", self.frames, dt)
                if self.frames == 2 {
                    self.SetEnabled(false)
                    print("disabled", self.IsEnabled())
                }
            }
        }
    )";
    scene.Update(0.25f);
    scene.Update(0.25f);
    REQUIRE_FALSE(component.IsFaulted());
    CHECK_FALSE(component.IsEnabled());
    CHECK(console.Messages() == std::vector<std::string>{"start Player", "enable 0", "fixed 0.25", "update 1 0.25",
                                                         "late 0.25", "fixed 0.25", "update 2 0.25", "disable 2 false",
                                                         "disabled false"});
    component.SetEnabled(true);
    scene.Update(0);
    entity.Destroy();
    const auto messages = console.Messages();
    REQUIRE(messages.size() == 14);
    CHECK(std::vector<std::string>(messages.end() - 5, messages.end()) ==
          std::vector<std::string>{"enable 2", "update 3 0", "late 0", "disable 3 false", "destroy Player false"});
}

TEST_CASE("ViceScript component selection rejects missing ambiguous and argument requiring classes",
          "[vice][scene][classes]") {
    for (const auto* source : {"override func OnStart() { print(1) }", "class Unrelated {}",
                               "class First : Component {}\nclass Second : Component {}",
                               "class NeedsArgs : Component { func init(arg) {} }"}) {
        INFO(source);
        Console console;
        Spot::Scene scene;
        auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
        component.language = "vice";
        component.source = source;
        scene.Update(0);
        CHECK(component.IsFaulted());
        CHECK(console.Messages().empty());
    }
}

TEST_CASE("ViceScript validates every component hook before global and field initializers", "[vice][scene][classes]") {
    for (const auto* hook :
         {"OnStart", "OnEnable", "OnDisable", "OnFixedUpdate", "OnUpdate", "OnLateUpdate", "OnDestroy"}) {
        Console console;
        Spot::Scene scene;
        auto& component = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
        component.language = "vice";
        const std::string parameters =
            std::string_view(hook).find("Update") != std::string_view::npos ? "" : "unexpected";
        component.source = "var effect = print(\"global\")\nclass Invalid : Component {\n"
                           "var effect = print(\"field\")\noverride func " +
                           std::string(hook) + "(" + parameters + ") {}\n}";
        scene.Update(0);
        CHECK(component.IsFaulted());
        CHECK(console.Messages().empty());
    }
}

TEST_CASE("ViceScript component classes inherit methods and native identity through source modules",
          "[vice][assets][classes]") {
    ScriptAssetFixture fixture;
    fixture.Import("controllers/base.vice", R"(
        class BaseController : Component {
            var updates = 0
            override func OnStart() { self.GetEntity().SetName("Controlled") }
            override func OnUpdate(dt) { self.updates += 1; self.GetEntity().GetTransform().Translate(Vector3(dt, 0, 0)) }
        }
    )");
    const auto root = fixture.Import("controllers/player.vice", R"(
        import base
        import spot
        class PlayerController : base.BaseController {
            override func OnStart() {
                super.OnStart()
                if self.GetEntity() != spot.entity or self.GetScene() != spot.Scene { missing() }
                if self.GetId() != spot.Component.GetId() { missing() }
            }
            override func OnUpdate(dt) { super.OnUpdate(dt * 2) }
        }
    )");
    Spot::AssetManager assets(fixture.imported);
    struct AssetScope {
        Spot::AssetManager* previous = Spot::Assets::GetManager();
        ~AssetScope() { Spot::Detail::SetAssetManager(previous); }
    } scope;
    Spot::Detail::SetAssetManager(&assets);
    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Player");
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.script = assets.Load<Spot::ScriptSource>(root);
    scene.Update(0.25f);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(entity.GetName() == "Controlled");
    CHECK(entity.GetTransform().GetPosition().x == 0.5f);
}

TEST_CASE("ViceScript removal cleanup resolves the owning component only during its hook", "[vice][scene][classes]") {
    Console console;
    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Owner");
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.language = "vice";
    script.source = R"(
        class Cleanup : Component {
            override func OnDestroy() { print(self.GetEntity().GetName(), self.IsValid(), self.GetScene() != null) }
        }
    )";
    scene.Update(0);
    REQUIRE_FALSE(script.IsFaulted());
    entity.RemoveComponent(script);
    scene.Update(0);
    CHECK(console.Messages() == std::vector<std::string>{"Owner false true"});
}

TEST_CASE("ViceScript bounds recursive native activity hooks", "[vice][scene]") {
    Console console;
    Spot::Scene scene;
    auto& script = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    script.language = "vice";
    script.source = R"(
        class Toggle : Component {
            override func OnEnable() { self.SetEnabled(false) }
            override func OnDisable() { self.SetEnabled(true) }
        }
    )";
    scene.Update(0);
    CHECK(script.IsFaulted());
    auto& healthy = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    healthy.language = "vice";
    healthy.source = "class Healthy : Component { override func OnStart() { print(42) } }";
    scene.Update(0);
    CHECK_FALSE(healthy.IsFaulted());
    CHECK(console.Messages() == std::vector<std::string>{"42"});
}

TEST_CASE("ViceScript runtime rejects component hook calls before initialization", "[vice][scene]") {
    auto runtime = Spot::CreateScriptRuntime("vice", "class Empty : Component {}", "empty.vice");
    CHECK_THROWS_AS(runtime->Call("OnStart"), std::logic_error);
}
