#include	"compiler.h"

#if defined(SUPPORT_KEYDISP)

#include	"keydisp.h"

typedef struct {
	UINT16	posx;
	UINT16	pals;
const UINT8	*data;
} KDKEYPOS;

typedef struct {
	UINT8	k[KEYDISP_NOTEMAX];
	UINT8	r[KEYDISP_NOTEMAX];
	UINT	remain;
	UINT8	flag;
	UINT8	padding[3];
} KDCHANNEL;

typedef struct {
	UINT8		mode;
	UINT8		dispflag;
	UINT8		framepast;
	UINT8		keymax;
	KDCHANNEL	ch[KEYDISP_CHMAX];
	UINT8		pal8[KEYDISP_PALS];
	UINT16		pal16[KEYDISP_LEVEL*2];
	RGB32		pal32[KEYDISP_LEVEL*2];
	KDKEYPOS	keypos[128];
} KEYDISP;

static	KEYDISP		keydisp;

#include	"keydisp.res"


// ---- event

static void keyon(UINT ch, UINT8 note) {

	UINT		i;
	KDCHANNEL	*kdch;

	note &= 0x7f;
	kdch = keydisp.ch + ch;
	for (i=0; i<kdch->remain; i++) {
		if (kdch->k[i] == note) {				// ヒットした
			for (; i<(kdch->remain-1); i++) {
				kdch->k[i] = kdch->k[i+1];
				kdch->r[i] = kdch->r[i+1];
			}
			kdch->k[i] = note;
			kdch->r[i] = 0x80 | (KEYDISP_LEVEL - 1);
			kdch->flag |= 1;
			return;
		}
	}
	if (i < KEYDISP_NOTEMAX) {
		kdch->k[i] = note;
		kdch->r[i] = 0x80 | (KEYDISP_LEVEL - 1);
		kdch->flag |= 1;
		kdch->remain++;
	}
}

static void keyoff(UINT ch, UINT8 note) {

	UINT		i;
	KDCHANNEL	*kdch;

	note &= 0x7f;
	kdch = keydisp.ch + ch;
	for (i=0; i<kdch->remain; i++) {
		if (kdch->k[i] == note) {				// ヒットした
			kdch->r[i] = 0x80 | (KEYDISP_LEVEL - 2);
			kdch->flag |= 1;
			break;
		}
	}
}

static void chkeyoff(UINT ch) {

	UINT		i;
	KDCHANNEL	*kdch;

	kdch = keydisp.ch + ch;
	for (i=0; i<kdch->remain; i++) {
		if ((kdch->r[i] & (~0x80)) >= (KEYDISP_LEVEL - 1)) {
			kdch->r[i] = 0x80 | (KEYDISP_LEVEL - 2);
			kdch->flag |= 1;
		}
	}
}

static void keyalloff(void) {

	UINT8	i;

	for (i=0; i<KEYDISP_CHMAX; i++) {
		chkeyoff(i);
	}
}

static void keyallreload(void) {

	UINT	i;

	for (i=0; i<KEYDISP_CHMAX; i++) {
		keydisp.ch[i].flag = 2;
	}
}

static void keyallclear(void) {

	ZeroMemory(keydisp.ch, sizeof(keydisp.ch));
	keyallreload();
}


// ---- MIDI

void keydisp_midi(const UINT8 *cmd) {

	switch(cmd[0] & 0xf0) {
		case 0x80:
			keyoff(cmd[0] & 0x0f, cmd[1]);
			break;

		case 0x90:
			if (cmd[2] & 0x7f) {
				keyon(cmd[0] & 0x0f, cmd[1]);
			}
			else {
				keyoff(cmd[0] & 0x0f, cmd[1]);
			}
			break;

		case 0xb0:
			if ((cmd[1] == 0x7b) || (cmd[1] == 0x78)) {
				chkeyoff(cmd[0] & 0x0f);
			}
			break;

		case 0xf0:
			if (cmd[0] == 0xfe) {
				keyalloff();
			}
			break;
	}
}


// ---- draw

static int getdispkeys(void) {

	return(16);
}

static void clearrect(CMNVRAM *vram, int x, int y, int cx, int cy) {

	CMNPAL	col;

	switch(vram->bpp) {
#if defined(SUPPORT_8BPP)
		case 8:
			col.pal8 = keydisp.pal8[KEYDISP_PALBG];
			break;
#endif
#if defined(SUPPORT_16BPP)
		case 16:
			col.pal16 = keydisp.pal16[KEYDISP_LEVEL];
			break;
#endif
#if defined(SUPPORT_24BPP)
		case 24:
#endif
#if defined(SUPPORT_32BPP)
		case 32:
#endif
#if defined(SUPPORT_24BPP) || defined(SUPPORT_32BPP)
			col.pal32 = keydisp.pal32[KEYDISP_LEVEL];
			break;
#endif
		default:
			return;
	}
	cmndraw_fill(vram, x, y, cx, cy, col);
}

static void drawkeybg(CMNVRAM *vram) {

	CMNPAL	bg;
	CMNPAL	fg;
	int		i;

	switch(vram->bpp) {
#if defined(SUPPORT_8BPP)
		case 8:
			bg.pal8 = keydisp.pal8[KEYDISP_PALBG];
			fg.pal8 = keydisp.pal8[KEYDISP_PALFG];
			break;
#endif
#if defined(SUPPORT_16BPP)
		case 16:
			bg.pal16 = keydisp.pal16[KEYDISP_LEVEL];
			fg.pal16 = keydisp.pal16[0];
			break;
#endif
#if defined(SUPPORT_24BPP)
		case 24:
			bg.pal32 = keydisp.pal32[KEYDISP_LEVEL];
			fg.pal32 = keydisp.pal32[0];
			break;
#endif
#if defined(SUPPORT_32BPP)
		case 32:
			bg.pal32 = keydisp.pal32[KEYDISP_LEVEL];
			fg.pal32 = keydisp.pal32[0];
			break;
#endif
		default:
			return;
	}
	for (i=0; i<10; i++) {
		cmndraw_setpat(vram, keybrd1, i * KEYDISP_KEYCX, 0, bg, fg);
	}
	cmndraw_setpat(vram, keybrd2, 10 * KEYDISP_KEYCX, 0, bg, fg);
}

static BOOL draw1key(CMNVRAM *vram, KDCHANNEL *kdch, UINT n) {

	KDKEYPOS	*pos;
	UINT		pal;
	CMNPAL		fg;

	pos = keydisp.keypos + (kdch->k[n] & 0x7f);
	pal = kdch->r[n] & 0x7f;
	switch(vram->bpp) {
#if defined(SUPPORT_8BPP)
		case 8:
			if (pal != (KEYDISP_LEVEL - 1)) {
				fg.pal8 = keydisp.pal8[
									(pos->pals)?KEYDISP_PALBG:KEYDISP_PALFG];
				cmndraw_setfg(vram, pos->data, pos->posx, 0, fg);
				kdch->r[n] = 0;
				return(TRUE);
			}
			fg.pal8 = keydisp.pal8[KEYDISP_PALHIT];
			break;
#endif
#if defined(SUPPORT_16BPP)
		case 16:
			fg.pal16 = keydisp.pal16[pal + pos->pals];
			break;
#endif
#if defined(SUPPORT_24BPP)
		case 24:
			fg.pal32 = keydisp.pal32[pal + pos->pals];
			break;
#endif
#if defined(SUPPORT_32BPP)
		case 32:
			fg.pal32 = keydisp.pal32[pal + pos->pals];
			break;
#endif
		default:
			return(FALSE);
	}
	cmndraw_setfg(vram, pos->data, pos->posx, 0, fg);
	return(FALSE);
}

static BOOL draw1ch(CMNVRAM *vram, UINT8 framepast, KDCHANNEL *kdch) {

	BOOL	draw;
	UINT	i;
	BOOL	coll;
	UINT8	nextf;
	UINT	j;

	draw = FALSE;
	if (kdch->flag & 2) {
		drawkeybg(vram);
		draw = TRUE;
	}
	if (kdch->flag) {
		coll = FALSE;
		nextf = 0;
		for (i=0; i<kdch->remain; i++) {
			if ((kdch->r[i] & 0x80) || (kdch->flag & 2)) {
				kdch->r[i] &= ~0x80;
				if (kdch->r[i] < (KEYDISP_LEVEL - 1)) {
					if (kdch->r[i] > framepast) {
						kdch->r[i] -= framepast;
						kdch->r[i] |= 0x80;
						nextf = 1;
					}
					else {
						kdch->r[i] = 0;
						coll = TRUE;
					}
				}
				coll |= draw1key(vram, kdch, i);
				draw = TRUE;
			}
		}
		if (coll) {
			for (i=0; i<kdch->remain; i++) {
				if (!kdch->r[i]) {
					break;
				}
			}
			for (j=i; i<kdch->remain; i++) {
				if (kdch->r[i]) {
					kdch->k[j] = kdch->k[i];
					kdch->r[j] = kdch->r[i];
					j++;
				}
			}
			kdch->remain = j;
		}
		kdch->flag = nextf;
	}
	return(draw);
}


// ----

void keydisp_initialize(void) {

	int		r;
	UINT16	x;
	int		i;

	r = 0;
	x = 0;
	do {
		for (i=0; i<12 && r<128; i++, r++) {
			keydisp.keypos[r].posx = keyposdef[i].posx + x;
			keydisp.keypos[r].pals = keyposdef[i].pals;
			keydisp.keypos[r].data = keyposdef[i].data;
		}
		x += 28;
	} while(r < 128);
	keyallclear();
	keydisp.dispflag |= KEYDISP_FLAGREDRAW | KEYDISP_FLAGSIZING;
}

void keydisp_setpal(CMNPALFN *palfn) {

	UINT	i;
	RGB32	pal32[KEYDISP_PALS];

	if (palfn == NULL) {
		return;
	}
	if (palfn->get8) {
		for (i=0; i<KEYDISP_PALS; i++) {
			keydisp.pal8[i] = (*palfn->get8)(palfn, i);
		}
	}
	if (palfn->get32) {
		for (i=0; i<KEYDISP_PALS; i++) {
			pal32[i].d = (*palfn->get32)(palfn, i);
			cmndraw_makegrad(keydisp.pal32, KEYDISP_LEVEL,
								pal32[KEYDISP_PALFG], pal32[KEYDISP_PALHIT]);
			cmndraw_makegrad(keydisp.pal32 + KEYDISP_LEVEL, KEYDISP_LEVEL,
								pal32[KEYDISP_PALBG], pal32[KEYDISP_PALHIT]);
		}
		if (palfn->cnv16) {
			for (i=0; i<KEYDISP_LEVEL*2; i++) {
				keydisp.pal16[i] = (*palfn->cnv16)(palfn, keydisp.pal32[i]);
			}
		}
	}
	keydisp.dispflag |= KEYDISP_FLAGREDRAW;
}

UINT8 keydisp_process(UINT8 framepast) {

	int		keys;
	int		i;

	if (framepast) {
		keydisp.framepast += framepast;
	}
	keys = getdispkeys();
	for (i=0; i<keys; i++) {
		if (keydisp.ch[i].flag) {
			keydisp.dispflag |= KEYDISP_FLAGDRAW;
			break;
		}
	}
	return(keydisp.dispflag);
}

void keydisp_getsize(int *width, int *height) {

	if (width) {
		*width = KEYDISP_WIDTH;
	}
	if (height) {
		*height = (getdispkeys() * KEYDISP_KEYCY) + 1;
	}
	keydisp.dispflag &= ~KEYDISP_FLAGSIZING;
}

BOOL keydisp_paint(CMNVRAM *vram, BOOL redraw) {

	BOOL		draw;
	int			keys;
	int			i;
	KDCHANNEL	*p;
	UINT8* saveptr;

	saveptr = vram->ptr;
	draw = FALSE;
	if ((vram == NULL) ||
		(vram->width < KEYDISP_WIDTH) || (vram->height < 1)) {
		goto kdpnt_exit;
	}
	if (keydisp.dispflag & KEYDISP_FLAGREDRAW){
		redraw = TRUE;
	}
	if (redraw) {
		keyallreload();
		clearrect(vram, 0, 0, KEYDISP_WIDTH, 1);
		clearrect(vram, 0, 0, 1, vram->height);
		draw = TRUE;
	}
	vram->ptr += vram->xalign + vram->yalign;		// ptr (1, 1)
	keys = (vram->height - 1) / KEYDISP_KEYCY;
	keys = min(keys, getdispkeys());
	for (i=0, p=keydisp.ch; i<keys; i++, p++) {
		draw |= draw1ch(vram, keydisp.framepast, p);
		vram->ptr += KEYDISP_KEYCY * vram->yalign;
	}
	keydisp.dispflag &= ~(KEYDISP_FLAGDRAW | KEYDISP_FLAGREDRAW);
	keydisp.framepast = 0;

kdpnt_exit:
	if (vram) {
		vram->ptr = saveptr;
	}
	return(draw);
}
#endif

