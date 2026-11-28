/* Hover and text logging for the debug log (Log=1). Included by ui_layout.inc.
 *
 * Every frame it notes, from the vertices the game hands to the renderer
 * (before any layout change):
 *   - the cursor sprite (the game's own cursor position),
 *   - tooltip / label boxes (0x00456ABD) and the text glyphs inside them,
 *   - which 3D world draw calls lie under the cursor,
 *   - for each text batch, its characters, decoded from the glyphs' texture
 *     rectangles (text_decode.inc).
 * When the hover state changes (a box appears, changes size or text, or
 * disappears) one "event: hover" line is written with everything needed to
 * size boxes and text: cursor, box, offset from the cursor, glyph count,
 * lines, glyph height, margins, the words, and what is under the cursor.
 */

#define H_MAXTIP  8
#define H_MAXTXT  256
#define H_GLY     12
typedef struct {
    float x0, y0, x1, y1;           /* union of glyphs */
    int   ng; float g[H_GLY][4];    /* glyph boxes */
    unsigned short ch[H_GLY];       /* decoded characters (0 = not decoded) */
    unsigned char  fo[H_GLY];       /* their fonts (td_file index, 255 = unknown) */
    DWORD mat;
} HTxt;
static HTxt  g_hTxt[H_MAXTXT]; static int g_hNTxt;
static float g_hTip[H_MAXTIP][4]; static int g_hNTip;
static float g_hCurX, g_hCurY; static int g_hCurSeen;      /* this frame, game coordinates */
static float g_hPCurX = -1e9f, g_hPCurY = -1e9f;            /* previous frame */
typedef struct { DWORD caller, mat; float x0, y0, x1, y1, area; } HWorld;
static HWorld g_hW[3]; static int g_hNW;
static DWORD g_hPrevKey; static int g_hActive;
static DWORD g_hHoverCount;
/* kept after each frame for click events: the label shown and what was under the cursor */
static char   g_hLStr[64]; static float g_hLBox[4]; static int g_hLHave;
static HWorld g_hLW[3]; static int g_hLNW;
static const char* h_region(int menu, float x, float y) {
    float cxc = x * 640.0f / (float)*EXE_SCREEN_W, cyc = y * 480.0f / (float)*EXE_SCREEN_H;
    return menu ? "menu" : (cxc >= 536.67f && cyc < 420) ? "hud-column" : cyc >= 405 ? "hud-bar" : "world";
}

/* text catalogue: each distinct line of text (string + font) is logged once */
#define H_MAXCAT 1200
static DWORD g_hCat[H_MAXCAT]; static int g_hNCat;

static DWORD h_hash_str(const char* s, DWORD h) { while (*s) h = (h ^ (unsigned char)*s++) * 16777619u; return h; }

static void hover_world(const Pending* p) {
    const float* v = p->v;
    float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f, area;
    int k, i;
    if (g_hPCurX < -1e8f) return;
    for (k = 0; k < p->n; k++) {
        float x = v[k*8], y = v[k*8+1];
        if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
    }
    if (g_hPCurX < x0 || g_hPCurX > x1 || g_hPCurY < y0 || g_hPCurY > y1) return;
    area = (x1 - x0) * (y1 - y0);
    if (area > 2e6f) return;                       /* terrain-sized batches say nothing */
    /* keep the three smallest draws under the cursor */
    for (i = 0; i < g_hNW && g_hW[i].area <= area; i++) ;
    if (i >= 3) return;
    if (g_hNW < 3) g_hNW++;
    for (k = g_hNW - 1; k > i; k--) g_hW[k] = g_hW[k-1];
    g_hW[i].caller = p->caller; g_hW[i].mat = p->a1;
    g_hW[i].x0 = x0; g_hW[i].y0 = y0; g_hW[i].x1 = x1; g_hW[i].y1 = y1; g_hW[i].area = area;
}

static void hover_ui(const Pending* p) {
    const float* v = p->v;
    int q, k;
    if (p->n % 6) return;
    for (q = 0; q < p->n; q += 6) {
        float ax = 1e30f, bx = -1e30f, ay = 1e30f, by = -1e30f, w, h;
        for (k = q; k < q + 6; k++) {
            float x = v[k*8], y = v[k*8+1];
            if (x < ax) ax = x; if (x > bx) bx = x; if (y < ay) ay = y; if (y > by) by = y;
        }
        w = bx - ax; h = by - ay;
        if (p->caller == C_SPRITE && p->n == 6 && w > 31.5f && w < 32.5f && h > 31.5f && h < 32.5f) {
            g_hCurX = ax; g_hCurY = ay; g_hCurSeen = 1;
        } else if (p->caller == C_HOVER && ax >= 0 && g_hNTip < H_MAXTIP) {     /* not the off-screen helper quad */
            g_hTip[g_hNTip][0] = ax; g_hTip[g_hNTip][1] = ay; g_hTip[g_hNTip][2] = bx; g_hTip[g_hNTip][3] = by; g_hNTip++;
        }
    }
    if (p->caller == C_TEXT && g_hNTxt < H_MAXTXT) {
        HTxt* t = &g_hTxt[g_hNTxt++];
        t->x0 = t->y0 = 1e30f; t->x1 = t->y1 = -1e30f; t->ng = 0; t->mat = p->a1;
        for (q = 0; q < p->n; q += 6) {
            float ax = 1e30f, bx = -1e30f, ay = 1e30f, by = -1e30f;
            for (k = q; k < q + 6; k++) {
                float x = v[k*8], y = v[k*8+1];
                if (x < ax) ax = x; if (x > bx) bx = x; if (y < ay) ay = y; if (y > by) by = y;
            }
            if (t->ng < H_GLY) { t->g[t->ng][0] = ax; t->g[t->ng][1] = ay; t->g[t->ng][2] = bx; t->g[t->ng][3] = by; t->ng++; }
            if (ax < t->x0) t->x0 = ax; if (bx > t->x1) t->x1 = bx; if (ay < t->y0) t->y0 = ay; if (by > t->y1) t->y1 = by;
        }
        for (q = 0; q < t->ng; q++) { t->ch[q] = 0; t->fo[q] = 255; }
        if (p->td >= 0 && g_tdRing[p->td].n == p->n / 6)
            for (q = 0; q < t->ng; q++) { t->ch[q] = g_tdRing[p->td].ch[q]; t->fo[q] = g_tdRing[p->td].font[q]; }
    }
}

/* Lines of text from this frame's decoded glyphs. Glyphs of one line come
   in several batches (one per font texture: capitals and small letters often
   differ), so they are joined by position: a glyph continues a line when its
   centre is level with the line's first glyph (within 0.45 of the height)
   and it starts at most 2.5 heights after the line's last glyph. (Not by
   font: several fonts share identical glyph shapes, so a glyph's font is
   only a guess.) A gap of more than 0.35 of the
   height (at least 3 px) is a space. Only glyphs inside `clip` count if it is
   given. Returns the number of lines; the text of line i is g_hLine[i]. */
#define H_MAXLINE 160
#define H_LINELEN 96
typedef struct { float x0, y0, x1, y1, last, cy, h; int n; unsigned char font; char s[H_LINELEN]; } HLine;
static HLine g_hLine[H_MAXLINE];
typedef struct { float x0, y0, x1, y1; unsigned short ch; unsigned char fo; } HG;
static HG g_hG[H_MAXTXT * H_GLY];
static int h_lines(const float* clip) {
    int ng = 0, nl = 0, i, j, gap;
    for (i = 0; i < g_hNTxt; i++) {
        HTxt* t = &g_hTxt[i]; int k;
        for (k = 0; k < t->ng; k++) {
            float cx = (t->g[k][0] + t->g[k][2]) * 0.5f, cy = (t->g[k][1] + t->g[k][3]) * 0.5f;
            if (!t->ch[k]) continue;
            if (clip && (cx < clip[0] || cx > clip[2] || cy < clip[1] || cy > clip[3])) continue;
            g_hG[ng].x0 = t->g[k][0]; g_hG[ng].y0 = t->g[k][1]; g_hG[ng].x1 = t->g[k][2]; g_hG[ng].y1 = t->g[k][3];
            g_hG[ng].ch = t->ch[k]; g_hG[ng].fo = t->fo[k]; ng++;
        }
    }
    for (gap = ng / 2; gap > 0; gap /= 2)                  /* by x (shell sort) */
        for (i = gap; i < ng; i++) {
            HG tmp = g_hG[i];
            for (j = i; j >= gap && g_hG[j - gap].x0 > tmp.x0; j -= gap) g_hG[j] = g_hG[j - gap];
            g_hG[j] = tmp;
        }
    for (i = 0; i < ng; i++) {
        HG* g = &g_hG[i];
        float cy = (g->y0 + g->y1) * 0.5f, h = g->y1 - g->y0;
        int best = -1;
        for (j = 0; j < nl; j++) {               /* the line's height and centre are those of its first glyph, */
            HLine* L = &g_hLine[j];              /* so a label just above or below does not get pulled in      */
            float lh = L->h > h ? L->h : h, d = cy - L->cy;
            if (d < 0) d = -d;
            if (d <= 0.45f * lh && g->x0 >= L->last - 1.0f && g->x0 - L->last <= 2.5f * lh) { best = j; break; }
        }
        if (best < 0) {
            if (nl >= H_MAXLINE) continue;
            best = nl++;
            g_hLine[best].x0 = g->x0; g_hLine[best].y0 = g->y0; g_hLine[best].x1 = g->x1; g_hLine[best].y1 = g->y1;
            g_hLine[best].last = g->x0; g_hLine[best].n = 0; g_hLine[best].font = g->fo; g_hLine[best].s[0] = 0;
            g_hLine[best].cy = cy; g_hLine[best].h = h;
        }
        {
            HLine* L = &g_hLine[best];
            float sp = 0.35f * (L->h > h ? L->h : h);
            if (sp < 3.0f) sp = 3.0f;
            if (L->n && g->x0 - L->last > sp && L->n < H_LINELEN - 2) L->s[L->n++] = ' ';
            if (L->n < H_LINELEN - 1) { char c = td_ascii(g->ch); L->s[L->n++] = c == '"' ? '\'' : c; }
            L->s[L->n] = 0;
            if (g->x1 > L->last) L->last = g->x1;
            if (g->x1 > L->x1) L->x1 = g->x1;
            if (g->y0 < L->y0) L->y0 = g->y0; if (g->y1 > L->y1) L->y1 = g->y1;
        }
    }
    return nl;
}

/* text catalogue: log each line not seen before (Log=1). Run only when this
   frame's text differs from the last frame's (cheap signature). */
static DWORD g_hTextSig;
static void h_catalogue(void) {
    DWORD sig = 2166136261u; int i, k, nl;
    for (i = 0; i < g_hNTxt; i++)
        for (k = 0; k < g_hTxt[i].ng; k++)
            sig = (sig ^ (g_hTxt[i].ch[k] + ((DWORD)g_hTxt[i].g[k][0] << 16) + (DWORD)g_hTxt[i].g[k][1])) * 16777619u;
    if (sig == g_hTextSig) return;
    g_hTextSig = sig;
    nl = h_lines(0);
    for (i = 0; i < nl && g_hNCat < H_MAXCAT; i++) {
        HLine* L = &g_hLine[i];
        DWORD key = h_hash_str(L->s, L->font * 2654435761u);
        int seen = 0;
        for (k = 0; k < g_hNCat; k++) if (g_hCat[k] == key) { seen = 1; break; }
        if (!seen) {
            char f[4][32];
            g_hCat[g_hNCat++] = key;
            LOG("text: \"%s\" font %s at (%s,%s) size %s x %s\r\n", L->s, L->font < TD_NFONT ? td_file[L->font] : "?",
                ftoa(L->x0, f[0]), ftoa(L->y0, f[1]), ftoa(L->x1 - L->x0, f[2]), ftoa(L->y1 - L->y0, f[3]));
        }
    }
}

/* called once per frame (from ui_events) with the data of the frame that ended */
static void hover_frame_end(int menu) {
    char f[16][32];
    int i, best = -1;
    float bd = 1e30f;
    if (g_hCurSeen) { g_hPCurX = g_hCurX; g_hPCurY = g_hCurY; }
    if (!g_hCurSeen) goto reset;
    for (i = 0; i < g_hNTip; i++) {           /* the box nearest the cursor */
        float dx = g_hTip[i][0] - g_hCurX, dy = g_hTip[i][1] - g_hCurY, d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = i; }
    }
    if (best >= 0) {
        float* b = g_hTip[best];
        float gx0 = 1e30f, gy0 = 1e30f, gx1 = -1e30f, gy1 = -1e30f, gh = 0;
        float tops[16]; int nt = 0, ng = 0, k, j;
        const char* s1 = ""; const char* s2 = "";
        static char lab[2][H_LINELEN];
        DWORD key;
        {   int nl = h_lines(b);                  /* the words inside the box, line by line */
            lab[0][0] = lab[1][0] = 0;
            if (nl > 0) { for (k = 0; k < H_LINELEN - 1 && g_hLine[0].s[k]; k++) lab[0][k] = g_hLine[0].s[k]; lab[0][k] = 0; s1 = lab[0]; }
            if (nl > 1) { for (k = 0; k < H_LINELEN - 1 && g_hLine[1].s[k]; k++) lab[1][k] = g_hLine[1].s[k]; lab[1][k] = 0; s2 = lab[1]; }
        }
        for (i = 0; i < g_hNTxt; i++) {
            HTxt* t = &g_hTxt[i]; int inside = 0;
            for (k = 0; k < t->ng; k++) {
                float cx = (t->g[k][0] + t->g[k][2]) * 0.5f, cy = (t->g[k][1] + t->g[k][3]) * 0.5f;
                if (cx < b[0] || cx > b[2] || cy < b[1] || cy > b[3]) continue;
                inside = 1; ng++;
                if (t->g[k][0] < gx0) gx0 = t->g[k][0]; if (t->g[k][2] > gx1) gx1 = t->g[k][2];
                if (t->g[k][1] < gy0) gy0 = t->g[k][1]; if (t->g[k][3] > gy1) gy1 = t->g[k][3];
                if (t->g[k][3] - t->g[k][1] > gh) gh = t->g[k][3] - t->g[k][1];
                for (j = 0; j < nt; j++) if (t->g[k][3] > tops[j] - 4 && t->g[k][3] < tops[j] + 4) break;
                if (j == nt && nt < 16) tops[nt++] = t->g[k][3];        /* lines by glyph bottom */
            }
            (void)inside;
        }
        key = h_hash_str(s1, h_hash_str(s2, (DWORD)(b[2] - b[0]) * 7919u + (DWORD)(b[3] - b[1]) * 104729u + (DWORD)ng));
        if (!g_hActive || key != g_hPrevKey) {
            float cxc = g_hCurX * 640.0f / (float)*EXE_SCREEN_W, cyc = g_hCurY * 480.0f / (float)*EXE_SCREEN_H;
            const char* region = menu ? "menu" : (cxc >= 536.67f && cyc < 420) ? "hud-column" : cyc >= 405 ? "hud-bar" : "world";
            g_hHoverCount++;
            LOG("event: hover \"%s\"%s%s%s  cursor (%s,%s) [canvas %s,%s] %s frame %u\r\n",
                s1, s2[0] ? " + \"" : "", s2, s2[0] ? "\"" : "",
                ftoa(g_hCurX, f[0]), ftoa(g_hCurY, f[1]), ftoa(cxc, f[2]), ftoa(cyc, f[3]), region, g_sigFrame);
            LOG("        box (%s,%s) %s x %s, offset from cursor (%s,%s); %u boxes on screen\r\n",
                ftoa(b[0], f[0]), ftoa(b[1], f[1]), ftoa(b[2] - b[0], f[2]), ftoa(b[3] - b[1], f[3]),
                ftoa(b[0] - g_hCurX, f[4]), ftoa(b[1] - g_hCurY, f[5]), g_hNTip);
            if (ng)
                LOG("        text %d glyphs, %d line(s), %s x %s, glyph height <= %s, margins L %s T %s R %s B %s\r\n",
                    ng, nt, ftoa(gx1 - gx0, f[0]), ftoa(gy1 - gy0, f[1]), ftoa(gh, f[2]),
                    ftoa(gx0 - b[0], f[3]), ftoa(gy0 - b[1], f[4]), ftoa(b[2] - gx1, f[5]), ftoa(b[3] - gy1, f[6]));
            else
                LOG("        no text inside the box\r\n");
            for (i = 0; i < g_hNW; i++)
                LOG("        under cursor: world %08X m=%X  (%s,%s)..(%s,%s)\r\n", g_hW[i].caller, g_hW[i].mat,
                    ftoa(g_hW[i].x0, f[0]), ftoa(g_hW[i].y0, f[1]), ftoa(g_hW[i].x1, f[2]), ftoa(g_hW[i].y1, f[3]));
            flush();
            g_hPrevKey = key; g_hActive = 1;
            for (i = 0; i < 63 && s1[i]; i++) g_hLStr[i] = s1[i]; g_hLStr[i] = 0;
            for (i = 0; i < 4; i++) g_hLBox[i] = b[i];
            g_hLHave = 1;
        }
    } else if (g_hActive) {
        char g[2][32];
        LOG("event: hover end  cursor (%s,%s) frame %u\r\n", ftoa(g_hCurX, g[0]), ftoa(g_hCurY, g[1]), g_sigFrame);
        g_hActive = 0;
    }
    if (!g_hActive) g_hLHave = 0;
reset:
    h_catalogue();
    for (i = 0; i < g_hNW; i++) g_hLW[i] = g_hW[i];
    g_hLNW = g_hNW;
    g_hNTxt = 0; g_hNTip = 0; g_hCurSeen = 0; g_hNW = 0;
}
