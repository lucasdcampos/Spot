#include "viewport/scene_view.h"

#include "app/editor_context.h"
#include "imgui/drag_payloads.h"
#include "imgui/editor_style.h"
#include "imgui/icons.h"
#include "imgui/imgui_renderer.h"
#include "project/editor_project.h"
#include "scene/entity_actions.h"
#include "scene/scene_document.h"
#include "scene/scene_workspace.h"

#include <imgui_internal.h>

#include <ImGuizmo.h>

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

// Orange, as sRGB (1.0, 0.55, 0.1) is, in the linear values the renderers take: it stands out from skies and grounds.
constexpr Color SelectionOutlineColor{1.0f, 0.262f, 0.010f, 1.0f};

// The meshes of the entity and of the entities below it, as the scene draws them.
void AddMeshes(const Entity& entity, std::vector<MeshInstance>& meshes) {
    if (!entity.IsActiveInHierarchy()) {
        return;
    }
    for (const MeshRenderer* renderer : entity.GetComponents<MeshRenderer>()) {
        if (renderer->IsEnabled() && renderer->GetMesh()) {
            meshes.push_back({renderer->GetMesh().Get(), entity.GetTransform().GetWorldMatrix()});
        }
    }
    for (const Entity* child : entity.GetChildren()) {
        AddMeshes(*child, meshes);
    }
}

// Where the view cube goes: the view's top-right corner.
ImRect GetViewCubeRect(const ImVec2& origin, const ImVec2& size) {
    const float side = std::round(ImGui::GetFontSize() * 6.0f);
    const ImVec2 padding = ImGui::GetStyle().WindowPadding;
    const ImVec2 min{origin.x + size.x - side - padding.x, origin.y + padding.y};
    return {min, {min.x + side, min.y + side}};
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
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 scale = io.DisplayFramebufferScale;
    const auto width = static_cast<std::uint32_t>(std::lround(size.x * scale.x));
    const auto height = static_cast<std::uint32_t>(std::lround(size.y * scale.y));
    m_outlineWidth = std::clamp(2.0f * ImGui::GetStyle().FontScaleDpi, 1.0f, 3.0f);

    // A click drawn into the picking target last frame is read now that the GPU has drawn it.
    if (std::optional<PickRequest> picking = std::exchange(m_picking, std::nullopt)) {
        const std::optional<std::size_t> index = renderer ? renderer->ReadPick() : std::nullopt;
        ApplyPick(*picking, index && *index < m_pickEntities.size() ? m_pickEntities[*index] : EntityId::Invalid);
    }

    // The whole area takes the mouse, so the camera can be moved anywhere in it. The view cube and the overlay's
    // buttons go over it, and the gizmo's handles take the mouse first (see TransformGizmo::Draw()). It is submitted
    // every frame: an item that lets others overlap it is only hovered after a frame of being under the mouse.
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##Viewport", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const ImGuiID areaId = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !io.KeyAlt;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) || ImGui::IsItemClicked(ImGuiMouseButton_Middle)) {
        ImGui::SetWindowFocus();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetsPayload)) {
            HandleAssetDrop(context, GetPayloadAssets(*payload), origin, size, scale);
        }
        ImGui::EndDragDropTarget();
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
    if (!m_looking) {
        HandleEntityShortcuts(m_document);
    }
    DrawIcons(origin, size);
    DrawViewCube(origin, size);
    m_gizmo.Draw(m_document, m_camera.GetCamera(), origin, size, context.GetSettings().GetGizmo(), GetGizmoId(),
                 areaId);
    // A click on the gizmo's handles drags them instead of selecting.
    if (clicked && !TransformGizmo::WantsMouse()) {
        ImGui::SetWindowFocus();
        HandleClick(origin, scale);
    }
    DrawOverlay(origin);
}

void SceneView::HandleClick(const ImVec2& origin, const ImVec2& scale) {
    const ImGuiIO& io = ImGui::GetIO();
    const PickRequest request{
        .pixel = {(io.MousePos.x - origin.x) * scale.x, (io.MousePos.y - origin.y) * scale.y},
        .toggle = io.KeyCtrl,
        .add = io.KeyShift,
    };
    // Icons are drawn over the meshes, the last one on top, so they are picked first.
    for (auto icon = m_icons.rbegin(); icon != m_icons.rend(); ++icon) {
        if (ImRect(icon->min, icon->max).Contains(io.MousePos)) {
            ApplyPick(request, icon->entity);
            return;
        }
    }
    // Meshes are picked on the GPU: drawn this frame, read back in the next.
    m_pick = request;
}

void SceneView::HandleAssetDrop(EditorContext& context, std::span<const AssetId> assets, const ImVec2& origin,
                                const ImVec2& size, const ImVec2& scale) {
    EditorProject* project = context.GetProject();
    if (!project) {
        return;
    }
    ImGui::SetWindowFocus();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (const AssetId asset : assets) {
        const AssetInfo* info = project->GetAssetDatabase().Find(asset);
        if (!info) {
            continue;
        }
        switch (info->type) {
        case AssetType::Scene:
            if (SceneWorkspace* workspace = context.GetWorkspace()) {
                workspace->OpenScene(asset);
            }
            return;
        case AssetType::Material:
            // The mesh under the mouse is found on the GPU, then gets the material.
            m_pick = PickRequest{.pixel = {(mouse.x - origin.x) * scale.x, (mouse.y - origin.y) * scale.y},
                                 .material = asset};
            return;
        default:
            CreateEntityFromAsset(m_document, *project, asset, EntityId::Invalid, GetGroundPoint(origin, size));
            break;
        }
    }
}

Vector3 SceneView::GetGroundPoint(const ImVec2& origin, const ImVec2& size) const {
    const Camera3D camera = m_camera.GetCamera();
    const Matrix4 inverse = Inverse(camera.GetViewProjection({size.x, size.y}));
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float x = (mouse.x - origin.x) / size.x * 2.0f - 1.0f;
    const float y = 1.0f - (mouse.y - origin.y) / size.y * 2.0f;
    const auto unproject = [&](float depth) {
        const Vector4 point = inverse * Vector4{x, y, depth, 1.0f};
        return Vector3{point.x, point.y, point.z} * (1.0f / point.w);
    };
    const Vector3 from = unproject(0.0f);
    const Vector3 direction = unproject(1.0f) - from;
    constexpr float MaxDistance = 500.0f;
    if (direction.y < -1e-6f) {
        const Vector3 hit = from + direction * (-from.y / direction.y);
        if (Length(hit - camera.position) < MaxDistance) {
            return hit;
        }
    }
    return m_camera.GetPivot();
}

void SceneView::ApplyPick(const PickRequest& request, EntityId entity) {
    if (request.material.IsValid()) {
        if (entity != EntityId::Invalid && ApplyMaterial(m_document, entity, request.material)) {
            m_document.GetSelection().Select(entity);
        }
        return;
    }
    Selection& selection = m_document.GetSelection();
    if (request.toggle) {
        if (entity != EntityId::Invalid) {
            selection.Toggle(entity);
        }
    } else if (request.add) {
        if (entity != EntityId::Invalid) {
            selection.Add(entity);
        }
    } else {
        selection.Select(entity);
    }
}

std::optional<std::pair<ImVec2, ImVec2>> SceneView::GetIconRect(EntityId entity) const {
    const auto found = std::ranges::find(m_icons, entity, &Icon::entity);
    return found != m_icons.end() ? std::optional(std::pair(found->min, found->max)) : std::nullopt;
}

int SceneView::GetGizmoId() const {
    return static_cast<int>(m_document.GetId());
}

void SceneView::DrawViewCube(const ImVec2& origin, const ImVec2& size) {
    const ImRect rect = GetViewCubeRect(origin, size);
    // Small views leave their room to the scene.
    if (rect.GetWidth() * 2.5f > size.x || rect.GetHeight() * 2.0f > size.y) {
        return;
    }
    // Clicks on the cube turn the view, so they neither select nor move the window.
    ImGui::SetCursorScreenPos(rect.Min);
    ImGui::InvisibleButton("##ViewCube", rect.GetSize());
    ImGui::SetItemTooltip("Click a face, an edge or a corner to look from that side; drag to turn the view");

    const Camera3D camera = m_camera.GetCamera();
    Matrix4 view = camera.GetView();
    const Matrix4 projection = camera.GetProjection({size.x, size.y});
    Matrix4 model;
    ImGuizmo::PushID(GetGizmoId());
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
    ImGuizmo::ViewManipulate(&view.columns[0].x, projection.GetData(), ImGuizmo::TRANSLATE, ImGuizmo::WORLD,
                             &model.columns[0].x, m_camera.GetDistance(), rect.Min, rect.GetSize(), 0);
    ImGuizmo::PopID();
    if (view != camera.GetView()) {
        // The camera looks down its -Z, the third row of the view matrix, negated.
        const Vector3 forward{-view.columns[0].z, -view.columns[1].z, -view.columns[2].z};
        m_camera.SetView(m_camera.GetPivot(), std::atan2(-forward.x, -forward.z),
                         std::asin(std::clamp(forward.y, -1.0f, 1.0f)), m_camera.GetDistance());
    }
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
    m_icons.clear();
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
        m_icons.push_back({entity.GetId(), corner, {corner.x + textSize.x, corner.y + textSize.y}});
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
    const Camera3D camera = m_camera.GetCamera();
    renderer.Render(context, m_targets, m_document.GetScene(), camera, m_options);

    std::vector<MeshInstance> selected;
    for (const Entity* entity : GetSelectedEntities(m_document)) {
        AddMeshes(*entity, selected);
    }
    renderer.DrawOutline(context, m_targets, camera, selected, SelectionOutlineColor, m_outlineWidth);

    if (m_pick) {
        std::vector<MeshInstance> meshes;
        m_pickEntities.clear();
        m_document.GetScene().ForEachComponent<MeshRenderer>([&](MeshRenderer& meshRenderer) {
            if (meshRenderer.GetMesh()) {
                const Entity& entity = meshRenderer.GetEntity();
                meshes.push_back({meshRenderer.GetMesh().Get(), entity.GetTransform().GetWorldMatrix()});
                m_pickEntities.push_back(entity.GetId());
            }
        });
        renderer.RenderPick(context, camera, {static_cast<float>(m_width), static_cast<float>(m_height)}, m_pick->pixel,
                            meshes);
        m_picking = std::exchange(m_pick, std::nullopt);
    }
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
