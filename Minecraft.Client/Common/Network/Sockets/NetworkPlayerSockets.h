#pragma once

#ifdef _WINDOWS64

#include "..\NetworkPlayerInterface.h"

class TcpLink;

// An implementation of INetworkPlayer backed by a plain TCP link rather than QNET
// or SQR. Managed by CPlatformNetworkManagerSockets.
//
// Identity here is a display name the player typed, not a platform account. The
// UID is derived from that name (see MakeUIDFromName) so that the surrounding
// XUID-keyed machinery - bans, GetPlayerByXuid, the UGC lists - keeps working
// unmodified. This is trust-on-assertion: anyone can claim any name. That is
// acceptable for a friends-only server and is documented rather than hidden.

class NetworkPlayerSockets : public INetworkPlayer
{
public:
	NetworkPlayerSockets(const wstring& displayName, unsigned char smallId, bool isLocal, bool isHost, int userIndex);
	virtual ~NetworkPlayerSockets();

	// INetworkPlayer
	virtual unsigned char GetSmallId();
	virtual void SendData(INetworkPlayer *player, const void *pvData, int dataSize, bool lowPriority);
	virtual bool IsSameSystem(INetworkPlayer *player);
	virtual int GetSendQueueSizeBytes( INetworkPlayer *player, bool lowPriority );
	virtual int GetSendQueueSizeMessages( INetworkPlayer *player, bool lowPriority );
	virtual int GetCurrentRtt();
	virtual bool IsHost();
	virtual bool IsGuest();
	virtual bool IsLocal();
	virtual int GetSessionIndex();
	virtual bool IsTalking();
	virtual bool IsMutedByLocalUser(int userIndex);
	virtual bool HasVoice();
	virtual bool HasCamera();
	virtual int GetUserIndex();
	virtual void SetSocket(Socket *pSocket);
	virtual Socket *GetSocket();
	virtual const wchar_t *GetOnlineName();
	virtual wstring GetDisplayName();
	virtual PlayerUID GetUID();

	// Sockets-specific
	void SetLink(TcpLink *pLink) { m_pLink = pLink; }
	TcpLink *GetLink() { return m_pLink; }

	void SetHost(bool isHost) { m_isHost = isHost; }
	void SetSessionIndex(int index) { m_sessionIndex = index; }

	// FNV-1a over the lowercased name. Stable across sessions and machines, which
	// is what the ban list and player-matching code assume of an XUID.
	static PlayerUID MakeUIDFromName(const wstring& name);

private:
	wstring		m_displayName;
	PlayerUID	m_uid;
	TcpLink		*m_pLink;
	Socket		*m_pSocket;
	unsigned char m_smallId;
	bool		m_isLocal;
	bool		m_isHost;
	int			m_userIndex;
	int			m_sessionIndex;
};

#endif // _WINDOWS64
