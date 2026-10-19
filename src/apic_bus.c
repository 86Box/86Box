/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Copyright 2026 skiretic.
 */
#include "apic_bus.h"
#include "lapic.h"

apic_bus_result_t
apic_bus_result(apic_bus_status_t status)
{
    apic_bus_result_t result = {
        .status     = status,
        .source_esr = 0,
        .target_esr = 0,
        .accepted   = status == APIC_BUS_ACCEPTED
    };

    /*
     * Default failures are send-side; receive-side helpers override this when
     * a target detects the fault after routing.
     */
    switch (status) {
        case APIC_BUS_NO_TARGET:
        case APIC_BUS_REJECTED:
            result.source_esr = LAPIC_ESR_SEND_ACCEPT;
            break;

        case APIC_BUS_INVALID_VECTOR:
            result.source_esr = LAPIC_ESR_SEND_ILLEGAL_VECTOR;
            break;

        case APIC_BUS_INVALID_REGISTER:
            result.source_esr = LAPIC_ESR_ILLEGAL_REG_ADDR;
            break;

        default:
            break;
    }

    return result;
}

int
apic_bus_result_accepted(apic_bus_result_t result)
{
    return result.accepted && (result.status == APIC_BUS_ACCEPTED);
}

apic_bus_result_t
apic_bus_result_invalid_send_vector(void)
{
    return apic_bus_result(APIC_BUS_INVALID_VECTOR);
}

apic_bus_result_t
apic_bus_result_invalid_recv_vector(void)
{
    apic_bus_result_t result = apic_bus_result(APIC_BUS_INVALID_VECTOR);

    /* Vector decode reached a target, so only the receiving LAPIC owns ESR. */
    result.source_esr = 0;
    result.target_esr = LAPIC_ESR_RECV_ILLEGAL_VECTOR;
    return result;
}

apic_bus_result_t
apic_bus_result_invalid_register(void)
{
    return apic_bus_result(APIC_BUS_INVALID_REGISTER);
}

apic_bus_result_t
apic_bus_result_invalid_mode(void)
{
    return apic_bus_result(APIC_BUS_INVALID_MODE);
}

apic_bus_result_t
apic_bus_result_no_target(void)
{
    return apic_bus_result(APIC_BUS_NO_TARGET);
}

apic_bus_result_t
apic_bus_result_rejected(void)
{
    return apic_bus_result(APIC_BUS_REJECTED);
}

apic_bus_result_t
apic_bus_result_retry(void)
{
    return apic_bus_result(APIC_BUS_RETRY);
}

apic_bus_result_t
apic_bus_result_rejected_recv_accept(void)
{
    apic_bus_result_t result = apic_bus_result_rejected();

    /* Accept rejection after target selection is a receive-side ESR event. */
    result.target_esr = LAPIC_ESR_RECV_ACCEPT;
    return result;
}
