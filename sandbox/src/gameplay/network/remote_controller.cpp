#include "gameplay/network/remote_controller.h"

#include "gameplay/core/pawn.h"
#include "gameplay/core/player_input.h"
#include "gameplay/network/pawn_intents.h"

namespace Gameplay {

SPOT_COMPONENT(RemoteController);

// Applies what remote players' machines sent before the fixed steps, as local players' input is read then.
class RemoteIntentSystem final : public Spot::SceneSystem {
protected:
    void OnBeforeFixedSteps(float /*deltaTime*/) override {
        GetScene().ForEachComponent<RemoteController>([](RemoteController& controller) {
            controller.RunGuarded("ApplyReceived", [&] { controller.ApplyReceived(); });
        });
    }
};

void RemoteController::OnStart() {
    GetScene().GetOrAddSystem<RemoteIntentSystem>();
    Controller::OnStart();
}

void RemoteController::Receive(std::uint32_t frame, std::string intents) {
    if (frame <= m_lastFrame) {
        return;
    }
    m_lastFrame = frame;
    m_received.push_back(std::move(intents));
}

void RemoteController::ApplyReceived() {
    Pawn* target = GetPawn();
    for (const std::string& intents : m_received) {
        if (target) {
            Spot::BitReader reader(std::as_bytes(std::span(intents.data(), intents.size())));
            PawnIntents::Read(*target, reader);
        }
    }
    m_received.clear();
}

void RemoteController::OnUnpossess(Pawn& target) {
    // As a local player's pawn does when let go: its parts drop what they held.
    target.GetEntity().ForEachComponent<Spot::Component>([](Spot::Component& component) {
        if (auto* listener = dynamic_cast<PlayerInputListener*>(&component)) {
            listener->OnPlayerUnpossess();
        }
    });
}

} // namespace Gameplay
