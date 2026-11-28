/* Text decoding for the debug log (Log=1). Included by soulbringer_modern.c
 * before ui_layout.inc (hover_log.inc uses it).
 *
 * The game draws text as glyph batches: 0x42B024 (ecx = batch, kept in edi)
 * hands each batch to the renderer's AddTri, which is where the hook sees it
 * (the stub saves the game's registers: edi = frame[-9]). A batch holds up to
 * 10 glyphs of one font texture: +4 count, +8 material, glyph k at
 * batch + k*0x80 with three corners x,y (floats) at +0x14/+0x18, +0x2C/+0x30,
 * +0x44/+0x48 and their texel coordinates (ints) at +0x20/+0x24, +0x38/+0x3C,
 * +0x50/+0x54 (see DEVLOG "Game-wide text").
 *
 * The fonts (data\*.fnt) list, per character, its texture and its rectangle
 * in it (u, v, width, height). Across all 16 fonts no rectangle stands for
 * two different characters, so the rectangle of a drawn glyph names its
 * character (and the font). That turns every drawn glyph back into text with
 * no patch to the game's code.
 *
 * Font file: u16 count, then 32-byte records: u16 code, u16 -, i32 material,
 * i32 u (+8), i32 v (+0xC), i32 size (+0x10), i32 size (+0x14), i32 -,
 * u16 vertical offset, u16 -. The draw code (0x433A76) spans u by +0x14 and
 * v by +0x10, so both pairings are tried. Glyphs clipped at a slot or panel
 * edge are smaller: they are matched by material and top-left corner (also
 * unique across the fonts) with a size no larger than the font's.
 */
#ifndef TD_PATHFMT
#define TD_PATHFMT "%sdata\\%s.fnt"      /* game folder + font name (the test harness uses its own) */
#endif
#define TD_MAXG   3200
#define TD_HASH   8192
#define TD_NFONT  16
static const char* const td_file[TD_NFONT] = {
    "5PxB", "5PxG", "5PxR", "5PxX", "5PxY", "16xB", "16xX", "16xY",
    "phil", "hbut", "magc", "mgrn", "mred", "cred", "numg", "numr" };
typedef struct { unsigned short code; unsigned char font, mat; short u, v, w, h; } TdGlyph;
static TdGlyph g_tdG[TD_MAXG]; static int g_tdN;
static short   g_tdHash[TD_HASH];          /* index into g_tdG, -1 empty */
static int     g_tdLoaded, g_tdMiss;

static DWORD td_key(int u, int v, int w, int h) {
    DWORD k = (DWORD)(u & 0xFFF) | ((DWORD)(v & 0xFFF) << 12) | ((DWORD)(w & 0xFF) << 24);
    k ^= (DWORD)(h & 0xFF) * 0x9E3779B1u;
    k ^= k >> 15; k *= 0x2C1B3C6Du; k ^= k >> 12;
    return k;
}
static int td_find(int u, int v, int w, int h) {
    DWORD i = td_key(u, v, w, h) & (TD_HASH - 1);
    while (g_tdHash[i] >= 0) {
        const TdGlyph* g = &g_tdG[g_tdHash[i]];
        if (g->u == u && g->v == v && g->w == w && g->h == h) return g_tdHash[i];
        i = (i + 1) & (TD_HASH - 1);
    }
    return -1;
}

/* load the fonts once (debug log only) */
static void td_load(const char* dir) {
    static unsigned char buf[8192];          /* the records: 2 + 202*32 = 6466 bytes */
    int f, i, total = 0;
    if (g_tdLoaded) return;
    g_tdLoaded = 1;
    for (i = 0; i < TD_HASH; i++) g_tdHash[i] = -1;
    for (f = 0; f < TD_NFONT; f++) {
        char path[700]; HANDLE h; DWORD got = 0; int n, r;
        wsprintfA(path, TD_PATHFMT, dir, td_file[f]);
        h = CreateFileA(path, 0x80000000 /*GENERIC_READ*/, 1, 0, 3 /*OPEN_EXISTING*/, 0x80, 0);
        if (h == (HANDLE)-1) { LOG("text decode: %s not found\r\n", path); continue; }
        if (!ReadFile(h, buf, sizeof buf, &got, 0)) got = 0;
        CloseHandle(h);
        if (got < 2) continue;
        n = buf[0] | (buf[1] << 8);
        if (2 + n * 32 > (int)got) n = ((int)got - 2) / 32;
        for (r = 0; r < n && g_tdN < TD_MAXG; r++) {
            const unsigned char* p = buf + 2 + r * 32;
            int code = p[0] | (p[1] << 8), mat = *(const int*)(p + 4);
            int u = *(const int*)(p + 8), v = *(const int*)(p + 12), w = *(const int*)(p + 16), hh = *(const int*)(p + 20);
            DWORD k;
            if (!mat || w <= 0 || hh <= 0) continue;          /* space and other empty glyphs */
            if (td_find(u, v, w, hh) >= 0) continue;          /* same rectangle in an earlier font: same character */
            g_tdG[g_tdN].code = (unsigned short)code; g_tdG[g_tdN].font = (unsigned char)f; g_tdG[g_tdN].mat = (unsigned char)mat;
            g_tdG[g_tdN].u = (short)u; g_tdG[g_tdN].v = (short)v; g_tdG[g_tdN].w = (short)w; g_tdG[g_tdN].h = (short)hh;
            k = td_key(u, v, w, hh) & (TD_HASH - 1);
            while (g_tdHash[k] >= 0) k = (k + 1) & (TD_HASH - 1);
            g_tdHash[k] = (short)g_tdN++;
            total++;
        }
    }
    LOG("text decode: %d glyph shapes from %d fonts\r\n", total, TD_NFONT);
}

/* one more glyph shape (HD fonts step 5: the new cells of a font with its own metrics) */
static void td_add(int code, int font, int mat, int u, int v, int w, int h) {
    DWORD k;
    if (!g_tdLoaded || g_tdN >= TD_MAXG || td_find(u, v, w, h) >= 0) return;
    g_tdG[g_tdN].code = (unsigned short)code; g_tdG[g_tdN].font = (unsigned char)font; g_tdG[g_tdN].mat = (unsigned char)mat;
    g_tdG[g_tdN].u = (short)u; g_tdG[g_tdN].v = (short)v; g_tdG[g_tdN].w = (short)w; g_tdG[g_tdN].h = (short)h;
    k = td_key(u, v, w, h) & (TD_HASH - 1);
    while (g_tdHash[k] >= 0) k = (k + 1) & (TD_HASH - 1);
    g_tdHash[k] = (short)g_tdN++;
}

/* decoded batches, handed from hook_pre (where the batch is seen) to the
   pending draw (where its vertices are processed) */
#define TD_RING 64
#define TD_PER  10
typedef struct { int n; unsigned short ch[TD_PER]; unsigned char font[TD_PER]; } TdBatch;
static TdBatch g_tdRing[TD_RING]; static int g_tdNext;

/* one byte for the log: ASCII and Latin-1 letters as they are (the log is read as Latin-1), else '?' */
static char td_ascii(unsigned short c) { return (c >= 32 && c < 127) || (c >= 160 && c < 256) ? (char)c : c ? '?' : 0; }

/* batch = the game's edi at its AddTri; returns a ring index, or -1 */
static int td_decode(const unsigned char* batch) {
    TdBatch* b; int n, k;
    if (!g_tdN || !batch || !readable(batch, 0x514)) return -1;
    n = *(const int*)(batch + 4);
    if (n <= 0 || n > TD_PER) return -1;
    b = &g_tdRing[g_tdNext];
    b->n = n;
    for (k = 0; k < n; k++) {
        const unsigned char* g = batch + k * 0x80;
        int u[3], v[3], j, u0, u1, v0, v1, idx;
        u[0] = *(const int*)(g + 0x20); v[0] = *(const int*)(g + 0x24);
        u[1] = *(const int*)(g + 0x38); v[1] = *(const int*)(g + 0x3C);
        u[2] = *(const int*)(g + 0x50); v[2] = *(const int*)(g + 0x54);
        u0 = u1 = u[0]; v0 = v1 = v[0];
        for (j = 1; j < 3; j++) { if (u[j] < u0) u0 = u[j]; if (u[j] > u1) u1 = u[j]; if (v[j] < v0) v0 = v[j]; if (v[j] > v1) v1 = v[j]; }
        idx = td_find(u0, v0, u1 - u0, v1 - v0);
        if (idx < 0) idx = td_find(u0, v0, v1 - v0, u1 - u0);   /* the draw code pairs u with +0x14, v with +0x10 */
        if (idx < 0) {                                          /* clipped at a slot or panel edge: same top-left, smaller */
            int mat = *(const int*)(batch + 8), j2;
            for (j2 = 0; j2 < g_tdN; j2++) {
                const TdGlyph* t = &g_tdG[j2];
                int big = t->w > t->h ? t->w : t->h;
                if (t->mat == mat && t->u == u0 && t->v == v0 && u1 - u0 <= big && v1 - v0 <= big) { idx = j2; break; }
            }
        }
        if (idx >= 0) { b->ch[k] = g_tdG[idx].code; b->font[k] = g_tdG[idx].font; }
        else {
            b->ch[k] = '?'; b->font[k] = 255;
            if (g_tdMiss++ < 12)
                LOG("text decode: unknown glyph m=%X texels u %d..%d v %d..%d (uv ints %d,%d %d,%d %d,%d)\r\n",
                    *(const DWORD*)(batch + 8), u0, u1, v0, v1, u[0], v[0], u[1], v[1], u[2], v[2]);
        }
    }
    k = g_tdNext; g_tdNext = (g_tdNext + 1) % TD_RING;
    return k;
}
