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

	// The cursor clip rectangle is in screen coordinates, so it goes stale the
	// moment the window moves or resizes. Call from WM_MOVE / WM_SIZE.
	void OnWindowMoved();

	// True while we are hiding and pinning the cursor, so WM_SETCURSOR can stop
	// the window class cursor being put back every time the mouse is moved.
	bool WantsHiddenCursor();

	// Registers for raw mouse input and remembers the window. Call once, after
	// the main window exists.
	void Initialise(HWND hWnd);

	// Drains the raw mouse pixels accumulated since the last call and applies
	// the sensitivity/sign tunables. Screen convention: +X right, +Y down.
	// Called once per rendered frame by Win64ApplyMouseLook (Minecraft.cpp) -
	// mouse look does not go through the right stick. Returns false if there
	// was no movement.
	bool ConsumeLookDelta(float &fDX, float &fDY);

	// Whether the game currently has a menu up for this pad, mirrored from
	// C_Win64Input::SetMenuDisplayed. Mouse look honours this so it is
	// suppressed in exactly the cases the right stick would have been.
	bool IsMenuDisplayed(int iPad);

	// True once the player has actually used the keyboard or mouse. Until then
	// we stay out of the way entirely and the pad behaves as it always did.
	bool InUse();

	// Typed text, fed from WM_CHAR. This is a separate path from OnKeyDown on
	// purpose: WM_CHAR is what applies the keyboard layout, shift state and dead
	// keys, and it delivers backspace, tab, return and escape as characters too,
	// so a text field needs nothing else. Drained by UITextEditor::DrainInput.
	void OnChar(wchar_t ch);
	bool ConsumeTypedChar(wchar_t &ch);
	void ClearTypedChars();

	// Set while a menu text field is being typed into (UITextEdit.cpp). The
	// keyboard stops contributing to game and menu actions entirely for as long
	// as it is set, so that typing a world name cannot also walk the player,
	// press buttons or open the pause menu. The pad is untouched: a splitscreen
	// guest can carry on playing while player 1 types.
	//
	// It stays effective for the remainder of the tick it is cleared on, so the
	// Return that commits a field is not then also read as "press the focused
	// button" by the same tick's input pass.
	void SetTextInputActive(bool bActive);
	bool IsTextInputActive();

	// Caret movement and Delete for a text field. These are the one part of
	// text entry that has to come from the key state rather than from WM_CHAR,
	// because they do not produce a character. Auto-repeats like the game's own
	// held-key repeat, so holding an arrow walks the caret.
	bool EditKeyRepeated(int iVirtualKey);

	// Cursor capture for mouse-look.
	void SetCaptured(bool bCapture);
	bool IsCaptured();
	void ToggleCaptured();

	// ---------------------------------------------------------------------
	// Pointer mode.
	//
	// Mouse-look and a usable menu pointer want opposite things from the OS
	// cursor: look needs it hidden, clipped and parked so it can never leave
	// the window or accumulate a position, while pointing at a menu needs a
	// real position that the player can see. So the cursor is put into one of
	// three modes, decided centrally once a tick by UpdatePointerMode rather
	// than by whoever happens to touch the mouse last.
	// ---------------------------------------------------------------------
	enum EPointerMode
	{
		ePointerMode_Look,			// hidden, clipped, parked on centre; raw deltas turn the player
		ePointerMode_MenuCursor,	// OS arrow visible and free; absolute position tracked
		ePointerMode_HiddenCursor,	// hidden and clipped but free; the game draws its own pointer
	};

	// Called once a tick from the UI (UIController::TickMousePointer).
	// bSceneDrawsPointer selects the hidden mode: the container menus position
	// and draw their own pointer in Flash, and a second OS arrow on top of it,
	// one frame behind, is worse than either alone.
	void UpdatePointerMode(bool bMenuDisplayed, bool bSceneDrawsPointer);
	EPointerMode GetPointerMode();

	// Absolute cursor position in client pixels, fed from WM_MOUSEMOVE. This is
	// a separate path from the raw deltas on purpose: a pointer wants the
	// position the OS has already accelerated and clamped to the screen, not a
	// position we integrate ourselves and have to clamp and accelerate again.
	void OnMouseMove(int iClientX, int iClientY);
	bool GetPointerPos(float &fX, float &fY);
	bool GetClientSize(int &iWidth, int &iHeight);

	// True once, for the tick after the pointer actually moved. Menu focus only
	// follows the mouse when it moved, so that a player using the pad is not
	// fighting a stationary cursor sitting over some other button.
	bool ConsumePointerMoved();

	// Raw mouse button state, for the one caller that needs a button as a button
	// rather than as a mapped action: dragging a menu slider. 0 left, 1 right,
	// 2 middle - the same numbering as OnMouseButton.
	bool IsMouseButtonDown(int iButton);
	bool MouseButtonWentDown(int iButton);

	// Hotbar impulses, consumed once per *game tick* by Minecraft::tick.
	//
	// Both pend rather than living in the per-frame snapshot: Tick() runs per
	// rendered frame and the gameplay input code runs at 20Hz, so a per-frame
	// edge is usually gone before the game tick that would have read it.
	//
	// ConsumeHotbarSlot returns the 0-based slot (0-8) a number key selected.
	// ConsumeWheelNotch returns one pending wheel notch as -1 / 0 / +1,
	// positive being wheel-up/away.
	bool ConsumeHotbarSlot(int &iSlot);

	// Chat. LCE has no chat action - the console builds never had typed chat -
	// so this does not go through the 4J_Input action table at all; it pends
	// like the hotbar keys and is collected by Minecraft::tick.
	//
	// bSlash is true when the player opened chat with '/', in which case the
	// screen starts with a '/' already typed, as it does in Java Edition.
	bool ConsumeChatRequest(bool &bSlash);
	int  ConsumeWheelNotch();
	void ClearWheelNotches();

	// True when the pointer is live: kb/m in use and not in look mode.
	bool IsPointerActive();
}

// Drop-in replacement for the library's C_4JInput global.
class C_Win64Input : public C_4JInput
{
public:
	// Shadowed (deliberately non-virtual - every call site resolves against the
	// static type of the Win64InputManager object, so shadowing is sufficient
	// and avoids assuming anything about the library's layout).
	void	Tick(void);

	// The action-value query. This is a separate path from ButtonPressed /
	// ButtonDown and is what jump, place/use, hotbar scrolling and menu cancel
	// actually read - see Input.cpp and Minecraft.cpp. Missing this shadow is
	// why those did nothing from the keyboard.
	unsigned int GetValue(int iPad, unsigned char ucAction, bool bRepeat = false);

	bool	ButtonPressed(int iPad, unsigned char ucAction = 255);
	bool	ButtonReleased(int iPad, unsigned char ucAction);
	bool	ButtonDown(int iPad, unsigned char ucAction = 255);

	float	GetJoypadStick_LX(int iPad, bool bCheckMenuDisplay = true);
	float	GetJoypadStick_LY(int iPad, bool bCheckMenuDisplay = true);
	// GetJoypadStick_RX / _RY are NOT shadowed: mouse look bypasses the stick
	// and the controller interpolation behind it. See Win64ApplyMouseLook in
	// Minecraft.cpp and docs/systems/windows-mouse-look.md.

	// Shadowed only to mirror the menu-displayed flag, which the library
	// otherwise keeps to itself and which mouse look needs to see.
	void	SetMenuDisplayed(int iPad, bool bVal);

	// So the game does not sit on "please connect a controller" when the player
	// only has a keyboard.
	bool	IsPadConnected(int iPad);

	// Text entry.
	//
	// RequestKeyboard is the console on-screen keyboard, and 4J_Input.lib has
	// no PC implementation of it - so every text field in the game (world name,
	// seed, signs, the anvil, renaming a save) did nothing on Windows. Shadowed
	// here so the request instead opens an edit session on the menu field that
	// is already on screen, or, for a pad player or a field the scene does not
	// expose, on the game's own in-game keyboard scene.
	//
	// Nothing at the ~15 call sites changed: they still get their completion
	// callback, and still read the answer back out of GetText. See
	// Common/UI/UITextEdit.h.
	EKeyboardResult	RequestKeyboard(LPCWSTR Title, LPCWSTR Text, DWORD dwPad, UINT uiMaxChars,
									int( *Func)(LPVOID,const bool), LPVOID lpParam,
									C_4JInput::EKeyboardMode eMode);
	void			GetText(uint16_t *UTF16String);
};

extern C_Win64Input Win64InputManager;

#endif // _WINDOWS64
