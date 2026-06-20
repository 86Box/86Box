/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of National Semiconductors NMC93Cxx EEPROMs (16 bits or 8 bits).
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2023 Cacodemon345
 */

/* Ported over from the MAME eepromser.cpp implementation. Copyright 2013 Aaron Giles */

#ifdef ENABLE_NMC93CXX_EEPROM_LOG
#include <stdarg.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <wchar.h>
#include <time.h>
#include <assert.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/log.h>
#include <86box/nmc93cxx.h>
#include <86box/plat_unused.h>

#define WRITE_TIME_US       1750
#define WRITE_ALL_TIME_US   8000
#define ERASE_TIME_US       1000
#define ERASE_ALL_TIME_US   8000

typedef enum EepromCommand {
    COMMAND_INVALID,
    COMMAND_READ,
    COMMAND_WRITE,
    COMMAND_ERASE,
    COMMAND_LOCK,
    COMMAND_UNLOCK,
    COMMAND_WRITEALL,
    COMMAND_ERASEALL,
    COMMAND_COPY_EEPROM_TO_RAM,
    COMMAND_COPY_RAM_TO_EEPROM
} EepromCommand;

typedef enum EepromState {
    STATE_IN_RESET,
    STATE_WAIT_FOR_START_BIT,
    STATE_WAIT_FOR_COMMAND,
    STATE_READING_DATA,
    STATE_WAIT_FOR_DATA,
    STATE_WAIT_FOR_COMPLETION
} EepromState;

typedef enum EepromEvent {
    EVENT_CS_RISING_EDGE = 1 << 0,
    EVENT_CS_FALLING_EDGE = 1 << 1,
    EVENT_CLK_RISING_EDGE = 1 << 2,
    EVENT_CLK_FALLING_EDGE = 1 << 3
} EepromEvent;

struct nmc93cxx_eeprom_t {
    /* Command completion timer */
    pc_timer_t cmd_complete_timer;
    /* Write tick */
    uint32_t write_tick;
    /* State of the CS line */
    bool cs_state;
    /* State of the CLK line */
    bool clk_state;
    /* State of the DI line */
    bool di_state;
    /* READY/BUSY status during a programming operation */
    bool is_busy;
    /* Internal device state */
    EepromState state;
    /* Accumulator of command+address bits */
    uint32_t command_address_accum;
    /* Current address extracted from command */
    uint32_t address;
    /* Holds data coming in/going out */
    uint32_t shift_register;
    /* Number of bits accumulated */
    uint32_t bits_accum;
    /* Current command */
    EepromCommand command;
    /* Number of memory cells */
    uint16_t cells;
    /* Number of bits per cell */
    uint16_t data_bits;
    /* Number of address bits in a command */
    uint8_t command_address_bits;
    /* Number of address bits in an address */
    uint8_t address_bits;
    /* Are we locked against writes? */
    bool is_locked;
    /* Tick of the last CS rising edge */
    uint32_t last_cs_rising_edge_tick;
    /* Device logging */
    void *log;
    /* Name of EEPROM image file */
    char filename[1024];

    /* EEPROM image words, must be the last structure member */
    uint16_t array_data[];
};

#ifdef ENABLE_NMC93CXX_EEPROM_LOG
int nmc93cxx_eeprom_do_log = ENABLE_NMC93CXX_EEPROM_LOG;

static void
nmc93cxx_eeprom_log(nmc93cxx_eeprom_t *dev, int lvl, const char *fmt, ...)
{
    va_list ap;

    if (nmc93cxx_eeprom_do_log >= lvl) {
        va_start(ap, fmt);
        log_out(dev->log, fmt, ap);
        va_end(ap);
    }
}

#define MAKE_CASE(x) case x: return #x;
static char*
nmc93cxx_eeprom_state_to_name(EepromState state)
{
    switch (state) {
        MAKE_CASE(STATE_IN_RESET);
        MAKE_CASE(STATE_WAIT_FOR_START_BIT);
        MAKE_CASE(STATE_WAIT_FOR_COMMAND);
        MAKE_CASE(STATE_READING_DATA);
        MAKE_CASE(STATE_WAIT_FOR_DATA);
        MAKE_CASE(STATE_WAIT_FOR_COMPLETION);
        default: return "<UNK>";
    }
}

static char *
nmc93cxx_eeprom_cmd_to_name(EepromCommand command)
{
    switch (command) {
        MAKE_CASE(COMMAND_INVALID);
        MAKE_CASE(COMMAND_READ);
        MAKE_CASE(COMMAND_WRITE);
        MAKE_CASE(COMMAND_ERASE);
        MAKE_CASE(COMMAND_LOCK);
        MAKE_CASE(COMMAND_UNLOCK);
        MAKE_CASE(COMMAND_WRITEALL);
        MAKE_CASE(COMMAND_ERASEALL);
        MAKE_CASE(COMMAND_COPY_EEPROM_TO_RAM);
        MAKE_CASE(COMMAND_COPY_RAM_TO_EEPROM);
        default: return "<UNK>";
    }
}
#undef MAKE_CASE
#else
#    define nmc93cxx_eeprom_log(dev, lvl, fmt, ...)
#endif

static void
nmc93cxx_eeprom_set_state(nmc93cxx_eeprom_t *dev, EepromState state)
{
    if (dev->state != state) {
        nmc93cxx_eeprom_log(dev, 2, "EEPROM: New state %s\n", nmc93cxx_eeprom_state_to_name(state));
    }
    dev->state = state;
}

static void
nmc93cxx_eeprom_cmd_complete_timer_callback(void *priv)
{
    nmc93cxx_eeprom_t *dev = priv;

    dev->is_busy = false;
}

static void
nmc93cxx_eeprom_start_cmd_timer(nmc93cxx_eeprom_t *dev, double period)
{
    dev->is_busy = true;
    timer_on_auto(&dev->cmd_complete_timer, period);
}

static void *
nmc93cxx_eeprom_init(const device_t *info)
{
    nmc93cxx_eeprom_params_t *params_details = (nmc93cxx_eeprom_params_t *) info->local;
    nmc93cxx_eeprom_t *dev;
    bool fill_default = true;
    uint16_t cells, nwords, data_bits;
    uint8_t addrbits;

    /* Check for mandatory parameters */
    assert(params_details != NULL);
    assert(params_details->filename != NULL);

    switch (params_details->type) {
        /* 16-bit EEPROMs */
        case NMC_93C06_x16_16:
            cells = 16;
            addrbits = 6;
            data_bits = 16;
            break;
        case NMC_93C46_x16_64:
            cells = 64;
            addrbits = 6;
            data_bits = 16;
            break;
        case NMC_93C56_x16_128:
            cells = 128;
            addrbits = 8;
            data_bits = 16;
            break;
        case NMC_93C57_x16_128:
            cells = 128;
            addrbits = 7;
            data_bits = 16;
            break;
        case NMC_93C66_x16_256:
            cells = 256;
            addrbits = 8;
            data_bits = 16;
            break;
        case NMC_93C76_x16_512:
            cells = 512;
            addrbits = 10;
            data_bits = 16;
            break;
        case NMC_93C86_x16_1024:
            cells = 1024;
            addrbits = 10;
            data_bits = 16;
            break;

        /* 8-bit EEPROMs */
        case NMC_93C46_x8_128:
            cells = 128;
            addrbits = 7;
            data_bits = 8;
            break;
        case NMC_93C56_x8_256:
            cells = 256;
            addrbits = 9;
            data_bits = 8;
            break;
        case NMC_93C57_x8_256:
            cells = 256;
            addrbits = 8;
            data_bits = 8;
            break;
        case NMC_93C66_x8_512:
            cells = 512;
            addrbits = 9;
            data_bits = 8;
            break;
        case NMC_93C76_x8_1024:
            cells = 1024;
            addrbits = 11;
            data_bits = 8;
            break;
        case NMC_93C86_x8_2048:
            cells = 1024;
            addrbits = 11;
            data_bits = 8;
            break;

        default:
            /* Invalid parameter passed to the device */
            assert(false);
            break;
    }

    /* The "ORG" pin can select the x8 or x16 memory organization */
    if (data_bits == 16) {
        nwords = cells;
    } else {
        nwords = cells / 2;

        assert(data_bits == 8);
    }

    dev = calloc(1, offsetof(nmc93cxx_eeprom_t, array_data[nwords]));
    if (!dev)
        return NULL;
    dev->cells = cells;
    dev->command_address_bits = addrbits;
    dev->data_bits = data_bits;
    dev->is_locked = true;
    dev->command = COMMAND_INVALID;
    dev->log = log_open("nmc93cxx");
    nmc93cxx_eeprom_set_state(dev, STATE_IN_RESET);

    /* Load the EEPROM image */
    snprintf(dev->filename, sizeof(dev->filename), "%s", params_details->filename);
    FILE *fp = nvr_fopen(dev->filename, "rb");
    if (fp) {
        fill_default = !fread(dev->array_data, (size_t) dev->data_bits / 8, dev->cells, fp);
        fclose(fp);
    }
    if (fill_default && params_details->default_content) {
        memcpy(dev->array_data, params_details->default_content, (size_t) dev->cells * ((size_t) dev->data_bits / 8));
    }

    /* Compute address bits */
    uint32_t count = dev->cells - 1;
    dev->address_bits = 0;
    while (count != 0) {
        count >>= 1;
        dev->address_bits++;
    }

    timer_add(&dev->cmd_complete_timer, nmc93cxx_eeprom_cmd_complete_timer_callback, dev, 0);

    return dev;
}

static uint32_t
nmc93cxx_eeprom_cell_read(nmc93cxx_eeprom_t *dev, uint32_t address)
{
    if (dev->data_bits == 16) {
        return dev->array_data[address];
    } else {
        return *((uint8_t *)dev->array_data + address);
    }
}

static void
nmc93cxx_eeprom_cell_write(nmc93cxx_eeprom_t *dev, uint32_t address, uint32_t data)
{
    if (dev->data_bits == 16) {
        dev->array_data[address] = data;
    } else {
        *((uint8_t *)dev->array_data + address) = data;
    }
}

static void
nmc93cxx_eeprom_handle_cmd_write(nmc93cxx_eeprom_t *dev, uint32_t address, uint32_t data)
{
    nmc93cxx_eeprom_log(dev, 1, "EEPROM: WR %08lX <-- %X\n", address, data);

    nmc93cxx_eeprom_cell_write(dev, address, data);
    nmc93cxx_eeprom_start_cmd_timer(dev, WRITE_TIME_US);
}

static void
nmc93cxx_eeprom_handle_cmd_write_all(nmc93cxx_eeprom_t *dev, uint32_t data)
{
    nmc93cxx_eeprom_log(dev, 1, "EEPROM: Write all operation %X\n", data);

    for (uint32_t address = 0; address < (1 << dev->address_bits); ++address) {
        nmc93cxx_eeprom_cell_write(dev, address, nmc93cxx_eeprom_cell_read(dev, address) & data);
    }

    nmc93cxx_eeprom_start_cmd_timer(dev, WRITE_ALL_TIME_US);
}

static void
nmc93cxx_eeprom_handle_cmd_erase(nmc93cxx_eeprom_t *dev, uint32_t address)
{
    nmc93cxx_eeprom_log(dev, 1, "EEPROM: Erase data at %08lx\n", address);

    nmc93cxx_eeprom_cell_write(dev, address, ~0);
    nmc93cxx_eeprom_start_cmd_timer(dev, ERASE_TIME_US);
}

static void
nmc93cxx_eeprom_handle_cmd_erase_all(nmc93cxx_eeprom_t *dev)
{
    nmc93cxx_eeprom_log(dev, 1, "EEPROM: Erase all operation\n");

    for (uint32_t address = 0; address < (1 << dev->address_bits); ++address) {
        nmc93cxx_eeprom_cell_write(dev, address, ~0);
    }

    nmc93cxx_eeprom_start_cmd_timer(dev, ERASE_ALL_TIME_US);
}

static void
nmc93cxx_eeprom_parse_command_and_address(nmc93cxx_eeprom_t *dev)
{
    dev->address = dev->command_address_accum & ((1 << dev->command_address_bits) - 1);

    /* Extract the command portion and handle it */
    switch (dev->command_address_accum >> dev->command_address_bits) {
        /* Opcode 0 needs two more bits to decode the operation */
        case 0:
            switch (dev->address >> (dev->command_address_bits - 2)) {
                case 0: dev->command = COMMAND_LOCK;       break;
                case 1: dev->command = COMMAND_WRITEALL;   break;
                case 2: dev->command = COMMAND_ERASEALL;   break;
                case 3: dev->command = COMMAND_UNLOCK;     break;
            }
            dev->address = 0;
            break;

        case 1: dev->command = COMMAND_WRITE;  break;
        case 2: dev->command = COMMAND_READ;   break;
        case 3: dev->command = COMMAND_ERASE;  break;

        default:
            dev->command = COMMAND_INVALID;
            break;
    }

    if (dev->address >= (1 << dev->address_bits)) {
        nmc93cxx_eeprom_log(dev, 1, "EEPROM: out-of-range address 0x%X provided (maximum should be 0x%X)\n", dev->address, (1 << dev->address_bits) - 1);
    }
}

static void
nmc93cxx_eeprom_execute_command(nmc93cxx_eeprom_t *dev)
{
    /* Parse into a generic command and reset the accumulator count */
    nmc93cxx_eeprom_parse_command_and_address(dev);
    dev->bits_accum = 0;

    nmc93cxx_eeprom_log(dev, 1, "EEPROM: Execute command %s\n", nmc93cxx_eeprom_cmd_to_name(dev->command));
    switch (dev->command) {
        /*
         * Advance to the READING_DATA state; data is fetched after first CLK
         * reset the shift register to 0 to simulate the dummy 0 bit that happens prior
         * to the first clock
         */
        case COMMAND_READ:
            dev->shift_register = 0;
            nmc93cxx_eeprom_set_state(dev, STATE_READING_DATA);
            break;

        /* Reset the shift register and wait for enough data to be clocked through */
        case COMMAND_WRITE:
        case COMMAND_WRITEALL:
            dev->shift_register = 0;
            nmc93cxx_eeprom_set_state(dev, STATE_WAIT_FOR_DATA);
            break;

        /* Erase the parsed address (unless locked) and wait for it to complete */
        case COMMAND_ERASE:
            if (dev->is_locked) {
                nmc93cxx_eeprom_log(dev, 1, "EEPROM: Attempt to erase while locked\n");
                nmc93cxx_eeprom_set_state(dev, STATE_IN_RESET);
                break;
            }
            nmc93cxx_eeprom_handle_cmd_erase(dev, dev->address);
            nmc93cxx_eeprom_set_state(dev, STATE_WAIT_FOR_COMPLETION);
            break;

        /* Lock the chip; return to IN_RESET state */
        case COMMAND_LOCK:
            dev->is_locked = true;
            nmc93cxx_eeprom_set_state(dev, STATE_IN_RESET);
            break;

        /* Unlock the chip; return to IN_RESET state */
        case COMMAND_UNLOCK:
            dev->is_locked = false;
            nmc93cxx_eeprom_set_state(dev, STATE_IN_RESET);
            break;

        /* Erase the entire chip (unless locked) and wait for it to complete */
        case COMMAND_ERASEALL:
            if (dev->is_locked) {
                nmc93cxx_eeprom_log(dev, 1, "EEPROM: Attempt to erase all while locked\n");
                nmc93cxx_eeprom_set_state(dev, STATE_IN_RESET);
                break;
            }
            nmc93cxx_eeprom_handle_cmd_erase_all(dev);
            nmc93cxx_eeprom_set_state(dev, STATE_WAIT_FOR_COMPLETION);
            break;

        default:
            nmc93cxx_eeprom_log(dev, 1, "execute_command called with invalid command %d\n", dev->command);
            assert(false);
            break;
    }
}

static void
nmc93cxx_eeprom_execute_write_command(nmc93cxx_eeprom_t *dev)
{
    nmc93cxx_eeprom_log(dev, 1, "EEPROM: Execute write command %s\n", nmc93cxx_eeprom_cmd_to_name(dev->command));

    switch (dev->command) {
        /* Reset the shift register and wait for enough data to be clocked through */
        case COMMAND_WRITE:
            if (dev->is_locked) {
                nmc93cxx_eeprom_log(dev, 1, "EEPROM: Attempt to write to address 0x%X while locked\n", dev->address);
                nmc93cxx_eeprom_set_state(dev, STATE_IN_RESET);
                break;
            }
            nmc93cxx_eeprom_handle_cmd_write(dev, dev->address, dev->shift_register);
            nmc93cxx_eeprom_set_state(dev, STATE_WAIT_FOR_COMPLETION);
            break;

        /*
         * Write the entire EEPROM with the same data; ERASEALL is required before so we
         * AND against the already-present data
         */
        case COMMAND_WRITEALL:
            if (dev->is_locked) {
                nmc93cxx_eeprom_log(dev, 1, "EEPROM: Attempt to write all while locked\n");
                nmc93cxx_eeprom_set_state(dev, STATE_IN_RESET);
                break;
            }
            nmc93cxx_eeprom_handle_cmd_write_all(dev, dev->shift_register);
            nmc93cxx_eeprom_set_state(dev, STATE_WAIT_FOR_COMPLETION);
            break;

        default:
            nmc93cxx_eeprom_log(dev, 1, "execute_command called with invalid command %d\n", dev->command);
            assert(false);
            break;
    }
}

static void
nmc93cxx_eeprom_handle_event(nmc93cxx_eeprom_t *dev, EepromEvent event)
{
    switch (dev->state) {
        /* CS is not asserted; wait for a rising CS to move us forward, ignoring all clocks */
        case STATE_IN_RESET:
            if (event == EVENT_CS_RISING_EDGE)
                nmc93cxx_eeprom_set_state(dev, STATE_WAIT_FOR_START_BIT);
            break;

        /*
         * CS is asserted; wait for rising clock with a 1 start bit; falling CS will reset us
         * note that because each bit is written independently, it is possible for us to receive
         * a false rising CLK edge at the exact same time as a rising CS edge; it appears we
         * should ignore these edges (makes sense really)
         */
        case STATE_WAIT_FOR_START_BIT:
            if ((event == EVENT_CLK_RISING_EDGE) && dev->di_state && !dev->is_busy && (dev->write_tick != dev->last_cs_rising_edge_tick)) {
                dev->command_address_accum = 0;
                dev->bits_accum = 0;
                nmc93cxx_eeprom_set_state(dev, STATE_WAIT_FOR_COMMAND);
            } else if (event == EVENT_CS_FALLING_EDGE)
                nmc93cxx_eeprom_set_state(dev, STATE_IN_RESET);
            break;

        /* CS is asserted; wait for a command to come through; falling CS will reset us */
        case STATE_WAIT_FOR_COMMAND:
            if (event == EVENT_CLK_RISING_EDGE) {
                /* If we have enough bits for a command + address, check it out */
                dev->command_address_accum = (dev->command_address_accum << 1) | dev->di_state;
                if (++dev->bits_accum == 2 + dev->command_address_bits)
                    nmc93cxx_eeprom_execute_command(dev);
            } else if (event == EVENT_CS_FALLING_EDGE)
                nmc93cxx_eeprom_set_state(dev, STATE_IN_RESET);
            break;

        /* CS is asserted; reading data, clock the shift register; falling CS will reset us */
        case STATE_READING_DATA:
            if (event == EVENT_CLK_RISING_EDGE) {
                uint32_t bit_index = dev->bits_accum++;

                /* Wrapping the address on multi-read */
                if (((bit_index % dev->data_bits) == 0) && (bit_index == 0))
                {
                    uint32_t addr = (dev->address + dev->bits_accum / dev->data_bits) & ((1 << dev->address_bits) - 1);
                    uint32_t data = nmc93cxx_eeprom_cell_read(dev, addr);

                    nmc93cxx_eeprom_log(dev, 1, "EEPROM: RD %08lX --> %X\n", addr, data);

                    dev->shift_register = data << (32 - dev->data_bits);
                } else {
                    dev->shift_register = (dev->shift_register << 1) | 1;
                }
            } else if (event == EVENT_CS_FALLING_EDGE) {
                nmc93cxx_eeprom_set_state(dev, STATE_IN_RESET);

                if (dev->bits_accum > (dev->data_bits + 1))
                    nmc93cxx_eeprom_log(dev, 1, "EEPROM: Overclocked read by %d bits\n", dev->bits_accum - dev->data_bits);
                else if (dev->bits_accum < dev->data_bits)
                    nmc93cxx_eeprom_log(dev, 1, "EEPROM: CS deasserted in READING_DATA after %d bits\n", dev->bits_accum);
            }
            break;

        /* CS is asserted; waiting for data; clock data through until we accumulate enough; falling CS will reset us */
        case STATE_WAIT_FOR_DATA:
            if (event == EVENT_CLK_RISING_EDGE) {
                dev->shift_register = (dev->shift_register << 1) | dev->di_state;
                if (++dev->bits_accum == dev->data_bits)
                    nmc93cxx_eeprom_execute_write_command(dev);
            } else if (event == EVENT_CS_FALLING_EDGE) {
                nmc93cxx_eeprom_set_state(dev, STATE_IN_RESET);
                nmc93cxx_eeprom_log(dev, 1, "EEPROM: CS deasserted in STATE_WAIT_FOR_DATA after %d bits\n", dev->bits_accum);
            }
            break;

        /* CS is asserted; waiting for completion; watch for CS falling */
        case STATE_WAIT_FOR_COMPLETION:
            if (event == EVENT_CS_FALLING_EDGE)
                nmc93cxx_eeprom_set_state(dev, STATE_IN_RESET);
            break;

        default:
            break;
    }
}

void
nmc93cxx_eeprom_write(nmc93cxx_eeprom_t *dev, bool eecs, bool eesk, bool eedi)
{
    assert(dev != NULL);

    dev->write_tick++;

    nmc93cxx_eeprom_log(dev, 3, "EEPROM: CS=%u SK=%u DI=%u DO=%u, tick = %u\n",
                        eecs, eesk, eedi, nmc93cxx_eeprom_read(dev), dev->write_tick);

    if (dev->cs_state != eecs) {
        dev->cs_state = eecs;

        /* Remember the rising edge tick so we don't process CLK signals at the same time */
        if (eecs) {
            dev->last_cs_rising_edge_tick = dev->write_tick;
        }

        nmc93cxx_eeprom_handle_event(dev, eecs ? EVENT_CS_RISING_EDGE : EVENT_CS_FALLING_EDGE);
    }

    dev->di_state = eedi;

    if (dev->clk_state != eesk) {
        dev->clk_state = eesk;
        nmc93cxx_eeprom_handle_event(dev, eesk ? EVENT_CLK_RISING_EDGE : EVENT_CLK_FALLING_EDGE);
    }
}

bool
nmc93cxx_eeprom_read(nmc93cxx_eeprom_t *dev)
{
    assert(dev != NULL);

    if (dev->state == STATE_WAIT_FOR_START_BIT) {
        /* Read the state of the READY/BUSY line */
        return !dev->is_busy;
    } else if (dev->state == STATE_READING_DATA) {
        /* Read the current output bit */
        return ((dev->shift_register & 0x80000000) != 0);
    }

    /* The DO pin is tristated */
    return true;
}

static void
nmc93cxx_eeprom_close(void *priv)
{
    nmc93cxx_eeprom_t *dev = priv;
    FILE              *fp  = nvr_fopen(dev->filename, "wb");
    if (fp) {
        fwrite(dev->array_data, dev->data_bits / 8, dev->cells, fp);
        fclose(fp);
    }
    log_close(dev->log);
    free(dev);
}

const uint16_t *
nmc93cxx_eeprom_data(nmc93cxx_eeprom_t *dev)
{
    assert(dev != NULL);

    /* Get EEPROM data array. */
    return &dev->array_data[0];
}

const device_t nmc93cxx_device = {
    .name          = "National Semiconductor NMC93Cxx",
    .internal_name = "nmc93cxx",
    .flags         = 0,
    .local         = 0,
    .init          = nmc93cxx_eeprom_init,
    .close         = nmc93cxx_eeprom_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
