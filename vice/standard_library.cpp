#include "standard_library.h"
#include "collections.h"
#include "runtime.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <numbers>
#include <random>

namespace Vice::Detail {
namespace {
void Count(std::span<const Value> arguments, std::size_t minimum, std::size_t maximum) {
    if (arguments.size() < minimum || arguments.size() > maximum)
        throw std::runtime_error("Incorrect argument count");
}
const std::string& Text(const Value& value) {
    const auto* text = std::get_if<std::string>(&value);
    if (!text)
        throw std::runtime_error("Expected a string");
    return *text;
}
std::size_t Index(const Value& value, std::size_t maximum) {
    const double number = Number(value);
    if (std::trunc(number) != number || number < 0 || number > static_cast<double>(maximum) ||
        number > 9007199254740991.0)
        throw std::runtime_error("String index is out of range");
    return static_cast<std::size_t>(number);
}
Value Finite(double value) {
    if (!std::isfinite(value))
        throw std::runtime_error("Numeric result is not finite");
    return value;
}
std::shared_ptr<Library> Math() {
    auto library = std::make_shared<Library>();
    library->Constant("pi", std::numbers::pi).Constant("tau", 2 * std::numbers::pi);
    const auto unary = [&](std::string name, double (*function)(double)) {
        library->Function(std::move(name), [function](std::span<const Value> arguments) -> Value {
            Count(arguments, 1, 1);
            return Finite(function(Number(arguments[0])));
        });
    };
    unary("abs", std::abs);
    unary("floor", std::floor);
    unary("ceil", std::ceil);
    unary("round", std::round);
    unary("sqrt", std::sqrt);
    unary("sin", std::sin);
    unary("cos", std::cos);
    unary("tan", std::tan);
    unary("asin", std::asin);
    unary("acos", std::acos);
    unary("atan", std::atan);
    unary("exp", std::exp);
    library->Function("sign", [](std::span<const Value> a) -> Value {
        Count(a, 1, 1);
        const double x = Number(a[0]);
        return x > 0 ? 1.0 : x < 0 ? -1.0 : 0.0;
    });
    library->Function("atan2", [](std::span<const Value> a) -> Value {
        Count(a, 2, 2);
        return Finite(std::atan2(Number(a[0]), Number(a[1])));
    });
    library->Function("pow", [](std::span<const Value> a) -> Value {
        Count(a, 2, 2);
        return Finite(std::pow(Number(a[0]), Number(a[1])));
    });
    library->Function("log", [](std::span<const Value> a) -> Value {
        Count(a, 1, 2);
        const double x = Number(a[0]);
        if (x <= 0)
            throw std::runtime_error("Logarithm of a value that is not positive");
        if (a.size() == 1)
            return Finite(std::log(x));
        const double base = Number(a[1]);
        if (base <= 0 || base == 1)
            throw std::runtime_error("Logarithm base must be positive and not 1");
        return Finite(std::log(x) / std::log(base));
    });
    library->Function("move_toward", [](std::span<const Value> a) -> Value {
        Count(a, 3, 3);
        const double current = Number(a[0]), target = Number(a[1]), step = Number(a[2]);
        if (std::abs(target - current) <= step)
            return target;
        return current + (target > current ? step : -step);
    });
    library->Function("inverse_lerp", [](std::span<const Value> a) -> Value {
        Count(a, 3, 3);
        const double from = Number(a[0]), to = Number(a[1]);
        if (from == to)
            throw std::runtime_error("Inverse lerp needs two different ends");
        return Finite((Number(a[2]) - from) / (to - from));
    });
    library->Function("smoothstep", [](std::span<const Value> a) -> Value {
        Count(a, 3, 3);
        const double from = Number(a[0]), to = Number(a[1]);
        if (from == to)
            throw std::runtime_error("Smoothstep needs two different edges");
        const double t = std::clamp((Number(a[2]) - from) / (to - from), 0.0, 1.0);
        return t * t * (3 - 2 * t);
    });
    library->Function("min", [](std::span<const Value> a) -> Value {
        Count(a, 2, 2);
        return std::min(Number(a[0]), Number(a[1]));
    });
    library->Function("max", [](std::span<const Value> a) -> Value {
        Count(a, 2, 2);
        return std::max(Number(a[0]), Number(a[1]));
    });
    library->Function("clamp", [](std::span<const Value> a) -> Value {
        Count(a, 3, 3);
        const double value = Number(a[0]), low = Number(a[1]), high = Number(a[2]);
        if (low > high)
            throw std::runtime_error("Clamp minimum exceeds maximum");
        return std::clamp(value, low, high);
    });
    library->Function("lerp", [](std::span<const Value> a) -> Value {
        Count(a, 3, 3);
        // Vectors and colors interpolate component by component.
        if (IsStruct(a[0]) || IsStruct(a[1])) {
            if (a[0].index() != a[1].index())
                throw std::runtime_error("Lerp ends must have the same type");
            return Binary(Operation::Add, a[0],
                          Binary(Operation::Multiply, Binary(Operation::Subtract, a[1], a[0]), Number(a[2])));
        }
        return Finite(std::lerp(Number(a[0]), Number(a[1]), Number(a[2])));
    });
    return library;
}
std::shared_ptr<Library> Strings() {
    auto library = std::make_shared<Library>();
    library->Function("length", [](std::span<const Value> a) -> Value {
        Count(a, 1, 1);
        return static_cast<double>(Text(a[0]).size());
    });
    library->Function("find", [](std::span<const Value> a) -> Value {
        Count(a, 2, 3);
        const auto& text = Text(a[0]);
        const auto position = text.find(Text(a[1]), a.size() == 3 ? Index(a[2], text.size()) : 0);
        return position == std::string::npos ? Value{} : Value(static_cast<double>(position));
    });
    library->Function("substring", [](std::span<const Value> a) -> Value {
        Count(a, 2, 3);
        const auto& text = Text(a[0]);
        const auto start = Index(a[1], text.size());
        const auto length = a.size() == 3 ? Index(a[2], text.size() - start) : text.size() - start;
        return text.substr(start, length);
    });
    library->Function("replace", [](std::span<const Value> a) -> Value {
        Count(a, 3, 3);
        const auto& text = Text(a[0]);
        const auto& needle = Text(a[1]);
        const auto& replacement = Text(a[2]);
        if (needle.empty())
            throw std::runtime_error("Replacement search string must not be empty");
        std::string result;
        std::size_t offset = 0;
        for (;;) {
            const auto next = text.find(needle, offset);
            if (next == std::string::npos) {
                result.append(text, offset);
                break;
            }
            result.append(text, offset, next - offset);
            result += replacement;
            offset = next + needle.size();
        }
        return result;
    });
    library->Function("split", [](std::span<const Value> a) -> Value {
        Count(a, 2, 2);
        const auto& text = Text(a[0]);
        const auto& separator = Text(a[1]);
        if (separator.empty())
            throw std::runtime_error("Split separator must not be empty");
        std::vector<Value> parts;
        std::size_t offset = 0;
        for (;;) {
            if (parts.size() >= MaxCollectionSize)
                throw std::runtime_error("Collection size limit exceeded");
            const auto next = text.find(separator, offset);
            parts.emplace_back(text.substr(offset, next == std::string::npos ? std::string::npos : next - offset));
            if (next == std::string::npos)
                break;
            offset = next + separator.size();
        }
        return MakeArray(parts);
    });
    library->Function("join", [](std::span<const Value> a) -> Value {
        Count(a, 2, 2);
        const auto* reference = std::get_if<ObjectRef>(&a[0]);
        const auto collection = reference ? std::dynamic_pointer_cast<Collection>(*reference) : nullptr;
        if (!collection || collection->kind != Collection::Kind::Array)
            throw std::runtime_error("Expected an array to join");
        const auto& separator = Text(a[1]);
        std::string result;
        for (std::size_t i = 0; i < collection->GetSize(); ++i) {
            if (i > 0)
                result += separator;
            result += ToString(collection->GetIndex(static_cast<double>(i)));
        }
        return result;
    });
    const auto transform = [&](std::string name, char (*function)(char)) {
        library->Function(std::move(name), [function](std::span<const Value> a) -> Value {
            Count(a, 1, 1);
            auto text = Text(a[0]);
            std::ranges::transform(text, text.begin(), function);
            return text;
        });
    };
    // ASCII letters only: other UTF-8 bytes are kept as they are.
    transform("upper", [](char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; });
    transform("lower", [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    library->Function("trim", [](std::span<const Value> a) -> Value {
        Count(a, 1, 1);
        const auto& text = Text(a[0]);
        constexpr std::string_view Space = " \t\r\n";
        const auto first = text.find_first_not_of(Space);
        if (first == std::string::npos)
            return std::string();
        return text.substr(first, text.find_last_not_of(Space) - first + 1);
    });
    library->Function("starts_with", [](std::span<const Value> a) -> Value {
        Count(a, 2, 2);
        return Text(a[0]).starts_with(Text(a[1]));
    });
    library->Function("ends_with", [](std::span<const Value> a) -> Value {
        Count(a, 2, 2);
        return Text(a[0]).ends_with(Text(a[1]));
    });
    return library;
}
// Each root instance draws from its own generator, seeded randomly until a script calls seed().
std::shared_ptr<Library> Random() {
    auto library = std::make_shared<Library>();
    auto generator = std::make_shared<std::mt19937_64>(std::random_device{}());
    library->Function("seed", [generator](std::span<const Value> a) -> Value {
        Count(a, 1, 1);
        const double seed = Number(a[0]);
        if (std::trunc(seed) != seed || std::abs(seed) > 9007199254740991.0)
            throw std::runtime_error("Seed must be an exact integer");
        generator->seed(static_cast<std::uint64_t>(static_cast<std::int64_t>(seed)));
        return {};
    });
    library->Function("value", [generator](std::span<const Value> a) -> Value {
        Count(a, 0, 0);
        return std::uniform_real_distribution<double>(0.0, 1.0)(*generator);
    });
    library->Function("range", [generator](std::span<const Value> a) -> Value {
        Count(a, 2, 2);
        const double low = Number(a[0]), high = Number(a[1]);
        if (low > high)
            throw std::runtime_error("Random range minimum exceeds maximum");
        if (low == high)
            return low;
        return Finite(std::uniform_real_distribution<double>(low, high)(*generator));
    });
    library->Function("int", [generator](std::span<const Value> a) -> Value {
        Count(a, 2, 2);
        const double low = Number(a[0]), high = Number(a[1]);
        if (std::trunc(low) != low || std::trunc(high) != high || std::abs(low) > 9007199254740991.0 ||
            std::abs(high) > 9007199254740991.0)
            throw std::runtime_error("Random int bounds must be exact integers");
        if (low > high)
            throw std::runtime_error("Random int minimum exceeds maximum");
        return static_cast<double>(std::uniform_int_distribution<std::int64_t>(
            static_cast<std::int64_t>(low), static_cast<std::int64_t>(high))(*generator));
    });
    library->Function("chance", [generator](std::span<const Value> a) -> Value {
        Count(a, 1, 1);
        const double probability = Number(a[0]);
        if (probability < 0 || probability > 1)
            throw std::runtime_error("Chance must be between 0 and 1");
        return std::uniform_real_distribution<double>(0.0, 1.0)(*generator) < probability;
    });
    library->Function("choice", [generator](std::span<const Value> a) -> Value {
        Count(a, 1, 1);
        const auto* reference = std::get_if<ObjectRef>(&a[0]);
        const auto collection = reference ? std::dynamic_pointer_cast<Collection>(*reference) : nullptr;
        if (!collection || collection->kind != Collection::Kind::Array)
            throw std::runtime_error("Expected an array to choose from");
        if (collection->GetSize() == 0)
            throw std::runtime_error("Cannot choose from an empty array");
        const auto index = std::uniform_int_distribution<std::size_t>(0, collection->GetSize() - 1)(*generator);
        return collection->GetIndex(static_cast<double>(index));
    });
    return library;
}
std::shared_ptr<Library> Core() {
    auto library = std::make_shared<Library>();
    library->Function("type", [](std::span<const Value> a) -> Value {
        Count(a, 1, 1);
        static constexpr std::array names{"null",    "boolean", "number", "string",
                                          "Vector2", "Vector3", "Color",  "object"};
        const auto* object = std::get_if<ObjectRef>(&a[0]);
        if (object)
            return std::string(*object ? (*object)->GetTypeName() : "null");
        return std::string(names[a[0].index()]);
    });
    library->Function("to_string", [](std::span<const Value> a) -> Value {
        Count(a, 1, 1);
        return ToString(a[0]);
    });
    library->Function("to_number", [](std::span<const Value> a) -> Value {
        Count(a, 1, 1);
        const auto& text = Text(a[0]);
        double value = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (text.empty() || result.ec != std::errc() || result.ptr != text.data() + text.size() ||
            !std::isfinite(value))
            return {};
        return value;
    });
    library->Function("assert", [](std::span<const Value> a) -> Value {
        Count(a, 1, 2);
        const std::string message = a.size() == 2 ? Text(a[1]) : "Assertion failed";
        if (!Boolean(a[0]))
            throw std::runtime_error(message);
        return {};
    });
    return library;
}
} // namespace

std::unordered_map<std::string, ObjectRef> CreateStandardLibraries() {
    return {{"math", Math()}, {"string", Strings()}, {"core", Core()}, {"random", Random()}};
}
} // namespace Vice::Detail
