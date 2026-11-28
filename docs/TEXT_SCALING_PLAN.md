# Text scaling plan (0.2, step 3: readable text)

> **Settings renamed 2026-11-28.** This plan uses the setting names of the development builds (`[UI] …TextScale`, `MenuFont`, `[Debug] TextHook`, `FontPages`, `FontRedraw`…). The ini now has `[Text]` / `[Text.Menus]` / `[Text.Screens]` / `[Text.Game]` and `[Fonts]`, the text hook is always on, and the font-texture switches are automatic; DEVLOG 2026-11-28 maps every old key to its new place.

Written 2026-11-21. Status: phase 1 built (decoding, debug log only); phase 2 done (layout code mapped, section 3a; the design in section 5 changed with it); phase 3 built (`[Debug] TextHook=1`, off by default), tested offline and in game (works; the enlarged original fonts are not legible enough). Phase 7 moved up as its own plan: `docs/HD_FONTS_PLAN.md`. Findings
and measurements behind this plan are in `DEVLOG.md`; progress is tracked
in `ROADMAP.md` (0.2, step 3).

## 1. The problem

Soulbringer draws all text with bitmap fonts at a fixed pixel size: glyphs
are 6–14 px tall, lines are 20 px apart, tooltip boxes are 46 px tall with
19 px / 17 px margins, and tooltips sit a fixed number of pixels from the
cursor. None of this scales with the resolution. At 640×480 a glyph is
about 2% of the screen height; at 3840×2160 it is about 0.4%, a quarter of
the size the game was designed for.

What is affected (everything that goes through the game's text code):

| Text | Where | Today in 0.2.0-dev |
|---|---|---|
| Cursor tooltips and object labels | next to the cursor, one line | stretched by the proxy (`TextScale`), looks magnified |
| HUD labels | column move slots (CUT, CUT LOW, COMBO…), bar | original size |
| Multi-line panels | fixed boxes, 48–128 px, 20 px per line | original size |
| Menus (inventory, character, options, journal, save/load) | pillarboxed 4:3 area | glyphs moved with the menu but not resized |
| Conversation screen | wrapped text beside the portrait | original size |
| Loading screen, level names, credits | full screen | original size |

## 2. Goal

One setting, `[UI] TextScale` (percent), makes **all** text larger, with the
game itself laying it out at the larger size:

- glyph size, letter spacing, line spacing, box margins and the gap to the
  cursor grow together, so every piece of text keeps its proportions;
- wrapping happens at the larger size, so text stays inside its panels
  (more lines, not text running past the edge);
- glyphs still read the same rectangle of the font texture (no wrong
  letters, no bleeding from neighbouring glyphs);
- a sensible default for the resolution (`auto`), overridable per user;
- **per kind of text** (decided 2026-11-23, user): one size per kind, by the
  game font it uses (`[UI] JournalTextScale` phil, `TooltipTextScale` hbut,
  `MenuTextScale` magc, `InterfaceTextScale` 16xY/16xB/16xX and their
  low-resolution 5px versions, `NameTextScale` mgrn/mred, `NumberTextScale`
  numg/numr, `CreditsTextScale` cred; 0 = `TextScale`): a single 300% made
  the 4K text readable but cut off text in the options menu and the journal
  tab. A kind changes every place its font is drawn; what each font is used
  for is in section 3 ("Where each font is used");
- the proxy-side tooltip stretching is removed once this works (it is the
  cause of the "magnified word" bug).

Not a goal (yet): sharper letters. Enlarged bitmap glyphs stay soft or
blocky; that is a separate, optional phase (section 6, phase 7).

## 3. What we know

**Where each font is used** (decoded text of 11 sessions, 2026-11-21..23,
counted per string; DEVLOG "text kinds checked"):

| Font | Kind | Seen drawing |
|---|---|---|
| `phil` | Journal | quest titles and entries on the journal page |
| `hbut` | Tooltip | every cursor tooltip (world objects, HUD bar "Hit Points 100/100", HUD column "Combo screen", inventory items and slots "Held Weapon", menu "Back"), place names ("The Eastern River", "Madrigal Inn") |
| `magc` | Menu | main menu buttons, save/load list (names, dates, "at the inn"), quit dialog, copyright line, conversations (Chant's speech, "Local Talk", "Leave") |
| `16xY` | Interface | options menu labels and values (Speech, Music, Gore, On/Off, Tool Tips, Delay…), character screen (Strength, Hit Points, Level, Experience… and their numbers), inventory numbers (stack counts, gold, encumbrance), HUD column move labels (CUT, COMBO) |
| `16xB` | Interface | the selected HUD column move label (CUT LOW) |
| `16xX` | Interface | not seen (loaded with the other two) |
| `5px*` | Interface | not seen: the low-resolution set the game loads instead of 16x* below 1000 px screen width (`0x67FB79`) |
| `mgrn` | Name | names over characters (Traveller, Emma, Innkeeper…), "Found an item!", "Gained object: …" |
| `mred` | Name | "Empty.", "Locked." |
| `numg`, `numr` | Number | not seen (stack counts and stats use 16xY) |
| `cred` | Credits | not seen in a session (credits screen) |


**Fonts** (`data\*.fnt`, 110,664 bytes; `numg`/`numr` 770): `u16 count`, then
32-byte records per character: `u16 code`, `u16` (0xBFF7), `i32 material`
(font texture: 0x4E, 0x4F, 0x50, 0x51; 0 = no glyph, e.g. space), `i32 u`,
`i32 v`, `i32 width`, `i32 height`, `i32` (0), `u16` (vertical offset, 4 for
most glyphs), `u16 0x65`. Width and height are both the size on screen and
the rectangle in the texture, so the font data cannot be scaled **on its
own**; phase 2 found the single place where they are used as the texture
rectangle (section 3a), which makes scaling the data workable.
Fonts named in the exe: `5pxb/r/x/y`, `16xb/x/y`, `phil`, `hbut`, `magc`,
`mgrn`, `mred`, `cred`, `numg`, `numr`, `debug`.

**Call chain** (from the stack walk in the debug log):

```
0x432300  (text object: layout)  ── calls ──▶  0x4345A4  (one UTF-16 string)
0x635462  (per frame: walks the text-object list
           [[0x1E4CFBC]+4]+8, linked through +0x510)  ──▶  0x42B024  (glyph flush)
                                                              └─▶ renderer AddTri (0x0042B0BC in logs)
```

**Glyph flush `0x42B024`** (ecx = text object; `+4` glyph count, `+8` font
material): copies per-glyph records whose corner positions are already in
screen pixels (`+0x14/+0x18`, `+0x2C/+0x30`, `+0x44/+0x48`, …) and whose
texture coordinates are integers (`+0x20/+0x24`, …, × 1/texture size at
`0x3678458`…). It adds only global offsets (`0x3678450`…). **All sizes are
decided before it, in layout.**

**Glyph batches are built by `0x626480`** (add glyph: thiscall on the text
system `[0x1E4CFBC]`; args: corner block, material, colour, double x, double
y; finds or creates the per-material batch list, `0x626720` → `0x626960` →
`0x626AE4` copies 96 bytes of finished corners and texel coordinates into
the record). It is called only from the draw-glyph function `0x433714`
(12 call sites), which reads the font record (`+8` u, `+0xC` v, `+0x10`
width, `+0x14` height) and computes the corners (with clipping). Details in
section 3a.

**Layout `0x4345A4`** (thiscall, arg = start index into the UTF-16 string at
`[ecx]`): maps each character through a binary-searched table of 6-byte
entries at `[[ecx+0x204]]` (two modes, `[ecx+0x1C4]`), then measures with
widths from the font object (`[edi+0xC]`, `[edi+0x14]`, `[…+0x10]`) and what
kerning pairs (`[esi+8*eax+4]`). It measures one word, for wrapping
(section 3a).

**Tooltip geometry** (measured from the logs): boxes 46 px tall; text line
centred (glyph centres 21–25 px below the box top); offsets from the cursor
tip (−17, −38) HUD bar and menus, (−17, +2) column, (+11, +30) / (+11, −10)
world labels; multi-line panels 48 / 88 / 108 / 128 px.

**Layout context:** menus are pillarboxed by the proxy (positions mapped into
the centred 4:3 area, glyphs moved rigidly, not resized); the HUD is moved by
`HudLayout`; the HUD mouse fix and the tooltip stretching run in
`src/ui_layout.inc`.

## 3a. The layout code (phase 2 results)

Full map with addresses and original bytes: DEVLOG, "Text layout code
mapped". All of it is byte-identical in `SoulbringeVC.exe` and
`SoulbringeVCnoeax.exe`.

```
0x411634  auto-fit box     box rect from the measured text (when not set by code)
0x549FC4  screen rect      box + position, kept inside the clip rect
0x432094  draw paragraph   lines and words; wrap at the text rect width
  0x434284  line start       alignment (left / right / centre)
  0x4345A4  measure word
  0x432820  draw word        pen advance: x += width + spacing + 1 (+ kerning)
    0x433714  draw glyph     corners from pen + record; texels = record + (clipped - unclipped), 1:1
      0x626480  add glyph    only called from 0x433714
```

- **Font object** (0x30 bytes, loaded by `0x681ED0` → file read `0x628394`,
  one call site `0x681F5E`): records `+4` (sorted) and `+0xC` (direct,
  codes < 256), count `+0x14`, **line height `+0x18`** (max y offset +
  height, computed at load), line gap `+0x20` (1), kerning `+0x24/+0x28`,
  letter spacing `+0x2C` (0, or −1 for 16xy).
- **Line step** = font `+0x18` + `+0x20` + text object `+0x1DC` (0):
  line height + 1, a font property, not a constant (answers 9.1).
- **Wrap width** = the text rect width; lines that do not fit the box
  height are not drawn.
- **Boxes** are auto-fitted from the font metrics (`0x411634`) unless code
  sets them (answers 9.2). Tooltip box margins are W/200 × H/154 and
  tooltip positions are cursor + offsets × W/768, H/512: **already
  proportional to the screen**.
- **The game already switches fonts by resolution:** at a screen width of
  1000 px or more it loads `16xy/16xb/16xx` instead of `5pxy/5pxb/5pxx`
  (`0x67FB79`). 4K uses the large set already; there is no larger one.
- Every glyph of every path goes through `0x433714` → `0x626480`; five
  other callers of `0x433714` (edit fields, lists) do their own pen work
  from the same font metrics (answers 9.3: numbers and credits use the
  same fonts and code).

## 4. Options considered

| Option | Verdict |
|---|---|
| **A. Stretch drawn text in the proxy** (what `TextScale` does now for cursor tooltips) | Works only for one-line text that owns its box. Multi-line text would overflow, HUD labels would cover the art, and stretched glyphs look magnified. Keep only until B is ready. |
| **B. Scale in the game's layout code** (hook) | Chosen before phase 2. The goal stands (the game lays out, wraps and sizes boxes at the larger size), but patching each measurement (advance, word width, auto-fit, alignment, line step, 5 secondary pen loops) is many patches; C + texel fix reaches the same with two. |
| C. Scale the font data on load | **Chosen after phase 2, combined with one texel fix** (section 5): every measurement reads the font data, and only `0x433714` uses it as the texture rectangle. |
| D. Draw all text ourselves (own font rendering) | Too large: we would have to reproduce wrapping, colours, effects and every text box. Not needed if B works. |
| E. Higher-resolution font textures | Not a sizing solution, but the way to sharp letters on top of B (phase 7). |

## 5. Design (revised after phase 2)

Scale **the font metrics**, and correct the texture coordinates at the one
place the metrics double as the texture rectangle:

1. **After each font load** (hook the call at `0x681F5E`): width, height
   and y offset of every record (both tables), line height `+0x18`, line
   gap `+0x20` and kerning adjusts × s; original sizes kept aside. Fonts
   already loaded when the proxy installs the hook are scaled in place.
2. **At `0x626480`** (add glyph; ebp is the `0x433714` frame): rewrite
   each vertex's texel as u = u_game − (x − x0) + (x − x0)/s (v likewise),
   so the quad is s times larger and still shows the original glyph,
   clipping included.

Everything else follows: glyph quads, advances and kerning, word widths
and wrapping, alignment, auto-fitted boxes and line steps all come from the
scaled metrics. Box margins and cursor offsets are already proportional to
the screen and stay as the game computes them. Fixed boxes (rect set by
code) keep their size and wrap more lines; what does not fit is cut off,
so they need phase 4 (grow them, or cap s per category).

s comes from `TextScale`: `auto` = a value based on the screen height,
or a fixed percentage. The game's own reference is the large font set at
1024×768 or more: matching its proportion at 768 px high gives
s = H / 768 (2.8 at 4K, 1.4 at 1080p); H / 1080 (2.0 at 4K, 1.0 at 1080p)
is the gentler option. Decided in phase 5 from screenshots.

Where text must fit fixed artwork (HUD slot labels, button captions) the
scale may need to be capped per category ("fit" rule: never larger than the
slot), decided in phase 5 from screenshots.

## 6. Phases

Each phase ends with offline tests passing and, where it touches the game,
an in-game check with F11 screenshots.

### Phase 1: Instrument (debug log only)

Make the log describe text objects, not just drawn glyphs.

**Done differently than first planned (no code hooks):** the glyph batch is
in the game's `edi` when it calls the renderer (the proxy's stub saves the
registers), and every glyph's texel rectangle names its character, because
no rectangle is shared by two characters across the 16 fonts. So the proxy
decodes each batch back into text (`src/text_decode.inc`), joins batches into
lines by position, and logs them. Calls into `0x432300`/`0x4345A4` are not
needed for this; they come back in phase 2.

- Log each text object at glyph flush (`0x42B024`): address, font material,
  glyph count, the string (UTF-16 from the object or the stack walk), the
  bounding box of its glyphs, and the record layout of its first glyphs
  (positions and UVs).
- Log calls into `0x432300` / `0x4345A4` (text, start index, return value).
- Clean up the hover-label stack walk (the junk strings) by taking the
  string from the text object instead.
- Record sessions covering every text type in the section 1 table (a
  checklist screen list in `tests/README.md`).

Exit: for any glyph drawn on screen, the log names its string, its font and
its text object.

### Phase 2: Map the layout code (static analysis) — done

Results in section 3a and the DEVLOG ("Text layout code mapped").

- Font object in memory: where a `.fnt` is loaded (`*.fnt` string at
  `0x3683FAC`, names around `0x36A8CD8`), its fields (record table,
  count, kerning, line height).
- The function that writes glyph records (`+0x14…` positions, `+0x20…`
  UVs): its inputs (pen x/y, glyph record) and where the pen advances.
- Where line breaks happen, the wrap width and the 20 px line step.
- Who builds tooltip/label boxes from the text size (the `0x00456ABD`
  draws) and where the cursor offsets (−17/−38, +11/+30 …) are applied.
- Confirm the same addresses in `SoulbringeVC.exe` and
  `SoulbringeVCnoeax.exe` (as for the widescreen patch).

Exit: a written map in DEVLOG with the exact instructions to change, their
original bytes, and why each one is safe to change.

### Phase 3: Prototype the scale (glyph size, advance, line height) — built, in use in game since 2026-11-22

Built as `src/text_scale.inc` + the hooks in `soulbringer_modern.c`
(DEVLOG "Text hook (phase 3)"). Differences from the plan below: a third
patch (the letter-spacing setter `0x691F50`); fonts are **not** restored on
unload (nothing is drawn without the renderer, and the next load recognises
them as scaled); line height, gap and spacing are recomputed, not
multiplied, because the game reloads fonts into the same objects.

- Two hooks from phase 2 (verified original bytes, restored on unload,
  like the widescreen patch): the font-load call at `0x681F5E`
  (`E8 31 64 FA FF`) and the entry of `0x626480`
  (`53 8B DC 83 EC 08`, 6 bytes); fonts already loaded are scaled in
  place.
- Log: each font scaled (name, s, line height before/after), and whether
  any font was loaded before the hook (to confirm the in-place path).
- Offline: the metric scaling and the texel rewrite are pure functions in
  an `.inc`, tested with the shipped fonts (every glyph keeps its texture
  rectangle at s = 1.5, 2, 3, also when clipped). The phase 1 decoder keeps
  working: it reads texels, which stay as they are.
- Behind a debug setting first (`[Debug] TextHook=1`), so the shipped
  behaviour stays as now until it is proven.
- No wrapping changes yet: expect overflow in panels; check single-line
  text (tooltips, labels) in game.

Exit: in game, a tooltip's text is drawn at 2× by the game itself, letters
correct, no proxy stretching for it.

### Phase 4: Wrapping and boxes

Also 2026-11-26: two places that assume 1024×768-sized text: the main-menu copyright position (`x = W − w·W/768`, now fixed at the 1024×768 place, `0x666143`) and the save-list date's fixed 200 px "..." limit (`0x645ED4`, now scaled with the text); DEVLOG "copyright line and save-list date".

Status (2026-11-26): the auto-fit `0x411634` (mode 2, target `+0x5C`) broke enlarged short labels onto two lines because its own width sum disagrees with the layout `0x413A64` at scale; fixed by re-laying such labels out at the target width with `0x413A64` after the auto-fit (`tx_autofit_choice`, DEVLOG "auto-fit's own width sum"). Remaining: the save-list date truncation; checking the other screens in game.


- Wrapping, auto-fitted boxes and alignment should already follow from
  phase 3 (they read the scaled metrics): confirm in game.
- Panels with a fixed size: the game cuts off lines that do not fit
  (`0x432094`); decide per panel (grow the box where code sets it, or cap
  the scale).
- Tooltip margins and cursor offsets are proportional to the screen
  already: check that the larger boxes still sit well next to the cursor.

Exit: conversation text, multi-line panels and tooltips wrap and fit at
200% and 300% in game.

### Phase 5: Per-category policy and the proxy

- HUD slot labels and button captions: apply the "fit" cap so they stay
  inside their art.
- Menus: confirm text scales correctly inside the pillarboxed 4:3 area
  (the proxy moves menu glyphs rigidly; with game-side scaling the glyphs
  arrive larger and are only moved).
- Remove the proxy-side tooltip stretching (`tp[8]` scale in
  `ui_layout.inc`); keep the HUD-mouse offset handling.
- `TextScale=auto` default; `TextScale=100` = original behaviour.

Status (2026-11-23): one size per kind of text, by font (`[UI]
JournalTextScale`, `TooltipTextScale`, `MenuTextScale`, `InterfaceTextScale`,
`NameTextScale`, `NumberTextScale`, `CreditsTextScale`; what each covers is in
section 3 "Where each font is used"). Cursor tooltip boxes are kept off the
enlarged cursor (above it, or below at the top of the screen; item tooltips
below), and the text inside a cursor tooltip is recognised by font (hbut for
tooltips, mgrn/mred for the name of the character under the cursor; other
labels under the box, e.g. COMBO, stay put). Limitation: a kind changes every
place its font is drawn (phase 5b).

Exit: every row of the section 1 table checked in game at 1080p, 1440p
and 4K; the "magnified word" bug closed.

### Phase 5b: Per-place sizes (decoupling shared fonts) — in progress: steps 1–2 done (2026-11-24/26), step 3 (naming session) next

**Why (user, 2026-11-23):** fine tuning is hard because one font serves many
places. `InterfaceTextScale` (16xY) is at once the options menu, the
character screen, the inventory numbers and the HUD column labels: the
options menu needs it small (fixed rows) while the character screen and the
HUD labels would be more legible larger. The same holds, less sharply, for
Menu (main menu and save list vs. conversations), Tooltip (cursor tooltips vs.
place names) and Name (names over characters vs. the messages above the HUD).

**Constraint:** the game measures, wraps and sizes boxes from the *font
object's* metrics, and draws from the same metrics (section 3a). A size can
therefore only differ between places that use **different font objects**, or
at times when the object's metrics can be switched consistently for both
measuring and drawing. Scaling individual glyph quads at draw time (what the
proxy tooltip stretching did) breaks wrapping and boxes, so it is not the way.

**Candidate splits** (from the font map, section 3):

| Kind | Possible places | Likely ini names |
|---|---|---|
| Interface (16xY/16xB) | options menu; character screen; inventory numbers; HUD column move labels | `OptionsTextScale`, `CharacterTextScale`, `InventoryNumberTextScale`, `HudLabelTextScale` |
| Menu (magc) | main menu, save/load list, quit dialog; conversations | `MainMenuTextScale`, `ConversationTextScale` |
| Tooltip (hbut) | cursor tooltips; place names | `PlaceNameTextScale` |
| Name (mgrn/mred) | names over characters; messages above the HUD | `MessageTextScale` |

Each new setting falls back to its kind's setting (0 = use the kind), so
existing ini files keep working.

**What the code shows (static analysis, DEVLOG 2026-11-23 "per-place
fonts"):** a text object holds its own font pointer at `+0x208`, and the
whole layout path reads it from there (layout `0x432300`, measure
`0x4345A4`, line start `0x434284`, draw word `0x432820`, auto-fit
`0x411634`). Fonts are shared only because many places copy the same
pointer into `+0x208` (first count; since then 38 places for 16xy and 98 over all shared fonts (92 on 2026-11-24, +6 for 16xb/16xx on 2026-11-26), see "Status by font" below): 16xy alone is copied by 28 instructions in 25
functions, almost all a single `mov edx,[0x35565DC]` (`8B 15 DC 65 55 03`)
or `mov eax,[0x35565DC]` (`A1 DC 65 55 03`) just before
`mov [ecx+0x208],reg`. The menu panels are built once at startup by
`0x4248C4` into a global text-object table (`0x3576878`, 0x94 per entry);
`0x41E910` is most likely the HUD column labels (10 items, 16xy / 16xb /
16xx by state). Some fonts already have several objects (hbut 6, magc 2,
phil 2, mgrn/mred 2 each). No code compares font pointers.

**Approach (replaces the earlier three-way choice):**

1. **Places with their own object already** (some hbut, magc, phil,
   mgrn/mred places): scale that object by the place. The text hook already
   scales each object separately and runs at every load (`0x681F5E`); it
   needs the load site (return address) to know the place.
2. **Places that share an object** (16xy above all): **a copy of the font
   object per place, and a redirected pointer read.**
   - At each font load (`0x681F5E`), after the original load, the proxy
     makes or refreshes one copy of the object per place that uses it:
     the 0x30-byte object, its sorted records, direct table and kerning
     table (own memory; on a reload the copy is rebuilt from the freshly
     loaded original, so copies never go stale). The letter spacing
     `+0x2C` is copied after the setter runs (16xy gets −1 at
     `0x67FBAA`), or set by the text hook's own spacing rule.
   - Each place's `mov reg,[0x35565DC]` gets its 4-byte address changed to
     a proxy-owned pointer holding that place's copy. Same instruction
     length, original bytes verified first, restored on unload (the
     renderer DLL is loaded twice), skipped and logged if the bytes differ.
     No entry/exit hooks, no new code in the game.
   - The text hook scales each copy at the place's size; texel fix,
     spacing and own-metrics (HD fonts) already work per object.
   - Places that get the font as a function argument (the `[esp+0x10]` /
     `[esp]` reads, e.g. `0x660176`, `0x66B4DD`, `0x670921`) are redirected
     the same way at the argument read.
   Measuring and drawing still read the same `+0x208`, so wrapping and
   boxes stay correct. Per-screen rescaling (old approach 2) is not needed.

**Place map so far** (to confirm in game; "?" = not identified yet):

| Site(s) | Font | Place |
|---|---|---|
| `0x41E939`, `0x41E9D0` | 16xy | HUD column move labels: "Cut", "-" (confirmed in game 2026-11-23) |
| `0x41E960`, `0x41E9A8` / `0x41EA06` | 16xb / 16xx | HUD column: the selected move's label ("Cut Low"), a label in state 1 (static, 2026-11-26) |
| `0x68A418` | 16xy | HUD column "COMBO" (confirmed) |
| `0x4471BB`, `0x64EF68` | 16xy | options menu (confirmed) |
| `0x443D38` | 16xy | map screen title ("Map of", "Madrigal") (confirmed) |
| 10 argument sites (`0x6899F0` callers `0x58A4F2`, `0x648250`, `0x660176`, `0x66B4DD`, `0x66B65E`, `0x670921`, `0x670A16`, `0x689921`; `0x657259`, `0x67B4E9`) | 16xy | character screen, inventory numbers, journal/map titles ? (they drew with the original in session 27032; redirected since build `5f238f7e`) |
| 11 panel builders called by `0x4248C4` (`0x424CE8`, `0x4291A8`, `0x42CE18`, `0x430798`, `0x43580B`, `0x437F1A`/`0x439618`/`0x43AA87`, `0x43BC88`, `0x43F7E8`, `0x443D38`, `0x4471BB`, `0x449448`) | 16xy | menu panels: options, character, inventory… ? |
| `0x424765`, `0x44CD98`, `0x617B48`, `0x6409D2`, `0x648CE8`, `0x64D0F8`, `0x64EF68`, `0x656F38`, `0x65DEF8`, `0x664658`, `0x66B328`, `0x670798`, `0x68A418` | 16xy | ? (runtime text: inventory numbers, character values…) |
| `0x6649F7` New Game, `0x6665FF` Load Game, `0x66639F` Options, `0x66501F` Credits, `0x66525B` Quit, `0x665F9A` copyright | magc | main menu, one place per line (confirmed 2026-11-26) |
| `0x64575F` name, `0x6459B6` "Level", `0x645C73` level number, `0x645EEC` date | magc | save list (confirmed 2026-11-26) |
| `0x66EADB`, `0x66EC3B`, `0x6704BB` | magc | single numbers ("5", "20", "6"), screen ? |
| other magc sites (`0x6549BF` … `0x65F9D3`, `0x66569A`, `0x665A36`, `0x665DBA`, arguments, second object `0x1E78684`) | magc | quit dialog, conversations ? (not reached yet) |
| `0x5567BE` | hbut | HUD and world cursor tooltips (confirmed 2026-11-26) |
| `0x556950` | hbut | item names under the cursor (confirmed) |
| `0x43BB1F`; `0x42C23E`, `0x42F01E` | hbut | "Character screen"/"Journal"; character and equipment labels (Armour Value, Gold, Ring…; Hit Points, Mana Points) (confirmed) |
| other hbut sites | hbut (`0x213ACF4`, `0x213F3DC`) | ? |
| `0x556C6D` | mgrn | names of characters under the cursor (confirmed) |
| `0x556BAA` | mred | messages ? (not seen yet) |

**Goal (user, 2026-11-24):** every font independently controlled per place, for fine tuning and legibility: e.g. the menu font sized separately for the main menu and for conversations, the interface font separately for the options menu, the character screen and the HUD labels.

**Status by font (2026-11-24):**

| Font (object global) | Places | Status |
|---|---|---|
| Interface `16xy` (`0x35565DC`) | 38 sites (28 store, 10 argument) | **mapped** (sessions 27032, 22252, 10752); copies made and redirected (`FontPlaces=1`), all still at InterfaceTextScale; no more reverse engineering needed |
| Menu `magc` (`0x213F3D8`) | 32 sites: 25 store, 5 argument, **2 measuring** (`0x549F7B`, `0x54A524`: measure a list with the font itself) | **in the site table** (step 1 done 2026-11-24); second object `0x1E78684`: 2 argument sites (possibly conversations) |
| Tooltip `hbut` (`0x213ACF4`) | 15 sites: 13 store, 2 argument; second object `0x213F3DC`: 1 store, 2 argument | **in the site table**; the other hbut objects (`0x1E4B06C`, `0x1E4EF84`, two object fields) are already their own places |
| Names `mgrn` (`0x213ACF0`) / messages `mred` (`0x213ACF8`) | 1 store each (in `0x555140`) | **in the site table**; the other reads are lifetime code (delete in `0x554C90`, existence checks in the loader `0x67F2F0`) and stay on the original; second objects at `0x674891` / `0x674823` |
| Interface highlights `16xb` (`0x213F3E0`) / `16xx` (`0x213F3E4`) | 6 stores: HUD column selected move label `41E960`, `41E9A8` (16xb) and state-1 label `41EA06` (16xx) in `0x41E910`; `0x424758` font-by-state helper `4247B1` (16xb), `42478B` (16xx); pool entries in `0x63E88C` `63F1E2` (16xb) | **in the site table** (2026-11-26, 98 places); the other 6 reads are the loader (`0x67F950`: load + spacing setter) and stay on the original |
| Shared pool `0x1E4AF64` | 64 text objects, 16xy, one loop | per-entry font needed (below); stack counts deferred |

**Steps:**
1. **Static pass for the other fonts — done 2026-11-24** (92 sites, build `a8793d49`; DEVLOG "step 1 done"): classify the "other" reads of magc, hbut, mgrn and mred (a store to `+0x208` a few instructions later, an argument, a font method call such as the spacing setter, or a reload), and the functions that use the second objects; add every redirectable site to `fp_site` (generalised: a copy per site of any font global, not only 16xy), with the replay test checking their bytes in both exes.
2. **Per-place size mechanism (offline) — done 2026-11-26** (build `701a3b62`; DEVLOG "step 2"): `g_fpPct[i]` per place (0 = the font kind's setting), set in the ini by site address (`[PlaceTextScale]` `4471BB=150`, as the log names the place); copies scaled at `fp_pct(place, kind)` when made and on every reload; the font page scale and the tooltip box rules (`fp_tip_pct`) take the place sizes into account. Offline test "place sizes": copies at their own sizes, unset places and the original at the kind's, clamping, unknown addresses refused, same after a reload, mutation caught. Measuring places `549F7B`/`54A524` must be given the size of the place whose list they measure (not identified yet; the log reminds).
3. **Logging + first settings build, one session — next** (checklist in `tests/README.md`): every screen once (main menu, save list, options, quit dialog, a conversation, inventory, character screen, journal, map, gameplay HUD, tooltips, place names, NPC names, messages), F11 on each; the place log names every new site; first interface-font settings tried at the same time.
4. **Settings — first part done 2026-11-26** (DEVLOG "text sizes by screen"): 10 groups in `fp_group` (font_places.inc), read from `[UI]`: MainMenu, SaveList, Options, Character, Map, HudLabel (incl. the 16xb/16xx labels), Label, PlaceName (load site `6721CF`, `fp_load`), NpcName, Message; any set size turns the copies on; `[PlaceTextScale]` stays as a developer override applied after the groups. Conversation added 2026-11-27 (`ConversationTextScale`: `6800EF` dialogue, `68017B` choice list). Quit dialog parked by the user 2026-11-27 (two copies: main menu `66569A`/`665A36`/`665DBA`, in-game options `6549BF`/`654D5D`/`6550E3`; suggestion: put both under `MainMenuTextScale`). Original plan: one size per place group in the ini, named by place, not by font: e.g. `HudLabelTextScale`, `OptionsTextScale`, `CharacterTextScale`, `InventoryTextScale`, `MainMenuTextScale`, `ConversationTextScale`, `TooltipTextScale`, `PlaceNameTextScale`… (final names after step 3). Unset or 0 = the font kind's current setting, so existing ini files keep working. In-game check per group, especially fixed panels (options rows, HUD slots) that cut off text that is too large.
5. **Legibility, with it:** the per-place sizes go together with redrawing more fonts in Cardo (HD_FONTS_PLAN step 5: `magc`, then `16xY`/`16xB`, `mgrn`/`mred`), since enlarged original bitmaps stay soft. Each redraw needs own metrics like phil/hbut; the redraw is per font file (one page region), the copies only change metrics, so a redrawn font can be used at several sizes.

Done so far: static analysis of 16xy (2026-11-23); logging build (`FontPlaces=1`, 28 then 38 sites); sessions 27032 / 22252 / 10752 mapped every 16xy place; offline tests for copies (deep copy, independent scaling, reload refresh, spacing, site bytes); place log with text object and pool entry (build `699245c7`); 92 places (step 1, `a8793d49`); sessions 2026-11-26 (pids 26656, 17356, 8080, 25552) named the main menu, save list, tooltip and NPC-name places (table below); per-place sizes (step 2, `701a3b62`).

**Shared text-object pool (found 2026-11-24):** some places are not separate sites at all. `0x66B400` creates a pool of 64 text objects at `0x1E4AF64` in one loop (`0x66B65E`), and screens reuse fixed entries: "Encumbrance", the inventory **stack counts** and character-screen values all come from it. A per-site copy treats them as one place, so these need a **per-entry font**: once the pool exists, set the chosen entries' `+0x208` to their own copy (before their text is laid out). The place log names the entry of every string (`pool#k`, build `699245c7`). First use: a separate, slightly larger size for the stack counts (user request; deferred until the save has stacks in several slots to map the entries). The same applies to any other place found to share the pool.

**Risks:** fixed panels (options rows, HUD slots) still cut off text that is
too large (per-place sizes are what makes this manageable); the copies must
be rebuilt on every reload (the game reloads fonts into the same object and
keeps the line height as a running maximum, DEVLOG phase 3); memory, about
120 KB per copy (records 6.5 KB, direct table 8 KB, kerning ~100 KB: the
text hook scales kerning adjusts, so each copy needs its own), ~3 MB for
every 16xy site; places that take their font from arguments or fields (~30
assignments not traced yet) may need more sites; the screen behind each site
is not known until the logging session.

### Phase 6: Release hardening

- Remove the debug guard; the hook is on with `Layout=1` unless
  `TextScale=100`.
- Unload/reload safety (the game unloads and reloads the renderer DLL):
  patches restored on every unload, as for the DirectInput hook.
- Docs: README settings, CHANGELOG, known issues.

### Phase 7: Legible letters (HD font pages) — moved up, own plan

The original fonts are hard to read even at 1024×768, so enlarging is not
enough: `phil` and `hbut` are redrawn from a TrueType serif into k×-larger
font pages; since 2026-11-26 the interface font (16xY/16xB/16xX, Montserrat
ExtraBold) and the menu font (`magc`, Eagle Lake with lining figures) are
redrawn too. Full plan, verified facts, open points
and steps: **`docs/HD_FONTS_PLAN.md`**.

## 7. Tests

- **Replay (existing):** every recorded screen, all layouts, plus
  `TextScale` runs. Extend the harness with the text hook's math (a pure
  function in an `.inc`, as `ui_hud_aim` is), so it runs offline.
- **New checks:**
  - glyphs grow by s while their texture rectangle stays the same;
  - advances and line steps scale by s (text keeps its shape);
  - wrapped text stays inside its panel at 200% and 300%;
  - HUD labels stay inside their slots (fit cap);
  - text not drawn through the hook is unchanged.
- **Mutation checks:** turning the hook's scale off must make the new
  checks fail (as done for the HUD mouse and tooltip tests).
- **In game:** F11 screenshots of each text type (section 1 table) at
  200% and 300%; one session per HUD layout.

## 8. Risks

| Risk | Mitigation |
|---|---|
| A code patch crashes the game | verified original bytes, smallest possible patches, debug guard first, restore on unload |
| Text overflows fixed panels or HUD art | phase 4 wrapping, phase 5 fit caps |
| Font metrics also used for something that must stay unscaled | phase 2 found only drawing, measuring and box sizing; phase 3 compares screens at s = 1 against the original |
| Fonts loaded before the proxy hooks the loader | scale the known font globals in place at attach; log it |
| Two executables differ | confirm addresses in both before patching |
| Tooltips stretched twice (proxy + game) | remove the proxy stretching in the same build that enables the hook |
| Letters stay soft at large sizes | expected; phase 7 |

## 9. Open questions

1. ~~Is the line step a constant in code or a font field?~~ A font field:
   line height + gap (+ the text object's extra leading), section 3a.
2. ~~Does the game size tooltip/label boxes from the measured text?~~ Yes,
   auto-fit `0x411634`, unless code sets the box.
3. ~~Text paths that bypass `0x4345A4`?~~ Five callers of `0x433714` do
   their own pen work, but every glyph uses the font metrics and
   `0x433714`, so the design covers them.
4. What should `auto` be: H/768 (the game's large-font reference) or
   H/1080?
5. Should HUD labels scale at all at 4K, or only up to what fits the art?
6. Are any fonts loaded before the renderer DLL (phase 3 log)?
