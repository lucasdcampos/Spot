#include "animation/animator.h"

#include "animation/pose.h"
#include "animation/skinned_mesh_renderer.h"
#include "core/log.h"
#include "core/profiler.h"
#include "scene/archive.h"
#include "scene/scene.h"

#include <algorithm>
#include <cmath>

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
    m_local.resize(count);
    m_model.resize(count);
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
    Evaluate(0);
}

void Animator::Simulate(float deltaTime) {
    if (!std::isfinite(deltaTime) || deltaTime < 0 || !m_playing) {
        return;
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
    if (!m_controller && m_clip && !m_loop && (m_speed >= 0 ? m_time >= m_clip->duration : m_time <= 0)) {
        m_playing = false;
    }
}

void Animator::Preview(float deltaTime) {
    if (!std::isfinite(deltaTime) || deltaTime < 0) {
        return;
    }
    m_time += deltaTime * m_speed;
    Evaluate(deltaTime);
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
    if (!m_skeleton || m_model.empty()) {
        return;
    }
    GetScene().ForEachComponent<SkinnedMeshRenderer>([&](SkinnedMeshRenderer& renderer) {
        if (renderer.GetAnimator() == GetEntity().GetId()) {
            Pose::Palette(*m_skeleton, m_model, renderer.GetMeshModel(), m_palette);
            renderer.SetPalette(m_palette);
        }
    });
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
}

void Animator::Serialize(Archive& archive) {
    archive.Field("skeleton", m_skeleton);
    archive.Field("controller", m_controller);
    auto clip = m_controller ? AssetRef<AnimationClip>{} : m_clip;
    archive.Field("clip", clip);
    if (archive.IsLoading() && !m_controller) {
        m_clip = std::move(clip);
    }
    archive.Field("speed", m_speed);
    archive.Field("loop", m_loop);
    archive.Field("autoPlay", m_autoPlay);
}

void AnimationSystem::OnUpdate(float deltaTime) {
    SPOT_PROFILE_SCOPE("AnimationSystem");
    GetScene().ForEachComponent<Animator>([&](Animator& animator) {
        if (!animator.IsActiveAndEnabled()) {
            return;
        }
        animator.InvokeHook("Animation", [&] {
            animator.Simulate(deltaTime);
            animator.RefreshRenderers();
        });
    });
}

} // namespace Spot
