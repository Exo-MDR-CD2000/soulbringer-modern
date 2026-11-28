#!/usr/bin/env python3
"""HD fonts step 2: build k-times font pages with phil/hbut redrawn from a
TrueType face, check them, and render preview images of real game lines.

  python3 tools/font_preview.py --game <dir> --out <dir> [--k 2]
          [--face NAME=path.ttf ...] [--scale 1.0] [--edge-hbut 1] ...

<dir> must hold SBM_fontpage_<slot>_256x256x1.raw and SBM_palette.raw (from a
Log=1 session) and data/*.fnt (the game's fonts). Optional backgrounds:
--journal-bg / --tooltip-bg (any image; a stock 1024x768 screenshot crop works).

For each face the glyphs are rendered with FreeType (Pillow), written to
glyphs_<face>.bin and composed by the C code the DLL will use
(src/font_pages.inc, through tests/fonts/test_font_pages.c, compiled here
with cc). The C builder checks that nothing outside phil/hbut changed, that
every glyph has ink and that 'H'/'x'/'n' sit on the baseline.

Sizing: the face's cap height is matched to the original 'H' (times k,
times --scale). Rectangles are the game's: ink that does not fit is
condensed (too wide) or clipped (too tall); clipping is reported per face.

Layout of the preview lines follows the game: each glyph is drawn at its
rectangle's size (k times), pen advance = width + 1 (kerning ignored), line
top + y offset. "today" is the original glyph magnified k times with
bilinear filtering, as the text hook draws it now.
"""
import argparse, os, struct, subprocess, sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
FONTS = ('phil', 'hbut')

JOURNAL = [
    "Drive Off Brigand",
    "The Ferryman Allis Ham has asked me to drive off the brigand near his house.",
    "He has promised me some sort of reward in exchange.",
    "The Ferryman - Allis Ham - told me to follow the road north out of Madrigal",
    "until I come to a small cave at the side of the road.",
]
TOOLTIPS = ["The Eastern River", "Madrigal Inn", "Inventory screen", "Hit Points 100/100",
            "Character screen", "Held Weapon", "Compass", "Gained object: Spade", "Empty."]


def read_fnt(path):
    b = open(path, 'rb').read()
    n = struct.unpack_from('<H', b, 0)[0]
    out = {}
    for r in range(n):
        code, _, mat, u, v, w, h, _x, yo, _y = struct.unpack_from('<HHiiiiiiHH', b, 2 + r * 32)
        out[code] = dict(mat=mat, u=u, v=v, w=w, h=h, yoff=yo)
    return out


def char_of(code):
    return bytes([code]).decode('cp1252', errors='replace') if code < 256 else chr(code)


def size_for_cap(path, cap_px):
    lo, hi = 4.0, 400.0
    for _ in range(40):
        mid = (lo + hi) / 2
        f = ImageFont.truetype(path, size=mid)
        x0, y0, x1, y1 = f.getbbox('H', anchor='ls')
        if -y0 < cap_px:
            lo = mid
        else:
            hi = mid
    return (lo + hi) / 2


# letters and digits whose rectangles decide the size (J and Q tails may be clipped)
CORE = [ord(c) for c in 'ABCDEFGHIKLMNOPRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789']


def overflow(font, fnt, k, codes):
    """largest vertical overflow (px) of the given glyphs out of their rectangles"""
    H = fnt[ord('H')]
    B = H['yoff'] + H['h']
    worst = 0
    for code in codes:
        r = fnt.get(code)
        if not r or not r['mat'] or r['h'] <= 0:
            continue
        x0, y0, x1, y1 = font.getbbox(char_of(code), anchor='ls')
        base = (B - r['yoff']) * k
        worst = max(worst, -y0 - base, y1 - (r['h'] * k - base))
    return worst


def fit_size(face_path, fnt, k, scale, fit):
    H = fnt[ord('H')]
    cap = size_for_cap(face_path, H['h'] * k * scale)
    if fit == 'cap':
        return cap
    # largest size (not above the cap match) whose letters and digits stay inside their rectangles (1 px tolerance)
    lo, hi = cap * 0.5, cap
    if overflow(ImageFont.truetype(face_path, size=hi), fnt, k, CORE) <= 1:
        return hi
    for _ in range(30):
        mid = (lo + hi) / 2
        if overflow(ImageFont.truetype(face_path, size=mid), fnt, k, CORE) <= 1:
            lo = mid
        else:
            hi = mid
    return lo


def render_glyphs(face_path, fnt, k, scale, fit='rects', size=None):
    """-> {code: (cw, ch, top, bytes)}, size, clipped report"""
    H = fnt[ord('H')]
    if size is None:
        size = fit_size(face_path, fnt, k, scale, fit)
    font = ImageFont.truetype(face_path, size=size)
    B = H['yoff'] + H['h']
    out, clipped = {}, []
    for code, r in fnt.items():
        if not r['mat'] or r['w'] <= 0 or r['h'] <= 0 or code < 33:
            continue
        ch = char_of(code)
        if ch == '�':
            continue
        mask, off = font.getmask2(ch, mode='L', anchor='ls')
        w, h = mask.size
        data = bytes(mask)
        # trim empty columns/rows (bearing), keep the baseline reference
        cols = [x for x in range(w) if any(data[y * w + x] for y in range(h))]
        rows = [y for y in range(h) if any(data[y * w:(y + 1) * w])]
        if not cols or not rows:
            continue
        x0, x1, y0, y1 = cols[0], cols[-1] + 1, rows[0], rows[-1] + 1
        cw, chh = x1 - x0, y1 - y0
        bm = bytes(data[(y0 + y) * w + x0 + x] for y in range(chh) for x in range(cw))
        top = -(off[1] + y0)                       # rows above the baseline
        out[code] = (cw, chh, top, bm)
        base = (B - r['yoff']) * k
        over_top = max(0, top - base)
        over_bot = max(0, (chh - top) - (r['h'] * k - base))
        over_w = max(0, cw - r['w'] * k)
        if over_top or over_bot or over_w:
            clipped.append((ch, over_top, over_bot, over_w))
    return out, size, clipped


def write_metrics(path, face_path, fnts, k, lo, hi):
    """own metrics: ink box, origin and advance of every glyph at every em size lo..hi (pixels)"""
    out = struct.pack('<4s3i', b'SBMM', k, lo, hi)
    for fnt in fnts:
        codes = sorted(c for c, r in fnt.items() if r['mat'] and r['w'] > 0 and r['h'] > 0 and c >= 33
                       and char_of(c) != '\ufffd')
        out += struct.pack('<i', len(codes)) + struct.pack('<%di' % len(codes), *codes)
        table = {c: [] for c in codes}
        for em in range(lo, hi + 1):
            f = ImageFont.truetype(face_path, size=em)
            for c in codes:
                ch = char_of(c)
                x0, y0, x1, y1 = f.getbbox(ch, anchor='ls')
                if x1 <= x0 or y1 <= y0:
                    table[c].append((0, 0, 0, 0, 0))
                else:
                    table[c].append((x1 - x0, y1 - y0, x0, -y0, int(round(f.getlength(ch)))))
        for c in codes:
            for v in table[c]:
                out += struct.pack('<5i', *v)
    open(path, 'wb').write(out)


def read_cells(path):
    """cells.txt of a build: name code slot u v w h base yoff -> {name: {code: record}}"""
    fonts = {}
    for line in open(path):
        p = line.split()
        if len(p) != 9:
            continue
        name, code, slot, u, v, w, h, base, yoff = p[0], int(p[1]), int(p[2], 16), *map(int, p[3:])
        fonts.setdefault(name, {})[code] = dict(mat=slot, u=u, v=v, w=w, h=h, yoff=yoff)
    return fonts


def write_glyphs(path, per_font, styles):
    n = sum(len(g) for g in per_font)
    hdr = struct.pack('<4sI', b'SBMG', n)
    hdr += struct.pack('<2I', styles[0]['edge_px'], styles[1]['edge_px'])
    hdr += struct.pack('<2I', styles[0]['edge_alpha'], styles[1]['edge_alpha'])
    hdr += struct.pack('<2I', styles[0].get('fill', 0xFFFFFFFF), styles[1].get('fill', 0xFFFFFFFF))
    hdr += struct.pack('<2I', styles[0].get('edge', 0xFFFFFFFF), styles[1].get('edge', 0xFFFFFFFF))
    body = b''
    for fi, g in enumerate(per_font):
        for code, (cw, ch, top, bm) in sorted(g.items()):
            body += struct.pack('<5I', fi, code, cw, ch, top & 0xFFFFFFFF) + bm
    open(path, 'wb').write(hdr + body)


def compile_builder(tmp):
    exe = os.path.join(tmp, 'test_font_pages')
    subprocess.run(['cc', '-O2', '-o', exe, os.path.join(REPO, 'tests/fonts/test_font_pages.c'),
                    '-I', os.path.join(REPO, 'src')], check=True)
    return exe


def load_pages(d, k):
    side = 256 * k
    pages = {}
    for s in (0x4E, 0x4F, 0x50, 0x51):
        raw = open(os.path.join(d, f'page_{s:02X}.argb'), 'rb').read()
        pages[s] = Image.frombuffer('RGBA', (side, side), raw, 'raw', 'BGRA', 0, 1).copy()
    return pages


def original_pages(game):
    pal = open(os.path.join(game, 'SBM_palette.raw'), 'rb').read()
    pages = {}
    for s in (0x4E, 0x4F, 0x50, 0x51):
        idx = open(os.path.join(game, f'SBM_fontpage_{s:02X}_256x256x1.raw'), 'rb').read()
        px = bytearray(256 * 256 * 4)
        for i, t in enumerate(idx):
            if t:
                px[4 * i:4 * i + 4] = bytes((pal[3 * t], pal[3 * t + 1], pal[3 * t + 2], 255))
        pages[s] = Image.frombuffer('RGBA', (256, 256), bytes(px), 'raw', 'RGBA', 0, 1).copy()
    return pages


def draw_line(canvas, x, y, text, fnt, k, pages, today, spacing=1):
    """today: pages are 1x and magnified bilinearly; else pages are k x."""
    for ch in text:
        code = ch.encode('cp1252', errors='replace')[0]
        r = fnt.get(code)
        if not r:
            continue
        if r['mat'] and r['w'] > 0 and r['h'] > 0:
            if today:
                g = pages[r['mat']].crop((r['u'], r['v'], r['u'] + r['w'], r['v'] + r['h']))
                g = g.resize((r['w'] * k, r['h'] * k), Image.BILINEAR)
            else:
                g = pages[r['mat']].crop((r['u'] * k, r['v'] * k, (r['u'] + r['w']) * k, (r['v'] + r['h']) * k))
            canvas.alpha_composite(g, (x, y + r['yoff'] * k))
        x += (r['w'] + spacing) * k
    return x


def background(path, size, fallback, crop=None):
    """tile an image (stock 1024x768 screenshot crop, magnified 2x like the 4K screen) or a plain colour"""
    if path and os.path.exists(path):
        im = Image.open(path).convert('RGBA')
        if crop:
            im = im.crop(crop)
        im = im.resize((im.width * 2, im.height * 2), Image.BILINEAR)
        out = Image.new('RGBA', size)
        for y in range(0, size[1], im.height):
            for x in range(0, size[0], im.width):
                out.paste(im, (x, y))
        return out
    return Image.new('RGBA', size, fallback)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--k', type=int, default=2)
    ap.add_argument('--scale', type=float, default=1.0, help='cap height relative to the original H (upper limit with --fit rects)')
    ap.add_argument('--fit', choices=('rects', 'cap'), default='rects',
                    help='rects: largest size whose letters and digits fit their rectangles; cap: match the original cap height')
    ap.add_argument('--fill-phil', default='darkest', help='bright | dark | darkest | RRGGBB')
    ap.add_argument('--fill-hbut', default='bright')
    ap.add_argument('--edge-colour-hbut', default='dark')
    ap.add_argument('--face', action='append', default=[], help='NAME=path.ttf (repeatable)')
    ap.add_argument('--edge-phil', type=int, default=0)
    ap.add_argument('--edge-hbut', type=int, default=1)
    ap.add_argument('--edge-alpha', type=int, default=220)
    ap.add_argument('--own-metrics', action='store_true',
                    help='step 5: new, taller cells and the face\'s own widths for phil/hbut (instead of the original rectangles)')
    ap.add_argument('--journal-bg')
    ap.add_argument('--tooltip-bg')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    k = a.k
    fnts = [read_fnt(os.path.join(a.game, 'data', f + '.fnt')) for f in FONTS]
    exe = compile_builder(a.out)
    def colour(v):
        return {'bright': 0xFFFFFFFF, 'dark': 0xFFFFFFFE, 'darkest': 0xFFFFFFFD}.get(v) or int(v, 16)
    styles = [dict(edge_px=a.edge_phil, edge_alpha=a.edge_alpha, fill=colour(a.fill_phil), edge=colour('dark')),
              dict(edge_px=a.edge_hbut, edge_alpha=a.edge_alpha, fill=colour(a.fill_hbut), edge=colour(a.edge_colour_hbut))]
    faces = []
    for spec in a.face:
        name, path = spec.split('=', 1)
        faces.append((name, path))

    orig = original_pages(a.game)
    variants = [('today (text hook, %d%%)' % (k * 100), None, None)]
    ok = True
    for name, path in faces:
        per_font, info = [], []
        pdir = os.path.join(a.out, name)
        os.makedirs(pdir, exist_ok=True)
        sizes = [None, None]
        if a.own_metrics:
            mpath = os.path.join(a.out, f'metrics_{name}.bin')
            write_metrics(mpath, path, fnts, k, 8, 24 * k)
            r = subprocess.run([exe, 'layout', os.path.join(a.game, 'data'), mpath, str(k), pdir], capture_output=True, text=True)
            print(f'== {name}: layout\n  ' + r.stdout.strip().replace('\n', '\n  '))
            ok = ok and r.returncode == 0
            for line in open(os.path.join(pdir, 'newcells.txt')):
                p = line.split()
                if p[0] == 'em':
                    sizes[FONTS.index(p[1])] = int(p[2])
        for fi, fnt in enumerate(fnts):
            g, size, clipped = render_glyphs(path, fnt, k, a.scale, a.fit, sizes[fi])
            per_font.append(g)
            capf = ImageFont.truetype(path, size=size).getbbox('H', anchor='ls')
            info.append(f'{FONTS[fi]} {size:.1f}px (cap height {-capf[1]} px = {-capf[1] / (fnt[ord("H")]["h"] * k):.0%} of the original), '
                        f'{len(g)} glyphs, {len(clipped)} clipped or condensed')
            if clipped:
                worst = sorted(clipped, key=lambda c: -(c[1] + c[2] + c[3]))[:8]
                info.append('   most: ' + ' '.join(f"{c[0]}(t{c[1]} b{c[2]} w{c[3]})" for c in worst))
        gpath = os.path.join(a.out, f'glyphs_{name}.bin')
        write_glyphs(gpath, per_font, styles)
        print(f'== {name} ({os.path.basename(path)})')
        for line in info:
            print('  ' + line)
        cmd = [exe, 'build', a.game, os.path.join(a.game, 'data'), gpath, str(k), pdir]
        if a.own_metrics:
            cmd.append(os.path.join(pdir, 'newcells.txt'))
        r = subprocess.run(cmd, capture_output=True, text=True)
        print('  ' + r.stdout.strip().replace('\n', '\n  '))
        ok = ok and r.returncode == 0
        cells = None
        if a.own_metrics:                       # the records the game would draw with: new cells, and the face's space
            cells = read_cells(os.path.join(pdir, 'cells.txt'))
            for fi, fn in enumerate(FONTS):
                sp = dict(fnts[fi].get(32, {'mat': 0, 'u': 0, 'v': 0, 'w': 4, 'h': 0, 'yoff': 0}))
                sp['w'] = max(1, round(ImageFont.truetype(path, size=sizes[fi]).getlength(' ') / k))
                cells.setdefault(fn, {})[32] = sp
        variants.append((name, load_pages(pdir, k), cells))

    # preview sheets
    lh = 18 * k + 6
    for fi, (fname, lines, bgp, fb) in enumerate([
            ('phil', JOURNAL, a.journal_bg, (226, 222, 176, 255)),
            ('hbut', TOOLTIPS, a.tooltip_bg, (34, 26, 18, 255))]):
        fnt = fnts[fi]
        W = 120 + max(sum((fnt.get(ord(c), {'w': 4})['w'] + 1) * k for c in t) for t in lines)
        block = len(lines) * lh + 30
        H = block * len(variants)
        sheet = background(bgp, (W, H), fb)
        d = ImageDraw.Draw(sheet)
        label_col = (40, 30, 20, 255) if fi == 0 else (230, 220, 190, 255)
        for vi, (vname, pages, cells) in enumerate(variants):
            y = vi * block + 4
            d.text((8, y), vname, fill=label_col)
            y += 22
            vf = dict(fnt)
            if cells and fname in cells:
                vf.update(cells[fname])
            for t in lines:
                draw_line(sheet, 20, y, t, vf, k, orig if pages is None else pages, pages is None, 0 if cells else 1)
                y += lh
        sheet.convert('RGB').save(os.path.join(a.out, f'preview_{fname}.png'))
        print('wrote', os.path.join(a.out, f'preview_{fname}.png'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
