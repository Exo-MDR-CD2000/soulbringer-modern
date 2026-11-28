/* The face name and weight of a TrueType / OpenType font file (2026-11-28), so a
 * custom font in the ini is just its file ([Fonts] Menu=..\MyFont.ttf): GDI is
 * asked for the family it registers for the file (name table, name ID 1,
 * Windows platform, preferably US English) at its weight (OS/2 usWeightClass).
 * For the bundled fonts this gives exactly the faces and weights the DLL used
 * before (Soulbringer Text 300, Soulbringer Uncial 400, MedievalSharp 500,
 * Montserrat ExtraBold 800, Cardo 700).
 *
 * Plain C on memory it is given (shared with the replay harness); the DLL
 * reads the table directory and the two tables from the file.
 */

static unsigned ttf_u16(const unsigned char* p) { return (unsigned)p[0] << 8 | p[1]; }
static unsigned ttf_u32(const unsigned char* p) { return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3]; }

/* the offset and length of table tag (e.g. "name") from the file's first bytes
   (offset table + directory: 12 + 16 x numTables); 0 if not found or not a font */
static int ttf_table(const unsigned char* head, unsigned len, const char* tag, unsigned* off, unsigned* tlen) {
    unsigned v, n, i;
    if (len < 12) return 0;
    v = ttf_u32(head);
    if (v != 0x00010000 && v != 0x4F54544F /* OTTO */ && v != 0x74727565 /* true */) return 0;
    n = ttf_u16(head + 4);
    for (i = 0; i < n && 12 + i * 16 + 16 <= len; i++) {
        const unsigned char* e = head + 12 + i * 16;
        if (e[0] == (unsigned char)tag[0] && e[1] == (unsigned char)tag[1] && e[2] == (unsigned char)tag[2] && e[3] == (unsigned char)tag[3]) {
            *off = ttf_u32(e + 8); *tlen = ttf_u32(e + 12);
            return 1;
        }
    }
    return 0;
}

/* the family name (name ID 1) from the name table, as ASCII (other characters
   become '?'): Windows platform (3), Unicode BMP (1), US English (0x409)
   first, else any Windows Unicode record. Returns its length, 0 if none. */
static int ttf_family(const unsigned char* t, unsigned len, char* out, int cap) {
    unsigned n, strings, i, best = 0xFFFFFFFF;
    int pass, k, m = 0;
    if (len < 6 || cap < 2) return 0;
    n = ttf_u16(t + 2); strings = ttf_u16(t + 4);
    for (pass = 0; pass < 2 && best == 0xFFFFFFFF; pass++)
        for (i = 0; i < n && 6 + i * 12 + 12 <= len; i++) {
            const unsigned char* r = t + 6 + i * 12;
            if (ttf_u16(r) != 3 || ttf_u16(r + 2) != 1 || ttf_u16(r + 6) != 1) continue;
            if (pass == 0 && ttf_u16(r + 4) != 0x409) continue;
            best = i; break;
        }
    if (best == 0xFFFFFFFF) return 0;
    {
        const unsigned char* r = t + 6 + best * 12;
        unsigned slen = ttf_u16(r + 8), soff = strings + ttf_u16(r + 10);
        if (soff + slen > len) return 0;
        for (k = 0; k + 1 < (int)slen && m < cap - 1; k += 2) {
            unsigned c = ttf_u16(t + soff + k);
            out[m++] = (char)(c >= 0x20 && c < 0x7F ? c : '?');
        }
    }
    out[m] = 0;
    return m;
}

/* usWeightClass from the OS/2 table (100 ... 900), 400 if missing or odd */
static int ttf_weight(const unsigned char* os2, unsigned len) {
    unsigned w = len >= 6 ? ttf_u16(os2 + 4) : 0;
    return w >= 100 && w <= 1000 ? (int)w : 400;
}
