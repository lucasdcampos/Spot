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

## The mixer

The mixer (`engine/audio/mixer.h`) mixes every playing voice into the output's buffer, converting sample rates as
sounds play. The audio thread only mixes: it never touches scenes or assets, and sounds stay alive while they play
because the mixer holds them.

## Not there yet

Buses, voice management, streaming, 3D sources with a listener and a mixer panel come in the roadmap's milestone M3.
