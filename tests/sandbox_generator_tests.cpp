#include "temp_directory.h"

#include <generator.h>

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <string_view>

TEST_CASE("The sandbox's generated props load back, wound outward and centered", "[sandbox]") {
    const TempDirectory temp;
    const Generator::OutputCounts first = Generator::GenerateSandboxAssets(temp.GetPath());
    CHECK(first.failed == 0);
    CHECK(first.unchanged == 0);

    for (const Generator::PropInfo& prop : Generator::GetProps()) {
        const std::string name(prop.name);
        CAPTURE(name);
        const auto model = Spot::ModelData::Load(temp.GetPath() / "models" / (name + ".gltf"));
        REQUIRE(model.has_value());
        REQUIRE(model->meshes.size() == 1);
        REQUIRE(model->materials.size() == 1);
        // Its painted colors, then its surface's normals and occlusion, roughness and metallic.
        REQUIRE(model->images.size() == 3);
        CHECK(model->images[0].GetWidth() == 256);
        const Spot::MaterialData& material = model->materials[0];
        CHECK(material.baseColorImage == 0u);
        CHECK(material.normalImage == 1u);
        CHECK(material.metallicRoughnessImage == 2u);
        CHECK(material.occlusionImage == 2u);
        CHECK(model->IsColorImage(0));
        CHECK_FALSE(model->IsColorImage(1));
        CHECK_FALSE(model->IsColorImage(2));

        const Spot::MeshData& mesh = model->meshes[0].mesh;
        REQUIRE(!mesh.indices.empty());
        Spot::Vector3 min{1e9f, 1e9f, 1e9f};
        Spot::Vector3 max{-1e9f, -1e9f, -1e9f};
        for (const Spot::MeshVertex& vertex : mesh.vertices) {
            min = {std::min(min.x, vertex.position.x), std::min(min.y, vertex.position.y),
                   std::min(min.z, vertex.position.z)};
            max = {std::max(max.x, vertex.position.x), std::max(max.y, vertex.position.y),
                   std::max(max.z, vertex.position.z)};
        }
        CHECK(std::abs(min.x + max.x) < 1e-4f);
        CHECK(std::abs(min.y + max.y) < 1e-4f);
        CHECK(std::abs(min.z + max.z) < 1e-4f);

        // Front faces wind counterclockwise seen from where their normals point.
        int inward = 0;
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const Spot::MeshVertex& a = mesh.vertices[mesh.indices[i]];
            const Spot::MeshVertex& b = mesh.vertices[mesh.indices[i + 1]];
            const Spot::MeshVertex& c = mesh.vertices[mesh.indices[i + 2]];
            const Spot::Vector3 face = Spot::Cross(b.position - a.position, c.position - a.position);
            // Spheres have triangles of next to no area at their poles, which face nowhere in particular.
            if (Spot::Length(face) > 1e-6f) {
                inward += Spot::Dot(face, a.normal + b.normal + c.normal) <= 0.0f ? 1 : 0;
            }
        }
        CHECK(inward == 0);
    }

    // The held items' models are made of parts of several surfaces, around their grips.
    for (const std::string_view weapon : Generator::GetWeaponModelNames()) {
        CAPTURE(weapon);
        const auto model = Spot::ModelData::Load(temp.GetPath() / "models" / (std::string(weapon) + ".gltf"));
        REQUIRE(model.has_value());
        CHECK(model->materials.size() >= 2);
        REQUIRE(model->meshes.size() == model->materials.size());
        for (const Spot::ModelMeshData& part : model->meshes) {
            CHECK(!part.mesh.indices.empty());
            for (const Spot::MeshVertex& vertex : part.mesh.vertices) {
                CHECK(Spot::Length(vertex.position) < 1.0f);
            }
        }
    }
    // Each surface's textures tile, the same size as the props'.
    for (const std::string_view surface : Generator::GetSurfaceNames()) {
        for (const std::string_view kind : {"color", "normal", "orm"}) {
            const auto path = temp.GetPath() / "textures" / std::format("{}_{}.png", surface, kind);
            CAPTURE(path);
            const auto image = Spot::Image::Load(path);
            REQUIRE(image.has_value());
            CHECK(image->GetWidth() == 256);
        }
    }

    // Running again writes nothing, as every file comes out the same.
    const Generator::OutputCounts second = Generator::GenerateSandboxAssets(temp.GetPath());
    CHECK(second.written == 0);
    CHECK(second.unchanged == first.written);
}

TEST_CASE("The sandbox's generated sounds load back, loud enough and never clipping", "[sandbox]") {
    const TempDirectory temp;
    CHECK(Generator::GenerateSandboxAssets(temp.GetPath()).failed == 0);

    for (const std::string_view name : Generator::GetSoundNames()) {
        CAPTURE(name);
        const auto sound = Spot::Sound::Load(temp.GetPath() / "sounds" / (std::string(name) + ".wav"));
        REQUIRE(sound);
        CHECK(sound->GetChannelCount() == 1);
        CHECK(sound->GetDuration() > 0.02f);
        float peak = 0.0f;
        for (const float sample : sound->GetSamples()) {
            peak = std::max(peak, std::abs(sample));
        }
        CHECK(peak > 0.3f);
        CHECK(peak < 0.99f);
    }
}
