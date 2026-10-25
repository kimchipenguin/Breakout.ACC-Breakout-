#!/usr/bin/env python3
"""readlog.py brklog.bin: steps per second from a LOGFILE measuring build"""
import struct, sys
t0, p0, s0, t1, p1, s1 = struct.unpack('>6I', open(sys.argv[1], 'rb').read())
dt = (t1 - t0) / 200
n = (p1 - p0) + (s1 - s0)
print('%d steps in %.2f s = %.1f steps/s (play %d, serve %d)'
      % (n, dt, n / dt, p1 - p0, s1 - s0))
