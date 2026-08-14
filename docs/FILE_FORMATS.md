# Stunts Resource & File Formats Specification

This document details all binary file formats used in *Stunts* / *4D Sports Driving*, specifying structure offsets, field data types, semantics, and our verified confidence level.

---

## 1. Summary of Formats & Confidence Levels

| Extension | Format Name | Typical Size | Confidence Level | Primary Reference / Parser |
| :---: | :--- | :---: | :---: | :--- |
| **`.TRK`** | Track Layout & Terrain Elevation | 1,802 B | **HIGH (100%)** | `format.html`, Restunts `fileio.c`, binary tests |
| **`.HIG`** | Track Highscores & Records | 364 B | **HIGH (100%)** | Stunts Wiki, `externs.h` |
| **`.RPL`** | Race Replay Recording | Variable (3–20 KB) | **HIGH (95%)** | `repldump.c`, `fileio.c` |
| **`.PRE`** | Packed Resource Archive | 1–25 KB | **HIGH (95%)** | `fileio.c`, `game.res.txt` |
| **`.PVS`** | Packed Video / UI Bitmaps | 1–40 KB | **HIGH (90%)** | `shape2d.c`, `fileio.c` |
| **`.P3S`** | 3D Mesh Geometry & Physics Block | 2–8 KB | **HIGH (95%)** | `shape3d.c`, `SIMD` in `externs.h` |
| **`SIMD`** | Car Physics Parameters | 320 B | **HIGH (98%)** | `externs.h`, `statecar.c`, `stateply.c` |
| **`plan`** | Drivable Surface Collision Planes | 536 × 34 B | **HIGH (95%)** | `game.res.txt`, `math.c`, `stateply.c` |
| **`wall`** | Vertical Barrier Collision Planes | Array of 16 B | **MEDIUM (85%)** | `game.res.txt`, `stateply.c` |
| **`.FNT`** | 2D Bitmap Fonts | 0.6–1.5 KB | **HIGH (100%)** | `shape2d.c`, `fileio.c` |
| **`.KMS`** | Music Sequence File | 2–9 KB | **MEDIUM (75%)** | Voyetra audio driver disassemblies |
| **`.DRV`** | Audio Synthesizer Drivers | 1.5–3 KB | **MEDIUM (70%)** | `seg008.asm`, `seg009.asm` |

---

## 2. Track Format (`.TRK` — 1,802 Bytes)

A `.TRK` file defines both the track element grid and the terrain heightmap for a $30 \times 30$ tile circuit.

```
+---------------------------+---------------------------+-----------+
|   Track Elements (0-899)  | Terrain Elevation (900-1799)| Checksum  |
|      (30 x 30 Bytes)      |      (30 x 30 Bytes)      | (2 Bytes) |
+---------------------------+---------------------------+-----------+
```

### 2.1 Track Elements Section (Bytes `0` to `899`)
* Array of 900 unsigned 8-bit bytes representing a row-major $30 \times 30$ grid (row 0 is North, row 29 is South; col 0 is West, col 29 is East).
* **Common Track Element IDs**:
  * `0x00`: Plain empty terrain
  * `0x01` – `0x06`: Straight roads (asphalt, dirt, ice)
  * `0x07` – `0x1E`: Curves, chicanes, intersections, 90-degree corners
  * `0x27` – `0x2A`: Start / Finish line gantry (orientations: N, E, S, W)
  * `0x39` – `0x40`: Slalom barriers and split tracks
  * `0x41` – `0x48`: Loop-the-loop segments (entry, apex, exit)
  * `0x49` – `0x50`: Corkscrews (left / right twist)
  * `0x51` – `0x58`: Elevated highway bridges and ramps
  * `0x60` – `0x7F`: Scenery items (buildings, windmills, barns, trees, tennis courts)

### 2.2 Terrain Elevation Section (Bytes `900` to `1799`)
* Array of 900 unsigned 8-bit bytes representing height levels ($0$ to $15$) at each grid coordinate:
  * Bits `0..3`: Tile base elevation ($0 = \text{ground level}, 1 = \text{plateau level 1}, \dots$)
  * Bits `4..7`: Terrain surface slope types (flat, gentle slope N/S/E/W, steep hill, water/lake)

### 2.3 Tail Checksum (Bytes `1800` to `1801`)
* 16-bit little-endian integrity check value calculated across the 1800 data bytes.

---

## 3. Highscore Format (`.HIG` — 364 Bytes)

Stores the top 6 fastest laps recorded for the corresponding `.TRK` file.

* **Header (4 bytes)**: Magic identifier / track hash.
* **Records (6 entries $\times$ 60 bytes = 360 bytes)**:
  * `char driver_name[30]`: Null-padded driver name string.
  * `char car_id[4]`: 4-character vehicle identifier (e.g. `"VETT"`, `"P962"`).
  * `unsigned short lap_time`: Lap duration in $\frac{1}{20}\text{th}$ second simulation frames.
  * `unsigned short penalty_time`: Off-track / shortcut penalty time in frames.
  * `char date_string[22]`: ASCII timestamp of record creation.

---

## 4. Replay Recording Format (`.RPL`)

* **Header Structure (`struct GAMEINFO`)**:
  * `char game_playercarid[4]`: Player car code (e.g. `"AUDI"`)
  * `char game_playermaterial`: Player car paint scheme / color index
  * `char game_playertransmission`: Manual (`0`) or Automatic (`1`)
  * `char game_opponenttype`: Opponent index (`0` to `6`, `0 = None`)
  * `char game_opponentcarid[4]`: Opponent car code
  * `char game_opponentmaterial`: Opponent car color index
  * `char game_opponenttransmission`: Opponent transmission
  * `char game_trackname[9]`: Track filename (without `.TRK` extension)
  * `unsigned short game_framespersec`: Simulation tick rate (always `20`)
  * `unsigned short game_recordedframes`: Total recorded frame count ($N$)
* **Frame Input Buffer**:
  * Sequence of $N$ 2-byte bitfields encoding player steering (-128 to +127), throttle (0/1), brake (0/1), handbrake, and gear shift inputs.

---

## 5. DSI Packed Resource Formats (`.PRE`, `.PVS`, `.P3S`, `.PES`)

Distinctive Software utilized a proprietary multi-stage compression scheme.

### 5.1 Compression Header (`struct compr_header`)
```c
struct compr_header {
    unsigned char type;          // Compression algorithm flag (0x01 = RLE, 0x02 = VLE/Huffman)
    unsigned short sizel;        // Decompressed size (low 16 bits)
    unsigned char sizeh;         // Decompressed size (high 8 bits, allowing up to 16 MB)
};
```

### 5.2 Compression Pipeline
1. **Pass 1 — Run-Length Encoding (RLE)**:
   - Encodes repeated single-byte runs using escape markers defined in `struct compr_rle_header`.
   - Sequential multi-byte run matching for repeating coordinate patterns.
2. **Pass 2 — Variable-Length Encoding (VLE / Bit-Tree)**:
   - Huffman-like bitstream using adaptive prefix tables (`RS_VLE_ESC_WIDTH = 0x40`).
   - Bitwise stream unpacker reconstructs literal bytes and dictionary matches into destination buffer.

---

## 6. Collision & Track Surface Geometry (`plan` and `wall`)

Stored within `GAME.PRE` (unpacked into `GAME.RES`), these structures define the physical collision geometry for all 3D track elements.

### 6.1 Drivable Surface Planes (`plan` — 536 Entries $\times$ 34 Bytes)
```c
struct PLANE {
    short angleYZ;               // Pitch inclination angle (0 to 1023)
    short angleXY;               // Bank / roll inclination angle (0 to 1023)
    struct VECTOR origin;        // Plane reference anchor point (x, y, z in tile coords)
    struct VECTOR normal;        // Normal vector perpendicular to surface (nx, ny, nz)
    struct VECTOR rotationMatrix[3]; // 3x3 orientation matrix for transforming vehicle local space
};
```
* **Physics Function**: When a wheel's $(X, Z)$ coordinate falls within the tile bounding polygon, its world height $Y$ is solved via plane equation:
  $$\vec{N} \cdot (\vec{P} - \vec{O}) = 0 \implies Y = O_y - \frac{N_x (P_x - O_x) + N_z (P_z - O_z)}{N_y}$$

### 6.2 Vertical Barriers & Obstacles (`wall` — 16 Bytes per Entry)
* Defines 2D bounding line segments $(X_1, Z_1) \to (X_2, Z_2)$ with minimum and maximum height thresholds ($Y_{\min}, Y_{\max}$).
* When the vehicle chassis collision box penetrates a wall segment, an elastic collision impulse is reflected along the wall normal, and deceleration damage is accumulated into `game_impactSpeed`.
