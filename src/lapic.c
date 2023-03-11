/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Local APIC emulation.
 *
 *
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2023 Cacodemon345.
 */
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
#include <86box/pic.h> /*ExtINT*/

/* Needed to set the default Local APIC base. */
#include "cpu/cpu.h"

#define INITIAL_LAPIC_ADDRESS 0xFEE00000

extern double bus_timing;

static __inline uint8_t
lapic_get_bit_irr(apic_t *lapic, uint8_t bit)
{
    return lapic->irr_ll[bit / 64] & (1ull << (bit & 63));
}

static __inline void
lapic_set_bit_irr(apic_t *lapic, uint8_t bit, uint8_t val)
{
    lapic->irr_ll[bit / 64] &= ~(1ull << (bit & 63));
    lapic->irr_ll[bit / 64] |= (((uint64_t)!!val) << (bit & 63));
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
    lapic->isr_ll[bit / 64] |= (((uint64_t)!!val) << (bit & 63));
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
    lapic->tmr_ll[bit / 64] |= (((uint64_t)!!val) << (bit & 63));
}

static __inline uint8_t
lapic_get_highest_bit(apic_t *lapic, uint8_t (*get_bit)(apic_t*, uint8_t)) {
    uint8_t highest_bit = 0xFF;
    for (uint32_t bit = 0; bit < 256; bit++) {
        if (get_bit(lapic, bit)) {
            highest_bit = bit;
        }
    }
    return highest_bit;
}

void
lapic_reset(apic_t *lapic)
{
    /* Always set lapic_id and lapic_arb to 0, regardless of soft/hard resets. */
    lapic->lapic_id = lapic->lapic_arb = 0;
    lapic->tmr_ll[0] = lapic->tmr_ll[1] = lapic->tmr_ll[2] = lapic->tmr_ll[3] = 
    lapic->irr_ll[0] = lapic->irr_ll[1] = lapic->irr_ll[2] = lapic->irr_ll[3] = 
    lapic->isr_ll[0] = lapic->isr_ll[1] = lapic->isr_ll[2] = lapic->isr_ll[3] = 0;

    lapic->lapic_timer_divider = lapic->lapic_timer_initial_count = lapic->lapic_timer_current_count = 0;
    lapic->lapic_tpr           = 0;
    lapic->icr                 = 0;
    lapic->lapic_id            = 0;

    lapic->lapic_lvt_timer_val   =
    lapic->lapic_lvt_perf_val    =
    lapic->lapic_lvt_lvt0_val    =
    lapic->lapic_lvt_lvt1_val    =
    lapic->lapic_lvt_thermal_val = 1 << 16;

    lapic->lapic_lvt_error_val = 0;
    lapic->lapic_lvt_read_error_val = 0;

    lapic->lapic_spurious_interrupt = 0xFF;
    lapic->lapic_dest_format        = -1;
    lapic->lapic_local_dest         = 0;

    timer_on_auto(&lapic->apic_timer, (1000000. / bus_timing) * (1 << ((lapic->lapic_timer_divider & 3) | ((lapic->lapic_timer_divider & 0x8) >> 1)) + 1));
    pclog("LAPIC: RESET!\n");
}

void
apic_lapic_writel(uint32_t addr, uint32_t val, void *priv)
{
    apic_t *dev = (apic_t *)priv;

    pclog("APIC write: 0x%X, 0x%X\n", addr, val);

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

        case 0xD0:
            dev->lapic_local_dest = val & 0xFF000000;
            break;

        case 0xE0:
            dev->lapic_dest_format = val | 0xFFFFFF;
            break;
        
        case 0xF0:
            dev->lapic_spurious_interrupt = val;
            break;

        case 0x280:
            dev->lapic_lvt_read_error_val = dev->lapic_lvt_error_val;
            dev->lapic_lvt_error_val = 0;
            break;

        case 0x300:
            dev->icr0 = val;
            {
                apic_ioredtable_t deliverstruct = { 0 };
                deliverstruct.intvec = dev->icr & 0xFF;
                deliverstruct.delmod = (dev->icr & 0x700) >> 8;
                deliverstruct.trigmode = 0;
                switch(deliverstruct.delmod) {
                    case 5:
                        return; /* INIT Level Deassert not needed to be implemented. */
                    case 6:
                        break;
                }
                switch ((dev->icr0 >> 18) & 3) {
                    case 0:
                        break;
                    case 1:
                    case 2:
                        if (deliverstruct.delmod == 6) {
                            loadcs((deliverstruct.intvec & 0xFF) << 8);
                            cpu_state.oldpc = cpu_state.pc;
                            cpu_state.pc = 0;
                            pclog("SIPI jump\n");
                        }
                        else
                            lapic_service_interrupt(dev, deliverstruct);
                        break;
                }
                dev->icr0 &= ~(1 << 12);
            }
            break;

        case 0x310:
            dev->icr1 = val;
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

        case 0x380:
            dev->lapic_timer_initial_count = dev->lapic_timer_current_count = val;
            break;

        case 0x3E0:
            dev->lapic_timer_divider = val;
            break;
    }
}

uint32_t
apic_lapic_readl(uint32_t addr, void *priv)
{
    apic_t *dev = (apic_t *)priv;

    pclog("APIC read: 0x%X\n", addr);
    switch(addr & 0x3FF) {
        case 0x20:
            return dev->lapic_id;

        case 0x30:
            return 0x40012;

        case 0x80:
            return dev->lapic_tpr;

        case 0xD0:
            return dev->lapic_local_dest;

        case 0xE0:
            return dev->lapic_dest_format;

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

        case 0x280:
            return dev->lapic_lvt_read_error_val;

        case 0x300:
            return dev->icr0;

        case 0x310:
            return dev->icr1;

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

        case 0x380:
            return dev->lapic_timer_initial_count;
        
        case 0x390:
            return dev->lapic_timer_current_count;

        case 0x3E0:
            return dev->lapic_timer_divider;
    }
}

void apic_lapic_set_base(uint32_t base)
{
    if (!current_apic)
        return;

    mem_mapping_set_addr(&current_apic->lapic_mem_window, base & 0xFFFFF000, 0x100000);
    if (base & (1 << 11)) {
        mem_mapping_disable(&current_apic->lapic_mem_window);
        current_apic->lapic_spurious_interrupt &= ~0x100;
    }
}

void
lapic_timer_callback(void *priv)
{
    apic_t *dev = (apic_t *)priv;

    if (dev->lapic_timer_current_count) {
        dev->lapic_timer_current_count--;
        if (dev->lapic_timer_current_count == 0) {
            lapic_service_interrupt(dev, dev->lapic_lvt_timer);
            if (dev->lapic_lvt_timer.timer_mode == 1) {
                dev->lapic_timer_current_count = dev->lapic_timer_initial_count;
            }
        }
    }
    
    if ((dev->lapic_timer_divider & 0xF) == 0xB)
        timer_on_auto(&dev->apic_timer, (1000000. / bus_timing));
    else
        timer_on_auto(&dev->apic_timer, (1000000. / bus_timing) * (1 << ((dev->lapic_timer_divider & 3) | ((dev->lapic_timer_divider & 0x8) >> 1)) + 1));
}

uint8_t
apic_lapic_is_irr_pending(void)
{
    if (!current_apic)
        return 0;

    if (current_apic->lapic_extint_servicing)
        return 1;

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

    msr.apic_base = INITIAL_LAPIC_ADDRESS | (1 << 11) | (1 << 8);
    mem_mapping_add(&dev->lapic_mem_window, INITIAL_LAPIC_ADDRESS, 0x100000, NULL, NULL, apic_lapic_readl, NULL, NULL, apic_lapic_writel, NULL, MEM_MAPPING_EXTERNAL, dev);
    timer_add(&dev->apic_timer, lapic_timer_callback, dev, 0);
    lapic_reset(dev);
    return dev;
}

uint8_t
apic_lapic_picinterrupt(void)
{
    apic_t *lapic = current_apic;
    uint8_t highest_irr = lapic_get_highest_bit(current_apic, lapic_get_bit_irr);
    uint8_t highest_isr = lapic_get_highest_bit(current_apic, lapic_get_bit_isr);
    uint8_t tpr         = current_apic->lapic_tpr;

    if (lapic->lapic_extint_servicing) {
        uint8_t ret = lapic->lapic_extint_servicing;
        lapic->lapic_extint_servicing = 0;
        pclog("LAPIC: Service EXTINT INTVEC 0x%02X\n", highest_irr);
        return ret;
    }
    
    if (highest_isr >= highest_irr) {
        return lapic->lapic_spurious_interrupt & 0xFF;
    }

    if ((highest_irr & 0xF0) <= (tpr & 0xF0)) {
        return lapic->lapic_spurious_interrupt & 0xFF;
    }

    lapic_set_bit_irr(lapic, highest_irr, 0);
    lapic_set_bit_isr(lapic, highest_isr, 1);

    pclog("LAPIC: Service INTVEC 0x%02X\n", highest_irr);
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
            apic_lapic_ioapic_remote_eoi(lapic, interrupt.intvec);
            return;
        case 4:
            nmi_raise();
            apic_lapic_ioapic_remote_eoi(lapic, interrupt.intvec);
            return;
        case 5: /*INIT*/
            {
                apic_lapic_ioapic_remote_eoi(lapic, interrupt.intvec);
                softresetx86();
                cpu_set_edx();
                flushmmucache();
                lapic_reset(lapic);
                return;
            }
        case 7: /*ExtINT*/
            {
                uint8_t extvector = 0xFF;
                /* ExtINT interrupts are to be delivered directly. */
                extvector = picinterrupt();
                if (extvector != -1) {
                    lapic->lapic_extint_servicing = extvector;
                } else
                    lapic->lapic_extint_servicing = 0;
                return;
            }
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

void
lapic_speed_changed(void* priv)
{
    apic_t* dev = (apic_t*)priv;

    if ((dev->lapic_timer_divider & 0xF) == 0xB)
        timer_on_auto(&dev->apic_timer, (1000000. / bus_timing));
    else
        timer_on_auto(&dev->apic_timer, (1000000. / bus_timing) * (1 << ((dev->lapic_timer_divider & 3) | ((dev->lapic_timer_divider & 0x8) >> 1)) + 1));
}

const device_t lapic_device = {
    .name          = "Local Advanced Programmable Interrupt Controller",
    .internal_name = "lapic",
    .flags         = DEVICE_AT,
    .local         = 0,
    .init          = lapic_init,
    .close         = lapic_close,
    .reset         = (void (*)(void*))lapic_reset,
    { .available = NULL },
    .speed_changed = lapic_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};