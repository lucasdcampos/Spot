#pragma once

#include "scene/scene_commands.h"

#include <optional>
#include <string>
#include <string_view>

// Components and transforms copied to the OS clipboard as JSON, so they can be pasted in any scene, even in another
// instance of the editor.
namespace Spot::Editor {

struct CopiedComponent {
    std::string typeName;
    // In the format of SaveComponentToString().
    std::string text;
};

// The clipboard text for a component saved with SaveComponentToString().
std::string MakeComponentClipboardText(std::string_view component);
// Nothing when the clipboard text is not a copied component.
std::optional<CopiedComponent> ReadComponentClipboardText(std::string_view text);

std::string MakeTransformClipboardText(const Pose& pose);
// Nothing when the clipboard text is not a copied transform.
std::optional<Pose> ReadTransformClipboardText(std::string_view text);

} // namespace Spot::Editor
