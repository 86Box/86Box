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
#include <stdint.h>
#include <stdio.h>
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
};
static struct unittester_state unittester;
static const struct unittester_state unittester_defaults = {
    .trigger_port = 0x0080,
    .iobase_port  = 0xFFFF,
    .fsm1         = UT_FSM1_WAIT_8,
    .fsm2         = UT_FSM2_IDLE,
};

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
        /* TODO! --GM */
        unittester_log("[UT] W %02X Command\n", val);
    } else if (port == unittester.iobase_port+0x01) {
        /* Data port */
        /* TODO! --GM */
        unittester_log("[UT] W %02X Data\n", val);
    } else {
        /* Not handled here - possibly open bus! */
    }
}

static uint8_t
unittester_read(uint16_t port, UNUSED(void *priv))
{
    if (port == unittester.iobase_port+0x00) {
        /* Status port */
        /* TODO! --GM */
        unittester_log("[UT] R -- Status\n");
        return 0x04;
    } else if (port == unittester.iobase_port+0x01) {
        /* Data port */
        /* TODO! --GM */
        unittester_log("[UT] R -- Data\n");
        return 0xFE;
    } else {
        /* Not handled here - possibly open bus! */
        return 0xFF;
    }
}

static void
unittester_trigger_write(UNUSED(uint16_t port), uint8_t val, UNUSED(void *priv))
{
    unittester_log("[UT] Trigger value %02X -> FSM1 = %02X, FSM2 = %02X, IOBASE = %04X\n", val, unittester.fsm1, unittester.fsm2, unittester.iobase_port);

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

            unittester_log("[UT] Remapping IOBASE: %04X -> %04X\n", val, unittester.iobase_port, unittester.fsm2_new_iobase);

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
