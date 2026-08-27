#include "stdafx.h"
#include "UI.h"
#include "UITextEdit.h"

#ifdef _UI_INLINE_TEXT_ENTRY

#include "UIScene_Keyboard.h"
#include "..\..\Windows64\Win64KeyboardMouse.h"

UITextEditor g_UITextEdit;

// A caret that never blinks reads as a stray underscore, and one that blinks
// fast reads as a glitch. 20 ticks on, 20 off.
#define CARET_BLINK_TICKS	20

// WM_CHAR delivers the editing keys as control characters, which is the whole
// reason text entry uses it rather than the virtual key codes: it has already
// applied the keyboard layout, the shift state and any dead keys.
#define CHAR_BACKSPACE		8
#define CHAR_TAB			9
#define CHAR_RETURN			13
#define CHAR_ESCAPE			27

UITextEditor::UITextEditor()
{
	m_bActive			= false;
	m_eTarget			= eTarget_Inline;
	m_pControl			= NULL;
	m_pScene			= NULL;
	m_iCaret			= 0;
	m_iMaxChars			= 0;
	m_iCaretBlink		= 0;
	m_bDirty			= false;
	m_eMode				= C_4JInput::EKeyboardMode_Default;
	m_pFunc				= NULL;
	m_lpParam			= NULL;

	m_bPending			= false;
	m_iPendingMaxChars	= 0;
	m_iPendingPad		= 0;
	m_ePendingMode		= C_4JInput::EKeyboardMode_Default;
	m_pPendingFunc		= NULL;
	m_lpPendingParam	= NULL;
}

void UITextEditor::Request(const wchar_t *pszTitle, const wchar_t *pszText, int iPad, int iMaxChars,
						   int (*pFunc)(LPVOID, const bool), LPVOID lpParam, C_4JInput::EKeyboardMode eMode)
{
	// A second request while one is up means the first was abandoned by its
	// scene without telling us. The new one wins; the old callback is dropped
	// exactly as a cancelled console keyboard would have dropped it.
	if(m_bActive) Abandon();

	m_bPending			= true;
	m_pendingTitle		= (pszTitle != NULL) ? pszTitle : L"";
	m_pendingText		= (pszText  != NULL) ? pszText  : L"";
	m_iPendingMaxChars	= (iMaxChars > 0) ? iMaxChars : 32;
	m_iPendingPad		= iPad;
	m_ePendingMode		= eMode;
	m_pPendingFunc		= pFunc;
	m_lpPendingParam	= lpParam;
}

void UITextEditor::Begin(ETarget eTarget, UIScene *pScene, UIControl_Base *pControl,
						 const wstring &initial, int iMaxChars, C_4JInput::EKeyboardMode eMode,
						 int (*pFunc)(LPVOID, const bool), LPVOID lpParam)
{
	m_bActive		= true;
	m_eTarget		= eTarget;
	m_pScene		= pScene;
	m_pControl		= pControl;
	m_buffer		= initial;
	m_iMaxChars		= iMaxChars;
	m_eMode			= eMode;
	m_pFunc			= pFunc;
	m_lpParam		= lpParam;
	m_iCaretBlink	= 0;
	m_bDirty		= true;

	if((int)m_buffer.length() > m_iMaxChars) m_buffer = m_buffer.substr(0, m_iMaxChars);
	m_iCaret = (int)m_buffer.length();

	// Stale keystrokes from before the field opened - including the Return or
	// the click that opened it - must not land in it.
	Win64Input::ClearTypedChars();

	// Tell the input layer to stop mapping the keyboard onto game actions while
	// we own it, or typing "was" would walk the player and open a menu.
	Win64Input::SetTextInputActive(true);
}

void UITextEditor::Abandon()
{
	if(m_bActive)
	{
		if(m_pControl != NULL && m_eTarget == eTarget_Inline)
		{
			// Put the field back to the text without the caret in it.
			m_pControl->setLabel(m_buffer, true, true);
		}
		Win64Input::SetTextInputActive(false);
	}

	m_bActive	= false;
	m_pControl	= NULL;
	m_pScene	= NULL;
	m_pFunc		= NULL;
	m_lpParam	= NULL;
	m_bPending	= false;
}

void UITextEditor::Finish(bool bAccepted)
{
	if(!m_bActive) return;

	// The scene-hosted keyboard lets ActionScript own the text, so the control
	// is the authority on what was actually typed there.
	if(m_eTarget == eTarget_Scene && m_pControl != NULL)
	{
		m_buffer = m_pControl->getLabel();
	}

	if((int)m_buffer.length() > m_iMaxChars) m_buffer = m_buffer.substr(0, m_iMaxChars);
	m_result = bAccepted ? m_buffer : L"";

	int (*pFunc)(LPVOID, const bool)	= m_pFunc;
	LPVOID lpParam						= m_lpParam;
	UIControl_Base *pControl			= m_pControl;
	const ETarget eTarget				= m_eTarget;
	const wstring committed				= m_buffer;

	// Clear the session before calling back: the callback is free to open the
	// next field - the sign entry screen walks its four lines that way.
	m_bActive	= false;
	m_pControl	= NULL;
	m_pScene	= NULL;
	m_pFunc		= NULL;
	m_lpParam	= NULL;
	Win64Input::SetTextInputActive(false);

	// Drop the caret. Every KeyboardComplete* callback in the game only writes
	// the field when bRes is true, so showing the committed text here and
	// letting an accepted callback overwrite it is right either way.
	if(eTarget == eTarget_Inline && pControl != NULL)
	{
		pControl->setLabel(committed, true, true);
	}

	if(pFunc != NULL) pFunc(lpParam, bAccepted);
}

void UITextEditor::SceneClosing(UIScene *pScene)
{
	if(m_bActive && m_pScene == pScene) Abandon();
}

void UITextEditor::GetResult(uint16_t *pOut) const
{
	if(pOut == NULL) return;

	int iLen = (int)m_result.length();
	if(iLen > m_iMaxChars) iLen = m_iMaxChars;

	for(int i = 0; i < iLen; ++i) pOut[i] = (uint16_t)m_result[i];
	pOut[iLen] = 0;
}

bool UITextEditor::ClaimPending(UIScene *pScene, UIControl_Base *pControl,
								wstring &titleOut, wstring &textOut, int &iMaxCharsOut)
{
	if(!m_bPending) return false;

	titleOut		= m_pendingTitle;
	textOut			= m_pendingText;
	iMaxCharsOut	= m_iPendingMaxChars;

	m_bPending = false;
	Begin(eTarget_Scene, pScene, pControl, m_pendingText, m_iPendingMaxChars, m_ePendingMode,
		  m_pPendingFunc, m_lpPendingParam);

	return true;
}

// ---------------------------------------------------------------------------
// Per-tick work
// ---------------------------------------------------------------------------

void UITextEditor::Tick()
{
	if(m_bPending) ResolvePending();
	if(!m_bActive) return;

	// The scene that owns the field can be navigated away from underneath us -
	// by the pad, or by a message box. Editing a control on a dead scene would
	// call into a freed movie.
	if(m_pScene != NULL && !ui.IsSceneLive(m_pScene))
	{
		Abandon();
		return;
	}

	DrainInput();

	if(m_bActive && m_eTarget == eTarget_Inline)
	{
		const int iBlink = m_iCaretBlink % (CARET_BLINK_TICKS * 2);
		m_iCaretBlink++;

		// Only touch Flash on the frames the display actually changes: a
		// SetLabel a tick is a string marshal a tick for nothing.
		if(m_bDirty || iBlink == 0 || iBlink == CARET_BLINK_TICKS)
		{
			Refresh(iBlink < CARET_BLINK_TICKS);
			m_bDirty = false;
		}
	}
}

void UITextEditor::ResolvePending()
{
	int iPad = m_iPendingPad;
	if(iPad < 0 || iPad >= XUSER_MAX_COUNT) iPad = ProfileManager.GetPrimaryPad();
	if(iPad < 0 || iPad >= XUSER_MAX_COUNT) iPad = 0;

	UIScene *pScene = ui.GetActiveScene(iPad);

	// A player who has not touched the keyboard gets the on-screen keyboard, so
	// a pad-only session is no worse off than it was.
	UIControl_Base *pTarget = NULL;
	if(Win64Input::InUse() && pScene != NULL) pTarget = pScene->findFocusedTextInput();

	if(pTarget != NULL)
	{
		m_bPending = false;
		Begin(eTarget_Inline, pScene, pTarget, m_pendingText, m_iPendingMaxChars, m_ePendingMode,
			  m_pPendingFunc, m_lpPendingParam);
		return;
	}

	// m_bPending stays set: UIScene_Keyboard claims it as it opens. If the
	// scene cannot be pushed there is nothing sensible left to do, so drop the
	// request rather than leaving it pending forever.
	if(!ui.NavigateToScene(iPad, eUIScene_Keyboard, NULL, eUILayer_Fullscreen, eUIGroup_Fullscreen))
	{
		m_bPending = false;
	}
}

bool UITextEditor::AcceptsChar(wchar_t ch) const
{
	if(ch < 0x20) return false;			// control characters are handled above
	if(ch == 0x7F) return false;		// DEL

	switch(m_eMode)
	{
	case C_4JInput::EKeyboardMode_Numeric:
	case C_4JInput::EKeyboardMode_Phone:
		return (ch >= L'0' && ch <= L'9');

	case C_4JInput::EKeyboardMode_IP_Address:
		return (ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L':';

	default:
		return true;
	}
}

void UITextEditor::DrainInput()
{
	// Caret movement and Delete have no WM_CHAR, so they are the one thing read
	// from the key state rather than the typed-character queue. Repeat is used
	// so holding an arrow walks the caret, matching what WM_CHAR already does
	// for backspace on its own.
	if(Win64Input::EditKeyRepeated(VK_LEFT)  && m_iCaret > 0)						{ m_iCaret--; m_bDirty = true; }
	if(Win64Input::EditKeyRepeated(VK_RIGHT) && m_iCaret < (int)m_buffer.length())	{ m_iCaret++; m_bDirty = true; }
	if(Win64Input::EditKeyRepeated(VK_HOME))										{ m_iCaret = 0; m_bDirty = true; }
	if(Win64Input::EditKeyRepeated(VK_END))											{ m_iCaret = (int)m_buffer.length(); m_bDirty = true; }

	if(Win64Input::EditKeyRepeated(VK_DELETE) && m_iCaret < (int)m_buffer.length())
	{
		m_buffer.erase(m_iCaret, 1);
		m_bDirty = true;
	}

	wchar_t ch;
	while(Win64Input::ConsumeTypedChar(ch))
	{
		// In the on-screen keyboard scene the movie owns the text and the pad
		// may have changed it since the last character, so re-read before
		// editing rather than writing a stale buffer back over it.
		if(m_eTarget == eTarget_Scene && m_pControl != NULL)
		{
			m_buffer = m_pControl->getLabel();
			if(m_iCaret > (int)m_buffer.length()) m_iCaret = (int)m_buffer.length();
		}

		switch(ch)
		{
		case CHAR_RETURN:
			Finish(true);
			return;

		case CHAR_ESCAPE:
			Finish(false);
			return;

		case CHAR_TAB:
			// Field-to-field tabbing would need to know the scene's field
			// order, which no scene exposes. Swallowed rather than inserted.
			break;

		case CHAR_BACKSPACE:
			if(m_iCaret > 0)
			{
				m_buffer.erase(m_iCaret - 1, 1);
				m_iCaret--;
				m_bDirty = true;
			}
			break;

		default:
			if(AcceptsChar(ch) && (int)m_buffer.length() < m_iMaxChars)
			{
				m_buffer.insert(m_iCaret, 1, ch);
				m_iCaret++;
				m_bDirty = true;
			}
			break;
		}

		if(m_bDirty && m_eTarget == eTarget_Scene && m_pControl != NULL)
		{
			m_pControl->setLabel(m_buffer, true, true);
			m_bDirty = false;
		}
	}
}

void UITextEditor::Refresh(bool bShowCaret)
{
	if(m_pControl == NULL) return;

	// The caret is display only - it is inserted into the label, never into the
	// buffer, so it can never be committed as text.
	wstring display = m_buffer;
	if(bShowCaret)
	{
		int iAt = m_iCaret;
		if(iAt < 0) iAt = 0;
		if(iAt > (int)display.length()) iAt = (int)display.length();
		display.insert(iAt, 1, L'_');
	}

	m_pControl->setLabel(display, true, true);
}

#endif // _UI_INLINE_TEXT_ENTRY
