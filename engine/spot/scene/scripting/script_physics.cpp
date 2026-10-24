#include "spot/scene/physics/collider.h"
#include "spot/scene/physics/physics_world.h"
#include "spot/scene/physics2d/physics_world_2d.h"
#include "spot/scene/scene.h"
#include "spot/scene/scripting/script_module_bindings.h"

namespace Spot::Detail {
namespace {
QueryFilter Filter(std::span<const ScriptValue> arguments, std::size_t first) {
    QueryFilter filter;
    if (arguments.size() > first)
        filter.ignore = ReadScriptEntityId(arguments[first]);
    if (arguments.size() > first + 1)
        filter.layerMask = Argument<std::uint32_t>(arguments[first + 1]);
    if (arguments.size() > first + 2)
        filter.includeTriggers = Argument<bool>(arguments[first + 2]);
    return filter;
}
template <typename Hit>
ScriptValue MakeHit(const std::shared_ptr<ScriptSceneContext>& context, const std::optional<Hit>& hit) {
    if (!hit)
        return {};
    auto record = std::make_shared<ScriptRecord>();
    record->properties.emplace("entity", MakeScriptReference(context, hit->entity));
    record->properties.emplace(
        "collider", hit->collider ? MakeScriptReference(context, hit->entity, hit->collider->GetId()) : ScriptValue{});
    record->properties.emplace("point", hit->point);
    record->properties.emplace("normal", hit->normal);
    record->properties.emplace("distance", static_cast<double>(hit->distance));
    return ScriptObjectRef(record);
}

} // namespace

ScriptValue CallScriptPhysics(Scene& scene, const std::shared_ptr<ScriptSceneContext>& context, std::string_view name,
                              std::span<const ScriptValue> arguments) {
    // Raycast(origin, direction, maxDistance[, ignore, layerMask, includeTriggers]); casts take a radius after
    // the origin. Results are hit objects, or null when nothing is met.
    const bool cast = name == "SphereCast" || name == "CircleCast2D";
    const std::size_t required = cast ? 4 : 3;
    if (arguments.size() < required || arguments.size() > required + 3)
        throw std::runtime_error("Incorrect argument count");
    const auto filter = Filter(arguments, required);
    const auto distance = Argument<float>(arguments[required - 1]);
    if (!(distance >= 0))
        throw std::runtime_error("Query distance cannot be negative");
    const auto radius = cast ? Argument<float>(arguments[1]) : 0.0f;
    if (cast && !(radius > 0))
        throw std::runtime_error("Cast radius must be positive");
    if (name == "Raycast" || name == "SphereCast") {
        auto& physics = scene.GetOrAddSystem<Spot::PhysicsWorld>();
        const auto origin = Argument<Vector3>(arguments[0]);
        const auto direction = Argument<Vector3>(arguments[cast ? 2 : 1]);
        return MakeHit(context, cast ? physics.SphereCast(origin, radius, direction, distance, filter)
                                     : physics.Raycast(origin, direction, distance, filter));
    }
    auto& physics = scene.GetOrAddSystem<Spot::PhysicsWorld2D>();
    const auto origin = Argument<Vector2>(arguments[0]);
    const auto direction = Argument<Vector2>(arguments[cast ? 2 : 1]);
    return MakeHit(context, cast ? physics.CircleCast(origin, radius, direction, distance, filter)
                                 : physics.Raycast(origin, direction, distance, filter));
}

} // namespace Spot::Detail
