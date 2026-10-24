#include "spot/scene/animation/animation_module.h"

#include "spot/animation/animation_assets.h"
#include "spot/runtime/application.h"
#include "spot/scene/animation/animation_components.h"

namespace Spot {

void AnimationModule::OnStart() {
    RegisterAnimationAssets(GetApplication().GetAssets());
    RegisterAnimationComponents();
}

} // namespace Spot
