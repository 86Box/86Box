/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of National Semiconductors NMC93Cxx EEPROMs.
 *
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2023 Cacodemon345
 */

/* Ported over from QEMU */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/net_eeprom_nmc93cxx.h>
#include <86box/plat_unused.h>

struct nmc93cxx_eeprom_t {
    uint8_t tick;
    uint8_t address;
    uint8_t command;
    uint8_t writable;

    uint8_t eecs;
    uint8_t eesk;
    uint8_t eedo;

    uint8_t  addrbits;
    uint16_t size;
    uint16_t data;
    char     filename[1024];
    uint16_t contents[];
};

typedef struct nmc93cxx_eeprom_t nmc93cxx_eeprom_t;

#ifdef ENABLE_NMC93CXX_EEPROM_LOG
int nmc93cxx_eeprom_do_log = ENABLE_NMC93CXX_EEPROM_LOG;

static void
nmc93cxx_eeprom_log(int lvl, const char *fmt, ...)
{
    va_list ap;

    if (nmc93cxx_eeprom_do_log >= lvl) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define nmc93cxx_eeprom_log(lvl, fmt, ...)
#endif

static const char *opstring[] = {
    "extended", "write", "read", "erase"
};

static void *
nmc93cxx_eeprom_init_params(UNUSED(const device_t *info), void *params)
{
    uint16_t                  nwords         = 64;
    uint8_t                   addrbits       = 6;
    uint8_t                   filldefault    = 1;
    nmc93cxx_eeprom_params_t *params_details = (nmc93cxx_eeprom_params_t *) params;
    nmc93cxx_eeprom_t        *eeprom         = NULL;
    if (!params)
        return NULL;

    nwords = params_details->nwords;

    switch (nwords) {
        case 16:
        case 64:
            addrbits = 6;
            break;
        case 128:
        case 256:
            addrbits = 8;
            break;
        default:
            nwords   = 64;
            addrbits = 6;
            break;
    }
    eeprom = calloc(1, sizeof(nmc93cxx_eeprom_t) + ((nwords + 1) * 2));
    if (!eeprom)
        return NULL;
    eeprom->size     = nwords;
    eeprom->addrbits = addrbits;
    /* Output DO is tristate, read results in 1. */
    eeprom->eedo = 1;

    if (params_details->filename) {
        FILE *fp = nvr_fopen(params_details->filename, "rb");
        strncpy(eeprom->filename, params_details->filename, 1024);
        if (fp) {
            filldefault = !fread(eeprom->contents, sizeof(uint16_t), nwords, fp);
            fclose(fp);
        }
    }

    if (filldefault) {
        memcpy(eeprom->contents, params_details->default_content, nwords * sizeof(uint16_t));
    }

    return eeprom;
}

void
nmc93cxx_eeprom_write(nmc93cxx_eeprom_t *eeprom, int eecs, int eesk, int eedi)
{
    uint8_t  tick    = eeprom->tick;
    uint8_t  eedo    = eeprom->eedo;
    uint16_t address = eeprom->address;
    uint8_t  command = eeprom->command;

    nmc93cxx_eeprom_log(1, "CS=%u SK=%u DI=%u DO=%u, tick = %u\n",
                        eecs, eesk, eedi, eedo, tick);

    if (!eeprom->eecs && eecs) {
        /* Start chip select cycle. */
        nmc93cxx_eeprom_log(1, "Cycle start, waiting for 1st start bit (0)\n");
        tick    = 0;
        command = 0x0;
        address = 0x0;
    } else if (eeprom->eecs && !eecs) {
        /* End chip select cycle. This triggers write / erase. */
        if (eeprom->writable) {
            uint8_t subcommand = address >> (eeprom->addrbits - 2);
            if (command == 0 && subcommand == 2) {
                /* Erase all. */
                for (address = 0; address < eeprom->size; address++) {
                    eeprom->contents[address] = 0xffff;
                }
            } else if (command == 3) {
                /* Erase word. */
                eeprom->contents[address] = 0xffff;
            } else if (tick >= 2 + 2 + eeprom->addrbits + 16) {
                if (command == 1) {
                    /* Write word. */
                    eeprom->contents[address] &= eeprom->data;
                } else if (command == 0 && subcommand == 1) {
                    /* Write all. */
                    for (address = 0; address < eeprom->size; address++) {
                        eeprom->contents[address] &= eeprom->data;
                    }
                }
            }
        }
        /* Output DO is tristate, read results in 1. */
        eedo = 1;
    } else if (eecs && !eeprom->eesk && eesk) {
        /* Raising edge of clock shifts data in. */
        if (tick == 0) {
            /* Wait for 1st start bit. */
            if (eedi == 0) {
                nmc93cxx_eeprom_log(1, "Got correct 1st start bit, waiting for 2nd start bit (1)\n");
                tick++;
            } else {
                nmc93cxx_eeprom_log(1, "wrong 1st start bit (is 1, should be 0)\n");
                tick = 2;
#if 0
                ~ assert(!"wrong start bit");
#endif
            }
        } else if (tick == 1) {
            /* Wait for 2nd start bit. */
            if (eedi != 0) {
                nmc93cxx_eeprom_log(1, "Got correct 2nd start bit, getting command + address\n");
                tick++;
            } else {
                nmc93cxx_eeprom_log(1, "1st start bit is longer than needed\n");
            }
        } else if (tick < 2 + 2) {
            /* Got 2 start bits, transfer 2 opcode bits. */
            tick++;
            command <<= 1;
            if (eedi) {
                command += 1;
            }
        } else if (tick < 2 + 2 + eeprom->addrbits) {
            /* Got 2 start bits and 2 opcode bits, transfer all address bits. */
            tick++;
            address = ((address << 1) | eedi);
            if (tick == 2 + 2 + eeprom->addrbits) {
                nmc93cxx_eeprom_log(1, "%s command, address = 0x%02x (value 0x%04x)\n",
                                    opstring[command], address, eeprom->contents[address]);
                if (command == 2) {
                    eedo = 0;
                }
                address = address % eeprom->size;
                if (command == 0) {
                    /* Command code in upper 2 bits of address. */
                    switch (address >> (eeprom->addrbits - 2)) {
                        case 0:
                            nmc93cxx_eeprom_log(1, "write disable command\n");
                            eeprom->writable = 0;
                            break;
                        case 1:
                            nmc93cxx_eeprom_log(1, "write all command\n");
                            break;
                        case 2:
                            nmc93cxx_eeprom_log(1, "erase all command\n");
                            break;
                        case 3:
                            nmc93cxx_eeprom_log(1, "write enable command\n");
                            eeprom->writable = 1;
                            break;

                        default:
                            break;
                    }
                } else {
                    /* Read, write or erase word. */
                    eeprom->data = eeprom->contents[address];
                }
            }
        } else if (tick < 2 + 2 + eeprom->addrbits + 16) {
            /* Transfer 16 data bits. */
            tick++;
            if (command == 2) {
                /* Read word. */
                eedo = ((eeprom->data & 0x8000) != 0);
            }
            eeprom->data <<= 1;
            eeprom->data += eedi;
        } else {
            nmc93cxx_eeprom_log(1, "additional unneeded tick, not processed\n");
        }
    }
    /* Save status of EEPROM. */
    eeprom->tick    = tick;
    eeprom->eecs    = eecs;
    eeprom->eesk    = eesk;
    eeprom->eedo    = eedo;
    eeprom->address = address;
    eeprom->command = command;
}

uint16_t
nmc93cxx_eeprom_read(nmc93cxx_eeprom_t *eeprom)
{
    /* Return status of pin DO (0 or 1). */
    return eeprom->eedo;
}

static void
nmc93cxx_eeprom_close(void *priv)
{
    nmc93cxx_eeprom_t *eeprom = (nmc93cxx_eeprom_t *) priv;
    FILE              *fp     = nvr_fopen(eeprom->filename, "wb");
    if (fp) {
        fwrite(eeprom->contents, 2, eeprom->size, fp);
        fclose(fp);
    }
    free(priv);
}

uint16_t *
nmc93cxx_eeprom_data(nmc93cxx_eeprom_t *eeprom)
{
    /* Get EEPROM data array. */
    return &eeprom->contents[0];
}

const device_t nmc93cxx_device = {
    .name          = "National Semiconductor NMC93Cxx",
    .internal_name = "nmc93cxx",
    .flags         = DEVICE_EXTPARAMS,
    .local         = 0,
    .init_ext      = nmc93cxx_eeprom_init_params,
    .close         = nmc93cxx_eeprom_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
