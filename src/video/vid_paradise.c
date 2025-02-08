/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Paradise VGA emulation
 *           PC2086, PC3086 use PVGA1A
 *           MegaPC uses W90C11A
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>

typedef struct paradise_t {
    svga_t svga;

    rom_t bios_rom;

    enum {
        PVGA1A = 0,
        WD90C11,
        WD90C30
    } type;

    uint32_t vram_mask;
    uint32_t memory;

    uint32_t read_bank[4], write_bank[4];

    int interlace;

    struct {
        uint8_t reg_block_ptr;
        uint8_t reg_idx;
        uint8_t disable_autoinc;

        uint16_t int_status;
        uint16_t blt_ctrl1, blt_ctrl2;
        uint16_t srclow, srchigh;
        uint16_t dstlow, dsthigh;

        uint32_t srcaddr, dstaddr;

        int invalid_block;
    } accel;
} paradise_t;

static video_timings_t timing_paradise_pvga1a = { .type = VIDEO_ISA, .write_b = 6, .write_w = 8, .write_l = 16, .read_b = 6, .read_w = 8, .read_l = 16 };
static video_timings_t timing_paradise_wd90c  = { .type = VIDEO_ISA, .write_b = 3, .write_w = 3, .write_l =  6, .read_b = 5, .read_w = 5, .read_l = 10 };

void paradise_remap(paradise_t *paradise);

uint8_t
paradise_in(uint16_t addr, void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;
    svga_t     *svga     = &paradise->svga;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3c5:
            if (svga->seqaddr > 7) {
                if (paradise->type < WD90C11 || svga->seqregs[6] != 0x48)
                    return 0xff;
                if (svga->seqaddr > 0x12)
                    return 0xff;
                return svga->seqregs[svga->seqaddr & 0x1f];
            }
            break;

        case 0x3c6:
        case 0x3c7:
        case 0x3c8:
        case 0x3c9:
            if (paradise->type == WD90C30)
                return sc1148x_ramdac_in(addr, 0, svga->ramdac, svga);
            return svga_in(addr, svga);

        case 0x3cf:
            if (svga->gdcaddr >= 9 && svga->gdcaddr <= 0x0e) {
                if (svga->gdcreg[0x0f] & 0x10)
                    return 0xff;
            }
            switch (svga->gdcaddr) {
                case 0x0f:
                    return (svga->gdcreg[0x0f] & 0x17) | 0x80;

                default:
                    break;
            }
            break;

        case 0x3D4:
            return svga->crtcreg;
        case 0x3D5:
            if ((paradise->type == PVGA1A) && (svga->crtcreg & 0x20))
                return 0xff;
            if (svga->crtcreg > 0x29 && svga->crtcreg < 0x30 && (svga->crtc[0x29] & 0x88) != 0x80)
                return 0xff;
            return svga->crtc[svga->crtcreg];

        default:
            break;
    }
    return svga_in(addr, svga);
}

void
paradise_out(uint16_t addr, uint8_t val, void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;
    svga_t     *svga     = &paradise->svga;
    uint8_t     old;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3c5:
            if (svga->seqaddr > 7) {
                if (paradise->type < WD90C11 || svga->seqregs[6] != 0x48)
                    return;
                svga->seqregs[svga->seqaddr & 0x1f] = val;
                if (svga->seqaddr == 0x11) {
                    paradise_remap(paradise);
                }
                return;
            }
            break;

        case 0x3c6:
        case 0x3c7:
        case 0x3c8:
        case 0x3c9:
            if (paradise->type == WD90C30)
                sc1148x_ramdac_out(addr, 0, val, svga->ramdac, svga);
            else
                svga_out(addr, val, svga);
            return;

        case 0x3cf:
            if (svga->gdcaddr >= 9 && svga->gdcaddr <= 0x0e) {
                if ((svga->gdcreg[0x0f] & 7) != 5)
                    return;
            }

            switch (svga->gdcaddr) {
                case 6:
                    if ((svga->gdcreg[6] & 0x0c) != (val & 0xc)) {
                        switch (val & 0x0c) {
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

                            default:
                                break;
                        }
                    }
                    svga->gdcreg[6] = val;
                    paradise_remap(paradise);
                    return;

                case 9:
                case 0x0a:
                    svga->gdcreg[svga->gdcaddr] = val;
                    paradise_remap(paradise);
                    return;
                case 0x0b:
                    svga->gdcreg[0x0b] = val;
                    svga->gdcreg[0x0b] &= ~0xc0;
                    if (paradise->memory == 1024)
                        svga->gdcreg[0x0b] |= 0xc0;
                    else if (paradise->memory == 512)
                        svga->gdcreg[0x0b] |= 0x80;
                    else
                        svga->gdcreg[0x0b] |= 0x40;

                    paradise_remap(paradise);
                    return;
                case 0x0e:
                    svga->gdcreg[0x0e] = val;
                    svga_recalctimings(svga);
                    return;

                default:
                    break;
            }
            break;

        case 0x3D4:
            svga->crtcreg = val & 0x3f;
            return;
        case 0x3D5:
            if ((paradise->type == PVGA1A) && (svga->crtcreg & 0x20))
                return;
            if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                return;
            if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);
            if (svga->crtcreg > 0x29 && (svga->crtc[0x29] & 7) != 5)
                return;
            if (svga->crtcreg >= 0x31 && svga->crtcreg <= 0x37)
                return;
            old                       = svga->crtc[svga->crtcreg];
            svga->crtc[svga->crtcreg] = val;

            if (old != val) {
                if (svga->crtcreg < 0xe || svga->crtcreg > 0x10) {
                    if ((svga->crtcreg == 0xc) || (svga->crtcreg == 0xd)) {
                        svga->fullchange = 3;
                        svga->ma_latch   = ((svga->crtc[0xc] << 8) | svga->crtc[0xd]) + ((svga->crtc[8] & 0x60) >> 5);
                    } else {
                        svga->fullchange = changeframecount;
                        svga_recalctimings(svga);
                    }
                }
            }
            break;

        default:
            break;
    }

    svga_out(addr, val, svga);
}

void
paradise_remap(paradise_t *paradise)
{
    svga_t *svga    = &paradise->svga;

    if (svga->seqregs[0x11] & 0x80) {
        paradise->read_bank[0] = paradise->read_bank[2] = svga->gdcreg[9] << 12;
        paradise->read_bank[1] = paradise->read_bank[3] = (svga->gdcreg[9] << 12) + ((svga->gdcreg[6] & 0x08) ? 0 : 0x8000);
        paradise->write_bank[0] = paradise->write_bank[2] = svga->gdcreg[0x0a] << 12;
        paradise->write_bank[1] = paradise->write_bank[3] = (svga->gdcreg[0x0a] << 12) + ((svga->gdcreg[6] & 0x08) ? 0 : 0x8000);
    } else if (svga->gdcreg[0x0b] & 0x08) {
        if (svga->gdcreg[6] & 0x0c) {
            paradise->read_bank[0] = paradise->read_bank[2] = svga->gdcreg[0x0a] << 12;
            paradise->write_bank[0] = paradise->write_bank[2] = svga->gdcreg[0x0a] << 12;
            paradise->read_bank[1] = paradise->read_bank[3] = (svga->gdcreg[9] << 12) + ((svga->gdcreg[6] & 0x08) ? 0 : 0x8000);
            paradise->write_bank[1] = paradise->write_bank[3] = (svga->gdcreg[9] << 12) + ((svga->gdcreg[6] & 0x08) ? 0 : 0x8000);
        } else {
            paradise->read_bank[0] = paradise->write_bank[0] = svga->gdcreg[0x0a] << 12;
            paradise->read_bank[1] = paradise->write_bank[1] = (svga->gdcreg[0xa] << 12) + ((svga->gdcreg[6] & 0x08) ? 0 : 0x8000);
            paradise->read_bank[2] = paradise->write_bank[2] = svga->gdcreg[9] << 12;
            paradise->read_bank[3] = paradise->write_bank[3] = (svga->gdcreg[9] << 12) + ((svga->gdcreg[6] & 0x08) ? 0 : 0x8000);
        }
    } else {
        paradise->read_bank[0] = paradise->read_bank[2] = svga->gdcreg[9] << 12;
        paradise->read_bank[1] = paradise->read_bank[3] = (svga->gdcreg[9] << 12) + ((svga->gdcreg[6] & 0x08) ? 0 : 0x8000);
        paradise->write_bank[0] = paradise->write_bank[2] = svga->gdcreg[9] << 12;
        paradise->write_bank[1] = paradise->write_bank[3] = (svga->gdcreg[9] << 12) + ((svga->gdcreg[6] & 0x08) ? 0 : 0x8000);
    }

    /*There are separate drivers for 1M and 512K/256K versions of the PVGA chips.*/
    if ((svga->gdcreg[0x0b] & 0xc0) < 0xc0) {
        paradise->read_bank[1] &= 0x7ffff;
        paradise->write_bank[1] &= 0x7ffff;
    } else {
        paradise->read_bank[1] &= 0xfffff;
        paradise->write_bank[1] &= 0xfffff;
    }
}

void
paradise_recalctimings(svga_t *svga)
{
    paradise_t *paradise = (paradise_t *) svga->priv;

    svga->lowres = !(svga->gdcreg[0x0e] & 0x01);

    if (paradise->type == WD90C30) {
        if (svga->crtc[0x3e] & 0x01)
            svga->vtotal |= 0x400;
        if (svga->crtc[0x3e] & 0x02)
            svga->dispend |= 0x400;
        if (svga->crtc[0x3e] & 0x04)
            svga->vsyncstart |= 0x400;
        if (svga->crtc[0x3e] & 0x08)
            svga->vblankstart |= 0x400;
        if (svga->crtc[0x3e] & 0x10)
            svga->split |= 0x400;

        svga->interlace = !!(svga->crtc[0x2d] & 0x20);
    }

    if (paradise->type < WD90C30) {
        if ((svga->gdcreg[6] & 1) || (svga->attrregs[0x10] & 1)) {
            if ((svga->bpp >= 8) && !svga->lowres) {
                svga->render = svga_render_8bpp_highres;
                svga->vram_display_mask = (svga->crtc[0x2f] & 0x02) ? 0x3ffff : paradise->vram_mask;
            }
        }
    } else {
        if ((svga->gdcreg[6] & 1) || (svga->attrregs[0x10] & 1)) {
            if ((svga->bpp >= 8) && !svga->lowres) {
                if (svga->bpp == 16) {
                    svga->render = svga_render_16bpp_highres;
                    svga->hdisp >>= 1;
                    if (svga->hdisp == 788)
                        svga->hdisp += 12;
                } else if (svga->bpp == 15) {
                    svga->render = svga_render_15bpp_highres;
                    svga->hdisp >>= 1;
                    if (svga->hdisp == 788)
                        svga->hdisp += 12;
                } else
                    svga->render = svga_render_8bpp_highres;

                svga->vram_display_mask = (svga->crtc[0x2f] & 0x02) ? 0x3ffff : paradise->vram_mask;
            } else if ((svga->bpp <= 8) && svga->lowres && !svga->interlace && (svga->hdisp >= 1024) &&
                       (svga->miscout >= 0x27) && (svga->miscout <= 0x2f))
                svga->interlace = 1; /*Horrible tweak to re-enable the interlace after returning to
                                       a windowed DOS box in Win3.x*/
        }
    }
}

static void
paradise_write(uint32_t addr, uint8_t val, void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;
    svga_t     *svga     = &paradise->svga;
    uint32_t    prev_addr;
    uint32_t    prev_addr2;

    addr = (addr & 0x7fff) + paradise->write_bank[(addr >> 15) & 3];

    /*Could be done in a better way but it works.*/
    if (!svga->lowres || (svga->attrregs[0x10] & 0x40)) {
        if (((svga->gdcreg[6] & 0x0c) == 0x04) && (svga->crtc[0x14] & 0x40) && ((svga->gdcreg[0x0b] & 0xc0) == 0xc0) && !svga->chain4) {
            prev_addr  = addr & 3;
            prev_addr2 = addr & 0xfffc;
            if (prev_addr == 3) {
                if ((addr & 0x30000) != 0x30000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else if (prev_addr == 2) {
                if ((addr & 0x30000) != 0x20000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else if (prev_addr == 1) {
                if ((addr & 0x30000) != 0x10000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else {
                if (addr & 0x30000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            }
        }
    }
    svga_write_linear(addr, val, svga);
}

static void
paradise_writew(uint32_t addr, uint16_t val, void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;
    svga_t     *svga     = &paradise->svga;
    uint32_t    prev_addr;
    uint32_t    prev_addr2;

    addr = (addr & 0x7fff) + paradise->write_bank[(addr >> 15) & 3];

    /*Could be done in a better way but it works.*/
    if (!svga->lowres || (svga->attrregs[0x10] & 0x40)) {
        if (((svga->gdcreg[6] & 0x0c) == 0x04) && (svga->crtc[0x14] & 0x40) && ((svga->gdcreg[0x0b] & 0xc0) == 0xc0) && !svga->chain4) {
            prev_addr  = addr & 3;
            prev_addr2 = addr & 0xfffc;
            if (prev_addr == 3) {
                if ((addr & 0x30000) != 0x30000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else if (prev_addr == 2) {
                if ((addr & 0x30000) != 0x20000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else if (prev_addr == 1) {
                if ((addr & 0x30000) != 0x10000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else {
                if (addr & 0x30000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            }
        }
    }
    svga_writew_linear(addr, val, svga);
}

static uint8_t
paradise_read(uint32_t addr, void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;
    svga_t     *svga     = &paradise->svga;
    uint32_t    prev_addr;
    uint32_t    prev_addr2;

    addr = (addr & 0x7fff) + paradise->read_bank[(addr >> 15) & 3];

    /*Could be done in a better way but it works.*/
    if (!svga->lowres || (svga->attrregs[0x10] & 0x40)) {
        if (((svga->gdcreg[6] & 0x0c) == 0x04) && (svga->crtc[0x14] & 0x40) && ((svga->gdcreg[0x0b] & 0xc0) == 0xc0) && !svga->chain4) {
            prev_addr  = addr & 3;
            prev_addr2 = addr & 0xfffc;
            if (prev_addr == 3) {
                if ((addr & 0x30000) != 0x30000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else if (prev_addr == 2) {
                if ((addr & 0x30000) != 0x20000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else if (prev_addr == 1) {
                if ((addr & 0x30000) != 0x10000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else {
                if (addr & 0x30000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            }
        }
    }
    return svga_read_linear(addr, svga);
}

static uint16_t
paradise_readw(uint32_t addr, void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;
    svga_t     *svga     = &paradise->svga;
    uint32_t    prev_addr;
    uint32_t    prev_addr2;

    addr = (addr & 0x7fff) + paradise->read_bank[(addr >> 15) & 3];

    /*Could be done in a better way but it works.*/
    if (!svga->lowres || (svga->attrregs[0x10] & 0x40)) {
        if (((svga->gdcreg[6] & 0x0c) == 0x04) && (svga->crtc[0x14] & 0x40) && ((svga->gdcreg[0x0b] & 0xc0) == 0xc0) && !svga->chain4) {
            prev_addr  = addr & 3;
            prev_addr2 = addr & 0xfffc;
            if (prev_addr == 3) {
                if ((addr & 0x30000) != 0x30000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else if (prev_addr == 2) {
                if ((addr & 0x30000) != 0x20000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else if (prev_addr == 1) {
                if ((addr & 0x30000) != 0x10000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            } else {
                if (addr & 0x30000)
                    addr = (addr >> 16) | (prev_addr << 16) | prev_addr2;
            }
        }
    }
    return svga_readw_linear(addr, svga);
}

void *
paradise_init(const device_t *info, uint32_t memory)
{
    paradise_t *paradise = malloc(sizeof(paradise_t));
    svga_t     *svga     = &paradise->svga;
    memset(paradise, 0, sizeof(paradise_t));

    if (info->local == PVGA1A)
        video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_paradise_pvga1a);
    else
        video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_paradise_wd90c);

    paradise->memory = memory;

    switch (info->local) {
        case PVGA1A:
            svga_init(info, svga, paradise, (memory << 10), /*256kb default*/
                      paradise_recalctimings,
                      paradise_in, paradise_out,
                      NULL,
                      NULL);
            paradise->vram_mask = (memory << 10) - 1;
            svga->decode_mask   = (memory << 10) - 1;
            break;
        case WD90C11:
            svga_init(info, svga, paradise, (memory << 10), /*512kb default*/
                      paradise_recalctimings,
                      paradise_in, paradise_out,
                      NULL,
                      NULL);
            paradise->vram_mask = (memory << 10) - 1;
            svga->decode_mask   = (memory << 10) - 1;
            break;
        case WD90C30:
            svga_init(info, svga, paradise, (memory << 10),
                      paradise_recalctimings,
                      paradise_in, paradise_out,
                      NULL,
                      NULL);
            paradise->vram_mask = (memory << 10) - 1;
            svga->decode_mask   = (memory << 10) - 1;
            svga->ramdac        = device_add(&sc11487_ramdac_device); /*Actually a Winbond W82c487-80, probably a clone.*/
            break;

        default:
            break;
    }

    svga->read = paradise_read;
    svga->readw = paradise_readw;
    svga->readl = NULL;
    svga->write = paradise_write;
    svga->writew = paradise_writew;
    svga->writel = NULL;
    mem_mapping_set_handler(&svga->mapping, paradise_read, paradise_readw, NULL, paradise_write, paradise_writew, NULL);
    mem_mapping_set_p(&svga->mapping, paradise);

    io_sethandler(0x03c0, 0x0020, paradise_in, NULL, NULL, paradise_out, NULL, NULL, paradise);

    /* Common to all three types. */
    svga->crtc[0x31] = 'W';
    svga->crtc[0x32] = 'D';
    svga->crtc[0x33] = '9';
    svga->crtc[0x34] = '0';
    svga->crtc[0x35] = 'C';

    switch (info->local) {
        case WD90C11:
            svga->crtc[0x36] = '1';
            svga->crtc[0x37] = '1';
            break;
        case WD90C30:
            svga->crtc[0x36] = '3';
            svga->crtc[0x37] = '0';
            break;

        default:
            break;
    }

    svga->bpp     = 8;
    svga->miscout = 1;

    paradise->type = info->local;

    return paradise;
}

static void *
paradise_pvga1a_ncr3302_init(const device_t *info)
{
    paradise_t *paradise = paradise_init(info, 256);

    if (paradise)
        rom_init(&paradise->bios_rom, "roms/machines/3302/c000-wd_1987-1989-740011-003058-019c.bin", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    return paradise;
}

static void *
paradise_pvga1a_pc2086_init(const device_t *info)
{
    paradise_t *paradise = paradise_init(info, 256);

    if (paradise)
        rom_init(&paradise->bios_rom, "roms/machines/pc2086/40186.ic171", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    return paradise;
}

static void *
paradise_pvga1a_pc3086_init(const device_t *info)
{
    paradise_t *paradise = paradise_init(info, 256);

    if (paradise)
        rom_init(&paradise->bios_rom, "roms/machines/pc3086/c000.bin", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    return paradise;
}

static void *
paradise_pvga1a_standalone_init(const device_t *info)
{
    paradise_t *paradise;
    uint32_t memsize = device_get_config_int("memory");

    paradise = paradise_init(info, memsize);

    if (paradise)
        rom_init(&paradise->bios_rom, "roms/video/pvga1a/BIOS.BIN", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    return paradise;
}

static int
paradise_pvga1a_standalone_available(void)
{
    return rom_present("roms/video/pvga1a/BIOS.BIN");
}

static void *
paradise_wd90c11_megapc_init(const device_t *info)
{
    paradise_t *paradise = paradise_init(info, 512);

    if (paradise)
        rom_init_interleaved(&paradise->bios_rom,
                             "roms/machines/megapc/41651-bios lo.u18",
                             "roms/machines/megapc/211253-bios hi.u19",
                             0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    return paradise;
}

static void *
paradise_wd90c11_standalone_init(const device_t *info)
{
    paradise_t *paradise = paradise_init(info, 512);

    if (paradise)
        rom_init(&paradise->bios_rom, "roms/video/wd90c11/WD90C11.VBI", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    return paradise;
}

static int
paradise_wd90c11_standalone_available(void)
{
    return rom_present("roms/video/wd90c11/WD90C11.VBI");
}

static void *
paradise_wd90c30_standalone_init(const device_t *info)
{
    paradise_t *paradise;
    uint32_t memsize = device_get_config_int("memory");

    paradise = paradise_init(info, memsize);

    if (paradise)
        rom_init(&paradise->bios_rom, "roms/video/wd90c30/90C30-LR.VBI", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    return paradise;
}

static int
paradise_wd90c30_standalone_available(void)
{
    return rom_present("roms/video/wd90c30/90C30-LR.VBI");
}

void
paradise_close(void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;

    svga_close(&paradise->svga);

    free(paradise);
}

void
paradise_speed_changed(void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;

    svga_recalctimings(&paradise->svga);
}

void
paradise_force_redraw(void *priv)
{
    paradise_t *paradise = (paradise_t *) priv;

    paradise->svga.fullchange = changeframecount;
}

const device_t paradise_pvga1a_pc2086_device = {
    .name          = "Paradise PVGA1A (Amstrad PC2086)",
    .internal_name = "pvga1a_pc2086",
    .flags         = 0,
    .local         = PVGA1A,
    .init          = paradise_pvga1a_pc2086_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .config        = NULL
};

const device_t paradise_pvga1a_pc3086_device = {
    .name          = "Paradise PVGA1A (Amstrad PC3086)",
    .internal_name = "pvga1a_pc3086",
    .flags         = 0,
    .local         = PVGA1A,
    .init          = paradise_pvga1a_pc3086_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .config        = NULL
};

static const device_config_t paradise_pvga1a_config[] = {
  // clang-format off
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 512,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "256 KB", .value = 256 },
            { .description = "512 KB", .value = 512 },
            { .description = ""                     }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t paradise_pvga1a_ncr3302_device = {
    .name          = "Paradise PVGA1A (NCR 3302)",
    .internal_name = "pvga1a_ncr3302",
    .flags         = 0,
    .local         = PVGA1A,
    .init          = paradise_pvga1a_ncr3302_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .config        = paradise_pvga1a_config
};

const device_t paradise_pvga1a_device = {
    .name          = "Paradise PVGA1A",
    .internal_name = "pvga1a",
    .flags         = DEVICE_ISA,
    .local         = PVGA1A,
    .init          = paradise_pvga1a_standalone_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = paradise_pvga1a_standalone_available,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .config        = paradise_pvga1a_config
};

const device_t paradise_wd90c11_megapc_device = {
    .name          = "Paradise WD90C11 (Amstrad MegaPC)",
    .internal_name = "wd90c11_megapc",
    .flags         = 0,
    .local         = WD90C11,
    .init          = paradise_wd90c11_megapc_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .config        = NULL
};

const device_t paradise_wd90c11_device = {
    .name          = "Paradise WD90C11-LR",
    .internal_name = "wd90c11",
    .flags         = DEVICE_ISA,
    .local         = WD90C11,
    .init          = paradise_wd90c11_standalone_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = paradise_wd90c11_standalone_available,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .config        = NULL
};

static const device_config_t paradise_wd90c30_config[] = {
  // clang-format off
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 1024,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "256 KB", .value =  256 },
            { .description = "512 KB", .value =  512 },
            { .description = "1 MB",   .value = 1024 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t paradise_wd90c30_device = {
    .name          = "Paradise WD90C30-LR",
    .internal_name = "wd90c30",
    .flags         = DEVICE_ISA,
    .local         = WD90C30,
    .init          = paradise_wd90c30_standalone_init,
    .close         = paradise_close,
    .reset         = NULL,
    .available     = paradise_wd90c30_standalone_available,
    .speed_changed = paradise_speed_changed,
    .force_redraw  = paradise_force_redraw,
    .config        = paradise_wd90c30_config
};
