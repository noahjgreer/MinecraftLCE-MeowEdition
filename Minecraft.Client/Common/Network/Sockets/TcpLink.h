#pragma once

// 4J-style plain-sockets transport for the Windows x64 build.
//
// This supplies the two ends that Minecraft.World's Socket class already asks for
// but which nothing implements on this platform:
//
//   inbound  - a receive thread calls Socket::pushDataToQueue()
//   outbound - NetworkPlayerSockets::SendData() calls TcpLink::Send()
//
// Nothing above Connection needs to know this exists. See
// docs/systems/dedicated-server-and-direct-connect.md.

#ifdef _WINDOWS64

#include <winsock2.h>
#include "..\..\..\..\Minecraft.World\C4JThread.h"

class Socket;

class TcpLink
{
public:
	// Blocking connect. Returns NULL on failure. host may be a dotted quad or a name.
	static TcpLink *Connect(const char *host, int port);

	// Takes ownership of an already-accepted OS socket.
	static TcpLink *Adopt(SOCKET s);

	~TcpLink();

	// Begin pumping received bytes into pSocket. fromHost selects which of the
	// Socket's two queues the bytes land in, matching Socket::pushDataToQueue.
	void StartReceiving(Socket *pSocket, bool fromHost);

	// Returns false if the link is closed or the send failed.
	bool Send(const void *pvData, int dataSize);

	void Close();
	bool IsClosed() { return m_closed; }

	const char *GetPeerAddress() { return m_peerAddress; }

	static bool InitialiseWinsock();
	static void ShutdownWinsock();

private:
	TcpLink(SOCKET s);

	static int ReceiveThreadFunc(void *pParam);
	void ReceiveLoop();

	SOCKET				m_socket;
	volatile bool		m_closed;
	Socket				*m_pSocket;
	bool				m_fromHost;
	C4JThread			*m_receiveThread;
	CRITICAL_SECTION	m_sendLock;
	char				m_peerAddress[64];

	static bool			s_winsockUp;
	static int			s_linkCount;
};

// Non-blocking listener. Accept() returns NULL when nobody is waiting, so it is
// safe to poll from CPlatformNetworkManager::DoWork() on the game thread.
class TcpListener
{
public:
	TcpListener();
	~TcpListener();

	bool Start(int port);
	TcpLink *Accept();
	void Stop();

	bool IsListening() { return m_socket != INVALID_SOCKET; }
	int GetPort() { return m_port; }

private:
	SOCKET	m_socket;
	int		m_port;
};

#endif // _WINDOWS64
