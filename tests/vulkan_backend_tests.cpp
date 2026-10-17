// The Vulkan backend, through NVRHI, on what it runs so far. The rest of the GPU tests run on it with
// SPOT_GRAPHICS_BACKEND=vulkan once it draws.
#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

std::unique_ptr<Spot::GraphicsDevice> CreateVulkanDevice() {
    if (!Spot::GraphicsDevice::IsSupported()) {
        return nullptr;
    }
    return Spot::GraphicsDevice::CreateOffscreen({.backend = Spot::GraphicsBackend::Vulkan});
}

bool AllPixelsAre(const std::vector<std::uint8_t>& pixels, std::array<std::uint8_t, 4> color) {
    if (pixels.empty() || pixels.size() % 4 != 0) {
        return false;
    }
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        if (!std::equal(color.begin(), color.end(), pixels.begin() + static_cast<std::ptrdiff_t>(i))) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("The Vulkan backend clears render targets and reads them back", "[gpu][vulkan]") {
    auto device = CreateVulkanDevice();
    if (!device) {
        SKIP("Vulkan 1.3 is not available on this machine");
    }
    CHECK(device->GetBackendName().find("vulkan") != std::string_view::npos);
    auto target =
        device->CreateTexture({.width = 8, .height = 4, .format = Spot::TextureFormat::Rgba8, .renderTarget = true});
    REQUIRE(target);
    for (const bool red : {true, false}) {
        Spot::RenderContext* context = device->BeginOffscreenFrame(*target);
        REQUIRE(context);
        CHECK(context->GetWidth() == 8);
        context->BeginRenderPass({.clearColor = red ? Spot::Color::Red : Spot::Color::Blue}).End();
        device->EndFrame();
        const auto expected =
            red ? std::array<std::uint8_t, 4>{255, 0, 0, 255} : std::array<std::uint8_t, 4>{0, 0, 255, 255};
        CHECK(AllPixelsAre(device->DownloadTexture(*target), expected));
    }
}

TEST_CASE("The Vulkan backend uploads texture pixels and downloads them unchanged", "[gpu][vulkan]") {
    auto device = CreateVulkanDevice();
    if (!device) {
        SKIP("Vulkan 1.3 is not available on this machine");
    }
    const std::vector<std::uint8_t> pixels{10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 100, 110, 120, 255};
    auto texture = device->CreateTexture({.width = 2, .height = 2, .format = Spot::TextureFormat::Rgba8});
    REQUIRE(texture);
    device->UploadToTexture(*texture, pixels);
    CHECK(device->DownloadTexture(*texture) == pixels);

    // The six faces of a cube, one after the other.
    std::vector<std::uint8_t> faces(4 * 6);
    for (std::size_t face = 0; face < 6; ++face) {
        faces[face * 4] = static_cast<std::uint8_t>(face * 40);
        faces[face * 4 + 3] = 255;
    }
    auto cube = device->CreateTexture(
        {.width = 1, .height = 1, .format = Spot::TextureFormat::Rgba8, .type = Spot::TextureType::Cube});
    REQUIRE(cube);
    device->UploadToTexture(*cube, faces);
    CHECK(device->DownloadTexture(*cube) == faces);
}

TEST_CASE("The Vulkan backend resolves multisampled targets", "[gpu][vulkan]") {
    auto device = CreateVulkanDevice();
    if (!device) {
        SKIP("Vulkan 1.3 is not available on this machine");
    }
    REQUIRE(device->SupportsSampleCount(Spot::TextureFormat::Rgba8, 4));
    auto samples = device->CreateTexture(
        {.width = 4, .height = 4, .format = Spot::TextureFormat::Rgba8, .renderTarget = true, .sampleCount = 4});
    auto resolved =
        device->CreateTexture({.width = 4, .height = 4, .format = Spot::TextureFormat::Rgba8, .renderTarget = true});
    REQUIRE(samples);
    REQUIRE(resolved);
    Spot::RenderContext* context = device->BeginOffscreenFrame(*resolved);
    REQUIRE(context);
    context
        ->BeginRenderPass({.target = samples.get(), .clearColor = Spot::Color::Green, .resolveTarget = resolved.get()})
        .End();
    device->EndFrame();
    CHECK(AllPixelsAre(device->DownloadTexture(*resolved), {0, 255, 0, 255}));
}
