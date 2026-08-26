#pragma once

#ifdef _WINDOWS64

// 4J Meow - A modal text prompt for the Windows x64 build.
//
// The game's own text entry is InputManager.RequestKeyboard, which comes from the
// prebuilt 4J_Input.lib and is the console on-screen keyboard. There is no PC
// implementation behind it, so it cannot be used here.
//
// A Screen (the Java-derived UI) is not an option at the main menu either:
// GameRenderer::render, which is what calls mc->screen->render, only runs inside
// "if (setLocalPlayerIdx(i))" in Minecraft::run_middle. With no level and no local
// player there is nothing to iterate, so no Screen is drawn at all before a game
// has started.
//
// That leaves a native window, which renders regardless of what the game is doing.
//
// See docs/systems/dedicated-server-and-direct-connect.md.

namespace Win64TextPrompt
{
	// Modal. Returns false if the player cancelled or the window could not be
	// made. pszBuffer carries the default in and the result out.
	bool Ask(const wchar_t *pszTitle, const wchar_t *pszPrompt, wchar_t *pszBuffer, int iBufferChars);
}

#endif // _WINDOWS64
