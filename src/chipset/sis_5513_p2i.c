/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SiS 5513 PCI to ISA bridge.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2024 Miran Grca.
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

#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/nvr.h>
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
#include <86box/apm.h>
#include <86box/ddma.h>
#include <86box/acpi.h>
#include <86box/smbus.h>
#include <86box/sis_55xx.h>
#include <86box/chipset.h>

#ifdef ENABLE_SIS_5513_PCI_TO_ISA_LOG
int sis_5513_pci_to_isa_do_log = ENABLE_SIS_5513_PCI_TO_ISA_LOG;

static void
sis_5513_pci_to_isa_log(const char *fmt, ...)
{
    va_list ap;

    if (sis_5513_pci_to_isa_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sis_5513_pci_to_isa_log(fmt, ...)
#endif

typedef struct sis_5513_pci_to_isa_t {
    uint8_t            rev;
    uint8_t            index;
    uint8_t            dam_index;
    uint8_t            irq_state;
    uint8_t            dam_enable;
    uint8_t            dam_irq_enable;
    uint8_t            ddma_enable;
    uint8_t            pci_conf[256];
    uint8_t            regs[16];
    uint8_t            dam_regs[256];
    uint8_t            apc_regs[256];

    uint16_t           dam_base;
    uint16_t           ddma_base;
    uint16_t           acpi_io_base;

    sis_55xx_common_t *sis;
    port_92_t         *port_92;
    void              *pit;
    nvr_t             *nvr;
    char              *fn;
    ddma_t            *ddma;
    acpi_t            *acpi;
    void              *smbus;

    uint8_t          (*pit_read_reg)(void *priv, uint8_t reg);
} sis_5513_pci_to_isa_t;

static void
sis_5595_acpi_recalc(sis_5513_pci_to_isa_t *dev)
{
    dev->acpi_io_base = (dev->pci_conf[0x91] << 8) | (dev->pci_conf[0x90] & 0xc0);
    acpi_update_io_mapping(dev->sis->acpi, dev->acpi_io_base, (dev->pci_conf[0x40] & 0x80));
}

static void
sis_5513_apc_reset(sis_5513_pci_to_isa_t *dev)
{
    memset(dev->apc_regs, 0x00, sizeof(dev->apc_regs));

    if (dev->rev == 0xb0) {
        dev->apc_regs[0x03] = 0x80;
        dev->apc_regs[0x04] = 0x38;
        dev->apc_regs[0x07] = 0x01;
    } else
        dev->apc_regs[0x04] = 0x08;
}

static void
sis_5513_apc_write(UNUSED(uint16_t addr), uint8_t val, void *priv)
{
    sis_5513_pci_to_isa_t *dev = (sis_5513_pci_to_isa_t *) priv;
    uint8_t nvr_index = nvr_get_index(dev->nvr, 0);

    sis_5513_pci_to_isa_log("SiS 5595 APC: [W] %04X = %02X\n", addr, val);

    switch (nvr_index) {
        case 0x02 ... 0x04:
            dev->apc_regs[nvr_index] = val;
            break;
        case 0x05:
        case 0x07 ... 0x08:
            if (dev->rev == 0xb0)
                dev->apc_regs[nvr_index] = val;
            break;
    }
}

static uint8_t
sis_5513_apc_read(UNUSED(uint16_t addr), void *priv)
{
    sis_5513_pci_to_isa_t *dev = (sis_5513_pci_to_isa_t *) priv;
    uint8_t nvr_index = nvr_get_index(dev->nvr, 0);
    uint8_t ret = 0xff;

    ret = dev->apc_regs[nvr_index];

    if (nvr_index == 0x06)
        dev->apc_regs[nvr_index] = 0x00;

    sis_5513_pci_to_isa_log("SiS 5595 APC: [R] %04X = %02X\n", addr, ret);

    return ret;
}

void
sis_5513_apc_recalc(sis_5513_pci_to_isa_t *dev, uint8_t apc_on)
{
    nvr_at_data_port(!apc_on, dev->nvr);
    io_removehandler(0x0071, 0x0001,
                     sis_5513_apc_read, NULL, NULL, sis_5513_apc_write, NULL, NULL, dev);

    if (apc_on)
        io_removehandler(0x0071, 0x0001,
                         sis_5513_apc_read, NULL, NULL, sis_5513_apc_write, NULL, NULL, dev);
}

static void
sis_5595_do_nmi(sis_5513_pci_to_isa_t *dev, int set)
{
    if (set)
        nmi_raise();

    dev->irq_state = set;
}

static void
sis_5595_dam_reset(sis_5513_pci_to_isa_t *dev)
{
    if (dev->irq_state) {
        if (dev->dam_regs[0x40] & 0x20)
            sis_5595_do_nmi(dev, 0);
        else if (dev->dam_irq_enable)
            pci_clear_mirq(6, 1, &dev->irq_state);
    }

    memset(dev->dam_regs, 0x00, sizeof(dev->dam_regs));

    dev->dam_regs[0x40] = 0x08;
    dev->dam_regs[0x47] = 0x50;
}

static void
sis_5595_dam_write(uint16_t addr, uint8_t val, void *priv)
{
    sis_5513_pci_to_isa_t *dev = (sis_5513_pci_to_isa_t *) priv;
    uint16_t reg = addr - dev->dam_base;
    uint8_t old;

    sis_5513_pci_to_isa_log("SiS 5595 DAM: [W] %04X = %02X\n", addr, val);

    switch (reg) {
        case 0x05:
            dev->dam_index = (dev->index & 0x80) | (val & 0x7f);
            break;
        case 0x06:
            switch (dev->dam_index) {
                case 0x40:
                    old = dev->dam_regs[0x40];
                    dev->dam_regs[0x40] = val & 0xef;
                    if (val & 0x80) {
                        sis_5595_dam_reset(dev);
                        return;
                    }
                    if (dev->irq_state) {
                        if (!(old & 0x20) && (val & 0x20)) {
                            if (dev->dam_irq_enable)
                                pci_clear_mirq(6, 1, &dev->irq_state);
                            sis_5595_do_nmi(dev, 1);
                        } else if ((old & 0x20) && !(val & 0x20)) {
                            sis_5595_do_nmi(dev, 0);
                            if (dev->dam_irq_enable)
                                pci_set_mirq(6, 1, &dev->irq_state);
                        }
                    }
                    if ((val & 0x08) && dev->dam_irq_enable)
                        pci_clear_mirq(6, 1, &dev->irq_state);
                    break;
                case 0x43 ... 0x47:
                    dev->dam_regs[dev->dam_index] = val;
                    break;
                case 0x2b ... 0x34:
                case 0x6b ... 0x74:
                case 0x3b ... 0x3c:
                case 0x7b ... 0x7c:
                    dev->dam_regs[dev->dam_index & 0x3f] = val;
                    break;
            }
            break;
    }
}

static uint8_t
sis_5595_dam_read(uint16_t addr, void *priv)
{
    sis_5513_pci_to_isa_t *dev = (sis_5513_pci_to_isa_t *) priv;
    uint16_t reg = addr - dev->dam_base;
    uint8_t ret = 0xff;

    switch (reg) {
        case 0x05:
            ret = dev->dam_index;
            break;
        case 0x06:
            switch (dev->dam_index) {
                default:
                    ret = dev->dam_regs[dev->dam_index];
                    break;
                case 0x20 ... 0x29:
                case 0x2b ... 0x3f:
                    ret = dev->dam_regs[dev->dam_index & 0x3f];
                    break;
                case 0x2a:
                case 0x6a:
                    ret = dev->pci_conf[0x78];
                    break;
            }
            break;
    }

    sis_5513_pci_to_isa_log("SiS 5595 DAM: [R] %04X = %02X\n", addr, ret);

    return ret;
}

static void
sis_5595_dam_recalc(sis_5513_pci_to_isa_t *dev)
{
    if (dev->dam_enable && (dev->dam_base != 0x0000))
        io_removehandler(dev->dam_base, 0x0008,
                         sis_5595_dam_read, NULL, NULL, sis_5595_dam_write, NULL, NULL, dev);

    dev->dam_base = dev->pci_conf[0x68] | (dev->pci_conf[0x69] << 16);
    dev->dam_enable = !!(dev->pci_conf[0x7b] & 0x80);

    if (dev->dam_enable && (dev->dam_base != 0x0000))
        io_sethandler(dev->dam_base, 0x0008,
                      sis_5595_dam_read, NULL, NULL, sis_5595_dam_write, NULL, NULL, dev);
}

static void
sis_5595_ddma_recalc(sis_5513_pci_to_isa_t *dev)
{
    uint16_t ch_base;

    dev->ddma_base = (dev->pci_conf[0x80] & 0xf0) | (dev->pci_conf[0x81] << 16);
    dev->ddma_enable = !!(dev->pci_conf[0x80] & 0x01);

    for (uint8_t i = 0; i < 8; i++) {
        ch_base = dev->ddma_base + (i << 4);
        ddma_update_io_mapping(dev->ddma, i, ch_base & 0xff, (ch_base >> 8),
                               dev->ddma_enable && (dev->pci_conf[0x84] & (1 << i)) &&
                               (dev->ddma_base != 0x0000));
    }
}

static void
sis_5513_00_pci_to_isa_write(int addr, uint8_t val, sis_5513_pci_to_isa_t *dev)
{
    switch (addr) {
        case 0x60: /* MIRQ0 Remapping Control Register */
        case 0x61: /* MIRQ1 Remapping Control Register */
            sis_5513_pci_to_isa_log("Set MIRQ routing: MIRQ%i -> %02X\n", addr & 0x01, val);
            dev->pci_conf[addr] = val & 0xcf;
            if (val & 0x80)
                pci_set_mirq_routing(PCI_MIRQ0 + (addr & 0x01), PCI_IRQ_DISABLED);
            else
                pci_set_mirq_routing(PCI_MIRQ0 + (addr & 0x01), val & 0xf);
            break;

        case 0x62: /* On-board Device DMA Control Register */
            dev->pci_conf[addr] = val;
            break;

        case 0x63: /* IDEIRQ Remapping Control Register */
            sis_5513_pci_to_isa_log("Set MIRQ routing: IDEIRQ -> %02X\n", val);
            dev->pci_conf[addr] = val & 0x8f;
            if (val & 0x80)
                pci_set_mirq_routing(PCI_MIRQ2, PCI_IRQ_DISABLED);
            else
                pci_set_mirq_routing(PCI_MIRQ2, val & 0xf);
            break;

        case 0x64: /* GPIO0 Control Register */
            dev->pci_conf[addr] = val & 0xef;
            break;

        case 0x65:
            dev->pci_conf[addr] = val & 0x80;
            break;

        case 0x66: /* GPIO0 Output Mode Control Register */
        case 0x67: /* GPIO0 Output Mode Control Register */
            dev->pci_conf[addr] = val;
            break;

        case 0x6a: /* GPIO Status Register */
            dev->pci_conf[addr] |= (val & 0x10);
            dev->pci_conf[addr] &= ~(val & 0x01);
            break;

        default:
            break;
    }
}

static void
sis_5513_01_pci_to_isa_write(int addr, uint8_t val, sis_5513_pci_to_isa_t *dev)
{
    uint8_t old;

    switch (addr) {
        /* Simply skip MIRQ0, so we can reuse the SiS 551x IDEIRQ infrastructure. */
        case 0x61: /* MIRQ Remapping Control Register */
            sis_5513_pci_to_isa_log("Set MIRQ routing: MIRQ%i -> %02X\n", addr & 0x01, val);
            dev->pci_conf[addr] = val & 0xcf;
            if (val & 0x80)
                pci_set_mirq_routing(PCI_MIRQ0 + (addr & 0x01), PCI_IRQ_DISABLED);
            else
                pci_set_mirq_routing(PCI_MIRQ0 + (addr & 0x01), val & 0xf);
            break;

        case 0x62: /* On-board Device DMA Control Register */
            dev->pci_conf[addr] = val;
            break;

        case 0x63: /* IDEIRQ Remapping Control Register */
            sis_5513_pci_to_isa_log("Set MIRQ routing: IDEIRQ -> %02X\n", val);
            dev->pci_conf[addr] = val & 0x8f;
            if (val & 0x80)
                 pci_set_mirq_routing(PCI_MIRQ2, PCI_IRQ_DISABLED);
            else
                pci_set_mirq_routing(PCI_MIRQ2, val & 0xf);
            break;

        case 0x64: /* GPIO Control Register */
            dev->pci_conf[addr] = val & 0xef;
            break;

        case 0x65:
            dev->pci_conf[addr] = val & 0x1b;
            break;

        case 0x66: /* GPIO Output Mode Control Register */
        case 0x67: /* GPIO Output Mode Control Register */
            dev->pci_conf[addr] = val;
            break;

        case 0x68: /* USBIRQ Remapping Control Register */
            sis_5513_pci_to_isa_log("Set MIRQ routing: USBIRQ -> %02X\n", val);
            dev->pci_conf[addr] = val & 0xcf;
            if (val & 0x80)
                 pci_set_mirq_routing(PCI_MIRQ3, PCI_IRQ_DISABLED);
            else
                pci_set_mirq_routing(PCI_MIRQ3, val & 0xf);
            break;

        case 0x69:
            dev->pci_conf[addr] = val;
            break;

        case 0x6a:
            dev->pci_conf[addr] = val & 0xfc;
            break;

        case 0x6b:
            dev->pci_conf[addr] = val;
            break;

        case 0x6c:
            dev->pci_conf[addr] = val & 0x02;
            break;

        case 0x6e: /* Software-Controlled Interrupt Request, Channels 7-0 */
            old = dev->pci_conf[addr];
            picint((val ^ old) & val);
            picintc((val ^ old) & ~val);
            dev->pci_conf[addr] = val;
            break;

        case 0x6f: /* Software-Controlled Interrupt Request, channels 15-8 */
            old = dev->pci_conf[addr];
            picint(((val ^ old) & val) << 8);
            picintc(((val ^ old) & ~val) << 8);
            dev->pci_conf[addr] = val;
            break;

        case 0x70:
            dev->pci_conf[addr] = (dev->pci_conf[addr] & 0x02) | (val & 0xdc);
            break;

        case 0x71: /* Type-F DMA Control Register */
            dev->pci_conf[addr] = val & 0xef;
            break;

        case 0x72: /* SMI Triggered By IRQ/GPIO Control */
        case 0x73: /* SMI Triggered By IRQ/GPIO Control */
            dev->pci_conf[addr] = val;
            break;

        case 0x74: /* System Standby Timer Reload,
                      System Standby State Exit And Throttling State Exit Control */
        case 0x75: /* System Standby Timer Reload,
                      System Standby State Exit And Throttling State Exit Control */
        case 0x76: /* Monitor Standby Timer Reload And Monitor Standby State ExitControl */
        case 0x77: /* Monitor Standby Timer Reload And Monitor Standby State ExitControl */
            dev->pci_conf[addr] = val;
            break;

        default:
            break;
    }
}

static void
sis_5513_11_pci_to_isa_write(int addr, uint8_t val, sis_5513_pci_to_isa_t *dev)
{
    uint8_t old;

    switch (addr) {
        case 0x61: /* IDEIRQ Remapping Control Register */
            sis_5513_pci_to_isa_log("Set MIRQ routing: IDEIRQ -> %02X\n", val);
            dev->pci_conf[addr] = val & 0xcf;
            dev->sis->ide_bits_1_3_writable = !!(val & 0x40);
            if (val & 0x80)
                pci_set_mirq_routing(PCI_MIRQ2, PCI_IRQ_DISABLED);
            else
                pci_set_mirq_routing(PCI_MIRQ2, val & 0xf);
            break;

        case 0x62: /* USBIRQ Remapping Control Register */
            sis_5513_pci_to_isa_log("Set MIRQ routing: USBIRQ -> %02X\n", val);
            dev->pci_conf[addr] = val;
            dev->sis->usb_enabled = !!(val & 0x40);
            if (val & 0x80)
                 pci_set_mirq_routing(PCI_MIRQ3, PCI_IRQ_DISABLED);
            else
                pci_set_mirq_routing(PCI_MIRQ3, val & 0xf);
            break;

        case 0x63: /* GPCS0 Control Register */
        case 0x64: /* GPCS1 Control Register */
        case 0x65: /* GPCS0 Output Mode Control Register */
        case 0x66: /* GPCS0 Output Mode Control Register */
        case 0x67: /* GPCS1 Output Mode Control Register */
        case 0x68: /* GPCS1 Output Mode Control Register */
        case 0x6b:
        case 0x6c:
            dev->pci_conf[addr] = val;
            break;

        case 0x69: /* GPCS0/1 De-Bounce Control Register */
            dev->pci_conf[addr] = val & 0xdf;
            if ((dev->apc_regs[0x03] & 0x40) && (val & 0x10)) {
                plat_power_off();
                return;
            }
            break;

        case 0x6a: /* ACPI/SCI IRQ Remapping Control Register */
            sis_5513_pci_to_isa_log("Set MIRQ routing: ACPI/SCI IRQ -> %02X\n", val);
            dev->pci_conf[addr] = val;
            if (val & 0x80)
                 pci_set_mirq_routing(PCI_MIRQ5, PCI_IRQ_DISABLED);
            else
                pci_set_mirq_routing(PCI_MIRQ5, val & 0xf);
            break;

        case 0x6d: /* I2C Bus Control Register */
            dev->pci_conf[addr] = val;
            /* TODO: Keyboard/mouse swapping and keyboard hot key. */
            break;

        case 0x6e: /* Software-Controlled Interrupt Request, Channels 7-0 */
            old = dev->pci_conf[addr];
            picint((val ^ old) & val);
            picintc((val ^ old) & ~val);
            dev->pci_conf[addr] = val;
            break;

        case 0x6f: /* Software-Controlled Interrupt Request, channels 15-8 */
            old = dev->pci_conf[addr];
            picint(((val ^ old) & val) << 8);
            picintc(((val ^ old) & ~val) << 8);
            dev->pci_conf[addr] = val;
            break;

        case 0x70: /* Misc. Controller Register */
            dev->pci_conf[addr] = val;
            /* TODO: Keyboard Lock Enable/Disable. */
            break;

        case 0x71: /* Type F DMA Control Register */
            dev->pci_conf[addr] = val & 0xef;
            break;

        case 0x72: /* SMI Triggered By IRQ Control */
            dev->pci_conf[addr] = val & 0xfa;
            break;
        case 0x73: /* SMI Triggered By IRQ Control */
        case 0x75: /* System Standby Timer Reload, System Standby State Exit And Throttling State Exit */
        case 0x77: /* Monoitor Standby Timer Reload And Monoitor Standby State Exit Control */
            dev->pci_conf[addr] = val;
            break;

        case 0x74: /* System Standby Timer Reload, System Standby State Exit And Throttling State Exit */
        case 0x76: /* Monoitor Standby Timer Reload And Monoitor Standby State Exit Control */
            dev->pci_conf[addr] = val & 0xfb;
            break;
            dev->pci_conf[addr] = val;
            break;

        case 0x80: /* Distributed DMA Master Configuration Register */
        case 0x81: /* Distributed DMA Master Configuration Register */
            dev->pci_conf[addr] = val;
            sis_5595_ddma_recalc(dev);
            break;

        case 0x84: /* Individual Distributed DMA Channel Enable */
            dev->pci_conf[addr] = val;
            sis_5595_ddma_recalc(dev);
            break;

        case 0x88: /* Serial Interrupt Control Register */
        case 0x89: /* Serial Interrupt Enable Register 1 */
        case 0x8a: /* Serial Interrupt Enable Register 2 */
            dev->pci_conf[addr] = val;
            break;

        case 0x90: /* ACPI Base Address Register */
            dev->pci_conf[addr] = val & 0xc0;
            sis_5595_acpi_recalc(dev);
            break;
        case 0x91: /* ACPI Base Address Register */
            dev->pci_conf[addr] = val;
            sis_5595_acpi_recalc(dev);
            break;

        default:
            break;
    }
}

static void
sis_5513_b0_pci_to_isa_write(int addr, uint8_t val, sis_5513_pci_to_isa_t *dev)
{
    uint8_t old;

    switch (addr) {
        case 0x61: /* IDEIRQ Remapping Control Register */
            sis_5513_pci_to_isa_log("Set MIRQ routing: IDEIRQ -> %02X\n", val);
            dev->pci_conf[addr] = val & 0xdf;
            sff_set_mirq(dev->sis->bm[0], (val & 0x10) ? 7 : 2);
            sff_set_mirq(dev->sis->bm[1], (val & 0x10) ? 2 : 7);
            pci_set_mirq_routing(PCI_MIRQ7, 14 + (!!(val & 0x10)));
            if (val & 0x80)
                pci_set_mirq_routing(PCI_MIRQ2, PCI_IRQ_DISABLED);
            else
                pci_set_mirq_routing(PCI_MIRQ2, val & 0xf);
            break;

        case 0x62: /* USBIRQ Remapping Control Register */
            sis_5513_pci_to_isa_log("Set MIRQ routing: USBIRQ -> %02X\n", val);
            dev->pci_conf[addr] = val;
            dev->sis->usb_enabled = !!(val & 0x40);
            if (val & 0x80)
                 pci_set_mirq_routing(PCI_MIRQ3, PCI_IRQ_DISABLED);
            else
                 pci_set_mirq_routing(PCI_MIRQ3, val & 0xf);
            break;

        case 0x63: /* PCI OutputBuffer Current Strength Register */
            dev->pci_conf[addr] = val;
            if ((dev->apc_regs[0x03] & 0x40) && (val & 0x04)) {
                plat_power_off();
                return;
            }
            if ((val & 0x18) == 0x18) {
                dma_reset();
                dma_set_at(1);

                device_reset_all(DEVICE_ALL);

                cpu_alt_reset = 0;

                pci_reset();

                mem_a20_alt = 0;
                mem_a20_recalc();

                flushmmucache();

                resetx86();
            }
            break;

        case 0x64: /* INIT Enable Register */
            dev->pci_conf[addr] = val;
            cpu_cpurst_on_sr = !(val & 0x20);
            break;

        case 0x65: /* PHOLD# Timer */
        case 0x66: /* Priority Timer */
        case 0x67: /* Respond to C/D Segments Register */
        case 0x6b: /* Test Mode Register I */
        case 0x6c: /* Test Mode Register II */
        case 0x71: /* Reserved */
        case 0x72: /* Individual PC/PCI DMA Channel Enable */
        case 0x7c: /* Data Acquisition Module ADC Calibration */
        case 0x7d: /* Data Acquisition Module ADC Calibration */
        case 0x88: /* Serial Interrupt Control Register */
        case 0x89: /* Serial Interrupt Enable Register 1 */
        case 0x8a: /* Serial Interrupt Enable Register 2 */
        case 0x8c: /* Serial Interrupt Enable Register 3 */
            dev->pci_conf[addr] = val;
            break;

        case 0x68: /* Data Acquistion Module Base Address */
            dev->pci_conf[addr] = val & 0xf8;
            sis_5595_dam_recalc(dev);
            break;

        case 0x6a: /* ACPI/SCI IRQ Remapping Control Register */
            sis_5513_pci_to_isa_log("Set MIRQ routing: ACPI/SCI IRQ -> %02X\n", val);
            dev->pci_conf[addr] = val & 0x8f;
            if (val & 0x80)
                 pci_set_mirq_routing(PCI_MIRQ5, PCI_IRQ_DISABLED);
            else
                pci_set_mirq_routing(PCI_MIRQ5, val & 0xf);
            break;

        case 0x6d: /* I2C Bus Control Register */
            dev->pci_conf[addr] = val;
            /* TODO: Keyboard/mouse swapping and keyboard hot key. */
            break;

        case 0x6e: /* Software-Controlled Interrupt Request, Channels 7-0 */
            old = dev->pci_conf[addr];
            picint((val ^ old) & val);
            picintc((val ^ old) & ~val);
            dev->pci_conf[addr] = val;
            break;

        case 0x6f: /* Software-Controlled Interrupt Request, channels 15-8 */
            old = dev->pci_conf[addr];
            picint(((val ^ old) & val) << 8);
            picintc(((val ^ old) & ~val) << 8);
            dev->pci_conf[addr] = val;
            break;

        case 0x70: /* Misc. Controller Register */
            dev->pci_conf[addr] = val;
            /* TODO: Keyboard Lock Enable/Disable. */
            break;

        case 0x73:
        case 0x75 ... 0x79:
            if (dev->rev == 0x81)
                dev->pci_conf[addr] = val;
            break;

        case 0x7a: /* Data Acquisition Module Function Selection Register */
            if (dev->rev == 0x81)
                dev->pci_conf[addr] = val;
            else
                dev->pci_conf[addr] = val & 0x90;
            break;

        case 0x7b: /* Data Acquisition Module Control Register */
            dev->pci_conf[addr] = val;
            sis_5595_dam_recalc(dev);
            break;

        case 0x7e: /* Data Acquisition Module and SMBUS IRQ Remapping Control Register */
            sis_5513_pci_to_isa_log("Set MIRQ routing: DAM/SMBUS IRQ -> %02X\n", val);
            if (dev->rev == 0x81)
                dev->pci_conf[addr] = val & 0x8f;
            else {
                dev->pci_conf[addr] = (dev->pci_conf[addr] & 0x10) | (val & 0xef);

                dev->dam_irq_enable = ((val & 0xc0) == 0x40);
                smbus_sis5595_irq_enable(dev->smbus, (val & 0xa0) == 0x20);
            }
            if (val & 0x80)
                 pci_set_mirq_routing(PCI_MIRQ6, PCI_IRQ_DISABLED);
            else
                pci_set_mirq_routing(PCI_MIRQ6, val & 0xf);
            break;

        case 0x80: /* Distributed DMA Master Configuration Register */
        case 0x81: /* Distributed DMA Master Configuration Register */
            dev->pci_conf[addr] = val;
            sis_5595_ddma_recalc(dev);
            break;

        case 0x84: /* Individual Distributed DMA Channel Enable */
            dev->pci_conf[addr] = val;
            sis_5595_ddma_recalc(dev);
            break;

        case 0x90: /* ACPI Base Address Register */
            dev->pci_conf[addr] = val & 0xc0;
            sis_5595_acpi_recalc(dev);
            break;
        case 0x91: /* ACPI Base Address Register */
            dev->pci_conf[addr] = val;
            sis_5595_acpi_recalc(dev);
            break;

        default:
            break;
    }
}

void
sis_5513_pci_to_isa_write(int addr, uint8_t val, void *priv)
{
    sis_5513_pci_to_isa_t *dev = (sis_5513_pci_to_isa_t *) priv;

    sis_5513_pci_to_isa_log("SiS 5513 P2I: [W] dev->pci_conf[%02X] = %02X\n", addr, val);

    switch (addr) {
        case 0x04: /* Command */
            dev->pci_conf[addr] = val & 0x0f;
            break;

        case 0x07: /* Status */
            dev->pci_conf[addr] = (dev->pci_conf[addr] & 0x06) & ~(val & 0x30);
            break;

        case 0x0d: /* Master Latency Timer */
            if (dev->rev >= 0x11)
                dev->pci_conf[addr] = val;
            break;

        case 0x40: /* BIOS Control Register */
            if (dev->rev >= 0x11) {
                dev->pci_conf[addr] = val;
                sis_5595_acpi_recalc(dev);
            } else
                dev->pci_conf[addr] = val & 0x3f;
            break;

        case 0x41: /* INTA# Remapping Control Register */
        case 0x42: /* INTB# Remapping Control Register */
        case 0x43: /* INTC# Remapping Control Register */
            dev->pci_conf[addr] = val & 0x8f;
            pci_set_irq_routing(addr & 0x07, (val & 0x80) ? PCI_IRQ_DISABLED : (val & 0x0f));
            break;
        case 0x44: /* INTD# Remapping Control Register */
            if (dev->rev == 0x11) {
                dev->pci_conf[addr] = val & 0xcf;
                sis_5513_apc_recalc(dev, val & 0x10);
            } else
                dev->pci_conf[addr] = val & 0x8f;
            pci_set_irq_routing(addr & 0x07, (val & 0x80) ? PCI_IRQ_DISABLED : (val & 0x0f));
            break;

        case 0x45: /* ISA Bus Control Register I */
            if (dev->rev >= 0x01) {
                if (dev->rev == 0x01)
                    dev->pci_conf[addr] = val & 0xec;
                else
                    dev->pci_conf[addr] = val;
                switch (val >> 6) {
                    case 0:
                        cpu_set_isa_speed(7159091);
                        break;
                    case 1:
                        cpu_set_isa_pci_div(4);
                        break;
                    case 2:
                        cpu_set_isa_pci_div(3);
                        break;

                    default:
                        break;
                }
                nvr_bank_set(0, !!(val & 0x08), dev->nvr);
                if (dev->rev == 0xb0)
                    sis_5513_apc_recalc(dev, val & 0x02);
            }
            break;
        case 0x46: /* ISA Bus Control Register II */
            if (dev->rev >= 0x11)
                dev->pci_conf[addr] = val;
            else if (dev->rev == 0x00)
                dev->pci_conf[addr] = val & 0xec;
            break;
        case 0x47: /* DMA Clock and Wait State Control Register */
            switch (dev->rev) {
                case 0x01:
                    dev->pci_conf[addr] = val & 0x3e;
                    break;
                case 0x11:
                    dev->pci_conf[addr] = val & 0x7f;
                    break;
                case 0xb0:
                    dev->pci_conf[addr] = val & 0xfd;
                    break;
            }
            break;

        case 0x48: /* ISA Master/DMA Memory Cycle Control Register 1 */
        case 0x49: /* ISA Master/DMA Memory Cycle Control Register 2 */
        case 0x4a: /* ISA Master/DMA Memory Cycle Control Register 3 */
        case 0x4b: /* ISA Master/DMA Memory Cycle Control Register 4 */
            dev->pci_conf[addr] = val;
            break;

        default:
            switch (dev->rev) {
                case 0x00:
                    sis_5513_00_pci_to_isa_write(addr, val, priv);
                    break;
                case 0x01:
                    sis_5513_01_pci_to_isa_write(addr, val, priv);
                    break;
                case 0x11:
                    sis_5513_11_pci_to_isa_write(addr, val, priv);
                    break;
                case 0x81:
                case 0xb0:
                    sis_5513_b0_pci_to_isa_write(addr, val, priv);
                    break;
            }
            break;
    }
}

uint8_t
sis_5513_pci_to_isa_read(int addr, void *priv)
{
    const sis_5513_pci_to_isa_t *dev = (sis_5513_pci_to_isa_t *) priv;
    uint8_t ret = 0xff;

    switch (addr) {
        default:
            ret = dev->pci_conf[addr];
            break;
        case 0x4c ... 0x4f:
            ret = pic_read_icw(0, addr & 0x03);
            break;
        case 0x50 ... 0x53:
            ret = pic_read_icw(1, addr & 0x03);
            break;
        case 0x54 ... 0x55:
            ret = pic_read_ocw(0, addr & 0x01);
            break;
        case 0x56 ... 0x57:
            ret = pic_read_ocw(1, addr & 0x01);
            break;
        case 0x58 ... 0x5f:
            ret = dev->pit_read_reg(dev->pit, addr & 0x07);
            break;
        case 0x60:
            if (dev->rev >= 0x01)
                ret = inb(0x0070);
            else
                ret = dev->pci_conf[addr];
            break;
    }

    sis_5513_pci_to_isa_log("SiS 5513 P2I: [R] dev->pci_conf[%02X] = %02X\n", addr, ret);

    return ret;
}

static void
sis_5513_isa_write(uint16_t addr, uint8_t val, void *priv)
{
    sis_5513_pci_to_isa_t *dev = (sis_5513_pci_to_isa_t *) priv;

    switch (addr) {
        case 0x22:
            dev->index = val - 0x50;
            break;
        case 0x23:
            sis_5513_pci_to_isa_log("SiS 5513 ISA: [W] dev->regs[%02X] = %02X\n", dev->index + 0x50, val);

            switch (dev->index) {
                case 0x00:
                    dev->regs[dev->index] = val & 0xed;
                    switch (val >> 6) {
                        case 0:
                            cpu_set_isa_speed(7159091);
                            break;
                        case 1:
                            cpu_set_isa_pci_div(4);
                            break;
                        case 2:
                            cpu_set_isa_pci_div(3);
                            break;

                        default:
                            break;
                    }
                    nvr_bank_set(0, !!(val & 0x08), dev->nvr);
                    break;
                case 0x01:
                    dev->regs[dev->index] = val & 0xf4;
                    break;
                case 0x03:
                    dev->regs[dev->index] = val & 3;
                    break;
                case 0x04: /* BIOS Register */
                    dev->regs[dev->index] = val;
                    break;
                case 0x05:
                    dev->regs[dev->index] = val;
                    outb(0x70, val);
                    break;
                case 0x08:
                case 0x09:
                case 0x0a:
                case 0x0b:
                    dev->regs[dev->index] = val;
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }
}

static uint8_t
sis_5513_isa_read(uint16_t addr, void *priv)
{
    const sis_5513_pci_to_isa_t *dev = (sis_5513_pci_to_isa_t *) priv;
    uint8_t ret = 0xff;

    if (addr == 0x23) {
        if (dev->index == 0x05)
            ret = inb(0x70);
        else
            ret = dev->regs[dev->index];

        sis_5513_pci_to_isa_log("SiS 5513 ISA: [R] dev->regs[%02X] = %02X\n", dev->index + 0x50, ret);
    }

    return ret;
}

static void
sis_5513_00_pci_to_isa_reset(sis_5513_pci_to_isa_t *dev)
{
    dev->pci_conf[0x60] = dev->pci_conf[0x61] = 0x80;
    dev->pci_conf[0x62] = 0x00;
    dev->pci_conf[0x63] = 0x80;
    dev->pci_conf[0x64] = 0x00;
    dev->pci_conf[0x65] = 0x00;
    dev->pci_conf[0x66] = dev->pci_conf[0x67] = 0x00;
    dev->pci_conf[0x68] = dev->pci_conf[0x69] = 0x00;
    dev->pci_conf[0x6a] = 0x04;

    pci_set_mirq_routing(PCI_MIRQ0, PCI_IRQ_DISABLED);
    pci_set_mirq_routing(PCI_MIRQ1, PCI_IRQ_DISABLED);

    dev->regs[0x00] = dev->regs[0x01] = 0x00;
    dev->regs[0x03] = dev->regs[0x04] = 0x00;
    dev->regs[0x05] = 0x00;
    dev->regs[0x08] = dev->regs[0x09] = 0x00;
    dev->regs[0x0a] = dev->regs[0x0b] = 0x00;
}

static void
sis_5513_01_pci_to_isa_reset(sis_5513_pci_to_isa_t *dev)
{
    dev->pci_conf[0x45] = dev->pci_conf[0x46] = 0x00;
    dev->pci_conf[0x47] = 0x00;

    dev->pci_conf[0x61] = 0x80;
    dev->pci_conf[0x62] = 0x00;
    dev->pci_conf[0x63] = 0x80;
    dev->pci_conf[0x64] = dev->pci_conf[0x65] = 0x00;
    dev->pci_conf[0x66] = dev->pci_conf[0x67] = 0x00;
    dev->pci_conf[0x68] = 0x80;
    dev->pci_conf[0x69] = dev->pci_conf[0x6a] = 0x00;
    dev->pci_conf[0x6b] = 0x00;
    dev->pci_conf[0x6c] = 0x02;
    dev->pci_conf[0x6d] = 0x00;
    dev->pci_conf[0x6e] = dev->pci_conf[0x6f] = 0x00;
    dev->pci_conf[0x70] = dev->pci_conf[0x71] = 0x00;
    dev->pci_conf[0x72] = dev->pci_conf[0x73] = 0x00;
    dev->pci_conf[0x74] = dev->pci_conf[0x75] = 0x00;
    dev->pci_conf[0x76] = dev->pci_conf[0x77] = 0x00;

    pci_set_mirq_routing(PCI_MIRQ1, PCI_IRQ_DISABLED);
    pci_set_mirq_routing(PCI_MIRQ3, PCI_IRQ_DISABLED);
}

static void
sis_5513_11_pci_to_isa_reset(sis_5513_pci_to_isa_t *dev)
{
    dev->pci_conf[0x45] = dev->pci_conf[0x46] = 0x00;
    dev->pci_conf[0x47] = 0x00;

    dev->pci_conf[0x61] = dev->pci_conf[0x62] = 0x80;
    dev->pci_conf[0x63] = dev->pci_conf[0x64] = 0x00;
    dev->pci_conf[0x65] = dev->pci_conf[0x66] = 0x00;
    dev->pci_conf[0x67] = dev->pci_conf[0x68] = 0x00;
    dev->pci_conf[0x69] = 0x01;
    dev->pci_conf[0x6a] = 0x80;
    dev->pci_conf[0x6b] = 0x00;
    dev->pci_conf[0x6c] = 0x20;
    dev->pci_conf[0x6d] = 0x19;
    dev->pci_conf[0x6e] = dev->pci_conf[0x6f] = 0x00;
    dev->pci_conf[0x70] = 0x12;
    dev->pci_conf[0x71] = dev->pci_conf[0x72] = 0x00;
    dev->pci_conf[0x73] = dev->pci_conf[0x74] = 0x00;
    dev->pci_conf[0x75] = dev->pci_conf[0x76] = 0x00;
    dev->pci_conf[0x77] = 0x00;
    dev->pci_conf[0x80] = dev->pci_conf[0x81] = 0x00;
    dev->pci_conf[0x84] = dev->pci_conf[0x88] = 0x00;
    dev->pci_conf[0x89] = dev->pci_conf[0x8a] = 0x00;
    dev->pci_conf[0x90] = dev->pci_conf[0x91] = 0x00;

    pci_set_mirq_routing(PCI_MIRQ3, PCI_IRQ_DISABLED);
    pci_set_mirq_routing(PCI_MIRQ5, PCI_IRQ_DISABLED);

    picint_common(0xffff, 0, 0, NULL);

    sis_5595_ddma_recalc(dev);
    sis_5595_acpi_recalc(dev);

    dev->sis->ide_bits_1_3_writable = 0;
    dev->sis->usb_enabled = 0;

    sis_5513_apc_recalc(dev, 0);
}

static void
sis_5513_b0_pci_to_isa_reset(sis_5513_pci_to_isa_t *dev)
{
    dev->pci_conf[0x45] = dev->pci_conf[0x46] = 0x00;
    dev->pci_conf[0x47] = 0x00;

    dev->pci_conf[0x61] = dev->pci_conf[0x62] = 0x80;
    dev->pci_conf[0x63] = dev->pci_conf[0x64] = 0x00;
    dev->pci_conf[0x65] = 0x01;
    dev->pci_conf[0x66] = dev->pci_conf[0x67] = 0x00;
    dev->pci_conf[0x68] = 0x90;
    dev->pci_conf[0x69] = 0x02;
    dev->pci_conf[0x6a] = 0x80;
    dev->pci_conf[0x6b] = 0x00;
    dev->pci_conf[0x6c] = 0x20;
    dev->pci_conf[0x6d] = 0x19;
    dev->pci_conf[0x6e] = dev->pci_conf[0x6f] = 0x00;
    dev->pci_conf[0x70] = 0x12;
    dev->pci_conf[0x71] = dev->pci_conf[0x72] = 0x00;
    dev->pci_conf[0x7a] = dev->pci_conf[0x7b] = 0x00;
    dev->pci_conf[0x7c] = dev->pci_conf[0x7d] = 0x00;
    dev->pci_conf[0x7e] = 0x80;
    dev->pci_conf[0x80] = dev->pci_conf[0x81] = 0x00;
    dev->pci_conf[0x84] = dev->pci_conf[0x88] = 0x00;
    dev->pci_conf[0x89] = dev->pci_conf[0x8a] = 0x00;
    dev->pci_conf[0x8b] = dev->pci_conf[0x8c] = 0x00;
    dev->pci_conf[0x90] = dev->pci_conf[0x91] = 0x00;

    pci_set_mirq_routing(PCI_MIRQ3, PCI_IRQ_DISABLED);
    pci_set_mirq_routing(PCI_MIRQ5, PCI_IRQ_DISABLED);
    pci_set_mirq_routing(PCI_MIRQ6, PCI_IRQ_DISABLED);
    pci_set_mirq_routing(PCI_MIRQ7, 15);

    sff_set_mirq(dev->sis->bm[0], 2);
    sff_set_mirq(dev->sis->bm[1], 7);

    picint_common(0xffff, 0, 0, NULL);

    sis_5595_ddma_recalc(dev);
    sis_5595_acpi_recalc(dev);

    /* SiS 5595 Data Acquisition Module */
    sis_5595_dam_recalc(dev);
    sis_5595_dam_reset(dev);

    dev->dam_irq_enable = 0;

    cpu_cpurst_on_sr = 1;

    dev->sis->usb_enabled = 0;

    sis_5513_apc_recalc(dev, 0);

    if (dev->rev == 0x81)
        dev->dam_irq_enable = 1;
}

static void
sis_5513_pci_to_isa_reset(void *priv)
{
    sis_5513_pci_to_isa_t *dev = (sis_5513_pci_to_isa_t *) priv;

    dev->pci_conf[0x00] = 0x39;
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x08;
    dev->pci_conf[0x03] = 0x00;
    dev->pci_conf[0x04] = 0x07;
    dev->pci_conf[0x05] = dev->pci_conf[0x06] = 0x00;
    dev->pci_conf[0x07] = 0x02;
    if ((dev->rev == 0x11) || (dev->rev == 0x81))
        dev->pci_conf[0x08] = 0x01;
    else
        dev->pci_conf[0x08] = dev->rev;
    dev->pci_conf[0x09] = 0x00;
    dev->pci_conf[0x0a] = 0x01;
    dev->pci_conf[0x0b] = 0x06;
    dev->pci_conf[0x0e] = 0x80;
    dev->pci_conf[0x40] = 0x00;
    dev->pci_conf[0x41] = dev->pci_conf[0x42] = 0x80;
    dev->pci_conf[0x43] = dev->pci_conf[0x44] = 0x80;
    dev->pci_conf[0x48] = dev->pci_conf[0x49] = 0x00;
    dev->pci_conf[0x4a] = dev->pci_conf[0x4b] = 0x00;

    switch (dev->rev) {
        case 0x00:
            sis_5513_00_pci_to_isa_reset(dev);
            break;
        case 0x01:
            sis_5513_01_pci_to_isa_reset(dev);
            break;
        case 0x11:
            sis_5513_11_pci_to_isa_reset(dev);
            break;
        case 0x81:
        case 0xb0:
            sis_5513_b0_pci_to_isa_reset(dev);
            break;
    }

    pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
    pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);

    pci_set_mirq_routing(PCI_MIRQ2, PCI_IRQ_DISABLED);

    cpu_set_isa_speed(7159091);
    nvr_bank_set(0, 0, dev->nvr);
}

static void
sis_5513_pci_to_isa_close(void *priv)
{
    sis_5513_pci_to_isa_t *dev = (sis_5513_pci_to_isa_t *) priv;
    FILE      *fp  = NULL;

    if (dev->fn != NULL)
        fp = nvr_fopen(dev->fn, "wb");

    if (fp != NULL) {
        (void) fwrite(dev->apc_regs, 256, 1, fp);
        fclose(fp);
    }

    if (dev->fn != NULL)
        free(dev->fn);

    free(dev);
}

static void *
sis_5513_pci_to_isa_init(UNUSED(const device_t *info))
{
    sis_5513_pci_to_isa_t *dev = (sis_5513_pci_to_isa_t *) calloc(1, sizeof(sis_5513_pci_to_isa_t));
    uint8_t pit_is_fast = (((pit_mode == -1) && is486) || (pit_mode == 1));
    FILE      *fp = NULL;
    int        c;

    dev->rev = info->local;

    dev->sis = device_get_common_priv();

    /* IDEIRQ */
    pci_enable_mirq(2);

    /* Port 92h */
    dev->port_92 = device_add(&port_92_device);

    /* PIT */
    dev->pit = device_find_first_priv(DEVICE_PIT);
    dev->pit_read_reg = pit_is_fast ? pitf_read_reg : pit_read_reg;

    /* NVR */
    dev->nvr = device_add(&at_mb_nvr_device);

    switch (dev->rev) {
        case 0x00:
            /* MIRQ */
            pci_enable_mirq(0);
            pci_enable_mirq(1);

            /* Ports 22h-23h: SiS 5513 ISA */
            io_sethandler(0x0022, 0x0002,
                          sis_5513_isa_read, NULL, NULL, sis_5513_isa_write, NULL, NULL, dev);
            break;
        case 0x01:
            /* MIRQ */
            pci_enable_mirq(1);
            break;
        case 0x11:
        case 0x81:
        case 0xb0:
            /* USBIRQ */
            pci_enable_mirq(3);

            /* ACPI/SCI IRQ */
            pci_enable_mirq(5);
  
            if (dev->rev == 0xb0) {
                /* Data Acquisition Module and SMBUS IRQ */
                pci_enable_mirq(6);

                /* Non-remapped native IDE IRQ */
                pci_enable_mirq(7);
            }
  
            dev->ddma = device_add(&ddma_device);

            switch (dev->rev) {
                case 0x11:
                    dev->sis->acpi   = device_add(&acpi_sis_5582_device);
                    break;
                case 0x81:
                    dev->sis->acpi   = device_add(&acpi_sis_5595_1997_device);
                    break;
                case 0xb0:
                    dev->sis->acpi   = device_add(&acpi_sis_5595_device);
                    dev->smbus       = acpi_get_smbus(dev->sis->acpi);
                    break;
            }

            dev->sis->acpi->priv = dev->sis;
            acpi_set_slot(dev->sis->acpi, dev->sis->sb_pci_slot);
            acpi_set_nvr(dev->sis->acpi, dev->nvr);

            /* Set up the NVR file's name. */
            c       = strlen(machine_get_nvr_name()) + 9;
            dev->fn = (char *) malloc(c + 1);
            sprintf(dev->fn, "%s_apc.nvr", machine_get_nvr_name());

            fp = nvr_fopen(dev->fn, "rb");

            memset(dev->apc_regs, 0x00, sizeof(dev->apc_regs));
            sis_5513_apc_reset(dev);
            if (fp != NULL) {
                if (fread(dev->apc_regs, 1, 256, fp) != 256)
                    fatal("sis_5513_pci_to_isa_init(): Error reading APC data\n");
                fclose(fp);
            }

            acpi_set_irq_mode(dev->sis->acpi, 2);
            break;
    }

    sis_5513_pci_to_isa_reset(dev);

    return dev;
}

const device_t sis_5513_p2i_device = {
    .name          = "SiS 5513 PCI to ISA bridge",
    .internal_name = "sis_5513_pci_to_isa",
    .flags         = DEVICE_PCI,
    .local         = 0x00,
    .init          = sis_5513_pci_to_isa_init,
    .close         = sis_5513_pci_to_isa_close,
    .reset         = sis_5513_pci_to_isa_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sis_5572_p2i_device = {
    .name          = "SiS 5572 PCI to ISA bridge",
    .internal_name = "sis_5572_pci_to_isa",
    .flags         = DEVICE_PCI,
    .local         = 0x01,
    .init          = sis_5513_pci_to_isa_init,
    .close         = sis_5513_pci_to_isa_close,
    .reset         = sis_5513_pci_to_isa_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sis_5582_p2i_device = {
    .name          = "SiS 5582 PCI to ISA bridge",
    .internal_name = "sis_5582_pci_to_isa",
    .flags         = DEVICE_PCI,
    .local         = 0x11,    /* Actually, 0x01, but we need to somehow distinguish it
                                 from SiS 5572 and SiS 5595 1997, which are also revision 0x01. */
    .init          = sis_5513_pci_to_isa_init,
    .close         = sis_5513_pci_to_isa_close,
    .reset         = sis_5513_pci_to_isa_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sis_5595_1997_p2i_device = {
    .name          = "SiS 5595 (1997) PCI to ISA bridge",
    .internal_name = "sis_5595_1997_pci_to_isa",
    .flags         = DEVICE_PCI,
    .local         = 0x81,    /* Actually, 0x01, but we need to somehow distinguish it
                                 from SiS 5572 and SiS 5582, which are also revision 0x01. */
    .init          = sis_5513_pci_to_isa_init,
    .close         = sis_5513_pci_to_isa_close,
    .reset         = sis_5513_pci_to_isa_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sis_5595_p2i_device = {
    .name          = "SiS 5595 PCI to ISA bridge",
    .internal_name = "sis_5595_pci_to_isa",
    .flags         = DEVICE_PCI,
    .local         = 0xb0,
    .init          = sis_5513_pci_to_isa_init,
    .close         = sis_5513_pci_to_isa_close,
    .reset         = sis_5513_pci_to_isa_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
