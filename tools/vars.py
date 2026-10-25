#!/usr/bin/env python3
"""vars.py ACC RAMDUMP name... : print word variables of the running ACC
(located by the 'BKCT' marker) from a RAM dump"""
import struct, sys
d = open(sys.argv[1], 'rb').read()
_, t, dt, b, s = struct.unpack('>HIIII', d[:18])
syms = {}
o = 28 + t + dt
for i in range(s // 14):
    e = d[o + i * 14:o + i * 14 + 14]
    ty, v = struct.unpack('>HI', e[8:])
    v += t if ty & 0x0400 else t + dt if ty & 0x0100 else 0   # vasm: per section
    syms[e[:8].rstrip(b'\0').decode()] = v
ram = open(sys.argv[2], 'rb').read()
# the marker whose program text matches the file (the first hit can be the
# move.l #'BKCT' instruction itself)
m = -1
while True:
    m = ram.find(b'BKCT', m + 1)
    base = m - syms['marker']
    if m < 0 or ram[m - 2:m] != b'\x23\xfc':
        break
print('text at $%x' % base)
for n in sys.argv[3:]:
    a = base + syms[n]
    print('%-10s $%06x  %04x' % (n, a, struct.unpack('>H', ram[a:a + 2])[0]))
