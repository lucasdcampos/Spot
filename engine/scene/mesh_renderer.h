#pragma once

#include "assets/asset_ref.h"
#include "graphics/material.h"
#include "graphics/mesh.h"
#include "scene/component.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace Spot {

class Entity;
class Mesh;
class Model;
class Scene;

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
    void SetMesh(AssetRef<Mesh> mesh) { m_mesh = std::move(mesh); }

    // Without a material, the mesh is drawn plain white.
    const AssetRef<Material>& GetMaterial() const { return m_material; }
    void SetMaterial(AssetRef<Material> material) { m_material = std::move(material); }
    // A material of the renderer's own, saved inline rather than as a material asset reference.
    void SetMaterial(Material material) { m_material = std::make_shared<const Material>(std::move(material)); }

    // The index of the mesh's submesh it draws, or Mesh::AllSubmeshes for all of it, as models' parts each draw one.
    std::uint32_t GetSubmesh() const { return m_submesh; }
    void SetSubmesh(std::uint32_t submesh) { m_submesh = submesh; }

    // Whether the mesh blocks lights that cast shadows, which it does unless told not to. It is lit and shadowed
    // either way.
    bool IsCastingShadows() const { return m_castingShadows; }
    void SetCastingShadows(bool castingShadows) { m_castingShadows = castingShadows; }

protected:
    void Serialize(Archive& archive) override;

private:
    AssetRef<Mesh> m_mesh;
    AssetRef<Material> m_material;
    bool m_castingShadows = true;
    std::uint32_t m_submesh = Mesh::AllSubmeshes;
};

// Same as Scene::Instantiate(model, name), preserving the model's node hierarchy.
Entity& InstantiateModel(Scene& scene, const Model& model, std::string name = {});

} // namespace Spot
