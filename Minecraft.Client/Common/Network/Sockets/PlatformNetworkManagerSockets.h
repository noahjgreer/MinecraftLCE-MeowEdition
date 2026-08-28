#pragma once

#ifdef _WINDOWS64

using namespace std;
#include <vector>
#include "..\..\..\..\Minecraft.World\C4JThread.h"
#include "..\NetworkPlayerInterface.h"
#include "..\PlatformNetworkManagerInterface.h"
#include "..\SessionInfo.h"

class TcpLink;
class TcpListener;
class NetworkPlayerSockets;

// The Windows x64 platform network manager, over plain TCP.
//
// Replaces CPlatformNetworkManagerStub on this platform. The stub is written
// against IQNet / IQNetPlayer, which on x64 are empty class shells in
// Minecraft.World/x64headers/extraX64.h - it compiles and does nothing, and
// m_pIQNet is never even assigned.
//
// This class owns peer lifetime and identity; TcpLink owns the bytes. See
// docs/systems/dedicated-server-and-direct-connect.md.

#define MINECRAFT_DEFAULT_SERVER_PORT 25565

class CPlatformNetworkManagerSockets : public CPlatformNetworkManager
{
	friend class CGameNetworkManager;

public:
	CPlatformNetworkManagerSockets();

	virtual bool Initialise(CGameNetworkManager *pGameNetworkManager, int flagIndexSize);
	virtual void Terminate();
	virtual int GetJoiningReadyPercentage();
	virtual int CorrectErrorIDS(int IDS);

	virtual void DoWork();
	virtual int GetPlayerCount();
	virtual int GetOnlinePlayerCount();
	virtual int GetLocalPlayerMask(int playerIndex);
	virtual bool AddLocalPlayerByUserIndex( int userIndex );
	virtual bool RemoveLocalPlayerByUserIndex( int userIndex );
	virtual INetworkPlayer *GetLocalPlayerByUserIndex( int userIndex );
	virtual INetworkPlayer *GetPlayerByIndex(int playerIndex);
	virtual INetworkPlayer *GetPlayerByXuid(PlayerUID xuid);
	virtual INetworkPlayer *GetPlayerBySmallId(unsigned char smallId);
	virtual bool ShouldMessageForFullSession();

	virtual INetworkPlayer *GetHostPlayer();
	virtual bool IsHost();
	virtual bool JoinGameFromInviteInfo( int userIndex, int userMask, const INVITE_INFO *pInviteInfo);
	virtual bool LeaveGame(bool bMigrateHost);

	virtual bool IsInSession();
	virtual bool IsInGameplay();
	virtual bool IsReadyToPlayOrIdle();
	virtual bool IsInStatsEnabledSession();
	virtual bool SessionHasSpace(unsigned int spaceRequired = 1);
	virtual void SendInviteGUI(int quadrant);
	virtual bool IsAddingPlayer();

	virtual void HostGame(int localUsersMask, bool bOnlineGame, bool bIsPrivate, unsigned char publicSlots = MINECRAFT_NET_MAX_PLAYERS, unsigned char privateSlots = 0);
	virtual int  JoinGame(FriendSessionInfo *searchResult, int localUsersMask, int primaryUserIndex );
	virtual bool SetLocalGame(bool isLocal);
	virtual bool IsLocalGame() { return m_bIsOfflineGame; }
	virtual void SetPrivateGame(bool isPrivate);
	virtual bool IsPrivateGame() { return m_bIsPrivateGame; }
	virtual bool IsLeavingGame() { return m_bLeavingGame; }
	virtual void ResetLeavingGame() { m_bLeavingGame = false; }

	virtual void RegisterPlayerChangedCallback(int iPad, void (*callback)(void *callbackParam, INetworkPlayer *pPlayer, bool leaving), void *callbackParam);
	virtual void UnRegisterPlayerChangedCallback(int iPad, void (*callback)(void *callbackParam, INetworkPlayer *pPlayer, bool leaving), void *callbackParam);

	virtual void HandleSignInChange();
	virtual bool _RunNetworkGame();

	virtual void UpdateAndSetGameSessionData(INetworkPlayer *pNetworkPlayerLeaving = NULL);

	virtual void SystemFlagSet(INetworkPlayer *pNetworkPlayer, int index);
	virtual bool SystemFlagGet(INetworkPlayer *pNetworkPlayer, int index);

	virtual wstring GatherStats();
	virtual wstring GatherRTTStats();

	virtual vector<FriendSessionInfo *> *GetSessionList(int iPad, int localPlayers, bool partyOnly);
	virtual bool GetGameSessionInfo(int iPad, SessionID sessionId, FriendSessionInfo *foundSession);
	virtual void SetSessionsUpdatedCallback( void (*SessionsUpdatedCallback)(LPVOID pParam), LPVOID pSearchParam );
	virtual void GetFullFriendSessionInfo( FriendSessionInfo *foundSession, void (* FriendSessionUpdatedFn)(bool success, void *pParam), void *pParam );
	virtual void ForceFriendsSessionRefresh();

	virtual void FakeLocalPlayerJoined();

	////////////////////////////////////////////////////////////////////////
	// Sockets-specific API, used by the direct-connect UI
	////////////////////////////////////////////////////////////////////////

	// The name this machine's player presents to a server.
	void SetLocalDisplayName(const wstring& name);
	wstring GetLocalDisplayName() { return m_localDisplayName; }

	// Host: open the listening port. Call after HostGame().
	bool StartListening(int port = MINECRAFT_DEFAULT_SERVER_PORT);
	void StopListening();
	bool IsListening();

	// Client: connect out to a server. Creates the host player and the local
	// player, and leaves the caller to build a ClientConnection on the local
	// player's socket. Returns false if the TCP connect failed.
	bool ConnectToServer(const char *hostName, int port);

	// The socket the local player should build its ClientConnection on, valid
	// after a successful ConnectToServer().
	Socket *GetLocalPlayerSocket();

private:
	void AcceptPendingConnections();
	void DropDeadPeers();

	NetworkPlayerSockets *AddPeer(const wstring& displayName, bool isLocal, bool isHost, int userIndex, TcpLink *pLink);
	void RemovePeer(NetworkPlayerSockets *pPlayer);
	unsigned char AllocateSmallId();

	virtual bool _LeaveGame(bool bMigrateHost, bool bLeaveRoom);
	virtual void _HostGame(int usersMask, unsigned char publicSlots = MINECRAFT_NET_MAX_PLAYERS, unsigned char privateSlots = 0);
	virtual bool _StartGame();
	virtual bool RemoveLocalPlayer( INetworkPlayer *pNetworkPlayer );

	virtual void SetSessionTexturePackParentId( int id );
	virtual void SetSessionSubTexturePackId( int id );
	virtual void Notify(int ID, ULONG_PTR Param);

	// Per-system flag storage, carried over from the stub. On this transport one
	// player is one system, but the game asks for it either way.
	class PlayerFlags
	{
	public:
		INetworkPlayer *m_pNetworkPlayer;
		unsigned char *flags;
		unsigned int count;
		PlayerFlags(INetworkPlayer *pNetworkPlayer, unsigned int count);
		~PlayerFlags();
	};
	vector<PlayerFlags *> m_playerFlags;
	void SystemFlagAddPlayer(INetworkPlayer *pNetworkPlayer);
	void SystemFlagRemovePlayer(INetworkPlayer *pNetworkPlayer);
	void SystemFlagReset();

	// Keeps GetSessionIndex() in the same index space as GetPlayerByIndex().
	// Callers must hold m_playersLock. See the definition for why.
	void ReindexPlayers();

	enum eSessionState
	{
		eState_Idle = 0,
		eState_Lobby,
		eState_Gameplay,
	};

	CGameNetworkManager	*m_pGameNetworkManager;
	int					m_flagIndexSize;

	eSessionState		m_state;
	bool				m_bIsHost;
	bool				m_bIsOfflineGame;
	bool				m_bIsPrivateGame;
	bool				m_bLeavingGame;

	wstring				m_localDisplayName;

	vector<NetworkPlayerSockets *> m_players;
	NetworkPlayerSockets *m_pLocalPlayer;
	NetworkPlayerSockets *m_pHostPlayer;

	TcpListener			*m_pListener;
	unsigned char		m_nextSmallId;
	unsigned char		m_publicSlots;

	CRITICAL_SECTION	m_playersLock;

	void (*playerChangedCallback[XUSER_MAX_COUNT])(void *callbackParam, INetworkPlayer *pPlayer, bool leaving);
	void *playerChangedCallbackParam[XUSER_MAX_COUNT];

	GameSessionData		m_hostGameSessionData;
};

extern CPlatformNetworkManagerSockets *g_pPlatformNetworkManagerSockets;

#endif // _WINDOWS64
