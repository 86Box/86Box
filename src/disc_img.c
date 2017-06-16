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
 * Version:	@(#)disc_img.c	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

#include <stdlib.h>
#include <wchar.h>

#include "ibm.h"
#include "config.h"
#include "disc.h"
#include "disc_img.h"
#include "fdc.h"
#include "fdd.h"

static struct
{
        FILE *f;
        uint8_t track_data[2][50000];
        int sectors, tracks, sides;
        uint8_t sector_size;
	int xdf_type;	/* 0 = not XDF, 1-5 = one of the five XDF types */
	int dmf;
	int hole;
	int track;
	int track_width;
	uint32_t base;
	uint8_t gap2_size;
	uint8_t gap3_size;
	uint16_t disk_flags;
	uint16_t track_flags;
	uint8_t sector_pos_side[2][256];
	uint16_t sector_pos[2][256];
	uint8_t current_sector_pos_side;
	uint16_t current_sector_pos;
	uint8_t *disk_data;
	uint8_t is_cqm;
	uint8_t disk_at_once;
	uint8_t interleave;
	uint8_t skew;
} img[FDD_NUM];

uint8_t dmf_r[21] = { 12, 2, 13, 3, 14, 4, 15, 5, 16, 6, 17, 7, 18, 8, 19, 9, 20, 10, 21, 11, 1 };
static uint8_t xdf_logical_sectors[2][2] = { { 38, 6 }, { 46, 8 } };
uint8_t xdf_physical_sectors[2][2] = { { 16, 3 }, { 19, 4 } };
uint8_t xdf_gap3_sizes[2][2] = { { 60, 69 }, { 60, 50 } };
uint16_t xdf_trackx_spos[2][8] = { { 0xA7F, 0xF02, 0x11B7, 0xB66, 0xE1B, 0x129E }, { 0x302, 0x7E2, 0xA52, 0x12DA, 0x572, 0xDFA, 0x106A, 0x154A } };

/* XDF: Layout of the sectors in the image. */
xdf_sector_t xdf_img_layout[2][2][46] = {	{	{ {0x8100}, {0x8200}, {0x8300}, {0x8400}, {0x8500}, {0x8600}, {0x8700}, {0x8800},
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
xdf_sector_t xdf_disk_layout[2][2][38] = {	{	{ {0x0100}, {0x0200}, {0x8100}, {0x8800}, {0x8200}, {0x0300}, {0x8300}, {0x0400},
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
static uint8_t maximum_sectors[8][6] = { { 26, 31, 38, 53, 64, 118 },	/*   128 */
                                         { 15, 19, 23, 32, 38,  73 },	/*   256 */
                                         {  7, 10, 12, 17, 22,  41 },	/*   512 */
                                         {  3,  5,  6,  9, 11,  22 },	/*  1024 */
                                         {  2,  2,  3,  4,  5,  11 },	/*  2048 */
                                         {  1,  1,  1,  2,  2,   5 },	/*  4096 */
					 {  0,  0,  0,  1,  1,   3 },	/*  8192 */
					 {  0,  0,  0,  0,  0,   1 } };	/* 16384 */

static uint8_t xdf_sectors[8][6]     = { {  0,  0,  0,  0,  0,   0 },	/*   128 */
                                         {  0,  0,  0,  0,  0,   0 },	/*   256 */
                                         {  0,  0,  0, 19, 23,   0 },	/*   512 */
                                         {  0,  0,  0,  0,  0,   0 },	/*  1024 */
                                         {  0,  0,  0,  0,  0,   0 },	/*  2048 */
                                         {  0,  0,  0,  0,  0,   0 },	/*  4096 */
					 {  0,  0,  0,  0,  0,   0 },	/*  8192 */
					 {  0,  0,  0,  0,  0,   0 } };	/* 16384 */

static uint8_t xdf_types[8][6]       = { {  0,  0,  0,  0,  0,   0 },	/*   128 */
                                         {  0,  0,  0,  0,  0,   0 },	/*   256 */
                                         {  0,  0,  0,  1,  2,   0 },	/*   512 */
                                         {  0,  0,  0,  0,  0,   0 },	/*  1024 */
                                         {  0,  0,  0,  0,  0,   0 },	/*  2048 */
                                         {  0,  0,  0,  0,  0,   0 },	/*  4096 */
					 {  0,  0,  0,  0,  0,   0 },	/*  8192 */
					 {  0,  0,  0,  0,  0,   0 } };	/* 16384 */

static double bit_rates_300[6]       = { (250.0 * 300.0) / 360.0, 250.0, 300.0, (500.0 * 300.0) / 360.0, 500.0, 1000.0 };

static uint8_t rates[6]              = { 2, 2, 1, 4, 0, 3 };

static uint8_t holes[6]              = { 0, 0, 0, 1, 1, 2 };

int gap3_sizes[5][8][48] = {	{	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* [0][0] */
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

void img_writeback(int drive);

static int sector_size_code(int sector_size)
{
	switch(sector_size)
	{
		case 128:
			return 0;
		case 256:
			return 1;
		default:
		case 512:
			return 2;
		case 1024:
			return 3;
		case 2048:
			return 4;
		case 4096:
			return 5;
		case 8192:
			return 6;
		case 16384:
			return 7;
	}
}

void img_init()
{
        memset(img, 0, sizeof(img));
}

void d86f_register_img(int drive);

int bps_is_valid(uint16_t bps)
{
	int i;
	for (i = 0; i <= 8; i++)
	{
		if (bps == (128 << i))
		{
			return 1;
		}
	}
	return 0;
}

int first_byte_is_valid(uint8_t first_byte)
{
	switch(first_byte)
	{
		case 0x60:
		case 0xE9:
		case 0xEB:
			return 1;
		default:
			return 0;
	}
}

double bit_rate_300;

wchar_t *ext;

uint8_t first_byte, second_byte, third_byte, fourth_byte;

/* This is hard-coded to 0 - if you really need to read those NT 3.1 Beta floppy images, change this to 1 and recompile the emulator. */
uint8_t fdf_suppress_final_byte = 0;

void img_load(int drive, wchar_t *fn)
{
        int size;
	uint16_t bpb_bps;
	uint16_t bpb_total;
	uint8_t bpb_mid;	/* Media type ID. */
	uint8_t bpb_sectors;
	uint8_t bpb_sides;
	int temp_rate;
	uint8_t fdi, cqm, fdf;
	int i;
	uint16_t comment_len = 0;
	int16_t block_len = 0;
	uint32_t cur_pos = 0;
	uint8_t rep_byte = 0;
	uint8_t run = 0;
	uint8_t real_run = 0;
	uint8_t *bpos;
	uint16_t track_bytes = 0;
	uint8_t *literal;

	ext = get_extension_w(fn);

	d86f_unregister(drive);

	writeprot[drive] = 0;
        img[drive].f = _wfopen(fn, L"rb+");
        if (!img[drive].f)
        {
                img[drive].f = _wfopen(fn, L"rb");
                if (!img[drive].f)
		{
			memset(discfns[drive], 0, sizeof(discfns[drive]));
                        return;
		}
                writeprot[drive] = 1;
        }
	if (ui_writeprot[drive])
	{
                writeprot[drive] = 1;
	}
        fwriteprot[drive] = writeprot[drive];

	fdi = cqm = 0;

	img[drive].interleave = img[drive].skew = 0;

	if (_wcsicmp(ext, L"FDI") == 0)
	{
		/* This is a Japanese FDI image, so let's read the header */
		pclog("img_load(): File is a Japanese FDI image...\n");
		fseek(img[drive].f, 0x10, SEEK_SET);
		fread(&bpb_bps, 1, 2, img[drive].f);
		fseek(img[drive].f, 0x0C, SEEK_SET);
		fread(&size, 1, 4, img[drive].f);
		bpb_total = size / bpb_bps;
		fseek(img[drive].f, 0x08, SEEK_SET);
		fread(&(img[drive].base), 1, 4, img[drive].f);
		fseek(img[drive].f, img[drive].base + 0x15, SEEK_SET);
		bpb_mid = fgetc(img[drive].f);
		if (bpb_mid < 0xF0)  bpb_mid = 0xF0;
		fseek(img[drive].f, 0x14, SEEK_SET);
		bpb_sectors = fgetc(img[drive].f);
		fseek(img[drive].f, 0x18, SEEK_SET);
		bpb_sides = fgetc(img[drive].f);

		fseek(img[drive].f, img[drive].base, SEEK_SET);
		first_byte = fgetc(img[drive].f);

		fdi = 1;
		cqm = 0;
		img[drive].disk_at_once = 0;
		fdf = 0;
	}
	else
	{
		/* Read the first four bytes. */
		fseek(img[drive].f, 0x00, SEEK_SET);
		first_byte = fgetc(img[drive].f);
		fseek(img[drive].f, 0x01, SEEK_SET);
		second_byte = fgetc(img[drive].f);
		fseek(img[drive].f, 0x02, SEEK_SET);
		third_byte = fgetc(img[drive].f);
		fseek(img[drive].f, 0x03, SEEK_SET);
		fourth_byte = fgetc(img[drive].f);

		if ((first_byte == 0x1A) && (second_byte == 'F') && (third_byte == 'D') && (fourth_byte == 'F'))
		{
			/* This is a FDF image. */
			pclog("img_load(): File is a FDF image...\n");
	                fwriteprot[drive] = writeprot[drive] = 1;
			fclose(img[drive].f);
			img[drive].f = _wfopen(fn, L"rb");

			fdf = 1;

			cqm = 0;
			img[drive].disk_at_once = 1;

			fseek(img[drive].f, 0x50, SEEK_SET);
			fread(&img[drive].tracks, 1, 4, img[drive].f);

			/* Decode the entire file - pass 1, no write to buffer, determine length. */
			fseek(img[drive].f, 0x80, SEEK_SET);
			size = 0;
			track_bytes = 0;
			bpos = img[drive].disk_data;
			while(!feof(img[drive].f))
			{
				if (!track_bytes)
				{
					/* Skip first 3 bytes - their meaning is unknown to us but could be a checksum. */
					first_byte = fgetc(img[drive].f);
					fread(&track_bytes, 1, 2, img[drive].f);
					pclog("Block header: %02X %04X ", first_byte, track_bytes);
					/* Read the length of encoded data block. */
					fread(&track_bytes, 1, 2, img[drive].f);
					pclog("%04X\n", track_bytes);
				}

				if (feof(img[drive].f))
				{
					break;
				}

				if (first_byte == 0xFF)
				{
					break;
				}

				if (first_byte)
				{
					run = fgetc(img[drive].f);

					/* I *HAVE* to read something because fseek tries to be smart and never hits EOF, causing an infinite loop. */
					track_bytes--;

					if (run & 0x80)
					{
						/* Repeat. */
						track_bytes--;
						rep_byte = fgetc(img[drive].f);
					}
					else
					{
						/* Literal. */
						track_bytes -= (run & 0x7f);
						literal = (uint8_t *) malloc(run & 0x7f);
						fread(literal, 1, (run & 0x7f), img[drive].f);
						free(literal);
					}
					size += (run & 0x7f);
					if (!track_bytes)
					{
						size -= fdf_suppress_final_byte;
					}
				}
				else
				{
					/* Literal block. */
					size += (track_bytes - fdf_suppress_final_byte);
					literal = (uint8_t *) malloc(track_bytes);
					fread(literal, 1, track_bytes, img[drive].f);
					free(literal);
					track_bytes = 0;
				}

				if (feof(img[drive].f))
				{
					break;
				}
			}

			/* Allocate the buffer. */
			img[drive].disk_data = (uint8_t *) malloc(size);

			/* Decode the entire file - pass 2, write to buffer. */
			fseek(img[drive].f, 0x80, SEEK_SET);
			track_bytes = 0;
			bpos = img[drive].disk_data;
			while(!feof(img[drive].f))
			{
				if (!track_bytes)
				{
					/* Skip first 3 bytes - their meaning is unknown to us but could be a checksum. */
					first_byte = fgetc(img[drive].f);
					fread(&track_bytes, 1, 2, img[drive].f);
					pclog("Block header: %02X %04X ", first_byte, track_bytes);
					/* Read the length of encoded data block. */
					fread(&track_bytes, 1, 2, img[drive].f);
					pclog("%04X\n", track_bytes);
				}

				if (feof(img[drive].f))
				{
					break;
				}

				if (first_byte == 0xFF)
				{
					break;
				}

				if (first_byte)
				{
					run = fgetc(img[drive].f);
					real_run = (run & 0x7f);

					/* I *HAVE* to read something because fseek tries to be smart and never hits EOF, causing an infinite loop. */
					track_bytes--;

					if (run & 0x80)
					{
						/* Repeat. */
						track_bytes--;
						if (!track_bytes)
						{
							real_run -= fdf_suppress_final_byte;
						}
						rep_byte = fgetc(img[drive].f);
						if (real_run)
						{
							memset(bpos, rep_byte, real_run);
						}
					}
					else
					{
						/* Literal. */
						track_bytes -= real_run;
						literal = (uint8_t *) malloc(real_run);
						fread(literal, 1, real_run, img[drive].f);
						if (!track_bytes)
						{
							real_run -= fdf_suppress_final_byte;
						}
						if (run & 0x7f)
						{
							memcpy(bpos, literal, real_run);
						}
						free(literal);
					}
					bpos += real_run;
				}
				else
				{
					/* Literal block. */
					literal = (uint8_t *) malloc(track_bytes);
					fread(literal, 1, track_bytes, img[drive].f);
					memcpy(bpos, literal, track_bytes - fdf_suppress_final_byte);
					free(literal);
					bpos += (track_bytes - fdf_suppress_final_byte);
					track_bytes = 0;
				}

				if (feof(img[drive].f))
				{
					break;
				}
			}

			first_byte = *img[drive].disk_data;

			bpb_bps = *(uint16_t *) (img[drive].disk_data + 0x0B);
			bpb_total = *(uint16_t *) (img[drive].disk_data + 0x13);
			bpb_mid = *(img[drive].disk_data + 0x15);
			bpb_sectors = *(img[drive].disk_data + 0x18);
			bpb_sides = *(img[drive].disk_data + 0x1A);

			/* Jump ahead to determine the image's geometry and finish the loading. */
			goto jump_if_fdf;
		}

		if (((first_byte == 'C') && (second_byte == 'Q')) || ((first_byte == 'c') && (second_byte == 'q')))
		{
			pclog("img_load(): File is a CopyQM image...\n");
	                fwriteprot[drive] = writeprot[drive] = 1;
			fclose(img[drive].f);
			img[drive].f = _wfopen(fn, L"rb");

			fseek(img[drive].f, 0x03, SEEK_SET);
			fread(&bpb_bps, 1, 2, img[drive].f);
			/* fseek(img[drive].f, 0x0B, SEEK_SET);
			fread(&bpb_total, 1, 2, img[drive].f); */
			fseek(img[drive].f, 0x10, SEEK_SET);
			bpb_sectors = fgetc(img[drive].f);
			fseek(img[drive].f, 0x12, SEEK_SET);
			bpb_sides = fgetc(img[drive].f);
			fseek(img[drive].f, 0x5B, SEEK_SET);
			img[drive].tracks = fgetc(img[drive].f);

			bpb_total = ((uint16_t) bpb_sectors) * ((uint16_t) bpb_sides) * img[drive].tracks;

			fseek(img[drive].f, 0x74, SEEK_SET);
			img[drive].interleave = fgetc(img[drive].f);
			fseek(img[drive].f, 0x76, SEEK_SET);
			img[drive].skew = fgetc(img[drive].f);

			img[drive].disk_data = (uint8_t *) malloc(((uint32_t) bpb_total) * ((uint32_t) bpb_bps));
			memset(img[drive].disk_data, 0xf6, ((uint32_t) bpb_total) * ((uint32_t) bpb_bps));

			fseek(img[drive].f, 0x6F, SEEK_SET);
			fread(&comment_len, 1, 2, img[drive].f);

		        fseek(img[drive].f, -1, SEEK_END);
		        size = ftell(img[drive].f) + 1;

			fseek(img[drive].f, 133 + comment_len, SEEK_SET);

			cur_pos = 0;

			while(!feof(img[drive].f))
			{
				fread(&block_len, 1, 2, img[drive].f);

				if (!feof(img[drive].f))
				{
					if (block_len < 0)
					{
						rep_byte = fgetc(img[drive].f);
						block_len = -block_len;
						if ((cur_pos + block_len) > ((uint32_t) bpb_total) * ((uint32_t) bpb_bps))
						{
							block_len = ((uint32_t) bpb_total) * ((uint32_t) bpb_bps) - cur_pos;
							memset(img[drive].disk_data + cur_pos, rep_byte, block_len);
							break;
						}
						else
						{
							memset(img[drive].disk_data + cur_pos, rep_byte, block_len);
							cur_pos += block_len;
						}
					}
					else if (block_len > 0)
					{
						if ((cur_pos + block_len) > ((uint32_t) bpb_total) * ((uint32_t) bpb_bps))
						{
							block_len = ((uint32_t) bpb_total) * ((uint32_t) bpb_bps) - cur_pos;
							fread(img[drive].disk_data + cur_pos, 1, block_len, img[drive].f);
							break;
						}
						else
						{
							fread(img[drive].disk_data + cur_pos, 1, block_len, img[drive].f);
							cur_pos += block_len;
						}
					}
				}
			}
			printf("Finished reading CopyQM image data\n");

			cqm = 1;
			img[drive].disk_at_once = 1;
			fdf = 0;
			first_byte = *img[drive].disk_data;
		}
		else
		{
			img[drive].disk_at_once = 0;
			/* Read the BPB */
			pclog("img_load(): File is a raw image...\n");
			fseek(img[drive].f, 0x0B, SEEK_SET);
			fread(&bpb_bps, 1, 2, img[drive].f);
			fseek(img[drive].f, 0x13, SEEK_SET);
			fread(&bpb_total, 1, 2, img[drive].f);
			fseek(img[drive].f, 0x15, SEEK_SET);
			bpb_mid = fgetc(img[drive].f);
			fseek(img[drive].f, 0x18, SEEK_SET);
			bpb_sectors = fgetc(img[drive].f);
			fseek(img[drive].f, 0x1A, SEEK_SET);
			bpb_sides = fgetc(img[drive].f);

			cqm = 0;
		}

	        fseek(img[drive].f, -1, SEEK_END);
	        size = ftell(img[drive].f) + 1;

jump_if_fdf:
		img[drive].base = 0;
		fdi = 0;
	}

        img[drive].sides = 2;
        img[drive].sector_size = 2;

	img[drive].hole = 0;

	pclog("BPB reports %i sides and %i bytes per sector (%i sectors total)\n", bpb_sides, bpb_bps, bpb_total);

	if (((bpb_sides < 1) || (bpb_sides > 2) || !bps_is_valid(bpb_bps) || !first_byte_is_valid(first_byte)) && !fdi && !cqm)
	{
		/* The BPB is giving us a wacky number of sides and/or bytes per sector, therefore it is most probably
		   not a BPB at all, so we have to guess the parameters from file size. */

		if (size <= (160*1024))        { img[drive].sectors = 8;  img[drive].tracks = 40; img[drive].sides = 1; }
	        else if (size <= (180*1024))   { img[drive].sectors = 9;  img[drive].tracks = 40; img[drive].sides = 1; }
	        else if (size <= (315*1024))   { img[drive].sectors = 9;  img[drive].tracks = 70; img[drive].sides = 1; }
	        else if (size <= (320*1024))   { img[drive].sectors = 8;  img[drive].tracks = 40; }
	        else if (size <= (320*1024))   { img[drive].sectors = 8;  img[drive].tracks = 40; }
	        else if (size <= (360*1024))   { img[drive].sectors = 9;  img[drive].tracks = 40; } /*Double density*/
	        else if (size <= (400*1024))   { img[drive].sectors = 10; img[drive].tracks = 80; img[drive].sides = 1; } /*DEC RX50*/
	        else if (size <= (640*1024))   { img[drive].sectors = 8;  img[drive].tracks = 80; } /*Double density 640k*/
	        else if (size <= (720*1024))   { img[drive].sectors = 9;  img[drive].tracks = 80; } /*Double density*/
	        else if (size <= (800*1024))   { img[drive].sectors = 10; img[drive].tracks = 80; } /*Double density*/
	        else if (size <= (880*1024))   { img[drive].sectors = 11; img[drive].tracks = 80; } /*Double density*/
	        else if (size <= (960*1024))   { img[drive].sectors = 12; img[drive].tracks = 80; } /*Double density*/
	        else if (size <= (1040*1024))  { img[drive].sectors = 13; img[drive].tracks = 80; } /*Double density*/
	        else if (size <= (1120*1024))  { img[drive].sectors = 14; img[drive].tracks = 80; } /*Double density*/
	        else if (size <= 1228800)      { img[drive].sectors = 15; img[drive].tracks = 80; } /*High density 1.2MB*/
	        else if (size <= 1261568)      { img[drive].sectors =  8; img[drive].tracks = 77; img[drive].sector_size = 3; } /*High density 1.25MB Japanese format*/
	        else if (size <= 1474560)      { img[drive].sectors = 18; img[drive].tracks = 80; } /*High density (not supported by Tandy 1000)*/
	        else if (size <= 1556480)      { img[drive].sectors = 19; img[drive].tracks = 80; } /*High density (not supported by Tandy 1000)*/
	        else if (size <= 1638400)      { img[drive].sectors = 10; img[drive].tracks = 80; img[drive].sector_size = 3; } /*High density (not supported by Tandy 1000)*/
	        else if (size <= 1720320)      { img[drive].sectors = 21; img[drive].tracks = 80; } /*DMF format - used by Windows 95 */
	        else if (size <= 1741824)      { img[drive].sectors = 21; img[drive].tracks = 81; }
	        else if (size <= 1763328)      { img[drive].sectors = 21; img[drive].tracks = 82; }
	        else if (size <= 1802240)      { img[drive].sectors = 22; img[drive].tracks = 80; img[drive].sector_size = 3; } /*High density (not supported by Tandy 1000)*/
	        else if (size == 1884160)      { img[drive].sectors = 23; img[drive].tracks = 80; } /*XDF format - used by OS/2 Warp*/
	        else if (size <= 2949120)      { img[drive].sectors = 36; img[drive].tracks = 80; } /*E density*/
		else if (size <= 3194880)      { img[drive].sectors = 39; img[drive].tracks = 80; } /*E density*/
		else if (size <= 3276800)      { img[drive].sectors = 40; img[drive].tracks = 80; } /*E density*/
		else if (size <= 3358720)      { img[drive].sectors = 41; img[drive].tracks = 80; } /*E density, maximum possible size*/
		else if (size <= 3440640)      { img[drive].sectors = 42; img[drive].tracks = 80; } /*E density, maximum possible size*/
	        /* else if (size <= 3440640)      { img[drive].sectors = 21; img[drive].tracks = 80; img[drive].sector_size = 3; } */ /*High density (not supported by Tandy 1000)*/
	        else if (size <= 3604480)      { img[drive].sectors = 22; img[drive].tracks = 80; img[drive].sector_size = 3; } /*High density (not supported by Tandy 1000)*/
		else if (size <= 3610624)      { img[drive].sectors = 41; img[drive].tracks = 86; } /*E density, maximum possible size*/
		else if (size <= 3698688)      { img[drive].sectors = 42; img[drive].tracks = 86; } /*E density, maximum possible size*/
		else
		{
			pclog("Image is bigger than can fit on an ED floppy, ejecting...\n");
			fclose(img[drive].f);
			memset(discfns[drive], 0, sizeof(discfns[drive]));
			return;
		}
	}
	else
	{
		/* The BPB readings appear to be valid, so let's set the values. */
		if (fdi)
		{
			/* The image is a Japanese FDI, therefore we read the number of tracks from the header. */
			fseek(img[drive].f, 0x1C, SEEK_SET);
			fread(&(img[drive].tracks), 1, 4, img[drive].f);
		}
		else
		{
			if (!cqm && !fdf)
			{
				/* Number of tracks = number of total sectors divided by sides times sectors per track. */
				img[drive].tracks = ((uint32_t) bpb_total) / (((uint32_t) bpb_sides) * ((uint32_t) bpb_sectors));
			}
		}
		/* The rest we just set directly from the BPB. */
		img[drive].sectors = bpb_sectors;
		img[drive].sides = bpb_sides;
		/* The sector size. */
		img[drive].sector_size = sector_size_code(bpb_bps);

		temp_rate = 0xFF;
	}

	for (i = 0; i < 6; i++)
	{
		if ((img[drive].sectors <= maximum_sectors[img[drive].sector_size][i]) || (img[drive].sectors == xdf_sectors[img[drive].sector_size][i]))
		{
			bit_rate_300 = bit_rates_300[i];
			temp_rate = rates[i];
			img[drive].disk_flags = holes[i] << 1;
			img[drive].xdf_type = (img[drive].sectors == xdf_sectors[img[drive].sector_size][i]) ? xdf_types[img[drive].sector_size][i] : 0;
			if ((bit_rate_300 == 500.0) && (img[drive].sectors == 21) && (img[drive].sector_size == 2) && (img[drive].tracks >= 80) && (img[drive].tracks <= 82) && (img[drive].sides == 2))
			{
				/* This is a DMF floppy, set the flag so we know to interleave the sectors. */
				img[drive].dmf = 1;
			}
			else
			{
				if ((bit_rate_300 == 500.0) && (img[drive].sectors == 22) && (img[drive].sector_size == 2) && (img[drive].tracks >= 80) && (img[drive].tracks <= 82) && (img[drive].sides == 2))
				{
					/* This is marked specially because of the track flag (a RPM slow down is needed). */
					img[drive].interleave = 2;
				}
				img[drive].dmf = 0;
			}

			pclog("Image parameters: bit rate 300: %f, temporary rate: %i, hole: %i, DMF: %i, XDF type: %i\n", bit_rate_300, temp_rate, img[drive].disk_flags >> 1, img[drive].dmf, img[drive].xdf_type);
			break;
		}
	}

	if (temp_rate == 0xFF)
	{
		pclog("Image is bigger than can fit on an ED floppy, ejecting...\n");
		fclose(img[drive].f);
		memset(discfns[drive], 0, sizeof(discfns[drive]));
		return;
	}

	img[drive].gap2_size = (temp_rate == 3) ? 41 : 22;
	if (img[drive].dmf)
	{
		img[drive].gap3_size = 8;
	}
	else
	{
		img[drive].gap3_size = gap3_sizes[temp_rate][img[drive].sector_size][img[drive].sectors];
	}
	if (!img[drive].gap3_size)
	{
		pclog("ERROR: Floppy image of unknown format was inserted into drive %c:!\n", drive + 0x41);
		fclose(img[drive].f);
		memset(discfns[drive], 0, sizeof(discfns[drive]));
		return;
	}

	img[drive].track_width = 0;
	if (img[drive].tracks > 43)  img[drive].track_width = 1;	/* If the image has more than 43 tracks, then the tracks are thin (96 tpi). */
	if (img[drive].sides == 2)   img[drive].disk_flags |= 8;	/* If the has 2 sides, mark it as such. */
	if (img[drive].interleave == 2)
	{
		img[drive].interleave = 1;
		img[drive].disk_flags |= 0x60;
	}

	img[drive].track_flags = 0x08;					/* IMG files are always assumed to be MFM-encoded. */
	img[drive].track_flags |= temp_rate & 3;			/* Data rate. */
	if (temp_rate & 4)  img[drive].track_flags |= 0x20;		/* RPM. */

	img[drive].is_cqm = cqm;

	pclog("Disk flags: %i, track flags: %i\n", img[drive].disk_flags, img[drive].track_flags);

	d86f_register_img(drive);

        drives[drive].seek        = img_seek;

	d86f_common_handlers(drive);
}

void img_close(int drive)
{
	d86f_unregister(drive);
        if (img[drive].f)
                fclose(img[drive].f);
        if (img[drive].disk_data)
                free(img[drive].disk_data);
}

#define xdf_img_sector xdf_img_layout[current_xdft][!is_t0][sector]
#define xdf_disk_sector xdf_disk_layout[current_xdft][!is_t0][array_sector]

int interleave(int sector, int skew, int track_spt)
{
	uint32_t skewed_i = 0;
	uint32_t adjusted_r = 0;

	uint32_t add = (track_spt & 1);
	uint32_t adjust = (track_spt >> 1);

	skewed_i = (sector + skew) % track_spt;
	adjusted_r = (skewed_i >> 1) + 1;
	if (skewed_i & 1)
	{
		adjusted_r += (adjust + add);
	}

	return adjusted_r;
}

void img_seek(int drive, int track)
{
        int side;
	int current_xdft = img[drive].xdf_type - 1;

	int read_bytes = 0;

	uint8_t id[4] = { 0, 0, 0, 0 };

	int is_t0, sector, current_pos, img_pos, sr, sside, total, array_sector, buf_side, buf_pos;

	int ssize = 128 << ((int) img[drive].sector_size);
	uint32_t cur_pos = 0;
        
        if (!img[drive].f)
                return;

        if (!img[drive].track_width && fdd_doublestep_40(drive))
                track /= 2;

	img[drive].track = track;

	is_t0 = (track == 0) ? 1 : 0;

	if (!img[drive].disk_at_once)
	{
		fseek(img[drive].f, img[drive].base + (track * img[drive].sectors * ssize * img[drive].sides), SEEK_SET);
	}

	for (side = 0; side < img[drive].sides; side++)
	{
		if (img[drive].disk_at_once)
		{
			cur_pos = (track * img[drive].sectors * ssize * img[drive].sides) + (side * img[drive].sectors * ssize);
			memcpy(img[drive].track_data[side], img[drive].disk_data + cur_pos, img[drive].sectors * ssize);
		}
		else
		{
	                read_bytes = fread(img[drive].track_data[side], 1, img[drive].sectors * ssize, img[drive].f);
			if (read_bytes < (img[drive].sectors * ssize))
			{
				memset(img[drive].track_data[side] + read_bytes, 0xf6, (img[drive].sectors * ssize) - read_bytes);
			}
		}
	}

	d86f_reset_index_hole_pos(drive, 0);
	d86f_reset_index_hole_pos(drive, 1);

	if (!img[drive].xdf_type || img[drive].is_cqm)
	{
		for (side = 0; side < img[drive].sides; side++)
		{
			current_pos = d86f_prepare_pretrack(drive, side, 0);

			for (sector = 0; sector < img[drive].sectors; sector++)
			{
				if (img[drive].is_cqm)
				{
					if (img[drive].interleave)
					{
						sr = interleave(sector, img[drive].skew, img[drive].sectors);
					}
					else
					{
						sr = sector + 1;
						sr += img[drive].skew;
						if (sr > img[drive].sectors)
						{
							sr -= img[drive].sectors;
						}
					}
				}
				else
				{
					if (img[drive].gap3_size < 68)
					{
						sr = interleave(sector, 1, img[drive].sectors);
					}
					else
					{
						sr = img[drive].dmf ? (dmf_r[sector]) : (sector + 1);
					}
				}
				id[0] = track;
				id[1] = side;
				id[2] = sr;
				id[3] = img[drive].sector_size;
				img[drive].sector_pos_side[side][sr] = side;
				img[drive].sector_pos[side][sr] = (sr - 1) * ssize;
				current_pos = d86f_prepare_sector(drive, side, current_pos, id, &img[drive].track_data[side][(sr - 1) * ssize], ssize, img[drive].gap2_size, img[drive].gap3_size, 0, 0);
			}
		}
	}
	else
	{
		total = img[drive].sectors;
		img_pos = 0;
		sside = 0;

		/* Pass 1, get sector positions in the image. */
		for (sector = 0; sector < xdf_logical_sectors[current_xdft][!is_t0]; sector++)
		{
			if (is_t0)
			{
				img_pos = (sector % total) << 9;
				sside = (sector >= total) ? 1 : 0;
			}

			if (xdf_img_sector.word)
			{
				img[drive].sector_pos_side[xdf_img_sector.id.h][xdf_img_sector.id.r] = sside;
				img[drive].sector_pos[xdf_img_sector.id.h][xdf_img_sector.id.r] = img_pos;
			}

			if (!is_t0)
			{
				img_pos += (128 << (xdf_img_sector.id.r & 7));
				if (img_pos >= (total << 9))  sside = 1;
				img_pos %= (total << 9);
			}
		}

		/* Pass 2, prepare the actual track. */
		for (side = 0; side < img[drive].sides; side++)
		{
			current_pos = d86f_prepare_pretrack(drive, side, 0);

			for (sector = 0; sector < xdf_physical_sectors[current_xdft][!is_t0]; sector++)
			{
				array_sector = (side * xdf_physical_sectors[current_xdft][!is_t0]) + sector;

				buf_side = img[drive].sector_pos_side[xdf_disk_sector.id.h][xdf_disk_sector.id.r];
				buf_pos = img[drive].sector_pos[xdf_disk_sector.id.h][xdf_disk_sector.id.r];

				id[0] = track;
				id[1] = xdf_disk_sector.id.h;
				id[2] = xdf_disk_sector.id.r;

				if (is_t0)
				{
					id[3] = 2;
					current_pos = d86f_prepare_sector(drive, side, current_pos, id, &img[drive].track_data[buf_side][buf_pos], ssize, img[drive].gap2_size, xdf_gap3_sizes[current_xdft][!is_t0], 0, 0);
				}
				else
				{
					id[3] = id[2] & 7;
					ssize = (128 << id[3]);
					current_pos = d86f_prepare_sector(drive, side, xdf_trackx_spos[current_xdft][array_sector], id, &img[drive].track_data[buf_side][buf_pos], ssize, img[drive].gap2_size, xdf_gap3_sizes[current_xdft][!is_t0], 0, 0);
				}
			}
		}
	}
}

void img_writeback(int drive)
{
	int side;
	int ssize = 128 << ((int) img[drive].sector_size);

        if (!img[drive].f)
                return;

	if (img[drive].disk_at_once)
		return;
                
	fseek(img[drive].f, img[drive].base + (img[drive].track * img[drive].sectors * ssize * img[drive].sides), SEEK_SET);
	for (side = 0; side < img[drive].sides; side++)
	{
                fwrite(img[drive].track_data[side], img[drive].sectors * ssize, 1, img[drive].f);
	}
}

int img_xdf_type(int drive)
{
	return img[drive].xdf_type;
}

uint16_t img_disk_flags(int drive)
{
	return img[drive].disk_flags;
}

uint16_t img_side_flags(int drive)
{
	return img[drive].track_flags;
}

void img_set_sector(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n)
{
	img[drive].current_sector_pos_side = img[drive].sector_pos_side[h][r];
	img[drive].current_sector_pos = img[drive].sector_pos[h][r];
	return;
}

uint8_t img_poll_read_data(int drive, int side, uint16_t pos)
{
	return img[drive].track_data[img[drive].current_sector_pos_side][img[drive].current_sector_pos + pos];
}

void img_poll_write_data(int drive, int side, uint16_t pos, uint8_t data)
{
	img[drive].track_data[img[drive].current_sector_pos_side][img[drive].current_sector_pos + pos] = data;
}

int img_format_conditions(int drive)
{
	int temp = (fdc_get_format_sectors() == img[drive].sectors);
	temp = temp && (fdc_get_format_n() == img[drive].sector_size);
	temp = temp && (img[drive].xdf_type == 0);
	return temp;
}

void d86f_register_img(int drive)
{
	d86f_handler[drive].disk_flags = img_disk_flags;
	d86f_handler[drive].side_flags = img_side_flags;
	d86f_handler[drive].writeback = img_writeback;
	d86f_handler[drive].set_sector = img_set_sector;
	d86f_handler[drive].write_data = img_poll_write_data;
	d86f_handler[drive].format_conditions = img_format_conditions;
	d86f_handler[drive].extra_bit_cells = null_extra_bit_cells;
	d86f_handler[drive].encoded_data = common_encoded_data;
	d86f_handler[drive].read_revolution = common_read_revolution;
	d86f_handler[drive].index_hole_pos = null_index_hole_pos;
	d86f_handler[drive].get_raw_size = common_get_raw_size;
	d86f_handler[drive].check_crc = 1;
}
