/* The game's DirectInput mouse: HUD mouse mapping (ui_hud_mouse, always on
 * with Layout=1 and HudMouse=1) and the buttons for the debug log.
 *
 * In gameplay the game reads the mouse through DirectInput (exclusive mode),
 * so GetAsyncKeyState never sees the buttons there. The game keeps its two
 * DirectInput device wrappers at [0x367361C] and [0x3673620]; the device is at
 * wrapper+8. The one whose GetCapabilities says "mouse" (DIDEVTYPE_MOUSE = 2;
 * the keyboard is 3) is the one we watch.
 *
 * DirectInput checks every object's vtable pointer on each call, so the
 * object's vtable pointer must not change (giving it a copied vtable made
 * every call fail and crashed the game, 0.2.0-dev of 2026-11-21 15:07).
 * Instead the two entries GetDeviceState (+0x24) and GetDeviceData (+0x28)
 * are patched in DirectInput's own vtable. That vtable is shared with the
 * keyboard, so the wrappers pass every call straight through and only look
 * at the results when the device is the mouse. The entries are put back when
 * the DLL unloads.
 */
typedef long (__stdcall *DiGetStateFn)(void*, DWORD, void*);
typedef long (__stdcall *DiGetDataFn)(void*, DWORD, void*, DWORD*, DWORD);
typedef long (__stdcall *DiGetCapsFn)(void*, DWORD*);
#define DI_WRAP_A ((DWORD*)0x0367361C)
#define DI_WRAP_B ((DWORD*)0x03673620)
static void*  g_diDev;              /* the mouse device */
static void** g_diVt;               /* DirectInput's vtable we patched */
static DiGetStateFn g_diGetState;
static DiGetDataFn  g_diGetData;
static volatile DWORD g_diDown, g_diPress;   /* bit 0 = left, 1 = right, 2 = middle */
static int g_diTries;

static void ui_hud_mouse(int* st, int oldx, int oldy);   /* ui_layout.inc */
static int  ui_hud_mouse_active(void);

static long __stdcall di_get_state(void* d, DWORD cb, void* data) {
    /* the game reads the mouse straight into its cursor block: remember x, y before */
    int hm = d == g_diDev && data == (void*)EXE_CURSOR && cb == 16 && GetCurrentThreadId() == g_mainThread;
    int ox = hm ? EXE_CURSOR[0] : 0, oy = hm ? EXE_CURSOR[1] : 0;
    long r = g_diGetState(d, cb, data);
    if (hm && r >= 0) ui_hud_mouse((int*)data, ox, oy);
    if (d == g_diDev && r >= 0 && data && (cb == 16 || cb == 20) && g_cfgLog) {     /* DIMOUSESTATE(2): click log */
        const unsigned char* b = (const unsigned char*)data + 12;
        DWORD m = 0; int i;
        for (i = 0; i < 4; i++) if (b[i] & 0x80) m |= 1u << i;
        g_diPress |= m & ~g_diDown;
        g_diDown = m;
    }
    return r;
}
static long __stdcall di_get_data(void* d, DWORD cbo, void* rg, DWORD* n, DWORD flags) {
    long r = g_diGetData(d, cbo, rg, n, flags);
    if (d == g_diDev && r >= 0 && rg && n && !(flags & 1 /*DIGDD_PEEK*/) && cbo >= 8) {
        DWORD i;
        for (i = 0; i < *n; i++) {
            const DWORD* o = (const DWORD*)((const char*)rg + i * cbo);
            if (o[0] >= 12 && o[0] <= 15) {                     /* DIMOFS_BUTTON0..3 */
                DWORD bit = 1u << (o[0] - 12);
                if (o[1] & 0x80) { if (!(g_diDown & bit)) g_diPress |= bit; g_diDown |= bit; }
                else g_diDown &= ~bit;
            }
        }
    }
    return r;
}

static int di_write_vt(void** vt, void* e9, void* e10) {
    DWORD old;
    if (!VirtualProtect(&vt[9], 8, 0x04 /*PAGE_READWRITE*/, &old)) return 0;
    vt[9] = e9; vt[10] = e10;
    VirtualProtect(&vt[9], 8, old, &old);
    return 1;
}

/* called once in a while from the main thread until the mouse is hooked */
static void di_try_hook(void) {
    DWORD* wraps[2]; int i;
    wraps[0] = DI_WRAP_A; wraps[1] = DI_WRAP_B;
    if (g_diDev || g_diTries > 200) return;
    g_diTries++;
    for (i = 0; i < 2; i++) {
        DWORD w, caps[6];
        void* dev; void** vt;
        if (!readable(wraps[i], 4)) continue;
        w = *wraps[i];
        if (!readable((void*)w, 12)) continue;
        dev = *(void**)(w + 8);
        if (!readable(dev, 4)) continue;
        vt = *(void***)dev;
        if (!readable(vt, 11 * 4)) continue;
        caps[0] = 0x18; caps[1] = caps[2] = caps[3] = caps[4] = caps[5] = 0;   /* DIDEVCAPS_DX3 */
        if (((DiGetCapsFn)vt[3])(dev, caps) < 0) continue;
        LOG("event: DirectInput device %08X (wrapper at %08X): type %u, %u buttons\r\n",
            (DWORD)dev, (DWORD)wraps[i], caps[2] & 0xFF, caps[4]);
        if ((caps[2] & 0xFF) != 2 /*DIDEVTYPE_MOUSE*/ || caps[4] > 8) continue;
        if (vt[9] == (void*)di_get_state) { g_diDev = dev; g_diVt = vt; return; }   /* already ours */
        g_diGetState = (DiGetStateFn)vt[9]; g_diGetData = (DiGetDataFn)vt[10];
        g_diDev = dev;                                    /* set before the wrappers can run */
        if (!di_write_vt(vt, (void*)di_get_state, (void*)di_get_data)) { g_diDev = 0; LOG("event: mouse hook failed (VirtualProtect)\r\n"); return; }
        g_diVt = vt;
        LOG("event: mouse buttons hooked (DirectInput mouse device %08X, %u buttons)\r\n", (DWORD)dev, caps[4]);
        flush();
        return;
    }
}
static void di_unhook(void) {
    if (g_diVt && g_diVt[9] == (void*)di_get_state) di_write_vt(g_diVt, (void*)g_diGetState, (void*)g_diGetData);
    g_diVt = 0; g_diDev = 0;
}
