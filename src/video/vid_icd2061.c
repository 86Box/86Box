/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		ICD2061 clock generator emulation.
 *
 *		Used by ET4000w32/p (Diamond Stealth 32)
 *
 * Version:	@(#)vid_icd2061.c	1.0.6	2018/10/02
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "vid_icd2061.h"

void
icd2061_write(icd2061_t *icd2061, int val)
{
    int /*od, */nd, oc, nc;
    int a/*, i*/, qa, q, pa, p, m;

#if 0
    od = (icd2061->state & 2) >> 1;	/* Old data. */
#endif
    nd = (val & 2) >> 1;		/* Old data. */
    oc = icd2061->state & 1;		/* Old clock. */
    nc = val & 1;			/* New clock. */

    icd2061->state = val;

    if (nc && !oc) {			/* Low-to-high transition of CLK. */
	if (!icd2061->unlocked) {
		if (nd) {			/* DATA high. */
			icd2061->count++;
			/* pclog("Low-to-high transition of CLK with DATA high, %i total\n", icd2061->count); */
		} else {			/* DATA low. */
			if (icd2061->count >= 5) {
				icd2061->unlocked = 1;
				icd2061->bit_count = icd2061->data = 0;
				/* pclog("ICD2061 unlocked\n"); */
			} else {
				icd2061->count = 0;
				/* pclog("ICD2061 locked\n"); */
			}
		}
	} else if (nc) {
		icd2061->data |= (nd << icd2061->bit_count);
		icd2061->bit_count++;

		if (icd2061->bit_count == 26) {
			/* pclog("26 bits received, data = %08X\n", icd2061->data); */
	
			a = ((icd2061->data >> 22) & 0x07);	/* A  */
			/* pclog("A = %01X\n", a); */

			if (a < 3) {
#if 0
				i = ((icd2061->data >> 18) & 0x0f);	/* I  */
#endif
				pa = ((icd2061->data >> 11) & 0x7f);	/* P' */
				m = ((icd2061->data >> 8) & 0x07);	/* M  */
				qa = ((icd2061->data >> 1) & 0x7f);	/* Q' */

				p = pa + 3;				/* P  */
				m = 1 << m;
				q = qa + 2;				/* Q  */

				if (icd2061->ctrl & (1 << a))
					p <<= 1;
				icd2061->freq[a] = ((float)p / (float)q) * 2.0 * 14318184.0 / (float)m;

				/* pclog("P = %02X, M = %01X, Q = %02X, freq[%i] = %f\n", p, m, q, a, icd2061->freq[a]); */
			} else if (a == 6) {
				icd2061->ctrl = ((icd2061->data >> 13) & 0xff);
				/* pclog("ctrl = %02X\n", icd2061->ctrl); */
			}
			icd2061->count = icd2061->bit_count = icd2061->data = 0;
			icd2061->unlocked = 0;
			/* pclog("ICD2061 locked\n"); */
		}
	}
    }
}


void
icd2061_init(icd2061_t *icd2061)
{
    memset(icd2061, 0, sizeof(icd2061_t));

    icd2061->freq[0] = 25175000.0;
    icd2061->freq[1] = 28322000.0;
    icd2061->freq[2] = 28322000.0;
}


float
icd2061_getclock(int clock, void *p)
{
    icd2061_t *icd2061 = (icd2061_t *) p;

    if (clock > 2)
	clock = 2;

    return icd2061->freq[clock];
}
