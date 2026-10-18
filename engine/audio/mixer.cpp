#include "audio/mixer.h"

#include "core/assert.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <utility>

namespace Spot::Detail {

namespace {

// The gains the volume and the pan give the left and right channels.
std::pair<float, float> GetGains(const PlayParams& params) {
    const float volume = std::max(params.volume, 0.0f);
    const float pan = std::clamp(params.pan, -1.0f, 1.0f);
    return {volume * std::min(1.0f - pan, 1.0f), volume * std::min(1.0f + pan, 1.0f)};
}

float StepToward(float value, float target, float step) {
    return value < target ? std::min(value + step, target) : std::max(value - step, target);
}

std::size_t ToSlot(AudioBusId bus) {
    return static_cast<std::size_t>(bus);
}

AudioBusId ToBus(std::size_t slot) {
    return static_cast<AudioBusId>(slot);
}

} // namespace

Mixer::Mixer(std::uint32_t sampleRate, bool streamThread) : m_sampleRate(sampleRate), m_useStreamThread(streamThread) {
    SPOT_ASSERT(sampleRate > 0, "The mixer needs a sample rate");
    m_voices.reserve(MaxRenderVoices);
    m_gameVoices.reserve(64);
    m_buses[0] = {.used = true};
    m_gameBuses[0] = {.used = true, .name = "Master"};
    m_busOrder.reserve(MaxBuses);
    m_busBuffers.resize(MaxBuses * ChunkFrames * 2);
    for (std::atomic<float>& peak : m_peaks) {
        peak.store(0.0f, std::memory_order_relaxed);
    }
}

Mixer::~Mixer() {
    {
        std::scoped_lock lock(m_streamsMutex);
        m_stopStreams = true;
    }
    m_streamsChanged.notify_all();
    if (m_streamThread.joinable()) {
        m_streamThread.join();
    }
}

void Mixer::UpdateStreams() {
    std::vector<std::shared_ptr<SoundStream>> streams;
    {
        std::scoped_lock lock(m_streamsMutex);
        std::erase_if(m_streams, [](const std::weak_ptr<SoundStream>& stream) { return stream.expired(); });
        for (const auto& stream : m_streams) {
            if (auto locked = stream.lock()) {
                streams.push_back(std::move(locked));
            }
        }
    }
    for (const auto& stream : streams) {
        stream->Fill();
    }
}

void Mixer::RunStreamThread() {
    std::unique_lock lock(m_streamsMutex);
    while (!m_stopStreams) {
        lock.unlock();
        UpdateStreams();
        lock.lock();
        // A tenth of the buffer, so streams refill long before they run dry.
        m_streamsChanged.wait_for(lock, std::chrono::milliseconds(50), [this] { return m_stopStreams; });
    }
}

SoundInstance Mixer::Play(std::shared_ptr<const Sound> sound, const PlayParams& params) {
    if (!sound) {
        return {};
    }

    // Decoded ahead before it plays, outside the lock, so the first mix has frames to play.
    std::shared_ptr<SoundStream> stream;
    if (sound->IsStreamed()) {
        stream = std::make_shared<SoundStream>(sound, params.loop);
        if (!stream->IsValid()) {
            return {};
        }
        stream->Fill();
        std::scoped_lock lock(m_streamsMutex);
        m_streams.push_back(stream);
        if (m_useStreamThread && !m_streamThread.joinable()) {
            m_streamThread = std::thread([this] { RunStreamThread(); });
        }
    }

    std::scoped_lock lock(m_mutex);
    CollectEnded();

    // Voices are in the order they started, so the first found is the oldest.
    const auto isActive = [](const GameVoice& voice) { return !voice.stopping; };
    if (params.maxInstances > 0) {
        const auto sameSound = [&](const GameVoice& voice) { return isActive(voice) && voice.sound == sound; };
        if (std::ranges::count_if(m_gameVoices, sameSound) >= params.maxInstances) {
            StopVoice(*std::ranges::find_if(m_gameVoices, sameSound), 0.0f);
        }
    }
    if (std::ranges::count_if(m_gameVoices, isActive) >= m_maxVoices) {
        GameVoice* victim = nullptr;
        for (GameVoice& voice : m_gameVoices) {
            if (isActive(voice) && (!victim || voice.params.priority < victim->params.priority)) {
                victim = &voice;
            }
        }
        if (!victim || victim->params.priority > params.priority) {
            return {};
        }
        StopVoice(*victim, 0.0f);
    }
    // The audio thread holds a voice of its own for each, fading ones included, in room it never grows.
    if (m_gameVoices.size() >= MaxRenderVoices) {
        return {};
    }

    const std::uint32_t id = m_nextId++;
    // The default instance must never match, even after the counter wraps around.
    if (m_nextId == 0) {
        m_nextId = 1;
    }
    PlayParams played = params;
    if (ToSlot(played.bus) >= MaxBuses || !m_gameBuses[ToSlot(played.bus)].used) {
        played.bus = AudioBusId::Master;
    }
    m_gameVoices.push_back({.id = id, .sound = sound, .stream = stream, .params = played});
    Send({.type = CommandType::Start, .id = id, .params = played, .sound = sound.get(), .stream = stream.get()});
    return {id};
}

void Mixer::Stop(SoundInstance instance, float fadeOut) {
    std::scoped_lock lock(m_mutex);
    CollectEnded();
    if (GameVoice* voice = FindVoice(instance)) {
        StopVoice(*voice, fadeOut);
    }
}

void Mixer::StopAll() {
    std::scoped_lock lock(m_mutex);
    CollectEnded();
    for (GameVoice& voice : m_gameVoices) {
        if (!voice.stopping) {
            StopVoice(voice, 0.0f);
        }
    }
}

int Mixer::GetMaxVoices() const {
    std::scoped_lock lock(m_mutex);
    return m_maxVoices;
}

void Mixer::SetMaxVoices(int count) {
    std::scoped_lock lock(m_mutex);
    m_maxVoices = std::clamp(count, 1, static_cast<int>(MaxRenderVoices / 2));
}

int Mixer::GetVoiceCount() const {
    std::scoped_lock lock(m_mutex);
    CollectEnded();
    return static_cast<int>(
        std::ranges::count_if(m_gameVoices, [](const GameVoice& voice) { return !voice.stopping; }));
}

void Mixer::StopVoice(GameVoice& voice, float fadeOut) {
    voice.stopping = true;
    Send({.type = CommandType::Stop, .id = voice.id, .value = fadeOut});
}

float Mixer::GetFadeStep(float seconds) const {
    return 1.0f / std::max(seconds * static_cast<float>(m_sampleRate), 1.0f);
}

bool Mixer::IsPlaying(SoundInstance instance) const {
    std::scoped_lock lock(m_mutex);
    CollectEnded();
    return FindVoice(instance) != nullptr;
}

bool Mixer::IsPaused(SoundInstance instance) const {
    std::scoped_lock lock(m_mutex);
    CollectEnded();
    const GameVoice* voice = FindVoice(instance);
    return voice && voice->pauseRequested;
}

void Mixer::SetPaused(SoundInstance instance, bool paused) {
    std::scoped_lock lock(m_mutex);
    CollectEnded();
    GameVoice* voice = FindVoice(instance);
    if (!voice || voice->pauseRequested == paused) {
        return;
    }
    voice->pauseRequested = paused;
    Send({.type = CommandType::SetPaused, .id = voice->id, .flag = paused});
}

void Mixer::SetVolume(SoundInstance instance, float volume) {
    std::scoped_lock lock(m_mutex);
    if (GameVoice* voice = FindVoice(instance)) {
        voice->params.volume = volume;
        Send({.type = CommandType::SetParams, .id = voice->id, .params = voice->params});
    }
}

void Mixer::SetPan(SoundInstance instance, float pan) {
    std::scoped_lock lock(m_mutex);
    if (GameVoice* voice = FindVoice(instance)) {
        voice->params.pan = pan;
        Send({.type = CommandType::SetParams, .id = voice->id, .params = voice->params});
    }
}

void Mixer::SetPitch(SoundInstance instance, float pitch) {
    std::scoped_lock lock(m_mutex);
    if (GameVoice* voice = FindVoice(instance)) {
        voice->params.pitch = pitch;
        Send({.type = CommandType::SetParams, .id = voice->id, .params = voice->params});
    }
}

float Mixer::GetMasterVolume() const {
    return GetBusVolume(AudioBusId::Master);
}

void Mixer::SetMasterVolume(float volume) {
    SetBusVolume(AudioBusId::Master, volume);
}

std::optional<AudioBusId> Mixer::AddBus(std::string_view name, AudioBusId parent) {
    std::scoped_lock lock(m_mutex);
    const auto free = std::ranges::find(m_gameBuses, false, &GameBus::used);
    if (free == m_gameBuses.end()) {
        return std::nullopt;
    }
    const auto slot = static_cast<std::size_t>(free - m_gameBuses.begin());
    *free = {.used = true, .name = std::string(name), .parent = FindBusSlot(parent) ? ToSlot(parent) : 0};
    m_peaks[slot * 2].store(0.0f, std::memory_order_relaxed);
    m_peaks[slot * 2 + 1].store(0.0f, std::memory_order_relaxed);
    SendBus(slot);
    return ToBus(slot);
}

bool Mixer::RemoveBus(AudioBusId bus) {
    std::scoped_lock lock(m_mutex);
    const GameBus* removed = FindBusSlot(bus);
    if (!removed || bus == AudioBusId::Master) {
        return false;
    }
    const std::size_t slot = ToSlot(bus);
    const std::size_t parent = removed->parent;
    for (std::size_t child = 0; child < MaxBuses; ++child) {
        if (m_gameBuses[child].used && m_gameBuses[child].parent == slot) {
            m_gameBuses[child].parent = parent;
            SendBus(child);
        }
    }
    for (GameVoice& voice : m_gameVoices) {
        if (voice.params.bus == bus) {
            voice.params.bus = ToBus(parent);
            Send({.type = CommandType::SetParams, .id = voice.id, .params = voice.params});
        }
    }
    m_gameBuses[slot] = {};
    SendBus(slot);
    return true;
}

bool Mixer::HasBus(AudioBusId bus) const {
    std::scoped_lock lock(m_mutex);
    return FindBusSlot(bus) != nullptr;
}

std::optional<AudioBusId> Mixer::FindBus(std::string_view name) const {
    std::scoped_lock lock(m_mutex);
    for (std::size_t slot = 0; slot < MaxBuses; ++slot) {
        if (m_gameBuses[slot].used && m_gameBuses[slot].name == name) {
            return ToBus(slot);
        }
    }
    return std::nullopt;
}

std::vector<AudioBusId> Mixer::GetBuses() const {
    std::scoped_lock lock(m_mutex);
    std::vector<AudioBusId> buses;
    for (std::size_t slot = 0; slot < MaxBuses; ++slot) {
        if (m_gameBuses[slot].used) {
            buses.push_back(ToBus(slot));
        }
    }
    return buses;
}

std::string Mixer::GetBusName(AudioBusId bus) const {
    std::scoped_lock lock(m_mutex);
    const GameBus* found = FindBusSlot(bus);
    return found ? found->name : std::string();
}

void Mixer::SetBusName(AudioBusId bus, std::string_view name) {
    std::scoped_lock lock(m_mutex);
    if (GameBus* found = FindBusSlot(bus)) {
        found->name = name;
    }
}

std::optional<AudioBusId> Mixer::GetBusParent(AudioBusId bus) const {
    std::scoped_lock lock(m_mutex);
    const GameBus* found = FindBusSlot(bus);
    if (!found || bus == AudioBusId::Master) {
        return std::nullopt;
    }
    return ToBus(found->parent);
}

bool Mixer::SetBusParent(AudioBusId bus, AudioBusId parent) {
    std::scoped_lock lock(m_mutex);
    GameBus* found = FindBusSlot(bus);
    if (!found || bus == AudioBusId::Master || !FindBusSlot(parent)) {
        return false;
    }
    // Climbing from the parent must never reach the bus, or the tree would become a loop.
    for (std::size_t slot = ToSlot(parent);; slot = m_gameBuses[slot].parent) {
        if (slot == ToSlot(bus)) {
            return false;
        }
        if (slot == 0) {
            break;
        }
    }
    found->parent = ToSlot(parent);
    SendBus(ToSlot(bus));
    return true;
}

float Mixer::GetBusVolume(AudioBusId bus) const {
    std::scoped_lock lock(m_mutex);
    const GameBus* found = FindBusSlot(bus);
    return found ? found->volume : 0.0f;
}

void Mixer::SetBusVolume(AudioBusId bus, float volume) {
    std::scoped_lock lock(m_mutex);
    if (GameBus* found = FindBusSlot(bus)) {
        found->volume = volume;
        SendBus(ToSlot(bus));
    }
}

bool Mixer::IsBusMuted(AudioBusId bus) const {
    std::scoped_lock lock(m_mutex);
    const GameBus* found = FindBusSlot(bus);
    return found && found->muted;
}

void Mixer::SetBusMuted(AudioBusId bus, bool muted) {
    std::scoped_lock lock(m_mutex);
    if (GameBus* found = FindBusSlot(bus)) {
        found->muted = muted;
        SendBus(ToSlot(bus));
    }
}

bool Mixer::IsBusSolo(AudioBusId bus) const {
    std::scoped_lock lock(m_mutex);
    const GameBus* found = FindBusSlot(bus);
    return found && found->solo;
}

void Mixer::SetBusSolo(AudioBusId bus, bool solo) {
    std::scoped_lock lock(m_mutex);
    if (GameBus* found = FindBusSlot(bus)) {
        found->solo = solo;
        SendBus(ToSlot(bus));
    }
}

AudioPeak Mixer::GetBusPeak(AudioBusId bus) const {
    const std::size_t slot = ToSlot(bus);
    if (slot >= MaxBuses) {
        return {};
    }
    return {m_peaks[slot * 2].load(std::memory_order_relaxed), m_peaks[slot * 2 + 1].load(std::memory_order_relaxed)};
}

void Mixer::Send(const Command& command) {
    // Full only when the audio thread has not mixed for thousands of commands; it soon makes room.
    while (!m_commands.Push(command)) {
        std::this_thread::yield();
    }
}

void Mixer::SendBus(std::size_t slot) {
    const GameBus& bus = m_gameBuses[slot];
    Send({.type = CommandType::SetBus,
          .id = static_cast<std::uint32_t>(slot),
          .value = bus.volume,
          .flag = bus.used,
          .muted = bus.muted,
          .solo = bus.solo,
          .parent = bus.parent});
}

void Mixer::CollectEnded() const {
    // The sounds of voices that ended are let go here, on the game's side, so the audio thread never frees them.
    bool any = false;
    while (const auto id = m_ended.Pop()) {
        for (GameVoice& voice : m_gameVoices) {
            if (voice.id == *id) {
                voice.ended = true;
                any = true;
            }
        }
    }
    if (any) {
        std::erase_if(m_gameVoices, [](const GameVoice& voice) { return voice.ended; });
    }
}

void Mixer::ApplyCommands() {
    while (const auto command = m_commands.Pop()) {
        switch (command->type) {
        case CommandType::Start: {
            const auto [leftGain, rightGain] = GetGains(command->params);
            Voice voice{.sound = command->sound,
                        .id = command->id,
                        .params = command->params,
                        .stream = command->stream,
                        .leftGain = leftGain,
                        .rightGain = rightGain,
                        .fade = command->params.fadeIn > 0.0f ? 0.0f : 1.0f,
                        .fadeStep = command->params.fadeIn > 0.0f ? GetFadeStep(command->params.fadeIn) : 0.0f};
            if (voice.stream) {
                AdvanceStream(voice);
                AdvanceStream(voice);
                voice.streamFrame = 0;
            }
            // Room for it was kept by the game's side.
            m_voices.push_back(voice);
            break;
        }
        case CommandType::Stop:
            if (Voice* voice = FindRenderVoice(command->id)) {
                voice->stopping = true;
                // A paused voice is silent already.
                if (voice->paused) {
                    voice->ended = true;
                } else {
                    voice->fadeStep = -GetFadeStep(std::max(command->value, ClickFadeSeconds));
                }
            }
            break;
        case CommandType::SetPaused:
            if (Voice* voice = FindRenderVoice(command->id); voice && !voice->stopping) {
                voice->pauseRequested = command->flag;
                const float step = GetFadeStep(ClickFadeSeconds);
                voice->fadeStep = command->flag ? -step : step;
                if (!command->flag) {
                    voice->paused = false;
                }
            }
            break;
        case CommandType::SetParams:
            if (Voice* voice = FindRenderVoice(command->id)) {
                voice->params = command->params;
            }
            break;
        case CommandType::SetBus: {
            Bus& bus = m_buses[command->id];
            const bool added = command->flag && !bus.used;
            bus.used = command->flag;
            bus.parent = command->parent;
            bus.volume = command->value;
            bus.muted = command->muted;
            bus.solo = command->solo;
            if (added) {
                bus.gain = 1.0f;
            }
            UpdateBusOrder();
            break;
        }
        }
    }
}

void Mixer::Mix(std::span<float> output) {
    SPOT_ASSERT(output.size() % 2 == 0, "Stereo output needs an even number of samples, not {}", output.size());
    std::ranges::fill(output, 0.0f);
    // Never waits for the game: what it asked for since the last mix arrives through the queue.
    ApplyCommands();
    std::array<AudioPeak, MaxBuses> peaks = {};
    const auto measure = [&peaks](std::size_t slot, std::span<const float> samples) {
        for (std::size_t i = 0; i + 1 < samples.size(); i += 2) {
            peaks[slot].left = std::max(peaks[slot].left, std::abs(samples[i]));
            peaks[slot].right = std::max(peaks[slot].right, std::abs(samples[i + 1]));
        }
    };

    for (std::size_t offset = 0; offset < output.size(); offset += ChunkFrames * 2) {
        const std::size_t count = std::min(ChunkFrames * 2, output.size() - offset);
        for (std::size_t slot = 0; slot < MaxBuses; ++slot) {
            if (m_buses[slot].used) {
                std::ranges::fill(GetBusBuffer(slot, count), 0.0f);
            }
        }
        for (Voice& voice : m_voices) {
            if (!voice.paused && !voice.ended) {
                const std::size_t slot = ToSlot(voice.params.bus);
                MixVoice(voice, GetBusBuffer(slot, count), m_buses[slot].soloSilenced ? 0.0f : 1.0f);
            }
        }
        // Each bus scales what reached it and passes it up, after the buses below it did.
        for (const std::size_t slot : m_busOrder) {
            Bus& bus = m_buses[slot];
            const std::span<float> samples = GetBusBuffer(slot, count);
            ApplyBusGain(bus, samples);
            const std::span<float> parent = GetBusBuffer(bus.parent, count);
            for (std::size_t i = 0; i < count; ++i) {
                parent[i] += samples[i];
            }
            measure(slot, samples);
        }
        // Clipping keeps loud mixes within the range every output format can hold.
        const std::span<float> mixed = GetBusBuffer(0, count);
        ApplyBusGain(m_buses[0], mixed);
        measure(0, mixed);
        for (std::size_t i = 0; i < count; ++i) {
            output[offset + i] = std::clamp(mixed[i], -1.0f, 1.0f);
        }
    }

    for (std::size_t slot = 0; slot < MaxBuses; ++slot) {
        m_peaks[slot * 2].store(peaks[slot].left, std::memory_order_relaxed);
        m_peaks[slot * 2 + 1].store(peaks[slot].right, std::memory_order_relaxed);
    }
    // Voices that ended go back to the game, which lets their sounds go; within the room kept, nothing is freed.
    for (const Voice& voice : m_voices) {
        if (voice.ended) {
            m_ended.Push(voice.id);
        }
    }
    std::erase_if(m_voices, [](const Voice& voice) { return voice.ended; });
}

void Mixer::ApplyBusGain(Bus& bus, std::span<float> samples) const {
    const float target = bus.muted ? 0.0f : std::max(bus.volume, 0.0f);
    const float step = GetFadeStep(ClickFadeSeconds);
    for (std::size_t i = 0; i + 1 < samples.size(); i += 2) {
        bus.gain = StepToward(bus.gain, target, step);
        samples[i] *= bus.gain;
        samples[i + 1] *= bus.gain;
    }
}

const Mixer::GameBus* Mixer::FindBusSlot(AudioBusId bus) const {
    const std::size_t slot = ToSlot(bus);
    return slot < MaxBuses && m_gameBuses[slot].used ? &m_gameBuses[slot] : nullptr;
}

Mixer::GameBus* Mixer::FindBusSlot(AudioBusId bus) {
    return const_cast<GameBus*>(std::as_const(*this).FindBusSlot(bus));
}

void Mixer::UpdateBusOrder() {
    const bool anySolo = std::ranges::any_of(m_buses, [](const Bus& bus) { return bus.used && bus.solo; });
    for (std::size_t slot = 0; slot < MaxBuses; ++slot) {
        bool heard = !anySolo;
        for (std::size_t up = slot; !heard && m_buses[slot].used; up = m_buses[up].parent) {
            heard = m_buses[up].solo;
            if (up == 0) {
                break;
            }
        }
        m_buses[slot].soloSilenced = !heard;
    }

    std::array<int, MaxBuses> depths = {};
    m_busOrder.clear();
    for (std::size_t slot = 1; slot < MaxBuses; ++slot) {
        if (!m_buses[slot].used) {
            continue;
        }
        for (std::size_t up = slot; up != 0; up = m_buses[up].parent) {
            ++depths[slot];
        }
        m_busOrder.push_back(slot);
    }
    std::ranges::stable_sort(m_busOrder, [&depths](std::size_t a, std::size_t b) { return depths[a] > depths[b]; });
}

std::span<float> Mixer::GetBusBuffer(std::size_t slot, std::size_t samples) {
    return std::span<float>(m_busBuffers).subspan(slot * ChunkFrames * 2, samples);
}

Mixer::GameVoice* Mixer::FindVoice(SoundInstance instance) {
    return const_cast<GameVoice*>(std::as_const(*this).FindVoice(instance));
}

const Mixer::GameVoice* Mixer::FindVoice(SoundInstance instance) const {
    const auto it = std::ranges::find_if(m_gameVoices, [&](const GameVoice& voice) {
        return voice.id == instance.id && !voice.stopping && !voice.ended;
    });
    return it != m_gameVoices.end() ? &*it : nullptr;
}

Mixer::Voice* Mixer::FindRenderVoice(std::uint32_t id) {
    const auto it = std::ranges::find(m_voices, id, &Voice::id);
    return it != m_voices.end() ? &*it : nullptr;
}

bool Mixer::StepEnvelope(Voice& voice, float leftTarget, float rightTarget) const {
    if (voice.fadeStep != 0.0f) {
        voice.fade = std::clamp(voice.fade + voice.fadeStep, 0.0f, 1.0f);
        if (voice.fade == 0.0f) {
            voice.fadeStep = 0.0f;
            if (voice.stopping) {
                voice.ended = true;
                return false;
            }
            if (voice.pauseRequested) {
                voice.paused = true;
                return false;
            }
        } else if (voice.fade == 1.0f) {
            voice.fadeStep = 0.0f;
        }
    }
    // A change of the whole range takes as long as a fade.
    const float rampStep = GetFadeStep(ClickFadeSeconds);
    voice.leftGain = StepToward(voice.leftGain, leftTarget, rampStep);
    voice.rightGain = StepToward(voice.rightGain, rightTarget, rampStep);
    return true;
}

void Mixer::AdvanceStream(Voice& voice) {
    std::array<float, 2> next = {};
    if (!voice.stream->ReadFrame(next)) {
        // Past the end comes silence, as for sounds in memory; before it, the stream ran dry, and waits.
        if (!voice.stream->IsFinished()) {
            return;
        }
        ++voice.streamTail;
    }
    voice.frame0 = voice.frame1;
    voice.frame1 = next;
    ++voice.streamFrame;
}

bool Mixer::ReadStreamFrame(Voice& voice, std::array<float, 2>& frame) {
    const auto target = static_cast<std::uint64_t>(voice.position);
    while (voice.streamFrame < target) {
        const std::uint64_t before = voice.streamFrame;
        AdvanceStream(voice);
        if (voice.streamFrame == before) {
            // The worker is late: the voice holds its place until frames come.
            voice.position = static_cast<double>(voice.streamFrame);
            break;
        }
    }
    // Two silent frames in, the first frame played between is past the end.
    if (voice.streamTail >= 2) {
        voice.ended = true;
        return false;
    }
    const auto t = static_cast<float>(voice.position - static_cast<double>(voice.streamFrame));
    frame = {std::lerp(voice.frame0[0], voice.frame1[0], t), std::lerp(voice.frame0[1], voice.frame1[1], t)};
    return true;
}

void Mixer::MixVoice(Voice& voice, std::span<float> output, float gain) const {
    const Sound& sound = *voice.sound;
    // The game's side holds the sound until the voice ended.
    const std::span<const float> samples = sound.GetSamples();
    const std::uint64_t frameCount = sound.GetFrameCount();
    const std::uint32_t channelCount = sound.GetChannelCount();
    const bool loop = voice.params.loop;
    if (frameCount == 0) {
        voice.ended = true;
        return;
    }

    const double step = static_cast<double>(sound.GetSampleRate()) / static_cast<double>(m_sampleRate) *
                        static_cast<double>(std::max(voice.params.pitch, 0.0f));
    auto [leftTarget, rightTarget] = GetGains(voice.params);
    leftTarget *= gain;
    rightTarget *= gain;

    if (voice.stream) {
        for (std::size_t i = 0; i + 1 < output.size(); i += 2) {
            std::array<float, 2> frame = {};
            if (!StepEnvelope(voice, leftTarget, rightTarget) || !ReadStreamFrame(voice, frame)) {
                return;
            }
            output[i] += frame[0] * voice.leftGain * voice.fade;
            output[i + 1] += frame[1] * voice.rightGain * voice.fade;
            voice.position += step;
        }
        return;
    }

    // Past the end, a looping sound continues from its start and any other sound is silent.
    const auto sampleAt = [&](std::uint64_t frame, std::uint32_t channel) {
        if (frame >= frameCount) {
            if (!loop) {
                return 0.0f;
            }
            frame %= frameCount;
        }
        return samples[frame * channelCount + channel];
    };

    for (std::size_t i = 0; i + 1 < output.size(); i += 2) {
        if (voice.position >= static_cast<double>(frameCount)) {
            if (!loop) {
                break;
            }
            voice.position = std::fmod(voice.position, static_cast<double>(frameCount));
        }

        if (!StepEnvelope(voice, leftTarget, rightTarget)) {
            return;
        }

        // Linear interpolation between neighboring frames converts the sample rate and applies the pitch.
        const auto frame = static_cast<std::uint64_t>(voice.position);
        const auto t = static_cast<float>(voice.position - static_cast<double>(frame));
        const float left = std::lerp(sampleAt(frame, 0), sampleAt(frame + 1, 0), t);
        const float right = channelCount == 2 ? std::lerp(sampleAt(frame, 1), sampleAt(frame + 1, 1), t) : left;
        output[i] += left * voice.leftGain * voice.fade;
        output[i + 1] += right * voice.rightGain * voice.fade;
        voice.position += step;
    }

    if (!loop && voice.position >= static_cast<double>(frameCount)) {
        voice.ended = true;
    }
}

} // namespace Spot::Detail
