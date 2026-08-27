#include "stdafx.h"
#include "UI.h"
#include "UIScene_Keyboard.h"
#include "UITextEdit.h"

#define KEYBOARD_DONE_TIMER_ID 0
#define KEYBOARD_DONE_TIMER_TIME 100

UIScene_Keyboard::UIScene_Keyboard(int iPad, void *initData, UILayer *parentLayer) : UIScene(iPad, parentLayer)
{
	// Setup all the Iggy references we need for this scene
	initialiseMovie();

	// 4J Meow - 4J left this scene built but never connected to anything: it
	// had a hardcoded sign-entry title, a hardcoded 15 character limit, and a
	// Done handler whose only comment was a ToDo about passing the string on.
	//
	// It is now the fallback for InputManager.RequestKeyboard - the case where
	// the request cannot be typed straight into a menu field, either because
	// the scene has no text input focused (renaming a save) or because the
	// player is on a pad and has no keyboard to type with. So the title, the
	// starting text and the character limit come from the request.
	wstring title		= L"Enter Sign Text";
	wstring startText	= L"";
	int iMaxChars		= 15;

#ifdef _UI_INLINE_TEXT_ENTRY
	g_UITextEdit.ClaimPending(this, &m_KeyboardTextInput, title, startText, iMaxChars);
#endif

	m_EnterTextLabel.init(title);

	m_KeyboardTextInput.init(startText, -1);
	m_KeyboardTextInput.SetCharLimit(iMaxChars);
	
	m_ButtonSpace.init(L"Space", -1);
	m_ButtonCursorLeft.init(L"Cursor Left", -1);
	m_ButtonCursorRight.init(L"Cursor Right", -1);
	m_ButtonCaps.init(L"Caps", -1);
	m_ButtonDone.init(L"Done", 0);	// only the done button needs an id, the others will never call back!
	m_ButtonSymbols.init(L"Symbols", -1);
	m_ButtonBackspace.init(L"Backspace", -1);

	// Initialise function keyboard Buttons and set alternative symbol button string
	wstring label = L"Abc";
	IggyStringUTF16 stringVal;
	stringVal.string = (IggyUTF16*)label.c_str();
	stringVal.length = label.length();
	
	IggyDataValue result;
	IggyDataValue value[1];
	value[0].type = IGGY_DATATYPE_string_UTF16;
	value[0].string16 = stringVal;

	IggyResult out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ), m_funcInitFunctionButtons , 1 , value );

	m_bKeyboardDonePressed = false;

	parentLayer->addComponent(iPad,eUIComponent_MenuBackground);
}

UIScene_Keyboard::~UIScene_Keyboard()
{
	m_parentLayer->removeComponent(eUIComponent_MenuBackground);
}

wstring UIScene_Keyboard::getMoviePath()
{
	if(app.GetLocalPlayerCount() > 1 && !m_parentLayer->IsFullscreenGroup())
	{
		return L"KeyboardSplit";
	}
	else
	{
		return L"Keyboard";
	}
}

void UIScene_Keyboard::updateTooltips()
{
	ui.SetTooltips( DEFAULT_XUI_MENU_USER, IDS_TOOLTIPS_SELECT,IDS_TOOLTIPS_BACK, -1, -1);
}

bool UIScene_Keyboard::allowRepeat(int key)
{
	// 4J - TomK - we want to allow X and Y repeats!
	switch(key)
	{
	case ACTION_MENU_OK:
	case ACTION_MENU_CANCEL:
	case ACTION_MENU_A:
	case ACTION_MENU_B:
	case ACTION_MENU_PAUSEMENU:
	//case ACTION_MENU_X:
	//case ACTION_MENU_Y:
		return false;
	}
	return true;
}

void UIScene_Keyboard::handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled)
{
	IggyDataValue result;
	IggyResult out;

	if(repeat || pressed)
	{
		switch(key)
		{
		case ACTION_MENU_CANCEL:
#ifdef _UI_INLINE_TEXT_ENTRY
			// Backing out is a cancelled keyboard, which the caller has to be
			// told about - every RequestKeyboard callback in the game takes a
			// bRes and leaves its field alone when it is false.
			g_UITextEdit.Finish(false);
#endif
			navigateBack();
			handled = true;
			break;
		case ACTION_MENU_X:					// X
			out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ), m_funcBackspaceButtonPressed, 0 , NULL );
			handled = true;
			break;
		case ACTION_MENU_PAGEUP:			// LT
			out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ), m_funcSymbolButtonPressed, 0 , NULL );
			handled = true;
			break;
		case ACTION_MENU_Y:					// Y
			out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ), m_funcSpaceButtonPressed, 0 , NULL );
			handled = true;
			break;
		case ACTION_MENU_STICK_PRESS:		// LS
			out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ), m_funcCapsButtonPressed, 0 , NULL );
			handled = true;
			break;
		case ACTION_MENU_LEFT_SCROLL:		// LB
			out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ), m_funcCursorLeftButtonPressed, 0 , NULL );
			handled = true;
			break;
		case ACTION_MENU_RIGHT_SCROLL:		// RB
			out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ), m_funcCursorRightButtonPressed, 0 , NULL );
			handled = true;
			break;
		case ACTION_MENU_PAUSEMENU:			// Start
			if(!m_bKeyboardDonePressed)
			{
				out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ), m_funcDoneButtonPressed, 0 , NULL );
				
				// kick off done timer
				addTimer(KEYBOARD_DONE_TIMER_ID,KEYBOARD_DONE_TIMER_TIME);
				m_bKeyboardDonePressed = true;
			}
			handled = true;
			break;
		}
	}

	switch(key)
	{
	case ACTION_MENU_OK:
	case ACTION_MENU_LEFT:
	case ACTION_MENU_RIGHT:
	case ACTION_MENU_UP:
	case ACTION_MENU_DOWN:
		sendInputToMovie(key, repeat, pressed, released);
		handled = true;
		break;
	}
}

void UIScene_Keyboard::handlePress(F64 controlId, F64 childId)
{
	if((int)controlId == 0)
	{
		// Done has been pressed. At this point we can query for the input string and pass it on to wherever it is needed.
		// we can not query for m_KeyboardTextInput.getLabel() here because we're in an iggy callback so we need to wait a frame.
		if(!m_bKeyboardDonePressed)
		{
			// kick off done timer
			addTimer(KEYBOARD_DONE_TIMER_ID,KEYBOARD_DONE_TIMER_TIME);
			m_bKeyboardDonePressed = true;
		}
	}
}

void UIScene_Keyboard::handleTimerComplete(int id)
{
	if(id == KEYBOARD_DONE_TIMER_ID)
	{
		// remove timer
		killTimer(KEYBOARD_DONE_TIMER_ID);

		// we're done here!
		KeyboardDonePressed();
	}
}

void UIScene_Keyboard::KeyboardDonePressed()
{
	// Debug
	app.DebugPrintf("UI Keyboard - DONE - [%ls]\n", m_KeyboardTextInput.getLabel());

#ifdef _UI_INLINE_TEXT_ENTRY
	// Hands the text to whoever called RequestKeyboard. Finish re-reads it from
	// the text field rather than from its own buffer, because in this scene it
	// is ActionScript - driven by the on-screen keys - that owns the text.
	g_UITextEdit.Finish(true);
#endif

	navigateBack();
}