# Runtime UI and text

Start with `Font`, `Renderer2D::DrawString()` and `TextRenderer` for text. Fonts load from imported
assets; the version 2 font importer keeps the original font bytes and size so layout can read kerning.
`FontAtlas` produces signed distance glyphs for printable ASCII and Latin-1 (other characters fall
back to `?`). The atlas stores distances in alpha, with the contour at 128/255 and six pixels of
padding. The renderer reconstructs coverage using screen derivatives, so the same atlas supports
small labels and large titles without blocky edges. Font sizes must be finite and between 1 and
256 pixels; 32–64 pixels are practical atlas resolutions. Extra imported sizes remain available.

`TextRenderer` is a serializable component: set its font, UTF-8 text, color, pixel scale, camera-facing
flag and order. Its entity origin is the top-left of the text; one atlas pixel occupies `pixelScale`
world units. Normal text uses the entity world matrix, including parent transforms. Camera-facing
text uses the camera rotation while keeping the entity's world position and scale. The engine and
editor draw world text after sprites in the HDR pass, ordered by `order` and stable traversal;
like sprites, it overlays meshes without depth testing. It still receives post-processing.
Use `DrawWorldText(Renderer2D&, Scene&, const Camera3D&)` inside a begun pass for custom rendering.

## Canvas and rectangles

Add a `Canvas` to a root entity and `UiRect` to its child elements. `UiRect::layout` describes
normalized anchor minimum/maximum, pivot, an offset and size delta in logical canvas pixels.
Coordinates grow right and up. Equal anchors give a fixed-size rectangle; separate anchors stretch
between points on the parent, with `sizeDelta` added to the stretched size. The offset moves the
pivot relative to the anchored point. Set anchors to (0,0)–(1,1), pivot to (0.5,0.5), and size delta
to (-20,-20) for ten-pixel margins on every side.

UI uses entity hierarchy and activity, but does not read or modify entity `Transform`. This keeps
screen layout independent of a world's transforms and makes prefab placement predictable. Each
canvas covers the viewport. Constant-pixel scaling uses `settings.scale`; reference-resolution
scaling multiplies it by the smaller viewport/reference ratio to preserve aspect ratio. All offsets
and sizes scale together. Nested canvases are independent viewport roots with their own order.

`ComputeUiRect()` and `ComputeUiScale()` are code-only mechanisms, usable without scenes or a GPU.
`UiSystem::Layout(viewSize)` builds transient screen rectangles keyed by entity id. Layout runs
before `UiSystem::DrawUi()`; canvases draw in increasing `settings.order`, ties in creation order, and
children draw depth-first in sibling order, parents first. Later elements appear over earlier ones.
Inactive entities and disabled rectangles are excluded. Resize and hierarchy changes are resolved
on the next pass. Canvas/rect fields serialize normally, including prefab and inspector editing.

## Widgets

Widgets are components on entities with a `UiRect`. They also draw before scene startup, allowing
canvas previews without running gameplay. `UiImage` fills with a tint or draws a texture; `borders`
(left, bottom, right, top) enable nine-slice images with source-pixel borders. Tiny destinations
shrink the border pairs proportionally. `UiText` has a font asset, UTF-8 text, display size, color,
left/center/right alignment, top/middle/bottom vertical alignment (middle centers a line in a panel or
button), word wrapping (long words break), and overflow/clip/ellipsis modes.
Text and image geometry are cached until their inputs change. `LayoutUiText()` and
`BuildUiNineSlice()` expose the same mechanisms without scene components.

`UiButton::OnClick`, `UiToggle::OnValueChanged` and `UiSlider::OnValueChanged` are signals. Keep
the returned `SignalConnection` to stay subscribed. Buttons have normal, hovered, pressed,
focused and disabled colors; all controls share `UiControlStyle`. A style's `texture`, nine-sliced by its
`borders` as `UiImage` does, replaces the flat rectangles a control draws, tinted with the state's color, so buttons
can be rounded panels. Sliders have a range, keyboard
step and horizontal/vertical orientation. Programmatic setters are silent unless `notify` is true.
`UiWidget::HandleUiEvent()` permits code-only activation as well as input dispatch; callbacks
are guarded and a throwing subscriber faults only its widget. Signal dispatch reuses snapshot
buffers per nesting depth, including after exceptions, so repeated UI events reuse storage.
Destruction during callbacks is deferred.

`UiInputField` accepts UTF-8, edits complete code points at its caret, limits character count,
filters single-line control characters, and emits change and submit signals. Its placeholder and
caret are rendered by the runtime. `UiScrollView` clips all descendants to its rectangle and shifts
its content according to a bounded offset. Content size is in logical pixels; zero dimensions fit
the viewport. Nested scroll clips intersect. The renderer flushes when the scissor changes, retaining
draw order and restoring the full viewport after the UI pass. Focus, pointer routing, keyboard and
gamepad navigation follow in M12.5.

## Automatic layout

Put `UiLayoutGroup` on a panel to arrange its direct active children with enabled rectangles.
Choose horizontal, vertical or grid direction; set padding (left, bottom, right, top), spacing,
alignment, and expansion along each axis. Horizontal order runs left to right, vertical order
top to bottom, and grid order fills rows from the top-left using `columns` and `cellSize`.
Group layout overrides children's rectangle positions transiently, keeping their authored anchors
and sizes intact. Expansion splits the available primary dimension equally (after spacing).

`UiSizeFitter` uses the preferred size of a text, texture or group, per enabled axis. Groups measure
bottom-up before layout resolves top-down, so nested fitted groups can size their parents. Text
preferred size measures unwrapped text; disable width fitting to retain a wrapping container.
Inactive children do not contribute. Groups retain their child and geometry buffers across frames;
`Entity::ForEachChild()` permits allocation-free hierarchy traversal. `MeasureUiLayoutGroup()` and
`ArrangeUiLayoutGroup()` expose the mechanism separately; an output-vector overload reuses storage.
Group and fitter state serializes normally, while generated rectangles remain transient.

## Input and navigation

The application routes viewport events to scene UI before gameplay input/action updates. Use
`UiInputSystem::HandleUiEvent(event, viewSize, input)` for custom hosts: pointer coordinates grow down from
the viewport's top-left, in pixels. The editor converts its played viewport coordinates too.
`Scene::GetOrAddSystem<UiSystem>()` and `Scene::GetOrAddSystem<UiInputSystem>()` lazily create each service exactly once.
While no modal canvas is active and no widget can be focused, as with a HUD of labels, input skips laying the
interface out.
Handled events clear the matching game control via `InputState::ConsumeEvent()`, avoiding held
buttons when ownership changes. Owned key/button releases are also consumed after a menu closes. Pointer hit testing follows reverse draw order and inherited clips;
a click activates only when released on the element that received the press. Sliders support drag.

Tab/Shift-Tab traverse sibling order; arrows and gamepad D-pad navigate spatially. Return/Space and
South activate. D-pad/arrows adjust sliders and scroll views; shoulder buttons traverse, and left
stick threshold crossings navigate. Focusing an offscreen scroll child reveals it. Focus is stored
by entity id and validated after changes. Fields handle text/caret events and request native text
input; their capture releases the cursor while retaining the game's requested lock.

A canvas with `settings.modal` owns input until disabled or hidden; only its controls can receive
focus. `dismissKey` defaults to Escape. `UiInputSystem::OnCancel` identifies the modal canvas,
allowing game code to close it. Escape first releases text focus; gamepad East/Start cancels a modal. A dismissal key
other than Escape, such as a letter, is typed into a focused field instead of closing its canvas.
The code-only input system can bind a state with `SetInputState()`. That state must outlive the
binding; detach it before destruction. UI input and component callbacks are fault-contained.

## Editor authoring

The entity creation menu has a UI section for canvases and all widgets/layout groups. Creating a
widget reuses an existing canvas or creates one when needed, as one undoable command, and selects
the widget on redo too. Buttons and toggles include label children; scroll views include a content
rectangle. If the project contains `fonts/FiraSans-Regular.ttf`, labels refer to it automatically;
otherwise assign a font in the inspector. World Text is available under 3D Object.

Turn on **UI** in the scene viewport toolbar to preview screen-space canvases without starting
components. Click a rectangle to select it. Drag the selected rect's interior to move it, or its
four corner handles to resize; yellow circles show anchors and the blue point shows the pivot.
A drag records one component edit for undo, including prefab overrides. Escape cancels the drag.
Group-managed positions are edited through the group's settings; fitters retain control of their
fitted dimensions. Drag state resolves entity/component ids and document revisions each frame.

The **Anchors** popup offers nine fixed positions and full stretching. **Keep rectangle** preserves
the visible rectangle while changing its anchoring; disable it to move to the preset's position.
Canvas scale, reference resolution, modal input and dismissal key use the ordinary inspector.

## Sandbox interface

The sandbox instantiates `sandbox/assets/prefabs/interface.prefab` under its HUD entity. It is a
normal editable UI tree with bound widget ids, label children and inactive catalog/binding templates.
Open it as a prefab in the editor, enable a menu canvas to preview it, and use the same rectangle
handles and inspector. `RuntimeInterface` fills the templates from the spawn manifest and input
map. The HUD shows a spreading crosshair, hit marker, the item in hand with its rounds or tool mode, a reload
bar, context controls, the clock, the controls on H and, on F11, statistics; see [Sandbox](sandbox.md).

Q or gamepad Back opens the spawn menu; a sidebar of Props/Lamps/Weapons and a search field filter its catalog.
Clicking a thumbnail closes the menu and shows the prefab, translucent, where it would go; PropSpawner places it on
Fire and cancels on Aim. Footer buttons undo and clean up.
`MenuThumbnails` renders the prefab's mesh tree through public SceneRenderer into a cached texture,
with a two-item budget per frame and no gameplay simulation. Escape/Start opens pause; Resume,
Escape or gamepad East/Start closes it and restores the previous time scale and cursor request.
Menu opening is routed before simulation, and captures update game actions immediately.

Pause holds sensitivity, FOV, master/effects/music/ambience/interface volumes, three graphics presets
and every action binding (including composite parts). Select a binding, then press a key, mouse or
gamepad control; Escape cancels capture. Settings debounce writes using unscaled time, flush on
closing, and save in the game's user directory as versioned `interface.json`. Rebinds are stored
with them. Headless callers can supply an explicit directory. Runtime graphics changes retain
input bindings and project defaults; both standalone and editor game rendering follow them.

Custom component overlays derive from `UiDrawListener` (`scene/ui/ui_draw_listener.h`) and
override `OnDrawUi(Renderer2D&, Vector2)`. `UiWidget` implements this interface for built-in widgets.

Custom UI listeners draw after `HasStarted()` becomes true, in component addition order outside
canvases and in canvas layout order inside them. Widgets may draw before their first update so
interfaces are visible immediately; disabled, removed and faulted components are skipped.

The custom-overlay pass checks UiDrawListener before finding a component's ancestor canvas. Non-UI gameplay and
physics components therefore incur no hierarchy search during interface drawing. Canvas widgets retain layout
order; custom overlays outside canvases retain component addition order.
