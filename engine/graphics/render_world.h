#pragma once

#include "graphics/mesh.h"
#include "math/matrix.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Spot {

struct Material;

// Names an object of a RenderWorld; stays invalid once the object is removed, even after its slot is reused.
enum class RenderObjectId : std::uint64_t { Invalid = 0 };

struct RenderMeshDesc {
    // Must stay alive while the object shows it; nothing shows without a mesh.
    const Mesh* mesh = nullptr;
    // Read when drawn, so changes to it show at once; plain white when null.
    const Material* material = nullptr;
    Matrix4 transform = {};
    std::uint32_t submesh = Mesh::AllSubmeshes;
    bool castsShadows = true;
    // Of the 32 that cameras' culling masks pick from.
    std::uint32_t layer = 0;
};

// A mesh as a world shows it, with what drawing it needs worked out once, when it changes.
struct RenderMeshObject {
    RenderMeshDesc desc;
    // Around the mesh, in world space.
    BoundingSphere bounds;
    // Whether the transform turns the mesh inside out.
    bool mirrored = false;
    // The range of the mesh's indices it draws.
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
};

// What a renderer draws that stays from frame to frame: objects are added once and changed only when they change,
// rather than handed to the renderer again each frame, which is what makes large static worlds cheap. Objects are kept
// side by side, in no particular order. Renderer3D::DrawWorld() draws one:
//
//     Spot::RenderWorld world;
//     const auto crate = world.AddMesh({.mesh = mesh.get(), .material = &material, .transform = at});
//     ...
//     world.SetTransform(crate, movedTo);
//     renderer.DrawWorld(world);
//
// Not for the scene's own use only: games keeping their objects elsewhere fill one the same way.
class RenderWorld {
public:
    RenderWorld();
    // Renderers keep what they made of a world under its id, so copies would be taken for it.
    RenderWorld(const RenderWorld&) = delete;
    RenderWorld& operator=(const RenderWorld&) = delete;

    // Invalid, after logging why, when the submesh is not one of the mesh's.
    RenderObjectId AddMesh(const RenderMeshDesc& desc);
    // False for objects no longer in the world.
    bool UpdateMesh(RenderObjectId id, const RenderMeshDesc& desc);
    bool SetTransform(RenderObjectId id, const Matrix4& transform);
    void Remove(RenderObjectId id);
    bool Contains(RenderObjectId id) const;
    // Null for objects no longer in the world.
    const RenderMeshObject* GetMesh(RenderObjectId id) const;
    void Clear();

    std::span<const RenderMeshObject> GetMeshes() const { return m_meshes; }
    // The id of each of GetMeshes(), in the same order.
    std::span<const RenderObjectId> GetMeshIds() const { return m_meshIds; }

    // What renderers keeping the world on the GPU follow it by: an id no other world has, a version that changes
    // when objects are added, removed or changed beyond their transform, or when a transform starts or stops
    // mirroring, and for each of GetMeshes() the count of changes when it last changed, which GetChangeCount() is now.
    std::uint64_t GetId() const { return m_id; }
    std::uint64_t GetStructureVersion() const { return m_structureVersion; }
    std::uint64_t GetChangeCount() const { return m_changeCount; }
    std::span<const std::uint64_t> GetChangeStamps() const { return m_changeStamps; }

private:
    struct Slot {
        std::uint32_t dense = 0;
        std::uint32_t generation = 0;
        bool used = false;
    };

    Slot* FindSlot(RenderObjectId id);
    const Slot* FindSlot(RenderObjectId id) const;

    std::vector<RenderMeshObject> m_meshes;
    std::vector<RenderObjectId> m_meshIds;
    std::vector<std::uint64_t> m_changeStamps;
    std::uint64_t m_id;
    std::uint64_t m_structureVersion = 0;
    std::uint64_t m_changeCount = 0;
    std::vector<Slot> m_slots;
    std::vector<std::uint32_t> m_freeSlots;
};

namespace Detail {

// Works out what drawing a mesh needs; false, after logging why, when the submesh is not one of the mesh's.
bool MakeMeshObject(const RenderMeshDesc& desc, RenderMeshObject& object);

} // namespace Detail

} // namespace Spot
