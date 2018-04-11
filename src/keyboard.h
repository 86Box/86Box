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
 * Version:	@(#)keyboard.h	1.0.14	2018/03/22
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#ifndef EMU_KEYBOARD_H
# define EMU_KEYBOARD_H


typedef struct {
    int	mk[9];
    int	brk[9];
} scancode;


#define STATE_SHIFT_MASK	0x22
#define STATE_RSHIFT		0x20
#define STATE_LSHIFT		0x02

#define FAKE_LSHIFT_ON		0x100
#define FAKE_LSHIFT_OFF		0x101
#define LSHIFT_ON		0x102
#define LSHIFT_OFF		0x103
#define RSHIFT_ON		0x104
#define RSHIFT_OFF		0x105


#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t	keyboard_mode;
extern int	keyboard_scan;
extern int64_t	keyboard_delay;

extern void	(*keyboard_send)(uint16_t val);
extern void	kbd_adddata_process(uint16_t val, void (*adddata)(uint16_t val));

extern const scancode	scancode_xt[512];

extern uint8_t	keyboard_set3_flags[512];
extern uint8_t	keyboard_set3_all_repeat;
extern uint8_t	keyboard_set3_all_break;
extern int	mouse_queue_start, mouse_queue_end;
extern int	mouse_scan;

#ifdef EMU_DEVICE_H
extern const device_t	keyboard_xt_device;
extern const device_t	keyboard_tandy_device;
extern const device_t	keyboard_at_device;
extern const device_t	keyboard_at_ami_device;
extern const device_t	keyboard_at_toshiba_device;
extern const device_t	keyboard_ps2_device;
extern const device_t	keyboard_ps2_ami_device;
extern const device_t	keyboard_ps2_mca_device;
extern const device_t	keyboard_ps2_mca_2_device;
extern const device_t	keyboard_ps2_quadtel_device;
#endif

extern void	keyboard_init(void);
extern void	keyboard_close(void);
extern void	keyboard_set_table(const scancode *ptr);
extern void	keyboard_poll_host(void);
extern void	keyboard_process(void);
extern uint16_t	keyboard_convert(int ch);
extern void	keyboard_input(int down, uint16_t scan);
extern void	keyboard_update_states(uint8_t cl, uint8_t nl, uint8_t sl);
extern uint8_t	keyboard_get_shift(void);
extern void	keyboard_get_states(uint8_t *cl, uint8_t *nl, uint8_t *sl);
extern void	keyboard_set_states(uint8_t cl, uint8_t nl, uint8_t sl);
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
