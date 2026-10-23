#pragma once

#include "runtime.h"

#include <functional>
#include <vector>

namespace Vice::Detail {

inline constexpr std::size_t MaxCollectionSize = 1024 * 1024;

class Collection : public ManagedObject {
public:
    enum class Kind { Array, Map, Range };
    explicit Collection(Kind kind) : kind(kind) {}
    void CheckAlive() const;
    virtual std::size_t GetSize() const = 0;
    virtual void ClearReferences() = 0;
    virtual void VisitReferences(const std::function<void(const Value&)>& visit) const = 0;
    const Kind kind;
    void* owner = nullptr;
    void (*track)(void*, const Value&) = nullptr;
    bool alive = true;
    std::size_t version = 0;
};

ObjectRef MakeArray(std::span<const Value> values);
ObjectRef MakeMap(std::span<const Value> pairs);
ObjectRef MakeRange(std::span<const Value> arguments);
ObjectRef MakeIterator(const Value& iterable);
// A single binding receives array/range values or map keys; two bindings receive index/key then value.
bool NextIterator(const Value& iterator, bool pair, Value& first, Value& second);

} // namespace Vice::Detail
