#include "spot/scene/scripting/script_component.h"
#include "spot/assets/assets.h"

#include "spot/assets/asset_archive.h"
#include "spot/core/debug/log.h"
#include "spot/core/debug/profiler.h"
#include "spot/core/io/archive.h"
#include "spot/core/io/paths.h"
#include "spot/scene/entity.h"
#include "spot/scene/physics/collision.h"
#include "spot/scene/physics2d/collision_2d.h"
#include "spot/scene/scripting/script_api.h"
#include "spot/scene/scripting/script_scene.h"

#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>
#include <utility>

namespace Spot {

ScriptComponent::ScriptComponent() = default;
ScriptComponent::~ScriptComponent() = default;

void ScriptComponent::Serialize(Archive& archive) {
    archive.Field("script", script, {.tooltip = "Script asset; changes take effect on the next Play"});
    archive.Field("language", language, {.hidden = true});

    archive.Field("source", source, {.hidden = true});
    archive.Field("sourceName", sourceName, {.hidden = true});

    // Exported fields, for each field the script exports now. All are saved, so restoring a saved state (as undo
    // does) brings back defaults too; loaded values equal to the default are not kept as edits.
    if (GetExports().empty())
        return;
    ExportedFields fields{*this};
    archive.Field("fields", fields, {.tooltip = "The script's exported fields"});
}

void ScriptComponent::ExportedFields::Serialize(Archive& archive) {
    for (const auto& field : owner.GetExports()) {
        const auto stored = owner.fieldValues.find(field.name);
        ScriptValue value = stored != owner.fieldValues.end() ? stored->second : field.value;
        // A value of another type, from an edit made before the default's type changed, starts from the default.
        if (value.index() != field.value.index())
            value = field.value;
        std::visit(
            [&](auto& typed) {
                using T = std::remove_cvref_t<decltype(typed)>;
                if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, double> || std::is_same_v<T, std::string> ||
                              std::is_same_v<T, Vector2> || std::is_same_v<T, Vector3> || std::is_same_v<T, Color>)
                    archive.Field(field.name, typed);
            },
            value);
        if (archive.IsLoading()) {
            if (value == field.value)
                owner.fieldValues.erase(field.name);
            else
                owner.fieldValues.insert_or_assign(field.name, std::move(value));
        }
    }
}

const std::vector<ScriptExport>& ScriptComponent::GetExports() const {
    const std::string_view text = script ? std::string_view(script->source) : std::string_view(source);
    const std::string_view name = script ? std::string_view(script->sourceName) : std::string_view(sourceName);
    std::string key;
    try {
        key = script ? GetScriptLanguageForSource(name) : language;
    } catch (const std::exception&) {
        return m_exports;
    }
    key.append(1, '\0').append(name).append(1, '\0').append(text);
    if (key == m_exportsKey)
        return m_exports;
    m_exportsKey = std::move(key);
    try {
        m_exports = GetScriptExports(script ? GetScriptLanguageForSource(name) : language, text, name);
    } catch (const std::exception&) {
        // Unfinished source keeps the fields it had, so edits are not lost while typing.
    }
    return m_exports;
}

void ScriptComponent::OnStart() {
    if (!script && script.GetId().IsValid()) {
        script = Assets::Load<ScriptSource>(script.GetId());
        if (!script) {
            throw std::runtime_error("Could not load Script asset " + script.GetId().ToString());
        }
    }
    auto runtime = Build();
    m_instance = std::move(runtime.instance);
    m_scriptContext = std::move(runtime.context);
    m_runningSource = script.GetShared();
    m_initialized = true;
    CallHook(ScriptApi::StartHook);
}

ScriptComponent::Runtime ScriptComponent::Build() {
    const std::string_view text = script ? script->source : source;
    const std::string_view name = script ? script->sourceName : sourceName;
    const std::string selectedLanguage = script ? GetScriptLanguageForSource(name) : language;
    if (selectedLanguage.empty())
        throw std::runtime_error("Embedded scripts require a language");
    Runtime runtime{CreateScriptRuntime(selectedLanguage, text, name), Detail::CreateScriptSceneContext()};
    auto& instance = *runtime.instance;
    instance.SetSourceLoader([](std::string_view request, std::string_view importer) -> std::optional<ScriptSource> {
        const auto normalize = [](std::string_view path) {
            std::string value(path);
            std::ranges::replace(value, '\\', '/');
            return Utf8ToPath(value);
        };
        const auto requested = normalize(request);
        const auto importing = normalize(importer);
        if (requested.has_root_path() || importing.has_root_path())
            throw std::runtime_error("Script imports must use relative asset paths");
        const auto path = (importing.parent_path() / requested).lexically_normal();
        if (path.empty() || *path.begin() == "..")
            throw std::runtime_error("Script import leaves the asset directory");
        const auto utf8 = path.generic_u8string();
        auto loaded = Assets::Load<ScriptSource>(std::string(utf8.begin(), utf8.end()));
        return loaded ? std::optional<ScriptSource>(*loaded) : std::nullopt;
    });
    Detail::BindSceneScript(instance, runtime.context, GetEntity().GetId(), GetId());
    // Calls into the script from elsewhere (another script's call, a callback it registered) see its scene.
    instance.SetInvocationScope([this, context = runtime.context](const std::function<void()>& call) {
        const Detail::ScriptSceneScope sceneScope(*context, GetScene(), this);
        call();
    });
    Detail::SetScriptEventDelivery(
        *runtime.context, [this](const std::function<void()>& callback) { RunGuarded("event callback", callback); });
    Detail::BindRegisteredScriptLibraries(instance);
    const Detail::ScriptSceneScope sceneScope(*runtime.context, GetScene(), this);
    for (const auto& hook : ScriptApi::Hooks)
        instance.ValidateFunction(hook.name, hook.parameterCount);
    instance.Initialize();
    if (!fieldValues.empty()) {
        if (const auto object = instance.GetObject()) {
            for (const auto& [field, value] : fieldValues) {
                if (object->HasProperty(field))
                    object->Set(field, value);
            }
        }
    }
    return runtime;
}

bool ScriptComponent::Reload() {
    if (!m_instance || !m_initialized || !script)
        return false;
    // Whatever happens, this source is not tried again.
    m_runningSource = script.GetShared();
    auto state = m_instance->CaptureState();
    Runtime runtime;
    try {
        runtime = Build();
    } catch (const std::exception& error) {
        Logger{"Scripts"}.WriteMessage(LogLevel::Warning, "Kept the running code of '" + GetScriptName() +
                                                              "', as its new code failed: " + error.what());
        return false;
    }
    CancelCoroutines();
    Detail::SetScriptEventDelivery(*m_scriptContext, {});
    auto previous = std::exchange(m_instance, std::move(runtime.instance));
    m_scriptContext = std::move(runtime.context);
    m_scopeNames.clear();
    m_profiling = false;
    m_debugRevision = 0;
    if (const auto object = m_instance->GetObject()) {
        const Detail::ScriptSceneScope sceneScope(*m_scriptContext, GetScene(), this);
        for (const auto& [field, value] : state) {
            try {
                if (!object->HasProperty(field))
                    continue;
                const auto current = object->Get(field);
                if (current.index() == value.index() || std::holds_alternative<std::monostate>(current))
                    object->Set(field, value);
            } catch (const std::exception&) {
                // A field the new code validates differently keeps its new value.
            }
        }
    }
    previous.reset();
    Logger{"Scripts"}.WriteMessage(LogLevel::Info, "Reloaded '" + GetScriptName() + "'");
    CallHook("OnReload");
    return true;
}

namespace {

Task RunScriptCoroutine(ScriptObjectRef coroutine) {
    for (;;) {
        const ScriptValue yielded = coroutine->Call("resume", {});
        if (Detail::FromScriptValue<bool>(coroutine->Get("done")))
            co_return;
        const auto wait = Detail::ReadScriptWait(yielded);
        switch (wait.kind) {
        case Detail::ScriptWait::Kind::Frame:
            co_await NextFrame{};
            break;
        case Detail::ScriptWait::Kind::Seconds:
            co_await WaitForSeconds{static_cast<float>(wait.amount)};
            break;
        case Detail::ScriptWait::Kind::Frames:
            for (double frame = 0; frame < wait.amount; ++frame)
                co_await NextFrame{};
            break;
        case Detail::ScriptWait::Kind::Condition:
            co_await WaitUntil{
                [condition = wait.condition] { return Detail::FromScriptValue<bool>(condition->Invoke({})); }};
            break;
        }
    }
}

} // namespace

TaskId ScriptComponent::StartScriptCoroutine(const ScriptObjectRef& function) {
    if (!m_instance || !m_initialized)
        throw std::runtime_error("Coroutines start once the script is running");
    auto coroutine = m_instance->CreateCoroutine(function);
    if (!coroutine)
        throw std::runtime_error("This script language has no coroutines");
    return StartCoroutine(RunScriptCoroutine(std::move(coroutine)));
}

ScriptObjectRef ScriptComponent::GetScriptObject() const {
    return m_instance && m_initialized ? m_instance->GetObject() : nullptr;
}
std::string ScriptComponent::GetScriptName() const {
    const std::string_view name = script ? std::string_view(script->sourceName) : std::string_view(sourceName);
    auto stem = Utf8ToPath(std::string(name)).stem().u8string();
    return std::string(stem.begin(), stem.end());
}

void ScriptComponent::CallHook(std::string_view name, std::span<const ScriptValue> arguments) {
    if (m_initialized && script && script.GetShared() != m_runningSource && name != "OnReload")
        Reload();
    if (m_instance && m_initialized && m_instance->HasFunction(name)) {
        auto scope = m_scopeNames.find(name);
        if (scope == m_scopeNames.end())
            scope = m_scopeNames.emplace(std::string(name), Profiler::Intern(GetScriptName() + "." + std::string(name)))
                        .first;
        SPOT_PROFILE_SCOPE(scope->second);
        if (const auto revision = Detail::GetScriptDebugRevision(); revision != m_debugRevision) {
            m_instance->SetBreakpoints(Detail::GetAllScriptBreakpoints());
            m_debugRevision = revision;
        }
        if (const bool profiling = IsScriptProfiling(); profiling != m_profiling) {
            m_instance->SetProfiling(profiling);
            m_profiling = profiling;
        }
        const Detail::ScriptSceneScope sceneScope(*m_scriptContext, GetScene(), this);
        m_instance->Call(name, arguments);
        if (m_profiling) {
            const auto profile = m_instance->TakeProfile();
            Detail::AddScriptProfile(profile);
        }
    }
}
void ScriptComponent::CallTimedHook(std::string_view name, float deltaTime) {
    const std::array<ScriptValue, 1> arguments{static_cast<double>(deltaTime)};
    CallHook(name, arguments);
}
template <typename Contact>
void ScriptComponent::CallContactHook(std::string_view name, const Contact& contact) {
    // Building the contact object costs more than the lookup, so skip it for scripts without the hook.
    if (!m_instance || !m_initialized || !m_instance->HasFunction(name))
        return;
    const std::array<ScriptValue, 1> arguments{Detail::MakeScriptContact(m_scriptContext, contact)};
    CallHook(name, arguments);
}
void ScriptComponent::OnCollisionEnter(const Collision& collision) {
    CallContactHook("OnCollisionEnter", collision);
}
void ScriptComponent::OnCollisionStay(const Collision& collision) {
    CallContactHook("OnCollisionStay", collision);
}
void ScriptComponent::OnCollisionExit(const Collision& collision) {
    CallContactHook("OnCollisionExit", collision);
}
void ScriptComponent::OnTriggerEnter(const TriggerContact& contact) {
    CallContactHook("OnTriggerEnter", contact);
}
void ScriptComponent::OnTriggerExit(const TriggerContact& contact) {
    CallContactHook("OnTriggerExit", contact);
}
void ScriptComponent::OnCollisionEnter2D(const Collision2D& collision) {
    CallContactHook("OnCollisionEnter2D", collision);
}
void ScriptComponent::OnCollisionStay2D(const Collision2D& collision) {
    CallContactHook("OnCollisionStay2D", collision);
}
void ScriptComponent::OnCollisionExit2D(const Collision2D& collision) {
    CallContactHook("OnCollisionExit2D", collision);
}
void ScriptComponent::OnTriggerEnter2D(const TriggerContact2D& contact) {
    CallContactHook("OnTriggerEnter2D", contact);
}
void ScriptComponent::OnTriggerExit2D(const TriggerContact2D& contact) {
    CallContactHook("OnTriggerExit2D", contact);
}

void ScriptComponent::OnEnable() {
    CallHook("OnEnable");
}
void ScriptComponent::OnDisable() {
    CallHook("OnDisable");
}
void ScriptComponent::OnFixedUpdate(float deltaTime) {
    CallTimedHook("OnFixedUpdate", deltaTime);
}
void ScriptComponent::OnUpdate(float deltaTime) {
    CallTimedHook(ScriptApi::UpdateHook, deltaTime);
}
void ScriptComponent::OnLateUpdate(float deltaTime) {
    CallTimedHook("OnLateUpdate", deltaTime);
}

void ScriptComponent::OnDestroy() {
    auto instance = std::move(m_instance);
    auto context = std::move(m_scriptContext);
    if (context)
        Detail::SetScriptEventDelivery(*context, {});
    const bool initialized = std::exchange(m_initialized, false);
    if (instance && initialized && instance->HasFunction("OnDestroy")) {
        const Detail::ScriptSceneScope sceneScope(*context, GetScene(), this);
        instance->Call("OnDestroy");
    }
}

} // namespace Spot
