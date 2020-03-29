/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the ISA Bus (de)Bugger expansion card
 *		sold as a DIY kit in the late 1980's in The Netherlands.
 *		This card was a assemble-yourself 8bit ISA addon card for
 *		PC and AT systems that had several tools to aid in low-
 *		level debugging (mostly for faulty BIOSes, bootloaders
 *		and system kernels...)
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
# define APM_H


#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t cmd,
	    stat, do_smi;
} apm_t;


/* Global variables. */
extern const device_t	apm_device;


/* Functions. */
extern void		apm_set_do_smi(apm_t *apm, uint8_t do_smi);

#ifdef __cplusplus
}
#endif


#endif	/*APM_H*/
