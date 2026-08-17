#!/usr/bin/env python3
"""
dis8086.py -- a small, self-contained 16-bit x86 (8086/80186) disassembler.

Written because there is no usable binary-mode disassembler on this machine
(llvm-objdump has no -b binary -m i8086, capstone is not installed).

Usage:
    dis8086.py FILE [--org N] [--start N] [--end N]
    dis8086.py FILE --trace ENTRY[,ENTRY...]   # recursive-descent pass
    dis8086.py FILE --report                   # linear + recursive + data dump

Output format:   OFFSET: HEXBYTES   mnemonic operands

Anything that cannot be decoded is emitted as `db 0xNN  ; ???`.
"""

import sys

# ---------------------------------------------------------------- registers

R16 = ['ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di']
R8  = ['al', 'cl', 'dl', 'bl', 'ah', 'ch', 'dh', 'bh']
SREG = ['es', 'cs', 'ss', 'ds', 'fs', 'gs', '?s6', '?s7']

RM16 = ['bx+si', 'bx+di', 'bp+si', 'bp+di', 'si', 'di', 'bp', 'bx']

CC = ['o', 'no', 'b', 'nb', 'z', 'nz', 'be', 'nbe',
      's', 'ns', 'p', 'np', 'l', 'nl', 'le', 'nle']
# nicer aliases matching common assembler output
CC_NAME = ['jo', 'jno', 'jb', 'jnb', 'jz', 'jnz', 'jbe', 'ja',
           'js', 'jns', 'jp', 'jnp', 'jl', 'jge', 'jle', 'jg']

GRP1 = ['add', 'or', 'adc', 'sbb', 'and', 'sub', 'xor', 'cmp']
GRP2 = ['rol', 'ror', 'rcl', 'rcr', 'shl', 'shr', 'sal', 'sar']
GRP3 = ['test', 'test', 'not', 'neg', 'mul', 'imul', 'div', 'idiv']


def h8(v):
    return '0x%02x' % (v & 0xff)


def h16(v):
    return '0x%04x' % (v & 0xffff)


def sdisp(v):
    """signed displacement string, e.g. +0x12 / -0x04"""
    if v < 0:
        return '-0x%x' % (-v)
    return '+0x%x' % v


class Insn:
    __slots__ = ('addr', 'length', 'text', 'bytes', 'kind', 'target',
                 'memref', 'ok')

    def __init__(self, addr, length, text, raw, kind='normal', target=None,
                 memref=None, ok=True):
        self.addr = addr
        self.length = length
        self.text = text
        self.bytes = raw
        # kind: normal | jmp | jcc | call | ret | int | bad | jmp_indirect
        self.kind = kind
        self.target = target
        self.memref = memref   # absolute-offset memory reference, if literal
        self.ok = ok

    def __str__(self):
        return self.text


class Decoder:
    def __init__(self, data, org=0):
        self.d = data
        self.org = org

    # -------------------------------------------------------------- helpers
    def u8(self, p):
        return self.d[p]

    def s8(self, p):
        v = self.d[p]
        return v - 256 if v >= 128 else v

    def u16(self, p):
        return self.d[p] | (self.d[p + 1] << 8)

    def s16(self, p):
        v = self.u16(p)
        return v - 0x10000 if v >= 0x8000 else v

    # -------------------------------------------------------------- modrm
    def modrm(self, p, seg):
        """Decode ModRM at file position p.
        Returns (nbytes, mod, reg, rm_str_word, rm_str_byte, memref_or_None)."""
        m = self.d[p]
        mod = (m >> 6) & 3
        reg = (m >> 3) & 7
        rm = m & 7
        n = 1
        memref = None
        if mod == 3:
            return (1, mod, reg, R16[rm], R8[rm], None)
        if mod == 0 and rm == 6:
            disp = self.u16(p + 1)
            n = 3
            body = h16(disp)
            memref = disp
        else:
            body = RM16[rm]
            if mod == 1:
                disp = self.s8(p + 1)
                n = 2
                if disp:
                    body += sdisp(disp)
            elif mod == 2:
                disp = self.s16(p + 1)
                n = 3
                if disp:
                    body += sdisp(disp)
        pfx = (seg + ':') if seg else ''
        s = pfx + '[' + body + ']'
        return (n, mod, reg, s, s, memref)

    # -------------------------------------------------------------- decode
    def decode(self, p):
        """Decode one instruction at file offset p. Returns Insn."""
        start = p
        seg = None
        rep = ''
        lock = ''
        n = len(self.d)

        # prefixes
        while p < n:
            b = self.d[p]
            if b == 0x26:
                seg = 'es'; p += 1
            elif b == 0x2e:
                seg = 'cs'; p += 1
            elif b == 0x36:
                seg = 'ss'; p += 1
            elif b == 0x3e:
                seg = 'ds'; p += 1
            elif b == 0xf0:
                lock = 'lock '; p += 1
            elif b == 0xf2:
                rep = 'repne '; p += 1
            elif b == 0xf3:
                rep = 'rep '; p += 1
            else:
                break
        if p >= n:
            return self._bad(start)

        ins = self._decode_core(start, p, seg, rep, lock)
        return ins

    def _bad(self, start):
        b = self.d[start]
        return Insn(start + self.org, 1, 'db %s' % h8(b) + '  ; ???',
                    self.d[start:start + 1], kind='bad', ok=False)

    def _mk(self, start, end, text, kind='normal', target=None, memref=None):
        return Insn(start + self.org, end - start, text,
                    self.d[start:end], kind=kind, target=target, memref=memref)

    def _decode_core(self, start, p, seg, rep, lock):
        d = self.d
        n = len(d)
        op = d[p]
        pfx = lock + rep

        def need(k):
            return p + k <= n

        # --- arithmetic/logic regular forms 0x00..0x3D
        if op < 0x40 and (op & 7) < 6 and op not in (0x0f,):
            grp = GRP1[(op >> 3) & 7]
            form = op & 7
            if form in (0, 1, 2, 3):
                if not need(2):
                    return self._bad(start)
                mn, mod, reg, rmw, rmb, mref = self.modrm(p + 1, seg)
                w = form & 1
                rmv = rmw if w else rmb
                rg = R16[reg] if w else R8[reg]
                if form < 2:
                    txt = '%s%s %s, %s' % (pfx, grp, rmv, rg)
                else:
                    txt = '%s%s %s, %s' % (pfx, grp, rg, rmv)
                return self._mk(start, p + 1 + mn, txt, memref=mref)
            if form == 4:
                if not need(2):
                    return self._bad(start)
                return self._mk(start, p + 2, '%s%s al, %s' % (pfx, grp, h8(d[p + 1])))
            if form == 5:
                if not need(3):
                    return self._bad(start)
                return self._mk(start, p + 3, '%s%s ax, %s' % (pfx, grp, h16(self.u16(p + 1))))

        # --- push/pop segment regs, misc single-byte
        SIMPLE = {
            0x06: 'push es', 0x07: 'pop es',
            0x0e: 'push cs',
            0x16: 'push ss', 0x17: 'pop ss',
            0x1e: 'push ds', 0x1f: 'pop ds',
            0x27: 'daa', 0x2f: 'das', 0x37: 'aaa', 0x3f: 'aas',
            0x60: 'pusha', 0x61: 'popa',
            0x6c: 'insb', 0x6d: 'insw', 0x6e: 'outsb', 0x6f: 'outsw',
            0x90: 'nop', 0x98: 'cbw', 0x99: 'cwd',
            0x9b: 'fwait', 0x9c: 'pushf', 0x9d: 'popf',
            0x9e: 'sahf', 0x9f: 'lahf',
            0xa4: 'movsb', 0xa5: 'movsw', 0xa6: 'cmpsb', 0xa7: 'cmpsw',
            0xaa: 'stosb', 0xab: 'stosw', 0xac: 'lodsb', 0xad: 'lodsw',
            0xae: 'scasb', 0xaf: 'scasw',
            0xc9: 'leave',
            0xd6: 'salc', 0xd7: 'xlat',
            0xec: 'in al, dx', 0xed: 'in ax, dx',
            0xee: 'out dx, al', 0xef: 'out dx, ax',
            0xf4: 'hlt', 0xf5: 'cmc',
            0xf8: 'clc', 0xf9: 'stc', 0xfa: 'cli', 0xfb: 'sti',
            0xfc: 'cld', 0xfd: 'std',
        }
        if op in SIMPLE:
            txt = pfx + SIMPLE[op]
            if seg and op in (0xa4, 0xa5, 0xa6, 0xa7, 0xac, 0xad):
                txt += '   ; src seg %s' % seg
            return self._mk(start, p + 1, txt)

        if op == 0x0f:
            return self._bad(start)   # 8086: pop cs; 286+: escape. Flag it.

        if 0x40 <= op <= 0x47:
            return self._mk(start, p + 1, '%sinc %s' % (pfx, R16[op - 0x40]))
        if 0x48 <= op <= 0x4f:
            return self._mk(start, p + 1, '%sdec %s' % (pfx, R16[op - 0x48]))
        if 0x50 <= op <= 0x57:
            return self._mk(start, p + 1, '%spush %s' % (pfx, R16[op - 0x50]))
        if 0x58 <= op <= 0x5f:
            return self._mk(start, p + 1, '%spop %s' % (pfx, R16[op - 0x58]))

        if op == 0x62:
            if not need(2):
                return self._bad(start)
            mn, mod, reg, rmw, rmb, mref = self.modrm(p + 1, seg)
            return self._mk(start, p + 1 + mn, '%sbound %s, %s' % (pfx, R16[reg], rmw), memref=mref)
        if op == 0x68:
            if not need(3):
                return self._bad(start)
            return self._mk(start, p + 3, '%spush %s' % (pfx, h16(self.u16(p + 1))))
        if op == 0x6a:
            if not need(2):
                return self._bad(start)
            return self._mk(start, p + 2, '%spush %s' % (pfx, h8(d[p + 1])))
        if op == 0x69:
            if not need(2):
                return self._bad(start)
            mn, mod, reg, rmw, rmb, mref = self.modrm(p + 1, seg)
            if not need(1 + mn + 2):
                return self._bad(start)
            imm = self.u16(p + 1 + mn)
            return self._mk(start, p + 1 + mn + 2,
                            '%simul %s, %s, %s' % (pfx, R16[reg], rmw, h16(imm)), memref=mref)
        if op == 0x6b:
            if not need(2):
                return self._bad(start)
            mn, mod, reg, rmw, rmb, mref = self.modrm(p + 1, seg)
            if not need(1 + mn + 1):
                return self._bad(start)
            imm = self.s8(p + 1 + mn)
            return self._mk(start, p + 1 + mn + 1,
                            '%simul %s, %s, %s' % (pfx, R16[reg], rmw, h16(imm)), memref=mref)

        # --- Jcc rel8
        if 0x70 <= op <= 0x7f:
            if not need(2):
                return self._bad(start)
            rel = self.s8(p + 1)
            tgt = (p + 2 + rel)
            return self._mk(start, p + 2,
                            '%s%s %s' % (pfx, CC_NAME[op - 0x70], h16(tgt + self.org)),
                            kind='jcc', target=tgt)

        # --- group 1: 80/81/82/83
        if op in (0x80, 0x81, 0x82, 0x83):
            if not need(2):
                return self._bad(start)
            mn, mod, reg, rmw, rmb, mref = self.modrm(p + 1, seg)
            grp = GRP1[reg]
            if op in (0x80, 0x82):
                if not need(1 + mn + 1):
                    return self._bad(start)
                imm = d[p + 1 + mn]
                rmv = rmb if mod == 3 else ('byte ' + rmb)
                return self._mk(start, p + 1 + mn + 1,
                                '%s%s %s, %s' % (pfx, grp, rmv, h8(imm)), memref=mref)
            if op == 0x81:
                if not need(1 + mn + 2):
                    return self._bad(start)
                imm = self.u16(p + 1 + mn)
                rmv = rmw if mod == 3 else ('word ' + rmw)
                return self._mk(start, p + 1 + mn + 2,
                                '%s%s %s, %s' % (pfx, grp, rmv, h16(imm)), memref=mref)
            # 0x83
            if not need(1 + mn + 1):
                return self._bad(start)
            imm = self.s8(p + 1 + mn)
            rmv = rmw if mod == 3 else ('word ' + rmw)
            return self._mk(start, p + 1 + mn + 1,
                            '%s%s %s, %s' % (pfx, grp, rmv, h16(imm)), memref=mref)

        # --- test/xchg/mov r/m,r
        if op in (0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b):
            if not need(2):
                return self._bad(start)
            mn, mod, reg, rmw, rmb, mref = self.modrm(p + 1, seg)
            w = op & 1
            rmv = rmw if w else rmb
            rg = R16[reg] if w else R8[reg]
            if op in (0x84, 0x85):
                txt = '%stest %s, %s' % (pfx, rmv, rg)
            elif op in (0x86, 0x87):
                txt = '%sxchg %s, %s' % (pfx, rmv, rg)
            elif op in (0x88, 0x89):
                txt = '%smov %s, %s' % (pfx, rmv, rg)
            else:
                txt = '%smov %s, %s' % (pfx, rg, rmv)
            return self._mk(start, p + 1 + mn, txt, memref=mref)

        if op == 0x8c or op == 0x8e:
            if not need(2):
                return self._bad(start)
            mn, mod, reg, rmw, rmb, mref = self.modrm(p + 1, seg)
            if op == 0x8c:
                txt = '%smov %s, %s' % (pfx, rmw, SREG[reg])
            else:
                txt = '%smov %s, %s' % (pfx, SREG[reg], rmw)
            return self._mk(start, p + 1 + mn, txt, memref=mref)

        if op == 0x8d:
            if not need(2):
                return self._bad(start)
            mn, mod, reg, rmw, rmb, mref = self.modrm(p + 1, seg)
            if mod == 3:
                return self._bad(start)
            return self._mk(start, p + 1 + mn, '%slea %s, %s' % (pfx, R16[reg], rmw), memref=mref)

        if op == 0x8f:
            if not need(2):
                return self._bad(start)
            mn, mod, reg, rmw, rmb, mref = self.modrm(p + 1, seg)
            if reg != 0:
                return self._bad(start)
            return self._mk(start, p + 1 + mn, '%spop %s' % (pfx, rmw), memref=mref)

        if 0x91 <= op <= 0x97:
            return self._mk(start, p + 1, '%sxchg ax, %s' % (pfx, R16[op - 0x90]))

        if op == 0x9a:
            if not need(5):
                return self._bad(start)
            o = self.u16(p + 1); s = self.u16(p + 3)
            return self._mk(start, p + 5, '%scall far %s:%s' % (pfx, h16(s), h16(o)),
                            kind='callfar')

        if op in (0xa0, 0xa1, 0xa2, 0xa3):
            if not need(3):
                return self._bad(start)
            addr = self.u16(p + 1)
            pf = (seg + ':') if seg else ''
            loc = '%s[%s]' % (pf, h16(addr))
            r = 'al' if (op & 1) == 0 else 'ax'
            if op < 0xa2:
                txt = '%smov %s, %s' % (pfx, r, loc)
            else:
                txt = '%smov %s, %s' % (pfx, loc, r)
            return self._mk(start, p + 3, txt, memref=addr)

        if op == 0xa8:
            if not need(2):
                return self._bad(start)
            return self._mk(start, p + 2, '%stest al, %s' % (pfx, h8(d[p + 1])))
        if op == 0xa9:
            if not need(3):
                return self._bad(start)
            return self._mk(start, p + 3, '%stest ax, %s' % (pfx, h16(self.u16(p + 1))))

        if 0xb0 <= op <= 0xb7:
            if not need(2):
                return self._bad(start)
            return self._mk(start, p + 2, '%smov %s, %s' % (pfx, R8[op - 0xb0], h8(d[p + 1])))
        if 0xb8 <= op <= 0xbf:
            if not need(3):
                return self._bad(start)
            return self._mk(start, p + 3, '%smov %s, %s' % (pfx, R16[op - 0xb8], h16(self.u16(p + 1))))

        # --- group 2 shifts
        if op in (0xc0, 0xc1, 0xd0, 0xd1, 0xd2, 0xd3):
            if not need(2):
                return self._bad(start)
            mn, mod, reg, rmw, rmb, mref = self.modrm(p + 1, seg)
            mnem = GRP2[reg]
            w = op & 1
            rmv = rmw if w else rmb
            if mod != 3:
                rmv = ('word ' if w else 'byte ') + rmv
            if op in (0xc0, 0xc1):
                if not need(1 + mn + 1):
                    return self._bad(start)
                cnt = h8(d[p + 1 + mn])
                return self._mk(start, p + 1 + mn + 1,
                                '%s%s %s, %s' % (pfx, mnem, rmv, cnt), memref=mref)
            cnt = '1' if op in (0xd0, 0xd1) else 'cl'
            return self._mk(start, p + 1 + mn, '%s%s %s, %s' % (pfx, mnem, rmv, cnt), memref=mref)

        if op == 0xc2:
            if not need(3):
                return self._bad(start)
            return self._mk(start, p + 3, '%sret %s' % (pfx, h16(self.u16(p + 1))), kind='ret')
        if op == 0xc3:
            return self._mk(start, p + 1, '%sret' % pfx, kind='ret')
        if op == 0xca:
            if not need(3):
                return self._bad(start)
            return self._mk(start, p + 3, '%sretf %s' % (pfx, h16(self.u16(p + 1))), kind='ret')
        if op == 0xcb:
            return self._mk(start, p + 1, '%sretf' % pfx, kind='ret')

        if op in (0xc4, 0xc5):
            if not need(2):
                return self._bad(start)
            mn, mod, reg, rmw, rmb, mref = self.modrm(p + 1, seg)
            if mod == 3:
                return self._bad(start)
            mnem = 'les' if op == 0xc4 else 'lds'
            return self._mk(start, p + 1 + mn, '%s%s %s, %s' % (pfx, mnem, R16[reg], rmw), memref=mref)

        if op in (0xc6, 0xc7):
            if not need(2):
                return self._bad(start)
            mn, mod, reg, rmw, rmb, mref = self.modrm(p + 1, seg)
            if reg != 0:
                return self._bad(start)
            if op == 0xc6:
                if not need(1 + mn + 1):
                    return self._bad(start)
                rmv = rmb if mod == 3 else ('byte ' + rmb)
                return self._mk(start, p + 1 + mn + 1,
                                '%smov %s, %s' % (pfx, rmv, h8(d[p + 1 + mn])), memref=mref)
            if not need(1 + mn + 2):
                return self._bad(start)
            rmv = rmw if mod == 3 else ('word ' + rmw)
            return self._mk(start, p + 1 + mn + 2,
                            '%smov %s, %s' % (pfx, rmv, h16(self.u16(p + 1 + mn))), memref=mref)

        if op == 0xc8:
            if not need(4):
                return self._bad(start)
            return self._mk(start, p + 4, '%senter %s, %s' % (pfx, h16(self.u16(p + 1)), h8(d[p + 3])))

        if op == 0xcc:
            return self._mk(start, p + 1, '%sint3' % pfx, kind='int')
        if op == 0xcd:
            if not need(2):
                return self._bad(start)
            return self._mk(start, p + 2, '%sint %s' % (pfx, h8(d[p + 1])), kind='int')
        if op == 0xce:
            return self._mk(start, p + 1, '%sinto' % pfx)
        if op == 0xcf:
            return self._mk(start, p + 1, '%siret' % pfx, kind='ret')

        if op == 0xd4:
            if not need(2):
                return self._bad(start)
            if d[p + 1] == 0x0a:
                return self._mk(start, p + 2, '%saam' % pfx)
            return self._mk(start, p + 2, '%saam %s' % (pfx, h8(d[p + 1])))
        if op == 0xd5:
            if not need(2):
                return self._bad(start)
            if d[p + 1] == 0x0a:
                return self._mk(start, p + 2, '%saad' % pfx)
            return self._mk(start, p + 2, '%saad %s' % (pfx, h8(d[p + 1])))

        if 0xd8 <= op <= 0xdf:
            if not need(2):
                return self._bad(start)
            mn, mod, reg, rmw, rmb, mref = self.modrm(p + 1, seg)
            return self._mk(start, p + 1 + mn,
                            '%sesc %s, %s   ; x87 (unlikely in this blob)' % (pfx, (op & 7), rmw),
                            memref=mref)

        if 0xe0 <= op <= 0xe3:
            if not need(2):
                return self._bad(start)
            rel = self.s8(p + 1)
            tgt = p + 2 + rel
            mnem = ['loopnz', 'loopz', 'loop', 'jcxz'][op - 0xe0]
            return self._mk(start, p + 2, '%s%s %s' % (pfx, mnem, h16(tgt + self.org)),
                            kind='jcc', target=tgt)

        if op in (0xe4, 0xe5, 0xe6, 0xe7):
            if not need(2):
                return self._bad(start)
            port = h8(d[p + 1])
            r = 'al' if (op & 1) == 0 else 'ax'
            if op < 0xe6:
                txt = '%sin %s, %s' % (pfx, r, port)
            else:
                txt = '%sout %s, %s' % (pfx, port, r)
            return self._mk(start, p + 2, txt)

        if op == 0xe8:
            if not need(3):
                return self._bad(start)
            rel = self.s16(p + 1)
            tgt = p + 3 + rel
            return self._mk(start, p + 3, '%scall %s' % (pfx, h16(tgt + self.org)),
                            kind='call', target=tgt)
        if op == 0xe9:
            if not need(3):
                return self._bad(start)
            rel = self.s16(p + 1)
            tgt = p + 3 + rel
            return self._mk(start, p + 3, '%sjmp %s' % (pfx, h16(tgt + self.org)),
                            kind='jmp', target=tgt)
        if op == 0xea:
            if not need(5):
                return self._bad(start)
            o = self.u16(p + 1); s = self.u16(p + 3)
            return self._mk(start, p + 5, '%sjmp far %s:%s' % (pfx, h16(s), h16(o)), kind='jmp')
        if op == 0xeb:
            if not need(2):
                return self._bad(start)
            rel = self.s8(p + 1)
            tgt = p + 2 + rel
            return self._mk(start, p + 2, '%sjmp short %s' % (pfx, h16(tgt + self.org)),
                            kind='jmp', target=tgt)

        if op in (0xf6, 0xf7):
            if not need(2):
                return self._bad(start)
            mn, mod, reg, rmw, rmb, mref = self.modrm(p + 1, seg)
            w = op & 1
            rmv = rmw if w else rmb
            if mod != 3:
                rmv = ('word ' if w else 'byte ') + rmv
            mnem = GRP3[reg]
            if reg in (0, 1):
                if w:
                    if not need(1 + mn + 2):
                        return self._bad(start)
                    imm = h16(self.u16(p + 1 + mn))
                    return self._mk(start, p + 1 + mn + 2,
                                    '%stest %s, %s' % (pfx, rmv, imm), memref=mref)
                if not need(1 + mn + 1):
                    return self._bad(start)
                imm = h8(d[p + 1 + mn])
                return self._mk(start, p + 1 + mn + 1,
                                '%stest %s, %s' % (pfx, rmv, imm), memref=mref)
            return self._mk(start, p + 1 + mn, '%s%s %s' % (pfx, mnem, rmv), memref=mref)

        if op == 0xfe:
            if not need(2):
                return self._bad(start)
            mn, mod, reg, rmw, rmb, mref = self.modrm(p + 1, seg)
            if reg > 1:
                return self._bad(start)
            rmv = rmb if mod == 3 else ('byte ' + rmb)
            mnem = 'inc' if reg == 0 else 'dec'
            return self._mk(start, p + 1 + mn, '%s%s %s' % (pfx, mnem, rmv), memref=mref)

        if op == 0xff:
            if not need(2):
                return self._bad(start)
            mn, mod, reg, rmw, rmb, mref = self.modrm(p + 1, seg)
            e = p + 1 + mn
            if reg == 0 or reg == 1:
                rmv = rmw if mod == 3 else ('word ' + rmw)
                return self._mk(start, e, '%s%s %s' % (pfx, 'inc' if reg == 0 else 'dec', rmv),
                                memref=mref)
            if reg == 2:
                return self._mk(start, e, '%scall %s' % (pfx, rmw), kind='call_indirect', memref=mref)
            if reg == 3:
                if mod == 3:
                    return self._bad(start)
                return self._mk(start, e, '%scall far %s' % (pfx, rmw),
                                kind='call_indirect', memref=mref)
            if reg == 4:
                return self._mk(start, e, '%sjmp %s' % (pfx, rmw), kind='jmp_indirect', memref=mref)
            if reg == 5:
                if mod == 3:
                    return self._bad(start)
                return self._mk(start, e, '%sjmp far %s' % (pfx, rmw),
                                kind='jmp_indirect', memref=mref)
            if reg == 6:
                rmv = rmw if mod == 3 else ('word ' + rmw)
                return self._mk(start, e, '%spush %s' % (pfx, rmv), memref=mref)
            return self._bad(start)

        return self._bad(start)


# ---------------------------------------------------------------- passes

def linear(data, org=0, start=0, end=None):
    dec = Decoder(data, org)
    if end is None:
        end = len(data)
    out = []
    p = start
    while p < end:
        ins = dec.decode(p)
        out.append(ins)
        p += ins.length
    return out


def recursive(data, entries, org=0):
    """Recursive-descent. Returns (dict addr->Insn, set of reached byte offsets)."""
    dec = Decoder(data, org)
    seen = {}
    covered = set()
    work = list(entries)
    while work:
        p = work.pop()
        while True:
            if p < 0 or p >= len(data):
                break
            if p in seen:
                break
            ins = dec.decode(p)
            seen[p] = ins
            for i in range(p, p + ins.length):
                covered.add(i)
            k = ins.kind
            if k == 'bad':
                break
            if k in ('ret',):
                break
            if k == 'jmp':
                if ins.target is not None and 0 <= ins.target < len(data):
                    work.append(ins.target)
                break
            if k in ('jmp_indirect',):
                break
            if k == 'jcc':
                if ins.target is not None and 0 <= ins.target < len(data):
                    work.append(ins.target)
            elif k == 'call':
                if ins.target is not None and 0 <= ins.target < len(data):
                    work.append(ins.target)
            p += ins.length
    return seen, covered


def fmt(ins, labels=None):
    hx = ' '.join('%02x' % b for b in ins.bytes)
    lbl = ''
    if labels and ins.addr in labels:
        lbl = ''
    return '%04x: %-22s %s' % (ins.addr, hx, ins.text)


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1
    fn = args[0]
    org = 0
    start = 0
    end = None
    trace = None
    i = 1
    while i < len(args):
        a = args[i]
        if a == '--org':
            org = int(args[i + 1], 0); i += 2
        elif a == '--start':
            start = int(args[i + 1], 0); i += 2
        elif a == '--end':
            end = int(args[i + 1], 0); i += 2
        elif a == '--trace':
            trace = [int(x, 0) for x in args[i + 1].split(',')]; i += 2
        else:
            print('unknown arg', a, file=sys.stderr)
            return 1
    data = open(fn, 'rb').read()
    if trace is not None:
        seen, covered = recursive(data, trace, org)
        for a in sorted(seen):
            print(fmt(seen[a]))
        print('; covered %d / %d bytes' % (len(covered), len(data)))
    else:
        for ins in linear(data, org, start, end):
            print(fmt(ins))
    return 0


if __name__ == '__main__':
    sys.exit(main())
