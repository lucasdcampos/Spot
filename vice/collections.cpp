#include "collections.h"
#include "runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace Vice::Detail {
namespace {

void Count(std::span<const Value> arguments, std::size_t minimum, std::size_t maximum) {
    if (arguments.size() < minimum || arguments.size() > maximum)
        throw std::runtime_error("Incorrect collection method argument count");
}

std::int64_t Integer(const Value& value) {
    const auto number = Number(value);
    if (std::trunc(number) != number || std::abs(number) > 9007199254740991.0)
        throw std::runtime_error("Expected an exact integer");
    return static_cast<std::int64_t>(number);
}

std::size_t Index(const Value& value, std::size_t size, bool insertion = false) {
    const auto index = Integer(value);
    if (index < 0 || static_cast<std::uint64_t>(index) > size ||
        (!insertion && static_cast<std::uint64_t>(index) == size))
        throw std::runtime_error("Array index out of bounds");
    return static_cast<std::size_t>(index);
}

const std::string& Key(const Value& value) {
    const auto* key = std::get_if<std::string>(&value);
    if (!key)
        throw std::runtime_error("Map keys must be strings");
    return *key;
}

class Array final : public Collection {
public:
    explicit Array(std::span<const Value> initial) : Collection(Kind::Array), values(initial.begin(), initial.end()) {}
    std::string_view GetTypeName() const override { return "array"; }
    std::size_t GetSize() const override { return values.size(); }
    bool HasProperty(std::string_view name) const override { return name == "length"; }
    bool HasMethod(std::string_view name) const override {
        return name == "push" || name == "pop" || name == "insert" || name == "remove_at" || name == "clear";
    }
    Value Get(std::string_view name) const override {
        CheckAlive();
        return name == "length" ? Value(static_cast<double>(values.size())) : Object::Get(name);
    }
    Value GetIndex(const Value& index) const override {
        CheckAlive();
        return values[Index(index, values.size())];
    }
    void SetIndex(const Value& index, const Value& value) override {
        CheckAlive();
        values[Index(index, values.size())] = value;
    }
    Value Call(std::string_view name, std::span<const Value> arguments) override {
        CheckAlive();
        if (name == "push" || name == "insert") {
            const bool insertion = name == "insert";
            Count(arguments, insertion ? 2 : 1, insertion ? 2 : 1);
            if (values.size() >= MaxCollectionSize)
                throw std::runtime_error("Collection size limit exceeded");
            const auto index = insertion ? Index(arguments[0], values.size(), true) : values.size();
            // Copy first: native callers may pass spans into this array.
            auto value = arguments[insertion ? 1 : 0];
            values.insert(values.begin() + static_cast<std::ptrdiff_t>(index), std::move(value));
            ++version;
            return {};
        }
        if (name == "pop" || name == "remove_at") {
            const bool removal = name == "remove_at";
            Count(arguments, removal ? 1 : 0, removal ? 1 : 0);
            if (values.empty())
                throw std::runtime_error("Cannot remove from an empty array");
            const auto index = removal ? Index(arguments[0], values.size()) : values.size() - 1;
            auto value = std::move(values[index]);
            values.erase(values.begin() + static_cast<std::ptrdiff_t>(index));
            ++version;
            return value;
        }
        if (name == "clear") {
            Count(arguments, 0, 0);
            if (!values.empty()) {
                values.clear();
                ++version;
            }
            return {};
        }
        return Object::Call(name, arguments);
    }
    void ClearReferences() override { values.clear(); }
    void VisitReferences(const std::function<void(const Value&)>& visit) const override {
        for (const auto& value : values)
            visit(value);
    }
    std::vector<Value> values;
};

class Map final : public Collection {
public:
    Map() : Collection(Kind::Map) {}
    std::string_view GetTypeName() const override { return "map"; }
    std::size_t GetSize() const override { return values.size(); }
    bool HasProperty(std::string_view name) const override { return name == "length"; }
    bool HasMethod(std::string_view name) const override {
        return name == "get" || name == "has" || name == "remove" || name == "clear" || name == "keys";
    }
    Value Get(std::string_view name) const override {
        CheckAlive();
        return name == "length" ? Value(static_cast<double>(values.size())) : Object::Get(name);
    }
    Value GetIndex(const Value& index) const override {
        CheckAlive();
        const auto found = values.find(Key(index));
        if (found == values.end())
            throw std::runtime_error("Unknown map key '" + Key(index) + "'");
        return found->second;
    }
    void SetIndex(const Value& index, const Value& value) override {
        CheckAlive();
        const auto& key = Key(index);
        if (const auto found = values.find(key); found != values.end()) {
            found->second = value;
            return;
        }
        if (values.size() >= MaxCollectionSize)
            throw std::runtime_error("Collection size limit exceeded");
        values.emplace(key, value);
        ++version;
    }
    std::vector<Value> GetKeys() const {
        std::vector<Value> keys;
        keys.reserve(values.size());
        for (const auto& [key, value] : values) {
            (void)value;
            keys.emplace_back(key);
        }
        std::sort(keys.begin(), keys.end(),
                  [](const Value& a, const Value& b) { return std::get<std::string>(a) < std::get<std::string>(b); });
        return keys;
    }
    Value Call(std::string_view name, std::span<const Value> arguments) override {
        CheckAlive();
        if (name == "get") {
            Count(arguments, 1, 2);
            const auto found = values.find(Key(arguments[0]));
            return found != values.end() ? found->second : arguments.size() == 2 ? arguments[1] : Value{};
        }
        if (name == "has" || name == "remove") {
            Count(arguments, 1, 1);
            const auto& key = Key(arguments[0]);
            if (name == "has")
                return values.contains(key);
            const bool removed = values.erase(key) != 0;
            if (removed)
                ++version;
            return removed;
        }
        if (name == "keys") {
            Count(arguments, 0, 0);
            Value result = MakeArray(GetKeys());
            if (track)
                track(owner, result);
            return result;
        }
        if (name == "clear") {
            Count(arguments, 0, 0);
            if (!values.empty()) {
                values.clear();
                ++version;
            }
            return {};
        }
        return Object::Call(name, arguments);
    }
    void ClearReferences() override { values.clear(); }
    void VisitReferences(const std::function<void(const Value&)>& visit) const override {
        for (const auto& [key, value] : values)
            visit(value);
    }
    std::unordered_map<std::string, Value> values;
};

class Range final : public Collection {
public:
    Range(std::int64_t start, std::int64_t step, std::size_t size)
        : Collection(Kind::Range), start(start), step(step), size(size) {}
    std::string_view GetTypeName() const override { return "range"; }
    std::size_t GetSize() const override { return size; }
    bool HasProperty(std::string_view name) const override { return name == "length"; }
    Value Get(std::string_view name) const override {
        CheckAlive();
        return name == "length" ? Value(static_cast<double>(size)) : Object::Get(name);
    }
    Value GetIndex(const Value& index) const override {
        CheckAlive();
        return static_cast<double>(start + static_cast<std::int64_t>(Index(index, size)) * step);
    }
    void ClearReferences() override {}
    void VisitReferences(const std::function<void(const Value&)>&) const override {}
    std::int64_t start;
    std::int64_t step;
    std::size_t size;
};

class Iterator final : public Object {
public:
    explicit Iterator(std::shared_ptr<Collection> collection)
        : collection(std::move(collection)), version(this->collection->version) {
        if (const auto map = std::dynamic_pointer_cast<Map>(this->collection))
            keys = map->GetKeys();
    }
    std::shared_ptr<Collection> collection;
    std::size_t version;
    std::size_t index = 0;
    std::vector<Value> keys;
};

} // namespace

void Collection::CheckAlive() const {
    if (!alive)
        throw std::runtime_error("Script collection is no longer available");
}

ObjectRef MakeArray(std::span<const Value> values) {
    if (values.size() > MaxCollectionSize)
        throw std::runtime_error("Collection size limit exceeded");
    return MakeManaged<Array>(values);
}

ObjectRef MakeMap(std::span<const Value> pairs) {
    auto map = MakeManaged<Map>();
    for (std::size_t i = 0; i < pairs.size(); i += 2)
        map->SetIndex(pairs[i], pairs[i + 1]);
    return map;
}

ObjectRef MakeRange(std::span<const Value> arguments) {
    if (arguments.empty() || arguments.size() > 3)
        throw std::runtime_error("range expects one, two or three arguments");
    const auto start = arguments.size() == 1 ? 0 : Integer(arguments[0]);
    const auto end = Integer(arguments[arguments.size() == 1 ? 0 : 1]);
    const auto step = arguments.size() == 3 ? Integer(arguments[2]) : 1;
    if (step == 0)
        throw std::runtime_error("range step cannot be zero");
    const auto distance = step > 0 ? end - start : start - end;
    const auto stride = step > 0 ? step : -step;
    const auto count = distance <= 0 ? 0 : 1 + (distance - 1) / stride;
    if (static_cast<std::uint64_t>(count) > MaxCollectionSize)
        throw std::runtime_error("Range size limit exceeded");
    return MakeManaged<Range>(start, step, static_cast<std::size_t>(count));
}

ObjectRef MakeIterator(const Value& iterable) {
    const auto* reference = std::get_if<ObjectRef>(&iterable);
    const auto collection = reference ? std::dynamic_pointer_cast<Collection>(*reference) : nullptr;
    if (!collection)
        throw std::runtime_error("Expected an array, map or range to iterate");
    collection->CheckAlive();
    return std::make_shared<Iterator>(collection);
}

bool NextIterator(const Value& value, bool pair, Value& first, Value& second) {
    auto& iterator = *std::static_pointer_cast<Iterator>(std::get<ObjectRef>(value));
    auto& collection = *iterator.collection;
    collection.CheckAlive();
    if (iterator.version != collection.version)
        throw std::runtime_error("Collection structure changed during iteration");
    if (iterator.index == collection.GetSize())
        return false;
    const auto index = iterator.index++;
    Value key = collection.kind == Collection::Kind::Map ? iterator.keys[index] : Value(static_cast<double>(index));
    if (pair) {
        first = key;
        second = collection.GetIndex(key);
    } else {
        first = collection.kind == Collection::Kind::Map ? key : collection.GetIndex(key);
    }
    return true;
}

} // namespace Vice::Detail
