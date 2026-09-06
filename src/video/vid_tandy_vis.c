#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_8514a.h>
#include <86box/vid_xga.h>
#include <86box/vid_svga.h>
#include "cpu.h"

typedef struct tvis_vid_t {
    svga_t   svga;

    uint8_t rdac_ext_cnt;

    uint8_t m_extreg;
    uint8_t m_extcnt;
} tvis_vid_t;

video_timings_t        timing_vis = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

void
tvis_recalc_banks(tvis_vid_t* tvis_vid)
{
    if (!(tvis_vid->svga.seqregs[0x25] & 0x40)) {
        tvis_vid->svga.fb_only = 0;
        tvis_vid->svga.read_bank = 0;
        tvis_vid->svga.write_bank = 0;
        return;
    }
    tvis_vid->svga.fb_only = 1;
    tvis_vid->svga.read_bank = ((tvis_vid->svga.seqregs[0x1e] & 0xf) == 3) ? (tvis_vid->svga.seqregs[0x19] | (tvis_vid->svga.seqregs[0x18] << 8)) : (tvis_vid->svga.seqregs[0x1d] | (tvis_vid->svga.seqregs[0x1c] << 8));
    tvis_vid->svga.read_bank *= 64;

    tvis_vid->svga.write_bank = tvis_vid->svga.seqregs[0x19] | (tvis_vid->svga.seqregs[0x18] << 8);
    tvis_vid->svga.write_bank *= 64;
}

void
tvis_out(uint16_t addr, uint8_t val, void *priv)
{
    tvis_vid_t  *vga  = (tvis_vid_t *) priv;
    svga_t *svga = &vga->svga;
    uint8_t old, o;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3c5:
            if (svga->seqaddr > 0x3f)
                return;
            o                                  = svga->seqregs[svga->seqaddr & 0x3f];
            svga->seqregs[svga->seqaddr & 0x3f] = val;
            if (o != val && (svga->seqaddr & 0x3f) == 1) {
                svga_recalctimings(svga);
            }
            switch (svga->seqaddr & 0x3f) {
                case 1:
                    if (svga->scrblank && !(val & 0x20))
                        svga->fullchange = 3;
                    svga->scrblank = (svga->scrblank & ~0x20) | (val & 0x20);
                    svga_recalctimings(svga);
                    break;
                case 2:
                    svga->writemask = val & 0xf;
                    break;
                case 3:
                    svga->charsetb = (((val >> 2) & 3) * 0x10000) + 2;
                    svga->charseta = ((val & 3) * 0x10000) + 2;
                    if (val & 0x10)
                        svga->charseta += 0x8000;
                    if (val & 0x20)
                        svga->charsetb += 0x8000;
                    break;
                case 4:
                    svga->chain2_write = !(val & 4);
                    svga->chain4       = (svga->chain4 & ~8) | (val & 8);
                    svga->fast         = (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) && !svga->gdcreg[1]) &&
                                        ((svga->chain4 && (svga->packed_chain4 || svga->force_old_addr)) || svga->fb_only) &&
                                        !(svga->adv_flags & FLAG_ADDR_BY8);
                    break;

                case 0x18 ... 0x1d:
                    tvis_recalc_banks(vga);
                    break;

                default:
                    break;
            }
            return;
        case 0x3C6:
            {
                if (vga->rdac_ext_cnt == 4) {
                    if((val & 0xc7) != 0xc7)
                        vga->m_extreg = val;
                    pclog("TVIS ramdac write\n");
                    vga->m_extcnt = 0;
                    return;
                }
                svga_out(addr, val, svga);
                return;
            }
        case 0x3D4:
            svga->crtcreg = val & 0x3f;
            return;
        case 0x3D5:
            if (svga->crtcreg & 0x20)
                return;
            if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                return;
            if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);
            old                       = svga->crtc[svga->crtcreg];
            svga->crtc[svga->crtcreg] = val;
            if (old != val) {
                if (svga->crtcreg < 0xe || svga->crtcreg > 0x10) {
                    if ((svga->crtcreg == 0xc) || (svga->crtcreg == 0xd)) {
                        svga->fullchange = 3;
                        svga->memaddr_latch   = ((svga->crtc[0xc] << 8) | svga->crtc[0xd]) + ((svga->crtc[8] & 0x60) >> 5);
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

uint8_t
tvis_in(uint16_t addr, void *priv)
{
    tvis_vid_t  *vga  = (tvis_vid_t *) priv;
    svga_t *svga = &vga->svga;
    uint8_t temp;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3c5:
            return svga->seqregs[svga->seqaddr & 0x3f];
        case 0x3C6:
            {
                if (vga->rdac_ext_cnt == 4) {
                    vga->rdac_ext_cnt = 0;
                    pclog("TVIS ramdac read\n");
                    return vga->m_extreg;
                }
                temp = svga_in(addr, svga);
                break;
            }
        case 0x3D4:
            temp = svga->crtcreg;
            break;
        case 0x3D5:
            if (svga->crtcreg & 0x20)
                temp = 0xff;
            else
                temp = svga->crtc[svga->crtcreg];
            break;
        default:
            temp = svga_in(addr, svga);
            break;
    }

    return temp;
}


void *
tvis_vid_init(const device_t *info)
{
    tvis_vid_t *vga = calloc(1, sizeof(tvis_vid_t));

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_vis);

    svga_init(info, &vga->svga, vga, 1 << 18, /*256kb*/
              NULL,
              tvis_in, tvis_out,
              NULL,
              NULL);

    vga->svga.bpp     = 8;
    vga->svga.miscout = 0;

    vga->svga.vga_enabled = 1;

    io_sethandler(0x03a0, 0x0040, tvis_in, NULL, NULL, tvis_out, NULL, NULL, vga);

    return vga;
}

void
tvis_vid_close(void *priv)
{
    tvis_vid_t *vga = (tvis_vid_t *) priv;

    svga_close(&vga->svga);

    free(vga);
}

void
tvis_vid_speed_changed(void *priv)
{
    tvis_vid_t *vga = (tvis_vid_t *) priv;

    svga_recalctimings(&vga->svga);
}

void
tvis_vid_force_redraw(void *priv)
{
    tvis_vid_t *vga = (tvis_vid_t *) priv;

    vga->svga.fullchange = changeframecount;
}

const device_t tvis_vid_device = {
    .name          = "Tandy VIS VGA",
    .internal_name = "tvis_vid",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = tvis_vid_init,
    .close         = tvis_vid_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = tvis_vid_speed_changed,
    .force_redraw  = tvis_vid_force_redraw,
    .config        = NULL
};
