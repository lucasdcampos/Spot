#include <spot/core/archive.h>
#include <spot/core/archive_state.h>
#include <spot/core/asset_ref.h>
#include <spot/core/entity_id.h>
#include <spot/core/geometry/mesh_data.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>

TEST_CASE("Core asset references retain ownership and unresolved ids", "[core][assets]") {
    const Spot::AssetId id{1, 2};
    auto asset = std::make_shared<int>(42);
    std::weak_ptr<int> weak = asset;
    Spot::AssetRef<int> reference(id, asset);
    asset.reset();
    CHECK_FALSE(weak.expired());
    CHECK(*reference == 42);
    CHECK(reference.GetId() == id);
    reference = Spot::AssetRef<int>(id, nullptr);
    CHECK(weak.expired());
    CHECK_FALSE(reference);
    CHECK(reference.GetId() == id);
}

TEST_CASE("Core archives expose only the service explicitly supplied by their caller", "[core][serialization]") {
    int service = 42;
    nlohmann::json document;
    Spot::Detail::ArchiveState state{.nodes = {&document}, .context = &service, .contextType = &typeid(int)};
    Spot::Archive archive(state);
    CHECK(archive.GetContext<int>() == &service);
    CHECK(archive.GetContext<float>() == nullptr);

    Spot::Detail::ArchiveState empty{.nodes = {&document}};
    Spot::Archive withoutContext(empty);
    CHECK(withoutContext.GetContext<int>() == nullptr);
}

TEST_CASE("Core entity ids round trip without a scene and filters accept ids", "[core][serialization]") {
    nlohmann::json document;
    auto id = static_cast<Spot::EntityId>(123);
    Spot::Detail::ArchiveState save{.nodes = {&document}};
    Spot::Archive writer(save);
    writer.Field("entity", id);
    CHECK(document["entity"] == 123);

    id = Spot::EntityId::Invalid;
    Spot::Detail::ArchiveState load{.loading = true, .nodes = {&document}};
    Spot::Archive reader(load);
    reader.Field("entity", id);
    CHECK(id == static_cast<Spot::EntityId>(123));
    Spot::FieldOptions options{.entityFilter = [id](Spot::EntityId candidate) { return candidate == id; }};
    CHECK(options.entityFilter(id));
    CHECK_FALSE(options.entityFilter(Spot::EntityId::Invalid));
}

TEST_CASE("CPU geometry and bounds are usable from the core alone", "[core][geometry]") {
    const auto mesh = Spot::MeshData::CreateCube(4.0f);
    const auto box = mesh.GetBoundingBox();
    CHECK((box.GetCenter() == Spot::Vector3{0, 0, 0}));
    CHECK((box.GetSize() == Spot::Vector3{4, 4, 4}));
    const auto sphere = mesh.GetBounds();
    CHECK((sphere.center == Spot::Vector3{0, 0, 0}));
    for (const auto& vertex : mesh.vertices) {
        CHECK(Spot::Length(vertex.position - sphere.center) <= sphere.radius);
    }
}
