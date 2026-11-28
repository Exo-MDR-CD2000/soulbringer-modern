#!/usr/bin/env python3
"""Interface fonts redrawn in layers ([Fonts] Interface): offline pages and previews.

  python3 tools/ui_font_preview.py --game <dir> --face <path.ttf> --out <dir> [--k 4] [--scale 2.0]

<dir> holds the game's data/*.fnt (or *.fnt directly) and a Log=1 session's
SBM_fontpage_<slot>_256x256x1.raw + SBM_palette.raw. Renders the 16xY / 16xB /
16xX glyphs from the TrueType face (FreeType, the way the DLL gets them from GDI:
capitals for lowercase codes, 'H' as tall as the original rectangle minus the
rings), runs tests/fonts/test_font_pages.c in "ui" mode (fpg_ui_colours +
fpg_glyph_layers, the DLL's own code) and draws sample lines from the original
and the redrawn pages, sampled the way the game does at --scale (text size).
Writes ui_pages_<slot>.png, ui_preview_<scale>.png and the test's output.
"""
import argparse, os, struct, subprocess, sys, tempfile
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FONTS = ('16xY', '16xB', '16xX')


def read_fnt(path):
    d = open(path, 'rb').read()
    n = struct.unpack_from('<H', d, 0)[0]
    recs = []
    for i in range(n):
        p = 2 + i * 32
        code = struct.unpack_from('<H', d, p)[0]
        mat, u, v, w, h = struct.unpack_from('<iiiii', d, p + 4)
        yo = struct.unpack_from('<h', d, p + 0x1C)[0]
        recs.append((code, mat, u, v, w, h, yo))
    return recs


def ui_code(c):
    if ord('a') <= c <= ord('z') or (0xE0 <= c <= 0xFE and c != 0xF7):
        return c - 32
    return c


def em_for(face, cap):
    """FreeType size whose 'H' is cap pixels tall (as fpg_ui_em with GDI metrics)"""
    lo, hi = 2, 400
    while lo < hi:
        m = (lo + hi + 1) // 2
        bb = ImageFont.truetype(face, m).getbbox('H', anchor='ls')
        if -bb[1] <= cap:
            lo = m
        else:
            hi = m - 1
    return lo


def coverage(font, code):
    ch = chr(code)
    try:
        bb = font.getbbox(ch, anchor='ls')
    except Exception:
        return None
    w, h = bb[2] - bb[0], bb[3] - bb[1]
    if w <= 0 or h <= 0:
        return None
    im = Image.new('L', (w, h), 0)
    ImageDraw.Draw(im).text((-bb[0], -bb[1]), ch, font=font, fill=255, anchor='ls')
    return np.asarray(im, np.uint8), -bb[1]          # bitmap, rows above the baseline


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', required=True)
    ap.add_argument('--face', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--k', type=int, default=4)
    ap.add_argument('--scale', type=float, action='append')
    a = ap.parse_args()
    k = a.k
    line, outer, shade = (k + 1) // 2 if k > 1 else 1, (k + 1) // 2 if k > 1 else 1, (k + 1) // 2   # as the DLL
    fdir = os.path.join(a.game, 'data') if os.path.isdir(os.path.join(a.game, 'data')) else a.game
    os.makedirs(a.out, exist_ok=True)
    recs = {f: read_fnt(os.path.join(fdir, f + '.fnt')) for f in FONTS}
    entries = []
    for fi, f in enumerate(FONTS):
        H = next(r for r in recs[f] if r[0] == ord('H') and r[1])
        cap = H[5] * k - 2 * (line + outer)
        font = ImageFont.truetype(a.face, em_for(a.face, cap))
        seen = set()
        for r in recs[f]:
            if not r[1] or r[0] < 33 or r[0] in seen:
                continue
            seen.add(r[0])
            cv = coverage(font, ui_code(r[0]))
            if cv is None:
                continue
            bm, top = cv
            entries.append(struct.pack('<5I', fi, r[0], bm.shape[1], bm.shape[0], top & 0xFFFFFFFF) + bm.tobytes())
        print(f'{f}: cap {cap} px, em {font.size}')
    gpath = os.path.join(a.out, 'glyphs-ui.bin')
    open(gpath, 'wb').write(struct.pack('<4s4I', b'SBMU', len(entries), line, outer, shade) + b''.join(entries))
    exe = os.path.join(tempfile.gettempdir(), 'sbm_test_font_pages')
    subprocess.check_call(['cc', '-O2', '-o', exe, os.path.join(ROOT, 'tests', 'fonts', 'test_font_pages.c'), '-I', os.path.join(ROOT, 'src'), '-lm'])
    res = subprocess.run([exe, 'ui', a.game, fdir, gpath, str(k), a.out], capture_output=True, text=True)
    open(os.path.join(a.out, 'ui_test.txt'), 'w').write(res.stdout)
    print(res.stdout.strip())

    # previews: original pages (from the dumps) and redrawn ones, sampled like the game
    pal = np.frombuffer(open(os.path.join(a.game, 'SBM_palette.raw'), 'rb').read(), np.uint8).reshape(256, 3)
    side = 256 * k
    orig, new = {}, {}
    for s in range(4):
        idx = np.frombuffer(open(os.path.join(a.game, f'SBM_fontpage_{0x4E + s:02X}_256x256x1.raw'), 'rb').read(), np.uint8).reshape(256, 256)
        rgba = np.dstack([pal[idx], np.where(idx > 0, 255, 0).astype(np.uint8)])
        orig[0x4E + s] = Image.fromarray(rgba.astype(np.uint8), 'RGBA').resize((side, side), Image.NEAREST)
        raw = np.fromfile(os.path.join(a.out, f'page_{0x4E + s:02X}.argb'), np.uint32).reshape(side, side)
        arr = np.dstack([(raw >> 16) & 255, (raw >> 8) & 255, raw & 255, raw >> 24]).astype(np.uint8)
        new[0x4E + s] = Image.fromarray(arr, 'RGBA')
        new[0x4E + s].save(os.path.join(a.out, f'ui_page_{0x4E + s:02X}.png'))

    def draw(text, f, pages, sc):
        R = {}
        for r in recs[f]:
            R.setdefault(r[0], r)
        x = 0; parts = []
        for ch in text:
            code, mat, u, v, w, h, yo = R.get(ord(ch), R[32])
            if mat:
                g = pages[mat].crop((u * k, v * k, (u + w) * k, (v + h) * k)).resize((max(1, round(w * sc)), max(1, round(h * sc))), Image.BILINEAR)
                parts.append((round(x * sc), round(yo * sc), g))
            x += w                                     # advance = w + spacing (-1) + 1
        im = Image.new('RGBA', (max(1, round(x * sc)) + 4, round(17 * sc)), (0, 0, 0, 0))
        for px, py, g in parts:
            im.alpha_composite(g, (px, py))
        return im

    samples = [('16xY', 'Encumbrance   14/200'), ('16xY', 'Tool Tips   Always Run   On   Off'), ('16xY', 'Strength   Hit Points   100/100   9/9'),
               ('16xB', 'Cut Low'), ('16xX', 'Cut Low'), ('16xY', 'COMBO   Cut')]
    lab = ImageFont.truetype(a.face, 16)
    for sc in a.scale or [2.0]:
        rows = []
        for name, pages in (('original', orig), ('redrawn', new)):
            for f, t in samples:
                im = draw(t, f, pages, sc)
                R = Image.new('RGB', (im.width + 300, im.height + 12), (12, 10, 22))
                ImageDraw.Draw(R).text((8, R.height // 2 - 9), f'{name} {f}', fill=(200, 200, 200), font=lab)
                R.paste(im, (290, 6), im)
                rows.append(R)
        W = max(r.width for r in rows)
        S = Image.new('RGB', (W, sum(r.height + 4 for r in rows)), (40, 40, 40))
        y = 0
        for r in rows:
            S.paste(r, (0, y)); y += r.height + 4
        S.save(os.path.join(a.out, f'ui_preview_{int(sc * 100)}.png'))
    return 1 if 'UI PASSED' not in res.stdout else 0


if __name__ == '__main__':
    sys.exit(main())
