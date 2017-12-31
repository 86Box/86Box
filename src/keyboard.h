/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the keyboard interface.
 *
 * Version:	@(#)keyboard.h	1.0.7	2017/12/31
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef EMU_KEYBOARD_H
# define EMU_KEYBOARD_H


typedef struct {
    int	mk[9];
    int	brk[9];
} scancode;


#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t	keyboard_mode;
extern int	keyboard_scan;
extern int64_t	keyboard_delay;

extern void	(*keyboard_send)(uint8_t val);

extern scancode scancode_xt[272];

extern uint8_t	keyboard_set3_flags[272];
extern uint8_t	keyboard_set3_all_repeat;
extern uint8_t	keyboard_set3_all_break;
extern int	mouse_queue_start, mouse_queue_end;
extern int	mouse_scan;

#ifdef EMU_DEVICE_H
extern device_t	keyboard_xt_device;
extern device_t	keyboard_tandy_device;
extern device_t	keyboard_at_device;
extern device_t	keyboard_ps2_device;
#endif

extern void	keyboard_init(void);
extern void	keyboard_close(void);
extern void	keyboard_set_table(scancode *ptr);
extern void	keyboard_poll_host(void);
extern void	keyboard_process(void);
extern uint16_t	keyboard_convert(int ch);
extern void	keyboard_input(int down, uint16_t scan);
extern int	keyboard_recv(uint16_t key);
extern int	keyboard_isfsexit(void);
extern int	keyboard_ismsexit(void);

extern void	keyboard_at_reset(void);
extern void	keyboard_at_adddata_keyboard_raw(uint8_t val);
extern void	keyboard_at_adddata_mouse(uint8_t val);
extern void	keyboard_at_set_mouse(void (*mouse_write)(uint8_t val,void *), void *);
extern uint8_t	keyboard_at_get_mouse_scan(void);
extern void	keyboard_at_set_mouse_scan(uint8_t val);

#ifdef __cplusplus
}
#endif


#endif	/*EMU_KEYBOARD_H*/
