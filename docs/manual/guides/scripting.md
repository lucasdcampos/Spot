# Scripting

Games script their behavior in [Luau](https://luau.org), a typed language descended from Lua. A script is a `.luau`
asset run by a **ScriptComponent** on an entity; each component runs its own copy, so two coins with the same script
do not share their variables. Everything a script can reach is in the [scripting API](../reference/scripting_api.md).

```lua
local spot = require("@spot")

speed = 4 -- a field: shown in the Inspector and saved with the scene
local hits = 0 -- a local: private to the script

function OnStart()
    spot.entity.name = "Player"
end

function OnUpdate(deltaTime: number)
    if spot.Input:IsActionDown("Jump") then
        spot.entity.transform.position.y += deltaTime * speed
    end
end
```

## Hooks

The engine calls the functions a script defines with these names:

| Hook | When |
|---|---|
| `OnStart()` | Once, before the first update |
| `OnUpdate(deltaTime)` | Every frame, with the seconds since the last one |
| `OnFixedUpdate(deltaTime)` | At the physics' fixed rate, for forces and movement that must not depend on the frame rate |
| `OnLateUpdate(deltaTime)` | Every frame after every `OnUpdate`, such as for a camera that follows the player |
| `OnEnable()`, `OnDisable()` | When the component or its entity is turned on or off |
| `OnDestroy()` | When the component or its entity goes away |
| `OnCollisionEnter/Stay/Exit(collision)` | Contacts of the entity's rigidbody; `collision.other` is the other entity |
| `OnTriggerEnter/Exit(contact)` | Colliders marked as triggers, entered or left |
| `OnReload()` | After the script's code changes during Play, to start again what reloading ended |

The 2D physics hooks end in `2D`: `OnCollisionEnter2D`, `OnTriggerEnter2D` and the rest. A script that fails, such as
by calling a function that does not exist, logs where and stops; only its component stops running.

## Fields

A global assigned a constant at the top of the script (a number, a string, a boolean, or a `Vector2`, `Vector3` or
`Color` built from numbers) is a **field**. The Inspector shows it under **Fields**, and a value set there is saved
with the scene and given to the script before `OnStart`. Use fields for what designers tune, and `local` for the
rest. Reading a name that was never declared is an error, not `nil`, so typos show at once.

## Entities and components

`spot.entity` is the script's entity; `spot.Scene:FindEntity(name)` and `FindEntityWithTag(tag)` find others, or give
`nil`. An entity has a `name`, a `transform` (`position`, `rotation` in radians, `scale`, and their `world` forms) and
components: `entity:GetComponent("Camera")`, or `entity.Camera` for short.

```lua
local camera = spot.Scene:FindEntity("Camera")
camera.Camera.fieldOfView = math.rad(50)

local bullet = spot.Scene:Instantiate("prefabs/bullet.prefab")
bullet.transform.position = spot.entity.transform.position
```

`Vector2`, `Vector3` and `Color` are values: `local p = spot.entity.transform.position` is a copy, and changing it
does nothing until it is assigned back. `spot.entity.transform.position.x = 5` writes through at once.

References to entities and components stay valid while what they refer to exists. Keep one across frames if you need
it, and check `IsValid()` when it may have been destroyed: using a destroyed one is an error.

## Events

Scripts that should not know about each other talk through events. `spot.Scene:Emit(name, ...)` sends one to the
whole scene, and `spot.Scene:On(name, function(...) end)` listens; `entity:Emit` and `entity:On` do the same for one
entity.

```lua
-- In the coin:
spot.Scene:Emit("Scored", 10)

-- In the score keeper:
spot.Scene:On("Scored", function(points)
    total += points
end)
```

Listeners run at once, in the order they subscribed. A subscription ends with its component, or with
`subscription:Cancel()`.

## Coroutines

A coroutine is a function that waits without blocking the game, for sequences such as a door that opens, waits and
closes:

```lua
local component = spot.Component

function OnStart()
    component:StartCoroutine(function()
        print("Opening")
        coroutine.yield(component:WaitSeconds(2))
        print("Closing")
        coroutine.yield(component:WaitUntil(function()
            return spot.Input:IsActionPressed("Fire")
        end))
        print("Fired")
    end)
end
```

Yielding nothing waits a frame; `WaitFrames(n)`, `WaitSeconds(s)` and `WaitUntil(condition)` wait longer.
`StartCoroutine` gives an id for `StopCoroutine(id)`. Coroutines pause while their component is off and end with it.

## Modules and other scripts

`require("./combat")` loads `combat.luau` beside the script; what it returns is what `require` gives:

```lua
-- combat.luau
local combat = {}

function combat.Damage(health: number, amount: number): number
    return math.max(health - amount, 0)
end

return combat
```

`entity:GetScript("health")` gives the globals of the entity's `health.luau` script, to read its fields or call its
functions.

## Reloading during Play

Saving a script while the game plays swaps the new code in before its next hook. Fields and other globals holding
values keep them; `OnStart` does not run again, and coroutines and event subscriptions end, so start them again in
`OnReload()` if the script needs them.

## Writing scripts in your own editor

Scripts open in the editor's **Code** panel, which checks their syntax as you type and sets breakpoints with F9. Any
editor with Luau's language server, [luau-lsp](https://github.com/JohnnyMorganz/luau-lsp), such as VS Code, Zed or
Neovim, also completes and checks `spot.` calls:

1. Open the project once in Spot's editor, or run `spot cook` on it. That writes the engine's types into
   `.spot/luau/` and points the project's `.luaurc` at them.
2. Load the definitions of the global constructors (`Vector3()` and the others): in VS Code, the setting
   `"luau-lsp.types.definitionFiles": {"spot": ".spot/luau/spot.d.luau"}`; with luau-lsp's command line,
   `--definitions=@spot=.spot/luau/spot.d.luau`. Set luau-lsp's platform to `standard`.

**Preferences > Scripts > External editor** sets the command that **Open in External Editor** runs, such as
`code {project} --goto {file}:{line}`.

## C++ components in scripts

Games with C++ code can give scripts components and libraries of their own; see
[exposing game components](../../internals/scripting.md#exposing-game-components).
