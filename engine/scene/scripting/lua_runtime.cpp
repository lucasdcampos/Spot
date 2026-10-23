#include "scene/scripting/lua_runtime.h"

#include "scene/scripting/script_api.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Spot {
namespace {

constexpr const char* RuntimeRegistryKey = "Spot.LuaRuntime";
constexpr const char* ObjectsRegistryKey = "Spot.LuaObjects";
constexpr const char* ModulesRegistryKey = "Spot.LuaModules";
constexpr const char* DeclaredGlobalsKey = "Spot.LuaDeclaredGlobals";
constexpr const char* ObjectMetatable = "Spot.ScriptObject";
constexpr const char* StructMetatable = "Spot.ScriptStruct";
constexpr const char* ModuleProxyMetatable = "Spot.ModuleProxy";
constexpr int MaxInstructionsPerCall = 100000;
constexpr std::size_t MaxModuleDepth = 32;

constexpr std::array<std::string_view, 10> StandardLibraryNames{
    "base", "coroutine", "debug", "io", "math", "os", "package", "string", "table", "utf8",
};

bool IsStandardLibraryName(std::string_view name) {
    return std::ranges::find(StandardLibraryNames, name) != StandardLibraryNames.end();
}

class LuaRuntime;

struct LuaObject {
    ScriptObjectRef object;
    LuaRuntime* runtime = nullptr;
};

struct LuaStruct {
    ScriptValue value;
    std::weak_ptr<ScriptObject> owner;
    std::string property;
};

int RaiseLuaError(lua_State* state, std::string_view message) {
    luaL_where(state, 1);
    lua_pushlstring(state, message.data(), message.size());
    lua_concat(state, 2);
    return lua_error(state);
}

void OnInstructionLimit(lua_State* state, lua_Debug*) {
    RaiseLuaError(state, "Execution limit exceeded");
}

// A Lua function handed to the engine, such as an event callback, kept in the registry while referenced.
struct LuaFunction final : ScriptObject {
    LuaFunction(LuaRuntime& runtime, std::weak_ptr<void> life, int reference)
        : runtime(&runtime), life(std::move(life)), reference(reference) {}
    ~LuaFunction() override;
    bool HasMethod(std::string_view) const override { return false; }
    bool IsCallable() const override { return true; }
    ScriptValue Invoke(std::span<const ScriptValue> arguments) override;
    LuaRuntime* runtime;
    std::weak_ptr<void> life;
    int reference;
};

// A Lua thread running one of the script's functions for the engine; see ScriptRuntime::CreateCoroutine().
struct LuaCoroutine final : ScriptObject {
    LuaCoroutine(LuaRuntime& runtime, std::weak_ptr<void> life, int thread)
        : runtime(&runtime), life(std::move(life)), thread(thread) {}
    ~LuaCoroutine() override;
    bool HasMethod(std::string_view name) const override { return name == "resume"; }
    bool HasProperty(std::string_view name) const override { return name == "done"; }
    ScriptValue Get(std::string_view name) const override {
        if (name == "done")
            return done;
        return ScriptObject::Get(name);
    }
    ScriptValue Call(std::string_view name, std::span<const ScriptValue> arguments) override;
    LuaRuntime* runtime;
    std::weak_ptr<void> life;
    int thread;
    bool done = false;
};

// The script's globals, as other scripts and C++ see them: functions are methods, other values properties.
struct LuaGlobals final : ScriptObject {
    LuaGlobals(LuaRuntime& runtime, std::weak_ptr<void> life) : runtime(&runtime), life(std::move(life)) {}
    LuaRuntime& Runtime() const;
    bool HasMethod(std::string_view name) const override;
    bool HasProperty(std::string_view name) const override;
    ScriptValue Get(std::string_view name) const override;
    void Set(std::string_view name, const ScriptValue& value) override;
    ScriptValue Call(std::string_view name, std::span<const ScriptValue> arguments) override;
    LuaRuntime* runtime;
    std::weak_ptr<void> life;
};

class LuaRuntime final : public ScriptRuntime {
public:
    LuaRuntime(std::string_view text, std::string_view name) : m_name(name) {
        m_state = luaL_newstate();
        if (!m_state)
            throw std::runtime_error("Could not create Lua state");

        try {
            SetUpState();
            const std::string chunkName = "@" + m_name;
            if (luaL_loadbufferx(m_state, text.data(), text.size(), chunkName.c_str(), "t") != LUA_OK) {
                const char* error = lua_tostring(m_state, -1);
                throw std::runtime_error(error ? error : "Failed to parse Lua script");
            }
            m_mainChunkRef = luaL_ref(m_state, LUA_REGISTRYINDEX);
        } catch (...) {
            lua_close(m_state);
            m_state = nullptr;
            throw;
        }
    }

    ~LuaRuntime() override {
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
        if (!m_initialized)
            return false;
        lua_pushglobaltable(m_state);
        lua_pushlstring(m_state, name.data(), name.size());
        const int type = lua_rawget(m_state, -2);
        lua_pop(m_state, 2);
        return type == LUA_TFUNCTION;
    }

    void ValidateFunction(std::string_view name, std::size_t parameterCount) const override {
        m_expectedHooks.insert_or_assign(std::string(name), parameterCount);
    }

    void Initialize() override {
        if (m_initialized)
            throw std::runtime_error("Lua runtime is already initialized");
        const RunScope scope(*this);
        m_moduleStack.push_back(m_name);
        struct PopModule {
            std::vector<std::string>& stack;
            ~PopModule() { stack.pop_back(); }
        } pop{m_moduleStack};

        lua_rawgeti(m_state, LUA_REGISTRYINDEX, m_mainChunkRef);
        luaL_unref(m_state, LUA_REGISTRYINDEX, m_mainChunkRef);
        m_mainChunkRef = LUA_NOREF;

        ProtectedCall(0, 0);
        ValidateHooks();
        m_initialized = true;
    }

    void Call(std::string_view name, std::span<const ScriptValue> arguments) override {
        if (!m_initialized)
            throw std::runtime_error("Lua runtime is not initialized");
        const RunScope scope(*this, name == "OnEnable" || name == "OnDisable");
        lua_pushglobaltable(m_state);
        lua_pushlstring(m_state, name.data(), name.size());
        if (lua_rawget(m_state, -2) != LUA_TFUNCTION) {
            lua_pop(m_state, 2);
            throw std::runtime_error("Unknown script function '" + std::string(name) + "'");
        }
        lua_remove(m_state, -2);

        for (const auto& argument : arguments)
            PushValue(m_state, argument);
        ProtectedCall(static_cast<int>(arguments.size()), 0);
    }

    ScriptObjectRef CreateCoroutine(const ScriptObjectRef& function) override {
        const auto* lua = dynamic_cast<const LuaFunction*>(function.get());
        if (!lua || lua->runtime != this)
            throw std::runtime_error("Expected a function of this script to run as a coroutine");
        lua_State* thread = lua_newthread(m_state);
        lua_rawgeti(thread, LUA_REGISTRYINDEX, lua->reference);
        const int reference = luaL_ref(m_state, LUA_REGISTRYINDEX);
        return std::make_shared<LuaCoroutine>(*this, m_life, reference);
    }
    // Resumes the registry thread; returns what it yielded or returned and whether it finished.
    ScriptValue ResumeThread(int reference, bool& finished) {
        ScriptValue result;
        RunInvocation([&] {
            const RunScope scope(*this, true);
            lua_rawgeti(m_state, LUA_REGISTRYINDEX, reference);
            lua_State* thread = lua_tothread(m_state, -1);
            lua_pop(m_state, 1);
            lua_sethook(thread, &OnInstructionLimit, LUA_MASKCOUNT, MaxInstructionsPerCall);
            int results = 0;
            const int status = lua_resume(thread, m_state, 0, &results);
            lua_sethook(thread, nullptr, 0, 0);
            if (status != LUA_OK && status != LUA_YIELD) {
                finished = true;
                const char* message = lua_tostring(thread, -1);
                const std::string error = message ? message : "Lua coroutine failed";
                lua_settop(thread, 0);
                throw std::runtime_error(error);
            }
            finished = status == LUA_OK;
            if (results > 0)
                result = ToScriptValue(thread, -results);
            lua_pop(thread, results);
        });
        return result;
    }
    void ReleaseReference(int reference) { luaL_unref(m_state, LUA_REGISTRYINDEX, reference); }

    ScriptObjectRef GetObject() override {
        return m_initialized ? std::make_shared<LuaGlobals>(*this, m_life) : nullptr;
    }

    // Runs a registry function, or a global one when the reference is LUA_NOREF, in the script's invocation scope.
    ScriptValue InvokeForeign(int reference, std::string_view global, std::span<const ScriptValue> arguments) {
        ScriptValue result;
        RunInvocation([&] {
            const RunScope scope(*this, true);
            if (reference != LUA_NOREF) {
                lua_rawgeti(m_state, LUA_REGISTRYINDEX, reference);
            } else {
                lua_pushglobaltable(m_state);
                lua_pushlstring(m_state, global.data(), global.size());
                const int type = lua_rawget(m_state, -2);
                lua_remove(m_state, -2);
                if (type != LUA_TFUNCTION) {
                    lua_pop(m_state, 1);
                    throw std::runtime_error("Unknown script function '" + std::string(global) + "'");
                }
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
    int GlobalType(std::string_view name) {
        lua_pushglobaltable(m_state);
        lua_pushlstring(m_state, name.data(), name.size());
        const int type = lua_rawget(m_state, -2);
        lua_pop(m_state, 2);
        return type;
    }
    ScriptValue GetGlobal(std::string_view name) {
        ScriptValue result;
        RunInvocation([&] {
            lua_pushglobaltable(m_state);
            lua_pushlstring(m_state, name.data(), name.size());
            lua_rawget(m_state, -2);
            struct Pop {
                lua_State* state;
                ~Pop() { lua_pop(state, 2); }
            } pop{m_state};
            result = ToScriptValue(m_state, -1);
        });
        return result;
    }
    void SetGlobal(std::string_view name, const ScriptValue& value) {
        RunInvocation([&] {
            lua_pushglobaltable(m_state);
            lua_pushlstring(m_state, name.data(), name.size());
            PushValue(m_state, value);
            lua_rawset(m_state, -3);
            lua_pop(m_state, 1);
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
                    void* memory = lua_newuserdatauv(state, sizeof(LuaStruct), 0);
                    new (memory) LuaStruct{value, owner, std::string(property)};
                    luaL_setmetatable(state, StructMetatable);
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
            return static_cast<double>(lua_tonumber(state, absIndex));
        case LUA_TSTRING: {
            std::size_t length = 0;
            const char* text = lua_tolstring(state, absIndex, &length);
            return std::string(text, length);
        }
        case LUA_TFUNCTION: {
            lua_pushvalue(state, absIndex);
            const int reference = luaL_ref(state, LUA_REGISTRYINDEX);
            return ScriptObjectRef(std::make_shared<LuaFunction>(*this, m_life, reference));
        }
        case LUA_TUSERDATA: {
            if (auto* structData = static_cast<LuaStruct*>(luaL_testudata(state, absIndex, StructMetatable)))
                return structData->value;
            if (auto* objectData = static_cast<LuaObject*>(luaL_testudata(state, absIndex, ObjectMetatable)))
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
        explicit RunScope(LuaRuntime& runtime, bool allowNested = false)
            : runtime(runtime), outermost(!runtime.m_running) {
            if (runtime.m_running && !allowNested)
                throw std::logic_error("A host callback cannot reenter its script instance");
            if (runtime.m_callDepth >= 32)
                throw std::runtime_error("Script lifecycle call depth limit exceeded");
            ++runtime.m_callDepth;
            if (outermost) {
                runtime.m_running = true;
                lua_sethook(runtime.m_state, &OnInstructionLimit, LUA_MASKCOUNT, MaxInstructionsPerCall);
            }
        }
        ~RunScope() {
            --runtime.m_callDepth;
            if (outermost) {
                lua_sethook(runtime.m_state, nullptr, 0, 0);
                runtime.m_running = false;
            }
        }
        LuaRuntime& runtime;
        bool outermost;
    };

    static LuaRuntime& FromState(lua_State* state) {
        lua_getfield(state, LUA_REGISTRYINDEX, RuntimeRegistryKey);
        auto* runtime = static_cast<LuaRuntime*>(lua_touserdata(state, -1));
        lua_pop(state, 1);
        return *runtime;
    }

    void PushObject(lua_State* state, const ScriptObjectRef& object) {
        if (!object) {
            lua_pushnil(state);
            return;
        }
        // A function of this script comes back as itself.
        if (const auto* function = dynamic_cast<const LuaFunction*>(object.get());
            function && function->runtime == this) {
            lua_rawgeti(state, LUA_REGISTRYINDEX, function->reference);
            return;
        }
        lua_getfield(state, LUA_REGISTRYINDEX, ObjectsRegistryKey);
        lua_pushlightuserdata(state, object.get());
        if (lua_rawget(state, -2) == LUA_TUSERDATA) {
            lua_remove(state, -2);
            return;
        }
        lua_pop(state, 1);

        void* memory = lua_newuserdatauv(state, sizeof(LuaObject), 0);
        new (memory) LuaObject{object, this};
        luaL_setmetatable(state, ObjectMetatable);

        lua_pushlightuserdata(state, object.get());
        lua_pushvalue(state, -2);
        lua_rawset(state, -4);
        lua_remove(state, -2);
    }

    void ProtectedCall(int argumentCount, int resultCount) {
        if (lua_pcall(m_state, argumentCount, resultCount, 0) != LUA_OK) {
            const char* message = lua_tostring(m_state, -1);
            const std::string error = message ? message : "Lua execution failed";
            lua_pop(m_state, 1);
            throw std::runtime_error(error);
        }
    }

    void ValidateHooks() const {
        for (const auto& [name, expectedCount] : m_expectedHooks) {
            lua_pushglobaltable(m_state);
            lua_pushlstring(m_state, name.data(), name.size());
            const int type = lua_rawget(m_state, -2);
            if (type == LUA_TNIL) {
                lua_pop(m_state, 2);
                continue;
            }
            if (type != LUA_TFUNCTION) {
                lua_pop(m_state, 2);
                throw std::runtime_error(m_name + ":1: " + name + " must be a function");
            }
            lua_Debug info{};
            lua_getinfo(m_state, ">u", &info);
            lua_pop(m_state, 1);
            if (info.isvararg || static_cast<std::size_t>(info.nparams) != expectedCount) {
                throw std::runtime_error(m_name + ":1: " + name + " expects " + std::to_string(expectedCount) +
                                         " parameter(s)");
            }
        }
    }

    static int LuaPrint(lua_State* state) {
        std::string message;
        const int count = lua_gettop(state);
        for (int index = 1; index <= count; ++index) {
            if (index > 1)
                message += ' ';
            if (lua_type(state, index) == LUA_TNUMBER) {
                if (lua_isinteger(state, index)) {
                    message += std::to_string(lua_tointeger(state, index));
                } else {
                    message +=
                        Detail::FormatScriptValue(ScriptValue(static_cast<double>(lua_tonumber(state, index))), "nil");
                }
            } else {
                std::size_t length = 0;
                const char* text = luaL_tolstring(state, index, &length);
                if (text)
                    message.append(text, length);
                lua_pop(state, 1);
            }
        }
        Detail::WriteScriptLog("Lua", message);
        return 0;
    }

    static int LuaConstructStruct(lua_State* state) {
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
        return RaiseLuaError(state, failure);
    }

    static int LuaStructGc(lua_State* state) {
        if (auto* self = static_cast<LuaStruct*>(luaL_testudata(state, 1, StructMetatable)))
            std::destroy_at(self);
        return 0;
    }

    static int LuaStructIndex(lua_State* state) {
        auto& runtime = FromState(state);
        auto* self = static_cast<LuaStruct*>(luaL_checkudata(state, 1, StructMetatable));
        std::size_t length = 0;
        const char* name = lua_type(state, 2) == LUA_TSTRING ? lua_tolstring(state, 2, &length) : nullptr;
        std::string failure;
        try {
            if (!name)
                throw std::runtime_error("Property name must be a string");
            runtime.PushValue(state, Detail::GetScriptStructField(self->value, std::string_view(name, length)));
            return 1;
        } catch (const std::exception& error) {
            failure = error.what();
        }
        return RaiseLuaError(state, failure);
    }

    static int LuaStructNewIndex(lua_State* state) {
        auto& runtime = FromState(state);
        auto* self = static_cast<LuaStruct*>(luaL_checkudata(state, 1, StructMetatable));
        std::size_t length = 0;
        const char* name = lua_type(state, 2) == LUA_TSTRING ? lua_tolstring(state, 2, &length) : nullptr;
        std::string failure;
        try {
            if (!name)
                throw std::runtime_error("Property name must be a string");
            ScriptValue updated = self->value;
            Detail::SetScriptStructField(updated, std::string_view(name, length), runtime.ToScriptValue(state, 3));
            if (!self->property.empty()) {
                auto owner = self->owner.lock();
                if (!owner)
                    throw std::runtime_error("Object reference is no longer valid");
                owner->Set(self->property, updated);
            }
            self->value = std::move(updated);
            return 0;
        } catch (const std::exception& error) {
            failure = error.what();
        }
        return RaiseLuaError(state, failure);
    }

    static int LuaStructEqual(lua_State* state) {
        const auto* left = static_cast<const LuaStruct*>(luaL_testudata(state, 1, StructMetatable));
        const auto* right = static_cast<const LuaStruct*>(luaL_testudata(state, 2, StructMetatable));
        lua_pushboolean(state, left && right && left->value == right->value);
        return 1;
    }

    static int LuaStructToString(lua_State* state) {
        const auto* self = static_cast<const LuaStruct*>(luaL_checkudata(state, 1, StructMetatable));
        const std::string formatted = Detail::FormatScriptValue(self->value, "nil");
        lua_pushlstring(state, formatted.data(), formatted.size());
        return 1;
    }

    static int LuaObjectGc(lua_State* state) {
        if (auto* self = static_cast<LuaObject*>(luaL_testudata(state, 1, ObjectMetatable)))
            std::destroy_at(self);
        return 0;
    }

    static int LuaObjectMethodCall(lua_State* state) {
        auto* self = static_cast<LuaObject*>(luaL_checkudata(state, lua_upvalueindex(1), ObjectMetatable));
        std::size_t length = 0;
        const char* method = lua_tolstring(state, lua_upvalueindex(2), &length);
        std::string failure;
        try {
            const int firstArg = lua_gettop(state) >= 1 && lua_rawequal(state, 1, lua_upvalueindex(1)) ? 2 : 1;
            const auto arguments = self->runtime->ToArguments(state, firstArg);
            const ScriptValue result = self->object->Call(std::string_view(method, length), arguments);
            self->runtime->PushValue(state, result);
            return 1;
        } catch (const std::exception& error) {
            failure = error.what();
        }
        return RaiseLuaError(state, failure);
    }

    // Calls a function of another script, such as an event callback or a function value it returned.
    static int LuaObjectInvoke(lua_State* state) {
        auto* self = static_cast<LuaObject*>(luaL_checkudata(state, 1, ObjectMetatable));
        std::string failure;
        try {
            const auto arguments = self->runtime->ToArguments(state, 2);
            const ScriptValue result = self->object->Invoke(arguments);
            self->runtime->PushValue(state, result);
            return 1;
        } catch (const std::exception& error) {
            failure = error.what();
        }
        return RaiseLuaError(state, failure);
    }

    static int LuaObjectIndex(lua_State* state) {
        auto* self = static_cast<LuaObject*>(luaL_checkudata(state, 1, ObjectMetatable));
        std::size_t length = 0;
        const char* key = lua_type(state, 2) == LUA_TSTRING ? lua_tolstring(state, 2, &length) : nullptr;
        std::string failure;
        try {
            if (!key)
                throw std::runtime_error("Property name must be a string");
            const std::string_view name(key, length);
            try {
                const ScriptValue value = self->object->Get(name);
                self->runtime->PushValue(state, value, self->object, name);
                return 1;
            } catch (const std::exception& error) {
                const std::string_view message = error.what();
                if (message.starts_with("Unknown script property ") && self->object->HasMethod(name)) {
                    lua_pushvalue(state, 1);
                    lua_pushvalue(state, 2);
                    lua_pushcclosure(state, &LuaObjectMethodCall, 2);
                    return 1;
                }
                throw;
            }
        } catch (const std::exception& error) {
            failure = error.what();
        }
        return RaiseLuaError(state, failure);
    }

    static int LuaObjectNewIndex(lua_State* state) {
        auto* self = static_cast<LuaObject*>(luaL_checkudata(state, 1, ObjectMetatable));
        std::size_t length = 0;
        const char* key = lua_type(state, 2) == LUA_TSTRING ? lua_tolstring(state, 2, &length) : nullptr;
        std::string failure;
        try {
            if (!key)
                throw std::runtime_error("Property name must be a string");
            const ScriptValue value = self->runtime->ToScriptValue(state, 3);
            self->object->Set(std::string_view(key, length), value);
            return 0;
        } catch (const std::exception& error) {
            failure = error.what();
        }
        return RaiseLuaError(state, failure);
    }

    static int LuaObjectEqual(lua_State* state) {
        const auto* left = static_cast<const LuaObject*>(luaL_testudata(state, 1, ObjectMetatable));
        const auto* right = static_cast<const LuaObject*>(luaL_testudata(state, 2, ObjectMetatable));
        lua_pushboolean(state, left && right && left->object == right->object);
        return 1;
    }

    static int LuaObjectToString(lua_State* state) {
        lua_pushliteral(state, "<object>");
        return 1;
    }

    static int LuaGlobalIndex(lua_State* state) {
        std::size_t length = 0;
        const char* key = lua_type(state, 2) == LUA_TSTRING ? lua_tolstring(state, 2, &length) : nullptr;
        if (key) {
            lua_getfield(state, LUA_REGISTRYINDEX, DeclaredGlobalsKey);
            lua_pushvalue(state, 2);
            const bool declared = lua_rawget(state, -2) == LUA_TBOOLEAN && lua_toboolean(state, -1) != 0;
            lua_pop(state, 2);
            if (declared) {
                lua_pushnil(state);
                return 1;
            }
            return RaiseLuaError(state, "Unknown variable '" + std::string(key, length) + "'");
        }
        return RaiseLuaError(state, "Unknown variable");
    }

    static int LuaGlobalNewIndex(lua_State* state) {
        std::size_t length = 0;
        const char* key = lua_type(state, 2) == LUA_TSTRING ? lua_tolstring(state, 2, &length) : nullptr;
        if (key && ScriptApi::IsValueConstructor(std::string_view(key, length)))
            return RaiseLuaError(state, "Cannot rebind value constructor '" + std::string(key, length) + "'");
        if (key) {
            lua_getfield(state, LUA_REGISTRYINDEX, DeclaredGlobalsKey);
            lua_pushvalue(state, 2);
            lua_pushboolean(state, 1);
            lua_rawset(state, -3);
            lua_pop(state, 1);
        }
        lua_rawset(state, 1);
        return 0;
    }

    static int LuaModuleEnvIndex(lua_State* state) {
        lua_pushvalue(state, 2);
        if (lua_rawget(state, lua_upvalueindex(1)) != LUA_TNIL)
            return 1;
        lua_pop(state, 1);

        lua_pushglobaltable(state);
        lua_pushvalue(state, 2);
        if (lua_rawget(state, -2) != LUA_TNIL) {
            lua_remove(state, -2);
            return 1;
        }
        lua_pop(state, 2);

        std::size_t length = 0;
        const char* key = lua_type(state, 2) == LUA_TSTRING ? lua_tolstring(state, 2, &length) : nullptr;
        if (key)
            return RaiseLuaError(state, "Unknown variable '" + std::string(key, length) + "'");
        return RaiseLuaError(state, "Unknown variable");
    }

    static int LuaModuleEnvNewIndex(lua_State* state) {
        std::size_t length = 0;
        const char* key = lua_type(state, 2) == LUA_TSTRING ? lua_tolstring(state, 2, &length) : nullptr;
        if (key && ScriptApi::IsValueConstructor(std::string_view(key, length)))
            return RaiseLuaError(state, "Cannot rebind value constructor '" + std::string(key, length) + "'");
        if (key) {
            lua_pushvalue(state, 2);
            lua_pushboolean(state, 1);
            lua_rawset(state, lua_upvalueindex(1));
        }
        lua_rawset(state, 1);
        return 0;
    }

    static int LuaModuleProxyIndex(lua_State* state) {
        lua_pushvalue(state, 2);
        if (lua_rawget(state, lua_upvalueindex(1)) != LUA_TNIL)
            return 1;
        lua_pop(state, 1);

        lua_pushvalue(state, 2);
        if (lua_rawget(state, lua_upvalueindex(2)) == LUA_TBOOLEAN) {
            lua_pop(state, 1);
            lua_pushnil(state);
            return 1;
        }
        lua_pop(state, 1);

        std::size_t length = 0;
        const char* key = lua_type(state, 2) == LUA_TSTRING ? lua_tolstring(state, 2, &length) : nullptr;
        return RaiseLuaError(state, "Unknown module member '" + (key ? std::string(key, length) : std::string()) + "'");
    }

    static int LuaModuleProxyNewIndex(lua_State* state) { return RaiseLuaError(state, "Cannot write module member"); }

    static int LuaRequire(lua_State* state) {
        auto& runtime = FromState(state);
        if (lua_gettop(state) != 1 || lua_type(state, 1) != LUA_TSTRING)
            return RaiseLuaError(state, "require expects a module name string");

        std::size_t length = 0;
        const char* rawName = lua_tolstring(state, 1, &length);
        const std::string request(rawName, length);
        if (request.empty())
            return RaiseLuaError(state, "Module name cannot be empty");

        if (const auto native = runtime.m_libraries.find(request); native != runtime.m_libraries.end()) {
            runtime.PushObject(state, native->second);
            return 1;
        }

        if (request == "math" || request == "string" || request == "table" || request == "utf8" ||
            request == "coroutine") {
            lua_pushglobaltable(state);
            lua_pushlstring(state, request.data(), request.size());
            lua_rawget(state, -2);
            lua_remove(state, -2);
            return 1;
        }

        std::string failure;
        try {
            if (!runtime.m_loader)
                throw std::runtime_error("Cannot load module '" + request + "'");
            const std::string importer = runtime.m_moduleStack.empty() ? runtime.m_name : runtime.m_moduleStack.back();
            const std::string resolved = Detail::ResolveScriptModulePath(request, ".lua", true);
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

            const std::string chunkName = "@" + source->sourceName;
            if (luaL_loadbufferx(state, source->source.data(), source->source.size(), chunkName.c_str(), "t") !=
                LUA_OK) {
                const char* error = lua_tostring(state, -1);
                throw std::runtime_error(error ? error : "Failed to parse module");
            }

            // Isolated module environment: reads standard globals, keeps module declarations private.
            lua_newtable(state); // env
            lua_newtable(state); // declared set
            lua_newtable(state); // env metatable
            lua_pushvalue(state, -2);
            lua_pushcclosure(state, &LuaModuleEnvIndex, 1);
            lua_setfield(state, -2, "__index");
            lua_pushvalue(state, -2);
            lua_pushcclosure(state, &LuaModuleEnvNewIndex, 1);
            lua_setfield(state, -2, "__newindex");
            lua_setmetatable(state, -3);

            // Stack: [modulesTable, chunk, env, declared]
            lua_pushvalue(state, -2); // copy env for _ENV upvalue
            lua_setupvalue(state, -4, 1);

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

            // Rotate chunk above env and declared so env/declared stay anchored if the module returns nil.
            // Stack before rotate: [modulesTable, chunk, env, declared]
            lua_rotate(state, -3, -1); // [modulesTable, env, declared, chunk]
            if (lua_pcall(state, 0, 1, 0) != LUA_OK) {
                const char* error = lua_tostring(state, -1);
                throw std::runtime_error(error ? error : "Module initialization failed");
            }
            // Stack: [modulesTable, env, declared, returnValue]
            if (lua_isnil(state, -1)) {
                lua_pop(state, 1); // pop nil
                // Create read-only module proxy wrapping (env, declared).
                lua_newuserdatauv(state, 1, 0);
                lua_newtable(state);
                lua_pushvalue(state, -4); // env
                lua_pushvalue(state, -4); // declared
                lua_pushcclosure(state, &LuaModuleProxyIndex, 2);
                lua_setfield(state, -2, "__index");
                lua_pushcfunction(state, &LuaModuleProxyNewIndex);
                lua_setfield(state, -2, "__newindex");
                lua_setmetatable(state, -2);
            }
            // Stack: [modulesTable, env, declared, moduleResult]
            lua_remove(state, -2); // remove declared
            lua_remove(state, -2); // remove env
            // Stack: [modulesTable, moduleResult]
            lua_pushlstring(state, source->sourceName.data(), source->sourceName.size());
            lua_pushvalue(state, -2);
            lua_rawset(state, -4);
            lua_remove(state, -2);
            return 1;
        } catch (const std::exception& error) {
            failure = error.what();
        }
        return RaiseLuaError(state, failure);
    }

    void SetUpState() {
        lua_pushlightuserdata(m_state, this);
        lua_setfield(m_state, LUA_REGISTRYINDEX, RuntimeRegistryKey);

        // Weak-valued table for ScriptObject* -> LuaObject userdata identity cache.
        lua_newtable(m_state);
        lua_newtable(m_state);
        lua_pushliteral(m_state, "v");
        lua_setfield(m_state, -2, "__mode");
        lua_setmetatable(m_state, -2);
        lua_setfield(m_state, LUA_REGISTRYINDEX, ObjectsRegistryKey);

        // Loaded source modules cache per instance.
        lua_newtable(m_state);
        lua_setfield(m_state, LUA_REGISTRYINDEX, ModulesRegistryKey);

        // Declared root globals set.
        lua_newtable(m_state);
        lua_setfield(m_state, LUA_REGISTRYINDEX, DeclaredGlobalsKey);

        // Safe standard libraries only.
        luaL_requiref(m_state, LUA_GNAME, luaopen_base, 1);
        lua_pop(m_state, 1);
        luaL_requiref(m_state, LUA_MATHLIBNAME, luaopen_math, 1);
        lua_pop(m_state, 1);
        luaL_requiref(m_state, LUA_STRLIBNAME, luaopen_string, 1);
        lua_pop(m_state, 1);
        luaL_requiref(m_state, LUA_TABLIBNAME, luaopen_table, 1);
        lua_pop(m_state, 1);
        luaL_requiref(m_state, LUA_UTF8LIBNAME, luaopen_utf8, 1);
        lua_pop(m_state, 1);
        luaL_requiref(m_state, LUA_COLIBNAME, luaopen_coroutine, 1);
        lua_pop(m_state, 1);

        // Remove raw filesystem / arbitrary chunk loaders.
        lua_pushglobaltable(m_state);
        for (const char* unsafe : {"dofile", "loadfile", "load"}) {
            lua_pushnil(m_state);
            lua_rawsetfield(m_state, -2, unsafe);
        }

        lua_pushcfunction(m_state, &LuaPrint);
        lua_rawsetfield(m_state, -2, "print");

        lua_pushcfunction(m_state, &LuaRequire);
        lua_rawsetfield(m_state, -2, "require");

        for (const auto constructor : ScriptApi::ValueConstructors) {
            lua_pushlstring(m_state, constructor.data(), constructor.size());
            lua_pushcclosure(m_state, &LuaConstructStruct, 1);
            lua_rawsetfield(m_state, -2, constructor.data());
        }

        // Strict global access: reading undeclared globals faults with source line diagnostics.
        lua_newtable(m_state);
        lua_pushcfunction(m_state, &LuaGlobalIndex);
        lua_setfield(m_state, -2, "__index");
        lua_pushcfunction(m_state, &LuaGlobalNewIndex);
        lua_setfield(m_state, -2, "__newindex");
        lua_setmetatable(m_state, -2);
        lua_pop(m_state, 1);

        // Struct metatable.
        luaL_newmetatable(m_state, StructMetatable);
        lua_pushcfunction(m_state, &LuaStructGc);
        lua_setfield(m_state, -2, "__gc");
        lua_pushcfunction(m_state, &LuaStructIndex);
        lua_setfield(m_state, -2, "__index");
        lua_pushcfunction(m_state, &LuaStructNewIndex);
        lua_setfield(m_state, -2, "__newindex");
        lua_pushcfunction(m_state, &LuaStructEqual);
        lua_setfield(m_state, -2, "__eq");
        lua_pushcfunction(m_state, &LuaStructToString);
        lua_setfield(m_state, -2, "__tostring");
        lua_pop(m_state, 1);

        // ScriptObject metatable.
        luaL_newmetatable(m_state, ObjectMetatable);
        lua_pushcfunction(m_state, &LuaObjectGc);
        lua_setfield(m_state, -2, "__gc");
        lua_pushcfunction(m_state, &LuaObjectIndex);
        lua_setfield(m_state, -2, "__index");
        lua_pushcfunction(m_state, &LuaObjectNewIndex);
        lua_setfield(m_state, -2, "__newindex");
        lua_pushcfunction(m_state, &LuaObjectEqual);
        lua_setfield(m_state, -2, "__eq");
        lua_pushcfunction(m_state, &LuaObjectToString);
        lua_setfield(m_state, -2, "__tostring");
        lua_pushcfunction(m_state, &LuaObjectInvoke);
        lua_setfield(m_state, -2, "__call");
        lua_pop(m_state, 1);
    }

    static void lua_rawsetfield(lua_State* state, int index, const char* key) {
        const int absIndex = lua_absindex(state, index);
        lua_pushstring(state, key);
        lua_insert(state, -2);
        lua_rawset(state, absIndex);
    }

    lua_State* m_state = nullptr;
    // Expired once the runtime ends, so functions and globals it exposed stop reaching its state.
    std::shared_ptr<void> m_life = std::make_shared<int>(0);
    std::string m_name;
    int m_mainChunkRef = LUA_NOREF;
    bool m_initialized = false;
    bool m_running = false;
    std::size_t m_callDepth = 0;
    std::unordered_map<std::string, ScriptObjectRef> m_libraries;
    mutable std::unordered_map<std::string, std::size_t> m_expectedHooks;
    ScriptSourceLoader m_loader;
    std::vector<std::string> m_loadingModules;
    std::vector<std::string> m_moduleStack;
};

LuaFunction::~LuaFunction() {
    if (!life.expired())
        luaL_unref(runtime->GetState(), LUA_REGISTRYINDEX, reference);
}
ScriptValue LuaFunction::Invoke(std::span<const ScriptValue> arguments) {
    if (life.expired())
        throw std::runtime_error("The script instance is no longer available");
    return runtime->InvokeForeign(reference, {}, arguments);
}

LuaCoroutine::~LuaCoroutine() {
    if (!life.expired())
        runtime->ReleaseReference(thread);
}
ScriptValue LuaCoroutine::Call(std::string_view name, std::span<const ScriptValue> arguments) {
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

LuaRuntime& LuaGlobals::Runtime() const {
    if (life.expired())
        throw std::runtime_error("The script instance is no longer available");
    return *runtime;
}
bool LuaGlobals::HasMethod(std::string_view name) const {
    return Runtime().GlobalType(name) == LUA_TFUNCTION;
}
bool LuaGlobals::HasProperty(std::string_view name) const {
    const int type = Runtime().GlobalType(name);
    return type != LUA_TNIL && type != LUA_TFUNCTION;
}
ScriptValue LuaGlobals::Get(std::string_view name) const {
    // Functions are methods: callers bind them through Call().
    if (Runtime().GlobalType(name) == LUA_TFUNCTION)
        return ScriptObject::Get(name);
    return Runtime().GetGlobal(name);
}
void LuaGlobals::Set(std::string_view name, const ScriptValue& value) {
    Runtime().SetGlobal(name, value);
}
ScriptValue LuaGlobals::Call(std::string_view name, std::span<const ScriptValue> arguments) {
    return Runtime().InvokeForeign(LUA_NOREF, name, arguments);
}

} // namespace

std::optional<ScriptDiagnostic> Detail::CheckLuaSyntax(std::string_view text, std::string_view name) {
    lua_State* state = luaL_newstate();
    if (!state)
        return std::nullopt;
    const std::string chunkName = "@" + std::string(name);
    std::optional<ScriptDiagnostic> diagnostic;
    if (luaL_loadbufferx(state, text.data(), text.size(), chunkName.c_str(), "t") != LUA_OK) {
        // Lua reports "name:line: message".
        const char* error = lua_tostring(state, -1);
        std::string message = error ? error : "Syntax error";
        ScriptDiagnostic result;
        const auto prefix = std::string(name) + ":";
        if (message.starts_with(prefix)) {
            const auto colon = message.find(':', prefix.size());
            if (colon != std::string::npos) {
                result.line = std::strtoull(message.c_str() + prefix.size(), nullptr, 10);
                message.erase(0, colon + 1);
                if (message.starts_with(' '))
                    message.erase(0, 1);
            }
        }
        result.message = std::move(message);
        diagnostic = std::move(result);
    }
    lua_close(state);
    return diagnostic;
}

std::unique_ptr<ScriptRuntime> Detail::CreateLuaRuntime(std::string_view text, std::string_view name) {
    return std::make_unique<LuaRuntime>(text, name);
}

} // namespace Spot
