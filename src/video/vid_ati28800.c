/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
/*ATI 28800 emulation (VGA Charger)*/
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
#include "../timer.h"
#include "video.h"
#include "vid_ati28800.h"
#include "vid_ati_eeprom.h"
#include "vid_svga.h"
#include "vid_svga_render.h"


typedef struct ati28800_t
{
        svga_t svga;
        ati_eeprom_t eeprom;
        
        rom_t bios_rom;
        
        uint8_t regs[256];
        int index;
} ati28800_t;

void ati28800_svga_recalctimings(ati28800_t *ati28800);

void ati28800_out(uint16_t addr, uint8_t val, void *p)
{
        ati28800_t *ati28800 = (ati28800_t *)p;
        svga_t *svga = &ati28800->svga;
        uint8_t old;
        
        if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout&1)) 
                addr ^= 0x60;

        switch (addr)
        {
                case 0x1ce:
                ati28800->index = val;
                break;
                case 0x1cf:
                ati28800->regs[ati28800->index & 0x3f] = val;
                pclog("ATI 28800 ATI register write %02x %02x\n", ati28800->index, val);
                switch (ati28800->index & 0x3f)
                {
			case 0x2d:
			if ((gfxcard == GFX_VGAWONDERXL24) && (val & 8))
			{
				svga->charseta = (svga->charseta & 0x3ffff) | ((((uint32_t) val) >> 4) << 18);
				svga->charsetb = (svga->charsetb & 0x3ffff) | ((((uint32_t) val) >> 4) << 18);
			}
			break;

                        case 0x32:
                        case 0x3e:
                        if (ati28800->regs[0x3e] & 8) /*Read/write bank mode*/
                        {
                                svga->read_bank  = ((ati28800->regs[0x32] >> 5) & 7) * 0x10000;
                                svga->write_bank = ((ati28800->regs[0x32] >> 1) & 7) * 0x10000;
                        }
                        else                    /*Single bank mode*/
                                svga->read_bank = svga->write_bank = ((ati28800->regs[0x32] >> 1) & 7) * 0x10000;
                        break;
                        case 0xb3:
                        ati_eeprom_write(&ati28800->eeprom, val & 8, val & 2, val & 1);
                        break;
                }
                break;
                
                case 0x3D4:
                svga->crtcreg = val & 0x3f;
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
                        if (svga->crtcreg < 0xe || svga->crtcreg > 0x10)
                        {
                                svga->fullchange = changeframecount;
                                ati28800_svga_recalctimings(ati28800);
                        }
                }
                break;
        }
        svga_out(addr, val, svga);
}

uint8_t ati28800_in(uint16_t addr, void *p)
{
        ati28800_t *ati28800 = (ati28800_t *)p;
        svga_t *svga = &ati28800->svga;
        uint8_t temp;

        if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout&1)) addr ^= 0x60;
             
        switch (addr)
        {
                case 0x1ce:
                temp = ati28800->index;
                break;
                case 0x1cf:
                switch (ati28800->index & 0x3f)
                {
			case 0x2a:
			temp = (gfxcard == GFX_VGAWONDERXL24) ? 6 : 5;
			break;

                        case 0x37:
                        temp = ati28800->regs[ati28800->index & 0x3f] & ~8;
                        if (ati_eeprom_read(&ati28800->eeprom))
                                temp |= 8;
                        break;
                        
                        default:
                        temp = ati28800->regs[ati28800->index & 0x3f];
                        break;
                }
                break;

                case 0x3c2:
                if ((svga->vgapal[0].r + svga->vgapal[0].g + svga->vgapal[0].b) >= 0x50)
                        temp = 0;
                else
                        temp = 0x10;
                break;
                case 0x3D4:
                temp = svga->crtcreg;
                break;
                case 0x3D5:
                temp = svga->crtc[svga->crtcreg];
                break;
                default:
                temp = svga_in(addr, svga);
                break;
        }
#ifndef RELEASE_BUILD
        if (addr != 0x3da) pclog("%02X  %04X:%04X\n", temp, CS,cpu_state.pc);
#endif
        return temp;
}

void ati28800_svga_recalctimings(ati28800_t *ati28800)
{
        double crtcconst;
        double _dispontime, _dispofftime, disptime;
        svga_t *svga = &ati28800->svga;

        svga->vtotal = svga->crtc[6];
        svga->dispend = svga->crtc[0x12];
        svga->vsyncstart = svga->crtc[0x10];
        svga->split = svga->crtc[0x18];
        svga->vblankstart = svga->crtc[0x15];

        if (svga->crtc[7] & 1)  svga->vtotal |= 0x100;
        if (svga->crtc[7] & 32) svga->vtotal |= 0x200;
        svga->vtotal += 2;

        if (svga->crtc[7] & 2)  svga->dispend |= 0x100;
        if (svga->crtc[7] & 64) svga->dispend |= 0x200;
        svga->dispend++;

        if (svga->crtc[7] & 4)   svga->vsyncstart |= 0x100;
        if (svga->crtc[7] & 128) svga->vsyncstart |= 0x200;
        svga->vsyncstart++;

        if (svga->crtc[7] & 0x10) svga->split|=0x100;
        if (svga->crtc[9] & 0x40) svga->split|=0x200;
        svga->split++;
        
        if (svga->crtc[7] & 0x08) svga->vblankstart |= 0x100;
        if (svga->crtc[9] & 0x20) svga->vblankstart |= 0x200;
        svga->vblankstart++;
        
        svga->hdisp = svga->crtc[1];
        svga->hdisp++;

        svga->htotal = svga->crtc[0];
	if ((ati28800->regs[0x2d] & 8) && (gfxcard == GFX_VGAWONDERXL24))  svga->htotal |= (ati28800->regs[0x2d] & 1) ? 0x100 : 0;
        svga->htotal += 6; /*+6 is required for Tyrian*/

        svga->rowoffset = svga->crtc[0x13];

        svga->clock = (svga->vidclock) ? VGACONST2 : VGACONST1;
        
        svga->lowres = svga->attrregs[0x10] & 0x40;
        
        svga->interlace = 0;
        
        svga->ma_latch = (svga->crtc[0xc] << 8) | svga->crtc[0xd];

        svga->hdisp_time = svga->hdisp;
        svga->render = svga_render_blank;
        if (!svga->scrblank && svga->attr_palette_enable)
        {
                if (!(svga->gdcreg[6] & 1)) /*Text mode*/
                {
                        if (svga->seqregs[1] & 8) /*40 column*/
                        {
                                svga->render = svga_render_text_40;
                                svga->hdisp *= (svga->seqregs[1] & 1) ? 16 : 18;
                        }
                        else
                        {
                                svga->render = svga_render_text_80;
                                svga->hdisp *= (svga->seqregs[1] & 1) ? 8 : 9;
                        }
                        svga->hdisp_old = svga->hdisp;
                }
                else 
                {
                        svga->hdisp *= (svga->seqregs[1] & 8) ? 16 : 8;
                        svga->hdisp_old = svga->hdisp;                        
                        
                        switch (svga->gdcreg[5] & 0x60)
                        {
                                case 0x00: /*16 colours*/
                                if (svga->seqregs[1] & 8) /*Low res (320)*/
                                        svga->render = svga_render_4bpp_lowres;
                                else
                                        svga->render = svga_render_4bpp_highres;
                                break;
                                case 0x20: /*4 colours*/
                                if (svga->seqregs[1] & 8) /*Low res (320)*/
                                        svga->render = svga_render_2bpp_lowres;
                                else
                                        svga->render = svga_render_2bpp_highres;
                                break;
                                case 0x40: case 0x60: /*256+ colours*/
                                switch (svga->bpp)
                                {
                                        case 8:
                                        if (svga->lowres)
                                                svga->render = svga_render_8bpp_lowres;
                                        else
                                                svga->render = svga_render_8bpp_highres;
                                        break;
                                        case 15:
                                        if (svga->lowres)
                                                svga->render = svga_render_15bpp_lowres;
                                        else
                                                svga->render = svga_render_15bpp_highres;
                                        break;
                                        case 16:
                                        if (svga->lowres)
                                                svga->render = svga_render_16bpp_lowres;
                                        else
                                                svga->render = svga_render_16bpp_highres;
                                        break;
                                        case 24:
                                        if (svga->lowres)
                                                svga->render = svga_render_24bpp_lowres;
                                        else
                                                svga->render = svga_render_24bpp_highres;
                                        break;
                                        case 32:
                                        if (svga->lowres)
                                                svga->render = svga_render_32bpp_lowres;
                                        else
                                                svga->render = svga_render_32bpp_highres;
                                        break;
                                }
                                break;
                        }
                }
        }        

        svga->linedbl = svga->crtc[9] & 0x80;
        svga->rowcount = svga->crtc[9] & 31;
        if (svga->recalctimings_ex) 
                svga->recalctimings_ex(svga);

        if (svga->vblankstart < svga->dispend)
                svga->dispend = svga->vblankstart;

        crtcconst = (svga->seqregs[1] & 1) ? (svga->clock * 8.0) : (svga->clock * 9.0);

        disptime  = svga->htotal;
        _dispontime = svga->hdisp_time;
        
        if (svga->seqregs[1] & 8) { disptime *= 2; _dispontime *= 2; }
        _dispofftime = disptime - _dispontime;
        _dispontime *= crtcconst;
        _dispofftime *= crtcconst;

	svga->dispontime = (int)(_dispontime * (1 << TIMER_SHIFT));
	svga->dispofftime = (int)(_dispofftime * (1 << TIMER_SHIFT));
/*        printf("SVGA horiz total %i display end %i vidclock %f\n",svga->crtc[0],svga->crtc[1],svga->clock);
        printf("SVGA vert total %i display end %i max row %i vsync %i\n",svga->vtotal,svga->dispend,(svga->crtc[9]&31)+1,svga->vsyncstart);
        printf("total %f on %i cycles off %i cycles frame %i sec %i %02X\n",disptime*crtcconst,svga->dispontime,svga->dispofftime,(svga->dispontime+svga->dispofftime)*svga->vtotal,(svga->dispontime+svga->dispofftime)*svga->vtotal*70,svga->seqregs[1]);

        pclog("svga->render %08X\n", svga->render);*/
}

void ati28800_recalctimings(svga_t *svga)
{
        ati28800_t *ati28800 = (ati28800_t *)svga->p;
        uint8_t clock_sel = (svga->miscout >> 2) & 3;
        double freq = 0;

#ifndef RELEASE_BUILD
        pclog("ati28800_recalctimings\n");
#endif
        svga->interlace = (!svga->scrblank && (ati28800->regs[0x3e] & 2));

        clock_sel |= (ati28800->regs[0x39] & 2) << 2;
        clock_sel |= (ati28800->regs[0x3e] & 0x10) >> 1;
        switch(clock_sel)
        {
                case 0x00: freq = 42954000; break;
                case 0x01: freq = 48771000; break;
                case 0x02: freq = 16657000; break;
                case 0x03: freq = 36000000; break;
                case 0x04: freq = 50350000; break;
                case 0x05: freq = 56640000; break;
                case 0x06: freq = 28322000; break;
                case 0x07: freq = 44900000; break;
                case 0x08: freq = 30240000; break;
                case 0x09: freq = 32000000; break;
                case 0x0a: freq = 37500000; break;
                case 0x0b: freq = 39000000; break;
                case 0x0c: freq = 40000000; break;
                case 0x0d: freq = 56644000; break;
                case 0x0e: freq = 75000000; break;
                case 0x0f: freq = 65000000; break;
        }

        svga->clock = cpuclock / freq;
		
        if (!svga->scrblank && (ati28800->regs[0x30] & 0x20)) /*Extended 256 colour modes*/
        {
#ifndef RELEASE_BUILD
                pclog("8bpp_highres\n");
#endif
                svga->bpp = 8;
                svga->render = svga_render_8bpp_highres;
                svga->rowoffset <<= 1;
                svga->ma <<= 1;
        }
}               

void *ati28800_init()
{
	uint32_t memory = 512;
        ati28800_t *ati28800;
	/* if (gfxcard == GFX_VGAWONDERXL) */  memory = device_get_config_int("memory");
	memory <<= 10;
        ati28800 = malloc(sizeof(ati28800_t));
        memset(ati28800, 0, sizeof(ati28800_t));

	if (gfxcard == GFX_VGAWONDERXL)
	{
		rom_init_interleaved(&ati28800->bios_rom,
					L"roms/video/ati28800/XLEVEN.BIN",
					L"roms/video/ati28800/XLODD.BIN",
					0xc0000, 0x10000, 0xffff, 0, MEM_MAPPING_EXTERNAL);
	}
	else if (gfxcard == GFX_VGAWONDERXL24)
	{
		rom_init_interleaved(&ati28800->bios_rom,
					L"roms/video/ati28800/112-14318-102.bin",
					L"roms/video/ati28800/112-14319-102.bin",
					0xc0000, 0x10000, 0xffff, 0, MEM_MAPPING_EXTERNAL);
	}
	else        
	        rom_init(&ati28800->bios_rom, L"roms/video/ati28800/bios.bin", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
        
        svga_init(&ati28800->svga, ati28800, memory, /*512kb*/
                   ati28800_recalctimings,
                   ati28800_in, ati28800_out,
                   NULL,
                   NULL);

        io_sethandler(0x01ce, 0x0002, ati28800_in, NULL, NULL, ati28800_out, NULL, NULL, ati28800);
        io_sethandler(0x03c0, 0x0020, ati28800_in, NULL, NULL, ati28800_out, NULL, NULL, ati28800);

        ati28800->svga.miscout = 1;

        ati_eeprom_load(&ati28800->eeprom, L"ati28800.nvr", 0);

        return ati28800;
}

static int ati28800_available()
{
        return rom_present(L"roms/video/ati28800/bios.bin");
}

static int compaq_ati28800_available()
{
        return (rom_present(L"roms/video/ati28800/XLEVEN.bin") && rom_present(L"roms/video/ati28800/XLODD.bin"));
}

static int ati28800_wonderxl24_available()
{
        return (rom_present(L"roms/video/ati28800/112-14318-102.bin") && rom_present(L"roms/video/ati28800/112-14319-102.bin"));
}

void ati28800_close(void *p)
{
        ati28800_t *ati28800 = (ati28800_t *)p;

        svga_close(&ati28800->svga);
        
        free(ati28800);
}

void ati28800_speed_changed(void *p)
{
        ati28800_t *ati28800 = (ati28800_t *)p;
        
        ati28800_svga_recalctimings(ati28800);
}

void ati28800_force_redraw(void *p)
{
        ati28800_t *ati28800 = (ati28800_t *)p;

        ati28800->svga.fullchange = changeframecount;
}

void ati28800_add_status_info(char *s, int max_len, void *p)
{
        ati28800_t *ati28800 = (ati28800_t *)p;
        
        svga_add_status_info(s, max_len, &ati28800->svga);
}

static device_config_t ati28800_config[] =
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

static device_config_t ati28800_wonderxl_config[] =
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

device_t ati28800_device =
{
        "ATI-28800",
        0,
        ati28800_init,
        ati28800_close,
        ati28800_available,
        ati28800_speed_changed,
        ati28800_force_redraw,
        ati28800_add_status_info,
	ati28800_config
};

device_t compaq_ati28800_device =
{
        "Compaq ATI-28800",
        0,
        ati28800_init,
        ati28800_close,
        compaq_ati28800_available,
        ati28800_speed_changed,
        ati28800_force_redraw,
        ati28800_add_status_info,
	ati28800_config
};

device_t ati28800_wonderxl24_device =
{
        "ATI-28800 (VGA Wonder XL24)",
        0,
        ati28800_init,
        ati28800_close,
        ati28800_wonderxl24_available,
        ati28800_speed_changed,
        ati28800_force_redraw,
        ati28800_add_status_info,
	ati28800_wonderxl_config
};
