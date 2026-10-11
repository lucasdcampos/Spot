# Developer tools

`Console` is a code-only command registry. `Console::Get()` is the process-wide runtime registry; tools and tests
can keep independent instances. `Register(ConsoleCommand)` returns a move-only `ConsoleRegistration`. Keep it
with the callback's owner: destruction or `Reset()` unregisters the callback. Duplicate or invalid names fail
without replacing an existing entry. Commands can unregister themselves or execute other commands.

`Execute()` parses one command with whitespace, single/double quotes and backslash escapes. Incomplete quotes
and invalid arguments return a `ConsoleResult`; callback exceptions become failed results. Completion returns
sorted command names. History keeps the last 128 distinct consecutive lines, and output keeps 256 results.

```cpp
Spot::Console console;
Spot::CVar<float> speed(console, "player.speed", 5.0f, "Walking speed",
    Spot::ConsoleFlags::Cheat, [](float value) { return value >= 0.0f && value <= 100.0f; });
auto spawn = console.Register({"spawn", "Create a prop", Spot::ConsoleFlags::Cheat,
    [](std::span<const std::string> args) -> Spot::ConsoleResult {
        if (args.size() != 1) return {false, "Usage: spawn <prop>"};
        return {true, "Requested " + args.front()};
    }});
console.SetCheatsEnabled(true);
console.Execute("player.speed 8");
```

`CVar<T>` supports booleans (`true/false`, `1/0`), integers, finite floating-point values and strings. It owns its
value or binds a getter/setter to an existing setting; bindings must outlive the cvar. Queries show the current
value, default and description. `Cheat` entries require `console.cheats true` in the runtime registry. `Saved`
entries serialize through `Console::Serialize(Archive&)`; validators also check values loaded from a save.

Applications keep an `EngineConsole` during `Run()` and load/save `console.json` in their user data directory.
Games and editor modules therefore share the same public registry, and scoped registrations disappear before
their code unloads. The engine registers:

| Entry | Effect |
|---|---|
| `help [prefix]` | List matching command names |
| `console.cheats` | Permit commands flagged as development cheats |
| `time.scale` | Simulation speed, including zero to pause |
| `physics.gravity` | Main scene gravity on Y |
| `physics.debug` | Main scene collider visualization |
| `render.enabled` | Application scene rendering |
| `render.max_fps` | Saved frame cap; zero is unlimited |
| `audio.volume` | Saved master audio gain |
| `profiler.enabled` | Frame scope/counter recording |

Scene bindings resolve the current scene each time, retaining no component pointers across scene changes.
Gravity and debug settings apply to the current main scene; its authored settings apply again when it is replaced.

## In-game and editor consoles

The physical Grave key opens the runtime overlay. Enter runs the line, Tab completes the command name, Up/Down
browse history and Escape closes it. Text comes from UTF-8 text-input events, with code-point-aware Backspace.
`ConsoleInput` owns an M2.6 input capture: held gameplay controls are released when it opens, mouse/keyboard/gamepad
events stay with the console, and closing restores the cursor lock the game requested. Escape is consumed while
closing, so it cannot also activate the game's pause action. Platform text input starts and stops with the overlay.
The simulation continues while it is open; use `console.cheats true` and `time.scale 0` to pause simulation.

Set the game's imported UI font with `Application::SetDeveloperFont()`. The sandbox uses its HUD font. Applications
draw `DrawConsoleOverlay()` with the public `Renderer2D` after the game's rendering, including when scene rendering
is disabled. Tools can set `ApplicationConfig::consoleOverlay = false`; the editor uses its own console panel.
Its command field executes the shared registry, logs results, completes with Tab and browses with Up/Down.

`ConsoleInput` and `DrawConsoleOverlay()` also work in custom loops. The input model is independent of the GPU;
feed it events before gameplay and deliver unconsumed events to `InputState`. The draw function expects a
`Renderer2D` camera spanning `[0, viewSize]` with Y up.

### Testing without windows

`GraphicsDevice::CreateOffscreen()` creates a GPU device without a window. `BeginOffscreenFrame(target)` records
into a single-sample 2D color render target; default render passes use that target, and `EndFrame()` submits it.
Read pixels with `DownloadTexture()` after submitting. A window device can record offscreen frames too.
VSync is irrelevant for an offscreen device. Headless tests validate console editing/captures and the editor
field; the targeted `[offscreen]` GPU test validates text, overlay pixels and readback without desktop windows.
