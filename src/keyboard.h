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
 * Version:	@(#)keyboard.h	1.0.3	2017/10/24
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#ifndef EMU_KEYBOARD_H
# define EMU_KEYBOARD_H


#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t	keyboard_mode;
extern int	keyboard_scan;
extern uint8_t	keyboard_set3_flags[272];
extern uint8_t	keyboard_set3_all_repeat;
extern uint8_t	keyboard_set3_all_break;


extern void	(*keyboard_send)(uint8_t val);
extern void	(*keyboard_poll)(void);

extern void	keyboard_init(void);
extern void	keyboard_close(void);
extern void	keyboard_poll_host(void);
extern void	keyboard_process(void);
extern uint16_t	keyboard_convert(int ch);
extern void	keyboard_input(int down, uint16_t scan);
extern int	keyboard_isfsexit(void);
extern int	keyboard_ismsexit(void);

#ifdef __cplusplus
}
#endif


#endif	/*EMU_KEYBOARD_H*/
