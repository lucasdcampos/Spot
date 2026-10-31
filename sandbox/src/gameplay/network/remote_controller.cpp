#include "gameplay/network/remote_controller.h"

#include "gameplay/core/pawn.h"
#include "gameplay/core/player_input.h"
#include "gameplay/network/pawn_intents.h"
#include "gameplay/network/player_messages.h"

#include <algorithm>

namespace Gameplay {

SPOT_COMPONENT(RemoteController);

namespace {

// How far ahead of time passing here a player's steps may run, in seconds, as when they arrive together after a
// delay; and how many may wait before they are applied at once, as a machine's clock runs a little fast.
constexpr float MaxStepLead = 0.25f;
constexpr std::size_t MaxWaitingSteps = 4;
// Steps waiting beyond these are dropped; the player's machine is corrected.
constexpr std::size_t MaxQueuedSteps = 30;

} // namespace

// Applies what remote players' machines sent before the fixed steps, as local players' input is read then.
class RemoteIntentSystem final : public Spot::SceneSystem {
protected:
    void OnBeforeFixedSteps(float deltaTime) override {
        GetScene().ForEachComponent<RemoteController>([deltaTime](RemoteController& controller) {
            controller.RunGuarded("ApplyReceived", [&] { controller.ApplyReceived(deltaTime); });
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

void RemoteController::ReceiveStep(std::uint32_t step, std::string intents) {
    if (step <= m_lastStep) {
        return;
    }
    m_lastStep = step;
    m_steps.emplace_back(step, std::move(intents));
    while (m_steps.size() > MaxQueuedSteps) {
        m_steps.pop_front();
    }
}

Pawn* RemoteController::GetPlayerPawn() const {
    const Spot::Entity* entity = GetScene().GetEntity(m_playerPawn);
    return entity ? entity->GetComponent<Pawn>() : nullptr;
}

void RemoteController::OnPossess(Pawn& target) {
    if (m_playerPawn == Spot::EntityId::Invalid) {
        m_playerPawn = target.GetEntity().GetId();
    }
    Spot::NetworkSystem* system = Spot::Network::Get(GetScene());
    if (Spot::NetworkObject* object = Spot::Network::GetObject(target.GetEntity()); system && object) {
        system->SetOwner(*object, m_peer);
    }
}

void RemoteController::ApplyReceived(float deltaTime) {
    Pawn* target = GetPawn();
    const bool predicted = target && PawnIntents::IsPredicted(*target);
    if (predicted) {
        ApplySteps(*target, deltaTime);
    } else {
        m_steps.clear();
    }
    // Then the frames', as items are used where the view looks.
    const PawnIntents::Set set = predicted ? PawnIntents::Set::PerFrame : PawnIntents::Set::All;
    for (const std::string& intents : m_received) {
        if (target) {
            Spot::BitReader reader(std::as_bytes(std::span(intents.data(), intents.size())));
            PawnIntents::Read(*target, reader, set);
        }
    }
    m_received.clear();
}

void RemoteController::ApplySteps(Pawn& target, float deltaTime) {
    const float step = GetScene().GetFixedTimeStep();
    m_stepTime = std::min(m_stepTime + deltaTime, MaxStepLead);
    bool stepped = false;
    while (!m_steps.empty() && (m_stepTime >= step || m_steps.size() > MaxWaitingSteps)) {
        const auto& [number, intents] = m_steps.front();
        Spot::BitReader reader(std::as_bytes(std::span(intents.data(), intents.size())));
        PawnIntents::Read(target, reader, PawnIntents::Set::PerStep);
        PawnIntents::Step(target, step, false);
        m_appliedStep = number;
        m_stepTime = std::max(m_stepTime - step, -MaxStepLead);
        m_steps.pop_front();
        stepped = true;
    }
    Spot::NetworkSystem* system = Spot::Network::Get(GetScene());
    Spot::NetworkObject* object = Spot::Network::GetObject(target.GetEntity());
    if (!stepped || !system || !object) {
        return;
    }
    Spot::BitWriter state;
    PawnIntents::WriteStepState(target, state);
    const std::vector<std::byte> bytes = state.TakeBytes();
    system->SendToOwner(*object,
                        StepStateMessage{.step = m_appliedStep,
                                         .position = target.GetEntity().GetTransform().GetWorldPosition(),
                                         .state = std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size())},
                        Spot::NetDelivery::Unreliable);
}

void RemoteController::OnUnpossess(Pawn& target) {
    m_received.clear();
    m_steps.clear();
    Spot::NetworkSystem* system = Spot::Network::Get(GetScene());
    if (Spot::NetworkObject* object = Spot::Network::GetObject(target.GetEntity());
        system && object && &target != GetPlayerPawn() && object->GetOwner() == m_peer) {
        system->SetOwner(*object, Spot::NetPeerId::Invalid);
    }
    // As a local player's pawn does when let go: its parts drop what they held.
    target.GetEntity().ForEachComponent<Spot::Component>([](Spot::Component& component) {
        if (auto* listener = dynamic_cast<PlayerInputListener*>(&component)) {
            listener->OnPlayerUnpossess();
        }
    });
}

} // namespace Gameplay
