#pragma once

#include <TextEditor.h>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Spot::Editor {

struct EditorScriptLanguage {
    std::string_view id;
    std::string_view displayName;
    std::string_view menuLabel;
    std::string_view defaultFileName;
    std::string_view extension;
    std::string_view badge;
    ImVec4 color{};
    std::string (*makeTemplate)(std::string_view fileName) = nullptr;
    void (*configureEditor)(TextEditor& editor) = nullptr;
};

std::span<const EditorScriptLanguage> GetEditorScriptLanguages();
const EditorScriptLanguage* FindEditorScriptLanguage(std::string_view path);

// Editor-only language definitions: highlighting must also accept unfinished or invalid source.
void ConfigureViceEditor(TextEditor& editor);
void ConfigureLuaEditor(TextEditor& editor);
void ConfigureScriptEditor(TextEditor& editor, std::string_view path);
// Unlike TextEditor::GetText(), this preserves whether the last line has a newline.
std::string GetEditorText(const TextEditor& editor);

} // namespace Spot::Editor
