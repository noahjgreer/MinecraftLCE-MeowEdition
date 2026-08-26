#include "stdafx.h"

#ifdef _WINDOWS64

#include "TcpLink.h"
#include "..\..\..\..\Minecraft.World\Socket.h"
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

bool TcpLink::s_winsockUp = false;
int TcpLink::s_linkCount = 0;

// The receive buffer only has to be big enough to keep the socket drained; the
// framing is Connection's problem, not ours.
#define TCPLINK_RECV_BUFFER_SIZE 4096

bool TcpLink::InitialiseWinsock()
{
	if( s_winsockUp ) return true;

	WSADATA wsaData;
	int result = WSAStartup(MAKEWORD(2,2), &wsaData);
	if( result != 0 )
	{
		app.DebugPrintf("TcpLink: WSAStartup failed with %d\n", result);
		return false;
	}
	s_winsockUp = true;
	return true;
}

void TcpLink::ShutdownWinsock()
{
	if( !s_winsockUp ) return;
	if( s_linkCount > 0 )
	{
		app.DebugPrintf("TcpLink: ShutdownWinsock with %d links still open\n", s_linkCount);
		return;
	}
	WSACleanup();
	s_winsockUp = false;
}

TcpLink::TcpLink(SOCKET s)
{
	m_socket = s;
	m_closed = false;
	m_pSocket = NULL;
	m_fromHost = true;
	m_receiveThread = NULL;
	m_peerAddress[0] = '\0';
	InitializeCriticalSection(&m_sendLock);

	// Nagle would coalesce the small movement packets the game sends every tick,
	// which is exactly the latency we cannot afford here.
	BOOL noDelay = TRUE;
	setsockopt(m_socket, IPPROTO_TCP, TCP_NODELAY, (const char *)&noDelay, sizeof(noDelay));

	sockaddr_in peer;
	int peerLen = sizeof(peer);
	if( getpeername(m_socket, (sockaddr *)&peer, &peerLen) == 0 )
	{
		inet_ntop(AF_INET, &peer.sin_addr, m_peerAddress, sizeof(m_peerAddress));
	}

	++s_linkCount;
}

TcpLink::~TcpLink()
{
	Close();

	if( m_receiveThread != NULL )
	{
		delete m_receiveThread;
		m_receiveThread = NULL;
	}

	DeleteCriticalSection(&m_sendLock);
	--s_linkCount;
}

TcpLink *TcpLink::Connect(const char *host, int port)
{
	if( !InitialiseWinsock() ) return NULL;

	char szPort[16];
	sprintf_s(szPort, sizeof(szPort), "%d", port);

	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	addrinfo *pResults = NULL;
	if( getaddrinfo(host, szPort, &hints, &pResults) != 0 || pResults == NULL )
	{
		app.DebugPrintf("TcpLink: could not resolve %s:%d\n", host, port);
		return NULL;
	}

	SOCKET s = INVALID_SOCKET;
	for( addrinfo *pAddr = pResults; pAddr != NULL; pAddr = pAddr->ai_next )
	{
		s = socket(pAddr->ai_family, pAddr->ai_socktype, pAddr->ai_protocol);
		if( s == INVALID_SOCKET ) continue;

		if( connect(s, pAddr->ai_addr, (int)pAddr->ai_addrlen) == 0 ) break;

		closesocket(s);
		s = INVALID_SOCKET;
	}
	freeaddrinfo(pResults);

	if( s == INVALID_SOCKET )
	{
		app.DebugPrintf("TcpLink: could not connect to %s:%d (%d)\n", host, port, WSAGetLastError());
		return NULL;
	}

	app.DebugPrintf("TcpLink: connected to %s:%d\n", host, port);
	return new TcpLink(s);
}

TcpLink *TcpLink::Adopt(SOCKET s)
{
	if( s == INVALID_SOCKET ) return NULL;
	return new TcpLink(s);
}

void TcpLink::StartReceiving(Socket *pSocket, bool fromHost)
{
	if( m_receiveThread != NULL ) return;

	m_pSocket = pSocket;
	m_fromHost = fromHost;

	m_receiveThread = new C4JThread(ReceiveThreadFunc, this, "TcpLinkRecv");
	m_receiveThread->SetProcessor(CPU_CORE_CONNECTIONS);
	m_receiveThread->Run();
}

int TcpLink::ReceiveThreadFunc(void *pParam)
{
	((TcpLink *)pParam)->ReceiveLoop();
	return 0;
}

void TcpLink::ReceiveLoop()
{
	char buffer[TCPLINK_RECV_BUFFER_SIZE];

	while( !m_closed )
	{
		int received = recv(m_socket, buffer, TCPLINK_RECV_BUFFER_SIZE, 0);

		if( received > 0 )
		{
			if( m_pSocket != NULL )
			{
				m_pSocket->pushDataToQueue((const BYTE *)buffer, (DWORD)received, m_fromHost);
			}
			continue;
		}

		if( received == 0 )
		{
			app.DebugPrintf("TcpLink: peer %s closed the connection\n", m_peerAddress);
		}
		else
		{
			app.DebugPrintf("TcpLink: recv from %s failed (%d)\n", m_peerAddress, WSAGetLastError());
		}
		break;
	}

	// Connection's read thread spins in SocketInputStreamNetwork::read() until the
	// stream is closed, so a dead peer has to be reported upwards or it spins forever.
	m_closed = true;
	if( m_pSocket != NULL )
	{
		m_pSocket->close(m_fromHost);
	}
}

bool TcpLink::Send(const void *pvData, int dataSize)
{
	if( m_closed || dataSize <= 0 ) return false;

	bool ok = true;
	const char *pbData = (const char *)pvData;
	int remaining = dataSize;

	EnterCriticalSection(&m_sendLock);
	while( remaining > 0 )
	{
		int sent = send(m_socket, pbData, remaining, 0);
		if( sent == SOCKET_ERROR )
		{
			app.DebugPrintf("TcpLink: send to %s failed (%d)\n", m_peerAddress, WSAGetLastError());
			ok = false;
			break;
		}
		pbData += sent;
		remaining -= sent;
	}
	LeaveCriticalSection(&m_sendLock);

	if( !ok ) m_closed = true;
	return ok;
}

void TcpLink::Close()
{
	if( m_socket == INVALID_SOCKET ) return;

	m_closed = true;

	// Unblocks the receive thread sitting in recv().
	shutdown(m_socket, SD_BOTH);
	closesocket(m_socket);
	m_socket = INVALID_SOCKET;
}

/////////////////////////////////// Listener ////////////////////

TcpListener::TcpListener()
{
	m_socket = INVALID_SOCKET;
	m_port = 0;
}

TcpListener::~TcpListener()
{
	Stop();
}

bool TcpListener::Start(int port)
{
	if( !TcpLink::InitialiseWinsock() ) return false;

	Stop();

	m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if( m_socket == INVALID_SOCKET )
	{
		app.DebugPrintf("TcpListener: socket() failed (%d)\n", WSAGetLastError());
		return false;
	}

	BOOL reuse = TRUE;
	setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

	sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons((u_short)port);

	// ::bind - the codebase pulls in namespace std globally, which shadows the Winsock bind.
	if( ::bind(m_socket, (sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR )
	{
		app.DebugPrintf("TcpListener: could not bind port %d (%d)\n", port, WSAGetLastError());
		Stop();
		return false;
	}

	if( listen(m_socket, SOMAXCONN) == SOCKET_ERROR )
	{
		app.DebugPrintf("TcpListener: listen on port %d failed (%d)\n", port, WSAGetLastError());
		Stop();
		return false;
	}

	// Accept() is polled from the game tick, so it must never block it.
	u_long nonBlocking = 1;
	ioctlsocket(m_socket, FIONBIO, &nonBlocking);

	m_port = port;
	app.DebugPrintf("TcpListener: listening on port %d\n", port);
	return true;
}

TcpLink *TcpListener::Accept()
{
	if( m_socket == INVALID_SOCKET ) return NULL;

	SOCKET accepted = accept(m_socket, NULL, NULL);
	if( accepted == INVALID_SOCKET )
	{
		int error = WSAGetLastError();
		if( error != WSAEWOULDBLOCK )
		{
			app.DebugPrintf("TcpListener: accept failed (%d)\n", error);
		}
		return NULL;
	}

	// The listener is non-blocking; accepted sockets inherit that on Windows, and
	// the receive thread wants a blocking recv().
	u_long blocking = 0;
	ioctlsocket(accepted, FIONBIO, &blocking);

	return TcpLink::Adopt(accepted);
}

void TcpListener::Stop()
{
	if( m_socket == INVALID_SOCKET ) return;
	closesocket(m_socket);
	m_socket = INVALID_SOCKET;
	m_port = 0;
}

#endif // _WINDOWS64
