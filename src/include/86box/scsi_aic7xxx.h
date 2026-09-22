/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Adaptec's AIC-7xxx SCSI controllers: the AIC-7770 on the EISA
 *          AHA-2742, and the AIC-7870 and AIC-7880 as the chip on a
 *          motherboard and as the AHA-2940 Ultra and Ultra Wide cards.
 *
 * Authors: Michael Pratte, <mpratte@makefox.group>
 *
 *          Copyright 2026 Michael Pratte.
 */
#ifndef SCSI_AIC7XXX_H
#define SCSI_AIC7XXX_H

extern const device_t aic7880_pci_device;
extern const device_t aha2940u_pci_device;
extern const device_t aha2940uw_pci_device;
extern const device_t aha2740_device;
extern const device_t aha2742_device;
extern const device_t aha2740t_device;
extern const device_t aha2742t_device;
extern const device_t aha2740w_device;
extern const device_t aha2742w_device;
extern const device_t aha2744w_device;

#endif /*SCSI_AIC7XXX_H*/
