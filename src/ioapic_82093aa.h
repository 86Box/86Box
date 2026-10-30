/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Copyright 2026 skiretic.
 */
#ifndef EMU_IOAPIC_82093AA_H
#define EMU_IOAPIC_82093AA_H

#include <stdint.h>

/* 82093AA model constants and bit policy. src/ioapic.c owns mutable chip state. */
#define IOAPIC_82093AA_REDIR_ENTRIES 24
#define IOAPIC_82093AA_SMI_PIN       23
#define IOAPIC_82093AA_VERSION       0x00000011
#define IOAPIC_82093AA_VERSION_REG \
    (((IOAPIC_82093AA_REDIR_ENTRIES - 1) << 16) | IOAPIC_82093AA_VERSION)
#define IOAPIC_82093AA_PIN_MASK      ((1u << IOAPIC_82093AA_REDIR_ENTRIES) - 1u)

#define IOAPIC_82093AA_REDIR_VECTOR    0x000000ff
#define IOAPIC_82093AA_REDIR_DELIV     0x00000700
#define IOAPIC_82093AA_REDIR_DESTMOD   0x00000800
#define IOAPIC_82093AA_REDIR_STATUS    0x00001000
#define IOAPIC_82093AA_REDIR_POLARITY  0x00002000
#define IOAPIC_82093AA_REDIR_REMOTEIRR 0x00004000
#define IOAPIC_82093AA_REDIR_TRIGGER   0x00008000
#define IOAPIC_82093AA_REDIR_MASK      0x00010000
/* Guest writes use masks; helper code separately preserves model-owned bits. */
#define IOAPIC_82093AA_REDIR_LOW_WRITE_MASK \
    (IOAPIC_82093AA_REDIR_VECTOR | IOAPIC_82093AA_REDIR_DELIV | \
     IOAPIC_82093AA_REDIR_DESTMOD | IOAPIC_82093AA_REDIR_POLARITY | \
     IOAPIC_82093AA_REDIR_TRIGGER | IOAPIC_82093AA_REDIR_MASK)
#define IOAPIC_82093AA_REDIR_HIGH_WRITE_MASK 0xff000000
#define IOAPIC_82093AA_REDIR_LOW_RESET       IOAPIC_82093AA_REDIR_MASK
#define IOAPIC_82093AA_REDIR_HIGH_RESET      0x00000000

#define IOAPIC_82093AA_DELIVERY_FIXED  0x00000000
#define IOAPIC_82093AA_DELIVERY_LOWEST 0x00000100
#define IOAPIC_82093AA_DELIVERY_SMI    0x00000200
#define IOAPIC_82093AA_DELIVERY_NMI    0x00000400
#define IOAPIC_82093AA_DELIVERY_INIT   0x00000500
#define IOAPIC_82093AA_DELIVERY_EXTINT 0x00000700

extern int      ioapic_82093aa_redir_level(uint32_t low);
extern int      ioapic_82093aa_redir_remote_irr_valid(uint32_t low);
extern int      ioapic_82093aa_redir_supported(uint32_t low);
extern int      ioapic_82093aa_redir_active_low(uint32_t low);
extern int      ioapic_82093aa_redir_smi(uint32_t low);
extern int      ioapic_82093aa_redir_extint(uint32_t low);
extern int      ioapic_82093aa_redir_physical_asserted(uint32_t low, int physical_high);
extern uint32_t ioapic_82093aa_redir_read_only_low_bits(uint32_t old_low, uint32_t new_low);
extern uint32_t ioapic_82093aa_redir_sanitize_low(uint32_t low);
extern uint32_t ioapic_82093aa_pin_bit(uint8_t pin);
extern uint32_t ioapic_82093aa_derive_raw_lines_from_logical(const uint32_t *redir,
                                                             uint32_t pin_lines);
extern const char *ioapic_82093aa_redir_delivery_name(uint32_t low);

#endif /* EMU_IOAPIC_82093AA_H */
