#include "graphics/render_world.h"

#include "core/log.h"

#include <algorithm>
#include <utility>

namespace Spot {

namespace {

constexpr std::uint64_t SlotBits = 32;

RenderObjectId MakeId(std::uint32_t slot, std::uint32_t generation) {
    // Generations start at 1, so no id is Invalid.
    return static_cast<RenderObjectId>((static_cast<std::uint64_t>(generation) << SlotBits) | slot);
}

} // namespace

bool Detail::MakeMeshObject(const RenderMeshDesc& desc, RenderMeshObject& object) {
    object.desc = desc;
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
    return true;
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
    return true;
}

bool RenderWorld::SetTransform(RenderObjectId id, const Matrix4& transform) {
    const Slot* slot = FindSlot(id);
    if (!slot) {
        return false;
    }
    RenderMeshDesc desc = m_meshes[slot->dense].desc;
    desc.transform = transform;
    Detail::MakeMeshObject(desc, m_meshes[slot->dense]);
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
    m_meshes.pop_back();
    m_meshIds.pop_back();
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
