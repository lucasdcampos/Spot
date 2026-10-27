#include "gameplay/core/trace_volume.h"

#include <algorithm>

namespace Gameplay::Traces {

std::optional<VolumeHit> Raycast(Spot::Scene& scene, Spot::Vector3 origin, Spot::Vector3 direction, float maxDistance) {
    std::optional<VolumeHit> nearest;
    scene.ForEachComponent<TraceVolume>([&](TraceVolume& volume) {
        const auto distance = volume.Trace(origin, direction, nearest ? nearest->distance : maxDistance);
        if (distance && *distance <= maxDistance && (!nearest || *distance < nearest->distance)) {
            nearest = VolumeHit{&volume, origin + direction * *distance, *distance};
        }
    });
    return nearest;
}

BlastMedium Blast(Spot::Scene& scene, Spot::Vector3 center, float radius) {
    BlastMedium medium = BlastMedium::None;
    scene.ForEachComponent<TraceVolume>(
        [&](TraceVolume& volume) { medium = std::max(medium, volume.OnBlast(center, radius)); });
    return medium;
}

} // namespace Gameplay::Traces
