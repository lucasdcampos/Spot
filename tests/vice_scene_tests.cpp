#include "log_capture.h"

#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace {

class Health final : public Spot::Component {
public:
    int value = 100;
    int calls = 0;
    int GetRemaining() const { return value; }
    int Damage(int amount) {
        if (amount < 0)
            throw std::runtime_error("Damage cannot be negative");
        ++calls;
        value -= amount;
        return value;
    }
};
struct HealthRegistration {
    HealthRegistration() {
        Spot::RegisterComponent<Health>("Health");
        Spot::ScriptBindings<Health>()
            .Field("value", &Health::value)
            .Property("guarded", &Health::GetRemaining,
                      [](Health& health, int value) {
                          if (value < 0)
                              throw std::runtime_error("Health cannot be negative");
                          health.value = value;
                      })
            .ReadOnly("calls", [](const Health& health) { return health.calls; })
            .Method("GetRemaining", &Health::GetRemaining)
            .Method("Damage", &Health::Damage);
    }
    ~HealthRegistration() { Spot::UnregisterComponent("Health"); }
};
struct Log {
    std::shared_ptr<Spot::Tests::CaptureSink> sink = std::make_shared<Spot::Tests::CaptureSink>();
    Spot::Tests::ScopedSinks sinks{{sink}};
    bool Contains(std::string_view text) const {
        for (const auto& record : sink->GetRecords()) {
            if (record.message.find(text) != std::string::npos)
                return true;
        }
        return false;
    }
};

} // namespace

TEST_CASE("ViceScript accesses its owning entity scene and transforms through public properties", "[vice][scene]") {
    Spot::Scene scene;
    auto& parent = scene.CreateEntity("Parent");
    parent.GetTransform().SetPosition({10, 0, 0});
    auto& entity = parent.CreateChild("Player");
    entity.AddTag("player");
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.language = "vice";
    script.source = R"(
        import spot
        var target = spot.Scene.FindEntityWithTag("player")

        class TestComponent : Component {
            override func OnStart() {
                if target != spot.entity { missing() }
                if spot.entity.Parent.FindChild("Player") != spot.entity { missing() }
                if spot.entity.Transform.Entity != spot.entity { missing() }
                spot.entity.name = "Hero"
                spot.entity.layer = 3
                spot.entity.Transform.position = Vector3(1, 2, 3)
                spot.entity.Transform.scale = Vector3(2, 2, 2)
                spot.entity.Transform.rotation2D = 0.5
                spot.entity.AddTag("hero")
                if !spot.entity.HasTag("hero") { missing() }
                if spot.Scene.FindEntity("Missing") != null { missing() }
            }
            override func OnUpdate(delta_time) {
                target.Transform.position.x += delta_time * 4
            }
        }
    )";
    scene.Update(0.25f);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(entity.GetName() == "Hero");
    CHECK(entity.GetLayer() == 3);
    CHECK(entity.GetTransform().GetPosition() == Spot::Vector3{2, 2, 3});
    CHECK(entity.GetTransform().GetWorldPosition() == Spot::Vector3{12, 2, 3});
    CHECK(entity.GetTransform().GetScale() == Spot::Vector3{2, 2, 2});
    CHECK(entity.GetTransform().GetRotation2D() == Catch::Approx(0.5));
}

TEST_CASE("ViceScript reads and writes game components using the generic registry", "[vice][scene]") {
    HealthRegistration registration;
    Spot::Scene scene;
    auto& player = scene.CreateEntity("Player");
    auto& health = player.AddComponent<Health>();
    auto& another = player.AddComponent<Health>();
    auto& script = scene.CreateEntity("Controller").AddComponent<Spot::ScriptComponent>();
    script.language = "vice";
    script.source = R"(
        import spot
        var player = spot.Scene.FindEntity("Player")

        class TestComponent : Component {
            override func OnStart() {
                if player.Health != player.GetComponent("Health") { missing() }
                if !player.HasComponent("Health") { missing() }
                if player.Camera != null { missing() }
                player.Health.value -= 10
                player.Health.guarded = 90
                if player.Health.Damage(5) != 85 { missing() }
                if player.Health.GetRemaining() != 85 { missing() }
                if player.Health.calls != 1 { missing() }
                player.GetComponent("Health", 1).value = 25
                if player.GetComponent("Health", 2) != null { missing() }
                player.Health.enabled = false
                if player.Health.active { missing() }
                if player.Health.Entity != player { missing() }
            }
        }
    )";
    scene.Update(0.01f);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(health.value == 85);
    CHECK(health.calls == 1);
    CHECK_FALSE(health.IsEnabled());
    CHECK(another.value == 25);
}

TEST_CASE("ViceScript exposes sprite camera and audio with the same descriptors", "[vice][scene]") {
    Spot::Scene scene;
    auto& entity = scene.CreateEntity();
    auto& sprite = entity.AddComponent<Spot::SpriteRenderer>();
    auto& camera = entity.AddComponent<Spot::Camera>();
    auto& audio = entity.AddComponent<Spot::AudioSource>();
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.language = "vice";
    script.source = R"(
        import spot

        class TestComponent : Component {
            override func OnStart() {
                spot.entity.SpriteRenderer.color = Color(1, 0.5, 0.25, 1)
                spot.entity.SpriteRenderer.color.a = 0.5
                spot.entity.SpriteRenderer.size = Vector2(2, 3)
                spot.entity.SpriteRenderer.flippedX = true
                spot.entity.SpriteRenderer.frame = 2
                spot.entity.SpriteRenderer.order = 5
                spot.entity.Camera.projection = "Orthographic"
                spot.entity.Camera.orthographicSize = 8
                spot.entity.Camera.background = Color(0, 0, 0, 1)
                spot.entity.AudioSource.volume = 0.25
                spot.entity.AudioSource.pitch = 1.5
                spot.entity.AudioSource.loop = true
                spot.entity.AudioSource.Play()
                spot.entity.AudioSource.Pause()
                spot.entity.AudioSource.Stop()
            }
        }
    )";
    scene.Update(0.01f);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(sprite.GetColor() == Spot::Color{1, 0.5f, 0.25f, 0.5f});
    CHECK(sprite.GetSize() == Spot::Vector2{2, 3});
    CHECK(sprite.IsFlippedX());
    CHECK(sprite.GetFrame() == 2);
    CHECK(sprite.GetOrder() == 5);
    CHECK(camera.GetProjection() == Spot::CameraProjection::Orthographic);
    CHECK(camera.GetOrthographicSize() == 8);
    CHECK(audio.volume == 0.25f);
    CHECK(audio.pitch == 1.5f);
    CHECK(audio.loop);
}

TEST_CASE("ViceScript component handles cannot retarget replacement components", "[vice][scene]") {
    HealthRegistration registration;
    Log log;
    Spot::Scene scene;
    auto& target = scene.CreateEntity("Target");
    auto& original = target.AddComponent<Health>();
    auto& script = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    script.language = "vice";
    script.source = R"(
        import spot
        var target = spot.Scene.FindEntity("Target")
        var health = target.Health

        class TestComponent : Component {
            override func OnUpdate(delta_time) {
                print(target.IsValid(), health.IsValid())
                if health.IsValid() { health.value -= 1 }
            }
        }
    )";
    scene.Update(0.01f);
    CHECK(original.value == 99);
    target.RemoveComponent(original);
    auto& replacement = target.AddComponent<Health>();
    scene.Update(0.01f);
    CHECK_FALSE(script.IsFaulted());
    CHECK(replacement.value == 100);
    CHECK(log.Contains("true false"));
    target.Destroy();
    scene.Update(0.01f);
    CHECK_FALSE(script.IsFaulted());
    CHECK(log.Contains("false false"));
}

TEST_CASE("ViceScript invalid component access faults only the script with source location", "[vice][scene]") {
    HealthRegistration registration;
    Log log;
    Spot::Scene scene;
    auto& target = scene.CreateEntity("Target");
    auto& health = target.AddComponent<Health>();
    auto& script = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    script.language = "vice";
    script.sourceName = "scripts/handles.vice";
    script.source = "import spot\nvar health = spot.Scene.FindEntity(\"Target\").Health\n\nclass TestComponent : "
                    "Component {\n    override func OnUpdate(delta_time) {\n    health.value -= 1\n    }\n}\n";
    scene.Update(0.01f);
    REQUIRE_FALSE(script.IsFaulted());
    target.RemoveComponent(health);
    scene.Update(0.01f);
    CHECK(script.IsFaulted());
    CHECK(log.Contains("scripts/handles.vice:6:"));
    CHECK(log.Contains("Component reference is no longer valid"));
}

TEST_CASE("ViceScript component binding errors reject invalid values without mutation", "[vice][scene]") {
    HealthRegistration registration;
    for (const auto* statement : {"spot.entity.layer = 32",
                                  "spot.entity.layer = -1",
                                  "spot.entity.Health.value = 1.5",
                                  "spot.entity.Health.value = 1e30",
                                  "spot.entity.Health.value = true",
                                  "spot.entity.Health.guarded = -1",
                                  "spot.entity.Health.calls = 5",
                                  "spot.entity.Health.Damage()",
                                  "spot.entity.Health.Damage(-1)",
                                  "spot.entity.Health.secret = 1",
                                  "spot.entity.Health.Damage(1, 2)",
                                  "spot.entity.SpriteRenderer.size = Vector2(-1, 1)",
                                  "spot.entity.Camera.projection = \"Invalid\"",
                                  "spot.entity.Camera.nearPlane = 10000",
                                  "spot.entity.Camera.orthographicSize = 0",
                                  "spot.entity.AudioSource.pitch = 0",
                                  "spot.entity.Transform.position = Vector3(1e300, 0, 0)",
                                  "spot.entity.Transform = null",
                                  "spot.Input.IsKeyDown(\"Invalid\")",
                                  "spot.entity.GetComponent(\"Health\", -1)",
                                  "spot.entity = null",
                                  "print(Self)",
                                  "print(unbound)",
                                  "spot.Scene = null"}) {
        INFO(statement);
        Spot::Scene scene;
        auto& entity = scene.CreateEntity();
        auto& health = entity.AddComponent<Health>();
        entity.AddComponent<Spot::SpriteRenderer>();
        entity.AddComponent<Spot::Camera>();
        entity.AddComponent<Spot::AudioSource>();
        auto& script = entity.AddComponent<Spot::ScriptComponent>();
        script.language = "vice";
        script.source =
            "import spot\nclass InvalidCase : Component { override func OnStart() { " + std::string(statement) + " } }";
        scene.Update(0.01f);
        CHECK(script.IsFaulted());
        CHECK(health.value == 100);
        CHECK(health.calls == 0);
        CHECK(entity.GetLayer() == 0);
        CHECK(entity.GetTransform().GetPosition() == Spot::Vector3{});
    }
}

TEST_CASE("ViceScript follows its entity to another scene without retaining scene pointers", "[vice][scene]") {
    Log log;
    Spot::Scene original;
    Spot::Scene destination;
    original.CreateEntity("Target");
    destination.CreateEntity("Other");
    auto& entity = original.CreateEntity("Player");
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.language = "vice";
    script.source = R"(
        import spot
        var old_target = spot.Scene.FindEntity("Target")

        class TestComponent : Component {
            override func OnUpdate(delta_time) {
                spot.entity.Transform.position.x += 1
                print(old_target.IsValid(), spot.Scene.FindEntity("Other") != null)
            }
        }
    )";
    original.Update(0.01f);
    entity.MoveToScene(destination);
    original.Clear();
    destination.Update(0.01f);
    CHECK_FALSE(script.IsFaulted());
    CHECK(entity.GetTransform().GetPosition().x == 2);
    CHECK(log.Contains("true false"));
    CHECK(log.Contains("false true"));
}

TEST_CASE("ViceScript uses captured input state and frame transitions", "[vice][scene]") {
    Spot::InputState state;
    struct RestoreInput {
        Spot::InputState* previous = Spot::Input::GetState();
        ~RestoreInput() { Spot::Input::SetState(previous); }
    } restore;
    Spot::Input::SetState(&state);
    Spot::Scene scene;
    auto& entity = scene.CreateEntity();
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.language = "vice";
    script.source = R"(
        import spot

        class TestComponent : Component {
            override func OnUpdate(delta_time) {
                if spot.Input.IsKeyDown("Right") { spot.entity.Transform.position.x += 1 }
                if spot.Input.IsKeyPressed("Right") { spot.entity.Transform.position.y += 1 }
                if spot.Input.IsKeyReleased("Right") { spot.entity.Transform.position.z += 1 }
            }
        }
    )";
    state.OnEvent(Spot::KeyPressedEvent{Spot::Key::Right, false});
    scene.Update(0.01f);
    state.BeginFrame();
    scene.Update(0.01f);
    state.OnEvent(Spot::KeyReleasedEvent{Spot::Key::Right});
    scene.Update(0.01f);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(entity.GetTransform().GetPosition() == Spot::Vector3{2, 1, 1});
    state.BeginFrame();
    const auto capture = state.PushCapture("overlay");
    state.OnEvent(Spot::KeyPressedEvent{Spot::Key::Right, false});
    scene.Update(0.01f);
    CHECK(entity.GetTransform().GetPosition() == Spot::Vector3{2, 1, 1});
    state.PopCapture(capture);
    Spot::Input::SetState(nullptr);
    scene.Update(0.01f);
    CHECK_FALSE(script.IsFaulted());
    CHECK(entity.GetTransform().GetPosition() == Spot::Vector3{2, 1, 1});
}

TEST_CASE("ViceScript unregistering game component types removes their script callbacks", "[vice][scene]") {
    HealthRegistration registration;
    Spot::Scene scene;
    auto& entity = scene.CreateEntity();
    auto& health = entity.AddComponent<Health>();
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.language = "vice";
    script.source =
        "import spot\nvar health = spot.entity.Health\n\nclass TestComponent : Component {\n    override func "
        "OnUpdate(delta_time) { health.Damage(1) }\n}\n";
    scene.Update(0.01f);
    CHECK(health.value == 99);
    Spot::UnregisterComponent("Health");
    scene.Update(0.01f);
    CHECK(script.IsFaulted());
    CHECK(health.value == 99);
    Spot::RegisterComponent<Health>("Health");
    auto& fresh = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    fresh.language = "vice";
    fresh.GetEntity().AddComponent<Health>();
    fresh.source = "import spot\n\nclass TestComponent : Component {\n    override func OnStart() { "
                   "spot.entity.Health.Damage(1) }\n}\n";
    scene.Update(0.01f);
    CHECK(fresh.IsFaulted());
}

TEST_CASE("ViceScript descriptors validate names and supported numeric ranges", "[vice][scene]") {
    HealthRegistration registration;
    CHECK_THROWS_AS(Spot::ScriptBindings<Health>().Field("bad-name", &Health::value), std::invalid_argument);
    CHECK_THROWS_AS(Spot::ScriptBindings<Health>().Field("enabled", &Health::value), std::invalid_argument);
    CHECK_THROWS_AS(Spot::ScriptBindings<Health>().Field("Damage", &Health::value), std::invalid_argument);
    CHECK_THROWS_AS(Spot::ScriptBindings<Health>().Method("value", &Health::Damage), std::invalid_argument);
    CHECK_THROWS_AS(Spot::Detail::FromScriptValue<std::uint32_t>(-1.0), std::runtime_error);
    CHECK_THROWS_AS(Spot::Detail::ToScriptValue(std::numeric_limits<std::uint64_t>::max()), std::runtime_error);
}

TEST_CASE("ViceScript canonical engine methods share validated properties and native names", "[vice][scene][classes]") {
    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Player");
    auto& sprite = entity.AddComponent<Spot::SpriteRenderer>();
    auto& camera = entity.AddComponent<Spot::Camera>();
    auto& audio = entity.AddComponent<Spot::AudioSource>();
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.language = "vice";
    script.source = R"(
        class PlayerController : Component {
            var transform
            override func OnStart() {
                var entity = self.GetEntity()
                self.transform = entity.GetTransform()
                if self.transform.GetEntity() != entity or entity.GetScene() != self.GetScene() { missing() }
                entity.SetName("Hero")
                entity.SetLayer(4)
                entity.AddTag("hero")
                if self.GetScene().FindEntityWithTag("hero") != entity or not entity.HasTag("hero") { missing() }
                var sprite = entity.GetComponent("SpriteRenderer")
                sprite.SetColor(Color(1, 0.5, 0.25, 1))
                sprite.SetFlippedX(true)
                sprite.SetSize(Vector2(2, 3))
                if not sprite.IsFlippedX() or sprite.GetColor().g != 0.5 { missing() }
                var camera = entity.GetComponent("Camera")
                camera.SetProjection("Orthographic")
                camera.SetOrthographicSize(8)
                camera.SetFieldOfView(1)
                if camera.GetProjection() != "Orthographic" or camera.GetOrthographicSize() != 8 { missing() }
                var audio = entity.GetComponent("AudioSource")
                audio.playOnStart = false
                audio.Play()
                audio.Pause()
                audio.Stop()
                if audio.IsPlaying() or audio.IsPaused() { missing() }
                self.transform.SetPosition(Vector3(1, 2, 3))
                self.transform.SetRotation2D(0.5)
            }
            override func OnUpdate(dt) { self.transform.Translate(Vector3(dt, 0, 0)) }
        }
    )";
    scene.Update(0.25f);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(entity.GetName() == "Hero");
    CHECK(entity.GetLayer() == 4);
    CHECK(sprite.GetSize() == Spot::Vector2{2, 3});
    CHECK(sprite.IsFlippedX());
    CHECK(camera.GetOrthographicSize() == 8);
    CHECK_FALSE(audio.playOnStart);
    CHECK(entity.GetTransform().GetPosition() == Spot::Vector3{1.25f, 2, 3});
    CHECK(entity.GetTransform().GetRotation2D() == Catch::Approx(0.5));
}

TEST_CASE("ViceScript canonical native setters retain property validation", "[vice][scene][classes]") {
    for (const auto* statement : {"sprite.SetSize(Vector2(-1, 1))", "camera.SetNearPlane(10000)",
                                  "camera.SetProjection(12)", "camera.SetFieldOfView(0)"}) {
        Spot::Scene scene;
        auto& entity = scene.CreateEntity();
        auto& sprite = entity.AddComponent<Spot::SpriteRenderer>();
        auto& camera = entity.AddComponent<Spot::Camera>();
        auto& script = entity.AddComponent<Spot::ScriptComponent>();
        script.language = "vice";
        script.source = "class Invalid : Component { override func OnStart() { var sprite = "
                        "self.GetEntity().GetComponent(\"SpriteRenderer\"); "
                        "var camera = self.GetEntity().GetComponent(\"Camera\"); " +
                        std::string(statement) + " } }";
        scene.Update(0);
        CHECK(script.IsFaulted());
        CHECK(sprite.GetSize() == Spot::Vector2{1, 1});
        CHECK(camera.GetNearPlane() == Catch::Approx(0.1));
        CHECK(camera.GetFieldOfView() == Catch::Approx(Spot::ToRadians(60.0f)));
    }
}

TEST_CASE("ViceScript components expose concise properties and implicit inherited methods", "[vice][scene][classes]") {
    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Original");
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.language = "vice";
    script.source = R"(
        class Mover : Component {
            var speed = 4
            var initialName = entity.name
            func Move(dt) { entity.transform.Translate(Vector3(speed * dt, 0, 0)) }
        }
        class PlayerController : Mover {
            override func OnStart() {
                entity.name = "Player"
                transform.position = Vector3(1, 2, 3)
                if transform != entity.transform or scene != entity.scene { missing() }
                if scene.FindEntity("Player") != entity or initialName != "Original" { missing() }
                if entity.parent != null or transform.entity != entity { missing() }
                { var entity = Vector3(0, 0, 0); entity.x = 9; self.entity.layer = 2 }
            }
            override func OnUpdate(dt) {
                Move(dt)
                speed += 1
            }
        }
    )";
    scene.Update(0.25f);
    scene.Update(0.25f);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(entity.GetName() == "Player");
    CHECK(entity.GetLayer() == 2);
    CHECK(entity.GetTransform().GetPosition() == Spot::Vector3{3.25f, 2, 3});
}

TEST_CASE("ViceScript concise properties retain read only and setter validation", "[vice][scene][classes]") {
    for (const auto* statement : {"entity = null", "scene = null", "transform = null", "entity.transform = null",
                                  "entity.layer = -1", "transform.position = 4", "undeclared = 1"}) {
        Spot::Scene scene;
        auto& entity = scene.CreateEntity("Original");
        auto& script = entity.AddComponent<Spot::ScriptComponent>();
        script.language = "vice";
        script.source = "class Invalid : Component { override func OnStart() { " + std::string(statement) + " } }";
        scene.Update(0);
        CHECK(script.IsFaulted());
        CHECK(entity.GetLayer() == 0);
        CHECK(entity.GetTransform().GetPosition() == Spot::Vector3{0, 0, 0});
    }
}

TEST_CASE("ViceScript component loops use independent array and map fields", "[vice][scene][collections]") {
    Spot::Scene scene;
    const std::string source = R"(
        class Walker : Component {
            var offsets = [1, 2, 3]
            var counters = {"updates": 0}
            override func OnUpdate(dt) {
                for index, offset in offsets {
                    if index == 1 { continue }
                    transform.position.x += offset * dt
                }
                offsets[0] += 1
                counters["updates"] += 1
                entity.layer = counters["updates"]
            }
        }
    )";
    auto& first = scene.CreateEntity();
    auto& firstScript = first.AddComponent<Spot::ScriptComponent>();
    firstScript.language = "vice";
    firstScript.source = source;
    auto& second = scene.CreateEntity();
    auto& secondScript = second.AddComponent<Spot::ScriptComponent>();
    secondScript.language = "vice";
    secondScript.source = source;
    scene.Update(0.5f);
    CHECK_FALSE(firstScript.IsFaulted());
    CHECK_FALSE(secondScript.IsFaulted());
    CHECK(first.GetTransform().GetPosition().x == 2);
    CHECK(second.GetTransform().GetPosition().x == 2);
    firstScript.SetEnabled(false);
    scene.Update(0.5f);
    CHECK(first.GetTransform().GetPosition().x == 2);
    CHECK(second.GetTransform().GetPosition().x == 4.5f);
    CHECK(first.GetLayer() == 1);
    CHECK(second.GetLayer() == 2);
}
