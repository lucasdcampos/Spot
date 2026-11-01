#pragma once

#include "gameplay/hud/hud.h"

#include <spot.h>

#include <string>
#include <string_view>
#include <vector>

namespace Gameplay {

// One chat line: an outlined sender name with its developer badge, then the message, fitted to the row and fading
// with Opacity.
class ChatRow : public Spot::UiWidget {
public:
    Spot::AssetRef<Spot::Font> font;
    float fontSize = 18.0f;
    std::string sender;
    std::string text;
    bool developer = false;
    Spot::Color senderColor = Spot::Color::White;
    Spot::Color textColor = Spot::Color::White;
    float opacity = 0.0f;

protected:
    void DrawWidget(Spot::Renderer2D& renderer, const Spot::UiLayoutEntry& entry) override;
    void Serialize(Spot::Archive& archive) override;
};

// Local and multiplayer chat: Enter opens it, typing and Enter again sends, over the room when there is one. Each
// line names who sent it, with a developer badge for one the server authenticated as a developer. Lines fade a
// lifetime after they arrive while chat is closed, and show in full again while it is open. A line starting with /
// runs as a console command instead of being sent, exactly as typing it into the console would.
class ChatElement : public HudElement {
public:
    // The ChatRows of its lines, newest at the first; also how many lines it keeps.
    std::vector<Spot::EntityId> rows;
    // The UiInputField it types into, inactive until Enter opens it.
    Spot::EntityId input = Spot::EntityId::Invalid;
    float lifetime = 8.0f;
    Spot::Color nameColor = {0.35f, 0.75f, 1.0f, 1.0f};
    Spot::Color textColor = Spot::Color::White;
    // For a command's own answer, which only whoever ran it sees.
    Spot::Color systemColor = {1.0f, 0.72f, 0.2f, 1.0f};

    bool IsOpen() const { return m_open; }

    void Open();
    void Close();
    void Submit(std::string_view text);
    void AddLine(std::string sender, std::string text, bool developer, bool system = false);
    void Refresh(const HudContext& context) override;

protected:
    bool IsWanted(const HudContext& context) override;
    void Serialize(Spot::Archive& archive) override;

private:
    struct Line {
        std::string sender;
        std::string text;
        bool developer = false;
        bool system = false;
        float age = 0.0f;
    };

    void Bind(Spot::NetworkSession& session);

    // Newest first; kept up to rows.size() so the full history shows again while chat is open.
    std::vector<Line> m_lines;
    bool m_open = false;
    Spot::NetworkSession* m_bound = nullptr;
    std::vector<Spot::SignalConnection> m_connections;
};

} // namespace Gameplay
