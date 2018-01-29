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
 * Version:	@(#)mouse.h	1.0.14	2018/01/29
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
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
#define MOUSE_TYPE_PS2		9	/* PS/2 series Bus Mouse */


#ifdef __cplusplus
extern "C" {
#endif

extern int	mouse_type;
extern int	mouse_x, mouse_y, mouse_z;
extern int	mouse_buttons;


#ifdef EMU_DEVICE_H
extern device_t	*mouse_get_device(int mouse);
extern void	*mouse_ps2_init(device_t *);

extern device_t	mouse_logibus_device;
extern device_t	mouse_msinport_device;
#if 0
extern device_t	mouse_genibus_device;
#endif
extern device_t	mouse_mssystems_device;
extern device_t	mouse_msserial_device;
extern device_t	mouse_ps2_device;
#endif

extern void	mouse_init(void);
extern void	mouse_close(void);
extern void	mouse_reset(void);
extern void	mouse_set_buttons(int buttons);
extern void	mouse_process(void);
extern void	mouse_set_poll(int (*f)(int,int,int,int,void *), void *);
extern void	mouse_poll(void);

extern char	*mouse_get_name(int mouse);
extern char	*mouse_get_internal_name(int mouse);
extern int	mouse_get_from_internal_name(char *s);
extern int	mouse_has_config(int mouse);
extern int	mouse_get_type(int mouse);
extern int	mouse_get_ndev(void);
extern int	mouse_get_buttons(void);

#ifdef __cplusplus
}
#endif


#endif	/*EMU_MOUSE_H*/
