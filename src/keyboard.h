/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Host to guest keyboard interface and keyboard scan code sets.
 *
 * Version:	@(#)keyboard.h	1.0.1	2017/08/23
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#ifndef EMU_KEYBOARD_H
# define EMU_KEYBOARD_H


extern int keyboard_scan;
extern int pcem_key[272];
extern uint8_t mode;

extern uint8_t set3_flags[272];
extern uint8_t set3_all_repeat;
extern uint8_t set3_all_break;


extern void (*keyboard_send)(uint8_t val);
extern void (*keyboard_poll)(void);
extern void keyboard_process(void);


#endif	/*EMU_KEYBOARD_H*/
