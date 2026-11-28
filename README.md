# Soulbringer Modern

Fixes that make **Soulbringer** (Gremlin Interactive / Infogrames, 2000) play properly on modern Windows PCs and widescreen monitors. It's a mod, not a source port: it adds a small drop-in DLL to your own copy of the game and leaves the game's files untouched.

> **Status: 0.2.0 in development.** Latest release: 0.1.1 (widescreen world). In the development build, menus are pillarboxed, the HUD has proper proportions with selectable layouts and reacts where it is drawn, and all text is enlarged and redrawn sharp, with sizes by category in a simplified settings file. Development targets 4K (3840×2160, 16:9). See the [roadmap](docs/ROADMAP.md) and [changelog](CHANGELOG.md).

## Features

| | Feature | Status |
|---|---|---|
| ✅ | **Native widescreen (Hor+)**: the world is drawn with correct proportions at any aspect ratio (16:9, 16:10, 21:9…). You see more on the sides instead of a stretched image. | 0.1.0 |
| ✅ | Mouse picking stays accurate in widescreen. | 0.1.0 |
| ✅ | Aspect-ratio cap (`MaxAspect`): on screens wider than the cap, the game is pillarboxed to the cap with the HUD at its edges. A safety net for ultrawide screens (not yet tested on a real ultrawide monitor). | 0.2.0-dev |
| ✅ | Optional settings file, plus a developer log for reverse-engineering: automatic capture of every new screen, hover/tooltip and click events (including in-game DirectInput clicks), the game's text decoded back into words, F11 screenshots paired with a frame capture. | 0.1.0 – 0.2.0-dev |
| ✅ | Full-screen menus (main menu, options, inventory, character…) at their native 4:3 shape, centered with black bars. | 0.2.0-dev |
| ✅ | HUD layouts (`Hud`): **split** (bottom bar bottom-center, action column at the right edge), **right** (whole HUD in the bottom-right corner), **center**, or the original **stretched**. All correctly proportioned. | 0.2.0-dev |
| ✅ | Larger mouse cursor (`CursorSize`). | 0.2.0-dev |
| ✅ | Mouse hover and clicks on the moved HUD mapped correctly: the game is told the HUD button under the mouse at its original position, the cursor is drawn where the mouse is. Confirmed in game with `split` and `right`. | 0.2.0-dev |
| ✅ | Readable text everywhere: the game lays out all its text at a larger size itself (lines wrap, boxes grow), with one size for all text and optional sizes by category (menus, screens, in-game) and by screen or area (main menu, options, conversations, HUD labels, messages…). | 0.2.0-dev |
| ✅ | Every game font redrawn sharp in the game's own colours (`[Fonts] All=Sharp`): names, tooltips and journal from Soulbringer Text, menus and conversations from Soulbringer Uncial, interface text and combat numbers from Montserrat ExtraBold, credits from Soulbringer Credits. Per font: `Original`, an alternative (Cardo, MedievalSharp), or a font file of your own for names and menus. | 0.2.0-dev |
| 🔜 | Conversation screen layout. | planned (0.2) |
| 🔜 | EAX 2.0 environmental audio through [DSOAL](https://github.com/kcat/dsoal) (the game ships EAX data, but it can't be used on modern Windows). | planned, last |

Cutscenes keep their original 4:3 pillarboxed presentation.

## Requirements

- Soulbringer from **GOG** or **Steam**. Both stores ship the same build, which includes GOG's 2012 compatibility patch (`patch.dll`) and the modern resolution list.
- The game must already start and run on your PC. Most systems need **[DDrawCompat](https://github.com/narzoul/DDrawCompat)** (`ddraw.dll` in the game folder). Soulbringer Modern runs alongside it.
- Pick your monitor's native resolution in the game launcher.

Supported game files (checked automatically; the patch is skipped if the code doesn't match):

| File | SHA-256 |
|---|---|
| `SoulbringeVCnoeax.exe` | `4816ee96b7077b5f4ad2f1bb43cc60a95d073bbf02aef7be09f241ae0cc5c304` |
| `SoulbringeVC.exe` | `57c2d217d51ae21d4f267a96b6d0d872a21081024a5fe8332383d4dd264c75e9` |
| `D3d7Lib.dll` (original renderer) | `a76cdfb79a4a238d117ab599f8b6834e187b19617986192ee660de2b4a87152b` |

### Frame rate

The game is reported to run too fast above 60 FPS. Soulbringer Modern doesn't limit the frame rate; use DDrawCompat's limiter instead. In `DDrawCompat.ini` set:

```ini
FpsLimiter = flipend(60)
```

Check the DDrawCompat documentation for the exact syntax for your version.

## Install

1. Download the latest `SoulbringerModern-x.y.z.zip` from **Releases**.
2. Extract it into your Soulbringer folder, the one that contains `SoulbringeVC.exe`. For Steam that's usually `...\steamapps\common\Soulbringer`.
3. Run **`install.bat`**.
4. Start the game as usual.

The installer renames the game's renderer `D3d7Lib.dll` to `D3d7Lib_orig.dll` and puts the Soulbringer Modern `D3d7Lib.dll` in its place. It does this both in the game folder and in `data\Stealth\Render`. Nothing else is changed.

## Known issues

- **0.1.1 (release):** the UI, HUD and menus are still stretched horizontally.
- **0.2.0-dev:** the moved HUD reacts where it is drawn. On the world beside the centered bar or left of the column, clicks act slightly above the bar line or left of the column line (the game still has its HUD there; a possible fix is in the roadmap). If the cursor misbehaves over the HUD, `[Debug] HudMouse=0` turns the mapping off.
- **0.2.0-dev, text sizes:** some text has no setting of its own yet and follows its category's `All` (e.g. the quit dialog follows `[Text.Menus] All`, interface text not identified yet follows `[Text.Screens] All`). Fixed panels such as the options menu can cut off text that is too large. The character stats, Encumbrance and stack counts share one size (`[Text.Screens] Character`).
- `MaxAspect` hasn't been tested on a real ultrawide monitor yet.
- **0.2.0-dev, resolutions:** development targets 3840×2160 (16:9) and only that has been play-tested. The layout, HUD clicks and tooltips are computed from the screen size and checked offline at 16:9, 16:10 and 21:9; an in-game pass at 1080p, 1440p and 16:10 is planned for later. Aspect ratios wider than 16:10 (21:9, 32:9) are not a focus: they should work, or use `MaxAspect` to cap them.
- **Upgrade from 0.1.0:** 0.1.0 could crash on some PCs because the game loads its renderer twice. Install 0.1.1 or later. Just run the new `install.bat` over the old version.

## Uninstall

Run **`uninstall.bat`** in the game folder. It restores the original `D3d7Lib.dll` files.

## Settings

Edit `SoulbringerModern.ini` in the game folder. Every setting is explained in the file itself; this is its layout:

```ini
[Display]
Widescreen=1     ; 0 = original stretched image
MaxAspect=0      ; cap, e.g. 16:9 or 21:9: wider screens get black side bars

[Interface]
Layout=1         ; 0 = original stretched menus and HUD
Hud=split        ; split | right | center | stretched
CursorSize=200   ; cursor size in percent

[Text]           ; text sizes in percent (50-600); 0 = follow the level above
Size=200         ; all text
Tooltips=0

[Text.Menus]     ; All, MainMenu, SaveList, Options
[Text.Screens]   ; All, Character, Labels, Journal, Map, Credits
[Text.Game]      ; All, Conversations, HudLabels, Names, Messages, PlaceNames, CombatNumbers

[Fonts]
All=Sharp        ; every game font redrawn sharp | Original
Names=           ; per font, empty = follow All: Names, Tooltips, Journal, Menu, Interface, Numbers, Credits
                 ; (a name such as Cardo or MedievalSharp, Original, or for Names / Menu a .ttf file of your own)

[Debug]
Log=0            ; 1 = write SoulbringerModern_<exe>_<pid>.log (developers only)
AutoCapture=1    ; with Log=1: capture every new screen automatically (F9 forces one)
ScreenshotKey=F11 ; with Log=1: screenshot (Screenshots\ in the game folder) plus a frame capture; 0 = off
Stock=0          ; 1 = every enhancement off (the game as shipped), for reference screenshots
TextHook=1       ; troubleshooting: 0 = the game's own text layout (only cursor tooltips are stretched)
HudMouse=1       ; troubleshooting: 0 = HUD clicks as if the HUD were still stretched
ConversationPortraitHold=1 ; troubleshooting: 0 = the game's own portrait drawing (flickers at each line)
FontPlaces=0     ; developers: 1 = "place:" lines in the log name the code behind every new text
```

An item follows its category's `All`, which follows `[Text] Size`. Text the game draws in a place that has no item yet follows its category.

**Upgrading from an earlier 0.2.0 development build:** the settings file changed on 2026-11-28 (`[UI]` and the `…TextScale` / `…Font` keys are no longer read). Replace your `SoulbringerModern.ini` with the shipped one and set your sizes again; `docs/DEVLOG.md` (2026-11-28) lists where each old setting went.

## How it works

Soulbringer doesn't talk to Direct3D directly. It loads a renderer plug-in, `D3d7Lib.dll`, and calls 76 named functions in it (see [docs/RENDERER_API.md](docs/RENDERER_API.md)). Soulbringer Modern is a proxy for that plug-in:

- **Pass-through.** Every renderer call is forwarded unchanged to the original DLL. Rendering itself is still done by the game's own renderer, and DDrawCompat underneath it.
- **In-memory patches.** When the game starts, the proxy patches the running game in memory, after checking that the original bytes are exactly what it expects. Nothing on disk is modified.
- **Widescreen.** The game computes its projection as *vertical scale = 0.8 × 4H/3W* but *horizontal scale = 0.8* (a constant), which stretches the picture horizontally on any non-4:3 screen. The patch gives the horizontal scale the same 4H/3W factor. The patch is removed again if the game unloads the renderer, so the game never calls into an unloaded DLL. The full analysis, with addresses, is in [docs/DEVLOG.md](docs/DEVLOG.md).
- **UI.** The game designs all 2D on a 640×480 canvas and stretches it to the screen (×6 horizontally but ×4.5 vertically at 4K). Drawing is deferred: vertices wait in the renderer's buffer until `RenderAllBuffers`/`EndFrame`, so the proxy can re-place them first. It tells UI from world by depth (`rhw`), recognises screens from what is drawn (a menu is the 6-tile full-screen background), and moves elements: menus centered at 4:3, HUD tiles anchored to their edges as rigid pieces, the cursor and tooltips left with the real mouse.
- **Debug log.** With `Log=1` the proxy also records what the game draws and does (screens, hover boxes, text, clicks) so the layout can be tested offline by replaying real sessions (`tests/`).

## Building from source

You need **LLVM** (clang, lld-link, llvm-dlltool) and **Python 3**. No Visual Studio or Windows SDK is required.

```sh
./build.sh        # Linux / macOS / Git Bash
build.cmd         # Windows command prompt
./make_release.sh # build + package dist/ into release/SoulbringerModern-<version>.zip
```

Output: `build/D3d7Lib.dll` (32-bit).

Run the regression tests with `python3 tests/replay/replay.py` (needs a C compiler; Pillow optional for pictures). See [tests/README.md](tests/README.md).

## Repository layout

```
src/soulbringer_modern.c   proxy DLL: forwarding, patches, debug logging
src/gen_stubs.py           generates the 76 export stubs + .def file
src/exports.txt            export names of the original D3d7Lib.dll (ordinal order)
src/imports/*.def          import definitions (no Windows SDK needed)
src/ui_layout.inc          UI layout rules (menus pillarboxed, HUD anchoring, aspect cap bars)
src/hover_log.inc          debug log: cursor, tooltips/labels, text, world under the cursor
src/dinput_mouse.inc       in-game DirectInput mouse: HUD mouse mapping, clicks for the log
src/screenshot.inc         debug: F11 screenshot paired with a frame capture
src/font_places.inc        per-place fonts: copies of a shared font per place ([Debug] FontPlaces)
src/text_decode.inc        debug log: drawn text decoded back into words (from the game's fonts)
src/font_probe.inc         debug log: font texture pages and surfaces (read only, HD fonts research)
src/font_pages.inc         HD fonts: larger font pages with redrawn glyphs (plain C, shared with tests/fonts/)
src/font_hd.inc            HD fonts in the game: larger A8R8G8B8 font textures, GDI redraw ([Fonts])
dist/fonts/                Cardo-Bold.ttf, Montserrat-ExtraBold.ttf, EagleLake-Regular.ttf (source of SoulbringerUncial-Regular.ttf), MedievalSharp-Regular.ttf, SoulbringerText-Regular.ttf + licences (SIL OFL), shipped for the font redraws
tools/ui_font_preview.py   interface font redraw: offline pages and previews (runs the DLL's drawing code)
dist/                      files shipped in the release zip (installer, ini)
tests/                     replay regression test + recorded frame captures
tools/sbm_log.py           reads a debug log: events, before/after pictures
tools/font_surface.py      renders and checks the font surface dumps
tools/font_preview.py      HD fonts: builds font pages from a TrueType face and renders previews
docs/DEVLOG.md             reverse-engineering log: addresses, findings, tests
docs/RENDERER_API.md       the Stealth renderer plug-in interface
docs/ROADMAP.md            what's next
docs/TEXT_SCALING_PLAN.md  plan for readable text (step 3)
CLAUDE.md                  working notes for AI-assisted development (build, test, deploy rules)
release/                   built release zips (not committed; attach them to GitHub Releases)
```

## Versioning

[Semantic versioning](https://semver.org). The version is `SBM_VERSION` in `src/soulbringer_modern.c`; development builds carry a `-dev` suffix. Every change is listed in [CHANGELOG.md](CHANGELOG.md) under the next version. A release is tagged `vX.Y.Z` and its zip (`./make_release.sh`) is attached to the GitHub release.

## Legal

This project contains **no game code, art, or data**. It only works with a legally owned copy of Soulbringer. Soulbringer is © its respective rights holders. This project is not affiliated with or endorsed by them, GOG, or Valve.

## Credits

- Reverse-engineered and written by Claude (Anthropic), directed and play-tested by the repository owner.
- Inspired by [requiem-modern](https://github.com/xkaliva0/requiem-modern).
- [Cardo](https://scholarsfonts.net/cardofnt.html) by David J. Perry (SIL Open Font License), bundled for the redrawn fonts.
- [Montserrat](https://github.com/JulietaUla/Montserrat) by the Montserrat Project Authors (SIL Open Font License), ExtraBold instance bundled for the redrawn interface font.
- [Eagle Lake](https://fonts.google.com/specimen/Eagle+Lake) by Astigmatic (Brian J. Bonislawsky) and [MedievalSharp](https://fonts.google.com/specimen/MedievalSharp) by wmk69 (SIL Open Font License), bundled for the redrawn menu font; Eagle Lake as "Soulbringer Uncial", modified with lining figures (`tools/make_menu_font.py`).
- **Soulbringer Text** (`SoulbringerText-Regular.ttf`): an upscaled outline version of the game's text font (names, tooltips, journal), made for this project and released under the SIL Open Font License.
- [DDrawCompat](https://github.com/narzoul/DDrawCompat) by narzoul, which keeps the game running on modern Windows.
- GOG for the 2012 compatibility patch shipped with the game.
