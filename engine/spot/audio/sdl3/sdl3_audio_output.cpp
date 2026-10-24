#include "spot/audio/audio_output.h"

#include "spot/audio/mixer.h"
#include "spot/core/debug/log.h"
#include "spot/platform/platform.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

namespace Spot::Detail {

namespace {

constexpr int BytesPerFrame = 2 * static_cast<int>(sizeof(float));

} // namespace

struct AudioOutput::Impl {
    explicit Impl(Mixer& mixer) : mixer(mixer), buffer(4096) {}

    ~Impl() {
        // Destroying the stream waits for a callback in progress, so the mixer is no longer used after it.
        SDL_DestroyAudioStream(stream);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        // SDL_Quit() would also close the audio, so SDL keeps running at least until the output is gone.
        Platform::Shutdown();
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // Runs on SDL's audio thread whenever the device needs more data.
    static void SDLCALL FeedStream(void* userdata, SDL_AudioStream* stream, int additionalAmount, int /*totalAmount*/) {
        auto& impl = *static_cast<Impl*>(userdata);
        const int chunkFrames = static_cast<int>(impl.buffer.size()) / 2;

        int framesNeeded = (additionalAmount + BytesPerFrame - 1) / BytesPerFrame;
        while (framesNeeded > 0) {
            const int frames = std::min(framesNeeded, chunkFrames);
            const std::span<float> chunk(impl.buffer.data(), static_cast<std::size_t>(frames) * 2);
            impl.mixer.Mix(chunk);
            SDL_PutAudioStreamData(stream, chunk.data(), frames * BytesPerFrame);
            framesNeeded -= frames;
        }
    }

    Mixer& mixer;
    SDL_AudioStream* stream = nullptr;
    // Interleaved stereo frames, mixed a chunk at a time so the audio thread never allocates.
    std::vector<float> buffer;
};

std::unique_ptr<AudioOutput> AudioOutput::Open(Mixer& mixer) {
    if (!Platform::Initialize()) {
        return nullptr;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        Log::GetEngineLogger().Error("Failed to initialize SDL audio: {}", SDL_GetError());
        Platform::Shutdown();
        return nullptr;
    }

    // From here on the Impl owns the subsystem and releases it however opening ends.
    auto impl = std::make_unique<Impl>(mixer);
    // SDL converts the mix to whatever format the hardware uses.
    const SDL_AudioSpec spec{.format = SDL_AUDIO_F32, .channels = 2, .freq = static_cast<int>(mixer.GetSampleRate())};
    impl->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Impl::FeedStream, impl.get());
    if (!impl->stream) {
        Log::GetEngineLogger().Error("Failed to open the audio device: {}", SDL_GetError());
        return nullptr;
    }
    // Devices opened this way start paused.
    if (!SDL_ResumeAudioStreamDevice(impl->stream)) {
        Log::GetEngineLogger().Error("Failed to start the audio device: {}", SDL_GetError());
        return nullptr;
    }

    return std::unique_ptr<AudioOutput>(new AudioOutput(std::move(impl)));
}

AudioOutput::AudioOutput(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}

AudioOutput::~AudioOutput() = default;

std::string_view AudioOutput::GetBackendName() const {
    const char* driver = SDL_GetCurrentAudioDriver();
    return driver ? driver : "";
}

} // namespace Spot::Detail
