/*
 * brkplus.c - Breakout+ for the Atari ST and later, desk accessory or
 *              application
 *
 * Extended version of the 1986 Breakout accessory (original by Matt
 * Householder, Atari Corp.), based on the C reconstruction in ../../c.
 * The same file runs as desk accessory (BRKPLUS.ACC) and as application
 * (BRKPLUS.PRG / .APP: own menu bar, quits with the close box or ^Q).
 * The game itself - rules, speed, sizes in logical pixels - is the
 * original's. New:
 *
 * - the window can be resized (and fulled) while the game is paused; the
 *   playfield keeps its aspect ratio, everything is drawn scaled
 * - iconify (AESes that have it): the icon shows the game as a still
 *   miniature
 * - 8 bit and more: rainbow gradient from top to bottom over the bricks
 * - high score, saved in BRKPLUS.INF
 * - between games the free space under the bricks shows the credits and
 *   the high score
 *
 * Coordinates: the game works in the original's pixels ("logical"); sx()
 * and sy() turn them into screen pixels with the current scale (8.8 fixed
 * point, 256 = original size) and origin.
 */

#include <stddef.h>
#include <gem.h>
#include <osbind.h>

#define ROWS        8           /* bricks */
#define COLS        14

#define TIMER_MS    10          /* evnt_multi timer */
#define STEP200     4           /* 200 Hz ticks per game step = 50/s */
#define MAXSTEP     3           /* catch-up limit after a hold-up */

#define EV_RUN      (MU_TIMER | MU_MESAG | MU_BUTTON | ev_key)
#define EV_IDLE     (MU_MESAG | MU_BUTTON | ev_key)

#define ONE         256         /* scale 1.0 */
#define GRAD_STEPS  64          /* rainbow steps */
#define GRAD_PEN0   192         /* true colour: pens set for the rainbow */
#define MAX_LINES   2048        /* screen lines with a rainbow pen */

#define INF_NAME    "BRKPLUS.INF"

extern short _app;              /* crt.S: 1 = started as program */
extern void mouse_trap(void);   /* mtrap.S */
void *old_motv;

/* work area of the window; mtrap.S keeps the pointer inside it */
short xwork, ywork, wwork, hwork;

/* sizes in logical pixels, set by init_game() */
static short BRICK_W, BRICK_H, GAP_WID, GAP_HI;
static short BALL_WI, BALL_HI, PADL_WI, PADL_HI, PADL_GAP;
static short BRKT_WI, BRKT_HI, ZONE_HI;
static short X_MIN, X_MAX, Y_MIN, Y_MAX;
static short BRICK_TOP, PADL_TOP;
static short xvel_init, yvel_init;
static short hsqueeze, vsqueeze;

/* drawing transform */
static long scale = ONE;
static short org_x, org_y;      /* screen position of logical 0,0 */

/* GEM */
static short handle, ap_id, gl_item, wi_hand, wi_kind;
static short gl_wchar, gl_hchar, gl_wbox, gl_hbox;
static short xdesk, ydesk, wdesk, hdesk;
static short msgbuff[8];
static short dummy;
static short em_flag, run, served;
static short iconified, can_iconify;
static long prev_scale;         /* fuller: scale to go back to */

/* game */
static short brick[ROWS][COLS], bricks, balls, score;
static char scrtxt[10];
static short one_time;
static short x_ball, y_ball, old_x, old_y, ydir;
static short xvel, yvel, xsum, ysum;
static short xyhit, service, doubled, br_okhit, br_bounce;
static short x_padl, old_padl, size_padl, pdx[3];

/* plus */
static short attract = 1;       /* between games: credits shown */
static short hiscore;
static short resizable;         /* more than 240 lines */
static short font_h;            /* system font (score) */
static char inf_path[160];
static short rainbow;           /* 8 bit or more */
static short grad_pen[GRAD_STEPS];
static short line_pen[MAX_LINES];
static short line_top, line_cnt; /* screen lines line_pen[] is for */

/* application mode */
static short ev_key;            /* MU_KEYBD as application (^Q) */
static short quitting;

/* fixes from the classic C version */
static short mt, hooked, hidden;
static long last;

/* ---------------------------------------------------------------- */
/* small helpers (no C library) */

static short str_len(const char *s)
{
	short n = 0;

	while (*s++)
		n++;
	return n;
}

static char *str_cpy(char *d, const char *s)
{
	while ((*d = *s++) != 0)
		d++;
	return d;
}

/* number to text, no leading zeros */
static char *num_str(char *d, short n)
{
	char tmp[8];
	short i = 0;

	do {
		tmp[i++] = '0' + n % 10;
		n /= 10;
	} while (n && i < 7);
	while (i)
		*d++ = tmp[--i];
	*d = 0;
	return d;
}

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
/* scaled drawing */

static short sx(short x)
{
	return org_x + (short)(((long)x * scale) >> 8);
}

static short sy(short y)
{
	return org_y + (short)(((long)y * scale) >> 8);
}

/* screen size of the playfield */
static short field_w(void)
{
	return (short)(((long)BRKT_WI * scale) >> 8);
}

static short field_h(void)
{
	return (short)(((long)BRKT_HI * scale) >> 8);
}

static void set_clip(void)
{
	short pxy[4];

	pxy[0] = xwork;
	pxy[1] = ywork;
	pxy[2] = xwork + wwork - 1;
	pxy[3] = ywork + hwork - 1;
	vs_clip(handle, 1, pxy);
}

/* fill a logical rectangle */
static void box(short x, short y, short w, short h)
{
	short pxy[4];

	pxy[0] = sx(x);
	pxy[1] = sy(y);
	pxy[2] = sx(x + w) - 1;
	pxy[3] = sy(y + h) - 1;
	if (pxy[2] < pxy[0])
		pxy[2] = pxy[0];
	if (pxy[3] < pxy[1])
		pxy[3] = pxy[1];
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

/* ---------------------------------------------------------------- */
/* rainbow (8 bit and more) */

/* hue 0..1000 (red .. violet) to VDI rgb 0..1000 */
static void rainbow_rgb(short t, short rgb[3])
{
	long h = (long)t * 5;           /* 0..5000: six 1000-wide sectors */
	short f, sector, i;
	short r = 0, g = 0, b = 0;

	h = h * 280 / 300;              /* stop at violet, not back at red */
	sector = h / 1000;
	f = h % 1000;
	switch (sector) {
	case 0: r = 1000; g = f;        b = 0;    break;  /* red -> yellow */
	case 1: r = 1000 - f; g = 1000; b = 0;    break;  /* -> green */
	case 2: r = 0;    g = 1000;     b = f;    break;  /* -> cyan */
	case 3: r = 0;    g = 1000 - f; b = 1000; break;  /* -> blue */
	default: r = f;   g = 0;        b = 1000; break;  /* -> violet */
	}
	rgb[0] = r;
	rgb[1] = g;
	rgb[2] = b;
	for (i = 0; i < 3; i++)         /* a little softer */
		rgb[i] = 120 + rgb[i] * 82 / 100;
}

/* Palette modes: the nearest existing pen (the palette stays untouched,
   it belongs to everybody). True colour: own pens GRAD_PEN0.. */
static void init_rainbow(void)
{
	short out[57], planes, clut, pens, i, p, best, rgb[3], c[3];
	long d, bestd;

	vq_extnd(handle, 1, out);
	planes = out[4];
	clut = out[5];
	if (planes < 8)
		return;
	vq_extnd(handle, 0, out);
	pens = out[13];
	rainbow = 1;
	for (i = 0; i < GRAD_STEPS; i++) {
		rainbow_rgb((long)i * 1000 / (GRAD_STEPS - 1), rgb);
		if (!clut && pens > GRAD_PEN0 + GRAD_STEPS - 1) {
			vs_color(handle, GRAD_PEN0 + i, rgb);
			grad_pen[i] = GRAD_PEN0 + i;
			continue;
		}
		best = 1;
		bestd = 0x7fffffffL;
		for (p = 0; p < pens && p < 256; p++) {
			vq_color(handle, p, 1, c);
			d = (long)(c[0] - rgb[0]) * (c[0] - rgb[0])
			  + (long)(c[1] - rgb[1]) * (c[1] - rgb[1])
			  + (long)(c[2] - rgb[2]) * (c[2] - rgb[2]);
			if (d < bestd) {
				bestd = d;
				best = p;
			}
		}
		grad_pen[i] = best;
	}
}

/* pen for every screen line of the brick wall at the current scale */
static void rainbow_lines(void)
{
	short top = sy(brick_y(0)), bottom = sy(brick_y(ROWS - 1) + BRICK_H);
	short n = bottom - top, i;

	if (n < 1)
		n = 1;
	if (n > MAX_LINES)
		n = MAX_LINES;
	line_top = top;
	line_cnt = n;
	for (i = 0; i < n; i++)
		line_pen[i] = grad_pen[(long)i * (GRAD_STEPS - 1) / (n > 1 ? n - 1 : 1)];
}

/* one brick, line groups of the same pen */
static void rainbow_brick(short row, short col)
{
	short pxy[4], y, y0, y1, pen, i;

	pxy[0] = sx(brick_x(col));
	pxy[2] = sx(brick_x(col) + BRICK_W) - 1;
	y0 = sy(brick_y(row));
	y1 = sy(brick_y(row) + BRICK_H) - 1;
	if (pxy[2] < pxy[0])
		pxy[2] = pxy[0];
	if (y1 < y0)
		y1 = y0;
	vsf_interior(handle, 1);
	for (y = y0; y <= y1; y = pxy[3] + 1) {
		i = y - line_top;
		pen = i >= 0 && i < line_cnt ? line_pen[i] : 1;
		pxy[1] = y;
		for (pxy[3] = y; pxy[3] < y1; pxy[3]++) {
			i = pxy[3] + 1 - line_top;
			if ((i >= 0 && i < line_cnt ? line_pen[i] : 1) != pen)
				break;
		}
		vsf_color(handle, pen);
		vr_recfl(handle, pxy);
	}
}

/* ---------------------------------------------------------------- */
/* playfield */

static void draw_brick(short row, short col)
{
	if (rainbow) {
		vswr_mode(handle, MD_REPLACE);
		rainbow_brick(row, col);
		return;
	}
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

static void erase_brick(short row, short col)
{
	vswr_mode(handle, MD_REPLACE);
	vsf_color(handle, 0);
	box(brick_x(col), brick_y(row), BRICK_W, BRICK_H);
}

static void draw_ball(short x, short y)
{
	vswr_mode(handle, MD_XOR);
	box(x, y, BALL_WI, BALL_HI);
}

static void draw_paddle(short x)
{
	vswr_mode(handle, MD_XOR);
	box(x, Y_MAX - PADL_HI - PADL_GAP, size_padl, PADL_HI);
}

static void erase_paddle(short x)
{
	vswr_mode(handle, MD_REPLACE);
	vsf_color(handle, 0);
	box(x, Y_MAX - PADL_HI - PADL_GAP, size_padl, PADL_HI);
}

static void draw_bricks(void)
{
	short row, col;

	for (col = 0; col < COLS; col++)
		for (row = 0; row < ROWS; row++)
			if (brick[row][col])
				draw_brick(row, col);
	vsf_interior(handle, 1);
}

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

/* Largest font whose cells are at most maxh high and len chars at most
   maxw wide. Without GDOS the VDI has the 6x6, 8x8 and 8x16 system fonts;
   with NVDI & Co. more sizes. Returns the cell size. */
static void fit_font(short maxw, short len, short maxh, short *cw, short *ch)
{
	short h, d1, d2;

	for (h = maxh; h > 1; h--) {
		vst_height(handle, h, &d1, &d2, cw, ch);
		if (*ch <= maxh && *cw * len <= maxw)
			return;
	}
	vst_height(handle, 1, &d1, &d2, cw, ch);
}

/* as in the original: system font, replace mode, 8 characters centred */
static void show_score(void)
{
	short d;

	vst_height(handle, font_h, &d, &d, &d, &d);  /* credits change it */
	vst_effects(handle, 0);
	vst_color(handle, vsqueeze ? 2 : 1);
	vswr_mode(handle, MD_REPLACE);
	v_gtext(handle, org_x + (field_w() - 64) / 2, org_y + 1, scrtxt);
}

/* free space between the bricks and the paddle (logical) */
static short free_top(void)
{
	return brick_y(ROWS - 1) + BRICK_H + GAP_HI * 2;
}

static short free_bottom(void)
{
	return Y_MAX - PADL_HI - PADL_GAP * 2;
}

static void erase_credits(void)
{
	vswr_mode(handle, MD_REPLACE);
	vsf_color(handle, 0);
	vsf_interior(handle, 1);
	box(0, free_top(), BRKT_WI, free_bottom() - free_top());
}

static void text_line(const char *s, short y, short cw, short colour)
{
	short w = field_w();

	vst_color(handle, colour);
	v_gtext(handle, org_x + (w - str_len(s) * cw) / 2, y, (char *)s);
}

/* the credits: "Breakout" - "The first game" - "Original by Matt
   Householder" - high score; the long line is split when it does not fit */
static void show_credits(void)
{
	static const char *by = "Original by Matt Householder";
	char hi[24];
	short top = sy(free_top()), h = sy(free_bottom()) - top;
	short w = field_w() - 2;
	short tw, th, bw, bh, bw2, bh2, split, lines, total, y, colour, d;

	num_str(str_cpy(hi, "High score "), hiscore);
	fit_font(w, 8, h / 4, &tw, &th);                /* title */
	fit_font(w, 28, h / 8, &bw, &bh);               /* one line */
	fit_font(w, 16, h / 8, &bw2, &bh2);             /* split */
	split = bh2 > bh;
	if (split) {
		bw = bw2;
		bh = bh2;
	}
	lines = split ? 5 : 4;                          /* incl. a gap */
	bh += (bh + 3) / 4;                             /* line spacing */
	total = th + bh / 2 + lines * bh;
	y = top + (h - total) / 2;
	colour = hsqueeze || vsqueeze || rainbow ? 2 : 1;   /* red in colour */

	vswr_mode(handle, MD_TRANS);
	vst_effects(handle, 1);                         /* bold title */
	fit_font(w, 8, h / 4, &tw, &th);
	text_line("Breakout", y, tw, colour);
	vst_effects(handle, 0);
	y += th + bh / 2;
	fit_font(w, split ? 16 : 28, h / 8, &bw, &d);
	text_line("The first game", y, bw, 1);
	y += bh;
	if (split) {
		text_line("Original by", y, bw, 1);
		y += bh;
		text_line(by + 12, y, bw, 1);
	} else
		text_line(by, y, bw, 1);
	y += bh * 2;
	text_line(hi, y, bw, 1);
	vswr_mode(handle, MD_REPLACE);
}

/* the whole playfield at the current transform */
static void draw_field(short icon)
{
	vswr_mode(handle, MD_REPLACE);
	vsf_interior(handle, 1);
	vsf_color(handle, 0);
	box(0, 0, BRKT_WI, Y_MAX);
	vsf_color(handle, 1);
	box(0, Y_MIN, BRKT_WI, 1);
	if (!icon) {
		show_lives();
		show_score();
	}
	draw_bricks();
	draw_paddle(x_padl);
	if (!icon || y_ball < Y_MAX)
		draw_ball(x_ball, y_ball);
	if (attract && !icon)
		show_credits();
}

/* iconified: the game as a still miniature, centred */
static void draw_icon(void)
{
	long keep = scale;
	short pxy[4], kx = org_x, ky = org_y;
	long sw = (long)wwork * ONE / BRKT_WI, sh = (long)hwork * ONE / BRKT_HI;

	pxy[0] = xwork;
	pxy[1] = ywork;
	pxy[2] = xwork + wwork - 1;
	pxy[3] = ywork + hwork - 1;
	vswr_mode(handle, MD_REPLACE);
	vsf_interior(handle, 1);
	vsf_color(handle, 0);
	vr_recfl(handle, pxy);
	scale = sw < sh ? sw : sh;
	org_x = xwork + (wwork - field_w()) / 2;
	org_y = ywork + (hwork - field_h()) / 2;
	if (rainbow)
		rainbow_lines();
	draw_field(1);
	vsf_color(handle, 1);                           /* frame */
	vsl_color(handle, 1);
	pxy[0] = org_x - 1;
	pxy[1] = org_y - 1;
	pxy[2] = org_x + field_w();
	pxy[3] = org_y + field_h();
	{
		short f[10];

		f[0] = pxy[0]; f[1] = pxy[1]; f[2] = pxy[2]; f[3] = pxy[1];
		f[4] = pxy[2]; f[5] = pxy[3]; f[6] = pxy[0]; f[7] = pxy[3];
		f[8] = pxy[0]; f[9] = pxy[1];
		v_pline(handle, 5, f);
	}
	scale = keep;
	org_x = kx;
	org_y = ky;
	if (rainbow)
		rainbow_lines();
}

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

static void do_redraw(short x, short y, short w, short h)
{
	GRECT area, r;
	short pxy[4];

	if (!wi_hand)
		return;
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
			if (iconified)
				draw_icon();
			else
				draw_field(0);
		}
		wind_get(wi_hand, WF_NEXTXYWH, &r.g_x, &r.g_y, &r.g_w, &r.g_h);
	}
	graf_mouse(M_ON, NULL);
	set_clip();
}

static void redraw_all(void)
{
	do_redraw(xwork, ywork, wwork, hwork);
}

/* a WM_REDRAW to ourselves: after a size change old TOS finishes its own
   window update (and clears the work area) after we return */
static void post_redraw(void)
{
	short m[8];

	m[0] = WM_REDRAW;
	m[1] = ap_id;
	m[2] = 0;
	m[3] = wi_hand;
	m[4] = xwork;
	m[5] = ywork;
	m[6] = wwork;
	m[7] = hwork;
	appl_write(ap_id, 16, m);
}

/* ---------------------------------------------------------------- */
/* high score file, next to the accessory (its start folder) */

static void init_inf_path(void)
{
	char *p = inf_path;
	short drv = Dgetdrv();

	*p++ = 'A' + drv;
	*p++ = ':';
	if (Dgetpath(p, drv + 1) < 0)
		*p = 0;
	while (*p)
		p++;
	if (p[-1] != '\\')
		*p++ = '\\';
	str_cpy(p, INF_NAME);
}

static void load_hiscore(void)
{
	char buf[32];
	long fh, n, i;

	fh = Fopen(inf_path, 0);
	if (fh < 0)
		return;
	n = Fread((short)fh, sizeof buf - 1, buf);
	Fclose((short)fh);
	hiscore = 0;
	for (i = 0; i < n && buf[i] >= '0' && buf[i] <= '9'; i++)
		hiscore = hiscore * 10 + buf[i] - '0';
}

static void save_hiscore(void)
{
	char buf[32], *p;
	long fh;

	p = num_str(buf, hiscore);
	p = str_cpy(p, "\r\nBreakout+ high score\r\n");
	fh = Fcreate(inf_path, 0);
	if (fh < 0)
		return;
	Fwrite((short)fh, p - buf, buf);
	Fclose((short)fh);
}

/* ---------------------------------------------------------------- */
/* setup */

static void open_vwork(void)
{
	short work_in[11], work_out[57], i;

	for (i = 0; i < 10; i++)
		work_in[i] = 1;
	work_in[10] = 2;
	v_opnvwk(work_in, &handle, work_out);
	hsqueeze = work_out[0] < 320;
	vsqueeze = work_out[1] < 200;
	resizable = work_out[1] + 1 > 240;
	{
		short attrib[10];

		vqt_attributes(handle, attrib);
		font_h = attrib[7];             /* system font height */
	}
}

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
	PADL_TOP = Y_MAX - (BALL_HI + PADL_HI + PADL_GAP);   /* as original */
	PADL_GAP = PADL_HI * 2;
}

static void init_bricks(void)
{
	short row, col;

	bricks = ROWS * COLS;
	for (row = 0; row < ROWS; row++)
		for (col = 0; col < COLS; col++)
			brick[row][col] = 1;
}

static void init_break(void)
{
	balls = 5;
	size_padl = PADL_WI;
	x_padl = old_padl = (BRKT_WI - size_padl) / 2;
	init_bricks();
}

/* work area -> transform */
static void work_changed(void)
{
	wind_get(wi_hand, WF_WORKXYWH, &xwork, &ywork, &wwork, &hwork);
	org_x = xwork;
	org_y = ywork;
	set_clip();
	if (rainbow && !iconified)
		rainbow_lines();
}

/* outer window size for a scale */
static void window_for(long s, short x, short y, short *w, short *h)
{
	short d;

	wind_calc(WC_BORDER, wi_kind, x, y, (short)((BRKT_WI * s) >> 8),
	          (short)((BRKT_HI * s) >> 8), &d, &d, w, h);
}

/* largest whole-number scale that fits the desktop */
static long max_scale(void)
{
	short w, h;
	long s;

	window_for(ONE, xdesk, ydesk, &w, &h);
	w -= BRKT_WI;                   /* border sizes */
	h -= BRKT_HI;
	s = (long)(wdesk - w) * ONE / BRKT_WI;
	if ((long)(hdesk - h) * ONE / BRKT_HI < s)
		s = (long)(hdesk - h) * ONE / BRKT_HI;
	s &= ~(long)(ONE - 1);              /* whole numbers only */
	return s < ONE ? ONE : s;
}

/* resize the window to scale s, keeping its top left corner */
static void set_scale(long s)
{
	short x, y, w, h;
	long m = max_scale();

	s = (s + ONE / 2) & ~(long)(ONE - 1);   /* nearest whole number */
	if (s < ONE)
		s = ONE;
	if (s > m)
		s = m;
	scale = s;
	wind_get(wi_hand, WF_CURRXYWH, &x, &y, &w, &h);
	window_for(s, x, y, &w, &h);
	if (x + w > xdesk + wdesk)          /* keep it on the desktop */
		x = xdesk + wdesk - w;
	if (y + h > ydesk + hdesk)
		y = ydesk + hdesk - h;
	if (x < xdesk)
		x = xdesk;
	if (y < ydesk)
		y = ydesk;
	wind_set(wi_hand, WF_CURRXYWH, x, y, w, h);
	work_changed();
	post_redraw();
}

static void open_window(void)
{
	short xw, yw, ww, hw;

	/* sizer and fuller only above 240 lines and if 2x fits at all */
	wi_kind = NAME | CLOSER | MOVER | SIZER | FULLER;
	if (!resizable || max_scale() < 2 * ONE)
		wi_kind = NAME | CLOSER | MOVER;
	if (can_iconify)
		wi_kind |= SMALLER;
	wi_hand = wind_create(wi_kind, xdesk, ydesk, wdesk, hdesk);
	if (wi_hand < 0) {
		wi_hand = 0;
		form_alert(1, "[1][Breakout+:|No window left.][ OK ]");
		return;
	}
	wind_set_str(wi_hand, WF_NAME, " Breakout+ ");
	if (scale > max_scale())
		scale = max_scale();
	window_for(scale, xdesk, ydesk, &ww, &hw);
	xw = xdesk + (wdesk - ww) / 2;
	yw = ydesk + (hdesk - hw) / 2;
	iconified = 0;
	wind_open(wi_hand, xw, yw, ww, hw);
	work_changed();
}

/* AES 4.1 / MagiC / XaAES: iconify? */
static void check_iconify(void)
{
	short o1 = 0, o2, o3, o4;

	if (_AESversion >= 0x400 || appl_find("?AGI") >= 0)
		if (appl_getinfo(11, &o1, &o2, &o3, &o4) && (o1 & 0x80))
			can_iconify = 1;
}

/* ---------------------------------------------------------------- */
/* game */

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

/* the paddle follows the mouse (screen -> logical) */
static void move_paddle(void)
{
	short status, x, y, maxoff;

	vq_mouse(handle, &status, &x, &y);
	x = (short)((long)(x - xwork) * ONE / scale);
	maxoff = BRKT_WI - size_padl;
	if (x < 0)
		x = 0;
	if (x > maxoff)
		x = maxoff;
	x_padl = x;
	draw_paddle(old_padl);
	draw_paddle(x_padl);
	pdx[2] = pdx[1];
	pdx[1] = pdx[0];
	pdx[0] = x_padl - old_padl;
	old_padl = x_padl;
}

static void bounce(void)
{
	draw_ball(x_ball, y_ball);
	if (y_ball == 0 || y_ball == Y_MIN - BALL_HI)
		ydir = -ydir;
	y_ball += ydir;
	draw_ball(x_ball, y_ball);
}

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
		y_ball = Y_MAX + 16;
		draw_ball(x_ball, y_ball);
	}
}

/* ---------------------------------------------------------------- */
/* start / stop, timing */

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

static void track_pointer(void)
{
	short status, x, y;

	vq_mouse(handle, &status, &x, &y);
	if (x >= xwork && x < xwork + wwork && y >= ywork && y < ywork + hwork)
		hide_pointer();
	else
		show_pointer();
}

static short steps_due(void)
{
	long now = hz200(), n = now - last;

	if (n >= STEP200 * MAXSTEP) {
		last = now;
		return MAXSTEP;
	}
	n /= STEP200;
	last += n * STEP200;
	return n;
}

static void tick(short serving)
{
	short top, n;

	if (iconified || (wind_get(0, WF_TOP, &top, &dummy, &dummy, &dummy)
	                  && top != wi_hand)) {
		pause_game();
		return;
	}
	if (mt)
		track_pointer();
	for (n = steps_due(); n > 0; n--) {
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

/* WM_SIZED: keep the aspect ratio; the side that changed more decides,
   rounded to a whole-number scale */
static void sized(short w, short h)
{
	short d, ww, wh;
	long sw, sh;

	wind_calc(WC_WORK, wi_kind, 0, 0, w, h, &d, &d, &ww, &wh);
	sw = (long)ww * ONE / BRKT_WI;
	sh = (long)wh * ONE / BRKT_HI;
	prev_scale = 0;
	set_scale((sw > scale ? sw - scale : scale - sw) >=
	          (sh > scale ? sh - scale : scale - sh) ? sw : sh);
}

/* ---------------------------------------------------------------- */
/* application mode: menu bar built in code (no resource file) */

enum { M_ROOT, M_BAR, M_ACTIVE, M_TDESK, M_TFILE, M_SCREEN,
       M_DESKBOX, M_ABOUT, M_SEP, M_ACC1, M_ACC2, M_ACC3, M_ACC4, M_ACC5,
       M_ACC6, M_FILEBOX, M_QUIT, M_COUNT };

/* x, y, w, h in characters, the high byte adds pixels (as in a .RSC);
   rsrc_obfix() turns them into pixels */
static OBJECT menu_tree[M_COUNT] = {
	{ -1, M_BAR, M_SCREEN, G_IBOX, 0, 0, { 0 }, 0, 0, 80, 25 },
	{ M_SCREEN, M_ACTIVE, M_ACTIVE, G_BOX, 0, 0, { 0x1100L }, 0, 0, 80, 0x201 },
	{ M_BAR, M_TDESK, M_TFILE, G_IBOX, 0, 0, { 0 }, 2, 0, 12, 0x301 },
	{ M_TFILE, -1, -1, G_TITLE, 0, 0, { 0 }, 0, 0, 6, 0x301 },
	{ M_ACTIVE, -1, -1, G_TITLE, 0, 0, { 0 }, 6, 0, 6, 0x301 },
	{ M_ROOT, M_DESKBOX, M_FILEBOX, G_IBOX, 0, 0, { 0 }, 0, 0x301, 80, 19 },
	{ M_FILEBOX, M_ABOUT, M_ACC6, G_BOX, 0, 0, { 0xFF1100L }, 2, 0, 21, 8 },
	{ M_SEP, -1, -1, G_STRING, 0, 0, { 0 }, 0, 0, 21, 1 },
	{ M_ACC1, -1, -1, G_STRING, 0, OS_DISABLED, { 0 }, 0, 1, 21, 1 },
	{ M_ACC2, -1, -1, G_STRING, 0, 0, { 0 }, 0, 2, 21, 1 },
	{ M_ACC3, -1, -1, G_STRING, 0, 0, { 0 }, 0, 3, 21, 1 },
	{ M_ACC4, -1, -1, G_STRING, 0, 0, { 0 }, 0, 4, 21, 1 },
	{ M_ACC5, -1, -1, G_STRING, 0, 0, { 0 }, 0, 5, 21, 1 },
	{ M_ACC6, -1, -1, G_STRING, 0, 0, { 0 }, 0, 6, 21, 1 },
	{ M_DESKBOX, -1, -1, G_STRING, 0, 0, { 0 }, 0, 7, 21, 1 },
	{ M_SCREEN, M_QUIT, M_QUIT, G_BOX, 0, 0, { 0xFF1100L }, 8, 0, 12, 1 },
	{ M_FILEBOX, -1, -1, G_STRING, OF_LASTOB, 0, { 0 }, 0, 0, 12, 1 },
};

static void set_text(short i, const char *s)
{
	menu_tree[i].ob_spec.free_string = (char *)s;
}

static void menu_create(void)
{
	short i, sw = xdesk + wdesk;        /* screen width */

	set_text(M_TDESK, " Desk ");
	set_text(M_TFILE, " File ");
	set_text(M_ABOUT, "  About Breakout+... ");
	set_text(M_SEP, "---------------------");
	for (i = M_ACC1; i <= M_ACC6; i++)  /* the AES puts the ACCs here */
		set_text(i, "  Desk Accessory     ");
	set_text(M_QUIT, "  Quit    ^Q");
	for (i = 0; i < M_COUNT; i++)
		rsrc_obfix(menu_tree, i);
	menu_tree[M_ROOT].ob_width = menu_tree[M_BAR].ob_width = sw;
	menu_tree[M_SCREEN].ob_width = sw;
	menu_bar(menu_tree, 1);
}

static void about(void)
{
	form_alert(1, "[0][Breakout+| |"
	              "The first game on the Atari ST.|"
	              "Original by Matt Householder,|"
	              "Atari Corp. 1985/86.][ OK ]");
}

static void menu_selected(short title, short item)
{
	if (item == M_ABOUT)
		about();
	else if (item == M_QUIT)
		quitting = 1;
	menu_tnormal(menu_tree, title, 1);
}

/* leave cleanly from wherever the game is; never returns. Called with
   the screen lock held (inside the event loop). */
static void app_exit(void)
{
	pause_game();                       /* mouse vector, pointer */
	if (score > hiscore) {              /* also when quitting mid-game */
		hiscore = score;
		save_hiscore();
	}
	if (wi_hand) {
		wind_close(wi_hand);
		wind_delete(wi_hand);
	}
	wind_update(END_UPDATE);
	menu_bar(menu_tree, 0);
	v_clsvwk(handle);
	appl_exit();
	Pterm0();
}

static void message(void)
{
	switch (msgbuff[0]) {
	case WM_REDRAW:
		do_redraw(msgbuff[4], msgbuff[5], msgbuff[6], msgbuff[7]);
		break;
	case WM_TOPPED:
		if (msgbuff[3] != wi_hand)
			pause_game();
		wind_set(msgbuff[3], WF_TOP, 0, 0, 0, 0);
		break;
	case WM_CLOSED:
		if (_app) {                     /* the only window: quit */
			quitting = 1;
			break;
		}
		pause_game();
		wind_close(msgbuff[3]);
		wind_delete(msgbuff[3]);
		wi_hand = 0;
		iconified = 0;
		break;
	case WM_MOVED:
		wind_set(msgbuff[3], WF_CURRXYWH, msgbuff[4], msgbuff[5],
		         msgbuff[6], msgbuff[7]);
		work_changed();
		break;
	case WM_SIZED:
		pause_game();
		sized(msgbuff[6], msgbuff[7]);
		break;
	case WM_FULLED:
		pause_game();
		if (prev_scale) {               /* back */
			long s = prev_scale;

			prev_scale = 0;
			set_scale(s);
		} else {
			prev_scale = scale;
			set_scale(max_scale());
		}
		break;
	case WM_ICONIFY:
	case WM_ALLICONIFY:
		pause_game();
		wind_set(wi_hand, WF_ICONIFY, msgbuff[4], msgbuff[5],
		         msgbuff[6], msgbuff[7]);
		iconified = 1;
		work_changed();
		break;
	case WM_UNICONIFY:
		wind_set(wi_hand, WF_UNICONIFY, msgbuff[4], msgbuff[5],
		         msgbuff[6], msgbuff[7]);
		iconified = 0;
		work_changed();
		break;
	case AC_OPEN:
		if (msgbuff[4] == gl_item && !wi_hand)
			open_window();
		else if (wi_hand) {
			if (iconified) {            /* bring it back */
				short x, y, w, h;

				wind_get(wi_hand, WF_UNICONIFY, &x, &y, &w, &h);
				wind_set(wi_hand, WF_UNICONIFY, x, y, w, h);
				iconified = 0;
				work_changed();
			}
			wind_set(wi_hand, WF_TOP, 0, 0, 0, 0);
		}
		break;
	case AP_TERM:                       /* MiNT / MagiC shutdown */
		if (_app)
			quitting = 1;
		break;
	case MN_SELECTED:
		menu_selected(msgbuff[3], msgbuff[4]);
		break;
	case AC_CLOSE:
		if (msgbuff[3] == gl_item && wi_hand) {
			pause_game();
			if (mt) {
				wind_close(wi_hand);
				wind_delete(wi_hand);
			}
			wi_hand = 0;
			iconified = 0;
		}
		break;
	}
}

#ifdef AUTOTEST
#include "autotest.h"
#endif

static void events(short serving)
{
	short ev, mx, my, mb, d, key;

	served = 0;
	while (serving ? !served : y_ball < Y_MAX) {
		if (serving)
			em_flag = run ? EV_RUN : EV_IDLE;
#ifdef AUTOTEST
		em_flag |= MU_TIMER;
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
		                msgbuff, TIMER_MS, &mx, &my, &d, &d, &key, &d);
		wind_update(BEG_UPDATE);
		if (ev & MU_MESAG)
			message();
		if ((ev & MU_KEYBD) && (key & 0xff) == 0x11)    /* ^Q */
			quitting = 1;
		if (quitting)
			app_exit();
#ifdef AUTOTEST
		autotest();
#endif
		if ((ev & MU_TIMER) && run)
			tick(serving);
		if ((ev & MU_BUTTON) && wi_hand && !iconified) {
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
		wind_update(BEG_UPDATE);
		if (wi_hand && !iconified)
			draw_ball(x_ball, y_ball);
		wind_update(END_UPDATE);
	}
}

static void serve(void)
{
	short draw = wi_hand && !iconified;

	wind_update(BEG_UPDATE);
	if (size_padl != PADL_WI) {
		if (draw)
			erase_paddle(x_padl);
		size_padl = PADL_WI;
		if (draw)
			draw_paddle(x_padl);
	}
	x_ball = BALL_WI * 3 + BALL_WI / 2 + 1;
	y_ball = 1;
	ydir = 1;
	if (!one_time && draw) {
		show_lives();
		draw_ball(x_ball, y_ball);
	}
	one_time = 0;
	wind_update(END_UPDATE);
	events(1);
	old_x = x_ball;
	old_y = y_ball;
	ydir = 1;
	ysum = xsum = 0;
	xvel = xvel_init;
	yvel = yvel_init;
	doubled = 0;
	service = 1;
	wind_update(BEG_UPDATE);
	if (attract) {                      /* a game starts: credits away */
		attract = 0;
		if (wi_hand && !iconified)
			erase_credits();
	}
	if (balls == 5) {
		score = 0;
		convert();
		show_score();
	}
	wind_update(END_UPDATE);
}

void __main(void)
{
}

int main(void)
{
	ap_id = appl_init();
	if (ap_id < 0)
		return 1;
	handle = graf_handle(&gl_wchar, &gl_hchar, &gl_wbox, &gl_hbox);
	open_vwork();
	mt = Supexec(find_mt);
	check_iconify();
	init_inf_path();
	load_hiscore();
	wind_get(0, WF_WORKXYWH, &xdesk, &ydesk, &wdesk, &hdesk);
	if (!_app)
		gl_item = menu_register(ap_id, "  Breakout+");
	else {
		ev_key = MU_KEYBD;
		if (mt)                         /* name in the application list */
			menu_register(ap_id, "  Breakout+");
		menu_create();
	}
	em_flag = EV_IDLE;
	graf_mouse(ARROW, NULL);
	wi_hand = 0;
	vst_alignment(handle, 0, 5, &dummy, &dummy);
	init_rainbow();
	init_game();
	init_break();
	score = 0;
	convert();
	run = 0;
	one_time = 1;
	if (_app) {
		open_window();
		if (!wi_hand) {                 /* no window left */
			wind_update(BEG_UPDATE);
			app_exit();
		}
	}
	for (;;) {
		while (balls) {
			serve();
			events(0);
		}
		if (score > hiscore) {
			hiscore = score;
			save_hiscore();
		}
		init_break();
		attract = 1;
		wind_update(BEG_UPDATE);
		if (wi_hand && !iconified)
			redraw_all();
		wind_update(END_UPDATE);
	}
}
