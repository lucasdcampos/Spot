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
Use `Renderer2D::DrawWorldText()` inside a begun pass for custom rendering.

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
before `Scene::DrawUi()`; canvases draw in increasing `settings.order`, ties in creation order, and
children draw depth-first in sibling order, parents first. Later elements appear over earlier ones.
Inactive entities and disabled rectangles are excluded. Resize and hierarchy changes are resolved
on the next pass. Canvas/rect fields serialize normally, including prefab and inspector editing.

## Widgets

Widgets are components on entities with a `UiRect`. They also draw before scene startup, allowing
canvas previews without running gameplay. `UiImage` fills with a tint or draws a texture; `borders`
(left, bottom, right, top) enable nine-slice images with source-pixel borders. Tiny destinations
shrink the border pairs proportionally. `UiText` has a font asset, UTF-8 text, display size, color,
left/center/right alignment, word wrapping (long words break), and overflow/clip/ellipsis modes.
Text and image geometry are cached until their inputs change. `LayoutUiText()` and
`BuildUiNineSlice()` expose the same mechanisms without scene components.

`UiButton::OnClick`, `UiToggle::OnValueChanged` and `UiSlider::OnValueChanged` are signals. Keep
the returned `SignalConnection` to stay subscribed. Buttons have normal, hovered, pressed,
focused and disabled colors; all controls share `UiControlStyle`. Sliders have a range, keyboard
step and horizontal/vertical orientation. Programmatic setters are silent unless `notify` is true.
`UiWidget::HandleUiEvent()` permits code-only activation as well as input dispatch; callbacks
are guarded and a throwing subscriber faults only its widget. Destruction during callbacks is deferred.

`UiInputField` accepts UTF-8, edits complete code points at its caret, limits character count,
filters single-line control characters, and emits change and submit signals. Its placeholder and
caret are rendered by the runtime. `UiScrollView` clips all descendants to its rectangle and shifts
its content according to a bounded offset. Content size is in logical pixels; zero dimensions fit
the viewport. Nested scroll clips intersect. The renderer flushes when the scissor changes, retaining
draw order and restoring the full viewport after the UI pass. Focus, pointer routing, keyboard and
gamepad navigation follow in M12.5.

Layout groups, input and editor authoring follow in M12.4–M12.7.
