#pragma once
#include "Screen.h"
using namespace std;

class ChatScreen : public Screen
{
protected:
	wstring message;
private:
	int frame;

public:
	ChatScreen();	//4J added
	// 4J Meow - opening with '/' starts the line with a slash, as in Java
	// Edition, so a command can be typed without reaching for the key twice.
	explicit ChatScreen(const wstring &initialMessage);
	virtual void init();
    virtual void removed();
    virtual void tick();
protected:
	void keyPressed(wchar_t ch, int eventKey);
public:
	void render(int xm, int ym, float a);
protected:
	void mouseClicked(int x, int y, int buttonNum);
};