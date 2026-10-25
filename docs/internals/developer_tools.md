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
| `profiler.stream` | Send completed frames over the game's log pipe |
| `stats` | Saved runtime statistics overlay |
| `screenshot [name.png]` | Queue a PNG of the next complete frame |
| `capture [name]` | Request the next RenderDoc frame when its runtime is present |

`DevToolsModule` also holds the reloads of assets edited while a game runs, which the editor uses after imports:
`ReloadAssets()` makes changed assets, and the loaded assets that refer to them, load anew from their imported files,
and `ReloadAssetReferences()` loads a component's references to them again. They stay out of `AssetManager` and the
scene layer, which games ship with and never reload.

Scene bindings resolve the current scene each time, retaining no component pointers across scene changes.
Gravity and debug settings apply to the current main scene; its authored settings apply again when it is replaced.

## In-game and editor consoles

The physical Grave key opens the runtime overlay. Enter runs the line, Tab completes the command name, Up/Down
browse history and Escape closes it. Text comes from UTF-8 text-input events, with code-point-aware Backspace.
`ConsoleInput` owns an M2.6 input capture: held gameplay controls are released when it opens, mouse/keyboard/gamepad
events stay with the console, and closing restores the cursor lock the game requested. Escape is consumed while
closing, so it cannot also activate the game's pause action. Platform text input starts and stops with the overlay.
The simulation continues while it is open; use `console.cheats true` and `time.scale 0` to pause simulation.

The overlays, screenshots, RenderDoc captures and profile streaming are `DevToolsModule`
(`engine/spot/runtime/dev_tools_module.h`), which development builds and the editor add first, so the console sees
events before the game and draws over everything. It registers the `stats`, `screenshot`, `capture` and
`profiler.stream` commands, connects to RenderDoc when it is made, before the graphics device, and streams profiled
frames as they complete. Set the game's imported UI font with `DevToolsModule::SetFont()`; the sandbox uses its HUD
font. It draws `DrawConsoleOverlay()` with the public `Renderer2D` after the game's rendering, including when scene
rendering is disabled. Tools can turn the overlay off (`DevToolsConfig::consoleOverlay`); the editor uses its own
console panel.
Its command field executes the shared registry, logs results, completes with Tab and browses with Up/Down.

`ConsoleInput` and `DrawConsoleOverlay()` also work in custom loops. The input model is independent of the GPU;
feed it events before gameplay and deliver unconsumed events to `InputState`. The draw function expects a
`Renderer2D` camera spanning `[0, viewSize]` with Y up.

## Runtime statistics

`stats true` displays FPS, measured frame milliseconds, draw calls, triangles, live physics bodies, active audio
voices and resident process memory. Frame duration is independent of simulation time scale. Rendering counts
combine 3D, shadow draw calls and Renderer2D; triangle counts include the main 3D view and 2D quads. Memory is CPU
resident memory, including assets and backend allocations; it does not measure dedicated GPU memory and is zero
when the platform cannot query it. The setting persists with the other saved cvars.

`RuntimeStats::Sample()` also works without graphics. When recording is disabled, FPS uses the unscaled frame
clock and rendering counts are zero instead of showing stale recordings. Applications record bodies, voices and
resident memory as profiler counters so tools can inspect them with the frame that produced them.

## Editor profiler

Open **Window > Profiler** to inspect the last 240 frames. The histogram shows frame duration; hover a bar to
read its time or click to select it and pause collection. Pause preserves owned copies even as the engine's ring
advances. The timeline shows nested scopes, with inclusive durations on hover. The averages table sorts scopes
by inclusive milliseconds per recorded frame and shows calls per frame; absent scopes count as zero. Dropped
samples are reported for each frame so an incomplete recording remains visible.

Scope totals update only when frames enter or leave the retained ring. Drawing averages does not scan every
sample in the history, so the panel avoids adding a history-sized workload while investigating a stress pile.

The default source is the editor, including games played in its scene view. Select **Game process** for a game
launched by the editor. `GameRunner` enables `SPOT_PROFILE_STREAM=1` only in that child's environment, reads
versioned profile records from its existing log pipe and retains the same bounded history. Normal logs still
reach the console. Standalone games stream only when this environment variable or `profiler.stream` is enabled.
Decoding validates records before storing them; restarting the process starts a new history.

Tools can pass `ProcessEnvironmentVariable` overrides as the third argument to `Process::Start()`; they inherit
the rest of the environment. `Process::GetEnvironmentValue()` returns an owned optional string without exposing
platform types or environment storage.

## Screenshots and frame captures

`screenshot` saves the next complete frame, including custom application drawing and developer UI, in the user's
`screenshots` folder. An optional bare `.png` name selects the file; otherwise a timestamp supplies one. Requests
reject paths and concurrent pending captures. The GPU renders that frame into a matching readable target, then
blits it into the acquired window target; there is no second scene render. Readback waits for submission only
when a screenshot was requested. PNG conversion handles RGBA/BGRA and preserves sRGB encoding.

Custom loops can use `RenderContext::SetFrameTarget()` between passes to redirect default targets and window
resolves, restore the acquired target with null, and `BlitTextureToFrame()` to present the recorded image.
Targets must match the acquired frame's size/format and be single-sample. Explicit pass targets are unaffected.
After `EndFrame()`, `SaveScreenshot()` reads the target and writes a PNG, creating its parent directories.

`capture` uses the optional [RenderDoc application API](https://renderdoc.org/docs/in_application_api.html).
Applications connect before creating graphics. Windows detects injection or the standard RenderDoc installation;
Linux loads `librenderdoc.so`, and `SPOT_RENDERDOC_LIBRARY` can select a different runtime. No SDK types enter the
public API and no RenderDoc UI is launched. Connected runtimes remain loaded because their hooks can outlive an
application's graphics device. Missing installations are normal; the command reports that capture is unavailable.
Metal/macOS capture is unavailable through RenderDoc. Templates share the screenshots folder; RenderDoc appends
its frame number and `.rdc` extension. Tools can call `FrameCapture::Connect(path)` and `Request(template)` directly.

### Testing without windows

`GraphicsDevice::CreateOffscreen()` creates a GPU device without a window. `BeginOffscreenFrame(target)` records
into a single-sample 2D color render target; default render passes use that target, and `EndFrame()` submits it.
Read pixels with `DownloadTexture()` after submitting. A window device can record offscreen frames too.
VSync is irrelevant for an offscreen device. Headless tests validate console editing/captures and the editor
field; the targeted `[offscreen]` GPU tests validate overlay text, counters, target redirection, presentation and
PNG readback in RGBA/BGRA/sRGB formats without desktop windows.
