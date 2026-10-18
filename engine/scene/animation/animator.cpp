#include "scene/animation/animator.h"

#include "animation/pose.h"
#include "assets/asset_archive.h"
#include "core/archive.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/profiler.h"
#include "scene/animation/bone_attachment.h"
#include "scene/animation/ragdoll.h"
#include "scene/animation/skinned_mesh_renderer.h"
#include "scene/scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <span>
#include <unordered_map>
#include <utility>

namespace Spot {

void Animator::SetSkeleton(AssetRef<Skeleton> skeleton) {
    if (skeleton && !skeleton->IsValid()) {
        Log::GetEngineLogger().Error("Animator rejected invalid skeleton");
        return;
    }
    m_skeleton = std::move(skeleton);
    m_time = 0;
    m_fadeDuration = 0;
    ResizePose();
    Seek(0);
}

void Animator::ResizePose() {
    const auto count = m_skeleton ? m_skeleton->bones.size() : 0;
    m_rootBoneIndex = m_skeleton
                          ? (m_rootBoneName.empty() ? (m_skeleton->joints.empty() ? 0 : m_skeleton->joints.front())
                                                    : m_skeleton->FindBone(m_rootBoneName).value_or(count))
                          : 0;
    m_local.resize(count);
    m_model.resize(count);
    m_layerPose.resize(count);
    m_layerReference.resize(count);
    if (m_skeleton) {
        Pose::Bind(*m_skeleton, m_layerReference);
    }
    if (!SetLayers(m_layers)) {
        m_layerRuntime.clear();
    }
    m_treeScratch.resize(count);
    std::size_t motions = 0;
    if (m_controller) {
        for (const auto& state : m_controller->states) {
            motions = std::max(motions, state.blendTree.motions.size());
        }
    }
    m_blendWeights.resize(motions);
    m_fadeSource.resize(count);
    m_palette.resize(m_skeleton ? m_skeleton->joints.size() : 0);
}

bool Animator::Accepts(const AssetRef<AnimationClip>& clip) const {
    return m_skeleton && clip && clip->IsValid() && std::ranges::all_of(clip->channels, [&](const auto& channel) {
               return channel.bone < m_skeleton->bones.size();
           });
}

bool Animator::Play(AssetRef<AnimationClip> clip, bool loop) {
    if (!Accepts(clip)) {
        Log::GetEngineLogger().Warning("Animator rejected a missing or incompatible animation clip");
        return false;
    }
    m_clip = std::move(clip);
    m_loop = loop;
    m_playing = true;
    m_fadeDuration = 0;
    Seek(0);
    m_emitStart = true;
    return true;
}

bool Animator::CrossFade(AssetRef<AnimationClip> clip, float duration, bool loop) {
    if (!Accepts(clip) || !std::isfinite(duration)) {
        return false;
    }
    if (duration <= 0 || m_local.empty()) {
        return Play(std::move(clip), loop);
    }
    m_fadeState = -1;
    m_fadeSource = m_local;
    m_fadeClip = m_fadeDuration > 0 ? AssetRef<AnimationClip>{} : m_clip;
    m_fadeClipTime = m_time;
    m_fadeClipSpeed = m_controllerRuntime.GetState() >= 0
                          ? m_controller->states[static_cast<std::size_t>(m_controllerRuntime.GetState())].speed
                          : 1;
    m_fadeLoop = m_loop;
    m_clip = std::move(clip);
    m_loop = loop;
    m_time = 0;
    m_playing = true;
    m_fadeTime = 0;
    m_fadeDuration = duration;
    Evaluate(0);
    m_emitStart = true;
    return true;
}

bool Animator::SetController(AssetRef<AnimatorController> controller) {
    if (controller &&
        (!controller->IsValid() || !m_skeleton || !std::ranges::all_of(controller->states, [&](const auto& state) {
            return state.blendTree.type == BlendTreeType::None
                       ? Accepts(state.clip)
                       : std::ranges::all_of(state.blendTree.motions,
                                             [&](const auto& motion) { return Accepts(motion.clip); });
        }))) {
        Log::GetEngineLogger().Warning("Animator rejected incompatible controller");
        return false;
    }
    m_controller = std::move(controller);
    m_controllerRuntime.SetController(m_controller);
    ResizePose();
    if (m_controller) {
        const auto& state = m_controller->states[static_cast<std::size_t>(m_controller->entryState)];
        Play(state.blendTree.type == BlendTreeType::None ? state.clip : state.blendTree.motions.front().clip,
             state.loop);
    }
    return true;
}

void Animator::SetSpeed(float speed) {
    m_speed = std::isfinite(speed) ? speed : 1;
}

void Animator::Seek(float time) {
    m_time = std::isfinite(time) ? time : 0;
    m_fadeDuration = 0;
    m_emitStart = false;
    for (std::size_t i = 0; i < m_layerRuntime.size(); ++i) {
        m_layerRuntime[i].time = m_time * m_layers[i].speed;
        m_layerRuntime[i].emitStart = false;
    }
    Evaluate(0);
}

void Animator::Simulate(float deltaTime) {
    if (BeginSimulate(deltaTime)) {
        FinishSimulate();
    }
}

bool Animator::BeginSimulate(float deltaTime) {
    if (!std::isfinite(deltaTime) || deltaTime < 0 || !m_playing) {
        return false;
    }
    std::array<LayerEventStep, MaxLayers>& layerEvents = m_pendingLayerEvents;
    layerEvents = {};
    m_pendingLayerCount = AdvanceLayers(deltaTime, layerEvents);
    m_pendingDelta = deltaTime;
    MotionStep& motion = m_pendingMotion;
    motion = {};
    motion.state = m_controllerRuntime.GetState();
    motion.clip = m_clip;
    motion.from = m_time;
    const float stateSpeed = motion.state >= 0 ? m_controller->states[static_cast<std::size_t>(motion.state)].speed : 1;
    motion.to = m_time + deltaTime * m_speed * stateSpeed;
    motion.loop = m_loop;
    motion.includeStart = m_emitStart;
    m_emitStart = false;
    if (m_fadeDuration > 0) {
        motion.fadeLoop = m_fadeLoop;
        motion.fadeState = m_fadeState;
        motion.fadeClip = m_fadeClip;
        motion.fadeFrom = m_fadeClipTime;
        motion.fadeTo = m_fadeClipTime + deltaTime * m_speed * m_fadeClipSpeed;
        motion.weight = (std::clamp(m_fadeTime / m_fadeDuration, 0.0f, 1.0f) +
                         std::clamp((m_fadeTime + deltaTime) / m_fadeDuration, 0.0f, 1.0f)) *
                        0.5f;
    }
    bool transitioned = false;
    if (m_controllerRuntime.GetController()) {
        const int previousState = m_controllerRuntime.GetState();
        const auto& state = m_controller->states[static_cast<std::size_t>(previousState)];
        m_time += deltaTime * m_speed * state.speed;
        Evaluate(0);
        const bool wasFading = m_fadeDuration > 0;
        if (const auto* transition =
                m_controllerRuntime.Advance(std::max(deltaTime * m_speed, 0.0f), m_playbackDuration)) {
            const auto& next = m_controller->states[static_cast<std::size_t>(transition->to)];
            CrossFade(next.blendTree.type == BlendTreeType::None ? next.clip : next.blendTree.motions.front().clip,
                      transition->duration, next.loop);
            m_fadeState = wasFading ? -1 : previousState;
            m_fadeClipSpeed = state.speed;
            transitioned = true;
        }
    } else {
        m_time += deltaTime * m_speed;
    }
    Evaluate(transitioned ? 0.0f : deltaTime);
    return true;
}

void Animator::FinishSimulate() {
    DeliverMotion(m_pendingMotion, m_pendingDelta);
    for (std::size_t i = 0; i < m_pendingLayerCount; ++i) {
        const auto& step = m_pendingLayerEvents[i];
        DispatchEvents(-1, step.clip, step.from, step.to, step.loop, step.includeStart);
    }
    if (!m_controller && m_clip && !m_loop && (m_speed >= 0 ? m_time >= m_clip->duration : m_time <= 0)) {
        m_playing = false;
    }
    // The clips are let go here, on the thread that frees assets.
    m_pendingMotion = {};
    m_pendingLayerEvents = {};
}

void Animator::Preview(float deltaTime) {
    if (!std::isfinite(deltaTime) || deltaTime < 0) {
        return;
    }
    m_time += deltaTime * m_speed;
    AdvanceLayers(deltaTime, {});
    Evaluate(deltaTime);
}

bool Animator::PreviewClip(AssetRef<AnimationClip> clip, float time) {
    if (!Accepts(clip)) {
        return false;
    }
    m_time = Pose::WrapTime(time, clip->duration, false);
    Pose::Sample(*m_skeleton, *clip, m_time, false, m_local);
    if (m_rootMotion != RootMotionMode::None && m_rootBoneIndex < m_local.size()) {
        m_local[m_rootBoneIndex].translation = m_skeleton->bones[m_rootBoneIndex].bindPose.translation;
        m_local[m_rootBoneIndex].rotation = m_skeleton->bones[m_rootBoneIndex].bindPose.rotation;
    }
    Pose::ToModel(*m_skeleton, m_local, m_model);
    return true;
}

void Animator::Evaluate(float deltaTime) {
    SPOT_PROFILE_SCOPE("Animation::Pose");
    if (!m_skeleton || !m_skeleton->IsValid()) {
        return;
    }
    if (m_local.size() != m_skeleton->bones.size()) {
        ResizePose();
    }
    if (m_controllerRuntime.GetController()) {
        SampleState(m_controllerRuntime.GetState(), m_time, m_local);
    } else if (m_clip && Accepts(m_clip)) {
        Pose::Sample(*m_skeleton, *m_clip, m_time, m_loop, m_local);
        m_playbackDuration = m_clip->duration;
    } else {
        Pose::Bind(*m_skeleton, m_local);
    }
    if (m_fadeDuration > 0) {
        m_fadeTime += deltaTime;
        if (m_fadeState >= 0 && m_controller) {
            m_fadeClipTime += deltaTime * m_speed * m_fadeClipSpeed;
            const float incomingDuration = m_playbackDuration;
            SampleState(m_fadeState, m_fadeClipTime, m_fadeSource);
            m_playbackDuration = incomingDuration;
        } else if (m_fadeClip) {
            m_fadeClipTime += deltaTime * m_speed * m_fadeClipSpeed;
            Pose::Sample(*m_skeleton, *m_fadeClip, m_fadeClipTime, m_fadeLoop, m_fadeSource);
        }
        const float weight = std::min(m_fadeTime / m_fadeDuration, 1.0f);
        Pose::Blend(m_fadeSource, m_local, weight, m_local);
        if (weight >= 1) {
            m_fadeDuration = 0;
        }
    }
    ApplyLayers();
    if (m_rootMotion != RootMotionMode::None && m_rootBoneIndex < m_local.size()) {
        m_local[m_rootBoneIndex].translation = m_skeleton->bones[m_rootBoneIndex].bindPose.translation;
        m_local[m_rootBoneIndex].rotation = m_skeleton->bones[m_rootBoneIndex].bindPose.rotation;
    }
    Pose::ToModel(*m_skeleton, m_local, m_model);
}

void Animator::SampleState(int index, float time, std::span<TransformComponents> pose) {
    const auto& state = m_controller->states[static_cast<std::size_t>(index)];
    if (state.blendTree.type == BlendTreeType::None) {
        Pose::Sample(*m_skeleton, *state.clip, time, state.loop, pose);
        m_playbackDuration = state.clip->duration;
    } else {
        const auto& tree = state.blendTree;
        const Vector2 value{static_cast<float>(m_controllerRuntime.GetParameter(tree.parameterX).value_or(0)),
                            static_cast<float>(m_controllerRuntime.GetParameter(tree.parameterY).value_or(0))};
        const auto weights = std::span(m_blendWeights).first(tree.motions.size());
        SampleBlendTree(*m_skeleton, tree, value, time, state.loop, pose, m_treeScratch, weights);
        m_playbackDuration = GetBlendDuration(tree, weights);
    }
}

void Animator::RefreshRenderers() {
    std::vector<SkinnedMeshRenderer*> renderers;
    GetScene().ForEachComponent<SkinnedMeshRenderer>([&](SkinnedMeshRenderer& renderer) {
        if (renderer.GetAnimator() == GetEntity().GetId()) {
            renderers.push_back(&renderer);
        }
    });
    std::vector<BoneAttachment*> attachments;
    GetScene().ForEachComponent<BoneAttachment>([&](BoneAttachment& attachment) {
        if (attachment.GetAnimator() == GetEntity().GetId() || attachment.GetAnimator() == EntityId::Invalid) {
            attachments.push_back(&attachment);
        }
    });
    RefreshRenderers(renderers, attachments);
}

void Animator::RefreshRenderers(std::span<SkinnedMeshRenderer* const> renderers,
                                std::span<BoneAttachment* const> attachments) {
    if (!m_skeleton || m_model.empty()) {
        return;
    }
    for (SkinnedMeshRenderer* renderer : renderers) {
        Pose::Palette(*m_skeleton, m_model, renderer->GetMeshModel(), m_palette);
        renderer->SetPalette(m_palette);
    }
    for (BoneAttachment* attachment : attachments) {
        if (attachment->IsActiveAndEnabled()) {
            attachment->InvokeHook("BoneAttachment", [&] { attachment->Synchronize(); });
        }
    }
}

bool Animator::SetPose(std::span<const TransformComponents> pose) {
    if (!m_skeleton || pose.size() != m_local.size()) {
        return false;
    }
    for (const auto& bone : pose) {
        if (!std::isfinite(bone.translation.x) || !std::isfinite(bone.translation.y) ||
            !std::isfinite(bone.translation.z) || !std::isfinite(bone.rotation.x) || !std::isfinite(bone.rotation.y) ||
            !std::isfinite(bone.rotation.z) || !std::isfinite(bone.rotation.w) || !std::isfinite(bone.scale.x) ||
            !std::isfinite(bone.scale.y) || !std::isfinite(bone.scale.z)) {
            return false;
        }
    }
    std::copy(pose.begin(), pose.end(), m_local.begin());
    Pose::ToModel(*m_skeleton, m_local, m_model);
    return true;
}

void Animator::BlendFromPose(std::span<const TransformComponents> pose, float duration, bool resume) {
    if (!SetPose(pose) || !std::isfinite(duration) || duration < 0) {
        return;
    }
    m_fadeSource.assign(pose.begin(), pose.end());
    m_fadeClip = {};
    m_fadeState = -1;
    m_fadeTime = 0;
    m_fadeDuration = duration;
    m_playing = resume;
    Evaluate(0);
}

void Animator::OnEnable() {
    if (!GetScene().GetSystem<AnimationSystem>()) {
        GetScene().AddSystem<AnimationSystem>();
    }
    if (m_local.empty()) {
        Seek(m_time);
    }
}

void Animator::OnValidate() {
    SetSpeed(m_speed);
    m_playing = m_autoPlay;
    ResizePose();
    Seek(0);
    if (m_controller) {
        SetController(m_controller);
    }
    m_playing = m_autoPlay;
    m_emitStart = m_autoPlay;
}

void Animator::Serialize(Archive& archive) {
    static constexpr std::array<std::string_view, 3> Modes{"None", "Transform", "Character controller"};
    archive.Field("rootMotion", m_rootMotion, {.enumNames = Modes});
    archive.Field("rootBone", m_rootBoneName);
    archive.Field("layers", m_layers);
    archive.Field("skeleton", m_skeleton);
    archive.Field("controller", m_controller);
    auto clip = m_controller ? AssetRef<AnimationClip>{} : m_clip;
    archive.Field("clip", clip);
    if (archive.IsLoading() && !m_controller) {
        m_clip = std::move(clip);
    }
    archive.Field("speed", m_speed);
    archive.Field(
        "updateInterval", m_updateInterval,
        {.min = 0.0f, .max = 1.0f, .speed = 0.001f, .tooltip = "Seconds between pose updates; 0 updates every frame"});
    archive.Field("loop", m_loop);
    archive.Field("autoPlay", m_autoPlay);
}

void AnimationSystem::OnUpdate(float deltaTime) {
    SPOT_PROFILE_SCOPE("AnimationSystem");
    struct Due {
        Animator* animator;
        float elapsed;
        bool finish = false;
        std::exception_ptr error = nullptr;
    };
    std::vector<Due> due;
    GetScene().ForEachComponent<Animator>([&](Animator& animator) {
        if (!animator.IsActiveAndEnabled()) {
            return;
        }
        animator.m_pendingTime += deltaTime;
        if (animator.m_pendingTime < animator.m_updateInterval) {
            return;
        }
        due.push_back({&animator, std::exchange(animator.m_pendingTime, 0.0f)});
    });
    if (due.empty()) {
        return;
    }

    // What each animator drives, gathered once rather than looked for by each.
    std::unordered_map<EntityId, std::vector<SkinnedMeshRenderer*>> renderers;
    GetScene().ForEachComponent<SkinnedMeshRenderer>(
        [&](SkinnedMeshRenderer& renderer) { renderers[renderer.GetAnimator()].push_back(&renderer); });
    std::unordered_map<EntityId, std::vector<BoneAttachment*>> attachments;
    std::vector<BoneAttachment*> anyAttachments;
    GetScene().ForEachComponent<BoneAttachment>([&](BoneAttachment& attachment) {
        if (attachment.GetAnimator() == EntityId::Invalid) {
            anyAttachments.push_back(&attachment);
        } else {
            attachments[attachment.GetAnimator()].push_back(&attachment);
        }
    });

    // Poses are worked out together, on the job system's threads: each animator only touches its own state there.
    // What reaches other components, root motion and events, then goes in order, here.
    JobSystem::GetShared().ParallelFor(due.size(), 4, [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
            try {
                due[i].finish = due[i].animator->BeginSimulate(due[i].elapsed);
            } catch (...) {
                due[i].error = std::current_exception();
            }
        }
    });
    std::vector<BoneAttachment*> driven;
    for (Due& item : due) {
        Animator& animator = *item.animator;
        const EntityId id = animator.GetEntity().GetId();
        animator.InvokeHook("Animation", [&] {
            if (item.error) {
                std::rethrow_exception(item.error);
            }
            if (item.finish) {
                animator.FinishSimulate();
            }
            const auto own = renderers.find(id);
            driven = anyAttachments;
            if (const auto found = attachments.find(id); found != attachments.end()) {
                driven.insert(driven.end(), found->second.begin(), found->second.end());
            }
            animator.RefreshRenderers(own != renderers.end() ? std::span<SkinnedMeshRenderer* const>(own->second)
                                                             : std::span<SkinnedMeshRenderer* const>(),
                                      driven);
        });
    }
}

void AnimationSystem::OnLateUpdate(float) {
    GetScene().ForEachComponent<Ragdoll>([](Ragdoll& ragdoll) {
        if (ragdoll.IsActiveAndEnabled()) {
            ragdoll.InvokeHook("Ragdoll", [&] { ragdoll.SynchronizePose(); });
        }
    });
    GetScene().ForEachComponent<BoneAttachment>([](BoneAttachment& attachment) {
        if (attachment.IsActiveAndEnabled()) {
            attachment.InvokeHook("BoneAttachment", [&] { attachment.Synchronize(); });
        }
    });
}

} // namespace Spot
