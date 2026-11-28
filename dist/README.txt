Soulbringer Modern - fixes for Soulbringer on modern PCs

INSTALL
  1. Extract this zip into your Soulbringer folder (the one containing
     SoulbringeVC.exe). Steam: ...\steamapps\common\Soulbringer
  2. Run install.bat
  3. Play as usual.

  The game must already run on your PC (most systems need DDrawCompat's
  ddraw.dll in the game folder). Choose your native resolution in the launcher.

UNINSTALL
  Run uninstall.bat. The original renderer is restored.

SETTINGS
  Edit SoulbringerModern.ini (each option is explained inside):
    [Display]    Widescreen (1 = native widescreen), MaxAspect (0 = no limit,
                 or a cap such as 16:9 / 21:9)
    [Interface]  Layout (1 = menus at 4:3 with black bars, HUD re-placed),
                 Hud (split | right | center | stretched), CursorSize
    [Text]       Size = size of all text in percent (200 suits 4K), and
                 optional sizes by category and screen in [Text.Menus],
                 [Text.Screens] and [Text.Game] (0 = follow the level above)
    [Fonts]      All = Sharp (every game font redrawn sharp) or Original,
                 and optionally one choice per font
    [Debug]      Log (developer log, leave at 0; with Log=1 F11 saves a
                 screenshot to the Screenshots folder), Stock, and
                 troubleshooting switches
  Settings files from earlier 0.2.0 development builds are not read any
  more: replace yours with the one in this zip.

KNOWN ISSUES
  Development build, tested at 3840x2160 (16:9). Fixed panels such as the
  options menu can cut off text that is too large.
  See the project's GitHub page. If something looks wrong, try in this
  order: [Debug] HudMouse=0, [Fonts] All=Original, [Debug] TextHook=0,
  [Interface] Hud=stretched, [Interface] Layout=0.

FONTS
  SoulbringerModern\fonts\Cardo-Bold.ttf is Cardo by David J. Perry, used
  under the SIL Open Font License 1.1 (Cardo-OFL.txt next to it). It is used
  by [Fonts] Tooltips/Journal=Cardo only and is not installed in Windows.
  SoulbringerModern\fonts\Montserrat-ExtraBold.ttf is Montserrat by the
  Montserrat Project Authors, SIL Open Font License 1.1 (Montserrat-OFL.txt),
  used by [Fonts] Interface and Numbers, not installed in Windows.
  SoulbringerModern\fonts\SoulbringerCredits-Regular.ttf (Soulbringer
  Credits, an upscale of the game's credits letters) is by the Soulbringer
  Modern contributors, SIL Open Font License 1.1 (SoulbringerCredits-OFL.txt),
  used by [Fonts] Credits, not installed in Windows.
  SoulbringerModern\fonts\SoulbringerUncial-Regular.ttf is a modified
  version of Eagle Lake by Brian J. Bonislawsky (Astigmatic), with lining
  figures, and MedievalSharp-Regular.ttf is MedievalSharp by wmk69, both SIL
  Open Font License 1.1 (SoulbringerUncial-OFL.txt, MedievalSharp-OFL.txt),
  used by [Fonts] Menu only, not installed in Windows.
  SoulbringerModern\fonts\SoulbringerText-Regular.ttf (Soulbringer Text) is
  an upscaled outline version of the game's text font (names, tooltips,
  journal) made for this project, SIL Open Font License 1.1
  (SoulbringerText-OFL.txt), used by [Fonts] Names, Tooltips and Journal.

Source, documentation and issues: see the project's GitHub page.
