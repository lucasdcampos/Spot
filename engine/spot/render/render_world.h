#pragma once

#include "spot/core/math/matrix.h"
#include "spot/render/camera_3d.h"
#include "spot/render/mesh.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Spot {

struct Material;

// Names an object of a RenderWorld; stays invalid once the object is removed, even after its slot is reused.
enum class RenderObjectId : std::uint64_t { Invalid = 0 };

// A simpler mesh shown in place of an object's own once it looks small: when its screen size, the height of its
// bounds as a fraction of the view's, is below screenSize.
struct MeshLod {
    const Mesh* mesh = nullptr;
    float screenSize = 0.0f;
};

// How large a sphere looks to the camera: the height it covers as a fraction of the view's, 1 when it fills it.
float GetScreenSize(const Camera3D& camera, const BoundingSphere& bounds);

// Where a mesh whose material projects its textures (Material::worldTextureSize) projects them from.
enum class TextureSpace : std::uint8_t {
    // The world's axes: blocks side by side continue each other's pattern, but the textures stay where they are when
    // the mesh moves, so they slide across it.
    World,
    // The mesh's own, at the world's scale, from its origin: the textures move and turn with it, as they should on
    // what moves, and keep their size however it is scaled.
    Object,
};

struct RenderMeshDesc {
    // Must stay alive while the object shows it; nothing shows without a mesh.
    const Mesh* mesh = nullptr;
    // Read when drawn, so changes to it show at once; plain white when null.
    const Material* material = nullptr;
    Matrix4 transform = {};
    std::uint32_t submesh = Mesh::AllSubmeshes;
    bool castsShadows = true;
    TextureSpace textureSpace = TextureSpace::World;
    // Of the 32 that cameras' culling masks pick from.
    std::uint32_t layer = 0;
    // Levels of detail, from the most detailed down, each with a smaller screenSize; the first without a mesh ends
    // them. They draw whole, whatever the submesh, and are chosen by the bounds of the object's own mesh.
    std::array<MeshLod, 3> lods = {};
    // Below this screen size the object is not drawn at all; 0 draws it however small.
    float cullScreenSize = 0.0f;
};

// One level of detail of an object: the mesh range it draws, while its screen size is in [minScreenSize,
// maxScreenSize).
struct RenderMeshLevel {
    const Mesh* mesh = nullptr;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    float minScreenSize = 0.0f;
    float maxScreenSize = 0.0f;
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
    // Its own mesh first, then its levels of detail; one level, drawn at any size, without them.
    std::array<RenderMeshLevel, 4> levels = {};
    std::uint32_t levelCount = 0;
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
// Not for the scene's own use only: games keeping their objects elsewhere fill one the same way. For scenes of many
// objects, thousands and more, a Renderer3D made with Renderer3DConfig::gpuCulling keeps the world on the GPU in
// batches and culls it there, so drawing it costs the CPU a command per batch instead of per object.
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
