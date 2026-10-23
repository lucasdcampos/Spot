#include "graphics/material.h"

#include "assets/asset_archive.h"
#include "core/archive.h"

#include <array>
#include <string_view>

namespace Spot {

void Material::Serialize(Archive& archive) {
    static constexpr std::array<std::string_view, 3> Modes = {"Opaque", "Cutout", "Transparent"};
    archive.Field("baseColor", baseColor);
    archive.Field("baseColorTexture", baseColorTexture);
    archive.Field("metallic", metallic, {.min = 0.0f, .max = 1.0f});
    archive.Field("roughness", roughness, {.min = 0.0f, .max = 1.0f});
    archive.Field("metallicRoughnessTexture", metallicRoughnessTexture);
    archive.Field("normalTexture", normalTexture);
    archive.Field("normalScale", normalScale, {.min = 0.0f, .max = 10.0f});
    archive.Field("occlusionTexture", occlusionTexture);
    archive.Field("occlusionStrength", occlusionStrength, {.min = 0.0f, .max = 1.0f});
    archive.Field("emissive", emissive, {.hdr = true});
    archive.Field("emissiveTexture", emissiveTexture);
    archive.Field("alphaMode", alphaMode, {.enumNames = Modes});
    archive.Field("alphaCutoff", alphaCutoff, {.min = 0.0f, .max = 1.0f});
    archive.Field("doubleSided", doubleSided);
    archive.Field("worldTextureSize", worldTextureSize, {.min = 0.0f, .max = 1000.0f});
    archive.Field("nearestFiltering", nearestFiltering);
}

} // namespace Spot
