# Behavioral Verification & Oracle Testing Strategy

## 1. Core Principle: Determinism & Oracle Testing

A retro-port of *Stunts* cannot be declared faithful based on subjective visual or tactile feedback ("it feels similar"). 

The original *Stunts* physics engine is **100% deterministic**:
1. It operates at a fixed **20 Hz discrete time step** ($\Delta t = 50\text{ ms}$).
2. It uses integer and fixed-point math (zero non-deterministic IEEE floating-point rounding discrepancies across architectures).
3. Random events (such as AI decisions or minor collision jitter) are driven by an explicit deterministic linear congruential generator (`kevinrandom`).
4. Replay files (`.RPL`) record only the player's discrete steering/pedal inputs per frame.

Because of this determinism, the original DOS executable serves as a **mathematical oracle**: feeding identical inputs must produce identical physics state trajectories on both DOS and macOS Apple Silicon.

```
+------------------------------------+
|  Input Replay Scenario (.RPL file) |
+------------------------------------+
         |                    |
         v                    v
+------------------+  +------------------+
| DOS Reference    |  | Native macOS Port|
| Oracle Runner    |  | Test Harness     |
| (repldump.exe)   |  | (stunts_verify)  |
+------------------+  +------------------+
         |                    |
         v                    v
+------------------+  +------------------+
| Reference State  |  | Target State     |
| Dump (.BIN)      |  | Dump (.BNI)      |
+------------------+  +------------------+
         \                    /
          \                  /
           v                v
     +---------------------------+
     | tools/compare_states.py   |
     | Frame-by-Frame Diffing    |
     +---------------------------+
                   |
     +-------------+-------------+
     |                           |
[SUCCESS: 100% Match]   [DIVERGENCE DETECTED]
                        - First divergent frame
                        - Exact byte & field offsets
                        - Position / speed deltas
```

---

## 2. The 1,120-Byte `GAMESTATE` Verification Payload

At every 50 ms tick, `repldump` dumps the entire `struct GAMESTATE` (1,120 bytes). Our test harness verifies all critical physical parameters:

| Field Group | Struct Member | Data Type | Physical Meaning |
| :--- | :--- | :---: | :--- |
| **World Position** | `playerstate.car_posWorld1` | `int32_t[3]` | $(X, Y, Z)$ 32-bit world coordinates |
| **Interpolated Pos**| `playerstate.car_posWorld2` | `int32_t[3]` | Sub-frame visual position vector |
| **Orientation** | `playerstate.car_rotate` | `int16_t[3]` | Angular pitch, yaw, roll in 1024-deg units |
| **Tire Speed** | `playerstate.car_speed` | `uint16_t` | Rev-coupled wheel speed ($256 \times \text{mph}$) |
| **Actual Speed** | `playerstate.car_speed2` | `uint16_t` | True inertial speed through space |
| **Engine RPM** | `playerstate.car_currpm` | `int16_t` | Engine revolutions per minute |
| **Drivetrain** | `playerstate.car_current_gear` | `uint8_t` | Active gear ($1 \dots 6$, or $0 = \text{Reverse}$) |
| **Pedals & Inputs**| `playerstate.car_is_braking` | `uint8_t` | Brake pedal active flag |
| | `playerstate.car_is_accelerating`| `uint8_t` | Throttle pedal active flag |
| | `playerstate.car_steeringAngle` | `int16_t` | Active front wheel steering angle |
| **Wheel Surfaces** | `playerstate.car_surfaceWhl[4]` | `uint8_t[4]` | Material under each wheel (asphalt, dirt, ice, grass) |
| **Airborne State** | `playerstate.car_sumSurfAllWheels`| `uint8_t` | Ground contact counter ($0 = \text{in flight / jumping}$) |
| **Pseudo-Gravity** | `playerstate.car_pseudoGravity` | `int16_t` | Vertical ballistic acceleration accumulator |
| **Lateral Drift** | `playerstate.car_slidingFlag` | `uint8_t` | Tire slip / drift state machine flag |
| **Crash & Damage** | `playerstate.car_crashBmpFlag` | `uint8_t` | Terminal crash trigger flag |
| **Impact Energy** | `game_impactSpeed` | `uint16_t` | Kinetic energy absorbed during latest collision |
| **Penalties** | `game_penalty` | `int16_t` | Off-track penalty time counter |
| **Opponent State** | `opponentstate` | `struct CARSTATE` | Complete duplicate state for AI vehicle |

---

## 3. The Automated Test Loop

### 3.1 Step 1: Generate Reference Oracle Dumps
Under `dosbox-staging` or `dosbox-x`, run `repldump.exe` against our test replay suite:
```bash
bash tools/run_dos_stunts.sh REPLDUMP.EXE DEFAULT.RPL
# Produces DEFAULT.BIN containing reference frame trajectory
```

### 3.2 Step 2: Run Native Headless Replay Harness
Execute the native Apple Silicon binary in headless replay verification mode:
```bash
./bin/stunts_verify --replay Original/DEFAULT.RPL --output DEFAULT.BNI
```

### 3.3 Step 3: Automated State Diffing
Run the diagnostic comparator:
```bash
python3 tools/compare_states.py DEFAULT.BIN DEFAULT.BNI
```

**Diagnostic Output Example**:
```text
Comparing State Dumps:
  Reference (Original DOS): DEFAULT.BIN
  Target (Native Port):     DEFAULT.BNI
======================================================================
Reference frame count: 624 (loaded 624)
Target frame count:    624 (loaded 624)

[DIVERGENCE DETECTED] at Frame 142 (Simulation time: 7.10s)
Total differing bytes in 1120-byte state: 8
First 10 differing byte offsets: [334, 335, 336, 337, 346, 347, 392, 393]

--- Player State Comparison ---
Field                | Original (Ref)       | Native Port          | Delta     
---------------------------------------------------------------------------
Position X           | 15360                | 15360                | 0         
Position Y           | 1024                 | 1024                 | 0         
Position Z           | 48120                | 48118                | -2        *
Rotation Y (Yaw)     | 512                  | 510                  | -2        *
Speed2 (Actual)      | 28416                | 28416                | 0         
Engine RPM           | 4200                 | 4200                 | 0         
```

---

## 4. Test Suite Strategy & Scenario Coverage

To guarantee complete physics fidelity across all driving regimes, the verification suite will include:
1. **`TEST_ACCEL_BRAKE.RPL`**: Flat tarmac straight-line maximum acceleration and threshold braking.
2. **`TEST_GEAR_SHIFTS.RPL`**: Manual transmission shifting up and down at redline and idle.
3. **`TEST_CORNER_GRIP.RPL`**: High-speed slalom and steady-state skidpad cornering up to lateral traction limit.
4. **`TEST_SURFACES.RPL`**: Transitions across asphalt $\to$ dirt road $\to$ ice sheet $\to$ off-track grass.
5. **`TEST_JUMP_LANDING.RPL`**: High-speed ramp jump, ballistic flight, and suspension landing compression.
6. **`TEST_LOOP_CORKSCREW.RPL`**: 3D track elements with extreme pitch and roll orientations (loops, corkscrews).
7. **`TEST_CRASH_IMPULSE.RPL`**: High-speed head-on and glancing wall collisions triggering crash routines.
8. **`TEST_AI_RACE.RPL`**: Full 2-car grid race with opponent AI interaction and overtaking.
