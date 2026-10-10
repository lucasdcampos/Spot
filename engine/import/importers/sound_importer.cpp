#include "assets/asset_formats.h"
#include "audio/sound.h"
#include "import/importers/importer.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace Spot::Detail {

// Sounds are stored decoded, so loading them needs no decoder, as 16-bit samples to keep the files small. Streamed ones
// keep their source's data, which they decode as they play.
bool ImportSound(ImportContext& context) {
    if (context.GetMeta().sound.stream) {
        const auto sound = Sound::LoadStream(context.GetSourcePath());
        if (!sound) {
            return false;
        }
        const std::span<const std::byte> encoded = sound->GetEncodedData();
        BinaryWriter data;
        Encode(data, SoundFile{.sampleRate = sound->GetSampleRate(),
                               .channelCount = sound->GetChannelCount(),
                               .streamed = true,
                               .encoded = std::vector<std::byte>(encoded.begin(), encoded.end())});
        return context.WriteAsset(AssetType::Sound, data);
    }

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
