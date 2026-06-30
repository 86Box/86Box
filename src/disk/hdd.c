/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Common code to handle all sorts of hard disk images.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2017-2019 Fred N. van Kempen.
 */
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/hdd.h>
#include <86box/cdrom.h>
#include <86box/video.h>
#include <86box/hdd_audio.h>
#include "cpu.h"

#define HDD_OVERHEAD_TIME 50.0

hard_disk_t hdd[HDD_NUM];

int
hdd_init(void)
{
    /* Clear all global data. */
    memset(hdd, 0x00, sizeof(hdd));
    return 0;
}

int
hdd_string_to_bus(char *str, int cdrom)
{
    if (!strcmp(str, "none"))
        return HDD_BUS_DISABLED;

    if (!strcmp(str, "mfm") && !cdrom)
        return HDD_BUS_MFM;

    if (!strcmp(str, "esdi") && !cdrom)
        return HDD_BUS_ESDI;

    if (!strcmp(str, "ide"))
        return HDD_BUS_IDE;

    if (!strcmp(str, "atapi"))
        return HDD_BUS_ATAPI;

    if (!strcmp(str, "xta"))
        return HDD_BUS_XTA;

    if (!strcmp(str, "scsi"))
        return HDD_BUS_SCSI;
    
    if (!strcmp(str, "mitsumi") && cdrom)
        return CDROM_BUS_MITSUMI;

    if (!strcmp(str, "mke") && cdrom)
        return CDROM_BUS_MKE;

    return HDD_BUS_DISABLED;
}

char *
hdd_bus_to_string(int bus, int cdrom)
{
    char *s = "none";

    switch (bus) {
        default:
        case HDD_BUS_DISABLED:
            break;

        case HDD_BUS_MFM:
            if (!cdrom)
                s = "mfm";
            break;

        case HDD_BUS_XTA:
            s = "xta";
            break;

        case HDD_BUS_ESDI:
            if (!cdrom)
                s = "esdi";
            break;

        case HDD_BUS_IDE:
            s = "ide";
            break;

        case HDD_BUS_ATAPI:
            s = "atapi";
            break;

        case HDD_BUS_SCSI:
            s = "scsi";
            break;

        case CDROM_BUS_MITSUMI:
            if (cdrom)
                s = "mitsumi";
            break;

        case CDROM_BUS_MKE:
            if (cdrom)
                s = "mke";
            break;
    }

    return s;
}

int
hdd_is_valid(int c)
{
    if (hdd[c].bus_type == HDD_BUS_DISABLED)
        return 0;

    if (strlen(hdd[c].fn) == 0)
        return 0;

    if ((hdd[c].tracks == 0) || (hdd[c].hpc == 0) || (hdd[c].spt == 0))
        return 0;

    return 1;
}

double
hdd_seek_get_time(hard_disk_t *hdd, uint32_t dst_addr, uint8_t operation, uint8_t continuous, double max_seek_time)
{
    if (!hdd->speed_preset)
        return HDD_OVERHEAD_TIME;

    const hdd_zone_t *zone = NULL;
    if (hdd->num_zones <= 0) {
#ifdef DO_FATAL
        fatal("hdd_seek_get_time(): hdd->num_zones < 0)\n");
        return 0.0;
#else
        return 1000.0;
#endif
    }
    for (uint32_t i = 0; i < hdd->num_zones; i++) {
        zone = &hdd->zones[i];
        if (zone->end_sector >= dst_addr)
            break;
    }

    double continuous_times[2][2] = {
        {hdd->head_switch_usec,   hdd->cyl_switch_usec  },
        { zone->sector_time_usec, zone->sector_time_usec}
    };
    double times[2] = { HDD_OVERHEAD_TIME, hdd->avg_rotation_lat_usec };

    uint32_t new_track     = zone->start_track + ((dst_addr - zone->start_sector) / zone->sectors_per_track);
    uint32_t new_cylinder  = new_track / hdd->phy_heads;
    uint32_t cylinder_diff = abs((int) hdd->cur_cylinder - (int) new_cylinder);

    bool sequential = dst_addr == hdd->cur_addr + 1;
    continuous      = continuous && sequential;

    double seek_time = 0.0;
    if (continuous)
        seek_time = continuous_times[new_track == hdd->cur_track][!!cylinder_diff];
    else {
        if (!cylinder_diff)
            seek_time = times[operation != HDD_OP_SEEK];
        else {
            seek_time = hdd->cyl_switch_usec + (hdd->full_stroke_usec * (double) cylinder_diff / (double) hdd->phy_cyl) + ((operation != HDD_OP_SEEK) * hdd->avg_rotation_lat_usec);
        }
    }

    if (!max_seek_time || seek_time <= max_seek_time) {
        if (new_cylinder != hdd->cur_cylinder)
            hdd_audio_seek(hdd, new_cylinder);

        hdd->cur_addr     = dst_addr;
        hdd->cur_track    = new_track;
        hdd->cur_cylinder = new_cylinder;
    }

    return seek_time;
}

static void
hdd_readahead_update(hard_disk_t *hdd)
{
    uint64_t elapsed_cycles;
    double   elapsed_us;
    double   seek_time;
    int32_t  max_read_ahead;
    uint32_t space_needed;

    hdd_cache_t *cache = &hdd->cache;
    if (cache->ra_ongoing) {
        hdd_cache_seg_t *segment = &cache->segments[cache->ra_segment];

        elapsed_cycles = tsc - cache->ra_start_time;
        elapsed_us     = (double) elapsed_cycles / cpuclock * 1000000.0;
        /* Do not overwrite data not yet read by host */
        max_read_ahead = (segment->host_addr + cache->segment_size) - segment->ra_addr;

        seek_time = 0.0;

        for (int32_t i = 0; i < max_read_ahead; i++) {
            seek_time += hdd_seek_get_time(hdd, segment->ra_addr, HDD_OP_READ, 1, elapsed_us - seek_time);
            if (seek_time > elapsed_us)
                break;

            segment->ra_addr++;
        }

        if (segment->ra_addr > segment->lba_addr + cache->segment_size) {
            space_needed = segment->ra_addr - (segment->lba_addr + cache->segment_size);
            segment->lba_addr += space_needed;
        }
    }
}

static double
hdd_writecache_flush(hard_disk_t *hdd)
{
    double seek_time = 0.0;

    while (hdd->cache.write_pending) {
        seek_time += hdd_seek_get_time(hdd, hdd->cache.write_addr, HDD_OP_WRITE, 1, 0);
        hdd->cache.write_addr++;
        hdd->cache.write_pending--;
    }

    return seek_time;
}

static void
hdd_writecache_update(hard_disk_t *hdd)
{
    uint64_t elapsed_cycles;
    double   elapsed_us;
    double   seek_time;

    if (hdd->cache.write_pending) {
        elapsed_cycles = tsc - hdd->cache.write_start_time;
        elapsed_us     = (double) elapsed_cycles / cpuclock * 1000000.0;
        seek_time      = 0.0;

        while (hdd->cache.write_pending) {
            seek_time += hdd_seek_get_time(hdd, hdd->cache.write_addr, HDD_OP_WRITE, 1, elapsed_us - seek_time);
            if (seek_time > elapsed_us)
                break;

            hdd->cache.write_addr++;
            hdd->cache.write_pending--;
        }
    }
}

double
hdd_timing_write(hard_disk_t *hdd, uint32_t addr, uint32_t len)
{
    double   seek_time = 0.0;
    uint32_t flush_needed;

    if (!hdd->speed_preset)
        return HDD_OVERHEAD_TIME;

    hdd_readahead_update(hdd);
    hdd_writecache_update(hdd);

    hdd->cache.ra_ongoing = 0;

    if (hdd->cache.write_pending && (addr != (hdd->cache.write_addr + hdd->cache.write_pending))) {
        /* New request is not sequential to existing cache, need to flush it */
        seek_time += hdd_writecache_flush(hdd);
    }

    if (!hdd->cache.write_pending) {
        /* Cache is empty */
        hdd->cache.write_addr = addr;
    }

    hdd->cache.write_pending += len;
    if (hdd->cache.write_pending > hdd->cache.write_size) {
        /* If request is bigger than free cache, flush some data first */
        flush_needed = hdd->cache.write_pending - hdd->cache.write_size;
        for (uint32_t i = 0; i < flush_needed; i++) {
            seek_time += hdd_seek_get_time(hdd, hdd->cache.write_addr, HDD_OP_WRITE, 1, 0);
            hdd->cache.write_addr++;
        }
    }

    hdd->cache.write_start_time = tsc + (uint64_t) (seek_time * cpuclock / 1000000.0);

    return seek_time;
}

double
hdd_timing_read(hard_disk_t *hdd, uint32_t addr, uint32_t len)
{
    double seek_time = 0.0;

    if (!hdd->speed_preset)
        return HDD_OVERHEAD_TIME;

    hdd_readahead_update(hdd);
    hdd_writecache_update(hdd);

    seek_time += hdd_writecache_flush(hdd);

    hdd_cache_t     *cache      = &hdd->cache;
    hdd_cache_seg_t *active_seg = &cache->segments[0];

    for (uint32_t i = 0; i < cache->num_segments; i++) {
        hdd_cache_seg_t *segment = &cache->segments[i];
        if (!segment->valid) {
            active_seg = segment;
            continue;
        }

        if (segment->lba_addr <= addr && (segment->lba_addr + cache->segment_size) >= addr) {
            /* Cache HIT */
            segment->host_addr = addr;
            active_seg         = segment;
            if (addr + len > segment->ra_addr) {
                uint32_t need_read = (addr + len) - segment->ra_addr;
                for (uint32_t j = 0; j < need_read; j++) {
                    seek_time += hdd_seek_get_time(hdd, segment->ra_addr, HDD_OP_READ, 1, 0.0);
                    segment->ra_addr++;
                }
            }
            if (addr + len > segment->lba_addr + cache->segment_size) {
                /* Need to erase some previously cached data */
                uint32_t space_needed = (addr + len) - (segment->lba_addr + cache->segment_size);
                segment->lba_addr += space_needed;
            }
            goto update_lru;
        } else {
            if (segment->lru > active_seg->lru)
                active_seg = segment;
        }
    }

    /* Cache MISS */
    active_seg->lba_addr  = addr;
    active_seg->valid     = 1;
    active_seg->host_addr = addr;
    active_seg->ra_addr   = addr;

    for (uint32_t i = 0; i < len; i++) {
        seek_time += hdd_seek_get_time(hdd, active_seg->ra_addr, HDD_OP_READ, i != 0, 0.0);
        active_seg->ra_addr++;
    }

update_lru:
    for (uint32_t i = 0; i < cache->num_segments; i++)
        cache->segments[i].lru++;

    active_seg->lru = 0;

    cache->ra_ongoing    = 1;
    cache->ra_segment    = active_seg->id;
    cache->ra_start_time = tsc + (uint64_t) (seek_time * cpuclock / 1000000.0);

    return seek_time;
}

static void
hdd_cache_init(hard_disk_t *hdd)
{
    hdd_cache_t *cache = &hdd->cache;

    cache->ra_segment    = 0;
    cache->ra_ongoing    = 0;
    cache->ra_start_time = 0;

    for (uint32_t i = 0; i < cache->num_segments; i++) {
        cache->segments[i].valid     = 0;
        cache->segments[i].lru       = 0;
        cache->segments[i].id        = i;
        cache->segments[i].ra_addr   = 0;
        cache->segments[i].host_addr = 0;
    }
}

static void
hdd_zones_init(hard_disk_t *hdd)
{
    uint32_t    lba = 0;
    uint32_t    track = 0;
    uint32_t    tracks;
    double      revolution_usec = 60.0 / (double) hdd->rpm * 1000000.0;
    hdd_zone_t *zone;

    for (uint32_t i = 0; i < hdd->num_zones; i++) {
        zone                   = &hdd->zones[i];
        zone->start_sector     = lba;
        zone->start_track      = track;
        zone->sector_time_usec = revolution_usec / (double) zone->sectors_per_track;
        tracks                 = zone->cylinders * hdd->phy_heads;
        lba += tracks * zone->sectors_per_track;
        zone->end_sector = lba - 1;
        track += tracks - 1;
    }
}

static hdd_preset_t hdd_speed_presets[] = {
  // clang-format off
    { .name = "[Generic] RAM Disk (max. speed)",                  .internal_name = "ramdisk",                                                                                                                                                                                 .rcache_num_seg = 16, .rcache_seg_size =  128, .max_multiple = 32 },
    { .name = "[Generic] 1983 MFM (3600 RPM)",                    .internal_name = "mfm_3600rpm",                                                                          .zones =  1, .avg_spt =  17, .heads =  4, .rpm = 3600, .full_stroke_ms = 80, .track_seek_ms = 10,  .rcache_num_seg =  1, .rcache_seg_size =   17, .max_multiple =  1 },
    { .name = "[Generic] 1983 RLL (3600 RPM)",                    .internal_name = "rll_3600rpm",                                                                          .zones =  1, .avg_spt =  26, .heads =  4, .rpm = 3600, .full_stroke_ms = 80, .track_seek_ms = 10,  .rcache_num_seg =  1, .rcache_seg_size =   26, .max_multiple =  1 },
    { .name = "[Generic] 1989 (3500 RPM)",                        .internal_name = "1989_3500rpm",                                                                         .zones =  1, .avg_spt =  35, .heads =  2, .rpm = 3500, .full_stroke_ms = 40, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =   16, .max_multiple =  8 },
    { .name = "[Generic] 1992 (3600 RPM)",                        .internal_name = "1992_3600rpm",                                                                         .zones =  1, .avg_spt =  45, .heads =  2, .rpm = 3600, .full_stroke_ms = 30, .track_seek_ms = 6,   .rcache_num_seg =  4, .rcache_seg_size =   16, .max_multiple =  8 },
    { .name = "[Generic] 1994 (4500 RPM)",                        .internal_name = "1994_4500rpm",                                                                         .zones =  8, .avg_spt =  80, .heads =  4, .rpm = 4500, .full_stroke_ms = 26, .track_seek_ms = 5,   .rcache_num_seg =  4, .rcache_seg_size =   32, .max_multiple = 16 },
    { .name = "[Generic] 1996 (5400 RPM)",                        .internal_name = "1996_5400rpm",                                                                         .zones = 16, .avg_spt = 135, .heads =  4, .rpm = 5400, .full_stroke_ms = 24, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple = 16 },
    { .name = "[Generic] 1997 (5400 RPM)",                        .internal_name = "1997_5400rpm",                                                                         .zones = 16, .avg_spt = 185, .heads =  6, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2.5, .rcache_num_seg =  8, .rcache_seg_size =   64, .max_multiple = 32 },
    { .name = "[Generic] 1998 (5400 RPM)",                        .internal_name = "1998_5400rpm",                                                                         .zones = 16, .avg_spt = 300, .heads =  8, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 32 },
    { .name = "[Generic] 2000 (7200 RPM)",                        .internal_name = "2000_7200rpm",                                                                         .zones = 16, .avg_spt = 350, .heads =  6, .rpm = 7200, .full_stroke_ms = 15, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  128, .max_multiple = 32 },
    /* MFM/RLL drives: single zone, 17 spt (MFM) or 26 spt (RLL), 3600 RPM typical */
    { .name = "[MFM] Ampex PYXIS-7 (5 MB)",                       .internal_name = "PYXIS7",       .model = "PYXIS-7",                                                     .zones =  1, .avg_spt =  32, .heads =  2, .rpm = 3600, .full_stroke_ms = 54, .track_seek_ms = 23,  .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[MFM] Ampex PYXIS-13 (10 MB)",                     .internal_name = "PYXIS13",      .model = "PYXIS-13",                                                    .zones =  1, .avg_spt =  32, .heads =  4, .rpm = 3600, .full_stroke_ms = 54, .track_seek_ms = 23,  .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[MFM] Ampex PYXIS-20 (15 MB)",                     .internal_name = "PYXIS20",      .model = "PYXIS-20",                                                    .zones =  1, .avg_spt =  32, .heads =  6, .rpm = 3600, .full_stroke_ms = 54, .track_seek_ms = 23,  .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[MFM] Ampex PYXIS-27 (20 MB)",                     .internal_name = "PYXIS27",      .model = "PYXIS-27",                                                    .zones =  1, .avg_spt =  32, .heads =  8, .rpm = 3600, .full_stroke_ms = 54, .track_seek_ms = 23,  .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[MFM] IBM 10mb 5.25 (10 MB)",                      .internal_name = "IBM10MB",      .model = "IBM 10MB",                                                    .zones =  1, .avg_spt =  17, .heads =  4, .rpm = 3600, .full_stroke_ms = 40, .track_seek_ms = 10,  .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[MFM] IBM WD 12 (12 MB)",                          .internal_name = "WD12",         .model = "WD 12",                                                       .zones =  1, .avg_spt =  17, .heads =  4, .rpm = 3600, .full_stroke_ms = 40, .track_seek_ms = 10,  .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[MFM] IBM WD 25 (25 MB)",                          .internal_name = "WD25",         .model = "WD 25",                                                       .zones =  1, .avg_spt =  17, .heads =  8, .rpm = 3600, .full_stroke_ms = 40, .track_seek_ms = 10,  .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[MFM] Kalok KL-320 (25 MB)",                       .internal_name = "KL320",        .model = "KL-320",                                                      .zones =  1, .avg_spt =  17, .heads =  4, .rpm = 3600, .full_stroke_ms = 40, .track_seek_ms = 10,  .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[MFM] Microcomputer Memories MM-112 (10 MB)",      .internal_name = "MM112",        .model = "MM-112",                                                      .zones =  1, .avg_spt =  17, .heads =  4, .rpm = 3600, .full_stroke_ms = 40, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =   17, .max_multiple =  1 },
    { .name = "[MFM] Microcomputer Memories MM-212 (10 MB)",      .internal_name = "MM212",        .model = "MM-212",                                                      .zones =  1, .avg_spt =  17, .heads =  4, .rpm = 3600, .full_stroke_ms = 40, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =   17, .max_multiple =  1 },
    { .name = "[MFM] Microcomputer Memories MM-312 (10 MB)",      .internal_name = "MM312",        .model = "MM-312",                                                      .zones =  1, .avg_spt =  17, .heads =  4, .rpm = 3600, .full_stroke_ms = 40, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =   17, .max_multiple =  1 },
    { .name = "[MFM] Microscience HH-825 (21 MB)",                .internal_name = "HH825",        .model = "HH 825",                                                      .zones =  1, .avg_spt =  32, .heads =  4, .rpm = 3600, .full_stroke_ms = 60, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =   17, .max_multiple =  1 },
    { .name = "[MFM] Microscience HH-1090 (80 MB)",               .internal_name = "HH-1090",      .model = "HH 1090",                                                     .zones =  1, .avg_spt =  17, .heads =  7, .rpm = 3600, .full_stroke_ms = 60, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[MFM] Miniscribe 8425 (21 MB)",                    .internal_name = "MNS8425",      .model = "Miniscribe 8425",                                             .zones =  1, .avg_spt =  17, .heads =  4, .rpm = 3600, .full_stroke_ms = 65, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =   17, .max_multiple =  1 },
    { .name = "[MFM] Miniscribe 8438 (32 MB)",                    .internal_name = "MNS8438",      .model = "Miniscribe 8438",                                             .zones =  1, .avg_spt =  17, .heads =  5, .rpm = 3600, .full_stroke_ms = 65, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =   17, .max_multiple =  1 },
    { .name = "[MFM] Seagate ST-124 (20 MB)",                     .internal_name = "ST124",        .model = "ST-124",                                                      .zones =  1, .avg_spt =  17, .heads =  4, .rpm = 3600, .full_stroke_ms = 65, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =   17, .max_multiple =  1 },
    { .name = "[MFM] Seagate ST-225 (20 MB)",                     .internal_name = "ST225",        .model = "ST-225",                                                      .zones =  1, .avg_spt =  17, .heads =  4, .rpm = 3600, .full_stroke_ms = 65, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =   17, .max_multiple =  1 },
    { .name = "[MFM] Seagate ST-251 (40 MB)",                     .internal_name = "ST251",        .model = "ST-251",                                                      .zones =  1, .avg_spt =  17, .heads =  6, .rpm = 3600, .full_stroke_ms = 65, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =   17, .max_multiple =  1 },
    { .name = "[MFM] Seagate ST-4038 (32 MB)",                    .internal_name = "ST4038",       .model = "ST-4038",                                                     .zones =  1, .avg_spt =  17, .heads =  5, .rpm = 3600, .full_stroke_ms = 80, .track_seek_ms = 10,  .rcache_num_seg =  1, .rcache_seg_size =   17, .max_multiple =  1 },
    { .name = "[MFM] Seagate ST-4051 (43 MB)",                    .internal_name = "ST4051",       .model = "ST-4051",                                                     .zones =  1, .avg_spt =  17, .heads =  5, .rpm = 3600, .full_stroke_ms = 80, .track_seek_ms = 10,  .rcache_num_seg =  1, .rcache_seg_size =   17, .max_multiple =  1 },
    { .name = "[MFM] Western Digital WD93028-X (20 MB)",          .internal_name = "WD93028X",     .model = "WD93028-X",                                                   .zones =  1, .avg_spt =  17, .heads =  4, .rpm = 3600, .full_stroke_ms = 75, .track_seek_ms = 10,  .rcache_num_seg =  1, .rcache_seg_size =   17, .max_multiple =  1 },
    { .name = "[RLL] Kalok KL-330 (32 MB)",                       .internal_name = "KL330",        .model = "KL-330",                                                      .zones =  1, .avg_spt =  26, .heads =  4, .rpm = 3600, .full_stroke_ms = 40, .track_seek_ms = 10,  .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[RLL] Microscience HH-1120 (122 MB)",              .internal_name = "HH-1120",      .model = "HH 1120",                                                     .zones =  1, .avg_spt =  26, .heads =  7, .rpm = 3600, .full_stroke_ms = 60, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   26, .max_multiple =  1 },
    { .name = "[RLL] Seagate ST-238R (32 MB)",                    .internal_name = "ST238R",       .model = "ST-238R",                                                     .zones =  1, .avg_spt =  26, .heads =  4, .rpm = 3600, .full_stroke_ms = 65, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =   26, .max_multiple =  1 },
    { .name = "[RLL] Seagate ST-253R (63 MB)",                    .internal_name = "ST253R",       .model = "ST-253R",                                                     .zones =  1, .avg_spt =  26, .heads =  6, .rpm = 3600, .full_stroke_ms = 65, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =   26, .max_multiple =  1 },
    { .name = "[PIO IDE] Areal A-90XT",                           .internal_name = "A90XT",        .model = "AREAL A-90XT",                                                .zones =  1, .avg_spt =  50, .heads =  2, .rpm = 2087, .full_stroke_ms = 55, .track_seek_ms = 4,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[PIO IDE] Areal A-120XT",                          .internal_name = "A120XT",       .model = "AREAL A-120XT",                                               .zones =  1, .avg_spt =  50, .heads =  4, .rpm = 2981, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[PIO IDE] Areal A-130XT",                          .internal_name = "A130XT",       .model = "AREAL A-130XT",                                               .zones =  1, .avg_spt =  50, .heads =  2, .rpm = 2981, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[PIO IDE] Areal A-170XT",                          .internal_name = "A170XT",       .model = "AREAL A-170XT",                                               .zones =  1, .avg_spt =  50, .heads =  4, .rpm = 2981, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[PIO IDE] Areal A-260XT",                          .internal_name = "A260XT",       .model = "AREAL A-260XT",                                               .zones =  1, .avg_spt =  50, .heads =  4, .rpm = 2981, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[PIO IDE] Areal A-340XT",                          .internal_name = "A340XT",       .model = "AREAL A-340XT",                                               .zones =  1, .avg_spt =  50, .heads =  6, .rpm = 3600, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =  128, .max_multiple =  1 },
    { .name = "[PIO IDE] Areal MD-2060XT",                        .internal_name = "MD2060XT",     .model = "AREAL MD-2060XT",                                             .zones =  1, .avg_spt =  90, .heads =  2, .rpm = 2087, .full_stroke_ms = 35, .track_seek_ms = 4,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[PIO IDE] Areal MD-2065XT",                        .internal_name = "MD2065XT",     .model = "AREAL MD-2065XT",                                             .zones =  1, .avg_spt =  50, .heads =  2, .rpm = 2504, .full_stroke_ms = 35, .track_seek_ms = 4,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[PIO IDE] Conner CP-341",                          .internal_name = "CP341",        .model = "Conner Peripherals 40MB - CP341",                             .zones =  1, .avg_spt =  40, .heads =  2, .rpm = 3500, .full_stroke_ms = 50, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =    8, .max_multiple =  1 },
    { .name = "[PIO IDE] HP C2233 A",                             .internal_name = "C2233A",       .model = "C2233 A",                                                     .zones =  2, .avg_spt = 126, .heads =  1, .rpm = 3600, .full_stroke_ms = 33, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =   64, .max_multiple =  1 },
    { .name = "[PIO IDE] HP C2234 A",                             .internal_name = "C2234A",       .model = "C2234 A",                                                     .zones =  2, .avg_spt = 126, .heads =  2, .rpm = 3600, .full_stroke_ms = 33, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =   64, .max_multiple =  1 },
    { .name = "[PIO IDE] HP C2235 A",                             .internal_name = "C2235A",       .model = "C2235 A",                                                     .zones =  2, .avg_spt = 126, .heads =  3, .rpm = 3600, .full_stroke_ms = 33, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =   64, .max_multiple =  1 },
    { .name = "[PIO IDE] IBM WDA-L42",                            .internal_name = "WDAL42",       .model = "WDA-L42",                                                     .zones =  1, .avg_spt =  85, .heads =  2, .rpm = 3600, .full_stroke_ms = 33, .track_seek_ms = 2.5, .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[PIO IDE] Quantum ProDrive 40AT",                  .internal_name = "QPD40AT",      .model = "QUANTUM PRODRIVE 40AT",                                       .zones =  1, .avg_spt =  50, .heads =  3, .rpm = 3662, .full_stroke_ms = 45, .track_seek_ms = 6,   .rcache_num_seg =  1, .rcache_seg_size =    8, .max_multiple =  1 },
    { .name = "[PIO IDE] Quantum ProDrive ELS (42AT)",            .internal_name = "ELS42AT",      .model = "QUANTUM PRODRIVE 42AT",                                       .zones =  1, .avg_spt =  90, .heads =  1, .rpm = 3600, .full_stroke_ms = 28, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =    8, .max_multiple =  1 },
    { .name = "[PIO IDE] Quantum ProDrive 80AT",                  .internal_name = "QPD80AT",      .model = "QUANTUM PRODRIVE 80AT",                                       .zones =  1, .avg_spt =  90, .heads =  6, .rpm = 3662, .full_stroke_ms = 45, .track_seek_ms = 6,   .rcache_num_seg =  1, .rcache_seg_size =    8, .max_multiple =  1 },
    { .name = "[PIO IDE] Seagate SWIFT (ST1090A)",                .internal_name = "ST1090A",      .model = "st1090AT",                                                    .zones =  1, .avg_spt =  33, .heads =  1, .rpm = 3600, .full_stroke_ms = 35, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[PIO IDE] Seagate SWIFT (ST1126A)",                .internal_name = "ST1126A",      .model = "st1126AT",                                                    .zones =  1, .avg_spt =  33, .heads =  1, .rpm = 3600, .full_stroke_ms = 35, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[PIO IDE] Seagate SWIFT (ST1162A)",                .internal_name = "ST1162A",      .model = "st1162AT",                                                    .zones =  1, .avg_spt =  33, .heads =  1, .rpm = 3600, .full_stroke_ms = 35, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[PIO IDE] Seagate SWIFT (ST1186A)",                .internal_name = "ST1186A",      .model = "st1186AT",                                                    .zones =  1, .avg_spt =  33, .heads =  1, .rpm = 3600, .full_stroke_ms = 35, .track_seek_ms = 4,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[PIO IDE] Seagate SWIFT (ST1201A)",                .internal_name = "ST1201A",      .model = "st1201AT",                                                    .zones =  1, .avg_spt =  50, .heads =  1, .rpm = 3600, .full_stroke_ms = 33, .track_seek_ms = 4,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[PIO IDE] Seagate SWIFT (ST1239A)",                .internal_name = "ST1239A",      .model = "st1239AT",                                                    .zones =  1, .avg_spt =  50, .heads =  2, .rpm = 3600, .full_stroke_ms = 33, .track_seek_ms = 4,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[PIO IDE] Westen Digital WD93024-A",               .internal_name = "WD93024A",     .model = "WD93024A",                                                    .zones =  1, .avg_spt =  33, .heads =  1, .rpm = 3600, .full_stroke_ms = 33, .track_seek_ms = 4,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[PIO IDE] Westen Digital WD93044-A",               .internal_name = "WD93044A",     .model = "WD93044A",                                                    .zones =  1, .avg_spt =  50, .heads =  1, .rpm = 3600, .full_stroke_ms = 33, .track_seek_ms = 4,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  1 },
    { .name = "[PIO-2] IBM DSAA-3270",                            .internal_name = "DSAA3270",     .model = "DSAA-3270",                            .version = "25505120", .zones =  8, .avg_spt = 268, .heads =  2, .rpm = 4500, .full_stroke_ms = 25, .track_seek_ms = 2.1, .rcache_num_seg =  3, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[CE-ATA] Hitachi DK110A-13",                       .internal_name = "DK110A13",     .model = "HITACHI DK110A",                                              .zones =  1, .avg_spt = 150, .heads =  4, .rpm = 4464, .full_stroke_ms = 35, .track_seek_ms = 6,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 }, // Never produced nor released
    { .name = "[ATA-1] Alps Electric DR-311C AT",                 .internal_name = "DR311",        .model = "ALPS DR311",                           .version = "E125052E", .zones =  1, .avg_spt =  33, .heads =  2, .rpm = 3448, .full_stroke_ms = 50, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-1] Alps Electric DR-312C AT",                 .internal_name = "DR312",        .model = "ALPS DR312C",                                                 .zones =  1, .avg_spt =  33, .heads =  4, .rpm = 3448, .full_stroke_ms = 50, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-1] Areal A-60",                               .internal_name = "A60",          .model = "AREAL A-60",                                                  .zones =  1, .avg_spt =  50, .heads =  1, .rpm = 2087, .full_stroke_ms = 35, .track_seek_ms = 4,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Areal A-80",                               .internal_name = "A80",          .model = "AREAL A-80",                                                  .zones =  1, .avg_spt =  50, .heads =  2, .rpm = 2087, .full_stroke_ms = 35, .track_seek_ms = 4,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Areal A-85AT",                             .internal_name = "A85AT",        .model = "AREAL A-85AT",                                                .zones =  1, .avg_spt =  50, .heads =  2, .rpm = 2981, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Areal A-120AT",                            .internal_name = "A120AT",       .model = "AREAL A-120AT",                                               .zones =  1, .avg_spt =  50, .heads =  4, .rpm = 3130, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-1] Areal A-130AT",                            .internal_name = "A130AT",       .model = "AREAL A-130AT",                                               .zones =  1, .avg_spt =  50, .heads =  2, .rpm = 2981, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Areal A-170AT",                            .internal_name = "A170AT",       .model = "AREAL A-170AT",                                               .zones =  1, .avg_spt =  50, .heads =  4, .rpm = 2981, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Areal A-175AT",                            .internal_name = "A175AT",       .model = "AREAL A-175AT",                                               .zones =  1, .avg_spt = 133, .heads =  2, .rpm = 3600, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Areal A-180AT",                            .internal_name = "A180AT",       .model = "AREAL A-180AT",                                               .zones =  1, .avg_spt =  50, .heads =  4, .rpm = 2981, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Areal A-260AT",                            .internal_name = "A260AT",       .model = "AREAL A-260AT",                                               .zones =  1, .avg_spt =  50, .heads =  4, .rpm = 2981, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Areal A-265AT",                            .internal_name = "A265AT",       .model = "AREAL A-265AT",                                               .zones =  1, .avg_spt = 133, .heads =  4, .rpm = 3600, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Areal A-340AT",                            .internal_name = "A340AT",       .model = "AREAL A-340AT",                                               .zones =  1, .avg_spt = 150, .heads =  6, .rpm = 3600, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-1] Areal A-520AT",                            .internal_name = "A520AT",       .model = "AREAL A-520AT",                                               .zones =  1, .avg_spt = 133, .heads =  6, .rpm = 3600, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Areal A-525AT",                            .internal_name = "A525AT",       .model = "AREAL A-525AT",                                               .zones =  2, .avg_spt = 150, .heads =  8, .rpm = 3600, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-1] Areal MD-2060AT",                          .internal_name = "MD2060AT",     .model = "AREAL MD-2060",                                               .zones =  1, .avg_spt =  90, .heads =  2, .rpm = 2087, .full_stroke_ms = 35, .track_seek_ms = 4,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Areal MD-2065AT",                          .internal_name = "MD2065AT",     .model = "AREAL MD-2065",                                               .zones =  1, .avg_spt =  50, .heads =  2, .rpm = 2504, .full_stroke_ms = 35, .track_seek_ms = 4,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Areal MD-2080AT",                          .internal_name = "MD2080AT",     .model = "AREAL MD-2080",                                               .zones =  1, .avg_spt =  90, .heads =  2, .rpm = 2087, .full_stroke_ms = 35, .track_seek_ms = 4,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Areal MD-2085AT",                          .internal_name = "MD2085AT",     .model = "AREAL MD-2085",                                               .zones =  1, .avg_spt =  60, .heads =  2, .rpm = 2504, .full_stroke_ms = 35, .track_seek_ms = 4,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Areal MD-2100AT",                          .internal_name = "MD2100AT",     .model = "AREAL MD-2100",                                               .zones =  1, .avg_spt =  90, .heads =  2, .rpm = 2504, .full_stroke_ms = 35, .track_seek_ms = 4,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Avastor DSP-2022A",                        .internal_name = "DSP2022A",     .model = "DEC DSP2022A",                                                .zones =  1, .avg_spt = 110, .heads =  5, .rpm = 5400, .full_stroke_ms = 34, .track_seek_ms = 2,   .rcache_num_seg =  1, .rcache_seg_size =  512, .max_multiple =  8 }, // Digital OEM(?)
    { .name = "[ATA-1] Brand Tech BT-9121A",                      .internal_name = "BT9121A",      .model = "BRAND BT-9121A",                                              .zones =  1, .avg_spt = 165, .heads =  5, .rpm = 3565, .full_stroke_ms = 33, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-1] Brand Tech BT-9170A",                      .internal_name = "BT9170A",      .model = "BRAND BT-9170A",                                              .zones =  1, .avg_spt = 165, .heads =  7, .rpm = 3565, .full_stroke_ms = 33, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-1] Brand Tech BT-9220A",                      .internal_name = "BT9220A",      .model = "BRAND BT-9220A",                                              .zones =  1, .avg_spt = 165, .heads =  9, .rpm = 3565, .full_stroke_ms = 33, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-1] CMS K040A3-N",                             .internal_name = "K040A3N",      .model = "K040A3 N",                                                    .zones =  1, .avg_spt =  25, .heads =  2, .rpm = 3557, .full_stroke_ms = 40, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =    8, .max_multiple =  8 },
    { .name = "[ATA-1] CMS H40CQ285D-P",                          .internal_name = "H40CQ285DP",   .model = "H40CQ285D P",                                                 .zones =  2, .avg_spt =  29, .heads =  4, .rpm = 3600, .full_stroke_ms = 40, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =    8, .max_multiple =  8 },
    { .name = "[ATA-1] Conner CP-2022AT",                         .internal_name = "CP2022AT",     .model = "CP2022",                                                      .zones =  1, .avg_spt =  23, .heads =  2, .rpm = 3433, .full_stroke_ms = 40, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =    8, .max_multiple =  8 },
    { .name = "[ATA-1] Conner CP-2034",                           .internal_name = "CP2034",       .model = "CP2034",                                                      .zones =  1, .avg_spt =  19, .heads =  2, .rpm = 3486, .full_stroke_ms = 34, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Conner CP-2040",                           .internal_name = "CP2040",       .model = "CP2040",                                                      .zones =  1, .avg_spt =  19, .heads =  4, .rpm = 3486, .full_stroke_ms = 34, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Conner CP-2044",                           .internal_name = "CP2044",       .model = "CP2044",                                                      .zones =  1, .avg_spt =  19, .heads =  4, .rpm = 3486, .full_stroke_ms = 34, .track_seek_ms = 12,  .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Conner CP-2044PK",                         .internal_name = "CP2044PK",     .model = "Conner Peripherals 40MB - CP2044",                            .zones =  1, .avg_spt =  19, .heads =  4, .rpm = 3486, .full_stroke_ms = 34, .track_seek_ms = 10,  .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 }, // Later version of Conner CP-2044
    { .name = "[ATA-1] Conner CP-2064",                           .internal_name = "CP2064",       .model = "CP2064",                                                      .zones =  1, .avg_spt =  19, .heads =  4, .rpm = 3486, .full_stroke_ms = 34, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Conner CP-2084",                           .internal_name = "CP2084",       .model = "CP2084",                                                      .zones =  1, .avg_spt =  19, .heads =  4, .rpm = 3486, .full_stroke_ms = 34, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Conner CP-2088",                           .internal_name = "CP2088",       .model = "CP2088",                                                      .zones =  1, .avg_spt =  19, .heads =  4, .rpm = 3486, .full_stroke_ms = 34, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Conner CP-3000",                           .internal_name = "CP3000",       .model = "Conner Peripherals 42MB - CP3000",                            .zones =  1,  .avg_spt = 28, .heads =  2, .rpm = 3557, .full_stroke_ms = 40, .track_seek_ms = 11,  .rcache_num_seg =  1, .rcache_seg_size =    8, .max_multiple =  8 },
    { .name = "[ATA-1] Conner CP-3024",                           .internal_name = "CP3024",       .model = "Conner Peripherals 20MB - CP3024",                            .zones =  1, .avg_spt =  33, .heads =  2, .rpm = 3500, .full_stroke_ms = 50, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =    8, .max_multiple =  8 }, // Needed for GRiDcase 1520 to work
    { .name = "[ATA-1] Conner CP-3044",                           .internal_name = "CP3044",       .model = "Conner Peripherals 40MB - CP3044",                            .zones =  1, .avg_spt =  40, .heads =  2, .rpm = 3500, .full_stroke_ms = 50, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =    8, .max_multiple =  8 }, // Needed for GRiDcase 1520 to work
    { .name = "[ATA-1] Conner CP-30064",                          .internal_name = "CP30064",      .model = "Conner Peripherals 60MB - CP30064",                           .zones =  1, .avg_spt =  50, .heads =  2, .rpm = 3600, .full_stroke_ms = 40, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-1] Conner CP-30084",                          .internal_name = "CP30084",      .model = "Conner Peripherals 84MB - CP30084",                           .zones =  1, .avg_spt =  70, .heads =  2, .rpm = 3595, .full_stroke_ms = 50, .track_seek_ms = 3,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Conner CP-3104",                           .internal_name = "CP3104",       .model = "Conner Peripherals 104MB - CP3104",                           .zones =  1, .avg_spt =  33, .heads =  8, .rpm = 3500, .full_stroke_ms = 45, .track_seek_ms = 8,   .rcache_num_seg =  4, .rcache_seg_size =    8, .max_multiple =  8 }, // Needed for GRiDcase 1520 to work
    { .name = "[ATA-1] Conner CP-30124",                          .internal_name = "CP30124",      .model = "Conner Peripherals 124MB - CP30124",                          .zones =  1, .avg_spt =  40, .heads =  2, .rpm = 4542, .full_stroke_ms = 26, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Conner CP-30174",                          .internal_name = "CP30174",      .model = "Conner Peripherals 170MB - CP30174",                          .zones =  1, .avg_spt =  70, .heads =  4, .rpm = 3822, .full_stroke_ms = 42, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Conner CP-30204",                          .internal_name = "CP30204",      .model = "Conner Peripherals 204MB - CP30204",                          .zones =  1, .avg_spt = 127, .heads =  4, .rpm = 4498, .full_stroke_ms = 30, .track_seek_ms = 7,   .rcache_num_seg =  4, .rcache_seg_size =  256, .max_multiple =  8 },
    { .name = "[ATA-1] Conner CP-3364",                           .internal_name = "CP3364",       .model = "Conner Peripherals 360MB - CP3364",                           .zones =  4, .avg_spt = 120, .heads =  8, .rpm = 4498, .full_stroke_ms = 40, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  256, .max_multiple =  8 },
    { .name = "[ATA-1] Conner CFS-210A",                          .internal_name = "CFS210A",      .model = "Conner Peripherals 210MB - CFS210A",                          .zones =  1, .avg_spt = 140, .heads =  2, .rpm = 3600, .full_stroke_ms = 33, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Conner CFN-250A",                          .internal_name = "CFN250A",      .model = "Conner Peripherals 250MB - CFN250A",                          .zones =  4, .avg_spt = 120, .heads =  6, .rpm = 4498, .full_stroke_ms = 34, .track_seek_ms = 2.6, .rcache_num_seg =  4, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Conner CFN-340A",                          .internal_name = "CFN340A",      .model = "Conner Peripherals 340MB - CFN340A",                          .zones =  4, .avg_spt = 130, .heads =  6, .rpm = 4500, .full_stroke_ms = 34, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Conner CFS-420A",                          .internal_name = "CFS420A",      .model = "Conner Peripherals 420MB - CFS420A",                          .zones =  1, .avg_spt =  40, .heads =  2, .rpm = 3600, .full_stroke_ms = 33, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] CSC GD210A",                               .internal_name = "GD210A",       .model = "GD210A",                                                      .zones =  1, .avg_spt =  45, .heads =  1, .rpm = 3500, .full_stroke_ms = 50, .track_seek_ms = 4,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] HP Kittyhawk",                             .internal_name = "C3014A",       .model = "HP C3014A",                                                   .zones =  6, .avg_spt =  80, .heads =  3, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =   16, .max_multiple =  8 },
    { .name = "[ATA-1] IBM WDA-L40",                              .internal_name = "WDAL80",       .model = "WDA-L40",                                                     .zones =  1, .avg_spt =  95, .heads =  1, .rpm = 3600, .full_stroke_ms = 32, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] IBM WDA-L80",                              .internal_name = "WDAL80",       .model = "WDA-L80",                                                     .zones =  1, .avg_spt =  95, .heads =  2, .rpm = 3600, .full_stroke_ms = 32, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] IBM WDA-L160",                             .internal_name = "WDAL160",      .model = "WDA-L160",                                                    .zones =  1, .avg_spt =  95, .heads =  4, .rpm = 3600, .full_stroke_ms = 32, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] IBM WDA-240",                              .internal_name = "WDA240",       .model = "WDA-240",                                                     .zones =  2, .avg_spt =  85, .heads =  2, .rpm = 3600, .full_stroke_ms = 30, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] IBM WDA-260",                              .internal_name = "WDA260",       .model = "WDA-260",                                                     .zones =  2, .avg_spt =  85, .heads =  3, .rpm = 3600, .full_stroke_ms = 30, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] IBM WDA-280",                              .internal_name = "WDA280",       .model = "WDA-280",                                                     .zones =  2, .avg_spt =  65, .heads =  4, .rpm = 3600, .full_stroke_ms = 30, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] IBM WDA-2120",                             .internal_name = "WDA2120",      .model = "WDA-2120",                                                    .zones =  2, .avg_spt =  65, .heads =  4, .rpm = 3600, .full_stroke_ms = 30, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] IBM WDA-380",                              .internal_name = "WDA380",       .model = "WDA-380",                                                     .zones =  3, .avg_spt =  85, .heads =  1, .rpm = 3600, .full_stroke_ms = 31, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] IBM WDA-3160",                             .internal_name = "WDA3160",      .model = "WDA-3160",                                                    .zones =  3, .avg_spt =  85, .heads =  2, .rpm = 3600, .full_stroke_ms = 31, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] IBM WAKASA (H2172-A2)",                    .internal_name = "H2172A2",      .model = "H2172-A2",                                                    .zones =  1, .avg_spt = 140, .heads =  2, .rpm = 3800, .full_stroke_ms = 32, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] IBM WAKASA (H2258-A3)",                    .internal_name = "H2258A3",      .model = "H2258-A3",                                                    .zones =  1, .avg_spt = 140, .heads =  3, .rpm = 3800, .full_stroke_ms = 32, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] IBM WAKASA (H2344-A4)",                    .internal_name = "H2344A4",      .model = "H2344-A4",                                                    .zones =  1, .avg_spt = 140, .heads =  4, .rpm = 3800, .full_stroke_ms = 32, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] IBM H3133-A2",                             .internal_name = "H3133A2",      .model = "H3133-A2",                                                    .zones =  1, .avg_spt =  40, .heads =  2, .rpm = 3600, .full_stroke_ms = 32, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =   96, .max_multiple =  8 },
    { .name = "[ATA-1] IBM H3171-A2",                             .internal_name = "H3171A2",      .model = "H3171-A2",                                                    .zones =  1, .avg_spt =  40, .heads =  2, .rpm = 3600, .full_stroke_ms = 32, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =   96, .max_multiple =  8 },
    { .name = "[ATA-1] IBM H3256-A3",                             .internal_name = "H3256A3",      .model = "H3256-A3",                                                    .zones =  1, .avg_spt =  40, .heads =  2, .rpm = 3600, .full_stroke_ms = 32, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =   96, .max_multiple =  8 },
    { .name = "[ATA-1] IBM H3342-A4",                             .internal_name = "H3342A4",      .model = "H3342-A4",                                                    .zones =  1, .avg_spt =  40, .heads =  2, .rpm = 3600, .full_stroke_ms = 32, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =   96, .max_multiple =  8 },
    { .name = "[ATA-1] IBM Deskstar (DSAA-3270)",                 .internal_name = "DSAA3270ATA",  .model = "DSAA-3270",                                                   .zones =  2, .avg_spt = 130, .heads =  2, .rpm = 4500, .full_stroke_ms = 31, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =   96, .max_multiple =  8 }, // ATA version of DSAA-3270
    { .name = "[ATA-1] IBM Deskstar (DSAA-3360)",                 .internal_name = "DSAA3360",     .model = "DSAA-3360",                                                   .zones =  2, .avg_spt = 130, .heads =  2, .rpm = 4500, .full_stroke_ms = 31, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =   96, .max_multiple =  8 },
    { .name = "[ATA-1] IBM Deskstar (DSAA-3540)",                 .internal_name = "DSAA3540",     .model = "DSAA-3540",                                                   .zones =  2, .avg_spt = 130, .heads =  3, .rpm = 4500, .full_stroke_ms = 31, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =   96, .max_multiple =  8 },
    { .name = "[ATA-1] IBM Deskstar (DSAA-3720)",                 .internal_name = "DSAA3720",     .model = "DSAA-3720",                                                   .zones =  2, .avg_spt = 130, .heads =  3, .rpm = 4500, .full_stroke_ms = 31, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =   96, .max_multiple =  8 },
    { .name = "[ATA-1] Kalok KL343",                              .internal_name = "KL343",        .model = "KALOK KL-343",                                                .zones =  1, .avg_spt =  80, .heads =  6, .rpm = 3600, .full_stroke_ms = 50, .track_seek_ms = 2,   .rcache_num_seg =  1, .rcache_seg_size =    8, .max_multiple =  8 },
    { .name = "[ATA-1] Kalok KL3100",                             .internal_name = "KL3100",       .model = "KALOK KL-3100",                                               .zones =  1, .avg_spt = 100, .heads =  6, .rpm = 3662, .full_stroke_ms = 50, .track_seek_ms = 2,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Maxtor 7040AT",                            .internal_name = "7040AT",       .model = "Maxtor 7040 AT",                                              .zones =  1, .avg_spt =  72, .heads =  2, .rpm = 3703, .full_stroke_ms = 40, .track_seek_ms = 5.3, .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Maxtor 7060AT",                            .internal_name = "7060AT",       .model = "Maxtor 7060 AT",                                              .zones =  1, .avg_spt =  62, .heads =  2, .rpm = 3524, .full_stroke_ms = 30, .track_seek_ms = 3.6, .rcache_num_seg =  1, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-1] Maxtor 7080AT",                            .internal_name = "7080AT",       .model = "Maxtor 7080 AT",                       .version = "A40PVY3S", .zones =  1, .avg_spt =  72, .heads =  4, .rpm = 3703, .full_stroke_ms = 40, .track_seek_ms = 6,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Maxtor 7120AT",                            .internal_name = "7120AT",       .model = "Maxtor 7120 AT",                                              .zones =  1, .avg_spt =  54, .heads =  2, .rpm = 3524, .full_stroke_ms = 27, .track_seek_ms = 4,   .rcache_num_seg =  1, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-1] Maxtor 7131AT",                            .internal_name = "7131AT",       .model = "Maxtor 7131 AT",                                              .zones =  2, .avg_spt =  54, .heads =  2, .rpm = 3551, .full_stroke_ms = 27, .track_seek_ms = 4.5, .rcache_num_seg =  1, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-1] Maxtor 7213AT",                            .internal_name = "7213AT",       .model = "Maxtor 7213 AT",                                              .zones =  4, .avg_spt = 155, .heads =  4, .rpm = 3551, .full_stroke_ms = 28, .track_seek_ms = 6.5, .rcache_num_seg =  1, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-1] Maxtor 7245AT",                            .internal_name = "7245AT",       .model = "Maxtor 7245 AT",                                              .zones =  4, .avg_spt = 149, .heads =  4, .rpm = 3551, .full_stroke_ms = 27, .track_seek_ms = 4.4, .rcache_num_seg =  8, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-1] NEC D3766",                                .internal_name = "D3766",        .model = "D3766",                                                       .zones =  1, .avg_spt =  70, .heads =  2, .rpm = 4500, .full_stroke_ms = 40, .track_seek_ms = 4,   .rcache_num_seg =  1, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-1] PrairieTek 120AT",                         .internal_name = "P120AT",       .model = "PRAIRIE 120AT",                                               .zones =  1, .avg_spt =  33, .heads =  1, .rpm = 3307, .full_stroke_ms = 50, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] PrairieTek 220AT",                         .internal_name = "P220AT",       .model = "PRAIRIE 220AT",                                               .zones =  1, .avg_spt =  33, .heads =  2, .rpm = 3307, .full_stroke_ms = 33, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] PrairieTek 24xAT",                         .internal_name = "P24XAT",       .model = "PRAIRIE 242AT",                                               .zones =  1, .avg_spt =  33, .heads =  4, .rpm = 3307, .full_stroke_ms = 33, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Quantum ProDrive LPS (52AT)",              .internal_name = "LPS52AT",      .model = "QUANTUM PRODRIVE 52AT",                                       .zones =  1, .avg_spt =  70, .heads =  1, .rpm = 3600, .full_stroke_ms = 45, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-1] Quantum ProDrive LPS (105AT)",             .internal_name = "LPS105AT",     .model = "QUANTUM PRODRIVE 105AT",                                      .zones =  1, .avg_spt =  70, .heads =  2, .rpm = 3662, .full_stroke_ms = 45, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-1] Quantum ProDrive LPS (120AT)",             .internal_name = "GM12A012",     .model = "QUANTUM PRODRIVE 120AT",                                      .zones =  2, .avg_spt =  50, .heads =  3, .rpm = 3605, .full_stroke_ms = 45, .track_seek_ms = 4,   .rcache_num_seg =  1, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-1] Quantum ProDrive ELS (170AT)",             .internal_name = "ELS170AT",     .model = "QUANTUM PRODRIVE 170AT",                                      .zones =  2, .avg_spt =  70, .heads =  4, .rpm = 3663, .full_stroke_ms = 28, .track_seek_ms = 5.5, .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Quantum GoDrive GU256AT",                  .internal_name = "GU25A011",     .model = "QUANTUM GODRIVE 256AT",                                       .zones =  2, .avg_spt = 170, .heads =  4, .rpm = 4500, .full_stroke_ms = 33, .track_seek_ms = 5,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-1] Seagate ST325A",                           .internal_name = "ST325A",       .model = "ST325A",                                                      .zones =  1, .avg_spt =  63, .heads =  1, .rpm = 3048, .full_stroke_ms = 33, .track_seek_ms = 4.4, .rcache_num_seg =  1, .rcache_seg_size =    8, .max_multiple =  8 }, // There is also a XTA version
    { .name = "[ATA-1] Seagate ST351A",                           .internal_name = "ST351A",       .model = "ST351A",                                                      .zones =  2, .avg_spt =  63, .heads =  1, .rpm = 3048, .full_stroke_ms = 33, .track_seek_ms = 4.4, .rcache_num_seg =  1, .rcache_seg_size =    8, .max_multiple =  8 }, // There is also a XTA version
    { .name = "[ATA-1] Seagate ST1102A",                          .internal_name = "ST1102A",      .model = "ST1102A",                                                     .zones =  1, .avg_spt =  70, .heads =  1, .rpm = 3528, .full_stroke_ms = 33, .track_seek_ms = 2.6, .rcache_num_seg =  1, .rcache_seg_size =    8, .max_multiple =  8 },
    { .name = "[ATA-1] Seagate ST1144A",                          .internal_name = "ST1144A",      .model = "ST1144A",                                                     .zones =  2, .avg_spt =  70, .heads =  2, .rpm = 3528, .full_stroke_ms = 33, .track_seek_ms = 2.6, .rcache_num_seg =  1, .rcache_seg_size =    8, .max_multiple =  8 },
    { .name = "[ATA-1] Seagate ST9051A",                          .internal_name = "ST9051A",      .model = "ST9051A",                                                     .zones =  1, .avg_spt =  60, .heads =  1, .rpm = 3811, .full_stroke_ms = 32, .track_seek_ms = 4.4, .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Seagate ST9080A",                          .internal_name = "ST9080A",      .model = "ST9080A",                                                     .zones =  2, .avg_spt =  60, .heads =  1, .rpm = 3811, .full_stroke_ms = 32, .track_seek_ms = 4.4, .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Seagate ST3096A",                          .internal_name = "ST3096A",      .model = "ST3096A",                                                     .zones =  1, .avg_spt =  33, .heads =  1, .rpm = 3211, .full_stroke_ms = 32, .track_seek_ms = 3.3, .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Seagate ST3120A",                          .internal_name = "ST3120A",      .model = "ST3120A",                                                     .zones =  1, .avg_spt =  33, .heads =  2, .rpm = 3211, .full_stroke_ms = 32, .track_seek_ms = 3.3, .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Seagate ST3145A",                          .internal_name = "ST3145A",      .model = "ST3145A",                                                     .zones =  1, .avg_spt =  40, .heads =  2, .rpm = 3811, .full_stroke_ms = 32, .track_seek_ms = 4.3, .rcache_num_seg =  1, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Seagate ST9145AG",                         .internal_name = "ST9145AG",     .model = "ST9145AG",                                                    .zones =  2, .avg_spt = 160, .heads =  2, .rpm = 3811, .full_stroke_ms = 32, .track_seek_ms = 4.3, .rcache_num_seg =  4, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Seagate ST3195A",                          .internal_name = "ST3195A",      .model = "ST3195A",                                                     .zones =  2, .avg_spt =  70, .heads =  4, .rpm = 3811, .full_stroke_ms = 32, .track_seek_ms = 5.5, .rcache_num_seg =  1, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-1] Seagate ST9235AG",                         .internal_name = "ST9235AG",     .model = "ST9235AG",                                                    .zones =  2, .avg_spt = 160, .heads =  3, .rpm = 3811, .full_stroke_ms = 32, .track_seek_ms = 4.4, .rcache_num_seg =  4, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Seagate ST3243A",                          .internal_name = "ST3243A",      .model = "ST3243A",                                                     .zones =  2, .avg_spt =  40, .heads =  4, .rpm = 3811, .full_stroke_ms = 32, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Seagate ST3290A",                          .internal_name = "ST3290A",      .model = "ST3290A",                                                     .zones =  2, .avg_spt =  60, .heads =  4, .rpm = 3811, .full_stroke_ms = 32, .track_seek_ms = 4.4, .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-1] Western Digital Caviar 140",               .internal_name = "AC140",        .model = "AC140",                                                       .zones =  2, .avg_spt =  70, .heads =  2, .rpm = 3551, .full_stroke_ms = 28, .track_seek_ms = 6,   .rcache_num_seg =  8, .rcache_seg_size =    8, .max_multiple =  8 },
    { .name = "[ATA-1] Western Digital Caviar 1170",              .internal_name = "AC1170",       .model = "WDC AC1170F",                                                 .zones =  4, .avg_spt =  30, .heads =  1, .rpm = 3314, .full_stroke_ms = 33, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-1] Western Digital Caviar 1210",              .internal_name = "AC1210",       .model = "WDC AC1210F",                                                 .zones =  4, .avg_spt =  30, .heads =  2, .rpm = 3314, .full_stroke_ms = 33, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-1] Western Digital Caviar 1365",              .internal_name = "AC1365",       .model = "WDC AC1365F",                                                 .zones =  2, .avg_spt = 135, .heads =  2, .rpm = 4200, .full_stroke_ms = 28, .track_seek_ms = 2.8, .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-1] Western Digital Caviar 1425",              .internal_name = "AC1425",       .model = "WDC AC1425F",                                                 .zones =  4, .avg_spt = 120, .heads =  2, .rpm = 4200, .full_stroke_ms = 30, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-1] Western Digital Caviar 280",               .internal_name = "AC280",        .model = "AC280",                                                       .zones =  4, .avg_spt =  70, .heads =  4, .rpm = 3595, .full_stroke_ms = 28, .track_seek_ms = 6,   .rcache_num_seg =  8, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Western Digital Caviar 2120",              .internal_name = "AC2120",       .model = "WDC AC2120M",                                                 .zones =  4, .avg_spt =  40, .heads =  2, .rpm = 3605, .full_stroke_ms = 28, .track_seek_ms = 2.8, .rcache_num_seg =  8, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-1] Western Digital Caviar 2340",              .internal_name = "AC2340",       .model = "WDC AC2340H",                                                 .zones =  4, .avg_spt = 130, .heads =  2, .rpm = 3320, .full_stroke_ms = 28, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-1] Western Digital Caviar 2420",              .internal_name = "AC2420",       .model = "WDC AC2420F",                                                 .zones =  4, .avg_spt = 130, .heads =  2, .rpm = 3314, .full_stroke_ms = 28, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Conner CFS-270A",                          .internal_name = "CFS270A",      .model = "Conner Peripherals 270MB - CFA270A",                          .zones =  2, .avg_spt = 150, .heads =  2, .rpm = 3400, .full_stroke_ms = 34, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-2] Conner CFA-425A",                          .internal_name = "CFA425A",      .model = "Conner Peripherals 426MB - CFA425A",                          .zones =  2, .avg_spt = 120, .heads =  2, .rpm = 4500, .full_stroke_ms = 38, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-2] Conner CFA-540A",                          .internal_name = "CFA540A",      .model = "Conner Peripherals 540MB - CFA540A",                          .zones =  2, .avg_spt = 120, .heads =  4, .rpm = 3551, .full_stroke_ms = 31, .track_seek_ms = 4.3, .rcache_num_seg =  4, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-2] Conner CFS-635A",                          .internal_name = "CFS635A",      .model = "Conner Peripherals 635MB - CFS635A",                          .zones =  4, .avg_spt = 140, .heads =  2, .rpm = 3600, .full_stroke_ms = 38, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-2] Conner CFA-810A",                          .internal_name = "CFA810A",      .model = "Conner Peripherals 810MB - CFA810A",                          .zones =  4, .avg_spt = 125, .heads =  6, .rpm = 4500, .full_stroke_ms = 40, .track_seek_ms = 2.5, .rcache_num_seg =  4, .rcache_seg_size =  256, .max_multiple =  8 },
    { .name = "[ATA-2] Conner CFS-850A",                          .internal_name = "CFS850A",      .model = "Conner Peripherals 850MB - CFS850A",                          .zones =  4, .avg_spt = 140, .heads =  4, .rpm = 3600, .full_stroke_ms = 38, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-2] Conner CFS-1080A (Diskstor)",              .internal_name = "CFS1080A",     .model = "Conner Peripherals 1080MB - CFS1080A",                        .zones =  4, .avg_spt = 205, .heads =  8, .rpm = 4500, .full_stroke_ms = 37, .track_seek_ms = 2.5, .rcache_num_seg =  4, .rcache_seg_size =  256, .max_multiple = 16 },
    { .name = "[ATA-2] Conner CFS-1081A (Cabo)",                  .internal_name = "CFS1081A",     .model = "Conner Peripherals 1080MB - CFS1081A",                        .zones =  4, .avg_spt = 140, .heads =  4, .rpm = 3600, .full_stroke_ms = 38, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-2] Conner CFS-1275A",                         .internal_name = "CFS1275A",     .model = "Conner Peripherals 1275MB - CFS1275A",                        .zones =  4, .avg_spt = 140, .heads =  6, .rpm = 3600, .full_stroke_ms = 38, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-2] Conner CFS-1621A",                         .internal_name = "CFS1621A",     .model = "Conner Peripherals 1621MB - CFS1621A",                        .zones =  4, .avg_spt = 140, .heads =  6, .rpm = 3600, .full_stroke_ms = 38, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-2] Fujitsu Picobird 7 540",                   .internal_name = "M1603TAU",     .model = "FUJITSU M1603TAU",                                            .zones =  4, .avg_spt = 100, .heads =  3, .rpm = 5400, .full_stroke_ms = 38, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  256, .max_multiple =  8 },
    { .name = "[ATA-2] Fujitsu Picobird 7 1080",                  .internal_name = "M1606TAU",     .model = "FUJITSU M1606TAU",                                            .zones =  4, .avg_spt = 100, .heads =  6, .rpm = 5400, .full_stroke_ms = 38, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  256, .max_multiple =  8 },
    { .name = "[ATA-2] Fujitsu Picobird 9 1.28GB",                .internal_name = "M1636TAU",     .model = "FUJITSU M1636TAU",                                            .zones =  2, .avg_spt = 110, .heads =  2, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Fujitsu Picobird 9 1.70GB",                .internal_name = "M1623TAU",     .model = "FUJITSU M1623TAU",                                            .zones =  4, .avg_spt = 110, .heads =  3, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Fujitsu Picobird 9 1.9GB",                 .internal_name = "M1637TAU",     .model = "FUJITSU M1637TAU",                                            .zones =  4, .avg_spt = 110, .heads =  3, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Fujitsu Picobird 9 2.11GB",                .internal_name = "M1624TAU",     .model = "FUJITSU M1624TAU",                                            .zones =  2, .avg_spt = 110, .heads =  4, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Fujitsu Picobird 9 2.57GB",                .internal_name = "M1638TAU",     .model = "FUJITSU M1638TAU",                                            .zones =  4, .avg_spt = 110, .heads =  4, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Fujitsu M2714TAM",                         .internal_name = "M2714TAM",     .model = "FUJITSU M2714TAM",                                            .zones =  1, .avg_spt = 110, .heads =  1, .rpm = 3600, .full_stroke_ms = 28, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Fujitsu M1612TAU",                         .internal_name = "M1612TAU",     .model = "FUJITSU M1612TAU",                                            .zones =  1, .avg_spt = 110, .heads =  1, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Fujitsu M1614TAU",                         .internal_name = "M1614TAU",     .model = "FUJITSU M1614TAU",                                            .zones =  1, .avg_spt = 110, .heads =  2, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Hitachi DK221A-34",                        .internal_name = "DK221A34",     .model = "HITACHI DK221A-34",                                           .zones =  2, .avg_spt = 120, .heads =  4, .rpm = 4464, .full_stroke_ms = 30, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-2] Hitachi DK211A-51",                        .internal_name = "DK211A51",     .model = "HITACHI DK211A-51",                                           .zones =  2, .avg_spt = 120, .heads =  6, .rpm = 4464, .full_stroke_ms = 30, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-2] Hitachi DK222A-54",                        .internal_name = "DK222A54",     .model = "HITACHI DK222A-54",                                           .zones =  4, .avg_spt = 120, .heads =  4, .rpm = 4464, .full_stroke_ms = 33, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-2] Hitachi DK211A-68",                        .internal_name = "DK211A68",     .model = "HITACHI DK211A-68",                                           .zones =  2, .avg_spt = 120, .heads =  8, .rpm = 4464, .full_stroke_ms = 30, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-2] Hitachi DK212A-81",                        .internal_name = "DK212A81",     .model = "HITACHI DK212A-81",                                           .zones =  4, .avg_spt = 120, .heads =  6, .rpm = 4464, .full_stroke_ms = 33, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-2] Hitachi DK212A-10",                        .internal_name = "DK212A10",     .model = "HITACHI DK212A-10",                                           .zones =  4, .avg_spt = 120, .heads =  8, .rpm = 4464, .full_stroke_ms = 33, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-2] Hitachi DK213A-13",                        .internal_name = "DK213A13",     .model = "HITACHI DK213A-13",                                           .zones =  8, .avg_spt = 120, .heads = 10, .rpm = 4464, .full_stroke_ms = 30, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Hitachi DK213A-18",                        .internal_name = "DK213A18",     .model = "HITACHI DK213A-18",                                           .zones =  8, .avg_spt = 120, .heads = 10, .rpm = 4464, .full_stroke_ms = 30, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Hitachi DK223A-11",                        .internal_name = "DK223A11",     .model = "HITACHI DK223A-11",                                           .zones =  4, .avg_spt = 120, .heads =  4, .rpm = 4464, .full_stroke_ms = 30, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Hitachi DK224A-14",                        .internal_name = "DK224A14",     .model = "HITACHI DK224A-14",                                           .zones =  4, .avg_spt = 120, .heads =  6, .rpm = 4464, .full_stroke_ms = 33, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] IBM Travelstar 270",                       .internal_name = "DHAA2270",     .model = "DHAA-2270",                                                   .zones =  1, .avg_spt = 140, .heads =  1, .rpm = 3800, .full_stroke_ms = 33, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-2] IBM Travelstar 405",                       .internal_name = "DHAA2405",     .model = "DHAA-2405",                                                   .zones =  1, .avg_spt = 140, .heads =  2, .rpm = 3800, .full_stroke_ms = 33, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-2] IBM Travelstar 540",                       .internal_name = "DHAA2540",     .model = "DHAA-2540",                                                   .zones =  1, .avg_spt = 140, .heads =  3, .rpm = 3800, .full_stroke_ms = 33, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-2] IBM Travelstar 810",                       .internal_name = "DVAA2810",     .model = "DVAA-2810",                                                   .zones =  2, .avg_spt = 145, .heads =  2, .rpm = 3800, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-2] IBM Travelstar LP 360",                    .internal_name = "DBOA2360",     .model = "DBOA-2360",                                                   .zones =  2, .avg_spt = 130, .heads =  1, .rpm = 4000, .full_stroke_ms = 30, .track_seek_ms = 5,   .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-2] IBM Travelstar LP 528",                    .internal_name = "DBOA2528",     .model = "DBOA-2528",                                                   .zones =  2, .avg_spt = 130, .heads =  2, .rpm = 4000, .full_stroke_ms = 30, .track_seek_ms = 5,   .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-2] IBM Travelstar LP 540",                    .internal_name = "DBOA2540",     .model = "DBOA-2540",                                                   .zones =  4, .avg_spt = 135, .heads =  1, .rpm = 4000, .full_stroke_ms = 30, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-2] IBM Travelstar LP 720",                    .internal_name = "DBOA2720",     .model = "DBOA-2720",                                                   .zones =  4, .avg_spt = 135, .heads =  2, .rpm = 4000, .full_stroke_ms = 30, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-2] IBM Travelstar 2LP 540",                   .internal_name = "DSOA20540",    .model = "DSOA-20540",                                                  .zones =  4, .avg_spt = 135, .heads =  2, .rpm = 4000, .full_stroke_ms = 30, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] IBM Travelstar 2LP 810",                   .internal_name = "DSOA20810",    .model = "DSOA-20810",                                                  .zones =  4, .avg_spt = 135, .heads =  4, .rpm = 4000, .full_stroke_ms = 30, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] IBM Travelstar 2LP 1080",                  .internal_name = "DSOA21080",    .model = "DSOA-21080",                                                  .zones =  4, .avg_spt = 135, .heads =  4, .rpm = 4000, .full_stroke_ms = 30, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] IBM Travelstar 3LP 1.0",                   .internal_name = "DMCA21080",    .model = "IBM-DMCA-21080",                                              .zones =  8, .avg_spt = 130, .heads =  2, .rpm = 4000, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =   96, .max_multiple = 16 },
    { .name = "[ATA-2] IBM Travelstar 3LP 1.4",                   .internal_name = "DMCA21440",    .model = "IBM-DMCA-21440",                                              .zones =  8, .avg_spt = 130, .heads =  2, .rpm = 4000, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =   96, .max_multiple = 16 },
    { .name = "[ATA-2] IBM Travelstar VP 1.2",                    .internal_name = "DDLA21215",    .model = "IBM-DDLA-21215",                                              .zones =  4, .avg_spt = 130, .heads =  3, .rpm = 4000, .full_stroke_ms = 23, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =   96, .max_multiple = 16 },
    { .name = "[ATA-2] IBM Travelstar VP 1.6",                    .internal_name = "DDLA21620",    .model = "IBM-DDLA-21620",                                              .zones =  4, .avg_spt = 130, .heads =  4, .rpm = 4000, .full_stroke_ms = 23, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =   96, .max_multiple = 16 },
    { .name = "[ATA-2] IBM Travelstar XP 2.1",                    .internal_name = "DCRA22160",    .model = "IBM-DCRA-22160",                                              .zones =  6, .avg_spt = 120, .heads =  3, .rpm = 4900, .full_stroke_ms = 21, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =   96, .max_multiple = 16 },
    { .name = "[ATA-2] IBM Travelstar XP (DPRA-20810)",           .internal_name = "DPRA20810",    .model = "IBM-DPRA-20810",                                              .zones =  6, .avg_spt = 125, .heads =  1, .rpm = 4900, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-2] IBM Travelstar XP (DPRA-21215)",           .internal_name = "DPRA21215",    .model = "IBM-DPRA-21215",                                              .zones =  6, .avg_spt = 125, .heads =  2, .rpm = 4900, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-2] IBM Travelstar 3XP (DLGA-22690)",          .internal_name = "DLGA22690",    .model = "IBM-DLGA-22690",                                              .zones =  8, .avg_spt = 125, .heads =  8, .rpm = 4000, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =   96, .max_multiple = 16 },
    { .name = "[ATA-2] IBM Travelstar 3XP (DLGA-23080)",          .internal_name = "DLGA23080",    .model = "IBM-DLGA-23080",                                              .zones =  8, .avg_spt = 125, .heads =  8, .rpm = 4000, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =   96, .max_multiple = 16 },
    { .name = "[ATA-2] IBM DALA-3540",                            .internal_name = "DALA3540",     .model = "IBM-DALA-3540",                                               .zones =  8, .avg_spt = 125, .heads =  3, .rpm = 4500, .full_stroke_ms = 25, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =   96, .max_multiple =  8 },
    { .name = "[ATA-2] IBM DJAA-31080",                           .internal_name = "DJAA31080",    .model = "IBM-DJAA-31080",                                              .zones =  8, .avg_spt = 135, .heads =  1, .rpm = 4500, .full_stroke_ms = 19, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =   96, .max_multiple = 16 },
    { .name = "[ATA-2] IBM DJAA-31270",                           .internal_name = "DJAA31270",    .model = "IBM-DJAA-31270",                                              .zones =  8, .avg_spt = 135, .heads =  3, .rpm = 4500, .full_stroke_ms = 19, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =   96, .max_multiple = 16 },
    { .name = "[ATA-2] IBM DJAA-31700",                           .internal_name = "DJAA31700",    .model = "IBM-DJAA-31700",                                              .zones =  8, .avg_spt = 135, .heads =  2, .rpm = 4500, .full_stroke_ms = 19, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =   96, .max_multiple = 16 },
    { .name = "[ATA-2] IBM Deskstar XP 540",                      .internal_name = "DPEA30540",    .model = "IBM-DPEA-30540",                                              .zones =  6, .avg_spt = 125, .heads =  1, .rpm = 5400, .full_stroke_ms = 31, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  448, .max_multiple =  8 },
    { .name = "[ATA-2] IBM Deskstar XP 810",                      .internal_name = "DPEA30810",    .model = "IBM-DPEA-30810",                                              .zones =  6, .avg_spt = 125, .heads =  2, .rpm = 5400, .full_stroke_ms = 31, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  448, .max_multiple =  8 },
    { .name = "[ATA-2] IBM Deskstar XP 1080",                     .internal_name = "DPEA31080",    .model = "IBM-DPEA-31080",                                              .zones =  6, .avg_spt = 125, .heads =  2, .rpm = 5400, .full_stroke_ms = 31, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  448, .max_multiple =  8 },
    { .name = "[ATA-2] IBM Deskstar 4 (DCAA-32880)",              .internal_name = "DCAA32880",    .model = "IBM-DCAA-32880",                                              .zones =  8, .avg_spt = 185, .heads =  2, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.7, .rcache_num_seg =  8, .rcache_seg_size =   96, .max_multiple = 16 },
    { .name = "[ATA-2] IBM Deskstar 4 (DCAA-33610)",              .internal_name = "DCAA33610",    .model = "IBM-DCAA-33610",                                              .zones =  8, .avg_spt = 185, .heads =  3, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.7, .rcache_num_seg =  8, .rcache_seg_size =   96, .max_multiple = 16 },
    { .name = "[ATA-2] IBM Deskstar 4 (DCAA-34330)",              .internal_name = "DCAA34330",    .model = "IBM-DCAA-34330",                                              .zones =  8, .avg_spt = 185, .heads =  3, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.7, .rcache_num_seg =  8, .rcache_seg_size =   96, .max_multiple = 16 },
    { .name = "[ATA-2] Maxtor 7540AV",                            .internal_name = "7540AV",       .model = "Maxtor 7540 AV",                                              .zones =  2, .avg_spt = 120, .heads =  4, .rpm = 3551, .full_stroke_ms = 31, .track_seek_ms = 4.3, .rcache_num_seg =  4, .rcache_seg_size =   32, .max_multiple =  8 },
    { .name = "[ATA-2] Maxtor 7546AT",                            .internal_name = "7546AT",       .model = "Maxtor 7546 AT",                                              .zones =  2, .avg_spt = 100, .heads =  4, .rpm = 4500, .full_stroke_ms = 28, .track_seek_ms = 2.3, .rcache_num_seg =  4, .rcache_seg_size =  256, .max_multiple =  8 },
    { .name = "[ATA-2] Maxtor 7850AV",                            .internal_name = "7850AV",       .model = "Maxtor 7850 AV",                                              .zones =  4, .avg_spt = 120, .heads =  4, .rpm = 3551, .full_stroke_ms = 31, .track_seek_ms = 3.7, .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-2] Maxtor 71000A",                            .internal_name = "71000A",       .model = "Maxtor 71000 A",                       .version = "16010002", .zones =  4, .avg_spt = 120, .heads =  4, .rpm = 4480, .full_stroke_ms = 12, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =   64, .max_multiple = 16 },
    { .name = "[ATA-2] Maxtor 71084A",                            .internal_name = "71084A",       .model = "Maxtor 71084 A",                       .version = "U20104JS", .zones =  4, .avg_spt = 120, .heads =  2, .rpm = 4480, .full_stroke_ms = 12, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =   64, .max_multiple = 16 },
    { .name = "[ATA-2] Maxtor 71084AP",                           .internal_name = "71084AP",      .model = "Maxtor 71084 AP",                                             .zones =  4, .avg_spt = 120, .heads =  2, .rpm = 4480, .full_stroke_ms = 12, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 }, // Second version of 71084A
    { .name = "[ATA-2] Maxtor 71336AP",                           .internal_name = "71336AP",      .model = "Maxtor 71336 AP",                                             .zones =  4, .avg_spt = 105, .heads =  4, .rpm = 4480, .full_stroke_ms = 12, .track_seek_ms = 3.4, .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-2] Maxtor 71626AP",                           .internal_name = "71626AP",      .model = "Maxtor 71626 AP",                                             .zones =  4, .avg_spt = 105, .heads =  4, .rpm = 4480, .full_stroke_ms = 12, .track_seek_ms = 3.4, .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-2] Quantum ProDrive LPS (270AT)",             .internal_name = "QT270AT",      .model = "QUANTUM PRODRIVE 270AT",                                      .zones =  2, .avg_spt = 130, .heads =  2, .rpm = 4500, .full_stroke_ms = 45, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum ProDrive LPS (420AT)",             .internal_name = "GM42A012",     .model = "QUANTUM PRODRIVE 420AT",                                      .zones =  2, .avg_spt = 130, .heads =  4, .rpm = 3600, .full_stroke_ms = 28, .track_seek_ms = 5,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum Lightning LT-365AT",               .internal_name = "LT36A461",     .model = "QUANTUM LIGHTNING 365AT",                                     .zones =  2, .avg_spt = 110, .heads =  2, .rpm = 4500, .full_stroke_ms = 30, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum Trailblazer TR-420AT",             .internal_name = "TR42A011",     .model = "QUANTUM TRAIBLAZER 420AT",                                    .zones =  4, .avg_spt = 140, .heads =  2, .rpm = 4500, .full_stroke_ms = 28, .track_seek_ms = 5,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum Trailblazer TR-635AT",             .internal_name = "TR63A011",     .model = "QUANTUM TRAIBLAZER 635AT",                                    .zones =  4, .avg_spt = 140, .heads =  3, .rpm = 4500, .full_stroke_ms = 28, .track_seek_ms = 5,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum Trailblazer TR-840AT",             .internal_name = "TR84A011",     .model = "QUANTUM TRAIBLAZER 840AT",                                    .zones =  4, .avg_spt = 140, .heads =  4, .rpm = 4500, .full_stroke_ms = 28, .track_seek_ms = 5,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum Fireball FB540AT",                 .internal_name = "FB54A011",     .model = "QUANTUM FIREBALL 540AT",                                      .zones =  2, .avg_spt = 120, .heads =  2, .rpm = 5400, .full_stroke_ms = 32, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum Fireball FB640AT",                 .internal_name = "FB64A341",     .model = "QUANTUM FIREBALL 640AT",                                      .zones =  2, .avg_spt = 120, .heads =  2, .rpm = 5400, .full_stroke_ms = 24, .track_seek_ms = 3.1, .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum Fireball FB1280AT",                .internal_name = "FB1280AT",     .model = "QUANTUM FIREBALL 1280AT",                                     .zones =  2, .avg_spt = 120, .heads =  4, .rpm = 5400, .full_stroke_ms = 24, .track_seek_ms = 3.1, .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum Fireball TM1080AT",                .internal_name = "TM10A462",     .model = "QUANTUM FIREBALL TM1.0A",                                     .zones =  4, .avg_spt = 120, .heads =  2, .rpm = 4500, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum Fireball TM1.2AT",                 .internal_name = "TM12A012",     .model = "QUANTUM FIREBALL TM1.2A",                                     .zones =  4, .avg_spt = 120, .heads =  2, .rpm = 4500, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum Fireball TM1070AT",                .internal_name = "TM17A012",     .model = "QUANTUM FIREBALL TM1.7A",                                     .zones =  4, .avg_spt = 130, .heads =  3, .rpm = 4500, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum Fireball TM2110AT",                .internal_name = "TM21A472",     .model = "QUANTUM FIREBALL TM2.1A",                                     .zones =  4, .avg_spt = 105, .heads =  4, .rpm = 3600, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum Fireball TM2.5AT",                 .internal_name = "TM25A472",     .model = "QUANTUM FIREBALL TM2.5A",                                     .zones =  4, .avg_spt = 105, .heads =  4, .rpm = 4500, .full_stroke_ms = 18, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum Fireball TM3.2AT",                 .internal_name = "TM32A472",     .model = "QUANTUM FIREBALL TM3.2A",                                     .zones =  4, .avg_spt = 105, .heads =  5, .rpm = 4500, .full_stroke_ms = 18, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum Fireball TM3.8AT",                 .internal_name = "TM38A472",     .model = "QUANTUM FIREBALL TM3.8A",                                     .zones =  4, .avg_spt = 105, .heads =  6, .rpm = 4500, .full_stroke_ms = 18, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum Bigfoot BF1.2AT",                  .internal_name = "BF12A011",     .model = "QUANTUM BIGFOOT BF1.2A",                                      .zones =  2, .avg_spt = 155, .heads =  2, .rpm = 3600, .full_stroke_ms = 30, .track_seek_ms = 3.5, .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum Bigfoot BF2.1AT",                  .internal_name = "BF25A011",     .model = "QUANTUM BIGFOOT BF2.1A",                                      .zones =  2, .avg_spt = 155, .heads =  4, .rpm = 3600, .full_stroke_ms = 30, .track_seek_ms = 3.5, .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum Bigfoot BF2.5AT",                  .internal_name = "BF25A011",     .model = "QUANTUM BIGFOOT BF2.5A",                                      .zones =  2, .avg_spt = 155, .heads =  4, .rpm = 3600, .full_stroke_ms = 30, .track_seek_ms = 3.5, .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum Bigfoot CY2.1AT",                  .internal_name = "CY2110A",      .model = "QUANTUM BIGFOOT_CY2160A",                                     .zones = 15, .avg_spt = 120, .heads =  2, .rpm = 3600, .full_stroke_ms = 25, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-2] Quantum Bigfoot CY4.3AT",                  .internal_name = "CY4320A",      .model = "QUANTUM BIGFOOT_CY4320A",                                     .zones = 15, .avg_spt = 140, .heads =  4, .rpm = 3600, .full_stroke_ms = 25, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-2] Quantum Bigfoot CY6.4AT",                  .internal_name = "CY6440A",      .model = "QUANTUM BIGFOOT_CY6480A",                                     .zones = 15, .avg_spt = 140, .heads =  6, .rpm = 3600, .full_stroke_ms = 25, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-2] Samsung SHD-30560A",                       .internal_name = "SHD30560A",    .model = "SAMSUNG SHD-30560A",                   .version = "J2QDB137", .zones =  2, .avg_spt = 110, .heads =  3, .rpm = 4500, .full_stroke_ms = 45, .track_seek_ms = 4.5, .rcache_num_seg =  4, .rcache_seg_size =  256, .max_multiple =  8 },
    { .name = "[ATA-2] Samsung PLS-31274A",                       .internal_name = "PLS31274A",    .model = "SAMSUNG PLS-31274A",                                          .zones =  4, .avg_spt = 110, .heads =  4, .rpm = 4500, .full_stroke_ms = 45, .track_seek_ms = 4.5, .rcache_num_seg =  4, .rcache_seg_size =  256, .max_multiple =  8 },
    { .name = "[ATA-2] Samsung Winner-1",                         .internal_name = "WNR31601A",    .model = "SAMSUNG WNR-31601A",                                          .zones =  8, .avg_spt = 110, .heads =  4, .rpm = 5400, .full_stroke_ms = 22, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-2] Seagate Medalist 210xe",                   .internal_name = "ST3250A",      .model = "ST3250A",                                                     .zones =  4, .avg_spt = 148, .heads =  2, .rpm = 3811, .full_stroke_ms = 30, .track_seek_ms = 4.1, .rcache_num_seg =  8, .rcache_seg_size =  120, .max_multiple =  8 },
    { .name = "[ATA-2] Seagate Medalist 275xe",                   .internal_name = "ST3295A",      .model = "ST3295A",                                                     .zones =  4, .avg_spt = 130, .heads =  2, .rpm = 3811, .full_stroke_ms = 30, .track_seek_ms = 3.4, .rcache_num_seg =  8, .rcache_seg_size =  120, .max_multiple =  8 },
    { .name = "[ATA-2] Seagate Medalist 425xe",                   .internal_name = "ST3491A",      .model = "ST3491A",                                                     .zones =  4, .avg_spt = 152, .heads =  3, .rpm = 3811, .full_stroke_ms = 30, .track_seek_ms = 4.4, .rcache_num_seg =  8, .rcache_seg_size =  120, .max_multiple =  8 },
    { .name = "[ATA-2] Seagate Medalist 545xe",                   .internal_name = "ST3660A",      .model = "ST3660A",                                                     .zones =  4, .avg_spt = 130, .heads =  4, .rpm = 3811, .full_stroke_ms = 34, .track_seek_ms = 3.4, .rcache_num_seg =  8, .rcache_seg_size =  120, .max_multiple =  8 },
    { .name = "[ATA-2] Seagate Medalist 640xe",                   .internal_name = "ST3630A",      .model = "ST3630A",                                                     .zones =  4, .avg_spt = 130, .heads =  4, .rpm = 3811, .full_stroke_ms = 34, .track_seek_ms = 3.5, .rcache_num_seg =  8, .rcache_seg_size =  120, .max_multiple =  8 },
    { .name = "[ATA-2] Seagate Medalist 780",                     .internal_name = "ST3780A",      .model = "ST3780A",                                                     .zones =  8, .avg_spt = 120, .heads =  4, .rpm = 4500, .full_stroke_ms = 25, .track_seek_ms = 3.5, .rcache_num_seg =  4, .rcache_seg_size =  256, .max_multiple = 16 },
    { .name = "[ATA-2] Seagate Medalist 850xe",                   .internal_name = "ST3850A",      .model = "ST3850A",                                                     .zones =  8, .avg_spt = 150, .heads =  4, .rpm = 3811, .full_stroke_ms = 34, .track_seek_ms = 3.8, .rcache_num_seg =  8, .rcache_seg_size =  120, .max_multiple =  8 },
    { .name = "[ATA-2] Seagate Medalist 1220",                    .internal_name = "ST31220A",     .model = "ST31220A",                                                    .zones =  8, .avg_spt = 140, .heads =  6, .rpm = 4500, .full_stroke_ms = 27, .track_seek_ms = 3.5, .rcache_num_seg =  4, .rcache_seg_size =  256, .max_multiple = 16 },
    { .name = "[ATA-2] Seagate Medalist 1270",                    .internal_name = "ST31270A",     .model = "ST31270A",                                                    .zones =  8, .avg_spt = 115, .heads =  6, .rpm = 4500, .full_stroke_ms = 25, .track_seek_ms = 3.5, .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple =  8 },
    { .name = "[ATA-2] Seagate Medalist 1270SL",                  .internal_name = "ST51270A",     .model = "ST51270A",                                                    .zones =  8, .avg_spt = 205, .heads =  6, .rpm = 5376, .full_stroke_ms = 25, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-2] Seagate Medalist 1640",                    .internal_name = "ST31640A",     .model = "ST31640A",                                                    .zones =  8, .avg_spt = 100, .heads =  6, .rpm = 5376, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 16 },
    { .name = "[ATA-2] Seagate Medalist 2140",                    .internal_name = "ST32140A",     .model = "ST32140A",                                                    .zones =  8, .avg_spt = 100, .heads =  8, .rpm = 5376, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 16 },
    { .name = "[ATA-2] Seagate Medalist 2160 Pro",                .internal_name = "ST52160A",     .model = "ST52160A",                                                    .zones = 16, .avg_spt = 220, .heads =  4, .rpm = 5400, .full_stroke_ms = 25, .track_seek_ms = 3.5, .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-2] Seagate Medalist 2520 Pro",                .internal_name = "ST52520A",     .model = "ST52520A",                                                    .zones = 16, .avg_spt = 220, .heads =  4, .rpm = 5400, .full_stroke_ms = 25, .track_seek_ms = 3.5, .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-2] Seagate Medalist 3240",                    .internal_name = "ST33240A",     .model = "ST33240A",                                                    .zones = 16, .avg_spt = 125, .heads =  8, .rpm = 4500, .full_stroke_ms = 25, .track_seek_ms = 2.5, .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-2] Seagate Decathlon 850",                    .internal_name = "ST5850N",      .model = "ST5850N",                                                     .zones =  1, .avg_spt = 135, .heads =  4, .rpm = 5376, .full_stroke_ms = 27, .track_seek_ms = 4.5, .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple =  8 },
    { .name = "[ATA-2] Toshiba MK-2720FC",                        .internal_name = "MK2720FC",     .model = "TOSHIBA MK2720FC",                                            .zones =  4, .avg_spt = 130, .heads = 10, .rpm = 4200, .full_stroke_ms = 36, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-2] Western Digital Caviar 2540",              .internal_name = "AC2540",       .model = "AC2540H",                                                     .zones =  4, .avg_spt = 150, .heads =  2, .rpm = 4500, .full_stroke_ms = 12, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Western Digital Caviar 2635",              .internal_name = "AC2635",       .model = "AC2635F",                                                     .zones =  4, .avg_spt = 130, .heads =  2, .rpm = 5200, .full_stroke_ms = 12, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Western Digital Caviar 2700",              .internal_name = "AC2700",       .model = "AC2700F",                                                     .zones =  4, .avg_spt = 110, .heads =  2, .rpm = 5200, .full_stroke_ms = 12, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Western Digital Caviar 2850",              .internal_name = "AC2850",       .model = "AC2850F",                                                     .zones =  4, .avg_spt = 130, .heads =  4, .rpm = 4500, .full_stroke_ms = 12, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Western Digital Caviar 11000",             .internal_name = "AC11000",      .model = "WDC AC11000H",                                                .zones =  4, .avg_spt = 120, .heads =  2, .rpm = 5200, .full_stroke_ms = 12, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Western Digital Caviar 11200",             .internal_name = "AC11200",      .model = "WDC AC11200L",                                                .zones =  4, .avg_spt = 110, .heads =  2, .rpm = 5200, .full_stroke_ms = 33, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Western Digital Caviar 11600",             .internal_name = "AC11600",      .model = "WDC AC11600H",                                                .zones =  4, .avg_spt = 110, .heads =  3, .rpm = 5200, .full_stroke_ms = 33, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Western Digital Caviar 12100",             .internal_name = "AC12100",      .model = "WDC AC12100F",                                                .zones =  4, .avg_spt = 110, .heads =  4, .rpm = 5200, .full_stroke_ms = 33, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Western Digital Caviar 12500",             .internal_name = "AC12500",      .model = "WDC AC12500L",                                                .zones =  8, .avg_spt = 130, .heads =  3, .rpm = 5200, .full_stroke_ms = 33, .track_seek_ms = 3.5, .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Western Digital Caviar 21000",             .internal_name = "AC21000",      .model = "WDC AC21000H",                                                .zones =  4, .avg_spt = 110, .heads =  3, .rpm = 5200, .full_stroke_ms = 28, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Western Digital Caviar 21200",             .internal_name = "AC21200",      .model = "WDC AC21200H",                                                .zones =  4, .avg_spt = 110, .heads =  3, .rpm = 5200, .full_stroke_ms = 39, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Western Digital Caviar 21600",             .internal_name = "AC21600",      .model = "WDC AC21600H",                                                .zones =  8, .avg_spt = 140, .heads =  3, .rpm = 5200, .full_stroke_ms = 30, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Western Digital Caviar 22000",             .internal_name = "AC22000",      .model = "WDC AC22000L",                                                .zones =  8, .avg_spt = 130, .heads =  3, .rpm = 5200, .full_stroke_ms = 33, .track_seek_ms = 3.5, .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Western Digital Caviar 22100",             .internal_name = "AC22100",      .model = "WDC AC22100H",                                                .zones =  8, .avg_spt = 140, .heads =  4, .rpm = 5200, .full_stroke_ms = 30, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-2] Western Digital Caviar 22500",             .internal_name = "AC22500",      .model = "WDC AC22500H",                                                .zones =  8, .avg_spt = 130, .heads =  2, .rpm = 5200, .full_stroke_ms = 30, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-2] Western Digital Caviar 31000",             .internal_name = "AC31000",      .model = "WDC AC31000F",                                                .zones =  8, .avg_spt = 110, .heads =  2, .rpm = 5200, .full_stroke_ms = 30, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Western Digital Caviar 31200",             .internal_name = "AC31200",      .model = "WDC AC31200F",                                                .zones =  8, .avg_spt = 210, .heads =  4, .rpm = 4500, .full_stroke_ms = 12, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =   64, .max_multiple = 16 },
    { .name = "[ATA-2] Western Digital Caviar 31600",             .internal_name = "AC31600",      .model = "WDC AC31600H",                                                .zones =  8, .avg_spt = 220, .heads =  4, .rpm = 5200, .full_stroke_ms = 12, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =   64, .max_multiple = 16 },
    { .name = "[ATA-2] Western Digital Caviar 32500",             .internal_name = "AC32500",      .model = "WDC AC32500H",                                                .zones =  8, .avg_spt = 230, .heads =  3, .rpm = 5200, .full_stroke_ms = 12, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Fujitsu MPA3017AT",                        .internal_name = "MPA3017AT",    .model = "FUJITSU MPA3017AT",                                           .zones =  5, .avg_spt = 210, .heads =  2, .rpm = 5400, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Fujitsu MPA3022AT",                        .internal_name = "MPA3022AT",    .model = "FUJITSU MPA3022AT",                                           .zones =  6, .avg_spt = 210, .heads =  3, .rpm = 5400, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Fujitsu MPA3026AT",                        .internal_name = "MPA3026AT",    .model = "FUJITSU MPA3026AT",                                           .zones =  8, .avg_spt = 210, .heads =  3, .rpm = 5400, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Fujitsu MPA3035AT",                        .internal_name = "MPA3035AT",    .model = "FUJITSU MPA3035AT",                                           .zones = 11, .avg_spt = 210, .heads =  4, .rpm = 5400, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Fujitsu MPA3043AT",                        .internal_name = "MPA3043AT",    .model = "FUJITSU MPA3043AT",                                           .zones = 15, .avg_spt = 210, .heads =  5, .rpm = 5400, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Fujitsu MPA3052AT",                        .internal_name = "MPA3052AT",    .model = "FUJITSU MPA3052AT",                                           .zones = 16, .avg_spt = 210, .heads =  5, .rpm = 5400, .full_stroke_ms = 35, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Fujitsu Mobile 3 2.1G",                    .internal_name = "MHA2021AT",    .model = "FUJITSU MHA2021AT",                                           .zones = 13, .avg_spt = 130, .heads =  4, .rpm = 4000, .full_stroke_ms = 30, .track_seek_ms = 2.5, .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Fujitsu Mobile 3 3.2G",                    .internal_name = "MHA2032AT",    .model = "FUJITSU MHA2032AT",                                           .zones = 13, .avg_spt = 130, .heads =  6, .rpm = 4000, .full_stroke_ms = 30, .track_seek_ms = 2.5, .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Fujitsu Mobile 4 3.2G",                    .internal_name = "MHC2032AT",    .model = "FUJITSU MHC2032AT",                                           .zones = 12, .avg_spt = 135, .heads =  4, .rpm = 4000, .full_stroke_ms = 30, .track_seek_ms = 2.5, .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-3] Fujitsu Mobile 4 4.0G",                    .internal_name = "MHC2040AT",    .model = "FUJITSU MHC2040AT",                                           .zones = 12, .avg_spt = 135, .heads =  6, .rpm = 4000, .full_stroke_ms = 30, .track_seek_ms = 2.5, .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-3] Fujitsu Mobile 4L 2.1G",                   .internal_name = "MHD2021AT",    .model = "FUJITSU MHD2021AT",                                           .zones = 12, .avg_spt = 135, .heads =  3, .rpm = 4000, .full_stroke_ms = 30, .track_seek_ms = 2.5, .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-3] Fujitsu Mobile 4L 3.2G",                   .internal_name = "MHD2032AT",    .model = "FUJITSU MHD2032AT",                                           .zones = 12, .avg_spt = 135, .heads =  4, .rpm = 4000, .full_stroke_ms = 30, .track_seek_ms = 2.5, .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-3] Fujitsu Picobird 3.2GB",                   .internal_name = "MPC3032AT",    .model = "FUJITSU MPC3032AT",                                           .zones = 15, .avg_spt = 205, .heads =  2, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 16 },
    { .name = "[ATA-3] Fujitsu Picobird 4.3GB",                   .internal_name = "MPC3043AT",    .model = "FUJITSU MPC3043AT",                                           .zones = 15, .avg_spt = 205, .heads =  3, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 16 },
    { .name = "[ATA-3] Fujitsu Picobird 6.4GB",                   .internal_name = "MPC3064AT",    .model = "FUJITSU MPC3064AT",                                           .zones = 15, .avg_spt = 205, .heads =  4, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 16 },
    { .name = "[ATA-3] Fujitsu Picobird 8.4GB",                   .internal_name = "MPC3084AT",    .model = "FUJITSU MPC3084AT",                                           .zones = 15, .avg_spt = 205, .heads =  6, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 16 },
    { .name = "[ATA-3] Fujitsu Picobird 9.6GB",                   .internal_name = "MPC3096AT",    .model = "FUJITSU MPC3096AT",                                           .zones = 15, .avg_spt = 205, .heads =  6, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 16 },
    { .name = "[ATA-3] Fujitsu Picobird 10.2GB",                  .internal_name = "MPC3102AT",    .model = "FUJITSU MPC3102AT",                                           .zones = 15, .avg_spt = 205, .heads =  6, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 16 },
    { .name = "[ATA-3] Hitachi DK225A-14",                        .internal_name = "DK225A14",     .model = "HITACHI DK225A-14",                                           .zones =  8, .avg_spt = 120, .heads =  4, .rpm = 4464, .full_stroke_ms = 30, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Hitachi DK225A-21",                        .internal_name = "DK225A21",     .model = "HITACHI DK225A-21",                                           .zones =  8, .avg_spt = 120, .heads =  6, .rpm = 4464, .full_stroke_ms = 30, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Hitachi DK226A-21",                        .internal_name = "DK226A21",     .model = "HITACHI DK226A-21",                                           .zones =  8, .avg_spt = 125, .heads =  6, .rpm = 4000, .full_stroke_ms = 33, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Hitachi DK226A-32",                        .internal_name = "DK226A32",     .model = "HITACHI DK226A-32",                                           .zones =  8, .avg_spt = 125, .heads =  6, .rpm = 4000, .full_stroke_ms = 33, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Hitachi DK227A-41",                        .internal_name = "DK227A41",     .model = "HITACHI DK227A-41",                                           .zones = 12, .avg_spt = 220, .heads =  6, .rpm = 4000, .full_stroke_ms = 33, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-3] Hitachi DK227A-50",                        .internal_name = "DK227A50",     .model = "HITACHI DK227A-50",                                           .zones = 12, .avg_spt = 220, .heads =  6, .rpm = 4000, .full_stroke_ms = 33, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-3] IBM Deskstar 3 (DAQA-32160)",              .internal_name = "DAQA32160",    .model = "IBM-DAQA-32160",                                              .zones =  8, .avg_spt = 230, .heads =  4, .rpm = 5400, .full_stroke_ms = 25, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =   96, .max_multiple = 16 },
    { .name = "[ATA-3] IBM Deskstar 3 (DAQA-32700)",              .internal_name = "DAQA32700",    .model = "IBM-DAQA-32700",                                              .zones =  8, .avg_spt = 230, .heads =  4, .rpm = 5400, .full_stroke_ms = 25, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =   96, .max_multiple = 16 },
    { .name = "[ATA-3] IBM Deskstar 3 (DAQA-33240)",              .internal_name = "DAQA33240",    .model = "IBM-DAQA-33240",                                              .zones =  8, .avg_spt = 230, .heads =  4, .rpm = 5400, .full_stroke_ms = 25, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =   96, .max_multiple = 16 },
    { .name = "[ATA-3] IBM Travelstar 4GT (DTCA-23240)",          .internal_name = "DTCA23240",    .model = "IBM-DTCA-23240",                                              .zones = 10, .avg_spt = 235, .heads =  3, .rpm = 4000, .full_stroke_ms = 28, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =   96, .max_multiple = 16 },
    { .name = "[ATA-3] IBM Travelstar 4GT (DTCA-24090)",          .internal_name = "DTCA24090",    .model = "IBM-DTCA-24090",                                              .zones = 10, .avg_spt = 235, .heads =  4, .rpm = 4000, .full_stroke_ms = 28, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =   96, .max_multiple = 16 },
    { .name = "[ATA-3] IBM Travelstar 5GS (DPLA-24480)",          .internal_name = "DPLA24480",    .model = "IBM-DPLA-24480",                                              .zones = 12, .avg_spt = 225, .heads =  4, .rpm = 4900, .full_stroke_ms = 28, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =   96, .max_multiple = 32 },
    { .name = "[ATA-3] IBM Travelstar 5GS (DPLA-25120)",          .internal_name = "DPLA25120",    .model = "IBM-DPLA-25120",                                              .zones = 12, .avg_spt = 225, .heads =  4, .rpm = 4900, .full_stroke_ms = 28, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =   96, .max_multiple = 32 },
    { .name = "[ATA-3] IBM Travelstar 8GS (DYLA-26480)",          .internal_name = "DYLA26480",    .model = "IBM-DYLA-26480",                                              .zones =  8, .avg_spt = 220, .heads =  8, .rpm = 4900, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =  459, .max_multiple = 16 },
    { .name = "[ATA-3] IBM Travelstar 8GS (DYLA-27900)",          .internal_name = "DYLA27900",    .model = "IBM-DYLA-27900",                                              .zones =  8, .avg_spt = 220, .heads =  8, .rpm = 4900, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =  459, .max_multiple = 16 },
    { .name = "[ATA-3] IBM Travelstar 8GS (DYLA-28100)",          .internal_name = "DYLA28100",    .model = "IBM-DYLA-28100",                                              .zones =  8, .avg_spt = 220, .heads = 10, .rpm = 4900, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =  459, .max_multiple = 16 },
    { .name = "[ATA-3] Micropolis Mustang (4525A)",               .internal_name = "MT4525A",      .model = "MICROPOLIS 4525 A",                                           .zones = 12, .avg_spt = 205, .heads =  4, .rpm = 5200, .full_stroke_ms = 23, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-3] Micropolis Mustang (4540A)",               .internal_name = "MT4540A",      .model = "MICROPOLIS 4540 A",                                           .zones = 12, .avg_spt = 205, .heads =  6, .rpm = 5200, .full_stroke_ms = 23, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-3] Micropolis Mustang (4550A)",               .internal_name = "MT4550A",      .model = "MICROPOLIS 4550 A",                                           .zones = 12, .avg_spt = 205, .heads =  8, .rpm = 5200, .full_stroke_ms = 23, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-3] Samsung Winner 3A",                        .internal_name = "WA32163A",     .model = "SAMSUNG WA32163A",                                            .zones = 16, .avg_spt = 210, .heads =  4, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Samsung Winner 3X (WU32163A)",             .internal_name = "WU32163A",     .model = "SAMSUNG WU32163A",                                            .zones = 16, .avg_spt = 210, .heads =  3, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Samsung Winner 3X (WU32543A)",             .internal_name = "WU32543A",     .model = "SAMSUNG WU32543A",                                            .zones = 16, .avg_spt = 210, .heads =  3, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Samsung Winner 5X (WU31605A)",             .internal_name = "WU31605A",     .model = "SAMSUNG WU31605A",                                            .zones = 16, .avg_spt = 200, .heads =  2, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Samsung Winner 5X (WU32165A)",             .internal_name = "WU32165A",     .model = "SAMSUNG WU32165A",                                            .zones = 16, .avg_spt = 200, .heads =  3, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Samsung Winner 5X (WU33205A)",             .internal_name = "WU33205A",     .model = "SAMSUNG WU33205A",                                            .zones = 16, .avg_spt = 200, .heads =  4, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Samsung Voyager 6 (SV0432A)",              .internal_name = "SV0432A",      .model = "SAMSUNG SV0432A",                                             .zones =  8, .avg_spt = 205, .heads =  2, .rpm = 5400, .full_stroke_ms = 22, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-3] Samsung Voyager 6 (SV0643A)",              .internal_name = "SV0643A",      .model = "SAMSUNG SV0643A",                                             .zones =  8, .avg_spt = 205, .heads =  3, .rpm = 5400, .full_stroke_ms = 22, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-3] Samsung Voyager 6 (SV0844A)",              .internal_name = "SV0844A",      .model = "SAMSUNG SV0844A",                                             .zones =  8, .avg_spt = 205, .heads =  4, .rpm = 5400, .full_stroke_ms = 22, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-3] Seagate Medalist 636",                     .internal_name = "ST3636A",      .model = "Seagate Technology 635MB - ST3636A",                          .zones =  2, .avg_spt = 130, .heads =  2, .rpm = 4500, .full_stroke_ms = 25, .track_seek_ms = 3.8, .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-3] Seagate Medalist 1082",                    .internal_name = "ST31082A",     .model = "Seagate Technology 1080MB - ST31082A",                        .zones =  4, .avg_spt = 130, .heads =  3, .rpm = 4500, .full_stroke_ms = 25, .track_seek_ms = 3.8, .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple =  8 },
    { .name = "[ATA-3] Seagate Medalist 1276",                    .internal_name = "ST31276A",     .model = "Seagate Technology 1275MB - ST31276A",                        .zones =  4, .avg_spt = 130, .heads =  3, .rpm = 4500, .full_stroke_ms = 25, .track_seek_ms = 3.8, .rcache_num_seg =  4, .rcache_seg_size =   64, .max_multiple = 16 },
    { .name = "[ATA-3] Seagate Medalist 1012",                    .internal_name = "ST31012A",     .model = "ST31012A",                                                    .zones =  4, .avg_spt = 130, .heads =  2, .rpm = 4500, .full_stroke_ms = 23, .track_seek_ms = 3.8, .rcache_num_seg =  8, .rcache_seg_size =   64, .max_multiple = 16 },
    { .name = "[ATA-3] Seagate Medalist 1720",                    .internal_name = "ST31720A",     .model = "ST31720A",                                                    .zones =  4, .avg_spt = 120, .heads =  4, .rpm = 4500, .full_stroke_ms = 25, .track_seek_ms = 2,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Seagate Medalist 2132",                    .internal_name = "ST32132A",     .model = "ST32132A",                                                    .zones =  8, .avg_spt = 125, .heads =  6, .rpm = 4500, .full_stroke_ms = 30, .track_seek_ms = 2.3, .rcache_num_seg =  8, .rcache_seg_size =  120, .max_multiple = 16 },
    { .name = "[ATA-3] Seagate Medalist 3230",                    .internal_name = "ST33230A",     .model = "ST33230A",                                                    .zones =  8, .avg_spt = 145, .heads =  6, .rpm = 4500, .full_stroke_ms = 23, .track_seek_ms = 3.5, .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Seagate Medalist 4340",                    .internal_name = "ST34340A",     .model = "ST34340A",                                                    .zones =  8, .avg_spt = 145, .heads =  8, .rpm = 4500, .full_stroke_ms = 23, .track_seek_ms = 3.5, .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Toshiba MK-1301MAV",                       .internal_name = "MK1301MAV",    .model = "TOSHIBA MK1301MAV",                                           .zones =  8, .avg_spt = 130, .heads =  6, .rpm = 4200, .full_stroke_ms = 36, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-3] Toshiba MK-2101MAN",                       .internal_name = "MK2101MAN",    .model = "TOSHIBA MK2101MAN",                                           .zones =  8, .avg_spt = 130, .heads = 10, .rpm = 4200, .full_stroke_ms = 36, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple = 16 }, // ATA-2/3 compatible. However, The Retro Web says it is ATA-2 only
    { .name = "[ATA-3] Toshiba MK-4313MAT",                       .internal_name = "MK4313MAT",    .model = "TOSHIBA MK4313MAT",                                           .zones =  8, .avg_spt = 174, .heads =  6, .rpm = 4200, .full_stroke_ms = 36, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 }, // ATA-2/3 compatible? The Retro Web thinks it is ATA-1
    { .name = "[ATA-3] Western Digital Caviar 13200",             .internal_name = "AC13200",      .model = "WDC AC13200R",                                                .zones =  8, .avg_spt = 211, .heads =  3, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 }, 
    { .name = "[ATA-3] Western Digital Caviar 21700",             .internal_name = "AC21700",      .model = "WDC AC21700H",                                                .zones =  8, .avg_spt = 185, .heads =  3, .rpm = 5200, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 }, // Only for Apple Computer OEM, not for retail.
    { .name = "[ATA-3] Western Digital Caviar 28400",             .internal_name = "AC28400",      .model = "WDC AC28400R",                                                .zones =  8, .avg_spt = 211, .heads =  5, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 }, 
    { .name = "[ATA-3] Western Digital Caviar 200AB",             .internal_name = "WD200AB",      .model = "WDC WD200AB-00CDB0",                                          .zones = 16, .avg_spt = 310, .heads =  6, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-4] Fujitsu MPA3026AT (Ultra-ATA)",            .internal_name = "MPA3026AT4",   .model = "FUJITSU MPA3026AT",                                           .zones =  8, .avg_spt = 195, .heads =  3, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 3.2, .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Fujitsu MPB3021AT",                        .internal_name = "MPB3021AT",    .model = "FUJITSU MPB3021AT",                                           .zones =  5, .avg_spt = 195, .heads =  2, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2.5, .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 16 },
    { .name = "[ATA-4] Fujitsu MPB3032AT",                        .internal_name = "MPB3032AT",    .model = "FUJITSU MPB3032AT",                                           .zones =  5, .avg_spt = 195, .heads =  3, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2.5, .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 16 },
    { .name = "[ATA-4] Fujitsu MPB3043AT",                        .internal_name = "MPB3043AT",    .model = "FUJITSU MPB3043AT",                                           .zones =  5, .avg_spt = 195, .heads =  4, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2.5, .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 16 },
    { .name = "[ATA-4] Fujitsu MPB3052AT",                        .internal_name = "MPB3052AT",    .model = "FUJITSU MPB3052AT",                                           .zones =  5, .avg_spt = 195, .heads =  5, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2.5, .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 16 },
    { .name = "[ATA-4] Fujitsu MPB3064AT",                        .internal_name = "MPB3064AT",    .model = "FUJITSU MPB3064AT",                                           .zones =  5, .avg_spt = 195, .heads =  6, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2.5, .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 16 },
    { .name = "[ATA-4] Fujitsu MPD3043AT",                        .internal_name = "MPD3043AT",    .model = "FUJITSU MPD3043AT",                                           .zones =  7, .avg_spt = 205, .heads =  2, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-4] Fujitsu MPD3064AT",                        .internal_name = "MPD3064AT",    .model = "FUJITSU MPD3064AT",                                           .zones =  7, .avg_spt = 205, .heads =  3, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-4] Fujitsu MPD3084AT",                        .internal_name = "MPD3084AT",    .model = "FUJITSU MPD3084AT",                                           .zones =  7, .avg_spt = 205, .heads =  4, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-4] Fujitsu MPD3108AT",                        .internal_name = "MPD3108AT",    .model = "FUJITSU MPD3108AT",                                           .zones =  7, .avg_spt = 205, .heads =  5, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-4] Fujitsu MPD3129AT",                        .internal_name = "MPD3129AT",    .model = "FUJITSU MPD3129AT",                                           .zones =  7, .avg_spt = 205, .heads =  6, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-4] Fujitsu MPD3173AT",                        .internal_name = "MPD3173AT",    .model = "FUJITSU MPD3173AT",                                           .zones =  7, .avg_spt = 205, .heads =  8, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-4] Fujitsu MPE3064AT",                        .internal_name = "MPE3064AT",    .model = "FUJITSU MPE3064AT",                                           .zones =  7, .avg_spt = 295, .heads =  2, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Fujitsu MPE3102AT",                        .internal_name = "MPE3102AT",    .model = "FUJITSU MPE3102AT",                                           .zones =  7, .avg_spt = 295, .heads =  3, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Fujitsu MPE3136AT",                        .internal_name = "MPE3136AT",    .model = "FUJITSU MPE3136AT",                                           .zones =  7, .avg_spt = 295, .heads =  4, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Fujitsu MPE3170AT",                        .internal_name = "MPE3170AT",    .model = "FUJITSU MPE3170AT",                                           .zones =  7, .avg_spt = 295, .heads =  5, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Fujitsu MPE3204AT",                        .internal_name = "MPE3204AT",    .model = "FUJITSU MPE3204AT",                                           .zones =  7, .avg_spt = 295, .heads =  6, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Fujitsu MPE3273AT",                        .internal_name = "MPE3273AT",    .model = "FUJITSU MPE3273AT",                                           .zones =  7, .avg_spt = 295, .heads =  8, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Travelstar 4LP (DTNA-21800)",              .internal_name = "DTNA21800",    .model = "IBM-DTNA-21800",                                              .zones = 10, .avg_spt = 235, .heads =  3, .rpm = 4000, .full_stroke_ms = 28, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =  463, .max_multiple = 16 },
    { .name = "[ATA-4] Travelstar 4LP (DTNA-22160)",              .internal_name = "DTNA22160",    .model = "IBM-DTNA-22160",                                              .zones = 10, .avg_spt = 235, .heads =  4, .rpm = 4000, .full_stroke_ms = 28, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =  463, .max_multiple = 16 },
    { .name = "[ATA-4] IBM Travelstar 3GN (DYKA-22160)",          .internal_name = "DYKA22160",    .model = "IBM-DYKA-22160",                                              .zones =  8, .avg_spt = 230, .heads =  2, .rpm = 4200, .full_stroke_ms = 18, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =  463, .max_multiple = 16 },
    { .name = "[ATA-4] IBM Travelstar 3GN (DYKA-23240)",          .internal_name = "DYKA23240",    .model = "IBM-DYKA-23240",                                              .zones =  8, .avg_spt = 230, .heads =  3, .rpm = 4200, .full_stroke_ms = 18, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =  463, .max_multiple = 16 },
    { .name = "[ATA-4] IBM Travelstar 4GN (DKLA-22160)",          .internal_name = "DKLA22160",    .model = "IBM-DKLA-22160",                                              .zones = 12, .avg_spt = 230, .heads =  2, .rpm = 4200, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =  463, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Travelstar 4GN (DKLA-23240)",          .internal_name = "DKLA23240",    .model = "IBM-DKLA-23240",                                              .zones = 12, .avg_spt = 230, .heads =  3, .rpm = 4200, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =  463, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Travelstar 4GN (DKLA-24320)",          .internal_name = "DKLA24320",    .model = "IBM-DKLA-24320",                                              .zones = 12, .avg_spt = 230, .heads =  4, .rpm = 4200, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =  463, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Travelstar 6GN 3.24",                  .internal_name = "DBCA203240",   .model = "IBM-DBCA-203240",                                             .zones = 12, .avg_spt = 240, .heads =  2, .rpm = 4200, .full_stroke_ms = 31, .track_seek_ms = 2.5, .rcache_num_seg = 16, .rcache_seg_size =  420, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Travelstar 6GN 4.86",                  .internal_name = "DBCA204860",   .model = "IBM-DBCA-204860",                                             .zones = 12, .avg_spt = 240, .heads =  3, .rpm = 4200, .full_stroke_ms = 31, .track_seek_ms = 2.5, .rcache_num_seg = 16, .rcache_seg_size =  420, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Travelstar 6GN 6.48",                  .internal_name = "DBCA206480",   .model = "IBM-DBCA-206480",                                             .zones = 12, .avg_spt = 240, .heads =  5, .rpm = 4200, .full_stroke_ms = 31, .track_seek_ms = 2.5, .rcache_num_seg = 16, .rcache_seg_size =  420, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Travelstar 6GT (DADA-25400)",          .internal_name = "DADA25400",    .model = "IBM-DADA-25400",                                              .zones = 16, .avg_spt = 220, .heads =  5, .rpm = 4200, .full_stroke_ms = 21, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Travelstar 6GT (DADA-26480)",          .internal_name = "DADA26480",    .model = "IBM-DADA-26480",                                              .zones = 16, .avg_spt = 220, .heads =  6, .rpm = 4200, .full_stroke_ms = 21, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 5 (DHEA-34330)",              .internal_name = "DHEA34330",    .model = "IBM-DHEA-34330",                                              .zones = 12, .avg_spt = 291, .heads =  5, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  476, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 5 (DHEA-36480)",              .internal_name = "DHEA36480",    .model = "IBM-DHEA-36480",                                              .zones = 12, .avg_spt = 295, .heads =  8, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  476, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 8 (DHEA-34331)",              .internal_name = "DHEA34331",    .model = "IBM-DHEA-34331",                                              .zones =  8, .avg_spt = 295, .heads =  4, .rpm = 5400, .full_stroke_ms = 15, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size =  464, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 8 (DHEA-34861)",              .internal_name = "DHEA34861",    .model = "IBM-DHEA-34861",                                              .zones =  8, .avg_spt = 295, .heads =  8, .rpm = 5400, .full_stroke_ms = 15, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size =  464, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 8 (DHEA-36431)",              .internal_name = "DHEA36431",    .model = "IBM-DHEA-36431",                                              .zones =  8, .avg_spt = 295, .heads =  6, .rpm = 5400, .full_stroke_ms = 15, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size =  464, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 8 (DHEA-38451)",              .internal_name = "DHEA38451",    .model = "IBM-DHEA-38451",                                              .zones =  8, .avg_spt = 295, .heads =  8, .rpm = 5400, .full_stroke_ms = 15, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size =  464, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 3GP",                         .internal_name = "DTTA350320",   .model = "IBM-DTTA-350320",                                             .zones =  8, .avg_spt = 305, .heads =  2, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size =  464, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 4GP",                         .internal_name = "DTTA350430",   .model = "IBM-DTTA-350430",                                             .zones =  8, .avg_spt = 305, .heads =  3, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size =  464, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 6GP",                         .internal_name = "DTTA350640",   .model = "IBM-DTTA-350640",                                             .zones =  8, .avg_spt = 305, .heads =  4, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size =  464, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 8GP",                         .internal_name = "DTTA350840",   .model = "IBM-DTTA-350840",                                             .zones =  8, .avg_spt = 305, .heads =  5, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size =  464, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 10GP",                        .internal_name = "DTTA351010",   .model = "IBM-DTTA-351010",                                             .zones =  8, .avg_spt = 305, .heads =  6, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size =  464, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 12GP",                        .internal_name = "DTTA351290",   .model = "IBM-DTTA-351290",                                             .zones =  8, .avg_spt = 305, .heads =  6, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size =  464, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 13GP",                        .internal_name = "DTTA351350",   .model = "IBM-DTTA-351350",                                             .zones =  8, .avg_spt = 305, .heads =  8, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size =  464, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 16GP",                        .internal_name = "DTTA351680",   .model = "IBM-DTTA-351680",                                             .zones =  8, .avg_spt = 305, .heads = 10, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size =  464, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 25GP (DJNA-351010)",          .internal_name = "DJNA351010",   .model = "IBM-DJNA-351010",                                             .zones = 12, .avg_spt = 311, .heads =  4, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.7, .rcache_num_seg = 16, .rcache_seg_size =  430, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 25GP (DJNA-351520)",          .internal_name = "DJNA351520",   .model = "IBM-DJNA-351520",                                             .zones = 12, .avg_spt = 311, .heads =  6, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.7, .rcache_num_seg = 16, .rcache_seg_size =  430, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 25GP (DJNA-352030)",          .internal_name = "DJNA352030",   .model = "IBM-DJNA-352030",                                             .zones = 12, .avg_spt = 311, .heads =  8, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.7, .rcache_num_seg = 16, .rcache_seg_size = 1966, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 25GP (DJNA-352500)",          .internal_name = "DJNA352500",   .model = "IBM-DJNA-352500",                                             .zones = 12, .avg_spt = 311, .heads = 10, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.7, .rcache_num_seg = 16, .rcache_seg_size = 1966, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 25GP (DJNA-370910)",          .internal_name = "DJNA370910",   .model = "IBM-DJNA-370910",                                             .zones = 12, .avg_spt = 283, .heads =  4, .rpm = 7200, .full_stroke_ms = 19, .track_seek_ms = 1.7, .rcache_num_seg = 16, .rcache_seg_size = 1966, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 25GP (DJNA-371350)",          .internal_name = "DJNA371350",   .model = "IBM-DJNA-371350",                                             .zones = 12, .avg_spt = 283, .heads =  6, .rpm = 7200, .full_stroke_ms = 19, .track_seek_ms = 1.7, .rcache_num_seg = 16, .rcache_seg_size = 1966, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 25GP (DJNA-371800)",          .internal_name = "DJNA371800",   .model = "IBM-DJNA-371800",                                             .zones = 12, .avg_spt = 283, .heads =  8, .rpm = 7200, .full_stroke_ms = 19, .track_seek_ms = 1.7, .rcache_num_seg = 16, .rcache_seg_size = 1966, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 25GP (DJNA-372200)",          .internal_name = "DJNA372200",   .model = "IBM-DJNA-372200",                                             .zones = 12, .avg_spt = 283, .heads = 10, .rpm = 7200, .full_stroke_ms = 19, .track_seek_ms = 1.7, .rcache_num_seg = 16, .rcache_seg_size = 1966, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 10GXP",                       .internal_name = "DTTA371010",   .model = "IBM-DTTA-371010",                                             .zones =  8, .avg_spt = 305, .heads =  7, .rpm = 7200, .full_stroke_ms = 19, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size =  464, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 12GXP",                       .internal_name = "DTTA371290",   .model = "IBM-DTTA-371290",                                             .zones =  8, .avg_spt = 305, .heads =  9, .rpm = 7200, .full_stroke_ms = 19, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size =  464, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 14GXP",                       .internal_name = "DTTA371440",   .model = "IBM-DTTA-371440",                                             .zones =  8, .avg_spt = 305, .heads = 10, .rpm = 7200, .full_stroke_ms = 19, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size =  464, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 34GXP (DPTA-371360)",         .internal_name = "DPTA371360",   .model = "IBM-DPTA-371360",                                             .zones = 12, .avg_spt = 390, .heads =  4, .rpm = 7200, .full_stroke_ms = 15, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 34GXP (DPTA-372050)",         .internal_name = "DPTA372050",   .model = "IBM-DPTA-372050",                                             .zones = 12, .avg_spt = 390, .heads =  6, .rpm = 7200, .full_stroke_ms = 15, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 34GXP (DPTA-372730)",         .internal_name = "DPTA372730",   .model = "IBM-DPTA-372730",                                             .zones = 12, .avg_spt = 390, .heads =  4, .rpm = 7200, .full_stroke_ms = 15, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 34GXP (DPTA-373420)",         .internal_name = "DPTA373420",   .model = "IBM-DPTA-373420",                                             .zones = 12, .avg_spt = 390, .heads =  4, .rpm = 7200, .full_stroke_ms = 15, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 37GP (DPTA-351500)",          .internal_name = "DPTA351500",   .model = "IBM-DPTA-351500",                                             .zones = 12, .avg_spt = 390, .heads =  4, .rpm = 5400, .full_stroke_ms = 15, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 37GP (DPTA-352250)",          .internal_name = "DPTA352250",   .model = "IBM-DPTA-352250",                                             .zones = 12, .avg_spt = 390, .heads =  6, .rpm = 5400, .full_stroke_ms = 15, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 37GP (DPTA-353000)",          .internal_name = "DPTA353000",   .model = "IBM-DPTA-353000",                                             .zones = 12, .avg_spt = 390, .heads =  8, .rpm = 5400, .full_stroke_ms = 15, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-4] IBM Deskstar 37GP (DPTA-353750)",          .internal_name = "DPTA353750",   .model = "IBM-DPTA-353750",                                             .zones = 12, .avg_spt = 390, .heads = 10, .rpm = 5400, .full_stroke_ms = 15, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-4] Maxtor DiamondMax 2160",                   .internal_name = "86480D6",      .model = "Maxtor 86480D6",                                              .zones =  8, .avg_spt = 197, .heads =  4, .rpm = 5200, .full_stroke_ms = 18, .track_seek_ms = 1,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Maxtor DiamondMax 2880",                   .internal_name = "90432D3",      .model = "Maxtor 90432D3",                                              .zones = 16, .avg_spt = 190, .heads =  3, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 1,   .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Maxtor DiamondMax 3400 (90340D2)",         .internal_name = "90340D2",      .model = "Maxtor 90340D2",                                              .zones = 16, .avg_spt = 290, .heads =  2, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 0.9, .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Maxtor DiamondMax 3400 (90510D3)",         .internal_name = "90510D3",      .model = "Maxtor 90510D3",                                              .zones = 16, .avg_spt = 290, .heads =  3, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 0.9, .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Maxtor DiamondMax 3400 (90644D3)",         .internal_name = "90644D3",      .model = "Maxtor 90644D3",                                              .zones = 16, .avg_spt = 290, .heads =  3, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 0.9, .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Maxtor DiamondMax 3400 (90680D4)",         .internal_name = "90680D4",      .model = "Maxtor 90680D4",                                              .zones = 16, .avg_spt = 290, .heads =  4, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 0.9, .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Maxtor DiamondMax 3400 (90845D5)",         .internal_name = "90845D5",      .model = "Maxtor 90845D5",                                              .zones = 16, .avg_spt = 290, .heads =  5, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 0.9, .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Maxtor DiamondMax 3400 (91020D6)",         .internal_name = "91020D6",      .model = "Maxtor 91020D6",                                              .zones = 16, .avg_spt = 290, .heads =  6, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 0.9, .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Maxtor DiamondMax 3400 (91190D7)",         .internal_name = "91190D7",      .model = "Maxtor 91190D7",                                              .zones = 16, .avg_spt = 290, .heads =  7, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 0.9, .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Maxtor DiamondMax 3400 (91360D8)",         .internal_name = "91360D8",      .model = "Maxtor 91360D8",                                              .zones = 16, .avg_spt = 290, .heads =  8, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 0.9, .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Maxtor DiamondMax 3400 OEM (91010E6)",     .internal_name = "91010E6",      .model = "Maxtor 91010E6",                                              .zones = 16, .avg_spt = 290, .heads =  6, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 0.9, .rcache_num_seg = 16, .rcache_seg_size =  256, .max_multiple = 32 }, /* Only for OEM */
    { .name = "[ATA-4] Maxtor DiamondMax 4320 (90432D2)",         .internal_name = "90432D2",      .model = "Maxtor 90432D2",                                              .zones = 16, .avg_spt = 290, .heads =  2, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 0.9, .rcache_num_seg = 16, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Maxtor DiamondMax 4320 (90845D4)",         .internal_name = "90845D4",      .model = "Maxtor 90845D4",                                              .zones = 16, .avg_spt = 290, .heads =  3, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 0.9, .rcache_num_seg = 16, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Maxtor DiamondMax Plus 6800 (90683U2)",    .internal_name = "90683U2",      .model = "Maxtor 90683U2",                                              .zones = 16, .avg_spt = 290, .heads =  2, .rpm = 7200, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg = 16, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Maxtor DiamondMax Plus 6800 (91024U3)",    .internal_name = "91024U3",      .model = "Maxtor 91024U3",                                              .zones = 16, .avg_spt = 290, .heads =  3, .rpm = 7200, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg = 16, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Maxtor DiamondMax Plus 6800 (91366U4)",    .internal_name = "91366U4",      .model = "Maxtor 91366U4",                                              .zones = 16, .avg_spt = 290, .heads =  4, .rpm = 7200, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg = 16, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Maxtor DiamondMax Plus 6800 (92049U6)",    .internal_name = "92049U6",      .model = "Maxtor 92049U6",                                              .zones = 16, .avg_spt = 290, .heads =  6, .rpm = 7200, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg = 16, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Maxtor DiamondMax Plus 6800 (92732U8)",    .internal_name = "92732U8",      .model = "Maxtor 92732U8",                                              .zones = 16, .avg_spt = 290, .heads =  8, .rpm = 7200, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg = 16, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Quantum Fireball ST1.6AT",                 .internal_name = "ST16A011",     .model = "QUANTUM FIREBALL ST1.6A",                                     .zones = 15, .avg_spt = 200, .heads =  2, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Quantum Fireball ST2.1AT",                 .internal_name = "ST21A011",     .model = "QUANTUM FIREBALL ST2.1A",                                     .zones = 15, .avg_spt = 200, .heads =  3, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Quantum Fireball ST3.2AT",                 .internal_name = "ST32A461",     .model = "QUANTUM FIREBALL ST3.2A",                                     .zones = 15, .avg_spt = 200, .heads =  4, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Quantum Fireball ST4.3AT",                 .internal_name = "ST43A011",     .model = "QUANTUM FIREBALL ST4.3A",                                     .zones = 15, .avg_spt = 200, .heads =  6, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Quantum Fireball ST6.4AT",                 .internal_name = "ST64A011",     .model = "QUANTUM FIREBALL ST6.4A",                                     .zones = 15, .avg_spt = 200, .heads =  8, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Quantum Fireball EL2.5AT",                 .internal_name = "EL25A011",     .model = "QUANTUM FIREBALL EL2.5A",                                     .zones =  1, .avg_spt = 200, .heads =  2, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Quantum Fireball EL5.1AT",                 .internal_name = "EL51A011",     .model = "QUANTUM FIREBALL EL5.1A",                                     .zones =  2, .avg_spt = 200, .heads =  4, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Quantum Fireball EL7.6AT",                 .internal_name = "EL76A011",     .model = "QUANTUM FIREBALL EL7.6A",                                     .zones =  3, .avg_spt = 200, .heads =  6, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Quantum Fireball EL10.2AT",                .internal_name = "EL10A012",     .model = "QUANTUM FIREBALL EL10.2A",                                    .zones =  4, .avg_spt = 200, .heads =  8, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Quantum Fireball SE2.1AT",                 .internal_name = "SE21A011",     .model = "QUANTUM FIREBALL SE2.1A",                                     .zones = 12, .avg_spt = 200, .heads =  2, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Quantum Fireball SE3.2AT",                 .internal_name = "SE32A011",     .model = "QUANTUM FIREBALL SE3.2A",                                     .zones = 12, .avg_spt = 200, .heads =  3, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Quantum Fireball SE4.3AT",                 .internal_name = "SE43A011",     .model = "QUANTUM FIREBALL SE4.3A",                                     .zones = 12, .avg_spt = 200, .heads =  4, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Quantum Fireball SE6.4AT",                 .internal_name = "SE64A011",     .model = "QUANTUM FIREBALL SE6.4A",                                     .zones = 12, .avg_spt = 200, .heads =  6, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Quantum Fireball SE8.4AT",                 .internal_name = "SE84A011",     .model = "QUANTUM FIREBALL SE8.4A",                                     .zones = 12, .avg_spt = 200, .heads =  8, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Quantum Fireball EX12.7A (Ultra ATA)",     .internal_name = "EX12A012",     .model = "QUANTUM FIREBALL EX12.7A",             .version = "A0A.0D00", .zones =  4, .avg_spt = 200, .heads =  8, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Quantum Fireball LCT-08 (LA04A011)",       .internal_name = "LA04A011",     .model = "QUANTUM FIREBALLlct08 04",             .version = "A05.0X00", .zones =  8, .avg_spt = 280, .heads =  6, .rpm = 5400, .full_stroke_ms = 40, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-4] Quantum Bigfoot TX4.0AT",                  .internal_name = "TX043A011",    .model = "QUANTUM BIGFOOT TX4.0A",                                      .zones =  2, .avg_spt = 220, .heads =  2, .rpm = 4000, .full_stroke_ms = 24, .track_seek_ms = 2.5, .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 32 },
    { .name = "[ATA-4] Quantum Bigfoot TX6.0AT",                  .internal_name = "TX064A011",    .model = "QUANTUM BIGFOOT TX6.0A",                                      .zones =  4, .avg_spt = 220, .heads =  4, .rpm = 4000, .full_stroke_ms = 24, .track_seek_ms = 2.5, .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 32 },
    { .name = "[ATA-4] Quantum Bigfoot TX8.0AT",                  .internal_name = "TX084A351",    .model = "QUANTUM BIGFOOT TX8.0A",                                      .zones =  4, .avg_spt = 220, .heads =  4, .rpm = 4000, .full_stroke_ms = 24, .track_seek_ms = 2.5, .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 32 },
    { .name = "[ATA-4] Quantum Bigfoot TX12.0AT",                 .internal_name = "TX012A011",    .model = "QUANTUM BIGFOOT TX12.0A",                                     .zones =  6, .avg_spt = 220, .heads =  6, .rpm = 4000, .full_stroke_ms = 24, .track_seek_ms = 2.5, .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple = 32 },
    { .name = "[ATA-4] Quantum Bigfoot TS6.4AT",                  .internal_name = "TS06A011",     .model = "QUANTUM BIGFOOT TS6.4A",                                      .zones = 15, .avg_spt = 305, .heads =  2, .rpm = 4000, .full_stroke_ms = 20, .track_seek_ms = 2.0, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Quantum Bigfoot TS8.4AT",                  .internal_name = "TS08A011",     .model = "QUANTUM BIGFOOT TS8.4A",                                      .zones = 15, .avg_spt = 305, .heads =  3, .rpm = 4000, .full_stroke_ms = 20, .track_seek_ms = 2.0, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Quantum Bigfoot TS9.6AT",                  .internal_name = "TS09A10Y",     .model = "QUANTUM BIGFOOT TS9.6A",                                      .zones = 15, .avg_spt = 305, .heads =  4, .rpm = 4000, .full_stroke_ms = 20, .track_seek_ms = 2.0, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Quantum Bigfoot TS10.0AT",                 .internal_name = "TS10A891",     .model = "QUANTUM BIGFOOT TS10.0A",                                     .zones = 15, .avg_spt = 305, .heads =  5, .rpm = 4000, .full_stroke_ms = 20, .track_seek_ms = 2.0, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Quantum Bigfoot TS12.7AT",                 .internal_name = "TS12A011",     .model = "QUANTUM BIGFOOT TS12.7A",                                     .zones = 15, .avg_spt = 305, .heads =  6, .rpm = 4000, .full_stroke_ms = 20, .track_seek_ms = 2.0, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Quantum Bigfoot TS19.2AT",                 .internal_name = "TS19A011",     .model = "QUANTUM BIGFOOT TS19.2A",                                     .zones = 15, .avg_spt = 305, .heads =  8, .rpm = 4000, .full_stroke_ms = 20, .track_seek_ms = 2.0, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Samsung Voyager 3 (VG32163A)",             .internal_name = "VG32163A",     .model = "SAMSUNG VG32163A",                                            .zones =  8, .avg_spt = 211, .heads =  4, .rpm = 5400, .full_stroke_ms = 22, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Samsung Voyager 3 (VG34323A)",             .internal_name = "VG34323A",     .model = "SAMSUNG VG34323A",                                            .zones =  8, .avg_spt = 211, .heads =  5, .rpm = 5400, .full_stroke_ms = 22, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Samsung Voyager 3 (VG36483A)",             .internal_name = "VG36483A",     .model = "SAMSUNG VG36483A",                                            .zones =  8, .avg_spt = 211, .heads =  6, .rpm = 5400, .full_stroke_ms = 22, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Samsung SpinPoint V9100 (SV0431D)",        .internal_name = "SV0431D",      .model = "SAMSUNG SV0431D",                                             .zones =  8, .avg_spt = 185, .heads =  1, .rpm = 5400, .full_stroke_ms = 16, .track_seek_ms = 0.8, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Samsung SpinPoint V9100 (SV0842D)",        .internal_name = "SV0842D",      .model = "SAMSUNG SV0842D",                                             .zones =  8, .avg_spt = 185, .heads =  2, .rpm = 5400, .full_stroke_ms = 16, .track_seek_ms = 0.8, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Samsung SpinPoint V9100 (SV1363D)",        .internal_name = "SV1363D",      .model = "SAMSUNG SV1363D",                                             .zones =  8, .avg_spt = 185, .heads =  3, .rpm = 5400, .full_stroke_ms = 16, .track_seek_ms = 0.8, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Samsung SpinPoint V9100 (SV1824D)",        .internal_name = "SV1824D",      .model = "SAMSUNG SV1824D",                                             .zones =  8, .avg_spt = 185, .heads =  4, .rpm = 5400, .full_stroke_ms = 16, .track_seek_ms = 0.8, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Seagate Medalist 2122",                    .internal_name = "ST32122A",     .model = "ST32122A",                                                    .zones = 16, .avg_spt = 215, .heads =  2, .rpm = 4500, .full_stroke_ms = 23, .track_seek_ms = 3.8, .rcache_num_seg = 16, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Seagate Medalist 2520",                    .internal_name = "ST32520A",     .model = "ST32520A",                                                    .zones = 15, .avg_spt = 230, .heads =  4, .rpm = 5411, .full_stroke_ms = 30, .track_seek_ms = 2.5, .rcache_num_seg = 16, .rcache_seg_size =  256, .max_multiple = 16 },
    { .name = "[ATA-4] Seagate Medalist 3321",                    .internal_name = "ST33221A",     .model = "ST33221A",                                                    .zones = 16, .avg_spt = 210, .heads =  4, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 1.7, .rcache_num_seg = 16, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Seagate Medalist 4321",                    .internal_name = "ST34321A",     .model = "ST34321A",                                                    .zones = 16, .avg_spt = 210, .heads =  4, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2.2, .rcache_num_seg = 16, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Seagate Medalist 4520 Pro",                .internal_name = "ST34520A",     .model = "ST34520A",                                                    .zones = 16, .avg_spt = 295, .heads =  4, .rpm = 7200, .full_stroke_ms = 21, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Seagate Medalist 6530 Pro",                .internal_name = "ST36530A",     .model = "ST36530A",                                                    .zones = 16, .avg_spt = 295, .heads =  6, .rpm = 7200, .full_stroke_ms = 21, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 }, // Pro verrsion of Medalist 6531
    { .name = "[ATA-4] Seagate Medalist 6531",                    .internal_name = "ST36531A",     .model = "ST36531A",                                                    .zones = 16, .avg_spt = 215, .heads =  6, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 1.7, .rcache_num_seg = 16, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Seagate Medalist 8420",                    .internal_name = "ST38420A",     .model = "ST38420A",                                                    .zones = 16, .avg_spt = 290, .heads =  4, .rpm = 5400, .full_stroke_ms = 16, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Seagate Medalist 8641",                    .internal_name = "ST38641A",     .model = "ST38641A",                                                    .zones = 12, .avg_spt = 215, .heads =  8, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 1.7, .rcache_num_seg = 16, .rcache_seg_size =  128, .max_multiple = 16 },
    { .name = "[ATA-4] Seagate Medalist 9140 Pro",                .internal_name = "ST39140A",     .model = "ST39140A",                                                    .zones = 16, .avg_spt = 295, .heads =  8, .rpm = 7200, .full_stroke_ms = 21, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Seagate Medalist 13030",                   .internal_name = "ST313030A",    .model = "ST313030A",                                                   .zones = 16, .avg_spt = 290, .heads =  6, .rpm = 5400, .full_stroke_ms = 16, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Seagate Medalist 17240",                   .internal_name = "ST317240A",    .model = "ST317240A",                                                   .zones = 16, .avg_spt = 290, .heads =  8, .rpm = 5400, .full_stroke_ms = 16, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Toshiba MK4006MAV",                        .internal_name = "MK4006MAV",    .model = "TOSHIBA MK4006MAV",                                           .zones =  8, .avg_spt = 230, .heads =  6, .rpm = 4200, .full_stroke_ms = 25, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Western Digital Caviar 14300",             .internal_name = "AC14300",      .model = "WDC AC14300R",                                                .zones =  8, .avg_spt = 195, .heads =  2, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 5.5, .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-4] Western Digital Caviar 23200",             .internal_name = "AC23200",      .model = "WDC AC23200L",                                                .zones =  8, .avg_spt = 210, .heads =  4, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 16 },
    { .name = "[ATA-4] Western Digital Caviar 24300",             .internal_name = "AC24300",      .model = "WDC AC24300L",                                                .zones =  8, .avg_spt = 210, .heads =  4, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 16 },
    { .name = "[ATA-4] Western Digital Caviar 25100",             .internal_name = "AC25100",      .model = "WDC AC25100H",                                                .zones =  8, .avg_spt = 210, .heads =  5, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 16 },
    { .name = "[ATA-4] Western Digital Caviar 26400",             .internal_name = "AC26400",      .model = "WDC AC26400R",                                                .zones = 16, .avg_spt = 295, .heads =  5, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Western Digital Caviar 33100",             .internal_name = "AC33100",      .model = "WDC AC33100H",                                                .zones = 16, .avg_spt = 210, .heads =  4, .rpm = 5200, .full_stroke_ms = 40, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Western Digital Caviar 33200",             .internal_name = "AC33200",      .model = "WDC AC33200L",                                                .zones = 16, .avg_spt = 310, .heads =  5, .rpm = 5200, .full_stroke_ms = 40, .track_seek_ms = 3,   .rcache_num_seg = 16, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Western Digital Caviar 34000",             .internal_name = "AC34000",      .model = "WDC AC34000R",                                                .zones = 16, .avg_spt = 210, .heads =  4, .rpm = 5400, .full_stroke_ms = 40, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Western Digital Caviar 34300",             .internal_name = "AC34300",      .model = "WDC AC34300L",                                                .zones = 16, .avg_spt = 311, .heads =  5, .rpm = 5400, .full_stroke_ms = 40, .track_seek_ms = 3,   .rcache_num_seg = 16, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Western Digital Caviar 35100",             .internal_name = "AC35100",      .model = "WDC AC35100L",                         .version = "09.09M08", .zones = 16, .avg_spt = 315, .heads =  5, .rpm = 5400, .full_stroke_ms = 40, .track_seek_ms = 3,   .rcache_num_seg = 16, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Western Digital Caviar 38400",             .internal_name = "AC38400",      .model = "WDC AC38400L",                                                .zones = 12, .avg_spt = 310, .heads =  6, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 3,   .rcache_num_seg = 16, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Western Digital Caviar 310100",            .internal_name = "AC310100",     .model = "WDC AC310100-00RN",                                           .zones = 12, .avg_spt = 310, .heads =  8, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 3,   .rcache_num_seg = 16, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-4] Western Digital Caviar 64AA",              .internal_name = "WD64AA",       .model = "WDC WD64AA-32AAA4",                                           .zones = 16, .avg_spt = 295, .heads =  6, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-4] Western Digital Expert 100BA",             .internal_name = "WD100BA",      .model = "WDC WD100BA-60AK",                                            .zones = 16, .avg_spt = 350, .heads =  6, .rpm = 7200, .full_stroke_ms = 15, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-5] Fujitsu XV8 (MPE3043AE)",                  .internal_name = "MPE3043AE",    .model = "FUJITSU MPE3043AE",                                           .zones = 15, .avg_spt = 295, .heads =  1, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Fujitsu XV8 (MPE3084AE)",                  .internal_name = "MPE3084AE",    .model = "FUJITSU MPE3084AE",                                           .zones = 15, .avg_spt = 295, .heads =  2, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Fujitsu XV8 (MPE3173AE)",                  .internal_name = "MPE3173AE",    .model = "FUJITSU MPE3173AE",                                           .zones = 15, .avg_spt = 295, .heads =  4, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Fujitsu XV10 (MPF3102AT)",                 .internal_name = "MPF3102AT",    .model = "FUJITSU MPF3102AT",                                           .zones = 16, .avg_spt = 305, .heads =  2, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Fujitsu XV10 (MPF3153AT)",                 .internal_name = "MPF3153AT",    .model = "FUJITSU MPF3153AT",                                           .zones = 16, .avg_spt = 305, .heads =  3, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Fujitsu XV10 (MPF3204AT)",                 .internal_name = "MPF3204AT",    .model = "FUJITSU MPF3204AT",                                           .zones = 16, .avg_spt = 305, .heads =  4, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Hitachi DK23BA-60",                        .internal_name = "DK23BA60",     .model = "HITACHI DK23BA-60",                                           .zones = 12, .avg_spt = 320, .heads =  2, .rpm = 4200, .full_stroke_ms = 24, .track_seek_ms = 3,   .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-5] Hitachi DK23BA-10",                        .internal_name = "DK23BA10",     .model = "HITACHI DK23BA-10",                                           .zones = 12, .avg_spt = 320, .heads =  2, .rpm = 4200, .full_stroke_ms = 24, .track_seek_ms = 3,   .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-5] Hitachi DK23BA-20",                        .internal_name = "DK23BA20",     .model = "HITACHI DK23BA-20",                                           .zones = 12, .avg_spt = 320, .heads =  4, .rpm = 4200, .full_stroke_ms = 24, .track_seek_ms = 3,   .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-5] IBM Travelstar 6GN",                       .internal_name = "DARA206000",   .model = "IBM-DARA-206000",                                             .zones = 12, .avg_spt = 292, .heads =  2, .rpm = 4200, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] IBM Travelstar 9GN",                       .internal_name = "DARA209000",   .model = "IBM-DARA-209000",                                             .zones = 12, .avg_spt = 292, .heads =  3, .rpm = 4200, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] IBM Travelstar 10GT 8.2GB",                .internal_name = "DCXA208100",   .model = "IBM-DCXA-208100",                                             .zones = 16, .avg_spt = 320, .heads =  5, .rpm = 4200, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =  460, .max_multiple = 32 },
    { .name = "[ATA-5] IBM Travelstar 10GT 10GB",                 .internal_name = "DXCA212000",   .model = "IBM-DXCA-212000",                                             .zones = 16, .avg_spt = 320, .heads =  6, .rpm = 4200, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =  460, .max_multiple = 32 },
    { .name = "[ATA-5] IBM Travelstar 12GN",                      .internal_name = "DARA212000",   .model = "IBM-DARA-212000",                                             .zones = 12, .avg_spt = 312, .heads =  4, .rpm = 4200, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] IBM Travelstar 15GN",                      .internal_name = "DARA215000",   .model = "IBM-DARA-215000",                                             .zones = 12, .avg_spt = 312, .heads =  5, .rpm = 4200, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] IBM Travelstar 18GN",                      .internal_name = "DARA218000",   .model = "IBM-DARA-218000",                                             .zones = 12, .avg_spt = 312, .heads =  6, .rpm = 4200, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] IBM Travelstar 20GN (DJSA-205)",           .internal_name = "DJSA205",      .model = "IBM-DJSA-205",                                                .zones = 16, .avg_spt = 330, .heads =  1, .rpm = 4200, .full_stroke_ms = 24, .track_seek_ms = 2.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] IBM Travelstar 20GN (DJSA-210)",           .internal_name = "DJSA210",      .model = "IBM-DJSA-210",                                                .zones = 16, .avg_spt = 330, .heads =  2, .rpm = 4200, .full_stroke_ms = 24, .track_seek_ms = 2.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] IBM Travelstar 20GN (DJSA-220)",           .internal_name = "DJSA220",      .model = "IBM-DJSA-220",                                                .zones = 16, .avg_spt = 330, .heads =  4, .rpm = 4200, .full_stroke_ms = 24, .track_seek_ms = 2.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] IBM Travelstar 25GN",                      .internal_name = "DARA225000",   .model = "IBM-DARA-225000",                                             .zones = 12, .avg_spt = 392, .heads = 10, .rpm = 5411, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] IBM Travelstar 30GT",                      .internal_name = "DJSA230",      .model = "IBM-DJSA-230",                                                .zones = 16, .avg_spt = 330, .heads =  6, .rpm = 4200, .full_stroke_ms = 24, .track_seek_ms = 2.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] IBM Travelstar 32GH",                      .internal_name = "DJSA232",      .model = "IBM-DJSA-232",                                                .zones = 16, .avg_spt = 400, .heads =  8, .rpm = 5400, .full_stroke_ms = 24, .track_seek_ms = 2.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Maxtor DiamondMax VL 17",                  .internal_name = "90871U2",      .model = "Maxtor 90871U2",                                              .zones = 16, .avg_spt = 290, .heads =  3, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 0.9, .rcache_num_seg = 16, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-5] Maxtor DiamondMax VL 20 (91021U2)",        .internal_name = "91021U2",      .model = "Maxtor 91021U2",                                              .zones = 16, .avg_spt = 295, .heads =  2, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Maxtor DiamondMax VL 20 (91531U3)",        .internal_name = "91531U3",      .model = "Maxtor 91531U3",                                              .zones = 16, .avg_spt = 295, .heads =  3, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Maxtor DiamondMax VL 20 (92041U4)",        .internal_name = "92041U4",      .model = "Maxtor 92041U4",                                              .zones = 16, .avg_spt = 295, .heads =  4, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Maxtor DiamondMax Plus 40 (51024U2)",      .internal_name = "51024U2",      .model = "Maxtor 51024U2",                                              .zones = 16, .avg_spt = 390, .heads =  2, .rpm = 7200, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-5] Maxtor DiamondMax Plus 40 (51536U3)",      .internal_name = "51536U3",      .model = "Maxtor 51536U3",                                              .zones = 16, .avg_spt = 390, .heads =  3, .rpm = 7200, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-5] Maxtor DiamondMax Plus 40 (52049U4)",      .internal_name = "52049U4",      .model = "Maxtor 52049U4",                                              .zones = 16, .avg_spt = 390, .heads =  4, .rpm = 7200, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-5] Maxtor DiamondMax Plus 40 (53073U6)",      .internal_name = "53073U6",      .model = "Maxtor 53073U6",                                              .zones = 16, .avg_spt = 390, .heads =  6, .rpm = 7200, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-5] Maxtor DiamondMax Plus 40 (54098U8)",      .internal_name = "54098U8",      .model = "Maxtor 54098U8",                                              .zones = 16, .avg_spt = 390, .heads =  8, .rpm = 7200, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-5] Quantum Fireball EX3.2A",                  .internal_name = "EX32A012",     .model = "QUANTUM FIREBALL EX3.2A",                                     .zones =  1, .avg_spt = 210, .heads =  2, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-5] Quantum Fireball EX5.1A",                  .internal_name = "EX51A012",     .model = "QUANTUM FIREBALL EX5.1A",                                     .zones =  2, .avg_spt = 210, .heads =  3, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-5] Quantum Fireball EX6.4A",                  .internal_name = "EX64A012",     .model = "QUANTUM FIREBALL EX6.4A",                                     .zones =  2, .avg_spt = 210, .heads =  4, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-5] Quantum Fireball EX10.2A",                 .internal_name = "EX10A011",     .model = "QUANTUM FIREBALL EX10.2A",                                    .zones =  3, .avg_spt = 210, .heads =  6, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-5] Quantum Fireball EX12.7A",                 .internal_name = "EX12A011",     .model = "QUANTUM FIREBALL EX12.7A",                                    .zones =  4, .avg_spt = 210, .heads =  8, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-5] Quantum Fireball CR4.3A",                  .internal_name = "CR43A013",     .model = "QUANTUM FIREBALL CR4.3A",                                     .zones =  2, .avg_spt = 295, .heads =  3, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-5] Quantum Fireball CR6.4A",                  .internal_name = "CR64A011",     .model = "QUANTUM FIREBALL CR6.4A",                                     .zones =  2, .avg_spt = 295, .heads =  4, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 },  
    { .name = "[ATA-5] Quantum Fireball CR8.4A",                  .internal_name = "CR84A011",     .model = "QUANTUM FIREBALL CR8.4A",                                     .zones =  3, .avg_spt = 295, .heads =  6, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-5] Quantum Fireball CR13.0A",                 .internal_name = "CR13A011",     .model = "QUANTUM FIREBALL CR13.0A",                                    .zones =  4, .avg_spt = 295, .heads =  8, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 16 },
    { .name = "[ATA-5] Quantum Fireball CX6.4A",                  .internal_name = "CX06A012",     .model = "QUANTUM FIREBALL CX6.4A",                                     .zones =  2, .avg_spt = 295, .heads =  3, .rpm = 5400, .full_stroke_ms = 16, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Quantum Fireball CX10.2A",                 .internal_name = "CX10A012",     .model = "QUANTUM FIREBALL CX10.2A",                                    .zones =  3, .avg_spt = 295, .heads =  4, .rpm = 5400, .full_stroke_ms = 16, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Quantum Fireball CX13.0A",                 .internal_name = "CX13A012",     .model = "QUANTUM FIREBALL CX13.0A",                                    .zones =  4, .avg_spt = 295, .heads =  6, .rpm = 5400, .full_stroke_ms = 16, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Quantum Fireball CX20.4A",                 .internal_name = "CX20A012",     .model = "QUANTUM FIREBALL CX20.4A",                                    .zones =  8, .avg_spt = 295, .heads =  8, .rpm = 5400, .full_stroke_ms = 16, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Quantum Fireball LCT-08 (LA08A011)",       .internal_name = "LA08A011",     .model = "QUANTUM FIREBALLlct08 08",                                    .zones =  8, .avg_spt = 280, .heads =  1, .rpm = 5400, .full_stroke_ms = 40, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Quantum Fireball LCT-08 (LA13A011)",       .internal_name = "LA13A011",     .model = "QUANTUM FIREBALLlct08 13",                                    .zones =  8, .avg_spt = 280, .heads =  2, .rpm = 5400, .full_stroke_ms = 40, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Quantum Fireball LCT-08 (LA17A011)",       .internal_name = "LA17A011",     .model = "QUANTUM FIREBALLlct08 17",                                    .zones =  8, .avg_spt = 280, .heads =  3, .rpm = 5400, .full_stroke_ms = 40, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Quantum Fireball LCT-08 (LA26A011)",       .internal_name = "LA26A011",     .model = "QUANTUM FIREBALLlct08 26",                                    .zones =  8, .avg_spt = 280, .heads =  4, .rpm = 5400, .full_stroke_ms = 40, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Quantum Fireball LCT-15 7.5AT",            .internal_name = "LC07A011",     .model = "QUANTUM FIREBALLlct15 07",                                    .zones =  4, .avg_spt = 350, .heads =  2, .rpm = 4500, .full_stroke_ms = 18, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Quantum Fireball LCT-15 15.0AT",           .internal_name = "LC15A011",     .model = "QUANTUM FIREBALLlct15 15",                                    .zones =  8, .avg_spt = 350, .heads =  3, .rpm = 4500, .full_stroke_ms = 18, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Quantum Fireball LCT-15 20.4AT",           .internal_name = "LC20A011",     .model = "QUANTUM FIREBALLlct15 20",                                    .zones = 12, .avg_spt = 350, .heads =  4, .rpm = 4500, .full_stroke_ms = 18, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Quantum Fireball LCT-15 30.0AT",           .internal_name = "LC30A011",     .model = "QUANTUM FIREBALLlct15 30",                                    .zones = 16, .avg_spt = 350, .heads =  5, .rpm = 4500, .full_stroke_ms = 18, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Quantum Fireball LCT-20 10.0AT",           .internal_name = "LD10A011",     .model = "QUANTUM FIREBALLlct20 10",             .version = "A03.0900", .zones = 15, .avg_spt = 320, .heads =  1, .rpm = 4502, .full_stroke_ms = 28, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size = 1280, .max_multiple = 32 },
    { .name = "[ATA-5] Quantum Fireball LCT-20 20.0AT",           .internal_name = "LD20A011",     .model = "QUANTUM FIREBALLlct20 20",                                    .zones = 15, .avg_spt = 320, .heads =  2, .rpm = 4502, .full_stroke_ms = 28, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size = 1280, .max_multiple = 32 },
    { .name = "[ATA-5] Quantum Fireball LCT-20 30.0AT",           .internal_name = "LD30A011",     .model = "QUANTUM FIREBALLlct20 30",                                    .zones = 15, .avg_spt = 320, .heads =  3, .rpm = 4502, .full_stroke_ms = 28, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size = 1280, .max_multiple = 32 },
    { .name = "[ATA-5] Quantum Fireball LCT-20 40.0AT",           .internal_name = "LD40A011",     .model = "QUANTUM FIREBALLlct20 40",                                    .zones = 15, .avg_spt = 320, .heads =  4, .rpm = 4502, .full_stroke_ms = 28, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size = 1280, .max_multiple = 32 },
    { .name = "[ATA-5] Quantum Fireball Plus LM 10.2AT",          .internal_name = "LM10A011",     .model = "QUANTUM FIREBALL LM10.2A",                                    .zones = 16, .avg_spt = 385, .heads =  5, .rpm = 7200, .full_stroke_ms = 18, .track_seek_ms = 0.8, .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-5] Quantum Fireball Plus LM 15.0AT",          .internal_name = "LM15A011",     .model = "QUANTUM FIREBALL LM15.0A",                                    .zones = 16, .avg_spt = 385, .heads =  6, .rpm = 7200, .full_stroke_ms = 18, .track_seek_ms = 0.8, .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-5] Quantum Fireball Plus LM 20.5AT",          .internal_name = "LM20A011",     .model = "QUANTUM FIREBALL LM20.5A",                                    .zones = 16, .avg_spt = 385, .heads =  8, .rpm = 7200, .full_stroke_ms = 18, .track_seek_ms = 0.8, .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-5] Quantum Fireball Plus LM 30.0AT",          .internal_name = "LM30A011",     .model = "QUANTUM FIREBALL LM30.0A",                                    .zones = 16, .avg_spt = 385, .heads = 10, .rpm = 7200, .full_stroke_ms = 18, .track_seek_ms = 0.8, .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-5] Samsung Voyager 6 Plus",                   .internal_name = "SV0432D",      .model = "SAMSUNG SV0432D",                                             .zones = 16, .avg_spt = 295, .heads =  2, .rpm = 5400, .full_stroke_ms = 22, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Samsung SpinPoint V6800 (SV0682D)",        .internal_name = "SV0682D",      .model = "SAMSUNG SV0682D",                                             .zones =  8, .avg_spt = 295, .heads =  2, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 1.3, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Samsung SpinPoint V6800 (SV1023D)",        .internal_name = "SV1023D",      .model = "SAMSUNG SV1023D",                                             .zones =  8, .avg_spt = 295, .heads =  3, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 1.3, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Samsung SpinPoint V6800 (SV1364D)",        .internal_name = "SV1364D",      .model = "SAMSUNG SV1364D",                                             .zones =  8, .avg_spt = 295, .heads =  4, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 1.3, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Samsung SpinPoint V6800 (SV1705D)",        .internal_name = "SV1705D",      .model = "SAMSUNG SV1705D",                                             .zones =  8, .avg_spt = 295, .heads =  5, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 1.3, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Samsung SpinPoint V6800 (SV2046D)",        .internal_name = "SV2046D",      .model = "SAMSUNG SV2046D",                                             .zones =  8, .avg_spt = 295, .heads =  6, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 1.3, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Seagate U4 - 2GB",                         .internal_name = "ST32112A",     .model = "ST32112A",                                                    .zones = 16, .avg_spt = 311, .heads =  1, .rpm = 5400, .full_stroke_ms = 25, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-5] Seagate U4 - 4GB",                         .internal_name = "ST34311A",     .model = "ST34311A",                                                    .zones = 16, .avg_spt = 311, .heads =  2, .rpm = 5400, .full_stroke_ms = 25, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-5] Seagate U4 - 6GB",                         .internal_name = "ST36421A",     .model = "ST36421A",                                                    .zones = 16, .avg_spt = 311, .heads =  3, .rpm = 5400, .full_stroke_ms = 25, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-5] Seagate U4 - 8GB",                         .internal_name = "ST38421A",     .model = "ST38421A",                                                    .zones = 16, .avg_spt = 311, .heads =  4, .rpm = 5400, .full_stroke_ms = 25, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size =  256, .max_multiple = 32 },
    { .name = "[ATA-5] Seagate U8 - 4.3gb",                       .internal_name = "ST34313A",     .model = "ST34313A",                                                    .zones = 16, .avg_spt = 289, .heads =  1, .rpm = 5400, .full_stroke_ms = 25, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Seagate U8 - 8.4gb",                       .internal_name = "ST38410A",     .model = "ST38410A",                                                    .zones = 16, .avg_spt = 289, .heads =  2, .rpm = 5400, .full_stroke_ms = 25, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Seagate U8 - 13gb",                        .internal_name = "ST313021A",    .model = "ST313021A",                                                   .zones = 16, .avg_spt = 289, .heads =  3, .rpm = 5400, .full_stroke_ms = 25, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Seagate U8 - 17.2gb",                      .internal_name = "ST317221A",    .model = "ST317221A",                                                   .zones = 16, .avg_spt = 289, .heads =  4, .rpm = 5400, .full_stroke_ms = 25, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Seagate U10 - 10GB",                       .internal_name = "ST310212A",    .model = "ST310212A",                                                   .zones = 16, .avg_spt = 289, .heads =  2, .rpm = 5400, .full_stroke_ms = 25, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Seagate U10 - 15GB",                       .internal_name = "ST315323A",    .model = "ST315323A",                                                   .zones = 16, .avg_spt = 289, .heads =  3, .rpm = 5400, .full_stroke_ms = 25, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Seagate U10 - 20GB",                       .internal_name = "ST320423A",    .model = "ST320423A",                                                   .zones = 16, .avg_spt = 289, .heads =  4, .rpm = 5400, .full_stroke_ms = 25, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Toshiba MK-1517GAP",                       .internal_name = "MK1517GAP",    .model = "TOSHIBA MK1517GAP",                                           .zones = 16, .avg_spt = 274, .heads =  2, .rpm = 4200, .full_stroke_ms = 36, .track_seek_ms = 3,   .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 }, // ATA-2/3/4/5 compatible. However, The Retro Web says it is ATA-2 only
    { .name = "[ATA-5] Toshiba GAS Series - MK2018GAS",           .internal_name = "MK2018GAS",    .model = "TOSHIBA MK2018GAS",                                           .zones = 16, .avg_spt = 320, .heads =  2, .rpm = 4200, .full_stroke_ms = 22, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-5] Toshiba GAS Series - MK3017GAS",           .internal_name = "MK3017GAS",    .model = "TOSHIBA MK3017GAS",                                           .zones = 16, .avg_spt = 330, .heads =  4, .rpm = 4200, .full_stroke_ms = 22, .track_seek_ms = 4,   .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-5] Toshiba GAS Series - MK4021GAS",           .internal_name = "MK4021GAS",    .model = "TOSHIBA MK4021GAS",                                           .zones = 16, .avg_spt = 320, .heads =  3, .rpm = 4200, .full_stroke_ms = 22, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-5] Toshiba GAS Series - MK6021GAS",           .internal_name = "MK6021GAS",    .model = "TOSHIBA MK6021GAS",                                           .zones = 16, .avg_spt = 320, .heads =  4, .rpm = 4200, .full_stroke_ms = 22, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-5] Western Digital Caviar 310000",            .internal_name = "AC310000",     .model = "WDC AC310000-60RT",                                           .zones = 15, .avg_spt = 355, .heads =  6, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 3,   .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Western Digital Caviar 100BB",             .internal_name = "WD100BB",      .model = "WDC WD100BB-75CLB0",                                          .zones = 16, .avg_spt = 289, .heads =  1, .rpm = 7200, .full_stroke_ms = 21, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-5] Western Digital Caviar 102AA",             .internal_name = "WD102AA",      .model = "WDC WD102AA-00ANA0",                                          .zones = 16, .avg_spt = 295, .heads =  8, .rpm = 5400, .full_stroke_ms = 12, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Western Digital Caviar 204BA",             .internal_name = "WD204BA",      .model = "WDC WD204BA-75AGA0",                                          .zones = 16, .avg_spt = 310, .heads =  8, .rpm = 7200, .full_stroke_ms = 12, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size =  512, .max_multiple = 32 },
    { .name = "[ATA-5] Western Digital Caviar 408AA",             .internal_name = "WD408AA",      .model = "WDC WD408AA-00BAA0",                                          .zones = 16, .avg_spt = 320, .heads = 10, .rpm = 5400, .full_stroke_ms = 12, .track_seek_ms = 1.5, .rcache_num_seg = 16, .rcache_seg_size = 2048, .max_multiple = 32 },
    { .name = "[ATA-5] Western Digital Expert 135BA",             .internal_name = "WD135BA",      .model = "WDC WD135BA-60AK",                                            .zones = 16, .avg_spt = 350, .heads =  4, .rpm = 7200, .full_stroke_ms = 15, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size = 1920, .max_multiple = 32 },
    { .name = "[ATA-5] Western Digital Expert 200BA",             .internal_name = "WD200BA",      .model = "WDC WD200BA-60AGA0",                                          .zones = 16, .avg_spt = 350, .heads =  6, .rpm = 7200, .full_stroke_ms = 15, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size = 1920, .max_multiple = 32 },
   // clang-format on
};

int
hdd_preset_get_num(void)
{
    return sizeof(hdd_speed_presets) / sizeof(hdd_preset_t);
}

const char *
hdd_preset_getname(int preset)
{
    return hdd_speed_presets[preset].name;
}

const char *
hdd_preset_get_internal_name(int preset)
{
    return hdd_speed_presets[preset].internal_name;
}

uint32_t
hdd_preset_get_rpm(int preset)
{
    if (preset < 0 || preset >= hdd_preset_get_num())
        return 0;
    return hdd_speed_presets[preset].rpm;
}

int
hdd_preset_get_from_internal_name(char *s)
{
    int c = 0;

    for (int i = 0; i < (sizeof(hdd_speed_presets) / sizeof(hdd_preset_t)); i++) {
        if (!strcmp(hdd_speed_presets[c].internal_name, s))
            return c;
        c++;
    }

    return 0;
}

void
hdd_preset_apply(int hdd_id)
{
    hard_disk_t *hd = &hdd[hdd_id];
    double       revolution_usec;
    double       zone_percent;
    uint32_t     disk_sectors;
    uint32_t     sectors_per_surface;
    uint32_t     cylinders;
    uint32_t     cylinders_per_zone;
    uint32_t     total_sectors = 0;
    uint32_t     spt;
    uint32_t     zone_sectors;

    if (hd->speed_preset >= hdd_preset_get_num())
        hd->speed_preset = 0;

    const hdd_preset_t *preset = &hdd_speed_presets[hd->speed_preset];

    hd->cache.num_segments = preset->rcache_num_seg;
    hd->cache.segment_size = preset->rcache_seg_size;
    hd->max_multiple_block = preset->max_multiple;
    if (preset->model)
        hd->model = preset->model;

    if (preset->version)
        hd->version = preset->version;

    if (!hd->speed_preset)
        return;

    hd->phy_heads = preset->heads;
    hd->rpm       = preset->rpm;

    revolution_usec           = 60.0 / (double) hd->rpm * 1000000.0;
    hd->avg_rotation_lat_usec = revolution_usec / 2;
    hd->full_stroke_usec      = preset->full_stroke_ms * 1000;
    hd->head_switch_usec      = preset->track_seek_ms * 1000;
    hd->cyl_switch_usec       = preset->track_seek_ms * 1000;

    hd->cache.write_size = 64;

    hd->num_zones = preset->zones;

    disk_sectors        = hd->tracks * hd->hpc * hd->spt;
    sectors_per_surface = (uint32_t) ceil((double) disk_sectors / (double) hd->phy_heads);
    cylinders           = (uint32_t) ceil((double) sectors_per_surface / (double) preset->avg_spt);
    hd->phy_cyl         = cylinders;
    cylinders_per_zone  = cylinders / preset->zones;

    for (uint32_t i = 0; i < preset->zones; i++) {
        zone_percent = i * 100 / (double) preset->zones;

        if (i < preset->zones - 1) {
            /* Function for realistic zone sector density */
            double spt_percent = -0.00341684 * pow(zone_percent, 2) - 0.175811 * zone_percent + 118.48;
            spt                = (uint32_t) ceil((double) preset->avg_spt * spt_percent / 100);
        } else
            spt = (uint32_t) ceil((double) (disk_sectors - total_sectors) / (double) (cylinders_per_zone * preset->heads));

        zone_sectors = spt * cylinders_per_zone * preset->heads;
        total_sectors += zone_sectors;

        hd->zones[i].cylinders         = cylinders_per_zone;
        hd->zones[i].sectors_per_track = spt;
    }

    hdd_zones_init(hd);
    hdd_cache_init(hd);
}
