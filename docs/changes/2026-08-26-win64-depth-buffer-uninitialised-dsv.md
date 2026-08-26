# 2026-08-26 — No depth buffer on Windows x64: an uninitialised DSV descriptor

## Symptom

Distant and underground geometry painted over nearer geometry: cave systems, ores,
lava and mobs showing through the surface, and the player skin drawn over the sky.
Nothing occluded anything, so whatever was submitted last won.

## Cause

`InitDevice()` in `Windows64_Minecraft.cpp` built its depth-stencil view from an
**uninitialised** `D3D11_DEPTH_STENCIL_VIEW_DESC`:

```cpp
D3D11_DEPTH_STENCIL_VIEW_DESC descDSView;   // stack garbage
descDSView.Format         = DXGI_FORMAT_D24_UNORM_S8_UINT;
descDSView.ViewDimension  = D3D11_DSV_DIMENSION_TEXTURE2D;
descDSView.Texture2D.MipSlice = 0;
// descDSView.Flags never assigned
```

`Flags` is a real member of that struct and it was never written. Both of the values
it could pick up are fatal:

- **`D3D11_DSV_READ_ONLY_DEPTH` (0x1) set** — the view is created successfully but is
  read-only. Depth *testing* still runs; depth *writes* are silently discarded. The
  buffer therefore never fills in, so nothing ever occludes anything.
- **any undefined bit set** — `CreateDepthStencilView` fails with `E_INVALIDARG`.

The second case went unnoticed because that create, and the `CreateTexture2D` above
it, were **the only two creates in the function with no `FAILED()` check** — the
`CreateRenderTargetView` immediately below has one. On failure `g_pDepthStencilView`
stayed `NULL`, was passed as `NULL` to the `OMSetRenderTargets` a few lines later, and
the game rendered with no depth buffer bound at all.

Both paths produce the same picture, which is why the symptom looked consistent.

## Fix

`Minecraft.Client/Windows64/Windows64_Minecraft.cpp`, `InitDevice()`:

- `ZeroMemory` both descriptors before use, and assign `descDSView.Flags = 0`
  explicitly with a comment saying why it must not be `D3D11_DSV_READ_ONLY_DEPTH`.
- `FAILED()` checks on `CreateTexture2D` and `CreateDepthStencilView`, so a failure
  here aborts device init loudly instead of quietly producing a depth-less renderer.
- `CleanupDevice()` now releases `g_pDepthStencilView` and `g_pDepthStencilBuffer`,
  which it never did.

x64 host file only. No shared or console-platform code touched.

## Note: this is almost certainly not a regression

The owner reported it as a side effect of the atlas-slicing work. It is not. That was
checked first and ruled out:

- commit `3b0c0f39` changed no C++ — `Textures.cpp:238-239` still constructs
  `PreStitchedTextureMap`;
- the 462 sliced PNGs are inert. The only code reading `textures/blocks/*.png`
  individually is the `texturesToAnimate` loop in `PreStitchedTextureMap.cpp:130`, and
  that list is hardcoded (compass, clock, portal, fire, water, lava) — exactly the
  files the slicer skipped;
- every copy of `terrain.png` / `items.png` / the mip levels is byte-identical across
  the source tree, both staged output trees and the console media directories;
- the atlas actually loaded (`Common\res\TitleUpdate\res\terrain.png`, via
  `TexturePack::getPath(true)`) is the same file the slicer parsed `loadUVs()` against,
  so there is no UV-table/atlas mismatch.

The likelier explanation for the timing is that this bug has been in the Windows64
host all along and only became *visible* once
[keyboard and mouse input](2026-08-26-windows-keyboard-mouse-input.md) let the owner
move and look around. Before that there was no way to see anything but the spawn view.

## Verified

- `MinecraftPC.sln` `Release|x64` builds clean, exe produced and staged.
- **Confirmed fixed at runtime by the owner (2026-08-26).** Depth now works; distant
  and underground geometry no longer paints over the surface.

This also settles what was an open question while the fix was being written: the
app-created DSV *is* the depth buffer the world renders against.
`C4JRender::Initialise(device, swapChain)` does not make its own - it uses the targets
bound by `InitDevice()`'s `OMSetRenderTargets`. That could not be established by
reading the tree, because `4J_Render_PC.lib` is prebuilt and its D3D calls are COM
vtable dispatches that never reach the symbol table. Fixing the descriptor and seeing
depth start working is what proved it. Worth knowing for anyone touching render
targets here later.

## Watch out

`RenderManager.Initialise(g_pd3dDevice, g_pSwapChain)` is called **twice** — once at
the end of `InitDevice()` and again at `Windows64_Minecraft.cpp:809`. That was left
alone because the library's re-entrancy behaviour is unknown, but it is suspicious and
worth a look if depth or render-target problems persist.
