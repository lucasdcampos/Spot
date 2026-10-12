#include "menu_thumbnails.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {
std::unique_ptr<Spot::Scene> Preview(const Spot::AssetRef<Spot::Prefab>& prefab) {
    auto scene = std::make_unique<Spot::Scene>();
    auto* root = scene->Instantiate(prefab);
    if (!root)
        return nullptr;
    root->GetTransform().SetPosition({});
    Spot::Vector3 min{}, max{};
    bool first = true;
    scene->ForEachComponent<Spot::MeshRenderer>([&](Spot::MeshRenderer& renderer) {
        if (!renderer.GetMesh())
            return;
        const auto world = renderer.GetEntity().GetTransform().GetWorldMatrix();
        const auto bounds = renderer.GetMesh()->GetBounds();
        const auto scale = Spot::Decompose(world).scale;
        const float radius = bounds.radius * std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)});
        const auto point = world * Spot::Vector4{bounds.center.x, bounds.center.y, bounds.center.z, 1};
        const Spot::Vector3 center{point.x, point.y, point.z};
        const auto a = center - Spot::Vector3{radius, radius, radius},
                   b = center + Spot::Vector3{radius, radius, radius};
        if (first) {
            min = a;
            max = b;
            first = false;
        } else {
            min = {std::min(min.x, a.x), std::min(min.y, a.y), std::min(min.z, a.z)};
            max = {std::max(max.x, b.x), std::max(max.y, b.y), std::max(max.z, b.z)};
        }
    });
    const auto center = (min + max) * 0.5f;
    const float radius = std::max(0.5f, Spot::Length(max - min) * 0.35f);
    Spot::Camera3D view;
    view.position = center + Spot::Normalize(Spot::Vector3{2, 1.5f, 2.5f}) * radius * 3;
    view.LookAt(center);
    auto& cameraEntity = scene->CreateEntity("Thumbnail Camera");
    cameraEntity.GetTransform().SetPosition(view.position);
    cameraEntity.GetTransform().SetRotation(view.rotation);
    auto& camera = cameraEntity.AddComponent<Spot::Camera>();
    camera.SetProjection(Spot::CameraProjection::Orthographic);
    camera.SetOrthographicSize(radius * 1.2f);
    camera.SetNearPlane(0.01f);
    camera.SetFarPlane(radius * 20 + 10);
    camera.SetBackground({0.06f, 0.075f, 0.1f, 1});
    scene->CreateEntity("Thumbnail Ambient").AddComponent<Spot::AmbientLight>(Spot::Color::White, 0.6f);
    auto& light = scene->CreateEntity("Thumbnail Light");
    light.GetTransform().SetRotation(Spot::Quaternion::LookRotation({-0.5f, -1, -0.6f}));
    light.AddComponent<Spot::DirectionalLight>().SetCastingShadows(false);
    return scene;
}
} // namespace

void MenuThumbnails::Add(Spot::EntityId image, Spot::AssetRef<Spot::Prefab> prefab) {
    m_entries.push_back({image, std::move(prefab), {}, {}, false});
}
void MenuThumbnails::Clear() {
    m_entries.clear();
}
std::size_t MenuThumbnails::GetCompletedCount() const {
    return static_cast<std::size_t>(std::ranges::count(m_entries, true, &Entry::ready));
}
void MenuThumbnails::Render(const Spot::RenderStageContext& stage, Spot::Scene& interfaceScene, unsigned int budget) {
    SPOT_PROFILE_SCOPE("Sandbox::Thumbnails");
    for (auto& entry : m_entries) {
        if (budget == 0)
            break;
        if (entry.ready)
            continue;
        auto* entity = interfaceScene.GetEntity(entry.image);
        auto* image = entity ? entity->GetComponent<Spot::UiImage>() : nullptr;
        if (!image) {
            entry.ready = true;
            continue;
        }
        if (!image->IsActiveAndEnabled())
            continue;
        if (!m_renderer)
            m_renderer = std::make_unique<Spot::SceneRenderer>(
                stage.device, Spot::SceneRendererConfig{.targetFormat = Spot::TextureFormat::Rgba8,
                                                        .sampleCount = 1,
                                                        .shadowMapSize = 0,
                                                        .localShadowMapSize = 0});
        if (!entry.preview)
            entry.preview = Preview(entry.prefab);
        auto texture = stage.device.CreateTexture(
            {.width = 128, .height = 128, .format = Spot::TextureFormat::Rgba8, .renderTarget = true});
        if (!entry.preview || !texture || !m_renderer->IsValid()) {
            Spot::Log::Warning("Could not render a spawn-menu thumbnail");
            entry.ready = true;
            continue;
        }
        const std::array scenes{entry.preview.get()};
        m_renderer->Render(stage.context, scenes, texture.get());
        entry.texture = Spot::AssetRef<Spot::Texture>(std::move(texture));
        image->texture = entry.texture;
        entry.ready = true;
        --budget;
    }
}
