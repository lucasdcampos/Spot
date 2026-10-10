#include <spot.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

namespace {

bool IsNear(const Spot::Matrix4& a, const Spot::Matrix4& b) {
    for (int i = 0; i < 16; ++i) {
        if (std::abs(a.GetData()[i] - b.GetData()[i]) > 1e-4f) {
            return false;
        }
    }
    return true;
}

Spot::Model MakeModel() {
    const Spot::AssetRef<Spot::Mesh> mesh(Spot::AssetId::Generate(), nullptr);
    const Spot::AssetRef<Spot::Material> red(
        Spot::AssetId::Generate(),
        std::make_shared<const Spot::Material>(Spot::Material{.baseColor = Spot::Color::Red}));
    const Spot::AssetRef<Spot::Material> green(
        Spot::AssetId::Generate(),
        std::make_shared<const Spot::Material>(Spot::Material{.baseColor = Spot::Color::Green}));
    const Spot::Matrix4 pivot =
        Spot::Matrix4::Translation({2.0f, 0.0f, 0.0f}) * Spot::Matrix4::Scale({2.0f, 1.0f, 1.0f});
    const Spot::Matrix4 child =
        Spot::Matrix4::Translation({0.0f, 1.0f, 0.0f}) *
        Spot::Matrix4::Rotation(Spot::Quaternion::FromAxisAngle({0.0f, 0.0f, 1.0f}, Spot::ToRadians(45.0f))) *
        Spot::Matrix4::Scale({-1.0f, 2.0f, 1.0f});
    return Spot::Model({{.name = "Pivot", .transform = pivot},
                        {.name = "Mesh", .transform = child, .parent = 0, .mesh = mesh, .materials = {red, green}},
                        {.name = "Mesh", .transform = {}, .parent = 0, .mesh = mesh, .materials = {red, green}},
                        {.name = "Empty", .transform = Spot::Matrix4::Translation({0.0f, 0.0f, 3.0f})}});
}

} // namespace

TEST_CASE("Model instances preserve nodes, local transforms and shared submesh assets", "[scene][model]") {
    const Spot::Model model = MakeModel();
    Spot::Scene scene;
    Spot::Entity& root = scene.Instantiate(model, "Instance");
    root.GetTransform().SetPosition({10.0f, 0.0f, 0.0f});
    CHECK(root.GetName() == "Instance");
    CHECK(scene.GetEntityCount() == model.GetNodes().size() + 1);
    REQUIRE(root.GetChildren().size() == 2);
    Spot::Entity* pivot = root.GetChildren()[0];
    CHECK(pivot->GetName() == "Pivot");
    CHECK_FALSE(pivot->HasComponent<Spot::MeshRenderer>());
    REQUIRE(pivot->GetChildren().size() == 2);
    Spot::Entity* first = pivot->GetChildren()[0];
    Spot::Entity* second = pivot->GetChildren()[1];
    CHECK(first->GetName() == second->GetName());
    CHECK(first->GetId() != second->GetId());
    CHECK(IsNear(first->GetTransform().GetLocalMatrix(), model.GetNodes()[1].transform));
    CHECK(IsNear(first->GetTransform().GetWorldMatrix(),
                 root.GetTransform().GetWorldMatrix() * model.GetNodeTransform(1)));
    CHECK(root.GetChildren()[1]->GetName() == "Empty");
    CHECK_FALSE(root.GetChildren()[1]->HasComponent<Spot::MeshRenderer>());

    const auto renderers = first->GetComponents<Spot::MeshRenderer>();
    const auto copies = second->GetComponents<Spot::MeshRenderer>();
    REQUIRE(renderers.size() == 2);
    REQUIRE(copies.size() == 2);
    for (std::size_t i = 0; i < renderers.size(); ++i) {
        CHECK(renderers[i]->GetSubmesh() == i);
        CHECK(renderers[i]->GetMesh().GetId() == model.GetNodes()[1].mesh.GetId());
        CHECK(renderers[i]->GetMesh().GetId() == copies[i]->GetMesh().GetId());
        CHECK(renderers[i]->GetMaterial().Get() == copies[i]->GetMaterial().Get());
    }
    CHECK(renderers[0]->GetMaterial()->baseColor == Spot::Color::Red);
    CHECK(renderers[1]->GetMaterial()->baseColor == Spot::Color::Green);
    first->SetActive(false);
    CHECK(second->IsActiveInHierarchy());
}

TEST_CASE("Model instances keep material changes and own independent entity trees", "[scene][model]") {
    Spot::Model model = MakeModel();
    const Spot::AssetRef<Spot::Material> blue =
        std::make_shared<const Spot::Material>(Spot::Material{.baseColor = Spot::Color::Blue});
    model.GetParts()[1].material = blue;
    Spot::Scene scene;
    Spot::Entity& first = scene.Instantiate(model, "First");
    Spot::Entity& second = Spot::InstantiateModel(scene, model, "Second");
    const auto renderers = first.FindChild("Pivot")->FindChild("Mesh")->GetComponents<Spot::MeshRenderer>();
    REQUIRE(renderers.size() == 2);
    CHECK(renderers[1]->GetMaterial().Get() == blue.Get());
    const Spot::EntityId secondId = second.GetId();
    first.Destroy();
    REQUIRE(scene.GetEntity(secondId));
    CHECK(scene.GetEntityCount() == model.GetNodes().size() + 1);
}

TEST_CASE("Model instances save hierarchy, asset ids and submesh selections without a GPU", "[scene][model]") {
    const Spot::Model model = MakeModel();
    Spot::Scene scene;
    scene.Instantiate(model, "Instance");
    const std::string saved = scene.SaveToString();
    Spot::Scene loaded;
    REQUIRE(loaded.LoadFromString(saved));
    REQUIRE(loaded.FindEntity("Instance"));
    const auto* pivot = loaded.FindEntity("Instance")->FindChild("Pivot");
    REQUIRE(pivot);
    REQUIRE(pivot->GetChildren().size() == 2);
    const auto* child = pivot->GetChildren()[0];
    CHECK(IsNear(child->GetTransform().GetWorldMatrix(), model.GetNodeTransform(1)));
    const auto renderers = child->GetComponents<Spot::MeshRenderer>();
    REQUIRE(renderers.size() == 2);
    for (std::size_t i = 0; i < renderers.size(); ++i) {
        CHECK(renderers[i]->GetSubmesh() == i);
        CHECK(renderers[i]->GetMaterial().GetId() == model.GetParts()[i].material.GetId());
        CHECK(renderers[i]->GetMesh().GetId() == model.GetParts()[i].mesh.GetId());
    }
}

TEST_CASE("An empty model instance creates only its named root", "[scene][model]") {
    Spot::Scene scene;
    const Spot::Model model({});
    Spot::Entity& root = scene.Instantiate(model, "Empty Model");
    CHECK(root.GetName() == "Empty Model");
    CHECK(root.GetChildren().empty());
    CHECK(scene.GetEntityCount() == 1);
}
