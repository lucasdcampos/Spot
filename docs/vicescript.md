# ViceScript

ViceScript is an experimental, dynamically typed language. Its C++20 runtime lives in `vice/` and depends only on
the standard library. Spot's optional adapter is `engine/scene/scripting/vice_runtime.*`. Enabling it registers the VICE runtime for
the generic `ScriptComponent` without changing scene scheduling. Standard and user libraries remain independent of Spot.

## Build

The experiment is off by default. To enable the runtime, command line, engine component and tests:

```sh
cmake --preset msvc -DSPOT_ENABLE_VICESCRIPT=ON
cmake --build --preset msvc-debug --target vice_cli vice_tests spot_script_asset_tests spot_vice_tests spot_vice_shared_tests
ctest --preset msvc-debug -R "ViceScript|Script asset|Reimporting script"
```

Use the `ninja` and `ninja-debug` presets on other platforms. Game modules built through `find_package(Spot CONFIG)`
inherit whether that engine build supports ViceScript.

The runtime and command line also build on their own, without configuring or linking Spot:

```sh
cmake -S vice -B build/vice
cmake --build build/vice --config Debug
```

The command is `vice <file.vice> [update-count]`. It calls `start()` once, then `update(delta_time)` the requested
number of times (one by default), with a simulated delta of 1/60 second. For example, with the MSVC engine build:

```powershell
./build/msvc/vice/Debug/vice.exe vice/examples/hello.vice 3
```

The command exits with 1 on a script or loading error and 2 on an invalid command shape. `VICE_BUILD_CLI=OFF` omits it.

## Language

```go
const message = "Hello from ViceScript!"
var frames = 0
var target = null

func start() {
    print(message, target)
}

func update(delta_time) {
    frames = frames + 1
    print("Frame:", frames, "Delta:", delta_time)
}
```

- Values are `null`, booleans, numbers (double precision), strings, `Vector2`, `Vector3`, `Color`, arrays, maps, ranges, script classes/instances and host objects.
  `nil` is an ordinary identifier, not a literal.
- `var` is mutable and may change type. Without an initializer it holds `null`. `const` requires an initializer and
  cannot be reassigned. Reading an undeclared variable is an error.
- File scope accepts imports, variable, function and class declarations. Global initializers execute in declaration order when
  the instance initializes. Functions may be declared after their callers.
- Globals belong to one instance and survive calls. Functions have their own local scope; braces introduce nested
  scopes. Locals can shadow outer bindings. Functions see their own locals and globals, never their caller's locals.
- Functions accept positional parameters and support `return expression` and bare `return`. Falling off the end
  returns `null`. Arguments must match the declared parameter count. Functions and methods are values; see
  [Functions as values](#functions-as-values).
- Newlines or semicolons separate statements. The final statement before `}` needs neither. Line breaks inside
  parentheses are accepted around arguments and grouped expressions. An expression continues on the next line after
  a binary operator ending a line (`total = a +` then `b` below it); a line starting with an operator does not.
- `+`, `-`, `*`, `/`, `%`, unary `+`/`-` and parentheses support numeric arithmetic, with multiplication, division
  and modulo before addition and subtraction. `%` is floored: the result takes the divisor's sign, so `-1 % 4` is
  `3`, which wraps indices and angles. `+=`, `-=`, `*=`, `/=` and `%=` combine it with assignment. `+` also joins two
  strings. There is no implicit conversion between types.
- Strings use double quotes, UTF-8 content and the escapes `\n`, `\r`, `\t`, `\"`, `\\`, `\{` and `\}`. Comments start with `//`.
- `{expression}` inside a string interpolates it: `"{player.name} has {score * 2} points"`. Each part is formatted
  as `print` formats values (`null`, `true`, `1.5`, `Vector3(1, 2, 3)`), so numbers need no conversion. The
  expression is any expression on the same line, including strings with their own interpolations; write `\{` and
  `\}` for literal braces. Syntax errors inside report their position within the string.
- The host supplies `print(...)`. It separates values with spaces and writes one message; `null` prints as `null`.
- `if condition { ... } else if condition { ... } else { ... }` selects a scoped block. Conditions, `not`, `and` and
  `or` require booleans; there is no implicit truthiness. Logical operators short circuit left to right.
  `!`, `&&` and `||` remain supported aliases, with the same precedence. New scripts should use the word forms.
  `not`, `and` and `or` are reserved words.
- `==` and `!=` compare values, with different types unequal. Structs compare their fields; classes, script instances and host objects compare
  reference identity. A null host reference equals `null`. `<`, `<=`, `>` and `>=` compare numbers. Precedence,
  highest first: member access/calls, unary operators, multiplication/division/modulo, addition/subtraction, ordered
  comparisons and `is`, equality, `and`, `or`.
- `value is Class` tells whether a value is an instance of a script or host class, or of one derived from it:
  `if other is Enemy`, `self is Component`. `null` and values that are not instances are never instances; a right
  side that is not a class is an error. `value is Vector2`, `is Vector3` and `is Color` check value types. `is` is a
  reserved word. `not` has the same precedence as the other unary operators;
  use `not (a == b)` to negate a comparison.
- `Vector2(x, y)`, `Vector3(x, y, z)` and `Color(r, g, b, a)` require the stated number of finite numeric arguments.
  Vectors expose `x`, `y`, `z` as appropriate; colors expose `r`, `g`, `b`, `a`. Constructors are reserved at file
  scope and cannot be rebound by the host.
- Vectors and colors support arithmetic: `+`, `-`, `*` and `/` between two values of the same type work per
  component, `vector * number`, `number * vector` and `vector / number` scale, and unary `-` negates. Mixing types,
  adding a number or dividing by a zero component are errors. Vectors also have `length()`, `length_squared()`,
  `normalized()` (the zero vector stays zero), `dot(other)` and `distance(other)`; `Vector3` adds `cross(other)`.
  `position += velocity * deltaTime` therefore moves a position.
- `value.member` reads a property; `value.method(arguments)` calls a host method. Members and methods are case
  sensitive; their names are defined by the host, without imposing an engine API naming convention.
- Assignment accepts variables and properties, including `+=`, `-=`, `*=` and `/=`. Assignments are statements,
  not expressions. Compound assignment evaluates the receiver and reads its old value once, before evaluating
  the right side. Simple property assignment does not call the destination getter.

```go
var position = Vector3(0, 0, 0)

func update(delta_time) {
    position.x += 3 * delta_time
    if position.x >= 10 {
        position.x = 0
    }
}
```

Structs copy by value: assigning `var other = position` creates an independent value. `const` prevents changes
to the bound value, including struct fields, while a const host-object reference still permits changes to its
writable properties. Function results containing structs are temporary values and cannot be assigned through.
Object and collection results can be modified because they represent references.

Nested writes such as `object.transform.position.x += 1` read intermediate properties once, modify the copied
vector, and send the replacement through the position setter. A read-only intermediate struct rejects writeback;
a read-only property returning a host object still allows changing that object's writable properties. Host setters
remain responsible for validation and synchronizing changes with their systems. The Spot adapter exposes these
capabilities through the scene API below.

Errors include source name, line and column (one based; columns count source bytes). An error raised inside nested
calls also lists them, innermost first, after its message, one `  at Function (file:line:column)` line per call
pointing at the call site (`Vice::Error::GetTrace()` returns that part); a component fault logs them with it.
Recursive syntax, VM call depth, value stack size and work per host call are bounded so ordinary script errors
terminate with diagnostics.

The engine integration is shared across languages; see [Scripting integration](scripting.md) for the runtime
adapter contract and how to add another language without duplicating scene bindings or component lifecycle.

## Loops and collections

`while condition { ... }` tests a strict boolean before each iteration. `break` exits the innermost loop and
`continue` advances it. Both are rejected outside a loop, even in an unreachable branch. Loop bodies have lexical
scope; locals are released on normal exits, `break`, `continue` and `return`. Backward jumps consume the same VM
instruction budget as other code, so an infinite loop raises a source-located error instead of hanging the game.

```go
func Countdown() {
    var remaining = 3
    while remaining > 0 {
        remaining -= 1
        if remaining == 1 { continue }
        print(remaining)
    }
}
```

Arrays use `[value, ...]` and maps use `{"key": value, ...}`. Empty literals are `[]` and `{}`; multiline literals
and trailing commas are supported. Entries evaluate from left to right, and each evaluation creates a fresh
collection. Arrays and maps are reference values: assignments and function arguments share the collection, and
equality compares identity. `const` prevents rebinding the variable while allowing changes to its contents.
Struct elements still copy by value; nested writes such as `positions[0].x += 1` replace the stored struct.

```go
func InventoryExample() {
    const items = ["key", "potion"]
    items.push("coin")
    items[1] = "health potion"

    const stats = {"health": 100, "speed": 4}
    stats["health"] -= 10
    print(items.length, stats.get("armor", 0))
}
```

| Collection | Operations |
| --- | --- |
| Array | Zero-based `array[index]`, writable existing elements, read-only `.length`, `.push(value)`, `.pop()`, `.insert(index, value)`, `.remove_at(index)`, `.clear()` |
| Map | String-only `map[key]`, insertion/replacement through assignment, read-only `.length`, `.get(key[, fallback])`, `.has(key)`, `.remove(key)`, `.keys()`, `.clear()` |
| Range | `range(end)`, `range(start, end)`, `range(start, end, step)`, read-only `.length` and indexed reads |

Array indices must be finite exact integers within bounds; assignment does not grow the array. `insert` also
accepts the index equal to length. `pop` and `remove_at` return the removed value and reject empty arrays or bad
indices. `push`, `insert` and `clear` return `null`. Map index reads reject missing keys; `get` returns its fallback
or `null`. `remove` returns whether a key existed. Duplicate map literal keys use the last value. `keys` returns
a fresh array sorted by key. Map keys use explicit bracket access; `.length` is collection metadata.

`for value in iterable { ... }` iterates array/range values or map keys. `for index, value in array` and
`for key, value in map` bind both parts. The iterable is evaluated once; loop variables are mutable, scoped to
the loop, and rebound each iteration. Arrays preserve index order; maps use sorted string keys for deterministic
iteration. Ranges exclude the end, default to step `1`, and support negative steps. Endpoints and steps must be
exact integers within the safe double integer range; a zero step is rejected. Ranges do not allocate their values.

```go
func MoveExample() {
    var positions = [Vector3(0, 0, 0), Vector3(1, 0, 0)]
    for index, position in positions {
        positions[index].x += 2
    }
    for wave in range(1, 4) { print("Wave", wave) }
}
```

Replacing existing array elements or map values during iteration is supported. Changing array length or adding
or removing map keys invalidates the iterator; its next advance raises an error. A mutation immediately followed
by `break` is allowed. Reassigning the variable holding the collection does not change the iterator's receiver.
Collections and ranges are limited to 1,048,576 entries. Native collection operations are bounded by that size;
their internal C++ work is not preempted instruction by instruction.

`core.type` returns `array`, `map` or `range`; string formatting uses `<array>`, `<map>` and `<range>` without
recursively expanding contents. Collections belong to their root session, expire when it ends and release their
reference cycles at root destruction; unreachable cycles are also collected during execution (see
[Memory and cycle collection](#memory-and-cycle-collection)). Deep acyclic graphs of script instances and collections release
iteratively instead of recursively consuming the C++ stack. Host objects may implement indexed access through the public
`Object::GetIndex`/`SetIndex` protocol. Engine bindings continue to accept their declared engine value types;
passing a Vice collection to a native API does not implicitly convert it into a C++ container.

Run `vice/examples/collections.vice` with the standalone CLI for a complete example.

## Functions as values

A file-scope function, a method, a native host function such as `print`, or a library function such as `math.max`
read without calling it is a value. Store it in a variable, field or collection, pass it as an argument, return it,
and call it with parentheses: `f(x)`, `callbacks[0](x)` or `Pick("jump")()`. Calls through values check the
argument count like direct calls, and calling a value that is not a function or class is an error.

```go
class Button {
    var onClick
    func Click() { return onClick() }
}

class Score {
    var points = 0
    func Add() { points += 10; return points }
}

const score = Score()

func start() {
    const button = Button()
    button.onClick = score.Add
    print(button.Click()) // 10
}
```

Reading a method through an instance, as `score.Add` or as `Add` inside a class, produces a **bound method**: it
keeps its receiver and dispatches by name on each call, so overrides apply. `super.Method` cannot be read as a value.
Host objects' methods (`Object::HasMethod` without a property of the same name) bind the same way. Free functions
keep no receiver, as when called directly. A field holding a function is called like a method: `button.onClick()`.

Functions compare by what they call: `Double == Double`, and `a.add == a.add` while `a.add != b.add`. `core.type`
returns `function`, and they print as `<function>`. Function values reference their instance's code: calling one
after its root instance ends is an error. A bound method retains its receiver, so a script instance storing its own
bound method forms a cycle; like other cycles, it is collected once unreachable.

C++ calls a function value through the public `Object` protocol: `IsCallable()` and `Invoke(arguments)` run it to
completion within the owning instance's execution limits, from the host or from a native callback. A function from
another root instance may be passed in with `BindValue`; it runs in its own instance's state.
`vice/examples/callbacks.vice` demonstrates fields holding callbacks and bound methods.

## Anonymous functions and closures

`func(parameters) { ... }` is an expression creating an anonymous function. Inside a function body,
`func Name(parameters) { ... }` declares a local function: it binds `Name` before its body runs, so it can call
itself. Both capture the variables of their enclosing functions lexically, by reference: a closure reads and writes
the variable itself, not a copy taken at creation, and changes are visible to the enclosing function and to other
closures over the same variable.

```go
func MakeCounter(start) {
    var count = start
    return func(step) {
        count += step
        return count
    }
}

func start() {
    const counter = MakeCounter(10)
    counter(1)
    print(counter(5)) // 16
    const actions = []
    for i in range(3) {
        actions.push(func() { return i })
    }
    print(actions[0](), actions[2]()) // 0 2
}
```

- Every declaration starts a new variable, so each loop iteration, block entry and call gives closures their own
  binding: the loop above captures `0`, `1` and `2`. A captured variable outlives its scope while a closure uses it.
- `const` captures cannot be assigned. Captured vectors and colors are variables like any other: `moved.x += 1`
  inside a closure changes the enclosing variable.
- A closure created in a method or field initializer keeps that instance: fields, implicit methods, `self` and
  `super` work as in the method itself. Closures created in free functions have no receiver.
- Globals are not captured; closures look them up by name in their declaring module, as functions do.
- Closures compare by identity: two evaluations of the same `func` expression create different values.
- Closures are `function` values: they are stored, passed, returned and invoked from C++ like other functions.

The compiler resolves captures: each closure lists the enclosing locals or enclosing captures it uses, and the VM
shares one *upvalue* per captured variable. While its scope is live the upvalue aliases the value stack slot; when
the binding ends (scope exit, redeclaration, return or a failed call), it keeps the last value. Locals that are never
captured cost nothing extra. `vice/examples/closures.vice` shows counters, event listeners and a recursive local
function.

## Enums and match

`enum` declares a set of named members at file scope, separated by commas or newlines:

```go
enum State { Idle, Walking, Running }

class Player : Component {
    var state = State.Idle

    override func OnUpdate(deltaTime) {
        match state {
            State.Idle { Look() }
            State.Walking, State.Running { Move(deltaTime) }
        }
    }
}
```

- Each member is a distinct value: `State.Idle == State.Idle`, while members of different enums never compare equal,
  even with the same name. A member has read-only `name` (`"Idle"`) and `value` (its position from 0), prints as
  `State.Idle`, and `core.type` returns `enum`. `State.values()` returns a new array of the members in order.
- Enums are constants like classes: they cannot be reassigned, are exported by modules (`models.State.Idle`) and
  cannot be declared inside functions or classes. `values` is reserved as a member name.

`match value { ... }` evaluates the value once and runs the first case whose pattern matches, with the case's
block as a scope; nothing runs when none matches. A case lists patterns separated by commas: a pattern is compared
with `==`, or, written `is Type`, checks the value's class or type as `is` does. `else { ... }`, when present, must be
the last case. `break`, `continue` and `return` inside a case act on the enclosing loop or function, as in an `if`.

```go
func Describe(value) {
    match value {
        0 { return "zero" }
        "hello", "hi" { return "greeting" }
        is Enemy { return "enemy" }
        is Vector3 { return "position" }
        else { return "something else" }
    }
}
```

## Coroutines

A coroutine runs a function on its own stack and pauses it at each `yield`, keeping its locals, captures and
receiver until it is resumed. `yield value` gives a value back to whoever resumed it; a bare `yield` gives `null`.

```go
import core

func Countdown(from) {
    var remaining = from
    while remaining > 0 {
        yield "{remaining}..."
        remaining -= 1
    }
    return "go!"
}

func start() {
    const countdown = core.coroutine(func() { return Countdown(3) })
    while not countdown.done {
        print(countdown.resume()) // 3... 2... 1... go!
    }
}
```

- `core.coroutine(function)` takes a function, closure or bound method called without arguments.
  `coroutine.resume()` runs it until it yields or returns and gives back that value. `done` becomes true once it has
  returned, and `status` is `"suspended"`, `"running"` or `"dead"`. Resuming a dead or running coroutine is an error.
- `yield` works in any function the coroutine calls, however deep, as long as only script calls are between them.
  A native function in between, such as a host callback calling back into script, runs on the C++ stack and cannot
  be suspended: yielding there is an error, as is yielding outside a coroutine.
- An error inside a coroutine ends it and propagates from `resume()`, with the calls inside the coroutine as its
  trace. Coroutines may resume other coroutines.
- Each resume from the host is a host call with the usual execution budget. Closures created inside a coroutine
  keep working after it suspends or ends. A suspended coroutine's stack counts as an external reference for cycle
  collection, so a cycle through it lasts until the coroutine finishes or the root instance ends.
- C++ creates one with `Instance::CreateCoroutine(function)` and drives it with `Call("resume")` and `Get("done")`.

Components start coroutines the engine resumes; see [Coroutines in components](#coroutines-in-components).
`vice/examples/coroutines.vice` resumes one per update with the standalone CLI.

## Memory and cycle collection

Values are reference counted, so most objects are released as soon as their last reference goes away. Reference
cycles, such as an instance whose field holds itself, an array containing a closure that captures the array, or a
bound method stored in its receiver, are found by a cycle collector during execution, besides being released at
root destruction.

The collector uses trial deletion over the root session's script instances, arrays, maps and closures, following
references through fields, elements, captured variables, bound methods and closure receivers. An object's reference
count minus the references found inside that graph is what holds it from outside: VM stacks, globals, modules or
the host. Objects not reachable from such an object belong to unreachable cycles; their references are cleared and
they are released. Since no roots have to be enumerated, an `ObjectRef` retained by C++ always keeps its object, and
everything it references, alive. Host objects are leaves: script objects they reference count as externally held.

Collection runs automatically when a host call enters an instance (`Call`, `Invoke` or a component hook) after
4,096 containers, or as many as survived the previous collection if that is greater, were allocated since the last
one. It never runs in the middle of script execution, so cycles created and dropped within one call wait for a
later call. `Instance::CollectCycles()` runs it explicitly between calls and returns how many objects were released.
There are no finalizers. The collector never looks inside host objects, so a cycle through one (a native object
holding a script callback that captures that object) stays alive until the root ends; host implementations should
hold such back references weakly.

## Classes, instances and inheritance

Declare a class at file scope, construct it with `ClassName(arguments)`, and access its declared fields and methods
with a dot. Inside methods and field initializers, fields and methods can be used directly: `value += 1` or
`Move(deltaTime)`. `self` is an optional explicit receiver.
`class`, `extends`, `self` and `super` are reserved words. Existing script variables named `self` or `super` must
be renamed (host-injected `self` remains readable for embedding compatibility).

```go
class Counter {
    var value = 0
    const label = "counter"

    func init(value) {
        self.value = value
    }

    func add(amount) {
        value += amount
        return value
    }
}

class DoubleCounter : Counter {
    func init(value) {
        super.init(value)
    }

    override func add(amount) {
        return super.add(amount * 2)
    }
}

var counter = DoubleCounter(10)

func start() {
    print(counter.add(3)) // 16
}
```

- Each construction evaluates field initializers afresh, base fields first, then derived fields, in declaration order.
  Fields without initializers hold `null`. Initializers see their declaring module's globals and `self`, including
  fields already initialized, rather than the caller's local variables. Global initialization order still applies.
- Name lookup checks parameters and local scopes first, then the current instance's fields/properties/methods,
  then the declaring module's globals and functions. This also applies to inherited methods, using the actual
  receiver for virtual dispatch. A local `var value` or parameter `value` shadows the field; use `self.value` to
  reach it, as in `init(value) { self.value = value }`. Free functions do not inherit their caller's instance.
  Typos do not create fields. Fields declared later do not fall back to equally named globals before initialization.
- All fields and methods are public. Assigning an undeclared field is an error. `const` fields need initializers and
  cannot be replaced, including through nested vector/color writes; an object stored in a const field remains mutable.
- Instances have reference semantics: assigning or passing an instance shares it, and equality compares identity.
  `const` on a variable prevents rebinding, while writable instance fields remain mutable. `core.type` reports `object`
  for classes and instances. Vector and color fields retain their existing copy semantics.
- `init` is the constructor method. Its argument count must match exactly, and its return value is ignored by
  construction. Without any inherited or declared `init`, construction takes no arguments. A derived class without
  `init` inherits its nearest base constructor; a derived class with `init` must call `super.init(...)` explicitly
  when base constructor behavior is needed. Field initializers always run before the selected constructor.
- `:` (or the `extends` alias) accepts one class name or an imported name such as `models.Actor`. Classes are registered
  and inheritance is resolved before global initializers, so a base class may appear later in the file. Bases must be
  script classes or host classes registered with `Instance::BindClass`; arbitrary host objects cannot be bases.
  Inheritance cycles and chains exceeding 32 classes are rejected. Root global variable aliases are not available as bases
  during this phase.
  Inheritance stays within one root instance's module session; a base class injected from another root instance is rejected.
- Methods dispatch on the actual instance, including implicit `method()` calls inside inherited methods. A derived method must
  use `override func` when replacing an inherited method, including native `Component` hooks. An override requires an
  inherited method and the same parameter count when its signature is known. These checks run before global or field
  initializers, including across source modules. `override` is a reserved keyword and cannot decorate fields or free
  functions. Constructors remain `func init`; they cannot use `override`, and may choose a different parameter count.
  `super.method(...)` starts lookup at the parent of the class declaring the
  executing method, preserving the current instance. `self` cannot be rebound inside methods. `super` provides method
  calls, not field access. Fields cannot hide inherited fields or methods, and methods cannot hide inherited fields.
- Source libraries export classes: use `models.Actor(...)`, or store `const Actor = models.Actor` and call `Actor(...)`.
  Method bodies and field initializers use their declaring module's lexical globals. Each root instance has independent
  module and class state. Methods can be read as bound values; there are no static members, access modifiers or multiple
  inheritance.
- Calls and initializers share the existing execution limits and diagnostics. Script instances implement the public
  `Vice::Object` protocol, so C++ can read/write fields and call methods through `ObjectRef`. Retained references become
  invalid when their root instance ends. Unreachable reference cycles are collected during execution and released at
  root destruction.

`vice/examples/classes.vice` demonstrates independent instances, constructors, overridden methods and `super`:

```powershell
./build/msvc/vice/Debug/vice.exe vice/examples/classes.vice
```

## Libraries and imports

Imports are file-scope declarations and are resolved before global initializers. The alias is optional:

```go
import math
import math as numbers
import combat
import "helpers/combat.vice" as Battle

func start() {
    print(math.clamp(12, 0, 10), numbers.pi)
    print(Battle.calculate_damage(20, 6))
}
```

Without `as`, a bare import uses its name and a quoted path uses its final filename without the extension.
`import "helpers/combat.vice"` therefore binds `combat`. Aliases must be valid non-reserved identifiers and
cannot conflict with other file-scope declarations or host globals/functions. Import references cannot be rebound.
Standard libraries are registered by the runtime but are available only through an import:

| Library | Members |
| --- | --- |
| `math` | Constants `pi`, `tau`; `abs(x)`, `sign(x)`, `min(a,b)`, `max(a,b)`, `clamp(value,min,max)`, `lerp(a,b,t)` (numbers, vectors or colors), `inverse_lerp(a,b,value)`, `smoothstep(edge0,edge1,x)`, `move_toward(current,target,maxStep)`, `floor(x)`, `ceil(x)`, `round(x)`, `sqrt(x)`, `pow(base,exponent)`, `exp(x)`, `log(x[,base])`, `sin(x)`, `cos(x)`, `tan(x)`, `asin(x)`, `acos(x)`, `atan(x)`, `atan2(y,x)` |
| `string` | `length(text)`, `find(text,needle[,start])`, `substring(text,start[,length])`, `replace(text,needle,replacement)`, `split(text,separator)`, `join(array,separator)`, `upper(text)`, `lower(text)`, `trim(text)`, `starts_with(text,prefix)`, `ends_with(text,suffix)` |
| `core` | `type(value)`, `to_string(value)`, `to_number(text)`, `assert(condition[,message])` |
| `random` | `value()` in [0, 1), `range(min,max)` in [min, max), `int(min,max)` inclusive, `chance(probability)`, `choice(array)`, `seed(integer)` |

Math uses finite numbers; invalid domains, reversed clamp bounds and non-finite results are errors. `lerp` permits
extrapolation. String sizes/offsets are **zero-based UTF-8 byte offsets**, not code-point or grapheme indices.
`find` returns `null` when absent. Substring indices/counts must be nonnegative integers within the string.
`replace` replaces all non-overlapping occurrences and rejects an empty needle. `split` returns a new array, keeping
empty parts; `join` formats each element as `print` does. `upper`/`lower` change ASCII letters only and `trim`
removes spaces, tabs and line breaks. `core.to_number` parses a decimal number and returns `null` for anything else.
`move_toward` steps toward the target without passing it; `smoothstep` clamps to the edges. Each root instance has
its own random generator, seeded unpredictably until `random.seed(n)` makes its sequence repeat. `core.type` returns `null`,
`boolean`, `number`, `string`, `Vector2`, `Vector3`, `Color`, `function` or `object` as a string. Assertions require a boolean.

Write a library as an ordinary Vice file, such as `combat.vice`:

```go
import math

const minimum_damage = 1
var attacks = 0

func calculate_damage(attack, defense) {
    attacks += 1
    return math.max(attack - defense, minimum_damage)
}
```

File-scope functions and class constructors are callable through the namespace; variables/constants can be read through it. External
namespace writes are rejected. Functions update their own module globals normally. Modules have isolated lexical
scopes: a library does not inherit the importing script's variables. Host native functions such as `print` are
shared services, and native libraries such as `spot` are explicitly importable from any module in that instance.
Libraries receive no automatic lifecycle calls; inherited class hooks run on the root component instance.

Each root instance owns its modules. Multiple imports/aliases of the same canonical source share one object and
one initialization within that instance, while another root instance gets independent state. Circular imports
and dependency chains beyond 32 active modules are rejected. Module function calls share the root's execution
budget and nesting limits. Source-module references retained by C++ become invalid when their root instance is
destroyed, so reference cycles stored in module globals do not keep module state alive.

The CLI resolves files relative to the importing file and appends `.vice` when no extension is given. Spot uses
imported Script assets relative to the importer's asset path, with the same extension convention. It loads through
the application's asset manager, including paks, without reading raw sources. Embedded scripts should use a
relative asset-style `sourceName` when importing sibling files. Import strings are paths: moving a library requires updating its importers. The VICE runtime itself performs no filesystem I/O.
`vice/examples/libraries.vice` and `vice/examples/combat.vice` demonstrate both standard and user libraries:

```powershell
./build/msvc/vice/Debug/vice.exe vice/examples/libraries.vice
```

### C++ libraries and custom loaders

Build a native library using public values and callbacks, then bind it before initialization:

```cpp
auto combat = std::make_shared<Vice::Library>();
combat->Constant("minimum_damage", 1.0)
    .Function("calculate_damage", [](std::span<const Vice::Value> args) -> Vice::Value {
        if (args.size() != 2)
            throw std::runtime_error("Expected attack and defense");
        return std::get<double>(args[0]) - std::get<double>(args[1]);
    });
instance.BindLibrary("combat", combat);
```

Duplicate members and empty callbacks are rejected. Finish constructing native libraries before executing scripts.
`Instance::SetModuleLoader` supplies an optional callback taking `(request, importer)` and returning
`std::optional<Vice::ModuleSource>`. Return `nullopt` when missing; return source text plus a nonempty canonical
`sourceName` when found. That name is both the module cache identity and the diagnostic filename, so aliases or
relative paths resolving to the same resource must return the same name. A loader may be called for different
requests resolving to a cached module; parsing and initialization happen only once per canonical resource.
Native libraries are resolved before the source loader. The host defines source resolution policy and can load
from files, memory or assets. The compiler/VM never
receives host file handles or asset-manager types. Native library descriptors, value conversions and the loader
contract do not depend on AST nodes, execution frames or the VM's bytecode layout.

For a game library shared by every Spot language runtime, use `Spot::RegisterScriptLibrary` with an engine
`ScriptObject` factory; see [Scripting integration](scripting.md). Vice source libraries are imported as normal assets.

## Using a component

Put a `.vice` file under the project's `assets/` directory, such as `assets/scripts/hello.vice`. The editor's normal
asset refresh, or `spot cook`, imports it and creates a `.meta` file. Commit the source and its `.meta` together.
The asset browser shows it as a Script asset. Double-click opens it in the editor's dockable **Code** panel.

Use **Create > ViceScript** in the asset browser, or the background context menu, to create a file in the current
asset folder. It starts with a `// filename.vice` comment, a blank line, and `start()` and `update(delta_time)` already
declared with a blank body line indented by four spaces. It is imported and selected, and can be renamed immediately.
Renaming an untouched template updates its filename comment; edited scripts keep their contents. Script tiles use
the same document style and accent as C++ source files, with a `VICE` badge.

Add `ScriptComponent` to an entity and select or drag the asset into its **Script** field. Only Script assets
are accepted. The embedded language selector is hidden when an asset is assigned; embedded source fields stay
serialized but are hidden in the inspector. Scenes save the asset id, so renaming or moving the file through the asset browser
preserves the reference. In code, with the application's asset manager available:

```cpp
auto& component = entity.AddComponent<Spot::ScriptComponent>();
component.script = Spot::Assets::Load<Spot::ScriptSource>("scripts/hello.vice");
```

`ScriptSource` is source text plus its relative path for diagnostics. It loads without a graphics device, supports
the asset manager's caching, asynchronous loading and preloading, and does not depend on the ViceScript runtime.
Importing preserves UTF-8 text and line endings, removing an optional UTF-8 BOM. Parsing happens when the component
starts. Importer version 1 stores a length-prefixed string; it does not compile the script or validate its syntax.

Scripts travel through `spot cook`, `spot export` and `assets.pak` like other assets. Exported games need only their
imported assets, not the original `.vice` or `.meta` files. Diagnostics still show paths such as
`scripts/hello.vice:3:12`, without embedding a developer's absolute filesystem path.

When no asset is assigned, set the embedded language explicitly:

```cpp
auto& script = entity.AddComponent<Spot::ScriptComponent>();
script.language = "vice";
script.sourceName = "hello.vice";
script.source = R"(
    class Hello : Component {
        var frames = 0
        override func OnStart() { print("Hello from ViceScript!", null) }
        override func OnUpdate(deltaTime) {
            frames += 1
            print("Frame:", frames, "Delta:", deltaTime)
        }
    }
)";
```

Set the fields before the component starts. An assigned Script asset takes precedence over embedded `source` and
`sourceName`. A missing or corrupt assigned asset faults the component instead of running the embedded fallback.
For embedded source, `sourceName` labels diagnostics and supplies the base path for module imports;
it never loads the root source file.

File edits are imported through the existing asset refresh. Reimporting invalidates the asset manager's cached
source; the next Play or scene load uses the new source. Scripts running in Play swap in the new code before their
next hook, keeping their state; see [Reloading while playing](#reloading-while-playing). Restarting Play or loading a
scene initializes fresh variables; runtime values are not serialized.

The root source declares a class derived from the host-provided `Component` base, for example
`class PlayerController : Component`. `Component` is available without an import; engine services such as input still
require `import spot`. The adapter constructs one instance per attached `ScriptComponent`, calls its inherited or
overridden lifecycle methods and keeps its fields between frames. Constructors must accept zero arguments.

Only classes declared in the root source participate in component selection. There must be exactly one most-derived
candidate: `BaseController : Component` followed by `PlayerController : BaseController` selects `PlayerController`,
while two unrelated component subclasses are an error. A class can inherit an imported component base with
`class PlayerController : controllers.BaseController`. Other classes and module-level helpers can coexist in the file.
Global functions named `start`/`update` or `OnStart`/`OnUpdate` are not component hooks.

The available optional hooks match the engine lifecycle:

| Hook | Parameters | When it runs |
| --- | --- | --- |
| `OnStart()` | None | Once, before the first enable |
| `OnEnable()` / `OnDisable()` | None | When activity changes |
| `OnFixedUpdate(fixedDeltaTime)` | One number | Each fixed simulation step |
| `OnUpdate(deltaTime)` | One number | Each active frame |
| `OnLateUpdate(deltaTime)` | One number | After normal updates |
| `OnDestroy()` | None | When a started component is removed or its scene unloads |
| `OnCollisionEnter(collision)` / `OnCollisionStay(collision)` / `OnCollisionExit(collision)` | A collision | When a solid body of the entity starts, keeps or stops touching another entity's |
| `OnTriggerEnter(contact)` / `OnTriggerExit(contact)` | A trigger contact | When a trigger and a body start or stop overlapping, on both entities |
| `OnCollisionEnter2D` … `OnTriggerExit2D` | The same, in 2D | The same events of 2D physics |
| `OnReload()` | None | After the script's code was swapped while playing; see [Reloading while playing](#reloading-while-playing) |

A collision has `other` (the other entity), `normal` (pushing this body out), `relativeVelocity`, `impulse`,
`pointCount`, `point` (the first contact point, or `null`) and `GetPoint(index)`; 2D collisions use `Vector2`
values. A trigger contact has `other` and `isTrigger`, true when this entity's trigger is the one overlapped. They
are read-only snapshots; `other` may be invalid in an exit after the other entity was destroyed.

Hook signatures are validated before the root module's globals and the component's field initializers execute. All hooks use the same instance, including
inherited hooks; `super.OnStart()` or `super.OnUpdate(deltaTime)` calls a base implementation. The native base hooks
are no-ops. Disabling and re-enabling preserves fields and does not repeat `OnStart`. Runtime state is discarded at
destruction and is not serialized. `OnDisable` and `OnDestroy` retain the engine's cleanup behavior after faults.

`print` writes Info records through a logger named `ViceScript`. The existing engine sinks carry them to the game
console, editor console and configured log file. Script text is passed as a message, never as a format string.
Parse errors, runtime errors and native-call failures propagate to the existing component fault guard: they are
logged with the entity, hook and script location, and stop that component while other components continue.

Script assets can be imported, edited and loaded as text even when `SPOT_ENABLE_VICESCRIPT` is off; that option
controls the runtime, CLI and built-in VICE language registration. The generic component and bindings remain available.

### Reloading while playing

When a script asset is imported again during Play, as after saving it in the Code panel, each `ScriptComponent`
using it swaps in the new code before its next hook (`ScriptComponent::Reload()`):

- Fields keep their running values when the new code still declares them and the value has the same type as the
  new initializer's, or the new one is `null`: numbers, booleans, strings, vectors, colors and engine references such
  as entities. Arrays, maps, script objects and functions belong to the old code and start over from the new
  initializers, as do new fields and fields whose type changed.
- `OnStart()` does not run again. The script's coroutines and event subscriptions end; the optional `OnReload()`
  hook runs right after, to start them again (`override func OnReload() { StartCoroutine(Patrol) }`).
- When the new code fails to compile or initialize, the warning names the script and the old code keeps running.
- Lua scripts reload too, without keeping state. Exported games never reload: their assets do not change.

## Scene API

Components expose read-only `entity`, `transform` and `scene` properties. `entity.name = "Player"`,
`entity.transform.Translate(offset)`, `transform.position.x += 1` and `scene.FindEntity("Target")` use the normal
engine operations and validation. These members are implicit inside the class; no `self` prefix is needed.
`self.entity` remains available if a local variable named `entity` shadows the property.

The inherited methods `GetEntity()`, `GetScene()`, `GetId()`, `IsEnabled()`, `SetEnabled(value)`,
`IsActiveAndEnabled()`, `IsFaulted()` and `IsValid()` also work without a prefix. `enabled` is writable; `id`, `active`
and `faulted` are read-only properties. All these operations resolve the actual attached engine component by id.

Use `import spot` for `spot.Input`, `spot.Scene`, `spot.Time`, `spot.Physics` and the explicit owning-object exports
`spot.entity` and `spot.Component`.
Aliases work normally (`import spot as engine`). Engine methods keep their C++ PascalCase names; public data fields
use camelCase. Libraries are read-only namespaces, while the objects they return remain mutable.

```go
import spot

class PlayerController : Component {
    var speed = 3
    var player

    override func OnStart() {
        entity.name = "Player"
        player = scene.FindEntity("Player")
        if player != null and player.HasComponent("SpriteRenderer") {
            player.GetComponent("SpriteRenderer").color = Color(1, 0.5, 0.2, 1)
        }
    }

    func Move(deltaTime) {
        entity.transform.Translate(Vector3(speed * deltaTime, 0, 0))
    }

    override func OnUpdate(deltaTime) {
        if spot.Input.IsKeyDown("Right") { Move(deltaTime) }
        if player != null and player.IsValid() {
            player.transform.rotation2D += deltaTime
        }
    }
}
```

| Object | Methods |
| --- | --- |
| Scene | `FindEntity(name)`, `FindEntityWithTag(tag)`; first live match, inactive entities included, or `null` |
| Scene structure | `CreateEntity([name])`, `Instantiate(prefabPath[, parent])` (returns the prefab's root), `DestroyEntity(entity)` |
| Events | `On(name, callback)` (returns a subscription), `Emit(name, arguments...)` (returns how many callbacks ran), on scenes and entities |
| Entity | `GetName()` / `SetName(value)`, `GetId()`, `IsActiveSelf()` / `SetActive(value)`, `IsActiveInHierarchy()`, `GetLayer()` / `SetLayer(value)`, `GetParent()`, `GetTransform()`, `GetScene()`, `IsValid()` |
| Entity discovery | `FindChild(name)`, `HasTag(tag)`, `AddTag(tag)`, `RemoveTag(tag)`, `HasComponent(typeName)`, `GetComponent(typeName, index)`, `GetScript([name])` |
| Entity structure | `CreateChild([name])`, `SetParent(parent or null[, keepWorldPose])`, `AddComponent(typeName)`, `RemoveComponent(component or typeName)`, `Destroy()` |
| Transform | `GetEntity()`, `GetScene()`, `IsValid()`, `Translate(offset)`; `Get`/`Set` pairs for `Position`, `WorldPosition`, `Scale`, `Rotation`, `WorldRotation` and `Rotation2D` |
| Component | `GetEntity()`, `GetScene()`, `GetId()`, `IsEnabled()`, `SetEnabled(value)`, `IsActiveAndEnabled()`, `IsFaulted()`, `IsValid()`, plus registered properties/methods |
| Input | `IsKeyDown(name)`, `IsKeyPressed(name)`, `IsKeyReleased(name)`; physical key names from `GetKeyName`, such as `"Right"`, `"W"`, `"Space"` |
| Input (mouse, gamepads, actions) | `IsMouseButtonDown/Pressed/Released(name)` (`"Left"`, `"Middle"`, `"Right"`, `"X1"`, `"X2"`); `IsGamepadButtonDown/Pressed/Released(name[, gamepad])` (`"South"`, `"Left Shoulder"`, `"D-Pad Up"`…); `GetGamepadAxis(name[, gamepad])` (`"Left X"`, `"Right Trigger"`…); `IsActionDown/Pressed/Released(action)`, `GetActionValue(action)`, `GetActionVector2(action)`; properties `mousePosition`, `mouseDelta`, `scrollDelta`, `gamepadCount`, writable `cursorLocked` |
| Time | Properties `deltaTime`, `unscaledDeltaTime`, `time`, `unscaledTime`, `frameCount`, `fixedDeltaTime` and writable `timeScale`, with `Get` methods and `SetTimeScale(value)` |
| Physics | `Raycast(origin, direction, maxDistance[, ignore, layerMask, includeTriggers])`, `SphereCast(origin, radius, direction, maxDistance[, …])`, and `Raycast2D` / `CircleCast2D` with `Vector2` values |

`GetComponent` accepts a registered type name because Vice has no C++ template arguments. Its optional zero-based index
selects components of exactly that registered type; it defaults to zero. Missing components return `null`.
`entity.Health` remains property shorthand for `entity.GetComponent("Health")`. Discovering a component does not expose
all its C++ members automatically: additional members require explicit descriptors.

Property access uses lower camelCase: entity `name`, `active`, `layer`, `id`, `activeInHierarchy`, `parent`, `transform`,
`scene`; transform `position`, `worldPosition`, `scale`, `rotation`, `worldRotation`, `rotation2D`, `entity`, `scene`;
component `enabled`, `id`, `active`, `faulted`, `entity`, `transform`, `scene`. `parent`, `entity`, `transform` and `scene`
are read-only references. Existing `Parent`, `Transform` and `Entity` aliases and the C++-style getters remain supported.
Method getters return vector/color copies; nested property writes such as `entity.transform.position.x += 1` write
through their setters. A local copy (`var position = transform.position`) is independent until assigned back.
Rotations use Euler `Vector3` values in radians, or a number for `Rotation2D`; this is the script adaptation of C++ quaternions.

References keep entity/component ids, not pointers or numeric script ids. Exposed ids are strings, preserving all
bits. Access resolves against the script owner's current scene. Removed components and destroyed or moved-away
entities invalidate old references; adding another component of the same type never retargets one. Use `IsValid()`
before accessing a cached reference. An invalid access reports the script location and faults only that script.
The executing component can still access its own entity/component during removal cleanup, while `IsValid()` reports
false for a removed object. This access ends with the hook. Wrappers for the same live object within an instance compare equal. A script moved to another scene resolves `spot.entity`
and `spot.Scene` there on its next hook; references to objects left behind become invalid. Scripts do not keep a scene
pointer between hooks, and object wrappers do not keep entities or components alive.

Input queries use the engine's existing input state, including capture and per-frame transitions. Valid keys report
false without an input state; unknown names and incorrect argument types/counts produce errors.

The initial built-in descriptors are:

| Component | Writable properties | Methods |
| --- | --- | --- |
| `SpriteRenderer` | `color`, `size`, `frame`, `flippedX`, `flippedY`, `order` | `Get`/`Set` pairs for `Color`, `Size`, `Frame`, `Order`; `IsFlippedX()` / `SetFlippedX(value)`, `IsFlippedY()` / `SetFlippedY(value)` |
| `Camera` | `projection`, `fieldOfView`, `orthographicSize`, `nearPlane`, `farPlane`, `background` | `Get`/`Set` pairs for the same properties, e.g. `GetFieldOfView()` / `SetFieldOfView(value)` |
| `AudioSource` | `volume`, `pitch`, `loop`, `spatial`, `playOnStart`, `bus` | `Play()`, `Pause()`, `Stop()`, `IsPlaying()`, `IsPaused()`; read-only `playing`, `paused` |

Projection values remain `"Perspective"` / `"Orthographic"`. Method setters use the same validation as property writes.

Transforms and built-in component properties use public engine operations. Invalid layers, non-finite/out-of-range
numeric conversions, fractional integer fields, negative sprite sizes, non-positive pitch, invalid projections and
invalid camera ranges are rejected. Custom setters define their own domain constraints. Vector/color member writes
go back through their enclosing setter. Values read into a script variable are independent copies.

Structural changes use the scene's normal operations: destroyed entities and removed components leave queries at
once and are cleaned up as the scene does, so a script may destroy its own entity. `AddComponent` takes a registered
type name; `RemoveComponent` returns whether a component was removed. `SetParent` rejects making an entity a
descendant of itself. `Instantiate` loads the prefab asset by path through the application's asset manager and fails
when it cannot. Gamepad indices are zero-based; without one, queries look at every gamepad. Input names are the
engine's own (`GetMouseButtonName`, `GetGamepadButtonName`, `GetGamepadAxisName`); unknown names are errors.

A physics query returns `null` when nothing is hit, or a hit with `entity`, `collider` (its component reference),
`point`, `normal` and `distance`. `ignore` is an entity whose bodies the query passes through, or `null`.

### Events

Scripts talk to each other through named events. `scene.On(name, callback)` subscribes to events sent to the whole
scene with `scene.Emit(name, ...)`; `entity.On(name, callback)` to those sent to that entity with `entity.Emit(name, ...)`.
C++ sends the same events with `Spot::EmitScriptEvent(scene or entity, name, arguments)`.

```go
import spot

class ScoreBoard : Component {
    var total = 0
    override func OnStart() {
        scene.On("Scored", func(points, player) { total += points })
        entity.On("Reset", Reset)
    }
    func Reset() { total = 0 }
}

class Coin : Component {
    override func OnTriggerEnter(contact) {
        scene.Emit("Scored", 10, contact.other)
        entity.Destroy()
    }
}
```

- Callbacks run synchronously, in subscription order, with the emitted arguments; `Emit` returns how many ran.
  Subscriptions added or cancelled during an emission take effect for later ones. Emissions nest up to 16 deep.
- A subscription belongs to the component whose script subscribed and ends when it is destroyed. While that
  component is disabled or faulted its callbacks are skipped. `subscription.Cancel()` ends it early and returns
  whether it was active; `subscription.active` tells whether it still exists.
- A callback runs as its subscriber, in its scene scope: an error faults the subscriber and is logged with the
  `event callback` hook, while the emitter carries on.
- Any language's functions can subscribe; arguments are engine values, entity and component references, or another
  script's objects and functions.

### Coroutines in components

`StartCoroutine(function)` runs a function as a coroutine of the component, after each frame's updates. What it
yields decides when it continues:

| Yield | Continues |
| --- | --- |
| `yield` (null) | Next frame |
| `yield WaitFrames(n)` | After `n` frames |
| `yield WaitSeconds(seconds)` | Once that much scaled time has passed |
| `yield WaitUntil(function)` | On the first frame the function returns `true` |

```go
class Door : Component {
    export var open = false
    override func OnStart() {
        StartCoroutine(func() {
            yield WaitUntil(func() { return open })
            for step in range(30) {
                transform.position.y += 0.1
                yield
            }
        })
    }
}
```

The coroutine runs at once until its first `yield`. `StartCoroutine` returns an id for `StopCoroutine(id)` (which
returns whether it was running) and `IsCoroutineRunning(id)`; `StopCoroutines()` stops them all. They are the
engine's component coroutines: paused while the component is inactive, ended when it is removed, and an error or an
invalid yield (any value other than those above) faults the component under the `Coroutine` hook. These methods
belong to every script component reference. Lua scripts use them too, yielding with `coroutine.yield(...)`; in C++,
`ScriptComponent::StartScriptCoroutine(function)` does the same.

### Calling other scripts

A reference to another entity's `ScriptComponent` reads its script's fields and calls its methods:
`enemy.GetComponent("ScriptComponent").Damage(10)`. `entity.GetScript()` returns the first started script object of
an entity, and `entity.GetScript("health")` the first one whose source file is named so (`scripts/health.vice`);
both return `null` when there is none. The call runs in the callee's scene scope, so its own `entity` and `scene`
work, and its errors propagate to the caller as from any method, without faulting the callee. A script's own
component reference does not forward to itself. In C++, `ScriptComponent::GetScriptObject()` returns the same object.

### Exported fields

`export var` declares a field the inspector shows and the scene saves for each component:

```go
class Mover : Component {
    export var speed = 3
    export var label = "crate"
    export var offset = Vector3(0, 1, 0)
    export var tint = Color(1, 0.5, 0, 1)
    export var loop = true
}
```

The initializer must be a constant: a boolean, number (optionally negated), string, or `Vector2`, `Vector3` or `Color`
of numbers, which tools read without running the script. Only class `var` fields can be exported. The inspector
lists the exported fields of the classes the source declares, under **Fields**. Edited values are stored in
`ScriptComponent::fieldValues` (absent while at their default), saved with the scene, and set on the script's
object after its initializers and constructor, before `OnStart`. Fields a script no longer exports are dropped when
the scene is loaded again; a value whose type no longer matches the default goes back to the default. A source that
does not parse keeps showing the fields it last had, so unfinished edits are not lost.

## Exposing game components

The generic descriptor API is `scene/scripting/script_bindings.h`, included by `<spot.h>` regardless of whether VICE is enabled.
It uses engine-native `ScriptValue`, not ViceScript types, AST nodes or execution frames. The VM only sees
the generic host-object protocol. `ScriptComponent` selects the VICE runtime through the language registry;
scene bindings, built-in descriptors and lifecycle management do not depend on VICE. To expose a game's `Health`
component with an integer `value` field and a `Damage(int)` method:

```cpp
Spot::RegisterComponent<Health>("Health");
Spot::ScriptBindings<Health>()
    .Field("value", &Health::value)
    .Method("Damage", &Health::Damage);
```

```go
import spot

class DamagePlayer : Component {
    override func OnStart() {
        var player = self.GetScene().FindEntity("Player")
        if player != null and player.GetComponent("Health") != null {
            var health = player.GetComponent("Health")
            health.value -= 10
            health.Damage(5)
        }
    }
}
```

The code editor suggests a component's described members, with the types the descriptors record: `color` of a
`SpriteRenderer` reads as a `Color`, so `sprite.color.` suggests `r`, `g`, `b` and `a`. Methods registered with
`Detail::SetScriptMethod()` directly give their return type as its last argument.

Use `.Property("name", getter, setter)` for validated or system-dependent properties and `.ReadOnly("name", getter)`
for read-only values. Getters accept `const T&`; setters accept `T&` and the getter's value type. Member function
pointers work directly, as do lambdas. `.Field` directly accesses a simple public data member; it does not invoke
serialization or `OnValidate`. `.Method` accepts const or non-const member functions, checks argument count/types,
and converts a supported return value (or returns `null` for `void`). Overloads need an explicit C++ cast.

Supported descriptor values are booleans, numeric C++ scalars, `std::string`, `Vector2`, `Vector3` and `Color`.
`ScriptObjectRef` can pass an explicitly exposed host reference. Integers must fit both their C++ type and the exact script integer range (±(2^53−1)). Enum/asset
arguments, collections and custom struct values need an explicit adapter and are not automatically exposed.
Properties and methods must be valid identifiers. Match C++ names: PascalCase methods and camelCase fields.
Base component properties and methods (such as `GetEntity`, `GetScene`, `SetEnabled`, `id` and `enabled`) are reserved. A property and a method cannot share a name. Descriptors use exact concrete C++ types;
derived types register their own API.

Register components and descriptors before running scenes. Default engine descriptors are installed before custom
descriptors, so later registrations can replace individual members. `UnregisterComponent()` discards that type's
descriptors before a game module unloads; re-registering a component also clears its old descriptors. Register them
again with the new code. Script wrappers retain no descriptor callbacks between calls. This explicit exposure keeps
runtime setters separate from serialization while letting new game components use the same bridge as engine types.

## Code editor

Open a `.vice` asset from the asset browser, or show the panel through **Window > Code**. Each open script has a
tab, an independent text undo history, line numbers and a monospaced font. Keywords (including `override`, `export`, `is`, `yield`, `enum`, `match`, `while`, `for`, `in`, `break`, `continue`, `class`, `extends`, `self`, `super`, `import`, `as`, `if` and `else`),
numbers, strings, comments, constructors, component names, standard library names, `spot`, implicit component members
(`entity`, `transform`, `scene`, `enabled`) and function calls have syntax highlighting.
Operators and punctuation are colored too. Interpolated expressions are colored as part of their string. Highlighting works without parsing or running the program, so unfinished
source remains editable. The editor and lexer share the language vocabulary, including builds with scripting disabled.

While Code has focus, **Ctrl+S** saves its active script and **Ctrl+W** closes its tab. **Ctrl+Z**, **Ctrl+Y** or
**Ctrl+Shift+Z**, **Ctrl+A/C/X/V** operate on its text instead of scene entities. **Tab** inserts spaces or indents
selected lines, **Shift+Tab** removes indentation, and **Enter** preserves indentation. New indentation uses four
spaces. The same actions are available in the File/Edit menus; **Ctrl+Alt+S** saves open scenes and scripts.
Hold **Ctrl** and scroll over the source text to increase or decrease its font size, from 10 to 40 points before
display scaling. This size is shared by script tabs during the editor session; toolbars keep their normal size.

Saving writes the source file and refreshes the project's existing asset pipeline. New Play sessions use the saved
source, and scripts already running in Play reload it. An asterisk marks unsaved text. Closing a tab, quitting, or
switching projects asks whether to save, discard, or cancel. Hiding the Code panel keeps its documents in memory.

Open tabs follow asset ids when files move or are renamed. Clean documents reload on asset refresh; externally
changed files with local edits must be copied or explicitly reloaded before they can be saved. Deleted files keep
their buffers available to copy. Reloading local edits also asks for confirmation. Save failures keep buffers and
confirmation dialogs open. UTF-8 BOM, existing tabs, LF/CRLF convention and the presence of the final newline are
preserved. Mixed line endings are normalized to CRLF when any CRLF is present.

The editor uses [ImGuiColorTextEdit by Johan A. Goossens](https://github.com/goossens/ImGuiColorTextEdit) (MIT), fetched
at a pinned commit alongside ImGui. The code panel and tolerant language definition live in `editor/panels/` and
`editor/code/`; neither the engine runtime nor `vice/` links this widget. The embedded monospaced font comes from
ImGui. Script tabs are not restored across editor runs.

### Suggestions

Typing a dot after something the editor knows the type of lists its members, narrowed as the name is typed and
inserted with **Enter** or **Tab**; **Ctrl+Space** lists them again. `entity.` suggests `transform`, `name`,
`GetComponent` and the entity's component shorthands; `entity.transform.position.` suggests `x`, `y` and `z`.
The receiver is a chain of names and calls starting at what a component script knows: its implicit members
(`entity`, `transform`, `scene` and the inherited methods), `self` (the class's own fields and methods besides),
the `spot` module under the name it was imported as, a hook's parameter (`collision` in `OnCollisionEnter`), or a
value constructor. Calls go through their return types, and `GetComponent("Camera")` or `entity.Camera` leads to that
component's members. Local variables are not followed, so `var player = scene.FindEntity("P")` then `player.` suggests
nothing, nor do Lua scripts. Like highlighting, it reads the text without parsing it, so unfinished code works.

What is suggested is what runs: the members of the engine's objects are described once, in `ScriptApi::Types`
(`engine/scene/scripting/script_api.h`), with their types, and the scene bindings answer `HasProperty`/`HasMethod`
and route getters and setters from that same table; a test checks every member it describes is one they dispatch.
Components add the members their script bindings describe (`GetComponentScriptMembers()`), with the value types the
bindings record. The suggestions themselves are `GetViceMemberCompletions()` (`editor/code/vice_completion.h`). The
text widget only opens suggestions as a word is typed, so the editor's build patches it to open them on a dot too
(`editor/cmake/patch_text_editor.cmake`).

### Diagnostics

The Code panel parses the source shortly after typing stops, without running it (`CheckScriptSyntax()`, which
languages provide with `SetScriptSyntaxChecker()`; Vice and Lua do). The first syntax error shows next to the cursor
position below the source, and its line is marked, with the message as tooltip. Unfinished code stays editable and
savable.

### Debugging

**Breakpoint** (or **F9**) toggles a breakpoint at the cursor's line; breakpoints are marked in the gutter and apply
to every script running from that file (`SetScriptBreakpoints()`). When a running script reaches one, the game pauses
at the end of that frame and the Code panel shows where it stopped, with the call stack and each call's locals,
including `self`, as they were then. **Continue** resumes the game; **Next Frame** runs one more frame, stopping again
at the next breakpoint reached.

The engine never blocks a frame, so the script itself finishes its hook: the panel shows the state at the moment of
the stop, and stepping line by line is not available in the editor. Only the first stop is kept until Continue. The
standalone CLI has a stepping debugger, `vice --debug script.vice [update-count]`, which pauses execution with
`break <line>`, `delete <line>`, `run`, `continue`, `step`, `next`, `out`, `locals`, `stack` and `quit`. Both use
`Instance::SetDebugHandler()` and `SetBreakpoints()`: the handler receives the stack with locals and returns whether to
continue or step in, over or out, and may block while the user decides.

### Profiling

The engine's profiler times every script hook as a `<script>.<hook>` scope, such as `player.OnUpdate`, in the
Profiler panel's timeline and averages. Checking **Script functions** there also profiles each function of the
scripts running in the editor: own time (without the script functions it called, with the native work it did),
calls, VM instructions and the script objects and collections it allocated, added up across components since
profiling started (`SetScriptProfiling()`, `GetScriptProfile()`). It costs a clock read per instruction, so it is
off by default. In C++, `Instance::SetProfiling()` and `GetProfile()` give the same figures for one instance; time
spent suspended in a coroutine is not counted.

## Embedding without Spot

`Vice::Program::Parse(source, sourceName)` parses and compiles immutable bytecode. Several `Vice::Instance` objects may share
it while maintaining separate variables. An instance retains the program even if the original `Program` is gone.
Bind native functions with `Instance::Bind()` before `Initialize()`, then invoke script functions with `Call()`.
Inject host globals with `Instance::BindValue(name, value, constant)`, also before initialization. They are const
by default; passing `false` permits script rebinding. Both binding methods reject conflicts with script declarations,
other bindings and built-in constructors.

`Instance::BindClass(name, object, methodSignatures)` registers a host-backed base class visible in the root and its
source modules. Unknown instance fields/methods delegate to the supplied object. Optional signature constraints validate
script overrides before initializers, and native calls check their argument counts. `Object::HasMethod` and `Object::HasProperty` describe native members without invoking getters. Implement both
for host-backed class members that should participate in implicit lookup; explicit member access still uses `Get`/`Call`. After `Initialize`, `CreateDerivedInstance(baseName)` constructs the sole most-derived subclass
declared in the root source, using a zero-argument constructor. Spot uses this protocol for `Component`; Vice itself
has no dependency on engine components or scenes.

Derive from `Vice::Object` to supply properties through `Get`/`Set` and methods through `Call`, then inject an
`ObjectRef` (a shared pointer). The default implementations reject unknown members and writes. Methods receive
evaluated `Value` arguments and return a `Value`; no syntax nodes or interpreter internals enter callbacks. For
example, an embedding can expose a validated `position` property returning `Vice::Vector3`; scripts can then write
`actor.position.x += 1` and the runtime calls the object's position setter with the updated vector. Callback failures
become script diagnostics at the member access or call site. Implementations should use weak references or handles
for back references: the cycle collector does not look inside host objects. An object wrapper owns no engine object unless
its implementation explicitly chooses to do so, and must validate external handles on each operation.

The runtime does not provide logging, file access or engine objects; those capabilities belong to the host.
Instances are single-threaded, and host callbacks cannot reenter the same instance.

## Bytecode compiler and stack VM

`Program::Parse` runs the lexer, parser and `compiler.cpp` once. The compiler resolves parameters and lexical
locals to indexed slots and lowers expressions, branches and short circuit logic into stack instructions.
`bytecode.h` holds immutable instructions, constant/name pools, local metadata and a separate source location
table. Parsed expression trees and function bodies are released after compilation; instances share compiled code.
Bytecode is an internal in-memory representation, with no stable file format or loader for external bytecode.
Imported source modules go through the same compiler.

`vm.cpp` executes a switch-based dispatch loop with contiguous value storage, explicit call frames and an
assignment target stack. Free functions, virtual methods, module calls, constructors and field initializers all
schedule VM frames rather than recursively evaluating script on the C++ call stack. Constructors initialize
base fields before derived fields and then invoke the selected `init`, retaining their object's identity across
those continuations. Slots are cleared when their lexical block ends; calls and failures release temporaries.
`MakeClosure`, `LoadUpvalue` and `UpvalueTarget` create closures and access their captures; `CallValue` calls any
callable value.

Each root instance owns its stacks, module session and instruction budget; its source modules share them.
Stack capacities and argument scratch storage are reused across updates. Parameters and locals need no runtime
hash table; free-function and declared-method lookup use prebuilt lookup tables. Globals, implicit instance members and native
properties retain dynamic name lookup, preserving host bindings and override behavior. There is no JIT, inline
member cache or tracing garbage collector; cycles are released by the trial-deletion collector described above.
These are separate optimization decisions that should follow profiling.

Runtime operations remain in `runtime.h`/`runtime.cpp`, independent of the execution strategy. Property writes
evaluate their receivers once, before their right side; copied structs propagate replacements through their
setters, while reference writes stop at the object. Source locations remain attached to failing operations,
including native callbacks. A failed call unwinds its frames, operands and assignment targets so another call
can run. `Instance::Call` cannot reenter a running instance. Native callbacks may synchronously call retained
script objects through `Object::Call`, as component enable/disable hooks do. These calls share the existing
instruction budget and VM stacks, with at most 16 nested native-to-script callbacks to protect the C++ stack.
Native argument buffers remain stable while those callbacks grow the VM stacks.

Execution allows at most 1,024 VM frames and 1,048,576 value slots, with a 100,000-step budget per host invocation.
VM instructions and dispatch/initialization work consume that budget; one bytecode instruction is not equivalent
to one old tree-interpreter step. Import dependency depth and inheritance depth remain limited to 32.
Syntax nesting limits are unchanged. Long property chains within the parser's limits execute iteratively.

### Measuring update costs

Build the optional standalone benchmark in Release, without configuring Spot:

```sh
cmake -S vice -B build/vice-vm -DVICE_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/vice-vm --config Release --target vice_benchmarks
```

Run `build/vice-vm/Release/vice_benchmarks` with a multi-config generator, or `build/vice-vm/vice_benchmarks`
with a single-config generator. An optional positive argument selects iterations per sample (100,000 by default).
The benchmark reports the median of five warmed-up samples for local arithmetic/branching, script calls,
component-style methods, native property writeback, and array/map/range iteration. Parsing and initialization are excluded. The last workload
updates 1,000 independent instances sharing one program, so its 1,000-update cost includes traversing separate
instance state. Other rows project 1,000 updates of the same warmed-up instance. None includes engine scene
scheduling, physics, rendering or a complete game frame. Use actual scene profiling before setting a game's script budget.

`tests/vice_tests.cpp` and `tests/vice_library_tests.cpp` test the runtime, standard libraries, module loading,
identity, lifetime, diagnostics and execution limits independently of Spot. `tests/vice_component_tests.cpp` exercises real scenes,
logs, failures, activation, independent state, serialization and execution from a shipped pak without a window or
GPU. `tests/script_asset_tests.cpp` covers importing, metadata, moving, caching, corrupt data and loading scripts
without the runtime.

## Implementation stages

The next essential stage is the first unchecked item. Complete each stage with runtime tests, editor vocabulary
and template updates where applicable, standalone examples and documentation.

- [x] Stack VM with indexed locals, reusable stacks and bounded execution.
- [x] Explicit `override func`, including native component hooks and signature validation.
- [x] `while`, `break` and `continue`, with lexical cleanup and bounded backward jumps.
- [x] Reference arrays, indexing, mutation and nested struct writeback.
- [x] `for` over arrays and lazy numeric ranges.
- [x] String-keyed maps, deterministic iteration and structural-mutation checks.
- [x] Functions as values and bound methods for callbacks.
- [x] Anonymous functions and lexical closures.
- [x] Collection of unreachable object, collection and closure cycles during execution.
- [x] Engine event subscriptions with automatic component-lifetime cleanup.
- [x] Physics hooks, structural scene changes, time, mouse, gamepad and action input, and physics queries.
- [x] Calls between scripts and inspector-editable `export var` fields.
- [x] Modulo, vector and color arithmetic, string interpolation, `is`, line continuation after operators, and the
  `random` library with more `math`, `string` and `core` functions.
- [x] Script stack traces.
- [x] Profiling for time, instructions and allocations.
- [x] Coroutines with suspension, resumption and engine scheduling.
- [x] Enums and `match`.
- [x] Syntax diagnostics while editing.
- [x] Reloading scripts while playing, keeping their state.
- [x] Breakpoints with call stacks and locals in the editor, and a stepping debugger in the CLI.

Later stages could include completion and navigation in the editor, stepping in the editor, enum fields in the
inspector and versioned cooked bytecode. The current bytecode stays internal and in memory.
