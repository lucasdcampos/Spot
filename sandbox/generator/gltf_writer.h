#pragma once

#include <spot.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Generator {

struct GltfMaterial {
    std::string name;
    Spot::Color baseColor = Spot::Color::White;
    // Images' URIs, relative to the .gltf file, such as "../textures/crate.png"; none when empty.
    std::string baseColorImage;
    // Multiplied by the ORM image's blue and green channels when there is one.
    float metallic = 0.0f;
    float roughness = 0.8f;
    std::string normalImage;
    // Occlusion, roughness and metallic in red, green and blue: both the occlusion and the metallic-roughness texture.
    std::string ormImage;
    Spot::Color emissive = Spot::Color::Black;
    bool doubleSided = false;
};

// A mesh of the model, drawn with one of its materials.
struct GltfPart {
    std::string name;
    Spot::MeshData mesh;
    std::size_t material = 0;
};

struct GltfModel {
    std::vector<GltfMaterial> materials;
    std::vector<GltfPart> parts;
    Spot::Skeleton skeleton;
    std::vector<Spot::AnimationClip> animations;
};

// A model as a .gltf file, whose buffer is in a .bin file beside it.
struct GltfFiles {
    std::string json;
    std::vector<std::byte> buffer;
};

// Encodes the model as glTF 2.0, with a node per part. The buffer holds, for each part, its positions, normals, UVs
// and indices, and binName is the URI the JSON refers to it by.
GltfFiles EncodeGltf(const GltfModel& model, std::string_view binName);

} // namespace Generator
