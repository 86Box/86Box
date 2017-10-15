/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Trident TKD8001 RAMDAC emulation.
 *
 * Version:	@(#)vid_tkd8001_ramdac.c	1.0.1	2017/10/10
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
#include "../ibm.h"
#include "../mem.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_tkd8001_ramdac.h"


void tkd8001_ramdac_out(uint16_t addr, uint8_t val, tkd8001_ramdac_t *ramdac, svga_t *svga)
{
        switch (addr)
        {
                case 0x3C6:
                if (ramdac->state == 4)
                {
                        ramdac->state = 0;
                        ramdac->ctrl = val;
                        switch (val >> 5)
                        {
                                case 0: case 1: case 2: case 3:
                                svga->bpp = 8;
                                break;
                                case 5:
                                svga->bpp = 15;
                                break;
                                case 6:
                                svga->bpp = 24;
                                break;
                                case 7:
                                svga->bpp = 16;
                                break;
                        }
                        return;
                }
                break;
                case 0x3C7: case 0x3C8: case 0x3C9:
                ramdac->state = 0;
                break;
        }
        svga_out(addr, val, svga);
}

uint8_t tkd8001_ramdac_in(uint16_t addr, tkd8001_ramdac_t *ramdac, svga_t *svga)
{
        switch (addr)
        {
                case 0x3C6:
                if (ramdac->state == 4)
                {
                        return ramdac->ctrl;
                }
                ramdac->state++;
                break;
                case 0x3C7: case 0x3C8: case 0x3C9:
                ramdac->state = 0;
                break;
        }
        return svga_in(addr, svga);
}
