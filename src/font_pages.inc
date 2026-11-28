/* font_pages.inc - HD font pages (docs/HD_FONTS_PLAN.md, step 2).
 *
 * Builds a k-times larger copy of one of the game's font pages (renderer
 * slots 0x4E-0x51, 256x256 8-bit palette indices) in A8R8G8B8, and redraws
 * chosen glyph rectangles from anti-aliased coverage bitmaps (TrueType
 * glyphs rendered by the caller: GDI in the game, FreeType in the offline
 * tools). Every glyph keeps its rectangle (scaled by k), so the texture
 * coordinates the game computes (texel / 256) still address it.
 *
 * Plain C on memory it is given (no Windows, no C runtime): the DLL and the
 * offline test (tests/fonts/test_font_pages.c) include the same code.
 *
 *   fpg_base()     enlarged copy of the whole page: index 0 -> transparent
 *                  (0x00000000), any other index -> opaque palette colour,
 *                  each texel repeated k x k (what the renderer itself does
 *                  with a larger surface: nearest sampling);
 *   fpg_glyph()    clears one k-scaled rectangle and draws a coverage
 *                  bitmap into it (or leaves the original if none of the
 *                  letter would land inside): horizontally centred (condensed if wider
 *                  than the rectangle), its baseline on the original font's
 *                  baseline, clipped to the rectangle, with an optional
 *                  outline under the letter; nothing outside the rectangle
 *                  is written;
 *   fpg_colours()  the fill and edge colours of a font, from the original
 *                  page (most frequent bright / dark colour in its glyphs).
 *
 * Colours are 0xAARRGGBB, not premultiplied (the game blends with source
 * alpha). Page side = 256 * k; k = 1..4.
 */
#define FPG_FN     static __attribute__((unused))
#define FPG_SIDE   256
#define FPG_MAXK   4
#define FPG_MAXW   (16 * FPG_MAXK * 3)   /* largest coverage bitmap side accepted (cred's widest letters: ~124 px at k = 4) */

typedef struct {
    int u, v, w, h;      /* glyph rectangle in the original page (texels) */
    int base;            /* baseline, from the top of the rectangle, in original texels (may lie below it) */
} FpgCell;

typedef struct {
    const unsigned char* cov;  /* coverage 0..255, cw x ch, rows of cw bytes */
    int cw, ch;
    int top;                   /* row of the bitmap that sits on y = baseline - top (pixels above the baseline) */
    int left1;                 /* 0 = centred in the rectangle, else the ink starts left1 - 1 pixels from its left edge */
} FpgGlyph;

typedef struct {
    DWORD fill;          /* letter colour (alpha ignored) */
    DWORD edge;          /* outline colour (alpha ignored) */
    int   edgePx;        /* outline radius in output pixels, 0 = none */
    int   edgeAlpha;     /* outline strength 0..255 */
    int   edgeClip;      /* 1 = the letter keeps its width and the outline is cut at the cell's sides;
                            0 = the letter is condensed to leave room for the outline on both sides */
} FpgStyle;

FPG_FN void fpg_base(const unsigned char* idx, const unsigned char* pal, int k, DWORD* out) {
    int x, y, dx, dy, side = FPG_SIDE * k;
    for (y = 0; y < FPG_SIDE; y++)
        for (x = 0; x < FPG_SIDE; x++) {
            int i = idx[y * FPG_SIDE + x];
            DWORD c = i ? 0xFF000000u | ((DWORD)pal[3 * i] << 16) | ((DWORD)pal[3 * i + 1] << 8) | pal[3 * i + 2] : 0;
            for (dy = 0; dy < k; dy++)
                for (dx = 0; dx < k; dx++) out[(y * k + dy) * side + x * k + dx] = c;
        }
}

/* "a over b" for non-premultiplied colours with 8-bit alpha */
FPG_FN DWORD fpg_over(DWORD rgbA, int aA, DWORD rgbB, int aB) {
    int ao = aA + aB * (255 - aA) / 255, ch, r = 0;
    if (ao <= 0) return 0;
    for (ch = 0; ch < 3; ch++) {
        int ca = (rgbA >> (16 - 8 * ch)) & 0xFF, cb = (rgbB >> (16 - 8 * ch)) & 0xFF;
        int c = (ca * aA + cb * aB * (255 - aA) / 255 + ao / 2) / ao;
        if (c > 255) c = 255;
        r = (r << 8) | c;
    }
    return ((DWORD)ao << 24) | (DWORD)r;
}

/* Draws one glyph. Returns 1 if drawn, 2 if left untouched because none of
   the letter would land inside the rectangle, 0 if the arguments were refused. */
FPG_FN int fpg_glyph(DWORD* out, int k, const FpgCell* c, const FpgGlyph* g, const FpgStyle* s) {
    static unsigned char fit[FPG_MAXW * FPG_MAXW];      /* coverage after condensing, rw x ch */
    static unsigned char ring[FPG_MAXW * FPG_MAXW];     /* outline coverage, same size */
    int side = FPG_SIDE * k, W, H, x0, y0, rw, ox, oy, x, y;
    if (k < 1 || k > FPG_MAXK || !c || !g || !s) return 0;
    if (c->u < 0 || c->v < 0 || c->w <= 0 || c->h <= 0 || c->u + c->w > FPG_SIDE || c->v + c->h > FPG_SIDE) return 0;
    W = c->w * k; H = c->h * k; x0 = c->u * k; y0 = c->v * k;
    if (g->cov && (g->cw > FPG_MAXW || g->ch > FPG_MAXW)) return 0;
    if (g->cov && g->cw > 0 && g->ch > 0) {
        /* would any ink land inside the rectangle? (a mark placed outside it, e.g. an accent
           that sits elsewhere in the original cell, would leave the glyph blank: keep the original) */
        int top = c->base * k - g->top, any = 0;
        for (y = 0; y < g->ch && !any; y++) {
            if (top + y < 0 || top + y >= H) continue;
            for (x = 0; x < g->cw; x++) if (g->cov[y * g->cw + x]) { any = 1; break; }
        }
        if (!any) return 2;
    }
    for (y = 0; y < H; y++) for (x = 0; x < W; x++) out[(y0 + y) * side + x0 + x] = 0;
    if (!g->cov || g->cw <= 0 || g->ch <= 0) return 1;                 /* empty glyph */

    /* condense horizontally (box filter) if the letter (plus its outline, unless edgeClip) is wider than the rectangle */
    rw = g->cw;
    if (rw + (s->edgeClip ? 0 : 2 * s->edgePx) > W) rw = W - (s->edgeClip ? 0 : 2 * s->edgePx);
    if (rw < 1) rw = 1;
    for (y = 0; y < g->ch; y++)
        for (x = 0; x < rw; x++) {
            /* source span [x*cw/rw, (x+1)*cw/rw) in 1/rw units */
            int a = x * g->cw, b = (x + 1) * g->cw, sum = 0, p;
            for (p = a / rw; p * rw < b && p < g->cw; p++) {
                int lo = p * rw > a ? p * rw : a, hi = (p + 1) * rw < b ? (p + 1) * rw : b;
                sum += g->cov[y * g->cw + p] * (hi - lo);
            }
            fit[y * rw + x] = (unsigned char)((sum + g->cw / 2) / g->cw);
        }
    /* outline: max coverage within edgePx (square-ish disc) */
    if (s->edgePx > 0) {
        int R = s->edgePx, gw = rw + 2 * R, gh = g->ch + 2 * R;
        if (gw > FPG_MAXW || gh > FPG_MAXW) return 0;
        for (y = 0; y < gh; y++)
            for (x = 0; x < gw; x++) {
                int m = 0, dx, dy;
                for (dy = -R; dy <= R; dy++)
                    for (dx = -R; dx <= R; dx++) {
                        int sx = x - R + dx, sy = y - R + dy, v;
                        if (dx * dx + dy * dy > R * R + R) continue;
                        if (sx < 0 || sy < 0 || sx >= rw || sy >= g->ch) continue;
                        v = fit[sy * rw + sx];
                        if (v > m) m = v;
                    }
                ring[y * gw + x] = (unsigned char)(m * s->edgeAlpha / 255);
            }
    }
    /* place: centred horizontally, bitmap row `top` on the baseline */
    ox = x0 + (W - rw) / 2;
    if (g->left1 > 0) { int l = g->left1 - 1; if (l + rw > W) l = W - rw; if (l < 0) l = 0; ox = x0 + l; }
    oy = y0 + c->base * k - g->top;
    for (y = -s->edgePx; y < g->ch + s->edgePx; y++) {
        int py = oy + y;
        if (py < y0 || py >= y0 + H) continue;
        for (x = -s->edgePx; x < rw + s->edgePx; x++) {
            int px = ox + x, a = 0, e = 0;
            if (px < x0 || px >= x0 + W) continue;
            if (x >= 0 && x < rw && y >= 0 && y < g->ch) a = fit[y * rw + x];
            if (s->edgePx > 0) e = ring[(y + s->edgePx) * (rw + 2 * s->edgePx) + x + s->edgePx];
            if (a || e) out[py * side + px] = fpg_over(s->fill & 0xFFFFFF, a, s->edge & 0xFFFFFF, e);
        }
    }
    return 1;
}

/* Colours of a font, from its glyphs on the original page: texels are split
   at the median luminance; *bright = most frequent colour of the brighter
   half, *dark = most frequent of the darker half, *darkest = the darkest
   colour that covers at least 3% of the glyph texels. Light letters (hbut)
   use bright + dark as edge; ink on parchment (phil) uses darkest. */
FPG_FN void fpg_colours(const unsigned char* idx, const unsigned char* pal, const FpgCell* cells, int n, DWORD* bright, DWORD* dark, DWORD* darkest) {
    static unsigned cnt[256];
    int i, x, y, total = 0, half, acc, med = 0, bf = -1, be = -1, bd = -1;
    unsigned lum[256];
    for (i = 0; i < 256; i++) { cnt[i] = 0; lum[i] = pal[3 * i] * 30u + pal[3 * i + 1] * 59u + pal[3 * i + 2] * 11u; }
    for (i = 0; i < n; i++)
        for (y = 0; y < cells[i].h; y++)
            for (x = 0; x < cells[i].w; x++) {
                int t = idx[(cells[i].v + y) * FPG_SIDE + cells[i].u + x];
                if (t) { cnt[t]++; total++; }
            }
    half = total / 2; acc = 0;
    {
        unsigned L;
        for (L = 0; L <= 25500 && acc <= half; L += 50) {
            for (i = 1; i < 256; i++) if (lum[i] >= L && lum[i] < L + 50) acc += cnt[i];
            med = (int)L;
        }
    }
    for (i = 1; i < 256; i++) {
        if (!cnt[i]) continue;
        if ((int)lum[i] >= med) { if (bf < 0 || cnt[i] > cnt[bf]) bf = i; }
        else if (be < 0 || cnt[i] > cnt[be]) be = i;
        if (cnt[i] * 100u >= (unsigned)total * 3u && (bd < 0 || lum[i] < lum[bd])) bd = i;
    }
    if (bf < 0) bf = be;
    if (be < 0) be = bf;
    if (bd < 0) bd = be;
#define FPG_RGB(t) ((t) > 0 ? ((DWORD)pal[3 * (t)] << 16) | ((DWORD)pal[3 * (t) + 1] << 8) | pal[3 * (t) + 2] : 0)
    *bright = bf > 0 ? FPG_RGB(bf) : 0xFFFFFF;
    *dark = FPG_RGB(be);
    *darkest = FPG_RGB(bd);
#undef FPG_RGB
}

/* ---- cells from a .fnt file ----
 * Records: u16 count, then 32 bytes each: u16 code, u16 -, i32 material
 * (= renderer slot), i32 u, i32 v, i32 width (+0x10), i32 height (+0x14),
 * i32 -, u16 y offset (+0x1C, from the top of the line), u16 -.
 * The font's baseline is the bottom of 'H' (y offset + height); each
 * cell's baseline is that line position measured from its own top. */
typedef struct { unsigned short code; unsigned char slot; FpgCell c; int yoff; } FpgRec;

FPG_FN int fpg_read_fnt(const unsigned char* f, int len, FpgRec* out, int max) {
    int n, r, m = 0, B = -1;
    if (len < 2) return 0;
    n = f[0] | (f[1] << 8);
    if (2 + n * 32 > len) n = (len - 2) / 32;
    for (r = 0; r < n; r++) {
        const unsigned char* p = f + 2 + r * 32;
        if ((p[0] | (p[1] << 8)) == 'H') B = (short)(p[0x1C] | (p[0x1D] << 8)) + *(const int*)(p + 0x14);
    }
    for (r = 0; r < n && m < max; r++) {
        const unsigned char* p = f + 2 + r * 32;
        int mat = *(const int*)(p + 4), u = *(const int*)(p + 8), v = *(const int*)(p + 12);
        int w = *(const int*)(p + 0x10), h = *(const int*)(p + 0x14), yo = (short)(p[0x1C] | (p[0x1D] << 8));
        if (mat < 0x4E || mat > 0x51 || w <= 0 || h <= 0 || u < 0 || v < 0 || u + w > FPG_SIDE || v + h > FPG_SIDE) continue;
        out[m].code = (unsigned short)(p[0] | (p[1] << 8));
        out[m].slot = (unsigned char)mat;
        out[m].c.u = u; out[m].c.v = v; out[m].c.w = w; out[m].c.h = h;
        out[m].c.base = (B >= 0 ? B : yo + h) - yo;
        out[m].yoff = yo;
        m++;
    }
    return m;
}

/* ---- size of the TrueType face ----
 * The largest em size (pixels) at which the face's letters and digits
 * (J and Q excepted: their tails may be clipped) fit their rectangles with
 * at most 1 px overflow, and no larger than the size whose 'H' matches the
 * original 'H' height x k. Same rule as tools/font_preview.py.
 * metrics(ctx, em, code, &top, &height) gives a glyph's ink: rows above the
 * baseline and total rows; returns 0 if the face has no such glyph. */
typedef int (*FpgMetrics)(void* ctx, int em, int code, int* top, int* height);

FPG_FN int fpg_core(int c) {
    return (c >= 'A' && c <= 'Z' && c != 'J' && c != 'Q') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}
FPG_FN int fpg_overflow(const FpgRec* r, int n, int k, int em, FpgMetrics m, void* ctx) {
    int i, worst = 0;
    for (i = 0; i < n; i++) {
        int top, h, base, o;
        if (!fpg_core(r[i].code) || !m(ctx, em, r[i].code, &top, &h)) continue;
        base = r[i].c.base * k;
        o = top - base; if (o > worst) worst = o;
        o = (h - top) - (r[i].c.h * k - base); if (o > worst) worst = o;
    }
    return worst;
}
/* returns the em size, 0 if 'H' is missing; *capEm = the size matching the original 'H' */
FPG_FN int fpg_fit(const FpgRec* r, int n, int k, FpgMetrics m, void* ctx, int* capEm) {
    int i, target = 0, lo, hi, top, h;
    for (i = 0; i < n; i++) if (r[i].code == 'H') target = r[i].c.h * k;
    if (!target) return 0;
    lo = 4; hi = 400;                                   /* smallest em whose H is at least the target */
    while (lo < hi) { int mid = (lo + hi) / 2; if (m(ctx, mid, 'H', &top, &h) && h >= target) hi = mid; else lo = mid + 1; }
    if (capEm) *capEm = lo;
    if (fpg_overflow(r, n, k, lo, m, ctx) <= 1) return lo;
    hi = lo; lo = lo / 2;                               /* largest em in [lo, hi) that fits */
    while (hi - lo > 1) { int mid = (lo + hi) / 2; if (fpg_overflow(r, n, k, mid, m, ctx) <= 1) lo = mid; else hi = mid; }
    return lo;
}


/* ---- own metrics: new, taller cells for a redrawn font (HD fonts step 5) ----
 * The original rectangles are tight around the original letters, so a
 * TrueType face only fits them at ~80% (step 2). Here each glyph of the
 * font gets a new cell sized for the face: width = the face's advance
 * (the text hook sets the font's letter spacing to 0), height = its ink, placed in the
 * original line box (0 .. L, L = the font's line height, so line steps and
 * boxes keep their size) on a common baseline Bn, and packed into free
 * space on the font pages (pages 0x4E-0x51 are 47-91% empty). The text hook
 * then writes the new u, v, width, height, y offset and page into the
 * font's records, and zeroes its letter spacing and its kerning (made for
 * the original shapes).
 *
 * Size: the largest em at which the letters and digits fit the line box
 * (tallest ascender + deepest descender <= L), but whose 'H' is no taller
 * than the original 'H' (so lines do not grow much longer). */
typedef struct { int bbx, bby, ox, oy, adv; } FpgMet;      /* ink box, origin (ox right, oy up from the baseline), advance; pixels */
typedef int (*FpgGlyphMetrics)(void* ctx, int em, int code, FpgMet* m);
typedef struct {
    unsigned short code; unsigned char slot;    /* slot: page 0x4E-0x51 once packed, 0 before */
    int u, v, w, h, yoff;                       /* new cell, original texel units */
    int left;                                   /* ink starts this many pixels into the cell (page pixels) */
    int base;                                   /* baseline, units from the cell's top */
    int src;                                    /* index of the original record */
} FpgNewCell;

/* letters, digits and the usual tall/deep marks decide the line box */
static int fpg_boxchar(int c) { return fpg_core(c) || c == 'J' || c == 'Q' || c == '(' || c == ')' || c == ',' || c == ';' || c == '/'; }

/* em size and baseline (units from the line top) for one font; 0 if 'H' is missing */
FPG_FN int fpg_choose_em(const FpgRec* r, int n, int k, FpgGlyphMetrics m, void* ctx, int* baseOut, int* lineOut) {
    int i, L = 0, B = -1, capT = 0, lo, hi, em, asc = 0, desc = 0, Bn, a, d;
    FpgMet g;
    for (i = 0; i < n; i++) {
        if (r[i].yoff + r[i].c.h > L) L = r[i].yoff + r[i].c.h;
        if (r[i].code == 'H') { B = r[i].yoff + r[i].c.h; capT = r[i].c.h; }
    }
    if (B < 0 || L <= 0) return 0;
    lo = 4; hi = 400;
    while (hi - lo > 1) {                       /* largest em that fits the line box and the cap limit */
        int mid = (lo + hi) / 2, ok = 1;
        asc = desc = 0;
        for (i = 0; i < n && ok; i++) {
            if (!fpg_boxchar(r[i].code) || !m(ctx, mid, r[i].code, &g)) continue;
            if (g.oy > asc) asc = g.oy;
            if (g.bby - g.oy > desc) desc = g.bby - g.oy;
        }
        if (asc + desc > L * k) ok = 0;
        if (ok && (!m(ctx, mid, 'H', &g) || g.bby > capT * k)) ok = 0;   /* no metrics at this size counts as too big */
        if (ok) lo = mid; else hi = mid;
    }
    em = lo;
    asc = desc = 0;
    for (i = 0; i < n; i++) {
        if (!fpg_boxchar(r[i].code) || !m(ctx, em, r[i].code, &g)) continue;
        if (g.oy > asc) asc = g.oy;
        if (g.bby - g.oy > desc) desc = g.bby - g.oy;
    }
    a = (asc + k - 1) / k; d = (desc + k - 1) / k;   /* in units, rounded up */
    Bn = B;                                     /* keep the original baseline when the face fits around it */
    if (Bn < a) Bn = a;
    if (Bn > L - d) Bn = L - d;
    if (Bn < a) Bn = a;                         /* (cannot happen with asc + desc <= L*k, kept for safety) */
    if (baseOut) *baseOut = Bn;
    if (lineOut) *lineOut = L;
    return em;
}

/* new cells (unpacked) for the glyphs of one font; returns their number */
FPG_FN int fpg_new_cells(const FpgRec* r, int n, int k, int em, int Bn, int L, int spacing,
                         FpgGlyphMetrics m, void* ctx, FpgNewCell* out, int max) {
    int i, nc = 0, deepest = -1, deep = 0;
    for (i = 0; i < n && nc < max; i++) {
        FpgMet g; FpgNewCell* c; int top, bot, y0, y1, w;
        if (r[i].code < 33 || !m(ctx, em, r[i].code, &g) || g.bbx <= 0 || g.bby <= 0) continue;
        c = &out[nc];
        w = (g.adv + k / 2) / k - spacing;                 /* cell = advance - letter spacing (both added by the game) */
        if (w < 1) w = 1;
        if (w * k + k < g.bbx) w = (g.bbx + k - 1) / k;   /* ink up to one unit wider (f, j hooks) is condensed, more widens the cell */
        top = Bn * k - g.oy; bot = top + g.bby;            /* ink rows, pixels from the line top */
        y0 = top / k; if (y0 < 0) y0 = 0;
        y1 = (bot + k - 1) / k; if (y1 > L) y1 = L;
        if (y1 <= y0) y1 = y0 + 1;
        c->code = r[i].code; c->slot = 0; c->u = c->v = 0;
        c->w = w; c->h = y1 - y0; c->yoff = y0; c->base = Bn - y0; c->src = i;
        c->left = g.ox + (w * k - g.adv) / 2;              /* half of the rounding of the advance on each side */
        if (c->left < 0) c->left = 0;
        if (c->left + g.bbx > w * k) c->left = w * k - g.bbx;
        if (c->left < 0) c->left = 0;
        if (y1 > deep) { deep = y1; deepest = nc; }
        nc++;
    }
    /* keep the font's line height: the lowest cell reaches the line box's bottom */
    if (deepest >= 0 && deep < L) out[deepest].h = L - out[deepest].yoff;
    return nc;
}

/* occupancy of the four pages: 1 = taken (a glyph of another font, or the gutter of 1 texel around it) */
FPG_FN void fpg_occupy(unsigned char* occ /* 4 * 256 * 256 */, int slot, int u, int v, int w, int h) {
    int x, y;
    if (slot < 0x4E || slot > 0x51) return;
    for (y = v - 1; y <= v + h; y++)
        for (x = u - 1; x <= u + w; x++)
            if (x >= 0 && y >= 0 && x < FPG_SIDE && y < FPG_SIDE) occ[(slot - 0x4E) * FPG_SIDE * FPG_SIDE + y * FPG_SIDE + x] = 1;
}

/* packs the cells (tallest first) into free space; avoid(ctx, slot, u, v) may veto a top-left corner
   (e.g. one the text hook already knows for another glyph). Returns the number packed (all = success). */
typedef int (*FpgVeto)(void* ctx, int slot, int u, int v);
FPG_FN int fpg_pack(unsigned char* occ, FpgNewCell* cells, int nc, FpgVeto veto, void* vctx) {
    static int sat[(FPG_SIDE + 1) * (FPG_SIDE + 1)];
    static short order[1024];
    int i, j, placed = 0, page = -1;
    if (nc > 1024) return 0;
    for (i = 0; i < nc; i++) order[i] = (short)i;
    for (i = 1; i < nc; i++) {                  /* insertion sort: height, then width, descending */
        short t = order[i]; j = i - 1;
        while (j >= 0 && (cells[order[j]].h < cells[t].h || (cells[order[j]].h == cells[t].h && cells[order[j]].w < cells[t].w))) { order[j + 1] = order[j]; j--; }
        order[j + 1] = t;
    }
    for (i = 0; i < nc; i++) {
        FpgNewCell* c = &cells[order[i]];
        int p, done = 0;
        for (p = 0; p < 4 && !done; p++) {
            const unsigned char* o = occ + p * FPG_SIDE * FPG_SIDE;
            int x, y, S = FPG_SIDE + 1;
            if (page != p) {                    /* summed-area table of this page */
                for (y = 0; y <= FPG_SIDE; y++) sat[y * S] = 0;
                for (x = 0; x <= FPG_SIDE; x++) sat[x] = 0;
                for (y = 1; y <= FPG_SIDE; y++)
                    for (x = 1; x <= FPG_SIDE; x++)
                        sat[y * S + x] = o[(y - 1) * FPG_SIDE + x - 1] + sat[(y - 1) * S + x] + sat[y * S + x - 1] - sat[(y - 1) * S + x - 1];
                page = p;
            }
            for (y = 0; y + c->h <= FPG_SIDE && !done; y++)
                for (x = 0; x + c->w <= FPG_SIDE; x++) {
                    int sum = sat[(y + c->h) * S + x + c->w] - sat[y * S + x + c->w] - sat[(y + c->h) * S + x] + sat[y * S + x];
                    if (sum) continue;
                    if (veto && veto(vctx, 0x4E + p, x, y)) continue;
                    c->slot = (unsigned char)(0x4E + p); c->u = x; c->v = y;
                    fpg_occupy(occ, c->slot, x, y, c->w, c->h);
                    page = -1;                  /* the table is stale */
                    done = 1; placed++;
                    break;
                }
        }
        if (!done) return placed;
    }
    return placed;
}

/* ---- interface fonts redrawn in layers (16xY / 16xB / 16xX, [Fonts] Interface, 2026-11-26) ----
 * The interface fonts are small pixel letters in layers (session 2026-11-26, from the pages):
 * 16xY: yellow strokes in a red/orange glow, a magenta ring, a dark border; 16xB: light
 * purple strokes, dark purple ring and border; 16xX: the letter cut out (transparent) in a
 * dark grey ring. A TrueType letter is drawn the same way into the original rectangle:
 * outer ring (outerPx), line ring (linePx), the letter in the shade colour, and the face
 * colour over it lifted by shadePx (so a band of the shade shows along the bottom of every
 * stroke); for a cut-out font the letter itself is cleared. The colours come from the font's
 * own glyphs (fpg_ui_colours). */
typedef struct {
    DWORD face, shade, line, outer;   /* 0xRRGGBB */
    int cut;                          /* 1 = the letter is a hole (16xX) */
    int linePx, outerPx, shadePx;     /* in output pixels */
} FpgLayers;

/* rings of a glyph's original texels: r = 0 touches transparency (4-neighbours, outside the
   rectangle counts as transparent), 1 touches ring 0, 2 = the rest */
FPG_FN int fpg_ring_of(const unsigned char* idx, int u, int v, int w, int h, int x, int y) {
    int r, dx, dy;
    for (r = 0; r < 2; r++)                   /* ring r: a transparent texel within r + 1 steps (Manhattan) */
        for (dy = -(r + 1); dy <= r + 1; dy++)
            for (dx = -(r + 1); dx <= r + 1; dx++) {
                int ax = x + dx, ay = y + dy, m = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                if (m == 0 || m > r + 1) continue;
                if (ax < 0 || ay < 0 || ax >= w || ay >= h || !idx[(v + ay) * FPG_SIDE + u + ax]) return r;
            }
    return 2;
}

/* layer colours of an interface font from its glyphs: cells[i] on page idx[cells[i].slot]
   (slots 0x4E-0x51 -> idx[0..3]); only letters A-Z are counted */
FPG_FN void fpg_ui_colours(const unsigned char* const* idx, const unsigned char* pal, const FpgRec* r, int n, FpgLayers* L) {
    static unsigned cnt[3][256];
    unsigned holes = 0, core = 0, lum[256];
    int i, j, x, y, best[3][4];
    for (i = 0; i < 256; i++) { cnt[0][i] = cnt[1][i] = cnt[2][i] = 0; lum[i] = pal[3 * i] * 30u + pal[3 * i + 1] * 59u + pal[3 * i + 2] * 11u; }
    for (i = 0; i < n; i++) {
        const unsigned char* p;
        if (r[i].code < 'A' || r[i].code > 'Z' || r[i].slot < 0x4E || r[i].slot > 0x51) continue;
        p = idx[r[i].slot - 0x4E];
        for (y = 0; y < r[i].c.h; y++)
            for (x = 0; x < r[i].c.w; x++) {
                int t = p[(r[i].c.v + y) * FPG_SIDE + r[i].c.u + x];
                if (!t) {   /* a transparent texel inside the border: part of a cut-out letter */
                    if (x > 0 && y > 0 && x < r[i].c.w - 1 && y < r[i].c.h - 1) holes++;
                    continue;
                }
                {
                    int rg = fpg_ring_of(p, r[i].c.u, r[i].c.v, r[i].c.w, r[i].c.h, x, y);
                    cnt[rg][t]++;
                    if (rg == 2) core++;
                }
            }
    }
    /* the four most frequent colours of each ring */
    for (j = 0; j < 3; j++) {
        int a;
        for (a = 0; a < 4; a++) {
            int b = -1;
            for (i = 1; i < 256; i++) {
                int used = 0, q;
                for (q = 0; q < a; q++) if (best[j][q] == i) used = 1;
                if (!used && cnt[j][i] && (b < 0 || cnt[j][i] > cnt[j][b])) b = i;
            }
            best[j][a] = b;
        }
    }
#define FPG_C(t) ((t) > 0 ? ((DWORD)pal[3 * (t)] << 16) | ((DWORD)pal[3 * (t) + 1] << 8) | pal[3 * (t) + 2] : 0)
    {   /* outer: the darkest of ring 0's three most frequent */
        int o = best[0][0], a;
        for (a = 1; a < 3; a++) if (best[0][a] > 0 && lum[best[0][a]] < lum[o]) o = best[0][a];
        L->outer = FPG_C(o);
        /* line: the brighter of ring 1's two most frequent colours that are not the core's top colour */
        {
            int c0 = best[2][0], l = -1;
            for (a = 0; a < 4; a++) {
                int t = best[1][a];
                if (t <= 0 || t == c0 || t == o) continue;
                if (l < 0) l = t; else { if (lum[t] > lum[l]) l = t; break; }
            }
            L->line = FPG_C(l > 0 ? l : best[1][0]);
        }
    }
    L->cut = holes > core;               /* more holes than core texels: the letter is cut out */
    L->face = FPG_C(best[2][0] > 0 ? best[2][0] : best[1][0]);
    L->shade = FPG_C(best[2][1] > 0 ? best[2][1] : best[2][0] > 0 ? best[2][0] : best[1][0]);
    /* a face darker than its shade (purple 16xB: both close) - keep the brighter on top */
    if (best[2][0] > 0 && best[2][1] > 0 && lum[best[2][1]] > lum[best[2][0]]) { DWORD t = L->face; L->face = L->shade; L->shade = t; }
#undef FPG_C
}

/* em size for the interface letters: 'H' as tall as the original 'H' rectangle minus the rings
   at both ends (inset px each), at most the em at which 'H' plus rings is as wide as its rectangle
   (a hair narrower is fine: fpg_glyph_layers condenses). Returns 0 if 'H' is missing. */
FPG_FN int fpg_ui_em(const FpgRec* r, int n, int k, FpgMetrics met, void* ctx, int inset) {
    int i, Hh = 0, em, top, height;
    for (i = 0; i < n; i++) if (r[i].code == 'H') { Hh = r[i].c.h; break; }
    if (!Hh) return 0;
    {
        int cap = Hh * k - 2 * inset;
        if (cap < 4) cap = 4;
        if (!met(ctx, 100, 'H', &top, &height) || top <= 0) return 0;
        em = (cap * 100 + top / 2) / top;
        for (i = 0; i < 6 && em > 4; i++) {      /* rounding: settle on the em whose 'H' is cap high */
            if (!met(ctx, em, 'H', &top, &height)) return 0;
            if (top > cap) em--; else if (top < cap - 1) em++; else break;
        }
    }
    return em;
}

/* the letter to draw for a code of an interface font: the pixel fonts have only capitals
   (lowercase codes hold the same capitals), so a-z and Latin-1 lowercase map to upper case */
FPG_FN int fpg_ui_code(int code) {
    if (code >= 'a' && code <= 'z') return code - 32;
    if (code >= 0xE0 && code <= 0xFE && code != 0xF7) return code - 32;
    return code;
}

/* ---- combat numbers (numr red damage, numg green misses; 13 glyphs each, one design) ----
 * Redrawn from a bold face into their original rectangles (spacing unchanged). The game's
 * digits are 8 texels tall including a one-texel outline: the face's '8' is drawn (h - 2)
 * texels tall, its outline one texel (k px) all round, its baseline on the digits' last
 * fill row. fpg_num_em: the em for that; fpg_num_base: that baseline as a line row (cell
 * base = it - the record's y offset). Both 0 / -1 if the font has no '8'. */
FPG_FN int fpg_num_em(const FpgRec* r, int n, int k, FpgMetrics met, void* ctx) {
    int i, h = 0, em, top, height, cap;
    for (i = 0; i < n; i++) if (r[i].code == '8') { h = r[i].c.h; break; }
    if (h < 3) return 0;
    cap = (h - 2) * k;
    if (!met(ctx, 100, '8', &top, &height) || top <= 0) return 0;
    em = (cap * 100 + top / 2) / top;
    for (i = 0; i < 6 && em > 4; i++) {
        if (!met(ctx, em, '8', &top, &height)) return 0;
        if (top > cap) em--; else if (top < cap - 1) em++; else break;
    }
    return em;
}
/* the two colours of the digits (fill and outline): the two most frequent colours of their
   texels, the brighter one is the fill. 0 if fewer than two colours. */
FPG_FN int fpg_num_colours(const unsigned char* idx, const unsigned char* pal, const FpgCell* cells, int n, DWORD* fill, DWORD* edge) {
    static unsigned cnt[256];
    int i, x, y, a = -1, b = -1, la, lb;
    for (i = 0; i < 256; i++) cnt[i] = 0;
    for (i = 0; i < n; i++)
        for (y = cells[i].v; y < cells[i].v + cells[i].h; y++)
            for (x = cells[i].u; x < cells[i].u + cells[i].w; x++) if (idx[y * FPG_SIDE + x]) cnt[idx[y * FPG_SIDE + x]]++;
    for (i = 1; i < 256; i++) if (cnt[i] && (a < 0 || cnt[i] > cnt[a])) a = i;
    for (i = 1; i < 256; i++) if (cnt[i] && i != a && (b < 0 || cnt[i] > cnt[b])) b = i;
    if (a < 0 || b < 0) return 0;
    la = pal[3 * a] * 30 + pal[3 * a + 1] * 59 + pal[3 * a + 2] * 11;
    lb = pal[3 * b] * 30 + pal[3 * b + 1] * 59 + pal[3 * b + 2] * 11;
    if (lb > la) { i = a; a = b; b = i; }
    *fill = 0xFF000000u | ((DWORD)pal[3 * a] << 16) | ((DWORD)pal[3 * a + 1] << 8) | pal[3 * a + 2];
    *edge = 0xFF000000u | ((DWORD)pal[3 * b] << 16) | ((DWORD)pal[3 * b + 1] << 8) | pal[3 * b + 2];
    return 1;
}
/* the character drawn for a number glyph: the game's minus fills its 7-texel cell, a hyphen
   would leave a gap before the digits, so '-' is drawn as the en dash (0x96 in Windows-1252) */
FPG_FN int fpg_num_code(int code) { return code == '-' ? 0x96 : code; }
FPG_FN int fpg_num_base(const FpgRec* r, int n) {
    int i;
    for (i = 0; i < n; i++) if (r[i].code == '8') return r[i].yoff + r[i].c.h - 1;
    return -1;
}

/* Draws one interface glyph in layers into its rectangle. Returns 1 drawn, 2 left untouched
   (no ink would land inside), 0 refused. g->top = bitmap row on the letter's baseline; the
   letter's baseline is inset px above the cell's (the rings below it). */
FPG_FN int fpg_glyph_layers(DWORD* out, int k, const FpgCell* c, const FpgGlyph* g, const FpgLayers* L) {
    static unsigned char fit[FPG_MAXW * FPG_MAXW], ringL[FPG_MAXW * FPG_MAXW], ringO[FPG_MAXW * FPG_MAXW];
    int side = FPG_SIDE * k, W, H, x0, y0, rw, ox, oy, x, y, R1, R2, gw, gh, inset;
    if (k < 1 || k > FPG_MAXK || !c || !g || !L) return 0;
    if (c->u < 0 || c->v < 0 || c->w <= 0 || c->h <= 0 || c->u + c->w > FPG_SIDE || c->v + c->h > FPG_SIDE) return 0;
    if (!g->cov || g->cw <= 0 || g->ch <= 0 || g->cw > FPG_MAXW || g->ch > FPG_MAXW) return 0;
    W = c->w * k; H = c->h * k; x0 = c->u * k; y0 = c->v * k;
    R1 = L->linePx; R2 = L->linePx + L->outerPx; inset = R2;
    {   /* would the letter land inside at all? */
        int top = c->base * k - inset - g->top, any = 0;
        for (y = 0; y < g->ch && !any; y++) {
            if (top + y < 0 || top + y >= H) continue;
            for (x = 0; x < g->cw; x++) if (g->cov[y * g->cw + x]) { any = 1; break; }
        }
        if (!any) return 2;
    }
    /* condense if letter plus rings is wider than the rectangle */
    rw = g->cw;
    if (rw + 2 * R2 > W) rw = W - 2 * R2;
    if (rw < 1) rw = 1;
    for (y = 0; y < g->ch; y++)
        for (x = 0; x < rw; x++) {
            int a = x * g->cw, b = (x + 1) * g->cw, sum = 0, p;
            for (p = a / rw; p * rw < b && p < g->cw; p++) {
                int lo = p * rw > a ? p * rw : a, hi = (p + 1) * rw < b ? (p + 1) * rw : b;
                sum += g->cov[y * g->cw + p] * (hi - lo);
            }
            fit[y * rw + x] = (unsigned char)((sum + g->cw / 2) / g->cw);
        }
    gw = rw + 2 * R2; gh = g->ch + 2 * R2;
    if (gw > FPG_MAXW || gh > FPG_MAXW) return 0;
    /* rings: max coverage within R1 (line) and R2 (outer) */
    for (y = 0; y < gh; y++)
        for (x = 0; x < gw; x++) {
            int m1 = 0, m2 = 0, dx, dy;
            for (dy = -R2; dy <= R2; dy++)
                for (dx = -R2; dx <= R2; dx++) {
                    int sx = x - R2 + dx, sy = y - R2 + dy, v, d2 = dx * dx + dy * dy;
                    if (sx < 0 || sy < 0 || sx >= rw || sy >= g->ch) continue;
                    v = fit[sy * rw + sx];
                    if (d2 <= R2 * R2 + R2 && v > m2) m2 = v;
                    if (d2 <= R1 * R1 + R1 && v > m1) m1 = v;
                }
            ringO[y * gw + x] = (unsigned char)m2;
            ringL[y * gw + x] = (unsigned char)(R1 > 0 ? m1 : 0);
        }
    for (y = 0; y < H; y++) for (x = 0; x < W; x++) out[(y0 + y) * side + x0 + x] = 0;
    ox = x0 + (W - rw) / 2;
    oy = y0 + c->base * k - inset - g->top;
    for (y = -R2; y < g->ch + R2; y++) {
        int py = oy + y;
        if (py < y0 || py >= y0 + H) continue;
        for (x = -R2; x < rw + R2; x++) {
            int px = ox + x, a = 0, f = 0, e1, e2, al;
            DWORD col;
            if (px < x0 || px >= x0 + W) continue;
            if (x >= 0 && x < rw && y >= 0 && y < g->ch) {
                a = fit[y * rw + x];
                /* face: where the letter also covers the texel shadePx below (the bottom band stays shade) */
                f = a;
                if (L->shadePx > 0) { int yb = y + L->shadePx; int b = yb < g->ch ? fit[yb * rw + x] : 0; if (b < f) f = b; }
            }
            e2 = ringO[(y + R2) * gw + x + R2]; e1 = ringL[(y + R2) * gw + x + R2];
            if (!e2 && !a) continue;
            /* compose bottom-up: outer, line, shade (letter), face */
            col = L->outer; al = e2;
            if (e1) { col = fpg_over(L->line, e1, col, al) & 0xFFFFFF; al = e1 + al * (255 - e1) / 255; }
            if (L->cut) {                         /* the letter is a hole: remove it from the rings */
                al = al * (255 - a) / 255;
            } else {
                if (a) { col = fpg_over(L->shade, a, col, al) & 0xFFFFFF; al = a + al * (255 - a) / 255; }
                if (f) { col = fpg_over(L->face, f, col, al) & 0xFFFFFF; al = f + al * (255 - f) / 255; }
            }
            if (al > 255) al = 255;
            out[py * side + px] = al ? ((DWORD)al << 24) | col : 0;
        }
    }
    return 1;
}
