#include "animation/pose.h"
#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace Spot;

TEST_CASE("Pose sampling restores bind fields and interpolates glTF channels", "[animation][pose]") {
    Skeleton skeleton{
        "Rig", {{"Root", -1, {}}, {"Child", 0, {{0, 1, 0}}}}, {0, 1}, {Matrix4{}, Matrix4::Translation({0, -1, 0})}};
    AnimationClip clip{
        "Move",
        2,
        {{1, AnimationPath::Translation, AnimationInterpolation::Linear, {0, 2}, {{0, 1, 0, 0}, {4, 1, 0, 0}}}}};
    std::vector<TransformComponents> local(2);
    Pose::Sample(skeleton, clip, 1, false, local);
    CHECK(local[1].translation.x == Catch::Approx(2));
    CHECK(local[1].scale == Vector3{1, 1, 1});
    Pose::Sample(skeleton, clip, 3, true, local);
    CHECK(local[1].translation.x == Catch::Approx(2));
    Pose::Sample(skeleton, clip, -1, true, local);
    CHECK(local[1].translation.x == Catch::Approx(2));
    Pose::Sample(skeleton, clip, 3, false, local);
    CHECK(local[1].translation.x == 4);
    clip.channels[0].interpolation = AnimationInterpolation::Step;
    Pose::Sample(skeleton, clip, 1, false, local);
    CHECK(local[1].translation.x == 0);
    Pose::Sample(skeleton, clip, 2, false, local);
    CHECK(local[1].translation.x == 4);
}

TEST_CASE("Cubic animation tangents use segment duration and quaternion samples normalize", "[animation][pose]") {
    AnimationChannel cubic{
        0, AnimationPath::Translation, AnimationInterpolation::Cubic, {0, 2}, {{}, {}, {2, 0, 0, 0}, {}, {}, {}}};
    CHECK(Pose::SampleChannel(cubic, 1).x == Catch::Approx(0.5f));
    AnimationChannel rotation{
        0, AnimationPath::Rotation, AnimationInterpolation::Linear, {0, 1}, {{0, 0, 0, 1}, {0, 0, 0, -1}}};
    CHECK(std::abs(Pose::SampleChannel(rotation, 0.5f).w) == Catch::Approx(1));
    rotation.interpolation = AnimationInterpolation::Cubic;
    rotation.values = {{}, {0, 0, 0, 1}, {}, {}, {0, 0, 1, 0}, {}};
    const auto q = Pose::SampleChannel(rotation, 0.5f);
    CHECK(q.z * q.z + q.w * q.w == Catch::Approx(1));
}

TEST_CASE("Pose blending and palettes preserve hierarchy and mesh coordinates", "[animation][pose]") {
    Skeleton skeleton{"Rig", {{"Root", -1, {}}, {"Child", 0, {{0, 1, 0}}}}, {1}, {Matrix4::Translation({0, -1, 0})}};
    std::vector<TransformComponents> a(2), b(2), blended(2);
    Pose::Bind(skeleton, a);
    b = a;
    b[0].translation.x = 4;
    Pose::Blend(a, b, 0.25f, blended);
    std::vector<Matrix4> model(2), palette(1);
    Pose::ToModel(skeleton, blended, model);
    CHECK(model[1].columns[3].x == Catch::Approx(1));
    CHECK(model[1].columns[3].y == Catch::Approx(1));
    Pose::Palette(skeleton, model, Matrix4::Translation({1, 0, 0}), palette);
    CHECK(palette[0] == Matrix4::Identity());
    Pose::Blend(a, b, 2, blended);
    CHECK(blended[0].translation.x == 4);
    CHECK(Pose::WrapTime(1, 0, true) == 0);
}
