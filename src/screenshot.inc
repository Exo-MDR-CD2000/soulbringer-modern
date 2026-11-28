/* Debug screenshots (Log=1): [Debug] ScreenshotKey, default F11.
 *
 * One press = one image of the game window, saved as a 24-bit BMP in
 * <game folder>\Screenshots\SBM_<pid>_<n>.bmp, plus a frame capture of the
 * same frame in the session log ("FRAME CAPTURE <marker>"). The log line
 *   event: screenshot <n> (capture <marker>) saved ...
 * ties the two together, so the picture can be matched to the coordinates
 * of everything drawn in it.
 *
 * Sequence: key -> request a frame capture -> the captured frame ends
 * (EndScene) -> at the next StartScene, that frame is on screen -> grab it.
 * If no capture finishes within 120 frames, the grab happens anyway.
 *
 * GDI and the extra user32/kernel32 functions are loaded at the first
 * screenshot with GetProcAddress, so the DLL's import table (and the
 * release build) is unchanged. The grab is tried from the screen (what is
 * actually shown) first, then from the window; an all-black result is
 * reported in the log (fullscreen modes can hide the picture from GDI).
 */
typedef void* HDC_; typedef void* HWND_; typedef void* HGDI_;
typedef struct { int l, t, r, b; } RECT_;
typedef struct { DWORD size; int w, h; unsigned short planes, bits; DWORD comp, img, xppm, yppm, used, imp; } BMIH_;

static HWND_ (WINAPI *p_GetForegroundWindow)(void);
static DWORD (WINAPI *p_GetWindowThreadProcessId)(HWND_, DWORD*);
static BOOL  (WINAPI *p_GetClientRect)(HWND_, RECT_*);
static BOOL  (WINAPI *p_ClientToScreen)(HWND_, POINT_*);
static HDC_  (WINAPI *p_GetDC)(HWND_);
static int   (WINAPI *p_ReleaseDC)(HWND_, HDC_);
static HDC_  (WINAPI *p_CreateCompatibleDC)(HDC_);
static HGDI_ (WINAPI *p_CreateDIBSection)(HDC_, const void*, DWORD, void**, HANDLE, DWORD);
static HGDI_ (WINAPI *p_SelectObject)(HDC_, HGDI_);
static BOOL  (WINAPI *p_BitBlt)(HDC_, int, int, int, int, HDC_, int, int, DWORD);
static BOOL  (WINAPI *p_DeleteObject)(HGDI_);
static BOOL  (WINAPI *p_DeleteDC)(HDC_);
static BOOL  (WINAPI *p_GdiFlush)(void);
static BOOL  (WINAPI *p_CreateDirectoryA)(const char*, void*);

/* g_dir (game folder) and g_shotKey are declared in soulbringer_modern.c (read in init) */
static int   g_shotDown, g_shotNum, g_shotState, g_shotWait;   /* state: 0 idle, 1 wait for capture, 2 grab now */
static int   g_shotApi;           /* 0 not loaded, 1 ok, -1 failed */
static int   g_shotMarker;        /* frame capture that goes with the pending screenshot */

#define SHOT_GET(m, n) (*(FARPROC*)&p_##n = GetProcAddress(m, #n))

static int shot_load(void) {
    HMODULE u, g, k;
    if (g_shotApi) return g_shotApi > 0;
    u = LoadLibraryA("user32.dll"); g = LoadLibraryA("gdi32.dll"); k = LoadLibraryA("kernel32.dll");
    g_shotApi = -1;
    if (!u || !g || !k) return 0;
    if (!SHOT_GET(u, GetForegroundWindow) || !SHOT_GET(u, GetWindowThreadProcessId) || !SHOT_GET(u, GetClientRect) ||
        !SHOT_GET(u, ClientToScreen) || !SHOT_GET(u, GetDC) || !SHOT_GET(u, ReleaseDC) ||
        !SHOT_GET(g, CreateCompatibleDC) || !SHOT_GET(g, CreateDIBSection) || !SHOT_GET(g, SelectObject) ||
        !SHOT_GET(g, BitBlt) || !SHOT_GET(g, DeleteObject) || !SHOT_GET(g, DeleteDC) || !SHOT_GET(g, GdiFlush) ||
        !SHOT_GET(k, CreateDirectoryA)) return 0;
    g_shotApi = 1;
    return 1;
}


/* copy one rectangle from src into a new 24-bit bottom-up DIB; returns the pixels; the caller frees bmp, mem */
static unsigned char* shot_blt(HDC_ src, int sx, int sy, int w, int h, HDC_* mem, HGDI_* bmp, HGDI_* old) {
    BMIH_ bi; void* bits = 0;
    memset(&bi, 0, sizeof bi);
    bi.size = sizeof bi; bi.w = w; bi.h = h; bi.planes = 1; bi.bits = 24;
    *mem = p_CreateCompatibleDC(src);
    if (!*mem) return 0;
    *bmp = p_CreateDIBSection(*mem, &bi, 0 /*DIB_RGB_COLORS*/, &bits, 0, 0);
    if (!*bmp || !bits) { p_DeleteDC(*mem); return 0; }
    *old = p_SelectObject(*mem, *bmp);
    if (!p_BitBlt(*mem, 0, 0, w, h, src, sx, sy, 0x00CC0020 /*SRCCOPY*/ | 0x40000000 /*CAPTUREBLT*/)) {
        p_SelectObject(*mem, *old); p_DeleteObject(*bmp); p_DeleteDC(*mem); return 0;
    }
    p_GdiFlush();
    return (unsigned char*)bits;
}

static int shot_black(const unsigned char* px, int stride, int w, int h) {
    int i, j;
    for (j = 0; j < 32; j++) for (i = 0; i < 32; i++) {
        const unsigned char* p = px + (h * j / 32) * stride + (w * i / 32) * 3;
        if (p[0] > 8 || p[1] > 8 || p[2] > 8) return 0;
    }
    return 1;
}

__attribute__((noinline)) static void shot_take(void) {
    char path[700], f[2][32];
    HWND_ wnd; DWORD pid = 0; RECT_ rc; POINT_ o; int w, h, stride, black = 0;
    HDC_ src = 0, mem = 0; HGDI_ bmp = 0, old = 0; HWND_ srcWnd = 0;
    unsigned char* px = 0; const char* how = "screen";
    (void)f;
    g_shotNum++;
    if (!shot_load()) { LOG("event: screenshot %d failed: could not load GDI functions\r\n", g_shotNum); flush(); return; }
    wnd = p_GetForegroundWindow();
    if (wnd) p_GetWindowThreadProcessId(wnd, &pid);
    if (!wnd || pid != GetCurrentProcessId()) { LOG("event: screenshot %d skipped: the game window is not in front\r\n", g_shotNum); flush(); return; }
    if (!p_GetClientRect(wnd, &rc)) rc.l = rc.t = rc.r = rc.b = 0;
    w = rc.r - rc.l; h = rc.b - rc.t;
    if (w <= 0 || h <= 0) { w = *EXE_SCREEN_W; h = *EXE_SCREEN_H; }
    o.x = o.y = 0; p_ClientToScreen(wnd, &o);
    stride = (w * 3 + 3) & ~3;

    /* 1: from the screen, at the window's client area */
    src = p_GetDC(0);
    if (src) {
        px = shot_blt(src, o.x, o.y, w, h, &mem, &bmp, &old);
        p_ReleaseDC(0, src);
        if (px && shot_black(px, stride, w, h)) { p_SelectObject(mem, old); p_DeleteObject(bmp); p_DeleteDC(mem); px = 0; black = 1; }
    }
    /* 2: from the window itself */
    if (!px) {
        src = p_GetDC(wnd); srcWnd = wnd; how = "window";
        if (src) {
            px = shot_blt(src, 0, 0, w, h, &mem, &bmp, &old);
            p_ReleaseDC(srcWnd, src);
            black = px ? shot_black(px, stride, w, h) : black;
        }
    }
    if (!px) { LOG("event: screenshot %d failed: BitBlt from screen and window failed\r\n", g_shotNum); flush(); return; }

    wsprintfA(path, "%sScreenshots", g_dir);
    p_CreateDirectoryA(path, 0);                       /* fails harmlessly if it exists */
    wsprintfA(path, "%sScreenshots\\SBM_%u_%03d.bmp", g_dir, GetCurrentProcessId(), g_shotNum);
    {
        HANDLE fh = CreateFileA(path, 0x40000000 /*GENERIC_WRITE*/, 1, 0, 2 /*CREATE_ALWAYS*/, 0x80, 0);
        DWORD wr = 0, img = (DWORD)stride * (DWORD)h; int ok = 0;
        if (fh != (HANDLE)-1) {
            unsigned char hdr[54]; BMIH_ bi; DWORD fsize = 54 + img;
            memset(hdr, 0, sizeof hdr);
            hdr[0] = 'B'; hdr[1] = 'M';
            hdr[2] = (unsigned char)fsize; hdr[3] = (unsigned char)(fsize >> 8); hdr[4] = (unsigned char)(fsize >> 16); hdr[5] = (unsigned char)(fsize >> 24);
            hdr[10] = 54;
            memset(&bi, 0, sizeof bi);
            bi.size = sizeof bi; bi.w = w; bi.h = h; bi.planes = 1; bi.bits = 24; bi.img = img;
            memcpy(hdr + 14, &bi, sizeof bi);
            ok = WriteFile(fh, hdr, 54, &wr, 0) && WriteFile(fh, px, img, &wr, 0) && wr == img;
            CloseHandle(fh);
        }
        if (ok) LOG("event: screenshot %d (capture %d) saved %s  %dx%d from %s%s, game frame %u\r\n", g_shotNum, g_shotMarker, path, w, h, how,
                    black ? "  WARNING: image is black (the fullscreen mode hides it from GDI)" : "", g_sigFrame);
        else LOG("event: screenshot %d failed: could not write %s\r\n", g_shotNum, path);
    }
    p_SelectObject(mem, old); p_DeleteObject(bmp); p_DeleteDC(mem);
    flush();
}

/* poll the key at frame boundaries (Log=1 only) */
static void shot_poll(void) {
    int down;
    if (!g_shotKey) return;
    down = (GetAsyncKeyState(g_shotKey) & 0x8000) != 0;
    if (down && !g_shotDown && !g_shotState) {
        g_marker++;
        LOG("\r\n######## MARKER %d (screenshot %d) ########\r\n", g_marker, g_shotNum + 1);
        flush();
        g_capReq = 1; g_shotState = 1; g_shotWait = 0; g_shotMarker = g_marker;
    }
    g_shotDown = down;
}

/* called at every StartScene, before a new capture may start */
static void shot_frame(void) {
    if (g_shotState == 1 && ++g_shotWait > 120) g_shotState = 2;     /* capture never came: shoot anyway */
    if (g_shotState == 2) { g_shotState = 0; shot_take(); }
}
/* called when a frame capture has been written */
static void shot_capture_done(void) { if (g_shotState == 1) { g_shotState = 2; g_shotMarker = g_marker; } }
