#include <spot/render/atmosphere.h>
#include <spot/render/cloud_noise.h>

#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdlib>

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

TEST_CASE("Clouds drift with the wind, and the sky draws them", "[sky]") {
    Scene scene;
    auto& entity = scene.CreateEntity("Sky");
    const Sky& sky = entity.AddComponent<Sky>();
    CHECK(sky.GetSkyData().clouds.coverage == 0.0f);

    auto& clouds = entity.AddComponent<Spot::Clouds>();
    clouds.coverage = 0.6f;
    clouds.windDirection = {3.0f, 5.0f, 4.0f};
    clouds.windSpeed = 10.0f;
    scene.Update(0.5f);
    // Five meters along the wind on the ground, whatever its height.
    CHECK(clouds.GetOffset().x == Approx(3.0f).margin(1e-4));
    CHECK(clouds.GetOffset().y == Approx(4.0f).margin(1e-4));
    const Spot::SkyData data = sky.GetSkyData();
    CHECK(data.clouds.coverage == 0.6f);
    CHECK(data.clouds.offset == clouds.GetOffset());

    // Out of range settings reach the sky within it.
    clouds.coverage = 4.0f;
    clouds.size = -1.0f;
    CHECK(sky.GetSkyData().clouds.coverage == 1.0f);
    CHECK(sky.GetSkyData().clouds.size >= 1.0f);
    entity.GetComponent<Spot::Clouds>()->SetEnabled(false);
    CHECK(sky.GetSkyData().clouds.coverage == 0.0f);
}

TEST_CASE("The clouds' noise tiles, and spreads each channel evenly", "[sky]") {
    const auto noise = Spot::Detail::GetCloudNoise();
    constexpr int Size = static_cast<int>(Spot::Detail::CloudNoiseSize);
    REQUIRE(noise.size() == static_cast<std::size_t>(Size * Size * 4));
    // It is made once, and kept.
    CHECK(Spot::Detail::GetCloudNoise().data() == noise.data());

    const auto at = [&](int x, int y, int channel) {
        return static_cast<int>(noise[static_cast<std::size_t>(((y * Size) + x) * 4 + channel)]);
    };
    for (int channel = 0; channel < 4; ++channel) {
        CAPTURE(channel);
        // A coverage of c covers about the share c of it.
        std::array<int, 4> quarters{};
        for (int y = 0; y < Size; ++y) {
            for (int x = 0; x < Size; ++x) {
                ++quarters[static_cast<std::size_t>(at(x, y, channel) / 64)];
            }
        }
        for (const int quarter : quarters) {
            CHECK(std::abs(quarter - Size * Size / 4) < Size * Size / 50);
        }
        // Texels across its edges, which meet when it repeats, are as alike as neighbors inside it.
        int edges = 0;
        int inside = 0;
        for (int i = 0; i < Size; ++i) {
            edges += std::abs(at(Size - 1, i, channel) - at(0, i, channel)) +
                     std::abs(at(i, Size - 1, channel) - at(i, 0, channel));
            inside += std::abs(at(Size / 2 - 1, i, channel) - at(Size / 2, i, channel)) +
                      std::abs(at(i, Size / 2 - 1, channel) - at(i, Size / 2, channel));
        }
        CHECK(edges < inside * 2 + Size);
    }
}

TEST_CASE("The clouds' volume noise fills its cube, and spreads its channels evenly", "[sky]") {
    const auto noise = Spot::Detail::GetCloudVolumeNoise();
    constexpr auto Size = static_cast<std::size_t>(Spot::Detail::CloudVolumeSize);
    REQUIRE(noise.size() == Size * Size * Size * 4);
    for (std::size_t channel = 0; channel < 3; ++channel) {
        CAPTURE(channel);
        std::array<std::size_t, 4> quarters{};
        for (std::size_t i = 0; i < Size * Size * Size; ++i) {
            ++quarters[noise[i * 4 + channel] / 64];
        }
        for (const std::size_t quarter : quarters) {
            CHECK(quarter > Size * Size * Size / 5);
            CHECK(quarter < Size * Size * Size * 3 / 10);
        }
    }
}

TEST_CASE("Stars, the moon and clouds are saved with their scene", "[sky][serialization]") {
    Scene scene;
    auto& entity = scene.CreateEntity("Sky");
    auto& sky = entity.AddComponent<Sky>();
    sky.SetStarBrightness(2.0f);
    sky.SetStarDensity(0.25f);
    sky.SetStarRotation(1.0f);
    sky.SetMoonSize(0.05f);
    auto& clouds = entity.AddComponent<Spot::Clouds>();
    clouds.coverage = 0.3f;
    clouds.height = 2500.0f;
    clouds.SetOffset({10.0f, -20.0f});

    Scene loaded;
    REQUIRE(loaded.LoadFromString(scene.SaveToString()));
    const auto* copy = loaded.FindComponent<Sky>();
    const auto* loadedClouds = loaded.FindComponent<Spot::Clouds>();
    REQUIRE(copy);
    REQUIRE(loadedClouds);
    CHECK(copy->GetStarBrightness() == 2.0f);
    CHECK(copy->GetStarDensity() == 0.25f);
    CHECK(copy->GetStarRotation() == 1.0f);
    CHECK(copy->GetMoonSize() == 0.05f);
    CHECK(loadedClouds->coverage == 0.3f);
    CHECK(loadedClouds->height == 2500.0f);
    CHECK(loadedClouds->GetOffset() == Spot::Vector2{10.0f, -20.0f});
    CHECK(copy->GetSkyData().night.starRotation == 1.0f);
}
