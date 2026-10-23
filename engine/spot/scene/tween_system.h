#pragma once

#include "spot/scene/scene_system.h"
#include "spot/scene/tween.h"

namespace Spot {

class Component;
class Entity;

class TweenSystem final : public SceneSystem {
public:
    static TweenSystem& Get(Scene& scene);
    TweenId Play(Tween tween, std::function<void()> onComplete = {});
    TweenId Play(Component& owner, Tween tween, std::function<void()> onComplete = {});
    bool Cancel(TweenId id) { return m_player.Cancel(id); }
    void CancelAll() { m_player.CancelAll(); }
    bool IsPlaying(TweenId id) const { return m_player.IsPlaying(id); }

    // Capture ids rather than keeping entity or transform pointers across frames.
    static Tween MoveTo(Entity& entity, Vector3 position, float duration, Ease ease = Ease::Linear);
    static Tween RotateTo(Entity& entity, Quaternion rotation, float duration, Ease ease = Ease::Linear);
    static Tween ScaleTo(Entity& entity, Vector3 scale, float duration, Ease ease = Ease::Linear);

protected:
    void OnUpdate(float deltaTime) override;

private:
    TweenId PlayOwned(Tween tween, Component* owner, std::function<void()> onComplete);
    TweenPlayer m_player;
};

} // namespace Spot
