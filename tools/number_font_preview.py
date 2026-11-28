#!/usr/bin/env python3
"""Combat numbers ([Fonts] Numbers): numr / numg redrawn from Montserrat ExtraBold.

Renders the 13 glyphs with FreeType at the em that fpg_num_em picks (same rule,
'8' = (8 - 2) texels x k tall), runs `test_font_pages numbers` (the real
font_pages.inc code: colours from the pages, baseline, fpg_glyph with a
one-texel outline, nothing outside the two fonts' rectangles touched) and
writes a preview: the original digits and the redrawn ones at 250 / 400 / 600 %.

  python3 tools/number_font_preview.py --game <folder with data\\*.fnt and the
      SBM_fontpage / SBM_palette dumps> --out <dir> [--k 4]
"""
import argparse, os, struct, subprocess, sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from font_preview import read_fnt  # noqa: E402

FACE = os.path.join(ROOT, 'dist', 'fonts', 'Montserrat-ExtraBold.ttf')
CODES = '+-0123456789='


def top8(em):
    if em <= 0:
        return 0
    bb = ImageFont.truetype(FACE, em).getbbox('8', anchor='ls')
    return -bb[1]


def num_em(h8, k, tops):
    """fpg_num_em (font_pages.inc), step for step"""
    cap = (h8 - 2) * k
    t = tops[100]
    em = (cap * 100 + t // 2) // t
    for _ in range(6):
        if em <= 4:
            break
        t = tops[em]
        if t > cap:
            em -= 1
        elif t < cap - 1:
            em += 1
        else:
            break
    return em


def coverage(font, ch):
    bb = font.getbbox(ch, anchor='ls')
    w, h = bb[2] - bb[0], bb[3] - bb[1]
    im = Image.new('L', (w, h), 0)
    ImageDraw.Draw(im).text((-bb[0], -bb[1]), ch, font=font, fill=255, anchor='ls')
    return im, -bb[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--k', type=int, default=4)
    a = ap.parse_args()
    k = a.k
    os.makedirs(a.out, exist_ok=True)
    data = os.path.join(a.game, 'data')
    fnt = {n: read_fnt(os.path.join(data, n + '.fnt')) for n in ('numr', 'numg')}
    tops = [top8(e) for e in range(301)]
    em = num_em(fnt['numr'][ord('8')]['h'], k, tops)
    font = ImageFont.truetype(FACE, em)
    entries = []
    for f in range(2):
        for ch in CODES:
            im, top = coverage(font, '\u2013' if ch == '-' else ch)   # fpg_num_code: '-' drawn as the en dash
            entries.append(struct.pack('<5I', f, ord(ch), im.width, im.height, top) + im.tobytes())
    gpath = os.path.join(a.out, 'numbers_glyphs.bin')
    with open(gpath, 'wb') as o:
        o.write(b'SBMN' + struct.pack('<2I', em, len(entries)) + struct.pack('<301I', *tops) + b''.join(entries))
    exe = '/tmp/sbm_test_font_pages'
    subprocess.check_call(['cc', '-O2', '-o', exe, os.path.join(ROOT, 'tests', 'fonts', 'test_font_pages.c'), '-I', os.path.join(ROOT, 'src'), '-lm'])
    res = subprocess.run([exe, 'numbers', a.game, data, gpath, str(k), a.out], capture_output=True, text=True)
    print(res.stdout.strip())
    if res.returncode:
        return 1
    side = 256 * k
    pages = {}
    for s in (0x4E, 0x4F, 0x50, 0x51):
        raw = open(os.path.join(a.out, 'page_%02X.argb' % s), 'rb').read()
        pages[s] = Image.frombuffer('RGBA', (side, side), raw, 'raw', 'BGRA', 0, 1)
    pal = open(os.path.join(a.game, 'SBM_palette.raw'), 'rb').read()
    idx = {s: open(os.path.join(a.game, 'SBM_fontpage_%02X_256x256x1.raw' % s), 'rb').read() for s in pages}

    def text(n, t, new, pct):
        f = fnt[n]
        L = 13
        W = sum(f[ord(c)]['w'] for c in t)
        im = Image.new('RGBA', (W * k, L * k), (0, 0, 0, 0))
        x = 0
        for c in t:
            r = f[ord(c)]
            if r['mat']:
                if new:
                    g = pages[r['mat']].crop((r['u'] * k, r['v'] * k, (r['u'] + r['w']) * k, (r['v'] + r['h']) * k))
                else:
                    g = Image.new('RGBA', (r['w'], r['h']), (0, 0, 0, 0))
                    for yy in range(r['h']):
                        for xx in range(r['w']):
                            i = idx[r['mat']][(r['v'] + yy) * 256 + r['u'] + xx]
                            if i:
                                g.putpixel((xx, yy), tuple(pal[3 * i:3 * i + 3]) + (255,))
                    g = g.resize((r['w'] * k, r['h'] * k), Image.NEAREST)
                im.alpha_composite(g, (x * k, r['yoff'] * k))
            x += r['w']
        sz = (round(im.width * pct / 100 / k), round(im.height * pct / 100 / k))
        return im.resize(sz, Image.LANCZOS if new else Image.NEAREST)

    samples = [('numr', '-16'), ('numr', '-7'), ('numg', '0'), ('numr', '+3')]
    lab = ImageFont.truetype(subprocess.run(['fc-match', '-f', '%{file}', 'DejaVu Sans'], capture_output=True, text=True).stdout, 22)
    pcts = [250, 400, 600]
    colw = 560
    rows = [('Original', False), ('Montserrat ExtraBold (redrawn)', True)]
    rh = 13 * 6 + 30
    sheet = Image.new('RGBA', (360 + colw * len(pcts), 60 + rh * len(rows)), (44, 46, 40, 255))
    d = ImageDraw.Draw(sheet)
    for ci, p in enumerate(pcts):
        d.text((360 + ci * colw, 16), f'{p}%', font=lab, fill=(255, 255, 255))
    for ri, (name, new) in enumerate(rows):
        y = 60 + ri * rh
        d.text((12, y + rh // 2 - 12), name, font=lab, fill=(230, 230, 230))
        for ci, p in enumerate(pcts):
            x = 360 + ci * colw
            for n, t in samples:
                im = text(n, t, new, p)
                sheet.alpha_composite(im, (x, y + (rh - im.height) // 2))
                x += im.width + p // 10
    out = os.path.join(a.out, 'number_font_preview.png')
    sheet.convert('RGB').save(out)
    print('preview:', out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
