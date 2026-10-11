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
