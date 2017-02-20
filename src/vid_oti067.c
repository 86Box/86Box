/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
/*Oak OTI067 emulation*/
#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "rom.h"
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

//        pclog("oti067_out : %04X %02X  %02X %i\n", addr, val, ram[0x489], ins);
                
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
        
//        if (addr != 0x3da && addr != 0x3ba) pclog("oti067_in : %04X ", addr);
        
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
                // temp = oti067->index | (2 << 5);
                break;               
                case 0x3DF: 
                if (oti067->index==0x10)     temp = 0x18;
                else                         temp = oti067->regs[oti067->index];
                break;

                default:
                temp = svga_in(addr, svga);
                break;
        }
//        if (addr != 0x3da && addr != 0x3ba) pclog("%02X  %04X:%04X\n", temp, CS,pc);        
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
        // svga->interlace = oti067->regs[0x14] & 0x80;
	if (oti067->regs[0x14] & 0x80)
	{
		svga->vtotal *= 2;
		svga->dispend *= 2;
		svga->vblankstart *= 2;
		svga->vsyncstart *=2;
		svga->split *= 2;			
	}
}

void *oti067_common_init(char *bios_fn, int vram_size, int chip_id)
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

/* void *oti037_init()
{
        int vram_size = device_get_config_int("memory");
        return oti067_common_init("roms/hyundai_oti037c.bin", vram_size, 0);
} */

void *oti067_init()
{
        int vram_size = device_get_config_int("memory");
        return oti067_common_init("roms/oti067/bios.bin", vram_size, 2);
}

void *oti077_init()
{
        int vram_size = device_get_config_int("memory");
        return oti067_common_init("roms/oti077.vbi", vram_size, 5);
}

void *oti067_acer386_init()
{
        oti067_t *oti067 = oti067_common_init("roms/acer386/oti067.bin", 512, 2);
        
        /* if (oti067)
                oti067->bios_rom.rom[0x5d] = 0x74; */
                
        return oti067;
}

/* static int oti037_available()
{
        return rom_present("roms/hyundai_oti037c.bin");
} */

static int oti067_available()
{
        return rom_present("roms/oti067/bios.bin");
}

static int oti077_available()
{
        return rom_present("roms/oti077.vbi");
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
                .name = "memory",
                .description = "Memory size",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "256 kB",
                                .value = 256
                        },
                        {
                                .description = "512 kB",
                                .value = 512
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 512
        },
        {
                .type = -1
        }
};

static device_config_t oti077_config[] =
{
        {
                .name = "memory",
                .description = "Memory size",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "256 kB",
                                .value = 256
                        },
                        {
                                .description = "512 kB",
                                .value = 512
                        },
                        {
                                .description = "1 MB",
                                .value = 1024
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 1024
        },
        {
                .type = -1
        }
};

/* device_t oti037_device =
{
        "Oak OTI-037",
        0,
        oti037_init,
        oti067_close,
        oti037_available,
        oti067_speed_changed,
        oti067_force_redraw,
        oti067_add_status_info,
        oti067_config
}; */
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
device_t oti067_acer386_device =
{
        "Oak OTI-067 (Acermate 386SX/25N)",
        0,
        oti067_acer386_init,
        oti067_close,
        oti067_available,
        oti067_speed_changed,
        oti067_force_redraw,
        oti067_add_status_info
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
