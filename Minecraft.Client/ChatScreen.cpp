#include "stdafx.h"
#include "ChatScreen.h"
#include "MultiplayerLocalPlayer.h"
#include "..\Minecraft.World\SharedConstants.h"
#include "..\Minecraft.World\StringHelpers.h"
#ifdef _WINDOWS64
#include "Windows64\Win64KeyboardMouse.h"
#endif

// 4J Meow - was:
//     const wstring ChatScreen::allowedChars = SharedConstants::acceptableLetters;
//
// That is a static initialisation order bug. Both are namespace-scope statics,
// and SharedConstants::acceptableLetters is not filled in until
// SharedConstants::staticCtor() runs from Minecraft.World.cpp - long after this
// copy was taken. allowedChars was therefore empty for the whole run.
//
// It went unnoticed because the character test was `find(ch) >= 0`, which is
// always true (find returns npos, not -1), so the empty table was never
// consulted. The table is now read at the point of use instead.

ChatScreen::ChatScreen()
{
	frame = 0;
}

ChatScreen::ChatScreen(const wstring &initialMessage)
{
	frame = 0;
	message = initialMessage;
}

void ChatScreen::init()
{
	Keyboard::enableRepeatEvents(true);

#ifdef _WINDOWS64
	// 4J Meow - claim the keyboard for text. This stops it driving the player
	// and the menus while chat is open, and it is also what makes
	// Screen::updateEvents deliver key events here at all.
	Win64Input::ClearTypedChars();
	Win64Input::SetTextInputActive(true);
#endif
}

void ChatScreen::removed()
{
	Keyboard::enableRepeatEvents(false);

#ifdef _WINDOWS64
	Win64Input::SetTextInputActive(false);
#endif
}

void ChatScreen::tick()
{
	frame++;
}

void ChatScreen::keyPressed(wchar_t ch, int eventKey)
{
    if (eventKey == Keyboard::KEY_ESCAPE)
	{
        minecraft->setScreen(NULL);
        return;
    }
    if (eventKey == Keyboard::KEY_RETURN)
	{
        wstring msg = trimString(message);
        if (msg.length() > 0)
		{
            wstring trim = trimString(message);
            if (!minecraft->handleClientSideCommand(trim))
			{
                minecraft->player->chat(trim);
            }
        }
        minecraft->setScreen(NULL);
        return;
    }
    if (eventKey == Keyboard::KEY_BACK && message.length() > 0) message = message.substr(0, message.length() - 1);

	// Control characters are handled above and must never reach the message -
	// backspace and return would otherwise be appended to it.
	if (ch >= 0x20 && ch != 0x7F && message.length() < SharedConstants::maxChatLength)
	{
		// An empty table means the accepted-character list has not been built
		// yet. Accept printable characters rather than silently refusing to
		// type anything, which is how the bug above presented.
		const wstring &allowed = SharedConstants::acceptableLetters;
		if (allowed.empty() || allowed.find(ch) != wstring::npos)
		{
			message += ch;
		}
	}

}

void ChatScreen::render(int xm, int ym, float a)
{
    fill(2, height - 14, width - 2, height - 2, 0x80000000);
    drawString(font, L"> " + message + (frame / 6 % 2 == 0 ? L"_" : L""), 4, height - 12, 0xe0e0e0);

    Screen::render(xm, ym, a);
}

void ChatScreen::mouseClicked(int x, int y, int buttonNum)
{
    if (buttonNum == 0)
	{
        if (minecraft->gui->selectedName != L"")	// 4J - was NULL comparison
		{
			if (message.length() > 0 && message[message.length()-1]!=L' ')
			{
                message += L" ";
            }
            message += minecraft->gui->selectedName;
            unsigned int maxLength = SharedConstants::maxChatLength;
            if (message.length() > maxLength)
			{
                message = message.substr(0, maxLength);
            }
        }
		else
		{
            Screen::mouseClicked(x, y, buttonNum);
        }
    }

}