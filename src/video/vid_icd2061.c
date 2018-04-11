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
 * Version:	@(#)vid_icd2061.c	1.0.2	2017/11/04
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "vid_icd2061.h"


void icd2061_write(icd2061_t *icd2061, int val)
{
        int q, p, m, a;
        if ((val & 1) && !(icd2061->state & 1))
        {
                if (!icd2061->status)
                {
                        if (val & 2) 
                                icd2061->unlock++;
                        else         
                        {
                                if (icd2061->unlock >= 5)
                                {
                                        icd2061->status = 1;
                                        icd2061->pos = 0;
                                }
                                else
                                   icd2061->unlock = 0;
                        }
                }
                else if (val & 1)
                {
                        icd2061->data = (icd2061->data >> 1) | (((val & 2) ? 1 : 0) << 24);
                        icd2061->pos++;
                        if (icd2061->pos == 26)
                        {
                                a = (icd2061->data >> 21) & 0x7;
                                if (!(a & 4))
                                {
                                        q = (icd2061->data & 0x7f) - 2;
                                        m = 1 << ((icd2061->data >> 7) & 0x7);
                                        p = ((icd2061->data >> 10) & 0x7f) - 3;
                                        if (icd2061->ctrl & (1 << a))
                                           p <<= 1;
                                        icd2061->freq[a] = ((double)p / (double)q) * 2.0 * 14318184.0 / (double)m;
                                }
                                else if (a == 6)
                                {
                                        icd2061->ctrl = val;
                                }
                                icd2061->unlock = icd2061->data = 0;
                                icd2061->status = 0;
                        }
                }
        }
        icd2061->state = val;
}

double icd2061_getfreq(icd2061_t *icd2061, int i)
{
        return icd2061->freq[i];
}
