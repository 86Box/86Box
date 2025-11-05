/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          Shared modem result and carrier timer tests.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
static int
test_modem_output(modem_t *modem, const char *expected)
{
    uint8_t bytes[256];
    size_t count = fifo8_num_used(&modem->data_pending);
    if (count >= sizeof(bytes))
        return 1;
    fifo8_pop_buf(&modem->data_pending, bytes, (uint32_t) count);
    bytes[count] = '\0';
    if (strcmp((char *) bytes, expected)) {
        fprintf(stderr, "Unexpected modem output: '%s', expected '%s'\n", bytes, expected);
        return 1;
    }
    return 0;
}

static int
test_modem_ati_commands(modem_t *modem)
{
    const struct {
        const char *command;
        const char *remainder;
        const char *expected;
    } cases[] = {
        { "", "", "\r\n86Box Emulated Modem\r\n\r\nOK\r\n" },
        { "0", "", "\r\n86Box Emulated Modem\r\n\r\nOK\r\n" },
        { "1", "", "\r\nOK\r\n" },
        { "2", "", "\r\nOK\r\n" },
        { "3", "", "\r\n86Box Emulated Modem Firmware V1.00\r\n\r\nOK\r\n" },
        { "4", "", "\r\nModem compiled for 86Box version " EMU_VERSION "\r\n\r\nOK\r\n" },
        { "5", "", "\r\nOK\r\n" },
        { "6", "", "\r\nOK\r\n" },
        { "7", "", "\r\nOK\r\n" },
        { "8", "", "\r\nOK\r\n" },
        { "9", "", "\r\nOK\r\n" },
        { "999", "", "\r\nOK\r\n" },
        { "1E0", "E0", "\r\nOK\r\n" }
    };
    fifo8_reset(&modem->data_pending);
    modem->reg[MREG_CR_CHAR] = '\r';
    modem->reg[MREG_LF_CHAR] = '\n';
    modem->doresponse = 0;
    modem->numericresponse = false;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char buffer[32];
        char *scan = buffer;
        snprintf(buffer, sizeof(buffer), "%s", cases[i].command);
        modem_command_identification(modem, &scan);
        if (strcmp(scan, cases[i].remainder))
            return 1;
        modem_send_res(modem, ResOK);
        if (test_modem_output(modem, cases[i].expected))
            return 1;
    }
    modem->numericresponse = true;
    {
        char buffer[] = "1";
        char *scan = buffer;
        modem_command_identification(modem, &scan);
    }
    modem_send_res(modem, ResOK);
    if (test_modem_output(modem, "0\r"))
        return 1;
    modem->doresponse = 1;
    {
        char buffer[] = "1";
        char *scan = buffer;
        modem_command_identification(modem, &scan);
    }
    modem_send_res(modem, ResOK);
    if (test_modem_output(modem, ""))
        return 1;
    return 0;
}

static int
test_modem_results_and_timers(modem_t *modem)
{
    fifo8_reset(&modem->data_pending);
    modem->reg[MREG_CR_CHAR] = '\r';
    modem->reg[MREG_LF_CHAR] = '\n';
    modem->doresponse = 0;
    modem->numericresponse = false;
    modem->result_mode = 0;
    modem->connect_report = 0;
    modem->dte_baudrate = 9600;
    modem_send_res(modem, ResCONNECT);
    if (test_modem_output(modem, "\r\nCONNECT\r\n"))
        return 1;
    modem->result_mode = 4;
    modem->dte_baudrate = 9615;
    modem_send_res(modem, ResCONNECT);
    if (test_modem_output(modem, "\r\nCONNECT 9600\r\n"))
        return 1;
    modem->connect_report = 1;
    modem_send_res(modem, ResCONNECT);
    if (test_modem_output(modem, "\r\nCARRIER 9600\r\n\r\nPROTOCOL: NONE\r\n"
                                 "\r\nCOMPRESSION: NONE\r\n\r\nCONNECT 9600\r\n"))
        return 1;
    if (modem->dte_baudrate != 9615)
        return 1;
    modem->numericresponse = true;
    modem_send_res(modem, ResCONNECT);
    if (test_modem_output(modem, "12\r"))
        return 1;
    modem->numericresponse = false;
    modem->connect_report = 2;
    modem->dte_baudrate = 38461;
    modem_send_res(modem, ResCONNECT);
    if (test_modem_output(modem, "\r\nCONNECT 38400\r\n"))
        return 1;
    if (modem->dte_baudrate != 38461)
        return 1;
    modem->numericresponse = true;
    modem_send_res(modem, ResCONNECT);
    if (test_modem_output(modem, "17\r"))
        return 1;
    modem->numericresponse = false;
    modem->connect_report = 0;
    modem->dte_baudrate = 12345;
    modem_send_res(modem, ResCONNECT);
    if (test_modem_output(modem, "\r\nCONNECT 12345\r\n"))
        return 1;
    modem->numericresponse = true;
    modem->result_mode = 0;
    modem_send_res(modem, ResCONNECT);
    if (test_modem_output(modem, "1\r"))
        return 1;
    modem_send_res(modem, ResBUSY);
    if (test_modem_output(modem, "3\r"))
        return 1;

    modem->numericresponse = false;
    modem->result_mode = MODEM_DEFAULT_X;
    modem->connected = true;
    modem->tcpIpMode = true;
    modem->mode = MODEM_MODE_DATA;
    modem->clientsocket = modem->serversocket = modem->waitingclientsocket = (SOCKET) -1;
    modem->reg[MREG_CARRIER_LOSS] = 1;
    modem_carrier_lost(modem, "test carrier loss");
    if (!modem->connected || modem->carrier_loss_ms != 100 || fifo8_num_used(&modem->data_pending))
        return 1;
    for (int i = 0; i < 99; i++)
        modem_carrier_loss_tick(modem);
    if (!modem->connected || modem->carrier_loss_ms != 1)
        return 1;
    modem_carrier_loss_tick(modem);
    if (modem->connected || test_modem_output(modem, "\r\nNO CARRIER\r\n"))
        return 1;

    modem->connected = true;
    modem->tcpIpMode = true;
    modem->reg[MREG_CARRIER_LOSS] = 0;
    modem_carrier_lost(modem, "test immediate carrier loss");
    if (modem->connected || test_modem_output(modem, "\r\nNO CARRIER\r\n"))
        return 1;

    modem->call_progress_active = true;
    modem->answer_side = false;
    modem->call_dial_end_ms = 100;
    modem->call_progress_elapsed_ms = 1099;
    modem->reg[MREG_CARRIER_WAIT] = 1;
    modem_carrier_wait_tick(modem);
    if (!modem->call_progress_active)
        return 1;
    modem->call_progress_elapsed_ms = 1100;
    modem->answer_side = true;
    modem_carrier_wait_tick(modem);
    if (!modem->call_progress_active)
        return 1;
    modem->answer_side = false;
    modem_carrier_wait_tick(modem);
    if (modem->call_progress_active || test_modem_output(modem, "\r\nNO CARRIER\r\n"))
        return 1;

    modem->connected = true;
    modem->tcpIpMode = true;
    modem->mode = MODEM_MODE_DATA;
    modem->reg[MREG_INACTIVITY] = 0;
    modem->data_idle_ms = 59999;
    modem_data_idle_tick(modem);
    if (!modem->connected || modem->data_idle_ms)
        return 1;
    modem->reg[MREG_INACTIVITY] = 1;
    for (int i = 0; i < 59999; i++)
        modem_data_idle_tick(modem);
    if (!modem->connected || modem->data_idle_ms != 59999)
        return 1;

    modem_data_mode_process_byte(modem, 'A');
    if (modem->data_idle_ms || modem->tx_count != 1)
        return 1;
    modem->data_idle_ms = 59999;
    const uint8_t incoming = 'B';
    modem_ppp_serial_push(modem, &incoming, 1);
    if (modem->data_idle_ms || fifo8_pop(&modem->rx_data) != incoming)
        return 1;

    modem->mode = MODEM_MODE_COMMAND;
    modem->data_idle_ms = 59999;
    modem_data_idle_tick(modem);
    if (modem->connected || modem->mode != MODEM_MODE_COMMAND
        || modem->data_idle_ms || test_modem_output(modem, "\r\nNO CARRIER\r\n"))
        return 1;

    modem->connected = true;
    modem->mode = MODEM_MODE_DATA;
    modem->reg[MREG_INACTIVITY] = 255;
    modem->data_idle_ms = 255u * 60000u - 2;
    modem_data_idle_tick(modem);
    if (!modem->connected || modem->data_idle_ms != 255u * 60000u - 1)
        return 1;
    modem_data_idle_tick(modem);
    if (modem->connected || test_modem_output(modem, "\r\nNO CARRIER\r\n"))
        return 1;

    modem->connected = true;
    modem->fax_class = 1;
    modem->reg[MREG_INACTIVITY] = 1;
    modem->data_idle_ms = 59999;
    modem_data_idle_tick(modem);
    if (!modem->connected || modem->data_idle_ms)
        return 1;
    modem->fax_class = 0;
    modem->voice_class = 8;
    modem->data_idle_ms = 59999;
    modem_data_idle_tick(modem);
    if (!modem->connected || modem->data_idle_ms)
        return 1;
    modem->voice_class = 0;
    modem->tcpIpMode = false;
    modem->data_idle_ms = 59999;
    modem_data_idle_tick(modem);
    if (modem->connected || test_modem_output(modem, "\r\nNO CARRIER\r\n"))
        return 1;
    modem->connected = true;
    modem->carrier_loss_ms = 10;
    modem->data_idle_ms = 59999;
    modem_data_idle_tick(modem);
    if (!modem->connected || modem->data_idle_ms)
        return 1;
    modem->carrier_loss_ms = 0;
    modem->connected = false;
    modem->reg[MREG_INACTIVITY] = 0;
    return 0;
}
