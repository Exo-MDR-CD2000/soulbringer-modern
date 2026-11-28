/* Per-place fonts (TEXT_SCALING_PLAN phase 5b; DEVLOG 2026-11-23 "per-place
 * fonts"). Included by soulbringer_modern.c after text_scale.inc, and by the
 * replay harness (tests/replay/harness.c), so everything here is plain C on
 * memory it is given; the code patches live in the DLL.
 *
 * Every text object keeps its font at +0x208, and the whole layout path
 * (measure, wrap, align, auto-fit, draw) reads it from there. A font is
 * shared only because many places copy the same pointer into +0x208, each
 * with one instruction that reads the font global, e.g. at 0x4291A8
 * `mov edx,[0x35565DC]` (8B 15 imm32) then `mov [ecx+0x208],edx`.
 *
 * So a place gets its own font by:
 *   1. a copy of the font object (0x30 bytes, own records, direct table and
 *      kerning, so the text hook can scale it on its own), made from the
 *      original right after the game loads it (unscaled), and refreshed on
 *      every reload into the same object;
 *   2. its instruction's address changed from the font global to a pointer
 *      that holds its copy (same length; done by the DLL, restored on unload).
 *
 * Logging build (2026-11-23): every 16xy site has its own copy, all at the
 * Interface size, so the screen does not change; the debug log names the
 * place of every text line. Since 2026-11-24 the sites of magc, hbut, mgrn
 * and mred too (92 places); 16xb and 16xx (6) on 2026-11-26, 98 in all.
 *
 * Per-place sizes (step 2, 2026-11-26): each place has a size, g_fpPct[i]
 * (percent; 0 = the size of its font's kind, e.g. [Text.Screens] All for
 * 16xy), and its copy is scaled to it when made and on every reload
 * (fp_pct). Only a copy changes: the original and the other places keep
 * their size, and measuring and drawing at that place both read the copy, so
 * wrapping and boxes follow. Set in the ini by site address, as the log names
 * the place ([PlaceTextScale] 4471BB=150 for "copy@4471BB"); names by place
 * come in step 4, after the naming session.
 *
 * Needs from the includer: DWORD, readable(), TX_PTR (text_scale.inc), and
 * FP_ALLOC(n): the game-side address of n zeroed bytes that stay valid for
 * the life of the process (VirtualAlloc in the DLL: text objects may keep
 * pointing at a copy after this DLL unloads), 0 if none.
 */

/* the sites: instruction address, the global it reads, where its imm32 is, and
   how the font reaches the text object: 0 = stored to +0x208 right after,
   1 = passed as an argument (to the text-object initializers 0x6899F0, which
   stores its 5th argument to +0x208, or 0x691C00), 2 = read to measure text
   with the font directly (its spacing, 0x691810), which must use the same copy
   as the place it measures for.
   Not listed (must keep reading the original): the loads and writes of the
   globals, and the font lifetime code (delete in 0x554C90 / 0x608550, the
   existence checks in the loader 0x67F2F0). 16xy entries first (the offline
   test uses the first two). */
typedef struct { DWORD site; DWORD global; unsigned char immAt, how; } FpSite;
static const FpSite fp_site[] = {
    /* 16xy (0x35565DC): HUD column labels (likely), normal state, two tables */
    { 0x41E939, 0x35565DC, 1, 0 }, { 0x41E9D0, 0x35565DC, 1, 0 },
    /* the menu panels built at startup by 0x4248C4 */
    { 0x424CE8, 0x35565DC, 2, 0 }, { 0x4291A8, 0x35565DC, 2, 0 }, { 0x42CE18, 0x35565DC, 2, 0 },
    { 0x430798, 0x35565DC, 2, 0 }, { 0x43580B, 0x35565DC, 2, 0 }, { 0x437F1A, 0x35565DC, 2, 0 },
    { 0x439618, 0x35565DC, 2, 0 }, { 0x43AA87, 0x35565DC, 1, 0 }, { 0x43BC88, 0x35565DC, 2, 0 },
    { 0x43F7E8, 0x35565DC, 2, 0 }, { 0x443D38, 0x35565DC, 2, 0 }, { 0x4471BB, 0x35565DC, 2, 0 },
    { 0x449448, 0x35565DC, 2, 0 },
    /* other 16xy places (screen not known yet) */
    { 0x424765, 0x35565DC, 2, 0 }, { 0x44CD98, 0x35565DC, 2, 0 }, { 0x617B48, 0x35565DC, 2, 0 },
    { 0x6409D2, 0x35565DC, 1, 0 }, { 0x648CE8, 0x35565DC, 2, 0 }, { 0x64D0F8, 0x35565DC, 2, 0 },
    { 0x64EF68, 0x35565DC, 2, 0 }, { 0x656F38, 0x35565DC, 2, 0 }, { 0x65DEF8, 0x35565DC, 2, 0 },
    { 0x664658, 0x35565DC, 2, 0 }, { 0x66B328, 0x35565DC, 2, 0 }, { 0x670798, 0x35565DC, 2, 0 },
    { 0x68A418, 0x35565DC, 2, 0 },
    /* passed as an argument (added after session pid 27032: the character screen, inventory
       numbers and journal/map labels still drew with the original). 0x6899F0 callers: */
    { 0x58A4F2, 0x35565DC, 1, 1 }, { 0x648250, 0x35565DC, 1, 1 }, { 0x660176, 0x35565DC, 1, 1 },
    { 0x66B4DD, 0x35565DC, 1, 1 }, { 0x66B65E, 0x35565DC, 1, 1 }, { 0x670921, 0x35565DC, 1, 1 },
    { 0x670A16, 0x35565DC, 1, 1 }, { 0x689921, 0x35565DC, 1, 1 },
    /* first argument of 0x674ED0 and 0x677350 */
    { 0x657259, 0x35565DC, 1, 1 }, { 0x67B4E9, 0x35565DC, 1, 1 },

    /* menu font magc (0x213F3D8; main menu, save list, quit dialog, conversations: which is which
       comes from the place log). Stores: */
    { 0x64575F, 0x213F3D8, 2, 0 }, { 0x6459B6, 0x213F3D8, 2, 0 }, { 0x645C73, 0x213F3D8, 2, 0 },
    { 0x645EEC, 0x213F3D8, 2, 0 }, { 0x6549BF, 0x213F3D8, 2, 0 }, { 0x654D5D, 0x213F3D8, 2, 0 },
    { 0x6550E3, 0x213F3D8, 2, 0 }, { 0x65EA05, 0x213F3D8, 2, 0 }, { 0x65ED6A, 0x213F3D8, 2, 0 },
    { 0x65F0C3, 0x213F3D8, 2, 0 }, { 0x65F4D7, 0x213F3D8, 2, 0 }, { 0x65F830, 0x213F3D8, 2, 0 },
    { 0x65F9D3, 0x213F3D8, 2, 0 }, { 0x6649F7, 0x213F3D8, 2, 0 }, { 0x66501F, 0x213F3D8, 2, 0 },
    { 0x66525B, 0x213F3D8, 2, 0 }, { 0x66569A, 0x213F3D8, 2, 0 }, { 0x665A36, 0x213F3D8, 2, 0 },
    { 0x665DBA, 0x213F3D8, 2, 0 }, { 0x665F9A, 0x213F3D8, 2, 0 }, { 0x66639F, 0x213F3D8, 2, 0 },
    { 0x6665FF, 0x213F3D8, 2, 0 }, { 0x66EADB, 0x213F3D8, 1, 0 }, { 0x66EC3B, 0x213F3D8, 1, 0 },
    { 0x6704BB, 0x213F3D8, 1, 0 },
    /* magc as an argument (0x6899F0 and others) */
    { 0x613914, 0x213F3D8, 1, 1 }, { 0x6178B1, 0x213F3D8, 1, 1 }, { 0x660017, 0x213F3D8, 1, 1 },
    { 0x6800EF, 0x213F3D8, 1, 1 }, { 0x68017B, 0x213F3D8, 1, 1 },
    /* magc read to measure a list (loop to [0x213F448]; 0x549E40 from 0x5EDBD4, 0x54A410 by pointer) */
    { 0x549F7B, 0x213F3D8, 2, 2 }, { 0x54A524, 0x213F3D8, 2, 2 },
    /* second magc object 0x1E78684 (loaded at 0x673092) */
    { 0x5F4D47, 0x1E78684, 1, 1 }, { 0x67324E, 0x1E78684, 1, 1 },

    /* tooltip font hbut (0x213ACF4; cursor tooltips, labels): stores */
    { 0x42C23E, 0x213ACF4, 1, 0 }, { 0x42F01E, 0x213ACF4, 1, 0 }, { 0x4355CE, 0x213ACF4, 1, 0 },
    { 0x437CB1, 0x213ACF4, 1, 0 }, { 0x43BB1F, 0x213ACF4, 1, 0 }, { 0x43E17E, 0x213ACF4, 1, 0 },
    { 0x442DCE, 0x213ACF4, 1, 0 }, { 0x445BEE, 0x213ACF4, 1, 0 }, { 0x449A9E, 0x213ACF4, 1, 0 },
    { 0x5567BE, 0x213ACF4, 1, 0 }, { 0x556950, 0x213ACF4, 1, 0 }, { 0x556B33, 0x213ACF4, 1, 0 },
    { 0x556C88, 0x213ACF4, 1, 0 },
    /* hbut as an argument (0x691C00; 0x6899F0 in the loader 0x67F2F0) */
    { 0x635CB8, 0x213ACF4, 1, 1 }, { 0x67F3CB, 0x213ACF4, 2, 1 },
    /* second hbut object 0x213F3DC (loaded at 0x67FB12) */
    { 0x43D315, 0x213F3DC, 1, 0 }, { 0x6360AC, 0x213F3DC, 1, 1 }, { 0x670B26, 0x213F3DC, 1, 1 },
    /* names mgrn (0x213ACF0) and messages mred (0x213ACF8), in 0x555140 */
    { 0x556C6D, 0x213ACF0, 1, 0 }, { 0x556BAA, 0x213ACF8, 1, 0 },

    /* interface highlight fonts (2026-11-26; both loaded by 0x67F950 next to 16xy, same kind):
       16xb (0x213F3E0) = selected, 16xx (0x213F3E4) = a third state. HUD column labels 0x41E910:
       the selected move (0x6B80E4) in both slot tables (0x357DC10, 0x357DC14), and state 1
       ([0x365B0C2 + i*0x208] == 1) in 16xx; next to the 16xy places 41E939 / 41E9D0 */
    { 0x41E960, 0x213F3E0, 1, 0 }, { 0x41E9A8, 0x213F3E0, 1, 0 }, { 0x41EA06, 0x213F3E4, 1, 0 },
    /* 0x424758 (font by state: 0 16xy 424765, 1 16xx, 3 16xb; called 10 times from 0x63E3xx) */
    { 0x42478B, 0x213F3E4, 2, 0 }, { 0x4247B1, 0x213F3E0, 2, 0 },
    /* 0x63E88C: entries of the shared text-object pool 0x1E4AF6C + 4i */
    { 0x63F1E2, 0x213F3E0, 1, 0 },
};
#define FP_NSITE ((int)(sizeof fp_site / sizeof fp_site[0]))

/* the opcode bytes in front of the imm32: 8B /r with mod 00 rm 101 = mov r32,[imm32]
   (8B 05/0D/15/1D/35/3D; 15 edx and 0D ecx are used); A1 = mov eax,[imm32] */
static int fp_opcode_ok(const unsigned char* p, int immAt) {
    return immAt == 2 ? p[0] == 0x8B && (p[1] & 0xC7) == 0x05 : immAt == 1 ? p[0] == 0xA1 : 0;
}

typedef struct {
    DWORD obj;        /* the copy's font object (0x30 bytes), stable for the process */
    DWORD data, cap;  /* its records + direct table + kerning */
    DWORD src;        /* the original it was copied from */
    int   refreshes;
} FpCopy;
static FpCopy g_fpCopy[FP_NSITE];

/* bytes needed for a deep copy of font, 0 if it is not a sane font object */
static DWORD fp_need(const unsigned char* font) {
    int n, nk, i; DWORD need;
    const unsigned char* k;
    if (!readable(font, 0x30)) return 0;
    n = *(const int*)(font + 0x14);
    if (n <= 0 || n > TX_MAXR || !*(const DWORD*)(font + 4) || !readable(TX_PTR(*(const DWORD*)(font + 4)), (DWORD)n * 32)) return 0;
    need = (DWORD)n * 32;
    if (*(const int*)(font + 8)) {
        if (!*(const DWORD*)(font + 0xC) || !readable(TX_PTR(*(const DWORD*)(font + 0xC)), 256 * 32)) return 0;
        need += 256 * 32;
    }
    nk = *(const int*)(font + 0x28);
    k = *(const DWORD*)(font + 0x24) ? TX_PTR(*(const DWORD*)(font + 0x24)) : 0;
    if (nk < 0 || nk > 4096 || (nk && (!k || !readable(k, (DWORD)nk * 12)))) return 0;
    need += (DWORD)nk * 12;
    for (i = 0; i < nk; i++) {
        int np = *(const int*)(k + i * 12 + 8);
        DWORD pp = *(const DWORD*)(k + i * 12 + 4);
        if (np < 0 || np > 4096 || (np && (!pp || !readable(TX_PTR(pp), (DWORD)np * 8)))) return 0;
        need += (DWORD)np * 8;
    }
    return need;
}

/* (re)make c as a deep copy of the font object at src. Returns 1 if done. */
static int fp_copy(FpCopy* c, DWORD src) {
    const unsigned char* s = TX_PTR(src);
    unsigned char *o, *d;
    DWORD need = fp_need(s), at;
    int n, nk, i;
    if (!need) return 0;
    if (!c->obj && !(c->obj = FP_ALLOC(0x30))) return 0;
    if (c->cap < need) {             /* a larger font reloaded: new block (the old one stays: cheap, rare) */
        DWORD a = FP_ALLOC(need);
        if (!a) return 0;
        c->data = a; c->cap = need;
    }
    o = TX_PTR(c->obj); d = TX_PTR(c->data);
    for (i = 0; i < 0x30; i++) o[i] = s[i];          /* name (shared), counts, fields */
    n = *(const int*)(s + 0x14);
    at = 0;
    for (i = 0; i < n * 32; i++) d[at + i] = TX_PTR(*(const DWORD*)(s + 4))[i];
    *(DWORD*)(o + 4) = c->data + at; at += (DWORD)n * 32;
    if (*(const int*)(s + 8)) {
        const unsigned char* q = TX_PTR(*(const DWORD*)(s + 0xC));
        for (i = 0; i < 256 * 32; i++) d[at + i] = q[i];
        *(DWORD*)(o + 0xC) = c->data + at; at += 256 * 32;
    }
    nk = *(const int*)(s + 0x28);
    if (nk) {
        const unsigned char* k = TX_PTR(*(const DWORD*)(s + 0x24));
        DWORD tab = at; int e, j;
        for (i = 0; i < nk * 12; i++) d[tab + i] = k[i];
        at += (DWORD)nk * 12;
        for (e = 0; e < nk; e++) {
            int np = *(const int*)(k + e * 12 + 8);
            const unsigned char* p = np ? TX_PTR(*(const DWORD*)(k + e * 12 + 4)) : 0;
            for (j = 0; j < np * 8; j++) d[at + j] = p[j];
            *(DWORD*)(d + tab + e * 12 + 4) = np ? c->data + at : 0;
            at += (DWORD)np * 8;
        }
        *(DWORD*)(o + 0x24) = c->data + tab;
    } else *(DWORD*)(o + 0x24) = 0;
    c->src = src; c->refreshes++;
    return 1;
}

/* the game (re)loaded the font object src: refresh the copies of every site
   whose global holds it. Call before src is scaled (it is fresh from the file
   then). gv(global) reads a global's value (game memory in the DLL, a stand-in
   offline). Returns the number of copies made. */
static int fp_refresh(DWORD src, DWORD (*gv)(DWORD global)) {
    int i, made = 0;
    for (i = 0; i < FP_NSITE; i++)
        if (src && gv(fp_site[i].global) == src && fp_copy(&g_fpCopy[i], src)) made++;
    return made;
}

/* the site whose copy this font object is, or -1 */
static int fp_place(DWORD font) {
    int i;
    if (!font) return -1;
    for (i = 0; i < FP_NSITE; i++) if (g_fpCopy[i].obj == font) return i;
    return -1;
}

/* ---- per-place sizes (TEXT_SCALING_PLAN phase 5b step 2) ---- */
#define FP_PCT_MIN 50
#define FP_PCT_MAX 600
static int g_fpPct[FP_NSITE];   /* size of each place in percent; 0 = its font kind's size */

/* Places that are a font object of their own (not a shared font copied into
   text objects): sized where the game loads them, by the load site (the call
   to 0x681ED0, as the log names it: "loaded (by 6721CF)"). The object is
   scaled at that size when loaded, so everything drawn with it follows. Only
   load sites whose object nothing else reads are listed. */
static const DWORD fp_load[] = {
    /* hbut into the object at 0x1E4EF84 (made in 0x6720E0): place names ("The Eastern River");
       read only by the two place-name texts (0x672226, 0x6727D6), deleted at 0x61746D */
    0x6721CF,
};
#define FP_NLOAD ((int)(sizeof fp_load / sizeof fp_load[0]))
static int g_fpLoadPct[FP_NLOAD];

/* the size of a font object the game just loaded at loadSite: its place's if set, else kindPct */
static int fp_load_pct(DWORD loadSite, int kindPct) {
    int j;
    for (j = 0; j < FP_NLOAD; j++) if (fp_load[j] == loadSite && g_fpLoadPct[j] > 0) return g_fpLoadPct[j];
    return kindPct;
}

/* the size place i's copy is scaled to: its own if set, else its font kind's (kindPct) */
static int fp_pct(int i, int kindPct) {
    return i >= 0 && i < FP_NSITE && g_fpPct[i] > 0 ? g_fpPct[i] : kindPct;
}

/* set the size of the place at this site address (as the log names it, copy@XXXXXX);
   0 or less = back to the kind's size, else clamped like the other text sizes (50-600).
   Also takes a load site (fp_load). Returns the place's index (FP_NSITE + j
   for a load site), -1 if the address is not a place. */
static int fp_set_pct(DWORD site, int pct) {
    int i, v = pct <= 0 ? 0 : pct < FP_PCT_MIN ? FP_PCT_MIN : pct > FP_PCT_MAX ? FP_PCT_MAX : pct;
    for (i = 0; i < FP_NSITE; i++)
        if (fp_site[i].site == site) { g_fpPct[i] = v; return i; }
    for (i = 0; i < FP_NLOAD; i++)
        if (fp_load[i] == site) { g_fpLoadPct[i] = v; return FP_NSITE + i; }
    return -1;
}

/* ---- text size settings (the ini's [Text], [Text.Menus], [Text.Screens], [Text.Game]) ----
   One table for every text size the user can set (2026-11-28, replaces the per-kind
   [UI] JournalTextScale ... and the per-screen [UI] MainMenuTextScale ... keys).
   Each setting is a percent; 0 / missing = follow its parent (an item follows its
   category's All, a category follows [Text] Size, Size defaults to 200).
   - kind >= 0: the setting is the size of that font kind (0 Journal phil, 1 Tooltip
     hbut, 2 Menu magc, 3 Interface 16x / 5px, 4 Name mgrn+mred, 5 Number numr/numg,
     6 Credits cred): every place of that font that is in no item, and every font
     object that is no place, is drawn at it.
   - place[]: the item sizes these places (sites or load sites, identified in game
     sessions: DEVLOG, TEXT_SCALING_PLAN place map); pk = the kind of those places.
     They get their own font copy only when their size differs from their kind's.
   Kept in this order: parents before their items. Read by the DLL (ts_read in
   soulbringer_modern.c) and by the replay harness ("I" command). */
#define FP_GMAX 8
#define TS_KINDS 7
typedef struct {
    const char* section; const char* key;
    signed char parent, kind, pk;
    const char* what;
    DWORD place[FP_GMAX];
} TsKey;
static const TsKey ts_key[] = {
    /* 0 */ { "Text", "Size", -1, -1, -1, "all text", { 0 } },
    /* 1 */ { "Text", "Tooltips", 0, 1, -1, "tooltips next to the cursor (world, HUD, inventory, menu buttons)", { 0 } },
    /* 2 */ { "Text.Menus", "All", 0, 2, -1, "every menu below, and the quit dialog", { 0 } },
    /* 3 */ { "Text.Menus", "MainMenu", 2, -1, 2, "main menu: New Game, Load Game, Options, Credits, Quit, copyright line",
              { 0x6649F7, 0x6665FF, 0x66639F, 0x66501F, 0x66525B, 0x665F9A } },
    /* 4 */ { "Text.Menus", "SaveList", 2, -1, 2, "save / load list: save names, level, date",
              { 0x64575F, 0x6459B6, 0x645C73, 0x645EEC } },
    /* 5 */ { "Text.Menus", "Options", 2, -1, 3, "options menu (fixed rows: too large cuts text off)",
              { 0x4471BB, 0x64EF68 } },
    /* 6 */ { "Text.Screens", "All", 0, 3, -1, "every screen below, and interface text not identified yet", { 0 } },
    /* 7 */ { "Text.Screens", "Character", 6, -1, 3, "character stats and numbers, Encumbrance, inventory numbers, stack counts",
              { 0x66B65E, 0x66B4DD, 0x660176, 0x670A16 } },
    /* 8 */ { "Text.Screens", "Labels", 6, -1, 1, "character and equipment labels: Armour Value, Gold, Ring, Hit Points, Mana Points; Character screen, Journal",
              { 0x42C23E, 0x42F01E, 0x43BB1F } },
    /* 9 */ { "Text.Screens", "Journal", 6, 0, -1, "the journal's quest titles and entries", { 0 } },
    /* 10 */ { "Text.Screens", "Map", 6, -1, 3, "map title and descriptions",
              { 0x443D38, 0x67B4E9, 0x4291A8, 0x670798 } },
    /* 11 */ { "Text.Screens", "Credits", 6, 6, -1, "credits and GAME OVER", { 0 } },
    /* 12 */ { "Text.Game", "All", 0, -1, -1, "everything below", { 0 } },
    /* 13 */ { "Text.Game", "Conversations", 12, -1, 2, "conversations: what the character says (beside the portrait) and your choices (top left)",
              { 0x6800EF, 0x68017B } },
    /* 14 */ { "Text.Game", "HudLabels", 12, -1, 3, "HUD column move labels (normal, selected, unavailable) and COMBO",
              { 0x41E939, 0x41E9D0, 0x68A418, 0x41E960, 0x41E9A8, 0x41EA06 } },
    /* 15 */ { "Text.Game", "Names", 12, -1, 4, "names of characters under the cursor",
              { 0x556C6D } },
    /* 16 */ { "Text.Game", "Messages", 12, 4, 4, "messages (\"Gained object\", \"Empty.\", \"Locked.\", \"You died!\")",
              { 0x556BAA } },
    /* 17 */ { "Text.Game", "PlaceNames", 12, -1, 1, "place names (\"The Eastern River\", \"Madrigal Inn\")",
              { 0x6721CF } },
    /* 18 */ { "Text.Game", "CombatNumbers", 12, 5, -1, "damage numbers in a fight", { 0 } },
};
#define TS_N ((int)(sizeof ts_key / sizeof ts_key[0]))
static int g_tsVal[TS_N];          /* as read from the ini: <= 0 = unset */
static int g_tsSet[TS_N];          /* places given the item's size by ts_groups (0 = it follows its kind) */

static int ts_clamp(int v) { return v < FP_PCT_MIN ? FP_PCT_MIN : v > FP_PCT_MAX ? FP_PCT_MAX : v; }
/* the size of setting i: its own value, else its parent's, ...; 200 if nothing is set */
static int ts_pct(int i) {
    for (; i >= 0; i = ts_key[i].parent) if (g_tsVal[i] > 0) return ts_clamp(g_tsVal[i]);
    return 200;
}
/* the size of every font kind (kindPct[TS_KINDS]); a kind without a setting gets Size */
static void ts_kinds(int* kindPct) {
    int i, k;
    for (k = 0; k < TS_KINDS; k++) kindPct[k] = ts_pct(0);
    for (i = 0; i < TS_N; i++) if (ts_key[i].kind >= 0) kindPct[(int)ts_key[i].kind] = ts_pct(i);
}
/* set every place of item g to pct (0 = back to the kind's). Returns the number of places set. */
static int fp_group_set(int g, int pct) {
    int k, n = 0;
    if (g < 0 || g >= TS_N) return 0;
    for (k = 0; k < FP_GMAX && ts_key[g].place[k]; k++) if (fp_set_pct(ts_key[g].place[k], pct) >= 0) n++;
    return n;
}
/* the items' places: an item whose size differs from its places' kind gets that size on its
   places (their own font copies), the others follow the kind (no copies needed). Returns the
   number of places set. */
static int ts_groups(const int* kindPct) {
    int i, n = 0;
    for (i = 0; i < TS_N; i++) {
        int r;
        g_tsSet[i] = 0;
        if (ts_key[i].pk < 0 || !ts_key[i].place[0]) continue;
        r = ts_pct(i);
        if (r != kindPct[(int)ts_key[i].pk]) { g_tsSet[i] = fp_group_set(i, r); n += g_tsSet[i]; }
    }
    return n;
}
/* "Text.Menus/MainMenu" -> its index, -1 if none (case-insensitive); for the harness and a later settings menu */
__attribute__((unused)) static int ts_find(const char* name) {
    int i;
    for (i = 0; i < TS_N; i++) {
        const char* a = ts_key[i].section; const char* b = name; int ok = 1;
        for (; *a; a++, b++) if ((*a | 0x20) != (*b | 0x20)) { ok = 0; break; }
        if (!ok || *b != '/') continue;
        for (a = ts_key[i].key, b++; *a || *b; a++, b++) if ((*a | 0x20) != (*b | 0x20)) { ok = 0; break; }
        if (ok) return i;
    }
    return -1;
}

/* the largest place size that is set (0 if none): the font page scale must cover it */
static int fp_max_pct(void) {
    int i, m = 0;
    for (i = 0; i < FP_NSITE; i++) if (g_fpPct[i] > m) m = g_fpPct[i];
    for (i = 0; i < FP_NLOAD; i++) if (g_fpLoadPct[i] > m) m = g_fpLoadPct[i];
    return m;
}

/* the largest size of the places that draw cursor tooltips and the text in
   them (0x555140: hbut 5567BE / 556950 / 556B33 / 556C88, mgrn 556C6D, mred
   556BAA), at least tipKind ([Text] Tooltips): the tooltip box rules in
   ui_layout.inc (g_gts) are upper bounds that must allow the largest box */
static int fp_tip_pct(int tipKind) {
    static const DWORD tips[] = { 0x5567BE, 0x556950, 0x556B33, 0x556C88, 0x556C6D, 0x556BAA };
    int i, j, m = tipKind;
    for (j = 0; j < (int)(sizeof tips / sizeof tips[0]); j++)
        for (i = 0; i < FP_NSITE; i++)
            if (fp_site[i].site == tips[j] && g_fpPct[i] > m) m = g_fpPct[i];
    return m;
}

/* the letter-spacing setter ran on an original: the copies follow it */
static void fp_spacing(DWORD src, int value) {
    int i;
    for (i = 0; i < FP_NSITE; i++)
        if (g_fpCopy[i].obj && g_fpCopy[i].src == src) {
            unsigned char* o = TX_PTR(g_fpCopy[i].obj);
            *(int*)(o + 0x2C) = tx_spacing(o, value);
        }
}
