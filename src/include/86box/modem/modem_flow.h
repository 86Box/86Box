/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          Serial modem flow-control state.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#ifndef EMU_MODEM_FLOW_H
#define EMU_MODEM_FLOW_H

#define MODEM_FLOW_XON  0x11
#define MODEM_FLOW_XOFF 0x13
#define MODEM_FLOW_HIGH_WATER 0xc000
#define MODEM_FLOW_LOW_WATER  0x8000

enum {
    MODEM_FLOW_NONE = 0,
    MODEM_FLOW_HARDWARE = 3,
    MODEM_FLOW_SOFTWARE = 4
};

typedef struct modem_flow_t {
    int mode;
    bool output_paused;
    bool input_stopped;
    bool xoff_sent;
} modem_flow_t;

bool modem_flow_set_mode(modem_flow_t *flow, int mode);
void modem_flow_update(modem_flow_t *flow, bool active, uint32_t queued);
bool modem_flow_receive(modem_flow_t *flow, uint8_t byte);
bool modem_flow_can_send(const modem_flow_t *flow, bool rts);
bool modem_flow_cts(const modem_flow_t *flow);
int modem_flow_control_pending(const modem_flow_t *flow);
void modem_flow_control_sent(modem_flow_t *flow, int byte);
void modem_flow_disconnect(modem_flow_t *flow);

#endif
