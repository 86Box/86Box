/*
 * 86Box
 *
 * Juko ST motherboard extended RAM window.
 *
 * This header declares the machine-local device that emulates the
 * Juko ST port E0h memory mapper.
 *
 * Intended pair:
 *
 *   src/machine/m_xt_jukost.c
 *   src/machine/m_xt_jukost.h
 */

#ifndef EMU_MACHINE_M_XT_JUKOST_H
#define EMU_MACHINE_M_XT_JUKOST_H

#include <86box/device.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const device_t jukopc_device;
extern const device_t juko_st_mapper_device;

#ifdef __cplusplus
}
#endif

#endif /* EMU_MACHINE_M_XT_JUKOST_H */
