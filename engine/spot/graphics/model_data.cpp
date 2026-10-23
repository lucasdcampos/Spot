#include "spot/graphics/model_data.h"

#include "spot/core/log.h"
#include "spot/core/matrix.h"
#include "spot/core/paths.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string_view>
#include <utility>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

namespace Spot {

namespace {

// Reads files through std::ifstream rather than cgltf's own file access, which mishandles non-ASCII paths on Windows.
cgltf_result ReadFile(const cgltf_memory_options*, const cgltf_file_options*, const char* path, cgltf_size* size,
                      void** data) {
    std::ifstream file(Utf8ToPath(path), std::ios::binary);
    if (!file) {
        return cgltf_result_file_not_found;
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

    void* copy = std::malloc(bytes.empty() ? 1 : bytes.size());
    if (!copy) {
        return cgltf_result_out_of_memory;
    }
    std::memcpy(copy, bytes.data(), bytes.size());
    *size = bytes.size();
    *data = copy;
    return cgltf_result_success;
}

void ReleaseFile(const cgltf_memory_options*, const cgltf_file_options*, void* data) {
    std::free(data);
}

const char* GetErrorText(cgltf_result result) {
    switch (result) {
    case cgltf_result_data_too_short:
    case cgltf_result_unknown_format:
    case cgltf_result_invalid_json:
    case cgltf_result_invalid_gltf:
        return "not a valid glTF 2.0 file";
    case cgltf_result_invalid_options:
        return "invalid options";
    case cgltf_result_file_not_found:
        return "a file it refers to was not found";
    case cgltf_result_io_error:
        return "a file could not be read";
    case cgltf_result_out_of_memory:
        return "out of memory";
    case cgltf_result_legacy_gltf:
        return "glTF 1.0 is not supported";
    default:
        return "unknown error";
    }
}

struct DataDeleter {
    void operator()(cgltf_data* data) const { cgltf_free(data); }
};

class Importer {
public:
    Importer(const cgltf_data& data, const cgltf_options& options, const std::filesystem::path* directory)
        : m_data(data), m_options(options), m_directory(directory) {}

    ModelData Import() {
        ModelData model;
        ImportImages(model);
        ImportMaterials(model);

        m_nodeIndices.assign(m_data.nodes_count, std::nullopt);
        m_meshIndices.assign(m_data.meshes_count, std::nullopt);
        m_meshesImported.assign(m_data.meshes_count, false);
        if (const cgltf_scene* scene = m_data.scene ? m_data.scene : (m_data.scenes_count ? m_data.scenes : nullptr)) {
            for (cgltf_size i = 0; i < scene->nodes_count; ++i) {
                ImportNode(*scene->nodes[i], std::nullopt, model);
            }
        } else {
            for (cgltf_size i = 0; i < m_data.nodes_count; ++i) {
                if (!m_data.nodes[i].parent) {
                    ImportNode(m_data.nodes[i], std::nullopt, model);
                }
            }
        }
        // Skins may reference joints outside the default scene.
        if (m_data.skins_count || m_data.animations_count) {
            for (cgltf_size i = 0; i < m_data.nodes_count; ++i) {
                ImportNodeWithParents(m_data.nodes[i], model);
            }
        }
        ImportAnimations(model);
        return model;
    }

private:
    void ImportImages(ModelData& model) {
        m_imageIndices.resize(m_data.images_count);
        for (cgltf_size i = 0; i < m_data.images_count; ++i) {
            if (auto image = LoadImage(m_data.images[i])) {
                m_imageIndices[i] = model.images.size();
                model.images.push_back(std::move(*image));
            } else {
                Log::GetEngineLogger().Warning("Model image {} could not be loaded; materials using it are untextured",
                                               i);
            }
        }
    }

    std::optional<Image> LoadImage(const cgltf_image& image) const {
        if (image.buffer_view) {
            const auto* bytes = reinterpret_cast<const std::byte*>(cgltf_buffer_view_data(image.buffer_view));
            return bytes ? Image::LoadFromMemory({bytes, image.buffer_view->size}) : std::nullopt;
        }
        if (!image.uri) {
            return std::nullopt;
        }

        const std::string_view uri = image.uri;
        if (uri.starts_with("data:")) {
            const auto comma = uri.find(',');
            if (comma == std::string_view::npos || !uri.substr(0, comma).ends_with(";base64")) {
                return std::nullopt;
            }
            const std::string_view base64 = uri.substr(comma + 1);
            const std::size_t padding = base64.ends_with("==") ? 2 : base64.ends_with('=') ? 1 : 0;
            const std::size_t size = base64.size() / 4 * 3 - padding;

            void* decoded = nullptr;
            if (cgltf_load_buffer_base64(&m_options, size, base64.data(), &decoded) != cgltf_result_success) {
                return std::nullopt;
            }
            const std::unique_ptr<void, decltype(&std::free)> owner(decoded, &std::free);
            return Image::LoadFromMemory({static_cast<const std::byte*>(decoded), size});
        }

        if (!m_directory) {
            Log::GetEngineLogger().Error("Model image '{}' is a separate file, which models read from memory cannot "
                                         "refer to",
                                         uri);
            return std::nullopt;
        }
        // URIs escape characters such as spaces, as in "my%20texture.png".
        std::string path(uri);
        path.resize(cgltf_decode_uri(path.data()));
        return Image::Load(*m_directory / Utf8ToPath(path));
    }

    std::optional<std::size_t> GetImage(const cgltf_texture_view& view) const {
        if (!view.texture || !view.texture->image) {
            return std::nullopt;
        }
        return m_imageIndices[cgltf_image_index(&m_data, view.texture->image)];
    }

    void ImportMaterials(ModelData& model) const {
        for (cgltf_size i = 0; i < m_data.materials_count; ++i) {
            const cgltf_material& source = m_data.materials[i];
            MaterialData material;
            material.name = source.name ? source.name : "";
            material.doubleSided = source.double_sided;
            if (source.has_pbr_metallic_roughness) {
                const auto& pbr = source.pbr_metallic_roughness;
                const cgltf_float* factor = pbr.base_color_factor;
                material.baseColor = {factor[0], factor[1], factor[2], factor[3]};
                material.baseColorImage = GetImage(pbr.base_color_texture);
                material.metallic = pbr.metallic_factor;
                material.roughness = pbr.roughness_factor;
                material.metallicRoughnessImage = GetImage(pbr.metallic_roughness_texture);
            }
            material.normalImage = GetImage(source.normal_texture);
            if (material.normalImage) {
                material.normalScale = source.normal_texture.scale;
            }
            material.occlusionImage = GetImage(source.occlusion_texture);
            if (material.occlusionImage) {
                material.occlusionStrength = source.occlusion_texture.scale;
            }
            const float strength = source.has_emissive_strength ? source.emissive_strength.emissive_strength : 1.0f;
            const cgltf_float* emissive = source.emissive_factor;
            material.emissive = {emissive[0] * strength, emissive[1] * strength, emissive[2] * strength, 1.0f};
            material.emissiveImage = GetImage(source.emissive_texture);
            material.alphaMode = source.alpha_mode == cgltf_alpha_mode_mask    ? AlphaMode::Cutout
                                 : source.alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Transparent
                                                                               : AlphaMode::Opaque;
            material.alphaCutoff = source.alpha_cutoff;
            model.materials.push_back(std::move(material));
        }
    }

    void ImportNode(const cgltf_node& node, std::optional<std::size_t> parent, ModelData& model) {
        const auto sourceIndex = cgltf_node_index(&m_data, &node);
        if (m_nodeIndices[sourceIndex]) {
            return;
        }
        ModelNodeData imported;
        imported.name = node.name ? node.name : "";
        cgltf_node_transform_local(&node, &imported.transform.columns[0].x);
        imported.parent = parent;
        if (node.skin) {
            imported.skin = cgltf_skin_index(&m_data, node.skin);
        }
        for (cgltf_size i = 0; i < m_data.skins_count; ++i) {
            for (cgltf_size j = 0; j < m_data.skins[i].joints_count; ++j) {
                imported.joint = imported.joint || m_data.skins[i].joints[j] == &node;
            }
        }
        if (node.mesh) {
            imported.mesh = ImportMesh(static_cast<std::size_t>(cgltf_mesh_index(&m_data, node.mesh)), model);
        }
        const std::size_t index = model.nodes.size();
        m_nodeIndices[sourceIndex] = index;
        model.nodes.push_back(std::move(imported));
        for (cgltf_size i = 0; i < node.children_count; ++i) {
            ImportNode(*node.children[i], index, model);
        }
    }

    void ImportNodeWithParents(const cgltf_node& node, ModelData& model) {
        if (m_nodeIndices[cgltf_node_index(&m_data, &node)]) {
            return;
        }
        if (node.parent) {
            ImportNodeWithParents(*node.parent, model);
        }
        ImportNode(node, node.parent ? m_nodeIndices[cgltf_node_index(&m_data, node.parent)] : std::nullopt, model);
    }

    void ImportAnimations(ModelData& model) const {
        for (cgltf_size i = 0; i < m_data.skins_count; ++i) {
            const auto& source = m_data.skins[i];
            Skeleton skeleton;
            skeleton.name = source.name ? source.name : "Skeleton " + std::to_string(i);
            for (const auto& node : model.nodes) {
                skeleton.bones.push_back(
                    {node.name, node.parent ? static_cast<std::int32_t>(*node.parent) : -1, Decompose(node.transform)});
            }
            for (cgltf_size j = 0; j < source.joints_count; ++j) {
                skeleton.joints.push_back(
                    static_cast<std::uint32_t>(*m_nodeIndices[cgltf_node_index(&m_data, source.joints[j])]));
                Matrix4 inverseBind;
                if (source.inverse_bind_matrices) {
                    cgltf_accessor_read_float(source.inverse_bind_matrices, j, &inverseBind.columns[0].x, 16);
                }
                skeleton.inverseBindMatrices.push_back(inverseBind);
            }
            model.skeletons.push_back(std::move(skeleton));
        }
        for (cgltf_size i = 0; i < m_data.animations_count; ++i) {
            const auto& source = m_data.animations[i];
            AnimationClip clip;
            clip.name = source.name ? source.name : "Animation " + std::to_string(i);
            for (cgltf_size j = 0; j < source.channels_count; ++j) {
                const auto& sourceChannel = source.channels[j];
                if (!sourceChannel.target_node || sourceChannel.target_path == cgltf_animation_path_type_weights) {
                    Log::GetEngineLogger().Warning("Skipped unsupported animation channel in '{}'", clip.name);
                    continue;
                }
                const auto& sampler = *sourceChannel.sampler;
                AnimationChannel channel;
                channel.bone =
                    static_cast<std::uint32_t>(*m_nodeIndices[cgltf_node_index(&m_data, sourceChannel.target_node)]);
                channel.path = sourceChannel.target_path == cgltf_animation_path_type_rotation ? AnimationPath::Rotation
                               : sourceChannel.target_path == cgltf_animation_path_type_scale
                                   ? AnimationPath::Scale
                                   : AnimationPath::Translation;
                channel.interpolation =
                    sampler.interpolation == cgltf_interpolation_type_step           ? AnimationInterpolation::Step
                    : sampler.interpolation == cgltf_interpolation_type_cubic_spline ? AnimationInterpolation::Cubic
                                                                                     : AnimationInterpolation::Linear;
                for (cgltf_size k = 0; k < sampler.input->count; ++k) {
                    float time = 0;
                    cgltf_accessor_read_float(sampler.input, k, &time, 1);
                    channel.times.push_back(time);
                    clip.duration = std::max(clip.duration, time);
                }
                for (cgltf_size k = 0; k < sampler.output->count; ++k) {
                    Vector4 value{};
                    cgltf_accessor_read_float(sampler.output, k, &value.x,
                                              channel.path == AnimationPath::Rotation ? 4 : 3);
                    channel.values.push_back(value);
                }
                clip.channels.push_back(std::move(channel));
            }
            if (source.extras.data) {
                const auto extras = nlohmann::json::parse(source.extras.data, nullptr, false);
                if (extras.is_object() && extras.contains("events") && extras["events"].is_array()) {
                    try {
                        for (const auto& event : extras["events"]) {
                            clip.events.push_back({event.at("time").get<float>(), event.at("name").get<std::string>(),
                                                   event.value("payload", std::string{})});
                        }
                        std::ranges::stable_sort(clip.events, {}, &AnimationEvent::time);
                    } catch (const nlohmann::json::exception&) {
                        clip.duration = -1;
                    }
                }
            }
            model.animations.push_back(std::move(clip));
        }
    }

    // Each of the file's meshes is read once, however many nodes show it.
    std::optional<std::size_t> ImportMesh(std::size_t index, ModelData& model) {
        if (m_meshesImported[index]) {
            return m_meshIndices[index];
        }
        m_meshesImported[index] = true;
        const cgltf_mesh& source = m_data.meshes[index];
        ModelMeshData mesh;
        mesh.name = source.name ? source.name : "";
        for (cgltf_size i = 0; i < source.primitives_count; ++i) {
            const cgltf_primitive& primitive = source.primitives[i];
            if (auto part = ImportPrimitive(mesh.name, primitive)) {
                mesh.mesh.AppendSubmesh(*part);
                mesh.materials.push_back(
                    primitive.material ? std::optional<std::size_t>(cgltf_material_index(&m_data, primitive.material))
                                       : std::nullopt);
            }
        }
        if (mesh.mesh.indices.empty()) {
            return std::nullopt;
        }
        m_meshIndices[index] = model.meshes.size();
        model.meshes.push_back(std::move(mesh));
        return m_meshIndices[index];
    }

    std::optional<MeshData> ImportPrimitive(const std::string& name, const cgltf_primitive& primitive) const {
        const cgltf_accessor* positions = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
        if (primitive.type != cgltf_primitive_type_triangles || !positions) {
            Log::GetEngineLogger().Warning("Skipped part of mesh '{}': only triangle lists are supported", name);
            return std::nullopt;
        }
        if (primitive.has_draco_mesh_compression || !positions->buffer_view) {
            Log::GetEngineLogger().Warning("Skipped part of mesh '{}': compressed meshes are not supported", name);
            return std::nullopt;
        }

        const cgltf_accessor* normals = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
        const cgltf_accessor* uvs = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0);
        const cgltf_accessor* tangents = cgltf_find_accessor(&primitive, cgltf_attribute_type_tangent, 0);
        const bool hasNormals = normals && normals->count == positions->count;
        // Given tangents only mean something along the normals they were made with.
        const bool hasTangents = hasNormals && tangents && tangents->count == positions->count;

        const auto* joints = cgltf_find_accessor(&primitive, cgltf_attribute_type_joints, 0);
        const auto* weights = cgltf_find_accessor(&primitive, cgltf_attribute_type_weights, 0);
        const bool skinned =
            joints && weights && joints->count == positions->count && weights->count == positions->count;
        MeshData part;
        if (skinned) {
            part.skin.resize(positions->count);
        }
        part.vertices.resize(positions->count);
        for (cgltf_size i = 0; i < positions->count; ++i) {
            if (skinned) {
                cgltf_uint jointIndices[4]{};
                cgltf_accessor_read_uint(joints, i, jointIndices, 4);
                std::copy_n(jointIndices, 4, part.skin[i].joints.begin());
                auto& weight = part.skin[i].weights;
                cgltf_accessor_read_float(weights, i, &weight.x, 4);
                if (!std::isfinite(weight.x) || !std::isfinite(weight.y) || !std::isfinite(weight.z) ||
                    !std::isfinite(weight.w) || weight.x < 0 || weight.y < 0 || weight.z < 0 || weight.w < 0) {
                    Log::GetEngineLogger().Error("Mesh '{}' contains invalid skin weights", name);
                    return std::nullopt;
                }
                const float sum = weight.x + weight.y + weight.z + weight.w;
                weight = sum > 0.0f ? weight / sum : Vector4{1, 0, 0, 0};
            }
            MeshVertex& vertex = part.vertices[i];
            cgltf_accessor_read_float(positions, i, &vertex.position.x, 3);
            if (hasNormals) {
                cgltf_accessor_read_float(normals, i, &vertex.normal.x, 3);
                vertex.normal = Normalize(vertex.normal);
            }
            if (uvs && i < uvs->count) {
                cgltf_accessor_read_float(uvs, i, &vertex.uv.x, 2);
            }
            if (hasTangents) {
                cgltf_accessor_read_float(tangents, i, &vertex.tangent.x, 4);
                const Vector3 direction = Normalize(Vector3{vertex.tangent.x, vertex.tangent.y, vertex.tangent.z});
                vertex.tangent = {direction.x, direction.y, direction.z, vertex.tangent.w < 0.0f ? -1.0f : 1.0f};
            }
        }

        if (primitive.indices) {
            part.indices.resize(primitive.indices->count);
            for (cgltf_size i = 0; i < primitive.indices->count; ++i) {
                const cgltf_size index = cgltf_accessor_read_index(primitive.indices, i);
                if (index >= positions->count) {
                    Log::GetEngineLogger().Warning("Skipped part of mesh '{}': an index is out of range", name);
                    return std::nullopt;
                }
                part.indices[i] = static_cast<std::uint32_t>(index);
            }
        } else {
            part.indices.resize(positions->count);
            for (std::uint32_t i = 0; i < positions->count; ++i) {
                part.indices[i] = i;
            }
        }
        part.indices.resize(part.indices.size() / 3 * 3);
        if (part.indices.empty()) {
            return std::nullopt;
        }
        if (!hasNormals) {
            part.RecalculateNormals();
        }
        // As glTF asks, tangents a file does not give are made with MikkTSpace.
        if (!hasTangents) {
            part.GenerateTangents();
        }
        return part;
    }

    const cgltf_data& m_data;
    const cgltf_options& m_options;
    const std::filesystem::path* m_directory;
    std::vector<std::optional<std::size_t>> m_imageIndices;
    // Where each of the file's meshes went in the model, once read.
    std::vector<std::optional<std::size_t>> m_meshIndices;
    std::vector<bool> m_meshesImported;
    std::vector<std::optional<std::size_t>> m_nodeIndices;
};

std::optional<ModelData> Import(std::span<const std::byte> bytes, const std::filesystem::path* path) {
    cgltf_options options{};
    options.file.read = &ReadFile;
    options.file.release = &ReleaseFile;

    cgltf_data* parsed = nullptr;
    cgltf_result result = cgltf_parse(&options, bytes.data(), bytes.size(), &parsed);
    const std::unique_ptr<cgltf_data, DataDeleter> data(parsed);
    if (result == cgltf_result_success) {
        const std::string utf8Path = path ? PathToUtf8(*path) : std::string();
        result = cgltf_load_buffers(&options, data.get(), path ? utf8Path.c_str() : nullptr);
        if (result == cgltf_result_unknown_format && !path) {
            Log::GetEngineLogger().Error(
                "Failed to read model: its buffers are separate files, which models read from memory cannot refer to");
            return std::nullopt;
        }
    }
    if (result == cgltf_result_success) {
        result = cgltf_validate(data.get());
    }
    if (result != cgltf_result_success) {
        Log::GetEngineLogger().Error("Failed to read model: {}", GetErrorText(result));
        return std::nullopt;
    }

    const std::filesystem::path directory = path ? path->parent_path() : std::filesystem::path();
    auto model = Importer(*data, options, path ? &directory : nullptr).Import();
    bool valid = std::ranges::all_of(model.skeletons, [](const Skeleton& skeleton) { return skeleton.IsValid(); }) &&
                 std::ranges::all_of(model.animations, [](const AnimationClip& clip) { return clip.IsValid(); });
    for (const auto& node : model.nodes) {
        if (node.skin && node.mesh) {
            const auto& skin = model.meshes[*node.mesh].mesh.skin;
            const auto count = model.skeletons[*node.skin].joints.size();
            valid = valid && !skin.empty();
            for (const auto& vertex : skin) {
                for (const auto joint : vertex.joints) {
                    valid = valid && joint < count;
                }
            }
        }
    }
    if (!valid) {
        Log::GetEngineLogger().Error("Model has invalid skeleton, weights or animation data");
        return std::nullopt;
    }
    return model;
}

} // namespace

std::optional<ModelData> ModelData::Load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        Log::GetEngineLogger().Error("Could not open model '{}'", PathToUtf8(path));
        return std::nullopt;
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

    auto model = Import(std::as_bytes(std::span(bytes)), &path);
    if (!model) {
        Log::GetEngineLogger().Error("Could not load model '{}'", PathToUtf8(path));
    }
    return model;
}

std::optional<ModelData> ModelData::LoadFromMemory(std::span<const std::byte> data) {
    return Import(data, nullptr);
}

bool ModelData::IsColorImage(std::size_t image) const {
    bool usedForData = false;
    for (const MaterialData& material : materials) {
        if (material.baseColorImage == image || material.emissiveImage == image) {
            return true;
        }
        usedForData = usedForData || material.metallicRoughnessImage == image || material.normalImage == image ||
                      material.occlusionImage == image;
    }
    return !usedForData;
}

Matrix4 ModelData::GetWorldTransform(std::size_t node) const {
    Matrix4 transform = nodes[node].transform;
    for (std::optional<std::size_t> parent = nodes[node].parent; parent; parent = nodes[*parent].parent) {
        transform = nodes[*parent].transform * transform;
    }
    return transform;
}

} // namespace Spot
