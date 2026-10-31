#include "session_display.h"

#include "gameplay/network/player_info.h"
#include "gameplay/network/session.h"

#include <format>

SPOT_COMPONENT(SessionDisplay);

void SessionDisplay::OnStart() {
    auto& canvas = GetScene().CreateEntity("Room Interface");
    canvas.AddComponent<Spot::Canvas>();
    m_canvas = canvas.GetId();
    const auto label = [&](std::string name, Spot::Vector2 position, Spot::Vector2 size) {
        auto& row = canvas.CreateChild(std::move(name));
        auto& rect = row.AddComponent<Spot::UiRect>();
        rect.layout.anchorMin = rect.layout.anchorMax = {1.0f, 1.0f};
        rect.layout.pivot = {1.0f, 1.0f};
        rect.layout.offset = position;
        rect.layout.sizeDelta = size;
        auto& text = row.AddComponent<Spot::UiText>();
        text.font = font;
        text.fontSize = 17.0f;
        text.color = {0.9f, 0.93f, 0.97f, 1.0f};
        return row.GetId();
    };
    m_status = label("Room", {-24.0f, -52.0f}, {360.0f, 28.0f});
    m_roster = label("Players", {-24.0f, -90.0f}, {360.0f, 440.0f});
}

void SessionDisplay::OnUpdate(float /*deltaTime*/) {
    const Gameplay::Session* session = Gameplay::Session::Get();
    auto* canvas = GetScene().GetEntity(m_canvas);
    if (!canvas) {
        return;
    }
    const bool active = session && session->IsActive();
    canvas->SetActive(active);
    if (!active) {
        return;
    }
    const auto players = Gameplay::PlayerInfos::GetAll(GetScene());
    if (auto* entity = GetScene().GetEntity(m_status)) {
        entity->GetComponent<Spot::UiText>()->text = std::format(
            "{}  {}/{}   [F2] Players", session->GetSettings().name, players.size(), session->GetSettings().maxPlayers);
    }
    if (auto* entity = GetScene().GetEntity(m_roster)) {
        const bool show = Spot::Input::IsActionDown(action);
        entity->SetActive(show);
        if (show) {
            std::string roster;
            for (const Gameplay::PlayerInfo* player : players) {
                roster += std::format("{}{}  {} ms\n", player->name, player->IsLocal() ? " (you)" : "", player->ping);
            }
            entity->GetComponent<Spot::UiText>()->text = std::move(roster);
        }
    }
}

void SessionDisplay::OnDestroy() {
    if (auto* canvas = GetScene().GetEntity(m_canvas)) {
        canvas->Destroy();
    }
}

void SessionDisplay::Serialize(Spot::Archive& archive) {
    archive.Field("font", font);
    archive.Field("action", action);
}
