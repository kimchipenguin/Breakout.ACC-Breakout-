# Instructions replaced in the fixed build (bin/BREAKOUT.ACC):
#   address -> (bytes replaced, new lines)
# A replacement must be exactly as long as the bytes it replaces; nops
# fill the rest. The new routines are in src/fix.s.

def fill(length, *lines):
    """lines (sizes given as the second item of each tuple) + nop padding"""
    used = sum(n for _, n in lines)
    assert used <= length and (length - used) % 2 == 0
    out = [l for l, _ in lines]
    if length > used:
        out.append('dcb.w %d,$4e71' % ((length - used) // 2))
    return (length, out)

JSR = 6   # jsr abs.l
BSR = 4   # bsr.w

PATCHES = {
    # main: open_vwork() -> also find out whether a multitasking AES runs
    0x0154: fill(4, ('bsr.w init_fix', BSR)),

    # --- multi0 (ball in play)
    # right button: stop the game
    0x06c6: fill(54, ('jsr pause_game', JSR)),
    # evnt_multi timer
    0x0752: fill(4, ('move.w #TIMER_MS,-(sp)', 4)),
    # WM_TOPPED for a window that is not ours: stop properly
    0x07e8: fill(6, ('jsr pause_game', JSR)),
    # WM_CLOSED: stop the game first
    0x0810: fill(6, ('jsr wm_closed', JSR)),
    # AC_CLOSE: window handle bug, stop the game first
    0x0904: fill(52, ('jsr ac_close', JSR)),
    # timer event while running: game_ta() -> as many steps as are due
    0x0962: fill(4, ('bsr.w tick_play', BSR)),
    # left click in the window: start the game
    0x09ec: fill(56, ('jsr start_game', JSR)),

    # --- multi1 (waiting for the serve)
    # timer only while running (the original woke 60 times a second idle)
    0x0a66: fill(8, ('jsr set_flags', JSR)),
    0x0aa6: fill(54, ('jsr pause_game', JSR)),
    0x0b32: fill(4, ('move.w #TIMER_MS,-(sp)', 4)),
    0x0bc8: fill(6, ('jsr pause_game', JSR)),
    0x0bf0: fill(6, ('jsr wm_closed', JSR)),
    0x0ce4: fill(52, ('jsr ac_close', JSR)),
    # timer event while running: move_paddle(); bounce() -> steps due
    0x0d42: fill(8, ('bsr.w tick_serve', BSR)),
    0x0dd0: fill(56, ('jsr start_game', JSR)),
}
