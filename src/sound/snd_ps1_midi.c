/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          PS/1 MIDI emulation.
 *
 * Authors: Cacodemon345,
 *
 *          Copyright 2026 Cacodemon345.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/sound.h>
#include <86box/midi.h>
#include <86box/fifo8.h>
#include <86box/timer.h>
#include <86box/plat_unused.h>

typedef struct ps1midi_t {
    uint8_t    status;
    uint8_t    ctrl;
    //uint8_t    buffer[2048];
    Fifo8      fifo;
    int        irq_pend;
} ps1midi_t;

static int
ps1midi_in_sysex(void *priv, uint8_t *buffer, uint32_t len, int abort)
{
    ps1midi_t *ps1midi = priv;

    for (int i = 0; i < len; i++) {
        //ps1midi->buffer[ps1midi->pos++] = buffer[i];
        fifo8_push(&ps1midi->fifo, buffer[i]);
    }
    if ((ps1midi->ctrl & 1)) {
        ps1midi->irq_pend = 1;
        picint(1 << 7);
    }
    return 0;
}

static void
ps1midi_rx(void *priv, uint8_t* bytes, uint32_t size)
{
    ps1midi_t *ps1midi = priv;

    for (int i = 0; i < size; i++) {
        fifo8_push(&ps1midi->fifo, bytes[i]);
    }
    if ((ps1midi->ctrl & 1)) {
        ps1midi->irq_pend = 1;
        picint(1 << 7);
    }
}

static uint8_t
ps1midi_read(uint16_t addr, void *priv)
{
    ps1midi_t *ps1midi = priv;

    switch (addr & 0x7) {
        case 0:
            return fifo8_pop(&ps1midi->fifo);
        case 1:
            return ps1midi->ctrl;
        case 2:
            return !!ps1midi->irq_pend;
        case 5:
            return !fifo8_is_empty(&ps1midi->fifo) | (1 << 5);
    }
    return 0xFF;
}

static void
ps1midi_write(uint16_t addr, uint8_t val, void *priv)
{
    ps1midi_t *ps1midi = priv;

    switch (addr & 0x7) {
        case 0:
            if (ps1midi->ctrl & (1 << 4)) {
                ps1midi_rx(priv, &val, 1);
            } else {
                midi_raw_out_byte(val);
                if (ps1midi->ctrl & 2) {
                    ps1midi->irq_pend = 1;
                    picint(1 << 7);
                }
            }
            break;
        case 1:
            ps1midi->ctrl = val;
            break;
        case 2:
            ps1midi->irq_pend &= ~(val & 1);
            picintc(1 << 7);
            break;
        case 5:
            break;
    }
}

static void *
ps1midi_init(UNUSED(const device_t *info))
{
    ps1midi_t *ps1midi = calloc(1, sizeof(ps1midi_t));

    io_sethandler(0x330, 3, ps1midi_read, NULL, NULL, ps1midi_write, NULL, NULL, ps1midi);
    io_sethandler(0x335, 1, ps1midi_read, NULL, NULL, ps1midi_write, NULL, NULL, ps1midi);

    fifo8_create(&ps1midi->fifo, 2048);

    midi_in_handler(1, ps1midi_rx, ps1midi_in_sysex, ps1midi);

    return ps1midi;
}

static void
ps1midi_close(void *priv)
{
    free(priv);
}

const device_t ps1midi_device = {
    .name          = "IBM PS/1 Audio Card (MIDI)",
    .internal_name = "ps1midi",
    .flags         = 0,
    .local         = 0,
    .init          = ps1midi_init,
    .close         = ps1midi_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
