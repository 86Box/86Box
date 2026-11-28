/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Texas Instruments TMS5200/TMS5220/TMS5220C speech synthesizer.
 *
 *          Ported from MAME's tms5220.cpp (BSD-3-Clause; Frank Palazzolo,
 *          Aaron Giles, Jonathan Gevaryahu, Raphael Nabet, Couriersud,
 *          Michael Zapf). No VSM (speech ROM) interface: only Speak
 *          External is useful.
 */
#ifndef SOUND_TMS5220_H
#define SOUND_TMS5220_H

enum {
    TMS5220_TYPE_5200  = 0,
    TMS5220_TYPE_5220  = 1,
    TMS5220_TYPE_5220C = 2
};

typedef struct tms5220_t tms5220_t;

/* sample_cb is called once per output sample (clock / 80 Hz) with a signed
   16-bit sample; irq_cb gets the /INT pin level (active low). */
extern tms5220_t *tms5220_init(int type, uint32_t clock,
                               void (*sample_cb)(int16_t sample, void *priv),
                               void (*irq_cb)(int state, void *priv),
                               void *priv);
extern void       tms5220_close(tms5220_t *tms);
extern void       tms5220_reset(tms5220_t *tms);

extern void    tms5220_rsq_w(tms5220_t *tms, int state);
extern void    tms5220_wsq_w(tms5220_t *tms, int state);
extern void    tms5220_data_w(tms5220_t *tms, uint8_t data);
extern uint8_t tms5220_status_r(tms5220_t *tms);
extern int     tms5220_readyq_r(tms5220_t *tms);
extern int     tms5220_intq_r(tms5220_t *tms);

#endif /*SOUND_TMS5220_H*/
