/*
 * breakout.c - Breakout desk accessory for the Atari ST
 *
 * C reconstruction of BREAKOUT.ACC (Alcyon C, 6 Feb 1986), written from
 * the disassembly of the original binary. Game rules, layout, colours,
 * scoring and the VDI drawing sequence follow the original routine by
 * routine; the comment above each function gives the original name (the
 * DRI symbol table keeps 8 characters).
 *
 * Differences to the original (see ../readme.md):
 * - the ball moves 50 steps a second, timed by the 200 Hz system clock,
 *   instead of one step per AES timer event (XaAES ran it ~700x too fast)
 * - multitasking: no mouse-motion vector under MiNT/MagiC/Geneva, pause
 *   when the window is not on top, clean stop on WM_CLOSED / AC_CLOSE,
 *   AC_CLOSE no longer closes the window whose handle equals the menu id,
 *   no timer events while idle
 * - after game over the window is only redrawn if it is open
 * - started as a program it says so and ends
 */

#include <stddef.h>
#include <gem.h>
#include <osbind.h>

#define ROWS        8           /* bricks */
#define COLS        14

#define TIMER_MS    10          /* evnt_multi timer (original: 16) */
#define STEP200     4           /* 200 Hz ticks per game step = 50/s */
#define MAXSTEP     3           /* catch-up limit after a hold-up */

#define EV_RUN      (MU_TIMER | MU_MESAG | MU_BUTTON)  /* original: 50 */
#define EV_IDLE     (MU_MESAG | MU_BUTTON)             /* original: 18 */

extern short _app;              /* crt.S: 1 = started as program */
extern void mouse_trap(void);   /* mtrap.S */
void *old_motv;                 /* previous motion vector (for mtrap.S) */

/* work area of the window; mtrap.S keeps the pointer inside it */
short xwork, ywork, wwork, hwork;

/* sizes, set by init_game() for the screen resolution */
static short BRICK_W, BRICK_H, GAP_WID, GAP_HI;
static short BALL_WI, BALL_HI, PADL_WI, PADL_HI, PADL_GAP;
static short BRKT_WI, BRKT_HI, ZONE_HI;    /* playfield, score zone */
static short X_MIN, X_MAX, Y_MIN, Y_MAX;
static short BRICK_TOP, PADL_TOP;
static short xvel_init, yvel_init;
static short hsqueeze, vsqueeze;   /* fewer than 320 / 200 pixels */

/* GEM */
static short handle, ap_id, gl_item, wi_hand;
static short gl_wchar, gl_hchar, gl_wbox, gl_hbox;
static short xdesk, ydesk, wdesk, hdesk;
static short msgbuff[8];
static short dummy;
static short em_flag;           /* evnt_multi events */
static short run;               /* game running (pointer caught) */
static short served;

/* game */
static short brick[ROWS][COLS], bricks, balls, score;
static char scrtxt[10];
static short one_time;
static short x_ball, y_ball, old_x, old_y, ydir;
static short xvel, yvel, xsum, ysum;  /* 1/100 pixel per step */
static short xyhit, service, doubled, br_okhit, br_bounce;
static short x_padl, old_padl, size_padl, pdx[3];

/* fixes */
static short mt;                /* MiNT / MagiC / Geneva */
static short hooked, hidden;
static long last;               /* clock at the last game step */

#ifdef MEASURE
/* Measuring build (make measure): opens its window OPEN_AT seconds after
   reset, starts, serves 3 s into each serve phase and counts steps behind
   the marker "BKCT" (written at run time, so file caches don't match).
   With LOGFILE the counts go to U:\z\brklog.bin (ARAnyM). The tools in
   ../tools read them: rate.sh, readlog.py. */
#ifndef OPEN_AT
#define OPEN_AT 12
#endif
struct { char marker[4]; long play, serve; } meas = { "bkct", 0, 0 };
static long serve_at;
static short opened;
static void open_window(void);
static void set_clip(void);
static void start_game(void);
static void measure_auto(long now);
#endif

/* ---------------------------------------------------------------- */
/* supervisor helpers */

static long read_hz200(void)
{
	return *(volatile long *)0x4baL;
}

static long hz200(void)
{
	return Supexec(read_hz200);
}

static long find_mt(void)
{
	long *jar = *(long **)0x5a0L;

	if (jar)
		for (; *jar; jar += 2)
			if (*jar == 0x4d694e54L        /* MiNT */
			 || *jar == 0x4d616758L        /* MagX */
			 || *jar == 0x476e7661L)       /* Gnva */
				return 1;
	return 0;
}

/* ---------------------------------------------------------------- */
/* drawing; coordinates relative to the work area */

/* set_cli(p) */
static void set_clip(void)
{
	short pxy[4];

	pxy[0] = xwork;
	pxy[1] = ywork;
	pxy[2] = xwork + wwork - 1;
	pxy[3] = ywork + hwork - 1;
	vs_clip(handle, 1, pxy);
}

/* box() */
static void box(short x, short y, short w, short h)
{
	short pxy[4];

	x += xwork;
	y += ywork;
	pxy[0] = x;
	pxy[1] = y;
	pxy[2] = x + w - 1;
	pxy[3] = y + h - 1;
	vr_recfl(handle, pxy);
}

static short brick_x(short col)
{
	return X_MIN + GAP_WID + (GAP_WID + BRICK_W) * col;
}

static short brick_y(short row)
{
	return Y_MIN + ZONE_HI + (GAP_HI + BRICK_H) * row;
}

/* dr_bric(k) - two rows per colour; patterned in medium and high res */
static void draw_brick(short row, short col)
{
	switch (row) {
	case 0: case 1:
		if (hsqueeze) {
			vsf_color(handle, 7);
			vsf_interior(handle, 1);
		} else {
			vsf_color(handle, 2);
			vsf_interior(handle, 2);
			vsf_style(handle, 4);
		}
		break;
	case 2: case 3:
		vsf_color(handle, 2);
		vsf_interior(handle, 1);
		break;
	case 4: case 5:
		if (hsqueeze) {
			vsf_color(handle, 4);
			vsf_interior(handle, 1);
		} else {
			vsf_color(handle, 3);
			vsf_interior(handle, 2);
			vsf_style(handle, 4);
		}
		break;
	case 6: case 7:
		vsf_color(handle, 3);
		vsf_interior(handle, 1);
		break;
	}
	vswr_mode(handle, MD_REPLACE);
	box(brick_x(col), brick_y(row), BRICK_W, BRICK_H);
}

/* er_bric(k) */
static void erase_brick(short row, short col)
{
	vswr_mode(handle, MD_REPLACE);
	vsf_color(handle, 0);
	box(brick_x(col), brick_y(row), BRICK_W, BRICK_H);
}

/* dr_ball - XOR, so drawing twice erases */
static void draw_ball(short x, short y)
{
	vswr_mode(handle, MD_XOR);
	box(x, y, BALL_WI, BALL_HI);
}

/* dr_padd(le) - XOR */
static void draw_paddle(short x)
{
	vswr_mode(handle, MD_XOR);
	box(x, Y_MAX - PADL_HI - PADL_GAP, size_padl, PADL_HI);
}

/* er_padd(le) */
static void erase_paddle(short x)
{
	vswr_mode(handle, MD_REPLACE);
	vsf_color(handle, 0);
	box(x, Y_MAX - PADL_HI - PADL_GAP, size_padl, PADL_HI);
}

/* draw_br(icks) */
static void draw_bricks(void)
{
	short row, col;

	for (col = 0; col < COLS; col++)
		for (row = 0; row < ROWS; row++)
			if (brick[row][col])
				draw_brick(row, col);
	vsf_interior(handle, 1);
}

/* show_li(ves) - spare balls as small balls, top left */
static void show_lives(void)
{
	short i, j, spare = balls;

	vswr_mode(handle, MD_REPLACE);
	vsf_color(handle, 0);
	box(0, 0, BALL_WI * 5, gl_hchar - 1);
	for (i = 0; i < 2; i++)
		for (j = 0; j < 2; j++)
			if (spare > 1) {
				spare--;
				draw_ball(BALL_WI * 3 / 2 * i + BALL_WI / 2 + 1,
				          BALL_HI * 3 / 2 * j + BALL_HI / 2 + 1);
			}
}

/* convert() - score to 8 characters, leading blanks */
static void convert(void)
{
	char digit[8];
	short n = score, i, blank = 1;

	for (i = 0; i < 8; i++) {
		digit[i] = n % 10;
		n /= 10;
	}
	for (i = 7; i > 0; i--) {
		if (digit[i])
			blank = 0;
		scrtxt[7 - i] = blank ? ' ' : '0' + digit[i];
	}
	scrtxt[7] = '0' + digit[0];
	scrtxt[8] = 0;
}

/* show_sc(ore) */
static void show_score(void)
{
	vst_color(handle, vsqueeze ? 2 : 1);
	vswr_mode(handle, MD_REPLACE);
	v_gtext(handle, xwork + (BRKT_WI - 64) / 2, ywork + 1, scrtxt);
}

/* redraw_() - the whole playfield, clipped by the caller */
static void redraw(void)
{
	vswr_mode(handle, MD_REPLACE);
	vsf_color(handle, 0);
	box(0, 0, BRKT_WI, Y_MAX);
	vsf_color(handle, 1);
	box(0, Y_MIN, BRKT_WI, 1);          /* line under the score */
	show_lives();
	show_score();
	draw_bricks();
	draw_paddle(x_padl);
	draw_ball(x_ball, y_ball);
}

/* rc_inte(rsect) */
static short rc_intersect_(const GRECT *a, GRECT *b)
{
	short tx = a->g_x > b->g_x ? a->g_x : b->g_x;
	short ty = a->g_y > b->g_y ? a->g_y : b->g_y;
	short tw = a->g_x + a->g_w < b->g_x + b->g_w ?
	           a->g_x + a->g_w : b->g_x + b->g_w;
	short th = a->g_y + a->g_h < b->g_y + b->g_h ?
	           a->g_y + a->g_h : b->g_y + b->g_h;

	b->g_x = tx;
	b->g_y = ty;
	b->g_w = tw - tx;
	b->g_h = th - ty;
	return tw > tx && th > ty;
}

/* do_redr(aw) - walk the rectangle list */
static void do_redraw(short x, short y, short w, short h)
{
	GRECT area, r;
	short pxy[4];

	graf_mouse(M_OFF, NULL);
	area.g_x = x;
	area.g_y = y;
	area.g_w = w;
	area.g_h = h;
	wind_get(wi_hand, WF_FIRSTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
	while (r.g_w && r.g_h) {
		if (rc_intersect_(&area, &r)) {
			pxy[0] = r.g_x;
			pxy[1] = r.g_y;
			pxy[2] = r.g_x + r.g_w - 1;
			pxy[3] = r.g_y + r.g_h - 1;
			vs_clip(handle, 1, pxy);
			redraw();
		}
		wind_get(wi_hand, WF_NEXTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
	}
	graf_mouse(M_ON, NULL);
	set_clip();
}

/* ---------------------------------------------------------------- */
/* setup */

/* open_vw(ork) */
static void open_vwork(void)
{
	short work_in[11], work_out[57], i;

	for (i = 0; i < 10; i++)
		work_in[i] = 1;
	work_in[10] = 2;
	v_opnvwk(work_in, &handle, work_out);
	hsqueeze = work_out[0] < 320;
	vsqueeze = work_out[1] < 200;
}

/* init_ga(me) - sizes for the resolution (low res: hsqueeze, low and
   medium: vsqueeze) */
static void init_game(void)
{
	if (hsqueeze) {
		BRICK_W = 6;  GAP_WID = 1; BALL_WI = 2; PADL_WI = 12;
		X_MIN = 0;    xvel_init = 50;
	} else {
		BRICK_W = 12; GAP_WID = 2; BALL_WI = 4; PADL_WI = 24;
		X_MIN = 0;    xvel_init = 100;
	}
	if (vsqueeze) {
		BRICK_H = 3;   GAP_HI = 1;   BALL_HI = 2; PADL_HI = 1;
		BRKT_HI = 120; ZONE_HI = 18; Y_MIN = 9;   yvel_init = 150;
	} else {
		BRICK_H = 6;   GAP_HI = 2;   BALL_HI = 4; PADL_HI = 2;
		BRKT_HI = 240; ZONE_HI = 36; Y_MIN = 17;  yvel_init = 300;
	}
	BRKT_WI = BRICK_W * COLS + GAP_WID * (COLS + 1);
	X_MAX = BRKT_WI - BALL_WI;
	Y_MAX = BRKT_HI;
	BRICK_TOP = Y_MIN + ZONE_HI - BALL_HI;
	/* as in the original: PADL_GAP is still 0 here */
	PADL_TOP = Y_MAX - (BALL_HI + PADL_HI + PADL_GAP);
	PADL_GAP = PADL_HI * 2;
}

/* bricks_() */
static void init_bricks(void)
{
	short row, col;

	bricks = ROWS * COLS;
	for (row = 0; row < ROWS; row++)
		for (col = 0; col < COLS; col++)
			brick[row][col] = 1;
}

/* init_br(eak) - new game */
static void init_break(void)
{
	balls = 5;
	size_padl = PADL_WI;
	x_padl = old_padl = (BRKT_WI - size_padl) / 2;
	init_bricks();
}

/* open_wi(ndow) - centred on the desktop */
static void open_window(void)
{
	short xw, yw, ww, hw;

	wi_hand = wind_create(NAME | CLOSER | MOVER, xdesk, ydesk, wdesk, hdesk);
	wind_set_str(wi_hand, WF_NAME, "  BREAKOUT ");
	wind_calc(WC_BORDER, NAME | CLOSER | MOVER, xdesk, ydesk, BRKT_WI,
	          BRKT_HI, &xw, &yw, &ww, &hw);
	xw += (wdesk - ww) / 2;
	yw += (hdesk - hw) / 2;
	wind_open(wi_hand, xw, yw, ww, hw);
}

/* ---------------------------------------------------------------- */
/* game */

/* scored() - points by row, doubled at high speed; speed doubles the
   first time one of the upper four rows is hit */
static void scored(short row)
{
	short points;

	if (!doubled && row < 4) {
		doubled = 1;
		xvel <<= 1;
		yvel <<= 1;
	}
	points = (7 - row) | 1;
	if (!br_bounce)
		points++;
	if (xvel > 200 || -xvel > 200)
		points <<= 1;
	score += points;
	convert();
	show_score();
}

/* move_pa(ddle) - the paddle follows the mouse */
static void move_paddle(void)
{
	short status, x, y;
	short maxoff;

	vq_mouse(handle, &status, &x, &y);
	maxoff = BRKT_WI - size_padl;
	if (x < xwork)
		x = xwork;
	if (xwork + maxoff < x)
		x = xwork + maxoff;
	x_padl = x - xwork;
	draw_paddle(old_padl);
	draw_paddle(x_padl);
	pdx[2] = pdx[1];
	pdx[1] = pdx[0];
	pdx[0] = x_padl - old_padl;
	old_padl = x_padl;
}

/* bounce() - before the serve the ball hops in the score zone */
static void bounce(void)
{
	draw_ball(x_ball, y_ball);
	if (y_ball == 0 || y_ball == Y_MIN - BALL_HI)
		ydir = -ydir;
	y_ball += ydir;
	draw_ball(x_ball, y_ball);
}

/* move_ba(ll) - velocities in 1/100 pixel; ydir: 1 down, 0 up */
static void move_ball(void)
{
	short over_paddle;

	xsum += xvel;
	while (xsum > 100) {
		xsum -= 100;
		if (x_ball <= X_MAX)
			x_ball++;
	}
	while (xsum < -100) {
		xsum += 100;
		if (x_ball >= X_MIN)
			x_ball--;
	}
	over_paddle = x_ball - x_padl < size_padl && x_ball - x_padl > -BALL_WI;
	xyhit = 0;
	ysum += yvel;
	while (ysum > 100) {
		if (ydir)
			y_ball++;
		else
			y_ball--;
		if (y_ball >= PADL_TOP && PADL_TOP + BALL_HI > y_ball
		 && over_paddle) {
			xyhit = 1;
			break;
		}
		ysum -= 100;
	}
	draw_ball(old_x, old_y);
	draw_ball(x_ball, y_ball);
	old_x = x_ball;
	old_y = y_ball;
}

/* hit_top() - the paddle shrinks to 3/4 the first time */
static void hit_top(void)
{
	if (size_padl == PADL_WI) {
		draw_paddle(old_padl);
		size_padl = PADL_WI * 3 / 4;
		draw_paddle(old_padl);
	}
	br_okhit = 1;
	br_bounce = 0;
	ydir = 1;
	move_ball();
}

/* hit_pad(dle) - the paddle's last three moves add spin */
static void hit_paddle(void)
{
	xvel += pdx[0] * 6 + pdx[1] * 3 + pdx[2];
	ydir = 0;
	br_okhit = 1;
	br_bounce = 1;
	service = 0;
	draw_ball(x_ball, y_ball);
	y_ball = Y_MAX - PADL_HI - PADL_GAP - BALL_HI;
	draw_ball(x_ball, y_ball);
	old_x = x_ball;
	old_y = y_ball;
}

/* hit_bri(ck) - one brick per paddle or top contact */
static void hit_brick(short row, short col)
{
	if (brick[row][col] && !service && br_okhit) {
		br_okhit = 0;
		brick[row][col] = 0;
		bricks--;
		scored(row);
		ydir = br_bounce;
		draw_ball(x_ball, y_ball);
		erase_brick(row, col);
		draw_ball(x_ball, y_ball);
	}
}

/* collisi(on) */
static void collision(void)
{
	short row, col;

	if (x_ball < X_MIN) {
		if (xvel < 0)
			xvel = -xvel;
		move_ball();
	}
	if (x_ball > X_MAX) {
		if (xvel > 0)
			xvel = -xvel;
		move_ball();
	}
	if (xyhit)
		hit_paddle();
	if (y_ball < Y_MIN && !service)
		hit_top();
	row = y_ball - BRICK_TOP;
	if (row > 0) {
		row /= GAP_HI + BRICK_H;
		if (row < ROWS) {
			col = (x_ball - X_MIN) / (GAP_WID + BRICK_W);
			if (col < COLS)
				hit_brick(row, col);
		}
	}
}

/* game_ta(sk) - one step with the ball in play. With all bricks gone
   every spare ball gives 10 points, then the game ends. */
static void game_step(void)
{
	move_paddle();
	if (bricks) {
		move_ball();
		collision();
	} else if (--balls) {
		evnt_timer(500);
		show_lives();
		score += 10;
		convert();
		show_score();
	} else {
		balls++;
		draw_ball(x_ball, y_ball);
		y_ball = Y_MAX + 16;            /* out: ends play() */
		draw_ball(x_ball, y_ball);
	}
}

/* ---------------------------------------------------------------- */
/* start / stop, timing (new) */

static void hide_pointer(void)
{
	if (!hidden) {
		hidden = 1;
		graf_mouse(M_OFF, NULL);
	}
}

static void show_pointer(void)
{
	if (hidden) {
		hidden = 0;
		graf_mouse(M_ON, NULL);
	}
}

/* left click in the window. On single-tasking TOS a mouse-motion vector
   keeps the pointer in the window, as in the original. */
static void start_game(void)
{
	if (!mt && !hooked) {
		vex_motv(handle, (void *)mouse_trap, &old_motv);
		hooked = 1;
	}
	wind_set(wi_hand, WF_TOP, 0, 0, 0, 0);
	hide_pointer();
	em_flag = EV_RUN;
	run = 1;
	last = hz200();
}

/* right button, window covered or closed */
static void pause_game(void)
{
	void *dummy_motv;

	if (hooked) {
		vex_motv(handle, old_motv, &dummy_motv);
		hooked = 0;
	}
	show_pointer();
	run = 0;
	em_flag = EV_IDLE;
}

/* multitasking: no mouse trap, so hide the pointer only over the window */
static void track_pointer(void)
{
	short status, x, y;

	vq_mouse(handle, &status, &x, &y);
	if (x >= xwork && x < xwork + wwork && y >= ywork && y < ywork + hwork)
		hide_pointer();
	else
		show_pointer();
}

/* game steps due by the clock */
static short steps_due(void)
{
	long now = hz200(), n = now - last;

	if (n >= STEP200 * MAXSTEP) {   /* held up: no rush */
		last = now;
		return MAXSTEP;
	}
	n /= STEP200;
	last += n * STEP200;
	return n;
}

/* timer event while running */
static void tick(short serving)
{
	short top, n;

	if (wind_get(0, WF_TOP, &top, &dummy, &dummy, &dummy) && top != wi_hand) {
		pause_game();
		return;
	}
	if (mt)
		track_pointer();
	for (n = steps_due(); n > 0; n--) {
#ifdef MEASURE
		if (serving) meas.serve++; else meas.play++;
#endif
		if (serving) {
			move_paddle();
			bounce();
		} else {
			game_step();
			if (y_ball >= Y_MAX)
				break;
		}
	}
}

/* ---------------------------------------------------------------- */
/* events */

static short in_work(short x, short y)
{
	return x < xwork + wwork && x >= xwork && y < ywork + hwork && y >= ywork;
}

/* multi0 (serving = 0: until the ball is lost) and multi1 (serving = 1:
   until a click serves) */
static void events(short serving)
{
	short ev, mx, my, mb, d;

	served = 0;
	while (serving ? !served : y_ball < Y_MAX) {
		if (serving)
			em_flag = run ? EV_RUN : EV_IDLE;
#ifdef MEASURE
		if (serving) {
			measure_auto(hz200());
			em_flag = EV_RUN;
		}
#endif
		if (run) {
			vq_mouse(handle, &mb, &mx, &my);
			if (mb & RIGHT_BUTTON) {
				pause_game();
				evnt_button(1, RIGHT_BUTTON, RIGHT_BUTTON, &d, &d, &d, &d);
			}
		}
		ev = evnt_multi(em_flag, 1, LEFT_BUTTON, LEFT_BUTTON,
		                0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		                msgbuff, TIMER_MS, &mx, &my, &d, &d, &d, &d);
		wind_update(BEG_UPDATE);

		if (ev & MU_MESAG)
			switch (msgbuff[0]) {
			case WM_REDRAW:
				if (wi_hand)
					do_redraw(msgbuff[4], msgbuff[5], msgbuff[6], msgbuff[7]);
				break;
			case WM_TOPPED:
				if (msgbuff[3] != wi_hand)
					pause_game();
				wind_set(msgbuff[3], WF_TOP, 0, 0, 0, 0);
				break;
			case WM_CLOSED:
				pause_game();
				wind_close(msgbuff[3]);
				wind_delete(msgbuff[3]);
				wi_hand = 0;
				break;
			case WM_MOVED:
				wind_set(msgbuff[3], WF_CURRXYWH, msgbuff[4], msgbuff[5],
				         msgbuff[6], msgbuff[7]);
				wind_get(msgbuff[3], WF_WORKXYWH, &xwork, &ywork, &wwork, &hwork);
				set_clip();
				break;
			case AC_OPEN:
				if (msgbuff[4] == gl_item && !wi_hand) {
					open_window();
					wind_get(wi_hand, WF_WORKXYWH, &xwork, &ywork, &wwork, &hwork);
					set_clip();
				} else
					wind_set(wi_hand, WF_TOP, 0, 0, 0, 0);
				break;
			case AC_CLOSE:
				/* single-tasking TOS has deleted our window already */
				if (msgbuff[3] == gl_item && wi_hand) {
					pause_game();
					if (mt) {
						wind_close(wi_hand);
						wind_delete(wi_hand);
					}
					wi_hand = 0;
				}
				break;
			}

		if ((ev & MU_TIMER) && run)
			tick(serving);

		if ((ev & MU_BUTTON) && wi_hand) {
			if (!run) {
				vq_mouse(handle, &mb, &mx, &my);
				if (in_work(mx, my))
					start_game();
			} else if (serving)
				served = 1;
		}
		wind_update(END_UPDATE);
	}
	if (!serving) {
		balls--;
		draw_ball(x_ball, y_ball);
	}
}

#ifdef MEASURE
#ifdef LOGFILE
static long log_t0, log_buf[6];
static short log_done;

static void measure_log(long now)
{
	short fh;

	if (log_done)
		return;
	if (!log_buf[0]) {
		if (!log_t0)
			log_t0 = now;
		else if (now >= log_t0 + 1000) {
			log_buf[0] = now;
			log_buf[1] = meas.play;
			log_buf[2] = meas.serve;
		}
	} else if (now >= log_buf[0] + 2000) {
		log_buf[3] = now;
		log_buf[4] = meas.play;
		log_buf[5] = meas.serve;
		log_done = 1;
		fh = Fcreate("U:\\z\\brklog.bin", 0);
		if (fh >= 0) {
			Fwrite(fh, sizeof log_buf, log_buf);
			Fclose(fh);
		}
	}
}
#endif

static void measure_auto(long now)
{
	if (!opened) {
		if (now < 200L * OPEN_AT)
			return;
		open_window();
		wind_get(wi_hand, WF_WORKXYWH, &xwork, &ywork, &wwork, &hwork);
		set_clip();
		opened = 1;
		meas.marker[0] = 'B'; meas.marker[1] = 'K';
		meas.marker[2] = 'C'; meas.marker[3] = 'T';
		start_game();
		return;
	}
#ifdef LOGFILE
	measure_log(now);
#endif
	if (!run)
		return;
	if (!serve_at)
		serve_at = now + 600;
	else if (now >= serve_at) {
		serve_at = 0;
		served = 1;
	}
}
#endif

/* serve() - the ball hops in the score zone until a click serves it */
static void serve(void)
{
	if (size_padl != PADL_WI) {
		erase_paddle(x_padl);
		size_padl = PADL_WI;
		draw_paddle(x_padl);
	}
	x_ball = BALL_WI * 3 + BALL_WI / 2 + 1;
	y_ball = 1;
	ydir = 1;
	if (!one_time) {
		show_lives();
		draw_ball(x_ball, y_ball);
	}
	one_time = 0;
	events(1);
	old_x = x_ball;
	old_y = y_ball;
	ydir = 1;
	ysum = xsum = 0;
	xvel = xvel_init;
	yvel = yvel_init;
	doubled = 0;
	service = 1;
	if (balls == 5) {
		score = 0;
		convert();
		show_score();
	}
}

/* GCC calls __main() from main() for constructors; there are none, and
   libgcc's version would pull in atexit() */
void __main(void)
{
}

int main(void)
{
	ap_id = appl_init();
	if (_app) {
		form_alert(1, "[1][BREAKOUT is a desk accessory.|"
		              "Rename it to BREAKOUT.ACC and|"
		              "copy it to the boot drive.][ OK ]");
		appl_exit();
		return 0;
	}
	handle = graf_handle(&gl_wchar, &gl_hchar, &gl_wbox, &gl_hbox);
	open_vwork();
	mt = Supexec(find_mt);
	gl_item = menu_register(ap_id, "     BREAKOUT");
	wind_get(0, WF_WORKXYWH, &xdesk, &ydesk, &wdesk, &hdesk);
	graf_mouse(ARROW, NULL);
	wi_hand = 0;
	vst_alignment(handle, 0, 5, &dummy, &dummy);    /* left, top */
	init_game();
	init_break();
	score = 0;
	convert();
	run = 0;
	one_time = 1;
	for (;;) {
		while (balls) {
			serve();
			events(0);
		}
		init_break();                   /* game over: new bricks */
		if (wi_hand) {
			wind_update(BEG_UPDATE);
			do_redraw(xwork, ywork, wwork, hwork);
			wind_update(END_UPDATE);
		}
	}
}
