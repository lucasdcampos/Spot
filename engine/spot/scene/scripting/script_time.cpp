#include "spot/core/time/time.h"
#include "spot/scene/scripting/script_module_bindings.h"

namespace Spot::Detail {
ScriptValue GetScriptTime(SceneProperty property, std::string_view name) {
    if (property == SceneProperty::DeltaTime)
        return static_cast<double>(Time::GetDeltaTime());
    if (property == SceneProperty::UnscaledDeltaTime)
        return static_cast<double>(Time::GetUnscaledDeltaTime());
    if (property == SceneProperty::Time)
        return Time::GetTime();
    if (property == SceneProperty::UnscaledTime)
        return Time::GetUnscaledTime();
    if (property == SceneProperty::FrameCount)
        return ToScriptValue(Time::GetFrameCount());
    if (property == SceneProperty::TimeScale)
        return static_cast<double>(Time::GetTimeScale());
    return ScriptObject().Get(name);
}
void SetScriptTimeScale(const ScriptValue& value) {
    if (!Time::SetTimeScale(Argument<float>(value)))
        throw std::runtime_error("Time scale must be finite and not negative");
}
} // namespace Spot::Detail
