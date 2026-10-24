#include "registered_assets.h"
#include "temp_directory.h"
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <limits>
#include <spot.h>
#include <spot_import.h>

using namespace Spot;
namespace {
SpriteAtlas Atlas() {
    SpriteAtlas atlas;
    atlas.texture = AssetRef<Texture>(AssetId::Generate(), nullptr);
    atlas.textureSize = {32, 16};
    atlas.sprites = {{"idle", {0, 0}, {16, 16}}, {"run", {16, 0}, {16, 16}}};
    atlas.animations = {{"walk", true, {{0, 0.1f}, {1, 0.2f}}}, {"once", false, {{1, 0.1f}, {0, 0.2f}}}};
    return atlas;
}
} // namespace
TEST_CASE("Sprite atlases validate bounds names timing and versioned metadata", "[sprites]") {
    auto atlas = Atlas();
    REQUIRE(atlas.IsValid());
    CHECK(atlas.FindSprite("run") == 1);
    CHECK(atlas.FindAnimation("absent") == -1);
    CHECK(atlas.GetRegion(1).min == Vector2{0.5f, 0});
    CHECK(atlas.GetRegion(1).max == Vector2{1, 1});
    auto restored = SpriteAtlas::LoadFromString(atlas.SaveToString());
    REQUIRE(restored);
    CHECK(restored->texture.GetId() == atlas.texture.GetId());
    CHECK(restored->animations[0].frames.size() == 2);
    atlas.sprites[1].position.x = 20;
    CHECK_FALSE(atlas.IsValid());
    atlas = Atlas();
    atlas.sprites[1].name = "idle";
    CHECK_FALSE(atlas.IsValid());
    atlas = Atlas();
    atlas.animations[0].frames[0].duration = 0;
    CHECK_FALSE(atlas.IsValid());
    atlas = Atlas();
    atlas.animations[0].frames[0].sprite = 8;
    CHECK_FALSE(atlas.IsValid());
    CHECK_FALSE(SpriteAtlas::LoadFromString("{\"version\":2}"));
    CHECK_FALSE(SpriteAtlas::LoadFromString("invalid"));
}
TEST_CASE("Sprite animation supports variable durations looping pause and completion", "[sprites]") {
    auto atlas = Atlas();
    SpriteAnimationPlayer player;
    player.Play(atlas, 0);
    CHECK(player.GetSprite() == 0);
    player.Advance(0.11f);
    CHECK(player.GetSprite() == 1);
    player.SetPlaying(false);
    player.Advance(1);
    CHECK(player.GetSprite() == 1);
    player.SetPlaying(true);
    player.Advance(0.21f);
    CHECK(player.GetSprite() == 0);
    player.Play(atlas, 0, false);
    CHECK(player.GetTime() == Catch::Approx(0.02f));
    player.Advance(1000);
    CHECK(player.IsPlaying());
    player.Play(atlas, 1);
    player.Advance(100);
    CHECK_FALSE(player.IsPlaying());
    CHECK(player.GetSprite() == 0);
    player.Advance(std::numeric_limits<float>::infinity());
    CHECK(player.GetSprite() == 0);
    player.Play(atlas, -1);
    CHECK_FALSE(player.IsPlaying());
    CHECK(player.GetSprite() == -1);
}
TEST_CASE("Sprite animator drives the renderer and serializes authored fields", "[sprites]") {
    Scene scene;
    auto& entity = scene.CreateEntity("Animated");
    auto& renderer = entity.AddComponent<SpriteRenderer>();
    auto& animator = entity.AddComponent<SpriteAnimator>();
    animator.SetAtlas(std::make_shared<SpriteAtlas>(Atlas()));
    REQUIRE(animator.Play("walk"));
    scene.Update(0.11f);
    CHECK(renderer.GetFrame() == 1);
    animator.SetPlaying(false);
    scene.Update(1);
    CHECK(renderer.GetFrame() == 1);
    animator.SetEnabled(false);
    scene.Update(1);
    CHECK(renderer.GetFrame() == 1);
    animator.SetEnabled(true);
    REQUIRE(animator.Play("once"));
    scene.Update(0.4f);
    CHECK(renderer.GetFrame() == 0);
    CHECK_FALSE(animator.IsPlaying());
    CHECK_FALSE(animator.Play("missing"));
    animator.SetSpeed(2);
    auto saved = SaveComponentToString(animator);
    Scene other;
    auto& copy = other.CreateEntity().AddComponent<SpriteAnimator>();
    REQUIRE(LoadComponentFromString(copy, saved));
    CHECK(copy.GetAnimation() == "once");
    CHECK(copy.GetSpeed() == 2);
}
TEST_CASE("Sprite atlas import caches metadata and loads from shipped assets", "[sprites][assets]") {
    TempDirectory temp;
    auto source = temp.GetPath() / "source", imported = temp.GetPath() / "imported";
    std::filesystem::create_directories(source);
    AssetDatabase database(source, imported);
    REQUIRE(Image(32, 16, std::vector<std::uint8_t>(32 * 16 * 4, 255)).SavePng(source / "sheet.png"));
    REQUIRE(database.Refresh() == 1);
    auto texture = database.FindByPath("sheet.png");
    REQUIRE(texture);
    auto atlas = Atlas();
    atlas.texture = AssetRef<Texture>(*texture, nullptr);
    {
        std::ofstream file(source / "sheet.atlas");
        file << atlas.SaveToString();
    }
    REQUIRE(database.Refresh() == 1);
    CHECK(database.Refresh() == 0);
    auto id = database.FindByPath("sheet.atlas");
    REQUIRE(id);
    Spot::Tests::RegisteredAssetManager manager(imported);
    auto loaded = manager.Load<SpriteAtlas>(*id);
    REQUIRE(loaded);
    CHECK(loaded->texture.GetId() == *texture);
    CHECK(loaded->sprites.size() == 2);
    std::array changed{*texture};
    auto invalidated = Spot::DevToolsModule::ReloadAssets(manager, changed);
    CHECK(std::ranges::find(invalidated, *id) != invalidated.end());
    auto next = manager.Load<SpriteAtlas>(*id);
    REQUIRE(next);
    CHECK(next.Get() != loaded.Get());
}
