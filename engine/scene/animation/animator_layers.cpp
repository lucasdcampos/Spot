#include "scene/animation/animator.h"

#include "animation/pose.h"
#include "core/log.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Spot {

bool Animator::SetLayers(std::span<const AnimationLayer> layers) {
    if (layers.size() > MaxLayers) {
        return false;
    }
    std::vector<AnimationLayer> config(layers.begin(), layers.end());
    std::vector<LayerRuntime> runtime;
    std::unordered_set<std::string> names;
    for (auto& layer : config) {
        if (layer.name.empty() || !names.insert(layer.name).second || !Accepts(layer.clip) ||
            !std::isfinite(layer.weight) || !std::isfinite(layer.speed) || layer.mode < AnimationLayerMode::Override ||
            layer.mode > AnimationLayerMode::Additive) {
            Log::GetEngineLogger().Warning("Animator rejected an invalid animation layer");
            return false;
        }
        layer.weight = std::clamp(layer.weight, 0.0f, 1.0f);
        LayerRuntime instance;
        instance.mask.resize(m_skeleton->bones.size());
        if (!BuildBoneMask(*m_skeleton, layer.mask, instance.mask)) {
            Log::GetEngineLogger().Warning("Animation layer '{}' refers to a missing bone", layer.name);
            return false;
        }
        runtime.push_back(std::move(instance));
    }
    m_layers = std::move(config);
    m_layerRuntime = std::move(runtime);
    return true;
}

bool Animator::SetLayerWeight(std::string_view name, float weight) {
    if (!std::isfinite(weight)) {
        return false;
    }
    for (auto& layer : m_layers) {
        if (layer.name == name) {
            layer.weight = std::clamp(weight, 0.0f, 1.0f);
            return true;
        }
    }
    return false;
}

std::size_t Animator::AdvanceLayers(float deltaTime, std::span<LayerEventStep> events) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < m_layerRuntime.size(); ++i) {
        auto& runtime = m_layerRuntime[i];
        const auto& config = m_layers[i];
        const float from = runtime.time;
        runtime.time += deltaTime * m_speed * config.speed;
        if (config.weight > 0 && count < events.size()) {
            events[count++] = {config.clip, from, runtime.time, config.loop, runtime.emitStart};
        }
        runtime.emitStart = false;
    }
    return count;
}

void Animator::ApplyLayers() {
    for (std::size_t i = 0; i < m_layerRuntime.size(); ++i) {
        const auto& config = m_layers[i];
        if (config.weight <= 0) {
            continue;
        }
        const auto& runtime = m_layerRuntime[i];
        Pose::Sample(*m_skeleton, *config.clip, runtime.time, config.loop, m_layerPose);
        ApplyAnimationLayer(m_local, m_layerPose, m_layerReference, runtime.mask, config.weight, config.mode, m_local);
    }
}

} // namespace Spot
