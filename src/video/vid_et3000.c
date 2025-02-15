/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Tseng Labs ET3000.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2018 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/mca.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>

#define BIOS_ROM_PATH "roms/video/et3000/Tseng ET3000AX ISA VGA-VGA ULTRA.bin"

typedef struct {
    const char *name;
    int         type;

    svga_t svga;

    rom_t bios_rom;

    uint8_t pel_wd;
    uint8_t banking;
    uint8_t reg_3d8;
    uint8_t reg_3bf;
    uint8_t tries;
    uint8_t ext_enable;
} et3000_t;

static video_timings_t timing_et3000_isa = { VIDEO_ISA, 3, 3, 6, 5, 5, 10 };

static uint8_t et3000_in(uint16_t addr, void *priv);
static void    et3000_out(uint16_t addr, uint8_t val, void *priv);

#ifdef ENABLE_ET3000_LOG
int svga_do_log = ENABLE_ET3000_LOG;

static void
et3000_log(const char *fmt, ...)
{
    va_list ap;

    if (et3000_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define et3000_log(fmt, ...)
#endif

static uint8_t
et3000_in(uint16_t addr, void *priv)
{
    et3000_t *dev  = (et3000_t *) priv;
    svga_t   *svga = &dev->svga;
    uint8_t  ret = 0xff;

    if ((addr >= 0x03b0) && (addr < 0x03bc) && (svga->miscout & 1))
        return 0xff;

    if ((addr >= 0x03d0) && (addr < 0x03dc) && !(svga->miscout & 1))
        return 0xff;

    switch (addr) {
        default:
            ret = svga_in(addr, svga);

#ifdef ENABLE_ET3000_LOG
            if (addr != 0x03da)
                et3000_log("[%04X:%08X] [R] %04X = %02X\n", CS, cpu_state.pc, addr, ret);
#endif
            break;

        case 0x3c1:
            /* It appears the extended attribute registers are **NOT**
               protected by key on the ET3000AX, as the BIOS attempts to
               write to attribute register 16h without the key. */
            ret = svga_in(addr, svga);
            et3000_log("[%04X:%08X] [R] %04X: %02X = %02X (%i)\n", CS, cpu_state.pc,
                       addr, svga->attraddr, ret, dev->ext_enable);
            break;

        case 0x3c5:
            if ((svga->seqaddr >= 6) && !dev->ext_enable)
                ret = 0xff;
            else
                ret = svga_in(addr, svga);
            et3000_log("[%04X:%08X] [R] %04X: %02X = %02X (%i)\n", CS, cpu_state.pc,
                       addr, svga->seqaddr, ret, dev->ext_enable);
            break;

        case 0x3cf:
            if ((svga->gdcaddr >= 0x0d) && !dev->ext_enable)
                ret = 0xff;
            else
                ret = svga_in(addr, svga);
            et3000_log("[%04X:%08X] [R] %04X: %02X = %02X (%i)\n", CS, cpu_state.pc,
                       addr, svga->gdcaddr & 15, ret, dev->ext_enable);
            break;

        case 0x3cb: /*PEL Address/Data Wd*/
            ret = dev->pel_wd;
            et3000_log("[%04X:%08X] [R] %04X = %02X\n", CS, cpu_state.pc, addr, ret);
            break;

        case 0x3cd: /*Banking*/
            ret = dev->banking;
            et3000_log("[%04X:%08X] [R] %04X = %02X\n", CS, cpu_state.pc, addr, ret);
            break;

        case 0x3b4:
        case 0x3d4:
            ret = svga->crtcreg;
            et3000_log("[%04X:%08X] [R] %04X = %02X\n", CS, cpu_state.pc, addr, ret);
            break;

        case 0x3b5:
        case 0x3d5:
            if ((svga->crtcreg >= 0x18) && (svga->crtcreg < 0x23) && !dev->ext_enable)
                ret = 0xff;
            else if (svga->crtcreg > 0x25)
                ret = 0xff;
            else
                ret = svga->crtc[svga->crtcreg];
            et3000_log("[%04X:%08X] [R] %04X: %02X = %02X\n", CS, cpu_state.pc,
                       addr, svga->crtcreg, ret);
            break;

        case 0x3b8:
        case 0x3d8:
            ret = dev->reg_3d8;
            et3000_log("[%04X:%08X] [R] %04X = %02X\n", CS, cpu_state.pc, addr, ret);
            break;

        case 0x3ba:
        case 0x3da:
            svga->attrff = 0;

            if (svga->cgastat & 0x01)
                svga->cgastat &= ~0x30;
            else
                svga->cgastat ^= 0x30;

            ret = svga->cgastat;

            if ((svga->fcr & 0x08) && svga->dispon)
                ret |= 0x08;
            break;

        case 0x3bf:
            ret = dev->reg_3bf;
            et3000_log("[%04X:%08X] [R] %04X = %02X\n", CS, cpu_state.pc, addr, ret);
            break;
    }

    return ret;
}

static void
et3000_out(uint16_t addr, uint8_t val, void *priv)
{
    et3000_t *dev  = (et3000_t *) priv;
    svga_t   *svga = &dev->svga;
    uint8_t   old;
    uint8_t   index;

    et3000_log("[%04X:%08X] [W] %04X = %02X\n", CS, cpu_state.pc, addr, val);

    if ((addr >= 0x03b0) && (addr < 0x03bc) && (svga->miscout & 1))
        return;

    if ((addr >= 0x03d0) && (addr < 0x03dc) && !(svga->miscout & 1))
        return;

    switch (addr) {
        case 0x3c0:
            /* It appears the extended attribute registers are **NOT**
               protected by key on the ET3000AX, as the BIOS attempts to
               write to attribute register 16h without the key. */
            if (svga->attrff && (svga->attraddr == 0x11) && (svga->attrregs[0x16] & 0x01))
                val = (val & 0xf0) | (svga->attrregs[0x11] & 0x0f);
#ifdef ENABLE_ET3000_LOG
            if (svga->attrff && (svga->attraddr > 0x14))
                et3000_log("3C1: %02X = %02X\n", svga->attraddr, val);
#endif
            if (svga->attrff && (svga->attraddr == 0x16)) {
                svga->attrregs[0x16] = val;
                svga->chain4 &= ~0x10;
                if (svga->gdcreg[5] & 0x40)
                    svga->chain4 |= (svga->attrregs[0x16] & 0x10);
                svga_recalctimings(svga);
                return;
            }
            break;
        case 0x3c1:
            return;

        case 0x3c2:
            svga->miscout  = val;
            svga->vidclock = val & 4;
            svga_recalctimings(svga);
            return;

        case 0x3c4:
            svga->seqaddr = val & 0x07;
            return;
        case 0x3c5:
            if ((svga->seqaddr >= 6) && !dev->ext_enable)
                return;

            if (svga->seqaddr == 4) {
                svga->seqregs[4] = val;

                svga->chain2_write = !(val & 4);
                svga->chain4       = (svga->chain4 & ~8) | (val & 8);
                et3000_log("CHAIN2 = %i, CHAIN4 = %i\n", svga->chain2_write, svga->chain4);
                svga->fast         = (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) &&
                                     !svga->gdcreg[1]) && svga->chain4 &&
                                     !(svga->adv_flags & FLAG_ADDR_BY8);
                return;
            }
#ifdef ENABLE_ET3000_LOG
            else if (svga->seqaddr > 4)
                et3000_log("3C5: %02X = %02X\n", svga->seqaddr, val);
#endif
            break;

        case 0x3c9:
            if (svga->adv_flags & FLAG_RAMDAC_SHIFT)
                val <<= 2;
            svga->fullchange = svga->monitor->mon_changeframecount;
            switch (svga->dac_pos) {
                case 0:
                    if (!(svga->attrregs[0x16] & 0x02) && !(svga->attrregs[0x17] & 0x80))
                        svga->dac_r = val;
                    svga->dac_pos++;
                    break;
                case 1:
                    if (!(svga->attrregs[0x16] & 0x02) && !(svga->attrregs[0x17] & 0x80))
                        svga->dac_g = val;
                    svga->dac_pos++;
                    break;
                case 2:
                    index                 = svga->dac_addr & 255;
                    if (!(svga->attrregs[0x16] & 0x02) && !(svga->attrregs[0x17] & 0x80)) {
                        svga->dac_b           = val;
                        svga->vgapal[index].r = svga->dac_r;
                        svga->vgapal[index].g = svga->dac_g;
                        svga->vgapal[index].b = svga->dac_b;
                        if (svga->ramdac_type == RAMDAC_8BIT)
                            svga->pallook[index] = makecol32(svga->vgapal[index].r, svga->vgapal[index].g,
                                                             svga->vgapal[index].b);
                        else
                            svga->pallook[index] = makecol32(video_6to8[svga->vgapal[index].r & 0x3f],
                                                             video_6to8[svga->vgapal[index].g & 0x3f],
                                                             video_6to8[svga->vgapal[index].b & 0x3f]);
                    }
                    svga->dac_pos  = 0;
                    svga->dac_addr = (svga->dac_addr + 1) & 255;
                    break;

                default:
                    break;
            }
            return;

        case 0x3cb: /*PEL Address/Data Wd*/
            et3000_log("3CB = %02X\n", val);
            dev->pel_wd = val;
            break;

        case 0x3cd: /*Banking*/
            et3000_log("3CD = %02X\n", val);
            if (!(svga->crtc[0x23] & 0x80) && !(svga->gdcreg[6] & 0x08)) {
                switch ((val >> 6) & 3) {
                    case 0: /*128K segments*/
                        svga->write_bank = ((val >> 0) & 7) << 17;
                        svga->read_bank  = ((val >> 3) & 7) << 17;
                        break;
                    case 1: /*64K segments*/
                        svga->write_bank = (val & 7) << 16;
                        svga->read_bank  = ((val >> 3) & 7) << 16;
                        break;

                    default:
                        break;
                }
            }
            dev->banking = val;
            return;

        case 0x3ce:
            svga->gdcaddr = val & 0x0f;
            return;
        case 0x3cf:
            if ((svga->gdcaddr >= 0x0d) && !dev->ext_enable)
                return;

            if ((svga->gdcaddr & 15) == 5) {
                svga->chain4 &= ~0x10;
                if (val & 0x40)
                    svga->chain4 |= (svga->attrregs[0x16] & 0x10);
            } else if ((svga->gdcaddr & 15) == 6) {
                if (!(svga->crtc[0x23] & 0x80) && !(val & 0x08)) {
                    switch ((dev->banking >> 6) & 3) {
                        case 0: /*128K segments*/
                            svga->write_bank = ((dev->banking >> 0) & 7) << 17;
                            svga->read_bank  = ((dev->banking >> 3) & 7) << 17;
                            break;
                        case 1: /*64K segments*/
                            svga->write_bank = (dev->banking & 7) << 16;
                            svga->read_bank  = ((dev->banking >> 3) & 7) << 16;
                            break;

                        default:
                            break;
                    }
                } else
                    svga->write_bank = svga->read_bank = 0;

                old = svga->gdcreg[6];
                svga_out(addr, val, svga);
                if ((old & 0xc) != 0 && (val & 0xc) == 0) {
                    /* Override mask - ET3000 supports linear 128k at A0000. */
                    svga->banked_mask = 0x1ffff;
                }
                return;
            }
#ifdef ENABLE_ET3000_LOG
            else if ((svga->gdcaddr & 15) > 8)
                et3000_log("3CF: %02X = %02X\n", (svga->gdcaddr & 15), val);
#endif
            break;

        case 0x3b4:
        case 0x3d4:
            svga->crtcreg = val & 0x3f;
            return;

        case 0x3b5:
        case 0x3d5:
            if ((svga->crtcreg >= 0x18) && (svga->crtcreg < 0x23) && !dev->ext_enable)
                return;
            else if (svga->crtcreg > 0x25)
                return;

            /* Unlike the ET4000AX, which protects all bits of the
               overflow high register (0x35 there, 0x25 here) except for
               bits 4 and 7, if bit 7 of CRTC 11h is set, the ET3000AX
               does not to that. */
            if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                return;
            if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);
            old                       = svga->crtc[svga->crtcreg];
            svga->crtc[svga->crtcreg] = val;

#ifdef ENABLE_ET3000_LOG
            if (svga->crtcreg > 0x18)
                et3000_log("3D5: %02X = %02X\n", svga->crtcreg, val);
#endif

            if (old != val) {
                if (svga->crtcreg < 0x0e || svga->crtcreg > 0x10) {
                    svga->fullchange = changeframecount;
                    svga_recalctimings(svga);
                }
            }
            break;

        case 0x3b8:
        case 0x3d8:
            et3000_log("%04X = %02X\n", addr, val);
            dev->reg_3d8 = val;
            if ((val == 0xa0) && (dev->tries == 1)) {
                dev->ext_enable = 1;
                dev->tries = 0;
            } else if (val == 0x29)
                dev->tries = 1;
            return;

        case 0x3bf:
            et3000_log("%04X = %02X\n", addr, val);
            dev->reg_3bf = val;
            if ((val == 0x01) && (dev->tries == 1)) {
                dev->ext_enable = 0;
                dev->tries = 0;
            } else if (val == 0x03)
                dev->tries = 1;
            return;

        default:
            break;
    }

    svga_out(addr, val, svga);
}

static void
et3000_recalctimings(svga_t *svga)
{
    svga->ma_latch |= (svga->crtc[0x23] & 2) << 15;
    if (svga->crtc[0x25] & 1)
        svga->vblankstart |= 0x400;
    if (svga->crtc[0x25] & 2)
        svga->vtotal |= 0x400;
    if (svga->crtc[0x25] & 4)
        svga->dispend |= 0x400;
    if (svga->crtc[0x25] & 8)
        svga->vsyncstart |= 0x400;
    if (svga->crtc[0x25] & 0x10)
        svga->split |= 0x400;

    svga->interlace = !!(svga->crtc[0x25] & 0x80);

    if (svga->attrregs[0x16] & 0x10) {
        svga->ma_latch <<= (1 << 0);
        svga->rowoffset <<= (1 << 0);
        switch (svga->gdcreg[5] & 0x60) {
            case 0x00:
                svga->render = svga_render_4bpp_highres;
                svga->hdisp *= 2;
                break;
            case 0x20:
                svga->render = svga_render_2bpp_highres;
                break;
            case 0x40:
            case 0x60:
                svga->render = svga_render_8bpp_highres;
                break;

            default:
                break;
        }
    }

    et3000_log("HDISP = %i, HTOTAL = %i, ROWOFFSET = %i, INTERLACE = %i\n",
               svga->hdisp, svga->htotal, svga->rowoffset, svga->interlace);

    switch (((svga->miscout >> 2) & 3) | ((svga->crtc[0x24] << 1) & 4)) {
        case 0:
        case 1:
            break;
        case 3:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 40000000.0;
            break;
        case 5:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 65000000.0;
            break;
        default:
            svga->clock = (cpuclock * (double) (1ULL << 32)) / 36000000.0;
            break;
    }
}

static void *
et3000_init(const device_t *info)
{
    const char *fn;
    et3000_t   *dev;

    dev = (et3000_t *) malloc(sizeof(et3000_t));
    memset(dev, 0x00, sizeof(et3000_t));
    dev->name = info->name;
    dev->type = info->local;
    fn        = BIOS_ROM_PATH;

    switch (dev->type) {
        case 0: /* ISA ET3000AX */
            video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_et3000_isa);
            svga_init(info, &dev->svga, dev, device_get_config_int("memory") << 10,
                      et3000_recalctimings, et3000_in, et3000_out,
                      NULL, NULL);
            io_sethandler(0x03b0, 48,
                          et3000_in, NULL, NULL, et3000_out, NULL, NULL, dev);
            break;

        default:
            break;
    }

    rom_init(&dev->bios_rom, fn,
             0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    dev->svga.bpp     = 8;
    dev->svga.miscout = 1;

    dev->svga.packed_chain4 = 1;

    return dev;
}

static void
et3000_close(void *priv)
{
    et3000_t *dev = (et3000_t *) priv;

    svga_close(&dev->svga);

    free(dev);
}

static void
et3000_speed_changed(void *priv)
{
    et3000_t *dev = (et3000_t *) priv;

    svga_recalctimings(&dev->svga);
}

static void
et3000_force_redraw(void *priv)
{
    et3000_t *dev = (et3000_t *) priv;

    dev->svga.fullchange = changeframecount;
}

static int
et3000_available(void)
{
    return rom_present(BIOS_ROM_PATH);
}

static const device_config_t et3000_config[] = {
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

const device_t et3000_isa_device = {
    .name          = "Tseng Labs ET3000AX (ISA)",
    .internal_name = "et3000ax",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = et3000_init,
    .close         = et3000_close,
    .reset         = NULL,
    .available     = et3000_available,
    .speed_changed = et3000_speed_changed,
    .force_redraw  = et3000_force_redraw,
    .config        = et3000_config
};
