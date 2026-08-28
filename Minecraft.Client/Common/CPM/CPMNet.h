#pragma once
// CPM - Custom Player Models: model sharing between players.
//
// This mirrors what the Java mod does over plugin channels, using LCE's
// CustomPayloadPacket as the transport. The flow is:
//
//   client -> server   CPM_OP_SET, in chunks, the sender's own model
//   server             reassembles, applies the sender's authoritative name,
//                      then rebroadcasts to everyone
//   server -> client   CPM_OP_MODEL, in chunks, {playerName, model}
//
// CustomPayloadPacket carries a 16-bit length, so a model larger than a single
// packet is split; each chunk repeats the total size and its own index so a
// receiver can validate the stream without trusting a prior packet.

#include <string>
#include <vector>

class CustomPayloadPacket;
class MinecraftServer;
class ClientConnection;

// Channel identifier. CustomPayloadPacket::read caps identifiers at 20 chars.
extern const std::wstring CPM_CHANNEL;

class CPMNet
{
public:
	// Client -> server. Sends the local player's current model, or a clear if
	// it has none. Safe to call when not connected; it does nothing.
	static void sendLocalModel(int pad);
	static void sendClear(int pad);

	// Returns true if the packet was a CPM packet and has been consumed.
	// `senderName` is the server's authoritative name for the connection the
	// packet arrived on; a client cannot claim to be someone else.
	static bool handleServer(MinecraftServer *server, const std::wstring &senderName,
	                         CustomPayloadPacket *packet);
	static bool handleClient(CustomPayloadPacket *packet);

	// Re-sends the local player's model after joining a server, so a model
	// chosen in the menus or in a previous session is applied.
	static void onClientJoined(int pad);

	// Builds the packets that bring a newly joined player up to date with every
	// model the server already knows about. The caller sends them on the new
	// player's own connection.
	static void buildJoinSync(std::vector<shared_ptr<CustomPayloadPacket> > &out);

	// Drops any partial transfers. Call on disconnect.
	static void reset();
};
