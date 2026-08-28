#include "stdafx.h"
#include "Screen.h"
#include "Button.h"
#include "GuiParticles.h"
#include "Tesselator.h"
#include "Textures.h"
#include "..\Minecraft.World\SoundTypes.h"
#ifdef _WINDOWS64
#include "Windows64\Win64KeyboardMouse.h"
#include "Windows64\MeowLog.h"
#include "Common\App_enums.h"
#endif



Screen::Screen()	// 4J added
{
	minecraft = NULL;
	width = 0;
    height = 0;
	passEvents = false;
	font = NULL;
	particles = NULL;
	clickedButton = NULL;
	m_iFocusButton = -1;	// 4J Meow
	m_bSwallowSelectUntilRelease = true;	// 4J Meow
}

// 4J Meow - focus helpers. Skip buttons that are hidden or greyed out, so the
// focus cursor never lands somewhere that cannot be pressed.
Button *Screen::getFocusedButton()
{
	if(m_iFocusButton < 0 || m_iFocusButton >= (int)buttons.size()) return NULL;
	return buttons[m_iFocusButton];
}

void Screen::setFocusToFirstButton()
{
	m_iFocusButton = -1;
	for(int i = 0; i < (int)buttons.size(); i++)
	{
		if(buttons[i]->visible && buttons[i]->active) { m_iFocusButton = i; break; }
	}
}

void Screen::moveFocus(int iDir)
{
	const int iCount = (int)buttons.size();
	if(iCount == 0) return;

	if(m_iFocusButton < 0) { setFocusToFirstButton(); return; }

	// Wrap, and never stop on a button that cannot be pressed. Bounded by
	// iCount so a screen of entirely inactive buttons cannot spin here.
	int i = m_iFocusButton;
	for(int iStep = 0; iStep < iCount; iStep++)
	{
		i += iDir;
		if(i < 0) i = iCount - 1;
		if(i >= iCount) i = 0;

		if(buttons[i]->visible && buttons[i]->active)
		{
			m_iFocusButton = i;
			return;
		}
	}
}

void Screen::render(int xm, int ym, float a)
{
	// 4J Meow - publish the pad focus to the buttons before they draw, so the
	// focused one highlights exactly as a moused-over one does.
	for (int i = 0; i < (int)buttons.size(); i++)
	{
		buttons[i]->focused = (i == m_iFocusButton);
	}

	AUTO_VAR(itEnd, buttons.end());
	for (AUTO_VAR(it, buttons.begin()); it != itEnd; it++)
	{
        Button *button = *it; //buttons[i];
        button->render(minecraft, xm, ym);
    }
}

void Screen::keyPressed(wchar_t eventCharacter, int eventKey)
{
	if (eventKey == Keyboard::KEY_ESCAPE)
	{
		minecraft->setScreen(NULL);
//    minecraft->grabMouse();	// 4J - removed
	}
}

wstring Screen::getClipboard()
{
	// 4J - removed
	return NULL;
}

void Screen::setClipboard(const wstring& str)
{
	// 4J - removed
}

void Screen::mouseClicked(int x, int y, int buttonNum)
{
    if (buttonNum == 0)
	{
		AUTO_VAR(itEnd, buttons.end());
		for (AUTO_VAR(it, buttons.begin()); it != itEnd; it++)
		{
            Button *button = *it; //buttons[i];
            if (button->clicked(minecraft, x, y))
			{
                clickedButton = button;
                minecraft->soundEngine->playUI(eSoundType_RANDOM_CLICK, 1, 1);
                buttonClicked(button);
            }
        }
    }
}

void Screen::mouseReleased(int x, int y, int buttonNum)
{
    if (clickedButton!=NULL && buttonNum==0)
	{
        clickedButton->released(x, y);
        clickedButton = NULL;
    }
}

void Screen::buttonClicked(Button *button)
{
}

void Screen::init(Minecraft *minecraft, int width, int height)
{
    particles = new GuiParticles(minecraft);
    this->minecraft = minecraft;
    this->font = minecraft->font;
    this->width = width;
    this->height = height;
    buttons.clear();
    init();

	// 4J Meow - start with the first usable button focused, so a pad player has
	// somewhere to be the moment the screen opens.
	setFocusToFirstButton();

	// And do not let the keypress that opened this screen press a button on it.
	m_bSwallowSelectUntilRelease = true;
}

void Screen::setSize(int width, int height)
{
    this->width = width;
    this->height = height;
}

void Screen::init()
{
}

void Screen::updateEvents()
{
#ifdef _WINDOWS64
	// 4J Meow - put back in. The original drained LWJGL's Mouse/Keyboard event
	// queues here; on Windows there is no queue to drain, so this samples the
	// pointer state that Win64KeyboardMouse already tracks and synthesises the
	// press/release edges the Screen API expects.
	//
	// Deliberately edge-triggered off MouseButtonWentDown rather than level-
	// triggered off IsMouseButtonDown: mouseClicked plays a sound and fires
	// buttonClicked, so a held button must not repeat it every frame.
	static bool s_bWasDown = false;

	const bool bDown = Win64Input::IsMouseButtonDown(0);
	const bool bWentDown = Win64Input::MouseButtonWentDown(0);

	int xm = 0, ym = 0;
	const bool bHavePos = getPointerPos(xm, ym);

	if(bHavePos)
	{
		if(bWentDown)
		{
			mouseClicked(xm, ym, 0);
		}
		else if(s_bWasDown && !bDown)
		{
			mouseReleased(xm, ym, 0);
		}
	}

	s_bWasDown = bDown;

	// 4J Meow - keyboardEvent() was left a TODO: nothing ever called keyPressed
	// on Windows, so a Screen that reads text (ChatScreen) could not be typed
	// into at all.
	//
	// Only screens that have claimed the keyboard for text get key events.
	// Every other Screen is driven by tickMenuInput below, and the base
	// Screen::keyPressed closes on Escape - which would fight the pause menu
	// and the menu-cancel action if it were delivered unconditionally.
	if(Win64Input::IsTextInputActive())
	{
		wchar_t ch;
		while(Win64Input::ConsumeTypedChar(ch))
		{
			// WM_CHAR delivers these as control characters; the Screen API
			// wants the key code beside the character.
			int iEventKey = 0;
			switch(ch)
			{
			case 0x1B:	iEventKey = Keyboard::KEY_ESCAPE;	break;
			case 0x0D:
			case 0x0A:	iEventKey = Keyboard::KEY_RETURN;	break;
			case 0x08:	iEventKey = Keyboard::KEY_BACK;		break;
			default:	break;
			}

			keyPressed(ch, iEventKey);

			// keyPressed can close the screen (Escape, or Return committing a
			// chat message). Anything still queued belongs to whatever comes
			// next, not to a screen that is already gone.
			if(minecraft != NULL && minecraft->screen != this) break;
		}
	}

	// Pad / keyboard navigation, so a Screen is drivable without a mouse.
	tickMenuInput();
#endif
}

// 4J Meow - added. Client pixels -> this screen's GUI-scaled coordinate space,
// which is what width/height and every control position are in. Same conversion
// UIController::TickMousePointer does for the Flash scenes, except there is no
// Y flip here: Windows client coordinates are already top-down, unlike the
// bottom-up coordinates the LWJGL original had to invert.
bool Screen::getPointerPos(int &xm, int &ym)
{
#ifdef _WINDOWS64
	if(!Win64Input::IsPointerActive()) return false;

	float fX = 0.0f, fY = 0.0f;
	if(!Win64Input::GetPointerPos(fX, fY)) return false;

	int iClientW = 0, iClientH = 0;
	if(!Win64Input::GetClientSize(iClientW, iClientH)) return false;
	if(iClientW <= 0 || iClientH <= 0) return false;

	xm = (int)(fX * (float)width  / (float)iClientW);
	ym = (int)(fY * (float)height / (float)iClientH);
	return true;
#else
	return false;
#endif
}

// 4J Meow - added. Pad and keyboard navigation for a Screen.
//
// Uses the same ACTION_MENU_* actions the Flash UI uses, read through the same
// edge-triggered ButtonPressed query, so this works from a controller and from
// the keyboard mappings without knowing anything about either.
void Screen::tickMenuInput()
{
#ifdef _WINDOWS64
	int iPad = ProfileManager.GetPrimaryPad();
	if(iPad < 0 || iPad >= XUSER_MAX_COUNT) iPad = 0;

	if(buttons.empty()) return;

	// The mouse and the focus cursor must not fight. If the pointer is live and
	// actually moved this tick, it owns the highlight and the pad cursor gets
	// out of the way - the same rule UIController::TickMousePointer uses for
	// the Flash controls.
	if(Win64Input::IsPointerActive() && Win64Input::ConsumePointerMoved())
	{
		m_iFocusButton = -1;
	}

	const bool bUp		= InputManager.ButtonPressed(iPad, ACTION_MENU_UP);
	const bool bDown	= InputManager.ButtonPressed(iPad, ACTION_MENU_DOWN);
	bool bSelect		= InputManager.ButtonPressed(iPad, ACTION_MENU_A);

	// Ignore select until the button that opened this screen has been let go.
	// Navigation stays live throughout - only activation is held back.
	if(m_bSwallowSelectUntilRelease)
	{
		if(InputManager.ButtonDown(iPad, ACTION_MENU_A))
		{
			bSelect = false;
		}
		else
		{
			m_bSwallowSelectUntilRelease = false;
			MEOWLOG("[meow]   Screen: select armed\n");
		}
	}

	// One line whenever the pointer's usability changes, so a dead menu can be
	// told apart from a menu nobody is talking to.
	{
		static int s_iLastActive = -1;
		const int iActive = Win64Input::IsPointerActive() ? 1 : 0;
		if(iActive != s_iLastActive)
		{
			s_iLastActive = iActive;
			MEOWLOG("[meow]   Screen: pointer active = %d, buttons = %d\n", iActive, (int)buttons.size());
		}
	}

	if(bUp || bDown)
	{
		if(m_iFocusButton < 0) setFocusToFirstButton();
		else moveFocus(bDown ? 1 : -1);
		MEOWLOG("[meow]   Screen: focus -> %d\n", m_iFocusButton);
	}

	// Back out of this screen. Without this, any screen you enter is a dead end:
	// Screen::keyPressed has the Escape handling but nothing calls it, because
	// Screen::keyboardEvent is still one of the unported LWJGL stubs.
	//
	// setScreen(NULL) is the Java idiom for "leave this screen"; with no level
	// loaded it lands back on the title screen (see Minecraft::setScreen).
	if(InputManager.ButtonPressed(iPad, ACTION_MENU_B))
	{
		MEOWLOG("[meow]   Screen: back\n");
		minecraft->setScreen(NULL);
		return;
	}

	if(bSelect)
	{
		Button *pButton = getFocusedButton();
		MEOWLOG("[meow]   Screen: select, focus %d\n", m_iFocusButton);
		if(pButton != NULL && pButton->visible && pButton->active)
		{
			clickedButton = pButton;
			minecraft->soundEngine->playUI(eSoundType_RANDOM_CLICK, 1, 1);
			buttonClicked(pButton);
		}
	}
#endif
}

void Screen::mouseEvent()
{
	// 4J Meow - folded into updateEvents; there is no per-event callback to
	// hang this off on Windows.
}

void Screen::keyboardEvent()
{
	/* 4J - TODO
    if (Keyboard.getEventKeyState()) {
        if (Keyboard.getEventKey() == Keyboard.KEY_F11) {
            minecraft.toggleFullScreen();
            return;
        }
        keyPressed(Keyboard.getEventCharacter(), Keyboard.getEventKey());
    }
	*/
}

void Screen::tick()
{
}

void Screen::removed()
{
}

void Screen::renderBackground()
{
	renderBackground(0);
}

void Screen::renderBackground(int vo)
{
	if (minecraft->level != NULL)
	{
		fillGradient(0, 0, width, height, 0xc0101010, 0xd0101010);
	}
	else
	{
		renderDirtBackground(vo);
	}
}

void Screen::renderDirtBackground(int vo)
{
	// 4J Unused
#if 0
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    Tesselator *t = Tesselator::getInstance();
    glBindTexture(GL_TEXTURE_2D, minecraft->textures->loadTexture(L"/gui/background.png"));
    glColor4f(1, 1, 1, 1);
    float s = 32;
    t->begin();
    t->color(0x404040);
    t->vertexUV((float)(0), (float)( height), (float)( 0), (float)( 0), (float)( height / s + vo));
    t->vertexUV((float)(width), (float)( height), (float)( 0), (float)( width / s), (float)( height / s + vo));
    t->vertexUV((float)(width), (float)( 0), (float)( 0), (float)( width / s), (float)( 0 + vo));
    t->vertexUV((float)(0), (float)( 0), (float)( 0), (float)( 0), (float)( 0 + vo));
    t->end();
#endif
}

bool Screen::isPauseScreen()
{
	return true;
}

void Screen::confirmResult(bool result, int id)
{
}

void Screen::tabPressed()
{
}
