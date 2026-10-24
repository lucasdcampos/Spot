#pragma once

#include "spot/audio/audio_device.h"
#include "spot/runtime/module.h"

#include <cstdint>
#include <memory>

namespace Spot {

class ConsoleRegistration;

// Audio for an application: owns the AudioDevice, which Audio::GetDevice() then returns, given the game's audio
// settings; registers the audio asset loaders and components; and pauses the scene's sounds while the application
// pauses its scene.
class AudioModule final : public Module {
public:
    explicit AudioModule(AudioConfig config = {});
    ~AudioModule() override;

    // Only available while the application runs. Without audio output, because it is disabled or the machine has
    // none, sounds play silently.
    AudioDevice& GetDevice();

protected:
    void OnStart() override;
    void OnStop() override;
    void OnUpdate(float deltaTime) override;

private:
    AudioConfig m_config;
    std::unique_ptr<AudioDevice> m_device;
    std::unique_ptr<ConsoleRegistration> m_console;
    std::uint64_t m_gameRevision = 0;
};

} // namespace Spot
