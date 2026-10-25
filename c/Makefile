#
# Breakout desk accessory, C version - m68k-atari-mint GCC
#
#   make        bin/BREAKOUT.ACC (68000: ST, STE, TT, Falcon, ...)
#   make measure  measuring builds (bin/MEASURE.ACC, bin/ARAMEAS.ACC)
#   make clean
#
# Own startup (crt.S, from the Neko project) instead of MiNTLib: the
# accessory stays in memory all the time.
#

CROSS   = m68k-atari-mint-
CC      = $(CROSS)gcc
STRIP   = $(CROSS)strip

CFLAGS  = -m68000 -O2 -fomit-frame-pointer -Wall
LDFLAGS = -nostdlib
LIBS    = -lgem -lgcc
SRCS    = crt.S breakout.c mtrap.S

all: bin/BREAKOUT.ACC

bin/BREAKOUT.ACC: $(SRCS)
	@mkdir -p bin
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(LIBS)
	$(STRIP) $@

# measuring builds, see breakout.c (MEASURE)
measure: bin/MEASURE.ACC bin/ARAMEAS.ACC
bin/MEASURE.ACC: $(SRCS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -DMEASURE $(LDFLAGS) -o $@ $(SRCS) $(LIBS)
bin/ARAMEAS.ACC: $(SRCS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -DMEASURE -DLOGFILE -DOPEN_AT=45 $(LDFLAGS) -o $@ $(SRCS) $(LIBS)

clean:
	rm -rf bin
