#!/usr/bin/env python3
"""Builds dist/fonts/SoulbringerUncial-Regular.ttf: Eagle Lake with lining figures.

Eagle Lake (Astigmatic, SIL OFL 1.1) has only old-style figures: 3 5 7 9 hang below the
baseline and 1 is x-height. For the menu font (save list numbers, dates, "2000") every digit
should sit on the baseline at the height of 0 2 4 6 8. Changes, nothing else touched:
  3 5 7 9  moved up onto the baseline (and fitted to the 0-8 height),
  1        stretched upwards from the baseline to that height.
Eagle Lake is a Reserved Font Name under the OFL, so the modified font is renamed
("Soulbringer Uncial") and keeps the original copyright and licence.

  python3 tools/make_menu_font.py dist/fonts/EagleLake-Regular.ttf dist/fonts/SoulbringerUncial-Regular.ttf
"""
import sys
from fontTools.ttLib import TTFont

FAMILY = 'Soulbringer Uncial'
PS = 'SoulbringerUncial-Regular'


def fit(glyf, name, bottom, top, mode):
    g = glyf[name]
    coords = g.coordinates
    y0, y1 = g.yMin, g.yMax
    for i, (x, y) in enumerate(coords):
        if mode == 'shift':      # move so the ink spans bottom..top (vertical scale close to 1)
            ny = bottom + (y - y0) * (top - bottom) / (y1 - y0)
        else:                    # 'stretch': keep the baseline (y = 0), scale upwards
            ny = y * (top / y1) if y > 0 else y
        coords[i] = (x, round(ny))
    g.recalcBounds(glyf)


def main(src, dst):
    f = TTFont(src)
    glyf, cmap = f['glyf'], f.getBestCmap()
    full = [glyf[cmap[ord(d)]] for d in '02468']
    bottom = round(sum(g.yMin for g in full) / len(full))   # ~ -18 (round overshoot)
    top = round(sum(g.yMax for g in full) / len(full))      # ~ 1568
    for d in '3579':
        fit(glyf, cmap[ord(d)], bottom, top, 'shift')
    fit(glyf, cmap[ord('1')], 0, top, 'stretch')
    for n in ('3579' + '1'):
        g = glyf[cmap[ord(n)]]
        assert not g.isComposite() and not (hasattr(g, 'program') and g.program and g.program.getBytecode())
    # names (OFL: a modified font may not use the Reserved Font Name "Eagle Lake")
    name = f['name']
    for rec in list(name.names):
        if rec.nameID in (1, 16):
            rec.string = FAMILY
        elif rec.nameID == 3:
            rec.string = '1.000;SBM;' + PS
        elif rec.nameID == 4:
            rec.string = FAMILY
        elif rec.nameID == 6:
            rec.string = PS
        elif rec.nameID == 5:
            rec.string = str(rec.toUnicode()) + '; Soulbringer Modern: lining figures'
    name.setName('Soulbringer Uncial is a modified version of Eagle Lake by Brian J. Bonislawsky '
                 '(Astigmatic): figures 1 3 5 7 9 changed to lining figures for Soulbringer Modern.', 10, 3, 1, 0x409)
    f['head'].fontRevision = f['head'].fontRevision
    f.save(dst)
    print(f'{dst}: digits on the baseline, height {bottom}..{top}; family "{FAMILY}"')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
