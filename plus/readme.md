# Breakout+

Extended version of the Breakout desk accessory (Matt Householder, Atari
Corp., 1985/86), built from the C reconstruction in `../c`. It is a separate
accessory (`BRKPLUS.ACC`, menu entry "Breakout+", high score file
`BRKPLUS.INF`), so it can be installed next to the classic one.

- `BRKPLUS.ACC` – the accessory (68000; ST, STE, TT, Falcon, emulators)
- `source/` – C source and Makefile
- `test/AUTOTEST.ACC` – scripted test build (`make autotest`)

## Goal

A minimal modernisation of the old Breakout accessory: run as an application,
optional scaling, iconify support, more colors if supported and high score
save. Finally I wanted to credit the original Breakout.acc develop, because
his name is nowhere in the original binary.

No attempt has been made to turn it into the best Breakout game for the
Atari ST. Atari themselves already did that with Breakout (1992), which
offered better graphics and sound samples.