#include "headless_imgui.h"

#include "app/editor_context.h"
#include "assets/asset_list.h"
#include "core/text.h"
#include "imgui/drag_payloads.h"
#include "imgui/widgets.h"
#include "inspector/inspector_backend.h"
#include "inspector/inspector_clipboard.h"
#include "inspector/multi_edit.h"
#include "inspector/property_widgets.h"
#include "panels/inspector_panel.h"
#include "scene/entity_actions.h"
#include "scene/scene_workspace.h"

#include <spot.h>
#include <spot/assets/script_source.h>
#include <spot/scene/scripting/script_component.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

using Catch::Approx;
using Spot::Entity;
using Spot::EntityId;

TEST_CASE("Names from code show as words", "[editor][inspector]") {
    using Spot::Editor::MakeDisplayName;
    CHECK(MakeDisplayName("castsShadows") == "Casts Shadows");
    CHECK(MakeDisplayName("DirectionalLight") == "Directional Light");
    CHECK(MakeDisplayName("HDRColor") == "HDR Color");
    CHECK(MakeDisplayName("m_speed") == "Speed");
    CHECK(MakeDisplayName("max_speed") == "Max Speed");
    CHECK(MakeDisplayName("texture2D") == "Texture2D");
    CHECK(MakeDisplayName("uv0Scale") == "Uv0 Scale");
    CHECK(MakeDisplayName("x") == "X");
}

TEST_CASE("Colors convert between linear and sRGB", "[editor][inspector]") {
    CHECK(Spot::Editor::LinearToSrgb(0.0f) == 0.0f);
    CHECK(Spot::Editor::LinearToSrgb(1.0f) == Approx(1.0f));
    CHECK(Spot::Editor::LinearToSrgb(0.2140411f) == Approx(0.5f).margin(1e-4));
    CHECK(Spot::Editor::SrgbToLinear(Spot::Editor::LinearToSrgb(0.3f)) == Approx(0.3f).margin(1e-5));
}

TEST_CASE("Rotations keep the Euler angles they were typed as", "[editor][inspector]") {
    Spot::Editor::EulerCache cache;
    // 180 degrees around Y is also 180 around X and Z; the cache remembers which one was typed.
    const Spot::Vector3 typed{0.0f, 180.0f, 0.0f};
    const Spot::Quaternion rotation = Spot::Quaternion::FromEuler({0.0f, Spot::ToRadians(180.0f), 0.0f});
    cache.Set(1, typed, rotation);
    const Spot::Vector3 shown = cache.Get(1, rotation);
    CHECK(shown.x == typed.x);
    CHECK(shown.y == typed.y);
    CHECK(shown.z == typed.z);

    // Another rotation, such as one a gizmo made, shows its own angles.
    const Spot::Quaternion other = Spot::Quaternion::FromEuler({Spot::ToRadians(30.0f), 0.0f, 0.0f});
    CHECK(cache.Get(1, other).x == Approx(30.0f).margin(1e-3));
}

TEST_CASE("Mixed values are found down to the axes of vectors", "[editor][inspector]") {
    const std::string primary = R"({"type":"Light","enabled":true,"data":{"color":[1,0.5,0],"range":4,"list":[1,2]}})";
    const std::vector<std::string> others{
        R"({"type":"Light","enabled":false,"data":{"color":[1,0.25,0],"range":4,"list":[1,2,3]}})",
        R"({"type":"Light","enabled":true,"data":{"color":[1,0.5,0],"range":4,"list":[1,2],"extra":1}})",
    };
    CHECK(Spot::Editor::FindMixedValues(primary, others) ==
          std::vector<std::string>{"/data/color/1", "/data/extra", "/data/list", "/enabled"});
    CHECK(Spot::Editor::FindMixedValues(primary, {}).empty());
}

TEST_CASE("Values copy from one component to another by pointer", "[editor][inspector]") {
    const std::string source = R"({"enabled":false,"data":{"color":[1,2,3],"list":[7,8,9],"new":5}})";
    const std::string target = R"({"enabled":true,"data":{"color":[4,5,6],"list":[1]}})";
    const std::vector<std::string> pointers{"/data/color/1", "/data/new", "/data/list/2", "/data/missing", "bad"};
    const std::string copied = Spot::Editor::CopyValues(source, target, pointers);
    // Only the axis that changed is copied; new fields are added, but arrays do not grow.
    const std::string expected = R"({"data":{"color":[4,2,6],"list":[1],"new":5},"enabled":true})";
    CHECK(copied == expected);
    CHECK(Spot::Editor::CopyValues("not json", target, pointers) == target);
    CHECK(Spot::Editor::EscapePointerToken("a/b~c") == "a~1b~0c");
}

TEST_CASE("Components and transforms round-trip through the clipboard's text", "[editor][inspector]") {
    const std::string component = R"({"data":{"range":4.0},"enabled":true,"type":"PointLight"})";
    const auto copied = Spot::Editor::ReadComponentClipboardText(Spot::Editor::MakeComponentClipboardText(component));
    REQUIRE(copied.has_value());
    CHECK(copied->typeName == "PointLight");
    CHECK(copied->text == component);
    CHECK_FALSE(Spot::Editor::ReadComponentClipboardText("plain text").has_value());
    CHECK_FALSE(Spot::Editor::ReadComponentClipboardText(R"({"format":"spot-component"})").has_value());

    Spot::Editor::Pose pose;
    pose.position = {1.0f, 2.0f, 3.0f};
    pose.rotation = Spot::Quaternion::FromEuler({0.5f, 0.0f, 0.0f});
    pose.scale = {2.0f, 2.0f, 2.0f};
    const auto pasted = Spot::Editor::ReadTransformClipboardText(Spot::Editor::MakeTransformClipboardText(pose));
    REQUIRE(pasted.has_value());
    CHECK(*pasted == pose);
    CHECK_FALSE(Spot::Editor::ReadTransformClipboardText(component).has_value());
}

TEST_CASE("Asset lists start with the built-in assets of the type", "[editor][inspector]") {
    const std::vector<Spot::Editor::AssetEntry> meshes = Spot::Editor::ListAssets(nullptr, Spot::AssetType::Mesh);
    REQUIRE(meshes.size() == 3);
    CHECK(meshes[0].name == "Cube");
    CHECK(meshes[0].builtIn);
    CHECK(Spot::Editor::ListAssets(nullptr, Spot::AssetType::Texture).size() == 2);
    CHECK(Spot::Editor::ListAssets(nullptr, Spot::AssetType::Sound).empty());

    const auto grid = Spot::Editor::FindAsset(nullptr, Spot::BuiltInAssets::GridTexture);
    REQUIRE(grid.has_value());
    CHECK(grid->path == "builtin/grid");
    CHECK_FALSE(Spot::Editor::FindAsset(nullptr, {}).has_value());
    CHECK_FALSE(Spot::Editor::FindAsset(nullptr, Spot::AssetId{7, 7}).has_value());
}

namespace {

// A new scene, open in a workspace, with a point light selected in an inspector that draws headless.
struct InspectorFixture {
    std::unique_ptr<Spot::Window> window = Spot::Window::Create();
    Spot::Tests::HeadlessImGui imgui;
    Spot::Editor::EditorSettings settings;
    std::unique_ptr<Spot::Editor::EditorContext> context;
    std::unique_ptr<Spot::Editor::SceneWorkspace> workspace;
    std::unique_ptr<Spot::Editor::InspectorPanel> panel;
    Spot::Editor::SceneDocument* document = nullptr;

    InspectorFixture() {
        REQUIRE(window != nullptr);
        context = std::make_unique<Spot::Editor::EditorContext>(*window, nullptr, settings);
        workspace = std::make_unique<Spot::Editor::SceneWorkspace>(*context);
        context->SetWorkspace(workspace.get());
        panel = std::make_unique<Spot::Editor::InspectorPanel>(*context);
        document = &workspace->NewScene();
        REQUIRE(Spot::Editor::CreateEntity(*document, Spot::Editor::EntityTemplate::PointLight));
        imgui.SetDraw([this] {
            ImGui::SetNextWindowPos({0.0f, 0.0f});
            ImGui::SetNextWindowSize({420.0f, 700.0f});
            panel->Draw();
        });
        Frame();
        Frame();
    }

    ~InspectorFixture() { context->SetWorkspace(nullptr); }

    void Frame() { imgui.Frame(); }

    Entity& Selected() {
        Entity* entity = document->GetScene().GetEntity(document->GetSelection().GetPrimary());
        REQUIRE(entity);
        return *entity;
    }

    Spot::PointLight& Light(Entity& entity) {
        auto* light = entity.GetComponent<Spot::PointLight>();
        REQUIRE(light);
        return *light;
    }

    // Another point light, which the selection then includes.
    Entity& AddLight() {
        REQUIRE(Spot::Editor::CreateEntity(*document, Spot::Editor::EntityTemplate::PointLight));
        return Selected();
    }

    ImVec2 Point(std::string_view item, float x = 0.5f) {
        const auto rect = panel->GetItemRect(item);
        REQUIRE(rect.has_value());
        return {rect->first.x + (rect->second.x - rect->first.x) * x, (rect->first.y + rect->second.y) * 0.5f};
    }

    void Click(std::string_view item) { imgui.Click(Point(item)); }

    std::size_t Steps() const { return document->GetHistory().GetUndoCount(); }
};

} // namespace

TEST_CASE("Dragging a field changes it while dragging, as one undoable step", "[editor][inspector][ui]") {
    InspectorFixture fixture;
    Spot::PointLight& light = fixture.Light(fixture.Selected());
    const float before = light.GetIntensity();
    const std::size_t steps = fixture.Steps();

    const ImVec2 from = fixture.Point("PointLight/data/intensity");
    fixture.imgui.Drag(from, {from.x + 60.0f, from.y});
    const float after = light.GetIntensity();
    CHECK(after > before);
    CHECK(fixture.Steps() == steps + 1);
    CHECK(fixture.document->GetHistory().GetUndoName() == "Edit Point Light");

    fixture.document->Undo();
    CHECK(light.GetIntensity() == before);
    fixture.document->Redo();
    CHECK(light.GetIntensity() == after);
}

#ifdef SPOT_ENABLE_LUAU
TEST_CASE("Script inspectors expose only the script asset and hide embedded fields", "[editor][inspector][ui][luau]") {
    InspectorFixture fixture;
    auto& component = fixture.Selected().AddComponent<Spot::ScriptComponent>();
    component.source = "function OnStart() end";
    component.sourceName = "legacy.luau";
    fixture.Frame();
    fixture.Frame();
    CHECK(fixture.panel->GetItemRect("ScriptComponent/data/script").has_value());
    CHECK_FALSE(fixture.panel->GetItemRect("ScriptComponent/data/language").has_value());
    CHECK_FALSE(fixture.panel->GetItemRect("ScriptComponent/data/source").has_value());
    CHECK_FALSE(fixture.panel->GetItemRect("ScriptComponent/data/sourceName").has_value());
    const auto saved = fixture.document->GetScene().SaveToString();
    CHECK(saved.find("legacy.luau") != std::string::npos);
    CHECK(saved.find("function OnStart()") != std::string::npos);
    component.script = std::make_shared<Spot::ScriptSource>();
    fixture.Frame();
    fixture.Frame();
    CHECK_FALSE(fixture.panel->GetItemRect("ScriptComponent/data/language").has_value());
}
#endif

TEST_CASE("Editing several entities changes the same field on all of them", "[editor][inspector][ui]") {
    InspectorFixture fixture;
    Entity& first = fixture.Selected();
    Entity& second = fixture.AddLight();
    fixture.Light(first).SetIntensity(2.0f);
    fixture.Light(first).SetRange(3.0f);
    fixture.Light(second).SetIntensity(5.0f);
    fixture.Light(second).SetRange(9.0f);
    first.GetTransform().SetPosition({1.0f, 0.0f, 0.0f});
    second.GetTransform().SetPosition({4.0f, 0.0f, 0.0f});
    const std::vector ids{first.GetId(), second.GetId()};
    fixture.document->GetSelection().Set(ids);
    fixture.Frame();
    const std::size_t steps = fixture.Steps();

    // The primary entity, selected last, gives the value; the others take it, and keep their other fields.
    const ImVec2 from = fixture.Point("PointLight/data/intensity");
    fixture.imgui.Drag(from, {from.x + 60.0f, from.y});
    CHECK(fixture.Light(second).GetIntensity() > 5.0f);
    CHECK(fixture.Light(first).GetIntensity() == fixture.Light(second).GetIntensity());
    CHECK(fixture.Light(first).GetRange() == 3.0f);
    CHECK(fixture.Light(second).GetRange() == 9.0f);
    CHECK(fixture.Steps() == steps + 1);
    fixture.document->Undo();
    CHECK(fixture.Light(first).GetIntensity() == 2.0f);
    CHECK(fixture.Light(second).GetIntensity() == 5.0f);

    // Transforms take only the axis that was dragged: Y, in the middle.
    const ImVec2 position = fixture.Point("Transform/position");
    fixture.imgui.Drag(position, {position.x + 40.0f, position.y});
    const Spot::Vector3 firstPosition = first.GetTransform().GetPosition();
    const Spot::Vector3 secondPosition = second.GetTransform().GetPosition();
    CHECK(firstPosition.y > 0.0f);
    CHECK(firstPosition.y == secondPosition.y);
    CHECK(firstPosition.x == 1.0f);
    CHECK(secondPosition.x == 4.0f);
    CHECK(fixture.document->GetHistory().GetUndoName() == "Move");
    fixture.document->Undo();
    CHECK(first.GetTransform().GetPosition().y == 0.0f);
    CHECK(second.GetTransform().GetPosition().y == 0.0f);
}

TEST_CASE("Components are added by searching their type", "[editor][inspector][ui]") {
    InspectorFixture fixture;
    Entity& entity = fixture.Selected();
    fixture.Click("AddComponent");
    fixture.imgui.Type("rigid");
    fixture.imgui.Press(ImGuiKey_Enter);
    CHECK(entity.GetComponent<Spot::Rigidbody>());
    CHECK(fixture.document->GetHistory().GetUndoName() == "Add Rigidbody");
    fixture.document->Undo();
    CHECK_FALSE(entity.GetComponent<Spot::Rigidbody>());
}

TEST_CASE("Component cards enable, copy, paste and remove components", "[editor][inspector][ui]") {
    InspectorFixture fixture;
    Entity& first = fixture.Selected();
    fixture.Light(first).SetIntensity(7.0f);
    fixture.Frame();

    fixture.Click("PointLight/enabled");
    CHECK_FALSE(fixture.Light(first).IsEnabled());
    CHECK(fixture.document->GetHistory().GetUndoName() == "Disable Point Light");
    fixture.document->Undo();
    CHECK(fixture.Light(first).IsEnabled());

    fixture.Click("PointLight/menu");
    fixture.Click("PointLight/menu/Copy");
    REQUIRE(Spot::Editor::ReadComponentClipboardText(Spot::Clipboard::GetText()).has_value());

    // Pasting values onto another light keeps whether it is enabled.
    Entity& second = fixture.AddLight();
    fixture.Light(second).SetEnabled(false);
    fixture.Frame();
    fixture.Click("PointLight/menu");
    fixture.Click("PointLight/menu/Paste Values");
    CHECK(fixture.Light(second).GetIntensity() == 7.0f);
    CHECK_FALSE(fixture.Light(second).IsEnabled());
    CHECK(fixture.document->GetHistory().GetUndoName() == "Paste Values");

    fixture.Click("PointLight/menu");
    fixture.Click("PointLight/menu/Paste as New");
    CHECK(second.GetComponents<Spot::PointLight>().size() == 2);
    fixture.document->Undo();
    CHECK(second.GetComponents<Spot::PointLight>().size() == 1);

    fixture.Click("PointLight/menu");
    fixture.Click("PointLight/menu/Remove");
    CHECK_FALSE(second.GetComponent<Spot::PointLight>());
    fixture.document->Undo();
    CHECK(second.GetComponent<Spot::PointLight>());
}

TEST_CASE("Typing a name renames the entity once the field is left", "[editor][inspector][ui]") {
    InspectorFixture fixture;
    Entity& entity = fixture.Selected();
    const std::size_t steps = fixture.Steps();
    fixture.Click("Name");
    fixture.imgui.Press(ImGuiMod_Ctrl | ImGuiKey_A);
    fixture.imgui.Type("Lamp");
    CHECK(entity.GetName() != "Lamp");
    fixture.imgui.Press(ImGuiKey_Enter);
    CHECK(entity.GetName() == "Lamp");
    CHECK(fixture.Steps() == steps + 1);
    CHECK(fixture.document->GetHistory().GetUndoName() == "Rename Entity");
}

TEST_CASE("Values that differ show as such again after changes and their undoing", "[editor][inspector][ui]") {
    InspectorFixture fixture;
    Entity& first = fixture.Selected();
    Entity& second = fixture.AddLight();
    fixture.Light(first).SetIntensity(2.0f);
    fixture.Light(second).SetIntensity(5.0f);
    const std::vector ids{first.GetId(), second.GetId()};
    fixture.document->GetSelection().Set(ids);
    fixture.Frame();
    const auto mixed = [&] { return fixture.panel->GetMixedValues(*fixture.document, "PointLight"); };
    CHECK(mixed() == std::vector<std::string>{"/data/intensity"});

    const std::string before = Spot::SaveComponentToString(fixture.Light(first));
    fixture.Light(first).SetIntensity(5.0f);
    const std::string after = Spot::SaveComponentToString(fixture.Light(first));
    fixture.Light(first).SetIntensity(2.0f);
    const std::size_t index = first.GetComponents<Spot::Component>().size() - 1;
    REQUIRE(fixture.document->Execute(
        std::make_unique<Spot::Editor::EditComponentCommand>(*fixture.document, first.GetId(), index, before, after)));
    fixture.Frame();
    CHECK(mixed().empty());

    fixture.document->Undo();
    fixture.Frame();
    CHECK(mixed() == std::vector<std::string>{"/data/intensity"});
}

TEST_CASE("Typed reference pickers filter choices and reject mismatched entity drops",
          "[editor][inspector][references]") {
    Spot::Tests::HeadlessImGui imgui;
    Spot::Scene scene;
    auto& plain = scene.CreateEntity("Impostor");
    auto& target = scene.CreateEntity("Lamp");
    auto& light = target.AddComponent<Spot::PointLight>();
    Spot::ComponentRef<Spot::PointLight> reference;
    ImVec2 plainSource{};
    ImVec2 lightSource{};
    ImVec2 field{};
    bool popupOpen = false;
    int lightDrags = 0;
    const auto center = [] {
        const auto min = ImGui::GetItemRectMin();
        const auto max = ImGui::GetItemRectMax();
        return ImVec2{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
    };
    imgui.SetDraw([&] {
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({420.0f, 500.0f});
        ImGui::Begin("Reference test");
        const auto source = [&](Entity& entity, ImVec2& point) {
            ImGui::Button(entity.GetName().c_str(), {120.0f, 25.0f});
            point = center();
            if (ImGui::BeginDragDropSource()) {
                if (&entity == &target) {
                    ++lightDrags;
                }
                const auto id = entity.GetId();
                ImGui::SetDragDropPayload(Spot::Editor::EntitiesPayload, &id, sizeof(id));
                ImGui::TextUnformatted(entity.GetName().c_str());
                ImGui::EndDragDropSource();
            }
        };
        source(plain, plainSource);
        source(target, lightSource);
        Spot::Editor::InspectorBackend backend({.scene = &scene});
        if (Spot::Editor::Widgets::BeginProperties("Fields")) {
            Spot::Archive archive(backend);
            archive.Field("target", reference);
            Spot::Editor::Widgets::EndProperties();
        }
        REQUIRE(backend.GetFieldRects().size() == 1);
        const auto& rect = backend.GetFieldRects().front();
        field = {(rect.min.x + rect.max.x) * 0.5f, (rect.min.y + rect.max.y) * 0.5f};
        popupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        ImGui::End();
    });
    imgui.Frame();
    imgui.Frame();
    CAPTURE(field.x, field.y, plainSource.x, plainSource.y, lightSource.x, lightSource.y);
    const ImVec2 fieldPoint = field;
    imgui.Click(fieldPoint);
    CHECK(popupOpen);
    imgui.Type("Impostor");
    imgui.Press(ImGuiKey_Enter);
    CHECK_FALSE(reference.IsValid(scene));
    imgui.Click({410.0f, 480.0f});
    CHECK_FALSE(popupOpen);
    imgui.Click(fieldPoint);
    CHECK(popupOpen);
    imgui.Type("Lamp");
    imgui.Press(ImGuiKey_Enter);
    CHECK(reference.Get(scene) == &light);
    reference.Reset();
    imgui.Frame();
    imgui.Drag(plainSource, fieldPoint);
    CHECK_FALSE(reference.IsValid(scene));
    imgui.Drag(lightSource, fieldPoint);
    CAPTURE(lightDrags, lightSource.x, lightSource.y, field.x, field.y);
    CHECK(lightDrags > 0);
    CHECK(reference.Get(scene) == &light);
}

TEST_CASE("Asset fields retain delivered payloads and check the asset type", "[editor][inspector][references]") {
    Spot::Tests::HeadlessImGui imgui;
    Spot::AssetId reference;
    Spot::AssetId dragged = Spot::BuiltInAssets::GridTexture;
    ImVec2 source{};
    ImVec2 field{};
    imgui.SetDraw([&] {
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({420.0f, 500.0f});
        ImGui::Begin("Asset drop test");
        ImGui::Button("Source", {120.0f, 25.0f});
        const auto min = ImGui::GetItemRectMin();
        const auto max = ImGui::GetItemRectMax();
        source = {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload(Spot::Editor::AssetsPayload, &dragged, sizeof(dragged));
            ImGui::TextUnformatted("Asset");
            ImGui::EndDragDropSource();
        }
        Spot::Editor::InspectorBackend backend({});
        if (Spot::Editor::Widgets::BeginProperties("Fields")) {
            Spot::Archive archive(backend);
            archive.Field("mesh", reference, {.assetType = Spot::AssetType::Mesh});
            Spot::Editor::Widgets::EndProperties();
        }
        REQUIRE(backend.GetFieldRects().size() == 1);
        const auto& rect = backend.GetFieldRects().front();
        field = {(rect.min.x + rect.max.x) * 0.5f, (rect.min.y + rect.max.y) * 0.5f};
        ImGui::End();
    });
    imgui.Frame();
    imgui.Frame();
    imgui.Drag(source, field);
    CHECK_FALSE(reference.IsValid());
    dragged = Spot::BuiltInAssets::CubeMesh;
    imgui.Drag(source, field);
    CHECK(reference == Spot::BuiltInAssets::CubeMesh);
}

TEST_CASE("Particle inspector preview controls do not create authored edits", "[editor][inspector][particles]") {
    InspectorFixture fixture;
    auto& entity = fixture.Selected();
    entity.RemoveComponent(fixture.Light(entity));
    auto& effect = entity.AddComponent<Spot::ParticleSystem>();
    fixture.Frame();
    fixture.Frame();
    auto& preview = fixture.document->GetParticlePreview();
    preview.Update(fixture.document->GetScene(), 0.5f);
    const auto authored = Spot::SaveComponentToString(effect);
    const auto steps = fixture.Steps();
    fixture.Click("ParticleSystem/preview/play");
    CHECK_FALSE(preview.IsPlaying());
    const auto time = preview.GetTime(fixture.document->GetScene());
    preview.Update(fixture.document->GetScene(), 1);
    CHECK(preview.GetTime(fixture.document->GetScene()) == time);
    fixture.Click("ParticleSystem/preview/restart");
    CHECK(preview.GetTime(fixture.document->GetScene()) == 0);
    const auto from = fixture.Point("ParticleSystem/preview/seek");
    fixture.imgui.Drag(from, {from.x + 40, from.y});
    CHECK(preview.GetTime(fixture.document->GetScene()) > 0);
    CHECK(fixture.Steps() == steps);
    CHECK(Spot::SaveComponentToString(effect) == authored);
    fixture.Click("ParticleSystem/preview/play");
    CHECK(preview.IsPlaying());
}

TEST_CASE("Curve and gradient key times commit before reordering", "[editor][inspector][particles]") {
    Spot::Tests::HeadlessImGui imgui;
    Spot::Curve curve = Spot::Curve::Linear(2, 4);
    Spot::Gradient gradient({{0, {1, 0, 0, 1}}, {1, {0, 0, 3, 1}}});
    bool editGradient = false;
    ImVec2 timeField{};
    std::vector<std::string> changed;
    imgui.SetDraw([&] {
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({600, 650});
        ImGui::Begin("Key time test");
        Spot::Editor::InspectorBackend backend({});
        if (Spot::Editor::Widgets::BeginProperties("Fields")) {
            const char* name = editGradient ? "gradient" : "curve";
            const std::string pointer = std::string("/data/") + name;
            ImGui::PushID(pointer.c_str());
            ImGui::GetStateStorage()->SetInt(ImGui::GetID((pointer + "/keys").c_str()), 1);
            ImGui::PopID();
            Spot::Archive archive(backend);
            if (editGradient)
                archive.Field(name, gradient);
            else
                archive.Field(name, curve);
            Spot::Editor::Widgets::EndProperties();
            for (const auto& rect : backend.GetFieldRects()) {
                if (rect.pointer == pointer + "/keys/1/time")
                    timeField = {(rect.min.x + rect.max.x) * 0.5f, (rect.min.y + rect.max.y) * 0.5f};
            }
            for (const auto& path : backend.GetChanged())
                changed.push_back(path);
        }
        ImGui::End();
    });
    const auto enterTime = [&] {
        imgui.Frame();
        imgui.Frame();
        REQUIRE(timeField.y > 0);
        imgui.Click(timeField);
        imgui.Press(ImGuiMod_Ctrl | ImGuiKey_A);
        imgui.Type("-0.5");
        CHECK(changed.empty());
    };
    enterTime();
    CHECK(curve.GetKeys()[1].time == 1);
    imgui.Press(ImGuiKey_Enter);
    REQUIRE(curve.GetKeys().size() == 2);
    CHECK(curve.GetKeys()[0].time == -0.5f);
    CHECK(curve.GetKeys()[0].value == 4);
    CHECK(curve.GetKeys()[1].value == 2);
    CHECK(changed == std::vector<std::string>{"/data/curve"});
    changed.clear();
    editGradient = true;
    timeField = {};
    enterTime();
    CHECK(gradient.GetKeys()[1].time == 1);
    imgui.Press(ImGuiKey_Enter);
    REQUIRE(gradient.GetKeys().size() == 2);
    CHECK(gradient.GetKeys()[0].time == -0.5f);
    CHECK(gradient.GetKeys()[0].color.b == 3);
    CHECK(gradient.GetKeys()[1].color.r == 1);
    CHECK(changed == std::vector<std::string>{"/data/gradient"});
}

#ifdef SPOT_ENABLE_LUAU
TEST_CASE("Script inspectors show exported fields and undo their edits", "[editor][inspector][ui][luau]") {
    InspectorFixture fixture;
    auto& component = fixture.Selected().AddComponent<Spot::ScriptComponent>();
    component.language = "luau";
    component.sourceName = "mover.luau";
    component.source = "speed = 3\nlabel = \"crate\"\nlocal hidden = 1\n";
    fixture.Frame();
    fixture.Frame();
    REQUIRE(fixture.panel->GetItemRect("ScriptComponent/data/fields/speed").has_value());
    CHECK(fixture.panel->GetItemRect("ScriptComponent/data/fields/label").has_value());
    CHECK_FALSE(fixture.panel->GetItemRect("ScriptComponent/data/fields/hidden").has_value());
    CHECK(component.fieldValues.empty());

    const std::size_t steps = fixture.Steps();
    const ImVec2 from = fixture.Point("ScriptComponent/data/fields/speed");
    fixture.imgui.Drag(from, {from.x + 60.0f, from.y});
    REQUIRE(component.fieldValues.contains("speed"));
    CHECK(std::get<double>(component.fieldValues.at("speed")) > 3);
    CHECK(fixture.Steps() == steps + 1);
    fixture.document->Undo();
    fixture.Frame();
    CHECK(component.fieldValues.empty());
}
#endif
