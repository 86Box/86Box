/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Intel 8042 (AT keyboard controller) emulation.
 *
 * Version:	@(#)keyboard_at.h	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

void keyboard_at_init();
void keyboard_at_init_ps2();
void keyboard_at_reset();
void keyboard_at_poll();
void keyboard_at_adddata_keyboard_raw(uint8_t val);
void keyboard_at_adddata_mouse(uint8_t val);
void keyboard_at_set_mouse(void (*mouse_write)(uint8_t val, void *p), void *p);

extern int mouse_queue_start, mouse_queue_end;
extern int mouse_scan;
