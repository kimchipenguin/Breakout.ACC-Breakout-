|
| Breakout - VDI mouse-motion vector (vex_motv), single-tasking TOS only
|
| Called by the VDI with the new pointer position in d0/d1. Keeps it in
| the window's work area, then goes on to the previous vector - the
| original's _mouse_t. d2 is saved here (the original did not).
|
	.text
	.globl	_mouse_trap

_mouse_trap:
	move.l	%d2,-(%sp)
	move.w	_xwork,%d2
	cmp.w	%d2,%d0
	blt.s	1f
	add.w	_wwork,%d2
	subq.w	#1,%d2
	cmp.w	%d2,%d0
	ble.s	2f
1:	move.w	%d2,%d0
2:	move.w	_ywork,%d2
	cmp.w	%d2,%d1
	blt.s	3f
	add.w	_hwork,%d2
	subq.w	#1,%d2
	cmp.w	%d2,%d1
	ble.s	4f
3:	move.w	%d2,%d1
4:	move.l	(%sp)+,%d2
	move.l	_old_motv,-(%sp)
	rts
