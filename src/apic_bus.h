/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Copyright 2026 skiretic.
 */
#ifndef EMU_APIC_BUS_H
#define EMU_APIC_BUS_H

#include <stdint.h>

typedef enum apic_bus_agent_t {
    APIC_BUS_AGENT_LAPIC = 0,
    APIC_BUS_AGENT_IOAPIC
} apic_bus_agent_t;

typedef enum apic_bus_delivery_t {
    APIC_BUS_DELIVERY_FIXED       = 0x00000000,
    APIC_BUS_DELIVERY_LOWEST      = 0x00000100,
    APIC_BUS_DELIVERY_SMI         = 0x00000200,
    APIC_BUS_DELIVERY_REMOTE_READ = 0x00000300,
    APIC_BUS_DELIVERY_NMI         = 0x00000400,
    APIC_BUS_DELIVERY_INIT        = 0x00000500,
    APIC_BUS_DELIVERY_SIPI        = 0x00000600,
    APIC_BUS_DELIVERY_EXTINT      = 0x00000700
} apic_bus_delivery_t;

typedef enum apic_bus_status_t {
    APIC_BUS_ACCEPTED = 0,
    APIC_BUS_NO_TARGET,
    APIC_BUS_REJECTED,
    APIC_BUS_INVALID_VECTOR,
    APIC_BUS_INVALID_REGISTER,
    APIC_BUS_INVALID_MODE,
    /* A target's interrupt slots for the class are full: the message is
       not accepted by anyone and the sender sends it again. */
    APIC_BUS_RETRY
} apic_bus_status_t;

/*
 * Internal APIC bus packet. Register decoding stays with the LAPIC/IOAPIC
 * endpoint; this struct carries the already-decoded delivery request.
 */
typedef struct apic_bus_message_t {
    apic_bus_agent_t    source;
    apic_bus_delivery_t delivery;
    uint8_t             vector;
    uint8_t             dest;
    uint8_t             dest_mode;
    uint8_t             level;
    uint8_t             trigger;
    uint8_t             shorthand;
} apic_bus_message_t;

/*
 * ESR ownership is split so routers can charge send-side faults to the source
 * LAPIC and receive-side faults to the target LAPIC that detected them.
 */
typedef struct apic_bus_result_t {
    apic_bus_status_t status;
    uint32_t          source_esr;
    uint32_t          target_esr;
    uint8_t           accepted;
} apic_bus_result_t;

extern apic_bus_result_t apic_bus_result(apic_bus_status_t status);
extern int               apic_bus_result_accepted(apic_bus_result_t result);
extern apic_bus_result_t apic_bus_result_invalid_send_vector(void);
extern apic_bus_result_t apic_bus_result_invalid_recv_vector(void);
extern apic_bus_result_t apic_bus_result_invalid_register(void);
extern apic_bus_result_t apic_bus_result_invalid_mode(void);
extern apic_bus_result_t apic_bus_result_no_target(void);
extern apic_bus_result_t apic_bus_result_rejected(void);
extern apic_bus_result_t apic_bus_result_retry(void);
extern apic_bus_result_t apic_bus_result_rejected_recv_accept(void);

#endif /* EMU_APIC_BUS_H */
