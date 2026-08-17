#!/usr/bin/env python3
"""Combine an AI upscale with the original artwork, per pixel.

The measurement that motivates this: Real-ESRGAN makes the cockpit's curves
genuinely smoother than anything algorithmic - gauge bezels, the wheel rim, the
wood panel - and at the same time rewrites the numerals printed on the dials.
On a Countach speedometer, 180 came back as IOO.

Both observations are true at once, and they are true about *different parts of
the same image*. So this does not pick a winner. It marks the parts of the
original that are thin, high-contrast structure - digits, tick marks, needles,
the diamond markers - and takes those from Scale2x, which cannot alter them
because every output pixel is a copy of an input pixel. Everything else, which
is where the AI is better and where nothing can be misread, comes from the AI.

    usage: blend_cockpit.py <original-dir> <ai-dir> <out-dir>

All three hold <tag>.bmp files; the AI ones must be exactly 4x the originals.
"""
import os
import struct
import sys

SCALE = 4


def read_bmp(path):
    d = open(path, 'rb').read()
    off, = struct.unpack_from('<I', d, 10)
    w, h = struct.unpack_from('<ii', d, 18)
    bpp, = struct.unpack_from('<H', d, 28)
    if bpp != 24:
        raise SystemExit(f'{path}: {bpp}-bit, vantade 24')
    flip = h < 0
    h = abs(h)
    rowb = w * 3
    pad = (4 - rowb % 4) % 4
    px = [[None] * w for _ in range(h)]
    for row in range(h):
        y = row if flip else h - 1 - row
        base = off + row * (rowb + pad)
        for x in range(w):
            b, g, r = d[base + x * 3:base + x * 3 + 3]
            px[y][x] = (r, g, b)
    return w, h, px


def write_bmp(path, w, h, px):
    rowb = w * 3
    pad = (4 - rowb % 4) % 4
    img = (rowb + pad) * h
    hdr = bytearray(54)
    hdr[0:2] = b'BM'
    struct.pack_into('<I', hdr, 2, 54 + img)
    struct.pack_into('<I', hdr, 10, 54)
    struct.pack_into('<I', hdr, 14, 40)
    struct.pack_into('<i', hdr, 18, w)
    struct.pack_into('<i', hdr, 22, h)
    struct.pack_into('<H', hdr, 26, 1)
    struct.pack_into('<H', hdr, 28, 24)
    struct.pack_into('<I', hdr, 34, img)
    with open(path, 'wb') as f:
        f.write(hdr)
        for y in range(h - 1, -1, -1):
            for x in range(w):
                r, g, b = px[y][x]
                f.write(bytes((b, g, r)))
            f.write(b'\0' * pad)


def scale2x(px, w, h):
    """EPX. Every output pixel is a copy of an input pixel - it cannot
    invent a colour, which is exactly why the digits survive it."""
    out = [[None] * (w * 2) for _ in range(h * 2)]
    for y in range(h):
        for x in range(w):
            P = px[y][x]
            A = px[y - 1][x] if y > 0 else P
            D = px[y + 1][x] if y < h - 1 else P
            C = px[y][x - 1] if x > 0 else P
            B = px[y][x + 1] if x < w - 1 else P
            e0 = A if (C == A and C != D and A != B) else P
            e1 = B if (A == B and A != C and B != D) else P
            e2 = C if (D == C and D != B and C != A) else P
            e3 = D if (B == D and B != A and D != C) else P
            out[y * 2][x * 2] = e0
            out[y * 2][x * 2 + 1] = e1
            out[y * 2 + 1][x * 2] = e2
            out[y * 2 + 1][x * 2 + 1] = e3
    return out, w * 2, h * 2


def detail_mask(px, w, h):
    """Thin, high-contrast structure in the ORIGINAL.

    A pixel counts as detail when it differs from most of its eight
    neighbours: that is what a one-pixel-wide stroke looks like, and what a
    flat panel or a broad bezel does not. Printed digits, tick marks, needles
    and the little diamond markers all fall out of this; wood grain and
    gradients do not."""
    m = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            p = px[y][x]
            diff = 0
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    if dx == 0 and dy == 0:
                        continue
                    ny, nx = y + dy, x + dx
                    q = px[ny][nx] if 0 <= ny < h and 0 <= nx < w else p
                    # perceptual-ish distance, cheap
                    if abs(q[0] - p[0]) + abs(q[1] - p[1]) + abs(q[2] - p[2]) > 90:
                        diff += 1
            if diff >= 5:
                m[y][x] = True
    # grow by one so the stroke keeps its own edge pixels, not the AI's
    g = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if m[y][x]:
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        ny, nx = y + dy, x + dx
                        if 0 <= ny < h and 0 <= nx < w:
                            g[ny][nx] = True
    return g


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    orig_dir, ai_dir, out_dir = sys.argv[1:4]
    os.makedirs(out_dir, exist_ok=True)
    done = skipped = 0
    for name in sorted(os.listdir(orig_dir)):
        if not name.endswith('.bmp'):
            continue
        tag = name[:-4]
        ai_path = os.path.join(ai_dir, name)
        if not os.path.exists(ai_path):
            print(f'  {tag}: ingen AI-version, hoppar over')
            skipped += 1
            continue
        w, h, px = read_bmp(os.path.join(orig_dir, name))
        aw, ah, ap = read_bmp(ai_path)
        if aw != w * SCALE or ah != h * SCALE:
            print(f'  {tag}: AI ar {aw}x{ah}, vantade {w*SCALE}x{h*SCALE}')
            skipped += 1
            continue

        mask = detail_mask(px, w, h)
        big, bw, bh = scale2x(px, w, h)
        big, bw, bh = scale2x(big, bw, bh)

        kept = 0
        out = [[None] * aw for _ in range(ah)]
        for y in range(ah):
            sy = y // SCALE
            for x in range(aw):
                if mask[sy][x // SCALE]:
                    out[y][x] = big[y][x]
                    kept += 1
                else:
                    out[y][x] = ap[y][x]
        write_bmp(os.path.join(out_dir, name), aw, ah, out)
        pct = 100.0 * kept / (aw * ah)
        print(f'  {tag:<6} {w}x{h}: {pct:5.1f}% original (detalj), resten AI')
        done += 1
    print(f'{done} bilder skrivna till {out_dir}' +
          (f', {skipped} hoppade over' if skipped else ''))


if __name__ == '__main__':
    main()
