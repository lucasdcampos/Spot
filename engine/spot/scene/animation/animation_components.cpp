#include "spot/scene/animation/animation_components.h"

#include "spot/scene/animation/animator.h"
#include "spot/scene/animation/bone_attachment.h"
#include "spot/scene/animation/ragdoll.h"
#include "spot/scene/animation/skinned_mesh_renderer.h"
#include "spot/scene/component_registry.h"
#include "spot/scene/entity.h"

namespace Spot {

void RegisterAnimationComponents() {
    RegisterComponent<BoneAttachment>("BoneAttachment");
    RegisterComponent<Ragdoll>("Ragdoll");
    RegisterComponent<Animator>("Animator");
    RegisterComponent<SkinnedMeshRenderer>("SkinnedMeshRenderer");
}

} // namespace Spot
