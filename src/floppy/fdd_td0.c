/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Teledisk floppy image format.
 *
 * Version:	@(#)fdd_td0.c	1.0.10	2019/12/06
 *
 * Authors:	Milodrag Milanovic,
 *		Haruhiko OKUMURA,
 *		Haruyasu YOSHIZAKI,
 *		Kenji RIKITAKE,
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Based on Japanese version 29-NOV-1988
 *		LZSS coded by Haruhiko OKUMURA
 *		Adaptive Huffman Coding coded by Haruyasu YOSHIZAKI
 *		Edited and translated to English by Kenji RIKITAKE
 *
 *		Copyright 2013-2019 Milodrag Milanovic.
 *		Copyright 1988-2019 Haruhiko OKUMURA.
 *		Copyright 1988-2019 Haruyasu YOSHIZAKI.
 *		Copyright 1988-2019 Kenji RIKITAKE.
 *		Copyright 2016-2019 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../timer.h"
#include "../plat.h"
#include "fdd.h"
#include "fdd_86f.h"
#include "fdd_td0.h"
#include "fdc.h"


#define BUFSZ           512		/* new input buffer */
#define TD0_MAX_BUFSZ	(1024UL*1024UL*4UL)

/* LZSS Parameters */
#define N		4096		/* Size of string buffer */
#define F		60		/* Size of look-ahead buffer */
#define THRESHOLD	2
#define NIL		N		/* End of tree's node  */

/* Huffman coding parameters */
#define N_CHAR		(256-THRESHOLD+F)	/* code (= 0..N_CHAR-1) */
#define T		(N_CHAR*2-1)	/* Size of table */
#define R		(T-1)		/* root position */
#define MAX_FREQ	0x8000
					/* update when cumulative frequency */
					/* reaches to this value */

typedef struct {
    uint16_t	r,
		bufcnt,bufndx,bufpos,	/* string buffer */
					/* the following to allow block reads
					   from input in next_word() */
		ibufcnt,ibufndx;	/* input buffer counters */
    uint8_t	inbuf[BUFSZ];		/* input buffer */
} tdlzhuf;

typedef struct {
    FILE	*fdd_file;
    off_t	fdd_file_offset;

    tdlzhuf	tdctl;
    uint8_t	text_buf[N + F - 1];
    uint16_t	freq[T + 1];		/* cumulative freq table */

    /*
     * pointing parent nodes.
     * area [T..(T + N_CHAR - 1)] are pointers for leaves
     */
    int16_t	prnt[T + N_CHAR];

    /* pointing children nodes (son[], son[] + 1)*/
    int16_t	son[T];

    uint16_t	getbuf;
    uint8_t	getlen;
} td0dsk_t;

typedef struct {
    uint8_t	track;
    uint8_t	head;
    uint8_t	sector;
    uint8_t	size;
    uint8_t	flags;
    uint8_t	fm;
    uint8_t	*data;
} td0_sector_t;

typedef struct {
    FILE	*f;

    int		tracks;
    int		track_width;
    int		sides;
    uint16_t	disk_flags;
    uint16_t	default_track_flags;
    uint16_t	side_flags[256][2];
    uint8_t	max_sector_size;
    uint8_t	track_in_file[256][2];
    td0_sector_t sects[256][2][256];
    uint8_t	track_spt[256][2];
    uint8_t	gap3_len;
    uint16_t	current_side_flags[2];
    int		track;
    int		current_sector_index[2];
    uint8_t	calculated_gap3_lengths[256][2];
    uint8_t	xdf_ordered_pos[256][2];
    uint8_t	interleave_ordered_pos[256][2];

    uint8_t	*imagebuf;
    uint8_t	*processed_buf;
} td0_t;


/*
 * Tables for encoding/decoding upper 6 bits of
 * sliding dictionary pointer
 */
static const uint8_t d_code[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
    0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
    0x0C, 0x0C, 0x0C, 0x0C, 0x0D, 0x0D, 0x0D, 0x0D,
    0x0E, 0x0E, 0x0E, 0x0E, 0x0F, 0x0F, 0x0F, 0x0F,
    0x10, 0x10, 0x10, 0x10, 0x11, 0x11, 0x11, 0x11,
    0x12, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13, 0x13,
    0x14, 0x14, 0x14, 0x14, 0x15, 0x15, 0x15, 0x15,
    0x16, 0x16, 0x16, 0x16, 0x17, 0x17, 0x17, 0x17,
    0x18, 0x18, 0x19, 0x19, 0x1A, 0x1A, 0x1B, 0x1B,
    0x1C, 0x1C, 0x1D, 0x1D, 0x1E, 0x1E, 0x1F, 0x1F,
    0x20, 0x20, 0x21, 0x21, 0x22, 0x22, 0x23, 0x23,
    0x24, 0x24, 0x25, 0x25, 0x26, 0x26, 0x27, 0x27,
    0x28, 0x28, 0x29, 0x29, 0x2A, 0x2A, 0x2B, 0x2B,
    0x2C, 0x2C, 0x2D, 0x2D, 0x2E, 0x2E, 0x2F, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
};

static const uint8_t d_len[256] = {
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
};


static td0_t	*td0[FDD_NUM];


#ifdef ENABLE_TD0_LOG
int td0_do_log = ENABLE_TD0_LOG;


static void
td0_log(const char *fmt, ...)
{
   va_list ap;

   if (td0_do_log)
   {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
   }
}
#else
#define td0_log(fmt, ...)
#endif


static void
fdd_image_read(int drive, char *buffer, uint32_t offset, uint32_t len)
{
    td0_t *dev = td0[drive];

    fseek(dev->f, offset, SEEK_SET);
    fread(buffer, 1, len, dev->f);
}


static int
dsk_identify(int drive)
{
    char header[2];

    fdd_image_read(drive, header, 0, 2);
    if (header[0]=='T' && header[1]=='D')
	return(1);
      else if (header[0]=='t' && header[1]=='d')
	return(1);

    return(0);
}


static int
state_data_read(td0dsk_t *state, uint8_t *buf, uint16_t size)
{
    uint32_t image_size = 0;

    fseek(state->fdd_file, 0, SEEK_END);
    image_size = ftell(state->fdd_file);
    if (size > image_size - state->fdd_file_offset)
	size = (image_size - state->fdd_file_offset) & 0xffff;
    fseek(state->fdd_file, state->fdd_file_offset, SEEK_SET);
    fread(buf, 1, size, state->fdd_file);
    state->fdd_file_offset += size;

    return(size);
}


static int
state_next_word(td0dsk_t *state)
{
    if (state->tdctl.ibufndx >= state->tdctl.ibufcnt) {
	state->tdctl.ibufndx = 0;
	state->tdctl.ibufcnt = state_data_read(state, state->tdctl.inbuf,BUFSZ);
	if (state->tdctl.ibufcnt == 0)
		return(-1);
    }

    while (state->getlen <= 8) { /* typically reads a word at a time */
	state->getbuf |= state->tdctl.inbuf[state->tdctl.ibufndx++] << (8 - state->getlen);
	state->getlen += 8;
    }

    return(0);
}


/* get one bit */
static int
state_GetBit(td0dsk_t *state)
{
    int16_t i;

    if (state_next_word(state) < 0)
	return(-1);

    i = state->getbuf;
    state->getbuf <<= 1;
    state->getlen--;
    if (i < 0)
	return(1);

    return(0);
}


/* get a byte */
static int
state_GetByte(td0dsk_t *state)
{
    uint16_t i;

    if (state_next_word(state) != 0)
	return(-1);

    i = state->getbuf;
    state->getbuf <<= 8;
    state->getlen -= 8;
    i = i >> 8;

    return((int) i);
}


/* initialize freq tree */
static void
state_StartHuff(td0dsk_t *state)
{
    int i, j;

    for (i = 0; i < N_CHAR; i++) {
	state->freq[i] = 1;
	state->son[i] = i + T;
	state->prnt[i + T] = i;
    }
    i = 0; j = N_CHAR;
    while (j <= R) {
	state->freq[j] = state->freq[i] + state->freq[i + 1];
	state->son[j] = i;
	state->prnt[i] = state->prnt[i + 1] = j;
	i += 2; j++;
    }
    state->freq[T] = 0xffff;
    state->prnt[R] = 0;
}


/* reconstruct freq tree */
static void
state_reconst(td0dsk_t *state)
{
    int16_t i, j, k;
    uint16_t f, l;

    /* halven cumulative freq for leaf nodes */
    j = 0;
    for (i = 0; i < T; i++) {
	if (state->son[i] >= T) {
		state->freq[j] = (state->freq[i] + 1) / 2;
		state->son[j] = state->son[i];
		j++;
	}
    }

    /* make a tree : first, connect children nodes */
    for (i = 0, j = N_CHAR; j < T; i += 2, j++) {
	k = i + 1;
	f = state->freq[j] = state->freq[i] + state->freq[k];
	for (k = j - 1; f < state->freq[k]; k--) {};
	k++;
	l = (j - k) * 2;

	/* These *HAVE* to be memmove's as destination and source
	   can overlap, which memcpy can't handle. */
	memmove(&state->freq[k + 1], &state->freq[k], l);
	state->freq[k] = f;
	memmove(&state->son[k + 1], &state->son[k], l);
	state->son[k] = i;
    }

    /* connect parent nodes */
    for (i = 0; i < T; i++) {
	if ((k = state->son[i]) >= T)
		state->prnt[k] = i;
	  else
		state->prnt[k] = state->prnt[k + 1] = i;
    }
}


/* update freq tree */
static void
state_update(td0dsk_t *state, int c)
{
    int i, j, k, l;

    if (state->freq[R] == MAX_FREQ)
	state_reconst(state);

    c = state->prnt[c + T];

    /* do it until reaching the root */
    do {
	k = ++state->freq[c];

	/* swap nodes to keep the tree freq-ordered */
	if (k > state->freq[l = c + 1]) {
		while (k > state->freq[++l]) {};
		l--;
		state->freq[c] = state->freq[l];
		state->freq[l] = k;

		i = state->son[c];
		state->prnt[i] = l;
		if (i < T) state->prnt[i + 1] = l;

		j = state->son[l];
		state->son[l] = i;

		state->prnt[j] = c;
		if (j < T) state->prnt[j + 1] = c;
		state->son[c] = j;

		c = l;
	}
    } while ((c = state->prnt[c]) != 0);
}


static int16_t
state_DecodeChar(td0dsk_t *state)
{
    int ret;
    uint16_t c;

    c = state->son[R];

    /*
     * start searching tree from the root to leaves.
     * choose node #(son[]) if input bit == 0
     * else choose #(son[]+1) (input bit == 1)
     */
    while (c < T) {
	if ((ret = state_GetBit(state)) < 0)
		return(-1);
	c += (unsigned) ret;
	c = state->son[c];
    }
    c -= T;

    state_update(state, c);

    return(c);
}


static int16_t
state_DecodePosition(td0dsk_t *state)
{
    int16_t bit;
    uint16_t i, j, c;

    /* decode upper 6 bits from given table */
    if ((bit = state_GetByte(state)) < 0)
	return(-1);

    i = (uint16_t) bit;
    c = (uint16_t)d_code[i] << 6;
    j = d_len[i];

    /* input lower 6 bits directly */
    j -= 2;
    while (j--) {
	if ((bit = state_GetBit(state)) < 0)
		return(-1);
	i = (i << 1) + bit;
    }

    return(c | (i & 0x3f));
}


/* DeCompression - split out initialization code to init_Decode() */
static void
state_init_Decode(td0dsk_t *state)
{
    int i;

    state->getbuf = 0;
    state->getlen = 0;
    state->tdctl.ibufcnt= state->tdctl.ibufndx = 0; /* input buffer is empty */
    state->tdctl.bufcnt = 0;

    state_StartHuff(state);
    for (i = 0; i < N - F; i++)
	state->text_buf[i] = ' ';

    state->tdctl.r = N - F;
}


/* Decoding/Uncompressing */
static int
state_Decode(td0dsk_t *state, uint8_t *buf, int len)
{
    int16_t c, pos;
    int count;  /* was an unsigned long, seems unnecessary */

    for (count = 0; count < len; ) {
	if (state->tdctl.bufcnt == 0) {
		if ((c = state_DecodeChar(state)) < 0)
			return(count); /* fatal error */
		if (c < 256) {
			*(buf++) = c & 0xff;
			state->text_buf[state->tdctl.r++] = c & 0xff;
			state->tdctl.r &= (N - 1);
			count++;
		} else {
			if ((pos = state_DecodePosition(state)) < 0)
				return(count); /* fatal error */
			state->tdctl.bufpos = (state->tdctl.r - pos - 1) & (N - 1);
			state->tdctl.bufcnt = c - 255 + THRESHOLD;
			state->tdctl.bufndx = 0;
		}
	} else {
		/* still chars from last string */
		while (state->tdctl.bufndx < state->tdctl.bufcnt && count < len) {
			c = state->text_buf[(state->tdctl.bufpos + state->tdctl.bufndx) & (N - 1)];
			*(buf++) = c & 0xff;
			state->tdctl.bufndx++;
			state->text_buf[state->tdctl.r++] = c & 0xff;
			state->tdctl.r &= (N - 1);
			count++;
		}

		/* reset bufcnt after copy string from text_buf[] */
		if (state->tdctl.bufndx >= state->tdctl.bufcnt)
			state->tdctl.bufndx = state->tdctl.bufcnt = 0;
	}
    }

    return(count); /* count == len, success */
}


static uint32_t
get_raw_tsize(int side_flags, int slower_rpm)
{
    uint32_t size;

    switch(side_flags & 0x27) {
	case 0x22:
		size = slower_rpm ?  5314 :  5208;
		break;

	default:
	case 0x02:
	case 0x21:
		size = slower_rpm ?  6375 :  6250;
		break;

	case 0x01:
		size = slower_rpm ?  7650 :  7500;
		break;

	case 0x20:
		size = slower_rpm ? 10629 : 10416;
		break;

	case 0x00:
		size = slower_rpm ? 12750 : 12500;
		break;

	case 0x23:
		size = slower_rpm ? 21258 : 20833;
		break;

	case 0x03:
		size = slower_rpm ? 25500 : 25000;
		break;

	case 0x25:
		size = slower_rpm ? 42517 : 41666;
		break;

	case 0x05:
		size = slower_rpm ? 51000 : 50000;
		break;
    }

    return(size);
}


static int
td0_initialize(int drive)
{
    td0_t *dev = td0[drive];
    uint8_t header[12];
    int fm, head, track;
    int track_count = 0;
    int head_count = 0;
    int track_spt, track_spt_adjusted;
    int offset = 0;
    int density = 0;
    int temp_rate = 0;
    uint32_t file_size;
    uint16_t len, rep;
    td0dsk_t disk_decode;
    uint8_t *hs;
    uint16_t size;
    uint8_t *dbuf = dev->processed_buf;
    uint32_t total_size = 0;
    uint32_t id_field = 0;
    uint32_t pre_sector = 0;
    int32_t track_size = 0;
    int32_t raw_tsize = 0;
    uint32_t minimum_gap3 = 0;
    uint32_t minimum_gap4 = 0;
    int i, j, k;
    int size_diff, gap_sum;

    if (dev->f == NULL) {
	td0_log("TD0: Attempted to initialize without loading a file first\n");
	return(0);
    }

    fseek(dev->f, 0, SEEK_END);
    file_size = ftell(dev->f);

    if (file_size < 12) {
	td0_log("TD0: File is too small to even contain the header\n");
	return(0);
    }

    if (file_size > TD0_MAX_BUFSZ) {
	td0_log("TD0: File exceeds the maximum size\n");
	return(0);
    }

    fseek(dev->f, 0, SEEK_SET);
    fread(header, 1, 12, dev->f);
    head_count = header[9];

    if (header[0] == 't') {
	td0_log("TD0: File is compressed\n");
	disk_decode.fdd_file = dev->f;
	state_init_Decode(&disk_decode);
	disk_decode.fdd_file_offset = 12;
	state_Decode(&disk_decode, dev->imagebuf, TD0_MAX_BUFSZ);
    } else {
	td0_log("TD0: File is uncompressed\n");
	fseek(dev->f, 12, SEEK_SET);
	fread(dev->imagebuf, 1, file_size - 12, dev->f);
    }

    if (header[7] & 0x80)
	offset = 10 + dev->imagebuf[2] + (dev->imagebuf[3] << 8);

    track_spt = dev->imagebuf[offset];
    if (track_spt == 255) {
	/* Empty file? */
	td0_log("TD0: File has no tracks\n");
	return(0);
    }

    density = (header[5] >> 1) & 3;

    if (density == 3) {
	td0_log("TD0: Unknown density\n");
	return(0);
    }

    /*
     * We determine RPM from the drive type as well as we possibly can.
     * This byte is actually the BIOS floppy drive type read by Teledisk
     * from the CMOS.
     */
    switch (header[6]) {
	case 0:		/* 5.25" 360k in 1.2M drive:	360 rpm
			   CMOS Drive type: None, value probably
			   reused by Teledisk */
	case 2:		/* 5.25" 1.2M			360 rpm */
	case 5:		/* 8"/5.25"/3.5" 1.25M		360 rpm */
		dev->default_track_flags = (density == 1) ? 0x20 : 0x21;
		dev->max_sector_size = (density == 1) ? 6 : 5;		/* 8192 or 4096 bytes. */
		break;

	case 1:		/* 5.25" 360k:			300 rpm */
	case 3:		/* 3.5" 720k:			300 rpm */
		dev->default_track_flags = 0x02;
		dev->max_sector_size = 5;				/* 4096 bytes. */
		break;

	case 4:		/* 3.5" 1.44M:			300 rpm */
		dev->default_track_flags = (density == 1) ? 0x00 : 0x02;
		dev->max_sector_size = (density == 1) ? 6 : 5;		/* 8192 or 4096 bytes. */
		break;

	case 6:		/* 3.5" 2.88M:			300 rpm */
		dev->default_track_flags = (density == 1) ? 0x00 : ((density == 2) ? 0x03 : 0x02);
		dev->max_sector_size = (density == 1) ? 6 : ((density == 2) ? 7 : 5);	/* 16384, 8192, or 4096 bytes. */
		break;
    }

    dev->disk_flags = header[5] & 0x06;

    dev->track_width = (header[7] & 1) ^ 1;

    for (i = 0; i < 256; i++) {
	memset(dev->side_flags[i], 0, 4);
	memset(dev->track_in_file[i], 0, 2);
	memset(dev->calculated_gap3_lengths[i], 0, 2);
	for (j = 0; j < 2; j++)
		memset(dev->sects[i][j], 0, sizeof(td0_sector_t));
    }

    while (track_spt != 255) {
	track_spt_adjusted = track_spt;

	track = dev->imagebuf[offset + 1];
	head = dev->imagebuf[offset + 2] & 1;
	fm = (header[5] & 0x80) || (dev->imagebuf[offset + 2] & 0x80); /* ? */
	dev->side_flags[track][head] = dev->default_track_flags | (fm ? 0 : 8);
	dev->track_in_file[track][head] = 1;
	offset += 4;
	track_size = fm ? 73 : 146;
	if (density == 2)
		id_field = fm ? 54 : 63;
	else
		id_field = fm ? 35 : 44;
	pre_sector = id_field + (fm ? 7 : 16);

	for (i = 0; i < track_spt; i++) {
		hs = &dev->imagebuf[offset];
		offset += 6;

		dev->sects[track][head][i].track = hs[0];
		dev->sects[track][head][i].head	= hs[1];
		dev->sects[track][head][i].sector = hs[2];
		dev->sects[track][head][i].size	= hs[3];
		dev->sects[track][head][i].flags = hs[4];
		dev->sects[track][head][i].fm = !!fm;
		dev->sects[track][head][i].data	= dbuf;

		size = 128 << hs[3];
		if ((total_size + size) >= TD0_MAX_BUFSZ) {
			td0_log("TD0: Processed buffer overflow\n");
			return(0);
		}

		if (hs[4] & 0x30)
			memset(dbuf, (hs[4] & 0x10) ? 0xf6 : 0x00, size);
		else {
			offset += 3;
			switch (hs[8]) {
				default:
					td0_log("TD0: Image uses an unsupported sector data encoding: %i\n", hs[8]);
					return(0);

				case 0:
					memcpy(dbuf, &dev->imagebuf[offset], size);
					offset += size;
					break;

				case 1:
					offset += 4;
					k = (hs[9] + (hs[10] << 8)) * 2;
					k = (k <= size) ? k : size;
					for(j = 0; j < k; j += 2) {
						dbuf[j] = hs[11];
						dbuf[j + 1] = hs[12];
					}
					if (k < size)
						memset(&(dbuf[k]), 0, size - k);
					break;

				case 2:
					k = 0;
					while (k < size) {
						len = dev->imagebuf[offset];
						rep = dev->imagebuf[offset + 1];
						offset += 2;
						if (! len) {
							memcpy(&(dbuf[k]), &dev->imagebuf[offset], rep);
							offset += rep;
							k += rep;
						} else {
							len = (1 << len);
							rep = len * rep;
							rep = ((rep + k) <= size) ? rep : (size - k);
							for(j = 0; j < rep; j += len)
								memcpy(&(dbuf[j + k]), &dev->imagebuf[offset], len);
							k += rep;
							offset += len;
						}
					}
					break;
			}
		}

		dbuf += size;
		total_size += size;

		if (hs[4] & 0x20) {
			track_size += id_field;
			track_spt_adjusted--;
		} else if (hs[4] & 0x40)
			track_size += (pre_sector - id_field + 3);
		else {
			if ((hs[4] & 0x02) || (hs[3] > (dev->max_sector_size - fm)))
				track_size += (pre_sector + 3);
			else
				track_size += (pre_sector + size + 2);
		}
	}

	if (track > track_count)
		track_count = track;

	if (track_spt != 255) {
		dev->track_spt[track][head] = track_spt;

		if ((dev->track_spt[track][head] == 8) && (dev->sects[track][head][0].size == 3))
			dev->side_flags[track][head] = (dev->side_flags[track][head] & ~0x67) | 0x20;

		raw_tsize = get_raw_tsize(dev->side_flags[track][head], 0);
		minimum_gap3 = 12 * track_spt_adjusted;
		size_diff = raw_tsize - track_size;
		gap_sum = minimum_gap3 + minimum_gap4;
		if (size_diff < gap_sum) {
			/* If we can't fit the sectors with a reasonable minimum gap at perfect RPM, let's try 2% slower. */
			raw_tsize = get_raw_tsize(dev->side_flags[track][head], 1);
			/* Set disk flags so that rotation speed is 2% slower. */
			dev->disk_flags |= (3 << 5);
			size_diff = raw_tsize - track_size;
			if ((size_diff < gap_sum) && !fdd_get_turbo(drive)) {
				/* If we can't fit the sectors with a reasonable minimum gap even at 2% slower RPM, abort. */
				td0_log("TD0: Unable to fit the %i sectors into drive %i, track %i, side %i\n", track_spt_adjusted, drive, track, head);
				return 0;
			}
		}
		dev->calculated_gap3_lengths[track][head] = (size_diff - minimum_gap4) / track_spt_adjusted;

		track_spt = dev->imagebuf[offset];
	}
    }

    if ((dev->disk_flags & 0x60) == 0x60)
	td0_log("TD0: Disk will rotate 2% below perfect RPM\n");

    dev->tracks = track_count + 1;

    temp_rate = dev->default_track_flags & 7;
    if ((dev->default_track_flags & 0x27) == 0x20)
	temp_rate = 4;
    dev->gap3_len = gap3_sizes[temp_rate][dev->sects[0][0][0].size][dev->track_spt[0][0]];
    if (! dev->gap3_len)
	dev->gap3_len = dev->calculated_gap3_lengths[0][0];		/* If we can't determine the GAP3 length, assume the smallest one we possibly know of. */

    if (head_count == 2)
	dev->disk_flags |= 8;	/* 2 sides */

    if (dev->tracks <= 43)
	dev->track_width &= ~1;

    dev->sides = head_count;

    dev->current_side_flags[0] = dev->side_flags[0][0];
    dev->current_side_flags[1] = dev->side_flags[0][1];

    td0_log("TD0: File loaded: %i tracks, %i sides, disk flags: %02X, side flags: %02X, %02X, GAP3 length: %02X\n", dev->tracks, dev->sides, dev->disk_flags, dev->current_side_flags[0], dev->current_side_flags[1], dev->gap3_len);

    return(1);
}


static uint16_t
disk_flags(int drive)
{
    td0_t *dev = td0[drive];

    return(dev->disk_flags);
}


static uint16_t
side_flags(int drive)
{
    td0_t *dev = td0[drive];
    int side = 0;
    uint16_t sflags = 0;

    side = fdd_get_head(drive);
    sflags = dev->current_side_flags[side];

    return(sflags);
}


static void
set_sector(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n)
{
    td0_t *dev = td0[drive];
    int i = 0;

    dev->current_sector_index[side] = 0;
    if (c != dev->track)  return;
    for (i = 0; i < dev->track_spt[c][side]; i++) {
	if ((dev->sects[c][side][i].track == c) &&
	    (dev->sects[c][side][i].head == h) &&
	    (dev->sects[c][side][i].sector == r) &&
	    (dev->sects[c][side][i].size == n)) {
		dev->current_sector_index[side] = i;
	}
    }
}


static uint8_t
poll_read_data(int drive, int side, uint16_t pos)
{
    td0_t *dev = td0[drive];

    return(dev->sects[dev->track][side][dev->current_sector_index[side]].data[pos]);
}


static int
track_is_xdf(int drive, int side, int track)
{
    td0_t *dev = td0[drive];
    uint8_t id[4] = { 0, 0, 0, 0 };
    int i, effective_sectors, xdf_sectors;
    int high_sectors, low_sectors;
    int max_high_id, expected_high_count, expected_low_count;

    effective_sectors = xdf_sectors = high_sectors = low_sectors = 0;

    memset(dev->xdf_ordered_pos[side], 0, 256);

    if (! track) {
	if ((dev->track_spt[track][side] == 16) || (dev->track_spt[track][side] == 19)) {
		if (! side) {
			max_high_id = (dev->track_spt[track][side] == 19) ? 0x8B : 0x88;
			expected_high_count = (dev->track_spt[track][side] == 19) ? 0x0B : 0x08;
			expected_low_count = 8;
		} else {
			max_high_id = (dev->track_spt[track][side] == 19) ? 0x93 : 0x90;
			expected_high_count = (dev->track_spt[track][side] == 19) ? 0x13 : 0x10;
			expected_low_count = 0;
		}

		for (i = 0; i < dev->track_spt[track][side]; i++) {
			id[0] = dev->sects[track][side][i].track;
			id[1] = dev->sects[track][side][i].head;
			id[2] = dev->sects[track][side][i].sector;
			id[3] = dev->sects[track][side][i].size;
			if (!(id[0]) && (id[1] == side) && (id[3] == 2)) {
				if ((id[2] >= 0x81) && (id[2] <= max_high_id)) {
					high_sectors++;
					dev->xdf_ordered_pos[id[2]][side] = i;
				}

				if ((id[2] >= 0x01) && (id[2] <= 0x08)) {
					low_sectors++;
					dev->xdf_ordered_pos[id[2]][side] = i;
				}
			}
		}

		if ((high_sectors == expected_high_count) && (low_sectors == expected_low_count)) {
			dev->current_side_flags[side] = (dev->track_spt[track][side] == 19) ?  0x08 : 0x28;
			return((dev->track_spt[track][side] == 19) ? 2 : 1);
		}
	}
    } else {
	for (i = 0; i < dev->track_spt[track][side]; i++) {
		id[0] = dev->sects[track][side][i].track;
		id[1] = dev->sects[track][side][i].head;
		id[2] = dev->sects[track][side][i].sector;
		id[3] = dev->sects[track][side][i].size;
		effective_sectors++;
		if ((id[0] == track) && (id[1] == side) && !(id[2]) && !(id[3])) {
			effective_sectors--;
		}
		if ((id[0] == track) && (id[1] == side) && (id[2] == (id[3] | 0x80))) {
			xdf_sectors++;
			dev->xdf_ordered_pos[id[2]][side] = i;
		}
	}

	if ((effective_sectors == 3) && (xdf_sectors == 3)) {
		dev->current_side_flags[side] = 0x28;
		return(1);		/* 5.25" 2HD XDF */
	}

	if ((effective_sectors == 4) && (xdf_sectors == 4)) {
		dev->current_side_flags[side] = 0x08;
		return(2);		/* 3.5" 2HD XDF */
	}
    }

    return(0);
}


static int
track_is_interleave(int drive, int side, int track)
{
    td0_t *dev = td0[drive];
    int i, effective_sectors;
    int track_spt;

    effective_sectors = 0;

    for (i = 0; i < 256; i++)
	dev->interleave_ordered_pos[i][side] = 0;

    track_spt = dev->track_spt[track][side];

    if (track_spt != 21) return(0);

    for (i = 0; i < track_spt; i++) {
	if ((dev->sects[track][side][i].track == track) && (dev->sects[track][side][i].head == side) && (dev->sects[track][side][i].sector >= 1) && (dev->sects[track][side][i].sector <= track_spt) && (dev->sects[track][side][i].size == 2)) {
		effective_sectors++;
		dev->interleave_ordered_pos[dev->sects[track][side][i].sector][side] = i;
	}
    }

    if (effective_sectors == track_spt) return(1);

    return(0);
}


static void
td0_seek(int drive, int track)
{
    td0_t *dev = td0[drive];
    int side;
    uint8_t id[4] = { 0, 0, 0, 0 };
    int sector, current_pos;
    int ssize = 512;
    int track_rate = 0;
    int track_gap2 = 22;
    int track_gap3 = 12;
    int xdf_type = 0;
    int interleave_type = 0;
    int is_trackx = 0;
    int xdf_spt = 0;
    int xdf_sector = 0;
    int ordered_pos = 0;
    int real_sector = 0;
    int actual_sector = 0;
    int fm, sector_adjusted;

    if (dev->f == NULL) return;

    if (!dev->track_width && fdd_doublestep_40(drive))
	track /= 2;

    d86f_set_cur_track(drive, track);

    is_trackx = (track == 0) ? 0 : 1;
    dev->track = track;

    dev->current_side_flags[0] = dev->side_flags[track][0];
    dev->current_side_flags[1] = dev->side_flags[track][1];

    d86f_reset_index_hole_pos(drive, 0);
    d86f_reset_index_hole_pos(drive, 1);

    d86f_destroy_linked_lists(drive, 0);
    d86f_destroy_linked_lists(drive, 1);

    if (track > dev->tracks) {
	d86f_zero_track(drive);
	return;
    }

    for (side = 0; side < dev->sides; side++) {
	track_rate = dev->current_side_flags[side] & 7;
	/* Make sure 300 kbps @ 360 rpm is treated the same as 250 kbps @ 300 rpm. */
	if (!track_rate && (dev->current_side_flags[side] & 0x20))
		track_rate = 4;
	if ((dev->current_side_flags[side] & 0x27) == 0x21)
		track_rate = 2;
	track_gap3 = gap3_sizes[track_rate][dev->sects[track][side][0].size][dev->track_spt[track][side]];
	if (! track_gap3)
		track_gap3 = dev->calculated_gap3_lengths[track][side];

	track_gap2 = ((dev->current_side_flags[side] & 7) >= 3) ? 41 : 22;

	xdf_type = track_is_xdf(drive, side, track);

	interleave_type = track_is_interleave(drive, side, track);

	current_pos = d86f_prepare_pretrack(drive, side, 0);
	sector_adjusted = 0;

	if (! xdf_type) {
		for (sector = 0; sector < dev->track_spt[track][side]; sector++) {
			if (interleave_type == 0) {
				real_sector = dev->sects[track][side][sector].sector;
				actual_sector = sector;
			} else {
				real_sector = dmf_r[sector];
				actual_sector = dev->interleave_ordered_pos[real_sector][side];
			}

			id[0] = dev->sects[track][side][actual_sector].track;
			id[1] = dev->sects[track][side][actual_sector].head;
			id[2] = real_sector;
			id[3] = dev->sects[track][side][actual_sector].size;
			fm = dev->sects[track][side][actual_sector].fm;
			if (((dev->sects[track][side][actual_sector].flags & 0x42) || (id[3] > (dev->max_sector_size - fm))) && !fdd_get_turbo(drive))
				ssize = 3;
			else
				ssize = 128 << ((uint32_t) id[3]);
			current_pos = d86f_prepare_sector(drive, side, current_pos, id, dev->sects[track][side][actual_sector].data, ssize, track_gap2, track_gap3, dev->sects[track][side][actual_sector].flags);

			if (sector_adjusted == 0)
				d86f_initialize_last_sector_id(drive, id[0], id[1], id[2], id[3]);

			if (!(dev->sects[track][side][actual_sector].flags & 0x40))
				sector_adjusted++;
		}
	} else {
		xdf_type--;
		xdf_spt = xdf_physical_sectors[xdf_type][is_trackx];
		for (sector = 0; sector < xdf_spt; sector++) {
			xdf_sector = (side * xdf_spt) + sector;
			id[0] = track;
			id[1] = side;
			id[2] = xdf_disk_layout[xdf_type][is_trackx][xdf_sector].id.r;
			id[3] = is_trackx ? (id[2] & 7) : 2;
			ordered_pos = dev->xdf_ordered_pos[id[2]][side];
			fm = dev->sects[track][side][ordered_pos].fm;
			if (((dev->sects[track][side][ordered_pos].flags & 0x42) || (id[3] > (dev->max_sector_size - fm))) && !fdd_get_turbo(drive))
				ssize = 3;
			else
				ssize = 128 << ((uint32_t) id[3]);
			if (is_trackx)
				current_pos = d86f_prepare_sector(drive, side, xdf_trackx_spos[xdf_type][xdf_sector], id, dev->sects[track][side][ordered_pos].data, ssize, track_gap2, xdf_gap3_sizes[xdf_type][is_trackx], dev->sects[track][side][ordered_pos].flags);
			else
				current_pos = d86f_prepare_sector(drive, side, current_pos, id, dev->sects[track][side][ordered_pos].data, ssize, track_gap2, xdf_gap3_sizes[xdf_type][is_trackx], dev->sects[track][side][ordered_pos].flags);

			if (sector_adjusted == 0)
				d86f_initialize_last_sector_id(drive, id[0], id[1], id[2], id[3]);

			if (!(dev->sects[track][side][ordered_pos].flags & 0x40))
				sector_adjusted++;
		}
	}
    }
}


void
td0_init(void)
{
    memset(td0, 0x00, sizeof(td0));
}


void
td0_abort(int drive)
{
    td0_t *dev = td0[drive];

    if (dev->imagebuf)
	free(dev->imagebuf);
    if (dev->processed_buf)
	free(dev->processed_buf);
    if (dev->f)
	fclose(dev->f);
    memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
    free(dev);
    td0[drive] = NULL;
}


void
td0_load(int drive, wchar_t *fn)
{
    td0_t *dev;
    uint32_t i;

    d86f_unregister(drive);

    writeprot[drive] = 1;

    dev = (td0_t *)malloc(sizeof(td0_t));
    memset(dev, 0x00, sizeof(td0_t));
    td0[drive] = dev;

    dev->f = plat_fopen(fn, L"rb");
    if (dev->f == NULL) {
	memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
	return;
    }

    fwriteprot[drive] = writeprot[drive];

    if (! dsk_identify(drive)) {
	td0_log("TD0: Not a valid Teledisk image\n");
	td0_abort(drive);
	return;
    } else {
	td0_log("TD0: Valid Teledisk image\n");
    }

    /* Allocate the processing buffers. */
    i = 1024UL * 1024UL * 4UL;
    dev->imagebuf = (uint8_t *)malloc(i);
    memset(dev->imagebuf, 0x00, i);
    dev->processed_buf = (uint8_t *)malloc(i);
    memset(dev->processed_buf, 0x00, i);

    if (! td0_initialize(drive)) {
	td0_log("TD0: Failed to initialize\n");
	td0_abort(drive);
	return;
    } else {
	td0_log("TD0: Initialized successfully\n");
    }

    /* Attach this format to the D86F engine. */
    d86f_handler[drive].disk_flags = disk_flags;
    d86f_handler[drive].side_flags = side_flags;
    d86f_handler[drive].writeback = null_writeback;
    d86f_handler[drive].set_sector = set_sector;
    d86f_handler[drive].read_data = poll_read_data;
    d86f_handler[drive].write_data = null_write_data;
    d86f_handler[drive].format_conditions = null_format_conditions;
    d86f_handler[drive].extra_bit_cells = null_extra_bit_cells;
    d86f_handler[drive].encoded_data = common_encoded_data;
    d86f_handler[drive].read_revolution = common_read_revolution;
    d86f_handler[drive].index_hole_pos = null_index_hole_pos;
    d86f_handler[drive].get_raw_size = common_get_raw_size;
    d86f_handler[drive].check_crc = 1;
    d86f_set_version(drive, 0x0063);

    drives[drive].seek = td0_seek;

    d86f_common_handlers(drive);
}


void
td0_close(int drive)
{
    td0_t *dev = td0[drive];
    int i, j, k;

    if (dev == NULL) return;

    d86f_unregister(drive);

    if (dev->imagebuf)
	free(dev->imagebuf);
    if (dev->processed_buf)
	free(dev->processed_buf);

    for (i = 0; i < 256; i++) {
	for (j = 0; j < 2; j++) {
		for (k = 0; k < 256; k++)
			dev->sects[i][j][k].data = NULL;
	}
    }

    for (i = 0; i < 256; i++) {
	memset(dev->side_flags[i], 0, 4);
	memset(dev->track_in_file[i], 0, 2);
	memset(dev->calculated_gap3_lengths[i], 0, 2);
	for (j = 0; j < 2; j++)
		memset(dev->sects[i][j], 0, sizeof(td0_sector_t));
    }

    if (dev->f != NULL)
	fclose(dev->f);

    /* Release resources. */
    free(dev);
    td0[drive] = NULL;
}
