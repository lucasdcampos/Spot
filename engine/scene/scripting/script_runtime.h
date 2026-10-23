#pragma once

#include "assets/script_source.h"
#include "scene/scripting/script_bindings.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Spot {

// One instance per component. Adapters translate values/objects and preserve object identity.
// Libraries are explicit runtime modules. Source loaders return imported text, never raw engine source files.
// Configure both before Initialize. Call failures propagate as exceptions.
using ScriptSourceLoader =
    std::function<std::optional<ScriptSource>(std::string_view request, std::string_view importer)>;
// What profiling recorded for one script function: its own execution time and instructions, without the script
// functions it called, how often it was called, and the script objects and collections it allocated.
struct ScriptFunctionProfile {
    std::string name;
    std::string sourceName;
    std::size_t line = 0;
    std::uint64_t calls = 0;
    std::uint64_t instructions = 0;
    std::uint64_t allocations = 0;
    std::chrono::nanoseconds time{};
};

// A script call stopped at a breakpoint, for tools: innermost first, with its live locals formatted as text.
struct ScriptDebugFrame {
    std::string function;
    std::string sourceName;
    std::size_t line = 0;
    std::vector<std::pair<std::string, std::string>> locals;
};
struct ScriptBreak {
    std::vector<ScriptDebugFrame> frames;
};
using ScriptBreakHandler = std::function<void(const ScriptBreak& stop)>;

// Wraps calls entering a runtime from outside its own hooks, such as event callbacks or another script calling its
// methods, so they run with the owning component's scene access.
using ScriptInvocationScope = std::function<void(const std::function<void()>& call)>;

class ScriptRuntime {
public:
    virtual ~ScriptRuntime() = default;
    virtual void BindLibrary(std::string_view name, const ScriptObjectRef& library) = 0;
    virtual void SetSourceLoader(ScriptSourceLoader loader) = 0;
    // Queried after Initialize so dynamic languages can discover functions by executing their module.
    virtual bool HasFunction(std::string_view name) const = 0;
    // Missing hooks are valid. Check signatures before initializers when supported; dynamic runtimes can
    // retain the expected arity and check at call time according to their own calling convention.
    virtual void ValidateFunction(std::string_view name, std::size_t parameterCount) const = 0;
    virtual void Initialize() = 0;
    virtual void Call(std::string_view name, std::span<const ScriptValue> arguments = {}) = 0;
    // The script's own object, through which other scripts and C++ read its fields and call its methods: the
    // component instance for class-based languages, the module's globals for others. Null when unsupported.
    virtual ScriptObjectRef GetObject() { return nullptr; }
    // Wraps one of the script's functions, called without arguments, in a coroutine the engine resumes: an object
    // whose Call("resume") runs it until it yields or returns, giving back that value, and whose "done" property
    // tells whether it returned. Null when the language has no coroutines.
    virtual ScriptObjectRef CreateCoroutine(const ScriptObjectRef& /*function*/) { return nullptr; }
    // The script's state that survives reloading its code: values that do not belong to the runtime, such as
    // numbers, strings, vectors and engine references, by field name. Empty when the language keeps none.
    virtual std::vector<std::pair<std::string, ScriptValue>> CaptureState() { return {}; }
    // Breakpoints by source name, for languages with a debugger; hits go to Detail::ReportScriptBreak().
    virtual void SetBreakpoints(const std::map<std::string, std::vector<std::size_t>>& /*breakpoints*/) {}
    // Per-function profiling, for languages that support it. TakeProfile() returns what ran since the last call.
    virtual void SetProfiling(bool /*enabled*/) {}
    virtual std::vector<ScriptFunctionProfile> TakeProfile() { return {}; }

    void SetInvocationScope(ScriptInvocationScope scope) { m_invocationScope = std::move(scope); }
    // Adapters run foreign calls into their language, from objects and functions they exposed, through this.
    void RunInvocation(const std::function<void()>& call) const {
        if (m_invocationScope)
            m_invocationScope(call);
        else
            call();
    }

private:
    ScriptInvocationScope m_invocationScope;
};

using ScriptRuntimeFactory =
    std::function<std::unique_ptr<ScriptRuntime>(std::string_view source, std::string_view sourceName)>;

// A field a script exposes to tools, such as ViceScript's `export var speed = 3`, with its default value: a
// boolean, number, string, Vector2, Vector3 or Color. ScriptComponent shows them in the inspector and saves edits.
struct ScriptExport {
    std::string name;
    ScriptValue value;
};
// The first syntax error of a source, with its one-based line and column (0 when the language gives none).
struct ScriptDiagnostic {
    std::size_t line = 0;
    std::size_t column = 0;
    std::string message;
};
// Parses a source without running it; returns nothing when it is valid.
using ScriptSyntaxChecker =
    std::function<std::optional<ScriptDiagnostic>(std::string_view source, std::string_view sourceName)>;

// Reads a source's exports without running it; throws on syntax errors.
using ScriptExportReader =
    std::function<std::vector<ScriptExport>(std::string_view source, std::string_view sourceName)>;

// Names and extensions are case insensitive. Register before scenes run. Re-registering a language atomically
// replaces its factory and extensions; another language cannot claim an extension already in use.
// An empty extension list registers an embedded-source-only runtime.
void RegisterScriptLanguage(std::string name, std::vector<std::string> extensions, ScriptRuntimeFactory factory);
void UnregisterScriptLanguage(std::string_view name);
// Lets tools list a registered language's exports. Registering the language again clears its reader.
void SetScriptExportReader(std::string_view language, ScriptExportReader reader);
// Lets tools check a registered language's sources as they are edited. Registering the language again clears it.
void SetScriptSyntaxChecker(std::string_view language, ScriptSyntaxChecker checker);
// Nothing for valid sources and for languages without a checker.
std::optional<ScriptDiagnostic> CheckScriptSyntax(std::string_view language, std::string_view source,
                                                  std::string_view sourceName);
// Empty for languages without a reader.
std::vector<ScriptExport> GetScriptExports(std::string_view language, std::string_view source,
                                           std::string_view sourceName);
// Unknown languages/extensions throw; a missing runtime never falls back to a different language.
std::string GetScriptLanguageForSource(std::string_view sourceName);
std::unique_ptr<ScriptRuntime> CreateScriptRuntime(std::string_view language, std::string_view source,
                                                   std::string_view sourceName);

// Profiles the functions of every running script, adding up each function's figures across components: while on,
// script components turn their runtime's profiling on and collect it after each hook. Off by default, as it costs
// a clock read per instruction. Hooks are always timed by the engine's Profiler, as "<script>.<hook>" scopes.
void SetScriptProfiling(bool enabled);
bool IsScriptProfiling();
// What was collected since profiling started or was last reset, most time first.
std::vector<ScriptFunctionProfile> GetScriptProfile();
void ResetScriptProfile();

// Breakpoints in script sources, by their source name ("scripts/player.vice"), with one-based lines; an empty list
// removes a source's. Running scripts take changes before their next hook. A hit calls the break handler with the
// call stack and continues: the engine never blocks a frame, so tools pause the game themselves, as the editor does.
void SetScriptBreakpoints(std::string sourceName, std::vector<std::size_t> lines);
std::vector<std::size_t> GetScriptBreakpoints(std::string_view sourceName);
void SetScriptBreakHandler(ScriptBreakHandler handler);

namespace Detail {
void AddScriptProfile(std::span<const ScriptFunctionProfile> profile);
std::uint64_t GetScriptDebugRevision();
std::map<std::string, std::vector<std::size_t>> GetAllScriptBreakpoints();
void ReportScriptBreak(const ScriptBreak& stop);
} // namespace Detail

// Native engine libraries are described once and installed in every runtime, with fresh objects per component.
using ScriptLibraryFactory = std::function<ScriptObjectRef()>;
void RegisterScriptLibrary(std::string name, ScriptLibraryFactory factory);
void UnregisterScriptLibrary(std::string_view name);

namespace Detail {
void BindRegisteredScriptLibraries(ScriptRuntime& runtime);
std::string ResolveScriptModulePath(std::string_view request, std::string_view defaultExtension,
                                    bool allowDotSeparators = false);
void WriteScriptLog(std::string_view loggerName, std::string_view message);
} // namespace Detail

} // namespace Spot
