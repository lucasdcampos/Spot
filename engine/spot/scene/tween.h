#pragma once

#include "spot/core/math/color.h"
#include "spot/core/math/quaternion.h"
#include "spot/core/math/vector.h"

#include <cstdint>
#include <exception>
#include <functional>
#include <utility>
#include <vector>

namespace Spot {

enum class Ease { Linear, QuadIn, QuadOut, QuadInOut, CubicIn, CubicOut, CubicInOut, SineInOut };
float EvaluateEase(Ease ease, float progress);
float Interpolate(float from, float to, float progress);
Vector2 Interpolate(Vector2 from, Vector2 to, float progress);
Vector3 Interpolate(Vector3 from, Vector3 to, float progress);
Vector4 Interpolate(Vector4 from, Vector4 to, float progress);
Color Interpolate(Color from, Color to, float progress);
Quaternion Interpolate(const Quaternion& from, const Quaternion& to, float progress);

// A reusable description. Playback owns its own cursor, so the same tween can be played more than once.
class Tween {
public:
    explicit Tween(float duration = 0.0f, std::function<void(float)> sample = {}, Ease ease = Ease::Linear);
    bool IsValid() const { return m_valid; }
    double GetDuration() const;
    static Tween Delay(float duration) { return Tween(duration); }
    static Tween Sequence(std::vector<Tween> tweens);

    template <typename T, typename Setter>
    static Tween Value(float duration, T from, T to, Setter setter, Ease ease = Ease::Linear) {
        return Tween(
            duration,
            [from, to, setter = std::move(setter)](float progress) mutable { setter(Interpolate(from, to, progress)); },
            ease);
    }

private:
    friend class TweenPlayer;
    struct Step {
        float duration;
        std::function<void(float)> sample;
        Ease ease;
    };
    std::vector<Step> m_steps;
    bool m_valid = true;
};

enum class TweenId : std::uint64_t { Invalid = 0 };

struct TweenOptions {
    std::function<void()> onComplete;
    // Missing predicates mean playback has no owner. An inactive owner pauses; a missing owner cancels.
    std::function<bool()> isAlive;
    std::function<bool()> canAdvance;
    // Without a handler, a failing tween is cancelled and its exception propagates to Update's caller.
    std::function<void(std::exception_ptr)> onError;
};

class TweenPlayer {
public:
    TweenId Play(Tween tween, TweenOptions options = {});
    bool Cancel(TweenId id);
    void CancelAll();
    bool IsPlaying(TweenId id) const;
    void Update(float deltaTime);

private:
    struct Playback {
        TweenId id;
        Tween tween;
        TweenOptions options;
        double elapsed = 0.0;
        double offset = 0.0;
        std::size_t step = 0;
    };
    void Advance(TweenId id, float deltaTime);
    std::vector<Playback> m_playbacks;
    std::uint64_t m_nextId = 1;
    bool m_updating = false;
};

} // namespace Spot
