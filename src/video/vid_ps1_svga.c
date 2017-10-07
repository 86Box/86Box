/*Emulation of the SVGA chip in the IBM PS/1 Model 2121, or at least the
  20 MHz version.
  
  I am not entirely sure what this chip actually is, possibly a CF62011? I can
  not find any documentation on the chip so have implemented enough to pass
  self-test in the PS/1 BIOS. It has 512kb video memory but I have not found any
  native drivers for any operating system and there is no VBE implementation, so
  it's just a VGA for now.
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../ibm.h"
#include "../io.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_vga.h"


typedef struct ps1_m2121_svga_t
{
        svga_t svga;
        
        rom_t bios_rom;
        
        uint8_t banking;
        uint8_t reg_2100;
        uint8_t reg_210a;
} ps1_m2121_svga_t;

void ps1_m2121_svga_out(uint16_t addr, uint8_t val, void *p)
{
        ps1_m2121_svga_t *ps1 = (ps1_m2121_svga_t *)p;
        svga_t *svga = &ps1->svga;
        uint8_t old;
        
        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
                addr ^= 0x60;

        switch (addr)
        {
                case 0x3D4:
                svga->crtcreg = val & 0x1f;
                return;
                case 0x3D5:
                if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                        return;
                if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                        val = (svga->crtc[7] & ~0x10) | (val & 0x10);
                old = svga->crtc[svga->crtcreg];
                svga->crtc[svga->crtcreg] = val;
                if (old != val)
                {
                        if (svga->crtcreg < 0xe || svga->crtcreg > 0x10)
                        {
                                svga->fullchange = changeframecount;
                                svga_recalctimings(svga);
                        }
                }
                break;
                
                case 0x2100:
                ps1->reg_2100 = val;
                if ((val & 7) < 4)
                        svga->read_bank = svga->write_bank = 0;
                else
                        svga->read_bank = svga->write_bank = (ps1->banking & 0x7) * 0x10000;
                break;
                case 0x2108:
                if ((ps1->reg_2100 & 7) >= 4)
                        svga->read_bank = svga->write_bank = (val & 0x7) * 0x10000;
                ps1->banking = val;
                break;
                case 0x210a:
                ps1->reg_210a = val;
                break;
        }
        svga_out(addr, val, svga);
}

uint8_t ps1_m2121_svga_in(uint16_t addr, void *p)
{
        ps1_m2121_svga_t *ps1 = (ps1_m2121_svga_t *)p;
        svga_t *svga = &ps1->svga;
        uint8_t temp;

        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
                addr ^= 0x60;
             
        switch (addr)
        {
                case 0x100:
                temp = 0xfe;
                break;
                case 0x101:
                temp = 0xe8;
                break;

                case 0x3D4:
                temp = svga->crtcreg;
                break;
                case 0x3D5:
                temp = svga->crtc[svga->crtcreg];
                break;

                case 0x2108:
                temp = ps1->banking;
                break;
                case 0x210a:
                temp = ps1->reg_210a;
                break;
                
                default:
                temp = svga_in(addr, svga);
                break;
        }
        return temp;
}


static void *ps1_m2121_svga_init(device_t *info)
{
        ps1_m2121_svga_t *ps1 = malloc(sizeof(ps1_m2121_svga_t));
        memset(ps1, 0, sizeof(ps1_m2121_svga_t));
       
        svga_init(&ps1->svga, ps1, 1 << 19, /*512kb*/
                   NULL,
                   ps1_m2121_svga_in, ps1_m2121_svga_out,
                   NULL,
                   NULL);

        io_sethandler(0x0100, 0x0002, ps1_m2121_svga_in, NULL, NULL, ps1_m2121_svga_out, NULL, NULL, ps1);
        io_sethandler(0x03c0, 0x0020, ps1_m2121_svga_in, NULL, NULL, ps1_m2121_svga_out, NULL, NULL, ps1);
        io_sethandler(0x2100, 0x0010, ps1_m2121_svga_in, NULL, NULL, ps1_m2121_svga_out, NULL, NULL, ps1);
                
        ps1->svga.bpp = 8;
        ps1->svga.miscout = 1;
        
        return ps1;
}

void ps1_m2121_svga_close(void *p)
{
        ps1_m2121_svga_t *ps1 = (ps1_m2121_svga_t *)p;

        svga_close(&ps1->svga);
        
        free(ps1);
}

void ps1_m2121_svga_speed_changed(void *p)
{
        ps1_m2121_svga_t *ps1 = (ps1_m2121_svga_t *)p;
        
        svga_recalctimings(&ps1->svga);
}

void ps1_m2121_svga_force_redraw(void *p)
{
        ps1_m2121_svga_t *ps1 = (ps1_m2121_svga_t *)p;

        ps1->svga.fullchange = changeframecount;
}

void ps1_m2121_svga_add_status_info(char *s, int max_len, void *p)
{
        ps1_m2121_svga_t *ps1 = (ps1_m2121_svga_t *)p;
        
        svga_add_status_info(s, max_len, &ps1->svga);
}

device_t ps1_m2121_svga_device =
{
        "PS/1 Model 2121 SVGA",
        0, 0,
        ps1_m2121_svga_init,
        ps1_m2121_svga_close,
        NULL,
	NULL,
        ps1_m2121_svga_speed_changed,
        ps1_m2121_svga_force_redraw,
        ps1_m2121_svga_add_status_info
};
