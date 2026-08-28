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
#include <dbghelp.h>

// 4J Meow - for the crash handler below. dbghelp ships with the Windows SDK.
#pragma comment(lib, "dbghelp.lib")

namespace Win64DedicatedServer
{
	static bool				s_bStarted			= false;
	static volatile bool	s_bStopRequested	= false;
	static bool				s_bShutdownDone		= false;
	static DWORD			s_dwLastStatusTick	= 0;
	static volatile bool	s_bSaveRequested	= false;
	static DWORD			s_dwLastAutosaveTick= 0;
	static int				s_iLastPlayerCount	= -1;
	static CRITICAL_SECTION	s_logLock;
	static bool				s_bLogLockReady		= false;
	static FILE				*s_pLogFile			= NULL;

	// Status line cadence. Long enough not to be noise in a log file.
	#define STATUS_INTERVAL_MS 60000

	// 4J Meow - How often the world is written out while the server runs. Nothing
	// else triggers a save on a dedicated server: the autosave the client gets is
	// driven by the pause menu / XUI, which is not running here, so without this
	// the only save would be the one on shutdown and a crash would cost the lot.
	#define AUTOSAVE_INTERVAL_MS 300000

	// Frames to let the game settle before starting the server. See Tick().
	#define DEDICATED_START_DELAY_FRAMES 60

	static int s_iFramesBeforeStart = 0;

	bool IsEnabled()
	{
		return Win64CommandLine::WantsDedicatedServer();
	}

	void RequestStop()
	{
		s_bStopRequested = true;
	}

	void RequestSave()
	{
		s_bSaveRequested = true;
	}

	// 4J Meow - Queue the save the same way the client's pause menu does.
	//
	// MinecraftServer's tick loop drains app.GetXuiServerAction() and performs the
	// save on the server thread, which is the only thread allowed to walk the
	// levels. Doing it here would race the tick. Note eXuiServerAction_AutoSaveGame
	// falls through to eXuiServerAction_SaveGame on this platform - the separate
	// autosave arm is #if'd to _XBOX_ONE / __ORBIS__ - so both write the world.
	static void QueueServerSave()
	{
		MinecraftServer *pServer = MinecraftServer::getInstance();
		if (pServer == NULL) return;

		app.SetXuiServerAction(ProfileManager.GetPrimaryPad(), eXuiServerAction_AutoSaveGame);
	}

	// 4J Meow - the Java dedicated server's stdin console. 4J left the shape of
	// this in place - MinecraftServer::handleConsoleInput / handleConsoleInputs
	// are still there - but the reader thread that fed it is commented out at
	// the top of initServer, so nothing ever reached it. This is that thread.
	//
	// Note handleConsoleInputs() currently drains the queue and discards it:
	// 4J removed the "commands->handleCommand(input)" dispatch. So anything
	// beyond the locally-handled commands below is accepted and ignored until a
	// dispatcher is wired up. "stop" is handled here rather than queued so that
	// it works even when the server thread is wedged.
	static unsigned long __stdcall ConsoleReaderProc(void *)
	{
		char szLine[512];

		while (!s_bStopRequested)
		{
			if (fgets(szLine, sizeof(szLine), stdin) == NULL)
			{
				// EOF - stdin is closed or was never connected (the server was
				// launched detached). There is nothing further to read, so the
				// thread retires rather than spinning on a dead handle.
				return 0;
			}

			// Trim the newline and any trailing whitespace.
			size_t len = strlen(szLine);
			while (len > 0 && (unsigned char)szLine[len-1] <= ' ') szLine[--len] = 0;

			// Skip a UTF-8 BOM. Whatever is on the other end of stdin decides
			// this, not us - a redirected pipe from PowerShell prepends one to
			// the first line, which turned "stop" into an unknown command.
			char *pszCmd = szLine;
			if ((unsigned char)pszCmd[0] == 0xEF &&
			    (unsigned char)pszCmd[1] == 0xBB &&
			    (unsigned char)pszCmd[2] == 0xBF)
			{
				pszCmd += 3;
			}

			// Skip leading whitespace.
			while (*pszCmd != 0 && (unsigned char)*pszCmd <= ' ') pszCmd++;

			if (*pszCmd == 0) continue;

			if (_stricmp(pszCmd, "stop") == 0 || _stricmp(pszCmd, "end") == 0 || _stricmp(pszCmd, "quit") == 0)
			{
				Log("Console: stop");
				RequestStop();
				return 0;
			}
			else if (_stricmp(pszCmd, "save") == 0 || _stricmp(pszCmd, "save-all") == 0)
			{
				Log("Console: save");
				RequestSave();
			}
			else if (_stricmp(pszCmd, "help") == 0 || _stricmp(pszCmd, "?") == 0)
			{
				Log("Commands: stop, save, players, help");
			}
			else if (_stricmp(pszCmd, "players") == 0 || _stricmp(pszCmd, "list") == 0)
			{
				int iPlayers = g_NetworkManager.GetPlayerCount();
				if (iPlayers > 0) iPlayers--;		// see Tick() - the host identity is not a player
				Log("Players online: %d/%d", iPlayers, Win64CommandLine::GetMaxPlayers());
			}
			else
			{
				// Hand it to the server's own queue. Harmless today, and the
				// right place for it once a dispatcher exists.
				MinecraftServer *pServer = MinecraftServer::getInstance();
				if (pServer != NULL)
				{
					wchar_t wszCmd[512];
					size_t converted = 0;
					mbstowcs_s(&converted, wszCmd, _countof(wszCmd), pszCmd, _TRUNCATE);
					pServer->handleConsoleInput(wstring(wszCmd), NULL);
				}
				Log("Unknown command: %s (try \"help\")", pszCmd);
			}
		}

		return 0;
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

	// 4J Meow - a dedicated server runs unattended, so a silent 0xC0000005 with
	// nothing in the log is useless to whoever has to run it. This resolves the
	// faulting stack against the PDB sitting next to the exe and writes it to
	// server.log before letting the process die.
	//
	// Deliberately minimal and allocation-free where it can be: the process is
	// already in an undefined state by the time this runs.
	static LONG WINAPI CrashHandler(EXCEPTION_POINTERS *pExceptionInfo)
	{
		Log("*** UNHANDLED EXCEPTION 0x%08X at %p ***",
			pExceptionInfo->ExceptionRecord->ExceptionCode,
			pExceptionInfo->ExceptionRecord->ExceptionAddress);

		HANDLE hProcess = GetCurrentProcess();
		SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
		SymInitialize(hProcess, NULL, TRUE);

		CONTEXT ctx = *pExceptionInfo->ContextRecord;

		STACKFRAME64 frame;
		memset(&frame, 0, sizeof(frame));
		frame.AddrPC.Offset		= ctx.Rip;
		frame.AddrPC.Mode		= AddrModeFlat;
		frame.AddrFrame.Offset	= ctx.Rbp;
		frame.AddrFrame.Mode	= AddrModeFlat;
		frame.AddrStack.Offset	= ctx.Rsp;
		frame.AddrStack.Mode	= AddrModeFlat;

		// SYMBOL_INFO wants trailing room for the undecorated name.
		char symbolBuffer[sizeof(SYMBOL_INFO) + 512];
		memset(symbolBuffer, 0, sizeof(symbolBuffer));
		SYMBOL_INFO *pSymbol = (SYMBOL_INFO *)symbolBuffer;
		pSymbol->SizeOfStruct	= sizeof(SYMBOL_INFO);
		pSymbol->MaxNameLen		= 512;

		for (int i = 0; i < 48; i++)
		{
			if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, hProcess, GetCurrentThread(),
					&frame, &ctx, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
			{
				break;
			}

			if (frame.AddrPC.Offset == 0) break;

			DWORD64 displacement = 0;
			if (SymFromAddr(hProcess, frame.AddrPC.Offset, &displacement, pSymbol))
			{
				IMAGEHLP_LINE64 line;
				memset(&line, 0, sizeof(line));
				line.SizeOfStruct = sizeof(line);
				DWORD lineDisplacement = 0;

				if (SymGetLineFromAddr64(hProcess, frame.AddrPC.Offset, &lineDisplacement, &line))
				{
					Log("  [%02d] %s  (%s:%d)", i, pSymbol->Name, line.FileName, line.LineNumber);
				}
				else
				{
					Log("  [%02d] %s + 0x%llX", i, pSymbol->Name, displacement);
				}
			}
			else
			{
				Log("  [%02d] 0x%016llX", i, frame.AddrPC.Offset);
			}
		}

		Log("*** end of stack ***");

		return EXCEPTION_EXECUTE_HANDLER;
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

			// 4J Meow - and stdin, or the console command thread's first fgets
			// hits EOF on an unattached handle and the reader retires before
			// anyone can type "stop".
			freopen_s(&pDummy, "CONIN$", "r", stdin);
		}

		// 4J Meow - tell the shared game layers there is no UI to drive. Set here
		// rather than in Start() because ServerStoppedWait can be reached before
		// the server is fully up.
		CGameNetworkManager::s_bHeadless = true;

		SetConsoleTitleA("Minecraft LCE - Meow Edition (dedicated server)");
		SetConsoleCtrlHandler(CtrlHandler, TRUE);
		SetUnhandledExceptionFilter(CrashHandler);

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
		Log("Type \"stop\" or press Ctrl+C to save and stop. \"help\" for commands.");

		// The stdin console. Detached as a plain thread rather than a C4JThread
		// because it spends its whole life blocked in fgets and must not be
		// waited on at shutdown - there is no way to cancel a blocking read.
		{
			DWORD dwThreadId = 0;
			HANDLE hReader = CreateThread(NULL, 0, ConsoleReaderProc, NULL, 0, &dwThreadId);
			if (hReader != NULL) CloseHandle(hReader);
		}

		return true;
	}

	void Tick()
	{
		// 4J Meow - Tick() is called unconditionally from the frame loop, so it
		// has to gate itself. Without this a plain client launch would start a
		// dedicated server ~60 frames in - i.e. on the 4J logo - and then block
		// the main thread in StartDedicatedServer's ServerReadyWait(), silently,
		// because the logging is only initialised on a real server launch.
		if (!IsEnabled()) return;

		// The server is started from the frame loop, not from
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

		if (s_bSaveRequested)
		{
			s_bSaveRequested = false;
			s_dwLastAutosaveTick = dwNow;
			Log("Saving world...");
			QueueServerSave();
		}
		else if ((dwNow - s_dwLastAutosaveTick) >= AUTOSAVE_INTERVAL_MS)
		{
			s_dwLastAutosaveTick = dwNow;
			Log("Autosaving world...");
			QueueServerSave();
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

		// 4J Meow - ...but stop() only clears Minecraft::running, which the
		// dedicated path never looks at: the Windows x64 loop runs until it sees
		// WM_QUIT, and on a client that arrives from the window being closed.
		// There is no window here, so nothing would ever post it and the process
		// sat idle forever after a successful shutdown. Shutdown() is only ever
		// reached from Tick(), which is on the main thread, so PostQuitMessage
		// is addressed to the right queue.
		PostQuitMessage(0);
	}
}

#endif // _WINDOWS64
