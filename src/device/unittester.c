/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Debug device for assisting in unit testing.
 *          See doc/specifications/86box-unit-tester.md for more info.
 *
 *
 *
 * Authors: GreaseMonkey, <thematrixeatsyou+86b@gmail.com>
 *
 *          Copyright 2024 GreaseMonkey.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/plat.h>
#include <86box/unittester.h>

enum fsm1_value {
    UT_FSM1_WAIT_8,
    UT_FSM1_WAIT_6,
    UT_FSM1_WAIT_B,
    UT_FSM1_WAIT_o,
    UT_FSM1_WAIT_x,
};
enum fsm2_value {
    UT_FSM2_IDLE,
    UT_FSM2_WAIT_IOBASE_0,
    UT_FSM2_WAIT_IOBASE_1,
};

/* Status bit mask */
#define UT_STATUS_AWAITING_READ   (1<<0)
#define UT_STATUS_AWAITING_WRITE  (1<<1)
#define UT_STATUS_IDLE            (1<<2)
#define UT_STATUS_UNSUPPORTED_CMD (1<<3)

/* Command list */
enum unittester_cmd {
    UT_CMD_NOOP = 0x00,
    UT_CMD_CAPTURE_SCREEN_SNAPSHOT = 0x01,
    UT_CMD_READ_SCREEN_SNAPSHOT_RECTANGLE = 0x02,
    UT_CMD_VERIFY_SCREEN_SNAPSHOT_RECTANGLE = 0x03,
    UT_CMD_EXIT = 0x04,
};

struct unittester_state {
    /* I/O port settings */
    uint16_t trigger_port;
    uint16_t iobase_port;

    /* Trigger port finite state machines */
    /* FSM1: "86Box" string detection */
    enum fsm1_value fsm1;
    /* FSM2: IOBASE port selection, once trigger is activated */
    enum fsm2_value fsm2;
    uint16_t        fsm2_new_iobase;

    /* Runtime state */
    uint8_t             status;
    enum unittester_cmd cmd_id;
    uint32_t            write_offs;
    uint32_t            read_offs;
    uint32_t            write_len;
    uint32_t            read_len;

    /* Command-specific state */
    /* 0x04: Exit */
    uint8_t exit_code;
};
static struct unittester_state unittester;
static const struct unittester_state unittester_defaults = {
    .trigger_port = 0x0080,
    .iobase_port  = 0xFFFF,
    .fsm1         = UT_FSM1_WAIT_8,
    .fsm2         = UT_FSM2_IDLE,
    .status       = UT_STATUS_IDLE,
    .cmd_id       = UT_CMD_NOOP,
};

/* FIXME: This needs a config option! --GM */
static bool unittester_exit_enabled = true;

/* FIXME TEMPORARY --GM */
#define ENABLE_UNITTESTER_LOG 1

#ifdef ENABLE_UNITTESTER_LOG
int unittester_do_log = ENABLE_UNITTESTER_LOG;

static void
unittester_log(const char *fmt, ...)
{
    va_list ap;

    if (unittester_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define unittester_log(fmt, ...)
#endif

static void
unittester_write(uint16_t port, uint8_t val, UNUSED(void *priv))
{
    if (port == unittester.iobase_port+0x00) {
        /* Command port */
        unittester_log("[UT] W %02X Command\n", val);

        unittester.write_offs = 0;
        unittester.write_len = 0;
        unittester.read_offs = 0;
        unittester.read_len = 0;

        switch (val) {
            /* 0x00: No-op */
            case UT_CMD_NOOP:
                unittester.cmd_id = UT_CMD_NOOP;
                unittester.status = UT_STATUS_IDLE;
                break;

            /* 0x04: Exit */
            case UT_CMD_EXIT:
                unittester.cmd_id = UT_CMD_EXIT;
                unittester.status = UT_STATUS_AWAITING_WRITE;
                unittester.write_len = 1;
                break;

            /* Unsupported command - terminate here */
            default:
                unittester.cmd_id = UT_CMD_NOOP;
                unittester.status = UT_STATUS_IDLE | UT_STATUS_UNSUPPORTED_CMD;
                break;
        }

    } else if (port == unittester.iobase_port+0x01) {
        /* Data port */
        unittester_log("[UT] W %02X Data\n", val);

        /* Skip if not awaiting */
        if ((unittester.status & UT_STATUS_AWAITING_WRITE) == 0)
            return;

        switch (unittester.cmd_id) {
            case UT_CMD_EXIT:
                switch(unittester.write_offs) {
                    case 0:
                        unittester.exit_code = val;
                        break;
                    default:
                        break;
                }
                break;

            /* This should not be reachable, but just in case... */
            default:
                break;
        }

        /* Advance write buffer */
        unittester.write_offs += 1;
        if (unittester.write_offs >= unittester.write_len) {
            unittester.status &= ~UT_STATUS_AWAITING_WRITE;
            /* Determine what we're doing here based on the command. */
            switch (unittester.cmd_id) {
                case UT_CMD_EXIT:
                    unittester_log("[UT] Exit received - code = %02X\n", unittester.exit_code);

                    /* CHECK: Do we actually exit? */
                    if (unittester_exit_enabled) {
                        /* Yes - call exit! */
                        /* Clamp exit code */
                        if (unittester.exit_code > 0x7F)
                            unittester.exit_code = 0x7F;

                        /* Exit somewhat quickly! */
                        unittester_log("[UT] Exit enabled, exiting with code %02X\n", unittester.exit_code);
                        exit(unittester.exit_code);

                    } else {
                        /* No - report successful command completion and continue program execution */
                        unittester_log("[UT] Exit disabled, continuing execution\n");
                        unittester.cmd_id = UT_CMD_NOOP;
                        unittester.status = UT_STATUS_IDLE;
                    }
                    break;

                default:
                    /* Nothing to write? Stop here. */
                    unittester.cmd_id = UT_CMD_NOOP;
                    unittester.status = UT_STATUS_IDLE;
                    break;
            }
        }

    } else {
        /* Not handled here - possibly open bus! */
    }
}

static uint8_t
unittester_read(uint16_t port, UNUSED(void *priv))
{
    uint8_t outval = 0xFF;

    if (port == unittester.iobase_port+0x00) {
        /* Status port */
        unittester_log("[UT] R -- Status = %02X\n", unittester.status);
        return unittester.status;
    } else if (port == unittester.iobase_port+0x01) {
        /* Data port */
        unittester_log("[UT] R -- Data\n");

        /* Skip if not awaiting */
        if ((unittester.status & UT_STATUS_AWAITING_READ) == 0)
            return 0xFF;

        switch (unittester.cmd_id) {
            /* This should not be reachable, but just in case... */
            default:
                break;
        }

        /* Advance read buffer */
        unittester.read_offs += 1;
        if (unittester.read_offs >= unittester.read_len) {
            /* Once fully read, we stop here. */
            unittester.cmd_id = UT_CMD_NOOP;
            unittester.status = UT_STATUS_IDLE;
        }

        return outval;
    } else {
        /* Not handled here - possibly open bus! */
        return 0xFF;
    }
}

static void
unittester_trigger_write(UNUSED(uint16_t port), uint8_t val, UNUSED(void *priv))
{
    /* This one gets quite spammy. */
    /* unittester_log("[UT] Trigger value %02X -> FSM1 = %02X, FSM2 = %02X, IOBASE = %04X\n", val, unittester.fsm1, unittester.fsm2, unittester.iobase_port); */

    /* Update FSM2 */
    switch (unittester.fsm2) {
        /* IDLE: Do nothing - FSM1 will put us in the right state. */
        case UT_FSM2_IDLE:
            unittester.fsm2 = UT_FSM2_IDLE;
            break;

        /* WAIT IOBASE 0: Set low byte of temporary IOBASE. */
        case UT_FSM2_WAIT_IOBASE_0:
            unittester.fsm2_new_iobase = ((uint16_t)val);
            unittester.fsm2 = UT_FSM2_WAIT_IOBASE_1;
            break;

        /* WAIT IOBASE 0: Set high byte of temporary IOBASE and commit to the real IOBASE. */
        case UT_FSM2_WAIT_IOBASE_1:
            unittester.fsm2_new_iobase |= ((uint16_t)val)<<8;

            unittester_log("[UT] Remapping IOBASE: %04X -> %04X\n", unittester.iobase_port, unittester.fsm2_new_iobase);

            /* Unmap old IOBASE */
            if (unittester.iobase_port != 0xFFFF)
                io_removehandler(unittester.iobase_port, 2, unittester_read, NULL, NULL, unittester_write, NULL, NULL, NULL);
            unittester.iobase_port = 0xFFFF;

            /* Map new IOBASE */
            unittester.iobase_port = unittester.fsm2_new_iobase;
            if (unittester.iobase_port != 0xFFFF)
                io_sethandler(unittester.iobase_port, 2, unittester_read, NULL, NULL, unittester_write, NULL, NULL, NULL);

            /* Reset FSM2 to IDLE */
            unittester.fsm2 = UT_FSM2_IDLE;
            break;
    }

    /* Update FSM1 */
    switch (val) {
        case '8':
            unittester.fsm1 = UT_FSM1_WAIT_6;
            break;
        case '6':
            if (unittester.fsm1 == UT_FSM1_WAIT_6)
                unittester.fsm1 = UT_FSM1_WAIT_B;
            else
                unittester.fsm1 = UT_FSM1_WAIT_8;
            break;
        case 'B':
            if (unittester.fsm1 == UT_FSM1_WAIT_B)
                unittester.fsm1 = UT_FSM1_WAIT_o;
            else
                unittester.fsm1 = UT_FSM1_WAIT_8;
            break;
        case 'o':
            if (unittester.fsm1 == UT_FSM1_WAIT_o)
                unittester.fsm1 = UT_FSM1_WAIT_x;
            else
                unittester.fsm1 = UT_FSM1_WAIT_8;
            break;
        case 'x':
            if (unittester.fsm1 == UT_FSM1_WAIT_x) {
                unittester_log("[UT] Config activated, awaiting new IOBASE\n");
                unittester.fsm2 = UT_FSM2_WAIT_IOBASE_0;
            }
            unittester.fsm1 = UT_FSM1_WAIT_8;
            break;

        default:
            unittester.fsm1 = UT_FSM1_WAIT_8;
            break;
    }
}

static void *
unittester_init(UNUSED(const device_t *info))
{
    unittester = (struct unittester_state)unittester_defaults;
    io_sethandler(unittester.trigger_port, 1, NULL, NULL, NULL, unittester_trigger_write, NULL, NULL, NULL);

    unittester_log("[UT] 86Box Unit Tester initialised\n");

    return &unittester;  /* Dummy non-NULL value */
}

static void
unittester_close(UNUSED(void *priv))
{
    io_removehandler(unittester.trigger_port, 1, NULL, NULL, NULL, unittester_trigger_write, NULL, NULL, NULL);

    if (unittester.iobase_port != 0xFFFF)
        io_removehandler(unittester.iobase_port, 2, unittester_read, NULL, NULL, unittester_write, NULL, NULL, NULL);
    unittester.iobase_port = 0xFFFF;

    unittester_log("[UT] 86Box Unit Tester closed\n");
}

const device_t unittester_device = {
    .name          = "86Box Unit Tester",
    .internal_name = "unittester",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = unittester_init,
    .close         = unittester_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
