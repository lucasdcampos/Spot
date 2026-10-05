#include "graphics/atmosphere.h"

#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

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

    REQUIRE(sky.FindSun());
    CHECK(&sky.FindSun()->GetEntity() == &sun);
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
    sky.SetAmbientIntensity(0.25f);

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
    CHECK(loadedSky->GetAmbientIntensity() == 0.25f);
}

TEST_CASE("Sunlight crosses more air and reddens as the sun nears the horizon", "[sky]") {
    const Spot::Detail::Atmosphere atmosphere;
    const auto elevated = [](float degrees) {
        const float angle = Spot::ToRadians(degrees);
        return Vector3{0.0f, std::sin(angle), -std::cos(angle)};
    };
    const float sunSize = Spot::ToRadians(0.5f);

    const Color noon = atmosphere.GetSunTransmittance(elevated(90.0f), sunSize);
    CHECK(noon.r > 0.9f);
    CHECK(noon.b > 0.7f);
    const Color sunset = atmosphere.GetSunTransmittance(elevated(2.0f), sunSize);
    CHECK(sunset.r < noon.r);
    CHECK(sunset.r > sunset.b * 3.0f);

    // The disc sets gradually, then the planet hides it.
    const Color setting = atmosphere.GetSunTransmittance(elevated(0.0f), sunSize);
    CHECK(setting.r > 0.0f);
    CHECK(setting.r < atmosphere.GetSunTransmittance(elevated(1.0f), sunSize).r);
    CHECK(atmosphere.GetSunTransmittance(elevated(-2.0f), sunSize) == Color{0.0f, 0.0f, 0.0f});

    // A thicker atmosphere takes more of it.
    const Spot::Detail::Atmosphere thick{.thickness = 3.0f};
    CHECK(thick.GetSunTransmittance(elevated(30.0f), sunSize).b <
          atmosphere.GetSunTransmittance(elevated(30.0f), sunSize).b);
}

TEST_CASE("The sky casts blue light by day and next to none at night", "[sky]") {
    const Spot::Detail::Atmosphere atmosphere;
    const Color day = atmosphere.GetSkyIrradiance({0.3f, 1.0f, 0.2f});
    CHECK(day.b > day.r * 2.0f);
    CHECK(day.b > 0.01f);
    CHECK(day.b < 1.0f);

    const Color night = atmosphere.GetSkyIrradiance({0.3f, -1.0f, 0.2f});
    CHECK(night.b < day.b * 0.01f);

    // The sky above the sun is brighter than the sky across from it, for the haze scatters light forward.
    const Vector3 toSun = {0.0f, 0.5f, -1.0f};
    CHECK(atmosphere.GetSkyRadiance(toSun, toSun).r > atmosphere.GetSkyRadiance({0.0f, 0.5f, 1.0f}, toSun).r);
}
