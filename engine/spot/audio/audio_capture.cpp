#include "spot/audio/audio_capture.h"

#include "spot/audio/audio_input.h"
#include "spot/core/debug/log.h"

#include <algorithm>
#include <cmath>

namespace Spot {

AudioCapture::AudioCapture(const AudioCaptureConfig& config)
    : m_sampleRate(std::max(config.sampleRate, 1u)), m_enabled(config.enabled), m_manual(config.manual) {
}

AudioCapture::~AudioCapture() = default;

bool AudioCapture::Start() {
    m_peak = 0.0f;
    if (m_manual) {
        m_pushed.clear();
        m_pushedRead = 0;
        m_capturing = true;
        return true;
    }
    if (!m_enabled) {
        return false;
    }
    if (!m_input) {
        m_input = Detail::AudioInput::Open(m_device, m_sampleRate);
        if (!m_input) {
            Log::GetEngineLogger().Warning("No recording device is available; the microphone will not be heard");
            return false;
        }
        Log::GetEngineLogger().Debug("Recording from '{}' at {} Hz", m_input->GetDeviceName(), m_sampleRate);
    }
    m_input->Clear();
    m_capturing = m_input->SetRecording(true);
    return m_capturing;
}

void AudioCapture::Stop() {
    if (m_capturing && m_input) {
        m_input->SetRecording(false);
    }
    m_capturing = false;
    m_peak = 0.0f;
}

std::size_t AudioCapture::Read(std::span<float> output) {
    std::size_t read = 0;
    if (m_manual) {
        read = std::min(output.size(), m_pushed.size() - m_pushedRead);
        std::copy_n(m_pushed.begin() + static_cast<std::ptrdiff_t>(m_pushedRead), read, output.begin());
        m_pushedRead += read;
        if (m_pushedRead == m_pushed.size()) {
            m_pushed.clear();
            m_pushedRead = 0;
        }
    } else if (m_input) {
        // What the device recorded before it paused is still read after Stop(), so the last words are not lost.
        read = m_input->Read(output);
    }
    if (read > 0) {
        float peak = 0.0f;
        for (const float sample : output.first(read)) {
            peak = std::max(peak, std::abs(sample));
        }
        m_peak = peak;
    } else if (!m_capturing) {
        m_peak = 0.0f;
    }
    return read;
}

void AudioCapture::Push(std::span<const float> samples) {
    if (m_manual && m_capturing) {
        m_pushed.insert(m_pushed.end(), samples.begin(), samples.end());
    }
}

std::vector<std::string> AudioCapture::GetDeviceNames() const {
    if (!m_enabled || m_manual) {
        return {};
    }
    return Detail::AudioInput::GetDeviceNames();
}

void AudioCapture::SetDevice(std::string_view name) {
    if (name == m_device) {
        return;
    }
    m_device = name;
    const bool wasCapturing = m_capturing;
    Stop();
    m_input.reset();
    if (wasCapturing) {
        Start();
    }
}

std::string AudioCapture::GetDeviceName() const {
    return m_input ? m_input->GetDeviceName() : std::string();
}

} // namespace Spot
