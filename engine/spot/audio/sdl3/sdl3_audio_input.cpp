#include "spot/audio/audio_input.h"

#include "spot/core/debug/log.h"
#include "spot/platform/platform.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <utility>

namespace Spot::Detail {

namespace {

// Keeps SDL's audio running while it lives, as the output does for itself.
class AudioSubsystem {
public:
    AudioSubsystem() {
        if (!Platform::Initialize()) {
            return;
        }
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            Log::GetEngineLogger().Error("Failed to initialize SDL audio: {}", SDL_GetError());
            Platform::Shutdown();
            return;
        }
        m_ready = true;
    }

    ~AudioSubsystem() {
        if (m_ready) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            Platform::Shutdown();
        }
    }

    AudioSubsystem(const AudioSubsystem&) = delete;
    AudioSubsystem& operator=(const AudioSubsystem&) = delete;

    bool IsReady() const { return m_ready; }

private:
    bool m_ready = false;
};

SDL_AudioDeviceID FindRecordingDevice(std::string_view name) {
    if (name.empty()) {
        return SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
    }
    int count = 0;
    SDL_AudioDeviceID* devices = SDL_GetAudioRecordingDevices(&count);
    SDL_AudioDeviceID found = SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
    for (int i = 0; devices && i < count; ++i) {
        const char* deviceName = SDL_GetAudioDeviceName(devices[i]);
        if (deviceName && name == deviceName) {
            found = devices[i];
            break;
        }
    }
    SDL_free(devices);
    if (found == SDL_AUDIO_DEVICE_DEFAULT_RECORDING) {
        Log::GetEngineLogger().Warning("No recording device is called '{}'; recording from the default one", name);
    }
    return found;
}

} // namespace

struct AudioInput::Impl {
    ~Impl() {
        // Destroying the stream closes the device it opened.
        SDL_DestroyAudioStream(stream);
    }

    AudioSubsystem subsystem;
    SDL_AudioStream* stream = nullptr;
};

std::unique_ptr<AudioInput> AudioInput::Open(std::string_view device, std::uint32_t sampleRate) {
    auto impl = std::make_unique<Impl>();
    if (!impl->subsystem.IsReady()) {
        return nullptr;
    }
    // SDL converts whatever the hardware records to this.
    const SDL_AudioSpec spec{.format = SDL_AUDIO_F32, .channels = 1, .freq = static_cast<int>(sampleRate)};
    impl->stream = SDL_OpenAudioDeviceStream(FindRecordingDevice(device), &spec, nullptr, nullptr);
    if (!impl->stream) {
        Log::GetEngineLogger().Error("Failed to open the recording device: {}", SDL_GetError());
        return nullptr;
    }
    // Devices opened this way start paused, which is what recording waits for.
    return std::unique_ptr<AudioInput>(new AudioInput(std::move(impl)));
}

std::vector<std::string> AudioInput::GetDeviceNames() {
    const AudioSubsystem subsystem;
    std::vector<std::string> names;
    if (!subsystem.IsReady()) {
        return names;
    }
    int count = 0;
    SDL_AudioDeviceID* devices = SDL_GetAudioRecordingDevices(&count);
    for (int i = 0; devices && i < count; ++i) {
        if (const char* name = SDL_GetAudioDeviceName(devices[i])) {
            names.emplace_back(name);
        }
    }
    SDL_free(devices);
    return names;
}

AudioInput::AudioInput(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {
}

AudioInput::~AudioInput() = default;

bool AudioInput::SetRecording(bool recording) {
    const bool done =
        recording ? SDL_ResumeAudioStreamDevice(m_impl->stream) : SDL_PauseAudioStreamDevice(m_impl->stream);
    if (!done) {
        Log::GetEngineLogger().Error("Failed to {} recording: {}", recording ? "start" : "stop", SDL_GetError());
    }
    return done;
}

std::size_t AudioInput::Read(std::span<float> output) {
    const int available = SDL_GetAudioStreamAvailable(m_impl->stream);
    if (available <= 0 || output.empty()) {
        return 0;
    }
    const int bytes =
        std::min(available, static_cast<int>(std::min<std::size_t>(output.size(), 1u << 20) * sizeof(float)));
    const int read =
        SDL_GetAudioStreamData(m_impl->stream, output.data(), bytes - bytes % static_cast<int>(sizeof(float)));
    return read > 0 ? static_cast<std::size_t>(read) / sizeof(float) : 0;
}

void AudioInput::Clear() {
    SDL_ClearAudioStream(m_impl->stream);
}

std::string AudioInput::GetDeviceName() const {
    const char* name = SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(m_impl->stream));
    return name ? name : "";
}

} // namespace Spot::Detail
