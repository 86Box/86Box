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
 * Version:	@(#)keyboard_at.h	1.0.1	2017/08/23
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */


extern int mouse_queue_start, mouse_queue_end;
extern int mouse_scan;


extern void keyboard_at_init(void);
extern void keyboard_at_init_ps2(void);
extern void keyboard_at_reset(void);
extern void keyboard_at_adddata_keyboard_raw(uint8_t val);
extern void keyboard_at_adddata_mouse(uint8_t val);
extern void keyboard_at_set_mouse(void (*mouse_write)(uint8_t val, void *p), void *p);
