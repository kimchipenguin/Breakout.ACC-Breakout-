VASM = $(HOME)/Develop/vasm-src/vasm/vasmm68k_mot -Ftos -no-opt -quiet

all: build/BREAKOUT.ACC bin/BREAKOUT.ACC
	python3 tools/prgcmp.py orig/BREAKOUT.ACC build/BREAKOUT.ACC

src/breakout.s: orig/BREAKOUT.ACC tools/mksrc.py
	python3 tools/mksrc.py $< $@
src/breakfix.s: orig/BREAKOUT.ACC tools/mksrc.py tools/patches.py
	python3 tools/mksrc.py $< $@ --fix

# original, rebuilt from source
build/BREAKOUT.ACC: src/breakout.s
	$(VASM) -o $@ $<
# fixed version
bin/BREAKOUT.ACC: src/breakfix.s src/fix.s
	@mkdir -p bin
	$(VASM) -Isrc -o $@ $<
# measuring build (see src/fix.s)
build/MEASURE.ACC: src/breakfix.s src/fix.s
	$(VASM) -Isrc -DMEASURE=1 -o $@ $<

build/MEASORIG.ACC: src/breakfix.s src/fix.s
	$(VASM) -Isrc -DMEASURE=1 -DORIGTIMING=1 -o $@ $<

clean:
	rm -rf build bin src/breakout.s src/breakfix.s
