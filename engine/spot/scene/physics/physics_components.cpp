#include "spot/scene/physics/physics_components.h"

#include "spot/scene/component_registry.h"
#include "spot/scene/entity.h"
#include "spot/scene/physics/character_controller.h"
#include "spot/scene/physics/collider.h"
#include "spot/scene/physics/joint.h"
#include "spot/scene/physics/rigidbody.h"

namespace Spot {

void RegisterPhysicsComponents() {
    RegisterComponent<Rigidbody>("Rigidbody");
    RegisterComponent<BoxCollider>("BoxCollider");
    RegisterComponent<SphereCollider>("SphereCollider");
    RegisterComponent<CapsuleCollider>("CapsuleCollider");
    RegisterComponent<MeshCollider>("MeshCollider");
    RegisterComponent<FixedJoint>("FixedJoint");
    RegisterComponent<HingeJoint>("HingeJoint");
    RegisterComponent<DistanceJoint>("DistanceJoint");
    RegisterComponent<SpringJoint>("SpringJoint");
    RegisterComponent<SwingTwistJoint>("SwingTwistJoint");
    RegisterComponent<CharacterController>("CharacterController");
}

} // namespace Spot
