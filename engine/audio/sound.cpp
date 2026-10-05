#include "audio/sound.h"

#include "core/assert.h"
#include "core/log.h"
#include "core/paths.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <limits>
#include <utility>

// The decoders' code generation warns about variables it cannot prove initialized, which /external does not
// silence because the warning comes from the optimizer rather than the parser.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4701)
#endif

#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#define DR_WAV_NO_WCHAR
#include <dr_wav.h>

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include <stb_vorbis.c>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace Spot {

namespace {

bool IsSupportedChannelCount(std::uint64_t channelCount) {
    return channelCount == 1 || channelCount == 2;
}

std::shared_ptr<Sound> DecodeWav(std::span<const std::byte> data) {
    unsigned int channels = 0;
    unsigned int sampleRate = 0;
    drwav_uint64 frameCount = 0;
    float* samples = drwav_open_memory_and_read_pcm_frames_f32(data.data(), data.size(), &channels, &sampleRate,
                                                               &frameCount, nullptr);
    if (!samples) {
        Log::GetEngineLogger().Error("Failed to decode WAV audio");
        return nullptr;
    }

    std::vector<float> copy(samples, samples + frameCount * channels);
    drwav_free(samples, nullptr);
    if (!IsSupportedChannelCount(channels) || sampleRate == 0) {
        Log::GetEngineLogger().Error("Unsupported WAV audio: {} channels at {} Hz; only mono and stereo are supported",
                                     channels, sampleRate);
        return nullptr;
    }
    return std::make_shared<Sound>(sampleRate, channels, std::move(copy));
}

std::shared_ptr<Sound> DecodeVorbis(std::span<const std::byte> data) {
    int error = 0;
    stb_vorbis* vorbis = stb_vorbis_open_memory(reinterpret_cast<const unsigned char*>(data.data()),
                                                static_cast<int>(data.size()), &error, nullptr);
    if (!vorbis) {
        Log::GetEngineLogger().Error("Failed to decode Ogg Vorbis audio (error {})", error);
        return nullptr;
    }

    const stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    if (!IsSupportedChannelCount(static_cast<std::uint64_t>(info.channels))) {
        stb_vorbis_close(vorbis);
        Log::GetEngineLogger().Error("Unsupported Ogg Vorbis audio: {} channels; only mono and stereo are supported",
                                     info.channels);
        return nullptr;
    }

    // The stream length is only a hint for the allocation; decoding runs until the decoder has no more frames.
    std::vector<float> samples;
    samples.reserve(static_cast<std::size_t>(stb_vorbis_stream_length_in_samples(vorbis)) *
                    static_cast<std::size_t>(info.channels));
    std::array<float, 4096> chunk{};
    while (true) {
        const int frames = stb_vorbis_get_samples_float_interleaved(vorbis, info.channels, chunk.data(),
                                                                    static_cast<int>(chunk.size()));
        if (frames <= 0) {
            break;
        }
        samples.insert(samples.end(), chunk.begin(), chunk.begin() + frames * info.channels);
    }
    stb_vorbis_close(vorbis);

    return std::make_shared<Sound>(info.sample_rate, static_cast<std::uint32_t>(info.channels), std::move(samples));
}

} // namespace

Sound::Sound(std::uint32_t sampleRate, std::uint32_t channelCount, std::vector<float> samples)
    : m_sampleRate(sampleRate), m_channelCount(channelCount), m_samples(std::move(samples)) {
    SPOT_ASSERT(sampleRate > 0, "A sound needs a sample rate");
    SPOT_ASSERT(IsSupportedChannelCount(channelCount), "A sound is mono or stereo, not {} channels", channelCount);
    SPOT_ASSERT(m_samples.size() % channelCount == 0, "{} samples do not make whole frames of {} channels",
                m_samples.size(), channelCount);
}

std::shared_ptr<Sound> Sound::Load(const std::filesystem::path& path) {
    // Read through std::ifstream rather than the decoders' own file access, which mishandles non-ASCII paths on
    // Windows.
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        Log::GetEngineLogger().Error("Could not open sound '{}'", PathToUtf8(path));
        return nullptr;
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

    auto sound = LoadFromMemory(std::as_bytes(std::span(bytes)));
    if (!sound) {
        Log::GetEngineLogger().Error("Could not decode sound '{}'", PathToUtf8(path));
    }
    return sound;
}

std::shared_ptr<Sound> Sound::LoadFromMemory(std::span<const std::byte> data) {
    if (data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        Log::GetEngineLogger().Error("Sound data is too large to decode");
        return nullptr;
    }

    constexpr std::array<std::byte, 4> OggSignature{std::byte{'O'}, std::byte{'g'}, std::byte{'g'}, std::byte{'S'}};
    if (data.size() >= OggSignature.size() && std::ranges::equal(data.first(OggSignature.size()), OggSignature)) {
        return DecodeVorbis(data);
    }
    return DecodeWav(data);
}

} // namespace Spot
