#pragma once

#include <spot.h>

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Widgets of the inspector's property rows.
namespace Spot::Editor {

// What fields show when the selected entities' values differ.
inline constexpr const char* MixedValueText = "\xE2\x80\x94";

// Colors are linear in the engine and sRGB-encoded in color pickers, the way people pick them.
float LinearToSrgb(float value);
float SrgbToLinear(float value);

// Floats side by side, filling the item width, each marked with its axis's color: X red, Y green, Z blue. Axes whose
// bit is set in mixedMask show MixedValueText. Returns the index of the axis that changed.
std::optional<int> DragAxes(const char* id, float* values, int count, float speed, float min = 0.0f, float max = 0.0f,
                            const char* format = "%.3f", unsigned mixedMask = 0);

// Remembers the Euler angles shown for rotations, so that editing one angle does not turn the others into those of
// an equivalent rotation, as converting the quaternion back each frame would.
class EulerCache {
public:
    // The pitch, yaw and roll to show for the rotation, in degrees.
    Vector3 Get(std::uint64_t key, const Quaternion& rotation) const;
    // Remembers the angles the rotation was made from.
    void Set(std::uint64_t key, Vector3 degrees, const Quaternion& rotation);

private:
    struct Entry {
        Quaternion rotation;
        Vector3 degrees;
    };
    std::unordered_map<std::uint64_t, Entry> m_entries;
};

// An item of a picker's list.
struct PickerItem {
    std::string label;
    const char* icon = nullptr;
    // Shown muted beside the label, such as an asset's folder.
    std::string detail = {};
};

struct PickerResult {
    // The index of the item picked from the list, or -1 for "None".
    std::optional<int> picked;
    // A drag of the payload type dropped on the field.
    const ImGuiPayload* dropped = nullptr;
};

// A field that shows what it refers to, opens a searchable list to pick something else, and takes drops of the
// payload type. The items are only listed while the list is open.
PickerResult PickerField(const char* icon, std::string_view current, const char* payloadType,
                         const std::function<std::vector<PickerItem>()>& listItems, bool mixed = false,
                         const ImVec4* currentColor = nullptr, const char* tooltip = nullptr);

} // namespace Spot::Editor
