/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of Tandy models 1000, 1000HX and 1000SL2.
 *
 * Version:	@(#)m_tandy.c	1.0.9	2018/10/22
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../io.h"
#include "../pit.h"
#include "../nmi.h"
#include "../mem.h"
#include "../rom.h"
#include "../timer.h"
#include "../device.h"
#include "../nvr.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../game/gameport.h"
#include "../keyboard.h"
#include "../sound/sound.h"
#include "../sound/snd_pssj.h"
#include "../sound/snd_sn76489.h"
#include "../video/video.h"
#include "../video/vid_cga_comp.h"
#include "machine.h"


#define TANDY_RGB	0
#define TANDY_COMPOSITE	1


enum {
    EEPROM_IDLE = 0,
    EEPROM_GET_OPERATION,
    EEPROM_READ,
    EEPROM_WRITE
};


typedef struct {
    mem_mapping_t	mapping;
    mem_mapping_t	vram_mapping;

    uint8_t		crtc[32];
    int			crtcreg;

    int			array_index;
    uint8_t		array[32];
    int			memctrl;
    uint8_t		mode, col;
    uint8_t		stat;

    uint8_t		*vram, *b8000;
    uint32_t		b8000_mask;
    uint32_t		b8000_limit;
    uint8_t		planar_ctrl;

    int			linepos,
			displine;
    int			sc, vc;
    int			dispon;
    int			con, coff,
			cursoron,
			blink;
    int64_t		vsynctime;
    int			vadj;
    uint16_t		ma, maback;

    int64_t		dispontime,
			dispofftime,
			vidtime;
    int			firstline,
			lastline;

    int			composite;
} t1kvid_t;

typedef struct {
    int			romset;
    wchar_t		*path;

    int			state;
    int			count;
    int			addr;
    int			clk;
    uint16_t		data;
    uint16_t		store[64];
} t1keep_t;

typedef struct {
    mem_mapping_t	ram_mapping;
    mem_mapping_t	rom_mapping;		/* SL2 */

    uint8_t		*rom;			/* SL2 */
    uint8_t		ram_bank;
    uint8_t		rom_bank;		/* SL2 */
    int			rom_offset;		/* SL2 */

    uint32_t		base;
    int			is_sl2;

    t1kvid_t		*vid;
} tandy_t;

static video_timings_t timing_dram     = {VIDEO_BUS, 0,0,0, 0,0,0}; /*No additional waitstates*/


static const scancode scancode_tandy[512] = {
    { {-1},       {-1}       }, { {0x01, -1}, {0x81, -1} },
    { {0x02, -1}, {0x82, -1} }, { {0x03, -1}, {0x83, -1} },
    { {0x04, -1}, {0x84, -1} }, { {0x05, -1}, {0x85, -1} },
    { {0x06, -1}, {0x86, -1} }, { {0x07, -1}, {0x87, -1} },
    { {0x08, -1}, {0x88, -1} }, { {0x09, -1}, {0x89, -1} },
    { {0x0a, -1}, {0x8a, -1} }, { {0x0b, -1}, {0x8b, -1} },
    { {0x0c, -1}, {0x8c, -1} }, { {0x0d, -1}, {0x8d, -1} },
    { {0x0e, -1}, {0x8e, -1} }, { {0x0f, -1}, {0x8f, -1} },
    { {0x10, -1}, {0x90, -1} }, { {0x11, -1}, {0x91, -1} },
    { {0x12, -1}, {0x92, -1} }, { {0x13, -1}, {0x93, -1} },
    { {0x14, -1}, {0x94, -1} }, { {0x15, -1}, {0x95, -1} },
    { {0x16, -1}, {0x96, -1} }, { {0x17, -1}, {0x97, -1} },
    { {0x18, -1}, {0x98, -1} }, { {0x19, -1}, {0x99, -1} },
    { {0x1a, -1}, {0x9a, -1} }, { {0x1b, -1}, {0x9b, -1} },
    { {0x1c, -1}, {0x9c, -1} }, { {0x1d, -1}, {0x9d, -1} },
    { {0x1e, -1}, {0x9e, -1} }, { {0x1f, -1}, {0x9f, -1} },
    { {0x20, -1}, {0xa0, -1} }, { {0x21, -1}, {0xa1, -1} },
    { {0x22, -1}, {0xa2, -1} }, { {0x23, -1}, {0xa3, -1} },
    { {0x24, -1}, {0xa4, -1} }, { {0x25, -1}, {0xa5, -1} },
    { {0x26, -1}, {0xa6, -1} }, { {0x27, -1}, {0xa7, -1} },
    { {0x28, -1}, {0xa8, -1} }, { {0x29, -1}, {0xa9, -1} },
    { {0x2a, -1}, {0xaa, -1} }, { {0x2b, -1}, {0xab, -1} },
    { {0x2c, -1}, {0xac, -1} }, { {0x2d, -1}, {0xad, -1} },
    { {0x2e, -1}, {0xae, -1} }, { {0x2f, -1}, {0xaf, -1} },
    { {0x30, -1}, {0xb0, -1} }, { {0x31, -1}, {0xb1, -1} },
    { {0x32, -1}, {0xb2, -1} }, { {0x33, -1}, {0xb3, -1} },
    { {0x34, -1}, {0xb4, -1} }, { {0x35, -1}, {0xb5, -1} },
    { {0x36, -1}, {0xb6, -1} }, { {0x37, -1}, {0xb7, -1} },
    { {0x38, -1}, {0xb8, -1} }, { {0x39, -1}, {0xb9, -1} },
    { {0x3a, -1}, {0xba, -1} }, { {0x3b, -1}, {0xbb, -1} },
    { {0x3c, -1}, {0xbc, -1} }, { {0x3d, -1}, {0xbd, -1} },
    { {0x3e, -1}, {0xbe, -1} }, { {0x3f, -1}, {0xbf, -1} },
    { {0x40, -1}, {0xc0, -1} }, { {0x41, -1}, {0xc1, -1} },
    { {0x42, -1}, {0xc2, -1} }, { {0x43, -1}, {0xc3, -1} },
    { {0x44, -1}, {0xc4, -1} }, { {0x45, -1}, {0xc5, -1} },
    { {0x46, -1}, {0xc6, -1} }, { {0x47, -1}, {0xc7, -1} },
    { {0x48, -1}, {0xc8, -1} }, { {0x49, -1}, {0xc9, -1} },
    { {0x4a, -1}, {0xca, -1} }, { {0x4b, -1}, {0xcb, -1} },
    { {0x4c, -1}, {0xcc, -1} }, { {0x4d, -1}, {0xcd, -1} },
    { {0x4e, -1}, {0xce, -1} }, { {0x4f, -1}, {0xcf, -1} },
    { {0x50, -1}, {0xd0, -1} }, { {0x51, -1}, {0xd1, -1} },
    { {0x52, -1}, {0xd2, -1} }, { {0x56, -1}, {0xd6, -1} },
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*054*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*058*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*05c*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*060*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*064*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*068*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*06c*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*070*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*074*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*078*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*07c*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*080*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*084*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*088*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*08c*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*090*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*094*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*098*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*09c*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0a0*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0a4*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0a8*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0ac*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0b0*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0b4*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0b8*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0bc*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0c0*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0c4*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0c8*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0cc*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0d0*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0d4*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0d8*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0dc*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0e0*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0e4*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0e8*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0ec*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0f0*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0f4*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0f8*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*0fc*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*100*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*104*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*108*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*10c*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*110*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*114*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*118*/
    { {0x57, -1}, {0xd7, -1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*11c*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*120*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*124*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*128*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*12c*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*130*/
    { {-1},             {-1} }, { {0x35, -1}, {0xb5, -1} },
    { {-1},             {-1} }, { {0x37, -1}, {0xb7, -1} },	/*134*/
    { {0x38, -1}, {0xb8, -1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*138*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*13c*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*140*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {0x46, -1}, {0xc6, -1} }, { {0x47, -1}, {0xc7, -1} },	/*144*/
    { {0x48, -1}, {0xc8, -1} }, { {0x49, -1}, {0xc9, -1} },
    { {-1},             {-1} }, { {0x4b, -1}, {0xcb, -1} },	/*148*/
    { {-1},             {-1} }, { {0x4d, -1}, {0xcd, -1} },
    { {-1},             {-1} }, { {0x4f, -1}, {0xcf, -1} },	/*14c*/
    { {0x50, -1}, {0xd0, -1} }, { {0x51, -1}, {0xd1, -1} },
    { {0x52, -1}, {0xd2, -1} }, { {0x53, -1}, {0xd3, -1} },	/*150*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*154*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*158*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*15c*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*160*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*164*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*168*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*16c*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*170*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*174*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*148*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*17c*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*180*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*184*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*88*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*18c*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*190*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*194*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*198*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*19c*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1a0*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1a4*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1a8*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1ac*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1b0*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1b4*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1b8*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1bc*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1c0*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1c4*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1c8*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1cc*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1d0*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1d4*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1d8*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1dc*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1e0*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1e4*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1e8*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1ec*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1f0*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1f4*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} },	/*1f8*/
    { {-1},             {-1} }, { {-1},             {-1} },
    { {-1},             {-1} }, { {-1},             {-1} }	/*1fc*/
};
static uint8_t crtcmask[32] = {
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f,
    0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static uint8_t crtcmask_sl[32] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0xff, 0xff,
    0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static int eep_data_out;


static uint8_t	vid_in(uint16_t addr, void *priv);
static void	vid_out(uint16_t addr, uint8_t val, void *priv);


#ifdef ENABLE_TANDY_LOG
int tandy_do_log = ENABLE_TANDY_LOG;


static void
tandy_log(const char *fmt, ...)
{
   va_list ap;

   if (tandy_do_log)
   {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
   }
}
#else
#define tandy_log(fmt, ...)
#endif


static void
recalc_mapping(tandy_t *dev)
{
    t1kvid_t *vid = dev->vid;

    mem_mapping_disable(&vid->mapping);
    io_removehandler(0x03d0, 16,
		     vid_in, NULL, NULL, vid_out, NULL, NULL, dev);

    if (vid->planar_ctrl & 4) {
	mem_mapping_enable(&vid->mapping);
	if (vid->array[5] & 1)
		mem_mapping_set_addr(&vid->mapping, 0xa0000, 0x10000);
	  else
		mem_mapping_set_addr(&vid->mapping, 0xb8000, 0x8000);
	io_sethandler(0x03d0, 16, vid_in,NULL,NULL, vid_out,NULL,NULL, dev);
    }
}


static void
recalc_timings(tandy_t *dev)
{
    t1kvid_t *vid = dev->vid;

    double _dispontime, _dispofftime, disptime;

    if (vid->mode & 1) {
	disptime = vid->crtc[0] + 1;
	_dispontime = vid->crtc[1];
    } else {
	disptime = (vid->crtc[0] + 1) << 1;
	_dispontime = vid->crtc[1] << 1;
    }

    _dispofftime = disptime - _dispontime;
    _dispontime  *= CGACONST;
    _dispofftime *= CGACONST;
    vid->dispontime  = (int64_t)(_dispontime  * (1 << TIMER_SHIFT));
    vid->dispofftime = (int64_t)(_dispofftime * (1 << TIMER_SHIFT));
}


static void
recalc_address(tandy_t *dev)
{
    t1kvid_t *vid = dev->vid;

    if ((vid->memctrl & 0xc0) == 0xc0) {
	vid->vram  = &ram[((vid->memctrl & 0x06) << 14) + dev->base];
	vid->b8000 = &ram[((vid->memctrl & 0x30) << 11) + dev->base];
	vid->b8000_mask = 0x7fff;
    } else {
	vid->vram  = &ram[((vid->memctrl & 0x07) << 14) + dev->base];
	vid->b8000 = &ram[((vid->memctrl & 0x38) << 11) + dev->base];
	vid->b8000_mask = 0x3fff;
    }
}


static void
recalc_address_sl(tandy_t *dev)
{
    t1kvid_t *vid = dev->vid;

    vid->b8000_limit = 0x8000;

    if (vid->array[5] & 1) {
	vid->vram  = &ram[((vid->memctrl & 0x04) << 14) + dev->base];
	vid->b8000 = &ram[((vid->memctrl & 0x20) << 11) + dev->base];
    } else if ((vid->memctrl & 0xc0) == 0xc0) {
	vid->vram  = &ram[((vid->memctrl & 0x06) << 14) + dev->base];
	vid->b8000 = &ram[((vid->memctrl & 0x30) << 11) + dev->base];
    } else {
	vid->vram  = &ram[((vid->memctrl & 0x07) << 14) + dev->base];
	vid->b8000 = &ram[((vid->memctrl & 0x38) << 11) + dev->base];
	if ((vid->memctrl & 0x38) == 0x38)
		vid->b8000_limit = 0x4000;
    }
}


static void
vid_out(uint16_t addr, uint8_t val, void *priv)
{
    tandy_t *dev = (tandy_t *)priv;
    t1kvid_t *vid = dev->vid;
    uint8_t old;

    switch (addr) {
	case 0x03d4:
		vid->crtcreg = val & 0x1f;
		break;

	case 0x03d5:
		old = vid->crtc[vid->crtcreg];
		if (dev->is_sl2)
			vid->crtc[vid->crtcreg] = val & crtcmask_sl[vid->crtcreg];
		  else
			vid->crtc[vid->crtcreg] = val & crtcmask[vid->crtcreg];
		if (old != val) {
			if (vid->crtcreg < 0xe || vid->crtcreg > 0x10) {
				fullchange = changeframecount;
				recalc_timings(dev);
			}
		}
		break;

	case 0x03d8:
		vid->mode = val;
		if (! dev->is_sl2)
			update_cga16_color(vid->mode);
		break;

	case 0x03d9:
		vid->col = val;
		break;

	case 0x03da:
		vid->array_index = val & 0x1f;
		break;

	case 0x03de:
		if (vid->array_index & 16) 
			val &= 0xf;
		vid->array[vid->array_index & 0x1f] = val;
		if (dev->is_sl2) {
			if ((vid->array_index & 0x1f) == 5) {
				recalc_mapping(dev);
				recalc_address_sl(dev);
			}
		}
		break;

	case 0x03df:
		vid->memctrl = val;
		if (dev->is_sl2)
			recalc_address_sl(dev);
		  else
			recalc_address(dev);
		break;

	case 0x0065:
		if (val == 8) return;	/*Hack*/
		vid->planar_ctrl = val;
		recalc_mapping(dev);
		break;
    }
}


static uint8_t
vid_in(uint16_t addr, void *priv)
{
    tandy_t *dev = (tandy_t *)priv;
    t1kvid_t *vid = dev->vid;
    uint8_t ret = 0xff;

    switch (addr) {
	case 0x03d4:
		ret = vid->crtcreg;
		break;

	case 0x03d5:
		ret = vid->crtc[vid->crtcreg];
		break;

	case 0x03da:
		ret = vid->stat;
		break;
    }

    return(ret);
}


static void
vid_write(uint32_t addr, uint8_t val, void *priv)
{
    tandy_t *dev = (tandy_t *)priv;
    t1kvid_t *vid = dev->vid;

    if (vid->memctrl == -1) return;

    egawrites++;
    if (dev->is_sl2) {
	if (vid->array[5] & 1)
		vid->b8000[addr & 0xffff] = val;
	  else {
		if ((addr & 0x7fff) < vid->b8000_limit)
			vid->b8000[addr & 0x7fff] = val;
	}
    } else {
	vid->b8000[addr & vid->b8000_mask] = val;
    }
}


static uint8_t
vid_read(uint32_t addr, void *priv)
{
    tandy_t *dev = (tandy_t *)priv;
    t1kvid_t *vid = dev->vid;

    if (vid->memctrl == -1) return(0xff);

    egareads++;
    if (dev->is_sl2) {
	if (vid->array[5] & 1)
		return(vid->b8000[addr & 0xffff]);
	if ((addr & 0x7fff) < vid->b8000_limit)
		return(vid->b8000[addr & 0x7fff]);
	  else
		return(0xff);
    } else {
	return(vid->b8000[addr & vid->b8000_mask]);
    }
}


static void
vid_poll(void *priv)
{
    tandy_t *dev = (tandy_t *)priv;
    t1kvid_t *vid = dev->vid;
    uint16_t ca = (vid->crtc[15] | (vid->crtc[14] << 8)) & 0x3fff;
    int drawcursor;
    int x, c;
    int oldvc;
    uint8_t chr, attr;
    uint16_t dat;
    int cols[4];
    int col;
    int oldsc;

    if (! vid->linepos) {
	vid->vidtime += vid->dispofftime;
	vid->stat |= 1;
	vid->linepos = 1;
	oldsc = vid->sc;
	if ((vid->crtc[8] & 3) == 3) 
		vid->sc = (vid->sc << 1) & 7;
	
	if (vid->dispon) {
		if (vid->displine < vid->firstline) {
			vid->firstline = vid->displine;
			video_wait_for_buffer();
		}
		vid->lastline = vid->displine;
		cols[0] = (vid->array[2] & 0xf) + 16;
		for (c = 0; c < 8; c++) {
			if (vid->array[3] & 4) {
				buffer->line[vid->displine][c] = cols[0];
				if (vid->mode & 1)
					buffer->line[vid->displine][c + (vid->crtc[1] << 3) + 8] = cols[0];
				  else
					buffer->line[vid->displine][c + (vid->crtc[1] << 4) + 8] = cols[0];
			} else if ((vid->mode & 0x12) == 0x12) {
				buffer->line[vid->displine][c] = 0;
				if (vid->mode & 1)
					buffer->line[vid->displine][c + (vid->crtc[1] << 3) + 8] = 0;
				  else
					buffer->line[vid->displine][c + (vid->crtc[1] << 4) + 8] = 0;
			} else {
				buffer->line[vid->displine][c] = (vid->col & 15) + 16;
				if (vid->mode & 1)
					buffer->line[vid->displine][c + (vid->crtc[1] << 3) + 8] = (vid->col & 15) + 16;
				  else
					buffer->line[vid->displine][c + (vid->crtc[1] << 4) + 8] = (vid->col & 15) + 16;
			}
		}

		if ((vid->array[3] & 0x10) && (vid->mode & 1)) { /*320x200x16*/
			for (x = 0; x < vid->crtc[1]; x++) {
				dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000)] << 8) | 
				       vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000) + 1];
				vid->ma++;
				buffer->line[vid->displine][(x << 3) + 8]  = 
					buffer->line[vid->displine][(x << 3) + 9]  = vid->array[((dat >> 12) & vid->array[1]) + 16] + 16;
				buffer->line[vid->displine][(x << 3) + 10] = 
					buffer->line[vid->displine][(x << 3) + 11] = vid->array[((dat >>  8) & vid->array[1]) + 16] + 16;
				buffer->line[vid->displine][(x << 3) + 12] = 
					buffer->line[vid->displine][(x << 3) + 13] = vid->array[((dat >>  4) & vid->array[1]) + 16] + 16;
				buffer->line[vid->displine][(x << 3) + 14] = 
					buffer->line[vid->displine][(x << 3) + 15] = vid->array[(dat & vid->array[1]) + 16] + 16;
			}
		} else if (vid->array[3] & 0x10) { /*160x200x16*/
			for (x = 0; x < vid->crtc[1]; x++) {
				dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000)] << 8) | 
				       vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000) + 1];
				vid->ma++;
				buffer->line[vid->displine][(x << 4) + 8]  = 
				buffer->line[vid->displine][(x << 4) + 9]  = 
				buffer->line[vid->displine][(x << 4) + 10] =
				buffer->line[vid->displine][(x << 4) + 11] = vid->array[((dat >> 12) & vid->array[1]) + 16] + 16;
				buffer->line[vid->displine][(x << 4) + 12] = 
				buffer->line[vid->displine][(x << 4) + 13] =
				buffer->line[vid->displine][(x << 4) + 14] =
				buffer->line[vid->displine][(x << 4) + 15] = vid->array[((dat >>  8) & vid->array[1]) + 16] + 16;
				buffer->line[vid->displine][(x << 4) + 16] = 
				buffer->line[vid->displine][(x << 4) + 17] =
				buffer->line[vid->displine][(x << 4) + 18] =
				buffer->line[vid->displine][(x << 4) + 19] = vid->array[((dat >>  4) & vid->array[1]) + 16] + 16;
				buffer->line[vid->displine][(x << 4) + 20] = 
				buffer->line[vid->displine][(x << 4) + 21] =
				buffer->line[vid->displine][(x << 4) + 22] =
				buffer->line[vid->displine][(x << 4) + 23] = vid->array[(dat & vid->array[1]) + 16] + 16;
			}
		} else if (vid->array[3] & 0x08) { /*640x200x4 - this implementation is a complete guess!*/
			for (x = 0; x < vid->crtc[1]; x++) {
				dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000)] << 8) |
				       vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000) + 1];
				vid->ma++;
				for (c = 0; c < 8; c++) {
					chr  =  (dat >>  7) & 1;
					chr |= ((dat >> 14) & 2);
					buffer->line[vid->displine][(x << 3) + 8 + c] = vid->array[(chr & vid->array[1]) + 16] + 16;
					dat <<= 1;
				}
			}
		} else if (vid->mode & 1) {
			for (x = 0; x < vid->crtc[1]; x++) {
				chr  = vid->vram[ (vid->ma << 1) & 0x3fff];
				attr = vid->vram[((vid->ma << 1) + 1) & 0x3fff];
				drawcursor = ((vid->ma == ca) && vid->con && vid->cursoron);
				if (vid->mode & 0x20) {
					cols[1] = vid->array[ ((attr & 15) & vid->array[1]) + 16] + 16;
					cols[0] = vid->array[(((attr >> 4) & 7) & vid->array[1]) + 16] + 16;
					if ((vid->blink & 16) && (attr & 0x80) && !drawcursor) 
						cols[1] = cols[0];
				} else {
					cols[1] = vid->array[((attr & 15) & vid->array[1]) + 16] + 16;
					cols[0] = vid->array[((attr >> 4) & vid->array[1]) + 16] + 16;
				}
				if (vid->sc & 8) {
					for (c = 0; c < 8; c++)
						buffer->line[vid->displine][(x << 3) + c + 8] = cols[0];
				} else {
					for (c = 0; c < 8; c++)
						buffer->line[vid->displine][(x << 3) + c + 8] = cols[(fontdat[chr][vid->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
				}
				if (drawcursor) {
					for (c = 0; c < 8; c++)
						buffer->line[vid->displine][(x << 3) + c + 8] ^= 15;
				}
				vid->ma++;
			}
		} else if (! (vid->mode & 2)) {
			for (x = 0; x < vid->crtc[1]; x++) {
				chr  = vid->vram[ (vid->ma << 1)      & 0x3fff];
				attr = vid->vram[((vid->ma << 1) + 1) & 0x3fff];
				drawcursor = ((vid->ma == ca) && vid->con && vid->cursoron);
				if (vid->mode & 0x20) {
					cols[1] = vid->array[ ((attr & 15) & vid->array[1]) + 16] + 16;
					cols[0] = vid->array[(((attr >> 4) & 7) & vid->array[1]) + 16] + 16;
					if ((vid->blink & 16) && (attr & 0x80) && !drawcursor) 
						cols[1] = cols[0];
				} else {
					cols[1] = vid->array[((attr & 15) & vid->array[1]) + 16] + 16;
					cols[0] = vid->array[((attr >> 4) & vid->array[1]) + 16] + 16;
				}
				vid->ma++;
				if (vid->sc & 8) {
					for (c = 0; c < 8; c++)
						buffer->line[vid->displine][(x << 4) + (c << 1) + 8] = 
							buffer->line[vid->displine][(x << 4) + (c << 1) + 1 + 8] = cols[0];
				} else {
					for (c = 0; c < 8; c++)
						buffer->line[vid->displine][(x << 4) + (c << 1) + 8] = 
							buffer->line[vid->displine][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdat[chr][vid->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
				}
				if (drawcursor) {
					for (c = 0; c < 16; c++)
						buffer->line[vid->displine][(x << 4) + c + 8] ^= 15;
				}
			}
		} else if (! (vid->mode& 16)) {
			cols[0] = (vid->col & 15) | 16;
			col = (vid->col & 16) ? 24 : 16;
			if (vid->mode & 4) {
				cols[1] = col | 3;
				cols[2] = col | 4;
				cols[3] = col | 7;
			} else if (vid->col & 32) {
				cols[1] = col | 3;
				cols[2] = col | 5;
				cols[3] = col | 7;
			} else {
				cols[1] = col | 2;
				cols[2] = col | 4;
				cols[3] = col | 6;
			}
			for (x = 0; x < vid->crtc[1]; x++) {
				dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000)] << 8) | 
				       vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000) + 1];
				vid->ma++;
				for (c = 0; c < 8; c++) {
					buffer->line[vid->displine][(x << 4) + (c << 1) + 8] =
					buffer->line[vid->displine][(x << 4) + (c << 1) + 1 + 8] = cols[dat >> 14];
					dat <<= 2;
				}
			}
		} else {
			cols[0] = 0; 
			cols[1] = vid->array[(vid->col & vid->array[1]) + 16] + 16;
			for (x = 0; x < vid->crtc[1]; x++) {
				dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000)] << 8) |
				       vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000) + 1];
				vid->ma++;
				for (c = 0; c < 16; c++) {
					buffer->line[vid->displine][(x << 4) + c + 8] = cols[dat >> 15];
					dat <<= 1;
				}
			}
		}
	} else {
		if (vid->array[3] & 4) {
			if (vid->mode & 1)
				hline(buffer, 0, vid->displine, (vid->crtc[1] << 3) + 16, (vid->array[2] & 0xf) + 16);
			  else
				hline(buffer, 0, vid->displine, (vid->crtc[1] << 4) + 16, (vid->array[2] & 0xf) + 16);
		} else {
			cols[0] = ((vid->mode & 0x12) == 0x12) ? 0 : (vid->col & 0xf) + 16;
			if (vid->mode & 1)
				hline(buffer, 0, vid->displine, (vid->crtc[1] << 3) + 16, cols[0]);
			  else
				hline(buffer, 0, vid->displine, (vid->crtc[1] << 4) + 16, cols[0]);
		}
	}

	if (vid->mode & 1)
		x = (vid->crtc[1] << 3) + 16;
	  else
		x = (vid->crtc[1] << 4) + 16;
	if (vid->composite) {
		for (c = 0; c < x; c++)
			buffer32->line[vid->displine][c] = buffer->line[vid->displine][c] & 0xf;

		Composite_Process(vid->mode, 0, x >> 2, buffer32->line[vid->displine]);
	}
	vid->sc = oldsc;
	if (vid->vc == vid->crtc[7] && !vid->sc)
		vid->stat |= 8;
	vid->displine++;
	if (vid->displine >= 360) 
		vid->displine = 0;
    } else {
	vid->vidtime += vid->dispontime;
	if (vid->dispon) 
		vid->stat &= ~1;
	vid->linepos = 0;
	if (vid->vsynctime) {
		vid->vsynctime--;
		if (! vid->vsynctime)
			vid->stat &= ~8;
	}
	if (vid->sc == (vid->crtc[11] & 31) || ((vid->crtc[8] & 3) == 3 && vid->sc == ((vid->crtc[11] & 31) >> 1))) { 
		vid->con = 0; 
		vid->coff = 1; 
	}
	if (vid->vadj) {
		vid->sc++;
		vid->sc &= 31;
		vid->ma = vid->maback;
		vid->vadj--;
		if (! vid->vadj) {
			vid->dispon = 1;
			vid->ma = vid->maback = (vid->crtc[13] | (vid->crtc[12] << 8)) & 0x3fff;
			vid->sc = 0;
		}
	} else if (vid->sc == vid->crtc[9] || ((vid->crtc[8] & 3) == 3 && vid->sc == (vid->crtc[9] >> 1))) {
		vid->maback = vid->ma;
		vid->sc = 0;
		oldvc = vid->vc;
		vid->vc++;
		vid->vc &= 127;
		if (vid->vc == vid->crtc[6]) 
			vid->dispon = 0;
		if (oldvc == vid->crtc[4]) {
			vid->vc = 0;
			vid->vadj = vid->crtc[5];
			if (! vid->vadj) 
				vid->dispon = 1;
			if (! vid->vadj) 
				vid->ma = vid->maback = (vid->crtc[13] | (vid->crtc[12] << 8)) & 0x3fff;
			if ((vid->crtc[10] & 0x60) == 0x20)
				vid->cursoron = 0;
			  else
				vid->cursoron = vid->blink & 16;
		}
		if (vid->vc == vid->crtc[7]) {
			vid->dispon = 0;
			vid->displine = 0;
			vid->vsynctime = 16;
			if (vid->crtc[7]) {
				if (vid->mode & 1)
					x = (vid->crtc[1] << 3) + 16;
				  else
					x = (vid->crtc[1] << 4) + 16;
				vid->lastline++;
				if ((x != xsize) || ((vid->lastline - vid->firstline) != ysize) || video_force_resize_get()) {
					xsize = x;
					ysize = vid->lastline - vid->firstline;
					if (xsize < 64) xsize = 656;
					if (ysize < 32) ysize = 200;
						set_screen_size(xsize, (ysize << 1) + 16);
					if (video_force_resize_get())
						video_force_resize_set(0);
				}
				if (vid->composite) 
				   video_blit_memtoscreen(0, vid->firstline-4, 0, (vid->lastline - vid->firstline) + 8, xsize, (vid->lastline - vid->firstline) + 8);
				else	  
				   video_blit_memtoscreen_8(0, vid->firstline-4, 0, (vid->lastline - vid->firstline) + 8, xsize, (vid->lastline - vid->firstline) + 8);

				frames++;

				video_res_x = xsize - 16;
				video_res_y = ysize;
				if ((vid->array[3] & 0x10) && (vid->mode & 1)) { /*320x200x16*/
					video_res_x /= 2;
					video_bpp = 4;
				} else if (vid->array[3] & 0x10) { /*160x200x16*/
					video_res_x /= 4;
					video_bpp = 4;
				} else if (vid->array[3] & 0x08) { /*640x200x4 - this implementation is a complete guess!*/
					video_bpp = 2;
				} else if (vid->mode & 1) {
					video_res_x /= 8;
					video_res_y /= vid->crtc[9] + 1;
					video_bpp = 0;
				} else if (! (vid->mode & 2)) {
					video_res_x /= 16;
					video_res_y /= vid->crtc[9] + 1;
					video_bpp = 0;
				} else if (! (vid->mode & 16)) {
					video_res_x /= 2;
					video_bpp = 2;
				} else {
				   video_bpp = 1;
				}
			}
			vid->firstline = 1000;
			vid->lastline = 0;
			vid->blink++;
		}
	} else {
		vid->sc++;
		vid->sc &= 31;
		vid->ma = vid->maback;
	}
	if ((vid->sc == (vid->crtc[10] & 31) || ((vid->crtc[8] & 3) == 3 && vid->sc == ((vid->crtc[10] & 31) >> 1)))) 
		vid->con = 1;
    }
}


static void
vid_poll_sl(void *priv)
{
    tandy_t *dev = (tandy_t *)priv;
    t1kvid_t *vid = dev->vid;
    uint16_t ca = (vid->crtc[15] | (vid->crtc[14] << 8)) & 0x3fff;
    int drawcursor;
    int x, c;
    int oldvc;
    uint8_t chr, attr;
    uint16_t dat;
    int cols[4];
    int col;
    int oldsc;

    if (! vid->linepos) {
	vid->vidtime += vid->dispofftime;
	vid->stat |= 1;
	vid->linepos = 1;
	oldsc = vid->sc;
	if ((vid->crtc[8] & 3) == 3) 
		vid->sc = (vid->sc << 1) & 7;
	if (vid->dispon) {
		if (vid->displine < vid->firstline) {
			vid->firstline = vid->displine;
			video_wait_for_buffer();
		}
		vid->lastline = vid->displine;
		cols[0] = (vid->array[2] & 0xf) + 16;
		for (c = 0; c < 8; c++) {
			if (vid->array[3] & 4) {
				buffer->line[vid->displine][c] = cols[0];
				if (vid->mode & 1)
					buffer->line[vid->displine][c + (vid->crtc[1] << 3) + 8] = cols[0];
				  else
					buffer->line[vid->displine][c + (vid->crtc[1] << 4) + 8] = cols[0];
			} else if ((vid->mode & 0x12) == 0x12) {
				buffer->line[vid->displine][c] = 0;
				if (vid->mode & 1)
					buffer->line[vid->displine][c + (vid->crtc[1] << 3) + 8] = 0;
				  else
					buffer->line[vid->displine][c + (vid->crtc[1] << 4) + 8] = 0;
			} else {
				buffer->line[vid->displine][c] = (vid->col & 15) + 16;
				if (vid->mode & 1)
					buffer->line[vid->displine][c + (vid->crtc[1] << 3) + 8] = (vid->col & 15) + 16;
				  else
					buffer->line[vid->displine][c + (vid->crtc[1] << 4) + 8] = (vid->col & 15) + 16;
			}
		}
		if (vid->array[5] & 1) { /*640x200x16*/
			for (x = 0; x < vid->crtc[1]*2; x++) {
				dat = (vid->vram[(vid->ma << 1) & 0xffff] << 8) | 
				       vid->vram[((vid->ma << 1) + 1) & 0xffff];
				vid->ma++;
				buffer->line[vid->displine][(x << 2) + 8]  = vid->array[((dat >> 12) & 0xf)/*vid->array[1])*/ + 16] + 16;
				buffer->line[vid->displine][(x << 2) + 9]  = vid->array[((dat >>  8) & 0xf)/*vid->array[1])*/ + 16] + 16;
				buffer->line[vid->displine][(x << 2) + 10] = vid->array[((dat >>  4) & 0xf)/*vid->array[1])*/ + 16] + 16;
				buffer->line[vid->displine][(x << 2) + 11] = vid->array[(dat & 0xf)/*vid->array[1])*/ + 16] + 16;
			}
		} else if ((vid->array[3] & 0x10) && (vid->mode & 1)) { /*320x200x16*/
			for (x = 0; x < vid->crtc[1]; x++) {
				dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000)] << 8) | 
				       vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000) + 1];
				vid->ma++;
				buffer->line[vid->displine][(x << 3) + 8]  = 
				buffer->line[vid->displine][(x << 3) + 9]  = vid->array[((dat >> 12) & vid->array[1]) + 16] + 16;
				buffer->line[vid->displine][(x << 3) + 10] = 
				buffer->line[vid->displine][(x << 3) + 11] = vid->array[((dat >>  8) & vid->array[1]) + 16] + 16;
				buffer->line[vid->displine][(x << 3) + 12] = 
				buffer->line[vid->displine][(x << 3) + 13] = vid->array[((dat >>  4) & vid->array[1]) + 16] + 16;
				buffer->line[vid->displine][(x << 3) + 14] = 
				buffer->line[vid->displine][(x << 3) + 15] = vid->array[(dat & vid->array[1]) + 16] + 16;
			}
		} else if (vid->array[3] & 0x10) { /*160x200x16*/
			for (x = 0; x < vid->crtc[1]; x++) {
				dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000)] << 8) | 
				       vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000) + 1];
				vid->ma++;
				buffer->line[vid->displine][(x << 4) + 8]  = 
				buffer->line[vid->displine][(x << 4) + 9]  = 
				buffer->line[vid->displine][(x << 4) + 10] =
				buffer->line[vid->displine][(x << 4) + 11] = vid->array[((dat >> 12) & vid->array[1]) + 16] + 16;
				buffer->line[vid->displine][(x << 4) + 12] = 
				buffer->line[vid->displine][(x << 4) + 13] =
				buffer->line[vid->displine][(x << 4) + 14] =
				buffer->line[vid->displine][(x << 4) + 15] = vid->array[((dat >>  8) & vid->array[1]) + 16] + 16;
				buffer->line[vid->displine][(x << 4) + 16] = 
				buffer->line[vid->displine][(x << 4) + 17] =
				buffer->line[vid->displine][(x << 4) + 18] =
				buffer->line[vid->displine][(x << 4) + 19] = vid->array[((dat >>  4) & vid->array[1]) + 16] + 16;
				buffer->line[vid->displine][(x << 4) + 20] = 
				buffer->line[vid->displine][(x << 4) + 21] =
				buffer->line[vid->displine][(x << 4) + 22] =
				buffer->line[vid->displine][(x << 4) + 23] = vid->array[(dat & vid->array[1]) + 16] + 16;
			}
		} else if (vid->array[3] & 0x08) { /*640x200x4 - this implementation is a complete guess!*/
			for (x = 0; x < vid->crtc[1]; x++) {
				dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000)] << 8) |
				       vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000) + 1];
				vid->ma++;
				for (c = 0; c < 8; c++) {
					chr  =  (dat >>  7) & 1;
					chr |= ((dat >> 14) & 2);
					buffer->line[vid->displine][(x << 3) + 8 + c] = vid->array[(chr & vid->array[1]) + 16] + 16;
					dat <<= 1;
				}
			}
		} else if (vid->mode & 1) {
			for (x = 0; x < vid->crtc[1]; x++) {
				chr  = vid->vram[ (vid->ma << 1)      & 0x3fff];
				attr = vid->vram[((vid->ma << 1) + 1) & 0x3fff];
				drawcursor = ((vid->ma == ca) && vid->con && vid->cursoron);
				if (vid->mode & 0x20) {
					cols[1] = vid->array[ ((attr & 15) & vid->array[1]) + 16] + 16;
					cols[0] = vid->array[(((attr >> 4) & 7) & vid->array[1]) + 16] + 16;
					if ((vid->blink & 16) && (attr & 0x80) && !drawcursor) 
						cols[1] = cols[0];
				} else {
					cols[1] = vid->array[((attr & 15) & vid->array[1]) + 16] + 16;
					cols[0] = vid->array[((attr >> 4) & vid->array[1]) + 16] + 16;
				}
				if (vid->sc & 8) {
					for (c = 0; c < 8; c++)
						buffer->line[vid->displine][(x << 3) + c + 8] = cols[0];
				} else {
					for (c = 0; c < 8; c++)
						buffer->line[vid->displine][(x << 3) + c + 8] = cols[(fontdat[chr][vid->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
				}
				if (drawcursor) {
					for (c = 0; c < 8; c++)
						buffer->line[vid->displine][(x << 3) + c + 8] ^= 15;
				}
				vid->ma++;
			}
		} else if (! (vid->mode & 2)) {
			for (x = 0; x < vid->crtc[1]; x++) {
				chr  = vid->vram[ (vid->ma << 1)      & 0x3fff];
				attr = vid->vram[((vid->ma << 1) + 1) & 0x3fff];
				drawcursor = ((vid->ma == ca) && vid->con && vid->cursoron);
				if (vid->mode & 0x20) {
					cols[1] = vid->array[ ((attr & 15) & vid->array[1]) + 16] + 16;
					cols[0] = vid->array[(((attr >> 4) & 7) & vid->array[1]) + 16] + 16;
					if ((vid->blink & 16) && (attr & 0x80) && !drawcursor) 
						cols[1] = cols[0];
				} else {
					cols[1] = vid->array[((attr & 15) & vid->array[1]) + 16] + 16;
					cols[0] = vid->array[((attr >> 4) & vid->array[1]) + 16] + 16;
				}
				vid->ma++;
				if (vid->sc & 8) {
					for (c = 0; c < 8; c++)
						buffer->line[vid->displine][(x << 4) + (c << 1) + 8] = 
							buffer->line[vid->displine][(x << 4) + (c << 1) + 1 + 8] = cols[0];
				} else 	{
					for (c = 0; c < 8; c++)
						buffer->line[vid->displine][(x << 4) + (c << 1) + 8] = 
							buffer->line[vid->displine][(x << 4) + (c << 1) + 1 + 8] = cols[(fontdat[chr][vid->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
				}
				if (drawcursor) {
					for (c = 0; c < 16; c++)
						buffer->line[vid->displine][(x << 4) + c + 8] ^= 15;
				}
			}
		} else if (! (vid->mode & 16)) {
			cols[0] = (vid->col & 15) | 16;
			col = (vid->col & 16) ? 24 : 16;
			if (vid->mode & 4) {
				cols[1] = col | 3;
				cols[2] = col | 4;
				cols[3] = col | 7;
			} else if (vid->col & 32) {
				cols[1] = col | 3;
				cols[2] = col | 5;
				cols[3] = col | 7;
			} else {
				cols[1] = col | 2;
				cols[2] = col | 4;
				cols[3] = col | 6;
			}

			for (x = 0; x < vid->crtc[1]; x++) {
				dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000)] << 8) | 
				       vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000) + 1];
				vid->ma++;
				for (c = 0; c < 8; c++) {
					buffer->line[vid->displine][(x << 4) + (c << 1) + 8] =
					buffer->line[vid->displine][(x << 4) + (c << 1) + 1 + 8] = cols[dat >> 14];
					dat <<= 2;
				}
			}
		} else {
			cols[0] = 0; 
			cols[1] = vid->array[(vid->col & vid->array[1]) + 16] + 16;
			for (x = 0; x < vid->crtc[1]; x++) {
				dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000)] << 8) |
				       vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000) + 1];
				vid->ma++;
				for (c = 0; c < 16; c++) {
					buffer->line[vid->displine][(x << 4) + c + 8] = cols[dat >> 15];
					dat <<= 1;
				}
			}
		}
	} else {
		if (vid->array[3] & 4) {
			if (vid->mode & 1)
				hline(buffer, 0, vid->displine, (vid->crtc[1] << 3) + 16, (vid->array[2] & 0xf) + 16);
			  else
				hline(buffer, 0, vid->displine, (vid->crtc[1] << 4) + 16, (vid->array[2] & 0xf) + 16);
		} else {
			cols[0] = ((vid->mode & 0x12) == 0x12) ? 0 : (vid->col & 0xf) + 16;
			if (vid->mode & 1)
				hline(buffer, 0, vid->displine, (vid->crtc[1] << 3) + 16, cols[0]);
			  else
				hline(buffer, 0, vid->displine, (vid->crtc[1] << 4) + 16, cols[0]);
		}
	}

	if (vid->mode & 1)
		x = (vid->crtc[1] << 3) + 16;
	  else
		x = (vid->crtc[1] << 4) + 16;
	vid->sc = oldsc;

	if (vid->vc == vid->crtc[7] && !vid->sc)
		vid->stat |= 8;
	vid->displine++;
	if (vid->displine >= 360) 
		vid->displine = 0;
    } else {
	vid->vidtime += vid->dispontime;
	if (vid->dispon) 
		vid->stat &= ~1;
	vid->linepos = 0;
	if (vid->vsynctime) {
		vid->vsynctime--;
		if (! vid->vsynctime)
			vid->stat &= ~8;
	}
	if (vid->sc == (vid->crtc[11] & 31) || ((vid->crtc[8] & 3) == 3 && vid->sc == ((vid->crtc[11] & 31) >> 1))) { 
		vid->con = 0; 
		vid->coff = 1; 
	}
	if (vid->vadj) {
		vid->sc++;
		vid->sc &= 31;
		vid->ma = vid->maback;
		vid->vadj--;
		if (! vid->vadj) {
			vid->dispon = 1;
			if (vid->array[5] & 1)
				vid->ma = vid->maback = vid->crtc[13] | (vid->crtc[12] << 8);
			  else
				vid->ma = vid->maback = (vid->crtc[13] | (vid->crtc[12] << 8)) & 0x3fff;
			vid->sc = 0;
		}
	} else if (vid->sc == vid->crtc[9] || ((vid->crtc[8] & 3) == 3 && vid->sc == (vid->crtc[9] >> 1))) {
		vid->maback = vid->ma;
		vid->sc = 0;
		oldvc = vid->vc;
		vid->vc++;
		vid->vc &= 255;
		if (vid->vc == vid->crtc[6]) 
			vid->dispon = 0;
		if (oldvc == vid->crtc[4]) {
			vid->vc = 0;
			vid->vadj = vid->crtc[5];
			if (! vid->vadj) 
				vid->dispon = 1;
			if (! vid->vadj) {
				if (vid->array[5] & 1)
					vid->ma = vid->maback = vid->crtc[13] | (vid->crtc[12] << 8);
				  else
					vid->ma = vid->maback = (vid->crtc[13] | (vid->crtc[12] << 8)) & 0x3fff;
			}
			if ((vid->crtc[10] & 0x60) == 0x20)
				vid->cursoron = 0;
			  else
				vid->cursoron = vid->blink & 16;
		}
		if (vid->vc == vid->crtc[7]) {
			vid->dispon = 0;
			vid->displine = 0;
			vid->vsynctime = 16;
			if (vid->crtc[7]) {
				if (vid->mode & 1)
					x = (vid->crtc[1] << 3) + 16;
				  else
					x = (vid->crtc[1] << 4) + 16;
				vid->lastline++;
				if ((x != xsize) || ((vid->lastline - vid->firstline) != ysize) || video_force_resize_get()) {
					xsize = x;
					ysize = vid->lastline - vid->firstline;
					if (xsize < 64) xsize = 656;
					if (ysize < 32) ysize = 200;

					set_screen_size(xsize, (ysize << 1) + 16);
					if (video_force_resize_get())
						video_force_resize_set(0);
				}

				video_blit_memtoscreen_8(0, vid->firstline-4, 0, (vid->lastline - vid->firstline) + 8, xsize, (vid->lastline - vid->firstline) + 8);

				frames++;
				video_res_x = xsize - 16;
				video_res_y = ysize;
				if ((vid->array[3] & 0x10) && (vid->mode & 1)) { /*320x200x16*/
					video_res_x /= 2;
						video_bpp = 4;
				} else if (vid->array[3] & 0x10) { /*160x200x16*/
					video_res_x /= 4;
					video_bpp = 4;
				} else if (vid->array[3] & 0x08) { /*640x200x4 - this implementation is a complete guess!*/
				   video_bpp = 2;
				} else if (vid->mode & 1) {
					video_res_x /= 8;
					video_res_y /= vid->crtc[9] + 1;
					video_bpp = 0;
				} else if (! (vid->mode & 2)) {
					video_res_x /= 16;
					video_res_y /= vid->crtc[9] + 1;
					video_bpp = 0;
				} else if (! (vid->mode & 16)) {
					video_res_x /= 2;
					video_bpp = 2;
				} else {
				   video_bpp = 1;
				}
			}
			vid->firstline = 1000;
			vid->lastline = 0;
			vid->blink++;
		}
	} else {
		vid->sc++;
		vid->sc &= 31;
		vid->ma = vid->maback;
	}
	if ((vid->sc == (vid->crtc[10] & 31) || ((vid->crtc[8] & 3) == 3 && vid->sc == ((vid->crtc[10] & 31) >> 1)))) 
		vid->con = 1;
    }
}


static void
vid_speed_changed(void *priv)
{
    tandy_t *dev = (tandy_t *)priv;

    recalc_timings(dev);
}


static void
vid_close(void *priv)
{
    tandy_t *dev = (tandy_t *)priv;

    free(dev->vid);
    dev->vid = NULL;
}


static void
vid_init(tandy_t *dev)
{
    int display_type;
    t1kvid_t *vid;

    vid = malloc(sizeof(t1kvid_t));
    memset(vid, 0x00, sizeof(t1kvid_t));
    vid->memctrl = -1;
    dev->vid = vid;

    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_dram);

    display_type = machine_get_config_int("display_type");
    vid->composite = (display_type != TANDY_RGB);

    cga_comp_init(1);

    if (dev->is_sl2) {
	vid->b8000_limit = 0x8000;
	vid->planar_ctrl = 4;
	overscan_x = overscan_y = 16;

	io_sethandler(0x0065, 1, vid_in,NULL,NULL, vid_out,NULL,NULL, dev);

	timer_add(vid_poll_sl, &vid->vidtime, TIMER_ALWAYS_ENABLED, dev);
    } else {
	vid->b8000_mask = 0x3fff;

	timer_add(vid_poll, &vid->vidtime, TIMER_ALWAYS_ENABLED, dev);
    }
    mem_mapping_add(&vid->mapping, 0xb8000, 0x08000,
		    vid_read,NULL,NULL, vid_write,NULL,NULL, NULL, 0, dev);
    io_sethandler(0x03d0, 16,
		  vid_in,NULL,NULL, vid_out,NULL,NULL, dev);
}


static const device_config_t vid_config[] = {
    {
	"display_type", "Display type", CONFIG_SELECTION, "", TANDY_RGB,
	{
		{
			"RGB", TANDY_RGB
		},
		{
			"Composite", TANDY_COMPOSITE
		},
		{
			""
		}
	}
    },
    {
	"", "", -1
    }
};


static const device_t vid_device = {
    "Tandy 1000",
    0, 0,
    NULL, vid_close, NULL,
    NULL,
    vid_speed_changed,
    NULL,
    vid_config
};

static const device_t vid_device_hx = {
    "Tandy 1000 HX",
    0, 0,
    NULL, vid_close, NULL,
    NULL,
    vid_speed_changed,
    NULL,
    vid_config
};

static const device_t vid_device_sl = {
    "Tandy 1000SL2",
    0, 1,
    NULL, vid_close, NULL,
    NULL,
    vid_speed_changed,
    NULL,
    NULL
};


const device_t *
tandy1k_get_device(void)
{
    return &vid_device;
}


const device_t *
tandy1k_hx_get_device(void)
{
    return &vid_device_hx;
}


static void
eep_write(uint16_t addr, uint8_t val, void *priv)
{
    t1keep_t *eep = (t1keep_t *)priv;

    if ((val & 4) && !eep->clk) switch (eep->state) {
	case EEPROM_IDLE:
		switch (eep->count) {
			case 0:
				if (! (val & 3))
					eep->count = 1;
				  else
					eep->count = 0;
				break;

			case 1:
				if ((val & 3) == 2)
					eep->count = 2;
				  else
					eep->count = 0;
				break;

			case 2:
				if ((val & 3) == 3)
					eep->state = EEPROM_GET_OPERATION;
				eep->count = 0;
				break;
		}
		break;

	case EEPROM_GET_OPERATION:
		eep->data = (eep->data << 1) | (val & 1);
		eep->count++;
		if (eep->count == 8) {
			eep->count = 0;
			eep->addr = eep->data & 0x3f;
			switch (eep->data & 0xc0) {
				case 0x40:
					eep->state = EEPROM_WRITE;
					break;

				case 0x80:
					eep->state = EEPROM_READ;
					eep->data = eep->store[eep->addr];
					break;

				default:
					eep->state = EEPROM_IDLE;
					break;
			}
		}
		break;

	case EEPROM_READ:
		eep_data_out = eep->data & 0x8000;
		eep->data <<= 1;
		eep->count++;
		if (eep->count == 16) {
			eep->count = 0;
			eep->state = EEPROM_IDLE;
		}
		break;

	case EEPROM_WRITE:
		eep->data = (eep->data << 1) | (val & 1);
		eep->count++;
		if (eep->count == 16) {
			eep->count = 0;
			eep->state = EEPROM_IDLE;
			eep->store[eep->addr] = eep->data;
		}
		break;
    }

    eep->clk = val & 4;
}


static void *
eep_init(const device_t *info)
{
    t1keep_t *eep;
    FILE *f = NULL;

    eep = (t1keep_t *)malloc(sizeof(t1keep_t));
    memset(eep, 0x00, sizeof(t1keep_t));
    eep->romset = romset;
    switch (romset) {
	case ROM_TANDY1000HX:
		eep->path = L"tandy1000hx.bin";
		break;

	case ROM_TANDY1000SL2:
		eep->path = L"tandy1000sl2.bin";
		break;

    }

    f = nvr_fopen(eep->path, L"rb");
    if (f != NULL) {
	fread(eep->store, 128, 1, f);
	(void)fclose(f);
    }

    io_sethandler(0x037c, 1, NULL,NULL,NULL, eep_write,NULL,NULL, eep);

    return(eep);
}


static void
eep_close(void *priv)
{
    t1keep_t *eep = (t1keep_t *)priv;
    FILE *f = NULL;

    f = nvr_fopen(eep->path, L"rb");
    if (f != NULL) {
	(void)fwrite(eep->store, 128, 1, f);
	(void)fclose(f);
    }

    free(eep);
}


static const device_t eep_device = {
    "Tandy 1000 EEPROM",
    0, 0,
    eep_init, eep_close, NULL,
    NULL, NULL, NULL,
    NULL
};


static void
tandy_write(uint16_t addr, uint8_t val, void *priv)
{
    tandy_t *dev = (tandy_t *)priv;

    switch (addr) {
	case 0x00a0:
		mem_mapping_set_addr(&dev->ram_mapping,
				     ((val >> 1) & 7) * 128 * 1024, 0x20000);
		dev->ram_bank = val;
		break;

	case 0xffe8:
		if ((val & 0xe) == 0xe)
			mem_mapping_disable(&dev->ram_mapping);
		  else
			mem_mapping_set_addr(&dev->ram_mapping,
					     ((val >> 1) & 7) * 128 * 1024,
					     0x20000);
		recalc_address_sl(dev);
		dev->ram_bank = val;
		break;

	case 0xffea:
		dev->rom_bank = val;
		dev->rom_offset = ((val ^ 4) & 7) * 0x10000;
		mem_mapping_set_exec(&dev->rom_mapping,
				     &dev->rom[dev->rom_offset]);
    }
}


static uint8_t
tandy_read(uint16_t addr, void *priv)
{
    tandy_t *dev = (tandy_t *)priv;
    uint8_t ret = 0xff;

    switch (addr) {
	case 0x00a0:
		ret = dev->ram_bank;
		break;

	case 0xffe8:
		ret = dev->ram_bank;
		break;

	case 0xffea:
		ret = (dev->rom_bank ^ 0x10);
		break;
    }

    return(ret);
}


static void
write_ram(uint32_t addr, uint8_t val, void *priv)
{
    tandy_t *dev = (tandy_t *)priv;

    ram[dev->base + (addr & 0x1ffff)] = val;
}


static uint8_t
read_ram(uint32_t addr, void *priv)
{
    tandy_t *dev = (tandy_t *)priv;

    return(ram[dev->base + (addr & 0x1ffff)]);
}


static uint8_t
read_rom(uint32_t addr, void *priv)
{
    tandy_t *dev = (tandy_t *)priv;
    uint32_t addr2 = (addr & 0xffff) + dev->rom_offset;

    return(dev->rom[addr2]);
}


static uint16_t
read_romw(uint32_t addr, void *priv)
{
    tandy_t *dev = (tandy_t *)priv;
    uint32_t addr2 = (addr & 0xffff) + dev->rom_offset;

    return(*(uint16_t *)&dev->rom[addr2]);
}


static uint32_t
read_roml(uint32_t addr, void *priv)
{
    tandy_t *dev = (tandy_t *)priv;

    return(*(uint32_t *)&dev->rom[addr]);
}


static void
init_rom(tandy_t *dev)
{
    dev->rom = (uint8_t *)malloc(0x80000);

#if 1
    if (! rom_load_interleaved(L"roms/machines/tandy1000sl2/8079047.hu1",
			       L"roms/machines/tandy1000sl2/8079048.hu2",
			       0x000000, 0x80000, 0, dev->rom)) {
	tandy_log("TANDY: unable to load BIOS for 1000/SL2 !\n");
	free(dev->rom);
	dev->rom = NULL;
	return;
    }
#else
    f  = rom_fopen(L"roms/machines/tandy1000sl2/8079047.hu1", L"rb");
    ff = rom_fopen(L"roms/machines/tandy1000sl2/8079048.hu2", L"rb");
    for (c = 0x0000; c < 0x80000; c += 2) {
	dev->rom[c] = getc(f);
	dev->rom[c + 1] = getc(ff);
    }
    fclose(ff);
    fclose(f);
#endif

    mem_mapping_add(&dev->rom_mapping, 0xe0000, 0x10000,
		    read_rom, read_romw, read_roml, NULL, NULL, NULL,
		    dev->rom, MEM_MAPPING_EXTERNAL, dev);
}


void
machine_tandy1k_init(const machine_t *model)
{
    tandy_t *dev;

    dev = malloc(sizeof(tandy_t));
    memset(dev, 0x00, sizeof(tandy_t));

    machine_common_init(model);

    nmi_init();

    /*
     * Base 128K mapping is controlled via ports 0xA0 or
     * 0xFFE8 (SL2), so we remove it from the main mapping.
     */
    dev->base = (mem_size - 128) * 1024;
    mem_mapping_add(&dev->ram_mapping, 0x80000, 0x20000,
		    read_ram,NULL,NULL, write_ram,NULL,NULL, NULL, 0, dev);
    mem_mapping_set_addr(&ram_low_mapping, 0, dev->base);

    device_add(&keyboard_tandy_device);
    keyboard_set_table(scancode_tandy);

    device_add(&fdc_xt_device);

    switch(romset) {
	case ROM_TANDY:
		io_sethandler(0x00a0, 1,
			      tandy_read,NULL,NULL,tandy_write,NULL,NULL,dev);
		vid_init(dev);
		device_add_ex(&vid_device, dev);
		device_add(&sn76489_device);
		break;

	case ROM_TANDY1000HX:
		io_sethandler(0x00a0, 1,
			      tandy_read,NULL,NULL,tandy_write,NULL,NULL,dev);
		vid_init(dev);
		device_add_ex(&vid_device, dev);
		device_add(&ncr8496_device);
		device_add(&eep_device);
		break;

	case ROM_TANDY1000SL2:
		dev->is_sl2 = 1;
		init_rom(dev);
		io_sethandler(0xffe8, 8,
			      tandy_read,NULL,NULL,tandy_write,NULL,NULL,dev);
		vid_init(dev);
		device_add_ex(&vid_device_sl, dev);
		device_add(&pssj_device);
		device_add(&eep_device);
    }

    if (joystick_type != 7)
	device_add(&gameport_device);

    eep_data_out = 0x0000;
}


int
tandy1k_eeprom_read(void)
{
    return(eep_data_out);
}
