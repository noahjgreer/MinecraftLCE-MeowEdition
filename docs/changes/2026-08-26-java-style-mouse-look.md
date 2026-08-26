# Java-Edition-style mouse look (Windows x64)

## What changed

Mouse look no longer goes through the controller path. Previously the raw mouse
delta was converted into a right-stick deflection and consumed by `Input::tick`,
which feeds `Entity::interpolateTurn` with a quadratic response curve and a
fixed `turnSpeed`, once per 20Hz game tick. That is correct for a stick (a rate
control) and wrong for a mouse (a displacement), so turn amount depended on the
framerate and small movements were squashed by the curve.

It is now modelled on Java Edition's `MouseHandler.turnPlayer`: accumulate raw
pixels, drain them **once per rendered frame**, apply the cubic sensitivity
curve, and call `Entity::turn` directly.

## Files

- `Minecraft.Client/Windows64/Win64KeyboardMouse.h/.cpp`
  - Removed the `GetJoypadStick_RX` / `_RY` shadows entirely — the right stick
    is pad-only again. Removed `MOUSE_LOOK_RANGE`, `s_lookX`, `s_lookY`.
  - `s_mouseAccumX/Y` are no longer cleared by `Tick()`; they are drained by the
    new `Win64Input::ConsumeLookDelta(float&, float&)`.
  - New shadow `C_Win64Input::SetMenuDisplayed`, which forwards to the base and
    mirrors the flag into `Win64Input::IsMenuDisplayed(iPad)`. The library keeps
    that state private and only exposes it via `bCheckMenuDisplay` on the stick
    getters; since mouse look no longer reads a stick, it needs its own view of
    it so it is suppressed in exactly the same cases.
- `Minecraft.Client/Minecraft.cpp`
  - New file-static `Win64ApplyMouseLook(Minecraft*)`, guarded by `_WINDOWS64`.
  - Called from `run_middle` immediately after `timer->advanceTime()` and before
    the tick loop — the same position as Java's
    `MouseHandler::handleAccumulatedMovement`.

## The maths

```
setting = GetGameSettings(iPad, eGameSetting_Sensitivity_InGame) / 100   // Java's 0..1
ss      = setting * 0.6 + 0.2
sens    = ss * ss * ss * 8
xo      =  dx * sens
yo      = -dy * sens          // see sign note below
player->turn(xo, invertLook ? -yo : yo)
```

`Entity::turn` then applies its own `* 0.15f`. At the middle sensitivity setting
`sens` is exactly 1.0, giving 0.15 degrees per mouse pixel — the same as Java.

**Sign note.** Java's `Entity.turn` takes screen-space Y (down positive) and
*adds* to `xRot`. LCE's `Entity::turn` takes the stick convention (up positive)
and *subtracts*. So the Y delta is negated on the way in. Everything else about
the two functions matches, including that both advance `xRotO`/`yRotO` so the
rotation is not smeared across the render interpolation — which is precisely
what `interpolateTurn` (the old path) does *not* do.

## Deliberately not ported

- **Smoothing.** Java's `smoothCamera` / `SmoothDouble` path. LCE has no
  equivalent option, and it is off by default in Java.
- **Scoping sensitivity reduction.** Java drops to `sensitivityMod` (no `* 8`)
  while a spyglass is scoped. LCE has no spyglass.
- **Screen/GUI mouse cursor.** Menus are still driven as a virtual pad. Out of
  scope here.

## Gating

`Win64ApplyMouseLook` turns only when: keyboard/mouse is in use, the cursor is
captured, `level != NULL`, `screen == NULL`, not paused, no menu is displayed for
the pad, `app.GetFreezePlayers()` is false, and the game mode allows the
`MINECRAFT_ACTION_LOOK_*` actions (tutorial gating — checked per axis, as
`Input::tick` does).

It also calls `ResetInactiveTicks()` on movement. The idle timer in `run_middle`
watched the sticks; mouse look no longer touches them, so without this, looking
around without pressing a key would drop into the idle animation after ~10s.

## Unverified

Compiles clean at `Release|x64` (0 warnings, 0 errors). **Runtime behaviour is
unverified — no agent can run the game.** Specifically worth checking:

- Pitch direction. The sign reasoning above is from reading both `turn`
  implementations, not from playing. If pitch is inverted, flip the negation on
  `fYo` in `Win64ApplyMouseLook` (not `LOOK_Y_SIGN`, which is now a raw-delta
  tunable).
- Sensitivity feel. If it is uniformly too fast or slow, `MOUSE_SENSITIVITY` in
  `Win64KeyboardMouse.cpp` is a straight scale on top of the in-game setting.
- Controller look is unchanged in principle (the stick shadows are gone, so the
  pad reaches `Input::tick` exactly as it did before this fork touched input),
  but it is worth a sanity check with a pad plugged in.
