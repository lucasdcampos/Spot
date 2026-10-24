#include "spot/audio/audio_assets.h"
#include "spot/assets/asset_formats.h"
#include "spot/assets/asset_manager.h"
#include "spot/audio/sound.h"

#include <algorithm>
#include <utility>

namespace Spot {
namespace {
struct PreparedSound {
    std::uint32_t sampleRate = 0, channelCount = 0;
    std::vector<float> samples;
    std::vector<std::byte> encoded;
    bool streamed = false;
};
} // namespace
void RegisterAudioAssets(AssetManager& assets) {
    assets.RegisterLoader(AssetType::Sound,
                          MakeAssetLoader<PreparedSound>(
                              [](AssetId, std::span<const std::byte> data) -> std::optional<PreparedSound> {
                                  auto sound = Detail::DecodeSound(data);
                                  if (!sound)
                                      return std::nullopt;
                                  PreparedSound prepared{.sampleRate = sound->sampleRate,
                                                         .channelCount = sound->channelCount,
                                                         .samples = std::vector<float>(sound->samples.size()),
                                                         .encoded = std::move(sound->encoded),
                                                         .streamed = sound->streamed};
                                  for (std::size_t i = 0; i < prepared.samples.size(); ++i)
                                      prepared.samples[i] = static_cast<float>(sound->samples[i]) / 32768.0f;
                                  return prepared;
                              },
                              [](AssetManager&, AssetId, PreparedSound& sound) -> std::shared_ptr<const void> {
                                  if (sound.streamed)
                                      return Sound::CreateStream(std::move(sound.encoded));
                                  return std::make_shared<const Sound>(sound.sampleRate, sound.channelCount,
                                                                       std::move(sound.samples));
                              }));
}
} // namespace Spot
