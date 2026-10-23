#include "bytecode.h"
#include "collections.h"
#include "runtime.h"
#include "standard_library.h"

#include <algorithm>

#include <cmath>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Vice::Detail {

struct Variable {
    Value value;
    bool constant = false;
};

using Scope = std::unordered_map<std::string, Variable>;

struct ScriptInstance;

struct ScriptClass;
struct FunctionObject;

// A captured variable. While open it aliases a live value stack slot; it closes over a copy when that slot's
// binding ends, so closures created in different loop iterations or calls see independent variables.
struct VmStack;
struct Upvalue final : Object {
    // The stack whose slot it aliases while open: the main stack or a coroutine's.
    VmStack* stack = nullptr;
    std::size_t slot = 0;
    bool open = true;
    Value closed;
    Value& Get();
};

struct CallRequest {
    InstanceData* owner = nullptr;
    const CompiledFunction* function = nullptr;
    std::shared_ptr<ScriptInstance> receiver;
    std::shared_ptr<ScriptClass> definingClass;
    std::shared_ptr<ScriptClass> construction;
    std::span<const Value> arguments;
    std::shared_ptr<const FunctionObject> closure;
};

struct ProfileEntry {
    FunctionProfile data;
};

struct Frame {
    // Where profiling adds this frame's own work, while profiling is on.
    ProfileEntry* profile = nullptr;
    // The line it last ran, so the debugger stops once per line.
    std::size_t debugLine = 0;
    InstanceData* owner = nullptr;
    const Bytecode* code = nullptr;
    // What a stack trace calls the frame: its function, or the class it constructs.
    const Token* name = nullptr;
    std::size_t ip = 0;
    std::size_t localBase = 0;
    std::size_t returnBase = 0;
    std::size_t targetBase = 0;
    std::shared_ptr<ScriptInstance> receiver;
    std::shared_ptr<ScriptClass> definingClass;
    std::shared_ptr<ScriptClass> construction;
    std::shared_ptr<const FunctionObject> closure;
    std::size_t fieldIndex = 0;
    std::size_t argumentCount = 0;
    bool waitingForField = false;
    bool constructorCalled = false;
};

struct WriteTarget {
    enum class Kind { Local, Global, Property, Temporary, Member, Index, Constant, Upvalue };
    Kind kind = Kind::Temporary;
    Value value;
    Value index;
    Token token;
    std::size_t slot = 0;
    bool constant = false;
    ObjectRef object;
    std::shared_ptr<Upvalue> upvalue;
};

// Operands, call frames, assignment targets and open captures of one line of execution. Each coroutine has its own,
// so its frames survive while it is suspended; slots index into it.
struct VmStack {
    std::vector<Value> values;
    std::vector<Frame> frames;
    std::vector<WriteTarget> targets;
    std::vector<std::shared_ptr<Upvalue>> openUpvalues;
    // Ends the bindings of captured slots at or above `from` (or exactly `from` when `exact`).
    void CloseUpvalues(std::size_t from, bool exact = false) {
        std::erase_if(openUpvalues, [&](const std::shared_ptr<Upvalue>& upvalue) {
            if (exact ? upvalue->slot != from : upvalue->slot < from)
                return false;
            upvalue->closed = values[upvalue->slot];
            upvalue->open = false;
            upvalue->stack = nullptr;
            return true;
        });
    }
};

inline Value& Upvalue::Get() {
    return open ? stack->values[slot] : closed;
}

struct CoroutineObject;

struct ModuleSession : std::enable_shared_from_this<ModuleSession> {
    ModuleSession();
    std::unordered_map<std::string, ObjectRef> libraries = CreateStandardLibraries();
    std::unordered_map<std::string, ObjectRef> modules;
    std::unordered_map<std::string, ObjectRef> classes;
    std::unordered_map<std::string, std::shared_ptr<InstanceData>> moduleInstances;
    std::unordered_map<std::string, NativeFunction> natives;
    ModuleLoader loader;
    std::vector<std::string> loading;
    bool running = false;
    std::size_t steps = 0;
    // The main stack, or the running coroutine's.
    VmStack mainStack;
    VmStack* stack = &mainStack;
    // The running coroutine, how deeply ExecuteVm is nested, and what the last `yield` gave.
    CoroutineObject* coroutine = nullptr;
    std::size_t vmDepth = 0;
    bool yielded = false;
    Value yieldValue;
    std::vector<std::weak_ptr<CoroutineObject>> coroutines;
    // Debugging: breakpoints by source, and the step requested when the handler last returned.
    DebugHandler debugHandler;
    std::unordered_map<std::string, std::unordered_set<std::size_t>> breakpoints;
    DebugAction step = DebugAction::Continue;
    std::size_t stepDepth = 0;
    const VmStack* stepStack = nullptr;
    bool IsDebugging() const { return debugHandler && (!breakpoints.empty() || step != DebugAction::Continue); }
    // Profiling: an entry per code block, and the clock of the last instruction, charged to the frame that ran it.
    bool profiling = false;
    std::unordered_map<const Bytecode*, ProfileEntry> profile;
    ProfileEntry* profiled = nullptr;
    std::chrono::steady_clock::time_point profiledAt;
    void ChargeProfile() {
        if (!profiled)
            return;
        const auto now = std::chrono::steady_clock::now();
        profiled->data.time += now - profiledAt;
        profiledAt = now;
    }
    void CountAllocation() {
        if (profiling && !stack->frames.empty() && stack->frames.back().profile)
            ++stack->frames.back().profile->data.allocations;
    }
    ObjectRef MakeCoroutine(const ObjectRef& function);
    std::vector<Value> arguments;
    std::optional<CallRequest> pending;
    bool dispatching = false;
    bool hostCallback = false;
    std::size_t nativeDepth = 0;
    std::size_t callbackDepth = 0;
    std::vector<std::vector<Value>> nativeArguments;
    std::vector<std::weak_ptr<ScriptInstance>> instances;
    std::vector<std::weak_ptr<Collection>> collections;
    std::vector<std::weak_ptr<FunctionObject>> closures;
    // Cycle collection runs between host invocations once enough containers were allocated since the last one.
    static constexpr std::size_t MinimumCollectionThreshold = 4096;
    std::size_t allocations = 0;
    std::size_t collectionThreshold = MinimumCollectionThreshold;
    std::size_t CollectCycles();
    void MaybeCollectCycles() {
        if (allocations >= collectionThreshold)
            CollectCycles();
    }
    std::shared_ptr<Upvalue> Capture(std::size_t slot) {
        for (const auto& upvalue : stack->openUpvalues) {
            if (upvalue->slot == slot)
                return upvalue;
        }
        auto upvalue = std::make_shared<Upvalue>();
        upvalue->slot = slot;
        upvalue->stack = stack;
        stack->openUpvalues.push_back(upvalue);
        return upvalue;
    }
    void CloseUpvalues(std::size_t from, bool exact = false) { stack->CloseUpvalues(from, exact); }
    void TrackClosure(const std::shared_ptr<FunctionObject>& closure) {
        if (closures.size() % 256 == 0)
            std::erase_if(closures, [](const auto& weak) { return weak.expired(); });
        closures.push_back(closure);
        ++allocations;
        CountAllocation();
    }
    void TrackCollection(const Value& value) {
        const auto* reference = std::get_if<ObjectRef>(&value);
        const auto collection = reference ? std::dynamic_pointer_cast<Collection>(*reference) : nullptr;
        if (collection && !collection->owner) {
            if (collections.size() % 256 == 0)
                std::erase_if(collections, [](const auto& weak) { return weak.expired(); });
            collection->owner = this;
            CountAllocation();
            collection->track = [](void* owner, const Value& value) {
                static_cast<ModuleSession*>(owner)->TrackCollection(value);
            };
            collections.push_back(collection);
            ++allocations;
        }
    }
    ~ModuleSession();
};

struct RunScope {
    explicit RunScope(ModuleSession& session) : session(session) {
        if (session.running)
            throw std::logic_error("A host callback cannot reenter its script instance");
        session.MaybeCollectCycles();
        session.running = true;
        session.steps = 0;
    }
    ~RunScope() { session.running = false; }
    ModuleSession& session;
};

struct HostScope {
    explicit HostScope(ModuleSession& session) : session(session), previous(session.hostCallback) {
        session.hostCallback = true;
    }
    ~HostScope() { session.hostCallback = previous; }
    ModuleSession& session;
    bool previous;
};

// A native call may synchronously invoke another script hook. Keep its argument span stable across VM growth.
struct NativeScope {
    NativeScope(ModuleSession& session, std::span<const Value> arguments) : session(session), host(session) {
        if (session.nativeArguments.size() == session.nativeDepth)
            session.nativeArguments.emplace_back();
        session.nativeArguments[session.nativeDepth].assign(arguments.begin(), arguments.end());
        index = session.nativeDepth++;
    }
    ~NativeScope() {
        session.nativeArguments[index].clear();
        --session.nativeDepth;
    }
    std::span<const Value> Arguments() const { return session.nativeArguments[index]; }
    ModuleSession& session;
    HostScope host;
    std::size_t index = 0;
};

struct CallbackEntry {
    explicit CallbackEntry(ModuleSession& session)
        : session(session), dispatching(session.dispatching), hostCallback(session.hostCallback) {
        if (session.callbackDepth >= 16)
            throw std::runtime_error("Native callback depth limit exceeded");
        ++session.callbackDepth;
        session.dispatching = false;
        session.hostCallback = false;
    }
    ~CallbackEntry() {
        session.dispatching = dispatching;
        session.hostCallback = hostCallback;
        --session.callbackDepth;
    }
    ModuleSession& session;
    bool dispatching;
    bool hostCallback;
};

template <typename F>
Value EnterObject(ModuleSession& session, F&& invoke) {
    std::optional<RunScope> run;
    if (!session.running)
        run.emplace(session);
    std::optional<CallbackEntry> callback;
    if (session.hostCallback)
        callback.emplace(session);
    return invoke();
}

struct ModuleObject final : Object {
    ModuleObject(const std::shared_ptr<InstanceData>& instance, std::weak_ptr<ModuleSession> session)
        : instance(instance), session(std::move(session)) {}
    Value Get(std::string_view name) const override;
    Value Call(std::string_view name, std::span<const Value> arguments) override;
    std::weak_ptr<InstanceData> instance;
    std::weak_ptr<ModuleSession> session;
};

struct ScriptClass final : Object {
    const CompiledClass* declaration = nullptr;
    std::shared_ptr<const CompiledClass> ownedDeclaration;
    ObjectRef host;
    std::unordered_set<std::string> fieldNames;
    std::unordered_map<std::string, std::size_t> signatures;
    std::weak_ptr<InstanceData*> lifetime;
    std::shared_ptr<ScriptClass> base;
    std::unordered_map<std::string, const CompiledFunction*> methods;
    std::vector<std::pair<InstanceData*, const CompiledVariable*>> fieldInitializers;
    bool resolving = false;
    bool resolved = false;
    InstanceData& Owner() const;
    const CompiledFunction* FindMethod(std::string_view name) const {
        Owner();
        const auto found = methods.find(std::string(name));
        return found != methods.end() ? found->second : nullptr;
    }
};

struct ScriptInstance final : ManagedObject, std::enable_shared_from_this<ScriptInstance> {
    std::shared_ptr<ScriptClass> type;
    Scope fields;
    bool HasMethod(std::string_view name) const override;
    bool HasProperty(std::string_view name) const override;
    Value Get(std::string_view name) const override;
    void Set(std::string_view name, const Value& value) override;
    Value Call(std::string_view name, std::span<const Value> arguments) override;
    std::vector<std::string> GetFieldNames() const override {
        std::vector<std::string> names;
        for (const auto& [owner, field] : type->fieldInitializers)
            names.push_back(field->token.text);
        return names;
    }
    Value Dispatch(std::shared_ptr<ScriptClass> start, std::string_view name, std::span<const Value> arguments);
};

// An enum declaration: its members are read-only properties, and values() lists them in order.
struct EnumType final : Object {
    std::vector<std::shared_ptr<EnumValue>> members;
    std::unordered_map<std::string, std::size_t> indices;
    std::string_view GetTypeName() const override { return "enum"; }
    bool HasProperty(std::string_view name) const override { return indices.contains(std::string(name)); }
    bool HasMethod(std::string_view name) const override { return name == "values"; }
    Value Get(std::string_view name) const override {
        if (const auto found = indices.find(std::string(name)); found != indices.end())
            return ObjectRef(members[found->second]);
        return Object::Get(name);
    }
    Value Call(std::string_view name, std::span<const Value> arguments) override {
        if (name != "values")
            return Object::Call(name, arguments);
        if (!arguments.empty())
            throw std::runtime_error("values() takes no arguments");
        std::vector<Value> values(members.begin(), members.end());
        return MakeArray(values);
    }
};

struct SuperObject final : Object {
    std::shared_ptr<ScriptInstance> instance;
    std::shared_ptr<ScriptClass> base;
    Value Call(std::string_view name, std::span<const Value> arguments) override;
};

template <typename T>
bool SameOwner(const std::weak_ptr<T>& a, const std::weak_ptr<T>& b) {
    return !a.owner_before(b) && !b.owner_before(a);
}

// A file-scope function read as a value, or a closure. File-scope functions keep no receiver: free functions never
// inherit an instance. Closures keep their captured variables and the receiver of the code that created them.
struct FunctionObject final : CallableObject, std::enable_shared_from_this<FunctionObject> {
    std::weak_ptr<InstanceData*> lifetime;
    const CompiledFunction* function = nullptr;
    bool closure = false;
    std::vector<std::shared_ptr<Upvalue>> upvalues;
    std::shared_ptr<ScriptInstance> receiver;
    std::shared_ptr<ScriptClass> definingClass;
    InstanceData& Owner() const;
    Value Invoke(std::span<const Value> arguments) override;
    bool IsSameCallable(const CallableObject& other) const override {
        if (closure)
            return &other == this;
        const auto* value = dynamic_cast<const FunctionObject*>(&other);
        return value && !value->closure && value->function == function && SameOwner(value->lifetime, lifetime);
    }
};

// A host function bound with Instance::Bind, such as print, read as a value.
struct NativeCallable final : CallableObject {
    NativeCallable(std::weak_ptr<ModuleSession> session, std::string name)
        : session(std::move(session)), name(std::move(name)) {}
    Value Invoke(std::span<const Value> arguments) override;
    bool IsSameCallable(const CallableObject& other) const override {
        const auto* value = dynamic_cast<const NativeCallable*>(&other);
        return value && value->name == name && SameOwner(value->session, session);
    }
    std::weak_ptr<ModuleSession> session;
    std::string name;
};

// Script objects of the running session schedule VM frames directly; anything else is a host callback.
bool InSession(const Object* object, const ModuleSession* session);

// A function running on its own stack, suspended at each `yield` until resumed.
struct CoroutineObject final : Object {
    enum class Status { Suspended, Running, Dead };
    std::weak_ptr<ModuleSession> session;
    ObjectRef function;
    std::unique_ptr<VmStack> stack = std::make_unique<VmStack>();
    Status status = Status::Suspended;
    bool started = false;
    std::size_t baseDepth = 0;
    ~CoroutineObject() override { Finish(); }
    std::string_view GetTypeName() const override { return "coroutine"; }
    bool HasProperty(std::string_view name) const override { return name == "done" || name == "status"; }
    bool HasMethod(std::string_view name) const override { return name == "resume"; }
    Value Get(std::string_view name) const override {
        if (name == "done")
            return status == Status::Dead;
        if (name == "status")
            return std::string(status == Status::Suspended ? "suspended"
                               : status == Status::Running ? "running"
                                                           : "dead");
        return Object::Get(name);
    }
    Value Call(std::string_view name, std::span<const Value> arguments) override;
    Value Resume(ModuleSession& owner);
    // Captures of a finished coroutine keep their last values.
    void Finish() {
        stack->CloseUpvalues(0);
        stack->frames.clear();
        stack->targets.clear();
        stack->values.clear();
        function.reset();
    }
};

struct InstanceData {
    InstanceData(std::shared_ptr<const ProgramData> program, std::shared_ptr<ModuleSession> session)
        : program(std::move(program)), ownedSession(std::move(session)), session(ownedSession.get()) {}
    InstanceData(std::shared_ptr<const ProgramData> program, ModuleSession& session)
        : program(std::move(program)), session(&session) {}

    ~InstanceData() { *lifetime = nullptr; }

    enum class State { Fresh, Initializing, Ready, Failed };
    static constexpr std::size_t MaxCallDepth = 32; // Imports and inheritance, independent of VM calls.
    static constexpr std::size_t MaxFrames = 1024;
    static constexpr std::size_t MaxValues = 1024 * 1024;

    [[noreturn]] void Fail(const Token& token, std::string_view message) const {
        throw Error(program->sourceName, token.location, message);
    }
    void Step(const Token& token) {
        if (++session->steps > 100000)
            Fail(token, "Execution limit exceeded");
    }
    const CompiledFunction* FindFunction(std::string_view name) const {
        const auto found = program->functionIndices.find(std::string(name));
        return found != program->functionIndices.end() ? &program->functions[found->second] : nullptr;
    }
    Variable* FindGlobal(const std::string& name) {
        const auto found = globals.find(name);
        return found != globals.end() ? &found->second : nullptr;
    }
    std::shared_ptr<ScriptInstance> Receiver() const {
        return !session->stack->frames.empty() && session->stack->frames.back().owner == this
                   ? session->stack->frames.back().receiver
                   : nullptr;
    }
    Value ReadName(const Token& name);
    Value ReadSuper() const;
    Value Evaluate(const Bytecode& code);
    Value Run(const CallRequest& request, const Bytecode* expression = nullptr);
    Value ExecuteVm(std::size_t boundary);
    Error AddTrace(const Error& error) const;
    void Debug(const Frame& frame, std::size_t offset);
    void PushFrame(const CallRequest& request, std::size_t returnBase, const Bytecode* expression = nullptr);
    void Return(Value value);
    void ResolveName(const Token& token, bool read);
    void StoreTarget(Value value);
    void AdvanceConstruction();

    Value Invoke(const Token& name, std::span<const Value> arguments, bool exported = false) {
        Step(name);
        if (!exported) {
            const auto invokeValue = [&](const Value& value) -> Value {
                if (!IsCallable(value))
                    Fail(name, "Variable or field '" + name.text + "' is not callable");
                return CallValue(value, arguments);
            };
            if (const auto receiver = Receiver()) {
                if (receiver->HasProperty(name.text) || receiver->HasMethod(name.text))
                    return receiver->Call(name.text, arguments);
            }
            if (const auto* global = FindGlobal(name.text))
                return invokeValue(global->value);
        }
        if (exported) {
            const auto found = globals.find(name.text);
            if (found != globals.end()) {
                const auto* object = std::get_if<ObjectRef>(&found->second.value);
                if (const auto type = object ? std::dynamic_pointer_cast<ScriptClass>(*object) : nullptr)
                    return Instantiate(type, arguments);
            }
        }
        if (exported && !FindFunction(name.text))
            Fail(name, "Unknown module function '" + name.text + "'");
        if (IsConstructor(name.text)) {
            try {
                return Construct(name.text, arguments);
            } catch (const std::exception& error) {
                Fail(name, error.what());
            }
        }
        const auto native = session->natives.find(name.text);
        if (native != session->natives.end()) {
            try {
                const NativeScope callback(*session, arguments);
                return native->second(callback.Arguments());
            } catch (const Error&) {
                throw;
            } catch (const std::exception& error) {
                Fail(name, "Native function '" + name.text + "': " + error.what());
            } catch (...) {
                Fail(name, "Native function '" + name.text + "' failed");
            }
        }
        const CompiledFunction* function = FindFunction(name.text);
        if (!function)
            Fail(name, "Unknown function '" + name.text + "'");
        return InvokeFunction(*function, arguments);
    }
    Value FunctionValue(const CompiledFunction& function) const {
        auto value = std::make_shared<FunctionObject>();
        value->lifetime = lifetime;
        value->function = &function;
        return ObjectRef(value);
    }
    static bool IsCallable(const Value& value) {
        const auto* object = std::get_if<ObjectRef>(&value);
        return object && *object && (std::dynamic_pointer_cast<ScriptClass>(*object) || (*object)->IsCallable());
    }
    // Calls a class (construction) or a callable value. Same-session script code becomes a pending VM frame.
    Value CallValue(const Value& callee, std::span<const Value> arguments) {
        if (!IsCallable(callee))
            throw std::runtime_error("Value is not callable");
        const auto object = std::get<ObjectRef>(callee);
        if (const auto type = std::dynamic_pointer_cast<ScriptClass>(object))
            return Instantiate(type, arguments);
        if (InSession(object.get(), session))
            return object->Invoke(arguments);
        const NativeScope callback(*session, arguments);
        return object->Invoke(callback.Arguments());
    }
    Value InvokeFunction(const CompiledFunction& declaration, std::span<const Value> arguments,
                         const std::shared_ptr<ScriptInstance>& receiver = {},
                         const std::shared_ptr<ScriptClass>& definingClass = {},
                         std::shared_ptr<const FunctionObject> closure = {}) {
        const auto* function = &declaration;
        const auto& name = function->name;
        Step(name);
        if (arguments.size() != function->parameterCount) {
            Fail(name, "Function '" + name.text + "' expects " + std::to_string(function->parameterCount) +
                           " argument(s), received " + std::to_string(arguments.size()));
        }
        const CallRequest request{this, function, receiver, definingClass, {}, arguments, std::move(closure)};
        if (session->dispatching) {
            session->pending = request;
            return {};
        }
        return Run(request);
    }

    void ResolveClass(const std::shared_ptr<ScriptClass>& type, std::size_t depth = 0) {
        if (type->resolved)
            return;
        if (type->resolving)
            Fail(type->declaration->name, "Circular class inheritance");
        if (depth >= MaxCallDepth)
            Fail(type->declaration->name, "Class inheritance depth limit exceeded");
        type->resolving = true;
        struct Exit {
            ScriptClass& type;
            ~Exit() { type.resolving = false; }
        } exit{*type};
        if (type->declaration->base) {
            const auto value = Evaluate(*type->declaration->base);
            const auto* object = std::get_if<ObjectRef>(&value);
            auto base = object ? std::dynamic_pointer_cast<ScriptClass>(*object) : nullptr;
            if (!base)
                Fail(type->declaration->name, "Base must be a script class");
            if (base->Owner().session != session)
                Fail(type->declaration->name, "Base class belongs to another script instance");
            base->Owner().ResolveClass(base, depth + 1);
            std::size_t levels = depth + 1;
            for (auto parent = base; parent; parent = parent->base) {
                if (++levels > MaxCallDepth)
                    Fail(type->declaration->name, "Class inheritance depth limit exceeded");
                for (const auto& field : type->declaration->fields) {
                    if (parent->FindMethod(field.token.text) || parent->signatures.contains(field.token.text))
                        Fail(field.token, "Field conflicts with inherited method");
                    for (const auto& inherited : parent->declaration->fields) {
                        if (inherited.token.text == field.token.text)
                            Fail(field.token, "Duplicate inherited field");
                    }
                }
                for (const auto& method : type->declaration->methods) {
                    if (const auto signature = parent->signatures.find(method.name.text);
                        signature != parent->signatures.end() && signature->second != method.parameterCount)
                        Fail(method.name, "Method '" + method.name.text + "' expects " +
                                              std::to_string(signature->second) + " parameter(s)");
                    for (const auto& inherited : parent->declaration->fields) {
                        if (method.name.text == inherited.token.text)
                            Fail(method.name, "Method conflicts with inherited field");
                    }
                }
            }
            type->base = std::move(base);
        }
        for (const auto& method : type->declaration->methods) {
            if (method.name.text == "init")
                continue;
            bool inherited = false;
            std::optional<std::size_t> parameterCount;
            for (auto parent = type->base; parent; parent = parent->base) {
                if (const auto* baseMethod = parent->FindMethod(method.name.text)) {
                    inherited = true;
                    parameterCount = baseMethod->parameterCount;
                    break;
                }
                if (parent->host) {
                    if (const auto signature = parent->signatures.find(method.name.text);
                        signature != parent->signatures.end()) {
                        inherited = true;
                        parameterCount = signature->second;
                    } else {
                        try {
                            const HostScope callback(*session);
                            inherited = parent->host->HasMethod(method.name.text);
                        } catch (const std::exception& error) {
                            Fail(method.name, error.what());
                        }
                    }
                    if (inherited)
                        break;
                }
            }
            if (inherited && !method.isOverride)
                Fail(method.name, "Method '" + method.name.text + "' requires override");
            if (!inherited && method.isOverride)
                Fail(method.name, "No inherited method '" + method.name.text + "' to override");
            if (parameterCount && *parameterCount != method.parameterCount)
                Fail(method.name,
                     "Override parameter count does not match inherited method '" + method.name.text + "'");
        }
        if (type->base)
            type->fieldInitializers = type->base->fieldInitializers;
        for (const auto& field : type->declaration->fields)
            type->fieldInitializers.emplace_back(&type->Owner(), &field);
        type->resolved = true;
    }
    Value Instantiate(const std::shared_ptr<ScriptClass>& type, std::span<const Value> arguments) {
        auto& owner = type->Owner();
        if (owner.session != session) {
            const HostScope callback(*session);
            const RunScope run(*owner.session);
            return owner.Instantiate(type, arguments);
        }
        owner.ResolveClass(type);
        auto constructor = type;
        while (constructor && !constructor->FindMethod("init"))
            constructor = constructor->base;
        const auto* initializer = constructor ? constructor->FindMethod("init") : nullptr;
        const auto parameterCount = initializer ? initializer->parameterCount : 0;
        if (arguments.size() != parameterCount)
            Fail(type->declaration->name, "Class '" + type->declaration->name.text + "' expects " +
                                              std::to_string(parameterCount) + " argument(s)");
        const CallRequest request{&owner, nullptr, {}, {}, type, arguments};
        if (session->dispatching) {
            session->pending = request;
            return {};
        }
        return owner.Run(request);
    }

    ObjectRef ImportLibrary(const CompiledImport& import) {
        Step(import.name);
        if (const auto found = session->libraries.find(import.request); found != session->libraries.end())
            return found->second;
        if (!session->loader)
            Fail(import.name, "No loader for module '" + import.request + "'");
        try {
            auto source = session->loader(import.request, program->sourceName);
            if (!source)
                Fail(import.name, "Module '" + import.request + "' was not found");
            if (source->sourceName.empty())
                Fail(import.name, "Module loader returned an empty canonical source name");
            if (std::ranges::find(session->loading, source->sourceName) != session->loading.end())
                Fail(import.name, "Circular import of '" + source->sourceName + "'");
            if (const auto found = session->modules.find(source->sourceName); found != session->modules.end())
                return found->second;
            auto data = std::make_shared<InstanceData>(ParseProgram(source->source, source->sourceName), *session);
            auto object = std::make_shared<ModuleObject>(data, session->weak_from_this());
            data->Initialize();
            session->moduleInstances.emplace(source->sourceName, data);
            session->modules.emplace(source->sourceName, object);
            return object;
        } catch (const Error&) {
            throw;
        } catch (const std::exception& error) {
            Fail(import.name, error.what());
        } catch (...) {
            Fail(import.name, "Module loader failed");
        }
    }
    void Initialize() {
        if (state != State::Fresh)
            throw std::logic_error("An instance can only be initialized once");
        if (session->loading.size() >= MaxCallDepth)
            throw Error(program->sourceName, {}, "Module dependency depth limit exceeded");
        session->loading.push_back(program->sourceName);
        struct Exit {
            ModuleSession& session;
            ~Exit() { session.loading.pop_back(); }
        } exit{*session};
        state = State::Initializing;
        try {
            for (const auto& [name, type] : session->classes) {
                for (const auto& global : program->globals) {
                    if (global.token.text == name)
                        Fail(global.token, "Global declaration conflicts with host class '" + name + "'");
                }
                if (const auto* function = FindFunction(name))
                    Fail(function->name, "Function declaration conflicts with host class '" + name + "'");
                globals.try_emplace(name, Variable{type, true});
            }
            for (const auto& global : program->globals) {
                if (session->natives.contains(global.token.text))
                    Fail(global.token, "Global declaration conflicts with native function '" + global.token.text + "'");
            }
            for (const auto& function : program->functions) {
                if (session->natives.contains(function.name.text))
                    Fail(function.name,
                         "Function declaration conflicts with native function '" + function.name.text + "'");
            }
            for (const auto& import : program->imports) {
                if (globals.contains(import.name.text) || session->natives.contains(import.name.text))
                    Fail(import.name, "Import alias conflicts with a host binding");
                globals.emplace(import.name.text, Variable{ImportLibrary(import), true});
            }
            for (const auto& declaration : program->enums) {
                if (globals.contains(declaration.name.text) || session->natives.contains(declaration.name.text))
                    Fail(declaration.name, "Enum declaration conflicts with a host binding");
                auto type = std::make_shared<EnumType>();
                for (const auto& member : declaration.members) {
                    type->indices.emplace(member, type->members.size());
                    type->members.push_back(std::make_shared<EnumValue>(declaration.name.text, member,
                                                                        static_cast<double>(type->members.size())));
                }
                globals.emplace(declaration.name.text, Variable{ObjectRef(type), true});
            }
            for (const auto& declaration : program->classes) {
                if (globals.contains(declaration.name.text) || session->natives.contains(declaration.name.text))
                    Fail(declaration.name, "Class declaration conflicts with a host binding");
                auto type = std::make_shared<ScriptClass>();
                type->declaration = &declaration;
                for (const auto& method : declaration.methods)
                    type->methods.emplace(method.name.text, &method);
                for (const auto& field : declaration.fields)
                    type->fieldNames.emplace(field.token.text);
                type->lifetime = lifetime;
                globals.emplace(declaration.name.text, Variable{ObjectRef(type), true});
            }
            for (const auto& declaration : program->classes) {
                const auto type = std::dynamic_pointer_cast<ScriptClass>(
                    std::get<ObjectRef>(globals.at(declaration.name.text).value));
                ResolveClass(type);
            }
            for (const auto& global : program->globals) {
                Step(global.token);
                auto value = global.expression ? Evaluate(*global.expression) : Value{};
                if (!globals.emplace(global.token.text, Variable{std::move(value), global.constant}).second)
                    Fail(global.token, "Duplicate declaration '" + global.token.text + "'");
            }
            state = State::Ready;
        } catch (...) {
            state = State::Failed;
            throw;
        }
    }
    void CheckImportBinding(std::string_view name) const {
        for (const auto& declaration : program->classes) {
            if (declaration.name.text == name)
                Fail(declaration.name, "Class declaration conflicts with a host binding");
        }
        for (const auto& import : program->imports) {
            if (import.name.text == name)
                Fail(import.name, "Import alias conflicts with a host binding");
        }
    }

    std::shared_ptr<const ProgramData> program;
    std::shared_ptr<ModuleSession> ownedSession;
    ModuleSession* session;
    std::shared_ptr<InstanceData*> lifetime = std::make_shared<InstanceData*>(this);
    Scope globals;
    State state = State::Fresh;
};

Value SuperObject::Call(std::string_view name, std::span<const Value> arguments) {
    auto& session = *instance->type->Owner().session;
    return EnterObject(session, [&] { return instance->Dispatch(base, name, arguments); });
}

Value InstanceData::ReadSuper() const {
    const auto& frame = session->stack->frames.back();
    if (frame.definingClass && frame.definingClass->base) {
        auto parent = std::make_shared<SuperObject>();
        parent->instance = frame.receiver;
        parent->base = frame.definingClass->base;
        return ObjectRef(parent);
    }
    return {};
}

Value InstanceData::ReadName(const Token& name) {
    if (const auto receiver = Receiver()) {
        if (name.text == "self")
            return ObjectRef(receiver);
        if (name.text == "super") {
            auto parent = ReadSuper();
            if (std::holds_alternative<ObjectRef>(parent))
                return parent;
        }
        if (receiver->HasProperty(name.text))
            return receiver->Get(name.text);
        if (receiver->HasMethod(name.text))
            return ObjectRef(std::make_shared<BoundMethod>(receiver, name.text));
    }
    if (const auto* global = FindGlobal(name.text))
        return global->value;
    if (const auto* function = FindFunction(name.text))
        return FunctionValue(*function);
    if (session->natives.contains(name.text))
        return ObjectRef(std::make_shared<NativeCallable>(session->weak_from_this(), name.text));
    Fail(name, "Unknown variable '" + name.text + "'");
}

void InstanceData::ResolveName(const Token& token, bool read) {
    WriteTarget target;
    target.token = token;
    if (const auto receiver = Receiver()) {
        if (token.text == "self" || (token.text == "super" && std::holds_alternative<ObjectRef>(ReadSuper()))) {
            target.kind = WriteTarget::Kind::Constant;
            target.value = ReadName(token);
            session->stack->targets.push_back(std::move(target));
            return;
        }
        if (receiver->HasProperty(token.text)) {
            target.kind = WriteTarget::Kind::Property;
            target.object = receiver;
            if (read)
                target.value = receiver->Get(token.text);
            session->stack->targets.push_back(std::move(target));
            return;
        }
        if (receiver->HasMethod(token.text))
            Fail(token, "Cannot assign to a method");
    }
    const auto* global = FindGlobal(token.text);
    if (!global)
        Fail(token, "Unknown variable '" + token.text + "'");
    target.kind = WriteTarget::Kind::Global;
    target.value = global->value;
    session->stack->targets.push_back(std::move(target));
}

void InstanceData::StoreTarget(Value value) {
    while (true) {
        auto target = std::move(session->stack->targets.back());
        session->stack->targets.pop_back();
        try {
            switch (target.kind) {
            case WriteTarget::Kind::Local:
                if (target.constant)
                    Fail(target.token, "Cannot assign to const '" + target.token.text + "'");
                session->stack->values[target.slot] = std::move(value);
                return;
            case WriteTarget::Kind::Global: {
                auto* global = FindGlobal(target.token.text);
                if (!global || global->constant)
                    Fail(target.token, "Cannot assign to const '" + target.token.text + "'");
                global->value = std::move(value);
                return;
            }
            case WriteTarget::Kind::Property:
                target.object->Set(target.token.text, value);
                return;
            case WriteTarget::Kind::Upvalue:
                if (target.constant)
                    Fail(target.token, "Cannot assign to a captured const");
                target.upvalue->Get() = std::move(value);
                return;
            case WriteTarget::Kind::Constant:
                Fail(target.token, "Cannot assign to const '" + target.token.text + "'");
            case WriteTarget::Kind::Temporary:
                Fail(target.token, "Cannot modify a temporary value");
            case WriteTarget::Kind::Index:
                SetIndex(session->stack->targets.back().value, target.index, value);
                while (session->stack->targets.back().kind == WriteTarget::Kind::Member ||
                       session->stack->targets.back().kind == WriteTarget::Kind::Index)
                    session->stack->targets.pop_back();
                session->stack->targets.pop_back();
                return;
            case WriteTarget::Kind::Member: {
                const auto& parent = session->stack->targets.back();
                const bool reference = std::holds_alternative<ObjectRef>(parent.value);
                if (!reference && parent.kind == WriteTarget::Kind::Temporary)
                    Fail(target.token, "Cannot modify a temporary value");
                value = SetMember(parent.value, target.token.text, value);
                if (reference) {
                    // A reference setter completes the write. Copied structs propagate toward the root instead.
                    while (session->stack->targets.back().kind == WriteTarget::Kind::Member ||
                           session->stack->targets.back().kind == WriteTarget::Kind::Index)
                        session->stack->targets.pop_back();
                    session->stack->targets.pop_back();
                    return;
                }
                break;
            }
            }
        } catch (const Error&) {
            throw;
        } catch (const std::exception& error) {
            Fail(target.token, error.what());
        } catch (...) {
            Fail(target.token, "Native property setter failed");
        }
    }
}

void InstanceData::PushFrame(const CallRequest& request, std::size_t returnBase, const Bytecode* expression) {
    const auto token = request.function       ? request.function->name
                       : request.construction ? request.construction->declaration->name
                                              : Token{};
    if (session->stack->frames.size() >= MaxFrames)
        request.owner->Fail(token, "Call depth limit exceeded");
    Frame frame;
    frame.owner = request.owner;
    frame.code = expression ? expression : request.function ? &request.function->code : nullptr;
    frame.name = request.function       ? &request.function->name
                 : request.construction ? &request.construction->declaration->name
                                        : nullptr;
    frame.localBase = session->stack->values.size();
    frame.returnBase = returnBase;
    frame.targetBase = session->stack->targets.size();
    frame.receiver = request.receiver;
    frame.definingClass = request.definingClass;
    frame.construction = request.construction;
    frame.closure = request.closure;
    frame.argumentCount = request.arguments.size();
    const auto localCount = frame.code ? frame.code->locals.size() : frame.argumentCount;
    const auto required = frame.localBase + localCount + (frame.code ? frame.code->maxStack : 1);
    if (required > MaxValues)
        request.owner->Fail(token, "Value stack limit exceeded");
    // Arguments may point into the value stack. Reuse scratch storage before reserve can move that stack.
    session->arguments.assign(request.arguments.begin(), request.arguments.end());
    if (session->stack->values.capacity() < required)
        session->stack->values.reserve(
            std::min(MaxValues, std::max(required, session->stack->values.capacity() * 2 + 32)));
    session->stack->values.insert(session->stack->values.end(), session->arguments.begin(), session->arguments.end());
    session->arguments.clear();
    session->stack->values.resize(frame.localBase + localCount);
    if (session->profiling && frame.code) {
        auto& entry = session->profile[frame.code];
        if (entry.data.name.empty()) {
            entry.data.name = frame.name ? frame.name->text : "<initializer>";
            entry.data.sourceName = request.owner->program->sourceName;
            entry.data.line = frame.name                       ? frame.name->location.line
                              : !frame.code->locations.empty() ? frame.code->locations.front().line
                                                               : 0;
        }
        ++entry.data.calls;
        frame.profile = &entry;
    }
    if (frame.construction) {
        session->CountAllocation();
        frame.receiver = MakeManaged<ScriptInstance>();
        frame.receiver->type = frame.construction;
        if (session->instances.size() % 256 == 0)
            std::erase_if(session->instances, [](const auto& weak) { return weak.expired(); });
        session->instances.push_back(frame.receiver);
        ++session->allocations;
    }
    session->stack->frames.push_back(std::move(frame));
}

void InstanceData::Return(Value value) {
    const auto returnBase = session->stack->frames.back().returnBase;
    if (!session->stack->openUpvalues.empty())
        session->CloseUpvalues(session->stack->frames.back().localBase);
    session->stack->targets.resize(session->stack->frames.back().targetBase);
    session->stack->frames.pop_back();
    session->stack->values.resize(returnBase);
    session->stack->values.push_back(std::move(value));
}

void InstanceData::AdvanceConstruction() {
    auto& frame = session->stack->frames.back();
    const auto object = frame.receiver;
    const auto type = frame.construction;
    if (frame.waitingForField) {
        const auto* field = type->fieldInitializers[frame.fieldIndex - 1].second;
        object->fields.emplace(field->token.text, Variable{std::move(session->stack->values.back()), field->constant});
        session->stack->values.pop_back();
        frame.waitingForField = false;
    }
    while (frame.fieldIndex < type->fieldInitializers.size()) {
        const auto [owner, field] = type->fieldInitializers[frame.fieldIndex++];
        owner->Step(field->token);
        if (!field->expression) {
            object->fields.emplace(field->token.text, Variable{{}, field->constant});
            continue;
        }
        frame.waitingForField = true;
        PushFrame({owner, nullptr, object, {}, {}, {}}, session->stack->values.size(), field->expression.get());
        return;
    }
    if (!frame.constructorCalled) {
        frame.constructorCalled = true;
        for (auto current = type; current; current = current->base) {
            if (const auto* initializer = current->FindMethod("init")) {
                const auto arguments =
                    std::span<const Value>(session->stack->values).subspan(frame.localBase, frame.argumentCount);
                current->Owner().Step(initializer->name);
                PushFrame({&current->Owner(), initializer, object, current, {}, arguments},
                          session->stack->values.size());
                return;
            }
        }
    }
    Return(ObjectRef(object));
}

Value InstanceData::Evaluate(const Bytecode& code) {
    return Run({this, nullptr, {}, {}, {}, {}}, &code);
}

Value InstanceData::Run(const CallRequest& request, const Bytecode* expression) {
    const auto boundary = session->stack->frames.size();
    const auto valueBase = session->stack->values.size();
    const auto targetBase = session->stack->targets.size();
    try {
        PushFrame(request, valueBase, expression);
        auto result = ExecuteVm(boundary);
        session->stack->values.resize(valueBase);
        return result;
    } catch (const Error& error) {
        // The innermost failing run sees every frame; outer runs keep the trace it built.
        std::optional<Error> traced;
        if (error.GetTrace().empty())
            traced = AddTrace(error);
        session->pending.reset();
        session->arguments.clear();
        session->CloseUpvalues(valueBase);
        session->stack->frames.resize(boundary);
        session->stack->values.resize(valueBase);
        session->stack->targets.resize(targetBase);
        if (traced)
            throw *traced;
        throw;
    } catch (...) {
        session->pending.reset();
        session->arguments.clear();
        session->CloseUpvalues(valueBase);
        session->stack->frames.resize(boundary);
        session->stack->values.resize(valueBase);
        session->stack->targets.resize(targetBase);
        throw;
    }
}

Error InstanceData::AddTrace(const Error& error) const {
    // The failing frame is the error's own location; list the calls below it.
    const auto& frames = session->stack->frames;
    if (frames.size() < 2)
        return error;
    std::string trace;
    std::size_t listed = 0;
    for (std::size_t i = frames.size(); i-- > 0;) {
        const auto& frame = frames[i];
        if (!frame.code)
            continue;
        if (++listed > 32) {
            trace += "  ...";
            break;
        }
        const auto location = frame.ip > 0 && frame.ip <= frame.code->locations.size()
                                  ? frame.code->locations[frame.ip - 1]
                                  : SourceLocation{};
        if (!trace.empty())
            trace += '\n';
        trace += "  at " + (frame.name ? frame.name->text : std::string("<initializer>")) + " (" +
                 frame.owner->program->sourceName + ":" + std::to_string(location.line) + ":" +
                 std::to_string(location.column) + ")";
    }
    return error.WithTrace(std::move(trace));
}

void InstanceData::Debug(const Frame& frame, std::size_t offset) {
    const auto& frames = session->stack->frames;
    const auto depth = frames.size();
    const auto line = frame.code->locations[offset].line;
    DebugReason reason = DebugReason::Step;
    bool stop = false;
    switch (session->step) {
    case DebugAction::StepIn:
        stop = true;
        break;
    case DebugAction::StepOver:
        stop = session->stack != session->stepStack || depth <= session->stepDepth;
        break;
    case DebugAction::StepOut:
        stop = session->stack != session->stepStack || depth < session->stepDepth;
        break;
    case DebugAction::Continue:
        break;
    }
    if (!stop) {
        const auto found = session->breakpoints.find(program->sourceName);
        stop = found != session->breakpoints.end() && found->second.contains(line);
        reason = DebugReason::Breakpoint;
    }
    if (!stop)
        return;
    DebugEvent event;
    event.reason = reason;
    for (std::size_t i = depth; i-- > 0;) {
        const auto& current = frames[i];
        if (!current.code)
            continue;
        DebugFrame entry;
        entry.function = current.name ? current.name->text : "<initializer>";
        entry.sourceName = current.owner->program->sourceName;
        const auto ip = &current == &frame ? offset + 1 : current.ip;
        entry.location =
            ip > 0 && ip <= current.code->locations.size() ? current.code->locations[ip - 1] : SourceLocation{};
        for (std::size_t slot = 0; slot < current.code->locals.size(); ++slot) {
            const auto& local = current.code->locals[slot];
            const auto index = current.localBase + slot;
            if (local.name.starts_with('$') || index >= session->stack->values.size())
                continue;
            // Parameters always show; other locals once they hold something, as cleared slots look like null.
            const auto& value = session->stack->values[index];
            if (slot < current.argumentCount || !std::holds_alternative<std::monostate>(value))
                entry.locals.emplace_back(local.name, value);
        }
        if (current.receiver)
            entry.locals.emplace_back("self", ObjectRef(current.receiver));
        event.frames.push_back(std::move(entry));
    }
    const auto action = session->debugHandler(event);
    session->step = action;
    session->stepDepth = depth;
    session->stepStack = session->stack;
}

Value InstanceData::ExecuteVm(std::size_t boundary) {
    struct Depth {
        ModuleSession& session;
        explicit Depth(ModuleSession& session) : session(session) {
            // Time between host calls belongs to no function.
            if (session.vmDepth++ == 0 && session.profiling) {
                session.profiled = nullptr;
                session.profiledAt = std::chrono::steady_clock::now();
            }
        }
        ~Depth() {
            if (--session.vmDepth == 0 && session.profiling) {
                session.ChargeProfile();
                session.profiled = nullptr;
            }
        }
    } depth{*session};
    auto& values = session->stack->values;
    const auto pop = [&]() {
        auto value = std::move(values.back());
        values.pop_back();
        return value;
    };
    while (session->stack->frames.size() > boundary) {
        auto& frame = session->stack->frames.back();
        auto& owner = *frame.owner;
        if (frame.construction) {
            AdvanceConstruction();
            continue;
        }
        const auto& code = *frame.code;
        const auto offset = frame.ip++;
        if (session->debugHandler && session->IsDebugging()) {
            const auto line = code.locations[offset].line;
            if (line != frame.debugLine) {
                frame.debugLine = line;
                owner.Debug(frame, offset);
            }
        }
        if (session->profiling && frame.profile) {
            session->ChargeProfile();
            session->profiled = frame.profile;
            session->profiledAt = std::chrono::steady_clock::now();
            ++frame.profile->data.instructions;
        }
        const auto instruction = code.instructions[offset];
        Token token{TokenKind::Identifier, {}, code.locations[offset]};
        try {
            owner.Step(token);
            switch (instruction.opcode) {
            case Opcode::MakeArray:
            case Opcode::MakeMap: {
                const auto base = values.size() - instruction.operand;
                const auto elements = std::span<const Value>(values).subspan(base);
                Value collection = instruction.opcode == Opcode::MakeArray ? MakeArray(elements) : MakeMap(elements);
                session->TrackCollection(collection);
                values.resize(base);
                values.push_back(std::move(collection));
                break;
            }
            case Opcode::GetIndex: {
                const auto index = pop();
                const HostScope callback(*session);
                values.back() = GetIndex(values.back(), index);
                break;
            }
            case Opcode::IndexTarget: {
                WriteTarget target;
                target.kind = WriteTarget::Kind::Index;
                target.token = token;
                target.index = pop();
                if (instruction.auxiliary) {
                    const HostScope callback(*session);
                    target.value = GetIndex(session->stack->targets.back().value, target.index);
                }
                session->stack->targets.push_back(std::move(target));
                break;
            }
            case Opcode::IteratorBegin:
                values.back() = MakeIterator(values.back());
                break;
            case Opcode::IteratorNext: {
                Value first, second;
                const bool next = NextIterator(values[frame.localBase + instruction.operand],
                                               instruction.auxiliary == 2, first, second);
                values.push_back(std::move(first));
                if (instruction.auxiliary == 2)
                    values.push_back(std::move(second));
                values.push_back(next);
                break;
            }
            case Opcode::Constant:
                values.push_back(code.constants[instruction.operand]);
                break;
            case Opcode::LoadLocal:
                values.push_back(values[frame.localBase + instruction.operand]);
                break;
            case Opcode::LoadName: {
                token.text = code.names[instruction.operand];
                const HostScope callback(*session);
                values.push_back(owner.ReadName(token));
                break;
            }
            case Opcode::DeclareLocal:
                // A declaration starts a new binding: closures over the previous one keep its last value.
                if (!session->stack->openUpvalues.empty())
                    session->CloseUpvalues(frame.localBase + instruction.operand, true);
                values[frame.localBase + instruction.operand] = pop();
                break;
            case Opcode::ClearLocal:
                if (!session->stack->openUpvalues.empty())
                    session->CloseUpvalues(frame.localBase + instruction.operand, true);
                values[frame.localBase + instruction.operand] = {};
                break;
            case Opcode::MakeClosure: {
                auto closure = std::make_shared<FunctionObject>();
                closure->lifetime = owner.lifetime;
                closure->function = code.functions[instruction.operand].get();
                closure->closure = true;
                closure->receiver = frame.receiver;
                closure->definingClass = frame.definingClass;
                for (const auto& upvalue : closure->function->upvalues)
                    closure->upvalues.push_back(upvalue.local ? session->Capture(frame.localBase + upvalue.index)
                                                              : frame.closure->upvalues[upvalue.index]);
                session->TrackClosure(closure);
                values.push_back(ObjectRef(std::move(closure)));
                break;
            }
            case Opcode::LoadUpvalue: {
                Value value = frame.closure->upvalues[instruction.operand]->Get();
                values.push_back(std::move(value));
                break;
            }
            case Opcode::UpvalueTarget: {
                WriteTarget target;
                target.kind = WriteTarget::Kind::Upvalue;
                target.upvalue = frame.closure->upvalues[instruction.operand];
                target.value = target.upvalue->Get();
                target.constant = frame.closure->function->upvalues[instruction.operand].constant;
                target.token = token;
                session->stack->targets.push_back(std::move(target));
                break;
            }
            case Opcode::DuplicateDeclaration:
                owner.Fail(token, "Duplicate declaration '" + code.names[instruction.operand] + "'");
            case Opcode::Pop:
                values.pop_back();
                break;
            case Opcode::Unary:
                if (instruction.operand == 0)
                    values.back() = !Boolean(values.back());
                else if (instruction.operand == 1)
                    values.back() = Negate(values.back());
                else if (!IsStruct(values.back()))
                    values.back() = Binary(Operation::Add, 0.0, values.back());
                break;
            case Opcode::Yield:
                if (!session->coroutine)
                    owner.Fail(token, "yield is only allowed while a coroutine runs");
                // Native frames between the coroutine and the yield live on the C++ stack and cannot suspend.
                if (session->vmDepth != session->coroutine->baseDepth)
                    owner.Fail(token, "Cannot yield across a native call");
                session->yieldValue = pop();
                session->yielded = true;
                return {};
            case Opcode::Concatenate: {
                const auto base = values.size() - instruction.operand;
                std::string text;
                for (auto i = base; i < values.size(); ++i)
                    text += ToString(values[i]);
                values.resize(base);
                values.push_back(std::move(text));
                break;
            }
            case Opcode::IsType: {
                const auto& value = values.back();
                values.back() = instruction.operand == 1   ? std::holds_alternative<Vector2>(value)
                                : instruction.operand == 2 ? std::holds_alternative<Vector3>(value)
                                                           : std::holds_alternative<Color>(value);
                break;
            }
            case Opcode::IsInstance: {
                const auto right = pop();
                const auto* reference = std::get_if<ObjectRef>(&right);
                const auto type = reference ? std::dynamic_pointer_cast<ScriptClass>(*reference) : nullptr;
                if (!type)
                    owner.Fail(token, "The right side of 'is' must be a class");
                const auto* left = std::get_if<ObjectRef>(&values.back());
                const auto* instance = left ? dynamic_cast<const ScriptInstance*>(left->get()) : nullptr;
                bool matches = false;
                for (auto current = instance ? instance->type : nullptr; current && !matches; current = current->base)
                    matches = current == type;
                values.back() = matches;
                break;
            }
            case Opcode::Binary: {
                auto right = pop();
                values.back() = Binary(static_cast<Operation>(instruction.operand), values.back(), right);
                break;
            }
            case Opcode::Boolean:
                (void)Boolean(values.back());
                break;
            case Opcode::Jump:
                frame.ip = instruction.operand;
                break;
            case Opcode::JumpFalse:
            case Opcode::JumpTrue:
                if (Boolean(values.back()) == (instruction.opcode == Opcode::JumpTrue))
                    frame.ip = instruction.operand;
                break;
            case Opcode::GetMember: {
                const HostScope callback(*session);
                values.back() = GetMember(values.back(), code.names[instruction.operand]);
                break;
            }
            case Opcode::Call:
            case Opcode::CallLocal:
            case Opcode::CallMember:
            case Opcode::CallValue: {
                const auto argumentBase = values.size() - instruction.auxiliary;
                const bool hasCallee =
                    instruction.opcode == Opcode::CallMember || instruction.opcode == Opcode::CallValue;
                const auto returnBase = argumentBase - (hasCallee ? 1 : 0);
                const auto arguments = std::span<const Value>(values).subspan(argumentBase);
                struct DispatchScope {
                    explicit DispatchScope(ModuleSession& session) : session(session) { session.dispatching = true; }
                    ~DispatchScope() { session.dispatching = false; }
                    ModuleSession& session;
                } dispatch{*session};
                Value result;
                if (instruction.opcode == Opcode::CallValue) {
                    const auto callee = values[returnBase];
                    result = owner.CallValue(callee, arguments);
                } else if (instruction.opcode == Opcode::CallLocal) {
                    token.text = code.locals[instruction.operand].name;
                    const auto callee = values[frame.localBase + instruction.operand];
                    if (!IsCallable(callee))
                        owner.Fail(token, "Variable or field '" + token.text + "' is not callable");
                    result = owner.CallValue(callee, arguments);
                } else {
                    token.text = code.names[instruction.operand];
                    if (instruction.opcode == Opcode::CallMember) {
                        const auto* reference = std::get_if<ObjectRef>(&values[returnBase]);
                        if (InSession(reference ? reference->get() : nullptr, session)) {
                            result = CallMember(values[returnBase], token.text, arguments);
                        } else {
                            const NativeScope callback(*session, arguments);
                            result = CallMember(values[returnBase], token.text, callback.Arguments());
                        }
                    } else
                        result = owner.Invoke(token, arguments);
                }
                if (session->pending) {
                    const auto request = *session->pending;
                    session->pending.reset();
                    PushFrame(request, returnBase);
                } else {
                    session->TrackCollection(result);
                    values.resize(returnBase);
                    values.push_back(std::move(result));
                }
                break;
            }
            case Opcode::NameTarget: {
                token.text = code.names[instruction.operand];
                const HostScope callback(*session);
                owner.ResolveName(token, instruction.auxiliary != 0);
                break;
            }
            case Opcode::LocalTarget: {
                token.text = code.locals[instruction.operand].name;
                WriteTarget target;
                target.kind = WriteTarget::Kind::Local;
                target.slot = frame.localBase + instruction.operand;
                target.value = values[target.slot];
                target.constant = code.locals[instruction.operand].constant;
                target.token = token;
                session->stack->targets.push_back(std::move(target));
                break;
            }
            case Opcode::TemporaryTarget: {
                WriteTarget target;
                target.value = pop();
                target.token = token;
                session->stack->targets.push_back(std::move(target));
                break;
            }
            case Opcode::MemberTarget: {
                token.text = code.names[instruction.operand];
                WriteTarget target;
                target.kind = WriteTarget::Kind::Member;
                target.token = token;
                if (instruction.auxiliary) {
                    const HostScope callback(*session);
                    target.value = GetMember(session->stack->targets.back().value, token.text);
                }
                session->stack->targets.push_back(std::move(target));
                break;
            }
            case Opcode::LoadTarget:
                values.push_back(session->stack->targets.back().value);
                break;
            case Opcode::StoreTarget: {
                const HostScope callback(*session);
                owner.StoreTarget(pop());
                break;
            }
            case Opcode::Return:
                Return(pop());
                break;
            }
        } catch (const Error&) {
            throw;
        } catch (const std::exception& error) {
            owner.Fail(token, error.what());
        } catch (...) {
            owner.Fail(token, "Native operation failed");
        }
    }
    return pop();
}

ModuleSession::~ModuleSession() {
    for (const auto& weak : coroutines) {
        if (const auto suspended = weak.lock())
            suspended->Finish();
    }
    // Break script field cycles when the root session ends, including objects retained by the host.
    for (const auto& weak : instances) {
        if (const auto object = weak.lock())
            object->fields.clear();
    }
    for (const auto& weak : collections) {
        if (const auto collection = weak.lock()) {
            collection->alive = false;
            collection->ClearReferences();
        }
    }
    for (const auto& weak : closures) {
        if (const auto closure = weak.lock()) {
            for (const auto& upvalue : closure->upvalues)
                upvalue->closed = {};
            closure->upvalues.clear();
            closure->receiver.reset();
        }
    }
}

bool InSession(const Object* object, const ModuleSession* session) {
    if (const auto* method = dynamic_cast<const BoundMethod*>(object))
        return InSession(method->GetReceiver().get(), session);
    if (const auto* function = dynamic_cast<const FunctionObject*>(object))
        return function->Owner().session == session;
    const auto* script = dynamic_cast<const ScriptInstance*>(object);
    const auto* module = dynamic_cast<const ModuleObject*>(object);
    const auto* parent = dynamic_cast<const SuperObject*>(object);
    return (script && script->type->Owner().session == session) ||
           (module && module->session.lock().get() == session) ||
           (parent && parent->instance->type->Owner().session == session);
}

ModuleSession::ModuleSession() {
    const auto core = std::dynamic_pointer_cast<Library>(libraries.at("core"));
    core->Function("coroutine", [this](std::span<const Value> arguments) -> Value {
        const auto* function = arguments.size() == 1 ? std::get_if<ObjectRef>(&arguments[0]) : nullptr;
        if (!function)
            throw std::runtime_error("Expected a function to run as a coroutine");
        return MakeCoroutine(*function);
    });
}

ObjectRef ModuleSession::MakeCoroutine(const ObjectRef& function) {
    if (!function || !function->IsCallable())
        throw std::runtime_error("Expected a function to run as a coroutine");
    auto created = std::make_shared<CoroutineObject>();
    created->session = weak_from_this();
    created->function = function;
    if (coroutines.size() % 256 == 0)
        std::erase_if(coroutines, [](const auto& weak) { return weak.expired(); });
    coroutines.push_back(created);
    return created;
}

Value CoroutineObject::Call(std::string_view name, std::span<const Value> arguments) {
    if (name != "resume")
        return Object::Call(name, arguments);
    if (!arguments.empty())
        throw std::runtime_error("resume() takes no arguments");
    const auto owner = session.lock();
    if (!owner)
        throw std::runtime_error("Coroutine instance is no longer available");
    if (status == Status::Dead)
        throw std::runtime_error("Cannot resume a finished coroutine");
    if (status == Status::Running)
        throw std::runtime_error("Cannot resume a running coroutine");
    return EnterObject(*owner, [&] { return Resume(*owner); });
}

Value CoroutineObject::Resume(ModuleSession& owner) {
    struct Switch {
        ModuleSession& session;
        VmStack* stack;
        CoroutineObject* coroutine;
        bool dispatching;
        ~Switch() {
            session.stack = stack;
            session.coroutine = coroutine;
            session.dispatching = dispatching;
        }
    } restore{owner, owner.stack, owner.coroutine, owner.dispatching};
    owner.stack = stack.get();
    owner.coroutine = this;
    owner.dispatching = false;
    status = Status::Running;
    baseDepth = owner.vmDepth + 1;
    try {
        if (!started) {
            started = true;
            // Script functions schedule their first frame instead of running; anything else just returns.
            owner.dispatching = true;
            Value immediate = function->Invoke({});
            owner.dispatching = false;
            if (!owner.pending) {
                status = Status::Dead;
                Finish();
                return immediate;
            }
            const auto request = *owner.pending;
            owner.pending.reset();
            request.owner->PushFrame(request, 0);
        }
        auto* runner = stack->frames.front().owner;
        Value result = runner->ExecuteVm(0);
        if (owner.yielded) {
            owner.yielded = false;
            status = Status::Suspended;
            return std::exchange(owner.yieldValue, {});
        }
        status = Status::Dead;
        Finish();
        return result;
    } catch (const Error& error) {
        owner.pending.reset();
        owner.yielded = false;
        status = Status::Dead;
        std::optional<Error> traced;
        if (error.GetTrace().empty() && !stack->frames.empty())
            traced = stack->frames.front().owner->AddTrace(error);
        Finish();
        if (traced)
            throw *traced;
        throw;
    } catch (...) {
        owner.pending.reset();
        owner.yielded = false;
        status = Status::Dead;
        Finish();
        throw;
    }
}

InstanceData& FunctionObject::Owner() const {
    const auto owner = lifetime.lock();
    if (!owner || !*owner)
        throw std::runtime_error("Script function instance is no longer available");
    return **owner;
}
Value FunctionObject::Invoke(std::span<const Value> arguments) {
    auto& owner = Owner();
    return EnterObject(*owner.session, [&] {
        if (!closure)
            return owner.InvokeFunction(*function, arguments);
        return owner.InvokeFunction(*function, arguments, receiver, definingClass, shared_from_this());
    });
}

Value NativeCallable::Invoke(std::span<const Value> arguments) {
    const auto owner = session.lock();
    if (!owner)
        throw std::runtime_error("Native function instance is no longer available");
    const auto native = owner->natives.find(name);
    if (native == owner->natives.end())
        throw std::runtime_error("Unknown native function '" + name + "'");
    return native->second(arguments);
}

namespace {

template <typename Visit>
void VisitValue(const Value& value, Visit& visit) {
    if (const auto* object = std::get_if<ObjectRef>(&value); object && *object)
        visit(*object);
}

// The strong references an object holds to other script objects. Host objects are leaves: what they retain is
// unknown, so the objects they reference simply count as externally referenced.
template <typename Visit>
void VisitReferences(const Object& object, Visit&& visit) {
    if (const auto* instance = dynamic_cast<const ScriptInstance*>(&object)) {
        for (const auto& [name, field] : instance->fields)
            VisitValue(field.value, visit);
    } else if (const auto* collection = dynamic_cast<const Collection*>(&object)) {
        collection->VisitReferences([&](const Value& value) { VisitValue(value, visit); });
    } else if (const auto* function = dynamic_cast<const FunctionObject*>(&object)) {
        for (const auto& upvalue : function->upvalues)
            visit(upvalue);
        if (function->receiver)
            visit(function->receiver);
    } else if (const auto* upvalue = dynamic_cast<const Upvalue*>(&object)) {
        if (!upvalue->open)
            VisitValue(upvalue->closed, visit);
    } else if (const auto* method = dynamic_cast<const BoundMethod*>(&object)) {
        visit(method->GetReceiver());
    } else if (const auto* parent = dynamic_cast<const SuperObject*>(&object)) {
        if (parent->instance)
            visit(parent->instance);
    }
}

} // namespace

// Trial deletion: an object's strong count minus the references found inside the graph is what holds it from
// outside (VM stacks, globals, the host). Anything not reachable from such an object is an unreachable cycle.
// No roots have to be enumerated, so references retained by C++ keep their objects alive.
std::size_t ModuleSession::CollectCycles() {
    struct Node {
        ObjectRef object;
        long external = 0;
        bool reachable = false;
    };
    std::unordered_map<const Object*, Node> nodes;
    std::vector<const Object*> order;
    // `held` counts the caller's own temporary references, which are not references from outside.
    const auto add = [&](const auto& object, long held) {
        const auto* key = static_cast<const Object*>(object.get());
        const auto [entry, inserted] = nodes.try_emplace(key);
        if (inserted) {
            entry->second.object = object;
            entry->second.external = object.use_count() - 1 - held;
            order.push_back(key);
        }
        return &entry->second;
    };
    const auto seed = [&](auto& tracked) {
        std::erase_if(tracked, [](const auto& weak) { return weak.expired(); });
        for (const auto& weak : tracked) {
            if (const auto object = weak.lock())
                add(object, 1);
        }
    };
    seed(instances);
    seed(collections);
    seed(closures);
    for (std::size_t i = 0; i < order.size(); ++i) {
        VisitReferences(*nodes.at(order[i]).object, [&](const auto& target) { --add(target, 0)->external; });
    }
    std::vector<const Object*> queue;
    for (const auto* key : order) {
        auto& node = nodes.at(key);
        if (node.external > 0) {
            node.reachable = true;
            queue.push_back(key);
        }
    }
    while (!queue.empty()) {
        const auto* key = queue.back();
        queue.pop_back();
        VisitReferences(*nodes.at(key).object, [&](const auto& target) {
            const auto* targetKey = static_cast<const Object*>(target.get());
            auto& node = nodes.at(targetKey);
            if (!node.reachable) {
                node.reachable = true;
                queue.push_back(targetKey);
            }
        });
    }
    std::size_t released = 0;
    for (const auto* key : order) {
        auto& node = nodes.at(key);
        if (node.reachable)
            continue;
        ++released;
        auto* object = node.object.get();
        if (auto* instance = dynamic_cast<ScriptInstance*>(object)) {
            instance->fields.clear();
        } else if (auto* collection = dynamic_cast<Collection*>(object)) {
            collection->alive = false;
            collection->ClearReferences();
        } else if (auto* function = dynamic_cast<FunctionObject*>(object)) {
            function->upvalues.clear();
            function->receiver.reset();
        } else if (auto* upvalue = dynamic_cast<Upvalue*>(object)) {
            upvalue->closed = {};
        }
    }
    // Releasing the nodes destroys the garbage; reachable objects only lose the collector's temporary reference.
    nodes.clear();
    allocations = 0;
    const auto expired = [](const auto& weak) { return weak.expired(); };
    std::erase_if(instances, expired);
    std::erase_if(collections, expired);
    std::erase_if(closures, expired);
    const auto survivors = instances.size() + collections.size() + closures.size();
    collectionThreshold = std::max(MinimumCollectionThreshold, survivors);
    return released;
}

InstanceData& ScriptClass::Owner() const {
    const auto owner = lifetime.lock();
    if (!owner || !*owner)
        throw std::runtime_error("Script class instance is no longer available");
    return **owner;
}
bool ScriptInstance::HasProperty(std::string_view name) const {
    type->Owner();
    const std::string key(name);
    if (fields.contains(key))
        return true;
    for (auto current = type; current; current = current->base) {
        current->Owner();
        if (current->fieldNames.contains(key))
            return true;
        if (current->host) {
            const HostScope callback(*current->Owner().session);
            if (current->host->HasProperty(name))
                return true;
        }
    }
    return false;
}
bool ScriptInstance::HasMethod(std::string_view name) const {
    for (auto current = type; current; current = current->base) {
        if (current->FindMethod(name))
            return true;
        if (current->host) {
            const HostScope callback(*current->Owner().session);
            if (current->host->HasMethod(name))
                return true;
        }
    }
    return false;
}
Value ScriptInstance::Get(std::string_view name) const {
    type->Owner();
    if (const auto found = fields.find(std::string(name)); found != fields.end())
        return found->second.value;
    for (auto current = type; current; current = current->base) {
        current->Owner();
        if (current->fieldNames.contains(std::string(name)))
            throw std::runtime_error("Field '" + std::string(name) + "' is not initialized");
        if (current->host) {
            const HostScope callback(*current->Owner().session);
            return current->host->Get(name);
        }
    }
    return Object::Get(name);
}
void ScriptInstance::Set(std::string_view name, const Value& value) {
    type->Owner();
    const auto found = fields.find(std::string(name));
    if (found == fields.end()) {
        for (auto current = type; current; current = current->base) {
            current->Owner();
            if (current->fieldNames.contains(std::string(name)))
                throw std::runtime_error("Field '" + std::string(name) + "' is not initialized");
            if (current->host) {
                const HostScope callback(*current->Owner().session);
                current->host->Set(name, value);
                return;
            }
        }
        throw std::runtime_error("Unknown field '" + std::string(name) + "'");
    }
    if (found->second.constant)
        throw std::runtime_error("Cannot assign to const field '" + std::string(name) + "'");
    found->second.value = value;
}
Value ScriptInstance::Call(std::string_view name, std::span<const Value> arguments) {
    auto& owner = type->Owner();
    const auto invoke = [&]() -> Value {
        if (HasProperty(name)) {
            const auto value = Get(name);
            if (!InstanceData::IsCallable(value))
                throw std::runtime_error("Field '" + std::string(name) + "' is not callable");
            return owner.CallValue(value, arguments);
        }
        return Dispatch(type, name, arguments);
    };
    return EnterObject(*owner.session, invoke);
}

Value ScriptInstance::Dispatch(std::shared_ptr<ScriptClass> start, std::string_view name,
                               std::span<const Value> arguments) {
    type->Owner();
    for (auto current = std::move(start); current; current = current->base) {
        if (const auto* method = current->FindMethod(name))
            return current->Owner().InvokeFunction(*method, arguments, shared_from_this(), current);
        if (current->host) {
            const auto signature = current->signatures.find(std::string(name));
            if (signature != current->signatures.end() && signature->second != arguments.size())
                throw std::runtime_error("Incorrect inherited method argument count");
            const NativeScope callback(*current->Owner().session, arguments);
            return current->host->Call(name, callback.Arguments());
        }
    }
    throw std::runtime_error("Unknown method '" + std::string(name) + "'");
}

Value ModuleObject::Get(std::string_view name) const {
    const auto owner = session.lock();
    const auto data = instance.lock();
    if (!owner || !data)
        throw std::runtime_error("Module instance is no longer available");
    if (const auto found = data->globals.find(std::string(name)); found != data->globals.end())
        return found->second.value;
    if (const auto* function = data->FindFunction(name))
        return data->FunctionValue(*function);
    return Object::Get(name);
}
Value ModuleObject::Call(std::string_view name, std::span<const Value> arguments) {
    const auto owner = session.lock();
    const auto data = instance.lock();
    if (!owner || !data)
        throw std::runtime_error("Module instance is no longer available");
    if (data->state != InstanceData::State::Ready)
        throw std::logic_error("Module is not initialized");
    return EnterObject(*owner,
                       [&] { return data->Invoke({TokenKind::Identifier, std::string(name), {}}, arguments, true); });
}

} // namespace Vice::Detail

namespace Vice {

Program::Program(std::shared_ptr<const Detail::ProgramData> data) : m_data(std::move(data)) {
}

Program Program::Parse(std::string_view source, std::string_view sourceName) {
    return Program(Detail::ParseProgram(source, sourceName));
}

std::vector<ExportedField> Program::GetExportedFields() const {
    std::vector<ExportedField> fields;
    for (const auto& type : m_data->classes) {
        for (const auto& field : type.fields) {
            if (field.exported)
                fields.push_back({type.name.text, field.token.text, field.exportDefault});
        }
    }
    return fields;
}

Instance::Instance(const Program& program)
    : m_data(std::make_unique<Detail::InstanceData>(program.m_data, std::make_shared<Detail::ModuleSession>())) {
}
Instance::~Instance() = default;

void Instance::Bind(std::string name, NativeFunction function) {
    if (m_data->state != Detail::InstanceData::State::Fresh) {
        throw std::logic_error("Bind native functions before initializing the instance");
    }
    m_data->CheckImportBinding(name);
    if (Detail::IsConstructor(name) || m_data->globals.contains(name))
        throw std::invalid_argument("Name is already declared or reserved");
    if (!function || name.empty())
        throw std::invalid_argument("A native function needs a name and a callback");
    for (const auto& global : m_data->program->globals) {
        if (global.token.text == name) {
            throw Error(m_data->program->sourceName, global.token.location,
                        "Global declaration conflicts with native function '" + name + "'");
        }
    }
    if (const auto* functionDeclaration = m_data->FindFunction(name)) {
        throw Error(m_data->program->sourceName, functionDeclaration->name.location,
                    "Function declaration conflicts with native function '" + name + "'");
    }
    if (m_data->session->natives.contains(name)) {
        throw std::invalid_argument("Function name is already declared");
    }
    m_data->session->natives.emplace(std::move(name), std::move(function));
}

void Instance::BindValue(std::string name, Value value, bool constant) {
    if (m_data->state != Detail::InstanceData::State::Fresh)
        throw std::logic_error("Bind host globals before initializing the instance");
    m_data->CheckImportBinding(name);
    if (name.empty() || Detail::IsConstructor(name) || m_data->session->natives.contains(name) ||
        m_data->globals.contains(name))
        throw std::invalid_argument("Global name is empty, already declared or reserved");
    for (const auto& global : m_data->program->globals) {
        if (global.token.text == name)
            throw Error(m_data->program->sourceName, global.token.location,
                        "Global declaration conflicts with host global '" + name + "'");
    }
    if (const auto* function = m_data->FindFunction(name))
        throw Error(m_data->program->sourceName, function->name.location,
                    "Function declaration conflicts with host global '" + name + "'");
    m_data->globals.emplace(std::move(name), Detail::Variable{std::move(value), constant});
}

void Instance::BindLibrary(std::string name, ObjectRef library) {
    if (m_data->state != Detail::InstanceData::State::Fresh)
        throw std::logic_error("Bind libraries before initializing the instance");
    if (name.empty() || !library || m_data->session->libraries.contains(name))
        throw std::invalid_argument("Library name is empty, already registered or has no object");
    m_data->session->libraries.emplace(std::move(name), std::move(library));
}
void Instance::BindClass(std::string name, ObjectRef object,
                         std::unordered_map<std::string, std::size_t> methodSignatures) {
    if (!object)
        throw std::invalid_argument("A host class needs an object");
    // Reuse the parser's identifier and reserved-name validation without exposing syntax to the host API.
    auto program = Detail::ParseProgram("class " + name + " {}", "<host class>");
    if (program->classes.size() != 1 || program->classes[0].name.text != name)
        throw std::invalid_argument("Invalid host class name");
    auto type = std::make_shared<Detail::ScriptClass>();
    auto declaration = std::make_shared<Detail::CompiledClass>();
    declaration->name = program->classes[0].name;
    type->ownedDeclaration = declaration;
    type->declaration = declaration.get();
    type->lifetime = m_data->lifetime;
    type->host = std::move(object);
    type->signatures = std::move(methodSignatures);
    type->resolved = true;
    BindValue(name, ObjectRef(type));
    m_data->session->classes.emplace(std::move(name), std::move(type));
}
ObjectRef Instance::CreateDerivedInstance(std::string_view baseName) {
    if (m_data->state != Detail::InstanceData::State::Ready)
        throw std::logic_error("Initialize before constructing a derived instance");
    const Detail::RunScope run(*m_data->session);
    const auto base = m_data->globals.find(std::string(baseName));
    const auto* reference = base != m_data->globals.end() ? std::get_if<ObjectRef>(&base->second.value) : nullptr;
    const auto baseType = reference ? std::dynamic_pointer_cast<Detail::ScriptClass>(*reference) : nullptr;
    if (!baseType)
        throw Error(m_data->program->sourceName, {}, "Unknown base class '" + std::string(baseName) + "'");
    std::vector<std::shared_ptr<Detail::ScriptClass>> candidates;
    for (const auto& declaration : m_data->program->classes) {
        m_data->Step(declaration.name);
        const auto type = std::dynamic_pointer_cast<Detail::ScriptClass>(
            std::get<ObjectRef>(m_data->globals.at(declaration.name.text).value));
        for (auto parent = type->base; parent; parent = parent->base) {
            if (parent == baseType) {
                candidates.push_back(type);
                break;
            }
        }
    }
    const auto all = candidates;
    std::erase_if(candidates, [&](const auto& candidate) {
        for (const auto& other : all) {
            m_data->Step(other->declaration->name);
            for (auto parent = other->base; parent; parent = parent->base) {
                if (parent == candidate)
                    return true;
            }
        }
        return false;
    });
    if (candidates.empty())
        throw Error(m_data->program->sourceName, {},
                    "No class inheriting '" + std::string(baseName) + "' was declared in the root source");
    if (candidates.size() != 1) {
        std::string names;
        for (const auto& candidate : candidates) {
            if (!names.empty())
                names += ", ";
            names += candidate->declaration->name.text;
        }
        throw Error(m_data->program->sourceName, {},
                    "Multiple most-derived classes inherit '" + std::string(baseName) + "': " + names);
    }
    return std::get<ObjectRef>(m_data->Instantiate(candidates.front(), {}));
}

void Instance::SetModuleLoader(ModuleLoader loader) {
    if (m_data->state != Detail::InstanceData::State::Fresh)
        throw std::logic_error("Set the module loader before initializing the instance");
    m_data->session->loader = std::move(loader);
}
void Instance::Initialize() {
    const Detail::RunScope run(*m_data->session);
    m_data->Initialize();
}

ObjectRef Instance::CreateCoroutine(const ObjectRef& function) {
    return m_data->session->MakeCoroutine(function);
}

void Instance::SetDebugHandler(DebugHandler handler) {
    m_data->session->debugHandler = std::move(handler);
    m_data->session->step = DebugAction::Continue;
}
void Instance::SetBreakpoints(std::string sourceName, std::vector<std::size_t> lines) {
    auto& breakpoints = m_data->session->breakpoints;
    if (lines.empty())
        breakpoints.erase(sourceName);
    else
        breakpoints[std::move(sourceName)] = {lines.begin(), lines.end()};
}
void Instance::ClearBreakpoints() {
    m_data->session->breakpoints.clear();
}

void Instance::SetProfiling(bool enabled) {
    m_data->session->profiling = enabled;
    m_data->session->profiled = nullptr;
}

std::vector<FunctionProfile> Instance::GetProfile() const {
    std::vector<FunctionProfile> profile;
    for (const auto& [code, entry] : m_data->session->profile)
        profile.push_back(entry.data);
    std::ranges::sort(profile, [](const FunctionProfile& a, const FunctionProfile& b) { return a.time > b.time; });
    return profile;
}

void Instance::ResetProfile() {
    // Frames still running keep pointers to their entries, so entries are zeroed rather than removed.
    for (auto& [code, entry] : m_data->session->profile) {
        entry.data.calls = 0;
        entry.data.instructions = 0;
        entry.data.allocations = 0;
        entry.data.time = {};
    }
}

std::size_t Instance::CollectCycles() {
    if (m_data->session->running)
        throw std::logic_error("Cannot collect cycles while the instance is running");
    return m_data->session->CollectCycles();
}

bool Instance::HasFunction(std::string_view name) const {
    return m_data->FindFunction(name) != nullptr;
}

std::size_t Instance::GetParameterCount(std::string_view name) const {
    const auto* function = m_data->FindFunction(name);
    if (!function)
        throw std::invalid_argument("Unknown script function");
    return function->parameterCount;
}

Value Instance::Call(std::string_view name, std::span<const Value> arguments) {
    if (m_data->state != Detail::InstanceData::State::Ready)
        throw std::logic_error("Initialize before calling functions");
    const Detail::RunScope run(*m_data->session);
    return m_data->Invoke({Detail::TokenKind::Identifier, std::string(name), {}}, arguments);
}

} // namespace Vice
