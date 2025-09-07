/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header for the implementation of the SiS 55xx Pentium
 *          PCI/ISA Chipsets.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2019-2020 Miran Grca.
 */
#ifndef EMU_SIS_55XX_H
#define EMU_SIS_55XX_H

typedef struct
{
    uint8_t     sb_pci_slot;
    uint8_t     ide_bits_1_3_writable;
    uint8_t     usb_enabled;

    uint8_t    *pmu_regs;

    sff8038i_t *bm[2];
    acpi_t     *acpi;
} sis_55xx_common_t;

extern void    sis_5511_host_to_pci_write(int addr, uint8_t val, void *priv);
extern uint8_t sis_5511_host_to_pci_read(int addr, void *priv);
extern void    sis_5571_host_to_pci_write(int addr, uint8_t val, void *priv);
extern uint8_t sis_5571_host_to_pci_read(int addr, void *priv);
extern void    sis_5581_host_to_pci_write(int addr, uint8_t val, void *priv);
extern uint8_t sis_5581_host_to_pci_read(int addr, void *priv);
extern void    sis_5591_host_to_pci_write(int addr, uint8_t val, void *priv);
extern uint8_t sis_5591_host_to_pci_read(int addr, void *priv);
extern void    sis_5600_host_to_pci_write(int addr, uint8_t val, void *priv);
extern uint8_t sis_5600_host_to_pci_read(int addr, void *priv);

extern void    sis_5513_pci_to_isa_write(int addr, uint8_t val, void *priv);
extern uint8_t sis_5513_pci_to_isa_read(int addr, void *priv);
extern void    sis_5513_ide_write(int addr, uint8_t val, void *priv);
extern uint8_t sis_5513_ide_read(int addr, void *priv);
extern void    sis_5572_usb_write(int addr, uint8_t val, void *priv);
extern uint8_t sis_5572_usb_read(int addr, void *priv);
extern void    sis_5595_pmu_write(int addr, uint8_t val, void *priv);
extern uint8_t sis_5595_pmu_read(int addr, void *priv);

extern const device_t sis_5511_h2p_device;
extern const device_t sis_5571_h2p_device;
extern const device_t sis_5581_h2p_device;
extern const device_t sis_5591_h2p_device;
extern const device_t sis_5600_h2p_device;

extern const device_t sis_5513_p2i_device;
extern const device_t sis_5572_p2i_device;
extern const device_t sis_5582_p2i_device;
extern const device_t sis_5595_1997_p2i_device;
extern const device_t sis_5595_p2i_device;

extern const device_t sis_5513_ide_device;
extern const device_t sis_5572_ide_device;
extern const device_t sis_5582_ide_device;
extern const device_t sis_5591_5600_ide_device;

extern const device_t sis_5572_usb_device;
extern const device_t sis_5582_usb_device;
extern const device_t sis_5595_usb_device;

extern const device_t sis_5595_pmu_device;
extern const device_t sis_5595_1997_pmu_device;

extern const device_t sis_55xx_common_device;


#endif /*EMU_SIS_55XX_H*/
