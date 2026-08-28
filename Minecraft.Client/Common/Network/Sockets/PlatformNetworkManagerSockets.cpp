#include "stdafx.h"

#ifdef _WINDOWS64

#include "PlatformNetworkManagerSockets.h"
#include "NetworkPlayerSockets.h"
#include "TcpLink.h"
#include "..\GameNetworkManager.h"
#include "..\..\..\..\Minecraft.World\Socket.h"
#include "..\..\..\Minecraft.h"

CPlatformNetworkManagerSockets *g_pPlatformNetworkManagerSockets = NULL;

CPlatformNetworkManagerSockets::CPlatformNetworkManagerSockets()
{
	m_pGameNetworkManager = NULL;
	m_flagIndexSize = 0;
	m_state = eState_Idle;
	m_bIsHost = false;
	m_bIsOfflineGame = true;
	m_bIsPrivateGame = false;
	m_bLeavingGame = false;
	m_localDisplayName = L"Player";
	m_pLocalPlayer = NULL;
	m_pHostPlayer = NULL;
	m_pListener = NULL;
	m_nextSmallId = 1;			// 0 means "no player" - Socket::setPlayer(NULL) stores it
	m_publicSlots = MINECRAFT_NET_MAX_PLAYERS;
	memset(&m_hostGameSessionData, 0, sizeof(m_hostGameSessionData));
}

bool CPlatformNetworkManagerSockets::Initialise(CGameNetworkManager *pGameNetworkManager, int flagIndexSize)
{
	m_pGameNetworkManager = pGameNetworkManager;
	m_flagIndexSize = flagIndexSize;
	g_pPlatformNetworkManagerSockets = this;

	InitializeCriticalSection(&m_playersLock);

	for( int i = 0; i < XUSER_MAX_COUNT; i++ )
	{
		playerChangedCallback[i] = NULL;
		playerChangedCallbackParam[i] = NULL;
	}

	TcpLink::InitialiseWinsock();

	return true;
}

void CPlatformNetworkManagerSockets::Terminate()
{
	StopListening();

	EnterCriticalSection(&m_playersLock);
	for( unsigned int i = 0; i < m_players.size(); i++ )
	{
		TcpLink *pLink = m_players[i]->GetLink();
		if( pLink != NULL )
		{
			pLink->Close();
			delete pLink;
		}
		delete m_players[i];
	}
	m_players.clear();
	m_pLocalPlayer = NULL;
	m_pHostPlayer = NULL;
	LeaveCriticalSection(&m_playersLock);

	SystemFlagReset();

	TcpLink::ShutdownWinsock();
	DeleteCriticalSection(&m_playersLock);
}

////////////////////////////////////////////////////////////////////////
// Peer management
////////////////////////////////////////////////////////////////////////

unsigned char CPlatformNetworkManagerSockets::AllocateSmallId()
{
	// Wrap past 0 - it is reserved for "no player".
	if( m_nextSmallId == 0 ) m_nextSmallId = 1;
	return m_nextSmallId++;
}

// 4J Meow - Stamp each player with its position in m_players.
//
// MinecraftServer::canSendOnSlowQueue gates world streaming on
//
//     player->GetSessionIndex() == s_slowQueuePlayerIndex
//
// and cycleSlowQueueIndex only ever produces values in [0, GetPlayerCount()),
// because it walks the list with GetPlayerByIndex. So GetSessionIndex has to
// live in the same index space as GetPlayerByIndex. It previously returned the
// smallId, which is 1-based and never reused, so with a host plus two joiners
// the second joiner's index (3) could never come up in a rotation over {0,1,2}
// and it never got a single chunk past the one the login path forces out.
//
// Positions shift when someone leaves, so this must run on every add and
// remove. Callers hold m_playersLock.
void CPlatformNetworkManagerSockets::ReindexPlayers()
{
	for( unsigned int i = 0; i < m_players.size(); i++ )
	{
		m_players[i]->SetSessionIndex( (int)i );
	}
}

NetworkPlayerSockets *CPlatformNetworkManagerSockets::AddPeer(const wstring& displayName, bool isLocal, bool isHost, int userIndex, TcpLink *pLink)
{
	NetworkPlayerSockets *pPlayer = new NetworkPlayerSockets(displayName, AllocateSmallId(), isLocal, isHost, userIndex);
	pPlayer->SetLink(pLink);

	EnterCriticalSection(&m_playersLock);
	m_players.push_back(pPlayer);
	ReindexPlayers();
	LeaveCriticalSection(&m_playersLock);

	SystemFlagAddPlayer(pPlayer);

	app.DebugPrintf("Sockets: added peer \"%ls\" smallId %d (local=%d host=%d)\n",
		displayName.c_str(), pPlayer->GetSmallId(), isLocal ? 1 : 0, isHost ? 1 : 0);

	return pPlayer;
}

void CPlatformNetworkManagerSockets::RemovePeer(NetworkPlayerSockets *pPlayer)
{
	if( pPlayer == NULL ) return;

	app.DebugPrintf("Sockets: removing peer \"%ls\" smallId %d\n", pPlayer->GetDisplayName().c_str(), pPlayer->GetSmallId());

	for( int i = 0; i < XUSER_MAX_COUNT; i++ )
	{
		if( playerChangedCallback[i] != NULL )
		{
			playerChangedCallback[i](playerChangedCallbackParam[i], pPlayer, true);
		}
	}

	g_NetworkManager.PlayerLeaving(pPlayer);

	SystemFlagRemovePlayer(pPlayer);

	EnterCriticalSection(&m_playersLock);
	for( AUTO_VAR(it, m_players.begin()); it != m_players.end(); it++ )
	{
		if( *it == pPlayer )
		{
			m_players.erase(it);
			break;
		}
	}
	if( m_pLocalPlayer == pPlayer ) m_pLocalPlayer = NULL;
	if( m_pHostPlayer == pPlayer ) m_pHostPlayer = NULL;
	ReindexPlayers();
	LeaveCriticalSection(&m_playersLock);

	// The Socket is owned by whoever called CreateSocket and is torn down by the
	// connection layer; only the link is ours.
	TcpLink *pLink = pPlayer->GetLink();
	if( pLink != NULL )
	{
		pLink->Close();
		delete pLink;
		pPlayer->SetLink(NULL);
	}

	delete pPlayer;
}

////////////////////////////////////////////////////////////////////////
// Hosting and joining
////////////////////////////////////////////////////////////////////////

void CPlatformNetworkManagerSockets::SetLocalDisplayName(const wstring& name)
{
	if( name.empty() ) return;
	m_localDisplayName = name;
}

bool CPlatformNetworkManagerSockets::StartListening(int port)
{
	if( m_pListener == NULL ) m_pListener = new TcpListener();

	if( !m_pListener->Start(port) )
	{
		app.DebugPrintf("Sockets: failed to listen on port %d\n", port);
		return false;
	}

	app.DebugPrintf("Sockets: hosting on port %d\n", port);
	return true;
}

void CPlatformNetworkManagerSockets::StopListening()
{
	if( m_pListener == NULL ) return;
	m_pListener->Stop();
	delete m_pListener;
	m_pListener = NULL;
}

bool CPlatformNetworkManagerSockets::IsListening()
{
	return ( m_pListener != NULL && m_pListener->IsListening() );
}

bool CPlatformNetworkManagerSockets::ConnectToServer(const char *hostName, int port)
{
	TcpLink *pLink = TcpLink::Connect(hostName, port);
	if( pLink == NULL ) return false;

	m_bIsHost = false;
	m_bIsOfflineGame = false;
	m_bLeavingGame = false;

	// Deliberately Lobby, not Gameplay, across the CreateSocket call below.
	// CGameNetworkManager::CreateSocket builds a ClientConnection of its own when
	// ( localPlayer && IsInGameplay() ) - that is the splitscreen join-in-progress
	// path - and ConnectScreen is about to build the real one. Being in Gameplay
	// here would give us two connections on one socket.
	m_state = eState_Lobby;

	// The remote host, carrying the link. Its name is not known until the server
	// has told us, so present the address until then.
	wchar_t szHostName[64];
	swprintf(szHostName, 64, L"%hs", pLink->GetPeerAddress());
	m_pHostPlayer = AddPeer(szHostName, false, true, 0, pLink);

	// Our own player. It sends through the host's link, so it needs none of its own.
	m_pLocalPlayer = AddPeer(m_localDisplayName, true, false, ProfileManager.GetPrimaryPad(), NULL);

	// Socket(player, response = IsHost() = false, hostLocal = false) gives the
	// client end of a network socket, which is what ClientConnection wants.
	g_NetworkManager.CreateSocket(m_pLocalPlayer, true);

	Socket *pSocket = m_pLocalPlayer->GetSocket();
	if( pSocket == NULL )
	{
		app.DebugPrintf("Sockets: CreateSocket produced no socket for the local player\n");
		return false;
	}

	// Bytes arriving on this link came from the host.
	pLink->StartReceiving(pSocket, true);

	g_NetworkManager.PlayerJoining(m_pLocalPlayer);

	// Safe now that CreateSocket has been and gone.
	m_state = eState_Gameplay;

	return true;
}

Socket *CPlatformNetworkManagerSockets::GetLocalPlayerSocket()
{
	return ( m_pLocalPlayer != NULL ) ? m_pLocalPlayer->GetSocket() : NULL;
}

void CPlatformNetworkManagerSockets::AcceptPendingConnections()
{
	if( m_pListener == NULL || !m_pListener->IsListening() ) return;

	// Non-blocking; returns NULL the moment nobody is waiting.
	TcpLink *pLink = m_pListener->Accept();
	while( pLink != NULL )
	{
		if( !SessionHasSpace(1) )
		{
			app.DebugPrintf("Sockets: refusing connection from %s, session full\n", pLink->GetPeerAddress());
			pLink->Close();
			delete pLink;
		}
		else
		{
			// The real display name arrives in the PreLoginPacket, which travels
			// over this very socket, so the peer is provisionally named by address.
			wchar_t szPeerName[64];
			swprintf(szPeerName, 64, L"%hs", pLink->GetPeerAddress());

			NetworkPlayerSockets *pPeer = AddPeer(szPeerName, false, false, 0, pLink);

			g_NetworkManager.PlayerJoining(pPeer);

			// IsHost() is true here, so this yields the server end of a network
			// socket and hands it to ServerConnection via addIncomingSocket.
			g_NetworkManager.CreateSocket(pPeer, false);

			Socket *pSocket = pPeer->GetSocket();
			if( pSocket != NULL )
			{
				// Bytes arriving here came from a client, not from the host.
				pLink->StartReceiving(pSocket, false);

				for( int i = 0; i < XUSER_MAX_COUNT; i++ )
				{
					if( playerChangedCallback[i] != NULL )
					{
						playerChangedCallback[i](playerChangedCallbackParam[i], pPeer, false);
					}
				}
			}
			else
			{
				app.DebugPrintf("Sockets: CreateSocket produced no socket for peer %s\n", pLink->GetPeerAddress());
				RemovePeer(pPeer);
			}
		}

		pLink = m_pListener->Accept();
	}
}

void CPlatformNetworkManagerSockets::DropDeadPeers()
{
	NetworkPlayerSockets *pDead = NULL;

	EnterCriticalSection(&m_playersLock);
	for( unsigned int i = 0; i < m_players.size(); i++ )
	{
		TcpLink *pLink = m_players[i]->GetLink();
		if( pLink != NULL && pLink->IsClosed() )
		{
			pDead = m_players[i];
			break;
		}
	}
	LeaveCriticalSection(&m_playersLock);

	// One per tick - RemovePeer reaches back into the game layers, so it must not
	// run with the players lock held.
	if( pDead != NULL ) RemovePeer(pDead);
}

// Called twice a frame, either side of the render call.
void CPlatformNetworkManagerSockets::DoWork()
{
	AcceptPendingConnections();
	DropDeadPeers();
}

////////////////////////////////////////////////////////////////////////
// Session lifecycle
////////////////////////////////////////////////////////////////////////

void CPlatformNetworkManagerSockets::HostGame(int localUsersMask, bool bOnlineGame, bool bIsPrivate, unsigned char publicSlots, unsigned char privateSlots)
{
	SetLocalGame( !bOnlineGame );
	SetPrivateGame( bIsPrivate );
	SystemFlagReset();

	m_bLeavingGame = false;
	m_bIsHost = true;
	m_publicSlots = publicSlots;
	m_state = eState_Lobby;

	// Hosting always has a local player, and on this transport it is the host.
	if( m_pLocalPlayer == NULL )
	{
		m_pLocalPlayer = AddPeer(m_localDisplayName, true, true, ProfileManager.GetPrimaryPad(), NULL);
		m_pHostPlayer = m_pLocalPlayer;
	}

	// An online host is reachable; a local game is not, and opens no port.
	if( bOnlineGame ) StartListening();

	_HostGame( localUsersMask, publicSlots, privateSlots );
}

void CPlatformNetworkManagerSockets::_HostGame(int usersMask, unsigned char publicSlots, unsigned char privateSlots)
{
}

bool CPlatformNetworkManagerSockets::_StartGame()
{
	m_state = eState_Gameplay;
	return true;
}

bool CPlatformNetworkManagerSockets::_RunNetworkGame()
{
	return true;
}

int CPlatformNetworkManagerSockets::JoinGame(FriendSessionInfo *searchResult, int localUsersMask, int primaryUserIndex)
{
	// Joining by platform session/invite does not exist on this transport; the
	// direct-connect UI calls ConnectToServer() instead.
	return CGameNetworkManager::JOINGAME_SUCCESS;
}

bool CPlatformNetworkManagerSockets::JoinGameFromInviteInfo( int userIndex, int userMask, const INVITE_INFO *pInviteInfo)
{
	return false;
}

bool CPlatformNetworkManagerSockets::LeaveGame(bool bMigrateHost)
{
	if( m_bLeavingGame ) return true;
	m_bLeavingGame = true;

	StopListening();

	if( m_bIsHost && g_NetworkManager.ServerStoppedValid() )
	{
		g_NetworkManager.ServerStoppedWait();
		g_NetworkManager.ServerStoppedDestroy();
	}

	// Drop every peer's link so the blocked reads in Connection unwind.
	EnterCriticalSection(&m_playersLock);
	for( unsigned int i = 0; i < m_players.size(); i++ )
	{
		TcpLink *pLink = m_players[i]->GetLink();
		if( pLink != NULL ) pLink->Close();
	}
	LeaveCriticalSection(&m_playersLock);

	m_state = eState_Idle;
	m_bIsHost = false;

	return true;
}

bool CPlatformNetworkManagerSockets::_LeaveGame(bool bMigrateHost, bool bLeaveRoom)
{
	return true;
}

bool CPlatformNetworkManagerSockets::IsInSession()
{
	return ( m_state != eState_Idle );
}

bool CPlatformNetworkManagerSockets::IsInGameplay()
{
	return ( m_state == eState_Gameplay );
}

bool CPlatformNetworkManagerSockets::IsReadyToPlayOrIdle()
{
	return true;
}

bool CPlatformNetworkManagerSockets::SetLocalGame(bool isLocal)
{
	m_bIsOfflineGame = isLocal;

	// A game that has gone local should not still be accepting strangers.
	if( isLocal ) StopListening();

	return true;
}

void CPlatformNetworkManagerSockets::SetPrivateGame(bool isPrivate)
{
	app.DebugPrintf("Sockets: setting as private game: %s\n", isPrivate ? "yes" : "no");
	m_bIsPrivateGame = isPrivate;
}

////////////////////////////////////////////////////////////////////////
// Player queries
////////////////////////////////////////////////////////////////////////

int CPlatformNetworkManagerSockets::GetPlayerCount()
{
	return (int)m_players.size();
}

int CPlatformNetworkManagerSockets::GetOnlinePlayerCount()
{
	return m_bIsOfflineGame ? 0 : (int)m_players.size();
}

int CPlatformNetworkManagerSockets::GetLocalPlayerMask(int playerIndex)
{
	return 1 << playerIndex;
}

INetworkPlayer *CPlatformNetworkManagerSockets::GetLocalPlayerByUserIndex( int userIndex )
{
	if( m_pLocalPlayer != NULL && m_pLocalPlayer->GetUserIndex() == userIndex ) return m_pLocalPlayer;
	return NULL;
}

INetworkPlayer *CPlatformNetworkManagerSockets::GetPlayerByIndex(int playerIndex)
{
	if( playerIndex < 0 || playerIndex >= (int)m_players.size() ) return NULL;
	return m_players[playerIndex];
}

INetworkPlayer *CPlatformNetworkManagerSockets::GetPlayerByXuid(PlayerUID xuid)
{
	for( unsigned int i = 0; i < m_players.size(); i++ )
	{
		if( m_players[i]->GetUID() == xuid ) return m_players[i];
	}
	return NULL;
}

INetworkPlayer *CPlatformNetworkManagerSockets::GetPlayerBySmallId(unsigned char smallId)
{
	// Socket::getPlayer() routes through here on every read and write, so keep it
	// a plain scan over a handful of entries rather than anything clever.
	for( unsigned int i = 0; i < m_players.size(); i++ )
	{
		if( m_players[i]->GetSmallId() == smallId ) return m_players[i];
	}
	return NULL;
}

INetworkPlayer *CPlatformNetworkManagerSockets::GetHostPlayer()
{
	return m_pHostPlayer;
}

bool CPlatformNetworkManagerSockets::IsHost()
{
	return m_bIsHost;
}

bool CPlatformNetworkManagerSockets::AddLocalPlayerByUserIndex( int userIndex )
{
	if( m_pLocalPlayer != NULL ) return true;

	m_pLocalPlayer = AddPeer(m_localDisplayName, true, m_bIsHost, userIndex, NULL);
	if( m_bIsHost ) m_pHostPlayer = m_pLocalPlayer;

	g_NetworkManager.PlayerJoining(m_pLocalPlayer);
	g_NetworkManager.CreateSocket(m_pLocalPlayer, true);

	return true;
}

bool CPlatformNetworkManagerSockets::RemoveLocalPlayerByUserIndex( int userIndex )
{
	if( m_pLocalPlayer != NULL && m_pLocalPlayer->GetUserIndex() == userIndex )
	{
		RemovePeer(m_pLocalPlayer);
	}
	return true;
}

bool CPlatformNetworkManagerSockets::RemoveLocalPlayer( INetworkPlayer *pNetworkPlayer )
{
	RemovePeer((NetworkPlayerSockets *)pNetworkPlayer);
	return true;
}

void CPlatformNetworkManagerSockets::FakeLocalPlayerJoined()
{
	AddLocalPlayerByUserIndex( ProfileManager.GetPrimaryPad() );
}

bool CPlatformNetworkManagerSockets::SessionHasSpace(unsigned int spaceRequired)
{
	return ( m_players.size() + spaceRequired ) <= m_publicSlots;
}

bool CPlatformNetworkManagerSockets::ShouldMessageForFullSession()
{
	return true;
}

bool CPlatformNetworkManagerSockets::IsAddingPlayer()
{
	return false;
}

bool CPlatformNetworkManagerSockets::IsInStatsEnabledSession()
{
	// No platform leaderboards behind this transport.
	return false;
}

////////////////////////////////////////////////////////////////////////
// Things this transport has no concept of
////////////////////////////////////////////////////////////////////////

int CPlatformNetworkManagerSockets::GetJoiningReadyPercentage()				{ return 100; }
int CPlatformNetworkManagerSockets::CorrectErrorIDS(int IDS)				{ return IDS; }
void CPlatformNetworkManagerSockets::SendInviteGUI(int quadrant)			{}
void CPlatformNetworkManagerSockets::HandleSignInChange()					{}
void CPlatformNetworkManagerSockets::Notify(int ID, ULONG_PTR Param)		{}
void CPlatformNetworkManagerSockets::SetSessionTexturePackParentId( int id ){ m_hostGameSessionData.texturePackParentId = id; }
void CPlatformNetworkManagerSockets::SetSessionSubTexturePackId( int id )	{ m_hostGameSessionData.subTexturePackId = id; }
void CPlatformNetworkManagerSockets::UpdateAndSetGameSessionData(INetworkPlayer *pNetworkPlayerLeaving)	{}

vector<FriendSessionInfo *> *CPlatformNetworkManagerSockets::GetSessionList(int iPad, int localPlayers, bool partyOnly)
{
	// There is no discovery on this transport - servers are joined by address.
	return new vector<FriendSessionInfo *>();
}

bool CPlatformNetworkManagerSockets::GetGameSessionInfo(int iPad, SessionID sessionId, FriendSessionInfo *foundSession)
{
	return false;
}

void CPlatformNetworkManagerSockets::SetSessionsUpdatedCallback( void (*SessionsUpdatedCallback)(LPVOID pParam), LPVOID pSearchParam )
{
}

void CPlatformNetworkManagerSockets::GetFullFriendSessionInfo( FriendSessionInfo *foundSession, void (* FriendSessionUpdatedFn)(bool success, void *pParam), void *pParam )
{
	FriendSessionUpdatedFn(true, pParam);
}

void CPlatformNetworkManagerSockets::ForceFriendsSessionRefresh()
{
}

void CPlatformNetworkManagerSockets::RegisterPlayerChangedCallback(int iPad, void (*callback)(void *callbackParam, INetworkPlayer *pPlayer, bool leaving), void *callbackParam)
{
	playerChangedCallback[iPad] = callback;
	playerChangedCallbackParam[iPad] = callbackParam;
}

void CPlatformNetworkManagerSockets::UnRegisterPlayerChangedCallback(int iPad, void (*callback)(void *callbackParam, INetworkPlayer *pPlayer, bool leaving), void *callbackParam)
{
	if( playerChangedCallbackParam[iPad] == callbackParam )
	{
		playerChangedCallback[iPad] = NULL;
		playerChangedCallbackParam[iPad] = NULL;
	}
}

wstring CPlatformNetworkManagerSockets::GatherStats()
{
	return L"";
}

wstring CPlatformNetworkManagerSockets::GatherRTTStats()
{
	return L"Rtt: n/a (tcp)";
}

////////////////////////////////////////////////////////////////////////
// Per-system flags (carried over from the stub unchanged)
////////////////////////////////////////////////////////////////////////

CPlatformNetworkManagerSockets::PlayerFlags::PlayerFlags(INetworkPlayer *pNetworkPlayer, unsigned int count)
{
	count = (count + 8 - 1) & ~(8 - 1);
	this->m_pNetworkPlayer = pNetworkPlayer;
	this->flags = new unsigned char [ count / 8 ];
	memset( this->flags, 0, count / 8 );
	this->count = count;
}

CPlatformNetworkManagerSockets::PlayerFlags::~PlayerFlags()
{
	delete [] flags;
}

void CPlatformNetworkManagerSockets::SystemFlagAddPlayer(INetworkPlayer *pNetworkPlayer)
{
	PlayerFlags *newPlayerFlags = new PlayerFlags( pNetworkPlayer, m_flagIndexSize );
	for( unsigned int i = 0; i < m_playerFlags.size(); i++ )
	{
		if( pNetworkPlayer->IsSameSystem(m_playerFlags[i]->m_pNetworkPlayer) )
		{
			memcpy( newPlayerFlags->flags, m_playerFlags[i]->flags, m_playerFlags[i]->count / 8 );
			break;
		}
	}
	m_playerFlags.push_back(newPlayerFlags);
}

void CPlatformNetworkManagerSockets::SystemFlagRemovePlayer(INetworkPlayer *pNetworkPlayer)
{
	for( unsigned int i = 0; i < m_playerFlags.size(); i++ )
	{
		if( m_playerFlags[i]->m_pNetworkPlayer == pNetworkPlayer )
		{
			delete m_playerFlags[i];
			m_playerFlags[i] = m_playerFlags.back();
			m_playerFlags.pop_back();
			return;
		}
	}
}

void CPlatformNetworkManagerSockets::SystemFlagReset()
{
	for( unsigned int i = 0; i < m_playerFlags.size(); i++ )
	{
		delete m_playerFlags[i];
	}
	m_playerFlags.clear();
}

void CPlatformNetworkManagerSockets::SystemFlagSet(INetworkPlayer *pNetworkPlayer, int index)
{
	if( ( index < 0 ) || ( index >= m_flagIndexSize ) ) return;
	if( pNetworkPlayer == NULL ) return;

	for( unsigned int i = 0; i < m_playerFlags.size(); i++ )
	{
		if( pNetworkPlayer->IsSameSystem(m_playerFlags[i]->m_pNetworkPlayer) )
		{
			m_playerFlags[i]->flags[ index / 8 ] |= ( 128 >> ( index % 8 ) );
		}
	}
}

bool CPlatformNetworkManagerSockets::SystemFlagGet(INetworkPlayer *pNetworkPlayer, int index)
{
	if( ( index < 0 ) || ( index >= m_flagIndexSize ) ) return false;
	if( pNetworkPlayer == NULL ) return false;

	for( unsigned int i = 0; i < m_playerFlags.size(); i++ )
	{
		if( m_playerFlags[i]->m_pNetworkPlayer == pNetworkPlayer )
		{
			return ( ( m_playerFlags[i]->flags[ index / 8 ] & ( 128 >> ( index % 8 ) ) ) != 0 );
		}
	}
	return false;
}

#endif // _WINDOWS64
