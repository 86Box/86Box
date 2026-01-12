/*
 * 86Box        A hypervisor and IBM PC system emulator that specializes in
 *              running old operating systems and software designed for IBM
 *              PC systems and compatibles from 1981 through fairly recent
 *              system designs based on the PCI bus.
 *
 *              This file is part of the 86Box distribution.
 *
 *              Definitions for platform specific serial to host passthrough.
 *
 *
 * Authors:     Andreas J. Reichel <webmaster@6th-dimension.com>,
 *              Jasmine Iwanek <jasmine@iwanek.co.uk>
 *
 *              Copyright 2021      Andreas J. Reichel.
 *              Copyright 2021-2025 Jasmine Iwanek.
 */
#ifndef PLAT_SERIAL_PASSTHROUGH_H
#define PLAT_SERIAL_PASSTHROUGH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void plat_serpt_write(void *priv, uint8_t data);
extern int  plat_serpt_read(void *priv, uint8_t *data);
extern int  plat_serpt_open_device(void *priv);
extern void plat_serpt_close(void *priv);
extern void plat_serpt_set_params(void *priv);
extern void plat_serpt_set_line_state(void *priv);

#ifdef __cplusplus
}
#endif

#endif /* PLAT_SERIAL_PASSTHROUGH_H */
