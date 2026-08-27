#pragma once

#include "UIPointer.h"

// 4J Meow - In-place text entry for the Windows x64 build.
//
// The problem this replaces
// -------------------------
// Every text field in this game is entered through
// InputManager.RequestKeyboard, which lives in the prebuilt 4J_Input.lib and is
// the *console* on-screen keyboard. There is no PC implementation behind it, so
// on Windows every one of those call sites - the world name, the world seed,
// signs, the anvil, renaming a save - simply did nothing. The one text prompt
// that did work, "Join Server", worked by opening a native Win32 window on top
// of the game, which is not what typing into a game should feel like.
//
// What happens instead
// --------------------
// A player with a keyboard should just type into the field that is already on
// screen. UITextEditor owns an edit session against an existing
// UIControl_TextInput: it drains WM_CHAR text from Win64Input, maintains a
// buffer and a caret, and pushes the result back into the Flash control every
// tick, so the field visibly changes as it is typed.
//
// Nothing at the call sites changed. C_Win64Input shadows RequestKeyboard and
// GetText (the same facade-subclassing trick the rest of Win64KeyboardMouse.h
// uses), so RequestKeyboard starts an edit session and the caller's existing
// completion callback still reads its answer back out of GetText.
//
// Where the text goes when there is no field to type into
// ------------------------------------------------------
// A RequestKeyboard caller does not tell us which control it is editing. It
// does not have to: it is always called in response to the player activating
// that control, so the scene's focused control *is* the target. When the
// focused control is not a text input (renaming a save, where focus is on the
// save list) or there is no keyboard in use at all (a pad player), the request
// falls back to UIScene_Keyboard - the game's own in-game on-screen keyboard
// scene, which 4J left unfinished and which is completed alongside this.
//
// Either way the text entry is inside the game. See
// docs/systems/windows-text-entry.md.

// _UI_INLINE_TEXT_ENTRY is defined in UIPointer.h, alongside the other UI
// feature macros, so that it is reachable from every UI header without anyone
// having to remember to include this one first.
#ifdef _UI_INLINE_TEXT_ENTRY

#include "..\..\Windows64\4JLibs\inc\4J_Input.h"

class UIControl_Base;
class UIScene;

class UITextEditor
{
public:
	// How the target control's text is kept in step.
	//
	// eTarget_Inline  the buffer here is authoritative and is pushed into the
	//                 control, with a blinking caret appended for display only.
	//
	// eTarget_Scene   UIScene_Keyboard's own text field, which the *pad* also
	//                 edits through ActionScript. The control is authoritative
	//                 there, so the buffer is re-read from it before every edit
	//                 and no caret is drawn - the movie draws its own.
	enum ETarget
	{
		eTarget_Inline,
		eTarget_Scene,
	};

private:
	bool			m_bActive;
	ETarget			m_eTarget;
	UIControl_Base *m_pControl;
	UIScene		   *m_pScene;

	wstring			m_buffer;
	int				m_iCaret;
	int				m_iMaxChars;
	int				m_iCaretBlink;
	bool			m_bDirty;

	C_4JInput::EKeyboardMode m_eMode;

	// The caller's completion callback, invoked exactly as the console keyboard
	// would have invoked it.
	int	 (*m_pFunc)(LPVOID, const bool);
	LPVOID m_lpParam;

	// The last committed (or cancelled) text, which is what GetText returns.
	wstring	m_result;

	// A request that arrived from RequestKeyboard and has not been given a
	// target yet. Deferred to the next tick on purpose: RequestKeyboard is
	// called from inside an Iggy callback, which is not a safe place to push a
	// scene from.
	bool	m_bPending;
	wstring	m_pendingTitle;
	wstring	m_pendingText;
	int		m_iPendingMaxChars;
	int		m_iPendingPad;
	C_4JInput::EKeyboardMode m_ePendingMode;
	int	 (*m_pPendingFunc)(LPVOID, const bool);
	LPVOID m_lpPendingParam;

public:
	UITextEditor();

	// Called by C_Win64Input::RequestKeyboard. Always accepts - the target is
	// worked out on the next tick.
	void Request(const wchar_t *pszTitle, const wchar_t *pszText, int iPad, int iMaxChars,
				 int (*pFunc)(LPVOID, const bool), LPVOID lpParam, C_4JInput::EKeyboardMode eMode);

	// Called once a tick from UIController::Tick, before input is dispatched to
	// the scenes - the editor swallows the keyboard while it is up.
	void Tick();

	// Abandons the session without calling the completion callback. This is
	// DestroyKeyboard's behaviour and also what a scene being torn down wants.
	void Abandon();

	bool IsActive() const	{ return m_bActive; }
	bool IsPending() const	{ return m_bPending; }

	// Fills a caller's UTF-16 buffer with the committed text, clamped to the
	// character limit the caller asked for - GetText takes no size, so the
	// limit the request carried is the only thing that makes it safe.
	void GetResult(uint16_t *pOut) const;

	// --- UIScene_Keyboard hooks -------------------------------------------
	//
	// The scene claims a pending request when it opens, and reports the result
	// when its Done or Back button is used.
	bool ClaimPending(UIScene *pScene, UIControl_Base *pControl,
					  wstring &titleOut, wstring &textOut, int &iMaxCharsOut);
	void Finish(bool bAccepted);
	void SceneClosing(UIScene *pScene);

private:
	void Begin(ETarget eTarget, UIScene *pScene, UIControl_Base *pControl,
			   const wstring &initial, int iMaxChars, C_4JInput::EKeyboardMode eMode,
			   int (*pFunc)(LPVOID, const bool), LPVOID lpParam);

	void ResolvePending();
	void DrainInput();
	void Refresh(bool bShowCaret);

	bool AcceptsChar(wchar_t ch) const;
};

extern UITextEditor g_UITextEdit;

#endif // _UI_INLINE_TEXT_ENTRY
