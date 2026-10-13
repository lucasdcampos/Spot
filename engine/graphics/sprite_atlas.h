#pragma once

#include "assets/asset_ref.h"
#include "graphics/quad_batch.h"

#include <optional>
#include <string>
#include <vector>

namespace Spot {
class Archive;
class AssetManager;
class Texture;
struct SpriteFrame {
    std::string name = {};
    Vector2 position = {};
    Vector2 size = {1, 1};
    void Serialize(Archive& archive);
};
struct SpriteAnimationFrame {
    int sprite = 0;
    float duration = 0.1f;
    void Serialize(Archive& archive);
};
struct SpriteAnimationClip {
    std::string name = {};
    bool loop = true;
    std::vector<SpriteAnimationFrame> frames = {};
    void Serialize(Archive& archive);
};
// Authored .atlas metadata references an ordinary imported texture; pixel rectangles start at its top-left.
struct SpriteAtlas {
    AssetRef<Texture> texture = {};
    Vector2 textureSize = {1, 1};
    std::vector<SpriteFrame> sprites = {};
    std::vector<SpriteAnimationClip> animations = {};
    void Serialize(Archive& archive);
    bool IsValid() const;
    TextureRegion GetRegion(int sprite) const;
    int FindSprite(std::string_view name) const;
    int FindAnimation(std::string_view name) const;
    std::string SaveToString() const;
    static std::optional<SpriteAtlas> LoadFromString(std::string_view text, AssetManager* assets = nullptr);
};

// Variable-duration playback, independent of scenes and rendering. No allocations during Advance().
class SpriteAnimationPlayer {
public:
    void Play(const SpriteAtlas& atlas, int animation, bool restart = true);
    void Advance(float deltaTime);
    void SetPlaying(bool playing) { m_playing = playing; }
    bool IsPlaying() const { return m_playing; }
    int GetSprite() const;
    float GetTime() const { return m_time; }

private:
    const SpriteAtlas* m_atlas = nullptr;
    int m_animation = -1;
    float m_time = 0;
    bool m_playing = false;
};
} // namespace Spot
