/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          PC Convertible-style soft power control card.
 *
 * Authors: Josh Rodd
 *
 *          Copyright 2026 Josh Rodd.
 */
#ifndef SOFTPOWER_H
#define SOFTPOWER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Global variables. */
extern const device_t softpower_device;

/* Shared control/deadline logic; the board supplies its own NMI and rail actions.
   No I/O handler is installed by this interface. */
typedef struct softpower_control_t softpower_control_t;
extern softpower_control_t *softpower_control_create(unsigned delay_ms,
                                                    void (*suspend)(void *),
                                                    void (*power_off)(void *),
                                                    void (*reset)(void *), void *priv);
extern void softpower_control_destroy(softpower_control_t *control);
extern void softpower_control_reset(softpower_control_t *control);
extern void softpower_control_write(softpower_control_t *control, uint8_t value);
extern uint8_t softpower_control_read(const softpower_control_t *control);

#ifdef __cplusplus
}
#endif

#endif /*SOFTPOWER_H*/
