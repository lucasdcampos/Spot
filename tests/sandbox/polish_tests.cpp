#include "playground_fixture.h"

#include "gameplay/camera/first_person_view.h"
#include "gameplay/core/physics_drive.h"
#include "gameplay/inventory/inventory.h"
#include "gameplay/inventory/view_model.h"
#include "gameplay/weapons/firearm.h"

#include <catch2/catch_approx.hpp>

TEST_CASE("Firearm recoil follows the rendered aim, pauses and recovers", "[sandbox][polish]") {
    SandboxTests::Playground game;
    game.Run(90);
    auto* player = game.scene.FindEntity("Player");
    auto* control = player->GetComponent<Gameplay::FirstPersonView>();
    auto* holder = player->GetComponent<Gameplay::Inventory>();
    auto* view = player->GetComponent<Gameplay::ViewModel>();
    holder->Select(1);
    game.Run(30);
    const auto before = control->GetAimDirection();
    SandboxTests::Fire(*holder);
    CHECK(control->GetRecoil() > 0);
    CHECK(control->GetAimDirection().y > before.y);
    game.Step();
    CHECK(view->GetRecoil() > 0);
    const auto* camera = player->FindChild("Head")->GetComponent<Spot::Camera>();
    CHECK(Spot::Length(camera->GetCamera3D().GetForward() - control->GetAimDirection()) < .001f);
    const float recoil = control->GetRecoil();
    const auto motion = view->GetMotionOffset();
    auto* ui = game.scene.FindComponent<Gameplay::PauseMenu>();
    ui->Open();
    game.Run(30);
    CHECK(control->GetRecoil() == recoil);
    CHECK(Spot::Length(view->GetMotionOffset() - motion) < .00001f);
    ui->Close();
    game.Run(30);
    CHECK(control->GetRecoil() == Catch::Approx(0).margin(.0001f));
    CHECK(view->GetRecoil() == Catch::Approx(0).margin(.0001f));
    CHECK(Spot::Length(control->GetAimDirection() - before) < .001f);
    SandboxTests::GetFirearm(*holder)->Refill();
    SandboxTests::Fire(*holder);
    control->SetEnabled(false);
    CHECK(control->GetRecoil() == 0);
}

TEST_CASE("Held weapon motion is bounded, settles and reduces when aiming", "[sandbox][polish]") {
    SandboxTests::Playground game;
    game.Run(90);
    auto* player = game.scene.FindEntity("Player");
    auto* holder = player->GetComponent<Gameplay::Inventory>();
    auto* view = player->GetComponent<Gameplay::ViewModel>();
    holder->Select(1);
    game.Run(30);
    game.input.OnEvent(Spot::MouseMovedEvent{.position = {}, .delta = {60, 0}});
    game.Step();
    const float hip = Spot::Length(view->GetMotionOffset());
    CHECK(hip > .001f);
    CHECK(hip < view->maxSway);
    game.Run(60);
    CHECK(Spot::Length(view->GetMotionOffset()) < .0001f);
    game.input.OnEvent(Spot::MouseButtonPressedEvent{Spot::MouseButton::Right});
    game.Run(15);
    REQUIRE(SandboxTests::GetFirearm(*holder)->IsAiming());
    game.input.OnEvent(Spot::MouseMovedEvent{.position = {}, .delta = {60, 0}});
    game.Step();
    CHECK(Spot::Length(view->GetMotionOffset()) < hip * .5f);
    game.input.OnEvent(Spot::MouseButtonReleasedEvent{Spot::MouseButton::Right});
    game.Run(30);
    game.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::W, false});
    game.Run(20);
    CHECK(Spot::Length(view->GetMotionOffset()) > .0005f);
    game.input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::W});
    game.Run(60);
    CHECK(Spot::Length(view->GetMotionOffset()) < .0001f);
    view->SetEnabled(false);
    CHECK(Spot::Length(view->GetMotionOffset()) == 0);
}

TEST_CASE("Prop drive keeps its settling time across fixed step rates", "[sandbox][polish]") {
    auto settle = [](int rate) {
        Spot::Scene scene;
        auto& entity = scene.CreateEntity();
        entity.AddComponent<Spot::BoxCollider>();
        auto& body = entity.AddComponent<Spot::Rigidbody>();
        body.SetGravityScale(0);
        const float step = 1.0f / static_cast<float>(rate);
        for (int i = 0; i < rate / 2; ++i) {
            const auto position = entity.GetTransform().GetWorldPosition();
            Gameplay::PhysicsDrive::Toward(body, position, {1, 0, 0}, {}, step, 100);
            entity.GetTransform().SetWorldPosition(position + body.GetLinearVelocity() * step);
        }
        return entity.GetTransform().GetWorldPosition().x;
    };
    CHECK(settle(30) == Catch::Approx(settle(120)).margin(.00001f));
    CHECK(settle(60) > .99f);
}
