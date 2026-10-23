#include "scene/scripting/luau_runtime.h"

#include "scene/scripting/script_api.h"

#include <Luau/Compiler.h>
#include <Luau/Parser.h>
#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Spot {
namespace {

constexpr const char* ObjectsRegistryKey = "Spot.LuauObjects";
constexpr const char* ModulesRegistryKey = "Spot.LuauModules";
constexpr const char* BuiltinsRegistryKey = "Spot.LuauBuiltins";
constexpr const char* DeclaredRegistryKey = "Spot.LuauDeclared";
constexpr int StructTag = 1;
constexpr int ObjectTag = 2;
// Interrupts come at loop back edges and calls, so this bounds the iterations of a runaway hook.
constexpr std::uint64_t MaxInterruptsPerCall = 1000000;
constexpr std::size_t MaxModuleDepth = 32;

// Luau's own libraries, which are safe in a sandbox; os and debug are left out, as the engine gives time and
// diagnostics itself.
constexpr std::array<std::pair<const char*, lua_CFunction>, 9> StandardLibraries{{
    {"", luaopen_base},
    {LUA_COLIBNAME, luaopen_coroutine},
    {LUA_TABLIBNAME, luaopen_table},
    {LUA_STRLIBNAME, luaopen_string},
    {LUA_MATHLIBNAME, luaopen_math},
    {LUA_UTF8LIBNAME, luaopen_utf8},
    {LUA_BITLIBNAME, luaopen_bit32},
    {LUA_BUFFERLIBNAME, luaopen_buffer},
    {LUA_VECLIBNAME, luaopen_vector},
}};

bool IsStandardLibraryName(std::string_view name) {
    return name == "base" || name == "os" || name == "debug" ||
           std::ranges::any_of(StandardLibraries, [&](const auto& library) { return name == library.first; });
}

class LuauRuntime;

struct LuauObject {
    ScriptObjectRef object;
    LuauRuntime* runtime = nullptr;
};

struct LuauStruct {
    ScriptValue value;
    std::weak_ptr<ScriptObject> owner;
    std::string property;
};

[[noreturn]] void RaiseLuauError(lua_State* state, std::string_view message) {
    luaL_where(state, 1);
    lua_pushlstring(state, message.data(), message.size());
    lua_concat(state, 2);
    lua_error(state);
}

std::string_view ToKey(lua_State* state, int index) {
    if (lua_type(state, index) != LUA_TSTRING)
        return {};
    std::size_t length = 0;
    const char* key = lua_tolstring(state, index, &length);
    return {key, length};
}

LuauStruct* ToStruct(lua_State* state, int index) {
    return static_cast<LuauStruct*>(lua_touserdatatagged(state, index, StructTag));
}
LuauObject* ToObject(lua_State* state, int index) {
    return static_cast<LuauObject*>(lua_touserdatatagged(state, index, ObjectTag));
}

// A Luau function handed to the engine, such as an event callback, kept in the registry while referenced.
struct LuauFunction final : ScriptObject {
    LuauFunction(LuauRuntime& runtime, std::weak_ptr<void> life, int reference)
        : runtime(&runtime), life(std::move(life)), reference(reference) {}
    ~LuauFunction() override;
    bool HasMethod(std::string_view) const override { return false; }
    bool IsCallable() const override { return true; }
    ScriptValue Invoke(std::span<const ScriptValue> arguments) override;
    LuauRuntime* runtime;
    std::weak_ptr<void> life;
    int reference;
};

// A Luau thread running one of the script's functions for the engine; see ScriptRuntime::CreateCoroutine().
struct LuauCoroutine final : ScriptObject {
    LuauCoroutine(LuauRuntime& runtime, std::weak_ptr<void> life, int thread)
        : runtime(&runtime), life(std::move(life)), thread(thread) {}
    ~LuauCoroutine() override;
    bool HasMethod(std::string_view name) const override { return name == "resume"; }
    bool HasProperty(std::string_view name) const override { return name == "done"; }
    ScriptValue Get(std::string_view name) const override {
        if (name == "done")
            return done;
        return ScriptObject::Get(name);
    }
    ScriptValue Call(std::string_view name, std::span<const ScriptValue> arguments) override;
    LuauRuntime* runtime;
    std::weak_ptr<void> life;
    int thread;
    bool done = false;
};

// The script's globals, as other scripts and C++ see them: functions are methods, other values properties.
struct LuauGlobals final : ScriptObject {
    LuauGlobals(LuauRuntime& runtime, std::weak_ptr<void> life) : runtime(&runtime), life(std::move(life)) {}
    LuauRuntime& Runtime() const;
    bool HasMethod(std::string_view name) const override;
    bool HasProperty(std::string_view name) const override;
    ScriptValue Get(std::string_view name) const override;
    void Set(std::string_view name, const ScriptValue& value) override;
    ScriptValue Call(std::string_view name, std::span<const ScriptValue> arguments) override;
    LuauRuntime* runtime;
    std::weak_ptr<void> life;
};

// Compiles a source for luau_load(); a syntax error gives bytecode that makes luau_load() fail with its message.
// Optimization level 1 keeps every line breakable, and debug level 2 keeps the names of locals for breakpoints.
std::string Compile(std::string_view text) {
    Luau::CompileOptions options;
    options.optimizationLevel = 1;
    options.debugLevel = 2;
    return Luau::compile(std::string(text), options);
}

// What a function profile is filed under: functions are told apart by where they are defined.
struct ProfileKey {
    std::string sourceName;
    int line = 0;
    std::string name;
    auto operator<=>(const ProfileKey&) const = default;
};

// The value of a constant expression, as a field's default: numbers, strings, booleans and value types built from
// numbers. Nothing for anything that would have to run.
std::optional<ScriptValue> ReadConstant(const Luau::AstExpr* expression) {
    if (const auto* group = expression->as<Luau::AstExprGroup>())
        return ReadConstant(group->expr);
    if (const auto* assertion = expression->as<Luau::AstExprTypeAssertion>())
        return ReadConstant(assertion->expr);
    if (const auto* number = expression->as<Luau::AstExprConstantNumber>())
        return number->value;
    if (const auto* boolean = expression->as<Luau::AstExprConstantBool>())
        return boolean->value;
    if (const auto* text = expression->as<Luau::AstExprConstantString>())
        return std::string(text->value.data, text->value.size);
    if (const auto* unary = expression->as<Luau::AstExprUnary>(); unary && unary->op == Luau::AstExprUnary::Op::Minus) {
        if (const auto value = ReadConstant(unary->expr); value && std::holds_alternative<double>(*value))
            return -std::get<double>(*value);
        return std::nullopt;
    }
    if (const auto* call = expression->as<Luau::AstExprCall>()) {
        const auto* constructor = call->func->as<Luau::AstExprGlobal>();
        if (!constructor || call->self || !ScriptApi::IsValueConstructor(constructor->name.value))
            return std::nullopt;
        std::vector<ScriptValue> arguments;
        for (const Luau::AstExpr* argument : call->args) {
            const auto value = ReadConstant(argument);
            if (!value || !std::holds_alternative<double>(*value))
                return std::nullopt;
            arguments.push_back(*value);
        }
        try {
            return Detail::ConstructScriptStruct(constructor->name.value, arguments);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

class LuauRuntime final : public ScriptRuntime {
public:
    LuauRuntime(std::string_view text, std::string_view name) : m_name(name) {
        m_state = luaL_newstate();
        if (!m_state)
            throw std::runtime_error("Could not create Luau state");

        try {
            SetUpState();
            const std::string bytecode = Compile(text);
            const std::string chunkName = "@" + m_name;
            if (luau_load(m_state, chunkName.c_str(), bytecode.data(), bytecode.size(), 0) != LUA_OK) {
                const char* error = lua_tostring(m_state, -1);
                throw std::runtime_error(error ? error : "Failed to parse Luau script");
            }
            m_mainChunkRef = lua_ref(m_state, -1);
            AddChunk(m_state, m_name);
            lua_pop(m_state, 1);
        } catch (...) {
            lua_close(m_state);
            m_state = nullptr;
            throw;
        }
    }

    ~LuauRuntime() override {
        m_life.reset();
        if (m_state) {
            lua_close(m_state);
            m_state = nullptr;
        }
    }

    void BindLibrary(std::string_view name, const ScriptObjectRef& library) override {
        if (m_initialized)
            throw std::runtime_error("Cannot bind a script library after initialization");
        if (name.empty() || !library)
            throw std::invalid_argument("A script library needs a name and object");
        if (IsStandardLibraryName(name) || m_libraries.contains(std::string(name)))
            throw std::runtime_error("Script library '" + std::string(name) + "' is already bound");
        m_libraries.emplace(name, library);
    }

    void SetSourceLoader(ScriptSourceLoader loader) override { m_loader = std::move(loader); }

    bool HasFunction(std::string_view name) const override {
        return m_initialized && GlobalType(name) == LUA_TFUNCTION;
    }

    void ValidateFunction(std::string_view name, std::size_t parameterCount) const override {
        m_expectedHooks.insert_or_assign(std::string(name), parameterCount);
    }

    void Initialize() override {
        if (m_initialized)
            throw std::runtime_error("Luau runtime is already initialized");
        const RunScope scope(*this);
        m_moduleStack.push_back(m_name);
        struct PopModule {
            std::vector<std::string>& stack;
            ~PopModule() { stack.pop_back(); }
        } pop{m_moduleStack};

        lua_getref(m_state, m_mainChunkRef);
        lua_unref(m_state, m_mainChunkRef);
        m_mainChunkRef = LUA_NOREF;

        ProtectedCall(0, 0);
        ValidateHooks();
        m_initialized = true;
    }

    void Call(std::string_view name, std::span<const ScriptValue> arguments) override {
        if (!m_initialized)
            throw std::runtime_error("Luau runtime is not initialized");
        const RunScope scope(*this, name == "OnEnable" || name == "OnDisable");
        if (!PushGlobal(name)) {
            lua_pop(m_state, 1);
            throw std::runtime_error("Unknown script function '" + std::string(name) + "'");
        }
        for (const auto& argument : arguments)
            PushValue(m_state, argument);
        ProtectedCall(static_cast<int>(arguments.size()), 0);
    }

    ScriptObjectRef CreateCoroutine(const ScriptObjectRef& function) override {
        const auto* luau = dynamic_cast<const LuauFunction*>(function.get());
        if (!luau || luau->runtime != this)
            throw std::runtime_error("Expected a function of this script to run as a coroutine");
        lua_State* thread = lua_newthread(m_state);
        lua_getref(thread, luau->reference);
        const int reference = lua_ref(m_state, -1);
        lua_pop(m_state, 1);
        return std::make_shared<LuauCoroutine>(*this, m_life, reference);
    }
    // Resumes the registry thread; returns what it yielded or returned and whether it finished.
    ScriptValue ResumeThread(int reference, bool& finished) {
        ScriptValue result;
        RunInvocation([&] {
            const RunScope scope(*this, true);
            lua_getref(m_state, reference);
            lua_State* thread = lua_tothread(m_state, -1);
            lua_pop(m_state, 1);
            const int status = lua_resume(thread, m_state, 0);
            if (status != LUA_OK && status != LUA_YIELD) {
                finished = true;
                const char* message = lua_tostring(thread, -1);
                const std::string error = message ? message : "Luau coroutine failed";
                lua_settop(thread, 0);
                throw std::runtime_error(error);
            }
            finished = status == LUA_OK;
            const int results = lua_gettop(thread);
            struct Clear {
                lua_State* thread;
                ~Clear() { lua_settop(thread, 0); }
            } clear{thread};
            if (results > 0)
                result = ToScriptValue(thread, -results);
        });
        return result;
    }
    void ReleaseReference(int reference) { lua_unref(m_state, reference); }

    ScriptObjectRef GetObject() override {
        return m_initialized ? std::make_shared<LuauGlobals>(*this, m_life) : nullptr;
    }

    // The script's globals that hold values of their own; functions and tables belong to the old code.
    std::vector<std::pair<std::string, ScriptValue>> CaptureState() override {
        std::vector<std::pair<std::string, ScriptValue>> state;
        lua_pushnil(m_state);
        while (lua_next(m_state, LUA_GLOBALSINDEX) != 0) {
            const int type = lua_type(m_state, -1);
            if (lua_type(m_state, -2) == LUA_TSTRING && type != LUA_TFUNCTION && type != LUA_TTABLE) {
                try {
                    state.emplace_back(ToKey(m_state, -2), ToScriptValue(m_state, -1));
                } catch (const std::exception&) {
                    // A value the engine cannot hold, such as a thread, starts over with the new code.
                }
            }
            lua_pop(m_state, 1);
        }
        return state;
    }

    void SetBreakpoints(const std::map<std::string, std::vector<std::size_t>>& breakpoints) override {
        m_requestedBreakpoints = breakpoints;
        for (const auto& [source, reference] : m_chunks)
            ApplyBreakpoints(source, reference);
    }

    void SetProfiling(bool enabled) override {
        m_profiling = enabled;
        lua_callbacks(m_state)->onallocate = enabled ? &OnAllocate : nullptr;
    }
    std::vector<ScriptFunctionProfile> TakeProfile() override {
        std::vector<ScriptFunctionProfile> profile;
        profile.reserve(m_profile.size());
        for (auto& [key, function] : m_profile)
            profile.push_back(std::move(function));
        m_profile.clear();
        m_profileByProto.clear();
        return profile;
    }

    // Runs a registry function, or a global one when the reference is LUA_NOREF, in the script's invocation scope.
    ScriptValue InvokeForeign(int reference, std::string_view global, std::span<const ScriptValue> arguments) {
        ScriptValue result;
        RunInvocation([&] {
            const RunScope scope(*this, true);
            if (reference != LUA_NOREF) {
                lua_getref(m_state, reference);
            } else if (!PushGlobal(global)) {
                lua_pop(m_state, 1);
                throw std::runtime_error("Unknown script function '" + std::string(global) + "'");
            }
            for (const auto& argument : arguments)
                PushValue(m_state, argument);
            ProtectedCall(static_cast<int>(arguments.size()), 1);
            struct Pop {
                lua_State* state;
                ~Pop() { lua_pop(state, 1); }
            } pop{m_state};
            result = ToScriptValue(m_state, -1);
        });
        return result;
    }
    // Whether the script assigned a global, even if only nil, which leaves no value behind.
    bool IsDeclared(std::string_view name) const {
        lua_getfield(m_state, LUA_REGISTRYINDEX, DeclaredRegistryKey);
        lua_pushlstring(m_state, name.data(), name.size());
        const bool declared = lua_rawget(m_state, -2) != LUA_TNIL;
        lua_pop(m_state, 2);
        return declared;
    }
    int GlobalType(std::string_view name) const {
        lua_pushlstring(m_state, name.data(), name.size());
        const int type = lua_rawget(m_state, LUA_GLOBALSINDEX);
        lua_pop(m_state, 1);
        return type;
    }
    ScriptValue GetGlobal(std::string_view name) {
        ScriptValue result;
        RunInvocation([&] {
            lua_pushlstring(m_state, name.data(), name.size());
            lua_rawget(m_state, LUA_GLOBALSINDEX);
            struct Pop {
                lua_State* state;
                ~Pop() { lua_pop(state, 1); }
            } pop{m_state};
            result = ToScriptValue(m_state, -1);
        });
        return result;
    }
    void SetGlobal(std::string_view name, const ScriptValue& value) {
        RunInvocation([&] {
            lua_pushlstring(m_state, name.data(), name.size());
            PushValue(m_state, value);
            lua_rawset(m_state, LUA_GLOBALSINDEX);
        });
    }
    lua_State* GetState() const { return m_state; }

    void PushValue(lua_State* state, const ScriptValue& value, const ScriptObjectRef& owner = {},
                   std::string_view property = {}) {
        std::visit(
            [&](const auto& item) {
                using T = std::remove_cvref_t<decltype(item)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    lua_pushnil(state);
                } else if constexpr (std::is_same_v<T, bool>) {
                    lua_pushboolean(state, item ? 1 : 0);
                } else if constexpr (std::is_same_v<T, double>) {
                    lua_pushnumber(state, item);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    lua_pushlstring(state, item.data(), item.size());
                } else if constexpr (std::is_same_v<T, Vector2> || std::is_same_v<T, Vector3> ||
                                     std::is_same_v<T, Color>) {
                    void* memory = lua_newuserdatataggedwithmetatable(state, sizeof(LuauStruct), StructTag);
                    new (memory) LuauStruct{value, owner, std::string(property)};
                } else if constexpr (std::is_same_v<T, ScriptObjectRef>) {
                    PushObject(state, item);
                }
            },
            value);
    }

    ScriptValue ToScriptValue(lua_State* state, int index) {
        const int absIndex = lua_absindex(state, index);
        switch (lua_type(state, absIndex)) {
        case LUA_TNIL:
        case LUA_TNONE:
            return {};
        case LUA_TBOOLEAN:
            return lua_toboolean(state, absIndex) != 0;
        case LUA_TNUMBER:
            return lua_tonumber(state, absIndex);
        case LUA_TSTRING: {
            std::size_t length = 0;
            const char* text = lua_tolstring(state, absIndex, &length);
            return std::string(text, length);
        }
        case LUA_TFUNCTION:
            return ScriptObjectRef(std::make_shared<LuauFunction>(*this, m_life, lua_ref(state, absIndex)));
        case LUA_TUSERDATA: {
            if (const auto* structData = ToStruct(state, absIndex))
                return structData->value;
            if (const auto* objectData = ToObject(state, absIndex))
                return objectData->object;
            throw std::runtime_error("Expected an engine script object");
        }
        default:
            throw std::runtime_error("Incorrect script value type");
        }
    }

    std::vector<ScriptValue> ToArguments(lua_State* state, int startIndex) {
        const int top = lua_gettop(state);
        std::vector<ScriptValue> arguments;
        if (top >= startIndex) {
            arguments.reserve(static_cast<std::size_t>(top - startIndex + 1));
            for (int index = startIndex; index <= top; ++index)
                arguments.push_back(ToScriptValue(state, index));
        }
        return arguments;
    }

private:
    struct RunScope {
        explicit RunScope(LuauRuntime& runtime, bool allowNested = false)
            : runtime(runtime), outermost(!runtime.m_running) {
            if (runtime.m_running && !allowNested)
                throw std::logic_error("A host callback cannot reenter its script instance");
            if (runtime.m_callDepth >= 32)
                throw std::runtime_error("Script lifecycle call depth limit exceeded");
            ++runtime.m_callDepth;
            if (outermost) {
                runtime.m_running = true;
                runtime.m_interrupts = 0;
                runtime.m_sampledThread = nullptr;
                runtime.m_lastSample = std::chrono::steady_clock::now();
            }
        }
        ~RunScope() {
            --runtime.m_callDepth;
            if (outermost)
                runtime.m_running = false;
        }
        LuauRuntime& runtime;
        bool outermost;
    };

    static LuauRuntime& FromState(lua_State* state) {
        return *static_cast<LuauRuntime*>(lua_callbacks(state)->userdata);
    }

    static void OnInterrupt(lua_State* state, int gc) {
        // Interrupts from the collector cannot raise errors.
        if (gc >= 0)
            return;
        auto& runtime = FromState(state);
        if (!runtime.m_running)
            return;
        if (++runtime.m_interrupts > MaxInterruptsPerCall)
            RaiseLuauError(state, "Execution limit exceeded");
        if (runtime.m_profiling)
            runtime.Sample(state);
    }

    static void OnDebugBreak(lua_State* state, lua_Debug*) { FromState(state).ReportBreak(state); }

    // Luau patches the first instruction of a breakpoint's line in each function, so a pass over the line stops once.
    void ReportBreak(lua_State* state) {
        lua_Debug info{};
        ScriptBreak stop;
        for (int level = 0; lua_getinfo(state, level, "sln", &info); ++level) {
            if (info.what && std::string_view(info.what) == "C")
                continue;
            ScriptDebugFrame frame;
            frame.function = info.name                                            ? info.name
                             : info.what && std::string_view(info.what) == "main" ? "main"
                                                                                  : "<anonymous>";
            frame.sourceName = info.short_src;
            frame.line = info.currentline > 0 ? static_cast<std::size_t>(info.currentline) : 0;
            for (int index = 1;; ++index) {
                const char* local = lua_getlocal(state, level, index);
                if (!local)
                    break;
                // Names in parentheses are the compiler's temporaries.
                if (local[0] != '(')
                    frame.locals.emplace_back(local, FormatLocal(state, -1));
                lua_pop(state, 1);
            }
            stop.frames.push_back(std::move(frame));
        }
        Detail::ReportScriptBreak(stop);
    }

    // Formats a local without calling into the script, which a stopped frame must not do.
    std::string FormatLocal(lua_State* state, int index) {
        switch (lua_type(state, index)) {
        case LUA_TNIL:
        case LUA_TBOOLEAN:
        case LUA_TNUMBER:
        case LUA_TSTRING:
            return Detail::FormatScriptValue(ToScriptValue(state, index), "nil");
        case LUA_TUSERDATA:
            if (const auto* structData = ToStruct(state, index))
                return Detail::FormatScriptValue(structData->value, "nil");
            if (ToObject(state, index))
                return "<object>";
            break;
        default:
            break;
        }
        return luaL_typename(state, index);
    }

    // The profile of the function at a level of the stack, found by its prototype once it has been seen.
    ScriptFunctionProfile* ProfileOf(lua_State* state, int level) {
        lua_Debug info{};
        if (!lua_getinfo(state, level, "p", &info))
            return nullptr;
        if (const auto found = m_profileByProto.find(info.protoid); found != m_profileByProto.end())
            return found->second;
        if (!lua_getinfo(state, level, "sn", &info))
            return nullptr;
        ProfileKey key{info.short_src, info.linedefined, info.name ? info.name : "<anonymous>"};
        if (info.what && std::string_view(info.what) == "main")
            key.name = "main";
        auto [entry, added] = m_profile.try_emplace(key);
        if (added) {
            entry->second.name = key.name;
            entry->second.sourceName = key.sourceName;
            entry->second.line = static_cast<std::size_t>(std::max(key.line, 0));
        }
        m_profileByProto.emplace(info.protoid, &entry->second);
        return &entry->second;
    }

    // Adds the time since the last safepoint to the function running now, which is where it went: safepoints come
    // at calls, returns and loop iterations, so engine calls count in the script function that made them.
    void Sample(lua_State* state) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = now - m_lastSample;
        m_lastSample = now;
        ScriptFunctionProfile* function = ProfileOf(state, 0);
        if (!function)
            return;
        // A deeper frame of the same thread is a call that started since the last safepoint.
        const int depth = lua_stackdepth(state);
        if (state != m_sampledThread || depth > m_sampledDepth)
            ++function->calls;
        m_sampledThread = state;
        m_sampledDepth = depth;
        ++function->instructions;
        function->time += std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed);
    }

    // Counts collectable objects (tables, strings, closures, userdata) in the innermost script function, including
    // those the engine makes for it; the VM's own buffers have no type.
    static void OnAllocate(lua_State* state, void*, std::size_t, std::size_t, std::uint8_t, int type, int) {
        auto& runtime = FromState(state);
        if (type == LUA_T_ALL || !runtime.m_running)
            return;
        lua_Debug info{};
        for (int level = 0; lua_getinfo(state, level, "p", &info); ++level) {
            if (info.protoid != 0) {
                if (ScriptFunctionProfile* function = runtime.ProfileOf(state, level))
                    ++function->allocations;
                return;
            }
        }
    }

    // Keeps a loaded chunk, the function at the top of the stack, so breakpoints can be set in its code.
    void AddChunk(lua_State* state, const std::string& sourceName) {
        const auto [chunk, added] = m_chunks.try_emplace(sourceName, LUA_NOREF);
        if (!added)
            return;
        chunk->second = lua_ref(state, -1);
        ApplyBreakpoints(sourceName, chunk->second);
    }

    void ApplyBreakpoints(const std::string& sourceName, int reference) {
        lua_getref(m_state, reference);
        auto& active = m_activeBreakpoints[sourceName];
        for (const int line : active)
            lua_breakpoint(m_state, -1, line, 0);
        active.clear();
        if (const auto requested = m_requestedBreakpoints.find(sourceName); requested != m_requestedBreakpoints.end()) {
            for (const std::size_t line : requested->second) {
                if (const int target = lua_breakpoint(m_state, -1, static_cast<int>(line), 1); target != -1)
                    active.push_back(target);
            }
        }
        lua_pop(m_state, 1);
    }

    // Pushes a global of the script, as found without metamethods; returns whether it is a function.
    bool PushGlobal(std::string_view name) {
        lua_pushlstring(m_state, name.data(), name.size());
        return lua_rawget(m_state, LUA_GLOBALSINDEX) == LUA_TFUNCTION;
    }

    void PushObject(lua_State* state, const ScriptObjectRef& object) {
        if (!object) {
            lua_pushnil(state);
            return;
        }
        // A function of this script comes back as itself.
        if (const auto* function = dynamic_cast<const LuauFunction*>(object.get());
            function && function->runtime == this) {
            lua_getref(state, function->reference);
            return;
        }
        lua_getfield(state, LUA_REGISTRYINDEX, ObjectsRegistryKey);
        lua_pushlightuserdata(state, object.get());
        if (lua_rawget(state, -2) == LUA_TUSERDATA) {
            lua_remove(state, -2);
            return;
        }
        lua_pop(state, 1);

        void* memory = lua_newuserdatataggedwithmetatable(state, sizeof(LuauObject), ObjectTag);
        new (memory) LuauObject{object, this};

        lua_pushlightuserdata(state, object.get());
        lua_pushvalue(state, -2);
        lua_rawset(state, -4);
        lua_remove(state, -2);
    }

    void ProtectedCall(int argumentCount, int resultCount) {
        if (lua_pcall(m_state, argumentCount, resultCount, 0) != LUA_OK) {
            const char* message = lua_tostring(m_state, -1);
            const std::string error = message ? message : "Luau execution failed";
            lua_pop(m_state, 1);
            throw std::runtime_error(error);
        }
    }

    void ValidateHooks() const {
        for (const auto& [name, expectedCount] : m_expectedHooks) {
            lua_pushlstring(m_state, name.data(), name.size());
            const int type = lua_rawget(m_state, LUA_GLOBALSINDEX);
            if (type == LUA_TNIL) {
                lua_pop(m_state, 1);
                continue;
            }
            if (type != LUA_TFUNCTION) {
                lua_pop(m_state, 1);
                throw std::runtime_error(m_name + ":1: " + name + " must be a function");
            }
            lua_Debug info{};
            lua_getinfo(m_state, -1, "a", &info);
            lua_pop(m_state, 1);
            if (info.isvararg || static_cast<std::size_t>(info.nparams) != expectedCount) {
                throw std::runtime_error(m_name + ":1: " + name + " expects " + std::to_string(expectedCount) +
                                         " parameter(s)");
            }
        }
    }

    static int LuauPrint(lua_State* state) {
        std::string message;
        const int count = lua_gettop(state);
        for (int index = 1; index <= count; ++index) {
            if (index > 1)
                message += ' ';
            if (lua_type(state, index) == LUA_TNUMBER) {
                message += Detail::FormatScriptValue(ScriptValue(lua_tonumber(state, index)), "nil");
            } else {
                std::size_t length = 0;
                const char* text = luaL_tolstring(state, index, &length);
                if (text)
                    message.append(text, length);
                lua_pop(state, 1);
            }
        }
        Detail::WriteScriptLog("Luau", message);
        return 0;
    }

    static int LuauConstructStruct(lua_State* state) {
        auto& runtime = FromState(state);
        const char* constructor = lua_tostring(state, lua_upvalueindex(1));
        std::string failure;
        try {
            const auto arguments = runtime.ToArguments(state, 1);
            const ScriptValue value = Detail::ConstructScriptStruct(constructor ? constructor : "", arguments);
            runtime.PushValue(state, value);
            return 1;
        } catch (const std::exception& error) {
            failure = error.what();
        }
        RaiseLuauError(state, failure);
    }

    static LuauStruct& CheckStruct(lua_State* state, int index) {
        auto* self = ToStruct(state, index);
        if (!self)
            RaiseLuauError(state, "Expected a value type");
        return *self;
    }

    static int LuauStructIndex(lua_State* state) {
        auto& runtime = FromState(state);
        auto& self = CheckStruct(state, 1);
        const std::string_view name = ToKey(state, 2);
        std::string failure;
        try {
            if (name.data() == nullptr)
                throw std::runtime_error("Property name must be a string");
            runtime.PushValue(state, Detail::GetScriptStructField(self.value, name));
            return 1;
        } catch (const std::exception& error) {
            failure = error.what();
        }
        RaiseLuauError(state, failure);
    }

    static int LuauStructNewIndex(lua_State* state) {
        auto& runtime = FromState(state);
        auto& self = CheckStruct(state, 1);
        const std::string_view name = ToKey(state, 2);
        std::string failure;
        try {
            if (name.data() == nullptr)
                throw std::runtime_error("Property name must be a string");
            ScriptValue updated = self.value;
            Detail::SetScriptStructField(updated, name, runtime.ToScriptValue(state, 3));
            if (!self.property.empty()) {
                auto owner = self.owner.lock();
                if (!owner)
                    throw std::runtime_error("Object reference is no longer valid");
                owner->Set(self.property, updated);
            }
            self.value = std::move(updated);
            return 0;
        } catch (const std::exception& error) {
            failure = error.what();
        }
        RaiseLuauError(state, failure);
    }

    static int LuauStructEqual(lua_State* state) {
        const auto* left = ToStruct(state, 1);
        const auto* right = ToStruct(state, 2);
        lua_pushboolean(state, left && right && left->value == right->value);
        return 1;
    }

    static int LuauStructToString(lua_State* state) {
        const std::string formatted = Detail::FormatScriptValue(CheckStruct(state, 1).value, "nil");
        lua_pushlstring(state, formatted.data(), formatted.size());
        return 1;
    }

    static LuauObject& CheckObject(lua_State* state, int index) {
        auto* self = ToObject(state, index);
        if (!self)
            RaiseLuauError(state, "Expected an engine object");
        return *self;
    }

    // An object's method read as a value, for calls with a dot: called with a colon too, it skips the object.
    static int LuauObjectMethodCall(lua_State* state) {
        auto& self = CheckObject(state, lua_upvalueindex(1));
        const std::string_view method = ToKey(state, lua_upvalueindex(2));
        std::string failure;
        try {
            const int firstArg = lua_gettop(state) >= 1 && lua_rawequal(state, 1, lua_upvalueindex(1)) ? 2 : 1;
            const auto arguments = self.runtime->ToArguments(state, firstArg);
            const ScriptValue result = self.object->Call(method, arguments);
            self.runtime->PushValue(state, result);
            return 1;
        } catch (const std::exception& error) {
            failure = error.what();
        }
        RaiseLuauError(state, failure);
    }

    // Calls with a colon, which Luau resolves by name without reading the method as a value first.
    static int LuauObjectNamecall(lua_State* state) {
        auto& self = CheckObject(state, 1);
        const char* method = lua_namecallatom(state, nullptr);
        std::string failure;
        try {
            if (!method)
                throw std::runtime_error("Unknown method");
            const auto arguments = self.runtime->ToArguments(state, 2);
            const ScriptValue result = self.object->Call(method, arguments);
            self.runtime->PushValue(state, result);
            return 1;
        } catch (const std::exception& error) {
            failure = error.what();
        }
        RaiseLuauError(state, failure);
    }

    // Calls a function of another script, such as an event callback or a function value it returned.
    static int LuauObjectInvoke(lua_State* state) {
        auto& self = CheckObject(state, 1);
        std::string failure;
        try {
            const auto arguments = self.runtime->ToArguments(state, 2);
            const ScriptValue result = self.object->Invoke(arguments);
            self.runtime->PushValue(state, result);
            return 1;
        } catch (const std::exception& error) {
            failure = error.what();
        }
        RaiseLuauError(state, failure);
    }

    static int LuauObjectIndex(lua_State* state) {
        auto& self = CheckObject(state, 1);
        const std::string_view name = ToKey(state, 2);
        std::string failure;
        try {
            if (name.data() == nullptr)
                throw std::runtime_error("Property name must be a string");
            try {
                const ScriptValue value = self.object->Get(name);
                self.runtime->PushValue(state, value, self.object, name);
                return 1;
            } catch (const std::exception& error) {
                const std::string_view message = error.what();
                if (message.starts_with("Unknown script property ") && self.object->HasMethod(name)) {
                    lua_pushvalue(state, 1);
                    lua_pushvalue(state, 2);
                    lua_pushcclosure(state, &LuauObjectMethodCall, "method", 2);
                    return 1;
                }
                throw;
            }
        } catch (const std::exception& error) {
            failure = error.what();
        }
        RaiseLuauError(state, failure);
    }

    static int LuauObjectNewIndex(lua_State* state) {
        auto& self = CheckObject(state, 1);
        const std::string_view name = ToKey(state, 2);
        std::string failure;
        try {
            if (name.data() == nullptr)
                throw std::runtime_error("Property name must be a string");
            self.object->Set(name, self.runtime->ToScriptValue(state, 3));
            return 0;
        } catch (const std::exception& error) {
            failure = error.what();
        }
        RaiseLuauError(state, failure);
    }

    static int LuauObjectEqual(lua_State* state) {
        const auto* left = ToObject(state, 1);
        const auto* right = ToObject(state, 2);
        lua_pushboolean(state, left && right && left->object == right->object);
        return 1;
    }

    static int LuauObjectToString(lua_State* state) {
        lua_pushliteral(state, "<object>");
        return 1;
    }

    // Reads of an environment's missing names: built-ins, then variables declared without a value yet. Anything
    // else is a mistake, reported where it happens instead of reading nil. Upvalues: built-ins, declared names.
    static int LuauEnvironmentIndex(lua_State* state) {
        lua_pushvalue(state, 2);
        if (lua_rawget(state, lua_upvalueindex(1)) != LUA_TNIL)
            return 1;
        lua_pop(state, 1);
        lua_pushvalue(state, 2);
        if (lua_rawget(state, lua_upvalueindex(2)) != LUA_TNIL) {
            lua_pushnil(state);
            return 1;
        }
        const std::string_view key = ToKey(state, 2);
        if (key.data() == nullptr)
            RaiseLuauError(state, "Unknown variable");
        RaiseLuauError(state, "Unknown variable '" + std::string(key) + "'");
    }

    // Declares a new name of an environment. Built-ins cannot be shadowed, which keeps the environment safe for
    // Luau's fast built-in calls.
    static int LuauEnvironmentNewIndex(lua_State* state) {
        lua_pushvalue(state, 2);
        if (lua_rawget(state, lua_upvalueindex(1)) != LUA_TNIL) {
            const std::string_view key = ToKey(state, 2);
            RaiseLuauError(state, "Cannot rebind built-in '" + std::string(key) + "'");
        }
        lua_pop(state, 1);
        lua_pushvalue(state, 2);
        lua_pushboolean(state, 1);
        lua_rawset(state, lua_upvalueindex(2));
        lua_rawset(state, 1);
        return 0;
    }

    // Pushes a new environment over the built-ins, and the table of the names declared in it.
    static void PushEnvironment(lua_State* state) {
        lua_newtable(state); // environment
        lua_newtable(state); // declared names
        lua_newtable(state); // metatable
        lua_getfield(state, LUA_REGISTRYINDEX, BuiltinsRegistryKey);
        lua_pushvalue(state, -3);
        lua_pushcclosure(state, &LuauEnvironmentIndex, "__index", 2);
        lua_setfield(state, -2, "__index");
        lua_getfield(state, LUA_REGISTRYINDEX, BuiltinsRegistryKey);
        lua_pushvalue(state, -3);
        lua_pushcclosure(state, &LuauEnvironmentNewIndex, "__newindex", 2);
        lua_setfield(state, -2, "__newindex");
        lua_setreadonly(state, -1, true);
        lua_setmetatable(state, -3);
        lua_setsafeenv(state, -2, true);
    }

    // What a module that returns nothing gives its importers: its declared names, read only. Upvalues: the
    // module's environment and its declared names.
    static int LuauModuleProxyIndex(lua_State* state) {
        lua_pushvalue(state, 2);
        if (lua_rawget(state, lua_upvalueindex(1)) != LUA_TNIL)
            return 1;
        lua_pop(state, 1);
        lua_pushvalue(state, 2);
        if (lua_rawget(state, lua_upvalueindex(2)) != LUA_TNIL) {
            lua_pushnil(state);
            return 1;
        }
        RaiseLuauError(state, "Unknown module member '" + std::string(ToKey(state, 2)) + "'");
    }

    static int LuauModuleProxyNewIndex(lua_State* state) { RaiseLuauError(state, "Cannot write module member"); }

    static int LuauRequire(lua_State* state) {
        auto& runtime = FromState(state);
        if (lua_gettop(state) != 1 || lua_type(state, 1) != LUA_TSTRING)
            RaiseLuauError(state, "require expects a module name string");
        const std::string request(ToKey(state, 1));
        if (request.empty())
            RaiseLuauError(state, "Module name cannot be empty");

        // Luau's require-by-string: aliases name engine libraries, paths name the project's modules.
        if (request.starts_with('@')) {
            const auto native = runtime.m_libraries.find(request.substr(1));
            if (native == runtime.m_libraries.end())
                RaiseLuauError(state, "Unknown engine library '" + request + "'");
            runtime.PushObject(state, native->second);
            return 1;
        }
        if (!request.starts_with("./") && !request.starts_with("../"))
            RaiseLuauError(state, "Cannot require '" + request +
                                      "': modules are required by a path starting with ./ or ../, engine libraries "
                                      "by an alias such as @spot");

        std::string failure;
        try {
            if (!runtime.m_loader)
                throw std::runtime_error("Cannot load module '" + request + "'");
            const std::string importer = runtime.m_moduleStack.empty() ? runtime.m_name : runtime.m_moduleStack.back();
            const std::string resolved = Detail::ResolveScriptModulePath(request, ".luau");
            auto source = runtime.m_loader(resolved, importer);
            if (!source || source->sourceName.empty())
                throw std::runtime_error("Could not load module '" + request + "'");

            lua_getfield(state, LUA_REGISTRYINDEX, ModulesRegistryKey);
            lua_pushlstring(state, source->sourceName.data(), source->sourceName.size());
            if (lua_rawget(state, -2) != LUA_TNIL) {
                lua_remove(state, -2);
                return 1;
            }
            lua_pop(state, 1);

            if (std::ranges::find(runtime.m_loadingModules, source->sourceName) != runtime.m_loadingModules.end())
                throw std::runtime_error("Circular module import '" + source->sourceName + "'");
            if (runtime.m_loadingModules.size() >= MaxModuleDepth)
                throw std::runtime_error("Module import depth limit exceeded");

            // Each module has an environment of its own: it reads the built-ins and keeps its declarations.
            PushEnvironment(state); // [modules, environment, declared]
            const std::string bytecode = Compile(source->source);
            const std::string chunkName = "@" + source->sourceName;
            if (luau_load(state, chunkName.c_str(), bytecode.data(), bytecode.size(), lua_absindex(state, -2)) !=
                LUA_OK) {
                const char* error = lua_tostring(state, -1);
                throw std::runtime_error(error ? error : "Failed to parse module");
            }
            runtime.AddChunk(state, source->sourceName);

            runtime.m_loadingModules.push_back(source->sourceName);
            runtime.m_moduleStack.push_back(source->sourceName);
            struct LoadingScope {
                std::vector<std::string>& loading;
                std::vector<std::string>& stack;
                ~LoadingScope() {
                    loading.pop_back();
                    stack.pop_back();
                }
            } loadingScope{runtime.m_loadingModules, runtime.m_moduleStack};

            if (lua_pcall(state, 0, 1, 0) != LUA_OK) {
                const char* error = lua_tostring(state, -1);
                throw std::runtime_error(error ? error : "Module initialization failed");
            }
            // [modules, environment, declared, result]
            if (lua_isnil(state, -1)) {
                lua_pop(state, 1);
                lua_newuserdata(state, 1);
                lua_newtable(state);
                lua_pushvalue(state, -4);
                lua_pushvalue(state, -4);
                lua_pushcclosure(state, &LuauModuleProxyIndex, "__index", 2);
                lua_setfield(state, -2, "__index");
                lua_pushcfunction(state, &LuauModuleProxyNewIndex, "__newindex");
                lua_setfield(state, -2, "__newindex");
                lua_setreadonly(state, -1, true);
                lua_setmetatable(state, -2);
            }
            lua_remove(state, -2);
            lua_remove(state, -2);
            // [modules, result]
            lua_pushlstring(state, source->sourceName.data(), source->sourceName.size());
            lua_pushvalue(state, -2);
            lua_rawset(state, -4);
            lua_remove(state, -2);
            return 1;
        } catch (const std::exception& error) {
            failure = error.what();
        }
        RaiseLuauError(state, failure);
    }

    void SetUpState() {
        lua_callbacks(m_state)->userdata = this;
        lua_callbacks(m_state)->interrupt = &OnInterrupt;
        lua_callbacks(m_state)->debugbreak = &OnDebugBreak;

        // Weak-valued table for ScriptObject* -> LuauObject userdata identity cache.
        lua_newtable(m_state);
        lua_newtable(m_state);
        lua_pushliteral(m_state, "v");
        lua_setfield(m_state, -2, "__mode");
        lua_setmetatable(m_state, -2);
        lua_setfield(m_state, LUA_REGISTRYINDEX, ObjectsRegistryKey);

        // Loaded source modules cache per instance.
        lua_newtable(m_state);
        lua_setfield(m_state, LUA_REGISTRYINDEX, ModulesRegistryKey);

        for (const auto& [name, open] : StandardLibraries) {
            lua_pushcfunction(m_state, open, nullptr);
            lua_pushstring(m_state, name);
            lua_call(m_state, 1, 0);
        }

        lua_pushcfunction(m_state, &LuauPrint, "print");
        lua_setglobal(m_state, "print");
        lua_pushcfunction(m_state, &LuauRequire, "require");
        lua_setglobal(m_state, "require");
        for (const auto constructor : ScriptApi::ValueConstructors) {
            const std::string name(constructor);
            lua_pushstring(m_state, name.c_str());
            lua_pushcclosure(m_state, &LuauConstructStruct, name.c_str(), 1);
            lua_setglobal(m_state, name.c_str());
        }

        lua_newtable(m_state);
        lua_pushcfunction(m_state, &LuauStructIndex, "__index");
        lua_setfield(m_state, -2, "__index");
        lua_pushcfunction(m_state, &LuauStructNewIndex, "__newindex");
        lua_setfield(m_state, -2, "__newindex");
        lua_pushcfunction(m_state, &LuauStructEqual, "__eq");
        lua_setfield(m_state, -2, "__eq");
        lua_pushcfunction(m_state, &LuauStructToString, "__tostring");
        lua_setfield(m_state, -2, "__tostring");
        lua_setreadonly(m_state, -1, true);
        lua_setuserdatametatable(m_state, StructTag);
        lua_setuserdatadtor(m_state, StructTag,
                            [](lua_State*, void* data) { std::destroy_at(static_cast<LuauStruct*>(data)); });

        lua_newtable(m_state);
        lua_pushcfunction(m_state, &LuauObjectIndex, "__index");
        lua_setfield(m_state, -2, "__index");
        lua_pushcfunction(m_state, &LuauObjectNewIndex, "__newindex");
        lua_setfield(m_state, -2, "__newindex");
        lua_pushcfunction(m_state, &LuauObjectNamecall, "__namecall");
        lua_setfield(m_state, -2, "__namecall");
        lua_pushcfunction(m_state, &LuauObjectEqual, "__eq");
        lua_setfield(m_state, -2, "__eq");
        lua_pushcfunction(m_state, &LuauObjectToString, "__tostring");
        lua_setfield(m_state, -2, "__tostring");
        lua_pushcfunction(m_state, &LuauObjectInvoke, "__call");
        lua_setfield(m_state, -2, "__call");
        lua_setreadonly(m_state, -1, true);
        lua_setuserdatametatable(m_state, ObjectTag);
        lua_setuserdatadtor(m_state, ObjectTag,
                            [](lua_State*, void* data) { std::destroy_at(static_cast<LuauObject*>(data)); });

        // The built-ins become read only, and the script gets an environment of its own over them.
        luaL_sandbox(m_state);
        lua_pushvalue(m_state, LUA_GLOBALSINDEX);
        lua_setfield(m_state, LUA_REGISTRYINDEX, BuiltinsRegistryKey);
        PushEnvironment(m_state);
        lua_setfield(m_state, LUA_REGISTRYINDEX, DeclaredRegistryKey);
        lua_replace(m_state, LUA_GLOBALSINDEX);
    }

    lua_State* m_state = nullptr;
    // Expired once the runtime ends, so functions and globals it exposed stop reaching its state.
    std::shared_ptr<void> m_life = std::make_shared<int>(0);
    std::string m_name;
    int m_mainChunkRef = LUA_NOREF;
    bool m_initialized = false;
    bool m_running = false;
    std::size_t m_callDepth = 0;
    std::uint64_t m_interrupts = 0;
    std::unordered_map<std::string, ScriptObjectRef> m_libraries;
    mutable std::unordered_map<std::string, std::size_t> m_expectedHooks;
    ScriptSourceLoader m_loader;
    std::vector<std::string> m_loadingModules;
    std::vector<std::string> m_moduleStack;
    // Registry references of the root and module chunks, by source name, and their breakpoints.
    std::map<std::string, int> m_chunks;
    std::map<std::string, std::vector<std::size_t>> m_requestedBreakpoints;
    std::unordered_map<std::string, std::vector<int>> m_activeBreakpoints;
    bool m_profiling = false;
    std::map<ProfileKey, ScriptFunctionProfile> m_profile;
    std::unordered_map<int, ScriptFunctionProfile*> m_profileByProto;
    lua_State* m_sampledThread = nullptr;
    int m_sampledDepth = 0;
    std::chrono::steady_clock::time_point m_lastSample;
};

LuauFunction::~LuauFunction() {
    if (!life.expired())
        lua_unref(runtime->GetState(), reference);
}
ScriptValue LuauFunction::Invoke(std::span<const ScriptValue> arguments) {
    if (life.expired())
        throw std::runtime_error("The script instance is no longer available");
    return runtime->InvokeForeign(reference, {}, arguments);
}

LuauCoroutine::~LuauCoroutine() {
    if (!life.expired())
        runtime->ReleaseReference(thread);
}
ScriptValue LuauCoroutine::Call(std::string_view name, std::span<const ScriptValue> arguments) {
    if (name != "resume")
        return ScriptObject::Call(name, arguments);
    if (life.expired())
        throw std::runtime_error("The script instance is no longer available");
    if (done)
        throw std::runtime_error("Cannot resume a finished coroutine");
    try {
        return runtime->ResumeThread(thread, done);
    } catch (...) {
        done = true;
        throw;
    }
}

LuauRuntime& LuauGlobals::Runtime() const {
    if (life.expired())
        throw std::runtime_error("The script instance is no longer available");
    return *runtime;
}
bool LuauGlobals::HasMethod(std::string_view name) const {
    return Runtime().GlobalType(name) == LUA_TFUNCTION;
}
bool LuauGlobals::HasProperty(std::string_view name) const {
    const int type = Runtime().GlobalType(name);
    return type == LUA_TNIL ? Runtime().IsDeclared(name) : type != LUA_TFUNCTION;
}
ScriptValue LuauGlobals::Get(std::string_view name) const {
    // Functions are methods: callers bind them through Call().
    if (Runtime().GlobalType(name) == LUA_TFUNCTION)
        return ScriptObject::Get(name);
    return Runtime().GetGlobal(name);
}
void LuauGlobals::Set(std::string_view name, const ScriptValue& value) {
    Runtime().SetGlobal(name, value);
}
ScriptValue LuauGlobals::Call(std::string_view name, std::span<const ScriptValue> arguments) {
    return Runtime().InvokeForeign(LUA_NOREF, name, arguments);
}

} // namespace

std::optional<ScriptDiagnostic> Detail::CheckLuauSyntax(std::string_view text, std::string_view) {
    Luau::Allocator allocator;
    Luau::AstNameTable names(allocator);
    const Luau::ParseResult result = Luau::Parser::parse(text.data(), text.size(), names, allocator);
    if (result.errors.empty())
        return std::nullopt;
    const Luau::ParseError& error = result.errors.front();
    return ScriptDiagnostic{.line = error.getLocation().begin.line + 1,
                            .column = error.getLocation().begin.column + 1,
                            .message = error.getMessage()};
}

std::vector<ScriptExport> Detail::ReadLuauExports(std::string_view text, std::string_view) {
    Luau::Allocator allocator;
    Luau::AstNameTable names(allocator);
    const Luau::ParseResult result = Luau::Parser::parse(text.data(), text.size(), names, allocator);
    if (!result.errors.empty())
        throw std::runtime_error(result.errors.front().getMessage());
    // Globals the script's top level gives constant values; locals stay private.
    std::vector<ScriptExport> exports;
    for (const Luau::AstStat* statement : result.root->body) {
        const auto* assignment = statement->as<Luau::AstStatAssign>();
        if (!assignment)
            continue;
        for (std::size_t i = 0; i < assignment->vars.size && i < assignment->values.size; ++i) {
            const auto* global = assignment->vars.data[i]->as<Luau::AstExprGlobal>();
            if (!global)
                continue;
            const std::string name = global->name.value;
            if (std::ranges::any_of(exports, [&](const ScriptExport& field) { return field.name == name; }))
                continue;
            if (auto value = ReadConstant(assignment->values.data[i]))
                exports.push_back({name, std::move(*value)});
        }
    }
    return exports;
}

std::unique_ptr<ScriptRuntime> Detail::CreateLuauRuntime(std::string_view text, std::string_view name) {
    return std::make_unique<LuauRuntime>(text, name);
}

} // namespace Spot
