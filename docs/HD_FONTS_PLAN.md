# Plan: legible text (HD font pages)

> **Settings renamed 2026-11-28.** This plan uses the setting names of the development builds (`[UI] …TextScale`, `MenuFont`, `[Debug] TextHook`, `FontPages`, `FontRedraw`…). The ini now has `[Text]` / `[Text.Menus]` / `[Text.Screens]` / `[Text.Game]` and `[Fonts]`, the text hook is always on, and the font-texture switches are automatic; DEVLOG 2026-11-28 maps every old key to its new place.

Written 2026-11-22. Status: research done (static analysis + one probe
session); design below; steps 1, 1b and 2 done 2026-11-22 (font surfaces are A1R5G5B5
today, the renderer offers A8R8G8B8; page generator and previews built);
face chosen: **Cardo Bold** for both fonts (alternatives kept in section 3,
"Face options"); step 3 (512² A8R8G8B8 font slots behind
`[Debug] FontPages=1`) done and checked in game; step 4 (redraw
`phil`/`hbut` in Cardo Bold with GDI) done 2026-11-23; step 5 (tuning) started:
own metrics for phil/hbut built and checked in game 2026-11-23 ("looks better", alt-tab confirmed); interface font `16xY`/`16xB`/`16xX` redrawn in layers from Montserrat ExtraBold (2026-11-26, `[UI] InterfaceFont`); menu font `magc` redrawn into its own cells from Eagle Lake with lining figures ("Soulbringer Uncial") or MedievalSharp (2026-11-26, `[UI] MenuFont`, both to check in game); name/message fonts `mgrn`/`mred` redrawn into their own cells, the tooltip font `hbut` and the journal font `phil` from the same face (Cardo only by choice) (2026-11-26: all from **Soulbringer Text**, `SoulbringerText-Regular.ttf`, an upscaled outline version of the game's font, OFL; `[UI] NameFont` / `TooltipFont` / `JournalFont` = `Text`; `Custom` for a private name face); next for this plan: together with the per-place sizes (TEXT_SCALING_PLAN phase 5b, step 5). This is a living plan: section 5 is updated after
2026-11-27: combat numbers `numr`/`numg` (Montserrat ExtraBold, `[UI] NumberFont`) and credits `cred` (Soulbringer Credits, the user's upscale, `[UI] CreditsFont`) redrawn into their original rectangles; with that every game font except the low-resolution `5px*` set is redrawn. every step. This
replaces phase 7 of `TEXT_SCALING_PLAN.md` and builds on its phases 1–3
(the text hook).

## 1. Problem and decisions

- The original fonts are hard to read **even at 1024×768**, the resolution
  they were drawn for: they are tiny bitmaps (journal text about 9 px tall),
  thin strokes, uneven shapes. Outside settings (DDrawCompat integer
  scaling, font anti-aliasing, MSAA) cannot add detail that is not there.
- Enlarging them (the text hook, `[Debug] TextHook=1`) fixes size and
  layout but not legibility: at 200% they look soft and smeared.
- Mockup (DEVLOG 2026-11-22): only letters **redrawn from a TrueType font**
  are clearly more legible.

Decided with the user (font chart in the DEVLOG):

| Font | Used for | Treatment |
|---|---|---|
| `phil` | journal | **redrawn** in an old-style serif |
| `hbut` | tooltips, place names, HUD bar labels | **redrawn** in an old-style serif |
| `magc` | menu buttons, conversations, save list, quit dialog | **redrawn** into its own cells from Eagle Lake with lining figures ("Soulbringer Uncial", default) or MedievalSharp (2026-11-26, `[UI] MenuFont`, needs TextHook; DEVLOG "menu font") |
| `16xY`, `16xB`, `16xX` | the main interface font (checked 2026-11-23): options menu, character screen labels and numbers, inventory numbers, HUD column move labels, combo screen | **redrawn** in layers from Montserrat ExtraBold into the original rectangles (2026-11-26, `[UI] InterfaceFont`; DEVLOG "interface font redrawn") |
| `mgrn`, `mred` | names over characters, messages above the HUD | later (redraw planned) |
| `numr`, `numg` | combat damage numbers (red), a miss "0" (green); own objects loaded by `67FE78` / `67FEF5`; 13 glyphs each (`+ - 0-9 =`), 7x8 texels with a one-texel outline | **redrawn** from Montserrat ExtraBold into the original rectangles (`[UI] NumberFont`, 2026-11-27): fill/outline = the two colours of the digits (`fpg_num_colours`), '8' 6 texels tall (`fpg_num_em`), baseline on the last fill row 12 (`fpg_num_base`), outline 1 texel (k px, `fpg_glyph`), '-' drawn as the en dash (`fpg_num_code`) |
| `cred` | credits, "GAME OVER" (load `67FFEF`); 191 glyphs on 4E (187) and 4F (4), white FCFCFC with grey anti-aliasing, no outline, line 35, 'H' 19 texels | **redrawn** from Soulbringer Credits (the user's upscale, OFL, `dist/fonts`) into the original rectangles (`[UI] CreditsFont`, 2026-11-27): 'H' 19 texels (`fpg_ui_em`, inset 0), fill = the brighter of the two commonest colours (`fpg_num_colours`), no outline; 35 codes the face lacks (@ [ \\ ] { \| } ¦ § © ª « ¬ soft hyphen ® ± ² ³ µ ¶ ¹ º » ¼ ½ ¾ Ð × Ý Þ ð ÷ ý þ) and '~' (ink outside its rectangle) stay the game's |
| others (`16xX`, `5px*`) | never seen drawing at 4K (`5px*` = low-resolution set, below 1000 px wide); credits | unchanged |

## 2. What is known (verified)

How text reaches the screen (all in `SoulbringeVCnoeax.exe`, identical in
`SoulbringeVC.exe`; details in the DEVLOG entries of 2026-11-21/22):

1. **Font pages.** All fonts share four 256×256 texture pages, renderer
   slots **0x4E–0x51** (= the font materials). Each glyph is a rectangle on
   one page: `.fnt` record `+4` page, `+8/+0xC` corner, `+0x10/+0x14` size.
   Checked from the files: 2,700 distinct rectangles, **no rectangle used by
   two fonts or two codes, and no two rectangles touch** (at least one empty
   texel between glyphs). So each glyph of `phil` and `hbut` can be redrawn
   in place without touching any other font.
2. **Page creation.** At startup the game calls `CreateTexture(slot, desc)`
   for ~113 slots (`0x678A5C`/`0x678ACC`); for 0x4E–0x51 the descriptor is
   (probe, session 9640): `+4` width 256, `+8` height 256, `+0xC` 1,
   `+0x10` 4, `+0x14` 0x40, `+0x2C` 2, `+0x30` 0x10, `+0x34` palette
   pointer `0x1FFDBCC`.
3. **Page upload.** `OverWriteTexture(slot, 0, pixels)` (cdecl, 3 args),
   once per page per session so far (and again whenever the game reloads
   textures). Source: **8-bit palette indices**, 256×256 = 65,536 bytes,
   index 0 = empty (47–91% of each page). The palette is the game's global
   256-entry RGB table at `0x1FFDBCC` (filled by `0x5C3D70` from a
   0x300-byte table, handed to the renderer through export table `+0x68`).
   Dumps: `SBM_fontpage_<slot>_256x256x1.raw`; rendered with the `.fnt`
   rectangles over them, every glyph rectangle sits on its glyph.
4. **Renderer side** (`D3d7Lib_orig.dll`, base 0x10000000): slot record
   `[[0x100786CC] + 0x1B54 + 4*slot]` = `+0` source width, `+4` source
   height, `+8` surface size, `+0xC` bytes per source pixel, `+0x20/+0x24`
   source/surface format codes (1 / 4 for font pages), `+0x34` mip levels
   (1: none), `+0x54` the DirectDraw surface. `OverWriteTexture`
   (`0x1000F380`) locks the surface (`IDirectDrawSurface7::Lock`, vtable
   `+0x64`), expands the source to surface size with nearest sampling when
   they differ (`0x1000F880`), converts the format (`0x1000F9D0`) and
   unlocks (`+0x80`).
5. **Texture coordinates are fractions.** The glyph flush (`0x42B024`)
   multiplies the integer texel coordinates by 1/256 (constants at
   `0x3678458`…`0x3678488`). A page with k× the pixels at the same layout
   is therefore addressed correctly with no change to the game.
7. **The menu font `magc`** (checked 2026-11-26 from `magc.fnt` and the
   page dumps): 191 drawn glyphs on pages 0x4E (24), 0x4F (116) and
   0x50 (51); line height 19 texels, 'H' 11×10 at y offset 4, x-height 8,
   descenders 5. No outline: yellow strokes (228,228,40) fading through
   olive (152,136,40) and browns to (36,24,8) at the edges (anti-aliasing
   in the palette indices). Font object `0x213F3D8`, a second one at
   `0x1E78684`.
6. **The text hook already scales the layout** (font metrics after load,
   texel rewrite at add glyph). With a k× page, a glyph's quad of
   `w × s` screen pixels samples `w × k` page pixels: **at k ≥ s the page
   is sharp**, nothing is magnified.

## 3. Design

```
game: CreateTexture(0x4E..0x51, desc 256x256)
  proxy: create the slot k times larger (desc width/height x k)         [hook 1]
game: OverWriteTexture(slot, 0, 256x256 indices)
  proxy: pass a k x k nearest-enlarged copy of the indices instead       [hook 2]
         (the renderer fills the big surface exactly as before)
  proxy, right after: lock the surface, redraw the glyph rectangles of   [hook 3]
         phil and hbut from the TrueType font, at k x resolution, in
         the surface's own pixel format; unlock
game: draws text as now; the text hook scales the layout by s
```

- **k** (page scale) = `TextScale`/100 rounded **up** (200% → 2, 512×512
  pages; 280% → 3). Equal to the text scale, a redrawn glyph is drawn at
  exactly the pixel size it appears on screen: one page pixel per screen
  pixel, so it is neither magnified (soft) nor minified (the pages have no
  mipmaps, so a page much larger than needed would make thin serif strokes
  shimmer or break up).
- **Hook 1/2** run inside the proxy's export stubs (the calls already pass
  through it): change `desc+4/+8` before forwarding, and swap the `pixels`
  argument for our own buffer. No game code is patched for this; the
  renderer does what it always does at a larger size.
- **Reading the renderer's slot record** (to find the surface) relies on
  the shipped `D3d7Lib_orig.dll`; before using it the proxy checks the
  bytes of `OverWriteTexture` (`0x1000F380`) and of the record lookup
  against the known build, and turns the feature off (logged) if they
  differ.
- **Hook 3** writes the redrawn glyphs straight into the locked surface
  after the renderer's upload, in the format `GetSurfaceDesc` reports
  (16-bit 4444/1555/565 or 32-bit 8888, handled generically from the bit
  masks). Only `phil`/`hbut` rectangles are touched; every other font keeps
  the renderer's (enlarged) pixels. Re-done on every upload of those slots
  (device loss, texture reloads), so it can never be lost.
- **Glyph drawing** (GDI, loaded at runtime like the screenshot code):
  `CreateFont` with the face from the ini. Default: **Cardo Bold**
  (`Cardo-Bold.ttf`), chosen by the user from the step 2 previews
  (2026-11-22) for both `phil` and `hbut`; Cardo is the old-style serif
  the user chose from the mockup. It is not part of
  Windows; it is free under the SIL Open Font License, which allows
  shipping it with the mod (its `.ttf` and licence text go in the release),
  and the proxy loads it privately for the game only
  (`AddFontResourceExA(..., FR_PRIVATE)`, nothing is installed). Fallbacks
  if the file is missing: old-style serifs that ship with Windows,
  Constantia → Palatino Linotype → Book Antiqua. Height
  chosen so the font's cap and x-heights match the original glyphs × k;
  each character drawn with anti-aliasing into a 32-bit DIB, then copied
  into its rectangle × k, aligned on the original baseline (from the
  `.fnt` y offsets), centred horizontally and condensed if wider, inset by
  one page pixel so nothing reaches the gap between glyphs.
- **Face options** (kept in case the choice changes; all previewed in
  step 2, `tools/font_preview.py --face NAME=file.ttf`):

  | Face | Licence / source | Step 2 result (k = 2, inside the original rectangles) | Status |
  |---|---|---|---|
  | **Cardo Bold** | SIL OFL, ship `Cardo-Bold.ttf` + `OFL.txt` | phil 80%, hbut 83% of the original cap height | **chosen (2026-11-22)** |
  | Cardo Regular | SIL OFL (same family) | phil 85%, hbut 83%; lighter strokes, reads thinner on parchment | option |
  | Crimson Text SemiBold | SIL OFL, ship `CrimsonText-SemiBold.ttf` + `OFL.txt` | phil 85%, hbut 94%; a little larger, similar old-style look | option |
  | Constantia, Palatino Linotype, Book Antiqua | ship with Windows (not bundled) | not previewed (not available offline) | fallbacks if the bundled file is missing |

  `FontFace` can name a different face per font later (e.g. Regular for
  the journal, Bold for tooltips) if tuning (step 5) calls for it.
- **Colour**: each redrawn font keeps the original look. Fill and edge
  colours are measured from the original glyph pixels on the surface
  (first upload, before redrawing), coverage from the TrueType rendering
  goes to alpha.
- **Settings** (`[UI]`): `FontFace=Cardo Bold` (any installed font name, or a
  `.ttf` in the mod folder), `FontRedraw=phil,hbut`
  (font list; empty = off), `FontPageScale=auto` (= `TextScale`/100
  rounded up; a number to force it). Requires `TextHook=1` for
  the larger layout; without it the redrawn glyphs are simply sharp at the
  original size.
- **`magc`** (2026-11-26, `[UI] MenuFont`): redrawn into its own cells like
  phil/hbut (own metrics generalised to three fonts, each with its own face),
  yellow fill with a soft dark edge, colours from its original letters.
  Faces: Eagle Lake (closest to the original; bundled as "Soulbringer
  Uncial" with its old-style digits changed to lining figures by
  `tools/make_menu_font.py`, renamed because "Eagle Lake" is a Reserved
  Font Name), MedievalSharp, or a private face (`MenuFont=Custom`). An
  upscale of the original pixels (EPX, smoothing) was tried offline and
  rejected: the letters stay blocky.

## 4. Open points, and how each is settled before building on it

| # | Question | How it is checked | If it fails |
|---|---|---|---|
| 1 | **Answered (step 3): 512×512 A8R8G8B8 granted as asked (k = 2); 1024² untested.** Does the renderer create a 1024×1024 surface when `desc` asks for it? (format 4, flag 0x40, device limits) | Step 1 probe: log the slot record `+8` and `GetSurfaceDesc` after creation with k = 4 | Fall back to k = 2, or create our own texture for those slots |
| 2 | The surface pixel format and how index 0 becomes transparent (alpha or colour key). **Answered (step 1): A1R5G5B5, 1-bit alpha. Resolved (step 1b): ask for A8R8G8B8 (code 0x80) in `desc+0x10`; A4R4G4B4 (0x2) as fallback** | Step 1: dump `GetSurfaceDesc` and the locked surface of slot 0x4E after upload (read only) | Match whatever is there: alpha → write coverage to alpha (4-bit alpha in a 4444 surface still gives 16 edge levels); colour key → ask the renderer for an alpha format in `desc+0x10` (format codes of other slots, logged in step 1), else threshold coverage |
| 3 | Is the upload ever partial or repeated with different content? | Log every `OverWriteTexture` of 0x4E–0x51 with a checksum of the source | Redraw after each upload (already the design) |
| 4 | Does anything else draw from the font pages, or read them back? | Static: the glyph flush is the only code that draws with the font materials; the renderer's `ViewTexture` export is unexplored: step 1 logs any call to it | A k× page is addressed in fractions anyway; only a readback would need care |
| 5 | Is a TrueType glyph's width compatible with the original spacing? | Offline: render both fonts' alphabets into their rectangles and compare a journal line (mockup) | Condense to fit (v1); later, also replace the widths in the font metrics (the text hook already rewrites metrics, and the texel rewrite maps any quad width to the rectangle) |
| 6 | Is the bundled Cardo found and loaded; which fallbacks exist on the user's PC | Log the result of loading the `.ttf` and the face GDI actually picked | Fallback list (Constantia, Palatino Linotype, Book Antiqua); logged |
| 7 | Memory/performance | k = 2: 4 pages × 1 MB at 32 bits (4 MB at k = 4); ~400 glyphs redrawn once per upload | — |
| 8 | Does the renderer restore lost surfaces itself (alt-tab, mode change) without a new `OverWriteTexture`? | Step 4 checklist: alt-tab out and back, then look at the journal | Re-apply the redraw when the proxy sees the surface was restored (`IsLost`, vtable `+0x60`, checked once per frame for the four slots) |

## 5. Steps (living plan: updated as probes and tests change it)

Who: **C** = Claude (reverse engineering, code, offline tests, docs);
**U** = user (game sessions, screenshots, look-and-feel decisions).
Each step ends with its exit check; a failed check sends us back to the
fallback in section 4, and this table is updated.

| # | Step | C | U (input needed) | Exit check |
|---|---|---|---|---|
| 1 | **Surface probe** (read only, `Log=1`) | Log each font page's surface size and pixel format after creation and upload, dump the converted surface once (`SBM_fontsurf_<slot>.raw`), checksum every upload, log the format codes of other slots and any `ViewTexture` call; render the dumps offline | **Short session**: start, main menu, load a save, open the journal, **alt-tab out and back**, open the journal again, quit. No screenshots needed | Pages render offline with their real colours and transparency; open points 2, 3, 4 answered. **Done (2026-11-22, session pid 16880):** pages render offline with their real colours (100% of texels explained) and transparency. Answers: **2** A1R5G5B5, index 0 → alpha 0, everything else alpha 1: **only 1-bit alpha**, no colour key; all 113 slots use that format; **3** one upload per page per session, never repeated; **4** no `ViewTexture` calls; **8** surfaces are in system memory (never lost); no `LOST` lines, no `OsTaskSwitch` calls. New: step **1b** below |
| 1b | **Texture-format list probe** (read only, `Log=1`) | Log the renderer's texture-format list (code → DirectDraw pixel format) so the font slots can ask for a format with real alpha (4444 or 8888) via `desc+0x10` in step 3 | **Short session**: start, main menu, quit | We know the code of an alpha format, or that none is listed. **Done (2026-11-22, session pid 12556):** the renderer offers **A8R8G8B8 (code 0x80)** and **A4R4G4B4 (code 0x2)**, both with encoders (table in RENDERER_API). Decision: step 3 creates the four font slots with `desc+0x10` = 0x80 (256 alpha levels; 4 × 1 MB at k = 2), fallback 0x2. New risk: the font material may be drawn with alpha test instead of blending (step 3 checks) |
| 2 | **Page generator, offline** (`src/font_pages.inc`) | Build the k× page from the dumps: other fonts enlarged as now, `phil`/`hbut` rectangles redrawn; harness tests (every glyph inside its rectangle, baseline, nothing else changed); **preview images** of real journal and tooltip lines at your `TextScale`: Cardo (the chosen old-style serif), plus Windows old-style serifs for comparison (Constantia, Palatino Linotype) | **Pick a face and weight** from the previews (or ask for others); say if size or colour looks off | Previews approved. **Status (2026-11-22): built, previews sent.** `src/font_pages.inc` (plain C, shared by the DLL and `tests/fonts/test_font_pages.c`), `tools/font_preview.py` (FreeType glyphs, C composition, previews). Build checks pass for Cardo Regular, Cardo Bold, Crimson Text SemiBold (k = 2). **Finding:** the game's glyph rectangles are tight around the original letters; Cardo's ascenders, old-style digits and descenders are taller relative to its capitals, so inside the original rectangles the face fits at about **80–85% of the original cap height** (J/Q tails clipped slightly, a few accents keep the original glyph). Full size needs taller rectangles: new metrics for phil/hbut (y offset, height, position on the page) written by the text hook, a step after 4. Letters sit in the original widths, so spacing is looser than the face's own (open point 5). **Picked (user, 22:32): Cardo Bold for both**; the other faces stay as options (section 3) |
| 3 | **Larger pages, no redraw** (`FontPageScale`, hooks 1–2) | Create slots 0x4E–0x51 k× larger and feed enlarged indices; check the renderer granted the size; renderer-build check | **Session + F11** on the main menu, a tooltip and the journal: text must look **exactly as today** | Screenshots identical to today; log shows the larger surfaces (open point 1). **Status (2026-11-22): built and deployed** (`src/font_hd.inc`, `[Debug] FontPages=1`, k = 2): CreateTexture of 0x4E–0x51 gets a copy of the descriptor with 512×512 and surface code 0x80 (only if the descriptor is the known 256², code 1 → 4, no size flags); OverWriteTexture gets a 2× nearest copy of the indices, only if the slot record really says 512² 1-byte source (else the upload passes through, so the renderer never reads past the game's buffer). Renderer-build check: 11 bytes each of `0x1000E930` and `0x1000F380`. Expected look: same letters and places; with `TextHook=1` sampling changes from "bilinear at 2×" to "a 2× nearest page, ~1:1", so letters may look a bit crisper. **Done (session pid 12740, 22:39, 8 × F11):** all four slots created **512×512 A8R8G8B8** (pitch 2048, system memory), one enlarged upload each, no pass-through, no crash; `font_surface.py` explains 100% of the texels of all four 512² surfaces from the 256² indices (index 0 → alpha 0, others → alpha 255, exact colours). Screenshots: main menu (`magc`), journal (`phil`), world and inventory tooltips (`hbut`), object names (`mgrn`) all correct, crisp, no fringes or boxes. Open point 1 answered |
| 4 | **Redraw in game** (`FontRedraw=phil,hbut`, hook 3, GDI) | Draw with the chosen Windows font into the surface on every upload; restore-after-alt-tab handling if step 1 showed it is needed | **Session + F11**: journal, HUD tooltips, inventory tooltips, place name, a message; alt-tab and back; report anything garbled, cut off or misplaced | Letters correct and sharp everywhere `phil`/`hbut` appear. **Status (2026-11-22): built and deployed** (`[Debug] FontRedraw=phil,hbut`, `FontFace=Cardo`, `FontWeight=700`, `FontFile=SoulbringerModern\fonts\Cardo-Bold.ttf`): GDI `GetGlyphOutlineA(GGO_GRAY8_BITMAP)` per glyph, size by `fpg_fit` (same rule as the previews), composed by `fpg_glyph`. **First session (22:53): old letters on screen** although the surface held Cardo: the renderer draws from a video-memory cache refreshed inside `OverWriteTexture` (RENDERER_API). Second try (23:02): composed page as an A8R8G8B8 upload source: Cardo visible, but the renderer reads such a source as bytes A,R,G,B (colours wrong in all text) with 1-bit alpha. **Third version:** step 3 upload unchanged; afterwards the composed page is written into the surface and the video-memory copy refreshed with `IDirect3DDevice7::Load`, as the renderer does itself. **Done (2026-11-23, sessions pid 21168 and 6532, user: "working as intended"):** every page redrawn (135/129/115 glyphs, 3 kept) and its cached copy refreshed (`Load` S_OK, cache 78/79/80); journal in Cardo Bold brown ink with smooth edges, world tooltip "Knife" cream with outline; all other fonts (HUD labels, `mgrn` messages, menus) in their original colours |
| 5 | **Tuning** | Adjust size match, baseline, weight, colour/outline, letter spacing (condense vs. new widths, open point 5) | **Decide** what still bothers you; **`magc`**: keep as is or try a smooth upscale (preview first) | You are happy with journal and tooltips. **Started 2026-11-23: own metrics** (user: text still too small): phil/hbut get new cells sized for the face, in the original line box, packed into free page space; the text hook rewrites their records (u, v, width, height, y offset, page), sets letter spacing and kerning to 0. Cardo Bold: phil at 95–97% and hbut at 100% of the original cap height (was 78–83%), natural widths (lines ~5–10% longer than the original). `[Debug] FontOwnMetrics=1` (default; needs TextHook, FontPages, FontRedraw=phil,hbut). **Checked in game 2026-11-23** (user: "looks better"); sizes now per kind of text (TEXT_SCALING_PLAN phase 5). **Done 2026-11-26:** interface font (`InterfaceFont`, in layers, original rectangles) and menu font (`MenuFont`, own cells, Eagle Lake with lining figures). **Next:** check both in game, then `mgrn`/`mred`, each with own metrics, together with the per-place sizes (TEXT_SCALING_PLAN phase 5b; the size mechanism is done since 2026-11-26, and the page scale k now also covers the largest place size) |
| 6 | **Release** | Defaults, docs (README settings, CHANGELOG, tests checklist), zip | **Final check** of the release zip on a fresh copy of the settings | Release built |

Later, separate decisions: names and messages (`mgrn`/`mred`), and the
other resolutions (`16xY`/`16xB` and `magc` are redrawn since 2026-11-26). Planned together with per-place sizes (TEXT_SCALING_PLAN
phase 5b), since both aim at legibility where the shared fonts are used.

## 6. Risks

| Risk | Mitigation |
|---|---|
| Renderer refuses or shrinks a 1024² surface | step 3 checks the record and surface size; fall back to 512² |
| Font material drawn with alpha **test** rather than alpha blending (soft edges would be cut at a threshold) | step 3 logs the render states around the glyph flush; if needed set blending for the text bucket only |
| Wrong pixel format or alpha handling corrupts the pages | step 1 dumps the real format before anything writes; writes only inside the two fonts' rectangles |
| Serif glyphs too wide for the original spacing | condense to the rectangle (v1); width replacement later |
| A missing font face | fallback list, logged |
| Device loss / texture reload drops the redraw | redraw on every upload of the four slots |
| Text decode (debug log) breaks | it reads texel coordinates, which do not change |

## 7. Corrections to earlier notes

- The DEVLOG entry "First `TextHook=1` session" guessed that bilinear
  filtering blended in **neighbouring glyphs**. The probe shows glyph
  rectangles never touch (at least one empty texel apart), so at 2× the
  filter only blends in the empty gap: the soft, fringed look is blur plus
  the transparent edge, not other letters.

### Outline room (2026-11-27)

`fpg_glyph` condensed every letter whose ink plus 2 x outline was wider than its cell. Soulbringer Uncial fills its advance, so at k = 4 (outline 2 px) each magc letter lost 4 px of 4-6 units: thinner letters with gaps, "r e f r e s h" in conversations (session 13012). `FpgStyle.edgeClip` (set for magc only): the letter keeps its width and the outline is cut at the cell sides (it is nearly black on dark backgrounds). hbut/mgrn/mred keep the old rule (approved as they are); the same flag could be tried there. Selftest: "edgeClip" case.
