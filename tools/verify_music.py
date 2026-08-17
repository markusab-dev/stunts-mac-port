#!/usr/bin/env python3
"""verify_music.py - an INDEPENDENT check on src/music_native.c.

This script re-implements, in Python and from the same disassembly, the
parts of the chain that decide which note sounds when:

  * the .KMS / .VCE chunk container       (seg029 audioresource_find)
  * the variable-length event stream      (seg028 sub_3945A)
  * tempo, delta timing, the 100 Hz accumulator  (seg028 sub_3868A)
  * the drum-kit dispatch and transpose   (seg028 off_38E7E, patch +0x10)
  * note -> (block, F-number)             (AD15.DRV 0x06F9 + table at 0x08B2)

It then reads `bin/music_tool --dump-opl` and checks that the C port wrote
exactly the same key-on events, at the same 100 Hz tick, in the same order.
Two implementations written from the same source agreeing is not proof, but
a transcription slip in either one shows up immediately.

  usage: python3 tools/verify_music.py [SONG] [SECONDS]
"""
import subprocess, sys, os, re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, 'extracted/stunts/stunts')

def u16(b, o): return b[o] | (b[o+1] << 8)
def u32(b, o): return b[o] | (b[o+1]<<8) | (b[o+2]<<16) | (b[o+3]<<24)
def s8(v):     return v - 256 if v >= 128 else v

# ---- container (seg029 audioresource_find) --------------------------
def res_find(buf, chunk, name):
    n = u16(buf, chunk + 4)
    for i in range(n):
        nm = bytes(buf[chunk+6+4*i:chunk+10+4*i])
        if nm.decode('latin1').upper() == name.upper():
            return chunk + 6 + 8*n + u32(buf, chunk + 6 + 4*n + 4*i)
    return None

# ---- event parser (seg028 sub_3945A) --------------------------------
ARGS = {  # status-0xD9 -> extra bytes after the status byte
    0:'',1:'',2:'',3:'b',4:'b',5:'b',6:'bb',7:'b',8:'b',9:'b',
    10:'',11:'b',12:'w',13:'bd',14:'t',15:'t',16:'b',17:'b'}

def parse(buf, p):
    st0 = p
    v = 0
    while True:
        b = buf[p]; p += 1
        v = (v << 7) + (b & 0x7F)
        if not b & 0x80: break
    delta = v
    st = buf[p]; p += 1
    d1 = d2 = None
    if st >= 0xD9:
        f = ARGS.get(st - 0xD9, '')
        if f == 'b':   d1 = buf[p]; p += 1
        elif f == 'bb': d1 = buf[p]; d2 = buf[p+1]; p += 2
        elif f == 'w': d2 = u16(buf, p); p += 2
        elif f == 'bd': d1 = buf[p]; d2 = u32(buf, p+1); p += 5
        elif f == 't': p += buf[p] + 1
    else:
        if st > 0x80: d1 = buf[p]; p += 1
        v = 0
        while True:
            b = buf[p]; p += 1
            v = (v << 7) + (b & 0x7F)
            if not b & 0x80: break
        d2 = v
    return delta, st, d1, d2, p - st0

# ---- AD15.DRV note_to_fnum (0x06F9, table base 0x08B2) --------------
drv = open(os.path.join(DATA, 'AD15.DRV'), 'rb').read()
FNUM = [u16(drv, 0x8B2 + 2*i) for i in range(-24, 36)]
def note_to_fnum(note, off=0):
    note &= 0xFF
    blk, rem = divmod(note, 12)
    idx = s8((rem + off) & 0xFF)
    return (FNUM[idx + 24] | (blk << 10)) & 0xFFFF

# ---- the drum-kit dispatch (seg028 off_38E7E) -----------------------
KIT = ['BASD','TOMM','SNAR','TOMM','TOMM','TOMM','CHHT','TOMM',
       'OHHT','TOMM','OHHT','TOMM','TOMM','RIDE','TOMM','CRSH']

SONGS = {'title':'SKIDTITL','select':'SKIDSLCT','over':'SKIDOVER',
         'victory':'SKIDVICT'}

def predict(song, seconds):
    kms = open(os.path.join(DATA, SONGS[song] + '.KMS'), 'rb').read()
    vce = open(os.path.join(DATA, 'ADSKIDMS.VCE'), 'rb').read()
    top = res_find(kms, 0, kms[6:10].decode('latin1'))
    hdr = res_find(kms, top, 'hdr1')
    ni  = kms[hdr+6]
    instr = [res_find(vce, 0, kms[hdr+7+4*i:hdr+11+4*i].decode('latin1'))
             for i in range(ni)]
    nt  = kms[hdr+7+4*ni]
    tstart = [res_find(kms, top, kms[hdr+8+4*ni+5*i:hdr+12+4*ni+5*i]
                       .decode('latin1')) for i in range(nt)]

    # per-track sequencer state
    ptr    = [t + 4 for t in tstart]
    start  = [t + 4 for t in tstart]
    delay  = [0] * nt
    patch  = [None] * nt
    lstack = [[] for _ in range(nt)]
    lcount = [[] for _ in range(nt)]
    events = []                       # (timer_tick, track, packed_fnum)
    divisor, accum = 0x80, 0

    def peek_delta(t):
        return parse(kms, ptr[t])[0] if ptr[t] is not None else 0

    for timer in range(int(seconds * 100)):
        accum = (accum + 0x80) & 0xFFFF
        while accum >= divisor:
            accum -= divisor
            for t in range(nt):
                if delay[t]:
                    delay[t] -= 1; continue
                if ptr[t] is None: continue
                while True:
                    if delay[t]: delay[t] -= 1; break
                    if ptr[t] is None: break
                    d, st, d1, d2, ln = parse(kms, ptr[t])
                    ptr[t] += ln
                    if st < 0xD9:
                        ins = patch[t]
                        n0  = st & 0x7F          # loc_38A54 masks first
                        if ins is not None and vce[ins+5] == 5:
                            ins = res_find(vce, 0,
                                  KIT[n0-0x18] if 0 <= n0-0x18 <= 15 else 'TOMM')
                        if ins is not None and u16(vce, ins+0x0C):
                            note = (n0 + s8(vce[ins+0x10])) & 0xFF
                            packed = (note_to_fnum(note)
                                      + s8(vce[ins+0x11])) & 0xFFFF
                            events.append((timer, t, packed))
                    elif st == 0xDC: patch[t] = instr[d1] if d1 < ni else None
                    elif st == 0xDD:
                        if t < 0x10 and d1: divisor = 0x7D00 // d1
                    elif st == 0xE2:
                        lstack[t].append(ptr[t]); lcount[t].append((d1-1) & 0xFF)
                    elif st == 0xE3:
                        if lstack[t]:
                            c = lcount[t][-1]
                            ptr[t] = lstack[t][-1]
                            lcount[t][-1] = (c - 1) & 0xFF
                            if c == 0: lstack[t].pop(); lcount[t].pop()
                    elif st in (0xD9, 0xDA): ptr[t] = None
                    elif st == 0xDB:
                        ptr[t] = start[t]; lstack[t] = []; lcount[t] = []
                    if ptr[t] is None: continue
                    delay[t] = peek_delta(t)
    return events

def from_c(song, seconds):
    out = subprocess.run([os.path.join(ROOT, 'bin/music_tool'),
                          '--data', DATA, '--dump-opl', song, str(seconds)],
                         capture_output=True, text=True, cwd=ROOT).stdout
    tick, pend, got = -1, {}, []
    for line in out.splitlines():
        m = re.match(r'tick (\d+)', line)
        if m: tick = int(m.group(1)); continue
        m = re.match(r'\s+OPL ([0-9A-F]{2}) <- ([0-9A-F]{2})', line)
        if not m: continue
        reg, val = int(m.group(1), 16), int(m.group(2), 16)
        if 0xA0 <= reg <= 0xA8: pend[reg - 0xA0] = val
        elif 0xB0 <= reg <= 0xB8 and (val & 0x20):
            ch = reg - 0xB0
            got.append((tick, ch, ((val & 0x1F) << 8) | pend.get(ch, 0)))
    return got

if __name__ == '__main__':
    song = sys.argv[1] if len(sys.argv) > 1 else 'title'
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0

    exp = predict(song, secs)
    act = from_c(song, secs)
    print(f"song {song}, first {secs} s")
    print(f"  python model   : {len(exp)} note-on events")
    print(f"  C port key-ons : {len(act)} (OPL 0xB0 writes with bit 5 set)")

    # The C port assigns OPL channels dynamically, so compare (tick, packed)
    # multisets rather than channel numbers.
    e = sorted((t, p) for t, _, p in exp)
    a = sorted((t, p) for t, _, p in act)
    if e == a:
        print("  MATCH: identical (100 Hz tick, block<<10|fnum) sequences")
        sys.exit(0)
    print("  MISMATCH")
    for i, (x, y) in enumerate(zip(e, a)):
        if x != y:
            print(f"    first difference at #{i}: python {x} vs C {y}")
            print(f"    python around: {e[max(0,i-3):i+4]}")
            print(f"    C      around: {a[max(0,i-3):i+4]}")
            break
    else:
        print(f"    prefixes agree; lengths differ ({len(e)} vs {len(a)})")
        print(f"    python tail: {e[len(a):len(a)+6]}")
        print(f"    C      tail: {a[len(e):len(e)+6]}")
    sys.exit(1)
