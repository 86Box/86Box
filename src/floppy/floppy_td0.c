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
 * Version:	@(#)floppy_td0.c	1.0.6	2017/11/04
 *
 * Authors:	Milodrag Milanovic,
 *		Haruhiko OKUMURA,
 *		Haruyasu YOSHIZAKI,
 *		Kenji RIKITAKE,
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 1988-2017 Haruhiko OKUMURA.
 *		Copyright 1988-2017 Haruyasu YOSHIZAKI.
 *		Copyright 1988-2017 Kenji RIKITAKE.
 *		Copyright 2013-2017 Milodrag Milanovic.
 *		Copyright 2016-2017 Miran Grca.
 */

/* license:BSD-3-Clause
   copyright-holders:Miodrag Milanovic, Miran Grca (translation to C and port to 86Box) */
/*********************************************************************

    formats/td0_dsk.c

    TD0 disk images

*********************************************************************/
/*
 * Based on Japanese version 29-NOV-1988
 * LZSS coded by Haruhiko OKUMURA
 * Adaptive Huffman Coding coded by Haruyasu YOSHIZAKI
 * Edited and translated to English by Kenji RIKITAKE
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../plat.h"
#include "floppy.h"
#include "floppy_td0.h"
#include "fdc.h"
#include "fdd.h"


#define BUFSZ           512     /* new input buffer */

/* LZSS Parameters */

#define N        4096    /* Size of string buffer */
#define F        60    /* Size of look-ahead buffer */
#define THRESHOLD    2
#define NIL        N    /* End of tree's node  */


/* Huffman coding parameters */

#define N_CHAR      (256 - THRESHOLD + F)
				/* character code (= 0..N_CHAR-1) */
#define T         (N_CHAR * 2 - 1)    /* Size of table */
#define R         (T - 1)            /* root position */
#define MAX_FREQ    0x8000
					/* update when cumulative frequency */
					/* reaches to this value */

typedef struct {
	uint16_t r,
					bufcnt,bufndx,bufpos,  /* string buffer */
				/* the following to allow block reads from input in next_word() */
					ibufcnt,ibufndx; /* input buffer counters */
	uint8_t  inbuf[BUFSZ];    /* input buffer */
} tdlzhuf;

typedef struct
{
	FILE *floppy_file;
	uint64_t floppy_file_offset;

	tdlzhuf tdctl;
	uint8_t text_buf[N + F - 1];
	uint16_t freq[T + 1];    /* cumulative freq table */

/*
 * pointing parent nodes.
 * area [T..(T + N_CHAR - 1)] are pointers for leaves
 */
	int16_t prnt[T + N_CHAR];

	/* pointing children nodes (son[], son[] + 1)*/
	int16_t son[T];

	uint16_t getbuf;
	uint8_t getlen;
} td0dsk_t;

typedef struct
{
	uint8_t track;
	uint8_t head;
	uint8_t sector;
	uint8_t size;
	uint8_t deleted;
	uint8_t bad_crc;
	uint8_t *data;
} td0_sector_t;

typedef struct
{
	FILE *f;

	int tracks;
	int track_width;
	int sides;
	uint16_t disk_flags;
	uint16_t default_track_flags;
	uint16_t side_flags[256][2];
	uint8_t track_in_file[256][2];
	td0_sector_t sects[256][2][256];
	uint8_t track_spt[256][2];
	uint8_t gap3_len;
	uint16_t current_side_flags[2];
	int track;
	int current_sector_index[2];
	uint8_t calculated_gap3_lengths[256][2];
	uint8_t xdf_ordered_pos[256][2];
	uint8_t interleave_ordered_pos[256][2];
} td0_t;

td0_t td0[FDD_NUM];


void floppy_image_read(int drive, char *buffer, uint32_t offset, uint32_t len)
{
	fseek(td0[drive].f, offset, SEEK_SET);
	fread(buffer, 1, len, td0[drive].f);
}

int td0_dsk_identify(int drive)
{
	char header[2];

	floppy_image_read(drive, header, 0, 2);
	if (header[0]=='T' && header[1]=='D') {
		return 1;
	} else if (header[0]=='t' && header[1]=='d') {
		return 1;
	} else {
		return 0;
	}
}

int td0_state_data_read(td0dsk_t *state, uint8_t *buf, uint16_t size)
{
	uint32_t image_size = 0;
	fseek(state->floppy_file, 0, SEEK_END);
	image_size = ftell(state->floppy_file);
	if (size > image_size - state->floppy_file_offset) {
		size = image_size - state->floppy_file_offset;
	}
	fseek(state->floppy_file, state->floppy_file_offset, SEEK_SET);
	fread(buf, 1, size, state->floppy_file);
	state->floppy_file_offset += size;
	return size;
}


/*
 * Tables for encoding/decoding upper 6 bits of
 * sliding dictionary pointer
 */

/* decoder table */
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

int td0_state_next_word(td0dsk_t *state)
{
	if(state->tdctl.ibufndx >= state->tdctl.ibufcnt)
	{
		state->tdctl.ibufndx = 0;
		state->tdctl.ibufcnt = td0_state_data_read(state, state->tdctl.inbuf,BUFSZ);
		if(state->tdctl.ibufcnt == 0)
			return(-1);
	}
	while (state->getlen <= 8) { /* typically reads a word at a time */
		state->getbuf |= state->tdctl.inbuf[state->tdctl.ibufndx++] << (8 - state->getlen);
		state->getlen += 8;
	}
	return(0);
}


int td0_state_GetBit(td0dsk_t *state)    /* get one bit */
{
	int16_t i;
	if(td0_state_next_word(state) < 0)
		return(-1);
	i = state->getbuf;
	state->getbuf <<= 1;
	state->getlen--;
	if(i < 0)
		return(1);
	else
		return(0);
}

int td0_state_GetByte(td0dsk_t *state)    /* get a byte */
{
	uint16_t i;
	if(td0_state_next_word(state) != 0)
		return(-1);
	i = state->getbuf;
	state->getbuf <<= 8;
	state->getlen -= 8;
	i = i >> 8;
	return((int) i);
}



/* initialize freq tree */

void td0_state_StartHuff(td0dsk_t *state)
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

void td0_state_reconst(td0dsk_t *state)
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

		/* movmem() is Turbo-C dependent
		   rewritten to memmove() by Kenji */

		/* movmem(&freq[k], &freq[k + 1], l); */
		(void)memmove(&state->freq[k + 1], &state->freq[k], l);
		state->freq[k] = f;
		/* movmem(&son[k], &son[k + 1], l); */
		(void)memmove(&state->son[k + 1], &state->son[k], l);
		state->son[k] = i;
	}
	/* connect parent nodes */
	for (i = 0; i < T; i++) {
		if ((k = state->son[i]) >= T) {
			state->prnt[k] = i;
		} else {
			state->prnt[k] = state->prnt[k + 1] = i;
		}
	}
}


/* update freq tree */

void td0_state_update(td0dsk_t *state, int c)
{
	int i, j, k, l;

	if (state->freq[R] == MAX_FREQ) {
		td0_state_reconst(state);
	}
	c = state->prnt[c + T];
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
	} while ((c = state->prnt[c]) != 0);    /* do it until reaching the root */
}


int16_t td0_state_DecodeChar(td0dsk_t *state)
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
		if((ret = td0_state_GetBit(state)) < 0)
			return(-1);
		c += (unsigned) ret;
		c = state->son[c];
	}
	c -= T;
	td0_state_update(state, c);
	return c;
}

int16_t td0_state_DecodePosition(td0dsk_t *state)
{
	int16_t bit;
	uint16_t i, j, c;

	/* decode upper 6 bits from given table */
	if((bit=td0_state_GetByte(state)) < 0)
		return(-1);
	i = (uint16_t) bit;
	c = (uint16_t)d_code[i] << 6;
	j = d_len[i];

	/* input lower 6 bits directly */
	j -= 2;
	while (j--) {
		if((bit = td0_state_GetBit(state)) < 0)
			return(-1);
		i = (i << 1) + bit;
	}
	return(c | (i & 0x3f));
}

/* DeCompression

split out initialization code to init_Decode()

*/

void td0_state_init_Decode(td0dsk_t *state)
{
	int i;
	state->getbuf = 0;
	state->getlen = 0;
	state->tdctl.ibufcnt= state->tdctl.ibufndx = 0; /* input buffer is empty */
	state->tdctl.bufcnt = 0;
	td0_state_StartHuff(state);
	for (i = 0; i < N - F; i++)
		state->text_buf[i] = ' ';
	state->tdctl.r = N - F;
}


int td0_state_Decode(td0dsk_t *state, uint8_t *buf, int len)  /* Decoding/Uncompressing */
{
	int16_t c,pos;
	int  count;  /* was an unsigned long, seems unnecessary */
	for (count = 0; count < len; ) {
			if(state->tdctl.bufcnt == 0) {
				if((c = td0_state_DecodeChar(state)) < 0)
					return(count); /* fatal error */
				if (c < 256) {
					*(buf++) = c;
					state->text_buf[state->tdctl.r++] = c;
					state->tdctl.r &= (N - 1);
					count++;
				}
				else {
					if((pos = td0_state_DecodePosition(state)) < 0)
							return(count); /* fatal error */
					state->tdctl.bufpos = (state->tdctl.r - pos - 1) & (N - 1);
					state->tdctl.bufcnt = c - 255 + THRESHOLD;
					state->tdctl.bufndx = 0;
				}
			}
			else { /* still chars from last string */
				while( state->tdctl.bufndx < state->tdctl.bufcnt && count < len ) {
					c = state->text_buf[(state->tdctl.bufpos + state->tdctl.bufndx) & (N - 1)];
					*(buf++) = c;
					state->tdctl.bufndx++;
					state->text_buf[state->tdctl.r++] = c;
					state->tdctl.r &= (N - 1);
					count++;
				}
				/* reset bufcnt after copy string from text_buf[] */
				if(state->tdctl.bufndx >= state->tdctl.bufcnt)
					state->tdctl.bufndx = state->tdctl.bufcnt = 0;
			}
	}
	return(count); /* count == len, success */
}


/*********************************************************************

    formats/td0_dsk.h

    Teledisk disk images

*********************************************************************/

int td0_initialize(int drive);

void td0_seek(int drive, int track);

void td0_init()
{
        memset(td0, 0, sizeof(td0));
}

void d86f_register_td0(int drive);

const int max_size = 4*1024*1024; /* 4MB ought to be large enough for any floppy */
const int max_processed_size = 5*1024*1024;
uint8_t imagebuf[4*1024*1024];
uint8_t processed_buf[5*1024*1024];
uint8_t header[12];

void td0_load(int drive, wchar_t *fn)
{
	int ret = 0;

	d86f_unregister(drive);

	writeprot[drive] = 1;
        td0[drive].f = plat_fopen(fn, L"rb");
        if (!td0[drive].f)
        {
		memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
		return;
        }
        fwriteprot[drive] = writeprot[drive];

	ret = td0_dsk_identify(drive);
	if (!ret)
	{
		pclog("TD0: Not a valid Teledisk image\n");
		fclose(td0[drive].f);
		td0[drive].f = NULL;
		memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
		return;
	}
	else
	{
		pclog("TD0: Valid Teledisk image\n");
	}

	memset(imagebuf, 0, 4*1024*1024);
	memset(processed_buf, 0, 4*1024*1024);
	ret = td0_initialize(drive);
	if (!ret)
	{
		pclog("TD0: Failed to initialize\n");
		fclose(td0[drive].f);
		td0[drive].f = NULL;
		memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
		return;
	}
	else
	{
		pclog("TD0: Initialized successfully\n");
	}

	d86f_register_td0(drive);

        drives[drive].seek        = td0_seek;

	d86f_common_handlers(drive);
}

void td0_close(int drive)
{
	int i = 0;
	int j = 0;
	int k = 0;
	d86f_unregister(drive);
	memset(imagebuf, 0, 4*1024*1024);
	memset(processed_buf, 0, 4*1024*1024);

	for (i = 0; i < 256; i++)
	{
		for (j = 0; j < 2; j++)
		{
			for (k = 0; k < 256; k++)
			{
				td0[drive].sects[i][j][k].data = NULL;
			}
		}
	}

	for (i = 0; i < 256; i++)
	{
		memset(td0[drive].side_flags[i], 0, 4);
		memset(td0[drive].track_in_file[i], 0, 2);
		memset(td0[drive].calculated_gap3_lengths[i], 0, 2);
		for (j = 0; j < 2; j++)
		{
			memset(td0[drive].sects[i][j], 0, sizeof(td0_sector_t));
		}
	}

        if (td0[drive].f)
	{
                fclose(td0[drive].f);
		td0[drive].f = NULL;
	}
}

uint32_t td0_get_raw_tsize(int side_flags, int slower_rpm)
{
	uint32_t size;
	switch(side_flags & 0x27)
	{
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
	return size;
}

int td0_initialize(int drive)
{
	int track;
	int head;
	int fm;
	int track_count = 0;
	int head_count = 0;
	int track_spt;
	int offset = 0;
	int density = 0;
	int i = 0;
	int j = 0;
	int k = 0;
	int temp_rate = 0;
	uint32_t file_size;
	uint16_t len;
	uint16_t rep;
	td0dsk_t disk_decode;
	uint8_t *hs;
	uint16_t size;
	uint8_t *dbuf = processed_buf;
	uint32_t total_size = 0;
	uint32_t pre_sector = 0;
	uint32_t track_size = 0;
	uint32_t raw_tsize = 0;
	uint32_t minimum_gap3 = 0;
	uint32_t minimum_gap4 = 0;

        if (!td0[drive].f)
	{
		pclog("TD0: Attempted to initialize without loading a file first\n");
                return 0;
	}

	fseek(td0[drive].f, 0, SEEK_END);
	file_size = ftell(td0[drive].f);

	if (file_size < 12)
	{
		pclog("TD0: File is too small to even contain the header\n");
		return 0;
	}

	if (file_size > max_size)
	{
		pclog("TD0: File exceeds the maximum size\n");
		return 0;
	}

	fseek(td0[drive].f, 0, SEEK_SET);
	fread(header, 1, 12, td0[drive].f);
	head_count = header[9];

	if(header[0] == 't')
	{
		pclog("TD0: File is compressed\n");
		disk_decode.floppy_file = td0[drive].f;
		td0_state_init_Decode(&disk_decode);
		disk_decode.floppy_file_offset = 12;
		td0_state_Decode(&disk_decode, imagebuf, max_size);
	}
	else
	{
		pclog("TD0: File is uncompressed\n");
		fseek(td0[drive].f, 12, SEEK_SET);
		fread(imagebuf, 1, file_size - 12, td0[drive].f);
	}

	if(header[7] & 0x80)
		offset = 10 + imagebuf[2] + (imagebuf[3] << 8);

	track_spt = imagebuf[offset];
	if(track_spt == 255) /* Empty file? */
	{
		pclog("TD0: File has no tracks\n");
		return 0;
	}

	density = (header[5] >> 1) & 3;

	if (density == 3)
	{
		pclog("TD0: Unknown density\n");
		return 0;
	}

	/* We determine RPM from the drive type as well as we possibly can. */
	/* This byte is actually the BIOS floppy drive type read by Teledisk from the CMOS. */
	switch(header[6])
	{
		case 0:					/* 5.25" 360k in 1.2M drive:	360 rpm
							   CMOS Drive type: None, value probably reused by Teledisk */
		case 2:					/* 5.25" 1.2M			360 rpm */
		case 5:					/* 8"/5.25"/3.5" 1.25M		360 rpm */
			td0[drive].default_track_flags = (density == 1) ? 0x20 : 0x21;
			break;
		case 1:					/* 5.25" 360k:			300 rpm */
		case 3:					/* 3.5" 720k:			300 rpm */
			td0[drive].default_track_flags = 0x02;
			break;
		case 4:					/* 3.5" 1.44M:			300 rpm */
			td0[drive].default_track_flags = (density == 1) ? 0x00 : 0x02;
			break;
		case 6:					/* 3.5" 2.88M:			300 rpm */
			td0[drive].default_track_flags = (density == 1) ? 0x00 : ((density == 2) ? 0x03 : 0x02);
			break;
	}

	td0[drive].disk_flags = header[5] & 0x06;

	td0[drive].track_width = (header[7] & 1) ^ 1;

	for (i = 0; i < 256; i++)
	{
		memset(td0[drive].side_flags[i], 0, 4);
		memset(td0[drive].track_in_file[i], 0, 2);
		memset(td0[drive].calculated_gap3_lengths[i], 0, 2);
		for (j = 0; j < 2; j++)
		{
			memset(td0[drive].sects[i][j], 0, sizeof(td0_sector_t));
		}
	}

	while(track_spt != 255)
	{
		track = imagebuf[offset + 1];
		head = imagebuf[offset + 2] & 1;
		fm = (header[5] & 0x80) || (imagebuf[offset + 2] & 0x80); /* ? */
		td0[drive].side_flags[track][head] = td0[drive].default_track_flags | (fm ? 0 : 8);
		td0[drive].track_in_file[track][head] = 1;
		offset += 4;
		track_size = fm ? 73 : 146;
		pre_sector = fm ? 42 : 60;
		
		for(i = 0; i < track_spt; i++)
		{
			hs = &imagebuf[offset];
			offset += 6;

			td0[drive].sects[track][head][i].track       = hs[0];
			td0[drive].sects[track][head][i].head        = hs[1];
			td0[drive].sects[track][head][i].sector      = hs[2];
			td0[drive].sects[track][head][i].size        = hs[3];
			td0[drive].sects[track][head][i].deleted     = (hs[4] & 4) == 4;
			td0[drive].sects[track][head][i].bad_crc     = (hs[4] & 2) == 2;
			td0[drive].sects[track][head][i].data        = dbuf;

			size = 128 << hs[3];
			if ((total_size + size) >= max_processed_size)
			{
				pclog("TD0: Processed buffer overflow\n");
				fclose(td0[drive].f);
				return 0;
			}

			if(hs[4] & 0x30)
			{
				memset(dbuf, 0, size);
			}
			else
			{
				offset += 3;
				switch(hs[8])
				{
					default:
						pclog("TD0: Image uses an unsupported sector data encoding\n");
						fclose(td0[drive].f);
						return 0;
					case 0:
						memcpy(dbuf, &imagebuf[offset], size);
						offset += size;
						break;
					case 1:
						offset += 4;
						k = (hs[9] + (hs[10] << 8)) * 2;
						k = (k <= size) ? k : size;
						for(j = 0; j < k; j += 2)
						{
							dbuf[j] = hs[11];
							dbuf[j + 1] = hs[12];
						}
						if(k < size)
							memset(&(dbuf[k]), 0, size - k);
						break;
					case 2:
						k = 0;
						while(k < size)
						{
							len = imagebuf[offset];
							rep = imagebuf[offset + 1];
							offset += 2;
							if(!len)
							{
								memcpy(&(dbuf[k]), &imagebuf[offset], rep);
								offset += rep;
								k += rep;
							}
							else
							{
								len = (1 << len);
								rep = len * rep;
								rep = ((rep + k) <= size) ? rep : (size - k);
								for(j = 0; j < rep; j += len)
									memcpy(&(dbuf[j + k]), &imagebuf[offset], len);
								k += rep;
								offset += len;
							}
						}
						break;
				}
			}

			dbuf += size;
			total_size += size;
			track_size += (pre_sector + size + 2);
		}

		track_count = track;

		if (track_spt != 255)
		{
			td0[drive].track_spt[track][head] = track_spt;

			if ((td0[drive].track_spt[track][head] == 8) && (td0[drive].sects[track][head][0].size == 3))
			{
				td0[drive].side_flags[track][head] |= 0x20;
			}

			raw_tsize = td0_get_raw_tsize(td0[drive].side_flags[track][head], 0);
			minimum_gap3 = 12 * track_spt;
			if ((raw_tsize - track_size + (fm ? 73 : 146)) < (minimum_gap3 + minimum_gap4))
			{
				/* If we can't fit the sectors with a reasonable minimum gap at perfect RPM, let's try 2% slower. */
				raw_tsize = td0_get_raw_tsize(td0[drive].side_flags[track][head], 1);
				/* Set disk flags so that rotation speed is 2% slower. */
				td0[drive].disk_flags |= (3 << 5);
				if ((raw_tsize - track_size + (fm ? 73 : 146)) < (minimum_gap3 + minimum_gap4))
				{
					/* If we can't fit the sectors with a reasonable minimum gap even at 2% slower RPM, abort. */
					pclog("TD0: Unable to fit the %i sectors in a track\n", track_spt);
					return 0;
				}
			}
			td0[drive].calculated_gap3_lengths[track][head] = (raw_tsize - track_size - minimum_gap4 + (fm ? 73 : 146)) / track_spt;

			track_spt = imagebuf[offset];
		}
	}

	if ((td0[drive].disk_flags & 0x60) == 0x60)
	{
		pclog("TD0: Disk will rotate 2% below perfect RPM\n");
	}

	td0[drive].tracks = track_count + 1;

	temp_rate = td0[drive].default_track_flags & 7;
	if ((td0[drive].default_track_flags & 0x27) == 0x20)  temp_rate = 4;
	td0[drive].gap3_len = gap3_sizes[temp_rate][td0[drive].sects[0][0][0].size][td0[drive].track_spt[0][0]];
	if (!td0[drive].gap3_len)
	{
		td0[drive].gap3_len = td0[drive].calculated_gap3_lengths[0][0];		/* If we can't determine the GAP3 length, assume the smallest one we possibly know of. */
	}

	if(head_count == 2)
	{
		td0[drive].disk_flags |= 8;	/* 2 sides */
	}

	if (td0[drive].tracks <= 43)
	{
		td0[drive].track_width &= ~1;
	}

	td0[drive].sides = head_count;

	td0[drive].current_side_flags[0] = td0[drive].side_flags[0][0];
	td0[drive].current_side_flags[1] = td0[drive].side_flags[0][1];

	pclog("TD0: File loaded: %i tracks, %i sides, disk flags: %02X, side flags: %02X, %02X, GAP3 length: %02X\n", td0[drive].tracks, td0[drive].sides, td0[drive].disk_flags, td0[drive].current_side_flags[0], td0[drive].current_side_flags[1], td0[drive].gap3_len);

	return 1;
}

int td0_track_is_xdf(int drive, int side, int track)
{
	uint8_t id[4] = { 0, 0, 0, 0 };
	int i, effective_sectors, xdf_sectors;
	int high_sectors, low_sectors;
	int max_high_id, expected_high_count, expected_low_count;

	effective_sectors = xdf_sectors = high_sectors = low_sectors = 0;

	memset(td0[drive].xdf_ordered_pos[side], 0, 256);

	if (!track)
	{
		if ((td0[drive].track_spt[track][side] == 16) || (td0[drive].track_spt[track][side] == 19))
		{
			if (!side)
			{
				max_high_id = (td0[drive].track_spt[track][side] == 19) ? 0x8B : 0x88;
				expected_high_count = (td0[drive].track_spt[track][side] == 19) ? 0x0B : 0x08;
				expected_low_count = 8;
			}
			else
			{
				max_high_id = (td0[drive].track_spt[track][side] == 19) ? 0x93 : 0x90;
				expected_high_count = (td0[drive].track_spt[track][side] == 19) ? 0x13 : 0x10;
				expected_low_count = 0;
			}
			for (i = 0; i < td0[drive].track_spt[track][side]; i++)
			{
				id[0] = td0[drive].sects[track][side][i].track;
				id[1] = td0[drive].sects[track][side][i].head;
				id[2] = td0[drive].sects[track][side][i].sector;
				id[3] = td0[drive].sects[track][side][i].size;
				if (!(id[0]) && (id[1] == side) && (id[3] == 2))
				{
					if ((id[2] >= 0x81) && (id[2] <= max_high_id))
					{
						high_sectors++;
						td0[drive].xdf_ordered_pos[id[2]][side] = i;
					}
					if ((id[2] >= 0x01) && (id[2] <= 0x08))
					{
						low_sectors++;
						td0[drive].xdf_ordered_pos[id[2]][side] = i;
					}
				}
			}
			if ((high_sectors == expected_high_count) && (low_sectors == expected_low_count))
			{
				td0[drive].current_side_flags[side] = (td0[drive].track_spt[track][side] == 19) ?  0x08 : 0x28;
				return (td0[drive].track_spt[track][side] == 19) ? 2 : 1;
			}
			return 0;
		}
		else
		{
			return 0;
		}
	}
	else
	{
		for (i = 0; i < td0[drive].track_spt[track][side]; i++)
		{
			id[0] = td0[drive].sects[track][side][i].track;
			id[1] = td0[drive].sects[track][side][i].head;
			id[2] = td0[drive].sects[track][side][i].sector;
			id[3] = td0[drive].sects[track][side][i].size;
			effective_sectors++;
			if ((id[0] == track) && (id[1] == side) && !(id[2]) && !(id[3]))
			{
				effective_sectors--;
			}
			if ((id[0] == track) && (id[1] == side) && (id[2] == (id[3] | 0x80)))
			{
				xdf_sectors++;
				td0[drive].xdf_ordered_pos[id[2]][side] = i;
			}
		}
		if ((effective_sectors == 3) && (xdf_sectors == 3))
		{
			td0[drive].current_side_flags[side] = 0x28;
			return 1;		/* 5.25" 2HD XDF */
		}
		if ((effective_sectors == 4) && (xdf_sectors == 4))
		{
			td0[drive].current_side_flags[side] = 0x08;
			return 2;		/* 3.5" 2HD XDF */
		}
		return 0;
	}
}

int td0_track_is_interleave(int drive, int side, int track)
{
	int i, effective_sectors;
	int track_spt;

	effective_sectors = 0;

	for (i = 0; i < 256; i++)
	{
		td0[drive].interleave_ordered_pos[i][side] = 0;
	}

	track_spt = td0[drive].track_spt[track][side];

	if (track_spt != 21)
	{
		return 0;
	}

	for (i = 0; i < track_spt; i++)
	{
		if ((td0[drive].sects[track][side][i].track == track) && (td0[drive].sects[track][side][i].head == side) && (td0[drive].sects[track][side][i].sector >= 1) && (td0[drive].sects[track][side][i].sector <= track_spt) && (td0[drive].sects[track][side][i].size == 2))
		{
			effective_sectors++;
			td0[drive].interleave_ordered_pos[td0[drive].sects[track][side][i].sector][side] = i;
		}
	}

	if (effective_sectors == track_spt)
	{
		return 1;
	}
	return 0;
}

void td0_seek(int drive, int track)
{
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
       
        if (!td0[drive].f)
                return;

        if (!td0[drive].track_width && fdd_doublestep_40(drive))
                track /= 2;

	is_trackx = (track == 0) ? 0 : 1;

	td0[drive].track = track;

	td0[drive].current_side_flags[0] = td0[drive].side_flags[track][0];
	td0[drive].current_side_flags[1] = td0[drive].side_flags[track][1];

	d86f_reset_index_hole_pos(drive, 0);
	d86f_reset_index_hole_pos(drive, 1);

	d86f_zero_bit_field(drive, 0);
	d86f_zero_bit_field(drive, 1);

	for (side = 0; side < td0[drive].sides; side++)
	{
		track_rate = td0[drive].current_side_flags[side] & 7;
		if (!track_rate && (td0[drive].current_side_flags[side] & 0x20))  track_rate = 4;
		track_gap3 = gap3_sizes[track_rate][td0[drive].sects[track][side][0].size][td0[drive].track_spt[track][side]];
		if (!track_gap3)
		{
			track_gap3 = td0[drive].calculated_gap3_lengths[track][side];
		}

		track_gap2 = ((td0[drive].current_side_flags[side] & 7) >= 3) ? 41 : 22;

		xdf_type = td0_track_is_xdf(drive, side, track);

		interleave_type = td0_track_is_interleave(drive, side, track);

		current_pos = d86f_prepare_pretrack(drive, side, 0);

		if (!xdf_type)
		{
			for (sector = 0; sector < td0[drive].track_spt[track][side]; sector++)
			{
				if (interleave_type == 0)
				{
					real_sector = td0[drive].sects[track][side][sector].sector;
					actual_sector = sector;
				}
				else
				{
					real_sector = dmf_r[sector];
					actual_sector = td0[drive].interleave_ordered_pos[real_sector][side];
				}
				id[0] = td0[drive].sects[track][side][actual_sector].track;
				id[1] = td0[drive].sects[track][side][actual_sector].head;
				id[2] = real_sector;
				id[3] = td0[drive].sects[track][side][actual_sector].size;
				ssize = 128 << ((uint32_t) td0[drive].sects[track][side][actual_sector].size);
				current_pos = d86f_prepare_sector(drive, side, current_pos, id, td0[drive].sects[track][side][actual_sector].data, ssize, track_gap2, track_gap3, td0[drive].sects[track][side][actual_sector].deleted, td0[drive].sects[track][side][actual_sector].bad_crc);

				if (sector == 0)
				{
					d86f_initialize_last_sector_id(drive, id[0], id[1], id[2], id[3]);
				}
			}
		}
		else
		{
			xdf_type--;
			xdf_spt = xdf_physical_sectors[xdf_type][is_trackx];
			for (sector = 0; sector < xdf_spt; sector++)
			{
				xdf_sector = (side * xdf_spt) + sector;
				id[0] = track;
				id[1] = side;
				id[2] = xdf_disk_layout[xdf_type][is_trackx][xdf_sector].id.r;
				id[3] = is_trackx ? (id[2] & 7) : 2;
				ssize = 128 << ((uint32_t) id[3]);
				ordered_pos = td0[drive].xdf_ordered_pos[id[2]][side];
				if (is_trackx)
				{
					current_pos = d86f_prepare_sector(drive, side, xdf_trackx_spos[xdf_type][xdf_sector], id, td0[drive].sects[track][side][ordered_pos].data, ssize, track_gap2, xdf_gap3_sizes[xdf_type][is_trackx], td0[drive].sects[track][side][ordered_pos].deleted, td0[drive].sects[track][side][ordered_pos].bad_crc);
				}
				else
				{
					current_pos = d86f_prepare_sector(drive, side, current_pos, id, td0[drive].sects[track][side][ordered_pos].data, ssize, track_gap2, xdf_gap3_sizes[xdf_type][is_trackx], td0[drive].sects[track][side][ordered_pos].deleted, td0[drive].sects[track][side][ordered_pos].bad_crc);
				}

				if (sector == 0)
				{
					d86f_initialize_last_sector_id(drive, id[0], id[1], id[2], id[3]);
				}
			}
		}
	}
}

uint16_t td0_disk_flags(int drive)
{
	return td0[drive].disk_flags;
}

uint16_t td0_side_flags(int drive)
{
	int side = 0;
	uint8_t sflags = 0;
	side = fdd_get_head(drive);
	sflags = td0[drive].current_side_flags[side];
	return sflags;
}

void td0_set_sector(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n)
{
	int i = 0;
	td0[drive].current_sector_index[side] = 0;
	if (c != td0[drive].track)  return;
	for (i = 0; i < td0[drive].track_spt[c][side]; i++)
	{
		if ((td0[drive].sects[c][side][i].track == c) &&
		    (td0[drive].sects[c][side][i].head == h) &&
		    (td0[drive].sects[c][side][i].sector == r) &&
		    (td0[drive].sects[c][side][i].size == n))
		{
			td0[drive].current_sector_index[side] = i;
		}
	}
	return;
}

uint8_t td0_poll_read_data(int drive, int side, uint16_t pos)
{
	return td0[drive].sects[td0[drive].track][side][td0[drive].current_sector_index[side]].data[pos];
}

void d86f_register_td0(int drive)
{
	d86f_handler[drive].disk_flags = td0_disk_flags;
	d86f_handler[drive].side_flags = td0_side_flags;
	d86f_handler[drive].writeback = null_writeback;
	d86f_handler[drive].set_sector = td0_set_sector;
	d86f_handler[drive].read_data = td0_poll_read_data;
	d86f_handler[drive].write_data = null_write_data;
	d86f_handler[drive].format_conditions = null_format_conditions;
	d86f_handler[drive].extra_bit_cells = null_extra_bit_cells;
	d86f_handler[drive].encoded_data = common_encoded_data;
	d86f_handler[drive].read_revolution = common_read_revolution;
	d86f_handler[drive].index_hole_pos = null_index_hole_pos;
	d86f_handler[drive].get_raw_size = common_get_raw_size;
	d86f_handler[drive].check_crc = 1;
	d86f_set_version(drive, 0x0063);
}
