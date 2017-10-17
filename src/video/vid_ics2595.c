/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		ICS2595 clock chip emulation.  Used by ATI Mach64.
 *
 * Version:	@(#)vid_ics2595.c	1.0.1	2017/10/16
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
#include "../ibm.h"
#include "vid_ics2595.h"


enum
{
        ICS2595_IDLE = 0,
        ICS2595_WRITE,
        ICS2595_READ
};


static int ics2595_div[4] = {8, 4, 2, 1};


void ics2595_write(ics2595_t *ics2595, int strobe, int dat)
{
        if (strobe)
        {
                if ((dat & 8) && !ics2595->oldfs3) /*Data clock*/
                {
                        switch (ics2595->state)
                        {
                                case ICS2595_IDLE:
                                ics2595->state = (dat & 4) ? ICS2595_WRITE : ICS2595_IDLE;
                                ics2595->pos = 0;
                                break;
                                case ICS2595_WRITE:
                                ics2595->dat = (ics2595->dat >> 1);
                                if (dat & 4)
                                        ics2595->dat |= (1 << 19);
                                ics2595->pos++;
                                if (ics2595->pos == 20)
                                {
                                        int d, n, l;
                                        l = (ics2595->dat >> 2) & 0xf;
                                        n = ((ics2595->dat >> 7) & 255) + 257;
                                        d = ics2595_div[(ics2595->dat >> 16) & 3];

                                        ics2595->clocks[l] = (14318181.8 * ((double)n / 46.0)) / (double)d;
                                        ics2595->state = ICS2595_IDLE;
                                }
                                break;                                                
                        }
                }
                        
                ics2595->oldfs2 = dat & 4;
                ics2595->oldfs3 = dat & 8;
        }
        ics2595->output_clock = ics2595->clocks[dat];
}
