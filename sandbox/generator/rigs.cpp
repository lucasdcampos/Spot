#include "rigs.h"

#include "shapes.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Generator::Rigs {
namespace {
using namespace Spot;

void FinishSkeleton(Skeleton& skeleton) {
    std::vector<TransformComponents> local(skeleton.bones.size());
    std::vector<Matrix4> model(skeleton.bones.size());
    Pose::Bind(skeleton, local);
    Pose::ToModel(skeleton, local, model);
    for (std::size_t i = 0; i < model.size(); ++i) {
        skeleton.joints.push_back(static_cast<std::uint32_t>(i));
        skeleton.inverseBindMatrices.push_back(Inverse(model[i]));
    }
}

std::vector<Matrix4> BindMatrices(const Skeleton& skeleton) {
    std::vector<TransformComponents> local(skeleton.bones.size());
    std::vector<Matrix4> model(skeleton.bones.size());
    Pose::Bind(skeleton, local);
    Pose::ToModel(skeleton, local, model);
    return model;
}

void Box(MeshData& target, Vector3 size, Vector3 center, std::uint32_t bone, const std::vector<Matrix4>& bind,
         Quaternion rotation = {}) {
    auto box = Shapes::BeveledBox(size, std::min({size.x, size.y, size.z}) * 0.15f, 1);
    const auto transform = bind[bone] * Matrix4::Translation(center) * Matrix4::Rotation(rotation);
    for (auto& vertex : box.vertices) {
        const auto position = transform * Vector4{vertex.position.x, vertex.position.y, vertex.position.z, 1};
        const auto normal = transform * Vector4{vertex.normal.x, vertex.normal.y, vertex.normal.z, 0};
        vertex.position = {position.x, position.y, position.z};
        vertex.normal = Normalize(Vector3{normal.x, normal.y, normal.z});
    }
    box.skin.resize(box.vertices.size());
    for (auto& vertex : box.skin) {
        vertex.joints[0] = bone;
    }
    target.AppendSubmesh(box);
}

AnimationChannel Translation(std::uint32_t bone, float duration, Vector3 start, Vector3 middle, Vector3 end) {
    return {bone,
            AnimationPath::Translation,
            AnimationInterpolation::Linear,
            {0, duration * 0.5f, duration},
            {{start.x, start.y, start.z, 0}, {middle.x, middle.y, middle.z, 0}, {end.x, end.y, end.z, 0}}};
}

AnimationChannel Swing(std::uint32_t bone, float duration, Vector3 axis, float amplitude) {
    AnimationChannel channel{bone, AnimationPath::Rotation, AnimationInterpolation::Linear, {}, {}};
    constexpr std::array Waves{0.0f, 1.0f, 0.0f, -1.0f, 0.0f};
    for (std::size_t i = 0; i < Waves.size(); ++i) {
        channel.times.push_back(duration * static_cast<float>(i) * 0.25f);
        const auto q = Quaternion::FromAxisAngle(axis, Waves[i] * amplitude);
        channel.values.push_back({q.x, q.y, q.z, q.w});
    }
    return channel;
}
} // namespace

GltfModel Humanoid() {
    GltfModel model;
    model.skeleton.name = "Humanoid";
    model.skeleton.bones = {
        {"Pelvis", -1, {}},
        {"Spine", 0, {{0, 0.16f, 0}}},
        {"Chest", 1, {{0, 0.20f, 0}}},
        {"Neck", 2, {{0, 0.20f, 0}}},
        {"Head", 3, {{0, 0.10f, 0}}},
        {"LeftShoulder", 2, {{-0.17f, 0.10f, 0}}},
        {"LeftUpperArm", 5, {{-0.06f, -0.035f, 0}}},
        {"LeftForearm", 6, {{0, -0.29f, 0}}},
        {"LeftHand", 7, {{0, -0.26f, 0}}},
        {"RightShoulder", 2, {{0.17f, 0.10f, 0}}},
        {"RightUpperArm", 9, {{0.06f, -0.035f, 0}}},
        {"RightForearm", 10, {{0, -0.29f, 0}}},
        {"RightHand", 11, {{0, -0.26f, 0}}},
        {"LeftUpperLeg", 0, {{-0.105f, -0.07f, 0}}},
        {"LeftLowerLeg", 13, {{0, -0.41f, 0}}},
        {"LeftFoot", 14, {{0, -0.39f, 0}}},
        {"LeftToes", 15, {{0, -0.025f, -0.12f}}},
        {"RightUpperLeg", 0, {{0.105f, -0.07f, 0}}},
        {"RightLowerLeg", 17, {{0, -0.41f, 0}}},
        {"RightFoot", 18, {{0, -0.39f, 0}}},
        {"RightToes", 19, {{0, -0.025f, -0.12f}}},
    };
    FinishSkeleton(model.skeleton);
    const auto bind = BindMatrices(model.skeleton);
    model.materials = {{.name = "Shirt", .baseColor = {0.12f, 0.32f, 0.53f, 1}, .roughness = 0.9f},
                       {.name = "Trousers", .baseColor = {0.12f, 0.15f, 0.19f, 1}, .roughness = 0.95f},
                       {.name = "Skin", .baseColor = {0.68f, 0.44f, 0.29f, 1}, .roughness = 0.85f},
                       {.name = "Details", .baseColor = {0.035f, 0.028f, 0.025f, 1}, .roughness = 0.9f}};
    model.parts = {{"Shirt", {}, 0}, {"Trousers", {}, 1}, {"Skin", {}, 2}, {"Details", {}, 3}};
    auto& shirt = model.parts[0].mesh;
    auto& trousers = model.parts[1].mesh;
    auto& skin = model.parts[2].mesh;
    auto& details = model.parts[3].mesh;
    Box(shirt, {0.30f, 0.23f, 0.21f}, {0, 0.04f, 0}, 1, bind);
    Box(shirt, {0.38f, 0.25f, 0.23f}, {0, 0.04f, 0}, 2, bind);
    Box(trousers, {0.31f, 0.17f, 0.22f}, {0, -0.015f, 0}, 0, bind);
    Box(skin, {0.10f, 0.15f, 0.105f}, {0, 0.035f, 0}, 3, bind);
    Box(skin, {0.18f, 0.235f, 0.19f}, {0, 0.105f, 0}, 4, bind);
    Box(skin, {0.038f, 0.045f, 0.035f}, {0, 0.085f, -0.102f}, 4, bind);
    Box(details, {0.19f, 0.065f, 0.20f}, {0, 0.21f, 0.006f}, 4, bind);
    for (const float side : {-1.0f, 1.0f}) {
        Box(details, {0.023f, 0.013f, 0.009f}, {side * 0.042f, 0.125f, -0.095f}, 4, bind);
        Box(skin, {0.025f, 0.065f, 0.045f}, {side * 0.095f, 0.105f, 0}, 4, bind);
    }
    for (const auto bone : {6u, 10u}) {
        Box(shirt, {0.135f, 0.16f, 0.145f}, {0, -0.06f, 0}, bone, bind);
        Box(skin, {0.105f, 0.17f, 0.115f}, {0, -0.205f, 0}, bone, bind);
    }
    for (const auto bone : {7u, 11u}) {
        Box(skin, {0.09f, 0.25f, 0.10f}, {0, -0.125f, 0}, bone, bind);
    }
    for (const auto bone : {8u, 12u}) {
        Box(skin, {0.08f, 0.135f, 0.055f}, {0, -0.055f, -0.01f}, bone, bind);
        const float side = bone == 8 ? 1.0f : -1.0f;
        Box(skin, {0.03f, 0.065f, 0.04f}, {side * 0.045f, -0.03f, -0.025f}, bone, bind);
    }
    for (const auto bone : {13u, 17u}) {
        Box(trousers, {0.145f, 0.40f, 0.17f}, {0, -0.195f, 0}, bone, bind);
    }
    for (const auto bone : {14u, 18u}) {
        Box(trousers, {0.115f, 0.38f, 0.135f}, {0, -0.185f, 0}, bone, bind);
    }
    for (const auto bone : {15u, 19u}) {
        Box(details, {0.13f, 0.105f, 0.21f}, {0, -0.02f, -0.035f}, bone, bind);
    }
    for (const auto bone : {16u, 20u}) {
        Box(details, {0.13f, 0.08f, 0.09f}, {0, 0, -0.025f}, bone, bind);
    }
    AnimationClip idle{"Idle", 2, {Translation(0, 2, {}, {}, {})}};
    idle.channels.push_back({1,
                             AnimationPath::Scale,
                             AnimationInterpolation::Linear,
                             {0, 1, 2},
                             {{1, 1, 1, 0}, {1, 1.008f, 1, 0}, {1, 1, 1, 0}}});
    model.animations.push_back(std::move(idle));
    for (const bool run : {false, true}) {
        const float duration = run ? 0.6f : 1.0f, distance = run ? 1.6f : 0.85f;
        AnimationClip clip{run ? "Run" : "Walk",
                           duration,
                           {Translation(0, duration, {}, {0, 0, -distance * 0.5f}, {0, 0, -distance})},
                           {{duration * 0.25f, "Footstep", "left"}, {duration * 0.75f, "Footstep", "right"}}};
        const float legs = run ? 0.9f : 0.55f, arms = run ? 0.6f : 0.35f;
        clip.channels.push_back(Swing(13, duration, {1, 0, 0}, legs));
        clip.channels.push_back(Swing(17, duration, {1, 0, 0}, -legs));
        clip.channels.push_back(Swing(6, duration, {1, 0, 0}, -arms));
        clip.channels.push_back(Swing(10, duration, {1, 0, 0}, arms));
        for (const auto bone : {14u, 18u, 7u, 11u}) {
            auto bend = Swing(bone, duration, {1, 0, 0}, 1);
            for (std::size_t i = 0; i < bend.values.size(); ++i) {
                const float wave = std::sin(static_cast<float>(i) * Pi * 0.5f + (bone == 18 ? Pi : 0));
                const bool leg = bone == 14 || bone == 18;
                const float angle = leg ? -std::max(wave, 0.0f) * (run ? 1.1f : 0.65f) : 0.15f;
                const auto q = Quaternion::FromAxisAngle({1, 0, 0}, angle);
                bend.values[i] = {q.x, q.y, q.z, q.w};
            }
            clip.channels.push_back(std::move(bend));
        }
        clip.channels.push_back(Swing(1, duration, {0, 1, 0}, 0.06f));
        model.animations.push_back(std::move(clip));
    }
    AnimationClip hold{"HoldPistol", 1, {}};
    for (const auto bone : {10u, 11u, 12u}) {
        const float angle = bone == 10 ? 1.05f : bone == 11 ? Pi * 0.5f - 1.05f : -Pi * 0.5f;
        const auto q = Quaternion::FromAxisAngle({1, 0, 0}, angle);
        hold.channels.push_back({bone,
                                 AnimationPath::Rotation,
                                 AnimationInterpolation::Linear,
                                 {0, 1},
                                 {{q.x, q.y, q.z, q.w}, {q.x, q.y, q.z, q.w}}});
    }
    model.animations.push_back(std::move(hold));
    // Physics moves the player; these clips keep the authored locomotion in place.
    for (const std::size_t index : {1u, 2u}) {
        auto clip = model.animations[index];
        clip.name += "InPlace";
        for (auto& channel : clip.channels) {
            if (channel.bone == 0 && channel.path == AnimationPath::Translation) {
                for (auto& value : channel.values) {
                    value = {};
                }
            }
        }
        model.animations.push_back(std::move(clip));
    }
    const auto rotate = [](AnimationClip& clip, std::uint32_t bone, float angle) {
        const auto q = Quaternion::FromAxisAngle({1, 0, 0}, angle);
        clip.channels.push_back({bone,
                                 AnimationPath::Rotation,
                                 AnimationInterpolation::Linear,
                                 {0, 1},
                                 {{q.x, q.y, q.z, q.w}, {q.x, q.y, q.z, q.w}}});
    };
    AnimationClip seated{"Seated", 1, {}};
    for (const auto bone : {13u, 17u}) {
        rotate(seated, bone, Pi * 0.5f);
    }
    for (const auto bone : {14u, 18u}) {
        rotate(seated, bone, -Pi * 0.5f);
    }
    for (const auto bone : {6u, 10u, 7u, 11u}) {
        rotate(seated, bone, Pi * 0.25f);
    }
    for (const auto bone : {8u, 12u}) {
        rotate(seated, bone, -Pi * 0.5f);
    }
    model.animations.push_back(std::move(seated));
    AnimationClip crouch{"Crouch", 1, {Translation(0, 1, {0, -0.36f, 0}, {0, -0.36f, 0}, {0, -0.36f, 0})}};
    rotate(crouch, 1, 0.3f);
    for (const auto bone : {13u, 17u}) {
        rotate(crouch, bone, 1.3f);
    }
    for (const auto bone : {14u, 18u}) {
        rotate(crouch, bone, -1.9f);
    }
    for (const auto bone : {15u, 19u}) {
        rotate(crouch, bone, 0.6f);
    }
    model.animations.push_back(std::move(crouch));
    AnimationClip compact{"SeatedCompact", 1, {}};
    for (const auto bone : {13u, 17u}) {
        rotate(compact, bone, 2.2f);
    }
    for (const auto bone : {14u, 18u}) {
        rotate(compact, bone, -1.0f);
    }
    for (const auto bone : {15u, 19u}) {
        rotate(compact, bone, -1.2f);
    }
    model.animations.push_back(std::move(compact));
    return model;
}

} // namespace Generator::Rigs
