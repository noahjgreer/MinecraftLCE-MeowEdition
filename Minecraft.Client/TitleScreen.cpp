#include "stdafx.h"
#include "Button.h"
#include "OptionsScreen.h"
#include "SelectWorldScreen.h"
#include "JoinMultiplayerScreen.h"
#include "Tesselator.h"
#include "Textures.h"
#include "Windows64\MeowLog.h"
#include "Font.h"
#include "ClientConstants.h"
#include "..\Minecraft.World\Mth.h"
#include "..\Minecraft.World\StringHelpers.h"
#include "..\Minecraft.World\InputOutputStream.h"
#include "..\Minecraft.World\net.minecraft.locale.h"
#include "..\Minecraft.World\System.h"
#include "..\Minecraft.World\Random.h"
#include "TitleScreen.h"

Random *TitleScreen::random = new Random();

TitleScreen::TitleScreen()
{
	// 4J - added initialisers
	vo = 0;
	multiplayerButton = NULL;
	m_panoramaTime = 0.0f;		// 4J Meow

    splash = L"missingno";
//    try {	// 4J - removed try/catch
    vector<wstring> splashes;

	/*
    BufferedReader *br = new BufferedReader(new InputStreamReader(InputStream::getResourceAsStream(L"res\\title\\splashes.txt"))); //, Charset.forName("UTF-8")
		
    wstring line = L"";
    while ( !(line = br->readLine()).empty() )
	{
        line = trimString( line );
        if (line.length() > 0)
		{
            splashes.push_back(line);
        }
    }
	
    br->close();
	delete br;
	*/

    splash = L""; //splashes.at(random->nextInt(splashes.size()));

//    } catch (Exception e) {
//    }
}

void TitleScreen::tick()
{
	// 4J Meow - drives the panorama rotation. One unit per tick, matching the
	// Java original, which divides this down heavily in renderPanorama.
	m_panoramaTime += 1.0f;
}

void TitleScreen::keyPressed(wchar_t eventCharacter, int eventKey)
{
}

void TitleScreen::init()
{
	/* 4J - removed
    Calendar c = Calendar.getInstance();
    c.setTime(new Date());

    if (c.get(Calendar.MONTH) + 1 == 11 && c.get(Calendar.DAY_OF_MONTH) == 9) {
        splash = "Happy birthday, ez!";
    } else if (c.get(Calendar.MONTH) + 1 == 6 && c.get(Calendar.DAY_OF_MONTH) == 1) {
        splash = "Happy birthday, Notch!";
    } else if (c.get(Calendar.MONTH) + 1 == 12 && c.get(Calendar.DAY_OF_MONTH) == 24) {
        splash = "Merry X-mas!";
    } else if (c.get(Calendar.MONTH) + 1 == 1 && c.get(Calendar.DAY_OF_MONTH) == 1) {
        splash = "Happy new year!";
    }
	*/

	// 4J Meow - see the declarations in TitleScreen.h.
	//
	// LCE authored everything at 1920x1080 (the 720 assets are the same layout
	// at 2/3). Uniform scale off height keeps the proportions the console had.
	// (defined out-of-line below so the constants stay next to the layout)

	// 4J Meow - labels now come from 4J's own string table via app.GetString,
	// not from Language::getElement.
	//
	// Language::getElement is a 4J stub that returns the key it was given
	// (Minecraft.World/Language.cpp: "// 4J TODO  return elementId;") - the
	// Java localisation system was replaced wholesale by the IDS_* table the
	// Flash UI uses. That is why these buttons read "menu.singleplayer" rather
	// than "Singleplayer". Using the same IDs the Flash main menu used means
	// these labels are already translated into every shipped language.
	// The console main menu, reproduced from the live layout dump in
	// docs/reference/lce-menu-layouts.txt:
	//
	//     Button1..6   x 623   y 375 + 75n   w 674   h 60      (1920x1080)
	//
	// 623 + 674/2 == 960, so the column is simply centred; the authored x is
	// the left edge. Button order and labels follow UIScene_MainMenu.cpp, which
	// is the authoritative list of what the console showed.
	const int bw = lceLen(674);
	const int bh = lceLen(60);
	const int bx = lceX(623);

	buttons.push_back(new LCEButton(1, bx, lceY(375), bw, bh, app.GetString(IDS_PLAY_GAME)));

	// Leaderboards is repurposed as "Join Server" on this platform, matching
	// what UIScene_MainMenu.cpp already does under _WINDOWS64.
	buttons.push_back(multiplayerButton =
		new LCEButton(2, bx, lceY(450), bw, bh, L"Join Server"));

	buttons.push_back(new LCEButton(3, bx, lceY(525), bw, bh, app.GetString(IDS_ACHIEVEMENTS)));
	buttons.push_back(new LCEButton(4, bx, lceY(600), bw, bh, app.GetString(IDS_HELP_AND_OPTIONS)));
	buttons.push_back(new LCEButton(5, bx, lceY(675), bw, bh, app.GetString(IDS_DOWNLOADABLECONTENT)));
	buttons.push_back(new LCEButton(6, bx, lceY(750), bw, bh, app.GetString(IDS_EXIT_GAME)));
}

// 4J Meow - LCE stage (1920x1080) -> this screen's coordinate space.
float TitleScreen::lceScale() const
{
	return height / 1080.0f;
}

int TitleScreen::lceX(float x1080) const
{
	// Centre the 1920-wide stage, so a non-16:9 window letterboxes rather than
	// stretching the layout.
	const float k = lceScale();
	return (int)(((float)width - 1920.0f * k) * 0.5f + x1080 * k);
}

int TitleScreen::lceY(float y1080) const
{
	return (int)(y1080 * lceScale());
}

int TitleScreen::lceLen(float len1080) const
{
	return (int)(len1080 * lceScale());
}

// 4J Meow - the console button: a flat translucent panel with a border, not the
// Java gui.png widget. Colours are eyeballed from the shipped UI (the selected
// state is the periwinkle fill seen on the save-message OK button), so they are
// constants here and easy to retune.
void LCEButton::render(Minecraft *minecraft, int xm, int ym)
{
	if (!visible) return;

	const bool hovered = focused || (xm >= x && ym >= y && xm < x + w && ym < y + h);

	const int fill    = !active ? 0x60202020 : (hovered ? 0xE07B7BC8 : 0xB02E3838);
	const int border  = !active ? 0x60808080 : (hovered ? 0xFFFFFFFF : 0xC0C6C6C6);
	const int textCol = !active ? 0xFF808080 : (hovered ? 0xFFFFFFFF : 0xFFE0E0E0);

	GuiComponent::fill(x, y, x + w, y + h, fill);

	// 1px border at 1080, so it stays hairline-thin at any scale.
	const int t = 1;
	GuiComponent::fill(x,         y,         x + w,     y + t,     border);	// top
	GuiComponent::fill(x,         y + h - t, x + w,     y + h,     border);	// bottom
	GuiComponent::fill(x,         y,         x + t,     y + h,     border);	// left
	GuiComponent::fill(x + w - t, y,         x + w,     y + h,     border);	// right

	Font *font = minecraft->font;
	drawCenteredString(font, msg, x + w / 2, y + (h - 8) / 2, textCol);
}

void TitleScreen::buttonClicked(Button *button)
{
	MEOWLOG("[meow]   TitleScreen: buttonClicked id %d\n", button->id);

	// 4J Meow - ids follow the console's Button1..6 order (UIScene_MainMenu):
	// 1 Play Game, 2 Join Server, 3 Achievements, 4 Help & Options,
	// 5 Downloadable Content, 6 Exit Game.
	//
	// 3 and 5 have no native destination yet and deliberately do nothing rather
	// than dropping into an unported Java screen.

	// 4J Meow - these destinations are unrevived 1.2.2-era screens. They are
	// wired up again rather than disabled, because a button that visibly does
	// nothing is worse than one that goes somewhere unfinished - and B /
	// Escape now backs out of any screen, so they are no longer dead ends.
    if (button->id == 1)
	{
        minecraft->setScreen(new SelectWorldScreen(this));
    }
    if (button->id == 2)
	{
        minecraft->setScreen(new JoinMultiplayerScreen(this));
    }
    if (button->id == 4)
	{
        minecraft->setScreen(new OptionsScreen(this, minecraft->options));
    }
    if (button->id == 6)
	{
        minecraft->stop();
    }
}

// 4J Meow - see the comment on the declaration in TitleScreen.h.
void TitleScreen::renderPanorama(float a)
{
	Tesselator *t = Tesselator::getInstance();

	// A perspective camera inside a unit cube. 120 degrees is the Java value
	// and is what makes the horizon curve the way the console menu's does.
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	gluPerspective(120.0f, 1.0f, 0.05f, 10.0f);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	// Inside of the cube: no culling, no depth, straight alpha blending.
	glColor4f(1, 1, 1, 1);
	glRotatef(180, 1, 0, 0);
	glEnable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_CULL_FACE);
	glDepthMask(false);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Java draws the cube 8x8 times at sub-pixel offsets to fake a blur. That
	// is 384 quads a frame, and every one of them is a separate Tesselator
	// begin/end through the D3D wrapper, so the cost here is draw calls rather
	// than pixels. 4x4 keeps the softness and a quarter of the calls.
	const int PASSES = 4;
	const float fTime = m_panoramaTime + a;

	for (int pass = 0; pass < PASSES * PASSES; pass++)
	{
		glPushMatrix();

		const float xo = ((pass % PASSES) / (float)PASSES - 0.5f) / 64.0f;
		const float yo = ((pass / PASSES) / (float)PASSES - 0.5f) / 64.0f;
		glTranslatef(xo, yo, 0.0f);

		// Drift up and down while turning slowly.
		glRotatef(Mth::sin(fTime / 400.0f) * 25.0f + 20.0f, 1, 0, 0);
		glRotatef(-fTime * 0.1f, 0, 1, 0);

		for (int face = 0; face < 6; face++)
		{
			glPushMatrix();

			if (face == 1) glRotatef(90, 0, 1, 0);
			if (face == 2) glRotatef(180, 0, 1, 0);
			if (face == 3) glRotatef(-90, 0, 1, 0);
			if (face == 4) glRotatef(90, 1, 0, 0);
			if (face == 5) glRotatef(-90, 1, 0, 0);

			wchar_t path[64];
			swprintf(path, 64, L"title/bg/panorama%d.png", face);
			minecraft->textures->bindTexture(path);

			// Later passes contribute less, which is what softens the image.
			t->begin();
			t->color(255, 255, 255, 255 / (pass + 1));
			t->vertexUV(-1.0f, -1.0f, 1.0f, 0.0f, 0.0f);
			t->vertexUV( 1.0f, -1.0f, 1.0f, 1.0f, 0.0f);
			t->vertexUV( 1.0f,  1.0f, 1.0f, 1.0f, 1.0f);
			t->vertexUV(-1.0f,  1.0f, 1.0f, 0.0f, 1.0f);
			t->end();

			glPopMatrix();
		}

		glPopMatrix();
	}

	// Put everything back the way the GUI expects to find it.
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	glDepthMask(true);
	glEnable(GL_CULL_FACE);
	glEnable(GL_ALPHA_TEST);
	glDisable(GL_BLEND);
	glColor4f(1, 1, 1, 1);
}

void TitleScreen::render(int xm, int ym, float a)
{
	// 4J Meow - put back in. This was #if 0'd out when the Iggy/Flash main menu
	// replaced it; restored as the first step of moving the UI back onto plain
	// C++ and PNGs. Changes from the original:
	//
	//  - the logo is bound by resource path through Textures::bindTexture
	//    rather than glBindTexture(textures->loadTexture(L"/title/mclogo.png")),
	//    which is how the rest of the client binds a texture that has no
	//    TEXTURE_NAME enum entry. The asset is Common/res/title/mclogo.png.
	//  - the splash text is skipped when empty. The original read it from
	//    res/title/splashes.txt, which is not present in this tree, so splash
	//    is "" and the rotating text would otherwise be an invisible no-op
	//    that still paid for the matrix push and the trig.
	// 4J Meow - the panorama replaces the flat dirt background. renderBackground()
	// is what the Java title screen falls back to when there is no panorama, and
	// it is what made this look like the old 1.2.2 menu.
	MEOWLOG("[meow]     TitleScreen: panorama\n");
	renderPanorama(a);

	// The console menu darkens the panorama so the logo and buttons read
	// against it - but only lightly. LCE keeps the backdrop clearly visible
	// (see the shipped screenshots), unlike Java's heavier scrim.
	fillGradient(0, 0, width, height, 0x30101018, 0x60101018);
	MEOWLOG("[meow]     TitleScreen: bg done\n");

	// 4J Meow - the Meow Edition logo replaces the two-row mclogo.png atlas.
	// GuiComponent::blit hardcodes 1/256 texel size and ties the source rect to
	// the destination size, so it cannot draw this one: the asset is a 1024x256
	// sheet with a 700x123 logo in the top-left corner, drawn at 274x48. Hence
	// the explicit quad below. The original mclogo.png is still in the tree.
	// 4J Meow - sized in LCE stage space now, so it keeps the console's
	// proportions instead of a fixed pixel size that only looked right at one
	// resolution. 840x148 centred at y=55 matches where the Flash logo
	// component sits above the button column (which starts at y=375).
	const int logoW = lceLen(840);
	const int logoH = lceLen(148);
	int logoX = lceX(960) - logoW / 2;
	int logoY = lceY(55);

	const float logoU = 700 / 1024.0f;
	const float logoV = 123 / 256.0f;

	MEOWLOG("[meow]     TitleScreen: bindTexture logo\n");
	minecraft->textures->bindTexture(L"title/meowlogo.png");
	MEOWLOG("[meow]     TitleScreen: logo bound\n");
	glColor4f(1, 1, 1, 1);
	{
		Tesselator *lt = Tesselator::getInstance();
		lt->begin();
		lt->vertexUV((float)logoX,          (float)(logoY + logoH), blitOffset, 0,     logoV);
		lt->vertexUV((float)(logoX + logoW),(float)(logoY + logoH), blitOffset, logoU, logoV);
		lt->vertexUV((float)(logoX + logoW),(float)logoY,           blitOffset, logoU, 0);
		lt->vertexUV((float)logoX,          (float)logoY,           blitOffset, 0,     0);
		lt->end();
	}

	// 4J Meow - the splash sits at 918,189 in the 1920x1080 stage (iggy_Splash
	// in the .swf), i.e. up and to the right of the logo, not over the button
	// column. Still empty until splashes.txt is read - it does exist, at
	// Common/res/1_2_2/title/splashes.txt.
	if (!splash.empty())
	{
		glPushMatrix();
		glTranslatef((float)lceX(918), (float)lceY(189), 0);

		glRotatef(-20, 0, 0, 1);
		float sss = 1.8f - Mth::abs(Mth::sin(System::currentTimeMillis() % 1000 / 1000.0f * PI * 2) * 0.1f);
		glScalef(sss, sss, sss);
		drawCenteredString(font, splash, 0, -8, 0xffff00);
		glPopMatrix();
	}

	MEOWLOG("[meow]     TitleScreen: logo quad done\n");

	// 4J Meow - the Java version string and the "Copyright Mojang AB" line are
	// gone: the console main menu carries neither, and they were a large part of
	// why this screen still read as Java Edition.
	MEOWLOG("[meow]     TitleScreen: strings done\n");

	Screen::render(xm, ym, a);
	MEOWLOG("[meow]     TitleScreen: buttons done\n");
}
