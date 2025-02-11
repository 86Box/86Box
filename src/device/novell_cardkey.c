/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Novell NetWare 2.x Key Card, which
 *          was used for anti-piracy protection.
 *
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2024 Cacodemon345.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/novell_cardkey.h>

typedef struct novell_cardkey_t {
    char serial_number_str[13];
} novell_cardkey_t;

static uint8_t
novell_cardkey_read(uint16_t port, void *priv)
{
    novell_cardkey_t* cardkey = (novell_cardkey_t*)priv;
    uint8_t val = 0x00;
    switch (port) {
        case 0x23A:
            val = (((cardkey->serial_number_str[11] > 'A') ? ((cardkey->serial_number_str[11] - 'A') + 10) : (cardkey->serial_number_str[11] - '0')) << 4) | (((cardkey->serial_number_str[9] > 'A') ? ((cardkey->serial_number_str[9] - 'A') + 10) : (cardkey->serial_number_str[9] - '0')) << 4);
            break;
        case 0x23B:
            val = (((cardkey->serial_number_str[10] > 'A') ? ((cardkey->serial_number_str[10] - 'A') + 10) : (cardkey->serial_number_str[10] - '0')) << 4) | (((cardkey->serial_number_str[8] > 'A') ? ((cardkey->serial_number_str[8] - 'A') + 10) : (cardkey->serial_number_str[8] - '0')) << 4);
            break;

        case 0x23C:
            val = ((cardkey->serial_number_str[4] - '0') << 4) | ((cardkey->serial_number_str[2] - '0'));
            break;
        case 0x23D:
            val = ((cardkey->serial_number_str[1] - '0') << 4) | ((cardkey->serial_number_str[6] - '0'));
            break;
        case 0x23E:
            val = ((cardkey->serial_number_str[0] - '0') << 4) | ((cardkey->serial_number_str[7] - '0'));
            break;
        case 0x23F:
            val = ((cardkey->serial_number_str[3] - '0') << 4) | ((cardkey->serial_number_str[5] - '0'));
            break;
    }
    return val ^ 0xFF;
}

void* novell_cardkey_init(UNUSED(const device_t* info))
{
    char sernumstr[13] = { '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', 0 };
    int i = 0;
    novell_cardkey_t* cardkey = calloc(1, sizeof(novell_cardkey_t));

    strncpy(sernumstr, device_get_config_string("serial_number"), sizeof(sernumstr) - 1);
    
    for (i = 0; i < sizeof(sernumstr) - 4; i++) {
        if (sernumstr[i] > '8' || sernumstr[i] < '0')
            sernumstr[i] = '0';
    }
    if (sernumstr[8] > 'F' || sernumstr[8] < '0')
        sernumstr[8] = '0';
    if (sernumstr[9] > 'F' || sernumstr[9] < '0')
        sernumstr[9] = '0';
    if (sernumstr[10] > 'F' || sernumstr[10] < '0')
        sernumstr[10] = '0';
    if (sernumstr[11] > 'F' || sernumstr[11] < '0')
        sernumstr[11] = '0';
    sernumstr[12] = 0;
    strncpy(cardkey->serial_number_str, sernumstr, sizeof(sernumstr));
    io_sethandler(NOVELL_KEYCARD_ADDR, NOVELL_KEYCARD_ADDRLEN, novell_cardkey_read, NULL, NULL, NULL, NULL, NULL, cardkey);
    return cardkey;
}

void novell_cardkey_close(void* priv)
{
    free(priv);
}

static const device_config_t keycard_config[] = {
  // clang-format off
    {
        .name           = "serial_number",
        .description    = "Serial Number",
        .type           = CONFIG_STRING,
        .default_string = "",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t novell_keycard_device = {
    .name          = "Novell Netware 2.x Key Card",
    .internal_name = "mssystems",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = novell_cardkey_init,
    .close         = novell_cardkey_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = keycard_config
};
