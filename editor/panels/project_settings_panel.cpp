#include "panels/project_settings_panel.h"

#include "core/text.h"
#include "imgui/editor_style.h"
#include "imgui/icons.h"
#include "imgui/widgets.h"
#include "inspector/inspector_backend.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <array>
#include <format>
#include <string>
#include <vector>

namespace Spot::Editor {

namespace {

constexpr std::array<const char*, 3> ActionTypeNames = {"Button", "Axis", "Vector 2"};
constexpr std::array<const char*, 4> CompositePartNames = {"Up", "Down", "Left", "Right"};
constexpr std::array<const char*, 2> AxisPartNames = {"Positive", "Negative"};

std::string GetControlLabel(const InputControl& control) {
    return control.source == InputSource::Key && control.code == static_cast<int>(Key::Unknown) ? "None"
                                                                                                : control.GetName();
}

std::string GetBindingLabel(const InputBinding& binding, InputActionType type) {
    if (binding.control.source != InputSource::Composite) {
        return GetControlLabel(binding.control);
    }
    std::string label;
    const std::size_t parts = type == InputActionType::Vector2 ? 4 : 2;
    for (std::size_t part = 0; part < parts; ++part) {
        label += (part > 0 ? " / " : "") + GetControlLabel(binding.parts[part]);
    }
    return label;
}

} // namespace

ProjectSettingsPanel::ProjectSettingsPanel(EditorContext& context)
    : Panel("ProjectSettings", ICON_FA_GEAR "  Project Settings"), m_context(context) {
    SetOpen(false);
}

void ProjectSettingsPanel::SetUpWindow() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({ImGui::GetFontSize() * 34.0f, ImGui::GetFontSize() * 26.0f}, ImGuiCond_FirstUseEver);
}

void ProjectSettingsPanel::OnDraw() {
    EditorProject* project = m_context.GetProject();
    if (!project) {
        DrawEmptyState(ICON_FA_GEAR, "Open a project to see its settings.");
        return;
    }
    ProjectSettings& settings = project->GetProject().GetSettings();

    if (!Widgets::BeginProperties("##ProjectSettings")) {
        return;
    }
    Widgets::PropertyLabel("Name", "The game's name, as the editor and the hub show it.");
    ImGui::InputText("##Name", &settings.name);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        Save();
    }

    Widgets::PropertyLabel("Version", "The game's own version, such as 1.0.0.");
    ImGui::InputText("##Version", &settings.version);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        Save();
    }

    Widgets::PropertyLabel("Startup Scene", "The scene the game starts with.");
    const AssetDatabase& database = project->GetAssetDatabase();
    const AssetInfo* startup = settings.startupScene.IsValid() ? database.Find(settings.startupScene) : nullptr;
    const std::string preview = startup ? startup->path : settings.startupScene.IsValid() ? "Missing scene" : "None";
    if (ImGui::BeginCombo("##StartupScene", preview.c_str())) {
        if (ImGui::Selectable("None", !settings.startupScene.IsValid())) {
            settings.startupScene = {};
            Save();
        }
        for (const AssetInfo& asset : database.GetAssets()) {
            if (asset.type != AssetType::Scene || asset.parent.IsValid()) {
                continue;
            }
            if (ImGui::Selectable((ICON_FA_CUBE "  " + asset.path).c_str(), asset.id == settings.startupScene)) {
                settings.startupScene = asset.id;
                Save();
            }
        }
        ImGui::EndCombo();
    }

    Widgets::PropertyLabel("Project File");
    const std::string file = PathToUtf8(project->GetProject().GetFile());
    ImGui::TextColored(EditorColors::TextMuted, "%s", file.c_str());
    ImGui::SetItemTooltip("%s", file.c_str());
    Widgets::EndProperties();

    settings.sections.ForEach([this, project](std::string_view name, auto& section) {
        // Drawn by an editor of its own, below.
        if (name == InputSettings::SectionName) {
            return;
        }
        ImGui::Spacing();
        Widgets::Heading(MakeDisplayName(name).c_str());
        const std::string id = "##" + std::string(name);
        if (!Widgets::BeginProperties(id.c_str())) {
            return;
        }
        InspectorBackend backend({.project = project, .root = "/" + std::string(name)});
        Archive archive(backend);
        section.Serialize(archive);
        Widgets::EndProperties();
        m_sectionsChanged = m_sectionsChanged || backend.WasChanged();
    });
    DrawLayers(settings.sections.physics);
    DrawInput(settings.sections.input);
    // Once a drag or an edit ends, rather than on every change it makes.
    if (m_sectionsChanged && !ImGui::IsAnyItemActive()) {
        m_sectionsChanged = false;
        Save();
    }

    ImGui::Spacing();
    if (ImGui::Button(ICON_FA_UP_RIGHT_FROM_SQUARE "  Show in File Manager")) {
        Shell::Open(project->GetProject().GetDirectory());
    }
}

void ProjectSettingsPanel::DrawLayers(PhysicsSettings& physics) {
    ImGui::Spacing();
    Widgets::Heading("Layers");
    if (ImGui::TreeNodeEx("Names", ImGuiTreeNodeFlags_SpanAvailWidth)) {
        if (Widgets::BeginProperties("##layerNames")) {
            for (int layer = 0; layer < Entity::LayerCount; ++layer) {
                Widgets::PropertyLabel(std::format("Layer {}", layer).c_str(),
                                       layer == 0 ? "Entities start on the default layer, which keeps its name"
                                                  : nullptr);
                ImGui::PushID(layer);
                ImGui::BeginDisabled(layer == 0);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputTextWithHint("##name", "Unused", &physics.layerNames[static_cast<std::size_t>(layer)]);
                m_sectionsChanged = m_sectionsChanged || ImGui::IsItemDeactivatedAfterEdit();
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            Widgets::EndProperties();
        }
        ImGui::TreePop();
    }

    // Only named layers are in use, so only they show, each pair once.
    std::vector<int> named;
    for (int layer = 0; layer < Entity::LayerCount; ++layer) {
        if (!physics.layerNames[static_cast<std::size_t>(layer)].empty()) {
            named.push_back(layer);
        }
    }
    if (!ImGui::TreeNodeEx("Collisions", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    ImGui::TextDisabled("Bodies of two layers collide when their box is checked.");
    const auto columns = static_cast<int>(named.size()) + 1;
    if (ImGui::BeginTable("##collisions", columns, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoHeaderLabel);
        // Columns go from the last layer to the first, so the checked pairs form a triangle.
        for (auto layer = named.rbegin(); layer != named.rend(); ++layer) {
            ImGui::TableSetupColumn(physics.layerNames[static_cast<std::size_t>(*layer)].c_str(),
                                    ImGuiTableColumnFlags_AngledHeader);
        }
        ImGui::TableAngledHeadersRow();
        for (std::size_t row = 0; row < named.size(); ++row) {
            const int a = named[row];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(physics.layerNames[static_cast<std::size_t>(a)].c_str());
            for (std::size_t column = 0; column + row < named.size(); ++column) {
                const int b = named[named.size() - 1 - column];
                ImGui::TableNextColumn();
                bool collides = physics.Collides(a, b);
                ImGui::PushID(a * Entity::LayerCount + b);
                if (ImGui::Checkbox("##collides", &collides)) {
                    physics.SetCollides(a, b, collides);
                    m_sectionsChanged = true;
                }
                ImGui::SetItemTooltip("%s and %s", physics.layerNames[static_cast<std::size_t>(a)].c_str(),
                                      physics.layerNames[static_cast<std::size_t>(b)].c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::TreePop();
}

void ProjectSettingsPanel::CaptureBinding(std::size_t action, std::size_t binding, int part) {
    m_capture = Capture{action, binding, part};
}

void ProjectSettingsPanel::UpdateCapture(InputSettings& input) {
    const InputState* state = m_context.GetInput();
    if (!m_capture || !state) {
        return;
    }
    const Capture capture = *m_capture;
    if (capture.action >= input.actions.size() || capture.binding >= input.actions[capture.action].bindings.size() ||
        capture.part >= 4) {
        m_capture.reset();
        return;
    }
    if (state->IsKeyPressed(Key::Escape)) {
        m_capture.reset();
        return;
    }
    const std::optional<InputControl> pressed = FindPressedControl(*state);
    if (!pressed) {
        return;
    }
    InputBinding& binding = input.actions[capture.action].bindings[capture.binding];
    (capture.part < 0 ? binding.control : binding.parts[static_cast<std::size_t>(capture.part)]) = *pressed;
    m_capture.reset();
    m_sectionsChanged = true;
}

void ProjectSettingsPanel::DrawInput(InputSettings& input) {
    // Before anything is drawn, so the click that starts a capture is not what it captures.
    UpdateCapture(input);

    ImGui::Spacing();
    Widgets::Heading("Input Actions");
    ImGui::TextDisabled("Games ask whether an action happened, whichever of its bindings the player used. "
                        "Right-click an action or a binding to remove it.");
    std::optional<std::size_t> removed;
    for (std::size_t index = 0; index < input.actions.size(); ++index) {
        InputAction& action = input.actions[index];
        ImGui::PushID(static_cast<int>(index));
        const std::string label = (action.name.empty() ? std::string("Unnamed") : action.name) + "###action";
        const bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem(ICON_FA_TRASH_CAN "  Remove Action")) {
                removed = index;
            }
            ImGui::EndPopup();
        }
        if (open) {
            if (Widgets::BeginProperties("##action")) {
                Widgets::PropertyLabel("Name", "What games ask for, as in Input::IsActionPressed(\"Jump\").");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputText("##name", &action.name);
                m_sectionsChanged = m_sectionsChanged || ImGui::IsItemDeactivatedAfterEdit();

                Widgets::PropertyLabel("Type", "On or off, a value from -1 to 1, or two of them, as moving takes.");
                ImGui::SetNextItemWidth(-FLT_MIN);
                int type = static_cast<int>(action.type);
                if (ImGui::Combo("##type", &type, ActionTypeNames.data(), static_cast<int>(ActionTypeNames.size()))) {
                    action.type = static_cast<InputActionType>(type);
                    m_sectionsChanged = true;
                }
                if (action.type == InputActionType::Button) {
                    Widgets::PropertyLabel("Press Point", "How far an axis must go for the button to be down.");
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    m_sectionsChanged = ImGui::SliderFloat("##pressPoint", &action.pressPoint, 0.05f, 1.0f, "%.2f") ||
                                        m_sectionsChanged;
                }
                Widgets::EndProperties();
            }
            for (std::size_t binding = 0; binding < action.bindings.size(); ++binding) {
                ImGui::PushID(static_cast<int>(binding));
                DrawBinding(input, index, binding);
                ImGui::PopID();
            }
            if (ImGui::Button(ICON_FA_PLUS "  Add Binding")) {
                ImGui::OpenPopup("AddBinding");
            }
            if (ImGui::BeginPopup("AddBinding")) {
                const auto add = [&](InputBinding binding) {
                    action.bindings.push_back(binding);
                    m_sectionsChanged = true;
                };
                if (ImGui::MenuItem(ICON_FA_KEYBOARD "  Key, Button or Axis...")) {
                    add(InputBinding::Of(InputControl::Of(Key::Unknown)));
                    CaptureBinding(index, action.bindings.size() - 1);
                }
                const InputControl none = InputControl::Of(Key::Unknown);
                if (ImGui::MenuItem(action.type == InputActionType::Vector2 ? "Four Buttons" : "Two Buttons")) {
                    add(action.type == InputActionType::Vector2 ? InputBinding::Composite(none, none, none, none)
                                                                : InputBinding::Composite(none, none));
                }
                if (ImGui::MenuItem("Mouse Motion")) {
                    add(InputBinding::Of({InputSource::MouseMotion, 0}));
                }
                if (ImGui::MenuItem("Mouse Wheel")) {
                    add(InputBinding::Of({InputSource::Scroll, 0}));
                }
                if (ImGui::MenuItem(ICON_FA_GAMEPAD "  Left Stick")) {
                    add({.control = {InputSource::GamepadStick, 0}, .deadZone = 0.2f});
                }
                if (ImGui::MenuItem(ICON_FA_GAMEPAD "  Right Stick")) {
                    add({.control = {InputSource::GamepadStick, 1}, .deadZone = 0.2f});
                }
                ImGui::EndPopup();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removed) {
        input.actions.erase(input.actions.begin() + static_cast<std::ptrdiff_t>(*removed));
        m_capture.reset();
        m_sectionsChanged = true;
    }
    if (ImGui::Button(ICON_FA_PLUS "  Add Action")) {
        std::string name = "New Action";
        for (int suffix = 2; std::ranges::any_of(input.actions, [&](const InputAction& a) { return a.name == name; });
             ++suffix) {
            name = std::format("New Action {}", suffix);
        }
        input.actions.push_back({.name = name});
        m_sectionsChanged = true;
    }
}

void ProjectSettingsPanel::DrawBinding(InputSettings& input, std::size_t action, std::size_t index) {
    InputAction& owner = input.actions[action];
    InputBinding& binding = owner.bindings[index];
    const std::string label = GetBindingLabel(binding, owner.type) + "###binding";
    const bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
    bool remove = false;
    if (ImGui::BeginPopupContextItem()) {
        remove = ImGui::MenuItem(ICON_FA_TRASH_CAN "  Remove Binding");
        ImGui::EndPopup();
    }
    if (open) {
        if (Widgets::BeginProperties("##binding")) {
            if (binding.control.source == InputSource::Composite) {
                const bool vector = owner.type == InputActionType::Vector2;
                const std::size_t parts = vector ? CompositePartNames.size() : AxisPartNames.size();
                for (std::size_t part = 0; part < parts; ++part) {
                    Widgets::PropertyLabel(vector ? CompositePartNames[part] : AxisPartNames[part]);
                    ImGui::PushID(static_cast<int>(part));
                    DrawCaptureButton(binding.parts[part], action, index, static_cast<int>(part));
                    ImGui::PopID();
                }
            } else {
                Widgets::PropertyLabel("Control", "Click, then press the key, button or axis to bind.");
                DrawCaptureButton(binding.control, action, index, -1);
            }
            if (binding.control.source == InputSource::MouseMotion || binding.control.source == InputSource::Scroll) {
                Widgets::PropertyLabel("Axes", "Actions of one axis read X, or Y when it is the only one.");
                ImGui::SetNextItemWidth(-FLT_MIN);
                constexpr std::array<const char*, 3> Axes = {"Both", "X", "Y"};
                m_sectionsChanged =
                    ImGui::Combo("##axes", &binding.control.code, Axes.data(), static_cast<int>(Axes.size())) ||
                    m_sectionsChanged;
            }
            Widgets::PropertyLabel("Dead Zone", "Values closer to rest count as 0, as sticks rarely rest at 0.");
            ImGui::SetNextItemWidth(-FLT_MIN);
            m_sectionsChanged =
                ImGui::SliderFloat("##deadZone", &binding.deadZone, 0.0f, 0.95f, "%.2f") || m_sectionsChanged;
            Widgets::PropertyLabel("Scale");
            ImGui::SetNextItemWidth(-FLT_MIN);
            m_sectionsChanged = ImGui::DragFloat("##scale", &binding.scale, 0.01f) || m_sectionsChanged;
            Widgets::PropertyLabel(owner.type == InputActionType::Vector2 ? "Invert X" : "Invert");
            m_sectionsChanged = ImGui::Checkbox("##invertX", &binding.invertX) || m_sectionsChanged;
            if (owner.type == InputActionType::Vector2) {
                Widgets::PropertyLabel("Invert Y");
                m_sectionsChanged = ImGui::Checkbox("##invertY", &binding.invertY) || m_sectionsChanged;
            }
            Widgets::EndProperties();
        }
        ImGui::TreePop();
    }
    if (remove) {
        owner.bindings.erase(owner.bindings.begin() + static_cast<std::ptrdiff_t>(index));
        m_capture.reset();
        m_sectionsChanged = true;
    }
}

void ProjectSettingsPanel::DrawCaptureButton(const InputControl& control, std::size_t action, std::size_t binding,
                                             int part) {
    const bool capturing =
        m_capture && m_capture->action == action && m_capture->binding == binding && m_capture->part == part;
    const std::string label =
        capturing ? std::string(ICON_FA_CIRCLE_DOT "  Press a control (Esc cancels)") : GetControlLabel(control);
    ImGui::Button((label + "###capture").c_str(), {-FLT_MIN, 0.0f});
    // On the press, so neither the release nor keyboard navigation reach the capture.
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        CaptureBinding(action, binding, part);
    }
}

void ProjectSettingsPanel::Save() {
    EditorProject* project = m_context.GetProject();
    if (project && project->GetProject().Save()) {
        const ProjectSettings& settings = project->GetProject().GetSettings();
        m_context.GetSettings().AddRecentProject(project->GetProject().GetFile(), settings.name);
        m_context.GetSettings().Save();
        m_context.GetWindow().SetTitle(settings.name + " - Spot Editor");
    }
}

} // namespace Spot::Editor
