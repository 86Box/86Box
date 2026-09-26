#include <stdlib.h>
#include <string.h>
#include "cm100_internal.h"

/* EP 0 241 081 gives ordering and timeout limits.
   These provisional durations are isolated until a real mechanism is traced. */
#define CM100_SPIN_UP_NS    UINT64_C(2000000000)
#define CM100_SPIN_DOWN_NS  UINT64_C(1000000000)
#define CM100_SEEK_NS       UINT64_C(40000000)
#define CM100_DIAG_NS       UINT64_C(10000000)
#define CM100_SECTOR_NS     UINT64_C(13333333)

void
cm100_emit(cm100_drive_t *d, cm100_signal_t signal, int level)
{
    uint8_t *state = NULL;
    switch (signal) {
        case CM100_COMMAND:    state = &d->input_command; break;
        case CM100_RESPONSE:   state = &d->output_response; break;
        case CM100_DATA:       state = &d->output_data; break;
        case CM100_DATA_CLOCK: state = &d->output_clock; break;
        case CM100_ATTENTION:  state = &d->attention; break;
    }
    if (state && *state != !!level) {
        *state = !!level;
        if (d->signal)
            d->signal(d->opaque, signal, !!level, d->now_ns);
    }
}

cm100_drive_t *
cm100_create(cm100_read_sector_fn read_sector, cm100_signal_fn signal,
             void *opaque)
{
    cm100_drive_t *d = calloc(1, sizeof(*d));
    if (!d)
        return NULL;
    d->read_sector = read_sector;
    d->signal = signal;
    d->opaque = opaque;
    cm100_reset(d, 0);
    return d;
}

void
cm100_destroy(cm100_drive_t *d)
{
    free(d);
}

void
cm100_reset(cm100_drive_t *d, uint64_t now_ns)
{
    cm100_read_sector_fn read_sector = d->read_sector;
    cm100_signal_fn signal = d->signal;
    void *opaque = d->opaque;
    uint32_t medium_sectors = d->medium_sectors;
    uint8_t disc_present = d->disc_present;
    uint8_t medium_known = d->medium_known;
    memset(d, 0, sizeof(*d));
    d->read_sector = read_sector;
    d->signal = signal;
    d->opaque = opaque;
    d->medium_sectors = medium_sectors;
    d->disc_present = disc_present;
    d->medium_known = medium_known;
    d->door_open = !disc_present;
    d->now_ns = now_ns;
    d->reset_done_ns = now_ns + CM100_DIAG_NS;
    d->motion = CM100_STOPPED;
    d->output_response = 1;
    d->input_command = 1;
    cm100_protocol_reset(d);
}

void
cm100_set_medium(cm100_drive_t *d, uint32_t sectors, int present)
{
    int changed = d->medium_known &&
                  (d->disc_present != !!present ||
                   d->medium_sectors != (present ? sectors : 0));
    d->medium_known = 1;
    d->disc_present = !!present;
    d->medium_sectors = present ? sectors : 0;
    d->door_open = !present;
    d->sector_loaded = 0;
    cm100_stop_read(d);
    if (!present) {
        d->motion = CM100_STOPPED;
        d->motion_end_ns = 0;
        d->pending_seek = 0;
        d->seek_invalid = 0;
    }
    if (changed)
        cm100_raise_drive_error(d, CM100_EVENT_UNIT_CHANGE);
}

void
cm100_raise_drive_error(cm100_drive_t *d, cm100_error_t error)
{
    if (!d->drive_error)
        d->drive_error = error;
    if (error != CM100_EVENT_RESET && error != CM100_EVENT_SPIN_UP &&
        error != CM100_EVENT_SPIN_DOWN && error != CM100_EVENT_TIMEOUT &&
        error != CM100_EVENT_UNIT_CHANGE)
        d->status_error = 1;
    cm100_encode_msf(d->sector + 150, d->status_address);
    d->latched_attention = 1;
    cm100_emit(d, CM100_ATTENTION, 1);
}

void
cm100_raise_comm_error(cm100_drive_t *d, cm100_comm_error_t error)
{
    if (!d->communication_error)
        d->communication_error = error;
    d->latched_attention = 1;
    cm100_emit(d, CM100_ATTENTION, 1);
}

void
cm100_stop_read(cm100_drive_t *d)
{
    d->sector_loaded = 0;
    d->sector_bit = 0;
    d->reading_bounded = 0;
    d->data_attention = 0;
    cm100_emit(d, CM100_ATTENTION, d->latched_attention);
    cm100_emit(d, CM100_DATA_CLOCK, 0);
    if (d->motion == CM100_READING)
        d->motion = CM100_HOLD_TRACK;
}

void
cm100_start_spin(cm100_drive_t *d)
{
    if (!d->disc_present) {
        cm100_raise_drive_error(d, CM100_ERROR_FOCUS);
        return;
    }
    d->spin_notice = 1;
    d->seek_origin = d->sector;
    d->seek_sector = 0;
    d->pending_seek = 1;
    d->seek_read = 0;
    d->seek_invalid = 0;
    if (d->motion == CM100_STOPPED || d->motion == CM100_SPINNING_DOWN) {
        d->motion = CM100_SPINNING_UP;
        d->motion_start_ns = d->now_ns;
        d->motion_end_ns = d->now_ns + CM100_SPIN_UP_NS;
    } else if (d->motion != CM100_SPINNING_UP) {
        d->motion = CM100_SEEKING;
        d->motion_start_ns = d->now_ns;
        d->motion_end_ns = d->now_ns + CM100_SEEK_NS;
    }
}

void
cm100_start_seek(cm100_drive_t *d, uint32_t sector, int read)
{
    if (!d->disc_present) {
        cm100_raise_drive_error(d, CM100_ERROR_FOCUS);
        return;
    }
    cm100_stop_read(d);
    d->spin_notice = 0;
    d->seek_origin = d->sector;
    d->seek_invalid = sector >= d->medium_sectors;
    d->seek_sector = d->seek_invalid ?
        (d->medium_sectors ? d->medium_sectors - 1 : 0) : sector;
    d->pending_seek = 1;
    d->seek_read = !!read && !d->seek_invalid;
    if (d->motion == CM100_STOPPED || d->motion == CM100_SPINNING_DOWN) {
        d->motion = CM100_SPINNING_UP;
        d->motion_start_ns = d->now_ns;
        d->motion_end_ns = d->now_ns + CM100_SPIN_UP_NS;
    } else if (d->motion != CM100_SPINNING_UP) {
        d->motion = CM100_SEEKING;
        d->motion_start_ns = d->now_ns;
        d->motion_end_ns = d->now_ns + CM100_SEEK_NS;
    }
}

void
cm100_advance(cm100_drive_t *d, uint64_t now_ns)
{
    if (now_ns < d->now_ns)
        return;
    if (d->reset_done_ns && now_ns >= d->reset_done_ns) {
        d->now_ns = d->reset_done_ns;
        d->reset_done_ns = 0;
        cm100_raise_drive_error(d, CM100_EVENT_RESET);
    }
    while (d->motion_end_ns && now_ns >= d->motion_end_ns) {
        d->now_ns = d->motion_end_ns;
        d->motion_end_ns = 0;
        if (d->motion == CM100_SPINNING_UP) {
            d->motion = CM100_READY;
            if (d->pending_seek) {
                d->motion = CM100_SEEKING;
                d->motion_start_ns = d->now_ns;
                d->motion_end_ns = d->now_ns + CM100_SEEK_NS;
            }
        } else if (d->motion == CM100_SEEKING) {
            d->sector = d->seek_sector;
            d->pending_seek = 0;
            d->motion = d->seek_read ? CM100_READING : CM100_HOLD_TRACK;
            d->next_sector_ns = d->now_ns;
            if (d->seek_invalid)
                cm100_raise_drive_error(d, CM100_ERROR_ADDRESS);
            else if (d->spin_notice)
                cm100_raise_drive_error(d, CM100_EVENT_SPIN_UP);
            d->seek_invalid = 0;
            d->spin_notice = 0;
        } else if (d->motion == CM100_SPINNING_DOWN) {
            d->motion = CM100_STOPPED;
        }
    }
    d->now_ns = now_ns;
}

void
cm100_snapshot(const cm100_drive_t *d, cm100_snapshot_t *out)
{
    memset(out, 0, sizeof(*out));
    out->now_ns = d->now_ns;
    out->next_sector_ns = d->next_sector_ns;
    out->sector = d->sector;
    out->sector_bit = d->sector_bit;
    out->seek_sector = d->seek_sector;
    out->medium_sectors = d->medium_sectors;
    out->motion = d->motion;
    out->pickup_fraction = d->medium_sectors ?
        (float) d->sector / d->medium_sectors : 0.0f;
    if (d->motion == CM100_SEEKING && d->medium_sectors &&
        d->motion_end_ns > d->motion_start_ns) {
        float progress = (float) (d->now_ns - d->motion_start_ns) /
                         (float) (d->motion_end_ns - d->motion_start_ns);
        out->pickup_fraction = ((float) d->seek_origin +
                                ((float) d->seek_sector - d->seek_origin) *
                                    progress) / d->medium_sectors;
    }
    out->spindle_fraction = d->motion == CM100_STOPPED ? 0.0f : 1.0f;
    if (d->motion == CM100_SPINNING_UP && d->motion_end_ns > d->motion_start_ns)
        out->spindle_fraction = (float) (d->now_ns - d->motion_start_ns) /
                                (float) (d->motion_end_ns - d->motion_start_ns);
    if (d->motion == CM100_SPINNING_DOWN && d->motion_end_ns > d->motion_start_ns)
        out->spindle_fraction = 1.0f - (float) (d->now_ns - d->motion_start_ns) /
                                         (float) (d->motion_end_ns - d->motion_start_ns);
    out->drive_error = d->drive_error;
    out->communication_error = d->communication_error;
    memcpy(out->last_command, d->last_command, sizeof(out->last_command));
    out->attention = d->attention;
    out->door_open = d->door_open;
    out->locked = d->locked;
    out->data_active = d->motion == CM100_READING;
}
