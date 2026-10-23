# Scripting integration

Spot exposes one scene API to all scripting languages. Start with `scene/scripting/script_runtime.h` for a
runtime adapter and `scene/scripting/script_component.h` for the component lifecycle. Built-in adapters are
provided for **Lua 5.4** (`SPOT_ENABLE_LUA=ON` by default, `scene/scripting/lua_runtime.cpp`) and
[ViceScript](vicescript.md) (`SPOT_ENABLE_VICESCRIPT=OFF` by default, `scene/scripting/vice_runtime.cpp`).

## Shared engine behavior

`ScriptValue` carries null, booleans, numbers, strings, engine vectors/colors and `ScriptObjectRef` references.
`ScriptObject` supplies `Get`, `Set`, `Call` and side-effect-free member discovery through `HasProperty` / `HasMethod`. A runtime translates this protocol to its native userdata,
objects or tables. It never implements scene operations itself. `IsCallable` / `Invoke` carry a language's functions
across the boundary, such as event callbacks; adapters expose their own functions and objects as `ScriptObject`s and
run every call into them through `ScriptRuntime::RunInvocation`, which `ScriptComponent` points at its scene scope.

`script_scene.cpp` owns the explicit `spot` library (`entity`, `Component`, `Scene`, `Input`, `Time` and `Physics`),
entity/component identity, transforms, structural changes, input and physics queries, physics contact snapshots,
events (a per-scene `ScriptEventSystem`), validation and the built-in component descriptors. References retain ids and resolve engine
objects only during a scoped script hook. Missing objects invalidate existing references. Scene scopes are
released on exceptions; wrappers do not keep entities, components or scenes alive. The executing owner remains
available only during its removal cleanup. Activity hooks triggered by `SetEnabled` may nest, sharing execution
limits; other host-driven reentry is rejected.

`ScriptBindings<T>` describes a C++ component's fields, validated properties and methods once, using engine
values. All runtimes dispatch through these same descriptors. Register them before running a scene;
`UnregisterComponent` clears their callbacks before a game module unloads. These APIs and their cleanup remain
available with `SPOT_ENABLE_VICESCRIPT=OFF`.

`ScriptComponent` owns asset loading, embedded-source fallback, serialization, a runtime per component, host
libraries, optional `OnStart`, `OnEnable`, `OnDisable`, `OnFixedUpdate`, `OnUpdate`, `OnLateUpdate`, `OnDestroy` and
physics contact hooks (`ScriptApi::Hooks`), scene scopes, event delivery and exported fields. A runtime's
`GetObject()` is the script's own object, used for calls from other scripts and `GetScriptObject()`; a language
registers a `ScriptExportReader` with `SetScriptExportReader` so tools can list its exported fields.
`CreateCoroutine(function)` wraps one of the script's functions in a resumable object (`Call("resume")`, `"done"`);
`ScriptComponent::StartScriptCoroutine` drives it with the engine's `Task` scheduler, so script coroutines wait with
`NextFrame`, `WaitForSeconds` and `WaitUntil` like C++ ones. Vice implements it with its own coroutines and Lua with
threads; a language returning null has no coroutines.

Tools reach running scripts through registries that need nothing from a language beyond its adapter:
`CheckScriptSyntax()` (a language's `ScriptSyntaxChecker`), `SetScriptBreakpoints()` with `SetScriptBreakHandler()`
(the runtime's `SetBreakpoints()`; hits report the stack and continue), and `SetScriptProfiling()` with
`GetScriptProfile()` (the runtime's `SetProfiling()` / `TakeProfile()`). Hooks are always timed as profiler scopes.
`ScriptComponent::Reload()` swaps in reimported code, carrying what the runtime's `CaptureState()` returns into the
new one and calling `OnReload()`. Vice implements all of them; Lua provides syntax checks and reloads without state. Normal component activation and
fault guards suspend disabled scripts and isolate runtime failures. Runtime state is discarded on destruction
and is not serialized. Imported scripts choose a registered runtime from their relative source extension,
including assets loaded from a pak. Embedded source uses the component's explicit `language` string; `sourceName`
labels diagnostics and supplies the base path for embedded imports; it does not select a language or load the root file. Assets take precedence over embedded fields.

```cpp
auto& script = entity.AddComponent<Spot::ScriptComponent>();
script.script = Spot::Assets::Load<Spot::ScriptSource>("scripts/hello.lua");

// Without an asset:
auto& embedded = entity.AddComponent<Spot::ScriptComponent>();
embedded.language = "lua";
embedded.source = "function OnStart() print(123) end";
```

The component is available even with all built-in runtimes disabled. An unregistered language/extension or a missing asset faults
only that component; another runtime is never used as a fallback.

## Vice component classes

Vice roots declare one most-derived class inheriting the implicit host `Component` base:

```go
import spot

class PlayerController : Component {
    var speed = 4

    override func OnStart() {
        entity.name = "Player"
    }

    override func OnUpdate(deltaTime) {
        if spot.Input.IsKeyDown("Right") {
            entity.transform.Translate(Vector3(speed * deltaTime, 0, 0))
        }
    }
}
```

The Vice adapter binds a host class using the runtime's public embedding API and constructs its sole leaf subclass.
Each attached component owns a fresh instance. Fields and methods, including inherited members, can be used without
`self`; locals and parameters take priority, followed by instance members and then module globals. Use `self.member`
when a local shadows a member. The read-only `entity`, `transform` and `scene` properties resolve the current owner;
`entity.name`, `entity.transform.position` and `enabled` expose validated writes. Inherited `GetEntity`, `GetScene`, `GetId`, `IsEnabled`, `SetEnabled`,
`IsActiveAndEnabled`, `IsFaulted` and `IsValid` operations forward to the same engine bindings as other languages.
Hook overrides are checked against native signatures before root global and instance field initializers. Base hooks are no-ops and can
be called with `super.OnStart()` or `super.OnUpdate(deltaTime)`. Source modules may declare reusable component bases;
only root source classes participate in selecting the component. Global free functions are library functions, not hooks.
See [ViceScript](vicescript.md#using-a-component) for selection rules and the full API.

Engine methods use their original PascalCase names (`FindEntity`, `GetTransform`, `Translate`, `SetColor`, `Play`), and
public data fields use camelCase. Validated property conveniences remain available. This is a source API change:
rename old `start`/`update` hooks to `OnStart`/`OnUpdate`, move Vice hooks into a `: Component` class, and replace old
snake_case calls such as `find`, `translate` or `is_key_down` with their engine method names. Stored script state is
not migrated while Play is running; restart it after importing the edited files.

## Lua scripting

When `SPOT_ENABLE_LUA=ON` (the default), `.lua` script assets and `language = "lua"` embedded scripts run in an
isolated Lua 5.4 state per component:

```lua
-- Player.lua
local spot = require("spot")

function OnStart()
    spot.entity:GetTransform():SetPosition(Vector3(1, 2, 3))
end

function OnUpdate(deltaTime)
    if spot.Input:IsKeyDown("Right") then
        spot.entity:GetTransform():Translate(Vector3(deltaTime * 4, 0, 0))
    end
end
```

- Engine objects support both dot (`spot.Scene.FindEntity("Player")`) and colon (`spot.Scene:FindEntity("Player")`) calls.
- Value types (`Vector2`, `Vector3`, `Color`) are available as global constructors; mutating a field on a struct
  read from an engine property (`spot.entity.Transform.position.x = 5`, `spot.entity.SpriteRenderer.color.a = 0.5`)
  writes the updated struct back to the owning object.
- `require("spot")` and registered native libraries return bound `ScriptObject` modules; `require("combat")` or
  `require("./combat.lua")` loads imported `.lua` `ScriptSource` assets through the shared asset loader (including
  from shipped `.pak` files).
- Only safe standard libraries (`base`, `math`, `string`, `table`, `utf8`, `coroutine`) are opened; `io`, `os`,
  `package` and `debug` are omitted, bytecode chunks are rejected, and a per-call instruction budget prevents
  runaway loops from hanging the frame.

## Adding a language

Implement `ScriptRuntime` around one language runtime instance:

- `BindLibrary` makes a named native library available for explicit import before module initialization. Adapt `ScriptObject` operations and
  translate arguments/results, including copied vectors/colors. Preserve reference identity when wrapping the same
  `ScriptObjectRef`; weak wrapper caches avoid keeping dead references alive. Reject unsupported native objects.
- `ValidateFunction` receives the expected arity for each optional lifecycle hook before initialization. Missing
  hooks are valid. A runtime with signature metadata can reject invalid signatures before user code executes.
  A dynamic runtime can defer checking according to its calling convention; it need not parse source to discover hooks.
- `SetSourceLoader` receives the shared engine loader for imported source assets. Translate it to the runtime module loader;
  the adapter supplies its language extension for extensionless requests. The engine resolves paths relative to the importer,
  normalizes separators and loads through `Assets`, including pak files. Imports cannot leave the asset root.
- `Initialize` executes module/global initialization once, with libraries already installed and scene access active.
- `HasFunction` discovers optional hooks after initialization. `Call` invokes a named function with translated arguments.
  Convert language errors to C++ exceptions with script source locations so the component fault guard can report them.

Keep interpreter/VM types in the adapter implementation. Runtime construction may parse/compile source but must
defer executing user code until `Initialize`, when library bindings and scene access are ready. Register its factory once:

```cpp
Spot::RegisterScriptLanguage("custom", {".custom"},
    [](std::string_view source, std::string_view sourceName) {
        return std::make_unique<CustomRuntime>(source, sourceName);
    });
```

No language-specific component
class is needed. Names and extensions are case insensitive. Registration replaces the same language's factory and
extensions atomically; conflicting extensions owned by another language are rejected. An empty extension list
registers a runtime for embedded source only. Unregister with `UnregisterScriptLanguage("custom")`; existing instances
retain their runtime while new creations fail. Destroy instances and unregister factories before unloading the
module that supplies their code. Register languages before running scenes. Factory calls execute outside the
registry lock, and throwing/null factories fault the calling component.

Lua's factory is installed automatically when `SPOT_ENABLE_LUA=ON`, and VICE's when `SPOT_ENABLE_VICESCRIPT=ON`. Source importing and the component are
independent of those options. A registered custom runtime uses the same `ScriptComponent` and descriptors.

A language's standard output function can format its native values and send messages through the engine logger,
as the VICE adapter does for `print`.

For asset tooling, map the new source extension to `AssetType::Script` in `assets/asset_type.cpp`. The existing
script importer and `ScriptSource` format preserve source text independently of any runtime and can be reused
without a new importer or format change. Add language highlighting/templates to the editor when enabling editing
for that language. Editor syntax support is separate from the engine integration.

`tests/script_runtime_tests.cpp` uses an independent test runtime to exercise the shared integration without
depending on VICE. The VICE scene/component tests validate the actual adapter against the same engine API, with both
static and shared engine libraries.

## Native game libraries

`RegisterScriptLibrary(name, factory)` installs an engine `ScriptObject` library in every script runtime before
initialization. The factory creates a fresh object per component; the bridge handles all language conversions.
Implement `Get` for constants/properties and `Call` for native functions. `spot` is reserved for the scene context.
Names are case sensitive; names already provided by a language's standard library fail when binding that runtime.

```cpp
Spot::RegisterScriptLibrary("combat", [] { return std::make_shared<CombatLibrary>(); });
```

A Vice script uses `import combat` and calls `combat.damage(attack, defense)`. Register before scenes run and call
`UnregisterScriptLibrary("combat")` before unloading its code, after destroying live instances. Existing script
instances retain their library objects after unregistering. Factories run outside the registry lock.

For libraries written in Vice, add `.vice` files as ordinary Script assets. Every importing root component gets
independent module state. A library that needs the scene imports `spot` itself; it does not inherit variables
from its importing script. A root can import a library without importing `spot` when only the library uses it.
