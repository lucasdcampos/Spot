#include "spot/scene/scripting/script_scene.h"

#include "spot/assets/assets.h"
#include "spot/core/math/quaternion.h"
#include "spot/core/time/time.h"
#include "spot/platform/input.h"
#include "spot/scene/audio/audio_source.h"
#include "spot/scene/component_registry.h"
#include "spot/scene/physics/collider.h"
#include "spot/scene/physics/collision.h"
#include "spot/scene/physics/physics_world.h"
#include "spot/scene/physics2d/collision_2d.h"
#include "spot/scene/physics2d/physics_world_2d.h"
#include "spot/scene/prefab.h"
#include "spot/scene/rendering/camera.h"
#include "spot/scene/rendering/sprite_renderer.h"
#include "spot/scene/scene.h"
#include "spot/scene/scene_system.h"
#include "spot/scene/scripting/script_api.h"
#include "spot/scene/scripting/script_bindings.h"
#include "spot/scene/scripting/script_component.h"
#include "spot/scene/scripting/script_events.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <unordered_map>

namespace Spot::Detail {

struct ScriptSceneContext : std::enable_shared_from_this<ScriptSceneContext> {
    Scene* scene = nullptr;
    // The executing owner can finish cleanup after public scene queries stop listing it.
    Component* component = nullptr;
    std::unordered_map<std::string, std::weak_ptr<ScriptObject>> objects;
    // The script's own entity and component, which own its event subscriptions.
    EntityId owner = EntityId::Invalid;
    ComponentId ownerComponent;
    std::function<void(const std::function<void()>&)> deliver;
};

namespace {

// The innermost script scope of this thread. Engine references from another script, received through an event or
// a call, resolve in its scene, and subscriptions belong to its component.
thread_local ScriptSceneContext* t_activeContext = nullptr;

// Event subscriptions of a scene's scripts. Each belongs to the component that subscribed and ends with it.
class ScriptEventSystem final : public SceneSystem {
public:
    struct Subscription {
        std::uint64_t id = 0;
        // Invalid for scene-wide events.
        EntityId target = EntityId::Invalid;
        std::string name;
        std::weak_ptr<ScriptSceneContext> subscriber;
        ScriptObjectRef callback;
    };
    std::vector<Subscription> subscriptions;
    std::uint64_t nextId = 1;

    bool Has(std::uint64_t id) const {
        return std::ranges::any_of(subscriptions, [id](const Subscription& item) { return item.id == id; });
    }
    void Prune() {
        std::erase_if(subscriptions, [](const Subscription& item) {
            const auto context = item.subscriber.lock();
            return !context || !context->deliver;
        });
    }
};

constexpr std::size_t MaxEventDepth = 16;
thread_local std::size_t t_eventDepth = 0;

std::size_t DispatchEvent(Scene& scene, EntityId target, std::string_view name,
                          std::span<const ScriptValue> arguments) {
    auto* system = scene.GetSystem<ScriptEventSystem>();
    if (!system)
        return 0;
    if (t_eventDepth >= MaxEventDepth)
        throw std::runtime_error("Script event depth limit exceeded");
    ++t_eventDepth;
    struct Exit {
        ~Exit() { --t_eventDepth; }
    } exit;
    system->Prune();
    // Callbacks may subscribe or cancel: run the subscriptions present now, unless cancelled before their turn.
    std::vector<ScriptEventSystem::Subscription> matches;
    for (const auto& subscription : system->subscriptions) {
        if (subscription.target == target && subscription.name == name)
            matches.push_back(subscription);
    }
    std::size_t delivered = 0;
    for (const auto& subscription : matches) {
        if (!system->Has(subscription.id))
            continue;
        const auto context = subscription.subscriber.lock();
        if (!context || !context->deliver)
            continue;
        auto* entity = scene.GetEntity(context->owner);
        Component* component = nullptr;
        if (entity) {
            entity->ForEachComponent<Component>([&](Component& item) {
                if (item.GetId() == context->ownerComponent)
                    component = &item;
            });
        }
        if (!component || !component->IsActiveAndEnabled())
            continue;
        ++delivered;
        const auto deliver = context->deliver;
        deliver([&] { subscription.callback->Invoke(arguments); });
    }
    return delivered;
}

template <typename T>
T Argument(const ScriptValue& value) {
    return FromScriptValue<T>(value);
}
void Count(std::span<const ScriptValue> arguments, std::size_t count) {
    if (arguments.size() != count)
        throw std::runtime_error("Incorrect argument count");
}

enum class ObjectKind { Entity, Transform, Component, Scene, Input, Time, Physics };
ScriptValue Wrap(const std::shared_ptr<ScriptSceneContext>& context, ObjectKind kind,
                 EntityId entity = EntityId::Invalid, ComponentId component = {});
ScriptValue WrapEntity(const std::shared_ptr<ScriptSceneContext>& context, const Entity* entity) {
    return entity ? Wrap(context, ObjectKind::Entity, entity->GetId()) : ScriptValue{};
}

// The name ScriptApi::Types describes each kind of object by.
constexpr std::string_view TypeName(ObjectKind kind) {
    switch (kind) {
    case ObjectKind::Entity:
        return "Entity";
    case ObjectKind::Transform:
        return "Transform";
    case ObjectKind::Component:
        return ScriptApi::Component;
    case ObjectKind::Scene:
        return ScriptApi::Scene;
    case ObjectKind::Input:
        return ScriptApi::Input;
    case ObjectKind::Time:
        return ScriptApi::Time;
    case ObjectKind::Physics:
        return ScriptApi::Physics;
    }
    return {};
}

constexpr std::array<std::string_view, 3> KeyQueries{"IsKeyDown", "IsKeyPressed", "IsKeyReleased"};
constexpr std::array<std::string_view, 3> MouseQueries{"IsMouseButtonDown", "IsMouseButtonPressed",
                                                       "IsMouseButtonReleased"};
constexpr std::array<std::string_view, 3> GamepadQueries{"IsGamepadButtonDown", "IsGamepadButtonPressed",
                                                         "IsGamepadButtonReleased"};
constexpr std::array<std::string_view, 4> PhysicsQueries{"Raycast", "SphereCast", "Raycast2D", "CircleCast2D"};
constexpr std::array<std::string_view, 7> CoroutineMethods{"StartCoroutine",     "StopCoroutine", "StopCoroutines",
                                                           "IsCoroutineRunning", "WaitSeconds",   "WaitFrames",
                                                           "WaitUntil"};

// What WaitSeconds(), WaitFrames() and WaitUntil() return, for a coroutine to yield.
struct WaitObject final : ScriptObject {
    ScriptWait wait;
    bool HasMethod(std::string_view) const override { return false; }
};

constexpr std::array<std::string_view, 8> EntityStructure{
    "Destroy", "AddComponent", "RemoveComponent", "SetParent", "CreateChild", "On", "Emit", "GetScript"};

// The script behind a ScriptComponent, whose members its component references forward to.
ScriptObjectRef ScriptOf(const Component& component) {
    const auto* script = dynamic_cast<const ScriptComponent*>(&component);
    return script ? script->GetScriptObject() : nullptr;
}
constexpr std::array<std::string_view, 5> SceneStructure{"CreateEntity", "Instantiate", "DestroyEntity", "On", "Emit"};

template <std::size_t N>
constexpr bool Contains(const std::array<std::string_view, N>& names, std::string_view name) {
    for (const auto item : names) {
        if (item == name)
            return true;
    }
    return false;
}
// Finds the enum value named as GetKeyName() and the like name it, for the first `count` values.
template <typename T, typename Name>
T ParseName(std::string_view text, int first, int last, Name name, std::string_view what) {
    for (int i = first; i <= last; ++i) {
        if (name(static_cast<T>(i)) == text)
            return static_cast<T>(i);
    }
    throw std::runtime_error("Unknown " + std::string(what) + " name '" + std::string(text) + "'");
}

// A value object with fixed read-only properties, such as a contact or a raycast hit. Points beyond the first are read
// with GetPoint(index), since script values have no engine array type.
struct ScriptRecord final : ScriptObject {
    std::unordered_map<std::string, ScriptValue> properties;
    std::vector<ScriptValue> points;
    bool HasProperty(std::string_view name) const override { return properties.contains(std::string(name)); }
    bool HasMethod(std::string_view name) const override { return name == "GetPoint" && !points.empty(); }
    ScriptValue Get(std::string_view name) const override {
        if (const auto found = properties.find(std::string(name)); found != properties.end())
            return found->second;
        return ScriptObject::Get(name);
    }
    ScriptValue Call(std::string_view name, std::span<const ScriptValue> arguments) override {
        if (name == "GetPoint" && !points.empty()) {
            Count(arguments, 1);
            const auto index = Argument<std::uint32_t>(arguments[0]);
            if (index >= points.size())
                throw std::runtime_error("Contact point index is out of range");
            return points[index];
        }
        return ScriptObject::Call(name, arguments);
    }
};

struct SceneObject final : ScriptObject {
    std::shared_ptr<ScriptSceneContext> context;
    ObjectKind kind;
    EntityId entityId;
    ComponentId componentId;

    Scene& CurrentScene() const {
        if (context->scene)
            return *context->scene;
        if (t_activeContext && t_activeContext->scene)
            return *t_activeContext->scene;
        throw std::runtime_error("Scene access is only available during script execution");
    }
    Entity* FindEntity() const { return CurrentScene().GetEntity(entityId); }
    Entity& RequireEntity() const {
        auto* entity = FindEntity();
        if (!entity && context->component && context->component->GetEntity().GetId() == entityId)
            entity = &context->component->GetEntity();
        if (!entity)
            throw std::runtime_error("Entity reference is no longer valid in this scene");
        return *entity;
    }
    Component* FindComponent() const {
        auto* entity = FindEntity();
        if (!entity)
            return nullptr;
        Component* found = nullptr;
        entity->ForEachComponent<Component>([&](Component& component) {
            if (component.GetId() == componentId)
                found = &component;
        });
        return found;
    }
    Component& RequireComponent() const {
        CurrentScene();
        if (context->component && context->component->GetId() == componentId &&
            context->component->GetEntity().GetId() == entityId)
            return *context->component;
        auto* component = FindComponent();
        if (!component)
            throw std::runtime_error("Component reference is no longer valid");
        return *component;
    }
    ScriptValue ComponentNamed(std::string_view name, std::size_t index = 0) const {
        Component* found = nullptr;
        RequireEntity().ForEachComponent<Component>([&](Component& component) {
            if (GetComponentTypeName(component) == name) {
                if (index == 0 && !found)
                    found = &component;
                else if (index != 0)
                    --index;
            }
        });
        return found ? Wrap(context, ObjectKind::Component, entityId, found->GetId()) : ScriptValue{};
    }
    // A script's own component reference is the base of its script object, so it never forwards back to it.
    ScriptObjectRef ForwardedScript(const Component& component) const {
        return context->ownerComponent == componentId ? nullptr : ScriptOf(component);
    }
    // The entity id of a script entity reference, or of a component's entity; null means none.
    static EntityId EntityArgument(const ScriptValue& value) {
        if (std::holds_alternative<std::monostate>(value))
            return EntityId::Invalid;
        const auto* reference = std::get_if<ScriptObjectRef>(&value);
        const auto object = reference ? std::dynamic_pointer_cast<SceneObject>(*reference) : nullptr;
        if (!object || (object->kind != ObjectKind::Entity && object->kind != ObjectKind::Component &&
                        object->kind != ObjectKind::Transform))
            throw std::runtime_error("Expected an entity reference or null");
        return object->entityId;
    }
    Entity* EntityFromArgument(const ScriptValue& value) const {
        const auto id = EntityArgument(value);
        if (id == EntityId::Invalid)
            return nullptr;
        auto* entity = CurrentScene().GetEntity(id);
        if (!entity)
            throw std::runtime_error("Entity reference is no longer valid in this scene");
        return entity;
    }
    QueryFilter Filter(std::span<const ScriptValue> arguments, std::size_t first) const {
        QueryFilter filter;
        if (arguments.size() > first)
            filter.ignore = EntityArgument(arguments[first]);
        if (arguments.size() > first + 1)
            filter.layerMask = Argument<std::uint32_t>(arguments[first + 1]);
        if (arguments.size() > first + 2)
            filter.includeTriggers = Argument<bool>(arguments[first + 2]);
        return filter;
    }
    template <typename Hit>
    ScriptValue MakeHit(const std::optional<Hit>& hit) const;
    ScriptValue CallInput(std::string_view name, std::span<const ScriptValue> arguments) const;
    ScriptValue CallPhysics(std::string_view name, std::span<const ScriptValue> arguments) const;
    ScriptValue CallStructure(std::string_view name, std::span<const ScriptValue> arguments);
    ScriptValue CallCoroutine(std::string_view name, std::span<const ScriptValue> arguments);
    bool Describes(std::string_view name, ScriptApi::MemberKind member) const {
        return ScriptApi::FindMember(TypeName(kind), name, member) != nullptr;
    }
    bool HasProperty(std::string_view name) const override {
        if (Describes(name, ScriptApi::MemberKind::Property))
            return true;
        // entity.Health is shorthand for entity.GetComponent("Health").
        if (kind == ObjectKind::Entity)
            return !name.empty() && name.front() >= 'A' && name.front() <= 'Z' && !HasMethod(name);
        if (kind == ObjectKind::Component) {
            if (HasScriptProperty(RequireComponent(), name))
                return true;
            const auto script = ForwardedScript(RequireComponent());
            return script && script->HasProperty(name);
        }
        return false;
    }
    bool HasMethod(std::string_view name) const override {
        if (Describes(name, ScriptApi::MemberKind::Method))
            return true;
        if (kind == ObjectKind::Component) {
            if (HasScriptMethod(RequireComponent(), name))
                return true;
            const auto script = ForwardedScript(RequireComponent());
            return script && script->HasMethod(name);
        }
        return false;
    }
    ScriptValue Get(std::string_view name) const override {
        if (HasMethod(name))
            return ScriptObject::Get(name);
        if (name == "scene" &&
            (kind == ObjectKind::Entity || kind == ObjectKind::Transform || kind == ObjectKind::Component)) {
            if (kind == ObjectKind::Component)
                RequireComponent();
            else
                RequireEntity();
            return Wrap(context, ObjectKind::Scene);
        }
        if (kind == ObjectKind::Entity) {
            const auto& entity = RequireEntity();
            if (name == "Transform" || name == "transform")
                return Wrap(context, ObjectKind::Transform, entityId);
            if (name == "Parent" || name == "parent")
                return WrapEntity(context, entity.GetParent());
            if (name == "name")
                return entity.GetName();
            if (name == "id")
                return std::to_string(static_cast<std::uint64_t>(entityId));
            if (name == "active")
                return entity.IsActiveSelf();
            if (name == "activeInHierarchy")
                return entity.IsActiveInHierarchy();
            if (name == "layer")
                return static_cast<double>(entity.GetLayer());
            if (!name.empty() && name.front() >= 'A' && name.front() <= 'Z')
                return ComponentNamed(name);
        } else if (kind == ObjectKind::Transform) {
            const auto& transform = RequireEntity().GetTransform();
            if (name == "Entity" || name == "entity")
                return Wrap(context, ObjectKind::Entity, entityId);
            if (name == "position")
                return transform.GetPosition();
            if (name == "worldPosition")
                return transform.GetWorldPosition();
            if (name == "scale")
                return transform.GetScale();
            if (name == "rotation")
                return ToEuler(transform.GetRotation());
            if (name == "worldRotation")
                return ToEuler(transform.GetWorldRotation());
            if (name == "rotation2D")
                return static_cast<double>(transform.GetRotation2D());
        } else if (kind == ObjectKind::Component) {
            const auto& component = RequireComponent();
            if (name == "transform")
                return Wrap(context, ObjectKind::Transform, entityId);
            if (name == "Entity" || name == "entity")
                return Wrap(context, ObjectKind::Entity, entityId);
            if (name == "id")
                return componentId.ToString();
            if (name == "enabled")
                return component.IsEnabled();
            if (name == "active")
                return component.IsActiveAndEnabled();
            if (name == "faulted")
                return component.IsFaulted();
            if (!HasScriptProperty(component, name)) {
                if (const auto script = ForwardedScript(component); script && script->HasProperty(name))
                    return script->Get(name);
            }
            return GetScriptProperty(component, name);
        } else if (kind == ObjectKind::Time) {
            if (name == "deltaTime")
                return static_cast<double>(Time::GetDeltaTime());
            if (name == "unscaledDeltaTime")
                return static_cast<double>(Time::GetUnscaledDeltaTime());
            if (name == "time")
                return Time::GetTime();
            if (name == "unscaledTime")
                return Time::GetUnscaledTime();
            if (name == "frameCount")
                return ToScriptValue(Time::GetFrameCount());
            if (name == "fixedDeltaTime")
                return static_cast<double>(CurrentScene().GetFixedTimeStep());
            if (name == "timeScale")
                return static_cast<double>(Time::GetTimeScale());
        } else if (kind == ObjectKind::Input) {
            if (name == "mousePosition")
                return Input::GetMousePosition();
            if (name == "mouseDelta")
                return Input::GetMouseDelta();
            if (name == "scrollDelta")
                return Input::GetScrollDelta();
            if (name == "gamepadCount")
                return static_cast<double>(Input::GetGamepadCount());
            if (name == "cursorLocked")
                return Input::IsCursorLocked();
        }
        return ScriptObject::Get(name);
    }
    void Set(std::string_view name, const ScriptValue& value) override {
        if (kind == ObjectKind::Entity) {
            auto& entity = RequireEntity();
            if (name == "name") {
                entity.SetName(Argument<std::string>(value));
                return;
            }
            if (name == "active") {
                entity.SetActive(Argument<bool>(value));
                return;
            }
            if (name == "layer") {
                const auto layer = Argument<int>(value);
                if (layer < 0 || layer >= Entity::LayerCount)
                    throw std::runtime_error("Entity layer must be between 0 and 31");
                entity.SetLayer(layer);
                return;
            }
        } else if (kind == ObjectKind::Transform) {
            auto& transform = RequireEntity().GetTransform();
            if (name == "position") {
                transform.SetPosition(Argument<Vector3>(value));
                return;
            }
            if (name == "worldPosition") {
                transform.SetWorldPosition(Argument<Vector3>(value));
                return;
            }
            if (name == "scale") {
                transform.SetScale(Argument<Vector3>(value));
                return;
            }
            if (name == "rotation") {
                transform.SetRotation(Quaternion::FromEuler(Argument<Vector3>(value)));
                return;
            }
            if (name == "worldRotation") {
                transform.SetWorldRotation(Quaternion::FromEuler(Argument<Vector3>(value)));
                return;
            }
            if (name == "rotation2D") {
                transform.SetRotation2D(Argument<float>(value));
                return;
            }
        } else if (kind == ObjectKind::Component) {
            auto& component = RequireComponent();
            if (name == "enabled") {
                component.SetEnabled(Argument<bool>(value));
                return;
            }
            if (name == "active" || name == "faulted" || name == "id" || name == "Entity" || name == "entity" ||
                name == "scene" || name == "transform")
                throw std::runtime_error("Component property is read-only");
            if (!HasScriptProperty(component, name)) {
                if (const auto script = ForwardedScript(component); script && script->HasProperty(name)) {
                    script->Set(name, value);
                    return;
                }
            }
            WriteScriptProperty(component, name, value);
            return;
        } else if (kind == ObjectKind::Time && name == "timeScale") {
            if (!Time::SetTimeScale(Argument<float>(value)))
                throw std::runtime_error("Time scale must be finite and not negative");
            return;
        } else if (kind == ObjectKind::Input && name == "cursorLocked") {
            Input::SetCursorLocked(Argument<bool>(value));
            return;
        }
        ScriptObject::Set(name, value);
    }
    ScriptValue Call(std::string_view name, std::span<const ScriptValue> arguments) override {
        // Getters and setters of described properties, as GetName() and SetName(value).
        if (const auto* member = ScriptApi::FindMember(TypeName(kind), name, ScriptApi::MemberKind::Method);
            member && !member->property.empty()) {
            Count(arguments, member->writable ? 1 : 0);
            if (member->writable) {
                Set(member->property, arguments[0]);
                return {};
            }
            return Get(member->property);
        }
        if (name == "GetScene" &&
            (kind == ObjectKind::Entity || kind == ObjectKind::Transform || kind == ObjectKind::Component)) {
            Count(arguments, 0);
            if (kind == ObjectKind::Component)
                RequireComponent();
            else
                RequireEntity();
            return Wrap(context, ObjectKind::Scene);
        }
        if (name == "IsValid" &&
            (kind == ObjectKind::Entity || kind == ObjectKind::Transform || kind == ObjectKind::Component)) {
            Count(arguments, 0);
            return kind == ObjectKind::Component ? FindComponent() != nullptr : FindEntity() != nullptr;
        }
        if ((kind == ObjectKind::Scene && Contains(SceneStructure, name)) ||
            (kind == ObjectKind::Entity && Contains(EntityStructure, name)))
            return CallStructure(name, arguments);
        if (kind == ObjectKind::Physics && Contains(PhysicsQueries, name))
            return CallPhysics(name, arguments);
        if (kind == ObjectKind::Input && !Contains(KeyQueries, name))
            return CallInput(name, arguments);
        if (kind == ObjectKind::Scene && (name == "FindEntity" || name == "FindEntityWithTag")) {
            Count(arguments, 1);
            const auto query = Argument<std::string>(arguments[0]);
            return WrapEntity(context, name == "FindEntity" ? CurrentScene().FindEntity(query)
                                                            : CurrentScene().FindEntityWithTag(query));
        }
        if (kind == ObjectKind::Entity) {
            auto& entity = RequireEntity();
            if (name == "GetComponent" || name == "HasComponent") {
                if (arguments.size() != 1 && !(name == "GetComponent" && arguments.size() == 2))
                    throw std::runtime_error(
                        "get_component expects a type name and optional index; has_component expects a type name");
                const auto index = arguments.size() == 2 ? Argument<std::uint32_t>(arguments[1]) : 0;
                const auto found = ComponentNamed(Argument<std::string>(arguments[0]), index);
                return name == "HasComponent" ? ScriptValue(!std::holds_alternative<std::monostate>(found)) : found;
            }
            Count(arguments, 1);
            const auto text = Argument<std::string>(arguments[0]);
            if (name == "FindChild")
                return WrapEntity(context, entity.FindChild(text));
            if (name == "HasTag")
                return entity.HasTag(text);
            if (name == "AddTag") {
                entity.AddTag(text);
                return {};
            }
            if (name == "RemoveTag")
                return entity.RemoveTag(text);
        }
        if (kind == ObjectKind::Transform && name == "Translate") {
            Count(arguments, 1);
            RequireEntity().GetTransform().Translate(Argument<Vector3>(arguments[0]));
            return {};
        }
        if (kind == ObjectKind::Component && Contains(CoroutineMethods, name) &&
            !HasScriptMethod(RequireComponent(), name))
            return CallCoroutine(name, arguments);
        if (kind == ObjectKind::Component) {
            auto& component = RequireComponent();
            if (!HasScriptMethod(component, name)) {
                if (const auto script = ForwardedScript(component); script && script->HasMethod(name))
                    return script->Call(name, arguments);
            }
            return CallScriptMethod(component, name, arguments);
        }
        if (kind == ObjectKind::Input && (name == "IsKeyDown" || name == "IsKeyPressed" || name == "IsKeyReleased")) {
            Count(arguments, 1);
            const auto keyName = Argument<std::string>(arguments[0]);
            for (int i = 1; i <= static_cast<int>(Key::RightSuper); ++i) {
                const auto key = static_cast<Key>(i);
                if (GetKeyName(key) == keyName) {
                    if (name == "IsKeyDown")
                        return Input::IsKeyDown(key);
                    if (name == "IsKeyPressed")
                        return Input::IsKeyPressed(key);
                    return Input::IsKeyReleased(key);
                }
            }
            throw std::runtime_error("Unknown key name '" + keyName + "'");
        }
        return ScriptObject::Call(name, arguments);
    }
};

ScriptValue SceneObject::CallInput(std::string_view name, std::span<const ScriptValue> arguments) const {
    if (Contains(MouseQueries, name)) {
        Count(arguments, 1);
        const auto button =
            ParseName<MouseButton>(Argument<std::string>(arguments[0]), 0, static_cast<int>(MouseButton::X2),
                                   GetMouseButtonName, "mouse button");
        if (name == "IsMouseButtonDown")
            return Input::IsMouseButtonDown(button);
        if (name == "IsMouseButtonPressed")
            return Input::IsMouseButtonPressed(button);
        return Input::IsMouseButtonReleased(button);
    }
    // Gamepad queries take an optional zero-based gamepad index; without it they look at every gamepad.
    const auto gamepad = [&](std::size_t index) {
        if (arguments.size() < index + 1 || arguments.size() > index + 2)
            throw std::runtime_error("Incorrect argument count");
        return arguments.size() == index + 2 ? Argument<int>(arguments[index + 1]) : Input::AnyGamepad;
    };
    if (Contains(GamepadQueries, name)) {
        const auto index = gamepad(0);
        const auto button =
            ParseName<GamepadButton>(Argument<std::string>(arguments[0]), 0, static_cast<int>(GamepadButton::Count) - 1,
                                     GetGamepadButtonName, "gamepad button");
        if (name == "IsGamepadButtonDown")
            return Input::IsGamepadButtonDown(button, index);
        if (name == "IsGamepadButtonPressed")
            return Input::IsGamepadButtonPressed(button, index);
        return Input::IsGamepadButtonReleased(button, index);
    }
    if (name == "GetGamepadAxis") {
        const auto index = gamepad(0);
        const auto axis =
            ParseName<GamepadAxis>(Argument<std::string>(arguments[0]), 0, static_cast<int>(GamepadAxis::Count) - 1,
                                   GetGamepadAxisName, "gamepad axis");
        return static_cast<double>(Input::GetGamepadAxis(axis, index));
    }
    Count(arguments, 1);
    const auto action = Argument<std::string>(arguments[0]);
    if (name == "IsActionDown")
        return Input::IsActionDown(action);
    if (name == "IsActionPressed")
        return Input::IsActionPressed(action);
    if (name == "IsActionReleased")
        return Input::IsActionReleased(action);
    if (name == "GetActionValue")
        return static_cast<double>(Input::GetActionValue(action));
    return Input::GetActionVector2(action);
}

template <typename Hit>
ScriptValue SceneObject::MakeHit(const std::optional<Hit>& hit) const {
    if (!hit)
        return {};
    auto record = std::make_shared<ScriptRecord>();
    record->properties.emplace("entity", Wrap(context, ObjectKind::Entity, hit->entity));
    record->properties.emplace("collider",
                               hit->collider ? Wrap(context, ObjectKind::Component, hit->entity, hit->collider->GetId())
                                             : ScriptValue{});
    record->properties.emplace("point", hit->point);
    record->properties.emplace("normal", hit->normal);
    record->properties.emplace("distance", static_cast<double>(hit->distance));
    return ScriptObjectRef(record);
}

ScriptValue SceneObject::CallPhysics(std::string_view name, std::span<const ScriptValue> arguments) const {
    // Raycast(origin, direction, maxDistance[, ignore, layerMask, includeTriggers]); casts take a radius after
    // the origin. Results are hit objects, or null when nothing is met.
    const bool cast = name == "SphereCast" || name == "CircleCast2D";
    const std::size_t required = cast ? 4 : 3;
    if (arguments.size() < required || arguments.size() > required + 3)
        throw std::runtime_error("Incorrect argument count");
    const auto filter = Filter(arguments, required);
    const auto distance = Argument<float>(arguments[required - 1]);
    if (!(distance >= 0))
        throw std::runtime_error("Query distance cannot be negative");
    const auto radius = cast ? Argument<float>(arguments[1]) : 0.0f;
    if (cast && !(radius > 0))
        throw std::runtime_error("Cast radius must be positive");
    if (name == "Raycast" || name == "SphereCast") {
        auto& physics = CurrentScene().GetPhysics();
        const auto origin = Argument<Vector3>(arguments[0]);
        const auto direction = Argument<Vector3>(arguments[cast ? 2 : 1]);
        return MakeHit(cast ? physics.SphereCast(origin, radius, direction, distance, filter)
                            : physics.Raycast(origin, direction, distance, filter));
    }
    auto& physics = CurrentScene().GetPhysics2D();
    const auto origin = Argument<Vector2>(arguments[0]);
    const auto direction = Argument<Vector2>(arguments[cast ? 2 : 1]);
    return MakeHit(cast ? physics.CircleCast(origin, radius, direction, distance, filter)
                        : physics.Raycast(origin, direction, distance, filter));
}

// What On() returns: Cancel() ends the subscription early; `active` tells whether it still exists.
struct SubscriptionObject final : ScriptObject {
    std::uint64_t id = 0;
    static ScriptEventSystem* System() {
        if (!t_activeContext || !t_activeContext->scene)
            throw std::runtime_error("Scene access is only available during script execution");
        return t_activeContext->scene->GetSystem<ScriptEventSystem>();
    }
    bool HasProperty(std::string_view name) const override { return name == "active"; }
    bool HasMethod(std::string_view name) const override { return name == "Cancel" || name == "IsActive"; }
    ScriptValue Get(std::string_view name) const override {
        if (name == "active") {
            const auto* system = System();
            return system && system->Has(id);
        }
        return ScriptObject::Get(name);
    }
    ScriptValue Call(std::string_view name, std::span<const ScriptValue> arguments) override {
        Count(arguments, 0);
        if (name == "IsActive")
            return Get("active");
        if (name == "Cancel") {
            auto* system = System();
            const auto before = system ? system->subscriptions.size() : 0;
            if (system)
                std::erase_if(system->subscriptions, [this](const auto& item) { return item.id == id; });
            return system && system->subscriptions.size() != before;
        }
        return ScriptObject::Call(name, arguments);
    }
};

ScriptValue SceneObject::CallCoroutine(std::string_view name, std::span<const ScriptValue> arguments) {
    if (name.starts_with("Wait")) {
        Count(arguments, 1);
        auto wait = std::make_shared<WaitObject>();
        if (name == "WaitSeconds") {
            wait->wait = {ScriptWait::Kind::Seconds, Argument<double>(arguments[0]), nullptr};
            if (wait->wait.amount < 0)
                throw std::runtime_error("Wait time cannot be negative");
        } else if (name == "WaitFrames") {
            wait->wait = {ScriptWait::Kind::Frames, static_cast<double>(Argument<std::uint32_t>(arguments[0])),
                          nullptr};
        } else {
            const auto* condition = std::get_if<ScriptObjectRef>(&arguments[0]);
            if (!condition || !*condition || !(*condition)->IsCallable())
                throw std::runtime_error("WaitUntil expects a function returning a boolean");
            wait->wait = {ScriptWait::Kind::Condition, 0, *condition};
        }
        return ScriptObjectRef(wait);
    }
    auto* script = dynamic_cast<ScriptComponent*>(&RequireComponent());
    if (!script)
        throw std::runtime_error("Coroutines run on script components");
    if (name == "StartCoroutine") {
        Count(arguments, 1);
        const auto* function = std::get_if<ScriptObjectRef>(&arguments[0]);
        if (!function || !*function || !(*function)->IsCallable())
            throw std::runtime_error("StartCoroutine expects a function");
        return ToScriptValue(static_cast<std::uint64_t>(script->StartScriptCoroutine(*function)));
    }
    if (name == "StopCoroutines") {
        Count(arguments, 0);
        script->CancelCoroutines();
        return {};
    }
    Count(arguments, 1);
    const auto id = static_cast<TaskId>(Argument<std::uint64_t>(arguments[0]));
    if (name == "StopCoroutine")
        return script->CancelCoroutine(id);
    return script->IsCoroutineRunning(id);
}

ScriptValue SceneObject::CallStructure(std::string_view name, std::span<const ScriptValue> arguments) {
    auto& scene = CurrentScene();
    const auto eventTarget = kind == ObjectKind::Entity ? RequireEntity().GetId() : EntityId::Invalid;
    if (name == "On") {
        // On(name, callback) subscribes the running script's component.
        Count(arguments, 2);
        auto eventName = Argument<std::string>(arguments[0]);
        const auto* callback = std::get_if<ScriptObjectRef>(&arguments[1]);
        if (!callback || !*callback || !(*callback)->IsCallable())
            throw std::runtime_error("Expected a function to call for the event");
        if (!t_activeContext || !t_activeContext->deliver)
            throw std::runtime_error("Only a running script component can subscribe to events");
        auto* system = scene.GetSystem<ScriptEventSystem>();
        if (!system)
            system = &scene.AddSystem<ScriptEventSystem>();
        system->Prune();
        auto subscription = std::make_shared<SubscriptionObject>();
        subscription->id = system->nextId++;
        system->subscriptions.push_back(
            {subscription->id, eventTarget, std::move(eventName), t_activeContext->weak_from_this(), *callback});
        return ScriptObjectRef(subscription);
    }
    if (name == "Emit") {
        // Emit(name, arguments...) returns how many callbacks ran.
        if (arguments.empty())
            throw std::runtime_error("Incorrect argument count");
        return static_cast<double>(
            DispatchEvent(scene, eventTarget, Argument<std::string>(arguments[0]), arguments.subspan(1)));
    }
    const auto optionalName = [&](std::size_t index) {
        if (arguments.size() > index + 1)
            throw std::runtime_error("Incorrect argument count");
        return arguments.size() == index + 1 ? Argument<std::string>(arguments[index]) : std::string();
    };
    if (name == "CreateEntity")
        return Wrap(context, ObjectKind::Entity, scene.CreateEntity(optionalName(0)).GetId());
    if (name == "Instantiate") {
        // Instantiate(prefabPath[, parent]) returns the prefab's root entity.
        if (arguments.empty() || arguments.size() > 2)
            throw std::runtime_error("Incorrect argument count");
        const auto path = Argument<std::string>(arguments[0]);
        const auto prefab = Assets::Load<Prefab>(path);
        if (!prefab)
            throw std::runtime_error("Could not load prefab '" + path + "'");
        InstantiateOptions options;
        options.parent = arguments.size() == 2 ? EntityFromArgument(arguments[1]) : nullptr;
        return WrapEntity(context, scene.Instantiate(prefab, options));
    }
    if (name == "DestroyEntity") {
        Count(arguments, 1);
        if (auto* entity = EntityFromArgument(arguments[0]))
            scene.DestroyEntity(*entity);
        return {};
    }
    auto& entity = RequireEntity();
    if (name == "GetScript") {
        // GetScript([name]) returns the first started script of the entity, or the first named so.
        if (arguments.size() > 1)
            throw std::runtime_error("Incorrect argument count");
        const auto wanted = arguments.empty() ? std::string() : Argument<std::string>(arguments[0]);
        ScriptObjectRef found;
        entity.ForEachComponent<ScriptComponent>([&](ScriptComponent& script) {
            if (!found && (wanted.empty() || script.GetScriptName() == wanted))
                found = script.GetScriptObject();
        });
        return found;
    }
    if (name == "Destroy") {
        Count(arguments, 0);
        scene.DestroyEntity(entity);
        return {};
    }
    if (name == "CreateChild")
        return Wrap(context, ObjectKind::Entity, entity.CreateChild(optionalName(0)).GetId());
    if (name == "SetParent") {
        // SetParent(parent or null[, keepWorldPose]).
        if (arguments.empty() || arguments.size() > 2)
            throw std::runtime_error("Incorrect argument count");
        auto* parent = EntityFromArgument(arguments[0]);
        for (auto* ancestor = parent; ancestor; ancestor = ancestor->GetParent()) {
            if (ancestor == &entity)
                throw std::runtime_error("An entity cannot become a child of itself or its descendants");
        }
        entity.SetParent(parent, arguments.size() == 2 && Argument<bool>(arguments[1]));
        return {};
    }
    if (name == "AddComponent") {
        Count(arguments, 1);
        const auto typeName = Argument<std::string>(arguments[0]);
        auto component = CreateComponent(typeName);
        if (!component)
            throw std::runtime_error("Unknown component type '" + typeName + "'");
        auto& added = entity.AddComponent(std::move(component));
        return Wrap(context, ObjectKind::Component, entityId, added.GetId());
    }
    // RemoveComponent(component reference or type name) returns whether one was removed.
    Count(arguments, 1);
    Component* target = nullptr;
    if (const auto* typeName = std::get_if<std::string>(&arguments[0])) {
        entity.ForEachComponent<Component>([&](Component& component) {
            if (!target && GetComponentTypeName(component) == *typeName)
                target = &component;
        });
    } else {
        const auto* reference = std::get_if<ScriptObjectRef>(&arguments[0]);
        const auto object = reference ? std::dynamic_pointer_cast<SceneObject>(*reference) : nullptr;
        if (!object || object->kind != ObjectKind::Component)
            throw std::runtime_error("Expected a component reference or type name");
        if (object->entityId == entityId)
            target = object->FindComponent();
    }
    if (!target)
        return false;
    entity.RemoveComponent(*target);
    return true;
}

ScriptValue Wrap(const std::shared_ptr<ScriptSceneContext>& context, ObjectKind kind, EntityId entity,
                 ComponentId component) {
    const auto key = std::to_string(static_cast<int>(kind)) + ":" + std::to_string(static_cast<std::uint64_t>(entity)) +
                     ":" + component.ToString();
    if (const auto found = context->objects.find(key); found != context->objects.end()) {
        if (auto existing = found->second.lock())
            return existing;
    }
    auto object = std::make_shared<SceneObject>();
    object->context = context;
    object->kind = kind;
    object->entityId = entity;
    object->componentId = component;
    context->objects.insert_or_assign(key, object);
    return ScriptObjectRef(object);
}

template <typename CollisionType>
ScriptValue MakeCollision(const std::shared_ptr<ScriptSceneContext>& context, const CollisionType& collision) {
    auto record = std::make_shared<ScriptRecord>();
    record->properties.emplace("other", Wrap(context, ObjectKind::Entity, collision.other));
    record->properties.emplace("normal", collision.normal);
    record->properties.emplace("relativeVelocity", collision.relativeVelocity);
    record->properties.emplace("impulse", static_cast<double>(collision.impulse));
    record->properties.emplace("pointCount", static_cast<double>(collision.pointCount));
    for (const auto& point : collision.GetPoints())
        record->points.emplace_back(point);
    record->properties.emplace("point", record->points.empty() ? ScriptValue{} : record->points.front());
    return ScriptObjectRef(record);
}
template <typename ContactType>
ScriptValue MakeTrigger(const std::shared_ptr<ScriptSceneContext>& context, const ContactType& contact) {
    auto record = std::make_shared<ScriptRecord>();
    record->properties.emplace("other", Wrap(context, ObjectKind::Entity, contact.other));
    record->properties.emplace("isTrigger", contact.isTrigger);
    return ScriptObjectRef(record);
}

template <typename T>
void PropertyMethods(std::string_view property, std::string_view getter, std::string_view setter = {}) {
    SetScriptMethod(
        typeid(T), std::string(getter),
        [name = std::string(property)](Component& component, std::span<const ScriptValue> arguments) {
            Count(arguments, 0);
            return GetScriptProperty(component, name);
        },
        GetScriptPropertyType(typeid(T), property));
    if (!setter.empty())
        SetScriptMethod(
            typeid(T), std::string(setter),
            [name = std::string(property)](Component& component,
                                           std::span<const ScriptValue> arguments) -> ScriptValue {
                Count(arguments, 1);
                WriteScriptProperty(component, name, arguments[0]);
                return {};
            },
            "");
}

void RegisterBuiltins() {
    ScriptBindings<SpriteRenderer>(BuiltinScriptBindingsTag{})
        .Property("color", &SpriteRenderer::GetColor, &SpriteRenderer::SetColor)
        .Property("size", &SpriteRenderer::GetSize,
                  [](SpriteRenderer& sprite, Vector2 value) {
                      if (value.x < 0 || value.y < 0)
                          throw std::runtime_error("Sprite size cannot be negative");
                      sprite.SetSize(value);
                  })
        .Property("frame", &SpriteRenderer::GetFrame, &SpriteRenderer::SetFrame)
        .Property("flippedX", &SpriteRenderer::IsFlippedX, &SpriteRenderer::SetFlippedX)
        .Property("flippedY", &SpriteRenderer::IsFlippedY, &SpriteRenderer::SetFlippedY)
        .Property("order", &SpriteRenderer::GetOrder, &SpriteRenderer::SetOrder);
    ScriptBindings<Camera>(BuiltinScriptBindingsTag{})
        .Property(
            "projection",
            [](const Camera& camera) {
                return std::string(camera.GetProjection() == CameraProjection::Perspective ? "Perspective"
                                                                                           : "Orthographic");
            },
            [](Camera& camera, const std::string& value) {
                if (value != "Perspective" && value != "Orthographic")
                    throw std::runtime_error("Unknown camera projection");
                camera.SetProjection(value == "Perspective" ? CameraProjection::Perspective
                                                            : CameraProjection::Orthographic);
            })
        .Property("fieldOfView", &Camera::GetFieldOfView,
                  [](Camera& camera, float value) {
                      if (value <= 0 || value >= ToRadians(180.0f))
                          throw std::runtime_error("Field of view must be between 0 and pi radians");
                      camera.SetFieldOfView(value);
                  })
        .Property("orthographicSize", &Camera::GetOrthographicSize,
                  [](Camera& camera, float value) {
                      if (value <= 0)
                          throw std::runtime_error("Orthographic size must be positive");
                      camera.SetOrthographicSize(value);
                  })
        .Property("nearPlane", &Camera::GetNearPlane,
                  [](Camera& camera, float value) {
                      if (value <= 0 || value >= camera.GetFarPlane())
                          throw std::runtime_error("Near plane must be positive and below far plane");
                      camera.SetNearPlane(value);
                  })
        .Property("farPlane", &Camera::GetFarPlane,
                  [](Camera& camera, float value) {
                      if (value <= camera.GetNearPlane())
                          throw std::runtime_error("Far plane must exceed near plane");
                      camera.SetFarPlane(value);
                  })
        .Property("background", &Camera::GetBackground, &Camera::SetBackground);
    ScriptBindings<AudioSource>(BuiltinScriptBindingsTag{})
        .Property(
            "volume", [](const AudioSource& audio) { return audio.volume; },
            [](AudioSource& audio, float value) {
                if (value < 0)
                    throw std::runtime_error("Volume cannot be negative");
                audio.volume = value;
            })
        .Property(
            "pitch", [](const AudioSource& audio) { return audio.pitch; },
            [](AudioSource& audio, float value) {
                if (value <= 0)
                    throw std::runtime_error("Pitch must be positive");
                audio.pitch = value;
            })
        .Field("loop", &AudioSource::loop)
        .Field("spatial", &AudioSource::spatial)
        .Field("playOnStart", &AudioSource::playOnStart)
        .Field("bus", &AudioSource::bus)
        .ReadOnly("playing", &AudioSource::IsPlaying)
        .ReadOnly("paused", &AudioSource::IsPaused)
        .Method("Play", &AudioSource::Play)
        .Method("Pause", &AudioSource::Pause)
        .Method("Stop", &AudioSource::Stop);
    PropertyMethods<SpriteRenderer>("color", "GetColor", "SetColor");
    PropertyMethods<SpriteRenderer>("size", "GetSize", "SetSize");
    PropertyMethods<SpriteRenderer>("frame", "GetFrame", "SetFrame");
    PropertyMethods<SpriteRenderer>("flippedX", "IsFlippedX", "SetFlippedX");
    PropertyMethods<SpriteRenderer>("flippedY", "IsFlippedY", "SetFlippedY");
    PropertyMethods<SpriteRenderer>("order", "GetOrder", "SetOrder");
    PropertyMethods<Camera>("projection", "GetProjection", "SetProjection");
    PropertyMethods<Camera>("fieldOfView", "GetFieldOfView", "SetFieldOfView");
    PropertyMethods<Camera>("orthographicSize", "GetOrthographicSize", "SetOrthographicSize");
    PropertyMethods<Camera>("nearPlane", "GetNearPlane", "SetNearPlane");
    PropertyMethods<Camera>("farPlane", "GetFarPlane", "SetFarPlane");
    PropertyMethods<Camera>("background", "GetBackground", "SetBackground");
    PropertyMethods<AudioSource>("playing", "IsPlaying");
    PropertyMethods<AudioSource>("paused", "IsPaused");
}

} // namespace

void EnsureBuiltinScriptBindings() {
    static std::once_flag builtins;
    std::call_once(builtins, RegisterBuiltins);
}

ScriptValue MakeScriptContact(const std::shared_ptr<ScriptSceneContext>& context, const Collision& collision) {
    return MakeCollision(context, collision);
}
ScriptValue MakeScriptContact(const std::shared_ptr<ScriptSceneContext>& context, const TriggerContact& contact) {
    return MakeTrigger(context, contact);
}
ScriptValue MakeScriptContact(const std::shared_ptr<ScriptSceneContext>& context, const Collision2D& collision) {
    return MakeCollision(context, collision);
}
ScriptValue MakeScriptContact(const std::shared_ptr<ScriptSceneContext>& context, const TriggerContact2D& contact) {
    return MakeTrigger(context, contact);
}

std::shared_ptr<ScriptSceneContext> CreateScriptSceneContext() {
    EnsureBuiltinScriptBindings();
    return std::make_shared<ScriptSceneContext>();
}
void BindSceneScript(ScriptRuntime& runtime, const std::shared_ptr<ScriptSceneContext>& context, EntityId owner,
                     ComponentId component) {
    context->owner = owner;
    context->ownerComponent = component;
    struct SceneLibrary final : ScriptObject {
        std::unordered_map<std::string, ScriptValue> exports;
        bool HasMethod(std::string_view) const override { return false; }
        ScriptValue Get(std::string_view name) const override {
            const auto found = exports.find(std::string(name));
            if (found != exports.end())
                return found->second;
            return ScriptObject::Get(name);
        }
    };
    auto library = std::make_shared<SceneLibrary>();
    library->exports.emplace(ScriptApi::Component, Wrap(context, ObjectKind::Component, owner, component));
    library->exports.emplace(ScriptApi::Entity, Wrap(context, ObjectKind::Entity, owner));
    library->exports.emplace(ScriptApi::Scene, Wrap(context, ObjectKind::Scene));
    library->exports.emplace(ScriptApi::Input, Wrap(context, ObjectKind::Input));
    library->exports.emplace(ScriptApi::Time, Wrap(context, ObjectKind::Time));
    library->exports.emplace(ScriptApi::Physics, Wrap(context, ObjectKind::Physics));
    runtime.BindLibrary(ScriptApi::Module, library);
}
ScriptWait ReadScriptWait(const ScriptValue& value) {
    if (std::holds_alternative<std::monostate>(value))
        return {};
    const auto* reference = std::get_if<ScriptObjectRef>(&value);
    const auto wait = reference ? std::dynamic_pointer_cast<WaitObject>(*reference) : nullptr;
    if (!wait)
        throw std::runtime_error("A coroutine yields null or WaitSeconds(), WaitFrames() or WaitUntil()");
    return wait->wait;
}

void SetScriptEventDelivery(ScriptSceneContext& context, std::function<void(const std::function<void()>&)> deliver) {
    context.deliver = std::move(deliver);
}

ScriptSceneScope::ScriptSceneScope(ScriptSceneContext& context, Scene& scene, Component* component)
    : m_context(context), m_previousScene(context.scene), m_previousComponent(context.component),
      m_previousActive(t_activeContext) {
    if (context.scene && (context.scene != &scene || context.component != component))
        throw std::logic_error("Scene script scope is already active for another context");
    if (!context.scene)
        std::erase_if(context.objects, [](const auto& item) { return item.second.expired(); });
    context.scene = &scene;
    context.component = component;
    t_activeContext = &context;
}
ScriptSceneScope::~ScriptSceneScope() {
    m_context.scene = m_previousScene;
    m_context.component = m_previousComponent;
    t_activeContext = m_previousActive;
}

} // namespace Spot::Detail

namespace Spot {

std::size_t EmitScriptEvent(Scene& scene, std::string_view name, std::span<const ScriptValue> arguments) {
    return Detail::DispatchEvent(scene, EntityId::Invalid, name, arguments);
}
std::size_t EmitScriptEvent(Entity& entity, std::string_view name, std::span<const ScriptValue> arguments) {
    return Detail::DispatchEvent(entity.GetScene(), entity.GetId(), name, arguments);
}

} // namespace Spot
