#include "animation/skinned_mesh_renderer.h"

#include "assets/asset_archive.h"
#include "core/archive.h"

#include <algorithm>
#include <cmath>

namespace Spot {

void SkinnedMeshRenderer::SetBoundsPadding(float padding) {
    m_boundsPadding = std::isfinite(padding) ? std::max(padding, 0.0f) : 0.0f;
}

void SkinnedMeshRenderer::Serialize(Archive& archive) {
    archive.Field("mesh", m_mesh);
    archive.Field("material", m_material);
    archive.Field("submesh", m_submesh);
    archive.Field("castsShadows", m_castingShadows);
    archive.Field("boundsPadding", m_boundsPadding, {.min = 0, .max = 100});
    archive.Field("animator", m_animator);
    auto transform = Decompose(m_meshModel);
    archive.Field("meshTranslation", transform.translation);
    archive.Field("meshRotation", transform.rotation);
    archive.Field("meshScale", transform.scale);
    if (archive.IsLoading()) {
        SetBoundsPadding(m_boundsPadding);
        m_meshModel = Matrix4::Translation(transform.translation) * Matrix4::Rotation(transform.rotation) *
                      Matrix4::Scale(transform.scale);
    }
}

} // namespace Spot
