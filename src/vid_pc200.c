/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
/*PC200 video emulation.
  CGA with some NMI stuff. But we don't need that as it's only used for TV and
  LCD displays, and we're emulating a CRT*/
#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "timer.h"
#include "video.h"
#include "vid_cga.h"
#include "vid_pc200.h"

typedef struct pc200_t
{
        mem_mapping_t mapping;
        
        cga_t cga;

        uint8_t reg_3dd, reg_3de, reg_3df;
} pc200_t;

static uint8_t crtcmask[32] = 
{
        0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f, 0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
        0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

void pc200_out(uint16_t addr, uint8_t val, void *p)
{
        pc200_t *pc200 = (pc200_t *)p;
        cga_t *cga = &pc200->cga;
        uint8_t old;
        
        switch (addr)
        {
                case 0x3d5:
                if (!(pc200->reg_3de & 0x40) && cga->crtcreg <= 11)
                {
                        if (pc200->reg_3de & 0x80) 
                                nmi = 1;
                                
                        pc200->reg_3dd = 0x20 | (cga->crtcreg & 0x1f);
                        pc200->reg_3df = val;
                        return;
                }
                old = cga->crtc[cga->crtcreg];
                cga->crtc[cga->crtcreg] = val & crtcmask[cga->crtcreg];
                if (old != val)
                {
                        if (cga->crtcreg < 0xe || cga->crtcreg > 0x10)
                        {
                                fullchange = changeframecount;
                                cga_recalctimings(cga);
                        }
                }
                return;
                
                case 0x3d8:
                old = cga->cgamode;
                cga->cgamode = val;
                if ((cga->cgamode ^ old) & 3)
                   cga_recalctimings(cga);
                pc200->reg_3dd |= 0x80;
                if (pc200->reg_3de & 0x80)
                   nmi = 1;
                return;
                
                case 0x3de:
                pc200->reg_3de = val;
                pc200->reg_3dd = 0x1f;
                if (val & 0x80) 
                        pc200->reg_3dd |= 0x40;
                return;
        }
        cga_out(addr, val, cga);
}

uint8_t pc200_in(uint16_t addr, void *p)
{
        pc200_t *pc200 = (pc200_t *)p;
        cga_t *cga = &pc200->cga;
        uint8_t temp;

        switch (addr)
        {
                case 0x3D8:
                return cga->cgamode;
                
                case 0x3DD:
                temp = pc200->reg_3dd;
                pc200->reg_3dd &= 0x1f;
		nmi = 0;
                return temp;
                
                case 0x3DE:
                return (pc200->reg_3de & 0xc7) | 0x10; /*External CGA*/
                
                case 0x3DF:
                return pc200->reg_3df;
        }
        return cga_in(addr, cga);
}

void *pc200_init()
{
        pc200_t *pc200 = malloc(sizeof(pc200_t));
        cga_t *cga = &pc200->cga;
        memset(pc200, 0, sizeof(pc200_t));

        pc200->cga.vram = malloc(0x4000);
        cga_init(&pc200->cga);
                        
        timer_add(cga_poll, &cga->vidtime, TIMER_ALWAYS_ENABLED, cga);
        mem_mapping_add(&pc200->mapping, 0xb8000, 0x08000, cga_read, NULL, NULL, cga_write, NULL, NULL,  NULL, 0, cga);
        io_sethandler(0x03d0, 0x0010, pc200_in, NULL, NULL, pc200_out, NULL, NULL, pc200);
	overscan_x = overscan_y = 16;
        return pc200;
}

void pc200_close(void *p)
{
        pc200_t *pc200 = (pc200_t *)p;

        free(pc200->cga.vram);
        free(pc200);
}

void pc200_speed_changed(void *p)
{
        pc200_t *pc200 = (pc200_t *)p;
        
        cga_recalctimings(&pc200->cga);
}

device_t pc200_device =
{
        "Amstrad PC200 (video)",
        0,
        pc200_init,
        pc200_close,
        NULL,
        pc200_speed_changed,
        NULL,
        NULL
};
