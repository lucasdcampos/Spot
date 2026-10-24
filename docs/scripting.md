# Scripting

Spot's scripting language is **[Luau](https://luau.org)**, a sandboxed and typed descendant of Lua. Scripts are
`.luau` assets run by `ScriptComponent`s, one isolated Luau state per component (`SPOT_SCRIPTING=ON` and `SPOT_ENABLE_LUAU=ON` by default,
`scene/scripting/luau_runtime.cpp`). The engine side does not depend on Luau: it is one scene API for any language,
starting at `scene/scripting/script_runtime.h` for a language's adapter and `scene/scripting/script_component.h` for the
component's lifecycle.

```lua
-- scripts/player.luau
local spot = require("@spot")
local combat = require("./combat")

speed = 4 -- a global with a constant value: shown and saved in the inspector
local hits = 0 -- a local: private to the script

function OnStart()
    spot.entity.name = "Player"
end

function OnUpdate(deltaTime: number)
    if spot.Input:IsKeyDown("Right") then
        spot.entity.transform.position.x += deltaTime * speed
    end
end

function OnCollisionEnter(collision)
    hits += 1
    combat.Hit(collision.other, hits)
end
```

## Optional support

`SPOT_SCRIPTING` controls the complete scene scripting module. With it off, no scripting components, bindings,
runtimes or `ScriptingModule` are compiled, and Luau is neither fetched nor linked, even if `SPOT_ENABLE_LUAU`
remains on. The text-only `ScriptSource` asset format and its loader remain usable independently of a runtime.
With scripting on, `SPOT_ENABLE_LUAU=OFF` still supports custom language adapters without Luau.

`<spot.h>` includes no scripting headers. `<spot/scripting.h>` includes the public ones, or include only the API you use, such as
`<spot/scene/scripting/script_component.h>`, `<spot/scene/scripting/script_bindings.h>` or
`<spot/scene/scripting/scripting_module.h>`. Applications add `ScriptingModule` explicitly; standalone scenes call
`RegisterScriptComponents()` from `scripting_components.h`, and register script assets when needed. Both static and
shared consumers inherit `SPOT_SCRIPTING=1` only in enabled builds; exported `Spot::Shared` packages carry the same
feature definitions.

The bundled editor, player, sandbox and integration tests require scripting support and build with it on. To build
only the engine without scripting:

```powershell
cmake --preset msvc -DSPOT_SCRIPTING=OFF -DSPOT_BUILD_EDITOR=OFF -DSPOT_BUILD_SANDBOX=OFF -DSPOT_BUILD_TESTS=OFF
cmake --build --preset msvc-debug --target spot spot_shared
```

The standalone consumer test deliberately requests Luau while disabling scripting, verifies that no Luau targets
exist and no scripting definitions reach its translation unit, then updates a native C++ scene through both static and shared libraries:

```powershell
cmake -S tests/cmake/scripting_disabled -B build/scripting-disabled -G "Visual Studio 18 2026" -A x64
cmake --build build/scripting-disabled --config Debug --target spot_no_scripting_smoke spot_no_scripting_shared_smoke
ctest --test-dir build/scripting-disabled -C Debug --output-on-failure
```

The MSVC Debug enabled build and all 945 headless cases passed (the long falling/respawn case ran separately with
one worker). Both static and shared scripting-disabled consumers built and passed their native-scene tests.

## Scripts and components

```cpp
#include <spot/scene/scripting/script_component.h>

auto& script = entity.AddComponent<Spot::ScriptComponent>();
script.script = Spot::Assets::Load<Spot::ScriptSource>("scripts/player.luau");

// Without an asset:
auto& embedded = entity.AddComponent<Spot::ScriptComponent>();
embedded.language = "luau";
embedded.source = "function OnStart() print(123) end";
```

`ScriptComponent` loads the script asset (or the embedded `source`, whose `language` names its runtime; assets take
precedence), runs its top level once, then calls the hooks it defines: `OnStart`, `OnEnable`, `OnDisable`,
`OnFixedUpdate`, `OnUpdate`, `OnLateUpdate`, `OnDestroy`, `OnReload` and the physics contact hooks
(`OnCollisionEnter`/`Stay`/`Exit`, `OnTriggerEnter`/`Exit` and their `2D` forms), as `ScriptApi::Hooks` lists them with
their parameter counts, which are checked before anything runs. Disabled components and faulted ones run nothing: a
script error logs its location once and faults only its component. Runtime state is discarded on destruction and is
not serialized. Imported scripts choose their runtime from their extension, including assets loaded from a pak; an
unknown language or a missing asset faults the component, and another runtime is never used instead.

## Luau

- Luau is fetched at a pinned release; only its parser, compiler and VM are built. Sources are compiled to bytecode
  when a component loads them, and the parser checks syntax for the editor without running anything.
- `require` follows Luau's require-by-string: an alias names an engine library (`require("@spot")`, or
  `require("@combat")` for one a game registers with `RegisterScriptLibrary`), and a path starting with `./` or `../`
  names a module of the project, relative to the requiring script, with `.luau` added when it has no extension. Modules
  load from imported `ScriptSource` assets, including shipped `.pak` files, and cannot leave the asset root; each gets
  an environment of its own over the built-ins, and what it returns (or, when it returns nothing, its globals, read
  only) is what `require` gives. A module that needs the scene requires `@spot` itself.
- Engine objects support both dot (`spot.Scene.FindEntity("Player")`) and colon (`spot.Scene:FindEntity("Player")`)
  calls; colon calls go through `__namecall`, without making a function per call.
- Value types (`Vector2`, `Vector3`, `Color`) are global constructors; mutating a field of one read from an engine
  property (`spot.entity.transform.position.x = 5`, `sprite.color.a = 0.5`) writes it back through the property. A
  local copy is independent until assigned back.
- Luau's `base`, `coroutine`, `table`, `string`, `math`, `utf8`, `bit32`, `buffer` and `vector` libraries are opened;
  `os` and `debug` are not. The built-ins are frozen with `luaL_sandbox`: their tables are read only and scripts
  cannot assign their names, so Luau's fast built-in calls stay valid. Reading a name that is neither a built-in nor
  declared faults the script with its location, instead of giving `nil`.
- Luau's interrupt callback bounds each hook: a runaway loop faults its component instead of hanging the frame.
- `print` writes to the engine's log, under "Luau".

A script's globals are its object (`GetScriptObject()`, other scripts' `GetScript()`): functions are its methods,
other values its fields. The tools build on that:

- **Exports.** Top-level assignments of constant values to globals (numbers, optionally negated, strings, booleans,
  and `Vector2`, `Vector3` or `Color` built from numbers) are the script's exported fields, read from the syntax tree
  without running it. The inspector shows them under **Fields**; edited values are kept in
  `ScriptComponent::fieldValues` (absent while at their default), saved with the scene, and assigned after the
  script's top level runs, before `OnStart`. Fields a script no longer exports are dropped, and a source that does not
  parse keeps the fields it last had. Use `local` for state that is not a field.
- **Reloading.** When a script asset is imported again during Play, each component running it swaps in the new code
  before its next hook (`ScriptComponent::Reload()`). Globals of the old code that hold values (numbers, strings,
  booleans, value types and engine references) carry over when the new code still declares them with a value of the
  same type or nil; locals, tables and functions start over. `OnStart()` does not run again; coroutines and event
  subscriptions end, and `OnReload()` runs to start them again. New code that fails to compile or initialize leaves
  the old code running, with a warning. Exported games never reload: their assets do not change.
- **Breakpoints.** `lua_breakpoint` patches the first instruction of the line in each function, or of the next line
  with code; the `debugbreak` callback reports the stack with each frame's locals, formatted without running script
  code, and lets the frame continue. Scripts are compiled at optimization level 1 and debug level 2 for that.
- **Profiling.** While on, the interrupt callback samples at Luau's safepoints (calls, returns and loop iterations):
  the time since the last one goes to the function running, and a deeper frame counts a call. `instructions` counts
  those safepoints, and `allocations` the collectable objects (tables, strings, closures, userdata) made while the
  function was innermost, including those the engine made for it.

### Writing scripts in other editors

Luau scripts can be written in any editor that runs Luau's language server,
[luau-lsp](https://github.com/JohnnyMorganz/luau-lsp) (VS Code, Zed, Neovim and others). The tools write the engine's
API into the project's generated files, from `ScriptApi`, whenever the editor opens a project or `spot cook` runs
(`tools/project/luau_tooling.h`):

- `.spot/luau/spot.luau` is the module `require("@spot")` resolves to. It does nothing, but its type describes every
  engine object, as structural types (`spot.Entity`, `spot.Vector3`...), so a project's `.luaurc` aliasing `spot` to it
  is all the language server needs to complete and check engine calls.
- `.spot/luau/spot.d.luau` declares the global constructors, in Luau's definition file syntax. Load it as the editor's
  luau-lsp settings say: `"luau-lsp.types.definitionFiles": {"spot": ".spot/luau/spot.d.luau"}` in VS Code, or
  `--definitions=@spot=.spot/luau/spot.d.luau` for luau-lsp's command line. Set the platform to `standard`.

`WriteLuauTooling()` adds the alias to the project's `.luaurc`, or writes one that also turns off the
`FunctionUnused` lint: a script's global functions are what the engine and other scripts call. The editor does it
when opening a project or a file in the external editor (see [the editor](editor.md#scripts)).

## Scene API

`require("@spot")` gives `Input`, `Scene`, `Time`, `Physics`, the script's own `entity`, and `Component`, its own
component. Engine methods keep their C++ PascalCase names; data fields use camelCase. Every member is described once,
with its type, in `ScriptApi::Types` (`engine/spot/scene/scripting/script_api.h`), which the scene bindings dispatch through
and the Luau types are written from; a test checks every member it describes is one the bindings answer to.

| Object | Members |
| --- | --- |
| Scene | `FindEntity(name)`, `FindEntityWithTag(tag)` (first live match, inactive ones included, or nil), `CreateEntity([name])`, `Instantiate(prefabPath[, parent])` (the prefab's root), `DestroyEntity(entity)`, `On`, `Emit` |
| Entity | `name`, `active`, `layer` (writable), `id`, `activeInHierarchy`, `parent`, `transform`, `scene`; `FindChild(name)`, `HasTag`/`AddTag`/`RemoveTag(tag)`, `HasComponent(type)`, `GetComponent(type[, index])`, `GetScript([name])`, `CreateChild([name])`, `SetParent(parent or nil[, keepWorldPose])`, `AddComponent(type)`, `RemoveComponent(component or type)`, `Destroy()`, `IsValid()`, `On`, `Emit`, and C++-style getters and setters |
| Transform | `position`, `worldPosition`, `scale`, `rotation`, `worldRotation` (Euler angles in radians), `rotation2D` (writable), `entity`, `scene`; `Translate(offset)`, `IsValid()` |
| Component | `enabled` (writable), `id`, `active`, `faulted`, `entity`, `transform`, `scene`; `IsValid()`, coroutines, plus what its type's script bindings describe |
| Input | `IsKeyDown/Pressed/Released(key)` with the engine's key names (`"Right"`, `"W"`, `"Space"`); `IsMouseButtonDown/Pressed/Released(button)`; `IsGamepadButtonDown/Pressed/Released(button[, gamepad])`, `GetGamepadAxis(axis[, gamepad])`; `IsActionDown/Pressed/Released(action)`, `GetActionValue(action)`, `GetActionVector2(action)`; `mousePosition`, `mouseDelta`, `scrollDelta`, `gamepadCount`, writable `cursorLocked` |
| Time | `deltaTime`, `unscaledDeltaTime`, `time`, `unscaledTime`, `frameCount`, `fixedDeltaTime`, writable `timeScale` |
| Physics | `Raycast(origin, direction, maxDistance[, ignore, layerMask, includeTriggers])`, `SphereCast(origin, radius, direction, maxDistance[, ...])`, and `Raycast2D`/`CircleCast2D` with `Vector2`s; nil, or a hit with `entity`, `collider`, `point`, `normal` and `distance` |

`entity.Health` is shorthand for `entity:GetComponent("Health")`; missing components are nil. The built-in
components script bindings describe are `SpriteRenderer` (`color`, `size`, `frame`, `flippedX`, `flippedY`, `order`),
`Camera` (`projection`, `"Perspective"` or `"Orthographic"`, `fieldOfView`, `orthographicSize`, `nearPlane`,
`farPlane`, `background`) and `AudioSource` (`volume`, `pitch`, `loop`, `spatial`, `playOnStart`, `bus`, `Play()`,
`Pause()`, `Stop()`, `playing`, `paused`). Writes go through the engine's own operations and validation: invalid
layers, out-of-range numbers, fractional integers, negative sizes and invalid camera ranges are errors, and an object
that rejects a value is left as it was.

References keep entity and component ids, never pointers: access resolves against the script's current scene, during
a hook only, and wrappers do not keep anything alive. Removed components and destroyed or moved-away entities make
old references invalid (adding another component of the same type never retargets one), so check `IsValid()` before
using a cached reference; using an invalid one faults the script with its location. A component can still reach its
own entity during its removal cleanup. Structural changes use the scene's normal operations, so a script may destroy
its own entity. Activity hooks triggered by `SetEnabled` may nest, sharing execution limits; other reentry from the
host is rejected.

### Events

Scripts talk to each other through named events: `spot.Scene:On(name, callback)` subscribes to those sent to the whole
scene with `spot.Scene:Emit(name, ...)`, and `entity:On(name, callback)` to those sent to that entity. C++ sends the
same events with `Spot::EmitScriptEvent(scene or entity, name, arguments)`.

```lua
local spot = require("@spot")

total = 0

function OnStart()
    spot.Scene:On("Scored", function(points, player)
        total += points
    end)
end
```

Callbacks run synchronously, in subscription order, with the emitted arguments; `Emit` returns how many ran, and
emissions nest up to 16 deep. A subscription belongs to the component that subscribed: its callbacks are skipped while
that component is disabled or faulted, and end with it; `subscription:Cancel()` ends it early. A callback runs as its
subscriber, so an error faults the subscriber, logged under the `event callback` hook, while the emitter carries on.

### Coroutines

`spot.Component:StartCoroutine(function)` runs a function as a coroutine of the component, after each frame's updates.
What it yields with `coroutine.yield(...)` decides when it continues: nothing, the next frame;
`spot.Component:WaitFrames(n)`, after `n` frames; `WaitSeconds(seconds)`, once that much scaled time has passed;
`WaitUntil(function)`, on the first frame the function returns true. It runs at once until its first yield.
`StartCoroutine` returns an id for `StopCoroutine(id)` and `IsCoroutineRunning(id)`; `StopCoroutines()` stops them all.
They are the engine's component coroutines (`ScriptComponent::StartScriptCoroutine()`, driven by its `Task`
scheduler): paused while the component is inactive, ended when it is removed, and an error or an invalid yield faults
the component under the `Coroutine` hook.

### Calling other scripts

`entity:GetScript()` gives the first started script of an entity, and `entity:GetScript("health")` the first one whose
source file is named so (`scripts/health.luau`): its globals, functions as methods. The call runs in the callee's scene
scope, and its errors propagate to the caller without faulting the callee. In C++, `ScriptComponent::GetScriptObject()`
gives the same object.

## Exposing game components

`ScriptBindings<T>` (`scene/scripting/script_bindings.h`, included explicitly) describes a C++ component's fields,
validated properties and methods once, with engine values; every language dispatches through these descriptors. To
expose a game's `Health` component:

```cpp
Spot::RegisterComponent<Health>("Health");
Spot::ScriptBindings<Health>()
    .Field("value", &Health::value)
    .Property("guarded", &Health::GetRemaining, &Health::SetRemaining)
    .ReadOnly("calls", [](const Health& health) { return health.calls; })
    .Method("Damage", &Health::Damage);
```

```lua
local spot = require("@spot")

function OnStart()
    local player = spot.Scene:FindEntity("Player")
    if player and player.Health then
        player.Health.value -= 10
        player.Health:Damage(5)
    end
end
```

`.Field` reads and writes a public data member directly, without serialization or `OnValidate`; `.Property` takes a
getter and a validating setter, and `.ReadOnly` a getter. `.Method` accepts const or non-const member functions and
lambdas, checks argument counts and types, and converts the result (nil for `void`); overloads need a cast. Values
are booleans, numeric scalars (integers must fit their C++ type and ±(2^53−1)), `std::string`, `Vector2`, `Vector3`,
`Color` and explicitly exposed `ScriptObjectRef`s. Names are identifiers, PascalCase for methods and camelCase for
fields; the base component's members are reserved, and a property and a method cannot share a name. Register
components and descriptors before running scenes; `UnregisterComponent()` drops a type's descriptors before a game
module unloads, and registering it again replaces them.

## Native game libraries

`RegisterScriptLibrary(name, factory)` installs an engine `ScriptObject` library in every script runtime before
initialization; Luau scripts require it by alias, `require("@combat")`. The factory creates a fresh object per
component, and the bridge handles all conversions: implement `Get` for constants and properties, and `Call` for
functions. `spot` is reserved for the scene; names are case sensitive, and names a language's standard library already
has fail when binding that runtime.

```cpp
Spot::RegisterScriptLibrary("combat", [] { return std::make_shared<CombatLibrary>(); });
```

Register before scenes run, and call `UnregisterScriptLibrary("combat")` before unloading its code, after destroying
live instances, which keep the objects they have.

## How the engine side works

`ScriptValue` carries nil, booleans, numbers, strings, engine vectors and colors, and `ScriptObjectRef` references.
`ScriptObject` supplies `Get`, `Set`, `Call` and member discovery without side effects through `HasProperty` /
`HasMethod`; `IsCallable` / `Invoke` carry a language's functions across the boundary, such as event callbacks. A
runtime translates this protocol to its own values and never implements scene operations itself; it exposes its
functions and objects as `ScriptObject`s and runs every call into them through `ScriptRuntime::RunInvocation`, which
`ScriptComponent` points at its scene scope.

`script_scene.cpp` owns the `spot` library, entity and component identity, transforms, structural changes, input and
physics queries, contact snapshots, events (a per-scene `ScriptEventSystem`), validation and the built-in component
descriptors.

A runtime's `GetObject()` is the script's own object. `CreateCoroutine(function)` wraps one of its functions in a
resumable object (`Call("resume")`, `"done"`); a runtime returning null has no coroutines. Tools reach running scripts
through registries that need nothing from a language beyond its adapter: `CheckScriptSyntax()` (a language's
`ScriptSyntaxChecker`), `GetScriptExports()` (its `ScriptExportReader`), `SetScriptBreakpoints()` with
`SetScriptBreakHandler()` (the runtime's `SetBreakpoints()`; hits report the stack and continue, as the engine never
blocks a frame), `SetScriptProfiling()` with `GetScriptProfile()` (the runtime's `SetProfiling()` / `TakeProfile()`),
and `ScriptComponent::Reload()`, which carries what the runtime's `CaptureState()` returns into the new code. Hooks are
always timed as `<script>.<hook>` profiler scopes.

### Adding a language

Implement `ScriptRuntime` around one instance of the language:

- `BindLibrary` makes a named native library available before initialization. Preserve reference identity when
  wrapping the same `ScriptObjectRef`; weak wrapper caches avoid keeping dead references alive.
- `ValidateFunction` receives each optional hook's expected parameter count before initialization; missing hooks are
  valid, and a dynamic language can check when it calls.
- `SetSourceLoader` receives the engine's loader of imported sources, which resolves paths relative to the importer
  and loads through `Assets`, including pak files; imports cannot leave the asset root.
- `Initialize` runs the module's top level once, with libraries installed and scene access active. Construction may
  compile but must not run user code.
- `HasFunction` discovers hooks after initialization, and `Call` invokes them. Convert the language's errors to C++
  exceptions with script locations, so the component's fault guard reports them.

Keep the interpreter's types in the adapter, and register its factory once:

```cpp
Spot::RegisterScriptLanguage("custom", {".custom"},
    [](std::string_view source, std::string_view sourceName) {
        return std::make_unique<CustomRuntime>(source, sourceName);
    });
```

Names and extensions are case insensitive; registering a language again replaces its factory and extensions, and an
extension another language owns is rejected. An empty extension list registers a runtime for embedded source only.
`UnregisterScriptLanguage()` stops new instances; destroy instances and unregister before unloading the module that
supplies the code. Map the extension to `AssetType::Script` in `assets/asset_type.cpp`: the script importer and the
`ScriptSource` format keep source text whatever the language. The editor's highlighting and templates are in
`editor/code/script_editor.cpp`.

`tests/script_runtime_tests.cpp` exercises the engine side with a test runtime of its own; the `luau_*_tests.cpp` run
the Luau adapter against the same API, with both the static and the shared engine libraries.

## Host property access

Scene references are cached by object kind, entity id and component id in each script context. Repeated traversal
of `entity.transform` reuses the wrapper even when a script keeps no reference between hooks. Returned references
share the context's lifetime; cached wrappers do not own it, so releasing the runtime and all exported references
releases the cache. References still resolve ids inside the current execution scope and reject destroyed objects. Luau
retains the userdata and property-token cache of context references until the runtime closes, so a garbage
collection between hooks does not recreate them. Transient records and ordinary host objects keep weak runtime
caches; custom context-backed objects can opt in through `IsPersistentReference()`.

Luau caches property tokens per host wrapper. Built-in scene properties dispatch through these tokens instead of
repeating name comparisons. Registered component properties receive numeric registry tokens too. Replacing a
binding keeps its token and updates the descriptor; unregistering a component removes its tokens and callbacks.
Wrappers cache only tokens, and an invalidated token falls back to resolving the current binding by name. Property
names that overlap another object's fields remain component bindings, including the uppercase `Transform` name.
Custom runtime adapters can use `ScriptObject::ResolveProperty`, `GetResolved` and
`SetResolved`; dynamic objects retain their named fallback. Warm scalar and reference reads avoid wrapper/key
allocations; strings and value snapshots can still allocate. Component bindings use heterogeneous name lookup and
retain an immutable property descriptor only for the invocation, avoiding copies of heap-backed callbacks without
caching callbacks in entity wrappers after component-type unregistration.

The completed native-token and persistent-wrapper paths passed all 17 native-runtime and 32 Luau cases against
both libraries, including binding replacement/unregistration and allocation pressure, plus the three real-scene
sandbox Luau cases. The benchmark now adds its system modules explicitly, preserving its workload after Phase 6.2.

## Binding layout

`scene/scripting/script_scene.cpp` owns scene scopes, cached entity/component references, structural operations,
events and coroutines. System-specific built-ins live beside it: `script_render.cpp` and `script_audio.cpp` register
component members; `script_input.cpp`, `script_time.cpp` and `script_physics.cpp` implement their libraries. Shared
conversion, validation, property tokens and read-only result records stay in the private `script_module_bindings.h`.
Physics resolves entity arguments and creates result references through the scene facade, retaining ids rather than
engine pointers. The public script API remains described once in `script_api.h`; the binding files implement it.

## Phase 9.1 measurements

### Initial implementation

Captured on 2026-10-24 from `658c7ac`, before the binding split, on the Phase 2.1 Windows/MSVC machine with Vulkan
(NVRHI), Release, the unchanged benchmark and workload: 500 crates, 50 lamps, 934 bodies, High quality, 1600x900,
vsync/validation off, orbit camera, 240 warm-up frames and 1,200 measured frames in each fresh process.

```powershell
cmake --build --preset msvc-release --target spot_sandbox_benchmark
./build/msvc/tests/Release/spot_sandbox_benchmark --props 500 --lamps 50 --warmup 240 --frames 1200 --quality 2
```

| Measurement (ms) | Capture 1 | Capture 2 | Capture 3 |
|---|---:|---:|---:|
| Frame mean | 10.146 | 9.949 | 9.919 |
| Frame median | 10.128 | 9.885 | 9.890 |
| Frame p95 | 11.533 | 10.822 | 10.837 |
| Startup scene load | 147.239 | 155.258 | 135.412 |
| Prefab spawn mean | 0.060455 | 0.053784 | 0.048319 |
| Script update mean per frame | 0.034147 | 0.029936 | 0.030898 |
| Script update median per frame | 0.031500 | 0.028600 | 0.029700 |
| Script update p95 per frame | 0.047600 | 0.042300 | 0.042100 |

The median of the three script-update means is **0.030898 ms/frame**, 15.1% below Phase 2.1's **0.036402 ms/frame**.
Each capture records 3,600 hooks without dropped samples; resident memory is 212.4 / 212.9 / 213.1 MiB. Compiler,
linker and test processes were monitored throughout; a capture overlapping another session's GPU tests was
rejected. Assets start uncached in each process; the OS cache is warm. These figures compare historical revisions
across intervening phases, so they do not isolate this change's contribution. Frame means increased relative to
Phase 2.1; the script gain is not a claim of whole-frame improvement.

Validation: the full MSVC Debug build succeeded. The headless run passed 941 of 942 cases, with the falling/respawn
sandbox case timing out under concurrent build load; its isolated repeat passed in 134.30 seconds. After the final
property fast-path adjustment and getter-copy regression test, all 16 native-runtime and 32 Luau cases passed
against both static and shared libraries. The binding split also passed those cases and six tooling/API checks.

### Final integrated implementation

Three new fresh-process captures from `a1ee0e8` include the completed native property-token path and persistent
Lua reference wrappers, plus the application and asset-reload refactors integrated from `main`. The sandbox
benchmark adds the required system/game modules explicitly; its workload and measured scopes remain the same.
Both scripting options are on. No compiler, linker or test process overlapped any capture.

| Measurement (ms) | Capture 1 | Capture 2 | Capture 3 |
|---|---:|---:|---:|
| Frame mean | 9.418 | 9.438 | 9.397 |
| Frame median | 9.374 | 9.361 | 9.351 |
| Frame p95 | 10.139 | 10.178 | 10.166 |
| Frame worst | 27.327 | 44.085 | 13.157 |
| Startup scene load | 404.369 | 130.342 | 127.761 |
| Prefab spawn mean | 0.049626 | 0.047834 | 0.047877 |
| Script update mean per frame | 0.027866 | 0.027207 | 0.027476 |
| Script update median per frame | 0.027200 | 0.026600 | 0.026900 |
| Script update p95 per frame | 0.040900 | 0.041200 | 0.040700 |

The median of run means is **0.027476 ms/frame**, **24.5% below** the historical baseline's 0.036402. All runs have
934 bodies, 3,600 measured script hooks and zero dropped samples. Resident memory is 211.3 / 211.8 / 237.1 MiB.
Each process starts with empty asset caches; the OS file cache was not flushed. The first scene-load result is an
outlier, retained rather than interpreted as a scripting effect. Frame time is still above the historical 9.200 ms
baseline. Intervening refactors and run variance mean these figures do not isolate the scripting change's effect.

Final validation on Windows/MSVC Debug: all **947 headless cases** passed after integrating the complete Phase 6,
with the long falling/respawn case run separately (125.44 seconds). After integrating `main`'s subsequent reload
commit, the full build and **257 affected integration cases** passed, followed by all **98 scripting cases**
(17 native-runtime and 32 Luau cases against each library). The enabled Release benchmark built and completed
three valid captures. Both static and shared scripting-disabled consumers also rebuilt and passed, with Luau
still requested in CMake but absent from the target graph and consumer definitions. The layer check passes.
