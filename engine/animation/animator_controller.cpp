#include "animation/animator_controller.h"

#include "scene/archive.h"
#include "scene/archive_state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>

namespace Spot {

void AnimatorParameter::Serialize(Archive& archive) {
    static constexpr std::array<std::string_view, 4> Types{"Float", "Int", "Bool", "Trigger"};
    archive.Field("name", name);
    archive.Field("type", type, {.enumNames = Types});
    archive.Field("default", defaultValue);
}
void AnimatorCondition::Serialize(Archive& archive) {
    static constexpr std::array<std::string_view, 4> Comparisons{"Greater", "Less", "Equal", "Not equal"};
    archive.Field("parameter", parameter);
    archive.Field("comparison", comparison, {.enumNames = Comparisons});
    archive.Field("threshold", threshold);
}
void AnimatorState::Serialize(Archive& archive) {
    archive.Field("name", name);
    archive.Field("clip", clip);
    archive.Field("speed", speed);
    archive.Field("loop", loop);
    archive.Field("editorPosition", editorPosition, {.hidden = true});
}
void AnimatorTransition::Serialize(Archive& archive) {
    archive.Field("from", from);
    archive.Field("to", to);
    archive.Field("conditions", conditions);
    archive.Field("exitTime", exitTime);
    archive.Field("duration", duration);
    archive.Field("interruptible", interruptible);
}
void AnimatorController::Serialize(Archive& archive) {
    archive.Field("parameters", parameters);
    archive.Field("states", states);
    archive.Field("transitions", transitions);
    archive.Field("entryState", entryState);
}

bool AnimatorController::IsValid() const {
    if (states.empty() || entryState < 0 || static_cast<std::size_t>(entryState) >= states.size()) {
        return false;
    }
    std::unordered_set<std::string> names;
    for (const auto& parameter : parameters) {
        if (parameter.name.empty() || !names.insert(parameter.name).second || !std::isfinite(parameter.defaultValue) ||
            (parameter.type < AnimatorParameterType::Float || parameter.type > AnimatorParameterType::Trigger)) {
            return false;
        }
    }
    std::unordered_set<std::string> stateNames;
    for (const auto& state : states) {
        if (state.name.empty() || !stateNames.insert(state.name).second || !std::isfinite(state.speed) ||
            state.speed < 0 || (!state.clip && !state.clip.GetId().IsValid()) ||
            (state.clip && !state.clip->IsValid())) {
            return false;
        }
    }
    for (const auto& transition : transitions) {
        if (transition.from < -1 || transition.from >= static_cast<int>(states.size()) || transition.to < 0 ||
            transition.to >= static_cast<int>(states.size()) || !std::isfinite(transition.duration) ||
            transition.duration < 0 || !std::isfinite(transition.exitTime)) {
            return false;
        }
        for (const auto& condition : transition.conditions) {
            if (!names.contains(condition.parameter) || !std::isfinite(condition.threshold) ||
                (condition.comparison < AnimatorComparison::Greater ||
                 condition.comparison > AnimatorComparison::NotEqual)) {
                return false;
            }
        }
    }
    return true;
}

std::string AnimatorController::SaveToString() const {
    auto document = nlohmann::json{{"version", 1}};
    Detail::ArchiveState state{.nodes = {&document}};
    Archive archive(state);
    auto copy = *this;
    copy.Serialize(archive);
    return document.dump(2) + "\n";
}
std::optional<AnimatorController> AnimatorController::LoadFromString(std::string_view text, AssetManager* assets) {
    auto document = nlohmann::json::parse(text, nullptr, false);
    if (!document.is_object() || !document.contains("version") || document["version"] != 1) {
        return std::nullopt;
    }
    try {
        Detail::ArchiveState state{.loading = true, .nodes = {&document}, .assets = assets};
        Archive archive(state);
        AnimatorController controller;
        controller.Serialize(archive);
        return controller.IsValid() ? std::optional(std::move(controller)) : std::nullopt;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

void AnimatorControllerRuntime::SetController(AssetRef<AnimatorController> controller) {
    m_controller = controller && controller->IsValid() ? std::move(controller) : AssetRef<AnimatorController>{};
    Restart();
}
void AnimatorControllerRuntime::Restart() {
    m_values.clear();
    if (m_controller) {
        for (const auto& parameter : m_controller->parameters) {
            const double value = parameter.type == AnimatorParameterType::Trigger ? 0 : parameter.defaultValue;
            m_values.push_back(parameter.type == AnimatorParameterType::Bool ? (value != 0 ? 1.0f : 0.0f) : value);
        }
    }
    m_state = m_controller ? m_controller->entryState : -1;
    m_time = 0;
    m_remainingFade = 0;
    m_interruptible = true;
}
bool AnimatorControllerRuntime::Set(std::string_view name, double value, AnimatorParameterType type) {
    if (!m_controller || !std::isfinite(value)) {
        return false;
    }
    for (std::size_t i = 0; i < m_values.size(); ++i) {
        if (m_controller->parameters[i].name == name && m_controller->parameters[i].type == type) {
            m_values[i] = value;
            return true;
        }
    }
    return false;
}
bool AnimatorControllerRuntime::SetFloat(std::string_view name, float value) {
    return Set(name, value, AnimatorParameterType::Float);
}
bool AnimatorControllerRuntime::SetInt(std::string_view name, int value) {
    return Set(name, static_cast<double>(value), AnimatorParameterType::Int);
}
bool AnimatorControllerRuntime::SetBool(std::string_view name, bool value) {
    return Set(name, value ? 1.0f : 0.0f, AnimatorParameterType::Bool);
}
bool AnimatorControllerRuntime::SetTrigger(std::string_view name) {
    return Set(name, 1, AnimatorParameterType::Trigger);
}
bool AnimatorControllerRuntime::ResetTrigger(std::string_view name) {
    return Set(name, 0, AnimatorParameterType::Trigger);
}
std::optional<double> AnimatorControllerRuntime::GetParameter(std::string_view name) const {
    if (m_controller) {
        for (std::size_t i = 0; i < m_values.size(); ++i) {
            if (m_controller->parameters[i].name == name) {
                return m_values[i];
            }
        }
    }
    return std::nullopt;
}
bool AnimatorControllerRuntime::Matches(const AnimatorCondition& condition) const {
    const auto value = GetParameter(condition.parameter);
    if (!value) {
        return false;
    }
    for (const auto& parameter : m_controller->parameters) {
        if (parameter.name == condition.parameter && parameter.type == AnimatorParameterType::Trigger) {
            return *value != 0;
        }
    }
    switch (condition.comparison) {
    case AnimatorComparison::Greater:
        return *value > condition.threshold;
    case AnimatorComparison::Less:
        return *value < condition.threshold;
    case AnimatorComparison::Equal:
        return *value == condition.threshold;
    case AnimatorComparison::NotEqual:
        return *value != condition.threshold;
    }
    return false;
}
const AnimatorTransition* AnimatorControllerRuntime::Advance(float deltaTime, float clipDuration) {
    if (!m_controller || !std::isfinite(deltaTime) || deltaTime < 0) {
        return nullptr;
    }
    const auto& current = m_controller->states[static_cast<std::size_t>(m_state)];
    m_time += deltaTime * current.speed;
    m_remainingFade = std::max(m_remainingFade - deltaTime, 0.0f);
    if (m_remainingFade > 0 && !m_interruptible) {
        return nullptr;
    }
    for (const auto& transition : m_controller->transitions) {
        if ((transition.from != -1 && transition.from != m_state) || transition.to == m_state ||
            (transition.exitTime >= 0 && (clipDuration > 0 ? m_time / clipDuration : 1) < transition.exitTime) ||
            !std::ranges::all_of(transition.conditions, [&](const auto& condition) { return Matches(condition); })) {
            continue;
        }
        for (const auto& condition : transition.conditions) {
            ResetTrigger(condition.parameter);
        }
        m_state = transition.to;
        m_time = 0;
        m_remainingFade = transition.duration;
        m_interruptible = transition.interruptible;
        return &transition;
    }
    return nullptr;
}

} // namespace Spot
