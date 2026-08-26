#include "stdafx.h"
#include "ConnectScreen.h"
#include "ClientConnection.h"
#include "TitleScreen.h"
#include "DisconnectedScreen.h"
#include "Button.h"
#include "Minecraft.h"
#include "User.h"
#include "..\Minecraft.World\net.minecraft.locale.h"


ConnectScreen::ConnectScreen(Minecraft *minecraft, const wstring& ip, int port)
{
	aborted = false;
//    System.out.println("Connecting to " + ip + ", " + port);
    minecraft->setLevel(NULL);
#if 1
	// 4J - removed from separate thread, but need to investigate what we actually need here
    connection = new ClientConnection(minecraft, ip, port);
    if (aborted) return;

	// 4J Meow - A direct connect can simply fail (host down, wrong port, refused),
	// which the console session-based paths never had to express. ClientConnection
	// reports that as createdOk == false and leaves its inner Connection NULL, so
	// sending here without checking is a null dereference.
	if (!connection->createdOk)
	{
		delete connection;
		connection = NULL;
		minecraft->setScreen(new DisconnectedScreen(L"connect.failed", L"Could not reach that server.", NULL));
		return;
	}

    connection->send( shared_ptr<PreLoginPacket>( new PreLoginPacket(minecraft->user->name) ) );
#else

    new Thread() {
        public void run() {

            try {
                connection = new ClientConnection(minecraft, ip, port);
                if (aborted) return;
                connection.send(new PreLoginPacket(minecraft.user.name));
            } catch (UnknownHostException e) {
                if (aborted) return;
                minecraft.setScreen(new DisconnectedScreen("connect.failed", "disconnect.genericReason", "Unknown host '" + ip + "'"));
            } catch (ConnectException e) {
                if (aborted) return;
                minecraft.setScreen(new DisconnectedScreen("connect.failed", "disconnect.genericReason", e.getMessage()));
            } catch (Exception e) {
                if (aborted) return;
                e.printStackTrace();
                minecraft.setScreen(new DisconnectedScreen("connect.failed", "disconnect.genericReason", e.toString()));
            }
        }
    }.start();
#endif
}

void ConnectScreen::tick()
{
    if (connection != NULL)
	{
        connection->tick();
    }
}

void ConnectScreen::keyPressed(char eventCharacter, int eventKey)
{
}

void ConnectScreen::init()
{
    Language *language = Language::getInstance();

    buttons.clear();
    buttons.push_back(new Button(0, width / 2 - 100, height / 4 + 24 * 5 + 12, language->getElement(L"gui.cancel")));

}

void ConnectScreen::buttonClicked(Button *button)
{
    if (button->id == 0)
	{
        aborted = true;
        if (connection != NULL) connection->close();
#ifdef _WINDOWS64
		// TitleScreen::render is entirely #if 0'd on this build (Iggy owns the
		// menu), so returning to it would leave a blank screen.
		minecraft->setScreen(NULL);
#else
        minecraft->setScreen(new TitleScreen());
#endif
    }
}

void ConnectScreen::render(int xm, int ym, float a)
{
    renderBackground();

    Language *language = Language::getInstance();

    if (connection == NULL)
	{
        drawCenteredString(font, language->getElement(L"connect.connecting"), width / 2, height / 2 - 50, 0xffffff);
        drawCenteredString(font, L"", width / 2, height / 2 - 10, 0xffffff);
    }
	else
	{
        drawCenteredString(font, language->getElement(L"connect.authorizing"), width / 2, height / 2 - 50, 0xffffff);
        drawCenteredString(font, connection->message, width / 2, height / 2 - 10, 0xffffff);
    }

    Screen::render(xm, ym, a);
}