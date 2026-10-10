# Audio

Audio is mixed by the engine and only delivered by the backend: `engine/audio/` has the engine's mixer, and the SDL3
backend (`AudioOutput`) only pulls mixed samples from it on its own thread.

## Sounds

A `Sound` (`engine/audio/sound.h`) is decoded audio in CPU memory: 32-bit float samples, mono or stereo. Sounds never
change, so one sound can play many times at once. They load as assets (`.wav` and `.ogg` sources) or from raw files.

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

There are at most 32 buses, the master one included; `SetMasterVolume()` is the master bus's volume. Removing a bus
hands its sounds and its buses to its parent, and `SetBusParent()` refuses to make loops. `GetBusPeak()` tells the
loudest sample of each channel that left a bus in the last mix, for meters, without waiting for the audio thread.

## The mixer

The mixer (`engine/audio/mixer.h`) mixes every playing voice into its bus's buffer, converting sample rates as
sounds play, then passes the buses up the tree, each before its parent, in chunks of 256 frames. Buses live in fixed
slots, so mixing never allocates and the peak meters are atomics. The audio thread only mixes: it never touches scenes or assets, and sounds stay alive while they play
because the mixer holds them.

## Not there yet

Voice management, streaming, 3D sources with a listener and a mixer panel come in the rest of the roadmap's
milestone M3.
