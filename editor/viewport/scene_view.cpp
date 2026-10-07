#include "viewport/scene_view.h"

#include "app/editor_context.h"
#include "imgui/editor_style.h"
#include "imgui/icons.h"
#include "imgui/imgui_renderer.h"
#include "scene/scene_document.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <utility>

namespace Spot::Editor {

namespace {

// Where an entity is, how far it reaches, for framing: its mesh's bounds, or a small sphere around it.
BoundingSphere GetWorldBounds(const Entity& entity) {
    const Matrix4& world = entity.GetTransform().GetWorldMatrix();
    const auto* renderer = entity.GetComponent<MeshRenderer>();
    if (!renderer || !renderer->GetMesh()) {
        return {entity.GetTransform().GetWorldPosition(), 0.5f};
    }
    const BoundingSphere local = renderer->GetMesh()->GetBounds();
    const Vector4 center = world * Vector4{local.center.x, local.center.y, local.center.z, 1.0f};
    float scale = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        const Vector4& column = world.columns[static_cast<std::size_t>(axis)];
        scale = std::max(scale, Length(Vector3{column.x, column.y, column.z}));
    }
    return {{center.x, center.y, center.z}, local.radius * scale};
}

const char* GetLightIcon(const Entity& entity) {
    if (entity.GetComponent<DirectionalLight>()) {
        return ICON_FA_SUN;
    }
    if (entity.GetComponent<PointLight>() || entity.GetComponent<SpotLight>()) {
        return ICON_FA_LIGHTBULB;
    }
    if (entity.GetComponent<AmbientLight>()) {
        return ICON_FA_CIRCLE_HALF_STROKE;
    }
    if (entity.GetComponent<Sky>()) {
        return ICON_FA_CLOUD_SUN;
    }
    return nullptr;
}

} // namespace

SceneView::SceneView(SceneDocument& document)
    : m_document(document),
      // Saved scenes keep their place in the layout from one run to the next.
      m_windowId(document.GetAssetId().IsValid() ? "Scene" + document.GetAssetId().ToString()
                                                 : std::format("NewScene{}", document.GetId())) {
}

SceneView::~SceneView() = default;

SceneView::DrawResult SceneView::Draw(EditorContext& context, ViewportRenderer* renderer, ImGuiID dockId) {
    DrawResult result;
    m_width = 0;
    m_height = 0;
    const std::string title = std::format("{}  {}###{}", ICON_FA_CUBE, m_document.GetName(), GetWindowId());
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (m_document.IsDirty()) {
        flags |= ImGuiWindowFlags_UnsavedDocument;
    }
    ImGui::SetNextWindowDockID(dockId, ImGuiCond_FirstUseEver);
    if (std::exchange(m_focusRequested, false)) {
        ImGui::SetNextWindowFocus();
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    bool open = true;
    const bool visible = ImGui::Begin(title.c_str(), &open, flags);
    ImGui::PopStyleVar();
    if (visible) {
        result.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        DrawViewport(context, renderer);
    }
    ImGui::End();
    result.closeRequested = !open;
    return result;
}

void SceneView::DrawViewport(EditorContext& context, ViewportRenderer* renderer) {
    const ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 1.0f || size.y < 1.0f) {
        return;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    const auto width = static_cast<std::uint32_t>(std::lround(size.x * scale.x));
    const auto height = static_cast<std::uint32_t>(std::lround(size.y * scale.y));

    // The whole area takes the mouse, so the camera can be moved anywhere in it; the overlay's buttons go over it.
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##Viewport", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) || ImGui::IsItemClicked(ImGuiMouseButton_Middle)) {
        ImGui::SetWindowFocus();
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (renderer && m_targets.Resize(renderer->GetDevice(), width, height, renderer->GetSampleCount())) {
        m_width = width;
        m_height = height;
        drawList->AddImage(ToImTextureId(*m_targets.GetColor()), origin, {origin.x + size.x, origin.y + size.y});
    } else {
        drawList->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(9, 10, 12, 255));
    }

    UpdateCamera(context, hovered, active, size.y);
    DrawIcons(origin, size);
    DrawOverlay(origin);
}

void SceneView::UpdateCamera(EditorContext& context, bool hovered, bool active, float viewHeight) {
    const ImGuiIO& io = ImGui::GetIO();
    EditorCameraInput input;
    input.deltaTime = io.DeltaTime;
    input.viewHeight = viewHeight;
    input.mouseDelta = context.GetMouseDelta();
    input.look = active && ImGui::IsMouseDown(ImGuiMouseButton_Right);
    input.pan = active && ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    input.orbit = active && io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    input.wheel = hovered || input.look ? io.MouseWheel : 0.0f;
    if (input.look) {
        const auto axis = [](ImGuiKey positive, ImGuiKey negative) {
            return (ImGui::IsKeyDown(positive) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(negative) ? 1.0f : 0.0f);
        };
        input.move = {axis(ImGuiKey_D, ImGuiKey_A), axis(ImGuiKey_E, ImGuiKey_Q), axis(ImGuiKey_W, ImGuiKey_S)};
        input.fast = io.KeyShift;
    }
    m_camera.Update(input);

    // Flying hides the cursor and keeps it in place, so the view can turn without limit.
    if (input.look != m_looking) {
        m_looking = input.look;
        context.GetWindow().SetMouseCaptured(m_looking);
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput && !m_looking &&
        ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.KeyCtrl) {
        FrameSelection();
    }
}

void SceneView::DrawIcons(const ImVec2& origin, const ImVec2& size) {
    const Camera3D camera = m_camera.GetCamera();
    const Matrix4 viewProjection = camera.GetViewProjection({size.x, size.y});
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    const float iconSize = ImGui::GetFontSize() * 1.25f;
    const ImU32 selectedColor = ImGui::GetColorU32(EditorColors::Accent);
    m_document.GetScene().ForEachEntity([&](Entity& entity) {
        const char* icon = GetLightIcon(entity);
        if (!icon) {
            return;
        }
        const Vector3 position = entity.GetTransform().GetWorldPosition();
        const Vector4 clip = viewProjection * Vector4{position.x, position.y, position.z, 1.0f};
        if (clip.w <= 0.0f) {
            return;
        }
        const ImVec2 screen{origin.x + (clip.x / clip.w * 0.5f + 0.5f) * size.x,
                            origin.y + (0.5f - clip.y / clip.w * 0.5f) * size.y};
        const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, icon);
        const ImVec2 corner{screen.x - textSize.x * 0.5f, screen.y - textSize.y * 0.5f};
        const bool selected = m_document.GetSelection().Contains(entity.GetId());
        drawList->AddText(ImGui::GetFont(), iconSize, {corner.x + 1.0f, corner.y + 1.0f}, IM_COL32(0, 0, 0, 160), icon);
        drawList->AddText(ImGui::GetFont(), iconSize, corner,
                          selected ? selectedColor : IM_COL32(235, 225, 170, entity.IsActiveInHierarchy() ? 230 : 90),
                          icon);
    });
    drawList->PopClipRect();
}

void SceneView::DrawOverlay(const ImVec2& origin) {
    // A small bar over the view's top-left corner, for what changes how the view looks.
    const ImVec2 padding = ImGui::GetStyle().WindowPadding;
    ImGui::SetCursorScreenPos({origin.x + padding.x, origin.y + padding.y});
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.11f, 0.13f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.21f, 0.24f, 0.90f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * ImGui::GetStyle().FontScaleDpi);
    const bool gridShown = m_options.showGrid;
    ImGui::PushStyleColor(ImGuiCol_Text, gridShown ? EditorColors::Accent : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    if (ImGui::Button(ICON_FA_TABLE_CELLS)) {
        m_options.showGrid = !gridShown;
    }
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip(gridShown ? "Hide the grid" : "Show the grid");
    ImGui::SameLine();
    const std::string speed = std::format("{} {:.1f} m/s", ICON_FA_GAUGE_HIGH, m_camera.GetSpeed());
    if (ImGui::Button(speed.c_str())) {
        ImGui::OpenPopup("##CameraSettings");
    }
    ImGui::SetItemTooltip("Camera speed while flying: hold the right mouse button and use WASD, Q and E;\n"
                          "the mouse wheel changes it meanwhile");
    if (ImGui::BeginPopup("##CameraSettings")) {
        float value = m_camera.GetSpeed();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
        if (ImGui::SliderFloat("Speed", &value, 0.1f, 100.0f, "%.1f m/s", ImGuiSliderFlags_Logarithmic)) {
            m_camera.SetSpeed(value);
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void SceneView::Render(RenderContext& context, ViewportRenderer& renderer) {
    if (m_width == 0 || m_height == 0 || !m_targets.GetColor()) {
        return;
    }
    renderer.Render(context, m_targets, m_document.GetScene(), m_camera.GetCamera(), m_options);
}

void SceneView::FrameSelection() {
    Scene& scene = m_document.GetScene();
    std::vector<BoundingSphere> spheres;
    for (const EntityId id : m_document.GetSelection().GetEntities()) {
        if (const Entity* entity = scene.GetEntity(id)) {
            spheres.push_back(GetWorldBounds(*entity));
        }
    }
    if (spheres.empty()) {
        scene.ForEachEntity([&](Entity& entity) {
            if (entity.GetComponent<MeshRenderer>()) {
                spheres.push_back(GetWorldBounds(entity));
            }
        });
    }
    if (spheres.empty()) {
        return;
    }
    // A sphere around the box around them all.
    Vector3 low{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()};
    Vector3 high = -low;
    for (const BoundingSphere& sphere : spheres) {
        low = {std::min(low.x, sphere.center.x - sphere.radius), std::min(low.y, sphere.center.y - sphere.radius),
               std::min(low.z, sphere.center.z - sphere.radius)};
        high = {std::max(high.x, sphere.center.x + sphere.radius), std::max(high.y, sphere.center.y + sphere.radius),
                std::max(high.z, sphere.center.z + sphere.radius)};
    }
    m_camera.Frame((low + high) * 0.5f, Length(high - low) * 0.5f);
}

} // namespace Spot::Editor
