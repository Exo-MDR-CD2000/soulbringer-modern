/*
 * Soulbringer Modern - D3d7Lib.dll proxy
 *
 * Soulbringer (1999/2000) draws through a swappable renderer plug-in,
 * D3d7Lib.dll. This DLL takes its place: every one of the 76 exports is
 * forwarded unchanged to the original renderer, renamed D3d7Lib_orig.dll and
 * kept in the same folder. On load, it patches the game in memory:
 *
 *   - Hor+ widescreen: the world is projected with square pixels at any
 *     aspect ratio instead of being stretched (see docs/DEVLOG.md).
 *
 * Settings: SoulbringerModern.ini next to this DLL (optional).
 *   [Display] Widescreen=1   0 = original stretched projection
 *   [Debug]   Log=0          1 = write SoulbringerModern_<exe>.log with
 *                                per-call renderer statistics (slow, large)
 *
 * No game files are modified on disk. Only memory of the running game is
 * patched, and only after the original bytes have been verified.
 */

#define SBM_VERSION "0.2.0-dev"

typedef unsigned int   DWORD;
typedef int            BOOL;
typedef void*          HANDLE;
typedef void*          HMODULE;
typedef void*          FARPROC;

#define WINAPI __stdcall
__declspec(dllimport) HMODULE WINAPI LoadLibraryA(const char*);
__declspec(dllimport) FARPROC WINAPI GetProcAddress(HMODULE, const char*);
__declspec(dllimport) DWORD   WINAPI GetModuleFileNameA(HMODULE, char*, DWORD);
__declspec(dllimport) HANDLE  WINAPI CreateFileA(const char*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE);
__declspec(dllimport) BOOL    WINAPI WriteFile(HANDLE, const void*, DWORD, DWORD*, void*);
__declspec(dllimport) BOOL    WINAPI ReadFile(HANDLE, void*, DWORD, DWORD*, void*);
__declspec(dllimport) DWORD   WINAPI SetFilePointer(HANDLE, int, int*, DWORD);
__declspec(dllimport) BOOL    WINAPI FlushFileBuffers(HANDLE);
__declspec(dllimport) BOOL    WINAPI CloseHandle(HANDLE);
__declspec(dllimport) BOOL    WINAPI VirtualProtect(void*, DWORD, DWORD, DWORD*);
__declspec(dllimport) void*   WINAPI VirtualAlloc(void*, DWORD, DWORD, DWORD);
__declspec(dllimport) BOOL    WINAPI FlushInstructionCache(HANDLE, const void*, DWORD);
__declspec(dllimport) HANDLE  WINAPI GetCurrentProcess(void);
__declspec(dllimport) unsigned WINAPI GetPrivateProfileIntA(const char*, const char*, int, const char*);
__declspec(dllimport) DWORD   WINAPI GetPrivateProfileStringA(const char*, const char*, const char*, char*, DWORD, const char*);
__declspec(dllimport) DWORD   WINAPI GetTickCount(void);
__declspec(dllimport) DWORD   WINAPI GetCurrentThreadId(void);
__declspec(dllimport) BOOL    WINAPI IsBadReadPtr(const void*, DWORD);
__declspec(dllimport) void    WINAPI ExitProcess(DWORD);
__declspec(dllimport) short   WINAPI GetAsyncKeyState(int);
typedef struct { int x, y; } POINT_;
typedef struct { unsigned short y, mo, dow, d, h, mi, s, ms; } SYSTIME_;
__declspec(dllimport) BOOL    WINAPI GetCursorPos(POINT_*);
__declspec(dllimport) DWORD   WINAPI GetCurrentProcessId(void);
__declspec(dllimport) void    WINAPI GetLocalTime(SYSTIME_*);
__declspec(dllimport) int     WINAPI MessageBoxA(void*, const char*, const char*, DWORD);
__declspec(dllimport) int __cdecl wsprintfA(char*, const char*, ...);

int _fltused = 0;

void* memset(void* d, int c, unsigned n) { unsigned char* p = d; while (n--) *p++ = (unsigned char)c; return d; }
void* memcpy(void* d, const void* s, unsigned n) { unsigned char* p = d; const unsigned char* q = s; while (n--) *p++ = *q++; return d; }

#include "stubs.h"

/* ---------------------------------------------------------------- state */

static HMODULE g_self;
static HMODULE g_orig;
static void*   g_fn[NFUNC];
static HANDLE  g_log = (HANDLE)-1;
static int     g_inited;
static DWORD   g_mainThread;
static int     g_cfgLog;        /* [Debug] Log */
static int     g_cfgWide = 1;   /* [Display] Widescreen */
static int     g_cfgLayout = 1; /* [Interface] Layout */
static int     g_cfgCursor = 200; /* [Interface] CursorSize, percent */
static int     g_cfgHud = 1;    /* [Interface] Hud, see ui_layout.inc */
static float   g_cfgMaxAspect;  /* [Display] MaxAspect, width/height; 0 = no cap */
static int     g_uiOn;          /* UI layout active in this process */
static int     g_cfgAuto = 1;   /* [Debug] AutoCapture: capture each new screen automatically */
static int     g_cfgHudMouse = 1; /* [Debug] HudMouse (default 1): HUD clicks and tooltips follow the moved HUD */
static int     g_cfgPortraitHold = 1; /* [Debug] ConversationPortraitHold: redraw the NPC portrait in the frames the game skips (ui_conv_end; removable) */
static int     g_cfgText = 200;  /* the proxy's tooltip stretch, percent (only without the text hook: the Tooltips size; else 100) */
static int     g_cfgStock;      /* [Debug] Stock: every enhancement off (the game as shipped); Log and the screenshot key still work */
static int     g_cfgTextHook;   /* [Debug] TextHook (default 1): the game lays out its text at the [Text] sizes itself (text_scale.inc); 0 = troubleshooting only */
static int     g_cfgFontPlaces; /* [Debug] FontPlaces: each place of a shared font gets its own copy of the font (font_places.inc) */
static int     fp_config(const char* ini, int places); /* text sizes ([Text] ...): the kinds' sizes into g_cfgTextKind, and with places != 0 the items' own sizes (+ [PlaceTextScale]); returns the largest place size (0 = none) */
static int     g_cfgFontPages;  /* font slots 0x4E-0x51 created larger in A8R8G8B8 (font_hd.inc): on whenever [Fonts] redraws a font (was [Debug] FontPages) */
static int     g_cfgMenuFont;   /* [Fonts] Menu: menu font magc redrawn: 1 Soulbringer Uncial (Eagle Lake), 2 MedievalSharp, 3 a file of your own; 0 = the game's (needs TextHook) */
static char    g_cfgMenuFile[260], g_cfgMenuFace[64]; static int g_cfgMenuWeight = 400;   /* [Fonts] Menu=<file>: the file, the face and weight read from it */
static char    g_cfgNameFile[260], g_cfgNameFace[64]; static int g_cfgNameWeight = 400;   /* [Fonts] Names=<file> */
static int     g_cfgMenuWanted; /* the setting before the TextHook check (for the log) */
static int     g_cfgNameFont;   /* [Fonts] Names: name/message fonts mgrn/mred redrawn: 1 = the bundled Soulbringer Text, 2 = a file of your own; 0 = the game's (needs TextHook) */
static int     g_cfgNameWanted;
static int     g_cfgJournalFont; /* [Fonts] Journal: journal font phil: 1 Soulbringer Text (the same typeface, one pixel larger), 2 Cardo, 3 the game's */
static int     g_cfgTipFont;    /* [Fonts] Tooltips: tooltip font hbut: 1 Soulbringer Text (hbut has mgrn's letters), 2 Cardo, 3 the game's */
static int     g_cfgCredFont;   /* [Fonts] Credits: 1 = cred (credits, GAME OVER) from the bundled Soulbringer Credits, 0 = the game's */
static int     g_cfgNumFont;    /* [Fonts] Numbers: 1 = combat numbers numr/numg redrawn (Montserrat ExtraBold), 0 = the game's digits */
static int     g_cfgUiFont;     /* [Fonts] Interface: 1 = interface fonts 16xY/16xB/16xX redrawn (Montserrat ExtraBold), 0 = the game's pixel font */
static int     g_cfgFontPageScale; /* [Debug] FontPageScale: 0 = the largest text size /100 rounded up (always 4 with redrawn fonts) */
static int     g_fpgK;          /* font page scale while the larger font pages are on, 0 = off (font_hd.inc) */
static void    fpg_config(void);   /* font_hd.inc: the redraw choices from the [Fonts] settings */
static void    fpg_own_install(void);         /* font_hd.inc: own metrics for phil/hbut (step 5), from tx_install() */
static int     g_txPct = 100;   /* game-side text scale in percent while the text hook is in */
/* the size of each kind of text (from the [Text] settings, ts_kinds in font_places.inc), by the game font each kind is drawn with
   (font map from the decoded text of all sessions, DEVLOG 2026-11-23 "text kinds checked"):
     0 Journal   phil                journal titles and entries
     1 Tooltip   hbut                every cursor tooltip, place names
     2 Menu      magc                main menu, save/load list, quit dialog, conversations
     3 Interface 16xY/B/X, 5px*      options menu, character stats, inventory numbers, HUD column labels
                                     (5px* = the same texts below 1000 px screen width)
     4 Name      mgrn, mred          names over characters, pick-up / "Empty." / "Locked." messages
     5 Number    numg, numr          not seen on screen in any session yet
     6 Credits   cred                credits */
#define TEXT_KINDS 7
static int     g_cfgTextKind[TEXT_KINDS];
/* td_file[] order: 5PxB 5PxG 5PxR 5PxX 5PxY 16xB 16xX 16xY phil hbut magc mgrn mred cred numg numr */
static const unsigned char g_fontKind[16] = { 3, 3, 3, 3, 3, 3, 3, 3, 0, 1, 2, 4, 4, 6, 5, 5 };
static int     g_txPctFile[16];   /* scale per font file while the text hook is in */
static float   g_gts = 1.0f;    /* the same as a factor, for the tooltip rules in ui_layout.inc */
static int     g_shotKey = 0x7A; /* [Debug] ScreenshotKey (VK code, default F11; 0 = off), see screenshot.inc */
static char    g_dir[600];      /* folder of this DLL (the game folder), with trailing backslash */
/* "F11", "f5", "0"/"off"/"" -> VK code */
static int shot_parse_key(const char* s) {
    int n = 0;
    if ((s[0] | 0x20) != 'f') return 0;
    for (s++; *s >= '0' && *s <= '9'; s++) n = n * 10 + (*s - '0');
    return n >= 1 && n <= 24 ? 0x6F + n : 0;
}

/* exe globals found by static analysis of SoulbringeVCnoeax.exe */
#define EXE_CURSOR     ((volatile int*)0x3559114)          /* game cursor {x, y, z, buttons}: DirectInput reads into it */
#define EXE_RDRAG      ((volatile unsigned char*)0x6B840C) /* right-button camera drag holds the cursor in place */
#define EXE_SCREEN_W   ((volatile int*)0x367370C)
#define EXE_SCREEN_H   ((volatile int*)0x3673708)
#define EXE_RENDERER   ((volatile DWORD*)0x35767D4)

/* ---------------------------------------------------------------- log */

static char g_buf[16384];
static int  g_blen;

static void flush(void) {
    DWORD w;
    if (g_log != (HANDLE)-1 && g_blen) { WriteFile(g_log, g_buf, g_blen, &w, 0); FlushFileBuffers(g_log); }
    g_blen = 0;
}
static void put(const char* s) {
    while (*s) { if (g_blen >= (int)sizeof(g_buf) - 1) flush(); g_buf[g_blen++] = *s++; }
}
static char g_line[1024];
#define LOG(...) do { wsprintfA(g_line, __VA_ARGS__); put(g_line); } while (0)

/* float -> "%.2f" style text (wsprintf has no float support) */
static char* ftoa(float f, char* out) {
    char* p = out;
    if (f != f) { out[0]='n'; out[1]='a'; out[2]='n'; out[3]=0; return out; }
    if (f < 0) { *p++ = '-'; f = -f; }
    if (f > 2e9f) { *p++='b'; *p++='i'; *p++='g'; *p=0; return out; }
    unsigned ip = (unsigned)f;
    unsigned fr = (unsigned)((f - (float)ip) * 1000.0f + 0.5f);
    if (fr >= 1000) { ip++; fr -= 1000; }
    wsprintfA(p, "%u.%03u", ip, fr);
    return out;
}

static BOOL readable(const void* p, DWORD n) { return p && !IsBadReadPtr(p, n); }


/* ---------------------------------------------------------------- widescreen patch
 * SoulbringeVCnoeax.exe 0x4DC540 computes the projection scales:
 *   yscale = -0.8 * (4*H)/(3*W)   (already follows the screen shape)
 *   xscale =  0.8 * zoom          (fixed -> image stretched sideways on non-4:3)
 * At 0x4DC5CD the 12 bytes  "fld [0.8]; fmul [zoom]"  are replaced with
 * "call ws_xscale; nop x7". ws_xscale pushes 0.8*(4*H)/(3*W)*zoom, giving square
 * pixels and a wider horizontal view (Hor+) at any aspect ratio.
 */
#define WS_ADDR 0x4DC5CD
static const unsigned char ws_orig[12] = { 0xD9,0x05,0xA8,0x66,0x68,0x03, 0xD8,0x0D,0xFC,0x83,0x6B,0x00 };
float ws_k = 0.8f * 4.0f / 3.0f;
extern void ws_xscale(void);
__asm__(
    ".intel_syntax noprefix\n"
    ".text\n"
    ".globl _ws_xscale\n"
    "_ws_xscale:\n"
    "  fild DWORD PTR ds:0x3673708\n"     /* H */
    "  fmul DWORD PTR [_ws_k]\n"          /* * 0.8*4/3 */
    "  fidiv DWORD PTR ds:0x367370C\n"    /* / W */
    "  fmul DWORD PTR ds:0x6B83FC\n"      /* * zoom (as the original did) */
    "  ret\n"
    ".att_syntax prefix\n");

static int g_wsApplied;

static void restore_widescreen(void) {
    unsigned char* p = (unsigned char*)WS_ADDR;
    DWORD old, i;
    if (!g_wsApplied) return;
    if (!VirtualProtect(p, 12, 0x40, &old)) return;
    for (i = 0; i < 12; i++) p[i] = ws_orig[i];
    VirtualProtect(p, 12, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 12);
    g_wsApplied = 0;
}

static void apply_widescreen(void) {
    unsigned char* p = (unsigned char*)WS_ADDR;
    DWORD old, i;
    if (!g_cfgWide) { LOG("widescreen: disabled in SoulbringerModern.ini\r\n"); return; }
    if (!readable(p, 12)) { LOG("widescreen: target not readable - not this exe?\r\n"); return; }
    {
        /* A previous instance of this DLL may have patched the exe and been
           unloaded without restoring (the game loads the renderer twice).
           Our patch is "E8 rel32" followed by 7 NOPs: accept it and re-point. */
        int ours = p[0] == 0xE8;
        for (i = 5; i < 12; i++) if (p[i] != 0x90) ours = 0;
        if (!ours) {
            for (i = 0; i < 12; i++) if (p[i] != ws_orig[i]) {
                LOG("widescreen: original bytes did not match at %08X (byte %u = %02X) - patch NOT applied\r\n", WS_ADDR, i, p[i]);
                return;
            }
        } else {
            LOG("widescreen: found an earlier copy of our patch, re-pointing it\r\n");
        }
    }
    if (!VirtualProtect(p, 12, 0x40 /*PAGE_EXECUTE_READWRITE*/, &old)) { LOG("widescreen: VirtualProtect failed\r\n"); return; }
    {
        DWORD rel = (DWORD)ws_xscale - (WS_ADDR + 5);
        p[0] = 0xE8;
        p[1] = (unsigned char)rel; p[2] = (unsigned char)(rel >> 8); p[3] = (unsigned char)(rel >> 16); p[4] = (unsigned char)(rel >> 24);
        for (i = 5; i < 12; i++) p[i] = 0x90;
    }
    VirtualProtect(p, 12, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 12);
    g_wsApplied = 1;
    LOG("widescreen: Hor+ patch applied at %08X\r\n", WS_ADDR);
}

static void tx_install(void);
static void tx_uninstall(void);

/* ---------------------------------------------------------------- init */

static int lstrcmp_(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a != *b; }
/* ---- ini helpers ---- */
/* settings that could not be used, logged once the log is open (cfg_bad) */
static char g_cfgWarn[600]; static int g_cfgWarnLen;
static int  g_cfgOldIni;        /* the ini still has a [UI] section (0.2-dev format before 2026-11-28) */
static void cfg_bad(const char* sec, const char* key, const char* val) {
    if (g_cfgWarnLen < (int)sizeof g_cfgWarn - 120)
        g_cfgWarnLen += wsprintfA(g_cfgWarn + g_cfgWarnLen, " [%s] %s=%s;", sec, key, val);
}
/* the value as one lower-case word, without spaces, up to a ';' comment on the same line
   (GetPrivateProfileString keeps those) */
static void ini_word(const char* t, char* w, int cap) {
    int i, n = 0;
    for (i = 0; t[i] && t[i] != ';' && n < cap - 1; i++) if (t[i] != ' ' && t[i] != '\t') w[n++] = (char)(t[i] >= 'A' && t[i] <= 'Z' ? t[i] + 32 : t[i]);
    w[n] = 0;
}
static int ini_word_is(const char* t, const char* a, const char* b, const char* c) {
    char w[40];
    ini_word(t, w, sizeof w);
    return (a && !lstrcmp_(w, a)) || (b && !lstrcmp_(w, b)) || (c && !lstrcmp_(w, c));
}
/* a font choice ([Fonts] ...): names = "word:value word:value ..." (lower case). Empty = follow
   All (sharp: 1, else 0); Original / Off / 0 = 0; Sharp / On / 1 = 1; a listed word = its value;
   anything else is logged and treated as empty. */
static int ini_pick(const char* t, int sharp, const char* sec, const char* key, const char* names) {
    char w[40]; const char* p = names;
    ini_word(t, w, sizeof w);
    if (!w[0]) return sharp ? 1 : 0;
    if (!lstrcmp_(w, "original") || !lstrcmp_(w, "off") || !lstrcmp_(w, "0")) return 0;
    if (!lstrcmp_(w, "sharp") || !lstrcmp_(w, "on") || !lstrcmp_(w, "1")) return 1;
    while (*p) {
        int i = 0;
        while (p[i] && p[i] != ':' && w[i] == p[i]) i++;
        if (p[i] == ':' && !w[i]) return p[i + 1] - '0';
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
    }
    cfg_bad(sec, key, t);
    return sharp ? 1 : 0;
}
/* a value naming a font file: ends in .ttf or .otf (before any ';' comment) */
static int ini_is_font_file(const char* t) {
    int n = 0;
    while (t[n] && t[n] != ';') n++;
    while (n > 0 && (t[n - 1] == ' ' || t[n - 1] == '\t')) n--;
    return n > 4 && t[n - 4] == '.' && ((t[n - 3] | 0x20) == 't' || (t[n - 3] | 0x20) == 'o') && (t[n - 2] | 0x20) == 't' && (t[n - 1] | 0x20) == 'f';
}
#include "ttf_name.inc"
/* [Fonts] Menu / Names = <file>: keep the path (relative to the game folder, or absolute), and
   read the face name and weight GDI will know it by from the file. 0 if it cannot be read. */
static int cfg_font_file(const char* t, char* file, char* face, int* weight) {
    static unsigned char head[12 + 16 * 64], tab[65536];
    char path[700]; HANDLE h; DWORD got, hlen = 0; unsigned off, len; int i, n = 0, ok = 0;
    while (*t == ' ' || *t == '\t') t++;
    for (i = 0; t[i] && t[i] != ';' && n < 259; i++) file[n++] = t[i];
    while (n > 0 && (file[n - 1] == ' ' || file[n - 1] == '\t')) n--;
    file[n] = 0;
    if (file[0] == '\\' || (file[0] && file[1] == ':')) wsprintfA(path, "%s", file); else wsprintfA(path, "%s%s", g_dir, file);
    h = CreateFileA(path, 0x80000000 /*GENERIC_READ*/, 1 /*FILE_SHARE_READ*/, 0, 3 /*OPEN_EXISTING*/, 0x80, 0);
    if (h == (HANDLE)-1) { cfg_bad("Fonts", "file not found", file); return 0; }
    if (ReadFile(h, head, sizeof head, &hlen, 0) && ttf_table(head, hlen, "name", &off, &len)) {
        unsigned w = len < sizeof tab ? len : sizeof tab;
        if (SetFilePointer(h, (int)off, 0, 0) == off && ReadFile(h, tab, w, &got, 0) && ttf_family(tab, got, face, 64)) ok = 1;
        *weight = 400;
        if (ok && ttf_table(head, hlen, "OS/2", &off, &len) && SetFilePointer(h, (int)off, 0, 0) == off && ReadFile(h, tab, 8, &got, 0))
            *weight = ttf_weight(tab, got);
    }
    CloseHandle(h);
    if (!ok) cfg_bad("Fonts", "not a font file", file);
    return ok;
}
static void init(void) {
    char path[600], dir[600], exe[300], tmp[700];
    int i, n, slash = -1;
    g_inited = 1;
    g_mainThread = GetCurrentThreadId();

    n = (int)GetModuleFileNameA(g_self, path, sizeof(path));
    for (i = 0; i < n; i++) if (path[i] == '\\' || path[i] == '/') slash = i;
    for (i = 0; i <= slash; i++) dir[i] = path[i];
    dir[slash + 1] = 0;
    for (i = 0; i <= slash + 1 && i < (int)sizeof(g_dir) - 1; i++) g_dir[i] = dir[i];

    n = (int)GetModuleFileNameA(0, exe, sizeof(exe));
    {
        int s = -1, e = n;
        for (i = 0; i < n; i++) { if (exe[i] == '\\' || exe[i] == '/') s = i; if (exe[i] == '.') e = i; }
        for (i = s + 1; i < e; i++) tmp[i - s - 1] = exe[i];
        tmp[e - s - 1] = 0;
    }
    {
        char ini[700];
        wsprintfA(ini, "%sSoulbringerModern.ini", dir);
        /* The settings (layout of the ini, 2026-11-28: one section per area, text sizes by
           category, one switch for the fonts; the 0.2-dev keys of before are not read any more).
           [Display]   Widescreen, MaxAspect
           [Interface] Layout, Hud, CursorSize
           [Text], [Text.Menus], [Text.Screens], [Text.Game]: text sizes (ts_key in font_places.inc)
           [Fonts]     All = Sharp | Original, and per font: Names, Tooltips, Journal, Menu,
                       Interface, Numbers, Credits (empty = follow All)
           [Debug]     Log, AutoCapture, ScreenshotKey, Stock, and the troubleshooting switches
                       TextHook, HudMouse, ConversationPortraitHold, FontPlaces, FontPageScale */
        char t[300];
        int sharp;
        g_cfgWide = (int)GetPrivateProfileIntA("Display", "Widescreen", 1, ini);
        GetPrivateProfileStringA("UI", 0, "", t, sizeof t, ini);   /* the key names of an old [UI] section, if any */
        g_cfgOldIni = t[0] != 0;
        g_cfgLayout = (int)GetPrivateProfileIntA("Interface", "Layout", 1, ini);
        g_cfgCursor = (int)GetPrivateProfileIntA("Interface", "CursorSize", 200, ini);
        GetPrivateProfileStringA("Interface", "Hud", "split", t, sizeof t, ini);
        {   char c0 = t[0] | 0x20;                  /* lower case first letter */
            g_cfgHud = c0 == 'r' ? 2 : c0 == 'c' ? 3 : (c0 == 's' && (t[1] | 0x20) == 't') ? 0 : 1;
        }
        g_cfgLog  = (int)GetPrivateProfileIntA("Debug", "Log", 0, ini);
        g_cfgAuto = (int)GetPrivateProfileIntA("Debug", "AutoCapture", 1, ini);
        GetPrivateProfileStringA("Debug", "ScreenshotKey", "F11", t, 16, ini);
        g_shotKey = shot_parse_key(t);
        g_cfgStock = (int)GetPrivateProfileIntA("Debug", "Stock", 0, ini);
        /* troubleshooting switches, on by default (not in the shipped ini's main sections) */
        g_cfgTextHook = (int)GetPrivateProfileIntA("Debug", "TextHook", 1, ini);
        g_cfgHudMouse = (int)GetPrivateProfileIntA("Debug", "HudMouse", 1, ini);
        g_cfgPortraitHold = (int)GetPrivateProfileIntA("Debug", "ConversationPortraitHold", 1, ini);
        g_cfgFontPlaces = (int)GetPrivateProfileIntA("Debug", "FontPlaces", 0, ini);
        g_cfgFontPageScale = (int)GetPrivateProfileIntA("Debug", "FontPageScale", 0, ini);
        {
            /* MaxAspect: "0"/"off"/"none", "16:9", "21:9", "64/27", "2.39" ... */
            float a = 0, b = 0, frac = 0; int i2, part = 0;
            GetPrivateProfileStringA("Display", "MaxAspect", "0", t, 32, ini);
            for (i2 = 0; t[i2]; i2++) {
                char ch = t[i2];
                if (ch >= '0' && ch <= '9') {
                    float* q = part ? &b : &a;
                    if (frac > 0) { *q += (ch - '0') * frac; frac *= 0.1f; } else *q = *q * 10 + (ch - '0');
                } else if (ch == '.') frac = 0.1f;
                else if (ch == ':' || ch == '/' || ch == 'x' || ch == 'X') { part = 1; frac = 0; }
                else if (ch != ' ') break;
            }
            g_cfgMaxAspect = part ? (b > 0 ? a / b : 0) : a;
            if (g_cfgMaxAspect < 1.3333f) g_cfgMaxAspect = g_cfgMaxAspect > 0 ? 1.3333334f : 0;   /* never below 4:3 */
        }

        /* [Fonts]: All = Sharp (default) or Original; each font: empty = follow All, a name, or
           (Names, Menu) the path of a .ttf / .otf file of your own, relative to the game folder */
        GetPrivateProfileStringA("Fonts", "All", "Sharp", t, 32, ini);
        sharp = !ini_word_is(t, "original", "off", "0");
        if (sharp && !ini_word_is(t, "sharp", "on", "1")) cfg_bad("Fonts", "All", t);
        GetPrivateProfileStringA("Fonts", "Interface", "", t, 32, ini);
        g_cfgUiFont = ini_pick(t, sharp, "Fonts", "Interface", "montserrat:1");
        GetPrivateProfileStringA("Fonts", "Numbers", "", t, 32, ini);
        g_cfgNumFont = ini_pick(t, sharp, "Fonts", "Numbers", "montserrat:1");
        GetPrivateProfileStringA("Fonts", "Credits", "", t, 32, ini);
        g_cfgCredFont = ini_pick(t, sharp, "Fonts", "Credits", "soulbringercredits:1 credits:1");
        GetPrivateProfileStringA("Fonts", "Tooltips", "", t, 32, ini);   /* 1 Soulbringer Text, 2 Cardo, 3 the game's */
        g_cfgTipFont = ini_pick(t, sharp, "Fonts", "Tooltips", "soulbringertext:1 text:1 cardo:2");
        g_cfgTipFont = g_cfgTipFont ? g_cfgTipFont : 3;
        GetPrivateProfileStringA("Fonts", "Journal", "", t, 32, ini);
        g_cfgJournalFont = ini_pick(t, sharp, "Fonts", "Journal", "soulbringertext:1 text:1 cardo:2");
        g_cfgJournalFont = g_cfgJournalFont ? g_cfgJournalFont : 3;
        GetPrivateProfileStringA("Fonts", "Menu", "", t, sizeof t, ini);   /* 1 Soulbringer Uncial, 2 MedievalSharp, 3 a file */
        if (ini_is_font_file(t)) g_cfgMenuFont = cfg_font_file(t, g_cfgMenuFile, g_cfgMenuFace, &g_cfgMenuWeight) ? 3 : 1;
        else g_cfgMenuFont = ini_pick(t, sharp, "Fonts", "Menu", "soulbringeruncial:1 uncial:1 eaglelake:1 medievalsharp:2");
        GetPrivateProfileStringA("Fonts", "Names", "", t, sizeof t, ini);  /* 1 Soulbringer Text, 2 a file */
        if (ini_is_font_file(t)) g_cfgNameFont = cfg_font_file(t, g_cfgNameFile, g_cfgNameFace, &g_cfgNameWeight) ? 2 : 1;
        else g_cfgNameFont = ini_pick(t, sharp, "Fonts", "Names", "soulbringertext:1 text:1");
        g_cfgMenuWanted = g_cfgMenuFont;
        g_cfgNameWanted = g_cfgNameFont;
        if (!g_cfgTextHook) { g_cfgMenuFont = 0; g_cfgNameFont = 0; }      /* their new cells are described to the game by the text hook */

        if (g_cfgStock) {          /* reference screenshots of the unmodified game: override every enhancement */
            g_cfgWide = 0; g_cfgLayout = 0; g_cfgMaxAspect = 0; g_cfgCursor = 100;
            g_cfgHudMouse = 0; g_cfgTextHook = 0; g_cfgFontPlaces = 0; g_cfgUiFont = 0; g_cfgNumFont = 0; g_cfgCredFont = 0; g_cfgMenuFont = 0; g_cfgNameFont = 0; g_cfgTipFont = 3; g_cfgJournalFont = 3;
        }
        g_cfgFontPages = g_cfgUiFont || g_cfgNumFont || g_cfgCredFont || g_cfgMenuFont || g_cfgNameFont || g_cfgTipFont < 3 || g_cfgJournalFont < 3;
        if (g_cfgCursor < 50) g_cfgCursor = 50; if (g_cfgCursor > 600) g_cfgCursor = 600;
        {   /* text sizes: every font kind's size, and (text hook) the items' own sizes; any item that
               differs from its kind turns the font copies on */
            int pm, k, i, mx;
            pm = fp_config(ini, g_cfgTextHook && !g_cfgStock);
            if (g_cfgStock) for (i = 0; i < TEXT_KINDS; i++) g_cfgTextKind[i] = 100;
            g_cfgText = g_cfgStock ? 100 : g_cfgTextKind[1];   /* without the text hook only cursor tooltips are stretched by the proxy */
            if (pm && !g_cfgFontPlaces) g_cfgFontPlaces = 2;     /* 2 = on because sizes are set (1 = [Debug] FontPlaces) */
            if (g_cfgFontPages) {      /* page scale k: FontPageScale, else the largest text scale /100 rounded up; then up to 1, 2 or 4 */
                mx = g_cfgText;
                for (i = 0; i < TEXT_KINDS; i++) if (g_cfgTextKind[i] > mx) mx = g_cfgTextKind[i];   /* the pages hold every font: the largest scale */
                if (pm > mx) mx = pm;                                                               /* ... including a place's own size */
                k = g_cfgFontPageScale > 0 ? g_cfgFontPageScale : (mx + 99) / 100;
                /* the renderer gives power-of-two surfaces: a 768 page would get a 1024 surface
                   (and a resampled upload), so only 256 x 1, 2 or 4; redrawn letters: always the 4x pages */
                if (g_cfgFontPageScale <= 0 && k < 4) k = 4;
                g_fpgK = k <= 1 ? 1 : k <= 2 ? 2 : 4;
                fpg_config();
            }
        }
    }
    if (g_cfgLog) {
        char lp[800];
        /* one file per game session; both renderer loads in a session share it */
        wsprintfA(lp, "%sSoulbringerModern_%s_%u.log", dir, tmp, GetCurrentProcessId());
        /* append: the game loads and unloads the renderer more than once per run */
        g_log = CreateFileA(lp, 0x4 /*FILE_APPEND_DATA*/, 3 /*SHARE_READ|WRITE*/, 0, 4 /*OPEN_ALWAYS*/, 0x80, 0);
    }

    LOG("\r\n******** Soulbringer Modern " SBM_VERSION " - DLL load, tick %u ********\r\n", GetTickCount());
    {
        SYSTIME_ t; GetLocalTime(&t);
        LOG("time:    %04u-%02u-%02u %02u:%02u:%02u\r\n", t.y, t.mo, t.d, t.h, t.mi, t.s);
    }
    LOG("proxy:   %s\r\n", path);
    LOG("process: %s\r\n", exe);
    if (g_cfgWarnLen) LOG("settings: not understood, the default is used instead:%s\r\n", g_cfgWarn);
    if (g_cfgOldIni) LOG("settings: this SoulbringerModern.ini is from an earlier development build ([UI] section): its [UI] keys are not read any more, defaults are used; replace it with the shipped one (DEVLOG 2026-11-28 maps the old keys)\r\n");

    wsprintfA(tmp, "%sD3d7Lib_orig.dll", dir);
    g_orig = LoadLibraryA(tmp);
    if (!g_orig) {
        LOG("FATAL: could not load %s\r\n", tmp);
        flush();
        MessageBoxA(0, "Soulbringer Modern: D3d7Lib_orig.dll was not found next to D3d7Lib.dll.\nRun install.bat again, or uninstall.bat to restore the original renderer.", "Soulbringer Modern", 0x10);
        ExitProcess(1);
    }
    LOG("original: %s\r\n", tmp);
    for (i = 0; i < NFUNC; i++) {
        g_fn[i] = GetProcAddress(g_orig, g_names[i]);
        if (!g_fn[i]) LOG("WARNING: original lacks %s\r\n", g_names[i]);
    }
    {
        char exename[300]; int k, m = 0, ln = (int)GetModuleFileNameA(0, exename, sizeof(exename));
        for (k = 0; k < ln; k++) if (exename[k] == '\\') m = k + 1;
        /* only the game exes, never the launcher */
        if (exename[m] == 'S' && exename[m+10] == 'V' && exename[m+11] == 'C') {
            if (g_cfgStock) LOG("STOCK mode ([Debug] Stock=1): all enhancements off, the game runs as shipped\r\n");
            apply_widescreen();
            tx_install();
            g_uiOn = g_cfgLayout || g_cfgMaxAspect > 0;
            if (g_cfgMaxAspect > 0) { char fa[16]; LOG("aspect cap: %s (wider screens are pillarboxed)\r\n", ftoa(g_cfgMaxAspect, fa)); }
            else LOG("aspect cap: none\r\n");
            LOG("ui layout: %s (cursor scale %d%%, text scale %d%%, HUD mouse %s, HUD %s)\r\n", g_uiOn ? "on" : "off", g_cfgCursor, g_cfgText, g_cfgHudMouse ? "on" : "off",
                g_cfgHud == 0 ? "stretched" : g_cfgHud == 2 ? "right" : g_cfgHud == 3 ? "center" : "split");
            if (g_cfgMenuWanted && !g_cfgMenuFont && !g_cfgStock) LOG("menu font: [Fonts] Menu needs the text hook ([Debug] TextHook=0 is set) - the game's menu letters stay\r\n");
            else if (g_cfgMenuFont) LOG("menu font: %s (magc redrawn into its own cells)\r\n", g_cfgMenuFont == 1 ? "Eagle Lake with lining figures (Soulbringer Uncial)" : g_cfgMenuFont == 2 ? "MedievalSharp" : g_cfgMenuFace);
            if (g_cfgMenuFont == 3) LOG("menu font: from %s (face \"%s\", weight %d)\r\n", g_cfgMenuFile, g_cfgMenuFace, g_cfgMenuWeight);
            if (g_cfgNameWanted && !g_cfgNameFont && !g_cfgStock) LOG("name font: [Fonts] Names needs the text hook ([Debug] TextHook=0 is set) - the game's name letters stay\r\n");
            LOG("journal font: %s%s\r\n", g_cfgJournalFont == 1 ? "Soulbringer Text" : g_cfgJournalFont == 2 ? "Cardo" : "the game's letters",
                                      g_cfgJournalFont < 3 ? (g_cfgTextHook ? " (phil redrawn into its own cells)" : " (phil redrawn in its original rectangles: TextHook is off)") : "");
            LOG("tooltip font: %s%s\r\n", g_cfgTipFont == 1 ? "Soulbringer Text" : g_cfgTipFont == 2 ? "Cardo" : "the game's letters",
                                  g_cfgTipFont < 3 ? (g_cfgTextHook ? " (hbut redrawn into its own cells)" : " (hbut redrawn in its original rectangles: TextHook is off)") : "");
            if (g_cfgNameFont) LOG("name font: %s (mgrn/mred redrawn into their own cells)\r\n", g_cfgNameFont == 1 ? "Soulbringer Text" : g_cfgNameFace);
            if (g_cfgNameFont == 2) LOG("name font: from %s (face \"%s\", weight %d)\r\n", g_cfgNameFile, g_cfgNameFace, g_cfgNameWeight);
        }
        else LOG("widescreen: skipped for this process\r\n");
    }
    LOG("\r\n");
    flush();
}

/* ---------------------------------------------------------------- stats */

typedef struct {
    DWORD caller;
    int   idx;
    DWORD calls;
    DWORD verts;
    float minx, maxx, miny, maxy, minz, maxz, minw, maxw;
    float sample[6];           /* x,y of first 3 vertices seen */
    int   hasSample;
    DWORD a0, a1;              /* first two args of the first call */
} Stat;

#define MAXSTAT 1024
static Stat  g_stat[MAXSTAT];
static int   g_nstat;
static DWORD g_firstCalls[NFUNC];
static DWORD g_totalCalls[NFUNC];
static DWORD g_frames, g_windowStart, g_windowFrames;
static int   g_marker;
static int   g_f9down;
static int   g_modesDumped;

static Stat* stat_for(int idx, DWORD caller) {
    int i;
    for (i = 0; i < g_nstat; i++) if (g_stat[i].caller == caller && g_stat[i].idx == idx) return &g_stat[i];
    if (g_nstat >= MAXSTAT) return 0;
    {
        Stat* s = &g_stat[g_nstat++];
        memset(s, 0, sizeof(*s));
        s->caller = caller; s->idx = idx;
        s->minx = s->miny = s->minz = s->minw = 1e30f;
        s->maxx = s->maxy = s->maxz = s->maxw = -1e30f;
        return s;
    }
}

/* vertex buffers handed out by Add* calls, read at the next renderer call
   (by then the game has finished writing them) */
typedef struct { const float* v; int n; Stat* s; DWORD caller; int idx; DWORD a0, a1, a2, a3; int td; } Pending;   /* td: decoded text batch or -1 */
#include "text_decode.inc"
#include "text_scale.inc"
/* font copies live for the whole process (text objects may point at one after this DLL unloads) */
#define FP_ALLOC(n) ((DWORD)VirtualAlloc(0, (n), 0x3000 /*MEM_COMMIT|MEM_RESERVE*/, 4 /*PAGE_READWRITE*/))
#include "font_places.inc"

/* ---------------------------------------------------------------- text hook (step 3, phase 3)
 * Three code patches, each applied only after its original bytes are
 * verified, all removed when the DLL unloads (see text_scale.inc):
 *   0x681F5E  call 0x628394 (font file read)  -> tx_load_thunk: read, then scale the font
 *   0x626480  add glyph entry (6 bytes)       -> tx_glyph_thunk: rewrite the texels, then the original
 *   0x691F50  letter-spacing setter (5 bytes) -> tx_spacing_thunk
 * The add-glyph patch goes in first: a scaled font without it would show the
 * wrong part of the font texture. Fonts stay scaled after unload (the game
 * draws nothing without its renderer); the next load finds them scaled.
 */
#define TX_LOAD_CALL 0x681F5E
#define TX_ADD_GLYPH 0x626480
#define TX_SPACING   0x691F50
#define TX_GLYPH_LO  0x433714          /* draw glyph: the only caller of add glyph */
#define TX_GLYPH_HI  0x434280
static const unsigned char tx_origLoad[5] = { 0xE8, 0x31, 0x64, 0xFA, 0xFF };   /* call 0x628394 */
static const unsigned char tx_origAdd[6]  = { 0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08 }; /* push ebx; mov ebx,esp; sub esp,8 */
static const unsigned char tx_origSp[9]   = { 0x56, 0x8B, 0x44, 0x24, 0x08, 0x89, 0x41, 0x2C, 0x83 };
/* auto-fit fix (text_scale.inc tx_autofit_choice): 0x411634 entry, 9 bytes: push ebp; mov ebp,esp; sub esp,0xA0 */
#define TX_AUTOFIT   0x411634
#define TX_LAYOUT    0x413A64          /* thiscall(text, width, int* outW) -> height, ret 8 */
#define TX_BOXPOS    0x621774          /* thiscall(text): what the auto-fit calls after setting the box */
#define TX_BOXDONE   0x585050          /* thiscall(text): ... and last */
static const unsigned char tx_origFit[9] = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00 };
DWORD tx_fitBack = TX_AUTOFIT + 9;
/* main-menu copyright and save-list date (text_scale.inc tx_copyright_xy / tx_date_limit) */
#define TX_COPYPOS   0x666143          /* mov [edi+0x64],eax; mov edx,[ebp-0x12C]; add esp,-8; mov [edi+0x60],edx */
#define TX_DATEMAX   0x645ED4          /* mov dword [esp+0xC],200: the date's "..." width limit */
static const unsigned char tx_origCopy[15] = { 0x89, 0x47, 0x64, 0x8B, 0x95, 0xD4, 0xFE, 0xFF, 0xFF, 0x83, 0xC4, 0xF8, 0x89, 0x57, 0x60 };
static const unsigned char tx_origDate[8] = { 0xC7, 0x44, 0x24, 0x0C, 0xC8, 0x00, 0x00, 0x00 };
DWORD tx_copyBack = TX_COPYPOS + 15;
static int g_cpIn, g_dateIn;
extern void tx_copy_thunk(void);
/* called by tx_copy_thunk: r = the text rectangle (left, top, right, bottom) from 0x549FC4,
   yx = the game's y and x, replaced */
void __cdecl tx_copy_pos(const int* r, int* yx) {
    int W = *(const int*)0x367370C, H = *(const int*)0x3673708, x, y;
    tx_copyright_xy(W, H, r[2] - r[0], r[3] - r[1], &x, &y);
    if (g_cfgLog && g_cpIn == 1) { LOG("copyright: game put it at %d,%d (%dx%d), moved to %d,%d\r\n", yx[1], yx[0], r[2] - r[0], r[3] - r[1], x, y); g_cpIn = 2; }
    yx[0] = y; yx[1] = x;
}
static int g_afIn, g_afFixed, g_afBusy;
extern void tx_fit_thunk(void);
DWORD tx_addBack = TX_ADD_GLYPH + 6;
DWORD tx_readFn = 0x628394;
static int g_txIn, g_txFix, g_txFixed, g_txFonts;
extern void tx_load_thunk(void);
extern void tx_glyph_thunk(void);
extern void tx_spacing_thunk(void);

static const char* tx_name(const unsigned char* font) {
    const char* n = readable(font, 4) ? (const char*)*(const DWORD*)font : 0;
    return n && readable(n, 1) ? n : "?";
}
/* scale a font object at its kind's size; place >= 0: it is the copy of that place (font_places.inc),
   scaled at the place's own size if one is set; else loadSite: where the game loaded it (a place of its
   own if listed in fp_load), 0 if not known */
static void tx_do_font_at(unsigned char* font, const char* where, int place, DWORD loadSite) {
    const char* why; int lh = readable(font, 0x30) ? *(int*)(font + 0x18) : 0;
    int f = tx_font_file(font), kind = f >= 0 ? g_txPctFile[f] : g_txPct;
    int pct = place >= 0 ? fp_pct(place, kind) : g_cfgFontPlaces ? fp_load_pct(loadSite, kind) : kind;
    int r = tx_scale_font(font, pct, &why);
    if (r == 1) g_txFonts++;
    LOG("text hook: %s font %s %08X at %d%%%s: %s%s%s (line height %d -> %d)\r\n", where, tx_name(font), (DWORD)font, pct,
        pct != kind ? " (place size)" : "",
        r == 1 ? "scaled" : r == 2 ? "kept" : "not scaled", *why ? ", " : "", why, lh, readable(font, 0x30) ? *(int*)(font + 0x18) : 0);
}
static void tx_do_font(unsigned char* font, const char* where) { tx_do_font_at(font, where, -1, 0); }
/* ---- per-place fonts ([Debug] FontPlaces=1, font_places.inc; TEXT_SCALING_PLAN phase 5b)
 * The redirects go in the first time a font with sites is loaded (the copies
 * exist from then on; before, every site reads the original). Each changes
 * only the imm32 of its `mov reg,[global]` to the address of its slot below,
 * after the opcode and the original imm32 are checked; all restored on unload. */
static DWORD g_fpSlot[FP_NSITE];          /* what each redirected instruction reads: its copy */
static unsigned char g_fpOn[FP_NSITE];
static int g_fpRedirects, g_fpSkipped;
static DWORD fp_gv(DWORD global) { return readable((const void*)global, 4) ? *(const DWORD*)global : 0; }
static void fp_imm(DWORD addr, DWORD v) {
    unsigned char* p = (unsigned char*)addr; DWORD old;
    if (!VirtualProtect(p, 4, 0x40, &old)) return;
    *(DWORD*)p = v;
    VirtualProtect(p, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 4);
}
static void fp_redirect_on(void) {
    int i, now = 0;
    for (i = 0; i < FP_NSITE; i++) {
        const unsigned char* p = (const unsigned char*)fp_site[i].site; DWORD imm;
        if (g_fpOn[i] || !g_fpCopy[i].obj) continue;
        if (!readable(p, 6)) { g_fpSkipped++; g_fpOn[i] = 2; LOG("font places: %06X not readable - skipped\r\n", fp_site[i].site); continue; }
        imm = *(const DWORD*)(p + fp_site[i].immAt);
        /* original: opcode + the global; or left by an earlier copy of this DLL: opcode + an address outside
           the exe image (0x400000-0x36DE000; the font globals are inside it) */
        if (!fp_opcode_ok(p, fp_site[i].immAt) || (imm != fp_site[i].global && imm >= 0x400000 && imm < 0x36DE000)) {
            g_fpSkipped++; g_fpOn[i] = 2;
            LOG("font places: bytes at %06X did not match (%02X %02X, %08X) - this place keeps the shared font\r\n", fp_site[i].site, p[0], p[1], imm);
            continue;
        }
        g_fpSlot[i] = g_fpCopy[i].obj;
        fp_imm(fp_site[i].site + fp_site[i].immAt, (DWORD)&g_fpSlot[i]);
        g_fpOn[i] = 1; g_fpRedirects++; now++;
    }
    if (now) LOG("font places: %d place(s) now read their own copy (%d in all, %d skipped)\r\n", now, g_fpRedirects, g_fpSkipped);
}
static void fp_redirect_off(void) {
    int i;
    for (i = 0; i < FP_NSITE; i++)
        if (g_fpOn[i] == 1) { fp_imm(fp_site[i].site + fp_site[i].immAt, fp_site[i].global); g_fpOn[i] = 0; }
    g_fpRedirects = 0;
}

/* Text sizes from the ini, read once at load (before the log is open; fp_config_log logs them):
   1. every [Text] / [Text.Menus] / [Text.Screens] / [Text.Game] setting (ts_key in font_places.inc),
      percent, 0 / missing = its parent's; the font kinds' sizes go to g_cfgTextKind;
   2. with places (the text hook is in): the items whose size differs from their places' kind
      give those places that size (ts_groups);
   3. [PlaceTextScale] <site>=<pct>: developer override for one place by its address in the log
      ("copy@4471BB" -> 4471BB=150; load sites too), applied after the items. Not in the shipped ini. */
static int g_fpOverrides;
static int fp_config(const char* ini, int places) {
    int g, i;
    for (g = 0; g < TS_N; g++) g_tsVal[g] = (int)GetPrivateProfileIntA(ts_key[g].section, ts_key[g].key, 0, ini);
    ts_kinds(g_cfgTextKind);
    if (!places) return 0;
    ts_groups(g_cfgTextKind);
    for (i = 0; i < FP_NSITE + FP_NLOAD; i++) {
        char key[12]; DWORD a = i < FP_NSITE ? fp_site[i].site : fp_load[i - FP_NSITE]; int v;
        wsprintfA(key, "%06X", a);
        v = (int)GetPrivateProfileIntA("PlaceTextScale", key, 0, ini);
        if (v > 0) { fp_set_pct(a, v); g_fpOverrides++; }
    }
    return fp_max_pct();
}
static void fp_config_log(void) {
    char line[900]; int g, i, n = 0, cnt = 0, measSet = 0, magcSet = 0;
    for (g = 0; g < TS_N; g++)
        if (g_tsSet[g] && n < (int)sizeof line - 48) { n += wsprintfA(line + n, " %s/%s=%d", ts_key[g].section, ts_key[g].key, ts_pct(g)); cnt++; }
    line[n] = 0;
    if (cnt) LOG("text sizes by place (own font copies):%s\r\n", line);
    else LOG("text sizes by place: every item at its font kind's size (no own copies needed)\r\n");
    if (g_fpOverrides) {
        n = 0;
        for (i = 0; i < FP_NSITE; i++) if (g_fpPct[i] && n < (int)sizeof line - 24) n += wsprintfA(line + n, " %06X=%d", fp_site[i].site, g_fpPct[i]);
        for (i = 0; i < FP_NLOAD; i++) if (g_fpLoadPct[i] && n < (int)sizeof line - 24) n += wsprintfA(line + n, " %06X=%d", fp_load[i], g_fpLoadPct[i]);
        line[n] = 0;
        LOG("text sizes by place: %d [PlaceTextScale] override(s); every place now:%s\r\n", g_fpOverrides, line);
    }
    for (i = 0; i < FP_NSITE; i++) if (g_fpPct[i]) { if (fp_site[i].how == 2) measSet++; else if (fp_site[i].global == 0x213F3D8) magcSet++; }
    /* 549F7B / 54A524 measure a magc list with the font itself: they must have the size of the place drawing that list */
    if (magcSet && !measSet)
        LOG("font places: note: menu font places are sized but the two measuring places (549F7B, 54A524) are not; if a list they measure is drawn larger, give them the same size\r\n");
}

/* load sites of the original font objects, for the place log */
#define PL_MAXOBJ 96
static DWORD g_plObj[PL_MAXOBJ], g_plSite[PL_MAXOBJ]; static int g_plNObj;
static void pl_note_obj(DWORD obj, DWORD site) {
    int i;
    for (i = 0; i < g_plNObj; i++) if (g_plObj[i] == obj) { g_plSite[i] = site; return; }
    if (g_plNObj < PL_MAXOBJ) { g_plObj[g_plNObj] = obj; g_plSite[g_plNObj++] = site; }
}
static DWORD pl_site_of(DWORD obj) {
    int i;
    for (i = 0; i < g_plNObj; i++) if (g_plObj[i] == obj) return g_plSite[i];
    return 0;
}

void __cdecl tx_after_load(unsigned char* font, DWORD loaderEbp) {
    /* 0x681ED0 keeps a standard frame: [ebp+4] returns into the place that loaded this font */
    DWORD site = readable((const void*)(loaderEbp + 4), 4) ? *(const DWORD*)(loaderEbp + 4) - 5 : 0;
    char where[48]; int made = 0, i;
    pl_note_obj((DWORD)font, site);
    if (g_cfgFontPlaces) made = fp_refresh((DWORD)font, fp_gv);    /* before the original is scaled: copy it fresh */
    wsprintfA(where, "loaded (by %06X)", site);
    tx_do_font_at(font, where, -1, site);
    if (made) {
        for (i = 0; i < FP_NSITE; i++)
            if (g_fpCopy[i].src == (DWORD)font && g_fpCopy[i].obj) {
                wsprintfA(where, "copy for place %06X", fp_site[i].site);
                tx_do_font_at((unsigned char*)g_fpCopy[i].obj, where, i, 0);
            }
        LOG("font places: %s %08X copied for %d place(s)\r\n", tx_name(font), (DWORD)font, made);
        fp_redirect_on();
    }
    flush();
}

/* debug log: the text each font object draws, one "place:" line per new string.
   Glyphs of one object on one pen line join into a string; a gap wider than
   the line height / 6 is a space (spaces are not drawn; /3 lost the spaces of hbut and magc). */
static DWORD g_plFont, g_plText; static int g_plX1, g_plY, g_plX0, g_plGap, g_plN; static char g_plBuf[100];
/* the text-object pool built by 0x66B400 (64 objects at 0x1E4AF64, shared by several screens:
   inventory labels and stack counts, character screen values...): index of obj in it, or -1 */
#define PL_POOL 0x1E4AF64
static int pl_pool_index(DWORD obj) {
    int i;
    if (!obj || !readable((const void*)PL_POOL, 64 * 4)) return -1;
    for (i = 0; i < 64; i++) if (((const DWORD*)PL_POOL)[i] == obj) return i;
    return -1;
}
static DWORD g_plSeen[8192]; static int g_plSeenN, g_plFull;
static void pl_label(DWORD font, char* out) {
    int p = fp_place(font), k = pl_pool_index(g_plText), n;
    if (p >= 0) n = wsprintfA(out, "%s copy@%06X", tx_name((const unsigned char*)font), fp_site[p].site);
    else n = wsprintfA(out, "%s obj %08X by %06X", tx_name((const unsigned char*)font), font, pl_site_of(font));
    /* the text object that drew it (when known) and its pool entry; pen position of the first glyph */
    if (g_plText) n += wsprintfA(out + n, " text %08X", g_plText);
    if (k >= 0) n += wsprintfA(out + n, " pool#%d", k);
    wsprintfA(out + n, " at %d,%d", g_plX0, g_plY);
}
/* debug (options-menu text boxes, 2026-11-26): the layout fields of the text object behind a new
   string, as raw dwords (ints; floats show as 10-digit numbers): clip +0x0C..+0x18, box +0x1C..+0x28,
   text rect +0x2C..+0x38, sizing mode +0x58 and fixed width +0x5C, position +0x60/+0x64, offset
   +0x68/+0x6C, +0x70, kerning +0x1B8, alignment +0x1C0, auto-fit tries +0x1C8, draw mode +0x1D0,
   keep inside +0x1D8, extra leading +0x1DC */
static __attribute__((noinline)) void pl_textobj(DWORD obj) {
    static const unsigned short off[] = { 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38,
        0x58, 0x5C, 0x60, 0x64, 0x68, 0x6C, 0x70, 0x1B8, 0x1C0, 0x1C8, 0x1D0, 0x1D8, 0x1DC };
    char line[600]; int n = 0, i;
    if (!obj || !readable((const void*)obj, 0x1E0)) return;
    n += wsprintfA(line + n, "  textobj %08X:", obj);
    for (i = 0; i < (int)(sizeof off / sizeof off[0]); i++)
        n += wsprintfA(line + n, " %X=%d", off[i], *(const int*)(obj + off[i]));
    LOG("%s\r\n", line);
}
static void pl_flush(void) {
    DWORD h = 2166136261u; int i;
    if (!g_plN) return;
    g_plBuf[g_plN] = 0;
    h = (h ^ g_plFont) * 16777619u;
    h = (h ^ g_plText) * 16777619u;                  /* the same string from another text object is logged again */
    for (i = 0; i < g_plN; i++) h = (h ^ (unsigned char)g_plBuf[i]) * 16777619u;
    if (!h) h = 1;
    for (i = (int)(h & 8191); g_plSeen[i]; i = (i + 1) & 8191) if (g_plSeen[i] == h) { g_plN = 0; return; }
    if (g_plSeenN < 7000) {
        char lab[160];
        g_plSeen[i] = h; g_plSeenN++;
        pl_label(g_plFont, lab);
        LOG("place: %s \"%s\"\r\n", lab, g_plBuf);
        pl_textobj(g_plText);
    } else if (!g_plFull) { g_plFull = 1; LOG("place: 7000 strings logged, no more\r\n"); }
    g_plN = 0;
}
static void pl_glyph(DWORD font, DWORD obj, int code, int x, int y, int x1) {
    char ch = td_ascii((unsigned short)code);
    if (!ch) return;
    if (font != g_plFont || obj != g_plText || y != g_plY || x < g_plX1 - 2 || g_plN >= (int)sizeof g_plBuf - 2) {
        pl_flush();
        g_plFont = font; g_plText = obj; g_plY = y; g_plX0 = x;
        g_plGap = readable((const void*)font, 0x30) ? *(const int*)(font + 0x18) / 6 : 4;
        if (g_plGap < 2) g_plGap = 2;
    } else if (x - g_plX1 > g_plGap) g_plBuf[g_plN++] = ' ';
    g_plBuf[g_plN++] = ch;
    g_plX1 = x1;
}

void __cdecl tx_glyph(DWORD ret, unsigned char* blk, int mat, DWORD ebp) {
    if (ret < TX_GLYPH_LO || ret >= TX_GLYPH_HI) return;    /* called from 0x433714: ebp is its frame, blk its locals */
    g_txFix++;
    g_txFixed += tx_fix_block(blk, mat, *(int*)(ebp + 0xC), *(int*)(ebp - 0x3C), *(int*)(ebp - 0x4C), *(int*)(ebp - 0x48));
    /* 0x433714: [ebp+8] character code (u16), [ebp+0xC] pen x, [ebp+0x10] pen y, [ebp-0x20] font, [ebp-0x4C] right edge */
    if (g_cfgLog && g_cfgFontPlaces) {
        /* called by draw word 0x432820 (returns to 0x432D44): it keeps its text object at [its ebp-0x10] */
        DWORD obj = 0, up = *(DWORD*)ebp;
        if (*(DWORD*)(ebp + 4) == 0x432D44 && readable((const void*)(up - 0x10), 4)) obj = *(DWORD*)(up - 0x10);
        pl_glyph(*(DWORD*)(ebp - 0x20), obj, *(unsigned short*)(ebp + 8), *(int*)(ebp + 0xC), *(int*)(ebp + 0x10), *(int*)(ebp - 0x4C));
    }
}
/* after the game's auto-fit 0x411634 (text in ecx): keep an enlarged mode-2 label on one line */
static int tx_lay_game(void* text, int width, int* outW) {
    typedef int (__thiscall *Lay)(void*, int, int*);
    return ((Lay)TX_LAYOUT)(text, width, outW);
}
void __cdecl tx_after_fit(unsigned char* text) {
    unsigned char* font; int reg, pct, step, nw, nh, bw, bh;
    if (g_afBusy || !text || !readable(text, 0x1E0)) return;
    font = (unsigned char*)*(DWORD*)(text + 0x208);
    if (!font || !readable(font, 0x30)) return;
    reg = tx_registered(font);
    pct = reg ? g_txFont[reg - 1].pct : 100;
    step = *(int*)(font + 0x18) + *(int*)(font + 0x20) + *(int*)(text + 0x1DC);
    bw = *(int*)(text + 0x24) - *(int*)(text + 0x1C); bh = *(int*)(text + 0x28) - *(int*)(text + 0x20);
    g_afBusy = 1;
    if (tx_autofit_choice(*(int*)(text + 0x58), *(int*)(text + 0x5C), *(int*)(text + 0x14) - *(int*)(text + 0x0C), bw, bh,
                          pct, step, tx_lay_game, text, &nw, &nh)) {
        typedef void (__thiscall *Fn)(void*);
        *(int*)(text + 0x1C) = 0; *(int*)(text + 0x20) = 0; *(int*)(text + 0x24) = nw; *(int*)(text + 0x28) = nh;
        ((Fn)TX_BOXPOS)(text);                     /* as the auto-fit does after setting a box */
        ((Fn)TX_BOXDONE)(text);
        if (g_cfgLog && g_afFixed < 200) {
            char str[48]; int i; const unsigned short* u = readable((const void*)*(DWORD*)text, 2) ? (const unsigned short*)*(DWORD*)text : 0;
            for (i = 0; u && i < 40 && u[i] && readable(u + i, 2); i++) str[i] = u[i] < 128 && u[i] >= 32 ? (char)u[i] : '?';
            str[i] = 0;
            LOG("autofit: text %08X \"%s\" (%s at %d%%) box %dx%d -> %dx%d (one line)\r\n", (DWORD)text, str, tx_name(font), pct, bw, bh, nw, nh);
        }
        g_afFixed++;
    }
    g_afBusy = 0;
}
void __cdecl tx_set_spacing(unsigned char* font, int v) {
    *(int*)(font + 0x2C) = tx_spacing(font, v);
    if (g_cfgFontPlaces) fp_spacing((DWORD)font, v);         /* the copies of this font follow the setter */
}
__asm__(
    ".intel_syntax noprefix\n"
    ".text\n"
    ".globl _tx_load_thunk\n"
    "_tx_load_thunk:\n"              /* ecx = font, as for the original call */
    "  push ecx\n"
    "  call DWORD PTR [_tx_readFn]\n"
    "  pop ecx\n"
    "  pushad\n"
    "  push ebp\n"                    /* 0x681ED0's frame: [ebp+4] is the load site */
    "  push ecx\n"
    "  call _tx_after_load\n"
    "  add esp, 8\n"
    "  popad\n"                       /* eax = the read's result */
    "  ret\n"
    ".globl _tx_glyph_thunk\n"
    "_tx_glyph_thunk:\n"             /* [esp] = return into 0x433714, [esp+4] = corner block, [esp+8] = material */
    "  pushfd\n"
    "  pushad\n"
    "  push ebp\n"                    /* 0x433714's frame */
    "  push DWORD PTR [esp+48]\n"     /* material */
    "  push DWORD PTR [esp+48]\n"     /* block */
    "  push DWORD PTR [esp+48]\n"     /* return address */
    "  call _tx_glyph\n"
    "  add esp, 16\n"
    "  popad\n"
    "  popfd\n"
    "  push ebx\n"                    /* the 6 bytes replaced at 0x626480 */
    "  mov ebx, esp\n"
    "  sub esp, 8\n"
    "  jmp DWORD PTR [_tx_addBack]\n"
    ".globl _tx_fit_thunk\n"
    "_tx_fit_thunk:\n"               /* replaces 0x411634's first 9 bytes: thiscall(text), plain ret */
    "  push ecx\n"
    "  call _tx_fit_orig\n"            /* the original auto-fit, ecx = text */
    "  pop ecx\n"
    "  pushad\n"
    "  push ecx\n"
    "  call _tx_after_fit\n"
    "  add esp, 4\n"
    "  popad\n"
    "  ret\n"
    "_tx_fit_orig:\n"                /* the 9 replaced bytes, then the rest of 0x411634 */
    "  push ebp\n"
    "  mov ebp, esp\n"
    "  sub esp, 0xA0\n"
    "  jmp DWORD PTR [_tx_fitBack]\n"
    ".globl _tx_copy_thunk\n"
    "_tx_copy_thunk:\n"              /* replaces 15 bytes at 0x666143 (main menu, the copyright line's position) */
    "  push DWORD PTR [ebp-0x12C]\n"  /* the game's x */
    "  push eax\n"                    /* the game's y */
    "  mov ecx, esp\n"
    "  pushad\n"
    "  lea edx, [ebp-0x49C]\n"        /* its rectangle: left, top, right, bottom */
    "  push ecx\n"
    "  push edx\n"
    "  call _tx_copy_pos\n"
    "  add esp, 8\n"
    "  popad\n"
    "  pop eax\n"
    "  pop edx\n"
    "  mov DWORD PTR [edi+0x64], eax\n"
    "  add esp, -8\n"
    "  mov DWORD PTR [edi+0x60], edx\n"
    "  jmp DWORD PTR [_tx_copyBack]\n"
    ".globl _tx_spacing_thunk\n"
    "_tx_spacing_thunk:\n"           /* thiscall(font, value), ret 4: replaces the whole setter */
    "  pushad\n"
    "  push DWORD PTR [esp+36]\n"
    "  push ecx\n"
    "  call _tx_set_spacing\n"
    "  add esp, 8\n"
    "  popad\n"
    "  mov eax, [esp+4]\n"
    "  ret 4\n"
    ".att_syntax prefix\n");

/* write op (E8 call / E9 jmp) + rel32 to target at addr, nops up to len */
static int tx_write(DWORD addr, unsigned char op, void* target, int len) {
    unsigned char* p = (unsigned char*)addr; DWORD old, rel = (DWORD)target - (addr + 5); int i;
    if (!VirtualProtect(p, (DWORD)len, 0x40, &old)) return 0;
    p[0] = op; p[1] = (unsigned char)rel; p[2] = (unsigned char)(rel >> 8); p[3] = (unsigned char)(rel >> 16); p[4] = (unsigned char)(rel >> 24);
    for (i = 5; i < len; i++) p[i] = 0x90;
    VirtualProtect(p, (DWORD)len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, (DWORD)len);
    return 1;
}
static void tx_restore(DWORD addr, const unsigned char* orig, int len) {
    unsigned char* p = (unsigned char*)addr; DWORD old; int i;
    if (!VirtualProtect(p, (DWORD)len, 0x40, &old)) return;
    for (i = 0; i < len; i++) p[i] = orig[i];
    VirtualProtect(p, (DWORD)len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, (DWORD)len);
}
/* original bytes, or our own patch left by an earlier copy of this DLL (op + rel32, then nops) */
static int tx_check(DWORD addr, const unsigned char* orig, int len, int cmp, unsigned char op, const char* what) {
    const unsigned char* p = (const unsigned char*)addr; int i, same = 1, ours = p[0] == op;
    if (!readable(p, (DWORD)len)) { LOG("text hook: %s not readable - not this exe?\r\n", what); return 0; }
    for (i = 0; i < cmp; i++) if (p[i] != orig[i]) same = 0;
    for (i = 5; i < len; i++) if (p[i] != 0x90) ours = 0;
    if (op == 0xE8 && same) ours = 0;
    if (same || ours) { if (ours) LOG("text hook: %s: found an earlier copy of our patch, re-pointing it\r\n", what); return 1; }
    LOG("text hook: original bytes did not match at %08X (%s) - text hook NOT installed\r\n", addr, what);
    return 0;
}

/* font objects the game keeps in globals (stored right after the font constructor
   0x4D9160); fonts loaded before this DLL are found here and scaled in place */
static const DWORD tx_globals[] = { 0x1E4B06C, 0x1E4EF84, 0x1E78684, 0x213ACF0, 0x213ACF4, 0x213ACF8, 0x213F3D8,
    0x213F3DC, 0x213F3E0, 0x213F3E4, 0x213F3E8, 0x213F3EC, 0x35565DC, 0x35565E0, 0x35590FC, 0x3559100, 0x3559104, 0x6F1774 };

/* the largest text size of the font kinds */
static int g_txMaxKind(void) {
    int f, m = 100;
    for (f = 0; f < 16; f++) if (g_txPctFile[f] > m) m = g_txPctFile[f];
    return m;
}
static void tx_install(void) {
    int i;
    if (!g_cfgTextHook) return;
    {
        int f, any = 0;
        for (f = 0; f < 16; f++) { g_txPctFile[f] = g_cfgTextKind[g_fontKind[f]]; if (g_txPctFile[f] != 100) any = 1; }
        if (g_cfgFontPlaces) { for (f = 0; f < FP_NSITE; f++) if (g_fpPct[f] && g_fpPct[f] != 100) any = 1;
                               for (f = 0; f < FP_NLOAD; f++) if (g_fpLoadPct[f] && g_fpLoadPct[f] != 100) any = 1; }
        if (!any) { LOG("text hook: every text scale is 100%%, nothing to do\r\n"); return; }
    }
    if (!tx_check(TX_ADD_GLYPH, tx_origAdd, 6, 6, 0xE9, "add glyph 0x626480")) return;
    if (!tx_check(TX_SPACING, tx_origSp, 5, 9, 0xE9, "spacing setter 0x691F50")) return;
    if (!tx_check(TX_LOAD_CALL, tx_origLoad, 5, 5, 0xE8, "font load 0x681F5E")) return;
    tx_load(g_dir);
    if (!g_txKeys) { LOG("text hook: no font files read - text hook NOT installed\r\n"); return; }
    fpg_own_install();             /* HD fonts: new cells for phil/hbut before any font is scaled */
    g_txPct = g_cfgText;
    if (!tx_write(TX_ADD_GLYPH, 0xE9, (void*)tx_glyph_thunk, 6)) { LOG("text hook: VirtualProtect failed\r\n"); return; }
    g_txIn = 1;
    tx_write(TX_SPACING, 0xE9, (void*)tx_spacing_thunk, 5);
    g_txIn = 2;
    for (i = 0; i < (int)(sizeof tx_globals / sizeof tx_globals[0]); i++) {
        DWORD f = readable((const void*)tx_globals[i], 4) ? *(const DWORD*)tx_globals[i] : 0;
        if (f && readable((const void*)f, 0x30) && *(const int*)(f + 0x1C)) tx_do_font((unsigned char*)f, "already loaded");
    }
    tx_write(TX_LOAD_CALL, 0xE8, (void*)tx_load_thunk, 5);
    g_txIn = 3;
    /* auto-fit fix: optional, the text hook works without it */
    if (tx_check(TX_AUTOFIT, tx_origFit, 9, 9, 0xE9, "auto-fit 0x411634") && tx_write(TX_AUTOFIT, 0xE9, (void*)tx_fit_thunk, 9)) {
        g_afIn = 1;
        LOG("text hook: auto-fit fix in (enlarged mode-2 labels kept on one line)\r\n");
    }
    /* main menu: the copyright line back at the bottom right */
    if (tx_check(TX_COPYPOS, tx_origCopy, 15, 15, 0xE9, "copyright position 0x666143") && tx_write(TX_COPYPOS, 0xE9, (void*)tx_copy_thunk, 15)) {
        g_cpIn = 1;
        LOG("text hook: copyright line kept at the bottom right of the main menu\r\n");
    }
    /* save list: the date's "..." limit grows with the text */
    {   unsigned char* p = (unsigned char*)TX_DATEMAX; DWORD old;
        int lim = tx_date_limit(fp_max_pct() > g_txMaxKind() ? fp_max_pct() : g_txMaxKind());
        if (readable(p, 8) && p[0] == 0xC7 && p[1] == 0x44 && p[2] == 0x24 && p[3] == 0x0C && VirtualProtect(p + 4, 4, 0x40, &old)) {
            *(int*)(p + 4) = lim;
            VirtualProtect(p + 4, 4, old, &old);
            FlushInstructionCache(GetCurrentProcess(), p, 8);
            g_dateIn = 1;
            LOG("text hook: save-list date cut at %d px instead of 200\r\n", lim);
        } else LOG("text hook: save-list date limit: bytes did not match at %08X - not changed\r\n", TX_DATEMAX);
    }
    /* the tooltip rules in ui_layout.inc follow the tooltip font (hbut), or the largest size of the cursor-tooltip places */
    g_gts = (g_cfgFontPlaces ? fp_tip_pct(g_cfgTextKind[1]) : g_cfgTextKind[1]) / 100.0f;
    g_cfgText = 100;               /* the proxy no longer stretches tooltips: the game draws them larger */
    LOG("text hook: installed (proxy tooltip stretching off); the game draws text at: journal %d%%, tooltips %d%%, menus %d%%, interface %d%%, names %d%%, numbers %d%%, credits %d%%\r\n",
        g_cfgTextKind[0], g_cfgTextKind[1], g_cfgTextKind[2], g_cfgTextKind[3], g_cfgTextKind[4], g_cfgTextKind[5], g_cfgTextKind[6]);
    if (g_cfgFontPlaces) LOG("font places: on%s, %d places of shared fonts (16xy, 16xb, 16xx, magc, hbut, mgrn, mred) get their own copy when the font loads%s\r\n", g_cfgFontPlaces == 2 ? " (text sizes by place are set)" : "", FP_NSITE,
        g_cfgLog ? "; \"place:\" lines name the font object of every new string drawn" : "");
    if (g_cfgFontPlaces) fp_config_log();
}
static void tx_uninstall(void) {
    fp_redirect_off();             /* before anything else: the slots live in this DLL */
    if (g_afIn) { tx_restore(TX_AUTOFIT, tx_origFit, 9); g_afIn = 0; }
    if (g_cpIn) { tx_restore(TX_COPYPOS, tx_origCopy, 15); g_cpIn = 0; }
    if (g_dateIn) { tx_restore(TX_DATEMAX, tx_origDate, 8); g_dateIn = 0; }
    if (g_txIn >= 3) tx_restore(TX_LOAD_CALL, tx_origLoad, 5);
    if (g_txIn >= 2) tx_restore(TX_SPACING, tx_origSp, 5);
    if (g_txIn >= 1) tx_restore(TX_ADD_GLYPH, tx_origAdd, 6);
    g_txIn = 0;
}

/* ---- F9 frame capture: every renderer call of one whole frame ---- */
#define TEXT_CALLER 0x0042B0BC
#define MAXCAP   6000
#define MAXGLYPH 40
#define MAXTEXT  1500
typedef struct {
    DWORD caller; int idx; DWORD a0, a1, a2; int n;
    float minx, maxx, miny, maxy, minz, maxz, minw, maxw, u0, v0;
    int text;                       /* index into g_glyph, or -1 */
    const float* vp;                /* vertex pointer, to link UI decisions */
    float ax0, ax1, ay0, ay1;       /* bounds after the UI layout */
    char how, kind;                 /* layout decision, see dump_capture */
} Cap;
static Cap   g_cap[MAXCAP];
static int   g_ncap, g_capturing, g_capReq, g_capDropped;
static float g_glyph[MAXTEXT][MAXGLYPH][4];
static int   g_nglyph[MAXTEXT];
static char  g_gchar[MAXTEXT][MAXGLYPH + 1];     /* decoded characters ('?' unknown), "" if not decoded */
static unsigned char g_gfont[MAXTEXT];           /* font of the first decoded glyph, 255 = none */
static int   g_ntext;

static void cap_call(DWORD caller, int idx, DWORD a0, DWORD a1, DWORD a2) {
    Cap* c;
    if (!g_capturing) return;
    if (g_ncap >= MAXCAP) { g_capDropped++; return; }
    c = &g_cap[g_ncap++];
    memset(c, 0, sizeof(*c));
    c->caller = caller; c->idx = idx; c->a0 = a0; c->a1 = a1; c->a2 = a2; c->text = -1;
}

static void cap_verts(const Pending* p) {
    Cap* c; int k;
    const float* v = p->v;
    if (!g_capturing) return;
    if (g_ncap >= MAXCAP) { g_capDropped++; return; }
    c = &g_cap[g_ncap++];
    memset(c, 0, sizeof(*c));
    c->caller = p->caller; c->idx = p->idx; c->a0 = p->a0; c->a1 = p->a1; c->a2 = p->a2; c->n = p->n; c->text = -1;
    c->vp = p->v; c->how = 0; c->kind = 0;
    c->minx = c->miny = c->minz = c->minw = 1e30f;
    c->maxx = c->maxy = c->maxz = c->maxw = -1e30f;
    for (k = 0; k < p->n; k++) {
        float x = v[k*8], y = v[k*8+1], z = v[k*8+2], w = v[k*8+3];
        if (!(x == x) || !(y == y) || x > 1e6f || x < -1e6f || y > 1e6f || y < -1e6f) continue;
        if (x < c->minx) c->minx = x; if (x > c->maxx) c->maxx = x;
        if (y < c->miny) c->miny = y; if (y > c->maxy) c->maxy = y;
        if (z == z) { if (z < c->minz) c->minz = z; if (z > c->maxz) c->maxz = z; }
        if (w == w) { if (w < c->minw) c->minw = w; if (w > c->maxw) c->maxw = w; }
    }
    c->u0 = v[6]; c->v0 = v[7];
    if (p->caller == TEXT_CALLER && g_ntext < MAXTEXT) {
        int q, nq = p->n / 6;
        c->text = g_ntext;
        if (nq > MAXGLYPH) nq = MAXGLYPH;
        for (q = 0; q < nq; q++) {
            float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
            for (k = q * 6; k < q * 6 + 6; k++) {
                float x = v[k*8], y = v[k*8+1];
                if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
            }
            g_glyph[g_ntext][q][0] = x0; g_glyph[g_ntext][q][1] = y0;
            g_glyph[g_ntext][q][2] = x1; g_glyph[g_ntext][q][3] = y1;
        }
        g_gchar[g_ntext][0] = 0; g_gfont[g_ntext] = 255;
        if (p->td >= 0 && g_tdRing[p->td].n == nq) {
            for (q = 0; q < nq; q++) { char ch = td_ascii(g_tdRing[p->td].ch[q]); g_gchar[g_ntext][q] = ch == '"' ? '\'' : ch; }
            g_gchar[g_ntext][nq] = 0; g_gfont[g_ntext] = g_tdRing[p->td].font[0];
        }
        g_nglyph[g_ntext++] = nq;
    }
}

#define MAXPEND 64
static Pending g_pend[MAXPEND];
static int     g_npend;


/* Draw two opaque black rectangles (x0..x1, x2..x3, full height) through the
   original renderer. bucket/material/arg4 are copied from a HUD tile the game
   itself drew this frame, so the renderer sees an ordinary call; the black
   vertex colour hides the texture (texture x diffuse). */
typedef float* (__cdecl *AddTriFn)(DWORD, DWORD, DWORD, DWORD);
typedef void   (__cdecl *CommitTriFn)(DWORD, DWORD, DWORD);
static void ui_draw_bars(float x0, float x1, float x2, float x3, float h, DWORD bucket, DWORD mat, DWORD a3, float u, float v);
static void ui_draw_quad(DWORD bucket, DWORD mat, DWORD a3, const float* v6);
#include "dinput_mouse.inc"
#define UI_LIVE_CURSOR readable((const void*)EXE_CURSOR, 8)   /* tooltip anchor: the game's cursor this frame */
#include "ui_layout.inc"
#include "screenshot.inc"
static void ui_draw_bars(float x0, float x1, float x2, float x3, float h, DWORD bucket, DWORD mat, DWORD a3, float u, float v) {
    float* p = ((AddTriFn)g_fn[IDX_AddTri])(bucket, mat, 4, a3);
    float r[2][2] = { { x0, x1 }, { x2, x3 } };
    int q, k;
    static const int cx[6] = { 0, 1, 1, 0, 1, 0 }, cy[6] = { 0, 0, 1, 0, 1, 1 };
    if (!p) return;
    for (q = 0; q < 2; q++)
        for (k = 0; k < 6; k++) {
            float* e = p + (q * 6 + k) * 8;
            e[0] = r[q][cx[k]]; e[1] = cy[k] ? h : 0.0f; e[2] = 0.0001f; e[3] = 1.0f;
            ((DWORD*)e)[4] = 0xFF000000; ((DWORD*)e)[5] = 0; e[6] = u; e[7] = v;
        }
    ((CommitTriFn)g_fn[IDX_CommitTri])(bucket, mat, 4);
}

/* one more quad (6 vertices of 8 floats, as the game wrote them) into the game's buffers */
static void ui_draw_quad(DWORD bucket, DWORD mat, DWORD a3, const float* v6) {
    float* p = ((AddTriFn)g_fn[IDX_AddTri])(bucket, mat, 2, a3);
    int k;
    if (!p) return;
    for (k = 0; k < 48; k++) p[k] = v6[k];
    ((CommitTriFn)g_fn[IDX_CommitTri])(bucket, mat, 2);
}

static void process_pending(void) {
    int i, k;
    for (i = 0; i < g_npend; i++) {
        const float* v = g_pend[i].v;
        int n = g_pend[i].n;
        Stat* s = g_pend[i].s;
        if (n <= 0 || n > 100000 || !readable(v, (DWORD)n * 32)) continue;
        cap_verts(&g_pend[i]);
        if ((g_uiOn || g_cfgLog) && g_pend[i].idx == IDX_AddTri) ui_record(&g_pend[i]);
        if (!s) continue;
        s->verts += n;
        for (k = 0; k < n; k++) {
            float x = v[k * 8 + 0], y = v[k * 8 + 1], z = v[k * 8 + 2], w = v[k * 8 + 3];
            if (!(x == x) || !(y == y) || x > 1e6f || x < -1e6f || y > 1e6f || y < -1e6f) continue;
            if (x < s->minx) s->minx = x; if (x > s->maxx) s->maxx = x;
            if (y < s->miny) s->miny = y; if (y > s->maxy) s->maxy = y;
            if (z == z) { if (z < s->minz) s->minz = z; if (z > s->maxz) s->maxz = z; }
            if (w == w) { if (w < s->minw) s->minw = w; if (w > s->maxw) s->maxw = w; }
            if (!s->hasSample && k < 3) { s->sample[k * 2] = x; s->sample[k * 2 + 1] = y; if (k == 2 || k == n - 1) s->hasSample = 1; }
        }
    }
    g_npend = 0;
}

static void dump_modes(void) {
    DWORD r, dev, tab;
    int cnt, i;
    if (!readable((void*)EXE_RENDERER, 4)) return;
    r = *EXE_RENDERER;
    if (!readable((void*)r, 0x10)) return;
    dev = *(DWORD*)(r + 0xC);
    if (!readable((void*)dev, 0xB0)) return;
    cnt = *(int*)(dev + 0x9C);
    tab = *(DWORD*)(dev + 0xAC);
    LOG("--- renderer mode table: %d modes at %08X ---\r\n", cnt, tab);
    if (cnt < 0 || cnt > 400 || !readable((void*)tab, (DWORD)cnt * 0x3C)) return;
    for (i = 0; i < cnt; i++) {
        const unsigned char* m = (const unsigned char*)(tab + i * 0x3C);
        LOG("  mode %2d: %4d x %4d  bpp %2d   raw:", i, *(int*)m, *(int*)(m + 4), m[0x39]);
        { int b; for (b = 8; b < 0x3C; b += 4) LOG(" %08X", *(DWORD*)(m + b)); }
        LOG("\r\n");
    }
    g_modesDumped = 1;
}

static int cmp_stat(const Stat* a, const Stat* b) {
    if (a->idx != b->idx) return a->idx - b->idx;
    return a->caller < b->caller ? -1 : a->caller > b->caller;
}

__attribute__((noinline)) static void dump_window(const char* why) {
    DWORD now = GetTickCount();
    int i, j;
    char f[8][32];
    process_pending();
    LOG("\r\n===== window end (%s)  t=%u ms  frames=%u  total frames=%u  marker=%d =====\r\n",
        why, now - g_windowStart, g_windowFrames, g_frames, g_marker);
    if (readable((void*)EXE_SCREEN_W, 4))
        LOG("exe screen globals: width=%d height=%d\r\n", *EXE_SCREEN_W, *EXE_SCREEN_H);
    if (!g_modesDumped) dump_modes();
    if (g_uiOn) {
        LOG("ui: recorded %u (world %u), flushes %u, quads placed %u (cursor %u), menu frames %u, game frames %u, flags menu %d col %d bar %d\r\n",
            g_dRec, g_dWorld, g_dFlush, g_dMoved, g_dCursor, g_dMenuFrames, g_dGameFrames, g_pMenu, g_pCol, g_pBar);
        if (g_dHave == 2) {
            LOG("ui: first menu tile x before %s %s %s y %s -> after %s %s %s y %s\r\n",
                ftoa(g_dBefore[0], f[0]), ftoa(g_dBefore[1], f[1]), ftoa(g_dBefore[2], f[2]), ftoa(g_dBefore[3], f[3]),
                ftoa(g_dAfter[0], f[4]), ftoa(g_dAfter[1], f[5]), ftoa(g_dAfter[2], f[6]), ftoa(g_dAfter[3], f[7]));
            g_dHave = 0;
        }
        g_dRec = g_dWorld = g_dFlush = g_dMoved = g_dCursor = g_dMenuFrames = g_dGameFrames = 0;
    }

    /* insertion sort, n is small */
    for (i = 1; i < g_nstat; i++) {
        Stat t = g_stat[i];
        j = i - 1;
        while (j >= 0 && cmp_stat(&g_stat[j], &t) > 0) { g_stat[j + 1] = g_stat[j]; j--; }
        g_stat[j + 1] = t;
    }
    LOG("%-26s %-8s %7s %8s  %-23s %-23s %-19s %-19s  args\r\n",
        "function", "caller", "calls", "verts", "x range", "y range", "z range", "rhw range");
    for (i = 0; i < g_nstat; i++) {
        Stat* s = &g_stat[i];
        if (s->verts) {
            LOG("%-26s %08X %7u %8u  %9s..%-9s %9s..%-9s %8s..%-8s %8s..%-8s  %08X %08X\r\n",
                g_names[s->idx], s->caller, s->calls, s->verts,
                ftoa(s->minx, f[0]), ftoa(s->maxx, f[1]), ftoa(s->miny, f[2]), ftoa(s->maxy, f[3]),
                ftoa(s->minz, f[4]), ftoa(s->maxz, f[5]), ftoa(s->minw, f[6]), ftoa(s->maxw, f[7]), s->a0, s->a1);
            if (s->hasSample)
                LOG("      first verts: (%s,%s) (%s,%s) (%s,%s)\r\n",
                    ftoa(s->sample[0], f[0]), ftoa(s->sample[1], f[1]), ftoa(s->sample[2], f[2]),
                    ftoa(s->sample[3], f[3]), ftoa(s->sample[4], f[4]), ftoa(s->sample[5], f[5]));
        } else {
            LOG("%-26s %08X %7u\r\n", g_names[s->idx], s->caller, s->calls);
        }
    }
    flush();
    g_nstat = 0;
    g_windowStart = now;
    g_windowFrames = 0;
}

__attribute__((noinline)) static void dump_capture(void) {
    int i, q;
    char f[12][32];
    float W = (float)*EXE_SCREEN_W, H = (float)*EXE_SCREEN_H;
    float sx = 640.0f / (W > 0 ? W : 640.0f), sy = 480.0f / (H > 0 ? H : 480.0f);
    LOG("\r\n%%%%%%%%%%%%%%%% FRAME CAPTURE %d: %d calls%s, screen %d x %d, fingerprint %08X, %s. Units: screen px, [canvas 640x480] %%%%%%%%%%%%%%%%\r\n",
        g_marker, g_ncap, g_capDropped ? " (TRUNCATED)" : "", *EXE_SCREEN_W, *EXE_SCREEN_H, g_curSig, g_mode == 1 ? "menu" : "game");
    LOG("layout codes: M menu pillarbox, B bottom-center HUD, R right-edge HUD, - untouched, * mixed;  S stretched, G rigid, C cursor, D 3D model\r\n");
    for (i = 0; i < g_ncap; i++) {
        Cap* c = &g_cap[i];
        if (!c->n) {
            LOG("%4d %-22s %08X  %08X %08X %08X\r\n", i, g_names[c->idx], c->caller, c->a0, c->a1, c->a2);
            continue;
        }
        LOG("%4d %-22s %08X  b=%X m=%-4X n=%-5d x %s..%s y %s..%s [%s..%s, %s..%s] z %s..%s rhw %s..%s uv0 %s,%s\r\n",
            i, g_names[c->idx], c->caller, c->a0, c->a1, c->n,
            ftoa(c->minx, f[0]), ftoa(c->maxx, f[1]), ftoa(c->miny, f[2]), ftoa(c->maxy, f[3]),
            ftoa(c->minx * sx, f[4]), ftoa(c->maxx * sx, f[5]), ftoa(c->miny * sy, f[6]), ftoa(c->maxy * sy, f[7]),
            ftoa(c->minz, f[8]), ftoa(c->maxz, f[9]), ftoa(c->minw, f[10]), ftoa(c->maxw, f[11]),
            ftoa(c->u0, f[0]), ftoa(c->v0, f[1]));
        if (c->how)
            LOG("        layout %c%c -> x %s..%s y %s..%s [%s..%s, %s..%s]\r\n", c->how, c->kind,
                ftoa(c->ax0, f[0]), ftoa(c->ax1, f[1]), ftoa(c->ay0, f[2]), ftoa(c->ay1, f[3]),
                ftoa(c->ax0 * sx, f[4]), ftoa(c->ax1 * sx, f[5]), ftoa(c->ay0 * sy, f[6]), ftoa(c->ay1 * sy, f[7]));
        if (c->text >= 0) {
            if (g_gchar[c->text][0])
                LOG("        chars \"%s\" font %s\r\n", g_gchar[c->text], g_gfont[c->text] < TD_NFONT ? td_file[g_gfont[c->text]] : "?");
            LOG("        glyphs:");
            for (q = 0; q < g_nglyph[c->text]; q++) {
                float* g = g_glyph[c->text][q];
                LOG(" (%s,%s %sx%s)", ftoa(g[0], f[0]), ftoa(g[1], f[1]), ftoa(g[2] - g[0], f[2]), ftoa(g[3] - g[1], f[3]));
            }
            LOG("\r\n");
        }
        if ((i & 63) == 63) flush();
    }
    LOG("%%%%%%%%%%%%%%%% END FRAME CAPTURE %d %%%%%%%%%%%%%%%%\r\n\r\n", g_marker);
    flush();
    g_ncap = 0; g_ntext = 0; g_capDropped = 0;
}

/* ---------------------------------------------------------------- hooks */

typedef struct { DWORD ret; int idx; DWORD args[4]; Stat* s; int td; } Frame;
#define MAXDEPTH 32
static Frame g_stack[MAXDEPTH];
static int   g_depth;

extern void post_thunk(void);

/* Debug log: text batches are decoded back into characters from their glyph
   texture rectangles (text_decode.inc). This replaced a stack walk that
   guessed strings from stack values (it mostly found unrelated strings). */
static int g_tdCur = -1;     /* decoded batch of the AddTri being called */

/* frame[0] = function index (replaced with target), frame[1] = return address, frame[2..] = args */
/* ---------------------------------------------------------------- font page and surface probe (HD fonts, debug only) */
#include "font_probe.inc"
#include "font_pages.inc"
#include "font_hd.inc"

void __cdecl hook_pre(DWORD* frame) {
    int idx = (int)frame[0];
    DWORD caller = frame[1];
    const DWORD* a = frame + 2;
    Stat* s;

    if (!g_inited) init();
    frame[0] = (DWORD)g_fn[idx];
    if (!g_cfgLog && !g_uiOn && !g_fpgK) return;       /* nothing to do: pure pass-through */
    if (GetCurrentThreadId() != g_mainThread) return;   /* pass through untouched */
    if (g_fpgK) {                                       /* HD font pages: redraws of the previous upload, argument swaps */
        fpg_tick();
        if (idx == IDX_CreateTexture || idx == IDX_OverWriteTexture) fpg_hook(idx, frame);
    }

    process_pending();
    if (idx == IDX_DestroyAllTextures) g_barHave = 0;    /* remembered HUD material is gone */
    if (g_cfgLog) fp_probe(idx, a);
    if (g_cfgLog && !g_tdLoaded) td_load(g_dir);
    g_tdCur = (g_cfgLog && idx == IDX_AddTri && caller == TEXT_CALLER) ? td_decode((const unsigned char*)frame[-9]) : -1;   /* frame[-9] = the game's edi = glyph batch */
    if (g_uiOn || g_cfgLog) {
        if (idx == IDX_StartScene) ui_frame_start();
        else if (idx == IDX_RenderAllBuffers || idx == IDX_EndFrame || idx == IDX_EndScene) {
            ui_flush();
            if (idx == IDX_EndFrame) ui_conv_end();
        }
    }
    if (!g_cfgLog) {                                    /* release mode: only track AddTri buffers */
        if (g_uiOn && idx == IDX_AddTri && g_depth < MAXDEPTH) {
            Frame* f = &g_stack[g_depth++];
            f->ret = caller; f->idx = idx; f->s = 0; f->td = -1;
            f->args[0] = a[0]; f->args[1] = a[1]; f->args[2] = a[2]; f->args[3] = a[3];
            frame[1] = (DWORD)post_thunk;
        }
        return;
    }
    g_totalCalls[idx]++;

    /* frame capture: starts at the StartScene after F9, ends at EndScene */
    if (idx == IDX_StartScene) shot_frame();           /* the previous frame is on screen now */
    if (idx == IDX_StartScene && g_capReq) { g_capReq = 0; g_capturing = 1; g_ncap = 0; g_ntext = 0; g_capDropped = 0; }
    if (idx == IDX_EndScene && g_capturing) { cap_call(caller, idx, a[0], a[1], a[2]); g_capturing = 0; dump_capture(); shot_capture_done(); }
    if (g_capturing && (!g_post[idx] || vcount(idx, a) == 0)) cap_call(caller, idx, a[0], a[1], a[2]);

    if (g_firstCalls[idx] < 6 || idx == IDX_OpenMode || idx == IDX_SelectDevice || idx == IDX_Initialise || idx == IDX_CloseMode) {
        g_firstCalls[idx]++;
        LOG("call %-24s from %08X  args %08X %08X %08X %08X %08X %08X\r\n",
            g_names[idx], caller, a[0], a[1], a[2], a[3], a[4], a[5]);
        if (idx == IDX_OpenMode && readable((void*)a[0], 0x40)) {
            int b;
            LOG("     OpenMode struct:");
            for (b = 0; b < 0x40; b += 4) LOG(" %08X", *(DWORD*)(a[0] + b));
            LOG("\r\n");
            g_modesDumped = 0;
        }
        flush();
    }

    s = stat_for(idx, caller);
    if (s) { if (!s->calls) { s->a0 = a[0]; s->a1 = a[1]; } s->calls++; }

    if (idx == IDX_FlipScene) { g_frames++; g_windowFrames++; }
    if (idx == IDX_FlipScene || idx == IDX_EndFrame || idx == IDX_EndScene || idx == IDX_StartFrame) {
        int down = (GetAsyncKeyState(0x78 /*F9*/) & 0x8000) != 0;
        if (down && !g_f9down) {
            dump_window("F9 pressed");
            g_marker++;
            LOG("\r\n######## MARKER %d (F9) ########\r\n", g_marker);
            flush();
            g_capReq = 1;
        } else if (GetTickCount() - g_windowStart > 30000) {
            dump_window("timer");
        }
        g_f9down = down;
        shot_poll();
    }

    if (g_post[idx] && g_depth < MAXDEPTH) {
        Frame* f = &g_stack[g_depth++];
        f->ret = caller; f->idx = idx; f->s = s; f->td = g_tdCur;
        f->args[0] = a[0]; f->args[1] = a[1]; f->args[2] = a[2]; f->args[3] = a[3];
        frame[1] = (DWORD)post_thunk;
    }
}

DWORD __cdecl hook_post(DWORD retval) {
    Frame* f = &g_stack[--g_depth];
    int n = vcount(f->idx, f->args);
    if (n > 0 && g_npend < MAXPEND) {
        g_pend[g_npend].v = (const float*)retval;
        g_pend[g_npend].n = n;
        g_pend[g_npend].s = f->s;
        g_pend[g_npend].caller = f->ret; g_pend[g_npend].idx = f->idx;
        g_pend[g_npend].a0 = f->args[0]; g_pend[g_npend].a1 = f->args[1]; g_pend[g_npend].a2 = f->args[2]; g_pend[g_npend].a3 = f->args[3];
        g_pend[g_npend].td = f->td;
        g_npend++;
    }
    return f->ret;
}

__asm__(
    ".intel_syntax noprefix\n"
    ".text\n"
    ".globl _common_stub\n"
    "_common_stub:\n"
    "  pushfd\n"
    "  pushad\n"
    "  lea eax, [esp+36]\n"
    "  push eax\n"
    "  call _hook_pre\n"
    "  add esp, 4\n"
    "  popad\n"
    "  popfd\n"
    "  ret\n"
    ".globl _post_thunk\n"
    "_post_thunk:\n"
    "  push 0\n"
    "  pushfd\n"
    "  pushad\n"
    "  push eax\n"
    "  call _hook_post\n"
    "  add esp, 4\n"
    "  mov [esp+36], eax\n"
    "  popad\n"
    "  popfd\n"
    "  ret\n"
    ".att_syntax prefix\n");

/* ---------------------------------------------------------------- entry */

BOOL WINAPI DllMain(HMODULE h, DWORD reason, void* r) {
    if (reason == 1) { g_self = h; g_windowStart = GetTickCount(); }
    /* FreeLibrary (r == NULL): the exe stays alive, so it must not keep
       calling into this DLL after it's unloaded. At process exit (r != NULL)
       nothing runs any more and memory may already be gone: leave it. */
    if (reason == 0) di_unhook();   /* the vtable lives in dinput.dll: never leave it pointing at this DLL */
    if (reason == 0 && r == 0) { restore_widescreen(); tx_uninstall(); }
    if (reason == 0) { if (g_inited && g_cfgLog) { fp_summary(); dump_window("unload"); LOG("\r\nunloading\r\n"); flush(); if (g_log != (HANDLE)-1) { CloseHandle(g_log); g_log = (HANDLE)-1; } } }
    return 1;
}
