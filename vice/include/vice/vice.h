#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Vice {

struct Vector2 {
    double x = 0, y = 0;
    bool operator==(const Vector2&) const = default;
};
struct Vector3 {
    double x = 0, y = 0, z = 0;
    bool operator==(const Vector3&) const = default;
};
struct Color {
    double r = 0, g = 0, b = 0, a = 1;
    bool operator==(const Color&) const = default;
};

class Object;
using ObjectRef = std::shared_ptr<Object>;
using Value = std::variant<std::monostate, bool, double, std::string, Vector2, Vector3, Color, ObjectRef>;
std::string ToString(const Value& value);

// Objects have reference semantics, including script class instances. Value structs above are copied instead.
// Host implementations resolve their
// own handles and enforce property/method access; neither this interface nor Values depend on the evaluator.
class Object {
public:
    virtual ~Object() = default;
    virtual std::string_view GetTypeName() const;
    virtual bool HasMethod(std::string_view name) const;
    // Property discovery does not invoke getters. It supports implicit member lookup in class methods.
    virtual bool HasProperty(std::string_view name) const;
    virtual Value Get(std::string_view name) const;
    virtual void Set(std::string_view name, const Value& value);
    virtual Value GetIndex(const Value& index) const;
    virtual void SetIndex(const Value& index, const Value& value);
    virtual Value Call(std::string_view name, std::span<const Value> arguments);
    // Function values, bound methods and host callables. Invoking a script function from C++ runs it to
    // completion with the same execution limits as Instance::Call.
    virtual bool IsCallable() const;
    virtual Value Invoke(std::span<const Value> arguments);
    // The fields a script instance declares, base classes' first, in declaration order; empty for other objects.
    virtual std::vector<std::string> GetFieldNames() const;
};

using NativeFunction = std::function<Value(std::span<const Value>)>;

// Native libraries use the same object/value contract as script modules, independently of the evaluator.
class Library final : public Object {
public:
    bool HasProperty(std::string_view name) const override;
    bool HasMethod(std::string_view name) const override;
    Library& Constant(std::string name, Value value);
    Library& Function(std::string name, NativeFunction function);
    Value Get(std::string_view name) const override;
    Value Call(std::string_view name, std::span<const Value> arguments) override;

private:
    void CheckName(std::string_view name) const;
    std::unordered_map<std::string, Value> m_constants;
    std::unordered_map<std::string, NativeFunction> m_functions;
};

struct ModuleSource {
    std::string source;
    // Stable canonical identity as well as the diagnostic name. The host resolves paths or asset ids.
    std::string sourceName;
};
using ModuleLoader = std::function<std::optional<ModuleSource>(std::string_view request, std::string_view importer)>;

struct SourceLocation {
    std::size_t line = 1;
    std::size_t column = 1;
};

class Error final : public std::runtime_error {
public:
    Error(std::string_view sourceName, SourceLocation location, std::string_view message);
    SourceLocation GetLocation() const { return m_location; }
    // The script calls that led to the error, innermost first, one "at Function (file:line:column)" line each,
    // appended to what(). Empty for errors outside a call or in the first frame.
    const std::string& GetTrace() const { return m_trace; }
    Error WithTrace(std::string trace) const;

private:
    struct Traced {};
    Error(Traced, std::string message, SourceLocation location, std::string trace);
    SourceLocation m_location;
    std::string m_trace;
};

namespace Detail {
struct ProgramData;
struct InstanceData;
} // namespace Detail

// What profiling recorded for one function, or one initializer, of a source. Time and instructions are the
// function's own, without the script functions it called; native work it does is included.
struct FunctionProfile {
    std::string name;
    std::string sourceName;
    std::size_t line = 0;
    std::uint64_t calls = 0;
    std::uint64_t instructions = 0;
    std::uint64_t allocations = 0;
    std::chrono::nanoseconds time{};
};

// A paused script call, innermost first in a DebugEvent: where it is and its live local variables (and `self`).
struct DebugFrame {
    std::string function;
    std::string sourceName;
    SourceLocation location;
    std::vector<std::pair<std::string, Value>> locals;
};
enum class DebugReason { Breakpoint, Step };
struct DebugEvent {
    DebugReason reason = DebugReason::Breakpoint;
    std::vector<DebugFrame> frames;
};
// What execution does after the handler returns: run to the next breakpoint, or stop at the next line run (StepIn),
// the next line of this call or its callers (StepOver), or the next line after this call returns (StepOut).
enum class DebugAction { Continue, StepIn, StepOver, StepOut };
// Called on the thread running the script, which waits for it: a host can block in it, as an interactive debugger
// does, or only record the event. It must not call into the instance.
using DebugHandler = std::function<DebugAction(const DebugEvent& event)>;

// A class field declared `export var`, with the value of its constant initializer.
struct ExportedField {
    std::string className;
    std::string name;
    Value value;
};

// Immutable compiled code, shareable by instances that keep their own variables and VM stacks.
class Program {
public:
    static Program Parse(std::string_view source, std::string_view sourceName = "<script>");
    // The exported fields of the classes this source declares, in declaration order, without running it.
    std::vector<ExportedField> GetExportedFields() const;

private:
    friend class Instance;
    explicit Program(std::shared_ptr<const Detail::ProgramData> data);
    std::shared_ptr<const Detail::ProgramData> m_data;
};

class Instance {
public:
    explicit Instance(const Program& program);
    ~Instance();
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    // Bind before Initialize(). The host decides which operations scripts can perform.
    void Bind(std::string name, NativeFunction function);
    // Injects a host global before initialization. Const prevents rebinding, not mutation of a referenced object.
    void BindValue(std::string name, Value value, bool constant = true);
    // Libraries are available only through explicit imports. Configure both before Initialize().
    void BindLibrary(std::string name, ObjectRef library);
    // A host class supplies inherited object operations and optional method signature constraints. It is visible
    // in source modules too. Object::Call supports bounded synchronous hooks; Instance::Call cannot reenter.
    void BindClass(std::string name, ObjectRef object,
                   std::unordered_map<std::string, std::size_t> methodSignatures = {});
    // After Initialize(), construct the sole most-derived class in this source that inherits the named base.
    // Missing or ambiguous classes are errors. Its constructor must accept no arguments.
    ObjectRef CreateDerivedInstance(std::string_view baseName);
    void SetModuleLoader(ModuleLoader loader);
    // Evaluates global initializers once. Must precede Call().
    void Initialize();
    bool HasFunction(std::string_view name) const;
    std::size_t GetParameterCount(std::string_view name) const;
    Value Call(std::string_view name, std::span<const Value> arguments = {});
    // Wraps a script function, closure or bound method, called without arguments, in a coroutine: an object whose
    // resume() runs it until it yields or returns, giving back the yielded or returned value; `done` tells whether
    // it returned and `status` is "suspended", "running" or "dead". The same as core.coroutine(function).
    ObjectRef CreateCoroutine(const ObjectRef& function);
    // While on, the instance and its modules record calls, instructions, script objects and collections allocated,
    // and execution time per function. It costs a clock read per instruction, so it is off by default.
    void SetProfiling(bool enabled);
    // Functions that ran since profiling started or was last reset, most time first.
    std::vector<FunctionProfile> GetProfile() const;
    void ResetProfile();
    // Pauses before running the first instruction of a breakpoint's line, in the instance and its modules, calling
    // the handler. Lines are one based; sourceName is the program's or module's canonical name. Without a handler,
    // breakpoints are ignored and execution keeps its normal speed.
    void SetDebugHandler(DebugHandler handler);
    void SetBreakpoints(std::string sourceName, std::vector<std::size_t> lines);
    void ClearBreakpoints();
    // Releases unreachable cycles of script objects, collections and closures, returning how many objects were
    // freed. It also runs automatically between host calls once enough of them were allocated.
    std::size_t CollectCycles();

private:
    std::unique_ptr<Detail::InstanceData> m_data;
};

} // namespace Vice
