#pragma once

#include "animation/animation_data.h"
#include "animation/animator_controller.h"
#include "assets/asset_ref.h"
#include "scene/component.h"
#include "scene/scene_system.h"

#include <span>
#include <vector>

namespace Spot {

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
    void Stop() { m_playing = false; }
    bool IsPlaying() const { return m_playing; }
    void SetSpeed(float speed);
    float GetSpeed() const { return m_speed; }
    bool IsLooping() const { return m_loop; }
    void SetLooping(bool loop) { m_loop = loop; }
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
    void ResizePose();
    AssetRef<AnimatorController> m_controller;
    AnimatorControllerRuntime m_controllerRuntime;
    float m_fadeClipSpeed = 1;
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
