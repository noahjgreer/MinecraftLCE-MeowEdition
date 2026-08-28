#pragma once
#include "Screen.h"
#include "Button.h"
class Random;
using namespace std;

// 4J Meow - a button drawn in the console style rather than the Java one.
//
// The Java Button blits the 200x20 widget art out of gui.png, which is what
// made the ported menus look like Java Edition. LCE's buttons are a flat
// translucent panel with a border, filling a much larger box (674x60 in the
// 1920x1080 authored space - see docs/reference/lce-menu-layouts.txt), so they
// are drawn rather than blitted.
//
// Lives here for now because the main menu is the only screen using it. Move it
// to its own file as soon as a second ported screen needs it.
class LCEButton : public Button
{
public:
	LCEButton(int id, int x, int y, int w, int h, const wstring &msg)
		: Button(id, x, y, w, h, msg) {}

	virtual void render(Minecraft *minecraft, int xm, int ym);
};

class TitleScreen : public Screen
{
private:
	static Random *random;

    float vo;

    wstring splash;
    Button *multiplayerButton;

public:
	TitleScreen();
    virtual void tick();
protected:
	virtual void keyPressed(wchar_t eventCharacter, int eventKey);
public:
	virtual void init();
protected:
	virtual void buttonClicked(Button *button);
public:
	virtual void render(int xm, int ym, float a);

private:
	// 4J Meow - the rotating cube panorama, restored from the Java original.
	//
	// Draws Common/res/1_2_2/title/bg/panorama0..5.png as the six faces of a
	// cube seen from the inside, slowly turning. The Flash main menu had its
	// own panorama (UIComponent_Panorama, a .swf); this is the same effect
	// built from plain PNGs, which is the point of the migration.
	//
	// The soft focus is the Java trick of drawing the cube several times at
	// sub-pixel offsets with falling alpha, rather than a real blur pass -
	// there is no render-to-texture in this path to blur with.
	void renderPanorama(float a);

	float m_panoramaTime;	// advanced in tick(), drives the rotation

	// 4J Meow - maps the LCE authored stage (1920x1080, the space every
	// coordinate in docs/reference/lce-menu-layouts.txt is expressed in) onto
	// this screen's coordinate space.
	//
	// Scaled uniformly off height and centred horizontally, so a non-16:9
	// window letterboxes the layout rather than stretching it.
	float lceScale() const;
	int lceX(float x1080) const;
	int lceY(float y1080) const;
	int lceLen(float len1080) const;
};