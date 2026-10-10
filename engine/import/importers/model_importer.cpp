#include "assets/asset_formats.h"
#include "core/paths.h"
#include "graphics/model_data.h"
#include "import/importers/importer.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <format>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace Spot::Detail {

namespace {

// A .gltf can keep its buffers and images in separate files; changes to those must import the model again. A .glb
// holds everything itself, and is not JSON, so nothing is found in it.
void AddGltfDependencies(ImportContext& context) {
    const auto bytes = ReadFileBytes(context.GetSourcePath());
    if (!bytes) {
        return;
    }
    const auto* begin = reinterpret_cast<const char*>(bytes->data());
    const nlohmann::json document = nlohmann::json::parse(begin, begin + bytes->size(), nullptr, false);
    if (!document.is_object()) {
        return;
    }

    const auto directory = context.GetSourcePath().parent_path();
    for (const char* key : {"buffers", "images"}) {
        const auto list = document.find(key);
        if (list == document.end() || !list->is_array()) {
            continue;
        }
        for (const auto& entry : *list) {
            const auto uri = entry.find("uri");
            if (uri == entry.end() || !uri->is_string()) {
                continue;
            }
            const std::string_view text = uri->get_ref<const std::string&>();
            if (text.starts_with("data:")) {
                continue;
            }
            // URIs escape characters such as spaces as "%20".
            std::string decoded;
            for (std::size_t i = 0; i < text.size(); ++i) {
                unsigned value = 0;
                if (text[i] == '%' && i + 2 < text.size() &&
                    std::from_chars(text.data() + i + 1, text.data() + i + 3, value, 16).ptr == text.data() + i + 3) {
                    decoded.push_back(static_cast<char>(value));
                    i += 2;
                } else {
                    decoded.push_back(text[i]);
                }
            }
            context.AddDependency((directory / Utf8ToPath(decoded)).lexically_normal());
        }
    }
}

} // namespace

bool ImportModel(ImportContext& context) {
    AddGltfDependencies(context);

    auto model = ModelData::Load(context.GetSourcePath());
    if (!model) {
        return false;
    }

    const float scale = context.GetMeta().model.scale;
    if (scale != 1.0f) {
        for (auto& part : model->meshes) {
            for (auto& vertex : part.mesh.vertices) {
                vertex.position *= scale;
            }
            // A negative scale mirrors the model, which would turn its triangles inside out.
            if (scale < 0.0f) {
                for (std::size_t i = 0; i + 2 < part.mesh.indices.size(); i += 3) {
                    std::swap(part.mesh.indices[i + 1], part.mesh.indices[i + 2]);
                }
                // Flipping the normals also flips which side of the tangent the bitangent is on.
                for (auto& vertex : part.mesh.vertices) {
                    vertex.normal = -vertex.normal;
                    vertex.tangent = {-vertex.tangent.x, -vertex.tangent.y, -vertex.tangent.z, -vertex.tangent.w};
                }
            }
        }
    }

    std::vector<AssetId> textures;
    for (std::size_t i = 0; i < model->images.size(); ++i) {
        BinaryWriter data;
        const bool color = model->IsColorImage(i);
        Encode(data, TextureFile{std::move(model->images[i]), true, color});
        textures.push_back(context.WriteSubAsset(std::format("texture:{}", i), AssetType::Texture,
                                                 std::format("Texture {}", i), data));
        if (!textures.back().IsValid()) {
            return false;
        }
    }

    const auto writeMaterial = [&](std::string_view key, std::string name, const MaterialData& source) {
        const auto texture = [&](std::optional<std::size_t> image) { return image ? textures[*image] : AssetId(); };
        const MaterialSource material{.baseColor = source.baseColor,
                                      .baseColorTexture = texture(source.baseColorImage),
                                      .metallic = source.metallic,
                                      .roughness = source.roughness,
                                      .metallicRoughnessTexture = texture(source.metallicRoughnessImage),
                                      .normalTexture = texture(source.normalImage),
                                      .normalScale = source.normalScale,
                                      .occlusionTexture = texture(source.occlusionImage),
                                      .occlusionStrength = source.occlusionStrength,
                                      .emissive = source.emissive,
                                      .emissiveTexture = texture(source.emissiveImage),
                                      .doubleSided = source.doubleSided};
        BinaryWriter data;
        Encode(data, material);
        return context.WriteSubAsset(key, AssetType::Material, std::move(name), data);
    };

    // Materials extracted to .material files replace the model's own, which are still imported so they can be
    // extracted again.
    const auto overridden = [&](std::string_view key, AssetId id) {
        for (const auto& entry : context.GetMeta().model.materials) {
            if (entry.key == key && entry.material.IsValid()) {
                return entry.material;
            }
        }
        return id;
    };

    std::vector<AssetId> materials;
    for (std::size_t i = 0; i < model->materials.size(); ++i) {
        const auto& source = model->materials[i];
        const std::string key = std::format("material:{}", i);
        const AssetId id =
            writeMaterial(key, source.name.empty() ? std::format("Material {}", i) : source.name, source);
        if (!id.IsValid()) {
            return false;
        }
        materials.push_back(overridden(key, id));
    }

    std::optional<AssetId> defaultMaterial;
    std::vector<ModelFilePart> parts;
    for (std::size_t i = 0; i < model->meshes.size(); ++i) {
        const auto& source = model->meshes[i];
        if (source.mesh.indices.empty()) {
            continue;
        }

        const std::string name = source.name.empty() ? std::format("Mesh {}", i) : source.name;
        BinaryWriter data;
        Encode(data, source.mesh);
        const AssetId mesh = context.WriteSubAsset(std::format("mesh:{}", i), AssetType::Mesh, name, data);
        if (!mesh.IsValid()) {
            return false;
        }

        // Parts without a material share a plain white one.
        if (!source.material && !defaultMaterial) {
            const AssetId id = writeMaterial("material:default", "Default", MaterialData{});
            if (!id.IsValid()) {
                return false;
            }
            defaultMaterial = overridden("material:default", id);
        }
        parts.push_back({name, mesh, source.material ? materials[*source.material] : *defaultMaterial});
    }

    BinaryWriter data;
    Encode(data, parts);
    return context.WriteAsset(AssetType::Model, data);
}

} // namespace Spot::Detail
