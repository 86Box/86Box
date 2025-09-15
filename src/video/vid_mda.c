/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM Monochrome Display and Printer Adapter emulation.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Connor Hyde, <mario64crashed@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2025 starfrost / Connor Hyde
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/video.h>
#include <86box/vid_mda.h>
#include <86box/plat_unused.h>

// Enumerates MDA monitor types
enum mda_monitor_type_e {
    MDA_MONITOR_TYPE_DEFAULT = 0, // Default MDA monitor type.
    MDA_MONITOR_TYPE_GREEN   = 1, // Green phosphor
    MDA_MONITOR_TYPE_AMBER   = 2, // Amber phosphor
    MDA_MONITOR_TYPE_GRAY    = 3, // Gray phosphor
    MDA_MONITOR_TYPE_RGBI    = 4, // RGBI colour monitor with modified rev1 or rev0 MDA card for colour support
} mda_monitor_type;

// [attr][blink][fg]
static int mda_attr_to_color_table[256][2][2];

static video_timings_t timing_mda = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

void mda_recalctimings(mda_t *mda);

void
mda_out(uint16_t addr, uint8_t val, void *priv)
{
    mda_t *mda = (mda_t *) priv;

    if (addr < MDA_REGISTER_START
        || addr > MDA_REGISTER_CRT_STATUS) // Maintain old behaviour for printer registers, just in case
        return;

    switch (addr) {
        case MDA_REGISTER_MODE_CONTROL:
            mda->mode = val;
            return;
        default:
            break;
    }

    // addr & 1 == 1 = MDA_REGISTER_CRTC_DATA
    // otherwise       MDA_REGISTER_CRTC_INDEX
    if (addr & 1) {
        mda->crtc[mda->crtcreg] = val;
        if (mda->crtc[MDA_CRTC_CURSOR_START] == 6
            && mda->crtc[MDA_CRTC_CURSOR_END] == 7) /*Fix for Generic Turbo XT BIOS, which sets up cursor registers wrong*/
        {
            mda->crtc[MDA_CRTC_CURSOR_START] = 0xb;
            mda->crtc[MDA_CRTC_CURSOR_END]   = 0xc;
        }
        mda_recalctimings(mda);
    } else
        mda->crtcreg = val & 31;
}

uint8_t
mda_in(uint16_t addr, void *priv)
{
    const mda_t *mda = (mda_t *) priv;

    switch (addr) {
        case MDA_REGISTER_CRT_STATUS:
            return mda->status | 0xF0;
        default:
            if (addr < MDA_REGISTER_START
                || addr > MDA_REGISTER_CRT_STATUS) // Maintain old behaviour for printer registers, just in case
                return 0xFF;

            // MDA_REGISTER_CRTC_DATA
            if (addr & 1)
                return mda->crtc[mda->crtcreg];
            else
                return mda->crtcreg;

            break;
    }

    return 0xFF;
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
    disptime     = (mda->crtc[MDA_CRTC_HTOTAL] + 1);
    _dispontime  = (mda->crtc[MDA_CRTC_HDISP]);
    _dispofftime = disptime - _dispontime;
    _dispontime *= MDACONST;
    _dispofftime *= MDACONST;
    mda->dispontime  = (uint64_t) (_dispontime);
    mda->dispofftime = (uint64_t) (_dispofftime);
}

void
mda_poll(void *priv)
{
    mda_t   *mda        = (mda_t *) priv;
    uint16_t cursoraddr = (mda->crtc[MDA_CRTC_CURSOR_ADDR_LOW] | (mda->crtc[MDA_CRTC_CURSOR_ADDR_HIGH] << 8)) & 0x3fff;
    bool     drawcursor;
    int32_t  oldvc;
    uint8_t  chr;
    uint8_t  attr;
    int32_t  scanline_old;
    int32_t  blink;

    VIDEO_MONITOR_PROLOGUE()

    if (!mda->linepos) {
        timer_advance_u64(&mda->timer, mda->dispofftime);
        mda->status |= 1;
        mda->linepos = 1;
        scanline_old = mda->scanline;
        if ((mda->crtc[MDA_CRTC_INTERLACE] & 3) == 3)
            mda->scanline = (mda->scanline << 1) & 7;

        if (mda->dispon) {
            if (mda->displine < mda->firstline) {
                mda->firstline = mda->displine;
                video_wait_for_buffer();
            }
            mda->lastline = mda->displine;

            for (uint32_t x = 0; x < mda->crtc[MDA_CRTC_HDISP]; x++) {
                chr        = mda->vram[(mda->memaddr << 1) & 0xfff];
                attr       = mda->vram[((mda->memaddr << 1) + 1) & 0xfff];
                drawcursor = ((mda->memaddr == cursoraddr) && mda->cursorvisible && mda->cursoron);
                blink      = ((mda->blink & 16) && (mda->mode & MDA_MODE_BLINK) && (attr & 0x80) && !drawcursor);

                // Colours that will be used
                int32_t color_bg = 0, color_fg = 0;

                // If we are using an RGBI monitor allow colour
                if (mda->monitor_type == MDA_MONITOR_TYPE_RGBI
                    && !(mda->mode & MDA_MODE_BW)) {
                    color_bg = (attr >> 4) & 0x0F;
                    color_fg = (attr & 0x0F);

                    // turn off bright bg colours in blink mode
                    if ((mda->mode & MDA_MODE_BLINK)
                        && (color_bg & 0x8))
                        color_bg &= ~(0x8);

                    // black-on-non black or white colours forced to white
                    // grey-on-colours forced to bright white

                    bool special_treatment = (color_bg != 0 && color_bg != 7);

                    if (color_fg == MDA_COLOR_GREY
                        && special_treatment)
                        color_fg = MDA_COLOR_BRIGHT_WHITE;

                    if (color_fg == 0
                        && special_treatment)
                        color_fg = MDA_COLOR_GREY;

                    // gray is black
                    if (color_fg == MDA_COLOR_GREY
                        && (color_bg == MDA_COLOR_GREY || color_bg == MDA_COLOR_BLACK))
                        color_fg = MDA_COLOR_BLACK;
                }

                if (mda->scanline == 12
                    && ((attr & 7) == 1)) { // underline
                    for (uint32_t column = 0; column < 9; column++) {
                        if (mda->monitor_type == MDA_MONITOR_TYPE_RGBI
                            && !(mda->mode & MDA_MODE_BW)) {
                            buffer32->line[mda->displine][(x * 9) + column] = CGAPAL_CGA_START + color_fg;
                        } else
                            buffer32->line[mda->displine][(x * 9) + column] = mda_attr_to_color_table[attr][blink][1];
                    }
                } else { // character
                    for (uint32_t column = 0; column < 8; column++) {
                        // bg=0, fg=1
                        bool is_fg = (fontdatm[chr + mda->fontbase][mda->scanline] & (1 << (column ^ 7))) ? 1 : 0;

                        uint32_t font_char = mda_attr_to_color_table[attr][blink][is_fg];

                        if (mda->monitor_type == MDA_MONITOR_TYPE_RGBI
                            && !(mda->mode & MDA_MODE_BW)) {
                            if (!is_fg)
                                font_char = CGAPAL_CGA_START + color_bg;
                            else
                                font_char = CGAPAL_CGA_START + color_fg;
                        }

                        buffer32->line[mda->displine][(x * 9) + column] = font_char;
                    }

                    // these characters (C0-DF) have their background extended to their 9th column
                    if ((chr & ~0x1f) == 0xc0) {
                        bool     is_fg        = fontdatm[chr + mda->fontbase][mda->scanline] & 1;
                        uint32_t final_result = mda_attr_to_color_table[attr][blink][is_fg];

                        if (mda->monitor_type == MDA_MONITOR_TYPE_RGBI
                            && !(mda->mode & MDA_MODE_BW)) {
                            if (!is_fg)
                                final_result = CGAPAL_CGA_START + color_bg;
                            else
                                final_result = CGAPAL_CGA_START + color_fg;
                        }

                        buffer32->line[mda->displine][(x * 9) + 8] = final_result;

                    } else {
                        if (mda->monitor_type == MDA_MONITOR_TYPE_RGBI
                            && !(mda->mode & MDA_MODE_BW)) {
                            buffer32->line[mda->displine][(x * 9) + 8] = CGAPAL_CGA_START + color_bg;

                        } else
                            buffer32->line[mda->displine][(x * 9) + 8] = mda_attr_to_color_table[attr][blink][0];
                    }
                }

                mda->memaddr++;

                if (drawcursor) {
                    for (uint32_t column = 0; column < 9; column++) {
                        if (mda->monitor_type == MDA_MONITOR_TYPE_RGBI
                            && !(mda->mode & MDA_MODE_BW)) {
                            buffer32->line[mda->displine][(x * 9) + column] ^= CGAPAL_CGA_START + color_fg;
                        } else
                            buffer32->line[mda->displine][(x * 9) + column] ^= mda_attr_to_color_table[attr][0][1];
                    }
                }
            }

            video_process_8(mda->crtc[MDA_CRTC_HDISP] * 9, mda->displine);
        }
        mda->scanline = scanline_old;
        if (mda->vc == mda->crtc[MDA_CRTC_VSYNC] && !mda->scanline) {
            mda->status |= 8;
        }
        mda->displine++;
        if (mda->displine >= 500)
            mda->displine = 0;
    } else {
        timer_advance_u64(&mda->timer, mda->dispontime);
        if (mda->dispon)
            mda->status &= ~1;
        mda->linepos = 0;

        if (mda->vsynctime) {
            mda->vsynctime--;
            if (!mda->vsynctime) {
                mda->status &= ~8;
            }
        }
        if (mda->scanline == (mda->crtc[MDA_CRTC_CURSOR_END] & 31)
            || ((mda->crtc[MDA_CRTC_INTERLACE] & 3) == 3
                && mda->scanline == ((mda->crtc[MDA_CRTC_CURSOR_END] & 31) >> 1))) {
            mda->cursorvisible = 0;
        }

        if (mda->vadj) {
            mda->scanline++;
            mda->scanline &= 31;
            mda->memaddr = mda->memaddr_backup;
            mda->vadj--;
            if (!mda->vadj) {
                mda->dispon  = 1;
                mda->memaddr = mda->memaddr_backup = (mda->crtc[MDA_CRTC_START_ADDR_LOW] | (mda->crtc[MDA_CRTC_START_ADDR_HIGH] << 8)) & 0x3fff;
                mda->scanline                      = 0;
            }
        } else if (mda->scanline == mda->crtc[MDA_CRTC_MAX_SCANLINE_ADDR]
                   || ((mda->crtc[MDA_CRTC_INTERLACE] & 3) == 3
                       && mda->scanline == (mda->crtc[MDA_CRTC_MAX_SCANLINE_ADDR] >> 1))) {
            mda->memaddr_backup = mda->memaddr;
            mda->scanline       = 0;
            oldvc               = mda->vc;
            mda->vc++;
            mda->vc &= 127;
            if (mda->vc == mda->crtc[MDA_CRTC_VDISP])
                mda->dispon = 0;
            if (oldvc == mda->crtc[MDA_CRTC_VTOTAL]) {
                mda->vc   = 0;
                mda->vadj = mda->crtc[MDA_CRTC_VTOTAL_ADJUST];
                if (!mda->vadj)
                    mda->dispon = 1;
                if (!mda->vadj)
                    mda->memaddr = mda->memaddr_backup = (mda->crtc[MDA_CRTC_START_ADDR_LOW] | (mda->crtc[MDA_CRTC_START_ADDR_HIGH] << 8)) & 0x3fff;
                if ((mda->crtc[MDA_CRTC_CURSOR_START] & 0x60) == 0x20)
                    mda->cursoron = 0;
                else
                    mda->cursoron = mda->blink & 16;
            }

            if (mda->vc == mda->crtc[MDA_CRTC_VSYNC]) {
                mda->dispon    = 0;
                mda->displine  = 0;
                mda->vsynctime = 16;
                if (mda->crtc[MDA_CRTC_VSYNC]) {
                    uint32_t x = mda->crtc[MDA_CRTC_HDISP] * 9;
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
                    video_res_x = mda->crtc[MDA_CRTC_HDISP];
                    video_res_y = mda->crtc[MDA_CRTC_VDISP];
                    video_bpp   = 0;
                }
                mda->firstline = 1000;
                mda->lastline  = 0;
                mda->blink++;
            }
        } else {
            mda->scanline++;
            mda->scanline &= 31;
            mda->memaddr = mda->memaddr_backup;
        }

        if (mda->scanline == (mda->crtc[MDA_CRTC_CURSOR_START] & 31)
            || ((mda->crtc[MDA_CRTC_INTERLACE] & 3) == 3
                && mda->scanline == ((mda->crtc[MDA_CRTC_CURSOR_START] & 31) >> 1))) {
            mda->cursorvisible = 1;
        }
    }
    VIDEO_MONITOR_EPILOGUE();
}

void
mda_init(mda_t *mda)
{

    for (uint16_t attr = 0; attr < 256; attr++) {
        mda_attr_to_color_table[attr][0][0] = mda_attr_to_color_table[attr][1][0] = mda_attr_to_color_table[attr][1][1] = 16;
        if (attr & 8)
            mda_attr_to_color_table[attr][0][1] = 15 + 16;
        else
            mda_attr_to_color_table[attr][0][1] = 7 + 16;
    }
    mda_attr_to_color_table[0x70][0][1] = 16;
    mda_attr_to_color_table[0x70][0][0] = mda_attr_to_color_table[0x70][1][0] = mda_attr_to_color_table[0x70][1][1] = CGAPAL_CGA_START + 15;
    mda_attr_to_color_table[0xF0][0][1]                                                                             = 16;
    mda_attr_to_color_table[0xF0][0][0] = mda_attr_to_color_table[0xF0][1][0] = mda_attr_to_color_table[0xF0][1][1] = CGAPAL_CGA_START + 15;
    mda_attr_to_color_table[0x78][0][1]                                                                             = CGAPAL_CGA_START + 7;
    mda_attr_to_color_table[0x78][0][0] = mda_attr_to_color_table[0x78][1][0] = mda_attr_to_color_table[0x78][1][1] = CGAPAL_CGA_START + 15;
    mda_attr_to_color_table[0xF8][0][1]                                                                             = CGAPAL_CGA_START + 7;
    mda_attr_to_color_table[0xF8][0][0] = mda_attr_to_color_table[0xF8][1][0] = mda_attr_to_color_table[0xF8][1][1] = CGAPAL_CGA_START + 15;
    mda_attr_to_color_table[0x00][0][1] = mda_attr_to_color_table[0x00][1][1] = 16;
    mda_attr_to_color_table[0x08][0][1] = mda_attr_to_color_table[0x08][1][1] = 16;
    mda_attr_to_color_table[0x80][0][1] = mda_attr_to_color_table[0x80][1][1] = 16;
    mda_attr_to_color_table[0x88][0][1] = mda_attr_to_color_table[0x88][1][1] = 16;

    overscan_x = overscan_y = 0;
    mda->monitor_index      = monitor_index_global;

    mda->monitor_type = device_get_config_int("rgb_type");
    cga_palette       = mda->monitor_type << 1;
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

    switch (device_get_config_int("font")) {
        case 0:
            loadfont(FONT_IBM_MDA_437_PATH, 0);
            break;
        case 1:
            loadfont(FONT_IBM_MDA_437_NORDIC_PATH, 0);
            break;
        case 2:
            loadfont(FONT_KAM_PATH, 0);
            break;
        case 3:
            loadfont(FONT_KAMCL16_PATH, 0);
            break;
        case 4:
            loadfont(FONT_TULIP_DGA_PATH, 0);
            break;
    }

    mem_mapping_add(&mda->mapping, 0xb0000, 0x08000,
                    mda_read, NULL, NULL,
                    mda_write, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL,
                    mda);

    io_sethandler(0x03b0, 0x0010,
                  mda_in, NULL, NULL,
                  mda_out, NULL, NULL,
                  mda);

    mda_init(mda);

    mda->lpt = device_add_inst(&lpt_port_device, 1);
    lpt_port_setup(mda->lpt, LPT_MDA_ADDR);
    lpt_set_3bc_used(1);

    return mda;
}

void
mda_setcol(int chr, int blink, int fg, uint8_t cga_ink)
{
    mda_attr_to_color_table[chr][blink][fg] = CGAPAL_CGA_START + cga_ink;
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
        .selection      = 
        {
            { .description = "Default", .value = MDA_MONITOR_TYPE_DEFAULT },
            { .description = "Green", .value = MDA_MONITOR_TYPE_GREEN },
            { .description = "Amber", .value = MDA_MONITOR_TYPE_AMBER },
            { .description = "Gray",  .value = MDA_MONITOR_TYPE_GRAY  },
            { .description = "Generic RGBI color monitor",  .value = MDA_MONITOR_TYPE_RGBI },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "font",
        .description    = "Font",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      =
        {
            { .description = "US (CP 437)", .value = 0 },
            { .description = "IBM Nordic (CP 437-Nordic)",  .value = 1 },
            { .description = "Czech Kamenicky (CP 895) #1", .value = 2 },
            { .description = "Czech Kamenicky (CP 895) #2", .value = 3 },
            { .description = "Tulip DGA",                   .value = 4 },
            { .description = "" }
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

