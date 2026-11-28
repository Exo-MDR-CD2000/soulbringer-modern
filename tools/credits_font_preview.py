#!/usr/bin/env python3
"""Credits font ([Fonts] Credits): cred redrawn from the bundled Soulbringer Credits.

Renders the face's glyphs at the em fpg_ui_em picks ('H' as tall as the game's 'H',
19 texels x k), runs `test_font_pages credits` (the real font_pages.inc code: white
fill from the page, no outline, nothing outside cred's rectangles touched, codes the
face lacks left to the game) and writes a preview: the game's letters and the redrawn
ones, with the game's widths and kerning, at 280 %.

  python3 tools/credits_font_preview.py --game <folder with data\\cred.fnt and the
      SBM_fontpage / SBM_palette dumps> --out <dir> [--k 4]
"""
import argparse, os, struct, subprocess, sys
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from font_preview import read_fnt  # noqa: E402

FACE = os.path.join(ROOT, 'dist', 'fonts', 'SoulbringerCredits-Regular.ttf')


def ansi(code):
    return bytes([code]).decode('cp1252', errors='replace') if code < 256 else chr(code)


def topH(em):
    return -ImageFont.truetype(FACE, em).getbbox('H', anchor='ls')[1] if em > 0 else 0


def ui_em(h, k, tops):
    """fpg_ui_em (font_pages.inc) with inset 0, step for step"""
    cap = max(4, h * k)
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


def kerning(path):
    b = open(path, 'rb').read()
    n = struct.unpack_from('<H', b, 0)[0]
    q = 2 + n * 32
    ne = struct.unpack_from('<H', b, q)[0]
    cnt = struct.unpack_from('<%dH' % ne, b, q + 2)
    p = q + 2 + 2 * ne
    K = {}
    for e in range(ne):
        for _ in range(cnt[e]):
            a, c, adj = struct.unpack_from('<HHi', b, p)
            K[(a, c)] = adj
            p += 8
    return K


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--k', type=int, default=4)
    a = ap.parse_args()
    k = a.k
    os.makedirs(a.out, exist_ok=True)
    data = os.path.join(a.game, 'data')
    fnt = read_fnt(os.path.join(data, 'cred.fnt'))
    K = kerning(os.path.join(data, 'cred.fnt'))
    tops = [topH(e) for e in range(301)]
    em = ui_em(fnt[ord('H')]['h'], k, tops)
    font = ImageFont.truetype(FACE, em)
    cmap = TTFont(FACE).getBestCmap()
    entries = []
    for code, r in sorted(fnt.items()):
        if not r['mat'] or code < 33 or ord(ansi(code)) not in cmap:
            continue
        ch = ansi(code)
        bb = font.getbbox(ch, anchor='ls')
        w, h = max(1, bb[2] - bb[0]), max(1, bb[3] - bb[1])
        im = Image.new('L', (w, h), 0)
        ImageDraw.Draw(im).text((-bb[0], -bb[1]), ch, font=font, fill=255, anchor='ls')
        entries.append(struct.pack('<4I', code, w, h, -bb[1]) + im.tobytes())
    gpath = os.path.join(a.out, 'credits_glyphs.bin')
    with open(gpath, 'wb') as o:
        o.write(b'SBMC' + struct.pack('<2I', em, len(entries)) + struct.pack('<301I', *tops) + b''.join(entries))
    exe = '/tmp/sbm_test_font_pages'
    subprocess.check_call(['cc', '-O2', '-o', exe, os.path.join(ROOT, 'tests', 'fonts', 'test_font_pages.c'), '-I', os.path.join(ROOT, 'src'), '-lm'])
    res = subprocess.run([exe, 'credits', a.game, data, gpath, str(k), a.out], capture_output=True, text=True)
    print(res.stdout.strip())
    if res.returncode:
        return 1
    side = 256 * k
    pages = {s: Image.frombuffer('RGBA', (side, side), open(os.path.join(a.out, 'page_%02X.argb' % s), 'rb').read(), 'raw', 'BGRA', 0, 1)
             for s in (0x4E, 0x4F, 0x50, 0x51)}
    pal = open(os.path.join(a.game, 'SBM_palette.raw'), 'rb').read()
    idx = {s: open(os.path.join(a.game, 'SBM_fontpage_%02X_256x256x1.raw' % s), 'rb').read() for s in pages}

    def cell(r, new):
        if new:
            return pages[r['mat']].crop((r['u'] * k, r['v'] * k, (r['u'] + r['w']) * k, (r['v'] + r['h']) * k))
        g = Image.new('RGBA', (r['w'], r['h']), (0, 0, 0, 0))
        for y in range(r['h']):
            for x in range(r['w']):
                i = idx[r['mat']][(r['v'] + y) * 256 + r['u'] + x]
                if i:
                    g.putpixel((x, y), tuple(pal[3 * i:3 * i + 3]) + (255,))
        return g.resize((r['w'] * k, r['h'] * k), Image.NEAREST)

    def line(t, new, pct=280):
        adv = [fnt[ord(c)]['w'] + (K.get((ord(c), ord(t[i + 1])), 1) if i + 1 < len(t) else 0) for i, c in enumerate(t)]
        im = Image.new('RGBA', (sum(adv) * k + 8, 35 * k), (0, 0, 0, 0))
        x = 0
        for c, d in zip(t, adv):
            r = fnt[ord(c)]
            if r['mat']:
                im.alpha_composite(cell(r, new), (x * k, r['yoff'] * k))
            x += d
        return im.resize((round(im.width * pct / 100 / k), round(im.height * pct / 100 / k)), Image.LANCZOS if new else Image.NEAREST)

    lines = ['GAME OVER', 'Soulbringer', 'Lead Programmer', 'The quick brown fox jumps over the lazy dog.', '0123456789  Éléonore, Œuvre, Straße']
    lab = ImageFont.truetype(subprocess.run(['fc-match', '-f', '%{file}', 'DejaVu Sans:bold'], capture_output=True, text=True).stdout, 26)
    rows = [[line(t, new) for t in lines] for new in (False, True)]
    half = max(i.width for r in rows for i in r) + 60
    out = Image.new('RGBA', (2 * half, 80 + sum(i.height + 10 for i in rows[0])), (30, 30, 34, 255))
    d = ImageDraw.Draw(out)
    d.text((20, 15), 'cred, the game\'s letters (280%)', font=lab, fill=(255, 255, 255))
    d.text((half + 20, 15), 'Soulbringer Credits, redrawn in cred\'s rectangles (280%)', font=lab, fill=(255, 255, 255))
    for col, ims in enumerate(rows):
        y = 70
        for i in ims:
            out.alpha_composite(i, (20 + col * half, y))
            y += i.height + 10
    path = os.path.join(a.out, 'credits_font_preview.png')
    out.convert('RGB').save(path)
    print('preview:', path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
