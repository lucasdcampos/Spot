#include "gameplay/effects/decals.h"

#include <algorithm>
#include <cmath>

namespace Gameplay {

namespace {

// Off the surface by this much, in meters, so the mark is drawn over it rather than into it.
constexpr float Lift = 0.008f;
// Cut out rather than transparent: transparent surfaces reflect light over all of the square, as glass does, showing
// its edges. Fading raises the cutoff, so the mark shrinks to its thickest parts as it dries.
constexpr float Cutoff = 0.3f;

// Turns +Y, the plane's face, to the direction.
Spot::Quaternion Face(Spot::Vector3 direction) {
    const Spot::Vector3 up{0.0f, 1.0f, 0.0f};
    const float dot = std::clamp(Spot::Dot(up, direction), -1.0f, 1.0f);
    if (dot < -0.9999f) {
        return Spot::Quaternion::FromAxisAngle({1.0f, 0.0f, 0.0f}, Spot::Pi);
    }
    if (dot > 0.9999f) {
        return {};
    }
    return Spot::Quaternion::FromAxisAngle(Spot::Normalize(Spot::Cross(up, direction)), std::acos(dot));
}

} // namespace

void DecalStyle::Serialize(Spot::Archive& archive) {
    archive.Field("texture", texture);
    archive.Field("color", color);
    archive.Field("roughness", roughness, {.min = 0.0f, .max = 1.0f, .speed = 0.01f});
    archive.Field("lifetime", lifetime, {.min = 0.0f, .max = 600.0f, .speed = 0.1f, .tooltip = "Seconds"});
    archive.Field("fadeTime", fadeTime, {.min = 0.0f, .max = 60.0f, .speed = 0.05f, .tooltip = "Seconds"});
}

Decals::Decals() : SceneSystem(100) {
    m_decals.reserve(MaxDecals);
}

Decals& Decals::Get(Spot::Scene& scene) {
    if (auto* decals = scene.GetSystem<Decals>()) {
        return *decals;
    }
    return scene.AddSystem<Decals>();
}

void Decals::Place(const DecalStyle& style, Spot::EntityId surface, Spot::Vector3 point, Spot::Vector3 normal,
                   float size, float angle, float stretch) {
    Spot::Scene& scene = GetScene();
    const Spot::Entity* on = scene.GetEntity(surface);
    if (!on || size <= 0.0f || Spot::LengthSquared(normal) < 1e-6f) {
        return;
    }
    normal = Spot::Normalize(normal);
    std::erase_if(m_decals, [&](const Decal& decal) { return !scene.GetEntity(decal.entity); });
    Decal* decal = nullptr;
    if (m_decals.size() >= MaxDecals) {
        decal = &*std::min_element(m_decals.begin(), m_decals.end(),
                                   [](const Decal& a, const Decal& b) { return a.sequence < b.sequence; });
    } else {
        Spot::Entity& entity = scene.CreateEntity("Decal");
        entity.AddTag("TransientEffect");
        auto& renderer = entity.AddComponent<Spot::MeshRenderer>(
            Spot::Assets::Load<Spot::Mesh>(Spot::BuiltInAssets::PlaneMesh));
        renderer.SetCastingShadows(false);
        decal = &m_decals.emplace_back();
        decal->entity = entity.GetId();
    }
    const Spot::Transform& base = on->GetTransform();
    const Spot::Quaternion inverse = Spot::Inverse(base.GetWorldRotation());
    const Spot::Quaternion rotation = Spot::Quaternion::FromAxisAngle(normal, angle) * Face(normal);
    decal->surface = surface;
    decal->position = inverse * (point + normal * Lift - base.GetWorldPosition());
    decal->rotation = inverse * rotation;
    decal->material = {.baseColor = style.color,
                       .baseColorTexture = style.texture,
                       .roughness = style.roughness,
                       .alphaMode = Spot::AlphaMode::Cutout,
                       .alphaCutoff = Cutoff};
    decal->lifetime = style.lifetime;
    decal->fadeTime = style.fadeTime;
    decal->age = 0.0f;
    decal->sequence = ++m_sequence;
    Spot::Entity* entity = scene.GetEntity(decal->entity);
    entity->GetTransform().SetScale({size, 1.0f, size * std::max(stretch, 0.01f)});
    entity->GetComponent<Spot::MeshRenderer>()->SetMaterial(decal->material);
    entity->SetActive(true);
}

float Decals::AngleAlong(Spot::Vector3 normal, Spot::Vector3 direction) {
    normal = Spot::Normalize(normal);
    const Spot::Vector3 across = direction - normal * Spot::Dot(direction, normal);
    if (Spot::LengthSquared(across) < 1e-6f) {
        return 0.0f;
    }
    // The plane's +Z, which its texture runs down, as the mark facing the normal has it before it is turned.
    const Spot::Vector3 down = Face(normal) * Spot::Vector3{0.0f, 0.0f, 1.0f};
    const Spot::Vector3 along = Spot::Normalize(across);
    return std::atan2(Spot::Dot(Spot::Cross(down, along), normal), Spot::Dot(down, along));
}

void Decals::OnLateUpdate(float deltaTime) {
    Spot::Scene& scene = GetScene();
    for (auto it = m_decals.begin(); it != m_decals.end();) {
        Spot::Entity* entity = scene.GetEntity(it->entity);
        const Spot::Entity* surface = scene.GetEntity(it->surface);
        if (!entity) {
            it = m_decals.erase(it);
            continue;
        }
        if (!entity->IsActiveSelf()) {
            ++it;
            continue;
        }
        it->age += deltaTime;
        const float left = it->lifetime + it->fadeTime - it->age;
        // Gone with what it was on, or once faded, kept to be laid again.
        if (!surface || left <= 0.0f) {
            entity->SetActive(false);
            ++it;
            continue;
        }
        const Spot::Transform& base = surface->GetTransform();
        Spot::Transform& transform = entity->GetTransform();
        transform.SetPosition(base.GetWorldPosition() + base.GetWorldRotation() * it->position);
        transform.SetRotation(base.GetWorldRotation() * it->rotation);
        if (left < it->fadeTime) {
            Spot::Material material = it->material;
            material.alphaCutoff = Cutoff + (1.0f - Cutoff) * (1.0f - left / std::max(it->fadeTime, 0.001f));
            entity->GetComponent<Spot::MeshRenderer>()->SetMaterial(std::move(material));
        }
        ++it;
    }
}

} // namespace Gameplay
