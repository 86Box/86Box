/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          Serial modem flow-control state.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdbool.h>
#include <stdint.h>
#include <86box/modem/modem_flow.h>

bool
modem_flow_set_mode(modem_flow_t *flow, int mode)
{
    if (mode != MODEM_FLOW_NONE && mode != MODEM_FLOW_HARDWARE && mode != MODEM_FLOW_SOFTWARE)
        return false;
    if (flow->mode != mode) {
        flow->mode = mode;
        flow->output_paused = false;
        flow->input_stopped = false;
    }
    return true;
}

void
modem_flow_update(modem_flow_t *flow, bool active, uint32_t queued)
{
    if (!active || flow->mode == MODEM_FLOW_NONE)
        flow->input_stopped = false;
    else if (flow->input_stopped) {
        if (queued <= MODEM_FLOW_LOW_WATER)
            flow->input_stopped = false;
    } else if (queued >= MODEM_FLOW_HIGH_WATER)
        flow->input_stopped = true;
}

bool
modem_flow_receive(modem_flow_t *flow, uint8_t byte)
{
    if (flow->mode != MODEM_FLOW_SOFTWARE)
        return false;
    if (byte == MODEM_FLOW_XOFF)
        flow->output_paused = true;
    else if (byte == MODEM_FLOW_XON)
        flow->output_paused = false;
    else
        return false;
    return true;
}

bool
modem_flow_can_send(const modem_flow_t *flow, bool rts)
{
    return !(flow->mode == MODEM_FLOW_HARDWARE && !rts)
           && !(flow->mode == MODEM_FLOW_SOFTWARE && flow->output_paused);
}

bool
modem_flow_cts(const modem_flow_t *flow)
{
    return flow->mode != MODEM_FLOW_HARDWARE || !flow->input_stopped;
}

int
modem_flow_control_pending(const modem_flow_t *flow)
{
    const bool stop = flow->mode == MODEM_FLOW_SOFTWARE && flow->input_stopped;
    return stop == flow->xoff_sent ? -1 : stop ? MODEM_FLOW_XOFF : MODEM_FLOW_XON;
}

void
modem_flow_control_sent(modem_flow_t *flow, int byte)
{
    if (byte == MODEM_FLOW_XOFF)
        flow->xoff_sent = true;
    else if (byte == MODEM_FLOW_XON)
        flow->xoff_sent = false;
}

void
modem_flow_disconnect(modem_flow_t *flow)
{
    flow->output_paused = false;
    flow->input_stopped = false;
}
