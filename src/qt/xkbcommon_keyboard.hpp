/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for xkbcommon keyboard input module.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2023 RichardG.
 */
extern void *xkbcommon_keymap;
void         xkbcommon_init(struct xkb_keymap *keymap);
void         xkbcommon_close();
uint16_t     xkbcommon_translate(uint32_t keycode);
