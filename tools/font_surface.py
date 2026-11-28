#!/usr/bin/env python3
"""Render and check the font surface dumps of the HD fonts surface probe.

With Log=1 the DLL writes, in the game folder:
  SBM_fontsurf_<slot>.raw                 converted surface (64-byte header + rows)
  SBM_fontpage_<slot>_<w>x<h>x1.raw       the game's 8-bit indices for the same page
  SBM_palette.raw                         the game palette (256 x RGB)

This tool decodes each surface with its own pixel format, writes PNGs
(colour over a checkerboard, and the alpha channel), and, when the indices
and palette are there, compares every texel with palette[index] to show how
the renderer converts the page and how index 0 becomes transparent.

  python3 tools/font_surface.py <folder with the dumps> [-o out_dir]
  python3 tools/font_surface.py --selftest

Needs Pillow for the PNGs (the checks run without it).
"""
import argparse, glob, os, struct, sys

HDR = 64
DDPF_ALPHAPIXELS = 0x1


def read_surface(path):
    data = open(path, 'rb').read()
    h = struct.unpack('<16I', data[:HDR])
    if h[0] != 0x534D4253:
        raise ValueError(f'{path}: not an SBMS dump')
    info = dict(slot=h[2], w=h[3], h=h[4], row=h[5], bits=h[6], pf=h[7],
                masks=h[8:12], ck=h[12], ck_lo=h[13], ck_hi=h[14], upload=h[15])
    px = data[HDR:]
    if len(px) != info['row'] * info['h']:
        raise ValueError(f'{path}: {len(px)} pixel bytes, expected {info["row"] * info["h"]}')
    return info, px


def _shift_bits(m):
    if not m:
        return 0, 0
    s = (m & -m).bit_length() - 1
    return s, bin(m).count('1')


def decode(info, px):
    """-> list of rows of (r, g, b, a, raw) with 8-bit channels."""
    bpp = info['bits'] // 8
    sb = [_shift_bits(m) for m in info['masks']]
    has_a = bool(info['pf'] & DDPF_ALPHAPIXELS) and info['masks'][3]
    out = []
    for y in range(info['h']):
        row = []
        base = y * info['row']
        for x in range(info['w']):
            v = int.from_bytes(px[base + x * bpp: base + (x + 1) * bpp], 'little')
            ch = []
            for (s, n), m in zip(sb, info['masks']):
                if not n:
                    ch.append(255)
                    continue
                c = (v & m) >> s
                ch.append((c * 255 + ((1 << n) - 1) // 2) // ((1 << n) - 1))
            a = ch[3] if has_a else 255
            if info['ck'] and info['ck_lo'] <= v <= info['ck_hi']:
                a = 0
            row.append((ch[0], ch[1], ch[2], a, v))
        out.append(row)
    return out


def quantize(rgb, masks):
    q = []
    for c, m in zip(rgb, masks[:3]):
        n = bin(m).count('1')
        if not n:
            q.append(255)
            continue
        v = c >> (8 - n)   # the renderer truncates (verified on all four pages, 2026-11-22)
        q.append((v * 255 + ((1 << n) - 1) // 2) // ((1 << n) - 1))
    return tuple(q)


def compare(info, pix, idx, pal, sw, sh):
    """Every surface texel against palette[index] (nearest sampling if sizes differ)."""
    W, H = info['w'], info['h']
    match = total = 0
    zero_alpha, zero_rgb, other_alpha = {}, {}, {}
    mism = []
    for y in range(H):
        sy = y * sh // H
        for x in range(W):
            sx = x * sw // W
            i = idx[sy * sw + sx]
            r, g, b, a, raw = pix[y][x]
            if i == 0:
                zero_alpha[a] = zero_alpha.get(a, 0) + 1
                zero_rgb[(r, g, b)] = zero_rgb.get((r, g, b), 0) + 1
                continue
            other_alpha[a] = other_alpha.get(a, 0) + 1
            want = quantize(pal[i], info['masks'])
            total += 1
            if (r, g, b) == want:
                match += 1
            elif len(mism) < 8:
                mism.append((x, y, i, pal[i], (r, g, b), raw))
    return dict(match=match, total=total, zero_alpha=zero_alpha, zero_rgb=zero_rgb,
                other_alpha=other_alpha, mism=mism)


def top(d, n=4):
    return ', '.join(f'{k}: {v}' for k, v in sorted(d.items(), key=lambda kv: -kv[1])[:n])


def report(path, out_dir):
    info, px = read_surface(path)
    pix = decode(info, px)
    slot = info['slot']
    folder = os.path.dirname(path)
    m = info['masks']
    print(f'slot {slot:02X}: {info["w"]}x{info["h"]}, {info["bits"]} bits, masks R {m[0]:08X} G {m[1]:08X} '
          f'B {m[2]:08X} A {m[3]:08X}, pixel format flags {info["pf"]:08X}, '
          f'colour key {"%08X..%08X" % (info["ck_lo"], info["ck_hi"]) if info["ck"] else "none"}, upload {info["upload"]}')
    res = None
    src = glob.glob(os.path.join(folder, f'SBM_fontpage_{slot:02X}_*x*x1.raw'))
    palp = os.path.join(folder, 'SBM_palette.raw')
    if src and os.path.exists(palp):
        sw, sh = map(int, os.path.basename(src[0]).split('_')[3].split('x')[:2])
        idx = open(src[0], 'rb').read()
        p = open(palp, 'rb').read()
        pal = [tuple(p[3 * i:3 * i + 3]) for i in range(256)]
        res = compare(info, pix, idx, pal, sw, sh)
        pct = 100.0 * res['match'] / max(1, res['total'])
        print(f'  colour vs palette[index]: {res["match"]}/{res["total"]} texels match ({pct:.1f}%)')
        print(f'  index 0 -> alpha {top(res["zero_alpha"])}; colour {top(res["zero_rgb"], 2)}')
        print(f'  other indices -> alpha {top(res["other_alpha"])}')
        for x, y, i, want, got, raw in res['mism']:
            print(f'    mismatch at {x},{y}: index {i} palette {want} surface {got} raw {raw:X}')
    else:
        print('  (no index dump or palette next to it: colours not checked)')
    try:
        from PIL import Image
    except ImportError:
        print('  (Pillow not installed: no PNGs)')
        return info, res
    W, H = info['w'], info['h']
    col = Image.new('RGB', (W, H))
    alpha = Image.new('L', (W, H))
    cp, ap = col.load(), alpha.load()
    for y in range(H):
        for x in range(W):
            r, g, b, a, _ = pix[y][x]
            bg = 200 if ((x // 8) + (y // 8)) & 1 else 140
            cp[x, y] = tuple((c * a + bg * (255 - a)) // 255 for c in (r, g, b))
            ap[x, y] = a
    os.makedirs(out_dir, exist_ok=True)
    s = max(1, 768 // max(W, H))
    col.resize((W * s, H * s), Image.NEAREST).save(os.path.join(out_dir, f'fontsurf_{slot:02X}.png'))
    alpha.resize((W * s, H * s), Image.NEAREST).save(os.path.join(out_dir, f'fontsurf_{slot:02X}_alpha.png'))
    print(f'  wrote fontsurf_{slot:02X}.png, fontsurf_{slot:02X}_alpha.png in {out_dir}')
    return info, res


def selftest():
    """Build a 4444 surface from indices + palette the way the renderer would
    (index 0 transparent), then check the tool decodes it back exactly."""
    import tempfile
    d = tempfile.mkdtemp()
    W = H = 16
    pal = b''.join(bytes((i * 7 % 256, i * 13 % 256, i * 29 % 256)) for i in range(256))
    idx = bytes((x * 3 + y * 5) % 9 if (x + y) % 4 else 0 for y in range(H) for x in range(W))
    rows = b''
    for y in range(H):
        for x in range(W):
            i = idx[y * W + x]
            r, g, b = pal[3 * i:3 * i + 3]
            a = 0 if i == 0 else 15
            v = (a << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)
            rows += struct.pack('<H', v)
    hdr = struct.pack('<16I', 0x534D4253, 1, 0x4E, W, H, W * 2, 16, 0x41, 0xF00, 0xF0, 0xF, 0xF000, 0, 0, 0, 1)
    open(os.path.join(d, 'SBM_fontsurf_4E.raw'), 'wb').write(hdr + rows)
    open(os.path.join(d, 'SBM_fontpage_4E_16x16x1.raw'), 'wb').write(idx)
    open(os.path.join(d, 'SBM_palette.raw'), 'wb').write(pal)
    info, res = report(os.path.join(d, 'SBM_fontsurf_4E.raw'), d)
    ok = res and res['match'] == res['total'] and set(res['zero_alpha']) == {0} and set(res['other_alpha']) == {255}
    # colour-key variant: 565, key = 0
    rows = b''
    for y in range(H):
        for x in range(W):
            i = idx[y * W + x]
            r, g, b = pal[3 * i:3 * i + 3]
            v = 0 if i == 0 else ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            if i and v == 0:
                v = 1
            rows += struct.pack('<H', v)
    hdr = struct.pack('<16I', 0x534D4253, 1, 0x4E, W, H, W * 2, 16, 0x40, 0xF800, 0x7E0, 0x1F, 0, 1, 0, 0, 1)
    open(os.path.join(d, 'SBM_fontsurf_4E.raw'), 'wb').write(hdr + rows)
    info, res2 = report(os.path.join(d, 'SBM_fontsurf_4E.raw'), d)
    ok = ok and set(res2['zero_alpha']) == {0} and set(res2['other_alpha']) == {255}
    print('SELFTEST', 'PASSED' if ok else 'FAILED')
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('folder', nargs='?')
    ap.add_argument('-o', '--out', default=None)
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.folder:
        ap.error('give the folder with the dumps (the game folder)')
    files = sorted(glob.glob(os.path.join(a.folder, 'SBM_fontsurf_*.raw')))
    if not files:
        print('no SBM_fontsurf_*.raw found')
        return 1
    for f in files:
        report(f, a.out or a.folder)
    return 0


if __name__ == '__main__':
    sys.exit(main())
