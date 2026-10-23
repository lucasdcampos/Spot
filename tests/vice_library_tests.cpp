#include <vice/vice.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace {
using Sources = std::unordered_map<std::string, Vice::ModuleSource>;
Vice::ModuleLoader Loader(Sources sources) {
    return [sources = std::move(sources)](std::string_view request,
                                          std::string_view) -> std::optional<Vice::ModuleSource> {
        const auto found = sources.find(std::string(request));
        return found != sources.end() ? std::optional(found->second) : std::nullopt;
    };
}
} // namespace

TEST_CASE("ViceScript standard math library validates domains and provides finite results", "[vice][libraries]") {
    Vice::Instance instance(Vice::Program::Parse(R"(
        import math
        import core
        func run() {
            core.assert(math.abs(-3) == 3)
            core.assert(math.min(3, 5) == 3 and math.max(3, 5) == 5)
            core.assert(math.clamp(8, 0, 4) == 4)
            core.assert(math.floor(1.8) == 1 and math.ceil(1.1) == 2 and math.round(-1.5) == -2)
            core.assert(math.sqrt(9) == 3 and math.sin(0) == 0 and math.cos(0) == 1)
            core.assert(math.lerp(2, 6, 0.25) == 3)
            core.assert(math.tau == math.pi * 2)
            return math.pi
        }
    )",
                                                 "math.vice"));
    instance.Initialize();
    CHECK(std::get<double>(instance.Call("run")) == Catch::Approx(3.141592653589793));
}

TEST_CASE("ViceScript string and core libraries expose explicit byte based operations", "[vice][libraries]") {
    Vice::Instance instance(Vice::Program::Parse(R"(
        import string
        import core
        func run() {
            core.assert(string.length("café") == 5)
            core.assert(string.find("banana", "an") == 1)
            core.assert(string.find("banana", "an", 2) == 3)
            core.assert(string.find("banana", "missing") == null)
            core.assert(string.substring("banana", 2, 3) == "nan")
            core.assert(string.substring("banana", 6) == "")
            core.assert(string.replace("banana", "an", "X") == "bXXa")
            core.assert(string.replace("aaa", "a", "") == "")
            core.assert(core.type(null) == "null" and core.type(false) == "boolean")
            core.assert(core.type(3) == "number" and core.type("a") == "string")
            core.assert(core.type(Vector3(1, 2, 3)) == "Vector3")
            core.assert(core.to_string(42) == "42" and core.to_string(null) == "null")
            return true
        }
    )"));
    instance.Initialize();
    CHECK(std::get<bool>(instance.Call("run")));
}

TEST_CASE("ViceScript library errors retain caller locations and unwind execution state", "[vice][libraries]") {
    for (const auto* expression :
         {"math.sqrt(-1)", "math.clamp(1, 3, 2)", "math.sin(true)", "math.abs()", "string.length(5)",
          "string.substring(\"abc\", -1)", "string.substring(\"abc\", 0.5)", "string.substring(\"abc\", 2, 2)",
          "string.find(\"abc\", \"a\", 4)", "string.replace(\"abc\", \"\", \"x\")",
          "core.assert(false, \"expected failure\")", "core.assert(1)"}) {
        INFO(expression);
        Vice::Instance instance(
            Vice::Program::Parse(std::string("import math\nimport string\nimport core\nfunc bad() { ") + expression +
                                     " }\nfunc healthy() { return 7 }",
                                 "bad.vice"));
        instance.Initialize();
        CHECK_THROWS_WITH(instance.Call("bad"), Catch::Matchers::ContainsSubstring("bad.vice:4:"));
        CHECK(std::get<double>(instance.Call("healthy")) == 7);
    }
}

TEST_CASE("ViceScript native libraries share one namespace protocol with imports and aliases", "[vice][libraries]") {
    auto combat = std::make_shared<Vice::Library>();
    combat->Constant("minimum", 2.0).Function("damage", [](std::span<const Vice::Value> args) -> Vice::Value {
        if (args.size() != 2)
            throw std::runtime_error("Expected attack and defense");
        return std::get<double>(args[0]) - std::get<double>(args[1]);
    });
    Vice::Instance instance(Vice::Program::Parse(R"(
        import combat
        import combat as Battle
        func run() { return combat == Battle and Battle.damage(10, 3) == 7 and combat.minimum == 2 }
        func mutate() { combat.minimum = 100 }
        func rebind() { combat = null }
    )"));
    instance.BindLibrary("combat", combat);
    instance.Initialize();
    CHECK(std::get<bool>(instance.Call("run")));
    CHECK_THROWS_AS(instance.Call("mutate"), Vice::Error);
    CHECK_THROWS_AS(instance.Call("rebind"), Vice::Error);
    CHECK(std::get<double>(combat->Get("minimum")) == 2);
    CHECK_THROWS_AS(combat->Function("minimum", [](std::span<const Vice::Value>) -> Vice::Value { return {}; }),
                    std::invalid_argument);
    CHECK_THROWS_AS(combat->Constant("if", 2.0), std::invalid_argument);
    CHECK_THROWS_AS(combat->Function("empty", {}), std::invalid_argument);
    CHECK_THROWS_AS(instance.BindLibrary("late", combat), std::logic_error);
    CHECK_THROWS_AS(instance.SetModuleLoader({}), std::logic_error);
}

TEST_CASE("ViceScript libraries require imports and reject conflicting declarations", "[vice][libraries]") {
    for (const auto* text : {"import", "import \"\"", "import \"bad-name.vice\"", "import math as if",
                             "import math\nvar math", "import math\nfunc math() {}", "import math\nimport math",
                             "import math as Vector3", "func run() { import math }"}) {
        INFO(text);
        CHECK_THROWS_AS(Vice::Program::Parse(text), Vice::Error);
    }
    Vice::Instance instance(Vice::Program::Parse("func run() { return math.pi }"));
    instance.Initialize();
    CHECK_THROWS_AS(instance.Call("run"), Vice::Error);
    Vice::Instance conflict(Vice::Program::Parse("import math"));
    CHECK_THROWS_AS(conflict.BindValue("math", 2.0), Vice::Error);
    CHECK_THROWS_AS(conflict.Bind("math", [](std::span<const Vice::Value>) -> Vice::Value { return {}; }), Vice::Error);
    CHECK_THROWS_AS(conflict.BindLibrary("math", std::make_shared<Vice::Library>()), std::invalid_argument);
    CHECK_THROWS_AS(conflict.BindLibrary("empty", {}), std::invalid_argument);
}

TEST_CASE("ViceScript source libraries initialize once per instance and export read only namespaces",
          "[vice][libraries]") {
    const Vice::ModuleSource combat{R"(
        import math
        var calls = initialized()
        const maximum = 100
        func bump() { calls += 1; return calls }
        func bounce(other) { var bump = 99; return other.bump() }
    )",
                                    "game/combat.vice"};
    const auto loader = Loader({{"combat", combat}, {"game/combat.vice", combat}});
    const auto program = Vice::Program::Parse(R"(
        import combat
        import "game/combat.vice" as Battle
        func run() { var calls = 999; return combat.bump() }
        func same() { return combat == Battle and Battle.maximum == 100 }
        func count() { return Battle.calls }
        func shadow() { return combat.bounce(combat) }
        func mutate() { combat.calls = 100 }
        func rebind() { combat = null }
    )",
                                              "root.vice");
    int initialized = 0;
    const auto create = [&] {
        auto instance = std::make_unique<Vice::Instance>(program);
        instance->SetModuleLoader(loader);
        instance->Bind("initialized", [&](std::span<const Vice::Value>) -> Vice::Value {
            ++initialized;
            return 0.0;
        });
        instance->Initialize();
        return instance;
    };
    auto first = create();
    CHECK(initialized == 1);
    CHECK(std::get<bool>(first->Call("same")));
    CHECK(std::get<double>(first->Call("run")) == 1);
    CHECK(std::get<double>(first->Call("run")) == 2);
    CHECK(std::get<double>(first->Call("count")) == 2);
    CHECK(std::get<double>(first->Call("shadow")) == 3);
    CHECK_THROWS_AS(first->Call("mutate"), Vice::Error);
    CHECK_THROWS_AS(first->Call("rebind"), Vice::Error);
    auto second = create();
    CHECK(initialized == 2);
    CHECK(std::get<double>(second->Call("run")) == 1);
    CHECK(std::get<double>(first->Call("count")) == 3);
}

TEST_CASE("ViceScript quoted imports infer aliases from file names", "[vice][libraries]") {
    Vice::Instance instance(Vice::Program::Parse("import \"game/combat.vice\"\nfunc run() { return combat.damage() }"));
    instance.SetModuleLoader(Loader({{"game/combat.vice", {"func damage() { return 9 }", "game/combat.vice"}}}));
    instance.Initialize();
    CHECK(std::get<double>(instance.Call("run")) == 9);
}

TEST_CASE("ViceScript reports missing modules and loader failures at the import location", "[vice][libraries]") {
    Vice::Instance instance(Vice::Program::Parse("import missing", "root.vice"));
    SECTION("No loader") {
    }
    SECTION("Missing source") {
        instance.SetModuleLoader(Loader({}));
    }
    SECTION("Invalid canonical source name") {
        instance.SetModuleLoader([](std::string_view, std::string_view) -> std::optional<Vice::ModuleSource> {
            return Vice::ModuleSource{"", ""};
        });
    }
    SECTION("Throwing loader") {
        instance.SetModuleLoader(
            [](std::string_view request, std::string_view importer) -> std::optional<Vice::ModuleSource> {
                CHECK(request == "missing");
                CHECK(importer == "root.vice");
                throw std::runtime_error("Unavailable source");
            });
    }
    CHECK_THROWS_WITH(instance.Initialize(), Catch::Matchers::ContainsSubstring("root.vice:1:"));
    CHECK_THROWS_AS(instance.Call("run"), std::logic_error);
}

TEST_CASE("ViceScript rejects canonical import cycles and excessive dependency depth", "[vice][libraries]") {
    SECTION("Cycle") {
        Vice::Instance instance(Vice::Program::Parse("import a", "root.vice"));
        instance.SetModuleLoader(Loader({{"a", {"import b", "a.vice"}}, {"b", {"import a", "b.vice"}}}));
        CHECK_THROWS_WITH(instance.Initialize(), Catch::Matchers::ContainsSubstring("b.vice:1:"));
    }
    SECTION("Dependency depth") {
        Vice::Instance instance(Vice::Program::Parse("import m0", "root.vice"));
        instance.SetModuleLoader([](std::string_view request, std::string_view) -> std::optional<Vice::ModuleSource> {
            const auto index = std::stoi(std::string(request.substr(1)));
            return Vice::ModuleSource{"import m" + std::to_string(index + 1), std::string(request) + ".vice"};
        });
        CHECK_THROWS_WITH(instance.Initialize(), Catch::Matchers::ContainsSubstring("Module dependency depth limit"));
    }
}

TEST_CASE("ViceScript errors inside source libraries retain their source locations", "[vice][libraries]") {
    SECTION("Initializer") {
        Vice::Instance instance(Vice::Program::Parse("import broken", "root.vice"));
        instance.SetModuleLoader(Loader({{"broken", {"import math\nvar value = math.sqrt(-1)", "broken.vice"}}}));
        CHECK_THROWS_WITH(instance.Initialize(), Catch::Matchers::ContainsSubstring("broken.vice:2:"));
    }
    SECTION("Function call and recovery") {
        Vice::Instance instance(Vice::Program::Parse(
            "import broken\nfunc bad() { broken.fail() }\nfunc healthy() { return 7 }", "root.vice"));
        instance.SetModuleLoader(
            Loader({{"broken", {"import math\nfunc fail() { return math.sqrt(-1) }", "broken.vice"}}}));
        instance.Initialize();
        CHECK_THROWS_WITH(instance.Call("bad"), Catch::Matchers::ContainsSubstring("broken.vice:2:"));
        CHECK(std::get<double>(instance.Call("healthy")) == 7);
    }
}

TEST_CASE("ViceScript shares execution limits across source module calls", "[vice][libraries]") {
    Sources sources;
    for (int i = 0; i < 20; ++i) {
        const auto name = "m" + std::to_string(i);
        const auto next = "m" + std::to_string(i + 1);
        const std::string source = i == 19 ? "func step() { tick(); return step() }"
                                           : "import " + next + "\nfunc step() { tick(); return " + next + ".step() }";
        sources.emplace(name, Vice::ModuleSource{source, name + ".vice"});
    }
    Vice::Instance instance(Vice::Program::Parse("import m0\nfunc run() { m0.step() }\nfunc healthy() { return 7 }"));
    instance.SetModuleLoader(Loader(std::move(sources)));
    int calls = 0;
    instance.Bind("tick", [&](std::span<const Vice::Value>) -> Vice::Value {
        ++calls;
        return {};
    });
    instance.Initialize();
    CHECK_THROWS_WITH(instance.Call("run"), Catch::Matchers::ContainsSubstring("limit exceeded"));
    CHECK(calls > 10);
    CHECK(calls < 1024);
    CHECK(std::get<double>(instance.Call("healthy")) == 7);
}

TEST_CASE("ViceScript releases module state even when modules retain their own references", "[vice][libraries]") {
    Vice::ObjectRef module;
    std::weak_ptr<int> lifetime;
    {
        auto guard = std::make_shared<int>(1);
        lifetime = guard;
        Vice::Instance instance(
            Vice::Program::Parse("import combat\nvar saved = combat.remember(combat)\nfunc get() { return combat }"));
        instance.SetModuleLoader(
            Loader({{"combat", {"var saved_self\nfunc remember(value) { saved_self = value }", "combat.vice"}}}));
        instance.Bind("keep_alive",
                      [guard](std::span<const Vice::Value>) -> Vice::Value { return static_cast<double>(*guard); });
        instance.Initialize();
        module = std::get<Vice::ObjectRef>(instance.Call("get"));
        CHECK(std::get<Vice::ObjectRef>(module->Get("saved_self")) == module);
    }
    CHECK(lifetime.expired());
    CHECK_THROWS_WITH(module->Get("saved_self"), "Module instance is no longer available");
    CHECK_THROWS_WITH(module->Call("remember", {}), "Module instance is no longer available");
}

TEST_CASE("ViceScript source modules do not inherit the importing script globals", "[vice][libraries]") {
    Vice::Instance instance(Vice::Program::Parse("import child\nfunc run() { return child.read() }"));
    instance.BindValue("secret", 42.0);
    instance.SetModuleLoader(Loader({{"child", {"func read() { return secret }", "child.vice"}}}));
    instance.Initialize();
    CHECK_THROWS_WITH(instance.Call("run"), Catch::Matchers::ContainsSubstring("Unknown variable 'secret'"));
}

TEST_CASE("ViceScript exports classes and inherits across source modules", "[vice][libraries][classes]") {
    const auto program = Vice::Program::Parse(R"(
        import models
        import core
        class Child extends models.Base {
            func init(value) { super.init(value + 1) }
            override func score() { return super.score() * 2 }
        }
        func run() {
            var base = models.Base(4)
            var child = Child(5)
            const Alias = models.Base
            core.assert(Alias(2).score() == 12)
            core.assert(base.score() == 14 and child.score() == 32)
            core.assert(child.dynamic_score() == 32)
            return models.count()
        }
    )",
                                              "main.vice");
    const Sources sources{{"models",
                           {R"(
        var offset = 10
        var instances = 0
        class Base {
            var value
            func init(value) { self.value = value; instances += 1 }
            func score() { return self.value + offset }
            func dynamic_score() { return self.score() }
        }
        func count() { return instances }
    )",
                            "models.vice"}}};
    Vice::Instance first(program);
    Vice::Instance second(program);
    first.SetModuleLoader(Loader(sources));
    second.SetModuleLoader(Loader(sources));
    first.Initialize();
    second.Initialize();
    CHECK(std::get<double>(first.Call("run")) == 3);
    CHECK(std::get<double>(first.Call("run")) == 6);
    CHECK(std::get<double>(second.Call("run")) == 3);
}

TEST_CASE("ViceScript module class dispatch does not export host natives or built in constructors",
          "[vice][libraries][classes]") {
    Vice::Instance instance(Vice::Program::Parse(R"(
        import model
        func native_call() { model.secret() }
        func builtin_call() { model.Vector3(1, 2, 3) }
        func class_call() { return model.Item() }
    )"));
    int calls = 0;
    instance.Bind("secret", [&](std::span<const Vice::Value>) -> Vice::Value {
        ++calls;
        return {};
    });
    instance.SetModuleLoader(Loader({{"model", {"class Item {}", "model.vice"}}}));
    instance.Initialize();
    CHECK_THROWS_AS(instance.Call("native_call"), Vice::Error);
    CHECK_THROWS_AS(instance.Call("builtin_call"), Vice::Error);
    CHECK(calls == 0);
    CHECK(std::get<Vice::ObjectRef>(instance.Call("class_call")) != nullptr);
}

TEST_CASE("ViceScript inherited implicit members keep their declaring module globals", "[vice][libraries][classes]") {
    Vice::Instance instance(Vice::Program::Parse(R"(
        import model
        var globalValue = 100
        class Child : model.Base {
            override func Score() { return super.Score() * 2 }
        }
        func run() { return Child().Run() }
    )"));
    instance.SetModuleLoader(Loader({{"model",
                                      {R"(
        var globalValue = 10
        func Helper() { return globalValue }
        class Base {
            var value = 3
            func Score() { return value + globalValue }
            func Run() { value += 1; return Score() + Helper() }
        }
    )",
                                       "model.vice"}}}));
    instance.Initialize();
    CHECK(std::get<double>(instance.Call("run")) == 38);
}
