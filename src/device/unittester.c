/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Debug device for assisting in unit testing.
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
#include <86box/plat.h>
#include <86box/unittester.h>

static uint16_t unittester_trigger_port = 0x0080;
static uint16_t unittester_base_port    = 0xFFFF;

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

static void *
unittester_init(UNUSED(const device_t *info))
{
    //io_sethandler(unittester_trigger_port, 1, NULL, NULL, NULL, unittester_write, NULL, NULL, NULL);
    unittester_log("[UT] 86Box Unit Tester initialised\n");

    return &unittester_trigger_port;  /* Dummy non-NULL value */
}

static void
unittester_close(UNUSED(void *priv))
{
    //io_removehandler(unittester_trigger_port, 1, NULL, NULL, NULL, unittester_write, NULL, NULL, NULL);
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
