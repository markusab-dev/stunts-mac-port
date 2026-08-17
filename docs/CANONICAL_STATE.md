# Canonical State Specification (`STUNTS_CANONICAL_STATE_V1`)

## 1. Rationale: Why Raw Memory Dumps Must Not Be Diffed Directly

In the original MS-DOS real-mode binary, [`struct GAMESTATE`](file:///Volumes/Extra%20SSD%202TB/Claude%20Code/Antigravity/Stunts%20Mac%20Port/reference/restunts/src/restunts/c/externs.h#L91-L138) occupies 1,120 contiguous bytes. Attempting to directly `memcmp()` or byte-diff a raw DOS struct against a modern native 64-bit ARM64 structure is fundamentally flawed for several reasons:

1. **Alignment & Padding**: 16-bit DOS real mode used 1-byte or 2-byte structure packing (`#pragma pack(1)`). Modern 64-bit ARM64 architectures enforce 4-byte or 8-byte natural alignment for performance.
2. **Pointer Widths & Segment References**: The DOS structure contains 16-bit near pointers and 32-bit `segment:offset` far pointers (e.g. `td14_elem_map_main`, `cvxptr`, `aerorestable`). On ARM64, pointers are 64-bit flat virtual addresses.
3. **Uninitialized Memory & Garbage Padding**: When DOS allocates heap paragraphs or stack frames, uninitialized memory bytes retain stale data from prior interrupts or DOS buffers. These uninitialized bytes vary between emulator runs without altering simulation physics.
4. **Architecture-Specific Fields**: Fields storing scratch registers or DOS DTA buffers have no meaning on native macOS.

**The Solution**: We define an architecture-independent canonical state representation: **`STUNTS_CANONICAL_STATE_V1`**. Every field has an explicit semantic name, exact bit width, defined signedness, and defined byte order.

---

## 2. Specification of Fields

`STUNTS_CANONICAL_STATE_V1` consists of 52 discrete simulation variables extracted at every 50 ms tick:

### 2.1 Frame & Timing Context
| Canonical Field | Type | Signedness | Unit / Range | Source in DOS `GAMESTATE` |
| :--- | :---: | :---: | :--- | :--- |
| `frame_index` | `uint32_t` | Unsigned | $0 \dots N$ | Frame counter index |
| `game_frame` | `uint16_t` | Unsigned | Simulation tick counter | `state.game_frame` |
| `time_seconds` | `int32_t` | Signed | Milliseconds ($50 \times \text{frame}$) | Derived ($\text{frame} \times 50$) |

### 2.2 Player World Position & Orientation
| Canonical Field | Type | Signedness | Unit / Range | Source in DOS `GAMESTATE` |
| :--- | :---: | :---: | :--- | :--- |
| `pos_x` | `int32_t` | Signed | World units ($1\text{ tile} = 1024$) | `playerstate.car_posWorld1.lx` |
| `pos_y` | `int32_t` | Signed | World elevation ($1\text{ tile} = 1024$) | `playerstate.car_posWorld1.ly` |
| `pos_z` | `int32_t` | Signed | World units ($1\text{ tile} = 1024$) | `playerstate.car_posWorld1.lz` |
| `rot_pitch_x` | `int16_t` | Signed | 1024-deg circle ($-512 \dots +511$) | `playerstate.car_rotate.x` |
| `rot_yaw_y` | `int16_t` | Signed | 1024-deg circle ($-512 \dots +511$) | `playerstate.car_rotate.y` |
| `rot_roll_z` | `int16_t` | Signed | 1024-deg circle ($-512 \dots +511$) | `playerstate.car_rotate.z` |

### 2.3 Player Velocity & Drivetrain
| Canonical Field | Type | Signedness | Unit / Range | Source in DOS `GAMESTATE` |
| :--- | :---: | :---: | :--- | :--- |
| `speed_coupled` | `uint16_t` | Unsigned | $256 \times \text{mph}$ (rev-coupled) | `playerstate.car_speed` |
| `speed_actual` | `uint16_t` | Unsigned | $256 \times \text{mph}$ (inertial speed)| `playerstate.car_speed2` |
| `speed_last` | `uint16_t` | Unsigned | Previous tick speed | `playerstate.car_lastspeed` |
| `speed_diff` | `int16_t` | Signed | Acceleration / Deceleration delta | `playerstate.car_speeddiff` |
| `engine_rpm` | `int16_t` | Signed | RPM ($0 \dots 12000$) | `playerstate.car_currpm` |
| `current_gear` | `uint8_t` | Unsigned | $0 = \text{Rev}, 1 \dots 6 = \text{Gears}$ | `playerstate.car_current_gear` |
| `gear_ratio` | `uint16_t` | Unsigned | Fixed-point transmission ratio | `playerstate.car_gearratio` |
| `is_braking` | `uint8_t` | Unsigned | Boolean ($0 = \text{off}, 1 = \text{on}$) | `playerstate.car_is_braking` |
| `is_accelerating` | `uint8_t` | Unsigned | Boolean ($0 = \text{off}, 1 = \text{on}$) | `playerstate.car_is_accelerating` |
| `steering_angle` | `int16_t` | Signed | Front wheel angle ($-128 \dots +127$) | `playerstate.car_steeringAngle` |

### 2.4 Suspension, Wheel Surface & Ground Contact
| Canonical Field | Type | Signedness | Unit / Range | Source in DOS `GAMESTATE` |
| :--- | :---: | :---: | :--- | :--- |
| `whl_surf_fl` | `uint8_t` | Unsigned | Surface type ID ($0=\text{tarmac}, 1=\text{dirt}, 2=\text{ice}, 3=\text{grass}$) | `playerstate.car_surfaceWhl[0]` |
| `whl_surf_fr` | `uint8_t` | Unsigned | Surface type ID | `playerstate.car_surfaceWhl[1]` |
| `whl_surf_rl` | `uint8_t` | Unsigned | Surface type ID | `playerstate.car_surfaceWhl[2]` |
| `whl_surf_rr` | `uint8_t` | Unsigned | Surface type ID | `playerstate.car_surfaceWhl[3]` |
| `wheels_on_ground` | `uint8_t` | Unsigned | Count ($0 = \text{in flight}, 1 \dots 4$) | `playerstate.car_sumSurfAllWheels` |
| `wheels_front_contact`| `uint8_t` | Unsigned | Front wheel contact sum | `playerstate.car_sumSurfFrontWheels` |
| `wheels_rear_contact` | `uint8_t` | Unsigned | Rear wheel contact sum | `playerstate.car_sumSurfRearWheels` |
| `whl_force_fl` | `int16_t` | Signed | Normal / contact force (Wheel 0) | `playerstate.car_rc1[0]` |
| `whl_force_fr` | `int16_t` | Signed | Normal / contact force (Wheel 1) | `playerstate.car_rc1[1]` |
| `whl_force_rl` | `int16_t` | Signed | Normal / contact force (Wheel 2) | `playerstate.car_rc1[2]` |
| `whl_force_rr` | `int16_t` | Signed | Normal / contact force (Wheel 3) | `playerstate.car_rc1[3]` |

### 2.5 Aerodynamics, Flight Dynamics & Collision Flags
| Canonical Field | Type | Signedness | Unit / Range | Source in DOS `GAMESTATE` |
| :--- | :---: | :---: | :--- | :--- |
| `pseudo_gravity` | `int16_t` | Signed | Vertical ballistic accumulator | `playerstate.car_pseudoGravity` |
| `sliding_flag` | `uint8_t` | Unsigned | Lateral drift / skid state flag | `playerstate.car_slidingFlag` |
| `demanded_grip` | `int16_t` | Signed | Lateral cornering demand | `playerstate.car_demandedGrip` |
| `surface_grip_sum` | `int16_t` | Signed | Combined tire friction coefficient | `playerstate.car_surfacegrip_sum` |
| `crash_flag` | `uint8_t` | Unsigned | Crash state ($1 = \text{crashed}$) | `playerstate.car_crashBmpFlag` |
| `engine_limiter_timer`| `uint8_t` | Unsigned | Over-rev limiter cooldown tick | `playerstate.car_engineLimiterTimer` |

### 2.6 Race Metrics & Deterministic PRNG
| Canonical Field | Type | Signedness | Unit / Range | Source in DOS `GAMESTATE` |
| :--- | :---: | :---: | :--- | :--- |
| `distance_traveled` | `int32_t` | Signed | Integrated distance accumulator | `game_travDist` |
| `penalty_counter` | `int16_t` | Signed | Off-track penalty time ticks | `game_penalty` |
| `impact_speed` | `uint16_t` | Unsigned | Kinetic impact energy absorbed | `game_impactSpeed` |
| `top_speed` | `uint16_t` | Unsigned | Maximum speed achieved ($256 \times \text{mph}$) | `game_topSpeed` |
| `jump_count` | `int16_t` | Signed | Total jumps taken | `game_jumpCount` |
| `kevin_seed` | `uint8_t[6]` | Unsigned | 6-byte linear congruential PRNG seed | `state.kevinseed` |

---

## 3. Serialization Formats

### 3.1 JSON Lines Format (`.jsonl`)
Used for human inspection, debugging, and line-by-line git diffs. Each simulation frame is a single-line JSON object:

```json
{"schema":"STUNTS_CANONICAL_STATE_V1","frame":140,"time_ms":7000,"pos":[15360,1024,48120],"rot":[0,512,0],"speed_coupled":28416,"speed_actual":28416,"rpm":4200,"gear":4,"steering":0,"brake":0,"accel":1,"sliding":0,"crashed":0,"wheels_on_ground":4,"pseudo_gravity":0,"distance":3978240}
```

### 3.2 Compact Binary Format (`.cs1`)
Used for ultra-fast batch regression testing across thousands of replay frames.
* **Header (16 Bytes)**:
  * `char magic[4]`: `"SCS1"`
  * `uint32_t version`: `1`
  * `uint32_t frame_count`: Total recorded frames ($N$)
  * `uint32_t frame_size_bytes`: Exactly `128` bytes per frame
* **Frame Record (128 Bytes fixed-size Little-Endian)**:
  Fixed-width serialization of all 52 fields with zero alignment padding.
