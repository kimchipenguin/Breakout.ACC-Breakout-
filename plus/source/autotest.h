/*
 * autotest.h - scripted test build (make autotest), included by brkplus.c
 *
 * Opens the window AT_OPEN seconds after reset and then sends itself the
 * messages a user would cause, AT_STEP seconds apart, so Hatari can take
 * screenshots in between without mouse moves:
 *   1 WM_SIZED to 1.5 times the size   2 WM_FULLED   3 WM_FULLED (back)
 *   4 WM_ICONIFY (if the AES has it)   5 WM_UNICONIFY
 *   6 high score 1234 saved, credits redrawn
 *   7 game started and served: the credits go
 */
#ifndef AT_OPEN
#define AT_OPEN 12
#endif
#ifndef AT_STEP
#define AT_STEP 8
#endif

static short at_state;
static long at_next;

static void at_send(short type, short x, short y, short w, short h)
{
	short m[8];

	m[0] = type;
	m[1] = ap_id;
	m[2] = 0;
	m[3] = wi_hand;
	m[4] = x;
	m[5] = y;
	m[6] = w;
	m[7] = h;
	appl_write(ap_id, 16, m);
}

static void autotest(void)
{
	long now = hz200();
	short x, y, w, h;

	if (at_state == 0) {
		if (now < 200L * AT_OPEN)
			return;
		if (!wi_hand)                   /* applications have it already */
			open_window();
		at_state = 1;
		at_next = now + 200L * AT_STEP;
		return;
	}
	if (now < at_next || !wi_hand)
		return;
	at_next = now + 200L * AT_STEP;
	wind_get(wi_hand, WF_CURRXYWH, &x, &y, &w, &h);
	switch (at_state++) {
	case 1:
		window_for(scale * 3 / 2, x, y, &w, &h);
		at_send(WM_SIZED, x, y, w, h);
		break;
	case 2:
	case 3:
		at_send(WM_FULLED, 0, 0, 0, 0);
		break;
	case 4:
		if (can_iconify)
			at_send(WM_ICONIFY, xdesk, ydesk + hdesk - 72, 72, 72);
		break;
	case 5:
		if (can_iconify && iconified) {
			wind_get(wi_hand, WF_UNICONIFY, &x, &y, &w, &h);
			at_send(WM_UNICONIFY, x, y, w, h);
		}
		break;
	case 6:
		hiscore = 1234;
		save_hiscore();
		redraw_all();
		break;
	case 7:
		start_game();
		served = 1;
		break;
	}
}
