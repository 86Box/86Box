#include <86box/apic.h>

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
            break;
        case 4:
            nmi_raise();
            break;
    }
    if (lapic->irq_queue_num == 2) {
        /* Queue is full. Reject interrupt. */
        apic_lapic_ioapic_remote_eoi(lapic, interrupt.intvec);
        return;
    }

    lapic->irq_queue[lapic->irq_queue_num++].vectorconf = interrupt;
}

void
lapic_close(void* priv)
{
    apic_t *dev = (apic_t *)priv;
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