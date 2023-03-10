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
    lapic->lapic_id = lapic->lapic_arb = 0;
    lapic->tmr_ll[0] = lapic->tmr_ll[1] = lapic->tmr_ll[2] = lapic->tmr_ll[3] = 
    lapic->irr_ll[0] = lapic->irr_ll[1] = lapic->irr_ll[2] = lapic->irr_ll[3] = 
    lapic->isr_ll[0] = lapic->isr_ll[1] = lapic->isr_ll[2] = lapic->isr_ll[3] = 0;

    lapic->lapic_timer_divider = lapic->lapic_timer_initial_count = lapic->lapic_timer_current_count = 0;
    lapic->lapic_timer_shift   = 1;
    lapic->lapic_tpr           = 0;
    lapic->icr                 = 0;
    lapic->lapic_id            = 0;

    lapic->lapic_lvt_timer_val   =
    lapic->lapic_lvt_perf_val    =
    lapic->lapic_lvt_lvt0_val    =
    lapic->lapic_lvt_lvt1_val    =
    lapic->lapic_lvt_thermal_val = 1 << 16;

    lapic->lapic_spurious_interrupt = 0xFF;
    lapic->lapic_dest_format        = -1;
    lapic->lapic_local_dest         = 0;
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

        case 0x320:
            dev->lapic_lvt_timer_val = val;
            break;

        case 0x330:
            dev->lapic_lvt_thermal_val = val;
            break;

        case 0x340:
            dev->lapic_lvt_perf_val = val;
            break;

        case 0x350:
            dev->lapic_lvt_lvt0_val = val;
            break;

        case 0x360:
            dev->lapic_lvt_lvt1_val = val;
            break;

        case 0x370:
            dev->lapic_lvt_error_val = val;
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

        case 0x320:
            return dev->lapic_lvt_timer_val;

        case 0x330:
            return dev->lapic_lvt_thermal_val;

        case 0x340:
            return dev->lapic_lvt_perf_val;

        case 0x350:
            return dev->lapic_lvt_lvt0_val;

        case 0x360:
            return dev->lapic_lvt_lvt1_val;

        case 0x370:
            return dev->lapic_lvt_error_val;
    }
}

void apic_lapic_set_base(uint32_t base)
{
    if (!current_apic)
        return;

    mem_mapping_set_addr(&current_apic->lapic_mem_window, base & 0xFFFFF000, 0x100000);
}

uint8_t
apic_lapic_is_irr_pending(void)
{
    if (!current_apic)
        return 0;

    if (current_apic->irr_ll[0] || current_apic->irr_ll[1] || current_apic->irr_ll[2] || current_apic->irr_ll[3]) {
        uint8_t highest_irr = lapic_get_highest_bit(current_apic, lapic_get_bit_irr);
        uint8_t highest_isr = lapic_get_highest_bit(current_apic, lapic_get_bit_isr);
        uint8_t tpr         = current_apic->lapic_tpr;

        if (highest_isr >= highest_irr)
            return 0;

        if ((highest_irr & 0xF0) < tpr)
            return 0;
        
        return 1;
    }
    
    return 0;
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

uint8_t
apic_lapic_picinterrupt(void)
{
    apic_t *lapic = current_apic;
    uint8_t highest_irr = lapic_get_highest_bit(current_apic, lapic_get_bit_irr);
    uint8_t highest_isr = lapic_get_highest_bit(current_apic, lapic_get_bit_isr);
    uint8_t tpr         = current_apic->lapic_tpr;
    
    if (highest_isr >= highest_irr) {
        return lapic->lapic_spurious_interrupt & 0xFF;
    }

    if ((highest_irr & 0xF0) <= (tpr & 0xF0)) {
        return lapic->lapic_spurious_interrupt & 0xFF;
    }

    lapic_set_bit_irr(lapic, highest_irr, 0);
    lapic_set_bit_isr(lapic, highest_isr, 1);
    return highest_irr;
}

void
lapic_service_interrupt(apic_t *lapic, apic_ioredtable_t interrupt)
{
    if (!(lapic->lapic_spurious_interrupt & 0x100)) {
        /* All interrupts are presumed masked. */
        apic_lapic_ioapic_remote_eoi(lapic, interrupt.intvec);
        return;
    }
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