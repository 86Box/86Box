/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Aureal Vortex 2 (AU8830) chip model: register file, DMA,
 *          audio routing, sample-rate converters, mixer, wavetable
 *          voices, A3D engine, crosstalk canceller and hardware EQ.
 *          The core has no 86Box dependencies; snd_vortex2.c wires it
 *          to PCI, memory, timers, the AC'97 codec and the sound output.
 *
 * Authors: Z2k Retro and contributors.
 *
 *          Copyright 2026 Z2k Retro.
 */
#ifndef SND_VORTEX2_CORE_H
#define SND_VORTEX2_CORE_H

#include <stdint.h>

#define V2_REG_SIZE  0x40000 /* BAR0: 256 KB */
#define V2_BLOCK     32      /* frames per processing block (0.67 ms) */
#define V2_RATE      48000

typedef struct v2_host_t {
    void *priv;
    /* Bus-master access to guest memory. */
    void (*mem_read)(void *priv, uint32_t addr, uint8_t *buf, uint32_t len);
    void (*mem_write)(void *priv, uint32_t addr, const uint8_t *buf, uint32_t len);
    /* PCI interrupt line level. */
    void (*set_irq)(void *priv, int level);
    /* Periodic timer: call v2_timer_expired() every this many 48 kHz
       samples (the counter reloads itself); 0 stops it. */
    void (*timer_arm)(void *priv, uint32_t samples);
    /* Current value of the 12.288 MHz sample clock (SMP_TIME). */
    uint32_t (*clock)(void *priv);
    /* AC'97 codec. */
    uint16_t (*codec_read)(void *priv, uint8_t reg);
    void (*codec_write)(void *priv, uint8_t reg, uint16_t val);
    /* Optional log sink (may be NULL). */
    void (*log)(void *priv, const char *msg);
    /* Optional: the codec's ADC input (what its record select picks, after
       record gain) for one block of V2_BLOCK samples at 48 kHz, left and
       right. The driver routes it (ADB sources 0x70/0x71) through the
       mixer: that is how CD audio reaches the output on an SQ2500. */
    void (*codec_adc)(void *priv, float *left, float *right, int n);
} v2_host_t;

typedef struct v2_chip_t v2_chip_t;

extern v2_chip_t *v2_create(const v2_host_t *host);
extern void       v2_destroy(v2_chip_t *c);
extern void       v2_reset(v2_chip_t *c);

extern uint32_t v2_read(v2_chip_t *c, uint32_t addr);
extern void     v2_write(v2_chip_t *c, uint32_t addr, uint32_t val);

/* Set or clear interrupt-source bits from outside the chip model (the
   host's MPU-401: IRQ_MIDI, 0x2000). The interrupt mask and enable apply. */
extern void v2_ext_irq(v2_chip_t *c, uint32_t bits, int set);

/* Called when the timer armed through host->timer_arm runs out. */
extern void v2_timer_expired(v2_chip_t *c);

/* Run one block of V2_BLOCK frames. out receives interleaved
   front L, front R, rear L, rear R as floats (1.0 = full scale). */
extern void v2_run_block(v2_chip_t *c, float *out);
/* Whether a capture (record) DMA channel is running and records the
   codec's ADC. */
extern int v2_capture_active(v2_chip_t *c);

/* Diagnostics: 1 if A3D, wavetable or ADB work happened in the last block. */
extern uint32_t v2_debug_flags(v2_chip_t *c);
extern const float *v2_debug_source(v2_chip_t *c, int addr);
extern int          v2_debug_source_live(v2_chip_t *c, int addr);

#endif
