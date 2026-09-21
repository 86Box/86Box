/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions shared between the Iomega Ditto parallel-port
 *          tape drive and the Tape drives settings tab / media menu.
 */
#ifndef EMU_LPT_DITTO_H
#define EMU_LPT_DITTO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* tape_drive_types[] index (Ditto models) <-> Ditto drive-model config
   value. Both return -1 when there is no mapping. */
extern int ditto_model_from_tape_type(uint32_t type);
extern int ditto_tape_type_from_model(int model);

/* tape_types[] (medium) index <-> Ditto cartridge config value. Both
   return -1 when there is no mapping. */
extern int ditto_cartridge_from_medium(uint32_t medium);
extern int ditto_tape_medium_from_cartridge(int cartridge);

/* Runtime cartridge swap on the (single) live parallel-port drive. Both
   are no-ops when no drive is attached; load accepts a "wp://" prefix. */
extern void lpt_ditto_eject(void);
extern void lpt_ditto_load(const char *path, int read_only);

#ifdef __cplusplus
}
#endif

#endif /*EMU_LPT_DITTO_H*/
