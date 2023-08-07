/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          808x CPU emulation, mostly ported from reenigne's XTCE, which
 *          is cycle-accurate.
 *
 * Authors: gloriouscow, <https://github.com/dbalsom>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2023 gloriouscow.
 *          Copyright 2023 Miran Grca.
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include <86box/machine.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/ppi.h>
#include <86box/timer.h>
#include <86box/gdbstub.h>
// #include "808x.h"
#include "queue.h"

/* TODO: Move to cpu.h so this can eventually be reused for 286+ as well. */
#define QUEUE_MAX 6

typedef struct queue_t
{
    size_t        size;
    size_t        len;
    size_t        back;
    size_t        front;
    uint8_t       q[QUEUE_MAX];
    uint16_t      preload;
    queue_delay_t delay;
} queue_t;

static queue_t queue;

#ifdef ENABLE_QUEUE_LOG
int queue_do_log = ENABLE_QUEUE_LOG;

static void
queue_log(const char *fmt, ...)
{
    va_list ap;

    if (queue_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define queue_log(fmt, ...)
#endif

void
queue_set_size(size_t size)
{
    if (size > QUEUE_MAX)
        fatal("Requested prefetch queue of %i bytes is too big\n", size);

    queue.size = size;
}

size_t
queue_get_len(void)
{
    return queue.len;
}

int
queue_is_full(void)
{
    return (queue.len != queue.size);
}

uint16_t
queue_get_preload(void)
{
    uint16_t ret = queue.preload;
    queue.preload = 0x0000;

    return ret;
}

int
queue_has_preload(void)
{
    return (queue.preload & FLAG_PRELOADED) ? 1 : 0;
}

void
queue_set_preload(void)
{
    uint8_t byte;

    if (queue.len > 0) {
        byte = queue_pop();
        queue.preload = ((uint16_t) byte) | FLAG_PRELOADED;
    } else
        fatal("Tried to preload with empty queue\n");
}

void
queue_push8(uint8_t byte)
{
    if (queue.len < queue.size) {
        queue.q[queue.front] = byte;
        queue.front = (queue.front + 1) % queue.size;
        queue.len++;

        if (queue.len == 3)
            queue.delay = DELAY_WRITE;
        else
            queue.delay = DELAY_NONE;
    } else
        fatal("Queue overrun\n");
}

void
queue_push16(uint16_t word)
{
    queue_push8((uint8_t) (word & 0xff));
    queue_push8((uint8_t) ((word >> 8) & 0xff));
}

uint8_t
queue_pop(void)
{
    uint8_t byte = 0xff;

    if (queue.len > 0) {
        byte = queue.q[queue.back];

        queue.back = (queue.back + 1) % queue.size;
        queue.len--;

        if (queue.len >= 3)
            queue.delay = DELAY_READ;
        else
            queue.delay = DELAY_NONE;
    } else
        fatal("Queue underrun\n");

    return byte;
}

queue_delay_t
queue_get_delay(void)
{
    return queue.delay;
}

void
queue_flush(void)
{
    memset(&queue, 0x00, sizeof(queue_t));

    queue.delay = DELAY_NONE;
}

void
queue_init(void)
{
    queue_flush();

    if (is8086)
        queue_set_size(6);
    else
        queue_set_size(4);
}
