#pragma once

#include "spot/scene/entity.h"
#include "spot/scene/scripting/script_runtime.h"

#include <memory>

namespace Spot {
struct Collision;
struct TriggerContact;
struct Collision2D;
struct TriggerContact2D;
} // namespace Spot

namespace Spot::Detail {

struct ScriptSceneContext;
// Read-only snapshots of physics contacts, whose `other` is an entity reference of the context.
ScriptValue MakeScriptContact(const std::shared_ptr<ScriptSceneContext>& context, const Collision& collision);
ScriptValue MakeScriptContact(const std::shared_ptr<ScriptSceneContext>& context, const TriggerContact& contact);
ScriptValue MakeScriptContact(const std::shared_ptr<ScriptSceneContext>& context, const Collision2D& collision);
ScriptValue MakeScriptContact(const std::shared_ptr<ScriptSceneContext>& context, const TriggerContact2D& contact);
std::shared_ptr<ScriptSceneContext> CreateScriptSceneContext();
void BindSceneScript(ScriptRuntime& runtime, const std::shared_ptr<ScriptSceneContext>& context, EntityId owner,
                     ComponentId component);

// A context retains only ids between hooks. Engine objects can be resolved while their scene is in this scope.
class ScriptSceneScope {
public:
    ScriptSceneScope(ScriptSceneContext& context, Scene& scene, Component* component = nullptr);
    ~ScriptSceneScope();
    ScriptSceneScope(const ScriptSceneScope&) = delete;
    ScriptSceneScope& operator=(const ScriptSceneScope&) = delete;

private:
    ScriptSceneContext& m_context;
    Scene* m_previousScene;
    Component* m_previousComponent;
    ScriptSceneContext* m_previousActive;
};

// What a script coroutine yields to wait: a frame for null, or what WaitSeconds(), WaitFrames() and WaitUntil()
// return. Anything else is an error.
struct ScriptWait {
    enum class Kind { Frame, Seconds, Frames, Condition };
    Kind kind = Kind::Frame;
    double amount = 0;
    ScriptObjectRef condition;
};
ScriptWait ReadScriptWait(const ScriptValue& value);

// What the owning ScriptComponent sets up: the component that runs callbacks, and how it runs them (in its scene
// scope, faulting it when they throw). Clearing the delivery stops callbacks once the component is destroyed.
void SetScriptEventDelivery(ScriptSceneContext& context, std::function<void(const std::function<void()>&)> deliver);

} // namespace Spot::Detail
