#include "stdafx.h"

#ifdef _WINDOWS64

#include "Win64DedicatedServer.h"
#include "Win64CommandLine.h"
#include "..\Minecraft.h"
#include "..\MinecraftServer.h"
#include "..\Common\Network\GameNetworkManager.h"
#include "..\Common\Network\Sockets\PlatformNetworkManagerSockets.h"
#include "..\..\Minecraft.World\net.minecraft.world.level.h"
#include <stdio.h>
#include <share.h>
#include <time.h>

namespace Win64DedicatedServer
{
	static bool				s_bStarted			= false;
	static volatile bool	s_bStopRequested	= false;
	static bool				s_bShutdownDone		= false;
	static DWORD			s_dwLastStatusTick	= 0;
	static int				s_iLastPlayerCount	= -1;
	static CRITICAL_SECTION	s_logLock;
	static bool				s_bLogLockReady		= false;
	static FILE				*s_pLogFile			= NULL;

	// Status line cadence. Long enough not to be noise in a log file.
	#define STATUS_INTERVAL_MS 60000

	// Frames to let the game settle before starting the server. See Tick().
	#define DEDICATED_START_DELAY_FRAMES 60

	static int s_iFramesBeforeStart = 0;

	bool IsEnabled()
	{
		return Win64CommandLine::WantsDedicatedServer();
	}

	static BOOL WINAPI CtrlHandler(DWORD dwCtrlType)
	{
		switch (dwCtrlType)
		{
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_CLOSE_EVENT:
		case CTRL_SHUTDOWN_EVENT:
			// Do not save from this thread - it is not the server thread and the
			// handler is on a clock. Just ask, and let Tick() do the work.
			Log("Shutdown requested, saving...");
			s_bStopRequested = true;

			// CTRL_CLOSE gives us only a few seconds before we are killed
			// regardless, so block here and let Tick get on with it.
			if (dwCtrlType == CTRL_CLOSE_EVENT || dwCtrlType == CTRL_SHUTDOWN_EVENT)
			{
				for (int i = 0; i < 200 && !s_bShutdownDone; i++) Sleep(50);
			}
			return TRUE;
		}

		return FALSE;
	}

	void Initialise()
	{
		if (!s_bLogLockReady)
		{
			InitializeCriticalSection(&s_logLock);
			s_bLogLockReady = true;
		}

		// Always write a log file. A dedicated server is usually run detached or
		// over SSH, and this is also the only output that survives the process
		// dying - the console buffer goes with it.
		// _fsopen with _SH_DENYWR, not fopen: the log has to stay readable while
		// the server is running so it can be tailed.
		s_pLogFile = _fsopen("server.log", "w", _SH_DENYWR);

		// If stdout has already been pointed somewhere (a pipe or a file, e.g.
		// "> out.txt"), leave it alone - taking it over would throw away exactly
		// the output the caller asked for.
		DWORD dwStdOutType = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
		bool bStdOutRedirected = (dwStdOutType == FILE_TYPE_DISK || dwStdOutType == FILE_TYPE_PIPE);

		if (!bStdOutRedirected)
		{
			// A GUI subsystem process has no console of its own. Attach to the one
			// we were launched from when there is one, otherwise make a new one.
			if (!::AttachConsole(ATTACH_PARENT_PROCESS))
			{
				::AllocConsole();
			}

			FILE *pDummy = NULL;
			freopen_s(&pDummy, "CONOUT$", "w", stdout);
			freopen_s(&pDummy, "CONOUT$", "w", stderr);
		}

		SetConsoleTitleA("Minecraft LCE - Meow Edition (dedicated server)");
		SetConsoleCtrlHandler(CtrlHandler, TRUE);

		Log("Minecraft LCE Meow Edition - dedicated server");
		Log("Logging to server.log");
	}

	void LogRaw(const char *pszText)
	{
		if (pszText == NULL || !s_bLogLockReady) return;

		EnterCriticalSection(&s_logLock);
		fputs(pszText, stdout);
		fflush(stdout);
		if (s_pLogFile != NULL)
		{
			fputs(pszText, s_pLogFile);
			fflush(s_pLogFile);		// flushed per line so a crash still leaves the trail
		}
		LeaveCriticalSection(&s_logLock);
	}

	void Log(const char *pszFormat, ...)
	{
		if (!s_bLogLockReady) return;

		char szMessage[1024];
		va_list ap;
		va_start(ap, pszFormat);
		vsnprintf(szMessage, sizeof(szMessage), pszFormat, ap);
		va_end(ap);

		time_t now = time(NULL);
		struct tm local;
		localtime_s(&local, &now);

		char szLine[1152];
		sprintf_s(szLine, sizeof(szLine), "[%02d:%02d:%02d] %s\n",
			local.tm_hour, local.tm_min, local.tm_sec, szMessage);

		LogRaw(szLine);
	}

	// server.properties is read by MinecraftServer::initServer (via Settings,
	// which this fork implements - it was a stub). The level-name in it decides
	// which world directory is used, so -world is applied by writing that key.
	static void PrepareProperties()
	{
		const char *pszPath = "server.properties";

		char szLevelName[128];
		size_t converted = 0;
		if (wcstombs_s(&converted, szLevelName, sizeof(szLevelName), Win64CommandLine::GetWorldName(), _TRUNCATE) != 0)
		{
			strcpy_s(szLevelName, sizeof(szLevelName), "world");
		}

		// Read whatever is already there so hand-edited settings survive.
		vector<string> lines;
		bool bFoundLevelName = false;

		FILE *pIn = NULL;
		if (fopen_s(&pIn, pszPath, "r") == 0 && pIn != NULL)
		{
			char szLine[1024];
			while (fgets(szLine, sizeof(szLine), pIn) != NULL)
			{
				string line(szLine);
				while (!line.empty() && (line[line.length()-1] == '\n' || line[line.length()-1] == '\r'))
				{
					line.erase(line.length()-1);
				}

				if (line.compare(0, 11, "level-name=") == 0)
				{
					bFoundLevelName = true;
					if (Win64CommandLine::HasWorldName())
					{
						line = string("level-name=") + szLevelName;
					}
					else
					{
						// Honour the file when -world was not given.
						strcpy_s(szLevelName, sizeof(szLevelName), line.c_str() + 11);
					}
				}

				lines.push_back(line);
			}
			fclose(pIn);
		}

		if (!bFoundLevelName)
		{
			if (lines.empty())
			{
				lines.push_back("#Minecraft server properties");
				lines.push_back("gamemode=0");
				lines.push_back("spawn-animals=true");
				lines.push_back("spawn-npcs=true");
			}
			lines.push_back(string("level-name=") + szLevelName);
		}

		FILE *pOut = NULL;
		if (fopen_s(&pOut, pszPath, "w") != 0 || pOut == NULL)
		{
			Log("WARNING: could not write server.properties - defaults will be used");
			return;
		}
		for (unsigned int i = 0; i < lines.size(); i++) fprintf(pOut, "%s\n", lines[i].c_str());
		fclose(pOut);

		Log("server.properties ready (level-name=%s)", szLevelName);
	}

	bool Start()
	{
		if (s_bStarted) return true;

		PrepareProperties();

		char szWorld[128];
		size_t converted = 0;
		wcstombs_s(&converted, szWorld, sizeof(szWorld), Win64CommandLine::GetWorldName(), _TRUNCATE);

		Log("World      : %s", szWorld);
		Log("Port       : %d", Win64CommandLine::GetServerPort());
		Log("Max players: %d", Win64CommandLine::GetMaxPlayers());
		if (Win64CommandLine::HasSeed())
		{
			Log("Seed       : %lld", Win64CommandLine::GetSeed());
		}

		// Host options. These are what MinecraftServer::initServer reads back out
		// of app, so they have to be set before the server thread starts.
		app.SetGameHostOption(eGameHostOption_GameType,		GameType::SURVIVAL->getId());
		app.SetGameHostOption(eGameHostOption_PvP,			1);
		app.SetGameHostOption(eGameHostOption_TrustPlayers,	1);
		app.SetGameHostOption(eGameHostOption_FireSpreads,	1);
		app.SetGameHostOption(eGameHostOption_TNT,			1);

		NetworkGameInitData *param = new NetworkGameInitData();
		param->seed			= Win64CommandLine::HasSeed() ? Win64CommandLine::GetSeed() : 0;
		param->saveData		= NULL;
		param->levelGen		= NULL;
		param->findSeed		= false;
		param->xzSize		= LEVEL_MAX_WIDTH;
		param->hellScale	= HELL_LEVEL_MAX_SCALE;
		param->settings		= app.GetGameHostOption(eGameHostOption_All);

		// bOnlineGame must be true or CPlatformNetworkManagerSockets::HostGame
		// never calls StartListening and nothing can ever connect.
		int localUsersMask = CGameNetworkManager::GetLocalPlayerMask(ProfileManager.GetPrimaryPad());
		g_NetworkManager.HostGame(localUsersMask, true, false,
			(unsigned char)Win64CommandLine::GetMaxPlayers(), 0);

		// Deliberately NOT FakeLocalPlayerJoined() - a dedicated server has no
		// local player, and calling it would spend a slot on one.

		Log("Starting server...");

		if (!g_NetworkManager.StartDedicatedServer(param))
		{
			Log("ERROR: server failed to start");
			return false;
		}

		s_bStarted = true;
		s_dwLastStatusTick = GetTickCount();

		Log("Server ready, listening on port %d", Win64CommandLine::GetServerPort());
		Log("Press Ctrl+C to save and stop.");

		return true;
	}

	void Tick()
	{
		// 4J Meow - The server is started from the frame loop, not from
		// Minecraft::init(). Starting inside init() crashed in the ServerLevel
		// constructor: the normal path reaches the server from a UI scene long
		// after the game is up and looping, and parts of the client that
		// ServerLevel reaches through (PlayerList view distance, the chunk
		// source) are evidently not ready that early.
		if (!s_bStarted)
		{
			if (s_bStopRequested) return;

			// Minecraft::init() must have finished - MinecraftServer reaches
			// through GetInstance()->progressRenderer while loading the level.
			Minecraft *pMinecraft = Minecraft::GetInstance();
			if (pMinecraft == NULL || pMinecraft->progressRenderer == NULL) return;

			// Then give the game a further moment to settle.
			if (++s_iFramesBeforeStart < DEDICATED_START_DELAY_FRAMES) return;

			if (!Start())
			{
				Log("Startup failed - stopping.");
				pMinecraft->stop();
			}
			return;
		}

		if (s_bStopRequested)
		{
			Shutdown();
			return;
		}

		// Player count changes are the interesting event; the timer is just a
		// heartbeat so a quiet server still shows it is alive.
		int iPlayers = g_NetworkManager.GetPlayerCount();

		// The host identity object is in that count but is not a real player.
		if (iPlayers > 0) iPlayers--;

		DWORD dwNow = GetTickCount();
		bool bDueForStatus = ((dwNow - s_dwLastStatusTick) >= STATUS_INTERVAL_MS);

		if (iPlayers != s_iLastPlayerCount || bDueForStatus)
		{
			Log("Players online: %d/%d", iPlayers, Win64CommandLine::GetMaxPlayers());
			s_iLastPlayerCount = iPlayers;
			s_dwLastStatusTick = dwNow;
		}
	}

	void Shutdown()
	{
		if (s_bShutdownDone) return;
		s_bShutdownDone = true;

		Log("Stopping server and saving world...");

		// HaltServer makes MinecraftServer::run fall out of its loop, which saves
		// the level and the players on the way.
		MinecraftServer::HaltServer();

		if (g_NetworkManager.ServerStoppedValid())
		{
			g_NetworkManager.ServerStoppedWait();
			g_NetworkManager.ServerStoppedDestroy();
		}

		g_NetworkManager.LeaveGame(false);

		Log("World saved. Goodbye.");

		// The game loop has nothing left to do without a server.
		Minecraft *pMinecraft = Minecraft::GetInstance();
		if (pMinecraft != NULL) pMinecraft->stop();
	}
}

#endif // _WINDOWS64
