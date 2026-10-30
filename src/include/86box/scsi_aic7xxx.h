/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Adaptec's AIC-7xxx SCSI controllers: the AIC-7770 on the EISA
 *          AHA-274x, the AIC-7870 on the AHA-2940 and 2940W, and the
 *          AIC-7880 as the chip on a motherboard and as the AHA-2940 Ultra,
 *          Ultra Wide and 2944 Ultra Wide cards.
 *
 * Authors: Michael Pratte, <mpratte@makefox.group>
 *
 *          Copyright 2026 Michael Pratte.
 */
#ifndef SCSI_AIC7XXX_H
#define SCSI_AIC7XXX_H

extern const device_t aic7880_pci_device;
extern const device_t aha274x_device;
extern const device_t aha2940_pci_device;
extern const device_t aha2940u_pci_device;
extern const device_t aha2944uw_pci_device;

/* A card entry from before the models became options: the internal name it
   is now, with its settings moved over (or NULL for any other name). */
extern const char *aic_config_migrate(const char *internal_name, int slot);

#endif /*SCSI_AIC7XXX_H*/
