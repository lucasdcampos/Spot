#include "interface_settings.h"

#include "player_controller.h"

#include <algorithm>
#include <cmath>

namespace {
float Clamp(float value, float low, float high, float fallback) {
    return std::clamp(std::isfinite(value) ? value : fallback, low, high);
}
} // namespace

void InterfaceSettings::Serialize(Spot::Archive& archive) {
    archive.Field("sensitivity", sensitivity);
    archive.Field("fieldOfView", fieldOfView);
    archive.Field("masterVolume", masterVolume);
    archive.Field("effectsVolume", effectsVolume);
    archive.Field("musicVolume", musicVolume);
    archive.Field("ambienceVolume", ambienceVolume);
    archive.Field("interfaceVolume", interfaceVolume);
    archive.Field("graphicsQuality", graphicsQuality);
    archive.Field("rebinds", rebinds);
}
void InterfaceSettings::Validate() {
    sensitivity = Clamp(sensitivity, 0.0002f, 0.02f, 0.0025f);
    fieldOfView = Clamp(fieldOfView, 45, 120, 60);
    masterVolume = Clamp(masterVolume, 0, 1, 1);
    effectsVolume = Clamp(effectsVolume, 0, 1, 1);
    musicVolume = Clamp(musicVolume, 0, 1, 0.7f);
    ambienceVolume = Clamp(ambienceVolume, 0, 1, 0.8f);
    interfaceVolume = Clamp(interfaceVolume, 0, 1, 1);
    graphicsQuality = std::clamp(graphicsQuality, 0, 2);
}
Spot::SaveDataResult InterfaceSettings::Save(const std::filesystem::path& directory) const {
    auto copy = *this;
    copy.Validate();
    return Spot::SaveData(directory).Save(
        "interface.json", 1, [&](Spot::Archive& archive, std::uint32_t) { archive.Field("interface", copy); });
}
Spot::SaveDataResult InterfaceSettings::Load(const std::filesystem::path& directory) {
    auto copy = *this;
    auto result = Spot::SaveData(directory).Load(
        "interface.json", 1, [&](Spot::Archive& archive, std::uint32_t) { archive.Field("interface", copy); });
    if (result) {
        copy.Validate();
        *this = std::move(copy);
    }
    return result;
}
void InterfaceSettings::Apply(Spot::Scene& scene) const {
    auto settings = *this;
    settings.Validate();
    if (auto* player = scene.FindComponent<PlayerController>()) {
        player->lookSensitivity = settings.sensitivity;
        player->SetFieldOfView(Spot::ToRadians(settings.fieldOfView));
    }
    if (auto* audio = Spot::Audio::GetDevice()) {
        audio->SetMasterVolume(settings.masterVolume);
        const auto volume = [&](std::string_view name, float value) {
            if (const auto bus = audio->FindBus(name))
                audio->SetBusVolume(*bus, value);
        };
        volume("Effects", settings.effectsVolume);
        volume("Music", settings.musicVolume);
        volume("Ambience", settings.ambienceVolume);
        volume("Interface", settings.interfaceVolume);
    }
    if (scene.IsLoaded()) {
        const auto previous = Spot::Application::Get().GetRenderingSettings();
        auto rendering = previous;
        rendering.msaaSamples = settings.graphicsQuality == 0 ? 1u : settings.graphicsQuality == 1 ? 2u : 4u;
        rendering.shadowMapSize = settings.graphicsQuality == 0 ? 0u : settings.graphicsQuality == 1 ? 1024u : 2048u;
        rendering.shadowCascadeCount = settings.graphicsQuality == 0 ? 1u : settings.graphicsQuality == 1 ? 2u : 4u;
        rendering.localShadowMapSize = settings.graphicsQuality == 0 ? 0u : settings.graphicsQuality == 1 ? 256u : 512u;
        rendering.fxaa = settings.graphicsQuality < 2;
        if (rendering.msaaSamples != previous.msaaSamples || rendering.shadowMapSize != previous.shadowMapSize ||
            rendering.shadowCascadeCount != previous.shadowCascadeCount ||
            rendering.localShadowMapSize != previous.localShadowMapSize || rendering.fxaa != previous.fxaa)
            Spot::Application::Get().SetRenderingSettings(rendering);
    }
}
