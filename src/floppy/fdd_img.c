/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the raw sector-based floppy image format,
 *		as well as the Japanese FDI, CopyQM, and FDF formats.
 *
 * NOTE:	This file is still a disaster, needs to be cleaned up and
 *		re-merged with the other files. Much of it is generic to
 *		all formats.
 *
 * Version:	@(#)fdd_img.c	1.0.10	2018/12/05
 *
 * Authors:	Sarah Walker, <tommowalker@tommowalker.co.uk>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2018,2019 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../timer.h"
#include "../config.h"
#include "../plat.h"
#include "fdd.h"
#include "fdd_86f.h"
#include "fdd_img.h"
#include "fdc.h"


typedef struct {
    FILE	*f;
    uint8_t	track_data[2][50000];
    int		sectors, tracks, sides;
    uint8_t	sector_size;
    int		xdf_type;  /* 0 = not XDF, 1-5 = one of the five XDF types */
    int		dmf;
    int		track;
    int		track_width;
    uint32_t	base;
    uint8_t	gap2_size;
    uint8_t	gap3_size;
    uint16_t	disk_flags;
    uint16_t	track_flags;
    uint8_t	sector_pos_side[2][256];
    uint16_t	sector_pos[2][256];
    uint8_t	current_sector_pos_side;
    uint16_t	current_sector_pos;
    uint8_t	*disk_data;
    uint8_t	is_cqm;
    uint8_t	disk_at_once;
    uint8_t	interleave;
    uint8_t	skew;
} img_t;


static img_t	*img[FDD_NUM];
static fdc_t	*img_fdc;

static double	bit_rate_300;
static wchar_t	*ext;
static uint8_t	first_byte,
		second_byte,
		third_byte,
		fourth_byte;
static uint8_t	fdf_suppress_final_byte = 0;	/* This is hard-coded to 0 -
						 * if you really need to read
						 * those NT 3.1 Beta floppy
						 * images, change this to 1
						 * and recompile.
						 */


const uint8_t dmf_r[21] = { 12, 2, 13, 3, 14, 4, 15, 5, 16, 6, 17, 7, 18, 8, 19, 9, 20, 10, 21, 11, 1 };
static const uint8_t xdf_logical_sectors[2][2] = { { 38, 6 }, { 46, 8 } };
const uint8_t xdf_physical_sectors[2][2] = { { 16, 3 }, { 19, 4 } };
const uint8_t xdf_gap3_sizes[2][2] = { { 60, 69 }, { 60, 50 } };
const uint16_t xdf_trackx_spos[2][8] = { { 0xA7F, 0xF02, 0x11B7, 0xB66, 0xE1B, 0x129E }, { 0x302, 0x7E2, 0xA52, 0x12DA, 0x572, 0xDFA, 0x106A, 0x154A } };

/* XDF: Layout of the sectors in the image. */
const xdf_sector_t xdf_img_layout[2][2][46] = {	{	{ {0x8100}, {0x8200}, {0x8300}, {0x8400}, {0x8500}, {0x8600}, {0x8700}, {0x8800},
							  {0x8101}, {0x8201}, {0x0100}, {0x0200}, {0x0300}, {0x0400}, {0x0500}, {0x0600},
							  {0x0700}, {0x0800}, {     0},
							  {0x8301}, {0x8401}, {0x8501}, {0x8601}, {0x8701}, {0x8801}, {0x8901}, {0x8A01},
							  {0x8B01}, {0x8C01}, {0x8D01}, {0x8E01}, {0x8F01}, {0x9001}, {     0}, {     0},
							  {     0}, {     0}, {     0} },
							{ {0x8300}, {0x8600}, {0x8201}, {0x8200}, {0x8601}, {0x8301} }
						},			/* 5.25" 2HD */
						{	{ {0x8100}, {0x8200}, {0x8300}, {0x8400}, {0x8500}, {0x8600}, {0x8700}, {0x8800},
							  {0x8900}, {0x8A00}, {0x8B00}, {0x8101}, {0x0100}, {0x0200}, {0x0300}, {0x0400},
							  {0x0500}, {0x0600}, {0x0700}, {0x0800}, {     0}, {     0}, {     0},
							  {0x8201}, {0x8301}, {0x8401}, {0x8501}, {0x8601}, {0x8701}, {0x8801}, {0x8901},
							  {0x8A01}, {0x8B01}, {0x8C01}, {0x8D01}, {0x8E01}, {0x8F01}, {     0}, {     0},
							  {     0}, {     0}, {     0}, {0x9001}, {0x9101}, {0x9201}, {0x9301} },
							{ {0x8300}, {0x8400}, {0x8601}, {0x8200}, {0x8201}, {0x8600}, {0x8401}, {0x8301} }
						}	/* 3.5" 2HD */
					   };

/* XDF: Layout of the sectors on the disk's track. */
const xdf_sector_t xdf_disk_layout[2][2][38] = {	{	{ {0x0100}, {0x0200}, {0x8100}, {0x8800}, {0x8200}, {0x0300}, {0x8300}, {0x0400},
							  {0x8400}, {0x0500}, {0x8500}, {0x0600}, {0x8600}, {0x0700}, {0x8700}, {0x0800},
							  {0x8D01}, {0x8501}, {0x8E01}, {0x8601}, {0x8F01}, {0x8701}, {0x9001}, {0x8801},
							  {0x8101}, {0x8901}, {0x8201}, {0x8A01}, {0x8301}, {0x8B01}, {0x8401}, {0x8C01} },
							{ {0x8300}, {0x8200}, {0x8600}, {0x8201}, {0x8301}, {0x8601} }
						},			/* 5.25" 2HD */
						{	{ {0x0100}, {0x8A00}, {0x8100}, {0x8B00}, {0x8200}, {0x0200}, {0x8300}, {0x0300},
							  {0x8400}, {0x0400}, {0x8500}, {0x0500}, {0x8600}, {0x0600}, {0x8700}, {0x0700},
							  {0x8800}, {0x0800}, {0x8900},
							  {0x9001}, {0x8701}, {0x9101}, {0x8801}, {0x9201}, {0x8901}, {0x9301}, {0x8A01},
							  {0x8101}, {0x8B01}, {0x8201}, {0x8C01}, {0x8301}, {0x8D01}, {0x8401}, {0x8E01},
							  {0x8501}, {0x8F01}, {0x8601} },
							{ {0x8300}, {0x8200}, {0x8400}, {0x8600}, {0x8401}, {0x8201}, {0x8301}, {0x8601} },
						},			/* 3.5" 2HD */
					   };

/* First dimension is possible sector sizes (0 = 128, 7 = 16384), second is possible bit rates (250/360, 250, 300, 500/360, 500, 1000). */
/* Disks formatted at 250 kbps @ 360 RPM can be read with a 360 RPM single-RPM 5.25" drive by setting the rate to 250 kbps.
   Disks formatted at 300 kbps @ 300 RPM can be read with any 300 RPM single-RPM drive by setting the rate rate to 300 kbps. */
static const uint8_t maximum_sectors[8][6] = { { 26, 31, 38, 53, 64, 118 },	/*   128 */
					 { 15, 19, 23, 32, 38,  73 },	/*   256 */
					 {  7, 10, 12, 17, 22,  41 },	/*   512 */
					 {  3,  5,  6,  9, 11,  22 },	/*  1024 */
					 {  2,  2,  3,  4,  5,  11 },	/*  2048 */
					 {  1,  1,  1,  2,  2,   5 },	/*  4096 */
					 {  0,  0,  0,  1,  1,   3 },	/*  8192 */
					 {  0,  0,  0,  0,  0,   1 } };	/* 16384 */

static const uint8_t xdf_sectors[8][6] = { {  0,  0,  0,  0,  0,   0 },	/*   128 */
					 {  0,  0,  0,  0,  0,   0 },	/*   256 */
					 {  0,  0,  0, 19, 23,   0 },	/*   512 */
					 {  0,  0,  0,  0,  0,   0 },	/*  1024 */
					 {  0,  0,  0,  0,  0,   0 },	/*  2048 */
					 {  0,  0,  0,  0,  0,   0 },	/*  4096 */
					 {  0,  0,  0,  0,  0,   0 },	/*  8192 */
					 {  0,  0,  0,  0,  0,   0 } };	/* 16384 */

static const uint8_t xdf_types[8][6]   = { {  0,  0,  0,  0,  0,   0 },	/*   128 */
					 {  0,  0,  0,  0,  0,   0 },	/*   256 */
					 {  0,  0,  0,  1,  2,   0 },	/*   512 */
					 {  0,  0,  0,  0,  0,   0 },	/*  1024 */
					 {  0,  0,  0,  0,  0,   0 },	/*  2048 */
					 {  0,  0,  0,  0,  0,   0 },	/*  4096 */
					 {  0,  0,  0,  0,  0,   0 },	/*  8192 */
					 {  0,  0,  0,  0,  0,   0 } };	/* 16384 */

static const double bit_rates_300[6]   = { (250.0 * 300.0) / 360.0, 250.0, 300.0, (500.0 * 300.0) / 360.0, 500.0, 1000.0 };

static const uint8_t rates[6]	  = { 2, 2, 1, 4, 0, 3 };

static const uint8_t holes[6]	      = { 0, 0, 0, 1, 1, 2 };

const int gap3_sizes[5][8][48] = {	{	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [0][0] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [0][1] */
					  0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [0][2] */
					  0x00, 0x00, 0x6C, 0x48, 0x2A, 0x08, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x83, 0x26, 0x00, 0x00, 0x00, 0x00,	/* [0][3] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [0][4] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [0][5] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [0][6] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [0][7] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
				{	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [1][0] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [1][1] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x54, 0x1C, 0x0E, 0x00, 0x00,	/* [1][2] */
					  0x00, 0x00, 0x6C, 0x48, 0x2A, 0x08, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [1][3] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [1][4] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [1][5] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [1][6] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [1][7] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
				{	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [2][0] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32, 0x0C, 0x00, 0x00, 0x00, 0x36,	/* [2][1] */
					  0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0x50, 0x2E, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [2][2] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0xF0, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [2][3] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [2][4] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [2][5] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [2][6] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [2][7] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
				{	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [3][0] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [3][1] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [3][2] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x53, 0x4E, 0x3D, 0x2C, 0x1C, 0x0D, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [3][3] */
					  0x00, 0x00, 0xF7, 0xAF, 0x6F, 0x55, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [3][4] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [3][5] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [3][6] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [3][7] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
				{	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [4][0] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [4][1] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x92, 0x54,	/* [4][2] */
					  0x38, 0x23, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x74, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [4][3] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [4][4] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [4][5] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [4][6] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
					{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [4][7] */
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } } };


#ifdef ENABLE_IMG_LOG
int img_do_log = ENABLE_IMG_LOG;


static void
img_log(const char *fmt, ...)
{
   va_list ap;

   if (img_do_log)
   {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
   }
}
#else
#define img_log(fmt, ...)
#endif


/* Generic */
static int
sector_size_code(int sector_size)
{
    switch(sector_size) {
	case 128:
		return(0);

	case 256:
		return(1);

	default:
	case 512:
		return(2);

	case 1024:
		return(3);

	case 2048:
		return(4);

	case 4096:
		return(5);

	case 8192:
		return(6);

	case 16384:
		return(7);
    }
}


static int
bps_is_valid(uint16_t bps)
{
    int i;

    for (i = 0; i <= 8; i++) {
	if (bps == (128 << i)) return(1);
    }

    return(0);
}


static int
first_byte_is_valid(uint8_t first_byte)
{
    switch(first_byte) {
	case 0x60:
	case 0xE9:
	case 0xEB:
		return(1);

	default:
		break;
    }

    return(0);
}


#define xdf_img_sector xdf_img_layout[current_xdft][!is_t0][sector]
#define xdf_disk_sector xdf_disk_layout[current_xdft][!is_t0][array_sector]


static int
interleave(int sector, int skew, int track_spt)
{
    uint32_t skewed_i;
    uint32_t adjusted_r;
    uint32_t add = (track_spt & 1);
    uint32_t adjust = (track_spt >> 1);

    skewed_i = (sector + skew) % track_spt;
    adjusted_r = (skewed_i >> 1) + 1;
    if (skewed_i & 1)
	adjusted_r += (adjust + add);

    return(adjusted_r);
}


static void
write_back(int drive)
{
    img_t *dev = img[drive];
    int ssize = 128 << ((int) dev->sector_size);
    int side;

    if (dev->f == NULL) return;

    if (dev->disk_at_once) return;
		
    fseek(dev->f, dev->base + (dev->track * dev->sectors * ssize * dev->sides), SEEK_SET);
    for (side = 0; side < dev->sides; side++)
	fwrite(dev->track_data[side], dev->sectors * ssize, 1, dev->f);
}


static uint16_t
disk_flags(int drive)
{
    img_t *dev = img[drive];

    return(dev->disk_flags);
}


static uint16_t
side_flags(int drive)
{
    img_t *dev = img[drive];

    return(dev->track_flags);
}


static void
set_sector(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n)
{
    img_t *dev = img[drive];

    dev->current_sector_pos_side = dev->sector_pos_side[h][r];
    dev->current_sector_pos = dev->sector_pos[h][r];
}


static uint8_t
poll_read_data(int drive, int side, uint16_t pos)
{
    img_t *dev = img[drive];

    return(dev->track_data[dev->current_sector_pos_side][dev->current_sector_pos + pos]);
}


static void
poll_write_data(int drive, int side, uint16_t pos, uint8_t data)
{
    img_t *dev = img[drive];

    dev->track_data[dev->current_sector_pos_side][dev->current_sector_pos + pos] = data;
}


static int
format_conditions(int drive)
{
    img_t *dev = img[drive];
    int temp = (fdc_get_format_sectors(img_fdc) == dev->sectors);

    temp = temp && (fdc_get_format_n(img_fdc) == dev->sector_size);
    temp = temp && (dev->xdf_type == 0);

    return(temp);
}


static void
img_seek(int drive, int track)
{
    img_t *dev = img[drive];
    int side;
    int current_xdft = dev->xdf_type - 1;
    int read_bytes = 0;
    uint8_t id[4] = { 0, 0, 0, 0 };
    int is_t0, sector, current_pos, img_pos, sr, sside, total, array_sector, buf_side, buf_pos;
    int ssize = 128 << ((int) dev->sector_size);
    uint32_t cur_pos = 0;

    if (dev->f == NULL) return;

    if (!dev->track_width && fdd_doublestep_40(drive))
	track /= 2;

    dev->track = track;
    d86f_set_cur_track(drive, track);

    is_t0 = (track == 0) ? 1 : 0;

    if (! dev->disk_at_once)
	fseek(dev->f, dev->base + (track * dev->sectors * ssize * dev->sides), SEEK_SET);

    for (side = 0; side < dev->sides; side++) {
	if (dev->disk_at_once) {
		cur_pos = (track * dev->sectors * ssize * dev->sides) + (side * dev->sectors * ssize);
		memcpy(dev->track_data[side], dev->disk_data + cur_pos, dev->sectors * ssize);
	} else {
		read_bytes = fread(dev->track_data[side], 1, dev->sectors * ssize, dev->f);
		if (read_bytes < (dev->sectors * ssize))
			memset(dev->track_data[side] + read_bytes, 0xf6, (dev->sectors * ssize) - read_bytes);
	}
    }

    d86f_reset_index_hole_pos(drive, 0);
    d86f_reset_index_hole_pos(drive, 1);

    d86f_destroy_linked_lists(drive, 0);
    d86f_destroy_linked_lists(drive, 1);

    if (track > dev->tracks) {
	d86f_zero_track(drive);
	return;
    }

    if (!dev->xdf_type || dev->is_cqm) {
	for (side = 0; side < dev->sides; side++) {
		current_pos = d86f_prepare_pretrack(drive, side, 0);

		for (sector = 0; sector < dev->sectors; sector++) {
			if (dev->is_cqm) {
				if (dev->interleave)
					sr = interleave(sector, dev->skew, dev->sectors);
				else {
					sr = sector + 1;
					sr += dev->skew;
					if (sr > dev->sectors)
						sr -= dev->sectors;
				}
			} else {
				if (dev->gap3_size < 68)
					sr = interleave(sector, 1, dev->sectors);
				else
					sr = dev->dmf ? (dmf_r[sector]) : (sector + 1);
			}
			id[0] = track;
			id[1] = side;
			id[2] = sr;
			id[3] = dev->sector_size;
			dev->sector_pos_side[side][sr] = side;
			dev->sector_pos[side][sr] = (sr - 1) * ssize;
			current_pos = d86f_prepare_sector(drive, side, current_pos, id, &dev->track_data[side][(sr - 1) * ssize], ssize, dev->gap2_size, dev->gap3_size, 0);

			if (sector == 0)
				d86f_initialize_last_sector_id(drive, id[0], id[1], id[2], id[3]);
		}
	}
    } else {
	total = dev->sectors;
	img_pos = 0;
	sside = 0;

	/* Pass 1, get sector positions in the image. */
	for (sector = 0; sector < xdf_logical_sectors[current_xdft][!is_t0]; sector++) {
		if (is_t0) {
			img_pos = (sector % total) << 9;
			sside = (sector >= total) ? 1 : 0;
		}

		if (xdf_img_sector.word) {
			dev->sector_pos_side[xdf_img_sector.id.h][xdf_img_sector.id.r] = sside;
			dev->sector_pos[xdf_img_sector.id.h][xdf_img_sector.id.r] = img_pos;
		}

		if (! is_t0) {
			img_pos += (128 << (xdf_img_sector.id.r & 7));
			if (img_pos >= (total << 9))  sside = 1;
			img_pos %= (total << 9);
		}
	}

	/* Pass 2, prepare the actual track. */
	for (side = 0; side < dev->sides; side++) {
		current_pos = d86f_prepare_pretrack(drive, side, 0);

		for (sector = 0; sector < xdf_physical_sectors[current_xdft][!is_t0]; sector++) {
			array_sector = (side * xdf_physical_sectors[current_xdft][!is_t0]) + sector;
			buf_side = dev->sector_pos_side[xdf_disk_sector.id.h][xdf_disk_sector.id.r];
			buf_pos = dev->sector_pos[xdf_disk_sector.id.h][xdf_disk_sector.id.r];

			id[0] = track;
			id[1] = xdf_disk_sector.id.h;
			id[2] = xdf_disk_sector.id.r;

			if (is_t0) {
				id[3] = 2;
				current_pos = d86f_prepare_sector(drive, side, current_pos, id, &dev->track_data[buf_side][buf_pos], ssize, dev->gap2_size, xdf_gap3_sizes[current_xdft][!is_t0], 0);
			} else {
				id[3] = id[2] & 7;
				ssize = (128 << id[3]);
				current_pos = d86f_prepare_sector(drive, side, xdf_trackx_spos[current_xdft][array_sector], id, &dev->track_data[buf_side][buf_pos], ssize, dev->gap2_size, xdf_gap3_sizes[current_xdft][!is_t0], 0);
			}

			if (sector == 0)
				d86f_initialize_last_sector_id(drive, id[0], id[1], id[2], id[3]);
		}
	}
    }
}


void
img_init(void)
{
    memset(img, 0x00, sizeof(img));
}


int
is_divisible(uint16_t total, uint8_t what)
{
    if ((total != 0) && (what != 0))
	return ((total % what) == 0);
    else
	return 0;
}


void
img_load(int drive, wchar_t *fn)
{
    uint16_t bpb_bps;
    uint16_t bpb_total;
    uint8_t bpb_mid;	/* Media type ID. */
    uint8_t bpb_sectors;
    uint8_t bpb_sides;
    uint8_t cqm, ddi, fdf, fdi;
    uint16_t comment_len = 0;
    int16_t block_len = 0;
    uint32_t cur_pos = 0;
    uint8_t rep_byte = 0;
    uint8_t run = 0;
    uint8_t real_run = 0;
    uint8_t *bpos;
    uint16_t track_bytes = 0;
    uint8_t *literal;
    img_t *dev;
    int temp_rate;
    int guess = 0;
    int size;
    int i;

    ext = plat_get_extension(fn);

    d86f_unregister(drive);

    writeprot[drive] = 0;

    /* Allocate a drive block. */
    dev = (img_t *)malloc(sizeof(img_t));
    memset(dev, 0x00, sizeof(img_t));

    dev->f = plat_fopen(fn, L"rb+");
    if (dev->f == NULL) {
	dev->f = plat_fopen(fn, L"rb");
	if (dev->f == NULL) {
		free(dev);
		memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
		return;
	}
	writeprot[drive] = 1;
    }

    if (ui_writeprot[drive])
		writeprot[drive] = 1;
    fwriteprot[drive] = writeprot[drive];

    cqm = ddi = fdf = fdi = 0;

    dev->interleave = dev->skew = 0;

    if (! wcscasecmp(ext, L"DDI")) {
	ddi = 1;
	dev->base = 0x2400;
    } else
	dev->base = 0;

    if (! wcscasecmp(ext, L"FDI")) {
	/* This is a Japanese FDI image, so let's read the header */
	img_log("img_load(): File is a Japanese FDI image...\n");
	fseek(dev->f, 0x10, SEEK_SET);
	(void)fread(&bpb_bps, 1, 2, dev->f);
	fseek(dev->f, 0x0C, SEEK_SET);
	(void)fread(&size, 1, 4, dev->f);
	bpb_total = size / bpb_bps;
	fseek(dev->f, 0x08, SEEK_SET);
	(void)fread(&(dev->base), 1, 4, dev->f);
	fseek(dev->f, dev->base + 0x15, SEEK_SET);
	bpb_mid = fgetc(dev->f);
	if (bpb_mid < 0xF0)
		bpb_mid = 0xF0;
	fseek(dev->f, 0x14, SEEK_SET);
	bpb_sectors = fgetc(dev->f);
	fseek(dev->f, 0x18, SEEK_SET);
	bpb_sides = fgetc(dev->f);
	fseek(dev->f, dev->base, SEEK_SET);
	first_byte = fgetc(dev->f);

	fdi = 1;
	cqm = 0;
	dev->disk_at_once = 0;
	fdf = 0;
    } else {
	/* Read the first four bytes. */
	fseek(dev->f, 0x00, SEEK_SET);
	first_byte = fgetc(dev->f);
	fseek(dev->f, 0x01, SEEK_SET);
	second_byte = fgetc(dev->f);
	fseek(dev->f, 0x02, SEEK_SET);
	third_byte = fgetc(dev->f);
	fseek(dev->f, 0x03, SEEK_SET);
	fourth_byte = fgetc(dev->f);

	if ((first_byte == 0x1A) && (second_byte == 'F') &&
	    (third_byte == 'D') && (fourth_byte == 'F')) {
		/* This is a FDF image. */
		img_log("img_load(): File is a FDF image...\n");
		fwriteprot[drive] = writeprot[drive] = 1;
		fclose(dev->f);
		dev->f = plat_fopen(fn, L"rb");

		fdf = 1;
		cqm = 0;
		dev->disk_at_once = 1;

		fseek(dev->f, 0x50, SEEK_SET);
		(void)fread(&dev->tracks, 1, 4, dev->f);

		/* Decode the entire file - pass 1, no write to buffer, determine length. */
		fseek(dev->f, 0x80, SEEK_SET);
		size = 0;
		track_bytes = 0;
		bpos = dev->disk_data;
		while (! feof(dev->f)) {
			if (! track_bytes) {
				/* Skip first 3 bytes - their meaning is unknown to us but could be a checksum. */
				first_byte = fgetc(dev->f);
				fread(&track_bytes, 1, 2, dev->f);
				img_log("Block header: %02X %04X ", first_byte, track_bytes);
				/* Read the length of encoded data block. */
				fread(&track_bytes, 1, 2, dev->f);
				img_log("%04X\n", track_bytes);
			}

			if (feof(dev->f)) break;

			if (first_byte == 0xFF) break;

			if (first_byte) {
				run = fgetc(dev->f);

				/* I *HAVE* to read something because fseek tries to be smart and never hits EOF, causing an infinite loop. */
				track_bytes--;

				if (run & 0x80) {
					/* Repeat. */
					track_bytes--;
					rep_byte = fgetc(dev->f);
				} else {
					/* Literal. */
					track_bytes -= (run & 0x7f);
					literal = (uint8_t *)malloc(run & 0x7f);
					fread(literal, 1, (run & 0x7f), dev->f);
					free(literal);
				}
				size += (run & 0x7f);
				if (!track_bytes)
					size -= fdf_suppress_final_byte;
			} else {
				/* Literal block. */
				size += (track_bytes - fdf_suppress_final_byte);
				literal = (uint8_t *)malloc(track_bytes);
				fread(literal, 1, track_bytes, dev->f);
				free(literal);
				track_bytes = 0;
			}

			if (feof(dev->f)) break;
		}

		/* Allocate the buffer. */
		dev->disk_data = (uint8_t *)malloc(size);

		/* Decode the entire file - pass 2, write to buffer. */
		fseek(dev->f, 0x80, SEEK_SET);
		track_bytes = 0;
		bpos = dev->disk_data;
		while(! feof(dev->f)) {
			if (! track_bytes) {
				/* Skip first 3 bytes - their meaning is unknown to us but could be a checksum. */
				first_byte = fgetc(dev->f);
				fread(&track_bytes, 1, 2, dev->f);
				img_log("Block header: %02X %04X ", first_byte, track_bytes);
				/* Read the length of encoded data block. */
				fread(&track_bytes, 1, 2, dev->f);
				img_log("%04X\n", track_bytes);
			}

			if (feof(dev->f)) break;

			if (first_byte == 0xFF) break;

			if (first_byte) {
				run = fgetc(dev->f);
				real_run = (run & 0x7f);

				/* I *HAVE* to read something because fseek tries to be smart and never hits EOF, causing an infinite loop. */
				track_bytes--;

				if (run & 0x80) {
					/* Repeat. */
					track_bytes--;
					if (! track_bytes)
						real_run -= fdf_suppress_final_byte;
					rep_byte = fgetc(dev->f);
					if (real_run)
						memset(bpos, rep_byte, real_run);
				} else {
					/* Literal. */
					track_bytes -= real_run;
					literal = (uint8_t *) malloc(real_run);
					fread(literal, 1, real_run, dev->f);
					if (! track_bytes)
						real_run -= fdf_suppress_final_byte;
					if (run & 0x7f)
						memcpy(bpos, literal, real_run);
					free(literal);
				}
				bpos += real_run;
			} else {
				/* Literal block. */
				literal = (uint8_t *) malloc(track_bytes);
				fread(literal, 1, track_bytes, dev->f);
				memcpy(bpos, literal, track_bytes - fdf_suppress_final_byte);
				free(literal);
				bpos += (track_bytes - fdf_suppress_final_byte);
				track_bytes = 0;
			}

			if (feof(dev->f)) break;
		}

		first_byte = *dev->disk_data;

		bpb_bps = *(uint16_t *)(dev->disk_data + 0x0B);
		bpb_total = *(uint16_t *)(dev->disk_data + 0x13);
		bpb_mid = *(dev->disk_data + 0x15);
		bpb_sectors = *(dev->disk_data + 0x18);
		bpb_sides = *(dev->disk_data + 0x1A);

		/* Jump ahead to determine the image's geometry. */
		goto jump_if_fdf;
	}

	if (((first_byte == 'C') && (second_byte == 'Q')) ||
	    ((first_byte == 'c') && (second_byte == 'q'))) {
		img_log("img_load(): File is a CopyQM image...\n");
		fwriteprot[drive] = writeprot[drive] = 1;
		fclose(dev->f);
		dev->f = plat_fopen(fn, L"rb");

		fseek(dev->f, 0x03, SEEK_SET);
		fread(&bpb_bps, 1, 2, dev->f);
#if 0
		fseek(dev->f, 0x0B, SEEK_SET);
		fread(&bpb_total, 1, 2, dev->f);
#endif
		fseek(dev->f, 0x10, SEEK_SET);
		bpb_sectors = fgetc(dev->f);
		fseek(dev->f, 0x12, SEEK_SET);
		bpb_sides = fgetc(dev->f);
		fseek(dev->f, 0x5B, SEEK_SET);
		dev->tracks = fgetc(dev->f);

		bpb_total = ((uint16_t)bpb_sectors) * ((uint16_t) bpb_sides) * dev->tracks;

		fseek(dev->f, 0x74, SEEK_SET);
		dev->interleave = fgetc(dev->f);
		fseek(dev->f, 0x76, SEEK_SET);
		dev->skew = fgetc(dev->f);

		dev->disk_data = (uint8_t *) malloc(((uint32_t) bpb_total) * ((uint32_t) bpb_bps));
		memset(dev->disk_data, 0xf6, ((uint32_t) bpb_total) * ((uint32_t) bpb_bps));

		fseek(dev->f, 0x6F, SEEK_SET);
		fread(&comment_len, 1, 2, dev->f);

		fseek(dev->f, -1, SEEK_END);
		size = ftell(dev->f) + 1;

		fseek(dev->f, 133 + comment_len, SEEK_SET);

		cur_pos = 0;

		while(! feof(dev->f)) {
			fread(&block_len, 1, 2, dev->f);

			if (! feof(dev->f)) {
				if (block_len < 0) {
					rep_byte = fgetc(dev->f);
					block_len = -block_len;
					if ((cur_pos + block_len) > ((uint32_t) bpb_total) * ((uint32_t) bpb_bps)) {
						block_len = ((uint32_t) bpb_total) * ((uint32_t) bpb_bps) - cur_pos;
						memset(dev->disk_data + cur_pos, rep_byte, block_len);
						break;
					} else {
						memset(dev->disk_data + cur_pos, rep_byte, block_len);
						cur_pos += block_len;
					}
				} else if (block_len > 0) {
					if ((cur_pos + block_len) > ((uint32_t) bpb_total) * ((uint32_t) bpb_bps)) {
						block_len = ((uint32_t) bpb_total) * ((uint32_t) bpb_bps) - cur_pos;
						fread(dev->disk_data + cur_pos, 1, block_len, dev->f);
						break;
					} else {
						fread(dev->disk_data + cur_pos, 1, block_len, dev->f);
						cur_pos += block_len;
					}
				}
			}
		}
		img_log("Finished reading CopyQM image data\n");

		cqm = 1;
		dev->disk_at_once = 1;
		fdf = 0;
		first_byte = *dev->disk_data;
	} else {
		dev->disk_at_once = 0;
		/* Read the BPB */
		if (ddi) {
			img_log("img_load(): File is a DDI image...\n");
			fwriteprot[drive] = writeprot[drive] = 1;
		} else
			img_log("img_load(): File is a raw image...\n");
		fseek(dev->f, dev->base + 0x0B, SEEK_SET);
		fread(&bpb_bps, 1, 2, dev->f);
		fseek(dev->f, dev->base + 0x13, SEEK_SET);
		fread(&bpb_total, 1, 2, dev->f);
		fseek(dev->f, dev->base + 0x15, SEEK_SET);
		bpb_mid = fgetc(dev->f);
		fseek(dev->f, dev->base + 0x18, SEEK_SET);
		bpb_sectors = fgetc(dev->f);
		fseek(dev->f, dev->base + 0x1A, SEEK_SET);
		bpb_sides = fgetc(dev->f);

		cqm = 0;
	}

	fseek(dev->f, -1, SEEK_END);
	size = ftell(dev->f) + 1;
	if (ddi)
		size -= 0x2400;

jump_if_fdf:
	if (!ddi)
		dev->base = 0;
	fdi = 0;
    }

    dev->sides = 2;
    dev->sector_size = 2;

    img_log("BPB reports %i sides and %i bytes per sector (%i sectors total)\n",
	bpb_sides, bpb_bps, bpb_total);

								/* Invalid conditions:						*/
    guess = (bpb_sides < 1);					/*     Sides < 1;						*/
    guess = guess || (bpb_sides > 2);				/*     Sides > 2;						*/
    guess = guess || !bps_is_valid(bpb_bps);			/*     Invalid number of bytes per sector;			*/
    guess = guess || !first_byte_is_valid(first_byte);		/*     Invalid first bytes;					*/
    guess = guess || !is_divisible(bpb_total, bpb_sectors);	/*     Total sectors not divisible by sectors per track;	*/
    guess = guess || !is_divisible(bpb_total, bpb_sides);	/*     Total sectors not divisible by sides.			*/
    guess = guess || !fdd_get_check_bpb(drive);
    guess = guess && !fdi;
    guess = guess && !cqm;
    if (guess) {
	/*
	 * The BPB is giving us a wacky number of sides and/or bytes
	 * per sector, therefore it is most probably not a BPB at all,
	 * so we have to guess the parameters from file size.
	 */
	if (size <= (160*1024))	{
		dev->sectors = 8;
		dev->tracks = 40;
		dev->sides = 1;
	} else if (size <= (180*1024)) {
		dev->sectors = 9;
		dev->tracks = 40;
		dev->sides = 1;
	} else if (size <= (315*1024)) {
		dev->sectors = 9;
		dev->tracks = 70;
		dev->sides = 1;
	} else if (size <= (320*1024)) {
		dev->sectors = 8;
		dev->tracks = 40;
	} else if (size <= (320*1024)) {
		dev->sectors = 8;
		dev->tracks = 40;
	} else if (size <= (360*1024)) {	/*DD 360K*/
		dev->sectors = 9;
		dev->tracks = 40;
	} else if (size <= (400*1024)) {	/*DEC RX50*/
		dev->sectors = 10;
		dev->tracks = 80;
		dev->sides = 1;
	} else if (size <= (640*1024)) {	/*DD 640K*/
		dev->sectors = 8;
		dev->tracks = 80;
	} else if (size <= (720*1024)) {	/*DD 720K*/
		dev->sectors = 9;
		dev->tracks = 80;
	} else if (size <= (800*1024)) {	/*DD*/
		dev->sectors = 10;
		dev->tracks = 80;
	} else if (size <= (880*1024)) {	/*DD*/
		dev->sectors = 11;
		dev->tracks = 80;
	} else if (size <= (960*1024)) {	/*DD*/
		dev->sectors = 12;
		dev->tracks = 80;
	} else if (size <= (1040*1024)) {	/*DD*/
		dev->sectors = 13;
		dev->tracks = 80;
	} else if (size <= (1120*1024)) {	/*DD*/
		dev->sectors = 14;
		dev->tracks = 80;
	} else if (size <= 1228800) {		/*HD 1.2MB*/
		dev->sectors = 15;
		dev->tracks = 80;
	} else if (size <= 1261568) {		/*HD 1.25MB Japanese*/
		dev->sectors =  8;
		dev->tracks = 77;
		dev->sector_size = 3;
	} else if (size <= 1474560) {		/*HD 1.44MB*/
		dev->sectors = 18;
		dev->tracks = 80;
	} else if (size <= 1556480) {		/*HD*/
		dev->sectors = 19;
		dev->tracks = 80;
	} else if (size <= 1638400) {		/*HD 1024 sector*/
		dev->sectors = 10;
		dev->tracks = 80;
		dev->sector_size = 3;
	} else if (size <= 1720320) {		/*DMF (Windows 95) */
		dev->sectors = 21;
		dev->tracks = 80;
	} else if (size <= 1741824) {
		dev->sectors = 21;
		dev->tracks = 81;
	} else if (size <= 1763328) {
		dev->sectors = 21;
		dev->tracks = 82;
	} else if (size <= 1802240) {		/*HD 1024 sector*/
		dev->sectors = 22;
		dev->tracks = 80;
		dev->sector_size = 3;
	} else if (size == 1884160) {		/*XDF (OS/2 Warp)*/
		dev->sectors = 23;
		dev->tracks = 80;
	} else if (size <= 2949120) {		/*ED*/
		dev->sectors = 36;
		dev->tracks = 80;
	} else if (size <= 3194880) {		/*ED*/
		dev->sectors = 39;
		dev->tracks = 80;
	} else if (size <= 3276800) {		/*ED*/
		dev->sectors = 40;
		dev->tracks = 80;
	} else if (size <= 3358720) {		/*ED, maximum possible size*/
		dev->sectors = 41;
		dev->tracks = 80;
	} else if (size <= 3440640) {		/*ED, maximum possible size*/
		dev->sectors = 42;
		dev->tracks = 80;
#if 0
	} else if (size <= 3440640) {		/*HD 1024 sector*/
		dev->sectors = 21;
		dev->tracks = 80;
		dev->sector_size = 3;
#endif
	} else if (size <= 3604480) {		/*HD 1024 sector*/
		dev->sectors = 22;
		dev->tracks = 80;
		dev->sector_size = 3;
	} else if (size <= 3610624) {		/*ED, maximum possible size*/
		dev->sectors = 41;
		dev->tracks = 86;
	} else if (size <= 3698688) {		/*ED, maximum possible size*/
		dev->sectors = 42;
		dev->tracks = 86;
	} else {
		img_log("Image is bigger than can fit on an ED floppy, ejecting...\n");
		fclose(dev->f);
		free(dev);
		memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
		return;
	}

	bpb_sides = dev->sides;
	bpb_sectors = dev->sectors;
	bpb_total = size >> (dev->sector_size + 7);
    } else {
	/* The BPB readings appear to be valid, so let's set the values. */
	if (fdi) {
		/* The image is a Japanese FDI, therefore we read the number of tracks from the header. */
		fseek(dev->f, 0x1C, SEEK_SET);
		fread(&(dev->tracks), 1, 4, dev->f);
	} else {
		if (!cqm && !fdf) {
			/* Number of tracks = number of total sectors divided by sides times sectors per track. */
			dev->tracks = ((uint32_t) bpb_total) / (((uint32_t) bpb_sides) * ((uint32_t) bpb_sectors));
		}
	}

	/* The rest we just set directly from the BPB. */
	dev->sectors = bpb_sectors;
	dev->sides = bpb_sides;

	/* The sector size. */
	dev->sector_size = sector_size_code(bpb_bps);

	temp_rate = 0xFF;
    }

    for (i = 0; i < 6; i++) {
	if ((dev->sectors <= maximum_sectors[dev->sector_size][i]) || (dev->sectors == xdf_sectors[dev->sector_size][i])) {
		bit_rate_300 = bit_rates_300[i];
		temp_rate = rates[i];
		dev->disk_flags = holes[i] << 1;
		dev->xdf_type = (dev->sectors == xdf_sectors[dev->sector_size][i]) ? xdf_types[dev->sector_size][i] : 0;
		if ((bit_rate_300 == 500.0) && (dev->sectors == 21) && (dev->sector_size == 2) && (dev->tracks >= 80) && (dev->tracks <= 82) && (dev->sides == 2)) {
			/* This is a DMF floppy, set the flag so we know to interleave the sectors. */
			dev->dmf = 1;
		} else {
			if ((bit_rate_300 == 500.0) && (dev->sectors == 22) && (dev->sector_size == 2) && (dev->tracks >= 80) && (dev->tracks <= 82) && (dev->sides == 2)) {
				/* This is marked specially because of the track flag (a RPM slow down is needed). */
				dev->interleave = 2;
			}
			dev->dmf = 0;
		}

		img_log("Image parameters: bit rate 300: %f, temporary rate: %i, hole: %i, DMF: %i, XDF type: %i\n", bit_rate_300, temp_rate, dev->disk_flags >> 1, dev->dmf, dev->xdf_type);
		break;
	}
    }

    if (temp_rate == 0xFF) {
	img_log("Image is bigger than can fit on an ED floppy, ejecting...\n");
	fclose(dev->f);
	free(dev);
	memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
	return;
    }

    dev->gap2_size = (temp_rate == 3) ? 41 : 22;
    if (dev->dmf)
	dev->gap3_size = 8;
      else
	dev->gap3_size = gap3_sizes[temp_rate][dev->sector_size][dev->sectors];
    if (! dev->gap3_size) {
	img_log("ERROR: Floppy image of unknown format was inserted into drive %c:!\n", drive + 0x41);
	fclose(dev->f);
	free(dev);
	memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
	return;
    }

    dev->track_width = 0;
    if (dev->tracks > 43)
	dev->track_width = 1;	/* If the image has more than 43 tracks, then the tracks are thin (96 tpi). */
    if (dev->sides == 2)
	dev->disk_flags |= 8;	/* If the has 2 sides, mark it as such. */
    if (dev->interleave == 2) {
	dev->interleave = 1;
	dev->disk_flags |= 0x60;
    }

    dev->track_flags = 0x08;			/* IMG files are always assumed to be MFM-encoded. */
    dev->track_flags |= temp_rate & 3;		/* Data rate. */
    if (temp_rate & 4)
	dev->track_flags |= 0x20;		/* RPM. */

    dev->is_cqm = cqm;

    img_log("Disk flags: %i, track flags: %i\n",
		dev->disk_flags, dev->track_flags);

    /* Set up the drive unit. */
    img[drive] = dev;

    /* Attach this format to the D86F engine. */
    d86f_handler[drive].disk_flags = disk_flags;
    d86f_handler[drive].side_flags = side_flags;
    d86f_handler[drive].writeback = write_back;
    d86f_handler[drive].set_sector = set_sector;
    d86f_handler[drive].read_data = poll_read_data;
    d86f_handler[drive].write_data = poll_write_data;
    d86f_handler[drive].format_conditions = format_conditions;
    d86f_handler[drive].extra_bit_cells = null_extra_bit_cells;
    d86f_handler[drive].encoded_data = common_encoded_data;
    d86f_handler[drive].read_revolution = common_read_revolution;
    d86f_handler[drive].index_hole_pos = null_index_hole_pos;
    d86f_handler[drive].get_raw_size = common_get_raw_size;
    d86f_handler[drive].check_crc = 1;
    d86f_set_version(drive, 0x0063);

    drives[drive].seek = img_seek;

    d86f_common_handlers(drive);
}


void
img_close(int drive)
{
    img_t *dev = img[drive];

    if (dev == NULL) return;

    d86f_unregister(drive);

    if (dev->f != NULL) {
	fclose(dev->f);
	dev->f = NULL;
    }

    if (dev->disk_data != NULL)
	free(dev->disk_data);

    /* Release the memory. */
    free(dev);
    img[drive] = NULL;
}


void
img_set_fdc(void *fdc)
{
    img_fdc = (fdc_t *) fdc;
}
