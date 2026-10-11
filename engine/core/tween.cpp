#include "core/tween.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Spot {

float EvaluateEase(Ease ease, float progress) {
    const float t = std::isfinite(progress) ? std::clamp(progress, 0.0f, 1.0f) : 0.0f;
    switch (ease) {
    case Ease::QuadIn:
        return t * t;
    case Ease::QuadOut:
        return t * (2.0f - t);
    case Ease::QuadInOut:
        return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
    case Ease::CubicIn:
        return t * t * t;
    case Ease::CubicOut:
        return 1.0f - std::pow(1.0f - t, 3.0f);
    case Ease::CubicInOut:
        return t < 0.5f ? 4.0f * t * t * t : 1.0f - 4.0f * std::pow(1.0f - t, 3.0f);
    case Ease::SineInOut:
        return (1.0f - std::cos(std::numbers::pi_v<float> * t)) * 0.5f;
    default:
        return t;
    }
}

float Interpolate(float from, float to, float progress) {
    return std::lerp(from, to, progress);
}
Vector2 Interpolate(Vector2 from, Vector2 to, float progress) {
    return from + (to - from) * progress;
}
Vector3 Interpolate(Vector3 from, Vector3 to, float progress) {
    return from + (to - from) * progress;
}
Vector4 Interpolate(Vector4 from, Vector4 to, float progress) {
    return from + (to - from) * progress;
}
Color Interpolate(Color from, Color to, float progress) {
    return {std::lerp(from.r, to.r, progress), std::lerp(from.g, to.g, progress), std::lerp(from.b, to.b, progress),
            std::lerp(from.a, to.a, progress)};
}
Quaternion Interpolate(const Quaternion& from, const Quaternion& to, float progress) {
    return Slerp(from, to, progress);
}

Tween::Tween(float duration, std::function<void(float)> sample, Ease ease)
    : m_valid(std::isfinite(duration) && duration >= 0.0f) {
    if (m_valid) {
        m_steps.push_back({duration, std::move(sample), ease});
    }
}

double Tween::GetDuration() const {
    double duration = 0.0;
    for (const auto& step : m_steps) {
        duration += step.duration;
    }
    return duration;
}

Tween Tween::Sequence(std::vector<Tween> tweens) {
    Tween sequence;
    sequence.m_steps.clear();
    for (auto& tween : tweens) {
        if (!tween.IsValid()) {
            sequence.m_valid = false;
            sequence.m_steps.clear();
            break;
        }
        for (auto& step : tween.m_steps) {
            sequence.m_steps.push_back(std::move(step));
        }
    }
    return sequence;
}

TweenId TweenPlayer::Play(Tween tween, TweenOptions options) {
    if (!tween.IsValid() || m_nextId == 0) {
        return TweenId::Invalid;
    }
    const auto id = static_cast<TweenId>(m_nextId++);
    m_playbacks.push_back({id, std::move(tween), std::move(options)});
    return id;
}

bool TweenPlayer::Cancel(TweenId id) {
    return std::erase_if(m_playbacks, [id](const Playback& playback) { return playback.id == id; }) != 0;
}

void TweenPlayer::CancelAll() {
    m_playbacks.clear();
}
bool TweenPlayer::IsPlaying(TweenId id) const {
    return std::ranges::any_of(m_playbacks, [id](const Playback& playback) { return playback.id == id; });
}

void TweenPlayer::Advance(TweenId id, float deltaTime) {
    const auto find = [&] { return std::ranges::find(m_playbacks, id, &Playback::id); };
    auto found = find();
    if (found == m_playbacks.end()) {
        return;
    }
    const auto active = found->options.canAdvance;
    const auto alive = found->options.isAlive;
    if (alive && !alive()) {
        Cancel(id);
        return;
    }
    if (active && !active()) {
        return;
    }
    found = find();
    if (found == m_playbacks.end()) {
        return;
    }
    found->elapsed += deltaTime;
    for (;;) {
        if (alive && !alive()) {
            Cancel(id);
            return;
        }
        if (active && !active()) {
            return;
        }
        found = find();
        if (found == m_playbacks.end()) {
            return;
        }
        if (found->step == found->tween.m_steps.size()) {
            auto complete = found->options.onComplete;
            m_playbacks.erase(found);
            if (complete) {
                complete();
            }
            return;
        }
        const auto& step = found->tween.m_steps[found->step];
        const double localTime = found->elapsed - found->offset;
        const bool done = localTime >= step.duration;
        const float progress =
            step.duration == 0.0f ? 1.0f : static_cast<float>(std::clamp(localTime / step.duration, 0.0, 1.0));
        auto sample = step.sample;
        const float eased = EvaluateEase(step.ease, progress);
        if (done) {
            found->offset += step.duration;
            ++found->step;
        }
        if (sample) {
            sample(eased);
        }
        if (!done) {
            return;
        }
    }
}

void TweenPlayer::Update(float deltaTime) {
    if (m_updating || !std::isfinite(deltaTime) || deltaTime <= 0.0f) {
        return;
    }
    m_updating = true;
    struct Reset {
        bool& updating;
        ~Reset() { updating = false; }
    } reset{m_updating};
    std::vector<TweenId> pending;
    pending.reserve(m_playbacks.size());
    for (const auto& playback : m_playbacks) {
        pending.push_back(playback.id);
    }
    for (const auto id : pending) {
        auto found = std::ranges::find(m_playbacks, id, &Playback::id);
        if (found == m_playbacks.end()) {
            continue;
        }
        auto onError = found->options.onError;
        try {
            Advance(id, deltaTime);
        } catch (...) {
            Cancel(id);
            if (onError) {
                onError(std::current_exception());
            } else {
                throw;
            }
        }
    }
}

} // namespace Spot
