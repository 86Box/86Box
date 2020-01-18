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
 * Version:	@(#)m_tandy.c	1.0.11	2019/12/28
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
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
#include "../timer.h"
#include "../io.h"
#include "../pit.h"
#include "../nmi.h"
#include "../mem.h"
#include "../rom.h"
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


enum {
    TANDY_RGB = 0,
    TANDY_COMPOSITE
};


enum {
    TYPE_TANDY = 0,
    TYPE_TANDY1000HX,
    TYPE_TANDY1000SL2
};


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
    uint8_t		array[256];
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
    int			vsynctime;
    int			vadj;
    uint16_t		ma, maback;

    uint64_t		dispontime,
			dispofftime;
    pc_timer_t		timer;
    int			firstline,
			lastline;

    int			composite;
} t1kvid_t;

typedef struct {
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
    { {0},       {0}       }, { {0x01, 0}, {0x81, 0} },
    { {0x02, 0}, {0x82, 0} }, { {0x03, 0}, {0x83, 0} },
    { {0x04, 0}, {0x84, 0} }, { {0x05, 0}, {0x85, 0} },
    { {0x06, 0}, {0x86, 0} }, { {0x07, 0}, {0x87, 0} },
    { {0x08, 0}, {0x88, 0} }, { {0x09, 0}, {0x89, 0} },
    { {0x0a, 0}, {0x8a, 0} }, { {0x0b, 0}, {0x8b, 0} },
    { {0x0c, 0}, {0x8c, 0} }, { {0x0d, 0}, {0x8d, 0} },
    { {0x0e, 0}, {0x8e, 0} }, { {0x0f, 0}, {0x8f, 0} },
    { {0x10, 0}, {0x90, 0} }, { {0x11, 0}, {0x91, 0} },
    { {0x12, 0}, {0x92, 0} }, { {0x13, 0}, {0x93, 0} },
    { {0x14, 0}, {0x94, 0} }, { {0x15, 0}, {0x95, 0} },
    { {0x16, 0}, {0x96, 0} }, { {0x17, 0}, {0x97, 0} },
    { {0x18, 0}, {0x98, 0} }, { {0x19, 0}, {0x99, 0} },
    { {0x1a, 0}, {0x9a, 0} }, { {0x1b, 0}, {0x9b, 0} },
    { {0x1c, 0}, {0x9c, 0} }, { {0x1d, 0}, {0x9d, 0} },
    { {0x1e, 0}, {0x9e, 0} }, { {0x1f, 0}, {0x9f, 0} },
    { {0x20, 0}, {0xa0, 0} }, { {0x21, 0}, {0xa1, 0} },
    { {0x22, 0}, {0xa2, 0} }, { {0x23, 0}, {0xa3, 0} },
    { {0x24, 0}, {0xa4, 0} }, { {0x25, 0}, {0xa5, 0} },
    { {0x26, 0}, {0xa6, 0} }, { {0x27, 0}, {0xa7, 0} },
    { {0x28, 0}, {0xa8, 0} }, { {0x29, 0}, {0xa9, 0} },
    { {0x2a, 0}, {0xaa, 0} }, { {0x2b, 0}, {0xab, 0} },
    { {0x2c, 0}, {0xac, 0} }, { {0x2d, 0}, {0xad, 0} },
    { {0x2e, 0}, {0xae, 0} }, { {0x2f, 0}, {0xaf, 0} },
    { {0x30, 0}, {0xb0, 0} }, { {0x31, 0}, {0xb1, 0} },
    { {0x32, 0}, {0xb2, 0} }, { {0x33, 0}, {0xb3, 0} },
    { {0x34, 0}, {0xb4, 0} }, { {0x35, 0}, {0xb5, 0} },
    { {0x36, 0}, {0xb6, 0} }, { {0x37, 0}, {0xb7, 0} },
    { {0x38, 0}, {0xb8, 0} }, { {0x39, 0}, {0xb9, 0} },
    { {0x3a, 0}, {0xba, 0} }, { {0x3b, 0}, {0xbb, 0} },
    { {0x3c, 0}, {0xbc, 0} }, { {0x3d, 0}, {0xbd, 0} },
    { {0x3e, 0}, {0xbe, 0} }, { {0x3f, 0}, {0xbf, 0} },
    { {0x40, 0}, {0xc0, 0} }, { {0x41, 0}, {0xc1, 0} },
    { {0x42, 0}, {0xc2, 0} }, { {0x43, 0}, {0xc3, 0} },
    { {0x44, 0}, {0xc4, 0} }, { {0x45, 0}, {0xc5, 0} },
    { {0x46, 0}, {0xc6, 0} }, { {0x47, 0}, {0xc7, 0} },
    { {0x48, 0}, {0xc8, 0} }, { {0x49, 0}, {0xc9, 0} },
    { {0x4a, 0}, {0xca, 0} }, { {0x4b, 0}, {0xcb, 0} },
    { {0x4c, 0}, {0xcc, 0} }, { {0x4d, 0}, {0xcd, 0} },
    { {0x4e, 0}, {0xce, 0} }, { {0x4f, 0}, {0xcf, 0} },
    { {0x50, 0}, {0xd0, 0} }, { {0x51, 0}, {0xd1, 0} },
    { {0x52, 0}, {0xd2, 0} }, { {0x56, 0}, {0xd6, 0} },
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*054*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*058*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*05c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*060*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*064*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*068*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*06c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*070*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*074*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*078*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*07c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*080*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*084*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*088*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*08c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*090*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*094*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*098*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*09c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0a0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0a4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0a8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0ac*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0b0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0b4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0b8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0bc*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0c0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0c4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0c8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0cc*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0d0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0d4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0d8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0dc*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0e0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0e4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0e8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0ec*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0f0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0f4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0f8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*0fc*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*100*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*104*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*108*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*10c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*110*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*114*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*118*/
    { {0x57, 0}, {0xd7, 0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*11c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*120*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*124*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*128*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*12c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*130*/
    { {0},             {0} }, { {0x35, 0}, {0xb5, 0} },
    { {0},             {0} }, { {0x37, 0}, {0xb7, 0} },	/*134*/
    { {0x38, 0}, {0xb8, 0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*138*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*13c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*140*/
    { {0},             {0} }, { {0},             {0} },
    { {0x46, 0}, {0xc6, 0} }, { {0x47, 0}, {0xc7, 0} },	/*144*/
    { {0x48, 0}, {0xc8, 0} }, { {0x49, 0}, {0xc9, 0} },
    { {0},             {0} }, { {0x4b, 0}, {0xcb, 0} },	/*148*/
    { {0},             {0} }, { {0x4d, 0}, {0xcd, 0} },
    { {0},             {0} }, { {0x4f, 0}, {0xcf, 0} },	/*14c*/
    { {0x50, 0}, {0xd0, 0} }, { {0x51, 0}, {0xd1, 0} },
    { {0x52, 0}, {0xd2, 0} }, { {0x53, 0}, {0xd3, 0} },	/*150*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*154*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*158*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*15c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*160*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*164*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*168*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*16c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*170*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*174*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*148*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*17c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*180*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*184*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*88*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*18c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*190*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*194*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*198*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*19c*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1a0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1a4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1a8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1ac*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1b0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1b4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1b8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1bc*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1c0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1c4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1c8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1cc*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1d0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1d4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1d8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1dc*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1e0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1e4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1e8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1ec*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1f0*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1f4*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} },	/*1f8*/
    { {0},             {0} }, { {0},             {0} },
    { {0},             {0} }, { {0},             {0} }	/*1fc*/
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
    vid->dispontime  = (uint64_t)(_dispontime);
    vid->dispofftime = (uint64_t)(_dispofftime);
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
    int x, c, xs_temp, ys_temp;
    int oldvc;
    uint8_t chr, attr;
    uint16_t dat;
    int cols[4];
    int col;
    int oldsc;

    if (! vid->linepos) {
	timer_advance_u64(&vid->timer, vid->dispofftime);
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
				buffer32->line[(vid->displine << 1)][c] = buffer32->line[(vid->displine << 1) + 1][c] = cols[0];
				if (vid->mode & 1) {
					buffer32->line[(vid->displine << 1)][c + (vid->crtc[1] << 3) + 8] =
					buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 3) + 8] = cols[0];
				} else {
					buffer32->line[(vid->displine << 1)][c + (vid->crtc[1] << 4) + 8] =
					buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 4) + 8] = cols[0];
				}
			} else if ((vid->mode & 0x12) == 0x12) {
				buffer32->line[(vid->displine << 1)][c] = buffer32->line[(vid->displine << 1) + 1][c] = 0;
				if (vid->mode & 1) {
					buffer32->line[(vid->displine << 1)][c + (vid->crtc[1] << 3) + 8] =
					buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 3) + 8] = 0;
				} else {
					buffer32->line[(vid->displine << 1)][c + (vid->crtc[1] << 4) + 8] =
					buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 4) + 8] = 0;
				}
			} else {
				buffer32->line[(vid->displine << 1)][c] = buffer32->line[(vid->displine << 1) + 1][c] = (vid->col & 15) + 16;
				if (vid->mode & 1) {
					buffer32->line[(vid->displine << 1)][c + (vid->crtc[1] << 3) + 8] =
					buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 3) + 8] = (vid->col & 15) + 16;
				} else {
					buffer32->line[(vid->displine << 1)][c + (vid->crtc[1] << 4) + 8] =
					buffer32->line[(vid->displine << 1) + 1][c + (vid->crtc[1] << 4) + 8] = (vid->col & 15) + 16;
				}
			}
		}
		if (dev->is_sl2 && (vid->array[5] & 1)) { /*640x200x16*/
			for (x = 0; x < vid->crtc[1]*2; x++) {
				dat = (vid->vram[(vid->ma << 1) & 0xffff] << 8) | 
				       vid->vram[((vid->ma << 1) + 1) & 0xffff];
				vid->ma++;
				buffer32->line[(vid->displine << 1)][(x << 2) + 8]  = buffer32->line[(vid->displine << 1) + 1][(x << 2) + 8]  =
					vid->array[((dat >> 12) & 0xf) + 16] + 16;
				buffer32->line[(vid->displine << 1)][(x << 2) + 9]  = buffer32->line[(vid->displine << 1) + 1][(x << 2) + 9]  =
					vid->array[((dat >>  8) & 0xf) + 16] + 16;
				buffer32->line[(vid->displine << 1)][(x << 2) + 10] = buffer32->line[(vid->displine << 1) + 1][(x << 2) + 10] =
					vid->array[((dat >>  4) & 0xf) + 16] + 16;
				buffer32->line[(vid->displine << 1)][(x << 2) + 11] = buffer32->line[(vid->displine << 1) + 1][(x << 2) + 11] =
					vid->array[(dat & 0xf) + 16] + 16;
			}
		} else if ((vid->array[3] & 0x10) && (vid->mode & 1)) { /*320x200x16*/
			for (x = 0; x < vid->crtc[1]; x++) {
				dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000)] << 8) | 
				       vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000) + 1];
				vid->ma++;
				buffer32->line[(vid->displine << 1)][(x << 3) + 8]  = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 8]  =
				buffer32->line[(vid->displine << 1)][(x << 3) + 9]  = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 9]  =
					vid->array[((dat >> 12) & vid->array[1]) + 16] + 16;
				buffer32->line[(vid->displine << 1)][(x << 3) + 10] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 10] =
				buffer32->line[(vid->displine << 1)][(x << 3) + 11] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 11] =
					vid->array[((dat >>  8) & vid->array[1]) + 16] + 16;
				buffer32->line[(vid->displine << 1)][(x << 3) + 12] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 12] =
				buffer32->line[(vid->displine << 1)][(x << 3) + 13] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 13] =
					vid->array[((dat >>  4) & vid->array[1]) + 16] + 16;
				buffer32->line[(vid->displine << 1)][(x << 3) + 14] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 14] =
				buffer32->line[(vid->displine << 1)][(x << 3) + 15] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 15] =
					vid->array[(dat & vid->array[1]) + 16] + 16;
			}
		} else if (vid->array[3] & 0x10) { /*160x200x16*/
			for (x = 0; x < vid->crtc[1]; x++) {
				if (dev->is_sl2) {
					dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000)] << 8) | 
					       vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000) + 1];
				} else {
					dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000)] << 8) | 
					       vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000) + 1];
				}
				vid->ma++;
				buffer32->line[(vid->displine << 1)][(x << 4) + 8]  = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 8]  =
				buffer32->line[(vid->displine << 1)][(x << 4) + 9]  = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 9]  =
				buffer32->line[(vid->displine << 1)][(x << 4) + 10] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 10] =
				buffer32->line[(vid->displine << 1)][(x << 4) + 11] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 11] =
					vid->array[((dat >> 12) & vid->array[1]) + 16] + 16;
				buffer32->line[(vid->displine << 1)][(x << 4) + 12] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 12] =
				buffer32->line[(vid->displine << 1)][(x << 4) + 13] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 13] =
				buffer32->line[(vid->displine << 1)][(x << 4) + 14] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 14] =
				buffer32->line[(vid->displine << 1)][(x << 4) + 15] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 15] =
					vid->array[((dat >>  8) & vid->array[1]) + 16] + 16;
				buffer32->line[(vid->displine << 1)][(x << 4) + 16] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 16] =
				buffer32->line[(vid->displine << 1)][(x << 4) + 17] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 17] =
				buffer32->line[(vid->displine << 1)][(x << 4) + 18] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 18] =
				buffer32->line[(vid->displine << 1)][(x << 4) + 19] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 19] =
					vid->array[((dat >>  4) & vid->array[1]) + 16] + 16;
				buffer32->line[(vid->displine << 1)][(x << 4) + 20] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 20] =
				buffer32->line[(vid->displine << 1)][(x << 4) + 21] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 21] =
				buffer32->line[(vid->displine << 1)][(x << 4) + 22] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 22] =
				buffer32->line[(vid->displine << 1)][(x << 4) + 23] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + 23] =
					vid->array[(dat & vid->array[1]) + 16] + 16;
			}
		} else if (vid->array[3] & 0x08) { /*640x200x4 - this implementation is a complete guess!*/
			for (x = 0; x < vid->crtc[1]; x++) {
				dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000)] << 8) |
				       vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 3) * 0x2000) + 1];
				vid->ma++;
				for (c = 0; c < 8; c++) {
					chr  =  (dat >>  6) & 2;
					chr |= ((dat >> 15) & 1);
					buffer32->line[(vid->displine << 1)][(x << 3) + 8 + c] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + 8 + c] =
						vid->array[(chr & vid->array[1]) + 16] + 16;
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
					for (c = 0; c < 8; c++) {
						buffer32->line[(vid->displine << 1)][(x << 3) + c + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + c + 8] =
							cols[0];
					}
				} else {
					for (c = 0; c < 8; c++) {
						buffer32->line[(vid->displine << 1)][(x << 3) + c + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 3) + c + 8] =
							cols[(fontdat[chr][vid->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
					}
				}
				if (drawcursor) {
					for (c = 0; c < 8; c++) {
						buffer32->line[(vid->displine << 1)][(x << 3) + c + 8] ^= 15;
						buffer32->line[(vid->displine << 1) + 1][(x << 3) + c + 8] ^= 15;
					}
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
						buffer32->line[(vid->displine << 1)][(x << 4) + (c << 1) + 8] =  buffer32->line[(vid->displine << 1)][(x << 4) + (c << 1) + 1 + 8] =
							cols[0];
				} else {
					for (c = 0; c < 8; c++) {
						buffer32->line[(vid->displine << 1)][(x << 4) + (c << 1) + 8] =  buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 8] = 
						buffer32->line[(vid->displine << 1)][(x << 4) + (c << 1) + 1 + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] =
							cols[(fontdat[chr][vid->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
					}
				}
				if (drawcursor) {
					for (c = 0; c < 16; c++) {
						buffer32->line[(vid->displine << 1)][(x << 4) + c + 8] ^= 15;
						buffer32->line[(vid->displine << 1) + 1][(x << 4) + c + 8] ^= 15;
					}
				}
			}
		} else if (! (vid->mode & 16)) {
			cols[0] = (vid->col & 15);
			col = (vid->col & 16) ? 8 : 0;
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
			cols[0] = vid->array[(cols[0] & vid->array[1]) + 16] + 16;
			cols[1] = vid->array[(cols[1] & vid->array[1]) + 16] + 16;
			cols[2] = vid->array[(cols[2] & vid->array[1]) + 16] + 16;
			cols[3] = vid->array[(cols[3] & vid->array[1]) + 16] + 16;
			for (x = 0; x < vid->crtc[1]; x++) {
				dat = (vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000)] << 8) | 
				       vid->vram[((vid->ma << 1) & 0x1fff) + ((vid->sc & 1) * 0x2000) + 1];
				vid->ma++;
				for (c = 0; c < 8; c++) {
					buffer32->line[(vid->displine << 1)][(x << 4) + (c << 1) + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 8] =
					buffer32->line[(vid->displine << 1)][(x << 4) + (c << 1) + 1 + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] =
						cols[dat >> 14];
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
					buffer32->line[(vid->displine << 1)][(x << 4) + c + 8] = buffer32->line[(vid->displine << 1) + 1][(x << 4) + c + 8] =
						cols[dat >> 15];
					dat <<= 1;
				}
			}
		}
	} else {
		if (vid->array[3] & 4) {
			if (vid->mode & 1) {
				hline(buffer32, 0, (vid->displine << 1), (vid->crtc[1] << 3) + 16, (vid->array[2] & 0xf) + 16);
				hline(buffer32, 0, (vid->displine << 1) + 1, (vid->crtc[1] << 3) + 16, (vid->array[2] & 0xf) + 16);
			} else {
				hline(buffer32, 0, (vid->displine << 1), (vid->crtc[1] << 4) + 16, (vid->array[2] & 0xf) + 16);
				hline(buffer32, 0, (vid->displine << 1) + 1, (vid->crtc[1] << 4) + 16, (vid->array[2] & 0xf) + 16);
			}
		} else {
			cols[0] = ((vid->mode & 0x12) == 0x12) ? 0 : (vid->col & 0xf) + 16;
			if (vid->mode & 1) {
				hline(buffer32, 0, (vid->displine << 1), (vid->crtc[1] << 3) + 16, cols[0]);
				hline(buffer32, 0, (vid->displine << 1) + 1, (vid->crtc[1] << 3) + 16, cols[0]);
			} else {
				hline(buffer32, 0, (vid->displine << 1), (vid->crtc[1] << 4) + 16, cols[0]);
				hline(buffer32, 0, (vid->displine << 1) + 1, (vid->crtc[1] << 4) + 16, cols[0]);
			}
		}
	}

	if (vid->mode & 1)
		x = (vid->crtc[1] << 3) + 16;
	  else
		x = (vid->crtc[1] << 4) + 16;
	if (!dev->is_sl2 && vid->composite) {
		Composite_Process(vid->mode, 0, x >> 2, buffer32->line[(vid->displine << 1)]);
		Composite_Process(vid->mode, 0, x >> 2, buffer32->line[(vid->displine << 1) + 1]);
	}
	vid->sc = oldsc;
	if (vid->vc == vid->crtc[7] && !vid->sc)
		vid->stat |= 8;
	vid->displine++;
	if (vid->displine >= 360) 
		vid->displine = 0;
    } else {
	timer_advance_u64(&vid->timer, vid->dispontime);
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
			if (dev->is_sl2 && (vid->array[5] & 1))
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
		if (dev->is_sl2)
			vid->vc &= 255;
		else
			vid->vc &= 127;
		if (vid->vc == vid->crtc[6]) 
			vid->dispon = 0;
		if (oldvc == vid->crtc[4]) {
			vid->vc = 0;
			vid->vadj = vid->crtc[5];
			if (! vid->vadj) 
				vid->dispon = 1;
			if (! vid->vadj) {
				if (dev->is_sl2 && (vid->array[5] & 1))
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

				xs_temp = x;
				ys_temp = (vid->lastline - vid->firstline) << 1;

				if ((xs_temp > 0) && (ys_temp > 0)) {
					if (xs_temp < 64) xs_temp = 656;
					if (ys_temp < 32) ys_temp = 400;
					if (!enable_overscan)
						xs_temp -= 16;

					if (((xs_temp != xsize) || (ys_temp != ysize) || video_force_resize_get())) {
						xsize = xs_temp;
						ysize = ys_temp;
						set_screen_size(xsize, ysize + (enable_overscan ? 16 : 0));

						if (video_force_resize_get())
							video_force_resize_set(0);
					}

					if (enable_overscan) {
						if (!dev->is_sl2 && vid->composite) 
							video_blit_memtoscreen(0, (vid->firstline - 4) << 1, 0, ((vid->lastline - vid->firstline) + 8) << 1,
								       xsize, ((vid->lastline - vid->firstline) + 8) << 1);
						else
							video_blit_memtoscreen_8(0, (vid->firstline - 4) << 1, 0, ((vid->lastline - vid->firstline) + 8) << 1,
										 xsize, ((vid->lastline - vid->firstline) + 8) << 1);
					} else {
						if (!dev->is_sl2 && vid->composite) 
							video_blit_memtoscreen(8, vid->firstline << 1, 0, (vid->lastline - vid->firstline) << 1,
								       xsize, (vid->lastline - vid->firstline) << 1);
						else
							video_blit_memtoscreen_8(8, vid->firstline << 1, 0, (vid->lastline - vid->firstline) << 1,
										 xsize, (vid->lastline - vid->firstline) << 1);
					}
				}

				frames++;

				video_res_x = xsize;
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
    } else
	vid->b8000_mask = 0x3fff;
    timer_add(&vid->timer, vid_poll, dev, 1);
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

    switch (info->local) {
	case TYPE_TANDY1000HX:
		eep->path = L"tandy1000hx.bin";
		break;

	case TYPE_TANDY1000SL2:
		eep->path = L"tandy1000sl2.bin";
		break;

    }

    f = nvr_fopen(eep->path, L"rb");
    if (f != NULL) {
	if (fread(eep->store, 1, 128, f) != 128)
		fatal("eep_init(): Error reading Tandy EEPROM\n");
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


static const device_t eep_1000hx_device = {
    "Tandy 1000HX EEPROM",
    0, TYPE_TANDY1000HX,
    eep_init, eep_close, NULL,
    NULL, NULL, NULL,
    NULL
};


static const device_t eep_1000sl2_device = {
    "Tandy 1000SL2 EEPROM",
    0, TYPE_TANDY1000SL2,
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


static void
machine_tandy1k_init(const machine_t *model, int type)
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

    switch(type) {
	case TYPE_TANDY:
		io_sethandler(0x00a0, 1,
			      tandy_read,NULL,NULL,tandy_write,NULL,NULL,dev);
		vid_init(dev);
		device_add_ex(&vid_device, dev);
		device_add(&sn76489_device);
		break;

	case TYPE_TANDY1000HX:
		io_sethandler(0x00a0, 1,
			      tandy_read,NULL,NULL,tandy_write,NULL,NULL,dev);
		vid_init(dev);
		device_add_ex(&vid_device, dev);
		device_add(&ncr8496_device);
		device_add(&eep_1000hx_device);
		break;

	case TYPE_TANDY1000SL2:
		dev->is_sl2 = 1;
		init_rom(dev);
		io_sethandler(0xffe8, 8,
			      tandy_read,NULL,NULL,tandy_write,NULL,NULL,dev);
		vid_init(dev);
		device_add_ex(&vid_device_sl, dev);
		device_add(&pssj_device);
		device_add(&eep_1000sl2_device);
    }

    if (joystick_type != JOYSTICK_TYPE_NONE)
	device_add(&gameport_device);

    eep_data_out = 0x0000;
}


int
tandy1k_eeprom_read(void)
{
    return(eep_data_out);
}


int
machine_tandy_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linearr(L"roms/machines/tandy/tandy1t1.020",
			    0x000f0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_tandy1k_init(model, TYPE_TANDY);

    return ret;
}


int
machine_tandy1000hx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear(L"roms/machines/tandy1000hx/v020000.u12",
			   0x000e0000, 131072, 0);

    if (bios_only || !ret)
	return ret;

    machine_tandy1k_init(model, TYPE_TANDY1000HX);

    return ret;
}


int
machine_tandy1000sl2_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved(L"roms/machines/tandy1000sl2/8079047.hu1",
				L"roms/machines/tandy1000sl2/8079048.hu2",
				0x000f0000, 65536, 0x18000);

    if (bios_only || !ret)
	return ret;

    machine_tandy1k_init(model, TYPE_TANDY1000SL2);

    return ret;
}
