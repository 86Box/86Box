/************************************************************************

    PCEM: IBM 5150 cassette support

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

typedef enum
{
	PZX_CLOSED,	/* File is not open */
	PZX_IDLE,	/* File is open, no block loaded */
	PZX_IN_PULS,	/* File is open, current block is a PULS block */
	PZX_IN_DATA,	/* File is open, current block is a DATA block */
	PZX_IN_PAUS,	/* File is open, current block is a PAUS block */
} PZX_STATE;


typedef struct pzxfile_t
{
	FILE *input;		/* Input PZX file */
	uint8_t *curblock;	/* Currently-loaded block, if any */
	int level;		/* Current signal level */
	PZX_STATE state;	/* State machine current status */
/* State variables for PULS */
	uint32_t puls_ptr;	/* Pointer within PULS block */
	uint32_t puls_len;	/* Length of PULS block */
	uint32_t puls_count;	/* Count of pulses */
	uint32_t puls_duration;	/* Duration of each pulse */
	uint32_t puls_remain;	/* Time remaining in this pulse */
/* State variables for PAUS */
	uint32_t paus_remain;	/* Time remaining in this pause */
/* State variables for DATA */
	uint32_t data_ptr;	/* Pointer within DATA block */
	uint32_t data_bits; 	/* Count of bits */
	uint16_t data_tail;	/* Length of pulse after last bit */
	uint8_t  data_mask;	/* Mask for current bit */
	uint8_t	 data_p0;	/* Length of 0 encoding */	
	uint8_t	 data_p1;	/* Length of 1 encoding */	
	int	 data_p;	/* Current sequence being emitted */
	uint32_t data_w;	/* Current waveform */
	uint32_t data_remain;	/* Current data pulse time remaining */
} pzxfile_t;

uint8_t *pzx_load_block(FILE *fp);

/* Initialise structure */
void pzx_init(pzxfile_t *pzx);

/* Open file for input */
wchar_t *pzx_open(pzxfile_t *pzx, FILE *fp);

/* Close file */
void pzx_close(pzxfile_t *pzx);

/* Advance by 'time' samples (3.5MHz sample rate) and return current state */
int pzx_advance(pzxfile_t *pzx, int time);
