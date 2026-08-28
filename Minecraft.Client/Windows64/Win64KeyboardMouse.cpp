#include "stdafx.h"

#if defined(_WINDOWS64)

#include "Win64KeyboardMouse.h"
#include "../Common/App_enums.h"
#include "../Common/UI/UITextEdit.h"

C_Win64Input Win64InputManager;

// Compile-time proof that the `#define InputManager Win64InputManager` redirect
// in 4J_Input.h is actually in effect for translation units that pull in
// stdafx.h - which is how all ~70 existing call sites reach the input manager.
// If someone reorders the includes in stdafx.h, or the macro stops being
// reached, this fails the build here rather than silently going back to a
// pad-only game that ignores the keyboard.
#include <type_traits>
static_assert(std::is_same<decltype(InputManager), C_Win64Input>::value,
	"InputManager is not being redirected to C_Win64Input - keyboard and mouse input would be silently dead.");

namespace
{
	// ---------------------------------------------------------------------
	// Tunables. Grouped here because the sign conventions below are the parts
	// most likely to need flipping once someone actually plays the game - see
	// docs/systems/windows-keyboard-mouse-input.md.
	// ---------------------------------------------------------------------

	// Mouse look no longer passes through a stick axis - see
	// Minecraft.cpp / Win64ApplyMouseLook and
	// docs/systems/windows-mouse-look.md. Raw pixel deltas are drained once a
	// frame by ConsumeLookDelta and turned into a rotation directly, the way
	// Java Edition's MouseHandler does it. This multiplier is a straight scale
	// on top of the in-game sensitivity setting; 1.0 matches Java.
	const float MOUSE_SENSITIVITY		= 1.0f;

	// Flip either of these if look or movement comes out inverted.
	const float LOOK_X_SIGN				=  1.0f;
	const float LOOK_Y_SIGN				=  1.0f;
	const float MOVE_X_SIGN				=  1.0f;
	const float MOVE_Y_SIGN				=  1.0f;

	// How long a mouse wheel notch reads as a held "scroll" action, in ticks.
	const int	WHEEL_HOLD_TICKS		= 2;

	// Auto-repeat for GetValue(..., bRepeat = true), in ticks: how long a key
	// must be held before it starts repeating, and how often it repeats after
	// that. Mirrors what C_4JInput::SetKeyRepeatRate does for the pad.
	const int	REPEAT_DELAY_TICKS		= 14;
	const int	REPEAT_RATE_TICKS		= 4;

	// What GetValue reports for an active action. Pad triggers report 0..255,
	// and every caller only tests > 0, so a full-scale value is the safe answer.
	const unsigned int ACTION_VALUE_ON	= 255;

	const int	VKEY_COUNT				= 256;
	const int	MOUSE_BUTTON_COUNT		= 3;

	// Virtual mouse buttons appended after the 256 virtual key codes so both
	// live in one keyed state array.
	const int	VK_MOUSE_LEFT			= VKEY_COUNT + 0;
	const int	VK_MOUSE_RIGHT			= VKEY_COUNT + 1;
	const int	VK_MOUSE_MIDDLE			= VKEY_COUNT + 2;
	const int	KEY_STATE_COUNT			= VKEY_COUNT + MOUSE_BUTTON_COUNT;

	// Manual capture toggle, so the player is never trapped if something goes
	// wrong with focus handling.
	const int	VK_CAPTURE_TOGGLE		= VK_F12;

	bool	s_keyDown[KEY_STATE_COUNT];		// live state, written by WndProc
	bool	s_keyThisTick[KEY_STATE_COUNT];	// snapshot for the current tick
	bool	s_keyLastTick[KEY_STATE_COUNT];	// snapshot for the previous tick
	int		s_keyHeldTicks[KEY_STATE_COUNT];// consecutive ticks held, for repeat

	// Raw mouse pixels since the last frame drained them. Deliberately NOT
	// per-tick: the turn is applied once per rendered frame so that mouse look
	// is as smooth as the framerate, rather than being quantised to 20Hz.
	float	s_mouseAccumX	= 0.0f;
	float	s_mouseAccumY	= 0.0f;

	// Mirrors what the game tells the library via SetMenuDisplayed, so mouse
	// look can be suppressed in exactly the cases where the right stick would
	// have been zeroed by bCheckMenuDisplay.
	bool	s_menuDisplayed[XUSER_MAX_COUNT] = { false };

	int		s_wheelUpTicks		= 0;
	int		s_wheelDownTicks	= 0;

	// Impulses waiting for the *game tick* to collect them.
	//
	// Tick() runs once per rendered frame, but the gameplay input code runs
	// once per game tick (20Hz), so at any decent framerate most frames are not
	// followed by a tick. Anything that lives only in the per-frame snapshot -
	// a key edge, or a wheel notch held for WHEEL_HOLD_TICKS frames - is
	// therefore usually latched and dropped before the game ever looks at it.
	// These two pend until the consumer takes them, exactly as 4J's own
	// Player::ullButtonsPressed does for pad button presses.
	int		s_wheelPending		= 0;	// notches, signed: + is up/away
	int		s_pendingHotbarSlot	= -1;	// 0-based, -1 for none

	bool	s_inUse			= false;		// has the player touched kb/mouse?
	bool	s_hasFocus		= true;
	bool	s_captured		= false;
	HWND	s_hWnd			= NULL;

	// Pointer state. s_captured stays the player's *intent* to be in mouse-look
	// (F12, or having touched the keyboard); s_mode is what that intent plus the
	// current menu state actually resolves to.
	Win64Input::EPointerMode s_mode = Win64Input::ePointerMode_Look;

	float	s_pointerX		= 0.0f;			// client pixels
	float	s_pointerY		= 0.0f;
	bool	s_pointerValid	= false;
	bool	s_pointerMoved	= false;

	void MarkInUse()
	{
		s_inUse = true;
	}

	bool KeyHeld(int vk)
	{
		return (vk >= 0 && vk < KEY_STATE_COUNT) ? s_keyThisTick[vk] : false;
	}

	bool KeyWentDown(int vk)
	{
		return (vk >= 0 && vk < KEY_STATE_COUNT) ? (s_keyThisTick[vk] && !s_keyLastTick[vk]) : false;
	}

	bool KeyWentUp(int vk)
	{
		return (vk >= 0 && vk < KEY_STATE_COUNT) ? (!s_keyThisTick[vk] && s_keyLastTick[vk]) : false;
	}

	// True on the tick the key goes down, and then again on each repeat tick
	// once it has been held past the delay.
	// Text entry ownership of the keyboard. s_textInput is the live flag;
	// s_textInputTick keeps it effective for the rest of the tick it is cleared
	// on, so the Return that commits a field is not then read as a button press
	// by the same tick's input pass.
	bool s_textInput		= false;
	bool s_textInputTick	= false;

	bool TextInputOwnsKeyboard()
	{
		return s_textInput || s_textInputTick;
	}

	bool KeyRepeated(int vk)
	{
		if (vk < 0 || vk >= KEY_STATE_COUNT) return false;

		const int iHeld = s_keyHeldTicks[vk];
		if (iHeld <= 0)					return false;
		if (iHeld == 1)					return true;	// initial press
		if (iHeld <= REPEAT_DELAY_TICKS)	return false;

		return ((iHeld - REPEAT_DELAY_TICKS) % REPEAT_RATE_TICKS) == 0;
	}

	// ---------------------------------------------------------------------
	// Action -> key bindings.
	//
	// Deliberately laid out as one table so the whole control scheme is
	// legible in one place and rebinding is a data change. Up to
	// MAX_KEYS_PER_ACTION keys may map to an action; any of them triggers it.
	// ---------------------------------------------------------------------
	const int MAX_KEYS_PER_ACTION = 3;

	struct SActionBinding
	{
		int iAction;
		int iKeys[MAX_KEYS_PER_ACTION];
	};

	const SActionBinding s_bindings[] =
	{
		// --- Menu navigation ---------------------------------------------
		{ ACTION_MENU_A,				{ VK_RETURN,		VK_SPACE,	0 } },
		{ ACTION_MENU_OK,				{ VK_RETURN,		VK_SPACE,	0 } },
		{ ACTION_MENU_B,				{ VK_ESCAPE,		VK_BACK,	0 } },
		{ ACTION_MENU_CANCEL,			{ VK_ESCAPE,		VK_BACK,	0 } },
		{ ACTION_MENU_X,				{ 'X',				0,			0 } },
		{ ACTION_MENU_Y,				{ 'Y',				0,			0 } },
		{ ACTION_MENU_UP,				{ VK_UP,			'W',		0 } },
		{ ACTION_MENU_DOWN,				{ VK_DOWN,			'S',		0 } },
		{ ACTION_MENU_LEFT,				{ VK_LEFT,			'A',		0 } },
		{ ACTION_MENU_RIGHT,			{ VK_RIGHT,			'D',		0 } },
		{ ACTION_MENU_PAGEUP,			{ VK_PRIOR,			0,			0 } },
		{ ACTION_MENU_PAGEDOWN,			{ VK_NEXT,			0,			0 } },
		{ ACTION_MENU_STICK_PRESS,		{ VK_LCONTROL,		0,			0 } },
		{ ACTION_MENU_PAUSEMENU,		{ VK_ESCAPE,		0,			0 } },

		// --- Gameplay -----------------------------------------------------
		{ MINECRAFT_ACTION_JUMP,		{ VK_SPACE,			0,			0 } },
		{ MINECRAFT_ACTION_FORWARD,		{ 'W',				VK_UP,		0 } },
		{ MINECRAFT_ACTION_BACKWARD,	{ 'S',				VK_DOWN,	0 } },
		{ MINECRAFT_ACTION_LEFT,		{ 'A',				VK_LEFT,	0 } },
		{ MINECRAFT_ACTION_RIGHT,		{ 'D',				VK_RIGHT,	0 } },
		{ MINECRAFT_ACTION_ACTION,		{ VK_MOUSE_LEFT,	0,			0 } },	// attack / mine
		{ MINECRAFT_ACTION_USE,			{ VK_MOUSE_RIGHT,	0,			0 } },	// place / use
		{ MINECRAFT_ACTION_INVENTORY,	{ 'E',				VK_TAB,		0 } },
		{ MINECRAFT_ACTION_CRAFTING,	{ 'C',				0,			0 } },
		{ MINECRAFT_ACTION_DROP,		{ 'Q',				0,			0 } },
		{ MINECRAFT_ACTION_SNEAK_TOGGLE,{ VK_LSHIFT,		VK_RSHIFT,	0 } },
		{ MINECRAFT_ACTION_PAUSEMENU,	{ VK_ESCAPE,		0,			0 } },
		{ MINECRAFT_ACTION_GAME_INFO,	{ VK_F3,			0,			0 } },
		{ MINECRAFT_ACTION_RENDER_THIRD_PERSON,	{ VK_F5,	0,			0 } },
		{ MINECRAFT_ACTION_DPAD_UP,		{ VK_UP,			0,			0 } },
		{ MINECRAFT_ACTION_DPAD_DOWN,	{ VK_DOWN,			0,			0 } },
		{ MINECRAFT_ACTION_DPAD_LEFT,	{ VK_LEFT,			0,			0 } },
		{ MINECRAFT_ACTION_DPAD_RIGHT,	{ VK_RIGHT,			0,			0 } },
	};

	const int BINDING_COUNT = sizeof(s_bindings) / sizeof(s_bindings[0]);

	// Hotbar cycling is driven by the mouse wheel as well as by keys, so these
	// four actions get their own handling on top of the table above.
	bool ActionHasWheel(int iAction, int &iWheelTicks)
	{
		if (iAction == MINECRAFT_ACTION_LEFT_SCROLL || iAction == ACTION_MENU_LEFT_SCROLL)
		{
			iWheelTicks = s_wheelDownTicks;
			return true;
		}
		if (iAction == MINECRAFT_ACTION_RIGHT_SCROLL || iAction == ACTION_MENU_RIGHT_SCROLL)
		{
			iWheelTicks = s_wheelUpTicks;
			return true;
		}
		return false;
	}

	// Mouse buttons as menu actions.
	//
	// Kept out of s_bindings because they are conditional in a way a static
	// table cannot express: they only apply while a menu is actually up (so a
	// click in gameplay is a swing, not a menu confirm), and what the right
	// button means depends on which kind of menu it is.
	//
	// In a container menu the mapping is Java Edition's: left click takes or
	// places the whole stack (LCE's A), right click splits or places one (LCE's
	// X). Everywhere else the right button is "back", which is what a mouse
	// user expects and what LCE's B does.
	//
	// What the UI last told us was on screen, set by UpdatePointerMode. Cached
	// so that the pointer mode can also be recomputed from a capture or focus
	// change, not only on the tick UpdatePointerMode runs on.
	bool	s_menuUp		= false;
	bool	s_containerUp	= false;

	int MouseKeyForMenuAction(int iAction)
	{
		if (!s_menuUp) return 0;

		switch (iAction)
		{
		case ACTION_MENU_A:
		case ACTION_MENU_OK:
			return VK_MOUSE_LEFT;

		case ACTION_MENU_X:
			return s_containerUp ? VK_MOUSE_RIGHT : 0;

		case ACTION_MENU_Y:
			return s_containerUp ? VK_MOUSE_MIDDLE : 0;

		case ACTION_MENU_B:
		case ACTION_MENU_CANCEL:
			return s_containerUp ? 0 : VK_MOUSE_RIGHT;
		}
		return 0;
	}

	const SActionBinding *FindBinding(int iAction)
	{
		for (int i = 0; i < BINDING_COUNT; i++)
		{
			if (s_bindings[i].iAction == iAction) return &s_bindings[i];
		}
		return NULL;
	}

	// Client rectangle in screen coordinates, which is what ClipCursor and
	// SetCursorPos both want.
	bool GetClientRectOnScreen(RECT &rc)
	{
		if (s_hWnd == NULL) return false;
		if (!GetClientRect(s_hWnd, &rc)) return false;

		POINT tl; tl.x = rc.left;  tl.y = rc.top;
		POINT br; br.x = rc.right; br.y = rc.bottom;
		if (!ClientToScreen(s_hWnd, &tl)) return false;
		if (!ClientToScreen(s_hWnd, &br)) return false;

		rc.left = tl.x; rc.top = tl.y; rc.right = br.x; rc.bottom = br.y;
		return (rc.right > rc.left) && (rc.bottom > rc.top);
	}

	// Park the cursor on the centre of the window.
	//
	// ClipCursor alone is not enough to keep it there. Windows drops the clip
	// whenever another window activates, a display or DPI change happens, or a
	// system dialog appears, and it is never restored on its own - after which
	// the cursor is free to wander onto another monitor while the player is
	// still looking around with raw input, and the next click lands in whatever
	// window it happens to be over. Re-centring every tick makes the position
	// self-correcting instead of depending on the clip surviving.
	//
	// Look is driven entirely by WM_INPUT deltas, which report physical device
	// movement, so warping the cursor contributes no motion of its own and
	// there is nothing to filter back out.
	void PinCursorToCentre()
	{
		RECT rc;
		if (!GetClientRectOnScreen(rc)) return;

		const int iCentreX = rc.left + (rc.right  - rc.left) / 2;
		const int iCentreY = rc.top  + (rc.bottom - rc.top ) / 2;

		POINT pt;
		if (GetCursorPos(&pt) && pt.x == iCentreX && pt.y == iCentreY) return;

		SetCursorPos(iCentreX, iCentreY);
	}

	// Seed the tracked pointer position from wherever the OS cursor is right
	// now. Called when entering a pointer mode, so the first frame of a menu
	// already has a sensible position instead of waiting for the player to
	// nudge the mouse before anything can be hovered.
	void SeedPointerFromCursor()
	{
		if (s_hWnd == NULL) return;

		POINT pt;
		if (!GetCursorPos(&pt)) return;
		if (!ScreenToClient(s_hWnd, &pt)) return;

		s_pointerX = (float)pt.x;
		s_pointerY = (float)pt.y;
		s_pointerValid = true;
	}

	void ApplyPointerMode()
	{
		if (s_hWnd == NULL) return;

		if (!s_hasFocus)
		{
			ClipCursor(NULL);
			while (ShowCursor(TRUE) < 0) {}
			return;
		}

		switch (s_mode)
		{
		case Win64Input::ePointerMode_Look:
			{
				RECT rc;
				if (GetClientRectOnScreen(rc)) ClipCursor(&rc);
				PinCursorToCentre();
				while (ShowCursor(FALSE) >= 0) {}
			}
			break;

		case Win64Input::ePointerMode_HiddenCursor:
			{
				// Free to move, but still clipped: an invisible cursor that can
				// wander onto another monitor is a cursor whose next click
				// lands in someone else's window.
				RECT rc;
				if (GetClientRectOnScreen(rc)) ClipCursor(&rc);
				while (ShowCursor(FALSE) >= 0) {}
			}
			break;

		case Win64Input::ePointerMode_MenuCursor:
			ClipCursor(NULL);
			while (ShowCursor(TRUE) < 0) {}
			break;
		}
	}

	// The whole pointer policy, in one place.
	//
	// Not captured at all (the player pressed F12, or has not touched the
	// keyboard yet) means the cursor belongs to the desktop and we leave it
	// alone. Otherwise a container menu draws its own pointer, any other menu
	// wants a real arrow, and gameplay wants mouse-look.
	void RecomputeMode()
	{
		Win64Input::EPointerMode eNew;

		if (!s_captured)			eNew = Win64Input::ePointerMode_MenuCursor;
		else if (s_containerUp)		eNew = Win64Input::ePointerMode_HiddenCursor;
		else if (s_menuUp)			eNew = Win64Input::ePointerMode_MenuCursor;
		else						eNew = Win64Input::ePointerMode_Look;

		if (eNew == s_mode)
		{
			ApplyPointerMode();
			return;
		}

		const bool bWasLook = (s_mode == Win64Input::ePointerMode_Look);
		s_mode = eNew;
		ApplyPointerMode();

		// Coming out of look, the cursor was parked on the window centre, so
		// that is where the pointer starts. Take the position from the OS
		// rather than assuming, since ClipCursor may have moved it.
		if (bWasLook && eNew != Win64Input::ePointerMode_Look)
		{
			SeedPointerFromCursor();
			s_pointerMoved = true;
		}

		// Going into look, any stale look delta accumulated while the pointer
		// was free would be applied as one large jerk on the first frame.
		if (eNew == Win64Input::ePointerMode_Look)
		{
			s_mouseAccumX = s_mouseAccumY = 0.0f;
		}
	}

	// Kept as the old name for the call sites that only care about focus and
	// capture changes; the mode has to be re-derived from those too.
	void ApplyCapture()
	{
		RecomputeMode();
	}
}

// ---------------------------------------------------------------------------
// Typed-text queue (see Win64Input::OnChar)
// ---------------------------------------------------------------------------

#define TYPED_CHAR_QUEUE_SIZE 64

static wchar_t			s_typedChars[TYPED_CHAR_QUEUE_SIZE];
static int				s_typedRead		= 0;
static int				s_typedWrite	= 0;
static CRITICAL_SECTION	s_typedLock;


// ---------------------------------------------------------------------------
// Window-procedure entry points
// ---------------------------------------------------------------------------

namespace Win64Input
{
	void Initialise(HWND hWnd)
	{
		s_hWnd = hWnd;

		InitializeCriticalSection(&s_typedLock);
		s_typedRead = s_typedWrite = 0;

		for (int i = 0; i < KEY_STATE_COUNT; i++)
		{
			s_keyDown[i] = s_keyThisTick[i] = s_keyLastTick[i] = false;
			s_keyHeldTicks[i] = 0;
		}

		// Raw input gives true relative motion, so we never have to warp the
		// cursor to the window centre and then filter our own warp back out.
		RAWINPUTDEVICE rid;
		rid.usUsagePage	= 0x01;		// generic desktop
		rid.usUsage		= 0x02;		// mouse
		rid.dwFlags		= 0;
		rid.hwndTarget	= hWnd;
		RegisterRawInputDevices(&rid, 1, sizeof(rid));
	}

	void OnKeyDown(int iVirtualKey)
	{
		if (iVirtualKey < 0 || iVirtualKey >= VKEY_COUNT) return;

		if (iVirtualKey == VK_CAPTURE_TOGGLE)
		{
			ToggleCaptured();
			return;
		}

		// Number row / numpad select a hotbar slot. Recorded here rather than
		// read as an edge off the per-tick snapshot so the press survives until
		// the next game tick - see s_pendingHotbarSlot. The s_keyDown test
		// discards the OS auto-repeat, which would otherwise stream presses
		// while the key is held.
		if (!s_keyDown[iVirtualKey])
		{
			if (iVirtualKey >= '1' && iVirtualKey <= '9')
				s_pendingHotbarSlot = iVirtualKey - '1';
			else if (iVirtualKey >= VK_NUMPAD1 && iVirtualKey <= VK_NUMPAD9)
				s_pendingHotbarSlot = iVirtualKey - VK_NUMPAD1;
		}

		s_keyDown[iVirtualKey] = true;
		MarkInUse();

		// Typing into the window is taken as intent to play with the keyboard.
		if (!s_captured && s_hasFocus) SetCaptured(true);
	}

	void OnKeyUp(int iVirtualKey)
	{
		if (iVirtualKey < 0 || iVirtualKey >= VKEY_COUNT) return;
		s_keyDown[iVirtualKey] = false;
	}

	// A tiny ring of typed characters. WM_CHAR arrives on the thread pumping the
	// message loop and is drained on the game thread, so both ends take the lock.
	void OnChar(wchar_t ch)
	{
		EnterCriticalSection(&s_typedLock);
		int iNext = (s_typedWrite + 1) % TYPED_CHAR_QUEUE_SIZE;
		if (iNext != s_typedRead)
		{
			s_typedChars[s_typedWrite] = ch;
			s_typedWrite = iNext;
		}
		LeaveCriticalSection(&s_typedLock);

		MarkInUse();
	}

	bool ConsumeTypedChar(wchar_t &ch)
	{
		bool bGot = false;

		EnterCriticalSection(&s_typedLock);
		if (s_typedRead != s_typedWrite)
		{
			ch = s_typedChars[s_typedRead];
			s_typedRead = (s_typedRead + 1) % TYPED_CHAR_QUEUE_SIZE;
			bGot = true;
		}
		LeaveCriticalSection(&s_typedLock);

		return bGot;
	}

	void ClearTypedChars()
	{
		EnterCriticalSection(&s_typedLock);
		s_typedRead = s_typedWrite = 0;
		LeaveCriticalSection(&s_typedLock);
	}

	void SetTextInputActive(bool bActive)
	{
		s_textInput = bActive;

		// Setting takes effect immediately; clearing waits for the next Tick.
		if (bActive) s_textInputTick = true;
	}

	bool IsTextInputActive()
	{
		return s_textInput;
	}

	bool EditKeyRepeated(int iVirtualKey)
	{
		return KeyRepeated(iVirtualKey);
	}

	void OnMouseButton(int iButton, bool bDown)
	{
		if (iButton < 0 || iButton >= MOUSE_BUTTON_COUNT) return;
		s_keyDown[VKEY_COUNT + iButton] = bDown;
		if (bDown)
		{
			MarkInUse();
			if (!s_captured && s_hasFocus) SetCaptured(true);
		}
	}

	void OnMouseWheel(int iDelta)
	{
		// The pending count is clamped so that spinning the wheel while the
		// hotbar is not accepting input cannot queue up a long backlog that
		// then unwinds visibly once it is.
		const int WHEEL_PENDING_MAX = 9;
		if (iDelta > 0)			{ s_wheelUpTicks   = WHEEL_HOLD_TICKS; if (s_wheelPending <  WHEEL_PENDING_MAX) s_wheelPending++; }
		else if (iDelta < 0)	{ s_wheelDownTicks = WHEEL_HOLD_TICKS; if (s_wheelPending > -WHEEL_PENDING_MAX) s_wheelPending--; }
		if (iDelta != 0) MarkInUse();
	}

	void OnRawMouseMove(int iDeltaX, int iDeltaY)
	{
		// Raw motion is only ever look. In a pointer mode the position comes
		// from WM_MOUSEMOVE instead, so that the OS's own acceleration and
		// screen clamping apply exactly once.
		if (!s_captured || s_mode != ePointerMode_Look) return;
		s_mouseAccumX += (float)iDeltaX;
		s_mouseAccumY += (float)iDeltaY;
		if (iDeltaX != 0 || iDeltaY != 0) MarkInUse();
	}

	void OnFocusChanged(bool bHasFocus)
	{
		s_hasFocus = bHasFocus;

		if (!bHasFocus)
		{
			// Drop every key so nothing sticks down while we are in the
			// background - a held W would otherwise walk forever.
			for (int i = 0; i < KEY_STATE_COUNT; i++) { s_keyDown[i] = false; s_keyHeldTicks[i] = 0; }
			s_mouseAccumX = s_mouseAccumY = 0.0f;
			SetCaptured(false);
		}
		ApplyCapture();
	}

	void OnWindowMoved()
	{
		// The clip rectangle is in screen space, so a move or resize leaves it
		// pointing at where the window used to be.
		ApplyCapture();
	}

	bool ConsumeLookDelta(float &fDX, float &fDY)
	{
		fDX = s_mouseAccumX * MOUSE_SENSITIVITY * LOOK_X_SIGN;
		fDY = s_mouseAccumY * MOUSE_SENSITIVITY * LOOK_Y_SIGN;
		s_mouseAccumX = s_mouseAccumY = 0.0f;
		return (fDX != 0.0f || fDY != 0.0f);
	}

	bool IsMenuDisplayed(int iPad)
	{
		if (iPad < 0 || iPad >= XUSER_MAX_COUNT) return false;
		return s_menuDisplayed[iPad];
	}

	void OnMouseMove(int iClientX, int iClientY)
	{
		if (s_mode == ePointerMode_Look) return;

		const float fX = (float)iClientX;
		const float fY = (float)iClientY;

		if (!s_pointerValid || fX != s_pointerX || fY != s_pointerY)
		{
			s_pointerX = fX;
			s_pointerY = fY;
			s_pointerValid = true;
			s_pointerMoved = true;
			MarkInUse();
		}
	}

	bool GetPointerPos(float &fX, float &fY)
	{
		fX = s_pointerX;
		fY = s_pointerY;
		return s_pointerValid;
	}

	bool GetClientSize(int &iWidth, int &iHeight)
	{
		RECT rc;
		if (s_hWnd == NULL || !GetClientRect(s_hWnd, &rc)) return false;
		iWidth  = rc.right  - rc.left;
		iHeight = rc.bottom - rc.top;
		return (iWidth > 0 && iHeight > 0);
	}

	bool ConsumePointerMoved()
	{
		const bool bMoved = s_pointerMoved;
		s_pointerMoved = false;
		return bMoved;
	}

	// The mouse buttons are otherwise only ever read through the action
	// mapping, which is right for anything a pad can also do. A slider drag is
	// not one of those things - it is a press, a position and a release, and the
	// position only means anything to the control that was under the cursor when
	// the press happened. So the UI reads the button directly.
	// See UIController::TickMousePointer.
	bool IsMouseButtonDown(int iButton)
	{
		if (iButton < 0 || iButton >= MOUSE_BUTTON_COUNT) return false;
		return KeyHeld(VKEY_COUNT + iButton);
	}

	bool MouseButtonWentDown(int iButton)
	{
		if (iButton < 0 || iButton >= MOUSE_BUTTON_COUNT) return false;
		return KeyWentDown(VKEY_COUNT + iButton);
	}

	void UpdatePointerMode(bool bMenuDisplayed, bool bSceneDrawsPointer)
	{
		s_menuUp		= bMenuDisplayed;
		s_containerUp	= bMenuDisplayed && bSceneDrawsPointer;
		RecomputeMode();
	}

	bool ConsumeHotbarSlot(int &iSlot)
	{
		if (s_pendingHotbarSlot < 0) return false;

		iSlot = s_pendingHotbarSlot;
		s_pendingHotbarSlot = -1;
		return true;
	}

	int ConsumeWheelNotch()
	{
		// One notch per call, not the whole accumulator: Inventory::swapPaint
		// clamps its argument to a single step, so handing it a three-notch
		// flick would move one slot and silently drop the other two. The caller
		// runs at 20Hz, so a flick still resolves in well under a fifth of a
		// second.
		if (s_wheelPending > 0)	{ s_wheelPending--; return  1; }
		if (s_wheelPending < 0)	{ s_wheelPending++; return -1; }
		return 0;
	}

	void ClearWheelNotches()	{ s_wheelPending = 0; }

	EPointerMode GetPointerMode()	{ return s_mode; }
	bool IsPointerActive()			{ return s_inUse && s_mode != ePointerMode_Look; }

	bool InUse()				{ return s_inUse; }
	bool IsCaptured()			{ return s_captured && s_mode == ePointerMode_Look; }
	bool WantsHiddenCursor()	{ return s_hasFocus && s_mode != ePointerMode_MenuCursor; }

	void SetCaptured(bool bCapture)
	{
		s_captured = bCapture;
		ApplyCapture();
	}

	void ToggleCaptured()
	{
		SetCaptured(!s_captured);
	}
}

// ---------------------------------------------------------------------------
// C_Win64Input - merges keyboard/mouse over the library's pad state
// ---------------------------------------------------------------------------

void C_Win64Input::Tick(void)
{
	C_4JInput::Tick();

	// Latch a stable snapshot for this tick so that a key pressed and released
	// between two ticks is still seen as a press, and so every query within a
	// tick agrees with every other.
	for (int i = 0; i < KEY_STATE_COUNT; i++)
	{
		s_keyLastTick[i] = s_keyThisTick[i];
		s_keyThisTick[i] = s_keyDown[i];

		if (s_keyThisTick[i])	s_keyHeldTicks[i]++;
		else					s_keyHeldTicks[i] = 0;
	}

	// Note: mouse motion is NOT consumed here. It is drained once per frame by
	// Win64Input::ConsumeLookDelta - see Win64ApplyMouseLook in Minecraft.cpp.

	// Carry the text-entry flag forward one tick past being cleared - see
	// TextInputOwnsKeyboard.
	s_textInputTick = s_textInput;

	if (s_wheelUpTicks   > 0) s_wheelUpTicks--;
	if (s_wheelDownTicks > 0) s_wheelDownTicks--;

	// Keep the cursor on the window centre while we are driving mouse-look.
	// Done here rather than only on capture and focus changes because the
	// things that break the cursor clip do not send us a message.
	if (s_captured && s_hasFocus && s_mode == Win64Input::ePointerMode_Look) PinCursorToCentre();
}

// The action-value query.
//
// This is the path jump (Input.cpp), place/use and hotbar scrolling
// (Minecraft.cpp) read, and it is entirely separate from ButtonPressed /
// ButtonDown - so shadowing only those left every one of them dead on the
// keyboard even though the bindings existed.
//
// bRepeat == false is a level query: is the action active right now. That is
// what jump wants, so that holding space keeps jumping.
// bRepeat == true is an edge query with auto-repeat, matching how the library
// treats a held pad button for scrolling and repeated block placement.
unsigned int C_Win64Input::GetValue(int iPad, unsigned char ucAction, bool bRepeat)
{
	unsigned int uiPad = C_4JInput::GetValue(iPad, ucAction, bRepeat);
	if (uiPad > 0) return uiPad;

	if (iPad != 0 || !s_inUse || TextInputOwnsKeyboard()) return 0;

	int iWheelTicks = 0;
	if (ActionHasWheel(ucAction, iWheelTicks))
	{
		// A wheel notch is inherently an edge, so with repeat asked for it
		// reports only on the notch's first tick rather than every tick of the
		// hold - otherwise one notch would scroll the hotbar twice.
		const bool bActive = bRepeat ? (iWheelTicks == WHEEL_HOLD_TICKS) : (iWheelTicks > 0);
		return bActive ? ACTION_VALUE_ON : 0;
	}

	const int iMouseKey = MouseKeyForMenuAction(ucAction);
	if (iMouseKey != 0 && (bRepeat ? KeyRepeated(iMouseKey) : KeyHeld(iMouseKey))) return ACTION_VALUE_ON;

	const SActionBinding *pBinding = FindBinding(ucAction);
	if (pBinding == NULL) return 0;

	for (int i = 0; i < MAX_KEYS_PER_ACTION; i++)
	{
		const int iKey = pBinding->iKeys[i];
		if (iKey == 0) continue;

		if (bRepeat ? KeyRepeated(iKey) : KeyHeld(iKey)) return ACTION_VALUE_ON;
	}
	return 0;
}

bool C_Win64Input::ButtonPressed(int iPad, unsigned char ucAction)
{
	if (C_4JInput::ButtonPressed(iPad, ucAction)) return true;

	// Keyboard drives player 1 only. Splitscreen guests stay on pads.
	if (iPad != 0 || !s_inUse || TextInputOwnsKeyboard()) return false;

	int iWheelTicks = 0;
	if (ActionHasWheel(ucAction, iWheelTicks))
	{
		return iWheelTicks == WHEEL_HOLD_TICKS;	// only the notch's first tick
	}

	const int iMouseKey = MouseKeyForMenuAction(ucAction);
	if (iMouseKey != 0 && KeyWentDown(iMouseKey)) return true;

	const SActionBinding *pBinding = FindBinding(ucAction);
	if (pBinding == NULL) return false;

	for (int i = 0; i < MAX_KEYS_PER_ACTION; i++)
	{
		if (pBinding->iKeys[i] != 0 && KeyWentDown(pBinding->iKeys[i])) return true;
	}
	return false;
}

bool C_Win64Input::ButtonReleased(int iPad, unsigned char ucAction)
{
	if (C_4JInput::ButtonReleased(iPad, ucAction)) return true;

	if (iPad != 0 || !s_inUse || TextInputOwnsKeyboard()) return false;

	const int iMouseKey = MouseKeyForMenuAction(ucAction);
	if (iMouseKey != 0 && KeyWentUp(iMouseKey)) return true;

	const SActionBinding *pBinding = FindBinding(ucAction);
	if (pBinding == NULL) return false;

	for (int i = 0; i < MAX_KEYS_PER_ACTION; i++)
	{
		if (pBinding->iKeys[i] != 0 && KeyWentUp(pBinding->iKeys[i])) return true;
	}
	return false;
}

bool C_Win64Input::ButtonDown(int iPad, unsigned char ucAction)
{
	if (C_4JInput::ButtonDown(iPad, ucAction)) return true;

	if (iPad != 0 || !s_inUse || TextInputOwnsKeyboard()) return false;

	int iWheelTicks = 0;
	if (ActionHasWheel(ucAction, iWheelTicks))
	{
		return iWheelTicks > 0;
	}

	const int iMouseKey = MouseKeyForMenuAction(ucAction);
	if (iMouseKey != 0 && KeyHeld(iMouseKey)) return true;

	const SActionBinding *pBinding = FindBinding(ucAction);
	if (pBinding == NULL) return false;

	for (int i = 0; i < MAX_KEYS_PER_ACTION; i++)
	{
		if (pBinding->iKeys[i] != 0 && KeyHeld(pBinding->iKeys[i])) return true;
	}
	return false;
}

float C_Win64Input::GetJoypadStick_LX(int iPad, bool bCheckMenuDisplay)
{
	float fPad = C_4JInput::GetJoypadStick_LX(iPad, bCheckMenuDisplay);
	if (iPad != 0 || !s_inUse || fPad != 0.0f || TextInputOwnsKeyboard()) return fPad;

	float fVal = 0.0f;
	if (KeyHeld('D')) fVal += 1.0f;
	if (KeyHeld('A')) fVal -= 1.0f;
	return fVal * MOVE_X_SIGN;
}

float C_Win64Input::GetJoypadStick_LY(int iPad, bool bCheckMenuDisplay)
{
	float fPad = C_4JInput::GetJoypadStick_LY(iPad, bCheckMenuDisplay);
	if (iPad != 0 || !s_inUse || fPad != 0.0f || TextInputOwnsKeyboard()) return fPad;

	float fVal = 0.0f;
	if (KeyHeld('W')) fVal += 1.0f;
	if (KeyHeld('S')) fVal -= 1.0f;
	return fVal * MOVE_Y_SIGN;
}

// GetJoypadStick_RX / _RY are deliberately NOT shadowed any more.
//
// Feeding a mouse delta into a stick axis meant the game's controller path
// (Input::tick -> interpolateTurn, with its quadratic response curve and a
// fixed turn speed) drove mouse look, which is exactly what made it feel wrong:
// a stick is a rate and a mouse delta is a displacement, so turn amount
// depended on framerate and on how the curve happened to bucket the delta.
// The right stick is now pad-only and mouse look bypasses it entirely.

void C_Win64Input::SetMenuDisplayed(int iPad, bool bVal)
{
	C_4JInput::SetMenuDisplayed(iPad, bVal);

	if (iPad == XUSER_INDEX_ANY)
	{
		for (int i = 0; i < XUSER_MAX_COUNT; i++) s_menuDisplayed[i] = bVal;
	}
	else if (iPad >= 0 && iPad < XUSER_MAX_COUNT)
	{
		s_menuDisplayed[iPad] = bVal;
	}
}

bool C_Win64Input::IsPadConnected(int iPad)
{
	if (iPad == 0 && s_inUse) return true;
	return C_4JInput::IsPadConnected(iPad);
}

// ---------------------------------------------------------------------------
// Text entry
//
// The library's RequestKeyboard is the console on-screen keyboard and has no PC
// implementation, so calling through to it does nothing at all - which is
// exactly what every text field in the game did on Windows. Handing the request
// to UITextEditor instead is what makes those fields typeable, without touching
// any of the call sites.
//
// The request is only recorded here. Deciding what to type into, and pushing
// the in-game keyboard scene when there is no field, happens on the next UI
// tick: RequestKeyboard is called from inside an Iggy callback, which is not a
// safe place to alter the scene stack from.
// ---------------------------------------------------------------------------

EKeyboardResult C_Win64Input::RequestKeyboard(LPCWSTR Title, LPCWSTR Text, DWORD dwPad, UINT uiMaxChars,
											  int( *Func)(LPVOID,const bool), LPVOID lpParam,
											  C_4JInput::EKeyboardMode eMode)
{
	g_UITextEdit.Request(Title, Text, (int)dwPad, (int)uiMaxChars, Func, lpParam, eMode);

	// Pending, not accepted: the callback fires later, which is the contract
	// every caller was already written against.
	return EKeyboard_Pending;
}

void C_Win64Input::GetText(uint16_t *UTF16String)
{
	g_UITextEdit.GetResult(UTF16String);
}


// ---------------------------------------------------------------------------
// 4J Meow - the LWJGL Mouse shim, backed by the real cursor.
//
// stubs.h had these returning 0, which is why the Java-derived Screen/Button
// UI never showed a hover state: GameRenderer feeds Mouse::getX/getY into
// Screen::render as the hover position, and the buttons were being asked
// forever about the top-left corner.
//
// Coordinates are in physical client pixels; GameRenderer scales them into the
// GUI coordinate space itself. getY is bottom-up because the call site does
// the LWJGL flip and is shared with the console builds, which must not change.
// ---------------------------------------------------------------------------
int Mouse::getX()
{
	float fX = 0.0f, fY = 0.0f;
	if(!Win64Input::GetPointerPos(fX, fY)) return 0;
	return (int)fX;
}

int Mouse::getY()
{
	float fX = 0.0f, fY = 0.0f;
	if(!Win64Input::GetPointerPos(fX, fY)) return 0;

	int iClientW = 0, iClientH = 0;
	if(!Win64Input::GetClientSize(iClientW, iClientH)) return 0;

	return iClientH - 1 - (int)fY;
}

bool Mouse::isButtonDown(int iButton)
{
	return Win64Input::IsMouseButtonDown(iButton);
}

#endif	// _WINDOWS64
