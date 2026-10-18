# Audio

Audio is mixed by the engine and only delivered by the backend: `engine/audio/` has the engine's mixer, and the SDL3
backend (`AudioOutput`) only pulls mixed samples from it on its own thread.

## Sounds

A `Sound` (`engine/audio/sound.h`) is decoded audio in CPU memory: 32-bit float samples, mono or stereo. Sounds never
change, so one sound can play many times at once. They load as assets (`.wav` and `.ogg` sources) or from raw files.

## Streaming

Music and long ambience would take megabytes decoded, so they stream instead: a sound whose import settings have
`stream` on keeps its source's WAV or Ogg Vorbis data in its imported file, and each playback decodes it as it goes
(`Sound::IsStreamed()`; from raw files, `Sound::LoadStream()` and `Sound::CreateStream()`). Every playback has its own
decoder and a ring buffer holding half a second, decoded before it starts playing and kept full by a thread of the
mixer's; the audio thread only reads it, without locks. Looping streams go on from their start seamlessly. Should
the thread fall behind, the playback waits where it is rather than skip.

## Playing

`AudioDevice` (`engine/audio/audio_device.h`) plays sounds on the default output device. The application owns one,
reached with `Application::GetAudioDevice()`:

```cpp
auto shot = Spot::Assets::Load<Spot::Sound>("sounds/shot.wav");
const Spot::SoundInstance instance = audio.Play(shot.GetShared(), {.volume = 0.8f, .pan = -0.2f});
audio.SetPitch(instance, 1.2f);
```

`PlayParams` sets the volume, the pan (from -1, left, to 1, right), the pitch (playback speed, which shifts the
tone) and looping. `Play()` returns a `SoundInstance` that refers to that one playback: once it ends, the instance
stops matching anything, so keeping it is safe and calls with it do nothing. Instances can be stopped, paused and
changed while they play; `SetMasterVolume()` scales everything.

Every `AudioDevice` method may be called from any thread. The device never fails: without an output, because audio
is disabled (`AudioConfig::enabled`) or no device can be opened, it stays usable and plays nothing.

## Sounds in scenes

An `AudioSource` component (`engine/audio/audio_source.h`) plays a sound from its entity: its sound, its bus (by the
name the mixer gives it; the master bus when empty), volume, pitch, looping, whether it plays on start, and whether
it is spatial, with the distances and the rolloff of its fading. `Play()` starts it, or resumes it when paused,
`Pause()` and `Stop()` do what they say. A source with `playOnStart` plays once the component starts, and again each
time it is enabled; it stops when disabled or removed.

Spatial sounds are heard from the scene's `AudioListener`, or its main camera without one: the scene's
`AudioSystem` (`Scene::GetAudio()`) works out, once a frame after everything moved, how loud each is at its distance
and from which side, and gives the playing sounds their volume and pan. Within the min distance a sound plays at full
volume, past the max distance it is silent, and in between it falls as its rolloff says: `Inverse`, as sound does
in the open (half as loud at twice the distance), or `Linear`. Sources that are not spatial, such as music, are heard
as they are anywhere.

```cpp
auto& source = entity.AddComponent<Spot::AudioSource>();
source.sound = Spot::Assets::Load<Spot::Sound>("sounds/engine.ogg");
source.loop = true;
source.spatialSettings = {.minDistance = 2.0f, .maxDistance = 40.0f};

Spot::Audio::PlayOneShot(GetScene(), explosion.GetShared(), position, "Effects");
```

`Audio::PlayOneShot()` plays a sound once where something happened, heard like a spatial source. A scene's sounds,
one-shots included, pause while the application pauses the scenes (as the editor does), except those the game paused
itself stay paused when they resume, and stop when the scene unloads. Audio components play through
`Audio::GetDevice()`, the running application's device.

An `AudioDevice` made with `AudioConfig::manualMix` opens no output and plays as if heard, while its owner pulls the
mix with `Render()`, as tests and offline rendering do.

## Voices

At most 64 sounds play at once (`SetMaxVoices()`); `GetVoiceCount()` tells how many do. Past the limit, a new sound
takes the voice of the lowest `PlayParams::priority` playing, the oldest of them, and does not play when every sound
playing ranks above it. `PlayParams::maxInstances` caps the playbacks of one sound, the oldest giving its voice to the
newest, so sounds that many things make at once, such as impacts, never pile up.

Nothing clicks: stopping, pausing, resuming and stolen voices fade over a few milliseconds
(`Detail::Mixer::ClickFadeSeconds`), and volume and pan changes, of sounds and of buses, ramp over as long.
`Stop(instance, fadeOut)` fades out longer, as music ending does, and `PlayParams::fadeIn` fades a sound in; sounds
start where they were cut to start, so they need none by default. A stopped sound's instance stops matching at once,
even while it fades.

## Buses

Buses group sounds, such as music, effects and voices, so each group's volume and muting change at once. They form a
tree under the master bus: a sound plays into its bus (`PlayParams::bus`, the master bus by default), and each bus
scales and mutes what reaches it before passing it to its parent.

```cpp
const Spot::AudioBusId effects = *audio.AddBus("Effects");
const Spot::AudioBusId weapons = *audio.AddBus("Weapons", effects);
audio.Play(shot.GetShared(), {.bus = weapons});
audio.SetBusVolume(effects, 0.5f);
audio.SetBusMuted(weapons, true);
```

Soloing buses (`SetBusSolo()`) silences every sound but those on soloed buses or below them, to listen to some
alone. There are at most 32 buses, the master one included; `SetMasterVolume()` is the master bus's volume. Removing a bus
hands its sounds and its buses to its parent, and `SetBusParent()` refuses to make loops. `GetBusPeak()` tells the
loudest sample of each channel that left a bus in the last mix, for meters, without waiting for the audio thread.

## Audio settings and the mixer panel

A game's buses come from the audio section of its settings (`AudioSettings`, section "audio"): the master volume,
and each bus's name, parent (by name), volume and muting. The application gives them to its device when it starts
(`AudioSettings::Apply()`, which adds missing buses, in any order, and leaves others alone). New projects start with
Music, Effects, Ambience and Interface buses.

The editor's Audio Mixer panel (Window menu) shows a strip per bus: a fader (double-click for 0 dB), a peak meter,
mute and solo. A bus's menu renames it, moves it under another or removes it, and the last strip adds one. Changes
are heard at once, while a game plays in the editor too, and saved to the project; solo only lasts while the editor
runs.

## The mixer

The mixer (`engine/audio/mixer.h`) mixes every playing voice into its bus's buffer, converting sample rates as
sounds play, then passes the buses up the tree, each before its parent, in chunks of 256 frames. Buses live in fixed
slots, so mixing never allocates and the peak meters are atomics. The audio thread only mixes: it never touches
scenes or assets, and sounds stay alive while they play because the mixer holds them.

The audio thread takes no lock, so a game holding the mixer busy never makes it miss its deadline and crackle. The
game's calls keep their own record of the voices and buses, under a lock of theirs, and send what changes through a
fixed-size queue without locks (`engine/core/spsc_queue.h`), which `Mix()` applies before mixing, on voices and buses
of its own kept in room reserved up front (1,024 voices, fading ones included). Voices that end go back through
another queue, and the game's side lets their sounds go on its next call, so the audio thread never frees memory
either. What the game asks for is heard from the next mix on.

