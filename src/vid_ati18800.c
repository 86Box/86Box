/*ATI 18800 emulation (VGA Edge-16)*/
#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "rom.h"
#include "video.h"
#include "vid_ati18800.h"
#include "vid_ati_eeprom.h"
#include "vid_svga.h"

typedef struct ati18800_t
{
        svga_t svga;
        ati_eeprom_t eeprom;

        rom_t bios_rom;
        
        uint8_t regs[256];
        int index;
} ati18800_t;

void ati18800_out(uint16_t addr, uint8_t val, void *p)
{
        ati18800_t *ati18800 = (ati18800_t *)p;
        svga_t *svga = &ati18800->svga;
        uint8_t old;
        
//        pclog("ati18800_out : %04X %02X  %04X:%04X\n", addr, val, CS,cpu_state.pc);
                
        if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout&1)) addr ^= 0x60;

        switch (addr)
        {
                case 0x1ce:
                ati18800->index = val;
                break;
                case 0x1cf:
                ati18800->regs[ati18800->index] = val;
                switch (ati18800->index)
                {
                        case 0xb2:
                        case 0xbe:
                        if (ati18800->regs[0xbe] & 8) /*Read/write bank mode*/
                        {
                                svga->read_bank  = ((ati18800->regs[0xb2] >> 5) & 7) * 0x10000;
                                svga->write_bank = ((ati18800->regs[0xb2] >> 1) & 7) * 0x10000;
                        }
                        else                    /*Single bank mode*/
                                svga->read_bank = svga->write_bank = ((ati18800->regs[0xb2] >> 1) & 7) * 0x10000;
                        break;
                        case 0xb3:
                        ati_eeprom_write(&ati18800->eeprom, val & 8, val & 2, val & 1);
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
                                svga_recalctimings(svga);
                        }
                }
                break;
        }
        svga_out(addr, val, svga);
}

uint8_t ati18800_in(uint16_t addr, void *p)
{
        ati18800_t *ati18800 = (ati18800_t *)p;
        svga_t *svga = &ati18800->svga;
        uint8_t temp;

//        if (addr != 0x3da) pclog("ati18800_in : %04X ", addr);
                
        if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout&1)) addr ^= 0x60;
             
        switch (addr)
        {
                case 0x1ce:
                temp = ati18800->index;
                break;
                case 0x1cf:
                switch (ati18800->index)
                {
                        case 0xb7:
                        temp = ati18800->regs[ati18800->index] & ~8;
                        if (ati_eeprom_read(&ati18800->eeprom))
                                temp |= 8;
                        break;
                        
                        default:
                        temp = ati18800->regs[ati18800->index];
                        break;
                }
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

void *ati18800_init()
{
        ati18800_t *ati18800 = malloc(sizeof(ati18800_t));
        memset(ati18800, 0, sizeof(ati18800_t));
        
        rom_init(&ati18800->bios_rom, "roms/vgaedge16.vbi", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
        
        svga_init(&ati18800->svga, ati18800, 1 << 19, /*512kb*/
                   NULL,
                   ati18800_in, ati18800_out,
                   NULL,
                   NULL);

        io_sethandler(0x01ce, 0x0002, ati18800_in, NULL, NULL, ati18800_out, NULL, NULL, ati18800);
        io_sethandler(0x03c0, 0x0020, ati18800_in, NULL, NULL, ati18800_out, NULL, NULL, ati18800);

        ati18800->svga.miscout = 1;

        ati_eeprom_load(&ati18800->eeprom, "ati18800.nvr", 0);

        return ati18800;
}

static int ati18800_available()
{
        return rom_present("roms/vgaedge16.vbi");
}

void ati18800_close(void *p)
{
        ati18800_t *ati18800 = (ati18800_t *)p;

        svga_close(&ati18800->svga);
        
        free(ati18800);
}

void ati18800_speed_changed(void *p)
{
        ati18800_t *ati18800 = (ati18800_t *)p;
        
        svga_recalctimings(&ati18800->svga);
}

void ati18800_force_redraw(void *p)
{
        ati18800_t *ati18800 = (ati18800_t *)p;

        ati18800->svga.fullchange = changeframecount;
}

void ati18800_add_status_info(char *s, int max_len, void *p)
{
        ati18800_t *ati18800 = (ati18800_t *)p;
        
        svga_add_status_info(s, max_len, &ati18800->svga);
}

device_t ati18800_device =
{
        "ATI-18800",
        0,
        ati18800_init,
        ati18800_close,
        ati18800_available,
        ati18800_speed_changed,
        ati18800_force_redraw,
        ati18800_add_status_info
};

