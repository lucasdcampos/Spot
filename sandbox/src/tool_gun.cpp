#include "tool_gun.h"

#include "player_controller.h"
#include "prop_carrier.h"
#include "sound_bank.h"
#include "undo_list.h"
#include "weapon_holder.h"

#include <algorithm>
#include <array>
#include <string_view>

SPOT_COMPONENT(ToolGun);

namespace {

Spot::Vector3 ToLocal(const Spot::Transform& transform, Spot::Vector3 point) {
    const Spot::Quaternion rotation = transform.GetWorldRotation();
    const Spot::Quaternion inverse{-rotation.x, -rotation.y, -rotation.z, rotation.w};
    const Spot::Vector3 local = inverse * (point - transform.GetWorldPosition());
    const Spot::Vector3 scale = transform.GetScale();
    return {local.x / scale.x, local.y / scale.y, local.z / scale.z};
}

Spot::Vector3 ToWorld(const Spot::Transform& transform, Spot::Vector3 point) {
    const Spot::Vector4 world = transform.GetWorldMatrix() * Spot::Vector4{point.x, point.y, point.z, 1.0f};
    return {world.x, world.y, world.z};
}

bool IsProp(const Spot::Entity& entity) {
    return entity.GetComponent<Spot::Rigidbody>() != nullptr;
}

} // namespace

void ToolGun::Click(Spot::Entity& entity, Spot::Vector3 point, Spot::Vector3 normal) {
    if (mode == Mode::Remover) {
        if (!RemoveLampNear(point)) {
            Remove(entity);
        }
        return;
    }
    if (mode == Mode::Lamp) {
        PlaceLamp(entity, point, normal);
        return;
    }
    if (mode == Mode::Color) {
        Paint(entity, Palette[static_cast<std::size_t>(color) % Palette.size()].color);
        return;
    }
    if (mode == Mode::Material) {
        if (!materials.empty()) {
            ApplyMaterial(entity, materials[static_cast<std::size_t>(material) % materials.size()].material);
        }
        return;
    }
    if (!m_first) {
        // The first click must be on a prop, which the joint goes on.
        if (IsProp(entity)) {
            m_first = Pick{entity.GetId(), point};
        }
        return;
    }
    const Pick first = *m_first;
    m_first.reset();
    Spot::Entity* firstEntity = GetScene().GetEntity(first.entity);
    if (!firstEntity || firstEntity == &entity) {
        return;
    }
    if (mode == Mode::Weld) {
        Weld(*firstEntity, entity);
    } else {
        Rope(*firstEntity, first.point, entity, point);
    }
}

Spot::FixedJoint* ToolGun::Weld(Spot::Entity& first, Spot::Entity& second) {
    if (!IsProp(first) || &first == &second) {
        return nullptr;
    }
    auto& joint = first.AddComponent<Spot::FixedJoint>();
    joint.SetConnectedEntity(second.GetId());
    if (auto* undo = GetEntity().GetComponent<UndoList>()) {
        undo->PushJoint(first.GetId(), first.GetComponentIndex(joint));
    }
    return &joint;
}

Spot::DistanceJoint* ToolGun::Rope(Spot::Entity& first, Spot::Vector3 firstPoint, Spot::Entity& second,
                                   Spot::Vector3 secondPoint) {
    if (!IsProp(first) || &first == &second) {
        return nullptr;
    }
    auto& joint = first.AddComponent<Spot::DistanceJoint>();
    joint.SetAnchor(ToLocal(first.GetTransform(), firstPoint));
    joint.SetConnectedEntity(second.GetId());
    joint.SetConnectedAnchor(ToLocal(second.GetTransform(), secondPoint));
    joint.SetDistance(0.0f, Spot::Distance(firstPoint, secondPoint));
    if (auto* undo = GetEntity().GetComponent<UndoList>()) {
        undo->PushJoint(first.GetId(), first.GetComponentIndex(joint));
    }
    return &joint;
}

bool ToolGun::Remove(Spot::Entity& entity) {
    if (!IsProp(entity)) {
        return false;
    }
    if (auto* undo = GetEntity().GetComponent<UndoList>()) {
        undo->PushRemoval(entity);
    }
    entity.Destroy();
    return true;
}

bool ToolGun::Paint(Spot::Entity& entity, Spot::Color paint) {
    auto* renderer = entity.GetComponent<Spot::MeshRenderer>();
    if (!IsProp(entity) || !renderer) {
        return false;
    }
    const Spot::AssetRef<Spot::Material> previous = renderer->GetMaterial();
    Spot::Material painted = previous ? *previous : Spot::Material{};
    // Glass keeps how much it lets through.
    painted.baseColor = {paint.r, paint.g, paint.b, painted.baseColor.a};
    renderer->SetMaterial(std::move(painted));
    if (auto* undo = GetEntity().GetComponent<UndoList>()) {
        undo->PushMaterial(entity.GetId(), previous);
    }
    return true;
}

bool ToolGun::ApplyMaterial(Spot::Entity& entity, const Spot::AssetRef<Spot::Material>& newMaterial) {
    auto* renderer = entity.GetComponent<Spot::MeshRenderer>();
    if (!IsProp(entity) || !renderer || !newMaterial) {
        return false;
    }
    const Spot::AssetRef<Spot::Material> previous = renderer->GetMaterial();
    renderer->SetMaterial(newMaterial);
    if (auto* undo = GetEntity().GetComponent<UndoList>()) {
        undo->PushMaterial(entity.GetId(), previous);
    }
    return true;
}

Spot::Color ToolGun::GetLampColor() const {
    const Spot::Color paint = Palette[static_cast<std::size_t>(color) % Palette.size()].color;
    const float brightest = std::max({paint.r, paint.g, paint.b});
    if (brightest < 0.1f) {
        return Spot::Color::White;
    }
    return {paint.r / brightest, paint.g / brightest, paint.b / brightest, 1.0f};
}

Spot::Entity& ToolGun::PlaceLamp(Spot::Entity& surface, Spot::Vector3 point, Spot::Vector3 normal) {
    const Spot::Vector3 out = Spot::LengthSquared(normal) > 1e-6f ? Spot::Normalize(normal) : Spot::Vector3{0, 1, 0};
    // On a prop, the lamp moves with it; on the level, it stands on its own.
    Spot::Entity& placed = IsProp(surface) ? surface.CreateChild("Lamp") : GetScene().CreateEntity("Lamp");
    placed.AddTag(LampTag);
    placed.GetTransform().SetWorldPosition(point + out * 0.12f);
    // Lights shine down their entity's -Z, out of the surface.
    placed.GetTransform().SetWorldRotation(Spot::Quaternion::LookRotation(out));
    const Spot::Color light = GetLampColor();
    if (lamp == LampKind::Spot || lamp == LampKind::ShadowedSpot) {
        auto& spot = placed.AddComponent<Spot::SpotLight>(light, 25.0f, 12.0f);
        spot.SetInnerConeAngle(Spot::ToRadians(25.0f));
        spot.SetOuterConeAngle(Spot::ToRadians(35.0f));
        spot.SetCastingShadows(lamp == LampKind::ShadowedSpot);
    } else {
        placed.AddComponent<Spot::PointLight>(light, 6.0f, 7.0f).SetCastingShadows(lamp == LampKind::ShadowedPoint);
    }
    // The bulb glows in the light's color, and is lit from within, so it casts no shadow of its own.
    auto& bulb = placed.AddComponent<Spot::MeshRenderer>(
        Spot::Assets::Load<Spot::Mesh>(Spot::BuiltInAssets::SphereMesh),
        Spot::Material{.baseColor = Spot::Color::Black,
                       .emissive = {light.r * 4.0f, light.g * 4.0f, light.b * 4.0f, 1.0f}});
    bulb.SetCastingShadows(false);
    placed.GetTransform().SetScale({0.12f, 0.12f, 0.12f});
    if (auto* undo = GetEntity().GetComponent<UndoList>()) {
        undo->PushSpawn(placed.GetId());
    }
    return placed;
}

bool ToolGun::RemoveLampNear(Spot::Vector3 point) {
    Spot::Entity* nearest = nullptr;
    float nearestDistance = 0.35f;
    for (Spot::Entity* candidate : GetScene().FindEntitiesWithTag(LampTag)) {
        const float distance = Spot::Distance(candidate->GetTransform().GetWorldPosition(), point);
        if (distance < nearestDistance) {
            nearest = candidate;
            nearestDistance = distance;
        }
    }
    if (!nearest) {
        return false;
    }
    if (auto* undo = GetEntity().GetComponent<UndoList>()) {
        undo->PushRemoval(*nearest);
    }
    nearest->Destroy();
    return true;
}

bool ToolGun::IsInHand() const {
    const auto* holder = GetEntity().GetComponent<WeaponHolder>();
    return holder && holder->IsToolGunSelected();
}

void ToolGun::OnUpdate(float /*deltaTime*/) {
    namespace Input = Spot::Input;
    if (!IsInHand()) {
        m_first.reset();
        return;
    }
    if (Input::IsActionPressed("Reload")) {
        SoundBank::PlayClick(GetScene());
        mode = static_cast<Mode>((static_cast<int>(mode) + 1) % 6);
        m_first.reset();
    }
    if (Input::IsActionPressed("Aim") && (mode == Mode::Color || mode == Mode::Material || mode == Mode::Lamp)) {
        SoundBank::PlayClick(GetScene());
        if (mode == Mode::Color) {
            color = (color + 1) % static_cast<int>(Palette.size());
        } else if (mode == Mode::Lamp) {
            lamp = static_cast<LampKind>((static_cast<int>(lamp) + 1) % static_cast<int>(LampNames.size()));
        } else {
            material = (material + 1) % std::max(static_cast<int>(materials.size()), 1);
        }
    }
    const auto* player = GetEntity().GetComponent<PlayerController>();
    const auto* carrier = GetEntity().GetComponent<PropCarrier>();
    if (!player || !Input::IsCursorLocked() || !Input::IsActionPressed("Fire") || (carrier && carrier->HasClick())) {
        return;
    }
    const auto hit = GetScene().GetPhysics().Raycast(player->GetEyePosition(), player->GetAimDirection(), range,
                                                     {.ignore = GetEntity().GetId()});
    if (Spot::Entity* entity = hit ? GetScene().GetEntity(hit->entity) : nullptr) {
        Click(*entity, hit->point, hit->normal);
    }
}

void ToolGun::OnEnable() {
    m_ropeHook =
        GetScene().AddRenderHook(Spot::RenderStage::AfterOpaque,
                                 [this](const Spot::RenderStageContext& stage) { m_ropeLines.Draw(stage, m_ropes); });
}

void ToolGun::OnDisable() {
    GetScene().RemoveRenderHook(m_ropeHook);
    m_ropes.clear();
}

void ToolGun::OnLateUpdate(float /*deltaTime*/) {
    // Ropes, until the rendering milestones draw them as more than lines.
    m_ropes.clear();
    GetScene().ForEachComponent<Spot::DistanceJoint>([this](const Spot::DistanceJoint& joint) {
        const Spot::Entity* connected = GetScene().GetEntity(joint.GetConnectedEntity());
        const Spot::Vector3 from = ToWorld(joint.GetEntity().GetTransform(), joint.GetAnchor());
        const Spot::Vector3 to =
            connected ? ToWorld(connected->GetTransform(), joint.GetConnectedAnchor()) : joint.GetConnectedAnchor();
        m_ropes.push_back({.from = from, .to = to, .color = {0.55f, 0.4f, 0.25f, 1.0f}});
    });
}

void ToolGun::ToolMaterial::Serialize(Spot::Archive& archive) {
    archive.Field("name", name);
    archive.Field("material", material);
}

void ToolGun::Serialize(Spot::Archive& archive) {
    static constexpr std::array<std::string_view, 6> Modes = {"Weld", "Rope", "Remover", "Color", "Material", "Lamp"};
    archive.Field("mode", mode, {.enumNames = Modes});
    archive.Field("lamp", lamp, {.enumNames = LampNames, .tooltip = "The lamp Lamp mode places"});
    archive.Field("color", color, {.min = 0.0f, .max = 7.0f, .tooltip = "The palette color Color mode paints in"});
    archive.Field("materials", materials, {.tooltip = "The materials Material mode gives, which Aim goes through"});
    archive.Field("material", material, {.min = 0.0f, .max = 64.0f, .tooltip = "The one Material mode gives now"});
    archive.Field("range", range, {.min = 0.0f, .max = 1000.0f, .speed = 0.5f});
}
