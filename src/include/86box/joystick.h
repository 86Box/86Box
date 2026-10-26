/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the analog joystick handlers.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#ifndef EMU_JOYSTICK_H
#define EMU_JOYSTICK_H

extern void   *joystick_standard_init(void);
extern void    joystick_standard_close(void *priv);
extern uint8_t joystick_standard_read_2button(void *priv);
extern uint8_t joystick_standard_read_4button(void *priv);
extern void    joystick_standard_write(void *priv);
extern int     joystick_standard_read_axis_3axis_throttle(void *priv, int axis);
extern int     joystick_standard_read_axis_4axis(void *priv, int axis);
extern void    joystick_standard_a0_over(void *priv);

#endif /*EMU_JOYSTICK_H*/
