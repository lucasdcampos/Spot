#include "assets/asset_formats.h"
#include "core/paths.h"
#include "graphics/model_data.h"
#include "import/importers/importer.h"
#include "import/importers/texture_compression.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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
        // Scaling every vertex and every node's offset from its parent scales the whole model, whatever its hierarchy.
        for (auto& node : model->nodes) {
            node.transform.columns[3].x *= scale;
            node.transform.columns[3].y *= scale;
            node.transform.columns[3].z *= scale;
        }
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

    for (auto& skeleton : model->skeletons) {
        for (auto& bone : skeleton.bones) {
            bone.bindPose.translation *= scale;
        }
        const auto scaling = Matrix4::Scale({scale, scale, scale});
        for (auto& matrix : skeleton.inverseBindMatrices) {
            matrix = scaling * matrix * Inverse(scaling);
        }
    }
    for (auto& clip : model->animations) {
        for (auto& channel : clip.channels) {
            if (channel.path == AnimationPath::Translation) {
                for (auto& value : channel.values) {
                    value *= scale;
                }
            }
        }
    }
    for (auto& clip : model->animations) {
        for (const auto& settings : context.GetMeta().model.animations) {
            if (settings.clip == clip.name) {
                clip.events = settings.events;
            }
        }
        std::ranges::stable_sort(clip.events, {}, &AnimationEvent::time);
        if (!clip.IsValid()) {
            return false;
        }
    }
    std::vector<AssetId> skeletons;
    for (std::size_t i = 0; i < model->skeletons.size(); ++i) {
        BinaryWriter data;
        Encode(data, model->skeletons[i]);
        const auto id =
            context.WriteSubAsset(std::format("skeleton:{}", i), AssetType::Skeleton, model->skeletons[i].name, data);
        if (!id.IsValid()) {
            return false;
        }
        skeletons.push_back(id);
    }
    std::vector<AssetId> animations;
    for (std::size_t i = 0; i < model->animations.size(); ++i) {
        BinaryWriter data;
        Encode(data, model->animations[i]);
        const auto id = context.WriteSubAsset(std::format("animation:{}", i), AssetType::AnimationClip,
                                              model->animations[i].name, data);
        if (!id.IsValid()) {
            return false;
        }
        animations.push_back(id);
    }
    std::vector<AssetId> textures;
    for (std::size_t i = 0; i < model->images.size(); ++i) {
        BinaryWriter data;
        const bool color = model->IsColorImage(i);
        if (CanCompressTexture(model->images[i], true)) {
            Encode(data, CompressTexture(model->images[i], color));
        } else {
            Encode(data, TextureFile{std::move(model->images[i]), true, color});
        }
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
                                      .alphaMode = source.alphaMode,
                                      .alphaCutoff = source.alphaCutoff,
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

    std::vector<AssetId> meshes;
    for (std::size_t i = 0; i < model->meshes.size(); ++i) {
        const auto& source = model->meshes[i];
        BinaryWriter data;
        Encode(data, source.mesh);
        const AssetId mesh = context.WriteSubAsset(std::format("mesh:{}", i), AssetType::Mesh,
                                                   source.name.empty() ? std::format("Mesh {}", i) : source.name, data);
        if (!mesh.IsValid()) {
            return false;
        }
        meshes.push_back(mesh);
    }

    // Submeshes without a material share a plain white one.
    std::optional<AssetId> defaultMaterial;
    std::vector<ModelFileNode> nodes;
    for (const auto& source : model->nodes) {
        ModelFileNode node{.name = source.name,
                           .transform = source.transform,
                           .parent = source.parent ? static_cast<std::int32_t>(*source.parent) : -1};
        node.joint = source.joint;
        node.animations = animations;
        if (source.skin) {
            node.skeleton = skeletons[*source.skin];
        }
        if (source.mesh) {
            node.mesh = meshes[*source.mesh];
            for (const auto& material : model->meshes[*source.mesh].materials) {
                if (!material && !defaultMaterial) {
                    const AssetId id = writeMaterial("material:default", "Default", MaterialData{});
                    if (!id.IsValid()) {
                        return false;
                    }
                    defaultMaterial = overridden("material:default", id);
                }
                node.materials.push_back(material ? materials[*material] : *defaultMaterial);
            }
        }
        nodes.push_back(std::move(node));
    }

    BinaryWriter data;
    Encode(data, nodes);
    return context.WriteAsset(AssetType::Model, data);
}

} // namespace Spot::Detail
