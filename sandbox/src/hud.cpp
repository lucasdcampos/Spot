#include "hud.h"

#include "day_cycle.h"
#include "physics_gun.h"
#include "player_controller.h"
#include "player_movement.h"
#include "prop_carrier.h"
#include "tool_gun.h"
#include "weapon.h"
#include "weapon_holder.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <format>
#include <string_view>

SPOT_COMPONENT(Hud);

namespace {

// The average milliseconds per frame that the profiled frames spent in scopes of that name.
double GetAverageMilliseconds(std::string_view scope) {
    const auto frames = Spot::Profiler::GetFrames();
    if (frames.empty()) {
        return 0.0;
    }
    std::chrono::nanoseconds total{};
    for (const Spot::ProfileFrame* frame : frames) {
        for (const Spot::ProfileSample& sample : frame->samples) {
            total += sample.name == scope ? sample.duration : std::chrono::nanoseconds{};
        }
    }
    return std::chrono::duration<double, std::milli>(total).count() / static_cast<double>(frames.size());
}

std::string Upper(std::string_view text) {
    std::string upper(text);
    std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return upper;
}

constexpr const char* ToolModes[] = {"Weld", "Rope", "Remover", "Color", "Material", "Lamp"};

} // namespace

void Hud::OnUpdate(float deltaTime) {
    const float elapsed = Spot::Time::GetUnscaledDeltaTime() > 0.0f ? Spot::Time::GetUnscaledDeltaTime() : deltaTime;
    if (elapsed > 0.0f) {
        m_frameRate = m_frameRate > 0.0f ? std::lerp(m_frameRate, 1.0f / elapsed, 0.1f) : 1.0f / elapsed;
    }
    if (Spot::Input::IsActionPressed("Statistics")) {
        m_showStatistics = !m_showStatistics;
        UpdateStatus();
    }
    m_slotsLeft = std::max(m_slotsLeft - elapsed, 0.0f);
}

void Hud::OnStart() {
    if (auto* existing = GetEntity().GetComponentInChildren<RuntimeInterface>())
        m_interface = existing->GetEntity().GetId();
    else if (const auto prefab = Spot::Assets::Load<Spot::Prefab>("prefabs/interface.prefab")) {
        if (auto* root = GetScene().Instantiate(prefab, {.parent = &GetEntity()}))
            m_interface = root->GetId();
    }
    UpdateStatus();
    InvokeRepeating(0.5f, 0.5f, [this] { UpdateStatus(); });
}

void Hud::OnEnable() {
    if (auto* root = GetScene().GetEntity(m_interface))
        root->SetActive(true);
}
void Hud::OnDisable() {
    if (auto* root = GetScene().GetEntity(m_interface))
        root->SetActive(false);
}
void Hud::OnDestroy() {
    if (auto* root = GetScene().GetEntity(m_interface))
        root->Destroy();
}

void Hud::OnLateUpdate(float) {
    m_state.statistics = m_status;
    m_state.showStatistics = m_showStatistics;
    const DayCycle* day = GetScene().FindComponent<DayCycle>();
    const int minutes = day ? static_cast<int>(day->timeOfDay * 24.0f * 60.0f) % (24 * 60) : 0;
    m_state.clock = std::format("{:02}:{:02}", minutes / 60, minutes % 60);
    DescribeHand();
    auto* root = GetScene().GetEntity(m_interface);
    if (auto* ui = root ? root->GetComponent<RuntimeInterface>() : nullptr)
        ui->UpdateHud(m_state);
}

void Hud::UpdateStatus() {
    m_status = std::format("{} FPS   {} bodies   update {:.2f} ms   render {:.2f} ms",
                           static_cast<int>(std::lround(m_frameRate)), GetScene().GetPhysics().GetBodyCount(),
                           GetAverageMilliseconds("Scene::Update"), GetAverageMilliseconds("Application::Render"));
}

void Hud::DescribeHand() {
    HudState& state = m_state;
    state.count.clear();
    state.detail.clear();
    state.hints.clear();
    state.prompt.clear();
    state.countIsWord = false;
    state.lowAmmo = false;
    state.reload = -1.0f;
    state.crosshair = true;
    state.hitMarker = 0.0f;
    state.item.clear();
    const WeaponHolder* holder = GetScene().FindComponent<WeaponHolder>();
    if (!holder) {
        state.slotsOpacity = 0.0f;
        return;
    }
    const Spot::Entity& player = holder->GetEntity();
    const auto* controller = player.GetComponent<PlayerController>();
    const auto* movement = player.GetComponent<PlayerMovement>();
    const auto* body = player.GetComponent<Spot::CharacterController>();

    // The slots, named as briefly as the bar has room for.
    state.slots.clear();
    state.slots.emplace_back("Physics Gun");
    for (const Weapon* weapon : holder->GetWeapons()) {
        state.slots.push_back(weapon->displayName);
    }
    state.slots.emplace_back("Tool Gun");
    state.selectedSlot = holder->GetSelected();
    if (state.selectedSlot != m_lastSlot) {
        // Not when the game starts, only when the player switches.
        m_slotsLeft = m_lastSlot < 0 ? 0.0f : slotsTime;
        m_lastSlot = state.selectedSlot;
    }
    state.slotsOpacity = std::clamp(m_slotsLeft / 0.4f, 0.0f, 1.0f);

    // The crosshair opens up while moving, in the air and as recoil kicks.
    float spread = 0.0f;
    if (body && movement) {
        const Spot::Vector3 velocity = body->GetVelocity();
        spread += std::min(Spot::Length(Spot::Vector3{velocity.x, 0.0f, velocity.z}) / movement->runSpeed, 1.0f) * 0.5f;
        spread += body->IsGrounded() || movement->IsFlying() ? 0.0f : 0.35f;
    }
    if (controller) {
        spread += controller->GetRecoil() * 8.0f;
    }
    state.spread += (std::min(spread, 1.0f) - state.spread) * 0.25f;
    state.hitMarker = holder->GetHitMarker();

    const auto* carrier = player.GetComponent<PropCarrier>();
    const auto* gun = player.GetComponent<PhysicsGun>();
    if (carrier && carrier->IsHolding()) {
        state.prompt = "LMB  Throw      E  Drop      RMB  Rotate";
    } else if (!(gun && gun->IsGrabbing()) && carrier && carrier->FindCarryable()) {
        state.prompt = "E  Pick up";
    }

    if (holder->IsToolGunSelected()) {
        const auto* tool = player.GetComponent<ToolGun>();
        state.item = "TOOL GUN";
        state.countIsWord = true;
        if (!tool) {
            return;
        }
        state.count = ToolModes[static_cast<int>(tool->mode)];
        if (tool->mode == ToolGun::Mode::Color) {
            state.detail = ToolGun::Palette[static_cast<std::size_t>(tool->color) % ToolGun::Palette.size()].name;
        } else if (tool->mode == ToolGun::Mode::Material && !tool->materials.empty()) {
            state.detail = tool->materials[static_cast<std::size_t>(tool->material) % tool->materials.size()].name;
        } else if (tool->mode == ToolGun::Mode::Lamp) {
            state.detail = ToolGun::LampNames[static_cast<std::size_t>(tool->lamp)];
        }
        const bool options = tool->mode == ToolGun::Mode::Color || tool->mode == ToolGun::Mode::Material ||
                             tool->mode == ToolGun::Mode::Lamp;
        state.hints = options ? "LMB  Use      RMB  Option      R  Mode" : "LMB  Use      R  Mode      Z  Undo";
        return;
    }
    const Weapon* weapon = holder->GetWeapon();
    if (!weapon) {
        state.item = "PHYSICS GUN";
        state.countIsWord = true;
        state.count = gun && gun->IsGrabbing() ? "Holding" : "Ready";
        state.hints = gun && gun->IsGrabbing() ? "Wheel  Distance      E  Rotate      RMB  Freeze"
                                               : "Hold LMB  Grab      U  Unfreeze all";
        return;
    }
    state.item = Upper(weapon->displayName);
    state.count = std::to_string(weapon->GetAmmo());
    state.detail = std::format("/ {}", weapon->GetReserve());
    state.lowAmmo = weapon->magazineSize > 3 && weapon->GetAmmo() <= weapon->magazineSize / 4 && !weapon->IsReloading();
    state.hints = "R  Reload      RMB  Aim";
    if (weapon->IsReloading()) {
        state.reload = weapon->GetReloadProgress();
        state.hints = "Reloading...";
    } else if (weapon->GetAmmo() == 0 && weapon->GetReserve() == 0) {
        state.hints = "Out of ammunition";
    }
    // Down the sights, the sights are the crosshair.
    state.crosshair = !holder->IsAiming();
}

void Hud::OnDrawUi(Spot::Renderer2D& renderer, Spot::Vector2 viewSize) {
    if (GetScene().GetEntity(m_interface))
        return;
    // Without the authored interface, a plain crosshair and what is in hand.
    const Spot::Vector2 center = viewSize * 0.5f;
    const Spot::Color crosshair{1.0f, 1.0f, 1.0f, 0.8f};
    renderer.DrawQuad(center, {14.0f, 2.0f}, crosshair);
    renderer.DrawQuad(center, {2.0f, 14.0f}, crosshair);
    if (font) {
        const std::string hand = m_state.item + "  " + m_state.count + " " + m_state.detail;
        renderer.DrawString(*font, hand, {viewSize.x - 12.0f - 10.0f * static_cast<float>(hand.size()), 40.0f},
                            Spot::Color::FromSrgb(1.0f, 0.95f, 0.7f));
    }
}

void Hud::Serialize(Spot::Archive& archive) {
    archive.Field("font", font);
    archive.Field("slotsTime", slotsTime, {.min = 0, .max = 10, .speed = .05f});
}
