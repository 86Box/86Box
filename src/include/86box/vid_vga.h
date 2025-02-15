/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the IBM MDA + VGA graphics cards.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2021 Jasmine Iwanek.
 */

#ifndef VIDEO_VGA_H
#define VIDEO_VGA_H

typedef struct vga_t {
    svga_t svga;

    rom_t bios_rom;
} vga_t;

extern void vga_out(uint16_t addr, uint8_t val, void *priv);
extern uint8_t vga_in(uint16_t addr, void *priv);

void vga_disable(void* p);
void vga_enable(void* p);
int vga_isenabled(void* p);

#endif /*VIDEO_VGA_H*/
