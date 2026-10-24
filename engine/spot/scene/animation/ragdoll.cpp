#include "spot/scene/animation/ragdoll.h"

#include "spot/assets/asset_archive.h"
#include "spot/core/debug/log.h"
#include "spot/core/debug/profiler.h"
#include "spot/core/io/archive.h"
#include "spot/core/math/angles.h"
#include "spot/scene/animation/animator.h"
#include "spot/scene/physics/character_controller.h"
#include "spot/scene/physics/collider.h"
#include "spot/scene/physics/joint.h"
#include "spot/scene/physics/rigidbody.h"
#include "spot/scene/scene.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Spot {

void RagdollBone::Serialize(Archive& archive) {
    archive.Field("bone", bone);
    archive.Field("size", size);
    archive.Field("center", center);
    archive.Field("mass", mass, {.min = 0.01f, .max = 1000});
    archive.Field("twistAxis", twistAxis);
    archive.Field("swingLimit", swingLimit, {.angle = true});
    archive.Field("twistMin", twistMin, {.angle = true});
    archive.Field("twistMax", twistMax, {.angle = true});
}

Animator* Ragdoll::GetAnimator() const {
    return GetEntity().GetComponent<Animator>();
}

bool Ragdoll::ValidateBones() const {
    const auto* animator = GetAnimator();
    if (!animator || !animator->GetSkeleton() || m_bones.empty()) {
        return false;
    }
    std::unordered_set<std::string> names;
    const auto finite = [](Vector3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    for (const auto& bone : m_bones) {
        if (!animator->GetSkeleton()->FindBone(bone.bone) || !names.insert(bone.bone).second || !finite(bone.size) ||
            bone.size.x <= 0 || bone.size.y <= 0 || bone.size.z <= 0 || !finite(bone.center) ||
            !finite(bone.twistAxis) || LengthSquared(bone.twistAxis) < 1e-6f || !std::isfinite(bone.mass) ||
            bone.mass <= 0 || !std::isfinite(bone.swingLimit) || bone.swingLimit < 0 || bone.swingLimit > Pi ||
            !std::isfinite(bone.twistMin) || !std::isfinite(bone.twistMax) || bone.twistMin < -Pi ||
            bone.twistMin > 0 || bone.twistMax < 0 || bone.twistMax > Pi) {
            return false;
        }
    }
    return true;
}

bool Ragdoll::SetBones(std::span<const RagdollBone> bones) {
    if (m_active) {
        return false;
    }
    auto old = m_bones;
    m_bones.assign(bones.begin(), bones.end());
    if (!ValidateBones()) {
        m_bones = std::move(old);
        return false;
    }
    DestroyBodies();
    return true;
}

void Ragdoll::ResizePose() {
    const auto* animator = GetAnimator();
    const auto count = animator && animator->GetSkeleton() ? animator->GetSkeleton()->bones.size() : 0;
    m_local.resize(count);
    m_model.resize(count);
    m_indices.clear();
    if (animator && animator->GetSkeleton()) {
        for (const auto& bone : m_bones) {
            m_indices.push_back(*animator->GetSkeleton()->FindBone(bone.bone));
        }
    }
}

bool Ragdoll::Build() {
    if (!ValidateBones()) {
        Log::GetEngineLogger().Warning("Ragdoll '{}' has invalid bone/body settings", GetEntity().GetName());
        return false;
    }
    auto* animator = GetAnimator();
    ResizePose();
    if (m_bodies.size() == m_bones.size() && std::ranges::all_of(m_bodies, [&](EntityId id) {
            auto* body = GetScene().GetEntity(id);
            return body && body->GetComponent<Rigidbody>();
        })) {
        m_appliedBones = m_bones;
        return true;
    }
    DestroyBodies();
    m_appliedBones = m_bones;
    const auto pose = animator->GetModelPose();
    const auto ownerWorld = GetEntity().GetTransform().GetWorldMatrix();
    for (std::size_t i = 0; i < m_bones.size(); ++i) {
        auto& body = GetScene().CreateEntity(GetEntity().GetName() + "/" + m_bones[i].bone);
        body.SetLayer(GetEntity().GetLayer());
        const auto world = Decompose(ownerWorld * pose[m_indices[i]]);
        body.GetTransform().SetPosition(world.translation);
        body.GetTransform().SetRotation(world.rotation);
        body.GetTransform().SetScale(world.scale);
        body.AddComponent<BoxCollider>(m_bones[i].size).SetCenter(m_bones[i].center);
        auto& rigidbody = body.AddComponent<Rigidbody>();
        rigidbody.SetMass(m_bones[i].mass);
        rigidbody.SetContinuous(true);
        rigidbody.SetLinearDamping(0.1f);
        rigidbody.SetAngularDamping(0.2f);
        body.SetActive(m_active);
        m_bodies.push_back(body.GetId());
    }
    const auto& skeleton = *animator->GetSkeleton();
    for (std::size_t i = 0; i < m_bones.size(); ++i) {
        auto parent = skeleton.bones[m_indices[i]].parent;
        while (parent >= 0) {
            const auto found = std::ranges::find(m_indices, static_cast<std::size_t>(parent));
            if (found != m_indices.end()) {
                const auto index = static_cast<std::size_t>(found - m_indices.begin());
                auto* body = GetScene().GetEntity(m_bodies[i]);
                auto& joint = body->AddComponent<SwingTwistJoint>();
                joint.SetConnectedEntity(m_bodies[index]);
                joint.SetTwistAxis(m_bones[i].twistAxis);
                joint.SetSwingLimit(m_bones[i].swingLimit);
                joint.SetTwistLimits(m_bones[i].twistMin, m_bones[i].twistMax);
                break;
            }
            parent = skeleton.bones[static_cast<std::size_t>(parent)].parent;
        }
    }
    return true;
}

bool Ragdoll::SetActive(bool active, float recoveryDuration) {
    m_requestedActive = active;
    auto* animator = GetAnimator();
    if (!animator || !std::isfinite(recoveryDuration) || recoveryDuration < 0) {
        return false;
    }
    if (active) {
        if (!Build()) {
            return false;
        }
        if (!m_active) {
            m_wasPlaying = animator->IsPlaying();
            const auto world = GetEntity().GetTransform().GetWorldMatrix();
            const auto pose = animator->GetModelPose();
            Vector3 velocity;
            if (auto* character = GetEntity().GetComponent<CharacterController>()) {
                m_characterWasEnabled = character->IsEnabled();
                velocity = character->GetVelocity();
            }
            for (std::size_t i = 0; i < m_bodies.size(); ++i) {
                auto* body = GetScene().GetEntity(m_bodies[i]);
                const auto placed = Decompose(world * pose[m_indices[i]]);
                body->GetTransform().SetPosition(placed.translation);
                body->GetTransform().SetRotation(placed.rotation);
                body->GetComponent<Rigidbody>()->SetLinearVelocity(velocity);
                body->GetComponent<Rigidbody>()->SetAngularVelocity({});
            }
        }
        m_active = true;
        animator->Stop();
        if (auto* character = GetEntity().GetComponent<CharacterController>()) {
            character->SetEnabled(false);
        }
        for (const auto id : m_bodies) {
            if (auto* body = GetScene().GetEntity(id)) {
                body->SetActive(true);
            }
        }
        return true;
    }
    if (!m_active) {
        return true;
    }
    SynchronizePose();
    m_active = false;
    for (const auto id : m_bodies) {
        if (auto* body = GetScene().GetEntity(id)) {
            body->SetActive(false);
        }
    }
    if (auto* character = GetEntity().GetComponent<CharacterController>()) {
        character->SetEnabled(m_characterWasEnabled);
    }
    animator->BlendFromPose(m_local, recoveryDuration, m_wasPlaying);
    return true;
}

bool Ragdoll::OwnsBody(EntityId body) const {
    return std::ranges::find(m_bodies, body) != m_bodies.end();
}

void Ragdoll::AddImpulse(Vector3 impulse, Vector3 worldPosition) {
    if (!m_active || m_bodies.empty()) {
        return;
    }
    Entity* nearest = nullptr;
    float distance = 0;
    for (const auto id : m_bodies) {
        if (auto* body = GetScene().GetEntity(id)) {
            const float candidate = LengthSquared(body->GetTransform().GetWorldPosition() - worldPosition);
            if (!nearest || candidate < distance) {
                nearest = body;
                distance = candidate;
            }
        }
    }
    if (nearest) {
        nearest->GetComponent<Rigidbody>()->AddImpulseAtPosition(impulse, worldPosition);
    }
}

void Ragdoll::SynchronizePose() {
    SPOT_PROFILE_SCOPE("Animation::Ragdoll");
    auto* animator = GetAnimator();
    if (!m_active || !animator || !animator->GetSkeleton() || m_local.size() != animator->GetLocalPose().size()) {
        return;
    }
    std::copy(animator->GetLocalPose().begin(), animator->GetLocalPose().end(), m_local.begin());
    const auto& skeleton = *animator->GetSkeleton();
    // Proxy bodies are scene roots, so following the pelvis cannot move the simulated bodies twice.
    for (std::size_t i = 0; i < m_indices.size(); ++i) {
        if (skeleton.bones[m_indices[i]].parent < 0) {
            if (auto* body = GetScene().GetEntity(m_bodies[i])) {
                const auto bind = skeleton.bones[m_indices[i]].bindPose.translation;
                const auto offset = GetEntity().GetTransform().GetWorldMatrix() * Vector4{bind.x, bind.y, bind.z, 0};
                GetEntity().GetTransform().SetWorldPosition(body->GetTransform().GetWorldPosition() -
                                                            Vector3{offset.x, offset.y, offset.z});
            }
            break;
        }
    }
    const auto inverseOwner = Inverse(GetEntity().GetTransform().GetWorldMatrix());
    for (std::size_t i = 0; i < m_local.size(); ++i) {
        const auto parent = skeleton.bones[i].parent;
        const auto& local = m_local[i];
        const auto matrix =
            Matrix4::Translation(local.translation) * Matrix4::Rotation(local.rotation) * Matrix4::Scale(local.scale);
        const auto found = std::ranges::find(m_indices, i);
        auto* body = found != m_indices.end()
                         ? GetScene().GetEntity(m_bodies[static_cast<std::size_t>(found - m_indices.begin())])
                         : nullptr;
        m_model[i] = body          ? inverseOwner * body->GetTransform().GetWorldMatrix()
                     : parent >= 0 ? m_model[static_cast<std::size_t>(parent)] * matrix
                                   : matrix;
        m_local[i] =
            Decompose(parent >= 0 ? Inverse(m_model[static_cast<std::size_t>(parent)]) * m_model[i] : m_model[i]);
    }
    animator->SetPose(m_local);
    animator->RefreshRenderers();
}

void Ragdoll::DestroyBodies() {
    for (const auto id : m_bodies) {
        if (auto* body = GetScene().GetEntity(id)) {
            body->Destroy();
        }
    }
    m_bodies.clear();
}
void Ragdoll::OnEnable() {
    m_initialized = true;
    if (!GetScene().GetSystem<AnimationSystem>()) {
        GetScene().AddSystem<AnimationSystem>();
    }
    if (m_active) {
        SetActive(true);
    }
}
void Ragdoll::OnDisable() {
    if (m_active) {
        SetActive(false, 0);
    }
}
void Ragdoll::OnDestroy() {
    DestroyBodies();
}
void Ragdoll::OnValidate() {
    if (!m_initialized) {
        m_active = m_requestedActive;
        return;
    }
    const bool requested = m_requestedActive;
    if (m_bones != m_appliedBones && !m_bodies.empty()) {
        if (m_active) {
            SetActive(false, 0);
        }
        DestroyBodies();
        SetActive(requested);
    } else if (requested != m_active) {
        SetActive(requested);
    }
}
void Ragdoll::Serialize(Archive& archive) {
    archive.Field("bones", m_bones);
    bool active = m_active;
    archive.Field("active", active);
    if (archive.IsLoading()) {
        m_requestedActive = active;
    }
    archive.Field("bodies", m_bodies, {.hidden = true});
    archive.Field("characterWasEnabled", m_characterWasEnabled, {.hidden = true});
    archive.Field("wasPlaying", m_wasPlaying, {.hidden = true});
}

} // namespace Spot
