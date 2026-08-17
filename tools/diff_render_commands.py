#!/usr/bin/env python3
"""
tools/diff_render_commands.py - Semantic Render-Command Stream Comparator
Diffs normalized render commands from Restunts reference vs native port.
"""

import sys
import json
from pathlib import Path

def diff_command_streams(ref_file, native_file):
    with open(ref_file, "r") as f:
        ref = json.load(f)
    with open(native_file, "r") as f:
        nat = json.load(f)

    print("================================================================================")
    print("         STUNTS SEMANTIC RENDER-COMMAND ORACLE COMPARISON REPORT                ")
    print("================================================================================")
    print(f"Reference Stream: {ref_file}")
    print(f"Native Stream:    {native_file}\n")

    # Step 1: Simulation Values
    sim_ref = ref.get("simulation", {})
    sim_nat = nat.get("simulation", {})
    if sim_ref != sim_nat:
        print("[MISMATCH #1] SIMULATION VALUES DIFFER")
        print(f"  Expected (Restunts): {sim_ref}")
        print(f"  Actual (Native):     {sim_nat}")
        print("  Responsible function: stunts_sim_get_canonical_state / stunts_sim_step")
        return 1
    print("  [PASS 1/5] Simulation State: Exact match (Player pos & rot identical)")

    # Step 2: Camera Values
    cam_ref = ref.get("camera", {})
    cam_nat = nat.get("camera", {})
    if cam_ref != cam_nat:
        print("\n[MISMATCH #2] CAMERA STATE DIFFERS")
        print(f"  Expected (Restunts): {json.dumps(cam_ref, indent=2)}")
        print(f"  Actual (Native):     {json.dumps(cam_nat, indent=2)}")
        print("  Responsible function: update_frame() camera positioning")
        return 2
    print("  [PASS 2/5] Camera State: Exact match (World pos, rotation, heading, lookahead index)")

    # Step 3: Visible Tile List
    tiles_ref = ref.get("visible_tiles", [])
    tiles_nat = nat.get("visible_tiles", [])
    if len(tiles_ref) != len(tiles_nat):
        print(f"\n[MISMATCH #3] VISIBLE TILE COUNT DIFFERS ({len(tiles_ref)} vs {len(tiles_nat)})")
        print("  Responsible function: 23-tile lookahead cone selector")
        return 3

    for i in range(len(tiles_ref)):
        if tiles_ref[i] != tiles_nat[i]:
            print(f"\n[MISMATCH #3] VISIBLE TILE LIST ENTRY {i} DIFFERS")
            print(f"  Expected: {tiles_ref[i]}")
            print(f"  Actual:   {tiles_nat[i]}")
            print("  Responsible function: lookahead_tiles_tables / bounds culling")
            return 3
    print(f"  [PASS 3/5] Visible Tile List: Exact match ({len(tiles_ref)}/{len(tiles_ref)} tiles in identical draw order)")

    # Step 4 & 5: Transformed Shapes
    shapes_ref = ref.get("transformed_shapes", [])
    shapes_nat = nat.get("transformed_shapes", [])
    if len(shapes_ref) != len(shapes_nat):
        print(f"\n[MISMATCH #4] TRANSFORMED SHAPE COUNT DIFFERS ({len(shapes_ref)} vs {len(shapes_nat)})")
        print("  Responsible function: transformed_shape_op submission queue")
        return 4

    for i in range(len(shapes_ref)):
        if shapes_ref[i] != shapes_nat[i]:
            print(f"\n[MISMATCH #5] TRANSFORMED SHAPE #{i+1} DIFFERS")
            print(f"  Expected: {shapes_ref[i]}")
            print(f"  Actual:   {shapes_nat[i]}")
            print("  Responsible function: shape submission / coordinate transformation")
            return 5
    print(f"  [PASS 4/5] Transformed Shapes: Exact match ({len(shapes_ref)}/{len(shapes_ref)} shape submissions identical)")

    print("\n================================================================================")
    print(" [COMPLETE SUCCESS] ALL RENDER COMMANDS MATCH 100% BIT-EXACT WITH RESTUNTS!     ")
    print("================================================================================")
    return 0

if __name__ == "__main__":
    ref_p = sys.argv[1] if len(sys.argv) > 1 else "tests/render_oracle/frame_00000_restunts.json"
    nat_p = sys.argv[2] if len(sys.argv) > 2 else "tests/render_oracle/frame_00000_native.json"
    sys.exit(diff_command_streams(ref_p, nat_p))
