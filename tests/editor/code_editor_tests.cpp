#include "headless_imgui.h"
#include "temp_directory.h"

#include "app/editor_context.h"
#include "code/vice_completion.h"
#include "code/vice_editor.h"
#include "panels/code_editor_panel.h"

#include <spot.h>
#include <spot_tools.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct CodeFixture {
    TempDirectory temp;
    std::unique_ptr<Spot::Window> window = Spot::Window::Create();
    Spot::Tests::HeadlessImGui imgui{{1000, 720}};
    Spot::Editor::EditorSettings settings;
    Spot::Editor::EditorContext context{*window, nullptr, settings};
    Spot::Editor::CodeEditorPanel panel{context};
    std::filesystem::path assets;
    std::string clipboard;

    CodeFixture() {
        REQUIRE(window);
        const auto file = Spot::CreateProject(temp.GetPath(), "Scripts", Spot::ProjectTemplate::Empty);
        REQUIRE(file);
        context.SetProject(Spot::Editor::EditorProject::Open(*file, nullptr));
        REQUIRE(context.GetProject());
        context.SetCodeEditor(&panel);
        ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
        platform.Platform_ClipboardUserData = this;
        platform.Platform_GetClipboardTextFn = [](ImGuiContext*) {
            return static_cast<CodeFixture*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->clipboard.c_str();
        };
        platform.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
            static_cast<CodeFixture*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->clipboard = text;
        };
        assets = context.GetProject()->GetProject().GetAssetsDirectory();
        imgui.SetDraw([this] {
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({1000, 720});
            panel.Draw();
            panel.HandleShortcuts();
        });
    }

    void Write(const std::string& path, const std::string& source) {
        std::filesystem::create_directories((assets / path).parent_path());
        std::ofstream stream(assets / path, std::ios::binary);
        stream << source;
        stream.close();
        REQUIRE(stream);
        context.GetProject()->Refresh();
    }

    std::string Read(const std::string& path) {
        std::ifstream stream(assets / path, std::ios::binary);
        REQUIRE(stream);
        return {(std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>()};
    }

    Spot::AssetId Open(const std::string& path, const std::string& source) {
        Write(path, source);
        const auto asset = context.GetProject()->GetAssetDatabase().FindByPath(path);
        REQUIRE(asset);
        REQUIRE(panel.OpenScript(*asset));
        imgui.Frame();
        imgui.Frame();
        imgui.Frame();
        return *asset;
    }

    TextEditor& Editor() {
        REQUIRE(panel.GetActiveEditor());
        return *panel.GetActiveEditor();
    }

    std::string Text() { return Spot::Editor::GetEditorText(Editor()); }

    void Replace(const char* text) {
        Editor().SetFocus();
        imgui.Frame();
        imgui.Frame();
        imgui.Press(ImGuiMod_Ctrl | ImGuiKey_A);
        imgui.Type(text);
    }

    void Click(const char* name) {
        const auto rect = panel.GetItemRect(name);
        REQUIRE(rect);
        imgui.Click({(rect->first.x + rect->second.x) * 0.5f, (rect->first.y + rect->second.y) * 0.5f});
    }
};

} // namespace

TEST_CASE("Ctrl wheel zooms only the hovered code text", "[editor][code][vice][ui]") {
    CodeFixture fixture;
    fixture.Open("hello.vice", "// hello\nfunc OnStart() {}\n");
    const auto source = fixture.panel.GetItemRect("Source");
    REQUIRE(source);
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 point{source->first.x + 100, source->first.y + 40};
    const float initialWidth = fixture.Editor().GetGlyphWidth();
    const float uiSize = ImGui::GetStyle().FontSizeBase;
    const auto wheel = [&](ImVec2 position, float delta, bool control) {
        io.AddMousePosEvent(position.x, position.y);
        io.AddKeyEvent(ImGuiMod_Ctrl, control);
        fixture.imgui.Frame();
        io.AddMouseWheelEvent(0, delta);
        fixture.imgui.Frame();
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        fixture.imgui.Frame();
    };
    wheel(point, 4, false);
    CHECK(fixture.Editor().GetGlyphWidth() == initialWidth);
    wheel({950, 700}, 4, true);
    CHECK(fixture.Editor().GetGlyphWidth() == initialWidth);
    wheel(point, 4, true);
    const float largerWidth = fixture.Editor().GetGlyphWidth();
    CHECK(largerWidth > initialWidth);
    wheel(point, -4, true);
    CHECK(fixture.Editor().GetGlyphWidth() == initialWidth);
    wheel(point, 100, true);
    const float maximumWidth = fixture.Editor().GetGlyphWidth();
    wheel(point, 100, true);
    CHECK(fixture.Editor().GetGlyphWidth() == maximumWidth);
    wheel(point, -100, true);
    const float minimumWidth = fixture.Editor().GetGlyphWidth();
    CHECK(minimumWidth < initialWidth);
    wheel(point, -100, true);
    CHECK(fixture.Editor().GetGlyphWidth() == minimumWidth);
    CHECK(ImGui::GetStyle().FontSizeBase == uiSize);
    CHECK_FALSE(fixture.panel.HasUnsavedChanges());
}

TEST_CASE("The code editor edits ViceScript with undo redo clipboard and save shortcuts", "[editor][code][vice][ui]") {
    CodeFixture fixture;
    const std::string original = "// hello.vice\nfunc OnStart() {}\n";
    const auto asset = fixture.Open("scripts/hello.vice", original);
    CHECK(fixture.panel.IsFocused());
    CHECK_FALSE(fixture.panel.HasUnsavedChanges());
    fixture.Replace("// edited\nfunc OnStart() { print(\"ola\", null) }\n");
    const std::string edited = fixture.Text();
    CHECK(edited == "// edited\nfunc OnStart() { print(\"ola\", null) }\n");
    CHECK(fixture.panel.HasUnsavedChanges());
    int undone = 0;
    while (fixture.Editor().CanUndo() && undone < 64) {
        fixture.imgui.Press(ImGuiMod_Ctrl | ImGuiKey_Z);
        ++undone;
    }
    CHECK(fixture.Text() == original);
    CHECK_FALSE(fixture.panel.HasUnsavedChanges());
    for (int action = 0; action < undone; ++action) {
        fixture.imgui.Press(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z);
    }
    CHECK(fixture.Text() == edited);
    fixture.imgui.Press(ImGuiMod_Ctrl | ImGuiKey_A);
    fixture.imgui.Press(ImGuiMod_Ctrl | ImGuiKey_C);
    fixture.imgui.Press(ImGuiMod_Ctrl | ImGuiKey_X);
    CHECK(fixture.Text().empty());
    fixture.imgui.Press(ImGuiMod_Ctrl | ImGuiKey_V);
    CHECK(fixture.Text() == edited);
    fixture.imgui.Press(ImGuiMod_Ctrl | ImGuiKey_S);
    CHECK(fixture.Read("scripts/hello.vice") == edited);
    CHECK_FALSE(fixture.panel.HasUnsavedChanges());
    const auto loaded = fixture.context.GetProject()->GetAssets().Load<Spot::ScriptSource>(asset);
    REQUIRE(loaded);
    CHECK(loaded->source == edited);
    fixture.imgui.Press(ImGuiMod_Ctrl | ImGuiKey_W);
    CHECK(fixture.panel.GetDocumentCount() == 0);
}

TEST_CASE("ViceScript editing preserves UTF8 line endings tabs and the final newline", "[editor][code][vice]") {
    CodeFixture fixture;
    const std::string source = "\xef\xbb\xbf// a\r\nfunc OnStart() {\r\n\tprint(\"ol\xc3\xa1 \xf0\x9f\x8c\x8d\")\r\n}";
    fixture.Open("hello.vice", source);
    CHECK(fixture.Text() == "// a\nfunc OnStart() {\n\tprint(\"ol\xc3\xa1 \xf0\x9f\x8c\x8d\")\n}");
    REQUIRE(fixture.panel.SaveActive());
    CHECK(fixture.Read("hello.vice") == source);
    fixture.Editor().ReplaceSectionText({0, 3}, {0, 4}, "b");
    CHECK(fixture.panel.HasUnsavedChanges());
    REQUIRE(fixture.panel.SaveActive());
    CHECK(fixture.Read("hello.vice") ==
          "\xef\xbb\xbf// b\r\nfunc OnStart() {\r\n\tprint(\"ol\xc3\xa1 \xf0\x9f\x8c\x8d\")\r\n}");
}

TEST_CASE("ViceScript editing supports indentation and unfinished syntax", "[editor][code][vice][ui]") {
    CodeFixture fixture;
    fixture.Open("hello.vice", "    print(\"unfinished");
    fixture.Editor().SetCursor({0, 4});
    fixture.imgui.Frame();
    fixture.imgui.Press(ImGuiKey_Enter);
    CHECK(fixture.Text() == "    \n    print(\"unfinished");
    fixture.imgui.Press(ImGuiKey_Tab);
    CHECK(fixture.Text() == "    \n        print(\"unfinished");
    fixture.imgui.Press(ImGuiMod_Shift | ImGuiKey_Tab);
    CHECK(fixture.Text() == "    \n    print(\"unfinished");
    CHECK(fixture.Editor().GetLanguageName() == "ViceScript");
    CHECK(fixture.Editor().GetLanguage()->keywords.contains("null"));
    CHECK_FALSE(fixture.Editor().GetLanguage()->keywords.contains("nil"));
    for (const auto keyword :
         {"func", "var",    "const",    "return", "if",      "else",  "true",  "false",    "not",   "and",
          "or",   "import", "as",       "class",  "extends", "self",  "super", "override", "while", "for",
          "in",   "break",  "continue", "export", "is",      "yield", "enum",  "match"})
        CHECK(fixture.Editor().GetLanguage()->keywords.contains(keyword));
    for (const auto constructor : {"Vector2", "Vector3", "Color", "Component", "range"})
        CHECK(fixture.Editor().GetLanguage()->declarations.contains(constructor));
    for (const auto global : {"spot", "math", "string", "core", "random", "print", "OnStart", "OnUpdate",
                              "OnCollisionEnter", "OnTriggerExit2D"})
        CHECK(fixture.Editor().GetLanguage()->identifiers.contains(global));
    for (const auto member : {"entity", "transform", "scene", "enabled"})
        CHECK(fixture.Editor().GetLanguage()->identifiers.contains(member));
    CHECK_FALSE(fixture.Editor().GetLanguage()->identifiers.contains("Scene"));
    CHECK_FALSE(fixture.Editor().GetLanguage()->identifiers.contains("Input"));
    CHECK_FALSE(fixture.Editor().GetLanguage()->identifiers.contains("Self"));
    CHECK_FALSE(fixture.Editor().GetLanguage()->identifiers.contains("self"));
    CHECK(fixture.Editor().GetLanguage()->singleLineComment == "//");
    REQUIRE(fixture.panel.SaveActive());
}

TEST_CASE("Luau editing configures Luau syntax highlighting and engine identifiers", "[editor][code][luau][ui]") {
    CodeFixture fixture;
    fixture.Open("hello.luau", "-- hello\nlocal spot = require(\"@spot\")\nfunction OnStart() end\n");
    CHECK(fixture.Editor().GetLanguageName() == "Luau");
    CHECK(fixture.Editor().GetLanguage()->keywords.contains("nil"));
    CHECK_FALSE(fixture.Editor().GetLanguage()->keywords.contains("null"));
    for (const auto keyword : {"function", "local", "return", "if", "then", "else", "elseif", "end", "true", "false",
                               "not", "and", "or", "continue"})
        CHECK(fixture.Editor().GetLanguage()->keywords.contains(keyword));
    for (const auto constructor : {"Vector2", "Vector3", "Color"})
        CHECK(fixture.Editor().GetLanguage()->declarations.contains(constructor));
    for (const auto global :
         {"spot", "require", "math", "string", "table", "utf8", "coroutine", "print", "typeof", "vector"})
        CHECK(fixture.Editor().GetLanguage()->identifiers.contains(global));
    CHECK_FALSE(fixture.Editor().GetLanguage()->identifiers.contains("entity"));
    CHECK_FALSE(fixture.Editor().GetLanguage()->identifiers.contains("Scene"));
    CHECK_FALSE(fixture.Editor().GetLanguage()->identifiers.contains("Input"));
    CHECK_FALSE(fixture.Editor().GetLanguage()->identifiers.contains("Self"));
    CHECK_FALSE(fixture.Editor().GetLanguage()->identifiers.contains("self"));
    CHECK(fixture.Editor().GetLanguage()->singleLineComment == "--");
    CHECK(fixture.Editor().GetLanguage()->otherStringStart == "`");
    REQUIRE(fixture.panel.SaveActive());
}

TEST_CASE("Code documents follow asset renames and keep separate undo histories", "[editor][code][vice]") {
    CodeFixture fixture;
    const auto first = fixture.Open("first.vice", "override func OnStart() {}\n");
    fixture.Editor().ReplaceSectionText({0, 0}, {0, 0}, "// edit\n");
    const auto second = fixture.Open("second.vice", "override func OnUpdate(deltaTime) {}\n");
    CHECK(fixture.panel.GetDocumentCount() == 2);
    CHECK(fixture.panel.GetActiveAsset() == second);
    CHECK_FALSE(fixture.Editor().CanUndo());
    REQUIRE(fixture.context.GetProject()->GetAssetDatabase().Move("first.vice", "renamed.vice"));
    fixture.context.GetProject()->Refresh();
    fixture.panel.SyncWithAssets();
    REQUIRE(fixture.panel.OpenScript(first));
    CHECK(fixture.panel.GetDocumentCount() == 2);
    CHECK(fixture.panel.GetActivePath() == "renamed.vice");
    CHECK(fixture.Editor().CanUndo());
    REQUIRE(fixture.panel.SaveAll());
    CHECK(fixture.Read("renamed.vice") == "// edit\noverride func OnStart() {}\n");
    CHECK_FALSE(std::filesystem::exists(fixture.assets / "first.vice"));
}

TEST_CASE("Code documents protect edits from external changes and deleted assets", "[editor][code][vice]") {
    CodeFixture fixture;
    const auto asset = fixture.Open("hello.vice", "// original\n");
    fixture.Write("hello.vice", "// changed outside\n");
    fixture.panel.SyncWithAssets();
    CHECK(fixture.Text() == "// changed outside\n");
    fixture.Editor().ReplaceSectionText({0, 0}, {0, 0}, "// local\n");
    fixture.Write("hello.vice", "// changed again outside\n");
    CHECK_FALSE(fixture.panel.SaveActive());
    CHECK(fixture.Text() == "// local\n// changed outside\n");
    CHECK(fixture.Read("hello.vice") == "// changed again outside\n");
    CHECK_FALSE(fixture.panel.GetError().empty());
    REQUIRE(fixture.context.GetProject()->GetAssetDatabase().Delete("hello.vice"));
    fixture.context.GetProject()->Refresh();
    CHECK_FALSE(fixture.panel.SaveAll());
    CHECK(fixture.panel.GetActiveAsset() == asset);
    CHECK(fixture.panel.HasUnsavedChanges());
    CHECK_FALSE(std::filesystem::exists(fixture.assets / "hello.vice"));
}

TEST_CASE("Closing code documents asks to save discard or cancel", "[editor][code][vice][ui]") {
    CodeFixture fixture;
    fixture.Open("hello.vice", "// original\n");
    fixture.Replace("// changed\n");
    fixture.panel.CloseActive();
    fixture.imgui.Frame();
    fixture.Click("SavePrompt/Cancel");
    CHECK(fixture.panel.GetDocumentCount() == 1);
    CHECK(fixture.panel.HasUnsavedChanges());
    CHECK(fixture.Read("hello.vice") == "// original\n");
    fixture.panel.CloseActive();
    fixture.imgui.Frame();
    fixture.Click("SavePrompt/Save");
    CHECK(fixture.panel.GetDocumentCount() == 0);
    CHECK(fixture.Read("hello.vice") == "// changed\n");
    const auto id = fixture.context.GetProject()->GetAssetDatabase().FindByPath("hello.vice");
    REQUIRE(id);
    REQUIRE(fixture.panel.OpenScript(*id));
    fixture.imgui.Frame();
    fixture.Replace("// discarded\n");
    bool proceeded = false;
    fixture.panel.SetOpen(false);
    fixture.panel.ConfirmAllUnsavedChanges([&] { proceeded = true; });
    fixture.imgui.Frame();
    CHECK(fixture.panel.IsOpen());
    fixture.Click("SavePrompt/Discard");
    CHECK(proceeded);
    CHECK(fixture.Read("hello.vice") == "// changed\n");
}

TEST_CASE("A failed script save keeps the unsaved confirmation open", "[editor][code][vice][ui]") {
    CodeFixture fixture;
    fixture.Open("hello.vice", "// original\n");
    fixture.Replace("// local\n");
    fixture.Write("hello.vice", "// external\n");
    bool proceeded = false;
    fixture.panel.ConfirmAllUnsavedChanges([&] { proceeded = true; });
    fixture.imgui.Frame();
    fixture.Click("SavePrompt/Save");
    CHECK_FALSE(proceeded);
    CHECK(fixture.panel.HasUnsavedChanges());
    REQUIRE(fixture.panel.GetItemRect("SavePrompt/Cancel"));
    fixture.Click("SavePrompt/Cancel");
    CHECK_FALSE(proceeded);
    CHECK(fixture.Text() == "// local\n");
}

TEST_CASE("Code save shortcuts follow focus and reload asks before discarding text", "[editor][code][vice][ui]") {
    CodeFixture fixture;
    fixture.Open("hello.vice", "// original\n");
    fixture.Replace("// local\n");
    fixture.imgui.SetDraw([&] {
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({700, 600});
        fixture.panel.Draw();
        fixture.panel.HandleShortcuts();
        ImGui::SetNextWindowPos({710, 0});
        ImGui::SetNextWindowSize({280, 300});
        ImGui::Begin("Other");
        ImGui::TextUnformatted("Another editor panel");
        ImGui::End();
    });
    fixture.imgui.Frame();
    ImGui::SetWindowFocus("Other");
    fixture.imgui.Frame();
    CHECK_FALSE(fixture.panel.IsFocused());
    fixture.imgui.Press(ImGuiMod_Ctrl | ImGuiKey_S);
    CHECK(fixture.Read("hello.vice") == "// original\n");
    fixture.imgui.Frame();
    fixture.Click("Reload");
    fixture.Click("SavePrompt/Cancel");
    CHECK(fixture.Text() == "// local\n");
    fixture.Click("Reload");
    fixture.Click("SavePrompt/Discard");
    CHECK(fixture.Text() == "// original\n");
    CHECK_FALSE(fixture.panel.HasUnsavedChanges());
}

TEST_CASE("Unsaved script confirmation brings a hidden dock tab forward", "[editor][code][vice][ui]") {
    CodeFixture fixture;
    fixture.Open("hello.vice", "// original\n");
    fixture.Replace("// local\n");
    fixture.imgui.SetDraw([&] {
        const ImGuiID dock = ImGui::DockSpaceOverViewport(ImGui::GetID("TestDock"));
        ImGui::SetNextWindowDockID(dock, ImGuiCond_Always);
        fixture.panel.Draw();
        ImGui::SetNextWindowDockID(dock, ImGuiCond_Always);
        ImGui::Begin("Other");
        ImGui::TextUnformatted("Another dock tab");
        ImGui::End();
    });
    fixture.imgui.Frame();
    fixture.imgui.Frame();
    ImGui::SetWindowFocus("Other");
    fixture.imgui.Frame();
    CHECK_FALSE(fixture.panel.IsFocused());
    bool proceeded = false;
    fixture.panel.ConfirmAllUnsavedChanges([&] { proceeded = true; });
    fixture.imgui.Frame();
    fixture.imgui.Frame();
    fixture.Click("SavePrompt/Cancel");
    CHECK_FALSE(proceeded);
    CHECK(fixture.panel.HasUnsavedChanges());
}

TEST_CASE("ViceScript templates create component classes with valid names and native hooks", "[editor][code][vice]") {
    const auto* language = Spot::Editor::FindEditorScriptLanguage("test.vice");
    REQUIRE(language);
    REQUIRE(language->makeTemplate);
    for (const auto& [fileName, className] :
         {std::pair{"player_controller.vice", "PlayerController"}, std::pair{"New Script.vice", "NewScript"},
          std::pair{"123.vice", "Script123"}, std::pair{"Component.vice", "ComponentScript"},
          std::pair{"Vector3.vice", "Vector3Script"}, std::pair{"---.vice", "NewScript"}}) {
        const auto source = language->makeTemplate(fileName);
        CHECK(source.find("class " + std::string(className) + " : Component {") != std::string::npos);
        CHECK(source.find("override func OnStart()") != std::string::npos);
        CHECK(source.find("override func OnUpdate(deltaTime)") != std::string::npos);
#ifdef SPOT_ENABLE_VICESCRIPT
        Spot::Scene scene;
        auto& script = scene.CreateEntity().AddComponent<Spot::ScriptComponent>();
        script.language = "vice";
        script.source = source;
        scene.Update(0);
        CHECK_FALSE(script.IsFaulted());
#endif
    }
}

TEST_CASE("The code editor shows the first syntax error while editing", "[editor][code][luau]") {
    CodeFixture fixture;
    fixture.Open("scripts/broken.luau", "function OnStart()\n    local speed = \nend\n");
    REQUIRE(fixture.panel.GetItemRect("Diagnostic").has_value());
    CHECK(fixture.Editor().HasMarkers());
    fixture.Replace("function OnStart()\n    local speed = 3\nend\n");
    for (int i = 0; i < 30; ++i)
        fixture.imgui.Frame();
    CHECK_FALSE(fixture.panel.GetItemRect("Diagnostic").has_value());
    CHECK_FALSE(fixture.Editor().HasMarkers());
}

TEST_CASE("The code editor sets breakpoints and shows where scripts stopped", "[editor][code][luau][ui]") {
    CodeFixture fixture;
    const std::string source = "-- stopper\n"
                               "function OnUpdate(deltaTime)\n"
                               "    local speed = 3\n"
                               "    print(speed)\n"
                               "end\n";
    fixture.Open("scripts/stopper.luau", source);
    fixture.Editor().SetCursor({3, 0});
    fixture.imgui.Frame();
    fixture.Click("Breakpoint");
    CHECK(Spot::GetScriptBreakpoints("scripts/stopper.luau") == std::vector<std::size_t>{4});
    CHECK(fixture.Editor().HasMarkers());

    Spot::Scene scene;
    auto& script = scene.CreateEntity("Stopper").AddComponent<Spot::ScriptComponent>();
    script.language = "luau";
    script.sourceName = "scripts/stopper.luau";
    script.source = source;
    scene.Update(1.0f / 60.0f);
    fixture.imgui.Frame();
    fixture.imgui.Frame();
    REQUIRE(fixture.panel.GetItemRect("Continue").has_value());
    fixture.Click("Continue");
    fixture.imgui.Frame();
    CHECK_FALSE(fixture.panel.GetItemRect("Continue").has_value());

    fixture.Click("Breakpoint");
    CHECK(Spot::GetScriptBreakpoints("scripts/stopper.luau").empty());
}

namespace {

// The names suggested where the source has '|', after the member being typed there.
std::vector<std::string> SuggestAt(std::string source) {
    const std::size_t cursor = source.find('|');
    REQUIRE(cursor != std::string::npos);
    source.erase(cursor, 1);
    std::size_t start = cursor;
    while (start > 0 && (std::isalnum(static_cast<unsigned char>(source[start - 1])) || source[start - 1] == '_'))
        --start;
    std::vector<std::string> names;
    for (const auto& completion :
         Spot::Editor::GetViceMemberCompletions(source, start, std::string_view(source).substr(start, cursor - start)))
        names.push_back(completion.name);
    return names;
}

bool Suggests(const std::vector<std::string>& names, std::string_view name) {
    return std::ranges::find(names, name) != names.end();
}

constexpr std::string_view ScriptStart = "import spot as engine\n\nclass Mover : Component {\n"
                                         "    var speed = 3\n    func Jump() {}\n";

std::string InHook(std::string_view hook, std::string_view body) {
    return std::string(ScriptStart) + "    override func " + std::string(hook) + " {\n        " + std::string(body) +
           "\n    }\n}\n";
}

} // namespace

TEST_CASE("The code editor suggests the members of a component's implicit objects after a dot",
          "[editor][code][completion]") {
    const auto entity = SuggestAt(InHook("OnStart()", "entity.|"));
    CHECK(Suggests(entity, "transform"));
    CHECK(Suggests(entity, "name"));
    CHECK(Suggests(entity, "GetComponent"));
    CHECK(Suggests(entity, "SpriteRenderer"));
    // Older spellings still work but are not suggested.
    CHECK_FALSE(Suggests(entity, "Transform"));

    const auto typed = SuggestAt(InHook("OnStart()", "entity.tra|"));
    CHECK(typed == std::vector<std::string>{"transform"});
    // Case does not matter while typing.
    CHECK(Suggests(SuggestAt(InHook("OnStart()", "entity.getc|")), "GetComponent"));

    CHECK(Suggests(SuggestAt(InHook("OnStart()", "transform.|")), "position"));
    CHECK(Suggests(SuggestAt(InHook("OnStart()", "scene.|")), "FindEntity"));
    // Members of Component need no self.
    CHECK(Suggests(SuggestAt(InHook("OnStart()", "GetEntity().|")), "transform"));
}

TEST_CASE("The code editor follows chains of members and calls to suggest what they lead to",
          "[editor][code][completion]") {
    CHECK(Suggests(SuggestAt(InHook("OnStart()", "entity.transform.|")), "Translate"));
    CHECK(SuggestAt(InHook("OnStart()", "entity.transform.position.|")) == std::vector<std::string>{"x", "y", "z"});
    CHECK(Suggests(SuggestAt(InHook("OnStart()", "scene.FindEntity(\"Player\").transform.|")), "worldPosition"));
    CHECK(Suggests(SuggestAt(InHook("OnStart()", "Vector3(1, 2, 3).|")), "z"));

    // Components bring the members their script bindings describe, besides what every component has.
    const auto sprite = SuggestAt(InHook("OnStart()", "entity.GetComponent(\"SpriteRenderer\").|"));
    CHECK(Suggests(sprite, "color"));
    CHECK(Suggests(sprite, "enabled"));
    CHECK(Suggests(SuggestAt(InHook("OnStart()", "entity.SpriteRenderer.color.|")), "a"));
    CHECK(Suggests(SuggestAt(InHook("OnStart()", "entity.Camera.|")), "fieldOfView"));
}

TEST_CASE("The code editor suggests the script's own members, the spot module and hook parameters",
          "[editor][code][completion]") {
    const auto self = SuggestAt(InHook("OnStart()", "self.|"));
    CHECK(Suggests(self, "speed"));
    CHECK(Suggests(self, "Jump"));
    CHECK(Suggests(self, "StartCoroutine"));

    // The module goes by the name it was imported as.
    const auto module = SuggestAt(InHook("OnStart()", "engine.|"));
    CHECK(Suggests(module, "Input"));
    CHECK(Suggests(SuggestAt(InHook("OnStart()", "engine.Input.|")), "IsKeyDown"));
    CHECK(Suggests(SuggestAt(InHook("OnStart()", "engine.Time.|")), "deltaTime"));

    CHECK(Suggests(SuggestAt(InHook("OnCollisionEnter(hit)", "hit.|")), "relativeVelocity"));
    CHECK(Suggests(SuggestAt(InHook("OnCollisionEnter(hit)", "hit.other.|")), "transform"));
    CHECK(Suggests(SuggestAt(InHook("OnTriggerEnter2D(contact)", "contact.|")), "isTrigger"));
}

TEST_CASE("The code editor suggests nothing where it cannot tell what is before the dot",
          "[editor][code][completion]") {
    // Local variables and fields are not followed, nor anything that is not a member access.
    CHECK(SuggestAt(InHook("OnStart()", "var player = scene.FindEntity(\"P\")\n        player.|")).empty());
    CHECK(SuggestAt(InHook("OnStart()", "speed.|")).empty());
    CHECK(SuggestAt(InHook("OnStart()", "unknown.|")).empty());
    CHECK(SuggestAt(InHook("OnStart()", "entity|")).empty());
    CHECK(SuggestAt(InHook("OnStart()", "1.|")).empty());
    CHECK(SuggestAt(InHook("OnStart()", "entity.name.|")).empty());
    // Outside the hook, its parameter means nothing.
    CHECK(SuggestAt(std::string(ScriptStart) +
                    "    override func OnCollisionEnter(hit) {}\n    func Other() { hit.| }\n}\n")
              .empty());
}

TEST_CASE("Typing a dot in ViceScript opens member suggestions, which Enter inserts",
          "[editor][code][completion][ui]") {
    CodeFixture fixture;
    // As people type: a character a frame.
    const auto type = [&](std::string_view text) {
        for (const char c : text) {
            fixture.imgui.Type(std::string(1, c).c_str());
            fixture.imgui.Frame();
        }
        fixture.imgui.Frame();
    };
    fixture.Open("mover.vice", "class Mover : Component {\n    override func OnStart() {\n        \n    }\n}\n");
    fixture.Editor().SetCursor({2, 8});
    fixture.imgui.Frame();

    // The dot alone opens them, sorted, the first selected.
    type("entity.");
    fixture.imgui.Press(ImGuiKey_Enter);
    CHECK(fixture.Editor().GetLineText(2) == "        entity.active");

    // Typing narrows them.
    fixture.imgui.Press(ImGuiKey_End);
    type(" = transform.worldP");
    fixture.imgui.Press(ImGuiKey_Enter);
    CHECK(fixture.Editor().GetLineText(2) == "        entity.active = transform.worldPosition");

    // Luau scripts have none.
    fixture.Open("mover.luau", "local x = 1\n");
    fixture.Editor().SetCursor({1, 0});
    fixture.imgui.Frame();
    type("x.");
    fixture.imgui.Press(ImGuiKey_Enter);
    CHECK(fixture.Text() == "local x = 1\nx.\n");
}
