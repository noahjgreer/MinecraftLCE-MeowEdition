#pragma once
#include "GuiComponent.h"
class Button;
class GuiParticles;
class Minecraft;
using namespace std;

class Screen : public GuiComponent
{
protected:
	Minecraft *minecraft;
public:
	int width;
    int height;
protected:
	vector<Button *> buttons;
public:
	bool passEvents;
protected:
	Font *font;
public:
	GuiParticles *particles;

	Screen();		// 4J added
    virtual void render(int xm, int ym, float a);
protected:
	virtual void keyPressed(wchar_t eventCharacter, int eventKey);
public:
	static wstring getClipboard();
    static void setClipboard(const wstring& str);
private:
	Button *clickedButton;

protected:
	virtual void mouseClicked(int x, int y, int buttonNum);
    virtual void mouseReleased(int x, int y, int buttonNum);
    virtual void buttonClicked(Button *button);
public:
	virtual void init(Minecraft *minecraft, int width, int height);
    virtual void setSize(int width, int height);
    virtual void init();
    virtual void updateEvents();
    virtual void mouseEvent();
protected:
	// 4J Meow - current pointer position in this screen's GUI-scaled space.
	// False when there is no live pointer (pad in use, or mouse-look mode).
	bool getPointerPos(int &xm, int &ym);

	// 4J Meow - pad / keyboard navigation.
	//
	// The Java screens this stack came from were mouse-only, so a controller
	// could not touch them. These give the buttons a focus cursor driven by
	// ACTION_MENU_UP/DOWN, activated with ACTION_MENU_A - the same actions the
	// Flash UI uses, so the keyboard mappings come along for free.
	int m_iFocusButton;					// index into buttons, -1 for none

	// 4J Meow - the press that opened this screen must not also press a button
	// on it. The Enter that dismissed the Flash save-message dialog was still
	// held when the native title screen appeared, so it immediately activated
	// the focused button and shot straight through to the next screen.
	//
	// Set whenever a screen opens; cleared on the first tick the select button
	// is not held. The Flash UI solves the same problem with its own
	// m_bIgnoreInput flag (see UIScene_SaveMessage::handlePress).
	bool m_bSwallowSelectUntilRelease;

	void tickMenuInput();
	void moveFocus(int iDir);
	void setFocusToFirstButton();
	Button *getFocusedButton();
public:
	// 4J Meow - for logging; a screen with no buttons is almost always one that
	// has not been ported yet.
	int GetButtonCount() { return (int)buttons.size(); }
    virtual void keyboardEvent();
    virtual void tick();
    virtual void removed();
    virtual void renderBackground();
    virtual void renderBackground(int vo);
    virtual void renderDirtBackground(int vo);
    virtual bool isPauseScreen();
    virtual void confirmResult(bool result, int id);
    virtual void tabPressed();
};

