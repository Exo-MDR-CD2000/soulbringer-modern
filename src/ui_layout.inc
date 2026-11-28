/* UI layout for Soulbringer Modern. Included by soulbringer_modern.c and by
 * tests/replay/harness.c (which supplies stand-ins for the logging hooks), so
 * the tests run exactly the code that ships.
 *
 * Needs from the includer: EXE_CURSOR, EXE_RDRAG, g_cfgHudMouse, g_cfgText, g_gts (game-side text scale, 1 = off), DWORD, Pending, Cap, g_cap/g_ncap/g_capturing/
 * g_capReq/g_marker, g_cfgLog/g_cfgAuto/g_cfgCursor/g_uiOn, EXE_SCREEN_W/H,
 * TEXT_CALLER, LOG(), flush(), ftoa(), GetAsyncKeyState(), GetCursorPos().
 */
/* ================================================================ UI layout
 * The game designs all 2D on a 640x480 canvas and draws it stretched to the
 * screen (x * W/640, y * H/480). Every UI vertex passes through AddTri and
 * stays in the renderer's buffer until RenderAllBuffers/EndFrame, so the
 * proxy re-places it there:
 *   - full-screen menus: pillarboxed to a centered 4:3 area
 *   - gameplay HUD: bottom bar anchored bottom-center, action column anchored
 *     to the right edge, both at 4:3 proportions
 * World geometry (z > 0) is never touched. See docs/DEVLOG.md 2026-11-21.
 */
#define C_TILES   0x004B686A   /* full-screen art, HUD tiles   */
#define C_SPRITE  0x004A9195   /* sprites, cursor              */
#define C_TEXT    TEXT_CALLER  /* glyph batches                */
#define C_HOVER   0x00456ABD   /* tooltip / hover boxes next to the cursor */

/* [Interface] Hud */
#define HUD_STRETCHED 0        /* original: HUD stretched over the full width            */
#define HUD_SPLIT     1        /* bottom bar bottom-center, action column at right edge  */
#define HUD_RIGHT     2        /* whole HUD as one 4:3 block in the bottom-right corner  */
#define HUD_CENTER    3        /* whole HUD as one 4:3 block, centered                   */

/* aspect cap (MaxAspect): last HUD tile seen, imitated to draw the black bars */
static DWORD g_barBucket, g_barMat, g_barA3; static int g_barHave, g_barsDone, g_capped;
static float g_barU, g_barV;

typedef struct { float* v; int n; DWORD caller; DWORD mat; DWORD bucket, a3; int grp; float x0, x1, y0, y1; int cap; } UiBuf;
#define MAXUI 4096
static UiBuf g_ui[MAXUI];
static int   g_nui;
/* per-frame evidence */
static int   g_fMenu, g_fWorld, g_fCol, g_fBar;
static int   g_pMenu, g_pCol, g_pBar;  /* previous frame */
static int   g_fConv, g_pConv;         /* conversation: the letterbox bands were drawn (this / previous frame) */
/* conversation portrait hold (PROVISIONAL, may be removed): the last portrait drawn (after the
   layout), redrawn in a frame with the letterbox but without the portrait. The game skips the
   portrait for 1 frame each time the dialogue moves to its next line (the "flicker": reported
   2026-11-23, measured in sessions 20048 and 1668). It may be the original game's behaviour;
   if so, turn it off with [Debug] ConversationPortraitHold=0, or remove it: everything is in
   g_cfgPortraitHold, ui_conv_end(), the g_port* saves in ui_flush, ui_draw_quad() and the
   "e"/"HQ" harness lines (DEVLOG 2026-11-26 "conversation screen", section "How to remove"). */
static int   g_fPort, g_portHave, g_convState = -1;
static float g_portV[48]; static DWORD g_portBucket, g_portMat, g_portA3, g_portFrame;
static DWORD g_convHeldRun, g_convMissRun;
#define CONV_HOLD_FRAMES 30
static int   g_mode = -1;         /* decided for the current frame: 0 game, 1 menu */
/* diagnostics, reported in the debug log */
static DWORD g_dRec, g_dWorld, g_dFlush, g_dMoved, g_dMenuFrames, g_dGameFrames, g_dCursor;
static float g_dBefore[4], g_dAfter[4]; static int g_dHave;
/* screen fingerprint: the set of (caller, material) pairs of UI drawn in a frame */
#define MAXSIG 96
static DWORD g_sigPair[MAXSIG][2]; static int g_nsig;
static DWORD g_sigLast, g_sigStable, g_sigFrame;
#define MAXSEEN 256
static DWORD g_seen[MAXSEEN]; static int g_nseen;
static DWORD g_curSig;              /* fingerprint of the last completed frame */
/* cursor as the game placed it, and where the layout drew it */
static float g_curGX, g_curGY, g_curDX, g_curDY; static int g_curSeen;
static int   g_btn[3];

#include "hover_log.inc"

static int near(float a, float b) { float d = a - b; return d > -0.6f && d < 0.6f; }

static void ui_record(const Pending* p) {
    float* v = (float*)p->v;
    int k;
    float W = (float)*EXE_SCREEN_W, H = (float)*EXE_SCREEN_H;
    /* World geometry has rhw < 0.002; flat UI has rhw ~1.0 and UI 3D models
       rhw ~0.33. (Depth is not usable: UI depth is tiny but not exactly 0.) */
    for (k = 0; k < p->n; k++) if (v[k * 8 + 3] < 0.1f) { g_fWorld = 1; g_dWorld++; if (g_cfgLog) hover_world(p); return; }
    if (g_cfgLog) hover_ui(p);
    if (p->caller == C_TILES && p->n == 6) {
        float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
        for (k = 0; k < 6; k++) {
            float x = v[k*8] * 640.0f / W, y = v[k*8+1] * 480.0f / H;
            if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
        }
        if (near(x0, 0) && near(x1, 213.333f) && near(y0, 0) && near(y1, 240)) g_fMenu = 1;     /* 6-tile background */
        if (near(x0, 536.667f) && near(x1, 640) && y1 < 421) {                                  /* right column tile */
            g_fCol = 1;
            /* remember it: the aspect-cap bars are drawn like this tile */
            g_barBucket = p->a0; g_barMat = p->a1; g_barA3 = p->a3; g_barHave = 1;
            g_barU = (v[6] + v[14] + v[22]) / 3.0f; g_barV = (v[7] + v[15] + v[23]) / 3.0f;
        }
        if (near(y0, 420) && near(y1, 480) && x1 - x0 > 150 && x1 - x0 < 300) g_fBar = 1;        /* bottom bar tile */
        if (near(x0, 0) && near(x1, 640) && near(y0, 0) && near(y1, 60)) g_fConv = 1;           /* conversation: top letterbox band */
    }
    g_dRec++;
    {
        int i;
        for (i = 0; i < g_nsig; i++) if (g_sigPair[i][0] == p->caller && g_sigPair[i][1] == p->a1) break;
        if (i == g_nsig && g_nsig < MAXSIG) { g_sigPair[g_nsig][0] = p->caller; g_sigPair[g_nsig][1] = p->a1; g_nsig++; }
    }
    if (g_nui < MAXUI) {
        UiBuf* b = &g_ui[g_nui++];
        b->v = v; b->n = p->n; b->caller = p->caller; b->mat = p->a1; b->bucket = p->a0; b->a3 = p->a3;
        b->cap = (g_capturing && g_ncap > 0 && g_cap[g_ncap - 1].vp == p->v) ? g_ncap - 1 : -1;
    }
}

/* record the layout decision for one UI buffer in the frame capture */
static void ui_note(const UiBuf* b, char how, char kind) {
    Cap* c; int k;
    if (b->cap < 0) return;
    c = &g_cap[b->cap];
    if (c->how && c->how != how) how = '*';            /* quads of one buffer got different rules */
    c->how = how; c->kind = kind;
    c->ax0 = c->ay0 = 1e30f; c->ax1 = c->ay1 = -1e30f;
    for (k = 0; k < b->n; k++) {
        float x = b->v[k*8], y = b->v[k*8+1];
        if (x < c->ax0) c->ax0 = x; if (x > c->ax1) c->ax1 = x; if (y < c->ay0) c->ay0 = y; if (y > c->ay1) c->ay1 = y;
    }
}

/* x mappings, screen px -> screen px */
static float g_W, g_H, g_sx, g_s, g_off;
static float g_capL, g_capR;          /* visible area: 0..W, or the centered MaxAspect area */
static float map_center(float x) { return g_off + (x / g_sx) * g_s; }
static float map_right(float x)  { return g_capR - (640.0f - x / g_sx) * g_s; }
static float map_capfit(float x) { return g_capL + x * (g_capR - g_capL) / g_W; }   /* stretched HUD inside the cap */

static int ui_how(float x0, float x1, float y0, float y1, int menu, int col, int bar) {
    float x0c = x0 / g_sx, cyc = (y0 + y1) * 0.5f / g_s;
    int hudCol = x0c >= 536.0f && y1 / g_s <= 421.0f, hudBar = cyc >= 405.0f;
    (void)x1;
    if (!g_cfgLayout)                  /* original stretched UI, only squeezed into the aspect cap */
        return g_capped && (menu || (col && hudCol) || (bar && hudBar)) ? 3 : 0;
    if (menu) return 1;                                            /* pillarbox */
    /* HudLayout: where the two HUD parts go (1 = centered, 2 = right edge) */
    if (col && x0c >= 536.0f && y1 / g_s <= 421.0f)               /* right action column */
        return g_cfgHud == HUD_SPLIT || g_cfgHud == HUD_RIGHT ? 2 : g_cfgHud == HUD_CENTER ? 1 : g_capped ? 3 : 0;
    if (bar && cyc >= 405.0f)                                      /* bottom bar */
        return g_cfgHud == HUD_RIGHT ? 2 : g_cfgHud == HUD_SPLIT || g_cfgHud == HUD_CENTER ? 1 : g_capped ? 3 : 0;
    return 0;
}

static float ui_map(int how, float x) { return how == 1 ? map_center(x) : how == 2 ? map_right(x) : how == 3 ? map_capfit(x) : x; }

/* Inverse of ui_map: screen x where the layout drew something -> screen x
   where the game has it (its original stretched position). */
static float ui_unmap(int how, float X) {
    return how == 1 ? (X - g_off) / g_s * g_sx
         : how == 2 ? (640.0f - (g_capR - X) / g_s) * g_sx
         : how == 3 ? g_capL >= g_capR ? X : (X - g_capL) * g_W / (g_capR - g_capL)
         : X;
}

/* Conversation screen (2026-11-26): the game draws the NPC portrait (sprite 0x4A9195, its own
   material per NPC, 144 x 144 canvas units at x 470.5-614.5, y 19.1-163.1, gold frame included)
   and the dialogue box (0x456ABD, right edge at 467.2) with its text right-aligned in it, at
   canvas positions: on a 16:9 screen the portrait is stretched 4:3 -> 16:9 (864 x 648 at 4K).
   Text and box are sized in screen pixels (not stretched), only placed by the canvas.
   With Layout on, the portrait keeps its square shape and goes to the top right as the HUD's
   right edge does (map_right; centred with HudLayout=center), and the box with its text moves
   by the same amount as the portrait's left edge, so the gap between them stays. */
static int ui_conv_portrait(float x0, float x1, float y0, float y1) {
    float a = x0 / g_sx, b = x1 / g_sx, c = y0 / g_s, d = y1 / g_s;
    if (a > 465.0f && a < 476.0f && b > 609.0f && b < 620.0f && c > 14.0f && c < 25.0f && d > 157.0f && d < 169.0f) return 1;
    /* street talk (guards and passers-by, no letterbox; session 28448): the same 144 x 144 portrait
       at canvas 363.8-507.8 x 32-176, the dialogue box's right edge at 360.5, 3.3 units left of it */
    if (a > 358.0f && a < 369.0f && b > 502.0f && b < 513.0f && c > 27.0f && c < 37.0f && d > 171.0f && d < 181.0f) return 2;
    return 0;
}
/* Street talk: the portrait is squared around its centre, which goes where it is in the 4:3 picture
   (map_center), and the box beside it moves by the same amount as the portrait's left edge, so the
   gap stays. The portrait is drawn after the box, so ui_flush looks for it first (this frame), else
   uses the last one seen within CONV_HOLD_FRAMES (the game skips it for a frame on each new line). */
static float g_strA, g_strB, g_strH;   /* last street portrait: left, right, height (screen px) */
static unsigned g_strFrame; static int g_strHave;
static float ui_street_left(void) { return map_center((g_strA + g_strB) * 0.5f) - g_strH * 0.5f; }
static float ui_conv_map(float x) { return g_cfgHud == HUD_CENTER ? map_center(x) : map_right(x); }

/* screen geometry for this frame: scales, 4:3 offset, MaxAspect area */
static void ui_geom(void) {
    g_W = (float)*EXE_SCREEN_W; g_H = (float)*EXE_SCREEN_H;
    g_sx = g_W / 640.0f; g_s = g_H / 480.0f; g_off = (g_W - 640.0f * g_s) * 0.5f;
    g_capL = 0; g_capR = g_W; g_capped = 0;
    if (g_cfgMaxAspect > 0 && g_W > g_H * g_cfgMaxAspect + 1.0f) {     /* screen wider than the cap */
        float wc = g_H * g_cfgMaxAspect;
        g_capL = (g_W - wc) * 0.5f; g_capR = g_capL + wc; g_capped = 1;
    }
}

/* Gameplay HUD click mapping (groundwork for step 2, also used by the debug
   log). Which HUD part the layout drew at screen point (X,Y), and the x where
   the game has that point: the game still hit-tests the HUD at its original
   stretched position. y is never moved. Returns 0 = no HUD drawn there
   (world), 1 = bottom bar, 2 = right column; *gx = the game's x (X for 0).
   col/bar: whether those HUD parts are on screen. Uses ui_how(), so it follows
   HudLayout, Layout and MaxAspect exactly like the drawing does. Rigid
   sprites are moved by their centre, so inside one the result can be off by
   up to a quarter of its half-width (a few px); art tiles map exactly. */
static int ui_hud_aim(float X, float Y, int col, int bar, float* gx) {
    float yc;
    *gx = X;
    ui_geom();
    if (g_W * 3.0f <= g_H * 4.0f + 1.0f) return 0;          /* 4:3: nothing is moved */
    yc = Y / g_s;
    if (col && yc < 420.0f) {                               /* same test order as ui_how: column first */
        int how = ui_how(536.667f * g_sx, g_W, 0.0f, 420.0f * g_s, 0, 1, 0);
        if (X >= ui_map(how, 536.667f * g_sx) && X <= ui_map(how, g_W)) { *gx = ui_unmap(how, X); return 2; }
    }
    if (bar && yc >= 405.0f) {
        int how = ui_how(0.0f, g_W, Y, Y, 0, 0, 1);
        if (X >= ui_map(how, 0.0f) && X <= ui_map(how, g_W)) { *gx = ui_unmap(how, X); return 1; }
    }
    return 0;
}

/* ---------------------------------------------------------- HUD mouse (step 2)
   The game hit-tests its HUD at the original stretched positions, in many
   places (one loop per screen). Instead of patching those, the cursor the
   game sees is changed at the one place it is made: in gameplay the game
   reads DirectInput's mouse state (GetDeviceState, 16 bytes) straight into
   its cursor block at 0x3559114 {x, y, z, buttons[4]} and then adds the
   movement to the previous x, y (see DEVLOG). In that call (dinput_mouse.inc)
   the real mouse position is kept here from the raw movement; when it is over
   a drawn HUD part, the game is given the point where the game has that part
   (ui_hud_aim), otherwise the real point. The cursor sprite and the tooltip
   next to it are then drawn at the real position (ui_flush, g_hmDX/DY).
   Over the world inside the game's old HUD area, the game gets the nearest
   point outside it (ui_hud_dead). Menus (where the cursor is drawn where the
   game has it) and HudLayout=stretched are left alone. Right-button camera
   drag: the game puts its cursor back each frame; the movement is passed
   through and the real position kept. */
/* EXE_CURSOR (game cursor x, y at 0x3559114) and EXE_RDRAG (0x6B840C,
   right-drag camera holds the cursor) come from the includer. */
static int   g_hmHave, g_hmRX, g_hmRY, g_hmCX, g_hmCY;  /* real position; game position we last produced */
static float g_hmDX, g_hmDY;       /* real - game, for drawing the cursor and its tooltip */
static int   g_prevMode = -1;      /* last decided mode (g_mode is -1 between frame start and flush) */

/* The world now shows where the stretched HUD used to be (beside the moved
   bar, left of the moved column), but the game still has its HUD there:
   its buttons answer (a second "Compass" tooltip left of the bar) and its
   coarse over-HUD test 0x5171E8 (x > 5/6 W with the column, y > 7/8 H)
   blocks world clicks. For a real point over the world, the game is given
   the nearest point outside its own HUD: above the bar line (canvas y 404,
   clear of the bar's ornaments from 405) and left of the column line
   (canvas x 533). Points already outside are unchanged. */
static void ui_hud_dead(int* x, int* y, int col, int bar) {
    float sx = (float)*EXE_SCREEN_W / 640.0f, sy = (float)*EXE_SCREEN_H / 480.0f;
    if (bar && *y >= (int)(405.0f * sy)) *y = (int)(404.0f * sy);
    if (col && *x >= (int)(533.0f * sx) && *y < (int)(420.0f * sy)) *x = (int)(533.0f * sx) - 1;
}

static int ui_hud_mouse_active(void) {
    int mode = g_mode >= 0 ? g_mode : g_prevMode;
    return g_cfgHudMouse && g_uiOn && mode == 0;
}

/* st: the DIMOUSESTATE the game just read (lX, lY, lZ, buttons); old: the game
   cursor before it adds lX, lY. Rewrites lX, lY so the game ends up where it should. */
static void ui_hud_mouse(int* st, int oldx, int oldy) {
    int W = *EXE_SCREEN_W, H = *EXE_SCREEN_H, cx, cy, part; float gx;
    if (!ui_hud_mouse_active() || W * 3 <= H * 4 + 1) { g_hmHave = 0; g_hmDX = g_hmDY = 0; return; }
    if (!g_hmHave || oldx != g_hmCX || oldy != g_hmCY) {    /* start, or the game moved its cursor itself */
        g_hmRX = oldx; g_hmRY = oldy;
    }
    if ((((unsigned char*)st)[13] & 0x80) && *EXE_RDRAG) {   /* right-drag camera: game holds its cursor */
        g_hmCX = oldx; g_hmCY = oldy; g_hmHave = 1;
        return;
    }
    g_hmRX += st[0]; g_hmRY += st[1];
    if (g_hmRX < 0) g_hmRX = 0; if (g_hmRX > W - 1) g_hmRX = W - 1;
    if (g_hmRY < 0) g_hmRY = 0; if (g_hmRY > H - 1) g_hmRY = H - 1;
    part = ui_hud_aim((float)g_hmRX, (float)g_hmRY, g_fCol || g_pCol, g_fBar || g_pBar, &gx);
    cx = part ? (int)(gx + 0.5f) : g_hmRX;
    cy = g_hmRY;
    if (!part) ui_hud_dead(&cx, &cy, g_fCol || g_pCol, g_fBar || g_pBar);
    if (cx < 0) cx = 0; if (cx > W - 1) cx = W - 1;
    st[0] = cx - oldx; st[1] = cy - oldy;
    g_hmCX = cx; g_hmCY = cy; g_hmHave = 1;
    g_hmDX = (float)(g_hmRX - cx); g_hmDY = (float)(g_hmRY - cy);
}

/* forward mapping of one HUD point, for the tests: where the layout draws a
   point the game put at (x,y), treating it as part of a stretched art tile */
__attribute__((unused)) static float ui_hud_place(float x, float y, int col, int bar) {
    ui_geom();
    if (g_W * 3.0f <= g_H * 4.0f + 1.0f) return x;
    return ui_map(ui_how(x, x, y, y, 0, col, bar), x);
}

static int ui_place(float* v, int n, int rigid, int cursor, int menu, int col, int bar) {
    float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
    int k, how;
    for (k = 0; k < n; k++) {
        float x = v[k*8], y = v[k*8+1];
        if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
    }
    how = ui_how(x0, x1, y0, y1, menu, col, bar);
    if (how) {
        if (rigid) {                                   /* keep shape, move its center */
            float cx = (x0 + x1) * 0.5f, dx = ui_map(how, cx) - cx;
            for (k = 0; k < n; k++) v[k*8] += dx;
            x0 += dx;
        } else {                                       /* drawn stretched: map every vertex */
            for (k = 0; k < n; k++) v[k*8] = ui_map(how, v[k*8]);
            x0 = ui_map(how, x0);
        }
    }
    if (cursor) {
        g_curDX = x0; g_curDY = y0;
        if (g_cfgCursor != 100) {                      /* enlarge around the hotspot (top-left) */
            float f = (float)g_cfgCursor / 100.0f;
            for (k = 0; k < n; k++) { v[k*8] = x0 + (v[k*8] - x0) * f; v[k*8+1] = y0 + (v[k*8+1] - y0) * f; }
        }
    }
    return how;
}

static char how_char(int how, int menu) { return how == 0 ? '-' : how == 2 ? 'R' : menu ? 'M' : 'B'; }

/* UI 3D models (inventory items, column icons) arrive one triangle per
   AddTri. Group touching triangles of the same material into one model and
   move each model as a whole, so it keeps its shape and has no seams.
   The game sizes these models by the screen WIDTH (with square pixels), so on
   a screen wider than 4:3 they come out W/(4H/3) too big next to their slot:
   at 16:9 the inventory apple is 1.34x its size at 1024x768 and covers the
   stack count (DEVLOG 2026-11-24). Each model is therefore also scaled about
   its centre by the local slope of the mapping it is moved by (4H/3W in the
   pillarboxed menus; 1 where nothing is narrowed, e.g. the stretched HUD). */
static int ui_find(int i) { while (g_ui[i].grp != i) i = g_ui[i].grp = g_ui[g_ui[i].grp].grp; return i; }

static void ui_models(int menu, int col, int bar) {
    int i, j, k;
    for (i = 0; i < g_nui; i++) {
        UiBuf* b = &g_ui[i];
        b->grp = -1;
        if (b->v[3] > 0.99f) continue;                 /* flat UI, not a model */
        b->grp = i;
        b->x0 = b->y0 = 1e30f; b->x1 = b->y1 = -1e30f;
        for (k = 0; k < b->n; k++) {
            float x = b->v[k*8], y = b->v[k*8+1];
            if (x < b->x0) b->x0 = x; if (x > b->x1) b->x1 = x; if (y < b->y0) b->y0 = y; if (y > b->y1) b->y1 = y;
        }
    }
    for (i = 0; i < g_nui; i++) {
        if (g_ui[i].grp < 0) continue;
        for (j = i + 1; j < g_nui; j++) {
            UiBuf *a = &g_ui[i], *b = &g_ui[j];
            if (b->grp < 0 || a->mat != b->mat || a->caller != b->caller) continue;
            if (a->x0 > b->x1 + 4 || b->x0 > a->x1 + 4 || a->y0 > b->y1 + 4 || b->y0 > a->y1 + 4) continue;
            { int ra = ui_find(i), rb = ui_find(j); if (ra != rb) g_ui[rb].grp = ra; }
        }
    }
    for (i = 0; i < g_nui; i++) {                      /* group bounds into the root */
        int r;
        if (g_ui[i].grp < 0) continue;
        r = ui_find(i);
        if (r == i) continue;
        if (g_ui[i].x0 < g_ui[r].x0) g_ui[r].x0 = g_ui[i].x0; if (g_ui[i].x1 > g_ui[r].x1) g_ui[r].x1 = g_ui[i].x1;
        if (g_ui[i].y0 < g_ui[r].y0) g_ui[r].y0 = g_ui[i].y0; if (g_ui[i].y1 > g_ui[r].y1) g_ui[r].y1 = g_ui[i].y1;
    }
    for (i = 0; i < g_nui; i++) {
        UiBuf* r; int how; float dx;
        if (g_ui[i].grp < 0) continue;
        r = &g_ui[ui_find(i)];
        how = ui_how(r->x0, r->x1, r->y0, r->y1, menu, col, bar);
        if (how && g_uiOn) {
            float cx = (r->x0 + r->x1) * 0.5f, cy = (r->y0 + r->y1) * 0.5f;
            float f = ui_map(how, cx + 0.5f) - ui_map(how, cx - 0.5f);   /* local slope of the mapping */
            dx = ui_map(how, cx) - cx;
            if (f > 0.999f || f < 0.25f) f = 1.0f;                       /* not narrowed (or nonsense): move only */
            for (k = 0; k < g_ui[i].n; k++) {
                float* v = g_ui[i].v + k * 8;
                v[0] = cx + dx + (v[0] - cx) * f;
                v[1] = cy + (v[1] - cy) * f;
            }
        }
        ui_note(&g_ui[i], how_char(how, menu), 'D');
    }
}

#define MAXTIP 16
/* this frame's tooltips, followed by the previous frame's: the game may draw
   a tooltip's text before its box, and tooltips stay put while hovering.
   Fields: 0-3 the box as the game drew it (+4 px, for matching its glyphs),
   4-5 offset dx dy, 6-7 anchor (cursor tip, game coordinates), 8 scale,
   9-10 shift that keeps the scaled box on screen, 11 one-line tooltip at
   the cursor (its text is only the line across the middle of the box: other
   text under the box, e.g. a HUD slot label, is not part of it). A point p
   of the tooltip is drawn at  anchor + offset + (p - anchor) * scale + shift. */
#define TIPF 12
static float g_tip[2 * MAXTIP][TIPF]; static int g_ntip, g_nptip;
static float g_ptip[MAXTIP][TIPF];
static float g_tipAX, g_tipAY; static int g_tipAHave;   /* cursor tip (game coordinates): live cursor, else sprite */
static float g_tipSX, g_tipSY; static int g_tipSHave;   /* last cursor sprite seen (fallback) */
static void tip_apply(const float* t, float* x, float* y) {
    *x = t[6] + t[4] + (*x - t[6]) * t[8] + t[9];
    *y = t[7] + t[5] + (*y - t[7]) * t[8] + t[10];
}
/* Is (x,y) part of tooltip t? Cursor tooltips: their one line of text has
   its glyph centres 21-25 px below the box top (recorded: 610 of 614); other
   text under the box was at 9-10 and 49 px. Accept 14-32 px. */
/* td_file[] indices of the fonts drawn inside cursor tooltips: hbut (tooltips), and mgrn / mred
   (the name of the character under the cursor, e.g. "Shopkeeper", in the same box) */
#define TIP_FONT(f) ((f) == 9 || (f) == 11 || (f) == 12)
static int tip_has(const float* t, float x, float y, int font) {
    if (x < t[0] || x > t[2] || y < t[1] || y > t[3]) return 0;
    /* text of another font under a cursor tooltip (e.g. the COMBO slot label, font 5px) is not its text
       (2026-11-23: with the text hook at 300% the box is 82 px tall and the label fell inside the band below,
       so it moved with the box when the box was moved off the cursor) */
    /* (2026-11-23, later: names of characters under the cursor are mgrn in a cursor tooltip box; left out,
       the box moved above the cursor without its name, and the glyphs past the column edge moved right) */
    if (t[11] && font >= 0 && !TIP_FONT(font)) return 0;
    /* the text line: inside the box less its 14 px margins (14..32 px below the top of a 46 px box;
       taller when the game itself draws larger text, TextHook) */
    return !t[11] || (y >= t[1] + 4.0f + 14.0f && y <= t[3] - 4.0f - 14.0f);
}
static int in_tip(float x, float y, int font) {       /* index of the tooltip containing (x,y), or -1; font: of a glyph, -1 unknown */
    int t;
    for (t = 0; t < g_ntip; t++) if (tip_has(g_tip[t], x, y, font)) return t;
    for (t = 0; t < g_nptip; t++)
        if (tip_has(g_ptip[t], x, y, font)) {
            int k; if (g_ntip >= 2 * MAXTIP) return -1;
            for (k = 0; k < TIPF; k++) g_tip[g_ntip][k] = g_ptip[t][k];
            return g_ntip++;
        }
    return -1;
}
static float g_ftip[MAXTIP][TIPF]; static int g_nftip;    /* collected over the whole frame */

static void ui_flush(void) {
    int i, q, menu, col, bar, conv, street;
    if (!g_nui) return;
    ui_geom();
    if (g_W * 3.0f <= g_H * 4.0f + 1.0f && g_uiOn) { g_nui = 0; return; }   /* 4:3 or narrower: nothing to do */

    /* Menu = the full-screen menu background was drawn in this frame. Menus
       always draw it before anything else; gameplay never draws it. (Do not
       wait for world geometry: gameplay flushes the HUD before the world, and
       do not carry the previous frame over: that lags one frame behind.) */
    g_mode = g_fMenu ? 1 : 0;
    menu = g_mode == 1;
    g_dFlush++;
    if (!g_dHave && menu && g_ui[0].caller == C_TILES && g_ui[0].n == 6) {
        g_dBefore[0] = g_ui[0].v[0]; g_dBefore[1] = g_ui[0].v[8]; g_dBefore[2] = g_ui[0].v[16]; g_dBefore[3] = g_ui[0].v[1];
        g_dHave = 1;
    }
    col = g_fCol || g_pCol;
    bar = g_fBar || g_pBar;
    conv = (g_fConv || g_pConv) && !menu && g_cfgLayout && g_uiOn;
    if (!menu) {                                   /* street talk portrait in this frame? (drawn after its box) */
        int t;
        for (t = 0; t < g_nui; t++) {
            const float* v = g_ui[t].v; int j; float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
            if (g_ui[t].caller != C_SPRITE || g_ui[t].n != 6) continue;
            for (j = 0; j < 6; j++) { if (v[j*8] < x0) x0 = v[j*8]; if (v[j*8] > x1) x1 = v[j*8];
                                      if (v[j*8+1] < y0) y0 = v[j*8+1]; if (v[j*8+1] > y1) y1 = v[j*8+1]; }
            if (ui_conv_portrait(x0, x1, y0, y1) == 2) { g_strA = x0; g_strB = x1; g_strH = y1 - y0; g_strFrame = g_sigFrame; g_strHave = 1; break; }
        }
    }
    street = !menu && g_cfgLayout && g_uiOn && g_strHave && g_sigFrame - g_strFrame <= CONV_HOLD_FRAMES;

    ui_models(menu, col, bar);

    /* Tooltip boxes drawn next to the cursor, and the text inside them, belong
       to the cursor: box and text move together by one transform.
       Offset: gameplay = the HUD mouse offset (real - game mouse, 0 over the
       world); menus = how the cursor itself is moved (its centre's mapping);
       boxes not at the cursor (fixed panels) = their left edge's mapping.
       Scale (TextScale): only one-line tooltips at the cursor (box 46 px tall,
       top-left within the game's cursor offsets: (-17,-38), (-17,+2),
       (+11,+30), (+11,-10); or, for inventory items, centred on the cursor
       (centre about +2 px) 88 px below it). Corner tooltips scale around the
       cursor tip (the gap grows with the cursor); item tooltips scale around
       their own centre (they stay over the item's cell). Messages in
       gameplay (one line, centred on the screen, e.g. "Box Empty") scale
       around their bottom centre, so they grow upwards above the HUD. Box, glyphs, margins and the gap to the cursor
       all scale around the cursor tip, so the tooltip stays where the game
       puts it relative to the cursor, only bigger; then it is shifted back on
       screen if it sticks out. Multi-line panels are not scaled here (their
       text wraps inside the game's layout code: step 3, later). */
    {
        int t;
        for (t = 0; t < g_nui; t++) {                  /* the cursor tip in this flush, if drawn */
            float* v = g_ui[t].v; float w, h;
            if (g_ui[t].caller != C_SPRITE || g_ui[t].n != 6) continue;
            w = v[8] - v[0]; if (w < 0) w = -w; h = v[17] - v[1]; if (h < 0) h = -h;
            { int j; float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
              for (j = 0; j < 6; j++) { if (v[j*8] < x0) x0 = v[j*8]; if (v[j*8] > x1) x1 = v[j*8];
                                        if (v[j*8+1] < y0) y0 = v[j*8+1]; if (v[j*8+1] > y1) y1 = v[j*8+1]; }
              if (x1 - x0 > 31.5f && x1 - x0 < 32.5f && y1 - y0 > 31.5f && y1 - y0 < 32.5f) {
                  g_tipAX = g_tipSX = x0; g_tipAY = g_tipSY = y0; g_tipAHave = g_tipSHave = 1; } }
            (void)w; (void)h;
        }
#ifdef UI_LIVE_CURSOR
        /* In game, read the cursor the game is using this frame. The sprite
           above is often drawn after the tooltip (so it is last frame's) and
           while the mouse moves, especially over the moved HUD where the game
           cursor moves faster than the mouse, last frame's cursor can be far
           enough off that a tooltip is not recognised for a frame (session
           22:41: a small box flashed at the game's own position). The sprite
           is drawn 2 px up and left of the game's cursor value. */
        if (UI_LIVE_CURSOR) { g_tipAX = (float)EXE_CURSOR[0] - 2.0f; g_tipAY = (float)EXE_CURSOR[1] - 2.0f; g_tipAHave = 1; }
#endif
    }
    {
        int t;                     /* g_ntip is reset once per frame, not per flush: */
        for (t = 0; t < g_nui && g_ntip < MAXTIP; t++) {   /* a box and its text can be in different flushes */
            int k; float* v = g_ui[t].v;
            if (g_ui[t].caller != C_HOVER || g_ui[t].n % 6) continue;
            for (k = 0; k < g_ui[t].n && g_ntip < MAXTIP; k += 6) {
                float ax = 1e30f, bx = -1e30f, ay = 1e30f, by = -1e30f; int j;
                for (j = k; j < k + 6; j++) {
                    float x = v[j*8], y = v[j*8+1];
                    if (x < ax) ax = x; if (x > bx) bx = x; if (y < ay) ay = y; if (y > by) by = y;
                }
                if (ax < 0) continue;                  /* off-screen helper quad */
                {
                    int slot = g_ntip, t2;               /* same box already listed (copied from the last frame)? update it */
                    float* tp;
                    for (t2 = 0; t2 < g_ntip; t2++)
                        if (g_tip[t2][0] == ax - 4 && g_tip[t2][1] == ay - 4 && g_tip[t2][2] == bx + 4 && g_tip[t2][3] == by + 4) { slot = t2; break; }
                    tp = g_tip[slot];
                    float sc = g_cfgText / 100.0f, anx = g_tipAX, any_ = g_tipAY;
                    int corner = 0, centred = 0, pass;
                    /* one line: 46 px (18 px text + 2 x 14), plus the text's growth when the
                       game draws it larger itself (g_gts, TextHook) */
                    int oneLine = by - ay > 40.0f && by - ay < 52.0f + 20.0f * (g_gts - 1.0f);
                    int atCursor;
                    for (pass = 0; pass < 2 && !corner && !centred; pass++) {   /* live cursor, then the sprite */
                        float ox, oy, mx;
                        if (pass) { if (!g_tipSHave) break; anx = g_tipSX; any_ = g_tipSY; }
                        ox = ax - anx; oy = ay - any_;
                        mx = (ax + bx) * 0.5f - anx;           /* inventory items: box centred under the cursor */
                        corner = ox > -30.0f && ox < 30.0f && oy > -50.0f - 24.0f * (g_gts - 1.0f) && oy < 40.0f;
                        centred = !corner && mx > -8.0f && mx < 12.0f && oy > 70.0f && oy < 110.0f;
                    }
                    atCursor = g_tipAHave && oneLine && (corner || centred);
                    /* messages (chest contents, doors): one line, centred on the screen, just above the HUD */
                    float sc2 = (ax + bx) * 0.5f - g_W * 0.5f;
                    int message = !menu && !atCursor && oneLine && sc2 > -3.0f && sc2 < 3.0f;
                    /* the journal's "A new quest has been added to your journal." (session 2184): same place
                       just above the HUD, but the game centres it on the view left of the HUD column (canvas
                       ~269, not 320), so it sat 305 px left of the screen centre at 4K. With Layout on it is
                       treated as a message and centred on the screen like the others (over the HUD bar). */
                    float mcx = (ax + bx) * 0.5f / g_sx;
                    int offMsg = !menu && !atCursor && oneLine && !message && by / g_s > 400.0f && by / g_s < 414.0f && mcx > 255.0f && mcx < 285.0f;
                    if (offMsg) message = 1;
                    tp[0] = ax - 4; tp[1] = ay - 4; tp[2] = bx + 4; tp[3] = by + 4;
                    tp[6] = anx; tp[7] = any_; tp[9] = tp[10] = 0; tp[11] = (float)atCursor;
                    if (message) {                         /* grow upwards from the bottom centre, so the */
                        tp[6] = (ax + bx) * 0.5f;          /* message stays just above the HUD (session   */
                        tp[7] = by; tp[11] = 1.0f;         /* 22:18); its text is the line in the middle  */
                    }
                    if (atCursor && centred) {             /* item tooltips grow around their own centre: */
                        tp[6] = (ax + bx) * 0.5f;          /* the game puts them over the item's cell, 88 px */
                        tp[7] = (ay + by) * 0.5f;          /* below the cursor; scaling that gap moved them  */
                    }                                      /* into the cell below (session 21:58)            */
                    tp[8] = (atCursor || message) && g_uiOn && g_cfgText != 100 ? sc : 1.0f;
                    if (menu) {
                        float c = atCursor ? anx + 16.0f : ax;       /* follow the cursor as it is moved */
                        tp[4] = g_cfgLayout ? map_center(c) - c : g_capped ? map_capfit(c) - c : 0.0f;
                        tp[5] = 0;
                    } else { tp[4] = atCursor ? g_hmDX : 0.0f; tp[5] = atCursor ? g_hmDY : 0.0f; }   /* fixed boxes stay put */
                    if (offMsg && g_cfgLayout && g_uiOn) tp[4] = g_W * 0.5f - (ax + bx) * 0.5f;     /* re-centred (see offMsg) */
                    if (conv && !atCursor && bx / g_sx > 440.0f && bx / g_sx < 480.0f && by / g_s < 200.0f)
                        tp[4] = ui_conv_map(bx) - bx;     /* conversation: the dialogue box follows the portrait */
                    else if (street && !atCursor && bx / g_sx > 355.0f && bx / g_sx < 366.0f && by / g_s < 200.0f)
                        tp[4] = ui_street_left() - g_strA; /* street talk: the box follows the portrait's left edge */
                    if (atCursor && g_uiOn) {
                        /* never over the cursor (2026-11-23: with larger text the game's box, placed at a
                           fixed offset for its 46 px height, grew down over the 200% cursor). Cursor sprite:
                           32 x 32 with its top-left at (anx, any_), enlarged by CursorSize around that corner;
                           cursor and box get the same layout shift (tp[4], tp[5]), so compare before it.
                           Corner tooltips go above the cursor (below if there is no room), item tooltips
                           below it (above if there is no room). */
                        float cs = (float)g_cfgCursor / 100.0f, gap = 6.0f;
                        float cx0 = anx, cy0 = any_, cx1 = cx0 + 32.0f * cs, cy1 = cy0 + 32.0f * cs;
                        float x0 = tp[6] + (ax - tp[6]) * tp[8], x1 = tp[6] + (bx - tp[6]) * tp[8];
                        float y0 = tp[7] + (ay - tp[7]) * tp[8], y1 = tp[7] + (by - tp[7]) * tp[8];
                        if (x0 < cx1 && x1 > cx0 && y0 < cy1 && y1 > cy0) {
                            float up = (cy0 - gap) - y1, down = (cy1 + gap) - y0;
                            if (centred) tp[10] = y1 + down <= g_H ? down : up;
                            else tp[10] = y0 + up >= 0.0f ? up : down;
                        }
                    }
                    if (tp[8] != 1.0f || tp[10] != 0.0f) {     /* keep the box on screen (menus: in the menu area) */
                        float x0 = ax, y0 = ay, x1 = bx, y1 = by;
                        float L = menu ? (g_cfgLayout ? map_center(0.0f) : g_capL) : 0.0f;
                        float R = menu ? (g_cfgLayout ? map_center(g_W) : g_capR) : g_W;
                        tip_apply(tp, &x0, &y0); tip_apply(tp, &x1, &y1);
                        if (x1 > R) tp[9] += R - x1; else if (x0 < L) tp[9] += L - x0;
                        if (y1 > g_H) tp[10] += g_H - y1; else if (y0 < 0) tp[10] += -y0;
                    }
                    if (g_nftip < MAXTIP) { int k3; for (k3 = 0; k3 < TIPF; k3++) g_ftip[g_nftip][k3] = tp[k3]; g_nftip++; }
                    if (slot == g_ntip) g_ntip++;
                }
            }
        }
    }

    for (i = 0; i < g_nui; i++) {
        float* v = g_ui[i].v;
        int n = g_ui[i].n;
        DWORD c = g_ui[i].caller;
        if (v[3] <= 0.99f) continue;               /* UI 3D model: done by ui_models */
        if (!g_uiOn) { ui_note(&g_ui[i], '-', '-'); continue; }   /* logging only */
        if (n % 6) {                               /* not quads: whole buffer is one element */
            ui_note(&g_ui[i], how_char(ui_place(v, n, 1, 0, menu, col, bar), menu), 'G');
            continue;
        }
        for (q = 0; q < n; q += 6) {               /* one quad per element */
            float* e = v + q * 8;
            float w = 0, h = 0; int k; float ax = 1e30f, bx = -1e30f, ay = 1e30f, by = -1e30f;
            int rigid, cursor;
            for (k = 0; k < 6; k++) {
                float x = e[k*8], y = e[k*8+1];
                if (x < ax) ax = x; if (x > bx) bx = x; if (y < ay) ay = y; if (y > by) by = y;
            }
            w = bx - ax; h = by - ay;
            cursor = c == C_SPRITE && n == 6 && w > 31.5f && w < 32.5f && h > 31.5f && h < 32.5f;
            rigid  = c == C_TEXT || cursor || (c == C_SPRITE && n == 6 && w - h < 1.0f && h - w < 1.0f);
            if (cursor) { g_curGX = ax; g_curGY = ay; g_curSeen = 1; }
            if (!menu && c == C_SPRITE && n == 6 && w > 0.0f && street && ui_conv_portrait(ax, bx, ay, by) == 2) {
                float L = ui_street_left(), f = h / w;      /* square again, centre where it is in the 4:3 picture */
                for (k = 0; k < 6; k++) e[k*8] = L + (e[k*8] - ax) * f;
                ui_note(&g_ui[i], how_char(1, 0), 'G');
                g_dMoved++;
                continue;
            }
            if (!menu && c == C_SPRITE && n == 6 && w > 0.0f && ui_conv_portrait(ax, bx, ay, by) == 1) {
                g_fPort = 1;
                if (conv) {
                    float R = ui_conv_map(bx), f = h / w;   /* square again, right edge where the HUD's would be */
                    int k5;
                    for (k = 0; k < 6; k++) e[k*8] = R - (bx - e[k*8]) * f;
                    for (k5 = 0; k5 < 48; k5++) g_portV[k5] = e[k5];
                    g_portBucket = g_ui[i].bucket; g_portMat = g_ui[i].mat; g_portA3 = g_ui[i].a3;
                    g_portFrame = g_sigFrame; g_portHave = 1;
                    ui_note(&g_ui[i], how_char(g_cfgHud == HUD_CENTER ? 1 : 2, 0), 'G');
                    g_dMoved++;
                    continue;
                }
            }
            /* tooltips and their text: one rigid offset per tooltip (see above) */
            {
                int font = -1, tip;
                if (c == C_TEXT) {                     /* which font: the glyph's texture corner (min tu, tv) */
                    float tu = 1e30f, tv = 1e30f; int k4;
                    for (k4 = 0; k4 < 6; k4++) { if (e[k4*8+6] < tu) tu = e[k4*8+6]; if (e[k4*8+7] < tv) tv = e[k4*8+7]; }
                    font = tx_glyph_font((int)g_ui[i].mat, (int)(tu * 256.0f + 0.25f), (int)(tv * 256.0f + 0.25f));   /* texel / 256, with or without a half-texel offset */
                }
                tip = (c == C_HOVER && ax >= 0) ? in_tip((ax + bx) * 0.5f, (ay + by) * 0.5f, -1)
                    : c == C_TEXT ? in_tip((ax + bx) * 0.5f, (ay + by) * 0.5f, font) : -1;
                if (tip >= 0) {
                    int k2; const float* tp = g_tip[tip];
                    for (k2 = 0; k2 < 6; k2++) tip_apply(tp, &e[k2*8], &e[k2*8+1]);
                    ui_note(&g_ui[i], tp[4] != 0.0f || tp[5] != 0.0f || tp[8] != 1.0f ? how_char(1, menu) : '-', 'T');
                    continue;
                }
            }
            /* gameplay cursor: never anchored, it follows the real mouse
               (the world is picked at the true mouse position) */
            {
                int free = !menu && cursor;
                if (free && (g_hmDX != 0.0f || g_hmDY != 0.0f)) {        /* drawn at the real mouse */
                    int k3; for (k3 = 0; k3 < 6; k3++) { e[k3*8] += g_hmDX; e[k3*8+1] += g_hmDY; }
                }
                ui_note(&g_ui[i], how_char(ui_place(e, 6, rigid, cursor, menu, free ? 0 : col, free ? 0 : bar), menu),
                        cursor ? 'C' : rigid ? 'G' : 'S');
            }
            g_dMoved++; if (cursor) g_dCursor++;
        }
    }
    if (g_capped && !menu && !g_barsDone && g_barHave && g_uiOn) {
        ui_draw_bars(0, g_capL, g_capR, g_W, g_H, g_barBucket, g_barMat, g_barA3, g_barU, g_barV);
        g_barsDone = 1;
    }
    if (g_dHave == 1) {
        g_dAfter[0] = g_ui[0].v[0]; g_dAfter[1] = g_ui[0].v[8]; g_dAfter[2] = g_ui[0].v[16]; g_dAfter[3] = g_ui[0].v[1];
        g_dHave = 2;
    }
    g_nui = 0;
}

static int g_lastMode = -1;

static DWORD sig_hash(void) {
    int i, j; DWORD h = 2166136261u;
    for (i = 1; i < g_nsig; i++) {                     /* sort pairs so order does not matter */
        DWORD a = g_sigPair[i][0], b = g_sigPair[i][1];
        for (j = i - 1; j >= 0 && (g_sigPair[j][0] > a || (g_sigPair[j][0] == a && g_sigPair[j][1] > b)); j--) {
            g_sigPair[j+1][0] = g_sigPair[j][0]; g_sigPair[j+1][1] = g_sigPair[j][1];
        }
        g_sigPair[j+1][0] = a; g_sigPair[j+1][1] = b;
    }
    for (i = 0; i < g_nsig; i++) { h = (h ^ g_sigPair[i][0]) * 16777619u; h = (h ^ g_sigPair[i][1]) * 16777619u; }
    if (g_fMenu) h ^= 0x4D454E55;
    return h;
}

/* Click diagnostics (Log=1).
   aim:    which HUD part is drawn where the click landed, where the game has
           that point, and where the game actually hit-tests (its cursor).
           "miss" = how far the game's hit-test is from what was clicked on.
   result: what the click did, CLICK_SETTLE frames later (or at the next
           click): mode and screen fingerprint before and after. Tooltips
           are part of the fingerprint, so "screen changed" alone can be a
           tooltip; a mode change is a menu opening or closing. */
#define CLICK_SETTLE 20
static int g_clkPending, g_clkBtn, g_clkMode; static DWORD g_clkFrame, g_clkSig;

__attribute__((noinline)) static void ui_log_aim(float rx, float cx, float cy) {
    char f[7][32]; float gx;
    int part = ui_hud_aim(rx, cy, g_fCol || g_pCol, g_fBar || g_pBar, &gx);
    const char* game = h_region(0, cx, cy);
    LOG("        aim: drawn %s  -> game point (%s,%s) [canvas %s,%s]  game hit-tests (%s,%s) as %s  miss %s px\r\n",
        part == 1 ? "hud-bar" : part == 2 ? "hud-column" : "world",
        ftoa(gx, f[0]), ftoa(cy, f[1]), ftoa(gx * 640.0f / g_W, f[2]), ftoa(cy * 480.0f / g_H, f[3]),
        ftoa(cx, f[4]), ftoa(cy, f[5]), game, ftoa(cx - gx, f[6]));
    if (g_hmHave) LOG("        hud mouse: real (%d,%d), game cursor (%d,%d)\r\n", g_hmRX, g_hmRY, g_hmCX, g_hmCY);
}

__attribute__((noinline)) static void ui_click_result(void) {
    int mode = g_mode >= 0 ? g_mode : g_lastMode;
    DWORD sig = g_sigLast;
    LOG("event: click result %s from frame %u: mode %s -> %s, screen %08X -> %08X%s\r\n",
        g_clkBtn == 0 ? "L" : g_clkBtn == 1 ? "R" : "M", g_clkFrame,
        g_clkMode == 1 ? "menu" : "game", mode == 1 ? "menu" : "game", g_clkSig, sig,
        mode != g_clkMode ? (mode == 1 ? "  (opened a menu)" : "  (back to game)") : sig == g_clkSig ? "  (no change)" : "  (screen changed)");
    g_clkPending = 0;
}

__attribute__((noinline)) static void ui_events(void) {
    char f[4][32];
    hover_frame_end(g_mode == 1);
    /* a new screen: fingerprint changed and stayed the same for 8 frames */
    DWORD sig = sig_hash();
    g_curSig = sig;
    if (sig == g_sigLast) g_sigStable++; else { g_sigLast = sig; g_sigStable = 0; }
    if (g_sigStable == 8) {
        int i, seen = 0;
        for (i = 0; i < g_nseen; i++) if (g_seen[i] == sig) seen = 1;
        LOG("event: screen %08X stable (%s, %d UI parts, frame %u)%s\r\n", sig, g_mode == 1 ? "menu" : "game", g_nsig, g_sigFrame,
            seen ? ", seen before" : "");
        if (!seen && g_nseen < MAXSEEN) {
            g_seen[g_nseen++] = sig;
            if (g_cfgAuto && !g_capturing && !g_capReq) { g_capReq = 1; g_marker++; LOG("event: auto capture %d of screen %08X\r\n", g_marker, sig); }
        }
        flush();
    }
    if (g_mode != g_lastMode && g_mode >= 0) {
        LOG("event: mode %s -> %s at frame %u\r\n", g_lastMode == 1 ? "menu" : g_lastMode == 0 ? "game" : "none", g_mode == 1 ? "menu" : "game", g_sigFrame);
        g_lastMode = g_mode;
    }
    /* mouse buttons: Windows state (menus) and the game's DirectInput mouse
       (gameplay, see dinput_mouse.inc), with what the cursor is on */
    {
        int b;
        DWORD di = g_diDown | g_diPress;
        g_diPress = 0;
        for (b = 0; b < 3; b++) {
            int os = (GetAsyncKeyState(b == 0 ? 1 : b == 1 ? 2 : 4) & 0x8000) != 0;
            int dn = (di >> b) & 1;
            int down = os || dn;
            if (down && !g_btn[b]) {
                POINT_ pt; int i;
                float cx = g_hPCurX, cy = g_hPCurY;
                pt.x = pt.y = -1; GetCursorPos(&pt);
                LOG("event: click %s (%s)  cursor (%s,%s) [canvas %s,%s] %s  os (%d,%d)  screen %08X %s frame %u\r\n",
                    b == 0 ? "L" : b == 1 ? "R" : "M", os && dn ? "os+di" : dn ? "di" : "os",
                    ftoa(cx, f[0]), ftoa(cy, f[1]),
                    ftoa(cx * 640.0f / (float)*EXE_SCREEN_W, f[2]), ftoa(cy * 480.0f / (float)*EXE_SCREEN_H, f[3]),
                    cx < -1e8f ? "no-cursor" : h_region(g_mode == 1, cx, cy), pt.x, pt.y,
                    g_curSig, g_mode == 1 ? "menu" : "game", g_sigFrame);
                if (g_hLHave)
                    LOG("        on label \"%s\"  box (%s,%s)..(%s,%s)\r\n", g_hLStr,
                        ftoa(g_hLBox[0], f[0]), ftoa(g_hLBox[1], f[1]), ftoa(g_hLBox[2], f[2]), ftoa(g_hLBox[3], f[3]));
                for (i = 0; i < g_hLNW; i++)
                    LOG("        under cursor: world %08X m=%X  (%s,%s)..(%s,%s)\r\n", g_hLW[i].caller, g_hLW[i].mat,
                        ftoa(g_hLW[i].x0, f[0]), ftoa(g_hLW[i].y0, f[1]), ftoa(g_hLW[i].x1, f[2]), ftoa(g_hLW[i].y1, f[3]));
                if (g_mode == 0 && cx > -1e8f) ui_log_aim(g_hmHave ? (float)g_hmRX : cx, cx, cy);
                if (g_clkPending) ui_click_result();           /* a new click settles the previous one */
                g_clkPending = 1; g_clkBtn = b; g_clkFrame = g_sigFrame; g_clkSig = g_curSig; g_clkMode = g_mode;
                flush();
            }
            g_btn[b] = down;
        }
    }
    if (g_clkPending && g_sigFrame - g_clkFrame >= CLICK_SETTLE) ui_click_result();
}

/* at EndFrame: a conversation frame (letterbox drawn) without the portrait gets the last one
   again, for up to CONV_HOLD_FRAMES frames; the log names every change of the two */
static void ui_conv_end(void) {
    int st = (g_fConv ? 1 : 0) | (g_fPort ? 2 : 0), hold = 0;
    if (g_cfgPortraitHold && g_fConv && !g_fPort && !g_fMenu && g_portHave && g_uiOn && g_cfgLayout && g_sigFrame - g_portFrame <= CONV_HOLD_FRAMES) {
        ui_draw_quad(g_portBucket, g_portMat, g_portA3, g_portV);
        g_convHeldRun++; hold = 1;
    }
    if (st == 1) g_convMissRun++;
    if (g_cfgLog && st != g_convState && (st || g_convState > 0)) {
        if (g_convState == 1) LOG("conversation: the portrait was missing for %u frame(s) with the letterbox on screen%s\r\n", g_convMissRun,
                                  g_convHeldRun ? " (last portrait drawn instead in those frames)" : g_portHave || !g_cfgPortraitHold ? "" : " (no portrait yet: start of the conversation)");
        LOG("conversation: frame %u: letterbox %s, portrait %s\r\n", g_sigFrame, g_fConv ? "on" : "off", g_fPort ? "drawn" : "not drawn");
    }
    if (st != 1) g_convMissRun = g_convHeldRun = 0;
    (void)hold;
    if (!g_fConv && !g_fPort) g_portHave = 0;   /* conversation over: forget it */
    g_convState = st;
}
static void ui_frame_start(void) {
    if (g_cfgLog) ui_events();
    if ((g_cfgLog || (g_cfgHudMouse && g_uiOn)) && !g_diDev && (g_sigFrame % 30) == 0) di_try_hook();
    {   /* this frame's tooltips become the fallback for the next frame */
        int t, k;
        for (t = 0; t < g_nftip; t++) for (k = 0; k < TIPF; k++) g_ptip[t][k] = g_ftip[t][k];
        g_nptip = g_nftip; g_nftip = 0; g_ntip = 0;
    }
    g_barsDone = 0;
    g_sigFrame++;
    g_nsig = 0;
    if (g_mode == 1) g_dMenuFrames++; else if (g_mode == 0) g_dGameFrames++;
    g_pMenu = g_fMenu; g_pCol = g_fCol; g_pBar = g_fBar; g_pConv = g_fConv;   /* evidence, not decisions */
    g_fMenu = g_fWorld = g_fCol = g_fBar = g_fConv = g_fPort = 0;
    if (g_mode >= 0) g_prevMode = g_mode;
    g_mode = -1;
    g_nui = 0;
}

