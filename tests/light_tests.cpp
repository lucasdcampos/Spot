#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using Spot::AmbientLight;
using Spot::Color;
using Spot::DirectionalLight;
using Spot::Entity;
using Spot::LightType;
using Spot::PointLight;
using Spot::Quaternion;
using Spot::Scene;
using Spot::SpotLight;
using Spot::Vector3;

namespace {

bool IsNear(Vector3 a, Vector3 b) {
    return a.x == Approx(b.x).margin(1e-5) && a.y == Approx(b.y).margin(1e-5) && a.z == Approx(b.z).margin(1e-5);
}

} // namespace

TEST_CASE("Lights take their color and intensity", "[light]") {
    Scene scene;
    auto& light = scene.CreateEntity().AddComponent<PointLight>(Color::Red, 2.0f, 5.0f);
    CHECK(light.GetColor() == Color::Red);
    CHECK(light.GetIntensity() == 2.0f);
    CHECK(light.GetRange() == 5.0f);

    light.SetColor(Color::Blue);
    light.SetIntensity(0.5f);
    light.SetRange(3.0f);
    const Spot::LightData data = light.GetLightData();
    CHECK(data.type == LightType::Point);
    CHECK(data.color == Color::Blue);
    CHECK(data.intensity == 0.5f);
    CHECK(data.range == 3.0f);
}

TEST_CASE("Lights are placed and aimed by their entity's world transform", "[light]") {
    Scene scene;
    auto& parent = scene.CreateEntity("Parent");
    parent.GetTransform().SetPosition({10.0f, 0.0f, 0.0f});
    parent.GetTransform().SetRotation(Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, Spot::Pi / 2.0f));
    Entity& child = parent.CreateChild("Child");
    child.GetTransform().SetPosition({0.0f, 2.0f, -1.0f});

    // Turning -Z a quarter turn counterclockwise around Y points it down -X, and moves the child's offset there too.
    const Spot::LightData spot = child.AddComponent<SpotLight>().GetLightData();
    CHECK(spot.type == LightType::Spot);
    CHECK(IsNear(spot.position, {9.0f, 2.0f, 0.0f}));
    CHECK(IsNear(spot.direction, {-1.0f, 0.0f, 0.0f}));

    const Spot::LightData directional = parent.AddComponent<DirectionalLight>().GetLightData();
    CHECK(directional.type == LightType::Directional);
    CHECK(IsNear(directional.direction, {-1.0f, 0.0f, 0.0f}));

    CHECK(child.AddComponent<AmbientLight>().GetLightData().type == LightType::Ambient);
}

TEST_CASE("Lights are saved with their scene", "[light][serialization]") {
    Scene scene;
    auto& entity = scene.CreateEntity("Lights");
    entity.AddComponent<AmbientLight>(Color::Blue, 0.5f);
    entity.AddComponent<DirectionalLight>(Color::Green, 2.0f);
    entity.AddComponent<PointLight>(Color::Red, 3.0f, 4.0f);
    auto& spot = entity.AddComponent<SpotLight>(Color::White, 5.0f, 6.0f);
    spot.SetInnerConeAngle(0.25f);
    spot.SetOuterConeAngle(0.5f);

    Scene loaded;
    REQUIRE(loaded.LoadFromString(scene.SaveToString()));
    const Entity* copy = loaded.FindEntity("Lights");
    REQUIRE(copy);

    const auto* ambient = copy->GetComponent<AmbientLight>();
    REQUIRE(ambient);
    CHECK(ambient->GetColor() == Color::Blue);
    CHECK(ambient->GetIntensity() == 0.5f);
    const auto* directional = copy->GetComponent<DirectionalLight>();
    REQUIRE(directional);
    CHECK(directional->GetColor() == Color::Green);
    CHECK(directional->GetIntensity() == 2.0f);
    const auto* point = copy->GetComponent<PointLight>();
    REQUIRE(point);
    CHECK(point->GetColor() == Color::Red);
    CHECK(point->GetIntensity() == 3.0f);
    CHECK(point->GetRange() == 4.0f);
    const auto* loadedSpot = copy->GetComponent<SpotLight>();
    REQUIRE(loadedSpot);
    CHECK(loadedSpot->GetIntensity() == 5.0f);
    CHECK(loadedSpot->GetRange() == 6.0f);
    CHECK(loadedSpot->GetInnerConeAngle() == 0.25f);
    CHECK(loadedSpot->GetOuterConeAngle() == 0.5f);
}
