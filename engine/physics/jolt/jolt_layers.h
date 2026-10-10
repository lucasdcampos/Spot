#pragma once

// Backend-internal: the filters that decide which bodies collide, and which bodies queries see.

#include "physics/jolt/jolt_common.h"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

#include <algorithm>

namespace Spot::Detail {

namespace BroadPhaseLayers {

inline constexpr JPH::BroadPhaseLayer Static{0};
inline constexpr JPH::BroadPhaseLayer Moving{1};
inline constexpr JPH::uint Count = 2;

} // namespace BroadPhaseLayers

// Static bodies never collide with each other, so they are kept apart from moving ones, which collide with everything.
class BroadPhaseLayerMap final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::Count; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return IsMoving(layer) ? BroadPhaseLayers::Moving : BroadPhaseLayers::Static;
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == BroadPhaseLayers::Static ? "Static" : "Moving";
    }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override {
        return IsMoving(layer) || broadPhaseLayer == BroadPhaseLayers::Moving;
    }
};

// Static bodies never collide with each other; other pairs collide when their entities' layers do. The masks change
// only between steps, while no thread reads them.
class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    explicit ObjectPairFilter(const LayerMasks& masks) : m_masks(masks) {}

    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        if (!IsMoving(a) && !IsMoving(b)) {
            return false;
        }
        return ((m_masks[static_cast<std::size_t>(GetEntityLayer(a))] >> GetEntityLayer(b)) & 1u) != 0;
    }

private:
    const LayerMasks& m_masks;
};

// The layers a query sees.
class QueryLayerFilter final : public JPH::ObjectLayerFilter {
public:
    explicit QueryLayerFilter(std::uint32_t mask) : m_mask(mask) {}

    bool ShouldCollide(JPH::ObjectLayer layer) const override { return ((m_mask >> GetEntityLayer(layer)) & 1u) != 0; }

private:
    std::uint32_t m_mask;
};

// The bodies a query sees: sensors when asked, and never the ignored ones.
class QueryBodyFilter final : public JPH::BodyFilter {
public:
    explicit QueryBodyFilter(const PhysicsQueryFilter& filter) : m_filter(filter) {}

    bool ShouldCollide(const JPH::BodyID& body) const override {
        const PhysicsBodyId id = FromJolt(body);
        return std::ranges::find(m_filter.ignored, id) == m_filter.ignored.end();
    }

    bool ShouldCollideLocked(const JPH::Body& body) const override { return m_filter.sensors || !body.IsSensor(); }

private:
    const PhysicsQueryFilter& m_filter;
};

} // namespace Spot::Detail
