# Development log

Reverse-engineering notes for Soulbringer Modern. Every address, what it does, and how it was confirmed. All addresses are virtual addresses in the process (image base `0x400000` for the exe, `0x10000000` for the renderer DLL).

Target build: the GOG release, which is also what Steam ships.

- `SoulbringeVCnoeax.exe` SHA-256 `4816ee96…c304` (no-EAX build, the one the GOG launcher starts with `EAX=0`)
- `SoulbringeVC.exe` SHA-256 `57c2d217…75e9` (EAX build; different binary, but the patched function and globals are identical, verified by diffing the disassembly)

---

## 2026-11-19: Survey of the install

**Process chain**

- `Soulbringer.exe` (200 KB): a small launcher from 2000. It reads `data\devopt.ini` and the GOG `LauncherConfig.ini`, enumerates renderers by calling `Detect` in them, then starts `SoulbringeVC.exe` or `SoulbringeVCnoeax.exe`.
- `patch.dll`: GOG's 2012 compatibility layer. The game exe's import table was edited so it imports DirectDraw from `PATCH.DLL` instead of `DDRAW.DLL`. The renderer DLL also imports from `PATCH.dll`. It reads `...\GOG\SoulBringer\Config.ini` (only reads, never writes; defaults when the file is absent). Options seen in its strings: `ScaleMode`, `WindowWidth`/`WindowHeight`, `FrameLength`, `Windowed`, `ReverseMouse`, `MouseCamera`, `WrapInput`, `SkipIntro`, `SkipAiFix`, `SkipAiFix2`. It also touches `binkw32.dll` (`data\BINKW32.DLL_org` backup exists), `data\ai.dll`, WINMM and DirectInput.
- `ddraw.dll`: DDrawCompat v0.7.2, the user's addition. `patch.dll` forwards to it.

**Launcher config** (`GOG\SoulBringer\LauncherConfig.ini`): `Device=6`, `GraphicsMode=27` (an index into the renderer's mode table; the same value is stored in `data\devopt.ini`), `EAX=0`, `TexelCenter=0`.

**Renderer plug-ins**: `data\Stealth\Render\` has `D3d6Lib.dll`, `D3d7Lib.dll` and `sft8lib.dll` (software). A byte-identical `D3d7Lib.dll` also sits in the game root, and **that root copy is the one the game loads**: `LoadLibrary("D3d7Lib.dll")` searches the exe folder first. The exe also has name slots for `D3d8Lib`, `OGLLib` and `Pvr1Lib`, which were never shipped.

**CPU math DLLs** (`base`, `pentium`, `katmai`, `3DNow`): generic vector and matrix math only (`PM_MatMult`, `PS_Normalise`…). No projection code.

## 2026-11-19: Renderer interface

- Loader at `0x694D50` resolves the 76 exports by name with `GetProcAddress` into a function table inside the renderer object. The object pointer lives at **`[0x35767D4]`**. Call sites look like `mov eax,[0x35767D4]; call [eax+offset]`. The full table is in [RENDERER_API.md](RENDERER_API.md).
- **The game never calls `SetViewToScreenTransform`, `SetWorldToViewTransform` or `SetLocalToWorldTransform`.** All 3D transformation and projection is done on the CPU in the exe. The renderer only receives screen-space vertices, so widescreen can't be fixed by changing a matrix at the API boundary.
- `AddTri(bucket, material, nTris, ptr)` returns a pointer to `3*nTris` vertices that the game then fills. `CommitTri` submits them. The vertex layout is `D3DFVF_XYZRHW|DIFFUSE|SPECULAR|TEX1`, 32 bytes: `x, y, z, rhw, diffuse, specular, u, v`. Screen y is written as `screenH - y`.
- All exports are `cdecl`.

## 2026-11-19: Screen globals and mode selection

- `0x367370C` = screen width, `0x3673708` = screen height (int). Both are set at `0x597481`/`0x597494` from the renderer's mode table (`renderer->+0xC->+0xAC`, entries of `0x3C` bytes, `+0` width, `+4` height). The two globals are referenced about 2,400 times.
- `0x5972D9` calls `OpenMode`. The fallback mode picker prefers a 16-bit 640×400 mode. The selected mode index is at `0x6B86D4`.

## 2026-11-20: Logging proxy (proxy builds v1 to v2)

The proxy logs each renderer call together with its caller address, and the screen-space bounding box of the vertices written after each `Add*` call.

- v1 lost its log: the game loads the renderer twice (once for `Detect`, then for real), and the second load couldn't reopen a log file that hadn't been closed. Fixed in v2 by appending and closing the file on `DLL_PROCESS_DETACH`.
- **Mode table on the test PC:** 28 modes. GOG's patch exposes modern resolutions up to 3840×2160. `GraphicsMode=27` is **3840×2160×32**, and the exe globals read `width=3840 height=2160`. **The game already renders at native resolution.** The stretching comes purely from the projection.
- About 63 FPS (314 frames per 5 s). The frame boundary is `StartScene`/`StartFrame` … `EndFrame`/`EndScene`. `FlipScene` is not called.

**2D draw sources** (vertices with `z=0, rhw=1`):

| Caller (return address) | What it draws |
|---|---|
| `0x004B686A` | 8 large tiles covering 0..3840 × 0..2160: full-screen overlay art (HUD frame in game, backgrounds in menus) |
| `0x004A9195` | Panels and icons, x 165..3670, mostly in the lower half: HUD panels |
| `0x0042B0BC` | Many tiny quads; in game a column at x≈3490..3552: the font/text renderer |
| `0x00456ABD` | A 38×28 sprite that moves in menus: the mouse cursor |
| `0x0053E5BB` | Small element, rhw 0.33, menus only |

**3D world draw routines** (they copy already-projected vertices): `0x0042C76B`, `0x0043B4F1`, `0x0048EA4B`, `0x0048F2D8`, `0x0050E8F1`, `0x0050ED97`, `0x0050EFE6`. Also `0x0041E4F5`, `0x0048ECAB`, `0x0048EEEB`, `0x0055950E` (effects, particles, sky).

## 2026-11-20: Projection, and the widescreen fix

Tracing back from the world routines (`0x50EF70` reads projected records with stride `0x18`: x, y, depth) leads to the terrain transform at `0x50F7xx`/`0x50FA80`:

```
x_screen = (row0 · v + 0x2092EF0) * p + cx      ; row0 = 0x2092EE4..EEC, cx = [0x2A98704]
y_screen = (row1 · v + 0x2092F00) * p + cy      ; row1 = 0x2092EF4..EFC, cy = [0x2A9870C]
depth    =  row2 · v + 0x2092F10                ; row2 = 0x2092F04..F0C
```

The camera matrix at `0x2092EE4` is built at `0x67C920`. At `0x67CBE8` it **multiplies row 0 by `[0x2A98708]` (xscale) and row 1 by `[0x2A98710]` (yscale)**. The screen width is passed to the camera setup as the perspective numerator (copy at `0x3575F48`).

**Where xscale and yscale come from: `0x4DC540`**

```
cx     = W/2                                   (0x4DC571; W/2 - W/12 in one mode)
A      = (1/3) / (W * 0.25)          = 4/(3W)  (constants 0x3686688, 0x3686690)
xscale = 0.8 * zoom                            (0x4DC5CD: fld [0x36866A8]=0.8; fmul [0x6B83FC])
yscale = H * A * 0.8 * zoomY * -1.0  = -0.8 * 4H/(3W)
cy     = H/2 - H/16
```

Because the numerator is W, the horizontal pixel scale is proportional to W, while the vertical one is proportional to W × 4H/3W = 4H/3. On a 4:3 screen the two match. On anything wider, horizontal FOV and vertical FOV are both fixed, so the picture is stretched horizontally.

**Fix (0.1.0):** at `0x4DC5CD`, replace the 12 bytes

```
D9 05 A8 66 68 03     fld  dword [0x036866A8]   ; 0.8
D8 0D FC 83 6B 00     fmul dword [0x006B83FC]   ; zoom
```

with `call ws_xscale` + 7×`nop`. `ws_xscale` computes `H * (0.8*4/3) / W * zoom` (`fild [0x3673708]; fmul [k]; fidiv [0x367370C]; fmul [0x6B83FC]`), which leaves one value on the FPU stack, exactly like the original. Result: xscale = yscale magnitude, square pixels, constant vertical FOV, and a horizontal FOV that grows with the aspect ratio (Hor+).

**Other readers of xscale and yscale:** only the matrix builder (`0x67CBEE…`) and `0x5842A4`, which is an inverse projection (screen to world, used for mouse picking). Both stay consistent with the patch.

**Play test (16:9, 3840×2160):** world proportions are correct. No pop-in at the screen edges was observed; the game's fog of war also hides distant objects. Mouse picking feels unchanged. Cutscenes still play pillarboxed at 4:3.

**Open questions**

- Culling: no problems seen yet, but it hasn't been confirmed in code. Watch for objects popping in at the left and right edges, especially on 21:9.
- `0x4DC640`: when the flag at `0x357DBA4` is set, both scales are multiplied by 0.9375. The mode this belongs to is unknown; it's consistent with the patch either way.

## 2026-11-20: Frame rate and audio notes

- **Frame rate:** the game reportedly runs too fast above 60 FPS (community reports, not yet checked in code). DDrawCompat is a requirement and has its own limiter (`FpsLimiter`), so capping is left to it and isn't done in this mod. During logging, DDrawCompat's log showed `FpsLimiter = off` and `VSync = app`, yet the game ran at about 63 FPS (314 frames per 5 s). Something may already be limiting it, possibly the game itself or GOG's `FrameLength` option. Still open.
- **EAX:** the install ships EAX 2.0 region data (`data\eax*.dat`). `SoulbringeVC.exe` imports `EAX.DLL` (Creative, not shipped) and DirectDraw directly, without `patch.dll`. `SoulbringeVCnoeax.exe` still queries `IKsPropertySet` (`0x6B6358`) on its sound buffers and sets EAX 2.0 listener and buffer properties (property-set IDs at `0x36AA4A4`/`0x36AA4B4`, setters around `0x698030`). With a DirectSound that supports EAX (DSOAL), this path may switch on without any patching. Scheduled last; see ROADMAP 0.5.

## 2026-11-21: Renderer reload bug, UI frame captures

**Bug in 0.1.0 (fixed in 0.1.1):** the renderer DLL is loaded, freed and loaded again (launcher-style `Detect`, then real use, in the same process). The widescreen patch from the first instance stayed in the exe and called into freed memory. It worked by luck because the DLL was reloaded at the same base. Fix: restore the original bytes on `DLL_PROCESS_DETACH` when `lpReserved == NULL` (FreeLibrary), and accept our own `E8 rel32 + 7×NOP` signature when patching.

**F9 frame captures (3840×2160)**, all coordinates on the 640×480 canvas:

| Screen | Content |
|---|---|
| Main menu | 6 background tiles (`0x004B686A`, 213.33×240 each, materials `0x66`–`0x6B`); the buttons are baked into the background. Text plus cursor only. |
| Options | Same 6 tiles plus sprites via `0x004A9195` (sliders, frames). |
| Gameplay | 3D world (depth > 0), **right column** = `0x004B686A` material `0x49` (x 536.67–640, y 0–420, 2 tiles) plus material `0x4B` (weapon box); **bottom bar** = `0x004B686A` material `0x4A` (x 0–640, y 420–480, 3 tiles), `0x4D` (health/mana bars, y 431–471), `0x004A9195` material `0x4A` (bar ornaments y 406–420), material `0x59` (left bulb). Column labels and action slots are text. |
| Inventory / character / other menus | The same 6 full-screen tiles (texture handles reused per screen) plus sprites. The bottom bar is part of the background art, with its bulbs and bars drawn on top. |
| Conversation | HUD hidden; full-screen overlay (`0x004B686A` material `0x4D`, 0–640×0–480), portrait panel `0x004A9195` material `0x6E` (x 470–614, y 19–163), wrapped text. |

**Mouse cursor:** `0x004A9195` material `0x52`, a 32×32 px sprite drawn 1:1 (not scaled with resolution).

**UI 3D models** (inventory items, weapon icon in the right column): drawn by `0x0048EA4B` with **z = 0 and rhw ≈ 0.333**, laid out stretched like the 2D UI. The world always has z > 0. So depth 0 identifies them as UI.

**Text:** `0x0042B0BC` sits inside `0x42B020`, which flushes a batch of up to 10 already laid-out glyphs per font material (`0x4E`, `0x4F`, `0x50`, `0x51`). Glyphs are drawn at 1:1 font pixels (6–14 px tall at 2160p), so text gets smaller as resolution rises. Line spacing is 20 px, and wrapping is done at the current pixel width. Scaling afterwards would overflow panels, so the font scale has to go into the string layout code (to find; it fills the glyph records at `[batch+0x14…]` that `0x42B020` reads).

**Renderer:** `CommitTri` only adds to the vertex count of the bucket; vertices are read at `RenderAllBuffers` or `EndFrame`. The proxy can therefore adjust UI vertices in place at any point before those calls.

## 2026-11-21: UI layout step 1, and automated test data

**Layout (0.2.0-dev):** UI vertices are re-placed in the proxy at `RenderAllBuffers`/`EndFrame`/`EndScene`, using the rules in `src/ui_layout.inc`. Full-screen menus (detected by the 6-tile background) are pillarboxed. In gameplay, the right column (canvas x ≥ 536.67, y < 420) is anchored to the right edge, and the bottom bar (canvas y ≥ 405) is placed bottom-center, both only while their tiles are on screen. Sprites, glyphs and the cursor are moved without changing their shape ("rigid"); canvas-sized art is mapped per vertex ("stretched"). UI 3D models arrive one triangle per `AddTri`: touching triangles with the same material are grouped and moved together.

**First in-game test failed, and why:** the UI was classified as world because the check was "depth == 0". UI depth is tiny but not 0; the log's 3 decimals hid it. **World vs UI is now decided by rhw:** world < 0.002, UI models ≈ 0.33, flat UI ≈ 1.0. The replay test now uses realistic values to catch this kind of mistake.

**Automated logging (Log=1):**
- One log per session: `SoulbringerModern_<exe>_<pid>.log`, with date and time.
- **Screen fingerprint** per frame: hash of the set of (caller, material) pairs of UI drawn, plus the menu flag. When it changes and stays the same for 8 frames, `event: screen XXXXXXXX stable` is logged and, the first time a fingerprint is seen, the next frame is captured automatically (`AutoCapture=1`).
- Captures now include the **layout decision** per buffer (`M` menu, `B` bottom bar, `R` right column, `-` untouched; `S` stretched, `G` rigid, `C` cursor, `D` 3D model) and the bounds after layout.
- `event: click L/R` with OS cursor position, the game's cursor (sprite position before layout) and where it was drawn. Groundwork for HUD click mapping.
- `event: mode menu -> game` transitions.
- Statistics windows every 30 s instead of 5 s.

**Tests:** `tests/replay/replay.py` replays every recorded capture through `src/ui_layout.inc` (built natively) and checks the placement rules. `tools/sbm_log.py` summarises a session log and draws before/after pictures.

## 2026-11-21: Gameplay detected as menu (fixed)

**Play test:** menus, inventory, character and journal were pillarboxed correctly, but in gameplay the HUD sat in the centered 4:3 position, and the cursor stopped at the 4:3 edges.

**Cause, from the automatic session log:** `game frames 0`; every frame was decided "menu". Gameplay frames draw and flush the HUD *before* the world, so at the first flush there was no world evidence yet, and the decision fell back to the previous frame's mode. After the main menu that was "menu", and it then repeated forever. The single-frame replay test could not see this, because the state comes from the previous frame.

**Fix:** menu = "the full-screen menu background was drawn in this frame". Menus always draw it first (before the first flush), and gameplay never draws it. No carry-over from the previous frame, so there is no one-frame lag either. Also: the gameplay cursor is never anchored to a HUD part, so it always follows the real mouse (it was being pulled along when over the bottom bar or the column).

**Test change:** `tests/replay/replay.py` now replays each session's captures in order through one harness run, so state that carries over between frames is tested. New checks: the gameplay cursor must not move. The old code fails the new session test exactly as seen in game; the fixed code passes all 24 recorded screens.

## 2026-11-21: Split HUD confirmed in game; tooltips; HUD hover offset measured

**Play test:** the in-game HUD is split correctly (bottom bar centered, column at the right edge), and menus and gameplay are detected correctly (`event: mode` lines follow every screen change).

**Tooltips:** `0x00456ABD` draws the small hover/tooltip box next to the cursor (about +17 px right of the cursor). Over the HUD, the layout rules pulled it (and its text) up to about 100 px away from the cursor. Fix: in gameplay, `0x00456ABD` boxes and glyphs inside them are never anchored, so they stay with the cursor. A replay check enforces this.

**HUD hover/click offset (step 3, open):** the game still hit-tests its HUD buttons at their original stretched positions. Offset between a drawn button and where the game expects it, at 3840×2160:
- bottom bar: `480 − 0.25·x` px (+480 at the left end, about +300 at the inventory button, 0 at the center, −480 at the right end)
- right column: `0.25·(3840 − x)` px (+155 at its left edge, 0 at the screen edge)

The game's cursor is not the OS cursor (`event: click` shows them differing), so the fix has to give the game's HUD hit-testing an inverse-mapped cursor position while the world keeps the real one.

## 2026-11-21: HudLayout setting

`[UI] HudLayout = split | right | center | stretched` (read with `GetPrivateProfileStringA`, first letters). In `ui_how()`, the right column and the bottom bar each get an anchor per layout: split = column right, bar center; right = both right (the whole 4:3 HUD sits flush in the bottom-right corner); center = both center; stretched = untouched. The tests replay every recorded session under all four layouts and check where the column and bar tiles land.

Fix order agreed: 1. layout (done), 2. HUD hover/click mapping (uses the active layout's inverse mapping), 3. text scaling, 4. conversation screen.

## 2026-11-21: MaxAspect (aspect-ratio cap)

`[Display] MaxAspect` (0 = none; `16:9`, `21:9`, `64/27`, `2.39`…; values below 4:3 count as 4:3). It is a safety net for ultrawide screens, which can't be tested here (the test screen is 16:9). It can be tested on 16:9 by setting the cap below it, e.g. `16:10` or `4:3`.

- **World:** the Hor+ projection already gives square pixels with a scale that doesn't depend on width (`x_pix ∝ 0.8·4H/3`), so a capped screen shows the same image as a narrower screen; only the visible width differs. The parts beyond the cap are covered by **two black rectangles**, drawn every gameplay frame through the original `AddTri`/`CommitTri`. To look like an ordinary call, they copy the bucket, material and fourth argument of the right-column HUD tile the game drew (bucket 0, the same pass as the HUD, over the world), with black vertex colour (texture × diffuse = black) and a UV inside the tile. The remembered material is dropped on `DestroyAllTextures`.
- **HUD:** `map_right` anchors to the capped right edge; `center` is unchanged (it's the screen centre either way); `HudLayout=stretched` (or `Layout=0`) is stretched over the capped area instead of the full screen.
- **Menus:** unchanged (4:3 pillarbox already fits any cap ≥ 4:3); with `Layout=0` they are stretched over the capped area.
- **Tests:** every session is also replayed with caps 16:10 and 4:3 (split, right) and 16:10 with `Layout=0`. Checks: bars exactly cover 0..left and right..W in gameplay, no bars without a cap, HUD tiles on the capped edges.

## 2026-11-21: Hover and text logging; tooltip layout fixes

**Goal:** know where the cursor is relative to what the game shows for it (labels over world objects, HUD and menu tooltips), as groundwork for scaling text and the translucent boxes around it.

**String function:** `0x42B020` (glyph flush) has no direct callers (it's reached through a runtime pointer). Instead of static tracing, the debug log now **walks the game's stack at every text draw**: it records return addresses into the exe that follow a call instruction (the call chain of the string code) and pointers to printable strings (the words being drawn). Each distinct string and chain is logged once: `text: "<words>" font m=.. first glyph at (x,y) h .. via A < B < C`.

**Hover events** (`src/hover_log.inc`, Log=1): every frame the cursor sprite (game coordinates), the `0x00456ABD` boxes and the glyphs inside them are noted from the game's own vertices, plus the three smallest world draw calls under the cursor. When a box appears, changes or disappears, the log writes: words, cursor (screen and canvas), region (world / hud-bar / hud-column / menu), box size and offset from the cursor, glyph count, lines, text size, glyph height and margins.

**First numbers (from recorded frames, 3840×2160):**
- world object labels: box offset **(+11,+30)** or **(+11,−10)** from the cursor
- HUD/menu tooltips: **(−17,−38)** over the bottom bar and menus, **(−17,+2)** over the column
- every box is **46 px** tall; margins about **19 px** left/right and **17 px** top/bottom; glyphs **9–12 px**, one line. All fixed pixel sizes that don't scale with resolution, so box and text have to be scaled together.

**Layout fixes found with this data:**
- In menus, a tooltip box was squeezed to 4:3 proportions and its text moved separately, so the text could drift out of the box. Now each tooltip and its text move as one rigid piece, by the offset of the box's left edge (≈ the cursor's own offset). In gameplay the offset is 0 (the cursor is not moved).
- A tooltip box and its text can be drawn in different flushes, in either order. The tooltip list is now kept for the whole frame, and the previous frame's tooltips are used as a fallback.

**Tests:** the replay feeds every captured screen twice and checks the second frame (screens are steady in game and some state carries over). New checks: tooltip boxes keep their shape, and text keeps its position relative to its box. The harness can also run the debug-log code (`G 1`), so hover events can be generated offline from recorded frames. `tools/sbm_log.py` prints a hover timeline, the sizes seen (box heights, offsets, glyph heights, margins) and the text catalogue.

## 2026-11-21: First hover session analysed; UTF-16 text logging

**Session 20460** (3840×2160, split HUD): 992 hover events, 600 text entries, 32 screens, **0 clicks**.

| Where the cursor is | Box offset from cursor (px) | Box |
|---|---|---|
| world object label | (+11, −10) or (+11, +30) | 46 px tall, one line |
| HUD bottom bar | (−17, −38) | 46 px |
| menu | (−17, +2) or (−17, −38) | 46 px |
| multi-line panels (not cursor tooltips) | fixed place | 48 / 88 / 108 / 128 px, 20 px per line |

Margins are 19 px left/right and 17 px top/bottom; glyphs 9–11 px. Every size is a fixed pixel count, the same at any resolution: at 4K the text is a quarter of the height it has at 640×480. Text scaling must scale the font, the line spacing, the margins and the cursor offsets together, around the cursor anchor, so the box stays next to the cursor and the object it names.

**Words:** the stack walk found real strings only for ASCII ones (loading screen, level names, `lang\english\e_panel.uni`). The game's text is **UTF-16**, so the walk now also tests every stack value as a UTF-16 string and as a pointer to an object whose first field is one (the layout code at `0x4345A4` reads `[[ecx]+i*2]`). UTF-16 hits are logged with a `~` prefix, e.g. `text: "~Inventory" ...`.

**Call chain found:** `0x635462` calls `0x42B024` (the real glyph-flush entry) once per text object, walking a list from `[[0x1E4CFBC]+4]+8` linked through `+0x510`; `0x432300` calls `0x4345A4`, which lays out one UTF-16 string. These are the candidates for hooking text size directly (step 3).

**Tests:** this session is in `tests/data/session-2026-11-21-1450.log` (hover/text lines stripped); all 8 configurations pass.

## 2026-11-21: Clicks in gameplay come from DirectInput

The click log used `GetAsyncKeyState`. Every click recorded so far was in a menu; a whole session of clicking on objects in the world logged none. In gameplay the game reads the mouse through **DirectInput** (`DirectInputCreateA`, version 0x700, object at `[0x3673628]`), which bypasses the Windows button state.

The game keeps two device wrappers at `[0x367361C]` and `[0x3673620]`, with the DirectInput device at wrapper+8. With `Log=1`, the DLL now finds the one whose `GetCapabilities` reports a mouse and watches its `GetDeviceState` (+0x24) and `GetDeviceData` (+0x28) results for the button bits (state offset 12..15 / `DIMOFS_BUTTON0..3`). Presses between frames are latched so short clicks aren't missed (`src/dinput_mouse.inc`).

**Crash fix (same day):** the first version gave the device object a copied vtable and matched DIDEVTYPE 3, which is the *keyboard* (mouse = 2); the log showed "128 buttons". The game crashed right after the intro. DirectInput validates each object's vtable pointer on calls, so a foreign vtable makes the keyboard reads fail, and the copy also pointed into this DLL, which the game unloads and reloads. Now the object is left alone: the two entries are patched **in the device's own vtable** (VirtualProtect), the wrappers pass every call through and only read the results when the device is the mouse, and the entries are restored on every unload. The type and button count of both devices are logged. (GOG's `patch.dll` also wraps DirectInput, so the vtable may be its; that doesn't change the approach.)

The click event now says where the press came from (`os`, `di` or both), the game cursor in screen and canvas units, the region (world / hud-bar / hud-column / menu), the label that was showing, and the world draws under the cursor (format example, numbers made up):

```
event: click L (di)  cursor (1904,1012) [canvas 317.3,224.9] world  os (..)  screen 1D5EAE24 game frame 5120
        on label "~Chest"  box (1915,1042)..(2003,1088)
        under cursor: world 0048EA4B m=1A3  (1870,960)..(1950,1060)
```

## 2026-11-21: Inverse HUD mapping, click diagnostics, click tests

**First click session** (15:17, pid 2500, split): the DirectInput hook found the mouse (type 2, 5 buttons); 19 clicks, all `di`: 4 on the bottom bar, 7 in the world, 8 in menus, none on the column. The OS cursor does not follow the game cursor in gameplay (it stayed at one spot during the bar clicks), so the mapping has to use the game's cursor.

**`ui_hud_aim()`** (`ui_layout.inc`): for a screen point, which HUD part the layout drew there (bar, column, or world) and the x where the game has that point. It is the exact inverse of the drawing: it asks `ui_how()` for each part's mapping, so it follows `HudLayout`, `Layout` and `MaxAspect`. Column is tested first (canvas y < 420), as in `ui_how()`. Rigid sprites move by their centre, so inside one the result can be off by a few px; art tiles map exactly. `ui_geom()` now holds the per-frame geometry (shared with `ui_flush`).

Replaying the four bar clicks (split, 3840×2160):

| Frame | Clicked at x | Game has that point at x | Miss | Result in game |
|---|---|---|---|---|
| 809 | 1968 | 1984 | −16 | menu opened |
| 951 | 2845 | 3153 | −308 | nothing |
| 1020 | 3233 | 3671 | −438 | a menu opened (likely a different button) |
| 1666 | 1886 | 1875 | +11 | menu opened |

**Debug log:** each gameplay click gets an `aim:` line (drawn part, game point, game hit-test, miss), and `event: click result` 20 frames later (or at the next click) with mode and fingerprint before/after.

**Tests:** round trip for all configurations at 16:9, 21:9 and 16:10; the in-game measured offsets; recorded clicks replayed (session `tests/data/session-2026-11-21-1517.log` added) and compared with the DLL's `aim:` lines when present. Optional `tests/data/hud_buttons.txt` names the buttons; mismatches become failures once `STEP2_DONE` is set. The first grid put bar points at canvas y = 421, which `ui_how()` counts as column (an element at y ≤ 421 with x ≥ 536); the only real element there (`0x004A9195` m=4A at x 561, y 406–420) lies inside the column, so the rule is right and the grid uses y = 422.

**Second click session** (16:00, pid 26384, split): 25 gameplay clicks, 24 on or near the HUD (9 on the column); all 25 `aim:` lines agree with the harness. Misses grow toward the edges as predicted: bar centre +1/+9 px (menus open), bar left +403…+417 px (always the same menu, `F0CB4983`: a neighbouring button), bar right −300 px (nothing), column +30…+128 px. Clicks on the ends of the bar's old stretched area (x 327, 3730) are drawn as world but still hit-tested as HUD by the game. The hover-label words are still stack junk (system strings, environment variables); the filter has to be tightened.

## 2026-11-21: Debug screenshot key

`[Debug] ScreenshotKey` (default F11, Log=1 only, `src/screenshot.inc`). A press requests a frame capture (a new marker); when that capture has been written (EndScene), the next StartScene grabs the window, because the captured frame is then on screen. If no capture finishes within 120 frames the grab happens anyway. Saved as 24-bit BMP to `<game folder>\Screenshots\SBM_<pid>_<n>.bmp`; the log line `event: screenshot <n> (capture <marker>) saved ...` pairs it with `FRAME CAPTURE <marker>`. GDI (`BitBlt` etc.) and the extra user32/kernel32 calls are resolved with `GetProcAddress` at the first press, so the import table stays kernel32 + user32. The grab is tried from the screen DC at the client area first (what DWM shows), then from the window DC; an all-black result (32×32 sample) is logged as a warning, since fullscreen flip modes can hide the picture from GDI. Untested in game yet.

## 2026-11-21: Screenshots work; HUD button map

**Screenshot key confirmed:** session 10964 saved 19 F11 shots, all `from screen`, none black (DDrawCompat fullscreen is visible to GDI here). Each paired with its frame capture.

**Button map** (`tests/data/hud_buttons.txt`, 34 buttons, canvas units): bar orbs and frames measured from screenshot 1 and mapped back to canvas, HP/mana fill exact from capture 6, names from the tooltips visible in the shots: Compass, Inventory screen, Hit Points, Mana Points, Options screen, Enemy, (enemy) Hit Points, Character screen. The right big orb is a feature that unlocks later in the game (per the user); it has no tooltip or action yet, so it stays in the map as `right-orb` and is checked once unlocked. Column: weapon `<`/weapon/`>`, six icons (potion, spell, ranged, melee, food, help), move slots (CUT, CUT LOW, 3 more), COMBO, 10 slots. The orbs are part of the bar tile art (only the compass has its own sprite, `0x004A9195` m=59), so no per-button draw calls exist to detect them.

**The map explains every recorded result:** drawn compass -> game hits Inventory (the menu that opened, `F0CB4983`); drawn character portrait -> Enemy (nothing happens); drawn right orb -> Character screen; drawn potion -> spell, melee -> food. Clicks on the old stretched compass area (now world) still hit Compass.

**Prediction per layout** (button centre clicked, no fix): split 27/34 work, right 25/34, center 5/34 (the whole column misses). With the inverse mapping every button maps back to itself in every configuration (new test).

**Harness:** limits raised (2M vertices, 200k buffers) for the 40-screen session. The logged hover-label words are still junk, while the screenshots show the real tooltip text: tightening the string filter remains open.

## 2026-11-21: Where the cursor lives; HUD mouse fix (step 2)

**Cursor** (static analysis of `SoulbringeVCnoeax.exe`): the game cursor is a block at **`0x3559114`**: `int x, y` (screen px), `z`, `buttons[4]` at `0x3559120`. Every screen loop (`0x424DDC`, `0x42600A`, … and the gameplay routine **`0x6307F0`**) updates it the same way: if `[0x213CB3C]` (DirectInput in use), `0x69DBD0` calls `0x69DEF4`, which calls the mouse device's `GetDeviceState(16, 0x3559114)` (vtable `+0x24`) **straight into the cursor block**, then adds the movement to the previous x, y and clamps to the screen; otherwise `0x69DB20` fills it from `GetCursorPos` minus the window origin (`[0x36736C0]`, `[0x36736C4]`). Movement is kept at `0x3575F34`/`0x3575F38`. With the right button held and `[0x6B840C]` set, the game calls `SetCursorPos(old)` and puts its cursor back (camera drag), which is why the OS cursor in the logs stays put. Helpers: `0x52F690`/`0x52F6B0` return cursor x/W, y/H (entries in the table at `0x6B8A50`).

**HUD hit-testing** is not in one place: the cursor is read in 559 places (most in per-screen copies of the same loop). The coarse "is the cursor over the HUD" test is **`0x5171E8`** (eax = x, edx = y): x > 5/6·W (when `[0x2A98754]` is 0, column shown) or y > 7/8·H; world handlers call it to ignore clicks on the HUD. Other cursor-rect tests seen: `0x62A840` (a 768×512 layout via `0x4226A4`/`0x4226C4`), `0x4B0A40` (an overlay at x 0..256).

**Fix (`HudMouse`, default on):** instead of patching the hit tests, change the cursor at the source. The existing DirectInput vtable patch (`dinput_mouse.inc`, now active whenever the layout is on, not only with `Log=1`) sees the game's `GetDeviceState` into `0x3559114`: it notes the old x, y, lets the call run, and `ui_hud_mouse()` keeps the **real** mouse position from the raw movement. If the real position is over a drawn HUD part, `ui_hud_aim()` gives the game's point for it; the movement is rewritten so the game's own "old + movement" lands exactly there (otherwise on the real point). Only in gameplay; menus pass through (they draw the cursor where the game has it). Right-drag with `[0x6B840C]`: movement passed through, real position kept. If the game moves its cursor itself, the real position resyncs. `ui_flush` then draws the gameplay cursor sprite and the tooltip next to it shifted by real − game (`g_hmDX`); over the world the shift is 0.

**Tests:** the harness simulates the game's mouse update (`Q`/`C`/`D` commands). For every configuration at 16:9, 21:9 and 16:10: each of the 34 buttons, reached by moving the real mouse onto its drawn centre, puts the game's cursor on that button; sweeps across the bar and the column out and back end exactly where expected (no drift, no sticking where the game clamps); menus and right-drag pass through; the cursor sprite is drawn at the real mouse. With the fix switched off in the harness, these fail as in game (compass -> inventory). Clicks recorded with the fix carry a `hud mouse: real (x,y), game cursor (x,y)` line and are checked strictly against `hud_buttons.txt`.

**Known limit:** where the stretched HUD used to be but the world is drawn now (beside the centered bar, the strip just left of the column), the game still treats the cursor as over the HUD. Next: in-game check with `split`, then `right`.

## 2026-11-21: HUD mouse confirmed in game (split); ghost tooltips fixed

**Play test, split** (session 16:57, pid 9920): 137 gameplay clicks, 54 on the HUD, every one on the button drawn under the mouse (compass, inventory, options, character, weapon `<`/`>`, all six icons, move slots, combo, the right orb). Cursor placement good.

**Ghost tooltip** (session 17:05, pid 13064, screenshot SBM_13064_001): hovering the empty world left of the bar showed "Compass". Real (347,2005) is world, so the game got the real point, which is inside the stretched compass. **Fix (`ui_hud_dead`):** for a real point over the world, the game gets the nearest point outside its own HUD area: canvas y 404 with the bar (above its ornaments at 405), canvas x < 533 above y 420 with the column (the game's coarse test is x > 5/6 W). Points the game never had as HUD are unchanged. The cursor sprite and its tooltip are now shifted in y too (`g_hmDY`). Trade-off: a world click in those strips acts on the nearest point above / left of the line; in the corner under the column that can be a few hundred px away.

Also: when the game moves its cursor itself, the real position now simply resyncs to it (an attempt to keep the last offset broke the tests).

**Tests:** whole-screen check, 14,400 points per configuration (24 configurations): over a drawn HUD part the game is on the same button; over the world the game is outside its HUD and on no button, and unchanged where the game never had HUD. Cursor sprite drawn at the real x and y in the strip. Recorded clicks with a real position are replayed through the current `ui_hud_mouse()` (not the logged game cursor of that day's build); sessions 16:57 and 17:05 added. With `ui_hud_dead` disabled the test fails at canvas y 405 as in game.

## 2026-11-21: Can the world strips get the real cursor? (investigation)

**Question:** the dead zone (`ui_hud_dead`) moves world clicks in the strips where the stretched HUD used to be. Could the HUD and the world see different cursors instead?

- `0x52F690`/`0x52F6B0` (cursor x/W, y/H) sit in the engine interface table at `0x6B8A50`, which the game hands to `data\ai.dll` through its only export `exchangeinterfaces` (game side `0x44D5DC`, passing `&[0x35767C8]`; ai.dll keeps it at `0x10085ACC`). ai.dll calls ~60 entries of the table but **not** `+0x30`/`+0x34`, and the exe never calls them directly: no separate cursor channel for the HUD. HUD and world code both read `0x3559114` in the exe.
- World interaction goes through **9 functions** that call the coarse over-HUD test `0x5171E8` and/or the world pick `0x509094`: `0x508C68`, `0x540C34`, `0x554E30`, `0x555140`, `0x557384`, `0x558DA8`, `0x58A820`, `0x58DE90`, `0x58DEF0`.

**Possible proper fix (not done):** keep giving the game the HUD-safe (dead-zone) cursor by default; hook those 9 functions so they see the real mouse while they run (entry/exit code patches with verified bytes and restore on unload); replace `0x5171E8` with a test on the drawn layout. Unknowns: hover labels over world objects may be computed elsewhere and would still use the dead-zone point. Cost: ~10 code hooks, each a crash risk until tested in game. Decision pending with the user.

## 2026-11-21: Tooltip and label scaling (TextScale, step 3 part 1)

**What is scaled:** one-line tooltips and labels at the cursor: a `0x00456ABD` box 40–52 px tall whose top-left is within the game's cursor offsets of the cursor tip ((-17,-38) bar/menus, (-17,+2) column, (+11,+30)/(+11,-10) world labels; accepted: dx -30..30, dy -50..40). Box and glyphs (text inside the box, matched as before) are transformed together: `p' = anchor + offset + (p - anchor) * TextScale + shift`, anchor = cursor tip (top-left of the 32×32 cursor sprite, game coordinates), offset = HUD mouse offset in gameplay or the cursor's own menu mapping, shift = moves the scaled box back on screen. Glyph size, spacing, margins and the gap to the cursor all grow by the same factor, so the tooltip keeps the game's placement relative to the cursor. Fixed boxes (multi-line panels, the two small corner boxes) are not touched; their text wraps in the game's layout code (later).

**Frame order:** the cursor sprite is often drawn in a later flush than its tooltip, and the text in an earlier one, so the anchor is the last cursor seen and tooltips carry over from the previous frame (as before). A box now updates an entry copied from the previous frame instead of adding a second one (that made the box find a stale, unscaled entry). In game a new tooltip's text can be unscaled for its first frame.

**Tests:** replay at TextScale 250 (split, right), 300 (split capped 16:10) and 250 (stretched, Layout=0, capped); each screen fed three times, third checked. Checks: cursor tooltip boxes scaled exactly, on screen, at the scaled cursor offset (gameplay); glyphs scaled and kept in place in their box; other boxes unchanged. 67 cursor tooltips and 23 other boxes in the recorded data; with scaling disabled 300 checks fail. Not checked offline: how upscaled glyphs look (texture filtering).

**Fix, same day (session 21:12, screenshot SBM_26696_001):** hovering the column's COMBO slot showed the slot's own "COMBO" label magnified under the "Combo screen" tooltip: any text inside the box was taken as tooltip text. Measured over all recorded cursor tooltips: their one line of text has glyph centres 21–25 px below the box top (610 of 614 glyphs); other text under a box sat at 9–10 px (slot labels) and 49 px (a menu label below the box). Cursor tooltips now only take glyphs centred 14–32 px below the box top (`tip_has`, tip field 11). New check: text under a scaled tooltip keeps its size; without the band it fails on the COMBO label and on the menu label (session 13:40, capture 13).

**Bug report, 21:25:** "hovering over anything with text still magnifies that word when the tooltip appears" (after the band fix). Session 21:24 (pid 23876): 4 cursor tooltips captured (1 gameplay, 3 menus); in each, the text in the box's middle line is only the tooltip's own objects (1–3 text objects drawn right after the box), as in all older data (76 tooltips). So no foreign text is scaled any more; the likely cause is the look itself: the game's 9–11 px bitmap glyphs stretched 2× beside unscaled text read as a magnifier. Not confirmed (no screenshot). Logged as a ROADMAP bug; expected to be solved by laying text out at the larger size (step 3 proper) instead of stretching the drawn quads.

## 2026-11-21: Game-wide text (step 3), first findings

**Fonts (`data\*.fnt`, 110,664 bytes each; `numg`/`numr` 770):** `u16 count`, then one 32-byte record per character: `u16 code`, `u16` (junk, 0xBFF7), `i32 texture` (font material: 0x4E, 0x4F, 0x50 … as in the logs; 0 = none, e.g. space), `i32 u`, `i32 v`, `i32 width`, `i32 height`, `i32` (0), `u16` (vertical offset? 0 for space, 4 for most), `u16 0x65`. Example `16xB` 'ß': material 0x4E, u 0, v 39, 13×9 px, offset 4. 5PxB has 202 characters (space = width 4, no texture). The exe names: `5pxb/5pxr/5pxx/5pxy`, `16xb/16xx/16xy`, `phil`, `hbut`, `magc`, `mgrn`, `mred`, `cred`, `numg`, `numr`, `debug.fnt`.

**Consequence:** width and height are both the glyph's size on screen and its rectangle in the font texture. Scaling the font data would make the game read the wrong part of the texture: the scale has to be applied in the layout code (screen sizes, advances, line heights × scale; texture rectangles unchanged).

**Glyph flush `0x42B024`** (ecx = text object; `+4` glyph count, `+8` material): asks the renderer for vertex space (`[0x35767D4]+0xBC`, the `0x0042B0BC` caller in the logs) and copies per-glyph records whose corner positions are already in screen pixels (`+0x14/+0x18`, `+0x2C/+0x30`, `+0x44/+0x48`, …) with integer texture coordinates (`+0x20/+0x24`, `+0x38/+0x3C`, …, × 1/texture size from `0x3678458`…). It only adds global offsets (`0x3678450`…). So all sizes are decided in layout.

**Layout `0x4345A4`** (thiscall, arg = start index in a UTF-16 string at `[ecx]`): looks each character up by binary search in a table of 6-byte entries (`u16 code, u16 …`) at `[[ecx+0x204]]` (two modes by `[ecx+0x1C4]`), then measures: reads widths through the font object (`[edi+0xC]`, `[edi+0x14]`, `[... + 0x10]`), with what look like kerning pairs (`[esi + 8*eax + 4]`). It seems to measure words (for wrapping); where the glyph records are emitted is not found yet.

**Plan:** (1) find the in-memory font object and the function that emits glyph records (positions + UV) and the line height; (2) scale screen positions/advances/line height there by `TextScale`, leaving UVs; (3) scale the word measurement used for wrapping the same way, so the game wraps at the larger size; (4) box margins and cursor offsets; (5) replace the proxy-side tooltip stretching. Tests: new captures replayed as now, plus checks that glyphs keep their texture rectangles while growing.

## 2026-11-21: Text decoding (step 3, phase 1)

**Where glyphs come from:** batches are built by `0x626480` (add glyph; called only from the draw-string code `0x433B80`–`0x4341AB`): it picks or creates the batch list for the glyph's font material and calls `0x626720` → `0x626960` (append, max 10 per batch, `ret 0x1C`) → `0x626AE4` (copies 96 bytes of corners and texel ints into the record, plus colour and the pen position as doubles at `+0x64…+0x78`). New batches are 0x518 bytes (`0x6269E0` initialises them). The draw-string code reads the font record at `+8` (u), `+0xC` (v), `+0x10`, `+0x14` and spans u by `+0x14`, v by `+0x10` (`0x433A76`).

**Batch layout** (seen at the glyph flush `0x42B024`, ecx = edi = batch): `+4` glyph count, `+8` font material, glyph k at `batch + k*0x80` with corners `+0x14/+0x18`, `+0x2C/+0x30`, `+0x44/+0x48` (floats) and texels `+0x20/+0x24`, `+0x38/+0x3C`, `+0x50/+0x54` (ints), colour `+0x78`; next batch `+0x510`.

**Decoding (`src/text_decode.inc`, Log=1 only):** the hook stub saves the game's registers, so at the text AddTri (`0x0042B0BC`) the batch is `frame[-9]` (edi). The 16 fonts are read once from `data\*.fnt`; each glyph's texel rectangle (both pairings of the two size fields) is looked up in a hash of all 2,700 distinct font rectangles, none of which is shared by two characters. The characters travel with the pending draw (`Pending.td`) to: frame captures (new line `chars "..." font X` before `glyphs:`), hover events (label = the text inside the box, line by line), and the text catalogue (`text: "<line>" font X at (x,y) size w x h`, each new line once). Lines are joined across batches by position (level within 0.6 line height, gap ≤ 2.5 heights), a gap over 0.35 height (≥ 3 px) is a space. The stack walk (which mostly found unrelated strings, even the PATH) is gone. Unknown glyphs (e.g. clipped at a panel edge) show as `?`; the first 12 are logged with their texels.

**Tests:** with `SBM_FONT_DIR` or `SBM_GAME_DIR` set, every glyph of every font is stored as the draw code stores it and must decode back (2,700 glyphs); "Combo screen" drawn as two interleaved batches (16xB: C b s c e in 0x4F, o m r n in 0x50) must come out as one catalogue line. Without the fonts these checks are skipped. `tools/sbm_log.py` reads the `chars` line. New import: `ReadFile`.

**First session with decoding** (21:45, pid 9744, 18 screenshots): 235 catalogue lines; real words throughout ("Load Game", "Food and drink", "Potions and scrolls", "Place in Backpack", "Armour Value", NPC names in `mgrn`, save dates in `magc`, HUD tooltips in `hbut`, Options/character labels in `16xY`). Fixes from it:
- **Inventory item tooltips were never scaled** (user report: "Apple" tooltip tiny). They are placed differently: centred on the cursor (centre about +2 px) and 88 px below it, e.g. "Apple" box at (−33, +88), "Map of Madrigal" at (−65, +88). The cursor-tooltip rule now also accepts one-line boxes centred on the cursor (−8..+12 px) 70–110 px below it (DLL and test mirror). Session added as `session-2026-11-21-2145.log`; without the rule the replay fails on capture 31 ("tooltip box 71x46 -> 71x46, expected x2.5").
- **Clipped glyphs** (last letter of every Options/character label, the "O" of the COMBO slot label: one texel narrower) decoded as `?`. Top-left corners are unique too once the material is included (2,700 keys, 0 clashes), so a glyph not found by its full rectangle is matched by material + top-left with a size no larger than the font's. Test: 90 glyphs drawn one texel short.
- **Lines mixed two texts** where a tooltip (hbut) overlaps a HUD label (16xY/16xB): lines now join only glyphs of the same font. Test: "COMBO" in hbut over "Combo screen" in 16xB stays two lines.
- Accented letters (Mêlée) are written as Latin-1 instead of `?`.
- HUD slot labels (Cut, Cut Low, COMBO) are drawn twice with a small offset (two fonts) while they slide in, which gives many short catalogue variants; expected.

**Item tooltips too far away** (session 21:56, pid 23628, screenshots SBM_23628_001–004): once scaled, "Apple" showed in the cell below the apple. The game puts item tooltips 88 px below the cursor, which at 4K is inside the item's own cell (cells are ~228 px tall); scaling around the cursor doubled that gap to 176 px plus the doubled box. Item tooltips (the centred kind) now scale around **their own centre**, so they stay where the game puts them and only grow; corner tooltips still scale around the cursor tip (their gap grows with the cursor). Also: in menus a scaled tooltip is kept inside the 4:3 menu area, not just on screen ("Mundane Compass" in the rightmost column reached into the side bar at 300%). Test: item tooltip centres stay at the game's centre (+ the menu's cursor offset) unless pushed back inside; with the old anchoring it fails by ~166 px on all four captures. Session added as `session-2026-11-21-2156.log`.

## 2026-11-21: Message boxes; line joining by position

**`HudLayout=right` confirmed in game** (user, 22:18).

**Messages were not scaled** (session 22:13, pid 23036, `HudLayout=right`, screenshots SBM_23036_001–003): opening a chest or a door shows a one-line `0x00456ABD` box centred on the screen just above the HUD: 4K boxes at y 1791–1837, centred at x 1920 ("Empty." 74 px, "Locked." 79 px, "Gained object: Spade" 151 px wide). They are not at the cursor, so no rule took them. Now: in gameplay, a one-line box (40–52 px) whose centre is within 3 px of the screen centre is a message; it scales around its bottom centre (grows upwards, stays above the HUD) and only its middle text line goes with it (text centres 22–25 px below the top, as for tooltips). Fixed boxes in gameplay also stopped taking the HUD-mouse offset (that belongs to cursor tooltips only). Test: message boxes scale and keep their bottom centre; without the rule the session fails on captures 11 and 12.

**Line joining, corrected:** the "join only by font" rule from 21:45 split real lines ("Empty." came out as "BOEpty"/"Cwm" pieces): several fonts share identical glyph shapes, so the font of a decoded glyph is only the first font that has the shape. Lines now join by position only, but the line's centre and height are those of its first glyph (they used to grow with every glyph, which is what pulled the COMBO slot label into the "Combo screen" line). Test: a label 13 px above a line stays a separate line.

**Small box flashing beside HUD tooltips** (user, 22:41, `HudLayout=right`; session pid 15568): hovering bar buttons showed the game's small tooltip at its original (unmoved) position for a moment before the scaled one. Cause: the tooltip rules measured the box against the last cursor *sprite* seen, and the sprite is often drawn after the tooltip, so it is last frame's. While the mouse moves over the moved HUD the game cursor moves faster than the mouse (the bar is drawn at 0.75 scale), so last frame's sprite can be more than the 30 px window away and the tooltip is not recognised for that frame. Before 22:18 such a frame still got the HUD-mouse offset (a small box under the big one, hardly visible); since fixed boxes stopped taking it, it showed at the game's own position. Fix: the anchor is now the game's cursor of this frame, read from `0x3559114` (the sprite is drawn 2 px up and left of it: clicks show sprite 702/2558 vs game 704/2560, still and moving), with the sprite as fallback if no rule matches (menus not verified). Test (`live_cursor_test`): cursor jumps 500 px and the tooltip is drawn before its sprite: 180 px wide with the live anchor, 90 px (unscaled) with the sprite.

**Confirmed in game (22:48):** no more small-tooltip flash on the HUD with the live cursor anchor. Dev build zipped (`release/SoulbringerModern-0.2.0-dev.zip`, not a release: version stays 0.2.0-dev).


## 2026-11-21: Text layout code mapped (step 3, phase 2)

Static analysis of `SoulbringeVCnoeax.exe`. Every function below is byte-identical in `SoulbringeVC.exe` (the two exes differ in 1,163 `.text` bytes spread over 313 functions, none of them in the text code; checked function by function).

**Pipeline, top to bottom:**

```
screen loop / widget        sets the text object's box, position, font, string
  0x411634  auto-fit box    (only when the box rect is empty) measures the text, picks the box size
  0x549FC4  screen rect     box + position (+ offset), pushed inside the clip rect
  0x432094  draw paragraph  thiscall(text), ret 8: line by line, word by word
    0x434284  line start      x for the alignment (+0x1C0: 0 left, 1 right, 2 centre)
    0x4345A4  measure word    width of the word at index i (for wrapping)
    0x432820  draw word       thiscall(text, i, x, y, clip l,t,r,b), ret 0x1C: per character
      0x433714  draw glyph    thiscall(font, code, x, y, clip, layer, colour, 2 doubles), ret 0x34
        0x626480  add glyph   the only callers are 12 call sites inside 0x433714
          0x626720 -> 0x626960 -> 0x626AE4   copy 96 bytes into the batch record
0x635462 (per frame)  ->  0x42B024 glyph flush  ->  renderer AddTri (0x0042B0BC in logs)
```

Other direct callers of `0x433714` (text drawn character by character with their own pen code, probably edit fields and lists): `0x442E94` (called from `0x440824`, `0x449CA8`), `0x446380` (`0x4462FD`), `0x446C70` (`0x44636A`, `0x446C61`). All glyphs, whatever path, end in `0x433714` → `0x626480`.

**Font object** (0x30 bytes, constructor `0x4D9160`, load `0x681ED0` = thiscall(font, name, flag), ret 8; 32 call sites, one file read `0x628394` called only from `0x681F5E`, then a sort `0x628044`):

| Offset | Meaning |
|---|---|
| `+0x00` | file name |
| `+0x04` | records (32 bytes each, as in the file), sorted by code, binary-searched |
| `+0x08` | 1 = direct table present |
| `+0x0C` | direct table, 256 × 32 bytes, indexed `code << 5` (code < 256); `+4` = −1 when absent |
| `+0x14` | record count |
| `+0x18` | line height = max over glyphs of (y offset `+0x1C` + height `+0x14`), computed at load (`0x628453`) |
| `+0x1C` | loaded flag |
| `+0x20` | line gap, 1 (constructor) |
| `+0x24`, `+0x28` | kerning table and its count: 12-byte entries {code, pairs pointer, pair count}, pairs 8 bytes {code, adjust at `+4`}; read from the file after the records (the 104,198-byte tail of every 202-glyph font) |
| `+0x2C` | letter spacing, 0; set by `0x691F50` (16xy gets −1) |

Record fields as used: `+0x08` u, `+0x0C` v, `+0x10` width, `+0x14` height, `+0x18` rotated flag (0 in every shipped font), `+0x1C` (i16) y offset. Width accessor `0x6245C4` (returns `+0x10`), height accessor `0x623B84`, "has glyph" `0x413894`.

**The game already picks bigger fonts at high resolution:** `0x67FB79` `cmp [0x367370C], 1000; jl` (`0x367370C` = screen width, `0x3673708` = height): at 1000 px wide or more the three main fonts (`[0x35565DC]`, `[0x213F3E0]`, `[0x213F3E4]`) load `16xy/16xb/16xx` instead of `5pxy/5pxb/5pxx`. So 4K already uses the largest set (line height 15 px); there is nothing bigger to switch to. Line heights from the files: 5px* 10, 16x* 15, phil/hbut/mgrn/mred 17, magc 19, cred 35, numg/numr 13.

**Draw glyph `0x433714`** (`0x433880`–`0x4339B4`, two copies of the code for the direct and sorted tables): unclipped corners `x0 = x`, `y0 = y + yoff` (kept at `[ebp-0x3C]`), `x1 = x + width` (`[ebp-0x4C]`), `y1 = y0 + height` (`[ebp-0x48]`); clipped against the clip rect into `[ebp-0x214..-0x208]` (or `-0x224..-0x218`); empty → no glyph. Texels: **u = rec.u + (clipped x − x0), v = rec.v + (clipped y − y0)**, i.e. one texel per screen pixel (`0x433C7B`: u spans width `+0x10`, v spans height `+0x14`; the rotated path at `0x433A2B` swaps them — the 0x433A76 note in the phase 1 entry was that path). Then `0x626480` with the 96-byte corner block (`[ebp-0x1A4]`), material `rec+4`, colour, two doubles; the text system is `[0x1E4CFBC]`, `[0x1E4CFC0]` or `[0x1E4CFC4]` by the layer argument (0/1/2). Font pointer at `[ebp-0x20]`, record offset at `[ebp-0x38]`.

**Advance** (`0x432820`, after each glyph): `x += width + spacing(+0x2C) + 1` (`0x433562`: `8B 49 2C 8B 5D F0 8D 44 08 01`, `add [ebp+0xC], eax`); with kerning on (text `+0x1B8`, default 1) the pair adjust from the font's kerning table is added (`0x4333A4`: `8B 4D C4 03 C8 01 4D 0C`). The word loop stops at a space or the end.

**Space and wrapping** (`0x432094`): space width = width of glyph 0x20 (10 if the font has none). For each word: `w = 0x4345A4(i)`; if it is wider than what is left on the line (`edi`), break; else draw it and subtract. **Wrap width** = text rect width (`+0x34 − +0x2C`). **Line step** (`0x432742`: `8B 59 18 03 59 20 03 D8`): font `+0x18` + font `+0x20` + text `+0x1DC` (extra leading, 0 by default) = line height + 1. The height still available (`[ebp-0x18]`, starts as text rect height − one line, `0x4321CE`) goes down by a step per line; below zero the rest is **not drawn** (fixed boxes cut text off rather than overflow).

**Text object** (constructor `0x4559D0`): `+0x00` UTF-16 string; `+0x0C..+0x18` clip/screen rect; `+0x1C..+0x28` box rect (local); `+0x2C..+0x38` text rect on screen (box + `+0x60/+0x64` position + `+0x68/+0x6C` offset); `+0x58` sizing mode (1; 3 = fixed width `+0x5C`, 200); `+0x1B8` kerning on; `+0x1C0` alignment; `+0x1C8` number of trial widths for auto-fit; `+0x1D0` draw mode (1 = wrapped); `+0x1D8` keep inside the clip rect; `+0x1DC` extra leading; `+0x204` character remap table (6-byte entries, mode `+0x1C4`); `+0x208` font.

**Box sizing** (`0x411634`, called from `0x432094`/`0x549FC4` when the box rect is empty): measures the whole text (longest line, longest word); if one line fits in the clip width the box is (0, 0, width, height) with the height from `0x413A64` (lay out at a given width, return the height); otherwise it tries `+0x1C8` widths for the best shape. So **tooltip and label boxes come from the font metrics**; nothing is hard-coded per box.

**Tooltip drawing** (inline in every screen loop: `0x4285A0`, `0x429160`, `0x42CB10`, `0x430730`, `0x435650`, `0x437ED0`, `0x439570`, `0x43BBC0`, `0x43F770`, `0x443CD0`, `0x4493D0`; example `0x431333`–`0x43157F`): the tooltip is the text object `[0x1E4AF60]`, shown when `[0x1E4AECC]`; its position = cursor + (`[0x1E4AED4]` × W/768, `[0x1E4AED8]` × H/512) (constants `0x3678DF8` = 1/768, `0x3678E00` = 1/512), set per kind: (0, 26) at `0x42C38C`, (8, 36) at `0x43E2A2`. The box is the text rect from `0x549FC4` grown by **W/200 × H/154** (19 × 14 px at 4K: 18 px hbut text + 2 × 14 = the measured 46 px), built by `0x4572B8` and drawn by `0x456A50` (the `0x00456ABD` draws in the logs), alpha `[0x6B8144]` × `[0x3678E08]`; then `0x432094` draws the text. **Box margins and cursor offsets are already proportional to the screen**; only the text is not.

**What this means for the design.** Every measurement (advance, word width, wrapping, alignment, box auto-fit, line step) reads the font's record widths/heights and `+0x18`/`+0x20`/`+0x2C`; only `0x433714` also uses them as the texture rectangle. So instead of patching each measurement:

1. **Scale the font metrics once, after load:** replace the call at `0x681F5E` (`E8 31 64 FA FF`, call `0x628394`) with a call to a proxy function that calls the original, then multiplies each record's width, height and y offset (both tables), `+0x18`, `+0x20` and the kerning adjusts by s, keeping the original sizes in a side table. Letter spacing (`+0x2C`, set later by `0x691F50`) scaled at the setter or ignored (±1 px). Fonts loaded before the proxy is in place must be scaled in place (known globals above; per-screen fonts are loaded later through the same call).
2. **Fix the texels at the one place they are made:** hook `0x626480` (entry `53 8B DC 83 EC 08 83 E4 F0 83 C4 08`, a 6-byte stolen prologue; every caller is `0x433714`, so ebp at entry is its frame). For each of the 4 vertices: u = u_game − (x − x0) + (x − x0)/s, v likewise with y0 (x0 = `[ebp+0xC]`, y0 = `[ebp-0x3C]`), because the game wrote u = rec.u + (x − x0). The glyph's quad then has the scaled size and the original texture rectangle, clipping included. (Alternative: 12 call-site patches inside `0x433714`, same code.)
3. Nothing else for one-line text: advances, word widths, wrap, alignment, auto-fit boxes and line steps follow from the scaled metrics; box margins and cursor offsets already scale with the screen. Fixed-size boxes (rect set by code) wrap more lines and cut off what does not fit (phase 4: grow, or cap s).

Risks found: menus at the original 4:3 positions get larger text from the game (the proxy only moves menu glyphs, as now); the 5 secondary `0x433714` callers do their own pen work from the same (scaled) metrics; hit tests on text (edit-field caret) read the same metrics, so they stay consistent. To be checked in phase 3: whether any fonts are loaded before the renderer DLL (log at the hook and at attach), and that no code reads font metrics for something that must stay unscaled.

## 2026-11-21: Text hook (step 3, phase 3), built behind `[Debug] TextHook=1`

**What it does** (`src/text_scale.inc`, hooks in `soulbringer_modern.c`, off by default): with `TextHook=1` the game draws its own text at `TextScale`, and the proxy's tooltip stretching is switched off (`g_cfgText` = 100 internally). Three patches, each applied only if the original bytes are there (or our own patch from an earlier load), all removed when the DLL unloads:

| Address | Original | Patch | Purpose |
|---|---|---|---|
| `0x626480` (add glyph entry) | `53 8B DC 83 EC 08` | `E9 rel32 90` → `tx_glyph_thunk` | rewrite the 4 texel pairs, replay the 6 bytes, jump to `0x626486` |
| `0x691F50` (letter-spacing setter) | `56 8B 44 24 08` (+ `89 41 2C 83` checked) | `E9 rel32` → `tx_spacing_thunk` | `font+0x2C = value × s` for fonts we scaled |
| `0x681F5E` (call font read) | `E8 31 64 FA FF` | `E8 rel32` → `tx_load_thunk` | call `0x628394`, then scale the font |

The add-glyph patch goes in first and the install stops if any check fails: a scaled font without the texel rewrite would show the wrong part of the font texture. Fonts already loaded when the DLL starts are found through the 18 globals that hold font objects (stored right after the constructor `0x4D9160`) and scaled in place.

**Scaling a font:** the font's name (`+0`, e.g. `16xB.fnt`) picks the file in `data\`, read once at install; every record must match the file (by code and texture corner; the files hold several spaces with code 0x20, so records are matched by content, not by code alone), either as loaded (→ scale) or already scaled by s (→ an earlier copy of the DLL did it: only registered). Anything else is left alone and logged. Scaled: width, height and y offset of each record in the sorted table and the direct table, every kerning adjust; then line height = max(y offset + height) of the scaled records, line gap = s, spacing = s × the unscaled spacing kept per font. These three are recomputed rather than multiplied because **the game reloads fonts into the same object** (per screen, and 5px → 16x at ≥ 1000 px): `0x628394` refreshes the records and kerning but keeps `+0x18` as a running maximum and leaves `+0x20`/`+0x2C` alone, so multiplying would compound on every reload.

**Texel rewrite:** at add-glyph entry ebp is still `0x433714`'s frame (the only caller; the return address is checked): unclipped corners x0 `[ebp+0xC]`, y0 `[ebp-0x3C]`, x1 `[ebp-0x4C]`, y1 `[ebp-0x48]` (written only at `0x4338C5`–`0x4338E5` / `0x433980`–`0x4339A0`). The game wrote u = rec.u + (x − x0), so rec.u = u − (x − x0) from any vertex; the glyph's file size is looked up by (material, u, v) (2,700 keys from the 16 files, none ambiguous) and each vertex gets u = rec.u + round((x − x0) × w / (x1 − x0)), v likewise. A glyph whose quad equals its file size (an unscaled font) is left as it is, so a font the hook missed is drawn small but correct, never garbled.

**Spacing detail:** the advance is width + spacing + 1, or width + spacing + kerning adjust when the pair is in the font's kerning table (the files have full 114×114 tables for ASCII, adjust 1 for almost every pair in the 16x fonts, −7…1 in cred/magc/phil). The adjust is scaled, the constant 1 of the unkerned path is not (code, not data): characters outside the kerning table (accented letters) keep a 1 px gap.

**Tooltip rules** (`ui_layout.inc`) follow the game's larger text (`g_gts` = s while the hook is in, 1 otherwise): a one-line box may be up to 52 + 20(s − 1) px tall (46 px at s = 1: 18 px text + 2 × 14 margins), a corner tooltip may sit up to 50 + 24(s − 1) px above the cursor, and a tooltip's text line is the box less its 14 px margins (identical to the old 14–32 px band at 46 px). Needed so the HUD-mouse offset still finds the cursor's tooltips; with `TextHook=0` nothing changes.

**Tests** (offline, with the fonts): every glyph of the 16 fonts loaded, scaled at 150/200/300% and drawn as `0x433714` draws it, plus clipped glyphs; scaled once; reload into the same object scaled once; 100% unchanged; mutation (no texel rewrite) caught; a 64 px game-drawn tooltip is recognised. All session replays still pass. The phase 1 decoder is unaffected (texels stay the originals).

**Not handled yet:** fixed-size panels cut text off (phase 4); HUD slot labels may outgrow their art (phase 5); `TextScale=auto`. Deployed to the game folder with `TextHook=1` for the first in-game session (DLL SHA-1 `1cca3689…`).

## 2026-11-21: First `TextHook=1` session; Stock mode

**Session 23:5x (pid 10900, `TextHook=1`, `TextScale=200`, `HudLayout=right`):** installed on both renderer loads, all 28 font loads scaled (phil, hbut, magc, mgrn, mred, cred, numg/numr, 5pxb/5pxr, 16xb/16xx/16xy/16xB; line heights 15 → 30, 17 → 34 …), no crash, no "not scaled"/"already loaded" lines (every font is loaded after the renderer DLL starts). The decoded catalogue shows the right words at twice the size ("Load Game" 159 px wide, "Madrigal Inn", "Cut Low", "COMBO"), so the texel rewrite picks the right glyphs. **User: the text is larger but hard to read, some letters look garbled.** Most likely the texture filter: each texel of the font texture now covers 2×2 screen pixels and is sampled bilinearly, which blurs the glyphs and, at their edges, blends in texels of the neighbouring glyphs in the font texture (the original 1:1 mapping never sampled across a glyph edge). No screenshots from that session; next: reference screenshots of the stock game at 4:3 and the same screens with `TextHook=1`, then point sampling for text and/or keeping the sampling inside each glyph's rectangle.

**`[Debug] Stock=1`:** for those reference screenshots: forces `Widescreen=0`, `Layout=0`, `MaxAspect=0`, `CursorScale=100`, `HudMouse=0`, `TextScale=100`, `TextHook=0`; `Log` and `ScreenshotKey` still work (the DirectInput hook only watches clicks for the log when `HudMouse` is off). Logged as `STOCK mode` at load.

## 2026-11-22: Font legibility: decision and HD-font research

**Finding (user, 1024×768 stock):** the original fonts are hard to read even at the resolution they were made for; ddraw settings (integer scaling, font anti-aliasing, MSAA) do not help, because the letters are small baked bitmaps (journal text ~9 px tall, thin strokes, colour fringes inside the glyph images). Enlarging them (`TextHook`) can at best reach "1024×768, but bigger". Mockup (`text_options_mockup.png`, sent to the user): bilinear 2.8×, nearest 3×, and the same line redrawn from TrueType fonts; only the redrawn version is clearly more legible.

**Font map** (from the decoded text at F11 in sessions 14296/3872, chart `font_chart.png`): `magc` main-menu buttons, conversations, save list, quit dialog (gold calligraphy); `phil` journal; `hbut` all tooltips, place names, HUD bar labels; `16xY` options labels, character stats, ENCUMBRANCE, HUD slot labels; `16xB` the selected HUD slot label; `mgrn`/`mred` names in the world (log only); `16xX`, `numg`/`numr`, `cred`, `5px*` not seen on screen yet.

**Decision (user):** redraw `phil` and `hbut` in an old-style serif (Cardo-like look; at runtime a Windows font chosen in the ini); `magc` enlarged only (keep its calligraphy, judge after); `16xY`/`16xB` left for later.

**How (plan):** keep the game-side layout (`TextHook`) and replace the font *textures*: draw each glyph of the chosen fonts from the TrueType font into a k× larger page at the same layout, so every glyph keeps its place and the normalised UVs the game computes still match.

**Research so far (static):**
- The renderer is reached through the export table; font pages are texture **slots 0x4E–0x51** (= the font materials). At startup the game creates ~113 slots with `CreateTexture(slot, desc)` (`0x678A5C`/`0x678ACC`), 256×256 (`desc+4`, `desc+8` = 0x100); slots 0x4E–0x51 get their own flags (`desc+0x14` = 0x40 or 0, `0x678A1B`–`0x678A65`). Pixels are uploaded later with `OverWriteTexture(slot, ?, pixels)` (3 args, cdecl).
- In the original renderer (`D3d7Lib_orig.dll`, base 0x10000000), the slot record is `[[0x100786CC] + 0x1B54 + 4*slot]`: `+0` source width, `+4` source height, `+8` surface size, `+0xC` bytes per source pixel, `+0x54` the DirectDraw surface. `OverWriteTexture` (`0x1000F380`) converts the source with `0x1000F880`, which **already resamples** when the source size differs from the surface size, then converts the pixel format (`0x1000F9D0`). So a larger source is possible if the slot is created larger; to be confirmed: the source pixel format and where the game gets the page pixels.
- **Probe (log only, deployed, SHA-1 `f83f979b…`):** with `Log=1` the DLL logs the create descriptor of slots 0x4E–0x51 and the renderer's slot record at each upload, and writes the first upload of each page to `<game>\SBM_fontpage_<slot>_<w>x<h>x<bpp>.raw`. Nothing is changed. Next: read the dumps, decode the format, render the pages, map each glyph rectangle to its font and code (the `.fnt` records), then build the replacement pages.

## 2026-11-22: Font page probe results; HD font plan written

**Probe session (pid 9640, `Log=1`):** slots 0x4E–0x51 are created with `desc` = width/height 256, `+0xC` 1, `+0x10` 4, `+0x14` 0x40, `+0x2C` 2, `+0x30` 0x10, `+0x34` → `0x1FFDBCC`; each is uploaded once with `OverWriteTexture(slot, 0, pixels)` from one shared buffer. The renderer's slot record: source 256×256, surface 256, **1 byte per source pixel**, formats 1 → 4, one mip level, flags 0x40. The dumps (`SBM_fontpage_<slot>_256x256x1.raw`) are 8-bit palette indices (66–120 distinct values per page, index 0 = empty on 47–91% of each page); the palette is the game's global 256 × RGB table at `0x1FFDBCC` (`SetGlobalPalette`, export table `+0x68`, filled by `0x5C3D70`). Drawn with the `.fnt` rectangles on top, every rectangle sits on its glyph.

**Checked from the font files:** the four pages hold all 16 fonts mixed (`phil`, `hbut`, `magc` each on 0x4E–0x50; 0x51 only `16x*`/`5Px*`); 2,700 distinct glyph rectangles; none is shared by two fonts or two codes, and **no two rectangles touch** (at least one empty texel apart). So (a) `phil`/`hbut` glyphs can be redrawn in place without affecting any other font, and (b) the earlier guess that bilinear filtering blended in neighbouring glyphs was wrong: it blends in the empty gap (blur plus a transparent edge).

**Texture coordinates:** the glyph flush multiplies texel coordinates by 1/256 (`0x3678458`…`0x3678488`), so a k×-larger page with the same layout is addressed correctly with no change to the game.

**Plan:** `docs/HD_FONTS_PLAN.md` (replaces text plan phase 7): create the four font slots k× larger (k = `TextScale`/100 rounded up, so a redrawn glyph is drawn 1:1 at its final screen size: no magnification, and no minification, which without mipmaps would make thin serif strokes shimmer), feed the renderer a k×-enlarged copy of the indices, then redraw the `phil`/`hbut` rectangles into the locked surface from a Windows serif font (GDI), in the surface's own pixel format, on every upload. Open points (surface size actually granted, surface pixel format and how index 0 becomes transparent, repeated uploads, `ViewTexture`) are settled by a second read-only probe before anything writes. Renderer texture calls added to `docs/RENDERER_API.md`.

## 2026-11-22: HD fonts: face and living plan

- **Face:** the user chose the old-style serif from the mockup, **Cardo**. It is not part of Windows; it is free under the SIL Open Font License, so it will be shipped with the mod (font file + licence text) and loaded privately for the game (`AddFontResourceExA`, `FR_PRIVATE`; nothing installed). Fallbacks: Constantia → Palatino Linotype → Book Antiqua (Windows old-style serifs). `FontFace=` can name any installed font or a `.ttf` in the mod folder.
- **Page scale** set to `TextScale`/100 rounded up (2 at 200%) instead of a fixed 4: the font pages have no mipmaps, so a page larger than needed would be minified at draw time and thin serif strokes would shimmer; at k = s each redrawn glyph is drawn 1:1 at its final size.
- **Renderer-build check:** reading the original renderer's slot record is guarded by a byte check of its `OverWriteTexture` code; on mismatch the feature turns itself off.
- **Living plan:** `docs/HD_FONTS_PLAN.md` section 5 is a step table (what Claude does, what input the user gives, exit check), updated after every step: 1 surface probe (session) → 2 offline generator + previews (user picks face/weight) → 3 larger pages without redraw (session: must look identical) → 4 redraw in game (session + F11, alt-tab) → 5 tuning (decisions, `magc`) → 6 release (final check). In-game checklists in `tests/README.md`.

## 2026-11-22: HD fonts step 1: surface probe built

**What:** `src/font_probe.inc` (moved the page probe there and extended it), debug only (`Log=1`), read only. The DirectDraw surface is taken from the renderer's slot record (`+0x54`) only when the record is consistent (sizes 1–4096, 1–4 bytes per pixel) and the object's vtable is readable with non-null entries up to `Unlock`. Inspection runs at the renderer call after `CreateTexture`/`OverWriteTexture`, when the renderer has finished.
- After create and after each upload: `GetSurfaceDesc` (size, pitch, mips, pixel format flags/bits/masks, caps, colour-key flag), `GetColorKey(DDCKEY_SRCBLT)`; the slot record after create (surface size actually granted, open point 1 later).
- Every upload: FNV-1a sum of the source indices and of the converted top level (locked `DDLOCK_WAIT | DDLOCK_READONLY`), "same as before"/"differs" against the previous upload (open point 3).
- First upload per slot: converted surface to `SBM_fontsurf_<slot>.raw` (64-byte `SBMS` header: slot, size, row bytes, bits, pixel format flags, RGBA masks, colour key, upload number; then rows), and the palette from the create descriptor `+0x34` to `SBM_palette.raw`.
- Every frame (`StartScene`): `IsLost` of each font surface, changes logged; two frames after a restore the surface sum is compared with the last upload ("content kept" / "CONTENT DIFFERS"): open point 8, early.
- Other slots (first 3 `EndTextureCreation`s after the font pages exist): one line per distinct (source format, surface format, bytes per pixel, mips) with the DirectDraw format of one example (input for open point 2's fallback).
- Every `ViewTexture` (open point 4), `OsTaskSwitch` and `SetGlobalPalette` call; totals at unload.
- `tools/font_surface.py`: decodes the dumps with their own masks/colour key, compares every texel with `palette[index]` (quantised to the masks), reports how index 0 is encoded (alpha / key), writes colour-over-checkerboard and alpha PNGs. `--selftest` (synthetic 4444-with-alpha and colour-keyed 565 pages) passes.

**Test (offline):** builds without warnings, imports unchanged (kernel32/user32 only, no `__chkstk`), `replay.py` ALL PASSED, `font_surface.py --selftest` PASSED. Deployed to the game root and `data\Stealth\Render\`.

**Next:** the user's short session (tests/README "Step 1"), then read the log and dumps, answer open points 2, 3, 4 (and 8), and update HD_FONTS_PLAN section 5.

## 2026-11-22: HD fonts step 1: surface probe results

**Session pid 16880 (22:10, `TextHook=1`, `HudLayout=right`):** main menu, load, journal twice; no crash. The second renderer load created and uploaded the four font slots; the first (hardware check) load created nothing.
- **Pixel format:** every font surface is 256×256, pitch 512, one level, **A1R5G5B5** (R `7C00` G `03E0` B `001F` **A `8000`**), caps `00401808` (texture | system memory | mipmap | complex), no colour key (`GetColorKey` → `E_FAIL`). The other 109 slots (code 1 → 4 ×51, code 4 → 4 ×62) use the same format.
- **Conversion:** `tools/font_surface.py` against the index dumps and palette: index 0 → `0x0000`, any other → alpha 1 and `c >> 3` per channel (truncation, not rounding). 100% of texels on all four pages; the reader now uses the truncation model.
- **Open points:** 2 answered (**1-bit alpha**: a redrawn, anti-aliased Cardo glyph cannot keep soft edges in this format; thresholded it would look jagged); 3 one upload per page, never repeated (same source buffer `1AF084E8`); 4 no `ViewTexture`; 8 not an issue: the textures are system-memory surfaces, which DirectDraw never loses (no `LOST`, no `OsTaskSwitch` calls in the session).
- **Static (renderer):** format codes are bit flags; the surface format is `desc+0x10` (code 4), looked up by code in the device's texture-format list (`[0x100785E4] + 0x3D0C`, 0x28-byte entries, code at `+0x24`, count `+0x44DC`), and the conversion goes source code → intermediate → surface code through two converter tables, so an 8-bit palette page can be written into any listed format. Surface size: next power of two, clamped to 256 only when `desc+0x30 & 0x80` (font pages have 0x10): a 512² page looks possible (step 3 checks). Details in RENDERER_API.

**Next:** a read-only step 1b that logs the device's texture-format list (code → pixel format). If it lists A4R4G4B4 or A8R8G8B8, step 3 asks for it in `desc+0x10` for the four font slots and the redraw keeps anti-aliased edges. Fallbacks: thresholded 1-bit edges (still sharper than today at k = 2), or our own texture for those slots.

## 2026-11-22: HD fonts step 1b: texture-format list probe built

`fp_formats()` in `src/font_probe.inc`, once at the first `EndTextureCreation` after the font pages exist (`Log=1`, read only): the device (`[orig+0x785E4]`) texture-format list (`+0x3D0C`, count `+0x44DC`) and the second list (`+0x3538`, count `+0x3D08`), each entry as code + DDPIXELFORMAT + the dword at `+0x20`; which list entry the font slot uses (record `+0x50`, confirms the entry layout); which format-code bits have a source decoder / surface encoder (`obj+0x102C` / `+0x10AC`); bytes per source pixel per code bit (`orig+0x2060C`). Counts above 50 are not read. Build `a5fea3e3…`, replay ALL PASSED; deployed. Next: user session (start, main menu, quit).

## 2026-11-22: HD fonts step 1b: texture formats

**Session pid 12556 (22:17, start → main menu → quit):** the device lists 15 texture formats (table in RENDERER_API): with alpha, **A8R8G8B8 = code 0x80** and **A4R4G4B4 = code 0x2**; also A1R5G5B5 (0x4, used by every game texture), X1R5G5B5, R5G6B5, X8R8G8B8, L8, A8L8, two bump formats and DXT1–5. Every listed code up to bit 16 has both a decoder and an encoder, so the game's 8-bit palette page can be written into A8R8G8B8 by the renderer itself. The font slot's record points at list entry 3 (code 4), which confirms the list layout. Code bits are `0x1000F7D0`'s highest-set-bit + 1 (code 1 → 1, code 4 → 3), matching the converter table indices.

**Decision:** step 3 creates slots 0x4E–0x51 with `desc+0x10` = 0x80 (A8R8G8B8): Cardo can keep 256 levels of edge coverage. Fallback A4R4G4B4 (0x2). Memory at k = 2: 512² × 4 B = 1 MB per page. Open: whether the text bucket is alpha-blended or alpha-tested (step 3 logs the render states). Open point 2 closed; plan section 5 updated. Next: step 2 (offline page generator and Cardo previews).

## 2026-11-22: HD fonts step 2: page generator and previews

**Code:** `src/font_pages.inc` (plain C, no Windows or C runtime; compiles for the DLL target with no imports, not included in the DLL yet):
- `fpg_base()`: k× page in A8R8G8B8, index 0 → 0x00000000, other indices → opaque palette colour, nearest (what the renderer does itself);
- `fpg_glyph()`: clears one k× rectangle and draws a coverage bitmap: centred, condensed with a box filter if wider than the rectangle, bitmap row `top` on the font's baseline (bottom of 'H': phil 14, hbut 12 texels from the line top), clipped to the rectangle, optional outline (dilated coverage) under the letter, non-premultiplied "over"; leaves the original glyph if none of the letter would land inside (happens to a few accents and `·`, whose original cells sit elsewhere);
- `fpg_colours()`: bright / dark / darkest colour of a font from its glyphs (phil: darkest = 5C442C, hbut: bright E0E0B0, dark 606048);
- `fpg_read_fnt()`: cells from a `.fnt` (record +0x10 = width, +0x14 = height confirmed by drawing them: the swapped pairing gives garbage).

**Tests:** `tests/fonts/test_font_pages.c selftest` (base mapping k = 1–4, nothing written outside the rectangle, baseline row, top clipping, condensing, empty glyphs, refused rectangles, "over") PASSED; `build` mode on the real pages for each face checks that no texel outside the phil/hbut rectangles and no texel of the other 14 fonts changes, every redrawn glyph has ink, and 'H'/'x'/'n' sit on the baseline: PASSED for Cardo Regular, Cardo Bold and Crimson Text SemiBold at k = 2 (377 / 377 / 375 glyphs redrawn, the rest kept).

**Previews** (`tools/font_preview.py`, at 4K pixel size on stock-screenshot backgrounds, magnified 2×): journal lines (phil) and tooltips/messages (hbut): "today" (original glyphs magnified bilinearly, as `TextHook=1` draws them) vs the three faces. Layout as in the game: rectangle widths, advance = width + 1, kerning ignored.

**Finding:** the original rectangles are tight around the original letters, whose ascenders and digits are cap-height. Matched to the original cap height, Cardo's ascenders (b d h k l), old-style digits (3 5) and J/Q tails stick out by ~2 texels and would be clipped flat. The tool therefore picks the largest size whose letters and digits fit (1 px tolerance, J/Q excepted): Cardo comes out at 80–85% of the original cap height, Crimson SemiBold 85–94%. Getting the full size needs taller rectangles, i.e. new phil/hbut metrics (y offset, height, page position) written by the text hook after load, with free space on the pages (they are 47–91% empty): a later step. Spacing looks loose because the letters keep the original widths (open point 5).

## 2026-11-22: HD fonts: Cardo Bold chosen

**Decision (user, 22:32, from the step 2 previews):** Cardo Bold for both `phil` (journal) and `hbut` (tooltips, place names, HUD bar labels), "for now". Cardo Regular and Crimson Text SemiBold (both SIL OFL, previewed) stay documented as options, with the Windows fallbacks Constantia / Palatino Linotype / Book Antiqua (HD_FONTS_PLAN section 3, "Face options"). Default setting becomes `FontFace=Cardo Bold`; the release will bundle `Cardo-Bold.ttf` with `OFL.txt`. Next: step 3 (font slots created k× larger in A8R8G8B8, no redraw yet: text must look exactly as today).

## 2026-11-22: HD fonts step 3: larger A8R8G8B8 font slots (built)

**What:** `src/font_hd.inc`, `[Debug] FontPages=1` (off by default; `FontPageScale=0` → `TextScale`/100 rounded up, 1–4; forced off by `Stock=1`). Runs before the early pass-through in `hook_pre`, so it works with `Log=0` too.
- `CreateTexture(slot 0x4E–0x51, desc)`: if the descriptor is the known one (256×256, source code 1, surface code 4, no 0x20/0x80 size flags at `+0x30`) the argument is replaced by our copy with width/height 256k and surface code 0x80 (A8R8G8B8). The game's own descriptor is not touched.
- `OverWriteTexture(slot, 0, pixels)`: if the slot was created by us and the renderer's record says source 256k × 256k at 1 byte per pixel, the pixel argument is replaced by a k× nearest copy of the game's 256² indices (static 1 MB buffer, used inside the call only); otherwise the upload passes through unchanged (the renderer would otherwise read past the game's buffer).
- Renderer-build check once: 11 bytes at `orig+0xE930` (CreateTexture body) and `orig+0xF380` (OverWriteTexture body); on mismatch the feature logs and stays off. No code patches, nothing to undo at unload.
- `src/font_pages.inc` functions marked unused-ok so the DLL includes it without warnings (step 4 uses them).

**Render states (static, renderer):** the renderer's default state block sets ALPHATESTENABLE = TRUE with ALPHAREF = 0 (so any non-zero alpha passes), ALPHABLENDENABLE = TRUE and SRCBLEND/DESTBLEND from per-material tables (`0x1000BE80`…, `0x1000C680`…). Partial alpha should therefore blend, not be cut; confirmed only when step 4 draws real coverage.

**Test (offline):** builds without warnings, imports unchanged, `replay.py` ALL PASSED, `test_font_pages selftest` PASSED. Deployed (`e74d3f32…`) to both renderer folders; the test copy's ini got `FontPages=1`. In-game check: tests/README "Step 3"; with `Log=1` the surface probe dumps the 512² surfaces so `font_surface.py` can check the conversion.

## 2026-11-22: HD fonts step 3: checked in game

**Session pid 12740 (22:39, `FontPages=1`, `TextHook=1`, `Log=1`, 8 F11 screenshots: main menu, gameplay with world tooltips, inventory with item tooltip, journal twice):** `font pages: on, font slots 4E-51 at 2x (512x512 A8R8G8B8)`; each slot created 512×512, 32 bits, A `FF000000`, pitch 2048, caps `00401808` (system memory, as before); one `upload enlarged 2x` per slot, nothing passed through, no crash, clean unload. `font_surface.py` on the 512² dumps: 100% of texels equal `palette[index]` of the 256² page under nearest 2× (index 0 → alpha 0, others alpha 255). Screenshots at 1:1: "Load Game" (`magc`), journal title/entries (`phil`), "Mug of Ale"/"Bread" (`hbut` world tooltips), "Apple" (inventory), "Traveller" (`mgrn`) all correct, crisp (the 2× nearest page shows through: texel blocks instead of the previous bilinear blur), no fringes or boxes. Open point 1: the renderer grants the larger size and the requested format. Next: step 4, GDI redraw of `phil`/`hbut` into the surface after each upload.

## 2026-11-22: HD fonts step 4: redraw in game (built)

**What:** `src/font_hd.inc` part 2, `[Debug] FontRedraw=phil,hbut` (needs `FontPages=1`; empty = off; forced off by `Stock=1`).
- At each upload of 0x4E–0x50 the game's 256² indices are copied (64 KB per page) and the palette from the create descriptor (`+0x34`) kept; once all three pages have arrived, at the next renderer call (the renderer has converted them) the pages are redrawn; any later upload redraws that page again.
- Face: `AddFontResourceExA(<game>\SoulbringerModern\fonts\Cardo-Bold.ttf, FR_PRIVATE)` while drawing (removed afterwards), `CreateFontA(-em, …, FW 700, ANSI_CHARSET, OUT_TT_ONLY_PRECIS, ANTIALIASED_QUALITY, "Cardo")`; `GetTextFaceA` must report the requested face, else Constantia → Palatino Linotype → Book Antiqua; every choice logged. GDI loaded with `GetProcAddress` (imports unchanged).
- Size: `fpg_fit()` (new in `font_pages.inc`, with a metrics callback; same rule as `font_preview.py`): the largest em ≤ the size whose 'H' matches the original 'H' × k at which letters and digits (J, Q excepted) overflow their rectangles by ≤ 1 px. Selftest with a synthetic face.
- Glyphs: `GetGlyphOutlineA(GGO_GRAY8_BITMAP)` (0–64 → 0–255, rows DWORD-aligned, origin.y = rows above the baseline), drawn with `fpg_glyph()` into a copy of the locked surface, which is checked to be 256k² A8R8G8B8 first; written back row by row with the surface's pitch. Styles as in the previews: phil darkest ink, no outline; hbut bright with the dark colour as a 1 px outline (alpha 220).
- Log (`Log=1`): `font redraw:` lines (file loaded, face GDI gave, size per font, glyphs redrawn/kept/refused per page).
- Release packaging: `dist/fonts/Cardo-Bold.ttf` + `Cardo-OFL.txt` (SIL OFL 1.1, David J. Perry) go to `SoulbringerModern\fonts\` in the zip; credit in `dist/README.txt`; `*.ttf binary` in `.gitattributes`.

**Test (offline):** builds without warnings, imports unchanged, `replay.py` ALL PASSED, `test_font_pages selftest` PASSED (now with `fpg_fit`). The GDI path itself can only run in game. Deployed (`c53923b1…`) with the font in the test copy's `SoulbringerModern\fonts\`; test ini: `FontRedraw=phil,hbut`.

## 2026-11-22: HD fonts step 4: first session showed the old letters; redraw moved before the upload

**Session pid 21528 (22:53, 7 F11):** the log was as planned (Cardo loaded privately, "GDI gave Cardo", phil and hbut at 23 px em, 135 + 129 + 115 glyphs redrawn on 4E/4F/50, 3 kept), and the surface dump of slot 0x50 taken after the redraw shows Cardo letters, but **the screenshots show the original letters, unchanged** (journal title, entries and quest names compared 1:1 with session 12740).

**Why:** the renderer keeps each slot's surface in **system memory** (caps `00401808`) and draws from a **video-memory texture cache** (`[obj + 0x1F54 + 4*i] + 0x3C`, set with `SetTexture` at device vtable `+0x8C`). The cache entry is assigned at `EndTextureCreation` (record `+0x60` = 0x4E for slot 0x4E at upload time, -1 right after create), and `OverWriteTexture` refreshes it from the surface right after unlocking (`0x1000F63D`: if record `+0x60 >= 0`, call `0x1000E7F0(slot, cache index)`). The redraw ran after the upload, so it only changed the system-memory copy.

**Fix (build `caefa1c8…`):** the page is composed *before* the upload and handed to the renderer as the upload's source, so the renderer's own cache refresh carries it. The font slots are now created with source code **0x80** too (A8R8G8B8, 4 bytes per source pixel per the `orig+0x2060C` table; decoder and encoder both exist), and each `OverWriteTexture` of 0x4E–0x51 gets our page: `fpg_base()` (the enlarged palette colours, index 0 transparent: identical to what the renderer made of the indices in step 3) plus, with `FontRedraw`, the GDI glyphs of that slot. Upload is swapped only if the record says 256k² source, 4 bytes per pixel, source code 0x80. Colours are taken from the first page (0x4E): offline, page 0x4E alone gives exactly the same phil "darkest" (5C442C) and hbut "bright"/"dark" (E0E0B0/606048) as all three pages. The em size is fitted once per font. No pending/next-call logic any more. Builds clean, replay ALL PASSED, selftest PASSED; deployed for a second session.

## 2026-11-22: HD fonts step 4: second session: Cardo on screen, but wrong colours in other text; fixed with Load

**Session pid 11068 (23:02, 14 F11):** Cardo letters on screen (journal, tooltips), but **the colours of the font pages were wrong everywhere** (user: "font colour altered in other areas of the menus and HUD"; journal ink blue). The surface dump of 0x4E shows why: the renderer's own conversion of an A8R8G8B8 *source* (code 0x80) reads each pixel's bytes as A, R, G, B: our `0xFF50BC5C` (bytes 5C BC 50 FF) became `0xFFBC50FF`, and alpha came out as 0 or 255 only (1-bit, from "non-zero"). So the source-format route is out.

**Fix (build `c9f45708…`):** back to step 3's upload (source = enlarged 8-bit indices, which the renderer converts correctly: 100% in step 3), then at the next renderer call: compose the page with `fpg_base()` (identical to the renderer's conversion) plus the redrawn glyphs, write it into the slot's system-memory surface, and refresh the renderer's video-memory copy exactly as `OverWriteTexture` does: `IDirect3DDevice7::Load(cache texture, NULL, surface, NULL, 0)` (device `[orig+0x21FC8]`, vtable `+0xAC`; cache texture `[[obj + 0x1F54 + 4*i] + 0x3C]`, i = record `+0x60`; checked readable, logged with its HRESULT). Since the system-memory surface holds the redrawn page, any later refresh by the renderer (cache eviction) keeps it too. Builds clean, replay ALL PASSED, selftest PASSED; deployed for a third session.

## 2026-11-23: HD fonts step 4 done (third version confirmed in game)

**Sessions pid 21168 (5 F11: journal ×2, inventory, world with "Knife" tooltip and "Gained object: Knife") and 6532 (1 F11), user: "the fix is working as intended":** log per page: `slot 4E: 135 glyphs redrawn, 3 kept` → `written, cached copy 78 refreshed (Load 00000000)`, 4F 129 → cache 79, 50 115 → cache 80; face "Cardo" from the private file; phil and hbut at 23 px em (cap-matched would be 29 / 26 px). Screenshots at 1:1: journal title, quest names and entry text in Cardo Bold, ink 5C442C, smooth anti-aliased edges (so the text is alpha-blended, not alpha-tested: the open risk is closed); "Knife" tooltip in cream E0E0B0 with the dark outline; HUD labels ("Cut", "Cut Low"), the green `mgrn` message and other fonts in their original colours. No crash, clean unload.

**Seen for step 5 (tuning):** letters ~80% of the original height and loosely spaced (original widths); tops of some ascenders and J clipped by the original rectangles (visible in "Journal" and "Off"); "u" reads close to "n" at this size. No `LOST`/`OsTaskSwitch` lines in either session, so alt-tab is still unconfirmed with the redraw.

## 2026-11-23: alt-tab confirmed; text still too small at 4K; page scale now 1, 2 or 4

**User:** alt-tabbed to the other monitor and back during the step 4 sessions: the Cardo text stayed (the redrawn page lives in the system-memory surface the renderer reloads from). Verdict: "the text is still too small, it needs to be enlarged more for 4K".

**Change (build `c1d4efa0…`):** the page scale k is rounded up to a power of two (1, 2 or 4): `TextScale` 250 or 300 gives k = 3 by the old rule, and the renderer would have made a 1024² surface for a 768² page (resampled upload, and the redraw refuses a surface that is not 256k² A8R8G8B8). Offline at k = 4: Cardo Bold phil 46 px / hbut 44 px em, build checks PASSED. Test ini: `TextScale=300` (k = 4, text drawn at 3×, the 1024² page sampled at 0.75). Two levers for size: `TextScale` (all text and its layout grow; wrapping/overflow in fixed panels is the known risk from TEXT_SCALING_PLAN) and, independently, the ~20% the redrawn letters lose inside the original rectangles (needs new phil/hbut metrics).

## 2026-11-23: one text size per kind of text

**User, after a session at `TextScale=300`:** "much more readable at 300%", but a blanket 300% breaks or cuts off text in other places (options menu, journal tab); wants a separate size per major kind of text.

**Change (build `fffbfc02…`):** `[UI] JournalTextScale`, `TooltipTextScale`, `MenuTextScale`, `TitleTextScale`, `NameTextScale`, `SmallTextScale`, `NumberTextScale`, `CreditsTextScale` (percent, 50–600, 0/missing = `TextScale`). The text hook already scaled each font object on its own (`tx_scale_font(font, pct)`; glyph texel fix and spacing setter use the scale registered per font), so the kind is mapped from the font file: phil → Journal, hbut → Tooltip, magc → Menu, 16xB/X/Y → Title, mgrn/mred → Name, 5px* → Small, numg/numr → Number, cred → Credits (`g_fontKind[]` in td_file order). Logged per font (`text hook: loaded font … at N%`) and once at install. `g_gts` (tooltip box rules in ui_layout.inc) follows TooltipTextScale; without the text hook the proxy's tooltip stretching uses TooltipTextScale. The font page scale k uses the largest of all sizes. Stock=1 resets them. Builds clean, replay ALL PASSED.

**Test ini:** `TextScale=200`, `JournalTextScale=250`, `TooltipTextScale=300`, `NameTextScale=300`, the rest 0 (= 200, as before 300 was tried).

## 2026-11-23: HD fonts step 5: own metrics for phil and hbut (built)

**Why:** inside the original glyph rectangles Cardo only fits at ~80% of the original letter height, with the original widths (loose spacing) and clipped ascenders/J. The user asked to go ahead with taller cells and the face's own spacing.

**How (build `469ae7bc…`):**
- `src/font_pages.inc` (plain C, tested offline): `fpg_choose_em()` — the largest em whose letters/digits/J/Q/()/,;/ fit the font's original line box (tallest ascender + deepest descender ≤ line height L = 17) and whose 'H' is no taller than the original 'H'; baseline kept where possible (phil 14 → 13 at k = 2, 12 at k = 4; hbut 12). `fpg_new_cells()` — one cell per glyph: width = the face's advance rounded to texel units (letter spacing 0; ink up to one unit wider, e.g. f, is condensed), height = the ink inside the line box, the lowest cell stretched to L so the font's line height stays 17; ink offset = left bearing plus half the advance rounding. `fpg_occupy()` / `fpg_pack()` — first fit, tallest first, into the free space of pages 0x4E–0x51 with a 1-texel gutter around every glyph of the other 14 fonts and every new cell (summed-area table), never on a top-left corner another glyph already uses (texel-fix keys stay unique). `FpgGlyph.left1` places ink at a given offset instead of centring.
- `src/text_scale.inc`: two optional hooks, `tx_convert_hook` (called for every font before it is checked and scaled) and `tx_own_hook` (letter spacing 0 in `tx_fields` and the spacing setter). Null in the replay harness.
- `src/font_hd.inc`: `fpg_own_layout()` from `tx_install()` (after the font files are read, before any font is scaled): GDI metrics (`GGO_METRICS`), layout, pack; on success the originals are kept, `g_txFile` gets the new cells (so the "same / already scaled" checks and a second copy of the DLL work), texel-fix keys and the log decoder learn the new rectangles, the space width becomes the face's. `fpg_convert()` rewrites a loaded phil/hbut font object's records (and the direct table) from original to new, zeroes its kerning and spacing. The redraw clears the old rectangles of moved glyphs and draws each glyph into its new cell (pages 0x4E–0x51 now all redrawn). Off with `[Debug] FontOwnMetrics=0`; any failure (no face, no size, cells do not fit) leaves the original cells.
- Tests: `test_font_pages selftest` (+ packing case) PASSED; new `layout` mode with FreeType metrics from `font_preview.py --own-metrics`: k = 2 phil em 29 px, hbut 26 px, k = 4 phil 57 px, hbut 52 px, 380 of 380 cells packed, gaps/overlap/line-height checks PASSED; `build` into the new cells PASSED (nothing outside old+new phil/hbut cells changed, other 14 fonts untouched, ink in every cell, baseline). Replay ALL PASSED, imports unchanged.

**Offline result (Cardo Bold):** phil cap height 95% (k = 2) of the original instead of 78%, hbut 100% instead of 83%; spacing natural, some ±1-unit rounding left ("so me"); sample lines 5–10% longer than the original, so a bit more wrapping.

## 2026-11-23: tooltip boxes kept off the cursor

**User:** own metrics "look better"; tooltip boxes overlap the cursor at the larger text sizes.

**Cause (logs pid 23892, 24928):** the game places a cursor tooltip at a fixed offset from the cursor, made for its 46 px box: HUD/world tooltips at (-17, -38) / (-17, -34), i.e. the box's top 38 px above the cursor and growing downwards. With the text hook at 300% the box is 82 px tall, so it reaches 44 px below the cursor and covers the 200% cursor (64 px). Item tooltips in the inventory are centred 70 px below the cursor and were clear (box top +70, cursor bottom +62).

**Fix (ui_layout.inc, build `fbc4c267…`):** for every cursor tooltip (corner or centred item tooltip), after scaling, compare the box with the cursor sprite (32 × 32 from the sprite's top-left, × CursorScale; both get the same layout shift, so compared before it). If they overlap: corner tooltips move up to end 6 px above the cursor (below the sprite if there is no room above), item tooltips move down to start 6 px below it (above if no room). The on-screen clamp now adds to that shift instead of replacing it and also runs when only the shift is set. Replay test `tooltip box vs cursor at 300% text, 200% cursor` (above / below at the top / item unchanged) PASSED; all replay tests PASSED with the fonts (`SBM_GAME_DIR`).

## 2026-11-23: the COMBO label moved with the tooltip again (fixed by font)

**User:** tooltip-vs-cursor fix confirmed. New report with 4 screenshots (session pid 24796): hovering the column's COMBO slot, the slot's own "COMBO" label is pulled along with the "Combo screen" tooltip (seen above, inside or below the box's text) — the same symptom as the "magnified word" bug fixed on 2026-11-21 (DEVLOG, session 21:12).

**Cause:** that fix took a glyph as tooltip text only if its centre lay 14 px or more inside the box's top and bottom (one-line tooltips: text centres 21–25 px below the top of a 46 px box; slot labels at 9–10 px). With the text hook at 300% the box is 82 px tall, the band grows to 18–64 px, and the label (centre ~51 px below the box top) falls inside it. Since the box is now moved off the cursor, the label moved with it (the hover log shows it decoded into the tooltip's line: "Combo screen" + "COMBO", "CoCOMBOscreen").

**Fix (build `e0621eef…`):** with the text hook on, every glyph's font is known: the texel-fix keys now carry the font (`TxKey.font`, td_file index; -1 when two fonts share a rectangle; the own-metrics cells are tagged phil/hbut), and `tx_glyph_font(mat, u, v)` finds it from the glyph's texture corner (min tu, tv × 256). A cursor tooltip only takes text in its own font, hbut (`tip_has`, `TIP_FONT`); text of other fonts inside its box stays where the game drew it. Unknown font (no text hook, clipped glyph) falls back to the band rule. Replay test "tooltip text by font at 300%" (box moved −52 px, its hbut text with it, a 5px label inside the box stays) PASSED, and FAILS with the rule removed; harness gained `b` (vertices with tu, tv). All replay tests PASSED.

## 2026-11-23: text kinds checked against what the game draws; settings renamed

**User:** the per-kind setting descriptions are not accurate; some settings change the same text as others, which makes fine tuning hard.

**Checked:** every decoded `text:` line of 11 sessions (2026-11-21..23), counted per font and string (table in TEXT_SCALING_PLAN section 3). What was wrong in the ini: `TitleTextScale` ("decorative capitals") is in fact the game's main interface font 16xY/16xB: the whole options menu, the character screen's labels and numbers, inventory stack counts / gold / encumbrance and the HUD column labels — that is why the options menu broke at 300%. `SmallTextScale` (5px) is the low-resolution version of the same texts, only loaded below 1000 px screen width (`0x67FB79`), so it did nothing at 4K. `NumberTextScale` (numg/numr) was never seen drawing anything (all numbers seen are 16xY or magc). `Name` also covers the messages above the HUD ("Gained object", "Empty.", "Locked."). `Tooltip` also covers place names.

**Change (build `a6da3a1b…`):** kinds by font, 7 instead of 8: Journal (phil), Tooltip (hbut), Menu (magc), **Interface** (16xY, 16xB, 16xX and 5px*), Name (mgrn, mred), Number (numg, numr), Credits (cred). `InterfaceTextScale` replaces `TitleTextScale` (still read if Interface is missing); `SmallTextScale` is gone (5px follows Interface). The ini describes each kind by what was seen on screen. Limitation stated in the ini: a kind changes every place its font is drawn (e.g. Interface = options menu + character screen + inventory numbers + HUD column labels: one font object, 16xY, serves all of them). Replay ALL PASSED. Test ini migrated (Interface 200 from Title, Small dropped).

## 2026-11-23: names under the cursor left their tooltip box

**User (screenshots, session 18:57, TextHook 300% / names 300%, cursor 200%):** hovering NPCs messes up the tooltip: an empty dark box above, the name under the cursor ("Emma" with the cursor over the E), "Shopkeeper" split into "hopke ... eper" with the end 170 px to the right. ("Traveller" and "Innkeeper" show no box: it is still fading in; the frame captures have it.)

**Cause (frame captures 8-11):** the name is drawn in font mgrn inside the cursor tooltip box (box 82 px tall, top-left at +11,-6 from the cursor sprite). The box overlapped the 200% cursor, so it was moved above it (as meant); but since the COMBO fix only hbut text belongs to a cursor tooltip, so the mgrn glyphs stayed under the cursor, and those whose batch reached past the HUD column's edge were laid out as column (`*G`, moved right with the column).

**Fix (build `a03fcda5…`):** the fonts of a cursor tooltip are hbut, mgrn and mred (`TIP_FONT(f)`); 16xY/5px labels under a box are still left alone. Replay test "name under the cursor (mgrn) at 300%" (the Shopkeeper frame: box and both name glyphs move by the same (0,-82); fails without the fix). ALL PASSED.

## 2026-11-23: docs brought up to date; per-place sizes planned

**User:** NPC name fix works; update all docs, and plan for decoupling fonts in certain places for finer size control and legibility.

**Docs:** TEXT_SCALING_PLAN phase 5 status and a new **phase 5b, per-place sizes** (why, the constraint that measuring and drawing share the font object's metrics, candidate splits per kind, three approaches: per font object by load site, per screen, per drawing function with a copied font object; steps starting with a logging session; risks). ROADMAP (phase 3 in game ticked, phase 5 progress, phase 5b item, HD fonts step 5 next), HD_FONTS_PLAN (font table corrected from the font map: 16xY is the main interface font; step 5 checked in game, next fonts to redraw), README (features: experimental per-kind text and Cardo; planned per-place sizes; known issue: a size setting changes every place its font is used), CLAUDE.md (current state and next step), tests/README (the three tooltip tests at 300% and the `b` harness command).

## 2026-11-23: per-place fonts: every text object carries its own font pointer (phase 5b, static analysis)

**Why:** the user wants finer control over which text is enlarged (phase 5b, per-place sizes). Before the planned logging session, the font loading and font use were mapped statically in `SoulbringeVCnoeax.exe` (`objdump -d`, hash checked); every site listed below is byte-identical in `SoulbringeVC.exe`.

**Finding 1: the font of a text block is a field of its text object, `+0x208`.** Every function of the layout code reads the font from there: layout `0x432300` (9 reads), draw word `0x432820` (7), measure word `0x4345A4` (1), line start `0x434284` (4), auto-fit box `0x411634` (9). Draw glyph `0x433714` gets the font from draw word. So measuring, wrapping, box sizing and drawing all use the one pointer in `+0x208`; a text object pointed at a different font object is laid out and drawn consistently in that font. (Next to it, `+0x204` is the character-mapping table found in phase 1.)

**Finding 2: fonts are shared only because many places copy the same pointer into `+0x208`.** 109 instructions write `[reg+0x208]`; by the font they store:

| Font (object global) | Assignments | Functions |
|---|---|---|
| `16xy` (`0x35565DC`) | 28 | 25: `0x41E910` (×2), `0x424758`, `0x424CA0`, `0x429160`, `0x42CB10`, `0x430730`, `0x435650`, `0x437ED0` (×3), `0x43BBC0`, `0x43F770`, `0x443CD0`, `0x447020`, `0x4493D0`, `0x44CD50`, `0x617984`, `0x63E88C`, `0x648CA0`, `0x64D0B0`, `0x64EF20`, `0x656EF0`, `0x65DEB0`, `0x664610`, `0x66B2E0`, `0x670750`, `0x68A3A0` |
| `magc` (`0x213F3D8`) | 25 | `0x64541C`, `0x64EF20`, `0x65DFD0`, `0x664610`, `0x66B400` |
| `hbut` (`0x213ACF4`) | 13 | `0x42BEE0`, `0x42ED50`, `0x435320`, `0x437B20`, `0x43B880`, `0x43DEA0`, `0x442800`, `0x445960`, `0x4493D0`, `0x555140` |
| `16xb` (`0x213F3E0`) / `16xx` (`0x213F3E4`) | 4 / 2 | `0x41E910`, `0x424758`, `0x63E88C` |
| `phil`/`hbut` "current" (`0x1E4B06C`) | 3 | `0x648DC0`, `0x64D410` |
| `16xB` (`0x3559104`, its own object) | 1 | `0x45C158` |
| `numg` / `numr` | 1 / 1 | `0x45C468` |
| `mgrn` / `mred` (`0x213ACF0` / `0x213ACF8`) | 1 / 1 | `0x555140` |
| `hbut` (`0x213F3DC`) | 1 | `0x43BBC0` |
| from arguments, locals, fields, or 0 | ~30 | `0x41C63C`, `0x435650`, `0x5967A0`, `0x455934`, `0x5BE9D4`, `0x5EDBD4`, `0x5F1810`, others (not yet traced) |

The 26 direct `16xy` sites are one instruction each, the pointer read straight from the global: 24 × `8B 15 DC 65 55 03` (`mov edx,[0x35565DC]`) at `0x424765`, `0x424CE8`, `0x4291A8`, `0x42CE18`, `0x430798`, `0x43580B`, `0x437F1A`, `0x439618`, `0x43BC88`, `0x43F7E8`, `0x443D38`, `0x4471BB`, `0x449448`, `0x44CD98`, `0x617B48`, `0x648CE8`, `0x64D0F8`, `0x64EF68`, `0x656F38`, `0x65DEF8`, `0x664658`, `0x66B328`, `0x670798`, `0x68A418`; and 4 × `A1 DC 65 55 03` (`mov eax,[0x35565DC]`) at `0x43AA87`, `0x6409D2`, `0x41E939`, `0x41E9D0`. Each is followed within a few instructions by `mov [ecx+0x208],reg`, usually then `call 0x411634` (auto-fit).

**Finding 3: the menu panels are built once, at startup, into a global text-object table.** `0x4248C4` calls the panel builders one after another (`0x429160`, `0x42CB10`, `0x430730`, `0x437ED0`, `0x43BBC0`, `0x435650`, `0x43F770`, `0x443CD0`, `0x4493D0`, `0x447020`, `0x424CA0`). Each loops over its items: text object = `[0x3576878 + 0x94 × index]`, `+0x208` = `[0x35565DC]`, auto-fit. Which builder is which screen is not known yet (no strings in them; the text comes from the language data).

**Finding 4: `0x41E910` is most likely the HUD column move labels.** It loops over 10 items (`cmp ebx,0xA`) and picks the font per item state: `16xy` normally, `16xb` or `16xx` otherwise (state word `[esi+0x365B0C2]`). This matches the column labels seen in the logs (CUT in 16xY, the selected CUT LOW in 16xB). To confirm in game.

**Finding 5: several fonts already have more than one object.** Font loads (`0x681ED0`, 32 sites): `hbut` into 6 objects (`0x213F3DC`, `0x213ACF4`, the "current" `0x1E4B06C`, `0x1E4EF84`, and two built in objects at `0x67749B` and `0x687FC2`), `magc` 2 (`0x213F3D8`, `0x1E78684`), `phil` 2 (`0x1E4B06C` at `0x6483BA` and `0x66B48F`), `mgrn`/`mred` 2 each (`0x213ACF0`/`0x213ACF8` and fields `+0x14`/`+0x10` of an object at `0x674823`/`0x674891`), `cred` 3, `16xB` (`0x3559104`, separate from `16xb` `0x213F3E0`), `5pxy` 2 (`0x35565DC` and `0x6F1774` at `0x53179E`). Two loads go into stack locals (`0x4D0C15`, `0x4D764E`). The main set (`0x67F950`, called from `0x588324`) loads 16xy/16xb/16xx or 5pxy/5pxb/5pxx into the same three objects by screen width.

**Finding 6: nothing compares font pointers.** The other 16xy reads pass it as an argument to text-creating functions (`[esp+0x10]` next to `[0x35565E8]`: `0x58A4F2`, `0x648250`, `0x660176`, `0x66B4DD`, `0x66B65E`, `0x670921`, `0x670A16`, `0x689921`; `[esp]` at `0x657259`, `0x67B4E9`), call the letter-spacing setter (`0x67FBAA`, `0x67FC4F`, `0x67FCF4`: all three call `0x691F50` with −1 on the 16xy object), or are the load itself. No `cmp` against a font global was found, so a place given a copy of a font is not treated differently.

**Consequence for phase 5b:** approach 3 ("per drawing function") needs no entry/exit hooks. A place can be given its own font by changing the 4-byte address in its `mov reg,[0x35565DC]` to a proxy-owned pointer to a **copy of the font object** (same instruction length, original bytes verified, restored on unload). The copy is made when the game loads the font (`0x681F5E`, already hooked) and refreshed on every reload; the text hook already scales per object, so the copy gets the place's size, and the texel fix, spacing and own-metrics work per object. Approach 2 (rescale per screen) is not needed. Same pattern for `magc`, `hbut`, `mgrn`/`mred` sites; places that receive the font as an argument (the `[esp+0x10]` sites) need the argument site patched instead. Plan rewritten: TEXT_SCALING_PLAN phase 5b.

**Open:** which builder/site is which screen (options menu, character screen, inventory numbers, save list, conversations); whether the main fonts are loaded before `0x4248C4` builds the panels (the copies must exist by then); the ~30 assignments from arguments and fields.

## 2026-11-23: per-place fonts, step 1: logging build (built, not yet run in game)

**What (build `72667006…` SHA-1, `[Debug] FontPlaces=1`, needs TextHook=1):** every one of the 28 places that copy the interface font 16xy into a text object gets its own copy of the font object, still at InterfaceTextScale, so the screen should not change; with `Log=1` the log names the font object behind every new string, to fill in the place map.
- `src/font_places.inc` (plain C, shared with the replay harness): the site table (address, global, imm32 offset), `fp_copy` (deep copy: 0x30-byte object with the name shared, own records, direct table, kerning entries and pairs), `fp_refresh` (copies every site whose global holds the object just loaded), `fp_place`, `fp_spacing` (the copies follow the spacing setter). Copy memory comes from `VirtualAlloc` (new import) and is never freed: text objects may keep a copy's address after this DLL unloads.
- `soulbringer_modern.c`: the font-load thunk also passes 0x681ED0's frame, so `tx_after_load` knows the load site (`[ebp+4] − 5`, logged as "loaded (by XXXXXX)"); for an object a site reads, the copies are made **before** the original is scaled (fresh from the file), then the original and every copy are scaled by the text hook as usual ("copy for place XXXXXX" lines). The first time copies exist, each site's imm32 is changed to the address of its slot in this DLL (`g_fpSlot[i]` = its copy), after checking the opcode (`8B 15` / `A1`) and that the imm32 is the global (or an address outside the exe, left by an earlier copy of the DLL); a site that does not match is skipped and logged. All restored first thing in `tx_uninstall`. Until the first load every site reads the original, so no instruction can ever read an empty slot.
- Order checked in the last session log (pid 22388): no font is loaded before the text hook installs ("already loaded" never appears), and the panels can only take the pointer after the font object exists (`0x67F950` stores the global right before the load). If a place still shows text in the shared original, its log lines say `16xy obj … by 67FBA4`.
- Place log (`Log=1`): at add glyph (ebp = 0x433714's frame: `[ebp+8]` character code, `[ebp+0xC]` pen x, `[ebp+0x10]` pen y, `[ebp−0x20]` font, `[ebp−0x4C]` right edge) glyphs of one font object on one pen line join into a string (a gap over line height/3 = a space); each new (object, string) pair is logged once as `place: 16xy copy@4291A8 "…"` or `place: <font> obj <address> by <load site> "…"` (up to 7,000).
- `text_scale.inc`: registry raised to 128 font objects (originals + 28 copies).

**Tests:** new replay test "font places" (harness `O`): 28 deep copies equal to the original and unshared; original 200% / copies 150% and 300% scale independently (widths, line heights, kerning, setter spacing); a reload refreshes the copies in place and scales once; places map back; a shallow-kerning mutation fails; every site's bytes checked in both exes (a wrong site address fails). Replay **ALL PASSED**, `test_font_pages selftest` PASSED, build without warnings; imports: VirtualAlloc added.

**Replay fix (before this change):** the replay's tooltip-position checks did not know the 2026-11-23 rule that moves a box off the enlarged cursor, so the four text configurations failed 208 captures on the unchanged code (expected box 26–30 px lower than drawn; item tooltips 16 px). The checks now expect that move (corner tooltips end 6 px above the 200% cursor, below if no room; item tooltips start 6 px below it), and nothing else.

**Deployed** to the game root and `data\Stealth\Render` (SHA-1 of both = build); test ini: `FontPlaces=1`. Next: the session in `tests/README.md` ("per-place fonts"), then the place map.

## 2026-11-23: per-place fonts: first sessions (pids 23508, 27032); 10 more places; conversation portrait flicker

**Sessions (build `72667006…`, FontPlaces=1, 13 F11 in 27032):** no crash, nothing looked different; `font places: 16xy.fnt … copied for 28 place(s)` then `28 place(s) now read their own copy (0 skipped)` right after the 16xy load at `67FBA4`, before any panel drew. Load sites of every font are now logged (`loaded (by XXXXXX)`): hbut `6721CF` (place names), `67F559` (cursor tooltips, 23 strings), `67FB12`, `687FC2`; magc `67FA95` (main menu, save list, quit dialog **and** conversations), `673092`; mgrn `67F5DA` (names over characters), `674891`; mred `67F4D8`, `674823`; phil `66B48F` (a new object every time the journal opens); 16xb `67FC49` (selected column label "CutLow").

**Place map (16xy copies that drew text):**

| Place | Strings seen |
|---|---|
| `41E939` | HUD column move label "Cut" (normal state) — confirms `0x41E910` = column labels |
| `41E9D0` | HUD column "-" (empty slot) |
| `68A418` | HUD column "COMBO" |
| `4471BB` | options menu: Music, Sound, Speech, FX, Effects, Gore, Blood, Tool Tips, Delay, Run Always, Text, High, Off/On, 25/40, Load Save Quit Resume |
| `64EF68` | options menu values and buttons (same screen, second builder) |
| `443D38` | map screen: "Map of", "Madrigal", "20 5" |
| `4291A8`, `670798` | "20 5" (with the inventory/map; to place) |

**Still drawn with the shared original (`16xy obj … by 67FBA4`):** character screen (Strength Speed Combat Health Magic, Hit Points, Mana, Level, Experience, Next, Advancement and their numbers 100/100, 9/9…), inventory (Encumbrance 14/200, Weight, stack counts), journal/map titles ("Journal", "Map of Madrigal", the map description). These places get the font as an **argument**, not by the `+0x208` store: 8 call the text-object initializer `0x6899F0` (5th argument → `[ebx+0x208]` at `0x689A10`) with `[esp+0x10] = [0x35565DC]` (`A1 imm32` at `0x58A4F2`, `0x648250`, `0x660176`, `0x66B4DD`, `0x66B65E`, `0x670921`, `0x670A16`, `0x689921`; `0x660176`, `0x66B4DD/0x66B65E` and `0x670921/0x670A16` sit in functions called by the panel builder `0x429160`), and 2 pass it as the first argument of `0x674ED0` (`0x657259`) and `0x677350` (`0x67B4E9`).

**Change (build `5f238f7e…`):** those 10 sites added to `fp_site` (field `how` = 1: passed as an argument); same same-length imm32 redirect, 38 places in all. Place log: a word gap is now a gap over line height / 6 (with /3, hbut and magc strings lost their spaces: "Characterscreen"). Test: argument sites must be the global's read followed by `mov [esp(+d8)],eax`; 38 sites checked in both exes; replay ALL PASSED. Deployed (both copies SHA-1 = build).

**Bug report (user, for the conversation-screen step):** in a conversation, the NPC's portrait flickers for a moment when the NPC's voice line ends and the next dialogue box appears. Not investigated yet; likely one or more frames where the conversation screen is not recognised (the portrait panel drawn unmoved / unpillarboxed for a frame) or the portrait's 3D model is drawn before the panel it is grouped with. To check with a frame capture around the box change (F9 during the transition, or AutoCapture).

## 2026-11-23: inventory stack count hidden behind the item model

**User (sessions pids 22252, 11948; F11 of the apple at rest and while it turns under the cursor):** the small stack count of a stacked item ("2" for two apples) is drawn **behind** the item's 3D model: at rest only its outline shows; as the apple turns, more of it appears.

**Measured (frame captures 11 and 14 of pid 11948):**
- The count is text in 16xY (Interface, 200%), at game (stretched) x 2313, y 935, glyph 14 × 20 px; the menu pillarbox maps it to x ≈ 2215–2229, y 935–955: the **bottom-left corner of the slot**, about 10 canvas units in from the slot's left edge and 30 px above its bottom. The game places it in canvas units (it scales with the screen), not at a fixed pixel offset.
- The apple (material 0x3F, `0x48EA4B`, rhw ≈ 0.333) spans x 2185–2381, y 791–980 after layout as it turns, so it sweeps over that corner. Its shape is right (≈ 1:1 with the stem; the models already use the Hor+ projection), so this is not a stretch bug in the layout.
- Draw order: the model's triangles are added and the first `RenderAllBuffers` (#356, from `0x429F2A`) follows; the text batch (#359) is added after and flushed by the next three `RenderAllBuffers` (`0x42A1B5`, `0x42A9F6`, `0x42AB5A`). Text is submitted later, yet ends up under the model, so either the model bucket (2, also the cursor's) is not drawn by the first flush but later, or the text fails a depth test against the model's z. Not settled yet.
- The 200% text grows from its top-left corner, i.e. towards the centre of the slot where the model is, so the larger text makes the overlap worse than in the original (where a 9 px digit sits closer to the corner).

**Options (to decide with the user):** (a) check the unmodified game (`Stock=1`) to see whether the original also hides the count; (b) a renderer probe: which buckets each `RenderAllBuffers` draws, and the depth states of the text and model materials; (c) keep the enlarged count anchored in its corner (grow away from the slot's centre) once its place is known; (d) draw the count after the models (bucket or depth change for that text only).

## 2026-11-24: UI 3D models are a third too big on 16:9 (cause of the hidden stack count)

**Reference session (pid 11940, `Stock=1`, game at 1024×768 scaled to a 2880×2160 desktop, 12 F11):** in the unmodified game the stack count "2" is always visible: it sits just past the apple's lower-left edge (and to its right when the apple turns), never under it.

**Measured (frame captures, sizes as a fraction of the screen height):**

| | 1024×768 stock | 3840×2160 mod | ratio |
|---|---|---|---|
| Inventory apple (material 0x3F) | 52 × 52 px = 0.068 H | 196 px = 0.091 H | 1.34 |
| HUD column weapon icon (material 0x3D) | 52 × 14 px = 0.068 H | 197 × 53 px = 0.091 H | 1.33 |

In canvas units (640×480) the apple is 32.4 wide in the original and 43.5 in the mod; the stack count is at canvas x ≈ 386 in both. So the game places the count the same way at any resolution, but **sizes the UI 3D models by the screen width** (square pixels, scale ∝ W, not by the 4:3 height like the 2D UI): on a 16:9 screen they are W/(4H/3) = 1.33× too big next to their slots and grow over the count. The Hor+ patch does not cause it (it changes the world camera's xscale only); stock 16:9 has the same scale.

**Fix (build `44426a20…`, `ui_layout.inc` `ui_models`):** each grouped model is now also scaled about its centre by the local slope of the mapping it is moved by: 4H/3W (0.75 at 16:9) in the pillarboxed menus and in the narrowed HUD layouts (split, right, center); unchanged where nothing is narrowed (stretched HUD, Layout=0, 4:3 screens). With Layout=0 and an aspect cap, the slope is the capped area's.

**Test:** replay checks that every model triangle in a 16:9 menu is scaled by 4H/3W (mutation: without the scaling, 252 captures fail); ALL PASSED. Deployed (both copies SHA-1 = build); test ini back to `Stock=0`. To confirm in game: inventory with a stack, at rest and while hovering.

## 2026-11-24: the stack count comes from a shared text-object pool

**User:** stack counts are now visible (model fix confirmed); asks for the count to be a little larger.

**Session pid 10752 (place log):** two places draw a stack count "2": `copy@66B65E` when the inventory opens (together with "Encumbrance"), and `copy@670A16` when the item's slot is clicked (canvas 404,43, the top-left slot).

**`0x66B400` is a pool builder, not one place:** it loads phil into a new object (`0x1E4B06C`), then creates **64 text objects** in a global array at **`0x1E4AF64`** (entry 0 at `0x66B4DD`, entries 1–63 in a loop at `0x66B65E`, `cmp eax,0x40` at `0x66B702`), each with the 16xy font via the initializer `0x6899F0`. Screens use fixed entries (e.g. `0x1E4AF68` = entry 1 from 9 places, `0x1E4AF98` = entry 13 from 10; `0x648DC0` resets a flag at `+0x1B4` of entries 20–39 in a loop at `0x64CF98`). So "Encumbrance", the stack counts and the character values come from different entries of one pool created by one instruction: the per-site copy cannot tell them apart; a per-entry font (set on the pool object's `+0x208` once the pool exists) can.

**Draw word `0x432820` keeps its text object at `[ebp-0x10]`** and calls draw glyph from `0x432D3F` (returns to `0x432D44`), so the glyph hook can name the text object of every glyph.

**Change (build `699245c7…`):** `place:` lines now also give the text object, its pool entry (`pool#k`) and the pen position of the first glyph (`… text 0F3A1240 pool#23 at 2215,935 "2"`); the same string from another text object is logged again. Next: a session with stacks in several slots to find the stack-count entries, then a per-entry size for them.

## 2026-11-24: plan: every font controlled per place; static scan of magc, hbut, mgrn, mred

**User goal:** fonts independently controlled per place for fine tuning and legibility (e.g. the menu font for the main menu vs. conversations), not only the interface font. Agreed order: (1) static pass for the other fonts, (2) per-place size mechanism + offline tests, (3) one session naming every place, with first interface settings, (4) ini settings named by place. Stack counts (shared pool) stay deferred.

**Scan (reads of each font global, classified by the next three instructions):**
- **magc** `0x213F3D8`: 33 reads: 15 `mov [reg+0x208]` stores, 5 argument pushes, 1 write (the load), **12 other**: `mov edx,[0x213F3D8]` at `0x64575F`, `0x6459B6`, `0x645C73`, `0x645EEC` (fn `0x64541C`) and `0x6649F7`, `0x66501F`, `0x66525B`, `0x665F9A`, `0x66639F`, `0x6665FF` (fn `0x664610`), and `mov ecx,[0x213F3D8]` at `0x549F7B` (fn `0x549E40`), `0x54A524` (fn `0x54A410`): the `edx` ones look like stores to `+0x208` further on; the `ecx` ones call a method on the font. Second object `0x1E78684` (loaded at `0x673092`): 2 argument, 2 write, `mov ecx` at `0x6085D1` (fn `0x608550`).
- **hbut** `0x213ACF4`: 19 reads: 13 store, 2 argument, 2 write, other at `0x554CD3` (fn `0x554C90`) and `0x67F352` (fn `0x67F2F0`). `0x213F3DC`: 1 store, 2 argument, 1 write.
- **mgrn** `0x213ACF0` / **mred** `0x213ACF8`: 5 reads each: 1 store, 2 write, other in the same two functions `0x554C90` and `0x67F2F0` (which read all three of hbut/mgrn/mred: probably reload or spacing for the cursor-tooltip fonts).

Nothing new is needed for these beyond the 16xy method (copy per site, imm32 redirect); `fp_site` becomes a table over any font global.

## 2026-11-24: per-place fonts, step 1 done: magc, hbut, mgrn, mred sites (92 places)

**Classification of the remaining reads (static, both exes):**
- magc: the 10 `mov edx,[0x213F3D8]` in `0x64541C` / `0x664610` are stores: `imul eax,eax,0x94; … mov ecx,[eax+0x35767E4]; mov [ecx+0x208],edx; call 0x411634` (the panel table, 5 instructions on: the first scan looked at 3). Two reads **measure** a list with the font itself: `0x549F7B` (fn `0x549E40`, called from `0x5EDBD4`: reads the font's spacing `+0x2C`) and `0x54A524` (fn `0x54A410`, reached by pointer: `call 0x691810` on the font), both looping to `[0x213F448]`. They must use the same copy as the place they measure for.
- Every argument site passes the font to a text-object initializer (`0x6899F0`, or `0x691C00` for `0x635CB8` / `0x6360AC`), including those inside the loaders (`0x6800EF`, `0x68017B` in `0x67F950`; `0x67F3CB` in `0x67F2F0`; `0x67324E` in `0x672FE0`), which create text right after loading.
- **Not redirected** (they must see the original): the loads/writes of the globals, and the font lifetime code: delete-and-null in `0x554C90` (hbut, mgrn, mred: `call 0x692180` with 1) and `0x608550` (magc2), the existence checks in the loader `0x67F2F0`.

**Site table (`font_places.inc`, 92 entries):** 16xy 38 (unchanged), magc 32 (25 store, 5 argument, 2 measuring), magc2 `0x1E78684` 2 (argument), hbut `0x213ACF4` 15 (13 store, 2 argument), hbut2 `0x213F3DC` 3 (1 store, 2 argument), mgrn 1, mred 1 (stores in `0x555140`). Opcode check generalised to `8B /r` with mod 00 rm 101 (`8B 05/0D/15/…`) and `A1`. Each font's copies are made when that font loads (any global), so the redirects of each font go in at its first load. The own-metrics conversion of hbut keeps its state per font, not per object, so the hbut copies reuse the same new cells. Memory: 92 copies, about 11 MB (VirtualAlloc, kept for the process); font registry raised to 256.

**Measuring sites:** given their own copy for now (all copies are at the font's size, so nothing can mismatch); before sizes differ, each is grouped with the place it measures for (from the session).

**Tests:** replay font-places test: 92 sites' bytes in both exes by kind (store to `+0x208`, argument push of any register, measuring use), sites of unknown globals rejected; copies made for the 16xy sites only in the offline font test; mutation (a wrong magc address) caught. Replay ALL PASSED, build without warnings. Deployed `a8793d49…` (both copies). Next: step 2, the per-place size mechanism.

## 2026-11-26: per-place fonts: first sessions with all 92 places (pids 26656, 17356, 8080, 25552; Stock 12648)

**Sessions (build `a8793d49…`, deployed 2026-11-24; ini: TextHook, FontPages, FontRedraw=phil,hbut, FontPlaces=1, HudLayout=right; Journal 250, Tooltip 300, Name 300, the rest 200):** in all four, `92 in all, 0 skipped`: every place reads its own copy from the first load of its font, no crash, clean unload. Pid 12648 ran with `Stock=1` (no copies, 256² font page dumps). No F11 in these sessions.

**New places that drew text** (`place:` lines; 16xy as before):

| Site | Font | Strings |
|---|---|---|
| `6649F7`, `6665FF`, `66639F`, `66501F`, `66525B` | magc | main menu: New Game, Load Game, Options, Credits, Quit (one place per button) |
| `665F9A` | magc | main menu copyright line |
| `64575F`, `6459B6`, `645C73`, `645EEC` | magc | save list: save name ("at the inn", "first save"), "Level", the level number, the date |
| `66EADB`, `66EC3B`, `6704BB` | magc | single numbers ("5", "20", "6"), screen not identified |
| `5567BE` | hbut | HUD and world cursor tooltips (Compass, Combo screen, Inventory screen, Hit Points 100/100, Cut Low…) |
| `556950` | hbut | item names under the cursor (Bread, Fish, Knife, Mug of Ale) |
| `43BB1F` | hbut | "Character screen", "Journal" |
| `42C23E`, `42F01E` | hbut | character/equipment labels: Armour Value, Gold, Held Weapon, Ring, Back; Hit Points, Mana Points |
| `556C6D` | mgrn | names of characters under the cursor (Innkeeper, Shopkeeper, Stranger, Traveller) |

Still on their own (non-shared) objects, as expected: hbut `6721CF` (place names "The Eastern River", "Madrigal Inn"), phil `66B48F` (journal), 16xb `67FC49` (selected column label "Cut Low"; 16xb has no sites yet, so a separate HUD-label size would not cover the selected label). Not reached: a conversation, the quit dialog, mred messages.

## 2026-11-26: per-place fonts, step 2: a size per place (built)

**What (`[PlaceTextScale]`, with TextHook=1 and FontPlaces=1):** every place has a size, `g_fpPct[i]` in `font_places.inc` (percent, 50–600; 0 / missing = the size of its font's kind, e.g. InterfaceTextScale for 16xy). The ini key is the site address as the log names the place: `4471BB=150` for `copy@4471BB`. Names by place (step 4) come after the naming session.
- `font_places.inc` (plain C, shared with the harness): `fp_pct(i, kind)` (the size a copy gets), `fp_set_pct(site, pct)` (by address; clamps; refuses an address that is not a place), `fp_max_pct()`, `fp_tip_pct(tipKind)` (largest of TooltipTextScale and the places that draw cursor tooltips and their text in `0x555140`: hbut `5567BE`/`556950`/`556B33`/`556C88`, mgrn `556C6D`, mred `556BAA`).
- `soulbringer_modern.c`: `fp_config()` reads `[PlaceTextScale]` at load (only with FontPlaces and TextHook; Stock=1 turns FontPlaces off first); `tx_do_font_at(font, where, place)` scales a copy at `fp_pct(place, kind)` when it is made and on every reload (the copy is rebuilt from the unscaled original first, so it is scaled once); the log line says `at N% (place size)` when the size is the place's own. The font page scale k takes the largest place size into account; the tooltip box rules (`g_gts`) use `fp_tip_pct`; the text hook installs if only a place size differs from 100%. Startup log: the sizes set (`font places: N place size(s) set …`), and a note if menu-font places are sized but the two measuring places (`549F7B`, `54A524`) are not (they measure a magc list with the font itself and must match the place that draws it; that list is not identified yet).
- Nothing changes without `[PlaceTextScale]` entries: every copy is at its kind's size, as in `a8793d49`.

**Why it is consistent:** a place's measuring, wrapping, box sizing and drawing all read the one font pointer in its text object (`+0x208`), which is the copy; the texel fix maps any quad size to the glyph's rectangle, and the spacing setter follows each copy's registered size, so a copy at its own size needs nothing else.

**Tests:** new replay test "place sizes" (harness `J`): 16xY at 200% with `4471BB=150`, `66B65E=300`, `41E939=200`, `68A418=20` (→ 50), `443D38=900` (→ 600), `64EF68=0`, `5567BE=350` and an unknown address (refused); every 16xy copy at its place's size, the rest at 200%, the original at 200%; widths 20/26/39 px at 150/200/300% (raw 13), line heights 23/30/45, spacing −2/−2/−3; same after a reload; page-scale size 600, tooltip-rules size 350; mutation (copies at the kind's size) caught. Replay **ALL PASSED** (209 checks), `test_font_pages selftest` PASSED, build without warnings, imports unchanged.

**Build:** `701a3b62…` (SHA-1; Ubuntu clang 18.1.3). lld-link writes the build time into the PE header (one byte apart between two builds of the same source), so every build has a new hash: a hash names a file, not a source state.

**Next (step 3):** one session with every screen (checklist in `tests/README.md`), first sizes tried for known places; then step 4, settings named by place.

## 2026-11-26: HUD column highlighted labels: 16xb and 16xx places (98 places)

**Why (user):** the selected move label in the HUD column ("Cut Low") is drawn in 16xB, not 16xY, so it had no place: a separate HUD-label size would have resized only the unselected labels.

**Static scan (both exes, byte-identical):** 12 references to `16xb` (`0x213F3E0`) and `16xx` (`0x213F3E4`):
- **6 stores to a text object's `+0x208`** (the same pattern as the other places):
  - `0x41E910` (HUD column labels): loop 1 over 5 slots (`[0x357DC10]+i`): the selected move (`i == [0x6B80E4]`) gets 16xb at **`41E960`**; state 1 (`[0x365B0C2 + i*0x208] == 1`) gets 16xx at **`41EA06`**; otherwise 16xy (`41E939`). Loop 2 (`[0x357DC14]+i`): selected → 16xb at **`41E9A8`**, else 16xy (`41E9D0`).
  - `0x424758` (sets a menu-table text's font by state: 0 → 16xy `424765`, 1 → 16xx at **`42478B`**, 3 → 16xb at **`4247B1`**); called 10 times from `0x63E3A7`–`0x63E672`, a screen not seen in a session yet.
  - `0x63E88C`: 16xb for entries of the shared text-object pool (`[0x1E4AF6C + 4i]`) at **`63F1E2`** (same unseen screen).
- **6 loader reads** in `0x67F950` (`67FC38`, `67FCDD`, `6803C0`, `6803EC`: `mov ecx,[global]; call 0x681ED0`, the load into the font object; plus the two writes `67FC12`, `67FCB7`): not redirected (the load must fill the original, whose copies are then refreshed).

**Change (build `5c61e9be…`):** the 6 stores added to `fp_site` (98 places). 16xb/16xx are Interface fonts, so their places fall back to `InterfaceTextScale` like 16xy's. Nothing else changes: copies are made when 16xb/16xx load (`0x67F950`, right after 16xy), refreshed on reload, scaled per place.

**Tests:** replay font-places test now knows the two globals and checks all 98 sites' bytes in both exes (mutation: `41E961` instead of `41E960` fails in both exes); replay **ALL PASSED** (209 checks).

**Expected in the log** (FontPlaces=1, Log=1): `16xB.fnt … copied for 4 place(s)`, `16xX.fnt … copied for 2 place(s)`, `98 in all, 0 skipped`; the selected label as `place: 16xB.fnt copy@41E960 "Cut Low"` instead of `16xb.fnt obj … by 67FC49`.

## 2026-11-26: text sizes by screen: named settings replace the address list (phase 5b step 4, first part)

**User:** the `[PlaceTextScale]` list (98 addresses) is far too complex and not intuitive; asked for a simpler way. Agreed: one named setting per screen or area in `[UI]`, the addresses hidden in the DLL; and before that, the HUD column's highlighted labels (16xb/16xx, entry above) and the own-object fonts.

**Own-object fonts, sized by load site:** some text has a font object of its own instead of a copy of a shared one, so it has no site to redirect. Place names: hbut loaded at `6721CF` into the object at global `0x1E4EF84`, made in `0x6720E0`; the only reads are the two place-name texts (`0x672226`, `0x6727D6`, font as an argument) and the delete at `0x61746D`. So scaling that object where it is loaded sizes place names and nothing else. `fp_load[]` (font_places.inc) lists such load sites; `tx_after_load` scales the original at `fp_load_pct(site, kind)`. (Journal phil is alone in its kind, so JournalTextScale already covers it; the second magc/mgrn/mred objects are not identified yet.)

**Named groups (`fp_group[]`, font_places.inc):** 10 settings in `[UI]`, each a list of places (sites or load sites) identified in sessions:

| Setting | Places | Falls back to |
|---|---|---|
| MainMenuTextScale | 6649F7 6665FF 66639F 66501F 66525B 665F9A | Menu |
| SaveListTextScale | 64575F 6459B6 645C73 645EEC | Menu |
| OptionsTextScale | 4471BB 64EF68 | Interface |
| CharacterTextScale | 66B65E 66B4DD 660176 670A16 (shared pool: stats, Encumbrance, stack counts) | Interface |
| MapTextScale | 443D38 67B4E9 4291A8 670798 | Interface |
| HudLabelTextScale | 41E939 41E9D0 68A418 41E960 41E9A8 41EA06 | Interface |
| LabelTextScale | 42C23E 42F01E 43BB1F | Tooltip |
| PlaceNameTextScale | load site 6721CF | Tooltip |
| NpcNameTextScale | 556C6D | Name |
| MessageTextScale | 556BAA | Name |

0 / missing = the places follow their kind (as before); places in no group follow their kind too (conversations, quit dialog, cursor tooltips, the unseen screens).

**DLL (`soulbringer_modern.c`):** `fp_config` reads the group keys from `[UI]`, then `[PlaceTextScale]` (kept as a developer override by address, applied after the groups, no longer in the shipped ini). Any size set turns the font copies on by itself (`g_cfgFontPlaces = 2`; `[Debug] FontPlaces=1` is now only for copies without sizes plus the `place:` log). Log: `text sizes by place: OptionsTextScale=150 …`, overrides listed separately; the copies' `at N% (place size)` lines as before. The font page scale and the tooltip rules see group sizes like any place size.

**Ini (shipped and the test copy):** the address section removed; after the per-kind sizes, a "Text size per screen or area" block: the 10 settings at 0, each with a one-line description and its fallback kind in brackets; the FontPlaces comment says it is for developers. The test ini keeps all its values (Journal 250, Tooltip 300, Name 300, the rest 200, HudLayout=right, FontPlaces=1).

**Tests:** new replay test "place groups" (harness `I`): the table (32 places in 10 groups, every place known, none in two groups, keys unique and not per-kind keys, each key in the shipped ini as `<Key>=0`, no address list there); HudLabel 250 / Options 150 / PlaceName 300 / Message 0 / an unknown key: each group's places at its size, every other place 0 (= kind), the override `4471BB=175` beats OptionsTextScale, place names by load site; largest size 300; tooltip rules follow NpcNameTextScale=320; mutation (no group lines) caught. Replay **ALL PASSED** (210). Build `81046ece…`.

**Next:** step 3, the naming session with first per-screen sizes (checklist in tests/README), then ConversationTextScale and the quit dialog.

## 2026-11-26: enlarged labels broken onto two lines: the auto-fit's own width sum (fix built)

**User (screenshots, session 20980, Interface 200%):** in the options menu the text "overlaps" and breaks into lines although the panel has plenty of room ("TOOL / TIPS", "TEXT / DELAY", "ALWAYS / RUN", "SOUND / FX"); the same on the stats page ("HIT / POINTS", "NEXT / EXPERIENCE") and the HUD column. In the stock game (1024×768, session 11940) these are one line.

**Logged (build `8298a08b`, `textobj` lines: the text object's layout fields for every new string; sessions 23992, 5488):** every such label has sizing mode `+0x58` = 2 with target width `+0x5C` = 3840, clip rect = the whole screen (set by the screen code after the auto-fit, e.g. `0x4471EB`), and an auto-fitted box: "Tool Tips" 75×64 (two lines), "Sound FX" 91×64, HUD "Cut Low" 55×64, stats "Hit Points" 91×64. So no fixed width limits them: the auto-fit chose two lines.

**The auto-fit `0x411634` (static):** it measures the whole text itself (`[ebp-0x1C]`, full one-line width) and the longest word (`[ebp-0x54]`), then lays the text out with `0x413A64` (thiscall(text, width, int* outW) → height, ret 8) at `+0x1C8` (6) trial widths from the longest word to the full width (capped to the clip width), and picks one: mode 1 the width/height ratio closest to `+0x50`; mode 2 (`0x4137E0`) the width closest to `+0x5C`; mode 3 fixed width. Then box = the pick, `0x621774(text)` (offset for the alignment/anchor `+0x70`), `0x585050(text)`. In mode 2 with a 3840 target the widest trial (one line) should win; it does at 100%. With enlarged fonts the auto-fit's own sum comes out narrower than what `0x413A64` needs for one line (the scaled metrics are summed differently by the two), so the "full width" trial wraps too, and every trial is two lines.

**Fix (build `73ab083c`, with TextHook):** `0x411634` entry (`55 8B EC 81 EC A0 00 00 00`, 9 bytes, checked in both exes, restored on unload) jumps to `tx_fit_thunk`: the original runs, then `tx_after_fit(text)`: for sizing mode 2 and a font this DLL enlarged (registered at ≠ 100%), lay the text out with the game's own `0x413A64` at the target width (at most the clip width); if that is one line (height ≤ one line step = font `+0x18` + `+0x20` + text `+0x1DC`) and lower than the game's box, the box becomes (0, 0, layout width, one line) and `0x621774` / `0x585050` run as after the game's own pick. Since the width comes from the game's layout, drawing (same word measure) keeps it on one line. Fonts at 100%, modes 1 and 3 (tooltips are mode 1, target 200), narrow targets and text that cannot be one line are left alone. Log: `autofit: text … "Tool Tips" (16xY.fnt at 200%) box 75x64 -> 84x32 (one line)`. The decision is `tx_autofit_choice` in `text_scale.inc` (plain C).

**Where it applies (from the logs, all mode 2, target 3840):** options menu, stats page labels and values, HUD column labels (incl. the selected one), main menu, save list, inventory numbers, the quit dialog. 

**Also found in these sessions:** the **quit dialog** places: magc `6549BF` ("Quit the game?"), `654D5D` (Yes), `6550E3` (No), and a second copy `66569A` / `665A36` / `665DBA` (from the main menu). The save-list date is cut with "..." inside a one-line box (199×40): a different mechanism, not this fix.

**Tests:** new replay test "auto-fit fix" (harness `E`, a greedy word-wrap stand-in for `0x413A64`): "Tool Tips" 75×64 → 84×32, "Sound FX" → one line; unchanged at 100%, in mode 1, with a narrow target, for text wider than its clip, for a box already one line; mutation (one-line guard off) caught. Replay **ALL PASSED** (211).

## 2026-11-26: interface font redrawn in layers from Montserrat ExtraBold (`[UI] InterfaceFont`, built)

**User:** the interface font (options menu, combo screen, stats page, Encumbrance and "14/200") stays ugly at any size; asked whether upscaling it would help, then picked a redraw.

**Why not upscale (offline previews, `~/fontprev`):** EPX 4x and a per-colour smoothing of the original glyphs were barely better than today: the letters are 8×9-texel tiles with two rings, the letter body about 5 texels tall, so there is no detail to recover ("4" stays mangled). Redraws from TrueType faces (Montserrat ExtraBold, Fira Sans Bold, Bitter Bold, Cardo Bold) in the game's colours were clearly legible; **user picked Montserrat ExtraBold**.

**How the pixel fonts are built (from the page dumps, ring analysis):** 16xY: yellow strokes (F4E000), 1 texel wide, in a red/orange glow (FC2C2C, F09010) that fills the counters, a magenta ring (94205C/AC2C68), a dark border (1C0000). 16xB: light purple strokes (6060AC) in dark purple. 16xX: the letter is cut out (transparent) in a dark grey ring. All three keep their 202 records (lowercase codes have their own rectangles holding capitals).

**Design:** redraw into the **original rectangles** (no own metrics): the game's layout, advances and spacing stay exactly as they are; only the letters change. `font_pages.inc`:
- `fpg_ui_colours()`: per font, rings of the original A–Z texels (0 = touches transparency, 1 = touches ring 0, 2 = core); outer = darkest of ring 0's three most frequent, line = the brighter of ring 1's top non-core colours, face/shade = the two most frequent core colours (brighter on top), cut-out = more enclosed transparent texels than core texels. Results: 16xY F4E000 / FC2C2C / 94205C / 1C0000; 16xB 6060AC / 58549C / 44145C / 04040C; 16xX cut out, 2C2C2C / 0C0C0C.
- `fpg_glyph_layers()`: outer ring (outerPx), line ring (linePx), the letter in the shade colour, the face over it where the letter also covers the texel shadePx below (a band of shade along the bottom of every stroke); cut-out fonts clear the letter from the rings. Condensed if letter + rings is wider than the rectangle; nothing written outside it.
- `fpg_ui_em()`: em size with 'H' as tall as the original 'H' rectangle minus the rings at both ends; `fpg_ui_code()`: lowercase (a–z, Latin-1) drawn as capitals.

**DLL (`font_hd.inc`, `soulbringer_modern.c`):** `[UI] InterfaceFont=1` turns FontPages on (and page scale ≥ 4 unless FontPageScale is set); after each upload of 0x4E–0x51 the slot's 16xY/16xB/16xX glyphs are redrawn with GDI from `SoulbringerModern\fonts\Montserrat-ExtraBold.ttf` (FR_PRIVATE; face "Montserrat ExtraBold", weight 800; fallbacks Arial Black, Segoe UI Black, Tahoma; `[Debug] InterfaceFontFace/Weight/File`), rings and shade (k+1)/2 px each (2 at k = 4). The colours come from the letters on page 0x50, so the redraw waits until all four pages were uploaded once (or 600 renderer calls); colours are kept per font. Independent of FontRedraw (journal/tooltips). Stock=1 turns it off. Log: `interface font: letters at N px em …`, the colours, `interface font: slot XX: N glyphs redrawn`.

**Font file:** a static instance of Montserrat[wght] at wght 800 (fontTools instancer, names updated: family "Montserrat ExtraBold"), `dist/fonts/Montserrat-ExtraBold.ttf` + `Montserrat-OFL.txt` (SIL OFL 1.1, no Reserved Font Name), packed by `make_release.sh`.

**Tests:** `test_font_pages selftest` gained layer checks (face on top, shade band at the bottom, line and outer rings, nothing outside the rectangle, cut-out transparent, no-ink → untouched, lowercase → capitals); new mode `test_font_pages ui` (via `tools/ui_font_preview.py`, FreeType glyphs at the em the DLL picks) on the real pages: colours as above, 570 glyphs drawn, all with ink, no texel outside the three fonts' rectangles changed — UI PASSED. Replay ALL PASSED (211). Build `8a8da356`. Shipped ini: `InterfaceFont=0` until checked in game; test ini: 1.

## 2026-11-26: menu font magc redrawn into its own cells (`[UI] MenuFont`, built)

**User:** after comparing uncial faces (Uncial Antiqua, MedievalSharp, Morris Roman, Eagle Lake), Eagle Lake is the top contender ("closest to the original"); MedievalSharp as a secondary option, switchable in the ini. Morris Roman's licence (1001fonts EULA, sections 3–4) allows private use but not publishing or distributing it, so it is not bundled; `MenuFont=Custom` lets a private build use it.

**magc, as the files and page dumps show it (reverse engineering, offline):** `data\magc.fnt` has 192 records, 191 drawn (codes 33 and up, Latin-1 included) plus the space (no page, advance 4 texels). The glyphs are spread over three pages: 24 on 0x4E, 116 on 0x4F, 51 on 0x50 (none on 0x51). Line height 19 texels; 'H' 11×10 at y offset 4 (so the baseline is 14 texels below the line top), 'x' 8 high at y offset 8, 'g' 11 high at y offset 8 (descenders 5 texels). The letters have no outline: yellow strokes (228,228,40, 41% of the drawn texels) shading through olive (152,136,40), brown (128,96,24; 104,60,12) to dark brown (64,32,4; 36,24,8) at the stroke edges, i.e. anti-aliasing baked into the palette indices. In the exe the font object is `0x213F3D8` with a second object `0x1E78684` (TEXT_SCALING_PLAN section on per-place fonts); td_file index 10.

**Why own cells (not the original rectangles):** magc has ascenders and descenders (line 19, 'H' 11×10, x-height 8), and an uncial face's letters differ in width from the original's; squeezed into the old rectangles they would be condensed. Same treatment as phil/hbut (step 5).

**DLL:** own metrics generalised from 2 to 3 fonts (`FPG_OWN_N`; td_file 8 phil, 9 hbut, 10 magc), each with its own face: Cardo for phil/hbut (`FontRedraw=phil,hbut`, as before, the two together), the menu face for magc (`[UI] MenuFont`: EagleLake 400 / MedievalSharp 500 / Custom from `[Debug] MenuFontFace/File/Weight`; no stand-in face: if GDI cannot give it, magc keeps its letters). `g_fpgOwn` is a bit mask; fonts not converted keep their rectangles occupied, the cells of all converted fonts are packed together; the redraw first clears every moved glyph's old rectangle, then draws each font from its own face. Colours per font from the first uploaded page that holds its letters (magc lies on 4E/4F/50: bright yellow fill, darkest brown edge of k/2 px at alpha 200); when a converted font gets its colours, all pages seen so far are redrawn; with MenuFont the redraw waits until all four pages were uploaded (as the interface font does). MenuFont needs the text hook (else logged and off), turns FontPages on and page scale ≥ 4.

**Offline (k = 4, PIL metrics):** Eagle Lake em 52 px (baseline 13), MedievalSharp em 53 px (baseline 14), line 19 kept, 190 cells each; with phil/hbut (Cardo) 570 of 570 cells packed. Eagle Lake comes out larger than the original letters (its x-height is high for its cap height) and its "Q" tail (72 px ink on a 48 px advance) widens the Q cell, which leaves a visible gap in "Quit"; to judge in game (MainMenuTextScale can make the menu smaller).

**Files:** `dist/fonts/EagleLake-Regular.ttf`, `MedievalSharp-Regular.ttf` (unmodified) + OFL texts with each font's copyright line; `make_release.sh` ships them. Shipped ini: `MenuFont=EagleLake` (active only with TextHook=1).

**Build:** `cdbc346c`.

**Tests:** `test_font_pages layout` reads "SBM3" metrics (any of phil/hbut/magc); `tools/menu_font_preview.py`; replay test "menu font".

**Open (to check in game):** menu size with Eagle Lake (larger than the original letters at the same cap height); the Q gap in "Quit"; that conversations (also magc) still look right; the save-list date and the copyright line.

### Eagle Lake with lining figures ("Soulbringer Uncial", build `48ec9c47`)

**User:** Eagle Lake's digits are offset ("I cannot unsee it"); can the font be changed? Eagle Lake has only old-style figures (3 5 7 9 hang to -520 units, 1 is x-height 1180; 0 2 4 6 8 run about -17..1568, units per em 2048) and no `lnum` alternates (GSUB: aalt, frac, liga, ordn, sups). The OFL allows modified versions, but "Eagle Lake" is a Reserved Font Name, so the result is renamed.

**`tools/make_menu_font.py`** (fontTools): 3 5 7 9 moved up and fitted to the 0/2/4/6/8 span (-17..1568), 1 stretched upwards from the baseline to 1568; the digits are simple glyphs without instructions and no composite uses them, so nothing else moves. Names: family "Soulbringer Uncial", PostScript `SoulbringerUncial-Regular`, description (name 10) says what changed. `SoulbringerUncial-OFL.txt` keeps the Eagle Lake copyright line plus a modification note. `MenuFont=EagleLake` now loads this font; `EagleLake-Regular.ttf` stays in `dist/fonts` as the source, not shipped. Replay test "menu font" checks the family name and that all ten digits are lining.

### Docs pass (2026-11-26, evening)

All docs brought up to date with the menu font: HD_FONTS_PLAN (status, section 2 item 7 "magc", design, step 5), TEXT_SCALING_PLAN (phase 7 no longer says "magc enlarged only"), CLAUDE.md (agreed order), ROADMAP. Correction: earlier notes in this entry and a code comment said magc lies on pages 4F/50; it lies on 4E (24 glyphs), 4F (116) and 50 (51). Only comments changed in `font_hd.inc` (the colours always came from the first page holding magc letters), so build `48ec9c47` in the game folder is the same code.

## 2026-11-26 (night): copyright line and save-list date with enlarged text (build `93bd4461`)

**User (session pid 24680, Eagle Lake menu font on, Menu 200%):** "It looks pretty good." Two things to fix: the copyright line at the bottom of the main menu is not at its original place (reference screenshot `Reference main menu.bmp`, 1024×768), and the save list cuts the date and time with "..." ("07/16/26 21:2...", "11/20/26 17:1...") although the panel has plenty of room.

**Copyright, reverse engineering** (main-menu builder around `0x665D00`–`0x666180`, place `665F9A`):
- `0x665E14`: the line is created by `0x62C45C(ecx = text, eax = x, edx = y, arg4 = width limit)` with x = W·512/768, y = H·480/512, limit = W·500/768 (W = `[0x367370C]`, H = `[0x3673708]`: the game's screen width and height, 3840 × 2160 here).
- `0x665F9A`: font set to magc (`0x213F3D8`, our place copy), auto-fit `0x411634`, mode `+0x70` = 6, auto-fit again.
- `0x665FF2`: `0x549FC4(text, &rect)` reads the text rectangle into `[ebp-0x49C]` (left, top, right, bottom); then **x = W − w·W/768, y = H·510/512 − h·H/512** (constants `0x36A3D38` = 1/768, `0x36A3D40` = 768, `0x36A3D48` = 1/512, `0x36A3D50` = 510), stored at `0x666143` (`+0x64`) and `0x66614F` (`+0x60`).
- At 1024×768 that is x = 1024 − 1.33·w: with the original w ≈ 365 the line ends at x ≈ 900, as in the reference (ink 536..897 × 739..753). At 3840 wide the factor is 5: w 720 (original letters at 200%, session 16048) gave x = 240, w 774 (Eagle Lake, session 24680) gave x = −30, drawn at the left edge of the 4:3 area. So the formula only works at 1024×768; the larger the text and the screen, the further left.

**Fix:** a jump at `0x666143` (15 bytes, original bytes checked, restored on unload) to `tx_copy_thunk`: x = W·897/1024 − w, y = H·756/768 − h (`tx_copyright_xy`, text_scale.inc): the right edge and bottom where the 1024×768 game has them (rectangle bottom = ink bottom + 3). 4K, w 774: x = 2589 (right edge 0.876 W, as the reference); 1024×768, w 365: x = 532 (game: 537). Installed with the text hook.

**Date, reverse engineering** (save list, places `64575F` name, `6459B6` "Level", `645C73` level, `645EEC` date):
- `0x645DC5`–`0x645DEA`: the date/time string is formatted (`0x6AA69B`, 32 chars), widened to UTF-16.
- `0x645ED4`: `mov dword [esp+0xC], 200`, then `0x62C45C` (`0x645EE2`): its arg 4 goes to `0x621E44(text, string, limit, 1)`, which stores the limit at text `+0x1E0` and, measuring '.' (`0x413894`/`0x6245C4`) with the text object's font **at that moment** (the default 16xy, before `0x645EEC` sets the magc place copy), cuts the string with "..." when it is wider than the limit.
- The limit is a fixed 200 screen pixels at every resolution; at 4K with 16xy at 200% "07/16/26 21:25" is wider, so it is cut (the log's auto-fit lines show "07/16/26 21:2..." in 16xy before the font changes).
- Other callers of `0x62C45C`: `0x645755` (save name, computed limit), `0x665F90` (copyright, W·500/768), `0x668397`; 21 calls to `0x62C45C`/`0x621E44` in all.

**Fix:** the immediate at `0x645ED8` becomes 200 × (largest text size in use)/100 (`tx_date_limit`; 400 at 200%), checked (`C7 44 24 0C`) and restored on unload; with the text hook.

**Tests:** harness `M <W> <H> <w> <h> <maxPct>`; replay "menu places" (both sessions' numbers, 1024×768, clamping, right edge at 0.876 W). ALL PASSED.

## 2026-11-26 (night): conversation screen: portrait, dialogue box, flicker (build `3a1e36f0`)

**User (session pid 20048, 4 screenshots in the Madrigal Inn: Chant, the innkeeper):** the NPC portrait frame is still stretched, maybe the dialogue boxes too: check that boxes and text are not stretched. Also look at the earlier bug: the portrait flickers for a moment when the dialogue moves to its next set of text.

**What the game draws** (frame capture 12, 4K, a conversation frame; canvas 640×480 in brackets):
- Letterbox bands: `0x4B686A`, material `0x4D`, full width, y 0–270 and 1890–2160 [0–60, 420–480], in their own flush (RenderAllBuffers `0x5EF21A`/`0x5EF247`).
- Dialogue box: `0x456ABD` (the tooltip/box routine), material `0x4D`, 2118–2803 × 296–524 [353.0–467.2, 65.8–116.4]: sized around the text in screen pixels, right edge at canvas 467.2.
- Text: `0x42B0BC` glyph batches (magc), right-aligned inside the box, glyphs in screen pixels.
- Portrait with its gold frame: one sprite, `0x4A9195`, material per NPC (`0x6F` Chant, `0x6D` the innkeeper; `0x6E` in the 2026-11-21 notes), 2823–3687 × 86–734 [470.5–614.5, 19.1–163.1]: 144 × 144 canvas units, so square at 640×480 and **stretched to 864 × 648** (4:3 → 16:9) on a 16:9 screen; flushed at `0x5EFB36`.
- Choice list ("Local Talk", "Buy Food and Drink", "Leave"): boxes `0x456ABD` at x 1 (top left), text in screen pixels: not stretched.
- So only the portrait is stretched; box and text are placed by the canvas (their right edge follows the stretched width) but keep their pixel size.

**Fix (ui_layout.inc, with Layout on):** the conversation is recognised by the top letterbox band (this or the previous frame). The portrait (by its canvas rectangle, any material) is made square again (648 × 648) with its right edge where the HUD's right edge would be (`map_right`, 3725.25 at 4K; centred with HudLayout=center); the dialogue box (a fixed box with its right edge near canvas 467, in the top part) and its text move by the same amount (+259.25), keeping the 15 px gap.

**Flicker, found:** the 30 s window around the conversation (1850 frames) has 3627 commits from the sprite caller `0x4A9195` where portrait + cursor would give 3700: the game leaves the portrait out of ~73 frames, a few at each change of dialogue line (the hover log shows the text gone for 2 frames at the same moments, e.g. frames 7100–7102). The letterbox commits (`0x4B6B92`, 3700) are complete, so the bands stay. Not something the proxy caused (reported 2026-11-23, before any conversation work). **Mitigation built:** at EndFrame, a frame with the letterbox but no portrait gets the last portrait drawn again (`ui_draw_quad`: the saved 6 vertices, bucket, material and arg 4 through AddTri/CommitTri, as the aspect-cap bars are drawn), for up to 30 frames; forgotten when neither letterbox nor portrait is drawn. With `Log=1` the log names every gap ("conversation: the portrait was missing for N frame(s) ..."). To confirm in game whether this removes the flicker (if the game also skips the bands in those frames, it cannot trigger: the log will show).

**Tests:** harness `e` (EndFrame), `HQ` lines; replay "conversation" (positions, held portrait, no hold without letterbox, Layout=0 untouched). ALL PASSED.

### Portrait hold: provisional, and how to remove it (2026-11-26, later)

**User:** the flicker may be the original game's behaviour (to be checked against videos online); keep the fix easy to remove. Portraits are now presented properly (square, top right: confirmed in game).

**Session pid 1668 (log `conversation:` lines):** every change of dialogue line leaves the portrait out of exactly **1 frame** with the letterbox on screen (frames 2053, 3813, 4647, 4966, 7421), each filled by the held portrait. At the start of a conversation the letterbox is on for 240 / 417 frames before the first portrait (the camera moving in): nothing is held there (no portrait yet). (The first build's log line said "last portrait drawn instead" for those too: it counted holds over the whole session; now per gap.)

**Switch:** `[Debug] ConversationPortraitHold` (default 1; 0 = the game's own drawing, nothing else changes; in the shipped ini with a comment).

**How to remove it completely:** in `src/ui_layout.inc` delete the block starting "conversation portrait hold (PROVISIONAL" (globals `g_fPort`… `CONV_HOLD_FRAMES`), the `g_portV`/`g_portHave` saves inside the portrait branch of `ui_flush` (keep `g_fPort = 1` only if the log lines stay), and `ui_conv_end()`; in `src/soulbringer_modern.c` `g_cfgPortraitHold` and its ini read, `ui_draw_quad()` and its prototype, and the `if (idx == IDX_EndFrame) ui_conv_end();` call in `hook_pre`; in the harness the `ui_draw_quad` stub, `g_cfgPortraitHold` and the `e` command; in `replay.py` the held-portrait checks of `conversation_tests` (the `held` lists); the ini lines; CHANGELOG/ROADMAP mentions. The portrait's square layout does not depend on any of it.

## 2026-11-26 (late): name/message fonts mgrn/mred redrawn (`[UI] NameFont=Custom`)

**User:** next, the "unique green font" of named NPCs under the cursor (screenshots, session 1668: "Shopkeeper", "Chant"). It is **mgrn** (green), with **mred** (red) its twin: the same letters, another palette.

**The fonts (offline, `data\mgrn.fnt` / `mred.fnt` and the page dumps):** 192 records each, 191 drawn (mgrn: 46 on 0x4E, 79 on 0x4F, 66 on 0x50; mred: 23 / 78 / 90), space 4 texels, line 17, 'H' cap 9 texels at y offset 3 (baseline 12), x-height 6, descenders to 17. Strokes 1–2 texels, letters 4–6 wide (W 14). Shapes: A without a crossbar (Λ), G open with a spur dropping below the baseline and curling left, Q's tail below the line, M splayed, W as two Vs. No outline: each glyph mixes light and dark tones of its colour (mgrn (188,252,184) … (52,100,48) and greys (116,116,88), (60,60,60), (28,28,28); mred (252,40,0) … (32,0,0)). Where: names of characters under the cursor (mgrn, place 0x556C6D), pickup messages ("Found an item!", "Gained object: Knife", mgrn), "Locked." / "Empty." (mred, place 0x556BAA); objects 0x213ACF0 / 0x213ACF8 (+ loads at 0x674891/0x67F5DA mgrn, 0x674823/0x67F4D8 mred).

**Face:** the modern sans faces first proposed (Montserrat, Fira, Inter, Open Sans, DM Sans, Poppins) and a condensed list were rejected ("none come close"); the user asked to match the G and found **Procopius** (David F. Nalle, Scriptorium, 1993/1997): A without crossbar, descending G spur, the same flared calligraphic sans: very likely the original's face. Commercial (sold by Scriptorium/MyFonts), so **not bundled**: used privately through `[UI] NameFont=Custom` with `[Debug] NameFontFace=Procopius`, `NameFontFile=SoulbringerModern\fonts\Procopius_Regular.ttf` (both the shipped ini's Debug defaults; the shipped `NameFont=0`). Procopius has 169 glyphs: 114 of mgrn's 191 codes (no accented capitals etc.).

**DLL:** own metrics from 3 to 5 fonts (td_file 8–12: phil, hbut, magc, mgrn, mred); mgrn/mred share the name face and go together (like phil/hbut). New: a code the face does not have gets no new cell (`fr_met` checks `GetGlyphIndicesA` with GGI_MARK_NONEXISTING_GLYPHS, loaded optionally; before, GDI would have drawn its default character) and **keeps its original rectangle**, which stays occupied for the packing (before, a missing glyph's rectangle could have been packed over). Style as magc: the font's bright colour, darkest colour as a soft k/2 px edge. Cells array 768 → 1024 (fpg_pack's limit). The redraw waits for all four pages when any font beyond phil/hbut is converted.

**Build:** `72240fa6`.

**Offline (k = 4):** with Procopius, mgrn/mred em 49 px (baseline 12, line 17), 114 cells each, 77 glyphs keep their rectangles; all five fonts: 798 of 798 cells packed, LAYOUT CHECKS PASSED. Replay "menu font" now packs all five (MedievalSharp standing in for the name face: 950 of 950).

### Name font face: the user's `mgrn-regular.ttf` instead of Procopius (build `05dd9635`)

**User:** replace Procopius with an attached font, `mgrn-regular.ttf`. Its name table: family "mgrn", Regular, "Version 0.1", weight class 300, no copyright or licence entries, 217 glyphs / 195 characters (fontTools): the same Procopius-style letters (crossbar-less A, descending G spur), a little lighter and with more characters. Of the 191 drawn mgrn codes only 19 are not in its cmap (soft hyphen, ¯ ° ± ² ³ µ ¹ ¼ ½ ¾ Ð × Ý Þ ð ÷ ý þ), against 45 for Procopius. Offline (k = 4): em 48 px, baseline 12, line 17, 190 cells per font; all five own-cells fonts 950 of 950 packed. Origin and licence unknown, so treated as private like Procopius: not in the repo. Defaults (code and the shipped ini's `[Debug]`) now `NameFontFace=mgrn`, `NameFontFile=SoulbringerModern\fonts\mgrn-regular.ttf`, `NameFontWeight=300` (its own weight, so GDI does not embolden or substitute); the user's ini the same. `Procopius_Regular.ttf` is still in the game's fonts folder (the proxy cannot delete files there) and no longer used.

### `mgrn-regular.ttf` bundled as the default name font (OFL) (build `88039b15`)

**User:** use a general licence so anyone can use it; it is "essentially an upscaled version of the in-game font"; mgrn-regular is the default replacing the game's mgrn. **Done:** `dist/fonts/mgrn-regular.ttf` with name-table entries added (copyright "Copyright (c) 2026, the Soulbringer Modern contributors.", description, licence (ID 13) and licence URL (ID 14): SIL Open Font License 1.1; nothing else changed) and `dist/fonts/mgrn-OFL.txt`; `make_release.sh` ships both. `[UI] NameFont`: `Mgrn` (1, the bundled face, family "mgrn", weight 300; the shipped default), `Custom` (2, `[Debug] NameFontFace/File/Weight`, now empty in the shipped ini), `0` (the game's). Replay "menu font": the shipped ini has `NameFont=Mgrn`, the release list has the font and its licence, and the five-font layout uses mgrn-regular.ttf (950 of 950 packed).

**Note for the record (told to the user):** its outlines are a new drawing (other point structures than Procopius: 'G' 72 points against 50), but its proportions follow Procopius closely (advance widths 2–3 % wider, glyph for glyph), as they would for an upscale of the game's font, which was itself drawn in Procopius. The licence decision is the user's.

## 2026-11-26 (late): the tooltip font hbut from mgrn-regular too (`[UI] TooltipFont`, build `acdb8c9d`)

**Finding (offline, all fonts compared pixel mask by mask):** the 16 `.fnt` files are 7 designs. **hbut = mgrn = mred**: all 191 glyphs have identical boxes and identical pixel masks, only the palette indices differ (hbut cream with darker shades). The five 5Px* fonts are one design in five colours; numg = numr (13 glyphs: digits, + - / : =, codes 43–61); 16xY/16xB/16xX share sizes but differ in their rings; phil (journal) looks like the same Procopius family (crossbar-less A, descending G) but its masks differ from hbut's (another weight or rendering); magc and cred (credits: line 35, cap 19, a large white serif; not seen in a session yet) are their own. Sheet: `all_game_fonts.png` (session files).

**User:** use the mgrn upscale for hbut. **Change:** `[UI] TooltipFont` = `Mgrn` (1: hbut from the bundled mgrn-regular, face "mgrn", weight 300), `Cardo` (2: as before), `0`/`Original` (3: the game's letters); missing/empty = `[Debug] FontRedraw` decides (as before). 1/2 set FontRedraw's hbut bit, 3 clears it; FontPages forced on (k ≥ 4). With TextHook: own cells (hbut alone is now wanted with `TooltipFont`, and the "phil and hbut go together" rule only applies without it); without TextHook: hbut redrawn inside its original rectangles, and that path now opens each font's own face (it used Cardo for both). Style unchanged for hbut: bright fill, dark edge (k+1)/2 px at alpha 220. Offline: phil (Cardo), hbut + mgrn + mred (mgrn-regular), magc: 950 of 950 cells packed. Shipped ini `TooltipFont=Mgrn`; replay "menu font" checks it and packs hbut from mgrn-regular.

## 2026-11-26 (late): the journal font phil from mgrn-regular too (`[UI] JournalFont`, build `8654a101`)

**phil vs hbut (offline):** the same typeface (Procopius-style: crossbar-less A, G with a descending spur, same K and y) drawn one pixel larger: cap 10 vs 9 texels, x-height 7 vs 6, y offsets one more; only 16 of 191 boxes are equal, lines ~2 % wider (1195 vs 1168 texels for all glyphs); ink colours (120,112,84), (148,132,104), (160,160,124) … for the parchment. **User:** replace Cardo with the mgrn upscale (the size difference is handled by the text sizes).

**Change:** `[UI] JournalFont` with the same values as `TooltipFont` (Mgrn 1 / Cardo 2 / 0 = original 3 / missing = FontRedraw decides) for phil; 1/2 set FontRedraw's phil bit, 3 clears it; phil alone is wanted for own cells; the "phil and hbut together" rule now only applies when neither setting is used. Style unchanged (phil: darkest colour as fill, no edge). Offline: phil em 54 px from mgrn-regular (hbut 48 px: the pixel of difference), baseline 14, line 17, 190 cells; all five fonts 950 of 950 packed. Shipped ini `JournalFont=Mgrn`, `TooltipFont=Mgrn`, `NameFont=Mgrn`: Cardo is only used when chosen. Replay "menu font" packs phil from mgrn-regular and checks the ini.

### Renamed: Soulbringer Text (build `433f9403`)

**User:** rename the face; "the more simplified the better"; the ini will get a user-friendliness pass later, this is a first step. **Done:** family "Soulbringer Text" (name IDs 1/4/16, PostScript `SoulbringerText-Regular`, unique ID, description), file `dist/fonts/SoulbringerText-Regular.ttf` + `SoulbringerText-OFL.txt` (the `mgrn-regular.ttf` / `mgrn-OFL.txt` in `dist/fonts` replaced; glyphs untouched). The three settings now sit in one ini block with plain values: `NameFont` / `TooltipFont` / `JournalFont` = `Text` (Soulbringer Text) | `Original` | `Cardo` (tooltips, journal) | `Custom` (names); parsed by `ini_font_choice` (case and spaces ignored; `Mgrn`, `SoulbringerText`, `1`, `0`, `Off` still accepted). Log lines name "Soulbringer Text". Replay "menu font" checks the new file names and `=Text`. In the user's folders the old `mgrn-regular.ttf` / `mgrn-OFL.txt` stay (the proxy cannot delete files) and are no longer used.

## 2026-11-27: conversation text: letter spacing fixed, `ConversationTextScale`

**Session 13012 (user screenshots, 15 BMPs):** dialogue text magc `copy@6800EF` (text object: mode +0x58=1, +0x5C=200, right edge 2784, 3-5 lines 40 px apart at 4K, menu size 200%); choice list magc `copy@68017B` at x=20, y=10/51/92 (mode 2, width 3840), top left inside the letterbox, the pentagram cursor beside the chosen line. Both made in the loader 0x67F950.

**RE:** 0x6899F0 (called for both texts) is a plain setter: rect args -> +0x0C..+0x18 and +0x3C..+0x48, font -> +0x208, [0x35565E8] -> +0x200, [0x35565E4] -> +0x204, byte [0x6B813C] (8) -> +0x1CC, byte [0x3556330] -> +0x1D0, then 0x411634. +0x1CC/+0x1D0 are the object's defaults from the constructor 0x455A.. (+0x1C8=6, +0x1CC=8; logged as "draw mode" 1D0=1); not letter spacing. 0x4556A0(2) follows for the dialogue.

**Cause of the loose spacing:** not the game: `fpg_glyph` condensed every letter by 2 x the outline (k/2 = 2 px each side at k = 4) when ink + outline was wider than the cell. Uncial letters fill their advance, so each lost ~1 texel unit: thinner letters, visible gaps ("r e f r e s h m e n t"), unlike the face rendered normally. Reproduced offline (cells from `menu_font_preview.py`, condensing simulated). **Fix:** `FpgStyle.edgeClip` (font_pages.inc), set for magc: the letter keeps its width, the outline is cut at the cell's sides. Affects the main menu and save list too (same font): letters slightly fuller. Selftest case added.

**`[UI] ConversationTextScale`:** new group in `fp_group` with `0x6800EF` and `0x68017B`; shipped ini `ConversationTextScale=0` (follows `MenuTextScale`); user's ini set to 300 for the check. Replay place-group test sets it to 280.

**Next:** user checks size, spacing, wrapping and the dialogue box next to the portrait at 300%; then the choice list placement (it is at the original spot, the top-left corner).

**Confirmed (user, 2026-11-27, build `612c2792`):** conversation text "looks much better", `ConversationTextScale=300` kept in the user's ini (the shipped default stays 0 = `MenuTextScale`). The choice list stays where the game puts it (top left) for now.

### Quit dialog: two copies, parked

**From the logs:** "Quit the game?" / "Yes" / "No" are drawn by two sets of magc places: `66569A` / `665A36` / `665DBA` after the main menu's Quit (session 13012, end of the log) and `6549BF` / `654D5D` / `6550E3` after the Quit button of the in-game options menu (Load / Save / Quit row, session 12400). The "Q" is drawn separately from the rest of the line (as in the main menu's "Quit"). Neither set is in a group, so both follow `MenuTextScale`, not `MainMenuTextScale` (they look tied to the main menu only because the user's `MainMenuTextScale` is 0). **Suggestion given:** add both sets to `MainMenuTextScale` instead of a new setting. **User:** document it and leave it for now.

### Credits parked; combat numbers looked at (session 15904)

**User:** the credits screen can wait (parked in ROADMAP). Numbers next: combat damage indicators.

**Session 15904 (7 screenshots, a fight with brigands outside Madrigal):** damage numbers are numr (`numr.fnt`, own object loaded by `67FE78`, "-1" … "-19"), a miss/zero in numg (`67FEF5`, "0"). Both loaded at `NumberTextScale` 250% (line 13 -> 33); a "-16" measures about 37 x 20 px at 4K. The look: chunky pixel digits, yellow fill with a red one-texel outline, each texel 2.5 screen pixels (blocky; screenshot 5). Also in this session: mred "You died!" / "Please click the left mouse button when ready." (`674823`), mgrn "Experience gained: x 3", "Gold gained: x 4", "Gained object: Knife" (`674891`), cred "GAMEO" / "VER" (`67FFEF`, game over).

## 2026-11-27: combat numbers redrawn (`[UI] NumberFont`, build `bae57729`)

**User:** compare original, upscaled and a heavy plain font (sheet `numbers_comparison.png`, character sheet `numbers_character_sheet.png` in the game folder: numr/numg have the same 13 shapes, `+ - 0-9 =`, 7x8 texels incl. a one-texel outline, '1' 6 wide, '4' 8 wide, '-' 7x3, '=' 7x5; numr yellow ECB018 / red C40000, numg green 80CC54 / dark green 0C3818). Free 8-bit-style options were listed (Pixelify Sans, Press Start 2P, Silkscreen, Bungee); **user chose Montserrat ExtraBold**, "no need to complicate things".

**Change:** `[UI] NumberFont` = `Montserrat` (1; also `Bold`, `1`) / `Original` (0). Drawn like the interface font into the original rectangles (layout and spacing unchanged), but as fill + outline (`fpg_glyph`, edge k px = one texel, alpha 255, condensed to leave room for it): font_pages.inc `fpg_num_em` ('8' = (h - 2) x k px tall, h = 8), `fpg_num_base` (baseline = the '8''s last fill row, line row 12; cell base = 12 - y offset), `fpg_num_colours` (the two most frequent colours of the digits, brighter = fill; `fpg_colours` splits at the median and gave red/red), `fpg_num_code` ('-' drawn as the en dash 0x96: Montserrat's hyphen is 13 px of the 20 available and left a gap before the digits). font_hd.inc `fpg_redraw_num` (face/file = the interface font's, `[Debug] InterfaceFontFace/File`), called per page from `fpg_redraw_page`; the page wait and the 4x pages include `g_cfgNumFont`. Shipped ini and the user's: `NumberFont=Montserrat`.

**Offline (k = 4):** em 33, baseline row 12, 26 glyphs drawn, 0 kept, 0 refused, nothing outside numr/numg changed (`tools/number_font_preview.py` -> `test_font_pages numbers`: NUMBERS PASSED); replay ALL PASSED (new "number font" test). Note: the game's '1' is narrower (6 texels) and the face's '1' is centred in it, so "16" keeps a small gap as in the original.

**Next:** user checks a fight (damage, a miss), with `NumberTextScale` as wanted.

## 2026-11-27: street-talk portrait squared (build `d3f7cab0`)

**User:** the upscaled text looks good (combat numbers included). Guards' portraits are still stretched.

**Session 28448 capture 11 (4K, a guard outside, "You'd best get indoors, stranger..."):** no letterbox (the HUD stays), so the conversation layout (`g_fConv`, letterbox bands) never applied. The portrait is the same 144 x 144 canvas sprite (0x4A9195, b=2, mat 6D) at canvas **363.8-507.8 x 32-176** (screen 2183-3047 x 144-792: 864 x 648, stretched, layout `-S`), drawn *after* the dialogue box (0x456ABD, canvas 200-360.5 x 74.2-133.8, right edge 3.3 units left of the portrait, text right-aligned in it). Same spot in session 15904. Survey of all logs for 144 x 144 sprites: letterbox portrait 470.5/19.1, street portrait 363.8/32.0, and a menu one at 79.2/79.6 (already pillarboxed, `MS`).

**Change (ui_layout.inc):** `ui_conv_portrait` returns 1 (letterbox) or 2 (street). `ui_flush` looks for a street portrait in the frame's batches first (it comes after the box) and remembers its geometry (`g_strA/B/H`, `g_strFrame`); street mode = one seen within `CONV_HOLD_FRAMES` (30, the game also skips it for a frame on a new line). The portrait is squared around its centre's 4:3 position (`map_center`): 4K 2117.25-2765.25; the box (right edge at canvas 355-366, above y 200, not at the cursor) and its text get the same shift as the portrait's left edge (-65.75), keeping the 20 px gap. Layout=0: untouched. Not held/redrawn (the 1-frame skip of the street portrait is left as the game does it).

**Tests:** replay "street talk" (numbers from the capture: portrait, box, text, a frame without the portrait, Layout=0); ALL PASSED.

**Confirmed (user):** street-talk fix "is working".

## 2026-11-27: quest message re-centred (build `b75ad38d`)

**User:** the green "A new quest has been added to your journal." does not look centred at the bottom (session 2184, screenshot 1).

**Measured (capture 12, 4K):** the game draws its box (0x456ABD) at 1249-1981 x 1755-1837 (layout `-T`, untouched by us), centre 1615, **305 px left of the screen centre**, with the text in it. Yet the text object (mgrn, created by the message code around `0x674891`, the same object as "Empty." / "Gained object") was set up centred: rect +0x0C..+0x18 = 0, 270, 3840, 1890 (between the letterbox bands), +0x60 = 1920, x 1573 for width 694. Every other message box in the logs is centred on 1920 (e.g. 1722-2119, 1682-2159). So the game moves this one afterwards; its centre (canvas ~269) is the middle of the view left of the HUD column (0-536.7 -> 268.3), i.e. the game centres the journal message on the play area, not the screen (also in 4:3 it would sit 51 canvas units left of the bar's centre). Where exactly it is moved was not traced.

**Change (ui_layout.inc, tooltip rules):** a one-line box, not at the cursor, bottom at canvas 400-414 (just above the HUD), centre at canvas 255-285 (`offMsg`) is treated as a message (grows from its bottom centre with TextScale when the proxy scales) and, with Layout on, moved so its centre is the screen's (`tp[4] = W/2 - centre`), its text with it. Layout=0: the game's place.

**Tests:** replay "quest message" (the capture's box/text: centred 1554-2286 with Layout, unchanged with Layout=0, an already centred message unchanged); the capture checker now knows the case (`offmsg`: bottom centre to W/2 with Layout) - session 2026-11-21-1450 capture 28 has this box (119 x 46 at 1440p, text 250%) and failed until then. ALL PASSED.

**Confirmed (user):** the quest message fix "seemed to fix it".

## 2026-11-27: credits font from the user's upscale (`[UI] CreditsFont`, build `d10d8288`)

**User:** replace cred with an attached upscale, `cred-upscale-Regular.ttf` (family "cred-upscale", 1000 UPM, 184 glyphs / 183 characters, weight 400, no licence entries). As with the text font, bundled for everyone: renamed **Soulbringer Credits** (`dist/fonts/SoulbringerCredits-Regular.ttf`, name IDs 0-6/10/13/14 like Soulbringer Text: copyright the Soulbringer Modern contributors, SIL OFL 1.1; outlines untouched, checked point for point) + `SoulbringerCredits-OFL.txt`; `make_release.sh` ships both.

**cred (offline):** 192 records, 191 drawn (187 on 4E, 4 on 4F), line 35, 'H' 21 x 19 texels at y offset 10, white FCFCFC with grey anti-aliasing (6C6C6C ... ECECEC), no outline. Used by the credits and "GAME OVER" (load `67FFEF`, session 15904). The upscale follows the game's letters closely (preview: same widths and shapes), so it is drawn **into cred's original rectangles** (layout, kerning and spacing stay the game's), not as own cells.

**Change:** `[UI] CreditsFont` = `Credits` (1; also `Soulbringer`, `Upscaled`, `1`) / `Original` (0); forces FontPages. font_hd.inc `fpg_redraw_cred`: reads `data\cred.fnt`, colour = the brighter of the two commonest colours of the glyphs on the 'H' page (`fpg_num_colours`: FCFCFC), em = `fpg_ui_em` (inset 0: 'H' 19 texels x k = 76 px at k = 4 -> em 104), codes not in the face (`GetGlyphIndicesA`) keep the game's letter (35: @ [ \\ ] { | } and most Latin-1 symbols, Ð Ý Þ ð ý þ × ÷), Œ/œ (codes 338/339) drawn from 0x8C/0x9C. `FPG_MAXW` 128 -> 192 (the widest letters, Œ ~124 px). Called per page from `fpg_redraw_page`; page wait and 4x pages include it. Shipped ini and the user's: `CreditsFont=Credits`; font + licence copied to the game's `SoulbringerModern\fonts`.

**Offline (k = 4):** `tools/credits_font_preview.py` -> `test_font_pages credits`: em 104, 156 glyphs drawn, '~' kept (the face's tilde sits high, outside cred's rectangle), 35 left to the game, nothing outside cred's rectangles changed: CREDITS PASSED; replay ALL PASSED (new "credits font" test).

**Next:** user checks GAME OVER (and the credits screen when convenient).

**Confirmed (user):** the credits font "seems to be working" (GAME OVER screenshot).

## 2026-11-27: title logo checked, left as is

**User:** the "Soulbringer" title image before the main menu looks stretched. **Found:** `data\logo.bmp` (640 x 366) is drawn by the game's start-up code with GDI (`LoadImageA` + `StretchBlt` at `0x58A2C9`, window from GetSystemMetrics) before D3d7Lib.dll is loaded (`0x6794C0` at `0x58A36F`, `0x597110` at `0x58A39A`), so the proxy cannot reach it and F11 cannot capture it. Measured on the user's screenshot (3840x2160): logo 3540 x 1351 px vs 599 x 228 in the bitmap, ratio 2.620 vs 2.627: already its true shape; the original 640x480 game showed it 31 % taller (`title_compare.png`). **User:** leave it.

## 2026-11-27: docs pass; frame rate question

**Docs:** ROADMAP tidied (HD font pages entry now lists every redrawn font; credits screen entry is the layout only; combat numbers confirmed), HD_FONTS_PLAN status updated.

**User:** is frame interpolation, or finding why the game speeds up above 60 FPS, out of scope? Would a decompilation help? **Notes:** the exe imports both `timeGetTime` (57 call sites) and `GetTickCount` (82), so much of the timing is clock-based; the speed-up above 60 FPS points at some systems stepped once per frame (GOG's `patch.dll` already has a `FrameLength` setting, see 2026-11-20). Finding and scaling those steps is possible from the proxy (it sees every frame and could also cap the rate itself); interpolation is not: the renderer gets pre-transformed screen vertices, so there is nothing to interpolate without the game's own simulation/render split (a decompilation or engine patches). Recorded in ROADMAP 0.3.

**User:** keep it as a written note; no plans for higher refresh rates without a proper decompilation, unless it can be done without one. ROADMAP 0.3 marks it "note only, not planned".

## 2026-11-27: docs check

Every repo file on the PC compared with the working copy (sizes; all equal). Leftover "to check in game" notes for fonts the user has since confirmed updated (ROADMAP: menu, name, tooltip, journal fonts); README feature row now lists the redrawn fonts instead of "Cardo Bold". Still on the PC from earlier and unused: `dist/fonts/mgrn-regular.ttf` + `mgrn-OFL.txt` (replaced by Soulbringer Text), `Cardo-Bold.ttf` stays (option); game folder `SoulbringerModern\fonts\mgrn-*`, `Procopius_Regular.ttf` (user to delete).

## 2026-11-28: settings file simplified (new format, text hook always on)

**User:** too many font and text settings; organise them cleanly by category (no need to limit what can be set), make the text hook permanent (a debug switch is fine), use the new format from now on (no reading of the old keys). Later: an in-game settings menu on a function key (kept in mind: the text settings are one table, `ts_key`, that such a menu can list).

**New layout:** `[Display]` Widescreen, MaxAspect; `[Interface]` Layout, Hud, CursorSize; `[Text]` Size, Tooltips; `[Text.Menus]` All, MainMenu, SaveList, Options; `[Text.Screens]` All, Character, Labels, Journal, Map, Credits; `[Text.Game]` All, Conversations, HudLabels, Names, Messages, PlaceNames, CombatNumbers; `[Fonts]` All (Sharp | Original), Names, Tooltips, Journal, Menu, Interface, Numbers, Credits (empty = follow All); `[Debug]` Log, AutoCapture, ScreenshotKey, Stock, TextHook (default 1), HudMouse, ConversationPortraitHold, FontPlaces (developers); hidden: FontPageScale, `[PlaceTextScale]`.

**Text sizes (font_places.inc `ts_key`, replaces `fp_group`):** every setting has a parent (item -> its category's All -> `[Text] Size` -> 200). The seven font kinds still exist inside: each takes the size of one setting (journal `Text.Screens/Journal`, tooltip `Text/Tooltips`, menu `Text.Menus/All`, interface `Text.Screens/All`, name `Text.Game/Messages`, number `Text.Game/CombatNumbers`, credits `Text.Screens/Credits`); a font place in no item, and every font object that is no place, is drawn at its kind's size. The name kind now follows **Messages**, not the NPC names: the names under the cursor are their own place (`556C6D`, item Names), and the messages players see are drawn from the second mgrn/mred objects (`0x674891` "Gained object", "Experience gained", the quest message; `0x674823` "You died!"), which are no places, so they follow the kind. (Before, `MessageTextScale` held only `556BAA`, never seen drawing, and probably changed nothing.) An item gets its own font copies only when its size differs from its places' kind (`ts_groups`), so with nothing set no code is redirected, as before.

**Fonts:** `[Fonts] All=Sharp` = every redraw on (the shipped defaults before, plus `InterfaceFont`, which was 0 in the shipped ini but on in the user's). Values: Names `SoulbringerText` or a file; Tooltips / Journal `SoulbringerText` | `Cardo`; Menu `SoulbringerUncial` (also `EagleLake`, `Uncial`) | `MedievalSharp` or a file; Interface / Numbers `Montserrat`; Credits `SoulbringerCredits`; any of them `Original`. A file (`.ttf` / `.otf`, relative to the game folder or absolute) replaces the old `Custom` + `[Debug] MenuFontFace/File/Weight` / `NameFontFace/File/Weight`: `ttf_name.inc` reads the family (name ID 1, Windows platform, US English first) and `usWeightClass` (OS/2), what GDI registers the file under. Checked on the bundled fonts: Soulbringer Text 300, Soulbringer Uncial 400, MedievalSharp 500, Montserrat ExtraBold 800, Cardo 700, Soulbringer Credits 400 = the values hardcoded before. New import: `SetFilePointer`. `FontPages` is on whenever a font is redrawn, `FontRedraw` comes from Tooltips/Journal, `FontOwnMetrics` is always 1, the Cardo / Montserrat faces are fixed. Values that cannot be used are logged once (`settings: not understood, the default is used instead: ...`).

**Old key -> new place** (for converting a development ini; the old keys are not read):

| Old | New |
|---|---|
| `[UI] Layout`, `HudLayout`, `CursorScale` | `[Interface] Layout`, `Hud`, `CursorSize` |
| `[UI] HudMouse` | `[Debug] HudMouse` |
| `[UI] TextScale` | `[Text] Size` |
| `JournalTextScale` | `[Text.Screens] Journal` |
| `TooltipTextScale` | `[Text] Tooltips`; it also sized equipment labels and place names: set `[Text.Screens] Labels` and `[Text.Game] PlaceNames` to the same value to keep them |
| `MenuTextScale` | `[Text.Menus] All` (conversations are now under `[Text.Game]`) |
| `InterfaceTextScale` | `[Text.Screens] All`; options and HUD labels now follow their own categories: `[Text.Menus] Options`, `[Text.Game] HudLabels` |
| `NameTextScale` | `[Text.Game] Names` and `Messages` |
| `NumberTextScale` | `[Text.Game] CombatNumbers` |
| `CreditsTextScale` | `[Text.Screens] Credits` |
| `MainMenuTextScale`, `SaveListTextScale`, `OptionsTextScale` | `[Text.Menus] MainMenu`, `SaveList`, `Options` |
| `CharacterTextScale`, `LabelTextScale`, `MapTextScale` | `[Text.Screens] Character`, `Labels`, `Map` |
| `ConversationTextScale`, `HudLabelTextScale`, `NpcNameTextScale`, `MessageTextScale`, `PlaceNameTextScale` | `[Text.Game] Conversations`, `HudLabels`, `Names`, `Messages`, `PlaceNames` |
| `InterfaceFont`, `NumberFont`, `CreditsFont`, `MenuFont`, `NameFont`, `TooltipFont`, `JournalFont` | `[Fonts] Interface`, `Numbers`, `Credits`, `Menu`, `Names`, `Tooltips`, `Journal` (empty = `All`) |
| `MenuFont=Custom` + `[Debug] MenuFontFace/File/Weight`; `NameFont=Custom` + `NameFontFace/File/Weight` | `[Fonts] Menu=<file>`, `Names=<file>` |
| `[Debug] TextHook` (default 0) | `[Debug] TextHook` (default 1) |
| `[Debug] FontPages`, `FontRedraw`, `FontFace`, `FontWeight`, `FontFile`, `FontOwnMetrics`, `InterfaceFontFace/File/Weight` | gone (automatic / fixed) |

**User's ini converted** (game folder): HUD right, Tooltips 300, Labels 300, Journal 250, Credits 280, Conversations 300, Names 300, Messages 300, PlaceNames 300, CombatNumbers 250, everything else at Size 200; Log=1, FontPlaces=1. Checked with the harness: font kinds 250/300/200/200/300/250/280 and only the conversation places (`6800EF`, `68017B`) with their own size (300), exactly as with the old file.

**Tests:** replay "text settings" (replaces "place groups"): the table (parents first, each font kind once, every place known and of its item's kind, none in two items), the shipped ini (every setting, Size=200, no 0.2-dev key, troubleshooting switches only in `[Debug]`, TextHook=1), the harness (`I` with `Section/Key` lines) against a separate Python implementation of the lookup on an empty ini, a worked case checked by hand, a `[PlaceTextScale]` override, the tooltip rules following Names, and 40 random settings; font file names (`f` command) for six bundled fonts and a text file refused. Two mutations caught (copies always made; values not clamped). The menu, number and credits font tests check `[Fonts] All=Sharp` with their font empty. ALL PASSED (with the game's fonts, executables and page dumps).

## 2026-11-28: 0.2 items on hold; clean upload folder

**User:** the remaining 0.2 items wait until after a playthrough (the user decides then which are still needed); document them. ROADMAP 0.2 now opens with an "On hold" list of every open item; their notes stay where they were.

**Upload folder:** `soulbringer-modern-upload` (next to `soulbringer-modern` in the test folder) holds exactly the files the repository should have, for uploading through the GitHub website: every file `.gitignore` keeps (sources, docs, tools, tests with the recorded sessions in `tests/data`, `dist` with the bundled fonts), none it excludes (build output, `release/` and the root zips: attach those to GitHub Releases; logs, executables, DLLs). Left out as well: `dist/fonts/mgrn-regular.ttf` and `mgrn-OFL.txt` (old font, replaced by Soulbringer Text; delete from the local repo). Line endings as `.gitattributes` asks (the website upload stores files as given): `.bat`, `.cmd`, `.ini` CRLF; `.sh`, `.py` LF. `.gitattributes` also marks `*.ttf` binary. On GitHub, `release/` still holds three zips from earlier uploads (0.1.0, 0.1.1, 0.2.0-dev): `.gitignore` excludes the folder, so they can be deleted there and attached to Releases instead.
