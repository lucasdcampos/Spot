# Editor

The editor (`spot_editor`, `editor/`) is a Dear ImGui application for making games from projects: it edits scenes
and assets, builds and reloads the game's code, and plays scenes. Everything but `main.cpp` is the
`spot_editor_core` library, which the editor's tests link.

It follows the rules games do: it uses only the engine's public API and never reaches a backend, and ImGui stays in
the editor, so the runtime never depends on it. It is itself an `Application` (with `renderScenes` off, since it
shows scenes in its own views).

## Projects

Without a project open, the **hub** lists recent projects and creates or opens one (from the templates in
`spot_tools`). An open project (`EditorProject`) has its `Project` (see
[Tools and the command line](tools_and_command_line.md)), an `AssetDatabase` that imports its sources as they change,
an `AssetManager` that loads the imports, and, for projects with code, a `ModuleHost` that builds and loads the
game's code.

## Documents, undo and ids

Each open scene is a `SceneDocument` in the `SceneWorkspace`, with its own view, selection and history. Every change
goes through a command (`editor/scene/scene_commands.h`): adding, deleting, reparenting and renaming entities,
editing transforms and components, and so on. A `CommandHistory` undoes and redoes them, and knows whether the
document changed since it was saved.

The editor never keeps pointers to entities or components across frames. Commands hold `EntityId`s, component
indices and saved JSON, so undo history stays valid when entities are deleted and brought back with their ids, or
when the game's code is reloaded and every component is recreated. Unsaved scenes are backed up under
`.spot/backups`, and offered back after a crash.

## Panels

| Panel | What it does |
|---|---|
| Scene views | Show a document through an editor camera, with the grid, the selection's outline, transform gizmos (move, turn, scale), picking by clicking, and the shapes queued with `DebugDraw` |
| Hierarchy | The entities as a tree: select, rename, reorder and reparent by dragging, create, duplicate and delete |
| Inspector | The selected entities' name, tags, layer, activity, transform and components; draws each component's fields from its `Serialize()` through the `InspectorBackend`; edits several entities at once; marks missing and faulted components |
| Assets | Browses the project's sources with thumbnails; moves, renames, duplicates, deletes and imports files dropped from the OS; inspects import settings |
| Console | The log, filtered by level and text |
| Project Settings | The project's name, version and startup scene, every settings section, drawn through the inspector backend, and the layers' names and collision matrix |

## Game code

For a project with code (`src/CMakeLists.txt`), the `ModuleHost` builds the game's code in the background with CMake
whenever it changes, into a game module (a shared library linking `spot_shared`), and loads it from a copy, so the
next build can replace the file meanwhile. Before loading a new module, the open scenes are saved to text and their
components destroyed; after, they load again with the new types. Components whose type the module no longer has stay
as `MissingComponent`s that keep their data.

## Play mode

Playing a document (`PlaySession`) copies its scene and loads the copy into the application, which updates it every
frame with the project's settings sections, while the editor's windows show and can edit the copy. The game's view
gets the keyboard and mouse while it has focus; Escape takes them back. Playing can be paused and stepped a frame at
a time. Stopping throws the copy away, so the document is as it was.

## Testing the editor

Headless tests drive panels with `HeadlessImGui` (`tests/editor/headless_imgui.h`), which runs ImGui frames without a
GPU and clicks, drags and types at the rectangles panels expose. `spot_editor --screenshot <file> [--frames <n>]`
saves a picture of the window after some frames and quits, to check the interface.
