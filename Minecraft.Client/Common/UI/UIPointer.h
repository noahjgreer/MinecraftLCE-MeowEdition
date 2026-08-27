#pragma once

// 4J Meow - Feature macros for pointer-driven UI navigation.
//
// The Vita port already had everything needed to drive the Iggy menus from a
// screen position rather than from the d-pad: control bounding boxes read back
// out of Flash, a scene-level SetFocus(id) call, and per-control touch helpers.
// All of it was written behind #ifdef __PSVITA__, because a touchscreen was the
// only pointing device 4J ever shipped this game with.
//
// A mouse is the same problem. Rather than writing a second, parallel hit-test
// for Windows, the small shared pieces are moved behind _UI_POINTER_SUPPORT and
// the Windows-specific driver that feeds them a mouse position sits behind
// _UI_MOUSE_POINTER.
//
// Deliberately two macros, not one:
//
//   _UI_POINTER_SUPPORT  the platform-agnostic plumbing - control bounds,
//                        scene focus by id, the "is this control gone" flag.
//                        Vita and Windows x64.
//
//   _UI_MOUSE_POINTER    UIController::TickMousePointer and the mouse-specific
//                        policy around it. Windows x64 only. Vita keeps its own
//                        touchbox path (UIController::HandleTouchInput), which
//                        has drag and gesture semantics a mouse does not want.
//
// Nothing here is defined for the console targets, so their preprocessed output
// is byte-for-byte what it was before.

#if defined(_WINDOWS64)
	#define _UI_MOUSE_POINTER

	// 4J Meow - in-place text entry, so a keyboard types straight into the menu
	// field that is already on screen instead of into the console on-screen
	// keyboard (which has no PC implementation) or a native Win32 prompt window
	// on top of the game. Windows x64 only; see UITextEdit.h.
	#define _UI_INLINE_TEXT_ENTRY
#endif

#if defined(__PSVITA__) || defined(_UI_MOUSE_POINTER)
	#define _UI_POINTER_SUPPORT
#endif
