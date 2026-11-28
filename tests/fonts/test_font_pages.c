/* Offline test and page builder for src/font_pages.inc (HD fonts step 2).
 *
 *   test_font_pages selftest
 *       synthetic checks, no game data needed
 *   test_font_pages build <dumps> <fonts> <glyphs.bin> <k> <outdir>
 *       <dumps>   folder with SBM_fontpage_<slot>_256x256x1.raw + SBM_palette.raw
 *       <fonts>   folder with the game's *.fnt
 *       <glyphs>  coverage bitmaps from tools/font_preview.py
 *       writes <outdir>/page_<slot>.argb (side 256k, A8R8G8B8 little endian)
 *       and <outdir>/cells.txt, and checks the result (see check_build)
 *   test_font_pages layout <fonts> <metrics.bin> <k> <outdir>
 *       own metrics (step 5): chooses the em size of phil/hbut from the face
 *       metrics written by tools/font_preview.py, makes new cells, packs them
 *       into free page space, writes <outdir>/newcells.txt and checks: all
 *       packed, a free texel between every cell and every other glyph,
 *       no two cells overlap, the line height is kept
 *   test_font_pages build ... <outdir> <newcells.txt>
 *       as build, drawing phil/hbut into the new cells
 *
 * Build: cc -O2 -o test_font_pages tests/fonts/test_font_pages.c -Isrc
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int DWORD;
#include "font_pages.inc"

static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static unsigned char* load(const char* path, long* len) {
    FILE* f = fopen(path, "rb"); unsigned char* b; long n;
    if (!f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(n ? n : 1);
    if (fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); return 0; }
    fclose(f); if (len) *len = n; return b;
}

/* ---------------------------------------------------------------- selftest */
static int synth_metrics(void* ctx, int em, int code, int* top, int* height) {
    (void)ctx;
    if (code == 'H') { *top = em * 7 / 10; *height = *top; return 1; }
    if (code == 'l') { *top = em * 8 / 10; *height = *top; return 1; }
    if (code == 'p') { *top = em * 45 / 100; *height = *top + em / 4; return 1; }
    if (code == 'J') { *top = em; *height = 3 * em; return 1; }
    return 0;
}
static int selftest(void) {
    static unsigned char idx[256 * 256], pal[768];
    static DWORD out[1024 * 1024], ref[1024 * 1024];
    unsigned char cov[20 * 30];
    int k, i, x, y;
    for (i = 0; i < 768; i++) pal[i] = (unsigned char)(i * 37);
    for (i = 0; i < 256 * 256; i++) idx[i] = (unsigned char)((i * 7 + i / 256) % 5 == 0 ? 0 : (i % 251) + 1);
    for (k = 1; k <= 4; k++) {
        int side = 256 * k;
        fpg_base(idx, pal, k, out);
        /* base: nearest enlargement, index 0 transparent */
        for (y = 0; y < side; y += 7)
            for (x = 0; x < side; x += 5) {
                int t = idx[(y / k) * 256 + x / k];
                DWORD want = t ? 0xFF000000u | (pal[3 * t] << 16) | (pal[3 * t + 1] << 8) | pal[3 * t + 2] : 0;
                if (out[y * side + x] != want) { CHECK(0, "base k=%d at %d,%d: %08X want %08X", k, x, y, out[y * side + x], want); goto next; }
            }
        memcpy(ref, out, sizeof(DWORD) * side * side);
        {
            /* a 6x9 cell at (100,50), baseline 7; a 10x14 bitmap (wider than 6k for k=1: condensed) */
            FpgCell c = { 100, 50, 6, 9, 7 };
            FpgGlyph g = { cov, 10, 14, 10 };
            FpgStyle s = { 0x00FF8040, 0x00102030, 1, 200 };
            int inkBottom = -1, inkTop = 1 << 30, inside = 0;
            memset(cov, 0, sizeof cov);
            for (y = 0; y < 14; y++) for (x = 0; x < 10; x++) cov[y * 10 + x] = (y <= 10) ? 255 : 0;  /* ink rows 0..10: rows 0..9 above baseline, row 10 on it */
            CHECK(fpg_glyph(out, k, &c, &g, &s), "glyph refused k=%d", k);
            for (y = 0; y < side; y++)
                for (x = 0; x < side; x++) {
                    int in = x >= 100 * k && x < 106 * k && y >= 50 * k && y < 59 * k;
                    if (!in && out[y * side + x] != ref[y * side + x]) { CHECK(0, "k=%d wrote outside the rectangle at %d,%d", k, x, y); goto next; }
                    if (in) { inside++; if ((out[y * side + x] >> 24) && (out[y * side + x] & 0xFFFFFF) == 0xFF8040) { if (y > inkBottom) inkBottom = y; if (y < inkTop) inkTop = y; } }
                }
            CHECK(inside == 54 * k * k, "k=%d rectangle size", k);
            /* the fill's last row is bitmap row 10 = baseline row (top=10 -> row 10 lands on y = base) */
            CHECK(inkBottom == 50 * k + 7 * k, "k=%d baseline: lowest ink row %d want %d", k, inkBottom, 50 * k + 7 * k);
            { int want = 50 * k + 7 * k - 10 > 50 * k ? 50 * k + 7 * k - 10 : 50 * k;   /* bitmap row 0 is 10 rows above the baseline, clipped at the top */
              CHECK(inkTop == want, "k=%d first ink row %d want %d", k, inkTop, want); }
        }
        if (k >= 2) {
            /* edgeClip: a letter as wide as its cell keeps its width (the outline is cut at the sides);
               without it the letter is condensed by 2 x the outline (the "letter-spaced" magc of session 13012) */
            FpgCell c = { 150, 50, 4, 6, 5 };
            FpgGlyph g = { cov, 4 * k, 4 * k, 4 * k };
            FpgStyle s0 = { 0x00FFFF00, 0x00101010, k / 2, 200 }, s1 = s0;
            int pass, xs[2];
            s1.edgeClip = 1;
            memset(cov, 0, sizeof cov);
            for (y = 0; y < 4 * k; y++) for (x = 0; x < 4 * k; x++) cov[y * 4 * k + x] = 255;
            for (pass = 0; pass < 2; pass++) {
                int lo = 1 << 30, hi = -1;
                CHECK(fpg_glyph(out, k, &c, &g, pass ? &s1 : &s0), "k=%d edgeClip glyph refused", k);
                for (y = 50 * k; y < 56 * k; y++)
                    for (x = 150 * k; x < 154 * k; x++)
                        if ((out[y * side + x] & 0xFFFFFF) == 0xFFFF00 && (out[y * side + x] >> 24) == 255) { if (x < lo) lo = x; if (x > hi) hi = x; }
                xs[pass] = hi - lo + 1;
            }
            CHECK(xs[0] == 4 * k - 2 * (k / 2), "k=%d condensed ink width %d want %d", k, xs[0], 4 * k - 2 * (k / 2));
            CHECK(xs[1] == 4 * k, "k=%d edgeClip ink width %d want %d", k, xs[1], 4 * k);
        }
        {
            /* empty glyph clears its rectangle only */
            FpgCell c = { 10, 10, 3, 3, 2 };
            FpgGlyph g = { 0, 0, 0, 0 };
            FpgStyle s = { 0xFFFFFF, 0, 0, 0 };
            fpg_glyph(out, k, &c, &g, &s);
            for (y = 10 * k; y < 13 * k; y++) for (x = 10 * k; x < 13 * k; x++) CHECK(out[y * side + x] == 0, "k=%d empty glyph not cleared", k);
        }
        {
            /* refused: rectangle off the page */
            FpgCell c = { 254, 0, 5, 5, 4 };
            FpgGlyph g = { cov, 2, 2, 1 };
            FpgStyle s = { 0xFFFFFF, 0, 0, 0 };
            CHECK(!fpg_glyph(out, k, &c, &g, &s), "k=%d off-page rectangle accepted", k);
        }
    next:;
    }
    /* size fitting: a synthetic face whose ink scales with em: cap = 0.7 em, ascender = 0.8 em, descender 0.25 em */
    {
        static FpgRec r[4];
        int em, cap = 0;
        r[0].code = 'H'; r[0].c.h = 10; r[0].c.base = 10;   /* cap 10 texels */
        r[1].code = 'l'; r[1].c.h = 10; r[1].c.base = 10;   /* ascender only as tall as the cap: limits the size */
        r[2].code = 'p'; r[2].c.h = 9;  r[2].c.base = 6;    /* x-height 6, descender 3 texels */
        r[3].code = 'J'; r[3].c.h = 10; r[3].c.base = 10;   /* ignored */
        em = fpg_fit(r, 4, 2, synth_metrics, 0, &cap);
        CHECK(cap == 29, "fit: cap em %d want 29", cap);                    /* ceil(20 / 0.7) */
        CHECK(em == 27, "fit: em %d want 27 (ascender (int)(0.8 em) <= 20 + 1)", em);
    }
    /* packing: a page full except a 20x10 hole: two 8x8 cells fit side by side with a free texel around each */
    {
        static unsigned char occ[4 * 256 * 256];
        FpgNewCell c[3];
        int i2, x2, y2;
        memset(occ, 1, sizeof occ);
        for (y2 = 100; y2 < 110; y2++) for (x2 = 50; x2 < 70; x2++) occ[(0x4F - 0x4E) * 65536 + y2 * 256 + x2] = 0;
        memset(c, 0, sizeof c);
        for (i2 = 0; i2 < 3; i2++) { c[i2].w = 8; c[i2].h = 8; }
        CHECK(fpg_pack(occ, c, 2, 0, 0) == 2, "pack: two cells should fit");
        CHECK(c[0].slot == 0x4F && c[1].slot == 0x4F, "pack: page");
        CHECK(c[0].v == 100 && c[1].v == 100 && (c[1].u - c[0].u == 9 || c[0].u - c[1].u == 9), "pack: positions %d,%d %d,%d", c[0].u, c[0].v, c[1].u, c[1].v);
        CHECK(fpg_pack(occ, c + 2, 1, 0, 0) == 0, "pack: a third cell cannot fit");
    }
    /* "over": opaque fill wins, transparent fill shows the edge, half/half */
    CHECK(fpg_over(0x112233, 255, 0x445566, 255) == 0xFF112233, "over opaque");
    CHECK(fpg_over(0x112233, 0, 0x445566, 128) == 0x80445566, "over edge only");
    CHECK((fpg_over(0xFFFFFF, 128, 0x000000, 255) >> 24) == 255, "over alpha");
    {   /* interface layers (fpg_glyph_layers): a 12 x 16 solid block letter in a 9 x 9 cell at k = 4 */
        static unsigned char blk[12 * 16];
        FpgCell cell = { 40, 40, 9, 9, 8 };
        FpgGlyph g; FpgLayers L = { 0xF4E000, 0xFC2C2C, 0x942C5C, 0x1C0000, 0, 2, 2, 2 };
        int side = 1024, rc, cx, cy, inside = 1;
        for (i = 0; i < 12 * 16; i++) blk[i] = 255;
        g.cov = blk; g.cw = 12; g.ch = 16; g.top = 16; g.left1 = 0;
        fpg_base(idx, pal, 4, out);
        memcpy(ref, out, sizeof(DWORD) * side * side);
        rc = fpg_glyph_layers(out, 4, &cell, &g, &L);
        CHECK(rc == 1, "layers: drawn (rc %d)", rc);
        for (y = 0; y < side; y++) for (x = 0; x < side; x++)
            if ((x < 160 || x >= 196 || y < 160 || y >= 196) && out[y * side + x] != ref[y * side + x]) inside = 0;
        CHECK(inside, "layers: nothing written outside the 36 x 36 rectangle");
        /* letter box: 12 x 16, centred: x 172..183; baseline at 160 + 32 - 4 = 188, top 172 */
        cx = 177; cy = 174;
        CHECK((out[cy * side + cx] & 0xFFFFFF) == 0xF4E000 && (out[cy * side + cx] >> 24) == 255, "layers: face on top of the stroke (%08X)", out[cy * side + cx]);
        cy = 187;
        CHECK((out[cy * side + cx] & 0xFFFFFF) == 0xFC2C2C, "layers: shade band at the bottom of the stroke (%08X)", out[cy * side + cx]);
        CHECK((out[180 * side + 171] & 0xFFFFFF) == 0x942C5C, "layers: line ring next to the letter (%08X)", out[180 * side + 171]);
        CHECK((out[180 * side + 169] & 0xFFFFFF) == 0x1C0000, "layers: outer ring outside the line ring (%08X)", out[180 * side + 169]);
        CHECK(out[180 * side + 165] == 0, "layers: cleared outside the rings (%08X)", out[180 * side + 165]);
        L.cut = 1;
        fpg_glyph_layers(out, 4, &cell, &g, &L);
        CHECK(out[180 * side + 177] == 0 && (out[180 * side + 171] >> 24) == 255, "layers: cut-out letter transparent, ring kept (%08X %08X)", out[180 * side + 177], out[180 * side + 171]);
        g.top = 200;   /* letter entirely above the rectangle: left as it was */
        memcpy(out, ref, sizeof(DWORD) * side * side);
        CHECK(fpg_glyph_layers(out, 4, &cell, &g, &L) == 2 && !memcmp(out, ref, sizeof(DWORD) * side * side), "layers: no ink inside -> untouched");
        CHECK(fpg_ui_code('e') == 'E' && fpg_ui_code(0xE9) == 0xC9 && fpg_ui_code(0xF7) == 0xF7 && fpg_ui_code('5') == '5', "layers: lowercase codes drawn as capitals");
    }
    printf(g_fail ? "SELFTEST FAILED (%d)\n" : "SELFTEST PASSED\n", g_fail);
    return g_fail != 0;
}

/* ---------------------------------------------------------------- build */
#define MAXREC 256
static const char* const g_fonts[2] = { "phil", "hbut" };

typedef struct { int font, code, cw, ch, top; unsigned char* cov; } GFile;

static int build(const char* dumps, const char* fonts, const char* glyphs, int k, const char* outdir, const char* newcells) {
    static FpgRec rec[2][MAXREC];
    static FpgRec all[16][MAXREC];
    static const char* const allNames[16] = { "5PxB", "5PxG", "5PxR", "5PxX", "5PxY", "16xB", "16xX", "16xY", "phil", "hbut", "magc", "mgrn", "mred", "cred", "numg", "numr" };
    int nall[16];
    int nrec[2], f, i, slot, side = 256 * k, ng = 0;
    static GFile g[1024];
    static int left1[2][MAXREC];
    static unsigned char oldMask[4][1024 * 1024];
    unsigned char* pal; unsigned char* gb; long glen, p;
    DWORD* pages[4]; DWORD* base[4];
    unsigned char* idx[4];
    FpgStyle style[2] = { { 0 } };
    char path[1024];
    FILE* cells;

    snprintf(path, sizeof path, "%s/SBM_palette.raw", dumps);
    pal = load(path, 0);
    if (!pal) { printf("no %s\n", path); return 2; }
    for (slot = 0; slot < 4; slot++) {
        snprintf(path, sizeof path, "%s/SBM_fontpage_%02X_256x256x1.raw", dumps, 0x4E + slot);
        idx[slot] = load(path, 0);
        if (!idx[slot]) { printf("no %s\n", path); return 2; }
        pages[slot] = malloc(sizeof(DWORD) * side * side);
        base[slot] = malloc(sizeof(DWORD) * side * side);
        fpg_base(idx[slot], pal, k, base[slot]);
        memcpy(pages[slot], base[slot], sizeof(DWORD) * side * side);
    }
    for (f = 0; f < 16; f++) {
        long len; unsigned char* b;
        snprintf(path, sizeof path, "%s/%s.fnt", fonts, allNames[f]);
        b = load(path, &len);
        nall[f] = b ? fpg_read_fnt(b, (int)len, all[f], MAXREC) : 0;
        free(b);
    }
    for (f = 0; f < 2; f++) {
        long len; unsigned char* b;
        snprintf(path, sizeof path, "%s/%s.fnt", fonts, g_fonts[f]);
        b = load(path, &len);
        if (!b) { printf("no %s\n", path); return 2; }
        nrec[f] = fpg_read_fnt(b, (int)len, rec[f], MAXREC);
        free(b);
    }
    /* glyph file: "SBMG", u32 count, u32 edgePx[2], u32 edgeAlpha[2], u32 fill[2], u32 edge[2] (colour codes: see PICK);
       entries: u32 font, code, cw, ch, top, then cw*ch bytes */
    gb = load(glyphs, &glen);
    if (!gb || glen < 40 || memcmp(gb, "SBMG", 4)) { printf("bad glyph file %s\n", glyphs); return 2; }
    {
        DWORD* h = (DWORD*)gb;
        int n = (int)h[1];
        for (f = 0; f < 2; f++) {
            DWORD bright, dark, darkest; int j;
            /* colours per font: all its cells are on pages 0x4E-0x50; sample them page by page */
            {
                static unsigned char tmp[256 * 256];
                int s2;
                /* combine the font's glyphs from the three pages into one index image by copying each cell */
                memset(tmp, 0, sizeof tmp);
                for (j = 0; j < nrec[f]; j++) {
                    FpgRec* r = &rec[f][j]; int yy, xx;
                    for (yy = 0; yy < r->c.h; yy++) for (xx = 0; xx < r->c.w; xx++) {
                        s2 = r->slot - 0x4E;
                        tmp[(r->c.v + yy) * 256 + r->c.u + xx] = idx[s2][(r->c.v + yy) * 256 + r->c.u + xx];
                    }
                }
                {
                    FpgCell whole = { 0, 0, 256, 256, 0 };
                    fpg_colours(tmp, pal, &whole, 1, &bright, &dark, &darkest);
                }
            }
            style[f].edgePx = (int)h[2 + f];
            style[f].edgeAlpha = (int)h[4 + f];
            /* 0xFFFFFFFF = bright, 0xFFFFFFFE = dark, 0xFFFFFFFD = darkest colour of the font; else an RGB value */
#define PICK(v) ((v) == 0xFFFFFFFFu ? bright : (v) == 0xFFFFFFFEu ? dark : (v) == 0xFFFFFFFDu ? darkest : (v))
            style[f].fill = PICK(h[6 + f]);
            style[f].edge = PICK(h[8 + f]);
#undef PICK
            printf("%s: %d cells, fill %06X edge %06X outline %d px alpha %d\n", g_fonts[f], nrec[f], style[f].fill & 0xFFFFFF, style[f].edge & 0xFFFFFF, style[f].edgePx, style[f].edgeAlpha);
        }
        p = 40;
        while (ng < n && p + 20 <= glen && ng < 1024) {
            DWORD* e = (DWORD*)(gb + p);
            g[ng].font = (int)e[0]; g[ng].code = (int)e[1]; g[ng].cw = (int)e[2]; g[ng].ch = (int)e[3]; g[ng].top = (int)e[4];
            g[ng].cov = gb + p + 20;
            p += 20 + (long)g[ng].cw * g[ng].ch;
            ng++;
        }
    }
    /* own metrics: clear the old phil/hbut rectangles, then use the new cells */
    memset(oldMask, 0, sizeof oldMask);
    memset(left1, 0, sizeof left1);
    if (newcells) {
        FILE* nf = fopen(newcells, "r"); char line[256]; int moved = 0;
        if (!nf) { printf("no %s\n", newcells); return 2; }
        while (fgets(line, sizeof line, nf)) {
            char fn[16]; int code, sl, u, v, w, h, yo, le, ba, os, ou, ov, ow, oh, oy;
            if (sscanf(line, "cell %15s %d %x %d %d %d %d %d %d %d %x %d %d %d %d %d", fn, &code, &sl, &u, &v, &w, &h, &yo, &le, &ba, &os, &ou, &ov, &ow, &oh, &oy) != 16) continue;
            f = !strcmp(fn, "phil") ? 0 : !strcmp(fn, "hbut") ? 1 : -1;
            if (f < 0) continue;
            for (i = 0; i < nrec[f]; i++) {
                FpgRec* r = &rec[f][i];
                if (r->code != code || r->slot != os || r->c.u != ou || r->c.v != ov) continue;
                {   /* the old rectangle is no longer used: clear it */
                    int y, x;
                    for (y = r->c.v * k; y < (r->c.v + r->c.h) * k; y++)
                        for (x = r->c.u * k; x < (r->c.u + r->c.w) * k; x++) { pages[r->slot - 0x4E][y * side + x] = 0; oldMask[r->slot - 0x4E][y * side + x] = 1; }
                }
                r->slot = (unsigned char)sl; r->c.u = u; r->c.v = v; r->c.w = w; r->c.h = h; r->c.base = ba; r->yoff = yo;
                left1[f][i] = le + 1; moved++;
                break;
            }
        }
        fclose(nf);
        printf("own metrics: %d glyphs moved to new cells\n", moved);
    }
    /* draw */
    {
        int drawn = 0, missing = 0, refused = 0;
        for (f = 0; f < 2; f++)
            for (i = 0; i < nrec[f]; i++) {
                FpgRec* r = &rec[f][i]; FpgGlyph gl = { 0, 0, 0, 0 }; int j, found = 0;
                if (r->slot == 0x51 && !newcells) continue;     /* phil/hbut are on 4E-50 unless moved */
                for (j = 0; j < ng; j++) if (g[j].font == f && g[j].code == r->code) {
                    gl.cov = g[j].cov; gl.cw = g[j].cw; gl.ch = g[j].ch; gl.top = g[j].top; gl.left1 = left1[f][i]; found = 1; break;
                }
                if (!found) { missing++; continue; }      /* keep the original glyph */
                { int rc = fpg_glyph(pages[r->slot - 0x4E], k, &r->c, &gl, &style[f]);
                  if (rc == 1) drawn++; else if (rc == 2) { missing++; g[j].cw = 0; } else refused++; }
            }
        printf("drawn %d glyphs, %d kept (no TrueType glyph, or it would land outside its rectangle), %d refused\n", drawn, missing, refused);
        CHECK(refused == 0, "%d glyphs refused", refused);
    }
    /* checks: (1) every texel outside phil/hbut cells unchanged; (2) every other font's cells identical to the base;
       (3) cells with a glyph have ink; (4) 'H' and 'x' sit on the baseline */
    {
        static unsigned char mask[4][1024 * 1024];
        int changedOutside = 0, otherChanged = 0, empty = 0, base_bad = 0;
        memset(mask, 0, sizeof mask);
        for (f = 0; f < 2; f++)
            for (i = 0; i < nrec[f]; i++) {
                FpgRec* r = &rec[f][i]; int y, x;
                for (y = r->c.v * k; y < (r->c.v + r->c.h) * k; y++) for (x = r->c.u * k; x < (r->c.u + r->c.w) * k; x++) mask[r->slot - 0x4E][y * side + x] = 1;
            }
        for (slot = 0; slot < 4; slot++) {
            int q;
            for (q = 0; q < side * side; q++) if (!mask[slot][q] && !oldMask[slot][q] && pages[slot][q] != base[slot][q]) changedOutside++;
        }
        for (f = 0; f < 16; f++) {
            if (f == 8 || f == 9) continue;
            for (i = 0; i < nall[f]; i++) {
                FpgRec* r = &all[f][i]; int y, x;
                for (y = r->c.v * k; y < (r->c.v + r->c.h) * k; y++) for (x = r->c.u * k; x < (r->c.u + r->c.w) * k; x++)
                    if (pages[r->slot - 0x4E][y * side + x] != base[r->slot - 0x4E][y * side + x]) otherChanged++;
            }
        }
        for (f = 0; f < 2; f++)
            for (i = 0; i < nrec[f]; i++) {
                FpgRec* r = &rec[f][i]; int y, x, ink = 0, low = -1, j, has = 0;
                for (j = 0; j < ng; j++) if (g[j].font == f && g[j].code == r->code && g[j].cw > 0) has = 1;
                if (!has) continue;
                for (y = 0; y < r->c.h * k; y++) for (x = 0; x < r->c.w * k; x++) {
                    DWORD c = pages[r->slot - 0x4E][(r->c.v * k + y) * side + r->c.u * k + x];
                    if (c >> 24) ink++;
                    if ((c >> 24) >= 128 && y > low) low = y;
                }
                if (!ink) { empty++; if (empty < 5) printf("  no ink: %s code %d\n", g_fonts[f], r->code); }
                if ((r->code == 'H' || r->code == 'x' || r->code == 'n') && ink) {
                    int want = r->c.base * k - 1 + style[f].edgePx;   /* lowest opaque row: the baseline row, plus the outline */
                    if (low < want - 1 - style[f].edgePx || low > want + 1) {   /* the outline's outer rows may stay below half alpha */ base_bad++; printf("  baseline: %s '%c' lowest ink row %d, baseline row %d\n", g_fonts[f], r->code, low, want); }
                }
            }
        CHECK(changedOutside == 0, "%d texels changed outside the phil/hbut rectangles", changedOutside);
        CHECK(otherChanged == 0, "%d texels of other fonts' glyphs changed", otherChanged);
        CHECK(empty == 0, "%d redrawn glyphs have no ink", empty);
        CHECK(base_bad == 0, "%d glyphs off the baseline", base_bad);
    }
    /* write */
    for (slot = 0; slot < 4; slot++) {
        FILE* o;
        snprintf(path, sizeof path, "%s/page_%02X.argb", outdir, 0x4E + slot);
        o = fopen(path, "wb");
        if (o) { fwrite(pages[slot], sizeof(DWORD), (size_t)side * side, o); fclose(o); }
    }
    snprintf(path, sizeof path, "%s/cells.txt", outdir);
    cells = fopen(path, "w");
    if (cells) {
        for (f = 0; f < 2; f++)
            for (i = 0; i < nrec[f]; i++) {
                FpgRec* r = &rec[f][i];
                fprintf(cells, "%s %d %02X %d %d %d %d %d %d\n", g_fonts[f], r->code, r->slot, r->c.u, r->c.v, r->c.w, r->c.h, r->c.base, r->yoff);
            }
        fclose(cells);
    }
    printf(g_fail ? "BUILD CHECKS FAILED (%d)\n" : "BUILD CHECKS PASSED\n", g_fail);
    return g_fail != 0;
}

/* ---------------------------------------------------------------- layout (own metrics) */
typedef struct { int k, lo, hi, nf, file[5], n[5]; int* codes[5]; int* m[5]; int font; } MetTab;
static int tab_metrics(void* ctx, int em, int code, FpgMet* g) {
    MetTab* t = (MetTab*)ctx; int i, f = t->font;
    if (em < t->lo || em > t->hi) return 0;
    for (i = 0; i < t->n[f]; i++) if (t->codes[f][i] == code) {
        int* v = t->m[f] + ((size_t)i * (t->hi - t->lo + 1) + (em - t->lo)) * 5;
        if (v[0] <= 0) return 0;
        g->bbx = v[0]; g->bby = v[1]; g->ox = v[2]; g->oy = v[3]; g->adv = v[4];
        return 1;
    }
    return 0;
}
static FpgRec g_all[16][256]; static int g_nall[16];
static const char* const g_allNames[16] = { "5PxB", "5PxG", "5PxR", "5PxX", "5PxY", "16xB", "16xX", "16xY", "phil", "hbut", "magc", "mgrn", "mred", "cred", "numg", "numr" };
static void load_all(const char* fonts) {
    int f;
    for (f = 0; f < 16; f++) {
        long len; unsigned char* b; char path[1024];
        snprintf(path, sizeof path, "%s/%s.fnt", fonts, g_allNames[f]);
        b = load(path, &len);
        g_nall[f] = b ? fpg_read_fnt(b, (int)len, g_all[f], 256) : 0;
        free(b);
    }
}
static int veto_known(void* ctx, int slot, int u, int v) {   /* a top-left corner some glyph already uses */
    int f, i; (void)ctx;
    for (f = 0; f < 16; f++) for (i = 0; i < g_nall[f]; i++)
        if (g_all[f][i].slot == slot && g_all[f][i].c.u == u && g_all[f][i].c.v == v) return 1;
    return 0;
}
/* own-metrics layout as fpg_own_layout() does it: metrics "SBMM" (phil, hbut) or "SBM3"
   (any of phil/hbut/magc/mgrn/mred: int count, then per font its td_file index before the table) */
static int layout(const char* fonts, const char* metrics, int k, const char* outdir) {
    static unsigned char occ[4 * 256 * 256];
    static FpgNewCell cells[1024];
    long len; unsigned char* b; MetTab t; int f, i, j, nc = 0, start[6], em[5], Bn[5], L[5], placed, own = 0;
    char path[1024]; FILE* o;
    load_all(fonts);
    b = load(metrics, &len);
    if (!b || len < 24 || (memcmp(b, "SBMM", 4) && memcmp(b, "SBM3", 4))) { printf("bad metrics file\n"); return 2; }
    {   int* h = (int*)(b + 4); int* p;
        t.k = h[0]; t.lo = h[1]; t.hi = h[2];
        p = h + 3;
        if (!memcmp(b, "SBM3", 4)) t.nf = *p++; else t.nf = 2;
        if (t.nf < 1 || t.nf > 5) { printf("bad font count\n"); return 2; }
        for (f = 0; f < t.nf; f++) {
            t.file[f] = memcmp(b, "SBM3", 4) ? 8 + f : *p++;
            t.n[f] = *p++;
            t.codes[f] = p; p += t.n[f];
            t.m[f] = p; p += (size_t)t.n[f] * (t.hi - t.lo + 1) * 5;
            own |= 1 << t.file[f];
        }
    }
    if (t.k != k) { printf("metrics are for k=%d\n", t.k); return 2; }
    memset(occ, 0, sizeof occ);
    for (f = 0; f < 16; f++) {
        if (own & (1 << f)) continue;
        for (i = 0; i < g_nall[f]; i++) fpg_occupy(occ, g_all[f][i].slot, g_all[f][i].c.u, g_all[f][i].c.v, g_all[f][i].c.w, g_all[f][i].c.h);
    }
    for (f = 0; f < t.nf; f++) {
        int ff = t.file[f];
        t.font = f;
        em[f] = fpg_choose_em(g_all[ff], g_nall[ff], k, tab_metrics, &t, &Bn[f], &L[f]);
        start[f] = nc;
        nc += fpg_new_cells(g_all[ff], g_nall[ff], k, em[f], Bn[f], L[f], 0, tab_metrics, &t, cells + nc, 1024 - nc);
        printf("%s: em %d px, baseline %d (was %d), line %d, %d cells\n", g_allNames[ff], em[f], Bn[f],
               g_all[ff][0].c.base + g_all[ff][0].yoff, L[f], nc - start[f]);
        CHECK(em[f] > 0, "%s: no em size", g_allNames[ff]);
    }
    start[t.nf] = nc;
    for (f = 0; f < t.nf; f++) {                 /* glyphs not in the face keep their original rectangle (as the DLL does) */
        int ff = t.file[f], kept = 0;
        for (i = 0; i < g_nall[ff]; i++) {
            int has = 0;
            for (j = start[f]; j < start[f + 1]; j++) if (cells[j].src == i) { has = 1; break; }
            if (!has) { fpg_occupy(occ, g_all[ff][i].slot, g_all[ff][i].c.u, g_all[ff][i].c.v, g_all[ff][i].c.w, g_all[ff][i].c.h); kept++; }
        }
        if (kept) printf("%s: %d glyph(s) not in the face keep their rectangles\n", g_allNames[ff], kept);
    }
    placed = fpg_pack(occ, cells, nc, veto_known, 0);
    printf("packed %d of %d cells\n", placed, nc);
    CHECK(placed == nc, "not every cell packed");
    /* checks: gap to other fonts' glyphs, no overlap between cells, line height kept */
    for (i = 0; i < nc; i++) {
        FpgNewCell* c = &cells[i];
        for (f = 0; f < 16; f++) {
            if (own & (1 << f)) continue;
            for (j = 0; j < g_nall[f]; j++) {
                const FpgRec* r = &g_all[f][j];
                if (r->slot != c->slot) continue;
                if (c->u <= r->c.u + r->c.w && r->c.u <= c->u + c->w && c->v <= r->c.v + r->c.h && r->c.v <= c->v + c->h)
                    { CHECK(0, "cell %d (code %d) touches %s code %d", i, c->code, g_allNames[f], r->code); break; }
            }
        }
        for (j = i + 1; j < nc; j++) {
            FpgNewCell* d = &cells[j];
            if (d->slot == c->slot && c->u < d->u + d->w && d->u < c->u + c->w && c->v < d->v + d->h && d->v < c->v + c->h)
                CHECK(0, "cells %d and %d overlap", i, j);
        }
    }
    for (f = 0; f < t.nf; f++) {
        int mx = 0;
        for (i = start[f]; i < start[f + 1]; i++) if (cells[i].yoff + cells[i].h > mx) mx = cells[i].yoff + cells[i].h;
        CHECK(mx == L[f], "%s line height %d, was %d", g_allNames[t.file[f]], mx, L[f]);
    }
    snprintf(path, sizeof path, "%s/newcells.txt", outdir);
    o = fopen(path, "w");
    if (o) {
        for (f = 0; f < t.nf; f++) fprintf(o, "em %s %d %d %d\n", g_allNames[t.file[f]], em[f], Bn[f], L[f]);
        for (f = 0; f < t.nf; f++)
            for (i = start[f]; i < start[f + 1]; i++) {
                FpgNewCell* c = &cells[i]; const FpgRec* r = &g_all[t.file[f]][c->src];
                fprintf(o, "cell %s %d %02X %d %d %d %d %d %d %d  %02X %d %d %d %d %d\n", g_allNames[t.file[f]], c->code, c->slot, c->u, c->v, c->w, c->h,
                        c->yoff, c->left, c->base, r->slot, r->c.u, r->c.v, r->c.w, r->c.h, r->yoff);
            }
        fclose(o);
    }
    printf(g_fail ? "LAYOUT CHECKS FAILED (%d)\n" : "LAYOUT CHECKS PASSED\n", g_fail);
    return g_fail != 0;
}

/* ---------------------------------------------------------------- ui: interface fonts in layers
 * test_font_pages ui <dumps> <fonts> <glyphs-ui.bin> <k> <outdir>
 * glyphs-ui.bin: "SBMU", u32 count, u32 linePx, outerPx, shadePx; entries: u32 font (0 16xY,
 * 1 16xB, 2 16xX), code, cw, ch, top, then cw*ch coverage bytes (tools/ui_font_preview.py).
 * Draws every glyph of the three fonts with fpg_glyph_layers in the colours fpg_ui_colours finds,
 * writes page_<slot>.argb, prints the colours, and checks: no texel outside the three fonts'
 * rectangles changed (other fonts untouched); every letter, digit and mark drawn has ink;
 * 16xY's face is yellow and its shade red, 16xX is cut out, 16xB is not. */
static int ui(const char* dumps, const char* fonts, const char* glyphs, int k, const char* outdir) {
    static const char* const names[3] = { "16xY", "16xB", "16xX" };
    static FpgRec rec[3][MAXREC];
    static unsigned char mask[4][1024 * 1024];
    int nrec[3], f, i, slot, side = 256 * k, drawn = 0, kept = 0, refused = 0, noink = 0;
    unsigned char* idx[4]; unsigned char* pal; unsigned char* gb; long glen, p;
    DWORD* pages[4]; DWORD* base[4];
    FpgLayers L[3];
    char path[1024];
    snprintf(path, sizeof path, "%s/SBM_palette.raw", dumps);
    if (!(pal = load(path, 0))) { printf("no %s\n", path); return 2; }
    for (slot = 0; slot < 4; slot++) {
        snprintf(path, sizeof path, "%s/SBM_fontpage_%02X_256x256x1.raw", dumps, 0x4E + slot);
        if (!(idx[slot] = load(path, 0))) { printf("no %s\n", path); return 2; }
        pages[slot] = malloc(sizeof(DWORD) * side * side); base[slot] = malloc(sizeof(DWORD) * side * side);
        fpg_base(idx[slot], pal, k, base[slot]);
        memcpy(pages[slot], base[slot], sizeof(DWORD) * side * side);
    }
    gb = load(glyphs, &glen);
    if (!gb || glen < 20 || memcmp(gb, "SBMU", 4)) { printf("bad glyph file %s\n", glyphs); return 2; }
    memset(mask, 0, sizeof mask);
    for (f = 0; f < 3; f++) {
        long len; unsigned char* b;
        snprintf(path, sizeof path, "%s/%s.fnt", fonts, names[f]);
        if (!(b = load(path, &len))) { printf("no %s\n", path); return 2; }
        nrec[f] = fpg_read_fnt(b, (int)len, rec[f], MAXREC); free(b);
        fpg_ui_colours((const unsigned char* const*)idx, pal, rec[f], nrec[f], &L[f]);
        L[f].linePx = (int)((DWORD*)gb)[2]; L[f].outerPx = (int)((DWORD*)gb)[3]; L[f].shadePx = (int)((DWORD*)gb)[4];
        printf("%s: %d cells, face %06X shade %06X line %06X outer %06X%s\n", names[f], nrec[f], L[f].face, L[f].shade, L[f].line, L[f].outer, L[f].cut ? " (cut out)" : "");
        for (i = 0; i < nrec[f]; i++) {
            FpgRec* r = &rec[f][i]; int x, y;
            for (y = r->c.v * k; y < (r->c.v + r->c.h) * k; y++) for (x = r->c.u * k; x < (r->c.u + r->c.w) * k; x++) mask[r->slot - 0x4E][y * side + x] = 1;
        }
    }
    CHECK((L[0].face >> 16) > 200 && ((L[0].face >> 8) & 0xFF) > 150 && (L[0].face & 0xFF) < 80, "16xY face not yellow (%06X)", L[0].face);
    CHECK((L[0].shade >> 16) > 200 && ((L[0].shade >> 8) & 0xFF) < 100, "16xY shade not red (%06X)", L[0].shade);
    CHECK(!L[0].cut && !L[1].cut && L[2].cut, "cut-out flags %d %d %d (expected 0 0 1)", L[0].cut, L[1].cut, L[2].cut);
    {
        DWORD* h = (DWORD*)gb; int n = (int)h[1], e;
        p = 20;
        for (e = 0; e < n && p + 20 <= glen; e++) {
            DWORD* q = (DWORD*)(gb + p); FpgGlyph g; int rc, code;
            f = (int)q[0]; code = (int)q[1]; g.cw = (int)q[2]; g.ch = (int)q[3]; g.top = (int)q[4]; g.cov = gb + p + 20; g.left1 = 0;
            p += 20 + (long)g.cw * g.ch;
            if (f < 0 || f > 2) continue;
            for (i = 0; i < nrec[f]; i++) {
                FpgRec* r = &rec[f][i]; DWORD* pg; int x, y, ink = 0;
                if (r->code != code) continue;
                pg = pages[r->slot - 0x4E];
                rc = fpg_glyph_layers(pg, k, &r->c, &g, &L[f]);
                if (rc == 1) drawn++; else if (rc == 2) { kept++; continue; } else { refused++; continue; }
                for (y = r->c.v * k; y < (r->c.v + r->c.h) * k; y++) for (x = r->c.u * k; x < (r->c.u + r->c.w) * k; x++) if (pg[y * side + x] >> 24) ink = 1;
                if (!ink) { noink++; if (noink < 5) printf("no ink: %s code %d\n", names[f], code); }
            }
        }
    }
    for (slot = 0; slot < 4; slot++) {
        int x, y, bad = 0; FILE* o;
        for (y = 0; y < side; y++) for (x = 0; x < side; x++) if (!mask[slot][y * side + x] && pages[slot][y * side + x] != base[slot][y * side + x]) bad++;
        CHECK(!bad, "slot %02X: %d texels changed outside the interface fonts' rectangles", 0x4E + slot, bad);
        snprintf(path, sizeof path, "%s/page_%02X.argb", outdir, 0x4E + slot);
        if ((o = fopen(path, "wb"))) { fwrite(pages[slot], sizeof(DWORD), (size_t)side * side, o); fclose(o); }
    }
    CHECK(drawn > 400 && !refused && !noink, "drawn %d, kept %d, refused %d, without ink %d", drawn, kept, refused, noink);
    printf("ui: %d glyphs drawn in layers, %d kept (no ink inside), %d refused\n", drawn, kept, refused);
    printf(g_fail ? "UI FAILED (%d)\n" : "UI PASSED\n", g_fail);
    return g_fail != 0;
}

/* combat numbers (numr / numg, [UI] NumberFont): glyph file "SBMN": u32 em, u32 n, u32 tops[301]
   ('8' rows above the baseline at em 0..300), then n entries u32 font (0 numr, 1 numg), code, cw,
   ch, top + cw*ch coverage bytes rendered at em. Checks fpg_num_em picks em, the colours, every
   glyph drawn with ink, nothing outside the two fonts' rectangles changed; writes the pages. */
static const DWORD* g_numTops;
static int num_met(void* ctx, int em, int code, int* top, int* height) {
    (void)ctx; if (code != '8' || em < 0 || em > 300) return 0;
    *top = (int)g_numTops[em]; *height = *top; return *top > 0;
}
static int numbers(const char* dumps, const char* fonts, const char* glyphs, int k, const char* outdir) {
    static const char* const names[2] = { "numr", "numg" };
    static FpgRec rec[2][MAXREC];
    static unsigned char mask[4][1024 * 1024];
    int nrec[2], f, i, slot, side = 256 * k, drawn = 0, kept = 0, refused = 0, noink = 0, em, base;
    unsigned char* idx[4]; unsigned char* pal; unsigned char* gb; long glen, p;
    DWORD* pages[4]; DWORD* base4[4];
    FpgStyle st[2];
    char path[1024];
    memset(st, 0, sizeof st);
    snprintf(path, sizeof path, "%s/SBM_palette.raw", dumps);
    if (!(pal = load(path, 0))) { printf("no %s\n", path); return 2; }
    for (slot = 0; slot < 4; slot++) {
        snprintf(path, sizeof path, "%s/SBM_fontpage_%02X_256x256x1.raw", dumps, 0x4E + slot);
        if (!(idx[slot] = load(path, 0))) { printf("no %s\n", path); return 2; }
        pages[slot] = malloc(sizeof(DWORD) * side * side); base4[slot] = malloc(sizeof(DWORD) * side * side);
        fpg_base(idx[slot], pal, k, base4[slot]);
        memcpy(pages[slot], base4[slot], sizeof(DWORD) * side * side);
    }
    gb = load(glyphs, &glen);
    if (!gb || glen < 8 + 301 * 4 || memcmp(gb, "SBMN", 4)) { printf("bad glyph file %s\n", glyphs); return 2; }
    g_numTops = (const DWORD*)(gb + 12);
    memset(mask, 0, sizeof mask);
    for (f = 0; f < 2; f++) {
        long len; unsigned char* b; static FpgCell cells[32]; int n = 0, s = -1; DWORD fill = 0, edge = 0;
        snprintf(path, sizeof path, "%s/%s.fnt", fonts, names[f]);
        if (!(b = load(path, &len))) { printf("no %s\n", path); return 2; }
        nrec[f] = fpg_read_fnt(b, (int)len, rec[f], MAXREC); free(b);
        for (i = 0; i < nrec[f]; i++) if (rec[f][i].code == '0') s = rec[f][i].slot - 0x4E;
        for (i = 0; i < nrec[f]; i++) if (rec[f][i].code >= '0' && rec[f][i].code <= '9' && rec[f][i].slot == 0x4E + s) cells[n++] = rec[f][i].c;
        CHECK(s >= 0 && n == 10, "%s: digits not on one page (%d)", names[f], n);
        CHECK(fpg_num_colours(idx[s], pal, cells, n, &fill, &edge), "%s: no two colours", names[f]);
        st[f].fill = fill; st[f].edge = edge; st[f].edgePx = k; st[f].edgeAlpha = 255;
        printf("%s: %d cells, fill %06X outline %06X\n", names[f], nrec[f], st[f].fill & 0xFFFFFF, st[f].edge & 0xFFFFFF);
        for (i = 0; i < nrec[f]; i++) {
            FpgRec* r = &rec[f][i]; int x, y;
            for (y = r->c.v * k; y < (r->c.v + r->c.h) * k; y++) for (x = r->c.u * k; x < (r->c.u + r->c.w) * k; x++) mask[r->slot - 0x4E][y * side + x] = 1;
        }
    }
    CHECK(nrec[0] == 13 && nrec[1] == 13, "record counts %d %d (13 drawn glyphs each: + - 0-9 =)", nrec[0], nrec[1]);
    CHECK((st[0].fill & 0xFFFFFF) == 0xECB018 && (st[0].edge & 0xFFFFFF) == 0xC40000, "numr colours %06X / %06X (yellow / red expected)", st[0].fill & 0xFFFFFF, st[0].edge & 0xFFFFFF);
    CHECK((st[1].fill & 0xFFFFFF) == 0x80CC54 && (st[1].edge & 0xFFFFFF) == 0x0C3818, "numg colours %06X / %06X (green / dark green expected)", st[1].fill & 0xFFFFFF, st[1].edge & 0xFFFFFF);
    em = fpg_num_em(rec[0], nrec[0], k, num_met, 0);
    base = fpg_num_base(rec[0], nrec[0]);
    CHECK(em == (int)((const DWORD*)gb)[1], "em %d, glyphs rendered at %d", em, (int)((const DWORD*)gb)[1]);
    CHECK(base == 12, "digit baseline row %d (expected 12: y offset 5 + 8 rows - the outline row)", base);
    CHECK(g_numTops[em] <= (DWORD)(6 * k) && g_numTops[em] >= (DWORD)(6 * k - 1), "'8' %u px tall at em %d (want %d)", g_numTops[em], em, 6 * k);
    {
        int n = (int)((const DWORD*)gb)[2], e;
        p = 12 + 301 * 4;
        for (e = 0; e < n && p + 20 <= glen; e++) {
            const DWORD* q = (const DWORD*)(gb + p); FpgGlyph g; int rc, code;
            f = (int)q[0]; code = (int)q[1]; g.cw = (int)q[2]; g.ch = (int)q[3]; g.top = (int)q[4]; g.cov = gb + p + 20; g.left1 = 0;
            p += 20 + (long)g.cw * g.ch;
            if (f < 0 || f > 1) continue;
            for (i = 0; i < nrec[f]; i++) {
                FpgRec* r = &rec[f][i]; FpgCell c = r->c; DWORD* pg; int x, y, ink = 0;
                if (r->code != code) continue;
                pg = pages[r->slot - 0x4E];
                c.base = base - r->yoff;
                rc = fpg_glyph(pg, k, &c, &g, &st[f]);
                if (rc == 1) drawn++; else if (rc == 2) { kept++; continue; } else { refused++; continue; }
                for (y = r->c.v * k; y < (r->c.v + r->c.h) * k; y++) for (x = r->c.u * k; x < (r->c.u + r->c.w) * k; x++) if ((pg[y * side + x] & 0xFFFFFF) == (st[f].fill & 0xFFFFFF) && (pg[y * side + x] >> 24) == 255) ink = 1;
                if (!ink) { noink++; printf("no fill: %s '%c'\n", names[f], code); }
            }
        }
    }
    for (slot = 0; slot < 4; slot++) {
        int x, y, bad = 0; FILE* o;
        for (y = 0; y < side; y++) for (x = 0; x < side; x++) if (!mask[slot][y * side + x] && pages[slot][y * side + x] != base4[slot][y * side + x]) bad++;
        CHECK(!bad, "slot %02X: %d texels changed outside the number fonts' rectangles", 0x4E + slot, bad);
        snprintf(path, sizeof path, "%s/page_%02X.argb", outdir, 0x4E + slot);
        if ((o = fopen(path, "wb"))) { fwrite(pages[slot], sizeof(DWORD), (size_t)side * side, o); fclose(o); }
    }
    CHECK(drawn == 26 && !refused && !noink && !kept, "drawn %d (want 26), kept %d, refused %d, without fill %d", drawn, kept, refused, noink);
    printf("numbers: em %d, baseline row %d, %d glyphs drawn, %d kept, %d refused\n", em, base, drawn, kept, refused);
    printf(g_fail ? "NUMBERS FAILED (%d)\n" : "NUMBERS PASSED\n", g_fail);
    return g_fail != 0;
}

/* credits font (cred, [UI] CreditsFont): glyph file "SBMC": u32 em, u32 n, u32 tops[301] ('H' rows
   above the baseline at em 0..300), then n entries u32 code, cw, ch, top + coverage, rendered at em
   (codes the face lacks are left out: their rectangles must stay). Checks fpg_ui_em picks em, the
   fill is the game's white, every given glyph drawn with fill, nothing outside cred's rectangles
   changed, the left-out codes unchanged; writes the pages. */
static int cred_met(void* ctx, int em, int code, int* top, int* height) {
    (void)ctx; if (code != 'H' || em < 0 || em > 300) return 0;
    *top = (int)g_numTops[em]; *height = *top; return *top > 0;
}
static int credits(const char* dumps, const char* fonts, const char* glyphs, int k, const char* outdir) {
    static FpgRec rec[MAXREC];
    static unsigned char mask[4][1024 * 1024], given[MAXREC];
    int nrec, i, slot, side = 256 * k, drawn = 0, kept = 0, refused = 0, noink = 0, em, n, e, s = -1, nc = 0;
    unsigned char* idx[4]; unsigned char* pal; unsigned char* gb; long glen, p, len;
    DWORD* pages[4]; DWORD* base4[4]; DWORD fill = 0, edge = 0;
    static FpgCell cells[MAXREC];
    FpgStyle st;
    char path[1024];
    unsigned char* b;
    memset(&st, 0, sizeof st); memset(given, 0, sizeof given);
    snprintf(path, sizeof path, "%s/SBM_palette.raw", dumps);
    if (!(pal = load(path, 0))) { printf("no %s\n", path); return 2; }
    for (slot = 0; slot < 4; slot++) {
        snprintf(path, sizeof path, "%s/SBM_fontpage_%02X_256x256x1.raw", dumps, 0x4E + slot);
        if (!(idx[slot] = load(path, 0))) { printf("no %s\n", path); return 2; }
        pages[slot] = malloc(sizeof(DWORD) * side * side); base4[slot] = malloc(sizeof(DWORD) * side * side);
        fpg_base(idx[slot], pal, k, base4[slot]);
        memcpy(pages[slot], base4[slot], sizeof(DWORD) * side * side);
    }
    gb = load(glyphs, &glen);
    if (!gb || glen < 12 + 301 * 4 || memcmp(gb, "SBMC", 4)) { printf("bad glyph file %s\n", glyphs); return 2; }
    g_numTops = (const DWORD*)(gb + 12);
    snprintf(path, sizeof path, "%s/cred.fnt", fonts);
    if (!(b = load(path, &len))) { printf("no %s\n", path); return 2; }
    nrec = fpg_read_fnt(b, (int)len, rec, MAXREC); free(b);
    memset(mask, 0, sizeof mask);
    for (i = 0; i < nrec; i++) {
        int x, y;
        if (rec[i].code == 'H') s = rec[i].slot - 0x4E;
        for (y = rec[i].c.v * k; y < (rec[i].c.v + rec[i].c.h) * k; y++) for (x = rec[i].c.u * k; x < (rec[i].c.u + rec[i].c.w) * k; x++) mask[rec[i].slot - 0x4E][y * side + x] = 1;
    }
    CHECK(nrec == 191, "cred: %d drawn records (191 expected)", nrec);
    CHECK(s == 0, "cred 'H' on slot %02X (4E expected)", 0x4E + s);
    for (i = 0; i < nrec; i++) if (rec[i].slot == 0x4E + s) cells[nc++] = rec[i].c;
    CHECK(fpg_num_colours(idx[s], pal, cells, nc, &fill, &edge), "cred: no two colours");
    CHECK((fill & 0xFFFFFF) == 0xFCFCFC, "cred fill %06X (FCFCFC expected)", fill & 0xFFFFFF);
    st.fill = fill; st.edge = edge; st.edgePx = 0; st.edgeAlpha = 0; st.edgeClip = 1;
    em = fpg_ui_em(rec, nrec, k, cred_met, 0, 0);
    CHECK(em == (int)((const DWORD*)gb)[1], "em %d, glyphs rendered at %d", em, (int)((const DWORD*)gb)[1]);
    CHECK(g_numTops[em] <= (DWORD)(19 * k) && g_numTops[em] >= (DWORD)(19 * k - 1), "'H' %u px tall at em %d (want %d)", g_numTops[em], em, 19 * k);
    n = (int)((const DWORD*)gb)[2];
    p = 12 + 301 * 4;
    for (e = 0; e < n && p + 16 <= glen; e++) {
        const DWORD* q = (const DWORD*)(gb + p); FpgGlyph g; int rc, code;
        code = (int)q[0]; g.cw = (int)q[1]; g.ch = (int)q[2]; g.top = (int)q[3]; g.cov = gb + p + 16; g.left1 = 0;
        p += 16 + (long)g.cw * g.ch;
        for (i = 0; i < nrec; i++) {
            FpgRec* r = &rec[i]; DWORD* pg; int x, y, ink = 0;
            if (r->code != code) continue;
            given[i] = 1;
            pg = pages[r->slot - 0x4E];
            rc = fpg_glyph(pg, k, &r->c, &g, &st);
            if (rc == 1) drawn++; else if (rc == 2) { kept++; printf("kept: %d (%dx%d top %d)\n", code, g.cw, g.ch, g.top); continue; } else { refused++; printf("refused: %d (%dx%d)\n", code, g.cw, g.ch); continue; }
            for (y = r->c.v * k; y < (r->c.v + r->c.h) * k; y++) for (x = r->c.u * k; x < (r->c.u + r->c.w) * k; x++) if ((pg[y * side + x] & 0xFFFFFF) == (fill & 0xFFFFFF) && (pg[y * side + x] >> 24) > 200) ink = 1;
            if (!ink) { noink++; printf("no fill: %d\n", code); }
        }
    }
    for (i = 0; i < nrec; i++) if (!given[i]) {                       /* left out: the game's letter stays */
        FpgRec* r = &rec[i]; int x, y, d = 0; DWORD* pg = pages[r->slot - 0x4E]; DWORD* bs = base4[r->slot - 0x4E];
        for (y = r->c.v * k; y < (r->c.v + r->c.h) * k; y++) for (x = r->c.u * k; x < (r->c.u + r->c.w) * k; x++) if (pg[y * side + x] != bs[y * side + x]) d++;
        CHECK(!d, "code %d (not in the face) changed %d texels", r->code, d);
    }
    for (slot = 0; slot < 4; slot++) {
        int x, y, bad = 0; FILE* o;
        for (y = 0; y < side; y++) for (x = 0; x < side; x++) if (!mask[slot][y * side + x] && pages[slot][y * side + x] != base4[slot][y * side + x]) bad++;
        CHECK(!bad, "slot %02X: %d texels changed outside cred's rectangles", 0x4E + slot, bad);
        snprintf(path, sizeof path, "%s/page_%02X.argb", outdir, 0x4E + slot);
        if ((o = fopen(path, "wb"))) { fwrite(pages[slot], sizeof(DWORD), (size_t)side * side, o); fclose(o); }
    }
    CHECK(drawn + kept == n && kept <= 2 && !refused && !noink, "drawn %d of %d, kept %d (ink outside the rectangle, at most 2: '~'), refused %d, without fill %d", drawn, n, kept, refused, noink);
    printf("credits: em %d, %d glyphs drawn, %d left to the game, %d refused\n", em, drawn, nrec - drawn, refused);
    printf(g_fail ? "CREDITS FAILED (%d)\n" : "CREDITS PASSED\n", g_fail);
    return g_fail != 0;
}

int main(int argc, char** argv) {
    if (argc >= 7 && !strcmp(argv[1], "credits")) return credits(argv[2], argv[3], argv[4], atoi(argv[5]), argv[6]);
    if (argc >= 7 && !strcmp(argv[1], "numbers")) return numbers(argv[2], argv[3], argv[4], atoi(argv[5]), argv[6]);
    if (argc >= 7 && !strcmp(argv[1], "ui")) return ui(argv[2], argv[3], argv[4], atoi(argv[5]), argv[6]);
    if (argc >= 2 && !strcmp(argv[1], "selftest")) return selftest();
    if (argc >= 6 && !strcmp(argv[1], "layout")) return layout(argv[2], argv[3], atoi(argv[4]), argv[5]);
    if (argc >= 7 && !strcmp(argv[1], "build")) return build(argv[2], argv[3], argv[4], atoi(argv[5]), argv[6], argc >= 8 ? argv[7] : 0);
    printf("usage: test_font_pages selftest | layout <fonts> <metrics.bin> <k> <outdir> | build <dumps> <fonts> <glyphs.bin> <k> <outdir> [newcells.txt]\n");
    return 2;
}
