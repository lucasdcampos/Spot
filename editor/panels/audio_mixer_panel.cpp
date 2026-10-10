#include "panels/audio_mixer_panel.h"

#include "imgui/editor_style.h"
#include "imgui/icons.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

namespace Spot::Editor {

namespace {

constexpr float MaxVolume = 2.0f;
// How fast meters fall, in full scale per second, so peaks stay visible a moment.
constexpr float MeterFall = 1.5f;

std::string FormatDecibels(float volume) {
    return volume <= 0.0001f ? std::string("-inf dB") : std::format("{:+.1f} dB", 20.0f * std::log10(volume));
}

// Whether the bus is the other one or below it, so it cannot become its parent.
bool IsWithin(const AudioSettings& settings, const std::string& bus, const std::string& other) {
    std::string current = bus;
    for (std::size_t steps = 0; steps <= settings.buses.size() && !current.empty(); ++steps) {
        if (current == other) {
            return true;
        }
        const auto found = std::ranges::find(settings.buses, current, &AudioBusSettings::name);
        current = found != settings.buses.end() ? found->parent : std::string();
    }
    return false;
}

} // namespace

AudioMixerPanel::AudioMixerPanel(EditorContext& context)
    : Panel("AudioMixer", ICON_FA_SLIDERS "  Audio Mixer"), m_context(context) {
    SetOpen(false);
}

void AudioMixerPanel::SetUpWindow() {
    ImGui::SetNextWindowSize({ImGui::GetFontSize() * 40.0f, ImGui::GetFontSize() * 18.0f}, ImGuiCond_FirstUseEver);
}

void AudioMixerPanel::OnDraw() {
    EditorProject* project = m_context.GetProject();
    AudioDevice* device = m_context.GetAudioDevice();
    if (!project || !device) {
        DrawEmptyState(ICON_FA_SLIDERS, "Open a project to mix its sounds.");
        return;
    }
    AudioSettings& settings = project->GetProject().GetSettings().sections.audio;
    if (m_appliedProject != project) {
        m_appliedProject = project;
        settings.Apply(*device);
    }

    const float deltaTime = ImGui::GetIO().DeltaTime;
    DrawStrip(settings, nullptr, deltaTime);
    for (std::size_t i = 0; i < settings.buses.size(); ++i) {
        ImGui::SameLine();
        ImGui::PushID(static_cast<int>(i));
        const bool removed = DrawStrip(settings, &settings.buses[i], deltaTime);
        ImGui::PopID();
        if (removed) {
            RemoveBus(settings, i);
            break;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLUS "##AddBus", {0.0f, ImGui::GetContentRegionAvail().y})) {
        std::string name = "Bus";
        for (int suffix = 2; std::ranges::find(settings.buses, name, &AudioBusSettings::name) != settings.buses.end();
             ++suffix) {
            name = std::format("Bus {}", suffix);
        }
        settings.buses.push_back({.name = name});
        m_changed = true;
    }
    ImGui::SetItemTooltip("Add a bus");

    if (m_changed) {
        settings.Apply(*device);
        // Once a drag or an edit ends, rather than on every change it makes.
        if (!ImGui::IsAnyItemActive()) {
            m_changed = false;
            Save();
        }
    }
}

bool AudioMixerPanel::DrawStrip(AudioSettings& settings, AudioBusSettings* bus, float deltaTime) {
    AudioDevice& device = *m_context.GetAudioDevice();
    const std::string name = bus ? bus->name : std::string("Master");
    const std::optional<AudioBusId> id = bus ? device.FindBus(bus->name) : AudioBusId::Master;
    const float fontSize = ImGui::GetFontSize();
    bool removed = false;

    ImGui::BeginChild("##Strip", {fontSize * 6.0f, 0.0f}, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    // The name, which buses can be renamed and reparented from.
    if (bus && m_renaming == name) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##Name", &m_newName,
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            const bool taken =
                std::ranges::find(settings.buses, m_newName, &AudioBusSettings::name) != settings.buses.end();
            if (!m_newName.empty() && !taken && m_newName != "Master") {
                for (AudioBusSettings& child : settings.buses) {
                    if (child.parent == bus->name) {
                        child.parent = m_newName;
                    }
                }
                if (id) {
                    device.SetBusName(*id, m_newName);
                }
                bus->name = m_newName;
                m_changed = true;
            }
            m_renaming.clear();
        } else if (ImGui::IsItemDeactivated()) {
            m_renaming.clear();
        }
    } else {
        ImGui::TextUnformatted(name.c_str());
        if (bus) {
            ImGui::SetItemTooltip("%s", bus->parent.empty() ? "Into Master" : ("Into " + bus->parent).c_str());
        }
    }
    if (bus && ImGui::BeginPopupContextWindow("##Bus")) {
        if (ImGui::MenuItem(ICON_FA_PEN "  Rename")) {
            m_renaming = bus->name;
            m_newName = bus->name;
        }
        if (ImGui::BeginMenu("Parent")) {
            if (ImGui::MenuItem("Master", nullptr, bus->parent.empty())) {
                bus->parent.clear();
                m_changed = true;
            }
            for (const AudioBusSettings& other : settings.buses) {
                if (IsWithin(settings, other.name, bus->name)) {
                    continue;
                }
                if (ImGui::MenuItem(other.name.c_str(), nullptr, bus->parent == other.name)) {
                    bus->parent = other.name;
                    m_changed = true;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        removed = ImGui::MenuItem(ICON_FA_TRASH_CAN "  Remove");
        ImGui::EndPopup();
    }

    // The meter beside the fader.
    // What is left once the decibels and the buttons below have their room.
    const float below = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeightWithSpacing();
    const float height = std::max(ImGui::GetContentRegionAvail().y - below, fontSize * 4.0f);
    DrawMeter(name, id ? device.GetBusPeak(*id) : AudioPeak{}, height, deltaTime);
    ImGui::SameLine();
    float& volume = bus ? bus->volume : settings.masterVolume;
    if (ImGui::VSliderFloat("##Fader", {fontSize * 1.6f, height}, &volume, 0.0f, MaxVolume, "")) {
        m_changed = true;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        volume = 1.0f;
        m_changed = true;
    }
    ImGui::SetItemTooltip("%s (double-click for 0 dB)", FormatDecibels(volume).c_str());
    ImGui::TextColored(EditorColors::TextMuted, "%s", FormatDecibels(volume).c_str());

    // Mute is saved with the project; solo is only for listening.
    if (bus && id) {
        const auto toggle = [](const char* label, bool on, const ImVec4& color) {
            if (on) {
                ImGui::PushStyleColor(ImGuiCol_Button, color);
            }
            const bool clicked = ImGui::Button(label, {ImGui::GetFontSize() * 1.8f, 0.0f});
            if (on) {
                ImGui::PopStyleColor();
            }
            return clicked;
        };
        if (toggle("M", bus->muted, EditorColors::Error)) {
            bus->muted = !bus->muted;
            m_changed = true;
        }
        ImGui::SetItemTooltip("Mute");
        ImGui::SameLine();
        const bool solo = device.IsBusSolo(*id);
        if (toggle("S", solo, EditorColors::Warning)) {
            device.SetBusSolo(*id, !solo);
        }
        ImGui::SetItemTooltip("Solo: hear only the soloed buses");
    }
    ImGui::EndChild();
    return removed;
}

void AudioMixerPanel::DrawMeter(const std::string& key, AudioPeak peak, float height, float deltaTime) {
    Level& level = m_levels[key];
    level.left = std::max(peak.left, level.left - MeterFall * deltaTime);
    level.right = std::max(peak.right, level.right - MeterFall * deltaTime);

    const float width = ImGui::GetFontSize() * 0.4f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const auto bar = [&](float x, float value) {
        const ImVec2 min{origin.x + x, origin.y};
        const ImVec2 max{min.x + width, origin.y + height};
        drawList.AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg));
        // Full scale at the top, where louder clips.
        const float filled = std::clamp(value / MaxVolume, 0.0f, 1.0f) * height;
        const ImVec4 color = value >= 1.0f   ? EditorColors::Error
                             : value >= 0.7f ? EditorColors::Warning
                                             : EditorColors::Success;
        drawList.AddRectFilled({min.x, max.y - filled}, max, ImGui::GetColorU32(color));
    };
    bar(0.0f, level.left);
    bar(width + 2.0f, level.right);
    // The line where 0 dB is.
    const float unity = origin.y + height * (1.0f - 1.0f / MaxVolume);
    drawList.AddLine({origin.x, unity}, {origin.x + width * 2.0f + 2.0f, unity}, ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::Dummy({width * 2.0f + 2.0f, height});
}

void AudioMixerPanel::RemoveBus(AudioSettings& settings, std::size_t index) {
    const AudioBusSettings removed = settings.buses[index];
    for (AudioBusSettings& child : settings.buses) {
        if (child.parent == removed.name) {
            child.parent = removed.parent;
        }
    }
    settings.buses.erase(settings.buses.begin() + static_cast<std::ptrdiff_t>(index));
    if (AudioDevice* device = m_context.GetAudioDevice()) {
        if (const auto id = device->FindBus(removed.name)) {
            device->RemoveBus(*id);
        }
    }
    m_levels.erase(removed.name);
    m_changed = true;
}

void AudioMixerPanel::Save() {
    if (EditorProject* project = m_context.GetProject()) {
        project->GetProject().Save();
    }
}

} // namespace Spot::Editor
