#include <86box/apic.h>

/* Only one processor is emulated */
apic_t* current_apic = NULL;

void apic_ioapic_set_base(apic_t* ioapic, uint8_t x_base, uint8_t y_base)
{
    mem_mapping_set_addr(&ioapic->ioapic_mem_window, 0xFEC00000 | ((y_base & 0x3) << 8) | ((x_base & 0x3C) << 14), 0x20);
}

void
ioapic_i82093aa_reset(apic_t* ioapic)
{
    int i = 0;

    for (i = 0; i < 256; i++) {
        ioapic->ioapic_regs[i] = 0;
    }
    for (i = 0; i < IOAPIC_RED_TABL_SIZE; i++) {
        ioapic->regs.ioredtabl_s[i].intr_mask = 1;
    }
}

void
apic_ioapic_lapic_interrupt(apic_t* ioapic, uint8_t irq)
{
    if (irq == 0) irq = 2;

    /* TODO: Actually service the interrupt */
}

void
apic_lapic_ioapic_remote_eoi(apic_t* ioapic, uint8_t vector)
{
    int i = 0;

    for (i = 0; i < IOAPIC_RED_TABL_SIZE; i++) {
        if (ioapic->regs.ioredtabl_s[i].intvec == vector && ioapic->regs.ioredtabl_s[i].rirr) {
            ioapic->regs.ioredtabl_s[i].rirr = 0;
            apic_ioapic_lapic_interrupt(ioapic, i);
        }
    }
}

uint32_t
ioapic_i82093aa_readl(uint32_t addr, void *priv)
{
    apic_t *dev = (apic_t *)priv;
    addr = (addr >> 2) & 0xFF;
    uint32_t ret = (uint32_t)-1;
    
    switch (addr) {
        case 0:
            ret = dev->regs.ioapicd;
            break;
        case 1:
            ret = 0x170011;
            break;
        case 2:
            ret = dev->regs.ioapicarb;
            break;
        case 0x10 ... 0x3F:
            ret = dev->regs.ioredtabl_l[addr - 0x10];
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
    addr = (addr >> 2) & 0xFF;

    switch (addr) {
        case 0:
            dev->regs.ioapicd = val & 0xFF;
            break;
        case 0x10 ... 0x3F: {
                /* TODO: Handle triggering interrupts. */
                uint8_t orig_rirr   = dev->regs.ioredtabl_s[addr - 0x10].rirr;
                uint8_t orig_delivs = dev->regs.ioredtabl_s[addr - 0x10].delivs;
                dev->regs.ioredtabl_l[addr - 0x10] = val;
                dev->regs.ioredtabl_s[addr - 0x10].reserved = 0;
                dev->regs.ioredtabl_s[addr - 0x10].rirr = orig_rirr;
                dev->regs.ioredtabl_s[addr - 0x10].delivs = orig_delivs;
                break;
            }
    }
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
    ioapic_i82093aa_reset(dev);
    return dev;
}

void
ioapic_i82093aa_close(void *priv)
{
    apic_t *dev = (apic_t *)priv;
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
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};