/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          National Semiconductor PC87366(NSC366) Header
 *
 *
 *
 * Authors: Tiseno100,
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2022      Tiseno100.
 *          Copyright 2022-2023 Jasmine Iwanek.
 */

#ifndef EMU_NSC_366_H
#define EMU_NSC_366_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    hwm_values_t *values;

    uint8_t  fscm_enable;
    uint16_t fscm_addr;
    uint8_t  fscm_config[15];

    uint16_t vlm_addr;
    uint8_t  vlm_config_global[10];
    uint8_t  vlm_config_bank[13][5];

    uint16_t tms_addr;
    uint8_t  tms_config_global[10];
    uint8_t  tms_config_bank[4][5];
} nsc366_hwm_t;

extern void nsc366_update_fscm_io(int enable, uint16_t addr, nsc366_hwm_t *dev);
extern void nsc366_update_vlm_io(int enable, uint16_t addr, nsc366_hwm_t *dev);
extern void nsc366_update_tms_io(int enable, uint16_t addr, nsc366_hwm_t *dev);

/* The Hardware Monitor */
extern const device_t nsc366_hwm_device;

#ifdef __cplusplus
}
#endif

#endif /*EMU_NSC_366_H*/
