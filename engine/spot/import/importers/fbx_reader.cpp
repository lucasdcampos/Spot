#include "spot/import/importers/fbx_reader.h"

#include "spot/core/debug/log.h"
#include "spot/core/io/paths.h"

#include <ufbx.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace Spot::Detail {

namespace {

struct SceneDeleter {
    void operator()(ufbx_scene* scene) const { ufbx_free_scene(scene); }
};
struct BakeDeleter {
    void operator()(ufbx_baked_anim* bake) const { ufbx_free_baked_anim(bake); }
};
using ScenePointer = std::unique_ptr<ufbx_scene, SceneDeleter>;

std::string_view ToView(const ufbx_string& text) {
    return {text.data, text.length};
}

Vector3 ToVector(const ufbx_vec3& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

Quaternion ToQuaternion(const ufbx_quat& q) {
    return Normalize(
        Quaternion{static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z), static_cast<float>(q.w)});
}

Matrix4 ToMatrix(const ufbx_matrix& m) {
    Matrix4 result;
    for (std::size_t i = 0; i < 4; ++i) {
        const Vector3 column = ToVector(m.cols[i]);
        result.columns[i] = {column.x, column.y, column.z, i == 3 ? 1.0f : 0.0f};
    }
    return result;
}

TransformComponents ToTransform(const ufbx_transform& transform) {
    return {ToVector(transform.translation), ToQuaternion(transform.rotation), ToVector(transform.scale)};
}

ScenePointer Load(const std::filesystem::path& path, bool content) {
    ufbx_load_opts options{};
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = 1.0f;
    // Scaling the geometry along with the transforms keeps every node in meters, rather than giving the top ones a
    // scale of 0.01, so animations move bones in meters too.
    options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    options.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
    options.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_COMPENSATE;
    options.generate_missing_normals = true;
    options.clean_skin_weights = true;
    // An OBJ's materials are in a file of their own.
    options.load_external_files = true;
    options.ignore_missing_external_files = true;
    options.ignore_geometry = !content;
    options.ignore_animation = !content;

    const std::string utf8 = PathToUtf8(path);
    ufbx_error error{};
    ScenePointer scene(ufbx_load_file_len(utf8.data(), utf8.size(), &options, &error));
    if (!scene) {
        Log::GetEngineLogger().Error("Could not read model '{}': {}", utf8, ToView(error.description));
    }
    return scene;
}

std::filesystem::path FileName(std::string_view path) {
    // Paths in FBX files written on Windows use backslashes, which other systems do not split at.
    std::string text(path);
    std::ranges::replace(text, '\\', '/');
    return Utf8ToPath(text).filename();
}

bool IsFile(const std::filesystem::path& path) {
    std::error_code error;
    return !path.empty() && std::filesystem::is_regular_file(path, error);
}

// The name of the texture's file, as the model gives it, wherever it was.
std::filesystem::path GetTextureFileName(const ufbx_texture& texture) {
    for (const auto* candidate : {&texture.relative_filename, &texture.absolute_filename, &texture.filename}) {
        if (candidate->length) {
            return FileName(ToView(*candidate));
        }
    }
    return {};
}

std::filesystem::path FindTextureFile(const ufbx_texture& texture, const std::filesystem::path& model,
                                      const TextureFinder& find) {
    const auto directory = model.parent_path();
    const std::filesystem::path name = GetTextureFileName(texture);
    std::vector<std::filesystem::path> candidates;
    if (texture.filename.length) {
        candidates.push_back(Utf8ToPath(ToView(texture.filename)));
    }
    if (texture.absolute_filename.length) {
        candidates.push_back(Utf8ToPath(ToView(texture.absolute_filename)));
    }
    if (texture.relative_filename.length) {
        candidates.push_back(directory / Utf8ToPath(ToView(texture.relative_filename)));
    }
    if (!name.empty()) {
        // Searched by name first, so textures extracted beside the model win over where it was made.
        auto extracted = model.stem();
        extracted += ".fbm";
        candidates.insert(candidates.begin(), {directory / name, directory / "textures" / name,
                                               directory / "Textures" / name, directory / extracted / name});
    }
    for (const auto& candidate : candidates) {
        if (IsFile(candidate)) {
            return candidate.lexically_normal();
        }
    }
    if (!name.empty() && find) {
        return find(PathToUtf8(name));
    }
    return {};
}

// Tools name the one animation they export after themselves rather than after what it does.
bool IsGenericClipName(std::string_view name) {
    std::string lower;
    for (const char c : name) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lower.empty() || lower == "mixamo.com" || lower.starts_with("take ") || lower == "take" ||
           lower == "unreal take" || lower == "default take" || lower.starts_with("armature|");
}

class Reader {
public:
    Reader(const ufbx_scene& scene, const std::filesystem::path& path, const TextureFinder& find)
        : m_scene(scene), m_path(path), m_find(find) {}

    FbxModel Read() {
        ReadMaterials();
        // ufbx's root node is the scene itself, which has no transform of its own after conversion.
        m_nodes.assign(m_scene.nodes.count, std::nullopt);
        m_meshes.assign(m_scene.meshes.count, std::nullopt);
        m_meshesRead.assign(m_scene.meshes.count, false);
        for (const ufbx_node* child : m_scene.root_node->children) {
            ReadNode(*child, std::nullopt);
        }
        ReadSkeletons();
        ReadAnimations();
        return std::move(m_result);
    }

private:
    std::optional<std::size_t> ReadImage(const ufbx_texture* texture, bool& missing) {
        if (!texture) {
            return std::nullopt;
        }
        if (texture->type != UFBX_TEXTURE_FILE && texture->file_textures.count) {
            texture = texture->file_textures.data[0];
        }
        const std::uint32_t key = texture->has_file ? texture->file_index : 0x80000000u | texture->typed_id;
        if (const auto found = m_images.find(key); found != m_images.end()) {
            return found->second;
        }
        std::optional<Image> image;
        std::filesystem::path file = FindTextureFile(*texture, m_path, m_find);
        if (!file.empty()) {
            image = Image::Load(file);
        }
        if (!image && texture->content.size) {
            file.clear();
            image =
                Image::LoadFromMemory({static_cast<const std::byte*>(texture->content.data), texture->content.size});
        }
        std::optional<std::size_t> index;
        if (image) {
            index = m_result.model.images.size();
            m_result.model.images.push_back(std::move(*image));
            m_result.imageFiles.push_back(std::move(file));
            m_result.imageNames.push_back(PathToUtf8(GetTextureFileName(*texture)));
        } else {
            missing = true;
            Log::GetEngineLogger().Warning(
                "Texture '{}' of model '{}' was not found; the model is imported without it",
                ToView(texture->relative_filename.length ? texture->relative_filename : texture->filename),
                PathToUtf8(m_path.filename()));
        }
        m_images[key] = index;
        return index;
    }

    static std::uint32_t GetFileKey(const ufbx_texture* texture) {
        if (!texture) {
            return 0xffffffffu;
        }
        if (texture->type != UFBX_TEXTURE_FILE && texture->file_textures.count) {
            texture = texture->file_textures.data[0];
        }
        return texture->has_file ? texture->file_index : 0x80000000u | texture->typed_id;
    }

    // Roughness, or glossiness turned into it, in green, and metalness in blue, as glTF packs them.
    std::optional<std::size_t> PackMetallicRoughness(std::optional<std::size_t> roughness, bool glossiness,
                                                     std::optional<std::size_t> metalness, float roughnessFactor,
                                                     float metalnessFactor) {
        const auto& images = m_result.model.images;
        const Image* reference = roughness ? &images[*roughness] : &images[*metalness];
        const std::uint32_t width = reference->GetWidth();
        const std::uint32_t height = reference->GetHeight();
        const auto sample = [&](std::optional<std::size_t> index, std::uint32_t x, std::uint32_t y) {
            const Image& image = images[*index];
            const std::uint32_t sx = static_cast<std::uint32_t>(std::uint64_t{x} * image.GetWidth() / width);
            const std::uint32_t sy = static_cast<std::uint32_t>(std::uint64_t{y} * image.GetHeight() / height);
            return image.GetPixels()[(std::size_t{sy} * image.GetWidth() + sx) * 4];
        };
        std::vector<std::uint8_t> pixels(std::size_t{width} * height * 4, 255);
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                std::uint8_t* pixel = &pixels[(std::size_t{y} * width + x) * 4];
                pixel[0] = 0;
                if (roughness) {
                    const std::uint8_t value = sample(roughness, x, y);
                    pixel[1] = glossiness ? static_cast<std::uint8_t>(255 - value) : value;
                } else {
                    pixel[1] = static_cast<std::uint8_t>(std::clamp(roughnessFactor, 0.0f, 1.0f) * 255.0f + 0.5f);
                }
                pixel[2] = metalness
                               ? sample(metalness, x, y)
                               : static_cast<std::uint8_t>(std::clamp(metalnessFactor, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
        const std::size_t index = m_result.model.images.size();
        m_result.model.images.emplace_back(width, height, std::move(pixels));
        m_result.imageFiles.emplace_back();
        m_result.imageNames.emplace_back();
        return index;
    }

    void ReadMaterials() {
        for (const ufbx_material* source : m_scene.materials) {
            const auto& pbr = source->pbr;
            MaterialData material;
            material.name = std::string(ToView(source->name));
            bool missing = false;

            const ufbx_texture* baseTexture = pbr.base_color.texture_enabled ? pbr.base_color.texture : nullptr;
            material.baseColorImage = ReadImage(baseTexture, missing);
            // Tools such as Maya replace the color with the texture rather than tint it, and keep a grey color under
            // it.
            const Vector3 color = material.baseColorImage ? Vector3{1, 1, 1} : ToVector(pbr.base_color.value_vec3);
            const float factor = pbr.base_factor.has_value ? static_cast<float>(pbr.base_factor.value_real) : 1.0f;
            const float scale = material.baseColorImage ? 1.0f : factor;
            material.baseColor = {color.x * scale, color.y * scale, color.z * scale, 1.0f};

            // FBX opacity is notoriously unreliable: many files come out see-through anywhere but where they were
            // made. Only a clear partial opacity, or a cutout in the base color's own alpha, is believed.
            const ufbx_texture* opacityTexture = pbr.opacity.texture ? pbr.opacity.texture
                                                 : source->fbx.transparency_factor.texture
                                                     ? source->fbx.transparency_factor.texture
                                                     : source->fbx.transparency_color.texture;
            const float opacity = pbr.opacity.has_value ? static_cast<float>(pbr.opacity.value_real) : 1.0f;
            if (opacityTexture && baseTexture && GetFileKey(opacityTexture) == GetFileKey(baseTexture)) {
                material.alphaMode = AlphaMode::Cutout;
            } else if (opacityTexture) {
                Log::GetEngineLogger().Warning(
                    "Material '{}' has an opacity texture of its own, which is not supported; "
                    "its alpha belongs in the base color",
                    material.name);
            } else if (opacity > 0.01f && opacity < 0.99f) {
                material.alphaMode = AlphaMode::Transparent;
                material.baseColor.a = opacity;
            }

            const ufbx_texture* normal = pbr.normal_map.texture           ? pbr.normal_map.texture
                                         : source->fbx.normal_map.texture ? source->fbx.normal_map.texture
                                                                          : source->fbx.bump.texture;
            // Tools export tangent-space normal maps through the bump slot as often as through the normal one.
            material.normalImage = ReadImage(normal, missing);

            const bool metallic = source->features.metalness.enabled || pbr.metalness.texture;
            material.metallic = metallic ? static_cast<float>(pbr.metalness.value_real) : 0.0f;
            material.roughness = std::clamp(static_cast<float>(pbr.roughness.value_real), 0.0f, 1.0f);
            const ufbx_texture* roughnessTexture = pbr.roughness.texture;
            bool glossiness = false;
            if (!roughnessTexture && pbr.glossiness.texture) {
                roughnessTexture = pbr.glossiness.texture;
                glossiness = true;
            }
            const auto roughnessImage = ReadImage(roughnessTexture, missing);
            const auto metalnessImage = metallic ? ReadImage(pbr.metalness.texture, missing) : std::nullopt;
            if (roughnessImage || metalnessImage) {
                material.metallicRoughnessImage = PackMetallicRoughness(roughnessImage, glossiness, metalnessImage,
                                                                        material.roughness, material.metallic);
                // The texture holds the values, which glTF's factors multiply.
                material.roughness = 1.0f;
                material.metallic = 1.0f;
            }

            material.occlusionImage = ReadImage(pbr.ambient_occlusion.texture, missing);
            const Vector3 emission = ToVector(pbr.emission_color.value_vec3);
            const float emissionFactor =
                pbr.emission_factor.has_value ? static_cast<float>(pbr.emission_factor.value_real) : 1.0f;
            material.emissive = {emission.x * emissionFactor, emission.y * emissionFactor, emission.z * emissionFactor,
                                 1};
            material.emissiveImage = ReadImage(pbr.emission_color.texture, missing);
            if (material.emissiveImage && emission.x == 0 && emission.y == 0 && emission.z == 0) {
                material.emissive = {emissionFactor, emissionFactor, emissionFactor, 1};
            }
            material.doubleSided = source->features.double_sided.enabled;
            m_result.model.materials.push_back(std::move(material));
        }
    }

    void ReadNode(const ufbx_node& node, std::optional<std::size_t> parent) {
        ModelNodeData imported;
        imported.name = std::string(ToView(node.name));
        imported.transform = ToMatrix(node.node_to_parent);
        imported.parent = parent;
        if (node.mesh) {
            imported.mesh = ReadMesh(*node.mesh);
        }
        const std::size_t index = m_result.model.nodes.size();
        m_nodes[node.typed_id] = index;
        m_result.model.nodes.push_back(std::move(imported));
        for (const ufbx_node* child : node.children) {
            ReadNode(*child, index);
        }
    }

    // Each of the file's meshes is read once, however many nodes show it.
    std::optional<std::size_t> ReadMesh(const ufbx_mesh& source) {
        if (m_meshesRead[source.typed_id]) {
            return m_meshes[source.typed_id];
        }
        m_meshesRead[source.typed_id] = true;
        if (source.blend_deformers.count) {
            Log::GetEngineLogger().Warning("Skipped the blend shapes of mesh '{}': they are not supported",
                                           ToView(source.name));
        }
        const ufbx_skin_deformer* skin = source.skin_deformers.count ? source.skin_deformers.data[0] : nullptr;

        ModelMeshData mesh;
        mesh.name = std::string(ToView(source.name));
        std::vector<std::uint32_t> triangle(source.max_face_triangles * 3);
        for (const ufbx_mesh_part& part : source.material_parts) {
            if (!part.num_triangles) {
                continue;
            }
            MeshData data;
            std::vector<SkinVertex> skinning;
            for (const std::uint32_t faceIndex : part.face_indices) {
                const ufbx_face face = source.faces.data[faceIndex];
                const std::uint32_t count = ufbx_triangulate_face(triangle.data(), triangle.size(), &source, face);
                for (std::uint32_t i = 0; i < count * 3; ++i) {
                    const std::uint32_t index = triangle[i];
                    MeshVertex vertex{};
                    vertex.position = ToVector(ufbx_get_vertex_vec3(&source.vertex_position, index));
                    if (source.vertex_normal.exists) {
                        vertex.normal = Normalize(ToVector(ufbx_get_vertex_vec3(&source.vertex_normal, index)));
                    }
                    if (source.vertex_uv.exists) {
                        const ufbx_vec2 uv = ufbx_get_vertex_vec2(&source.vertex_uv, index);
                        // FBX puts v = 0 at the bottom of the texture, glTF and the engine at its top.
                        vertex.uv = {static_cast<float>(uv.x), 1.0f - static_cast<float>(uv.y)};
                    }
                    data.vertices.push_back(vertex);
                    if (skin) {
                        skinning.push_back(ReadSkinVertex(*skin, source.vertex_indices.data[index]));
                    }
                }
            }
            Deduplicate(data, skinning);
            data.skin = std::move(skinning);
            // Tangents are made as glTF's normal maps are baked, with MikkTSpace, which FBX tools use as well.
            data.GenerateTangents();
            mesh.mesh.AppendSubmesh(data);
            if (part.index < source.materials.count && source.materials.data[part.index]) {
                mesh.materials.push_back(source.materials.data[part.index]->typed_id);
            } else {
                mesh.materials.push_back(std::nullopt);
            }
        }
        if (mesh.mesh.indices.empty()) {
            return std::nullopt;
        }
        m_meshes[source.typed_id] = m_result.model.meshes.size();
        if (skin) {
            m_meshSkins.emplace_back(m_result.model.meshes.size(), skin);
        }
        m_result.model.meshes.push_back(std::move(mesh));
        return m_meshes[source.typed_id];
    }

    // The four largest weights, normalized; ufbx sorts them largest first.
    static SkinVertex ReadSkinVertex(const ufbx_skin_deformer& skin, std::uint32_t vertex) {
        SkinVertex result;
        if (vertex >= skin.vertices.count) {
            return result;
        }
        const ufbx_skin_vertex& weights = skin.vertices.data[vertex];
        const std::uint32_t count = std::min(weights.num_weights, 4u);
        std::array<float, 4> values{};
        float sum = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            const ufbx_skin_weight& weight = skin.weights.data[weights.weight_begin + i];
            result.joints[i] = weight.cluster_index;
            values[i] = std::max(static_cast<float>(weight.weight), 0.0f);
            sum += values[i];
        }
        result.weights = sum > 0 ? Vector4{values[0], values[1], values[2], values[3]} / sum : Vector4{1, 0, 0, 0};
        return result;
    }

    // Faces give each corner a vertex of its own; corners that are alike share one.
    static void Deduplicate(MeshData& data, std::vector<SkinVertex>& skin) {
        std::array<ufbx_vertex_stream, 2> streams{{
            {data.vertices.data(), data.vertices.size(), sizeof(MeshVertex)},
            {skin.data(), skin.size(), sizeof(SkinVertex)},
        }};
        data.indices.resize(data.vertices.size());
        ufbx_error error{};
        const std::size_t count = ufbx_generate_indices(streams.data(), skin.empty() ? 1 : 2, data.indices.data(),
                                                        data.indices.size(), nullptr, &error);
        if (error.type != UFBX_ERROR_NONE) {
            for (std::uint32_t i = 0; i < data.indices.size(); ++i) {
                data.indices[i] = i;
            }
            return;
        }
        data.vertices.resize(count);
        if (!skin.empty()) {
            skin.resize(count);
        }
    }

    void ReadSkeletons() {
        std::unordered_map<const ufbx_skin_deformer*, std::size_t> skins;
        for (const auto& [mesh, skin] : m_meshSkins) {
            if (skins.contains(skin)) {
                continue;
            }
            Skeleton skeleton;
            skeleton.name = skin->name.length ? std::string(ToView(skin->name))
                                              : "Skeleton " + std::to_string(m_result.model.skeletons.size());
            for (const auto& node : m_result.model.nodes) {
                skeleton.bones.push_back(
                    {node.name, node.parent ? static_cast<std::int32_t>(*node.parent) : -1, Decompose(node.transform)});
            }
            for (const ufbx_skin_cluster* cluster : skin->clusters) {
                const auto bone = cluster->bone_node ? m_nodes[cluster->bone_node->typed_id] : std::nullopt;
                skeleton.joints.push_back(static_cast<std::uint32_t>(bone.value_or(0)));
                skeleton.inverseBindMatrices.push_back(ToMatrix(cluster->geometry_to_bone));
                if (bone) {
                    m_result.model.nodes[*bone].joint = true;
                }
            }
            skins[skin] = m_result.model.skeletons.size();
            m_result.model.skeletons.push_back(std::move(skeleton));
        }
        for (auto& node : m_result.model.nodes) {
            const auto skin = std::ranges::find(
                m_meshSkins, node.mesh, [](const auto& entry) { return std::optional<std::size_t>(entry.first); });
            if (node.mesh && skin != m_meshSkins.end()) {
                node.skin = skins[skin->second];
            }
        }
    }

    void ReadAnimations() {
        const bool single = m_scene.anim_stacks.count == 1;
        for (const ufbx_anim_stack* stack : m_scene.anim_stacks) {
            ufbx_bake_opts options{};
            options.trim_start_time = true;
            options.key_reduction_enabled = true;
            options.key_reduction_rotation = true;
            ufbx_error error{};
            const std::unique_ptr<ufbx_baked_anim, BakeDeleter> bake(
                ufbx_bake_anim(&m_scene, stack->anim, &options, &error));
            if (!bake) {
                Log::GetEngineLogger().Warning("Skipped animation '{}': {}", ToView(stack->name),
                                               ToView(error.description));
                continue;
            }
            AnimationClip clip;
            clip.name = std::string(ToView(stack->name));
            if (single && IsGenericClipName(clip.name)) {
                clip.name = PathToUtf8(m_path.stem());
            } else if (const auto separator = clip.name.find('|'); separator != std::string::npos) {
                // Blender names its actions "Armature|Walk".
                clip.name = clip.name.substr(separator + 1);
            }
            clip.duration = static_cast<float>(std::max(bake->playback_duration, 0.0));
            for (const ufbx_baked_node& baked : bake->nodes) {
                const auto bone = baked.typed_id < m_nodes.size() ? m_nodes[baked.typed_id] : std::nullopt;
                if (!bone) {
                    continue;
                }
                const TransformComponents bind = Decompose(m_result.model.nodes[*bone].transform);
                AddChannel(clip, *bone, AnimationPath::Translation, baked.translation_keys, baked.constant_translation,
                           {bind.translation.x, bind.translation.y, bind.translation.z, 0});
                AddChannel(clip, *bone, AnimationPath::Rotation, baked.rotation_keys, baked.constant_rotation,
                           {bind.rotation.x, bind.rotation.y, bind.rotation.z, bind.rotation.w});
                AddChannel(clip, *bone, AnimationPath::Scale, baked.scale_keys, baked.constant_scale,
                           {bind.scale.x, bind.scale.y, bind.scale.z, 0});
            }
            if (clip.channels.empty()) {
                continue;
            }
            for (const auto& channel : clip.channels) {
                clip.duration = std::max(clip.duration, channel.times.back());
            }
            m_result.model.animations.push_back(std::move(clip));
        }
    }

    template <typename Keys>
    static void AddChannel(AnimationClip& clip, std::size_t bone, AnimationPath path, const Keys& keys, bool constant,
                           Vector4 bind) {
        if (!keys.count) {
            return;
        }
        const auto value = [&](std::size_t i) {
            const auto& key = keys.data[i].value;
            if constexpr (std::is_same_v<std::decay_t<decltype(key)>, ufbx_quat>) {
                const Quaternion q = ToQuaternion(key);
                return Vector4{q.x, q.y, q.z, q.w};
            } else {
                const Vector3 v = ToVector(key);
                return Vector4{v.x, v.y, v.z, 0};
            }
        };
        // Exporters key bones that never move; those that hold their bind pose need no channel.
        if (constant) {
            const Vector4 first = value(0);
            const bool same = path == AnimationPath::Rotation
                                  ? std::abs(Dot(first, bind)) > 1.0f - 1e-6f
                                  : Length(first - bind) <= 1e-5f * std::max(1.0f, Length(bind));
            if (same) {
                return;
            }
        }
        AnimationChannel channel;
        channel.bone = static_cast<std::uint32_t>(bone);
        channel.path = path;
        channel.interpolation = AnimationInterpolation::Linear;
        for (std::size_t i = 0; i < keys.count; ++i) {
            const float time = std::max(static_cast<float>(keys.data[i].time), 0.0f);
            if (!channel.times.empty() && time <= channel.times.back()) {
                continue;
            }
            channel.times.push_back(time);
            channel.values.push_back(value(i));
        }
        clip.channels.push_back(std::move(channel));
    }

    const ufbx_scene& m_scene;
    const std::filesystem::path& m_path;
    const TextureFinder& m_find;
    FbxModel m_result;
    std::unordered_map<std::uint32_t, std::optional<std::size_t>> m_images;
    std::vector<std::optional<std::size_t>> m_nodes;
    std::vector<std::optional<std::size_t>> m_meshes;
    std::vector<bool> m_meshesRead;
    // The skin of each skinned mesh, in the order of the meshes, so skeletons keep their keys across imports.
    std::vector<std::pair<std::size_t, const ufbx_skin_deformer*>> m_meshSkins;
};

} // namespace

std::optional<FbxModel> ReadFbx(const std::filesystem::path& path, const TextureFinder& findTexture) {
    const ScenePointer scene = Load(path, true);
    if (!scene) {
        return std::nullopt;
    }
    FbxModel result = Reader(*scene, path, findTexture).Read();

    // Materials index the file's own list, which the reader kept in order.
    for (auto& mesh : result.model.meshes) {
        for (auto& material : mesh.materials) {
            if (material && *material >= result.model.materials.size()) {
                material.reset();
            }
        }
    }
    if (path.extension() == ".obj" || path.extension() == ".OBJ") {
        auto library = path;
        library.replace_extension(".mtl");
        result.dependencies.push_back(library);
    }

    bool valid =
        std::ranges::all_of(result.model.skeletons, [](const Skeleton& skeleton) { return skeleton.IsValid(); }) &&
        std::ranges::all_of(result.model.animations, [](const AnimationClip& clip) { return clip.IsValid(); });
    for (const auto& node : result.model.nodes) {
        if (node.skin && node.mesh) {
            const auto count = result.model.skeletons[*node.skin].joints.size();
            for (const auto& vertex : result.model.meshes[*node.mesh].mesh.skin) {
                for (const auto joint : vertex.joints) {
                    valid = valid && joint < count;
                }
            }
        }
    }
    if (!valid) {
        Log::GetEngineLogger().Error("Model '{}' has invalid skeleton, weights or animation data", PathToUtf8(path));
        return std::nullopt;
    }
    return result;
}

std::vector<std::filesystem::path> ExtractFbxTextures(const std::filesystem::path& path,
                                                      const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> written;
    const ScenePointer scene = Load(path, false);
    if (!scene) {
        return written;
    }
    std::error_code error;
    for (const ufbx_texture* texture : scene->textures) {
        if (!texture->content.size) {
            continue;
        }
        std::filesystem::path name = GetTextureFileName(*texture);
        if (name.empty()) {
            name = Utf8ToPath(std::string(ToView(texture->name)) + ".png");
        }
        const auto target = folder / name;
        if (std::filesystem::exists(target, error) || std::ranges::find(written, target) != written.end()) {
            continue;
        }
        std::filesystem::create_directories(folder, error);
        std::ofstream file(target, std::ios::binary);
        file.write(static_cast<const char*>(texture->content.data),
                   static_cast<std::streamsize>(texture->content.size));
        if (file) {
            written.push_back(target);
        } else {
            Log::GetEngineLogger().Error("Could not write texture '{}'", PathToUtf8(target));
        }
    }
    return written;
}

} // namespace Spot::Detail
