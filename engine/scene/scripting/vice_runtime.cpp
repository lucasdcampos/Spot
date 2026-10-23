#include "scene/scripting/vice_runtime.h"
#include "scene/scripting/script_api.h"

#include <vice/vice.h>

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Spot {
namespace {

struct Bridge;
struct ViceObject final : Vice::Object {
    ScriptObjectRef object;
    std::shared_ptr<Bridge> bridge;
    bool HasMethod(std::string_view name) const override { return object->HasMethod(name); }
    bool HasProperty(std::string_view name) const override { return object->HasProperty(name); }
    Vice::Value Get(std::string_view name) const override;
    void Set(std::string_view name, const Vice::Value& value) override;
    Vice::Value Call(std::string_view name, std::span<const Vice::Value> arguments) override;
};

// A Vice function or object handed to the engine, such as an event callback or the component itself. Every
// operation enters the script's runtime through its invocation scope.
struct ViceForeign final : ScriptObject {
    Vice::ObjectRef object;
    std::shared_ptr<Bridge> bridge;
    template <typename F>
    auto Run(F&& operation) const;
    bool HasMethod(std::string_view name) const override;
    bool HasProperty(std::string_view name) const override;
    ScriptValue Get(std::string_view name) const override;
    void Set(std::string_view name, const ScriptValue& value) override;
    ScriptValue Call(std::string_view name, std::span<const ScriptValue> arguments) override;
    bool IsCallable() const override { return object->IsCallable(); }
    ScriptValue Invoke(std::span<const ScriptValue> arguments) override;
};

struct Bridge : std::enable_shared_from_this<Bridge> {
    std::unordered_map<const ScriptObject*, std::weak_ptr<Vice::Object>> objects;
    std::unordered_map<const Vice::Object*, std::weak_ptr<ScriptObject>> foreign;
    // Cleared when the runtime ends, after which exposed functions and objects can no longer run.
    const ScriptRuntime* runtime = nullptr;

    ScriptObjectRef Expose(const Vice::ObjectRef& object) {
        if (const auto found = foreign.find(object.get()); found != foreign.end()) {
            if (auto existing = found->second.lock())
                return existing;
        }
        auto wrapper = std::make_shared<ViceForeign>();
        wrapper->object = object;
        wrapper->bridge = shared_from_this();
        foreign.insert_or_assign(object.get(), wrapper);
        return wrapper;
    }
    ScriptValue FromVice(const Vice::Value& value) {
        return std::visit(
            [this](const auto& item) -> ScriptValue {
                using T = std::remove_cvref_t<decltype(item)>;
                if constexpr (std::is_same_v<T, Vice::Vector2>)
                    return Vector2{Detail::FromScriptValue<float>(item.x), Detail::FromScriptValue<float>(item.y)};
                else if constexpr (std::is_same_v<T, Vice::Vector3>)
                    return Vector3{Detail::FromScriptValue<float>(item.x), Detail::FromScriptValue<float>(item.y),
                                   Detail::FromScriptValue<float>(item.z)};
                else if constexpr (std::is_same_v<T, Vice::Color>)
                    return Color{Detail::FromScriptValue<float>(item.r), Detail::FromScriptValue<float>(item.g),
                                 Detail::FromScriptValue<float>(item.b), Detail::FromScriptValue<float>(item.a)};
                else if constexpr (std::is_same_v<T, Vice::ObjectRef>) {
                    if (!item)
                        return {};
                    if (const auto wrapper = std::dynamic_pointer_cast<ViceObject>(item))
                        return wrapper->object;
                    return Expose(item);
                } else
                    return item;
            },
            value);
    }
    Vice::Value ToVice(const ScriptValue& value) {
        return std::visit(
            [this](const auto& item) -> Vice::Value {
                using T = std::remove_cvref_t<decltype(item)>;
                if constexpr (std::is_same_v<T, Vector2>)
                    return Vice::Vector2{item.x, item.y};
                else if constexpr (std::is_same_v<T, Vector3>)
                    return Vice::Vector3{item.x, item.y, item.z};
                else if constexpr (std::is_same_v<T, Color>)
                    return Vice::Color{item.r, item.g, item.b, item.a};
                else if constexpr (std::is_same_v<T, ScriptObjectRef>) {
                    if (!item)
                        return {};
                    // A value this script exposed comes back as itself, keeping its identity.
                    if (const auto exposed = std::dynamic_pointer_cast<ViceForeign>(item);
                        exposed && exposed->bridge.get() == this)
                        return exposed->object;
                    if (const auto found = objects.find(item.get()); found != objects.end()) {
                        if (auto existing = found->second.lock())
                            return existing;
                    }
                    auto wrapper = std::make_shared<ViceObject>();
                    wrapper->object = item;
                    wrapper->bridge = shared_from_this();
                    objects.insert_or_assign(item.get(), wrapper);
                    return Vice::ObjectRef(wrapper);
                } else
                    return item;
            },
            value);
    }
    std::vector<ScriptValue> Arguments(std::span<const Vice::Value> arguments) {
        std::vector<ScriptValue> converted;
        converted.reserve(arguments.size());
        for (const auto& argument : arguments)
            converted.push_back(FromVice(argument));
        return converted;
    }
};

template <typename F>
auto ViceForeign::Run(F&& operation) const {
    if (!bridge->runtime)
        throw std::runtime_error("The script instance is no longer available");
    std::optional<decltype(operation())> result;
    bridge->runtime->RunInvocation([&] { result.emplace(operation()); });
    return std::move(*result);
}
bool ViceForeign::HasMethod(std::string_view name) const {
    return Run([&] { return object->HasMethod(name); });
}
bool ViceForeign::HasProperty(std::string_view name) const {
    return Run([&] { return object->HasProperty(name); });
}
ScriptValue ViceForeign::Get(std::string_view name) const {
    return Run([&] { return bridge->FromVice(object->Get(name)); });
}
void ViceForeign::Set(std::string_view name, const ScriptValue& value) {
    Run([&] {
        object->Set(name, bridge->ToVice(value));
        return true;
    });
}
ScriptValue ViceForeign::Call(std::string_view name, std::span<const ScriptValue> arguments) {
    return Run([&] {
        std::vector<Vice::Value> converted;
        for (const auto& argument : arguments)
            converted.push_back(bridge->ToVice(argument));
        return bridge->FromVice(object->Call(name, converted));
    });
}
ScriptValue ViceForeign::Invoke(std::span<const ScriptValue> arguments) {
    return Run([&] {
        std::vector<Vice::Value> converted;
        for (const auto& argument : arguments)
            converted.push_back(bridge->ToVice(argument));
        return bridge->FromVice(object->Invoke(converted));
    });
}

Vice::Value ViceObject::Get(std::string_view name) const {
    return bridge->ToVice(object->Get(name));
}
void ViceObject::Set(std::string_view name, const Vice::Value& value) {
    object->Set(name, bridge->FromVice(value));
}
Vice::Value ViceObject::Call(std::string_view name, std::span<const Vice::Value> arguments) {
    return bridge->ToVice(object->Call(name, bridge->Arguments(arguments)));
}

struct ComponentBase final : Vice::Object {
    Vice::ObjectRef object;
    bool HasProperty(std::string_view name) const override { return object->HasProperty(name); }
    bool HasMethod(std::string_view name) const override {
        for (const auto& hook : ScriptApi::Hooks) {
            if (name == hook.name)
                return false;
        }
        return object->HasMethod(name);
    }
    Vice::Value Get(std::string_view name) const override { return object->Get(name); }
    void Set(std::string_view name, const Vice::Value& value) override { object->Set(name, value); }
    Vice::Value Call(std::string_view name, std::span<const Vice::Value> arguments) override {
        for (const auto& hook : ScriptApi::Hooks) {
            if (name == hook.name)
                return {};
        }
        return object->Call(name, arguments);
    }
};

class ViceRuntime final : public ScriptRuntime {
public:
    ~ViceRuntime() override { m_bridge->runtime = nullptr; }
    ViceRuntime(std::string_view text, std::string_view name) : m_instance(Vice::Program::Parse(text, name)) {
        m_bridge->runtime = this;
        m_instance.Bind("print", [](std::span<const Vice::Value> arguments) -> Vice::Value {
            std::string message;
            bool first = true;
            for (const auto& argument : arguments) {
                if (!first)
                    message += ' ';
                message += Vice::ToString(argument);
                first = false;
            }
            Detail::WriteScriptLog("ViceScript", message);
            return {};
        });
    }
    void BindLibrary(std::string_view name, const ScriptObjectRef& library) override {
        m_instance.BindLibrary(std::string(name), std::get<Vice::ObjectRef>(m_bridge->ToVice(library)));
        if (name == ScriptApi::Module) {
            auto base = std::make_shared<ComponentBase>();
            base->object = std::get<Vice::ObjectRef>(m_bridge->ToVice(library->Get(ScriptApi::Component)));
            std::unordered_map<std::string, std::size_t> signatures{
                {"GetEntity", 0},  {"GetScene", 0},           {"GetId", 0},     {"IsEnabled", 0},
                {"SetEnabled", 1}, {"IsActiveAndEnabled", 0}, {"IsFaulted", 0}, {"IsValid", 0}};
            for (const auto& hook : ScriptApi::Hooks)
                signatures.emplace(hook.name, hook.parameterCount);
            m_instance.BindClass(std::string(ScriptApi::Component), base, std::move(signatures));
        }
    }
    void SetSourceLoader(ScriptSourceLoader loader) override {
        m_instance.SetModuleLoader(
            [loader = std::move(loader)](std::string_view request,
                                         std::string_view importer) -> std::optional<Vice::ModuleSource> {
                auto source = loader(Detail::ResolveScriptModulePath(request, ".vice"), importer);
                if (!source)
                    return std::nullopt;
                return Vice::ModuleSource{std::move(source->source), std::move(source->sourceName)};
            });
    }
    bool HasFunction(std::string_view name) const override { return m_component && m_component->HasMethod(name); }
    void ValidateFunction(std::string_view, std::size_t) const override {
        // The host class constrains hook signatures before global or field initializers execute.
    }
    void Initialize() override {
        m_instance.Initialize();
        m_component = m_instance.CreateDerivedInstance(ScriptApi::Component);
    }
    ScriptObjectRef GetObject() override { return m_component ? m_bridge->Expose(m_component) : nullptr; }
    std::vector<std::pair<std::string, ScriptValue>> CaptureState() override {
        std::vector<std::pair<std::string, ScriptValue>> state;
        if (!m_component)
            return state;
        for (auto& name : m_component->GetFieldNames()) {
            try {
                auto value = m_bridge->FromVice(m_component->Get(name));
                // Script objects, collections and functions belong to the runtime that is going away.
                if (const auto* object = std::get_if<ScriptObjectRef>(&value);
                    object && std::dynamic_pointer_cast<ViceForeign>(*object))
                    continue;
                state.emplace_back(std::move(name), std::move(value));
            } catch (const std::exception&) {
                // Values without an engine form, such as a field still initializing, start over.
            }
        }
        return state;
    }
    void SetBreakpoints(const std::map<std::string, std::vector<std::size_t>>& breakpoints) override {
        m_instance.ClearBreakpoints();
        for (const auto& [source, lines] : breakpoints)
            m_instance.SetBreakpoints(source, lines);
        // Without breakpoints, no handler keeps execution at full speed.
        if (breakpoints.empty()) {
            m_instance.SetDebugHandler({});
            return;
        }
        m_instance.SetDebugHandler([](const Vice::DebugEvent& event) {
            ScriptBreak stop;
            for (const auto& frame : event.frames) {
                ScriptDebugFrame entry{frame.function, frame.sourceName, frame.location.line, {}};
                for (const auto& [name, value] : frame.locals)
                    entry.locals.emplace_back(name, Vice::ToString(value));
                stop.frames.push_back(std::move(entry));
            }
            Detail::ReportScriptBreak(stop);
            return Vice::DebugAction::Continue;
        });
    }
    void SetProfiling(bool enabled) override { m_instance.SetProfiling(enabled); }
    std::vector<ScriptFunctionProfile> TakeProfile() override {
        std::vector<ScriptFunctionProfile> profile;
        for (auto& function : m_instance.GetProfile()) {
            if (function.calls == 0 && function.instructions == 0)
                continue;
            profile.push_back({std::move(function.name), std::move(function.sourceName), function.line, function.calls,
                               function.instructions, function.allocations, function.time});
        }
        m_instance.ResetProfile();
        return profile;
    }
    ScriptObjectRef CreateCoroutine(const ScriptObjectRef& function) override {
        const auto value = m_bridge->ToVice(function);
        const auto* object = std::get_if<Vice::ObjectRef>(&value);
        if (!object || !*object)
            throw std::runtime_error("Expected a function to run as a coroutine");
        return m_bridge->Expose(m_instance.CreateCoroutine(*object));
    }
    void Call(std::string_view name, std::span<const ScriptValue> arguments) override {
        if (!m_component)
            throw std::logic_error("Initialize before calling component hooks");
        if (m_running && name != "OnEnable" && name != "OnDisable")
            throw std::logic_error("A host callback cannot reenter its script instance");
        struct Restore {
            bool& target;
            bool previous;
            ~Restore() { target = previous; }
        } restore{m_running, m_running};
        m_running = true;
        std::erase_if(m_bridge->objects, [](const auto& item) { return item.second.expired(); });
        std::erase_if(m_bridge->foreign, [](const auto& item) { return item.second.expired(); });
        std::vector<Vice::Value> converted;
        converted.reserve(arguments.size());
        for (const auto& argument : arguments)
            converted.push_back(m_bridge->ToVice(argument));
        m_component->Call(name, converted);
    }

private:
    std::shared_ptr<Bridge> m_bridge = std::make_shared<Bridge>();
    Vice::Instance m_instance;
    Vice::ObjectRef m_component;
    bool m_running = false;
};

} // namespace

std::unique_ptr<ScriptRuntime> Detail::CreateViceRuntime(std::string_view text, std::string_view name) {
    return std::make_unique<ViceRuntime>(text, name);
}

std::optional<ScriptDiagnostic> Detail::CheckViceSyntax(std::string_view text, std::string_view name) {
    try {
        Vice::Program::Parse(text, name);
        return std::nullopt;
    } catch (const Vice::Error& error) {
        // Messages start with "name:line:column: ".
        std::string message = error.what();
        const auto prefix = std::string(name) + ":" + std::to_string(error.GetLocation().line) + ":" +
                            std::to_string(error.GetLocation().column) + ": ";
        if (message.starts_with(prefix))
            message.erase(0, prefix.size());
        return ScriptDiagnostic{error.GetLocation().line, error.GetLocation().column, std::move(message)};
    }
}

std::vector<ScriptExport> Detail::ReadViceExports(std::string_view text, std::string_view name) {
    const auto bridge = std::make_shared<Bridge>();
    std::vector<ScriptExport> exports;
    for (auto& field : Vice::Program::Parse(text, name).GetExportedFields()) {
        if (std::ranges::none_of(exports, [&](const ScriptExport& item) { return item.name == field.name; }))
            exports.push_back({std::move(field.name), bridge->FromVice(field.value)});
    }
    return exports;
}

} // namespace Spot
