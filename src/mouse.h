/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the MOUSE driver.
 *
 * Version:	@(#)mouse.h	1.0.3	2017/06/21
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */
#ifndef EMU_MOUSE_H
# define EMU_MOUSE_H


#define MOUSE_TYPE_NONE		 0
#if 0
#define MOUSE_TYPE_GENIUS	11	/* Genius Bus Mouse */
#endif
#define MOUSE_TYPE_BUS		 1	/* Logitech/ATI Bus Mouse */
#define MOUSE_TYPE_INPORT	 2	/* Microsoft InPort Mouse */
#define MOUSE_TYPE_MSYSTEMS	 3	/* Mouse Systems mouse */
#define MOUSE_TYPE_SERIAL	 4	/* Serial Mouse */
#define MOUSE_TYPE_LOGITECH	 5	/* Logitech Serial Mouse */
#define MOUSE_TYPE_MSWHEEL	 6	/* Serial Wheel Mouse */
#define MOUSE_TYPE_PS2		 7	/* IBM PS/2 series Bus Mouse */
#define MOUSE_TYPE_PS2_MS	 8	/* Microsoft Intellimouse PS/2 */
#define MOUSE_TYPE_AMSTRAD	 9	/* Amstrad PC system mouse */
#define MOUSE_TYPE_OLIM24	10	/* Olivetti M24 system mouse */

#define MOUSE_TYPE_MASK		0x0f
#define MOUSE_TYPE_3BUTTON	(1<<7)	/* device has 3+ buttons */


typedef struct {
    char	name[80];
    char	internal_name[24];
    int		type;
    void	*(*init)(void);
    void	(*close)(void *p);
    uint8_t	(*poll)(int x, int y, int z, int b, void *p);
} mouse_t;


extern int	mouse_type;


extern void	mouse_emu_init(void);
extern void	mouse_emu_close(void);
extern void	mouse_poll(int x, int y, int z, int b);
extern char	*mouse_get_name(int mouse);
extern char	*mouse_get_internal_name(int mouse);
extern int	mouse_get_from_internal_name(char *s);
extern int	mouse_get_type(int mouse);
extern int	mouse_get_ndev(void);


#endif	/*EMU_MOUSE_H*/
