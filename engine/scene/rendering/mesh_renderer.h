#pragma once

#include "assets/asset_ref.h"
#include "graphics/material.h"
#include "graphics/mesh.h"
#include "graphics/render_world.h"
#include "scene/component.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Spot {

class Entity;
class Mesh;
class Model;
class Scene;

// A simpler mesh a MeshRenderer shows in place of its own once its screen size, the height of its bounds as a
// fraction of the view's, is below screenSize (see MeshLod).
struct MeshRendererLod {
    AssetRef<Mesh> mesh;
    float screenSize = 0.1f;

    void Serialize(Archive& archive);
    friend bool operator==(const MeshRendererLod&, const MeshRendererLod&) = default;
};

// Shows a mesh with a material at its entity's world transform. Renderer3D::DrawScene() draws every active one:
//
//     auto& crate = scene.CreateEntity("Crate");
//     crate.AddComponent<Spot::MeshRenderer>(Spot::Assets::Load<Spot::Mesh>(meshId),
//                                            Spot::Assets::Load<Spot::Material>("materials/crate.material"));
//
// Meshes and materials are shared, so many entities can show the same ones. Scenes save them by asset id; meshes
// made in code have none, so they are saved as null. A renderer's own material is saved inline, with texture asset ids.
class MeshRenderer final : public Component {
public:
    MeshRenderer() = default;
    explicit MeshRenderer(AssetRef<Mesh> mesh, AssetRef<Material> material = {})
        : m_mesh(std::move(mesh)), m_material(std::move(material)) {}
    MeshRenderer(AssetRef<Mesh> mesh, Material material)
        : m_mesh(std::move(mesh)), m_material(std::make_shared<const Material>(std::move(material))) {}

    // Without a mesh, nothing is drawn.
    const AssetRef<Mesh>& GetMesh() const { return m_mesh; }
    void SetMesh(AssetRef<Mesh> mesh) {
        m_mesh = std::move(mesh);
        ++m_version;
        MarkChanged();
    }

    // Without a material, the mesh is drawn plain white.
    const AssetRef<Material>& GetMaterial() const { return m_material; }
    void SetMaterial(AssetRef<Material> material) {
        m_material = std::move(material);
        ++m_version;
        MarkChanged();
    }
    // A material of the renderer's own, saved inline rather than as a material asset reference.
    void SetMaterial(Material material) {
        m_material = std::make_shared<const Material>(std::move(material));
        ++m_version;
        MarkChanged();
    }

    // The index of the mesh's submesh it draws, or Mesh::AllSubmeshes for all of it, as models' parts each draw one.
    std::uint32_t GetSubmesh() const { return m_submesh; }
    void SetSubmesh(std::uint32_t submesh) {
        m_submesh = submesh;
        ++m_version;
        MarkChanged();
    }

    // Whether the mesh blocks lights that cast shadows, which it does unless told not to. It is lit and shadowed
    // either way.
    bool IsCastingShadows() const { return m_castingShadows; }
    void SetCastingShadows(bool castingShadows) {
        m_castingShadows = castingShadows;
        ++m_version;
        MarkChanged();
    }

    // Levels of detail, from the most detailed down, each with a smaller screen size; up to three are drawn.
    const std::vector<MeshRendererLod>& GetLods() const { return m_lods; }
    void SetLods(std::vector<MeshRendererLod> lods) {
        m_lods = std::move(lods);
        ++m_version;
        MarkChanged();
    }
    // Below this screen size nothing is drawn; 0 draws the mesh however small it looks.
    float GetCullScreenSize() const { return m_cullScreenSize; }
    void SetCullScreenSize(float screenSize) {
        m_cullScreenSize = screenSize;
        ++m_version;
        MarkChanged();
    }

protected:
    void Serialize(Archive& archive) override;

private:
    friend class RenderWorldSystem;

    AssetRef<Mesh> m_mesh;
    AssetRef<Material> m_material;
    bool m_castingShadows = true;
    std::uint32_t m_submesh = Mesh::AllSubmeshes;
    std::vector<MeshRendererLod> m_lods;
    float m_cullScreenSize = 0.0f;
    // Changes with each of the above, so the scene's render world updates the renderer's object only then.
    std::uint32_t m_version = 0;
    // What the render world last took from it. Copies of the renderer start without, so each has an object of its own.
    struct RenderSync {
        RenderObjectId object = RenderObjectId::Invalid;
        std::uint32_t version = 0;
        std::uint64_t transform = 0;
        int layer = 0;
        const Mesh* mesh = nullptr;
        const Material* material = nullptr;

        RenderSync() = default;
        RenderSync(const RenderSync&) {}
        RenderSync& operator=(const RenderSync&) { return *this; }
    };
    RenderSync m_sync;
};

// Same as Scene::Instantiate(model, name), preserving the model's node hierarchy.
Entity& InstantiateModel(Scene& scene, const Model& model, std::string name = {});

} // namespace Spot
