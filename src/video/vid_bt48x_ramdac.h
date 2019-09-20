/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Header of the emulation of the Brooktree BT484-BT485A
 *		true colour RAMDAC family.
 *
 * Version:	@(#)vid_bt485_ramdac.h	1.0.5	2019/01/12
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995,
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2018 TheCollector1995.
 */
typedef struct
{
	PALETTE extpal;
	uint32_t extpallook[256];
	uint8_t cursor32_data[256];
	uint8_t cursor64_data[1024];
	int hwc_y, hwc_x;
	uint8_t cmd_r0;
        uint8_t cmd_r1;
        uint8_t cmd_r2;
	uint8_t cmd_r3;
	uint8_t cmd_r4;
	uint8_t status;
	uint8_t type;
} bt48x_ramdac_t;

extern void	bt48x_ramdac_out(uint16_t addr, int rs2, int rs3, uint8_t val, bt48x_ramdac_t *ramdac, svga_t *svga);
extern uint8_t	bt48x_ramdac_in(uint16_t addr, int rs2, int rs3, bt48x_ramdac_t *ramdac, svga_t *svga);
extern void	bt48x_hwcursor_draw(svga_t *svga, int displine);

extern const device_t bt484_ramdac_device;
extern const device_t att20c504_ramdac_device;
extern const device_t bt485_ramdac_device;
extern const device_t att20c505_ramdac_device;
extern const device_t bt485a_ramdac_device;
