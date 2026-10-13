#include "scene/sprite_animator.h"
#include "core/profiler.h"
#include "scene/archive.h"
#include "scene/entity.h"
#include "scene/sprite_renderer.h"
#include <cmath>

namespace Spot {
void SpriteAnimator::OnValidate() {
    m_player = {};
    m_playedAtlas = nullptr;
    m_playedAnimation.clear();
}
bool SpriteAnimator::Play(std::string animation, bool restart) {
    if (!m_atlas || m_atlas->FindAnimation(animation) < 0)
        return false;
    if (restart)
        OnValidate();
    m_animation = std::move(animation);
    m_playing = true;
    OnUpdate(0);
    return true;
}
void SpriteAnimator::OnUpdate(float dt) {
    SPOT_PROFILE_SCOPE("SpriteAnimator");
    if (!m_atlas)
        return;
    if (m_playedAtlas != m_atlas.Get() || m_playedAnimation != m_animation) {
        m_player.Play(*m_atlas, m_atlas->FindAnimation(m_animation));
        m_playedAtlas = m_atlas.Get();
        m_playedAnimation = m_animation;
    }
    m_player.SetPlaying(m_playing);
    if (std::isfinite(m_speed) && m_speed >= 0)
        m_player.Advance(dt * m_speed);
    m_playing = m_player.IsPlaying();
    if (auto* renderer = GetEntity().GetComponent<SpriteRenderer>(); renderer && m_player.GetSprite() >= 0) {
        renderer->SetAtlas(m_atlas);
        renderer->SetFrame(m_player.GetSprite());
    }
}
void SpriteAnimator::Serialize(Archive& archive) {
    archive.Field("atlas", m_atlas);
    archive.Field("animation", m_animation);
    archive.Field("speed", m_speed, {.min = 0, .max = 100});
    archive.Field("playing", m_playing);
}
} // namespace Spot
