// A game that runs its own loop and keeps its objects its own way, with neither Application nor Scene: only the
// layers below them, which is what a large game with an ECS of its own builds on.
#include <spot/core/geometry/mesh_data.h>
#include <spot/core/math/matrix.h>
#include <spot/graphics/graphics_device.h>
#include <spot/platform/event.h>
#include <spot/platform/input_state.h>
#include <spot/render/camera_3d.h>
#include <spot/render/light_data.h>
#include <spot/render/material.h>
#include <spot/render/mesh.h>
#include <spot/render/renderer_3d.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

// The game's own storage: plain arrays a system walks, rather than entities and components.
struct World {
    std::vector<Spot::Vector3> positions;
    std::vector<Spot::Color> colors;

    std::size_t Spawn(Spot::Vector3 position, Spot::Color color) {
        positions.push_back(position);
        colors.push_back(color);
        return positions.size() - 1;
    }
};

void MovePlayer(World& world, std::size_t player, const Spot::InputState& input, float deltaTime) {
    if (input.IsKeyDown(Spot::Key::D)) {
        world.positions[player].x += 2.0f * deltaTime;
    }
}

void DrawWorld(Spot::Renderer3D& renderer, const Spot::Mesh& cube, const World& world) {
    renderer.DrawLight({.type = Spot::LightType::Directional, .direction = {0.0f, 0.0f, -1.0f}});
    for (std::size_t i = 0; i < world.positions.size(); ++i) {
        renderer.DrawMesh(cube, {.baseColor = world.colors[i]}, Spot::Matrix4::Translation(world.positions[i]));
    }
}

} // namespace

TEST_CASE("A game loop of its own drives input, its own storage and the renderers without scenes", "[gpu][offscreen]") {
    if (!Spot::GraphicsDevice::IsSupported()) {
        SKIP("No GPU backend is available on this machine");
    }
    auto device = Spot::GraphicsDevice::CreateOffscreen({.debug = false});
    REQUIRE(device);
    constexpr std::uint32_t Size = 32;
    auto target = device->CreateTexture(
        {.width = Size, .height = Size, .format = Spot::TextureFormat::Rgba8, .renderTarget = true});
    auto depth = device->CreateTexture(
        {.width = Size, .height = Size, .format = Spot::TextureFormat::Depth32Float, .renderTarget = true});
    REQUIRE(target);
    REQUIRE(depth);
    Spot::Renderer3D renderer(*device, {.targetFormat = Spot::TextureFormat::Rgba8, .shadowMapSize = 0});
    REQUIRE(renderer.IsValid());
    const auto cube = Spot::Mesh::Create(*device, Spot::MeshData::CreateCube());
    REQUIRE(cube);

    World world;
    const std::size_t player = world.Spawn({-1.0f, 0.0f, 0.0f}, Spot::Color::Green);
    Spot::Camera3D camera;
    camera.position = {0.0f, 0.0f, 4.0f};
    camera.LookAt({0.0f, 0.0f, 0.0f});

    Spot::InputState input;
    constexpr float DeltaTime = 0.25f;
    std::vector<std::uint8_t> pixels;
    for (int frame = 0; frame < 4; ++frame) {
        // A platform loop would poll these; the game feeds its input state itself.
        input.BeginFrame();
        if (frame == 0) {
            input.OnEvent(Spot::KeyPressedEvent{.key = Spot::Key::D, .repeat = false});
        }
        MovePlayer(world, player, input, DeltaTime);

        Spot::RenderContext* context = device->BeginOffscreenFrame(*target);
        REQUIRE(context);
        renderer.Begin(*context, {.clearColor = Spot::Color::Black, .depthTarget = depth.get(), .clearDepth = 1.0f},
                       camera);
        DrawWorld(renderer, *cube, world);
        renderer.End();
        device->EndFrame();
    }
    pixels = device->DownloadTexture(*target);

    // Four frames of a quarter second at 2 units per second moved the cube from the left to the right.
    CHECK(world.positions[player].x == 1.0f);
    const auto green = [&](std::uint32_t x, std::uint32_t y) { return pixels[(y * Size + x) * 4 + 1]; };
    CHECK(green(Size / 2 + 7, Size / 2) > 100);
    CHECK(green(4, Size / 2) == 0);
}
