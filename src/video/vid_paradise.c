/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
/*Paradise VGA emulation

  PC2086, PC3086 use PVGA1A
  MegaPC uses W90C11A
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
#include "vid_paradise.h"
#include "vid_svga.h"
#include "vid_svga_render.h"


typedef struct paradise_t
{
        svga_t svga;
        
        rom_t bios_rom;
        
        enum
        {
                PVGA1A = 0,
                WD90C11
        } type;

        uint32_t read_bank[4], write_bank[4];
} paradise_t;

void    paradise_write(uint32_t addr, uint8_t val, void *p);
uint8_t paradise_read(uint32_t addr, void *p);
void paradise_remap(paradise_t *paradise);


void paradise_out(uint16_t addr, uint8_t val, void *p)
{
        paradise_t *paradise = (paradise_t *)p;
        svga_t *svga = &paradise->svga;
        uint8_t old;
        
        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
                addr ^= 0x60;
        switch (addr)
        {
                case 0x3c5:
                if (svga->seqaddr > 7)                        
                {
                        if (paradise->type < WD90C11 || svga->seqregs[6] != 0x48) 
                           return;
                        svga->seqregs[svga->seqaddr & 0x1f] = val;
                        if (svga->seqaddr == 0x11)
                           paradise_remap(paradise);
                        return;
                }
                break;

                case 0x3cf:
                if (svga->gdcaddr >= 0x9 && svga->gdcaddr < 0xf)
                {
                        if ((svga->gdcreg[0xf] & 7) != 5)
                           return;
                }
                if (svga->gdcaddr == 6)
                {
                        if ((svga->gdcreg[6] & 0xc) != (val & 0xc))
                        {
                                switch (val&0xC)
                                {
                                        case 0x0: /*128k at A0000*/
                                        mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
                                        svga->banked_mask = 0xffff;
                                        break;
                                        case 0x4: /*64k at A0000*/
                                        mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
                                        svga->banked_mask = 0xffff;
                                        break;
                                        case 0x8: /*32k at B0000*/
                                        mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
                                        svga->banked_mask = 0x7fff;
                                        break;
                                        case 0xC: /*32k at B8000*/
                                        mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
                                        svga->banked_mask = 0x7fff;
                                        break;
                                }
                        }
                        svga->gdcreg[6] = val;
                        paradise_remap(paradise);
                        return;
                }
                if (svga->gdcaddr == 0x9 || svga->gdcaddr == 0xa)
                {
                        svga->gdcreg[svga->gdcaddr] = val;
                        paradise_remap(paradise);
                        return;
                }
                if (svga->gdcaddr == 0xe)
                {
                        svga->gdcreg[0xe] = val;
                        paradise_remap(paradise);
                        return;
                }
                break;
                
                case 0x3D4:
                if (paradise->type == PVGA1A)
                   svga->crtcreg = val & 0x1f;
                else
                   svga->crtcreg = val & 0x3f;
                return;
                case 0x3D5:
		if (svga->crtcreg <= 0x18)
			val &= mask_crtc[svga->crtcreg];
                if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                        return;
                if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                        val = (svga->crtc[7] & ~0x10) | (val & 0x10);
                if (svga->crtcreg > 0x29 && (svga->crtc[0x29] & 7) != 5)
                   return;
                if (svga->crtcreg >= 0x31 && svga->crtcreg <= 0x37)
                   return;
                old = svga->crtc[svga->crtcreg];
                svga->crtc[svga->crtcreg] = val;

                if (old != val)
                {
                        if (svga->crtcreg < 0xe ||  svga->crtcreg > 0x10)
                        {
                                svga->fullchange = changeframecount;
                                svga_recalctimings(&paradise->svga);
                        }
                }
                break;
        }
        svga_out(addr, val, svga);
}

uint8_t paradise_in(uint16_t addr, void *p)
{
        paradise_t *paradise = (paradise_t *)p;
        svga_t *svga = &paradise->svga;
        
        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
                addr ^= 0x60;
        
        switch (addr)
        {
                case 0x3c2:
                return 0x10;
                
                case 0x3c5:
                if (svga->seqaddr > 7)
                {
                        if (paradise->type < WD90C11 || svga->seqregs[6] != 0x48) 
                           return 0xff;
                        if (svga->seqaddr > 0x12) 
                           return 0xff;
                        return svga->seqregs[svga->seqaddr & 0x1f];
                }
                break;
                        
                case 0x3cf:
                if (svga->gdcaddr >= 0x9 && svga->gdcaddr < 0xf)
                {
                        if (svga->gdcreg[0xf] & 0x10)
                           return 0xff;
                        switch (svga->gdcaddr)
                        {
                                case 0xf:
                                return (svga->gdcreg[0xf] & 0x17) | 0x80;
                        }
                }
                break;

                case 0x3D4:
                return svga->crtcreg;
                case 0x3D5:
                if (svga->crtcreg > 0x29 && svga->crtcreg < 0x30 && (svga->crtc[0x29] & 0x88) != 0x80)
                   return 0xff;
                return svga->crtc[svga->crtcreg];
        }
        return svga_in(addr, svga);
}

void paradise_remap(paradise_t *paradise)
{
        svga_t *svga = &paradise->svga;
        
        if (svga->seqregs[0x11] & 0x80)
        {
                paradise->read_bank[0]  = paradise->read_bank[2]  =  (svga->gdcreg[0x9] & 0x7f) << 12;
                paradise->read_bank[1]  = paradise->read_bank[3]  = ((svga->gdcreg[0x9] & 0x7f) << 12) + ((svga->gdcreg[6] & 0x08) ? 0 : 0x8000);
                paradise->write_bank[0] = paradise->write_bank[2] =  (svga->gdcreg[0xa] & 0x7f) << 12;
                paradise->write_bank[1] = paradise->write_bank[3] = ((svga->gdcreg[0xa] & 0x7f) << 12) + ((svga->gdcreg[6] & 0x08) ? 0 : 0x8000);
        }
        else if (svga->gdcreg[0xe] & 0x08)
        {
                if (svga->gdcreg[0x6] & 0xc)
                {
                        paradise->read_bank[0]  = paradise->read_bank[2]  =  (svga->gdcreg[0xa] & 0x7f) << 12;
                        paradise->write_bank[0] = paradise->write_bank[2] =  (svga->gdcreg[0xa] & 0x7f) << 12;
                        paradise->read_bank[1]  = paradise->read_bank[3]  = ((svga->gdcreg[0x9] & 0x7f) << 12) + ((svga->gdcreg[6] & 0x08) ? 0 : 0x8000);
                        paradise->write_bank[1] = paradise->write_bank[3] = ((svga->gdcreg[0x9] & 0x7f) << 12) + ((svga->gdcreg[6] & 0x08) ? 0 : 0x8000);
                }
                else
                {
                        paradise->read_bank[0] = paradise->write_bank[0] =  (svga->gdcreg[0xa] & 0x7f) << 12;
                        paradise->read_bank[1] = paradise->write_bank[1] = ((svga->gdcreg[0xa] & 0x7f) << 12) + ((svga->gdcreg[6] & 0x08) ? 0 : 0x8000);
                        paradise->read_bank[2] = paradise->write_bank[2] =  (svga->gdcreg[0x9] & 0x7f) << 12;
                        paradise->read_bank[3] = paradise->write_bank[3] = ((svga->gdcreg[0x9] & 0x7f) << 12) + ((svga->gdcreg[6] & 0x08) ? 0 : 0x8000);
                }
        }
        else
        {
                paradise->read_bank[0]  = paradise->read_bank[2]  =  (svga->gdcreg[0x9] & 0x7f) << 12;
                paradise->read_bank[1]  = paradise->read_bank[3]  = ((svga->gdcreg[0x9] & 0x7f) << 12) + ((svga->gdcreg[6] & 0x08) ? 0 : 0x8000);
                paradise->write_bank[0] = paradise->write_bank[2] =  (svga->gdcreg[0x9] & 0x7f) << 12;
                paradise->write_bank[1] = paradise->write_bank[3] = ((svga->gdcreg[0x9] & 0x7f) << 12) + ((svga->gdcreg[6] & 0x08) ? 0 : 0x8000);
        }
}

void paradise_recalctimings(svga_t *svga)
{
        svga->lowres = !(svga->gdcreg[0xe] & 0x01);
        if (svga->bpp == 8 && !svga->lowres)
                svga->render = svga_render_8bpp_highres;
}

void paradise_write(uint32_t addr, uint8_t val, void *p)
{
        paradise_t *paradise = (paradise_t *)p;
        addr = (addr & 0x7fff) + paradise->write_bank[(addr >> 15) & 3];

	/* Horrible hack, I know, but it's the only way to fix the 440FX BIOS filling the VRAM with garbage until Tom fixes the memory emulation. */
	if ((cs == 0xE0000) && (cpu_state.pc == 0xBF2F) && (romset == ROM_440FX))  return;
	if ((cs == 0xE0000) && (cpu_state.pc == 0xBF77) && (romset == ROM_440FX))  return;
        svga_write_linear(addr, val, &paradise->svga);
}

uint8_t paradise_read(uint32_t addr, void *p)
{
        paradise_t *paradise = (paradise_t *)p;
        addr = (addr & 0x7fff) + paradise->read_bank[(addr >> 15) & 3];
        return svga_read_linear(addr, &paradise->svga);
}

void *paradise_pvga1a_init()
{
        paradise_t *paradise = malloc(sizeof(paradise_t));
        svga_t *svga = &paradise->svga;
        memset(paradise, 0, sizeof(paradise_t));
        
        io_sethandler(0x03c0, 0x0020, paradise_in, NULL, NULL, paradise_out, NULL, NULL, paradise);

        svga_init(&paradise->svga, paradise, 1 << 18, /*256kb*/
                   NULL,
                   paradise_in, paradise_out,
                   NULL,
                   NULL);

        mem_mapping_set_handler(&paradise->svga.mapping, paradise_read, NULL, NULL, paradise_write, NULL, NULL);
        mem_mapping_set_p(&paradise->svga.mapping, paradise);
        
        svga->crtc[0x31] = 'W';
        svga->crtc[0x32] = 'D';
        svga->crtc[0x33] = '9';
        svga->crtc[0x34] = '0';
        svga->crtc[0x35] = 'C';

        svga->bpp = 8;
        svga->miscout = 1;

	svga->linear_base = 0;
        
        paradise->type = PVGA1A;               
        
        return paradise;
}

void *paradise_wd90c11_init()
{
        paradise_t *paradise = malloc(sizeof(paradise_t));
        svga_t *svga = &paradise->svga;
        memset(paradise, 0, sizeof(paradise_t));
        
        io_sethandler(0x03c0, 0x0020, paradise_in, NULL, NULL, paradise_out, NULL, NULL, paradise);

        svga_init(&paradise->svga, paradise, 1 << 19, /*512kb*/
                   paradise_recalctimings,
                   paradise_in, paradise_out,
                   NULL,
                   NULL);

        mem_mapping_set_handler(&paradise->svga.mapping, paradise_read, NULL, NULL, paradise_write, NULL, NULL);
        mem_mapping_set_p(&paradise->svga.mapping, paradise);

        svga->crtc[0x31] = 'W';
        svga->crtc[0x32] = 'D';
        svga->crtc[0x33] = '9';
        svga->crtc[0x34] = '0';
        svga->crtc[0x35] = 'C';
        svga->crtc[0x36] = '1';
        svga->crtc[0x37] = '1';

        svga->bpp = 8;
        svga->miscout = 1;
        
	svga->linear_base = 0;
        
        paradise->type = WD90C11;               
        
        return paradise;
}

static void *paradise_pvga1a_pc2086_init()
{
        paradise_t *paradise = paradise_pvga1a_init();
        
        if (paradise)
                rom_init(&paradise->bios_rom, L"roms/machines/pc2086/40186.ic171", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
                
        return paradise;
}
static void *paradise_pvga1a_pc3086_init()
{
        paradise_t *paradise = paradise_pvga1a_init();

        if (paradise)
                rom_init(&paradise->bios_rom, L"roms/machines/pc3086/c000.bin", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
                
        return paradise;
}

static void *paradise_wd90c11_megapc_init()
{
        paradise_t *paradise = paradise_wd90c11_init();
        
        if (paradise)
                rom_init_interleaved(&paradise->bios_rom,
                                     L"roms/machines/megapc/41651-bios lo.u18",
                                     L"roms/machines/megapc/211253-bios hi.u19",
                                     0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
        
        return paradise;
}

static int paradise_wd90c11_standalone_available()
{
        return rom_present(L"roms/machines/megapc/41651-bios lo.u18") && rom_present(L"roms/machines/megapc/211253-bios hi.u19");
}

void paradise_close(void *p)
{
        paradise_t *paradise = (paradise_t *)p;

        svga_close(&paradise->svga);
        
        free(paradise);
}

void paradise_speed_changed(void *p)
{
        paradise_t *paradise = (paradise_t *)p;
        
        svga_recalctimings(&paradise->svga);
}

void paradise_force_redraw(void *p)
{
        paradise_t *paradise = (paradise_t *)p;

        paradise->svga.fullchange = changeframecount;
}

void paradise_add_status_info(char *s, int max_len, void *p)
{
        paradise_t *paradise = (paradise_t *)p;
        
        svga_add_status_info(s, max_len, &paradise->svga);
}

device_t paradise_pvga1a_pc2086_device =
{
        "Paradise PVGA1A (Amstrad PC2086)",
        0,
        paradise_pvga1a_pc2086_init,
        paradise_close,
        NULL,
        paradise_speed_changed,
        paradise_force_redraw,
        paradise_add_status_info
};
device_t paradise_pvga1a_pc3086_device =
{
        "Paradise PVGA1A (Amstrad PC3086)",
        0,
        paradise_pvga1a_pc3086_init,
        paradise_close,
        NULL,
        paradise_speed_changed,
        paradise_force_redraw,
        paradise_add_status_info
};
device_t paradise_wd90c11_megapc_device =
{
        "Paradise WD90C11 (Amstrad MegaPC)",
        0,
        paradise_wd90c11_megapc_init,
        paradise_close,
        NULL,
        paradise_speed_changed,
        paradise_force_redraw,
        paradise_add_status_info
};
device_t paradise_wd90c11_device =
{
        "Paradise WD90C11",
        0,
        paradise_wd90c11_megapc_init,
        paradise_close,
        paradise_wd90c11_standalone_available,
        paradise_speed_changed,
        paradise_force_redraw,
        paradise_add_status_info
};
