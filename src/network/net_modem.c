/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Hayes AT-compliant modem emulation.
 *
 *
 *
 * Authors: Cacodemon345
 *          The DOSBox Team
 *
 *          Copyright 2024 Cacodemon345
 *          Copyright (C) 2022       The DOSBox Staging Team
 *          Copyright (C) 2002-2021  The DOSBox Team
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <wchar.h>
#include <stdbool.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/thread.h>
#include <86box/fifo.h>
#include <86box/fifo8.h>
#include <86box/timer.h>
#include <86box/serial.h>
#include <86box/plat.h>
#include <86box/network.h>
#include <86box/version.h>
#include <86box/plat_unused.h>
#include <86box/plat_netsocket.h>

#ifdef ENABLE_MODEM_LOG
int modem_do_log = ENABLE_MODEM_LOG;

static void
modem_log(const char *fmt, ...)
{
    va_list ap;

    if (modem_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define modem_log(fmt, ...)
#endif

/* From RFC 1055. */
#define END     0300 /* indicates end of packet */
#define ESC     0333 /* indicates byte stuffing */
#define ESC_END 0334 /* ESC ESC_END means END data byte */
#define ESC_ESC 0335 /* ESC ESC_ESC means ESC data byte */

typedef enum ResTypes {
    ResNONE,
    ResOK,
    ResERROR,
    ResCONNECT,
    ResRING,
    ResBUSY,
    ResNODIALTONE,
    ResNOCARRIER,
    ResNOANSWER
} ResTypes;

enum modem_types {
    MODEM_TYPE_SLIP  = 1,
    MODEM_TYPE_PPP   = 2,
    MODEM_TYPE_TCPIP = 3
};

typedef enum modem_mode_t {
    MODEM_MODE_COMMAND = 0,
    MODEM_MODE_DATA    = 1
} modem_mode_t;

typedef enum modem_slip_stage_t {
    MODEM_SLIP_STAGE_USERNAME,
    MODEM_SLIP_STAGE_PASSWORD
} modem_slip_stage_t;

#define COMMAND_BUFFER_SIZE 512
#define NUMBER_BUFFER_SIZE  128
#define PHONEBOOK_SIZE      200

typedef struct modem_phonebook_entry_t {
    char phone[NUMBER_BUFFER_SIZE];
    char address[NUMBER_BUFFER_SIZE];
} modem_phonebook_entry_t;

typedef struct modem_t {
    uint8_t   mac[6];
    serial_t *serial;
    uint32_t  baudrate;

    modem_mode_t mode;

    uint8_t    esc_character_expected;
    pc_timer_t host_to_serial_timer;
    pc_timer_t dtr_timer;
    pc_timer_t cmdpause_timer;

    uint8_t  tx_pkt_ser_line[0x10000]; /* SLIP-encoded. */
    uint32_t tx_count;

    Fifo8   rx_data; /* Data received from the network. */
    uint8_t reg[100];

    Fifo8 data_pending; /* Data yet to be sent to the host. */

    char     cmdbuf[COMMAND_BUFFER_SIZE];
    char     prevcmdbuf[COMMAND_BUFFER_SIZE];
    char     numberinprogress[NUMBER_BUFFER_SIZE];
    char     lastnumber[NUMBER_BUFFER_SIZE];
    uint32_t cmdpos;
    uint32_t port;
    int      plusinc, flowcontrol;
    int      in_warmup, dtrmode;
    int      dcdmode;

    bool     connected, ringing;
    bool     echo, numericresponse;
    bool     tcpIpMode, tcpIpConnInProgress;
    bool     cooldown;
    bool     telnet_mode;
    bool     dtrstate;
    uint32_t tcpIpConnCounter;

    int doresponse;
    int cmdpause;
    int listen_port;
    int ringtimer;

    SOCKET serversocket;
    SOCKET clientsocket;
    SOCKET waitingclientsocket;

    struct {
        bool    binary[2];
        bool    echo[2];
        bool    supressGA[2];
        bool    timingMark[2];
        bool    inIAC;
        bool    recCommand;
        uint8_t command;
    } telClient;

    modem_phonebook_entry_t entries[PHONEBOOK_SIZE];
    uint32_t                entries_num;

    netcard_t *card;
} modem_t;

#define MREG_AUTOANSWER_COUNT 0
#define MREG_RING_COUNT       1
#define MREG_ESCAPE_CHAR      2
#define MREG_CR_CHAR          3
#define MREG_LF_CHAR          4
#define MREG_BACKSPACE_CHAR   5
#define MREG_GUARD_TIME       12
#define MREG_DTR_DELAY        25

static void modem_do_command(modem_t *modem, int repeat);
static void modem_accept_incoming_call(modem_t *modem);

extern ssize_t local_getline(char **buf, size_t *bufsiz, FILE *fp);

// https://stackoverflow.com/a/122974
char *
trim(char *str)
{
    size_t len    = 0;
    char  *frontp = str;
    char  *endp   = NULL;

    if (str == NULL) {
        return NULL;
    }
    if (str[0] == '\0') {
        return str;
    }

    len  = strlen(str);
    endp = str + len;

    /* Move the front and back pointers to address the first non-whitespace
     * characters from each end.
     */
    while (isspace((unsigned char) *frontp)) {
        ++frontp;
    }
    if (endp != frontp) {
        while (isspace((unsigned char) *(--endp)) && endp != frontp) { }
    }

    if (frontp != str && endp == frontp)
        *str = '\0';
    else if (str + len - 1 != endp)
        *(endp + 1) = '\0';

    /* Shift the string so that it starts at str so that if it's dynamically
     * allocated, we can still free it on the returned pointer.  Note the reuse
     * of endp to mean the front of the string buffer now.
     */
    endp = str;
    if (frontp != str) {
        while (*frontp) {
            *endp++ = *frontp++;
        }
        *endp = '\0';
    }

    return str;
}

static void
modem_read_phonebook_file(modem_t *modem, const char *path)
{
    FILE  *file = plat_fopen(path, "r");
    char  *buf  = NULL;
    char  *buf2 = NULL;
    size_t size = 0;
    if (!file)
        return;

    modem->entries_num = 0;

    modem_log("Modem: Reading phone book file %s...\n", path);
    while (local_getline(&buf, &size, file) != -1) {
        modem_phonebook_entry_t entry = { { 0 }, { 0 } };
        buf[strcspn(buf, "\r\n")]     = '\0';

        /* Remove surrounding whitespace from the input line and find the address part. */
        buf  = trim(buf);
        buf2 = &buf[strcspn(buf, " \t")];

        /* Remove surrounding whitespace and any extra text from the address part, then store it. */
        buf2                       = trim(buf2);
        buf2[strcspn(buf2, " \t")] = '\0';
        strncpy(entry.address, buf2, sizeof(entry.address) - 1);

        /* Split the line to get the phone number part, then store it. */
        buf2[0] = '\0';
        strncpy(entry.phone, buf, sizeof(entry.phone) - 1);

        if ((entry.phone[0] == '\0') || (entry.address[0] == '\0')) {
            /* Appears to be a bad line. */
            modem_log("Modem: Skipped a bad line\n");
            continue;
        }

        if (strspn(entry.phone, "01234567890*=,;#+>") != strlen(entry.phone)) {
            /* Invalid characters. */
            modem_log("Modem: Invalid character in phone number %s\n", entry.phone);
            continue;
        }

        modem_log("Modem: Mapped phone number %s to address %s\n", entry.phone, entry.address);
        modem->entries[modem->entries_num++] = entry;
        if (modem->entries_num >= PHONEBOOK_SIZE)
            break;
    }
    fclose(file);
}

static void
modem_echo(modem_t *modem, uint8_t c)
{
    if (modem->echo && fifo8_num_free(&modem->data_pending))
        fifo8_push(&modem->data_pending, c);
}

static uint32_t
modem_scan_number(char **scan)
{
    char     c   = 0;
    uint32_t ret = 0;
    while (1) {
        c = **scan;
        if (c == 0)
            break;
        if (c >= '0' && c <= '9') {
            ret *= 10;
            ret += c - '0';
            *scan = *scan + 1;
        } else
            break;
    }
    return ret;
}

static uint8_t
modem_fetch_character(char **scan)
{
    uint8_t c = **scan;
    *scan     = *scan + 1;
    return c;
}

static void
modem_speed_changed(void *priv)
{
    modem_t *dev = (modem_t *) priv;
    if (!dev)
        return;

    timer_stop(&dev->host_to_serial_timer);
    /* FIXME: do something to dev->baudrate */
    timer_on_auto(&dev->host_to_serial_timer, (1000000.0 / (double) dev->baudrate) * 9);
#if 0
    serial_clear_fifo(dev->serial);
#endif
}

static void
modem_send_line(modem_t *modem, const char *line)
{
    fifo8_push(&modem->data_pending, modem->reg[MREG_CR_CHAR]);
    fifo8_push(&modem->data_pending, modem->reg[MREG_LF_CHAR]);
    fifo8_push_all(&modem->data_pending, (uint8_t *) line, strlen(line));
    fifo8_push(&modem->data_pending, modem->reg[MREG_CR_CHAR]);
    fifo8_push(&modem->data_pending, modem->reg[MREG_LF_CHAR]);
}

static void
modem_send_number(modem_t *modem, uint32_t val)
{
    fifo8_push(&modem->data_pending, modem->reg[MREG_CR_CHAR]);
    fifo8_push(&modem->data_pending, modem->reg[MREG_LF_CHAR]);

    fifo8_push(&modem->data_pending, val / 100 + '0');
    val = val % 100;
    fifo8_push(&modem->data_pending, val / 10 + '0');
    val = val % 10;
    fifo8_push(&modem->data_pending, val + '0');

    fifo8_push(&modem->data_pending, modem->reg[MREG_CR_CHAR]);
    fifo8_push(&modem->data_pending, modem->reg[MREG_LF_CHAR]);
}

static void
process_tx_packet(modem_t *modem, uint8_t *p, uint32_t len)
{
    int      received            = 0;
    uint32_t pos                 = 0;
    uint8_t *processed_tx_packet = calloc(len, 1);
    uint8_t  c                   = 0;

    modem_log("Processing SLIP packet of %u bytes\n", len);

    while (pos < len) {
        c = p[pos];
        pos++;
        switch (c) {
            case END:
                if (received)
                    goto send_tx_packet;
                else
                    break;

            case ESC:
                {
                    c = p[pos];
                    pos++;

                    switch (c) {
                        case ESC_END:
                            c = END;
                            break;
                        case ESC_ESC:
                            c = ESC;
                            break;
                    }
                }

            default:
                if (received < len)
                    processed_tx_packet[received++] = c;
                break;
        }
    }

send_tx_packet:
    if (received) {
        uint8_t *buf = calloc(received + 14, 1);
        buf[0] = buf[1] = buf[2] = buf[3] = buf[4] = buf[5] = 0xFF;
        buf[6] = buf[7] = buf[8] = buf[9] = buf[10] = buf[11] = 0xFC;
        buf[12]                                               = 0x08;
        buf[13]                                               = 0x00;
        memcpy(buf + 14, processed_tx_packet, received);
        network_tx(modem->card, buf, received + 14);
        free(buf);
    }
    free(processed_tx_packet);
    return;
}

static void
modem_data_mode_process_byte(modem_t *modem, uint8_t data)
{
    if (modem->reg[MREG_ESCAPE_CHAR] <= 127) {
        if (modem->plusinc >= 1 && modem->plusinc <= 3 && modem->reg[MREG_ESCAPE_CHAR] == data) {
            modem->plusinc++;
        } else {
            modem->plusinc = 0;
        }
    }
    modem->cmdpause = 0;

    if (modem->tx_count < 0x10000 && modem->connected) {
        modem->tx_pkt_ser_line[modem->tx_count++] = data;
        if (data == END && !modem->tcpIpMode) {
            process_tx_packet(modem, modem->tx_pkt_ser_line, (uint32_t) modem->tx_count);
            modem->tx_count = 0;
        }
    }
}

static void
host_to_modem_cb(void *priv)
{
    modem_t *modem = (modem_t *) priv;

    if (modem->in_warmup || (modem->serial == NULL))
        goto no_write_to_machine;

    if ((modem->serial->type >= SERIAL_16550) && modem->serial->fifo_enabled) {
        if (fifo_get_full(modem->serial->rcvr_fifo)) {
            goto no_write_to_machine;
        }
    } else {
        if (modem->serial->lsr & 1) {
            goto no_write_to_machine;
        }
    }

    if (!((modem->serial->mctrl & 2) || modem->flowcontrol != 3))
        goto no_write_to_machine;

    if (modem->mode == MODEM_MODE_DATA && fifo8_num_used(&modem->rx_data) && !modem->cooldown) {
        serial_write_fifo(modem->serial, fifo8_pop(&modem->rx_data));
    } else if (fifo8_num_used(&modem->data_pending)) {
        uint8_t val = fifo8_pop(&modem->data_pending);
        serial_write_fifo(modem->serial, val);
    }

    if (fifo8_num_used(&modem->data_pending) == 0) {
        modem->cooldown = false;
    }

no_write_to_machine:
    timer_on_auto(&modem->host_to_serial_timer, (1000000.0 / (double) modem->baudrate) * (double) 9);
}

static void
modem_write(UNUSED(serial_t *s), void *priv, uint8_t txval)
{
    modem_t *modem = (modem_t *) priv;

    if (modem->mode == MODEM_MODE_COMMAND) {
        if (modem->cmdpos < 2) {
            // Ignore everything until we see "AT" sequence.
            if (modem->cmdpos == 0 && toupper(txval) != 'A') {
                return;
            }

            if (modem->cmdpos == 1 && toupper(txval) != 'T') {
                if (txval == '/') {
                    // Repeat the last command.
                    modem_echo(modem, txval);
                    modem_log("Repeat last command (%s)\n", modem->prevcmdbuf);
                    modem_do_command(modem, 1);
                } else {
                    modem_echo(modem, modem->reg[MREG_BACKSPACE_CHAR]);
                    modem->cmdpos = 0;
                }
                return;
            }
        } else {
            // Now entering command.
            if (txval == modem->reg[MREG_BACKSPACE_CHAR]) {
                if (modem->cmdpos > 2) {
                    modem_echo(modem, txval);
                    modem->cmdpos--;
                }
                return;
            }

            if (txval == modem->reg[MREG_LF_CHAR]) {
                return; // Real modem doesn't seem to skip this?
            }

            if (txval == modem->reg[MREG_CR_CHAR]) {
                modem_echo(modem, txval);
                modem_do_command(modem, 0);
                return;
            }
        }

        if (modem->cmdpos < 99) {
            modem_echo(modem, txval);
            modem->cmdbuf[modem->cmdpos] = txval;
            modem->cmdpos++;
        }
    } else {
        modem_data_mode_process_byte(modem, txval);
    }
}

void
modem_send_res(modem_t *modem, const ResTypes response)
{
    char        response_str_connect[256] = { 0 };
    const char *response_str              = NULL;
    uint32_t    code                      = -1;

    snprintf(response_str_connect, sizeof(response_str_connect), "CONNECT %u", modem->baudrate);

    switch (response) {
        case ResOK:
            code         = 0;
            response_str = "OK";
            break;
        case ResCONNECT:
            code         = 1;
            response_str = response_str_connect;
            break;
        case ResRING:
            code         = 2;
            response_str = "RING";
            break;
        case ResNOCARRIER:
            code         = 3;
            response_str = "NO CARRIER";
            break;
        case ResERROR:
            code         = 4;
            response_str = "ERROR";
            break;
        case ResNODIALTONE:
            code         = 6;
            response_str = "NO DIALTONE";
            break;
        case ResBUSY:
            code         = 7;
            response_str = "BUSY";
            break;
        case ResNOANSWER:
            code         = 8;
            response_str = "NO ANSWER";
            break;
        case ResNONE:
            return;
    }

    if (modem->doresponse != 1) {
        if (modem->doresponse == 2 && (response == ResRING || response == ResCONNECT || response == ResNOCARRIER)) {
            return;
        }
        modem_log("Modem response: %s\n", response_str);
        if (modem->numericresponse && code != ~0) {
            modem_send_number(modem, code);
        } else if (response_str != NULL) {
            modem_send_line(modem, response_str);
        }
    }
}

void
modem_enter_idle_state(modem_t *modem)
{
    timer_disable(&modem->dtr_timer);
    modem->connected           = false;
    modem->ringing             = false;
    modem->mode                = MODEM_MODE_COMMAND;
    modem->in_warmup           = 0;
    modem->tcpIpConnInProgress = 0;
    modem->tcpIpConnCounter    = 0;

    if (modem->waitingclientsocket != (SOCKET) -1)
        plat_netsocket_close(modem->waitingclientsocket);

    if (modem->clientsocket != (SOCKET) -1)
        plat_netsocket_close(modem->clientsocket);

    modem->clientsocket = modem->waitingclientsocket = (SOCKET) -1;
    if (modem->serversocket != (SOCKET) -1) {
        modem->waitingclientsocket = plat_netsocket_accept(modem->serversocket);
        while (modem->waitingclientsocket != (SOCKET) -1) {
            plat_netsocket_close(modem->waitingclientsocket);
            modem->waitingclientsocket = plat_netsocket_accept(modem->serversocket);
        }
        plat_netsocket_close(modem->serversocket);
        modem->serversocket = (SOCKET) -1;
    }

    if (modem->waitingclientsocket != (SOCKET) -1)
        plat_netsocket_close(modem->waitingclientsocket);

    modem->waitingclientsocket = (SOCKET) -1;
    modem->tcpIpMode           = false;
    modem->tcpIpConnInProgress = false;

    if (modem->listen_port) {
        modem->serversocket = plat_netsocket_create_server(NET_SOCKET_TCP, modem->listen_port);
        if (modem->serversocket == (SOCKET) -1) {
            modem_log("Failed to set up server on port %d\n", modem->listen_port);
        }
    }

    if (modem->serial != NULL) {
        serial_set_cts(modem->serial, 1);
        serial_set_dsr(modem->serial, 1);
        serial_set_dcd(modem->serial, (!modem->dcdmode ? 1 : 0));
        serial_set_ri(modem->serial, 0);
    }
}

void
modem_enter_connected_state(modem_t *modem)
{
    modem_send_res(modem, ResCONNECT);
    modem->mode      = MODEM_MODE_DATA;
    modem->ringing   = false;
    modem->connected = true;
    modem->tcpIpMode = true;
    modem->cooldown  = true;
    modem->tx_count  = 0;
    plat_netsocket_close(modem->serversocket);
    modem->serversocket = -1;
    memset(&modem->telClient, 0, sizeof(modem->telClient));

    if (modem->serial != NULL) {
        serial_set_dcd(modem->serial, 1);
        serial_set_ri(modem->serial, 0);
    }
}

void
modem_reset(modem_t *modem)
{
    modem->dcdmode = 1;
    modem_enter_idle_state(modem);
    modem->cmdpos              = 0;
    modem->cmdbuf[0]           = 0;
    modem->prevcmdbuf[0]       = 0;
    modem->lastnumber[0]       = 0;
    modem->numberinprogress[0] = 0;
    modem->flowcontrol         = 0;
    modem->cmdpause            = 0;
    modem->plusinc             = 0;
    modem->dtrmode             = 2;

    memset(&modem->reg, 0, sizeof(modem->reg));
    modem->reg[MREG_AUTOANSWER_COUNT] = 0; // no autoanswer
    modem->reg[MREG_RING_COUNT]       = 1;
    modem->reg[MREG_ESCAPE_CHAR]      = '+';
    modem->reg[MREG_CR_CHAR]          = '\r';
    modem->reg[MREG_LF_CHAR]          = '\n';
    modem->reg[MREG_BACKSPACE_CHAR]   = '\b';
    modem->reg[MREG_GUARD_TIME]       = 50;
    modem->reg[MREG_DTR_DELAY]        = 5;

    modem->echo            = true;
    modem->doresponse      = 0;
    modem->numericresponse = false;
}

void
modem_dial(modem_t *modem, const char *str)
{
    modem->tcpIpConnCounter = 0;
    modem->tcpIpMode        = false;
    if (!strcmp(str, "0.0.0.0") || !strcmp(str, "0000")) {
        modem_log("Turning on SLIP\n");
        modem_enter_connected_state(modem);
        modem->numberinprogress[0] = 0;
        modem->tcpIpMode           = false;
    } else {
        char buf[NUMBER_BUFFER_SIZE] = "";
        strncpy(buf, str, sizeof(buf) - 1);
        strncpy(modem->lastnumber, str, sizeof(modem->lastnumber) - 1);
        modem_log("Connecting to %s...\n", buf);

        // Scan host for port
        uint16_t port;
        char    *hasport = strrchr(buf, ':');
        if (hasport) {
            *hasport++ = 0;
            port       = (uint16_t) atoi(hasport);
        } else {
            port = 23;
        }

        modem->numberinprogress[0] = 0;
        modem->clientsocket        = plat_netsocket_create(NET_SOCKET_TCP);
        if (modem->clientsocket == -1) {
            modem_log("Failed to create client socket\n");
            modem_send_res(modem, ResNOCARRIER);
            modem_enter_idle_state(modem);
            return;
        }

        if (-1 == plat_netsocket_connect(modem->clientsocket, buf, port)) {
            modem_log("Failed to connect to %s\n", buf);
            modem_send_res(modem, ResNOCARRIER);
            modem_enter_idle_state(modem);
            return;
        }
        modem->tcpIpConnInProgress = 1;
        modem->tcpIpConnCounter    = 0;
    }
}

static bool
is_next_token(const char *a, size_t N, const char *b)
{
    // Is 'b' at least as long as 'a'?
    size_t N_without_null = N - 1;
    if (strnlen(b, N) < N_without_null)
        return false;
    return (strncmp(a, b, N_without_null) == 0);
}

static const char *
modem_get_address_from_phonebook(modem_t *modem, const char *input)
{
    int i = 0;
    for (i = 0; i < modem->entries_num; i++) {
        if (strcmp(input, modem->entries[i].phone) == 0)
            return modem->entries[i].address;
    }

    return NULL;
}

static void
modem_do_command(modem_t *modem, int repeat)
{
    int   i       = 0;
    char *scanbuf = NULL;

    if (repeat) {
        /* Handle the case of A/ being invoked without a previous command to run */
        if (modem->prevcmdbuf[0] == '\0') {
            modem_send_res(modem, ResOK);
            return;
        }
        /* Load the stored previous command line */
        strncpy(modem->cmdbuf, modem->prevcmdbuf, sizeof(modem->cmdbuf) - 1);
        modem->cmdbuf[COMMAND_BUFFER_SIZE - 1] = '\0';
    } else {
        /* Store the command line to be recalled */
        strncpy(modem->prevcmdbuf, modem->cmdbuf, sizeof(modem->prevcmdbuf) - 1);
        modem->prevcmdbuf[COMMAND_BUFFER_SIZE - 1] = '\0';
        modem->cmdbuf[modem->cmdpos] = modem->prevcmdbuf[modem->cmdpos] = '\0';
    }
    modem->cmdpos = 0;
    for (i = 0; i < sizeof(modem->cmdbuf); i++) {
        modem->cmdbuf[i] = toupper(modem->cmdbuf[i]);
    }

    /* AT command set interpretation */
    if ((modem->cmdbuf[0] != 'A') || (modem->cmdbuf[1] != 'T')) {
        modem_send_res(modem, ResERROR);
        return;
    }

    modem_log("Command received: %s (doresponse = %d)\n", modem->cmdbuf, modem->doresponse);

    scanbuf = &modem->cmdbuf[2];

    while (1) {
        char chr = modem_fetch_character(&scanbuf);
        switch (chr) {
            case '+':
                if (is_next_token("NET", sizeof("NET"), scanbuf)) {
                    // only walk the pointer ahead if the command matches
                    scanbuf += 3;
                    const uint32_t requested_mode = modem_scan_number(&scanbuf);

                    // If the mode isn't valid then stop parsing
                    if (requested_mode != 1 && requested_mode != 0) {
                        modem_send_res(modem, ResERROR);
                        return;
                    }
                    // Inform the user on changes
                    if (modem->telnet_mode != !!requested_mode) {
                        modem->telnet_mode = !!requested_mode;
                    }
                    break;
                }
                modem_send_res(modem, ResERROR);
                return;
            case 'D':
                { // Dial.
                    char        buffer[NUMBER_BUFFER_SIZE];
                    char        obuffer[NUMBER_BUFFER_SIZE];
                    char       *foundstr   = &scanbuf[0];
                    const char *mappedaddr = NULL;
                    size_t      i          = 0;

                    if (*foundstr == 'T' || *foundstr == 'P') // Tone/pulse dialing
                        foundstr++;
                    else if (*foundstr == 'L') { // Redial last number
                        if (modem->lastnumber[0] == 0)
                            modem_send_res(modem, ResERROR);
                        else {
                            modem_log("Redialing number %s\n", modem->lastnumber);
                            modem_dial(modem, modem->lastnumber);
                        }
                        return;
                    }

                    if ((!foundstr[0] && !modem->numberinprogress[0]) || ((strlen(modem->numberinprogress) + strlen(foundstr)) > (NUMBER_BUFFER_SIZE - 1))) {
                        // Check for empty or too long strings
                        modem_send_res(modem, ResERROR);
                        modem->numberinprogress[0] = 0;
                        return;
                    }

                    foundstr = trim(foundstr);

                    // Check for ; and return to command mode if found
                    char *semicolon = strchr(foundstr, ';');
                    if (semicolon != NULL) {
                        modem_log("Semicolon found in number, returning to command mode\n");
                        strncat(modem->numberinprogress, foundstr, strcspn(foundstr, ";"));
                        scanbuf = semicolon + 1;
                        break;
                    } else {
                        strcat(modem->numberinprogress, foundstr);
                        foundstr = modem->numberinprogress;
                    }

                    modem_log("Dialing number %s\n", foundstr);
                    mappedaddr = modem_get_address_from_phonebook(modem, foundstr);
                    if (mappedaddr) {
                        modem_dial(modem, mappedaddr);
                        return;
                    }

                    if (strlen(foundstr) >= 12) {
                        // Check if supplied parameter only consists of digits
                        bool   isNum = true;
                        size_t fl    = strlen(foundstr);
                        for (i = 0; i < fl; i++)
                            if (foundstr[i] < '0' || foundstr[i] > '9')
                                isNum = false;
                        if (isNum && (fl > (NUMBER_BUFFER_SIZE - 5))) {
                            // Check if the number is long enough to cause buffer
                            // overflows during the number => IP transformation
                            modem_send_res(modem, ResERROR);
                            modem->numberinprogress[0] = 0;
                            return;
                        } else if (isNum) {
                            // Parameter is a number with at least 12 digits => this cannot
                            // be a valid IP/name
                            // Transform by adding dots
                            size_t       j        = 0;
                            const size_t foundlen = strlen(foundstr);
                            for (i = 0; i < foundlen; i++) {
                                buffer[j++] = foundstr[i];
                                // Add a dot after the third, sixth and ninth number
                                if (i == 2 || i == 5 || i == 8)
                                    buffer[j++] = '.';
                                // If the string is longer than 12 digits,
                                // interpret the rest as port
                                if (i == 11 && foundlen > 12)
                                    buffer[j++] = ':';
                            }
                            buffer[j] = 0;
                            foundstr  = buffer;

                            // Remove Zeros from beginning of octets
                            size_t k         = 0;
                            size_t foundlen2 = strlen(foundstr);
                            for (i = 0; i < foundlen2; i++) {
                                if (i == 0 && foundstr[0] == '0')
                                    continue;
                                if (i == 1 && foundstr[0] == '0' && foundstr[1] == '0')
                                    continue;
                                if (foundstr[i] == '0' && foundstr[i - 1] == '.')
                                    continue;
                                if (foundstr[i] == '0' && foundstr[i - 1] == '0' && foundstr[i - 2] == '.')
                                    continue;
                                obuffer[k++] = foundstr[i];
                            }
                            obuffer[k] = 0;
                            foundstr   = obuffer;
                        }
                    }
                    modem_dial(modem, foundstr);
                    return;
                }
            case 'I': // Some strings about firmware
                switch (modem_scan_number(&scanbuf)) {
                    case 3:
                        modem_send_line(modem, "86Box Emulated Modem Firmware V1.00");
                        break;
                    case 4:
                        modem_send_line(modem, "Modem compiled for 86Box version " EMU_VERSION);
                        break;
                }
                break;
            case 'E': // Echo on/off
                switch (modem_scan_number(&scanbuf)) {
                    case 0:
                        modem->echo = false;
                        break;
                    case 1:
                        modem->echo = true;
                        break;
                }
                break;
            case 'V':
                switch (modem_scan_number(&scanbuf)) {
                    case 0:
                        modem->numericresponse = true;
                        break;
                    case 1:
                        modem->numericresponse = false;
                        break;
                }
                break;
            case 'H': // Hang up
                switch (modem_scan_number(&scanbuf)) {
                    case 0:
                        modem->numberinprogress[0] = 0;
                        if (modem->connected) {
                            modem_send_res(modem, ResNOCARRIER);
                            modem_enter_idle_state(modem);
                            return;
                        }
                        // else return ok
                }
                break;
            case 'O': // Return to data mode
                switch (modem_scan_number(&scanbuf)) {
                    case 0:
                        if (modem->connected) {
                            modem->mode = MODEM_MODE_DATA;
                            return;
                        } else {
                            modem_send_res(modem, ResERROR);
                            return;
                        }
                }
                break;
            case 'T': // Tone Dial
            case 'P': // Pulse Dial
                break;
            case 'M': // Monitor
            case 'L': // Volume
            case 'W':
            case 'X':
                modem_scan_number(&scanbuf);
                break;
            case 'A': // Answer call
                {
                    if (modem->waitingclientsocket == -1) {
                        modem_send_res(modem, ResERROR);
                        return;
                    }
                    modem_accept_incoming_call(modem);
                    break;
                }
                return;
            case 'Z':
                { // Reset and load profiles
                    // scan the number away, if any
                    modem_scan_number(&scanbuf);
                    if (modem->connected)
                        modem_send_res(modem, ResNOCARRIER);
                    modem_reset(modem);
                    break;
                }
            case ' ': // skip space
                break;
            case 'Q':
                {
                    // Response options
                    // 0 = all on, 1 = all off,
                    // 2 = no ring and no connect/carrier in answermode
                    const uint32_t val = modem_scan_number(&scanbuf);
                    if (!(val > 2)) {
                        modem->doresponse = val;
                        break;
                    } else {
                        modem_send_res(modem, ResERROR);
                        return;
                    }
                }

            case 'S':
                { // Registers
                    const uint32_t index = modem_scan_number(&scanbuf);
                    if (index >= 100) {
                        modem_send_res(modem, ResERROR);
                        return; // goto ret_none;
                    }

                    while (scanbuf[0] == ' ')
                        scanbuf++; // skip spaces

                    if (scanbuf[0] == '=') { // set register
                        scanbuf++;
                        while (scanbuf[0] == ' ')
                            scanbuf++; // skip spaces
                        const uint32_t val = modem_scan_number(&scanbuf);
                        modem->reg[index]  = val;
                        break;
                    } else if (scanbuf[0] == '?') { // get register
                        modem_send_number(modem, modem->reg[index]);
                        scanbuf++;
                        break;
                    }
                    // else
                    // LOG_MSG("SERIAL: Port %" PRIu8 " print reg %" PRIu32
                    //         " with %" PRIu8 ".",
                    //         GetPortNumber(), index, reg[index]);
                }
                break;
            case '&':
                { // & escaped commands
                    char cmdchar = modem_fetch_character(&scanbuf);
                    switch (cmdchar) {
                        case 'C':
                            {
                                const uint32_t val = modem_scan_number(&scanbuf);
                                if (val < 2)
                                    modem->dcdmode = val;
                                else {
                                    modem_send_res(modem, ResERROR);
                                    return;
                                }
                                break;
                            }
                        case 'K':
                            {
                                const uint32_t val = modem_scan_number(&scanbuf);
                                if (val < 5)
                                    modem->flowcontrol = val;
                                else {
                                    modem_send_res(modem, ResERROR);
                                    return;
                                }
                                break;
                            }
                        case 'D':
                            {
                                const uint32_t val = modem_scan_number(&scanbuf);
                                if (val < 4)
                                    modem->dtrmode = val;
                                else {
                                    modem_send_res(modem, ResERROR);
                                    return;
                                }
                                break;
                            }
                        case '\0':
                            // end of string
                            modem_send_res(modem, ResERROR);
                            return;
                    }
                    break;
                }
                break;
            case '\\':
                { // \ escaped commands
                    char cmdchar = modem_fetch_character(&scanbuf);
                    switch (cmdchar) {
                        case 'N':
                            // error correction stuff - not emulated
                            if (modem_scan_number(&scanbuf) > 5) {
                                modem_send_res(modem, ResERROR);
                                return;
                            }
                            break;
                        case '\0':
                            // end of string
                            modem_send_res(modem, ResERROR);
                            return;
                    }
                    break;
                }
            case '%': // % escaped commands
                // Windows 98 modem prober sends unknown command AT%V
                modem_send_res(modem, ResERROR);
                return;
            case '\0':
                modem_send_res(modem, ResOK);
                return;
        }
    }
}

void
modem_dtr_callback_timer(void *priv)
{
    modem_t *dev = (modem_t *) priv;
    if (dev->connected) {
        switch (dev->dtrmode) {
            case 1:
                modem_log("DTR dropped, returning to command mode (dtrmode = %i)\n", dev->dtrmode);
                dev->mode = MODEM_MODE_COMMAND;
                break;
            case 2:
                modem_log("DTR dropped, hanging up (dtrmode = %i)\n", dev->dtrmode);
                modem_send_res(dev, ResNOCARRIER);
                modem_enter_idle_state(dev);
                break;
            case 3:
                modem_log("DTR dropped, resetting modem (dtrmode = %i)\n", dev->dtrmode);
                modem_send_res(dev, ResNOCARRIER);
                modem_reset(dev);
                break;
        }
    }
}

void
modem_dtr_callback(UNUSED(serial_t *serial), int status, void *priv)
{
    modem_t *dev  = (modem_t *) priv;
    dev->dtrstate = !!status;
    if (status == 1)
        timer_disable(&dev->dtr_timer);
    else if (!timer_is_enabled(&dev->dtr_timer))
        timer_on_auto(&dev->dtr_timer, 1000000);
}

static void
fifo8_resize_2x(Fifo8 *fifo)
{
    uint32_t pos  = 0;
    uint32_t size = fifo->capacity * 2;
    uint32_t used = fifo8_num_used(fifo);
    if (!used)
        return;

    uint8_t *temp_buf = calloc(size, 1);
    if (!temp_buf) {
        fatal("net_modem: Out Of Memory!\n");
    }
    while (!fifo8_is_empty(fifo)) {
        temp_buf[pos] = fifo8_pop(fifo);
        pos++;
    }
    pos = 0;
    fifo8_destroy(fifo);
    fifo8_create(fifo, size);
    fifo8_push_all(fifo, temp_buf, used);
    free(temp_buf);
}

#define TEL_CLIENT 0
#define TEL_SERVER 1
void
modem_process_telnet(modem_t *modem, uint8_t *data, uint32_t size)
{
    uint32_t i = 0;
    for (i = 0; i < size; i++) {
        uint8_t c = data[i];
        if (modem->telClient.inIAC) {
            if (modem->telClient.recCommand) {
                if ((c != 0) && (c != 1) && (c != 3)) {
                    if (modem->telClient.command > 250) {
                        /* Reject anything we don't recognize */
                        modem_data_mode_process_byte(modem, 0xff);
                        modem_data_mode_process_byte(modem, 252);
                        modem_data_mode_process_byte(modem, c); /* We won't do crap! */
                    }
                }
                switch (modem->telClient.command) {
                    case 251: /* Will */
                        if (c == 0)
                            modem->telClient.binary[TEL_SERVER] = true;
                        if (c == 1)
                            modem->telClient.echo[TEL_SERVER] = true;
                        if (c == 3)
                            modem->telClient.supressGA[TEL_SERVER] = true;
                        break;
                    case 252: /* Won't */
                        if (c == 0)
                            modem->telClient.binary[TEL_SERVER] = false;
                        if (c == 1)
                            modem->telClient.echo[TEL_SERVER] = false;
                        if (c == 3)
                            modem->telClient.supressGA[TEL_SERVER] = false;
                        break;
                    case 253: /* Do */
                        if (c == 0) {
                            modem->telClient.binary[TEL_CLIENT] = true;
                            modem_data_mode_process_byte(modem, 0xff);
                            modem_data_mode_process_byte(modem, 251);
                            modem_data_mode_process_byte(modem, 0); /* Will do binary transfer */
                        }
                        if (c == 1) {
                            modem->telClient.echo[TEL_CLIENT] = false;
                            modem_data_mode_process_byte(modem, 0xff);
                            modem_data_mode_process_byte(modem, 252);
                            modem_data_mode_process_byte(modem, 1); /* Won't echo (too lazy) */
                        }
                        if (c == 3) {
                            modem->telClient.supressGA[TEL_CLIENT] = true;
                            modem_data_mode_process_byte(modem, 0xff);
                            modem_data_mode_process_byte(modem, 251);
                            modem_data_mode_process_byte(modem, 3); /* Will Suppress GA */
                        }
                        break;
                    case 254: /* Don't */
                        if (c == 0) {
                            modem->telClient.binary[TEL_CLIENT] = false;
                            modem_data_mode_process_byte(modem, 0xff);
                            modem_data_mode_process_byte(modem, 252);
                            modem_data_mode_process_byte(modem, 0); /* Won't do binary transfer */
                        }
                        if (c == 1) {
                            modem->telClient.echo[TEL_CLIENT] = false;
                            modem_data_mode_process_byte(modem, 0xff);
                            modem_data_mode_process_byte(modem, 252);
                            modem_data_mode_process_byte(modem, 1); /* Won't echo (fine by me) */
                        }
                        if (c == 3) {
                            modem->telClient.supressGA[TEL_CLIENT] = true;
                            modem_data_mode_process_byte(modem, 0xff);
                            modem_data_mode_process_byte(modem, 251);
                            modem_data_mode_process_byte(modem, 3); /* Will Suppress GA (too lazy) */
                        }
                        break;
                    default:
                        break;
                }
                modem->telClient.inIAC      = false;
                modem->telClient.recCommand = false;
                continue;
            } else {
                if (c == 249) {
                    /* Go Ahead received */
                    modem->telClient.inIAC = false;
                    continue;
                }
                modem->telClient.command    = c;
                modem->telClient.recCommand = true;

                if ((modem->telClient.binary[TEL_SERVER]) && (c == 0xff)) {
                    /* Binary data with value of 255 */
                    modem->telClient.inIAC      = false;
                    modem->telClient.recCommand = false;
                    fifo8_push(&modem->rx_data, 0xff);
                    continue;
                }
            }
        } else {
            if (c == 0xff) {
                modem->telClient.inIAC = true;
                continue;
            }
            fifo8_push(&modem->rx_data, c);
        }
    }
}

static int
modem_rx(void *priv, uint8_t *buf, int io_len)
{
    modem_t *modem = (modem_t *) priv;
    uint32_t i     = 0;

    if (modem->tcpIpMode)
        return 0;

    if (!modem->connected) {
        /* Drop packet. */
        modem_log("Dropping %d bytes\n", io_len - 14);
        return 0;
    }

    while ((io_len) >= (fifo8_num_free(&modem->rx_data) / 2)) {
        fifo8_resize_2x(&modem->rx_data);
    }

    if (!(buf[12] == 0x08 && buf[13] == 0x00)) {
        modem_log("Dropping %d bytes (non-IP packet (ethtype 0x%02X%02X))\n", io_len - 14, buf[12], buf[13]);
        return 0;
    }

    modem_log("Receiving %d bytes\n", io_len - 14);
    /* Strip the Ethernet header. */
    io_len -= 14;
    buf += 14;

    fifo8_push(&modem->rx_data, END);
    for (i = 0; i < io_len; i++) {
        switch (buf[i]) {
            case END:
                fifo8_push(&modem->rx_data, ESC);
                fifo8_push(&modem->rx_data, ESC_END);
                break;
            case ESC:
                fifo8_push(&modem->rx_data, ESC);
                fifo8_push(&modem->rx_data, ESC_ESC);
                break;
            default:
                fifo8_push(&modem->rx_data, buf[i]);
                break;
        }
    }
    fifo8_push(&modem->rx_data, END);
    return 1;
}

static void
modem_rcr_cb(UNUSED(struct serial_s *serial), void *priv)
{
    modem_t *dev = (modem_t *) priv;

    timer_stop(&dev->host_to_serial_timer);
    /* FIXME: do something to dev->baudrate */
    timer_on_auto(&dev->host_to_serial_timer, (1000000.0 / (double) dev->baudrate) * (double) 9);
#if 0
    serial_clear_fifo(dev->serial);
#endif
}

static void
modem_accept_incoming_call(modem_t *modem)
{
    if (modem->waitingclientsocket != -1) {
        modem->clientsocket        = modem->waitingclientsocket;
        modem->waitingclientsocket = -1;
        modem_enter_connected_state(modem);
        modem->in_warmup = 250;
    } else {
        modem_enter_idle_state(modem);
    }
}

static void
modem_cmdpause_timer_callback(void *priv)
{
    modem_t *modem            = (modem_t *) priv;
    uint32_t guard_threshold = 0;
    timer_on_auto(&modem->cmdpause_timer, 1000);

    if (modem->tcpIpConnInProgress) {
        do {
            int status = plat_netsocket_connected(modem->clientsocket);

            if (status == -1) {
                plat_netsocket_close(modem->clientsocket);
                modem->clientsocket = -1;
                modem_enter_idle_state(modem);
                modem_send_res(modem, ResNOCARRIER);
                modem->tcpIpConnInProgress = 0;
                break;
            } else if (status == 1) {
                modem_enter_connected_state(modem);
                modem->tcpIpConnInProgress = 0;
                break;
            }

            modem->tcpIpConnCounter++;

            if (status < 0 || (status == 0 && modem->tcpIpConnCounter >= 5000)) {
                plat_netsocket_close(modem->clientsocket);
                modem->clientsocket = -1;
                modem_enter_idle_state(modem);
                modem_send_res(modem, ResNOANSWER);
                modem->tcpIpConnInProgress = 0;
                modem->tcpIpMode           = 0;
                break;
            }
        } while (0);
    }

    if (!modem->connected && modem->waitingclientsocket == -1 && modem->serversocket != -1) {
        modem->waitingclientsocket = plat_netsocket_accept(modem->serversocket);
        if (modem->waitingclientsocket != -1) {
            if (modem->dtrstate == 0 && modem->dtrmode != 0) {
                modem_enter_idle_state(modem);
            } else {
                modem->ringing = true;
                modem_send_res(modem, ResRING);
                if (modem->serial != NULL)
                    serial_set_ri(modem->serial, !serial_get_ri(modem->serial));
                modem->ringtimer            = 3000;
                modem->reg[MREG_RING_COUNT] = 0;
            }
        }
    }
    if (modem->ringing) {
        if (modem->ringtimer <= 0) {
            modem->reg[MREG_RING_COUNT]++;
            if ((modem->reg[MREG_AUTOANSWER_COUNT] > 0) && (modem->reg[MREG_RING_COUNT] >= modem->reg[MREG_AUTOANSWER_COUNT])) {
                modem_accept_incoming_call(modem);
                return;
            }
            modem_send_res(modem, ResRING);
            if (modem->serial != NULL)
                serial_set_ri(modem->serial, !serial_get_ri(modem->serial));

            modem->ringtimer = 3000;
        }
        --modem->ringtimer;
    }

    if (modem->in_warmup) {
        modem->in_warmup--;
        if (modem->in_warmup == 0) {
            modem->tx_count = 0;
            fifo8_reset(&modem->rx_data);
        }
    } else if (modem->connected && modem->tcpIpMode) {
        if (modem->tx_count) {
            int wouldblock = 0;
            int res        = plat_netsocket_send(modem->clientsocket, modem->tx_pkt_ser_line, modem->tx_count, &wouldblock);

            if (res <= 0 && !wouldblock) {
                /* No bytes sent or error. */
                modem->tx_count = 0;
                modem_enter_idle_state(modem);
                modem_send_res(modem, ResNOCARRIER);
            } else if (res > 0) {
                if (res == modem->tx_count) {
                    modem->tx_count = 0;
                } else {
                    memmove(modem->tx_pkt_ser_line, &modem->tx_pkt_ser_line[res], modem->tx_count - res);
                    modem->tx_count -= res;
                }
            }
        }
        if (modem->connected) {
            uint8_t buffer[16];
            int     wouldblock = 0;
            int     recv       = MIN(modem->rx_data.capacity - modem->rx_data.num, sizeof(buffer));
            int     res        = plat_netsocket_receive(modem->clientsocket, buffer, recv, &wouldblock);

            if (res > 0) {
                if (modem->telnet_mode)
                    modem_process_telnet(modem, buffer, res);
                else
                    fifo8_push_all(&modem->rx_data, buffer, res);
            } else if (res == 0) {
                modem->tx_count = 0;
                modem_enter_idle_state(modem);
                modem_send_res(modem, ResNOCARRIER);
            } else if (!wouldblock) {
                modem->tx_count = 0;
                modem_enter_idle_state(modem);
                modem_send_res(modem, ResNOCARRIER);
            }
        }
    }

    modem->cmdpause++;
    guard_threshold = (uint32_t) (modem->reg[MREG_GUARD_TIME] * 20);
    if (modem->cmdpause > guard_threshold) {
        if (modem->plusinc == 0) {
            modem->plusinc = 1;
        } else if (modem->plusinc == 4) {
            modem_log("Escape sequence triggered, returning to command mode\n");
            modem->mode = MODEM_MODE_COMMAND;
            modem_send_res(modem, ResOK);
            modem->plusinc = 0;
        }
    }
}

/* Initialize the device for use by the user. */
static void *
modem_init(UNUSED(const device_t *info))
{
    modem_t    *modem          = (modem_t *) calloc(1, sizeof(modem_t));
    const char *phonebook_file = NULL;
    memset(modem->mac, 0xfc, 6);

    modem->port        = device_get_config_int("port");
    modem->baudrate    = device_get_config_int("baudrate");
    modem->listen_port = device_get_config_int("listen_port");
    modem->telnet_mode = device_get_config_int("telnet_mode");

    modem->clientsocket = modem->serversocket = modem->waitingclientsocket = -1;

    fifo8_create(&modem->data_pending, 0x40000);
    fifo8_create(&modem->rx_data, 0x40000);

    timer_add(&modem->dtr_timer, modem_dtr_callback_timer, modem, 0);
    timer_add(&modem->host_to_serial_timer, host_to_modem_cb, modem, 0);
    timer_add(&modem->cmdpause_timer, modem_cmdpause_timer_callback, modem, 0);
    timer_on_auto(&modem->cmdpause_timer, 1000);
    modem->serial = serial_attach_ex_2(modem->port, modem_rcr_cb, modem_write, modem_dtr_callback, modem);

    modem_reset(modem);
    modem->card = network_attach(modem, modem->mac, modem_rx, NULL);

    phonebook_file = device_get_config_string("phonebook_file");
    if (phonebook_file && phonebook_file[0] != 0) {
        modem_read_phonebook_file(modem, phonebook_file);
    }

    return modem;
}

void
modem_close(void *priv)
{
    modem_t *modem     = (modem_t *) priv;
    modem->listen_port = 0;
    modem_reset(modem);
    fifo8_destroy(&modem->data_pending);
    fifo8_destroy(&modem->rx_data);
    netcard_close(modem->card);
    free(priv);
}

// clang-format off
static const device_config_t modem_config[] = {
    {
        .name           = "port",
        .description    = "Serial Port",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "COM1", .value = 0 },
            { .description = "COM2", .value = 1 },
            { .description = "COM3", .value = 2 },
            { .description = "COM4", .value = 3 },
            { .description = ""                 }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "baudrate",
        .description    = "Baud Rate",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 115200,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "115200", .value = 115200 },
            { .description =  "57600", .value =  57600 },
            { .description =  "56000", .value =  56000 },
            { .description =  "38400", .value =  38400 },
            { .description =  "19200", .value =  19200 },
            { .description =  "14400", .value =  14400 },
            { .description =   "9600", .value =   9600 },
            { .description =   "7200", .value =   7200 },
            { .description =   "4800", .value =   4800 },
            { .description =   "2400", .value =   2400 },
            { .description =   "1800", .value =   1800 },
            { .description =   "1200", .value =   1200 },
            { .description =    "600", .value =    600 },
            { .description =    "300", .value =    300 },
            { .description = ""                        }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "listen_port",
        .description    = "TCP/IP listening port",
        .type           = CONFIG_SPINNER,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = {
            .min =     0,
            .max = 32767
        },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "phonebook_file",
        .description    = "Phonebook File",
        .type           = CONFIG_FNAME,
        .default_string = NULL,
        .file_filter    = "Text files (*.txt)|*.txt",
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "telnet_mode",
        .description    = "Telnet emulation",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t modem_device = {
    .name          = "Standard Hayes-compliant Modem",
    .internal_name = "modem",
    .flags         = DEVICE_COM,
    .local         = 0,
    .init          = modem_init,
    .close         = modem_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = modem_speed_changed,
    .force_redraw  = NULL,
    .config        = modem_config
};
