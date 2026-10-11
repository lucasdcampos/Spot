#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <spot.h>

using Catch::Approx;

TEST_CASE("Curves interpolate Hermite tangents and clamp endpoints", "[particles][curve]") {
    const auto linear = Spot::Curve::Linear(2.0f, 6.0f);
    CHECK(linear.Evaluate(0.25f) == Approx(3.0f));
    CHECK(linear.Evaluate(-1.0f) == 2.0f);
    CHECK(linear.Evaluate(2.0f) == 6.0f);
    const Spot::Curve hermite({{2.0f, 0.0f, 0.0f, 2.0f}, {4.0f, 0.0f, -2.0f, 0.0f}});
    CHECK(hermite.Evaluate(3.0f) == Approx(1.0f));
    CHECK(Spot::Curve().Evaluate(0.4f) == 1.0f);
}
TEST_CASE("Curve and gradient keys normalize invalid and duplicate input", "[particles][curve]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const Spot::Curve curve({{1, 2}, {0, 3}, {1, 4}, {nan, 5}});
    REQUIRE(curve.GetKeys().size() == 2);
    CHECK(curve.Evaluate(1) == 4);
    CHECK(curve.Evaluate(nan) == 3);
    CHECK(Spot::Curve(std::vector<Spot::CurveKey>{}).Evaluate(0) == 1);
    const Spot::Gradient gradient(
        {{1, Spot::Color::Red}, {0, {4, 2, 0, 0}}, {1, Spot::Color::Blue}, {nan, Spot::Color::White}});
    REQUIRE(gradient.GetKeys().size() == 2);
    const auto middle = gradient.Evaluate(0.5f);
    CHECK(middle.r == Approx(2));
    CHECK(middle.b == Approx(0.5f));
    CHECK(middle.a == Approx(0.5f));
    CHECK(gradient.Evaluate(2) == Spot::Color::Blue);
    CHECK(Spot::Gradient(std::vector<Spot::GradientKey>{}).Evaluate(0) == Spot::Color::White);
}
namespace {
class CurveOwner final : public Spot::Component {
public:
    Spot::Curve curve = Spot::Curve::Linear(3, 7);
    Spot::Gradient gradient{{{0, Spot::Color::Red}, {1, Spot::Color::Transparent}}};
    SPOT_FIELDS(curve, gradient)
};
} // namespace
TEST_CASE("Curves and HDR gradients round trip with scene fields", "[particles][curve]") {
    Spot::RegisterComponent<CurveOwner>("ParticleTestCurveOwner");
    Spot::Scene scene;
    scene.CreateEntity().AddComponent<CurveOwner>();
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(scene.SaveToString()));
    const auto* owner = loaded.FindComponent<CurveOwner>();
    REQUIRE(owner);
    CHECK(owner->curve.Evaluate(0.25f) == Approx(4));
    CHECK(owner->gradient.Evaluate(0.75f).a == Approx(0.25f));
    CHECK(owner->gradient.GetKeys().size() == 2);
    Spot::UnregisterComponent("ParticleTestCurveOwner");
}
