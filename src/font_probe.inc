/* font_probe.inc - debug only (Log=1), read only: the four font texture
 * pages (renderer slots 0x4E-0x51, the font materials).
 *
 * HD fonts research (docs/HD_FONTS_PLAN.md). Nothing here changes the game
 * or the renderer: it reads the renderer's slot records and the DirectDraw
 * surfaces behind them, and writes files and log lines.
 *
 * Page probe (2026-11-22):
 *   - the create descriptor of each font slot (CreateTexture(slot, desc));
 *   - the renderer's slot record at each upload (OverWriteTexture(slot, ?, pixels));
 *   - the first upload of each page as 8-bit indices:
 *     <game>\SBM_fontpage_<slot>_<w>x<h>x<bytes per pixel>.raw
 *
 * Surface probe (step 1 of the HD fonts plan):
 *   - after creation and after every upload (checked at the next renderer
 *     call, when the renderer has finished converting): the surface's
 *     description (size, pitch, pixel format, caps, colour key), and a
 *     checksum of the uploaded source and of the converted surface;
 *   - the first converted surface of each page (top mip level), with a
 *     64-byte header describing its format:
 *     <game>\SBM_fontsurf_<slot>.raw  (read with tools/font_surface.py)
 *   - the game palette the pages are converted with: <game>\SBM_palette.raw
 *     (256 x RGB, from the create descriptor's +0x34);
 *   - once per frame: IsLost() of each font surface, logging changes; after
 *     a lost surface comes back, its checksum is compared with the one
 *     after the last upload (did the renderer restore its content?);
 *   - the format of the other texture slots, one line per distinct
 *     combination (source/surface format codes, bytes per pixel, mips) with
 *     the DirectDraw pixel format of one example;
 *   - every ViewTexture, OsTaskSwitch and SetGlobalPalette call;
 *   - step 1b: the device's texture-format lists (code -> pixel format),
 *     which converters exist per format code, bytes per source pixel.
 *
 * Renderer object [orig + 0x786CC]; slot record [obj + 0x1B54 + 4*slot]:
 *   +0 source width, +4 source height, +8 surface size, +0xC bytes per
 *   source pixel, +0x20/+0x24 source/surface format codes, +0x34 mip levels,
 *   +0x54 IDirectDrawSurface7.
 * The surface is only touched when the record is consistent and the
 * surface's vtable is readable (renderer-build check).
 */

#define FP_FIRST 0x4E
#define FP_N     4

/* IDirectDrawSurface7 vtable byte offsets */
#define DDS7_GetCaps        0x38
#define DDS7_GetColorKey    0x40
#define DDS7_GetSurfaceDesc 0x58
#define DDS7_IsLost         0x60
#define DDS7_Lock           0x64
#define DDS7_Unlock         0x80

typedef long (WINAPI *dds_desc_t)(void*, void*);
typedef long (WINAPI *dds_lost_t)(void*);
typedef long (WINAPI *dds_ckey_t)(void*, DWORD, DWORD*);
typedef long (WINAPI *dds_lock_t)(void*, void*, void*, DWORD, HANDLE);
typedef long (WINAPI *dds_unlock_t)(void*, void*);

/* DDSURFACEDESC2 (0x7C bytes), the fields used here */
#define SD_FLAGS   0x04
#define SD_HEIGHT  0x08
#define SD_WIDTH   0x0C
#define SD_PITCH   0x10
#define SD_MIPS    0x18
#define SD_SURFACE 0x24
#define SD_CKSRC   0x40   /* ddckCKSrcBlt: low, high */
#define SD_PF      0x48   /* DDPIXELFORMAT: size, flags, fourcc, bits, R, G, B, A masks */
#define SD_CAPS    0x68   /* DDSCAPS2: caps, caps2, caps3, caps4 */
#define SD_SIZE    0x7C
#define DDSD_CKSRCBLT 0x10000
#define DDCKEY_SRCBLT 8
#define DDLOCK_WAIT     0x1
#define DDLOCK_READONLY 0x10

static int   g_fpDumped[FP_N];      /* source indices written */
static int   g_fpSurfDumped[FP_N];  /* converted surface written */
static int   g_fpPalDumped;
static DWORD g_fpPal;               /* palette pointer from the create descriptor (+0x34) */
static int   g_fpPending[FP_N];     /* 1 = created, 2 = uploaded: inspect at the next renderer call */
static DWORD g_fpUploads[FP_N];
static DWORD g_fpSrcSum[FP_N];
static DWORD g_fpSurfSum[FP_N];     /* surface checksum after the last upload (0 = unknown) */
static void* g_fpSurf[FP_N];        /* surface seen last */
static int   g_fpLost[FP_N];        /* last IsLost state: 0 ok, 1 lost */
static int   g_fpVerify[FP_N];      /* frames to wait before comparing after a restore */
static int   g_fpOthers;            /* other-slot summaries written */
static DWORD g_fpViews, g_fpSwitches;

static void fp_hex(const char* what, const void* p, int n) {
    char line[200]; int i, o;
    if (!readable(p, (DWORD)n)) { LOG("font page: %s not readable\r\n", what); return; }
    for (o = 0; o < n; o += 32) {
        int k = wsprintfA(line, "font page: %s +%02X:", what, o);
        for (i = o; i < o + 32 && i < n; i += 4) k += wsprintfA(line + k, " %08X", *(const DWORD*)((const unsigned char*)p + i));
        LOG("%s\r\n", line);
    }
}
static const unsigned char* fp_slot(DWORD slot) {
    const unsigned char* obj = readable((const unsigned char*)g_orig + 0x786CC, 4) ? *(const unsigned char* const*)((const unsigned char*)g_orig + 0x786CC) : 0;
    const unsigned char* rec;
    if (!obj || slot > 0x3FF || !readable(obj + 0x1B54 + 4 * slot, 4)) return 0;
    rec = *(const unsigned char* const*)(obj + 0x1B54 + 4 * slot);
    return rec && readable(rec, 0x64) ? rec : 0;
}
/* FNV-1a */
static DWORD fp_sum(DWORD h, const unsigned char* p, DWORD n) {
    if (!h) h = 0x811C9DC5u;
    while (n--) { h ^= *p++; h *= 0x01000193u; }
    return h;
}

/* The record's surface, if the record is consistent and the object looks
   like a DirectDraw surface (readable vtable whose entries are non-null). */
static void* fp_surface(const unsigned char* rec) {
    void* s; const DWORD* vt; int i;
    DWORD w, h, sz, bpp;
    if (!rec) return 0;
    w = *(const DWORD*)rec; h = *(const DWORD*)(rec + 4); sz = *(const DWORD*)(rec + 8); bpp = rec[0xC];
    if (!w || !h || w > 4096 || h > 4096 || !sz || sz > 4096 || !bpp || bpp > 4) return 0;
    s = *(void* const*)(rec + 0x54);
    if (!s || !readable(s, 4)) return 0;
    vt = *(const DWORD* const*)s;
    if (!readable(vt, DDS7_Unlock + 4)) return 0;
    for (i = 0; i <= DDS7_Unlock / 4; i++) if (vt[i] < 0x10000) return 0;
    return s;
}
#define FP_VT(s, off) (((void* const*)*(void* const*)(s))[(off) / 4])

static __attribute__((noinline)) long fp_desc(void* s, unsigned char* d) {
    memset(d, 0, SD_SIZE); *(DWORD*)d = SD_SIZE;
    return ((dds_desc_t)FP_VT(s, DDS7_GetSurfaceDesc))(s, d);
}
static void fp_log_desc(const char* what, const unsigned char* d) {
    const DWORD* pf = (const DWORD*)(d + SD_PF);
    const DWORD* caps = (const DWORD*)(d + SD_CAPS);
    LOG("font surface: %s %ux%u pitch %d mips %u, pixel format flags %08X fourcc %08X %u bits R %08X G %08X B %08X A %08X, caps %08X %08X, flags %08X",
        what, *(const DWORD*)(d + SD_WIDTH), *(const DWORD*)(d + SD_HEIGHT), *(const int*)(d + SD_PITCH), *(const DWORD*)(d + SD_MIPS),
        pf[1], pf[2], pf[3], pf[4], pf[5], pf[6], pf[7], caps[0], caps[1], *(const DWORD*)(d + SD_FLAGS));
    if (*(const DWORD*)(d + SD_FLAGS) & DDSD_CKSRCBLT)
        LOG(", colour key %08X..%08X", *(const DWORD*)(d + SD_CKSRC), *(const DWORD*)(d + SD_CKSRC + 4));
    LOG("\r\n");
}

/* Lock read-only, checksum (and optionally dump) the top level, unlock.
   Returns 1 if locked. */
static __attribute__((noinline)) int fp_read_surface(int k, void* s, DWORD* sum, int dump, DWORD ckFlags, DWORD ckLow, DWORD ckHigh) {
    unsigned char d[SD_SIZE];
    long hr; DWORD w, h, bits, row, y, got;
    int pitch; const unsigned char* p;
    memset(d, 0, SD_SIZE); *(DWORD*)d = SD_SIZE;
    hr = ((dds_lock_t)FP_VT(s, DDS7_Lock))(s, 0, d, DDLOCK_WAIT | DDLOCK_READONLY, 0);
    if (hr != 0) { LOG("font surface: slot %02X lock failed %08X\r\n", FP_FIRST + k, (DWORD)hr); return 0; }
    w = *(DWORD*)(d + SD_WIDTH); h = *(DWORD*)(d + SD_HEIGHT); pitch = *(int*)(d + SD_PITCH);
    bits = ((DWORD*)(d + SD_PF))[3]; p = *(const unsigned char**)(d + SD_SURFACE);
    row = w * bits / 8;
    *sum = 0;
    if (p && w && h && w <= 4096 && h <= 4096 && bits >= 8 && bits <= 32 && pitch >= (int)row && readable(p, (DWORD)pitch * (h - 1) + row)) {
        for (y = 0; y < h; y++) *sum = fp_sum(*sum, p + (DWORD)pitch * y, row);
        if (dump) {
            char path[700]; HANDLE f; DWORD hdr[16];
            const DWORD* pf = (const DWORD*)(d + SD_PF);
            memset(hdr, 0, sizeof hdr);
            hdr[0] = 0x534D4253;  /* "SBMS" */
            hdr[1] = 1;           /* header version */
            hdr[2] = FP_FIRST + k; hdr[3] = w; hdr[4] = h; hdr[5] = row; hdr[6] = bits;
            hdr[7] = pf[1]; hdr[8] = pf[4]; hdr[9] = pf[5]; hdr[10] = pf[6]; hdr[11] = pf[7];
            hdr[12] = ckFlags; hdr[13] = ckLow; hdr[14] = ckHigh; hdr[15] = g_fpUploads[k];
            wsprintfA(path, "%sSBM_fontsurf_%02X.raw", g_dir, FP_FIRST + k);
            f = CreateFileA(path, 0x40000000 /*GENERIC_WRITE*/, 0, 0, 2 /*CREATE_ALWAYS*/, 0x80, 0);
            got = 0;
            if (f != (HANDLE)-1) {
                DWORD t; WriteFile(f, hdr, sizeof hdr, &got, 0);
                for (y = 0; y < h; y++) { WriteFile(f, p + (DWORD)pitch * y, row, &t, 0); got += t; }
                CloseHandle(f);
            }
            LOG("font surface: slot %02X wrote %u bytes (%ux%u, %u bits, pitch %d) to %s\r\n", FP_FIRST + k, got, w, h, bits, pitch, path);
        }
    } else LOG("font surface: slot %02X locked but not read (surface %08X %ux%u pitch %d, %u bits)\r\n", FP_FIRST + k, (DWORD)p, w, h, pitch, bits);
    ((dds_unlock_t)FP_VT(s, DDS7_Unlock))(s, 0);
    return 1;
}

/* After creation (why = 1) or an upload (why = 2): describe the surface. */
static __attribute__((noinline)) void fp_inspect(int k, int why) {
    const unsigned char* rec = fp_slot(FP_FIRST + k);
    void* s = fp_surface(rec);
    unsigned char d[SD_SIZE];
    char what[48];
    DWORD key[2] = { 0, 0 }, ckf = 0, sum = 0;
    long hr;
    if (!rec) { LOG("font surface: slot %02X no record\r\n", FP_FIRST + k); return; }
    if (why == 1) { char w[40]; wsprintfA(w, "slot %02X record after create", FP_FIRST + k); fp_hex(w, rec, 0x64); }
    if (!s) { LOG("font surface: slot %02X no usable surface (record +54 = %08X) - not inspected\r\n", FP_FIRST + k, *(const DWORD*)(rec + 0x54)); return; }
    if (s != g_fpSurf[k]) { if (g_fpSurf[k]) LOG("font surface: slot %02X surface changed %08X -> %08X\r\n", FP_FIRST + k, (DWORD)g_fpSurf[k], (DWORD)s); g_fpSurf[k] = s; }
    hr = fp_desc(s, d);
    wsprintfA(what, "slot %02X %s (surface %08X):", FP_FIRST + k, why == 1 ? "created" : "uploaded", (DWORD)s);
    if (hr == 0) fp_log_desc(what, d); else LOG("font surface: %s GetSurfaceDesc failed %08X\r\n", what, (DWORD)hr);
    hr = ((dds_ckey_t)FP_VT(s, DDS7_GetColorKey))(s, DDCKEY_SRCBLT, key);
    if (hr == 0) { ckf = 1; LOG("font surface: slot %02X source colour key %08X..%08X\r\n", FP_FIRST + k, key[0], key[1]); }
    else if (why == 1) LOG("font surface: slot %02X no source colour key (%08X)\r\n", FP_FIRST + k, (DWORD)hr);
    if (why == 2) {
        int dump = !g_fpSurfDumped[k];
        if (fp_read_surface(k, s, &sum, dump, ckf, key[0], key[1])) {
            LOG("font surface: slot %02X upload %u: source sum %08X, surface sum %08X%s\r\n", FP_FIRST + k, g_fpUploads[k], g_fpSrcSum[k], sum,
                g_fpSurfSum[k] && g_fpSurfSum[k] != sum ? " (surface differs from the previous upload)" : g_fpSurfSum[k] ? " (same as before)" : "");
            g_fpSurfSum[k] = sum;
            if (dump) g_fpSurfDumped[k] = 1;
        }
    }
    flush();
}

/* The other slots: one line per distinct format combination. */
static __attribute__((noinline)) void fp_others(const char* when) {
    DWORD key[24]; int cnt[24], first[24], ng = 0, slot, i, total = 0;
    unsigned char d[SD_SIZE];
    for (slot = 0; slot < 0x400; slot++) {
        const unsigned char* rec = fp_slot(slot);
        DWORD w, h, kk;
        if (!rec) continue;
        w = *(const DWORD*)rec; h = *(const DWORD*)(rec + 4);
        if (!w || !h || w > 4096 || h > 4096) continue;
        total++;
        kk = (rec[0x20] << 24) | (rec[0x24] << 16) | (rec[0xC] << 8) | (rec[0x34] & 0xFF);
        for (i = 0; i < ng && key[i] != kk; i++) ;
        if (i == ng) { if (ng == 24) continue; key[ng] = kk; cnt[ng] = 0; first[ng] = slot; ng++; }
        cnt[i]++;
    }
    LOG("font surface: texture slots (%s): %d in use, %d format combinations\r\n", when, total, ng);
    for (i = 0; i < ng; i++) {
        const unsigned char* rec = fp_slot(first[i]);
        void* s = fp_surface(rec);
        LOG("font surface:   formats %u -> %u, %u bytes per source pixel, %u mips: %d slots, e.g. %02X (%ux%u, surface %u)\r\n",
            key[i] >> 24, (key[i] >> 16) & 0xFF, (key[i] >> 8) & 0xFF, key[i] & 0xFF, cnt[i], first[i],
            *(const DWORD*)rec, *(const DWORD*)(rec + 4), *(const DWORD*)(rec + 8));
        if (s && fp_desc(s, d) == 0) { char w[40]; wsprintfA(w, "  e.g. slot %02X", first[i]); fp_log_desc(w, d); }
    }
    flush();
}

/* Step 1b: the renderer's texture-format lists and converter tables.
 * CreateTexture looks the requested surface code (desc+0x10) up in the
 * device's list at [orig+0x785E4] + 0x3D0C (0x28-byte entries, code at +0x24,
 * count at +0x44DC); a second list of the same shape is at +0x3538 (count
 * +0x3D08). Conversions use [obj + 0x102C + 4*bit] (source decoders) and
 * [obj + 0x10AC + 4*bit] (surface encoders); bytes per source pixel per code
 * bit are at orig+0x2060C. Read only. */
static void fp_list(const unsigned char* dev, DWORD first, DWORD countOff, const char* name) {
    DWORD n, i;
    if (!readable(dev + countOff, 4)) { LOG("format list: %s count not readable\r\n", name); return; }
    n = *(const DWORD*)(dev + countOff);
    LOG("format list: %s at device+%X: %u entries\r\n", name, first, n);
    if (n > 50) { LOG("format list: %s count %u too large - not read\r\n", name, n); return; }
    for (i = 0; i < n; i++) {
        const DWORD* e = (const DWORD*)(dev + first + i * 0x28);
        if (!readable(e, 0x28)) { LOG("format list: %s entry %u not readable\r\n", name, i); return; }
        LOG("format list: %s %2u: code %08X | %08X %08X %08X %2u bits R %08X G %08X B %08X A %08X | +20 %08X\r\n",
            name, i, e[9], e[0], e[1], e[2], e[3], e[4], e[5], e[6], e[7], e[8]);
    }
}
static __attribute__((noinline)) void fp_formats(void) {
    const unsigned char* o = (const unsigned char*)g_orig;
    const unsigned char* dev = readable(o + 0x785E4, 4) ? *(const unsigned char* const*)(o + 0x785E4) : 0;
    const unsigned char* obj = readable(o + 0x786CC, 4) ? *(const unsigned char* const*)(o + 0x786CC) : 0;
    const unsigned char* rec = fp_slot(FP_FIRST);
    int b;
    LOG("format list: device %08X, renderer object %08X\r\n", (DWORD)dev, (DWORD)obj);
    if (dev && readable(dev, 0x44E0)) {
        fp_list(dev, 0x3D0C, 0x44DC, "texture");
        fp_list(dev, 0x3538, 0x3D08, "other");
    } else LOG("format list: device not readable\r\n");
    if (rec) {
        DWORD e = *(const DWORD*)(rec + 0x50);
        LOG("format list: font slot %02X uses entry %08X (texture list index %d)\r\n", FP_FIRST, e,
            dev && e >= (DWORD)(dev + 0x3D0C) && e < (DWORD)(dev + 0x44DC) ? (int)((e - (DWORD)(dev + 0x3D0C)) / 0x28) : -1);
    }
    if (obj && readable(obj + 0x102C, 0x100)) {
        LOG("format list: converters by code bit (src/dst):");
        for (b = 0; b < 32; b++) {
            DWORD s = *(const DWORD*)(obj + 0x102C + 4 * b), d = *(const DWORD*)(obj + 0x10AC + 4 * b);
            if (s || d) LOG(" %d=%c%c", b, s ? 'S' : '-', d ? 'D' : '-');
        }
        LOG("\r\n");
    }
    if (readable(o + 0x2060C, 32)) {
        LOG("format list: bytes per source pixel by code bit:");
        for (b = 0; b < 32; b++) LOG(" %u", o[0x2060C + b]);
        LOG("\r\n");
    }
    flush();
}

/* Once per frame: IsLost() of each font surface; after a restore, compare
   the content with the last upload. */
static __attribute__((noinline)) void fp_frame(void) {
    int k;
    for (k = 0; k < FP_N; k++) {
        const unsigned char* rec; void* s; int lost;
        if (!g_fpSurf[k]) continue;
        rec = fp_slot(FP_FIRST + k);
        s = fp_surface(rec);
        if (!s) continue;
        if (s != g_fpSurf[k]) { LOG("font surface: slot %02X surface changed %08X -> %08X (no upload yet)\r\n", FP_FIRST + k, (DWORD)g_fpSurf[k], (DWORD)s); g_fpSurf[k] = s; }
        lost = ((dds_lost_t)FP_VT(s, DDS7_IsLost))(s) != 0;
        if (lost != g_fpLost[k]) {
            LOG("font surface: slot %02X %s (frame %u, uploads so far %u)\r\n", FP_FIRST + k, lost ? "LOST" : "restored", g_frames, g_fpUploads[k]);
            g_fpLost[k] = lost;
            if (!lost) g_fpVerify[k] = 2;   /* give the renderer a frame to restore it */
            flush();
        }
        if (!lost && g_fpVerify[k] && --g_fpVerify[k] == 0) {
            DWORD sum = 0;
            if (fp_read_surface(k, s, &sum, 0, 0, 0, 0))
                LOG("font surface: slot %02X after restore: surface sum %08X, %s\r\n", FP_FIRST + k, sum,
                    !g_fpSurfSum[k] ? "no earlier sum" : sum == g_fpSurfSum[k] ? "content kept" : "CONTENT DIFFERS from the last upload");
            flush();
        }
    }
}

static void fp_write_palette(void) {
    char path[700]; HANDLE h; DWORD got = 0;
    if (g_fpPalDumped || !g_fpPal || !readable((const void*)g_fpPal, 768)) return;
    wsprintfA(path, "%sSBM_palette.raw", g_dir);
    h = CreateFileA(path, 0x40000000 /*GENERIC_WRITE*/, 0, 0, 2 /*CREATE_ALWAYS*/, 0x80, 0);
    if (h != (HANDLE)-1) { WriteFile(h, (const void*)g_fpPal, 768, &got, 0); CloseHandle(h); }
    LOG("font page: palette %08X (index 0 = %02X %02X %02X): wrote %u bytes to %s\r\n", g_fpPal,
        ((const unsigned char*)g_fpPal)[0], ((const unsigned char*)g_fpPal)[1], ((const unsigned char*)g_fpPal)[2], got, path);
    g_fpPalDumped = 1;
}

/* Called from hook_pre with Log=1, before the call is forwarded. */
static __attribute__((noinline)) void fp_probe(int idx, const DWORD* a) {
    int k;
    /* the previous call has returned: inspect what it created or uploaded */
    for (k = 0; k < FP_N; k++) if (g_fpPending[k]) { int why = g_fpPending[k]; g_fpPending[k] = 0; fp_inspect(k, why); }

    if (idx == IDX_StartScene) fp_frame();
    if (idx == IDX_EndTextureCreation && g_fpSurf[0] && g_fpOthers < 3) { if (!g_fpOthers) fp_formats(); g_fpOthers++; fp_others("at EndTextureCreation"); }
    if (idx == IDX_ViewTexture && g_fpViews++ < 50) { LOG("font surface: ViewTexture args %08X %08X %08X %08X\r\n", a[0], a[1], a[2], a[3]); flush(); }
    if (idx == IDX_OsTaskSwitch && g_fpSwitches++ < 50) { LOG("font surface: OsTaskSwitch args %08X %08X (frame %u)\r\n", a[0], a[1], g_frames); flush(); }
    if (idx == IDX_SetGlobalPalette) {
        LOG("font surface: SetGlobalPalette args %08X %08X %08X", a[0], a[1], a[2]);
        if (readable((const void*)a[0], 6)) LOG(" (first entries %02X%02X%02X %02X%02X%02X)", ((const unsigned char*)a[0])[0], ((const unsigned char*)a[0])[1], ((const unsigned char*)a[0])[2], ((const unsigned char*)a[0])[3], ((const unsigned char*)a[0])[4], ((const unsigned char*)a[0])[5]);
        LOG("\r\n");
    }

    if (idx == IDX_CreateTexture && a[0] >= FP_FIRST && a[0] < FP_FIRST + FP_N) {
        char w[40]; wsprintfA(w, "create slot %02X desc", a[0]);
        fp_hex(w, (const void*)a[1], 0x40);
        if (readable((const void*)a[1], 0x38)) g_fpPal = *(const DWORD*)(a[1] + 0x34);
        g_fpPending[a[0] - FP_FIRST] = 1;
        g_fpSurfSum[a[0] - FP_FIRST] = 0;
    }
    if (idx == IDX_OverWriteTexture && a[0] >= FP_FIRST && a[0] < FP_FIRST + FP_N) {
        int k2 = a[0] - FP_FIRST;
        const unsigned char* rec = fp_slot(a[0]);
        g_fpUploads[k2]++;
        LOG("font page: upload %u of slot %02X args %08X %08X %08X from the game\r\n", g_fpUploads[k2], a[0], a[1], a[2], a[3]);
        if (rec && g_fpUploads[k2] == 1) { char w[40]; wsprintfA(w, "slot %02X record", a[0]); fp_hex(w, rec, 0x64); }
        g_fpSrcSum[k2] = 0;
        if (rec) {
            DWORD sw = *(const DWORD*)rec, sh = *(const DWORD*)(rec + 4), bpp = rec[0xC], n = sw * sh * bpp, got = 0;
            if (sw && sh && bpp && sw <= 4096 && sh <= 4096 && bpp <= 4 && readable((const void*)a[2], n)) {
                g_fpSrcSum[k2] = fp_sum(0, (const unsigned char*)a[2], n);
                if (!g_fpDumped[k2]) {
                    char path[700]; HANDLE h;
                    wsprintfA(path, "%sSBM_fontpage_%02X_%ux%ux%u.raw", g_dir, a[0], sw, sh, bpp);
                    h = CreateFileA(path, 0x40000000 /*GENERIC_WRITE*/, 0, 0, 2 /*CREATE_ALWAYS*/, 0x80, 0);
                    if (h != (HANDLE)-1) { WriteFile(h, (const void*)a[2], n, &got, 0); CloseHandle(h); }
                    LOG("font page: slot %02X source %ux%u, %u bytes per pixel: wrote %u bytes to %s\r\n", a[0], sw, sh, bpp, got, path);
                    g_fpDumped[k2] = 1;
                }
            } else LOG("font page: slot %02X source %ux%ux%u not read (pixels %08X readable %d)\r\n", a[0], sw, sh, bpp, a[2], readable((const void*)a[2], 16));
        }
        fp_write_palette();
        g_fpPending[k2] = 2;
        flush();
    }
}

/* Totals for the end of the log. */
static void fp_summary(void) {
    int k;
    LOG("font surface: summary: uploads");
    for (k = 0; k < FP_N; k++) LOG(" %02X=%u", FP_FIRST + k, g_fpUploads[k]);
    LOG(", ViewTexture calls %u, OsTaskSwitch calls %u\r\n", g_fpViews, g_fpSwitches);
}
