#pragma once

#include "app/editor_context.h"
#include "panels/panel.h"

#include <map>
#include <string>

namespace Spot::Editor {

// The project's audio buses as a mixing desk: a strip for the master bus and one for each bus, with its fader, its
// peak meter, mute and solo. Changes are heard at once, while a game plays too, and saved to the project's audio
// settings; solo only lasts while the editor runs, to listen to some sounds alone.
class AudioMixerPanel final : public Panel {
public:
    explicit AudioMixerPanel(EditorContext& context);

protected:
    void SetUpWindow() override;
    void OnDraw() override;

private:
    struct Level {
        float left = 0.0f;
        float right = 0.0f;
    };

    // A strip; null bus settings for the master bus. Returns whether the bus was removed.
    bool DrawStrip(AudioSettings& settings, AudioBusSettings* bus, float deltaTime);
    void DrawMeter(const std::string& key, AudioPeak peak, float height, float deltaTime);
    void RemoveBus(AudioSettings& settings, std::size_t index);
    void Save();

    EditorContext& m_context;
    // The settings were last given to the device for this project.
    const EditorProject* m_appliedProject = nullptr;
    bool m_changed = false;
    // What the meters show, falling slower than the peaks do, by bus name.
    std::map<std::string, Level> m_levels;
    std::string m_renaming;
    std::string m_newName;
};

} // namespace Spot::Editor
