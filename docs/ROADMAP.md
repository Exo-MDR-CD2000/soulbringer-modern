# Roadmap

Priorities: first a correct widescreen picture, then a UI that fits it, then everything else. Audio (EAX) comes last.

## 0.1: Widescreen (done)
- [x] Proxy renderer DLL that forwards all 76 calls to the original
- [x] Native Hor+ widescreen for the 3D world, any aspect ratio
- [x] Installer and uninstaller, settings file, developer log

## 0.1.1: Reload fix (done)
- [x] Widescreen patch removed when the renderer unloads, and re-pointed if left over (0.1.0 could crash)
- [x] F9 full-frame capture in the debug log

## 0.2: Scalable UI and HUD (in progress)

> **On hold (user, 2026-11-28): the open 0.2 items below wait for a full playthrough.** The user plays through first and decides which of them are still needed. Nothing is dropped; each keeps its notes further down. On hold:
> - naming session for the remaining text places, and putting them into `[Text.*]` items (Phase 5b steps 3–4 rest: in-game save/load list, journal tabs, inventory, combo screen; the quit dialog now follows `[Text.Menus] All`)
> - wrapping and boxes at the larger size (Phase 4), multi-line panels, hover labels staying next to their world object
> - stack-count size (needs a per-pool-entry size), interface font own metrics if the spacing looks too wide
> - HUD: naming the column icons, the locked bottom-right orb, the click trade-off in the world strips beside the HUD, recording a session with HUD/world/menu clicks
> - conversation portrait-flicker fix: check against videos of the original (`[Debug] ConversationPortraitHold=0` turns it off)
> - parked by the user: credits screen layout, quit dialog sizing; the long "Q" tail of Soulbringer Uncial in "Quit"
> - older checks still open: in-game check of the tooltip stretch (only without the text hook now), the "hovered word looks magnified" report, Phase 1 text-type session, Phase 5 per-category policy / `Size=auto`, Phase 7 sharp letters
> - Phase 6 release hardening and the 0.2.0 release steps (docs, full replay pass, every HUD layout in game, release zip) follow once the playthrough settles the list.
Order agreed 2026-11-21: layout first, then HUD input (it depends on where the HUD is placed), then text.

**1. Layout**
- [x] Screen type per frame: full-screen menu (6 background tiles) or gameplay. Decided from the menu background only, never from the previous frame.
- [x] Menus pillarboxed (main menu, options, inventory, character, journal…)
- [x] HUD parts: bottom bar and right action column, each moved as a unit, correctly proportioned
- [x] **`HudLayout`**: `split` (default), `right` (whole HUD in the bottom-right corner), `center`, `stretched`
- [x] UI 3D models grouped and moved with their panel; cursor and tooltips stay with the real mouse in gameplay
- [x] UI 3D models (inventory items, HUD weapon icon) scaled to the 4:3 area: the game sizes them by the screen width, 1.33× too big at 16:9, which hid the stack counts (2026-11-24, confirmed in game)
- [x] Cursor enlarged (`CursorScale`)
- [x] **`MaxAspect`** cap: wider screens pillarboxed to the cap (black bars drawn by the proxy), HUD anchored to the capped edges, menus unchanged
- [ ] Conversation screen: portrait panel and text placement
  - [x] Portrait square again (it was stretched 4:3 -> 16:9) at the top right; dialogue box and text move with it (sizes were never stretched: drawn in screen pixels) (2026-11-26, build `3a1e36f0`; confirmed in game)
  - [ ] Bug (user, 2026-11-23): the NPC portrait flickers briefly when the NPC's voice line ends and the next dialogue box appears. **Found 2026-11-26** (session 20048): in a 30 s conversation window the sprite caller `0x4A9195` committed 3627 quads for 1850 frames instead of 3700 (portrait + cursor each frame): the game skips the portrait in ~73 frames, i.e. a few frames at each line change. Built: the last portrait is drawn again in a frame that has the letterbox but no portrait (up to 30 frames); the log names each gap (`conversation:` lines); session 1668: exactly 1 frame per line change, filled. **Provisional** (user: may be the original behaviour, to check against videos): `[Debug] ConversationPortraitHold=0` turns it off; removal steps in DEVLOG "Portrait hold: provisional"
  - [x] Conversation text size (`ConversationTextScale`, 2026-11-27: dialogue `0x6800EF` + choice list `0x68017B`); loose letter spacing of the menu font fixed (outline no longer condenses the letters)
  - [x] The choice list stays at the top left (x = 20, as in the original): user decision 2026-11-27

**Logging groundwork (done)**
- [x] Hover events: cursor, tooltip/label box, offset, text metrics, world draws under the cursor
- [x] Text catalogue with call chains; UTF-16 strings
- [x] Click events in menus (Windows) and gameplay (DirectInput mouse), with region, label and object under the cursor

**2. HUD hover and clicks** (done except the open items below)
- [ ] Record a session with clicks on HUD buttons (Inventory etc.), world objects and menus (first one: 4 bar clicks, 0 column, session 15:17)
- [x] Inverse HUD mapping `ui_hud_aim()` (drawn point -> game point), shared by the debug log and the tests; round-trip, measured-offset and recorded-click tests
- [x] Debug log: `aim:` line per gameplay click (miss in px) and `event: click result` (what the click did)
- [x] Debug screenshot key (F11) paired with a frame capture, for naming HUD buttons
- [x] `tests/data/hud_buttons.txt`: 34 HUD buttons (canvas units), from F11 screenshots and tooltips; one map serves every HudLayout
- [ ] Name the column icons from their tooltips
- [ ] Right bottom orb: locked until later in the game; check its tooltip and click once it unlocks
- [x] Fix implemented and tested offline; clicks logged with the fix are checked strictly by `replay.py`
- [x] Find where the game keeps its cursor position (`0x3559114`, filled by DirectInput in place) and how it hit-tests the HUD (many places; coarse over-HUD test `0x5171E8`)
- [x] Give HUD hit-testing the cursor position mapped back through the active `HudLayout`; the world and the drawn cursor keep the real position (`HudMouse`, `ui_hud_mouse`)
- [x] In-game check `split`: 137 clicks, 54 on the HUD, all on the right button (session 16:57)
- [x] In-game check `right` (user, 2026-11-21 22:18)
- [x] World strips that were HUD before: the game gets the nearest point outside its HUD (`ui_hud_dead`), no ghost tooltips
- [ ] *Possible fix for the trade-off* (world clicks in those strips act at the nearest point above the bar line / left of the column line): keep the HUD-safe cursor by default, let the 9 world-interaction functions (`0x508C68`, `0x540C34`, `0x554E30`, `0x555140`, `0x557384`, `0x558DA8`, `0x58A820`, `0x58DE90`, `0x58DEF0`) see the real mouse while they run (entry/exit code hooks, verified bytes, restored on unload), and replace the coarse over-HUD test `0x5171E8` with one on the drawn layout. ~10 code hooks; world hover labels may come from code not found yet. Deferred: not a big issue in play. Details in DEVLOG ("Can the world strips get the real cursor?").

**3. Readable text** (detailed plan: [`TEXT_SCALING_PLAN.md`](TEXT_SCALING_PLAN.md))
- [x] Measure tooltips and labels: all fixed pixel sizes (46 px boxes, 19/17 px margins, 9–11 px glyphs, fixed cursor offsets), see DEVLOG
- [x] Cursor tooltips and labels (one line, next to the cursor): box, text, margins and offset scaled together around the cursor (`TextScale`), kept on screen; done in the proxy, no game hooks. Tested offline (67 recorded tooltips)
- [ ] In-game check of `TextScale` (sharpness of upscaled glyphs, default value)
- [x] Inventory item tooltips (centred 88 px below the cursor) were not scaled: now recognised (session 21:45), and scaled around their own centre so they stay over the item (21:56)
- [x] Messages ("Empty.", "Locked.", "Gained object: Spade": one line, centred on the screen above the HUD) were not scaled: now grow upwards from their bottom centre (session 22:13)
- [x] Small tooltip flashing at the HUD's old position while moving the mouse: tooltips are now matched to the game's cursor of the same frame (confirmed fixed by the user, 22:48)
- [ ] **Bug: hovered word looks magnified** (reported 21:25): hovering anything with text, the word in the tooltip appears magnified when the tooltip shows. The logs (session 21:24) show only the tooltip's own glyphs are scaled (drawn right after its box; no other text in its line), so this is most likely the look of the game's small bitmap font stretched 2× next to unscaled text. Needs a screenshot to confirm. Likely fixed by the text-layout work below (draw text at the larger size instead of stretching the result).
- [x] Font file format and glyph flush decoded (size = texture rectangle; phase 2 found the one place that uses it as such)
- [x] Phase 1 (offline): text decoded from the glyphs' texture rectangles (`text_decode.inc`): hover labels, `text:` catalogue lines and capture `chars` lines name the real string and font; stack walk removed. Tested offline (all 2,700 glyph shapes of 16 fonts; lines joined across batches)
- [ ] Phase 1 (in game): a session covering every text type, confirming decoded text matches the screen
- [x] Phase 2: map the layout code (font object, glyph-record writer, line step, wrap width, box sizing, cursor offsets) in both executables. Result: every measurement reads the font metrics, only `0x433714` also uses them as the texture rectangle; tooltip margins and cursor offsets already scale with the screen; the game already loads its large font set at ≥ 1000 px wide. Design changed to two hooks (DEVLOG "Text layout code mapped", plan section 3a/5)
- [x] Phase 3 (offline): `[Debug] TextHook=1` (off by default): fonts scaled after load (`0x681F5E`), texels rewritten at add glyph (`0x626480`), letter-spacing setter (`0x691F50`); proxy tooltip stretching off while it is on; tooltip rules accept the game's larger boxes. Tested offline (every glyph of 16 fonts at 150/200/300%, clipped glyphs, font reloads, mutation check)
- [x] Legibility check (1024×768 stock): the original fonts are hard to read even at their own resolution; enlarging alone is not enough. Decision: redraw `phil` + `hbut` in an old-style serif, enlarge `magc`, decorative capitals later (font chart in DEVLOG 2026-11-22; since 2026-11-26 `magc` and the interface font are redrawn too)
- [x] Font page probe: pages are slots 0x4E–0x51, 256×256 8-bit palette indices; glyph rectangles never shared or touching; UVs are fractions (DEVLOG 2026-11-22)
- [x] HD font pages (`docs/HD_FONTS_PLAN.md`, living plan): k x font slots (`[Debug] FontPages`), GDI redraw, own metrics. Every game font is now redrawn: journal `phil`, tooltips `hbut`, names/messages `mgrn`/`mred` from **Soulbringer Text** (the user's upscale, OFL); menus/conversations `magc` from **Soulbringer Uncial** (Eagle Lake with lining figures); interface `16xY`/`16xB`/`16xX` and combat numbers `numr`/`numg` from Montserrat ExtraBold; credits/GAME OVER `cred` from **Soulbringer Credits** (the user's upscale). Cardo Bold stays an option (`TooltipFont`/`JournalFont=Cardo`). Only the low-resolution `5px*` set (below 1000 px wide) is untouched
- [x] Phase 3 (in game): session with `TextHook=1` (checklist in `tests/README.md`): letters correct, tooltips drawn larger by the game, no crash
- [ ] Phase 4: wrapping and boxes at the larger size
  - [x] Labels broken onto two lines by the game's auto-fit once enlarged (options menu, stats page, HUD column): one-line box from the game's own layout (2026-11-26, build `73ab083c`; to confirm in game)
  - [x] Save-list date cut with "..." in its box (another mechanism): the 200 px limit at `0x645ED4` grows with the text size (2026-11-26, build `93bd4461`; to confirm in game)
  - [x] Main-menu copyright line back at the bottom right (`0x666143`, 2026-11-26; to confirm in game)
  - [x] Interface font 16xY/16xB/16xX redrawn in layers from Montserrat ExtraBold into the original rectangles (`[UI] InterfaceFont`, 2026-11-26, build `8a8da356`; to check in game, then the default)
  - [ ] Interface font: own metrics (tighter spacing) if the original advances look too wide
  - [x] Menu font magc redrawn from Eagle Lake (default) or MedievalSharp into its own cells (`[UI] MenuFont`, 2026-11-26; confirmed in game; open: Eagle Lake's long "Q" tail leaves a gap in "Quit"); Eagle Lake's old-style digits made lining ("Soulbringer Uncial", build `48ec9c47`)
  - [x] Name/message fonts mgrn/mred redrawn into their own cells from a face of the user's (`[UI] NameFont=Custom`, `[Debug] NameFontFace/File`; default `NameFont=Text`: Soulbringer Text, the user's upscaled outline version of the game's font (first called `mgrn-regular.ttf`), bundled under the OFL; Procopius (Scriptorium, commercial) only via `Custom`) (2026-11-26; confirmed by the user)
  - [x] Tooltip font hbut from Soulbringer Text too (`[UI] TooltipFont=Text`; hbut, mgrn and mred have identical pixel masks for all 191 glyphs) (2026-11-26, build `acdb8c9d`; confirmed by the user)
  - [x] Journal font phil from Soulbringer Text too (`[UI] JournalFont=Text`; phil is the same typeface one pixel larger: cap 10 vs 9, x-height 7 vs 6) (2026-11-26, build `8654a101`; confirmed by the user: "Looks pretty good"). Cardo now only by choice (`JournalFont`/`TooltipFont=Cardo`)
- [x] **Title logo before the main menu** (closed 2026-11-27: left as is, user's choice) (user, 2026-11-27: looks stretched). RE: `data\logo.bmp` (640 x 366, 24-bit, ratio 1.749) is loaded with `LoadImageA` and `StretchBlt`ed (GDI, call at `0x58A2C9`) over the whole window by the start-up code at `0x58A17C`, **before** D3d7Lib.dll is loaded (`0x597110` at `0x58A39A`, `0x6794C0` at `0x58A36F`), so the proxy cannot hook it (F11 does not work there either). **Measured** (user's screenshot "soulbringer title before menu.png", 3840x2160): the logo's bright area is 3540 x 1351 px against 599 x 228 in the bitmap, ratio 2.620 vs 2.627: shown at its true shape (5.91 x 5.93). In the original 4:3 (640x480) it was stretched 31 % taller; that is the look the user remembers. User: leave it (true shape); not changed.
- [x] **Credits font** (2026-11-27: `[UI] CreditsFont=Credits`, the user's upscale "Soulbringer Credits", OFL, in cred's rectangles; build `d10d8288`, confirmed by the user on the GAME OVER screen; the credits screen layout itself is still parked)
- [ ] **Parked (user, 2026-11-27): credits screen layout** (`CreditsTextScale` sizes it; the font is done, see "Credits font")
- [x] **Combat numbers** (2026-11-27: `[UI] NumberFont=Montserrat`, Montserrat ExtraBold in the original rectangles, user's choice; build `bae57729`, confirmed by the user) (numr red damage "-16", numg green "0"; own font objects loaded by `67FE78` / `67FEF5`, 13 glyphs each, same design; `NumberTextScale`, 250% now = ~20 px tall at 4K, blocky): session 15904 looked at, redraw next (face to choose with the user)
- [x] **Quest message centred** ("A new quest has been added to your journal.": the game centres it on the view left of the HUD column, canvas ~269; re-centred on the screen with Layout on, build `b75ad38d`, confirmed by the user)
- [x] **Street-talk portrait** (guards, passers-by: no letterbox; canvas 363.8-507.8 x 32-176, box right edge 360.5): squared around its 4:3 centre, box follows (2026-11-27, build `d3f7cab0`, confirmed by the user)
- [x] **ini simplification pass** (user, 2026-11-26; done 2026-11-28): sections `[Display]`, `[Interface]`, `[Text]` + `[Text.Menus]` / `[Text.Screens]` / `[Text.Game]` (one Size, a category All, items; 0 = follow the level above), `[Fonts]` (`All=Sharp | Original`, optional choice per font, own fonts as a file), `[Debug]`; text hook always on (`[Debug] TextHook=0` for troubleshooting); old keys not read (mapping in DEVLOG 2026-11-28). To check in game with the converted ini: everything looks as before
- [ ] Phase 5: per-category policy (HUD labels fit their slots, menus), remove the proxy tooltip stretching, `TextScale=auto`. **Started 2026-11-23:** one size per kind of text (`JournalTextScale` … `CreditsTextScale`, by font), asked for by the user after 300% for everything cut off text in the options menu and the journal tab. Done so far: 7 kinds with descriptions checked against what the game draws (`InterfaceTextScale` replaced `TitleTextScale`, `SmallTextScale` folded into it), tooltip boxes kept off the enlarged cursor, COMBO label no longer moved by a tooltip, NPC names stay in their tooltip box
- [ ] Phase 5b (**in progress**, started 2026-11-23; steps 1–2 done): **per-place sizes**: split fonts shared by several places (Interface: options menu / character screen / inventory numbers / HUD labels; Menu: main menu / conversations; Tooltip: tooltips / place names; Name: names / messages) so each can be sized for legibility; plan in TEXT_SCALING_PLAN phase 5b
  - [x] Static analysis (2026-11-23): a text object keeps its font at `+0x208`, read by all layout code; 16xy is copied into it by 28 instructions in 25 functions (38 with the argument sites found later; 92 places over all shared fonts after step 1) (mostly one `mov edx,[0x35565DC]` each); identical in both exes. Approach: a copy of the font object per place, and that place's pointer read redirected to it (same-length address change, no code hooks). DEVLOG "per-place fonts"
  - [x] Logging build (2026-11-23, `[Debug] FontPlaces=1`): every 16xy site gets its own copy at the current size, the log names the place of each text line (`place:` lines); offline tests pass; deployed
  - [x] First session (pid 27032): HUD column labels, COMBO, options menu, map title identified; character screen, inventory numbers and journal titles pass the font as an argument (10 more sites, build `5f238f7e`)
  - [x] Second session (pid 22252): character screen `66B65E`, inventory numbers `660176`/`670A16`, map and item descriptions `67B4E9`; no 16xy text left on the shared original
  - [x] Bug (user, 2026-11-23): inventory stack counts hidden behind the item's 3D model. Cause (Stock=1 at 1024×768, 2026-11-24): the game sizes UI 3D models by the screen width, 1.33× too big at 16:9; models now scaled to the 4:3 area (build `44426a20`); confirmed in game 2026-11-24 ("looks much better")
  - [ ] **Later: stack-count size** (user request 2026-11-24: "slightly larger"). The count is drawn by an entry of the shared text-object pool (`0x1E4AF64`, 64 objects made by one loop at `0x66B65E`), so it needs a **per-pool-entry** font, not a per-site one (DEVLOG "stack count comes from a shared text-object pool"). The place log already names the entry (`pool#k`, build `699245c7`). Deferred until the save has stacks in several slots: then one session (inventory with stacks, click-and-hold one, the character screen) to find the stack-count entries, then a `StackCountTextScale`-style setting (default ~250–300%)
  - [x] Offline tests for the copies (deep copy, independent scaling, reload refresh, spacing, site bytes in both exes)
  - [x] Static scan of the other shared fonts (2026-11-24): magc 33 reads (15 store, 5 argument, 12 to classify), hbut 19 (13 store, 2 argument), mgrn/mred 5 each; details in TEXT_SCALING_PLAN phase 5b
  - [x] Step 1 (2026-11-24): remaining reads classified; magc, hbut, mgrn, mred sites added (92 places in all; 2 magc sites measure text and will follow the place they measure for); build `a8793d49`
  - [x] Sessions 2026-11-26 (all 92 places redirected, no crash): main menu, save list, cursor tooltips, item names, character labels and NPC names named
  - [x] HUD column highlighted labels (2026-11-26): the selected move label ("Cut Low", 16xb) and the state-1 label (16xx) were not places; 6 sites of 16xb/16xx added (98 places), so the HUD labels can be sized together
  - [x] Step 2 (2026-11-26): per-place size mechanism + offline tests: `[PlaceTextScale]` `<site>=<pct>` (0 = the font kind's setting), build `701a3b62`
  - [ ] **Step 3 (next):** one session: every screen named in the place log (still missing: conversations, quit dialog, messages), first sizes tried
  - [x] Step 4, first part (2026-11-26, brought forward: the address list was too hard to use): 10 settings named by screen in `[UI]` (`MainMenuTextScale`, `SaveListTextScale`, `OptionsTextScale`, `CharacterTextScale`, `MapTextScale`, `HudLabelTextScale`, `LabelTextScale`, `PlaceNameTextScale`, `NpcNameTextScale`, `MessageTextScale`); place names sized by their load site (own font object); setting any of them turns the copies on (no `FontPlaces=1` needed); `[PlaceTextScale]` kept as an undocumented developer override
  - [ ] Step 4, rest (`ConversationTextScale` done 2026-11-27; since 2026-11-28 these are the items of `[Text.Menus]` / `[Text.Screens]` / `[Text.Game]`, `ts_key`): remaining unnamed places into items. The real messages come from the second mgrn/mred objects (`0x674891`, `0x674823`); they now follow `[Text.Game] Messages` through the name kind
  - [ ] **Parked (user, 2026-11-27):** quit dialog "Quit the game?" / Yes / No. Two copies, both magc, both follow `MenuTextScale` today and are in no group: main menu Quit `66569A`/`665A36`/`665DBA` (session 13012), in-game options menu Quit `6549BF`/`654D5D`/`6550E3` (session 12400). Suggested: add both to `MainMenuTextScale` (no new setting); not decided
- [ ] Phase 6: release hardening (unload/reload safety, docs)
- [ ] Phase 7 (optional): sharp letters (point sampling or higher-resolution font textures)
- [ ] Hover labels over objects and items in the 3D world stay next to their object
- [ ] Multi-line panels (48–128 px, 20 px per line)

**4. 0.2.0 release**
- Focus: 3840×2160 at 16:9 (agreed 2026-11-21). Other resolutions are tested later (see "Later: resolution pass").
- [ ] Docs, full replay test pass, in-game check of every HUD layout, release zip

## 0.3: Stability and compatibility
- [ ] Document what GOG's `patch.dll` already fixes: `Config.ini` defaults, `SkipAiFix`/`SkipAiFix2`, `FrameLength`, input options
- [ ] Frame rate: the game is reported to run too fast above 60 FPS. The cap is left to DDrawCompat (a requirement), not to this mod; see "Frame rate" in the README. Document the recommended setting, and confirm whether anything in the game or `patch.dll` already limits it (about 63 FPS was measured with DDrawCompat's `FpsLimiter = off`).
  - **Note only, not planned (user, 2026-11-27):** high frame rates are not pursued unless a fix turns out possible without a decompilation, or a proper decompilation exists. Notes for that day:
  - [ ] Find what runs per frame (2026-11-27 note: the exe reads two clocks, `timeGetTime` at 57 call sites and `GetTickCount` at 82, so timing is partly clock-based; the parts that speed up are presumably stepped once per frame). If the cause is a few fixed per-frame steps, scaling them by the real frame time could be done from the proxy; otherwise keep the 60 FPS cap (DDrawCompat `FpsLimiter`, or a built-in cap in the proxy, which already sees every frame)
  - [ ] Frame interpolation (rendering between simulation steps): out of scope for a proxy. The game hands the renderer pre-transformed screen vertices (`rhw` set), so object positions and cameras are not visible here; it would need the simulation and render split inside the game, i.e. a decompilation or large engine patches. Not planned
- [ ] Culling check at 21:9 and 32:9
- [ ] Alt-tab and borderless window behaviour on top of DDrawCompat

## 0.4: Polish
- [ ] Optional FOV setting
- [ ] In-game settings menu (user, 2026-11-28): opened with a function key, changes Soulbringer Modern settings on the fly. Probably no separate program: the DLL can draw an overlay or open its own window. Text sizes apply when the game loads a font, and it reloads fonts per screen, so most should apply on the next screen; the `[Text]` settings are already one table (`ts_key`) the menu can list. Settings launcher outside the game as the fallback
- [ ] (Optional, large) HD texture option that upscales on the player's own PC

## 0.5: Audio, EAX via DSOAL (last)
Findings so far:
- The game ships EAX 2.0 reverb data per region: `data\eax.dat`, `eax0.dat`–`eax6.dat`, `custeax.dat`.
- `SoulbringeVC.exe` (the launcher's `EAX=1` build) imports Creative's `EAX.DLL`, which isn't shipped, and it bypasses GOG's `patch.dll`. Not usable as is.
- `SoulbringeVCnoeax.exe` still contains EAX 2.0 code through the standard DirectSound `IKsPropertySet` route (listener property set at `0x36AA4A4`, buffer property set at `0x36AA4B4`, setters around `0x698030`). Modern Windows DirectSound offers no EAX, so it's presumably inactive.

Plan:
- [ ] Test [DSOAL](https://github.com/kcat/dsoal) (`dsound.dll` + OpenAL Soft as `dsoal-aldrv.dll`) in the game folder with `DSOAL_LOGLEVEL=3`. Confirm the game sets EAX properties.
- [ ] If EAX isn't switched on automatically, find the option gate (`opteax`, around `0x655717`) and patch it from the proxy
- [ ] Bundle DSOAL and OpenAL Soft in the installer (LGPL: include license texts and a link to their source), with an ini setting to turn them off

## 1.0
- [ ] Full playthrough test on GOG and Steam installs
- [ ] Public release

## Later (not scheduled)
Ideas kept for after 1.0. Not being worked on.

**Resolution pass (deferred; to test after 4K 16:9 is done)**: only 3840×2160 has been play-tested. The layout, HUD clicks and tooltips are computed from the screen size and checked offline at 16:9, 16:10 and 21:9, but not yet in game at other resolutions.
- [ ] One session each at 1920×1080, 2560×1440 and a 16:10 mode (e.g. 1920×1200), with the checklist in `tests/README.md`; their captures become test data (all recorded captures are 4K today)
- [ ] Confirm the fixed-pixel tooltip rules hold: 46 px box, cursor offsets (−17,−38) / (−17,+2) / (+11,+30) / (+11,−10), item tooltips centred 88 px below the cursor (the least certain), tooltip text line 14–32 px into the box
- [ ] HUD clicks and tooltips per `HudLayout`; menus pillarboxed
- [ ] A sensible `TextScale` default per resolution (200% suits 4K; likely too big at 1080p): input for `TextScale=auto` (text plan section 5)
- [ ] Wider than 16:10 (21:9, 32:9): not a focus. Covered by the offline 21:9 checks and by `MaxAspect`; collect any reports

**HUD skins (texture replacement)**: users replace the HUD artwork with their own graphics; the layout (positions of bars, buttons, slots) stays the game's.
- Every texture passes through the renderer when loaded (`BeginTextureCreation`, `CreateTexture`, `OverWriteTexture`, `EndTextureCreation`), so the proxy can swap images there without touching the game's data files (art is stored in the game's own formats, e.g. `data\*.b16`).
- Planned shape: a dump setting writes every UI texture to a folder with a stable ID (pixel hash); artists edit them, possibly at higher resolution, into `SoulbringerModern\skins\<name>\`; `HudSkin=<name>` in the ini loads them.
- To solve: converting the game's 16-bit texture formats, PNG with alpha without a C runtime (Windows' built-in image decoder, WIC), and whether the renderer accepts textures larger than the originals.
- Shares its mechanism with the HD texture option (0.4).
- Out of scope: a fully custom HUD layout (new shapes, moved buttons). That would need per-element drawing and click remapping, essentially a HUD editor.
