#include "scene/text_renderer.h"

#include "assets/asset_archive.h"
#include "core/archive.h"
#include "graphics/camera_3d.h"
#include "graphics/font.h"
#include "scene/entity.h"

#include <algorithm>
#include <cmath>

namespace Spot {

void TextRenderer::SetPixelScale(float scale) {
    m_pixelScale = std::isfinite(scale) ? std::max(0.0f, scale) : 0.0f;
}

Matrix4 TextRenderer::GetDrawMatrix(const Camera3D& camera) const {
    const auto& transform = GetEntity().GetTransform();
    if (!m_faceCamera) {
        return transform.GetWorldMatrix();
    }
    const auto world = Decompose(transform.GetWorldMatrix());
    return Matrix4::Translation(world.translation) * Matrix4::Rotation(camera.rotation) * Matrix4::Scale(world.scale);
}

void TextRenderer::Serialize(Archive& archive) {
    archive.Field("font", m_font);
    archive.Field("text", m_text);
    archive.Field("color", m_color);
    archive.Field("pixelScale", m_pixelScale, {.min = 0.0f, .max = 1000.0f, .speed = 0.001f});
    archive.Field("faceCamera", m_faceCamera);
    archive.Field("order", m_order);
    SetPixelScale(m_pixelScale);
}

} // namespace Spot
