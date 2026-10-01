#include <string.h>
#include "cm100_internal.h"

#define CM100_SECTOR_NS UINT64_C(13333333)

static int
cm100_load_sector(cm100_drive_t *d)
{
    if (!d->read_sector || d->sector >= d->medium_sectors) {
        cm100_stop_read(d);
        cm100_raise_drive_error(d, CM100_ERROR_END);
        return 0;
    }
    memset(d->sector_urd, 0, sizeof(d->sector_urd));
    if (!d->read_sector(d->opaque, d->sector, d->sector_data, d->sector_urd)) {
        cm100_stop_read(d);
        cm100_raise_drive_error(d, CM100_ERROR_UNREADABLE);
        return 0;
    }
    d->sector_loaded = 1;
    d->sector_bit = 0;
    d->data_attention = !!d->sector_urd[0];
    cm100_emit(d, CM100_ATTENTION,
               d->latched_attention || d->data_attention);
    return 1;
}

static void
cm100_finish_sector(cm100_drive_t *d)
{
    d->sector_loaded = 0;
    d->sector_bit = 0;
    d->data_attention = 0;
    cm100_emit(d, CM100_ATTENTION, d->latched_attention);
    d->sector++;
    d->next_sector_ns += CM100_SECTOR_NS;
    if (d->reading_bounded && d->sector > d->read_end) {
        cm100_stop_read(d);
        d->motion = CM100_HOLD_TRACK;
    } else if (d->sector >= d->medium_sectors) {
        d->sector = d->medium_sectors ? d->medium_sectors - 1 : 0;
        cm100_stop_read(d);
        cm100_raise_drive_error(d, CM100_ERROR_END);
        cm100_start_seek(d, d->read_start, 0);
    }
}

int
cm100_data_edge(cm100_drive_t *d, int rising, uint64_t now_ns)
{
    cm100_advance(d, now_ns);
    if (d->motion != CM100_READING || now_ns < d->next_sector_ns)
        return -1;

    if (!rising) {
        cm100_emit(d, CM100_DATA_CLOCK, 0);
        if (d->sector_loaded && d->sector_bit == CM100_SECTOR_BYTES * 8)
            cm100_finish_sector(d);
        if (d->motion != CM100_READING || now_ns < d->next_sector_ns)
            return -1;
        if (!d->sector_loaded && !cm100_load_sector(d))
            return -1;
        unsigned byte = d->sector_bit / 8;
        unsigned bit = d->sector_bit % 8;
        cm100_emit(d, CM100_DATA,
                   (d->sector_data[byte] >> bit) & 1);
        return d->output_data;
    }

    if (!d->sector_loaded || d->output_clock)
        return -1;
    cm100_emit(d, CM100_DATA_CLOCK, 1);
    d->sector_bit++;
    if (!(d->sector_bit & 7)) {
        unsigned next_byte = d->sector_bit / 8;
        d->data_attention = next_byte < CM100_SECTOR_BYTES &&
                            !!d->sector_urd[next_byte];
        cm100_emit(d, CM100_ATTENTION,
                   d->latched_attention || d->data_attention);
    }
    return d->output_data;
}
