#include "spot/scene/scene.h"

#include "spot/core/profiler.h"
#include "spot/render/model.h"
#include "spot/scene/animation/animator.h"
#include "spot/scene/animation/bone_attachment.h"
#include "spot/scene/animation/skinned_mesh_renderer.h"
#include "spot/scene/rendering/mesh_renderer.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace Spot {

Entity& Scene::Instantiate(const Model& model, std::string name) {
    SPOT_PROFILE_SCOPE("Scene::Instantiate(Model)");
    Entity& root = CreateEntity(std::move(name));
    std::vector<Entity*> instances;
    std::vector<Matrix4> skippedTransforms;
    std::vector<std::pair<const Skeleton*, EntityId>> animators;
    instances.reserve(model.GetNodes().size());
    for (const ModelNode& node : model.GetNodes()) {
        Entity& parent = node.parent ? *instances[*node.parent] : root;
        Matrix4 localMatrix = node.transform;
        if (node.parent) {
            localMatrix = skippedTransforms[*node.parent] * localMatrix;
        }
        if (node.joint && !node.mesh && !node.mesh.GetId().IsValid()) {
            instances.push_back(&parent);
            skippedTransforms.push_back(localMatrix);
            continue;
        }
        Entity& entity = parent.CreateChild(node.name);
        const TransformComponents local = Decompose(localMatrix);
        entity.GetTransform().SetPosition(local.translation);
        entity.GetTransform().SetRotation(local.rotation);
        entity.GetTransform().SetScale(local.scale);
        instances.push_back(&entity);
        skippedTransforms.push_back(Matrix4{});
        if (node.skeleton) {
            const auto found =
                std::ranges::find_if(animators, [&](const auto& pair) { return pair.first == node.skeleton.Get(); });
            if (found == animators.end()) {
                Entity& owner = animators.empty() ? root : root.CreateChild(node.skeleton->name);
                auto& animator = owner.AddComponent<Animator>();
                animator.SetSkeleton(node.skeleton);
                if (!node.animations.empty()) {
                    animator.Play(node.animations.front());
                }
                animators.emplace_back(node.skeleton.Get(), owner.GetId());
            }
        }
    }
    for (const ModelPart& part : model.GetParts()) {
        const auto& node = model.GetNodes()[part.node];
        if (node.skeleton) {
            auto& renderer = instances[part.node]->AddComponent<SkinnedMeshRenderer>();
            renderer.SetMesh(part.mesh);
            renderer.SetMaterial(part.material);
            renderer.SetSubmesh(part.submesh);
            renderer.SetMeshModel(model.GetNodeTransform(part.node));
            const auto found =
                std::ranges::find_if(animators, [&](const auto& pair) { return pair.first == node.skeleton.Get(); });
            renderer.SetAnimator(found->second);
        } else {
            instances[part.node]->AddComponent<MeshRenderer>(part.mesh, part.material).SetSubmesh(part.submesh);
            bool followsJoint = false;
            for (std::optional<std::size_t> ancestor = part.node; ancestor;
                 ancestor = model.GetNodes()[*ancestor].parent) {
                followsJoint = followsJoint || model.GetNodes()[*ancestor].joint;
            }
            if (followsJoint && !animators.empty() && !instances[part.node]->GetComponent<BoneAttachment>()) {
                auto& attachment = instances[part.node]->AddComponent<BoneAttachment>();
                attachment.SetAnimator(animators.front().second);
                attachment.SetBoneIndex(static_cast<int>(part.node));
            }
        }
    }
    for (const auto& [skeleton, owner] : animators) {
        static_cast<void>(skeleton);
        GetEntity(owner)->GetComponent<Animator>()->RefreshRenderers();
    }
    return root;
}

} // namespace Spot
