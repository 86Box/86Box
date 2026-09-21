/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Mindscape Music Board emulation.
 *
 * Authors: Roy Baer, <https://pcem-emulator.co.uk/>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025 Roy Baer.
 *          Copyright 2025 Jasmine Iwanek.
 */
#ifndef _SOUND_SND_MMB_H_
#define _SOUND_SND_MMB_H_

#define MMB_FREQ FREQ_48000

/* NOTE:
 * The constant clock rate is a deviation from the real hardware which has
 * the design flaw that the clock rate is always half the ISA bus clock.
 */
#define MMB_CLOCK 2386364

typedef struct ay_3_891x_s {
    uint8_t index;
    uint8_t regs[16];
    struct  ayumi chip;
} ay_3_891x_t;

typedef struct mmb_s {
    ay_3_891x_t first;
    ay_3_891x_t second;

    int16_t buffer[SOUNDBUFLEN * 2];
    int     pos;
} mmb_t;

#endif /* _SOUND_SND_MMB_H_ */
