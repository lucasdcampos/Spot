#pragma once

namespace Spot {

class ParticleRenderer;
class Renderer2D;
class Renderer3D;
class Scene;
struct Camera3D;

// The scene's components drawn with the code-only renderers, through their public API alone, as SceneRenderer and the
// editor's views do. Games that keep their objects elsewhere, such as in an ECS of their own, draw them the same way.

// Every MeshRenderer, SkinnedMeshRenderer and Light that is enabled on an active entity, at its entity's world
// transform, and the first such Sky and Fog. The scene must not change until End(), since its components keep the
// meshes alive.
void DrawScene(Renderer3D& renderer, Scene& scene);
// Every SpriteRenderer and Tilemap that is enabled on an active entity, at its entity's world position, rotation
// around Z and scale, in their order, and of the same order the lower ones along Z first. The scene must not change
// until End(), since its sprites keep their textures alive.
void DrawScene(Renderer2D& renderer, Scene& scene);
// World text, over sprites, by its order and then the order entities are visited in.
void DrawWorldText(Renderer2D& renderer, Scene& scene, const Camera3D& camera);
// Every active ParticleSystem on a layer the renderer's camera sees.
void DrawScene(ParticleRenderer& renderer, Scene& scene);

} // namespace Spot
