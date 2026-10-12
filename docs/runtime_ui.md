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

Screen-space canvases, widgets, layout, input and editor authoring follow in M12.2–M12.7.
