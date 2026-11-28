/* Native test harness for src/ui_layout.inc (gcc/clang on Linux, macOS or
 * Windows). Reads a frame description on stdin, runs the real layout code
 * and prints the vertices afterwards. Driven by tests/replay/replay.py.
 *
 * Input:   S <width> <height>          screen size
 *          H <layout>                  HudLayout: 0 stretched, 1 split, 2 right, 3 center
 *          A <aspect>                  MaxAspect (0 = no cap)
 *          L <0|1>                     Layout
 *          T <percent>                 TextScale (default 100 here)
 *          G <0|1>                     also run the debug-log code (hover events etc.), output on stderr
 *          F                           frame start (StartScene)
 *          B <caller> <material> <n>   followed by n lines "x y z rhw"
 *          R                           renderer flush (RenderAllBuffers/EndFrame)
 *          P <x> <y>                   HUD click mapping, forward: where the layout draws a HUD
 *                                      point the game has at (x,y); prints "P <x'>" at once
 *          U <x> <y> <col> <bar>       HUD click mapping, inverse (ui_hud_aim): the HUD part drawn
 *                                      at screen (x,y) and the game's x; prints "U <part> <gx>" at once
 *          Q <mode> <col> <bar>        HUD mouse: set the mode (0 game, 1 menu) and which HUD parts are shown
 *          C <x> <y>                   HUD mouse: put the game cursor at (x,y) (as the game would)
 *          V <0|1>                     tooltips anchor to the game cursor (C) like the DLL, not the sprite
 *          K <dir>                     text decoding: load the fonts from <dir> (the game's data folder)
 *          X <n> <mat>                 text decoding: a glyph batch of n glyphs, followed by n lines
 *                                      "u0 v0 u1 v1 u2 v2" (texel corners as the game stores them);
 *                                      prints "X <code> <font> ..." at once
 *          D <dx> <dy> <btn> <drag>    HUD mouse: one DirectInput read of movement dx,dy (btn bit 1 = right
 *                                      button, drag = the game's right-drag flag), applied the way the game
 *                                      does; prints "D <game x> <game y> <real x> <real y> <draw dx>" at once
 *          Y <percent>                 the game draws text at this scale itself (TextHook): tooltip rules follow
 *          Z <dir> <font> <pct> <n>    text hook: load a font as the game does, scale it, draw n glyphs
 *                                      ("code x y clipL clipT clipR clipB" lines) as 0x433714 does and
 *                                      rewrite their texels; prints ZF / ZG lines (see below in main)
 *          N <0|1>                     text hook: skip the texel rewrite (mutation check)
 * Output:  B <n> then n lines "x y", one block per B in input order
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int DWORD;
static int SW = 3840, SH = 2160;
#define EXE_SCREEN_W (&SW)
#define EXE_SCREEN_H (&SH)
#define TEXT_CALLER 0x0042B0BC

/* stand-ins for the DLL's logging and settings */
/* the game's cursor block and right-drag flag (in game memory in the DLL) */
static int g_gameCur[4]; static unsigned char g_rdrag;
#define EXE_CURSOR (g_gameCur)
#define EXE_RDRAG  (&g_rdrag)
static int g_cfgHudMouse = 1, g_cfgText = 100, g_cfgPortraitHold = 1;
static int g_harnessLive = 0;    /* "V 1": tooltips take the game cursor (C) as anchor, as the DLL does */
#define UI_LIVE_CURSOR g_harnessLive
static int g_cfgLog = 0, g_cfgAuto = 0, g_cfgCursor = 200, g_uiOn = 1, g_cfgHud = 1, g_cfgLayout = 1;
static float g_cfgMaxAspect = 0;
/* bars drawn by the layout code are printed as "BARS x0 x1 x2 x3 h" lines (collected, printed at the end) */
static char g_barsOut[1 << 20]; static int g_barsLen;
static void ui_draw_bars(float x0, float x1, float x2, float x3, float h, DWORD bucket, DWORD mat, DWORD a3, float u, float v) {
    (void)bucket; (void)mat; (void)a3; (void)u; (void)v;
    if (g_barsLen < (int)sizeof(g_barsOut) - 100) g_barsLen += sprintf(g_barsOut + g_barsLen, "BARS %.2f %.2f %.2f %.2f %.2f\n", x0, x1, x2, x3, h);
}
static void ui_draw_quad(DWORD bucket, DWORD mat, DWORD a3, const float* v6) {
    float x0 = 1e30f, x1 = -1e30f; int k;
    (void)bucket; (void)a3;
    for (k = 0; k < 6; k++) { if (v6[k*8] < x0) x0 = v6[k*8]; if (v6[k*8] > x1) x1 = v6[k*8]; }
    if (g_barsLen < (int)sizeof(g_barsOut) - 100) g_barsLen += sprintf(g_barsOut + g_barsLen, "HQ %X %.2f %.2f\n", (unsigned)mat, x0, x1);
}
static int g_capturing = 0, g_capReq = 0, g_marker = 0, g_ncap = 0;
typedef struct { const float* v; int n; void* s; DWORD caller; int idx; DWORD a0, a1, a2, a3; int td; } Pending;
typedef struct { const float* vp; float ax0, ax1, ay0, ay1; char how, kind; } Cap;
static Cap g_cap[1];
typedef struct { int x, y; } POINT_;
static short GetAsyncKeyState(int k) { (void)k; return 0; }
static int GetCursorPos(POINT_* p) { p->x = p->y = 0; return 1; }
static void flush(void) {}
static char* ftoa(float f, char* o) { sprintf(o, "%.3f", f); return o; }
static int g_harnessLog = 0;     /* "G 1": run the debug-log code too, printing to stderr */
#define LOG(...) do { if (g_harnessLog) fprintf(stderr, __VA_ARGS__); } while (0)

/* text decoding (text_decode.inc) with stand-ins for Windows file reading */
typedef void* HANDLE;
static int readable(const void* p, DWORD n) { (void)p; (void)n; return 1; }
static int g_lastX = -1;      /* decoded batch handed to the next B draw (as hook_pre/hook_post do) */
static HANDLE CreateFileA(const char* p, DWORD a, DWORD b, void* c, DWORD d, DWORD e, void* f) {
    FILE* fp = fopen(p, "rb"); (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return fp ? (HANDLE)fp : (HANDLE)-1; }
static int ReadFile(HANDLE h, void* buf, DWORD n, DWORD* got, void* o) { (void)o; *got = (DWORD)fread(buf, 1, n, (FILE*)h); return 1; }
static int CloseHandle(HANDLE h) { fclose((FILE*)h); return 1; }
#define wsprintfA sprintf
#define TD_PATHFMT "%s/%s.fnt"
#include "../../src/text_decode.inc"

/* game-side text scaling (text_scale.inc): game memory is an arena with 32-bit offsets as addresses */
static unsigned char g_arena[1 << 23]; static DWORD g_arenaTop = 16;
#define TX_PTR(a) ((a) ? g_arena + (a) : (unsigned char*)0)
static DWORD arena_alloc(DWORD n) { DWORD a = (g_arenaTop + 15) & ~15u; g_arenaTop = a + n; if (g_arenaTop > sizeof g_arena) exit(3); memset(g_arena + a, 0, n); return a; }
static int g_noFix = 0;          /* "N 1": skip the texel rewrite (mutation check) */
static float g_gts = 1.0f;       /* "Y <pct>": the game draws text at pct itself (TextHook) */
#include "../../src/text_scale.inc"

/* build a font object the way the game's loader (0x628394) does, from a .fnt file */
/* f = 0: a new object (constructor 0x4D9160); else reload into f: records, direct table and
   kerning are fresh, line height stays a running max, line gap and spacing stay as they were */
static unsigned char* fake_font(const char* dir, const char* name, unsigned char* f) {
    static unsigned char buf[1 << 18]; char path[700]; FILE* fp; size_t got; int n, i, mb;
    DWORD nm = arena_alloc(64), recs, direct;
    sprintf(path, "%s/%s.fnt", dir, name);
    if (!(fp = fopen(path, "rb"))) return 0;
    got = fread(buf, 1, sizeof buf, fp); fclose(fp);
    if (!f) { f = g_arena + arena_alloc(0x30); *(int*)(f + 0x20) = 1; }
    mb = *(int*)(f + 0x18);
    n = buf[0] | (buf[1] << 8);
    sprintf((char*)g_arena + nm, "%s.fnt", name);
    recs = arena_alloc((DWORD)n * 32); memcpy(g_arena + recs, buf + 2, (size_t)n * 32);
    direct = arena_alloc(256 * 32);
    for (i = 0; i < 256; i++) *(int*)(g_arena + direct + i * 32 + 4) = -1;
    for (i = 0; i < n; i++) {
        unsigned char* r = g_arena + recs + i * 32; int code = r[0] | (r[1] << 8);
        int b = *(short*)(r + 0x1C) + *(int*)(r + 0x14); if (b > mb) mb = b;
        if (code < 256) memcpy(g_arena + direct + code * 32, r, 32);
    }
    *(DWORD*)(f + 0) = nm; *(DWORD*)(f + 4) = recs; *(int*)(f + 8) = 1; *(DWORD*)(f + 0xC) = direct;
    *(int*)(f + 0x14) = n; *(int*)(f + 0x18) = mb; *(int*)(f + 0x1C) = 1;
    if ((size_t)(2 + n * 32 + 2) <= got) {              /* kerning: u16 entries, u16 count each, then 8-byte pairs {first, second, adjust} */
        const unsigned char* q = buf + 2 + n * 32; int ne = q[0] | (q[1] << 8), e, j; DWORD tab = arena_alloc((DWORD)ne * 12);
        const unsigned char* pr = q + 2 + ne * 2;
        for (e = 0; e < ne; e++) {
            int np = q[2 + e * 2] | (q[3 + e * 2] << 8); DWORD pp = arena_alloc((DWORD)np * 8);
            *(int*)(g_arena + tab + e * 12 + 8) = np; *(DWORD*)(g_arena + tab + e * 12 + 4) = pp;
            for (j = 0; j < np; j++, pr += 8) {
                *(unsigned short*)(g_arena + tab + e * 12) = (unsigned short)(pr[0] | (pr[1] << 8));
                *(unsigned short*)(g_arena + pp + j * 8) = (unsigned short)(pr[2] | (pr[3] << 8));
                *(int*)(g_arena + pp + j * 8 + 4) = *(const int*)(pr + 4);
            }
        }
        *(DWORD*)(f + 0x24) = tab; *(int*)(f + 0x28) = ne;
    }
    return f;
}

/* stand-in for the game's layout 0x413A64 (auto-fit fix test): ctx = {space, n, w1..wn, ..., [65] = step} */
static int harness_lay(void* ctx, int width, int* outW) {
    const int* c = (const int*)ctx; int i, lines = 1, cur = 0, mx = 0;
    for (i = 0; i < c[1]; i++) {
        int need = cur ? cur + c[0] + c[2 + i] : c[2 + i];
        if (cur && need > width) { if (cur > mx) mx = cur; lines++; cur = c[2 + i]; }
        else cur = need;
    }
    if (cur > mx) mx = cur;
    *outW = mx;
    return lines * c[65];
}
/* per-place fonts (font_places.inc): copies come from the arena; the 16xy global is a stand-in */
#define FP_ALLOC(n) arena_alloc(n)
static DWORD g_fpG16;
static DWORD fp_gv_h(DWORD global) { return global == 0x35565DC ? g_fpG16 : 0; }
#include "../../src/font_places.inc"
#include "../../src/ttf_name.inc"
/* first kerning adjust of a font object (entry 0, pair 0), 0 if none */
static int fp_k0(const unsigned char* f) {
    const unsigned char* k = *(const DWORD*)(f + 0x24) ? TX_PTR(*(const DWORD*)(f + 0x24)) : 0;
    if (!k || *(const int*)(f + 0x28) <= 0 || *(const int*)(k + 8) <= 0) return 0;
    return *(const int*)(TX_PTR(*(const DWORD*)(k + 4)) + 4);
}
/* deep equality of two font objects' records, direct table and kerning, with no memory shared */
static int fp_same(const unsigned char* a, const unsigned char* b) {
    int n = *(const int*)(a + 0x14), nk = *(const int*)(a + 0x28), e;
    const unsigned char *ka, *kb;
    if (n != *(const int*)(b + 0x14) || nk != *(const int*)(b + 0x28)) return 0;
    if (*(const DWORD*)(a + 4) == *(const DWORD*)(b + 4) || memcmp(TX_PTR(*(const DWORD*)(a + 4)), TX_PTR(*(const DWORD*)(b + 4)), (size_t)n * 32)) return 0;
    if (*(const int*)(a + 8) && (*(const DWORD*)(a + 0xC) == *(const DWORD*)(b + 0xC) || memcmp(TX_PTR(*(const DWORD*)(a + 0xC)), TX_PTR(*(const DWORD*)(b + 0xC)), 256 * 32))) return 0;
    if (!nk) return 1;
    ka = TX_PTR(*(const DWORD*)(a + 0x24)); kb = TX_PTR(*(const DWORD*)(b + 0x24));
    if (ka == kb) return 0;
    for (e = 0; e < nk; e++) {
        int np = *(const int*)(ka + e * 12 + 8);
        if (*(const unsigned short*)(ka + e * 12) != *(const unsigned short*)(kb + e * 12) || np != *(const int*)(kb + e * 12 + 8)) return 0;
        if (np && (*(const DWORD*)(ka + e * 12 + 4) == *(const DWORD*)(kb + e * 12 + 4)
                   || memcmp(TX_PTR(*(const DWORD*)(ka + e * 12 + 4)), TX_PTR(*(const DWORD*)(kb + e * 12 + 4)), (size_t)np * 8))) return 0;
    }
    return 1;
}

/* the DirectInput mouse hook lives in the DLL; nothing is pressed offline */
static DWORD g_diDown, g_diPress; static void* g_diDev; static void di_try_hook(void) {}
#include "../../src/ui_layout.inc"

#define MAXV 2000000
#define MAXB 200000
static float vb[MAXV][8];
static float* bufs[MAXB]; static int ns[MAXB];

int main(void) {
    char t[8]; int nv = 0, nb = 0;
    while (scanf("%7s", t) == 1) {
        if (t[0] == 'S') { if (scanf("%d %d", &SW, &SH) != 2) return 1; }
        else if (t[0] == 'H') { if (scanf("%d", &g_cfgHud) != 1) return 1; }
        else if (t[0] == 'A') { if (scanf("%f", &g_cfgMaxAspect) != 1) return 1; }
        else if (t[0] == 'G') { if (scanf("%d", &g_harnessLog) != 1) return 1; g_cfgLog = g_harnessLog; }
        else if (t[0] == 'T') { if (scanf("%d", &g_cfgText) != 1) return 1; }
        else if (t[0] == 'L') { if (scanf("%d", &g_cfgLayout) != 1) return 1; }
        else if (t[0] == 'F') ui_frame_start();
        else if (t[0] == 'R') ui_flush();
        else if (t[0] == 'e') { ui_flush(); ui_conv_end(); }   /* EndFrame */
        else if (t[0] == 'P') {
            float x, y; if (scanf("%f %f", &x, &y) != 2) return 1;
            printf("P %.4f\n", ui_hud_place(x, y, 1, 1));
        }
        else if (t[0] == 'K') {
            char dir[600]; if (scanf("%599s", dir) != 1) return 1;
            td_load(dir);
        }
        else if (t[0] == 'X') {
            static unsigned char batch[0x514]; int n, mat, k, r;
            if (scanf("%d %d", &n, &mat) != 2) return 1;
            memset(batch, 0, sizeof batch);
            *(int*)(batch + 4) = n; *(int*)(batch + 8) = mat;
            for (k = 0; k < n; k++) {
                int* g = (int*)(batch + k * 0x80);
                if (scanf("%d %d %d %d %d %d", &g[0x20/4], &g[0x24/4], &g[0x38/4], &g[0x3C/4], &g[0x50/4], &g[0x54/4]) != 6) return 1;
            }
            r = td_decode(batch); g_lastX = r;
            printf("X");
            if (r >= 0) for (k = 0; k < g_tdRing[r].n; k++) printf(" %d %d", g_tdRing[r].ch[k], g_tdRing[r].font[k]);
            printf("\n");
        }
        else if (t[0] == 'Q') {
            int m, c, b; if (scanf("%d %d %d", &m, &c, &b) != 3) return 1;
            g_mode = g_prevMode = m; g_pCol = c; g_pBar = b; g_fCol = g_fBar = 0;
        }
        else if (t[0] == 'N') { if (scanf("%d", &g_noFix) != 1) return 1; }
        else if (t[0] == 'Y') { int pc; if (scanf("%d", &pc) != 1) return 1; g_gts = pc / 100.0f; }
        else if (t[0] == 'W') { char dir[600]; if (scanf("%599s", dir) != 1) return 1; tx_load(dir); }
        else if (t[0] == 'Z') {
            /* Z <dir> <font> <pct> <n>, then n lines "code x y clipL clipT clipR clipB": load the font as
               the game does, scale it, then draw each glyph the way 0x433714 does (corners, clip, texels
               u = rec.u + (x - x0)) and run the texel rewrite. Prints
               "ZF <result> <line height> <gap> <spacing> <again> <spacing after setter(-1)>" and per glyph
               "ZG x0 y0 x1 y1 u0 v0 u1 v1 advance" (clipped quad and texels; "ZG -" if clipped away). */
            char dir[600], name[32]; int pct, n, k, r, r2, r3, lh, gap, sp, sp1; const char* why; unsigned char* f;
            if (scanf("%599s %31s %d %d", dir, name, &pct, &n) != 4) return 1;
            tx_load(dir);
            f = fake_font(dir, name, 0);
            if (!f) { printf("ZF -1 0 0 0 0 0 0 0 0 0\n"); continue; }
            r = tx_scale_font(f, pct, &why);                       /* the load hook */
            if (g_harnessLog) fprintf(stderr, "scale: %s\n", why);
            lh = *(int*)(f + 0x18); gap = *(int*)(f + 0x20); sp = *(int*)(f + 0x2C);
            r2 = tx_scale_font(f, pct, &why);                      /* again without a reload: must keep */
            *(int*)(f + 0x2C) = tx_spacing(f, -1); sp1 = *(int*)(f + 0x2C);   /* the setter, as 16xy gets it */
            fake_font(dir, name, f);                               /* the game reloads the font into the same object */
            r3 = tx_scale_font(f, pct, &why);
            printf("ZF %d %d %d %d %d %d %d %d %d %d\n", r, lh, gap, sp, r2, sp1, r3, *(int*)(f + 0x18), *(int*)(f + 0x20), *(int*)(f + 0x2C));
            for (k = 0; k < n; k++) {
                int code, x, y, cl, ct, cr, cb, i, nn; unsigned char* rec = 0; unsigned char blk[96];
                if (scanf("%d %d %d %d %d %d %d", &code, &x, &y, &cl, &ct, &cr, &cb) != 7) return 1;
                nn = f ? *(int*)(f + 0x14) : 0;
                for (i = 0; i < nn; i++) { unsigned char* q = TX_PTR(*(DWORD*)(f + 4)) + i * 32; if ((q[0] | (q[1] << 8)) == code) { rec = q; break; } }
                if (!rec) { printf("ZG ?\n"); continue; }
                {
                    int x0 = x, y0 = y + *(short*)(rec + 0x1C), x1 = x0 + *(int*)(rec + 0x10), y1 = y0 + *(int*)(rec + 0x14);
                    int a0 = x0 > cl ? x0 : cl, b0 = y0 > ct ? y0 : ct, a1 = x1 < cr ? x1 : cr, b1 = y1 < cb ? y1 : cb;
                    int ru = *(int*)(rec + 8), rv = *(int*)(rec + 0xC), mat = *(int*)(rec + 4), adv = *(int*)(rec + 0x10) + *(int*)(f + 0x2C) + 1, j;
                    int cx[4] = { a0, a1, a1, a0 }, cy[4] = { b0, b0, b1, b1 };
                    if (a1 <= a0 || b1 <= b0) { printf("ZG - %d\n", adv); continue; }
                    for (j = 0; j < 4; j++) {
                        *(float*)(blk + j * 24) = (float)cx[j]; *(float*)(blk + j * 24 + 4) = (float)cy[j]; *(float*)(blk + j * 24 + 8) = 0;
                        *(int*)(blk + j * 24 + 0xC) = ru + (cx[j] - x0); *(int*)(blk + j * 24 + 0x10) = rv + (cy[j] - y0);
                    }
                    if (!g_noFix) tx_fix_block(blk, mat, x0, y0, x1, y1);
                    printf("ZG %d %d %d %d %d %d %d %d %d\n", a0, b0, a1, b1, *(int*)(blk + 0xC), *(int*)(blk + 0x10), *(int*)(blk + 2 * 24 + 0xC), *(int*)(blk + 2 * 24 + 0x10), adv);
                }
            }
        }
        else if (t[0] == 'O') {
            /* O <dir> <font> <pctOrig> <pctA> <pctB> <mutation>: the game loads <font> into the 16xy
               object; every place gets a copy (fp_refresh); the original is scaled at pctOrig, copy 0 at
               pctA, copy 1 at pctB, the rest at pctOrig; the spacing setter (-1) runs on the original;
               then the game reloads the font into the same object and everything is done again.
               mutation 1: copy 0 shares the original's kerning (a shallow copy) - must be caught.
               Prints "OF made sameAll k0raw w0raw lh(o a b) w0(o a b) k0(o a b) sp(o a b)" after the
               first load, "OR made2 stable refreshes lh(o a b) w0(o a b) k0(o a b) sp(o a b)" after the
               reload, and "OP place(copy0) place(copy1) place(orig) nsite". */
            char dir[600], name[32]; int po, pa, pb, mut, made, same = 1, i, k, k0raw, w0raw; const char* why;
            unsigned char* f; DWORD keep[FP_NSITE];
            if (scanf("%599s %31s %d %d %d %d", dir, name, &po, &pa, &pb, &mut) != 6) return 1;
            tx_load(dir);
            f = fake_font(dir, name, 0);
            if (!f) { printf("OF -1\n"); continue; }
            g_fpG16 = (DWORD)(f - g_arena);
            k0raw = fp_k0(f); w0raw = *(int*)(TX_PTR(*(DWORD*)(f + 4)) + 0x10);
            for (k = 0; k < 2; k++) {
                if (k) { fake_font(dir, name, f); for (i = 0; i < FP_NSITE; i++) keep[i] = g_fpCopy[i].obj; }
                made = fp_refresh(g_fpG16, fp_gv_h);                  /* the load hook, before scaling */
                /* only the sites of the 16xy global get copies here (the others read other fonts) */
                if (!k) for (i = 0; i < FP_NSITE; i++) if (fp_site[i].global == 0x35565DC && (!g_fpCopy[i].obj || !fp_same(f, TX_PTR(g_fpCopy[i].obj)))) same = 0;
                if (mut) *(DWORD*)(TX_PTR(g_fpCopy[0].obj) + 0x24) = *(DWORD*)(f + 0x24);
                tx_scale_font(f, po, &why);
                for (i = 0; i < FP_NSITE; i++) if (g_fpCopy[i].obj) tx_scale_font(TX_PTR(g_fpCopy[i].obj), i == 0 ? pa : i == 1 ? pb : po, &why);
                if (!k) { *(int*)(f + 0x2C) = tx_spacing(f, -1); fp_spacing(g_fpG16, -1); }   /* setter after the first load only (as the game) */
                {
                    unsigned char* o[3] = { f, TX_PTR(g_fpCopy[0].obj), TX_PTR(g_fpCopy[1].obj) };
                    int stable = 1;
                    if (!k) printf("OF %d %d %d %d", made, same, k0raw, w0raw);
                    else { for (i = 0; i < FP_NSITE; i++) if (keep[i] != g_fpCopy[i].obj) stable = 0;
                           printf("OR %d %d %d", made, stable, g_fpCopy[0].refreshes); }
                    for (i = 0; i < 3; i++) printf(" %d", *(int*)(o[i] + 0x18));
                    for (i = 0; i < 3; i++) printf(" %d", *(int*)(TX_PTR(*(DWORD*)(o[i] + 4)) + 0x10));
                    for (i = 0; i < 3; i++) printf(" %d", fp_k0(o[i]));
                    for (i = 0; i < 3; i++) printf(" %d", *(int*)(o[i] + 0x2C));
                    printf("\n");
                }
            }
            printf("OP %d %d %d %d\n", fp_place(g_fpCopy[0].obj), fp_place(g_fpCopy[1].obj), fp_place(g_fpG16), FP_NSITE);
        }
        else if (t[0] == 'J') {
            /* J <dir> <font> <kindPct> <mutation> <n>, then n lines "site pct": per-place sizes
               (font_places.inc, phase 5b step 2). Sets each place's size with fp_set_pct (hex site
               address, as in [PlaceTextScale]), loads <font> into the 16xy object, and does what the
               DLL's load hook does: copies first (fp_refresh), the original at the kind's size, each
               copy at fp_pct(place, kind); then a reload into the same object, all again.
               mutation 1: every copy at the kind's size (the place size ignored) - must be caught.
               Prints "JS <index>" per size line, "JM <fp_max_pct> <fp_tip_pct(kind)>", then per load
               "JL <load> <orig lh> <orig w0>" and per 16xy place "JP <load> <site> <pct used> <lh> <w0> <k0> <sp>". */
            char dir[600], name[32]; int kind, mut, n, i, k; const char* why; unsigned char* f;
            if (scanf("%599s %31s %d %d %d", dir, name, &kind, &mut, &n) != 5) return 1;
            for (i = 0; i < FP_NSITE; i++) g_fpPct[i] = 0;
            for (i = 0; i < n; i++) {
                unsigned site; int pct;
                if (scanf("%x %d", &site, &pct) != 2) return 1;
                printf("JS %d\n", fp_set_pct((DWORD)site, pct));
            }
            printf("JM %d %d\n", fp_max_pct(), fp_tip_pct(kind));
            tx_load(dir);
            f = fake_font(dir, name, 0);
            if (!f) { printf("JL -1\n"); continue; }
            g_fpG16 = (DWORD)(f - g_arena);
            for (k = 0; k < 2; k++) {
                if (k) fake_font(dir, name, f);                            /* the game reloads into the same object */
                fp_refresh(g_fpG16, fp_gv_h);                              /* the load hook: copies before scaling */
                tx_scale_font(f, kind, &why);                              /* the original: its kind's size */
                for (i = 0; i < FP_NSITE; i++)
                    if (g_fpCopy[i].obj && g_fpCopy[i].src == g_fpG16) tx_scale_font(TX_PTR(g_fpCopy[i].obj), mut ? kind : fp_pct(i, kind), &why);
                if (!k) { *(int*)(f + 0x2C) = tx_spacing(f, -1); fp_spacing(g_fpG16, -1); }   /* the setter, after the first load */
                printf("JL %d %d %d\n", k, *(int*)(f + 0x18), *(int*)(TX_PTR(*(DWORD*)(f + 4)) + 0x10));
                for (i = 0; i < FP_NSITE; i++) {
                    unsigned char* o;
                    if (fp_site[i].global != 0x35565DC || !g_fpCopy[i].obj) continue;
                    o = TX_PTR(g_fpCopy[i].obj);
                    printf("JP %d %06X %d %d %d %d %d\n", k, (unsigned)fp_site[i].site, fp_pct(i, kind), *(int*)(o + 0x18),
                           *(int*)(TX_PTR(*(DWORD*)(o + 4)) + 0x10), fp_k0(o), *(int*)(o + 0x2C));
                }
            }
        }
        else if (t[0] == 'I') {
            /* I <n>, then n lines "<Section>/<Key> <pct>": text size settings (ts_key) as the ini gives
               them ([Text.Menus] MainMenu=150 -> "Text.Menus/MainMenu 150"; 0 = unset), applied the way
               fp_config does (ts_kinds, then ts_groups), then "IP <site> <pct>" lines (the
               [PlaceTextScale] override, applied after the items) until "IP 0 0". Prints
               "IG <section/key> <index or -1>" per input line, "IK <kind> <pct>" per font kind,
               "IT <section/key> <resolved pct> <places set>" per setting, then "IS <site> <size>"
               for every shared-font place (fp_pct(i, 0): 0 = the kind's) and "IL <site> <size>" for
               every load-site place, then "IM <fp_max_pct> <fp_tip_pct(tooltip kind)> <nsettings>". */
            int n, i, kp[TS_KINDS];
            if (scanf("%d", &n) != 1) return 1;
            for (i = 0; i < FP_NSITE; i++) g_fpPct[i] = 0;
            for (i = 0; i < FP_NLOAD; i++) g_fpLoadPct[i] = 0;
            for (i = 0; i < TS_N; i++) g_tsVal[i] = 0;
            for (i = 0; i < n; i++) {
                char key[64]; int pct, g;
                if (scanf("%63s %d", key, &pct) != 2) return 1;
                g = ts_find(key);
                if (g >= 0) g_tsVal[g] = pct;
                printf("IG %s %d\n", key, g);
            }
            ts_kinds(kp);
            ts_groups(kp);
            for (;;) {
                unsigned site; int pct;
                if (scanf(" IP %x %d", &site, &pct) != 2) return 1;
                if (!site) break;
                if (pct > 0) fp_set_pct((DWORD)site, pct);
            }
            for (i = 0; i < TS_KINDS; i++) printf("IK %d %d\n", i, kp[i]);
            for (i = 0; i < TS_N; i++) printf("IT %s/%s %d %d\n", ts_key[i].section, ts_key[i].key, ts_pct(i), g_tsSet[i]);
            for (i = 0; i < FP_NSITE; i++) printf("IS %06X %d\n", (unsigned)fp_site[i].site, fp_pct(i, 0));
            for (i = 0; i < FP_NLOAD; i++) printf("IL %06X %d\n", (unsigned)fp_load[i], fp_load_pct(fp_load[i], 0));
            printf("IM %d %d %d\n", fp_max_pct(), fp_tip_pct(kp[1]), TS_N);
        }
        else if (t[0] == 'f') {
            /* f <path>: the face name and weight of a font file as the DLL reads them for
               [Fonts] Menu / Names = <file> (ttf_name.inc). Prints "FN <weight> <face>" or "FN 0 -". */
            char path[512]; FILE* f; static unsigned char buf[4 << 20]; size_t len; unsigned off, tl; char face[64]; int w = 0;
            if (scanf("%511s", path) != 1) return 1;
            f = fopen(path, "rb"); len = f ? fread(buf, 1, sizeof buf, f) : 0; if (f) fclose(f);
            face[0] = 0;
            if (ttf_table(buf, (unsigned)len, "name", &off, &tl) && off + tl <= len && ttf_family(buf + off, tl, face, 64)) {
                w = 400;
                if (ttf_table(buf, (unsigned)len, "OS/2", &off, &tl) && off + tl <= len) w = ttf_weight(buf + off, tl);
            }
            printf("FN %d %s\n", w, face[0] ? face : "-");
        }
        else if (t[0] == 'M') {
            /* M <W> <H> <w> <h> <maxPct>: copyright position and save-list date limit (text_scale.inc).
               Prints "CP <x> <y> <limit>". */
            int W, H, w, h, mp, x, y;
            if (scanf("%d %d %d %d %d", &W, &H, &w, &h, &mp) != 5) return 1;
            tx_copyright_xy(W, H, w, h, &x, &y);
            printf("CP %d %d %d\n", x, y, tx_date_limit(mp));
        }
        else if (t[0] == 'E') {
            /* E <mode> <target> <clipW> <boxW> <boxH> <pct> <step> <guard> <space> <n> <w1..wn>: the auto-fit fix
               (tx_autofit_choice, text_scale.inc) with a stand-in for the game's layout 0x413A64: words of
               the given widths wrapped greedily at the width (a space between words), height = lines x step,
               *outW = the widest line; <guard> is the line step given to the fix (= step; larger for the mutation
               check). Prints "EF <changed> <newW> <newH>". */
            int mode, target, clipW, bw, bh, pct, step, guard, i, nw = 0, nh = 0, r;
            static int lay[66];
            if (scanf("%d %d %d %d %d %d %d %d %d %d", &mode, &target, &clipW, &bw, &bh, &pct, &step, &guard, &lay[0], &lay[1]) != 10) return 1;
            if (lay[1] < 0 || lay[1] > 64) return 1;
            for (i = 0; i < lay[1]; i++) if (scanf("%d", &lay[2 + i]) != 1) return 1;
            lay[65] = step;
            r = tx_autofit_choice(mode, target, clipW, bw, bh, pct, guard, harness_lay, lay, &nw, &nh);
            printf("EF %d %d %d\n", r, nw, nh);
        }
        else if (t[0] == 'V') { if (scanf("%d", &g_harnessLive) != 1) return 1; }
        else if (t[0] == 'C') {
            if (scanf("%d %d", &g_gameCur[0], &g_gameCur[1]) != 2) return 1;
        }
        else if (t[0] == 'D') {
            /* the game's mouse update (0x6307F0): read the state over the cursor block, add the
               movement to the old position, clamp; right-drag puts the cursor back (SetCursorPos) */
            int dx, dy, btn, drag, ox = g_gameCur[0], oy = g_gameCur[1];
            if (scanf("%d %d %d %d", &dx, &dy, &btn, &drag) != 4) return 1;
            g_gameCur[0] = dx; g_gameCur[1] = dy; g_gameCur[2] = 0;
            g_gameCur[3] = (btn & 1 ? 0x80 : 0) | (btn & 2 ? 0x8000 : 0);
            g_rdrag = (unsigned char)drag;
            ui_hud_mouse(g_gameCur, ox, oy);
            g_gameCur[0] += ox; g_gameCur[1] += oy;
            if ((btn & 2) && drag) { g_gameCur[0] = ox; g_gameCur[1] = oy; }
            if (g_gameCur[0] > SW) g_gameCur[0] = SW; if (g_gameCur[0] < 0) g_gameCur[0] = 0;
            if (g_gameCur[1] > SH) g_gameCur[1] = SH; if (g_gameCur[1] < 0) g_gameCur[1] = 0;
            printf("D %d %d %d %d %.3f %.3f\n", g_gameCur[0], g_gameCur[1], g_hmRX, g_hmRY, g_hmDX, g_hmDY);
        }
        else if (t[0] == 'U') {
            float x, y, gx; int col, bar, part; if (scanf("%f %f %d %d", &x, &y, &col, &bar) != 4) return 1;
            part = ui_hud_aim(x, y, col, bar, &gx);
            printf("U %d %.4f\n", part, gx);
        }
        else if (t[0] == 'B' || t[0] == 'b') {      /* b: each vertex also has tu tv (texture coordinates) */
            unsigned c, m; int n, k; Pending p;
            if (scanf("%x %x %d", &c, &m, &n) != 3) return 1;
            if (nv + n > MAXV || nb >= MAXB) return 2;
            for (k = 0; k < n; k++) {
                if (scanf("%f %f %f %f", &vb[nv+k][0], &vb[nv+k][1], &vb[nv+k][2], &vb[nv+k][3]) != 4) return 1;
                if (t[0] == 'b' && scanf("%f %f", &vb[nv+k][6], &vb[nv+k][7]) != 2) return 1;
            }
            memset(&p, 0, sizeof p);
            p.v = vb[nv]; p.n = n; p.caller = c; p.a1 = m; p.td = g_lastX; g_lastX = -1;
            ui_record(&p);
            bufs[nb] = vb[nv]; ns[nb] = n; nb++; nv += n;
        }
    }
    fputs(g_barsOut, stdout);
    for (int i = 0; i < nb; i++) {
        printf("B %d\n", ns[i]);
        for (int k = 0; k < ns[i]; k++) printf("%.3f %.3f\n", bufs[i][k*8], bufs[i][k*8+1]);
    }
    return 0;
}
