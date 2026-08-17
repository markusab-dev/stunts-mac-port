#!/usr/bin/env python3
"""
extract_dseg_tables.py -- reproducible transcription of initialized DOS data-segment
tables from reference/restunts/src/restunts/asm/dseg.asm into C initializers for
src/render_faithful/rdata.c.

Emits (to stdout):
  - struct TRACKOBJECT trkObjectList[215]
  - struct TRACKOBJECT sceneshapes2[19]
  - struct TRACKOBJECT sceneshapes3[13]
  - struct SHAPE3D* off_3BE44[8]

Method:
  dseg.asm is the IDA-style disassembly of the original data segment.  Labels start
  in column 0; continuation lines are indented "db/dw/dd <decimal>" (one value per
  line).  All symbols are contiguous; a symbol's dseg offset is the running sum of
  the sizes of all preceding data lines.  Label names derived from linear addresses
  (word_3C0D6 = dseg base 0x3B770 + 0x966) are used to self-verify the offset map.

  TRACKOBJECT entries are 14 bytes in the DOS layout (#pragma pack(1), near
  pointers are 2 bytes):
      +0  ss_trkObjInfoPtr (dw, dseg offset into 'shapeinfos', 14-byte TRKOBJINFO)
      +2  ss_rotY          (dw, signed)
      +4  ss_shapePtr      (dw, dseg offset into game3dshapes, 22-byte SHAPE3D)
      +6  ss_loShapePtr    (dw, ditto)
      +8  ss_ssOvelay, ss_surfaceType, ss_ignoreZBias, ss_multiTileFlag,
          ss_physicalModel, scene_unk5 (6 x db)
  Shape pointers are relocated to &game3dshapes[offset/22]; every offset in the
  tables is verified to land on a 22-byte boundary inside game3dshapes.
  ss_trkObjInfoPtr is kept as the raw DOS dseg offset (cast), because the
  'shapeinfos' TRKOBJINFO table is not part of the faithful-renderer port and the
  renderer never dereferences this field; the matching shapeinfos[] index is
  emitted as a comment.
"""
import os
import re
import sys

ASM = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir,
                   "reference", "restunts", "src", "restunts", "asm", "dseg.asm")

DSEG_BASE = 0x3B770
SIZE = {"db": 1, "dw": 2, "dd": 4}
DOS_SIZEOF_SHAPE3D = 22      # 2 + 4(far) + 2 + 2 + 4(far) + 4(far) + 4(far)
DOS_SIZEOF_TRKOBJINFO = 14   # 6x char + int16 + 2(near ptr) + 4x char
DOS_SIZEOF_TRACKOBJECT = 14


def parse(path):
    """Return (blocks, order): blocks[name] = [(directive, valuestr), ...]."""
    blocks, order, cur = {}, [], None
    with open(path) as f:
        for line in f:
            if ";" in line:
                line = line.split(";", 1)[0]
            if not line.strip():
                continue
            if not line[0].isspace():
                m = re.match(r"^(\S+)\s+(db|dw|dd)\s+(.*)$", line)
                if m:
                    cur = m.group(1)
                    blocks[cur] = [(m.group(2), m.group(3).strip())]
                    order.append(cur)
                continue
            m = re.match(r"^\s+(db|dw|dd)\s+(.*)$", line)
            if m and cur is not None:
                blocks[cur].append((m.group(1), m.group(2).strip()))
    return blocks, order


def build_offsets(blocks, order):
    ofs, cur = {}, 0
    for nm in order:
        ofs[nm] = cur
        cur += sum(SIZE[d] for d, _ in blocks[nm])
    # self-check against address-derived label names
    ok = bad = 0
    for nm in order:
        m = re.match(r"^(?:word|byte|unk|off|dword)_([0-9A-F]{5,6})$", nm)
        if m:
            if int(m.group(1), 16) - DSEG_BASE == ofs[nm]:
                ok += 1
            else:
                bad += 1
    assert bad == 0 and ok > 100, f"offset map inconsistent (ok={ok} bad={bad})"
    return ofs


def block_bytes(blocks, nm):
    out = []
    for d, v in blocks[nm]:
        assert re.match(r"^-?\d+$", v), f"symbolic value in {nm}: {v}"
        x = int(v) & ((1 << (8 * SIZE[d])) - 1)
        out.extend((x >> (8 * i)) & 0xFF for i in range(SIZE[d]))
    return out


def emit_trackobject_table(blocks, ofs, name):
    """Emit the static table plus a shape-index side table.

    ss_shapePtr/ss_loShapePtr cannot be statically initialized to
    &game3dshapes[i]: struct TRACKOBJECT is #pragma pack(1), which puts the
    64-bit pointer fields at offsets 10/18 of each 32-byte entry, and the
    Mach-O linker refuses pointer relocations at misaligned data offsets
    ("ld: pointer not aligned").  The static initializer therefore leaves the
    two shape pointers 0 and records the game3dshapes indices (-1 = NULL) in
    a side table; rdata.c patches the pointers in a constructor at startup.
    """
    b = block_bytes(blocks, name)
    assert len(b) % DOS_SIZEOF_TRACKOBJECT == 0, name
    n = len(b) // DOS_SIZEOF_TRACKOBJECT
    g3d, shp_end = ofs["game3dshapes"], ofs["game3dshapes"] + 130 * DOS_SIZEOF_SHAPE3D
    sinfo = ofs["shapeinfos"]
    rows = []
    idxpairs = []
    for i in range(n):
        e = b[i * 14:(i + 1) * 14]
        w = lambda k: e[k] | (e[k + 1] << 8)
        info, roty, shp, lo = w(0), w(2), w(4), w(6)
        if roty >= 0x8000:
            roty -= 0x10000
        def shape_idx(p):
            if p == 0:
                return -1
            assert g3d <= p < shp_end and (p - g3d) % DOS_SIZEOF_SHAPE3D == 0, \
                f"{name}[{i}]: shape ptr 0x{p:X} not a game3dshapes element"
            return (p - g3d) // DOS_SIZEOF_SHAPE3D
        si, li = shape_idx(shp), shape_idx(lo)
        idxpairs.append((si, li))
        if info == 0:
            info_c, info_note = "0", ""
        else:
            q, r = divmod(info - sinfo, DOS_SIZEOF_TRKOBJINFO)
            assert 0 <= info - sinfo < 1680, f"{name}[{i}]: info ptr outside shapeinfos"
            tgt = f"shapeinfos[{q}]" if r == 0 else f"shapeinfos+{info - sinfo}"
            info_c = f"(struct TRKOBJINFO*)0x{info:04X}"
            info_note = f" dseg:{tgt};"
        flags = ", ".join((f"0x{x:02X}" if x > 127 else str(x)) for x in e[8:14])
        rows.append(f"\t{{ {info_c}, {roty}, 0, 0, {flags} }},"
                    f" /* [{i}] shapes {si}/{li};{info_note} */")
    print(f"/* dseg 0x{ofs[name] + DSEG_BASE:05X}: {n} entries x 14 DOS bytes,"
          f" transcribed from dseg.asm.")
    print("   ss_shapePtr/ss_loShapePtr are patched to &game3dshapes[i] at startup")
    print(f"   from {name}_shapeidx[] (misaligned-pointer-relocation workaround"
          " -- see tools/extract_dseg_tables.py). */")
    print(f"struct TRACKOBJECT {name}[{n}] = {{")
    print("\n".join(rows))
    print("};")
    print(f"static const int16_t {name}_shapeidx[{n}][2] = {{")
    for i in range(0, n, 8):
        print("\t" + " ".join(f"{{{a},{b}}}," for a, b in idxpairs[i:i + 8]))
    print("};\n")


def emit_off_3BE44(blocks, ofs):
    print("/* dseg 0x3BE44: dw offset game3dshapes+NNNh -> &game3dshapes[NNN/22]"
          " (DOS sizeof(SHAPE3D) == 22) */")
    print("struct SHAPE3D* off_3BE44[8] = {")
    for d, v in blocks["off_3BE44"]:
        m = re.match(r"^offset game3dshapes\.shape3d_numverts\+([0-9A-F]+)h$", v)
        disp = int(m.group(1), 16)
        q, r = divmod(disp, DOS_SIZEOF_SHAPE3D)
        assert r == 0, f"off_3BE44 displacement 0x{disp:X} not multiple of 22"
        print(f"\t&game3dshapes[{q}], /* +0x{disp:03X} = {disp} = {q}*22 */")
    print("};\n")


def main():
    blocks, order = parse(ASM)
    ofs = build_offsets(blocks, order)
    print("/* Generated by tools/extract_dseg_tables.py -- do not hand-edit the")
    print("   tables below; regenerate instead. */\n")
    emit_off_3BE44(blocks, ofs)
    tables = ("trkObjectList", "sceneshapes2", "sceneshapes3")
    for t in tables:
        emit_trackobject_table(blocks, ofs, t)
    print("/* Patch the pack(1)-misaligned shape pointer fields at startup. */")
    print("__attribute__((constructor)) static void rdata_fixup_trackobject_shapes(void)")
    print("{")
    print("\tsize_t i;")
    for t in tables:
        print(f"\tfor (i = 0; i < sizeof({t}) / sizeof({t}[0]); i++) {{")
        print(f"\t\tif ({t}_shapeidx[i][0] >= 0)")
        print(f"\t\t\t{t}[i].ss_shapePtr = &game3dshapes[{t}_shapeidx[i][0]];")
        print(f"\t\tif ({t}_shapeidx[i][1] >= 0)")
        print(f"\t\t\t{t}[i].ss_loShapePtr = &game3dshapes[{t}_shapeidx[i][1]];")
        print("\t}")
    print("}\n")


if __name__ == "__main__":
    main()
