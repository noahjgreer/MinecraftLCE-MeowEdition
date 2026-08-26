#include "stdafx.h"

#if defined(_WINDOWS64)

#include "Win64KeyboardMouse.h"
#include "../Common/App_enums.h"

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

	// Mouse pixels per frame that count as full stick deflection.
	const float MOUSE_LOOK_RANGE		= 24.0f;

	// Multiplies raw mouse movement before the range above is applied.
	const float MOUSE_SENSITIVITY		= 1.0f;

	// Flip either of these if look or movement comes out inverted.
	const float LOOK_X_SIGN				=  1.0f;
	const float LOOK_Y_SIGN				=  1.0f;
	const float MOVE_X_SIGN				=  1.0f;
	const float MOVE_Y_SIGN				=  1.0f;

	// How long a mouse wheel notch reads as a held "scroll" action, in ticks.
	const int	WHEEL_HOLD_TICKS		= 2;

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

	float	s_mouseAccumX	= 0.0f;			// raw delta since last tick
	float	s_mouseAccumY	= 0.0f;
	float	s_lookX			= 0.0f;			// resolved stick value this tick
	float	s_lookY			= 0.0f;

	int		s_wheelUpTicks		= 0;
	int		s_wheelDownTicks	= 0;

	bool	s_inUse			= false;		// has the player touched kb/mouse?
	bool	s_hasFocus		= true;
	bool	s_captured		= false;
	HWND	s_hWnd			= NULL;

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

	const SActionBinding *FindBinding(int iAction)
	{
		for (int i = 0; i < BINDING_COUNT; i++)
		{
			if (s_bindings[i].iAction == iAction) return &s_bindings[i];
		}
		return NULL;
	}

	void ApplyCapture()
	{
		if (s_hWnd == NULL) return;

		if (s_captured && s_hasFocus)
		{
			RECT rc;
			GetClientRect(s_hWnd, &rc);
			POINT tl; tl.x = rc.left;  tl.y = rc.top;
			POINT br; br.x = rc.right; br.y = rc.bottom;
			ClientToScreen(s_hWnd, &tl);
			ClientToScreen(s_hWnd, &br);
			rc.left = tl.x; rc.top = tl.y; rc.right = br.x; rc.bottom = br.y;
			ClipCursor(&rc);
			while (ShowCursor(FALSE) >= 0) {}
		}
		else
		{
			ClipCursor(NULL);
			while (ShowCursor(TRUE) < 0) {}
		}
	}
}

// ---------------------------------------------------------------------------
// Window-procedure entry points
// ---------------------------------------------------------------------------

namespace Win64Input
{
	void Initialise(HWND hWnd)
	{
		s_hWnd = hWnd;

		for (int i = 0; i < KEY_STATE_COUNT; i++)
		{
			s_keyDown[i] = s_keyThisTick[i] = s_keyLastTick[i] = false;
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
		if (iDelta > 0)			s_wheelUpTicks   = WHEEL_HOLD_TICKS;
		else if (iDelta < 0)	s_wheelDownTicks = WHEEL_HOLD_TICKS;
		if (iDelta != 0) MarkInUse();
	}

	void OnRawMouseMove(int iDeltaX, int iDeltaY)
	{
		if (!s_captured) return;
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
			for (int i = 0; i < KEY_STATE_COUNT; i++) s_keyDown[i] = false;
			s_mouseAccumX = s_mouseAccumY = 0.0f;
			SetCaptured(false);
		}
		ApplyCapture();
	}

	bool InUse()		{ return s_inUse; }
	bool IsCaptured()	{ return s_captured; }

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
	}

	// Convert accumulated raw mouse motion into a stick deflection.
	s_lookX = (s_mouseAccumX * MOUSE_SENSITIVITY) / MOUSE_LOOK_RANGE;
	s_lookY = (s_mouseAccumY * MOUSE_SENSITIVITY) / MOUSE_LOOK_RANGE;
	if (s_lookX >  1.0f) s_lookX =  1.0f;
	if (s_lookX < -1.0f) s_lookX = -1.0f;
	if (s_lookY >  1.0f) s_lookY =  1.0f;
	if (s_lookY < -1.0f) s_lookY = -1.0f;
	s_mouseAccumX = s_mouseAccumY = 0.0f;

	if (s_wheelUpTicks   > 0) s_wheelUpTicks--;
	if (s_wheelDownTicks > 0) s_wheelDownTicks--;
}

bool C_Win64Input::ButtonPressed(int iPad, unsigned char ucAction)
{
	if (C_4JInput::ButtonPressed(iPad, ucAction)) return true;

	// Keyboard drives player 1 only. Splitscreen guests stay on pads.
	if (iPad != 0 || !s_inUse) return false;

	int iWheelTicks = 0;
	if (ActionHasWheel(ucAction, iWheelTicks))
	{
		return iWheelTicks == WHEEL_HOLD_TICKS;	// only the notch's first tick
	}

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

	if (iPad != 0 || !s_inUse) return false;

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

	if (iPad != 0 || !s_inUse) return false;

	int iWheelTicks = 0;
	if (ActionHasWheel(ucAction, iWheelTicks))
	{
		return iWheelTicks > 0;
	}

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
	if (iPad != 0 || !s_inUse || fPad != 0.0f) return fPad;

	float fVal = 0.0f;
	if (KeyHeld('D')) fVal += 1.0f;
	if (KeyHeld('A')) fVal -= 1.0f;
	return fVal * MOVE_X_SIGN;
}

float C_Win64Input::GetJoypadStick_LY(int iPad, bool bCheckMenuDisplay)
{
	float fPad = C_4JInput::GetJoypadStick_LY(iPad, bCheckMenuDisplay);
	if (iPad != 0 || !s_inUse || fPad != 0.0f) return fPad;

	float fVal = 0.0f;
	if (KeyHeld('W')) fVal += 1.0f;
	if (KeyHeld('S')) fVal -= 1.0f;
	return fVal * MOVE_Y_SIGN;
}

float C_Win64Input::GetJoypadStick_RX(int iPad, bool bCheckMenuDisplay)
{
	float fPad = C_4JInput::GetJoypadStick_RX(iPad, bCheckMenuDisplay);
	if (iPad != 0 || !s_inUse || fPad != 0.0f) return fPad;

	return s_lookX * LOOK_X_SIGN;
}

float C_Win64Input::GetJoypadStick_RY(int iPad, bool bCheckMenuDisplay)
{
	float fPad = C_4JInput::GetJoypadStick_RY(iPad, bCheckMenuDisplay);
	if (iPad != 0 || !s_inUse || fPad != 0.0f) return fPad;

	// Screen Y grows downward, stick Y grows up.
	return -s_lookY * LOOK_Y_SIGN;
}

bool C_Win64Input::IsPadConnected(int iPad)
{
	if (iPad == 0 && s_inUse) return true;
	return C_4JInput::IsPadConnected(iPad);
}

#endif // _WINDOWS64
