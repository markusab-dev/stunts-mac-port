# Stunts — native macOS port

A source port of *Stunts* / *4D Sports Driving* (Distinctive Software, 1990)
to native Apple Silicon. The simulation and renderer are transcribed from the
DOS original instruction by instruction, and checked against it frame by
frame.

**You need your own copy of the game.** No game data is included here — point
the port at your own files with `--data`.

## How faithful

The physics is compared against the real DOS build running under DOSBox, one
1120-byte game state per frame:

| Track | Fields identical | Byte agreement |
|---|---|---|
| TUBETEST | 16/16 | 100.00% |
| HILLTEST | 16/16 | 100.00% |
| PIPEROLL | 16/16 | 100.00% |
| T_HELL2 | 16/16 | 100.00% |
| T_HELL4 | 16/16 | 100.00% |
| PIPEFLIP | 16/16 | 100.00% |

Every frame of all six is byte-identical to the DOS build outside
`opponentstate`, which these recordings never use.

Plus: all 39 shipped tracks start, all 11 cars and all 6 opponents run, no
unpainted pixels across 12 replays, and rewinding restores the game state
byte-identically.

```bash
bash tools/verify.sh
```

That is the standing bar. Every check in it exists because a bug got past the
previous set.

## Building

Needs clang and SDL2.

```bash
bash tools/build_native.sh
./bin/stunts_native --data /path/to/your/stunts
```

Useful flags: `--track`, `--car`, `--opponent`, `--replay`, `--intro`,
`--headless N`, `--nosound`.

## What works

Intro and credits, menus, track and car pickers with the 3D preview and
showroom, all 39 tracks, all 11 cars, all 6 opponents, racing, crashes,
results and records, replay recording and playback, pause and rewind, engine
and skid sound, and OPL music through a vendored Nuked-OPL3 core.

Not done yet: the on-screen replay bar's buttons, the track editor, and
joystick support. See `docs/HANDOVER_PHASE9_11.md`.

## Layout

    src/sim_faithful/     the simulation, transcribed from the original
    src/render_faithful/  the renderer, likewise
    src/asset/            archive, track and replay loading
    src/vendored/opl/     Nuked-OPL3, unmodified
    tools/verify.sh       the whole verification bar in one command
    docs/                 the running technical record

`docs/FAITHFUL_RENDERER.md` is the detailed log — what each piece does, what
the data turned out to say, and every bug worth remembering.

## Credit where it is due

This port would not exist without two projects that did the hard part:

* **[Restunts](https://github.com/4d-stunts/restunts)** — the Stunts reverse
  engineering project, fifteen years of community work centred on the
  [Stunts Wiki](https://wiki.stunts.hu/wiki/Restunts). The disassembly this
  port is transcribed from.
* **[restunts2](https://github.com/dstien/restunts2)** — a refurbishment of
  the same work with contemporary tooling. Its symbolic data segment settled
  several tables that the original disassembly carries only as raw bytes.

Neither project is included in this repository; both are worth reading in
their own right.

The OPL2 sound is **[Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3)** by
Nuke.YKT, vendored unmodified under LGPL-2.1 in `src/vendored/opl/`.

*Stunts* and *4D Sports Driving* are the property of their respective rights
holders. This repository contains no game data.
