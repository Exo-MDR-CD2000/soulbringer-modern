#!/usr/bin/env python3
"""Menu font (magc, [Fonts] Menu) offline check and preview.

1. Writes own-metrics tables ("SBM3") for magc from the menu face (and, with
   --with-journal, phil/hbut from Cardo, as [Fonts] Tooltips/Journal=Cardo adds them),
   runs `test_font_pages layout` (the DLL's em choice, new cells and packing)
   and fails if a cell does not fit or touches another font.
2. Draws menu words the way the game would: each letter from its new cell
   (advance = cell width, letter spacing 0, kerning 0), yellow with a soft
   dark edge, next to the original magc letters enlarged k times.

  python3 tools/menu_font_preview.py --game ~/game --face dist/fonts/EagleLake-Regular.ttf --out ~/menuprev
"""
import argparse, os, struct, subprocess, sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from font_preview import read_fnt, char_of  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
WORDS = ["New Game", "Load Game", "Save Game", "Options", "Credits", "Quit", "Return to Game",
         "Soulbringer  (c) 2000 Gremlin", "Level 3  The Eastern River", "Level 7  26/11/1995  14:35"]
FILES = {'phil': 8, 'hbut': 9, 'magc': 10, 'mgrn': 11, 'mred': 12}


def metrics_for(face_path, fnt, k, lo, hi):
    codes = sorted(c for c, r in fnt.items() if r['mat'] and r['w'] > 0 and r['h'] > 0 and c >= 33
                   and char_of(c) != '�')
    out = struct.pack('<i', len(codes)) + struct.pack('<%di' % len(codes), *codes)
    table = {c: [] for c in codes}
    for em in range(lo, hi + 1):
        f = ImageFont.truetype(face_path, size=em)
        for c in codes:
            ch = char_of(c)
            x0, y0, x1, y1 = f.getbbox(ch, anchor='ls')
            table[c].append((0, 0, 0, 0, 0) if x1 <= x0 or y1 <= y0 else
                            (x1 - x0, y1 - y0, x0, -y0, int(round(f.getlength(ch)))))
    for c in codes:
        for v in table[c]:
            out += struct.pack('<5i', *v)
    return out


def write_sbm3(path, fonts, k, lo, hi):
    """fonts: [(name, face_path, fnt)]"""
    out = struct.pack('<4s4i', b'SBM3', k, lo, hi, len(fonts))
    for name, face, fnt in fonts:
        out += struct.pack('<i', FILES[name]) + metrics_for(face, fnt, k, lo, hi)
    open(path, 'wb').write(out)


def read_newcells(path):
    em, cells = {}, {}
    for line in open(path):
        p = line.split()
        if p[0] == 'em':
            em[p[1]] = tuple(map(int, p[2:5]))
        elif p[0] == 'cell':
            v = list(map(int, [p[2]] + [int(p[3], 16)] + p[4:11]))
            cells.setdefault(p[1], {})[v[0]] = dict(slot=v[1], u=v[2], v=v[3], w=v[4], h=v[5], yoff=v[6], left=v[7], base=v[8])
    return em, cells


def styled(cov, fill, edge, edge_px, edge_alpha):
    """yellow letter over a soft dark edge (fpg_glyph: fill over the dilated coverage)"""
    a = cov
    ring = cov.filter(ImageFilter.MaxFilter(2 * edge_px + 1)) if edge_px > 0 else Image.new('L', cov.size, 0)
    ring = ring.point(lambda v: v * edge_alpha // 255)
    im = Image.new('RGBA', cov.size, edge + (0,))
    im.putalpha(ring)
    top = Image.new('RGBA', cov.size, fill + (0,))
    top.putalpha(a)
    return Image.alpha_composite(im, top)


def draw_new(text, face, em, cells, L, k, fill, edge):
    font = ImageFont.truetype(face, size=em[0])
    space = int(round(font.getlength(' ') / k))
    W = sum(cells[ord(ch)]['w'] if ord(ch) in cells else space for ch in text) * k + 8
    canvas = Image.new('RGBA', (W, L * k + 8), (0, 0, 0, 0))
    x = 4
    for ch in text:
        c = cells.get(ord(ch))
        if not c:
            x += space * k
            continue
        cw, chh = c['w'] * k, c['h'] * k
        cov = Image.new('L', (cw, chh), 0)
        d = ImageDraw.Draw(cov)
        x0, y0, x1, y1 = font.getbbox(ch, anchor='ls')
        # ink starts c['left'] px from the cell's left; baseline c['base'] units from its top
        d.text((c['left'] - x0, c['base'] * k), ch, font=font, fill=255, anchor='ls')
        g = styled(cov, fill, edge, max(1, k // 2), 200)
        canvas.alpha_composite(g, (x, 4 + c['yoff'] * k))
        x += cw
    return canvas


def draw_orig(text, fnt, pages, pal, k):
    W = sum(fnt[ord(ch)]['w'] if ord(ch) in fnt else 4 for ch in text) + 4
    L = max(r['yoff'] + r['h'] for r in fnt.values())
    im = Image.new('RGBA', (W, L + 2), (0, 0, 0, 0))
    x = 2
    for ch in text:
        r = fnt.get(ord(ch))
        if not r or not r['mat']:
            x += r['w'] if r else 4
            continue
        pg = pages[r['mat']]
        for yy in range(r['h']):
            for xx in range(r['w']):
                i = pg[(r['v'] + yy) * 256 + r['u'] + xx]
                if i:
                    im.putpixel((x + xx, r['yoff'] + yy), tuple(pal[3 * i:3 * i + 3]) + (255,))
        x += r['w']
    return im.resize((im.width * k, im.height * k), Image.NEAREST)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', required=True)
    ap.add_argument('--face', action='append', required=True, help='menu face (repeat for several rows)')
    ap.add_argument('--out', required=True)
    ap.add_argument('--k', type=int, default=4)
    ap.add_argument('--with-journal', action='store_true', help='pack phil/hbut (Cardo) too, as [Fonts] Tooltips/Journal=Cardo does')
    ap.add_argument('--name-face', help='pack mgrn/mred too, from this face, as [Fonts] Names does')
    ap.add_argument('--journal-face', help='with --with-journal: phil from this face instead of Cardo, as [Fonts] Journal=Text does')
    ap.add_argument('--tip-face', help='with --with-journal: hbut from this face instead of Cardo, as [Fonts] Tooltips=Text does')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    data = os.path.join(a.game, 'data')
    magc = read_fnt(os.path.join(data, 'magc.fnt'))
    exe = '/tmp/sbm_test_font_pages'
    subprocess.run(['cc', '-O2', '-o', exe, os.path.join(ROOT, 'tests/fonts/test_font_pages.c'), '-I' + os.path.join(ROOT, 'src'), '-lm'], check=True)
    pal = open(os.path.join(a.game, 'SBM_palette.raw'), 'rb').read()
    pages = {s: open(os.path.join(a.game, 'SBM_fontpage_%02X_256x256x1.raw' % s), 'rb').read() for s in (0x4E, 0x4F, 0x50, 0x51)}
    cardo = subprocess.run(['fc-match', '-f', '%{file}', 'Cardo:bold'], capture_output=True, text=True).stdout
    rows, ok = [], True
    for face in a.face:
        name = os.path.splitext(os.path.basename(face))[0]
        fonts = [('magc', face, magc)]
        if a.with_journal:
            fonts = [('phil', a.journal_face or cardo, read_fnt(os.path.join(data, 'phil.fnt'))), ('hbut', a.tip_face or cardo, read_fnt(os.path.join(data, 'hbut.fnt')))] + fonts
        if a.name_face:
            fonts += [('mgrn', a.name_face, read_fnt(os.path.join(data, 'mgrn.fnt'))), ('mred', a.name_face, read_fnt(os.path.join(data, 'mred.fnt')))]
        pdir = os.path.join(a.out, name)
        os.makedirs(pdir, exist_ok=True)
        mpath = os.path.join(pdir, 'metrics.bin')
        write_sbm3(mpath, fonts, a.k, 8, 24 * a.k)
        r = subprocess.run([exe, 'layout', data, mpath, str(a.k), pdir], capture_output=True, text=True)
        print(f'== {name}\n  ' + r.stdout.strip().replace('\n', '\n  '))
        ok = ok and r.returncode == 0
        em, cells = read_newcells(os.path.join(pdir, 'newcells.txt'))
        rows.append((name, [draw_new(w, face, em['magc'], cells['magc'], em['magc'][2], a.k, (228, 228, 40), (36, 24, 8)) for w in WORDS]))
    rows.insert(0, ('original magc', [draw_orig(w, magc, pages, pal, a.k) for w in WORDS]))
    # sheet: dark menu-like background
    lab = ImageFont.truetype(subprocess.run(['fc-match', '-f', '%{file}', 'DejaVu Sans'], capture_output=True, text=True).stdout, 20)
    colw = [max(r[1][i].width for r in rows) + 40 for i in range(len(WORDS))]
    per = 3
    blocks = [list(range(i, min(i + per, len(WORDS)))) for i in range(0, len(WORDS), per)]
    rh = max(im.height for _, ims in rows for im in ims) + 16
    W = 260 + max(sum(colw[i] for i in b) for b in blocks)
    H = len(blocks) * (len(rows) * rh + 20)
    sheet = Image.new('RGBA', (W, H), (26, 20, 14, 255))
    d = ImageDraw.Draw(sheet)
    y = 10
    for b in blocks:
        for name, ims in rows:
            d.text((10, y + rh // 2 - 10), name, fill=(200, 200, 200), font=lab)
            x = 260
            for i in b:
                sheet.alpha_composite(ims[i], (x, y + (rh - ims[i].height) // 2))
                x += colw[i]
            y += rh
        y += 20
    out = os.path.join(a.out, 'menu_font_preview.png')
    sheet.convert('RGB').save(out)
    print('preview:', out)
    print('MENU LAYOUT PASSED' if ok else 'MENU LAYOUT FAILED')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
