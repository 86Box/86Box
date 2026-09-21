/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CLAP event list builder implementation.
 *
 *          Ported from DOSBox Staging (C++ → C).
 *
 * Authors: The DOSBox Staging Team (original C++ implementation)
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Original Copyright 2024-2025 The DOSBox Staging Team.
 *          86Box adaptation 2026.
 *          Copyright 2026 Jasmine Iwanek.
 */
#include <stdlib.h>
#include <string.h>
#include "clap_event_list.h"

/* ------------------------------------------------------------------ */
/*  Static callbacks wired into clap_input_events / clap_output_events */
/* ------------------------------------------------------------------ */
static uint32_t
evt_size_cb(const clap_input_events_t *in)
{
    const clap_event_list_t *el = (const clap_event_list_t *)in->ctx;
    return clap_event_list_size(el);
}

static const clap_event_header_t *
evt_get_cb(const clap_input_events_t *in, uint32_t index)
{
    const clap_event_list_t *el = (const clap_event_list_t *)in->ctx;
    return clap_event_list_get(el, index);
}

static int
evt_try_push_cb(const clap_output_events_t *out, const clap_event_header_t *event)
{
    (void) out;
    (void) event;
    return 0; /* output events not supported */
}

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                         */
/* ------------------------------------------------------------------ */
void
clap_event_list_init(clap_event_list_t *el)
{
    memset(el, 0, sizeof(*el));

    el->event_data_cap   = CLAP_EVT_INITIAL_BYTES;
    el->event_data       = (uint8_t *)calloc(1, el->event_data_cap);

    el->event_offsets_cap = CLAP_EVT_INITIAL_EVENTS;
    el->event_offsets     = (size_t *)calloc(el->event_offsets_cap, sizeof(size_t));

    el->sysex_data_cap   = CLAP_EVT_MAX_SYSEX_BYTES;
    el->sysex_data       = (uint8_t *)calloc(1, el->sysex_data_cap);

    el->input_events.ctx  = (void *)el;
    el->input_events.size = evt_size_cb;
    el->input_events.get  = evt_get_cb;

    el->output_events.ctx      = (void *)el;
    el->output_events.try_push = evt_try_push_cb;
}

void
clap_event_list_free(clap_event_list_t *el)
{
    free(el->event_data);
    free(el->event_offsets);
    free(el->sysex_data);
    memset(el, 0, sizeof(*el));
}

void
clap_event_list_clear(clap_event_list_t *el)
{
    el->event_data_size = 0;
    el->event_count     = 0;
    el->sysex_data_size = 0;
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */
static void
ensure_event_data(clap_event_list_t *el, size_t additional)
{
    while (el->event_data_size + additional > el->event_data_cap) {
        el->event_data_cap *= 2;
        el->event_data = (uint8_t *)realloc(el->event_data, el->event_data_cap);
    }
}

static void
ensure_event_offsets(clap_event_list_t *el)
{
    if (el->event_count >= el->event_offsets_cap) {
        el->event_offsets_cap *= 2;
        el->event_offsets = (size_t *)realloc(el->event_offsets,
                                              el->event_offsets_cap * sizeof(size_t));
    }
}

/* ------------------------------------------------------------------ */
/*  Add events                                                        */
/* ------------------------------------------------------------------ */
void
clap_event_list_add_midi(clap_event_list_t *el,
                         const uint8_t *msg, int msg_len,
                         uint32_t sample_offset)
{
    clap_event_midi_t ev;
    memset(&ev, 0, sizeof(ev));

    ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.header.type     = CLAP_EVENT_MIDI;
    ev.header.time     = sample_offset;
    ev.header.flags    = 0;
    ev.header.size     = sizeof(ev);
    ev.port_index      = 0;
    ev.data[0]         = msg[0];
    ev.data[1]         = (msg_len >= 2) ? msg[1] : 0;
    ev.data[2]         = (msg_len >= 3) ? msg[2] : 0;

    ensure_event_data(el, sizeof(ev));
    ensure_event_offsets(el);

    el->event_offsets[el->event_count++] = el->event_data_size;
    memcpy(el->event_data + el->event_data_size, &ev, sizeof(ev));
    el->event_data_size += sizeof(ev);
}

void
clap_event_list_add_sysex(clap_event_list_t *el,
                          const uint8_t *data, uint32_t len,
                          uint32_t sample_offset)
{
    clap_event_midi_sysex_t ev;
    memset(&ev, 0, sizeof(ev));

    ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.header.type     = CLAP_EVENT_MIDI_SYSEX;
    ev.header.time     = sample_offset;
    ev.header.flags    = 0;
    ev.header.size     = sizeof(ev);
    ev.port_index      = 0;

    /* Append sysex payload into the sysex_data pool */
    if (el->sysex_data_size + len > el->sysex_data_cap) {
        el->sysex_data_cap = (el->sysex_data_size + len) * 2;
        el->sysex_data = (uint8_t *)realloc(el->sysex_data, el->sysex_data_cap);
    }
    ev.buffer = el->sysex_data + el->sysex_data_size;
    ev.size   = len;
    memcpy(el->sysex_data + el->sysex_data_size, data, len);
    el->sysex_data_size += len;

    ensure_event_data(el, sizeof(ev));
    ensure_event_offsets(el);

    el->event_offsets[el->event_count++] = el->event_data_size;
    memcpy(el->event_data + el->event_data_size, &ev, sizeof(ev));
    el->event_data_size += sizeof(ev);
}

/* ------------------------------------------------------------------ */
/*  Query                                                             */
/* ------------------------------------------------------------------ */
uint32_t
clap_event_list_size(const clap_event_list_t *el)
{
    return (uint32_t)el->event_count;
}

const clap_event_header_t *
clap_event_list_get(const clap_event_list_t *el, uint32_t index)
{
    if (index < el->event_count) {
        return (const clap_event_header_t *)(el->event_data + el->event_offsets[index]);
    }
    return NULL;
}

const clap_input_events_t *
clap_event_list_get_input(const clap_event_list_t *el)
{
    return &el->input_events;
}

const clap_output_events_t *
clap_event_list_get_output(const clap_event_list_t *el)
{
    return &el->output_events;
}
