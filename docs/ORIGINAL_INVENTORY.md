# Original Stunts Game Inventory

## 1. Archive Provenance & Hash Verification

* **Canonical Archive**: `Original/Stunts_DOS_EN.zip` (also referenced as `original/STUNTS.zip` on case-insensitive APFS)
* **Archive Size**: `1,135,140` bytes
* **Archive SHA-256**: `8c016289cf1e03525f0e6be93d73daf55f43bb5f546789f73fa3136660ef0886`
* **Extraction Target**: `extracted/stunts/stunts/` (immutable working reference)
* **Total File Count**: `210` files

### Version Identification
* **Game Title**: Stunts (Brøderbund Software release)
* **Version**: `1.1` (English / US Retail)
* **Release Date**: March 13, 1991 (all core game binaries and resources timestamped `1991-03-13 00:00:00`)
* **Executable Platform**: 16-bit MS-DOS Real Mode (8086/80286/80386 compatible)
* **Compiler**: Microsoft C 5.10 / Microsoft MASM / Distinctive Software Inc. internal toolchain

---

## 2. Category Summary

| Category | File Count | Total Size (Bytes) | Description |
| :--- | :---: | :---: | :--- |
| **Executable / Launcher** | 5 | 47,271 | DOS loaders, main launcher, setup utility |
| **Graphics Driver & Overlay** | 15 | 321,809 | Dynamic display drivers (`.COD`, `.DIF`, `.HDR`) and common exe core |
| **Audio Driver & Resources** | 14 | 26,050 | Audio drivers (`.DRV`), voice/SFX banks (`.VCE`, `.SFX`), patch banks (`.PLB`), MIDI/music (`.KMS`) |
| **Resource Containers** | 57 | 817,543 | DSI packed archives (`.PRE`, `.PVS`, `.PES`, `.RES`) containing UI, textures, collision |
| **3D Models & Geometry** | 14 | 74,534 | 3D object / car polygon geometry (`.P3S`) |
| **Track Definitions** | 41 | 73,882 | 30x30 track layout & terrain elevation files (`.TRK`) |
| **Highscore Records** | 41 | 14,924 | Track records and best lap times (`.HIG`) |
| **Replay Recordings** | 1 | 12,474 | Default game replay recording (`DEFAULT.RPL`) |
| **Bitmap Fonts** | 3 | 2,783 | 2D raster fonts (`.FNT`) |
| **Configuration** | 1 | 83 | Hardware setup config (`SETUP.DAT`) |
| **Metadata & Icons** | 4 | 19,720 | Helper icons, documentation, splash image |

---

## 3. Core Executables & Dynamic Overlays

### Executables
| Filename | Size (Bytes) | SHA-256 | Purpose |
| :--- | :---: | :--- | :--- |
| `STUNTS.COM` | 758 | `3c8742b66236b2803b9ad658897003010b9fa0eb8fa49c394c8b66da5bfeb7aa` | Primary DOS launcher. Reads `SETUP.DAT` and spawns `LOAD.EXE` with appropriate video/sound flags. |
| `ST.COM` | 208 | `71b2d713c706d3d922bb525f0e1d5ba263fcae1a6c117d745cba2a731efc794a` | Short launcher redirecting to `STUNTS.COM`. |
| `LOAD.EXE` | 20,829 | `d4e41054ee42cf9a5a3a71b2024b423cbf988a804ec6015509b5523fa595e7b2` | Dynamic overlay loader & relocator. Loads `EGA.CMN`, applies `.DIF`, binds `.COD`, initializes heap. |
| `SETUP.EXE` | 15,065 | `60281cfdfbc576f30e9d93bb85f36e87f5898d2aa2db59d6e876ae9a9fbb00d0` | Interactive DOS hardware configuration utility (video/sound card selection). |
| `Start.exe` | 5,411 | `15e454f738a9be1c54b6d475ef7a29e160a0f8bf90b91d291e1d0879e64e5c8e` | Third-party / bundled DOS menu wrapper. |

### Dynamic Graphics Overlays (`.COD`, `.DIF`, `.HDR`, `.CMN`)
| Driver Set | Files Included | Total Size | Video Standard Supported |
| :--- | :--- | :---: | :--- |
| **MCGA / VGA** | `MCGA.HDR` (30 B), `MCGA.DIF` (17,958 B), `MCGA.COD` (48,899 B) | 66,887 B | 320x200 256 colors (Primary target for port) |
| **EGA** | `EGA.HDR` (30 B), `EGA.CMN` (127,337 B), `EGA.COD` (68,823 B) | 196,190 B | 320x200 16 colors (Contains the base common code `EGA.CMN`) |
| **CGA** | `CGA.HDR` (30 B), `CGA.DIF` (18,030 B), `CGA.COD` (56,988 B) | 75,048 B | 320x200 4 colors |
| **Tandy** | `TDY.HDR` (30 B), `TDY.DIF` (18,230 B), `TDY.COD` (55,169 B) | 73,429 B | 320x200 16 colors (Tandy / PCjr) |
| **Hercules** | `HERC.HDR` (30 B), `HERC.DIF` (18,174 B), `HERC.COD` (56,229 B) | 74,433 B | 720x348 2 colors |

---

## 4. Audio Subsystems & Drivers

| File | Size (Bytes) | Category | Description |
| :--- | :---: | :--- | :--- |
| `MT15.DRV` | 1,750 | Audio Driver | Roland MT-32 MIDI driver (Command line flag `/smt`) |
| `MT32.PLB` | 1,271 | Patch Bank | Roland MT-32 SysEx patch bank |
| `MTENG1.VCE` | 976 | Voice Data | Roland MT-32 car engine synthesizer parameters |
| `MTSKIDMS.VCE` | 1,184 | Voice Data | Roland MT-32 tire skid and crash sound parameters |
| `PC15.DRV` | 2,227 | Audio Driver | IBM PC Speaker 1-bit PWM driver (Flag `/spc`) |
| `PCENG1.VCE` | 1,049 | Voice Data | PC Speaker engine tone tables |
| `PCSKIDMS.VCE` | 1,232 | Voice Data | PC Speaker sound effects |
| `TD15.DRV` | 2,993 | Audio Driver | Tandy 3-voice sound driver (Flag `/std`) |
| `TDENG1.VCE` | 912 | Voice Data | Tandy engine sound data |
| `TDSKIDMS.VCE` | 2,528 | Voice Data | Tandy skid/crash sound data |
| `GEENG.SFX` | 2,074 | Sound Bank | Generic engine audio samples |
| `SKIDTITL.KMS` | 2,590 | Music Sequence | Title screen music sequence |
| `SKIDSLCT.KMS` | 8,612 | Music Sequence | Car/Track selection menu music sequence |
| `SKIDVICT.KMS` | 4,221 | Music Sequence | Victory / Podium celebration music |
| `SKIDOVER.KMS` | 4,303 | Music Sequence | Game Over / Crash screen music |

---

## 5. Vehicle Assets (11 Original Vehicles)

Each vehicle consists of three companion files:
1. `ST<ID>.P3S`: 3D car mesh geometry, materials, and physics parameter block (`struct SIMD`)
2. `STDA<ID>.PVS`: Dashboard artwork, gauges, steering wheel sprites, cockpit graphics
3. `STDB<ID>.PVS`: Secondary dashboard/cockpit overlays & instrumentation

| Vehicle Name | Car ID | 3D Geometry (`.P3S`) | Dashboard A (`.PVS`) | Dashboard B (`.PVS`) |
| :--- | :---: | :---: | :---: | :---: |
| **Acura NSX** | `ANSX` | `STANSX.P3S` (5,140 B) | `STDAANSX.PVS` (14,776 B) | `STDBANSX.PVS` (772 B) |
| **Audi Quattro Sport** | `AUDI` | `STAUDI.P3S` (7,026 B) | `STDAAUDI.PVS` (15,167 B) | `STDBAUDI.PVS` (841 B) |
| **Lamborghini Countach 25th**| `COUN` | `STCOUN.P3S` (5,338 B) | `STDACOUN.PVS` (12,896 B) | `STDBCOUN.PVS` (951 B) |
| **Ferrari 288 GTO** | `FGTO` | `STFGTO.P3S` (4,183 B) | `STDAFGTO.PVS` (15,364 B) | `STDBFGTO.PVS` (955 B) |
| **Jaguar XJR-9 IMSA** | `JAGU` | `STJAGU.P3S` (5,544 B) | `STDAJAGU.PVS` (16,203 B) | `STDBJAGU.PVS` (743 B) |
| **Lancia Delta HF Integrale**| `LANC` | `STLANC.P3S` (4,004 B) | `STDALANC.PVS` (19,460 B) | `STDBLANC.PVS` (1,069 B) |
| **Lamborghini LM002 SUV** | `LM02` | `STLM02.P3S` (5,853 B) | `STDALM02.PVS` (17,252 B) | `STDBLM02.PVS` (912 B) |
| **Porsche 962 IMSA** | `P962` | `STP962.P3S` (5,838 B) | `STDAP962.PVS` (13,659 B) | `STDBP962.PVS` (720 B) |
| **Porsche 911 Carrera 4** | `PC04` | `STPC04.P3S` (4,097 B) | `STDAPC04.PVS` (15,727 B) | `STDBPC04.PVS` (933 B) |
| **Porsche March-Indy** | `PMIN` | `STPMIN.P3S` (2,697 B) | `STDAPMIN.PVS` (11,069 B) | `STDBPMIN.PVS` (796 B) |
| **Chevrolet Corvette ZR1** | `VETT` | `STVETT.P3S` (6,283 B) | `STDAVETT.PVS` (15,240 B) | `STDBVETT.PVS` (1,097 B) |

---

## 6. Opponents & AI Personalities

| Opponent ID | Name / Persona | Resource Archive | Win Animation | Lose Animation |
| :---: | :--- | :--- | :--- | :--- |
| `OPP1` | **Squealin' Bernie Rubber** | `OPP1.PRE` (1,254 B) | `OPP1WIN.PVS` (10,143 B) | `OPP1LOSE.PVS` (10,733 B) |
| `OPP2` | **Herr Otto von Error** | `OPP2.PRE` (1,321 B) | `OPP2WIN.PVS` (20,830 B) | `OPP2LOSE.PVS` (18,883 B) |
| `OPP3` | **Smokey Bacon** | `OPP3.PRE` (993 B) | `OPP3WIN.PVS` (21,125 B) | `OPP3LOSE.PVS` (11,578 B) |
| `OPP4` | **"Fast" Helen Wheels** | `OPP4.PRE` (1,372 B) | `OPP4WIN.PVS` (25,855 B) | `OPP4LOSE.PVS` (26,093 B) |
| `OPP5` | **Jean-Paul Jean** | `OPP5.PRE` (1,256 B) | `OPP5WIN.PVS` (33,740 B) | `OPP5LOSE.PVS` (22,016 B) |
| `OPP6` | **Skid Vicious** (Champion) | `OPP6.PRE` (1,297 B) | `OPP6WIN.PVS` (24,653 B) | `OPP6LOSE.PVS` (28,687 B) |

---

## 7. Tracks and High Scores

The archive includes **41 built-in and community tracks** (each paired with a 1,802-byte `.TRK` layout file and a 364-byte `.HIG` high-score file):
- Canonical Tracks: `DEFAULT`, `CTRACK01` to `CTRACK11`, `TRY_IT`, `THE_EDGE`, `TRICKY`, `WHAT_THE`, `FUNHILLS`, `FUNISLES`, `HEAVY`, `JUMP`, `MEGAJUMP`, `MONSTER`, `KING`, `HELL`, etc.
- Canonical Replay: `DEFAULT.RPL` (12,474 bytes, 600+ recorded physics frames).

---

## 8. Resource Archives & Global Data

| File | Size (Bytes) | Description |
| :--- | :---: | :--- |
| `GAME.PRE` | 15,434 | Primary game resource archive containing physics plane tables (`plan`), collision wall tables (`wall`), windshield crack textures, and HUD elements. |
| `GAME1.P3S` | 17,556 | 3D scenery assets: bridges, loops, corkscrews, tunnels, highway ramps, start/finish gantry. |
| `GAME2.P3S` | 21,852 | 3D trackside objects: buildings, tennis courts, barns, windmills, signs, trees. |
| `MAIN.RES` | 1,504 | Central resource lookup directory and index table. |
| `MISC.PRE` | 1,814 | Miscellaneous sprites and UI markers. |
| `SDTITL.PVS` | 9,984 | Title screen graphics and logo bitmap. |
| `SDMAIN.PVS` | 1,176 | Main menu background. |
| `SDCSEL.PVS` | 12,362 | Car selection screen artwork. |
| `SDOSEL.PVS` | 39,363 | Opponent selection screen artwork. |
| `SDMSEL.PVS` | 15,577 | Track/Mode selection screen artwork. |
| `SDGAME.PVS` | 12,286 | In-game HUD, rear-view mirror, timer graphics. |
| `SDGAME2.PVS` | 1,252 | In-game penalty arrows and flags. |
| `SDTEDIT.PES` | 16,076 | Track editor UI icons and toolbar buttons. |
| `SDCRED.PES` | 3,608 | Credits screen bitmaps. |
| `FONTDEF.FNT` | 670 | Default proportional bitmap font. |
| `FONTLED.FNT` | 670 | 7-segment digital speedometer font. |
| `FONTN.FNT` | 1,443 | Monospace numeric and UI font. |
