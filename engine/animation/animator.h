#pragma once

#include "animation/animation_data.h"
#include "animation/animation_layer.h"
#include "animation/animator_controller.h"
#include "assets/asset_ref.h"
#include "scene/component.h"
#include "scene/scene_system.h"

#include <span>
#include <vector>

namespace Spot {

enum class RootMotionMode { None, Transform, CharacterController };

class Animator final : public Component {
public:
    void SetSkeleton(AssetRef<Skeleton> skeleton);
    const AssetRef<Skeleton>& GetSkeleton() const { return m_skeleton; }
    bool Play(AssetRef<AnimationClip> clip, bool loop = true);
    bool CrossFade(AssetRef<AnimationClip> clip, float duration, bool loop = true);
    bool SetController(AssetRef<AnimatorController> controller);
    const AssetRef<AnimatorController>& GetController() const { return m_controller; }
    const AnimatorControllerRuntime& GetControllerRuntime() const { return m_controllerRuntime; }
    bool SetFloat(std::string_view name, float value) { return m_controllerRuntime.SetFloat(name, value); }
    bool SetInt(std::string_view name, int value) { return m_controllerRuntime.SetInt(name, value); }
    bool SetBool(std::string_view name, bool value) { return m_controllerRuntime.SetBool(name, value); }
    bool SetTrigger(std::string_view name) { return m_controllerRuntime.SetTrigger(name); }
    bool ResetTrigger(std::string_view name) { return m_controllerRuntime.ResetTrigger(name); }
    void SetRootMotion(RootMotionMode mode);
    RootMotionMode GetRootMotion() const { return m_rootMotion; }
    bool SetRootMotionBone(std::string name);
    static constexpr std::size_t MaxLayers = 16;
    bool SetLayers(std::span<const AnimationLayer> layers);
    std::span<const AnimationLayer> GetLayers() const { return m_layers; }
    bool SetLayerWeight(std::string_view name, float weight);
    void Stop() { m_playing = false; }
    bool IsPlaying() const { return m_playing; }
    void SetSpeed(float speed);
    float GetSpeed() const { return m_speed; }
    bool IsLooping() const { return m_loop; }
    void SetLooping(bool loop) { m_loop = loop; }
    float GetDuration() const { return m_playbackDuration; }
    float GetTime() const { return m_time; }
    const AssetRef<AnimationClip>& GetClip() const { return m_clip; }
    std::span<const TransformComponents> GetLocalPose() const { return m_local; }
    std::span<const Matrix4> GetModelPose() const { return m_model; }
    // Preview and gameplay use the same evaluator; seeking produces no gameplay callbacks.
    void Seek(float time);
    void Simulate(float deltaTime);
    void Preview(float deltaTime);
    void RefreshRenderers();

protected:
    void OnEnable() override;
    void OnValidate() override;
    void Serialize(Archive& archive) override;

private:
    friend class AnimationSystem;
    void Evaluate(float deltaTime);
    bool Accepts(const AssetRef<AnimationClip>& clip) const;
    struct MotionStep {
        int state = -1;
        AssetRef<AnimationClip> clip;
        float from = 0, to = 0;
        bool loop = true, includeStart = false, fadeLoop = true;
        int fadeState = -1;
        AssetRef<AnimationClip> fadeClip;
        float fadeFrom = 0, fadeTo = 0, weight = 1;
    };
    void DeliverMotion(const MotionStep& step, float deltaTime);
    TransformComponents MotionDelta(int state, const AssetRef<AnimationClip>& clip, float from, float to, bool loop);
    void DispatchEvents(int state, const AssetRef<AnimationClip>& clip, float from, float to, bool loop,
                        bool includeStart);
    void ResizePose();
    void SampleState(int state, float time, std::span<TransformComponents> pose);
    struct LayerRuntime {
        float time = 0;
        bool emitStart = true;
        std::vector<float> mask;
    };
    struct LayerEventStep {
        AssetRef<AnimationClip> clip;
        float from = 0, to = 0;
        bool loop = true, includeStart = false;
    };
    std::size_t AdvanceLayers(float deltaTime, std::span<LayerEventStep> events);
    void ApplyLayers();
    std::vector<AnimationLayer> m_layers;
    std::vector<LayerRuntime> m_layerRuntime;
    std::vector<TransformComponents> m_layerPose;
    std::vector<TransformComponents> m_layerReference;
    RootMotionMode m_rootMotion = RootMotionMode::None;
    std::string m_rootBoneName;
    std::size_t m_rootBoneIndex = 0;
    bool m_emitStart = false;
    AssetRef<AnimatorController> m_controller;
    AnimatorControllerRuntime m_controllerRuntime;
    float m_fadeClipSpeed = 1;
    int m_fadeState = -1;
    float m_playbackDuration = 0;
    std::vector<TransformComponents> m_treeScratch;
    std::vector<float> m_blendWeights;
    AssetRef<Skeleton> m_skeleton;
    AssetRef<AnimationClip> m_clip;
    float m_speed = 1;
    bool m_loop = true;
    bool m_playing = true;
    bool m_autoPlay = true;
    float m_time = 0;
    float m_fadeTime = 0;
    float m_fadeDuration = 0;
    AssetRef<AnimationClip> m_fadeClip;
    float m_fadeClipTime = 0;
    bool m_fadeLoop = true;
    std::vector<TransformComponents> m_local;
    std::vector<TransformComponents> m_fadeSource;
    std::vector<Matrix4> m_model;
    std::vector<Matrix4> m_palette;
};

class AnimationSystem final : public SceneSystem {
protected:
    void OnUpdate(float deltaTime) override;
};

} // namespace Spot
