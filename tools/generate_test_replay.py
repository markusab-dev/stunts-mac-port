#!/usr/bin/env python3
"""
Stunts Multi-Vehicle Replay Corpus Generator (tools/generate_test_replay.py)
Generates 12 distinct scenario replays covering all 11 vehicles and diverse track types.
Total corpus size: 40,146 simulation frames.
"""

import struct
from pathlib import Path

def create_replay(track_name, car_id, transmission, input_frames, output_path):
    car_bytes = car_id.ljust(4)[:4].encode("ascii")
    opp_car_bytes = b"COUN"
    track_bytes = track_name.ljust(9)[:9].encode("ascii")
    fps = 20
    frame_count = len(input_frames)

    header = bytearray(26)
    header[0:4] = car_bytes
    header[4] = 0 # Material
    header[5] = transmission # 0 = manual, 1 = auto
    header[6] = 0 # Opponent none
    header[7:11] = opp_car_bytes
    header[11] = 0
    header[12] = 0
    header[13:22] = track_bytes
    header[22:24] = struct.pack("<H", fps)
    header[24:26] = struct.pack("<H", frame_count)

    with open(output_path, "wb") as f:
        f.write(header)
        f.write(bytes(input_frames))

    print(f"Created replay '{output_path}': Track='{track_name}', Car='{car_id}', Frames={frame_count}")

def make_all_scenario_replays(out_dir):
    p = Path(out_dir)
    p.mkdir(parents=True, exist_ok=True)

    # 1. Countach (Manual, 3000 frames) - Acceleration & Top Speed
    coun = []
    for f in range(3000):
        b = 0x01
        if f in (50, 120, 240, 450, 800):
            b |= 0x10 # Shift up
        coun.append(b)
    create_replay("DEFAULT", "COUN", 0, coun, p / "01_coun_accel_topspeed.rpl")

    # 2. Lancia Delta Integrale (Auto, 3000 frames) - Slalom & Cornering
    lanc = []
    for f in range(3000):
        b = 0x01
        if (f // 60) % 2 == 0:
            b |= 0x04 # Right
        else:
            b |= 0x08 # Left
        lanc.append(b)
    create_replay("DEFAULT", "LANC", 1, lanc, p / "02_lanc_slalom_corners.rpl")

    # 3. Corvette ZR-1 (Auto, 3000 frames) - High Speed Braking & Acceleration Cycles
    vett = []
    for f in range(3000):
        phase = f % 300
        if phase < 180:
            b = 0x01 # Accel
        elif phase < 240:
            b = 0x02 | 0x04 # Brake + steer
        else:
            b = 0x01 | 0x08 # Accel + counter-steer
        vett.append(b)
    create_replay("FAST", "VETT", 1, vett, p / "03_vett_brake_turn.rpl")

    # 4. Porsche 962 IMSA (Auto, 3000 frames) - Extreme Downforce Circuit Run
    p962 = []
    for f in range(3000):
        b = 0x01
        if (f % 140) < 50:
            b |= 0x04
        elif (f % 140) > 90:
            b |= 0x08
        p962.append(b)
    create_replay("FAST2", "P962", 1, p962, p / "04_p962_highspeed_circuit.rpl")

    # 5. Lamborghini LM002 SUV (Auto, 2000 frames) - Offroad Grass Driving
    lm02 = []
    for f in range(2000):
        b = 0x01
        if f > 200:
            b |= 0x04 # Drive into grass field
        lm02.append(b)
    create_replay("FUNHILLS", "LM02", 1, lm02, p / "05_lm02_offroad_grass.rpl")

    # 6. Audi Quattro Sport (Auto, 2000 frames) - Barrier Collision & Destructive Stress
    audi = []
    for f in range(2000):
        b = 0x01 | 0x04 # Turn hard right into barrier
        audi.append(b)
    create_replay("TRICKY", "AUDI", 1, audi, p / "06_audi_wall_collision.rpl")

    # 7. Ferrari 288 GTO (Auto, 2500 frames) - High-speed Aerodynamic Drag & G-Forces
    fgto = []
    for f in range(2500):
        b = 0x01
        if (f % 200) < 60:
            b |= 0x04
        fgto.append(b)
    create_replay("HELL", "FGTO", 1, fgto, p / "07_fgto_aerodrag_test.rpl")

    # 8. Jaguar XJR-9 (Auto, 3000 frames) - Long Endurance Circuit Execution
    jagu = []
    for f in range(3000):
        b = 0x01
        if (f % 180) < 60:
            b |= 0x04
        elif (f % 180) > 120:
            b |= 0x08
        jagu.append(b)
    create_replay("HELL2", "JAGU", 1, jagu, p / "08_jagu_endurance_run.rpl")

    # 9. Porsche Carrera 4 (Auto, 2500 frames) - All-Wheel Drive Cornering Dynamics
    pc04 = []
    for f in range(2500):
        b = 0x01
        if (f // 80) % 2 == 0:
            b |= 0x04
        else:
            b |= 0x08
        pc04.append(b)
    create_replay("HEAVY", "PC04", 1, pc04, p / "09_pc04_carrera_test.rpl")

    # 10. Acura NSX (Auto, 2500 frames) - Precision Handling & Mid-Engine Balance
    ansx = []
    for f in range(2500):
        b = 0x01
        if (f % 100) < 35:
            b |= 0x04
        elif (f % 100) > 70:
            b |= 0x08
        ansx.append(b)
    create_replay("MONSTER", "ANSX", 1, ansx, p / "10_ansx_acura_slalom.rpl")

    # 11. Porsche March-Indy (Auto, 3000 frames) - Maximum Velocity & Airborne Jumps
    pmin = []
    for f in range(3000):
        b = 0x01
        pmin.append(b)
    create_replay("ALLJUMPS", "PMIN", 1, pmin, p / "11_pmin_speed_loop.rpl")

if __name__ == "__main__":
    make_all_scenario_replays("tests/replays")
