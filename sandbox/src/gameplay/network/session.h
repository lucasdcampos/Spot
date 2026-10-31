#pragma once

#include <spot.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Gameplay {

struct SessionSettings {
    std::uint16_t port = 27015;
    // Where a hosted room answers those looking for rooms on the local network (LanServers); 0 keeps it unlisted.
    std::uint16_t discoveryPort = 27016;
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

// Why a session failed, for menus to say what went wrong and what to do; GetLastError() says it in words.
enum class SessionError {
    None,
    // What was asked could not be done here, as an invalid address or name, or a room that could not start.
    Request,
    // Nothing answered at the address, or not in time.
    Unreachable,
    // The room answered and refused, saying why, as for a wrong password or another version of the game.
    Refused,
    // The room accepted, but its map did not finish loading in time.
    Loading,
    // The session ended after it was joined: the room closed, removed the player, or the connection was lost.
    Disconnected,
};

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
// startup scene, with the last error retained for its menu. A hosted room answers LanServers' queries with its name,
// map, mode, players and whether it has a password.
//
// The host's own players, and those the host makes operators (SetOperator(), for as long as they stay), run the room:
// they may use cheats even while the server's console does not permit them (Spot::Console::AreCheatsEnabled()), in
// which case only they may. Each client is told whether it may cheat, and its console permits cheats only then.
class Session final : public Spot::Module {
public:
    static Session* Get();
    bool Host(SessionSettings settings);
    bool Join(std::string_view address, std::string playerName = "Player", std::string password = {});
    // Joins again the last room Join() was asked for, with the same name and password.
    bool Retry();
    bool CanRetry() const { return !m_lastJoin.address.empty(); }
    // The address of the last room Join() was asked for.
    const std::string& GetJoinAddress() const { return m_lastJoin.address; }
    void Leave();
    SessionState GetState() const { return m_state; }
    const std::string& GetLastError() const { return m_error; }
    SessionError GetLastErrorKind() const { return m_errorKind; }
    // Seconds since it started connecting or hosting, and how long connecting and loading may take before it fails.
    float GetElapsed() const;
    static constexpr float Timeout = 15.0f;
    const SessionSettings& GetSettings() const { return m_settings; }
    // On the host: the password of those who join from now on, none when empty; false when longer than 128 bytes.
    bool SetPassword(std::string password);
    bool IsHost() const;
    bool IsActive() const;
    std::string GetPlayerName(Spot::NetPeerId peer) const;
    // The connected players whose name it is.
    std::vector<Spot::NetPeerId> FindPlayers(std::string_view name) const;

    // On the host: whether the player is an operator; Invalid is the host's own, which always is.
    bool IsOperator(Spot::NetPeerId peer) const;
    void SetOperator(Spot::NetPeerId peer, bool op);
    // Whether the player may use cheats: the server's console permits them, or it is an operator. On a client, with
    // Invalid, whether this one may, as the server said.
    bool MayCheat(Spot::NetPeerId peer = Spot::NetPeerId::Invalid) const;

    Spot::Signal<SessionState> OnStateChanged;

protected:
    void OnStart() override;
    void OnStop() override;
    void OnUpdate(float deltaTime) override;

private:
    struct Welcome;
    void Bind(Spot::NetworkSession& session);
    void SetState(SessionState state);
    bool Fail(std::string reason, SessionError kind = SessionError::Request, bool returnToStartup = false);
    // Answers those looking for rooms on the local network.
    void Listen();
    void ReturnToStartup();
    void ClearConnections();
    // On the host: tells each client whether it may cheat, when that changed.
    void SendPermissions();

    Spot::NetworkModule* m_network = nullptr;
    Spot::NetworkSession* m_bound = nullptr;
    SessionState m_state = SessionState::Offline;
    SessionSettings m_settings;
    std::string m_error;
    SessionError m_errorKind = SessionError::None;
    struct JoinRequest {
        std::string address;
        std::string playerName;
        std::string password;
    };
    JoinRequest m_lastJoin;
    std::uint64_t m_content = 0;
    std::chrono::steady_clock::time_point m_since;
    std::unordered_map<Spot::NetPeerId, std::string> m_names;
    std::unordered_set<Spot::NetPeerId> m_operators;
    // On the host, what each client was last told; on a client, what it was told.
    std::unordered_map<Spot::NetPeerId, bool> m_toldCheats;
    bool m_mayCheat = false;
    std::unique_ptr<Spot::NetDiscoveryResponder> m_responder;
    // What the room is called where it is listed.
    std::string m_mapName;
    std::string m_modeName;
    std::vector<Spot::SignalConnection> m_connections;
    Spot::SignalConnection m_ended;
};

} // namespace Gameplay
