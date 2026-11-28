#!/usr/bin/env python3
"""Regression test: replay recorded frames through the real UI layout code.

    python3 tests/replay/replay.py [capture-log ...]   (default: tests/data/*.log)

Builds tests/replay/harness.c with the system C compiler, feeds it every frame
capture found in the logs, and checks the result:
  - menus:   every UI element ends up inside the centered 4:3 area
  - HUD:     right-column tiles touch the right edge, bottom-bar tiles are
             centered at 4:3 width
  - shapes:  stretched elements get 4:3 proportions, rigid ones keep their size;
             menu 3D models are scaled to the 4:3 area (the game sizes them by width)
  - world:   3D world geometry is never moved
  - HUD click mapping (ui_hud_aim): mapping a HUD point to where it is drawn
             and back gives the same point, for every layout, aspect cap and
             screen shape; matches the offsets measured in game; and every
             click recorded in a session is replayed through it (and, for
             logs with "aim:" lines, compared with what the DLL logged)
Writes before/after pictures next to the log when Pillow is installed.
Exit code 0 = all checks passed.
"""
import glob, os, re, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import sbm_log  # noqa: E402

def quad(x0, y0, x1, y1, z, w):
    return [(x0, y0, z, w), (x1, y0, z, w), (x1, y1, z, w), (x0, y0, z, w), (x1, y1, z, w), (x0, y1, z, w)]

def frame_input(cap):
    """Rebuild the frame from a capture. UI depth/rhw use realistic values
    (depth slightly above 0, rhw slightly below 1), as the game sends them."""
    out = ['F']
    order = []
    for it in cap['items']:
        if it.get('flush'):
            out.append('R'); continue
        x0, y0, x1, y1 = it['box']
        if it['world']:
            vs = [(x0, y0, 0.03, 0.0012)] * 3
        elif it['caller'] == '0042B0BC' and it['glyphs']:
            vs = []
            for gx, gy, gw, gh in it['glyphs']:
                vs += quad(gx, gy, gx + gw, gy + gh, 0.0001, 0.99999)
        elif it['rhw'] < 0.99:
            vs = [(x0, y0, 0.0001, it['rhw']), (x1, y1, 0.0001, it['rhw']), (x0, y1, 0.0001, it['rhw'])]
        else:
            vs = quad(x0, y0, x1, y1, 0.0001, 0.99999) * max(1, it['n'] // 6)
        out.append(f"B {it['caller']} {it['mat']} {len(vs)}")
        out += [f'{a} {b} {c} {d}' for a, b, c, d in vs]
        order.append(it)
    out.append('R')
    return '\n'.join(out) + '\n', order

def bbox(pts):
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    return min(xs), min(ys), max(xs), max(ys)

def cursor_tip(it, A):
    """Is this 0x00456ABD box a one-line tooltip at the cursor (scaled by TextScale)?"""
    x0, y0, x1, y1 = it['box']
    if A is None or not 40 < y1 - y0 < 52:
        return False
    corner = -30 < x0 - A[0] < 30 and -50 < y0 - A[1] < 40
    centred = -8 < (x0 + x1) / 2 - A[0] < 12 and 70 < y0 - A[1] < 110      # inventory items: centred, 88 px below
    return 'corner' if corner else 'centred' if centred else False

def check(cap, order, result, hud, aspect=0.0, layout=1, text=100):
    W, H = cap['W'], cap['H']
    s, sx = H / 480, W / 640
    off = (W - 640 * s) / 2
    capL, capR = 0.0, float(W)
    if aspect and W > H * aspect + 1:
        capL = (W - H * aspect) / 2; capR = capL + H * aspect
    errs = []
    is_menu = any(it['caller'] == '004B686A' and abs(it['box'][2] - W / 3) < 2 and it['box'][0] < 1 and it['box'][1] < 1
                  for it in order)
    for it, pts in zip(order, result):
        b = bbox(pts)
        it['after'] = b
        if it['box'][2] < 0 or it['box'][0] < -1:   # helper quads the game draws off-screen
            continue
        if it['world']:
            if abs(b[0] - it['box'][0]) > 0.01:
                errs.append(f"world geometry moved ({it['caller']})")
            continue
        bw, bh = it['box'][2] - it['box'][0], it['box'][3] - it['box'][1]
        if not is_menu and it['caller'] == '004A9195' and 31.5 < bw < 32.5 and 31.5 < bh < 32.5 and abs(b[0] - it['box'][0]) > 0.5:
            errs.append(f"gameplay cursor moved: x {it['box'][0]:.0f} -> {b[0]:.0f} (must follow the real mouse)")
        mL, mR = (off, W - off) if layout else (capL, capR)   # Layout=0: menus stretched over the capped area
        if is_menu and (b[0] < mL - 1 or b[2] > mR + 1) and it['caller'] != '004A9195':
            errs.append(f"menu element outside {'4:3' if layout else 'capped'} area: {it['caller']} m={it['mat']} x {b[0]:.0f}..{b[2]:.0f}")
        if it['rhw'] < 0.99 and is_menu and layout and not aspect and W * 3 > H * 4 + 1:
            # UI 3D models: the game sizes them by the screen width; in the pillarboxed menu
            # each is scaled to the 4:3 area (x 4H/3W), as at 1024x768 (DEVLOG 2026-11-24)
            k = 4 * H / (3 * W)
            if abs((b[2] - b[0]) - bw * k) > 0.6 or abs((b[3] - b[1]) - bh * k) > 0.6:
                errs.append(f"menu model {bw:.0f}x{bh:.0f} -> {b[2]-b[0]:.0f}x{b[3]-b[1]:.0f}, expected x{k:.3f}")
        if it['caller'] == '004B686A' and it['n'] == 6 and not is_menu:
            cx0, cx1 = it['box'][0] / sx, it['box'][2] / sx
            wc = cx1 - cx0           # bar tiles are a third of the width; full-width bands are cinematic
            region = None
            if abs(cx0 - 536.667) < 0.7 and it['box'][3] / s < 421:
                region = 'col'
            elif abs(it['box'][1] / s - 420) < 0.7 and it['box'][3] / s > 479 and 150 < wc < 300:
                region = 'bar'
            if region:
                anchor = {('split', 'col'): 'R', ('split', 'bar'): 'C', ('right', 'col'): 'R', ('right', 'bar'): 'R',
                          ('center', 'col'): 'C', ('center', 'bar'): 'C'}.get((hud, region))
                if not layout or anchor is None:
                    anchor = 'F' if capR - capL < W - 1 else None      # stretched UI squeezed into the cap
                if anchor == 'C':
                    ex0, ex1 = off + cx0 * s, off + cx1 * s
                elif anchor == 'R':
                    ex0, ex1 = capR - (640 - cx0) * s, capR - (640 - cx1) * s
                elif anchor == 'F':
                    ex0, ex1 = capL + it['box'][0] * (capR - capL) / W, capL + it['box'][2] * (capR - capL) / W
                else:
                    ex0, ex1 = it['box'][0], it['box'][2]
                if abs(b[0] - ex0) > 1 or abs(b[2] - ex1) > 1:
                    errs.append(f"{hud}: {region} tile at x {b[0]:.0f}..{b[2]:.0f}, expected {ex0:.0f}..{ex1:.0f}")
    # tooltip/hover boxes (0x00456ABD) and their text: one transform per box. One-line tooltips at
    # the cursor are scaled by TextScale around the cursor tip and kept on screen; the text keeps its
    # place in the box, scaled the same way
    curs = [it for it in order if it['caller'] == '004A9195' and 31.5 < it['box'][2] - it['box'][0] < 32.5
            and 31.5 < it['box'][3] - it['box'][1] < 32.5]
    A = (curs[-1]['box'][0], curs[-1]['box'][1]) if curs else None
    sc = text / 100
    def kind(it):
        k = cursor_tip(it, A)
        x0, y0, x1, y1 = it['box']
        if not k and not is_menu and 40 < y1 - y0 < 52 and abs((x0 + x1) / 2 - W / 2) < 3:
            k = 'message'                       # one line, centred on the screen (chest contents, doors)
        if (not k and not is_menu and 40 < y1 - y0 < 52 and 400 < y1 / s < 414 and 255 < (x0 + x1) / 2 / sx < 285):
            k = 'offmsg'                        # "A new quest has been added...": centred on the view left of the column
        return k
    tips = [(it['box'], it['after'], kind(it)) for it in order
            if it['caller'] == '00456ABD' and it['box'][0] >= 0 and it.get('after')]
    for (b0, b1, at) in tips:
        f = sc if at else 1.0
        if abs((b1[2] - b1[0]) - (b0[2] - b0[0]) * f) > 0.5 or abs((b1[3] - b1[1]) - (b0[3] - b0[1]) * f) > 0.5:
            errs.append(f"tooltip box {b0[2]-b0[0]:.0f}x{b0[3]-b0[1]:.0f} -> {b1[2]-b1[0]:.0f}x{b1[3]-b1[1]:.0f}, expected x{f:g}")
        if at == 'centred' and layout and not aspect and is_menu:
            # item tooltips grow around their own centre, which moves only as the cursor does in menus
            s_ = H / 480; sx_ = W / 640; off_ = (W - 640 * s_) / 2
            c = A[0] + 16; d = off_ + c * s_ / sx_ - c
            cx0, cy0 = (b0[0] + b0[2]) / 2 + d, (b0[1] + b0[3]) / 2
            cx1, cy1 = (b1[0] + b1[2]) / 2, (b1[1] + b1[3]) / 2
            Lm, Rm = off_, off_ + 640 * s_
            shifted = b1[0] <= Lm + 0.5 or b1[2] >= Rm - 0.5 or b1[1] <= 0.5 or b1[3] >= H - 0.5   # pushed back inside
            # moved off the enlarged cursor (2026-11-23): straight down to 6 px below it, or up to 6 px above
            offcur = abs(cx1 - cx0) <= 1 and (abs(b1[1] - (A[1] + 64 + 6)) <= 1 or abs(b1[3] - (A[1] - 6)) <= 1)
            if f != 1 and not shifted and not offcur and (abs(cx1 - cx0) > 1 or abs(cy1 - cy0) > 1):
                errs.append(f"item tooltip moved: centre ({cx1:.0f},{cy1:.0f}), the game's centre ({cx0:.0f},{cy0:.0f})")
        if at in ('message', 'offmsg') and (f != 1 or at == 'offmsg'):
            # messages grow upwards from their bottom centre (they stay just above the HUD); the quest
            # message is also centred on the screen with Layout on (2026-11-27)
            tx = W / 2 if at == 'offmsg' and layout else (b0[0] + b0[2]) / 2
            if abs((b1[0] + b1[2]) / 2 - tx) > 1 or abs(b1[3] - b0[3]) > 1:
                errs.append(f"message box moved: bottom centre ({(b1[0] + b1[2]) / 2:.0f},{b1[3]:.0f}), "
                            f"the game's ({(b0[0] + b0[2]) / 2:.0f},{b0[3]:.0f})")
        if at and f != 1:
            if b1[0] < -0.5 or b1[1] < -0.5 or b1[2] > W + 0.5 or b1[3] > H + 0.5:
                errs.append(f"scaled tooltip off screen: {[round(v) for v in b1]}")
            if not is_menu and at not in ('message', 'offmsg'):  # gameplay: the cursor is not moved (no mouse input here)
                ex0 = A[0] + (b0[0] - A[0]) * f; ey0 = A[1] + (b0[1] - A[1]) * f
                ex1 = A[0] + (b0[2] - A[0]) * f; ey1 = A[1] + (b0[3] - A[1]) * f
                # never over the cursor (2026-11-23): a box overlapping the enlarged cursor sprite
                # (32 px x CursorScale 200% from A) moves to end 6 px above it, or below if no room
                cs1 = 64.0
                if ex0 < A[0] + cs1 and ex1 > A[0] and ey0 < A[1] + cs1 and ey1 > A[1]:
                    up = (A[1] - 6) - ey1; down = (A[1] + cs1 + 6) - ey0
                    d_ = (down if ey1 + down <= H else up) if at == 'centred' else (up if ey0 + up >= 0 else down)
                    ey0 += d_; ey1 += d_
                ex0 += min(0, W - ex1); ey0 += min(0, H - ey1)
                ex0 += max(0, -ex0); ey0 += max(0, -ey0)
                if abs(b1[0] - ex0) > 1 or abs(b1[1] - ey0) > 1:
                    errs.append(f"tooltip not at the scaled cursor offset: at {b1[0]:.0f},{b1[1]:.0f}, expected {ex0:.0f},{ey0:.0f}")
        for it, pts in zip(order, result):
            if it['caller'] != '0042B0BC' or not it['glyphs']:
                continue
            for k, (gx, gy, gw, gh) in enumerate(it['glyphs']):
                cx, cy = gx + gw / 2, gy + gh / 2
                if not (b0[0] - 4 <= cx <= b0[2] + 4 and b0[1] - 4 <= cy <= b0[3] + 4) or len(pts) < 6 * k + 6:
                    continue
                q = pts[6 * k:6 * k + 6]
                ax, ay = min(p[0] for p in q), min(p[1] for p in q)
                aw = max(p[0] for p in q) - ax
                if at and not (b0[1] + 14 <= cy <= b0[1] + 32):
                    # other text under a cursor tooltip (e.g. a HUD slot label) is not the tooltip's text
                    if abs(aw - gw) > 1:
                        errs.append(f"text under a tooltip was scaled with it: glyph at {gx:.0f},{gy:.0f} ({gw:.0f} -> {aw:.0f} px wide)")
                        break
                    continue
                if abs((ax - b1[0]) - (gx - b0[0]) * f) > 1 or abs((ay - b1[1]) - (gy - b0[1]) * f) > 1 or abs(aw - gw * f) > 1:
                    errs.append(f"text not kept in its tooltip box (x{f:g}): glyph x {gx:.0f} -> {ax:.0f}, box x {b0[0]:.0f} -> {b1[0]:.0f}")
                    break
    return errs, is_menu

# ------------------------------------------------------------ HUD click mapping
# (name, HudLayout, MaxAspect, Layout): every HUD layout, plus aspect caps below 16:9
HUDS = {'stretched': 0, 'split': 1, 'right': 2, 'center': 3}
RUNS = [(h, c, 0.0, 1) for h, c in HUDS.items()] + [
    ('split', 1, 1.6, 1), ('right', 2, 1.6, 1), ('split', 1, 4 / 3, 1), ('stretched', 0, 1.6, 0)]

# Clicks recorded with the HUD mouse fix (a "hud mouse: real ..." line) must
# hit the button drawn under the real mouse; older clicks are only reported.
BUTTONS = os.path.join(ROOT, 'tests', 'data', 'hud_buttons.txt')

def run_harness(exe, head, lines):
    res = subprocess.run([exe], input=head + '\n'.join(lines) + '\n', capture_output=True, text=True)
    if res.returncode:
        raise RuntimeError(f'harness error {res.returncode}')
    return [l.split() for l in res.stdout.split('\n') if l[:2] in ('P ', 'U ', 'D ', 'OF', 'OR', 'OP', 'JS', 'JM', 'JL', 'JP', 'IG', 'IK', 'IT', 'IS', 'IL', 'IM', 'FN', 'EF', 'CP')]

def hud_points(W, H):
    """Canvas points on the HUD art: (x, y, part) with part 1 = bar, 2 = column."""
    pts = []
    for x in range(0, 641, 10):
        for y in (422, 450, 479):      # 420-421: ui_how counts a strip that thin as column
            pts.append((min(x, 639.9), y, 1))
        if x < 536:
            pts.append((x, 410, 1))               # bar ornaments above the bar (y 406-420)
    for x in (537, 560, 590, 620, 639.9):
        for y in (5, 100, 200, 300, 404):
            pts.append((x, y, 2))
    return [(x * W / 640, y * H / 480, p) for x, y, p in pts]

def hud_mapping_tests(exe):
    fails = 0
    for W, H in ((3840, 2160), (2560, 1080), (1920, 1200)):        # 16:9, 21:9, 16:10
        for hud, code, aspect, layout in RUNS:
            label = f'{W}x{H} {hud}' + (f' cap {aspect:.2f}' if aspect else '') + ('' if layout else ' Layout=0')
            head = f'S {W} {H}\nH {code}\nA {aspect}\nL {layout}\n'
            errs = []
            pts = hud_points(W, H)
            drawn = [float(r[1]) for r in run_harness(exe, head, [f'P {x} {y}' for x, y, _ in pts])]
            back = run_harness(exe, head, [f'U {d} {y} 1 1' for d, (_, y, _) in zip(drawn, pts)])
            for (x, y, part), d, (_, p, gx) in zip(pts, drawn, back):
                if int(p) != part or abs(float(gx) - x) > 0.02:
                    errs.append(f'round trip: game ({x:.1f},{y:.0f}) part {part} -> drawn {d:.1f} -> part {p}, game x {float(gx):.1f}')
            capped = aspect and W > H * aspect + 1
            if hud == 'stretched' and not capped:                     # nothing moves
                errs += [f'stretched HUD moved: {x:.1f} -> {d:.1f}' for (x, _, _), d in zip(pts, drawn) if abs(d - x) > 0.02]
            if (W, H, hud, aspect) == (3840, 2160, 'split', 0.0):    # offsets measured in game (DEVLOG 2026-11-21)
                for (x, _, part), d in zip(pts, drawn):
                    want = 480 - 0.25 * x if part == 1 else 0.25 * (3840 - x)
                    if abs((d - x) - want) > 0.05:
                        errs.append(f'offset at x {x:.0f} ({"bar" if part == 1 else "column"}): {d - x:.1f}, measured in game {want:.1f}')
            # world: off the drawn HUD (where the stretched HUD used to be), and HUD parts not on screen
            lo = min(d for d, (_, _, p) in zip(drawn, pts) if p == 1)
            probes = [(W / 2, H / 3, 1, 1)] + ([(lo - 5, H - 10, 1, 1)] if lo > 6 else []) + [
                (W / 2, H - 10, 1, 0), (W - 2, H / 3, 0, 1)]
            for (X, Y, c, b), (_, p, gx) in zip(probes, run_harness(exe, head, [f'U {X} {Y} {c} {b}' for X, Y, c, b in probes])):
                if int(p) != 0 or abs(float(gx) - X) > 0.02:
                    errs.append(f'world point ({X:.0f},{Y:.0f}) col={c} bar={b} mapped as HUD part {p}, x {float(gx):.1f}')
            # every named button: clicked at its drawn centre, the mapping must
            # give back that button; also report what the game hits today
            buttons, broken = load_buttons(), []
            if buttons:
                cs = [((x0 + x1) / 2, (y0 + y1) / 2, name) for name, x0, y0, x1, y1 in buttons]
                dr = [float(r[1]) for r in run_harness(exe, head, [f'P {cx * W / 640} {cy * H / 480}' for cx, cy, _ in cs])]
                bk = run_harness(exe, head, [f'U {d} {cy * H / 480} 1 1' for d, (_, cy, _) in zip(dr, cs)])
                for (cx, cy, name), d, (_, p, gx) in zip(cs, dr, bk):
                    back = button_at(buttons, float(gx) * 640 / W, cy)
                    if back != name:
                        errs.append(f'button {name}: drawn at x {d:.0f}, mapped back to {back or "nothing"}')
                    today = button_at(buttons, d * 640 / W, cy)
                    if today != name:
                        broken.append(f'{name}->{today or "-"}')
            fails += bool(errs)
            if broken and (W, H) == (3840, 2160) and not aspect:
                label2 = f'{len(buttons) - len(broken)}/{len(buttons)} buttons hit without the fix'
                print(f'       [{label.strip()}] {label2}: ' + ', '.join(broken))
            print(f"{'ok  ' if not errs else 'FAIL'} hud click mapping [{label:32s}] {len(pts)} points round-tripped"
                  + ('' if not errs else ':\n       ' + '\n       '.join(errs[:8])))
    return fails

def hud_mouse_tests(exe):
    """The HUD mouse fix (ui_hud_mouse), driven the way the game drives it."""
    fails = 0
    buttons = load_buttons()
    for W, H in ((3840, 2160), (2560, 1080), (1920, 1200)):
        for hud, code, aspect, layout in RUNS:
            label = f'{W}x{H} {hud}' + (f' cap {aspect:.2f}' if aspect else '') + ('' if layout else ' Layout=0')
            head = f'S {W} {H}\nH {code}\nA {aspect}\nL {layout}\n'
            errs = []
            # 1. every button: move the real mouse onto its drawn centre; the game's cursor must be on that button
            cs = [((x0 + x1) / 2, (y0 + y1) / 2, name) for name, x0, y0, x1, y1 in buttons]
            drawn = [float(r[1]) for r in run_harness(exe, head, [f'P {cx * W / 640} {cy * H / 480}' for cx, cy, _ in cs])]
            sx, sy = W // 2, H // 3                     # start in the world
            cmds = []
            for d, (cx, cy, name) in zip(drawn, cs):
                tx, ty = int(round(d)), int(cy * H / 480)
                cmds += ['Q 0 1 1', f'C {sx} {sy}', 'D 0 0 0 0', f'D {tx - sx} {ty - sy} 0 0']
            out = run_harness(exe, head, cmds)
            for k, (d, (cx, cy, name)) in enumerate(zip(drawn, cs)):
                gx, gy, rx, ry, ddx, ddy = out[2 * k + 1][1:]
                tx = int(round(d))
                if int(rx) != tx:
                    errs.append(f'{name}: real mouse at x {rx}, expected {tx}')
                got = button_at(buttons, int(gx) * 640 / W, int(gy) * 480 / H)
                if got != name:
                    errs.append(f'{name}: drawn at x {tx}, game cursor at x {gx} is on {got or "nothing"}')
                if abs(float(ddx) - (int(rx) - int(gx))) > 0.01:
                    errs.append(f'{name}: cursor drawn {ddx} px from the game cursor, real is {int(rx) - int(gx)} px away')
            # 2. sweeps: across the bar and over the column, out and back; no drift, no sticking at the ends
            for y in (int(450 * H / 480), int(200 * H / 480)):
                cmds = ['Q 0 1 1', f'C 0 {y}', 'D 0 0 0 0'] + ['D 7 0 0 0'] * (W // 7 + 3) + ['D -7 0 0 0'] * (W // 7 + 3)
                out = run_harness(exe, head, cmds)
                xs = [int(o[3]) for o in out]
                exp, ok = 0, True
                for step, rx in zip([0] + [7] * (W // 7 + 3) + [-7] * (W // 7 + 3), xs):
                    exp = min(max(exp + step, 0), W - 1)
                    if rx != exp:
                        errs.append(f'sweep at y {y}: real mouse at x {rx}, expected {exp}'); ok = False; break
            # 2b. the whole screen: over a drawn HUD part the game must be on the same button (see 1);
            #     over the world the game must be outside its own HUD (no ghost tooltips, world clicks
            #     not blocked): canvas y < 405 with the bar, and not x > 533 above y 420 with the column;
            #     points the game never had as HUD are passed unchanged
            pts2 = [(x, y) for x in range(0, W, max(8, W // 160)) for y in range(0, H, max(8, H // 90))]
            us = run_harness(exe, head, [f'U {x} {y} 1 1' for x, y in pts2])
            cmds = []
            for x, y in pts2:
                cmds += ['Q 0 1 1', f'C {W // 2} {H // 3}', 'D 0 0 0 0', f'D {x - W // 2} {y - H // 3} 0 0']
            out = run_harness(exe, head, cmds)
            bad = 0
            for k, ((x, y), (_, part, _)) in enumerate(zip(pts2, us)):
                gx, gy = int(out[2 * k + 1][1]), int(out[2 * k + 1][2])
                if int(part):
                    continue
                cxg, cyg = gx * 640 / W, gy * 480 / H
                inside_old = y * 480 / H >= 405 or (x * 640 / W >= 533 and y * 480 / H < 420)
                ok = cyg < 405 and not (cxg >= 533 and cyg < 420) and button_at(buttons, cxg, cyg) is None
                if not inside_old and (gx, gy) != (x, y):
                    ok = False
                if not ok:
                    bad += 1
                    if bad <= 3:
                        errs.append(f'world point ({x},{y}): game cursor at ({gx},{gy}) [canvas {cxg:.1f},{cyg:.1f}]'
                                    f' {"on " + button_at(buttons, cxg, cyg) if button_at(buttons, cxg, cyg) else "in its HUD" if inside_old else "moved"}')
            # 3. menus: movement passed through unchanged
            out = run_harness(exe, head, ['Q 1 1 1', f'C {W // 2} {H - 20}', 'D -400 5 0 0'])
            if (int(out[0][1]), int(out[0][2])) != (W // 2 - 400, H - 15):
                errs.append(f'menu: game cursor at {out[0][1]},{out[0][2]}, expected {W // 2 - 400},{H - 15}')
            # 4. right-drag camera: the game holds its cursor; the real mouse stays too
            out = run_harness(exe, head, ['Q 0 1 1', f'C {W // 2} {H // 3}', 'D 0 0 0 0', 'D 50 20 2 1', 'D 50 20 2 1'])
            if any((int(o[1]), int(o[2]), int(o[3]), int(o[4])) != (W // 2, H // 3, W // 2, H // 3) for o in out):
                errs.append(f'right-drag: cursor moved: {out}')
            # 5. drawing: the cursor sprite is drawn at the real mouse (gameplay frame, over the bar)
            tx = int(round(drawn[[n for *_, n in cs].index('inventory')]))
            ty = int(447 * H / 480)
            out = run_harness(exe, head, ['Q 0 1 1', f'C {sx} {sy}', 'D 0 0 0 0', f'D {tx - sx} {ty - sy} 0 0'])
            gx = int(out[1][1])
            res = subprocess.run([exe], input=head + '\n'.join(['Q 0 1 1', f'C {sx} {sy}', 'D 0 0 0 0', f'D {tx - sx} {ty - sy} 0 0',
                                 'F', f'B 004A9195 52 6'] + [f'{a} {b} 0.0001 0.99999' for a, b in
                                 ((gx, ty), (gx + 32, ty), (gx + 32, ty + 32), (gx, ty), (gx + 32, ty + 32), (gx, ty + 32))] + ['R']) + '\n',
                                 capture_output=True, text=True)
            pts = [tuple(map(float, l.split())) for l in res.stdout.split('\n') if l and l[0] in '0123456789-']
            if not pts or abs(min(p[0] for p in pts) - tx) > 0.5:
                errs.append(f'cursor sprite drawn at x {min(p[0] for p in pts) if pts else "?"}, real mouse at {tx}')
            # 5b. same in the world strip below the bar line: the sprite is drawn at the real x and y
            wx, wy = 4, H - 40
            out = run_harness(exe, head, ['Q 0 1 1', f'C {sx} {sy}', 'D 0 0 0 0', f'D {wx - sx} {wy - sy} 0 0'])
            gx, gy = int(out[1][1]), int(out[1][2])
            quadv = ((gx, gy), (gx + 32, gy), (gx + 32, gy + 32), (gx, gy), (gx + 32, gy + 32), (gx, gy + 32))
            res = subprocess.run([exe], input=head + '\n'.join(['Q 0 1 1', f'C {sx} {sy}', 'D 0 0 0 0', f'D {wx - sx} {wy - sy} 0 0',
                                 'F', 'B 004A9195 52 6'] + [f'{a} {b} 0.0001 0.99999' for a, b in quadv] + ['R']) + '\n',
                                 capture_output=True, text=True)
            pts = [tuple(map(float, l.split())) for l in res.stdout.split('\n') if l and l[0] in '0123456789-']
            if not pts or abs(min(p[0] for p in pts) - wx) > 0.5 or abs(min(p[1] for p in pts) - wy) > 0.5:
                errs.append(f'cursor sprite in the world strip drawn at {min(p[0] for p in pts) if pts else "?"},'
                            f'{min(p[1] for p in pts) if pts else "?"}, real mouse at {wx},{wy}')
            fails += bool(errs)
            print(f"{'ok  ' if not errs else 'FAIL'} hud mouse        [{label:32s}] {len(cs)} buttons, {len(pts2)} screen points, sweeps, menu, right-drag, cursor drawing"
                  + ('' if not errs else ':\n       ' + '\n       '.join(errs[:8])))
    return fails

def font_dir():
    """Folder with the game's .fnt files: SBM_FONT_DIR, or SBM_GAME_DIR/data. None if not set."""
    d = os.environ.get('SBM_FONT_DIR') or (os.path.join(os.environ['SBM_GAME_DIR'], 'data') if os.environ.get('SBM_GAME_DIR') else None)
    return d if d and os.path.isdir(d) else None

def load_fonts(d):
    """{font name: [(code, material, u, v, a, b), ...]} from the game's .fnt files (u16 count, 32-byte records)."""
    import struct
    out = {}
    for f in sorted(os.listdir(d)):
        if not f.lower().endswith('.fnt'):
            continue
        data = open(os.path.join(d, f), 'rb').read()
        n = struct.unpack_from('<H', data, 0)[0]
        out[f[:-4]] = [struct.unpack_from('<HHiiiii', data, 2 + 32 * i) for i in range(n)]
    return out

def text_decode_tests(exe):
    """Every glyph of every font, stored the way the game's draw code stores it (u spans +0x14,
    v spans +0x10), must decode back to its character (text_decode.inc)."""
    d = font_dir()
    if not d:
        print('skip text decoding: set SBM_GAME_DIR (game folder) or SBM_FONT_DIR to check it against the fonts')
        return 0
    fonts = load_fonts(d)
    cmds, want = [f'K {d}'], []
    for name, recs in fonts.items():
        glyphs = [(code, mat, u, v, a, b) for code, _, mat, u, v, a, b in recs if mat and a > 0 and b > 0]
        for i in range(0, len(glyphs), 10):
            chunk = glyphs[i:i + 10]
            cmds.append(f'X {len(chunk)} {chunk[0][1]}')
            for code, mat, u, v, a, b in chunk:
                cmds.append(f'{u} {v} {u + b} {v} {u + b} {v + a}')       # three corners: u..u+[+0x14], v..v+[+0x10]
            want.append([c[0] for c in chunk])
    res = subprocess.run([exe], input='\n'.join(cmds) + '\n', capture_output=True, text=True)
    got = [list(map(int, l.split()[1:])) for l in res.stdout.split('\n') if l.startswith('X')]
    errs = []
    for w, g in zip(want, got):
        codes = g[0::2]
        if codes != w:
            errs.append(f'decoded {codes} expected {w}')
    # line assembly (hover_log.inc h_lines): one line drawn as two batches (capital in one font
    # texture, the rest in another) must come out as one catalogue line with its space
    f16 = {code: (mat, u, v, a, b) for code, _, mat, u, v, a, b in fonts.get('16xB', []) if mat}
    if all(ord(c) in f16 for c in 'Combscren'):
        x, quads = 1000.0, []
        for c in 'Combo screen':
            if c == ' ':
                x += 6; continue
            mat, u, v, a, b = f16[ord(c)]
            quads.append((c, mat, u, v, a, b, x)); x += b + 1
        lines = [f'K {d}', 'G 1', 'S 3840 2160', 'F']
        for group in ([q for q in quads if q[1] == quads[0][1]], [q for q in quads if q[1] != quads[0][1]]):
            if not group:
                continue
            for i in range(0, len(group), 10):
                g = group[i:i + 10]
                lines.append(f'X {len(g)} {g[0][1]}')
                lines += [f'{u} {v} {u + b} {v} {u + b} {v + a}' for _, _, u, v, a, b, _ in g]
                lines.append(f'B 0042B0BC {g[0][1]:X} {6 * len(g)}')
                for _, _, u, v, a, b, gx in g:
                    for cx, cy in ((0, 0), (1, 0), (1, 1), (0, 0), (1, 1), (0, 1)):
                        lines.append(f'{gx + cx * b} {900 + cy * a} 0.0001 0.99999')
        lines += ['R', 'F']
        hb = {code: (mat, u, v, a, b) for code, _, mat, u, v, a, b in fonts.get('hbut', []) if mat}
        if all(ord(c) in hb for c in 'COMB'):
            x = 1004.0                                   # a label 13 px above the line (the COMBO slot label case)
            for c in 'COMBO':
                mat, u, v, a, b = hb[ord(c)]
                lines[-2:-2] = [f'X 1 {mat}', f'{u} {v} {u + b} {v} {u + b} {v + a}', f'B 0042B0BC {mat:X} 6'] + [
                    f'{x + cx * b} {887 + cy * a} 0.0001 0.99999' for cx, cy in ((0, 0), (1, 0), (1, 1), (0, 0), (1, 1), (0, 1))]
                x += b + 1
        res = subprocess.run([exe], input='\n'.join(lines) + '\n', capture_output=True, text=True)
        if 'text: "COMBO" font hbut' not in res.stderr and all(ord(c) in hb for c in 'COMB'):
            errs.append('a label just above the line was mixed into it: '
                        + ' | '.join(l for l in res.stderr.split('\n') if l.startswith('text:'))[:200])
        if 'text: "Combo screen" font 16xB' not in res.stderr:
            errs.append('catalogue line for "Combo screen" (two batches) not found: '
                        + ' | '.join(l for l in res.stderr.split('\n') if l.startswith('text:'))[:200])
    # clipped glyphs (one texel narrower and shorter at a slot edge) decode by their top-left corner
    clip_cmds, clip_want = [f'K {d}'], []
    for name in ('16xY', 'hbut', '5PxB'):
        g = [(code, mat, u, v, a, b) for code, _, mat, u, v, a, b in fonts.get(name, []) if mat and a > 1 and b > 1][:30]
        for code, mat, u, v, a, b in g:
            clip_cmds += [f'X 1 {mat}', f'{u} {v} {u + b - 1} {v} {u + b - 1} {v + a - 1}']
            clip_want.append(code)
    res = subprocess.run([exe], input='\n'.join(clip_cmds) + '\n', capture_output=True, text=True)
    clip_got = [int(l.split()[1]) for l in res.stdout.split('\n') if l.startswith('X ') and len(l.split()) > 1]
    if clip_got != clip_want:
        bad = [(chr(w), g) for w, g in zip(clip_want, clip_got) if w != g][:5]
        errs.append(f'clipped glyphs: {len(clip_want)} tried, wrong: {bad}')
    n = sum(len(w) for w in want)
    print(f"{'ok  ' if not errs and len(got) == len(want) else 'FAIL'} text decoding: {n} glyphs from {len(fonts)} fonts decoded back to their characters; clipped glyphs; batches joined into lines, labels kept apart"
          + ('' if not errs else ':\n       ' + '\n       '.join(errs[:5])))
    return 1 if errs or len(got) != len(want) else 0

def text_hook_tests(exe):
    """Game-side text scaling (text_scale.inc, [Debug] TextHook): every font is loaded the way the
    game loads it, scaled, and every glyph drawn the way 0x433714 draws it. The quad must be the
    scaled size, the texture rectangle the original one (also when clipped), advances and line
    heights scaled; a second scaling must not happen; 100% must change nothing. Mutation check:
    without the texel rewrite the texture rectangles must come out wrong."""
    import struct
    d = font_dir()
    if not d:
        print('skip text hook: set SBM_GAME_DIR (game folder) or SBM_FONT_DIR to check it against the fonts')
        return 0
    fails = 0
    def run(name, pct, glyphs, nofix=0):
        cmds = [f'N {nofix}', f'Z {d} {name} {pct} {len(glyphs)}'] + [' '.join(map(str, g)) for g in glyphs]
        out = subprocess.run([exe], input='\n'.join(cmds) + '\n', capture_output=True, text=True).stdout.split('\n')
        zf = next(l.split()[1:] for l in out if l.startswith('ZF'))
        return list(map(int, zf)), [l.split()[1:] for l in out if l.startswith('ZG')]
    rnd = lambda x, p: (x * p + 50) // 100 if x >= 0 else -((-x * p + 50) // 100)
    checked = 0
    for f in sorted(os.listdir(d)):
        if not f.lower().endswith('.fnt'):
            continue
        name = f[:-4]
        data = open(os.path.join(d, f), 'rb').read()
        n = struct.unpack_from('<H', data, 0)[0]
        recs = [struct.unpack_from('<HHiiiiiih', data, 2 + 32 * i) for i in range(n)]   # code, -, mat, u, v, w, h, -, yoff
        if name.lower() not in ('5pxb', '5pxg', '5pxr', '5pxx', '5pxy', '16xb', '16xx', '16xy', 'phil', 'hbut', 'magc', 'mgrn', 'mred', 'cred', 'numg', 'numr'):
            continue
        lh = max(r[8] + r[6] for r in recs)
        for pct in (150, 200, 300):
            lhs = max(rnd(r[8], pct) + rnd(r[6], pct) for r in recs)       # line height: max of the scaled glyphs
            glyphs = [(r[0], 1000, 500, -9999, -9999, 9999, 9999) for r in recs]
            # clipped: right half and top third cut off by the clip rect
            clipped = [(r[0], 1000, 500, -9999, 500 + rnd(r[8], pct) + rnd(r[6], pct) // 3, 1000 + rnd(r[5], pct) // 2, 9999)
                       for r in recs if r[2] and r[5] >= 4 and r[6] >= 4]
            zf, out = run(name, pct, glyphs + clipped)
            errs = []
            if zf[0] != 1: errs.append(f'not scaled (result {zf[0]})')
            if zf[4] != 2: errs.append(f'scaled twice (second result {zf[4]})')
            if zf[1] != lhs: errs.append(f'line height {zf[1]}, expected {lhs}')
            if zf[2] != rnd(1, pct): errs.append(f'line gap {zf[2]}, expected {rnd(1, pct)}')
            if zf[5] != rnd(-1, pct): errs.append(f'letter spacing -1 set to {zf[5]}, expected {rnd(-1, pct)}')
            if zf[6:10] != [1, lhs, rnd(1, pct), rnd(-1, pct)]:
                errs.append(f'after a reload into the same object: result {zf[6]}, line height {zf[7]}, gap {zf[8]}, spacing {zf[9]}'
                            f' (expected 1, {lhs}, {rnd(1, pct)}, {rnd(-1, pct)}: scaled once, not twice)')
            for r, o in zip(recs, out[:len(recs)]):
                code, _, mat, u, v, w, h, _, yo = r
                if o[0] == '-':
                    if mat and w > 0 and h > 0: errs.append(f'glyph {code} vanished')
                    continue
                x0, y0, x1, y1, u0, v0, u1, v1, adv = map(int, o)
                if (x1 - x0, y1 - y0, y0 - 500) != (rnd(w, pct), rnd(h, pct), rnd(yo, pct)):
                    errs.append(f'glyph {code}: quad {x1 - x0}x{y1 - y0} at +{y0 - 500}, expected {rnd(w, pct)}x{rnd(h, pct)} at +{rnd(yo, pct)}')
                elif mat and (u0, v0, u1, v1) != (u, v, u + w, v + h):
                    errs.append(f'glyph {code}: texels {u0},{v0}..{u1},{v1}, expected {u},{v}..{u + w},{v + h}')
                if adv != rnd(w, pct) + zf[9] + 1:                # width + spacing (the setter's -1, scaled) + 1
                    errs.append(f'glyph {code}: advance {adv}, expected {rnd(w, pct) + zf[9] + 1}')
                if len(errs) > 3: break
            byc = {r[0]: r for r in recs}
            for g, o in zip(clipped, out[len(recs):]):
                code, _, mat, u, v, w, h, _, yo = byc[g[0]]
                if o[0] in '-?':
                    errs.append(f'clipped glyph {code} vanished'); break
                x0, y0, x1, y1, u0, v0, u1, v1, _ = map(int, o)
                eu = u + round((x1 - 1000) * w / rnd(w, pct)); ev0 = v + round((y0 - 500 - rnd(yo, pct)) * h / rnd(h, pct))
                if not (u0 == u and abs(u1 - eu) <= 1 and abs(v0 - ev0) <= 1 and v1 == v + h and u1 <= u + w):
                    errs.append(f'clipped glyph {code}: texels {u0},{v0}..{u1},{v1}, expected {u},{ev0}..{eu},{v + h}')
                    break
            checked += len(out)
            if errs:
                fails += 1
                print(f"FAIL text hook {name} at {pct}%: " + '; '.join(errs[:4]))
        zf, out = run(name, 100, [(r[0], 1000, 500, -9999, -9999, 9999, 9999) for r in recs if r[2]])
        if zf[0] != 0 or any(list(map(int, o[4:8])) != [r[3], r[4], r[3] + r[5], r[4] + r[6]] for r, o in zip([r for r in recs if r[2]], out)):
            fails += 1; print(f'FAIL text hook {name} at 100%: changed something')
    if not checked:
        print('skip text hook: no fonts found'); return 0
    # mutation: without the texel rewrite, a scaled glyph shows the wrong part of the texture
    name = next(f[:-4] for f in sorted(os.listdir(d)) if f.lower() == '16xb.fnt')
    data = open(os.path.join(d, name + '.fnt'), 'rb').read()
    r = struct.unpack_from('<HHiiiiiih', data, 2 + 32 * next(i for i in range(struct.unpack_from('<H', data)[0])
                                                              if struct.unpack_from('<H', data, 2 + 32 * i)[0] == ord('A')))
    _, out = run(name, 200, [(ord('A'), 1000, 500, -9999, -9999, 9999, 9999)], nofix=1)
    mut_ok = list(map(int, out[0][4:8])) != [r[3], r[4], r[3] + r[5], r[4] + r[6]]
    if not mut_ok:
        fails += 1
    print(f"{'ok  ' if not fails else 'FAIL'} text hook: {checked} glyphs of every font at 150/200/300% "
          f"(scaled quads, original texture rectangles, clipped too; scaled once; 100% unchanged); "
          f"mutation (no texel rewrite) {'caught' if mut_ok else 'NOT caught'}")
    # tooltip rules follow the game's larger text: a one-line tooltip drawn by the game at 200%
    # (box 64 px tall, above the cursor) must still be recognised as the cursor's tooltip
    def quad(x0, y0, x1, y1):
        return [f'{x} {y} 0.0001 0.99999' for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y0), (x1, y1), (x0, y1))]
    def width(gts):
        inp = ['S 3840 2160', 'H 1', 'A 0', 'L 1', 'T 200', 'V 1', f'Y {gts}', 'Q 0 1 1', 'C 1500 1500', 'F',
               'B 00456ABD 4D 6'] + quad(1498 - 17, 1498 - 56, 1498 - 17 + 90, 1498 + 8) + ['R']
        out = subprocess.run([exe], input='\n'.join(inp) + '\n', capture_output=True, text=True).stdout.split('\n')
        i = out.index(next(l for l in out if l.startswith('B ')))
        xs = [float(l.split()[0]) for l in out[i + 1:i + 7]]
        return max(xs) - min(xs)
    w2, w1 = width(200), width(100)
    ok = abs(w2 - 180) < 0.5 and abs(w1 - 90) < 0.5
    print(f"{'ok  ' if ok else 'FAIL'} tooltip rules with game-drawn text at 200%: a 64 px tall tooltip is recognised "
          f"({w2:.0f} px with the rule, expected 180; {w1:.0f} px without, expected 90)")
    # tooltip boxes never cover the cursor (2026-11-23): with the text hook at 300% the game's
    # tooltip box (82 px tall, placed for a 46 px box at -17,-38 from the cursor) grew over the
    # 200% cursor (64 px); it goes above the cursor, or below it at the top of the screen
    def box_y(cy, x0, y0, x1, y1):
        inp = ['S 3840 2160', 'H 1', 'A 0', 'L 1', 'T 100', 'V 1', 'Y 300', 'Q 0 1 1', f'C 1500 {cy}', 'F',
               'B 00456ABD 4D 6'] + quad(x0, y0, x1, y1) + ['R']
        out = subprocess.run([exe], input='\n'.join(inp) + '\n', capture_output=True, text=True).stdout.split('\n')
        i = out.index(next(l for l in out if l.startswith('B ')))
        ys = [float(l.split()[1]) for l in out[i + 1:i + 7]]
        return min(ys), max(ys)
    cases = []
    a0, a1 = box_y(1500, 1483, 1462, 1633, 1544)          # corner tooltip mid-screen: above the cursor
    cases.append(('above', a1 <= 1498 - 6 + 0.5 and abs((a1 - a0) - 82) < 0.5, (a0, a1)))
    b0, b1 = box_y(30, 1483, -8, 1633, 74)                # at the top: below the cursor (28 + 64 + 6)
    cases.append(('below at the top', b0 >= 28 + 64 + 6 - 0.5 and abs((b1 - b0) - 82) < 0.5, (b0, b1)))
    c0, c1 = box_y(1500, 1425, 1570, 1575, 1652)          # item tooltip 70 px below: already clear, unchanged
    cases.append(('item unchanged', abs(c0 - 1570) < 0.5 and abs(c1 - 1652) < 0.5, (c0, c1)))
    ok = all(c[1] for c in cases)
    if not ok:
        fails += 1
    print(f"{'ok  ' if ok else 'FAIL'} tooltip box vs cursor at 300% text, 200% cursor: " +
          ', '.join(f"{c[0]} {'ok' if c[1] else 'WRONG'} (y {c[2][0]:.0f}..{c[2][1]:.0f})" for c in cases))
    # text of another font inside a cursor tooltip's box is not the tooltip's text (2026-11-23: the
    # COMBO slot label, font 5px, sat inside the 82 px box at 300% and moved with the box)
    def rec_of(fname, ch):
        data = open(os.path.join(d, fname + '.fnt'), 'rb').read()
        for i2 in range(struct.unpack_from('<H', data)[0]):
            code, _, mat, u, v, w, h = struct.unpack_from('<HHiiiii', data, 2 + 32 * i2)
            if code == ord(ch) and mat:
                return mat, u, v, w, h
    def tquad(x0, y0, x1, y1, u, v, w, h):
        pts = ((x0, y0, u, v), (x1, y0, u + w, v), (x1, y1, u + w, v + h), (x0, y0, u, v), (x1, y1, u + w, v + h), (x0, y1, u, v + h))
        return [f'{x} {y} 0.0001 0.99999 {uu / 256} {vv / 256}' for x, y, uu, vv in pts]
    fnames = {f[:-4].lower(): f[:-4] for f in os.listdir(d) if f.lower().endswith('.fnt')}
    if 'hbut' in fnames and '5pxb' in fnames:
        hm, hu, hv, hw, hh = rec_of(fnames['hbut'], 'C')
        pm, pu, pv, pw, ph = rec_of(fnames['5pxb'], 'C')
        inp = ['S 3840 2160', 'H 1', 'A 0', 'L 1', 'T 100', 'V 1', 'Y 300', f'W {d}', 'Q 0 1 1', 'C 1500 1500', 'F',
               'B 00456ABD 4D 6'] + quad(1483, 1462, 1633, 1544) + \
              [f'b 0042B0BC {hm:X} 6'] + tquad(1502, 1480, 1502 + 3 * hw, 1480 + 3 * hh, hu, hv, hw, hh) + \
              [f'b 0042B0BC {pm:X} 6'] + tquad(1560, 1500, 1560 + 2 * pw, 1500 + 2 * ph, pu, pv, pw, ph) + ['R']
        out = subprocess.run([exe], input='\n'.join(inp) + '\n', capture_output=True, text=True).stdout.split('\n')
        starts = [k for k, l in enumerate(out) if l.startswith('B ')]
        ys = [[float(l.split()[1]) for l in out[k + 1:k + 7]] for k in starts[:3]]
        dy_box = min(ys[0]) - 1462
        tip_ok = abs((min(ys[1]) - 1480) - dy_box) < 0.5 and dy_box < -1
        label_ok = abs(min(ys[2]) - 1500) < 0.5
        ok = tip_ok and label_ok
        if not ok:
            fails += 1
        print(f"{'ok  ' if ok else 'FAIL'} tooltip text by font at 300%: box moved {dy_box:.0f} px, its hbut text "
              f"{'with it' if tip_ok else 'NOT with it'}, a 5px label inside the box {'stays' if label_ok else 'MOVED'}")
    else:
        print('skip tooltip text by font: hbut/5pxb fonts not found')
    # the name of the character under the cursor (font mgrn) is drawn inside a cursor tooltip box and
    # moves with it (session 2026-11-23 18:57: "Shopkeeper" at 300%: box 3081..3303 x 454..536, cursor
    # sprite at 3070,460; the box went above the cursor, the name stayed under it and the glyphs past
    # the HUD column edge were moved right with the column)
    if 'mgrn' in fnames:
        gm, gu, gv, gw, gh = rec_of(fnames['mgrn'], 'e')
        inp = ['S 3840 2160', 'H 1', 'A 0', 'L 1', 'T 100', 'V 1', 'Y 300', f'W {d}', 'Q 0 1 1', 'C 3072 462', 'F',
               'B 00456ABD 4D 6'] + quad(3081, 454, 3303, 536) + \
              [f'b 0042B0BC {gm:X} 6'] + tquad(3100, 486, 3115, 504, gu, gv, gw, gh) + \
              [f'b 0042B0BC {gm:X} 6'] + tquad(3270, 486, 3285, 504, gu, gv, gw, gh) + ['R']
        out = subprocess.run([exe], input='\n'.join(inp) + '\n', capture_output=True, text=True).stdout.split('\n')
        starts = [k for k, l in enumerate(out) if l.startswith('B ')]
        pts = [[tuple(map(float, l.split()[:2])) for l in out[k + 1:k + 7]] for k in starts[:3]]
        bdx, bdy = min(p[0] for p in pts[0]) - 3081, min(p[1] for p in pts[0]) - 454
        moved = [(min(p[0] for p in q) - x, min(p[1] for p in q) - 486) for q, x in ((pts[1], 3100), (pts[2], 3270))]
        ok = bdy < -1 and all(abs(mx - bdx) < 0.5 and abs(my - bdy) < 0.5 for mx, my in moved)
        if not ok:
            fails += 1
        print(f"{'ok  ' if ok else 'FAIL'} name under the cursor (mgrn) at 300%: box moved ({bdx:.0f},{bdy:.0f}), "
              f"name glyphs moved " + ', '.join(f'({mx:.0f},{my:.0f})' for mx, my in moved))
    else:
        print('skip name under the cursor: mgrn font not found')
    return fails + (0 if ok else 1)

def font_places_tests(exe):
    """Per-place fonts (font_places.inc, [Debug] FontPlaces): every place of the 16xy font gets a
    deep copy of the font object when the game loads it. Copies must equal the original with no
    memory shared, scale independently (records, line height, kerning, spacing setter), keep
    their address and scale once when the game reloads the font, and map back to their place.
    Mutation check: a copy that shares the original's kerning must be caught. With the game's
    exes in SBM_GAME_DIR, every site's bytes must be the instruction the DLL redirects."""
    import re, struct
    errs = []
    src = open(os.path.join(ROOT, 'src', 'font_places.inc')).read()
    sites = [(int(a, 16), int(g, 16), int(i), int(h)) for a, g, i, h in re.findall(r'\{ 0x([0-9A-F]+), 0x([0-9A-F]+), (\d), (\d) \}', src)]
    gd = os.environ.get('SBM_GAME_DIR')
    nexe = 0
    for name in ('SoulbringeVCnoeax.exe', 'SoulbringeVC.exe'):
        path = os.path.join(gd, name) if gd else None
        if not path or not os.path.exists(path):
            continue
        nexe += 1
        d = open(path, 'rb').read()
        for a, g, i, h in sites:
            o = a - 0x401000 + 0x400                     # .text: VA 0x401000 at file offset 0x400
            ins = d[o:o + i + 4]
            op_ok = (ins[0] == 0x8b and (ins[1] & 0xc7) == 0x05) if i == 2 else ins[:1] == b'\xa1'
            imm = struct.unpack_from('<I', ins, i)[0]
            if h == 0:   # mov [reg+0x208],reg soon after
                store = re.search(rb'\x89[\x80-\xbf]\x08\x02\x00\x00', d[o + i + 4:o + i + 40])
            elif h == 1: # onto the stack as an argument: mov [esp],r32 or mov [esp+disp8],r32
                store = re.search(rb'\x89[\x04\x0c\x14\x1c\x34\x3c]\x24|\x89[\x44\x4c\x54\x5c\x74\x7c]\x24', d[o + i + 4:o + i + 20])
            else:        # measuring: read into ecx for a font method / its spacing (+0x2C) soon after
                store = re.search(rb'\x8b\x49\x2c|\xe8', d[o + i + 4:o + i + 16])
            if not op_ok or imm != g or not store:
                errs.append(f'{name} {a:06X}: {ins.hex(" ")} (expected the read of {g:08X}, then ' + ('a store to +0x208)' if h == 0 else 'an argument push)' if h == 1 else 'a measuring use)'))
    d = font_dir()
    if not d:
        print('skip font places: set SBM_GAME_DIR (game folder) or SBM_FONT_DIR to check it against the fonts')
        return 1 if errs else 0
    def tm(x, p):
        return (x * p + 50) // 100 if x >= 0 else -((-x * p + 50) // 100)
    po, pa, pb = 200, 150, 300
    out = run_harness(exe, '', [f'O {d} 16xY {po} {pa} {pb} 0', f'O {d} 16xY {po} {pa} {pb} 1'])
    good, mut = out[:3], out[3:]
    def expect(rows, label):
        e = []
        of, orr, op = [list(map(int, r[1:])) for r in rows]
        made, same, k0, w0 = of[:4]
        n16 = sum(1 for s in sites if s[1] == 0x35565DC)
        if made != n16 or not same:
            e.append(f'{label}: {made} of {n16} copies made, deep and unshared: {bool(same)}')
        want = [None, None, None, tm(w0, po), tm(w0, pa), tm(w0, pb), tm(k0, po), tm(k0, pa), tm(k0, pb), tm(-1, po), tm(-1, pa), tm(-1, pb)]
        for name_, row in (('first load', of[4:]), ('reload', orr[3:])):
            got = row[3:12]
            if got != want[3:12]:
                e.append(f'{label} {name_}: widths/kerning/spacing {got}, expected {want[3:12]}')
            if not (row[1] < row[0] < row[2]):
                e.append(f'{label} {name_}: line heights {row[:3]} not ordered by scale')
        if orr[3:6] != of[4:7]:
            e.append(f'{label}: line heights changed on reload {of[4:7]} -> {orr[3:6]} (scaled twice?)')
        if orr[0] != n16 or not orr[1] or orr[2] != 2:
            e.append(f'{label}: reload refreshed {orr[0]}, addresses kept {bool(orr[1])}, refreshes {orr[2]}')
        if op != [0, 1, -1, len(sites)]:
            e.append(f'{label}: places {op}')
        return e
    errs += expect(good, 'copies')
    if not expect(mut, 'shallow kerning'):
        errs.append('mutation not caught: a copy sharing the kerning passed')
    fonts = {0x35565DC: '16xy', 0x213F3D8: 'magc', 0x1E78684: 'magc2', 0x213ACF4: 'hbut', 0x213F3DC: 'hbut2', 0x213ACF0: 'mgrn', 0x213ACF8: 'mred', 0x213F3E0: '16xb', 0x213F3E4: '16xx'}
    unknown = [f'{s[1]:08X}' for s in sites if s[1] not in fonts]
    if unknown:
        errs.append(f'sites of an unknown global: {unknown}')
    print(f"{'ok  ' if not errs else 'FAIL'} font places: {len(sites)} places (" + ', '.join(f'{v} {sum(1 for s in sites if s[1] == k)}' for k, v in fonts.items()) + f"; {sum(1 for s in sites if s[3] == 1)} by argument, {sum(1 for s in sites if s[3] == 2)} measuring), deep copies scaled on their own (150/200/300%), reload refreshes once, "
          f"spacing follows the setter, shallow copy caught; site bytes checked in {nexe} exe(s)"
          + ('' if not errs else ':\n       ' + '\n       '.join(errs[:8])))
    if not nexe:
        print('     (site bytes not checked: put SoulbringeVCnoeax.exe / SoulbringeVC.exe in SBM_GAME_DIR)')
    return 1 if errs else 0

def place_sizes_tests(exe):
    """Per-place sizes (font_places.inc, phase 5b step 2): a place's copy is scaled to its own size
    ([PlaceTextScale] <site>=<pct>), every other place and the original to the font kind's size;
    0 / unset = the kind's; sizes are clamped to 50-600 and only known site addresses are taken;
    the font page scale sees the largest place size and the tooltip box rules the largest size of
    the cursor-tooltip places; a reload scales every copy once again at the same size, and the
    letter spacing setter follows each copy's size. Mutation check: copies at the kind's size
    (the place sizes ignored) must fail."""
    d = font_dir()
    if not d:
        print('skip place sizes: set SBM_GAME_DIR (game folder) or SBM_FONT_DIR to check it against the fonts')
        return 0
    import re
    src = open(os.path.join(ROOT, 'src', 'font_places.inc')).read()
    sites = [int(a, 16) for a, g in re.findall(r'\{ 0x([0-9A-F]+), 0x([0-9A-F]+), \d, \d \}', src)]
    def tm(x, p):
        return (x * p + 50) // 100 if x >= 0 else -((-x * p + 50) // 100)
    kind = 200
    # options menu smaller, character screen larger, HUD label at the kind's size explicitly,
    # one clamped low, one clamped high, one reset to 0, one cursor-tooltip place (hbut), one unknown address
    sizes = [(0x4471BB, 150), (0x66B65E, 300), (0x41E939, 200), (0x68A418, 20), (0x443D38, 900),
             (0x64EF68, 0), (0x5567BE, 350), (0x123456, 250)]
    want = {0x4471BB: 150, 0x66B65E: 300, 0x41E939: 200, 0x68A418: 50, 0x443D38: 600}
    def run(mut):
        lines = [f'J {d} 16xY {kind} {mut} {len(sizes)}'] + [f'{a:X} {p}' for a, p in sizes]
        return run_harness(exe, '', lines)
    def check(out, label):
        e = []
        js = [int(r[1]) for r in out if r[0] == 'JS']
        exp_idx = [sites.index(a) if a in sites else -1 for a, _ in sizes]
        if js != exp_idx:
            e.append(f'{label}: fp_set_pct indexes {js}, expected {exp_idx}')
        jm = [list(map(int, r[1:])) for r in out if r[0] == 'JM'][0]
        if jm != [600, 350]:
            e.append(f'{label}: largest place size {jm[0]} (expected 600), tooltip rules size {jm[1]} (expected 350)')
        jl = [list(map(int, r[1:])) for r in out if r[0] == 'JL']
        jp = [r[1:] for r in out if r[0] == 'JP']
        if len(jl) != 2:
            return e + [f'{label}: font not loaded']
        lh0, w0 = None, None
        for load, olh, ow in jl:
            # the original: always the kind's size, whatever the places are set to (FontPlaces=0 is this font alone)
            if lh0 is None:
                lh0, w0 = olh, ow
            elif (olh, ow) != (lh0, w0):
                e.append(f'{label}: original changed on reload {lh0},{w0} -> {olh},{ow}')
        rows = {}
        for load, site, pct, lh, w, k0, sp in jp:
            rows.setdefault(int(site, 16), []).append(tuple(map(int, (load, pct, lh, w, k0, sp))))
        n16 = sum(1 for r in re.findall(r'\{ 0x[0-9A-F]+, 0x35565DC', src))
        if len(rows) != n16:
            e.append(f'{label}: {len(rows)} 16xy places reported, expected {n16}')
        # widths of the unscaled font, from a place at the kind's size: w(kind) = tm(w_raw, kind)
        ref = {}
        for site, rr in rows.items():
            p = want.get(site, kind)
            for load, pct, lh, w, k0, sp in rr:
                if pct != p:
                    e.append(f'{label}: {site:06X} load {load} sized {pct}%, expected {p}%')
                ref.setdefault(p, set()).add((lh, w, k0, sp))
        for p, vals in ref.items():
            if len(vals) != 1:
                e.append(f'{label}: places at {p}% differ (or changed on reload): {sorted(vals)[:3]}')
        # sizes relate to the kind's copy by the ratio of their scales (widths, kerning, spacing setter -1)
        if kind in ref and len(ref[kind]) == 1:
            lhk, wk, kk, spk = next(iter(ref[kind]))
            if (lhk, wk) != (lh0, w0):
                e.append(f'{label}: a place at the kind size ({lhk},{wk}) differs from the original ({lh0},{w0})')
            raw_w = None
            for cand in range(1, 64):
                if tm(cand, kind) == wk:
                    raw_w = cand; break
            for p, vals in ref.items():
                if len(vals) != 1:
                    continue
                lh, w, k0, sp = next(iter(vals))
                if raw_w is not None and w != tm(raw_w, p):
                    e.append(f'{label}: width at {p}% is {w}, expected {tm(raw_w, p)}')
                if sp != tm(-1, p):
                    e.append(f'{label}: spacing at {p}% is {sp}, expected {tm(-1, p)}')
                if p != kind and (lh - lhk) * (p - kind) <= 0:
                    e.append(f'{label}: line height at {p}% ({lh}) not on the right side of {kind}% ({lhk})')
        return e
    errs = check(run(0), 'place sizes')
    mut = check(run(1), 'mutation')
    if not mut:
        errs.append('mutation not caught: copies at the kind size passed')
    print(f"{'ok  ' if not errs else 'FAIL'} place sizes: 16xY at {kind}%, places at 150/300/200%, clamped to 50/600, 0 = kind, unknown address refused; "
          f"original stays at the kind size; same sizes after a reload; widths, line heights and spacing follow each place's size; "
          f"page scale sees 600%, tooltip rules 350%; mutation (sizes ignored) caught"
          + ('' if not errs else ':\n       ' + '\n       '.join(errs[:8])))
    return 1 if errs else 0

def shipped_ini():
    """The shipped SoulbringerModern.ini as {section: {key: value}} (inline ';' comments dropped)."""
    import configparser
    cp = configparser.ConfigParser(inline_comment_prefixes=(';',), interpolation=None, strict=True)
    cp.optionxform = str
    cp.read(os.path.join(ROOT, 'dist', 'SoulbringerModern.ini'))
    return {sec: dict(cp[sec]) for sec in cp.sections()}

def shipped_font_default(key):
    """[Fonts] in the shipped ini: All=Sharp and this font left empty (it follows All: the sharp face)."""
    f = shipped_ini().get('Fonts', {})
    e = []
    if f.get('All') != 'Sharp':
        e.append(f"shipped ini: [Fonts] All={f.get('All')} (expected Sharp)")
    if f.get(key, None) != '':
        e.append(f"shipped ini: [Fonts] {key}={f.get(key)} (expected empty: follows All)")
    return e

def ts_table():
    """ts_key in font_places.inc: [(section, key, parent, kind, pk, places)]."""
    import re
    src = open(os.path.join(ROOT, 'src', 'font_places.inc')).read()
    txt = src[src.index('ts_key[] = {'):src.index('#define TS_N')]
    rows = re.findall(r'\{ "([\w.]+)", "(\w+)", (-?\d+), (-?\d+), (-?\d+), "(?:[^"\\]|\\.)*",\s+\{([^}]*)\} \}', txt)
    return [(sec, key, int(par), int(kind), int(pk), [int(a, 16) for a in re.findall(r'0x([0-9A-F]+)', pl)]) for sec, key, par, kind, pk, pl in rows]

def place_groups_tests(exe):
    """Text size settings ([Text], [Text.Menus], [Text.Screens], [Text.Game]; ts_key in
    font_places.inc, 2026-11-28): the table (parents before items, one setting per font kind,
    every place known and in one item only, keys unique), the shipped ini (every setting, no
    0.2-dev key left), and the sizes: the harness against a separate implementation of the lookup
    order (item -> category All -> Size -> 200) on fixed and random settings, an item getting
    its own copies only when it differs from its font kind, a [PlaceTextScale] override winning,
    place names by load site, the tooltip rules following the Names item."""
    import re, random
    errs = []
    src = open(os.path.join(ROOT, 'src', 'font_places.inc')).read()
    sites = [int(a, 16) for a in re.findall(r'\{ 0x([0-9A-F]+), 0x[0-9A-F]+, \d, \d \}', src)]
    loads = [int(a, 16) for a in re.findall(r'^\s+0x([0-9A-F]+),\s*$', src[src.index('fp_load[] = {'):src.index('#define FP_NLOAD')], re.M)]
    site_kind = {}
    gk = {'35565DC': 3, '213F3E0': 3, '213F3E4': 3, '213F3D8': 2, '1E78684': 2, '213ACF4': 1, '213F3DC': 1, '213ACF0': 4, '213ACF8': 4}
    for a, g in re.findall(r'\{ 0x([0-9A-F]+), 0x([0-9A-F]+), \d, \d \}', src):
        site_kind[int(a, 16)] = gk.get(g, -1)
    site_kind[0x6721CF] = 1                      # the place-name load site loads hbut
    T = ts_table()
    names = [f'{sec}/{key}' for sec, key, *_ in T]
    if len(T) < 15:
        errs.append(f'only {len(T)} settings parsed from ts_key')
    if len(set(n.lower() for n in names)) != len(names):
        errs.append('duplicate settings')
    kinds = [r[3] for r in T if r[3] >= 0]
    if sorted(kinds) != list(range(7)):
        errs.append(f'font kinds covered: {sorted(kinds)} (each of 0-6 exactly once)')
    seen = {}
    for i, (sec, key, par, kind, pk, pl) in enumerate(T):
        if i == 0 and par != -1 or i > 0 and not 0 <= par < i:
            errs.append(f'{names[i]}: parent {par} (must come before it)')
        if bool(pl) != (pk >= 0):
            errs.append(f'{names[i]}: places {len(pl)} but place kind {pk}')
        for a in pl:
            if a not in sites and a not in loads:
                errs.append(f'{names[i]}: {a:06X} is not a place')
            elif site_kind.get(a) != pk:
                errs.append(f'{names[i]}: {a:06X} is font kind {site_kind.get(a)}, not {pk}')
            if a in seen:
                errs.append(f'{a:06X} in two settings: {seen[a]} and {names[i]}')
            seen[a] = names[i]
    ini = shipped_ini()
    for sec, key, *_ in T:
        want = '200' if (sec, key) == ('Text', 'Size') else '0'
        if ini.get(sec, {}).get(key) != want:
            errs.append(f'shipped ini: [{sec}] {key}={ini.get(sec, {}).get(key)} (expected {want})')
    raw = open(os.path.join(ROOT, 'dist', 'SoulbringerModern.ini')).read()
    old = re.findall(r'^(\w*TextScale|InterfaceFont|NumberFont|CreditsFont|MenuFont|NameFont|TooltipFont|JournalFont|FontPages|FontRedraw|FontFace|FontFile|FontWeight|FontOwnMetrics|CursorScale|HudLayout|\w+FontFace|\w+FontFile|\w+FontWeight)\s*=', raw, re.M)
    if old or re.search(r'^\[(UI|PlaceTextScale)\]', raw, re.M):
        errs.append(f'shipped ini still has 0.2-dev keys: {sorted(set(old))[:8]}')
    for sec, keys in ini.items():
        for k in ('TextHook', 'HudMouse', 'ConversationPortraitHold', 'FontPlaces'):
            if k in keys and sec != 'Debug':
                errs.append(f'{k} in [{sec}]: troubleshooting switches belong in [Debug]')
    if ini.get('Debug', {}).get('TextHook') != '1':
        errs.append('shipped ini: [Debug] TextHook is not 1 (the text hook is on for good)')

    def resolve(vals):
        """separate implementation: kinds, every setting's size, the place sizes ts_groups gives"""
        def pct(i):
            while i >= 0:
                v = vals.get(names[i], 0)
                if v > 0:
                    return max(50, min(600, v))
                i = T[i][2]
            return 200
        kp = [pct(0)] * 7
        for i, r in enumerate(T):
            if r[3] >= 0:
                kp[r[3]] = pct(i)
        size = {a: 0 for a in sites + loads}
        nset = {}
        for i, r in enumerate(T):
            nset[names[i]] = 0
            if r[4] >= 0 and r[5] and pct(i) != kp[r[4]]:
                for a in r[5]:
                    size[a] = pct(i)
                nset[names[i]] = len(r[5])
        return kp, {n: pct(i) for i, n in enumerate(names)}, size, nset

    def run(vals, over=()):
        inp = [f'I {len(vals)}'] + [f'{k} {v}' for k, v in vals.items()] + [f'IP {a:X} {p}' for a, p in over] + ['IP 0 0']
        out = run_harness(exe, '', inp)
        ig = {r[1]: int(r[2]) for r in out if r[0] == 'IG'}
        ik = [int(r[2]) for r in out if r[0] == 'IK']
        it = {r[1]: (int(r[2]), int(r[3])) for r in out if r[0] == 'IT'}
        size = {int(r[1], 16): int(r[2]) for r in out if r[0] in ('IS', 'IL')}
        im = [list(map(int, r[1:])) for r in out if r[0] == 'IM'][0]
        return ig, ik, it, size, im

    def compare(vals, tag):
        ig, ik, it, size, im = run(vals)
        kp, pc, sz, ns = resolve(vals)
        e = []
        if ik != kp:
            e.append(f'{tag}: kinds {ik}, expected {kp}')
        for n in names:
            if it.get(n) != (pc[n], ns[n]):
                e.append(f'{tag}: {n} {it.get(n)}, expected {(pc[n], ns[n])}')
        bad = [f'{a:06X} {size.get(a)}/{w}' for a, w in sz.items() if size.get(a) != w]
        if bad:
            e.append(f'{tag}: place sizes ' + ', '.join(bad[:5]))
        if im[2] != len(T):
            e.append(f'{tag}: {im[2]} settings in the DLL, {len(T)} parsed')
        return e, ig, ik, it, size, im

    # nothing set: every kind at 200, no place needs its own copy
    e, _, ik, _, size, im = compare({}, 'empty')
    errs += e
    if ik != [200] * 7 or any(size.values()) or im[0] != 0:
        errs.append(f'empty ini: kinds {ik}, max place size {im[0]} (expected all 200, no copies)')
    # a worked case, also checked by hand
    case = {'Text/Size': 150, 'Text.Menus/All': 250, 'Text.Menus/Options': 120, 'Text.Screens/All': 175,
            'Text.Game/HudLabels': 300, 'Text.Game/PlaceNames': 300, 'Text/Tooltips': 0, 'Text.Foo/Bar': 200}
    e, ig, ik, it, size, im = compare(case, 'case')
    errs += e
    hand = [175, 150, 250, 175, 150, 150, 175]       # journal, tooltip, menu, interface, name(messages), number, credits
    if ik != hand:
        errs.append(f'case: kinds {ik}, by hand {hand}')
    if ig.get('Text.Foo/Bar') != -1:
        errs.append('an unknown setting was taken')
    hand_sets = {'Text.Menus/MainMenu': (250, 0), 'Text.Menus/Options': (120, 2), 'Text.Screens/Labels': (175, 3),
                 'Text.Screens/Character': (175, 0), 'Text.Game/Conversations': (150, 2), 'Text.Game/HudLabels': (300, 6),
                 'Text.Game/PlaceNames': (300, 1), 'Text.Game/Names': (150, 0)}
    for n, w in hand_sets.items():
        if it.get(n) != w:
            errs.append(f'case: {n} {it.get(n)}, by hand {w}')
    if size.get(0x6721CF) != 300 or im[0] != 300:
        errs.append(f'case: place names by load site {size.get(0x6721CF)}, max {im[0]} (expected 300, 300)')
    # [PlaceTextScale] override after the items
    _, _, it2, size2, _ = run(case, [(0x4471BB, 175)])
    if size2.get(0x4471BB) != 175 or size2.get(0x64EF68) != 120:
        errs.append(f'override: 4471BB {size2.get(0x4471BB)} (expected 175), 64EF68 {size2.get(0x64EF68)} (expected 120)')
    # the tooltip rules follow the Names item when it is larger than the tooltip kind
    _, _, _, _, im3 = run({"Text.Game/Names": 320})
    if im3[1] != 320:
        errs.append(f'Names=320: tooltip rules size {im3[1]} (expected 320)')
    # random settings against the separate implementation
    rng = random.Random(28)
    for k in range(40):
        vals = {n: rng.choice([0, 0, 0, 80, 100, 150, 200, 250, 300, 700]) for n in rng.sample(names, rng.randint(1, len(names)))}
        e, *_ = compare(vals, f'random {k}')
        errs += e[:2]
    # mutation: the case sets places; the same case without values does not
    if not any(size.values()):
        errs.append('mutation: the worked case set no place')
    # custom font files: face and weight as GDI knows them (the values the DLL used before)
    want = {'SoulbringerText-Regular.ttf': (300, 'Soulbringer Text'), 'SoulbringerUncial-Regular.ttf': (400, 'Soulbringer Uncial'),
            'MedievalSharp-Regular.ttf': (500, 'MedievalSharp'), 'Montserrat-ExtraBold.ttf': (800, 'Montserrat ExtraBold'),
            'Cardo-Bold.ttf': (700, 'Cardo'), 'SoulbringerCredits-Regular.ttf': (400, 'Soulbringer Credits')}
    for f, (w, face) in want.items():
        out = run_harness(exe, '', [f'f {os.path.join(ROOT, "dist", "fonts", f)}'])
        got = [(int(r[1]), ' '.join(r[2:])) for r in out if r[0] == 'FN']
        if got != [(w, face)]:
            errs.append(f'font file {f}: {got} (expected {(w, face)})')
    out = run_harness(exe, '', [f'f {os.path.join(ROOT, "dist", "fonts", "Cardo-OFL.txt")}'])
    if [r for r in out if r[0] == 'FN'] != [['FN', '0', '-']]:
        errs.append('a text file was read as a font')
    print(f"{'ok  ' if not errs else 'FAIL'} text settings: {len(T)} settings in 4 sections over {len(seen)} places (table, shipped ini, no old keys); "
          f"item -> category -> Size, copies only where an item differs from its kind, override, load site, tooltip rules, 40 random settings; font file names"
          + ('' if not errs else ':\n       ' + '\n       '.join(errs[:12])))
    return 1 if errs else 0

def autofit_tests(exe):
    """Auto-fit fix (text_scale.inc tx_autofit_choice, hooked after the game's 0x411634): an enlarged
    label in sizing mode 2 (keep the trial width closest to the target +0x5C) that the game broke onto
    two lines although one line fits is given the one-line box from the game's own layout; nothing
    else changes: fonts at 100%, other modes, a narrow target, text that cannot be one line in the clip,
    a box already one line. Numbers from the options menu at 200% (session 23992: "Tool Tips" box
    75 x 64, line step 32, space 10). Mutation: without the one-line guard a long text is changed."""
    # (label, mode, target, clipW, boxW, boxH, pct, step, space, words, expected (changed, w, h))
    cases = [
        ('Tool Tips at 200%',            2, 3840, 3840, 75, 64, 200, 32, 10, [37, 37], (1, 84, 32)),
        ('Sound FX at 200%',             2, 3840, 3840, 91, 64, 200, 32, 10, [91, 30], (1, 131, 32)),
        ('font at 100% (the game as is)', 2, 3840, 3840, 38, 32, 100, 16, 5, [19, 19], (0, 0, 0)),
        ('sizing mode 1',                1, 3840, 3840, 75, 64, 200, 32, 10, [37, 37], (0, 0, 0)),
        ('narrow target 60',             2, 60, 3840, 75, 64, 200, 32, 10, [37, 37], (0, 0, 0)),
        ('description wider than clip',  2, 3840, 600, 410, 96, 200, 32, 10, [200, 200, 200, 200], (0, 0, 0)),
        ('already one line',             2, 3840, 3840, 83, 32, 200, 32, 10, [83], (0, 0, 0)),
    ]
    errs = []
    def run(c, step=None):
        _, m, t, cw, bw, bh, p, st, sp, ws, _ = c
        out = run_harness(exe, '', [f'E {m} {t} {cw} {bw} {bh} {p} {st} {step or st} {sp} {len(ws)} ' + ' '.join(map(str, ws))])
        return tuple(map(int, out[0][1:]))
    for c in cases:
        got = run(c)
        if got != c[-1]:
            errs.append(f'{c[0]}: {got}, expected {c[-1]}')
    # mutation: with the one-line guard off (any height counts as one line), the description (3 lines in its
    # box, 2 at the clip width) would be re-laid out: the guard must be what keeps it
    if run(cases[5], step=10000)[0] != 1:
        errs.append('mutation not caught: the one-line guard made no difference')
    print(f"{'ok  ' if not errs else 'FAIL'} auto-fit fix: enlarged mode-2 labels broken onto two lines get the one-line box (Tool Tips 75x64 -> 84x32, Sound FX); "
          f"unchanged at 100%, in mode 1, with a narrow target, for text wider than its clip, for a box already one line; mutation caught"
          + ('' if not errs else ':\n       ' + '\n       '.join(errs)))
    return 1 if errs else 0

def menu_place_tests(exe):
    """Main-menu copyright and save-list date (text_scale.inc, 2026-11-26).
    Copyright: the game puts it at x = W - w*W/768, which at 4K with enlarged text is past the
    left edge (session 24680: w 774 -> x -30); the fix keeps the 1024x768 place: right edge at
    897/1024 of W, bottom at 756/768 of H. At 1024x768 with the original width (365 x 15) it
    lands where the game puts it (x 532 vs the reference ink 536, y 741 vs 739).
    Date: the 200 px "..." limit grows with the largest text size (200% -> 400, 100% stays 200)."""
    cases = [  # (label, W, H, w, h, maxPct, expected x, y, limit)
        ('4K, Eagle Lake at 200% (session 24680)', 3840, 2160, 774, 40, 200, 2589, 2086, 400),
        ('4K, original letters at 200% (session 16048)', 3840, 2160, 720, 40, 200, 2643, 2086, 400),
        ('1024x768, the game as is', 1024, 768, 365, 15, 100, 532, 741, 200),
        ('wider than the screen', 1024, 768, 2000, 15, 300, 0, 741, 600),
    ]
    errs = []
    for c in cases:
        got = tuple(map(int, run_harness(exe, '', [f'M {c[1]} {c[2]} {c[3]} {c[4]} {c[5]}'])[0][1:]))
        if got != c[6:]:
            errs.append(f'{c[0]}: {got}, expected {c[6:]}')
    # the 4K results put the line's right edge where the reference has it (897/1024 = 0.876 of the width)
    x, _, _ = map(int, run_harness(exe, '', ['M 3840 2160 774 40 200'])[0][1:])
    if abs((x + 774) / 3840 - 897 / 1024) > 0.002:
        errs.append(f'right edge at {(x + 774) / 3840:.3f} of the width, reference 0.876')
    print(f"{'ok  ' if not errs else 'FAIL'} menu places: copyright back at the bottom right (4K: x -30 -> 2589, right edge 0.876 W as at 1024x768); "
          f"save-list date limit 200 px -> 400 at 200%"
          + ('' if not errs else ':\n       ' + '\n       '.join(errs)))
    return 1 if errs else 0

def conversation_tests(exe):
    """Conversation screen (ui_layout.inc, 2026-11-26), numbers from session 20048 capture 12 (4K):
    the letterbox bands (0x4B686A, 0-640 x 0-60 and 420-480), the dialogue box (0x456ABD,
    2118-2803 x 296-524) with its text, the portrait (0x4A9195, 2823-3687 x 86-734, i.e. 144 x 144
    canvas units stretched to 864 x 648). Layout on, HudLayout=right: the portrait is square again
    (648 x 648) with its right edge where the HUD's would be (3725.25), the box and its text move
    by the same amount (+259.25), so the 15 px gap stays; glyph and box sizes do not change.
    A frame with the letterbox but no portrait (the game's skipped frames when the dialogue turns
    to its next line) gets the last portrait again; a frame without the letterbox does not;
    with Layout=0 nothing changes and nothing is held."""
    def quad(x0, y0, x1, y1, uv=False):
        pts = ((x0, y0), (x1, y0), (x1, y1), (x0, y0), (x1, y1), (x0, y1))
        return [f'{x} {y} 0.0001 0.99999' + (' 0.1 0.1' if uv else '') for x, y in pts]
    def frame(portrait=True, bands=True):
        f = ['F']
        if bands:
            f += ['B 004B686A 4D 6'] + quad(0, 0, 3840, 270) + ['B 004B686A 4D 6'] + quad(0, 1890, 3840, 2160)
        f += ['R', 'B 00456ABD 4D 6'] + quad(2118, 296, 2803, 524)
        if portrait:
            f += ['B 004A9195 6F 6'] + quad(2823, 86, 3687, 734)
        f += ['b 0042B0BC 4F 6'] + quad(2700, 470, 2783, 498, True) + ['R', 'e']
        return f
    def run(layout, frames):
        inp = ['S 3840 2160', 'H 2', 'A 0', f'L {layout}', 'T 100', 'Q 0 0 0'] + sum(frames, [])
        out = subprocess.run([exe], input='\n'.join(inp) + '\n', capture_output=True, text=True).stdout.split('\n')
        blocks, held, i = [], [], 0
        while i < len(out):
            if out[i].startswith('B '):
                n = int(out[i].split()[1]); blocks.append([tuple(map(float, l.split())) for l in out[i + 1:i + 1 + n]]); i += n + 1
            else:
                if out[i].startswith('HQ '):
                    held.append(tuple(out[i].split()[1:]))
                i += 1
        return blocks, held
    def xr(b):
        return (round(min(p[0] for p in b), 2), round(max(p[0] for p in b), 2))
    errs = []
    blocks, held = run(1, [frame(), frame(portrait=False), frame(portrait=False, bands=False)])
    # frame 1: bands 0,1; box 2; portrait 3; glyph 4
    if xr(blocks[3]) != (3077.25, 3725.25):
        errs.append(f'portrait at {xr(blocks[3])}, expected (3077.25, 3725.25)')
    if xr(blocks[2]) != (2377.25, 3062.25):
        errs.append(f'dialogue box at {xr(blocks[2])}, expected (2377.25, 3062.25)')
    if xr(blocks[4]) != (2959.25, 3042.25):
        errs.append(f'text at {xr(blocks[4])}, expected (2959.25, 3042.25)')
    if xr(blocks[0]) != (0.0, 3840.0):
        errs.append(f'letterbox at {xr(blocks[0])}, expected the full width')
    if held != [('6F', '3077.25', '3725.25')]:
        errs.append(f'held portraits {held}, expected one, in frame 2 only, at (3077.25, 3725.25)')
    blocks0, held0 = run(0, [frame(), frame(portrait=False)])
    if xr(blocks0[3]) != (2823.0, 3687.0) or xr(blocks0[2]) != (2118.0, 2803.0) or held0:
        errs.append(f'Layout=0: portrait {xr(blocks0[3])}, box {xr(blocks0[2])}, held {held0}: expected the game\'s, none held')
    print(f"{'ok  ' if not errs else 'FAIL'} conversation: portrait square at the top right (864x648 -> 648x648, right edge 3725), dialogue box and text "
          f"moved with it (+259, sizes kept), a skipped portrait frame drawn from the last one; Layout=0 untouched"
          + ('' if not errs else ':\n       ' + '\n       '.join(errs)))
    return 1 if errs else 0

def street_talk_tests(exe):
    """Street talk (guards, passers-by: no letterbox; ui_layout.inc 2026-11-27), numbers from session
    28448 capture 11 (4K): the dialogue box (0x456ABD, 1200-2163 x 334-602) is drawn before the
    portrait (0x4A9195, 2183-3047 x 144-792 = canvas 363.8-507.8 x 32-176, 864 x 648 stretched).
    Layout on: the portrait is square (648 x 648) around its centre's 4:3 position (map_center:
    480 + 435.833 x 4.5 = 2441.25 -> 2117.25-2765.25), the box and its text move by the same amount
    as its left edge (-65.75), so the 20 px gap stays. A frame where the game skips the portrait
    (a new line) still moves the box. Layout=0: untouched."""
    def quad(x0, y0, x1, y1, uv=False):
        pts = ((x0, y0), (x1, y0), (x1, y1), (x0, y0), (x1, y1), (x0, y1))
        return [f'{x} {y} 0.0001 0.99999' + (' 0.1 0.1' if uv else '') for x, y in pts]
    def frame(portrait=True):
        f = ['F', 'B 00456ABD 4D 6'] + quad(1200, 334, 2163, 602)
        if portrait:
            f += ['B 004A9195 6D 6'] + quad(2183, 144, 3047, 792)
        f += ['b 0042B0BC 4F 6'] + quad(2050, 540, 2140, 570, True) + ['R', 'e']
        return f
    def run(layout, frames):
        inp = ['S 3840 2160', 'H 2', 'A 0', f'L {layout}', 'T 100', 'Q 0 0 0'] + sum(frames, [])
        out = subprocess.run([exe], input='\n'.join(inp) + '\n', capture_output=True, text=True).stdout.split('\n')
        blocks, i = [], 0
        while i < len(out):
            if out[i].startswith('B '):
                n = int(out[i].split()[1]); blocks.append([tuple(map(float, l.split())) for l in out[i + 1:i + 1 + n]]); i += n + 1
            else:
                i += 1
        return blocks
    def xr(b):
        return (round(min(p[0] for p in b), 2), round(max(p[0] for p in b), 2))
    errs = []
    b = run(1, [frame(), frame(portrait=False)])
    # frame 1: box 0, portrait 1, glyph 2; frame 2: box 3, glyph 4
    if xr(b[1]) != (2117.25, 2765.25):
        errs.append(f'portrait at {xr(b[1])}, expected (2117.25, 2765.25)')
    if xr(b[0]) != (1134.25, 2097.25):
        errs.append(f'dialogue box at {xr(b[0])}, expected (1134.25, 2097.25)')
    if xr(b[2]) != (1984.25, 2074.25):
        errs.append(f'text at {xr(b[2])}, expected (1984.25, 2074.25)')
    if len(b) < 5 or xr(b[3]) != (1134.25, 2097.25):
        errs.append(f'frame without the portrait: box at {xr(b[3]) if len(b) > 3 else None}, expected (1134.25, 2097.25)')
    b0 = run(0, [frame()])
    if xr(b0[1]) != (2183.0, 3047.0) or xr(b0[0]) != (1200.0, 2163.0):
        errs.append(f'Layout=0: portrait {xr(b0[1])}, box {xr(b0[0])}: expected the game\'s')
    print(f"{'ok  ' if not errs else 'FAIL'} street talk: guard portrait square around its 4:3 centre (864x648 -> 648x648 at 2117-2765), box and text moved with it (-65.75, gap kept, also without the portrait); Layout=0 untouched"
          + ('' if not errs else ':\n       ' + '\n       '.join(errs)))
    return 1 if errs else 0

def quest_message_test(exe):
    """"A new quest has been added to your journal." (session 2184 capture 12, 4K, TooltipTextScale
    300 so one-line boxes up to 92 px): the game draws its box at 1249-1981 x 1755-1837, centred on the
    view left of the HUD column (canvas ~269) instead of the screen. Layout on: box and text are
    centred on the screen (1554-2286, text +305); Layout=0: the game's place; a message the game
    already centres (1722-2119) stays."""
    def quad(x0, y0, x1, y1, uv=False):
        pts = ((x0, y0), (x1, y0), (x1, y1), (x0, y0), (x1, y1), (x0, y1))
        return [f'{x} {y} 0.0001 0.99999' + (' 0.1 0.1' if uv else '') for x, y in pts]
    def run(layout, box, txt):
        inp = ['S 3840 2160', 'H 2', 'A 0', f'L {layout}', 'T 100', 'Y 300', 'Q 0 0 0', 'F', 'B 00456ABD 4D 6'] + quad(box[0], 1755, box[1], 1837)
        inp += ['b 0042B0BC 4F 6'] + quad(txt[0], 1775, txt[1], 1814, True) + ['R', 'e']
        out = subprocess.run([exe], input='\n'.join(inp) + '\n', capture_output=True, text=True).stdout.split('\n')
        res, i = [], 0
        while i < len(out):
            if out[i].startswith('B '):
                n = int(out[i].split()[1]); xs = [float(l.split()[0]) for l in out[i + 1:i + 1 + n]]; res.append((min(xs), max(xs))); i += n + 1
            else:
                i += 1
        return res
    errs = []
    r = run(1, (1249, 1981), (1268, 1350))
    if r != [(1554.0, 2286.0), (1573.0, 1655.0)]:
        errs.append(f'Layout=1: {r}, expected box (1554, 2286), text (1573, 1655)')
    r = run(0, (1249, 1981), (1268, 1350))
    if r != [(1249.0, 1981.0), (1268.0, 1350.0)]:
        errs.append(f'Layout=0: {r}, expected the game\'s place')
    r = run(1, (1722, 2119), (1741, 1800))
    if r != [(1722.0, 2119.0), (1741.0, 1800.0)]:
        errs.append(f'centred message: {r}, expected unchanged')
    print(f"{'ok  ' if not errs else 'FAIL'} quest message: centred on the screen over the HUD bar (game: on the view left of the column, -305 px at 4K); Layout=0 and centred messages untouched"
          + ('' if not errs else ':\n       ' + '\n       '.join(errs)))
    return 1 if errs else 0

def live_cursor_test(exe):
    """Tooltips are anchored to the game's cursor of this frame, not last frame's sprite (the
    sprite is often drawn after the tooltip). Two frames: the cursor jumps 500 px, the tooltip
    of frame 2 is drawn before its cursor sprite; it must still be recognised and scaled."""
    def quad(x0, y0, x1, y1):
        return [f'{x} {y} 0.0001 0.99999' for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y0), (x1, y1), (x0, y1))]
    def run(live):
        inp = ['S 3840 2160', 'H 1', 'A 0', 'L 1', 'T 200', f'V {live}', 'Q 0 1 1']
        inp += ['C 1000 1000', 'F', 'B 004A9195 52 6'] + quad(998, 998, 1030, 1030) + ['R']      # frame 1: cursor
        sx, sy = 1498, 1498                                                                       # frame 2: moved
        inp += ['C 1500 1500', 'F', 'B 00456ABD 4D 6'] + quad(sx - 17, sy - 38, sx - 17 + 90, sy + 8) + ['R']
        inp += ['B 004A9195 52 6'] + quad(sx, sy, sx + 32, sy + 32) + ['R']
        out = subprocess.run([exe], input='\n'.join(inp) + '\n', capture_output=True, text=True).stdout.split('\n')
        blocks, i = [], 0
        while i < len(out):
            if out[i].startswith('B '):
                n = int(out[i].split()[1]); blocks.append([tuple(map(float, l.split())) for l in out[i + 1:i + 1 + n]]); i += n + 1
            else:
                i += 1
        box = blocks[1]
        return max(p[0] for p in box) - min(p[0] for p in box)
    w_live, w_old = run(1), run(0)
    ok = abs(w_live - 180) < 0.5
    print(f"{'ok  ' if ok else 'FAIL'} tooltip anchor: cursor moved 500 px, tooltip drawn before the cursor: "
          f"{w_live:.0f} px wide with the live cursor (expected 180), {w_old:.0f} px with last frame's sprite")
    return 0 if ok else 1

CLICK = re.compile(r'event: click (\w) \((\S+)\)\s+cursor \(([\d.\-]+),([\d.\-]+)\) \[canvas ([\d.\-]+),([\d.\-]+)\] (\S+)'
                   r'.*?screen \w+ (menu|game) frame (\d+)')
AIM = re.compile(r'aim: drawn (\S+)\s+-> game point \(([\d.\-]+),')
HM = re.compile(r'hud mouse: real \((-?\d+),(-?\d+)\), game cursor \((-?\d+),(-?\d+)\)')
HDRUI = re.compile(r'ui layout: (on|off).*?HUD (?!mouse)(\w+)\)')
HDRCAP = re.compile(r'aspect cap: ([\d.]+)')

def load_buttons():
    """tests/data/hud_buttons.txt: '<name> x0 y0 x1 y1' per line, canvas units (640x480), # comments."""
    if not os.path.exists(BUTTONS):
        return []
    out = []
    for l in open(BUTTONS, encoding='utf-8'):
        l = l.split('#')[0].split()
        if len(l) == 5:
            out.append((l[0], *map(float, l[1:])))
    return out

def button_at(buttons, cx, cy):
    for name, x0, y0, x1, y1 in buttons:
        if x0 <= cx <= x1 and y0 <= cy <= y1:
            return name
    return None

def click_replay(exe, log):
    """Replay the gameplay clicks recorded in a session through ui_hud_aim."""
    lines = open(log, encoding='latin1').read().split('\n')
    hud, aspect, layout = 'split', 0.0, 1
    for l in lines[:40]:
        m = HDRUI.search(l)
        if m: layout, hud = (1 if m.group(1) == 'on' else 0), m.group(2)
        m = HDRCAP.search(l)
        if m: aspect = float(m.group(1))
    W = H = None
    for l in lines:
        m = re.search(r'screen (\d+) x (\d+)', l)
        if 'FRAME CAPTURE' in l and m: W, H = int(m.group(1)), int(m.group(2)); break
    clicks = []
    for i, l in enumerate(lines):
        m = CLICK.search(l)
        if not m or m.group(8) != 'game':
            continue
        aim = real = None
        for k in range(i + 1, min(i + 12, len(lines))):
            if not lines[k].startswith('        '): break
            a = AIM.search(lines[k])
            if a: aim = (a.group(1), float(a.group(2)))
            h = HM.search(lines[k])
            if h: real = (int(h.group(1)), int(h.group(2)))
        clicks.append({'btn': m.group(1), 'x': float(m.group(3)), 'y': float(m.group(4)), 'region': m.group(7),
                       'frame': int(m.group(9)), 'aim': aim, 'real': real})
    if not clicks or not W or hud not in HUDS:
        return 0
    head = f'S {W} {H}\nH {HUDS[hud]}\nA {aspect}\nL {layout}\n'
    # the point that was clicked on screen: the real mouse (with the fix), else the game cursor
    res = run_harness(exe, head, [f"U {(c['real'] or (c['x'], 0))[0]} {c['y']} 1 1" for c in clicks])
    buttons = load_buttons()
    # clicks recorded with the HUD mouse fix: replay the real mouse through the current ui_hud_mouse()
    # to get the point the game would see now (the log's game cursor is what the build of that day did)
    now = {}
    reals = [c for c in clicks if c['real']]
    if reals:
        cmds = []
        for c in reals:
            cmds += ['Q 0 1 1', f'C {W // 2} {H // 3}', 'D 0 0 0 0', f"D {c['real'][0] - W // 2} {c['real'][1] - H // 3} 0 0"]
        out = run_harness(exe, head, cmds)
        for k, c in enumerate(reals):
            now[id(c)] = (int(out[2 * k + 1][1]), int(out[2 * k + 1][2]))
    errs, rows, misses = [], [], 0
    names = {0: 'world', 1: 'hud-bar', 2: 'hud-column'}
    for c, (_, p, gx) in zip(clicks, res):
        p, gx = int(p), float(gx)
        if c['aim'] and (c['aim'][0] != names[p] or abs(c['aim'][1] - gx) > 0.5):
            errs.append(f"frame {c['frame']}: DLL logged aim {c['aim'][0]} x {c['aim'][1]:.1f}, harness says {names[p]} x {gx:.1f}")
        if p == 0 and c['region'] == 'world':
            continue
        miss = c['x'] - gx
        cx, cy = c['x'] * 640 / W, c['y'] * 480 / H
        line = (f"frame {c['frame']:5d} {c['btn']}: drawn {names[p]:10s} at x {c['x']:7.1f} -> game has it at x {gx:7.1f}"
                f" [canvas {gx * 640 / W:5.1f},{cy:5.1f}]; game hit-tests canvas {cx:5.1f} ({c['region']}), miss {miss:+.0f} px")
        if buttons:
            want = button_at(buttons, gx * 640 / W, cy) if p else None   # drawn world: nothing was aimed at
            if id(c) in now:                                              # with the fix: what the game sees now
                got = button_at(buttons, now[id(c)][0] * 640 / W, now[id(c)][1] * 480 / H)
                if c['region'] != 'world' and not p:
                    line += f"; game cursor now ({now[id(c)][0]},{now[id(c)][1]})"
            else:
                got = button_at(buttons, cx, cy)
            line += f"; aimed {want or '-'}, game hit {got or '-'}"
            if want != got:
                misses += 1
                if c['real']:
                    errs.append(f"frame {c['frame']}: clicked {want or 'nothing'}, game hit {got or 'nothing'} (with the HUD mouse fix)")
        rows.append(line)
    print(f"{'ok  ' if not errs else 'FAIL'} {os.path.basename(log)} [clicks, {hud}{' cap %.2f' % aspect if aspect else ''}"
          f"{'' if layout else ' Layout=0'}] {len(clicks)} gameplay clicks replayed, {len(rows)} on or near the HUD"
          + (f", {sum(1 for c in clicks if c['aim'])} compared with the DLL's aim lines" if any(c['aim'] for c in clicks) else '')
          + (f", {misses} hit a different button" if buttons else ', no tests/data/hud_buttons.txt yet'))
    for r in rows:
        print('       ' + r)
    for e in errs[:8]:
        print('       ' + e)
    return 1 if errs else 0

def number_font_tests():
    """Combat numbers ([UI] NumberFont, 2026-11-27): numr/numg redrawn from Montserrat ExtraBold into
    their original rectangles (tools/number_font_preview.py -> test_font_pages numbers: em from the
    '8', baseline row 12, colours yellow/red and green/dark green from the pages, all 26 glyphs drawn,
    nothing outside the two fonts' rectangles changed). Shipped ini: [Fonts] All=Sharp, Numbers empty (= Montserrat)."""
    game = os.environ.get('SBM_GAME_DIR')
    errs = []
    errs += shipped_font_default('Numbers')
    if not game or not os.path.exists(os.path.join(game, 'data', 'numr.fnt')):
        print('skip number font: SBM_GAME_DIR with data/numr.fnt not set')
    else:
        import tempfile
        out = tempfile.mkdtemp(prefix='sbm_num_')
        r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'number_font_preview.py'), '--game', game, '--out', out], capture_output=True, text=True)
        if 'NUMBERS PASSED' not in r.stdout:
            errs.append('test_font_pages numbers: ' + (r.stdout + r.stderr).strip().replace('\n', ' | ')[-400:])
    print(f"{'ok  ' if not errs else 'FAIL'} number font: numr/numg from Montserrat ExtraBold in their rectangles (26 glyphs, colours from the pages, nothing else touched); shipped ini [Fonts] All=Sharp, Numbers follows it"
          + ('' if not errs else ':\n       ' + '\n       '.join(errs)))
    return 1 if errs else 0

def credits_font_tests():
    """Credits font ([UI] CreditsFont, 2026-11-27): cred redrawn from the bundled Soulbringer Credits in its
    original rectangles (tools/credits_font_preview.py -> test_font_pages credits: em from 'H' = 19 texels,
    white fill from the page, no outline, codes the face lacks left to the game, nothing outside cred's
    rectangles changed). Shipped ini [Fonts] All=Sharp, Credits empty (= Soulbringer Credits); the font and its licence in the release list."""
    game = os.environ.get('SBM_GAME_DIR')
    errs = []
    errs += shipped_font_default('Credits')
    rel = open(os.path.join(ROOT, 'make_release.sh')).read()
    for fn in ('SoulbringerCredits-Regular.ttf', 'SoulbringerCredits-OFL.txt'):
        if not os.path.exists(os.path.join(ROOT, 'dist', 'fonts', fn)) or 'dist/fonts/' + fn not in rel:
            errs.append(f'{fn}: missing in dist/fonts or the release list')
    if not game or not os.path.exists(os.path.join(game, 'data', 'cred.fnt')):
        print('skip credits font: SBM_GAME_DIR with data/cred.fnt not set')
    else:
        import tempfile
        out = tempfile.mkdtemp(prefix='sbm_cred_')
        r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'credits_font_preview.py'), '--game', game, '--out', out], capture_output=True, text=True)
        if 'CREDITS PASSED' not in r.stdout:
            errs.append('test_font_pages credits: ' + (r.stdout + r.stderr).strip().replace('\n', ' | ')[-400:])
    print(f"{'ok  ' if not errs else 'FAIL'} credits font: cred from Soulbringer Credits in its rectangles (156 glyphs, 'H' 19 texels, white, the rest left to the game); shipped ini and release list"
          + ('' if not errs else ':\n       ' + '\n       '.join(errs)))
    return 1 if errs else 0

def menu_font_tests():
    """Menu font ([UI] MenuFont, 2026-11-26): the menu font magc gets its own cells from Eagle Lake
    and from MedievalSharp, together with phil/hbut (Cardo) as FontRedraw=phil,hbut adds them; every
    cell is packed into free page space, touching no other font, the line height kept
    (tools/menu_font_preview.py -> test_font_pages layout). The shipped ini sets [Fonts] All=Sharp (Menu, Names, Tooltips, Journal empty: Soulbringer Uncial / Text)
    and both faces ship in dist/fonts with their OFL texts. Needs the game data (SBM_GAME_DIR)."""
    game = os.environ.get('SBM_GAME_DIR')
    errs = []
    if not game or not os.path.exists(os.path.join(game, 'data', 'magc.fnt')):
        print('skip menu font: SBM_GAME_DIR with data/magc.fnt not set')
        return 0
    import tempfile
    fonts = os.path.join(ROOT, 'dist', 'fonts')
    faces = [os.path.join(fonts, f) for f in ('SoulbringerUncial-Regular.ttf', 'MedievalSharp-Regular.ttf')]
    for f in faces + [os.path.join(fonts, 'SoulbringerUncial-OFL.txt'), os.path.join(fonts, 'MedievalSharp-OFL.txt'),
                      os.path.join(fonts, 'SoulbringerText-Regular.ttf'), os.path.join(fonts, 'SoulbringerText-OFL.txt')]:
        if not os.path.exists(f):
            errs.append(f'missing {os.path.relpath(f, ROOT)}')
    for k in ('Menu', 'Names', 'Tooltips', 'Journal'):
        errs += shipped_font_default(k)
    rel = open(os.path.join(ROOT, 'make_release.sh')).read()
    for f in ('SoulbringerUncial-Regular.ttf', 'SoulbringerUncial-OFL.txt', 'MedievalSharp-Regular.ttf', 'MedievalSharp-OFL.txt', 'SoulbringerText-Regular.ttf', 'SoulbringerText-OFL.txt'):
        if f not in rel:
            errs.append(f'make_release.sh does not ship {f}')
    if not errs:   # Soulbringer Uncial = Eagle Lake with lining figures: every digit on the baseline, one height
        from fontTools.ttLib import TTFont
        tf = TTFont(faces[0]); g = tf['glyf']; cm = tf.getBestCmap()
        fam = tf['name'].getDebugName(1)
        lo = [g[cm[ord(d)]].yMin for d in '0123456789']; hi = [g[cm[ord(d)]].yMax for d in '0123456789']
        if fam != 'Soulbringer Uncial':
            errs.append(f'menu face family is {fam!r}, not "Soulbringer Uncial"')
        if min(lo) < -40 or max(hi) - min(hi) > 60:
            errs.append(f'digits not lining: bottoms {lo}, tops {hi}')
    out = ''
    if not errs:
        # all five own-cells fonts at once: magc (each menu face); phil, hbut, mgrn, mred from Soulbringer Text
        # ([UI] JournalFont/TooltipFont/NameFont=Text; its few missing glyphs keep their rectangles)
        args = [sys.executable, os.path.join(ROOT, 'tools', 'menu_font_preview.py'), '--game', game, '--with-journal',
                '--name-face', os.path.join(fonts, 'SoulbringerText-Regular.ttf'), '--tip-face', os.path.join(fonts, 'SoulbringerText-Regular.ttf'),
                '--journal-face', os.path.join(fonts, 'SoulbringerText-Regular.ttf'),
                '--out', os.path.join(tempfile.gettempdir(), 'sbm_menu_font')]
        for f in faces:
            args += ['--face', f]
        r = subprocess.run(args, capture_output=True, text=True)
        out = r.stdout
        if r.returncode != 0 or 'MENU LAYOUT PASSED' not in out:
            errs.append('layout failed: ' + ' | '.join(l.strip() for l in out.splitlines() if 'FAIL' in l or 'touches' in l or 'packed' in l)[:300])
    packed = [l.strip() for l in out.splitlines() if l.strip().startswith('packed')]
    print(f"{'ok  ' if not errs else 'FAIL'} menu font: magc cells from Soulbringer Uncial (Eagle Lake) and MedievalSharp with phil, hbut and mgrn/mred (Soulbringer Text) ({'; '.join(packed)}), lining figures, shipped ini ([Fonts] All=Sharp, Menu/Names/Tooltips/Journal follow it) and release list")
    for e in errs:
        print('       ' + e)
    return 1 if errs else 0

def main():
    logs = sys.argv[1:] or sorted(glob.glob(os.path.join(ROOT, 'tests', 'data', '*.log')))
    import tempfile
    exe = os.path.join(tempfile.gettempdir(), 'sbm_harness' + ('.exe' if os.name == 'nt' else ''))
    cc = os.environ.get('CC', 'cc')
    subprocess.check_call([cc, '-O1', '-w', '-o', exe, os.path.join(HERE, 'harness.c')])
    total_fail = hud_mapping_tests(exe) + hud_mouse_tests(exe) + text_decode_tests(exe) + text_hook_tests(exe) + font_places_tests(exe) + place_sizes_tests(exe) + place_groups_tests(exe) + autofit_tests(exe) + menu_place_tests(exe) + conversation_tests(exe) + street_talk_tests(exe) + quest_message_test(exe) + menu_font_tests() + number_font_tests() + credits_font_tests() + live_cursor_test(exe)
    for log in logs:
        total_fail += click_replay(exe, log)
        _, caps = sbm_log.parse(log)
        outdir = os.path.splitext(log)[0] + '_replay'
        # Replay the whole session in order through ONE harness run, so state
        # carried between frames (menu -> gameplay etc.) is tested as in game.
        inputs, orders = [], []
        for cap in caps:
            inp, order = frame_input(cap)
            # Each screen is fed three times and the third frame is checked: in game a
            # screen stays the same for many frames (captures are taken after 8
            # steady frames), and some state (e.g. tooltip boxes) carries over.
            inputs.append(inp + inp + inp); orders.append(order)
        if not caps:
            continue
        # every layout at TextScale 100, plus scaled text in the main layouts and with a cap
        TEXT_RUNS = [r + (100,) for r in RUNS] + [('split', 1, 0.0, 1, 250), ('right', 2, 0.0, 1, 250),
                                                  ('split', 1, 1.6, 1, 300), ('stretched', 0, 1.6, 0, 250)]
        for hud, code, aspect, layout, text in TEXT_RUNS:
            label = hud + (f' cap {aspect:.2f}' if aspect else '') + ('' if layout else ' Layout=0') + (f' text {text}%' if text != 100 else '')
            head = f"S {caps[0]['W']} {caps[0]['H']}\nH {code}\nA {aspect}\nL {layout}\nT {text}\n"
            res = subprocess.run([exe], input=head + ''.join(inputs), capture_output=True, text=True)
            if res.returncode:
                print(f'FAIL {os.path.basename(log)} [{hud}]: harness error {res.returncode}'); total_fail += 1; continue
            blocks, cur, bars = [], None, []
            for l in res.stdout.split('\n'):
                if l.startswith('BARS '): bars.append(tuple(map(float, l.split()[1:])))
                elif l.startswith('B '): cur = []; blocks.append(cur)
                elif l.strip(): a, b = l.split(); cur.append((float(a), float(b)))
            pos, fails = 0, 0
            W, H = caps[0]['W'], caps[0]['H']
            capped = aspect and W > H * aspect + 1
            games = sum(1 for c in caps if c['mode'] == 'game' or any(not i.get('flush') and i['world'] for i in c['items']))
            if capped and games:
                exL = (W - H * aspect) / 2
                bad = [b for b in bars if abs(b[0]) > 0.5 or abs(b[1] - exL) > 1 or abs(b[2] - (W - exL)) > 1 or abs(b[3] - W) > 0.5]
                if not bars or bad:
                    fails += 1
                    print(f"FAIL {os.path.basename(log)} [{label}]: black bars {'missing' if not bars else 'wrong: ' + str(bad[0])}"
                          f" (expected 0..{exL:.0f} and {W - exL:.0f}..{W})")
            elif bars:
                fails += 1
                print(f"FAIL {os.path.basename(log)} [{label}]: black bars drawn without a cap")
            for cap, order in zip(caps, orders):
                pos += 2 * len(order)                          # first two copies: warm-up frames
                mine = blocks[pos:pos + len(order)]; pos += len(order)
                errs, is_menu = check(cap, order, mine, hud, aspect, layout, text)
                if errs:
                    fails += 1
                    print(f"FAIL {os.path.basename(log)} [{label}] capture {cap['id']:2d} ({'menu' if is_menu else 'game'}):\n       "
                          + '\n       '.join(errs[:8]))
                if hud in ('split', 'right') and not aspect and text == 100:
                    try:
                        d = outdir + ('' if hud == 'split' else '_' + hud)
                        os.makedirs(d, exist_ok=True)
                        sbm_log.render(cap, os.path.join(d, f"capture_{cap['id']:02d}.png"))
                    except ImportError:
                        pass
            total_fail += fails
            print(f"{'ok  ' if not fails else 'FAIL'} {os.path.basename(log)} [{label:28s}] {len(caps)} screens replayed in order"
                  + ('' if not fails else f', {fails} failed'))
    print('ALL PASSED' if not total_fail else f'{total_fail} capture(s) FAILED')
    sys.exit(1 if total_fail else 0)

if __name__ == '__main__':
    main()
