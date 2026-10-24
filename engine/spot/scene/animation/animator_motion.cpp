#include "spot/scene/animation/animator.h"

#include "spot/animation/animation_motion.h"
#include "spot/core/debug/log.h"
#include "spot/scene/entity.h"
#include "spot/scene/physics/character_controller.h"
#include "spot/scene/transform.h"

#include <algorithm>
#include <array>

namespace Spot {

void Animator::SetRootMotion(RootMotionMode mode) {
    m_rootMotion = mode;
    Evaluate(0);
}

bool Animator::SetRootMotionBone(std::string name) {
    if (!name.empty() && (!m_skeleton || !m_skeleton->FindBone(name))) {
        Log::GetEngineLogger().Warning("Root motion bone '{}' does not exist", name);
        return false;
    }
    m_rootBoneName = std::move(name);
    ResizePose();
    Evaluate(0);
    return true;
}

TransformComponents Animator::MotionDelta(int stateIndex, const AssetRef<AnimationClip>& clip, float from, float to,
                                          bool loop) {
    if (!m_skeleton || m_rootBoneIndex >= m_skeleton->bones.size()) {
        return {};
    }
    if (stateIndex < 0 || !m_controller) {
        return clip ? GetRootMotionDelta(*m_skeleton, *clip, m_rootBoneIndex, from, to, loop) : TransformComponents{};
    }
    const auto& state = m_controller->states[static_cast<std::size_t>(stateIndex)];
    if (state.blendTree.type == BlendTreeType::None) {
        return GetRootMotionDelta(*m_skeleton, *state.clip, m_rootBoneIndex, from, to, state.loop);
    }
    const auto& tree = state.blendTree;
    const Vector2 value{static_cast<float>(m_controllerRuntime.GetParameter(tree.parameterX).value_or(0)),
                        static_cast<float>(m_controllerRuntime.GetParameter(tree.parameterY).value_or(0))};
    const auto weights = std::span(m_blendWeights).first(tree.motions.size());
    ComputeBlendWeights(tree, value, weights);
    const float duration = GetBlendDuration(tree, weights);
    TransformComponents result;
    float sum = 0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        const auto& motion = tree.motions[i];
        if (weights[i] <= 0 || !motion.clip) {
            continue;
        }
        const float scale = duration > 0 ? motion.clip->duration / duration : 0;
        const auto delta =
            GetRootMotionDelta(*m_skeleton, *motion.clip, m_rootBoneIndex, from * scale, to * scale, state.loop);
        sum += weights[i];
        const float weight = weights[i] / sum;
        result.translation = Lerp(result.translation, delta.translation, weight);
        result.rotation = Slerp(result.rotation, delta.rotation, weight);
    }
    return result;
}

void Animator::DispatchEvents(int stateIndex, const AssetRef<AnimationClip>& source, float from, float to, bool loop,
                              bool includeStart) {
    AssetRef<AnimationClip> clip = source;
    if (stateIndex >= 0 && m_controller) {
        const auto& state = m_controller->states[static_cast<std::size_t>(stateIndex)];
        loop = state.loop;
        if (state.blendTree.type == BlendTreeType::None) {
            clip = state.clip;
        } else {
            const auto& tree = state.blendTree;
            const Vector2 value{static_cast<float>(m_controllerRuntime.GetParameter(tree.parameterX).value_or(0)),
                                static_cast<float>(m_controllerRuntime.GetParameter(tree.parameterY).value_or(0))};
            const auto weights = std::span(m_blendWeights).first(tree.motions.size());
            ComputeBlendWeights(tree, value, weights);
            const auto index =
                static_cast<std::size_t>(std::max_element(weights.begin(), weights.end()) - weights.begin());
            clip = tree.motions[index].clip;
            const float duration = GetBlendDuration(tree, weights);
            const float scale = clip && duration > 0 ? clip->duration / duration : 0;
            from *= scale;
            to *= scale;
        }
    }
    if (!clip) {
        return;
    }
    std::array<const AnimationEvent*, 256> events{};
    const auto count = CollectAnimationEvents(*clip, from, to, loop, events, includeStart);
    if (count == events.size()) {
        Log::GetEngineLogger().Warning("Animation event delivery reached its per-update limit");
    }
    for (std::size_t i = 0; i < count; ++i) {
        GetEntity().ForEachComponent<Component>([&](Component& component) {
            if (component.IsActiveAndEnabled()) {
                component.InvokeHook("OnAnimationEvent", [&] { component.OnAnimationEvent(*events[i]); });
            }
        });
    }
}

void Animator::DeliverMotion(const MotionStep& step, float deltaTime) {
    if (m_rootMotion != RootMotionMode::None && m_skeleton) {
        auto delta = MotionDelta(step.state, step.clip, step.from, step.to, step.loop);
        if (step.weight < 1) {
            const auto outgoing = MotionDelta(step.fadeState, step.fadeClip, step.fadeFrom, step.fadeTo, step.fadeLoop);
            delta.translation = Lerp(outgoing.translation, delta.translation, step.weight);
            delta.rotation = Slerp(outgoing.rotation, delta.rotation, step.weight);
        }
        auto& transform = GetEntity().GetTransform();
        const auto rotation = transform.GetWorldRotation();
        const auto translation =
            transform.GetWorldMatrix() * Vector4{delta.translation.x, delta.translation.y, delta.translation.z, 0};
        const Vector3 movement{translation.x, translation.y, translation.z};
        if (m_rootMotion == RootMotionMode::CharacterController) {
            if (auto* character = GetEntity().GetComponent<CharacterController>();
                character && character->IsActiveAndEnabled()) {
                if (deltaTime > 0) {
                    character->Move(movement / deltaTime, deltaTime);
                }
            }
        } else {
            transform.SetWorldPosition(transform.GetWorldPosition() + movement);
        }
        transform.SetWorldRotation(Normalize(rotation * delta.rotation));
    }
    DispatchEvents(step.state, step.clip, step.from, step.to, step.loop, step.includeStart);
}

} // namespace Spot
