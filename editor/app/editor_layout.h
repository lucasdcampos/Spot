#pragma once

#include <imgui.h>

#include <string>
#include <vector>

namespace Spot::Editor {

// Window ids of the default layout's regions, as Panel ids.
struct LayoutWindows {
    std::vector<std::string> scenes;
    std::string hierarchy;
    std::string inspector;
    std::string assets;
    std::string console;
    // Docked with the console, for when it opens.
    std::string mixer = {};
    std::string profiler = {};
    std::string code = {};
};

// Splits the dock space into the default layout: scenes in the middle, the hierarchy above the inspector on the
// right, and the assets beside the console below the scenes. Returns the node scenes are docked into.
ImGuiID BuildDefaultLayout(ImGuiID dockSpace, const LayoutWindows& windows);

} // namespace Spot::Editor
