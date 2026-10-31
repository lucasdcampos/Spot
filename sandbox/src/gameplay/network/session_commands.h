#pragma once

#include <spot.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Gameplay {

// The console commands of whoever runs a room, from a dedicated server's terminal (Spot::TerminalConsole), a host's
// console, or an operator's (Session::SetOperator()): `status` says the room's name, map, password and players with
// their pings, operators marked; `kick <name or #peer> [reason]` removes a player, who is told why; `password [new]`
// changes the password of those who join from now on, removing it without one; `op <player>` and `deop <player>`
// make a player an operator, or no longer.
//
// On a client, these and the commands games allow (AllowRemote()) are sent to the server, which runs them for that
// player when it may (an operator, or, for those allowed with cheats, anyone while the server permits cheats) and
// sends back what they answered, logged here.
class SessionCommands final : public Spot::Module {
public:
    enum class Permission {
        // Operators and the host only.
        Operator,
        // Also anyone, while the server's console permits cheats.
        Cheat,
    };

    // The console they are registered with, the process's unless another is given, as in tests.
    explicit SessionCommands(Spot::Console* console = nullptr) : m_console(console) {}

    static SessionCommands* Get();
    // Lets clients run a command of this console on the server, as the room's are.
    void AllowRemote(std::string name, Permission permission);
    // On a client: sends the line to the server to run, true when it was; it answers later, in the log.
    bool Forward(std::string_view line);
    // Whether this machine is a client, whose room commands go to the server.
    bool IsRemote() const;

protected:
    void OnStart() override;
    void OnStop() override;
    void OnUpdate(float deltaTime) override;

private:
    Spot::Console& GetConsole() const;
    void Bind(Spot::NetworkSession& session);

    Spot::Console* m_console;
    std::vector<Spot::ConsoleRegistration> m_registrations;
    std::unordered_map<std::string, Permission> m_remote;
    Spot::NetworkSession* m_bound = nullptr;
};

} // namespace Gameplay
