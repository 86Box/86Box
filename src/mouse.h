/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the mouse driver.
 *
 * Version:	@(#)mouse.h	1.0.10	2017/12/03
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef EMU_MOUSE_H
# define EMU_MOUSE_H


#define MOUSE_TYPE_NONE		0	/* no mouse configured */
#define MOUSE_TYPE_INTERNAL	1	/* machine has internal mouse */
#define MOUSE_TYPE_LOGIBUS	2	/* Logitech/ATI Bus Mouse */
#define MOUSE_TYPE_INPORT	3	/* Microsoft InPort Mouse */
#if 0
# define MOUSE_TYPE_GENIBUS	4	/* Genius Bus Mouse */
#endif
#define MOUSE_TYPE_MSYSTEMS	5	/* Mouse Systems mouse */
#define MOUSE_TYPE_MICROSOFT	6	/* Microsoft Serial Mouse */
#define MOUSE_TYPE_LOGITECH	7	/* Logitech Serial Mouse */
#define MOUSE_TYPE_MSWHEEL	8	/* Serial Wheel Mouse */
#define MOUSE_TYPE_PS2		9	/* IBM PS/2 series Bus Mouse */
#define MOUSE_TYPE_PS2_MS	10	/* Microsoft Intellimouse PS/2 */

#define MOUSE_TYPE_MASK		0x0f
#define MOUSE_TYPE_3BUTTON	(1<<7)	/* device has 3+ buttons */


#ifdef __cplusplus
extern "C" {
#endif

extern int	mouse_type;
extern int	mouse_x, mouse_y, mouse_z;
extern int	mouse_buttons;


#ifdef EMU_DEVICE_H
extern device_t	mouse_logibus_device;
extern device_t	mouse_msinport_device;
#if 0
extern device_t	mouse_genibus_device;
#endif
extern device_t	mouse_mssystems_device;
extern device_t	mouse_msserial_device;
extern device_t	mouse_lserial_device;
extern device_t	mouse_mswheel_device;
extern device_t	mouse_ps2_device;
extern device_t	mouse_ps2ms_device;

extern void	*mouse_ps2_init(device_t *);
#endif

extern void	mouse_init(void);
extern void	mouse_reset(void);
extern void	mouse_process(void);
extern void	mouse_poll(void);
extern void	mouse_setpoll(int (*f)(int,int,int,int,void *), void *);

extern char	*mouse_get_name(int mouse);
extern char	*mouse_get_internal_name(int mouse);
extern int	mouse_get_from_internal_name(char *s);
extern int	mouse_get_type(int mouse);
extern int	mouse_get_ndev(void);

#ifdef __cplusplus
}
#endif


#endif	/*EMU_MOUSE_H*/
