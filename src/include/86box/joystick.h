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
 *          Copyright 2025      Jasmine Iwanek.
 */
#ifndef EMU_JOYSTICK_H
#define EMU_JOYSTICK_H

void   *joystick_standard_init(void);
void    joystick_standard_close(UNUSED(void *priv));
uint8_t joystick_standard_read_2button(UNUSED(void *priv));
uint8_t joystick_standard_read_4button(UNUSED(void *priv));
void    joystick_standard_write(UNUSED(void *priv));
int     joystick_standard_read_axis_3axis_throttle(UNUSED(void *priv), int axis);
int     joystick_standard_read_axis_4axis(UNUSED(void *priv), int axis);
void    joystick_standard_a0_over(UNUSED(void *priv));

#endif /*EMU_JOYSTICK_H*/
