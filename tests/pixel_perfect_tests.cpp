#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <spot.h>

using namespace Spot;
using Catch::Approx;
TEST_CASE("Pixel-perfect cameras use integer viewport scales and align odd viewport edges",
          "[pixelperfect][renderer2d]") {
    PixelPerfectSettings settings{.enabled = true, .pixelsPerUnit = 16, .referenceResolution = {320, 180}};
    CHECK(settings.GetZoom({640, 360}) == 32);
    CHECK(settings.GetZoom({1000, 700}) == 48);
    CHECK(settings.GetZoom({160, 90}) == 16);
    settings.pixelsPerUnit = 1;
    settings.referenceResolution = {32, 24};
    CHECK(settings.SnapCamera({0.1f, 0.1f}, {64, 48}) == Vector2{});
    CHECK(settings.SnapCamera({0.1f, 0.1f}, {65, 49}) == Vector2{0.25f, 0.25f});
    CHECK(settings.SnapQuad({0.1f, 0.1f}, {4, 4}) == Vector2{});
    CHECK(settings.SnapQuad({0.1f, 0.1f}, {3, 3}) == Vector2{0.5f, 0.5f});
    settings.pixelsPerUnit = 0;
    CHECK_FALSE(settings.IsValid());
    CHECK(settings.GetZoom({64, 48}) == 1);
    settings.pixelsPerUnit = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(settings.IsValid());
}
TEST_CASE("Pixel-perfect Camera2D projection and picking agree at every viewport size", "[pixelperfect][renderer2d]") {
    Camera2D camera;
    camera.position = {0.1f, 0.1f};
    camera.pixelPerfect = {.enabled = true, .pixelsPerUnit = 1, .referenceResolution = {32, 24}};
    for (Vector2 size : {Vector2{64, 48}, Vector2{65, 49}, Vector2{96, 72}, Vector2{16, 12}}) {
        const auto projection = camera.GetViewProjection(size);
        const auto minimum = camera.ScreenToWorld({0, size.y}, size);
        CHECK(minimum.x == Approx(std::round(minimum.x)).margin(0.00001f));
        CHECK(minimum.y == Approx(std::round(minimum.y)).margin(0.00001f));
        for (Vector2 screen : {Vector2{0, 0}, size, Vector2{17, 11}}) {
            const auto world = camera.ScreenToWorld(screen, size);
            const auto clip = projection * Vector4{world.x, world.y, 0, 1};
            CHECK((clip.x + 1) * size.x * 0.5f == Approx(screen.x).margin(0.0001f));
            CHECK((1 - clip.y) * size.y * 0.5f == Approx(screen.y).margin(0.0001f));
        }
    }
}
TEST_CASE("Orthographic scene cameras expose pixel art settings without editing their transforms", "[pixelperfect]") {
    Scene scene;
    auto& entity = scene.CreateEntity("Camera");
    entity.GetTransform().SetPosition({0.1f, 0.1f, 3});
    auto& camera = entity.AddComponent<Camera>();
    camera.SetProjection(CameraProjection::Orthographic);
    camera.SetPixelPerfect(true);
    camera.SetPixelsPerUnit(1);
    camera.SetReferenceResolution({32, 24});
    const auto view = camera.GetCamera3D();
    const auto projected = view.GetViewProjection({64, 48});
    CHECK(entity.GetTransform().GetPosition() == Vector3{0.1f, 0.1f, 3});
    const auto clip = projected * Vector4{0, 0, 0, 1};
    CHECK(clip.x == Approx(0).margin(0.00001f));
    CHECK(clip.y == Approx(0).margin(0.00001f));
    Scene restored;
    REQUIRE(restored.LoadFromString(scene.SaveToString()));
    auto* copy = restored.FindEntity("Camera")->GetComponent<Camera>();
    REQUIRE(copy);
    CHECK(copy->IsPixelPerfect());
    CHECK(copy->GetPixelsPerUnit() == 1);
    CHECK(copy->GetReferenceResolution() == Vector2{32, 24});
    auto perspective = view;
    perspective.orthographic = false;
    perspective.pixelPerfect.enabled = false;
    const auto regular = perspective.GetViewProjection({64, 48});
    perspective.pixelPerfect.enabled = true;
    CHECK(perspective.GetViewProjection({64, 48}) == regular);
}
