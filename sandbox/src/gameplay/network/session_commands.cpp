#include "gameplay/network/session_commands.h"

#include "gameplay/network/session.h"

#include <format>
#include <string>

namespace Gameplay {

namespace {

// From a client: a command line to run on the server for its player.
struct RemoteCommand {
    static constexpr std::string_view NetName = "Gameplay.RemoteCommand";
    std::string line;
    void Serialize(Spot::Archive& archive) { archive.Field("line", line); }
};

// To a client: what a command it sent answered, or something the server tells it, as that it became an operator.
struct RemoteAnswer {
    static constexpr std::string_view NetName = "Gameplay.RemoteAnswer";
    bool success = true;
    std::string message;
    void Serialize(Spot::Archive& archive) {
        archive.Field("success", success);
        archive.Field("message", message);
    }
};

// The room this machine runs, or why there is none to command.
struct Room {
    Session* session = nullptr;
    Spot::NetHost* host = nullptr;
    std::string error;
};

Room GetRoom() {
    Session* session = Session::Get();
    if (!session || !session->IsActive()) {
        return {.error = "There is no room: host one first."};
    }
    if (!session->IsHost()) {
        return {.error = "The room is another machine's: only its host commands it."};
    }
    auto* network = Spot::Application::Get().GetModule<Spot::NetworkModule>();
    return {.session = session, .host = &network->GetSession()->GetHost()};
}

std::string JoinWords(std::span<const std::string> words, std::size_t from) {
    std::string text;
    for (std::size_t i = from; i < words.size(); ++i) {
        text += (text.empty() ? "" : " ") + words[i];
    }
    return text;
}

// The line that runs the command with these arguments, quoting what the console would split.
std::string MakeLine(std::string_view name, std::span<const std::string> arguments) {
    std::string line(name);
    for (const std::string& argument : arguments) {
        line += ' ';
        if (!argument.empty() && argument.find_first_of(" \t\"'\\") == std::string::npos) {
            line += argument;
            continue;
        }
        line += '"';
        for (const char c : argument) {
            if (c == '"' || c == '\\') {
                line += '\\';
            }
            line += c;
        }
        line += '"';
    }
    return line;
}

// The one connected player a name or "#number" means, or why there is none.
struct Player {
    Spot::NetPeerId peer = Spot::NetPeerId::Invalid;
    std::string error;
};

Player FindPlayer(const Room& room, std::string_view name) {
    std::vector<Spot::NetPeerId> found;
    for (const Spot::NetPeerId peer : room.host->GetPeers()) {
        if (name == "#" + std::to_string(static_cast<std::uint32_t>(peer))) {
            found.push_back(peer);
        }
    }
    if (found.empty()) {
        found = room.session->FindPlayers(name);
    }
    if (found.empty()) {
        return {.error = std::format("No player is called {}; status lists them.", name)};
    }
    if (found.size() > 1) {
        return {.error = std::format("Several players are called {}; name one by its #number.", name)};
    }
    return {.peer = found.front()};
}

} // namespace

SessionCommands* SessionCommands::Get() {
    Spot::Application* application = Spot::Application::TryGet();
    return application ? application->GetModule<SessionCommands>() : nullptr;
}

Spot::Console& SessionCommands::GetConsole() const {
    return m_console ? *m_console : Spot::Console::Get();
}

void SessionCommands::AllowRemote(std::string name, Permission permission) {
    m_remote[std::move(name)] = permission;
}

bool SessionCommands::IsRemote() const {
    const Session* session = Session::Get();
    return session && session->IsActive() && !session->IsHost();
}

bool SessionCommands::Forward(std::string_view line) {
    auto* network = GetApplication().GetModule<Spot::NetworkModule>();
    Spot::NetworkSession* session = network ? network->GetSession() : nullptr;
    if (!IsRemote() || !session || !session->IsConnected()) {
        return false;
    }
    session->SendToServer(RemoteCommand{std::string(line)});
    return true;
}

void SessionCommands::Bind(Spot::NetworkSession& session) {
    m_bound = &session;
    session.GetMessages().On<RemoteAnswer>([](Spot::NetPeerId, RemoteAnswer& answer) {
        if (answer.success) {
            Spot::Log::Info("Server: {}", answer.message);
        } else {
            Spot::Log::Warning("Server: {}", answer.message);
        }
    });
    session.GetMessages().On<RemoteCommand>([this, &session](Spot::NetPeerId from, RemoteCommand& command) {
        Session* room = Session::Get();
        if (!session.IsServer() || !room) {
            return;
        }
        std::vector<std::string> words;
        if (!Spot::Console::Parse(command.line, words) || words.empty()) {
            return;
        }
        const auto allowed = m_remote.find(words.front());
        RemoteAnswer answer;
        if (allowed == m_remote.end()) {
            answer = {false, words.front() + " cannot be run on the server."};
        } else if (allowed->second == Permission::Operator ? !room->IsOperator(from) : !room->MayCheat(from)) {
            answer = {false, allowed->second == Permission::Operator
                                 ? words.front() + " is for the room's operators."
                                 : words.front() + " is for operators while cheats are off."};
        } else {
            Spot::Log::Info("{} runs: {}", room->GetPlayerName(from), command.line);
            const Spot::ConsoleResult result = GetConsole().Execute(command.line);
            answer = {result.success, result.message};
        }
        session.Send(from, std::move(answer));
    });
}

void SessionCommands::OnUpdate(float /*deltaTime*/) {
    auto* network = GetApplication().GetModule<Spot::NetworkModule>();
    Spot::NetworkSession* session = network ? network->GetSession() : nullptr;
    if (session && session != m_bound) {
        Bind(*session);
    } else if (!session) {
        m_bound = nullptr;
    }
}

void SessionCommands::OnStart() {
    Spot::Console& console = GetConsole();
    // Each runs here on the server, and is sent there from a client.
    const auto add = [&](std::string name, std::string description,
                         std::function<Spot::ConsoleResult(const Room&, std::span<const std::string>)> run) {
        AllowRemote(name, Permission::Operator);
        m_registrations.push_back(console.Register(
            {name, std::move(description), Spot::ConsoleFlags::None,
             [this, name, run = std::move(run)](std::span<const std::string> arguments) -> Spot::ConsoleResult {
                 if (IsRemote()) {
                     return Forward(MakeLine(name, arguments))
                                ? Spot::ConsoleResult{true, "Sent to the server"}
                                : Spot::ConsoleResult{false, "Not connected to the room"};
                 }
                 const Room room = GetRoom();
                 if (!room.session) {
                     return {false, room.error};
                 }
                 return run(room, arguments);
             }}));
    };

    add("status", "Shows the room: its players, operators and password",
        [](const Room& room, std::span<const std::string>) -> Spot::ConsoleResult {
            const SessionSettings& settings = room.session->GetSettings();
            const std::vector<Spot::NetPeerId> peers = room.host->GetPeers();
            const int players = static_cast<int>(peers.size()) + (settings.dedicated ? 0 : 1);
            std::string text =
                std::format("{} on port {}, {} of {} players, {}, cheats {}", settings.name, settings.port, players,
                            settings.maxPlayers, settings.password.empty() ? "no password" : "with a password",
                            Spot::Console::Get().AreCheatsEnabled() ? "on" : "off");
            if (!settings.dedicated) {
                text += std::format("\n  {} (host)", settings.playerName);
            }
            for (const Spot::NetPeerId peer : peers) {
                text +=
                    std::format("\n  #{} {}{}, {} ms", static_cast<std::uint32_t>(peer),
                                room.session->GetPlayerName(peer), room.session->IsOperator(peer) ? " (operator)" : "",
                                static_cast<int>(room.host->GetStats(peer).roundTrip * 1000.0f));
            }
            return {true, text};
        });
    add("kick", "Removes a player from the room: kick <name or #peer> [reason]",
        [](const Room& room, std::span<const std::string> arguments) -> Spot::ConsoleResult {
            if (arguments.empty()) {
                return {false, "Usage: kick <name or #peer> [reason]"};
            }
            const Player player = FindPlayer(room, arguments[0]);
            if (player.peer == Spot::NetPeerId::Invalid) {
                return {false, player.error};
            }
            const std::string why = JoinWords(arguments, 1);
            const std::string name = room.session->GetPlayerName(player.peer);
            room.host->Disconnect(player.peer,
                                  why.empty() ? "The host removed you from the room." : "The host removed you: " + why);
            Spot::Log::Info("Kicked {}{}", name, why.empty() ? "" : ": " + why);
            return {true, "Kicked " + name};
        });
    add("password", "Sets the password of those who join from now on: password [new]; none without one",
        [](const Room& room, std::span<const std::string> arguments) -> Spot::ConsoleResult {
            const std::string password = JoinWords(arguments, 0);
            if (!room.session->SetPassword(password)) {
                return {false, "A password has 128 bytes at most."};
            }
            return {true, password.empty() ? "The room has no password now" : "The room's password changed"};
        });
    for (const bool op : {true, false}) {
        add(op ? "op" : "deop",
            op ? "Makes a player an operator, who runs the room and may cheat: op <name or #peer>"
               : "Makes a player no longer an operator: deop <name or #peer>",
            [op](const Room& room, std::span<const std::string> arguments) -> Spot::ConsoleResult {
                if (arguments.size() != 1) {
                    return {false, op ? "Usage: op <name or #peer>" : "Usage: deop <name or #peer>"};
                }
                const Player player = FindPlayer(room, arguments[0]);
                if (player.peer == Spot::NetPeerId::Invalid) {
                    return {false, player.error};
                }
                const std::string name = room.session->GetPlayerName(player.peer);
                if (room.session->IsOperator(player.peer) == op) {
                    return {false, name + (op ? " is an operator already" : " is not an operator")};
                }
                room.session->SetOperator(player.peer, op);
                auto* network = Spot::Application::Get().GetModule<Spot::NetworkModule>();
                network->GetSession()->Send(player.peer,
                                            RemoteAnswer{true, op ? "You are an operator of the room now"
                                                                  : "You are no longer an operator of the room"});
                Spot::Log::Info("{} {} an operator", name, op ? "is now" : "is no longer");
                return {true, name + (op ? " is an operator now" : " is no longer an operator")};
            });
    }
}

void SessionCommands::OnStop() {
    m_registrations.clear();
    m_bound = nullptr;
}

} // namespace Gameplay
