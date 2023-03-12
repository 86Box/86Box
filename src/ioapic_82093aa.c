/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          82093AA I/O APIC emulation.
 *
 *
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2023 Cacodemon345.
 */

/* Code based on https://github.com/copy/v86/blob/master/src/ioapic.js */
#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>

#include <86box/86box.h>
#include "cpu/cpu.h"
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/mem.h>

#include <86box/apic.h>

/* Only one processor is emulated */
apic_t* current_apic = NULL;

void apic_ioapic_set_base(uint8_t x_base, uint8_t y_base)
{
    if (!current_apic)
        return;

    mem_mapping_set_addr(&current_apic->ioapic_mem_window, 0xFEC00000 | ((y_base & 0x3) << 8) | ((x_base & 0xF) << 16), 0x20);

    pclog("I/O APIC base: 0x%08X\n", current_apic->ioapic_mem_window.base);
}

void
ioapic_i82093aa_reset(apic_t* ioapic)
{
    int i = 0;

    for (i = 0; i < 256; i++) {
        ioapic->ioapic_regs[i] = 0;
    }
    for (i = 0; i < IOAPIC_RED_TABL_SIZE; i++) {
        ioapic->ioredtabl_s[i].intr_mask = 1;
    }
    pclog("IOAPIC: RESET!\n");
}

void
apic_ioapic_lapic_interrupt_check(apic_t* ioapic, uint8_t irq)
{
    uint32_t mask = 1 << irq;
    apic_ioredtable_t service_parameters;

    if (irq >= 24)
        return;

    service_parameters = ioapic->ioredtabl_s[irq];

    if (!(ioapic->irr & mask))
        return;

    if (ioapic->ioredtabl_s[irq].intr_mask)
        return;
    
    if (service_parameters.trigmode == 0) {
        ioapic->irr &= ~mask;
    } else {
        ioapic->ioredtabl_s[irq].rirr = 1;
        if (service_parameters.rirr == 1) {
            return;
        }
    }

    lapic_service_interrupt(ioapic, service_parameters);
}

void
apic_ioapic_set_irq(apic_t* ioapic, uint8_t irq)
{
    uint32_t mask = 1 << irq;

    if ((ioapic->irq_value & mask) == 0) {
        ioapic->irq_value |= mask;
        if ((ioapic->ioredtabl[irq] & (IOAPIC_TRIGMODE_MASK | IOAPIC_INTERRUPT_MASK)) == (IOAPIC_INTERRUPT_MASK)) {
            return;
        }
        ioapic->irr |= mask;
        apic_ioapic_lapic_interrupt_check(ioapic, irq);
    }
}

void
apic_ioapic_clear_irq(apic_t* ioapic, uint8_t irq)
{
    uint32_t mask = 1 << irq;

    if ((ioapic->irq_value & mask) == mask) {
        ioapic->irq_value &= ~mask;
        if (!ioapic->ioredtabl_s[irq].trigmode)
            ioapic->irr &= ~mask;
    }
}

void
apic_lapic_ioapic_remote_eoi(apic_t* ioapic, uint8_t vector)
{
    int i = 0;

    for (i = 0; i < IOAPIC_RED_TABL_SIZE; i++) {
        if (ioapic->ioredtabl_s[i].intvec == vector && ioapic->ioredtabl_s[i].rirr) {
            ioapic->ioredtabl_s[i].rirr = 0;
            apic_ioapic_lapic_interrupt_check(ioapic, i);
        }
    }
}

uint32_t
ioapic_i82093aa_readl(uint32_t addr, void *priv)
{
    apic_t *dev = (apic_t *)priv;
    uint32_t ret = (uint32_t)-1;

    if ((addr - dev->ioapic_mem_window.base) >= 0x40)
        return -1;
    addr = (addr >> 2) & 0xFF;
    switch (addr) {
        case 0:
            ret = dev->ioapicd;
            break;
        case 1:
            ret = 0x170011;
            break;
        case 2:
            ret = dev->ioapicarb;
            break;
        case 0x10 ... 0x3F:
            ret = dev->ioredtabl_l[addr - 0x10];
            break;
        default:
            break;
    }
    return ret;
}

void
ioapic_i82093aa_writel(uint32_t addr, uint32_t val, void *priv)
{
    apic_t *dev = (apic_t *)priv;

    if ((addr - dev->ioapic_mem_window.base) >= 0x40)
        return;

    addr = (addr >> 2) & 0xFF;
    switch (addr) {
        case 0:
            dev->ioapicd = val & 0xFF;
            break;
        case 0x10 ... 0x3F: {
                uint8_t orig_rirr   = dev->ioredtabl_s[addr - 0x10].rirr;
                uint8_t orig_delivs = dev->ioredtabl_s[addr - 0x10].delivs;
                dev->ioredtabl_l[addr - 0x10] = val;
                dev->ioredtabl_s[addr - 0x10].reserved = 0;
                dev->ioredtabl_s[addr - 0x10].rirr = orig_rirr;
                dev->ioredtabl_s[addr - 0x10].delivs = orig_delivs;
                apic_ioapic_lapic_interrupt_check(dev, addr - 0x10);
                break;
            }
    }
}

void
ioapic_i82093aa_write(uint32_t addr, uint8_t val, void *priv)
{
    uint32_t mask = 0xFFFFFFFF & ~(0xFF << (8 * (addr & 3)));

    return ioapic_i82093aa_writel(addr, (ioapic_i82093aa_readl(addr, priv) & mask) | ((val << (8 * (addr & 3)))), priv);
}

uint8_t
ioapic_i82093aa_read(uint32_t addr, void *priv)
{
    return (ioapic_i82093aa_readl(addr, priv) >> (8 * (addr & 3))) & 0xFF;
}

void ioapic_i82093aa_writew(uint32_t addr, uint16_t val, void *priv)
{
    ioapic_i82093aa_write(addr, val & 0xFF, priv);
    ioapic_i82093aa_write(addr + 1, (val >> 8) & 0xFF, priv);
}

uint16_t
ioapic_i82093aa_readw(uint32_t addr, void *priv)
{
    return ioapic_i82093aa_read(addr, priv) | (ioapic_i82093aa_read(addr + 1, priv) << 8);
}

void*
ioapic_i82093aa_init(const device_t* info)
{
    apic_t *dev = NULL;
    
    if (current_apic) {
        current_apic->ref_count++;
        dev = current_apic;
    } else {
        dev = (apic_t *) calloc(sizeof(apic_t), 1);
        current_apic = dev;
    }
    mem_mapping_add(&dev->ioapic_mem_window, 0xFEC00000, 0x20, ioapic_i82093aa_read, ioapic_i82093aa_readw, ioapic_i82093aa_readl, ioapic_i82093aa_write, ioapic_i82093aa_writew, ioapic_i82093aa_writel, NULL, MEM_MAPPING_EXTERNAL, dev);
    ioapic_i82093aa_reset(dev);
    return dev;
}

void
ioapic_i82093aa_close(void *priv)
{
    apic_t *dev = (apic_t *)priv;
    mem_mapping_disable(&dev->ioapic_mem_window);
    if ((--dev->ref_count) == 0) {
        current_apic = NULL;
        free(priv);
    }
}

const device_t i82093aa_ioapic_device = {
    .name          = "Intel 82093AA I/O Advanced Programmable Interrupt Controller",
    .internal_name = "ioapic_82093aa",
    .flags         = DEVICE_AT,
    .local         = 0,
    .init          = ioapic_i82093aa_init,
    .close         = ioapic_i82093aa_close,
    .reset         = (void (*)(void*))ioapic_i82093aa_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};