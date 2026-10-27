#include "gameplay/core/player_controller.h"

#include "gameplay/core/input_context.h"
#include "gameplay/core/pawn.h"
#include "gameplay/core/player_input.h"

#include <algorithm>

namespace Gameplay {

namespace {

template <typename F>
void VisitInput(Spot::Entity& entity, const Pawn& pawn, F&& visit) {
    if (Pawn::Of(entity) != &pawn) {
        return;
    }
    Spot::Scene& scene = entity.GetScene();
    const Spot::EntityId id = entity.GetId();
    entity.ForEachComponent<Spot::Component>([&](Spot::Component& component) {
        if (auto* listener = dynamic_cast<PlayerInputListener*>(&component);
            listener && component.HasStarted() && component.IsActiveAndEnabled()) {
            visit(*listener);
        }
    });
    if (Spot::Entity* current = scene.GetEntity(id)) {
        current->ForEachChild([&](Spot::Entity& child) { VisitInput(child, pawn, visit); });
    }
}

} // namespace

SPOT_COMPONENT(PlayerController);

class PlayerInputSystem final : public Spot::SceneSystem {
protected:
    void OnBeforeFixedSteps(float /*deltaTime*/) override {
        GetScene().ForEachComponent<PlayerController>(
            [](PlayerController& player) { player.RunGuarded("OnPlayerInput", [&] { player.ReadInput(true); }); });
    }
};

void PlayerController::OnStart() {
    GetScene().GetOrAddSystem<PlayerInputSystem>();
    Controller::OnStart();
}

PlayerController::~PlayerController() {
    DetachContexts();
}

bool PlayerController::IsTakenAbove(const InputContext& context, std::string_view action) const {
    return std::ranges::any_of(m_contexts, [&](const InputContext* other) {
        return other != &context && other->m_desc.priority > context.m_desc.priority && other->Takes(action);
    });
}

bool PlayerController::IsConsumed(std::string_view action) const {
    return std::ranges::find(m_consumed, action) != m_consumed.end();
}

void PlayerController::OnPossess(Pawn& target) {
    ShowCamera(target, true);
}

void PlayerController::OnUnpossess(Pawn& target) {
    ShowCamera(target, false);
    VisitInput(target.GetEntity(), target, [](PlayerInputListener& listener) { listener.OnPlayerUnpossess(); });
}

void PlayerController::OnUpdate(float /*deltaTime*/) {
    ReadInput(false);
}

void PlayerController::ReadInput(bool beforeFixedSteps) {
    if (Pawn* target = GetPawn()) {
        VisitInput(target->GetEntity(), *target, [&](PlayerInputListener& listener) {
            // Using a control can transfer possession, as leaving a vehicle does.
            if (GetPawn() == target && listener.IsInputBeforeFixedSteps() == beforeFixedSteps) {
                listener.OnPlayerInput(*this);
            }
        });
    }
}

void PlayerController::OnLateUpdate(float /*deltaTime*/) {
    // Once every part has read this frame's input: what closed during it stops taking its actions.
    m_consumed.clear();
    std::erase_if(m_contexts, [](InputContext* context) {
        if (!context->m_closing) {
            return false;
        }
        context->m_controller = nullptr;
        context->m_closing = false;
        return true;
    });
}

void PlayerController::OnDestroy() {
    Controller::OnDestroy();
    DetachContexts();
}

void PlayerController::ShowCamera(const Pawn& target, bool show) {
    if (Spot::Entity* entity = GetScene().GetEntity(target.GetViewCamera())) {
        if (auto* camera = entity->GetComponent<Spot::Camera>()) {
            camera->SetEnabled(show);
        }
    }
}

void PlayerController::DetachContexts() {
    for (InputContext* context : m_contexts) {
        context->m_controller = nullptr;
        context->m_closing = false;
    }
    m_contexts.clear();
}

} // namespace Gameplay
