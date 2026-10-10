#include "spawnable.h"

#include <array>
#include <string_view>

SPOT_COMPONENT(Spawnable);

void Spawnable::Serialize(Spot::Archive& archive) {
    static constexpr std::array<std::string_view, 3> Categories = {"Prop", "Lamp", "Weapon"};
    archive.Field("category", category, {.enumNames = Categories});
    archive.Field("order", order, {.tooltip = "Earlier entries appear first in the spawn list"});
    archive.Field("clearance", clearance, {.min = 0.0f, .max = 100.0f, .speed = 0.01f});
}
