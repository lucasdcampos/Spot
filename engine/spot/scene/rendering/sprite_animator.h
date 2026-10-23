#pragma once
#include "spot/render/sprite_atlas.h"
#include "spot/scene/component.h"

namespace Spot {
class SpriteAnimator final : public Component {
public:
    const AssetRef<SpriteAtlas>& GetAtlas() const { return m_atlas; }
    void SetAtlas(AssetRef<SpriteAtlas> atlas) {
        m_atlas = std::move(atlas);
        OnValidate();
    }
    const std::string& GetAnimation() const { return m_animation; }
    bool Play(std::string animation, bool restart = true);
    void SetPlaying(bool playing) {
        m_playing = playing;
        m_player.SetPlaying(playing);
    }
    bool IsPlaying() const { return m_playing; }
    float GetSpeed() const { return m_speed; }
    void SetSpeed(float speed) { m_speed = speed; }

protected:
    void OnEnable() override { OnUpdate(0); }
    void OnUpdate(float deltaTime) override;
    void OnValidate() override;
    void Serialize(Archive& archive) override;

private:
    AssetRef<SpriteAtlas> m_atlas;
    std::string m_animation;
    float m_speed = 1;
    bool m_playing = true;
    SpriteAnimationPlayer m_player;
    const SpriteAtlas* m_playedAtlas = nullptr;
    std::string m_playedAnimation;
};
} // namespace Spot
