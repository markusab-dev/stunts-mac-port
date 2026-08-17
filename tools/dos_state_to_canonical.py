#!/usr/bin/env python3
"""
DOS GameState to Canonical State Converter (tools/dos_state_to_canonical.py)
Converts 1,120-byte DOS GAMESTATE frames produced by repldumo.exe / repldump.exe
into STUNTS_CANONICAL_STATE_V1 JSON Lines (.jsonl) or binary (.cs1).
"""

import sys
import struct
import json
import argparse
from pathlib import Path

DOS_GAMESTATE_SIZE = 1120

def parse_dos_gamestate_frame(frame_idx, raw_bytes):
    if len(raw_bytes) < DOS_GAMESTATE_SIZE:
        return None

    # Offsets in struct GAMESTATE (verified against restunts externs.h)
    # game_travDist @ 312 (int32)
    # game_frame @ 316 (int16)
    # game_penalty @ 330 (int16)
    # game_impactSpeed @ 332 (uint16)
    # game_topSpeed @ 334 (uint16)
    # game_jumpCount @ 336 (int16)
    game_travDist, game_frame = struct.unpack_from("<li", raw_bytes, 312)[:2]
    game_frame = struct.unpack_from("<h", raw_bytes, 316)[0]
    game_penalty, game_impactSpeed, game_topSpeed, game_jumpCount = struct.unpack_from("<hHHh", raw_bytes, 330)

    # playerstate starts @ 340 (0x154)
    p_off = 340
    lx1, ly1, lz1 = struct.unpack_from("<lll", raw_bytes, p_off + 0)
    rot_pitch, rot_yaw, rot_roll = struct.unpack_from("<hhh", raw_bytes, p_off + 24)
    pseudo_gravity = struct.unpack_from("<h", raw_bytes, p_off + 30)[0]
    steering_angle = struct.unpack_from("<h", raw_bytes, p_off + 32)[0]
    curr_rpm, last_rpm, idle_rpm2, speeddiff = struct.unpack_from("<hhhh", raw_bytes, p_off + 34)
    speed_coupled, speed_actual, last_speed, gear_ratio = struct.unpack_from("<HHHH", raw_bytes, p_off + 42)

    demanded_grip = struct.unpack_from("<h", raw_bytes, p_off + 68)[0]
    surface_grip_sum = struct.unpack_from("<h", raw_bytes, p_off + 70)[0]

    rc1 = struct.unpack_from("<4h", raw_bytes, p_off + 76)

    is_braking = raw_bytes[p_off + 188]
    is_accel = raw_bytes[p_off + 189]
    current_gear = raw_bytes[p_off + 190]
    sum_front = raw_bytes[p_off + 191]
    sum_rear = raw_bytes[p_off + 192]
    sum_all = raw_bytes[p_off + 193]
    surf_whl = list(raw_bytes[p_off + 194 : p_off + 198])
    limiter_timer = raw_bytes[p_off + 198]
    sliding_flag = raw_bytes[p_off + 199]
    crash_flag = raw_bytes[p_off + 201]

    # kevinseed @ 1018 (0x3FA)
    kevin_seed = list(raw_bytes[1018:1024]) if len(raw_bytes) >= 1024 else [0]*6

    canonical = {
        "schema": "STUNTS_CANONICAL_STATE_V1",
        "frame": frame_idx + 1,
        "game_frame": game_frame,
        "time_ms": (frame_idx + 1) * 50,
        "pos": [lx1, ly1, lz1],
        "rot": [rot_pitch, rot_yaw, rot_roll],
        "speed_coupled": speed_coupled,
        "speed_actual": speed_actual,
        "speed_last": last_speed,
        "speed_diff": speeddiff,
        "rpm": curr_rpm,
        "gear": current_gear,
        "gear_ratio": gear_ratio,
        "brake": is_braking,
        "accel": is_accel,
        "steering": steering_angle,
        "whl_surf": surf_whl,
        "wheels_on_ground": sum_all,
        "whl_force": list(rc1),
        "pseudo_gravity": pseudo_gravity,
        "sliding": sliding_flag,
        "demanded_grip": demanded_grip,
        "surface_grip_sum": surface_grip_sum,
        "crashed": crash_flag,
        "distance": game_travDist,
        "penalty": game_penalty,
        "impact_speed": game_impactSpeed,
    }
    return canonical

def convert_dos_bin_to_canonical(input_path, output_jsonl_path, max_frames=None):
    with open(input_path, "rb") as f:
        # First 2 bytes are recorded frame count (unsigned short)
        hdr = f.read(2)
        if len(hdr) < 2:
            return 0
        total_recorded = struct.unpack("<H", hdr)[0]

        frame_count = 0
        with open(output_jsonl_path, "w", encoding="utf-8") as out_f:
            while True:
                if max_frames is not None and frame_count >= max_frames:
                    break
                raw = f.read(DOS_GAMESTATE_SIZE)
                if len(raw) < DOS_GAMESTATE_SIZE:
                    break
                state = parse_dos_gamestate_frame(frame_count, raw)
                if state:
                    out_f.write(json.dumps(state) + "\n")
                    frame_count += 1

    return frame_count

def main():
    parser = argparse.ArgumentParser(description="Convert DOS GAMESTATE .BIN to STUNTS_CANONICAL_STATE_V1")
    parser.add_argument("input_bin", help="Path to DOS .BIN state dump")
    parser.add_argument("output_jsonl", help="Path to output .jsonl canonical file")
    parser.add_argument("--max-frames", type=int, default=None, help="Max frames to convert")
    args = parser.parse_args()

    frames = convert_dos_bin_to_canonical(args.input_bin, args.output_jsonl, args.max_frames)
    print(f"Converted {frames} frames from '{args.input_bin}' to '{args.output_jsonl}'")

if __name__ == "__main__":
    main()
