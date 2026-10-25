; fix.s - code added behind the original BREAKOUT.ACC text
;
; Called from the patched spots listed in tools/patches.py. Calls into the
; original program use its Alcyon C convention: arguments pushed right to
; left (int = word), caller removes them, result in d0, d0-d2/a0-a2 free.
;
; 1. Speed: the original moved the ball one step per AES timer event. Old
;    TOS rounds the 16 ms timer to its 20 ms tick, so a 1986 ST ran 50 steps
;    a second (measured in Hatari: ST low and mono). Faster AESes deliver
;    the timer sooner. Now the steps are counted off the 200 Hz system
;    clock: always 50 a second, at most MAXSTEP at once.
; 2. Multitasking:
;    - the VDI mouse-motion hook that keeps the pointer in the window is
;      only installed on single-tasking TOS (under MiNT/MagiC the hook would
;      run in other processes' context and point into our memory);
;      instead the pointer is hidden only while it is over the window
;    - the game pauses as soon as its window is not the top window
;    - WM_CLOSED / AC_CLOSE stop the game (unhook, pointer back) first
;    - AC_CLOSE closed the window whose handle equalled the menu id
;    - no timer events while the game is not running

	ifd	ORIGTIMING
TIMER_MS = 16		; measuring the original timing: one step per event
	else
TIMER_MS = 10		; evnt_multi timer (was 16)
	endif
STEP200	= 4		; 200 Hz ticks per game step (= 50 steps a second)
MAXSTEP	= 3		; catch-up limit after a hold-up
WF_TOP	= 10

	section text

; main: after open_vwork()
init_fix:
	jsr	_open_vw
	pea	find_mt
	move.w	#38,-(sp)
	trap	#14			; Supexec
	addq.l	#6,sp
	move.w	d0,mt
	rts

; (supervisor) d0 = 1 if the cookie jar names a multitasking system
find_mt:
	move.l	$5a0.w,d0
	beq.s	.no
	move.l	d0,a0
.next:	move.l	(a0),d1
	beq.s	.no
	cmp.l	#'MiNT',d1
	beq.s	.yes
	cmp.l	#'MagX',d1
	beq.s	.yes
	cmp.l	#'Gnva',d1
	beq.s	.yes
	addq.l	#8,a0
	bra.s	.next
.no:	moveq	#0,d0
	rts
.yes:	moveq	#1,d0
	rts

; d0 = 200 Hz system clock
now200:
	pea	.get
	move.w	#38,-(sp)
	trap	#14			; Supexec
	addq.l	#6,sp
	rts
.get:	move.l	$4ba.w,d0
	rts

; left click in the window: start (or go on with) the game
start_game:
	tst.w	mt
	bne.s	.nohook
	tst.w	hooked
	bne.s	.nohook
	pea	_old_cod		; vex_motv(handle, mouse_trap, &old_code)
	pea	_mouse_t
	move.w	_handle,-(sp)
	jsr	_vex_mot
	lea	10(sp),sp
	move.w	#1,hooked
.nohook:
	clr.l	-(sp)			; wind_set(wi_hand, WF_TOP, 0, 0, 0, 0):
	clr.l	-(sp)			; playing means being the top window
	move.w	#WF_TOP,-(sp)
	move.w	_wi_hand,-(sp)
	jsr	_wind_se
	lea	12(sp),sp
	bsr	hide_ptr
	move.w	#50,_em_flag		; MU_TIMER | MU_MESAG | MU_BUTTON
	move.w	#1,_run
	bsr	now200
	move.l	d0,last
	rts

; stop the game: right button, window no longer on top, closed
pause_game:
	tst.w	hooked
	beq.s	.nohook
	pea	_dummy			; vex_motv(handle, old_code, &dummy)
	move.l	_old_cod,-(sp)
	move.w	_handle,-(sp)
	jsr	_vex_mot
	lea	10(sp),sp
	clr.w	hooked
.nohook:
	bsr	show_ptr
	clr.w	_run
	move.w	#18,_em_flag		; MU_MESAG | MU_BUTTON
	rts

hide_ptr:
	tst.w	hidden
	bne.s	.x
	move.w	#1,hidden
	moveq	#0,d0			; M_OFF
	bra.s	graf_mouse
.x:	rts
show_ptr:
	tst.w	hidden
	beq.s	.x
	clr.w	hidden
	moveq	#1,d0			; M_ON
	bra.s	graf_mouse
.x:	rts
graf_mouse:
	clr.l	-(sp)
	add.w	#256,d0
	move.w	d0,-(sp)
	jsr	_graf_mo
	addq.l	#6,sp
	rts

; WM_CLOSED: stop the game, then the replaced "msgbuff[3] -> stack"
wm_closed:
	bsr	pause_game
	move.w	_msgbuff+6,4(sp)
	rts

; AC_CLOSE. On single-tasking TOS the AES has already deleted our window;
; a multitasking AES may not have, so delete it there - by its handle, not
; by the menu id as the original did.
ac_close:
	move.w	_msgbuff+6,d0
	cmp.w	_gl_item,d0
	bne.s	.x
	tst.w	_wi_hand
	beq.s	.x
	bsr	pause_game
	tst.w	mt
	beq.s	.gone
	move.w	_wi_hand,-(sp)
	jsr	_wind_cl
	move.w	_wi_hand,(sp)
	jsr	_wind_de
	addq.l	#2,sp
.gone:	clr.w	_wi_hand
.x:	rts

; multi1 loop start: timer only while running
set_flags:
	ifd	MEASURE
	bsr	m_auto
	move.w	#50,_em_flag
	rts
	endif
	move.w	#18,_em_flag
	tst.w	_run
	beq.s	.x
	move.w	#50,_em_flag
.x:	rts

; timer event while the ball is in play (was: game_ta())
tick_play:
	bsr	tick_common
.step:	subq.w	#1,steps
	bmi.s	.x
	ifd	MEASURE
	addq.l	#1,cnt_play
	endif
	jsr	_game_ta
	move.w	_y_ball,d0		; ball lost: back to multi0's loop test
	cmp.w	_Y_MAX,d0
	blt.s	.step
.x:	rts

; timer event while waiting for the serve (was: move_paddle(); bounce())
tick_serve:
	bsr	tick_common
.step:	subq.w	#1,steps
	bmi.s	.x
	ifd	MEASURE
	addq.l	#1,cnt_serve
	endif
	jsr	_move_pa
	jsr	_bounce
	bra.s	.step
.x:	rts

; pause if our window is not on top; steps = game steps due now
tick_common:
	clr.w	steps
	pea	top_dummy		; wind_get(0, WF_TOP, &top, ...)
	pea	top_dummy
	pea	top_dummy
	pea	top_win
	move.w	#WF_TOP,-(sp)
	clr.w	-(sp)
	jsr	_wind_ge
	lea	20(sp),sp
	tst.w	d0
	beq.s	.ontop			; call failed: assume we are
	move.w	top_win,d0
	cmp.w	_wi_hand,d0
	beq.s	.ontop
	bra	pause_game
.ontop:	tst.w	mt
	beq.s	.time
	bsr	track_ptr
.time:
	ifd	ORIGTIMING
	move.w	#1,steps
	rts
	endif
	bsr	now200
	move.l	d0,d1
	sub.l	last,d0
	cmp.l	#STEP200*MAXSTEP,d0
	blo.s	.due
	move.l	d1,last			; held up (or clock wrapped): no rush
	move.w	#MAXSTEP,steps
	rts
.due:	lsr.w	#2,d0			; / STEP200
	move.w	d0,steps
	lsl.w	#2,d0
	ext.l	d0
	add.l	d0,last
	rts

; multitasking: no mouse trap, so hide the pointer only over the window
track_ptr:
	pea	m_y			; vq_mouse(handle, &buttons, &x, &y)
	pea	m_x
	pea	m_b
	move.w	_handle,-(sp)
	jsr	_vq_mous
	lea	14(sp),sp
	move.w	m_x,d0
	sub.w	_xwork,d0
	bmi	show_ptr
	cmp.w	_wwork,d0
	bge	show_ptr
	move.w	m_y,d0
	sub.w	_ywork,d0
	bmi	show_ptr
	cmp.w	_hwork,d0
	bge	show_ptr
	bra	hide_ptr

	ifd	MEASURE
	ifnd	OPEN_AT
OPEN_AT	= 12		; seconds after reset (later on MagiC: its desktop
	endif		; opens a drive window over ours)
; --- measuring build: opens the window 12 s after reset, starts, serves
; 3 s after each start of the serve phase, counts game steps (search RAM
; for 'BKCT')
m_auto:	bsr	now200
	tst.w	opened
	bne.s	.serve
	cmp.l	#200*OPEN_AT,d0
	blo	.x
	jsr	_open_wi
	pea	_hwork
	pea	_wwork
	pea	_ywork
	pea	_xwork
	move.w	#4,-(sp)
	move.w	_wi_hand,-(sp)
	jsr	_wind_ge
	lea	20(sp),sp
	jsr	_set_cli
	move.w	#1,opened
	move.l	#'BKCT',marker
	bra	start_game
.serve:
	ifd	LOGFILE
	bsr	m_log
	endif
	ifd	NOSERVE
	rts
	endif
	tst.w	_run
	beq.s	.x
	tst.l	serve_at
	bne.s	.wait
	add.l	#600,d0
	move.l	d0,serve_at
	bra.s	.x
.wait:	cmp.l	serve_at,d0
	blo	.x
	clr.l	serve_at
	move.w	#1,_served
.x:	rts

	ifd	LOGFILE
; LOGFILE: 5 s after the start note clock + counters, 10 s later again,
; then write both to U:\z\brklog.bin (for emulators without a debugger
; channel, e.g. ARAnyM)
m_log:	movem.l	d0-d2/a0-a2,-(sp)
	move.l	d0,d2			; clock
	tst.w	log_done
	bne	.x
	lea	log_buf,a0
	tst.l	(a0)
	bne.s	.second
	tst.l	log_t0
	bne.s	.w1
	move.l	d2,log_t0
	bra	.x
.w1:	move.l	log_t0,d1
	add.l	#1000,d1
	cmp.l	d1,d2
	blo	.x
	move.l	d2,(a0)+
	move.l	cnt_play,(a0)+
	move.l	cnt_serve,(a0)+
	bra	.x
.second: move.l	(a0),d1
	add.l	#2000,d1
	cmp.l	d1,d2
	blo	.x
	move.l	d2,12(a0)
	move.l	cnt_play,16(a0)
	move.l	cnt_serve,20(a0)
	move.w	#1,log_done
	clr.w	-(sp)
	pea	log_name
	move.w	#$3c,-(sp)		; Fcreate
	trap	#1
	addq.l	#8,sp
	move.w	d0,d1
	bmi	.x
	move.w	d1,-(sp)		; for Fclose
	pea	log_buf
	move.l	#24,-(sp)
	move.w	d1,-(sp)
	move.w	#$40,-(sp)		; Fwrite
	trap	#1
	lea	12(sp),sp
	move.w	#$3e,-(sp)		; Fclose
	trap	#1
	addq.l	#4,sp
.x:	movem.l	(sp)+,d0-d2/a0-a2
	rts
	endif

	section data
	ifd	LOGFILE
log_name: dc.b	'U:\z\brklog.bin',0
	even
	endif
marker:	dc.b	'bkct'		; becomes 'BKCT' when running (not in caches)
cnt_play: dc.l	0
cnt_serve: dc.l	0
	section bss
serve_at: ds.l	1
	ifd	LOGFILE
log_t0:	ds.l	1
log_buf: ds.l	6
log_done: ds.w	1
	endif
opened:	ds.w	1
	endif

	section bss
last:	ds.l	1		; clock at the last game step
mt:	ds.w	1		; 1 = MiNT / MagiC / Geneva
hooked:	ds.w	1		; mouse trap installed
hidden:	ds.w	1		; we hid the pointer
steps:	ds.w	1
top_win: ds.w	1
top_dummy: ds.w	1
m_b:	ds.w	1
m_x:	ds.w	1
m_y:	ds.w	1
