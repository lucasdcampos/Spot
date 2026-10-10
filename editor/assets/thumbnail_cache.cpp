#include "assets/thumbnail_cache.h"

#include "project/editor_project.h"

#include <algorithm>
#include <cmath>

namespace Spot::Editor {

namespace {

constexpr TextureFormat PictureFormat = TextureFormat::Rgba8Srgb;

// A sphere around all the spheres.
BoundingSphere Enclose(const std::vector<BoundingSphere>& spheres) {
    if (spheres.empty()) {
        return {{}, 0.5f};
    }
    Vector3 center{};
    for (const BoundingSphere& sphere : spheres) {
        center = center + sphere.center;
    }
    center = center * (1.0f / static_cast<float>(spheres.size()));
    float radius = 0.0f;
    for (const BoundingSphere& sphere : spheres) {
        radius = std::max(radius, Length(sphere.center - center) + sphere.radius);
    }
    return {center, std::max(radius, 0.001f)};
}

} // namespace

ThumbnailCache::ThumbnailCache(GraphicsDevice& device) : m_device(device) {
    m_sampleCount = device.SupportsSampleCount(PictureFormat, 4) ? 4 : 1;
    // Shadows would only darken the small pictures' undersides.
    m_renderer = std::make_unique<Renderer3D>(device, Renderer3DConfig{.targetFormat = PictureFormat,
                                                                       .sampleCount = m_sampleCount,
                                                                       .shadowMapSize = 0,
                                                                       .localShadowMapSize = 0});
    if (m_sampleCount > 1) {
        m_samples = device.CreateTexture({.width = Size,
                                          .height = Size,
                                          .format = PictureFormat,
                                          .renderTarget = true,
                                          .sampleCount = m_sampleCount});
    }
    m_depth = device.CreateTexture({.width = Size,
                                    .height = Size,
                                    .format = TextureFormat::Depth32Float,
                                    .renderTarget = true,
                                    .sampleCount = m_sampleCount});
}

ThumbnailCache::~ThumbnailCache() = default;

const Texture* ThumbnailCache::Get(EditorProject& project, AssetId id, AssetType type) {
    if (type != AssetType::Texture && type != AssetType::Material && type != AssetType::Mesh &&
        type != AssetType::Model) {
        return nullptr;
    }
    Entry& entry = m_entries[id];
    entry.type = type;
    const bool outdated = entry.version != project.GetVersion() || (!entry.picture && !entry.texture);
    if (outdated && !entry.queued) {
        entry.queued = true;
        m_queue.push_back(id);
    }
    return type == AssetType::Texture ? entry.texture.Get() : entry.picture.get();
}

void ThumbnailCache::Render(RenderContext& context, EditorProject& project) {
    if (!m_renderer->IsValid() || !m_depth || (m_sampleCount > 1 && !m_samples)) {
        m_queue.clear();
        return;
    }
    AssetManager& assets = project.GetAssets();
    std::size_t drawn = 0;
    while (!m_queue.empty() && drawn < DrawnPerFrame) {
        const AssetId id = m_queue.front();
        m_queue.erase(m_queue.begin());
        const auto found = m_entries.find(id);
        if (found == m_entries.end()) {
            continue;
        }
        Entry& entry = found->second;
        entry.queued = false;
        entry.version = project.GetVersion();
        ++drawn;

        switch (entry.type) {
        case AssetType::Texture:
            entry.texture = assets.Load<Texture>(id);
            break;
        case AssetType::Material: {
            const AssetRef<Material> material = assets.Load<Material>(id);
            const AssetRef<Mesh> sphere = assets.Load<Mesh>(BuiltInAssets::SphereMesh);
            if (material && sphere) {
                Draw(context, entry, {{sphere.Get(), material.Get()}});
            }
            break;
        }
        case AssetType::Mesh: {
            static const Material Plain{.baseColor = Color{0.78f, 0.79f, 0.82f, 1.0f}, .roughness = 0.6f};
            if (const AssetRef<Mesh> mesh = assets.Load<Mesh>(id)) {
                Draw(context, entry, {{mesh.Get(), &Plain}});
            }
            break;
        }
        case AssetType::Model: {
            static const Material Missing{};
            const AssetRef<Model> model = assets.Load<Model>(id);
            std::vector<std::pair<const Mesh*, const Material*>> parts;
            if (model) {
                for (const ModelPart& part : model->GetParts()) {
                    if (part.mesh) {
                        parts.emplace_back(part.mesh.Get(), part.material ? part.material.Get() : &Missing);
                    }
                }
            }
            if (!parts.empty()) {
                Draw(context, entry, parts);
            }
            break;
        }
        default:
            break;
        }
    }
}

void ThumbnailCache::Clear() {
    m_entries.clear();
    m_queue.clear();
}

void ThumbnailCache::Draw(RenderContext& context, Entry& entry,
                          const std::vector<std::pair<const Mesh*, const Material*>>& parts) {
    if (!entry.picture) {
        entry.picture =
            m_device.CreateTexture({.width = Size, .height = Size, .format = PictureFormat, .renderTarget = true});
        if (!entry.picture) {
            return;
        }
    }
    std::vector<BoundingSphere> bounds;
    for (const auto& [mesh, material] : parts) {
        bounds.push_back(mesh->GetBounds());
    }
    const BoundingSphere sphere = Enclose(bounds);

    // From above and to the side, so shapes read in three dimensions, close enough to fill the picture.
    Camera3D camera;
    camera.fieldOfView = ToRadians(30.0f);
    const float distance = sphere.radius / std::sin(camera.fieldOfView * 0.5f) * 1.05f;
    camera.position = sphere.center + Normalize(Vector3{0.9f, 0.75f, 1.4f}) * distance;
    camera.nearPlane = std::max(distance - sphere.radius * 1.5f, distance * 0.01f);
    camera.farPlane = distance + sphere.radius * 2.0f;
    camera.LookAt(sphere.center);

    m_renderer->Begin(context,
                      {.target = m_samples ? m_samples.get() : entry.picture.get(),
                       .clearColor = Color{0.0f, 0.0f, 0.0f, 0.0f},
                       .depthTarget = m_depth.get(),
                       .clearDepth = 1.0f,
                       .resolveTarget = m_samples ? entry.picture.get() : nullptr},
                      camera);
    // A studio's lights: a warm key from above, a cool fill from the other side, and some ambient.
    m_renderer->DrawLight(
        {.type = LightType::Directional, .color = Color{1.0f, 0.96f, 0.9f, 1.0f}, .direction = {-0.5f, -1.0f, -0.6f}});
    m_renderer->DrawLight(
        {.type = LightType::Directional, .color = Color{0.25f, 0.3f, 0.4f, 1.0f}, .direction = {0.7f, -0.2f, 0.4f}});
    m_renderer->DrawLight({.type = LightType::Ambient, .color = Color{0.3f, 0.32f, 0.36f, 1.0f}});
    for (const auto& [mesh, material] : parts) {
        m_renderer->DrawMesh(*mesh, *material, Matrix4::Identity(), false);
    }
    m_renderer->End();
}

} // namespace Spot::Editor
