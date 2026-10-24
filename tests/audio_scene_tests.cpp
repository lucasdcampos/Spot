#include "temp_directory.h"
#include <spot/scene/audio/audio_system.h>

#include <spot.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>

using Catch::Approx;
using Spot::Vector3;

namespace {

// A device mixed by hand, which audio components play through while it lives.
struct ManualAudio {
    Spot::AudioDevice device{{.manualMix = true}};
    Spot::AudioDevice* previous = Spot::Audio::GetDevice();

    ManualAudio() { Spot::Audio::SetDevice(&device); }
    ~ManualAudio() { Spot::Audio::SetDevice(previous); }

    // The last frame of enough mixing for fades and ramps to end.
    std::pair<float, float> Render() {
        std::vector<float> output(2000);
        device.Render(output);
        return {output[output.size() - 2], output[output.size() - 1]};
    }
};

std::shared_ptr<const Spot::Sound> MakeTone() {
    return std::make_shared<Spot::Sound>(48000, 1, std::vector<float>{0.5f, 0.5f});
}

Spot::Entity& AddListener(Spot::Scene& scene) {
    Spot::Entity& listener = scene.CreateEntity("Listener");
    listener.AddComponent<Spot::AudioListener>();
    return listener;
}

Spot::AudioSource& AddSource(Spot::Scene& scene, Vector3 position) {
    Spot::Entity& entity = scene.CreateEntity("Source");
    entity.GetTransform().SetPosition(position);
    auto& source = entity.AddComponent<Spot::AudioSource>();
    source.sound = MakeTone();
    source.loop = true;
    return source;
}

} // namespace

TEST_CASE("Spatial sounds fade with distance, as their rolloff says", "[audio][spatial]") {
    const Spot::AudioSpatialSettings inverse{.minDistance = 2.0f, .maxDistance = 20.0f};
    CHECK(inverse.GetGain(1.0f) == 1.0f);
    CHECK(inverse.GetGain(4.0f) == Approx(0.5f));
    CHECK(inverse.GetGain(8.0f) == Approx(0.25f));
    CHECK(inverse.GetGain(20.0f) == 0.0f);

    const Spot::AudioSpatialSettings linear{
        .minDistance = 2.0f, .maxDistance = 12.0f, .rolloff = Spot::AudioRolloff::Linear};
    CHECK(linear.GetGain(7.0f) == Approx(0.5f));
    CHECK(linear.GetGain(11.0f) == Approx(0.1f));
}

TEST_CASE("The listener hears sounds from their side", "[audio][spatial]") {
    Spot::Scene scene;
    Spot::AudioSystem& audio = scene.GetOrAddSystem<Spot::AudioSystem>();
    const Spot::AudioSpatialSettings settings;

    // Without a listener or a camera, sounds play as they are.
    CHECK(audio.Spatialize({5.0f, 0.0f, 0.0f}, settings) == std::pair(1.0f, 0.0f));

    Spot::Entity& listener = AddListener(scene);
    auto [gain, pan] = audio.Spatialize({5.0f, 0.0f, 0.0f}, settings);
    CHECK(gain == Approx(0.2f));
    CHECK(pan == Approx(1.0f));
    CHECK(audio.Spatialize({-3.0f, 0.0f, 0.0f}, settings).second == Approx(-1.0f));
    CHECK(audio.Spatialize({0.0f, 0.0f, -3.0f}, settings).second == Approx(0.0f).margin(1e-6f));

    // Turned around, its right is the world's left.
    listener.GetTransform().SetRotation(Spot::Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, Spot::Pi));
    CHECK(audio.Spatialize({5.0f, 0.0f, 0.0f}, settings).second == Approx(-1.0f));

    // Without a listener, the main camera hears.
    listener.Destroy();
    Spot::Entity& camera = scene.CreateEntity("Camera");
    camera.AddComponent<Spot::Camera>();
    camera.GetTransform().SetPosition({10.0f, 0.0f, 0.0f});
    std::tie(gain, pan) = audio.Spatialize({5.0f, 0.0f, 0.0f}, settings);
    CHECK(gain == Approx(0.2f));
    CHECK(pan == Approx(-1.0f));
}

TEST_CASE("Audio sources play from their entity, and follow it", "[audio][spatial]") {
    ManualAudio audio;
    Spot::Scene scene;
    AddListener(scene);
    Spot::AudioSource& source = AddSource(scene, {5.0f, 0.0f, 0.0f});

    scene.Update(0.016f);
    REQUIRE(source.IsPlaying());
    CHECK(source.GetSpatialGain() == Approx(0.2f));
    CHECK(source.GetSpatialPan() == Approx(1.0f));
    auto [left, right] = audio.Render();
    CHECK(left == Approx(0.0f).margin(1e-4f));
    CHECK(right == Approx(0.1f));

    // Moved in front of the listener, closer, it is heard louder and from the middle.
    source.GetEntity().GetTransform().SetPosition({0.0f, 0.0f, -2.0f});
    scene.Update(0.016f);
    CHECK(source.GetSpatialGain() == Approx(0.5f));
    std::tie(left, right) = audio.Render();
    CHECK(left == Approx(0.25f));
    CHECK(right == Approx(0.25f));

    // Sources that are not spatial are heard as they are, anywhere.
    source.spatial = false;
    source.GetEntity().GetTransform().SetPosition({100.0f, 0.0f, 0.0f});
    scene.Update(0.016f);
    std::tie(left, right) = audio.Render();
    CHECK(left == Approx(0.5f));
    CHECK(right == Approx(0.5f));
}

TEST_CASE("Audio sources play, pause and stop, and stop while disabled", "[audio]") {
    ManualAudio audio;
    Spot::Scene scene;
    Spot::AudioSource& source = AddSource(scene, {});
    source.spatial = false;
    source.playOnStart = false;

    scene.Update(0.016f);
    CHECK_FALSE(source.IsPlaying());
    source.Play();
    CHECK(source.IsPlaying());
    CHECK(audio.Render().first == Approx(0.5f));

    source.Pause();
    CHECK(source.IsPaused());
    CHECK(source.IsPlaying());
    CHECK(audio.Render().first == 0.0f);
    source.Play();
    CHECK_FALSE(source.IsPaused());
    CHECK(audio.Render().first == Approx(0.5f));

    source.Stop();
    CHECK_FALSE(source.IsPlaying());

    // Playing on start, it plays again each time it is enabled.
    source.playOnStart = true;
    source.SetEnabled(false);
    source.SetEnabled(true);
    CHECK(source.IsPlaying());
    source.SetEnabled(false);
    CHECK_FALSE(source.IsPlaying());
    CHECK(audio.device.GetVoiceCount() == 0);
}

TEST_CASE("Sources play on their bus, found by name", "[audio]") {
    ManualAudio audio;
    const Spot::AudioBusId music = *audio.device.AddBus("Music");
    Spot::Scene scene;
    Spot::AudioSource& source = AddSource(scene, {});
    source.spatial = false;
    source.bus = "Music";
    scene.Update(0.016f);

    audio.device.SetBusMuted(music, true);
    CHECK(audio.Render().first == 0.0f);
    audio.device.SetBusMuted(music, false);
    CHECK(audio.Render().first == Approx(0.5f));
    CHECK(Spot::Audio::FindBus("Missing") == Spot::AudioBusId::Master);
}

TEST_CASE("A scene's sounds pause with it and stop when it goes", "[audio]") {
    ManualAudio audio;
    {
        Spot::Scene scene;
        Spot::AudioSource& music = AddSource(scene, {});
        music.spatial = false;
        Spot::AudioSource& paused = AddSource(scene, {});
        paused.spatial = false;
        scene.Update(0.016f);
        paused.Pause();
        const auto shot = scene.GetOrAddSystem<Spot::AudioSystem>().PlayOneShot(
            std::make_shared<Spot::Sound>(48000, 1, std::vector<float>(48000, 0.5f)), {}, {.volume = 0.0f});
        CHECK(audio.device.IsPlaying(shot));
        CHECK(audio.device.GetVoiceCount() == 3);

        scene.GetOrAddSystem<Spot::AudioSystem>().SetPaused(true);
        CHECK(audio.Render().first == 0.0f);
        CHECK(music.IsPlaying());
        CHECK(audio.device.IsPaused(shot));

        // What the game paused stays paused.
        scene.GetOrAddSystem<Spot::AudioSystem>().SetPaused(false);
        CHECK(audio.Render().first == Approx(0.5f));
        CHECK(paused.IsPaused());
        CHECK_FALSE(audio.device.IsPaused(shot));
    }
    CHECK(audio.device.GetVoiceCount() == 0);
}

TEST_CASE("One-shots play where they happen, and end on their own", "[audio][spatial]") {
    ManualAudio audio;
    Spot::Scene scene;
    AddListener(scene);
    const auto shot = Spot::Audio::PlayOneShot(
        scene, std::make_shared<Spot::Sound>(48000, 1, std::vector<float>(100, 0.5f)), {-4.0f, 0.0f, 0.0f});
    CHECK(audio.device.IsPlaying(shot));
    std::vector<float> output(20);
    audio.device.Render(output);
    // A quarter of the volume, on the left.
    CHECK(output[0] == Approx(0.125f));
    CHECK(output[1] == Approx(0.0f).margin(1e-4f));
    audio.Render();
    CHECK_FALSE(audio.device.IsPlaying(shot));
    scene.Update(0.016f);
}

namespace {

class PausingAudioApplication final : public Spot::Application {
public:
    using Application::Application;

    bool usedItsDevice = false;
    bool audioPaused = false;

protected:
    void OnStart() override { LoadScene<Spot::Scene>(); }

    void OnUpdate(float /*deltaTime*/) override {
        // The audio module pauses the scene's sounds after the frame's update.
        if (!AreScenesPaused()) {
            usedItsDevice = Spot::Audio::GetDevice() == &GetModule<Spot::AudioModule>()->GetDevice();
            GetScene()->GetOrAddSystem<Spot::AudioSystem>();
            SetScenesPaused(true);
            return;
        }
        audioPaused = GetScene()->GetOrAddSystem<Spot::AudioSystem>().IsPaused();
        Quit();
    }
};

} // namespace

TEST_CASE("Applications play through their device, and pause their scenes' sounds with them", "[audio]") {
    const TempDirectory temp;
    PausingAudioApplication application({.window = {.width = 320, .height = 240},
                                         .graphics = {.enabled = false},
                                         .logFile = temp.GetPath() / "game.log"});
    application.AddModule<Spot::AudioModule>(Spot::AudioConfig{.manualMix = true});
    CHECK(application.Run() == EXIT_SUCCESS);
    CHECK(application.usedItsDevice);
    CHECK(application.audioPaused);
    CHECK(Spot::Audio::GetDevice() == nullptr);
}
