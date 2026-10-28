#include "gameplay/environment/wind_sway.h"

#include <algorithm>
#include <cmath>

namespace Gameplay {

SPOT_COMPONENT(WindSway);

void WindSway::OnStart() {
    if (!skeleton) {
        return;
    }
    m_local.resize(skeleton->bones.size());
    m_model.resize(skeleton->bones.size());
    m_palette.resize(skeleton->joints.size());
    GetEntity().ForEachChild([&](Spot::Entity& child) {
        if (auto* mesh = child.GetComponent<Spot::MeshRenderer>()) {
            auto& skin = child.AddComponent<Spot::SkinnedMeshRenderer>();
            skin.SetMesh(mesh->GetMesh());
            skin.SetMaterial(mesh->GetMaterial());
            skin.SetSubmesh(mesh->GetSubmesh());
            skin.SetCastingShadows(mesh->IsCastingShadows());
            skin.SetBoundsPadding(5.0f);
            skin.SetEnabled(false);
            m_visuals.push_back(child.GetId());
        }
    });
    const auto position = GetEntity().GetTransform().GetWorldPosition();
    m_time = position.x * 0.7f + position.z * 0.4f;
}

void WindSway::OnLateUpdate(float deltaTime) {
    if (deltaTime <= 0.0f || !skeleton) {
        return;
    }
    m_time = std::fmod(m_time + deltaTime, 1000.0f);
    if (Spot::LengthSquared(m_wind) > 1.0f) {
        m_wind = Spot::Normalize(m_wind);
    }
    m_force += (m_wind - m_force) * (1.0f - std::exp(-7.0f * deltaTime));
    m_wind = {};
    const float strength = Spot::Length(m_force);
    if (strength < 0.0001f) {
        OnDisable();
        return;
    }
    const auto localWind = Spot::Inverse(GetEntity().GetTransform().GetWorldRotation()) * m_force / strength;
    auto axis = Spot::Cross(Spot::Vector3{0.0f, 1.0f, 0.0f}, localWind);
    if (Spot::LengthSquared(axis) < 0.001f) {
        axis = {1.0f, 0.0f, 0.0f};
    }
    axis = Spot::Normalize(axis);
    Spot::Pose::Bind(*skeleton, m_local);
    for (std::size_t i = 1; i < m_local.size(); ++i) {
        const float phase = static_cast<float>(i) * 2.39996f;
        const float frequency = 3.5f + static_cast<float>(i % 7) * 0.75f;
        int depth = 0;
        for (int parent = skeleton->bones[i].parent; parent >= 0; parent = skeleton->bones[parent].parent) {
            ++depth;
        }
        const float flex = 0.4f * std::min(0.28f + static_cast<float>(depth) * 0.18f, 0.85f);
        const float gust = 0.65f + 0.65f * std::sin(m_time * frequency + phase) +
                           0.25f * std::sin(m_time * (frequency * 1.73f) + phase * 0.37f);
        const auto bend = Spot::Quaternion::FromAxisAngle(axis, strength * flex * gust);
        const auto twist = Spot::Quaternion::FromAxisAngle(
            localWind, strength * flex * 0.35f * std::sin(m_time * (frequency + 2.0f) + phase));
        m_local[i].rotation = bend * twist * m_local[i].rotation;
    }
    Spot::Pose::ToModel(*skeleton, m_local, m_model);
    Spot::Pose::Palette(*skeleton, m_model, {}, m_palette);
    for (const auto id : m_visuals) {
        if (auto* entity = GetScene().GetEntity(id)) {
            auto* skin = entity->GetComponent<Spot::SkinnedMeshRenderer>();
            skin->SetPalette(m_palette);
            skin->SetEnabled(true);
            entity->GetComponent<Spot::MeshRenderer>()->SetEnabled(false);
        }
    }
}

void WindSway::OnDisable() {
    for (const auto id : m_visuals) {
        if (auto* entity = GetScene().GetEntity(id)) {
            entity->GetComponent<Spot::SkinnedMeshRenderer>()->SetEnabled(false);
            entity->GetComponent<Spot::MeshRenderer>()->SetEnabled(true);
        }
    }
    m_wind = {};
    m_force = {};
}

void WindSway::Serialize(Spot::Archive& archive) {
    archive.Field("skeleton", skeleton);
}

} // namespace Gameplay
