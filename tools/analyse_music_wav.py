#!/usr/bin/env python3
"""analyse_music_wav.py - measure what actually came out of the renderer.

Renders a song with bin/music_tool, then for each analysis window
  * takes the OPL register writes the port made (bin/music_tool --dump-opl)
    and computes the frequency every keyed channel SHOULD be at, using the
    documented YM3812 formula  f = fnum * 49716 / 2^(20-block),
  * FFTs the rendered .wav over the same window and finds the peaks,
  * reports how far each expected fundamental is from the nearest peak.

With src/music_opl_stub.c this is a closed loop over the register writes,
so it verifies the pitch pipeline end to end (note number -> transpose ->
note_to_fnum -> A0/B0 -> audio). It says NOTHING about timbre; the stub is
not an OPL emulator.

  usage: python3 tools/analyse_music_wav.py [SONG] [SECONDS]
"""
import os, re, subprocess, sys, wave
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, 'extracted/stunts/stunts')
TOOL = os.path.join(ROOT, 'bin/music_tool')

def opl_timeline(song, secs):
    """-> list of (tick, {ch: freq_hz}) reconstructed from the register log"""
    out = subprocess.run([TOOL, '--data', DATA, '--dump-opl', song, str(secs)],
                         capture_output=True, text=True, cwd=ROOT).stdout
    a = [0]*9; b = [0]*9
    tl = []
    tick = 0
    for line in out.splitlines():
        m = re.match(r'tick (\d+)', line)
        if m:
            tick = int(m.group(1))
            live = {}
            for c in range(9):
                if b[c] & 0x20:
                    fn = ((b[c] & 3) << 8) | a[c]
                    blk = (b[c] >> 2) & 7
                    if fn: live[c] = fn * 49716.0 / (1 << (20 - blk))
            tl.append((tick, live))
            continue
        m = re.match(r'\s+OPL ([0-9A-F]{2}) <- ([0-9A-F]{2})', line)
        if not m: continue
        r, v = int(m.group(1), 16), int(m.group(2), 16)
        if 0xA0 <= r <= 0xA8: a[r-0xA0] = v
        elif 0xB0 <= r <= 0xB8: b[r-0xB0] = v
        if tl: tl[-1] = (tick, tl[-1][1])
    return tl

def main():
    song = sys.argv[1] if len(sys.argv) > 1 else 'title'
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 12.0
    wav  = f'/tmp/music_{song}.wav'

    subprocess.run([TOOL, '--data', DATA, '--wav', song, str(secs), wav],
                   check=True, cwd=ROOT, capture_output=True)
    w = wave.open(wav)
    rate = w.getframerate()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(float)
    w.close()

    tl = dict(opl_timeline(song, secs))

    # 0.15 s windows (6.7 Hz bins). Only windows over which the pitch state
    # of every channel is CONSTANT are measured - a window containing a
    # key-on or a pitch change has no single fundamental to compare against,
    # so measuring it would test nothing. The music is dense (a note every
    # 6 sequencer ticks = 0.125 s in places), so windows longer than this
    # simply do not exist in these songs.
    wticks = 15
    win = int(wticks / 100.0 * rate)
    hann = np.hanning(win)
    checked = matched = 0
    worst = 0.0
    errs = []
    report = []
    stable = []
    for tick in range(1, int(secs * 100) - wticks - 2):
        base = tl.get(tick)
        if not base: continue
        if all(tl.get(tick + k) == base for k in range(1, wticks + 1)):
            stable.append(tick)
    for tick in stable:
        exp = tl[tick]
        s = int(tick / 100.0 * rate)
        if s + win > len(x): break
        spec = np.abs(np.fft.rfft(x[s:s+win] * hann))
        fr = np.fft.rfftfreq(win, 1.0/rate)
        # local maxima above 1% of the strongest bin, refined by fitting a
        # parabola through the peak bin and its two neighbours (the standard
        # sub-bin estimator; without it the answer is quantised to 6.7 Hz)
        df = fr[1] - fr[0]
        thr = spec.max() * 0.01
        pk = []
        for i in range(1, len(spec)-1):
            if spec[i] > thr and spec[i] >= spec[i-1] and spec[i] >= spec[i+1]:
                d = spec[i-1] - 2*spec[i] + spec[i+1]
                off = 0.5*(spec[i-1] - spec[i+1])/d if d else 0.0
                pk.append(fr[i] + off*df)
        for ch, f in sorted(exp.items()):
            if f < 60: continue          # below 4 bins, not resolvable here
            checked += 1
            if not pk: continue
            near = min(pk, key=lambda p: abs(p - f))
            err = abs(near - f)
            errs.append(err)
            if err <= 2.0:               # 0.15 s window, sub-bin estimator
                matched += 1
            worst = max(worst, err)
            if len(report) < 12:
                report.append((tick/100.0, ch, f, near, err))

    print(f"song {song}, {secs} s at {rate} Hz, {len(x)} samples")
    print(f"peak RMS {np.sqrt((x**2).mean()):.0f}, max |sample| {int(np.abs(x).max())}")
    print("\n  time   ch   expected Hz   nearest FFT peak   error")
    for t, ch, f, n, e in report:
        print(f"  {t:5.2f}  {ch}    {f:9.2f}      {n:9.2f}     {e:6.2f}")
    print(f"\n  {len(stable)} stable windows of {wticks/100:.2f} s found")
    print(f"  {matched}/{checked} expected fundamentals found within "
          f"2 Hz of an interpolated spectral peak")
    if errs:
        errs = np.array(errs)
        print(f"  median error {np.median(errs):.2f} Hz, "
              f"90th percentile {np.percentile(errs,90):.2f} Hz, "
              f"worst {worst:.2f} Hz")
    return 0 if checked and matched == checked else 1

if __name__ == '__main__':
    sys.exit(main())
