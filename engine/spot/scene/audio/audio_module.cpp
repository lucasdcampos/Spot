#include "spot/scene/audio/audio_module.h"

#include "spot/audio/audio_assets.h"
#include "spot/core/debug/assert.h"
#include "spot/core/debug/profiler.h"
#include "spot/runtime/application.h"
#include "spot/runtime/console.h"
#include "spot/scene/audio/audio_components.h"
#include "spot/scene/audio/audio_system.h"

#include <algorithm>

namespace Spot {

AudioModule::AudioModule(AudioConfig config) : m_config(config) {
}

AudioModule::~AudioModule() = default;

AudioDevice& AudioModule::GetDevice() {
    SPOT_ASSERT(m_device, "The audio device only exists while the application runs");
    return *m_device;
}

void AudioModule::OnStart() {
    Application& application = GetApplication();
    m_device = std::make_unique<AudioDevice>(m_config);
    if (const GameSettings* game = application.GetGameSettings()) {
        game->sections.audio.Apply(*m_device);
    }
    m_gameRevision = application.GetGameSettingsRevision();
    Audio::SetDevice(m_device.get());
    RegisterAudioAssets(application.GetAssets());
    RegisterAudioComponents();
    m_console = std::make_unique<ConsoleRegistration>(Console::Get().RegisterVariable(
        "audio.volume", "Master audio gain", ConsoleFlags::Saved, FormatConsoleValue(m_device->GetMasterVolume()),
        [this] { return FormatConsoleValue(m_device->GetMasterVolume()); },
        [this](std::string_view text) {
            float value = 0.0f;
            if (!ParseConsoleValue(text, value) || value < 0.0f || value > 4.0f) {
                return false;
            }
            m_device->SetMasterVolume(value);
            return true;
        }));
}

void AudioModule::OnStop() {
    m_console.reset();
    Audio::SetDevice(nullptr);
    m_device.reset();
}

void AudioModule::OnUpdate(float /*deltaTime*/) {
    Application& application = GetApplication();
    if (m_gameRevision != application.GetGameSettingsRevision()) {
        m_gameRevision = application.GetGameSettingsRevision();
        if (const GameSettings* game = application.GetGameSettings()) {
            game->sections.audio.Apply(*m_device);
        }
    }
    if (Scene* scene = application.GetScene()) {
        if (auto* audio = scene->GetSystem<AudioSystem>(); audio && audio->IsPaused() != scene->IsPaused())
            audio->SetPaused(scene->IsPaused());
    }
    if (Profiler::IsEnabled()) {
        Profiler::Count("Voices", static_cast<double>(std::max(0, m_device->GetVoiceCount())));
    }
}

} // namespace Spot
