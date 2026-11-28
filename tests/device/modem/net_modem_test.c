/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          Network modem DCD line tests.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include "../../../src/network/net_modem.c"
#include "modem_reset_test_stubs.h"
#include "modem_command_test_stubs.h"
#include "modem_config_test.h"
#include "modem_at_test.h"

static int dcd = -1;
static int cts = -1;
static double timer_period;
static int can_receive;
static int rts;
static uint8_t received;
static int receive_count;
static char log_buffer[65536];
static size_t log_length;

uint32_t
plat_get_ticks(void)
{
    return 1234;
}

void
log_out(UNUSED(void *priv), const char *fmt, va_list args)
{
    int written = vsnprintf(log_buffer + log_length, sizeof(log_buffer) - log_length, fmt, args);
    if (written > 0) {
        size_t remaining = sizeof(log_buffer) - log_length;
        log_length += (size_t) written < remaining ? (size_t) written : remaining - 1;
    }
}

void
timer_on_auto(pc_timer_t *timer, double period)
{
    timer->flags |= TIMER_ENABLED;
    timer_period = period;
}

void
timer_disable(pc_timer_t *timer)
{
    timer->flags &= ~TIMER_ENABLED;
}

void
timer_stop(pc_timer_t *timer)
{
    timer->flags &= ~TIMER_ENABLED;
}

void
serial_port_set_line(UNUSED(ser_port_t *port), uint32_t line, int on)
{
    if (line == CHAR_COM_DCD)
        dcd = on;
    else if (line == CHAR_COM_CTS)
        cts = on;
}

int
serial_port_can_receive(UNUSED(ser_port_t *port))
{
    return can_receive;
}

int
serial_port_get_line(UNUSED(ser_port_t *port), UNUSED(uint32_t line))
{
    return rts;
}

void
serial_port_receive(UNUSED(ser_port_t *port), uint8_t value)
{
    received = value;
    receive_count++;
}

static int
test_pnp_profiles_output(void)
{
    modem_t modem = { 0 };
    ser_port_t port = { 0 };

    modem.serial = &port;
    modem.dte_baudrate = 1200;
    fifo8_create(&modem.data_pending, 256);
    fifo8_create(&modem.rx_data, 256);
    can_receive = 1;
    for (size_t i = 0; i < sizeof(test_pnp_profiles) / sizeof(test_pnp_profiles[0]); i++) {
        modem_pnp_init(&modem.pnp, test_pnp_profiles[i].value);
        modem_pnp_serial_format(&modem, 1201, 7, 0, 1);
        modem_pnp_serial_control(&modem, 0);
        for (int tick = 0; tick < 200; tick++)
            modem_pnp_tick(&modem.pnp, true);
        modem_pnp_serial_control(&modem, CHAR_COM_DTR);
        if (!modem_pnp_pending(&modem.pnp)
            || !strstr((const char *) modem.pnp.response, test_pnp_profiles[i].id))
            return 1;
        const unsigned length = modem.pnp.response_length;
        for (unsigned j = 0; j < length; j++) {
            const int count = receive_count;
            host_to_modem_cb(&modem);
            if (receive_count != count + 1 || received != modem.pnp.response[j])
                return 1;
        }
        if (modem_pnp_pending(&modem.pnp))
            return 1;
    }
    fifo8_destroy(&modem.data_pending);
    fifo8_destroy(&modem.rx_data);
    can_receive = 0;
    receive_count = 0;
    log_length = 0;
    log_buffer[0] = '\0';
    return 0;
}

/* Profile writes go to a temporary file so that a successful save updates the
   stored profile, as it would on a real machine. */
FILE *
nvr_fopen(char *str, char *mode)
{
    (void) str;
    (void) mode;
    return tmpfile();
}

/* Commands change volatile settings only; they must not be written to the profile,
   and ATZ must return echo and result codes to their defaults. */
static int
test_volatile_settings_not_saved_by_commands(void)
{
    modem_t modem = { 0 };

    modem.clientsocket = modem.serversocket = modem.waitingclientsocket = -1;
    fifo8_create(&modem.data_pending, 256);
    fifo8_create(&modem.rx_data, 256);
    modem_reset(&modem);
    modem_profile_init(&modem.profile, modem_profile_state(&modem), "test", "net", 0);

    for (const char *p = "ATE0Q1\r"; *p; p++)
        modem_write(NULL, &modem, (uint8_t) *p);
    /* saved[7] is the stored echo setting. */
    if (modem.echo || modem.doresponse != 1 || modem.profile.saved[7] != 1)
        return 1;

    for (const char *p = "ATZ\r"; *p; p++)
        modem_write(NULL, &modem, (uint8_t) *p);
    if (!modem.echo || modem.doresponse != 0)
        return 1;

    fifo8_destroy(&modem.data_pending);
    fifo8_destroy(&modem.rx_data);
    return 0;
}

int
main(void)
{
    modem_t modem = { 0 };
    ser_port_t port = { 0 };

    if (test_modem_config())
        return 1;
    if (test_pnp_profiles_output())
        return 1;
    if (test_volatile_settings_not_saved_by_commands())
        return 1;
    modem.serial = &port;
    serial_t serial = { 0 };
    serial.transmit_period = 1000000.0 / 9615.0;
    modem.baudrate = 115200;
    modem_update_dte_baudrate(&modem, &serial);
    if (modem.dte_baudrate != 9615 || timer_period < 935.9 || timer_period > 936.2)
        return 1;
    modem.baudrate = 9600;
    modem_update_dte_baudrate(&modem, &serial);
    if (modem.dte_baudrate != 9600 || timer_period != 937.5)
        return 1;
    modem.dcdmode = 0;
    modem_update_dcd(&modem);
    if (dcd != 1)
        return 1;
    modem.dcdmode = 1;
    modem_update_dcd(&modem);
    if (dcd != 0)
        return 1;
    modem.connected = true;
    modem_update_dcd(&modem);
    if (dcd != 1)
        return 1;
    modem.connected = false;
    modem.mode = MODEM_MODE_COMMAND;
    modem.reg[MREG_DTR_DELAY] = 5;
    modem_dtr_callback(NULL, 0, &modem);
    if (timer_period != 50000.0)
        return 1;
    modem_dtr_callback(NULL, 1, &modem);
    if (timer_is_enabled(&modem.dtr_timer))
        return 1;
    modem.reg[MREG_DTR_DELAY] = 0;
    modem_dtr_callback(NULL, 0, &modem);
    if (timer_period != 1.0)
        return 1;
    modem_dtr_callback(NULL, 1, &modem);
    modem.reg[MREG_DTR_DELAY] = 255;
    modem_dtr_callback(NULL, 0, &modem);
    if (timer_period != 2550000.0)
        return 1;
    modem_dtr_callback(NULL, 1, &modem);
    modem.dte_baudrate = 1200;
    modem.flowcontrol = 3;
    modem_update_flow(&modem);
    fifo8_create(&modem.data_pending, 256);
    fifo8_create(&modem.rx_data, 256);
    modem_pnp_init(&modem.pnp, MODEM_PNP_GENERIC);
    modem_pnp_serial_format(&modem, 1201, 7, 0, 1);
    if (!strstr(log_buffer, "SERIAL PNP format t=1234 ms profile=1 baud=1201 data_bits=7 parity=0 stop_bits=1"))
        return 1;
    modem.connected = true;
    modem_pnp_serial_control(&modem, CHAR_COM_DTR | CHAR_COM_RTS);
    if (!strstr(log_buffer, "dtr=0->1 rts=0->1 format_ok=1 eligible=0"))
        return 1;
    modem.connected = false;
    modem_pnp_serial_control(&modem, CHAR_COM_DTR);
    modem_pnp_serial_control(&modem, 0);
    for (int i = 0; i < 200; i++)
        modem_pnp_tick(&modem.pnp, true);
    modem_pnp_serial_control(&modem, CHAR_COM_DTR);
    if (!modem_pnp_pending(&modem.pnp) || timer_period != 7500.0
        || !strstr(log_buffer, "dtr=0->1 rts=0->0 format_ok=1 eligible=1 low_ms=200"))
        return 1;
    host_to_modem_cb(&modem);
    if (receive_count || !modem_pnp_pending(&modem.pnp))
        return 1;
    can_receive = 1;
    host_to_modem_cb(&modem);
    if (receive_count != 1 || received != '('
        || !strstr(log_buffer, "SERIAL RX (modem->guest) t=1234 ms mode=0 byte=28 source=pnp"))
        return 1;
    modem.connected = true;
    host_to_modem_cb(&modem);
    if (receive_count != 1 || modem_pnp_pending(&modem.pnp)
        || !strstr(log_buffer, "reason=output-ineligible connected=1"))
        return 1;
    modem.connected = false;
    rts = 1;
    fifo8_push(&modem.data_pending, 0x4f);
    host_to_modem_cb(&modem);
    if (receive_count != 2 || received != 0x4f
        || !strstr(log_buffer, "SERIAL RX (modem->guest) t=1234 ms mode=0 byte=4F source=output"))
        return 1;
    rts = 0;
    modem.profile.saved[0] = 0;
    modem.profile.saved[1] = 2;
    modem.profile.saved[3] = 2;
    modem.profile.saved[4] = 3;
    modem.profile.saved[9 + MREG_DTR_DELAY] = 5;
    modem.clientsocket = modem.serversocket = modem.waitingclientsocket = -1;
    modem_reset_profile(&modem);
    if (modem.dcdmode != 0 || dcd != 1 || modem.speaker_mode != 2
        || modem.speaker_level != 3 || test_speaker_mode != 2 || test_speaker_level != 3
        || modem.reg[MREG_RING_COUNT] != 0
        || modem.reg[MREG_DTR_DELAY] != 5)
        return 1;
    modem.connected = true;
    modem.dtrmode = 3;
    modem.dcdmode = 1;
    modem_dtr_callback_timer(&modem);
    if (modem.connected || modem.dcdmode != 0 || dcd != 1 || modem.speaker_mode != 2)
        return 1;
    fifo8_reset(&modem.data_pending);
    modem.connected = true;
    modem.tcpIpMode = true;
    modem.mode = MODEM_MODE_DATA;
    modem.flowcontrol = MODEM_FLOW_HARDWARE;
    modem.tx_count = MODEM_FLOW_HIGH_WATER;
    modem_update_flow(&modem);
    if (cts != 0)
        return 1;
    modem.tx_count = MODEM_FLOW_LOW_WATER;
    modem_update_flow(&modem);
    if (cts != 1)
        return 1;
    modem.flowcontrol = MODEM_FLOW_SOFTWARE;
    modem.tx_count = MODEM_FLOW_HIGH_WATER;
    modem_update_flow(&modem);
    modem_flow_receive(&modem.flow, MODEM_FLOW_XOFF);
    receive_count = 0;
    host_to_modem_cb(&modem);
    if (receive_count != 1 || received != MODEM_FLOW_XOFF
        || !strstr(log_buffer, "byte=13 source=flow"))
        return 1;
    host_to_modem_cb(&modem);
    if (receive_count != 1)
        return 1;
    modem.tx_count = MODEM_FLOW_LOW_WATER;
    modem_update_flow(&modem);
    host_to_modem_cb(&modem);
    if (receive_count != 2 || received != MODEM_FLOW_XON)
        return 1;
    modem.flowcontrol = MODEM_FLOW_NONE;
    modem_update_flow(&modem);
    modem.data_idle_ms = 200;
    fifo8_push(&modem.rx_data, 0x41);
    host_to_modem_cb(&modem);
    if (receive_count != 3 || received != 0x41 || modem.data_idle_ms)
        return 1;
    if (test_modem_ati_commands(&modem) || test_modem_results_and_timers(&modem))
        return 1;
    fifo8_destroy(&modem.data_pending);
    fifo8_destroy(&modem.rx_data);
    return 0;
}
