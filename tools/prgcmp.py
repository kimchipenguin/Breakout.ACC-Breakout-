#!/usr/bin/env python3
"""prgcmp.py A B: compare two PRGs ignoring the symbol table"""
import struct, sys
def parts(f):
    d = open(f, 'rb').read()
    _, t, dt, b, s = struct.unpack('>HIIII', d[:18])
    return (t, dt, b), d[28:28 + t + dt], d[28 + t + dt + s:].rstrip(b'\0')
a, b = parts(sys.argv[1]), parts(sys.argv[2])
for n, x, y in zip(('sizes', 'text+data', 'relocations'), a, b):
    if x != y:
        sys.exit('DIFFERENT: %s' % n)
print('OK: identical to original (code, data, relocations)')
