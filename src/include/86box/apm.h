/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the Advanced Power Management emulation.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2019 Miran Grca.
 */
#ifndef APM_H
#define APM_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t cmd,
        stat, do_smi;
} apm_t;

/* Global variables. */
extern const device_t apm_device;

extern const device_t apm_pci_device;
extern const device_t apm_pci_acpi_device;

/* Functions. */
extern void apm_set_do_smi(apm_t *dev, uint8_t do_smi);

#ifdef __cplusplus
}
#endif

#endif /*APM_H*/
