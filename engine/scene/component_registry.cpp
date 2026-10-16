#include "scene/component_registry.h"

#include "scene/animation/animator.h"
#include "scene/animation/bone_attachment.h"
#include "scene/animation/ragdoll.h"
#include "scene/animation/skinned_mesh_renderer.h"
#include "scene/audio/audio_source.h"
#include "scene/missing_component.h"
#include "scene/particles/particle_system.h"
#include "scene/physics/character_controller.h"
#include "scene/physics/collider.h"
#include "scene/physics/joint.h"
#include "scene/physics/rigidbody.h"
#include "scene/physics2d/physics_world_2d.h"
#include "scene/physics2d/tilemap_collider_2d.h"
#include "scene/rendering/camera.h"
#include "scene/rendering/fog.h"
#include "scene/rendering/light.h"
#include "scene/rendering/mesh_renderer.h"
#include "scene/rendering/post_processing.h"
#include "scene/rendering/sky.h"
#include "scene/rendering/sprite_animator.h"
#include "scene/rendering/sprite_renderer.h"
#include "scene/rendering/text_renderer.h"
#include "scene/rendering/tilemap.h"
#include "scene/ui/canvas.h"
#include "scene/ui/ui_layout_group.h"
#include "scene/ui/ui_widgets.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace Spot::Detail {

namespace {

struct Registry {
    std::mutex mutex;
    std::unordered_map<std::type_index, std::string> names;
    std::unordered_map<std::string, ComponentFactory> factories;
    std::unordered_map<std::string, std::type_index> types;
};

template <typename T>
void RegisterBuiltIn(Registry& registry, const std::string& name) {
    const std::type_index type = typeid(T);
    registry.names.insert_or_assign(type, name);
    registry.factories.insert_or_assign(name, []() -> std::unique_ptr<Component> { return std::make_unique<T>(); });
    registry.types.insert_or_assign(name, type);
}

// Built on first use, so components can register from static initializers in any order. The engine's own components
// are registered from the start, so scenes save them without any setup.
Registry& GetRegistry() {
    static Registry registry;
    static const bool builtInRegistered = [] {
        RegisterBuiltIn<ParticleSystem>(registry, "ParticleSystem");
        RegisterBuiltIn<BoneAttachment>(registry, "BoneAttachment");
        RegisterBuiltIn<Ragdoll>(registry, "Ragdoll");
        RegisterBuiltIn<Animator>(registry, "Animator");
        RegisterBuiltIn<SkinnedMeshRenderer>(registry, "SkinnedMeshRenderer");
        RegisterBuiltIn<MeshRenderer>(registry, "MeshRenderer");
        RegisterBuiltIn<Rigidbody>(registry, "Rigidbody");
        RegisterBuiltIn<Rigidbody2D>(registry, "Rigidbody2D");
        RegisterBuiltIn<BoxCollider2D>(registry, "BoxCollider2D");
        RegisterBuiltIn<CircleCollider2D>(registry, "CircleCollider2D");
        RegisterBuiltIn<CapsuleCollider2D>(registry, "CapsuleCollider2D");
        RegisterBuiltIn<PolygonCollider2D>(registry, "PolygonCollider2D");
        RegisterBuiltIn<BoxCollider>(registry, "BoxCollider");
        RegisterBuiltIn<SphereCollider>(registry, "SphereCollider");
        RegisterBuiltIn<CapsuleCollider>(registry, "CapsuleCollider");
        RegisterBuiltIn<MeshCollider>(registry, "MeshCollider");
        RegisterBuiltIn<FixedJoint>(registry, "FixedJoint");
        RegisterBuiltIn<HingeJoint>(registry, "HingeJoint");
        RegisterBuiltIn<DistanceJoint>(registry, "DistanceJoint");
        RegisterBuiltIn<SpringJoint>(registry, "SpringJoint");
        RegisterBuiltIn<SwingTwistJoint>(registry, "SwingTwistJoint");
        RegisterBuiltIn<CharacterController>(registry, "CharacterController");
        RegisterBuiltIn<AmbientLight>(registry, "AmbientLight");
        RegisterBuiltIn<DirectionalLight>(registry, "DirectionalLight");
        RegisterBuiltIn<PointLight>(registry, "PointLight");
        RegisterBuiltIn<SpotLight>(registry, "SpotLight");
        RegisterBuiltIn<Sky>(registry, "Sky");
        RegisterBuiltIn<Fog>(registry, "Fog");
        RegisterBuiltIn<PostProcessing>(registry, "PostProcessing");
        RegisterBuiltIn<UiLayoutGroup>(registry, "UiLayoutGroup");
        RegisterBuiltIn<UiSizeFitter>(registry, "UiSizeFitter");
        RegisterBuiltIn<UiImage>(registry, "UiImage");
        RegisterBuiltIn<UiText>(registry, "UiText");
        RegisterBuiltIn<UiButton>(registry, "UiButton");
        RegisterBuiltIn<UiToggle>(registry, "UiToggle");
        RegisterBuiltIn<UiSlider>(registry, "UiSlider");
        RegisterBuiltIn<UiInputField>(registry, "UiInputField");
        RegisterBuiltIn<UiScrollView>(registry, "UiScrollView");
        RegisterBuiltIn<Canvas>(registry, "Canvas");
        RegisterBuiltIn<UiRect>(registry, "UiRect");
        RegisterBuiltIn<TextRenderer>(registry, "TextRenderer");
        RegisterBuiltIn<SpriteRenderer>(registry, "SpriteRenderer");
        RegisterBuiltIn<SpriteAnimator>(registry, "SpriteAnimator");
        RegisterBuiltIn<Tilemap>(registry, "Tilemap");
        RegisterBuiltIn<TilemapCollider2D>(registry, "TilemapCollider2D");
        RegisterBuiltIn<Camera>(registry, "Camera");
        RegisterBuiltIn<AudioSource>(registry, "AudioSource");
        RegisterBuiltIn<AudioListener>(registry, "AudioListener");
        return true;
    }();
    static_cast<void>(builtInRegistered);
    return registry;
}

} // namespace

void RegisterComponentType(std::type_index type, std::string name, ComponentFactory factory) {
    // Code that registers types may be a reloaded game module, whose types may override hooks they did not before.
    GetUnusedComponentHooks().clear();
    Registry& registry = GetRegistry();
    const std::lock_guard lock(registry.mutex);

    // The name stands for a single type, and a type for a single name.
    if (const auto previous = registry.types.find(name); previous != registry.types.end()) {
        registry.names.erase(previous->second);
        registry.types.erase(previous);
    }
    if (const auto previous = registry.names.find(type); previous != registry.names.end()) {
        registry.factories.erase(previous->second);
        registry.types.erase(previous->second);
    }

    registry.names.insert_or_assign(type, name);
    registry.factories.insert_or_assign(name, factory);
    registry.types.insert_or_assign(std::move(name), type);
}

const std::string* FindComponentTypeName(std::type_index type) {
    Registry& registry = GetRegistry();
    const std::lock_guard lock(registry.mutex);
    const auto found = registry.names.find(type);
    // Entries are never moved once inserted, so the pointer stays valid until the type registers again.
    return found != registry.names.end() ? &found->second : nullptr;
}

ComponentFactory FindComponentFactory(std::string_view name) {
    Registry& registry = GetRegistry();
    const std::lock_guard lock(registry.mutex);
    const auto found = registry.factories.find(std::string(name));
    return found != registry.factories.end() ? found->second : nullptr;
}

} // namespace Spot::Detail

namespace Spot {

void UnregisterComponent(std::string_view name) {
    Detail::GetUnusedComponentHooks().clear();
    Detail::Registry& registry = Detail::GetRegistry();
    const std::lock_guard lock(registry.mutex);
    const auto found = registry.types.find(std::string(name));
    if (found == registry.types.end()) {
        return;
    }
    // What the type requires refers to code of the module that defined it, which may be about to be unloaded; a
    // module loaded again declares it anew.
    Detail::SetComponentRequirements(Detail::GetComponentTypeIndex(found->second), {});
    registry.names.erase(found->second);
    registry.factories.erase(found->first);
    registry.types.erase(found);
}

std::vector<std::string> GetComponentTypeNames() {
    Detail::Registry& registry = Detail::GetRegistry();
    const std::lock_guard lock(registry.mutex);
    std::vector<std::string> names;
    names.reserve(registry.types.size());
    for (const auto& [name, type] : registry.types) {
        names.push_back(name);
    }
    std::ranges::sort(names);
    return names;
}

std::unique_ptr<Component> CreateComponent(std::string_view name) {
    const Detail::ComponentFactory factory = Detail::FindComponentFactory(name);
    return factory ? factory() : nullptr;
}

std::string GetComponentTypeName(const Component& component) {
    if (const auto* missing = dynamic_cast<const MissingComponent*>(&component)) {
        return missing->GetTypeName();
    }
    const std::string* name = Detail::FindComponentTypeName(typeid(component));
    return name ? *name : std::string();
}

} // namespace Spot
