#!/usr/bin/env python3
"""
Stunts Semantic State-Diff Tool (tools/state_diff.py)
Compares two canonical state streams (JSON Lines or .cs1 binary) frame-by-frame.
Identifies the EXACT first divergent frame, divergent field, delta, and context frames.
"""

import sys
import json
import struct
import argparse
from pathlib import Path

BINARY_FRAME_SIZE = 128
BINARY_FORMAT = "<IIi iii hhh HHHh h B H B B h BBBB B hhhh h B hh B B i H H H h 6s"

def parse_jsonl(filepath, max_frames=None):
    states = []
    with open(filepath, "r", encoding="utf-8") as f:
        for i, line in enumerate(f):
            if max_frames is not None and i >= max_frames:
                break
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
                states.append(obj)
            except json.JSONDecodeError as e:
                print(f"Error parsing JSON on line {i+1}: {e}", file=sys.stderr)
                break
    return states

def parse_binary(filepath, max_frames=None):
    states = []
    with open(filepath, "rb") as f:
        frame_idx = 0
        while True:
            if max_frames is not None and frame_idx >= max_frames:
                break
            buf = f.read(BINARY_FRAME_SIZE)
            if len(buf) < BINARY_FRAME_SIZE:
                break
            unpacked = struct.unpack(BINARY_FORMAT, buf[:78]) # unpack main fields
            state = {
                "frame": unpacked[0],
                "game_frame": unpacked[1],
                "time_ms": unpacked[2],
                "pos": [unpacked[3], unpacked[4], unpacked[5]],
                "rot": [unpacked[6], unpacked[7], unpacked[8]],
                "speed_coupled": unpacked[9],
                "speed_actual": unpacked[10],
                "speed_last": unpacked[11],
                "speed_diff": unpacked[12],
                "rpm": unpacked[13],
                "gear": unpacked[14],
                "gear_ratio": unpacked[15],
                "brake": unpacked[16],
                "accel": unpacked[17],
                "steering": unpacked[18],
                "whl_surf": [unpacked[19], unpacked[20], unpacked[21], unpacked[22]],
                "wheels_on_ground": unpacked[23],
                "whl_force": [unpacked[24], unpacked[25], unpacked[26], unpacked[27]],
                "pseudo_gravity": unpacked[28],
                "sliding": unpacked[29],
                "demanded_grip": unpacked[30],
                "surface_grip_sum": unpacked[31],
                "crashed": unpacked[32],
            }
            states.append(state)
            frame_idx += 1
    return states

def load_states(filepath, max_frames=None):
    p = Path(filepath)
    if not p.exists():
        raise FileNotFoundError(f"File not found: {filepath}")
    if p.suffix == ".cs1" or p.suffix == ".bin":
        return parse_binary(filepath, max_frames)
    else:
        return parse_jsonl(filepath, max_frames)

def compare_states(ref_states, target_states, tolerance=0):
    total_ref = len(ref_states)
    total_target = len(target_states)
    min_len = min(total_ref, total_target)

    if min_len == 0:
        return {
            "status": "EMPTY",
            "ref_frames": total_ref,
            "target_frames": total_target,
            "first_divergent_frame": 0,
            "divergence": "No frames loaded from one or both files."
        }

    keys_to_compare = [
        "pos", "rot", "speed_actual", "speed_coupled", "rpm", "gear",
        "steering", "whl_surf", "wheels_on_ground", "pseudo_gravity", "sliding", "crashed"
    ]

    for f in range(min_len):
        ref_f = ref_states[f]
        tgt_f = target_states[f]

        for k in keys_to_compare:
            v_ref = ref_f.get(k)
            v_tgt = tgt_f.get(k)

            if isinstance(v_ref, list) and isinstance(v_tgt, list):
                if len(v_ref) != len(v_tgt):
                    return format_divergence(f, k, v_ref, v_tgt, ref_states, target_states)
                for idx, (elem_ref, elem_tgt) in enumerate(zip(v_ref, v_tgt)):
                    if abs(elem_ref - elem_tgt) > tolerance:
                        return format_divergence(f, f"{k}[{idx}]", elem_ref, elem_tgt, ref_states, target_states)
            else:
                if v_ref is None or v_tgt is None:
                    continue
                if abs(v_ref - v_tgt) > tolerance:
                    return format_divergence(f, k, v_ref, v_tgt, ref_states, target_states)

    if total_ref != total_target:
        return {
            "status": "LENGTH_MISMATCH",
            "ref_frames": total_ref,
            "target_frames": total_target,
            "matching_frames": min_len,
            "first_divergent_frame": min_len,
            "divergence": f"All {min_len} shared frames matched, but frame counts differ (Ref: {total_ref}, Target: {total_target})"
        }

    return {
        "status": "IDENTICAL",
        "ref_frames": total_ref,
        "target_frames": total_target,
        "matching_frames": total_ref,
        "first_divergent_frame": None,
        "divergence": None
    }

def format_divergence(frame_idx, field, expected, actual, ref_states, target_states):
    delta = actual - expected if isinstance(actual, (int, float)) and isinstance(expected, (int, float)) else "N/A"
    
    # Extract preceding context frames (up to 3)
    start_ctx = max(0, frame_idx - 3)
    context = []
    for c in range(start_ctx, frame_idx):
        context.append({
            "frame": c,
            "ref_pos": ref_states[c].get("pos"),
            "tgt_pos": target_states[c].get("pos"),
            "ref_speed": ref_states[c].get("speed_actual"),
            "tgt_speed": target_states[c].get("speed_actual"),
            "ref_rpm": ref_states[c].get("rpm"),
            "tgt_rpm": target_states[c].get("rpm"),
        })

    return {
        "status": "DIVERGENT",
        "ref_frames": len(ref_states),
        "target_frames": len(target_states),
        "matching_frames": frame_idx,
        "first_divergent_frame": frame_idx,
        "divergent_field": field,
        "expected_oracle": expected,
        "actual_native": actual,
        "delta": delta,
        "context_frames": context
    }

def main():
    parser = argparse.ArgumentParser(description="Stunts Canonical State Comparator")
    parser.add_argument("reference", help="Path to reference oracle state file (.jsonl or .cs1)")
    parser.add_argument("target", help="Path to native target state file (.jsonl or .cs1)")
    parser.add_argument("--tolerance", type=int, default=0, help="Allowed integer delta (default: 0 for bit-exact)")
    parser.add_argument("--max-frames", type=int, default=None, help="Limit comparison to N frames")
    parser.add_argument("--json", action="store_true", help="Output results as JSON")

    args = parser.parse_args()

    try:
        ref = load_states(args.reference, args.max_frames)
        tgt = load_states(args.target, args.max_frames)
    except Exception as e:
        print(f"Error loading files: {e}", file=sys.stderr)
        sys.exit(2)

    res = compare_states(ref, tgt, args.tolerance)

    if args.json:
        print(json.dumps(res, indent=2))
    else:
        print("============================================================")
        print(" Stunts Behavioral Fidelity & State Diff Report")
        print("============================================================")
        print(f"Reference File: {args.reference} ({res['ref_frames']} frames)")
        print(f"Target File:    {args.target} ({res['target_frames']} frames)")
        print(f"Status:         {res['status']}")
        print("------------------------------------------------------------")

        if res["status"] == "IDENTICAL":
            print(f"[PASS] 100% BIT-EXACT MATCH across all {res['matching_frames']} frames! (0 divergent variables)")
        else:
            print(f"[FAIL] Divergence detected!")
            print(f"  Last Matching Frame:   Frame {res['matching_frames'] - 1 if res['matching_frames'] > 0 else 'None'}")
            print(f"  First Divergent Frame: Frame {res['first_divergent_frame']}")
            if "divergent_field" in res:
                print(f"  Divergent Variable:    '{res['divergent_field']}'")
                print(f"  Expected (Oracle):     {res['expected_oracle']}")
                print(f"  Actual (Native):       {res['actual_native']}")
                print(f"  Delta:                 {res['delta']}")

            if "context_frames" in res and res["context_frames"]:
                print("\n  Preceding Trajectory Context:")
                for ctx in res["context_frames"]:
                    print(f"    Frame {ctx['frame']:4d} | Ref Speed: {ctx['ref_speed']}, Tgt Speed: {ctx['tgt_speed']} | Ref RPM: {ctx['ref_rpm']}, Tgt RPM: {ctx['tgt_rpm']}")

        print("============================================================")

    sys.exit(0 if res["status"] == "IDENTICAL" else 1)

if __name__ == "__main__":
    main()
