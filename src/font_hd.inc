/* font_hd.inc - HD font pages in the game (docs/HD_FONTS_PLAN.md, steps 3-4).
 *
 * Larger font pages (on whenever [Fonts] redraws a font; was [Debug]
 * FontPages=1): the four font texture slots 0x4E-0x51 are created k times
 * larger (k = 4, or [Debug] FontPageScale) with an A8R8G8B8 surface (renderer code 0x80)
 * instead of A1R5G5B5 (code 4); each upload of the game's 256x256 page of
 * 8-bit indices is replaced by a k-times enlarged copy (nearest), which the
 * renderer converts itself (index 0 -> transparent). Texture coordinates
 * are fractions (texel / 256), so the game needs no change. No code is
 * patched: only the arguments of two renderer calls are swapped
 * (CreateTexture's descriptor, OverWriteTexture's pixels).
 *
 * Redraw of phil/hbut ([Fonts] Journal / Tooltips; was [Debug] FontRedraw,
 * step 4): after an upload of 0x4E-0x50 has
 * been converted (at the next renderer call) the page is composed again by
 * the DLL - fpg_base() gives exactly what the renderer made of the indices
 * (step 3: 100% of texels) - with the phil/hbut glyphs redrawn from a
 * TrueType face, written into the slot's surface, and the renderer's
 * video-memory copy refreshed the way the renderer does it itself:
 * IDirect3DDevice7::Load(cache texture, surface).
 *
 * How the renderer holds textures (found 2026-11-22, RENDERER_API): the
 * slot's surface (record +0x54) is in system memory; drawing uses a cached
 * video-memory texture [obj + 0x1F54 + 4*i] + 0x3C, i = record +0x60 (set at
 * EndTextureCreation), refreshed inside OverWriteTexture by
 * 0x1000E7F0 = device->Load(cache texture, 0, surface, 0, 0). Two attempts
 * that failed: (1) writing the surface after the upload without Load: the
 * screen kept the old letters; (2) handing the renderer a composed A8R8G8B8
 * page as the upload's source (code 0x80): the renderer's own conversion
 * reads that source as bytes A,R,G,B (red and blue end up shifted: wrong
 * colours everywhere) and keeps only 1-bit alpha.
 *
 * Renderer-build check: the original renderer's CreateTexture and
 * OverWriteTexture bodies (D3d7Lib_orig.dll +0xE930, +0xF380) and the slot
 * record layout are known only for the shipped build; the bytes are
 * checked once and the feature stays off on any other build.
 *
 * Needs: g_fpgK (set from the ini), g_cfgLog, g_orig, g_dir, readable(),
 * LOG/flush, fp_slot() (font_probe.inc), IDX_* (stubs.h), font_pages.inc.
 */
#define FPG_FORMAT_A8R8G8B8 0x80

static int   g_fpgChecked;          /* renderer-build check: 0 not yet, 1 ok, -1 failed */
static DWORD g_fpgDesc[4][16];      /* our copies of the create descriptors */
static int   g_fpgCreated[4];       /* slot created by us at k (0 = not ours) */
static DWORD g_fpgUploads, g_fpgPassed;
static DWORD g_fpgPalPtr;           /* the game's palette (create descriptor +0x34) */
static unsigned char g_fpgBuf[FPG_SIDE * FPG_MAXK * FPG_SIDE * FPG_MAXK];   /* enlarged indices for one upload */
static DWORD g_fpgPage[FPG_SIDE * FPG_MAXK * FPG_SIDE * FPG_MAXK];   /* page composed for the redraw */
static unsigned char g_fpgIdx[4][FPG_SIDE * FPG_SIDE];     /* the game's indices of 4E-51, last upload */
static unsigned char g_fpgPal[768];
static int   g_fpgPend[4], g_fpgAny;
static int   g_fpgSeen, g_fpgWait;  /* slots uploaded at least once (bits); ticks waited for the rest */

/* step 4 settings and state */
static int    g_fpgRedraw;          /* bit 0 phil, bit 1 hbut ([Fonts] Journal / Tooltips) */
static char   g_fpgFace[64] = "Cardo";
static int    g_fpgWeight = 700;
static char   g_fpgFile[260] = "SoulbringerModern\\fonts\\Cardo-Bold.ttf";
#define FPG_OWN_N 5                         /* phil, hbut, magc, mgrn, mred (td_file 8-12) */
static FpgRec g_fpgRec[FPG_OWN_N][256];
static int    g_fpgNRec[FPG_OWN_N] = { -1, -1, -1, -1, -1 };
static FpgStyle g_fpgStyle[FPG_OWN_N];
static int    g_fpgStyled;          /* bit per font: colours found */
static int    g_fpgEm[2];           /* em size per font, 0 = not fitted yet (in-rectangle redraw, phil/hbut) */
static const char* const g_fpgFontName[FPG_OWN_N] = { "phil", "hbut", "magc", "mgrn", "mred" };

/* [Fonts] Menu: the menu font magc (main menu, save list, quit dialog) redrawn from an
   uncial face into its own cells; 0 = the game's letters, 1 = Eagle Lake (as Soulbringer Uncial:
   lining figures), 2 = MedievalSharp,
   3 = [Debug] MenuFontFace / MenuFontFile / MenuFontWeight (a private face) */
static int    g_fpgMenu;
/* [Fonts] Names: the name/message fonts mgrn (green: names under the cursor, "Gained object: ...")
   and mred (red: "Locked.", "Empty.") - the same letters in two colours - redrawn into their own
   cells from one face: 1 = Text (bundled SoulbringerText-Regular.ttf, family "Soulbringer Text",
   an upscaled outline version of the game's font, OFL), 2 = Custom ([Debug] NameFontFace / NameFontFile /
   NameFontWeight, e.g. Procopius, private), 0 = the game's letters */
static int    g_fpgName;
static int    g_fpgJournal;         /* [Fonts] Journal (g_cfgJournalFont): 1 = phil from Soulbringer Text, 2 Cardo, 3 the game's */
static int    g_fpgTip;             /* [Fonts] Tooltips (g_cfgTipFont): 1 = hbut from Soulbringer Text, 2 Cardo, 3 the game's */
static char   g_fpgNameFace[64] = "Soulbringer Text";
static int    g_fpgNameWeight = 300;
static char   g_fpgNameFile[260] = "SoulbringerModern\\fonts\\SoulbringerText-Regular.ttf";
static char   g_fpgMenuFace[64] = "Soulbringer Uncial";
static int    g_fpgMenuWeight = 400;
static char   g_fpgMenuFile[260] = "SoulbringerModern\\fonts\\SoulbringerUncial-Regular.ttf";

/* step 5: own metrics (with the text hook): new cells for phil/hbut/magc, see font_pages.inc */
#define FPG_FILE_PHIL 8                     /* index in td_file[] (hbut 9, magc 10) */
static int    g_fpgOwn;                     /* bit per font (0 phil, 1 hbut, 2 magc): drawn into its own cells */
static int    g_fpgOwnCfg = 1;              /* own glyph cells for the redrawn text and menu fonts (always on since 2026-11-28; was [Debug] FontOwnMetrics) */
static FpgNewCell g_fpgCell[FPG_OWN_N][256];
static int    g_fpgNCell[FPG_OWN_N];
static TxRec  g_fpgOrig[FPG_OWN_N][TX_MAXR];   /* the original file records (g_txFile now holds the new ones) */
static int    g_fpgOrigN[FPG_OWN_N];
static int    g_fpgOwnEm[FPG_OWN_N], g_fpgSpaceW[FPG_OWN_N];

static int fpg_renderer_ok(void) {
    static const unsigned char create[11] = { 0x81, 0xEC, 0x60, 0x02, 0x00, 0x00, 0x53, 0x8B, 0x9C, 0x24, 0x68 };
    static const unsigned char upload[11] = { 0x83, 0xEC, 0x1C, 0x55, 0x8B, 0x6C, 0x24, 0x24, 0x56, 0x8B, 0xF1 };
    const unsigned char* o = (const unsigned char*)g_orig;
    int i;
    if (g_fpgChecked) return g_fpgChecked > 0;
    g_fpgChecked = -1;
    if (!o || !readable(o + 0xE930, sizeof create) || !readable(o + 0xF380, sizeof upload)) {
        LOG("font pages: renderer code not readable - off\r\n"); return 0;
    }
    for (i = 0; i < (int)sizeof create; i++) if (o[0xE930 + i] != create[i]) { LOG("font pages: unknown renderer build (CreateTexture) - off\r\n"); return 0; }
    for (i = 0; i < (int)sizeof upload; i++) if (o[0xF380 + i] != upload[i]) { LOG("font pages: unknown renderer build (OverWriteTexture) - off\r\n"); return 0; }
    g_fpgChecked = 1;
    LOG("font pages: on, font slots 4E-51 at %ux (%ux%u A8R8G8B8)\r\n", g_fpgK, FPG_SIDE * g_fpgK, FPG_SIDE * g_fpgK);
    return 1;
}

/* ---------------------------------------------------------------- GDI (step 4)
 * Faces: Cardo Bold (700, SoulbringerModern\fonts\Cardo-Bold.ttf) and the
 * other bundled fonts, or a file of your own ([Fonts] Names / Menu), added for this process only (AddFontResourceExA,
 * FR_PRIVATE) while drawing, removed afterwards; if GDI gives another face
 * the Windows old-style serifs are tried: Constantia, Palatino Linotype,
 * Book Antiqua. Glyphs: GetGlyphOutlineA(GGO_GRAY8_BITMAP), 65 levels.
 * Loaded with GetProcAddress (import table unchanged). */
typedef void* HFONT_;
typedef struct { unsigned bbx, bby; int ox, oy; short incx, incy; } GLYPHMETRICS_;
typedef struct { unsigned short f1; short v1; unsigned short f2; short v2; unsigned short f3; short v3; unsigned short f4; short v4; } MAT2_;

static int    (WINAPI *fr_AddFontResourceExA)(const char*, DWORD, void*);
static BOOL   (WINAPI *fr_RemoveFontResourceExA)(const char*, DWORD, void*);
static HFONT_ (WINAPI *fr_CreateFontA)(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, const char*);
static void*  (WINAPI *fr_CreateCompatibleDC)(void*);
static void*  (WINAPI *fr_SelectObject)(void*, void*);
static BOOL   (WINAPI *fr_DeleteObject)(void*);
static BOOL   (WINAPI *fr_DeleteDC)(void*);
static DWORD  (WINAPI *fr_GetGlyphOutlineA)(void*, unsigned, unsigned, GLYPHMETRICS_*, DWORD, void*, const MAT2_*);
static int    (WINAPI *fr_GetTextFaceA)(void*, int, char*);
static DWORD  (WINAPI *fr_GetGlyphIndicesA)(void*, const char*, int, unsigned short*, DWORD);   /* optional */
static int    g_frApi;              /* 0 not loaded, 1 ok, -1 failed */

#define FR_GET(m, n) (*(FARPROC*)&fr_##n = GetProcAddress(m, #n))
static int fr_load(void) {
    HMODULE g;
    if (g_frApi) return g_frApi > 0;
    g_frApi = -1;
    g = LoadLibraryA("gdi32.dll");
    if (!g || !FR_GET(g, AddFontResourceExA) || !FR_GET(g, RemoveFontResourceExA) || !FR_GET(g, CreateFontA) ||
        !FR_GET(g, CreateCompatibleDC) || !FR_GET(g, SelectObject) || !FR_GET(g, DeleteObject) || !FR_GET(g, DeleteDC) ||
        !FR_GET(g, GetGlyphOutlineA) || !FR_GET(g, GetTextFaceA)) { LOG("font redraw: GDI not available - off\r\n"); return 0; }
    FR_GET(g, GetGlyphIndicesA);   /* missing glyphs (else GDI draws the default character) */
    g_frApi = 1;
    return 1;
}

/* GDI face at one em size; re-created when the size changes */
typedef struct { void* dc; HFONT_ font; void* old; int em; const char* face; int weight; } FrCtx;
static const MAT2_ fr_identity = { 0, 1, 0, 0, 0, 0, 0, 1 };
static int fr_size(FrCtx* c, int em) {
    if (c->font && c->em == em) return 1;
    if (c->font) { fr_SelectObject(c->dc, c->old); fr_DeleteObject(c->font); c->font = 0; }
    /* negative height = em size in pixels; ANSI_CHARSET 0, OUT_TT_ONLY_PRECIS 7, ANTIALIASED_QUALITY 4 */
    c->font = fr_CreateFontA(-em, 0, 0, 0, c->weight ? c->weight : g_fpgWeight, 0, 0, 0, 0, 7, 0, 4, 0, c->face);
    if (!c->font) return 0;
    c->old = fr_SelectObject(c->dc, c->font);
    c->em = em;
    return 1;
}
static int fr_metrics(void* ctx, int em, int code, int* top, int* height) {
    FrCtx* c = (FrCtx*)ctx; GLYPHMETRICS_ gm;
    if (!fr_size(c, em)) return 0;
    if (fr_GetGlyphOutlineA(c->dc, (unsigned)code, 0 /*GGO_METRICS*/, &gm, 0, 0, &fr_identity) == 0xFFFFFFFFu) return 0;
    *top = gm.oy; *height = (int)gm.bby;
    return 1;
}
/* coverage 0..255 of one glyph (tight box); returns 0 if none */
static unsigned char g_frRaw[FPG_MAXW * FPG_MAXW * 2];
static int fr_glyph(FrCtx* c, int code, unsigned char* cov, int* cw, int* ch, int* top) {
    GLYPHMETRICS_ gm; DWORD n; int x, y, pitch;
    n = fr_GetGlyphOutlineA(c->dc, (unsigned)code, 6 /*GGO_GRAY8_BITMAP*/, &gm, 0, 0, &fr_identity);
    if (n == 0xFFFFFFFFu || n == 0 || n > sizeof g_frRaw) return 0;
    if (gm.bbx > FPG_MAXW || gm.bby > FPG_MAXW) return 0;
    if (fr_GetGlyphOutlineA(c->dc, (unsigned)code, 6, &gm, n, g_frRaw, &fr_identity) == 0xFFFFFFFFu) return 0;
    pitch = ((int)gm.bbx + 3) & ~3;
    for (y = 0; y < (int)gm.bby; y++)
        for (x = 0; x < (int)gm.bbx; x++) {
            int v = g_frRaw[y * pitch + x];
            cov[y * gm.bbx + x] = (unsigned char)(v >= 64 ? 255 : v * 4);
        }
    *cw = (int)gm.bbx; *ch = (int)gm.bby; *top = gm.oy;
    return 1;
}
/* the configured face if GDI really gives it, else a Windows old-style serif (journal/tooltips)
   or the fallbacks given (interface font) */
static const char* const fr_serifs[3] = { "Constantia", "Palatino Linotype", "Book Antiqua" };
static int fr_open_face(FrCtx* c, const char* want, int weight, const char* const* fallback, int nfb) {
    char got[64]; int i;
    c->dc = fr_CreateCompatibleDC(0); c->font = 0; c->em = 0; c->face = want; c->weight = weight;
    if (!c->dc) return 0;
    for (i = -1; i < nfb; i++) {
        int a, same;
        c->face = i < 0 ? want : fallback[i];
        if (c->font) { fr_SelectObject(c->dc, c->old); fr_DeleteObject(c->font); c->font = 0; }
        if (!fr_size(c, 24)) continue;
        got[0] = 0; fr_GetTextFaceA(c->dc, sizeof got, got);
        for (a = 0, same = 1; c->face[a] || got[a]; a++) if ((c->face[a] | 0x20) != (got[a] | 0x20)) { same = 0; break; }
        LOG("font redraw: face \"%s\" -> GDI gave \"%s\"%s\r\n", c->face, got, same ? "" : " (not it)");
        if (same) return 1;
    }
    return 0;
}
static int fr_open(FrCtx* c) { return fr_open_face(c, g_fpgFace, g_fpgWeight, fr_serifs, 3); }
/* the face of own-metrics font f: Cardo (phil, hbut) or the menu face (magc: no stand-in, an
   uncial has no Windows equivalent - the game's letters stay); its file, relative to the game folder */
/* hbut has exactly mgrn's letters (all 191 pixel masks equal), phil is the same typeface one pixel larger:
   [Fonts] Tooltips/Journal=SoulbringerText draw them from the bundled Soulbringer Text */
#define FR_MGRN_FACE "Soulbringer Text"
#define FR_MGRN_FILE "SoulbringerModern\\fonts\\SoulbringerText-Regular.ttf"
static int fr_open_font(FrCtx* c, int f) {
    if ((f == 1 && g_fpgTip == 1) || (f == 0 && g_fpgJournal == 1)) return fr_open_face(c, FR_MGRN_FACE, 300, 0, 0);
    return f < 2 ? fr_open(c) : f == 2 ? fr_open_face(c, g_fpgMenuFace, g_fpgMenuWeight, 0, 0) : fr_open_face(c, g_fpgNameFace, g_fpgNameWeight, 0, 0);
}
static const char* fr_file_of(int f) {
    if ((f == 1 && g_fpgTip == 1) || (f == 0 && g_fpgJournal == 1)) return FR_MGRN_FILE;
    return f < 2 ? g_fpgFile : f == 2 ? g_fpgMenuFile : g_fpgNameFile;
}
static void fr_close(FrCtx* c) {
    if (c->font) { fr_SelectObject(c->dc, c->old); fr_DeleteObject(c->font); }
    if (c->dc) fr_DeleteDC(c->dc);
    c->font = 0; c->dc = 0;
}

static void fpg_load_cells(void) {
    int f;
    for (f = 0; f < FPG_OWN_N; f++) {
        static unsigned char buf[8192];
        char path[700]; HANDLE h; DWORD got = 0;
        if (g_fpgNRec[f] >= 0) continue;
        g_fpgNRec[f] = 0;
        wsprintfA(path, "%sdata\\%s.fnt", g_dir, g_fpgFontName[f]);
        h = CreateFileA(path, 0x80000000 /*GENERIC_READ*/, 1, 0, 3 /*OPEN_EXISTING*/, 0x80, 0);
        if (h == (HANDLE)-1) { LOG("font redraw: %s not found\r\n", path); continue; }
        if (!ReadFile(h, buf, sizeof buf, &got, 0)) got = 0;
        CloseHandle(h);
        g_fpgNRec[f] = fpg_read_fnt(buf, (int)got, g_fpgRec[f], 256);
    }
}

/* colours of each font from its original glyphs: the first page uploaded so far (0x4E-0x51,
   the game's indices g_fpgIdx) that holds some of them (for phil/hbut 0x4E, which gives the
   same colours as all three: checked offline; magc: 24 glyphs on 0x4E, 116 on 0x4F, 51 on 0x50) */
static void fpg_make_styles(const unsigned char* pal) {
    static unsigned char tmp[FPG_SIDE * FPG_SIDE];
    int f, i, x, y, s;
    for (f = 0; f < FPG_OWN_N; f++) {
        DWORD bright, dark, darkest;
        FpgCell whole = { 0, 0, FPG_SIDE, FPG_SIDE, 0 };
        int any = 0;
        if (g_fpgStyled & (1 << f)) continue;
        for (s = 0; s < 4 && !any; s++) {
            if (!(g_fpgSeen & (1 << s))) continue;
            memset(tmp, 0, sizeof tmp);
            for (i = 0; i < g_fpgNRec[f]; i++) {
                const FpgRec* r = &g_fpgRec[f][i];
                if (r->slot != 0x4E + s) continue;
                any = 1;
                for (y = 0; y < r->c.h; y++) for (x = 0; x < r->c.w; x++)
                    tmp[(r->c.v + y) * FPG_SIDE + r->c.u + x] = g_fpgIdx[s][(r->c.v + y) * FPG_SIDE + r->c.u + x];
            }
        }
        if (!any) continue;
        s--;
        fpg_colours(tmp, pal, &whole, 1, &bright, &dark, &darkest);
        if (f == 0) { g_fpgStyle[f].fill = darkest; g_fpgStyle[f].edge = dark; g_fpgStyle[f].edgePx = 0; g_fpgStyle[f].edgeAlpha = 0; }
        else if (f == 1) { g_fpgStyle[f].fill = bright; g_fpgStyle[f].edge = dark; g_fpgStyle[f].edgePx = (g_fpgK + 1) / 2; g_fpgStyle[f].edgeAlpha = 220; }
        else {   /* magc: yellow letters whose strokes fade into olive/brown at the edges; mgrn/mred: light green /
                    red letters fading to dark: a thin soft edge */
            g_fpgStyle[f].fill = bright; g_fpgStyle[f].edge = darkest; g_fpgStyle[f].edgePx = g_fpgK > 1 ? g_fpgK / 2 : 1; g_fpgStyle[f].edgeAlpha = 200;
            /* magc (menus, conversation): the uncial letters fill their advance, so making room for the
               outline condensed them by one unit (k px) each and the text looked letter-spaced
               ("r e f r e s h"); the outline is nearly black on the dark backgrounds, so cut it instead */
            g_fpgStyle[f].edgeClip = f == 2;
        }
        LOG("font redraw: %s: %d glyphs, fill %06X, outline %06X x %d px (colours from slot %02X)\r\n", g_fpgFontName[f], g_fpgNRec[f],
            g_fpgStyle[f].fill, g_fpgStyle[f].edge, g_fpgStyle[f].edgePx, 0x4E + s);
        g_fpgStyled |= 1 << f;
    }
}

/* ---------------------------------------------------------------- interface fonts ([Fonts] Interface)
 * 16xY / 16xB / 16xX (options menu, stats page, inventory numbers, HUD column labels, combo
 * screen) redrawn in layers from a bold sans (default Montserrat ExtraBold, shipped in
 * SoulbringerModern\fonts\ under the SIL OFL) into their original rectangles: the game's
 * layout and spacing are unchanged, only the letters are new (font_pages.inc,
 * fpg_glyph_layers / fpg_ui_colours / fpg_ui_em). 0 = the game's pixel font. */
#define FPG_UI_N 3
static const char* const g_fpgUiName[FPG_UI_N] = { "16xY", "16xB", "16xX" };
static char   g_fpgUiFace[64] = "Montserrat ExtraBold";
static int    g_fpgUiWeight = 800;
static char   g_fpgUiFile[260] = "SoulbringerModern\\fonts\\Montserrat-ExtraBold.ttf";
static FpgRec g_fpgUiRec[FPG_UI_N][256];
static int    g_fpgUiNRec[FPG_UI_N] = { -1, -1, -1 };
static int    g_fpgUiEm, g_fpgUiFailed;
static FpgLayers g_fpgUiL[FPG_UI_N];         /* colours per font, kept once found */
static int    g_fpgUiHave[FPG_UI_N];
static const char* const fr_sans[3] = { "Arial Black", "Segoe UI Black", "Tahoma" };
static __attribute__((noinline)) void fpg_redraw_ui(int slot) {
    static unsigned char cov[FPG_MAXW * FPG_MAXW];
    const unsigned char* pages[4];
    char path[700];
    FrCtx c;
    int f, i, added, drawn = 0, kept = 0, refused = 0, px = g_fpgK > 1 ? (g_fpgK + 1) / 2 : 1;
    if (g_fpgUiFailed || !fr_load()) return;
    for (f = 0; f < FPG_UI_N; f++) {
        static unsigned char buf[8192];
        HANDLE h; DWORD got = 0;
        if (g_fpgUiNRec[f] >= 0) continue;
        g_fpgUiNRec[f] = 0;
        wsprintfA(path, "%sdata\\%s.fnt", g_dir, g_fpgUiName[f]);
        h = CreateFileA(path, 0x80000000 /*GENERIC_READ*/, 1, 0, 3 /*OPEN_EXISTING*/, 0x80, 0);
        if (h == (HANDLE)-1) { LOG("interface font: %s not found\r\n", path); continue; }
        if (!ReadFile(h, buf, sizeof buf, &got, 0)) got = 0;
        CloseHandle(h);
        g_fpgUiNRec[f] = fpg_read_fnt(buf, (int)got, g_fpgUiRec[f], 256);
    }
    for (i = 0; i < 4; i++) pages[i] = g_fpgIdx[i];          /* the game's last upload of each slot (0 if none yet) */
    wsprintfA(path, "%s%s", g_dir, g_fpgUiFile);
    added = fr_AddFontResourceExA(path, 0x10 /*FR_PRIVATE*/, 0);
    if (g_fpgUploads <= 1) LOG("interface font: %s: %s\r\n", path, added ? "loaded for this process" : "NOT loaded (missing?)");
    if (!fr_open_face(&c, g_fpgUiFace, g_fpgUiWeight, fr_sans, 3)) {
        LOG("interface font: no usable face - the game's pixel font stays\r\n");
        fr_close(&c); if (added) fr_RemoveFontResourceExA(path, 0x10, 0);
        g_fpgUiFailed = 1; return;
    }
    if (!g_fpgUiEm && g_fpgUiNRec[0] > 0) {
        g_fpgUiEm = fpg_ui_em(g_fpgUiRec[0], g_fpgUiNRec[0], g_fpgK, fr_metrics, &c, 2 * px);
        LOG("interface font: letters at %d px em (rings %d + %d px, shade %d px, page scale %d)\r\n", g_fpgUiEm, px, px, px, g_fpgK);
    }
    if (g_fpgUiEm && fr_size(&c, g_fpgUiEm))
        for (f = 0; f < FPG_UI_N; f++) {
            FpgLayers L;
            if (g_fpgUiNRec[f] <= 0) continue;
            if (!g_fpgUiHave[f]) {                            /* colours from the font's letters A-Z (all on one page) */
                fpg_ui_colours(pages, g_fpgPal, g_fpgUiRec[f], g_fpgUiNRec[f], &L);
                if (L.face || L.cut) { g_fpgUiL[f] = L; g_fpgUiHave[f] = 1; }
            }
            if (!g_fpgUiHave[f]) continue;                    /* no letters of this font uploaded yet */
            L = g_fpgUiL[f];
            L.linePx = px; L.outerPx = px; L.shadePx = px;
            for (i = 0; i < g_fpgUiNRec[f]; i++) {
                const FpgRec* r = &g_fpgUiRec[f][i];
                FpgGlyph g; int rc;
                if (r->slot != slot || r->code < 33) continue;
                if (!fr_glyph(&c, fpg_ui_code(r->code), cov, &g.cw, &g.ch, &g.top)) { kept++; continue; }
                g.cov = cov; g.left1 = 0;
                rc = fpg_glyph_layers(g_fpgPage, g_fpgK, &r->c, &g, &L);
                if (rc == 1) drawn++; else if (rc == 2) kept++; else refused++;
            }
            if (slot == 0x50 && g_fpgUploads <= 8)
                LOG("interface font: %s face %06X shade %06X line %06X outer %06X%s\r\n", g_fpgUiName[f], L.face, L.shade, L.line, L.outer, L.cut ? " (cut out)" : "");
        }
    fr_close(&c);
    if (added) fr_RemoveFontResourceExA(path, 0x10, 0);
    LOG("interface font: slot %02X: %d glyphs redrawn, %d kept, %d refused\r\n", slot, drawn, kept, refused);
}

/* ---------------------------------------------------------------- combat numbers ([Fonts] Numbers)
 * numr (red damage numbers "-16") and numg (green, a miss "0") redrawn from the interface face
 * (Montserrat ExtraBold) into their original rectangles: yellow / green fill with the game's
 * outline colour, one texel wide (font_pages.inc fpg_num_em / fpg_num_base, fpg_glyph). */
#define FPG_NUM_N 2
static const char* const g_fpgNumName[FPG_NUM_N] = { "numr", "numg" };
static FpgRec   g_fpgNumRec[FPG_NUM_N][32];
static int      g_fpgNumNRec[FPG_NUM_N] = { -1, -1 };
static int      g_fpgNumEm, g_fpgNumBase = -1, g_fpgNumFailed, g_fpgNumHave[FPG_NUM_N];
static FpgStyle g_fpgNumStyle[FPG_NUM_N];
static __attribute__((noinline)) void fpg_redraw_num(int slot) {
    static unsigned char cov[FPG_MAXW * FPG_MAXW];
    char path[700];
    FrCtx c;
    int f, i, added, drawn = 0, kept = 0, refused = 0;
    if (g_fpgNumFailed || !fr_load()) return;
    for (f = 0; f < FPG_NUM_N; f++) {
        static unsigned char buf[2048];
        HANDLE h; DWORD got = 0;
        if (g_fpgNumNRec[f] >= 0) continue;
        g_fpgNumNRec[f] = 0;
        wsprintfA(path, "%sdata\\%s.fnt", g_dir, g_fpgNumName[f]);
        h = CreateFileA(path, 0x80000000 /*GENERIC_READ*/, 1, 0, 3 /*OPEN_EXISTING*/, 0x80, 0);
        if (h == (HANDLE)-1) { LOG("number font: %s not found\r\n", path); continue; }
        if (!ReadFile(h, buf, sizeof buf, &got, 0)) got = 0;
        CloseHandle(h);
        g_fpgNumNRec[f] = fpg_read_fnt(buf, (int)got, g_fpgNumRec[f], 32);
    }
    for (f = 0; f < FPG_NUM_N; f++) {                 /* colours from the font's digits on their page, once uploaded */
        static FpgCell cells[32];
        int n = 0, s = -1; DWORD fill, edge;
        if (g_fpgNumHave[f] || g_fpgNumNRec[f] <= 0) continue;
        for (i = 0; i < g_fpgNumNRec[f]; i++) if (g_fpgNumRec[f][i].code == '0') s = g_fpgNumRec[f][i].slot - 0x4E;
        if (s < 0 || !(g_fpgSeen & (1 << s))) continue;
        for (i = 0; i < g_fpgNumNRec[f]; i++)
            if (g_fpgNumRec[f][i].code >= '0' && g_fpgNumRec[f][i].code <= '9' && g_fpgNumRec[f][i].slot == 0x4E + s) cells[n++] = g_fpgNumRec[f][i].c;
        if (!fpg_num_colours(g_fpgIdx[s], g_fpgPal, cells, n, &fill, &edge)) continue;
        g_fpgNumStyle[f].fill = fill; g_fpgNumStyle[f].edge = edge;
        g_fpgNumStyle[f].edgePx = g_fpgK; g_fpgNumStyle[f].edgeAlpha = 255; g_fpgNumStyle[f].edgeClip = 0;
        g_fpgNumHave[f] = 1;
        LOG("number font: %s fill %06X outline %06X x %d px\r\n", g_fpgNumName[f], g_fpgNumStyle[f].fill & 0xFFFFFF, g_fpgNumStyle[f].edge & 0xFFFFFF, g_fpgK);
    }
    if (!g_fpgNumHave[0] && !g_fpgNumHave[1]) return;
    wsprintfA(path, "%s%s", g_dir, g_fpgUiFile);
    added = fr_AddFontResourceExA(path, 0x10 /*FR_PRIVATE*/, 0);
    if (!fr_open_face(&c, g_fpgUiFace, g_fpgUiWeight, fr_sans, 3)) {
        LOG("number font: no usable face - the game's digits stay\r\n");
        fr_close(&c); if (added) fr_RemoveFontResourceExA(path, 0x10, 0);
        g_fpgNumFailed = 1; return;
    }
    if (!g_fpgNumEm && g_fpgNumNRec[0] > 0) {
        g_fpgNumEm = fpg_num_em(g_fpgNumRec[0], g_fpgNumNRec[0], g_fpgK, fr_metrics, &c);
        g_fpgNumBase = fpg_num_base(g_fpgNumRec[0], g_fpgNumNRec[0]);
        LOG("number font: digits at %d px em, baseline row %d, outline %d px (page scale %d)\r\n", g_fpgNumEm, g_fpgNumBase, g_fpgK, g_fpgK);
    }
    if (g_fpgNumEm && g_fpgNumBase >= 0 && fr_size(&c, g_fpgNumEm))
        for (f = 0; f < FPG_NUM_N; f++) {
            if (!g_fpgNumHave[f]) continue;
            for (i = 0; i < g_fpgNumNRec[f]; i++) {
                const FpgRec* r = &g_fpgNumRec[f][i];
                FpgCell cell; FpgGlyph g; int rc;
                if (r->slot != slot || r->code < 33) continue;
                if (!fr_glyph(&c, fpg_num_code(r->code), cov, &g.cw, &g.ch, &g.top)) { kept++; continue; }
                cell = r->c; cell.base = g_fpgNumBase - r->yoff;
                g.cov = cov; g.left1 = 0;
                rc = fpg_glyph(g_fpgPage, g_fpgK, &cell, &g, &g_fpgNumStyle[f]);
                if (rc == 1) drawn++; else if (rc == 2) kept++; else refused++;
            }
        }
    fr_close(&c);
    if (added) fr_RemoveFontResourceExA(path, 0x10, 0);
    if (drawn || kept || refused) LOG("number font: slot %02X: %d glyphs redrawn, %d kept, %d refused\r\n", slot, drawn, kept, refused);
}

/* ---------------------------------------------------------------- credits font ([Fonts] Credits)
 * cred (the large white serif of the credits and of "GAME OVER") redrawn from the bundled Soulbringer
 * Credits (the user's upscale of it) into its original rectangles: 'H' as tall as the game's 'H'
 * (19 texels), the game's brightest colour as fill, anti-aliased, no outline (the game's letters have
 * grey edges, no outline). Codes the face does not have keep the game's letters. */
#define FR_CRED_FACE "Soulbringer Credits"
#define FR_CRED_FILE "SoulbringerModern\\fonts\\SoulbringerCredits-Regular.ttf"
static FpgRec   g_fpgCredRec[256];
static int      g_fpgCredNRec = -1, g_fpgCredEm, g_fpgCredFailed, g_fpgCredHave;
static FpgStyle g_fpgCredStyle;
static __attribute__((noinline)) void fpg_redraw_cred(int slot) {
    static unsigned char cov[FPG_MAXW * FPG_MAXW];
    char path[700];
    FrCtx c;
    int i, added, drawn = 0, kept = 0, refused = 0;
    if (g_fpgCredFailed || !fr_load()) return;
    if (g_fpgCredNRec < 0) {
        static unsigned char buf[16384];
        HANDLE h; DWORD got = 0;
        g_fpgCredNRec = 0;
        wsprintfA(path, "%sdata\\cred.fnt", g_dir);
        h = CreateFileA(path, 0x80000000 /*GENERIC_READ*/, 1, 0, 3 /*OPEN_EXISTING*/, 0x80, 0);
        if (h == (HANDLE)-1) { LOG("credits font: %s not found\r\n", path); g_fpgCredFailed = 1; return; }
        if (!ReadFile(h, buf, sizeof buf, &got, 0)) got = 0;
        CloseHandle(h);
        g_fpgCredNRec = fpg_read_fnt(buf, (int)got, g_fpgCredRec, 256);
    }
    if (g_fpgCredNRec <= 0) return;
    if (!g_fpgCredHave) {                              /* colour: the brighter of the two commonest, from the page of 'H' */
        static FpgCell cells[256];
        int n = 0, s = -1; DWORD fill, edge;
        for (i = 0; i < g_fpgCredNRec; i++) if (g_fpgCredRec[i].code == 'H') s = g_fpgCredRec[i].slot - 0x4E;
        if (s < 0 || !(g_fpgSeen & (1 << s))) return;
        for (i = 0; i < g_fpgCredNRec; i++) if (g_fpgCredRec[i].slot == 0x4E + s) cells[n++] = g_fpgCredRec[i].c;
        if (!fpg_num_colours(g_fpgIdx[s], g_fpgPal, cells, n, &fill, &edge)) return;
        g_fpgCredStyle.fill = fill; g_fpgCredStyle.edge = edge; g_fpgCredStyle.edgePx = 0; g_fpgCredStyle.edgeAlpha = 0; g_fpgCredStyle.edgeClip = 1;
        g_fpgCredHave = 1;
        LOG("credits font: fill %06X, no outline\r\n", fill & 0xFFFFFF);
    }
    wsprintfA(path, "%s%s", g_dir, FR_CRED_FILE);
    added = fr_AddFontResourceExA(path, 0x10 /*FR_PRIVATE*/, 0);
    if (!fr_open_face(&c, FR_CRED_FACE, 400, 0, 0)) {
        LOG("credits font: %s not usable - the game's letters stay\r\n", path);
        fr_close(&c); if (added) fr_RemoveFontResourceExA(path, 0x10, 0);
        g_fpgCredFailed = 1; return;
    }
    if (!g_fpgCredEm) {
        g_fpgCredEm = fpg_ui_em(g_fpgCredRec, g_fpgCredNRec, g_fpgK, fr_metrics, &c, 0);
        LOG("credits font: Soulbringer Credits at %d px em (page scale %d)\r\n", g_fpgCredEm, g_fpgK);
    }
    if (g_fpgCredEm && fr_size(&c, g_fpgCredEm))
        for (i = 0; i < g_fpgCredNRec; i++) {
            const FpgRec* r = &g_fpgCredRec[i];
            FpgGlyph g; int rc;
            if (r->slot != slot || r->code < 33) continue;
            if (fr_GetGlyphIndicesA) {                 /* not in the face: the game's letter stays */
                char s1[2]; unsigned short gi = 0;
                s1[0] = (char)(r->code < 256 ? r->code : r->code == 0x152 ? 0x8C : r->code == 0x153 ? 0x9C : '?'); s1[1] = 0;
                if (fr_GetGlyphIndicesA(c.dc, s1, 1, &gi, 1) != 0xFFFFFFFFu && gi == 0xFFFF) { kept++; continue; }
            }
            if (!fr_glyph(&c, r->code < 256 ? r->code : r->code == 0x152 ? 0x8C : r->code == 0x153 ? 0x9C : '?', cov, &g.cw, &g.ch, &g.top)) { kept++; continue; }
            g.cov = cov; g.left1 = 0;
            rc = fpg_glyph(g_fpgPage, g_fpgK, &r->c, &g, &g_fpgCredStyle);
            if (rc == 1) drawn++; else if (rc == 2) kept++; else refused++;
        }
    fr_close(&c);
    if (added) fr_RemoveFontResourceExA(path, 0x10, 0);
    if (drawn || kept || refused) LOG("credits font: slot %02X: %d glyphs redrawn, %d kept, %d refused\r\n", slot, drawn, kept, refused);
}

/* draws the phil/hbut/magc glyphs of one slot into g_fpgPage (already holding the enlarged page) */
static __attribute__((noinline)) void fpg_redraw_own(int f, int slot, int* drawn, int* kept, int* refused) {
    static unsigned char cov[FPG_MAXW * FPG_MAXW];
    char path[700];
    FrCtx c;
    int i, added;
    wsprintfA(path, "%s%s", g_dir, fr_file_of(f));
    added = fr_AddFontResourceExA(path, 0x10 /*FR_PRIVATE*/, 0);
    if (g_fpgUploads <= 1) LOG("font redraw: %s: %s: %s\r\n", g_fpgFontName[f], path, added ? "loaded for this process" : "NOT loaded (missing?)");
    if (fr_open_font(&c, f) && fr_size(&c, g_fpgOwnEm[f]))
        for (i = 0; i < g_fpgNCell[f]; i++) {
            const FpgNewCell* n = &g_fpgCell[f][i];
            FpgCell cell; FpgGlyph g; int rc;
            if (n->slot != slot) continue;
            if (!fr_glyph(&c, n->code, cov, &g.cw, &g.ch, &g.top)) { (*kept)++; continue; }
            cell.u = n->u; cell.v = n->v; cell.w = n->w; cell.h = n->h; cell.base = n->base;
            g.cov = cov; g.left1 = n->left + 1;
            rc = fpg_glyph(g_fpgPage, g_fpgK, &cell, &g, &g_fpgStyle[f]);
            if (rc == 1) (*drawn)++; else if (rc == 2) (*kept)++; else (*refused)++;
        }
    else LOG("font redraw: %s: face not usable at %d px - its new cells stay empty\r\n", g_fpgFontName[f], g_fpgOwnEm[f]);
    fr_close(&c);
    if (added) fr_RemoveFontResourceExA(path, 0x10, 0);
}
static __attribute__((noinline)) void fpg_redraw_page(int slot, const unsigned char* idx, const unsigned char* pal) {
    static unsigned char cov[FPG_MAXW * FPG_MAXW];
    char path[700];
    FrCtx c;
    int f, added, drawn = 0, kept = 0, refused = 0, side = FPG_SIDE * g_fpgK;
    (void)idx;
    if (g_cfgUiFont) fpg_redraw_ui(slot);
    if (g_cfgNumFont) fpg_redraw_num(slot);
    if (g_cfgCredFont) fpg_redraw_cred(slot);
    if (!g_fpgRedraw && !g_fpgOwn) return;
    if (!fr_load()) { g_fpgRedraw = 0; return; }
    fpg_load_cells();
    fpg_make_styles(pal);
    /* step 5: clear the old rectangles of every moved glyph first (a new cell of one font may lie
       on an old rectangle of another), then draw each font into its new cells */
    for (f = 0; f < FPG_OWN_N; f++) {
        int i, x, y;
        if (!(g_fpgOwn & (1 << f))) continue;
        for (i = 0; i < g_fpgNCell[f]; i++) {
            const TxRec* o = &g_fpgOrig[f][g_fpgCell[f][i].src];
            if (o->mat != slot) continue;
            for (y = o->v * g_fpgK; y < (o->v + o->h) * g_fpgK; y++)
                for (x = o->u * g_fpgK; x < (o->u + o->w) * g_fpgK; x++) g_fpgPage[y * side + x] = 0;
        }
    }
    for (f = 0; f < FPG_OWN_N; f++)
        if ((g_fpgOwn & (1 << f)) && (g_fpgStyled & (1 << f))) fpg_redraw_own(f, slot, &drawn, &kept, &refused);
    /* step 4: phil/hbut redrawn inside their original rectangles (without own metrics), each from its face
       (Cardo; Soulbringer Text with [Fonts] Tooltips/Journal=SoulbringerText) */
    for (f = 0; f < 2; f++) {
        int i;
        if (!(g_fpgRedraw & (1 << f)) || (g_fpgOwn & (1 << f)) || g_fpgNRec[f] <= 0 || !(g_fpgStyled & (1 << f))) continue;
        wsprintfA(path, "%s%s", g_dir, fr_file_of(f));
        added = fr_AddFontResourceExA(path, 0x10 /*FR_PRIVATE*/, 0);
        if (g_fpgUploads <= 1) LOG("font redraw: %s: %s: %s\r\n", g_fpgFontName[f], path, added ? "loaded for this process" : "NOT loaded (missing?)");
        if (!fr_open_font(&c, f)) {
            LOG("font redraw: %s: no usable face - not redrawn\r\n", g_fpgFontName[f]);
            fr_close(&c); if (added) fr_RemoveFontResourceExA(path, 0x10, 0);
            g_fpgRedraw &= ~(1 << f); continue;
        }
        if (!g_fpgEm[f]) {
            int capEm = 0;
            g_fpgEm[f] = fpg_fit(g_fpgRec[f], g_fpgNRec[f], g_fpgK, fr_metrics, &c, &capEm);
            LOG("font redraw: %s at %d px (the original cap height would be %d px)\r\n", g_fpgFontName[f], g_fpgEm[f], capEm);
        }
        if (g_fpgEm[f] && fr_size(&c, g_fpgEm[f]))
            for (i = 0; i < g_fpgNRec[f]; i++) {
                const FpgRec* r = &g_fpgRec[f][i];
                FpgGlyph g; int rc;
                if (r->slot != slot || r->code < 33) continue;
                if (!fr_glyph(&c, r->code, cov, &g.cw, &g.ch, &g.top)) { kept++; continue; }
                g.cov = cov; g.left1 = 0;
                rc = fpg_glyph(g_fpgPage, g_fpgK, &r->c, &g, &g_fpgStyle[f]);
                if (rc == 1) drawn++; else if (rc == 2) kept++; else refused++;
            }
        fr_close(&c);
        if (added) fr_RemoveFontResourceExA(path, 0x10, 0);
    }
    LOG("font redraw: slot %02X: %d glyphs redrawn, %d kept, %d refused\r\n", slot, drawn, kept, refused);
}

/* ---------------------------------------------------------------- the two renderer calls
 * frame as in hook_pre: frame[2..] = arguments. Called before the call is forwarded. */
static __attribute__((noinline)) void fpg_hook(int idx, DWORD* frame) {
    DWORD slot = frame[2];
    int s = (int)slot - 0x4E, k = g_fpgK;
    if (slot < 0x4E || slot > 0x51) return;
    if (!fpg_renderer_ok()) return;
    if (idx == IDX_CreateTexture) {
        const DWORD* d = (const DWORD*)frame[3];
        int x;
        if (!readable(d, 0x40)) return;
        /* only the descriptor the font pages are known to use: 256x256, 8-bit indices, A1R5G5B5, no size flags */
        if (d[1] != FPG_SIDE || d[2] != FPG_SIDE || d[3] != 1 || d[4] != 4 || (d[12] & 0xA0)) {
            LOG("font pages: slot %02X descriptor not as expected (%ux%u, codes %X/%X, flags %X) - left alone\r\n", slot, d[1], d[2], d[3], d[4], d[12]);
            g_fpgCreated[s] = 0;
            return;
        }
        for (x = 0; x < 16; x++) g_fpgDesc[s][x] = d[x];
        g_fpgDesc[s][1] = g_fpgDesc[s][2] = FPG_SIDE * k;
        g_fpgDesc[s][4] = FPG_FORMAT_A8R8G8B8;     /* surface; the source stays 8-bit indices (code 1) */
        g_fpgPalPtr = d[13];                       /* palette the game's indices refer to (desc+0x34) */
        frame[3] = (DWORD)g_fpgDesc[s];
        g_fpgCreated[s] = k;
        return;
    }
    if (idx == IDX_OverWriteTexture) {
        const unsigned char* rec = fp_slot(slot);
        const unsigned char* src = (const unsigned char*)frame[4];
        int side = FPG_SIDE * k, x, y;
        /* swap the pixels only if the renderer really holds a k-times 8-bit source for this slot,
           or it would read past the game's 256x256 buffer */
        if (!g_fpgCreated[s] || !rec || *(const DWORD*)rec != (DWORD)side || *(const DWORD*)(rec + 4) != (DWORD)side || rec[0xC] != 1) {
            if (g_fpgCreated[s]) LOG("font pages: slot %02X record not %ux%u - upload passed through\r\n", slot, side, side);
            g_fpgPassed++;
            return;
        }
        if (!readable(src, FPG_SIDE * FPG_SIDE)) { LOG("font pages: slot %02X pixels not readable\r\n", slot); return; }
        for (y = 0; y < side; y++) {
            const unsigned char* sr = src + (y / k) * FPG_SIDE;
            unsigned char* dr = g_fpgBuf + y * side;
            for (x = 0; x < side; x++) dr[x] = sr[x / k];
        }
        frame[4] = (DWORD)g_fpgBuf;
        g_fpgUploads++;
        if (g_cfgLog) LOG("font pages: slot %02X upload enlarged %ux (upload %u)\r\n", slot, k, g_fpgUploads);
        if ((g_fpgRedraw || g_fpgOwn || g_cfgUiFont || g_cfgNumFont || g_cfgCredFont) && (s < 3 || g_fpgOwn || g_cfgUiFont || g_cfgNumFont || g_cfgCredFont) && readable((const void*)g_fpgPalPtr, 768)) {   /* step 4: redraw once the renderer has converted it */
            memcpy(g_fpgIdx[s], src, FPG_SIDE * FPG_SIDE);
            memcpy(g_fpgPal, (const void*)g_fpgPalPtr, 768);
            g_fpgPend[s] = 1; g_fpgAny = 1; g_fpgSeen |= 1 << s;
        }
    }
}

/* ---------------------------------------------------------------- step 4: write the redrawn page
 * into the slot's surface, then refresh the renderer's video-memory copy
 * with IDirect3DDevice7::Load (device [orig+0x21FC8], vtable +0xAC), as
 * OverWriteTexture does itself (0x1000E8B5). */
typedef long (WINAPI *d3d_load_t)(void* dev, void* dst, void* dstPoint, void* src, void* srcRect, DWORD flags);
static __attribute__((noinline)) void fpg_write_page(int slot) {
    const unsigned char* o = (const unsigned char*)g_orig;
    const unsigned char* rec = fp_slot(slot);
    const unsigned char* obj = readable(o + 0x786CC, 4) ? *(const unsigned char* const*)(o + 0x786CC) : 0;
    void* surf = fp_surface(rec);
    unsigned char d[SD_SIZE];
    unsigned char* bits;
    int side = FPG_SIDE * g_fpgK, y, pitch, ci;
    long hr;
    if (!surf) { LOG("font redraw: slot %02X no surface\r\n", slot); return; }
    memset(d, 0, SD_SIZE); *(DWORD*)d = SD_SIZE;
    if (((dds_lock_t)FP_VT(surf, DDS7_Lock))(surf, 0, d, DDLOCK_WAIT, 0) != 0) { LOG("font redraw: slot %02X lock failed\r\n", slot); return; }
    pitch = *(int*)(d + SD_PITCH); bits = *(unsigned char**)(d + SD_SURFACE);
    if (*(DWORD*)(d + SD_WIDTH) != (DWORD)side || *(DWORD*)(d + SD_HEIGHT) != (DWORD)side || ((DWORD*)(d + SD_PF))[3] != 32 ||
        ((DWORD*)(d + SD_PF))[7] != 0xFF000000u || ((DWORD*)(d + SD_PF))[4] != 0x00FF0000u || ((DWORD*)(d + SD_PF))[6] != 0x000000FFu ||
        !bits || pitch < side * 4) {
        LOG("font redraw: slot %02X surface is not %ux%u A8R8G8B8 - left alone\r\n", slot, side, side);
        ((dds_unlock_t)FP_VT(surf, DDS7_Unlock))(surf, 0);
        return;
    }
    for (y = 0; y < side; y++) memcpy(bits + y * pitch, g_fpgPage + y * side, side * 4);
    ((dds_unlock_t)FP_VT(surf, DDS7_Unlock))(surf, 0);
    /* refresh the video-memory copy, if the slot has one */
    ci = *(const int*)(rec + 0x60);
    if (ci < 0) { LOG("font redraw: slot %02X written (no cached copy yet)\r\n", slot); return; }
    {
        void* dev = readable(o + 0x21FC8, 4) ? *(void* const*)(o + 0x21FC8) : 0;
        const unsigned char* entry = obj && ci < 0x400 && readable(obj + 0x1F54 + 4 * ci, 4) ? *(const unsigned char* const*)(obj + 0x1F54 + 4 * ci) : 0;
        void* tex = entry && readable(entry + 0x3C, 4) ? *(void* const*)(entry + 0x3C) : 0;
        if (!dev || !readable(dev, 4) || !readable(*(void* const*)dev, 0xB0) || !tex || !readable(tex, 4)) {
            LOG("font redraw: slot %02X cached copy %d not found (device %08X, texture %08X) - screen keeps the old page\r\n", slot, ci, (DWORD)dev, (DWORD)tex);
            return;
        }
        hr = ((d3d_load_t)(((void* const*)*(void* const*)dev)[0xAC / 4]))(dev, tex, 0, surf, 0, 0);
        LOG("font redraw: slot %02X written, cached copy %d refreshed (Load %08X)\r\n", slot, ci, (DWORD)hr);
    }
}

/* every renderer call while the larger font pages are on: redraw the pages uploaded by the previous call */
static void fpg_tick(void) {
    int s;
    if (!g_fpgAny) return;
    /* interface font: its colours come from its letters on page 0x50, the menu font's from 0x4E-0x50;
       the pages arrive one after another, so wait until all four were uploaded once (or 600 renderer
       calls, if one never is) */
    if ((g_cfgUiFont || g_cfgNumFont || g_cfgCredFont || (g_fpgOwn & ~3)) && g_fpgSeen != 0xF && ++g_fpgWait < 600) return;
    g_fpgAny = 0;
    if ((g_fpgRedraw || g_fpgOwn) && fr_load()) {
        /* a font's colours come from the first page holding its original letters; when an own-cells
           font gets them, every page seen so far is redrawn (its new cells may lie on any page) */
        int had = g_fpgStyled;
        fpg_load_cells();
        fpg_make_styles(g_fpgPal);
        if (g_fpgStyled & ~had & g_fpgOwn)
            for (s = 0; s < 4; s++) if (g_fpgSeen & (1 << s)) g_fpgPend[s] = 1;
    }
    for (s = 0; s < 4; s++) {
        if (!g_fpgPend[s]) continue;
        g_fpgPend[s] = 0;
        fpg_base(g_fpgIdx[s], g_fpgPal, g_fpgK, g_fpgPage);
        fpg_redraw_page(0x4E + s, g_fpgIdx[s], g_fpgPal);
        fpg_write_page(0x4E + s);
    }
    flush();
}

/* ---------------------------------------------------------------- step 5: own metrics
 * With the text hook and the redraw: phil/hbut get new cells
 * sized for the face (font_pages.inc: fpg_choose_em, fpg_new_cells,
 * fpg_pack), computed once in tx_install() from the font files and GDI
 * metrics. The text hook's tables then describe the new cells: g_txFile
 * holds them (the originals are kept here), the texel-fix keys know them,
 * and every loaded phil/hbut font object is converted by fpg_convert()
 * before it is checked and scaled. Deterministic, so a second copy of the
 * DLL (the game loads the renderer twice) computes the same cells. */
static int fr_met(void* ctx, int em, int code, FpgMet* m) {
    FrCtx* c = (FrCtx*)ctx; GLYPHMETRICS_ gm;
    if (!fr_size(c, em)) return 0;
    if (fr_GetGlyphIndicesA) {      /* not in the face: no new cell, the game's letter stays (GGI_MARK_NONEXISTING_GLYPHS) */
        char s[2]; unsigned short gi = 0;
        s[0] = (char)code; s[1] = 0;
        if (fr_GetGlyphIndicesA(c->dc, s, 1, &gi, 1) != 0xFFFFFFFFu && gi == 0xFFFF) return 0;
    }
    if (fr_GetGlyphOutlineA(c->dc, (unsigned)code, 0 /*GGO_METRICS*/, &gm, 0, 0, &fr_identity) == 0xFFFFFFFFu) return 0;
    m->bbx = (int)gm.bbx; m->bby = (int)gm.bby; m->ox = gm.ox; m->oy = gm.oy; m->adv = gm.incx;
    return 1;
}
static int fpg_veto(void* ctx, int slot, int u, int v) { (void)ctx; return tx_slot(slot, u, v, 0) != 0; }

/* FpgRec copies of a font's file records (drawn glyphs only); map[i] = index in g_txFile[f] */
static int fpg_recs_of(int f, FpgRec* out, int* map) {
    int i, n = 0, B = -1;
    for (i = 0; i < g_txFileN[f]; i++) if (g_txFile[f][i].code == 'H') B = g_txFile[f][i].yoff + g_txFile[f][i].h;
    for (i = 0; i < g_txFileN[f]; i++) {
        const TxRec* t = &g_txFile[f][i];
        if (t->mat < 0x4E || t->mat > 0x51 || t->w <= 0 || t->h <= 0) continue;
        out[n].code = t->code; out[n].slot = (unsigned char)t->mat;
        out[n].c.u = t->u; out[n].c.v = t->v; out[n].c.w = t->w; out[n].c.h = t->h;
        out[n].c.base = (B >= 0 ? B : t->yoff + t->h) - t->yoff; out[n].yoff = t->yoff;
        if (map) map[n] = i;
        n++;
    }
    return n;
}

static __attribute__((noinline)) void fpg_own_layout(void) {
    static unsigned char occ[4 * FPG_SIDE * FPG_SIDE];
    static FpgRec r[TX_MAXR];
    static int map[FPG_OWN_N][TX_MAXR];
    static FpgNewCell all[1024];
    static unsigned char fontOf[1024];
    char path[700];
    FrCtx c;
    int f, i, n, nc = 0, start[FPG_OWN_N], Bn[FPG_OWN_N], L[FPG_OWN_N], added, placed, want = 0, got = 0;
    if (!g_fpgK || !g_fpgOwnCfg) return;
    if (g_fpgRedraw == 3) want |= 3;            /* phil + hbut */
    if (g_fpgTip && (g_fpgRedraw & 2)) want |= 2;   /* hbut alone: [Fonts] Tooltips */
    if (g_fpgJournal && (g_fpgRedraw & 1)) want |= 1;   /* phil alone: [Fonts] Journal */
    if (g_fpgMenu) want |= 4;                   /* magc: [Fonts] Menu */
    if (g_fpgName) want |= 8 | 16;              /* mgrn + mred: [Fonts] Names */
    if (!want || !fr_load()) return;
    memset(occ, 0, sizeof occ);
    for (f = 0; f < TD_NFONT; f++) {             /* every other font's glyphs (and a 1-texel gutter) are taken */
        int o = f - FPG_FILE_PHIL;
        if (o >= 0 && o < FPG_OWN_N && (want & (1 << o))) continue;
        for (i = 0; i < g_txFileN[f]; i++) {
            const TxRec* t = &g_txFile[f][i];
            if (t->w > 0 && t->h > 0) fpg_occupy(occ, t->mat, t->u, t->v, t->w, t->h);
        }
    }
    for (f = 0; f < FPG_OWN_N; f++) {
        start[f] = nc;
        g_fpgOwnEm[f] = 0;
        if (!(want & (1 << f))) continue;
        wsprintfA(path, "%s%s", g_dir, fr_file_of(f));
        added = fr_AddFontResourceExA(path, 0x10 /*FR_PRIVATE*/, 0);
        if (!added) LOG("own metrics: %s: %s not loaded (missing?)\r\n", g_fpgFontName[f], path);
        if (fr_open_font(&c, f)) {
            n = fpg_recs_of(FPG_FILE_PHIL + f, r, map[f]);
            g_fpgOwnEm[f] = fpg_choose_em(r, n, g_fpgK, fr_met, &c, &Bn[f], &L[f]);
            if (g_fpgOwnEm[f]) nc += fpg_new_cells(r, n, g_fpgK, g_fpgOwnEm[f], Bn[f], L[f], 0, fr_met, &c, all + nc, 1024 - nc);
            for (i = start[f]; i < nc; i++) { all[i].src = map[f][all[i].src]; fontOf[i] = (unsigned char)f; }   /* index into g_txFile */
            {   FpgMet sp; g_fpgSpaceW[f] = g_fpgOwnEm[f] && fr_met(&c, g_fpgOwnEm[f], ' ', &sp) ? (sp.adv + g_fpgK / 2) / g_fpgK : 0; }
            LOG("own metrics: %s: %d px em, baseline %d, line %d, %d cells, space %d\r\n", g_fpgFontName[f], g_fpgOwnEm[f], Bn[f], L[f], nc - start[f], g_fpgSpaceW[f]);
        } else LOG("own metrics: %s: no usable face - the original cells stay\r\n", g_fpgFontName[f]);
        fr_close(&c);
        if (added) fr_RemoveFontResourceExA(path, 0x10, 0);
        if (g_fpgOwnEm[f]) got |= 1 << f;
        else nc = start[f];
    }
    if (!g_fpgTip && !g_fpgJournal && (want & 3) == 3 && (got & 3) != 3) {    /* phil and hbut go together (old FontRedraw-only path; g_fpgTip/g_fpgJournal are always set now) */
        LOG("own metrics: phil/hbut: no size found - they keep the original cells\r\n");
        got &= ~3;
    }
    if ((want & 24) == 24 && (got & 24) != 24) { /* so do mgrn and mred (one face, two colours) */
        LOG("own metrics: mgrn/mred: no size found - they keep the original cells\r\n");
        got &= ~24;
    }
    if (!got) { LOG("own metrics: off\r\n"); return; }
    /* the cells of the fonts that made it, packed together; their original rectangles are free */
    {   int m = 0;
        for (i = 0; i < nc; i++) if (got & (1 << fontOf[i])) { all[m] = all[i]; fontOf[m] = fontOf[i]; m++; }
        nc = m;
    }
    {   /* glyphs of the kept fonts that got no new cell (not in the face) keep their original rectangle */
        static unsigned char hasCell[TX_MAXR];
        for (f = 0; f < FPG_OWN_N; f++) {
            int ff = FPG_FILE_PHIL + f, kept = 0;
            if (!(got & (1 << f))) continue;
            memset(hasCell, 0, sizeof hasCell);
            for (i = 0; i < nc; i++) if (fontOf[i] == f && all[i].src >= 0 && all[i].src < TX_MAXR) hasCell[all[i].src] = 1;
            for (i = 0; i < g_txFileN[ff]; i++) {
                const TxRec* t = &g_txFile[ff][i];
                if (t->w > 0 && t->h > 0 && t->mat >= 0x4E && t->mat <= 0x51 && !hasCell[i]) { fpg_occupy(occ, t->mat, t->u, t->v, t->w, t->h); kept++; }
            }
            if (kept) LOG("own metrics: %s: %d glyph(s) not in the face keep the game's letters\r\n", g_fpgFontName[f], kept);
        }
    }
    {   /* fonts dropped above: their original rectangles are taken again */
        for (f = 0; f < FPG_OWN_N; f++) {
            int ff = FPG_FILE_PHIL + f;
            if (!(want & (1 << f)) || (got & (1 << f))) continue;
            for (i = 0; i < g_txFileN[ff]; i++) {
                const TxRec* t = &g_txFile[ff][i];
                if (t->w > 0 && t->h > 0) fpg_occupy(occ, t->mat, t->u, t->v, t->w, t->h);
            }
        }
    }
    placed = fpg_pack(occ, all, nc, fpg_veto, 0);
    if (placed != nc) { LOG("own metrics: only %d of %d cells fit on the pages - off\r\n", placed, nc); return; }
    /* commit: keep the originals, make the text hook's tables describe the new cells */
    if (g_cfgLog && !g_tdLoaded) td_load(g_dir);
    {   int m;
        for (f = 0; f < FPG_OWN_N; f++) {
            int ff = FPG_FILE_PHIL + f;
            if (!(got & (1 << f))) continue;
            g_fpgOrigN[f] = g_txFileN[ff];
            for (i = 0; i < g_txFileN[ff]; i++) g_fpgOrig[f][i] = g_txFile[ff][i];
            g_fpgNCell[f] = 0;
            for (m = 0; m < nc; m++) {
                FpgNewCell* cl = &all[m]; TxRec* t = &g_txFile[ff][cl->src]; TxKey* key;
                if (fontOf[m] != f || g_fpgNCell[f] >= 256) continue;
                g_fpgCell[f][g_fpgNCell[f]++] = *cl;
                t->mat = cl->slot; t->u = cl->u; t->v = cl->v; t->w = cl->w; t->h = cl->h; t->yoff = (short)cl->yoff;
                key = tx_slot(cl->slot, cl->u, cl->v, 1);
                if (key) { key->w = (short)cl->w; key->h = (short)cl->h; key->font = (signed char)ff; }
                td_add(cl->code, ff, cl->slot, cl->u, cl->v, cl->w, cl->h);
            }
            if (g_fpgSpaceW[f] > 0)
                for (i = 0; i < g_txFileN[ff]; i++) if (g_txFile[ff][i].code == ' ' && !g_txFile[ff][i].mat) g_txFile[ff][i].w = g_fpgSpaceW[f];
        }
    }
    g_fpgOwn = got;
    LOG("own metrics: on for%s%s%s%s%s, %d cells packed into free space on pages 4E-51; letter spacing 0, kerning 0\r\n",
        got & 1 ? " phil" : "", got & 2 ? " hbut" : "", got & 4 ? " magc" : "", got & 8 ? " mgrn" : "", got & 16 ? " mred" : "", nc);
}

/* a phil/hbut/magc/mgrn/mred font object as the game loaded it (original records) -> the new cells, unscaled */
static int fpg_own_font(int f) { return f >= FPG_FILE_PHIL && f < FPG_FILE_PHIL + FPG_OWN_N && (g_fpgOwn & (1 << (f - FPG_FILE_PHIL))); }
static void fpg_convert_rec(int f, unsigned char* r) {
    int code = r[0] | (r[1] << 8), j;
    int mat = *(int*)(r + 4), u = *(int*)(r + 8), v = *(int*)(r + 0xC), w = *(int*)(r + 0x10), h = *(int*)(r + 0x14), yo = *(short*)(r + 0x1C);
    for (j = 0; j < g_fpgOrigN[f]; j++) {
        const TxRec* o = &g_fpgOrig[f][j]; const TxRec* nw = &g_txFile[FPG_FILE_PHIL + f][j];
        if (o->code != code || o->mat != mat || o->u != u || o->v != v || o->w != w || o->h != h || o->yoff != yo) continue;
        *(int*)(r + 4) = nw->mat; *(int*)(r + 8) = nw->u; *(int*)(r + 0xC) = nw->v;
        *(int*)(r + 0x10) = nw->w; *(int*)(r + 0x14) = nw->h; *(short*)(r + 0x1C) = nw->yoff;
        return;
    }
}
static void fpg_convert(unsigned char* font, int ff) {
    int f = ff - FPG_FILE_PHIL, n, i, j;
    unsigned char* recs;
    if (!fpg_own_font(ff)) return;
    n = *(int*)(font + 0x14);
    recs = (unsigned char*)*(DWORD*)(font + 4);
    if (n <= 0 || n > TX_MAXR || !readable(recs, (DWORD)n * 32)) return;
    for (i = 0; i < n; i++) fpg_convert_rec(f, recs + i * 32);
    if (*(int*)(font + 8)) {                                   /* direct table: copies for codes < 256 */
        unsigned char* d = (unsigned char*)*(DWORD*)(font + 0xC);
        if (d && readable(d, 256 * 32))
            for (i = 0; i < 256; i++) if (*(int*)(d + i * 32 + 4) != -1) fpg_convert_rec(f, d + i * 32);
    }
    {                                                          /* kerning was made for the original shapes */
        unsigned char* k = (unsigned char*)*(DWORD*)(font + 0x24);
        int nk = *(int*)(font + 0x28);
        if (k && nk > 0 && nk <= 1024 && readable(k, (DWORD)nk * 12))
            for (i = 0; i < nk; i++) {
                unsigned char* p = (unsigned char*)*(DWORD*)(k + i * 12 + 4);
                int np = *(int*)(k + i * 12 + 8);
                if (p && np > 0 && np <= 1024 && readable(p, (DWORD)np * 8))
                    for (j = 0; j < np; j++) *(int*)(p + j * 8 + 4) = 0;
            }
    }
    *(int*)(font + 0x2C) = 0;                                  /* letter spacing: the new widths are the face's advances */
}

/* called from tx_install() before the loaded fonts are scaled */
static void fpg_own_install(void) {
    fpg_own_layout();
    if (g_fpgOwn) { tx_convert_hook = fpg_convert; tx_own_hook = fpg_own_font; }
}

/* the redraw choices, from the [Fonts] settings read in init() (g_cfg*Font). Before 2026-11-28
   [Debug] FontRedraw / FontFace / FontWeight / FontFile / FontOwnMetrics and the *FontFace/File/Weight
   keys chose these; the faces are now fixed (Cardo Bold, Montserrat ExtraBold, the bundled
   Soulbringer fonts) or read from a file of your own ([Fonts] Menu / Names = <file>). */
static void fpg_config(void) {
    g_fpgRedraw = 0;
    g_fpgTip = g_cfgTipFont;           /* 1 Soulbringer Text, 2 Cardo, 3 the game's */
    if (g_fpgTip == 1 || g_fpgTip == 2) g_fpgRedraw |= 2;
    g_fpgJournal = g_cfgJournalFont;
    if (g_fpgJournal == 1 || g_fpgJournal == 2) g_fpgRedraw |= 1;
    /* menu font: 1 Soulbringer Uncial (Eagle Lake with lining figures, tools/make_menu_font.py;
       renamed: Reserved Font Name), 2 MedievalSharp, 3 a file of your own */
    g_fpgMenu = g_cfgMenuFont;
    if (g_fpgMenu == 2) {
        wsprintfA(g_fpgMenuFace, "%s", "MedievalSharp"); g_fpgMenuWeight = 500;
        wsprintfA(g_fpgMenuFile, "%s", "SoulbringerModern\\fonts\\MedievalSharp-Regular.ttf");
    } else if (g_fpgMenu == 3) {
        wsprintfA(g_fpgMenuFace, "%s", g_cfgMenuFace); g_fpgMenuWeight = g_cfgMenuWeight;
        wsprintfA(g_fpgMenuFile, "%s", g_cfgMenuFile);
    } else {
        wsprintfA(g_fpgMenuFace, "%s", "Soulbringer Uncial"); g_fpgMenuWeight = 400;
        wsprintfA(g_fpgMenuFile, "%s", "SoulbringerModern\\fonts\\SoulbringerUncial-Regular.ttf");
    }
    /* name font: 1 the bundled Soulbringer Text (the defaults above), 2 a file of your own */
    g_fpgName = g_cfgNameFont;
    if (g_fpgName == 2) {
        wsprintfA(g_fpgNameFace, "%s", g_cfgNameFace); g_fpgNameWeight = g_cfgNameWeight;
        wsprintfA(g_fpgNameFile, "%s", g_cfgNameFile);
    }
}
