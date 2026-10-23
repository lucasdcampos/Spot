#include <vice/vice.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <limits>
#include <string>
#include <vector>

namespace {
Vice::Value Run(std::string_view source) {
    Vice::Instance instance(Vice::Program::Parse(source, "collections.vice"));
    instance.Initialize();
    return instance.Call("run");
}
} // namespace

TEST_CASE("ViceScript while loops nest and preserve lexical scope through break and continue", "[vice][loops]") {
    CHECK(std::get<double>(Run(R"(
        func run() {
            var total = 0
            var outer = 0
            while outer < 4 {
                outer += 1
                var inner = 0
                while true {
                    inner += 1
                    { var temporary = 100; if inner == 2 { continue } }
                    if inner == 4 { break }
                    total += outer * inner
                }
            }
            return total
        }
    )")) == 40);
    CHECK(std::get<double>(Run("func run() { while true { if true { return 7 } } }")) == 7);
    for (const auto* source : {"func run() { break }", "func run() { if false { continue } }"})
        CHECK_THROWS_AS(Vice::Program::Parse(source), Vice::Error);
}

TEST_CASE("ViceScript loops share the VM budget and recover after infinite execution", "[vice][loops]") {
    Vice::Instance instance(Vice::Program::Parse(R"(
        func infinite() { while true {} }
        func recursive() { for value in range(1000) { recursive() } }
        func healthy() { return 7 }
        func badCondition() { while 1 {} }
    )"));
    instance.Initialize();
    CHECK_THROWS_WITH(instance.Call("infinite"), Catch::Matchers::ContainsSubstring("Execution limit exceeded"));
    CHECK_THROWS_WITH(instance.Call("recursive"), Catch::Matchers::ContainsSubstring("limit exceeded"));
    CHECK_THROWS_WITH(instance.Call("badCondition"), Catch::Matchers::ContainsSubstring("Expected a boolean"));
    CHECK(std::get<double>(instance.Call("healthy")) == 7);
}

TEST_CASE("ViceScript loop exits release block locals and iterator references", "[vice][loops]") {
    std::weak_ptr<Vice::Object> weak;
    Vice::Instance instance(Vice::Program::Parse(R"(
        func run() {
            var count = 0
            while count < 3 {
                var temporary = Make()
                count += 1
                if count == 2 { break }
                continue
            }
            CheckReleased()
            for value in [Make()] { break }
            CheckReleased()
            for value in [Make()] { continue }
            CheckReleased()
        }
    )"));
    instance.Bind("Make", [&](auto) -> Vice::Value {
        auto object = std::make_shared<Vice::Object>();
        weak = object;
        return object;
    });
    int released = 0;
    instance.Bind("CheckReleased", [&](auto) -> Vice::Value {
        if (weak.expired())
            ++released;
        return {};
    });
    instance.Initialize();
    instance.Call("run");
    CHECK(released == 3);
}

TEST_CASE("ViceScript arrays have reference identity and support struct and nested collection writeback",
          "[vice][collections]") {
    CHECK(std::get<bool>(Run(R"(
        import core
        class Inventory { var items = [] }
        func run() {
            const items = [Vector3(1, 2, 3), [4, 5], {"hp": 10},]
            var alias = items
            alias[0].x += 3
            items[1][0] *= 2
            items[2]["hp"] -= 1
            var copied = items[0]
            copied.x = 99
            var a = Inventory()
            var b = Inventory()
            a.items.push(1)
            var valuesOk = items == alias and items != [] and items[0].x == 4 and items[1][0] == 8
            var instancesOk = items[2]["hp"] == 9 and a.items.length == 1 and b.items.length == 0
            var typesOk = core.type(items) == "array" and core.type(items[2]) == "map"
            return valuesOk and instancesOk and typesOk and core.type(range(2)) == "range"
        }
    )")));
}

TEST_CASE("ViceScript array methods validate bounds and return removed values", "[vice][collections]") {
    CHECK(std::get<bool>(Run(R"(
        func run() {
            var items = []
            items.push(1)
            items.push(3)
            items.insert(1, 2)
            var middle = items.remove_at(1)
            var last = items.pop()
            var first = items[0]
            items.clear()
            return middle == 2 and last == 3 and first == 1 and items.length == 0
        }
    )")));
    for (const auto* expression :
         {"[1][-1]", "[1][1]", "[1][0.5]", "[1][true]", "[1][\"0\"]", "[][0]", "[].pop()", "[1].insert(2, 3)",
          "[1].remove_at(1)", "[1].push()", "[1].clear(0)", "[1].unknown()", "1[0]"}) {
        INFO(expression);
        CHECK_THROWS_AS(Run("func run() { return " + std::string(expression) + " }"), Vice::Error);
    }
    CHECK_THROWS_AS(Run("func run() { const items = []; items = [] }"), Vice::Error);
    CHECK_THROWS_AS(Run("func run() { var items = []; items.length = 2 }"), Vice::Error);
    CHECK_THROWS_AS(Run("func run() { var items = [1]; items[2] = 3 }"), Vice::Error);
}

TEST_CASE("ViceScript maps use string keys and deterministic iteration", "[vice][collections]") {
    CHECK(std::get<bool>(Run(R"(
        func run() {
            const values = {"z": 3, "a": 1, "z": 4}
            values["b"] = 2
            values["z"] += 1
            var keys = ""
            var sum = 0
            for key, value in values { keys += key; sum += value }
            var single = ""
            for key in values { single += key }
            var sorted = values.keys()
            var missing = values.get("missing", 99)
            var removed = values.remove("b")
            var keysOk = keys == "abz" and single == keys and sum == 8 and sorted[0] == "a"
            var removalsOk = values.has("z") and not values.has("b") and removed and not values.remove("b")
            return keysOk and removalsOk and missing == 99 and values.get("absent") == null and values.length == 2
        }
    )")));
    for (const auto* body : {"var m = {1: 2}", "var m = {}; m[false] = 2", "var m = {}; return m[\"absent\"]",
                             "var m = {}; m[\"absent\"] += 1", "var m = {}; m.has()", "var m = {}; m.keys(1)"}) {
        INFO(body);
        CHECK_THROWS_AS(Run("func run() { " + std::string(body) + " }"), Vice::Error);
    }
}

TEST_CASE("ViceScript for loops evaluate iterables once and support range steps and local shadowing", "[vice][loops]") {
    CHECK(std::get<bool>(Run(R"(
        var calls = 0
        func Items() { calls += 1; return [2, 4, 6] }
        func run() {
            var value = 100
            var total = 0
            for index, value in Items() { total += index * value }
            for value in range(2, 8, 2) { if value == 4 { continue }; total += value }
            for value in range(5, 0, -2) { total += value; if value == 3 { break } }
            for value in [] { return false }
            for value in range(4, 1) { return false }
            return total == 32 and value == 100 and calls == 1 and range(1, 8, 2).length == 4
        }
    )")));
    for (const auto* expression : {"1", "null", "\"text\"", "range()", "range(1, 2, 0)", "range(1.5)",
                                   "range(0, 2000000)", "range(1, 2, 3, 4)"}) {
        INFO(expression);
        CHECK_THROWS_AS(Run("func run() { for item in " + std::string(expression) + " {} }"), Vice::Error);
    }
    for (const auto* source : {"func run() { for a, a in [] {} }", "func run() { for a [] {} }"})
        CHECK_THROWS_AS(Vice::Program::Parse(source), Vice::Error);
}

TEST_CASE("ViceScript iterators reject structural mutation while allowing value replacement", "[vice][loops]") {
    for (const auto* mutation : {"a.push(4)", "a.pop()", "a.remove_at(0)", "a.insert(0, 4)", "a.clear()"}) {
        INFO(mutation);
        CHECK_THROWS_WITH(Run("func run() { var a = [1, 2, 3]; for value in a { " + std::string(mutation) + " } }"),
                          Catch::Matchers::ContainsSubstring("structure changed"));
    }
    CHECK_THROWS_WITH(Run("func run() { var m = {\"a\": 1}; for key in m { m[\"b\"] = 2 } }"),
                      Catch::Matchers::ContainsSubstring("structure changed"));
    CHECK(std::get<bool>(Run(R"(
        func run() {
            var a = [1, 2]
            for i, value in a { a[i] += 10 }
            var m = {"a": 1, "b": 2}
            for key in m { m[key] += 10 }
            for value in a { a.clear(); break }
            return a.length == 0 and m["a"] == 11 and m["b"] == 12
        }
    )")));
}

TEST_CASE("ViceScript index assignment evaluates receivers and indices once before the right side",
          "[vice][collections]") {
    CHECK(std::get<bool>(Run(R"(
        var order = ""
        var values = [Vector3(1, 0, 0)]
        func Receiver() { order += "r"; return values }
        func Index() { order += "i"; return 0 }
        func Right() { order += "v"; return 2 }
        func run() {
            Receiver()[Index()].x += Right()
            return order == "riv" and values[0].x == 3
        }
    )")));
    class Indexed final : public Vice::Object {
    public:
        Vice::Value GetIndex(const Vice::Value&) const override {
            ++reads;
            return value;
        }
        void SetIndex(const Vice::Value&, const Vice::Value& replacement) override {
            value = replacement;
            ++writes;
        }
        mutable int reads = 0;
        int writes = 0;
        Vice::Value value{0.0};
    };
    Vice::Instance instance(Vice::Program::Parse("func run() { items[0] = 3; items[0] += 2 }"));
    auto object = std::make_shared<Indexed>();
    instance.BindValue("items", Vice::ObjectRef(object));
    instance.Initialize();
    instance.Call("run");
    CHECK(object->reads == 1);
    CHECK(object->writes == 2);
    CHECK(std::get<double>(object->value) == 5);
}

TEST_CASE("ViceScript collection cycles release and retained collections expire with their root",
          "[vice][collections]") {
    Vice::ObjectRef retained;
    Vice::ObjectRef nativeKeys;
    std::weak_ptr<Vice::Object> weak;
    {
        Vice::Instance instance(Vice::Program::Parse(R"(
            var array = []
            var map = {}
            func run() { array.push(map); map["array"] = array; Capture(array); return map }
        )"));
        instance.Bind("Capture", [&](std::span<const Vice::Value> arguments) -> Vice::Value {
            weak = std::get<Vice::ObjectRef>(arguments[0]);
            return {};
        });
        instance.Initialize();
        retained = std::get<Vice::ObjectRef>(instance.Call("run"));
        nativeKeys = std::get<Vice::ObjectRef>(retained->Call("keys", {}));
        CHECK_FALSE(weak.expired());
    }
    CHECK(weak.expired());
    CHECK_THROWS(retained->GetIndex(std::string("array")));
    CHECK_THROWS(nativeKeys->GetIndex(0.0));
}

TEST_CASE("ViceScript deep collection and instance graphs release without recursive native destruction",
          "[vice][collections]") {
    CHECK(std::get<double>(Run(R"(
        func run() {
            var nested = []
            for index in range(3000) { nested = [nested] }
            nested = null
            var maps = {}
            for index in range(3000) { maps = {"child": maps} }
            maps = null
            return 7
        }
    )")) == 7);
    CHECK(std::get<double>(Run(R"(
        class Node { var child }
        func run() {
            var nested = []
            for index in range(3000) {
                var node = Node()
                node.child = nested
                nested = [node]
            }
            nested = null
            return 7
        }
    )")) == 7);
}

TEST_CASE("ViceScript requires explicit compatible overrides before evaluating initializers", "[vice][override]") {
    for (const auto* declaration : {"class Base { func Tick(dt) {} }; class Child : Base { func Tick(dt) {} }",
                                    "class Base { func Tick(dt) {} }; class Child : Base { override func Tick() {} }",
                                    "class Base { func Tick(dt) {} }; class Child : Base { override func Tock(dt) {} }",
                                    "class Child { override func Tick(dt) {} }"}) {
        INFO(declaration);
        Vice::Instance instance(Vice::Program::Parse("var sideEffect = Native()\n" + std::string(declaration)));
        bool executed = false;
        instance.Bind("Native", [&](auto) -> Vice::Value {
            executed = true;
            return {};
        });
        CHECK_THROWS_AS(instance.Initialize(), Vice::Error);
        CHECK_FALSE(executed);
    }
    CHECK(std::get<double>(Run(R"(
        class Base { func Score(n) { return n } }
        class Child : Base { override func Score(n) { return super.Score(n) * 2 } }
        func run() { return Child().Score(3) }
    )")) == 6);
    CHECK_THROWS_AS(Vice::Program::Parse("class A { override func init() {} }"), Vice::Error);
    CHECK_THROWS_AS(Vice::Program::Parse("override func run() {}"), Vice::Error);
}
