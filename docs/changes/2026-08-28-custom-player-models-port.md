# 2026-08-28 — Port CustomPlayerModels (geometry, animation, model sync)

Ports the loading, rendering, animation and network-sharing parts of
[CustomPlayerModels](https://github.com/tom5454/CustomPlayerModels) from Java to this
tree. Players can wear `.cpmmodel` files and everyone on the server sees everyone
else's model.

Full subsystem write-up: [systems/custom-player-models.md](../systems/custom-player-models.md).

## Why not a complete port

The mod's core module is ~46k lines of Java. Roughly 25k of that is an in-game 3D
model editor built on the mod's own GUI toolkit (`cpl/gui`), which has no path onto
LCE's Iggy/Flash UI stack — porting it is a rewrite, not a port, and it was explicitly
excluded. Models are authored in the Java/desktop CPM editor or Blockbench instead.

Gestures, parameter/staged triggers, cape/elytra/armour roots, render effects,
templates, texture stitching and the HTTP model loaders are also not ported. Their
part types are *skipped* rather than rejected, which is the format's own
forward-compatibility rule, so models using them still load and render what they can.

## What changed

New, all platform-independent except `CPMFiles.cpp`:

- `Minecraft.Client/Common/CPM/CPMIO.{h,cpp}` — byte-exact `IOHelper` port
- `Minecraft.Client/Common/CPM/CPMModel.{h,cpp}` — cube tree + part-stream parser
- `Minecraft.Client/Common/CPM/CPMAnim.{h,cpp}` — animations and all 7 interpolators
- `Minecraft.Client/Common/CPM/CPMRender.{h,cpp}` — `BoxRender` port onto `Tesselator`
- `Minecraft.Client/Common/CPM/CPMManager.{h,cpp}` — registry, pose detection, `/cpm`
- `Minecraft.Client/Common/CPM/CPMNet.{h,cpp}` — chunked sync over `CustomPayloadPacket`
- `Minecraft.Client/Common/CPM/CPMFiles.{h,cpp}` — model folder access; console stub

Modified:

- `HumanoidModel.h/.cpp` — added `cpmEnabled`; `render` now asks `CPMManager` per bone
  and feeds the answer into the existing `bHideParentBodyPart` argument
- `PlayerRenderer.cpp` — sets `cpmEnabled` on the main model only (not armour layers)
- `MultiPlayerLocalPlayer.cpp` — `chat` intercepts `/cpm`
- `ClientConnection.cpp` — consumes the `CPM|Data` channel; announces own model on login
- `PlayerConnection.cpp` — validates and relays models; join-syncs new players
- `Minecraft.cpp` — `CPMManager::tick()` advances the animation clock
- `Minecraft.Client.vcxproj` — the seven new pairs, unconditional in all configs

## Verification

**Built:** `MinecraftPC.sln` `Release|x64` on VS2022, exit 0, no new warnings (the
`vc110.pdb` LNK4099 is pre-existing). A clean baseline build was taken first to
confirm the tree already built.

**Parser tested against an independent implementation.** No `.cpmmodel` samples ship
with the reference repo, so `.cpmmodel` encoding was transcribed *separately* from the
Java writers into a Python generator, and the C++ reader was checked against its
output — not against itself. Verified: container + both checksums, cube tree
parenting across roots and cube-to-cube, `ROOT_INFO` hiding, degree→radian conversion
(30° → 0.5236, 45° → 0.7853), coloured vs textured cubes, PNG texture extraction,
animation trigger→pose resolution, channel index → (cube, channel kind) mapping,
and interpolator output including the `1/127` fixed-point quantisation (0.5 → 0.4961).
Linear and spline interpolators were compared off-keyframe and differ correctly
(0.2480 vs 0.3410 at the midpoint), with the spline passing through keyframes.

**Fuzzed:** 400 corrupted/truncated model files — 400 rejected, 0 crashes, 0 accepted.

## Not verified

**Nothing has been run in the game.** No model has actually been rendered, no
animation seen, and no client-server sync exercised against a live server. Everything
above is static: it compiles, and the parser matches an independent reading of the
spec. Whether models *look* right on screen is unconfirmed.

Specific things most likely to be wrong on first run, in rough order of risk:

1. **Bone-space alignment.** `CPMManager::renderPart` reproduces `ModelPart::render`'s
   transform by hand. If a model's parts sit at the right *scale* but the wrong
   *place*, this is the first suspect.
2. **UV orientation.** `BoxRender`'s quad winding was mapped onto `_Polygon`'s
   `(u1,v0),(u0,v0),(u0,v1),(u1,v1)` order. Mirrored or upside-down faces point here.
3. **Pose detection.** Approximate; see the systems doc. Wrong-animation-playing is
   more likely than no-animation.
4. **Texture binding.** `CPMManager::bindActiveTexture` binds before the vanilla parts
   draw, on the assumption a CPM model's embedded sheet is a full replacement skin.
5. **Packet sizing.** Chunking is untested against a real connection; 8000-byte chunks
   were chosen to sit well inside `CustomPayloadPacket`'s 16-bit length, but LCE's
   per-tick packet budget was not investigated.

## Watch out for

- **The `Tesselator` ignores `glColor`.** With `hasColor` false it forces every vertex
  to white in `end()`. Tinting must go through `Tesselator::color`. This is not
  CPM-specific and is worth remembering elsewhere.
- **`BufferedImage(BYTE*, DWORD)` kills the process** via `app.FatalLoadError()` on a
  decode failure. Since model data is untrusted, `CPMManager` validates the PNG header
  first. Any other path that feeds network data to `BufferedImage` has the same hazard.
- **Texture ids leak** when a player switches models; LCE has no per-texture free.
- **7th gen:** the new files compile in all configurations and contain no
  `#ifdef _WINDOWS64` outside `CPMFiles.cpp`, but console targets cannot be built here,
  so this is unverified for them. `CPMFiles.cpp`'s non-Windows branch is a stub, so on
  a console the folder is empty and local model loading is simply unavailable —
  network-received models would still work.

## Provenance

CustomPlayerModels is MIT-licensed (`References/CustomPlayerModels/LICENSE`). This is
a derivative port; no upstream attribution or license file has been added to the
repository, since adding license files was not requested.

---

## Follow-up 2026-08-28: V1 (`DEFINITION`) models did not load

Reported from testing: `/cpm list` found the model, `/cpm set` refused it.

**The bug.** The port implemented only the V2 part stream (`CUBES` / `TEXTURE` /
`ROOT_INFO`). The CPM editor writes V2 **only** when the `EDITOR_EXPERIMENTAL_EXPORT`
config flag is set; by default `Exporter.prepareExport` calls `prepareDefinition`,
which produces a V1 `ModelPartDefinition` with the cubes and every other part nested
*inside* that one block. Practically every real model is therefore V1, and the parser
skipped the whole thing — loading a model with zero cubes and no texture.

Reproduced locally by extending the spec-derived Python generator to emit a realistic
default export (`SKIN_TYPE` + `DEFINITION{V1 cubes, PLAYER, SKIN, END}` + `END`, with
a real icon): before the fix the container read fine and the model came out with
0 cubes; after, 3 cubes, the texture, and the correct hidden root.

**Fixed** by recursing one level into `DEFINITION` and adding the V1 parts:
`Cube.loadDefinitionCube` (fixed layout, single-byte `u`/`v`), `PLAYER` (vanilla-bone
keep bitmask), `SKIN` (bare `TextureProvider`), `PLAYER_PARTPOS`, and the
`RENDER_EFFECT` types that carry data V1 cubes lack — `HIDE`, `SCALE`, `COLOR` and
`UV_OVERFLOW`. Effects reference cubes by id and are applied after parsing.

Re-fuzzed: 600 corrupted/truncated files across both encodings, 600 rejected, 0
crashes. The V2 path is unregressed.

### Honesty about the reported error

This fix is definitely correct and definitely necessary, but it does **not** by itself
explain the exact message that was reported. A V1 model with the old code loaded
"successfully" with nothing in it; it would not have produced a container error.

The container reader was verified against a realistic file and works, so the most
likely remaining cause is a model whose data spilled into an external `Link` — which
the editor produces whenever the export exceeds its buffer (only **2KB** with "skin
compatible" ticked, 30KB otherwise). That path was rejected with a generic message.

`CPMLoadModelFile` now returns a specific reason for every failure, including a
Link-specific one that names the fix, so the next attempt is diagnostic rather than
another round of guessing.

### Also worth knowing

V1 models carry animations as the legacy `ANIMATION_DATA` part, a different encoding
from the ported `ANIMATION_NEW`. So a default-exported model **renders but does not
animate**. Exporting with the editor's experimental (V2) export enables animation.

## Follow-up 2: the file was not an exported model

Retest reported "bad magic byte" — the first byte was not `0x53`, so the file was
never a `.cpmmodel` in the first place. That also explains the original report, which
the V1 fix could not: the container was failing before it read anything.

The likely culprit is a **`.cpmproject`**, the CPM editor's working format. It is a
ZIP (`PK`), not a model, and renaming one to `.cpmmodel` is exactly how it
would end up listed by `/cpm list` and then rejected by `/cpm set`.

Rather than report a hex byte, `CPMIdentifyFile` now names the format and says what
to do:

| Magic | Reported as |
|---|---|
| `PK` | a CPM editor project — export it via Export > Model |
| `PNG` | a skin PNG — skin-embedded models are not ported |
| `{` or `[` | JSON (Blockbench `.bbmodel` or similar) |
| gzip | gzip compressed |
| anything else | the actual first byte, and the expected one |

Verified against real files: the vendored `examples/animated_texture.cpmproject`,
a real PNG, a JSON file, and a valid V1 model — each identified correctly.

`/cpm list` now checks each file's magic byte too, so a project file or skin sitting
in the folder is flagged in the listing instead of only failing on selection.

**Note for future debugging:** `.cpmproject` (ZIP, editor working format) and
`.cpmmodel` (the `0x53` container) are entirely different formats. Only the latter is
readable here, and porting the project format is not planned — it is the editor's
save file, not a distributable model.

## Follow-up 3: load `.cpmproject` directly

The owner had a `.cpmproject` and no exported model. Rather than require Minecraft
Java plus the CPM mod purely to run Export > Model, the editor's working format is now
read directly.

`.cpmproject` is a ZIP of `config.json`, `skin.png` and optionally `animations/*.json`.
`CPMLoadProject` reads it into the **same** `CPMModelDefinition` cube tree the binary
loader builds — a parallel front end, not a layer on top; no part of the binary format
is involved.

New:

- `CPMZip.{h,cpp}` — minimal in-memory ZIP reader on the client's existing zlib
- `CPMJson.{h,cpp}` — small read-only JSON parser
- `CPMProject.{h,cpp}` — `config.json` → cube tree

`CPMManager::loadFromBytes` dispatches on magic bytes (`PK\x03\x04` vs `0x53`);
`CPMFiles` lists and opens both extensions, preferring an export when both exist since
only that carries animations. `CPMModelDefinition::prepareRoots` / `buildTree` were
made public so both front ends share the tree building.

### Things that had to be right

- Java's `ZipOutputStream` sets the data-descriptor flag, so **local headers carry a
  zero compressed size**. The central directory has to be used instead — reading local
  headers would have produced empty entries.
- Zip entries are raw deflate: zlib needs `inflateInit2(..., -MAX_WBITS)`.
- In `config.json`, `scale` is the *mesh* scale and `rscale` is the *render* scale —
  the opposite of what the names suggest. Rotations are degrees. `color` is a hex
  string. On a child, `show` is the editor's visibility toggle and `hidden` is the
  real one; on a root, `show` inverted is what hides the vanilla bone.
- `skinSize` is often not 64x64 — both vendored example projects are 128 wide. Reading
  it rather than assuming is what makes their UVs land correctly.

### Verification

Tested against the two **real** `.cpmproject` files vendored in the reference repo:
8 cubes / 128x64 and 119 cubes / 128x128, textures extracted, roots and hidden flags
correct.

Fuzzed at 600 corrupted/truncated projects: **0 crashes**. Entry CRCs are now verified
— deflate tolerates a lot of corruption without failing, so without the CRC a damaged
archive could yield a garbage model. The mutations still accepted are ones landing in
the `animations/` entries, which are never read; that is correct, not a miss.

### Limits

- Project **animations are not read**. They are per-animation JSON files under
  `animations/`, a third encoding after `ANIMATION_NEW` and `ANIMATION_DATA`. Geometry,
  texture and hidden parts all work.
- Duplicated roots and custom (cape / elytra / armour) parts are skipped, as in the
  binary path.
- Projects sync over the network like any other model — the raw file is what is sent,
  and the receiving client dispatches on magic bytes the same way. Project files are
  larger than exports, but well inside the 1MB cap.

## Follow-up 4: first-person hand uses the model's arm

Reported from testing: the third-person model was right, but the first-person hand was
still Steve.

`PlayerRenderer::renderHand` drew `humanoidModel->arm0` directly and knew nothing
about CPM. It now runs the same three-step sequence as the third-person path —
`beginEntity`, `bindActiveTexture`, `renderPart(CPM_PP_RIGHT_ARM, arm0, 1/16)` — and
suppresses the vanilla arm when the model hides that root.

`arm0` is the right arm (its pivot is set to `-5` in `HumanoidModel::_init`, matching
`PlayerPartValues.RIGHT_ARM`), and it is the arm LCE shows in first person, so no
handedness choice is involved.

Two details that make this work rather than fight the existing code:

- `setupAnim` has already positioned `arm0` for the hand pose before the CPM call, and
  `renderPart` derives the cube transform from that same bone, so custom cubes track
  the vanilla arm exactly.
- `ItemInHandRenderer` binds the player skin and calls `clearLastBoundId()`
  immediately before `renderHand`, so rebinding the CPM texture inside actually takes
  effect; the held item rebinds its own texture afterwards, so nothing leaks.

`renderHand` calls `arm0->render` directly rather than `HumanoidModel::render`, so the
`cpmEnabled` path in the latter is not involved and there is no double draw.

**Built, not run.** If the hand is in the wrong place rather than absent, the transform
in `CPMManager::renderPart` is the thing to look at; if it renders but untextured, the
bind ordering above is.

## Follow-up 5: UV and pivot fixes

Reported from testing: UVs wrong on some cubes, and the head not pivoting from its
base. Three genuine discrepancies against the reference, found by re-reading
`BoxRender`, `ModelRenderManager.createBox` and `RootModelElement`.

**1. `singleTex` was not implemented — the likely UV bug.** A "single texture" cube
maps one square UV patch onto every face rather than the unwrapped box layout
(`BoxRender.createTexturedSingle`). It is not a rare option: **82 of the 119 cubes**
in the vendored `animation_status_test.cpmproject` use it. Cubes with it were being
drawn with the ordinary box UV, so their texture was wrong. Now implemented, and the
flag is read from both front ends — `singleTex` in project JSON, and the
`SINGLE_TEX` render effect in the binary format.

**2. Coloured cubes lost their mesh scale.** `BoxRender.createColored` appears to
ignore `meshScale`, and the port followed that reading — but the caller,
`ModelRenderManager.createBox`, passes it `c.size * c.meshScale`. Every scaled
coloured cube was therefore rendered at its unscaled size.

**3. Root transforms were composed instead of combined.** `RootModelElement` **adds**
the root's own offset and the animated delta to the vanilla bone's pivot and rotation,
then applies one translate and one rotate. The port was doing translate(bone),
rotate(bone), translate(root), rotate(root) — which rotates about the bone pivot and
only then shifts, putting the pivot in the wrong place. Also fixed the matching
`reset` semantics: a root's `pos`/`rotation` are the *animated delta* and zero each
frame, with the static offset in `cube.pos`/`cube.rotation`.

### Honest scope of the pivot fix

Fix 3 is a real discrepancy and is now faithful to the reference, but **both vendored
example projects have all root offsets at zero**, so it only changes models that
actually move a root. If the reported head pivot persists on a model whose head root
is not offset, the cause is something else and more information is needed — the
vanilla LCE head pivot itself is correct (`head->setPos(0, 0, 0)` with the box at
`-4,-8,-4`, i.e. rotating about its base, matching `PlayerPartValues.HEAD`).

Note also that a cube's own `pos` is its pivot and `offset` places the mesh relative
to it; that was already handled. Bone rotation always pivots at the bone origin,
which is what the reference does too.

### Verification

Built; both example projects still load (8 cubes / 128x64 and 119 / 128x128) with
`singleTex` now counted on 2 and 82 cubes respectively. Not run in the game.

Still unported and a possible cause of remaining UV oddities: per-face UV (`faceUV`)
and `extrude` cubes, which currently fall back to the standard box layout.

## Follow-up 6: per-face UV and extruded cubes

The remaining UV problems were these two unported cube variants, as suspected.

**Per-face UV.** Each face carries its own UV rectangle and a quarter-turn, and —
the part that matters most — **a face absent from the map is not drawn at all**.
Deleting a face in the editor removes it from the map, so falling back to the box
layout drew all six faces with the wrong UVs. One cube in the vendored
`animation_status_test` project has only `east` and `west`; the other four are gone.

Corner order follows `PerFaceUV.Face.getVertexU/V`: rotate the vertex index by
`(index + rot + 3) % 4`, then pick `sx`/`ex` and `sy`/`ey`. Verified against the real
project: for `rot=0` and `sx,sy,ex,ey = 5,16,8,19` the corners come out
`(8,16) (5,16) (5,19) (8,19)`, matching the standard quad mapping.

**Extrude.** A flat quad given real thickness, with a comb of side faces one per
texel so the silhouette follows the texture. The reference builds it in a unit space
(x,y in [0,1], z in [-1,0]) and maps it in with a matrix; this port applies the same
mapping per vertex, since the caller already scales positions. Normals only need the
sign of the scale, being axis-aligned.

Both are read from **both** front ends: `faceUV` / `extrude` in project JSON, and the
`PER_FACE_UV` / `EXTRUDE` render effects in the binary format. `PerFaceUV.readFaces`
uses a presence bitmask in `Direction` order (UP, DOWN, NORTH, SOUTH, EAST, WEST),
followed only by the faces that are present.

### Verification

Per-face UV parses correctly out of the real example project (3 cubes, with the
vertex-rotation output checked by hand as above). Re-fuzzed at 600 corrupted projects:
0 crashes.

**Extrude is unverified against real data** — neither vendored example uses it, so it
is implemented faithfully from the reference but has never been exercised on an actual
extruded model. It is also the one path that generates unbounded geometry, so the comb
is capped at 256 texels per edge.

## Follow-up 7: the vanilla hat layer was still drawing

Reported from testing: a second head, wearing the model's texture, sitting over the
custom one.

That is `hair` - LCE's hat/second layer. It is a separate `ModelPart` on the head
pivot with the same box inflated by 0.5 and UV (32,0), rendered inline at the end of
`HumanoidModel::render`, and the CPM path never touched it. Once the CPM texture is
bound it picks that up, so it reads as a slightly oversized duplicate head.

The reference is explicit about this. `RedirectHolderPlayer` registers the hat with a
**null** part and `setCopyFrom(head)`, and `ModelRenderManager.render` does:

```java
if(part == null) {
    if(dh.copyFrom != null) holder.copyModel((P) dh.copyFrom, tp);
    return;                     // draws nothing
}
```

The whole block is gated on `holder.def != null`, so with any model loaded the hat
draws nothing at all - **not** merely when the head root is hidden. `hideHair` is now
forced true whenever CPM is active, matching that.

Checked while here: `renderHair` is dead code (declared and defined, never called);
`renderEars`/`renderCloak` are separate conditional paths left vanilla, since the cape
is a `RootModelType` this port does not redirect; and `HumanoidModel`'s `young` branch
has no CPM path, which is correct because players are never `young`. The armour
layers are unaffected - they have `cpmEnabled` false, so `hideHair` keeps its original
value there.

Built, not run.
