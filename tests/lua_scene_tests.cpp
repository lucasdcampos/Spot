#include "log_capture.h"

#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("Lua accesses its owning entity scene and transforms with both dot and colon calls", "[lua][scene]") {
    Spot::Scene scene;
    auto& parent = scene.CreateEntity("Parent");
    parent.GetTransform().SetPosition({10, 0, 0});
    auto& entity = parent.CreateChild("Player");
    entity.AddTag("player");
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.language = "lua";
    script.source = R"(
        local spot = require("spot")
        local target = spot.Scene:FindEntityWithTag("player")
        function OnStart()
            if target ~= spot.entity then missing() end
            if spot.entity.Parent:FindChild("Player") ~= spot.entity then missing() end
            if spot.entity.Transform.Entity ~= spot.entity then missing() end
            spot.entity.name = "Hero"
            spot.entity.layer = 3
            spot.entity.Transform.position = Vector3(1, 2, 3)
            spot.entity.Transform.scale = Vector3(2, 2, 2)
            spot.entity.Transform.rotation2D = 0.5
            spot.entity:AddTag("hero")
            if not spot.entity.HasTag("hero") then missing() end
            if spot.Scene.FindEntity("Missing") ~= nil then missing() end
        end
        function OnUpdate(delta_time)
            target.Transform.position.x = target.Transform.position.x + delta_time * 4
        end
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

TEST_CASE("Lua reads and writes game components using the generic registry", "[lua][scene]") {
    HealthRegistration registration;
    Spot::Scene scene;
    auto& player = scene.CreateEntity("Player");
    auto& health = player.AddComponent<Health>();
    auto& another = player.AddComponent<Health>();
    auto& script = scene.CreateEntity("Controller").AddComponent<Spot::ScriptComponent>();
    script.language = "lua";
    script.source = R"(
        local spot = require("spot")
        local player = spot.Scene.FindEntity("Player")
        function OnStart()
            if player.Health ~= player:GetComponent("Health") then missing() end
            if not player:HasComponent("Health") then missing() end
            if player.Camera ~= nil then missing() end
            player.Health.value = player.Health.value - 10
            player.Health.guarded = 90
            if player.Health:Damage(5) ~= 85 then missing() end
            if player.Health.GetRemaining() ~= 85 then missing() end
            if player.Health.calls ~= 1 then missing() end
            player:GetComponent("Health", 1).value = 25
            if player.GetComponent("Health", 2) ~= nil then missing() end
            player.Health.enabled = false
            if player.Health.active then missing() end
            if player.Health.Entity ~= player then missing() end
        end
    )";
    scene.Update(0.01f);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(health.value == 85);
    CHECK(health.calls == 1);
    CHECK_FALSE(health.IsEnabled());
    CHECK(another.value == 25);
}

TEST_CASE("Lua exposes sprite camera and audio with the same descriptors", "[lua][scene]") {
    Spot::Scene scene;
    auto& entity = scene.CreateEntity();
    auto& sprite = entity.AddComponent<Spot::SpriteRenderer>();
    auto& camera = entity.AddComponent<Spot::Camera>();
    auto& audio = entity.AddComponent<Spot::AudioSource>();
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.language = "lua";
    script.source = R"(
        local spot = require("spot")
        function OnStart()
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
            spot.entity.AudioSource:Play()
            spot.entity.AudioSource:Pause()
            spot.entity.AudioSource:Stop()
        end
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

TEST_CASE("Lua component handles cannot retarget replacement components", "[lua][scene]") {
    HealthRegistration registration;
    Log log;
    Spot::Scene scene;
    auto& target = scene.CreateEntity("Target");
    auto& original = target.AddComponent<Health>();
    auto& script = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    script.language = "lua";
    script.source = R"(
        local spot = require("spot")
        local target = spot.Scene.FindEntity("Target")
        local health = target.Health
        function OnUpdate(delta_time)
            print(target:IsValid(), health:IsValid())
            if health:IsValid() then health.value = health.value - 1 end
        end
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

TEST_CASE("Lua invalid component access faults only the script with source location", "[lua][scene]") {
    HealthRegistration registration;
    Log log;
    Spot::Scene scene;
    auto& target = scene.CreateEntity("Target");
    auto& health = target.AddComponent<Health>();
    auto& script = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    script.language = "lua";
    script.sourceName = "scripts/handles.lua";
    script.source = "local spot = require(\"spot\")\nlocal health = spot.Scene.FindEntity(\"Target\").Health\nfunction "
                    "OnUpdate(delta_time)\nhealth.value = health.value - 1\nend";
    scene.Update(0.01f);
    REQUIRE_FALSE(script.IsFaulted());
    target.RemoveComponent(health);
    scene.Update(0.01f);
    CHECK(script.IsFaulted());
    CHECK(log.Contains("scripts/handles.lua:4:"));
    CHECK(log.Contains("Component reference is no longer valid"));
}

TEST_CASE("Lua component binding errors reject invalid values without mutation", "[lua][scene]") {
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
                                  "spot.entity.Transform = nil",
                                  "spot.Input.IsKeyDown(\"Invalid\")",
                                  "spot.entity.GetComponent(\"Health\", -1)",
                                  "spot.entity = nil",
                                  "print(Self)",
                                  "print(self)",
                                  "spot.Scene = nil"}) {
        INFO(statement);
        Spot::Scene scene;
        auto& entity = scene.CreateEntity();
        auto& health = entity.AddComponent<Health>();
        entity.AddComponent<Spot::SpriteRenderer>();
        entity.AddComponent<Spot::Camera>();
        entity.AddComponent<Spot::AudioSource>();
        auto& script = entity.AddComponent<Spot::ScriptComponent>();
        script.language = "lua";
        script.source = "local spot = require(\"spot\")\nfunction OnStart() " + std::string(statement) + " end";
        scene.Update(0.01f);
        CHECK(script.IsFaulted());
        CHECK(health.value == 100);
        CHECK(health.calls == 0);
        CHECK(entity.GetLayer() == 0);
        CHECK(entity.GetTransform().GetPosition() == Spot::Vector3{});
    }
}

TEST_CASE("Lua uses captured input state and frame transitions", "[lua][scene]") {
    Spot::InputState state;
    struct RestoreInput {
        Spot::InputState* previous = Spot::Input::GetState();
        ~RestoreInput() { Spot::Input::SetState(previous); }
    } restore;
    Spot::Input::SetState(&state);
    Spot::Scene scene;
    auto& entity = scene.CreateEntity();
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.language = "lua";
    script.source = R"(
        local spot = require("spot")
        function OnUpdate(delta_time)
            if spot.Input:IsKeyDown("Right") then
                spot.entity.Transform.position.x = spot.entity.Transform.position.x + 1
            end
            if spot.Input:IsKeyPressed("Right") then
                spot.entity.Transform.position.y = spot.entity.Transform.position.y + 1
            end
            if spot.Input:IsKeyReleased("Right") then
                spot.entity.Transform.position.z = spot.entity.Transform.position.z + 1
            end
        end
    )";
    state.OnEvent(Spot::KeyPressedEvent{Spot::Key::Right, false});
    scene.Update(0.01f);
    state.BeginFrame();
    scene.Update(0.01f);
    state.OnEvent(Spot::KeyReleasedEvent{Spot::Key::Right});
    scene.Update(0.01f);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(entity.GetTransform().GetPosition() == Spot::Vector3{2, 1, 1});
}

TEST_CASE("Lua shares lowercase engine object properties with ViceScript", "[lua][scene]") {
    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Original");
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.language = "lua";
    script.source = R"(
        local spot = require("spot")
        function OnStart()
            local entity = spot.Component.entity
            entity.name = "Player"
            assert(entity.transform == spot.Component.transform)
            assert(entity.scene == spot.Component.scene)
            assert(entity.parent == nil and entity.transform.entity == entity)
            entity.transform.position = Vector3(1, 2, 3)
            entity.transform:Translate(Vector3(4, 0, 0))
        end
    )";
    scene.Update(0);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(entity.GetName() == "Player");
    CHECK(entity.GetTransform().GetPosition() == Spot::Vector3{5, 2, 3});
}

TEST_CASE("Lua subscribes to events, calls other scripts and receives physics hooks", "[lua][scene][events]") {
    Log log;
    Spot::Scene scene;
    auto& ground = scene.CreateEntity("Ground");
    ground.GetTransform().SetPosition({0, -0.5f, 0});
    ground.AddComponent<Spot::BoxCollider>(Spot::Vector3{20, 1, 20});
    auto& listener = scene.CreateEntity("Listener");
    listener.GetTransform().SetPosition({0, 2, 0});
    listener.AddComponent<Spot::SphereCollider>();
    listener.AddComponent<Spot::Rigidbody>();
    auto& listenerScript = listener.AddComponent<Spot::ScriptComponent>();
    listenerScript.language = "lua";
    listenerScript.sourceName = "scripts/listener.lua";
    listenerScript.source = R"(
        local spot = require("spot")
        total = 0
        landed = nil
        function OnStart()
            spot.Scene:On("Scored", function(points, source) total = total + points; print("from " .. source.name) end)
        end
        function Add(amount) total = total + amount; return spot.entity.name end
        function OnCollisionEnter(collision) landed = collision.other.name end
    )";
    auto& emitter = scene.CreateEntity("Emitter");
    auto& emitterScript = emitter.AddComponent<Spot::ScriptComponent>();
    emitterScript.language = "lua";
    emitterScript.source = R"(
        local spot = require("spot")
        function OnUpdate(deltaTime)
            if spot.Scene:Emit("Scored", 5, spot.entity) ~= 1 then error("not delivered") end
            local other = spot.Scene:FindEntity("Listener"):GetScript("listener")
            print("called " .. other.Add(2) .. " " .. other.total)
            spot.Component.enabled = false
        end
    )";
    for (int i = 0; i < 120; ++i)
        scene.Update(Spot::Scene::DefaultFixedTimeStep);
    REQUIRE_FALSE(listenerScript.IsFaulted());
    REQUIRE_FALSE(emitterScript.IsFaulted());
    CHECK(log.Contains("from Emitter"));
    CHECK(log.Contains("called Listener 7"));
    const auto object = listenerScript.GetScriptObject();
    REQUIRE(object);
    CHECK(std::get<std::string>(object->Get("landed")) == "Ground");
    CHECK(std::get<double>(object->Get("total")) == 7);
}

TEST_CASE("Lua component coroutines yield engine waits", "[lua][scene][coroutines]") {
    Log log;
    Spot::Scene scene;
    auto& script = scene.CreateEntity("Owner").AddComponent<Spot::ScriptComponent>();
    script.language = "lua";
    script.source = R"(
        local spot = require("spot")
        steps = {}
        function OnStart()
            spot.Component:StartCoroutine(function()
                table.insert(steps, "start")
                coroutine.yield(spot.Component:WaitFrames(2))
                table.insert(steps, "frames")
                coroutine.yield(spot.Component:WaitSeconds(0.25))
                table.insert(steps, "seconds")
                coroutine.yield()
                print("lua coroutine " .. table.concat(steps, " "))
            end)
        end
    )";
    for (int i = 0; i < 40; ++i)
        scene.Update(Spot::Scene::DefaultFixedTimeStep);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(log.Contains("lua coroutine start frames seconds"));
}
