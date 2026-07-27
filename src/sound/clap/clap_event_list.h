/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CLAP event list builder — accumulates MIDI events in the
 *          format expected by the CLAP process() call.
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
#ifndef EMU_CLAP_EVENT_LIST_H
#define EMU_CLAP_EVENT_LIST_H

#include "clap_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLAP_EVT_INITIAL_BYTES    8192
#define CLAP_EVT_INITIAL_EVENTS   1024
#define CLAP_EVT_MAX_SYSEX_BYTES  8192

typedef struct clap_event_list_t {
    uint8_t  *event_data;
    size_t    event_data_size;
    size_t    event_data_cap;

    size_t   *event_offsets;
    size_t    event_count;
    size_t    event_offsets_cap;

    uint8_t  *sysex_data;
    size_t    sysex_data_size;
    size_t    sysex_data_cap;

    clap_input_events_t  input_events;
    clap_output_events_t output_events;
} clap_event_list_t;

void clap_event_list_init(clap_event_list_t *el);
void clap_event_list_free(clap_event_list_t *el);
void clap_event_list_clear(clap_event_list_t *el);

void clap_event_list_add_midi(clap_event_list_t *el,
                              const uint8_t *msg, int msg_len,
                              uint32_t sample_offset);

void clap_event_list_add_sysex(clap_event_list_t *el,
                               const uint8_t *data, uint32_t len,
                               uint32_t sample_offset);

uint32_t               clap_event_list_size(const clap_event_list_t *el);
const clap_event_header_t *clap_event_list_get(const clap_event_list_t *el, uint32_t index);

const clap_input_events_t  *clap_event_list_get_input(const clap_event_list_t *el);
const clap_output_events_t *clap_event_list_get_output(const clap_event_list_t *el);

#ifdef __cplusplus
}
#endif

#endif /* EMU_CLAP_EVENT_LIST_H */
