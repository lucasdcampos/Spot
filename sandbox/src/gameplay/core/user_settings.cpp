#include "gameplay/core/user_settings.h"

#include "gameplay/core/player_controller.h"
#include "gameplay/core/players.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace Gameplay {

namespace {

float Clamp(float value, float low, float high, float fallback) {
    return std::clamp(std::isfinite(value) ? value : fallback, low, high);
}

// Visits controllers, their pawns and held items, including the occupant while driving.
void ForEachListener(Spot::Scene& scene, const std::function<void(UserSettingsListener&)>& function) {
    const auto visit = [&](Spot::Entity* entity) {
        if (!entity) {
            return;
        }
        entity->ForEachComponent<Spot::Component>([&](Spot::Component& component) {
            if (auto* listener = dynamic_cast<UserSettingsListener*>(&component)) {
                function(*listener);
            }
        });
    };
    const auto visitPawn = [&](auto&& self, Spot::Entity& entity, const Pawn& pawn) -> void {
        if (Pawn::Of(entity) != &pawn) {
            return;
        }
        visit(&entity);
        entity.ForEachChild([&](Spot::Entity& child) { self(self, child, pawn); });
    };
    for (PlayerController* player : Players::GetAll(scene)) {
        visit(&player->GetEntity());
        auto* entity = scene.GetEntity(player->pawn);
        if (auto* pawn = entity ? entity->GetComponent<Pawn>() : nullptr) {
            visitPawn(visitPawn, *entity, *pawn);
            if (auto* occupant = pawn->GetOccupant()) {
                visitPawn(visitPawn, occupant->GetEntity(), *occupant);
            }
        }
    }
}

} // namespace

void UserSettings::Serialize(Spot::Archive& archive) {
    archive.Field("playerName", playerName);
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

void UserSettings::Validate() {
    if (playerName.empty() || playerName.size() > 64 ||
        std::ranges::any_of(playerName, [](unsigned char c) { return c < 32 || c == 127; })) {
        playerName = "Player";
    }
    sensitivity = Clamp(sensitivity, 0.0002f, 0.02f, 0.0025f);
    fieldOfView = Clamp(fieldOfView, 45.0f, 120.0f, 60.0f);
    masterVolume = Clamp(masterVolume, 0.0f, 1.0f, 1.0f);
    effectsVolume = Clamp(effectsVolume, 0.0f, 1.0f, 1.0f);
    musicVolume = Clamp(musicVolume, 0.0f, 1.0f, 0.7f);
    ambienceVolume = Clamp(ambienceVolume, 0.0f, 1.0f, 0.8f);
    interfaceVolume = Clamp(interfaceVolume, 0.0f, 1.0f, 1.0f);
    graphicsQuality = std::clamp(graphicsQuality, 0, 2);
}

Spot::SaveDataResult UserSettings::Save(const std::filesystem::path& directory) const {
    auto copy = *this;
    copy.Validate();
    return Spot::SaveData(directory).Save(
        "interface.json", 1, [&](Spot::Archive& archive, std::uint32_t) { archive.Field("interface", copy); });
}

Spot::SaveDataResult UserSettings::Load(const std::filesystem::path& directory) {
    auto copy = *this;
    auto result = Spot::SaveData(directory).Load(
        "interface.json", 1, [&](Spot::Archive& archive, std::uint32_t) { archive.Field("interface", copy); });
    if (result) {
        copy.Validate();
        *this = std::move(copy);
    }
    return result;
}

void UserSettings::Read(Spot::Scene& scene) {
    ForEachListener(scene, [this](UserSettingsListener& listener) { listener.ReadSettings(*this); });
    if (auto* audio = Spot::Audio::GetDevice()) {
        masterVolume = audio->GetMasterVolume();
        const auto volume = [&](std::string_view name, float fallback) {
            const auto bus = audio->FindBus(name);
            return bus ? audio->GetBusVolume(*bus) : fallback;
        };
        effectsVolume = volume("Effects", effectsVolume);
        musicVolume = volume("Music", musicVolume);
        ambienceVolume = volume("Ambience", ambienceVolume);
        interfaceVolume = volume("Interface", interfaceVolume);
    }
}

void UserSettings::Apply(Spot::Scene& scene) const {
    auto settings = *this;
    settings.Validate();
    ForEachListener(scene, [&](UserSettingsListener& listener) { listener.ApplySettings(settings); });
    if (auto* audio = Spot::Audio::GetDevice()) {
        audio->SetMasterVolume(settings.masterVolume);
        const auto volume = [&](std::string_view name, float value) {
            if (const auto bus = audio->FindBus(name)) {
                audio->SetBusVolume(*bus, value);
            }
        };
        volume("Effects", settings.effectsVolume);
        volume("Music", settings.musicVolume);
        volume("Ambience", settings.ambienceVolume);
        volume("Interface", settings.interfaceVolume);
    }
    if (scene.IsLoaded()) {
        const int quality = settings.graphicsQuality;
        const auto previous = Spot::Application::Get().GetRenderingSettings();
        auto rendering = previous;
        rendering.msaaSamples = quality == 0 ? 1u : quality == 1 ? 2u : 4u;
        rendering.shadowMapSize = quality == 0 ? 0u : quality == 1 ? 1024u : 2048u;
        rendering.shadowCascadeCount = quality == 0 ? 1u : quality == 1 ? 2u : 4u;
        rendering.localShadowMapSize = quality == 0 ? 0u : quality == 1 ? 256u : 512u;
        rendering.fxaa = quality < 2;
        if (rendering.msaaSamples != previous.msaaSamples || rendering.shadowMapSize != previous.shadowMapSize ||
            rendering.shadowCascadeCount != previous.shadowCascadeCount ||
            rendering.localShadowMapSize != previous.localShadowMapSize || rendering.fxaa != previous.fxaa) {
            Spot::Application::Get().SetRenderingSettings(rendering);
        }
    }
}

} // namespace Gameplay
