#include "spot/audio/sound.h"

#include "spot/audio/sound_decoder.h"
#include "spot/core/debug/assert.h"
#include "spot/core/debug/log.h"
#include "spot/core/io/paths.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
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

constexpr std::array<std::byte, 4> OggSignature{std::byte{'O'}, std::byte{'g'}, std::byte{'g'}, std::byte{'S'}};

bool IsOgg(std::span<const std::byte> data) {
    return data.size() >= OggSignature.size() && std::ranges::equal(data.first(OggSignature.size()), OggSignature);
}

std::optional<std::vector<std::byte>> ReadFile(const std::filesystem::path& path) {
    // Read through std::ifstream rather than the decoders' own file access, which mishandles non-ASCII paths on
    // Windows.
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const auto view = std::as_bytes(std::span(bytes));
    return std::vector<std::byte>(view.begin(), view.end());
}

class WavDecoder final : public Detail::SoundDecoder {
public:
    ~WavDecoder() override {
        if (m_open) {
            drwav_uninit(&m_wav);
        }
    }

    bool Open(std::span<const std::byte> data) {
        m_open = drwav_init_memory(&m_wav, data.data(), data.size(), nullptr);
        return m_open;
    }

    std::uint32_t GetSampleRate() const override { return m_wav.sampleRate; }
    std::uint32_t GetChannelCount() const override { return m_wav.channels; }
    std::uint64_t GetFrameCount() const override { return m_wav.totalPCMFrameCount; }

    std::size_t Decode(std::span<float> output) override {
        return static_cast<std::size_t>(
            drwav_read_pcm_frames_f32(&m_wav, output.size() / m_wav.channels, output.data()));
    }

    bool Rewind() override { return drwav_seek_to_pcm_frame(&m_wav, 0); }

private:
    drwav m_wav{};
    bool m_open = false;
};

class VorbisDecoder final : public Detail::SoundDecoder {
public:
    ~VorbisDecoder() override {
        if (m_vorbis) {
            stb_vorbis_close(m_vorbis);
        }
    }

    bool Open(std::span<const std::byte> data) {
        int error = 0;
        m_vorbis = stb_vorbis_open_memory(reinterpret_cast<const unsigned char*>(data.data()),
                                          static_cast<int>(data.size()), &error, nullptr);
        if (!m_vorbis) {
            return false;
        }
        m_info = stb_vorbis_get_info(m_vorbis);
        m_frameCount = stb_vorbis_stream_length_in_samples(m_vorbis);
        return true;
    }

    std::uint32_t GetSampleRate() const override { return m_info.sample_rate; }
    std::uint32_t GetChannelCount() const override { return static_cast<std::uint32_t>(m_info.channels); }
    std::uint64_t GetFrameCount() const override { return m_frameCount; }

    std::size_t Decode(std::span<float> output) override {
        const int frames = stb_vorbis_get_samples_float_interleaved(m_vorbis, m_info.channels, output.data(),
                                                                    static_cast<int>(output.size()));
        return frames > 0 ? static_cast<std::size_t>(frames) : 0;
    }

    bool Rewind() override { return stb_vorbis_seek_start(m_vorbis) != 0; }

private:
    stb_vorbis* m_vorbis = nullptr;
    stb_vorbis_info m_info{};
    std::uint64_t m_frameCount = 0;
};

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

std::unique_ptr<Detail::SoundDecoder> Detail::SoundDecoder::Open(std::span<const std::byte> encoded) {
    if (encoded.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        Log::GetEngineLogger().Error("Sound data is too large to decode");
        return nullptr;
    }
    std::unique_ptr<SoundDecoder> decoder;
    if (IsOgg(encoded)) {
        auto vorbis = std::make_unique<VorbisDecoder>();
        if (vorbis->Open(encoded)) {
            decoder = std::move(vorbis);
        }
    } else {
        auto wav = std::make_unique<WavDecoder>();
        if (wav->Open(encoded)) {
            decoder = std::move(wav);
        }
    }
    if (!decoder) {
        Log::GetEngineLogger().Error("Failed to open the sound's data for streaming");
        return nullptr;
    }
    if (!IsSupportedChannelCount(decoder->GetChannelCount()) || decoder->GetSampleRate() == 0) {
        Log::GetEngineLogger().Error("Unsupported audio: {} channels at {} Hz; only mono and stereo are supported",
                                     decoder->GetChannelCount(), decoder->GetSampleRate());
        return nullptr;
    }
    return decoder;
}

Sound::Sound(std::uint32_t sampleRate, std::uint32_t channelCount, std::vector<float> samples)
    : m_sampleRate(sampleRate), m_channelCount(channelCount), m_frameCount(0), m_samples(std::move(samples)) {
    SPOT_ASSERT(sampleRate > 0, "A sound needs a sample rate");
    SPOT_ASSERT(IsSupportedChannelCount(channelCount), "A sound is mono or stereo, not {} channels", channelCount);
    SPOT_ASSERT(m_samples.size() % channelCount == 0, "{} samples do not make whole frames of {} channels",
                m_samples.size(), channelCount);
    m_frameCount = m_samples.size() / channelCount;
}

Sound::Sound(std::uint32_t sampleRate, std::uint32_t channelCount, std::uint64_t frameCount,
             std::vector<std::byte> encoded)
    : m_sampleRate(sampleRate), m_channelCount(channelCount), m_frameCount(frameCount), m_encoded(std::move(encoded)) {
}

std::shared_ptr<Sound> Sound::LoadStream(const std::filesystem::path& path) {
    auto bytes = ReadFile(path);
    if (!bytes) {
        Log::GetEngineLogger().Error("Could not open sound '{}'", PathToUtf8(path));
        return nullptr;
    }
    auto sound = CreateStream(std::move(*bytes));
    if (!sound) {
        Log::GetEngineLogger().Error("Could not stream sound '{}'", PathToUtf8(path));
    }
    return sound;
}

std::shared_ptr<Sound> Sound::CreateStream(std::vector<std::byte> encoded) {
    const auto decoder = Detail::SoundDecoder::Open(encoded);
    if (!decoder) {
        return nullptr;
    }
    // Streamed sounds play while they decode, so they are never empty, or nothing would tell them apart.
    if (decoder->GetFrameCount() == 0 || encoded.empty()) {
        Log::GetEngineLogger().Error("A streamed sound needs at least one frame");
        return nullptr;
    }
    return std::shared_ptr<Sound>(
        new Sound(decoder->GetSampleRate(), decoder->GetChannelCount(), decoder->GetFrameCount(), std::move(encoded)));
}

std::shared_ptr<Sound> Sound::Load(const std::filesystem::path& path) {
    const auto bytes = ReadFile(path);
    if (!bytes) {
        Log::GetEngineLogger().Error("Could not open sound '{}'", PathToUtf8(path));
        return nullptr;
    }

    auto sound = LoadFromMemory(*bytes);
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

    if (IsOgg(data)) {
        return DecodeVorbis(data);
    }
    return DecodeWav(data);
}

} // namespace Spot
