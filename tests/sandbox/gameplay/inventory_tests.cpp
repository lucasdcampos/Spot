#include "gameplay_world.h"

#include "gameplay/core/pawn.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/inventory/held_item.h"
#include "gameplay/inventory/inventory.h"
#include "gameplay/inventory/inventory_elements.h"
#include "gameplay/inventory/view_model.h"
#include "gameplay/inventory/world_model.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

namespace {

// A new item behavior that the inventory, view model and HUD serve without knowing its type.
class CounterItem final : public Gameplay::HeldItem {
public:
    CounterItem() : HeldItem({"Count"}) { displayName = "Counter"; }
    int uses = 0;
    int equipped = 0;
    int unequipped = 0;
    bool active = false;

    Gameplay::ItemStatus DescribeStatus() const override {
        auto status = HeldItem::DescribeStatus();
        status.count = std::to_string(uses);
        return status;
    }

protected:
    void OnEquip() override { ++equipped; }
    void OnUnequip() override { ++unequipped; }
    void OnUse(const Gameplay::ItemInput& input) override {
        active = input.active;
        uses += input.canUse && input.IsPressed("Count") ? 1 : 0;
    }
};

struct InventoryWorld : GameplayTests::World {
    Spot::Entity& entity = scene.CreateEntity("Pawn");
    Gameplay::Pawn& pawn = entity.AddComponent<Gameplay::Pawn>();
    Gameplay::Inventory& inventory = entity.AddComponent<Gameplay::Inventory>();
    CounterItem& first = entity.CreateChild("First").AddComponent<CounterItem>();
    CounterItem& second = entity.CreateChild("Second").AddComponent<CounterItem>();

    InventoryWorld() { Step(); }

    void Use() {
        Gameplay::ItemInput intent;
        intent.active = intent.canUse = true;
        intent.Set("Count", true, true);
        inventory.Use(intent);
    }
};

} // namespace

TEST_CASE("An inventory equips arbitrary item subclasses and routes intents", "[sandbox][gameplay][inventory]") {
    InventoryWorld world;
    REQUIRE(world.inventory.GetEquipped() == &world.first);
    CHECK(world.first.equipped == 1);
    world.Use();
    CHECK(world.first.uses == 1);
    CHECK(world.second.uses == 0);
    world.inventory.Select(world.second);
    CHECK(world.first.unequipped == 1);
    CHECK_FALSE(world.first.IsEquipped());
    CHECK(world.second.IsEquipped());
    world.Use();
    CHECK(world.second.uses == 1);
    world.inventory.Select(-2);
    CHECK(world.inventory.GetEquipped() == &world.first);
    CHECK(world.first.equipped == 2);
}

TEST_CASE("Inventory states gate external item intents", "[sandbox][gameplay][inventory]") {
    InventoryWorld world;
    world.pawn.SetState("Sprinting", true);
    world.Use();
    CHECK(world.first.active);
    CHECK(world.first.uses == 0);
    world.pawn.SetState("Sprinting", false);
    world.Use();
    CHECK(world.first.uses == 1);
    for (const auto* state : {"Dead", "Seated"}) {
        world.pawn.SetState(state, true);
        world.Use();
        CHECK_FALSE(world.first.active);
        CHECK(world.first.uses == 1);
        world.pawn.SetState(state, false);
    }
}

TEST_CASE("A player's item intents respect input contexts and possession", "[sandbox][gameplay][inventory]") {
    InventoryWorld world;
    world.actions.Add("Count", Spot::InputActionType::Button).Bind(Spot::InputControl::Of(Spot::Key::F));
    auto& player = world.scene.CreateEntity("Player").AddComponent<Gameplay::PlayerController>();
    player.pawn = world.entity.GetId();
    world.Step();
    world.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::F, false});
    // The local controller needs game input; direct intents above do not need a cursor.
    const bool locked = Spot::Input::IsCursorLocked();
    Spot::Input::SetCursorLocked(true);
    world.Step();
    CHECK(world.first.uses == 1);
    world.input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::F});
    world.Step();
    Gameplay::InputContext modal{{.name = "Modal", .priority = 30, .actions = {"Count"}}};
    modal.Activate(&player);
    world.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::F, false});
    world.Step();
    CHECK(world.first.uses == 1);
    player.Unpossess();
    CHECK_FALSE(world.first.active);
    Spot::Input::SetCursorLocked(locked);
}

TEST_CASE("The item panel shows a new item's status without a type branch", "[sandbox][gameplay][inventory]") {
    InventoryWorld world;
    world.Use();
    auto& player = world.scene.CreateEntity("Player").AddComponent<Gameplay::PlayerController>();
    player.pawn = world.entity.GetId();
    auto& ui = world.scene.CreateEntity("HUD");
    ui.AddComponent<Gameplay::Hud>();
    auto& panel = ui.CreateChild("Item");
    auto& element = panel.AddComponent<Gameplay::ItemPanelElement>();
    auto& count = panel.CreateChild("Count").AddComponent<Spot::UiText>();
    element.count = count.GetEntity().GetId();
    world.Step();
    CHECK(count.text == "1");
    world.inventory.Select(world.second);
    world.Step();
    CHECK(count.text == "0");
}

TEST_CASE("Adding a held prefab preserves the selection and strips its world body", "[sandbox][gameplay][inventory]") {
    InventoryWorld world;
    Spot::RegisterComponent<CounterItem>("GameplayTestCounterItem");
    Spot::Scene source;
    auto& root = source.CreateEntity("Counter");
    root.AddComponent<CounterItem>();
    root.AddComponent<Spot::Rigidbody>();
    root.CreateChild("Collider").AddComponent<Spot::BoxCollider>();
    auto prefab = Spot::Prefab::FromString(root.SaveToString());
    REQUIRE(prefab);
    world.inventory.Select(world.second);
    auto* added = world.inventory.AddFromPrefab(std::make_shared<const Spot::Prefab>(std::move(*prefab)));
    REQUIRE(added);
    CHECK(world.inventory.GetItemCount() == 3);
    CHECK(world.inventory.GetEquipped() == &world.second);
    CHECK(added->GetEntity().GetParent() == &world.entity);
    CHECK_FALSE(added->GetEntity().HasComponent<Spot::Rigidbody>());
    CHECK_FALSE(added->GetEntity().GetComponentInChildren<Spot::Collider>());
}

TEST_CASE("Closing the inventory wheel restores time unless another system changed it",
          "[sandbox][gameplay][inventory]") {
    InventoryWorld world;
    const float previous = Spot::Time::GetTimeScale();
    Spot::Time::SetTimeScale(0.8f);
    world.inventory.OpenWheel();
    CHECK(Spot::Time::GetTimeScale() == Catch::Approx(0.8f * world.inventory.wheelTimeScale));
    world.inventory.CloseWheel(false);
    CHECK(Spot::Time::GetTimeScale() == Catch::Approx(0.8f));
    world.inventory.OpenWheel();
    Spot::Time::SetTimeScale(0.0f);
    world.inventory.CloseWheel(false);
    CHECK(Spot::Time::GetTimeScale() == 0.0f);
    Spot::Time::SetTimeScale(previous);
}

TEST_CASE("World items follow an arbitrary rig and mount, switch models and release their holding pose",
          "[sandbox][gameplay][inventory][world_model]") {
    InventoryWorld world;
    world.entity.SetLayer(3);
    auto& animator = world.entity.AddComponent<Spot::Animator>();
    animator.SetSkeleton(std::make_shared<const Spot::Skeleton>(Spot::Skeleton{
        "Custom rig", {{"Socket", -1, {{0, 0.5f, 0}}}}, {0}, {Spot::Matrix4::Translation({0, -0.5f, 0})}}));
    auto clip =
        std::make_shared<const Spot::AnimationClip>(Spot::AnimationClip{"Custom hold",
                                                                        1,
                                                                        {{0,
                                                                          Spot::AnimationPath::Translation,
                                                                          Spot::AnimationInterpolation::Linear,
                                                                          {0, 1},
                                                                          {{0, 0.5f, 0, 0}, {0, 0.5f, 0, 0}}}}});
    REQUIRE(animator.Play(clip));
    const Spot::AnimationLayer layer{.name = "Custom layer", .clip = clip, .weight = 0};
    REQUIRE(animator.SetLayers(std::span(&layer, 1)));
    auto& mount = world.entity.CreateChild("Custom socket");
    auto& attachment = mount.AddComponent<Spot::BoneAttachment>();
    attachment.SetAnimator(world.entity.GetId());
    attachment.SetBone("Socket");
    auto& visual = world.entity.AddComponent<Gameplay::WorldModel>();
    visual.mount = mount.GetId();
    visual.animator.Set(&animator);
    visual.poseLayer = "Custom layer";
    visual.muzzleNode = "Tip";
    world.first.model = std::make_shared<const Spot::Model>(std::vector<Spot::ModelNode>{
        {.name = "Tool"}, {.name = "Tip", .transform = Spot::Matrix4::Translation({0, 0, -0.4f}), .parent = 0}});
    world.second.model = std::make_shared<const Spot::Model>(std::vector<Spot::ModelNode>{{.name = "Other tool"}});
    world.Step();
    REQUIRE(visual.GetModelEntity());
    CHECK(visual.GetModelEntity()->GetParent() == &mount);
    CHECK(visual.GetModelEntity()->GetChildren()[0]->GetLayer() == 3);
    CHECK(visual.GetShownModel() == world.first.model);
    CHECK(animator.GetLayers()[0].weight == 1);
    CHECK(Spot::Distance(visual.GetMuzzlePosition(),
                         mount.GetTransform().GetWorldPosition() + Spot::Vector3{0, 0, -0.4f}) < 0.001f);
    const auto firstId = visual.GetModelEntity()->GetId();
    world.inventory.Select(world.second);
    world.Step();
    REQUIRE(visual.GetModelEntity());
    CHECK(visual.GetModelEntity()->GetId() != firstId);
    CHECK(visual.GetShownModel() == world.second.model);
    world.pawn.SetState("Dead", true);
    world.Step();
    CHECK_FALSE(visual.GetModelEntity());
    CHECK(animator.GetLayers()[0].weight == 0);
    world.pawn.SetState("Dead", false);
    world.Step();
    REQUIRE(visual.GetModelEntity());
    world.inventory.SetEnabled(false);
    world.Step();
    CHECK_FALSE(visual.GetModelEntity());
    world.inventory.SetEnabled(true);
    world.Step();
    REQUIRE(visual.GetModelEntity());
    mount.Destroy();
    world.Step();
    CHECK_FALSE(visual.GetModelEntity());
    CHECK(animator.GetLayers()[0].weight == 0);
}
