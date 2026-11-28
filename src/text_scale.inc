/* Game-side text scaling ([Debug] TextHook=1, step 3 phase 3). Included by
 * soulbringer_modern.c after text_decode.inc; the offline harness includes it
 * too (tests/replay/harness.c), so everything here except the hooks is plain
 * C on memory it is given.
 *
 * How the game sizes text (DEVLOG "Text layout code mapped"): every
 * measurement (pen advance, word width, wrapping, alignment, box auto-fit,
 * line step) reads the font object's glyph records and three font fields;
 * only the draw-glyph function 0x433714 also uses a record's width/height as
 * the glyph's rectangle in the font texture (one texel per screen pixel).
 * So:
 *
 *   1. after the game loads a font (0x628394, called from 0x681F5E), its
 *      metrics are multiplied by s: per record width +0x10, height +0x14 and
 *      y offset +0x1C (sorted table +4 and direct table +0xC), line height
 *      +0x18, line gap +0x20, letter spacing +0x2C, kerning adjusts;
 *   2. at add glyph 0x626480 (only ever called by 0x433714, so ebp is that
 *      function's frame) the four texel coordinates the game wrote,
 *      u = rec.u + (x - x0), are rewritten to rec.u + (x - x0) * w / (w * s),
 *      w being the size in the font file: the quad is s times larger and
 *      still shows exactly the original glyph, clipping included;
 *   3. the letter-spacing setter 0x691F50 (called after loading, -1 for
 *      16xy, 3 for two others) scales its value for fonts scaled here.
 *
 * Fonts are recognised by file name (font +0, e.g. "16xB.fnt") and checked
 * record by record against the files in data\: only a font whose records
 * match the file exactly is scaled; one that matches the file scaled by s
 * was scaled by an earlier copy of this DLL (the game loads the renderer
 * twice) and is only registered. The texel rewrite looks the glyph up by
 * material and texel corner in the files, so a glyph of a font that was not
 * scaled (sizes equal to the file) is left alone.
 *
 * Needs from the includer: DWORD, LOG, readable(), CreateFileA/ReadFile/
 * CloseHandle, wsprintfA, td_file[] and TD_NFONT, TD_PATHFMT (text_decode.inc),
 * and optionally TX_PTR (game address -> pointer; the harness maps it into
 * an arena because it runs 64-bit).
 */
#ifndef TX_PTR
#define TX_PTR(a) ((unsigned char*)(a))
#endif

/* ---- the font files: original metrics ---- */
#define TX_MAXR 256
typedef struct { unsigned short code; short yoff; int mat, u, v, w, h; } TxRec;
static TxRec g_txFile[TD_NFONT][TX_MAXR];
static int   g_txFileN[TD_NFONT];
static int   g_txLoaded;
#define TX_HASH 8192
typedef struct { int mat, u, v; short w, h; signed char font; } TxKey;   /* w = -1: two fonts disagree, never rewritten; font: td_file index, -1 = shared by two fonts */
static TxKey g_txKey[TX_HASH];
static unsigned char g_txUsed[TX_HASH];
static int   g_txKeys, g_txClash;

static DWORD tx_hash(int mat, int u, int v) {
    DWORD k = (DWORD)(u & 0xFFF) | ((DWORD)(v & 0xFFF) << 12) | ((DWORD)(mat & 0xFF) << 24);
    k ^= k >> 15; k *= 0x2C1B3C6Du; k ^= k >> 12;
    return k & (TX_HASH - 1);
}
static TxKey* tx_slot(int mat, int u, int v, int add) {
    DWORD i = tx_hash(mat, u, v), n;
    for (n = 0; n < TX_HASH; n++, i = (i + 1) & (TX_HASH - 1)) {
        if (!g_txUsed[i]) {
            if (!add) return 0;
            g_txUsed[i] = 1; g_txKey[i].mat = mat; g_txKey[i].u = u; g_txKey[i].v = v; g_txKey[i].w = 0; g_txKey[i].h = 0; g_txKey[i].font = -2;
            g_txKeys++;
            return &g_txKey[i];
        }
        if (g_txKey[i].mat == mat && g_txKey[i].u == u && g_txKey[i].v == v) return &g_txKey[i];
    }
    return 0;
}

static void tx_load(const char* dir) {
    static unsigned char buf[8192];
    int f;
    if (g_txLoaded) return;
    g_txLoaded = 1;
    for (f = 0; f < TD_NFONT; f++) {
        char path[700]; HANDLE h; DWORD got = 0; int n, r;
        wsprintfA(path, TD_PATHFMT, dir, td_file[f]);
        g_txFileN[f] = 0;
        h = CreateFileA(path, 0x80000000 /*GENERIC_READ*/, 1, 0, 3 /*OPEN_EXISTING*/, 0x80, 0);
        if (h == (HANDLE)-1) { LOG("text hook: %s not found (that font stays unscaled)\r\n", path); continue; }
        if (!ReadFile(h, buf, sizeof buf, &got, 0)) got = 0;
        CloseHandle(h);
        if (got < 2) continue;
        n = buf[0] | (buf[1] << 8);
        if (2 + n * 32 > (int)got) n = ((int)got - 2) / 32;
        if (n > TX_MAXR) n = TX_MAXR;
        for (r = 0; r < n; r++) {
            const unsigned char* p = buf + 2 + r * 32;
            TxRec* t = &g_txFile[f][r];
            TxKey* k;
            t->code = (unsigned short)(p[0] | (p[1] << 8));
            t->mat = *(const int*)(p + 4); t->u = *(const int*)(p + 8); t->v = *(const int*)(p + 12);
            t->w = *(const int*)(p + 16); t->h = *(const int*)(p + 20);
            t->yoff = (short)(p[0x1C] | (p[0x1D] << 8));
            if (!t->mat || t->w <= 0 || t->h <= 0) continue;
            k = tx_slot(t->mat, t->u, t->v, 1);
            if (!k) continue;
            if (k->font == -2) k->font = (signed char)f; else if (k->font != f) k->font = -1;
            if (!k->w && !k->h) { k->w = (short)t->w; k->h = (short)t->h; }
            else if (k->w != t->w || k->h != t->h) { if (k->w >= 0) g_txClash++; k->w = -1; }
        }
        g_txFileN[f] = n;
    }
    LOG("text hook: %d glyph rectangles from the font files (%d ambiguous)\r\n", g_txKeys, g_txClash);
}

/* HD fonts step 5 (font_hd.inc sets these in the DLL; 0 elsewhere): a font
   file can get its own metrics. tx_convert_hook rewrites a loaded font's
   records from the original to the new cells before they are checked and
   scaled; tx_own_hook(f) says the font's letter spacing is 0 (its new
   widths are the face's advances). */
static void (*tx_convert_hook)(unsigned char* font, int f);
static int  (*tx_own_hook)(int f);

/* the font a drawn glyph comes from, by its texture rectangle's top-left corner: td_file index, or -1 */
static int tx_glyph_font(int mat, int u, int v) {
    TxKey* k = g_txKeys ? tx_slot(mat, u, v, 0) : 0;
    return k && k->font >= 0 ? k->font : -1;
}

/* round(x * pct / 100), symmetric */
static int tx_mul(int x, int pct) { return x >= 0 ? (x * pct + 50) / 100 : -((-x * pct + 50) / 100); }

/* font +0 -> index into td_file, or -1 */
static int tx_font_file(const unsigned char* font) {
    const char* name; int f, i, s = 0, e;
    if (!readable(font, 0x30)) return -1;
    name = (const char*)TX_PTR(*(const DWORD*)font);
    if (!name || !readable(name, 1)) return -1;
    for (e = 0; e < 260 && name[e]; e++) if (name[e] == '\\' || name[e] == '/' || name[e] == ':') s = e + 1;
    if (e >= 260 || e - s < 5) return -1;
    if (name[e - 4] != '.' || (name[e - 3] | 0x20) != 'f' || (name[e - 2] | 0x20) != 'n' || (name[e - 1] | 0x20) != 't') return -1;
    for (f = 0; f < TD_NFONT; f++) {
        const char* t = td_file[f];
        for (i = 0; t[i] && s + i < e - 4; i++) if ((t[i] | 0x20) != (name[s + i] | 0x20)) break;
        if (!t[i] && s + i == e - 4) return g_txFileN[f] ? f : -1;
    }
    return -1;
}

/* fonts scaled (or found scaled) in this process. The game reloads fonts
   into the same objects (per screen, or 5px -> 16x): the reload refreshes the
   records and kerning but keeps line height +0x18 (a running max), line gap
   +0x20 and letter spacing +0x2C, so those are recomputed from the records
   and from the unscaled spacing kept here, never multiplied again. */
#define TX_MAXFONT 256   /* originals (~25) + per-place copies (font_places.inc, 92) + reloads into new objects */
typedef struct { const unsigned char* font; DWORD recs; int pct, rawSp; } TxFont;
static TxFont g_txFont[TX_MAXFONT]; static int g_txNFont, g_txNextFont;
static int tx_entry(const unsigned char* font) {
    int i;
    for (i = 0; i < g_txNFont; i++) if (g_txFont[i].font == font) return i;
    return -1;
}
static int tx_registered(const unsigned char* font) {
    int i = tx_entry(font);
    return i >= 0 && g_txFont[i].pct && readable(font, 0x30) && *(const DWORD*)(font + 4) == g_txFont[i].recs ? i + 1 : 0;
}
static void tx_register(const unsigned char* font, int pct, int rawSp) {
    int i = tx_entry(font);
    if (i < 0) {
        if (g_txNFont < TX_MAXFONT) i = g_txNFont++;
        else { i = g_txNextFont; g_txNextFont = (g_txNextFont + 1) % TX_MAXFONT; }
    }
    g_txFont[i].font = font; g_txFont[i].recs = *(const DWORD*)(font + 4); g_txFont[i].pct = pct; g_txFont[i].rawSp = rawSp;
}

static void tx_scale_rec(unsigned char* r, int pct) {
    *(int*)(r + 0x10) = tx_mul(*(int*)(r + 0x10), pct);
    *(int*)(r + 0x14) = tx_mul(*(int*)(r + 0x14), pct);
    *(short*)(r + 0x1C) = (short)tx_mul(*(short*)(r + 0x1C), pct);
}

/* line height, gap and spacing as the game would have them at this scale (pct 100 = unscaled) */
static void tx_fields(unsigned char* font, int n, const unsigned char* recs, int pct, int rawSp) {
    int i, mb = 0;
    for (i = 0; i < n; i++) { int b = *(const short*)(recs + i * 32 + 0x1C) + *(const int*)(recs + i * 32 + 0x14); if (b > mb) mb = b; }
    *(int*)(font + 0x18) = mb;                     /* max (y offset + height), as 0x628394 computes it */
    *(int*)(font + 0x20) = tx_mul(1, pct);         /* line gap: 1 (only the constructor sets it) */
    *(int*)(font + 0x2C) = tx_own_hook && tx_own_hook(tx_font_file(font)) ? 0 : tx_mul(rawSp, pct);   /* letter spacing */
}

/* Returns 1 scaled now, 2 found scaled already (registered), 0 left alone. */
static int tx_scale_font(unsigned char* font, int pct, const char** why) {
    int f, n, i, j, same = 0, scaled = 0, other = 0, e, rawSp;
    unsigned char* recs;
    *why = "";
    if (!readable(font, 0x30)) { *why = "not readable"; return 0; }
    if (tx_convert_hook) { int ff = tx_font_file(font); if (ff >= 0) tx_convert_hook(font, ff); }
    if (pct == 100) { *why = "scale 100%"; return 0; }
    e = tx_entry(font);
    rawSp = e >= 0 && g_txFont[e].pct ? g_txFont[e].rawSp : *(int*)(font + 0x2C);
    n = *(int*)(font + 0x14);
    recs = TX_PTR(*(DWORD*)(font + 4));
    if (n <= 0 || n > TX_MAXR || !recs || !readable(recs, (DWORD)n * 32)) { *why = "records not readable"; return 0; }
    f = tx_font_file(font);
    if (f >= 0)
        for (i = 0; i < n; i++) {                /* each record must be in the file, as it is or scaled */
            const unsigned char* r = recs + i * 32;   /* (codes repeat: the files hold several spaces) */
            int code = r[0] | (r[1] << 8), w = *(const int*)(r + 0x10), h = *(const int*)(r + 0x14), yo = *(const short*)(r + 0x1C);
            int u = *(const int*)(r + 8), v = *(const int*)(r + 0xC), isSame = 0, isScaled = 0;
            for (j = 0; j < g_txFileN[f]; j++) {
                const TxRec* t = &g_txFile[f][j];
                if (t->code != code || t->u != u || t->v != v) continue;
                if (w == t->w && h == t->h && yo == t->yoff) isSame = 1;
                if (w == tx_mul(t->w, pct) && h == tx_mul(t->h, pct) && yo == tx_mul(t->yoff, pct)) isScaled = 1;
            }
            if (isSame && isScaled) continue;     /* e.g. empty: tells nothing */
            if (isSame) same++; else if (isScaled) scaled++; else other++;
        }
    if (f < 0 || other || (same && scaled)) {
        *why = f < 0 ? "not one of the known fonts" : "records differ from the font file";
        if (e >= 0 && g_txFont[e].pct) {         /* one of ours reloaded with something else: undo our fields */
            tx_fields(font, n, recs, 100, rawSp);
            g_txFont[e].pct = 0;
        }
        return 0;
    }
    if (scaled) {                                /* scaled by an earlier copy of this DLL, or called twice */
        if (e < 0 || !g_txFont[e].pct) rawSp = tx_mul(*(int*)(font + 0x2C), 10000 / pct);
        tx_register(font, pct, rawSp);
        *why = "found scaled already";
        return 2;
    }

    for (i = 0; i < n; i++) tx_scale_rec(recs + i * 32, pct);
    if (*(int*)(font + 8)) {                                   /* direct table: copies for codes < 256 */
        unsigned char* d = TX_PTR(*(DWORD*)(font + 0xC));
        if (d && readable(d, 256 * 32))
            for (i = 0; i < 256; i++) if (*(int*)(d + i * 32 + 4) != -1) tx_scale_rec(d + i * 32, pct);
    }
    {                                                          /* kerning: 12-byte entries {code, pairs, count}, pairs 8 bytes {code, adjust +4} */
        unsigned char* k = TX_PTR(*(DWORD*)(font + 0x24));
        int nk = *(int*)(font + 0x28);
        if (k && nk > 0 && nk <= 1024 && readable(k, (DWORD)nk * 12))
            for (i = 0; i < nk; i++) {
                unsigned char* p = TX_PTR(*(DWORD*)(k + i * 12 + 4));
                int np = *(int*)(k + i * 12 + 8);
                if (p && np > 0 && np <= 1024 && readable(p, (DWORD)np * 8))
                    for (j = 0; j < np; j++) *(int*)(p + j * 8 + 4) = tx_mul(*(int*)(p + j * 8 + 4), pct);
            }
    }
    tx_fields(font, n, recs, pct, rawSp);
    tx_register(font, pct, rawSp);
    return 1;
}

/* the letter-spacing setter 0x691F50: font +0x2C = value, scaled for our fonts */
static int tx_spacing(const unsigned char* font, int value) {
    int i = tx_registered(font);
    if (!i) return value;
    g_txFont[i - 1].rawSp = value;
    if (tx_own_hook && tx_own_hook(tx_font_file(font))) return 0;
    return tx_mul(value, g_txFont[i - 1].pct);
}

/* The 96-byte corner block handed to add glyph: 4 vertices of 24 bytes,
   float x +0, float y +4, z +8, int u +0xC, int v +0x10. x0..y1 = the
   glyph's unclipped rectangle on screen. Returns 1 if rewritten. */
static int tx_fix_block(unsigned char* blk, int mat, int x0, int y0, int x1, int y1) {
    int wm = x1 - x0, hm = y1 - y0, ru, rv, k;
    TxKey* key;
    if (wm <= 0 || hm <= 0 || wm > 4096 || hm > 4096) return 0;
    ru = *(int*)(blk + 0xC) - ((int)*(float*)(blk + 0) - x0);   /* the game wrote u = rec.u + (x - x0) */
    rv = *(int*)(blk + 0x10) - ((int)*(float*)(blk + 4) - y0);
    key = tx_slot(mat, ru, rv, 0);
    if (!key || key->w <= 0 || key->h <= 0) return 0;
    if (key->w == wm && key->h == hm) return 0;                   /* not scaled */
    for (k = 0; k < 4; k++) {
        unsigned char* e = blk + k * 24;
        int dx = (int)*(float*)(e + 0) - x0, dy = (int)*(float*)(e + 4) - y0;
        *(int*)(e + 0xC) = ru + (2 * dx * key->w + wm) / (2 * wm);
        *(int*)(e + 0x10) = rv + (2 * dy * key->h + hm) / (2 * hm);
    }
    return 1;
}

/* ---- auto-fit fix (2026-11-26, options menu): enlarged text kept on one line ----
   The game's auto-fit 0x411634 lays a text out at +0x1C8 trial widths, from the
   longest word up to the text's full one-line width as it sums it itself, and in
   sizing mode 2 (+0x58) keeps the trial whose width is closest to the target
   width +0x5C (3840 for the options labels: the widest, one line). With enlarged
   fonts its own sum comes out narrower than what the layout 0x413A64 needs, so
   the "full width" trial wraps too and short labels ("Tool Tips", "Sound FX",
   "Hit Points") are broken onto two lines inside boxes much wider than the text.
   Fix, for mode 2 and fonts this DLL enlarged only: lay the text out with the
   game's own layout at the target width (at most the clip width, as the game);
   if that is one line and lower than the box the game chose, that line becomes
   the box. Its width is the layout's own, so drawing wraps exactly the same. */
typedef int (*TxLayFn)(void* ctx, int width, int* outW);   /* height of the text laid out at width; *outW = widest line */
/* Returns 1 and the new box size if the box should be replaced. step = one line's height
   (font +0x18 + font +0x20 + text +0x1DC). fontPct: the text's font size (100 = not enlarged). */
static int tx_autofit_choice(int mode, int target, int clipW, int boxW, int boxH, int fontPct, int step,
                             TxLayFn lay, void* ctx, int* newW, int* newH) {
    int w, h, ow = 0;
    if (mode != 2 || fontPct == 100 || boxW <= 0 || boxH <= 0 || step <= 0) return 0;
    w = target < clipW ? target : clipW;
    if (w <= boxW) return 0;                       /* nothing wider to try */
    h = lay(ctx, w, &ow);
    if (h <= 0 || h > step || h >= boxH || ow <= 0 || ow > w) return 0;   /* only: one line, lower than the game's box */
    *newW = ow; *newH = h;
    return 1;
}

/* ---- main-menu copyright line and save-list date (2026-11-26) ----------------------------
   Copyright (0x665D..0x666152, place 665F9A): after the auto-fit the main menu moves the line
   to x = W - w*W/768, y = H*510/512 - h*H/512 (W/H = 0x367370C/0x3673708, w/h = the text
   rectangle from 0x549FC4). That only puts it at the bottom right at 1024x768 with the font
   of that size; with enlarged text at 4K (w*5) it runs off to the left edge. Instead the line
   keeps the place it has at 1024x768 (reference screenshot: ink 536..897 x 739..753): right
   edge at 897/1024 of the width, bottom at 756/768 of the height (rectangle bottom = ink + 3). */
static void tx_copyright_xy(int W, int H, int w, int h, int* x, int* y) {
    *x = W * 897 / 1024 - w;
    *y = H * 756 / 768 - h;
    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
}
/* Save-list date (0x645ED4: push 200 before 0x62C45C -> 0x621E44, text object +0x1E0): the
   game cuts a string with "..." when it is wider than this many screen pixels, measured with
   the text object's font at that moment (16xy, before the place's magc copy is set). The limit
   is fixed at 200 px, so enlarged text is cut; it grows with the largest text size in use. */
#define TX_DATE_LIMIT 200
static int tx_date_limit(int maxPct) {
    if (maxPct < 100) maxPct = 100;
    return TX_DATE_LIMIT * maxPct / 100;
}
