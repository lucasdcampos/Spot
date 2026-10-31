#pragma once

#include <spot.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Gameplay {

struct SessionSettings {
    std::uint16_t port = 27015;
    int maxPlayers = 16;
    std::string name = "Room";
    std::string password;
    std::string playerName = "Player";
    Spot::AssetId map;
    Spot::AssetId mode;
    // A dedicated server: nobody plays on it, so all its players come from other machines.
    bool dedicated = false;
};

enum class SessionState { Offline, Connecting, Loading, Playing, Failed };

// The introduction is checked before any scene is sent. A signature covers the imported content, so sessions never
// silently play with missing or different assets. These messages also work with standalone NetHosts in tests.
struct SessionHello {
    std::string playerName;
    std::string password;
    std::uint64_t content = 0;
    void Serialize(Spot::Archive& archive);
};

std::uint64_t GetSessionContentSignature(const Spot::AssetManager& assets);
std::string ValidateSessionHello(const SessionSettings& settings, std::uint64_t content,
                                 std::span<const std::byte> bytes);

// A game's room, over the application's NetworkModule. It owns the game-level admission policy and status across
// map changes; the engine still knows only scenes, objects and bytes. Failure and leaving return to the game's
// startup scene, with the last error retained for its menu.
class Session final : public Spot::Module {
public:
    static Session* Get();
    bool Host(SessionSettings settings);
    bool Join(std::string_view address, std::string playerName = "Player", std::string password = {});
    void Leave();
    SessionState GetState() const { return m_state; }
    const std::string& GetLastError() const { return m_error; }
    const SessionSettings& GetSettings() const { return m_settings; }
    bool IsHost() const;
    bool IsActive() const;
    std::string GetPlayerName(Spot::NetPeerId peer) const;

    Spot::Signal<SessionState> OnStateChanged;

protected:
    void OnStart() override;
    void OnStop() override;
    void OnUpdate(float deltaTime) override;

private:
    struct Welcome;
    void Bind(Spot::NetworkSession& session);
    void SetState(SessionState state);
    bool Fail(std::string reason, bool returnToStartup = false);
    void ReturnToStartup();
    void ClearConnections();

    Spot::NetworkModule* m_network = nullptr;
    Spot::NetworkSession* m_bound = nullptr;
    SessionState m_state = SessionState::Offline;
    SessionSettings m_settings;
    std::string m_error;
    std::uint64_t m_content = 0;
    std::chrono::steady_clock::time_point m_since;
    std::unordered_map<Spot::NetPeerId, std::string> m_names;
    std::vector<Spot::SignalConnection> m_connections;
    Spot::SignalConnection m_ended;
};

} // namespace Gameplay
