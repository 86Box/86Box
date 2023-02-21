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
                /* We only target little-endian architectures. */
                struct {
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
                } ioredtabl_s[IOAPIC_RED_TABL_SIZE];
                uint32_t ioredtabl_l[IOAPIC_RED_TABL_SIZE * 2];
            };
        } regs;
    };
    uint8_t ioapic_index;
    mem_mapping_t ioapic_mem_window;

    /* Local APIC parts. */
    pc_timer_t apic_timer;

    /* Common parts. */
    uint32_t irr;
    uint32_t isr;
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

extern void apic_ioapic_set_base(apic_t* ioapic, uint8_t x_base, uint8_t y_base);
extern void apic_ioapic_lapic_interrupt(apic_t* ioapic, uint8_t irq);