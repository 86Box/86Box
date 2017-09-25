/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
/*Oak OTI067 emulation*/
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
#include "vid_oti067.h"
#include "vid_svga.h"


typedef struct oti067_t
{
        svga_t svga;
        
        rom_t bios_rom;
        
        int index;
        uint8_t regs[32];
        
        uint8_t pos;
        
        uint32_t vram_size;
        uint32_t vram_mask;

	uint8_t chip_id;
} oti067_t;

void oti067_out(uint16_t addr, uint8_t val, void *p)
{
        oti067_t *oti067 = (oti067_t *)p;
        svga_t *svga = &oti067->svga;
        uint8_t old;

        if ((((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && addr < 0x3de) && !(svga->miscout & 1)) addr ^= 0x60;

        switch (addr)
        {
                case 0x3D4:
                svga->crtcreg = val & 31;
                return;
                case 0x3D5:
		if (svga->crtcreg <= 0x18)
			val &= mask_crtc[svga->crtcreg];
                if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                        return;
                if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                        val = (svga->crtc[7] & ~0x10) | (val & 0x10);
                old = svga->crtc[svga->crtcreg];
                svga->crtc[svga->crtcreg] = val;
                if (old != val)
                {
                        if (svga->crtcreg < 0xE || svga->crtcreg > 0x10)
                        {
                                svga->fullchange = changeframecount;
                                svga_recalctimings(svga);
                        }
                }
                break;

                case 0x3DE: 
                oti067->index = val & 0x1f; 
                return;
                case 0x3DF:
                oti067->regs[oti067->index] = val;
                switch (oti067->index)
                {
                        case 0xD:
                        svga->vrammask = (val & 0xc) ? oti067->vram_mask : 0x3ffff;
                        if ((val & 0x80) && oti067->vram_size == 256)
                                mem_mapping_disable(&svga->mapping);
                        else
                                mem_mapping_enable(&svga->mapping);
                        if (!(val & 0x80))
                                svga->vrammask = 0x3ffff;
                        break;
                        case 0x11:
                        svga->read_bank = (val & 0xf) * 65536;
                        svga->write_bank = (val >> 4) * 65536;
                        break;
                }
                return;
        }
        svga_out(addr, val, svga);
}

uint8_t oti067_in(uint16_t addr, void *p)
{
        oti067_t *oti067 = (oti067_t *)p;
        svga_t *svga = &oti067->svga;
        uint8_t temp;
        
        if ((((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && addr < 0x3de) && !(svga->miscout & 1)) addr ^= 0x60;
        
        switch (addr)
        {
                case 0x3D4:
                temp = svga->crtcreg;
                break;
                case 0x3D5:
                temp = svga->crtc[svga->crtcreg];
                break;
                
                case 0x3DE: 
                temp = oti067->index | (oti067->chip_id << 5);
                break;               
                case 0x3DF: 
                if (oti067->index==0x10)     temp = 0x18;
                else                         temp = oti067->regs[oti067->index];
                break;

                default:
                temp = svga_in(addr, svga);
                break;
        }
        return temp;
}

void oti067_pos_out(uint16_t addr, uint8_t val, void *p)
{
        oti067_t *oti067 = (oti067_t *)p;

        if ((val & 8) != (oti067->pos & 8))
        {
                if (val & 8)
                        io_sethandler(0x03c0, 0x0020, oti067_in, NULL, NULL, oti067_out, NULL, NULL, oti067);
                else
                        io_removehandler(0x03c0, 0x0020, oti067_in, NULL, NULL, oti067_out, NULL, NULL, oti067);
        }
        
        oti067->pos = val;
}

uint8_t oti067_pos_in(uint16_t addr, void *p)
{
        oti067_t *oti067 = (oti067_t *)p;

        return oti067->pos;
}        

void oti067_recalctimings(svga_t *svga)
{
        oti067_t *oti067 = (oti067_t *)svga->p;
        
        if (oti067->regs[0x14] & 0x08) svga->ma_latch |= 0x10000;
        if (oti067->regs[0x0d] & 0x0c) svga->rowoffset <<= 1;
	if (oti067->regs[0x14] & 0x80)
	{
		svga->vtotal *= 2;
		svga->dispend *= 2;
		svga->vblankstart *= 2;
		svga->vsyncstart *=2;
		svga->split *= 2;			
	}
}

void *oti067_common_init(wchar_t *bios_fn, int vram_size, int chip_id)
{
        oti067_t *oti067 = malloc(sizeof(oti067_t));
        memset(oti067, 0, sizeof(oti067_t));
        
        rom_init(&oti067->bios_rom, bios_fn, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

        oti067->vram_size = vram_size;
        oti067->vram_mask = (vram_size << 10) - 1;

	oti067->chip_id = chip_id;
        
        svga_init(&oti067->svga, oti067, vram_size << 10,
                   oti067_recalctimings,
                   oti067_in, oti067_out,
                   NULL,
                   NULL);

        io_sethandler(0x03c0, 0x0020, oti067_in, NULL, NULL, oti067_out, NULL, NULL, oti067);
        io_sethandler(0x46e8, 0x0001, oti067_pos_in, NULL, NULL, oti067_pos_out, NULL, NULL, oti067);
        
        oti067->svga.miscout = 1;
        return oti067;
}

void *oti067_init()
{
        int vram_size = device_get_config_int("memory");
        return oti067_common_init(L"roms/video/oti/bios.bin", vram_size, 2);
}

void *oti077_init()
{
        int vram_size = device_get_config_int("memory");
        return oti067_common_init(L"roms/video/oti/oti077.vbi", vram_size, 5);
}

static int oti067_available()
{
        return rom_present(L"roms/video/oti/bios.bin");
}

static int oti077_available()
{
        return rom_present(L"roms/video/oti/oti077.vbi");
}

void oti067_close(void *p)
{
        oti067_t *oti067 = (oti067_t *)p;

        svga_close(&oti067->svga);
        
        free(oti067);
}

void oti067_speed_changed(void *p)
{
        oti067_t *oti067 = (oti067_t *)p;
        
        svga_recalctimings(&oti067->svga);
}
        
void oti067_force_redraw(void *p)
{
        oti067_t *oti067 = (oti067_t *)p;

        oti067->svga.fullchange = changeframecount;
}

void oti067_add_status_info(char *s, int max_len, void *p)
{
        oti067_t *oti067 = (oti067_t *)p;
        
        svga_add_status_info(s, max_len, &oti067->svga);
}

static device_config_t oti067_config[] =
{
        {
                "memory", "Memory size", CONFIG_SELECTION, "", 512,
                {
                        {
                                "256 kB", 256
                        },
                        {
                                "512 kB", 512
                        },
                        {
                                ""
                        }
                }
        },
        {
                "", "", -1
        }
};

static device_config_t oti077_config[] =
{
        {
                "memory", "Memory size", CONFIG_SELECTION, "", 1024,
                {
                        {
                                "256 kB", 256
                        },
                        {
                                "512 kB", 512
                        },
                        {
                                "1 MB", 1024
                        },
                        {
                                ""
                        }
                }
        },
        {
                "", "", -1
        }
};

device_t oti067_device =
{
        "Oak OTI-067",
        0,
        oti067_init,
        oti067_close,
        oti067_available,
        oti067_speed_changed,
        oti067_force_redraw,
        oti067_add_status_info,
        oti067_config
};
device_t oti077_device =
{
        "Oak OTI-077",
        0,
        oti077_init,
        oti067_close,
        oti077_available,
        oti067_speed_changed,
        oti067_force_redraw,
        oti067_add_status_info,
        oti077_config
};
