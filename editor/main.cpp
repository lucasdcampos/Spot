#include "app/editor_application.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <utility>

// spot_editor [--settings <folder>] [--screenshot <file> [--frames <count>]] [project]
//   project               a project file, or a folder with one, to open instead of showing the hub
//   --settings <folder>   where to keep settings, layout and log instead of the user's data folder
//   --screenshot <file>   saves a PNG of the window once the UI has settled, then quits
//   --frames <count>      how many frames to wait before the screenshot, such as for the game's code to build
int main(int argc, char** argv) {
    Spot::Editor::EditorOptions options;
    for (int i = 1; i < argc; ++i) {
        // Arguments come in the encoding the OS uses for narrow paths, which this constructor expects.
        const std::string_view argument = argv[i];
        if (argument == "--settings" && i + 1 < argc) {
            options.settingsDirectory = std::filesystem::path(argv[++i]);
        } else if (argument == "--screenshot" && i + 1 < argc) {
            options.screenshot = std::filesystem::path(argv[++i]);
        } else if (argument == "--frames" && i + 1 < argc) {
            options.screenshotFrame = std::max(1, std::atoi(argv[++i]));
        } else {
            options.project = std::filesystem::path(argv[i]);
        }
    }
    options.showCrashMessage = options.screenshot.empty();
    Spot::Editor::EditorApplication editor(std::move(options));
    return editor.Run();
}
