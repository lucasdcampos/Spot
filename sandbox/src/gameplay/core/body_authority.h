#pragma once

#include <spot.h>

#include <vector>

namespace Gameplay {

// Who simulates the bodies of replicated objects the server decides, as props. In a session, the server may give one
// to a client for a while: what that client's player pushes, drives into or carries then moves at once there, rather
// than standing still as the server's until the server is heard from, and the other machines are shown it. The server
// takes it back once nothing of that player's is near it for a while (NetworkPlayers decides when), or at once when
// code here must act on it, as a blast pushing it. Pawns are never given. Alone, nothing ever is.
namespace BodyAuthority {

// On the server: gives the object of the body to the client to simulate; false when it is a pawn's, has no body, is
// held by a joint or is already another client's. Giving it to the client that has it does nothing.
bool Give(Spot::Entity& body, Spot::NetPeerId peer);
// On the server: takes back the object of the body, if it was given, so its body is simulated here again.
void TakeBack(Spot::Entity& body);
// On the server: the client the object of the body was given to; Invalid when it is the server's.
Spot::NetPeerId GetHolder(const Spot::Entity& body);
// On the server: the entities of the objects given to the client, whose bodies it moves.
std::vector<Spot::EntityId> GetGiven(Spot::Scene& scene, Spot::NetPeerId peer);
// Gives the object of the body to no client, taking it back, while kept: as what flies and hits on its own, as a
// rocket, whose hits are the server's, or what the server holds, as with the physics gun.
void KeepOnServer(Spot::Entity& body, bool keep = true);

} // namespace BodyAuthority

} // namespace Gameplay
