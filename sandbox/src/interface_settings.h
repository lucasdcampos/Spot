#pragma once

#include <spot.h>

#include <filesystem>
#include <string>

struct InterfaceSettings {
    float sensitivity = 0.0025f;
    float fieldOfView = 60;
    float masterVolume = 1;
    float effectsVolume = 1;
    float musicVolume = 0.7f;
    float ambienceVolume = 0.8f;
    float interfaceVolume = 1;
    int graphicsQuality = 2;
    std::string rebinds;

    void Serialize(Spot::Archive& archive);
    void Validate();
    Spot::SaveDataResult Save(const std::filesystem::path& directory) const;
    Spot::SaveDataResult Load(const std::filesystem::path& directory);
    void Apply(Spot::Scene& scene) const;
};
