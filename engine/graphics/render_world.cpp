#include "graphics/render_world.h"

#include "core/log.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <utility>

namespace Spot {

namespace {

constexpr std::uint64_t SlotBits = 32;

RenderObjectId MakeId(std::uint32_t slot, std::uint32_t generation) {
    // Generations start at 1, so no id is Invalid.
    return static_cast<RenderObjectId>((static_cast<std::uint64_t>(generation) << SlotBits) | slot);
}

} // namespace

float GetScreenSize(const Camera3D& camera, const BoundingSphere& bounds) {
    if (camera.orthographic) {
        return camera.orthographicSize > 0.0f ? bounds.radius / camera.orthographicSize : 0.0f;
    }
    const float distance = Length(bounds.center - camera.position);
    // Inside its bounds, it fills the view.
    if (distance <= bounds.radius) {
        return 1.0f;
    }
    return bounds.radius / (distance * std::tan(camera.fieldOfView * 0.5f));
}

bool Detail::MakeMeshObject(const RenderMeshDesc& desc, RenderMeshObject& object) {
    object.desc = desc;
    object.levelCount = 0;
    if (!desc.mesh) {
        object.bounds = {};
        object.indexCount = 0;
        return true;
    }
    const Mesh& mesh = *desc.mesh;
    const std::span<const SubMesh> submeshes = mesh.GetSubmeshes();
    if (desc.submesh != Mesh::AllSubmeshes && desc.submesh >= submeshes.size()) {
        Log::GetEngineLogger().Warning("Cannot draw submesh {} of a mesh with {} submeshes", desc.submesh,
                                       submeshes.size());
        return false;
    }
    const Matrix4& transform = desc.transform;
    const BoundingSphere bounds = mesh.GetBounds();
    const Vector4 center = transform * Vector4{bounds.center.x, bounds.center.y, bounds.center.z, 1.0f};
    const auto axis = [&](std::size_t column) {
        const Vector4& c = transform.columns[column];
        return Vector3{c.x, c.y, c.z};
    };
    const float scale = std::max({Length(axis(0)), Length(axis(1)), Length(axis(2))});
    object.bounds = {{center.x, center.y, center.z}, bounds.radius * scale};
    object.mirrored = Dot(Cross(axis(0), axis(1)), axis(2)) < 0.0f;
    if (desc.submesh < submeshes.size()) {
        object.firstIndex = submeshes[desc.submesh].firstIndex;
        object.indexCount = submeshes[desc.submesh].indexCount;
    } else {
        object.firstIndex = 0;
        object.indexCount = mesh.GetIndexCount();
    }
    // Each level shows from the screen size of the next one, or of culling, up to its own.
    object.levels[0] = {.mesh = desc.mesh,
                        .firstIndex = object.firstIndex,
                        .indexCount = object.indexCount,
                        .maxScreenSize = std::numeric_limits<float>::infinity()};
    object.levelCount = 1;
    for (const MeshLod& lod : desc.lods) {
        if (!lod.mesh) {
            break;
        }
        object.levels[object.levelCount - 1].minScreenSize = lod.screenSize;
        object.levels[object.levelCount] = {
            .mesh = lod.mesh, .indexCount = lod.mesh->GetIndexCount(), .maxScreenSize = lod.screenSize};
        ++object.levelCount;
    }
    object.levels[object.levelCount - 1].minScreenSize = desc.cullScreenSize;
    return true;
}

RenderWorld::RenderWorld() {
    static std::atomic<std::uint64_t> nextId = 1;
    m_id = nextId++;
}

RenderObjectId RenderWorld::AddMesh(const RenderMeshDesc& desc) {
    RenderMeshObject object;
    if (!Detail::MakeMeshObject(desc, object)) {
        return RenderObjectId::Invalid;
    }
    std::uint32_t slot = 0;
    if (!m_freeSlots.empty()) {
        slot = m_freeSlots.back();
        m_freeSlots.pop_back();
    } else {
        slot = static_cast<std::uint32_t>(m_slots.size());
        m_slots.emplace_back();
    }
    Slot& entry = m_slots[slot];
    ++entry.generation;
    entry.used = true;
    entry.dense = static_cast<std::uint32_t>(m_meshes.size());
    const RenderObjectId id = MakeId(slot, entry.generation);
    m_meshes.push_back(object);
    m_meshIds.push_back(id);
    m_changeStamps.push_back(++m_changeCount);
    ++m_structureVersion;
    return id;
}

bool RenderWorld::UpdateMesh(RenderObjectId id, const RenderMeshDesc& desc) {
    const Slot* slot = FindSlot(id);
    if (!slot) {
        return false;
    }
    RenderMeshObject object;
    if (!Detail::MakeMeshObject(desc, object)) {
        // It then shows nothing, as drawing a submesh the mesh lacks does.
        object = {.desc = desc};
        object.desc.mesh = nullptr;
    }
    m_meshes[slot->dense] = object;
    m_changeStamps[slot->dense] = ++m_changeCount;
    ++m_structureVersion;
    return true;
}

bool RenderWorld::SetTransform(RenderObjectId id, const Matrix4& transform) {
    const Slot* slot = FindSlot(id);
    if (!slot) {
        return false;
    }
    RenderMeshObject& object = m_meshes[slot->dense];
    const bool mirrored = object.mirrored;
    RenderMeshDesc desc = object.desc;
    desc.transform = transform;
    Detail::MakeMeshObject(desc, object);
    m_changeStamps[slot->dense] = ++m_changeCount;
    if (object.mirrored != mirrored) {
        ++m_structureVersion;
    }
    return true;
}

void RenderWorld::Remove(RenderObjectId id) {
    Slot* slot = FindSlot(id);
    if (!slot) {
        return;
    }
    // The last object takes the removed one's place, so the objects stay side by side.
    const std::uint32_t dense = slot->dense;
    const RenderObjectId moved = m_meshIds.back();
    m_meshes[dense] = m_meshes.back();
    m_meshIds[dense] = moved;
    m_changeStamps[dense] = m_changeStamps.back();
    m_meshes.pop_back();
    m_meshIds.pop_back();
    m_changeStamps.pop_back();
    ++m_structureVersion;
    m_slots[static_cast<std::uint32_t>(static_cast<std::uint64_t>(moved))].dense = dense;
    slot->used = false;
    m_freeSlots.push_back(static_cast<std::uint32_t>(static_cast<std::uint64_t>(id)));
}

bool RenderWorld::Contains(RenderObjectId id) const {
    return FindSlot(id) != nullptr;
}

const RenderMeshObject* RenderWorld::GetMesh(RenderObjectId id) const {
    const Slot* slot = FindSlot(id);
    return slot ? &m_meshes[slot->dense] : nullptr;
}

void RenderWorld::Clear() {
    for (const RenderObjectId id : m_meshIds) {
        const auto slot = static_cast<std::uint32_t>(static_cast<std::uint64_t>(id));
        m_slots[slot].used = false;
        m_freeSlots.push_back(slot);
    }
    m_meshes.clear();
    m_meshIds.clear();
    m_changeStamps.clear();
    ++m_structureVersion;
}

RenderWorld::Slot* RenderWorld::FindSlot(RenderObjectId id) {
    return const_cast<Slot*>(std::as_const(*this).FindSlot(id));
}

const RenderWorld::Slot* RenderWorld::FindSlot(RenderObjectId id) const {
    const auto value = static_cast<std::uint64_t>(id);
    const auto slot = static_cast<std::uint32_t>(value);
    const auto generation = static_cast<std::uint32_t>(value >> SlotBits);
    if (slot >= m_slots.size() || !m_slots[slot].used || m_slots[slot].generation != generation) {
        return nullptr;
    }
    return &m_slots[slot];
}

} // namespace Spot
