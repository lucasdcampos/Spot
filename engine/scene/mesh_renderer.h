#pragma once

#include "graphics/material.h"
#include "scene/component.h"

#include <memory>
#include <utility>

namespace Spot {

class Mesh;

// Shows a mesh with a material at its entity's world transform. Renderer3D::DrawScene() draws every active one:
//
//     auto cube = std::shared_ptr<const Spot::Mesh>(Spot::Mesh::Create(device, Spot::MeshData::CreateCube()));
//     auto& crate = scene.CreateEntity("Crate");
//     crate.AddComponent<Spot::MeshRenderer>(cube, Spot::Material{.baseColor = Spot::Color::Red});
//
// Meshes are shared, so many entities can show the same one. Scenes do not save mesh renderers yet: meshes and
// textures are not assets with a name a file could refer to.
class MeshRenderer final : public Component {
public:
    MeshRenderer() = default;
    explicit MeshRenderer(std::shared_ptr<const Mesh> mesh, Material material = {})
        : m_mesh(std::move(mesh)), m_material(std::move(material)) {}

    // Without a mesh, nothing is drawn.
    const std::shared_ptr<const Mesh>& GetMesh() const { return m_mesh; }
    void SetMesh(std::shared_ptr<const Mesh> mesh) { m_mesh = std::move(mesh); }

    Material& GetMaterial() { return m_material; }
    const Material& GetMaterial() const { return m_material; }
    void SetMaterial(Material material) { m_material = std::move(material); }

private:
    std::shared_ptr<const Mesh> m_mesh;
    Material m_material;
};

} // namespace Spot
