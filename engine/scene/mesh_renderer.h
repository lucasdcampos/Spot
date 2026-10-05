#pragma once

#include "assets/asset_ref.h"
#include "graphics/material.h"
#include "scene/component.h"

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
// and materials made in code have none, so they are saved as null.
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
    // A material of the renderer's own, which is not an asset and so is not saved.
    void SetMaterial(Material material) { m_material = std::make_shared<const Material>(std::move(material)); }

protected:
    void Serialize(Archive& archive) override;

private:
    AssetRef<Mesh> m_mesh;
    AssetRef<Material> m_material;
};

// Creates an entity for the model with a child per part, each showing the part's mesh and material with a
// MeshRenderer, so the parts can be moved, hidden or given other materials one by one.
Entity& InstantiateModel(Scene& scene, const Model& model, std::string name = {});

} // namespace Spot
