/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Intel AC'97 Header
 *
 *
 *
 * Authors: Tiseno100,
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2022      Tiseno100.
 *          Copyright 2022-2023 Jasmine Iwanek.
 *
 */

/*
 * Note: The Intel AC'97 code is divided into three parts
 *
 * 1. intel_ac97.c The main AC'97 code handling configuration.
 * 3. intel_ac97_buffer.c The AC'97 buffer
 *
 *
 * The general AC'97 configures the buffer base address and capabilities like channels, reset, interrupts etc.
 * The AC'97 buffer is where all playback happens.
 */

#ifndef EMU_INTEL_AC97_H
#define EMU_INTEL_AC97_H
#ifdef __cplusplus
extern "C" {
#endif
#include <86box/mem.h>
#include <86box/snd_ac97.h>

typedef struct intel_ac97_t {
    uint16_t ac97_base;
    uint16_t mixer_base;
    uint32_t buffer_base;

    uint8_t regs[256];
    int     irq;

    ac97_codec_t  *mixer;
    mem_mapping_t *buffer_location;
} intel_ac97_t;

/* AC'97 Configuration */
extern void intel_ac97_base(int enable, uint16_t addr, intel_ac97_t *dev);
extern void intel_ac97_mixer_base(int enable, uint16_t addr, intel_ac97_t *dev);
extern void intel_ac97_set_irq(int irq, intel_ac97_t *dev);

extern const device_t intel_ac97_device;
extern const device_t intel_ac97_mixer_device;

#ifdef __cplusplus
}
#endif

#endif /*EMU_INTEL_AC97_H*/
