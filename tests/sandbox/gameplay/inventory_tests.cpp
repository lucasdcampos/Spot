#include "gameplay_world.h"

#include "gameplay/core/interactor.h"
#include "gameplay/core/pawn.h"
#include "gameplay/core/player_controller.h"
#include "gameplay/core/world_time.h"
#include "gameplay/inventory/held_item.h"
#include "gameplay/inventory/inventory.h"
#include "gameplay/inventory/inventory_elements.h"
#include "gameplay/inventory/view_model.h"
#include "gameplay/inventory/world_model.h"
#include "gameplay/weapons/firearm.h"

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

TEST_CASE("Holstering clears item use and preserves the inventory until selection", "[sandbox][gameplay][inventory]") {
    InventoryWorld world;
    world.Use();
    REQUIRE(world.first.active);
    world.inventory.OpenWheel();
    world.inventory.Holster();
    world.inventory.Holster();
    world.Step();
    CHECK_FALSE(world.inventory.IsWheelOpen());
    CHECK_FALSE(world.inventory.GetEquipped());
    CHECK_FALSE(world.first.IsEquipped());
    CHECK_FALSE(world.first.active);
    CHECK(world.first.unequipped == 1);
    CHECK(world.inventory.GetItemCount() == 2);
    CHECK_FALSE(world.inventory.Drop());
    world.Use();
    CHECK(world.first.uses == 1);
    world.inventory.Select(0);
    CHECK(world.inventory.GetEquipped() == &world.first);
    CHECK(world.first.equipped == 2);
    world.Use();
    CHECK(world.first.uses == 2);
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

TEST_CASE("The inventory wheel keeps normal speed when the rules forbid time scaling",
          "[sandbox][gameplay][inventory]") {
    InventoryWorld world;
    const float previous = Spot::Time::GetTimeScale();
    Spot::Time::SetTimeScale(1.0f);
    Gameplay::WorldTime::Get(world.scene).SetAllowed(false);
    world.inventory.OpenWheel();
    CHECK(world.inventory.IsWheelOpen());
    CHECK(Spot::Time::GetTimeScale() == 1.0f);
    world.inventory.CloseWheel(false);
    CHECK(Spot::Time::GetTimeScale() == 1.0f);
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
    visual.poseMount = true;
    visual.readyPosition = {0.12f, -0.1f, -0.35f};
    visual.aimingPosition = {0.02f, 0.14f, -0.44f};
    visual.Synchronize();
    CHECK(attachment.GetOffset().translation == visual.readyPosition);
    world.pawn.SetState("Aiming", true);
    world.Run(0.7f);
    CHECK(Spot::Distance(attachment.GetOffset().translation, visual.aimingPosition) < 0.002f);
    world.pawn.SetState("Aiming", false);
    world.Run(0.7f);
    CHECK(Spot::Distance(attachment.GetOffset().translation, visual.readyPosition) < 0.002f);
    world.pawn.SetState("Sprinting", true);
    world.Run(0.7f);
    CHECK(Spot::Distance(attachment.GetOffset().translation, visual.sprintPosition) < 0.002f);
    CHECK((attachment.GetOffset().rotation * Spot::Vector3{0, 0, -1}).y < -0.5f);
    world.pawn.SetState("Sprinting", false);
    world.pawn.SetState("Aiming", true);
    world.Run(0.7f);
    CHECK(Spot::Distance(attachment.GetOffset().translation, visual.aimingPosition) < 0.002f);
    world.pawn.SetState("Aiming", false);
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

TEST_CASE("Dropped items leave the inventory and can be collected by another pawn", "[sandbox][gameplay][inventory]") {
    InventoryWorld world;
    world.entity.GetTransform().SetPosition({0, 1, 0});
    world.first.model = std::make_shared<const Spot::Model>(std::vector<Spot::ModelNode>{{.name = "Tool"}});
    auto& other = world.scene.CreateEntity("Other pawn");
    other.AddComponent<Gameplay::Pawn>();
    auto& inventory = other.AddComponent<Gameplay::Inventory>();
    auto& interactor = other.AddComponent<Gameplay::Interactor>();
    world.Step();
    CHECK_FALSE(world.first.CanInteract(interactor));
    REQUIRE(world.inventory.Drop() == &world.first);
    CHECK(world.first.unequipped == 1);
    CHECK_FALSE(world.first.IsEquipped());
    CHECK_FALSE(world.first.GetInventory());
    CHECK(world.inventory.GetItemCount() == 1);
    CHECK(world.inventory.GetEquipped() == &world.second);
    CHECK(world.first.GetEntity().HasComponent<Spot::Rigidbody>());
    CHECK(world.first.GetEntity().HasComponent<Spot::BoxCollider>());
    CHECK(world.first.GetEntity().GetChildren().size() == 1);
    CHECK(world.first.CanInteract(interactor));
    CHECK(world.first.GetPrompt(interactor) == "Pick up Counter");
    world.first.Interact(interactor);
    CHECK(inventory.GetEquipped() == &world.first);
    CHECK(world.first.GetInventory() == &inventory);
    CHECK_FALSE(world.first.GetEntity().HasComponent<Spot::Rigidbody>());
    CHECK_FALSE(world.first.GetEntity().HasComponent<Spot::Collider>());
    CHECK_FALSE(inventory.PickUp(world.first));
    world.Step();
    CHECK(world.first.GetEntity().GetChildren().empty());
    REQUIRE(inventory.Drop() == &world.first);
    CHECK(inventory.GetItemCount() == 0);
    CHECK_FALSE(inventory.GetEquipped());
    inventory.OpenWheel();
    CHECK_FALSE(inventory.IsWheelOpen());
    REQUIRE(inventory.PickUp(world.first));
    CHECK(inventory.GetEquipped() == &world.first);
}

TEST_CASE("Drop and pickup respect the inventory's blocked states", "[sandbox][gameplay][inventory]") {
    InventoryWorld world;
    auto& item = world.scene.CreateEntity("World item").AddComponent<CounterItem>();
    world.Step();
    for (const auto* state : {"Dead", "Seated", "Sprinting", "Placing"}) {
        world.pawn.SetState(state, true);
        CHECK_FALSE(world.inventory.Drop());
        CHECK_FALSE(world.inventory.PickUp(item));
        world.pawn.SetState(state, false);
    }
    world.inventory.OpenWheel();
    CHECK_FALSE(world.inventory.Drop());
    CHECK_FALSE(world.inventory.PickUp(item));
    world.inventory.CloseWheel(false);
    world.first.droppable = false;
    CHECK_FALSE(world.inventory.Drop());
    world.inventory.SetEnabled(false);
    CHECK_FALSE(world.inventory.PickUp(item));
    CHECK_FALSE(world.inventory.Drop());
}

TEST_CASE("Dropping a firearm preserves ammunition and releases aim", "[sandbox][gameplay][inventory]") {
    InventoryWorld world;
    auto& firearm = world.entity.CreateChild("Firearm").AddComponent<Gameplay::Firearm>();
    firearm.magazineSize = 7;
    world.Step();
    world.inventory.Select(firearm);
    Gameplay::ItemInput intent;
    intent.active = intent.canUse = true;
    intent.Set("Fire", true, true);
    intent.Set("Aim", true);
    world.inventory.Use(intent);
    const int ammo = firearm.GetAmmo();
    const int reserve = firearm.GetReserve();
    CHECK(ammo == 6);
    REQUIRE(world.inventory.Drop() == &firearm);
    CHECK_FALSE(world.pawn.HasState("Aiming"));
    REQUIRE(world.inventory.PickUp(firearm));
    CHECK(firearm.GetAmmo() == ammo);
    CHECK(firearm.GetReserve() == reserve);
}

TEST_CASE("The weapon wheel reaches every item beyond eight slots", "[sandbox][gameplay][inventory]") {
    InventoryWorld world;
    for (int i = 0; i < 10; ++i) {
        world.entity.CreateChild("Extra").AddComponent<CounterItem>();
    }
    auto& player = world.scene.CreateEntity("Player").AddComponent<Gameplay::PlayerController>();
    player.pawn = world.entity.GetId();
    world.actions.Add("Weapon Wheel", Spot::InputActionType::Button).Bind(Spot::InputControl::Of(Spot::Key::Tab));
    world.actions.Add("Look", Spot::InputActionType::Vector2).Bind(Spot::InputControl::MouseMotion());
    world.Step();
    const bool locked = Spot::Input::IsCursorLocked();
    Spot::Input::SetCursorLocked(true);
    world.input.OnEvent(Spot::KeyPressedEvent{Spot::Key::Tab, false});
    world.Step();
    world.input.OnEvent(Spot::MouseMovedEvent{{0, 0}, {-45, -78}});
    world.Step();
    CHECK(world.inventory.GetItemCount() == 12);
    CHECK(world.inventory.GetWheelSlot() == 11);
    world.input.OnEvent(Spot::KeyReleasedEvent{Spot::Key::Tab});
    world.Step();
    CHECK(world.inventory.GetSelected() == 11);
    Spot::Input::SetCursorLocked(locked);
}

TEST_CASE("Authored weapon stances switch independently of model identity and keep wrist offsets",
          "[sandbox][gameplay][inventory][world_model]") {
    InventoryWorld world;
    auto& mount = world.entity.CreateChild("Mount");
    auto& attachment = mount.AddComponent<Spot::BoneAttachment>();
    attachment.SetEnabled(false);
    auto& hand = world.entity.CreateChild("Support hand").AddComponent<Spot::TwoBoneIK>();
    hand.SetEnabled(false);
    const Spot::TransformComponents wrist{{0.01f, 0.02f, -0.03f}, Spot::Quaternion::FromEuler({0, 0.4f, 0})};
    hand.SetOffset(wrist);
    auto& visual = world.entity.AddComponent<Gameplay::WorldModel>();
    visual.mount = mount.GetId();
    visual.poseMount = true;
    visual.leftHand.Set(&hand);
    world.first.model = std::make_shared<const Spot::Model>(std::vector<Spot::ModelNode>{
        {.name = "Weapon"},
        {.name = "RightHand", .parent = 0},
        {.name = "LeftHand", .transform = Spot::Matrix4::Translation({-0.07f, 0, -0.3f}), .parent = 0}});
    world.second.model = world.first.model;
    world.first.worldPose = {.enabled = true,
                             .ready = {{0.08f, -0.06f, -0.3f}, {-0.3f, 0.5f, 0}},
                             .aiming = {{0.02f, 0.12f, -0.35f}, {0, 0.7f, 0}},
                             .sprint = {{0.12f, -0.2f, -0.25f}, {-0.6f, -0.2f, 0}},
                             .supportOffset = {0, 0, 0.08f}};
    world.Run(0.2f);
    const auto instance = visual.GetModelEntity()->GetId();
    CHECK(attachment.GetOffset().translation == world.first.worldPose.ready.position);
    const auto* contact = world.scene.GetEntity(hand.GetTarget());
    REQUIRE(contact);
    CHECK(contact->GetTransform().GetPosition() == world.first.worldPose.supportOffset);
    CHECK(hand.GetOffset().translation == wrist.translation);
    CHECK(hand.GetOffset().rotation == wrist.rotation);
    world.pawn.SetState("Aiming", true);
    world.Run(0.7f);
    CHECK(Spot::Distance(attachment.GetOffset().translation, world.first.worldPose.aiming.position) < 0.002f);
    world.inventory.Select(world.second);
    world.Run(0.2f);
    CHECK(visual.GetModelEntity()->GetId() == instance);
    CHECK(Spot::Distance(attachment.GetOffset().translation, visual.aimingPosition) < 0.002f);
    REQUIRE(world.scene.GetEntity(hand.GetTarget()));
    CHECK(world.scene.GetEntity(hand.GetTarget())->GetName() == "LeftHand");
    world.inventory.Select(world.first);
    world.Run(0.2f);
    const auto* restoredContact = world.scene.GetEntity(hand.GetTarget());
    REQUIRE(restoredContact);
    CHECK(restoredContact->GetTransform().GetPosition() == world.first.worldPose.supportOffset);
    auto& firearm = world.scene.CreateEntity("Authored firearm").AddComponent<Gameplay::Firearm>();
    firearm.worldPose = world.first.worldPose;
    const auto prefab = Spot::Prefab::FromString(firearm.GetEntity().SaveToString());
    REQUIRE(prefab);
    auto* restored = world.scene.Instantiate(std::make_shared<const Spot::Prefab>(*prefab));
    REQUIRE(restored);
    const auto& pose = restored->GetComponent<Gameplay::Firearm>()->worldPose;
    CHECK(pose.enabled);
    CHECK(pose.aiming.position == firearm.worldPose.aiming.position);
    CHECK(pose.aiming.angles == firearm.worldPose.aiming.angles);
    CHECK(pose.supportOffset == firearm.worldPose.supportOffset);
}
