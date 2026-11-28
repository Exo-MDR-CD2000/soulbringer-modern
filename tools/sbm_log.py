#!/usr/bin/env python3
"""Read a Soulbringer Modern debug log (Log=1) and summarise it.

    python3 tools/sbm_log.py SoulbringerModern_SoulbringeVCnoeax_1234.log [outdir]

Prints the session events (screens, mode changes, clicks), a hover timeline
(tooltip/label boxes: cursor, box size and offset, text size, words, what is
under the cursor), the text catalogue (every distinct string drawn, with
its font and the code that drew it) and, for every frame capture, writes
outdir/capture_NN.png with the UI as the game placed it (top)
and after the Soulbringer Modern layout (bottom). Needs Pillow for the pictures.
"""
import os, re, sys

CAP = re.compile(r'^\s*(\d+) (\w+)\s+([0-9A-F]{8})\s+b=(\w+) m=(\w+)\s+n=(\d+)\s+x (\S+)\.\.(\S+) y (\S+)\.\.(\S+) '
                 r'\[.*?\] z (\S+)\.\.(\S+) rhw (\S+)\.\.(\S+)')
LAY = re.compile(r'^\s+layout (.)(.) -> x (\S+)\.\.(\S+) y (\S+)\.\.(\S+)')
GLY = re.compile(r'\(([\d.\-]+),([\d.\-]+) ([\d.]+)x([\d.]+)\)')
HDR = re.compile(r'FRAME CAPTURE (\d+): (\d+) calls.*?screen (\d+) x (\d+)(?:, fingerprint (\w+), (\w+))?')
COLORS = {'004B686A': (90, 90, 170), '004A9195': (230, 170, 40), '0042B0BC': (255, 255, 255),
          '00456ABD': (60, 200, 90), '0048EA4B': (220, 60, 200), '0053E5BB': (220, 60, 200)}


def parse(path):
    lines = open(path, encoding='latin1').read().split('\n')
    events, caps, cur = [], [], None
    last_event = None
    for i, l in enumerate(lines):
        if l.startswith('event:') or l.startswith('text:'):
            events.append(l); last_event = len(events) - 1
            continue
        if last_event is not None and l.startswith('        ') and cur is None and events[last_event].startswith('event: hover'):
            events[last_event] += '\n' + l
            continue
        last_event = None
        m = HDR.search(l)
        if m and 'END' not in l:
            cur = {'id': int(m.group(1)), 'W': int(m.group(3)), 'H': int(m.group(4)),
                   'sig': m.group(5), 'mode': m.group(6), 'items': []}
            caps.append(cur)
            continue
        if 'END FRAME CAPTURE' in l:
            cur = None
            continue
        if cur is None:
            continue
        if re.match(r'^\s*\d+ (RenderAllBuffers|EndFrame|EndScene)\b', l):
            cur['items'].append({'flush': True})
            continue
        m = CAP.match(l)
        if m:
            x0, x1, y0, y1, z0, z1, w0, w1 = map(float, m.group(7, 8, 9, 10, 11, 12, 13, 14))
            it = {'func': m.group(2), 'caller': m.group(3), 'mat': m.group(5), 'n': int(m.group(6)),
                  'box': (x0, y0, x1, y1), 'rhw': w0, 'world': w1 < 0.1, 'after': None, 'how': None, 'glyphs': []}
            cur['items'].append(it)
            continue
        m = LAY.match(l)
        if m and cur['items']:
            it = cur['items'][-1]
            it['how'], it['kind'] = m.group(1), m.group(2)
            it['after'] = tuple(map(float, (m.group(3), m.group(5), m.group(4), m.group(6))))
            continue
        if l.strip().startswith('chars "') and cur['items']:
            m = re.match(r'\s*chars "(.*)" font (\S+)', l)
            if m:
                cur['items'][-1]['chars'] = m.group(1); cur['items'][-1]['font'] = m.group(2)
            continue
        if l.strip().startswith('glyphs:') and cur['items']:
            cur['items'][-1]['glyphs'] = [tuple(map(float, g)) for g in GLY.findall(l)]
    return events, caps


def render(cap, path):
    from PIL import Image, ImageDraw
    S = 960 / cap['W']
    w, h = 960, int(cap['H'] * S)
    im = Image.new('RGB', (w, h * 2 + 8), (40, 0, 0))
    for half, key in ((0, 'box'), (1, 'after')):
        top = half * (h + 8)
        d = ImageDraw.Draw(im)
        d.rectangle([0, top, w - 1, top + h - 1], fill=(0, 0, 0), outline=(120, 0, 0))
        # 4:3 guide
        g = (cap['W'] - cap['H'] * 4 / 3) / 2 * S
        d.line([g, top, g, top + h], fill=(50, 50, 50)); d.line([w - g, top, w - g, top + h], fill=(50, 50, 50))
        for it in cap['items']:
            if it.get('flush') or it['world']:
                continue
            b = it[key] if it[key] else it['box']
            c = COLORS.get(it['caller'], (150, 150, 150))
            r = [b[0] * S, top + b[1] * S, b[2] * S, top + b[3] * S]
            if it['caller'] == '0042B0BC':
                d.rectangle(r, fill=c)
            else:
                d.rectangle(r, outline=c)
        d.text((6, top + 4), ('GAME (before)' if half == 0 else 'AFTER LAYOUT') +
               f"  capture {cap['id']}  {cap.get('mode') or ''}  {cap.get('sig') or ''}", fill=(255, 90, 90))
    im.save(path)


def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    events, caps = parse(sys.argv[1])
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(sys.argv[1])[0] + '_captures'
    hovers = [e for e in events if e.startswith('event: hover ') and not e.startswith('event: hover end')]
    texts = [e for e in events if e.startswith('text:')]
    other = [e for e in events if not e.startswith('event: hover') and not e.startswith('text:')]
    print(f'{len(other)} events, {len(hovers)} hovers, {len(texts)} distinct texts, {len(caps)} frame captures')
    for e in other:
        print('  ' + e)
    if hovers:
        print('\nHOVER TIMELINE (tooltip / label boxes)')
        for e in hovers:
            print('  ' + e.replace('\n', '\n  '))
        # sizes, for text and box scaling
        rx = re.compile(r'box \(\S+\) (\S+) x (\S+), offset from cursor \((\S+),(\S+)\)')
        tx = re.compile(r'text (\d+) glyphs, (\d+) line\(s\), (\S+) x (\S+), glyph height <= (\S+), margins L (\S+) T (\S+) R (\S+) B (\S+)')
        boxes = [tuple(map(float, m.groups())) for m in (rx.search(e) for e in hovers) if m]
        txts = [tuple(map(float, m.groups())) for m in (tx.search(e) for e in hovers) if m]
        if boxes:
            hs = sorted(set(round(b[1]) for b in boxes)); offs = sorted(set((round(b[2]), round(b[3])) for b in boxes))
            print(f'\n  box heights seen: {hs} px; offsets from cursor: {offs}')
        if txts:
            gh = sorted(set(round(t[4]) for t in txts)); ml = sorted(set(round(t[5]) for t in txts))
            mt = sorted(set(round(t[6]) for t in txts)); lines_ = sorted(set(int(t[1]) for t in txts))
            print(f'  glyph heights: {gh} px; left margins: {ml} px; top margins: {mt} px; lines per box: {lines_}')
    if texts:
        print('\nTEXT CATALOGUE (first draw of each string)')
        for e in texts:
            print('  ' + e)
    try:
        import PIL  # noqa
        os.makedirs(out, exist_ok=True)
        for c in caps:
            render(c, os.path.join(out, f"capture_{c['id']:02d}.png"))
        print(f'pictures written to {out}')
    except ImportError:
        print('(install Pillow for pictures)')
    for c in caps:
        ui = [i for i in c['items'] if not i.get('flush') and not i['world']]
        hows = {}
        for i in ui:
            k = (i['how'] or '?') + (i.get('kind') or '')
            hows[k] = hows.get(k, 0) + 1
        print(f"capture {c['id']:2d}: {c.get('mode') or '?':4s} {c.get('sig') or '':8s} "
              f"{len(c['items'])} calls, {len(ui)} UI buffers, layout {dict(sorted(hows.items()))}")


if __name__ == '__main__':
    main()
