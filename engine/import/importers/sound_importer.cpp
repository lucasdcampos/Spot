#include "assets/asset_formats.h"
#include "audio/sound.h"
#include "import/importers/importer.h"

#include <algorithm>
#include <cmath>

namespace Spot::Detail {

// Sounds are stored decoded, so loading them needs no decoder, as 16-bit samples to keep the files small.
bool ImportSound(ImportContext& context) {
    const auto sound = Sound::Load(context.GetSourcePath());
    if (!sound) {
        return false;
    }

    SoundFile file{.sampleRate = sound->GetSampleRate(), .channelCount = sound->GetChannelCount(), .samples = {}};
    file.samples.reserve(sound->GetSamples().size());
    for (const float sample : sound->GetSamples()) {
        file.samples.push_back(static_cast<std::int16_t>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f)));
    }

    BinaryWriter data;
    Encode(data, file);
    return context.WriteAsset(AssetType::Sound, data);
}

} // namespace Spot::Detail
