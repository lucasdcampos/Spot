#include "spot/scene/rendering/scene_drawing.h"

#include "spot/core/debug/profiler.h"
#include "spot/render/material.h"
#include "spot/render/particle_renderer.h"
#include "spot/render/renderer_2d.h"
#include "spot/render/renderer_3d.h"
#include "spot/scene/animation/skinned_mesh_renderer.h"
#include "spot/scene/entity.h"
#include "spot/scene/particles/particle_system.h"
#include "spot/scene/rendering/fog.h"
#include "spot/scene/rendering/light.h"
#include "spot/scene/rendering/mesh_renderer.h"
#include "spot/scene/rendering/reflection_probe.h"
#include "spot/scene/rendering/render_world_system.h"
#include "spot/scene/rendering/sky.h"
#include "spot/scene/rendering/sprite_renderer.h"
#include "spot/scene/rendering/surface_renderer.h"
#include "spot/scene/rendering/text_renderer.h"
#include "spot/scene/rendering/tilemap.h"
#include "spot/scene/scene.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Spot {

namespace {

struct SpriteDraw {
    const SpriteRenderer* sprite = nullptr;
    const Tilemap* tilemap = nullptr;
    Vector3 position = {};
    float rotation = 0;
    Vector2 size = {};
    int order = 0;
    std::size_t index = 0;
};

} // namespace

void DrawScene(Renderer3D& meshes, Scene& scene) {
    SPOT_PROFILE_SCOPE("DrawScene (meshes)");
    static const Material DefaultMaterial;
    const Sky* sky = nullptr;
    scene.ForEachComponent<Sky>([&sky](const Sky& found) { sky = sky ? sky : &found; });
    // The sky lights the scene with its sun itself, once the air has dimmed it.
    const DirectionalLight* sun = sky ? sky->FindSun() : nullptr;
    scene.ForEachComponent<Light>([&meshes, sun](const Light& light) {
        if (&light != sun) {
            meshes.DrawLight(light.GetLightData());
        }
    });
    if (sky) {
        meshes.DrawSky(sky->GetSkyData());
    }
    const Fog* fog = nullptr;
    scene.ForEachComponent<Fog>([&fog](const Fog& found) { fog = fog ? fog : &found; });
    if (fog) {
        meshes.DrawFog(fog->GetFogData());
    }
    scene.ForEachComponent<ReflectionProbe>(
        [&meshes](const ReflectionProbe& probe) { meshes.DrawReflectionProbe(probe.GetProbeData()); });
    scene.ForEachComponent<SkinnedMeshRenderer>([&meshes](const SkinnedMeshRenderer& renderer) {
        if (renderer.GetMesh() && !renderer.GetPalette().empty() &&
            ((meshes.GetCamera().cullingMask >> renderer.GetEntity().GetLayer()) & 1u)) {
            meshes.DrawSkinnedMesh(*renderer.GetMesh(),
                                   renderer.GetMaterial() ? *renderer.GetMaterial() : DefaultMaterial,
                                   renderer.GetPalette(), renderer.GetEntity().GetTransform().GetWorldMatrix(),
                                   renderer.GetBoundsPadding(), renderer.IsCastingShadows(), renderer.GetSubmesh());
        }
    });
    scene.ForEachComponent<SurfaceRenderer>([&meshes](const SurfaceRenderer& renderer) {
        if (renderer.GetSurface() && ((meshes.GetCamera().cullingMask >> renderer.GetEntity().GetLayer()) & 1u)) {
            meshes.DrawSurface(*renderer.GetSurface());
        }
    });
    RenderWorldSystem* world = scene.GetSystem<RenderWorldSystem>();
    if (!world) {
        world = &scene.AddSystem<RenderWorldSystem>();
    }
    meshes.DrawWorld(world->Sync());
}

void DrawScene(Renderer2D& renderer, Scene& scene) {
    SPOT_PROFILE_SCOPE("DrawScene (sprites)");
    std::vector<SpriteDraw> draws;
    scene.ForEachEntity([&](Entity& entity) {
        if (!entity.IsActiveInHierarchy())
            return;
        const auto& transform = entity.GetTransform();
        const auto angle = transform.GetWorldRotation2D();
        const auto& matrix = transform.GetWorldMatrix();
        const Vector2 right{std::cos(angle), std::sin(angle)}, up{-right.y, right.x};
        const Vector2 scale{Dot(Vector2{matrix.columns[0].x, matrix.columns[0].y}, right),
                            Dot(Vector2{matrix.columns[1].x, matrix.columns[1].y}, up)};
        entity.ForEachComponent<Component>([&](Component& component) {
            if (!component.IsActiveAndEnabled())
                return;
            if (const auto* sprite = dynamic_cast<SpriteRenderer*>(&component)) {
                Vector2 size{sprite->GetSize().x * scale.x, sprite->GetSize().y * scale.y};
                if (sprite->IsFlippedX())
                    size.x = -size.x;
                if (sprite->IsFlippedY())
                    size.y = -size.y;
                draws.push_back({sprite, nullptr, transform.GetWorldPosition(), transform.GetWorldRotation2D(), size,
                                 sprite->GetOrder(), draws.size()});
            } else if (const auto* map = dynamic_cast<Tilemap*>(&component)) {
                draws.push_back({nullptr,
                                 map,
                                 transform.GetWorldPosition(),
                                 transform.GetWorldRotation2D(),
                                 {scale.x, scale.y},
                                 map->GetOrder(),
                                 draws.size()});
            }
        });
    });
    std::ranges::sort(draws, [](const SpriteDraw& a, const SpriteDraw& b) {
        if (a.order != b.order)
            return a.order < b.order;
        if (a.position.z != b.position.z)
            return a.position.z < b.position.z;
        return a.index < b.index;
    });
    float depth = 0;
    for (const auto& item : draws) {
        const float nextDepth = renderer.IsDrawingInWorld() ? item.position.z : 0;
        if (nextDepth != depth) {
            depth = nextDepth;
            renderer.SetTransform(Matrix4::Translation({0, 0, depth}));
        }
        if (item.sprite) {
            const Vector2 position{item.position.x, item.position.y};
            if (const auto* texture = item.sprite->GetDrawTexture())
                renderer.DrawRotatedQuad(position, item.size, item.rotation, *texture, item.sprite->GetColor(),
                                         item.sprite->GetRegion());
            else
                renderer.DrawRotatedQuad(position, item.size, item.rotation, item.sprite->GetColor());
        } else {
            const auto& data = item.tilemap->GetData();
            const auto* atlas = item.tilemap->GetAtlas().Get();
            const auto* texture = atlas ? atlas->texture.Get() : nullptr;
            const auto& matrix = item.tilemap->GetEntity().GetTransform().GetWorldMatrix();
            for (int y = 0; y < data.height; ++y)
                for (int x = 0; x < data.width; ++x) {
                    const int tile = data.GetCell(x, y);
                    if (tile < 0 || static_cast<std::size_t>(tile) >= data.definitions.size())
                        continue;
                    const int frame = data.definitions[static_cast<std::size_t>(tile)].sprite;
                    if (atlas && (frame < 0 || static_cast<std::size_t>(frame) >= atlas->sprites.size()))
                        continue;
                    const auto world = matrix * Vector4{(static_cast<float>(x) + 0.5f) * data.cellSize.x,
                                                        (static_cast<float>(y) + 0.5f) * data.cellSize.y, 0, 1};
                    const Vector2 position{world.x, world.y};
                    if (texture)
                        renderer.DrawRotatedQuad(position, data.cellSize * item.size, item.rotation, *texture,
                                                 item.tilemap->GetColor(), atlas->GetRegion(frame));
                    else
                        renderer.DrawRotatedQuad(position, data.cellSize * item.size, item.rotation,
                                                 item.tilemap->GetColor());
                }
        }
    }
    if (depth != 0) {
        renderer.SetTransform({});
    }
}

void DrawWorldText(Renderer2D& renderer, Scene& scene, const Camera3D& camera) {
    SPOT_PROFILE_SCOPE("DrawWorldText");
    std::vector<TextRenderer*> texts;
    scene.ForEachComponent<TextRenderer>([&](TextRenderer& text) { texts.push_back(&text); });
    std::stable_sort(texts.begin(), texts.end(),
                     [](const auto* a, const auto* b) { return a->GetOrder() < b->GetOrder(); });
    for (const auto* text : texts) {
        if (!text->GetFont())
            continue;
        renderer.SetTransform(text->GetDrawMatrix(camera));
        renderer.DrawString(*text->GetFont(), text->GetText(), {}, text->GetColor(), text->GetPixelScale());
    }
    if (!texts.empty()) {
        renderer.SetTransform({});
    }
}

void DrawScene(ParticleRenderer& renderer, Scene& scene) {
    std::vector<Particle> particles;
    scene.ForEachComponent<ParticleSystem>([&](ParticleSystem& system) {
        if (!system.IsActiveAndEnabled() ||
            !(renderer.GetCamera().cullingMask & (1u << system.GetEntity().GetLayer()))) {
            return;
        }
        const auto& settings = system.GetSettings();
        const auto simulated = system.GetParticles();
        particles.resize(simulated.size());
        for (std::size_t i = 0; i < simulated.size(); ++i) {
            particles[i] = {.position = simulated[i].position,
                            .velocity = simulated[i].velocity,
                            .age = simulated[i].age,
                            .lifetime = simulated[i].lifetime,
                            .size = simulated[i].size,
                            .rotation = simulated[i].rotation,
                            .color = simulated[i].color};
        }
        const ParticleDrawSettings draw{
            .facing = static_cast<ParticleDrawFacing>(settings.facing),
            .blend = static_cast<ParticleDrawBlend>(settings.blend),
            .velocityStretch = settings.velocityStretch,
            .softDistance = settings.softDistance,
            .flipbookColumns = settings.flipbookColumns,
            .flipbookRows = settings.flipbookRows,
            .flipbookCycles = settings.flipbookCycles,
        };
        const Matrix4 emitter =
            settings.space == ParticleSpace::Local ? system.GetEntity().GetTransform().GetWorldMatrix() : Matrix4{};
        renderer.Draw(particles, draw, emitter, system.GetTexture().Get());
    });
}

} // namespace Spot
