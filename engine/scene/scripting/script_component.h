#pragma once

#include "assets/asset_ref.h"
#include "assets/script_source.h"
#include "scene/component.h"
#include "scene/scripting/script_runtime.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Spot {

namespace Detail {
struct ScriptSceneContext;
}

// Uses an imported script asset, or embedded source when none is assigned. Each component owns its runtime state.
class ScriptComponent final : public Component {
public:
    ScriptComponent();
    ~ScriptComponent() override;

    AssetRef<ScriptSource> script;
    // Used only for embedded source; imported assets select their runtime by source extension.
    std::string language;
    std::string source;
    std::string sourceName = "<ScriptComponent>";
    // Values for the script's exported fields, set on its object once it initializes, before OnStart(). Fields at
    // their default are absent; names the script no longer exports are ignored.
    std::map<std::string, ScriptValue, std::less<>> fieldValues;

    // The script's exported fields with their defaults, read from its source without running it. Empty when the
    // language has no exports or the source does not parse; the last list read is kept while it does not.
    const std::vector<ScriptExport>& GetExports() const;

    // The running script's own object, through which C++ and other scripts read its fields and call its methods
    // (see ScriptRuntime::GetObject()). Null before the component starts and after it is destroyed. Calls run in
    // the script's scene scope and propagate its errors to the caller.
    ScriptObjectRef GetScriptObject() const;
    // Runs one of the script's functions as a coroutine of this component, resumed after each frame's updates: it
    // waits a frame at `yield`, or what it yields (see Detail::ScriptWait). Like other coroutines of the component it
    // pauses while the component is inactive and ends with it. An error faults the component.
    TaskId StartScriptCoroutine(const ScriptObjectRef& function);
    // Swaps in the code of the script asset, as when it was imported again while playing, keeping the running state
    // the language can keep (see ScriptRuntime::CaptureState()): fields keep their values where the new code still
    // declares them with a value of the same type, or none. OnStart() does not run again; the script's coroutines
    // and event subscriptions end, and its OnReload() hook runs, to start them anew. When the new code fails to
    // compile or initialize, the error is logged and the old code keeps running. Hooks call it when the asset's
    // source changed; returns whether it reloaded.
    bool Reload();
    // The source's file name without directories or extension, such as "player" for "scripts/player.luau".
    std::string GetScriptName() const;

protected:
    void Serialize(Archive& archive) override;
    void OnStart() final;
    void OnEnable() final;
    void OnDisable() final;
    void OnFixedUpdate(float deltaTime) final;
    void OnUpdate(float deltaTime) final;
    void OnLateUpdate(float deltaTime) final;
    void OnDestroy() final;
    void OnCollisionEnter(const Collision& collision) final;
    void OnCollisionStay(const Collision& collision) final;
    void OnCollisionExit(const Collision& collision) final;
    void OnTriggerEnter(const TriggerContact& contact) final;
    void OnTriggerExit(const TriggerContact& contact) final;
    void OnCollisionEnter2D(const Collision2D& collision) final;
    void OnCollisionStay2D(const Collision2D& collision) final;
    void OnCollisionExit2D(const Collision2D& collision) final;
    void OnTriggerEnter2D(const TriggerContact2D& contact) final;
    void OnTriggerExit2D(const TriggerContact2D& contact) final;

private:
    struct Runtime {
        std::unique_ptr<ScriptRuntime> instance;
        std::shared_ptr<Detail::ScriptSceneContext> context;
    };
    // Creates and initializes a runtime for the current source, with the exported field values applied.
    Runtime Build();
    std::unique_ptr<ScriptRuntime> m_instance;
    // The asset source the runtime was built from, to notice when it was imported again.
    std::shared_ptr<const ScriptSource> m_runningSource;
    std::shared_ptr<Detail::ScriptSceneContext> m_scriptContext;
    bool m_initialized = false;
    struct ExportedFields {
        ScriptComponent& owner;
        void Serialize(Archive& archive);
    };
    // Interned profiler scope names of its hooks, and whether its runtime profiles.
    std::map<std::string, std::string_view, std::less<>> m_scopeNames;
    bool m_profiling = false;
    std::uint64_t m_debugRevision = 0;
    mutable std::string m_exportsKey;
    mutable std::vector<ScriptExport> m_exports;
    void CallHook(std::string_view name, std::span<const ScriptValue> arguments = {});
    void CallTimedHook(std::string_view name, float deltaTime);
    template <typename Contact>
    void CallContactHook(std::string_view name, const Contact& contact);
};

} // namespace Spot
