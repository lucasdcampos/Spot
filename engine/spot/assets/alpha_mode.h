#pragma once

namespace Spot {

// How a material's alpha, the base color's times its texture's, covers what is behind it.
enum class AlphaMode {
    // Alpha is ignored: the surface hides what is behind it.
    Opaque,
    // Pixels whose alpha is below the cutoff are not drawn, nor cast shadows, and the others are opaque: leaves,
    // fences and grates, with hard edges.
    Cutout,
    // The surface covers what is behind it as much as its alpha says, and reflects light at full strength over it, as
    // glass does. Transparent surfaces are drawn after the opaque ones, from the farthest to the nearest, and do not
    // hide what is drawn after them or cast shadows.
    Transparent,
};

} // namespace Spot
