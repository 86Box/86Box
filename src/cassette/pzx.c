/************************************************************************

    PCEM: IBM 5150 Cassette support

    Copyright (C) 2019  John Elliott <seasip.webmaster@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../ui.h"
#include "pzx.h"

/* This module is intended to abstract all the details of a PZX file and 
 * emit its contents as a bitstream in a form suitable for PCEM. Similar 
 * modules could be written to add support for other tape formats such as TZX, 
 * TAP or CSW. */


#ifdef ENABLE_PZX_LOG
int pzx_do_log = ENABLE_PZX_LOG;


static void
pzx_log(const char *fmt, ...)
{
   va_list ap;

   if (pzx_do_log)
   {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
   }
}
#else
#define pzx_log(fmt, ...)
#endif

static uint32_t 
peek2(uint8_t *data)
{
	return (((uint32_t)data[1]) <<  8) | data[0];
}

static uint32_t 
peek4(uint8_t *data)
{
	return (((uint32_t)data[3]) << 24) |
	       (((uint32_t)data[2]) << 16) |
	       (((uint32_t)data[1]) <<  8) | data[0];
}

/* Cue up the next pulse definition from the current PULS block. */
static void 
pzx_parse_pulse(pzxfile_t *pzx)
{
	pzx->puls_duration = peek2(pzx->curblock + pzx->puls_ptr);
	pzx->puls_ptr += 2;
	if (pzx->puls_duration > 0x8000) {
		pzx->puls_count = pzx->puls_duration & 0x7FFF;
		pzx->puls_duration = peek2(pzx->curblock + pzx->puls_ptr);
		pzx->puls_ptr += 2;
	}
	if (pzx->puls_duration >= 0x8000) {
		pzx->puls_duration &= 0x7FFF;
		pzx->puls_duration <<= 16;
		pzx->puls_duration |= peek2(pzx->curblock + pzx->puls_ptr);
		pzx->puls_ptr += 2;
	}
	if (!pzx->puls_count) pzx->puls_count = 1;
}


void 
pzx_init(pzxfile_t *pzx)
{
	memset(pzx, 0, sizeof(pzxfile_t));
	pzx->state = PZX_CLOSED;
}

/* Load the next block from a PZX-format file. 
 *
 * Returns block if successful, NULL if end of file or error
 * Caller must free the block with free(). */
uint8_t 
*pzx_load_block(FILE *fp)
{
	uint8_t block_header[8];
	uint8_t *block_data;
	uint32_t block_len;

	/* The first 8 bytes of a PZX block are fixed: the first 4 give
	 * the ID, the second 4 the length (excluding the header itself) */
	if (fread(block_header, 1, 8, fp) < 8) 
		return NULL;	/* EoF */

	block_len = peek4(block_header + 4);
	block_data = malloc(8 + block_len);
	if (!block_data) return NULL;
	memcpy(block_data, block_header, 8);
	if (!block_len)	{ /* Block is only the header */
/*		CAS_LOG(("Loaded PZX block: %-4.4s\n", block_data)); */
		return block_data;
	}	
	if (fread(block_data + 8, 1, block_len, fp) < block_len) {
		free(block_data);	/* Unexpected EoF */
		return NULL;
	}
/* 	CAS_LOG(("Loaded PZX block: %-4.4s\n", block_data)); */
	return block_data;	
}


/* Search the current file for PZX version headers and check they're all 1.x */
static wchar_t 
*pzx_check_version(FILE *fp)
{
	uint8_t *block;
	static wchar_t message[80];

	rewind(fp);
	while ((block = pzx_load_block(fp))) {
		if (!memcmp(block, "PZXT", 4)) {
			pzx_log("PZX version %d.%d\n", block[8], block[9]);
			if (block[8] != 1) {
				swprintf(message, 80, L"Unsupported PZX version %d.%d\n", block[8], block[9]);
				free(block);
				return message;
			}
		}
		free(block);
	}
	rewind(fp);
	return NULL;
}


wchar_t 
*pzx_open(pzxfile_t *pzx, FILE *fp)
{
	wchar_t *result;

	rewind(fp);
	/* Check that this file is compatible */
	result = pzx_check_version(fp);
	if (result) 
		return result;

	pzx->level = 0;
	pzx->state = PZX_IDLE;
	pzx->input = fp;
	return NULL;	
}

void 
pzx_close(pzxfile_t *pzx)
{
	if (pzx->input) {
		fclose(pzx->input);
		pzx->input = NULL;
	}
	if (pzx->curblock) {
		free(pzx->curblock);
		pzx->curblock = NULL;
	}
	pzx->state = PZX_CLOSED;
}

/* Read the next block of type DATA, PAUS or PULS */
int 
pzx_next_block(pzxfile_t *pzx)
{
	long pos;

	pos = ftell(pzx->input);
	while (pzx->state == PZX_IDLE) {
		uint8_t *blk;

		/* In idle state there should be no current block. But
		 * make sure of that */
		if (pzx->curblock) {
			free(pzx->curblock);
			pzx->curblock = NULL;
		}

		/* Load the next block */
		blk = pzx_load_block(pzx->input);

		/* If that didn't load we've reached the end of file; wrap to
		 * beginning. */
		if (!blk) {
			rewind(pzx->input);
			blk = pzx_load_block(pzx->input);
			if (!blk) { /* Couldn't even load first block */
				pzx_close(pzx);
				return 0;
			}
			/* Have we read the whole file and come back to where
			 * we were? */
			if (ftell(pzx->input) == pos) {
				free(blk);
				pzx_close(pzx);
				return 0;
			}
		}
		/* We have loaded the next block. What is it? */
		if (!memcmp(blk, "PULS", 4)) {
			pzx->state = PZX_IN_PULS;
			pzx->curblock = blk;
			pzx->puls_len = 8 + peek4(blk + 4);
			pzx->puls_ptr = 8;
			pzx->puls_count = 0;
			pzx->puls_remain = 0;
			pzx->puls_duration = 0;
			pzx->level = 0;
			pzx_log("Beginning PULS block\n");
		}
		else if (!memcmp(blk, "PAUS", 4)) {
			pzx->state = PZX_IN_PAUS;
			pzx->curblock = blk;
			pzx->paus_remain = peek4(blk + 8);
			pzx->level = (pzx->paus_remain >> 31);
			pzx->paus_remain &= 0x7FFFFFFF;
			pzx_log("Beginning PAUS block, duration=%d\n",
					pzx->paus_remain);
		}
		else if (!memcmp(blk, "DATA", 4)) {
			pzx->state = PZX_IN_DATA;
			pzx->curblock = blk;
			pzx->data_bits = peek4(blk + 8);
			pzx->level = (pzx->data_bits >> 31);
			pzx->data_bits &= 0x7FFFFFFF;
			pzx->data_tail = peek2(blk + 12);
			pzx->data_p0   = blk[14];
			pzx->data_p1   = blk[15];
			pzx->data_p    = 0;
			pzx->data_w    = 16;
			pzx->data_remain = 0;
			pzx->data_ptr = 16 + 2 * (pzx->data_p0 + pzx->data_p1);
			pzx->data_mask = 0x80;
			pzx_log("Beginning DATA block, length=%d p0=%d p1=%d"
				" data_ptr=%d\n",
					pzx->data_bits, 
					pzx->data_p0, pzx->data_p1, 
					pzx->data_ptr);
		}
	}
	return 1;
}

static void 
pzx_endblock(pzxfile_t *pzx)
{
	if (pzx->curblock) 
		free(pzx->curblock);
	pzx->curblock = NULL;
	pzx->state = PZX_IDLE;
}

/* PAUS is easy - just run the timer down */
static int 
pzx_advance_paus(pzxfile_t *pzx, int time)
{
	if (pzx->paus_remain > time) {
		pzx->paus_remain -= time;
		return 0;
	}
	time -= pzx->paus_remain;
	pzx_endblock(pzx);
	return time;
}

static int 
pzx_advance_puls(pzxfile_t *pzx, int time)
{
	/* At the start of a pulse sequence? */
	if (pzx->puls_count == 0) {
		pzx_parse_pulse(pzx);
		pzx->puls_remain = pzx->puls_duration;
	}
	/* Does sample trigger a pulse change? If not, that's easy. */
	if (time < pzx->puls_remain) {
		pzx->puls_remain -= time;
		return 0;
	}
	/* Sample does trigger a pulse change */
	time -= pzx->puls_remain;
	/* If there's another pulse in the current sequence, that's 
	 * straightforward; just flip the level and continue */
	--pzx->puls_count;
	pzx->level = !pzx->level;
	if (pzx->puls_count) {
		pzx->puls_remain = pzx->puls_duration;
		return time;
	}
	/* If we've reached the end of the pulse sequence, there may be 
	 * another one */
	if (pzx->puls_ptr < pzx->puls_len) {
		return time;
	}
	/* If there isn't another one, it's the end of the block */
	pzx_endblock(pzx);
	return time;
}

/* Decode a DATA block */
static int 
pzx_advance_data(pzxfile_t *pzx, int time)
{
	uint8_t bit;

	/* Reached end of data? */
	if (pzx->data_bits == 0) {
		/* Time interval is covered by the tail bit */
		if (pzx->data_tail > time) {
			pzx->data_tail -= time;
			return 0;	
		}
		/* Have run out of block */
		time -= pzx->data_tail;
		pzx_endblock(pzx);
		return time;
	}
	/* No more time remaining on the current bit? */
	if (pzx->data_p < 1 && !pzx->data_remain) {
		bit = pzx->curblock[pzx->data_ptr] & pzx->data_mask;
		pzx->data_mask >>= 1;
		if (!pzx->data_mask) {
			pzx->data_mask = 0x80;
			++pzx->data_ptr;
		}
		--pzx->data_bits;	

		if (bit) {
			pzx->data_p = pzx->data_p1;
			pzx->data_w = 16 + 2 * pzx->data_p0;
			pzx->data_remain = 0;
		} else {
			pzx->data_p = pzx->data_p0;
			pzx->data_w = 16;
			pzx->data_remain = 0;
		}
	}
	/* See if we've started processing the current waveform. If not, 
	 * load its first element (assuming that there is one) */
	if (!pzx->data_remain) {
		if (pzx->data_p) {
			pzx->data_remain = peek2(pzx->curblock + pzx->data_w);
			pzx->data_w += 2;
			pzx->data_p--;
		}
	}
	if (pzx->data_remain > time) {
		/* Time advance is contained within current wave */
		pzx->data_remain -= time;
		return 0;
	} else { /* Move on to next element of wave / next bit / next block */
		time -= pzx->data_remain;
		pzx->data_remain = 0;
		pzx->level = !pzx->level;
	}

	return time;
}

int 
pzx_advance(pzxfile_t *pzx, int time)
{
	if (pzx->state == PZX_CLOSED) 
		return 0;	/* No tape loaded */

	while (time) {
		switch (pzx->state)
		{
			case PZX_IDLE:
				if (!pzx_next_block(pzx)) return 0;
				break;
			case PZX_IN_PULS:
				time = pzx_advance_puls(pzx, time);
				break;
			case PZX_IN_PAUS:
				time = pzx_advance_paus(pzx, time);
				break;
			case PZX_IN_DATA:
				time = pzx_advance_data(pzx, time);
				break;
		}
	}
	return pzx->level;
}



