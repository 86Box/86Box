/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SiS 530/5595 Chipset.
 *
 * Authors: mw308
 *
 *          Copyright 2026 mw308
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/apm.h>
#include <86box/acpi.h>
#include <86box/keyboard.h>
#include <86box/hdd.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/pit_fast.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>
#include <86box/port_92.h>
#include <86box/smram.h>
#include <86box/spd.h>
#include <86box/sis_55xx.h>
#include <86box/chipset.h>

#ifdef ENABLE_SIS_530_LOG
int sis_530_do_log = ENABLE_SIS_530_LOG;

static void
sis_530_log(const char *fmt, ...)
{
    va_list ap;

    if (sis_530_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sis_530_log(fmt, ...)
#endif

typedef struct sis_530_t {
    uint8_t            nb_slot;
    uint8_t            sb_slot;

    void              *h2p;
    void              *p2i;
    void              *ide;
    void              *usb;
    void              *pmu;

    sis_55xx_common_t *sis;
} sis_530_t;

static void
sis_530_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    const sis_530_t *dev = (sis_530_t *) priv;

    sis_530_log("SiS 530: [W] dev->pci_conf[%02X] = %02X\n", addr, val);

    if (func == 0x00)
        sis_530_host_to_pci_write(addr, val, dev->h2p);
    else if (func == 0x01)
        sis_5513_ide_write(addr, val, dev->ide);
}

static uint8_t
sis_530_read(int func, int addr, UNUSED(int len), void *priv)
{
    const sis_530_t *dev = (sis_530_t *) priv;
    uint8_t ret = 0xff;

    if (func == 0x00)
        ret = sis_530_host_to_pci_read(addr, dev->h2p);
    else if (func == 0x01)
        ret = sis_5513_ide_read(addr, dev->ide);

    sis_530_log("SiS 530: [R] dev->pci_conf[%02X] = %02X\n", addr, ret);

    return ret;
}

static void
sis_5595_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    const sis_530_t *dev = (sis_530_t *) priv;

    sis_530_log("SiS 5595: [W] dev->pci_conf[%02X] = %02X\n", addr, val);

    switch (func) {
        case 0x00:
            sis_5513_pci_to_isa_write(addr, val, dev->p2i);
            break;
        case 0x01:
            sis_5595_pmu_write(addr, val, dev->pmu);
            break;
        case 0x02:
            sis_5572_usb_write(addr, val, dev->usb);
            break;
    }
}

static uint8_t
sis_5595_read(int func, int addr, UNUSED(int len), void *priv)
{
    const sis_530_t *dev = (sis_530_t *) priv;
    uint8_t ret = 0xff;

    switch (func) {
        case 0x00:
            ret = sis_5513_pci_to_isa_read(addr, dev->p2i);
            break;
        case 0x01:
            ret = sis_5595_pmu_read(addr, dev->pmu);
            break;
        case 0x02:
            ret = sis_5572_usb_read(addr, dev->usb);
            break;
    }

    sis_530_log("SiS 5595: [R] dev->pci_conf[%02X] = %02X\n", addr, ret);

    return ret;
}

static void
sis_530_close(void *priv)
{
    sis_530_t *dev = (sis_530_t *) priv;

    free(dev);
}

static void *
sis_530_init(UNUSED(const device_t *info))
{
    sis_530_t *dev = (sis_530_t *) calloc(1, sizeof(sis_530_t));

    /* Device 0: SiS 530 */
    pci_add_card(PCI_ADD_NORTHBRIDGE, sis_530_read, sis_530_write, dev, &dev->nb_slot);
    /* Device 1: SiS 5595 */
    pci_add_card(PCI_ADD_SOUTHBRIDGE, sis_5595_read, sis_5595_write, dev, &dev->sb_slot);

    dev->sis = device_add(&sis_55xx_common_device);

    dev->ide = device_add_linked(&sis_5591_5600_ide_device, dev->sis);
    dev->p2i = device_add_linked(&sis_5595_p2i_device, dev->sis);
    dev->h2p = device_add_linked(&sis_530_h2p_device, dev->sis);
    dev->usb = device_add_linked(&sis_5595_usb_device, dev->sis);
    dev->pmu = device_add_linked(&sis_5595_pmu_device, dev->sis);

    device_add_params(&kbc_at_device, (void *) KBC_VEN_SIS);

    return dev;
}

const device_t sis_530_device = {
    .name          = "SiS 530/5595",
    .internal_name = "sis_530",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = sis_530_init,
    .close         = sis_530_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
