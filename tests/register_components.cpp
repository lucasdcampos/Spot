#include <spot.h>
#include <spot/scene/scripting/scripting_components.h>

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace {

class ComponentRegistrations final : public Catch::EventListenerBase {
public:
    using EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo&) override {
        Spot::RegisterAnimationComponents();
        Spot::RegisterAudioComponents();
        Spot::RegisterNetworkComponents();
        Spot::RegisterParticleComponents();
        Spot::RegisterPhysicsComponents();
        Spot::RegisterPhysics2DComponents();
        Spot::RegisterRenderComponents();
        Spot::RegisterUiComponents();
        Spot::RegisterScriptComponents();
    }
};

CATCH_REGISTER_LISTENER(ComponentRegistrations)

} // namespace
