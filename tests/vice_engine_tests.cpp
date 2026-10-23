#include "log_capture.h"

#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>

namespace {

constexpr float Step = Spot::Scene::DefaultFixedTimeStep;

struct Log {
    std::shared_ptr<Spot::Tests::CaptureSink> sink = std::make_shared<Spot::Tests::CaptureSink>();
    Spot::Tests::ScopedSinks sinks{{sink}};
    std::vector<std::string> Messages(std::string_view logger = "ViceScript") const {
        std::vector<std::string> messages;
        for (const auto& record : sink->GetRecords()) {
            if (record.loggerName == logger)
                messages.push_back(record.message);
        }
        return messages;
    }
    bool Contains(std::string_view text) const {
        for (const auto& record : sink->GetRecords()) {
            if (record.message.find(text) != std::string::npos)
                return true;
        }
        return false;
    }
};

Spot::ScriptComponent& AddScript(Spot::Entity& entity, std::string source) {
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    script.language = "vice";
    script.sourceName = "test.vice";
    script.source = std::move(source);
    return script;
}

void Simulate(Spot::Scene& scene, float seconds) {
    for (long i = 0, updates = std::lround(seconds / Step); i < updates; ++i)
        scene.Update(Step);
}

} // namespace

TEST_CASE("ViceScript components receive collision and trigger hooks with contact details", "[vice][engine]") {
    Log log;
    Spot::Scene scene;
    auto& ground = scene.CreateEntity("Ground");
    ground.GetTransform().SetPosition({0, -0.5f, 0});
    ground.AddComponent<Spot::BoxCollider>(Spot::Vector3{20, 1, 20});
    auto& zone = scene.CreateEntity("Zone");
    zone.GetTransform().SetPosition({0, 3, 0});
    zone.AddComponent<Spot::BoxCollider>(Spot::Vector3{4, 1, 4}).SetTrigger(true);
    auto& ball = scene.CreateEntity("Ball");
    ball.GetTransform().SetPosition({0, 5, 0});
    ball.AddComponent<Spot::SphereCollider>();
    ball.AddComponent<Spot::Rigidbody>();
    auto& script = AddScript(ball, R"(
        class Ball : Component {
            var stays = 0
            override func OnCollisionEnter(collision) {
                print("enter", collision.other.name, collision.normal.y > 0.9, collision.impulse > 0,
                      collision.pointCount >= 1, collision.point == collision.GetPoint(0))
            }
            override func OnCollisionStay(collision) { stays += 1 }
            override func OnTriggerEnter(contact) { print("trigger", contact.other.name, contact.isTrigger) }
            override func OnTriggerExit(contact) { print("left", contact.other.name) }
        }
    )");
    Simulate(scene, 3.0f);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(log.Messages() ==
          std::vector<std::string>{"trigger Zone false", "left Zone", "enter Ground true true true true"});
}

TEST_CASE("ViceScript creates, parents, changes and destroys entities and components", "[vice][engine]") {
    Log log;
    Spot::Scene scene;
    auto& owner = scene.CreateEntity("Spawner");
    auto& script = AddScript(owner, R"(
        import spot
        class Spawner : Component {
            var spawned
            override func OnStart() {
                spawned = scene.CreateEntity("Bullet")
                spawned.transform.position = Vector3(1, 2, 3)
                const sprite = spawned.AddComponent("SpriteRenderer")
                sprite.color = Color(1, 0, 0, 1)
                const child = spawned.CreateChild("Trail")
                print(child.parent == spawned, spawned.HasComponent("SpriteRenderer"))
                child.SetParent(null)
                print(child.parent == null)
                child.SetParent(entity, true)
                print(child.parent == entity, spawned.RemoveComponent("SpriteRenderer"),
                      spawned.RemoveComponent(sprite), spawned.HasComponent("SpriteRenderer"))
            }
            override func OnUpdate(deltaTime) {
                if spawned.IsValid() {
                    spawned.Destroy()
                } else {
                    print("destroyed", scene.FindEntity("Bullet") == null)
                    enabled = false
                }
            }
            func Cycle() { entity.SetParent(entity.FindChild("Trail")) }
            func Unknown() { entity.AddComponent("Nope") }
        }
    )");
    scene.Update(Step);
    scene.Update(Step);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(log.Messages() == std::vector<std::string>{"true true", "true", "true true false false", "destroyed true"});
    CHECK(scene.FindEntity("Trail")->GetParent() == &owner);
}

TEST_CASE("ViceScript reports invalid structural changes", "[vice][engine]") {
    for (const std::string body : {"entity.SetParent(entity.CreateChild(\"Child\"))", "entity.AddComponent(\"Nope\")",
                                   "scene.Instantiate(\"prefabs/missing.prefab\")", "entity.SetParent(3)"}) {
        Log log;
        Spot::Scene scene;
        auto& script = AddScript(scene.CreateEntity("Owner"),
                                 "class Bad : Component { override func OnStart() { " + body + " } }");
        scene.Update(Step);
        CHECK(script.IsFaulted());
        CHECK(log.Contains("test.vice:1:"));
    }
}

TEST_CASE("ViceScript reads time, physics queries and input through the spot library", "[vice][engine]") {
    Log log;
    Spot::Scene scene;
    auto& ground = scene.CreateEntity("Ground");
    ground.GetTransform().SetPosition({0, -0.5f, 0});
    ground.AddComponent<Spot::BoxCollider>(Spot::Vector3{20, 1, 20});
    auto& script = AddScript(scene.CreateEntity("Eye"), R"(
        import spot
        import math
        class Eye : Component {
            override func OnStart() {
                const hit = spot.Physics.Raycast(Vector3(0, 5, 0), Vector3(0, -1, 0), 100)
                print(hit.entity.name, math.abs(hit.distance - 5) < 0.01, hit.normal.y, math.abs(hit.point.y) < 0.01,
                      hit.collider.enabled)
                print(spot.Physics.Raycast(Vector3(0, 5, 0), Vector3(0, 1, 0), 100) == null)
                print(spot.Physics.Raycast(Vector3(0, 5, 0), Vector3(0, -1, 0), 100, scene.FindEntity("Ground")) == null)
                const cast = spot.Physics.SphereCast(Vector3(0, 5, 0), 0.5, Vector3(0, -1, 0), 100)
                print(math.abs(cast.distance - 4.5) < 0.01)
                print(spot.Time.fixedDeltaTime == spot.Time.GetFixedDeltaTime(), spot.Time.frameCount >= 0)
                const previous = spot.Time.timeScale
                spot.Time.timeScale = 0.5
                print(spot.Time.GetTimeScale())
                spot.Time.SetTimeScale(previous)
                print(spot.Input.IsMouseButtonDown("Left"), spot.Input.IsGamepadButtonDown("South"),
                      spot.Input.GetGamepadAxis("Left X", 0), spot.Input.IsActionDown("Jump"),
                      spot.Input.mousePosition == Vector2(0, 0))
            }
            func BadButton() { spot.Input.IsMouseButtonDown("Thumb") }
        }
    )");
    scene.Update(Step);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(log.Messages() == std::vector<std::string>{"Ground true 1 true true", "true", "true", "true", "true true",
                                                     "0.5", "false false 0 false true"});
}

TEST_CASE("ViceScript events deliver to scene and entity subscribers with arguments", "[vice][engine][events]") {
    Log log;
    Spot::Scene scene;
    auto& listener = scene.CreateEntity("Listener");
    auto& listenerScript = AddScript(listener, R"(
        import spot
        class Listener : Component {
            var total = 0
            var once
            override func OnStart() {
                scene.On("Scored", func(points) { total += points })
                entity.On("Hit", OnHit)
                once = scene.On("Once", func() { print("once", once.active); once.Cancel() })
            }
            func OnHit(damage, source) {
                print("hit", damage, source.name, entity.name)
            }
        }
    )");
    auto& emitter = scene.CreateEntity("Emitter");
    auto& emitterScript = AddScript(emitter, R"(
        import spot
        class Emitter : Component {
            override func OnUpdate(deltaTime) {
                const target = scene.FindEntity("Listener")
                print("delivered", scene.Emit("Scored", 5), target.Emit("Hit", 3, entity), entity.Emit("Hit", 1, entity))
                print("once", scene.Emit("Once"), scene.Emit("Once"))
                enabled = false
            }
        }
    )");
    scene.Update(Step);
    REQUIRE_FALSE(listenerScript.IsFaulted());
    REQUIRE_FALSE(emitterScript.IsFaulted());
    CHECK(log.Messages() ==
          std::vector<std::string>{"hit 3 Emitter Listener", "delivered 1 1 0", "once true", "once 1 0"});
    const std::array arguments{Spot::ScriptValue(10.0)};
    CHECK(Spot::EmitScriptEvent(scene, "Scored", arguments) == 1);
    const auto object = listenerScript.GetScriptObject();
    REQUIRE(object);
    CHECK(std::get<double>(object->Get("total")) == 15);
}

TEST_CASE("ViceScript event subscriptions skip inactive subscribers and end with their component",
          "[vice][engine][events]") {
    Log log;
    Spot::Scene scene;
    auto& listener = scene.CreateEntity("Listener");
    auto& script = AddScript(listener, R"(
        import spot
        class Listener : Component {
            override func OnStart() { scene.On("Ping", func() { print("pong") }) }
        }
    )");
    scene.Update(Step);
    CHECK(Spot::EmitScriptEvent(scene, "Ping") == 1);
    script.SetEnabled(false);
    CHECK(Spot::EmitScriptEvent(scene, "Ping") == 0);
    script.SetEnabled(true);
    CHECK(Spot::EmitScriptEvent(scene, "Ping") == 1);
    listener.RemoveComponent(script);
    scene.Update(Step);
    CHECK(Spot::EmitScriptEvent(scene, "Ping") == 0);
    CHECK(log.Messages() == std::vector<std::string>{"pong", "pong"});
}

TEST_CASE("ViceScript event callback errors fault the subscriber, not the emitter", "[vice][engine][events]") {
    Log log;
    Spot::Scene scene;
    auto& listener = AddScript(scene.CreateEntity("Listener"), R"(
        import spot
        class Listener : Component {
            override func OnStart() { scene.On("Boom", func(value) { missing_function() }) }
        }
    )");
    auto& emitter = AddScript(scene.CreateEntity("Emitter"), R"(
        import spot
        class Emitter : Component {
            override func OnUpdate(deltaTime) { print("sent", scene.Emit("Boom", 1)); enabled = false }
        }
    )");
    scene.Update(Step);
    CHECK(listener.IsFaulted());
    CHECK_FALSE(emitter.IsFaulted());
    CHECK(log.Contains("event callback"));
    CHECK(log.Messages() == std::vector<std::string>{"sent 1"});
    CHECK(Spot::EmitScriptEvent(scene, "Boom") == 0);
}

TEST_CASE("ViceScript calls another script's methods and fields through its component", "[vice][engine]") {
    Log log;
    Spot::Scene scene;
    auto& target = scene.CreateEntity("Enemy");
    auto& health = AddScript(target, R"(
        class Health : Component {
            var value = 100
            func Damage(amount) {
                value -= amount
                return entity.name + " " + core.to_string(value)
            }
            func Fail() { missing_function() }
        }
        import core
    )");
    health.sourceName = "scripts/health.vice";
    auto& attacker = AddScript(scene.CreateEntity("Player"), R"(
        import spot
        class Attacker : Component {
            override func OnUpdate(deltaTime) {
                const enemy = scene.FindEntity("Enemy")
                const health = enemy.GetScript("health")
                print(health.Damage(10), enemy.GetScript() == health, enemy.GetScript("other") == null)
                const component = enemy.GetComponent("ScriptComponent")
                component.value += 5
                print(component.value, health.value, component.Damage(1))
                enabled = false
            }
        }
    )");
    scene.Update(Step);
    REQUIRE_FALSE(attacker.IsFaulted());
    REQUIRE_FALSE(health.IsFaulted());
    CHECK(log.Messages() == std::vector<std::string>{"Enemy 90 true true", "95 95 Enemy 94"});
    const auto object = health.GetScriptObject();
    CHECK_THROWS(object->Call("Fail", {}));
    CHECK_FALSE(health.IsFaulted());
}

TEST_CASE("ViceScript exported fields take saved values before OnStart", "[vice][engine][export]") {
    Log log;
    const std::string source = R"(
        class Mover : Component {
            export var speed = 3
            export var label = "crate"
            export var offset = Vector3(0, 1, 0)
            export var tint = Color(1, 1, 1, 1)
            export var active = true
            override func OnStart() { print(speed, label, offset.y, tint.g, active) }
        }
    )";
    std::string saved;
    {
        Spot::Scene scene;
        auto& script = AddScript(scene.CreateEntity("Crate"), source);
        const auto& exports = script.GetExports();
        REQUIRE(exports.size() == 5);
        CHECK(exports[0].name == "speed");
        CHECK(std::get<double>(exports[0].value) == 3);
        script.fieldValues["speed"] = 7.0;
        script.fieldValues["offset"] = Spot::Vector3{0, 5, 0};
        script.fieldValues["removed"] = 1.0;
        saved = scene.SaveToString();
    }
    CHECK(saved.find("\"speed\"") != std::string::npos);
    CHECK(saved.find("\"label\"") != std::string::npos);
    CHECK(saved.find("\"removed\"") == std::string::npos);
    Spot::Scene scene;
    scene.Instantiate(saved);
    auto* script = scene.FindEntity("Crate")->GetComponent<Spot::ScriptComponent>();
    REQUIRE(script);
    CHECK(script->fieldValues.size() == 2);
    script->fieldValues["active"] = false;
    scene.Update(Step);
    REQUIRE_FALSE(script->IsFaulted());
    CHECK(log.Messages() == std::vector<std::string>{"7 crate 5 1 false"});
}

TEST_CASE("ViceScript component coroutines wait for frames, seconds and conditions", "[vice][engine][coroutines]") {
    Log log;
    Spot::Scene scene;
    auto& script = AddScript(scene.CreateEntity("Blinker"), R"(
        class Blinker : Component {
            export var ready = false
            var stopped
            override func OnStart() {
                StartCoroutine(func() {
                    print("start")
                    yield WaitFrames(2)
                    print("frames")
                    yield WaitSeconds(0.5)
                    print("seconds")
                    yield WaitUntil(func() { return ready })
                    print("ready")
                    yield
                    print("done")
                })
                stopped = StartCoroutine(Forever)
                print("stop", StopCoroutine(stopped), IsCoroutineRunning(stopped))
            }
            func Forever() {
                while true {
                    yield
                    print("never")
                }
            }
        }
    )");
    const auto messages = [&] {
        std::vector<std::string> result;
        for (const auto& message : log.Messages()) {
            if (message != "stop true false")
                result.push_back(message);
        }
        return result;
    };
    scene.Update(Step);
    REQUIRE_FALSE(script.IsFaulted());
    // A coroutine runs at once until its first yield.
    CHECK(log.Messages() == std::vector<std::string>{"start", "stop true false"});
    for (int i = 0; i < 4; ++i)
        scene.Update(Step);
    CHECK(messages() == std::vector<std::string>{"start", "frames"});
    Simulate(scene, 0.6f);
    CHECK(messages() == std::vector<std::string>{"start", "frames", "seconds"});
    script.SetEnabled(false);
    script.GetScriptObject()->Set("ready", true);
    Simulate(scene, 0.2f);
    CHECK(messages().size() == 3);
    script.SetEnabled(true);
    for (int i = 0; i < 4; ++i)
        scene.Update(Step);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(messages() == std::vector<std::string>{"start", "frames", "seconds", "ready", "done"});
}

TEST_CASE("ViceScript coroutine errors and invalid waits fault their component", "[vice][engine][coroutines]") {
    for (const std::string body : {"yield 5", "missing_function()", "yield WaitSeconds(-1)"}) {
        Log log;
        Spot::Scene scene;
        auto& script = AddScript(scene.CreateEntity("Owner"),
                                 "class Bad : Component {\n override func OnStart() { StartCoroutine(Run) }\n"
                                 " func Run() {\n  yield\n  " +
                                     body + "\n }\n}");
        for (int i = 0; i < 4; ++i)
            scene.Update(Step);
        CHECK(script.IsFaulted());
        CHECK(log.Contains("Coroutine"));
    }
}

TEST_CASE("ViceScript coroutines end with their component", "[vice][engine][coroutines]") {
    Log log;
    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Owner");
    auto& script = AddScript(entity, R"(
        class Ticker : Component {
            override func OnStart() { StartCoroutine(Tick) }
            func Tick() {
                while true {
                    print("tick")
                    yield
                }
            }
        }
    )");
    for (int i = 0; i < 3; ++i)
        scene.Update(Step);
    const auto ticks = log.Messages().size();
    CHECK(ticks >= 2);
    entity.RemoveComponent(script);
    for (int i = 0; i < 3; ++i)
        scene.Update(Step);
    CHECK(log.Messages().size() == ticks);
}

TEST_CASE("Script profiling collects functions of running components and times hooks", "[vice][engine][profile]") {
    Log log;
    Spot::Scene scene;
    for (const auto* name : {"First", "Second"}) {
        AddScript(scene.CreateEntity(name), R"(
            class Mover : Component {
                override func OnUpdate(deltaTime) { Step(deltaTime) }
                func Step(deltaTime) { transform.position.x += deltaTime }
            }
        )");
    }
    const bool profilerEnabled = Spot::Profiler::IsEnabled();
    Spot::Profiler::SetEnabled(true);
    Spot::ResetScriptProfile();
    scene.Update(Step);
    CHECK(Spot::GetScriptProfile().empty());
    Spot::SetScriptProfiling(true);
    Spot::Profiler::BeginFrame();
    for (int i = 0; i < 3; ++i)
        scene.Update(Step);
    Spot::Profiler::EndFrame();
    Spot::SetScriptProfiling(false);
    const auto profile = Spot::GetScriptProfile();
    const auto step = std::ranges::find(profile, "Step", &Spot::ScriptFunctionProfile::name);
    REQUIRE(step != profile.end());
    CHECK(step->calls == 6);
    CHECK(step->sourceName == "test.vice");
    CHECK(step->instructions > 0);
    const auto frames = Spot::Profiler::GetFrames();
    REQUIRE_FALSE(frames.empty());
    CHECK(
        std::ranges::any_of(frames.back()->samples, [](const auto& sample) { return sample.name == "test.OnUpdate"; }));
    scene.Update(Step);
    CHECK(Spot::GetScriptProfile().front().calls == profile.front().calls);
    Spot::ResetScriptProfile();
    CHECK(Spot::GetScriptProfile().empty());
    Spot::Profiler::SetEnabled(profilerEnabled);
}

TEST_CASE("ViceScript components swap in new code while keeping their state", "[vice][engine][reload]") {
    Log log;
    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Counter");
    auto& script = entity.AddComponent<Spot::ScriptComponent>();
    const auto source = [](std::string text) {
        auto asset = std::make_shared<Spot::ScriptSource>();
        asset->source = std::move(text);
        asset->sourceName = "scripts/counter.vice";
        return asset;
    };
    script.script = source(R"(
        import spot
        class Counter : Component {
            var count = 0
            var label = "first"
            var items = [1, 2]
            var target
            override func OnStart() { target = entity }
            override func OnUpdate(deltaTime) { count += 1 }
        }
    )");
    for (int i = 0; i < 3; ++i)
        scene.Update(Step);
    REQUIRE_FALSE(script.IsFaulted());

    script.script = source(R"(
        import spot
        class Counter : Component {
            var count = 100
            var label = 7
            var items = []
            var target
            var extra = 5
            override func OnStart() { print("not again") }
            override func OnUpdate(deltaTime) { count += 10 }
            override func OnReload() {
                print("reloaded {count} {label} {items.length} {target.name} {extra}")
            }
        }
    )");
    scene.Update(Step);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(log.Messages() == std::vector<std::string>{"reloaded 3 7 0 Counter 5"});
    CHECK(std::get<double>(script.GetScriptObject()->Get("count")) == 13);
    CHECK(log.Contains("Reloaded 'counter'"));

    script.script = source("class Counter : Component { var count = ");
    scene.Update(Step);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(log.Contains("Kept the running code of 'counter'"));
    CHECK(std::get<double>(script.GetScriptObject()->Get("count")) == 23);
}

TEST_CASE("Script breakpoints report the call stack and let the frame continue", "[vice][engine][debug]") {
    Log log;
    Spot::Scene scene;
    auto& script = AddScript(scene.CreateEntity("Walker"), "class Walker : Component {\n"
                                                           "    var steps = 0\n"
                                                           "    override func OnUpdate(deltaTime) {\n"
                                                           "        const before = steps\n"
                                                           "        steps = before + 1\n"
                                                           "    }\n"
                                                           "}\n");
    script.sourceName = "scripts/walker.vice";
    std::vector<Spot::ScriptBreak> stops;
    Spot::SetScriptBreakHandler([&](const Spot::ScriptBreak& stop) { stops.push_back(stop); });
    Spot::SetScriptBreakpoints("scripts/walker.vice", {5});
    CHECK(Spot::GetScriptBreakpoints("scripts/walker.vice") == std::vector<std::size_t>{5});
    scene.Update(Step);
    scene.Update(Step);
    REQUIRE(stops.size() == 2);
    const auto& frame = stops[1].frames.front();
    CHECK(frame.function == "OnUpdate");
    CHECK(frame.sourceName == "scripts/walker.vice");
    CHECK(frame.line == 5);
    CHECK(std::ranges::find(frame.locals, std::pair<std::string, std::string>{"before", "1"}) != frame.locals.end());
    CHECK(std::get<double>(script.GetScriptObject()->Get("steps")) == 2);
    Spot::SetScriptBreakpoints("scripts/walker.vice", {});
    scene.Update(Step);
    CHECK(stops.size() == 2);
    Spot::SetScriptBreakHandler({});
    REQUIRE_FALSE(script.IsFaulted());
}
