#include "stdafx.h"
#include "CPMNet.h"
#include "CPMManager.h"
#include "CPMIO.h"
#include "../../Minecraft.h"
#include "../../MultiPlayerLocalPlayer.h"
#include "../../ClientConnection.h"
#include "../../MinecraftServer.h"
#include "../../PlayerList.h"
#include "../../../Minecraft.World/CustomPayloadPacket.h"
#include <map>

const std::wstring CPM_CHANNEL = L"CPM|Data";

// Opcodes, shared by both directions.
#define CPM_OP_MODEL 0
#define CPM_OP_CLEAR 1

// CustomPayloadPacket length is a signed 16-bit field, and the identifier and
// per-chunk header also have to fit, so chunks stay well under that.
#define CPM_CHUNK_SIZE 8000

// Refuse anything implausible before allocating for it.
#define CPM_MAX_MODEL_BYTES (1024 * 1024)
#define CPM_MAX_CHUNKS 256

namespace
{
	// Partial transfers, keyed by the player the model belongs to.
	struct Assembly
	{
		int total;
		int nextIndex;
		std::vector<unsigned char> data;
		Assembly() : total(0), nextIndex(0) {}
	};

	std::map<std::wstring, Assembly> g_clientAssembly;
	std::map<std::wstring, Assembly> g_serverAssembly;

	// UTF-16 name <-> the byte-oriented CPMOut/CPMIn. Player names are ASCII in
	// practice; anything outside that is replaced rather than mangled.
	std::string narrow(const std::wstring &w)
	{
		std::string s;
		s.reserve(w.size());
		for (size_t i = 0; i < w.size(); i++)
			s += (w[i] < 128 && w[i] > 0) ? (char)w[i] : '?';
		return s;
	}

	std::wstring widen(const std::string &s)
	{
		std::wstring w;
		w.reserve(s.size());
		for (size_t i = 0; i < s.size(); i++) w += (wchar_t)(unsigned char)s[i];
		return w;
	}

	byteArray toByteArray(const CPMOut &out)
	{
		const std::vector<unsigned char> &b = out.bytes();
		byteArray a((unsigned int)b.size(), false);
		for (size_t i = 0; i < b.size(); i++) a.data[i] = (byte)b[i];
		return a;
	}

	shared_ptr<CustomPayloadPacket> makePacket(const CPMOut &out)
	{
		return shared_ptr<CustomPayloadPacket>(
			new CustomPayloadPacket(CPM_CHANNEL, toByteArray(out)));
	}

	// Builds the chunk packets for one model. `owner` is empty for C2S, where
	// the server supplies the name instead.
	void buildChunks(const std::wstring &owner,
	                 const std::vector<unsigned char> &model,
	                 std::vector<shared_ptr<CustomPayloadPacket> > &out)
	{
		int total = (int)model.size();
		int chunks = (total + CPM_CHUNK_SIZE - 1) / CPM_CHUNK_SIZE;
		if (chunks == 0) chunks = 1;

		for (int i = 0; i < chunks; i++)
		{
			int off = i * CPM_CHUNK_SIZE;
			int len = total - off;
			if (len > CPM_CHUNK_SIZE) len = CPM_CHUNK_SIZE;
			if (len < 0) len = 0;

			CPMOut o;
			o.write(CPM_OP_MODEL);
			o.writeUTF(narrow(owner));
			o.writeInt(total);
			o.writeVarInt(i);
			o.writeVarInt(chunks);
			o.writeVarInt(len);
			if (len > 0) o.write(&model[off], len);

			out.push_back(makePacket(o));
		}
	}

	// Feeds one chunk into an assembly table. Returns true when the model is
	// complete, leaving the bytes in `full`.
	bool acceptChunk(std::map<std::wstring, Assembly> &table,
	                 const std::wstring &key, CPMIn &in,
	                 std::vector<unsigned char> &full)
	{
		int total = in.readInt();
		int index = in.readVarInt();
		int count = in.readVarInt();
		int len = in.readVarInt();

		if (in.fail()) return false;
		if (total <= 0 || total > CPM_MAX_MODEL_BYTES) return false;
		if (count <= 0 || count > CPM_MAX_CHUNKS) return false;
		if (index < 0 || index >= count) return false;
		if (len < 0 || len > CPM_CHUNK_SIZE || len > in.remaining()) return false;

		Assembly &a = table[key];

		// A restarted or mismatched transfer resets rather than corrupting.
		if (index == 0 || a.total != total)
		{
			a.total = total;
			a.nextIndex = 0;
			a.data.clear();
			a.data.reserve(total);
		}
		if (index != a.nextIndex)
		{
			table.erase(key);
			return false;
		}

		size_t before = a.data.size();
		a.data.resize(before + len);
		if (len > 0) in.readFully(&a.data[before], len);
		if (in.fail() || (int)a.data.size() > total)
		{
			table.erase(key);
			return false;
		}
		a.nextIndex++;

		if (a.nextIndex < count) return false;
		if ((int)a.data.size() != total) { table.erase(key); return false; }

		full.swap(a.data);
		table.erase(key);
		return true;
	}
}

//////////////////////////////////////////////////////////////////////////
// Client -> server
//////////////////////////////////////////////////////////////////////////

void CPMNet::sendLocalModel(int pad)
{
	Minecraft *mc = Minecraft::GetInstance();
	if (mc == NULL) return;

	ClientConnection *conn = mc->getConnection(pad);
	if (conn == NULL) return;

	CPMPlayerModel *m = CPMManager::getLocalModel();
	if (m == NULL || m->raw.empty())
	{
		sendClear(pad);
		return;
	}

	std::vector<shared_ptr<CustomPayloadPacket> > packets;
	buildChunks(L"", m->raw, packets);
	for (size_t i = 0; i < packets.size(); i++) conn->send(packets[i]);
}

void CPMNet::sendClear(int pad)
{
	Minecraft *mc = Minecraft::GetInstance();
	if (mc == NULL) return;

	ClientConnection *conn = mc->getConnection(pad);
	if (conn == NULL) return;

	CPMOut o;
	o.write(CPM_OP_CLEAR);
	o.writeUTF("");
	conn->send(makePacket(o));
}

//////////////////////////////////////////////////////////////////////////
// Server
//////////////////////////////////////////////////////////////////////////

bool CPMNet::handleServer(MinecraftServer *server, const std::wstring &senderName,
                          CustomPayloadPacket *packet)
{
	if (packet == NULL) return false;
	if (CPM_CHANNEL.compare(packet->identifier) != 0) return false;
	if (server == NULL || server->getPlayers() == NULL) return true;
	if (packet->data.data == NULL || packet->length <= 0) return true;

	CPMIn in((const unsigned char *)packet->data.data, packet->length);
	int op = in.read();

	// The name the client put in the packet is ignored - the server uses the
	// name of the connection the packet arrived on.
	std::string claimed = in.readUTF();
	(void)claimed;

	if (op == CPM_OP_CLEAR)
	{
		CPMManager::clearFor(senderName);
		g_serverAssembly.erase(senderName);

		CPMOut o;
		o.write(CPM_OP_CLEAR);
		o.writeUTF(narrow(senderName));
		server->getPlayers()->broadcastAll(makePacket(o));
		return true;
	}

	if (op != CPM_OP_MODEL) return true;

	std::vector<unsigned char> full;
	if (!acceptChunk(g_serverAssembly, senderName, in, full)) return true;

	// Validate before relaying, so one malformed model does not reach everyone.
	std::string err;
	if (!CPMManager::assignFromBytes(senderName, &full[0], (int)full.size(), err))
		return true;

	std::vector<shared_ptr<CustomPayloadPacket> > packets;
	buildChunks(senderName, full, packets);
	for (size_t i = 0; i < packets.size(); i++)
		server->getPlayers()->broadcastAll(packets[i]);

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Client
//////////////////////////////////////////////////////////////////////////

bool CPMNet::handleClient(CustomPayloadPacket *packet)
{
	if (packet == NULL) return false;
	if (CPM_CHANNEL.compare(packet->identifier) != 0) return false;
	if (packet->data.data == NULL || packet->length <= 0) return true;

	CPMIn in((const unsigned char *)packet->data.data, packet->length);
	int op = in.read();
	std::wstring owner = widen(in.readUTF());
	if (in.fail() || owner.empty()) return true;

	if (op == CPM_OP_CLEAR)
	{
		CPMManager::clearFor(owner);
		g_clientAssembly.erase(owner);
		return true;
	}

	if (op != CPM_OP_MODEL) return true;

	std::vector<unsigned char> full;
	if (!acceptChunk(g_clientAssembly, owner, in, full)) return true;

	std::string err;
	CPMManager::assignFromBytes(owner, &full[0], (int)full.size(), err);
	return true;
}

void CPMNet::onClientJoined(int pad)
{
	// Only send if this client actually has a model; sendLocalModel falls back
	// to a clear, which would needlessly clear the player on the server.
	if (CPMManager::getLocalModel() != NULL) sendLocalModel(pad);
}

void CPMNet::buildJoinSync(std::vector<shared_ptr<CustomPayloadPacket> > &out)
{
	std::vector<std::wstring> owners;
	std::vector<const std::vector<unsigned char> *> datas;
	CPMManager::collectAll(owners, datas);

	for (size_t i = 0; i < owners.size(); i++)
		buildChunks(owners[i], *datas[i], out);
}

void CPMNet::reset()
{
	g_clientAssembly.clear();
	g_serverAssembly.clear();
}
