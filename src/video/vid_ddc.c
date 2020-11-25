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


#define STD_TIMING(idx, width, aspect_ratio)	do { \
							edid->standard_timings[idx].horiz_pixels = ((width) / 8) - 31; \
							edid->standard_timings[idx].aspect_ratio_refresh_rate = (aspect_ratio) << 6; /* 60 Hz */ \
						} while (0)

enum {
    STD_ASPECT_16_10 = 0x0,
    STD_ASPECT_4_3,
    STD_ASPECT_5_4,
    STD_ASPECT_16_9
};

typedef struct {
    uint8_t	horiz_pixels, aspect_ratio_refresh_rate;
} edid_standard_timing_t;

typedef struct {
    uint8_t	pixel_clock_lsb, pixel_clock_msb, h_active_lsb, h_blank_lsb,
		h_active_blank_msb, v_active_lsb, v_blank_lsb, v_active_blank_msb,
		h_front_porch_lsb, h_sync_pulse_lsb, v_front_porch_sync_pulse_lsb,
		hv_front_porch_sync_pulse_msb, h_size_lsb, v_size_lsb, hv_size_msb,
		h_border, v_border, features;
} edid_detailed_timing_t;

typedef struct {
    uint8_t	min_v_field, max_v_field, min_h_line, max_h_line, max_pixel_clock,
		timing_type;
    union {
	uint8_t	padding[7];
	struct {
		uint8_t	reserved, gtf_start_freq, gtf_c, gtf_m_lsb, gtf_m_msb,
			gtf_k, gtf_j;
	};
	struct {
		uint8_t	cvt_version, add_clock_precision, max_active_pixels,
			aspect_ratios, aspect_ratio_pref, scaling_support,
			refresh_pref;
	};
    };
} edid_range_limits_t;

typedef struct {
    uint8_t	version, timings[6], reserved[6];
} edid_established_timings3_t;

typedef struct {
    uint8_t	version;
    struct {
	uint8_t	lines_lsb, lines_msb_aspect_ratio, refresh_rate;
    } timings[4];
} edid_cvt_timings_t;

typedef struct {
    uint8_t	magic[2], reserved, tag, range_limit_offsets;
    union {
	char	ascii[13];
	edid_range_limits_t range_limits;
	edid_established_timings3_t established_timings3;
	edid_cvt_timings_t cvt_timings;
    };
} edid_descriptor_t;

typedef struct {
    uint8_t	magic[8], mfg[2], mfg_product[2], serial[4], mfg_week, mfg_year,
		edid_version, edid_rev;
    uint8_t	input_params, horiz_size, vert_size, gamma, features;
    uint8_t	red_green_lsb, blue_white_lsb, red_x_msb, red_y_msb, green_x_msb,
		green_y_msb, blue_x_msb, blue_y_msb, white_x_msb, white_y_msb;
    uint8_t	established_timings[3];
    edid_standard_timing_t standard_timings[8];
    union {
	edid_detailed_timing_t detailed_timings[4];
	edid_descriptor_t descriptors[4];
    };
    uint8_t	extensions, checksum;

    uint8_t	ext_tag, ext_rev, ext_dtd_offset, ext_native_dtds;
    union {
	edid_detailed_timing_t ext_detailed_timings[6];
	edid_descriptor_t ext_descriptors[6];
    };
    uint8_t	padding[15], checksum2;
} edid_t;


void *
ddc_init(void *i2c)
{
    edid_t *edid = malloc(sizeof(edid_t));
    memset(edid, 0, sizeof(edid_t));

    memset(&edid->magic[1], 0xff, sizeof(edid->magic) - 2);

    edid->mfg[0] = 0x09; /* manufacturer "BOX" (apparently unassigned by UEFI) */
    edid->mfg[1] = 0xf8;
    edid->mfg_week = 48;
    edid->mfg_year = 2020 - 1990;
    edid->edid_version = 0x01;
    edid->edid_rev = 0x04; /* EDID 1.4 */

    edid->input_params = 0x0e; /* analog input; separate sync; composite sync; sync on green */
    edid->horiz_size = ((4.0 / 3.0) * 100) - 99; /* landscape 4:3 */
    edid->features = 0x09; /* RGB color; GTF/CVT */

    edid->red_green_lsb = 0x81;
    edid->blue_white_lsb = 0xf1;
    edid->red_x_msb = 0xa3;
    edid->red_y_msb = 0x57;
    edid->green_x_msb = 0x53;
    edid->green_y_msb = 0x9f;
    edid->blue_x_msb = 0x27;
    edid->blue_y_msb = 0x0a;
    edid->white_x_msb = 0x50;
    edid->white_y_msb = 0x00;

    memset(&edid->established_timings, 0xff, sizeof(edid->established_timings)); /* all enabled */

    memset(&edid->standard_timings, 0x01, sizeof(edid->standard_timings)); /* pad unused entries */
    STD_TIMING(0, 1280, STD_ASPECT_16_9);  /* 1280x720 */
    STD_TIMING(1, 1280, STD_ASPECT_16_10); /* 1280x800 */
    STD_TIMING(2, 1366, STD_ASPECT_16_9);  /* 1360x768 (closest to 1366x768) */
    STD_TIMING(3, 1440, STD_ASPECT_16_10); /* 1440x900 */
    STD_TIMING(4, 1600, STD_ASPECT_16_9);  /* 1600x900 */
    STD_TIMING(5, 1680, STD_ASPECT_16_10); /* 1680x1050 */
    STD_TIMING(6, 1920, STD_ASPECT_16_9);  /* 1920x1080 */
    STD_TIMING(7, 2048, STD_ASPECT_4_3);   /* 2048x1536 */

    /* Detailed timings for the preferred mode of 800x600 */
    edid->detailed_timings[0].pixel_clock_lsb = 0xa0; /* 40000 KHz */
    edid->detailed_timings[0].pixel_clock_msb = 0x0f;
    edid->detailed_timings[0].h_active_lsb = 800 & 0xff;
    edid->detailed_timings[0].h_blank_lsb = 256 & 0xff;
    edid->detailed_timings[0].h_active_blank_msb = ((800 >> 4) & 0xf0) | ((256 >> 8) & 0x0f);
    edid->detailed_timings[0].v_active_lsb = 600 & 0xff;
    edid->detailed_timings[0].v_blank_lsb = 28;
    edid->detailed_timings[0].v_active_blank_msb = (600 >> 4) & 0xf0;
    edid->detailed_timings[0].h_front_porch_lsb = 40;
    edid->detailed_timings[0].h_sync_pulse_lsb = 128;
    edid->detailed_timings[0].v_front_porch_sync_pulse_lsb = (1 << 4) | 4;

    edid->descriptors[1].tag = 0xf7; /* established timings 3 */
    edid->descriptors[1].established_timings3.version = 0x0a;
    memset(&edid->descriptors[1].established_timings3.timings, 0xff, sizeof(edid->descriptors[1].established_timings3.timings)); /* all enabled */

    edid->descriptors[2].tag = 0xfc; /* display name */
    memcpy(&edid->descriptors[2].ascii, "86Box Monitor", 13); /* exactly 13 characters (would otherwise require LF termination and space padding) */

    edid->descriptors[3].tag = 0xfd; /* range limits */
    edid->descriptors[3].range_limits.min_v_field = 1;
    edid->descriptors[3].range_limits.max_v_field = -1;
    edid->descriptors[3].range_limits.min_h_line = 1;
    edid->descriptors[3].range_limits.max_h_line = -1;
    edid->descriptors[3].range_limits.max_pixel_clock = -1;
    edid->descriptors[3].range_limits.timing_type = 0x00; /* default GTF */
    edid->descriptors[3].range_limits.padding[0] = 0x0a;
    memset(&edid->descriptors[3].range_limits.padding[1], 0x20, sizeof(edid->descriptors[3].range_limits.padding) - 1);

    uint8_t *edid_bytes = (uint8_t *) edid;
    for (uint8_t c = 0; c < 127; c++)
        edid->checksum += edid_bytes[c];
    edid->checksum = 256 - edid->checksum;
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
