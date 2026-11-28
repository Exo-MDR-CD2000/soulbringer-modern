# Tests and tools

## Regression test: replay recorded frames

```sh
python3 tests/replay/replay.py                 # all logs in tests/data/
python3 tests/replay/replay.py my-session.log  # or any debug log with frame captures
```

It builds `tests/replay/harness.c` with your C compiler. The harness includes `src/ui_layout.inc`, the exact layout code the DLL ships. Every recorded frame is fed through it, and the results are checked: menus inside the centered 4:3 area, HUD column at the right edge, bottom bar centered, world geometry untouched. Exit code 0 means everything passed. With Pillow installed, it also writes before/after pictures next to the log.

`tests/data/` holds frame captures from real sessions (`session-<date>-<time>.log`, first line describes it; hover and text lines are stripped to keep them small). They contain only coordinates, texture IDs and call addresses, no game assets. Add new captures there when a new screen is found, so every later change is checked against it.

What is checked, for every recorded session, replayed in order (each screen three times, checking the steady frame), under 8 configurations, plus 4 with `TextScale` 250/300 (`stretched`, `split`, `right`, `center`; `split`/`right` capped at 16:10, `split` capped at 4:3; `stretched` capped at 16:10 with `Layout=0`):
- menus inside the centered 4:3 area (or the capped area with `Layout=0`)
- HUD column and bar tiles exactly where the layout puts them; black bars exactly outside the cap
- the gameplay cursor and its tooltips not moved; world geometry not moved
- tooltip boxes and their text: one-line tooltips at the cursor are scaled by `TextScale` around the cursor tip (box size, glyph size, place in the box, gap to the cursor) and kept on screen; other boxes keep their shape; text stays in place relative to its box

HUD click mapping (`ui_hud_aim()` in `ui_layout.inc`: which HUD part is drawn at a screen point, and where the game has that point):
- **round trip:** HUD points on the bar and column are mapped to where they are drawn and back, for all 8 configurations at 3840×2160, 2560×1080 and 1920×1200; the result must be the same point and part. World points, points beside the moved HUD, and HUD parts not on screen must map to "world".
- **measured offsets:** at 3840×2160 `split`, the shift must equal what was measured in game (bar `480 − 0.25·x`, column `0.25·(3840 − x)`).
- **recorded clicks:** every gameplay click in a session log is replayed and printed (drawn part, the game's point, the game's hit-test, the miss in px). Logs with `aim:` lines (from the DLL) must agree with the harness to 0.5 px.
- **every named button** (from `tests/data/hud_buttons.txt`): clicked at its drawn centre, the mapping must give back the same button, in every configuration. At 3840×2160 the tests also print which buttons the game hits *without* the fix (split 27/34, right 25/34, center 5/34 today).
- **buttons:** with `tests/data/hud_buttons.txt` (`<name> x0 y0 x1 y1` per line, canvas units 640×480), each click also shows the button aimed at and the button the game hit. Until the fix is in (`STEP2_DONE = False` in `replay.py`) a mismatch is reported, not failed.

Harness commands for this: `P <x> <y>` (forward) and `U <x> <y> <col> <bar>` (inverse), printed immediately.

Text decoding (`text_decode.inc`), when the game's fonts are available (`SBM_GAME_DIR=<game folder>` or `SBM_FONT_DIR=<folder with the .fnt files>`; the fonts are game data and are not in the repo):
- every glyph of every font, stored the way the game's draw code stores it, decodes back to its character;
- a line drawn as two batches (capitals and small letters in different font textures) is joined into one catalogue line with its spaces.
Tooltip anchor: the cursor jumps 500 px between frames and the tooltip is drawn before its cursor sprite; with the live cursor (`V 1`, as in the DLL) it must still be scaled.

Harness commands: `K <dir>` (load fonts), `X <n> <material>` + n lines `u0 v0 u1 v1 u2 v2` (decode a batch; the next `B` draw gets its characters).

Game-side text scaling (`text_scale.inc`, `[Debug] TextHook=1`), with the fonts available: every font is built in memory the way the game's loader builds it (sorted records, direct table, line height, kerning), scaled at 150, 200 and 300%, and every glyph drawn the way the draw-glyph function `0x433714` draws it (corners from the pen and the record, clipping, texels one per screen pixel), then the texel rewrite runs:
- the quad is the scaled size at the scaled vertical offset, the texture rectangle is the original one; glyphs clipped at the right and top keep their original texture corner and the proportional part of the rectangle;
- advances, line height, line gap and letter spacing (the setter's −1) are scaled; scaling the same font again does nothing; a reload of the font into the same object (as the game does per screen) is scaled once, not on top of the old values;
- at 100% nothing changes; mutation check: without the texel rewrite the texture rectangles come out wrong.
Tooltip rules with game-drawn text (`Y 200`): a one-line tooltip 64 px tall above the cursor is still recognised as the cursor's tooltip (with the old fixed 40–52 px rule it is not).
Tooltips with game-drawn text at 300% and a 200% cursor (`Y 300`):
- the box never covers the cursor: a corner tooltip goes above the cursor (below it at the top of the screen), an item tooltip already clear of the cursor is unchanged;
- text inside a cursor tooltip box moves with the box only if it is the tooltip's own font: hbut text moves, a 5px slot label under the box (the COMBO case) stays;
- the name of the character under the cursor (mgrn, the recorded "Shopkeeper" frame next to the HUD column) moves with its box, every glyph by the same offset.
Harness command `b <caller> <material> <n>` + n lines `x y z rhw tu tv`: a draw with texture coordinates (for tests that need the glyph's font).

Harness commands: `Y <pct>` (game-side text scale for the tooltip rules), `Z <dir> <font> <pct> <n>` + n lines `code x y clipL clipT clipR clipB` (load, scale and draw; prints `ZF` and `ZG` lines), `N 1` (skip the texel rewrite).

Per-place fonts (`font_places.inc`, `[Debug] FontPlaces=1`; TEXT_SCALING_PLAN phase 5b), with the fonts available: 16xY is loaded the way the game loads it into its object, then every one of the 38 interface-font places gets a copy (`fp_refresh`, as the load hook does before scaling):
- each copy equals the original (records, direct table, kerning entries and pairs) and shares no memory with it;
- the original at 200%, copy 0 at 150% and copy 1 at 300% scale independently: record widths, line heights, kerning adjusts and the letter-spacing setter's −1 (run on the original, followed by the copies);
- the game reloads the font into the same object: every copy is refreshed in place (same address), scaled once (not on top of the old values) and keeps its spacing; each copy maps back to its place, the original to none;
- mutation check: a copy that shares the original's kerning (a shallow copy) must fail;
- with `SoulbringeVCnoeax.exe` / `SoulbringeVC.exe` in `SBM_GAME_DIR`: at every site the bytes are the read of the font global the DLL redirects (`8B 15 imm32` or `A1 imm32`, the global's address), followed, by kind, by a store to `+0x208`, an argument push (`mov [esp(+d8)],r32`) or a measuring use; this covers all 98 sites (16xy 38, magc 32, magc2 2, hbut 15, hbut2 3, mgrn 1, mred 1, 16xb 4, 16xx 2), and a site of an unknown global fails.
Harness command `O <dir> <font> <pctOrig> <pctA> <pctB> <mutation>` (prints `OF`, `OR`, `OP` lines).

Per-place sizes (`font_places.inc` `fp_pct`/`fp_set_pct`, `[PlaceTextScale]`; phase 5b step 2), with the fonts available: 16xY at an Interface size of 200%, places set by site address the way the ini sets them: options menu `4471BB=150`, inventory/character values `66B65E=300`, a HUD label `41E939=200`, `68A418=20` (clamped to 50), `443D38=900` (clamped to 600), `64EF68=0` (the kind's size), the cursor-tooltip place `5567BE=350`, and an address that is not a place (refused). Then the load hook as the DLL runs it (copies first, the original at the kind's size, every copy at its place's size), the spacing setter, and a reload into the same object:
- each 16xy place's copy is at its own size, every unset place at 200%; widths, line heights and the spacing setter's −1 follow that size (raw width 13 → 20 / 26 / 39 px at 150 / 200 / 300%);
- the original stays at the kind's size; a place at the kind's size equals it;
- after the reload every place has the same metrics again (scaled once);
- the largest place size (600, for the font page scale) and the tooltip-rules size (350, the largest of TooltipTextScale and the cursor-tooltip places) are reported;
- mutation check: copies scaled at the kind's size (place sizes ignored) must fail.
Harness command `J <dir> <font> <kindPct> <mutation> <n>` + n lines `site pct` (prints `JS`, `JM`, `JL`, `JP` lines).

Auto-fit fix (`tx_autofit_choice`, text_scale.inc): with a greedy word-wrap stand-in for the game's layout `0x413A64`, an enlarged mode-2 label the game broke onto two lines gets the one-line box ("Tool Tips" 75×64 → 84×32, "Sound FX"); unchanged at 100%, in mode 1, with a narrow target, for text wider than its clip, for a box already one line; mutation (one-line guard off) caught. Harness command `E <mode> <target> <clipW> <boxW> <boxH> <pct> <step> <guard> <space> <n> <w1..wn>` (prints `EF`).

Conversation layout: "conversation" (letterbox portrait squared at the top right, box follows, a skipped portrait frame held) and "street talk" (guards, no letterbox, session 28448: portrait drawn after its box, squared around its 4:3 centre 2117.25-2765.25 at 4K, box and text moved by the left edge's shift -65.75, also in a frame without the portrait; Layout=0 untouched).

Text settings (`ts_key` in font_places.inc: `[Text]`, `[Text.Menus]`, `[Text.Screens]`, `[Text.Game]`; 2026-11-28): the table (parents before their items, each of the 7 font kinds sized by exactly one setting, every place a known site or load site of its item's font kind, none in two items, keys unique); the shipped ini (every setting present, `Size=200`, the rest 0, no key of the earlier development builds, `TextHook`/`HudMouse`/`ConversationPortraitHold`/`FontPlaces` only in `[Debug]`, `TextHook=1`); and the sizes: the harness against a separate Python implementation of the lookup (item → category `All` → `Size` → 200, clamped 50–600; an item's places get their own size only when it differs from their font kind's) on an empty ini, a worked case also checked by hand (e.g. Conversations follow `[Text.Game]`, not `[Text.Menus]`), 40 random settings, a `[PlaceTextScale]` override after the items, place names by load site, the tooltip rules following `Names`. Font files (`[Fonts] Names`/`Menu` = `<file>`): face name and weight read from six bundled fonts (Soulbringer Text 300, Soulbringer Uncial 400, MedievalSharp 500, Montserrat ExtraBold 800, Cardo 700, Soulbringer Credits 400), a text file refused. Harness commands `I <n>` + n lines `<Section>/<Key> <pct>`, then `IP <site> <pct>` … `IP 0 0` (prints `IG`, `IK`, `IT`, `IS`, `IL`, `IM`), and `f <path>` (prints `FN <weight> <face>`).

UI 3D models (inventory items, the HUD weapon icon): in a 16:9 menu every model triangle must be scaled by 4H/3W, the game's width-based size brought back to the 4:3 area (as at 1024×768).

Tooltip checks in the session replays follow the 2026-11-23 rule that keeps a box off the enlarged cursor (corner tooltips end 6 px above it, below if there is no room; item tooltips start 6 px below it): the expected position includes that move.

HUD mouse fix (`ui_hud_mouse()`, `[Debug] HudMouse=1`, the default), with the harness playing the game's mouse update (`Q <mode> <col> <bar>`, `C <x> <y>`, `D <dx> <dy> <btn> <drag>`), for every configuration at 16:9, 21:9 and 16:10:
- each of the 34 buttons, reached by moving the real mouse onto its drawn centre, puts the game's cursor on that button;
- sweeps across the bar and the column, out and back, end exactly where expected (no drift, no sticking at the ends);
- menus and right-button camera drag pass the movement through;
- the gameplay cursor sprite is drawn at the real mouse.
Clicks recorded with the fix (`hud mouse: real (x,y), game cursor (x,y)` after a click) must hit the button drawn under the real mouse.

The harness can also run the debug-log code (`G 1` in its input), which prints hover events for recorded frames on stderr.

## Reading a debug log

```sh
python3 tools/sbm_log.py SoulbringerModern_SoulbringeVCnoeax_1234.log
```

Lists the session events (new screens, menu/game switches, mouse clicks), a **hover timeline** (every tooltip or object label: cursor, box size and offset from the cursor, text size, glyph height, margins, the words, the world draw calls under the cursor), a summary of the sizes seen, and the **text catalogue** (each distinct string, its font, and the code that drew it). Draws every frame capture as it was drawn by the game and as placed by the layout, using the layout decisions the DLL recorded in game.

Not covered offline: DirectInput (the click hook) and the in-memory widescreen patch. Those need a run in the game.

## Recording a session

Set `Log=1` in `SoulbringerModern.ini` (in the game folder). With `AutoCapture=1` (the default), every new screen is captured once, automatically, when it has been steady for 8 frames. F9 forces a capture of the current screen. **F11** (`ScreenshotKey`) saves a screenshot to `Screenshots\SBM_<pid>_<n>.bmp` in the game folder and a frame capture of the same frame; no need to press F9 as well.

Useful lines in a log:

| Line | Meaning |
|---|---|
| `event: screen XXXXXXXX stable (...)` | a new screen; followed by an auto capture |
| `event: mode menu -> game` | switch between full-screen menus and gameplay |
| `event: hover "..."` + `box`/`text`/`under cursor` | a tooltip or object label appeared or changed |
| `event: click L (di)` | mouse press; `os` = Windows (menus), `di` = DirectInput (gameplay) |
| `text: "Combo screen" font 16xB at (x,y) size w x h` | a line of text seen for the first time, decoded from the glyphs |
| `chars "Combo scr" font 16xB` (in captures) | the characters of one glyph batch, in draw order |
| `aim: drawn hud-bar -> game point ... miss N px` | gameplay click: the HUD part drawn under the cursor, where the game has it, and how far the game's hit-test is off |
| `event: screenshot N (capture M) saved ...` | an F11 screenshot and the frame capture (`FRAME CAPTURE M`) of the same frame; `WARNING: image is black` if the fullscreen mode hid it from GDI |
| `event: click result ...` | what a click did 20 frames later: `opened a menu`, `back to game`, `screen changed` (may be just a tooltip), `no change` |
| `event: DirectInput device ...` / `mouse buttons hooked` | the click hook found the game's mouse |
| `text: "..."` | a distinct string and the code that drew it; `~` = UTF-16 game text |

To add a session to the tests: copy the log to `tests/data/session-<date>-<time>.log`, put a one-line `# description` at the top, drop `text:` and `event: hover` lines, and run the replay.


## Session checklist: text (step 3)

With `Log=1`, visit each of these and rest the cursor a moment; press F11 on each so the screenshot can be compared with the decoded text:
1. Gameplay: hover a few HUD buttons (bar and column), a world object with a label, an NPC.
2. The column's move slots (CUT, CUT LOW, COMBO) and any HUD numbers.
3. A conversation (wrapped text beside the portrait).
4. Inventory, character, options and journal screens (hover items and buttons).
5. Save/load screen, loading screen with the level name.
6. Anything with a multi-line panel (item descriptions, spell info).

## Session checklist: game-drawn text (step 3 phase 3; the text hook is always on since 2026-11-28)

`[Debug] Log=1` in `SoulbringerModern.ini`, `[Text] Size=200`. Start the game and check, with F11 on each:
1. The log has `text hook: installed` and a `text hook: ... font ... scaled` line per font (note any `already loaded` or `not scaled` lines).
2. Main menu, options: text is larger and the letters are right (no garbled or cut glyphs).
3. Gameplay: hover HUD buttons (tooltip box grows with its text, sits next to the cursor), a world label, an NPC; the HUD slot labels (CUT, COMBO).
4. Open a chest or door: the message above the HUD.
5. Inventory: item tooltips; character and journal screens.
6. A conversation and a multi-line panel: wrapping at the larger size, or text cut off at the bottom (expected for fixed panels, phase 4).
If anything crashes or looks broken, set `[Debug] TextHook=0` to get the game's own text layout.

## HD font pages (offline)

Own metrics (step 5): `tools/font_preview.py … --own-metrics` writes the face's metrics for every em size, runs `test_font_pages layout` (em size, new cells, packing; checks: every cell packed, a free texel to every other glyph, no overlaps, line height kept) and builds the pages into the new cells (`build … newcells.txt`).


`src/font_pages.inc` is tested on its own (no game data needed):

```sh
cc -O2 -o /tmp/test_font_pages tests/fonts/test_font_pages.c -Isrc && /tmp/test_font_pages selftest   # SELFTEST PASSED
```

Credits font (`[Fonts] Credits`, 2026-11-27): `python3 tools/credits_font_preview.py --game <folder> --out <dir>` renders the face's glyphs at the em `fpg_ui_em` picks ('H' 19 texels x k) and runs `test_font_pages credits`: 191 records, 'H' on 4E, fill FCFCFC, every given glyph drawn with fill (at most 2 kept: '~'), codes the face lacks unchanged, nothing outside cred's rectangles changed (CREDITS PASSED); writes an original-vs-redrawn preview. The replay suite runs it ("credits font") and checks `CreditsFont=Credits`, the font and its licence in `dist/fonts` and the release list.

Combat numbers (`[Fonts] Numbers`, 2026-11-27): `python3 tools/number_font_preview.py --game <folder> --out <dir>` renders the 13 glyphs at the em `fpg_num_em` picks and runs `test_font_pages numbers`: colours yellow/red (numr) and green/dark green (numg) from the pages, baseline row 12, '8' 6 texels tall, all 26 glyphs drawn with fill, nothing outside the two fonts' rectangles changed (NUMBERS PASSED); writes an original-vs-redrawn preview. The replay suite runs it ("number font") and checks `NumberFont=Montserrat` in the shipped ini.

Outline room (`FpgStyle.edgeClip`, 2026-11-27): the selftest draws a letter as wide as its cell with a k/2 outline; without the flag the ink is condensed by 2 × outline, with it the ink keeps the full width (k = 2–4).

Interface font in layers (`[Fonts] Interface`): the selftest checks `fpg_glyph_layers` on a synthetic letter (face on top, shade band at the bottom, line and outer rings, nothing outside the rectangle, cut-out transparent, no ink → untouched, lowercase → capitals). With a `Log=1` session's page dumps and the game's `data\*.fnt` in one folder, `python3 tools/ui_font_preview.py --game <folder> --face dist/fonts/Montserrat-ExtraBold.ttf --out <dir> --k 4 --scale 3.0` renders the glyphs, runs `test_font_pages ui` (colours from the pages: 16xY yellow/red, 16xX cut out; every glyph drawn with ink; nothing outside the three fonts' rectangles changed: UI PASSED) and writes before/after previews.

Conversation (2026-11-26): harness `e` = EndFrame (flush + `ui_conv_end`), `HQ <material> <x0> <x1>` = a held portrait drawn by the proxy; replay test "conversation" builds frames from session 20048 capture 12 (letterbox, dialogue box, portrait, a glyph) and checks the square portrait at the top right (3077.25-3725.25), the box and text moved by +259.25, one held portrait in a frame without it, none without the letterbox, nothing with Layout=0. In game: talk to an NPC through several lines; the log's `conversation:` lines name every frame range where the game skipped the portrait.

Menu places (2026-11-26): harness `M <W> <H> <w> <h> <maxPct>` prints the main-menu copyright position (`tx_copyright_xy`: right edge at 897/1024 of the width, bottom at 756/768, as at 1024×768) and the save-list date's "..." limit (`tx_date_limit`: 200 px × the largest text size); replay test "menu places" checks the numbers of sessions 24680 and 16048 and the 1024×768 case.

Menu font (`[Fonts] Menu`): `tools/menu_font_preview.py --game <folder> --face dist/fonts/SoulbringerUncial-Regular.ttf --face dist/fonts/MedievalSharp-Regular.ttf --with-journal [--name-face <ttf>] --out <dir>` writes own-metrics tables ("SBM3": any of phil/hbut/magc/mgrn/mred, PIL metrics; `--name-face` adds mgrn/mred, as `[UI] NameFont=Custom`; glyphs not in a face keep their original rectangles, as in the DLL), runs `test_font_pages layout` (the DLL's em choice, new cells, packing; fails if a cell is not packed, touches another font, or the line height changes) and draws menu words from the new cells next to the original letters (`menu_font_preview.png`). The replay suite runs it as "menu font" (with `SBM_GAME_DIR`), and checks the shipped ini (`MenuFont=EagleLake`), the release file list, and that Soulbringer Uncial's digits are lining (all on the baseline, one height). `tools/make_menu_font.py` rebuilds that font from `EagleLake-Regular.ttf`.

With the dumps of a `Log=1` session (`SBM_fontpage_*.raw`, `SBM_palette.raw`) and the game's `data\*.fnt` in one folder, `tools/font_preview.py --game <folder> --out <dir> --face NAME=font.ttf …` builds the pages for each face through the same C code (`test_font_pages build`, which checks: nothing outside the phil/hbut rectangles or in the other fonts changes, every redrawn glyph has ink, 'H'/'x'/'n' on the baseline) and writes `preview_phil.png` / `preview_hbut.png`.

## Session checklist: per-place fonts (`FontPlaces=1`, TEXT_SCALING_PLAN phase 5b, steps 1 and 3)

Settings: `[Debug] Log=1`, `FontPlaces=1` (everything else as you play). Nothing should look different: every place still draws at its setting's size. The log names the font copy behind every new piece of text, so the session only needs to **show every screen once**; take one F11 on each so a screen can be matched to its lines:

1. Main menu, then the save/load list, then the options menu (open each options page/tab).
2. Load a save. In gameplay: hover the HUD column move slots (CUT, CUT LOW, COMBO…), select a different move, hover the bar.
3. Inventory: items with stack counts, gold, encumbrance; hover an item.
4. Character screen (stats and their numbers), journal, map if it has text.
5. Talk to someone (conversation), pick up an item ("Gained object"), open an empty chest ("Empty.").
6. Quit from the menu (quit dialog).

Step 3 (naming session with first sizes): the same tour, with a few item sizes, e.g. `[Text.Menus] Options=150`, `[Text.Screens] Character=300`, `[Text.Game] HudLabels=250`, `[Text.Game] PlaceNames=300`. Check: only those screens change size (the selected HUD move label too); their text still fits and wraps inside its panel; every other place looks as before. The log's startup lines list the sizes set (`text sizes by place: OptionsTextScale=150 …`) and each sized copy (`copy for place 4471BB font 16xY.fnt … at 150% (place size): scaled`). Places not seen yet that the session should reach: a **conversation** (magc), the quit dialog, the map, place names, a pick-up message (mred).

Report anything that looks different, cut off or wrong (it should not, apart from sized places), and any crash. Afterwards: the `place:` lines (`place: 16xy copy@4291A8 "Strength"`, or `place: hbut obj 3F981140 by 6721CF "…"` for fonts without copies) give the place map; `16xy obj … by 67FBA4` lines are 16xy text that still uses the shared original (a place not redirected yet).

## Session checklists: HD fonts (`docs/HD_FONTS_PLAN.md`, section 5)

The plan's step table is the source; these are the in-game parts.
- **Step 1b, texture-format list** (`Log=1`, nothing visible changes): start, main menu, quit. Check: `format list: texture N: code …` lines, one per format the renderer can create, plus the converters line.
- **Step 1, surface probe** (`Log=1`, nothing visible changes): start, main menu, load a save, open the journal, alt-tab out and back, open the journal again, quit. Check: the log has `font surface:` lines for slots 4E–51 with the surface format, an `upload … surface sum` line per upload, any `LOST`/`restored` lines around the alt-tab, and `SBM_fontsurf_4E…51.raw` plus `SBM_palette.raw` appear in the game folder. Offline: `python3 tools/font_surface.py <game folder>` decodes each surface, compares every texel with the palette colour of its index and writes PNGs (`--selftest` checks the reader with synthetic 4444 and colour-keyed 565 pages).
- **Step 3, larger pages** (development builds before 2026-11-28; `[Debug] FontPages=1`, `Log=1`, no redraw): F11 on the main menu, a HUD tooltip, an inventory tooltip and the journal (twice), then quit. Text must show the same letters in the same places as before (with `TextHook=1` they may look a little crisper or differently soft: the page is enlarged with nearest sampling instead of being magnified bilinearly at draw time); nothing missing, garbled, dark-fringed or boxed. Log: `font pages: on`, one `upload enlarged` line per slot, `font surface: slot 4E created … 512x512 … 32 bits … A FF000000`. Offline: `tools/font_surface.py <game folder>` must explain 100% of the texels (the 512² surfaces against the 256² indices).
- **Step 4, redraw** (development builds before 2026-11-28; now `[Fonts] Tooltips`/`Journal`; then `[Debug] FontPages=1`, `FontRedraw=phil,hbut`, `Log=1`; log: `font redraw:` lines with the face GDI gave and glyphs redrawn per page): F11 on the journal, HUD tooltips, inventory tooltips, a place name, a chest/door message; alt-tab and back; journal again. Report anything garbled, cut off or misplaced.

## Session checklist: other resolutions (deferred)

Not now: the current focus is 3840×2160 at 16:9 (see ROADMAP "Later: resolution pass"). Everything above has been play-tested at 3840×2160 only. For each of 1920×1080, 2560×1440 and a 16:10 mode (e.g. 1920×1200), set the resolution in the game launcher, keep `Log=1`, and do a short session (F11 on each):
1. Main menu and options screen (pillarbox, menu tooltips).
2. Gameplay: hover and click each HUD button on the bar and the column (`[Interface] Hud=split`, then `right`), hover a labelled object.
3. Inventory: hover items, including the rightmost column (item tooltips over their own cell).
4. A conversation and a multi-line panel.
Replaying these sessions adds real captures at that resolution to the tests (today all recorded captures are 4K; the 16:10 and 21:9 checks are synthetic).

