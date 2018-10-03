/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Header of the emulation of the Brooktree BT485 and BT485A
 *		true colour RAM DAC's.
 *
 * Version:	@(#)vid_bt485_ramdac.h	1.0.1	2018/10/03
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995,
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2018 TheCollector1995.
 */
typedef struct bt485_ramdac_t
{
	PALETTE extpal;
	uint32_t extpallook[256];
	uint8_t cursor32_data[256];
	uint8_t cursor64_data[1024];
	int hwc_y, hwc_x;
	uint8_t cr0;
        uint8_t cr1;
        uint8_t cr2;
	uint8_t cr3;
	uint8_t cr4;
	uint8_t status;
	uint8_t type;
} bt485_ramdac_t;

enum {
	BT484 = 0,
	ATT20C504,
	BT485,
	ATT20C505,
	BT485A
};

extern void	bt485_ramdac_out(uint16_t addr, int rs2, int rs3, uint8_t val, bt485_ramdac_t *ramdac, svga_t *svga);
extern uint8_t	bt485_ramdac_in(uint16_t addr, int rs2, int rs3, bt485_ramdac_t *ramdac, svga_t *svga);
extern void	bt485_init(bt485_ramdac_t *ramdac, svga_t *svga, uint8_t type);
