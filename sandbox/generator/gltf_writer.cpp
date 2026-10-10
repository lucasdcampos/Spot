#include "gltf_writer.h"

#define CGLTF_WRITE_IMPLEMENTATION
#include <cgltf_write.h>

#include <algorithm>
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

    // Strings and arrays cgltf points into: a deque keeps its strings in place as it grows, and the arrays are sized
    // up front.
    std::deque<std::string> strings;
    const auto keep = [&strings](std::string text) { return Text(strings.emplace_back(std::move(text))); };

    std::vector<cgltf_buffer_view> views(partCount * 4);
    std::vector<cgltf_accessor> accessors(partCount * 4);
    std::vector<cgltf_attribute> attributes(partCount * 3);
    std::vector<cgltf_primitive> primitives(partCount);
    std::vector<cgltf_mesh> meshes(partCount);
    std::vector<cgltf_node> nodes(partCount);
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
            cgltf_buffer_view& view = views[i * 4 + index];
            view.buffer = &buffer;
            view.offset = offset;
            view.size = size;
            view.type = type;
            return &view;
        };
        const auto addAccessor = [&](std::size_t index, cgltf_buffer_view* view, cgltf_type type,
                                     cgltf_component_type component, std::size_t count) {
            cgltf_accessor& accessor = accessors[i * 4 + index];
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

        cgltf_attribute* partAttributes = &attributes[i * 3];
        partAttributes[0] = {keep("POSITION"), cgltf_attribute_type_position, 0, position};
        partAttributes[1] = {keep("NORMAL"), cgltf_attribute_type_normal, 0, normal};
        partAttributes[2] = {keep("TEXCOORD_0"), cgltf_attribute_type_texcoord, 0, uv};

        cgltf_primitive& primitive = primitives[i];
        primitive.type = cgltf_primitive_type_triangles;
        primitive.indices = indices;
        primitive.material = materials.empty() ? nullptr : &materials[std::min(part.material, materials.size() - 1)];
        primitive.attributes = partAttributes;
        primitive.attributes_count = 3;

        meshes[i].name = keep(part.name);
        meshes[i].primitives = &primitive;
        meshes[i].primitives_count = 1;
        nodes[i].name = keep(part.name);
        nodes[i].mesh = &meshes[i];
        sceneNodes[i] = &nodes[i];
    }

    buffer.size = files.buffer.size();
    buffer.uri = keep(std::string(binName));

    cgltf_scene scene{};
    scene.nodes = sceneNodes.data();
    scene.nodes_count = partCount;

    cgltf_data data{};
    data.asset.version = keep("2.0");
    data.asset.generator = keep("Spot sandbox generator");
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
