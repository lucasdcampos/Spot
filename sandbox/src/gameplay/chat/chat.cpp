#include "gameplay/chat/chat.h"

#include "developer_mode.h"
#include "gameplay/core/players.h"
#include "gameplay/core/user_settings.h"
#include "gameplay/menus/settings_menu.h"
#include "gameplay/network/player_info.h"
#include "sandbox_hud.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace Gameplay {

SPOT_COMPONENT(ChatElement);
SPOT_COMPONENT(ChatRow);

namespace {

constexpr std::size_t MaxMessageLength = 240;

// From a client, or the host to itself: a line to say in the room's chat.
struct ChatSend {
    static constexpr std::string_view NetName = "Gameplay.ChatSend";
    std::string text;
    void Serialize(Spot::Archive& archive) { archive.Field("text", text); }
};

// From the server to everyone: who said it, their developer badge and the line, resolved from the sender's own
// PlayerInfo rather than trusted from the message.
struct ChatMessage {
    static constexpr std::string_view NetName = "Gameplay.ChatMessage";
    std::string sender;
    bool developer = false;
    std::string text;
    void Serialize(Spot::Archive& archive) {
        archive.Field("sender", sender);
        archive.Field("developer", developer);
        archive.Field("text", text);
    }
};

std::string Trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

} // namespace

void ChatRow::DrawWidget(Spot::Renderer2D& renderer, const Spot::UiLayoutEntry& entry) {
    if (!font || opacity <= 0.0f || (sender.empty() && text.empty())) {
        return;
    }
    const float pixel = entry.scale;
    const float factor = fontSize / font->GetPixelSize() * pixel;
    const float badgeWidth = developer ? 20.0f * pixel : 0.0f;
    const float padding = 8.0f * pixel;
    const float maxWidth = std::max(0.0f, entry.bounds.GetSize().x - badgeWidth - padding * 3);
    // Keeps a long name or message from running past the row.
    const auto shorten = [&](std::string value, float width) {
        while (!value.empty() && font->MeasureText(value).x * factor > width) {
            std::size_t last = value.size() - 1;
            while (last > 0 && (static_cast<unsigned char>(value[last]) & 0xc0) == 0x80) {
                --last;
            }
            value.resize(last);
        }
        return value;
    };
    const auto nameText = shorten(sender.empty() ? std::string{} : sender + ":", maxWidth * 0.4f);
    const auto nameSize = font->MeasureText(nameText) * factor;
    const auto bodyText = shorten(text, std::max(0.0f, maxWidth - nameSize.x));
    const auto bodySize = font->MeasureText(bodyText) * factor;
    const float width =
        badgeWidth + (nameText.empty() ? 0.0f : nameSize.x + padding) + bodySize.x + padding * 2;
    const float height = entry.bounds.GetSize().y;
    const Spot::Vector2 left = entry.bounds.min;
    const Spot::Vector2 center{left.x + width * 0.5f, left.y + height * 0.5f};
    renderer.DrawQuad(center, {width, height}, {0.008f, 0.012f, 0.022f, opacity * 0.78f});
    float x = left.x + padding;
    if (developer) {
        DrawDeveloperBadge(renderer, {x + badgeWidth * 0.5f, center.y}, opacity);
        x += badgeWidth;
    }
    const auto drawText = [&](std::string_view value, Spot::Vector2 size, float atX, Spot::Color color) {
        const Spot::Vector2 position{std::round(atX), std::round(center.y + size.y * 0.5f)};
        for (const auto offset :
             {Spot::Vector2{-pixel, 0}, Spot::Vector2{pixel, 0}, Spot::Vector2{0, -pixel}, Spot::Vector2{0, pixel}}) {
            renderer.DrawString(*font, value, position + offset, {0, 0, 0, opacity}, factor);
        }
        color.a *= opacity;
        renderer.DrawString(*font, value, position, color, factor);
    };
    if (!nameText.empty()) {
        drawText(nameText, nameSize, x, senderColor);
        x += nameSize.x + padding;
    }
    drawText(bodyText, bodySize, x, textColor);
}

void ChatRow::Serialize(Spot::Archive& archive) {
    SerializeWidget(archive);
    archive.Field("font", font);
    archive.Field("fontSize", fontSize, {.min = 1, .max = 200});
}

void ChatElement::Open() {
    if (m_open) {
        return;
    }
    m_open = true;
    SetActive(input, true);
    auto& uiInput = GetScene().GetOrAddSystem<Spot::UiInputSystem>();
    uiInput.SetInputState(Spot::Input::GetState());
    auto size = GetScene().GetOrAddSystem<Spot::UiSystem>().GetViewSize();
    if (size.x <= 0 || size.y <= 0) {
        size = {1280, 720};
    }
    uiInput.Refresh(size);
    uiInput.Focus(input);
}

void ChatElement::Close() {
    if (!m_open) {
        return;
    }
    m_open = false;
    if (auto* uiInput = GetScene().GetSystem<Spot::UiInputSystem>();
        uiInput && uiInput->GetFocusedEntity() == input) {
        uiInput->ClearFocus();
    }
    SetActive(input, false);
}

void ChatElement::AddLine(std::string sender, std::string text, bool developer, bool system) {
    m_lines.insert(m_lines.begin(), {std::move(sender), std::move(text), developer, system, 0.0f});
    if (m_lines.size() > rows.size()) {
        m_lines.resize(rows.size());
    }
}

void ChatElement::Submit(std::string_view submitted) {
    std::string message = Trim(submitted);
    Close();
    if (auto* field = Get<Spot::UiInputField>(input)) {
        field->SetText({});
    }
    if (message.empty()) {
        return;
    }
    if (message.front() == '/') {
        const Spot::ConsoleResult result = Spot::Console::Get().Execute(message.substr(1));
        if (!result.message.empty()) {
            AddLine({}, result.message, false, true);
        }
        return;
    }
    if (message.size() > MaxMessageLength) {
        std::size_t end = MaxMessageLength;
        while (end > 0 && (static_cast<unsigned char>(message[end]) & 0xc0) == 0x80) {
            --end;
        }
        message.resize(end);
    }
    std::string name;
    bool developer = false;
    if (const PlayerInfo* local = PlayerInfos::GetLocal(GetScene())) {
        name = local->name;
        developer = local->developerAuthenticated;
    } else {
        if (const auto* settings = GetScene().FindComponent<SettingsMenu>()) {
            name = settings->GetSettings().playerName;
        }
        if (name.empty() || name == "Player") {
            if (auto* app = Spot::Application::TryGet()) {
                UserSettings preferences;
                if (preferences.Load(app->GetUserDataDirectory())) {
                    name = preferences.playerName;
                }
            }
        }
        if (name.empty() || name == "???") {
            name = "Player";
        }
        if (const auto* pawnMode = Gameplay::Players::FindComponent<DeveloperMode>(GetScene())) {
            developer = pawnMode->IsAuthenticated();
        } else if (const auto* sceneMode = GetScene().FindComponent<DeveloperMode>()) {
            developer = sceneMode->IsAuthenticated();
        }
    }
    auto* app = Spot::Application::TryGet();
    auto* network = app ? app->GetModule<Spot::NetworkModule>() : nullptr;
    Spot::NetworkSession* session = network ? network->GetSession() : nullptr;
    if (!session) {
        AddLine(name, message, developer);
    } else if (session->IsServer()) {
        AddLine(name, message, developer);
        session->SendToAll(ChatMessage{name, developer, message});
    } else {
        session->SendToServer(ChatSend{std::move(message)});
    }
}

void ChatElement::Bind(Spot::NetworkSession& session) {
    m_bound = &session;
    session.GetMessages().On<ChatMessage>(
        [this](Spot::NetPeerId, ChatMessage& message) { AddLine(message.sender, message.text, message.developer); });
    session.GetMessages().On<ChatSend>([this, &session](Spot::NetPeerId from, ChatSend& sent) {
        if (!session.IsServer()) {
            return;
        }
        std::string name = "Player";
        bool developer = false;
        for (const PlayerInfo* info : PlayerInfos::GetAll(GetScene())) {
            if (info->peer == static_cast<std::uint32_t>(from)) {
                name = info->name;
                developer = info->developerAuthenticated;
                break;
            }
        }
        std::string text = Trim(sent.text);
        if (text.empty()) {
            return;
        }
        if (text.size() > MaxMessageLength) {
            text.resize(MaxMessageLength);
        }
        AddLine(name, text, developer);
        session.SendToAll(ChatMessage{std::move(name), developer, std::move(text)});
    });
}

bool ChatElement::IsWanted(const HudContext& context) {
    if (m_connections.empty()) {
        auto& uiInput = GetScene().GetOrAddSystem<Spot::UiInputSystem>();
        m_connections.push_back(uiInput.OnRawEvent.Connect([this](const Spot::Event& event, bool& consumed) {
            if (m_open) {
                return;
            }
            const auto* key = event.GetIf<Spot::KeyPressedEvent>();
            if (key && key->key == Spot::Key::Enter && !key->repeat) {
                Open();
                consumed = true;
            }
        }));
        if (auto* field = Get<Spot::UiInputField>(input)) {
            m_connections.push_back(field->OnSubmit.Connect([this](std::string_view text) { Submit(text); }));
        }
    }
    auto* app = Spot::Application::TryGet();
    auto* network = app ? app->GetModule<Spot::NetworkModule>() : nullptr;
    Spot::NetworkSession* session = network ? network->GetSession() : nullptr;
    if (session && session != m_bound) {
        Bind(*session);
    } else if (!session) {
        m_bound = nullptr;
    }
    if (m_open && GetScene().GetOrAddSystem<Spot::UiInputSystem>().GetFocusedEntity() != input) {
        Close();
    }
    for (Line& line : m_lines) {
        line.age += context.deltaTime;
    }
    return true;
}

void ChatElement::Refresh(const HudContext& /*context*/) {
    for (std::size_t index = 0; index < rows.size(); ++index) {
        auto* row = Get<ChatRow>(rows[index]);
        if (!row) {
            continue;
        }
        if (index >= m_lines.size()) {
            row->sender.clear();
            row->text.clear();
            row->opacity = 0.0f;
            continue;
        }
        const Line& line = m_lines[index];
        row->sender = line.sender;
        row->text = line.text;
        row->developer = line.developer;
        row->senderColor = line.system ? systemColor : nameColor;
        row->textColor = line.system ? systemColor : textColor;
        row->opacity = m_open ? 1.0f : std::clamp(lifetime - line.age, 0.0f, 1.0f);
    }
}

void ChatElement::Serialize(Spot::Archive& archive) {
    HudElement::Serialize(archive);
    archive.Field("rows", rows, {.tooltip = "The ChatRows of its lines, newest at the first"});
    archive.Field("input", input, {.tooltip = "The UiInputField it types into, inactive until Enter opens it"});
    archive.Field("lifetime", lifetime,
                  {.min = 0.5f, .max = 120.0f, .speed = 0.1f, .tooltip = "Seconds a line stays while chat is closed"});
    archive.Field("nameColor", nameColor);
    archive.Field("textColor", textColor);
    archive.Field("systemColor", systemColor, {.tooltip = "For a command's own answer"});
}

} // namespace Gameplay
