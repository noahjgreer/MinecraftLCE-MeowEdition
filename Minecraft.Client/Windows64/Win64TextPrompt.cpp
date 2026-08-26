#include "stdafx.h"

#ifdef _WINDOWS64

#include "Win64TextPrompt.h"
#include "Win64KeyboardMouse.h"

namespace Win64TextPrompt
{
	#define PROMPT_ID_EDIT		101
	#define PROMPT_ID_OK		102
	#define PROMPT_ID_CANCEL	103

	static bool		s_bDone		= false;
	static bool		s_bAccepted	= false;
	static HWND		s_hEdit		= NULL;

	static LRESULT CALLBACK PromptWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
			case PROMPT_ID_OK:
				s_bAccepted = true;
				s_bDone = true;
				return 0;

			case PROMPT_ID_CANCEL:
				s_bAccepted = false;
				s_bDone = true;
				return 0;
			}
			break;

		case WM_CLOSE:
			s_bAccepted = false;
			s_bDone = true;
			return 0;
		}

		return DefWindowProcW(hWnd, message, wParam, lParam);
	}

	// Enter accepts and Escape cancels. The edit control eats plain WM_KEYDOWN, so
	// this is filtered in the message pump rather than in the window procedure.
	static bool HandleAcceleratorKeys(const MSG &msg)
	{
		if (msg.message != WM_KEYDOWN) return false;

		if (msg.wParam == VK_RETURN)
		{
			s_bAccepted = true;
			s_bDone = true;
			return true;
		}

		if (msg.wParam == VK_ESCAPE)
		{
			s_bAccepted = false;
			s_bDone = true;
			return true;
		}

		return false;
	}

	bool Ask(const wchar_t *pszTitle, const wchar_t *pszPrompt, wchar_t *pszBuffer, int iBufferChars)
	{
		if (pszBuffer == NULL || iBufferChars <= 1) return false;

		HINSTANCE hInstance = GetModuleHandleW(NULL);

		static bool s_bClassRegistered = false;
		if (!s_bClassRegistered)
		{
			WNDCLASSEXW wc;
			ZeroMemory(&wc, sizeof(wc));
			wc.cbSize			= sizeof(wc);
			wc.lpfnWndProc		= PromptWndProc;
			wc.hInstance		= hInstance;
			wc.hCursor			= LoadCursor(NULL, IDC_ARROW);
			wc.hbrBackground	= (HBRUSH)(COLOR_BTNFACE + 1);
			wc.lpszClassName	= L"MinecraftMeowTextPrompt";

			if (RegisterClassExW(&wc) == 0) return false;
			s_bClassRegistered = true;
		}

		const int iWidth	= 380;
		const int iHeight	= 150;

		// Centre on the game window when there is one, otherwise on the desktop.
		int iX = CW_USEDEFAULT;
		int iY = CW_USEDEFAULT;
		HWND hParent = GetActiveWindow();
		RECT rcParent;
		if (hParent != NULL && GetWindowRect(hParent, &rcParent))
		{
			iX = rcParent.left + ((rcParent.right - rcParent.left) - iWidth) / 2;
			iY = rcParent.top + ((rcParent.bottom - rcParent.top) - iHeight) / 2;
		}

		HWND hPrompt = CreateWindowExW(
			WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
			L"MinecraftMeowTextPrompt", pszTitle,
			WS_POPUP | WS_CAPTION | WS_SYSMENU,
			iX, iY, iWidth, iHeight,
			hParent, NULL, hInstance, NULL);

		if (hPrompt == NULL) return false;

		HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

		HWND hLabel = CreateWindowExW(0, L"STATIC", pszPrompt,
			WS_CHILD | WS_VISIBLE,
			14, 14, iWidth - 40, 18, hPrompt, NULL, hInstance, NULL);

		s_hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", pszBuffer,
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
			14, 38, iWidth - 40, 24, hPrompt, (HMENU)PROMPT_ID_EDIT, hInstance, NULL);

		HWND hOk = CreateWindowExW(0, L"BUTTON", L"OK",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
			iWidth - 190, 76, 80, 26, hPrompt, (HMENU)PROMPT_ID_OK, hInstance, NULL);

		HWND hCancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP,
			iWidth - 104, 76, 80, 26, hPrompt, (HMENU)PROMPT_ID_CANCEL, hInstance, NULL);

		SendMessageW(hLabel,	WM_SETFONT, (WPARAM)hFont, TRUE);
		SendMessageW(s_hEdit,	WM_SETFONT, (WPARAM)hFont, TRUE);
		SendMessageW(hOk,		WM_SETFONT, (WPARAM)hFont, TRUE);
		SendMessageW(hCancel,	WM_SETFONT, (WPARAM)hFont, TRUE);

		SendMessageW(s_hEdit, EM_LIMITTEXT, (WPARAM)(iBufferChars - 1), 0);
		SendMessageW(s_hEdit, EM_SETSEL, 0, -1);

		// The game pins and hides the cursor for mouse look; it has to come back
		// or the prompt cannot be used.
		Win64Input::SetCaptured(false);

		EnableWindow(hParent, FALSE);
		ShowWindow(hPrompt, SW_SHOW);
		SetForegroundWindow(hPrompt);
		SetFocus(s_hEdit);

		s_bDone = false;
		s_bAccepted = false;

		// Modal pump. The game loop is not running while this is up, which is fine
		// - this is only ever called from a menu, where nothing is simulating.
		MSG msg;
		while (!s_bDone && GetMessageW(&msg, NULL, 0, 0) > 0)
		{
			if (HandleAcceleratorKeys(msg)) continue;

			if (!IsDialogMessageW(hPrompt, &msg))
			{
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}
		}

		if (s_bAccepted)
		{
			GetWindowTextW(s_hEdit, pszBuffer, iBufferChars);
		}

		EnableWindow(hParent, TRUE);
		DestroyWindow(hPrompt);
		s_hEdit = NULL;

		SetForegroundWindow(hParent);

		// Whatever was typed into the prompt is not meant for the game.
		Win64Input::ClearTypedChars();

		return s_bAccepted;
	}
}

#endif // _WINDOWS64
