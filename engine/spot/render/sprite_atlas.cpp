#include "spot/render/sprite_atlas.h"
#include "spot/assets/asset_archive.h"
#include "spot/core/archive.h"
#include "spot/core/archive_state.h"
#include "spot/graphics/texture.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Spot {
void SpriteFrame::Serialize(Archive& archive) {
    archive.Field("name", name);
    archive.Field("position", position);
    archive.Field("size", size);
}
void SpriteAnimationFrame::Serialize(Archive& archive) {
    archive.Field("sprite", sprite);
    archive.Field("duration", duration);
}
void SpriteAnimationClip::Serialize(Archive& archive) {
    archive.Field("name", name);
    archive.Field("loop", loop);
    archive.Field("frames", frames);
}
void SpriteAtlas::Serialize(Archive& archive) {
    archive.Field("texture", texture);
    archive.Field("textureSize", textureSize);
    archive.Field("sprites", sprites);
    archive.Field("animations", animations);
}
bool SpriteAtlas::IsValid() const {
    const auto finite = [](Vector2 v) { return std::isfinite(v.x) && std::isfinite(v.y); };
    if ((!texture && !texture.GetId().IsValid()) || !finite(textureSize) || textureSize.x <= 0 || textureSize.y <= 0 ||
        sprites.empty())
        return false;
    if (texture && (textureSize.x != static_cast<float>(texture->GetWidth()) ||
                    textureSize.y != static_cast<float>(texture->GetHeight())))
        return false;
    std::unordered_set<std::string> names;
    for (const auto& sprite : sprites) {
        if (sprite.name.empty() || !names.insert(sprite.name).second || !finite(sprite.position) ||
            !finite(sprite.size) || sprite.position.x < 0 || sprite.position.y < 0 || sprite.size.x <= 0 ||
            sprite.size.y <= 0 || sprite.position.x + sprite.size.x > textureSize.x ||
            sprite.position.y + sprite.size.y > textureSize.y)
            return false;
    }
    names.clear();
    for (const auto& clip : animations) {
        if (clip.name.empty() || !names.insert(clip.name).second || clip.frames.empty())
            return false;
        for (const auto& frame : clip.frames) {
            if (frame.sprite < 0 || static_cast<std::size_t>(frame.sprite) >= sprites.size() ||
                !std::isfinite(frame.duration) || frame.duration <= 0)
                return false;
        }
    }
    return true;
}
TextureRegion SpriteAtlas::GetRegion(int sprite) const {
    if (sprite < 0 || static_cast<std::size_t>(sprite) >= sprites.size() || textureSize.x <= 0 || textureSize.y <= 0)
        return {};
    const auto& frame = sprites[static_cast<std::size_t>(sprite)];
    return {{frame.position.x / textureSize.x, frame.position.y / textureSize.y},
            {(frame.position.x + frame.size.x) / textureSize.x, (frame.position.y + frame.size.y) / textureSize.y}};
}
int SpriteAtlas::FindSprite(std::string_view name) const {
    auto found = std::ranges::find(sprites, name, &SpriteFrame::name);
    return found == sprites.end() ? -1 : static_cast<int>(found - sprites.begin());
}
int SpriteAtlas::FindAnimation(std::string_view name) const {
    auto found = std::ranges::find(animations, name, &SpriteAnimationClip::name);
    return found == animations.end() ? -1 : static_cast<int>(found - animations.begin());
}
std::string SpriteAtlas::SaveToString() const {
    nlohmann::json json;
    Detail::ArchiveState state{.nodes = {&json}};
    Archive archive(state);
    auto copy = *this;
    copy.Serialize(archive);
    json["version"] = 1;
    return json.dump(2);
}
std::optional<SpriteAtlas> SpriteAtlas::LoadFromString(std::string_view text, AssetManager* assets) {
    auto json = nlohmann::json::parse(text, nullptr, false);
    if (!json.is_object() || !json.contains("version") || json["version"] != 1)
        return std::nullopt;
    SpriteAtlas atlas;
    Detail::ArchiveState state{.loading = true, .nodes = {&json}, .assets = assets};
    Archive archive(state);
    atlas.Serialize(archive);
    return atlas.IsValid() ? std::optional{std::move(atlas)} : std::nullopt;
}
void SpriteAnimationPlayer::Play(const SpriteAtlas& atlas, int animation, bool restart) {
    if (!restart && m_atlas == &atlas && m_animation == animation)
        return;
    m_atlas = &atlas;
    m_animation = animation;
    m_time = 0;
    m_playing = atlas.IsValid() && animation >= 0 && static_cast<std::size_t>(animation) < atlas.animations.size() &&
                !atlas.animations[static_cast<std::size_t>(animation)].frames.empty();
}
void SpriteAnimationPlayer::Advance(float deltaTime) {
    if (!m_playing || !std::isfinite(deltaTime) || deltaTime <= 0)
        return;
    const auto& clip = m_atlas->animations[static_cast<std::size_t>(m_animation)];
    double duration = 0;
    for (const auto& frame : clip.frames)
        duration += frame.duration;
    if (!std::isfinite(duration) || duration <= 0) {
        m_playing = false;
        return;
    }
    const double time = static_cast<double>(m_time) + deltaTime;
    m_time = static_cast<float>(clip.loop ? std::fmod(time, duration) : std::min(time, duration));
    if (!clip.loop && time >= duration)
        m_playing = false;
}
int SpriteAnimationPlayer::GetSprite() const {
    if (!m_atlas || m_animation < 0 || static_cast<std::size_t>(m_animation) >= m_atlas->animations.size())
        return -1;
    const auto& clip = m_atlas->animations[static_cast<std::size_t>(m_animation)];
    double time = m_time;
    for (const auto& frame : clip.frames) {
        if (time < frame.duration)
            return frame.sprite;
        time -= frame.duration;
    }
    return clip.frames.empty() ? -1 : clip.frames.back().sprite;
}
} // namespace Spot
