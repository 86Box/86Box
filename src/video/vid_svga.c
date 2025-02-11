/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Generic SVGA handling.
 *
 *          This is intended to be used by another SVGA driver,
 *          and not as a card in its own right.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/video.h>
#include <86box/vid_8514a.h>
#include <86box/vid_xga.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/vid_xga_device.h>

void svga_doblit(int wx, int wy, svga_t *svga);
void svga_poll(void *priv);

svga_t *svga_8514;

extern int     cyc_total;
extern uint8_t edatlookup[4][4];

uint8_t svga_rotate[8][256];

/*Primary SVGA device. As multiple video cards are not yet supported this is the
  only SVGA device.*/
static svga_t *svga_pri;

#ifdef ENABLE_SVGA_LOG
int svga_do_log = ENABLE_SVGA_LOG;

static void
svga_log(const char *fmt, ...)
{
    va_list ap;

    if (svga_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define svga_log(fmt, ...)
#endif

svga_t *
svga_get_pri(void)
{
    return svga_pri;
}

void
svga_set_poll(svga_t *svga)
{
    svga_log("SVGA Timer activated, enabled?=%x.\n", timer_is_enabled(&svga->timer));
    timer_set_callback(&svga->timer, svga_poll);
    if (!timer_is_enabled(&svga->timer))
        timer_enable(&svga->timer);
}

void
svga_set_override(svga_t *svga, int val)
{
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    xga_t     *xga = (xga_t *) svga->xga;
    uint8_t    ret_poll = 0;

    if (svga->override && !val)
        svga->fullchange = svga->monitor->mon_changeframecount;

    svga->override = val;

    svga_log("Override=%x.\n", val);
    if (ibm8514_active && (svga->dev8514 != NULL))
        ret_poll |= 1;

    if (xga_active && (svga->xga != NULL))
        ret_poll |= 2;

    if (svga->override)
        svga_set_poll(svga);
    else {
        switch (ret_poll) {
            case 0:
            default:
                svga_set_poll(svga);
                break;

            case 1:
                if (ibm8514_active && (svga->dev8514 != NULL)) {
                    if (dev->on)
                        ibm8514_set_poll(svga);
                    else
                        svga_set_poll(svga);
                } else
                    svga_set_poll(svga);
                break;

            case 2:
                if (xga_active && (svga->xga != NULL)) {
                    if (xga->on)
                        xga_set_poll(svga);
                    else
                        svga_set_poll(svga);
                } else
                    svga_set_poll(svga);
                break;

            case 3:
                if (ibm8514_active && (svga->dev8514 != NULL) && xga_active && (svga->xga != NULL))  {
                    if (dev->on)
                        ibm8514_set_poll(svga);
                    else if (xga->on)
                        xga_set_poll(svga);
                    else
                        svga_set_poll(svga);
                } else
                    svga_set_poll(svga);
                break;
        }
    }

#ifdef OVERRIDE_OVERSCAN
    if (!val) {
        /* Override turned off, restore overscan X and Y per the CRTC. */
        svga->monitor->mon_overscan_y = (svga->rowcount + 1) << 1;

        if (svga->monitor->mon_overscan_y < 16)
            svga->monitor->mon_overscan_y = 16;

        svga->monitor->mon_overscan_x = (svga->seqregs[1] & 1) ? 16 : 18;

        if (svga->seqregs[1] & 8)
            svga->monitor->mon_overscan_x <<= 1;
    } else
        svga->monitor->mon_overscan_x = svga->monitor->mon_overscan_y = 16;
    /* Override turned off, fix overcan X and Y to 16. */
#endif
}

void
svga_out(uint16_t addr, uint8_t val, void *priv)
{
    svga_t    *svga = (svga_t *) priv;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    xga_t     *xga = (xga_t *) svga->xga;
    uint8_t    o;
    uint8_t    index;
    uint8_t    pal4to16[16] = { 0, 7, 0x38, 0x3f, 0, 3, 4, 0x3f, 0, 2, 4, 0x3e, 0, 3, 5, 0x3f };

    if ((addr >= 0x2ea) && (addr <= 0x2ed)) {
        if (!dev)
            return;
    }

    switch (addr) {
        case 0x2ea:
            dev->dac_mask = val;
            break;
        case 0x2eb:
        case 0x2ec:
            dev->dac_pos    = 0;
            dev->dac_status = addr & 0x03;
            dev->dac_addr   = (val + (addr & 0x01)) & 0xff;
            break;
        case 0x2ed:
            svga->fullchange = svga->monitor->mon_changeframecount;
            switch (dev->dac_pos) {
                case 0:
                    dev->dac_r = val;
                    dev->dac_pos++;
                    break;
                case 1:
                    dev->dac_g = val;
                    dev->dac_pos++;
                    break;
                case 2:
                    index                 = dev->dac_addr & 0xff;
                    dev->dac_b = val;
                    dev->_8514pal[index].r = dev->dac_r;
                    dev->_8514pal[index].g = dev->dac_g;
                    dev->_8514pal[index].b = dev->dac_b;
                    if (svga->ramdac_type == RAMDAC_8BIT)
                        dev->pallook[index] = makecol32(dev->_8514pal[index].r, dev->_8514pal[index].g, dev->_8514pal[index].b);
                    else
                        dev->pallook[index] = makecol32(video_6to8[dev->_8514pal[index].r & 0x3f], video_6to8[dev->_8514pal[index].g & 0x3f], video_6to8[dev->_8514pal[index].b & 0x3f]);
                    dev->dac_pos  = 0;
                    dev->dac_addr = (dev->dac_addr + 1) & 0xff;
                    break;

                default:
                    break;
            }
            break;

        case 0x3c0:
        case 0x3c1:
            if (!svga->attrff) {
                svga->attraddr = val & 0x1f;
                if ((val & 0x20) != svga->attr_palette_enable) {
                    svga->fullchange          = 3;
                    svga->attr_palette_enable = val & 0x20;
                    svga_log("Write Port %03x palette enable=%02x.\n", addr, svga->attr_palette_enable);
                    svga_recalctimings(svga);
                }
            } else {
                if ((svga->attraddr == 0x13) && (svga->attrregs[0x13] != val))
                    svga->fullchange = svga->monitor->mon_changeframecount;
                o                                   = svga->attrregs[svga->attraddr & 0x1f];
                svga->attrregs[svga->attraddr & 0x1f] = val;
                if (svga->attraddr < 0x10)
                    svga->fullchange = svga->monitor->mon_changeframecount;

                if ((svga->attraddr == 0x10) || (svga->attraddr == 0x14) || (svga->attraddr < 0x10)) {
                    for (int c = 0; c < 0x10; c++) {
                        if (svga->attrregs[0x10] & 0x80)
                            svga->egapal[c] = (svga->attrregs[c] & 0xf) | ((svga->attrregs[0x14] & 0xf) << 4);
                        else if (svga->ati_4color)
                            svga->egapal[c] = pal4to16[(c & 0x03) | ((val >> 2) & 0xc)];
                        else
                            svga->egapal[c] = (svga->attrregs[c] & 0x3f) | ((svga->attrregs[0x14] & 0xc) << 4);
                    }
                    svga->fullchange = svga->monitor->mon_changeframecount;
                }
                /* Recalculate timings on change of attribute register 0x11
                   (overscan border color) too. */
                if (svga->attraddr == 0x10) {
                    if (o != val) {
                        svga_log("ATTR10.\n");
                        svga_recalctimings(svga);
                    }
                } else if (svga->attraddr == 0x11) {
                    svga->overscan_color = svga->pallook[svga->attrregs[0x11]];
                    if (o != val) {
                        svga_log("ATTR11.\n");
                        svga_recalctimings(svga);
                    }
                } else if (svga->attraddr == 0x12) {
                    if ((val & 0xf) != svga->plane_mask)
                        svga->fullchange = svga->monitor->mon_changeframecount;
                    svga->plane_mask = val & 0xf;
                }
            }
            svga->attrff ^= 1;
            break;
        case 0x3c2:
            svga->miscout  = val;
            svga->vidclock = val & 4;
            io_removehandler(0x03a0, 0x0020, svga->video_in, NULL, NULL, svga->video_out, NULL, NULL, svga->priv);
            if (!(val & 1))
                io_sethandler(0x03a0, 0x0020, svga->video_in, NULL, NULL, svga->video_out, NULL, NULL, svga->priv);
            svga_recalctimings(svga);
            break;
        case 0x3c3:
            if (xga_active && xga)
                xga->on = (val & 0x01) ? 0 : 1;
            if (ibm8514_active && dev)
                dev->on = (val & 0x01) ? 0 : 1;

            svga_log("Write Port 3C3=%x.\n", val & 0x01);
            svga_recalctimings(svga);
            break;
        case 0x3c4:
            svga->seqaddr = val;
            break;
        case 0x3c5:
            if (svga->seqaddr > 0xf)
                return;
            o                                  = svga->seqregs[svga->seqaddr & 0xf];
            svga->seqregs[svga->seqaddr & 0xf] = val;
            if (o != val && (svga->seqaddr & 0xf) == 1) {
                svga_log("SEQADDR1 write1.\n");
                svga_recalctimings(svga);
            }
            switch (svga->seqaddr & 0xf) {
                case 1:
                    if (svga->scrblank && !(val & 0x20))
                        svga->fullchange = 3;
                    svga->scrblank = (svga->scrblank & ~0x20) | (val & 0x20);
                    svga_log("SEQADDR1 write2.\n");
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
                    svga->fast         = (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) && !svga->gdcreg[1]) && ((svga->chain4 && (svga->packed_chain4 || svga->force_old_addr)) || svga->fb_only) && !(svga->adv_flags & FLAG_ADDR_BY8);
                    break;

                default:
                    break;
            }
            break;
        case 0x3c6:
            svga->dac_mask = val;
            break;
        case 0x3c7:
        case 0x3c8:
            svga->dac_pos    = 0;
            svga->dac_status = addr & 0x03;
            svga->dac_addr   = (val + (addr & 0x01)) & 0xff;
            break;
        case 0x3c9:
            if (svga->adv_flags & FLAG_RAMDAC_SHIFT)
                val <<= 2;
            svga->fullchange = svga->monitor->mon_changeframecount;
            switch (svga->dac_pos) {
                case 0:
                    svga->dac_r = val;
                    svga->dac_pos++;
                    break;
                case 1:
                    svga->dac_g = val;
                    svga->dac_pos++;
                    break;
                case 2:
                    index                 = svga->dac_addr & 0xff;
                    svga->dac_b           = val;
                    svga->vgapal[index].r = svga->dac_r;
                    svga->vgapal[index].g = svga->dac_g;
                    svga->vgapal[index].b = svga->dac_b;
                    if (svga->ramdac_type == RAMDAC_8BIT)
                        svga->pallook[index] = makecol32(svga->vgapal[index].r, svga->vgapal[index].g, svga->vgapal[index].b);
                    else
                        svga->pallook[index] = makecol32(video_6to8[svga->vgapal[index].r & 0x3f], video_6to8[svga->vgapal[index].g & 0x3f], video_6to8[svga->vgapal[index].b & 0x3f]);
                    svga->dac_pos  = 0;
                    svga->dac_addr = (svga->dac_addr + 1) & 0xff;
                    break;

                default:
                    break;
            }
            break;
        case 0x3ce:
            svga->gdcaddr = val;
            break;
        case 0x3cf:
            o = svga->gdcreg[svga->gdcaddr & 15];
            switch (svga->gdcaddr & 15) {
                case 2:
                    svga->colourcompare = val;
                    break;
                case 4:
                    svga->readplane = val & 3;
                    break;
                case 5:
                    svga->writemode   = val & 3;
                    svga->readmode    = val & 8;
                    svga->chain2_read = val & 0x10;
                    break;
                case 6:
                    if ((svga->gdcreg[6] & 0xc) != (val & 0xc)) {
                        switch (val & 0xc) {
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
                    break;
                case 7:
                    svga->colournocare = val;
                    break;

                default:
                    break;
            }
            svga->gdcreg[svga->gdcaddr & 15] = val;
            svga->fast                       = (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) && !svga->gdcreg[1]) && ((svga->chain4 && (svga->packed_chain4 || svga->force_old_addr)) || svga->fb_only);
            if (((svga->gdcaddr & 15) == 5 && (val ^ o) & 0x70) || ((svga->gdcaddr & 15) == 6 && (val ^ o) & 1)) {
                svga_log("GDCADDR%02x recalc.\n", svga->gdcaddr & 0x0f);
                svga_recalctimings(svga);
            }
            break;
        case 0x3da:
            svga->fcr = val;
            break;

        default:
            break;
    }
}

uint8_t
svga_in(uint16_t addr, void *priv)
{
    svga_t    *svga = (svga_t *) priv;
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    xga_t     *xga = (xga_t *) svga->xga;
    uint8_t    index;
    uint8_t    ret = 0xff;

    if ((addr >= 0x2ea) && (addr <= 0x2ed)) {
        if (!dev)
            return ret;
    }

    switch (addr) {
        case 0x2ea:
            ret = dev->dac_mask;
            break;
        case 0x2eb:
            ret = dev->dac_status;
            break;
        case 0x2ec:
            ret = dev->dac_addr;
            break;
        case 0x2ed:
            index = (dev->dac_addr - 1) & 0xff;
            switch (dev->dac_pos) {
                case 0:
                    dev->dac_pos++;
                    if (svga->ramdac_type == RAMDAC_8BIT)
                        ret = dev->_8514pal[index].r;
                    else
                        ret = dev->_8514pal[index].r & 0x3f;
                    break;
                case 1:
                    dev->dac_pos++;
                    if (svga->ramdac_type == RAMDAC_8BIT)
                        ret = dev->_8514pal[index].g;
                    else
                        ret = dev->_8514pal[index].g & 0x3f;
                    break;
                case 2:
                    dev->dac_pos  = 0;
                    dev->dac_addr = (dev->dac_addr + 1) & 0xff;
                    if (svga->ramdac_type == RAMDAC_8BIT)
                        ret = dev->_8514pal[index].b;
                    else
                        ret = dev->_8514pal[index].b & 0x3f;
                    break;

                default:
                    break;
            }
            break;

        case 0x3c0:
            ret = svga->attraddr | svga->attr_palette_enable;
            break;
        case 0x3c1:
            ret = svga->attrregs[svga->attraddr];
            break;
        case 0x3c2:
            if ((svga->vgapal[0].r + svga->vgapal[0].g + svga->vgapal[0].b) >= 0x4e)
                ret = 0;
            else
                ret = 0x10;
            break;
        case 0x3c3:
            ret = 0x01;
            if (xga_active && xga) {
                if (xga->on)
                    ret = 0x00;
            }
            if (ibm8514_active && dev) {
                if (dev->on)
                    ret = 0x00;
            }
            svga_log("VGA read: (0x%04x) ret=%02x.\n", addr, ret);
            break;
        case 0x3c4:
            ret = svga->seqaddr;
            break;
        case 0x3c5:
            ret = svga->seqregs[svga->seqaddr & 0x0f];
            break;
        case 0x3c6:
            ret = svga->dac_mask;
            break;
        case 0x3c7:
            ret = svga->dac_status;
            break;
        case 0x3c8:
            ret = svga->dac_addr;
            break;
        case 0x3c9:
            index = (svga->dac_addr - 1) & 0xff;
            switch (svga->dac_pos) {
                case 0:
                    svga->dac_pos++;
                    if (svga->ramdac_type == RAMDAC_8BIT)
                        ret = svga->vgapal[index].r;
                    else
                        ret = svga->vgapal[index].r & 0x3f;
                    break;
                case 1:
                    svga->dac_pos++;
                    if (svga->ramdac_type == RAMDAC_8BIT)
                        ret = svga->vgapal[index].g;
                    else
                        ret = svga->vgapal[index].g & 0x3f;
                    break;
                case 2:
                    svga->dac_pos  = 0;
                    svga->dac_addr = (svga->dac_addr + 1) & 0xff;
                    if (svga->ramdac_type == RAMDAC_8BIT)
                        ret = svga->vgapal[index].b;
                    else
                        ret = svga->vgapal[index].b & 0x3f;
                    break;

                default:
                    break;
            }
            if (svga->adv_flags & FLAG_RAMDAC_SHIFT)
                ret >>= 2;
            break;
        case 0x3ca:
            ret = svga->fcr;
            break;
        case 0x3cc:
            ret = svga->miscout;
            break;
        case 0x3ce:
            ret = svga->gdcaddr;
            break;
        case 0x3cf:
            /* The spec says GDC addresses 0xF8 to 0xFB return the latch. */
            switch (svga->gdcaddr) {
                case 0xf8:
                    ret = svga->latch.b[0];
                    break;
                case 0xf9:
                    ret = svga->latch.b[1];
                    break;
                case 0xfa:
                    ret = svga->latch.b[2];
                    break;
                case 0xfb:
                    ret = svga->latch.b[3];
                    break;
                default:
                    ret = svga->gdcreg[svga->gdcaddr & 0xf];
                    break;
            }
            break;
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

        default:
            break;
    }

    if ((addr >= 0x3c6) && (addr <= 0x3c9))
        svga_log("VGA IN addr=%03x, temp=%02x.\n", addr, ret);
    else if ((addr >= 0x2ea) && (addr <= 0x2ed))
        svga_log("8514/A IN addr=%03x, temp=%02x.\n", addr, ret);

    return ret;
}

void
svga_set_ramdac_type(svga_t *svga, int type)
{
    ibm8514_t *dev = (ibm8514_t *) svga->dev8514;
    xga_t *xga = (xga_t *) svga->xga;

    if (svga->ramdac_type != type) {
        svga->ramdac_type = type;

        for (int c = 0; c < 256; c++) {
            if (ibm8514_active && dev) {
                if (svga->ramdac_type == RAMDAC_8BIT)
                    dev->pallook[c] = makecol32(dev->_8514pal[c].r, dev->_8514pal[c].g, dev->_8514pal[c].b);
                else
                    dev->pallook[c] = makecol32((dev->_8514pal[c].r & 0x3f) * 4,
                                                 (dev->_8514pal[c].g & 0x3f) * 4,
                                                 (dev->_8514pal[c].b & 0x3f) * 4);
            }
            if (xga_active && xga) {
                if (svga->ramdac_type == RAMDAC_8BIT)
                    xga->pallook[c] = makecol32(xga->xgapal[c].r, xga->xgapal[c].g, xga->xgapal[c].b);
                else {
                    xga->pallook[c] = makecol32((xga->xgapal[c].r & 0x3f) * 4,
                                                 (xga->xgapal[c].g & 0x3f) * 4,
                                                 (xga->xgapal[c].b & 0x3f) * 4);
                }
            }
            if (svga->ramdac_type == RAMDAC_8BIT)
                svga->pallook[c] = makecol32(svga->vgapal[c].r, svga->vgapal[c].g, svga->vgapal[c].b);
            else
                svga->pallook[c] = makecol32((svga->vgapal[c].r & 0x3f) * 4,
                                             (svga->vgapal[c].g & 0x3f) * 4,
                                             (svga->vgapal[c].b & 0x3f) * 4);
        }
    }
}

void
svga_recalctimings(svga_t *svga)
{
    ibm8514_t       *dev = (ibm8514_t *) svga->dev8514;
    xga_t           *xga = (xga_t *) svga->xga;
    uint8_t          set_timer = 0;
    double           crtcconst;
    double           _dispontime;
    double           _dispofftime;
    double           disptime;
    double           crtcconst8514 = 0.0;
    double           _dispontime8514 = 0.0;
    double           _dispofftime8514 = 0.0;
    double           disptime8514 = 0.0;
    double           crtcconst_xga = 0.0;
    double           _dispontime_xga = 0.0;
    double           _dispofftime_xga = 0.0;
    double           disptime_xga = 0.0;
#ifdef ENABLE_SVGA_LOG
    int              vsyncend;
    int              vblankend;
    int              hdispstart;
    int              hdispend;
    int              hsyncstart;
    int              hsyncend;
#endif
    int              old_monitor_overscan_x = svga->monitor->mon_overscan_x;
    int              old_monitor_overscan_y = svga->monitor->mon_overscan_y;

    svga->vtotal      = svga->crtc[6];
    svga->dispend     = svga->crtc[0x12];
    svga->vsyncstart  = svga->crtc[0x10];
    svga->split       = svga->crtc[0x18];
    svga->vblankstart = svga->crtc[0x15];

    if (svga->crtc[7] & 1)
        svga->vtotal |= 0x100;
    if (svga->crtc[7] & 32)
        svga->vtotal |= 0x200;
    svga->vtotal += 2;

    if (svga->crtc[7] & 2)
        svga->dispend |= 0x100;
    if (svga->crtc[7] & 64)
        svga->dispend |= 0x200;
    svga->dispend++;

    if (svga->crtc[7] & 4)
        svga->vsyncstart |= 0x100;
    if (svga->crtc[7] & 128)
        svga->vsyncstart |= 0x200;
    svga->vsyncstart++;

    if (svga->crtc[7] & 0x10)
        svga->split |= 0x100;
    if (svga->crtc[9] & 0x40)
        svga->split |= 0x200;
    svga->split++;

    if (svga->crtc[7] & 0x08)
        svga->vblankstart |= 0x100;
    if (svga->crtc[9] & 0x20)
        svga->vblankstart |= 0x200;
    svga->vblankstart++;

    svga->hdisp = svga->crtc[1];
    svga->hdisp++;

    svga->htotal = svga->crtc[0];
    /* +5 has been verified by Sergi to be correct - +6 must have been an off by one error. */
    svga->htotal += 5; /*+5 is required for Tyrian*/

    svga->rowoffset = svga->crtc[0x13];

    svga->clock = (svga->vidclock) ? VGACONST2 : VGACONST1;

    svga->lowres = !!(svga->attrregs[0x10] & 0x40);

    svga->interlace = 0;

    svga->ma_latch = ((svga->crtc[0xc] << 8) | svga->crtc[0xd]) + ((svga->crtc[8] & 0x60) >> 5);
    svga->ca_adj   = 0;

    svga->rowcount = svga->crtc[9] & 0x1f;

    svga->hdisp_time = svga->hdisp;
    svga->render     = svga_render_blank;
    if (!svga->scrblank && (svga->crtc[0x17] & 0x80) && svga->attr_palette_enable) {
        /* TODO: In case of bug reports, disable 9-dots-wide character clocks in graphics modes. */
        if (!(svga->gdcreg[6] & 1) && !(svga->attrregs[0x10] & 1)) {
            if (svga->seqregs[1] & 8)
                svga->hdisp *= (svga->seqregs[1] & 1) ? 16 : 18;
            else
                svga->hdisp *= (svga->seqregs[1] & 1) ? 8 : 9;
        } else {
            if (svga->seqregs[1] & 8)
                svga->hdisp *= 16;
            else
                svga->hdisp *= 8;
        }

        if (!(svga->gdcreg[6] & 1) && !(svga->attrregs[0x10] & 1)) { /*Text mode*/
            if (svga->seqregs[1] & 8) {                             /*40 column*/
                svga->render = svga_render_text_40;
            } else
                svga->render = svga_render_text_80;

            if (xga_active && (svga->xga != NULL)) {
                if (xga->on) {
                    if ((svga->mapping.base == 0xb8000) && (xga->aperture_cntl == 1)) /*Some operating systems reset themselves with ctrl-alt-del by going into text mode.*/
                        xga->on = 0;
                }
            }
            svga->hdisp_old = svga->hdisp;
        } else {
            svga->hdisp_old = svga->hdisp;

            if ((svga->bpp <= 8) || ((svga->gdcreg[5] & 0x60) <= 0x20)) {
                if ((svga->gdcreg[5] & 0x60) == 0x00) {
                    if (svga->seqregs[1] & 8) /*Low res (320)*/
                        svga->render = svga_render_4bpp_lowres;
                    else
                        svga->render = svga_render_4bpp_highres;
                } else if ((svga->gdcreg[5] & 0x60) == 0x20) {
                    if (svga->seqregs[1] & 8) { /*Low res (320)*/
                        svga->render = svga_render_2bpp_lowres;
                        svga_log("2 bpp low res\n");
                    } else
                        svga->render = svga_render_2bpp_highres;
                } else {
                    svga->map8 = svga->pallook;
                    if (svga->lowres) /*Low res (320)*/
                        svga->render = svga_render_8bpp_lowres;
                    else
                        svga->render = svga_render_8bpp_highres;
                }
            } else {
                switch (svga->gdcreg[5] & 0x60) {
                    case 0x40:
                    case 0x60: /*256+ colours*/
                        switch (svga->bpp) {
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
                            case 17:
                                if (svga->lowres)
                                    svga->render = svga_render_15bpp_mix_lowres;
                                else
                                    svga->render = svga_render_15bpp_mix_highres;
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

                            default:
                                break;
                        }
                        break;

                    default:
                        break;
                }
            }
        }
    }

    svga->linedbl    = svga->crtc[9] & 0x80;
    svga->char_width = (svga->seqregs[1] & 1) ? 8 : 9;

    svga->monitor->mon_overscan_y = (svga->rowcount + 1) << 1;

    if (svga->monitor->mon_overscan_y < 16)
        svga->monitor->mon_overscan_y = 16;

    if (!(svga->gdcreg[6] & 1) && !(svga->attrregs[0x10] & 1)) {
        svga->monitor->mon_overscan_x = (svga->seqregs[1] & 1) ? 16 : 18;

        if (svga->seqregs[1] & 8)
            svga->monitor->mon_overscan_x <<= 1;
    } else
        svga->monitor->mon_overscan_x = 16;

    svga->hblankstart    = svga->crtc[2];
    svga->hblank_end_val = (svga->crtc[3] & 0x1f) | ((svga->crtc[5] & 0x80) ? 0x20 : 0x00);
    svga->hblank_end_mask = 0x0000003f;

    svga_log("htotal = %i, hblankstart = %i, hblank_end_val = %02X\n",
             svga->htotal, svga->hblankstart, svga->hblank_end_val);

    if (!svga->scrblank && svga->attr_palette_enable) {
        /* TODO: In case of bug reports, disable 9-dots-wide character clocks in graphics modes. */
        if (!(svga->gdcreg[6] & 1) && !(svga->attrregs[0x10] & 1)) {
            if (svga->seqregs[1] & 8)
                svga->dots_per_clock = ((svga->seqregs[1] & 1) ? 16 : 18);
            else
                svga->dots_per_clock = ((svga->seqregs[1] & 1) ? 8 : 9);
        } else {
            if (svga->seqregs[1] & 8)
                svga->dots_per_clock = 16;
            else
                svga->dots_per_clock = 8;
        }
    } else
        svga->dots_per_clock = 1;

    svga->multiplier = 1.0;

    if (svga->recalctimings_ex)
        svga->recalctimings_ex(svga);

    if (ibm8514_active && (svga->dev8514 != NULL)) {
        if ((dev->local & 0xff) == 0x00)
            ibm8514_recalctimings(svga);
    }

    if (xga_active && (svga->xga != NULL))
        xga_recalctimings(svga);

    if (!svga->hoverride) {
        uint32_t dot = svga->hblankstart;
        uint32_t adj_dot = svga->hblankstart;
        /* Verified with both the Voodoo 3 and the S3 cards: compare 7 bits if bit 7 is set,
           otherwise compare 6 bits. */
        uint32_t eff_mask = (svga->hblank_end_val & ~0x0000003f) ? svga->hblank_end_mask : 0x0000003f;
        svga->hblank_sub = 0;

        svga_log("HDISP=%d, CRTC1+1=%d, Blank: %04i-%04i, Total: %04i, Mask: %02X, ADJ_DOT=%04i.\n", svga->hdisp, svga->crtc[1] + 1, svga->hblankstart, svga->hblank_end_val,
                 svga->htotal, eff_mask, adj_dot);

        while (adj_dot < (svga->htotal << 1)) {
            if (dot == svga->htotal)
                dot = 0;

            if (adj_dot >= svga->htotal)
                svga->hblank_sub++;

            svga_log("Loop: adjdot=%d, htotal=%d, dotmask=%02x, hblankendvalmask=%02x, blankendval=%02x.\n", adj_dot, svga->htotal, dot & eff_mask, svga->hblank_end_val & eff_mask, svga->hblank_end_val);
            if ((dot & eff_mask) == (svga->hblank_end_val & eff_mask))
                break;

            dot++;
            adj_dot++;
        }

        svga->hdisp -= (svga->hblank_sub * svga->dots_per_clock);
    }

#ifdef TBD
    if (ibm8514_active && (svga->dev8514 != NULL)) {
        if (dev->on) {
            uint32_t dot8514 = dev->h_blankstart;
            uint32_t adj_dot8514 = dev->h_blankstart;
            uint32_t eff_mask8514 = 0x0000003f;
            dev->hblank_sub = 0;

            while (adj_dot8514 < (dev->h_total << 1)) {
                if (dot8514 == dev->h_total)
                    dot8514 = 0;

                if (adj_dot8514 >= dev->h_total)
                    dev->hblank_sub++;

                if ((dot8514 & eff_mask8514) == (dev->h_blank_end_val & eff_mask8514))
                    break;

                dot8514++;
                adj_dot8514++;
            }

            dev->h_disp -= dev->hblank_sub;
        }
    }
#endif

    if (svga->hdisp >= 2048)
        svga->monitor->mon_overscan_x = 0;

    svga->y_add = (svga->monitor->mon_overscan_y >> 1);
    svga->x_add = (svga->monitor->mon_overscan_x >> 1);

    if (svga->vblankstart < svga->dispend) {
        svga_log("DISPEND > VBLANKSTART.\n");
        svga->dispend = svga->vblankstart;
    }

    crtcconst = svga->clock * svga->char_width;
    if (ibm8514_active && (svga->dev8514 != NULL)) {
        if (dev->on)
            crtcconst8514 = svga->clock8514;
    }
    if (xga_active && (svga->xga != NULL)) {
        if (xga->on)
            crtcconst_xga = svga->clock_xga;
    }

#ifdef ENABLE_SVGA_LOG
    vsyncend = (svga->vsyncstart & 0xfffffff0) | (svga->crtc[0x11] & 0x0f);
    if (vsyncend <= svga->vsyncstart)
        vsyncend += 0x00000010;
    vblankend = (svga->vblankstart & 0xffffff80) | (svga->crtc[0x16] & 0x7f);
    if (vblankend <= svga->vblankstart)
        vblankend += 0x00000080;

    hdispstart = ((svga->crtc[3] >> 5) & 3);
    hdispend   = svga->crtc[1] + 1;
    hsyncstart = svga->crtc[4] + ((svga->crtc[5] >> 5) & 3) + 1;
    hsyncend   = (hsyncstart & 0xffffffe0) | (svga->crtc[5] & 0x1f);
    if (hsyncend <= hsyncstart)
        hsyncend += 0x00000020;
#endif

    svga_log("Last scanline in the vertical period: %i\n"
             "First scanline after the last of active display: %i\n"
             "First scanline with vertical retrace asserted: %i\n"
             "First scanline after the last with vertical retrace asserted: %i\n"
             "First scanline of blanking: %i\n"
             "First scanline after the last of blanking: %i\n"
             "\n"
             "Last character in the horizontal period: %i\n"
             "First character of active display: %i\n"
             "First character after the last of active display: %i\n"
             "First character with horizontal retrace asserted: %i\n"
             "First character after the last with horizontal retrace asserted: %i\n"
             "First character of blanking: %i\n"
             "First character after the last of blanking: %i\n"
             "\n"
             "\n",
             svga->vtotal, svga->dispend, svga->vsyncstart, vsyncend,
             svga->vblankstart, vblankend,
             svga->htotal, hdispstart, hdispend, hsyncstart, hsyncend,
             svga->hblankstart, svga->hblankend);

    disptime    = svga->htotal * svga->multiplier;
    _dispontime = svga->hdisp_time;

    if (ibm8514_active && (svga->dev8514 != NULL)) {
        if (dev->on) {
            disptime8514 = dev->h_total ? dev->h_total : TIMER_USEC;
            _dispontime8514 = dev->hdisped;
        }
    }

    if (xga_active && (svga->xga != NULL)) {
        if (xga->on) {
            disptime_xga = xga->h_total ? xga->h_total : TIMER_USEC;
            _dispontime_xga = xga->h_disp;
        }
    }

    if (svga->seqregs[1] & 8) {
        disptime *= 2;
        _dispontime *= 2;
    }

    _dispofftime = disptime - _dispontime;
    _dispontime *= crtcconst;
    _dispofftime *= crtcconst;

    svga->dispontime  = (uint64_t) (_dispontime);
    svga->dispofftime = (uint64_t) (_dispofftime);
    if (svga->dispontime < TIMER_USEC)
        svga->dispontime = TIMER_USEC;
    if (svga->dispofftime < TIMER_USEC)
        svga->dispofftime = TIMER_USEC;

    if (ibm8514_active && (svga->dev8514 != NULL))
        set_timer |= 1;

    if (xga_active && (svga->xga != NULL))
        set_timer |= 2;

    switch (set_timer) {
        default:
        case 0: /*VGA only*/
            svga_set_poll(svga);
            break;

        case 1: /*Plus 8514/A*/
            if (dev->on) {
                _dispofftime8514 = disptime8514 - _dispontime8514;
                _dispontime8514 *= crtcconst8514;
                _dispofftime8514 *= crtcconst8514;

                dev->dispontime  = (uint64_t) (_dispontime8514);
                dev->dispofftime = (uint64_t) (_dispofftime8514);
                if (dev->dispontime < TIMER_USEC)
                    dev->dispontime = TIMER_USEC;
                if (dev->dispofftime < TIMER_USEC)
                    dev->dispofftime = TIMER_USEC;

                ibm8514_set_poll(svga);
            } else
                svga_set_poll(svga);
            break;

        case 2: /*Plus XGA*/
            if (xga->on) {
                _dispofftime_xga = disptime_xga - _dispontime_xga;
                _dispontime_xga *= crtcconst_xga;
                _dispofftime_xga *= crtcconst_xga;

                xga->dispontime  = (uint64_t) (_dispontime_xga);
                xga->dispofftime = (uint64_t) (_dispofftime_xga);
                if (xga->dispontime < TIMER_USEC)
                    xga->dispontime = TIMER_USEC;
                if (xga->dispofftime < TIMER_USEC)
                    xga->dispofftime = TIMER_USEC;

                xga_set_poll(svga);
            } else
                svga_set_poll(svga);
            break;

        case 3: /*Plus 8514/A and XGA*/
            if (dev->on) {
                _dispofftime8514 = disptime8514 - _dispontime8514;
                _dispontime8514 *= crtcconst8514;
                _dispofftime8514 *= crtcconst8514;

                dev->dispontime  = (uint64_t) (_dispontime8514);
                dev->dispofftime = (uint64_t) (_dispofftime8514);
                if (dev->dispontime < TIMER_USEC)
                    dev->dispontime = TIMER_USEC;
                if (dev->dispofftime < TIMER_USEC)
                    dev->dispofftime = TIMER_USEC;

                ibm8514_set_poll(svga);
            } else if (xga->on) {
                _dispofftime_xga = disptime_xga - _dispontime_xga;
                _dispontime_xga *= crtcconst_xga;
                _dispofftime_xga *= crtcconst_xga;

                xga->dispontime  = (uint64_t) (_dispontime_xga);
                xga->dispofftime = (uint64_t) (_dispofftime_xga);
                if (xga->dispontime < TIMER_USEC)
                    xga->dispontime = TIMER_USEC;
                if (xga->dispofftime < TIMER_USEC)
                    xga->dispofftime = TIMER_USEC;

                xga_set_poll(svga);
            } else
                svga_set_poll(svga);
            break;
    }

    if (!svga->force_old_addr)
        svga_recalc_remap_func(svga);

    /* Inform the user interface of any DPMS mode changes. */
    if (svga->dpms) {
        if (!svga->dpms_ui) {
            /* Make sure to black out the entire screen to avoid lingering image. */
            int y_add   = enable_overscan ? svga->monitor->mon_overscan_y : 0;
            int x_add   = enable_overscan ? svga->monitor->mon_overscan_x : 0;
            int y_start = enable_overscan ? 0 : (svga->monitor->mon_overscan_y >> 1);
            int x_start = enable_overscan ? 0 : (svga->monitor->mon_overscan_x >> 1);
            video_wait_for_buffer_monitor(svga->monitor_index);
            memset(svga->monitor->target_buffer->dat, 0, svga->monitor->target_buffer->w * svga->monitor->target_buffer->h * 4);
            video_blit_memtoscreen_monitor(x_start, y_start, svga->monitor->mon_xsize + x_add, svga->monitor->mon_ysize + y_add, svga->monitor_index);
            video_wait_for_buffer_monitor(svga->monitor_index);
            svga->dpms_ui = 1;
            ui_sb_set_text_w(plat_get_string(STRING_MONITOR_SLEEP));
        }
    } else if (svga->dpms_ui) {
        svga->dpms_ui = 0;
        ui_sb_set_text_w(NULL);
    }

    if (enable_overscan && (svga->monitor->mon_overscan_x != old_monitor_overscan_x || svga->monitor->mon_overscan_y != old_monitor_overscan_y))
        video_force_resize_set_monitor(1, svga->monitor_index);
}

static void
svga_do_render(svga_t *svga)
{
    /* Always render a blank screen and nothing else while in DPMS mode. */
    if (svga->dpms) {
        svga_render_blank(svga);
        return;
    }

    if (!svga->override) {
        svga->render(svga);

        svga->x_add = (svga->monitor->mon_overscan_x >> 1);
        svga_render_overscan_left(svga);
        svga_render_overscan_right(svga);
        svga->x_add = (svga->monitor->mon_overscan_x >> 1) - svga->scrollcache;
    }

    if (svga->overlay_on) {
        if (!svga->override && svga->overlay_draw)
            svga->overlay_draw(svga, svga->displine + svga->y_add);
        svga->overlay_on--;
        if (svga->overlay_on && svga->interlace)
            svga->overlay_on--;
    }

    if (svga->dac_hwcursor_on) {
        if (!svga->override && svga->dac_hwcursor_draw)
            svga->dac_hwcursor_draw(svga, (svga->displine + svga->y_add + ((svga->dac_hwcursor_latch.y >= 0) ? 0 : svga->dac_hwcursor_latch.y)) & 2047);
        svga->dac_hwcursor_on--;
        if (svga->dac_hwcursor_on && svga->interlace)
            svga->dac_hwcursor_on--;
    }

    if (svga->hwcursor_on) {
        if (!svga->override && svga->hwcursor_draw)
            svga->hwcursor_draw(svga, (svga->displine + svga->y_add + ((svga->hwcursor_latch.y >= 0) ? 0 : svga->hwcursor_latch.y)) & 2047);

        svga->hwcursor_on--;
        if (svga->hwcursor_on && svga->interlace)
            svga->hwcursor_on--;
    }
}

void
svga_poll(void *priv)
{
    svga_t    *svga = (svga_t *) priv;
    uint32_t   x;
    uint32_t   blink_delay;
    int        wx;
    int        wy;
    int        ret;
    int        old_ma;

    svga_log("SVGA Poll.\n");
    if (!svga->linepos) {
        if (svga->displine == ((svga->hwcursor_latch.y < 0) ? 0 : svga->hwcursor_latch.y) && svga->hwcursor_latch.ena) {
            svga->hwcursor_on      = svga->hwcursor_latch.cur_ysize - svga->hwcursor_latch.yoff;
            svga->hwcursor_oddeven = 0;
        }

        if (svga->displine == (((svga->hwcursor_latch.y < 0) ? 0 : svga->hwcursor_latch.y) + 1) && svga->hwcursor_latch.ena && svga->interlace) {
            svga->hwcursor_on      = svga->hwcursor_latch.cur_ysize - (svga->hwcursor_latch.yoff + 1);
            svga->hwcursor_oddeven = 1;
        }

        if (svga->displine == ((svga->dac_hwcursor_latch.y < 0) ? 0 : svga->dac_hwcursor_latch.y) && svga->dac_hwcursor_latch.ena) {
            svga->dac_hwcursor_on      = svga->dac_hwcursor_latch.cur_ysize - svga->dac_hwcursor_latch.yoff;
            svga->dac_hwcursor_oddeven = 0;
        }

        if (svga->displine == (((svga->dac_hwcursor_latch.y < 0) ? 0 : svga->dac_hwcursor_latch.y) + 1) && svga->dac_hwcursor_latch.ena && svga->interlace) {
            svga->dac_hwcursor_on      = svga->dac_hwcursor_latch.cur_ysize - (svga->dac_hwcursor_latch.yoff + 1);
            svga->dac_hwcursor_oddeven = 1;
        }

        if (svga->displine == svga->overlay_latch.y && svga->overlay_latch.ena) {
            svga->overlay_on      = svga->overlay_latch.cur_ysize - svga->overlay_latch.yoff;
            svga->overlay_oddeven = 0;
        }

        if (svga->displine == svga->overlay_latch.y + 1 && svga->overlay_latch.ena && svga->interlace) {
            svga->overlay_on      = svga->overlay_latch.cur_ysize - svga->overlay_latch.yoff;
            svga->overlay_oddeven = 1;
        }

        timer_advance_u64(&svga->timer, svga->dispofftime);
        svga->cgastat |= 1;
        svga->linepos = 1;

        if (svga->dispon) {
            svga->hdisp_on = 1;

            svga->ma &= svga->vram_display_mask;
            if (svga->firstline == 2000) {
                svga->firstline = svga->displine;
                video_wait_for_buffer_monitor(svga->monitor_index);
            }

            if (svga->hwcursor_on || svga->dac_hwcursor_on || svga->overlay_on)
                svga->changedvram[svga->ma >> 12] = svga->changedvram[(svga->ma >> 12) + 1] = svga->interlace ? 3 : 2;

            if (svga->vertical_linedbl) {
                old_ma = svga->ma;

                svga->displine <<= 1;
                svga->y_add <<= 1;

                svga_do_render(svga);

                svga->displine++;

                svga->ma = old_ma;

                svga_do_render(svga);

                svga->y_add >>= 1;
                svga->displine >>= 1;
            } else
                svga_do_render(svga);

            if (svga->lastline < svga->displine)
                svga->lastline = svga->displine;
        }

        svga->displine++;
        if (svga->interlace)
            svga->displine++;
        if ((svga->cgastat & 8) && ((svga->displine & 15) == (svga->crtc[0x11] & 15)) && svga->vslines)
            svga->cgastat &= ~8;
        svga->vslines++;
        if (svga->displine > 2000)
            svga->displine = 0;
    } else {
        timer_advance_u64(&svga->timer, svga->dispontime);

        if (svga->dispon)
            svga->cgastat &= ~1;
        svga->hdisp_on = 0;

        svga->linepos = 0;
        if ((svga->sc == (svga->crtc[11] & 31)) || (svga->sc == svga->rowcount))
            svga->con = 0;
        if (svga->dispon) {
            /* TODO: Verify real hardware behaviour for out-of-range fine vertical scroll
               - S3 Trio64V2/DX: sc == rowcount, wrapping 5-bit counter. */
            if (svga->linedbl && !svga->linecountff) {
                svga->linecountff = 1;
                svga->ma          = svga->maback;
            } else if (svga->sc == svga->rowcount) {
                svga->linecountff = 0;
                svga->sc          = 0;

                svga->maback += (svga->adv_flags & FLAG_NO_SHIFT3) ? svga->rowoffset : (svga->rowoffset << 3);
                if (svga->interlace)
                    svga->maback += (svga->adv_flags & FLAG_NO_SHIFT3) ? svga->rowoffset : (svga->rowoffset << 3);

                svga->maback &= svga->vram_display_mask;
                svga->ma = svga->maback;
            } else {
                svga->linecountff = 0;
                svga->sc++;
                svga->sc &= 0x1f;
                svga->ma = svga->maback;
            }
        }

        svga->hsync_divisor ^= 1;

        if (svga->hsync_divisor && (svga->crtc[0x17] & 4))
            return;

        svga->vc++;
        svga->vc &= 0x7ff;

        if (svga->vc == svga->split) {
            ret = 1;

            if (svga->line_compare)
                ret = svga->line_compare(svga);

            if (ret) {
                if (svga->interlace && svga->oddeven)
                    svga->ma = svga->maback = (svga->rowoffset << 1) + svga->hblank_sub;
                else
                    svga->ma = svga->maback = svga->hblank_sub;

                svga->ma     = (svga->ma << 2);
                svga->maback = (svga->maback << 2);

                svga->sc = 0;
                if (svga->attrregs[0x10] & 0x20) {
                    svga->scrollcache = 0;
                    svga->x_add       = (svga->monitor->mon_overscan_x >> 1);
                }
            }
        }
        if (svga->vc == svga->dispend) {
            if (svga->vblank_start)
                svga->vblank_start(svga);

            svga->dispon = 0;
            blink_delay  = (svga->crtc[11] & 0x60) >> 5;
            if (svga->crtc[10] & 0x20)
                svga->cursoron = 0;
            else if (blink_delay == 2)
                svga->cursoron = ((svga->blink % 96) >= 48);
            else
                svga->cursoron = svga->blink & (16 + (16 * blink_delay));

            if (!(svga->blink & 15))
                svga->fullchange = 2;

            svga->blink = (svga->blink + 1) & 0x7f;

            for (x = 0; x < ((svga->vram_mask + 1) >> 12); x++) {
                if (svga->changedvram[x])
                    svga->changedvram[x]--;
            }

            if (svga->fullchange)
                svga->fullchange--;
        }
        if (svga->vc == svga->vsyncstart) {
            svga->dispon = 0;
            svga->cgastat |= 8;
            x = svga->hdisp;

            if (svga->interlace && !svga->oddeven)
                svga->lastline++;
            if (svga->interlace && svga->oddeven)
                svga->firstline--;

            wx = x;

            if (!svga->override) {
                if (svga->vertical_linedbl) {
                    wy = (svga->lastline - svga->firstline) << 1;
                    svga->vdisp = wy + 1;
                    svga_doblit(wx, wy, svga);
                } else {
                    wy = svga->lastline - svga->firstline;
                    svga->vdisp = wy + 1;
                    svga_doblit(wx, wy, svga);
                }
            }

            svga->firstline = 2000;
            svga->lastline  = 0;

            svga->firstline_draw = 2000;
            svga->lastline_draw  = 0;

            svga->oddeven ^= 1;

            svga->monitor->mon_changeframecount = svga->interlace ? 3 : 2;
            svga->vslines                       = 0;

            if (svga->interlace && svga->oddeven)
                svga->ma = svga->maback = svga->ma_latch + (svga->rowoffset << 1) + svga->hblank_sub;
            else
                svga->ma = svga->maback = svga->ma_latch + svga->hblank_sub;

            svga->ca     = ((svga->crtc[0xe] << 8) | svga->crtc[0xf]) + ((svga->crtc[0xb] & 0x60) >> 5) + svga->ca_adj;
            if (!(svga->adv_flags & FLAG_NO_SHIFT3)) {
                svga->ma     = (svga->ma << 2);
                svga->maback = (svga->maback << 2);
            }
            svga->ca     = (svga->ca << 2);

            if (svga->vsync_callback)
                svga->vsync_callback(svga);
        }
#if 0
        if (svga->vc == lines_num) {
#endif
        if (svga->vc == svga->vtotal) {
            svga->vc       = 0;
            svga->sc       = (svga->crtc[0x8] & 0x1f);
            svga->dispon   = 1;
            svga->displine = (svga->interlace && svga->oddeven) ? 1 : 0;

            svga->scrollcache = (svga->attrregs[0x13] & 0x0f);
            if (!(svga->gdcreg[6] & 1) && !(svga->attrregs[0x10] & 1)) { /*Text mode*/
                if (svga->seqregs[1] & 1)
                    svga->scrollcache &= 0x07;
                else {
                    svga->scrollcache++;
                    if (svga->scrollcache > 8)
                        svga->scrollcache = 0;
                }
            } else if ((svga->render == svga_render_2bpp_lowres) || (svga->render == svga_render_2bpp_highres) || (svga->render == svga_render_4bpp_lowres) || (svga->render == svga_render_4bpp_highres))
                svga->scrollcache &= 0x07;
            else
                svga->scrollcache = (svga->scrollcache & 0x06) >> 1;

            if ((svga->seqregs[1] & 8) || (svga->render == svga_render_8bpp_lowres))
                svga->scrollcache <<= 1;

            svga->x_add = (svga->monitor->mon_overscan_x >> 1) - svga->scrollcache;

            svga->linecountff = 0;

            svga->hwcursor_on    = 0;
            svga->hwcursor_latch = svga->hwcursor;

            svga->dac_hwcursor_on    = 0;
            svga->dac_hwcursor_latch = svga->dac_hwcursor;

            svga->overlay_on    = 0;
            svga->overlay_latch = svga->overlay;
        }
        if (svga->sc == (svga->crtc[10] & 31))
            svga->con = 1;
    }
}

uint32_t
svga_conv_16to32(UNUSED(struct svga_t *svga), uint16_t color, uint8_t bpp)
{
    return (bpp == 15) ? video_15to32[color] : video_16to32[color];
}

int
svga_init(const device_t *info, svga_t *svga, void *priv, int memsize,
          void (*recalctimings_ex)(struct svga_t *svga),
          uint8_t (*video_in)(uint16_t addr, void *priv),
          void (*video_out)(uint16_t addr, uint8_t val, void *priv),
          void (*hwcursor_draw)(struct svga_t *svga, int displine),
          void (*overlay_draw)(struct svga_t *svga, int displine))
{
    int e;

    svga->priv          = priv;
    svga->monitor_index = monitor_index_global;
    svga->monitor       = &monitors[svga->monitor_index];

    for (int c = 0; c < 256; c++) {
        e = c;
        for (int d = 0; d < 8; d++) {
            svga_rotate[d][c] = e;
            e                 = (e >> 1) | ((e & 1) ? 0x80 : 0);
        }
    }
    svga->readmode = 0;

    svga->attrregs[0x11] = 0;
    svga->overscan_color = 0x000000;

    svga->monitor->mon_overscan_x = 16;
    svga->monitor->mon_overscan_y = 32;
    svga->x_add                   = 8;
    svga->y_add                   = 16;

    svga->crtc[0]           = 63;
    svga->crtc[6]           = 255;
    svga->dispontime        = 1000ULL << 32;
    svga->dispofftime       = 1000ULL << 32;
    svga->bpp               = 8;
    svga->vram              = calloc(memsize + 8, 1);
    svga->vram_max          = memsize;
    svga->vram_display_mask = svga->vram_mask = memsize - 1;
    svga->decode_mask                         = 0x7fffff;
    svga->changedvram                         = calloc((memsize >> 12) + 1, 1);
    svga->recalctimings_ex                    = recalctimings_ex;
    svga->video_in                            = video_in;
    svga->video_out                           = video_out;
    svga->hwcursor_draw                       = hwcursor_draw;
    svga->overlay_draw                        = overlay_draw;
    svga->conv_16to32                         = svga_conv_16to32;
    svga->render                              = svga_render_blank;

    svga->hwcursor.cur_xsize = svga->hwcursor.cur_ysize = 32;

    svga->dac_hwcursor.cur_xsize = svga->dac_hwcursor.cur_ysize = 32;

    svga->translate_address         = NULL;
    svga->ksc5601_english_font_type = 0;

    if ((info->flags & DEVICE_PCI) || (info->flags & DEVICE_VLB) || (info->flags & DEVICE_MCA)) {
        svga->read = svga_read;
        svga->readw = svga_readw;
        svga->readl = svga_readl;
        svga->write = svga_write;
        svga->writew = svga_writew;
        svga->writel = svga_writel;
        mem_mapping_add(&svga->mapping, 0xa0000, 0x20000,
                        svga_read, svga_readw, svga_readl,
                        svga_write, svga_writew, svga_writel,
                        NULL, MEM_MAPPING_EXTERNAL, svga);
    } else if ((info->flags & DEVICE_ISA) && (info->flags & DEVICE_AT)) {
        svga->read = svga_read;
        svga->readw = svga_readw;
        svga->readl = NULL;
        svga->write = svga_write;
        svga->writew = svga_writew;
        svga->writel = NULL;
        mem_mapping_add(&svga->mapping, 0xa0000, 0x20000,
                        svga_read, svga_readw, NULL,
                        svga_write, svga_writew, NULL,
                        NULL, MEM_MAPPING_EXTERNAL, svga);
    } else {
        svga->read = svga_read;
        svga->readw = NULL;
        svga->readl = NULL;
        svga->write = svga_write;
        svga->writew = NULL;
        svga->writel = NULL;
        mem_mapping_add(&svga->mapping, 0xa0000, 0x20000,
                        svga_read, NULL, NULL,
                        svga_write, NULL, NULL,
                        NULL, MEM_MAPPING_EXTERNAL, svga);
    }

    timer_add(&svga->timer, svga_poll, svga, 1);

    svga_pri = svga;

    svga->ramdac_type = RAMDAC_6BIT;

    svga->map8            = svga->pallook;

    return 0;
}

void
svga_close(svga_t *svga)
{
    free(svga->changedvram);
    free(svga->vram);

    if (svga->dpms_ui)
        ui_sb_set_text_w(NULL);

    svga_pri = NULL;
}

uint32_t
svga_decode_addr(svga_t *svga, uint32_t addr, int write)
{
    int memory_map_mode = (svga->gdcreg[6] >> 2) & 3;

    addr &= 0x1ffff;

    switch (memory_map_mode) {
        case 0:
            break;
        case 1:
            if (addr >= 0x10000)
                return 0xffffffff;
            break;
        case 2:
            addr -= 0x10000;
            if (addr >= 0x8000)
                return 0xffffffff;
            break;
        default:
        case 3:
            addr -= 0x18000;
            if (addr >= 0x8000)
                return 0xffffffff;
            break;
    }

    if (memory_map_mode <= 1) {
        if (write)
            addr += svga->write_bank;
        else
            addr += svga->read_bank;
    }

    return addr;
}

static __inline void
svga_write_common(uint32_t addr, uint8_t val, uint8_t linear, void *priv)
{
    svga_t *svga       = (svga_t *) priv;
    int     writemask2 = svga->writemask;
    int     reset_wm   = 0;
    latch_t vall;
    uint8_t wm         = svga->writemask;
    uint8_t count;
    uint8_t i;

    if (svga->adv_flags & FLAG_ADDR_BY8)
        writemask2 = svga->seqregs[2];

    cycles -= svga->monitor->mon_video_timing_write_b;

    if (!linear) {
        xga_write_test(addr, val, svga);
        addr = svga_decode_addr(svga, addr, 1);
        if (addr == 0xffffffff) {
            svga_log("WriteCommon Over.\n");
            return;
        }
    }

    if (!(svga->gdcreg[6] & 1))
        svga->fullchange = 2;

    if ((svga->adv_flags & FLAG_ADDR_BY16) && (svga->writemode == 4 || svga->writemode == 5))
        addr <<= 4;
    else if ((svga->adv_flags & FLAG_ADDR_BY8) && (svga->writemode < 4))
        addr <<= 3;
    else if (((svga->chain4 && (svga->packed_chain4 || svga->force_old_addr)) || svga->fb_only) && (svga->writemode < 4)) {
        writemask2 = 1 << (addr & 3);
        addr &= ~3;
    } else if (svga->chain4 && (svga->writemode < 4)) {
        writemask2 = 1 << (addr & 3);
        if (!linear)
            addr &= ~3;
        addr = ((addr & 0xfffc) << 2) | ((addr & 0x30000) >> 14) | (addr & ~0x3ffff);
    } else if (svga->chain2_write) {
        writemask2 &= ~0xa;
        if (addr & 1)
            writemask2 <<= 1;
        addr &= ~1;
        addr <<= 2;
    } else
        addr <<= 2;

    addr &= svga->decode_mask;

    if (svga->translate_address)
        addr = svga->translate_address(addr, priv);

    if (addr >= svga->vram_max) {
        svga_log("WriteBankedOver=%08x, val=%02x.\n", addr & svga->vram_mask, val);
        return;
    }

    addr &= svga->vram_mask;

    svga->changedvram[addr >> 12] = svga->monitor->mon_changeframecount;

    count = 4;
    if (svga->adv_flags & FLAG_LATCH8)
        count = 8;

    /* Undocumented Cirrus Logic behavior: The datasheet says that, with EXT_WRITE and FLAG_ADDR_BY8, the write mask only
       changes meaning in write modes 4 and 5, as well as write mode 1. In reality, however, all other write modes are also
       affected, as proven by the Windows 3.1 CL-GD 5422/4 drivers in 8bpp modes. */
    switch (svga->writemode) {
        case 0:
            val = ((val >> (svga->gdcreg[3] & 7)) | (val << (8 - (svga->gdcreg[3] & 7))));
            if ((svga->gdcreg[8] == 0xff) && !(svga->gdcreg[3] & 0x18) && (!svga->gdcreg[1] || svga->set_reset_disabled)) {
                for (i = 0; i < count; i++) {
                    if ((svga->adv_flags & FLAG_EXT_WRITE) && (svga->adv_flags & FLAG_ADDR_BY8)) {
                        if (writemask2 & (0x80 >> i))
                            svga->vram[addr | i] = val;
                    } else {
                        if (writemask2 & (1 << i))
                            svga->vram[addr | i] = val;
                    }
                }
                return;
            } else {
                for (i = 0; i < count; i++) {
                    if (svga->gdcreg[1] & (1 << i))
                        vall.b[i] = !!(svga->gdcreg[0] & (1 << i)) * 0xff;
                    else
                        vall.b[i] = val;
                }
            }
            break;
        case 1:
            for (i = 0; i < count; i++) {
                if ((svga->adv_flags & FLAG_EXT_WRITE) && (svga->adv_flags & FLAG_ADDR_BY8)) {
                    if (writemask2 & (0x80 >> i))
                        svga->vram[addr | i] = svga->latch.b[i];
                } else {
                    if (writemask2 & (1 << i))
                        svga->vram[addr | i] = svga->latch.b[i];
                }
            }
            return;
        case 2:
            for (i = 0; i < count; i++)
                vall.b[i] = !!(val & (1 << i)) * 0xff;

            if (!(svga->gdcreg[3] & 0x18) && (!svga->gdcreg[1] || svga->set_reset_disabled)) {
                for (i = 0; i < count; i++) {
                    if ((svga->adv_flags & FLAG_EXT_WRITE) && (svga->adv_flags & FLAG_ADDR_BY8)) {
                        if (writemask2 & (0x80 >> i))
                            svga->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | (svga->latch.b[i] & ~svga->gdcreg[8]);
                    } else {
                        if (writemask2 & (1 << i))
                            svga->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | (svga->latch.b[i] & ~svga->gdcreg[8]);
                    }
                }
                return;
            }
            break;
        case 3:
            val = ((val >> (svga->gdcreg[3] & 7)) | (val << (8 - (svga->gdcreg[3] & 7))));
            wm  = svga->gdcreg[8];
            svga->gdcreg[8] &= val;

            for (i = 0; i < count; i++)
                vall.b[i] = !!(svga->gdcreg[0] & (1 << i)) * 0xff;

            reset_wm = 1;
            break;
        default:
            if (svga->ven_write)
                svga->ven_write(svga, val, addr);
            return;
    }

    switch (svga->gdcreg[3] & 0x18) {
        case 0x00: /* Set */
            for (i = 0; i < count; i++) {
                if ((svga->adv_flags & FLAG_EXT_WRITE) && (svga->adv_flags & FLAG_ADDR_BY8)) {
                    if (writemask2 & (0x80 >> i))
                        svga->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | (svga->latch.b[i] & ~svga->gdcreg[8]);
                } else {
                    if (writemask2 & (1 << i))
                        svga->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | (svga->latch.b[i] & ~svga->gdcreg[8]);
                }
            }
            break;
        case 0x08: /* AND */
            for (i = 0; i < count; i++) {
                if ((svga->adv_flags & FLAG_EXT_WRITE) && (svga->adv_flags & FLAG_ADDR_BY8)) {
                    if (writemask2 & (0x80 >> i))
                        svga->vram[addr | i] = (vall.b[i] | ~svga->gdcreg[8]) & svga->latch.b[i];
                } else {
                    if (writemask2 & (1 << i))
                        svga->vram[addr | i] = (vall.b[i] | ~svga->gdcreg[8]) & svga->latch.b[i];
                }
            }
            break;
        case 0x10: /* OR */
            for (i = 0; i < count; i++) {
                if ((svga->adv_flags & FLAG_EXT_WRITE) && (svga->adv_flags & FLAG_ADDR_BY8)) {
                    if (writemask2 & (0x80 >> i))
                        svga->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | svga->latch.b[i];
                } else {
                    if (writemask2 & (1 << i))
                        svga->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) | svga->latch.b[i];
                }
            }
            break;
        case 0x18: /* XOR */
            for (i = 0; i < count; i++) {
                if ((svga->adv_flags & FLAG_EXT_WRITE) && (svga->adv_flags & FLAG_ADDR_BY8)) {
                    if (writemask2 & (0x80 >> i))
                        svga->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) ^ svga->latch.b[i];
                } else {
                    if (writemask2 & (1 << i))
                        svga->vram[addr | i] = (vall.b[i] & svga->gdcreg[8]) ^ svga->latch.b[i];
                }
            }
            break;

        default:
            break;
    }

    if (reset_wm)
        svga->gdcreg[8] = wm;
}

static __inline uint8_t
svga_read_common(uint32_t addr, uint8_t linear, void *priv)
{
    svga_t  *svga       = (svga_t *) priv;
    uint32_t latch_addr = 0;
    int      readplane  = svga->readplane;
    uint8_t  count;
    uint8_t  temp;
    uint8_t  ret = 0x00;

    if (svga->adv_flags & FLAG_ADDR_BY8)
        readplane = svga->gdcreg[4] & 7;

    cycles -= svga->monitor->mon_video_timing_read_b;

    if (!linear) {
        (void) xga_read_test(addr, svga);
        addr = svga_decode_addr(svga, addr, 0);
        if (addr == 0xffffffff)
            return 0xff;
    }

    count = 2;
    if (svga->adv_flags & FLAG_LATCH8)
        count = 3;

    latch_addr = (addr << count) & svga->decode_mask;
    count      = (1 << count);

    if (svga->adv_flags & FLAG_ADDR_BY16)
        addr <<= 4;
    else if (svga->adv_flags & FLAG_ADDR_BY8)
        addr <<= 3;
    else if ((svga->chain4 && (svga->packed_chain4 || svga->force_old_addr)) || svga->fb_only) {
        addr &= svga->decode_mask;
        if (svga->translate_address)
            addr = svga->translate_address(addr, priv);
        if (addr >= svga->vram_max)
            return 0xff;
        latch_addr = (addr & svga->vram_mask) & ~3;
        for (uint8_t i = 0; i < count; i++)
            svga->latch.b[i] = svga->vram[latch_addr | i];
        return svga->vram[addr & svga->vram_mask];
    } else if (svga->chain4 && !svga->force_old_addr) {
        readplane = addr & 3;
        addr      = ((addr & 0xfffc) << 2) | ((addr & 0x30000) >> 14) | (addr & ~0x3ffff);
    } else if (svga->chain2_read) {
        readplane = (readplane & 2) | (addr & 1);
        addr &= ~1;
        addr <<= 2;
    } else
        addr <<= 2;

    addr &= svga->decode_mask;

    if (svga->translate_address) {
        latch_addr = svga->translate_address(latch_addr, priv);
        addr       = svga->translate_address(addr, priv);
    }

    /* standard VGA latched access */
    if (latch_addr >= svga->vram_max) {
        for (uint8_t i = 0; i < count; i++)
            svga->latch.b[i] = 0xff;
    } else {
        latch_addr &= svga->vram_mask;

        for (uint8_t i = 0; i < count; i++)
            svga->latch.b[i] = svga->vram[latch_addr | i];
    }

    if (addr >= svga->vram_max)
        return 0xff;

    addr &= svga->vram_mask;

    if (svga->readmode) {
        temp = 0xff;

        for (uint8_t pixel = 0; pixel < 8; pixel++) {
            for (uint8_t plane = 0; plane < count; plane++) {
                if (svga->colournocare & (1 << plane)) {
                    /* If we care about a plane, and the pixel has a mismatch on it, clear its bit. */
                    if (((svga->latch.b[plane] >> pixel) & 1) != ((svga->colourcompare >> plane) & 1))
                        temp &= ~(1 << pixel);
                }
            }
        }

        ret = temp;
    } else
        ret = svga->vram[addr | readplane];

    return ret;
}

void
svga_write(uint32_t addr, uint8_t val, void *priv)
{
    svga_write_common(addr, val, 0, priv);
}

void
svga_write_linear(uint32_t addr, uint8_t val, void *priv)
{
    svga_write_common(addr, val, 1, priv);
}

uint8_t
svga_read(uint32_t addr, void *priv)
{
    return svga_read_common(addr, 0, priv);
}

uint8_t
svga_read_linear(uint32_t addr, void *priv)
{
    return svga_read_common(addr, 1, priv);
}

void
svga_doblit(int wx, int wy, svga_t *svga)
{
    int       y_add;
    int       x_add;
    int       y_start;
    int       x_start;
    int       bottom;
    uint32_t *p;
    int       i;
    int       j;
    int       xs_temp;
    int       ys_temp;

    y_add   = enable_overscan ? svga->monitor->mon_overscan_y : 0;
    x_add   = enable_overscan ? svga->monitor->mon_overscan_x : 0;
    y_start = enable_overscan ? 0 : (svga->monitor->mon_overscan_y >> 1);
    x_start = enable_overscan ? 0 : (svga->monitor->mon_overscan_x >> 1);
    bottom  = (svga->monitor->mon_overscan_y >> 1);

    if (svga->vertical_linedbl) {
        y_add <<= 1;
        y_start <<= 1;
        bottom <<= 1;
    }

    if ((wx <= 0) || (wy <= 0))
        return;

    if (svga->vertical_linedbl)
        svga->y_add <<= 1;

    xs_temp = wx;
    ys_temp = wy + 1;
    if (svga->vertical_linedbl)
        ys_temp++;
    if (xs_temp < 64)
        xs_temp = 640;
    if (ys_temp < 32)
        ys_temp = 200;

    if ((svga->crtc[0x17] & 0x80) && ((xs_temp != svga->monitor->mon_xsize) || (ys_temp != svga->monitor->mon_ysize) || video_force_resize_get_monitor(svga->monitor_index))) {
        /* Screen res has changed.. fix up, and let them know. */
        svga->monitor->mon_xsize = xs_temp;
        svga->monitor->mon_ysize = ys_temp;

        if ((svga->monitor->mon_xsize > 1984) || (svga->monitor->mon_ysize > 2016)) {
            /* 2048x2048 is the biggest safe render texture, to account for overscan,
               we suppress overscan starting from x 1984 and y 2016. */
            x_add             = 0;
            y_add             = 0;
            suppress_overscan = 1;
        } else
            suppress_overscan = 0;

        /* Block resolution changes while in DPMS mode to avoid getting a bogus
           screen width (320). We're already rendering a blank screen anyway. */
        if (!svga->dpms)
            set_screen_size_monitor(svga->monitor->mon_xsize + x_add, svga->monitor->mon_ysize + y_add, svga->monitor_index);

        if (video_force_resize_get_monitor(svga->monitor_index))
            video_force_resize_set_monitor(0, svga->monitor_index);
    }

    if ((wx >= 160) && ((wy + 1) >= 120)) {
        /* Draw (overscan_size - scroll size) lines of overscan on top and bottom. */
        for (i = 0; i < svga->y_add; i++) {
            p = &svga->monitor->target_buffer->line[i & 0x7ff][0];

            for (j = 0; j < (svga->monitor->mon_xsize + x_add); j++)
                p[j] = svga->dpms ? 0 : svga->overscan_color;
        }

        for (i = 0; i < bottom; i++) {
            p = &svga->monitor->target_buffer->line[(svga->monitor->mon_ysize + svga->y_add + i) & 0x7ff][0];

            for (j = 0; j < (svga->monitor->mon_xsize + x_add); j++)
                p[j] = svga->dpms ? 0 : svga->overscan_color;
        }
    }

    video_blit_memtoscreen_monitor(x_start, y_start, svga->monitor->mon_xsize + x_add, svga->monitor->mon_ysize + y_add, svga->monitor_index);

    if (svga->vertical_linedbl)
        svga->vertical_linedbl >>= 1;
}

void
svga_writeb_linear(uint32_t addr, uint8_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    if (!svga->fast) {
        svga_write_linear(addr, val, priv);
        return;
    }

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return;
    addr &= svga->vram_mask;
    svga->changedvram[addr >> 12] = svga->monitor->mon_changeframecount;
    svga->vram[addr]              = val;
}

void
svga_writew_common(uint32_t addr, uint16_t val, uint8_t linear, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    if (!svga->fast) {
        svga_write_common(addr, val, linear, priv);
        svga_write_common(addr + 1, val >> 8, linear, priv);
        return;
    }

    cycles -= svga->monitor->mon_video_timing_write_w;

    if (!linear) {
        xga_write_test(addr, val & 0xff, svga);
        xga_write_test(addr + 1, val >> 8, svga);
        addr = svga_decode_addr(svga, addr, 1);

        if (addr == 0xffffffff)
            return;
    }

    addr &= svga->decode_mask;
    if (svga->translate_address) {
        uint32_t addr2 = svga->translate_address(addr, priv);
        if (addr2 < svga->vram_max) {
            svga->vram[addr2 & svga->vram_mask] = val & 0xff;
            svga->changedvram[addr2 >> 12]      = svga->monitor->mon_changeframecount;
        }
        addr2 = svga->translate_address(addr + 1, priv);
        if (addr2 < svga->vram_max) {
            svga->vram[addr2 & svga->vram_mask] = (val >> 8) & 0xff;
            svga->changedvram[addr2 >> 12]      = svga->monitor->mon_changeframecount;
        }
        return;
    }
    if (addr >= svga->vram_max)
        return;
    addr &= svga->vram_mask;

    svga->changedvram[addr >> 12]   = svga->monitor->mon_changeframecount;
    *(uint16_t *) &svga->vram[addr] = val;
}

void
svga_writew(uint32_t addr, uint16_t val, void *priv)
{
    svga_writew_common(addr, val, 0, priv);
}

void
svga_writew_linear(uint32_t addr, uint16_t val, void *priv)
{
    svga_writew_common(addr, val, 1, priv);
}

void
svga_writel_common(uint32_t addr, uint32_t val, uint8_t linear, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    if (!svga->fast) {
        svga_write_common(addr, val, linear, priv);
        svga_write_common(addr + 1, val >> 8, linear, priv);
        svga_write_common(addr + 2, val >> 16, linear, priv);
        svga_write_common(addr + 3, val >> 24, linear, priv);
        return;
    }

    cycles -= svga->monitor->mon_video_timing_write_l;

    if (!linear) {
        xga_write_test(addr, val & 0xff, svga);
        xga_write_test(addr + 1, (val >> 8) & 0xff, svga);
        xga_write_test(addr + 2, (val >> 16) & 0xff, svga);
        xga_write_test(addr + 3, (val >> 24) & 0xff, svga);
        addr = svga_decode_addr(svga, addr, 1);

        if (addr == 0xffffffff)
            return;
    }

    addr &= svga->decode_mask;
    if (svga->translate_address) {
        uint32_t addr2 = svga->translate_address(addr, priv);
        if (addr2 < svga->vram_max) {
            svga->vram[addr2 & svga->vram_mask] = val & 0xff;
            svga->changedvram[addr2 >> 12]      = svga->monitor->mon_changeframecount;
        }
        addr2 = svga->translate_address(addr + 1, priv);
        if (addr2 < svga->vram_max) {
            svga->vram[addr2 & svga->vram_mask] = (val >> 8) & 0xff;
            svga->changedvram[addr2 >> 12]      = svga->monitor->mon_changeframecount;
        }
        addr2 = svga->translate_address(addr + 2, priv);
        if (addr2 < svga->vram_max) {
            svga->vram[addr2 & svga->vram_mask] = (val >> 16) & 0xff;
            svga->changedvram[addr2 >> 12]      = svga->monitor->mon_changeframecount;
        }
        addr2 = svga->translate_address(addr + 3, priv);
        if (addr2 < svga->vram_max) {
            svga->vram[addr2 & svga->vram_mask] = (val >> 24) & 0xff;
            svga->changedvram[addr2 >> 12]      = svga->monitor->mon_changeframecount;
        }
        return;
    }
    if (addr >= svga->vram_max)
        return;

    addr &= svga->vram_mask;

    svga->changedvram[addr >> 12]   = svga->monitor->mon_changeframecount;
    *(uint32_t *) &svga->vram[addr] = val;
}

void
svga_writel(uint32_t addr, uint32_t val, void *priv)
{
    svga_writel_common(addr, val, 0, priv);
}

void
svga_writel_linear(uint32_t addr, uint32_t val, void *priv)
{
    svga_writel_common(addr, val, 1, priv);
}

uint8_t
svga_readb_linear(uint32_t addr, void *priv)
{
    const svga_t *svga = (svga_t *) priv;

    if (!svga->fast)
        return svga_read_linear(addr, priv);

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return 0xff;

    return svga->vram[addr & svga->vram_mask];
}

uint16_t
svga_readw_common(uint32_t addr, uint8_t linear, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    if (!svga->fast)
        return svga_read_common(addr, linear, priv) | (svga_read_common(addr + 1, linear, priv) << 8);

    cycles -= svga->monitor->mon_video_timing_read_w;

    if (!linear) {
        (void) xga_read_test(addr, svga);
        (void) xga_read_test(addr + 1, svga);
        addr = svga_decode_addr(svga, addr, 0);
        if (addr == 0xffffffff)
            return 0xffff;
    }

    addr &= svga->decode_mask;
    if (svga->translate_address) {
        uint8_t  val1  = 0xff;
        uint8_t  val2  = 0xff;
        uint32_t addr2 = svga->translate_address(addr, priv);
        if (addr2 < svga->vram_max)
            val1 = svga->vram[addr2 & svga->vram_mask];
        addr2 = svga->translate_address(addr + 1, priv);
        if (addr2 < svga->vram_max)
            val2 = svga->vram[addr2 & svga->vram_mask];
        return (val2 << 8) | val1;
    }
    if (addr >= svga->vram_max)
        return 0xffff;

    return *(uint16_t *) &svga->vram[addr & svga->vram_mask];
}

uint16_t
svga_readw(uint32_t addr, void *priv)
{
    return svga_readw_common(addr, 0, priv);
}

uint16_t
svga_readw_linear(uint32_t addr, void *priv)
{
    return svga_readw_common(addr, 1, priv);
}

uint32_t
svga_readl_common(uint32_t addr, uint8_t linear, void *priv)
{
    svga_t *svga = (svga_t *) priv;

    if (!svga->fast)
        return svga_read_common(addr, linear, priv) | (svga_read_common(addr + 1, linear, priv) << 8) | (svga_read_common(addr + 2, linear, priv) << 16) | (svga_read_common(addr + 3, linear, priv) << 24);

    cycles -= svga->monitor->mon_video_timing_read_l;

    if (!linear) {
        (void) xga_read_test(addr, svga);
        (void) xga_read_test(addr + 1, svga);
        (void) xga_read_test(addr + 2, svga);
        (void) xga_read_test(addr + 3, svga);
        addr = svga_decode_addr(svga, addr, 0);
        if (addr == 0xffffffff)
            return 0xffffffff;
    }

    addr &= svga->decode_mask;
    if (svga->translate_address) {
        uint8_t  val1  = 0xff;
        uint8_t  val2  = 0xff;
        uint8_t  val3  = 0xff;
        uint8_t  val4  = 0xff;
        uint32_t addr2 = svga->translate_address(addr, priv);
        if (addr2 < svga->vram_max)
            val1 = svga->vram[addr2 & svga->vram_mask];
        addr2 = svga->translate_address(addr + 1, priv);
        if (addr2 < svga->vram_max)
            val2 = svga->vram[addr2 & svga->vram_mask];
        addr2 = svga->translate_address(addr + 2, priv);
        if (addr2 < svga->vram_max)
            val3 = svga->vram[addr2 & svga->vram_mask];
        addr2 = svga->translate_address(addr + 3, priv);
        if (addr2 < svga->vram_max)
            val4 = svga->vram[addr2 & svga->vram_mask];
        return (val4 << 24) | (val3 << 16) | (val2 << 8) | val1;
    }
    if (addr >= svga->vram_max)
        return 0xffffffff;

    return *(uint32_t *) &svga->vram[addr & svga->vram_mask];
}

uint32_t
svga_readl(uint32_t addr, void *priv)
{
    return svga_readl_common(addr, 0, priv);
}

uint32_t
svga_readl_linear(uint32_t addr, void *priv)
{
    return svga_readl_common(addr, 1, priv);
}
