#
# Breakout+ desk accessory - m68k-atari-mint GCC
#
#   make           ../BRKPLUS.ACC and ../BRKPLUS.APP (68000: ST, STE, TT,
#                  Falcon, ...) - the same file: accessory or application
#   make autotest  ../test/AUTOTEST.ACC (scripted, see autotest.h)
#   make clean
#
# Own startup (crt.S, from the Neko project) instead of MiNTLib.
#

CROSS   = m68k-atari-mint-
CC      = $(CROSS)gcc
STRIP   = $(CROSS)strip

CFLAGS  = -m68000 -O2 -fomit-frame-pointer -Wall
# no C library: keep GCC from turning loops into strlen/memset calls
CFLAGS += -fno-tree-loop-distribute-patterns -fno-builtin
# GCC 15 combined "move.b (a0)+,(a0,d0.l)" assuming a0 before the increment;
# the 68000 uses it after (seen in save_hiscore). Leave out auto-increment.
CFLAGS += -fno-auto-inc-dec
LDFLAGS = -nostdlib
LIBS    = -lgem -lgcc
SRCS    = crt.S brkplus.c mtrap.S

all: ../BRKPLUS.ACC ../BRKPLUS.APP

../BRKPLUS.APP: ../BRKPLUS.ACC
	cp ../BRKPLUS.ACC ../BRKPLUS.APP

../BRKPLUS.ACC: $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(LIBS)
	$(STRIP) $@

autotest: ../test/AUTOTEST.ACC
../test/AUTOTEST.ACC: $(SRCS) autotest.h
	@mkdir -p ../test
	$(CC) $(CFLAGS) -DAUTOTEST $(LDFLAGS) -o $@ $(SRCS) $(LIBS)

clean:
	rm -f ../BRKPLUS.ACC ../BRKPLUS.APP ../test/AUTOTEST.ACC
