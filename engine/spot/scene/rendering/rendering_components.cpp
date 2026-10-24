#include "spot/scene/rendering/rendering_components.h"

#include "spot/scene/component_registry.h"
#include "spot/scene/rendering/camera.h"
#include "spot/scene/rendering/clouds.h"
#include "spot/scene/rendering/fog.h"
#include "spot/scene/rendering/light.h"
#include "spot/scene/rendering/mesh_renderer.h"
#include "spot/scene/rendering/post_processing.h"
#include "spot/scene/rendering/sky.h"
#include "spot/scene/rendering/sprite_animator.h"
#include "spot/scene/rendering/sprite_renderer.h"
#include "spot/scene/rendering/surface_renderer.h"
#include "spot/scene/rendering/text_renderer.h"
#include "spot/scene/rendering/tilemap.h"

namespace Spot {

void RegisterRenderComponents() {
    RegisterComponent<MeshRenderer>("MeshRenderer");
    RegisterComponent<SurfaceRenderer>("SurfaceRenderer");
    RegisterComponent<AmbientLight>("AmbientLight");
    RegisterComponent<DirectionalLight>("DirectionalLight");
    RegisterComponent<PointLight>("PointLight");
    RegisterComponent<SpotLight>("SpotLight");
    RegisterComponent<Sky>("Sky");
    RegisterComponent<Clouds>("Clouds");
    RegisterComponent<Fog>("Fog");
    RegisterComponent<PostProcessing>("PostProcessing");
    RegisterComponent<TextRenderer>("TextRenderer");
    RegisterComponent<SpriteRenderer>("SpriteRenderer");
    RegisterComponent<SpriteAnimator>("SpriteAnimator");
    RegisterComponent<Tilemap>("Tilemap");
    RegisterComponent<Camera>("Camera");
}

} // namespace Spot
