/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          Character modem serial timing and trace tests.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include "../../../src/char/char_modem.c"
#include "modem_reset_test_stubs.h"
#include "modem_config_test.h"
#include "modem_at_test.h"

static double timer_period;
static char log_buffer[65536];
static size_t log_length;
static int status_updates;

void
char_update_status(UNUSED(char_port_t *port))
{
    status_updates++;
}

void
timer_stop(pc_timer_t *timer)
{
    timer->flags &= ~TIMER_ENABLED;
}

void
timer_disable(pc_timer_t *timer)
{
    timer->flags &= ~TIMER_ENABLED;
}

void
timer_on_auto(pc_timer_t *timer, double period)
{
    timer->flags |= TIMER_ENABLED;
    timer_period = period;
}

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

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int
test_pnp_profiles_output(void)
{
    modem_t modem = { 0 };
    char_port_t port = { 0 };
    uint8_t byte;

    modem.char_port = &port;
    port.com.baud = 1201;
    port.com.data_bits = 7;
    port.com.stop_bits = 1;
    fifo8_create(&modem.data_pending, 256);
    fifo8_create(&modem.rx_data, 256);
    fifo8_create(&modem.char_tx_data, 256);
    for (size_t i = 0; i < sizeof(test_pnp_profiles) / sizeof(test_pnp_profiles[0]); i++) {
        modem_pnp_init(&modem.pnp, test_pnp_profiles[i].value);
        modem_char_port_config(&modem);
        modem_char_control(0, &modem);
        for (int tick = 0; tick < 200; tick++)
            modem_pnp_tick(&modem.pnp, true);
        modem_char_control(CHAR_COM_DTR, &modem);
        CHECK(modem_pnp_pending(&modem.pnp));
        CHECK(strstr((const char *) modem.pnp.response, test_pnp_profiles[i].id) != NULL);
        const unsigned length = modem.pnp.response_length;
        for (unsigned j = 0; j < length; j++) {
            host_to_modem_cb(&modem);
            CHECK(modem_char_read(&byte, 1, &modem) == 1);
            CHECK(byte == modem.pnp.response[j]);
        }
        CHECK(!modem_pnp_pending(&modem.pnp));
    }
    fifo8_destroy(&modem.data_pending);
    fifo8_destroy(&modem.rx_data);
    fifo8_destroy(&modem.char_tx_data);
    log_length = 0;
    log_buffer[0] = '\0';
    return 0;
}

int
main(void)
{
    modem_t modem = { 0 };
    char_port_t port = { 0 };
    uint8_t bytes[64];

    CHECK(test_modem_config() == 0);
    CHECK(test_pnp_profiles_output() == 0);
    modem.char_port = &port;
    modem.baudrate = modem.dte_baudrate = 115200;
    modem.result_mode = MODEM_DEFAULT_X;
    modem.reg[MREG_CR_CHAR] = '\r';
    modem.reg[MREG_LF_CHAR] = '\n';
    fifo8_create(&modem.data_pending, 256);
    fifo8_create(&modem.rx_data, 16384);
    fifo8_create(&modem.char_tx_data, 256);

    modem.char_status = CHAR_COM_CTS | CHAR_COM_DSR | CHAR_COM_RI;
    modem.dcdmode = 0;
    modem_update_dcd(&modem);
    CHECK((modem.char_status & CHAR_COM_DCD) != 0);
    CHECK((modem.char_status & CHAR_COM_RI) != 0);
    modem.dcdmode = 1;
    modem_update_dcd(&modem);
    CHECK((modem.char_status & CHAR_COM_DCD) == 0);
    modem.connected = true;
    modem_update_dcd(&modem);
    CHECK((modem.char_status & CHAR_COM_DCD) != 0);
    CHECK(status_updates == 3);
    modem.connected = false;

    port.com.baud = 9600;
    modem_char_port_config(&modem);
    CHECK(modem.dte_baudrate == 9600);
    CHECK(timer_period > 937.4 && timer_period < 937.6);
    modem_send_res(&modem, ResCONNECT);
    size_t count = fifo8_num_used(&modem.data_pending);
    CHECK(count > 0 && count < sizeof(bytes));
    fifo8_pop_buf(&modem.data_pending, bytes, (uint32_t) count);
    bytes[count] = '\0';
    CHECK(strcmp((char *) bytes, "\r\nCONNECT 9600\r\n") == 0);

    port.com.baud = 9615;
    modem_char_port_config(&modem);
    CHECK(modem.dte_baudrate == 9615);
    CHECK(timer_period > 935.9 && timer_period < 936.2);
    modem_send_res(&modem, ResCONNECT);
    CHECK(test_modem_output(&modem, "\r\nCONNECT 9600\r\n") == 0);

    modem.rx_queue_limit_ms = 1000;
    for (int i = 0; i < 4000; i++)
        fifo8_push(&modem.rx_data, 0x41);
    CHECK(modem_rx_queue_full(&modem, 0));
    port.com.baud = 115200;
    modem_char_port_config(&modem);
    CHECK(modem.dte_baudrate == 115200);
    CHECK(!modem_rx_queue_full(&modem, 0));
    port.com.baud = 0;
    modem_char_port_config(&modem);
    CHECK(modem.dte_baudrate == 115200);
    fifo8_reset(&modem.rx_data);

    modem.connected = true;
    modem.mode = MODEM_MODE_DATA;
    modem.flowcontrol = 3;
    modem_update_flow(&modem);
    modem.reg[MREG_DTR_DELAY] = 5;
    fifo8_push(&modem.rx_data, 0x41);
    modem.data_idle_ms = 200;
    host_to_modem_cb(&modem);
    CHECK(fifo8_is_empty(&modem.char_tx_data));
    CHECK(modem.data_idle_ms == 200);
    modem_char_control(CHAR_COM_RTS | CHAR_COM_DTR, &modem);
    host_to_modem_cb(&modem);
    CHECK(modem.data_idle_ms == 0);
    CHECK(strstr(log_buffer, "SERIAL RX (modem->guest)") == NULL);
    CHECK(modem_char_read(bytes, sizeof(bytes), &modem) == 1 && bytes[0] == 0x41);
    CHECK(strstr(log_buffer, "MODEM DTR t=1234 ms asserted") != NULL);
    CHECK(strstr(log_buffer, "SERIAL RX (modem->guest) t=1234 ms mode=1 byte=41") != NULL);
    modem_char_control(CHAR_COM_RTS, &modem);
    CHECK(strstr(log_buffer, "MODEM DTR t=1234 ms deasserted") != NULL);
    CHECK(timer_period == 50000.0);
    CHECK(timer_is_enabled(&modem.dtr_timer));
    modem_char_control(CHAR_COM_RTS | CHAR_COM_DTR, &modem);
    CHECK(!timer_is_enabled(&modem.dtr_timer));
    modem.reg[MREG_DTR_DELAY] = 0;
    modem_char_control(CHAR_COM_RTS, &modem);
    CHECK(timer_period == 1.0);
    modem_char_control(CHAR_COM_RTS | CHAR_COM_DTR, &modem);
    modem.reg[MREG_DTR_DELAY] = 255;
    modem_char_control(CHAR_COM_RTS, &modem);
    CHECK(timer_period == 2550000.0);
    modem_char_control(CHAR_COM_RTS | CHAR_COM_DTR, &modem);

    modem.connected = false;
    modem.mode = MODEM_MODE_COMMAND;
    modem_pnp_init(&modem.pnp, MODEM_PNP_ENHANCED);
    port.com.baud = 1201;
    port.com.data_bits = 7;
    port.com.parity = 0;
    port.com.stop_bits = 1;
    modem_char_port_config(&modem);
    CHECK(strstr(log_buffer, "SERIAL PNP format t=1234 ms profile=2 baud=1201 data_bits=7 parity=0 stop_bits=1") != NULL);
    modem.connected = true;
    modem_char_control(CHAR_COM_DTR | CHAR_COM_RTS, &modem);
    CHECK(strstr(log_buffer, "dtr=0->1 rts=0->1 format_ok=1 eligible=0") != NULL);
    modem.connected = false;
    modem_char_control(CHAR_COM_DTR, &modem);
    modem_char_control(0, &modem);
    for (int i = 0; i < 200; i++)
        modem_pnp_tick(&modem.pnp, true);
    modem_char_control(CHAR_COM_DTR, &modem);
    CHECK(modem_pnp_pending(&modem.pnp));
    CHECK(timer_period == 7500.0);
    CHECK(strstr(log_buffer, "dtr=0->1 rts=0->0 format_ok=1 eligible=1 low_ms=200") != NULL);
    size_t pnp_length = 0;
    host_to_modem_cb(&modem);
    host_to_modem_cb(&modem);
    CHECK(modem.pnp.response_pos == 0);
    pnp_length += modem_char_read(bytes, sizeof(bytes), &modem);
    CHECK(pnp_length == 1 && bytes[0] == '(');
    CHECK(strstr(log_buffer, "SERIAL RX (modem->guest) t=1234 ms mode=0 byte=28 source=pnp") != NULL);
    while (modem_pnp_pending(&modem.pnp)) {
        CHECK(pnp_length < sizeof(bytes));
        host_to_modem_cb(&modem);
        pnp_length += modem_char_read(bytes + pnp_length, sizeof(bytes) - pnp_length, &modem);
    }
    CHECK(pnp_length > 10 && pnp_length < sizeof(bytes));
    bytes[pnp_length] = '\0';
    CHECK(strstr((char *) bytes, "PNPC107") != NULL);
    CHECK(strstr(log_buffer, "byte=29 source=pnp") != NULL);

    modem_char_control(CHAR_COM_DTR | CHAR_COM_RTS, &modem);
    fifo8_push(&modem.char_tx_data, 0x4f);
    CHECK(modem_char_read(bytes, sizeof(bytes), &modem) == 1 && bytes[0] == 0x4f);
    CHECK(strstr(log_buffer, "SERIAL RX (modem->guest) t=1234 ms mode=0 byte=4F source=output") != NULL);

    modem_char_control(0, &modem);
    for (int i = 0; i < 200; i++)
        modem_pnp_tick(&modem.pnp, true);
    modem_char_control(CHAR_COM_DTR, &modem);
    host_to_modem_cb(&modem);
    CHECK(modem.pnp_char_ready);
    port.com.baud = 9600;
    modem_char_port_config(&modem);
    CHECK(strstr(log_buffer, "baud=9600 data_bits=7 parity=0 stop_bits=1 low_ms=0 pending=1") != NULL);
    CHECK(modem_char_read(bytes, sizeof(bytes), &modem) == 0);

    port.com.baud = 1200;
    modem_char_port_config(&modem);
    modem_char_control(0, &modem);
    for (int i = 0; i < 200; i++)
        modem_pnp_tick(&modem.pnp, true);
    modem_char_control(CHAR_COM_DTR, &modem);
    host_to_modem_cb(&modem);
    modem.connected = true;
    CHECK(modem_char_read(bytes, sizeof(bytes), &modem) == 0);
    CHECK(!modem_pnp_pending(&modem.pnp));
    CHECK(strstr(log_buffer, "reason=read-ineligible connected=1") != NULL);

    modem.connected = false;
    modem.profile.saved[0] = 0;
    modem.profile.saved[1] = 2;
    modem.profile.saved[3] = 2;
    modem.profile.saved[4] = 3;
    modem.profile.saved[9 + MREG_DTR_DELAY] = 5;
    modem.clientsocket = modem.serversocket = modem.waitingclientsocket = -1;
    modem_reset_profile(&modem);
    CHECK(modem.dcdmode == 0 && (modem.char_status & CHAR_COM_DCD));
    CHECK(modem.speaker_mode == 2 && modem.speaker_level == 3);
    CHECK(test_speaker_mode == 2 && test_speaker_level == 3);
    CHECK(modem.reg[MREG_RING_COUNT] == 0 && modem.reg[MREG_DTR_DELAY] == 5);
    modem.connected = true;
    modem.dtrmode = 3;
    modem.dcdmode = 1;
    modem_dtr_callback_timer(&modem);
    CHECK(!modem.connected && modem.dcdmode == 0 && (modem.char_status & CHAR_COM_DCD));
    CHECK(modem.speaker_mode == 2 && modem.speaker_level == 3);

    fifo8_reset(&modem.data_pending);
    fifo8_reset(&modem.char_tx_data);
    fifo8_reset(&modem.rx_data);
    modem.connected = true;
    modem.tcpIpMode = true;
    modem.mode = MODEM_MODE_DATA;
    modem.flowcontrol = MODEM_FLOW_HARDWARE;
    modem.tx_count = MODEM_FLOW_HIGH_WATER;
    modem_update_flow(&modem);
    CHECK(!(modem.char_status & CHAR_COM_CTS));
    modem.tx_count = MODEM_FLOW_LOW_WATER;
    modem_update_flow(&modem);
    CHECK(modem.char_status & CHAR_COM_CTS);

    modem.flowcontrol = MODEM_FLOW_SOFTWARE;
    modem.tx_count = MODEM_FLOW_HIGH_WATER;
    modem_update_flow(&modem);
    CHECK(modem_flow_receive(&modem.flow, MODEM_FLOW_XOFF));
    fifo8_push(&modem.char_tx_data, 0x41);
    host_to_modem_cb(&modem);
    CHECK(modem_char_read(bytes, sizeof(bytes), &modem) == 1 && bytes[0] == MODEM_FLOW_XOFF);
    CHECK(strstr(log_buffer, "byte=13 source=flow") != NULL);
    CHECK(modem_char_read(bytes, sizeof(bytes), &modem) == 0);
    modem.tx_count = MODEM_FLOW_LOW_WATER;
    modem_update_flow(&modem);
    host_to_modem_cb(&modem);
    CHECK(modem_char_read(bytes, sizeof(bytes), &modem) == 1 && bytes[0] == MODEM_FLOW_XON);
    CHECK(modem_flow_receive(&modem.flow, MODEM_FLOW_XON));
    CHECK(modem_char_read(bytes, sizeof(bytes), &modem) == 1 && bytes[0] == 0x41);

    CHECK(test_modem_ati_commands(&modem) == 0);
    CHECK(test_modem_results_and_timers(&modem) == 0);
    fifo8_destroy(&modem.data_pending);
    fifo8_destroy(&modem.rx_data);
    fifo8_destroy(&modem.char_tx_data);
    return 0;
}
