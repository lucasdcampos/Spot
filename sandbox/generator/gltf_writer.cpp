#include "gltf_writer.h"

#define CGLTF_WRITE_IMPLEMENTATION
#include <cgltf_write.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <limits>

namespace Generator {

namespace {

// cgltf takes mutable C strings; these live as long as the data that points at them.
char* Text(std::string& text) {
    return text.data();
}

template <typename T>
std::size_t Append(std::vector<std::byte>& buffer, const std::vector<T>& values) {
    // Every stream starts on a 4-byte boundary, as accessors of floats and 32-bit indices need.
    buffer.resize((buffer.size() + 3) / 4 * 4);
    const std::size_t offset = buffer.size();
    const auto bytes = std::as_bytes(std::span(values));
    buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    return offset;
}

} // namespace

GltfFiles EncodeGltf(const GltfModel& model, std::string_view binName) {
    GltfFiles files;
    const std::size_t partCount = model.parts.size();
    const bool rigged = !model.skeleton.bones.empty();
    const std::size_t streams = rigged ? 6 : 4, attributeCount = rigged ? 5 : 3;
    std::size_t channelCount = 0;
    for (const auto& clip : model.animations) {
        channelCount += clip.channels.size();
    }
    const std::size_t extraStreams = (rigged ? 1 : 0) + channelCount * 2;

    // Strings and arrays cgltf points into: a deque keeps its strings in place as it grows, and the arrays are sized
    // up front.
    std::deque<std::string> strings;
    const auto keep = [&strings](std::string text) { return Text(strings.emplace_back(std::move(text))); };

    std::vector<cgltf_buffer_view> views(partCount * streams + extraStreams);
    std::vector<cgltf_accessor> accessors(partCount * streams + extraStreams);
    std::vector<cgltf_attribute> attributes(partCount * attributeCount);
    std::vector<cgltf_primitive> primitives(partCount);
    std::vector<cgltf_mesh> meshes(partCount);
    std::vector<cgltf_node> nodes(partCount + model.skeleton.bones.size() + model.markers.size());
    std::vector<cgltf_node*> sceneNodes(partCount);
    std::vector<cgltf_material> materials(model.materials.size());
    // Each material has up to three images, which materials share by URI.
    std::vector<cgltf_image> images;
    std::vector<cgltf_texture> textures;
    images.reserve(model.materials.size() * 3);
    textures.reserve(model.materials.size() * 3);
    cgltf_sampler sampler{};
    sampler.mag_filter = cgltf_filter_type_linear;
    sampler.min_filter = cgltf_filter_type_linear_mipmap_linear;
    sampler.wrap_s = cgltf_wrap_mode_repeat;
    sampler.wrap_t = cgltf_wrap_mode_repeat;

    cgltf_buffer buffer{};

    for (std::size_t i = 0; i < model.materials.size(); ++i) {
        const GltfMaterial& source = model.materials[i];
        cgltf_material& material = materials[i];
        material.name = keep(source.name);
        material.has_pbr_metallic_roughness = true;
        material.pbr_metallic_roughness.base_color_factor[0] = source.baseColor.r;
        material.pbr_metallic_roughness.base_color_factor[1] = source.baseColor.g;
        material.pbr_metallic_roughness.base_color_factor[2] = source.baseColor.b;
        material.pbr_metallic_roughness.base_color_factor[3] = source.baseColor.a;
        material.pbr_metallic_roughness.metallic_factor = source.metallic;
        material.pbr_metallic_roughness.roughness_factor = source.roughness;
        // glTF's emissive factor stops at 1; brighter light is that times a strength.
        const float strength = std::max({source.emissive.r, source.emissive.g, source.emissive.b, 1.0f});
        material.emissive_factor[0] = source.emissive.r / strength;
        material.emissive_factor[1] = source.emissive.g / strength;
        material.emissive_factor[2] = source.emissive.b / strength;
        if (strength > 1.0f) {
            material.has_emissive_strength = true;
            material.emissive_strength.emissive_strength = strength;
        }
        material.double_sided = source.doubleSided;
        material.alpha_mode = source.transparent ? cgltf_alpha_mode_blend
                              : source.cutout    ? cgltf_alpha_mode_mask
                                                 : cgltf_alpha_mode_opaque;
        material.alpha_cutoff = 0.5f;
        const auto texture = [&](const std::string& uri) -> cgltf_texture* {
            if (uri.empty()) {
                return nullptr;
            }
            for (cgltf_texture& existing : textures) {
                if (existing.image->uri == uri) {
                    return &existing;
                }
            }
            cgltf_image& image = images.emplace_back();
            image.uri = keep(uri);
            cgltf_texture& added = textures.emplace_back();
            added.image = &image;
            added.sampler = &sampler;
            return &added;
        };
        if (cgltf_texture* color = texture(source.baseColorImage)) {
            material.pbr_metallic_roughness.base_color_texture.texture = color;
            material.pbr_metallic_roughness.base_color_texture.scale = 1.0f;
        }
        if (cgltf_texture* normal = texture(source.normalImage)) {
            material.normal_texture.texture = normal;
            material.normal_texture.scale = 1.0f;
        }
        if (cgltf_texture* orm = texture(source.ormImage)) {
            material.pbr_metallic_roughness.metallic_roughness_texture.texture = orm;
            material.pbr_metallic_roughness.metallic_roughness_texture.scale = 1.0f;
            material.occlusion_texture.texture = orm;
            material.occlusion_texture.scale = 1.0f;
        }
    }

    for (std::size_t i = 0; i < partCount; ++i) {
        const GltfPart& part = model.parts[i];
        std::vector<Spot::Vector3> positions;
        std::vector<Spot::Vector3> normals;
        std::vector<Spot::Vector2> uvs;
        for (const Spot::MeshVertex& vertex : part.mesh.vertices) {
            positions.push_back(vertex.position);
            normals.push_back(vertex.normal);
            uvs.push_back(vertex.uv);
        }

        const auto addView = [&](std::size_t index, std::size_t offset, std::size_t size, cgltf_buffer_view_type type) {
            cgltf_buffer_view& view = views[i * streams + index];
            view.buffer = &buffer;
            view.offset = offset;
            view.size = size;
            view.type = type;
            return &view;
        };
        const auto addAccessor = [&](std::size_t index, cgltf_buffer_view* view, cgltf_type type,
                                     cgltf_component_type component, std::size_t count) {
            cgltf_accessor& accessor = accessors[i * streams + index];
            accessor.buffer_view = view;
            accessor.type = type;
            accessor.component_type = component;
            accessor.count = count;
            return &accessor;
        };

        const std::size_t count = part.mesh.vertices.size();
        cgltf_accessor* position = addAccessor(
            0,
            addView(0, Append(files.buffer, positions), count * sizeof(Spot::Vector3), cgltf_buffer_view_type_vertices),
            cgltf_type_vec3, cgltf_component_type_r_32f, count);
        // Positions must give their bounds.
        position->has_min = true;
        position->has_max = true;
        for (int axis = 0; axis < 3; ++axis) {
            position->min[axis] = std::numeric_limits<float>::max();
            position->max[axis] = std::numeric_limits<float>::lowest();
        }
        for (const Spot::Vector3& p : positions) {
            const float values[3] = {p.x, p.y, p.z};
            for (int axis = 0; axis < 3; ++axis) {
                position->min[axis] = std::min(position->min[axis], values[axis]);
                position->max[axis] = std::max(position->max[axis], values[axis]);
            }
        }
        cgltf_accessor* normal = addAccessor(
            1,
            addView(1, Append(files.buffer, normals), count * sizeof(Spot::Vector3), cgltf_buffer_view_type_vertices),
            cgltf_type_vec3, cgltf_component_type_r_32f, count);
        cgltf_accessor* uv = addAccessor(
            2, addView(2, Append(files.buffer, uvs), count * sizeof(Spot::Vector2), cgltf_buffer_view_type_vertices),
            cgltf_type_vec2, cgltf_component_type_r_32f, count);
        const std::size_t indexCount = part.mesh.indices.size();
        cgltf_accessor* indices =
            addAccessor(3,
                        addView(3, Append(files.buffer, part.mesh.indices), indexCount * sizeof(std::uint32_t),
                                cgltf_buffer_view_type_indices),
                        cgltf_type_scalar, cgltf_component_type_r_32u, indexCount);

        cgltf_attribute* partAttributes = &attributes[i * attributeCount];
        partAttributes[0] = {keep("POSITION"), cgltf_attribute_type_position, 0, position};
        partAttributes[1] = {keep("NORMAL"), cgltf_attribute_type_normal, 0, normal};
        partAttributes[2] = {keep("TEXCOORD_0"), cgltf_attribute_type_texcoord, 0, uv};

        if (rigged) {
            std::vector<std::array<std::uint16_t, 4>> joints;
            std::vector<Spot::Vector4> weights;
            for (const auto& vertex : part.mesh.skin) {
                joints.push_back(
                    {static_cast<std::uint16_t>(vertex.joints[0]), static_cast<std::uint16_t>(vertex.joints[1]),
                     static_cast<std::uint16_t>(vertex.joints[2]), static_cast<std::uint16_t>(vertex.joints[3])});
                weights.push_back(vertex.weights);
            }
            auto* joint = addAccessor(4,
                                      addView(4, Append(files.buffer, joints), joints.size() * sizeof(joints[0]),
                                              cgltf_buffer_view_type_vertices),
                                      cgltf_type_vec4, cgltf_component_type_r_16u, joints.size());
            auto* weight = addAccessor(5,
                                       addView(5, Append(files.buffer, weights), weights.size() * sizeof(Spot::Vector4),
                                               cgltf_buffer_view_type_vertices),
                                       cgltf_type_vec4, cgltf_component_type_r_32f, weights.size());
            partAttributes[3] = {keep("JOINTS_0"), cgltf_attribute_type_joints, 0, joint};
            partAttributes[4] = {keep("WEIGHTS_0"), cgltf_attribute_type_weights, 0, weight};
        }
        cgltf_primitive& primitive = primitives[i];
        primitive.type = cgltf_primitive_type_triangles;
        primitive.indices = indices;
        primitive.material = materials.empty() ? nullptr : &materials[std::min(part.material, materials.size() - 1)];
        primitive.attributes = partAttributes;
        primitive.attributes_count = attributeCount;

        meshes[i].name = keep(part.name);
        meshes[i].primitives = &primitive;
        meshes[i].primitives_count = 1;
        nodes[i].name = keep(part.name);
        nodes[i].mesh = &meshes[i];
        sceneNodes[i] = &nodes[i];
    }

    std::vector<cgltf_node*> joints;
    std::vector<std::vector<cgltf_node*>> children(model.skeleton.bones.size());
    cgltf_skin skin{};
    std::size_t nextStream = partCount * streams;
    const auto addRaw = [&](std::size_t offset, std::size_t bytes, cgltf_type type, std::size_t count) {
        auto& view = views[nextStream];
        view.buffer = &buffer;
        view.offset = offset;
        view.size = bytes;
        auto& accessor = accessors[nextStream++];
        accessor.buffer_view = &view;
        accessor.type = type;
        accessor.component_type = cgltf_component_type_r_32f;
        accessor.count = count;
        return &accessor;
    };
    if (rigged) {
        for (std::size_t i = 0; i < model.skeleton.bones.size(); ++i) {
            const auto& bone = model.skeleton.bones[i];
            auto& node = nodes[partCount + i];
            node.name = keep(bone.name);
            node.has_translation = true;
            node.has_rotation = true;
            node.has_scale = true;
            node.translation[0] = bone.bindPose.translation.x;
            node.translation[1] = bone.bindPose.translation.y;
            node.translation[2] = bone.bindPose.translation.z;
            node.rotation[0] = bone.bindPose.rotation.x;
            node.rotation[1] = bone.bindPose.rotation.y;
            node.rotation[2] = bone.bindPose.rotation.z;
            node.rotation[3] = bone.bindPose.rotation.w;
            node.scale[0] = bone.bindPose.scale.x;
            node.scale[1] = bone.bindPose.scale.y;
            node.scale[2] = bone.bindPose.scale.z;
            if (bone.parent >= 0) {
                node.parent = &nodes[partCount + static_cast<std::size_t>(bone.parent)];
                children[static_cast<std::size_t>(bone.parent)].push_back(&node);
            } else {
                sceneNodes.push_back(&node);
            }
        }
        for (std::size_t i = 0; i < children.size(); ++i) {
            nodes[partCount + i].children = children[i].data();
            nodes[partCount + i].children_count = children[i].size();
        }
        for (const auto index : model.skeleton.joints) {
            joints.push_back(&nodes[partCount + index]);
        }
        skin.name = keep(model.skeleton.name);
        skin.joints = joints.data();
        skin.joints_count = joints.size();
        skin.inverse_bind_matrices = addRaw(Append(files.buffer, model.skeleton.inverseBindMatrices),
                                            model.skeleton.inverseBindMatrices.size() * sizeof(Spot::Matrix4),
                                            cgltf_type_mat4, model.skeleton.inverseBindMatrices.size());
        for (std::size_t i = 0; i < partCount; ++i) {
            nodes[i].skin = &skin;
        }
    }
    for (std::size_t i = 0; i < model.markers.size(); ++i) {
        auto& node = nodes[partCount + model.skeleton.bones.size() + i];
        node.name = keep(model.markers[i].name);
        node.has_translation = true;
        node.translation[0] = model.markers[i].position.x;
        node.translation[1] = model.markers[i].position.y;
        node.translation[2] = model.markers[i].position.z;
        sceneNodes.push_back(&node);
    }
    std::vector<cgltf_animation> animations(model.animations.size());
    std::vector<cgltf_animation_sampler> samplers(channelCount);
    std::vector<cgltf_animation_channel> channels(channelCount);
    std::size_t firstChannel = 0;
    for (std::size_t i = 0; i < model.animations.size(); ++i) {
        const auto& source = model.animations[i];
        auto& animation = animations[i];
        animation.name = keep(source.name);
        animation.samplers = samplers.data() + firstChannel;
        animation.channels = channels.data() + firstChannel;
        animation.samplers_count = source.channels.size();
        animation.channels_count = source.channels.size();
        if (!source.events.empty()) {
            auto extras = nlohmann::json::object();
            extras["events"] = nlohmann::json::array();
            for (const auto& event : source.events) {
                extras["events"].push_back({{"time", event.time}, {"name", event.name}, {"payload", event.payload}});
            }
            animation.extras.data = keep(extras.dump());
        }
        for (std::size_t j = 0; j < source.channels.size(); ++j) {
            const auto& channel = source.channels[j];
            auto& animationSampler = samplers[firstChannel + j];
            animationSampler.input = addRaw(Append(files.buffer, channel.times), channel.times.size() * sizeof(float),
                                            cgltf_type_scalar, channel.times.size());
            animationSampler.input->has_min = animationSampler.input->has_max = true;
            animationSampler.input->min[0] = channel.times.front();
            animationSampler.input->max[0] = channel.times.back();
            std::vector<float> values;
            for (const auto& value : channel.values) {
                values.insert(values.end(), {value.x, value.y, value.z});
                if (channel.path == Spot::AnimationPath::Rotation) {
                    values.push_back(value.w);
                }
            }
            animationSampler.output =
                addRaw(Append(files.buffer, values), values.size() * sizeof(float),
                       channel.path == Spot::AnimationPath::Rotation ? cgltf_type_vec4 : cgltf_type_vec3,
                       channel.values.size());
            animationSampler.interpolation =
                channel.interpolation == Spot::AnimationInterpolation::Step    ? cgltf_interpolation_type_step
                : channel.interpolation == Spot::AnimationInterpolation::Cubic ? cgltf_interpolation_type_cubic_spline
                                                                               : cgltf_interpolation_type_linear;
            auto& imported = channels[firstChannel + j];
            imported.sampler = &animationSampler;
            imported.target_node = &nodes[partCount + channel.bone];
            imported.target_path = channel.path == Spot::AnimationPath::Rotation ? cgltf_animation_path_type_rotation
                                   : channel.path == Spot::AnimationPath::Scale ? cgltf_animation_path_type_scale
                                                                                : cgltf_animation_path_type_translation;
        }
        firstChannel += source.channels.size();
    }

    buffer.size = files.buffer.size();
    buffer.uri = keep(std::string(binName));

    cgltf_scene scene{};
    scene.nodes = sceneNodes.data();
    scene.nodes_count = sceneNodes.size();

    cgltf_data data{};
    data.asset.version = keep("2.0");
    data.asset.generator = keep("Spot sandbox generator");
    data.skins = rigged ? &skin : nullptr;
    data.skins_count = rigged ? 1 : 0;
    data.animations = animations.data();
    data.animations_count = animations.size();
    data.meshes = meshes.data();
    data.meshes_count = meshes.size();
    data.materials = materials.data();
    data.materials_count = materials.size();
    data.accessors = accessors.data();
    data.accessors_count = accessors.size();
    data.buffer_views = views.data();
    data.buffer_views_count = views.size();
    data.buffers = &buffer;
    data.buffers_count = 1;
    data.images = images.data();
    data.images_count = images.size();
    data.textures = textures.data();
    data.textures_count = textures.size();
    data.samplers = &sampler;
    data.samplers_count = textures.empty() ? 0 : 1;
    data.nodes = nodes.data();
    data.nodes_count = nodes.size();
    data.scenes = &scene;
    data.scenes_count = 1;
    data.scene = &scene;

    const cgltf_options options{};
    const cgltf_size size = cgltf_write(&options, nullptr, 0, &data);
    std::string json(size, '\0');
    cgltf_write(&options, json.data(), size, &data);
    // The size counts the terminating null, which the file does not have.
    while (!json.empty() && json.back() == '\0') {
        json.pop_back();
    }
    files.json = std::move(json);
    files.json.push_back('\n');
    return files;
}

} // namespace Generator
