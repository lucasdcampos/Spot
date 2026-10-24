#include <spot.h>
#include <spot/assets/script_source.h>
#include <spot/scene/scripting/script_bindings.h>
#include <spot/scene/scripting/script_component.h>
#include <spot/scene/scripting/script_runtime.h>

#include <spot/scene/scripting/script_api.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <map>
#include <vector>

namespace {
struct RuntimeState {
    std::map<std::string, Spot::ScriptObjectRef> libraries;
    Spot::ScriptSourceLoader loader;
    std::vector<std::string> events;
    std::function<void()> initialize;
    std::function<void(std::string_view, std::span<const Spot::ScriptValue>)> call;
    bool initialized = false;
    bool rejectSignature = false;
    bool destroyed = false;
    std::string text;
    std::string name;
};

// Deliberately has no dependency on Luau: it exercises the complete engine side of a runtime of its own.
class TestRuntime final : public Spot::ScriptRuntime {
public:
    explicit TestRuntime(std::shared_ptr<RuntimeState> state) : m_state(std::move(state)) {}
    ~TestRuntime() override { m_state->destroyed = true; }
    void BindLibrary(std::string_view name, const Spot::ScriptObjectRef& library) override {
        m_state->libraries.emplace(name, library);
    }
    void SetSourceLoader(Spot::ScriptSourceLoader loader) override { m_state->loader = std::move(loader); }
    bool HasFunction(std::string_view name) const override {
        if (!m_state->initialized)
            throw std::runtime_error("Functions are discovered after initialization");
        return name == "OnStart" || name == "OnUpdate";
    }
    void ValidateFunction(std::string_view name, std::size_t count) const override {
        m_state->events.push_back(std::string(name) + ":" + std::to_string(count));
        if (m_state->rejectSignature)
            throw std::runtime_error("Invalid hook signature");
    }
    void Initialize() override {
        m_state->events.push_back("initialize");
        if (m_state->initialize)
            m_state->initialize();
        m_state->initialized = true;
    }
    void Call(std::string_view name, std::span<const Spot::ScriptValue> arguments) override {
        m_state->events.emplace_back(name);
        if (m_state->call)
            m_state->call(name, arguments);
    }

private:
    std::shared_ptr<RuntimeState> m_state;
};
class TestLanguage {
public:
    std::shared_ptr<RuntimeState> state = std::make_shared<RuntimeState>();
    std::string name;
    explicit TestLanguage(std::string language = "test", std::vector<std::string> extensions = {".test"})
        : name(std::move(language)) {
        Spot::RegisterScriptLanguage(name, std::move(extensions),
                                     [runtimeState = state](std::string_view text, std::string_view sourceName) {
                                         runtimeState->text = text;
                                         runtimeState->name = sourceName;
                                         return std::make_unique<TestRuntime>(runtimeState);
                                     });
    }
    ~TestLanguage() { Spot::UnregisterScriptLanguage(name); }
    Spot::ScriptComponent& Add(Spot::Entity& entity) {
        auto& component = entity.AddComponent<Spot::ScriptComponent>();
        component.language = name;
        return component;
    }
};
Spot::ScriptObjectRef Object(const Spot::ScriptValue& value) {
    return std::get<Spot::ScriptObjectRef>(value);
}
} // namespace

TEST_CASE("Script runtimes share scene bindings and lifecycle without a language dependency", "[scripting]") {
    TestLanguage language;
    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Actor");
    auto& sprite = entity.AddComponent<Spot::SpriteRenderer>();
    auto& script = language.Add(entity);
    script.source = "second runtime source";
    script.sourceName = "example.test";
    auto state = language.state;
    Spot::ScriptObjectRef owner;
    Spot::ScriptObjectRef transform;
    Spot::ScriptObjectRef component;
    bool identity = false;
    state->initialize = [&] {
        owner = Object(state->libraries.at("spot")->Get("entity"));
        transform = Object(owner->Get("Transform"));
        component = Object(owner->Get("SpriteRenderer"));
        const std::array<Spot::ScriptValue, 1> query{std::string("Actor")};
        identity = Object(Object(state->libraries.at("spot")->Get("Scene"))->Call("FindEntity", query)) == owner;
        owner->Set("name", std::string("Renamed"));
        component->Set("color", Spot::Color{1, 0, 0, 1});
    };
    state->call = [&](std::string_view name, std::span<const Spot::ScriptValue> arguments) {
        if (name == "OnUpdate") {
            auto position = std::get<Spot::Vector3>(transform->Get("position"));
            position.x += static_cast<float>(std::get<double>(arguments[0]));
            transform->Set("position", position);
        }
    };
    scene.Update(0.25f);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(identity);
    CHECK(state->text == "second runtime source");
    CHECK(state->name == "example.test");
    CHECK(state->events == std::vector<std::string>{"OnStart:0",
                                                    "OnEnable:0",
                                                    "OnDisable:0",
                                                    "OnFixedUpdate:1",
                                                    "OnUpdate:1",
                                                    "OnLateUpdate:1",
                                                    "OnDestroy:0",
                                                    "OnCollisionEnter:1",
                                                    "OnCollisionStay:1",
                                                    "OnCollisionExit:1",
                                                    "OnTriggerEnter:1",
                                                    "OnTriggerExit:1",
                                                    "OnCollisionEnter2D:1",
                                                    "OnCollisionStay2D:1",
                                                    "OnCollisionExit2D:1",
                                                    "OnTriggerEnter2D:1",
                                                    "OnTriggerExit2D:1",
                                                    "OnReload:0",
                                                    "initialize",
                                                    "OnStart",
                                                    "OnUpdate"});
    CHECK(entity.GetName() == "Renamed");
    CHECK(entity.GetTransform().GetPosition().x == 0.25f);
    CHECK(sprite.GetColor() == Spot::Color{1, 0, 0, 1});
    CHECK_THROWS_WITH(owner->Get("name"), "Scene access is only available during script execution");
    script.SetEnabled(false);
    scene.Update(0.25f);
    CHECK(entity.GetTransform().GetPosition().x == 0.25f);
    script.SetEnabled(true);
    scene.Update(0.5f);
    CHECK(entity.GetTransform().GetPosition().x == 0.75f);
    CHECK(state->events.back() == "OnUpdate");
}

TEST_CASE("Script runtime errors use component fault isolation and release scene access", "[scripting]") {
    TestLanguage language;
    Spot::Scene scene;
    TestLanguage other("other", {".other"});
    auto& bad = language.Add(scene.CreateEntity("Bad"));
    auto& good = other.Add(scene.CreateEntity("Good"));
    const auto state = language.state;
    SECTION("Signature validation runs before initializers") {
        state->rejectSignature = true;
        scene.Update(0.1f);
        CHECK(state->events == std::vector<std::string>{"OnStart:0"});
    }
    SECTION("Runtime exceptions close the scene scope") {
        state->call = [](std::string_view, std::span<const Spot::ScriptValue>) {
            throw std::runtime_error("Runtime failure");
        };
        scene.Update(0.1f);
        CHECK_THROWS_WITH(Object(state->libraries.at("spot")->Get("entity"))->Get("name"),
                          "Scene access is only available during script execution");
    }
    CHECK(bad.IsFaulted());
    CHECK_FALSE(good.IsFaulted());
}

TEST_CASE("Script component releases its runtime with the scene", "[scripting]") {
    TestLanguage language;
    std::shared_ptr<RuntimeState> state;
    {
        Spot::Scene scene;
        language.Add(scene.CreateEntity("Actor"));
        state = language.state;
        scene.Update(0.1f);
        CHECK_FALSE(state->destroyed);
    }
    CHECK(state->destroyed);
    CHECK_THROWS(Object(state->libraries.at("spot")->Get("entity"))->Get("name"));
}

namespace {
class ScriptCounter final : public Spot::Component {
public:
    int value = 0;
    int Increment(int amount) { return value += amount; }
};
struct CounterRegistration {
    CounterRegistration() {
        Spot::RegisterComponent<ScriptCounter>("ScriptCounter");
        Spot::ScriptBindings<ScriptCounter>()
            .Field("value", &ScriptCounter::value)
            .Method("increment", &ScriptCounter::Increment);
    }
    ~CounterRegistration() { Spot::UnregisterComponent("ScriptCounter"); }
};
} // namespace

TEST_CASE("Language independent bindings share game descriptors and invalidate removed references", "[scripting]") {
    CounterRegistration registration;
    TestLanguage language;
    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Counter");
    auto& counter = entity.AddComponent<ScriptCounter>();
    auto& script = language.Add(entity);
    Spot::ScriptObjectRef reference;
    bool valid = false;
    double result = 0;
    language.state->call = [&](std::string_view name, std::span<const Spot::ScriptValue>) {
        if (name == "OnStart") {
            reference = Object(Object(language.state->libraries.at("spot")->Get("entity"))->Get("ScriptCounter"));
            reference->Set("value", 10.0);
            const std::array<Spot::ScriptValue, 1> arguments{5.0};
            result = std::get<double>(reference->Call("increment", arguments));
        }
        valid = std::get<bool>(reference->Call("IsValid", {}));
    };
    scene.Update(0.1f);
    REQUIRE_FALSE(script.IsFaulted());
    CHECK(counter.value == 15);
    CHECK(result == 15);
    CHECK(valid);
    entity.RemoveComponent<ScriptCounter>();
    auto& replacement = entity.AddComponent<ScriptCounter>();
    scene.Update(0.1f);
    CHECK_FALSE(script.IsFaulted());
    CHECK_FALSE(valid);
    CHECK(replacement.value == 0);
}

TEST_CASE("Script host validation is independent of runtime value conversion", "[scripting]") {
    TestLanguage language;
    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Actor");
    auto& script = language.Add(entity);
    language.state->initialize = [&] {
        const auto owner = Object(language.state->libraries.at("spot")->Get("entity"));
        const auto transform = Object(owner->Get("Transform"));
        transform->Set("position", Spot::Vector3{std::numeric_limits<float>::quiet_NaN(), 0, 0});
    };
    scene.Update(0.1f);
    CHECK(script.IsFaulted());
    CHECK(entity.GetTransform().GetPosition() == Spot::Vector3{});
    CHECK_THROWS(
        Spot::Detail::FromScriptValue<Spot::Vector2>(Spot::Vector2{0, std::numeric_limits<float>::infinity()}));
    CHECK_THROWS(
        Spot::Detail::FromScriptValue<Spot::Color>(Spot::Color{0, 0, 0, std::numeric_limits<float>::infinity()}));
}

TEST_CASE("Script component selects registered runtimes from asset extensions", "[scripting]") {
    TestLanguage first("test", {".test"});
    TestLanguage second("second", {".second"});
    Spot::Scene scene;
    auto& a = scene.CreateEntity("First").AddComponent<Spot::ScriptComponent>();
    auto& b = scene.CreateEntity("Second").AddComponent<Spot::ScriptComponent>();
    a.script = std::make_shared<Spot::ScriptSource>("first source", "scripts/first.TEST");
    b.script = std::make_shared<Spot::ScriptSource>("second source", "scripts/second.second");
    a.language = "unavailable";
    a.source = "ignored embedded fallback";
    scene.Update(0.1f);
    CHECK_FALSE(a.IsFaulted());
    CHECK_FALSE(b.IsFaulted());
    CHECK(first.state->text == "first source");
    CHECK(second.state->text == "second source");
    CHECK(first.state->name == "scripts/first.TEST");
    CHECK(second.state->name == "scripts/second.second");
    CHECK(Spot::GetScriptLanguageForSource("folder.with.dots\\first.TEST") == "test");
}

TEST_CASE("Script component rejects unavailable languages without falling back", "[scripting]") {
    TestLanguage language;
    Spot::Scene scene;
    auto& script = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
    SECTION("Embedded source requires a language") {
        script.sourceName = "diagnostic.test";
    }
    SECTION("Unknown explicit language") {
        script.language = "unavailable";
    }
    SECTION("Unknown asset extension overrides an available embedded language") {
        script.language = "test";
        script.script = std::make_shared<Spot::ScriptSource>("source", "scripts/unknown.unavailable");
    }
    scene.Update(0.1f);
    CHECK(script.IsFaulted());
    CHECK(language.state->events.empty());
}

TEST_CASE("Script language registration replaces factories and releases extension ownership", "[scripting]") {
    TestLanguage language;
    const auto original = language.state;
    auto replacement = std::make_shared<RuntimeState>();
    Spot::RegisterScriptLanguage("TEST", {".replacement", ".REPLACEMENT"},
                                 [replacement](std::string_view, std::string_view) {
                                     CHECK(Spot::GetScriptLanguageForSource("reentrant.replacement") == "test");
                                     return std::make_unique<TestRuntime>(replacement);
                                 });
    CHECK_THROWS(Spot::GetScriptLanguageForSource("old.test"));
    CHECK(Spot::GetScriptLanguageForSource("new.REPLACEMENT") == "test");
    auto runtime = Spot::CreateScriptRuntime("TeSt", "", "");
    runtime->Initialize();
    CHECK(replacement->initialized);
    CHECK_FALSE(original->initialized);
    Spot::UnregisterScriptLanguage("TEST");
    CHECK_THROWS(Spot::GetScriptLanguageForSource("new.replacement"));
    CHECK_THROWS(Spot::CreateScriptRuntime("test", "", ""));
    runtime->Call("OnUpdate");
    CHECK(replacement->events.back() == "OnUpdate");
}

TEST_CASE("Script registry rejects invalid registrations without changing existing languages", "[scripting]") {
    TestLanguage language;
    const auto factory = [](std::string_view, std::string_view) {
        return std::make_unique<TestRuntime>(std::make_shared<RuntimeState>());
    };
    CHECK_THROWS_AS(Spot::RegisterScriptLanguage("other", {".TEST"}, factory), std::invalid_argument);
    CHECK_THROWS_AS(Spot::RegisterScriptLanguage("invalid name", {".other"}, factory), std::invalid_argument);
    CHECK_THROWS_AS(Spot::RegisterScriptLanguage("other", {"other"}, factory), std::invalid_argument);
    CHECK_THROWS_AS(Spot::RegisterScriptLanguage("other", {"../other"}, factory), std::invalid_argument);
    CHECK_THROWS_AS(Spot::RegisterScriptLanguage("test", {".test"}, {}), std::invalid_argument);
    CHECK(Spot::GetScriptLanguageForSource("file.test") == "test");
    CHECK(Spot::CreateScriptRuntime("test", "", "") != nullptr);
    Spot::RegisterScriptLanguage(
        "test", {".test"},
        [](std::string_view, std::string_view) -> std::unique_ptr<Spot::ScriptRuntime> { return {}; });
    CHECK_THROWS_WITH(Spot::CreateScriptRuntime("test", "", ""), "Script runtime factory returned null");
}

TEST_CASE("Script components serialize their language and embedded source", "[scripting]") {
    TestLanguage language;
    Spot::Scene scene;
    auto& component = language.Add(scene.CreateEntity("Scripted"));
    component.source = "embedded code";
    component.sourceName = "diagnostic-only";
    const std::string saved = scene.SaveToString();
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(saved));
    auto* restored = loaded.GetRootEntities()[0]->GetComponent<Spot::ScriptComponent>();
    REQUIRE(restored);
    CHECK(restored->language == "test");
    CHECK(restored->source == "embedded code");
    CHECK(restored->sourceName == "diagnostic-only");
    CHECK(Spot::GetComponentTypeName(*restored) == "ScriptComponent");
}

TEST_CASE("Script native library factories are shared across languages and create independent objects", "[scripting]") {
    class Counter final : public Spot::ScriptObject {
    public:
        Spot::ScriptValue Call(std::string_view name, std::span<const Spot::ScriptValue> arguments) override {
            if (name == "next" && arguments.empty())
                return static_cast<double>(++m_value);
            return ScriptObject::Call(name, arguments);
        }

    private:
        int m_value = 0;
    };
    TestLanguage first;
    TestLanguage second("second", {});
    int created = 0;
    Spot::RegisterScriptLibrary("counter", [&] {
        CHECK(Spot::GetScriptLanguageForSource("check.test") == "test");
        ++created;
        return std::make_shared<Counter>();
    });
    struct Cleanup {
        ~Cleanup() { Spot::UnregisterScriptLibrary("counter"); }
    } cleanup;
    CHECK_THROWS_AS(Spot::RegisterScriptLibrary("spot", [] { return std::make_shared<Counter>(); }),
                    std::invalid_argument);
    CHECK_THROWS_AS(Spot::RegisterScriptLibrary("counter", {}), std::invalid_argument);
    Spot::Scene scene;
    auto& a = first.Add(scene.CreateEntity());
    auto& b = second.Add(scene.CreateEntity());
    scene.Update(0.1f);
    CHECK_FALSE(a.IsFaulted());
    CHECK_FALSE(b.IsFaulted());
    CHECK(created == 2);
    auto one = first.state->libraries.at("counter");
    auto two = second.state->libraries.at("counter");
    CHECK(one != two);
    CHECK(std::get<double>(one->Call("next", {})) == 1);
    CHECK(std::get<double>(two->Call("next", {})) == 1);
    Spot::UnregisterScriptLibrary("counter");
    CHECK(std::get<double>(one->Call("next", {})) == 2);
    TestLanguage third("third", {});
    auto& c = third.Add(scene.CreateEntity());
    scene.Update(0.1f);
    CHECK_FALSE(c.IsFaulted());
    CHECK_FALSE(third.state->libraries.contains("counter"));
    CHECK(created == 2);
}

TEST_CASE("Script languages report the first syntax error of a source", "[script]") {
#ifdef SPOT_ENABLE_LUAU
    const auto luau = Spot::CheckScriptSyntax("luau", "function f()\n  return 1 +\nend", "scripts/f.luau");
    REQUIRE(luau);
    CHECK(luau->line == 3);
    CHECK(luau->column == 1);
    CHECK_FALSE(luau->message.empty());
    CHECK(luau->message.find("scripts/f.luau") == std::string::npos);
    CHECK_FALSE(
        Spot::CheckScriptSyntax("luau", "local function f(x: number): number\n  x += 1\n  return x\nend", "f.luau"));
#endif
    CHECK_FALSE(Spot::CheckScriptSyntax("unknown", "anything", "f.x"));
}

TEST_CASE("Every member the script API describes is one the scene bindings answer to", "[scripting]") {
    TestLanguage language;
    Spot::Scene scene;
    auto& entity = scene.CreateEntity("Described");
    language.Add(entity).source = "described";
    auto state = language.state;
    std::vector<std::string> missing;
    std::size_t checked = 0;
    state->initialize = [&] {
        const auto& library = state->libraries.at("spot");
        const auto owner = Object(library->Get("entity"));
        const std::vector<std::pair<std::string_view, Spot::ScriptObjectRef>> objects{
            {"Entity", owner},
            {"Transform", Object(owner->Get("transform"))},
            {"Component", Object(library->Get("Component"))},
            {"Scene", Object(library->Get("Scene"))},
            {"Input", Object(library->Get("Input"))},
            {"Time", Object(library->Get("Time"))},
            {"Physics", Object(library->Get("Physics"))},
        };
        // More arguments than any member takes, so methods are rejected before they do anything.
        const std::vector<Spot::ScriptValue> arguments(7, Spot::ScriptValue(true));
        for (const auto& [type, object] : objects) {
            const auto* described = Spot::ScriptApi::FindType(type);
            REQUIRE(described);
            for (const auto& member : described->members) {
                const std::string name = std::string(type) + "." + std::string(member.name);
                const bool property = member.kind == Spot::ScriptApi::MemberKind::Property;
                ++checked;
                if (property ? !object->HasProperty(member.name) : !object->HasMethod(member.name)) {
                    missing.push_back(name);
                    continue;
                }
                try {
                    if (property)
                        object->Get(member.name);
                    else
                        object->Call(member.name, arguments);
                } catch (const std::exception& error) {
                    if (std::string_view(error.what()).starts_with("Unknown script"))
                        missing.push_back(name);
                }
            }
        }
    };
    scene.Update(0.0f);
    REQUIRE_FALSE(entity.GetComponent<Spot::ScriptComponent>()->IsFaulted());
    CHECK(checked > 100);
    CHECK(missing.empty());
    for (const auto& name : missing)
        UNSCOPED_INFO(name);
}

TEST_CASE("Component script bindings tell tools their members and types", "[scripting]") {
    const auto members = Spot::GetComponentScriptMembers("SpriteRenderer");
    const auto find = [&](std::string_view name) {
        const auto found = std::ranges::find(members, name, &Spot::ScriptMemberInfo::name);
        REQUIRE(found != members.end());
        return *found;
    };
    CHECK(find("color").type == "Color");
    CHECK(find("color").writable);
    CHECK_FALSE(find("color").method);
    CHECK(find("GetColor").method);
    CHECK(find("GetColor").type == "Color");
    CHECK(find("SetColor").type.empty());
    CHECK(find("flippedX").type == "bool");
    CHECK(std::ranges::is_sorted(members, {}, &Spot::ScriptMemberInfo::name));

    const auto audio = Spot::GetComponentScriptMembers("AudioSource");
    const auto playing = std::ranges::find(audio, "playing", &Spot::ScriptMemberInfo::name);
    REQUIRE(playing != audio.end());
    CHECK(playing->type == "bool");
    CHECK_FALSE(playing->writable);
    const auto play = std::ranges::find(audio, "Play", &Spot::ScriptMemberInfo::name);
    REQUIRE(play != audio.end());
    CHECK(play->type.empty());

    CHECK(Spot::GetComponentScriptMembers("NoSuchComponent").empty());
}

TEST_CASE("Scene wrappers persist between hooks and release with their context", "[scripting]") {
    TestLanguage language;
    std::weak_ptr<Spot::ScriptObject> cached;
    {
        Spot::Scene scene;
        auto& entity = scene.CreateEntity("Cached");
        language.Add(entity);
        auto state = language.state;
        state->call = [&](std::string_view name, std::span<const Spot::ScriptValue>) {
            if (name != "OnUpdate")
                return;
            auto owner = Object(state->libraries.at("spot")->Get("entity"));
            auto transform = Object(owner->Get("transform"));
            if (auto previous = cached.lock())
                CHECK(previous == transform);
            cached = transform;
            const auto position = transform->ResolveProperty("position");
            transform->SetResolved(position, "position", Spot::Vector3{2, 3, 4});
            CHECK(std::get<Spot::Vector3>(transform->GetResolved(position, "position")) == Spot::Vector3{2, 3, 4});
            CHECK_THROWS(transform->GetResolved(transform->ResolveProperty("missing"), "missing"));
        };
        scene.Update(0.01f);
        REQUIRE_FALSE(cached.expired());
        scene.Update(0.01f);
        CHECK_FALSE(entity.GetComponent<Spot::ScriptComponent>()->IsFaulted());
    }
    language.state->libraries.clear();
    CHECK(cached.expired());
}

TEST_CASE("Reading bound properties does not copy heap backed getters", "[scripting]") {
    struct Bound final : Spot::Component {
        double value = 7;
    };
    struct Getter {
        std::shared_ptr<int> copies;
        std::array<char, 64> padding{};
        explicit Getter(std::shared_ptr<int> counter) : copies(std::move(counter)) {}
        Getter(const Getter& other) : copies(other.copies) { ++*copies; }
        double operator()(const Bound& component) const { return component.value; }
    };
    Spot::RegisterComponent<Bound>("PropertyProbe");
    struct Cleanup {
        ~Cleanup() { Spot::UnregisterComponent("PropertyProbe"); }
    } cleanup;
    auto copies = std::make_shared<int>(0);
    Spot::ScriptBindings<Bound>().ReadOnly("property_with_a_name_beyond_small_string_storage", Getter(copies));
    TestLanguage language;
    Spot::Scene scene;
    auto& entity = scene.CreateEntity();
    entity.AddComponent<Bound>();
    language.Add(entity);
    auto state = language.state;
    state->call = [&](std::string_view name, std::span<const Spot::ScriptValue>) {
        if (name != "OnUpdate")
            return;
        const auto owner = Object(state->libraries.at("spot")->Get("entity"));
        const auto component = Object(owner->Get("PropertyProbe"));
        *copies = 0;
        for (int i = 0; i < 100; ++i)
            CHECK(std::get<double>(component->Get("property_with_a_name_beyond_small_string_storage")) == 7);
        CHECK(*copies == 0);
    };
    scene.Update(0.01f);
    CHECK_FALSE(entity.GetComponent<Spot::ScriptComponent>()->IsFaulted());
}
