#pragma once

#include <spot.h>

// The sandbox's room status and roster. Names and ownership come from replicated PlayerInfo objects; F2 opens the
// list without taking Tab from the inventory wheel.
class SessionDisplay final : public Spot::Component {
public:
    Spot::AssetRef<Spot::Font> font;
    std::string action = "Player List";

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDestroy() override;
    void Serialize(Spot::Archive& archive) override;

private:
    Spot::EntityId m_canvas = Spot::EntityId::Invalid;
    Spot::EntityId m_status = Spot::EntityId::Invalid;
    Spot::EntityId m_roster = Spot::EntityId::Invalid;
};
