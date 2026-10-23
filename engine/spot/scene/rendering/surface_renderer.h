#pragma once

#include "spot/graphics/custom_surface.h"
#include "spot/scene/component.h"

#include <memory>

namespace Spot {

// Shows a custom surface (see CustomSurface): Renderer3D::DrawScene() hands the surface of every active one to the
// renderer, unless the camera does not see the entity's layer. The surface is made by code, such as terrain kept in
// buffers of a game's own, so scenes save the component without it.
class SurfaceRenderer final : public Component {
public:
    SurfaceRenderer() = default;
    explicit SurfaceRenderer(std::shared_ptr<CustomSurface> surface) : m_surface(std::move(surface)) {}

    const std::shared_ptr<CustomSurface>& GetSurface() const { return m_surface; }
    void SetSurface(std::shared_ptr<CustomSurface> surface) { m_surface = std::move(surface); }

private:
    std::shared_ptr<CustomSurface> m_surface;
};

} // namespace Spot
