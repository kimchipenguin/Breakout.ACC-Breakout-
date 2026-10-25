|
| Breakout - minimal startup for a desk accessory that also runs as program
|
| Replaces MiNTLib's crt0, which pulls in stdio, time zones and more -
| far too much for something that stays in memory all the time.
|
| Started as accessory: a0 = basepage, the memory is already sized.
| Started as program:   4(sp) = basepage, give back what is not needed.
|
	.text
	.globl	__start
	.globl	__app
	.globl	__base

__start:
	move.l	%a0,%d0
	beq.s	prg
	move.l	%a0,__base
	lea	stack_top,%sp
	clr.w	__app
	jsr	_main
1:	bra.s	1b			| an accessory never ends

prg:
	move.l	4(%sp),%a0
	move.l	%a0,__base
	lea	stack_top,%sp
	moveq	#1,%d0
	move.w	%d0,__app
	move.l	12(%a0),%d0		| text
	add.l	20(%a0),%d0		| data
	add.l	28(%a0),%d0		| bss (includes the stack)
	add.l	#256,%d0		| basepage
	move.l	%d0,-(%sp)
	move.l	%a0,-(%sp)
	clr.w	-(%sp)
	move.w	#0x4a,-(%sp)		| Mshrink
	trap	#1
	lea	12(%sp),%sp
	jsr	_main
	move.w	%d0,-(%sp)
	move.w	#0x4c,-(%sp)		| Pterm
	trap	#1

	.data
__app:	.word	0
	.even
__base:	.long	0

	.bss
	.even
stack:	.skip	8192
stack_top:
