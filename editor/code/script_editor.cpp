#include "code/script_editor.h"
#include "scene/scripting/script_api.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>

namespace Spot::Editor {

namespace {

constexpr std::array LuauKeywords{
    std::string_view("and"),    std::string_view("break"),    std::string_view("continue"), std::string_view("do"),
    std::string_view("else"),   std::string_view("elseif"),   std::string_view("end"),      std::string_view("false"),
    std::string_view("for"),    std::string_view("function"), std::string_view("if"),       std::string_view("in"),
    std::string_view("local"),  std::string_view("nil"),      std::string_view("not"),      std::string_view("or"),
    std::string_view("repeat"), std::string_view("return"),   std::string_view("then"),     std::string_view("true"),
    std::string_view("until"),  std::string_view("while"),
};

constexpr std::array LuauBuiltins{
    std::string_view("print"),    std::string_view("require"),  std::string_view("type"),
    std::string_view("tostring"), std::string_view("tonumber"), std::string_view("pairs"),
    std::string_view("ipairs"),   std::string_view("next"),     std::string_view("select"),
    std::string_view("pcall"),    std::string_view("xpcall"),   std::string_view("error"),
    std::string_view("assert"),   std::string_view("math"),     std::string_view("string"),
    std::string_view("table"),    std::string_view("utf8"),     std::string_view("coroutine"),
    std::string_view("typeof"),   std::string_view("bit32"),    std::string_view("buffer"),
    std::string_view("vector"),
};

std::string MakeLuauScriptTemplate(std::string_view name) {
    return "-- " + std::string(name) +
           "\n\nlocal spot = require(\"@spot\")\n\nfunction OnStart()\n    \nend\n\nfunction OnUpdate(deltaTime)\n    "
           "\nend\n";
}

void AttachScriptTokenizer(TextEditor::Language& language) {
    for (const auto constructor : ScriptApi::ValueConstructors)
        language.declarations.emplace(constructor);
    language.identifiers.emplace(ScriptApi::Module);
    for (const auto& hook : ScriptApi::Hooks)
        language.identifiers.emplace(hook.name);
    const auto keywords = language.keywords;
    const auto identifiers = language.identifiers;
    const auto declarations = language.declarations;
    const auto getIdentifier = language.getIdentifier;
    language.customTokenizer = [keywords, identifiers, declarations, getIdentifier](
                                   TextEditor::Iterator start, TextEditor::Iterator end, TextEditor::Color& color) {
        const auto finish = getIdentifier(start, end);
        if (finish == start)
            return start;
        std::string name;
        for (auto cursor = start; cursor != finish; ++cursor)
            name += static_cast<char>(*cursor);
        auto next = finish;
        while (next != end && (*next == ' ' || *next == '\t'))
            ++next;
        color = TextEditor::Color::identifier;
        if (keywords.contains(name))
            color = TextEditor::Color::keyword;
        else if (identifiers.contains(name))
            color = TextEditor::Color::knownIdentifier;
        else if (declarations.contains(name))
            color = TextEditor::Color::declaration;
        else if (next != end && *next == '(')
            color = TextEditor::Color::knownIdentifier;
        else if (name.front() >= 'A' && name.front() <= 'Z')
            color = TextEditor::Color::declaration;
        return finish;
    };
}

void ApplyScriptEditorStyle(TextEditor& editor, const TextEditor::Language& language) {
    editor.SetLanguage(&language);
    editor.SetTabSize(4);
    editor.SetInsertSpacesOnTabs(true);
    editor.SetAutoIndentEnabled(true);
    editor.SetCompletePairedGlyphs(false);
    editor.SetShowMatchingBrackets(false);
    editor.SetFindReplaceEnabled(false);
    editor.SetShowWhitespacesEnabled(false);
    editor.SetShowMiniMapEnabled(false);
    editor.SetShowScrollbarMiniMapEnabled(false);
    auto palette = TextEditor::GetDarkPalette();
    const auto set = [&](TextEditor::Color color, ImU32 value) { palette[static_cast<std::size_t>(color)] = value; };
    set(TextEditor::Color::background, IM_COL32(24, 25, 28, 255));
    set(TextEditor::Color::text, IM_COL32(220, 222, 227, 255));
    set(TextEditor::Color::identifier, IM_COL32(220, 222, 227, 255));
    set(TextEditor::Color::keyword, IM_COL32(192, 144, 232, 255));
    set(TextEditor::Color::knownIdentifier, IM_COL32(110, 190, 235, 255));
    set(TextEditor::Color::declaration, IM_COL32(86, 190, 180, 255));
    set(TextEditor::Color::punctuation, IM_COL32(155, 160, 172, 255));
    set(TextEditor::Color::string, IM_COL32(217, 166, 115, 255));
    set(TextEditor::Color::number, IM_COL32(155, 205, 155, 255));
    set(TextEditor::Color::comment, IM_COL32(110, 153, 105, 255));
    editor.SetPalette(palette);
}

constexpr std::array Languages{
    EditorScriptLanguage{
        .id = "luau",
        .displayName = "Luau",
        .menuLabel = "Luau Script",
        .defaultFileName = "New Script.luau",
        .extension = ".luau",
        .badge = "LUAU",
        .color = {68.0f / 255.0f, 138.0f / 255.0f, 228.0f / 255.0f, 1.0f},
        .makeTemplate = &MakeLuauScriptTemplate,
        .configureEditor = &ConfigureLuauEditor,
    },
};

} // namespace

std::span<const EditorScriptLanguage> GetEditorScriptLanguages() {
    return Languages;
}

const EditorScriptLanguage* FindEditorScriptLanguage(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\");
    const std::string_view fileName = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const std::size_t dot = fileName.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0)
        return nullptr;
    std::string extension(fileName.substr(dot));
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    for (const EditorScriptLanguage& language : Languages) {
        if (language.extension == extension)
            return &language;
    }
    return nullptr;
}

void ConfigureLuauEditor(TextEditor& editor) {
    static const TextEditor::Language language = [] {
        TextEditor::Language result = *TextEditor::Language::Lua();
        result.name = "Luau";
        // Interpolated strings: `Hello {name}`.
        result.otherStringStart = "`";
        result.otherStringEnd = "`";
        result.keywords.clear();
        result.declarations.clear();
        result.identifiers.clear();
        for (const auto keyword : LuauKeywords)
            result.keywords.emplace(keyword);
        for (const auto builtin : LuauBuiltins)
            result.identifiers.emplace(builtin);
        AttachScriptTokenizer(result);
        return result;
    }();
    ApplyScriptEditorStyle(editor, language);
}

void ConfigureScriptEditor(TextEditor& editor, std::string_view path) {
    if (const EditorScriptLanguage* language = FindEditorScriptLanguage(path); language && language->configureEditor) {
        language->configureEditor(editor);
        return;
    }
    ConfigureLuauEditor(editor);
}

std::string GetEditorText(const TextEditor& editor) {
    return editor.GetSectionText({0, 0}, {editor.GetLineCount() - 1, std::numeric_limits<std::size_t>::max()});
}

} // namespace Spot::Editor
