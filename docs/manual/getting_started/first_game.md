# Your first game

A small 3D game without C++: you walk a cube across the ground with the keyboard or a gamepad and collect three
coins. It takes a scene and three Luau scripts, and you can finish it in the editor or with only the command line and
a text editor. The finished project is [`examples/first_game`](../../../examples/first_game/), which the tests play.

You need the editor and the command line built: see [Installing Spot](install.md).

## 1. Create the project

In the editor, the hub opens first: choose **New Project**, name it `Coins`, pick the **3D** template and
**Scripts only**. With the command line:

```sh
spot new Coins --code none
```

The project is a folder:

```
Coins/
  Coins.spot              the project: its name, its startup scene and its settings
  assets/scenes/Main.scene
  .spot/                  generated: imported assets and the editor's state, never edited or committed
```

The template's scene has a camera, a sun, a sky, the ground and a cube. Open it in the editor (double-click
`scenes/Main.scene` in **Assets**), or run it now with `spot run Coins`.

Every asset gets a `.meta` file beside it the first time it is imported. It holds the asset's id, which scenes use
to refer to it: keep it with the asset, and commit both.

## 2. Move the player

Rename the cube `Player` (select it in the **Hierarchy** and edit its name in the **Inspector**). In **Assets**,
make a `scripts` folder, and in it a script with **Create > Luau Script**, named `player.luau`:

```lua
local spot = require("@spot")

-- Moves the player on the ground with the Move action: WASD or a gamepad's left stick.
speed = 5

function OnUpdate(deltaTime: number)
    local move = spot.Input:GetActionVector2("Move")
    local position = spot.entity.transform.position
    position.x += move.x * speed * deltaTime
    position.z -= move.y * speed * deltaTime
    spot.entity.transform.position = position
end
```

Give the player a **ScriptComponent** (**Add Component** in the Inspector) and set its script to `player.luau`.

What this does:

- `require("@spot")` gives the script the engine: `spot.entity` is the entity the script runs on, `spot.Input` the
  keyboard, mouse and gamepads, `spot.Scene` the other entities.
- The engine calls `OnUpdate` every frame, with the seconds since the last one. Multiplying by them makes the
  speed the same at any frame rate.
- `Move` is one of the input actions every project starts with (the others are `Look`, `Jump`, `Fire` and `Pause`);
  the project settings change their keys and buttons. An action is better than a key: it works with a gamepad too.
- `speed` is a global with a constant value, so it is a **field**: the Inspector shows it, and a value set there is
  saved with the scene. Variables the Inspector should not show are `local`.

Press **Play** (Ctrl+P) and walk with WASD. Shift+F1 gives the mouse back to the editor; Play again stops.

## 3. Add the coins

Create a sphere (**Create > 3D Object > Sphere** in the Hierarchy), name it `Coin 1`, scale it to `0.5` and put it
at `(2, 0.5, 0)`. Then the script that makes it a coin, `scripts/coin.luau`:

```lua
local spot = require("@spot")

-- Spins in place until the player comes close, then tells the scene it was collected and goes away.
pickupDistance = 1
spinSpeed = 3

local player
local angle = 0

function OnStart()
    player = spot.Scene:FindEntity("Player")
end

function OnUpdate(deltaTime: number)
    angle += spinSpeed * deltaTime
    spot.entity.transform.rotation = Vector3(0, angle, 0)

    local coin = spot.entity.transform.position
    local target = player.transform.position
    local dx, dz = coin.x - target.x, coin.z - target.z
    if dx * dx + dz * dz < pickupDistance * pickupDistance then
        spot.Scene:Emit("CoinCollected")
        spot.entity:Destroy()
    end
end
```

Give the coin a ScriptComponent with `coin.luau`, then duplicate it twice (Ctrl+D) as `Coin 2` at `(4, 0.5, 0)` and
`Coin 3` at `(6, 0.5, 0)`.

- `OnStart` runs once, before the first `OnUpdate`: the place to find what the script needs. `FindEntity` finds an
  entity by name.
- Rotations are in radians, around each axis.
- `Emit` sends an event to the scene: the coin does not need to know who counts the coins.

## 4. Count them

Create an empty entity named `Game` (Ctrl+Shift+N), with a ScriptComponent running `scripts/game.luau`:

```lua
local spot = require("@spot")

-- Counts the coins collected and says when they all are.
coinCount = 3

local collected = 0

function OnStart()
    spot.Scene:On("CoinCollected", function()
        collected += 1
        print(`Coins: {collected}/{coinCount}`)
        if collected == coinCount then
            print("You collected every coin!")
        end
    end)
end
```

`On` calls the function each time the event is sent. `print` writes to the **Console**, and to the log of an
exported game. Play: walk right through the coins, and the console counts them.

Save the scene (Ctrl+S).

## 5. Ship it

Export the game with the command line:

```sh
spot export Coins
```

`Coins/builds/<platform>/` is the game: the player, the engine, a `game.json` and the imported assets packed into
`assets.pak`. That folder runs on its own, on any machine of the same platform: zip it and share it.

## Without the editor

Everything above is files, which any text editor can write. A scene is JSON, but writing one by hand is error prone:
start from the template's and copy the finished one's entities from
[`examples/first_game/assets/scenes/Main.scene`](../../../examples/first_game/assets/scenes/Main.scene). Then:

```sh
spot cook Coins     # imports what changed and writes the .meta files of new assets
spot run Coins
```

`spot cook` also writes the engine's types for Luau's language server, so editors such as VS Code complete and check
`spot.` calls: see [writing scripts in your own editor](../guides/scripting.md#writing-scripts-in-your-own-editor).

## Next

- [Scripting](../guides/scripting.md): the hooks, events, coroutines and how scripts call each other.
- [Scripting API](../reference/scripting_api.md): everything scripts can reach.
- [Command line](../reference/command_line.md): every `spot` command.
