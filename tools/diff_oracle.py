#!/usr/bin/env python3
"""
tools/diff_oracle.py - Diff a native GAMESTATE dump against the DOS oracle.

Both files are: 2-byte frame count, then N x 1120-byte GAMESTATE records.

Two regions are reported SEPARATELY rather than folded into the headline
number, because they measure something other than the car's physics:
  * game_longs1/2/3 (0x000-0x120) - crash-debris particle positions, written
    by state_op_unk (seg001.asm 9206-9367, 138 instructions, still a stub in
    this port). It calls get_kevinrandom, so it is a visual effect only: the
    simulation never reads it back. NOTE: an earlier version of this script
    called the region "dead - zero in every oracle frame". That is FALSE on
    any replay where the car crashes (102233 non-zero bytes on HILLTEST), and
    it silently hid a real gap. It is now counted and printed.
  * opponentstate   (0x222-0x2F2) - written once, never changes, no opponent

Layout facts (verified against structs.inc, see docs/FAITHFUL_RENDERER.md):
  playerstate starts at 0x152 (338); CARSTATE is 0xD0 (208) bytes.

Usage: diff_oracle.py <oracle.bin> <native.bin> [max_frames]
"""
import struct
import sys

REC = 1120
PLAYER = 0x152
CARSTATE_LEN = 0xD0

# (offset within CARSTATE, size, signed, name)
CAR_FIELDS = [
    (0x00, 4, True,  "pos.x"),
    (0x04, 4, True,  "pos.y"),
    (0x08, 4, True,  "pos.z"),
    (0x18, 2, True,  "rot.x (heading)"),
    (0x1A, 2, True,  "rot.y (pitch)"),
    (0x1C, 2, True,  "rot.z (roll)"),
    (0x1E, 2, True,  "pseudoGravity"),
    (0x20, 2, True,  "steeringAngle"),
    (0x22, 2, True,  "rpm"),
    (0x2A, 2, False, "speed (rev-coupled)"),
    (0x2C, 2, False, "speed2 (actual)"),
    (0x30, 2, False, "gearratio"),
    (0xBD, 1, False, "gear"),
    (0xBE, 1, False, "surfFront"),
    (0xBF, 1, False, "surfRear"),
    (0xC0, 1, False, "surfAll"),
]

SEPARATE_RANGES = [(0x000, 0x120, "game_longs (kraschsplitter, state_op_unk)"),
                   (0x222, 0x2F2, "opponentstate (ingen motståndare)")]
DEAD_RANGES = [(lo, hi) for lo, hi, _ in SEPARATE_RANGES]


def rd(buf, off, size, signed):
    fmt = {1: "b" if signed else "B", 2: "h" if signed else "H",
           4: "l" if signed else "L"}[size]
    return struct.unpack_from("<" + fmt, buf, off)[0]


def load(path):
    d = open(path, "rb").read()
    n = struct.unpack_from("<H", d, 0)[0]
    if len(d) != 2 + REC * n:
        print(f"VARNING {path}: storlek {len(d)} != 2+1120*{n}")
        n = (len(d) - 2) // REC
    return [d[2 + i * REC: 2 + (i + 1) * REC] for i in range(n)]


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    a = load(sys.argv[1])          # oracle
    b = load(sys.argv[2])          # native
    n = min(len(a), len(b))
    if len(sys.argv) > 3:
        n = min(n, int(sys.argv[3]))
    print(f"jämför {n} bildrutor (facit {len(a)}, vår {len(b)})\n")

    live = [i for i in range(REC)
            if not any(lo <= i < hi for lo, hi in DEAD_RANGES)]

    first_any = None
    identical = 0
    for f in range(n):
        if a[f] == b[f]:
            identical += 1
        elif first_any is None:
            first_any = f

    print(f"helt identiska poster : {identical}/{n}")
    print(f"första avvikande ruta : {first_any + 1 if first_any is not None else '-'}")

    same_live = sum(1 for f in range(n) for i in live if a[f][i] == b[f][i])
    print(f"byte-överensstämmelse (bilens tillstånd): "
          f"{100.0 * same_live / (n * len(live)):.2f}%")

    print("\nseparat redovisade områden:")
    for lo, hi, label in SEPARATE_RANGES:
        first = next((f for f in range(n) if a[f][lo:hi] != b[f][lo:hi]), None)
        nz = sum(1 for f in range(n) for i in range(lo, hi) if a[f][i])
        print(f"  {label}")
        print(f"    facit har {nz} nollskilda byte; "
              f"första avvikande ruta: "
              f"{first + 1 if first is not None else 'identiska'}")

    print("\nförsta avvikelse per fält i playerstate:")
    print(f"{'fält':22s} {'ruta':>6s}  {'facit':>12s}  {'vår':>12s}")
    for off, size, signed, name in CAR_FIELDS:
        o = PLAYER + off
        hit = None
        for f in range(n):
            va = rd(a[f], o, size, signed)
            vb = rd(b[f], o, size, signed)
            if va != vb:
                hit = (f, va, vb)
                break
        if hit:
            print(f"{name:22s} {hit[0]+1:6d}  {hit[1]:12d}  {hit[2]:12d}")
        else:
            print(f"{name:22s} {'-':>6s}  {'identiska hela vägen':>27s}")

    if first_any is not None:
        print(f"\nbyte-skillnader i första avvikande rutan ({first_any+1}):")
        diffs = [i for i in live if a[first_any][i] != b[first_any][i]]
        print(f"  {len(diffs)} byte skiljer; offsets: "
              f"{', '.join(hex(x) for x in diffs[:24])}"
              f"{' ...' if len(diffs) > 24 else ''}")
        inside = [x for x in diffs if PLAYER <= x < PLAYER + CARSTATE_LEN]
        print(f"  varav i playerstate: {len(inside)}"
              f" (CARSTATE-offset: {', '.join(hex(x - PLAYER) for x in inside[:16])})")


if __name__ == "__main__":
    main()
