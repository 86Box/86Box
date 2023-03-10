#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>

#include "86box.h"
#include "../../cpu/cpu.h"
#include "timer.h"
#include "device.h"
#include "mem.h"

#define IOAPIC_RED_TABL_SIZE 24

/* We only target little-endian architectures. */
typedef struct apic_ioredtable_t {
    uint32_t intvec    : 8;
    uint32_t delmod    : 3;
    uint32_t destmod   : 1;
    uint32_t delivs    : 1;
    uint32_t intpol    : 1;
    uint32_t rirr      : 1;
    uint32_t trigmode  : 1;
    uint32_t intr_mask : 1;
    uint64_t reserved  : 45;
    uint32_t dest_mask : 3;
} apic_ioredtable_t;

typedef struct apic_lapic_lvt_t
{
    uint32_t intvec    : 8;
    uint32_t delmod    : 3;
    uint32_t dummy     : 1;
    uint32_t delivs    : 1;
    uint32_t intpol    : 1;
    uint32_t rirr      : 1;
    uint32_t trigmode  : 1;
} apic_lapic_icr_t;

typedef struct apic_t
{
    /* I/O APIC parts */
    union {
        uint32_t ioapic_regs[256];
        struct {
            uint32_t ioapicd;
            uint32_t ioapicver;
            uint32_t ioapicarb;
            union {
                uint64_t ioredtabl[IOAPIC_RED_TABL_SIZE];
                apic_ioredtable_t ioredtabl_s[IOAPIC_RED_TABL_SIZE];
                uint32_t ioredtabl_l[IOAPIC_RED_TABL_SIZE * 2];
            };
        };
    };
    uint8_t ioapic_index;
    mem_mapping_t ioapic_mem_window;
    uint32_t irq_value;
    uint32_t irr;

    /* Local APIC parts. */
    pc_timer_t apic_timer;
    union {
        uint64_t isr_ll[4];
        uint32_t isr_l[8];
        uint8_t isr_b[8 * sizeof(uint32_t)];
    };
    union {
        uint64_t irr_ll[4];
        uint32_t irr_l[8];
        uint8_t irr_b[8 * sizeof(uint32_t)];
    };
    union {
        uint64_t tmr_ll[4];
        uint32_t tmr_l[8];
        uint8_t tmr_b[8 * sizeof(uint32_t)];
    };
    union {
        uint64_t icr;
        struct {
            uint32_t icr0;
            uint32_t icr1;
        };
    };
    uint32_t lapic_id;
    uint32_t lapic_spurious_interrupt;
    uint32_t lapic_tpr;
    uint8_t irq_queue_num;
    struct
    {
        uint8_t vector;
        apic_ioredtable_t vectorconf;
    } irq_queue[2];
    mem_mapping_t lapic_mem_window;
    
    /* Common parts. */
    uint32_t lines; /* For level triggered interrupts. */
    uint32_t ref_count; /* Structure reference count. */
} apic_t;

/* IOREDTABL masks */
#define IOAPIC_INTVEC_MASK    0xFF
#define IOAPIC_DELMOD_MASK    0x700
#define IOAPIC_DESTMOD_MASK   0x800
#define IOAPIC_DELIVS_MASK    0x1000
#define IOAPIC_INTPOL_MASK    0x2000
#define IOAPIC_RIRR_MASK      0x4000
#define IOAPIC_TRIGMODE_MASK  0x8000
#define IOAPIC_INTERRUPT_MASK 0x10000
#define IOAPIC_DEST_MASK      0xE000000000000000

extern const device_t i82093aa_ioapic_device;

/* Only one processor is emulated. */
extern apic_t* current_apic;

extern void apic_ioapic_set_base(uint8_t x_base, uint8_t y_base);
extern void apic_lapic_set_base(uint32_t base);
extern void apic_ioapic_lapic_interrupt_check(apic_t* ioapic, uint8_t irq);
extern void apic_ioapic_set_irq(apic_t* ioapic, uint8_t irq);
extern void apic_ioapic_clear_irq(apic_t* ioapic, uint8_t irq);
extern void apic_lapic_ioapic_remote_eoi(apic_t* ioapic, uint8_t vector);
extern void lapic_service_interrupt(apic_t *lapic, apic_ioredtable_t interrupt);
extern void apic_lapic_picinterrupt();