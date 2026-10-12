#include "rigs.h"

#include "shapes.h"

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
    auto box = Shapes::Box(size, 1);
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

GltfModel Robot() {
    GltfModel model;
    model.skeleton.name = "RobotRig";
    model.skeleton.bones = {
        {"Pelvis", -1, {}},
        {"Spine", 0, {{0, 0.38f, 0}}},
        {"Head", 1, {{0, 0.45f, 0}}},
        {"LeftArm", 1, {{-0.45f, 0.10f, 0}}},
        {"LeftHand", 3, {{0, -0.45f, 0}}},
        {"RightArm", 1, {{0.45f, 0.10f, 0}}},
        {"RightHand", 5, {{0, -0.45f, 0}}},
        {"LeftLeg", 0, {{-0.2f, -0.2f, 0}}},
        {"LeftFoot", 7, {{0, -0.6f, -0.05f}}},
        {"RightLeg", 0, {{0.2f, -0.2f, 0}}},
        {"RightFoot", 9, {{0, -0.6f, -0.05f}}},
    };
    FinishSkeleton(model.skeleton);
    const auto bind = BindMatrices(model.skeleton);
    model.materials = {
        {.name = "Robot armor",
         .baseColor = {0.12f, 0.5f, 0.6f, 1},
         .baseColorImage = "../textures/painted_metal_color.png",
         .metallic = 0.65f,
         .roughness = 0.6f,
         .normalImage = "../textures/painted_metal_normal.png",
         .ormImage = "../textures/painted_metal_orm.png"},
        {.name = "Robot joints", .baseColor = {0.06f, 0.07f, 0.09f, 1}, .metallic = 0.7f, .roughness = 0.45f},
        {.name = "Robot eyes", .baseColor = {0.1f, 0.9f, 0.4f, 1}, .emissive = {0.1f, 2, 0.4f, 1}}};
    model.parts = {{"RobotBody", {}, 0}, {"RobotJoints", {}, 1}, {"RobotEyes", {}, 2}};
    auto& armor = model.parts[0].mesh;
    Box(armor, {0.45f, 0.3f, 0.3f}, {}, 0, bind);
    Box(armor, {0.6f, 0.48f, 0.34f}, {}, 1, bind);
    Box(armor, {0.4f, 0.36f, 0.34f}, {}, 2, bind);
    for (const auto bone : {3u, 5u}) {
        Box(armor, {0.18f, 0.36f, 0.18f}, {0, -0.18f, 0}, bone, bind);
    }
    for (const auto bone : {4u, 6u}) {
        Box(armor, {0.22f, 0.16f, 0.2f}, {}, bone, bind);
    }
    for (const auto bone : {7u, 9u}) {
        Box(armor, {0.2f, 0.45f, 0.22f}, {0, -0.2f, 0}, bone, bind);
    }
    for (const auto bone : {8u, 10u}) {
        Box(armor, {0.24f, 0.12f, 0.38f}, {0, 0, -0.08f}, bone, bind);
    }
    for (std::uint32_t bone = 0; bone < model.skeleton.bones.size(); ++bone) {
        Box(model.parts[1].mesh, {0.13f, 0.13f, 0.13f}, {}, bone, bind);
    }
    Box(model.parts[2].mesh, {0.08f, 0.045f, 0.025f}, {-0.1f, 0.035f, -0.177f}, 2, bind);
    Box(model.parts[2].mesh, {0.08f, 0.045f, 0.025f}, {0.1f, 0.035f, -0.177f}, 2, bind);
    AnimationClip idle{"Idle", 2, {Translation(0, 2, {}, {}, {})}};
    idle.channels.push_back({1,
                             AnimationPath::Scale,
                             AnimationInterpolation::Linear,
                             {0, 1, 2},
                             {{1, 1, 1, 0}, {1, 1.035f, 1, 0}, {1, 1, 1, 0}}});
    model.animations.push_back(std::move(idle));
    for (const bool run : {false, true}) {
        const float duration = run ? 0.6f : 1.0f, distance = run ? 1.6f : 0.85f;
        AnimationClip clip{run ? "Run" : "Walk",
                           duration,
                           {Translation(0, duration, {}, {0, 0, -distance * 0.5f}, {0, 0, -distance})},
                           {{duration * 0.25f, "Footstep", "left"}, {duration * 0.75f, "Footstep", "right"}}};
        const float legs = run ? 0.9f : 0.55f, arms = run ? 0.6f : 0.35f;
        clip.channels.push_back(Swing(7, duration, {1, 0, 0}, legs));
        clip.channels.push_back(Swing(9, duration, {1, 0, 0}, -legs));
        clip.channels.push_back(Swing(3, duration, {1, 0, 0}, -arms));
        clip.channels.push_back(Swing(5, duration, {1, 0, 0}, arms));
        clip.channels.push_back(Swing(1, duration, {0, 1, 0}, 0.06f));
        model.animations.push_back(std::move(clip));
    }
    return model;
}

GltfModel Arms() {
    GltfModel model;
    model.skeleton.name = "ArmsRig";
    model.skeleton.bones = {{"ViewRoot", -1, {}},
                            {"LeftElbow", 0, {{-0.32f, -0.35f, -0.12f}}},
                            {"LeftHand", 1, {{0.18f, 0.13f, -0.45f}}},
                            {"RightElbow", 0, {{0.4f, -0.3f, -0.1f}}},
                            {"RightHand", 3, {{-0.2f, 0.1f, -0.35f}}}};
    FinishSkeleton(model.skeleton);
    const auto bind = BindMatrices(model.skeleton);
    model.materials = {{.name = "Arms", .baseColor = {0.58f, 0.35f, 0.22f, 1}, .roughness = 0.9f},
                       {.name = "Gloves", .baseColor = {0.06f, 0.09f, 0.13f, 1}, .roughness = 0.85f}};
    model.parts = {{"Forearms", {}, 0}, {"Gloves", {}, 1}};
    for (const auto elbow : {1u, 3u}) {
        const auto delta = model.skeleton.bones[elbow + 1].bindPose.translation;
        Box(model.parts[0].mesh, {0.12f, 0.12f, Length(delta)}, delta * 0.5f, elbow, bind,
            Quaternion::LookRotation(delta));
        Box(model.parts[1].mesh, {0.14f, 0.1f, 0.16f}, {0, 0, -0.03f}, elbow + 1, bind);
    }
    AnimationClip idle{"arms_idle", 2, {Translation(0, 2, {}, {0, 0.003f, 0}, {})}};
    model.animations.push_back(std::move(idle));
    constexpr std::array<std::string_view, 6> Names{"pistol",          "rifle",       "shotgun",
                                                    "rocket_launcher", "physics_gun", "tool_gun"};
    constexpr std::array ReloadTimes{1.1f, 1.6f, 2.3f, 2.2f, 1.0f, 1.0f};
    const auto right = model.skeleton.bones[4].bindPose.translation,
               left = model.skeleton.bones[2].bindPose.translation;
    for (std::size_t i = 0; i < Names.size(); ++i) {
        const auto prefix = std::string(Names[i]);
        AnimationClip fire{prefix + "_fire",
                           0.18f,
                           {Translation(4, 0.18f, right, right + Vector3{0, 0.015f, 0.06f}, right)},
                           {{0, "Fire", prefix}}};
        AnimationClip reload{prefix + "_reload",
                             ReloadTimes[i],
                             {Translation(2, ReloadTimes[i], left, left + Vector3{0.18f, 0.1f, 0.15f}, left)},
                             {{ReloadTimes[i] * 0.6f, "ReloadInsert", prefix}}};
        const auto tilted = Quaternion::FromAxisAngle({1, 0, 0}, -0.5f);
        reload.channels.push_back({4,
                                   AnimationPath::Rotation,
                                   AnimationInterpolation::Linear,
                                   {0, ReloadTimes[i] * 0.5f, ReloadTimes[i]},
                                   {{0, 0, 0, 1}, {tilted.x, tilted.y, tilted.z, tilted.w}, {0, 0, 0, 1}}});
        AnimationClip change{
            prefix + "_switch",
            0.35f,
            {Translation(4, 0.35f, right + Vector3{0, -0.35f, 0}, right + Vector3{0, -0.08f, 0}, right)}};
        model.animations.push_back(std::move(fire));
        model.animations.push_back(std::move(reload));
        model.animations.push_back(std::move(change));
    }
    return model;
}

} // namespace Generator::Rigs
