#!/usr/bin/env python3
"""
tools/compare_reference.py - Compare a DOSBox reference capture against a
native faithful-renderer frame.

DOSBox-staging captures are 320x200 content scaled with aspect correction
(e.g. 1280x960 or raw 320x200 depending on settings); this tool normalizes
both sides to 320x200 nearest-neighbor before diffing, prints per-pixel
stats, and writes a side-by-side + diff composite PNG.

Usage:
  python3 tools/compare_reference.py <dos_capture.png> <native.bmp> <out_composite.png>
"""
import sys
from PIL import Image, ImageChops


def load_320(path):
    im = Image.open(path).convert("RGB")
    if im.size != (320, 200):
        im = im.resize((320, 200), Image.NEAREST)
    return im


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        sys.exit(1)
    ref = load_320(sys.argv[1])
    nat = load_320(sys.argv[2])

    diff = ImageChops.difference(ref, nat)
    total = 320 * 200
    same = sum(1 for p in diff.getdata() if p == (0, 0, 0))
    pct = 100.0 * same / total
    print(f"identical pixels: {same}/{total} ({pct:.2f}%)")

    # Composite: reference | native | diff highlight
    comp = Image.new("RGB", (320 * 3 + 8, 200), (40, 40, 40))
    comp.paste(ref, (0, 0))
    comp.paste(nat, (324, 0))
    hl = Image.new("RGB", (320, 200))
    hl.putdata([(255, 0, 128) if p != (0, 0, 0) else (30, 30, 30)
                for p in diff.getdata()])
    comp.paste(hl, (648, 0))
    comp = comp.resize((comp.width * 2, comp.height * 2), Image.NEAREST)
    comp.save(sys.argv[3])
    print(f"wrote {sys.argv[3]}")


if __name__ == "__main__":
    main()
