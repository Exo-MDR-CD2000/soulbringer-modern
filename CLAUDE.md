# CLAUDE.md

Working notes for AI-assisted development of Soulbringer Modern. Read this first; details live in `docs/`.

## What this is

A drop-in proxy for Soulbringer's renderer plug-in `D3d7Lib.dll` (Stealth engine, 2000). It forwards all 76 cdecl exports to the original (`D3d7Lib_orig.dll`), patches the running exe in memory (widescreen), and re-places UI vertices before the renderer draws them (menus pillarboxed at 4:3, HUD layouts, aspect cap). No game files are shipped or modified on disk.

Target exe: `SoulbringeVCnoeax.exe` (GOG build, also sold on Steam). All addresses in the docs are for that file.

## Layout

| Path | Contents |
|---|---|
| `src/soulbringer_modern.c` | DLL: config, widescreen patch, forwarding hooks (`hook_pre`/`hook_post`), debug log, `DllMain` |
| `src/ui_layout.inc` | layout logic (shared with the test harness) |
| `src/hover_log.inc` | debug log: cursor, tooltips/labels, text, world under the cursor |
| `src/dinput_mouse.inc` | DirectInput mouse vtable patch: HUD mouse mapping (`HudMouse`, always on with the layout) and gameplay clicks for the log |
| `src/text_decode.inc` | debug: decodes drawn glyph batches back into text using the game's fonts (`data\*.fnt`) |
| `src/font_places.inc` | text sizes: the `[Text]` settings table `ts_key` (item -> category All -> Size; font kinds; items' places) and per-place copies of a shared font (site table, deep copy, refresh on reload, spacing, `fp_pct`, `[PlaceTextScale]` developer override); plain C, shared with the harness; the imm32 redirects and the `place:` log are in `soulbringer_modern.c` |
| `src/ttf_name.inc` | face name (name ID 1) and weight (OS/2) of a font file, for `[Fonts] Names` / `Menu` = `<file>`; plain C, shared with the harness |
| `src/text_scale.inc` | `[Debug] TextHook`: game-side text scaling (font metrics after load, texel rewrite at add glyph, spacing setter); shared with the harness |
| `src/font_probe.inc` | debug, read only: font texture pages 0x4E–0x51 (create/upload records, index and converted-surface dumps, palette, checksums, `IsLost`, other slots' formats) |
| `src/font_pages.inc` | HD fonts: k× A8R8G8B8 font pages, glyph redraw from coverage bitmaps, font colours, `.fnt` cells (plain C, shared with `tests/fonts/`; included by the DLL) |
| `src/font_hd.inc` | HD fonts in the game (on whenever `[Fonts]` redraws a font): font slots 0x4E–0x51 created k× in A8R8G8B8, uploads enlarged (argument swaps at CreateTexture/OverWriteTexture, renderer-build byte check); GDI redraw into the locked surfaces (`fpg_config` maps the `[Fonts]` choices) |
| `dist/fonts/` | `Cardo-Bold.ttf`, `Montserrat-ExtraBold.ttf`, `SoulbringerUncial-Regular.ttf` (Eagle Lake with lining figures, made by `tools/make_menu_font.py` from `EagleLake-Regular.ttf`, which is kept as its source and not shipped), `MedievalSharp-Regular.ttf`, `SoulbringerText-Regular.ttf` (Soulbringer Text: the user's upscaled outline version of the game's text font, OFL; default for names, tooltips and journal) + their OFL texts (never bundle a font without a licence that allows it, e.g. Morris Roman (`MenuFont=Custom`) and Procopius (`NameFont=Custom`): private use only), packed into `SoulbringerModern\fonts\` by `make_release.sh` |
| `src/screenshot.inc` | debug: `ScreenshotKey` (F11) saves a BMP to `Screenshots\` plus a frame capture of the same frame; GDI loaded at runtime |
| `src/gen_stubs.py`, `src/exports.txt`, `src/imports/*.def` | export stubs and import libs (no Windows SDK) |
| `dist/` | install/uninstall scripts, ini, README.txt shipped in the zip |
| `tests/replay/` | harness (includes `ui_layout.inc`) + `replay.py` |
| `tests/data/` | recorded sessions (coordinates and IDs only, no game assets) |
| `tools/sbm_log.py` | log reader |
| `tools/font_preview.py` | HD fonts step 2: renders a TrueType face into the font pages (through `tests/fonts/test_font_pages.c`) and writes preview images |
| `tools/font_surface.py` | renders/checks `SBM_fontsurf_*.raw` against the index dumps and palette (`--selftest`) |
| `tools/credits_font_preview.py` | credits font (`CreditsFont`): cred from Soulbringer Credits through `test_font_pages credits`, preview original vs redrawn at 280 % |
| `tools/number_font_preview.py` | combat numbers (`NumberFont`): numr/numg from Montserrat ExtraBold through `test_font_pages numbers`, preview original vs redrawn at 250/400/600 % |
| `tools/ui_font_preview.py` | interface font (`InterfaceFont`): layer previews of a face in the 16xY/16xB/16xX rectangles (through `test_font_pages ui`) |
| `tools/menu_font_preview.py` | menu font (`MenuFont`): own-cells layout check for magc (with phil/hbut) through `test_font_pages layout`, and menu-word previews next to the original letters |
| `tools/make_menu_font.py` | builds `dist/fonts/SoulbringerUncial-Regular.ttf` from `EagleLake-Regular.ttf` (lining figures, renamed) |
| `tests/fonts/test_font_pages.c` | offline tests of `font_pages.inc`: `selftest`, `layout` (own cells), `ui` (interface layers) |
| `docs/DEVLOG.md` | reverse-engineering log: every finding, address and decision, dated |
| `docs/RENDERER_API.md` | renderer interface, draw callers, engine addresses |
| `docs/ROADMAP.md` | plan and agreed order |
| `docs/TEXT_SCALING_PLAN.md` | detailed plan for step 3 (readable text): findings, design, phases, tests, risks |
| `docs/HD_FONTS_PLAN.md` | legible letters: redraw `phil`/`hbut` (Cardo), the interface font (Montserrat ExtraBold) and `magc` (Eagle Lake) into larger font pages; step table with the user's input per step (living plan) |

## Build and test

```sh
./build.sh                         # -> build/D3d7Lib.dll (clang -target i686-pc-windows-msvc, lld-link, no CRT); PYTHON=python if there is no python3
CC=clang python3 tests/replay/replay.py   # must print ALL PASSED (CC = a native C compiler, default cc)
./make_release.sh                  # -> release/SoulbringerModern-<SBM_VERSION>.zip
```

Run the replay after every change to `ui_layout.inc` or `hover_log.inc`. The harness has stand-ins for everything the DLL provides (`LOG`, `GetAsyncKeyState`, DirectInput globals…); add a stand-in when `ui_layout.inc` starts using something new.

## Settings (since 2026-11-28)

The ini is organised by area: `[Display]`, `[Interface]`, `[Text]` + `[Text.Menus]` / `[Text.Screens]` / `[Text.Game]`, `[Fonts]`, `[Debug]` (shipped `dist/SoulbringerModern.ini`; read in `init()` in `soulbringer_modern.c`, text sizes in `fp_config` from `ts_key`). Rules:
- A new text size is a row in `ts_key` (section, key, parent, kind, place kind, places), added to the shipped ini as `<Key>=0` in its section; the replay test "text settings" checks both. No per-font-kind keys in the ini: kinds stay internal.
- A new font choice goes into `[Fonts]` with plain names (`ini_pick` value list), empty = follow `All`. Font-texture plumbing turns on by itself; don't add switches for it.
- Troubleshooting and developer switches live in `[Debug]` only. The text hook is always on (`[Debug] TextHook=0` only to troubleshoot).
- Old keys are not read (user's decision): when renaming, convert the user's ini on the PC and note the mapping in DEVLOG.
- Later: an in-game settings menu on a function key (ROADMAP 0.4); keep settings table-driven so it can list them.

## Code rules

- **No C runtime.** Only kernel32/user32 imports listed in `src/imports/*.def`; add the decorated name there when using a new API. Debug-only APIs from other DLLs (GDI for screenshots) are loaded at runtime with `GetProcAddress`, so the import table stays the same.
- **Stack:** built with `-mno-stack-arg-probe` (no `__chkstk`). Keep large locals in `__attribute__((noinline))` helpers so `hook_pre` stays small.
- Log lines end in `\r\n` (C escape, not a literal backslash). Floats go through `ftoa()`.
- Everything runs on the game's main thread; `hook_pre` returns early on other threads.
- Debug-only features stay behind `g_cfgLog` (`[Debug] Log=1`).

## Safety rules (learned the hard way)

- **The renderer DLL is loaded twice** (hardware check, then for real). Anything patched into the game or another DLL must be restored in `DllMain` detach, or the game calls into an unloaded DLL (0.1.0 crash). Recognise and re-point leftovers of our own patches.
- **Verify original bytes before any code patch**; skip the patch if they don't match.
- **DirectInput:** never replace an object's vtable pointer (DirectInput validates it; the game crashed after the intro). Patch entries in the existing vtable with `VirtualProtect`, filter by device pointer, restore on unload. DIDEVTYPE: mouse = 2, keyboard = 3.
- **UI vs world:** classify by `rhw` (`< 0.1` = world), never `z == 0`.
- **Menu vs gameplay** is decided from the current frame only (the 6-tile menu background); gameplay flushes the HUD before the world.
- The gameplay cursor and its tooltips are only moved by the HUD mouse offset (real − game x, `g_hmDX`); over the world that is 0. The game cursor lives at `0x3559114` and is filled by DirectInput in place.

## Testing on the user's PC

- Test folder: `C:\Users\ExoJag\Documents\soulbringer-testfiles\` (a copy of the game). Mirror of this repo: `...\soulbringer-testfiles\soulbringer-modern\`.
- Deploy `build/D3d7Lib.dll` to **both** the game root and `data\Stealth\Render\` (next to `D3d7Lib_orig.dll`). The game must be closed (the DLL is locked while it runs).
- Copy through a **new, uniquely named** staged file each time (a reused name can send a stale copy), then **re-stage and compare SHA-1** of both deployed copies.
- Logs appear in the game root as `SoulbringerModern_<exe>_<pid>.log`; the largest recent one is the real session (small ones are launcher/probe loads).
- Test monitor: 3840×2160 (16:9). Ultrawide is untested; say so when publishing `MaxAspect`.
- Game data (fonts, textures, page dumps) is never committed; tests read it through `SBM_FONT_DIR` / `SBM_GAME_DIR`. Exception planned for HD fonts: **Cardo Bold** (`Cardo-Bold.ttf`, SIL Open Font License, the chosen face) may be committed and shipped, with its licence text; the same applies to an alternative OFL face if the choice changes.
- `docs/HD_FONTS_PLAN.md` section 5 is a **living plan**: update its step table (status, user input, exit checks, changed fallbacks) after every step or finding.
- The user plays and reports; prefer automatic logging over asking them to press keys. For HUD/button work, ask for F11 screenshots (`Screenshots\SBM_<pid>_<n>.bmp`); each one is paired with `FRAME CAPTURE <marker>` in the log via its `event: screenshot` line.

## Docs to keep current with every change

`CHANGELOG.md` (under the next version), `docs/DEVLOG.md` (dated entry: finding, fix, test), `docs/ROADMAP.md` (tick boxes), the plan being worked on (`docs/TEXT_SCALING_PLAN.md`, `docs/HD_FONTS_PLAN.md`), `docs/RENDERER_API.md` (new renderer facts), `README.md` (features table, known issues, settings), `tests/README.md`, `dist/SoulbringerModern.ini` comments, `dist/README.txt`. Bump `SBM_VERSION` when releasing.

## Agreed order (2026-11-21)

**Target (user, 2026-11-21): 3840×2160 at 16:9 first.** Everything is designed to follow the screen size, but play-testing, defaults and polish aim at 4K 16:9. Other resolutions (1080p, 1440p, 16:10) are a deferred test pass (ROADMAP "Later: resolution pass"); wider than 16:10 is not a focus. Don't ask the user to test other resolutions until 4K is done.

1. HUD layout options (done)
2. HUD hover/click mapping: `HudMouse` done, play-tested at 4K with split and right
3. Text scaling: fonts, tooltip/label boxes, margins and cursor offsets together (`TextScale`); phases 1–2 done, phase 3 built behind `[Debug] TextHook=1`, one size per kind of text since 2026-11-23 (`[UI] JournalTextScale`, `Tooltip…`, `Menu…`, `Interface…`, `Name…`, `Number…`, `CreditsTextScale`, by font; font map in TEXT_SCALING_PLAN section 3); next: HD font pages, `phil`/`hbut` redrawn in Cardo, `magc` enlarged only, `16xY`/`16xB` later; steps 1/1b done: font surfaces are A1R5G5B5 today, the renderer offers A8R8G8B8 (code 0x80) for them; step 2 done, face **Cardo Bold** chosen for phil and hbut (other faces listed as options in the plan); step 3 done (512² A8R8G8B8 font slots, `[Debug] FontPages=1`, checked in game 22:39); step 4 done 2026-11-23 (GDI redraw into the surface + `IDirect3DDevice7::Load` of the renderer's cached copy, `[Debug] FontRedraw=phil,hbut`); step 5 started 2026-11-23: own metrics for phil/hbut (`[Debug] FontOwnMetrics=1`, new cells packed into free page space, spacing/kerning 0) checked in game; alt-tab confirmed; tooltip boxes kept off the cursor, cursor-tooltip text recognised by font (hbut, mgrn, mred); **now: per-place sizes for shared fonts (TEXT_SCALING_PLAN phase 5b)**: static analysis done 2026-11-23 (text object `+0x208` = its font; per place a copy of the font object, the place's `mov reg,[0x35565DC]` redirected to it; DEVLOG "per-place fonts"); logging build done 2026-11-23 (`[Debug] FontPlaces=1`); every 16xy place mapped (sessions 27032, 22252, 10752; 38 sites); goal agreed 2026-11-24: every font controlled per place (menu font for main menu vs. conversations, etc.); step 1 done 2026-11-24 (magc/hbut/mgrn/mred sites added, 92 places, build `a8793d49`; sessions 2026-11-26 named the main menu, save list, tooltip and NPC-name places); 16xb/16xx sites added 2026-11-26 (HUD selected move label etc., 98 places); step 2 done 2026-11-26 (per-place sizes `[PlaceTextScale]` `<site>=<pct>`, 0 = the kind's, build `701a3b62`); step 4 first part done 2026-11-26 (10 settings named by screen in `[UI]`, `fp_group`; place names by load site `fp_load`; address list `[PlaceTextScale]` only a hidden developer override); auto-fit fix 2026-11-26 (enlarged mode-2 labels kept on one line: hook after `0x411634`, `tx_autofit_choice`; to confirm in game on options, stats, HUD column); quit dialog places found (magc `6549BF`/`654D5D`/`6550E3`, `66569A`/`665A36`/`665DBA`); interface font redrawn 2026-11-26 (`[UI] InterfaceFont`, Montserrat ExtraBold in layers, original rectangles; `fpg_glyph_layers`); menu font redrawn 2026-11-26 (`[UI] MenuFont=EagleLake | MedievalSharp | Custom | 0`, own cells for magc, own metrics generalised to phil/hbut/magc; Eagle Lake shipped as "Soulbringer Uncial" with lining figures, `tools/make_menu_font.py`; Morris Roman private only); checked by the user 2026-11-26 ("looks pretty good"); copyright line kept at the bottom right (`0x666143`) and save-list date limit scaled (`0x645ED4`) 2026-11-26 (build `93bd4461`); confirmed; name font 2026-11-26 (`[UI] NameFont=Custom` for mgrn/mred, own cells, five own-cells fonts now; default `NameFont=Text` = the bundled Soulbringer Text; `Custom` = `[Debug] NameFontFace/File`, e.g. Procopius, private, never bundle it); tooltip and journal fonts from the same face (`[UI] TooltipFont`, `JournalFont` = `Text | Original | Cardo`, override FontRedraw; hbut = mgrn = mred letters, phil one pixel larger); face renamed 2026-11-26 to **Soulbringer Text** (`SoulbringerText-Regular.ttf`, ini value `Text`, `Mgrn` still accepted); **next: check journal, names and tooltips in game (Text vs Original), then the ini simplification pass the user asked for**, then phase 5b step 3, one naming session (conversations, quit dialog, messages still unseen) with first per-screen sizes, then add `ConversationTextScale` etc.; the shared text-object pool `0x1E4AF64` (stack counts) needs per-entry fonts, deferred; UI 3D models scaled to the 4:3 area (2026-11-24, confirmed); then redrawing `mgrn`/`mred` (`docs/TEXT_SCALING_PLAN.md`, `docs/HD_FONTS_PLAN.md`)
4. Conversation screen: started 2026-11-26 (portrait square at the top right, dialogue box follows it, portrait held in the frames the game skips at line changes; build `3a1e36f0`, confirmed; 2026-11-27: `[UI] ConversationTextScale` (`6800EF` dialogue, `68017B` choices) and the menu font's loose spacing fixed (`FpgStyle.edgeClip` for magc: the outline no longer condenses the letters); confirmed by the user at 300% (build `612c2792`); the choice list stays at the top left (user); quit dialog sizing parked by the user (ROADMAP)); credits screen parked (user); combat numbers numr/numg redrawn from Montserrat ExtraBold 2026-11-27 (`[UI] NumberFont=Montserrat`, build `bae57729`, user: "looks good"); street-talk portrait (guards, no letterbox) squared 2026-11-27 (build `d3f7cab0`, user: "working"); quest message re-centred (build `b75ad38d`, user: fixed); credits font cred from the user's upscale "Soulbringer Credits" (`[UI] CreditsFont=Credits`, build `d10d8288`, user: working)
   ini simplified 2026-11-28 (sections by area, text sizes by category, `[Fonts] All`, text hook always on; DEVLOG "settings file simplified"); open 0.2 items on hold until the user's playthrough (ROADMAP 0.2 "On hold", 2026-11-28); clean upload folder `soulbringer-modern-upload` made the same day
5. 0.2.0 release
6. 0.3 stability: document `patch.dll`; frame cap is DDrawCompat's job (game runs too fast above 60 FPS; finding the per-frame steps is a possible proxy fix, interpolation is out of scope: ROADMAP); alt-tab
7. Polish
8. EAX via DSOAL: last
9. HUD skins: someday (roadmap "Later")

## Versioning and releases

Semantic versioning; `-dev` suffix between releases, `-beta.N` for test builds. Release: set `SBM_VERSION`, update CHANGELOG, push the tag `v<SBM_VERSION>`; GitHub Actions (`.github/workflows/build.yml`) builds, runs the offline tests and creates a **draft** release with the zip (pre-release when the version has a suffix; notes from the CHANGELOG section of the base version); the user publishes it. The workflow fails on a tag that differs from `SBM_VERSION`. Every push to `main` is built too (zip as a workflow artifact). `release/` is not committed. Zips built locally for testing are copied to `C:\Users\ExoJag\Documents\soulbringer-github\` (user, 2026-11-28). 0.1.0 is withdrawn (crash); 0.1.1 is the last full release (not on the Releases page); 0.2.0-beta.1 (2026-11-28) is the first build released through Actions.
