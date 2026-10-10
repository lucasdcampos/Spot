#include "graphics/model_data.h"

#include "core/log.h"
#include "core/paths.h"
#include "math/matrix.h"

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

        if (const cgltf_scene* scene = m_data.scene ? m_data.scene : (m_data.scenes_count ? m_data.scenes : nullptr)) {
            for (cgltf_size i = 0; i < scene->nodes_count; ++i) {
                ImportNode(*scene->nodes[i], model);
            }
        } else {
            for (cgltf_size i = 0; i < m_data.nodes_count; ++i) {
                if (!m_data.nodes[i].parent) {
                    ImportNode(m_data.nodes[i], model);
                }
            }
        }
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
                if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image) {
                    material.baseColorImage =
                        m_imageIndices[cgltf_image_index(&m_data, pbr.base_color_texture.texture->image)];
                }
            }
            model.materials.push_back(std::move(material));
        }
    }

    void ImportNode(const cgltf_node& node, ModelData& model) const {
        if (node.mesh) {
            Matrix4 world;
            cgltf_node_transform_world(&node, &world.columns[0].x);
            for (cgltf_size i = 0; i < node.mesh->primitives_count; ++i) {
                ImportPrimitive(node, node.mesh->primitives[i], world, model);
            }
        }
        for (cgltf_size i = 0; i < node.children_count; ++i) {
            ImportNode(*node.children[i], model);
        }
    }

    void ImportPrimitive(const cgltf_node& node, const cgltf_primitive& primitive, const Matrix4& world,
                         ModelData& model) const {
        const char* name = node.mesh->name ? node.mesh->name : (node.name ? node.name : "");
        const cgltf_accessor* positions = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
        if (primitive.type != cgltf_primitive_type_triangles || !positions) {
            Log::GetEngineLogger().Warning("Skipped part of mesh '{}': only triangle lists are supported", name);
            return;
        }
        if (primitive.has_draco_mesh_compression || !positions->buffer_view) {
            Log::GetEngineLogger().Warning("Skipped part of mesh '{}': compressed meshes are not supported", name);
            return;
        }

        const cgltf_accessor* normals = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
        const cgltf_accessor* uvs = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0);
        const cgltf_accessor* tangents = cgltf_find_accessor(&primitive, cgltf_attribute_type_tangent, 0);
        const bool hasNormals = normals && normals->count == positions->count;
        // Given tangents only mean something along the normals they were made with.
        const bool hasTangents = hasNormals && tangents && tangents->count == positions->count;
        // A mirroring transform flips which side of the tangent the bitangent is on.
        const auto axis = [&](int column) {
            return Vector3{world.columns[column].x, world.columns[column].y, world.columns[column].z};
        };
        const bool mirrored = Dot(Cross(axis(0), axis(1)), axis(2)) < 0.0f;

        // Normals need the inverse transpose to stay perpendicular to surfaces under non-uniform scale.
        const Matrix4 normalMatrix = Transpose(Inverse(world));
        ModelMeshData part;
        part.name = name;
        part.mesh.vertices.resize(positions->count);
        for (cgltf_size i = 0; i < positions->count; ++i) {
            MeshVertex& vertex = part.mesh.vertices[i];
            cgltf_accessor_read_float(positions, i, &vertex.position.x, 3);
            const Vector4 position = world * Vector4{vertex.position.x, vertex.position.y, vertex.position.z, 1.0f};
            vertex.position = {position.x, position.y, position.z};
            if (hasNormals) {
                cgltf_accessor_read_float(normals, i, &vertex.normal.x, 3);
                const Vector4 normal = normalMatrix * Vector4{vertex.normal.x, vertex.normal.y, vertex.normal.z, 0.0f};
                vertex.normal = Normalize(Vector3{normal.x, normal.y, normal.z});
            }
            if (uvs && i < uvs->count) {
                cgltf_accessor_read_float(uvs, i, &vertex.uv.x, 2);
            }
            if (hasTangents) {
                cgltf_accessor_read_float(tangents, i, &vertex.tangent.x, 4);
                const Vector4 tangent = world * Vector4{vertex.tangent.x, vertex.tangent.y, vertex.tangent.z, 0.0f};
                const Vector3 direction = Normalize(Vector3{tangent.x, tangent.y, tangent.z});
                const float side = (vertex.tangent.w < 0.0f ? -1.0f : 1.0f) * (mirrored ? -1.0f : 1.0f);
                vertex.tangent = {direction.x, direction.y, direction.z, side};
            }
        }

        if (primitive.indices) {
            part.mesh.indices.resize(primitive.indices->count);
            for (cgltf_size i = 0; i < primitive.indices->count; ++i) {
                const cgltf_size index = cgltf_accessor_read_index(primitive.indices, i);
                if (index >= positions->count) {
                    Log::GetEngineLogger().Warning("Skipped part of mesh '{}': an index is out of range", name);
                    return;
                }
                part.mesh.indices[i] = static_cast<std::uint32_t>(index);
            }
        } else {
            part.mesh.indices.resize(positions->count);
            for (std::uint32_t i = 0; i < positions->count; ++i) {
                part.mesh.indices[i] = i;
            }
        }
        part.mesh.indices.resize(part.mesh.indices.size() / 3 * 3);

        // A mirroring transform turns counterclockwise triangles clockwise, so they are wound back.
        if (mirrored) {
            for (std::size_t i = 0; i < part.mesh.indices.size(); i += 3) {
                std::swap(part.mesh.indices[i + 1], part.mesh.indices[i + 2]);
            }
        }
        if (!hasNormals) {
            part.mesh.RecalculateNormals();
        }
        // As glTF asks, tangents a file does not give are made with MikkTSpace.
        if (!hasTangents) {
            part.mesh.GenerateTangents();
        }

        if (primitive.material) {
            part.material = cgltf_material_index(&m_data, primitive.material);
        }
        model.meshes.push_back(std::move(part));
    }

    const cgltf_data& m_data;
    const cgltf_options& m_options;
    const std::filesystem::path* m_directory;
    std::vector<std::optional<std::size_t>> m_imageIndices;
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
    return Importer(*data, options, path ? &directory : nullptr).Import();
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

} // namespace Spot
