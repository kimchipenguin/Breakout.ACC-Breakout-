# BREAKOUT.ACC – fixed

Breakout desk accessory for the Atari ST (Alcyon C, file date 6 Feb 1986),
made to run at its original speed everywhere and to behave under
multitasking AESes. There is no source; the program is rebuilt from the
binary.

- `orig/BREAKOUT.ACC` – the original, untouched
- `bin/BREAKOUT.ACC` – the fixed accessory, patched binary (copy to the boot
  drive / ACC folder)
- `c/` – the same game rewritten in C (`c/bin/BREAKOUT.ACC`), with the same
  fixes; see "C version" below
- `plus/` – Breakout+, an extended version built on the C version
  (resizing, iconify, rainbow bricks, high score, credits); see
  `plus/readme.md`

## What was wrong and what changed

**Speed.** The game moves the ball one step per AES timer event (16 ms
requested). Atari TOS 1.04/3.06, EmuTOS and MagiC round that to their
20 ms tick, giving 50 steps a second – the speed the game was made for.
XaAES under MiNT returns the timer almost at once. Measured with the
original timing (steps per second):

| setup | original timing | fixed |
|---|---|---|
| ST, TOS 1.04, low res | 50.0 | 50.0 |
| ST, TOS 1.04, mono | 49.8 | – |
| TT, TOS 3.06 | 50.1 | 48.4 * |
| EmuTOS on ST / Falcon | 49.7 / 50.1 | 50.0 / 50.0 |
| MagiC 6 on ST / TT | 49.4 / 49.9 | 49.8 / 50.0 |
| ARAnyM, FreeMiNT 1.19 + XaAES | **34567** | 50.0 |

\* the measuring build forces a serve every 3 s; a few steps are dropped at
each change of phase. Without the serves: 50.0.

Now the steps are counted off the 200 Hz system clock (`_hz_200`): always
50 per second, at most 3 at once to catch up after a hold-up. The AES
timer is 10 ms now.

**Multitasking.**
- The VDI mouse-motion hook (`vex_motv`) that keeps the pointer inside the
  window during play is installed only on single-tasking TOS. Under MiNT,
  MagiC or Geneva (cookie `MiNT`/`MagX`/`Gnva`) it is left out: the hook
  runs in other processes' context and points into the accessory's memory
  (a crash with memory protection). There the paddle still follows the
  mouse, and the pointer is hidden only while it is over the window.
- The game pauses when its window is not the top window (it used to draw
  straight over other programs' windows). Starting the game tops the
  window.
- WM_CLOSED and AC_CLOSE stop the game first (mouse hook removed, pointer
  shown again). Before, closing left the hook in place and the pointer
  hidden.
- AC_CLOSE called `wind_close`/`wind_delete` with the **menu id** as window
  handle, which could close another program's window. Now: on
  single-tasking TOS the handle is just forgotten (the AES has deleted the
  window); on a multitasking AES the accessory's own window is deleted.
- While the game is not running there are no timer events any more (the
  original woke up 60 times a second while idle).

Unchanged: the 0.5 s pause after clearing all bricks still holds the screen
lock (`wind_update`), as before; right click stops the game, left click in
the window starts / serves.

## C version

`c/breakout.c` is a reconstruction of the whole accessory in C, written
from the disassembly routine by routine (each function names the original
symbol it replaces). Game rules, sizes per resolution, brick colours and
patterns, scoring, spin from the paddle, the paddle shrinking after the
first hit at the top, the VDI drawing order (XOR for ball and paddle) are
the original's. It includes all fixes above, plus:

- the window is only redrawn after game over if it is open (the original
  redrew through handle 0, the desktop)
- started as a program it shows an alert and ends
- the pointer-trap routine (`mtrap.S`) saves d2

Kept as in the original, although they look like slips: `PADL_TOP` is
computed before `PADL_GAP` is set; after the right button the program waits
for the right button to be *down* (it already is).

Build: `make -C c` (m68k-atari-mint GCC with GEMlib; own startup `crt.S`
from the Neko project, no MiNTLib) -> `c/bin/BREAKOUT.ACC`, 14 KB.
`make -C c measure` builds the measuring variants (`-DMEASURE`, same use as
the asm ones below).

Checked against the original in Hatari, TOS 1.04: the window in low,
medium and mono is pixel-identical; after serving, both hit the same brick
(the only differences are the ball's position by a step). Speed: 50 steps/s
on TOS 1.04, EmuTOS (Falcon), MagiC (TT) and ARAnyM/XaAES. Play test as
for the patched version. Not tested: the alert when started as a program.

## Building

`make` (needs vasm, `~/Develop/vasm-src/vasm/vasmm68k_mot`, Python 3 and
`m68k-atari-mint-objdump`):

- `tools/mksrc.py` turns `orig/BREAKOUT.ACC` into `src/breakout.s`: every
  instruction as raw words with objdump's disassembly as remark, every
  relocated address as a label (names from the program's own DRI symbol
  table). `make` checks that it rebuilds identical to the original
  (`tools/prgcmp.py`, code + data + relocations).
- With `--fix` it writes `src/breakfix.s`: the instructions listed in
  `tools/patches.py` are replaced (same length, nop padding) by calls into
  `src/fix.s`, which is appended behind the original code.

## Tests

- `make build/MEASURE.ACC` / `build/MEASORIG.ACC` (fixed / original timing):
  opens its window 12 s after reset (`-DOPEN_AT=n`), starts, serves every
  3 s and counts steps behind the marker `BKCT`. `tools/rate.sh ACC tos.img
  [hatari options]` boots it in Hatari and prints steps per second;
  `IMG=disk.img ACCDIR=AUTO/ACCS/` for a hard disk image (MagiC),
  `RETURN_AT=s` presses Return during boot. With `-DLOGFILE=1` the counts
  are written to `U:\z\brklog.bin` (ARAnyM; `tools/readlog.py`).
  `tools/vars.py ACC ramdump name...` reads variables from a RAM dump.
- `tools/hatari_run.sh name tos.img test/scripts/play.txt` plays the fixed
  accessory with Alt+cursor mouse moves (from the Neko project, plus
  `rclick`).

Tested: TOS 1.04 by hand (open, start, paddle, right click, pointer free
again). Not tested: real hardware, MiNT with memory protection on, closing
the window during play under a multitasking AES, Geneva.
