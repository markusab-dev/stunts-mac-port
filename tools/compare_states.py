#!/usr/bin/env python3
"""
State Comparison Tool for Stunts Behavioral Verification.
Compares binary state dumps (.BIN / .BNI) generated frame-by-frame
between original DOS Stunts (via repldump) and the native port.
"""

import sys
import struct
from pathlib import Path
from dataclasses import dataclass
from typing import List, Optional, Tuple

GAMESTATE_SIZE = 1120

@dataclass
class Vector3D:
    x: int
    y: int
    z: int

@dataclass
class CarStateSummary:
    pos_x: int
    pos_y: int
    pos_z: int
    rot_x: int
    rot_y: int
    rot_z: int
    speed: int
    speed2: int
    rpm: int
    gear: int
    steering: int
    brake: int
    accel: int
    sliding: int
    crashed: int
    pseudo_gravity: int
    sum_all_wheels: int

@dataclass
class FrameState:
    frame_index: int
    game_frame: int
    player: CarStateSummary
    raw_bytes: bytes

def unpack_carstate(data: bytes, offset: int = 0) -> CarStateSummary:
    # Based on struct CARSTATE in Restunts externs.h (192 bytes)
    pos_x, pos_y, pos_z = struct.unpack_from("<3i", data, offset + 0)
    rot_x, rot_y, rot_z = struct.unpack_from("<3h", data, offset + 24)
    pseudo_grav, steering, currpm, lastrpm, idlerpm2, speeddiff = struct.unpack_from("<6h", data, offset + 30)
    speed, speed2, lastspeed, gearratio = struct.unpack_from("<4H", data, offset + 42)
    
    # Flags at offset 188-207
    is_braking = data[offset + 188]
    is_accel = data[offset + 189]
    current_gear = data[offset + 190]
    sum_front = data[offset + 191]
    sum_rear = data[offset + 192]
    sum_all = data[offset + 193]
    sliding = data[offset + 199]
    crashed = data[offset + 201]
    
    return CarStateSummary(
        pos_x=pos_x, pos_y=pos_y, pos_z=pos_z,
        rot_x=rot_x, rot_y=rot_y, rot_z=rot_z,
        speed=speed, speed2=speed2, rpm=currpm,
        gear=current_gear, steering=steering,
        brake=is_braking, accel=is_accel,
        sliding=sliding, crashed=crashed,
        pseudo_gravity=pseudo_grav,
        sum_all_wheels=sum_all
    )

def read_dump_file(filepath: Path) -> Tuple[int, List[FrameState]]:
    with open(filepath, "rb") as f:
        data = f.read()
        
    if len(data) < 2:
        raise ValueError(f"File too small: {len(data)} bytes")
        
    total_frames = struct.unpack_from("<H", data, 0)[0]
    expected_size = 2 + total_frames * GAMESTATE_SIZE
    
    frames = []
    offset = 2
    frame_idx = 0
    
    while offset + GAMESTATE_SIZE <= len(data) and frame_idx < total_frames:
        frame_bytes = data[offset:offset + GAMESTATE_SIZE]
        game_frame = struct.unpack_from("<h", frame_bytes, 101 * 2)[0] # approx offset
        
        # Player CARSTATE is at offset 334 in GAMESTATE
        # (calculated from sizeof fields before playerstate: 24*4*3 + 4*6 + 2*6 + 2 + 2 + 4 + 2 + 2 + 2 + 2 + 2 + 2 + 2 + 2 = 334)
        player_offset = 334
        player_summary = unpack_carstate(frame_bytes, player_offset)
        
        frames.append(FrameState(
            frame_index=frame_idx,
            game_frame=game_frame,
            player=player_summary,
            raw_bytes=frame_bytes
        ))
        
        offset += GAMESTATE_SIZE
        frame_idx += 1
        
    return total_frames, frames

def compare_state_dumps(file1: Path, file2: Path) -> bool:
    print(f"Comparing State Dumps:")
    print(f"  Reference (Original DOS): {file1}")
    print(f"  Target (Native Port):     {file2}")
    print("=" * 70)
    
    total1, frames1 = read_dump_file(file1)
    total2, frames2 = read_dump_file(file2)
    
    print(f"Reference frame count: {total1} (loaded {len(frames1)})")
    print(f"Target frame count:    {total2} (loaded {len(frames2)})")
    
    min_frames = min(len(frames1), len(frames2))
    first_divergence = None
    
    for i in range(min_frames):
        f1 = frames1[i]
        f2 = frames2[i]
        
        if f1.raw_bytes != f2.raw_bytes:
            # Find byte differences
            diff_indices = [j for j in range(GAMESTATE_SIZE) if f1.raw_bytes[j] != f2.raw_bytes[j]]
            first_divergence = (i, diff_indices, f1, f2)
            break
            
    if first_divergence is None:
        if len(frames1) == len(frames2):
            print("\n[SUCCESS] PERFECT 100% BEHAVIORAL MATCH ACROSS ALL FRAMES!")
            return True
        else:
            print(f"\n[WARNING] All {min_frames} frames matched, but total frame count differs!")
            return False
            
    frame_idx, diff_bytes, f1, f2 = first_divergence
    print(f"\n[DIVERGENCE DETECTED] at Frame {frame_idx} (Simulation time: {frame_idx * 0.05:.2f}s)")
    print(f"Total differing bytes in 1120-byte state: {len(diff_bytes)}")
    print(f"First 10 differing byte offsets: {diff_bytes[:10]}")
    
    p1 = f1.player
    p2 = f2.player
    print("\n--- Player State Comparison ---")
    print(f"{'Field':<20} | {'Original (Ref)':<20} | {'Native Port':<20} | {'Delta':<10}")
    print("-" * 75)
    
    fields = [
        ("Position X", p1.pos_x, p2.pos_x),
        ("Position Y", p1.pos_y, p2.pos_y),
        ("Position Z", p1.pos_z, p2.pos_z),
        ("Rotation X (Pitch)", p1.rot_x, p2.rot_x),
        ("Rotation Y (Yaw)", p1.rot_y, p2.rot_y),
        ("Rotation Z (Roll)", p1.rot_z, p2.rot_z),
        ("Speed (Coupled)", p1.speed, p2.speed),
        ("Speed2 (Actual)", p1.speed2, p2.speed2),
        ("Engine RPM", p1.rpm, p2.rpm),
        ("Current Gear", p1.gear, p2.gear),
        ("Steering Angle", p1.steering, p2.steering),
        ("Braking", p1.brake, p2.brake),
        ("Accelerating", p1.accel, p2.accel),
        ("Sliding Flag", p1.sliding, p2.sliding),
        ("Crashed Flag", p1.crashed, p2.crashed),
        ("Pseudo Gravity", p1.pseudo_gravity, p2.pseudo_gravity),
        ("All Wheels On Ground", p1.sum_all_wheels, p2.sum_all_wheels),
    ]
    
    for name, v1, v2 in fields:
        delta = v2 - v1 if isinstance(v1, int) and isinstance(v2, int) else "N/A"
        match_marker = " " if v1 == v2 else "*"
        print(f"{name:<20} | {str(v1):<20} | {str(v2):<20} | {str(delta):<10} {match_marker}")
        
    return False

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <reference_dump.bin> <target_dump.bin>")
        sys.exit(1)
        
    match = compare_state_dumps(Path(sys.argv[1]), Path(sys.argv[2]))
    sys.exit(0 if match else 1)
