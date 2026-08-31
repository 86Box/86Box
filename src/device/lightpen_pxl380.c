/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of PXL-380 lightpen.
 *
 * Authors: Cacodemon345
 *          Daniel Balsom
 *
 *          Copyright 2026 Cacodemon345.
 *          Copyright 2022-2026 Daniel Balsom
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/video.h>
#include <86box/mouse.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/io.h>

struct pxl380_t
{
    uint16_t timer0_cnt;

    uint16_t timer0_cnt_latch;
    uint16_t timer1_cnt_latch;

    uint16_t timer1_cnt_dummy; // Only used to satisfy manual strobe sets.

    int irq_num;
    int irq_enabled;

    uint8_t status;
    pc_timer_t timer;

    float luma_thresh;
};

typedef struct pxl380_t pxl380_t;

static float
pxl380_sample_luma(bitmap_t* target_buffer, uint32_t x, uint32_t y)
{
    const float R_COEFF    = 0.3;
    const float G_COEFF    = 0.4;
    const float B_COEFF    = 0.7;
    float total_luma       = 0.0;
    
    if (!mouse_tablet_in_proximity || mouse_input_mode == 0)
        return 0.0; // simulate lightpen not being pointed at the screen.

    // Can't sample unrendered lines...
    for (int ky = -2; ky <= 0; ky++) {
        for (int kx = -2; kx <= 2; kx++) {
            uint32_t xx = (((int)x) + kx) & 2047;
            uint32_t yy = (((int)y) + ky) & 2047;

            float r = ((target_buffer->line[yy][xx] >> 16) & 0xFF) / 255.0;
            float g = ((target_buffer->line[yy][xx] >> 8) & 0xFF) / 255.0;
            float b = (target_buffer->line[yy][xx] & 0xFF) / 255.0;

            total_luma += (r * R_COEFF) + (g * G_COEFF) + (b * B_COEFF);
        }
    }

    return total_luma / 15.0;
}

void pxl380_lightpen_check_trigger_strobe(void* priv, int x_offset, int y, int x_offset_from_hsync, int firstline, double pix_clock, int monitor_used)
{
    pxl380_t* pxl380 = (pxl380_t*)priv;
    double abs_x, abs_y;

    if (!mouse_tablet_in_proximity || mouse_input_mode == 0)
        return;

    mouse_get_abs_coords(&abs_x, &abs_y);

    abs_x *= monitors[monitor_used].mon_unscaled_size_x - 1;
    abs_y *= monitors[monitor_used].mon_efscrnsz_y - 1;

    int x = abs_x + (!enable_overscan ? x_offset : 0);
    float sampled_luma = pxl380_sample_luma(monitors[monitor_used].target_buffer, x, y);
    int real_x = x; // This is the position from the actual beginning of the render.
    x -= x_offset;
    y -= firstline + monitors[monitor_used].mon_overscan_y / 2;
    
    if (enable_overscan) {
        y += monitors[monitor_used].mon_overscan_y / 2;
    }

    // Checking for x makes no sense.
    if (sampled_luma >= pxl380->luma_thresh && y == (int)abs_y) {
        double factor = 48000000. / pix_clock;
        int latch_val = (x_offset_from_hsync + real_x) * factor;

        pxl380->timer1_cnt_dummy  = latch_val & 0xfff;
        pxl380->timer1_cnt_latch  = latch_val & 0xfff;
        pxl380->timer0_cnt_latch  = pxl380->timer0_cnt & 0xfff;
        pxl380->status           |= 0x8;
        if (pxl380->irq_enabled)
            picint(1 << pxl380->irq_num);
    }
}

void pxl380_lightpen_hsync(void* priv)
{
    pxl380_t* pxl380 = (pxl380_t*)priv;

    pxl380->timer0_cnt++;
    pxl380->timer1_cnt_dummy = 0;
}

void pxl380_lightpen_vsync(void* priv)
{
    pxl380_t* pxl380 = (pxl380_t*)priv;

    pxl380->timer0_cnt = 0;

    if (!(pxl380->status & 1)) {
        pxl380->status |= 1;
        if (pxl380->irq_enabled)
            picint(1 << pxl380->irq_num);
    }
}

uint8_t
pxl380_read(uint16_t addr, void* priv)
{
    pxl380_t* pxl380 = (pxl380_t*)priv;
    uint8_t   ret = 0x00;
    switch (addr & 7) {
        case 0:
            ret = ~(pxl380->status | ((tablet_get_buttons_ex() & 1) << 2)) | ((tablet_get_buttons_ex() & 2) << 1);
            break;
        case 1:
            ret = ((pxl380->timer1_cnt_latch >> 8) & 0xf) | (((pxl380->timer0_cnt_latch >> 8) & 0xf) << 4);
            break;
        case 2:
            ret = pxl380->timer1_cnt_latch & 0xff;
            break;
        case 3:
            ret = pxl380->timer0_cnt_latch & 0xff;
            break;
        case 4:
        {
            pxl380->status &= ~0x8;
            break;
        }
        case 5:
        {
            if (!(pxl380->status & 0x8)) {
                pxl380->timer1_cnt_latch = pxl380->timer1_cnt_dummy & 0xfff;
                pxl380->timer0_cnt_latch = pxl380->timer0_cnt & 0xfff;
                pxl380->status |= 0x8;
                if (pxl380->irq_enabled)
                    picint(1 << pxl380->irq_num);
            }
            break;
        }
        case 6:
        {
            pxl380->irq_enabled = 0;
            break;
        }
        case 7:
        {
            pxl380->irq_enabled = 1;
            pxl380->status &= ~1;
            picintc(1 << pxl380->irq_num);
            break;
        }
        default:
            break;
    }

    return ret;
}

void
pxl380_timer1_cntr(void *priv)
{
    pxl380_t* pxl380 = (pxl380_t*)priv;

    pxl380->timer1_cnt_dummy++;
    timer_on_auto(&pxl380->timer, (1. / 48000000.) * 1000. * 1000.);
}

void* pxl380_init(const device_t* info)
{
    pxl380_t* pxl380 = (pxl380_t*)calloc(1, sizeof(pxl380_t));

    timer_add(&pxl380->timer, pxl380_timer1_cntr, pxl380, 0);
    timer_on_auto(&pxl380->timer, (1. / 48000000.) * 1000. * 1000.);
    io_sethandler(device_get_config_hex16("base"), 8, pxl380_read, NULL, NULL, NULL, NULL, NULL, pxl380);
    pxl380->irq_num = device_get_config_int("irq");

    video_lightpen_set_callbacks(pxl380, pxl380_lightpen_hsync, pxl380_lightpen_vsync, pxl380_lightpen_check_trigger_strobe);

    pxl380->luma_thresh = device_get_config_int("luma_thresh") / 100.;

    mouse_set_buttons(2);
    mouse_set_poll_ex(NULL, NULL);

    return pxl380;
}

void pxl380_close(void* priv)
{
    video_lightpen_set_callbacks(NULL, NULL, NULL, NULL);
    free(priv);
}

static const device_config_t pxl380_config[] = {
  // clang-format off
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 7,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "IRQ 3",  .value =  3 },
            { .description = "IRQ 4",  .value =  4 },
            { .description = "IRQ 5",  .value =  5 },
            { .description = "IRQ 7",  .value =  7 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x210,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x210", .value = 0x210 },
            { .description = "0x270", .value = 0x270 },
            { .description = "0x300", .value = 0x300 },
            { .description = "0x330", .value = 0x330 },
            { .description = "0x360", .value = 0x360 },
            { .description = "0x370", .value = 0x370 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "luma_thresh",
        .description    = "Luminance threshold (%)",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 12,
        .file_filter    = NULL,
        .spinner        = { .min = 0, .max = 100, .step = 1 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t mouse_pxl_380_device = {
    .name          = "PXL-380 Lightpen",
    .internal_name = "pxl_380",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = pxl380_init,
    .close         = pxl380_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pxl380_config
};

