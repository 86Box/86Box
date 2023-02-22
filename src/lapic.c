#include <86box/apic.h>

void*
lapic_init(const device_t* info)
{
    apic_t *dev = NULL;
    
    if (current_apic) {
        current_apic->ref_count++;
        return current_apic;
    } else {
        dev = (apic_t *) calloc(sizeof(apic_t), 1);
        current_apic = dev;
    }
    return dev;
}

void
lapic_service_interrupt(apic_t *lapic, apic_ioredtable_t interrupt)
{

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