#include <86box/apic.h>

/* Needed to set the default Local APIC base. */
#include "cpu/cpu.h"

#define INITIAL_LAPIC_ADDRESS 0xFEE00000

static __inline uint8_t
lapic_get_bit_irr(apic_t *lapic, uint8_t bit)
{
    return lapic->irr_ll[bit / 64] & (1ull << (bit & 63));
}

static __inline void
lapic_set_bit_irr(apic_t *lapic, uint8_t bit, uint8_t val)
{
    lapic->irr_ll[bit / 64] &= ~(1ull << (bit & 63));
    lapic->irr_ll[bit / 64] |= (((uint64_t)val) << (bit & 63));
}

static __inline uint8_t
lapic_get_bit_isr(apic_t *lapic, uint8_t bit)
{
    return lapic->isr_ll[bit / 64] & (1ull << (bit & 63));
}

static __inline void
lapic_set_bit_isr(apic_t *lapic, uint8_t bit, uint8_t val)
{
    lapic->isr_ll[bit / 64] &= ~(1ull << (bit & 63));
    lapic->isr_ll[bit / 64] |= (((uint64_t)val) << (bit & 63));
}

static __inline uint8_t
lapic_get_bit_tmr(apic_t *lapic, uint8_t bit)
{
    return lapic->tmr_ll[bit / 64] & (1ull << (bit & 63));
}

static __inline void
lapic_set_bit_tmr(apic_t *lapic, uint8_t bit, uint8_t val)
{
    lapic->tmr_ll[bit / 64] &= ~(1ull << (bit & 63));
    lapic->tmr_ll[bit / 64] |= (((uint64_t)val) << (bit & 63));
}

static __inline uint8_t
lapic_get_highest_bit(apic_t *lapic, uint8_t (*get_bit)(apic_t*, uint8_t)) {
    uint8_t highest_bit = 0xFF;
    for (uint8_t bit = 0; bit <= 255; bit++) {
        if (get_bit(lapic, bit)) {
            highest_bit = bit;
        }
    }
    return highest_bit;
}

void
lapic_reset(apic_t *lapic)
{
    lapic->lapic_id = 0;
    lapic->tmr_ll[0] = lapic->tmr_ll[1] = lapic->tmr_ll[2] = lapic->tmr_ll[3] = 
    lapic->irr_ll[0] = lapic->irr_ll[1] = lapic->irr_ll[2] = lapic->irr_ll[3] = 
    lapic->isr_ll[0] = lapic->isr_ll[1] = lapic->isr_ll[2] = lapic->isr_ll[3] = 0;
}

void
apic_lapic_writel(uint32_t addr, uint32_t val, void *priv)
{
    apic_t *dev = (apic_t *)priv;

    switch(addr & 0x3FF) {
        case 0x20:
            dev->lapic_id = val;
            break;

        case 0x80:
            dev->lapic_tpr = val & 0xFF;
            break;

        case 0xB0:
            uint8_t bit = lapic_get_highest_bit(dev, lapic_get_bit_isr);
            if (bit != -1) {
                lapic_set_bit_isr(dev, bit, 0);
                if (lapic_get_bit_tmr(dev, bit)) {
                    apic_lapic_ioapic_remote_eoi(dev, bit);
                }
            }
            break;
        
        case 0xF0:
            dev->lapic_spurious_interrupt = val;
            break;
    }
}

uint32_t
apic_lapic_readl(uint32_t addr, void *priv)
{
    apic_t *dev = (apic_t *)priv;

    switch(addr & 0x3FF) {
        case 0x20:
            return dev->lapic_id;

        case 0x30:
            return 0x50014;

        case 0x80:
            return dev->lapic_tpr;

        case 0xF0:
            return dev->lapic_spurious_interrupt;

        case 0x100:
        case 0x110:
        case 0x120:
        case 0x130:
        case 0x140:
        case 0x150:
        case 0x160:
        case 0x170:
            return dev->isr_l[(addr - 0x100) >> 4];

        case 0x180:
        case 0x190:
        case 0x1A0:
        case 0x1B0:
        case 0x1C0:
        case 0x1D0:
        case 0x1E0:
        case 0x1F0:
            return dev->tmr_l[(addr - 0x180) >> 4];

        case 0x200:
        case 0x210:
        case 0x220:
        case 0x230:
        case 0x240:
        case 0x250:
        case 0x260:
        case 0x270:
            return dev->irr_l[(addr - 0x180) >> 4];
    }
}

void apic_lapic_set_base(uint32_t base)
{
    if (!current_apic)
        return;

    mem_mapping_set_addr(&current_apic->lapic_mem_window, base & 0xFFFFF000, 0x100000);
}

void*
lapic_init(const device_t* info)
{
    apic_t *dev = NULL;
    
    if (current_apic) {
        current_apic->ref_count++;
        dev = current_apic;
    } else {
        dev = (apic_t *) calloc(sizeof(apic_t), 1);
        current_apic = dev;
    }

    msr.apic_base = INITIAL_LAPIC_ADDRESS;
    mem_mapping_add(&dev->lapic_mem_window, INITIAL_LAPIC_ADDRESS, 0x100000, NULL, NULL, apic_lapic_readl, NULL, NULL, apic_lapic_writel, NULL, MEM_MAPPING_EXTERNAL, dev);
    return dev;
}

void
lapic_deliver_interrupt_to_cpu(apic_t *lapic)
{

}

void
apic_lapic_picinterrupt()
{

}

void
lapic_service_interrupt(apic_t *lapic, apic_ioredtable_t interrupt)
{
    switch (interrupt.delmod) {
        case 2:
            smi_raise();
            return;
        case 4:
            nmi_raise();
            return;
    }
    
    lapic_set_bit_irr(lapic, interrupt.intvec, 1);
    lapic_set_bit_tmr(lapic, interrupt.intvec, !!interrupt.trigmode);
}

void
lapic_close(void* priv)
{
    apic_t *dev = (apic_t *)priv;
    mem_mapping_disable(&dev->lapic_mem_window);
    if ((--dev->ref_count) == 0) {
        current_apic = NULL;
        free(priv);
    }
}

const device_t lapic_device = {
    .name          = "Local Advanced Programmable Interrupt Controller",
    .internal_name = "lapic",
    .flags         = DEVICE_AT,
    .local         = 0,
    .init          = lapic_init,
    .close         = lapic_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};