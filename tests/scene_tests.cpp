#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using Catch::Approx;
using Spot::Entity;
using Spot::Quaternion;
using Spot::Scene;
using Spot::Vector3;

namespace {

using Calls = std::vector<std::string>;
using Action = std::function<void(class Recorder&)>;

// Records its hooks as "<label>.<hook>" and runs optional actions from them.
class Recorder : public Spot::Component {
public:
    Recorder(Calls& calls, std::string label) : m_calls(calls), m_label(std::move(label)) {}

    Action onStart;
    Action onUpdate;
    Action onDestroy;

protected:
    void OnStart() override { Record("start", onStart); }
    void OnEnable() override { Record("enable", {}); }
    void OnDisable() override { Record("disable", {}); }
    void OnUpdate(float /*deltaTime*/) override { Record("update", onUpdate); }
    void OnLateUpdate(float /*deltaTime*/) override { Record("late", {}); }
    void OnDestroy() override { Record("destroy", onDestroy); }

private:
    void Record(const char* hook, const Action& action) {
        m_calls.push_back(m_label + "." + hook);
        // A copy, because actions may replace themselves while they run.
        if (const auto run = action) {
            run(*this);
        }
    }

    Calls& m_calls;
    std::string m_label;
};

class Health final : public Spot::Component {
public:
    explicit Health(int initialPoints) : points(initialPoints) {}

    int points;
};

class Mover : public Spot::Component {
public:
    float distance = 0.0f;

protected:
    void OnUpdate(float deltaTime) override { distance += 10.0f * deltaTime; }
};

class FastMover final : public Mover {};

bool IsNear(Vector3 a, Vector3 b) {
    constexpr float Tolerance = 1e-4f;
    return std::abs(a.x - b.x) < Tolerance && std::abs(a.y - b.y) < Tolerance && std::abs(a.z - b.z) < Tolerance;
}

} // namespace

TEST_CASE("Scene creates, finds and destroys entities", "[scene]") {
    Scene scene;
    Entity& player = scene.CreateEntity("Player");
    Entity& enemy = scene.CreateEntity("Enemy");

    CHECK(scene.GetEntityCount() == 2);
    CHECK(&player.GetScene() == &scene);
    CHECK(scene.FindEntity("Enemy") == &enemy);
    CHECK(scene.FindEntity("Missing") == nullptr);
    CHECK_FALSE(scene.IsLoaded());

    enemy.SetName("Boss");
    CHECK(scene.FindEntity("Boss") == &enemy);

    // Outside of Update(), the entity is freed at once.
    enemy.Destroy();
    CHECK(scene.GetEntityCount() == 1);
    CHECK(scene.FindEntity("Boss") == nullptr);

    std::vector<std::string> names;
    scene.ForEachEntity([&names](Entity& entity) { names.push_back(entity.GetName()); });
    CHECK(names == std::vector<std::string>{"Player"});
}

TEST_CASE("Entity ids stay safe after the entity is destroyed", "[scene]") {
    Scene scene;
    Entity& first = scene.CreateEntity();
    Entity& second = scene.CreateEntity();
    const Spot::EntityId id = first.GetId();

    CHECK(id != Spot::EntityId::Invalid);
    CHECK(second.GetId() != id);
    CHECK(scene.GetEntity(id) == &first);

    first.Destroy();
    CHECK(scene.GetEntity(id) == nullptr);
    CHECK(scene.GetEntity(Spot::EntityId::Invalid) == nullptr);
    CHECK(scene.CreateEntity().GetId() != id);
}

TEST_CASE("Entities are found by tag", "[scene]") {
    Scene scene;
    Entity& player = scene.CreateEntity("Player");
    Entity& goblin = scene.CreateEntity("Goblin");
    Entity& orc = scene.CreateEntity("Orc");
    player.AddTag("Player");
    goblin.AddTag("Enemy");
    orc.AddTag("Enemy");
    orc.AddTag("Boss");
    orc.AddTag("Boss");

    CHECK(orc.GetTags().size() == 2);
    CHECK(orc.HasTag("Boss"));
    CHECK_FALSE(player.HasTag("Enemy"));
    CHECK(scene.FindEntityWithTag("Enemy") == &goblin);
    CHECK(scene.FindEntitiesWithTag("Enemy") == std::vector<Entity*>{&goblin, &orc});
    CHECK(scene.FindEntityWithTag("Missing") == nullptr);

    CHECK(goblin.RemoveTag("Enemy"));
    CHECK_FALSE(goblin.RemoveTag("Enemy"));
    CHECK(scene.FindEntitiesWithTag("Enemy") == std::vector<Entity*>{&orc});

    // Inactive entities are found too; destroyed ones are not.
    orc.SetActive(false);
    CHECK(scene.FindEntityWithTag("Boss") == &orc);
    orc.Destroy();
    CHECK(scene.FindEntitiesWithTag("Enemy").empty());
}

TEST_CASE("Entities hold custom components found by type", "[scene]") {
    Scene scene;
    Entity& entity = scene.CreateEntity();

    Health& health = entity.AddComponent<Health>(3);
    FastMover& mover = entity.AddComponent<FastMover>();
    Mover& otherMover = entity.AddComponent<Mover>();

    CHECK(health.points == 3);
    CHECK(&health.GetEntity() == &entity);
    CHECK(&health.GetScene() == &scene);
    CHECK(entity.GetComponent<Health>() == &health);
    // Base types find derived components.
    CHECK(entity.GetComponent<Mover>() == &mover);
    CHECK(entity.GetComponents<Mover>() == std::vector<Mover*>{&mover, &otherMover});
    CHECK(entity.GetComponent<Spot::Component>() == &health);
    CHECK(entity.HasComponent<FastMover>());
    CHECK_FALSE(entity.HasComponent<Recorder>());
    CHECK(scene.FindComponent<FastMover>() == &mover);

    CHECK(entity.RemoveComponent<Health>());
    CHECK_FALSE(entity.HasComponent<Health>());
    CHECK_FALSE(entity.RemoveComponent<Health>());
    CHECK(entity.HasComponent<Mover>());
}

TEST_CASE("Components start, enable, update and late update in order", "[scene]") {
    Calls calls;
    {
        Scene scene;
        Entity& first = scene.CreateEntity();
        first.AddComponent<Recorder>(calls, "a");
        first.AddComponent<Recorder>(calls, "b");
        scene.CreateEntity().AddComponent<Recorder>(calls, "c");

        CHECK(calls.empty());
        scene.Update(0.016f);
        CHECK(calls == Calls{"a.start", "a.enable", "b.start", "b.enable", "c.start", "c.enable", "a.update",
                             "b.update", "c.update", "a.late", "b.late", "c.late"});

        calls.clear();
        scene.Update(0.016f);
        CHECK(calls == Calls{"a.update", "b.update", "c.update", "a.late", "b.late", "c.late"});
        calls.clear();
    }
    // Within an entity, in reverse order of addition.
    CHECK(calls == Calls{"a.disable", "b.disable", "c.disable", "b.destroy", "a.destroy", "c.destroy"});
}

TEST_CASE("Components that never started are not destroyed", "[scene]") {
    Calls calls;
    {
        Scene scene;
        Entity& entity = scene.CreateEntity();
        entity.AddComponent<Recorder>(calls, "removed");
        entity.AddComponent<Recorder>(calls, "kept");
        entity.RemoveComponent<Recorder>();
    }
    CHECK(calls.empty());
}

TEST_CASE("Disabled components are not updated and start once enabled", "[scene]") {
    Calls calls;
    Scene scene;
    auto& recorder = scene.CreateEntity().AddComponent<Recorder>(calls, "a");
    recorder.SetEnabled(false);
    CHECK_FALSE(recorder.IsActiveAndEnabled());

    scene.Update(0.016f);
    CHECK(calls.empty());

    recorder.SetEnabled(true);
    CHECK(recorder.IsActiveAndEnabled());
    // Not started yet, so no OnEnable() before the next update.
    CHECK(calls.empty());
    scene.Update(0.016f);
    CHECK(calls == Calls{"a.start", "a.enable", "a.update", "a.late"});

    calls.clear();
    recorder.SetEnabled(false);
    scene.Update(0.016f);
    recorder.SetEnabled(true);
    CHECK(calls == Calls{"a.disable", "a.enable"});
}

TEST_CASE("Inactive entities and their descendants do not run", "[scene]") {
    Calls calls;
    Scene scene;
    Entity& parent = scene.CreateEntity("Parent");
    Entity& child = parent.CreateChild("Child");
    parent.AddComponent<Recorder>(calls, "parent");
    child.AddComponent<Recorder>(calls, "child");

    parent.SetActive(false);
    CHECK_FALSE(parent.IsActiveSelf());
    CHECK(child.IsActiveSelf());
    CHECK_FALSE(child.IsActiveInHierarchy());

    scene.Update(0.016f);
    CHECK(calls.empty());

    parent.SetActive(true);
    scene.Update(0.016f);
    CHECK(calls == Calls{"parent.start", "parent.enable", "child.start", "child.enable", "parent.update",
                         "child.update", "parent.late", "child.late"});

    // Started components hear about it at once.
    calls.clear();
    parent.SetActive(false);
    CHECK(calls == Calls{"parent.disable", "child.disable"});
    scene.Update(0.016f);
    CHECK(calls == Calls{"parent.disable", "child.disable"});

    calls.clear();
    child.SetActive(false);
    parent.SetActive(true);
    CHECK(calls == Calls{"parent.enable"});
}

TEST_CASE("Scene updates move components by the frame time", "[scene]") {
    Scene scene;
    auto& mover = scene.CreateEntity().AddComponent<Mover>();

    scene.Update(0.5f);
    scene.Update(0.25f);

    CHECK(mover.distance == 7.5f);
}

TEST_CASE("Entities form a hierarchy", "[scene]") {
    Scene scene;
    Entity& root = scene.CreateEntity("Root");
    Entity& arm = root.CreateChild("Arm");
    Entity& hand = arm.CreateChild("Hand");
    Entity& other = scene.CreateEntity("Other");

    CHECK(arm.GetParent() == &root);
    CHECK(root.GetChildren() == std::vector<Entity*>{&arm});
    CHECK(root.FindChild("Arm") == &arm);
    CHECK(root.FindChild("Hand") == nullptr);
    CHECK(hand.IsDescendantOf(root));
    CHECK_FALSE(root.IsDescendantOf(hand));
    CHECK(scene.GetRootEntities() == std::vector<Entity*>{&root, &other});

    hand.SetParent(&other);
    CHECK(arm.GetChildren().empty());
    CHECK(other.GetChildren() == std::vector<Entity*>{&hand});

    hand.SetParent(nullptr);
    CHECK(hand.GetParent() == nullptr);
    // In creation order.
    CHECK(scene.GetRootEntities() == std::vector<Entity*>{&root, &hand, &other});

    // Children found in the hierarchy.
    auto& health = hand.AddComponent<Health>(1);
    hand.SetParent(&arm);
    CHECK(root.GetComponentInChildren<Health>() == &health);
    CHECK(hand.GetComponentInParent<Health>() == &health);
    CHECK(other.GetComponentInChildren<Health>() == nullptr);
    auto& rootHealth = root.AddComponent<Health>(2);
    CHECK(arm.GetComponentInParent<Health>() == &rootHealth);
}

TEST_CASE("Destroying an entity destroys its descendants", "[scene]") {
    Calls calls;
    Scene scene;
    Entity& root = scene.CreateEntity("Root");
    Entity& child = root.CreateChild("Child");
    child.CreateChild("Grandchild").AddComponent<Recorder>(calls, "grandchild");
    Entity& sibling = root.CreateChild("Sibling");
    scene.Update(0.016f);
    calls.clear();

    // A destroyed child leaves its parent's children.
    sibling.Destroy();
    CHECK(root.GetChildren() == std::vector<Entity*>{&child});

    root.Destroy();
    CHECK(calls == Calls{"grandchild.disable", "grandchild.destroy"});
    CHECK(scene.GetEntityCount() == 0);
}

TEST_CASE("World transforms follow the parents", "[scene]") {
    Scene scene;
    Entity& parent = scene.CreateEntity();
    Entity& child = parent.CreateChild();
    Spot::Transform& parentTransform = parent.GetTransform();
    Spot::Transform& transform = child.GetTransform();

    parentTransform.SetPosition({10.0f, 0.0f, 0.0f});
    parentTransform.SetRotation2D(Spot::Pi / 2.0f);
    parentTransform.SetScale({2.0f, 2.0f, 2.0f});
    transform.SetPosition({5.0f, 0.0f, 0.0f});

    // Scaled, then turned a quarter counterclockwise, then moved.
    CHECK(IsNear(transform.GetWorldPosition(), {10.0f, 10.0f, 0.0f}));
    CHECK(transform.GetWorldRotation() == parentTransform.GetRotation());
    CHECK(transform.GetWorldRotation2D() == Approx(Spot::Pi / 2.0f));

    transform.SetWorldPosition({0.0f, 0.0f, 0.0f});
    CHECK(IsNear(transform.GetWorldPosition(), {0.0f, 0.0f, 0.0f}));
    CHECK(IsNear(transform.GetPosition(), {0.0f, 5.0f, 0.0f}));

    transform.SetWorldRotation(Quaternion::Identity());
    CHECK(transform.GetWorldRotation2D() == Approx(0.0f).margin(1e-5f));
    CHECK(transform.GetRotation2D() == Approx(-Spot::Pi / 2.0f));
}

TEST_CASE("Cached world transforms update when an ancestor changes", "[scene]") {
    Scene scene;
    Entity& root = scene.CreateEntity();
    Entity& middle = root.CreateChild();
    Entity& leaf = middle.CreateChild();
    leaf.GetTransform().SetPosition({1.0f, 0.0f, 0.0f});
    CHECK(IsNear(leaf.GetTransform().GetWorldPosition(), {1.0f, 0.0f, 0.0f}));

    root.GetTransform().Translate({10.0f, 0.0f, 0.0f});
    CHECK(IsNear(leaf.GetTransform().GetWorldPosition(), {11.0f, 0.0f, 0.0f}));

    middle.GetTransform().SetScale({3.0f, 3.0f, 3.0f});
    CHECK(IsNear(leaf.GetTransform().GetWorldPosition(), {13.0f, 0.0f, 0.0f}));

    root.GetTransform().Rotate(Quaternion::FromAxisAngle({0.0f, 0.0f, 1.0f}, Spot::Pi));
    CHECK(IsNear(leaf.GetTransform().GetWorldPosition(), {7.0f, 0.0f, 0.0f}));

    // Moving the leaf under another parent changes its world transform as well.
    Entity& other = scene.CreateEntity();
    other.GetTransform().SetPosition({0.0f, 50.0f, 0.0f});
    leaf.SetParent(&other);
    CHECK(IsNear(leaf.GetTransform().GetWorldPosition(), {1.0f, 50.0f, 0.0f}));
    // A new child starts from the cached world transform of its parent.
    Entity& sibling = middle.CreateChild();
    sibling.GetTransform().SetPosition({1.0f, 0.0f, 0.0f});
    CHECK(IsNear(sibling.GetTransform().GetWorldPosition(), {7.0f, 0.0f, 0.0f}));
}

TEST_CASE("Reparenting keeps the local or the world pose", "[scene]") {
    Scene scene;
    Entity& parent = scene.CreateEntity();
    parent.GetTransform().SetPosition({100.0f, 0.0f, 0.0f});
    parent.GetTransform().SetRotation2D(1.0f);

    Entity& local = scene.CreateEntity();
    local.GetTransform().SetPosition({1.0f, 2.0f, 0.0f});
    local.SetParent(&parent);
    CHECK(local.GetTransform().GetPosition() == Vector3{1.0f, 2.0f, 0.0f});

    Entity& world = scene.CreateEntity();
    world.GetTransform().SetPosition({1.0f, 2.0f, 0.0f});
    world.SetParent(&parent, true);
    CHECK(IsNear(world.GetTransform().GetWorldPosition(), {1.0f, 2.0f, 0.0f}));
    CHECK(world.GetTransform().GetWorldRotation2D() == Approx(0.0f).margin(1e-5f));

    world.SetParent(nullptr, true);
    CHECK(IsNear(world.GetTransform().GetPosition(), {1.0f, 2.0f, 0.0f}));
}

TEST_CASE("Entities destroyed during an update are freed when it ends", "[scene]") {
    Calls calls;
    Scene scene;
    Entity& target = scene.CreateEntity("Target");
    target.AddComponent<Recorder>(calls, "target");
    auto& killer = scene.CreateEntity("Killer").AddComponent<Recorder>(calls, "killer");
    scene.Update(0.016f);
    calls.clear();

    killer.onUpdate = [](Recorder& self) {
        Entity* found = self.GetScene().FindEntity("Target");
        REQUIRE(found != nullptr);
        found->Destroy();
        // Gone from queries, but still alive until the update ends.
        CHECK(self.GetScene().FindEntity("Target") == nullptr);
        CHECK(found->IsDestroyed());
        CHECK(found->HasComponent<Recorder>());
    };
    scene.Update(0.016f);

    CHECK(calls == Calls{"target.update", "killer.update", "target.disable", "killer.late", "target.destroy"});
    CHECK(scene.GetEntityCount() == 1);
}

TEST_CASE("An entity that destroys itself stops updating its other components", "[scene]") {
    Calls calls;
    Scene scene;
    Entity& entity = scene.CreateEntity();
    auto& first = entity.AddComponent<Recorder>(calls, "a");
    entity.AddComponent<Recorder>(calls, "b");
    scene.Update(0.016f);
    calls.clear();

    first.onUpdate = [](Recorder& self) { self.GetEntity().Destroy(); };
    scene.Update(0.016f);

    CHECK(calls == Calls{"a.update", "a.disable", "b.disable", "b.destroy", "a.destroy"});
    CHECK(scene.GetEntityCount() == 0);
}

TEST_CASE("Entities created during an update start on the next one", "[scene]") {
    Calls calls;
    Scene scene;
    auto& spawner = scene.CreateEntity().AddComponent<Recorder>(calls, "spawner");
    spawner.onUpdate = [&calls](Recorder& self) {
        self.GetScene().CreateEntity().AddComponent<Recorder>(calls, "spawned");
        self.onUpdate = nullptr;
    };

    scene.Update(0.016f);
    CHECK(calls == Calls{"spawner.start", "spawner.enable", "spawner.update", "spawner.late"});
    CHECK(scene.GetEntityCount() == 2);

    calls.clear();
    scene.Update(0.016f);
    CHECK(calls ==
          Calls{"spawned.start", "spawned.enable", "spawner.update", "spawned.update", "spawner.late", "spawned.late"});
}

TEST_CASE("Components added in OnStart start in the same update", "[scene]") {
    Calls calls;
    Scene scene;
    auto& first = scene.CreateEntity().AddComponent<Recorder>(calls, "a");
    first.onStart = [&calls](Recorder& self) { self.GetEntity().AddComponent<Recorder>(calls, "b"); };

    scene.Update(0.016f);

    CHECK(calls == Calls{"a.start", "a.enable", "b.start", "b.enable", "a.update", "b.update", "a.late", "b.late"});
}

TEST_CASE("Components removed during an update are destroyed when it ends", "[scene]") {
    Calls calls;
    Scene scene;
    Entity& entity = scene.CreateEntity();
    auto& first = entity.AddComponent<Recorder>(calls, "a");
    auto& second = entity.AddComponent<Recorder>(calls, "b");
    scene.Update(0.016f);
    calls.clear();

    first.onUpdate = [&second](Recorder& self) {
        self.GetEntity().RemoveComponent(second);
        self.onUpdate = nullptr;
    };
    scene.Update(0.016f);
    CHECK(calls == Calls{"a.update", "b.disable", "a.late", "b.destroy"});

    calls.clear();
    scene.Update(0.016f);
    CHECK(calls == Calls{"a.update", "a.late"});
    CHECK(entity.GetComponent<Recorder>() == &first);
}

TEST_CASE("Removals asked for in OnDestroy are handled too", "[scene]") {
    Calls calls;
    Scene scene;
    scene.CreateEntity("First").AddComponent<Recorder>(calls, "first");
    auto& second = scene.CreateEntity("Second").AddComponent<Recorder>(calls, "second");
    scene.Update(0.016f);
    calls.clear();

    // Destroying the second entity destroys the first one, created earlier, in turn.
    second.onDestroy = [](Recorder& self) { self.GetScene().FindEntity("First")->Destroy(); };
    second.GetEntity().Destroy();

    CHECK(calls == Calls{"second.disable", "second.destroy", "first.disable", "first.destroy"});
    CHECK(scene.GetEntityCount() == 0);
}

TEST_CASE("Clear destroys every entity", "[scene]") {
    Calls calls;
    Scene scene;
    scene.CreateEntity().CreateChild().AddComponent<Recorder>(calls, "child");
    scene.CreateEntity().AddComponent<Recorder>(calls, "root");
    scene.Update(0.016f);
    calls.clear();

    scene.Clear();

    CHECK(calls == Calls{"child.disable", "root.disable", "child.destroy", "root.destroy"});
    CHECK(scene.GetEntityCount() == 0);
    scene.CreateEntity();
    CHECK(scene.GetEntityCount() == 1);
}

TEST_CASE("ForEachComponent visits enabled components of active entities", "[scene]") {
    Scene scene;
    auto& slow = scene.CreateEntity().AddComponent<Mover>();
    scene.CreateEntity().AddComponent<Health>(1);
    auto& fast = scene.CreateEntity().AddComponent<FastMover>();
    scene.CreateEntity().AddComponent<Mover>().SetEnabled(false);
    Entity& inactive = scene.CreateEntity();
    inactive.CreateChild().AddComponent<Mover>();
    inactive.SetActive(false);

    std::vector<Mover*> visited;
    scene.ForEachComponent<Mover>([&visited](Mover& mover) { visited.push_back(&mover); });
    CHECK(visited == std::vector<Mover*>{&slow, &fast});

    // Destroying entities while iterating is safe; they are freed once the loop ends.
    scene.ForEachComponent<Mover>([](Mover& mover) {
        mover.GetEntity().Destroy();
        CHECK(mover.GetEntity().IsDestroyed());
    });
    CHECK(scene.GetEntityCount() == 4);
}
