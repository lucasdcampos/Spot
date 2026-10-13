#pragma once

#include "animation/animation_data.h"
#include "animation/blend_tree.h"
#include "assets/asset_ref.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Spot {

class Archive;
class AssetManager;

enum class AnimatorParameterType { Float, Int, Bool, Trigger };
enum class AnimatorComparison { Greater, Less, Equal, NotEqual };

struct AnimatorParameter {
    std::string name;
    AnimatorParameterType type = AnimatorParameterType::Float;
    double defaultValue = 0;
    void Serialize(Archive& archive);
};

struct AnimatorCondition {
    std::string parameter;
    AnimatorComparison comparison = AnimatorComparison::Greater;
    double threshold = 0;
    void Serialize(Archive& archive);
};

struct AnimatorState {
    std::string name;
    AssetRef<AnimationClip> clip = {};
    float speed = 1;
    bool loop = true;
    Vector2 editorPosition = {};
    BlendTree blendTree = {};
    void Serialize(Archive& archive);
};

struct AnimatorTransition {
    // -1 means any state. First matching transition wins, in authored order.
    int from = -1;
    int to = 0;
    std::vector<AnimatorCondition> conditions = {};
    // Negative disables exit time; otherwise a normalized time (can exceed one for loops).
    float exitTime = -1;
    float duration = 0.2f;
    bool interruptible = true;
    void Serialize(Archive& archive);
};

struct AnimatorController {
    std::vector<AnimatorParameter> parameters;
    std::vector<AnimatorState> states;
    std::vector<AnimatorTransition> transitions;
    int entryState = 0;

    bool IsValid() const;
    void Serialize(Archive& archive);
    std::string SaveToString() const;
    static std::optional<AnimatorController> LoadFromString(std::string_view text, AssetManager* assets = nullptr);
};

// Independent of scenes and GPU resources. Parameters and timers are per instance.
class AnimatorControllerRuntime {
public:
    void SetController(AssetRef<AnimatorController> controller);
    const AssetRef<AnimatorController>& GetController() const { return m_controller; }
    bool SetFloat(std::string_view name, float value);
    bool SetInt(std::string_view name, int value);
    bool SetBool(std::string_view name, bool value);
    bool SetTrigger(std::string_view name);
    bool ResetTrigger(std::string_view name);
    std::optional<double> GetParameter(std::string_view name) const;
    std::span<const double> GetParameters() const { return m_values; }
    int GetState() const { return m_state; }
    float GetStateTime() const { return m_time; }
    // Returns the chosen transition, or null when the current state keeps playing.
    const AnimatorTransition* Advance(float deltaTime, float clipDuration);
    void Restart();

private:
    bool Set(std::string_view name, double value, AnimatorParameterType type);
    bool Matches(const AnimatorCondition& condition) const;
    AssetRef<AnimatorController> m_controller;
    std::vector<double> m_values;
    int m_state = -1;
    float m_time = 0;
    float m_remainingFade = 0;
    bool m_interruptible = true;
};

} // namespace Spot
