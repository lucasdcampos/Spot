// Golden-image tests: reference scenes rendered as games show them, through SceneRenderer, and compared with images
// stored in tests/golden/ within a tolerance, so changes to how things look are seen before they ship. When a change
// is meant, run the tests with SPOT_UPDATE_GOLDEN=1 to store the new images, look at them, and commit them with it.
#include "rendering_application.h"

#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace Spot::Tests;

namespace {

constexpr std::uint32_t Width = 160;
constexpr std::uint32_t Height = 120;

// How far a render may stray from its golden image: GPUs and drivers round differently, so a few pixels may differ by
// more than a few steps, but no change worth seeing stays under these.
struct Tolerance {
    // Channels that differ by more than this many steps make their pixel differ.
    int channelSteps = 12;
    // The share of pixels that may differ.
    double differingPixels = 0.003;
    // The average difference of every channel, in steps.
    double meanSteps = 1.0;
};

bool IsUpdating() {
    std::string update;
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, "SPOT_UPDATE_GOLDEN") == 0 && value) {
        update = value;
        std::free(value);
    }
#else
    if (const char* value = std::getenv("SPOT_UPDATE_GOLDEN")) {
        update = value;
    }
#endif
    return !update.empty() && update != "0";
}

// Whether the render stays within the tolerance of the expected image; when not, leaves the render and an image of
// the differences in the build's golden_output folder, under the name.
bool CompareImages(std::string_view name, const Spot::Image& expected, const Spot::Image& render,
                   const Tolerance& tolerance) {
    const std::filesystem::path output = SPOT_GOLDEN_OUTPUT_DIR;
    std::filesystem::create_directories(output);
    REQUIRE(expected.GetWidth() == render.GetWidth());
    REQUIRE(expected.GetHeight() == render.GetHeight());

    const auto a = expected.GetPixels();
    const auto b = render.GetPixels();
    std::vector<std::uint8_t> difference(a.size());
    std::size_t differing = 0;
    double total = 0.0;
    for (std::size_t pixel = 0; pixel < a.size() / 4; ++pixel) {
        bool differs = false;
        for (std::size_t channel = 0; channel < 4; ++channel) {
            const std::size_t i = pixel * 4 + channel;
            const int steps = std::abs(int{a[i]} - int{b[i]});
            total += steps;
            differs = differs || steps > tolerance.channelSteps;
            // Differences are brightened, so small ones show.
            difference[i] = channel == 3 ? 255 : static_cast<std::uint8_t>(std::min(steps * 8, 255));
        }
        differing += differs ? 1 : 0;
    }
    const double differingShare = static_cast<double>(differing) / static_cast<double>(a.size() / 4);
    const double mean = total / static_cast<double>(a.size());
    INFO(name << ": " << differing << " differing pixels (" << differingShare * 100.0 << "%), mean difference " << mean
              << " steps");
    const bool matches = differingShare <= tolerance.differingPixels && mean <= tolerance.meanSteps;
    if (!matches) {
        render.SavePng(output / std::format("{}.png", name));
        Spot::Image(render.GetWidth(), render.GetHeight(), std::move(difference))
            .SavePng(output / std::format("{}_difference.png", name));
        UNSCOPED_INFO("The render and its differences are in " << output.string());
    }
    return matches;
}

// Compares the render with tests/golden/<name>.png. A missing or mismatched golden image fails the test, and leaves
// the render and, for mismatches, an image of the differences in the build's golden_output folder.
void CheckGolden(std::string_view name, const Spot::Image& render, const Tolerance& tolerance = {}) {
    const std::filesystem::path golden = std::filesystem::path(SPOT_GOLDEN_DIR) / std::format("{}.png", name);
    const std::filesystem::path output = SPOT_GOLDEN_OUTPUT_DIR;
    if (IsUpdating()) {
        std::filesystem::create_directories(golden.parent_path());
        REQUIRE(render.SavePng(golden));
        WARN("Updated the golden image " << golden.string());
        return;
    }
    std::filesystem::create_directories(output);
    const auto expected = std::filesystem::exists(golden) ? Spot::Image::Load(golden) : std::nullopt;
    if (!expected) {
        render.SavePng(output / std::format("{}.png", name));
        FAIL("No golden image " << golden.string() << "; the render is in " << output.string()
                                << ". Run with SPOT_UPDATE_GOLDEN=1 to store it.");
    }
    CHECK(CompareImages(name, *expected, render, tolerance));
}

// Renders the scene, as built by the setup, through SceneRenderer into an image. Multisampling, dithering and bloom
// are left out, which vary the most between GPUs without telling more about how things look.
std::optional<Spot::Image> RenderScene(const std::function<void(Spot::GraphicsDevice&, Spot::Scene&)>& setup,
                                       const Spot::GraphicsConfig& graphics = {}) {
    std::unique_ptr<Spot::Texture> target;
    std::unique_ptr<Spot::SceneRenderer> renderer;
    Spot::Scene scene;
    std::vector<std::uint8_t> pixels;
    int frame = 0;
    RenderingApplication application(
        [&](Spot::Application& app, Spot::RenderContext& context) {
            auto& device = app.GetGraphicsDevice();
            ++frame;
            if (frame == 1) {
                target = device.CreateTexture(
                    {.width = Width, .height = Height, .format = Spot::TextureFormat::Rgba8Srgb, .renderTarget = true});
                REQUIRE(target);
                renderer = std::make_unique<Spot::SceneRenderer>(
                    device, Spot::SceneRendererConfig{.targetFormat = Spot::TextureFormat::Rgba8Srgb,
                                                      .sampleCount = 1,
                                                      .tonemapping = {.dither = false},
                                                      .bloom = {.intensity = 0.0f}});
                REQUIRE(renderer->IsValid());
                setup(device, scene);
                renderer->Render(context, scene, target.get());
            } else if (frame == 2) {
                pixels = device.DownloadTexture(*target);
            }
        },
        {}, graphics);
    REQUIRE(application.Run() == EXIT_SUCCESS);
    if (pixels.size() != std::size_t{Width} * Height * 4) {
        return std::nullopt;
    }
    return Spot::Image(Width, Height, std::move(pixels));
}

// A camera at the position looking at the target, and the sun shining in the direction with a sky that it lights.
void AddCameraAndSky(Spot::Scene& scene, Spot::Vector3 position, Spot::Vector3 target, Spot::Vector3 sunDirection) {
    auto& camera = scene.CreateEntity("Camera");
    camera.GetTransform().SetPosition(position);
    camera.GetTransform().SetRotation(Spot::Quaternion::LookRotation(target - position));
    camera.AddComponent<Spot::Camera>().SetFieldOfView(Spot::ToRadians(40.0f));
    auto& sun = scene.CreateEntity("Sun");
    sun.GetTransform().SetRotation(Spot::Quaternion::LookRotation(sunDirection));
    sun.AddComponent<Spot::DirectionalLight>(Spot::Color::White, 3.0f);
    scene.CreateEntity("Sky").AddComponent<Spot::Sky>();
}

void AddMesh(Spot::Scene& scene, const Spot::AssetRef<Spot::Mesh>& mesh, const Spot::Material& material,
             Spot::Vector3 position, Spot::Vector3 scale = {1.0f, 1.0f, 1.0f}) {
    auto& entity = scene.CreateEntity("Mesh");
    entity.GetTransform().SetPosition(position);
    entity.GetTransform().SetScale(scale);
    entity.AddComponent<Spot::MeshRenderer>(mesh, material);
}

// Places each part of the model, and the camera in front of it, along -Z, far enough to see all of it.
void AddModel(Spot::GraphicsDevice& device, Spot::Scene& scene, const std::filesystem::path& path,
              Spot::Vector3 sunDirection) {
    const auto data = Spot::ModelData::Load(path);
    REQUIRE(data);
    const std::shared_ptr<Spot::Model> model = Spot::Model::Create(device, *data);
    REQUIRE(model);
    Spot::Vector3 min{1e9f, 1e9f, 1e9f};
    Spot::Vector3 max{-1e9f, -1e9f, -1e9f};
    // Around every vertex where its node puts it.
    for (std::size_t node = 0; node < data->nodes.size(); ++node) {
        if (!data->nodes[node].mesh) {
            continue;
        }
        const Spot::Matrix4 world = data->GetWorldTransform(node);
        for (const auto& vertex : data->meshes[*data->nodes[node].mesh].mesh.vertices) {
            const Spot::Vector4 p =
                world * Spot::Vector4{vertex.position.x, vertex.position.y, vertex.position.z, 1.0f};
            min = {std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z)};
            max = {std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z)};
        }
    }
    scene.Instantiate(*model, "Model");
    // The view is 40 degrees high and a third wider than high.
    const Spot::Vector3 center = (min + max) * 0.5f;
    const float halfHeight = std::max((max.y - min.y) * 0.5f, (max.x - min.x) * 0.5f * 0.75f) * 1.1f;
    const float distance = halfHeight / std::tan(Spot::ToRadians(20.0f)) + (max.z - min.z) * 0.5f;
    AddCameraAndSky(scene, center + Spot::Vector3{0.0f, 0.0f, distance}, center, sunDirection);
}

std::filesystem::path GetSampleModel(std::string_view name) {
    return std::filesystem::path(SPOT_SAMPLE_MODELS_DIR) / name;
}

} // namespace

TEST_CASE("Golden: spheres from dielectric to metal and from smooth to rough, under the sky", "[gpu][golden]") {
    RequireGpu();
    const auto render = RenderScene([](Spot::GraphicsDevice& device, Spot::Scene& scene) {
        const Spot::AssetRef<Spot::Mesh> sphere = Spot::Mesh::Create(device, Spot::MeshData::CreateSphere(0.4f));
        const Spot::AssetRef<Spot::Mesh> floor = Spot::Mesh::Create(device, Spot::MeshData::CreatePlane(20.0f));
        REQUIRE((sphere && floor));
        for (int row = 0; row < 2; ++row) {
            for (int column = 0; column < 5; ++column) {
                AddMesh(scene, sphere,
                        {.baseColor = row == 0 ? Spot::Color{0.8f, 0.1f, 0.1f} : Spot::Color{0.95f, 0.75f, 0.35f},
                         .metallic = static_cast<float>(row),
                         .roughness = static_cast<float>(column) / 4.0f},
                        {static_cast<float>(column) - 2.0f, 0.5f + static_cast<float>(1 - row), 0.0f});
            }
        }
        AddMesh(scene, floor, {.baseColor = {0.5f, 0.5f, 0.5f}}, {0.0f, 0.0f, 0.0f});
        AddCameraAndSky(scene, {0.0f, 1.2f, 7.0f}, {0.0f, 1.0f, 0.0f}, {-0.5f, -0.7f, -0.5f});
    });
    REQUIRE(render);
    CheckGolden("pbr_spheres", *render);
}

TEST_CASE("Golden: cutout leaves, transparent panes and shadows", "[gpu][golden]") {
    RequireGpu();
    const auto render = RenderScene([](Spot::GraphicsDevice& device, Spot::Scene& scene) {
        const Spot::AssetRef<Spot::Mesh> cube = Spot::Mesh::Create(device, Spot::MeshData::CreateCube());
        const Spot::AssetRef<Spot::Mesh> floor = Spot::Mesh::Create(device, Spot::MeshData::CreatePlane(20.0f));
        // Discs on a transparent background, as a cutout texture of leaves would have.
        std::vector<std::uint8_t> discs(64 * 64 * 4);
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                const float dx = static_cast<float>(x % 16) - 7.5f;
                const float dy = static_cast<float>(y % 16) - 7.5f;
                const auto i = static_cast<std::size_t>((y * 64 + x) * 4);
                discs[i] = 60;
                discs[i + 1] = 170;
                discs[i + 2] = 50;
                discs[i + 3] = dx * dx + dy * dy < 36.0f ? 255 : 0;
            }
        }
        const Spot::AssetRef<Spot::Texture> leaves(device.CreateTexture(Spot::Image(64, 64, std::move(discs)), true));
        REQUIRE((cube && floor && leaves));
        AddMesh(scene, floor, {.baseColor = {0.6f, 0.6f, 0.6f}}, {0.0f, 0.0f, 0.0f});
        AddMesh(scene, cube, {.baseColor = {0.2f, 0.3f, 0.8f}, .roughness = 0.4f}, {-1.6f, 0.5f, 0.0f});
        AddMesh(scene, cube, {.baseColorTexture = leaves, .alphaMode = Spot::AlphaMode::Cutout, .doubleSided = true},
                {0.0f, 1.0f, -0.5f}, {1.6f, 1.6f, 0.02f});
        AddMesh(scene, cube,
                {.baseColor = {1.0f, 0.2f, 0.2f, 0.4f}, .roughness = 0.1f, .alphaMode = Spot::AlphaMode::Transparent},
                {1.2f, 0.8f, 0.8f}, {1.2f, 1.6f, 0.05f});
        AddMesh(scene, cube,
                {.baseColor = {0.2f, 0.9f, 1.0f, 0.4f}, .roughness = 0.1f, .alphaMode = Spot::AlphaMode::Transparent},
                {1.7f, 0.8f, 0.0f}, {1.2f, 1.6f, 0.05f});
        AddCameraAndSky(scene, {0.5f, 2.5f, 6.0f}, {0.0f, 0.7f, 0.0f}, {0.6f, -0.8f, -0.4f});
    });
    REQUIRE(render);
    CheckGolden("alpha_and_shadows", *render);
}

TEST_CASE("Golden: a normal-mapped wall lit at a grazing angle", "[gpu][golden]") {
    RequireGpu();
    const auto render = RenderScene([](Spot::GraphicsDevice& device, Spot::Scene& scene) {
        const Spot::AssetRef<Spot::Mesh> plane = Spot::Mesh::Create(device, Spot::MeshData::CreatePlane(4.0f));
        // Rows of rounded bumps, as tangent-space normals with +Y toward the texture's top.
        std::vector<std::uint8_t> normals(64 * 64 * 4);
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                const float u = std::sin(static_cast<float>(x) / 64.0f * 8.0f * 3.14159265f);
                const float v = std::sin(static_cast<float>(y) / 64.0f * 4.0f * 3.14159265f);
                const Spot::Vector3 n = Spot::Normalize(Spot::Vector3{u * 0.6f, -v * 0.6f, 1.0f});
                const auto i = static_cast<std::size_t>((y * 64 + x) * 4);
                normals[i] = static_cast<std::uint8_t>((n.x * 0.5f + 0.5f) * 255.0f + 0.5f);
                normals[i + 1] = static_cast<std::uint8_t>((n.y * 0.5f + 0.5f) * 255.0f + 0.5f);
                normals[i + 2] = static_cast<std::uint8_t>((n.z * 0.5f + 0.5f) * 255.0f + 0.5f);
                normals[i + 3] = 255;
            }
        }
        const Spot::AssetRef<Spot::Texture> normalMap(
            device.CreateTexture(Spot::Image(64, 64, std::move(normals)), true, Spot::TextureFormat::Rgba8));
        REQUIRE((plane && normalMap));
        auto& wall = scene.CreateEntity("Wall");
        // The plane faces +Y; standing up, it faces the camera along +Z.
        wall.GetTransform().SetRotation(Spot::Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, Spot::ToRadians(90.0f)));
        wall.AddComponent<Spot::MeshRenderer>(
            plane, Spot::Material{.baseColor = {0.8f, 0.7f, 0.6f}, .roughness = 0.5f, .normalTexture = normalMap});
        AddCameraAndSky(scene, {0.0f, 0.0f, 6.0f}, {0.0f, 0.0f, 0.0f}, {0.9f, -0.3f, -0.3f});
    });
    REQUIRE(render);
    CheckGolden("normal_map", *render);
}

TEST_CASE("Golden: glTF sample models", "[gpu][golden]") {
    RequireGpu();
    const std::filesystem::path helmet = GetSampleModel("DamagedHelmet.glb");
    const std::filesystem::path alphaModes = GetSampleModel("AlphaBlendModeTest.glb");
    if (!std::filesystem::exists(helmet) || !std::filesystem::exists(alphaModes)) {
        SKIP("The glTF sample models were not downloaded (see SPOT_DOWNLOAD_SAMPLE_MODELS)");
    }

    SECTION("DamagedHelmet: every PBR texture, emissive and occlusion included") {
        const auto render = RenderScene([&](Spot::GraphicsDevice& device, Spot::Scene& scene) {
            AddModel(device, scene, helmet, {-0.4f, -0.6f, -0.7f});
        });
        REQUIRE(render);
        // Its fine, high-contrast detail is filtered a little differently by Vulkan and Direct3D 12 drivers, which
        // tells about 1.3% of the pixels apart; both pass.
        CheckGolden("damaged_helmet", *render, {.differingPixels = 0.02});
    }
    SECTION("AlphaBlendModeTest: opaque, cutout and blended alpha") {
        const auto render = RenderScene([&](Spot::GraphicsDevice& device, Spot::Scene& scene) {
            AddModel(device, scene, alphaModes, {-0.3f, -0.6f, -0.7f});
        });
        REQUIRE(render);
        CheckGolden("alpha_blend_mode_test", *render);
    }
}

TEST_CASE("Renderers draw scenes on the baseline as they do with every capability", "[gpu][golden][baseline]") {
    RequireGpu();
    const auto setup = [](Spot::GraphicsDevice& device, Spot::Scene& scene) {
        const Spot::AssetRef<Spot::Mesh> sphere = Spot::Mesh::Create(device, Spot::MeshData::CreateSphere(0.4f));
        const Spot::AssetRef<Spot::Mesh> floor = Spot::Mesh::Create(device, Spot::MeshData::CreatePlane(20.0f));
        REQUIRE((sphere && floor));
        for (int column = 0; column < 5; ++column) {
            AddMesh(scene, sphere, {.baseColor = {0.8f, 0.3f, 0.1f}, .roughness = static_cast<float>(column) / 4.0f},
                    {static_cast<float>(column) - 2.0f, 0.5f, 0.0f});
        }
        AddMesh(scene, sphere, {.baseColor = {0.2f, 0.6f, 0.9f, 0.5f}, .alphaMode = Spot::AlphaMode::Transparent},
                {0.0f, 1.3f, 1.0f});
        AddMesh(scene, floor, {.baseColor = {0.5f, 0.5f, 0.5f}}, {0.0f, 0.0f, 0.0f});
        auto& lamp = scene.CreateEntity("Lamp");
        lamp.GetTransform().SetPosition({1.0f, 1.5f, 1.5f});
        lamp.AddComponent<Spot::PointLight>(Spot::Color{1.0f, 0.8f, 0.6f}, 4.0f);
        AddCameraAndSky(scene, {0.0f, 1.5f, 6.0f}, {0.0f, 0.6f, 0.0f}, {-0.5f, -0.7f, -0.5f});
    };
    const auto full = RenderScene(setup);
    const auto baseline = RenderScene(setup, {.baselineOnly = true});
    REQUIRE((full && baseline));
    INFO("Compares the baseline's render with the full device's");
    CHECK(CompareImages("baseline", *full, *baseline, {}));
}
