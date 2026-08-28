#include "stdafx.h"

#ifdef _WINDOWS64

#include "NetworkPlayerSockets.h"
#include "TcpLink.h"
#include "..\..\..\..\Minecraft.World\Socket.h"
#include <algorithm>

NetworkPlayerSockets::NetworkPlayerSockets(const wstring& displayName, unsigned char smallId, bool isLocal, bool isHost, int userIndex)
{
	m_displayName = displayName;
	m_uid = MakeUIDFromName(displayName);
	m_pLink = NULL;
	m_pSocket = NULL;
	m_smallId = smallId;
	m_isLocal = isLocal;
	m_isHost = isHost;
	m_userIndex = userIndex;
	// -1 until the manager stamps our position in its player list. It must NOT
	// default to the smallId: MinecraftServer::canSendOnSlowQueue compares this
	// against an index into that list, and smallIds are 1-based and never
	// reused, so the two spaces do not line up. See ReindexPlayers().
	m_sessionIndex = -1;
}

NetworkPlayerSockets::~NetworkPlayerSockets()
{
	// The link is owned by the manager, which closes and deletes it when the peer
	// is removed. Do not delete it here; the receive thread may still be in recv().
	m_pLink = NULL;
}

PlayerUID NetworkPlayerSockets::MakeUIDFromName(const wstring& name)
{
	// FNV-1a, 64 bit.
	unsigned __int64 hash = 14695981039346656037ULL;

	for( unsigned int i = 0; i < name.length(); i++ )
	{
		// Lowercase so "Noah" and "noah" are the same player to the ban list.
		wchar_t c = name[i];
		if( c >= L'A' && c <= L'Z' ) c = c - L'A' + L'a';

		hash ^= (unsigned __int64)(c & 0xFF);
		hash *= 1099511628211ULL;
		hash ^= (unsigned __int64)((c >> 8) & 0xFF);
		hash *= 1099511628211ULL;
	}

	// INVALID_XUID is used as "no player" all over the login path, so never hand
	// it back as a real UID.
	if( hash == (unsigned __int64)INVALID_XUID ) hash = 1;

	return (PlayerUID)hash;
}

unsigned char NetworkPlayerSockets::GetSmallId()
{
	return m_smallId;
}

void NetworkPlayerSockets::SendData(INetworkPlayer *player, const void *pvData, int dataSize, bool lowPriority)
{
	// 'this' is the sender and 'player' is the recipient. Socket.cpp calls this as
	// host->SendData(peer) when serving and local->SendData(host) when joining, so
	// routing on the recipient's link is correct in both directions.
	if( player == NULL ) return;

	TcpLink *pLink = ((NetworkPlayerSockets *)player)->GetLink();
	if( pLink == NULL )
	{
		app.DebugPrintf("NetworkPlayerSockets: no link for recipient smallId %d\n", player->GetSmallId());
		return;
	}

	pLink->Send(pvData, dataSize);
}

bool NetworkPlayerSockets::IsSameSystem(INetworkPlayer *player)
{
	// One player per machine on this transport - splitscreen over TCP is not a
	// thing here - so sharing a system means being the same player.
	return ( player == this );
}

int NetworkPlayerSockets::GetSendQueueSizeBytes( INetworkPlayer *player, bool lowPriority )
{
	// TCP does its own buffering and we have no visibility into it. Reporting zero
	// means the callers that throttle on queue depth simply never throttle.
	return 0;
}

int NetworkPlayerSockets::GetSendQueueSizeMessages( INetworkPlayer *player, bool lowPriority )
{
	return 0;
}

int NetworkPlayerSockets::GetCurrentRtt()
{
	// Not measured. Only used for the debug stats overlay.
	return 0;
}

bool NetworkPlayerSockets::IsHost()
{
	return m_isHost;
}

bool NetworkPlayerSockets::IsGuest()
{
	return false;
}

bool NetworkPlayerSockets::IsLocal()
{
	return m_isLocal;
}

int NetworkPlayerSockets::GetSessionIndex()
{
	return m_sessionIndex;
}

bool NetworkPlayerSockets::IsTalking()
{
	return false;
}

bool NetworkPlayerSockets::IsMutedByLocalUser(int userIndex)
{
	return false;
}

bool NetworkPlayerSockets::HasVoice()
{
	return false;
}

bool NetworkPlayerSockets::HasCamera()
{
	return false;
}

int NetworkPlayerSockets::GetUserIndex()
{
	return m_userIndex;
}

void NetworkPlayerSockets::SetSocket(Socket *pSocket)
{
	m_pSocket = pSocket;
}

Socket *NetworkPlayerSockets::GetSocket()
{
	return m_pSocket;
}

const wchar_t *NetworkPlayerSockets::GetOnlineName()
{
	return m_displayName.c_str();
}

wstring NetworkPlayerSockets::GetDisplayName()
{
	return m_displayName;
}

PlayerUID NetworkPlayerSockets::GetUID()
{
	return m_uid;
}

#endif // _WINDOWS64
