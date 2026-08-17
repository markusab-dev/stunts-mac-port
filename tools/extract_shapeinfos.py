#!/usr/bin/env python3
"""
tools/extract_shapeinfos.py - emit src/sim_faithful/sfshapeinfo.c

`shapeinfos` is the 120-entry TRKOBJINFO table that says, per track element,
where its drivable surface sits.  sub_18D60 (the routine that answers "what am
I standing on?") reaches it through trkObjectList[].ss_trkObjInfoPtr, so
without it every hill and ramp is invisible to the simulation.

restunts stores the table as 1680 anonymous `db` lines, where si_cameraDataOffset
is a bare DOS segment offset that cannot be relocated.  restunts2's Ghidra
export stores the same bytes as structured `TRKOBJINFO <...>` records whose
si_cameraDataOffset is a *symbol* (shapedata174, shapedata42_2, ...), which is
what makes the table portable.

Both are parsed and the 1680 bytes compared, so the structured read is checked
against the original byte image rather than trusted.

Usage: extract_shapeinfos.py [--check]      (--check: verify only, write nothing)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RS1 = ROOT / "reference/restunts/src/restunts/asmorig/dseg.asm"
RS2 = ROOT / "reference/restunts2/src/asm/dseg.asm"
OUT = ROOT / "src/sim_faithful/sfshapeinfo.c"

N_SHAPEINFOS = 120
TRKOBJINFO_SIZE = 14
TRACKOBJECT_SIZE = 14

# TRKOBJINFO field widths in DOS bytes, in declaration order.
FIELDS = [
    ("si_noOfBlocks", 1), ("si_entryPoint", 1), ("si_exitPoint", 1),
    ("si_entryType", 1), ("si_exitType", 1), ("si_arrowType", 1),
    ("si_arrowOrient", 2), ("si_cameraDataOffset", 2),
    ("si_opp1", 1), ("si_opp2", 1), ("si_opp3", 1), ("si_oppSpedCode", 1),
]


def die(msg):
    print(f"FEL: {msg}", file=sys.stderr)
    sys.exit(1)


def read_lines(path):
    return path.read_text(encoding="latin-1").replace("\r", "").split("\n")


# ---------------------------------------------------------------- restunts 1
def rs1_label_bytes(lines, label, count):
    """Collect `count` bytes of `db` data starting at the line defining label."""
    start = next((i for i, l in enumerate(lines)
                  if re.match(rf"^{label}\s", l)), None)
    if start is None:
        die(f"hittar inte etiketten {label} i {RS1}")
    out = []
    for line in lines[start:]:
        m = re.match(r"^(?:\S+\s+)?d([bwd])\s+(.*)$", line.strip())
        if not m:
            if out:
                break
            continue
        width = {"b": 1, "w": 2, "d": 4}[m.group(1)]
        for tok in m.group(2).split(";")[0].split(","):
            tok = tok.strip()
            if not tok:
                continue
            v = int(tok[:-1], 16) if tok.lower().endswith("h") else int(tok, 0)
            out.extend((v >> (8 * k)) & 0xFF for k in range(width))
        if len(out) >= count:
            break
    if len(out) < count:
        die(f"{label}: fick {len(out)} byte, väntade {count}")
    return bytes(out[:count])


# ---------------------------------------------------------------- restunts 2
def rs2_struct_records(lines, label, struct_name, n_fields):
    """Read consecutive `STRUCT <a, b, ...>` records starting at label."""
    start = next((i for i, l in enumerate(lines)
                  if re.match(rf"^{label}\s+{struct_name}\s*<", l)), None)
    if start is None:
        die(f"hittar inte {label} {struct_name} i {RS2}")
    recs = []
    for i, line in enumerate(lines[start:]):
        # sceneshapes2/sceneshapes3 follow trkObjectList and are the same
        # struct type, so the run has to end at the next label, not at the
        # next line that fails to parse.
        if i and re.match(r"^\S", line):
            break
        m = re.search(rf"{struct_name}\s*<([^>]*)>", line)
        if not m:
            if recs and line.strip() and not line.strip().startswith(";"):
                break
            continue
        vals = [v.strip() for v in m.group(1).split(",")]
        if len(vals) != n_fields:
            die(f"{label}: {len(vals)} fält, väntade {n_fields}: {line}")
        recs.append(vals)
    return recs


def rs2_offset_map(lines):
    """label -> dseg offset, anchored so that shapeinfos lands at 0x1A08.

    0x1A08 is not a guess: rdata.c independently recorded trkObjectList at
    linear 0x3D808 with dseg based at 0x3B770, i.e. dseg offset 0x2098, and
    trkObjectList directly follows shapeinfos' 1680 bytes.  The anchor is
    asserted below rather than assumed.
    """
    def size_of(line):
        m = re.search(r"\bd([bwd])\s+(.*)$", line)
        if m:
            width = {"b": 1, "w": 2, "d": 4}[m.group(1)]
            items = [t for t in m.group(2).split(";")[0].split(",") if t.strip()]
            return width * len(items)
        if re.search(r"\b(TRKOBJINFO|TRACKOBJECT)\s*<", line):
            return TRKOBJINFO_SIZE
        return 0

    off, labels = 0, {}
    for line in lines:
        m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)\s+(?:d[bwd]\s|TRKOBJINFO|TRACKOBJECT)",
                     line)
        if m:
            labels.setdefault(m.group(1), off)
        off += size_of(line)

    if "shapeinfos" not in labels:
        die("shapeinfos saknas i offsetkartan")
    shift = 0x1A08 - labels["shapeinfos"]
    labels = {k: v + shift for k, v in labels.items()}
    want = 0x1A08 + N_SHAPEINFOS * TRKOBJINFO_SIZE
    if labels.get("trkObjectList") != want:
        die(f"offsetkartan stämmer inte: trkObjectList på "
            f"{labels.get('trkObjectList'):#06x}, väntade {want:#06x}")
    return labels


def resolve_offset(labels, blob_bytes, raw):
    """A raw dseg offset -> (label, delta) inside a known blob."""
    best = None
    for name, size in ((n, len(b)) for n, b in blob_bytes.items()):
        base = labels.get(name)
        if base is not None and base <= raw < base + size:
            best = (name, raw - base)
    return best


def rs2_blob(lines, label):
    """Bytes of a `label db ...` run, up to the next labelled line."""
    start = next((i for i, l in enumerate(lines)
                  if re.match(rf"^{label}\s+db\s", l)), None)
    if start is None:
        die(f"hittar inte blobben {label} i {RS2}")
    out = []
    for i, line in enumerate(lines[start:]):
        if i and re.match(r"^\S", line):        # next label ends the run
            break
        m = re.search(r"\bdb\s+(.*)$", line)
        if not m:
            if out:
                break
            continue
        for tok in m.group(1).split(";")[0].split(","):
            tok = tok.strip()
            if tok:
                out.append(int(tok, 0) & 0xFF)
    return bytes(out)


def main():
    check_only = "--check" in sys.argv
    l1, l2 = read_lines(RS1), read_lines(RS2)

    # --- structured read ---------------------------------------------------
    recs = rs2_struct_records(l2, "shapeinfos", "TRKOBJINFO", len(FIELDS))
    if len(recs) != N_SHAPEINFOS:
        die(f"shapeinfos: {len(recs)} poster, väntade {N_SHAPEINFOS}")

    # Every symbolic si_cameraDataOffset, in first-seen order.
    blob_names, blob_bytes = [], {}
    for r in recs:
        sym = r[7]
        if re.match(r"^(0x)?[0-9A-Fa-f]+$", sym):
            continue                             # a literal, not a symbol
        if sym not in blob_bytes:
            blob_names.append(sym)
            blob_bytes[sym] = rs2_blob(l2, sym)

    # --- cross-check against the restunts 1 byte image ---------------------
    raw1 = rs1_label_bytes(l1, "shapeinfos", N_SHAPEINFOS * TRKOBJINFO_SIZE)

    # Rebuild the same image from the structured records.  Symbolic pointers
    # can't be reproduced (they are dseg offsets we deliberately dropped), so
    # those two bytes are compared as "both present" rather than by value.
    ptr_gaps = 0
    mismatch = []
    for i, r in enumerate(recs):
        base = i * TRKOBJINFO_SIZE
        off = 0
        for (name, width), tok in zip(FIELDS, r):
            if name == "si_cameraDataOffset" and not re.match(
                    r"^(0x)?[0-9A-Fa-f]+$", tok):
                ptr_gaps += 1
                off += width
                continue
            v = int(tok, 0)
            for k in range(width):
                got, want = (v >> (8 * k)) & 0xFF, raw1[base + off + k]
                if got != want:
                    mismatch.append((i, name, got, want))
            off += width
        if off != TRKOBJINFO_SIZE:
            die(f"post {i}: {off} byte, väntade {TRKOBJINFO_SIZE}")

    print(f"shapeinfos      : {len(recs)} poster, "
          f"{len(raw1)} byte lästa ur restunts1")
    print(f"symbolpekare    : {ptr_gaps} av {len(recs)} "
          f"(si_cameraDataOffset), {len(blob_names)} unika mål")
    if mismatch:
        for m in mismatch[:12]:
            print(f"  post {m[0]:3d} {m[1]:20s} restunts2={m[2]:#04x} "
                  f"restunts1={m[3]:#04x}")
        die(f"{len(mismatch)} byte skiljer mellan restunts1 och restunts2")
    print("korsvalidering  : alla icke-pekarbyte identiska i båda källorna")

    total = sum(len(b) for b in blob_bytes.values())
    print(f"ytdata          : {total} byte i {len(blob_names)} block")
    for n in blob_names:
        if not blob_bytes[n]:
            die(f"blobben {n} blev tom")

    # --- si_opp1/si_opp2 read as one word ---------------------------------
    # sub_18D60 reads TRKOBJINFO+10 as a WORD, and where it is nonzero the
    # value is a near dseg offset used instead of si_cameraDataOffset.  Ghidra
    # keeps it as two byte fields, so it has to be relocated by hand.
    labels = rs2_offset_map(l2)
    opp = []
    for i, r in enumerate(recs):
        raw = (int(r[8], 0) & 0xFF) | ((int(r[9], 0) & 0xFF) << 8)
        if raw == 0:
            opp.append(None)
            continue
        hit = resolve_offset(labels, blob_bytes, raw)
        if hit is None:
            die(f"shapeinfos[{i}]: si_opp-ordet {raw:#06x} pekar utanför "
                f"all känd ytdata")
        opp.append(hit)
    used = [(i, h) for i, h in enumerate(opp) if h]
    print(f"si_opp-pekare   : {len(used)} poster, alla upplösta "
          f"({', '.join(f'[{i}]->{h[0]}+{h[1]}' for i, h in used[:3])}"
          f"{', ...' if len(used) > 3 else ''})")

    # --- trkObjectList -> shapeinfos index --------------------------------
    tol = rs2_struct_records(l2, "trkObjectList", "TRACKOBJECT", 10)
    idx = []
    for r in tol:
        sym = r[0]
        m = re.match(r"^shapeinfos(?:\s*\+\s*(0x[0-9A-Fa-f]+|\d+))?$", sym)
        if not m:
            idx.append(-1)                      # word_3B770: the null entry
            continue
        byteoff = int(m.group(1), 0) if m.group(1) else 0
        if byteoff % TRKOBJINFO_SIZE:
            die(f"trkObjectList-offset {byteoff} är inte en multipel av 14")
        e = byteoff // TRKOBJINFO_SIZE
        if not 0 <= e < N_SHAPEINFOS:
            die(f"trkObjectList pekar på shapeinfos[{e}], utanför tabellen")
        idx.append(e)
    print(f"trkObjectList   : {len(tol)} poster, "
          f"{sum(1 for i in idx if i >= 0)} pekar in i shapeinfos")

    if check_only:
        return

    # --- emit --------------------------------------------------------------
    w = ["/* Generated by tools/extract_shapeinfos.py - do not edit.",
         " *",
         " * shapeinfos and the surface blobs si_cameraDataOffset points at.",
         " * Bytes come from restunts2's Ghidra export (symbolic pointers) and are",
         " * checked field by field against restunts1's raw dseg image.",
         " */",
         "#include <stdint.h>",
         '#include "sfport.h"',
         '#include "externs.h"',
         ""]

    for n in blob_names:
        b = blob_bytes[n]
        w.append(f"/* dseg {n}: {len(b)} bytes, read as 6-byte vectors */")
        w.append(f"static const uint8_t {n}[{len(b)}] = {{")
        for i in range(0, len(b), 12):
            w.append("\t" + ", ".join(f"0x{x:02X}" for x in b[i:i + 12]) + ",")
        w.append("};")
        w.append("")

    w.append("/* struct TRKOBJINFO is #pragma pack(1), so its pointer field lands on")
    w.append(" * unaligned addresses and Mach-O refuses to emit a relocation for it.")
    w.append(" * si_cameraDataOffset is therefore left null here and filled in by the")
    w.append(" * constructor below, exactly as rdata.c does for ss_shapePtr. */")
    w.append(f"struct TRKOBJINFO shapeinfos[{N_SHAPEINFOS}] = {{")
    cam_src = []
    for i, r in enumerate(recs):
        cells = []
        for (name, _), tok in zip(FIELDS, r):
            if name == "si_cameraDataOffset":
                if re.match(r"^(0x)?[0-9A-Fa-f]+$", tok):
                    if int(tok, 0) != 0:
                        die(f"shapeinfos[{i}]: si_cameraDataOffset {tok} är en "
                            f"literal skild från noll, kan inte flyttas")
                    cam_src.append(None)
                else:
                    cam_src.append((blob_names.index(tok), 0))
                cells.append("0")
                continue
            v = int(tok, 0)
            cells.append("0" if v == 0 else f"0x{v:02X}"
                         if v < 0x100 else f"0x{v:04X}")
        w.append(f"\t{{ {', '.join(cells)} }},   /* [{i}] "
                 f"{r[7] if cam_src[i] else '-'} */")
    w.append("};")
    w.append("")

    w.append(f"static const uint8_t* const shapedata_blobs[{len(blob_names)}] = {{")
    for n in blob_names:
        w.append(f"\t{n},")
    w.append("};")
    w.append("")
    w.append("/* si_cameraDataOffset as (blob index, byte offset); -1 = null. */")
    w.append(f"static const int16_t shapeinfo_camera_blob[{N_SHAPEINFOS}] = {{")
    for i in range(0, N_SHAPEINFOS, 16):
        w.append("\t" + ", ".join(
            f"{-1 if cam_src[j] is None else cam_src[j][0]:3d}"
            for j in range(i, min(i + 16, N_SHAPEINFOS))) + ",")
    w.append("};")
    w.append("")

    w.append("/* The si_opp1/si_opp2 pair read as one word, relocated. sub_18D60 uses")
    w.append(" * this instead of si_cameraDataOffset where it is non-NULL; the struct")
    w.append(" * declares the two bytes separately, so the pointer cannot live there. */")
    w.append(f"const uint8_t* shapeinfo_opp_ptr[{N_SHAPEINFOS}];")
    w.append("")
    w.append("/* (blob index, byte offset) for the six entries that use it; -1 = none. */")
    w.append(f"static const int16_t shapeinfo_opp_blob[{N_SHAPEINFOS}][2] = {{")
    for i, h in enumerate(opp):
        if h is None:
            w.append("\t{ -1, 0 },")
        else:
            w.append(f"\t{{ {blob_names.index(h[0]):3d}, {h[1]:4d} }},"
                     f"   /* [{i}] {h[0]}+{h[1]} */")
    w.append("};")
    w.append("")

    w.append("/* trkObjectList[i].ss_trkObjInfoPtr as a shapeinfos index; -1 = the")
    w.append(" * dseg null entry (word_3B770), which the original never dereferences. */")
    w.append(f"static const int16_t trkobj_shapeinfo_idx[{len(idx)}] = {{")
    for i in range(0, len(idx), 16):
        w.append("\t" + ", ".join(f"{x:3d}" for x in idx[i:i + 16]) + ",")
    w.append("};")
    w.append("")
    w.append(f"""/* The 48 trkObjectList entries with no shapeinfo (blank tile, ghost cars) held
 * dseg offset 0 in the original, which under DOS is a readable address holding
 * unrelated bytes. There is no way to reproduce those bytes here, so they point
 * at a zeroed block instead: reads stay defined and deterministic rather than
 * faulting. shapeinfo_null_hits counts every time sub_18D60 lands on one, so
 * "this never happens on a real drive" can be shown rather than assumed. */
struct TRKOBJINFO shapeinfo_null[16];
unsigned long shapeinfo_null_hits;

/* trkObjectList lives in rdata.c with ss_trkObjInfoPtr still holding the raw
 * DOS offset it had in dseg. Relocate it once, before main runs, the same way
 * rdata.c already relocates ss_shapePtr. */
extern struct TRACKOBJECT trkObjectList[];

__attribute__((constructor))
static void sfshapeinfo_relocate(void)
{{
	for (int i = 0; i < {N_SHAPEINFOS}; i++) {{
		int16_t b = shapeinfo_camera_blob[i];
		if (b >= 0)
			shapeinfos[i].si_cameraDataOffset =
				(int16_t*)shapedata_blobs[b];
		b = shapeinfo_opp_blob[i][0];
		if (b >= 0)
			shapeinfo_opp_ptr[i] =
				shapedata_blobs[b] + shapeinfo_opp_blob[i][1];
	}}
	for (int i = 0; i < {len(idx)}; i++) {{
		int16_t e = trkobj_shapeinfo_idx[i];
		trkObjectList[i].ss_trkObjInfoPtr =
			e < 0 ? shapeinfo_null : &shapeinfos[e];
	}}
}}""")

    OUT.write_text("\n".join(w) + "\n")
    print(f"skrev           : {OUT.relative_to(ROOT)} "
          f"({len(w)} rader)")


if __name__ == "__main__":
    main()
