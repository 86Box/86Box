/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM CGA composite filter, borrowed from reenigne's DOSBox
 *          patch and ported to C.
 *
 *
 *
 * Authors: reenigne,
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2015-2018 reenigne.
 *          Copyright 2015-2018 Miran Grca.
 */

#ifndef VIDEO_CGA_COMP_H
#define VIDEO_CGA_COMP_H

#define Bitu unsigned int
#define bool uint8_t

void    update_cga16_color(uint8_t cgamode);
void    cga_comp_init(int revision);
Bit32u *Composite_Process(uint8_t cgamode, uint8_t border, uint32_t blocks /*, bool doublewidth*/, uint32_t *TempLine);

#endif /*VIDEO_CGA_COMP_H*/
