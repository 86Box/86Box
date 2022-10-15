/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		DDC monitor emulation.
 *
 *
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <wchar.h>
#include <math.h>
#include <86box/86box.h>
#include <86box/i2c.h>

#define PIXEL_MM(px) ((uint16_t) (((px) *25.4) / 96))
#define STANDARD_TIMING(slot, width, aspect_ratio, refresh)                             \
    do {                                                                                \
        edid->slot.horiz_pixels              = ((width) >> 3) - 31;                     \
        edid->slot.aspect_ratio_refresh_rate = ((aspect_ratio) << 6) | ((refresh) -60); \
    } while (0)
#define DETAILED_TIMING(slot, clk, width, height, hblank, vblank, hfp, hsp, vfp, vsp)                                                             \
    do {                                                                                                                                          \
        edid->slot.pixel_clock_lsb               = ((clk) / 10) & 0xff;                                                                           \
        edid->slot.pixel_clock_msb               = ((clk) / 10) >> 8;                                                                             \
        edid->slot.h_active_lsb                  = (width) &0xff;                                                                                 \
        edid->slot.h_blank_lsb                   = (hblank) &0xff;                                                                                \
        edid->slot.h_active_blank_msb            = (((width) >> 4) & 0xf0) | (((hblank) >> 8) & 0x0f);                                            \
        edid->slot.v_active_lsb                  = (height) &0xff;                                                                                \
        edid->slot.v_blank_lsb                   = (vblank) &0xff;                                                                                \
        edid->slot.v_active_blank_msb            = (((height) >> 4) & 0xf0) | (((vblank) >> 8) & 0x0f);                                           \
        edid->slot.h_front_porch_lsb             = (hfp) &0xff;                                                                                   \
        edid->slot.h_sync_pulse_lsb              = (hsp) &0xff;                                                                                   \
        edid->slot.v_front_porch_sync_pulse_lsb  = (((vfp) &0x0f) << 4) | ((vsp) &0x0f);                                                          \
        edid->slot.hv_front_porch_sync_pulse_msb = (((hfp) >> 2) & 0xc0) | (((hsp) >> 4) & 0x30) | (((vfp) >> 2) & 0x0c) | (((vsp) >> 4) & 0x03); \
        edid->slot.h_size_lsb                    = horiz_mm & 0xff;                                                                               \
        edid->slot.v_size_lsb                    = vert_mm & 0xff;                                                                                \
        edid->slot.hv_size_msb                   = ((horiz_mm >> 4) & 0xf0) | ((vert_mm >> 8) & 0x0f);                                            \
    } while (0)

enum {
    STD_ASPECT_16_10 = 0x0,
    STD_ASPECT_4_3,
    STD_ASPECT_5_4,
    STD_ASPECT_16_9
};

typedef struct {
    uint8_t horiz_pixels, aspect_ratio_refresh_rate;
} edid_standard_timing_t;

typedef struct {
    uint8_t pixel_clock_lsb, pixel_clock_msb, h_active_lsb, h_blank_lsb,
        h_active_blank_msb, v_active_lsb, v_blank_lsb, v_active_blank_msb,
        h_front_porch_lsb, h_sync_pulse_lsb, v_front_porch_sync_pulse_lsb,
        hv_front_porch_sync_pulse_msb, h_size_lsb, v_size_lsb, hv_size_msb,
        h_border, v_border, features;
} edid_detailed_timing_t;

typedef struct {
    uint8_t magic[2], reserved, tag, range_limit_offsets;
    union {
        char ascii[13];
        struct {
            uint8_t min_v_field, max_v_field, min_h_line, max_h_line, max_pixel_clock,
                timing_type;
            union {
                uint8_t padding[7];
                struct {
                    uint8_t reserved, gtf_start_freq, gtf_c, gtf_m_lsb, gtf_m_msb,
                        gtf_k, gtf_j;
                };
                struct {
                    uint8_t cvt_version, add_clock_precision, max_active_pixels,
                        aspect_ratios, aspect_ratio_pref, scaling_support,
                        refresh_pref;
                };
            };
        } range_limits;
        struct {
            edid_standard_timing_t timings[6];
            uint8_t                padding;
        } ext_standard_timings;
        struct {
            uint8_t version;
            struct {
                uint8_t lines_lsb, lines_msb_aspect_ratio, refresh_rate;
            } timings[4];
        } cvt_timings;
        struct {
            uint8_t version, timings[6], reserved[6];
        } established_timings3;
    };
} edid_descriptor_t;

typedef struct {
    uint8_t magic[8], mfg[2], mfg_product[2], serial[4], mfg_week, mfg_year,
        edid_version, edid_rev;
    uint8_t input_params, horiz_size, vert_size, gamma, features;
    uint8_t red_green_lsb, blue_white_lsb, red_x_msb, red_y_msb, green_x_msb,
        green_y_msb, blue_x_msb, blue_y_msb, white_x_msb, white_y_msb;
    uint8_t                established_timings[3];
    edid_standard_timing_t standard_timings[8];
    union {
        edid_detailed_timing_t detailed_timings[4];
        edid_descriptor_t      descriptors[4];
    };
    uint8_t extensions, checksum;

    uint8_t ext_tag, ext_rev, ext_dtd_offset, ext_native_dtds;
    union {
        edid_detailed_timing_t ext_detailed_timings[6];
        edid_descriptor_t      ext_descriptors[6];
    };
    uint8_t padding[15], checksum2;
} edid_t;

void *
ddc_init(void *i2c)
{
    edid_t *edid = malloc(sizeof(edid_t));
    memset(edid, 0, sizeof(edid_t));

    uint8_t *edid_bytes = (uint8_t *) edid;
    uint16_t horiz_mm = PIXEL_MM(1366), vert_mm = PIXEL_MM(768);

    memset(&edid->magic[1], 0xff, sizeof(edid->magic) - 2);

    edid->mfg[0]       = 0x09; /* manufacturer "BOX" (apparently unassigned by UEFI) */
    edid->mfg[1]       = 0xf8;
    edid->mfg_week     = 48;
    edid->mfg_year     = 2020 - 1990;
    edid->edid_version = 0x01;
    edid->edid_rev     = 0x03; /* EDID 1.3 */

    edid->input_params = 0x0e; /* analog input; separate sync; composite sync; sync on green */
    edid->horiz_size   = horiz_mm / 10;
    edid->vert_size    = vert_mm / 10;
    edid->features     = 0xeb; /* DPMS standby/suspend/active-off; RGB color; first timing is preferred; GTF/CVT */

    edid->red_green_lsb  = 0x81;
    edid->blue_white_lsb = 0xf1;
    edid->red_x_msb      = 0xa3;
    edid->red_y_msb      = 0x57;
    edid->green_x_msb    = 0x53;
    edid->green_y_msb    = 0x9f;
    edid->blue_x_msb     = 0x27;
    edid->blue_y_msb     = 0x0a;
    edid->white_x_msb    = 0x50;
    edid->white_y_msb    = 0x00;

    memset(&edid->established_timings, 0xff, sizeof(edid->established_timings)); /* all enabled */

    /* 60 Hz timings */
    STANDARD_TIMING(standard_timings[0], 1280, STD_ASPECT_16_9, 60);  /* 1280x720 */
    STANDARD_TIMING(standard_timings[1], 1280, STD_ASPECT_16_10, 60); /* 1280x800 */
    STANDARD_TIMING(standard_timings[2], 1366, STD_ASPECT_16_9, 60);  /* 1360x768 (closest to 1366x768) */
    STANDARD_TIMING(standard_timings[3], 1440, STD_ASPECT_16_10, 60); /* 1440x900 */
    STANDARD_TIMING(standard_timings[4], 1600, STD_ASPECT_16_9, 60);  /* 1600x900 */
    STANDARD_TIMING(standard_timings[5], 1680, STD_ASPECT_16_10, 60); /* 1680x1050 */
    STANDARD_TIMING(standard_timings[6], 1920, STD_ASPECT_16_9, 60);  /* 1920x1080 */
    STANDARD_TIMING(standard_timings[7], 2048, STD_ASPECT_4_3, 60);   /* 2048x1536 */

    /* Detailed timing for the preferred mode of 800x600 @ 60 Hz */
    DETAILED_TIMING(detailed_timings[0], 40000, 800, 600, 256, 28, 40, 128, 1, 4);

    edid->descriptors[1].tag                          = 0xf7; /* established timings 3 */
    edid->descriptors[1].established_timings3.version = 0x0a;
    memset(&edid->descriptors[1].established_timings3.timings, 0xff, sizeof(edid->descriptors[1].established_timings3.timings)); /* all enabled */
    edid->descriptors[1].established_timings3.timings[5] &= 0xf0;                                                                /* reserved bits */

    edid->descriptors[2].tag                          = 0xfd; /* range limits */
    edid->descriptors[2].range_limits.min_v_field     = 45;
    edid->descriptors[2].range_limits.max_v_field     = 125;
    edid->descriptors[2].range_limits.min_h_line      = 30;   /* 640x480 = ~31.5 KHz */
    edid->descriptors[2].range_limits.max_h_line      = 115;  /* 1920x1440 = 112.5 KHz */
    edid->descriptors[2].range_limits.max_pixel_clock = 30;   /* 1920x1440 = 297 MHz */
    edid->descriptors[2].range_limits.timing_type     = 0x00; /* default GTF */
    edid->descriptors[2].range_limits.padding[0]      = 0x0a;
    memset(&edid->descriptors[2].range_limits.padding[1], 0x20, sizeof(edid->descriptors[2].range_limits.padding) - 1);

    edid->descriptors[3].tag = 0xfc;                          /* display name */
    memcpy(&edid->descriptors[3].ascii, "86Box Monitor", 13); /* exactly 13 characters (would otherwise require LF termination and space padding) */

    edid->extensions = 1;
    for (uint8_t c = 0; c < 127; c++)
        edid->checksum += edid_bytes[c];
    edid->checksum = 256 - edid->checksum;

    edid->ext_tag         = 0x02;
    edid->ext_rev         = 0x03;
    edid->ext_native_dtds = 0x80; /* underscans IT; no native extended modes */
    edid->ext_dtd_offset  = 0x04;

    /* Detailed timing for 1366x768 */
    DETAILED_TIMING(ext_detailed_timings[0], 85500, 1366, 768, 426, 30, 70, 143, 3, 3);

    /* High refresh rate timings (VGA is limited to 85 Hz) */
    edid->ext_descriptors[1].tag = 0xfa; /* standard timing identifiers */
#define ext_standard_timings0 ext_descriptors[1].ext_standard_timings.timings
    STANDARD_TIMING(ext_standard_timings0[0], 640, STD_ASPECT_4_3, 90);  /* 640x480 @ 90 Hz */
    STANDARD_TIMING(ext_standard_timings0[1], 640, STD_ASPECT_4_3, 120); /* 640x480 @ 120 Hz */
    STANDARD_TIMING(ext_standard_timings0[2], 800, STD_ASPECT_4_3, 90);  /* 800x600 @ 90 Hz */
    STANDARD_TIMING(ext_standard_timings0[3], 800, STD_ASPECT_4_3, 120); /* 800x600 @ 120 Hz */
    STANDARD_TIMING(ext_standard_timings0[4], 1024, STD_ASPECT_4_3, 90); /* 1024x768 @ 90 Hz */
    STANDARD_TIMING(ext_standard_timings0[5], 1280, STD_ASPECT_5_4, 90); /* 1280x1024 @ 90 Hz */
    edid->ext_descriptors[1].ext_standard_timings.padding = 0x0a;

    for (uint8_t c = 128; c < 255; c++)
        edid->checksum2 += edid_bytes[c];
    edid->checksum2 = 256 - edid->checksum2;

    return i2c_eeprom_init(i2c, 0x50, edid_bytes, sizeof(edid_t), 0);
}

void
ddc_close(void *eeprom)
{
    i2c_eeprom_close(eeprom);
}
