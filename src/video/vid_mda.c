/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          MDA emulation.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2025 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/lpt.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/vid_mda.h>
#include <86box/plat_unused.h>

static int mdacols[256][2][2];

static video_timings_t timing_mda = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

void mda_recalctimings(mda_t *mda);

void
mda_out(uint16_t addr, uint8_t val, void *priv)
{
    mda_t *mda = (mda_t *) priv;

    switch (addr) {
        case 0x3b0:
        case 0x3b2:
        case 0x3b4:
        case 0x3b6:
            mda->crtcreg = val & 31;
            return;
        case 0x3b1:
        case 0x3b3:
        case 0x3b5:
        case 0x3b7:
            mda->crtc[mda->crtcreg] = val;
            if (mda->crtc[10] == 6 && mda->crtc[11] == 7) /*Fix for Generic Turbo XT BIOS, which sets up cursor registers wrong*/
            {
                mda->crtc[10] = 0xb;
                mda->crtc[11] = 0xc;
            }
            mda_recalctimings(mda);
            return;
        case 0x3b8:
            mda->ctrl = val;
            return;

        default:
            break;
    }
}

uint8_t
mda_in(uint16_t addr, void *priv)
{
    const mda_t *mda = (mda_t *) priv;

    switch (addr) {
        case 0x3b0:
        case 0x3b2:
        case 0x3b4:
        case 0x3b6:
            return mda->crtcreg;
        case 0x3b1:
        case 0x3b3:
        case 0x3b5:
        case 0x3b7:
            return mda->crtc[mda->crtcreg];
        case 0x3ba:
            return mda->stat | 0xF0;

        default:
            break;
    }
    return 0xff;
}

void
mda_write(uint32_t addr, uint8_t val, void *priv)
{
    mda_t *mda              = (mda_t *) priv;
    mda->vram[addr & 0xfff] = val;
}

uint8_t
mda_read(uint32_t addr, void *priv)
{
    const mda_t *mda = (mda_t *) priv;

    return mda->vram[addr & 0xfff];
}

void
mda_recalctimings(mda_t *mda)
{
    double _dispontime;
    double _dispofftime;
    double disptime;
    disptime     = mda->crtc[0] + 1;
    _dispontime  = mda->crtc[1];
    _dispofftime = disptime - _dispontime;
    _dispontime *= MDACONST;
    _dispofftime *= MDACONST;
    mda->dispontime  = (uint64_t) (_dispontime);
    mda->dispofftime = (uint64_t) (_dispofftime);
}

void
mda_poll(void *priv)
{
    mda_t   *mda = (mda_t *) priv;
    uint16_t ca  = (mda->crtc[15] | (mda->crtc[14] << 8)) & 0x3fff;
    int      drawcursor;
    int      x;
    int      c;
    int      oldvc;
    uint8_t  chr;
    uint8_t  attr;
    int      oldsc;
    int      blink;

    VIDEO_MONITOR_PROLOGUE()
    if (!mda->linepos) {
        timer_advance_u64(&mda->timer, mda->dispofftime);
        mda->stat |= 1;
        mda->linepos = 1;
        oldsc        = mda->sc;
        if ((mda->crtc[8] & 3) == 3)
            mda->sc = (mda->sc << 1) & 7;
        if (mda->dispon) {
            if (mda->displine < mda->firstline) {
                mda->firstline = mda->displine;
                video_wait_for_buffer();
            }
            mda->lastline = mda->displine;
            for (x = 0; x < mda->crtc[1]; x++) {
                chr        = mda->vram[(mda->ma << 1) & 0xfff];
                attr       = mda->vram[((mda->ma << 1) + 1) & 0xfff];
                drawcursor = ((mda->ma == ca) && mda->con && mda->cursoron);
                blink      = ((mda->blink & 16) && (mda->ctrl & 0x20) && (attr & 0x80) && !drawcursor);
                if (mda->sc == 12 && ((attr & 7) == 1)) {
                    for (c = 0; c < 9; c++)
                        buffer32->line[mda->displine][(x * 9) + c] = mdacols[attr][blink][1];
                } else {
                    for (c = 0; c < 8; c++)
                        buffer32->line[mda->displine][(x * 9) + c] = mdacols[attr][blink][(fontdatm[chr + mda->fontbase][mda->sc] & (1 << (c ^ 7))) ? 1 : 0];
                    if ((chr & ~0x1f) == 0xc0)
                        buffer32->line[mda->displine][(x * 9) + 8] = mdacols[attr][blink][fontdatm[chr + mda->fontbase][mda->sc] & 1];
                    else
                        buffer32->line[mda->displine][(x * 9) + 8] = mdacols[attr][blink][0];
                }
                mda->ma++;
                if (drawcursor) {
                    for (c = 0; c < 9; c++)
                        buffer32->line[mda->displine][(x * 9) + c] ^= mdacols[attr][0][1];
                }
            }

            video_process_8(mda->crtc[1] * 9, mda->displine);
        }
        mda->sc = oldsc;
        if (mda->vc == mda->crtc[7] && !mda->sc) {
            mda->stat |= 8;
        }
        mda->displine++;
        if (mda->displine >= 500)
            mda->displine = 0;
    } else {
        timer_advance_u64(&mda->timer, mda->dispontime);
        if (mda->dispon)
            mda->stat &= ~1;
        mda->linepos = 0;
        if (mda->vsynctime) {
            mda->vsynctime--;
            if (!mda->vsynctime) {
                mda->stat &= ~8;
            }
        }
        if (mda->sc == (mda->crtc[11] & 31) || ((mda->crtc[8] & 3) == 3 && mda->sc == ((mda->crtc[11] & 31) >> 1))) {
            mda->con  = 0;
            mda->coff = 1;
        }
        if (mda->vadj) {
            mda->sc++;
            mda->sc &= 31;
            mda->ma = mda->maback;
            mda->vadj--;
            if (!mda->vadj) {
                mda->dispon = 1;
                mda->ma = mda->maback = (mda->crtc[13] | (mda->crtc[12] << 8)) & 0x3fff;
                mda->sc               = 0;
            }
        } else if (mda->sc == mda->crtc[9] || ((mda->crtc[8] & 3) == 3 && mda->sc == (mda->crtc[9] >> 1))) {
            mda->maback = mda->ma;
            mda->sc     = 0;
            oldvc       = mda->vc;
            mda->vc++;
            mda->vc &= 127;
            if (mda->vc == mda->crtc[6])
                mda->dispon = 0;
            if (oldvc == mda->crtc[4]) {
                mda->vc   = 0;
                mda->vadj = mda->crtc[5];
                if (!mda->vadj)
                    mda->dispon = 1;
                if (!mda->vadj)
                    mda->ma = mda->maback = (mda->crtc[13] | (mda->crtc[12] << 8)) & 0x3fff;
                if ((mda->crtc[10] & 0x60) == 0x20)
                    mda->cursoron = 0;
                else
                    mda->cursoron = mda->blink & 16;
            }
            if (mda->vc == mda->crtc[7]) {
                mda->dispon    = 0;
                mda->displine  = 0;
                mda->vsynctime = 16;
                if (mda->crtc[7]) {
                    x = mda->crtc[1] * 9;
                    mda->lastline++;
                    if ((x != xsize) || ((mda->lastline - mda->firstline) != ysize) || video_force_resize_get()) {
                        xsize = x;
                        ysize = mda->lastline - mda->firstline;
                        if (xsize < 64)
                            xsize = 656;
                        if (ysize < 32)
                            ysize = 200;
                        set_screen_size(xsize, ysize);

                        if (video_force_resize_get())
                            video_force_resize_set(0);
                    }
                    video_blit_memtoscreen(0, mda->firstline, xsize, ysize);
                    frames++;
                    video_res_x = mda->crtc[1];
                    video_res_y = mda->crtc[6];
                    video_bpp   = 0;
                }
                mda->firstline = 1000;
                mda->lastline  = 0;
                mda->blink++;
            }
        } else {
            mda->sc++;
            mda->sc &= 31;
            mda->ma = mda->maback;
        }
        if (mda->sc == (mda->crtc[10] & 31) || ((mda->crtc[8] & 3) == 3 && mda->sc == ((mda->crtc[10] & 31) >> 1))) {
            mda->con = 1;
        }
    }
    VIDEO_MONITOR_EPILOGUE();
}

void
mda_init(mda_t *mda)
{
    for (uint16_t c = 0; c < 256; c++) {
        mdacols[c][0][0] = mdacols[c][1][0] = mdacols[c][1][1] = 16;
        if (c & 8)
            mdacols[c][0][1] = 15 + 16;
        else
            mdacols[c][0][1] = 7 + 16;
    }
    mdacols[0x70][0][1] = 16;
    mdacols[0x70][0][0] = mdacols[0x70][1][0] = mdacols[0x70][1][1] = 16 + 15;
    mdacols[0xF0][0][1]                                             = 16;
    mdacols[0xF0][0][0] = mdacols[0xF0][1][0] = mdacols[0xF0][1][1] = 16 + 15;
    mdacols[0x78][0][1]                                             = 16 + 7;
    mdacols[0x78][0][0] = mdacols[0x78][1][0] = mdacols[0x78][1][1] = 16 + 15;
    mdacols[0xF8][0][1]                                             = 16 + 7;
    mdacols[0xF8][0][0] = mdacols[0xF8][1][0] = mdacols[0xF8][1][1] = 16 + 15;
    mdacols[0x00][0][1] = mdacols[0x00][1][1] = 16;
    mdacols[0x08][0][1] = mdacols[0x08][1][1] = 16;
    mdacols[0x80][0][1] = mdacols[0x80][1][1] = 16;
    mdacols[0x88][0][1] = mdacols[0x88][1][1] = 16;

    overscan_x = overscan_y = 0;
    mda->monitor_index      = monitor_index_global;

    cga_palette = device_get_config_int("rgb_type") << 1;
    if (cga_palette > 6) {
        cga_palette = 0;
    }
    cgapal_rebuild();

    timer_add(&mda->timer, mda_poll, mda, 1);
}

void *
mda_standalone_init(UNUSED(const device_t *info))
{
    mda_t *mda = malloc(sizeof(mda_t));
    memset(mda, 0, sizeof(mda_t));
    video_inform(VIDEO_FLAG_TYPE_MDA, &timing_mda);

    mda->vram = malloc(0x1000);

    mem_mapping_add(&mda->mapping, 0xb0000, 0x08000, mda_read, NULL, NULL, mda_write, NULL, NULL, NULL, MEM_MAPPING_EXTERNAL, mda);
    io_sethandler(0x03b0, 0x0010, mda_in, NULL, NULL, mda_out, NULL, NULL, mda);

    mda_init(mda);

    lpt3_setup(LPT_MDA_ADDR);

    return mda;
}

void
mda_setcol(int chr, int blink, int fg, uint8_t cga_ink)
{
    mdacols[chr][blink][fg] = 16 + cga_ink;
}

void
mda_close(void *priv)
{
    mda_t *mda = (mda_t *) priv;

    free(mda->vram);
    free(mda);
}

void
mda_speed_changed(void *priv)
{
    mda_t *mda = (mda_t *) priv;

    mda_recalctimings(mda);
}

static const device_config_t mda_config[] = {
  // clang-format off
    {
        .name           = "rgb_type",
        .description    = "Display type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Default", .value = 0 },
            { .description = "Green",   .value = 1 },
            { .description = "Amber",   .value = 2 },
            { .description = "Gray",    .value = 3 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t mda_device = {
    .name          = "IBM MDA",
    .internal_name = "mda",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = mda_standalone_init,
    .close         = mda_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = mda_speed_changed,
    .force_redraw  = NULL,
    .config        = mda_config
};
