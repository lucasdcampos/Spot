#include <vice/vice.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Script {
    explicit Script(std::string_view source) : instance(Vice::Program::Parse(source, "test.vice")) {
        instance.Bind("print", [this](std::span<const Vice::Value> values) -> Vice::Value {
            std::string message;
            bool first = true;
            for (const auto& value : values) {
                if (!first)
                    message += ' ';
                message += Vice::ToString(value);
                first = false;
            }
            output.push_back(std::move(message));
            return {};
        });
        instance.Initialize();
    }
    std::vector<std::string> output;
    Vice::Instance instance;
};

} // namespace

TEST_CASE("ViceScript uses null and variables can change type", "[vice]") {
    Script script(R"(
        var value
        const greeting = "hello"
        func start() {
            print(value, null, true, false)
            value = 42
            print(greeting, value)
            value = "changed"
            print(value)
        }
    )");
    script.instance.Call("start");
    CHECK(script.output == std::vector<std::string>{"null null true false", "hello 42", "changed"});
}

TEST_CASE("ViceScript honors arithmetic precedence and string escapes", "[vice]") {
    Script script(R"(
        func start() {
            print(2 + 3 * 4, (2 + 3) * 4, 12 / 3 - 1, -2 * +3, 1.5e2)
            print("hello" + " world", "a\n\t\"\\b")
            print()
        }
    )");
    script.instance.Call("start");
    CHECK(script.output == std::vector<std::string>{"14 20 3 -6 150", "hello world a\n\t\"\\b", ""});
}

TEST_CASE("ViceScript accepts comments CRLF semicolons and multiline arguments", "[vice]") {
    Script script("// comment\r\nconst n = 3;\r\nfunc start() { print(\r\nn,\r\n4\r\n); print(5) }\r\n");
    script.instance.Call("start");
    CHECK(script.output == std::vector<std::string>{"3 4", "5"});
}

TEST_CASE("ViceScript functions return values and keep lexical scope", "[vice]") {
    Script script(R"(
        var value = 10
        var initial = twice(3)
        func read() { return value }
        func twice(n) { return n * 2 }
        func empty() { return }
        func start() {
            var value = 20
            { var value = 30; print(value) }
            print(value, read(), twice(initial), empty())
            return
            print("unreachable")
        }
    )");
    script.instance.Call("start");
    CHECK(script.output == std::vector<std::string>{"30", "20 10 12 null"});
}

TEST_CASE("ViceScript instances share code and own their state", "[vice]") {
    const auto program = Vice::Program::Parse("var n = 0\nfunc next() { n = n + 1; return n }");
    Vice::Instance first(program);
    Vice::Instance second(program);
    first.Initialize();
    second.Initialize();
    CHECK(std::get<double>(first.Call("next")) == 1);
    CHECK(std::get<double>(first.Call("next")) == 2);
    CHECK(std::get<double>(second.Call("next")) == 1);
}

TEST_CASE("ViceScript retains program ownership and accepts injected host functions", "[vice]") {
    Vice::Instance instance(Vice::Program::Parse("func result() { return host(6, 7) }"));
    instance.Bind("host", [](std::span<const Vice::Value> values) -> Vice::Value {
        REQUIRE(values.size() == 2);
        return std::get<double>(values[0]) * std::get<double>(values[1]);
    });
    instance.Initialize();
    CHECK(std::get<double>(instance.Call("result")) == 42);
    CHECK_THROWS_AS(instance.Bind("late", [](auto) -> Vice::Value { return {}; }), std::logic_error);
    CHECK_THROWS_AS(instance.Initialize(), std::logic_error);
}

TEST_CASE("ViceScript syntax errors include source line and column", "[vice]") {
    try {
        Vice::Program::Parse("func start() {\n    print(\"hello\"\n}\n", "broken.vice");
        FAIL("Expected a syntax error");
    } catch (const Vice::Error& error) {
        CHECK(error.GetLocation().line == 3);
        CHECK(error.GetLocation().column == 1);
        CHECK_THAT(error.what(), Catch::Matchers::ContainsSubstring("broken.vice:3:1:"));
    }
    for (const auto* source : {"const x", "func f(a, a) {}", "var x = 1\nvar x = 2", "func f() {}\nfunc f() {}",
                               "func f() { print(1) print(2) }", "print(1)", "var x = 1e", "var x = 1e999",
                               "var x = \"unterminated", "var x = \"bad\\q\"", "func f() {"}) {
        INFO(source);
        CHECK_THROWS_AS(Vice::Program::Parse(source), Vice::Error);
    }
}

TEST_CASE("ViceScript runtime errors identify the failing instruction", "[vice]") {
    for (const auto* statement : {"missing = 1", "print(missing)", "missing()", "print(1 / 0)", "print(1 + true)",
                                  "print(-null)", "print(nil)", "fixed = 2", "var n = 1; n()", "var n; var n"}) {
        INFO(statement);
        Script script("const fixed = 1\nfunc start() {\n" + std::string(statement) + "\n}");
        CHECK_THROWS_WITH(script.instance.Call("start"), Catch::Matchers::ContainsSubstring("test.vice:3:"));
    }
}

TEST_CASE("ViceScript validates arguments and unwinds failed calls", "[vice]") {
    Script script("func fail(n) { var zero = 0; return n / zero }\nfunc fine() { return 7 }");
    CHECK_THROWS_AS(script.instance.Call("fail"), Vice::Error);
    const std::array<Vice::Value, 1> arguments{1.0};
    CHECK_THROWS_AS(script.instance.Call("fail", arguments), Vice::Error);
    CHECK(std::get<double>(script.instance.Call("fine")) == 7);
}

TEST_CASE("ViceScript bounds recursive execution and syntax nesting", "[vice]") {
    SECTION("calls") {
        Script script("func recurse() { return recurse() }");
        CHECK_THROWS_WITH(script.instance.Call("recurse"), Catch::Matchers::ContainsSubstring("limit exceeded"));
    }
    SECTION("syntax") {
        const std::string nested = "var x = " + std::string(100, '(') + "1" + std::string(100, ')');
        CHECK_THROWS_AS(Vice::Program::Parse(nested), Vice::Error);
    }
}

TEST_CASE("ViceScript initialization failures cannot leave a callable partial instance", "[vice]") {
    Vice::Instance instance(Vice::Program::Parse("var bad = missing\nfunc start() {}"));
    CHECK_THROWS_AS(instance.Call("start"), std::logic_error);
    CHECK_THROWS_AS(instance.Initialize(), Vice::Error);
    CHECK_THROWS_AS(instance.Call("start"), std::logic_error);
    CHECK_THROWS_AS(instance.Initialize(), std::logic_error);
}

TEST_CASE("ViceScript host failures are reported at their call site", "[vice]") {
    Vice::Instance instance(Vice::Program::Parse("func start() { host() }", "host.vice"));
    instance.Bind("host", [](auto) -> Vice::Value { throw std::runtime_error("host failed"); });
    instance.Initialize();
    CHECK_THROWS_WITH(instance.Call("start"), Catch::Matchers::ContainsSubstring("host.vice:1:16:"));
}

TEST_CASE("ViceScript bounds the work of a host call", "[vice]") {
    std::string source = "func f0() {}\n";
    for (int i = 1; i <= 18; ++i) {
        const std::string previous = "f" + std::to_string(i - 1) + "()";
        source += "func f" + std::to_string(i) + "() { " + previous + "; " + previous + " }\n";
    }
    Script script(source);
    CHECK_THROWS_WITH(script.instance.Call("f18"), Catch::Matchers::ContainsSubstring("Execution limit exceeded"));
}

TEST_CASE("ViceScript native name conflicts carry script locations", "[vice]") {
    for (const auto* source : {"var host = 1", "func host() {}"}) {
        Vice::Instance instance(Vice::Program::Parse(source, "conflict.vice"));
        CHECK_THROWS_WITH(instance.Bind("host", [](auto) -> Vice::Value { return {}; }),
                          Catch::Matchers::ContainsSubstring("conflict.vice:1:"));
    }
}

TEST_CASE("ViceScript branches have lexical scope and propagate returns", "[vice]") {
    Script script(R"(
        func choose(n) {
            var result = 9
            if n < 0 { return -1 }
            else if n == 0 {
                var result = 0
                return result
            }
            else { result = 1 }
            return result
        }
        func start() {
            print(choose(-2), choose(0), choose(3))
            if false { missing() }
            print(1 + 2 * 3 == 7 && 4 >= 4 || false)
            print(1 < 2, 2 <= 2, 3 > 2, 2 != 3, !false)
            print(null == null, null != 0, 1 == true, "a" == "a")
        }
    )");
    script.instance.Call("start");
    CHECK(script.output ==
          std::vector<std::string>{"-1 0 1", "true", "true true true true true", "true true false true"});
}

TEST_CASE("ViceScript logic short circuits and requires booleans", "[vice]") {
    Script script(R"(
        var calls = 0
        func touch() { calls += 1; return true }
        func start() {
            print(false && missing(), true || missing())
            print(true && touch(), false || touch(), calls)
            print(false and missing(), true or missing())
            print(true and touch(), false or touch(), calls)
            print(not false and false or true, not (1 == 2), not not true)
            print(true or false and false, not true and false)
            var android = true
            var origin = false
            var notice = false
            print(android and not origin or notice, true && not false, false or !false)
        }
    )");
    script.instance.Call("start");
    CHECK(script.output == std::vector<std::string>{"false true", "true true 2", "false true", "true true 4",
                                                    "true true true", "true false", "true true true"});
    for (const auto* statement :
         {"if 1 {}", "print(!null)", "print(1 && true)", "print(false || 1)", "print(1 < true)", "print(not null)",
          "print(1 and true)", "print(false or 1)", "if true { var local = 1 }; print(local)"}) {
        INFO(statement);
        Script invalid("func start() { " + std::string(statement) + " }");
        CHECK_THROWS_AS(invalid.instance.Call("start"), Vice::Error);
    }
}

TEST_CASE("ViceScript structs copy by value and support member writeback", "[vice]") {
    Script script(R"(
        func start() {
            var p = Vector3(1, 2, 3)
            var copy = p
            p.x += 4
            p.y *= 3
            p.z /= 3
            p.x -= 2
            print(p, copy)
            var size = Vector2(2, 4)
            size.y = 8
            var color = Color(1, 0, 0, 1)
            color.a = 0.5
            print(size.x, size.y, color.a)
            print(copy == Vector3(1, 2, 3), size != Vector2(2, 4))
            var text = "a"
            text += "b"
            print(text)
        }
    )");
    script.instance.Call("start");
    CHECK(script.output == std::vector<std::string>{"Vector3(3, 6, 1) Vector3(1, 2, 3)", "2 8 0.5", "true true", "ab"});
    for (const auto* statement :
         {"Vector2(1)", "Vector3(1, true, 3)", "Color(1, 2, 3)", "Vector2(1, 2).z", "var p = Vector2(1, 2); p.x = true",
          "const p = Vector2(1, 2); p.x = 3", "Vector3(1, 2, 3).x = 4", "var p = Vector2(1, 2); p.x /= 0"}) {
        INFO(statement);
        Script invalid("func start() { " + std::string(statement) + " }");
        CHECK_THROWS_AS(invalid.instance.Call("start"), Vice::Error);
    }
}

namespace {

class TestObject final : public Vice::Object {
public:
    Vice::Value Get(std::string_view name) const override {
        ++reads;
        if (name == "position")
            return position;
        if (name == "child")
            return child;
        if (name == "fault")
            throw std::runtime_error("getter failed");
        return Object::Get(name);
    }
    void Set(std::string_view name, const Vice::Value& value) override {
        if (name == "position") {
            position = std::get<Vice::Vector3>(value);
            ++writes;
        } else {
            Object::Set(name, value);
        }
    }
    Vice::Value Call(std::string_view name, std::span<const Vice::Value> arguments) override {
        if (name == "offset") {
            if (arguments.size() != 1)
                throw std::runtime_error("offset expects one argument");
            position.x += std::get<double>(arguments[0]);
            return position;
        }
        if (name == "child")
            return child;
        if (name == "fault")
            throw 1;
        return Object::Call(name, arguments);
    }
    mutable int reads = 0;
    int writes = 0;
    Vice::Vector3 position{1, 2, 3};
    Vice::ObjectRef child;
};

} // namespace

TEST_CASE("ViceScript host objects write nested values through setters once", "[vice]") {
    Vice::Instance instance(Vice::Program::Parse(R"(
        func start() {
            self.child.position.x += amount()
            self.child.position = Vector3(7, 8, 9)
            var snapshot = self.child.position
            snapshot.x = 100
            self.child.offset(2)
            return self.child.position.x
        }
    )"));
    auto root = std::make_shared<TestObject>();
    auto child = std::make_shared<TestObject>();
    root->child = child;
    instance.BindValue("self", root);
    int calls = 0;
    instance.Bind("amount", [&](auto) -> Vice::Value {
        ++calls;
        return 4.0;
    });
    instance.Initialize();
    CHECK(std::get<double>(instance.Call("start")) == 9);
    CHECK(child->position == Vice::Vector3{9, 8, 9});
    CHECK(child->writes == 2);
    CHECK(child->reads == 3);
    CHECK(calls == 1);
}

TEST_CASE("ViceScript assignment evaluates receivers before arguments and only once", "[vice]") {
    Vice::Instance instance(Vice::Program::Parse(R"(
        func start() { receiver().position.x += amount(); return receiver().offset(3).x }
    )"));
    auto object = std::make_shared<TestObject>();
    std::string order;
    instance.Bind("receiver", [&](auto) -> Vice::Value {
        order += 'r';
        return object;
    });
    instance.Bind("amount", [&](auto) -> Vice::Value {
        order += 'a';
        return 2.0;
    });
    instance.Initialize();
    CHECK(std::get<double>(instance.Call("start")) == 6);
    CHECK(order == "rar");
    CHECK(object->reads == 1);
    CHECK(object->writes == 1);
}

TEST_CASE("ViceScript reports native property and method failures at their source", "[vice]") {
    for (const auto* expression : {"self.fault", "self.fault()", "self.missing", "self.missing = 1", "self.offset()",
                                   "self.child.position", "null.x", "self = null"}) {
        INFO(expression);
        Vice::Instance instance(
            Vice::Program::Parse("func start() {\n" + std::string(expression) + "\n}", "object.vice"));
        instance.BindValue("self", std::make_shared<TestObject>());
        instance.Initialize();
        CHECK_THROWS_WITH(instance.Call("start"), Catch::Matchers::ContainsSubstring("object.vice:2:"));
    }
}

TEST_CASE("ViceScript validates host globals and constructor conflicts", "[vice]") {
    Vice::Instance instance(Vice::Program::Parse("func value() { counter += 1; return counter }"));
    instance.BindValue("counter", 1.0, false);
    CHECK_THROWS_AS(instance.BindValue("counter", 2.0), std::invalid_argument);
    CHECK_THROWS_AS(instance.Bind("counter", [](auto) -> Vice::Value { return {}; }), std::invalid_argument);
    CHECK_THROWS_AS(instance.BindValue("Vector2", 2.0), std::invalid_argument);
    instance.Initialize();
    CHECK(std::get<double>(instance.Call("value")) == 2);
    CHECK_THROWS_AS(instance.BindValue("late", 2.0), std::logic_error);
    for (const auto* source : {"var host", "func host() {}"}) {
        Vice::Instance conflict(Vice::Program::Parse(source));
        CHECK_THROWS_AS(conflict.BindValue("host", 1.0), Vice::Error);
    }
    for (const auto* source : {"var Vector2", "func Vector3() {}", "func f() { 1 = 2 }", "func f() { if true {} else }",
                               "func f() { true & false }"}) {
        INFO(source);
        CHECK_THROWS_AS(Vice::Program::Parse(source), Vice::Error);
    }
}

TEST_CASE("ViceScript unwinds nested branches and evaluates long property chains iteratively", "[vice]") {
    std::string body = "return recurse()";
    for (int i = 0; i < 8; ++i)
        body = "if true { " + body + " }";
    Script script("func recurse() { " + body + " }\nfunc fine() { return 7 }");
    CHECK_THROWS_WITH(script.instance.Call("recurse"), Catch::Matchers::ContainsSubstring("limit exceeded"));
    CHECK(std::get<double>(script.instance.Call("fine")) == 7);

    std::string chain = "self";
    for (int i = 0; i < 100; ++i)
        chain += ".child";
    auto object = std::make_shared<TestObject>();
    object->child = object;
    Vice::Instance instance(
        Vice::Program::Parse("func read() { return " + chain + " }\nfunc write() { " + chain + ".position.x += 3 }"));
    instance.BindValue("self", object);
    instance.Initialize();
    CHECK(std::get<Vice::ObjectRef>(instance.Call("read")) == object);
    instance.Call("write");
    CHECK(object->position.x == 4);
    CHECK(object->writes == 1);
    object->child.reset();
}

TEST_CASE("ViceScript distinguishes reference aliases from value copies and const bindings", "[vice]") {
    Vice::Instance instance(Vice::Program::Parse(R"(
        func start() {
            const alias = self
            alias.position.x = 5
            return self == alias && self != other && absent == null
        }
        func readonly() { self.position.x = 10 }
    )"));
    auto object = std::make_shared<TestObject>();
    instance.BindValue("self", object);
    instance.BindValue("other", std::make_shared<TestObject>());
    instance.BindValue("absent", Vice::ObjectRef{});
    instance.Initialize();
    CHECK(std::get<bool>(instance.Call("start")));
    CHECK(object->position.x == 5);
}

TEST_CASE("ViceScript simple property writes skip getters and failed writeback preserves values", "[vice]") {
    class WriteOnly final : public Vice::Object {
    public:
        void Set(std::string_view name, const Vice::Value& value) override {
            REQUIRE(name == "value");
            stored = std::get<double>(value);
        }
        double stored = 0;
    };
    Vice::Instance instance(Vice::Program::Parse(R"(
        func write() { sink.value = 9 }
        func compound() { sink.value += 1 }
        func readonly() { self.child = null }
        func snapshot() { const p = Vector3(1, 2, 3); p.x = 9 }
        func fine() { return 42 }
    )"));
    auto sink = std::make_shared<WriteOnly>();
    auto object = std::make_shared<TestObject>();
    instance.BindValue("sink", sink);
    instance.BindValue("self", object);
    instance.Initialize();
    instance.Call("write");
    CHECK(sink->stored == 9);
    CHECK_THROWS_AS(instance.Call("compound"), Vice::Error);
    CHECK(sink->stored == 9);
    CHECK_THROWS_AS(instance.Call("readonly"), Vice::Error);
    CHECK_THROWS_AS(instance.Call("snapshot"), Vice::Error);
    CHECK(std::get<double>(instance.Call("fine")) == 42);
}

TEST_CASE("ViceScript classes construct independent instances with reference identity", "[vice][classes]") {
    Script script(R"(
        import core
        var defaults = 3
        class Counter {
            var value = defaults
            var position = Vector3(1, 2, 3)
            var child
            const label = "counter"
            func init(value) { self.value = value }
            func add(amount) { self.value += amount; return self }
        }
        func run() {
            var defaults = 99
            var a = Counter(5)
            var b = Counter(7)
            const alias = a
            core.assert(alias == a and a != b)
            core.assert(a.add(2) == a)
            alias.position.x += 10
            a.child = b
            a.child.value += 1
            core.assert(a.value == 7 and b.value == 8)
            core.assert(a.position.x == 11 and b.position.x == 1)
            core.assert(a.label == "counter" and b.child == null)
            core.assert(core.type(a) == "object")
            return a
        }
        func bad_const() { var a = Counter(0); a.label = "changed" }
        func bad_field() { var a = Counter(0); a.missing = 1 }
        func bad_method() { Counter(0).missing() }
        func bad_arity() { Counter() }
    )");
    auto object = std::get<Vice::ObjectRef>(script.instance.Call("run"));
    CHECK(std::get<double>(object->Get("value")) == 7);
    const std::array<Vice::Value, 1> arguments{2.0};
    CHECK(std::get<Vice::ObjectRef>(object->Call("add", arguments)) == object);
    CHECK(std::get<double>(object->Get("value")) == 9);
    for (const auto* function : {"bad_const", "bad_field", "bad_method", "bad_arity"})
        CHECK_THROWS_AS(script.instance.Call(function), Vice::Error);
    CHECK_NOTHROW(script.instance.Call("run"));
}

TEST_CASE("ViceScript inheritance dispatches overrides and lexical super methods", "[vice][classes]") {
    Script script(R"(
        import core
        class Leaf extends Middle {
            var extra = 4
            override func score() { return super.score() + self.extra }
        }
        class Base {
            var value = 1
            func init(value) { self.value = value }
            func score() { return self.value }
            func dynamic_score() { return self.score() }
        }
        class Middle extends Base {
            func init(value) { super.init(value + 2) }
            override func score() { return super.score() * 3 }
        }
        class Empty {}
        class Plain extends Base {}
        func run() {
            var a = Leaf(5)
            core.assert(a.score() == 25 and a.dynamic_score() == 25)
            core.assert(a.extra == 4 and Plain(9).score() == 9)
            core.assert(Empty() != Empty())
            return true
        }
        func bad_args() { Empty(1) }
        func bad_self() { self = null }
    )");
    CHECK(std::get<bool>(script.instance.Call("run")));
    CHECK_THROWS_AS(script.instance.Call("bad_args"), Vice::Error);
    CHECK_THROWS_AS(script.instance.Call("bad_self"), Vice::Error);
}

TEST_CASE("ViceScript class fields initialize per object in declaration module scope", "[vice][classes]") {
    Script script(R"(
        var count = 0
        var seed = 6
        func next() { count += 1; return count }
        class Box {
            var serial = next()
            var value = seed
            var copied = self.value + 1
            var position = Vector3(1, 2, 3)
        }
        var global = Box()
        func run() {
            var seed = 99
            var box = Box()
            return global.serial == 1 and box.serial == 2 and box.value == 6 and box.copied == 7
        }
    )");
    CHECK(std::get<bool>(script.instance.Call("run")));
}

TEST_CASE("ViceScript rejects invalid class declarations and inheritance", "[vice][classes]") {
    for (const auto* source : {
             "class A {}\nclass A {}",
             "class Vector3 {}",
             "class A { var x; var x }",
             "class A { var x; func x() {} }",
             "class A { var init }",
             "class A { func f(self) {} }",
             "class A { class B {} }",
             "class A extends Missing {}",
             "class A extends A {}",
             "class A extends B {}\nclass B extends A {}",
             "class A {}\nclass B extends A { var x }\nclass C extends B { var x }",
             "class A { var x }\nclass B extends A { func x() {} }",
             "class A { func x() {} }\nclass B extends A { var x }",
         }) {
        INFO(source);
        CHECK_THROWS_AS(Script(source), Vice::Error);
    }
    Vice::Instance instance(Vice::Program::Parse("class Item {}"));
    CHECK_THROWS_AS(instance.BindValue("Item", 1.0), Vice::Error);
    CHECK_THROWS_AS(instance.Bind("Item", [](auto) -> Vice::Value { return {}; }), Vice::Error);
}

TEST_CASE("ViceScript bounds recursive constructors and methods and recovers after errors", "[vice][classes]") {
    Script script(R"(
        class Recursive { var child = Recursive() }
        class Method { func recurse() { return self.recurse() } }
        func fields() { Recursive() }
        func methods() { Method().recurse() }
        func ok() { return 42 }
    )");
    CHECK_THROWS_AS(script.instance.Call("fields"), Vice::Error);
    CHECK_THROWS_AS(script.instance.Call("methods"), Vice::Error);
    CHECK(std::get<double>(script.instance.Call("ok")) == 42);
    std::string source = "class C0 {}\n";
    for (int i = 1; i < 40; ++i)
        source += "class C" + std::to_string(i) + " extends C" + std::to_string(i - 1) + " {}\n";
    CHECK_THROWS_AS(Script(source), Vice::Error);
}

TEST_CASE("ViceScript retained instances expire and cyclic fields release at root destruction", "[vice][classes]") {
    Vice::ObjectRef retained;
    std::weak_ptr<Vice::Object> cycle;
    {
        Script script(R"(
            class Node {
                var next
                func ping() { return 1 }
            }
            func run() { var node = Node(); node.next = node; return node }
        )");
        retained = std::get<Vice::ObjectRef>(script.instance.Call("run"));
        cycle = std::get<Vice::ObjectRef>(script.instance.Call("run"));
        CHECK_FALSE(cycle.expired());
    }
    CHECK(cycle.expired());
    CHECK_THROWS(retained->Get("next"));
    CHECK_THROWS(retained->Set("next", {}));
    CHECK_THROWS(retained->Call("ping", {}));
}

TEST_CASE("ViceScript host classes inherit native operations and are visible in source modules", "[vice][classes]") {
    class Native final : public Vice::Object {
    public:
        double value = 1;
        bool HasMethod(std::string_view name) const override { return name == "Read"; }
        Vice::Value Get(std::string_view name) const override {
            if (name == "value")
                return value;
            return Object::Get(name);
        }
        void Set(std::string_view name, const Vice::Value& replacement) override {
            if (name == "value")
                value = std::get<double>(replacement);
            else
                Object::Set(name, replacement);
        }
        Vice::Value Call(std::string_view name, std::span<const Vice::Value> arguments) override {
            if (name == "Read" && arguments.empty())
                return value;
            return Object::Call(name, arguments);
        }
    };
    Vice::Instance instance(Vice::Program::Parse(R"(
        import parent
        class Child : parent.Parent {
            var local = 2
            override func Read() { return super.Read() + self.local }
            override func OnUpdate(dt) { self.value += dt }
        }
    )"));
    auto native = std::make_shared<Native>();
    instance.BindClass("Native", native, {{"OnUpdate", 1}, {"Read", 0}});
    instance.SetModuleLoader([](auto, auto) -> std::optional<Vice::ModuleSource> {
        return Vice::ModuleSource{"class Parent : Native {}", "parent.vice"};
    });
    instance.Initialize();
    const auto object = instance.CreateDerivedInstance("Native");
    CHECK(object->HasMethod("Read"));
    CHECK(object->HasMethod("OnUpdate"));
    CHECK_FALSE(object->HasMethod("Missing"));
    CHECK(std::get<double>(object->Call("Read", {})) == 3);
    const std::array<Vice::Value, 1> args{4.0};
    object->Call("OnUpdate", args);
    CHECK(std::get<double>(object->Get("value")) == 5);
    object->Set("value", 9.0);
    CHECK(native->value == 9);
    CHECK(std::get<double>(object->Call("Read", {})) == 11);
}

TEST_CASE("ViceScript host class constraints validate before script initializers", "[vice][classes]") {
    for (const auto* source : {
             "var side_effect = tick()\nclass Bad : Native { override func OnUpdate() {} }",
             "var side_effect = tick()\nclass Bad : Native { var OnUpdate }",
             "var side_effect = tick()\nclass Bad : Native { func Read(value) {} }",
             "var side_effect = tick()\nclass Native {}",
         }) {
        Vice::Instance instance(Vice::Program::Parse(source));
        int calls = 0;
        instance.Bind("tick", [&](auto) -> Vice::Value {
            ++calls;
            return {};
        });
        if (std::string_view(source).find("class Native") != std::string_view::npos) {
            CHECK_THROWS_AS(instance.BindClass("Native", std::make_shared<Vice::Object>()), Vice::Error);
        } else {
            instance.BindClass("Native", std::make_shared<Vice::Object>(), {{"OnUpdate", 1}, {"Read", 0}});
            CHECK_THROWS_AS(instance.Initialize(), Vice::Error);
        }
        CHECK(calls == 0);
    }
}

TEST_CASE("ViceScript derived instance discovery rejects missing ambiguous and non default constructible classes",
          "[vice][classes]") {
    for (const auto* source : {"", "class Unrelated {}", "class A : Native {}\nclass B : Native {}",
                               "class A : Native { func init(arg) {} }"}) {
        Vice::Instance instance(Vice::Program::Parse(source));
        instance.BindClass("Native", std::make_shared<Vice::Object>());
        CHECK_THROWS_AS(instance.CreateDerivedInstance("Native"), std::logic_error);
        instance.Initialize();
        CHECK_THROWS_AS(instance.CreateDerivedInstance("Native"), Vice::Error);
    }
    Vice::Instance instance(Vice::Program::Parse("class A : Native {}\nclass B : A {}"));
    instance.BindClass("Native", std::make_shared<Vice::Object>());
    instance.Initialize();
    CHECK(instance.CreateDerivedInstance("Native") != nullptr);
}

TEST_CASE("ViceScript implicit members use locals then instance members then lexical globals", "[vice][classes]") {
    Script script(R"(
        import core
        var value = 100
        func Score() { return 900 }
        func GlobalValue() { return value }
        class Base {
            var value = 3
            var copy = value + 1
            func Score() { return value * 2 }
            func DynamicScore() { return Score() }
        }
        class Child : Base {
            override func Score() { return super.Score() + 1 }
            func Set(value) { self.value = value }
            func Run() {
                core.assert(value == 3 and copy == 4)
                value += 2
                core.assert(Score() == 11 and DynamicScore() == 11)
                core.assert(GlobalValue() == 100)
                { var value = 50; value += 1; core.assert(value == 51 and self.value == 5) }
                Set(8)
                var Score = 3
                core.assert(Score == 3 and self.Score() == 17)
                return value
            }
            func LocalNotCallable() { var Score = 3; Score() }
        }
        var instance = Child()
        func run() { return instance.Run() }
        func bad() { instance.LocalNotCallable() }
        func global() { return value }
    )");
    CHECK(std::get<double>(script.instance.Call("run")) == 8);
    CHECK(std::get<double>(script.instance.Call("global")) == 100);
    CHECK_THROWS_AS(script.instance.Call("bad"), Vice::Error);
}

TEST_CASE("ViceScript implicit member writes preserve const fields and reject typos and method writes",
          "[vice][classes]") {
    Script script(R"(
        class Sample {
            const scalar = 3
            const point = Vector3(1, 2, 3)
            func Read() { return point.x }
            func BadScalar() { scalar = 9 }
            func BadPoint() { point.x += 2 }
            func Typo() { missing_field = 3 }
            func MethodWrite() { Read = 4 }
            func Recurse() { Recurse() }
        }
        var instance = Sample()
        func scalar() { instance.BadScalar() }
        func point() { instance.BadPoint() }
        func typo() { instance.Typo() }
        func method_write() { instance.MethodWrite() }
        func recurse() { instance.Recurse() }
        func read() { return instance.Read() }
    )");
    for (const auto* function : {"scalar", "point", "typo", "method_write", "recurse"})
        CHECK_THROWS_AS(script.instance.Call(function), Vice::Error);
    CHECK(std::get<double>(script.instance.Call("read")) == 1);
}

TEST_CASE("ViceScript host property discovery avoids getters during implicit assignment", "[vice][classes]") {
    class Native final : public Vice::Object {
    public:
        mutable int reads = 0;
        double value = 0;
        bool HasProperty(std::string_view name) const override { return name == "value"; }
        Vice::Value Get(std::string_view name) const override {
            if (name != "value")
                return Object::Get(name);
            ++reads;
            return value;
        }
        void Set(std::string_view name, const Vice::Value& replacement) override {
            if (name != "value") {
                Object::Set(name, replacement);
                return;
            }
            value = std::get<double>(replacement);
        }
    };
    Vice::Instance instance(Vice::Program::Parse(R"(
        var value = 99
        class Child : Native {
            func Write() { value = 3 }
            func Add() { value += 2 }
        }
    )"));
    const auto native = std::make_shared<Native>();
    instance.BindClass("Native", native);
    instance.Initialize();
    const auto object = instance.CreateDerivedInstance("Native");
    object->Call("Write", {});
    CHECK(native->value == 3);
    CHECK(native->reads == 0);
    object->Call("Add", {});
    CHECK(native->value == 5);
    CHECK(native->reads == 1);

    Vice::Instance shadow(Vice::Program::Parse("class Shadow : Native { var first = value; var value = 2 }"));
    shadow.BindClass("Native", native);
    shadow.Initialize();
    CHECK_THROWS_AS(shadow.CreateDerivedInstance("Native"), Vice::Error);
    CHECK(native->reads == 1);

    Vice::Instance writeShadow(Vice::Program::Parse(
        "class Shadow : Native { var first = Write(); var value = 2; func Write() { value = 9; return 0 } }"));
    writeShadow.BindClass("Native", native);
    writeShadow.Initialize();
    CHECK_THROWS_AS(writeShadow.CreateDerivedInstance("Native"), Vice::Error);
    CHECK(native->value == 5);
}

TEST_CASE("ViceScript uninitialized fields do not fall back to globals and fields can hold class aliases",
          "[vice][classes]") {
    Script script(R"(
        var later = 100
        class Invalid { var first = later; var later = 2 }
        class Value { var number = 7 }
        class Factory {
            const Type = Value
            func Create() { return Type().number + self.Type().number }
        }
        func invalid() { Invalid() }
        func create() { return Factory().Create() }
    )");
    CHECK_THROWS_AS(script.instance.Call("invalid"), Vice::Error);
    CHECK(std::get<double>(script.instance.Call("create")) == 14);
}

TEST_CASE("ViceScript VM grows its value and call stacks without losing arguments or pending writes", "[vice][vm]") {
    Script script(R"(
        func Sum(n, increment) {
            if n == 0 { return 0 }
            const saved = n * increment
            return saved + Sum(n - 1, increment)
        }
        func run() {
            var position = Vector3(1, 2, 3)
            position.x += Sum(400, 2)
            return position.x + position.y + position.z
        }
    )");
    CHECK(std::get<double>(script.instance.Call("run")) == 160406);
    CHECK(std::get<double>(script.instance.Call("run")) == 160406);
}

TEST_CASE("ViceScript VM schedules constructors field initializers and methods on its own stacks", "[vice][vm]") {
    Script script(R"(
        class Node {
            var child
            var depth = 0
            func init(n) {
                depth = n
                if n > 0 { child = Node(n - 1) }
            }
            func Count() {
                if child == null { return 1 }
                return 1 + child.Count()
            }
        }
        func run() { return Node(180).Count() }
    )");
    CHECK(std::get<double>(script.instance.Call("run")) == 181);
}

TEST_CASE("ViceScript VM resolves shadowing at declaration and clears slots on block exits", "[vice][vm]") {
    Script script(R"(
        var value = 10
        func run(value) {
            var result = value
            if true {
                var value = value + 1
                result += value
            }
            if false { var value = 100; return value }
            { const value = 2; result += value }
            return result + value
        }
    )");
    const std::array<Vice::Value, 1> arguments{3.0};
    CHECK(std::get<double>(script.instance.Call("run", arguments)) == 12);
}

TEST_CASE("ViceScript VM releases temporary arguments and scoped locals after calls", "[vice][vm]") {
    Vice::Instance instance(Vice::Program::Parse(R"(
        func Consume(value) { return null }
        func run() {
            Consume(Make())
            { var temporary = Make() }
            CheckReleased()
        }
    )"));
    std::weak_ptr<Vice::Object> temporary;
    instance.Bind("Make", [&](auto) -> Vice::Value {
        auto object = std::make_shared<TestObject>();
        temporary = object;
        return Vice::ObjectRef(object);
    });
    bool released = false;
    instance.Bind("CheckReleased", [&](auto) -> Vice::Value {
        released = temporary.expired();
        return {};
    });
    instance.Initialize();
    instance.Call("run");
    CHECK(released);
    CHECK(temporary.expired());
}

TEST_CASE("ViceScript VM bounds synchronous object callbacks and preserves native argument spans", "[vice][vm]") {
    Vice::Instance instance(Vice::Program::Parse(R"(
        class Actor {
            func Tick(n) { return Native(n) }
            func Deep(n) {
                if n == 0 { return 1 }
                return 1 + Deep(n - 1)
            }
        }
        var actor = Actor()
        func GetActor() { return actor }
        func healthy() { return 7 }
    )"));
    Vice::ObjectRef actor;
    instance.Bind("Native", [&](std::span<const Vice::Value> arguments) -> Vice::Value {
        if (std::get<double>(arguments[0]) == -1)
            return actor->Call("Tick", arguments);
        if (std::get<double>(arguments[0]) == -2)
            return instance.Call("healthy");
        const std::array<Vice::Value, 1> depth{400.0};
        if (std::get<double>(arguments[0]) == 5) {
            for (int i = 0; i < 100; ++i)
                actor->Call("Deep", depth);
        }
        const auto result = actor->Call("Deep", depth);
        return std::get<double>(result) + std::get<double>(arguments[0]);
    });
    instance.Initialize();
    actor = std::get<Vice::ObjectRef>(instance.Call("GetActor"));
    const std::array<Vice::Value, 1> argument{3.0}, recursive{-1.0}, rootCall{-2.0}, budget{5.0};
    CHECK(std::get<double>(actor->Call("Tick", argument)) == 404);
    CHECK_THROWS_WITH(actor->Call("Tick", recursive),
                      Catch::Matchers::ContainsSubstring("callback depth limit exceeded"));
    CHECK_THROWS_WITH(actor->Call("Tick", rootCall), Catch::Matchers::ContainsSubstring("cannot reenter"));
    CHECK_THROWS_WITH(actor->Call("Tick", budget), Catch::Matchers::ContainsSubstring("Execution limit exceeded"));
    CHECK(std::get<double>(instance.Call("healthy")) == 7);
}

TEST_CASE("ViceScript VM shares immutable code across independent concurrent instances", "[vice][vm]") {
    const auto program = Vice::Program::Parse("var count = 0\nfunc tick() { count += 1; return count }");
    std::atomic<bool> passed{true};
    std::vector<std::jthread> threads;
    for (int worker = 0; worker < 4; ++worker) {
        threads.emplace_back([&] {
            try {
                Vice::Instance instance(program);
                instance.Initialize();
                for (int i = 1; i <= 200; ++i) {
                    if (std::get<double>(instance.Call("tick")) != i)
                        passed = false;
                }
            } catch (...) {
                passed = false;
            }
        });
    }
    threads.clear();
    CHECK(passed.load());
}

TEST_CASE("ViceScript VM keeps foreign objects and class constructors in their owning session", "[vice][vm]") {
    Script foreign(R"(
        var offset = 40
        class Value { var number = offset; func Read() { return number + 2 } }
        var object = Value()
        func GetObject() { return object }
        func GetClass() { return Value }
    )");
    Vice::Instance instance(Vice::Program::Parse(R"(
        var offset = 100
        func run() { return foreign.Read() + ForeignClass().Read() }
    )"));
    instance.BindValue("foreign", foreign.instance.Call("GetObject"));
    instance.BindValue("ForeignClass", foreign.instance.Call("GetClass"));
    instance.Initialize();
    CHECK(std::get<double>(instance.Call("run")) == 84);
    Vice::Instance derived(Vice::Program::Parse("class Derived : ForeignClass {}"));
    derived.BindValue("ForeignClass", foreign.instance.Call("GetClass"));
    CHECK_THROWS_WITH(derived.Initialize(), Catch::Matchers::ContainsSubstring("another script instance"));
}

TEST_CASE("ViceScript functions are values that can be stored, passed and returned", "[vice][functions]") {
    Script script(R"(
        func Double(x) { return x * 2 }
        func Square(x) { return x * x }
        func Apply(f, value) { return f(value) }
        func Pick(name) {
            const table = {"double": Double, "square": Square}
            return table[name]
        }
        var transforms = [Double, Square]
        func start() {
            var f = Double
            print(f(3), Apply(Square, 4), Pick("square")(5), transforms[1](6))
            print(f == Double, Double == Square, core.type(f), f)
            var log = print
            log("logged", 1)
            const largest = math.max
            print(largest(2, 7))
        }
        import core
        import math
    )");
    script.instance.Call("start");
    REQUIRE(script.output.size() == 4);
    CHECK(script.output[0] == "6 16 25 36");
    CHECK(script.output[1] == "true false function <function>");
    CHECK(script.output[2] == "logged 1");
    CHECK(script.output[3] == "7");
}

TEST_CASE("ViceScript bound methods keep their receiver and dispatch virtually", "[vice][functions]") {
    Script script(R"(
        class Counter {
            var value = 0
            func add(amount) { value += amount; return value }
            func Adder() { return add }
        }
        class Double : Counter {
            override func add(amount) { return super.add(amount * 2) }
        }
        class Button {
            var onClick
            func Click() { return onClick() }
        }
        func start() {
            const a = Counter()
            const b = Double()
            var add = a.add
            add(2)
            add(3)
            const inner = b.Adder()
            inner(5)
            print(a.value, b.value, a.add == a.add, a.add == b.add)
            const button = Button()
            button.onClick = b.Adder()
            print(button.onClick == inner)
            button.onClick = func_value
            print(button.Click(), button.onClick())
        }
        func func_value() { return "clicked" }
    )");
    script.instance.Call("start");
    REQUIRE(script.output.size() == 3);
    CHECK(script.output[0] == "5 10 true false");
    CHECK(script.output[1] == "true");
    CHECK(script.output[2] == "clicked clicked");
}

TEST_CASE("ViceScript reports calls through non-callable values", "[vice][functions]") {
    Script script(R"(
        func One(x) { return x }
        func NotCallable() { var x = 3; return x() }
        func NullCall() { var list = [null]; return list[0]() }
        func WrongCount() { var f = One; return f(1, 2) }
    )");
    CHECK_THROWS_WITH(script.instance.Call("NotCallable"), Catch::Matchers::ContainsSubstring("not callable"));
    CHECK_THROWS_WITH(script.instance.Call("NullCall"), Catch::Matchers::ContainsSubstring("test.vice:4:"));
    CHECK_THROWS_WITH(script.instance.Call("WrongCount"), Catch::Matchers::ContainsSubstring("expects 1"));
}

TEST_CASE("ViceScript function values can be invoked by the host and across instances", "[vice][functions]") {
    std::optional<Vice::Value> callback;
    {
        Script script(R"(
            var calls = 0
            class Player { var health = 10; func Damage(amount) { health -= amount; return health } }
            const player = Player()
            func Count(step) { calls += step; return calls }
            func GetCount() { return Count }
            func GetDamage() { return player.Damage }
        )");
        const auto count = std::get<Vice::ObjectRef>(script.instance.Call("GetCount"));
        REQUIRE(count->IsCallable());
        const std::array arguments{Vice::Value(2.0)};
        CHECK(std::get<double>(count->Invoke(arguments)) == 2);
        CHECK(std::get<double>(count->Invoke(arguments)) == 4);
        const auto damage = std::get<Vice::ObjectRef>(script.instance.Call("GetDamage"));
        CHECK(std::get<double>(damage->Invoke(arguments)) == 8);

        Vice::Instance other(Vice::Program::Parse("func run() { return callback(3) }"));
        other.BindValue("callback", count);
        other.Initialize();
        CHECK(std::get<double>(other.Call("run")) == 7);
        callback = count;
    }
    const std::array arguments{Vice::Value(1.0)};
    CHECK_THROWS(std::get<Vice::ObjectRef>(*callback)->Invoke(arguments));
}

TEST_CASE("ViceScript anonymous functions capture variables lexically", "[vice][closures]") {
    Script script(R"(
        func MakeCounter(start) {
            var count = start
            return func(step) {
                count += step
                return count
            }
        }
        func start() {
            const a = MakeCounter(0)
            const b = MakeCounter(100)
            a(1)
            print(a(2), b(5), a == a, a == b)

            var shared = 1
            const read = func() { return shared }
            const write = func(value) { shared = value }
            write(7)
            print(read(), shared)

            var moved = Vector3(0, 0, 0)
            const move = func() { moved.x += 2 }
            move()
            move()
            print(moved.x)
        }
    )");
    script.instance.Call("start");
    REQUIRE(script.output.size() == 3);
    CHECK(script.output[0] == "3 105 true false");
    CHECK(script.output[1] == "7 7");
    CHECK(script.output[2] == "4");
}

TEST_CASE("ViceScript closures capture a fresh binding per loop iteration and block", "[vice][closures]") {
    Script script(R"(
        func start() {
            const actions = []
            for i in range(3) {
                actions.push(func() { return i })
            }
            var n = 0
            while n < 2 {
                const doubled = n * 2
                actions.push(func() { return doubled })
                n += 1
            }
            var text = ""
            for action in actions { text += core.to_string(action()) + " " }
            print(text)
        }
        import core
    )");
    script.instance.Call("start");
    REQUIRE(script.output.size() == 1);
    CHECK(script.output[0] == "0 1 2 0 2 ");
}

TEST_CASE("ViceScript nested closures, local functions and recursion", "[vice][closures]") {
    Script script(R"(
        func start() {
            var total = 0
            func Factorial(n) {
                if n <= 1 { return 1 }
                return n * Factorial(n - 1)
            }
            const outer = func(a) {
                return func(b) {
                    return func(c) { total += a + b + c; return total }
                }
            }
            print(Factorial(5), outer(1)(2)(3), outer(10)(20)(30), total)
        }
    )");
    script.instance.Call("start");
    REQUIRE(script.output.size() == 1);
    CHECK(script.output[0] == "120 6 66 66");
}

TEST_CASE("ViceScript closures created in methods keep their receiver", "[vice][closures]") {
    Script script(R"(
        class Base {
            func Describe() { return "base" }
        }
        class Player : Base {
            var health = 10
            func DamageCallback(amount) {
                return func() {
                    health -= amount
                    return core.to_string(health) + " " + super.Describe() + " " + Describe()
                }
            }
            override func Describe() { return "player" }
        }
        const player = Player()
        func start() {
            const hit = player.DamageCallback(3)
            hit()
            print(hit(), player.health)
        }
        import core
    )");
    script.instance.Call("start");
    REQUIRE(script.output.size() == 1);
    CHECK(script.output[0] == "4 base player 4");
}

TEST_CASE("ViceScript closures respect const captures and report errors", "[vice][closures]") {
    Script script(R"(
        func ConstWrite() {
            const fixed = 1
            const change = func() { fixed = 2 }
            change()
        }
        func Retained() {
            const pending = []
            for i in range(2) {
                const value = i * 10
                pending.push(func() { return value })
            }
            return pending
        }
        func Fails() {
            const f = func(x) { return x }
            return f()
        }
    )");
    CHECK_THROWS_WITH(script.instance.Call("ConstWrite"), Catch::Matchers::ContainsSubstring("const"));
    const auto pending = script.instance.Call("Retained");
    CHECK_THROWS_WITH(script.instance.Call("Fails"), Catch::Matchers::ContainsSubstring("expects 1"));
    REQUIRE(std::holds_alternative<Vice::ObjectRef>(pending));
    const auto second = std::get<Vice::ObjectRef>(pending)->GetIndex(1.0);
    CHECK(std::get<double>(std::get<Vice::ObjectRef>(second)->Invoke({})) == 10);
}

namespace {

struct Probe final : Vice::Object {
    explicit Probe(int& alive) : alive(alive) { ++alive; }
    ~Probe() override { --alive; }
    int& alive;
};

struct CycleScript {
    explicit CycleScript(std::string_view source) : instance(Vice::Program::Parse(source, "cycles.vice")) {
        instance.Bind("Probe", [this](std::span<const Vice::Value>) -> Vice::Value {
            return Vice::ObjectRef(std::make_shared<Probe>(alive));
        });
        instance.Initialize();
    }
    int alive = 0;
    Vice::Instance instance;
};

constexpr std::string_view CycleSource = R"(
    class Node {
        var next
        var probe = Probe()
        func Touch() { return probe }
    }
    var kept
    func Pair() { const a = Node(); const b = Node(); a.next = b; b.next = a }
    func SelfMethod() { const n = Node(); n.next = n.Touch }
    func ClosureBox() { const box = [Probe()]; box.push(func() { return box }) }
    func Recursive() {
        const probe = Probe()
        var again
        again = func() { return [again, probe] }
    }
    func MapCycle() { const map = {"probe": Probe()}; map["self"] = map }
    func Keep() { const n = Node(); n.next = n; kept = n }
    func Retain() { const n = Node(); n.next = n; return n }
    func KeptIsIntact() { return kept.next == kept and kept.Touch() != null }
    var counter
    func MakeCounter() {
        var count = 0
        const box = []
        counter = func() { count += 1; box.push(counter); return count }
    }
    func Count() { return counter() }
)";

} // namespace

TEST_CASE("ViceScript collects unreachable cycles of instances, collections and closures", "[vice][gc]") {
    CycleScript script(CycleSource);
    for (const auto* function : {"Pair", "SelfMethod", "ClosureBox", "Recursive", "MapCycle"})
        script.instance.Call(function);
    CHECK(script.alive == 6);
    CHECK(script.instance.CollectCycles() > 0);
    CHECK(script.alive == 0);
    CHECK(script.instance.CollectCycles() == 0);
}

TEST_CASE("ViceScript cycle collection keeps objects referenced by globals and the host", "[vice][gc]") {
    CycleScript script(CycleSource);
    script.instance.Call("Keep");
    auto retained = script.instance.Call("Retain");
    script.instance.Call("Pair");
    CHECK(script.alive == 4);
    script.instance.CollectCycles();
    CHECK(script.alive == 2);
    CHECK(std::get<bool>(script.instance.Call("KeptIsIntact")));
    script.instance.Call("MakeCounter");
    script.instance.Call("Count");
    script.instance.CollectCycles();
    CHECK(std::get<double>(script.instance.Call("Count")) == 2);
    const auto node = std::get<Vice::ObjectRef>(retained);
    CHECK(std::get<Vice::ObjectRef>(node->Get("next")) == node);
    retained = {};
    CHECK(script.alive == 2);
    script.instance.CollectCycles();
    CHECK(script.alive == 2);
    CHECK(std::get<Vice::ObjectRef>(node->Get("next")) == node);
}

TEST_CASE("ViceScript collects cycles automatically between host calls", "[vice][gc]") {
    CycleScript script(CycleSource);
    int peak = 0;
    for (int i = 0; i < 6000; ++i) {
        script.instance.Call("Pair");
        peak = std::max(peak, script.alive);
    }
    CHECK(peak < 6000);
    CHECK(script.alive < 6000);
}

TEST_CASE("ViceScript lists exported fields with constant defaults without running", "[vice][export]") {
    const auto program = Vice::Program::Parse(R"(
        class Player : Component {
            export var speed = 3.5
            export var lives = -2
            export var name = "hero"
            export var god = false
            export var offset = Vector3(1, -2, 3)
            export var tint = Color(1, 0.5, 0, 1)
            var hidden = missing()
        }
    )");
    const auto fields = program.GetExportedFields();
    REQUIRE(fields.size() == 6);
    CHECK(fields[0].className == "Player");
    CHECK(fields[0].name == "speed");
    CHECK(std::get<double>(fields[0].value) == 3.5);
    CHECK(std::get<double>(fields[1].value) == -2);
    CHECK(std::get<std::string>(fields[2].value) == "hero");
    CHECK_FALSE(std::get<bool>(fields[3].value));
    CHECK(std::get<Vice::Vector3>(fields[4].value) == Vice::Vector3{1, -2, 3});
    CHECK(std::get<Vice::Color>(fields[5].value) == Vice::Color{1, 0.5, 0, 1});
    for (const auto* source :
         {"class A { export const x = 1 }", "class A { export var x }", "class A { export var x = 1 + 2 }",
          "class A { export var x = Vector3(1, 2, y) }", "class A { export func F() {} }", "export var x = 1"})
        CHECK_THROWS_AS(Vice::Program::Parse(source), Vice::Error);
    Script script("class A { export var x = 4\n func Get() { return x } }\nfunc start() { print(A().Get()) }");
    script.instance.Call("start");
    CHECK(script.output == std::vector<std::string>{"4"});
}

TEST_CASE("ViceScript modulo is floored and has a compound assignment", "[vice][phase0]") {
    Script script(R"(
        func start() {
            var angle = 370
            angle %= 360
            print(7 % 3, -1 % 4, 5.5 % 2, 7 % -3, angle, 2 + 7 % 3 * 2)
        }
        func zero() { return 1 % 0 }
    )");
    script.instance.Call("start");
    CHECK(script.output == std::vector<std::string>{"1 3 1.5 -2 10 4"});
    CHECK_THROWS_WITH(script.instance.Call("zero"), Catch::Matchers::ContainsSubstring("Modulo by zero"));
}

TEST_CASE("ViceScript vectors and colors support arithmetic and vector methods", "[vice][phase0]") {
    Script script(R"(
        import math
        func start() {
            var position = Vector3(1, 2, 3)
            const velocity = Vector3(2, 0, -2)
            position += velocity * 0.5
            print(position, -position, 2 * Vector2(1, 2), Vector2(4, 2) / 2, Vector2(1, 2) * Vector2(3, 4))
            print(Color(1, 0.5, 0, 1) * 0.5, Color(0.5, 0.5, 0.5, 1) + Color(0.5, 0, 0, 0))
            print(Vector3(3, 4, 0).length(), Vector2(3, 4).normalized(), Vector3(0, 0, 0).normalized())
            print(Vector3(1, 0, 0).cross(Vector3(0, 1, 0)), Vector2(1, 2).dot(Vector2(3, 4)),
                  Vector3(1, 1, 1).distance(Vector3(1, 1, 3)), Vector2(1, 1).length_squared())
            print(math.lerp(Vector2(0, 0), Vector2(10, 20), 0.5), math.lerp(Color(0, 0, 0, 0), Color(1, 1, 1, 1), 0.25))
        }
        func mixed() { return Vector2(1, 1) + Vector3(1, 1, 1) }
        func added() { return Vector2(1, 1) + 1 }
        func divide() { return Vector2(1, 1) / Vector2(0, 1) }
    )");
    script.instance.Call("start");
    REQUIRE(script.output.size() == 5);
    CHECK(script.output[0] == "Vector3(2, 2, 2) Vector3(-2, -2, -2) Vector2(2, 4) Vector2(2, 1) Vector2(3, 8)");
    CHECK(script.output[1] == "Color(0.5, 0.25, 0, 0.5) Color(1, 0.5, 0.5, 1)");
    CHECK(script.output[2] == "5 Vector2(0.6, 0.8) Vector3(0, 0, 0)");
    CHECK(script.output[3] == "Vector3(0, 0, 1) 11 2 2");
    CHECK(script.output[4] == "Vector2(5, 10) Color(0.25, 0.25, 0.25, 0.25)");
    for (const auto* function : {"mixed", "added", "divide"})
        CHECK_THROWS_AS(script.instance.Call(function), Vice::Error);
}

TEST_CASE("ViceScript strings interpolate expressions with escapes", "[vice][phase0]") {
    Script script(R"(
        class Player { var name = "Ana"; var score = 12 }
        func start() {
            const player = Player()
            const items = ["sword", "shield"]
            print("{player.name} has {player.score * 2} points and {items.length} items: {items[0]}")
            print("braces \{literal\} and {"nested {1 + 1}"} at {Vector2(1, 2)} {null} {true}")
            print("{player.name}")
        }
    )");
    script.instance.Call("start");
    CHECK(script.output == std::vector<std::string>{"Ana has 24 points and 2 items: sword",
                                                    "braces {literal} and nested 2 at Vector2(1, 2) null true", "Ana"});
    for (const auto* source : {"func f() { return \"{}\" }", "func f() { return \"{1 + }\" }",
                               "func f() { return \"{1\" }", "func f() { return \"{\"x\" }"})
        CHECK_THROWS_AS(Vice::Program::Parse(source), Vice::Error);
    try {
        Vice::Program::Parse("func f() {\n    return \"value {1 +* 2}\"\n}", "test.vice");
        FAIL("expected a syntax error");
    } catch (const Vice::Error& error) {
        CHECK(error.GetLocation().line == 2);
        CHECK(error.GetLocation().column > 20);
    }
}

TEST_CASE("ViceScript is checks classes, inheritance and value types", "[vice][phase0]") {
    Script script(R"(
        class Animal {}
        class Dog : Animal {}
        class Car {}
        func start() {
            const dog = Dog()
            print(dog is Dog, dog is Animal, dog is Car, Animal() is Dog, null is Animal, 3 is Animal)
            print(Vector3(0, 0, 0) is Vector3, Vector2(0, 0) is Vector3, Color(0, 0, 0, 1) is Color, dog is Vector2)
            print(not (dog is Car) and dog is Animal)
        }
        func bad() { return Dog() is 3 }
    )");
    script.instance.Call("start");
    CHECK(script.output ==
          std::vector<std::string>{"true true false false false false", "true false true false", "true"});
    CHECK_THROWS_WITH(script.instance.Call("bad"), Catch::Matchers::ContainsSubstring("must be a class"));
}

TEST_CASE("ViceScript continues expressions after a trailing binary operator", "[vice][phase0]") {
    Script script(
        "func start() {\n    const total = 1 +\n        2 *\n        3\n    const ok = total == 7 and\n        true\n"
        "    print(total, ok)\n}");
    script.instance.Call("start");
    CHECK(script.output == std::vector<std::string>{"7 true"});
}

TEST_CASE("ViceScript errors list the script calls that led to them", "[vice][phase0]") {
    Script script("class Gun {\n    func Fire() { return Explode() }\n    func Explode() { return missing() }\n}\n"
                  "func Shoot() {\n    return Gun().Fire()\n}\nfunc start() {\n    Shoot()\n}\n"
                  "func direct() { return missing() }");
    try {
        script.instance.Call("start");
        FAIL("expected an error");
    } catch (const Vice::Error& error) {
        const std::string message = error.what();
        CHECK_THAT(message, Catch::Matchers::StartsWith("test.vice:3:"));
        CHECK(error.GetTrace() == "  at Explode (test.vice:3:29)\n  at Fire (test.vice:2:26)\n"
                                  "  at Shoot (test.vice:6:18)\n  at start (test.vice:9:5)");
        CHECK_THAT(message, Catch::Matchers::EndsWith(error.GetTrace()));
    }
    try {
        script.instance.Call("direct");
        FAIL("expected an error");
    } catch (const Vice::Error& error) {
        CHECK(error.GetTrace().empty());
    }
}

TEST_CASE("ViceScript standard libraries cover math, strings, numbers and random values", "[vice][phase0]") {
    Script script(R"(
        import math
        import string
        import core
        import random
        func start() {
            print(math.sign(-3), math.atan2(0, 1), math.pow(2, 10), math.log(8, 2), math.exp(0), math.tan(0))
            print(math.move_toward(0, 10, 3), math.move_toward(9, 10, 3), math.inverse_lerp(10, 20, 15),
                  math.smoothstep(0, 1, 0.5), math.smoothstep(0, 1, 2))
            const parts = string.split("a,b,,c", ",")
            print(parts.length, string.join(parts, "|"), string.join([1, true, "x"], " "))
            print(string.upper("Hello x"), string.lower("ABC"), "[" + string.trim("  pad \t") + "]",
                  string.starts_with("player_1", "player"), string.ends_with("file.vice", ".png"))
            print(core.to_number("2.5") + 1, core.to_number("abc") == null, core.to_number("") == null)
            random.seed(42)
            const first = [random.value(), random.range(5, 6), random.int(1, 3), random.chance(1), random.chance(0)]
            random.seed(42)
            const second = [random.value(), random.range(5, 6), random.int(1, 3), random.chance(1), random.chance(0)]
            var same = true
            for i, value in first { if value != second[i] { same = false } }
            print(same, first[0] >= 0 and first[0] < 1, first[1] >= 5 and first[1] < 6,
                  first[2] >= 1 and first[2] <= 3, first[3], first[4], random.choice(["only"]))
        }
        func badLog() { return math.log(0) }
        func badChoice() { return random.choice([]) }
        func badInt() { return random.int(1.5, 3) }
    )");
    script.instance.Call("start");
    REQUIRE(script.output.size() == 6);
    CHECK(script.output[0] == "-1 0 1024 3 1 0");
    CHECK(script.output[1] == "3 10 0.5 0.5 1");
    CHECK(script.output[2] == "4 a|b||c 1 true x");
    CHECK(script.output[3] == "HELLO X abc [pad] true false");
    CHECK(script.output[4] == "3.5 true true");
    CHECK(script.output[5] == "true true true true true false only");
    for (const auto* function : {"badLog", "badChoice", "badInt"})
        CHECK_THROWS_AS(script.instance.Call(function), Vice::Error);
}

TEST_CASE("ViceScript coroutines suspend at yield and resume where they left off", "[vice][coroutines]") {
    Script script(R"(
        import core
        var log = ""
        func Count(limit) {
            for i in range(limit) {
                log += "{i} "
                yield i * 10
            }
            return "finished"
        }
        func start() {
            const counter = core.coroutine(func() { return Count(3) })
            print(counter.status, counter.done)
            print(counter.resume(), counter.resume(), counter.status)
            print(counter.resume(), counter.resume(), counter.done, counter.status)
            print(log)
        }
    )");
    script.instance.Call("start");
    CHECK(script.output ==
          std::vector<std::string>{"suspended false", "0 10 suspended", "20 finished true dead", "0 1 2 "});
}

TEST_CASE("ViceScript coroutines keep locals, captures and receivers across resumes", "[vice][coroutines]") {
    Script script(R"(
        import core
        class Walker {
            var steps = 0
            func Walk() {
                var distance = 0
                while true {
                    distance += 2
                    steps += 1
                    yield distance
                }
            }
        }
        func start() {
            const walker = Walker()
            const walking = core.coroutine(walker.Walk)
            walking.resume()
            walking.resume()
            print(walking.resume(), walker.steps)

            var shared = 0
            var reader
            const capture = core.coroutine(func() {
                var local = 5
                reader = func() { return local + shared }
                yield null
                local = 7
                yield null
            })
            capture.resume()
            shared = 1
            print(reader())
            capture.resume()
            print(reader())
            capture.resume()
            print(reader(), capture.done)
        }
    )");
    script.instance.Call("start");
    CHECK(script.output == std::vector<std::string>{"6 3", "6", "8", "8 true"});
}

TEST_CASE("ViceScript coroutines nest and report misuse", "[vice][coroutines]") {
    Script script(R"(
        import core
        func start() {
            const inner = core.coroutine(func() { yield "a"; yield "b" })
            const outer = core.coroutine(func() {
                yield inner.resume()
                yield inner.resume()
            })
            print(outer.resume(), outer.resume(), inner.status)
        }
        func outside() { yield 1 }
        func finished() {
            const once = core.coroutine(func() { return 1 })
            once.resume()
            once.resume()
        }
        func reentrant() {
            var self_ref
            self_ref = core.coroutine(func() { self_ref.resume() })
            self_ref.resume()
        }
        func failing() {
            const broken = core.coroutine(func() { yield 1; missing() })
            broken.resume()
            broken.resume()
        }
        func across() {
            const items = [3, 1, 2]
            const sorter = core.coroutine(func() { core.assert(core.type(items) == "array"); yield 1 })
            return sorter.resume()
        }
    )");
    script.instance.Call("start");
    CHECK(script.output == std::vector<std::string>{"a b suspended"});
    CHECK_THROWS_WITH(script.instance.Call("outside"), Catch::Matchers::ContainsSubstring("while a coroutine runs"));
    CHECK_THROWS_WITH(script.instance.Call("finished"), Catch::Matchers::ContainsSubstring("finished coroutine"));
    CHECK_THROWS_WITH(script.instance.Call("reentrant"), Catch::Matchers::ContainsSubstring("running coroutine"));
    CHECK_THROWS_WITH(script.instance.Call("failing"),
                      Catch::Matchers::ContainsSubstring("Unknown function 'missing'"));
    CHECK(std::get<double>(script.instance.Call("across")) == 1);
}

TEST_CASE("ViceScript hosts resume coroutines and native calls cannot yield through them", "[vice][coroutines]") {
    Vice::Instance instance(Vice::Program::Parse(R"(
        func Steps() { yield 1; yield 2; return 3 }
        func GetSteps() { return Steps }
        func Through() { callback(func() { yield 1 }) }
        func Inside() { core.coroutine(Through).resume() }
        import core
    )"));
    instance.Bind("callback", [](std::span<const Vice::Value> arguments) -> Vice::Value {
        return std::get<Vice::ObjectRef>(arguments[0])->Invoke({});
    });
    instance.Initialize();
    const auto coroutine = instance.CreateCoroutine(std::get<Vice::ObjectRef>(instance.Call("GetSteps")));
    std::vector<double> results;
    while (!std::get<bool>(coroutine->Get("done")))
        results.push_back(std::get<double>(coroutine->Call("resume", {})));
    CHECK(results == std::vector<double>{1, 2, 3});
    CHECK_THROWS_WITH(instance.Call("Through"), Catch::Matchers::ContainsSubstring("while a coroutine runs"));
    CHECK_THROWS_WITH(instance.Call("Inside"), Catch::Matchers::ContainsSubstring("across a native call"));
    CHECK_THROWS_AS(instance.CreateCoroutine(nullptr), std::runtime_error);
}

TEST_CASE("ViceScript enums have named, ordered and comparable members", "[vice][enums]") {
    Script script(R"(
        import core
        enum State {
            Idle,
            Walking
            Running
        }
        enum Other { Idle }
        func start() {
            var state = State.Idle
            print(state, state.name, state.value, state == State.Idle, state == Other.Idle, core.type(state))
            state = State.Running
            print(state.value, State.values().length, State.values()[1], "{state}")
        }
        func write() { State.Idle = 3 }
    )");
    script.instance.Call("start");
    CHECK(script.output ==
          std::vector<std::string>{"State.Idle Idle 0 true false enum", "2 3 State.Walking State.Running"});
    CHECK_THROWS_AS(script.instance.Call("write"), Vice::Error);
    for (const auto* source : {"enum Empty {}", "enum A { X, X }", "enum A { values }", "enum A { X }\nvar A = 1",
                               "func f() { enum B { Y } }"})
        CHECK_THROWS_AS(Vice::Program::Parse(source), Vice::Error);
}

TEST_CASE("ViceScript match runs the first case whose pattern matches", "[vice][enums]") {
    Script script(R"(
        enum Direction { North, East, South, West }
        class Animal {}
        class Dog : Animal {}
        func Describe(value) {
            match value {
                Direction.North, Direction.South { return "vertical" }
                Direction.East { return "east" }
                1 + 1 { return "two" }
                "hello" { return "greeting" }
                is Dog { return "dog" }
                is Animal { return "animal" }
                is Vector3 { return "vector" }
                else { return "other" }
            }
        }
        func NoElse(value) {
            var result = "none"
            match value {
                1 { result = "one" }
            }
            return result
        }
        func start() {
            print(Describe(Direction.South), Describe(Direction.East), Describe(Direction.West), Describe(2))
            print(Describe("hello"), Describe(Dog()), Describe(Animal()), Describe(Vector3(0, 0, 0)), Describe(null))
            print(NoElse(1), NoElse(2))
            var count = 0
            for direction in Direction.values() {
                match direction {
                    Direction.North { continue }
                    else { count += 1 }
                }
            }
            print(count)
        }
    )");
    script.instance.Call("start");
    CHECK(script.output ==
          std::vector<std::string>{"vertical east other two", "greeting dog animal vector other", "one none", "3"});
    for (const auto* source : {"func f(x) { match x { else {} 1 {} } }", "func f(x) { match x { else {} else {} } }",
                               "func f(x) { match x 1 }", "func f(x) { match x { 1 } }"})
        CHECK_THROWS_AS(Vice::Program::Parse(source), Vice::Error);
}

TEST_CASE("ViceScript profiling records calls, instructions, allocations and time per function", "[vice][profile]") {
    Script script(R"(
        class Bullet {}
        func Spawn() {
            const bullets = []
            for i in range(10) { bullets.push(Bullet()) }
            return bullets.length
        }
        func Busy() {
            var total = 0
            for i in range(2000) { total += i % 7 }
            return total
        }
        func start() {
            Spawn()
            Spawn()
            Busy()
        }
    )");
    script.instance.Call("start");
    CHECK(script.instance.GetProfile().empty());
    script.instance.SetProfiling(true);
    script.instance.Call("start");
    const auto profile = script.instance.GetProfile();
    const auto find = [&](std::string_view name) {
        const auto found = std::ranges::find(profile, name, &Vice::FunctionProfile::name);
        REQUIRE(found != profile.end());
        return *found;
    };
    const auto spawn = find("Spawn");
    const auto busy = find("Busy");
    const auto start = find("start");
    CHECK(spawn.calls == 2);
    // Each call allocates its array, its range and ten bullets.
    CHECK(spawn.allocations == 24);
    CHECK(spawn.sourceName == "test.vice");
    CHECK(spawn.line == 3);
    CHECK(start.calls == 1);
    CHECK(busy.instructions > spawn.instructions);
    CHECK(busy.instructions > start.instructions);
    CHECK(busy.time > std::chrono::nanoseconds::zero());
    CHECK(profile.front().time >= profile.back().time);

    script.instance.ResetProfile();
    for (const auto& entry : script.instance.GetProfile())
        CHECK(entry.calls == 0);
    script.instance.SetProfiling(false);
    script.instance.Call("start");
    CHECK(script.instance.GetProfile().front().calls == 0);
}

TEST_CASE("ViceScript debugger stops at breakpoints with the call stack and locals", "[vice][debug]") {
    Script script("class Player {\n"                       // 1
                  "    var health = 10\n"                  // 2
                  "    func Hit(amount) {\n"               // 3
                  "        const left = health - amount\n" // 4
                  "        health = left\n"                // 5
                  "        return left\n"                  // 6
                  "    }\n"                                // 7
                  "}\n"                                    // 8
                  "func start() {\n"                       // 9
                  "    const player = Player()\n"          // 10
                  "    var total = 0\n"                    // 11
                  "    total += player.Hit(3)\n"           // 12
                  "    total += player.Hit(2)\n"           // 13
                  "    return total\n"                     // 14
                  "}\n");
    std::vector<Vice::DebugEvent> events;
    script.instance.SetDebugHandler([&](const Vice::DebugEvent& event) {
        events.push_back(event);
        return Vice::DebugAction::Continue;
    });
    script.instance.SetBreakpoints("test.vice", {5});
    CHECK(std::get<double>(script.instance.Call("start")) == 12);
    REQUIRE(events.size() == 2);
    const auto& first = events[0];
    CHECK(first.reason == Vice::DebugReason::Breakpoint);
    REQUIRE(first.frames.size() == 2);
    CHECK(first.frames[0].function == "Hit");
    CHECK(first.frames[0].location.line == 5);
    CHECK(first.frames[1].function == "start");
    CHECK(first.frames[1].location.line == 12);
    const auto local = [](const Vice::DebugFrame& frame, std::string_view name) -> std::optional<Vice::Value> {
        for (const auto& [localName, value] : frame.locals) {
            if (localName == name)
                return value;
        }
        return std::nullopt;
    };
    CHECK(std::get<double>(*local(first.frames[0], "amount")) == 3);
    CHECK(std::get<double>(*local(first.frames[0], "left")) == 7);
    CHECK(local(first.frames[0], "self").has_value());
    CHECK(std::get<double>(*local(first.frames[1], "total")) == 0);
    CHECK(std::get<double>(*local(events[1].frames[0], "left")) == 5);

    script.instance.ClearBreakpoints();
    events.clear();
    script.instance.Call("start");
    CHECK(events.empty());
}

TEST_CASE("ViceScript debugger steps in, over and out of calls", "[vice][debug]") {
    Script script("func Inner() {\n"      // 1
                  "    var a = 1\n"       // 2
                  "    a += 1\n"          // 3
                  "    return a\n"        // 4
                  "}\n"                   // 5
                  "func start() {\n"      // 6
                  "    var x = Inner()\n" // 7
                  "    x += Inner()\n"    // 8
                  "    return x\n"        // 9
                  "}\n");
    std::vector<Vice::DebugAction> actions;
    std::vector<std::string> stops;
    script.instance.SetDebugHandler([&](const Vice::DebugEvent& event) {
        stops.push_back(event.frames[0].function + ":" + std::to_string(event.frames[0].location.line));
        if (actions.empty())
            return Vice::DebugAction::Continue;
        const auto action = actions.front();
        actions.erase(actions.begin());
        return action;
    });
    script.instance.SetBreakpoints("test.vice", {7});
    actions = {Vice::DebugAction::StepIn, Vice::DebugAction::StepOver, Vice::DebugAction::StepOut,
               Vice::DebugAction::StepOver, Vice::DebugAction::StepOver};
    script.instance.Call("start");
    CHECK(stops == std::vector<std::string>{"start:7", "Inner:2", "Inner:3", "start:8", "start:9"});
}
