#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using Spot::Color;
using Spot::DirectionalLight;
using Spot::Entity;
using Spot::Quaternion;
using Spot::Scene;
using Spot::Sky;
using Spot::Vector3;

TEST_CASE("Skies take their sun from the brightest active directional light", "[sky]") {
    Scene scene;
    const Sky& sky = scene.CreateEntity("Sky").AddComponent<Sky>();
    CHECK_FALSE(sky.GetSkyData().hasSun);

    scene.CreateEntity("Dim").AddComponent<DirectionalLight>(Color::White, 0.5f);
    auto& sun = scene.CreateEntity("Sun");
    sun.GetTransform().SetRotation(Quaternion::LookRotation({1.0f, -1.0f, 0.0f}));
    sun.AddComponent<DirectionalLight>(Color::Red, 2.0f);
    auto& inactive = scene.CreateEntity("Inactive");
    inactive.AddComponent<DirectionalLight>(Color::White, 10.0f);
    inactive.SetActive(false);

    const Spot::SkyData data = sky.GetSkyData();
    REQUIRE(data.hasSun);
    CHECK(data.sunColor == Color::Red);
    CHECK(data.sunIntensity == 2.0f);
    const Vector3 expected = Spot::Normalize(Vector3{1.0f, -1.0f, 0.0f});
    CHECK(data.sunDirection.x == Approx(expected.x).margin(1e-5));
    CHECK(data.sunDirection.y == Approx(expected.y).margin(1e-5));
    CHECK(data.sunDirection.z == Approx(expected.z).margin(1e-5));
}

TEST_CASE("Skies are saved with their scene", "[sky][serialization]") {
    Scene scene;
    auto& sky = scene.CreateEntity("Sky").AddComponent<Sky>();
    sky.SetSunSize(0.1f);
    sky.SetAtmosphereThickness(2.0f);
    sky.SetSkyTint(Color::Green);
    sky.SetGroundColor(Color::Red);
    sky.SetNightColor(Color::Blue);
    sky.SetExposure(0.5f);

    Scene loaded;
    REQUIRE(loaded.LoadFromString(scene.SaveToString()));
    const Entity* copy = loaded.FindEntity("Sky");
    REQUIRE(copy);
    const auto* loadedSky = copy->GetComponent<Sky>();
    REQUIRE(loadedSky);
    CHECK(loadedSky->GetSunSize() == 0.1f);
    CHECK(loadedSky->GetAtmosphereThickness() == 2.0f);
    CHECK(loadedSky->GetSkyTint() == Color::Green);
    CHECK(loadedSky->GetGroundColor() == Color::Red);
    CHECK(loadedSky->GetNightColor() == Color::Blue);
    CHECK(loadedSky->GetExposure() == 0.5f);
}
