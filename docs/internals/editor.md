# Editor

The editor (`spot_editor`, `editor/`) is a Dear ImGui application for making games from projects: it edits scenes
and assets, builds and reloads the game's code (looking for changes four times a second, building only the module,
and reloading a one-line change about a second after it), and plays scenes. Everything but `main.cpp` is the
`spot_editor_core` library, which the editor's tests link.

It follows the rules games do: it uses only the engine's public API and never reaches a backend, and ImGui stays in
the editor, so the runtime never depends on it. It is itself an `Application`, with the engine's modules and its
`RenderModule` drawing no scene, since it shows scenes in its own views.

Its icon is `logo.png`, at the repository's root, scaled down into `editor/resources/icons/`: embedded and set on
the window with `Window::SetIcon()`, which any game can call with RGBA pixels, and on Windows also the executable's
icon (`spot_editor.rc`).

## Projects

Without a project open, the **hub** lists recent projects and creates or opens one (from the templates in
`spot_tools`). The hub is only the interface: creating, building and running projects are the tools' (see
[Tools and the command line](tools_and_command_line.md)). A new project has C++ code or is made of scripts only; the
hub warns when the toolchain the tools were built with is missing (`Toolchain::IsAvailable()`), which only projects
with code need. An open project (`EditorProject`) has its `Project` (see
[Tools and the command line](tools_and_command_line.md)), an `AssetDatabase` that imports its sources as they change,
an `AssetManager` that loads the imports, and, for projects with code, a `ModuleHost` that builds and loads the
game's code.

## Documents, undo and ids

Each open scene is a `SceneDocument` in the `SceneWorkspace`, with its own view, selection and history. Every change
goes through a command (`editor/scene/scene_commands.h`): adding, deleting, reparenting and renaming entities,
editing transforms and components, and so on. A `CommandHistory` undoes and redoes them, and knows whether the
document changed since it was saved.

The editor never keeps pointers to entities or components across frames. Commands hold `EntityId`s, component
UUIDs and saved JSON, so undo history stays valid when entities are deleted and brought back with their ids, or
when the game's code is reloaded and every component is recreated. Unsaved scenes are backed up under
`.spot/backups`, and offered back after a crash.

Prefab assets also open as documents, with a **Prefab** view label and stable source-local ids. Saving validates
that the source has exactly one entity tree. Drag entities into an asset folder to create a prefab; the inspector
shows instance overrides and offers Apply, Revert and Unpack, including actions per field and component. Apply
history restores both the source file and the scene. See [Prefabs](prefabs.md) for the workflow and tool APIs.

Prefab documents preserve nested prefab links. Drag an ordinary tree containing instances into the browser to
save that hierarchy, or drag a linked instance root to create a variant with its base id and overrides. Variants
open as inherited trees and keep their base links across saves and instance Apply operations.

Saving a prefab updates its instances in all open documents, including nested prefabs and variants, while keeping
their overrides, selections and histories. Updates stage the current compact records before replacing entities;
a broken dependency keeps the document intact. Component history uses UUIDs and replays only edited fields, and
missing source targets are skipped safely. Play copies retain their existing instances until play stops.

## Panels

The **Console** executes the shared engine command registry, with Tab completion and Up/Down history. The
**Profiler** selects and pauses recorded frames, shows nested scopes and averages, and switches to profiles from
a game launched in its own process. Both are available from **Window**; see [Developer tools](developer_tools.md).

Dropping a prefab into a scene or the hierarchy adds its saved tree as one undoable action, preserving the root's
rotation and scale and placing it at the drop position. See [Prefabs](prefabs.md) for the source format and runtime API.

Dropping a model into a scene uses `Scene::Instantiate()` to create its node hierarchy below a named root. The
drop position places that root, and a hierarchy drop can parent it to another entity. Empty nodes remain editable,
and each mesh node holds a renderer per submesh. The complete tree is one undoable addition; undo and redo preserve
its entity ids and selection. Picking and selection outlines respect each renderer's submesh range.

| Panel | What it does |
|---|---|
| Scene views | Show a document through an editor camera, with the grid, the colliders as physics sees them, the selection's outline, transform gizmos (move, turn, scale), picking by clicking, and the shapes queued with `DebugDraw` |
| Hierarchy | The entities as a tree: select, rename, reorder and reparent by dragging, create, duplicate and delete |
| Inspector | The selected entities' name, tags, layer, activity, transform and components; draws each component's fields from its `Serialize()` through the `InspectorBackend`; edits several entities at once; marks missing and faulted components; fits box, sphere and capsule colliders to the entity's mesh (Fit to Mesh in their menu) |
| Assets | Browses the project's sources with thumbnails; moves, renames, duplicates, deletes and imports files dropped from the OS; inspects import settings |
| Console | The log, filtered by level and text |
| Audio Mixer | The project's audio buses as strips with faders, peak meters, mute and solo, heard at once, also while playing |
| Project Settings | The project's name, version and startup scene, every settings section, drawn through the inspector backend, the layers' names and collision matrix, and the input actions, whose controls are bound by clicking and pressing them |

## Game code

Projects without code have nothing to build or load: their scripts reload as they change, and Run starts the player
(see [Tools and the command line](tools_and_command_line.md)). Adding a C++ component gives them code.

For a project with code (`src/CMakeLists.txt`), the `ModuleHost` builds the game's code in the background with CMake
whenever it changes, into a game module (a shared library linking `spot_shared`), and loads it from a copy, so the
next build can replace the file meanwhile. Before loading a new module, the open scenes are saved to text and their
components destroyed; after, they load again with the new types. Components whose type the module no longer has stay
as `MissingComponent`s that keep their data: the editor loads every scene with `keepUnknownComponents`.

Scene documents preserve saved entity ids when first opened, including while the game's module is still building.
Missing components keep opaque fields, so changing those ids before their types load would leave references such
as the player's camera pointing at missing entities. Saving and reloading the module keep those references valid.

## Scripts

Script assets open in the Code panel (double-click them, or **Window > Code**), or in the user's own code editor.

**The Code panel** gives each open script a tab with its own text undo history, line numbers, a monospaced font and
syntax highlighting that works on unfinished source (`editor/code/`, on
[ImGuiColorTextEdit](https://github.com/goossens/ImGuiColorTextEdit), fetched at a pinned commit; the runtime never
links it). While it has focus, **Ctrl+S** saves its script, **Ctrl+W** closes the tab, and the Edit shortcuts act on
its text; **Ctrl** and the wheel resize the text. Saving writes the file and imports it, and tabs follow their assets
when they move or are renamed. Closing a tab with unsaved text, quitting or switching projects asks whether to save;
files changed outside with local edits must be reloaded or copied before saving. UTF-8 BOMs, tabs, line endings and
the final newline are kept.

- **Diagnostics**: shortly after typing stops, the source is parsed without running it (`CheckScriptSyntax()`), and
  the first syntax error shows below the text, its line marked.
- **Breakpoints**: **F9** toggles one at the cursor's line, for every script running from that file
  (`SetScriptBreakpoints()`). A running script that reaches one finishes its hook, the game pauses at the end of the
  frame, and the panel shows the call stack with each call's locals as they were. **Continue** resumes; **Next Frame**
  runs one more frame.
- **Profiling**: the Profiler panel times every hook as a `<script>.<hook>` scope; **Script functions** also profiles
  each function of the running scripts (`SetScriptProfiling()`), which costs time, so it is off by default.
- **Reloading**: scripts running in Play swap in saved code before their next hook, keeping the state their language
  keeps (see [scripting](scripting.md)); `OnStart()` does not run again, coroutines and event subscriptions end, and
  the optional `OnReload()` hook runs. Code that fails to compile leaves the old code running.

**External editors.** **File > Open in External Editor** opens the project, and the asset browser's
**Open in External Editor** opens a file, with the command set in **Preferences > Scripts > External editor**: the
program and its arguments, where `{project}`, `{file}` and `{line}` go (`code {project} --goto {file}:{line}`,
`zed {file}`, `"C:\Program Files\Editor\editor.exe" {file}`); a command naming neither gets what to open last. Empty, the
project opens in the file manager and files with the application the system opens them with. The editor launches the
command on its own (`Process::Launch()`), so it keeps running after the editor exits, and first points the project's
`.luaurc` at the engine's Luau types, so the editor's Luau language server knows the engine's API (see
[writing scripts in other editors](scripting.md#writing-scripts-in-other-editors)). `MakeExternalEditorCommand()`
(`editor/project/external_editor.h`) builds the command.

## Play mode

Playing a document (`PlaySession`) copies its scene and loads the copy into the application, which updates it every
frame with the project's settings sections, while the editor's windows show and can edit the copy. The game's view
gets the keyboard and mouse while it has focus; Escape takes them back. Playing can be paused and stepped a frame at
a time. Stopping throws the copy away, so the document is as it was.

## Testing the editor

Headless tests drive panels with `HeadlessImGui` (`tests/editor/headless_imgui.h`), which runs ImGui frames without a
GPU and clicks, drags and types at the rectangles panels expose. `spot_editor --screenshot <file> [--frames <n>]`
saves a picture of the window after some frames and quits, to check the interface.

## Particle authoring

Add a ParticleSystem from the component search. Its selected entity previews in edit mode; the
inspector provides Play/Pause, Restart and Seek without altering authored data or running gameplay.
Curve plots and gradient strips accompany editable keys. See [Particles](particles.md).

Runtime UI has a creation submenu, canvas preview, rectangle handles and anchor presets with normal undo/prefab edits; see [Runtime UI and text](runtime_ui.md#editor-authoring).

## Animation

Import glTF skins/clips, then create an Animator Controller in the asset browser. Its inspector
contains a draggable graph and transition/parameter editing; Apply and undo update the source
asset. Clip sub-assets have key/event timelines and can preview on a selected scene rig without
changing its authored clip. Animator components preview in edit mode and expose live parameters
in Play. Picking and selection outlines follow the deformed mesh. See [animation](animation.md).
