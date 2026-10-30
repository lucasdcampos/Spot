#include "viewport/scene_view.h"
#include <spot/scene/ui/ui_system.h>

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
#include <numbers>
#include <utility>

namespace Spot::Editor {

namespace {

// Where an entity is, how far it reaches, for framing: its mesh's bounds, or a small sphere around it.
BoundingSphere GetWorldBounds(const Entity& entity) {
    const Matrix4& world = entity.GetTransform().GetWorldMatrix();
    const auto* renderer = entity.GetComponent<MeshRenderer>();
    BoundingSphere local;
    if (renderer && renderer->GetMesh()) {
        local = renderer->GetMesh()->GetBounds();
    } else if (const auto* skin = entity.GetComponent<SkinnedMeshRenderer>(); skin && skin->GetMesh()) {
        local = skin->GetMesh()->GetBounds();
        local.radius += skin->GetBoundsPadding();
    } else if (const auto* map = entity.GetComponent<Tilemap>()) {
        const auto& data = map->GetData();
        const Vector2 size{static_cast<float>(data.width) * data.cellSize.x,
                           static_cast<float>(data.height) * data.cellSize.y};
        local = {{size.x * 0.5f, size.y * 0.5f, 0}, Length(size) * 0.5f};
    } else {
        return {entity.GetTransform().GetWorldPosition(), 0.5f};
    }
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

// Cameras' frustums are drawn as wide as most games' views are.
constexpr float GameAspectRatio = 16.0f / 9.0f;

// Where a sprite's rectangle is: the built-in plane, which lies in XZ, turned to face +Z and sized like the sprite.
Matrix4 GetSpriteMatrix(const SpriteRenderer& sprite) {
    const Vector2 size = sprite.GetSize();
    return sprite.GetEntity().GetTransform().GetWorldMatrix() * Matrix4::Scale({size.x, size.y, 1.0f}) *
           Matrix4::Rotation(Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, ToRadians(90.0f)));
}

// Where the view cube goes: the view's top-right corner.
ImRect GetViewCubeRect(const ImVec2& origin, const ImVec2& size) {
    const float side = std::round(ImGui::GetFontSize() * 6.0f);
    const ImVec2 padding = ImGui::GetStyle().WindowPadding;
    const ImVec2 min{origin.x + size.x - side - padding.x, origin.y + padding.y};
    return {min, {min.x + side, min.y + side}};
}

const char* GetLightIcon(const Entity& entity) {
    if (entity.GetComponent<Camera>()) {
        return ICON_FA_VIDEO;
    }
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
                                                 : std::format("NewScene{}", document.GetId())),
      m_shown(&document) {
}

SceneView::~SceneView() {
    m_uiRectGizmo.Finish(*m_shown);
}

SceneView::DrawResult SceneView::Draw(EditorContext& context, ViewportRenderer* renderer, ImGuiID dockId) {
    DrawResult result;
    // While the document plays, the view shows the copy being played, through the game's camera at first.
    PlaySession* play = context.GetWorkspace() ? context.GetWorkspace()->GetPlaySession() : nullptr;
    const bool playing = play && &play->GetSource() == &m_document;
    if (playing && !m_playing) {
        m_gameView = true;
    }
    auto& preview = m_document.GetParticlePreview();
    if (playing)
        preview.Clear(m_document.GetScene());
    else {
        preview.Select(m_document.GetScene(), m_document.GetSelection().GetPrimary(), {},
                       m_document.GetSceneRevision());
        preview.Update(m_document.GetScene(), ImGui::GetIO().DeltaTime);
    }
    auto& animation = m_document.GetAnimationPreview();
    if (playing) {
        animation.Clear(m_document.GetScene());
    } else {
        animation.Select(m_document.GetScene(), m_document.GetSelection().GetPrimary(), {},
                         m_document.GetSceneRevision());
        animation.Update(m_document.GetScene(), ImGui::GetIO().DeltaTime);
    }
    m_playing = playing;
    m_shown = playing && play->GetDocument() ? play->GetDocument() : &m_document;
    m_rendering = context.GetProject() ? context.GetProject()->GetProject().GetSettings().sections.rendering
                                       : RenderingSettings{};
    m_width = 0;
    m_height = 0;
    const std::string title = std::format("{}  {}{}###{}", playing ? ICON_FA_PLAY : ICON_FA_CUBE, m_document.GetName(),
                                          playing                 ? " (Playing)"
                                          : m_document.IsPrefab() ? " (Prefab)"
                                                                  : "",
                                          GetWindowId());
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
        // ImGui moves a docked window's name when its title changes, as when the scene plays, but leaves its draw
        // list pointing to the old one. ImGuizmo finds the window by that name to know whether the mouse is over it.
        ImGui::GetWindowDrawList()->_OwnerName = ImGui::GetCurrentWindow()->Name;
        result.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        DrawViewport(context, renderer);
    }
    if (playing) {
        // The game gets the keyboard and the mouse while its view has focus and shows it.
        play->SetView(visible && result.focused && m_gameView, {m_viewOrigin.x, m_viewOrigin.y},
                      {static_cast<float>(m_width), static_cast<float>(m_height)},
                      {ImGui::GetIO().DisplayFramebufferScale.x, ImGui::GetIO().DisplayFramebufferScale.y});
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
    m_viewOrigin = {origin.x, origin.y};
    const bool game = IsShowingGame();
    const ImVec2 scale = io.DisplayFramebufferScale;
    const auto width = static_cast<std::uint32_t>(std::lround(size.x * scale.x));
    const auto height = static_cast<std::uint32_t>(std::lround(size.y * scale.y));
    m_outlineWidth = std::clamp(2.0f * ImGui::GetStyle().FontScaleDpi, 1.0f, 3.0f);
    m_options.grid2D = m_camera.Is2D();

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
    // The game's view leaves the mouse and the keys to the game.
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !io.KeyAlt && !game;
    if (ImGui::IsItemClicked() || ImGui::IsItemClicked(ImGuiMouseButton_Right) ||
        ImGui::IsItemClicked(ImGuiMouseButton_Middle)) {
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

    if (game) {
        DrawOverlay(origin);
        return;
    }
    UpdateCamera(context, hovered, active, size.y);
    if (!m_looking) {
        HandleEntityShortcuts(*m_shown);
    }
    DrawCameraFrustums(origin, size);
    DrawIcons(origin, size);
    if (!m_camera.Is2D()) {
        DrawViewCube(origin, size);
    }
    const bool uiPointer =
        m_options.drawUi &&
        m_uiRectGizmo.Draw(*m_shown, origin, size, {static_cast<float>(width), static_cast<float>(height)});
    const auto* primary = m_shown->GetScene().GetEntity(m_shown->GetSelection().GetPrimary());
    const bool uiSelected =
        m_options.drawUi && primary && (primary->GetComponent<UiRect>() || primary->GetComponent<Canvas>());
    if (!uiSelected)
        m_gizmo.Draw(*m_shown, m_camera.GetCamera(), origin, size, context.GetSettings().GetGizmo(), GetGizmoId(),
                     areaId, m_camera.Is2D());
    // A click on the gizmo's handles drags them instead of selecting.
    if (clicked && !uiPointer && !TransformGizmo::WantsMouse()) {
        ImGui::SetWindowFocus();
        HandleClick(origin, scale);
    }
    DrawOverlay(origin);
    if (SceneWorkspace* workspace = context.GetWorkspace(); workspace && context.GetProject() &&
                                                            workspace->IsPlayPending() &&
                                                            workspace->GetActiveDocument() == &m_document) {
        DrawPlayPending(context, origin, size);
    }
}

void SceneView::HandleClick(const ImVec2& origin, const ImVec2& scale) {
    const ImGuiIO& io = ImGui::GetIO();
    const PickRequest request{
        .pixel = {(io.MousePos.x - origin.x) * scale.x, (io.MousePos.y - origin.y) * scale.y},
        .toggle = io.KeyCtrl,
        .add = io.KeyShift,
    };
    if (m_options.drawUi) {
        auto& ui = m_shown->GetScene().GetOrAddSystem<Spot::UiSystem>();
        ui.Layout({static_cast<float>(m_width), static_cast<float>(m_height)});
        const Vector2 point{request.pixel.x, static_cast<float>(m_height) - request.pixel.y};
        const auto entries = ui.GetEntries();
        for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry) {
            if (entry->entity != entry->canvas && entry->bounds.Contains(point) && entry->clip.Contains(point)) {
                ApplyPick(request, entry->entity);
                return;
            }
        }
    }
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
        case AssetType::AnimationClip:
            m_pick = PickRequest{.pixel = {(mouse.x - origin.x) * scale.x, (mouse.y - origin.y) * scale.y},
                                 .clip = asset,
                                 .project = project};
            return;
        default:
            CreateEntityFromAsset(*m_shown, *project, asset, EntityId::Invalid, GetGroundPoint(origin, size));
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
    // 2D views look at the XY plane, where the mouse always points.
    if (m_camera.Is2D()) {
        return {from.x, from.y, 0.0f};
    }
    constexpr float MaxDistance = 500.0f;
    if (direction.y < -1e-6f) {
        const Vector3 hit = from + direction * (-from.y / direction.y);
        if (Length(hit - camera.position) < MaxDistance) {
            return hit;
        }
    }
    return m_camera.GetFocus();
}

void SceneView::ApplyPick(const PickRequest& request, EntityId entity) {
    if (request.material.IsValid()) {
        if (entity != EntityId::Invalid && ApplyMaterial(*m_shown, entity, request.material)) {
            m_shown->GetSelection().Select(entity);
        }
        return;
    }
    if (request.clip.IsValid()) {
        if (entity != EntityId::Invalid && request.project &&
            ApplyAnimationClip(*m_shown, *request.project, entity, request.clip)) {
            m_shown->GetSelection().Select(entity);
        }
        return;
    }
    Selection& selection = m_shown->GetSelection();
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
    const ImVec2 windowCenter{ImGui::GetItemRectMin().x + ImGui::GetItemRectSize().x * 0.5f,
                              ImGui::GetItemRectMin().y + ImGui::GetItemRectSize().y * 0.5f};
    input.cursor = {io.MousePos.x - windowCenter.x, io.MousePos.y - windowCenter.y};
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
    m_shown->GetScene().ForEachEntity([&](Entity& entity) {
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
        const bool selected = m_shown->GetSelection().Contains(entity.GetId());
        m_icons.push_back({entity.GetId(), corner, {corner.x + textSize.x, corner.y + textSize.y}});
        drawList->AddText(ImGui::GetFont(), iconSize, {corner.x + 1.0f, corner.y + 1.0f}, IM_COL32(0, 0, 0, 160), icon);
        drawList->AddText(ImGui::GetFont(), iconSize, corner,
                          selected ? selectedColor : IM_COL32(235, 225, 170, entity.IsActiveInHierarchy() ? 230 : 90),
                          icon);
    });
    drawList->PopClipRect();
}

void SceneView::AddMeshes(const Entity& entity, std::vector<MeshInstance>& meshes) const {
    if (!entity.IsActiveInHierarchy()) {
        return;
    }
    for (const MeshRenderer* renderer : entity.GetComponents<MeshRenderer>()) {
        if (renderer->IsEnabled() && renderer->GetMesh()) {
            meshes.push_back(
                {renderer->GetMesh().Get(), entity.GetTransform().GetWorldMatrix(), renderer->GetSubmesh()});
        }
    }
    for (const auto* skin : entity.GetComponents<SkinnedMeshRenderer>()) {
        if (skin->IsActiveAndEnabled() && skin->GetMesh()) {
            meshes.push_back({skin->GetMesh().Get(), entity.GetTransform().GetWorldMatrix(), skin->GetSubmesh(),
                              skin->GetPalette()});
        }
    }
    for (const SpriteRenderer* sprite : entity.GetComponents<SpriteRenderer>()) {
        if (sprite->IsEnabled() && m_plane) {
            meshes.push_back({m_plane.Get(), GetSpriteMatrix(*sprite)});
        }
    }
    for (const auto* map : entity.GetComponents<Tilemap>()) {
        if (!map->IsActiveAndEnabled() || !m_plane)
            continue;
        const auto& data = map->GetData();
        const Vector3 size{static_cast<float>(data.width) * data.cellSize.x,
                           static_cast<float>(data.height) * data.cellSize.y, 1};
        const auto matrix = entity.GetTransform().GetWorldMatrix() *
                            Matrix4::Translation({size.x * 0.5f, size.y * 0.5f, 0}) * Matrix4::Scale(size) *
                            Matrix4::Rotation(Quaternion::FromAxisAngle({1, 0, 0}, ToRadians(90.0f)));
        meshes.push_back({m_plane.Get(), matrix});
    }
    for (const Entity* child : entity.GetChildren()) {
        AddMeshes(*child, meshes);
    }
}

void SceneView::DrawCameraFrustums(const ImVec2& origin, const ImVec2& size) {
    const Matrix4 viewProjection = m_camera.GetCamera().GetViewProjection({size.x, size.y});
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    const float thickness = ImGui::GetStyle().FontScaleDpi;
    m_shown->GetScene().ForEachComponent<Camera>([&](Camera& component) {
        const Camera3D camera = component.GetCamera3D();
        // Out to a few units, past which the lines would only clutter the view.
        const float depth = std::min(camera.farPlane, camera.orthographic ? camera.orthographicSize * 4.0f : 4.0f);
        const auto rectangle = [&](float distance) {
            const float halfHeight =
                camera.orthographic ? camera.orthographicSize : std::tan(camera.fieldOfView * 0.5f) * distance;
            const float halfWidth = halfHeight * GameAspectRatio;
            std::array<Vector3, 4> corners;
            const std::array<Vector2, 4> signs{{{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}}};
            for (std::size_t i = 0; i < 4; ++i) {
                corners[i] = camera.position +
                             camera.rotation * Vector3{signs[i].x * halfWidth, signs[i].y * halfHeight, -distance};
            }
            return corners;
        };
        const auto toScreen = [&](Vector3 point, ImVec2& screen) {
            const Vector4 clip = viewProjection * Vector4{point.x, point.y, point.z, 1.0f};
            if (clip.w <= 1e-4f) {
                return false;
            }
            screen = {origin.x + (clip.x / clip.w * 0.5f + 0.5f) * size.x,
                      origin.y + (0.5f - clip.y / clip.w * 0.5f) * size.y};
            return true;
        };
        const bool selected = m_shown->GetSelection().Contains(component.GetEntity().GetId());
        const ImU32 color = selected ? ImGui::GetColorU32(EditorColors::Accent) : IM_COL32(200, 200, 210, 140);
        const std::array nearCorners = rectangle(camera.nearPlane);
        const std::array farCorners = rectangle(depth);
        const auto line = [&](Vector3 a, Vector3 b) {
            ImVec2 from;
            ImVec2 to;
            if (toScreen(a, from) && toScreen(b, to)) {
                drawList->AddLine(from, to, color, thickness);
            }
        };
        for (std::size_t i = 0; i < 4; ++i) {
            line(nearCorners[i], nearCorners[(i + 1) % 4]);
            line(farCorners[i], farCorners[(i + 1) % 4]);
            line(nearCorners[i], farCorners[i]);
        }
    });
    drawList->PopClipRect();
}

void SceneView::StopShowingPlay() {
    m_uiRectGizmo.Cancel(*m_shown);
    m_gameRenderer.reset();
    m_playing = false;
    m_shown = &m_document;
    // Picks in flight hold the copy's entities.
    m_pick.reset();
    m_picking.reset();
}

void SceneView::DrawOverlay(const ImVec2& origin) {
    // A small bar over the view's top-left corner, for what changes how the view looks.
    const ImVec2 padding = ImGui::GetStyle().WindowPadding;
    ImGui::SetCursorScreenPos({origin.x + padding.x, origin.y + padding.y});
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.11f, 0.13f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.21f, 0.24f, 0.90f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * ImGui::GetStyle().FontScaleDpi);
    if (m_playing) {
        // Playing views switch between the game's camera and the editor's.
        if (ImGui::Button(m_gameView ? ICON_FA_GAMEPAD "  Game" : ICON_FA_VIDEO "  Scene")) {
            m_gameView = !m_gameView;
        }
        ImGui::SetItemTooltip(m_gameView ? "Showing the game through its main camera; it gets the keyboard and the "
                                           "mouse while this view has focus, and Shift+F1 takes them back\nClick to "
                                           "look around with the editor's camera"
                                         : "Showing the game through the editor's camera\nClick to show it as the "
                                           "game does");
        if (m_gameView) {
            if (!FindMainCamera(m_shown->GetScene())) {
                ImGui::SameLine();
                ImGui::TextColored(EditorColors::Warning, ICON_FA_TRIANGLE_EXCLAMATION "  No camera in the scene");
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            return;
        }
        ImGui::SameLine();
    }
    const bool view2D = m_camera.Is2D();
    ImGui::PushStyleColor(ImGuiCol_Text, view2D ? EditorColors::Accent : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    if (ImGui::Button(view2D ? "2D" : "3D")) {
        m_camera.Set2D(!view2D);
    }
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip(view2D ? "2D: looking at the XY plane; drag with the right or middle button to move, and "
                                   "use the wheel to zoom\nClick for 3D"
                                 : "3D: looking around the scene\nClick for 2D, looking at the XY plane");
    ImGui::SameLine();
    const bool gridShown = m_options.showGrid;
    ImGui::PushStyleColor(ImGuiCol_Text, gridShown ? EditorColors::Accent : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    if (ImGui::Button(ICON_FA_TABLE_CELLS)) {
        m_options.showGrid = !gridShown;
    }
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip(gridShown ? "Hide the grid" : "Show the grid");
    ImGui::SameLine();
    const bool collidersShown = m_options.showColliders;
    ImGui::PushStyleColor(ImGuiCol_Text,
                          collidersShown ? EditorColors::Accent : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    if (ImGui::Button(ICON_FA_SHAPES)) {
        m_options.showColliders = !collidersShown;
    }
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip(collidersShown ? "Hide the colliders" : "Show the colliders, as physics sees them");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text,
                          m_options.drawUi ? EditorColors::Accent : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    if (ImGui::Button("UI")) {
        m_uiRectGizmo.Finish(*m_shown);
        m_options.drawUi = !m_options.drawUi;
    }
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("Preview screen-space canvases and edit their rectangles");
    if (m_options.drawUi)
        m_uiRectGizmo.DrawAnchorMenu(*m_shown, {static_cast<float>(m_width), static_cast<float>(m_height)});
    // Only 3D views fly.
    if (!view2D) {
        ImGui::SameLine();
        DrawSpeedButton();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void SceneView::DrawSpeedButton() {
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
}

void SceneView::DrawPlayPending(EditorContext& context, const ImVec2& origin, const ImVec2& size) {
    const CodeBuild& build = context.GetProject()->GetModules().GetBuild();
    const std::string title = build.IsRunning()
                                  ? std::format("Building the game's code  {:.0f} s", build.GetDuration().count())
                                  : std::string("Loading the game's code");
    constexpr const char* Detail = "The scene plays as soon as it is ready; Ctrl+P cancels";

    const float fontSize = ImGui::GetFontSize();
    const float padding = fontSize;
    const float radius = fontSize * 0.6f;
    const float gap = fontSize * 0.75f;
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    const ImVec2 titleSize = ImGui::CalcTextSize(title.c_str());
    const ImVec2 detailSize = ImGui::CalcTextSize(Detail);
    const ImVec2 card{padding * 2.0f + radius * 2.0f + gap + std::max(titleSize.x, detailSize.x),
                      padding * 2.0f + titleSize.y + spacing + detailSize.y};
    const ImVec2 min{std::round(origin.x + (size.x - card.x) * 0.5f), std::round(origin.y + (size.y - card.y) * 0.5f)};
    const ImVec2 max{min.x + card.x, min.y + card.y};

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    drawList->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(0, 0, 0, 90));
    const float rounding = 6.0f * ImGui::GetStyle().FontScaleDpi;
    drawList->AddRectFilled(min, max, IM_COL32(26, 28, 33, 235), rounding);
    drawList->AddRect(min, max, IM_COL32(255, 255, 255, 30), rounding);

    // It turns, so the editor is seen not to hang while the build takes its time.
    const ImVec2 center{min.x + padding + radius, (min.y + max.y) * 0.5f};
    const float thickness = std::max(2.0f, fontSize * 0.15f);
    drawList->AddCircle(center, radius, IM_COL32(255, 255, 255, 40), 32, thickness);
    const auto angle = static_cast<float>(ImGui::GetTime() * 5.0);
    drawList->PathArcTo(center, radius, angle, angle + 1.6f * std::numbers::pi_v<float>, 32);
    drawList->PathStroke(ImGui::GetColorU32(EditorColors::Accent), thickness);

    const float textX = center.x + radius + gap;
    drawList->AddText({textX, min.y + padding}, ImGui::GetColorU32(ImGuiCol_Text), title.c_str());
    drawList->AddText({textX, min.y + padding + titleSize.y + spacing}, ImGui::GetColorU32(EditorColors::TextMuted),
                      Detail);
    drawList->PopClipRect();
}

void SceneView::Render(RenderContext& context, ViewportRenderer& renderer) {
    if (m_width == 0 || m_height == 0 || !m_targets.GetColor()) {
        return;
    }
    // The game's view looks through its main camera, as the game would, without the editor's aids.
    if (IsShowingGame()) {
        Scene& scene = m_shown->GetScene();
        const auto revision = scene.IsLoaded() ? Application::Get().GetRenderingRevision() : 0;
        if (!m_gameRenderer || m_gameRenderingRevision != revision) {
            SceneRendererConfig config{.targetFormat = ViewportRenderer::ColorFormat};
            if (scene.IsLoaded())
                Application::Get().GetRenderingSettings().Apply(config);
            else
                m_rendering.Apply(config);
            m_gameRenderer = std::make_unique<SceneRenderer>(renderer.GetDevice(), config);
            m_gameRenderingRevision = revision;
        }
        m_gameRenderer->Render(context, scene, m_targets.GetColor());
        return;
    }
    const Camera3D camera = m_camera.GetCamera();
    ViewportOptions options = m_options;
    options.tonemapping = m_rendering.tonemapping;
    options.grading = m_rendering.grading;
    options.bloom = m_rendering.bloom;
    options.fxaa = m_rendering.fxaa;
    m_colliderLines.Clear();
    if (options.showColliders) {
        DrawColliders(m_shown->GetScene(), m_colliderLines);
        options.lines = m_colliderLines.GetLines();
    }
    renderer.Render(context, m_targets, m_shown->GetScene(), camera, options);

    if (!m_plane) {
        m_plane = Assets::Load<Mesh>(BuiltInAssets::PlaneMesh);
    }
    std::vector<MeshInstance> selected;
    for (const Entity* entity : GetSelectedEntities(*m_shown)) {
        AddMeshes(*entity, selected);
    }
    renderer.DrawOutline(context, m_targets, camera, selected, SelectionOutlineColor, m_outlineWidth);

    if (m_pick) {
        std::vector<MeshInstance> meshes;
        m_pickEntities.clear();
        m_shown->GetScene().ForEachEntity([&](Entity& entity) {
            if (!entity.IsActiveInHierarchy()) {
                return;
            }
            for (const MeshRenderer* meshRenderer : entity.GetComponents<MeshRenderer>()) {
                if (meshRenderer->IsEnabled() && meshRenderer->GetMesh()) {
                    meshes.push_back({meshRenderer->GetMesh().Get(), entity.GetTransform().GetWorldMatrix(),
                                      meshRenderer->GetSubmesh()});
                }
            }
            for (const auto* skin : entity.GetComponents<SkinnedMeshRenderer>()) {
                if (skin->IsActiveAndEnabled() && skin->GetMesh()) {
                    meshes.push_back({skin->GetMesh().Get(), entity.GetTransform().GetWorldMatrix(), skin->GetSubmesh(),
                                      skin->GetPalette()});
                }
            }
            for (const SpriteRenderer* sprite : entity.GetComponents<SpriteRenderer>()) {
                if (sprite->IsEnabled() && m_plane) {
                    meshes.push_back({m_plane.Get(), GetSpriteMatrix(*sprite)});
                }
            }
            for (const auto* map : entity.GetComponents<Tilemap>()) {
                if (!map->IsActiveAndEnabled() || !m_plane)
                    continue;
                const auto& data = map->GetData();
                const Vector3 size{static_cast<float>(data.width) * data.cellSize.x,
                                   static_cast<float>(data.height) * data.cellSize.y, 1};
                meshes.push_back(
                    {m_plane.Get(), entity.GetTransform().GetWorldMatrix() *
                                        Matrix4::Translation({size.x * 0.5f, size.y * 0.5f, 0}) * Matrix4::Scale(size) *
                                        Matrix4::Rotation(Quaternion::FromAxisAngle({1, 0, 0}, ToRadians(90.0f)))});
            }
            m_pickEntities.resize(meshes.size(), entity.GetId());
        });
        renderer.RenderPick(context, camera, {static_cast<float>(m_width), static_cast<float>(m_height)}, m_pick->pixel,
                            meshes);
        m_picking = std::exchange(m_pick, std::nullopt);
    }
}

void SceneView::FrameSelection() {
    Scene& scene = m_shown->GetScene();
    std::vector<BoundingSphere> spheres;
    for (const EntityId id : m_shown->GetSelection().GetEntities()) {
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
