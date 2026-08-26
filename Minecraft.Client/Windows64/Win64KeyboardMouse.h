#pragma once

// 4J Meow - Keyboard and mouse input for the Windows x64 build.
//
// The Windows64 port never had keyboard or mouse support: the stock WndProc in
// Windows64_Minecraft.cpp handled only WM_COMMAND / WM_PAINT / WM_DESTROY, and
// every input read in the game goes through the prebuilt 4J_Input middleware,
// whose public API is joypad-only.
//
// 4J_Input.lib ships without source, so C_4JInput cannot be extended directly.
// What makes a clean interception possible is that C_4JInput is only a facade:
// its methods operate on a separate global inside the library
// (InternalInputManager, class CInput), not on their own `this` state. So a
// subclass can shadow the handful of methods we care about, merge in keyboard
// and mouse state, and let everything else inherit and behave exactly as before.
//
// 4J_Input.h then does `#define InputManager Win64InputManager`, which points
// all ~70 existing call sites at the subclass without touching any of them.

#if defined(_WINDOWS64)

#include "4JLibs/inc/4J_Input.h"

namespace Win64Input
{
	// Fed from the window procedure. See Windows64_Minecraft.cpp.
	void OnKeyDown(int iVirtualKey);
	void OnKeyUp(int iVirtualKey);
	void OnMouseButton(int iButton, bool bDown);	// 0 left, 1 right, 2 middle
	void OnMouseWheel(int iDelta);
	void OnRawMouseMove(int iDeltaX, int iDeltaY);
	void OnFocusChanged(bool bHasFocus);

	// Registers for raw mouse input and remembers the window. Call once, after
	// the main window exists.
	void Initialise(HWND hWnd);

	// True once the player has actually used the keyboard or mouse. Until then
	// we stay out of the way entirely and the pad behaves as it always did.
	bool InUse();

	// Cursor capture for mouse-look.
	void SetCaptured(bool bCapture);
	bool IsCaptured();
	void ToggleCaptured();
}

// Drop-in replacement for the library's C_4JInput global.
class C_Win64Input : public C_4JInput
{
public:
	// Shadowed (deliberately non-virtual - every call site resolves against the
	// static type of the Win64InputManager object, so shadowing is sufficient
	// and avoids assuming anything about the library's layout).
	void	Tick(void);

	bool	ButtonPressed(int iPad, unsigned char ucAction = 255);
	bool	ButtonReleased(int iPad, unsigned char ucAction);
	bool	ButtonDown(int iPad, unsigned char ucAction = 255);

	float	GetJoypadStick_LX(int iPad, bool bCheckMenuDisplay = true);
	float	GetJoypadStick_LY(int iPad, bool bCheckMenuDisplay = true);
	float	GetJoypadStick_RX(int iPad, bool bCheckMenuDisplay = true);
	float	GetJoypadStick_RY(int iPad, bool bCheckMenuDisplay = true);

	// So the game does not sit on "please connect a controller" when the player
	// only has a keyboard.
	bool	IsPadConnected(int iPad);
};

extern C_Win64Input Win64InputManager;

#endif // _WINDOWS64
