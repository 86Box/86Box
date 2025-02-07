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
 *
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

    if (!strcmp(str, "mfm")) {
        if (cdrom) {
no_cdrom:
            ui_msgbox_header(MBX_ERROR, plat_get_string(STRING_INVALID_CONFIG), plat_get_string(STRING_NO_ST506_ESDI_CDROM));
            return 0;
        }

        return HDD_BUS_MFM;
    }

    if (!strcmp(str, "esdi")) {
        if (cdrom)
            goto no_cdrom;

        return HDD_BUS_ESDI;
    }

    if (!strcmp(str, "ide"))
        return HDD_BUS_IDE;

    if (!strcmp(str, "atapi"))
        return HDD_BUS_ATAPI;

    if (!strcmp(str, "xta"))
        return HDD_BUS_XTA;

    if (!strcmp(str, "scsi"))
        return HDD_BUS_SCSI;

    return 0;
}

char *
hdd_bus_to_string(int bus, UNUSED(int cdrom))
{
    char *s = "none";

    switch (bus) {
        default:
        case HDD_BUS_DISABLED:
            break;

        case HDD_BUS_MFM:
            s = "mfm";
            break;

        case HDD_BUS_XTA:
            s = "xta";
            break;

        case HDD_BUS_ESDI:
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
        fatal("hdd_seek_get_time(): hdd->num_zones < 0)\n");
        return 0.0;
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

    hdd->cache.write_start_time = tsc + (uint32_t) (seek_time * cpuclock / 1000000.0);

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
    cache->ra_start_time = tsc + (uint32_t) (seek_time * cpuclock / 1000000.0);

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
    { .name = "RAM Disk (max. speed)",                            .internal_name = "ramdisk",                                                                                                        .rcache_num_seg = 16, .rcache_seg_size = 128, .max_multiple = 32 },
    { .name = "[1989] 3500 RPM",                                  .internal_name = "1989_3500rpm", .zones =  1,  .avg_spt = 35, .heads = 2, .rpm = 3500, .full_stroke_ms = 40, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =  16, .max_multiple =  8 },
    { .name = "[1992] 3600 RPM",                                  .internal_name = "1992_3600rpm", .zones =  1,  .avg_spt = 45, .heads = 2, .rpm = 3600, .full_stroke_ms = 30, .track_seek_ms = 6,   .rcache_num_seg =  4, .rcache_seg_size =  16, .max_multiple =  8 },
    { .name = "[1994] 4500 RPM",                                  .internal_name = "1994_4500rpm", .zones =  8,  .avg_spt = 80, .heads = 4, .rpm = 4500, .full_stroke_ms = 26, .track_seek_ms = 5,   .rcache_num_seg =  4, .rcache_seg_size =  32, .max_multiple = 16 },
    { .name = "[1996] 5400 RPM",                                  .internal_name = "1996_5400rpm", .zones = 16, .avg_spt = 135, .heads = 4, .rpm = 5400, .full_stroke_ms = 24, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  64, .max_multiple = 16 },
    { .name = "[1997] 5400 RPM",                                  .internal_name = "1997_5400rpm", .zones = 16, .avg_spt = 185, .heads = 6, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2.5, .rcache_num_seg =  8, .rcache_seg_size =  64, .max_multiple = 32 },
    { .name = "[1998] 5400 RPM",                                  .internal_name = "1998_5400rpm", .zones = 16, .avg_spt = 300, .heads = 8, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size = 128, .max_multiple = 32 },
    { .name = "[2000] 7200 RPM",                                  .internal_name = "2000_7200rpm", .zones = 16, .avg_spt = 350, .heads = 6, .rpm = 7200, .full_stroke_ms = 15, .track_seek_ms = 2,   .rcache_num_seg = 16, .rcache_seg_size = 128, .max_multiple = 32 },
    { .name = "[PIO IDE] IBM WDA-L42",                            .internal_name = "WDAL42", .model = "WDA-L42", .zones =  1,  .avg_spt = 85, .heads = 2, .rpm = 3600, .full_stroke_ms = 33, .track_seek_ms = 2.5,   .rcache_num_seg =  1, .rcache_seg_size =  32, .max_multiple =  1 },
    { .name = "[ATA-1] Conner CP3024",                            .internal_name = "CP3024", .model = "Conner Peripherals 20MB - CP3024", .zones =  1,  .avg_spt = 33, .heads = 2, .rpm = 3500, .full_stroke_ms = 50, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =  8, .max_multiple =  8 }, // Needed for GRiDcase 1520 to work
    { .name = "[ATA-1] Conner CP3044",                            .internal_name = "CP3044", .model = "Conner Peripherals 40MB - CP3044", .zones =  1,  .avg_spt = 40, .heads = 2, .rpm = 3500, .full_stroke_ms = 50, .track_seek_ms = 8,   .rcache_num_seg =  1, .rcache_seg_size =  8, .max_multiple =  8 }, // Needed for GRiDcase 1520 to work
    { .name = "[ATA-1] Conner CP3104",                            .internal_name = "CP3104", .model = "Conner Peripherals 104MB - CP3104", .zones =  1,  .avg_spt = 33, .heads = 8, .rpm = 3500, .full_stroke_ms = 45, .track_seek_ms = 8,   .rcache_num_seg =  4, .rcache_seg_size =  8, .max_multiple =  8 }, // Needed for GRiDcase 1520 to work
    { .name = "[ATA-1] Conner CFS420A",                           .internal_name = "CFS420A", .model = "Conner Peripherals 420MB - CFS420A", .zones =  1,  .avg_spt = 40, .heads = 2, .rpm = 3600, .full_stroke_ms = 33, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  32, .max_multiple =  8 },
    { .name = "[ATA-1] HP Kittyhawk",                             .internal_name = "C3014A", .model = "HP C3014A", .zones =  6,  .avg_spt = 80, .heads = 3, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  16, .max_multiple =  8 },
    { .name = "[ATA-1] IBM H3256-A3",                             .internal_name = "H3256A3", .model = "H3256-A3", .zones =  1,  .avg_spt = 40, .heads = 2, .rpm = 3600, .full_stroke_ms = 32, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =  96, .max_multiple =  8 },
    { .name = "[ATA-1] IBM H3342-A4",                             .internal_name = "H3342A4", .model = "H3342-A4", .zones =  1,  .avg_spt = 40, .heads = 2, .rpm = 3600, .full_stroke_ms = 30, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =  96, .max_multiple =  8 },
    { .name = "[ATA-1] Kalok KL343",                              .internal_name = "KL343", .model = "KALOK KL-343", .zones =  1,  .avg_spt = 80, .heads = 6, .rpm = 3600, .full_stroke_ms = 50, .track_seek_ms = 2,   .rcache_num_seg =  1, .rcache_seg_size =  8, .max_multiple =  8 },
    { .name = "[ATA-1] Kalok KL3100",                             .internal_name = "KL3100", .model = "KALOK KL-3100", .zones =  1,  .avg_spt = 100, .heads = 6, .rpm = 3662, .full_stroke_ms = 50, .track_seek_ms = 2,   .rcache_num_seg =  1, .rcache_seg_size =  32, .max_multiple =  8 },
    { .name = "[ATA-1] Maxtor 7060AT",                            .internal_name = "7060AT", .model = "Maxtor 7060 AT", .zones =  1,  .avg_spt = 62, .heads = 2, .rpm = 3524, .full_stroke_ms = 30, .track_seek_ms = 3.6,   .rcache_num_seg =  1, .rcache_seg_size =  64, .max_multiple =  8 },
    { .name = "[ATA-1] Maxtor 7131AT",                            .internal_name = "7131AT", .model = "Maxtor 7131 AT", .zones =  2,  .avg_spt = 54, .heads = 2, .rpm = 3551, .full_stroke_ms = 27, .track_seek_ms = 4.5,   .rcache_num_seg =  1, .rcache_seg_size =  64, .max_multiple =  8 },
    { .name = "[ATA-1] Maxtor 7213AT",                            .internal_name = "7213AT", .model = "Maxtor 7213 AT", .zones =  4,  .avg_spt = 55, .heads = 4, .rpm = 3551, .full_stroke_ms = 28, .track_seek_ms = 6.5,   .rcache_num_seg =  1, .rcache_seg_size =  64, .max_multiple =  8 },
    { .name = "[ATA-1] Maxtor 7245AT",                            .internal_name = "7245AT", .model = "Maxtor 7245 AT", .zones =  4,  .avg_spt = 49, .heads = 4, .rpm = 3551, .full_stroke_ms = 27, .track_seek_ms = 4.4,   .rcache_num_seg =  8, .rcache_seg_size =  64, .max_multiple =  8 },
    { .name = "[ATA-1] Quantum ProDrive LPS 105",                 .internal_name = "LPS105AT", .model = "QUANTUM PRODRIVE 105", .zones =  1,  .avg_spt = 70, .heads = 2, .rpm = 3662, .full_stroke_ms = 45, .track_seek_ms = 5,   .rcache_num_seg =  1, .rcache_seg_size =  64, .max_multiple =  8 },
    { .name = "[ATA-1] Quantum ProDrive LPS 120AT",               .internal_name = "GM12A012", .model = "QUANTUM PRODRIVE 120AT", .zones =  1,  .avg_spt = 50, .heads = 2, .rpm = 3605, .full_stroke_ms = 45, .track_seek_ms = 4,   .rcache_num_seg =  1, .rcache_seg_size =  64, .max_multiple =  8 },
    { .name = "[ATA-1] Seagate ST3243A",                          .internal_name = "ST3243A", .model = "ST3243A", .zones =  1,  .avg_spt = 40, .heads = 4, .rpm = 3811, .full_stroke_ms = 32, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =  32, .max_multiple =  8 },
    { .name = "[ATA-1] Western Digital Caviar 140",               .internal_name = "AC140", .model = "WDC AC140", .zones =  4,  .avg_spt = 70, .heads = 2, .rpm = 3551, .full_stroke_ms = 28, .track_seek_ms = 6,   .rcache_num_seg =  8, .rcache_seg_size =  8, .max_multiple =  8 },
    { .name = "[ATA-1] Western Digital Caviar 280",               .internal_name = "AC280", .model = "WDC AC280", .zones =  4,  .avg_spt = 70, .heads = 4, .rpm = 3595, .full_stroke_ms = 28, .track_seek_ms = 6,   .rcache_num_seg =  8, .rcache_seg_size =  32, .max_multiple =  8 },
    { .name = "[ATA-1] Western Digital Caviar 1210",              .internal_name = "AC1210", .model = "WDC AC1210F", .zones =  4,  .avg_spt = 30, .heads = 2, .rpm = 3314, .full_stroke_ms = 33, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-1] Western Digital Caviar 2120",              .internal_name = "AC2120", .model = "WDC AC2120M", .zones =  4,  .avg_spt = 40, .heads = 2, .rpm = 3605, .full_stroke_ms = 28, .track_seek_ms = 2.8,   .rcache_num_seg =  8, .rcache_seg_size =  32, .max_multiple =  8 },
    { .name = "[ATA-2] IBM DBOA-2720",                            .internal_name = "DBOA2720", .model = "DBOA-2720", .zones =  2,  .avg_spt = 135, .heads = 2, .rpm = 4000, .full_stroke_ms = 30, .track_seek_ms = 5,   .rcache_num_seg =  4, .rcache_seg_size =  64, .max_multiple =  8 },
    { .name = "[ATA-2] IBM DeskStar 4 (DCAA-32880)",              .internal_name = "DCAA32880", .model = "IBM-DCAA-32880", .zones =  8,  .avg_spt = 185, .heads = 2, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.7,   .rcache_num_seg =  4, .rcache_seg_size =  96, .max_multiple =  16 },
    { .name = "[ATA-2] IBM DeskStar 4 (DCAA-33610)",              .internal_name = "DCAA33610", .model = "IBM-DCAA-33610", .zones =  8,  .avg_spt = 185, .heads = 3, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.7,   .rcache_num_seg =  4, .rcache_seg_size =  96, .max_multiple =  16 },
    { .name = "[ATA-2] IBM DeskStar 4 (DCAA-34330)",              .internal_name = "DCAA34330", .model = "IBM-DCAA-34330", .zones =  8,  .avg_spt = 185, .heads = 3, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.7,   .rcache_num_seg =  4, .rcache_seg_size =  96, .max_multiple =  16 },
    { .name = "[ATA-2] Maxtor 7540AV",                            .internal_name = "7540AV", .model = "Maxtor 7540 AV", .zones =  2,  .avg_spt = 120, .heads = 4, .rpm = 3551, .full_stroke_ms = 31, .track_seek_ms = 4.3,   .rcache_num_seg =  4, .rcache_seg_size =  32, .max_multiple =  8 },
    { .name = "[ATA-2] Maxtor 7546AT",                            .internal_name = "7546AT", .model = "Maxtor 7546 AT", .zones =  2,  .avg_spt = 100, .heads = 4, .rpm = 4500, .full_stroke_ms = 28, .track_seek_ms = 2.3,   .rcache_num_seg =  4, .rcache_seg_size =  256, .max_multiple =  8 },
    { .name = "[ATA-2] Maxtor 7850AV",                            .internal_name = "7850AV", .model = "Maxtor 7850 AV", .zones =  4,  .avg_spt = 120, .heads = 4, .rpm = 3551, .full_stroke_ms = 31, .track_seek_ms = 3.7,   .rcache_num_seg =  4, .rcache_seg_size =  64, .max_multiple =  8 },
    { .name = "[ATA-2] Maxtor 71336AP",                           .internal_name = "71336AP", .model = "Maxtor 71336 AP", .zones =  4,  .avg_spt = 105, .heads = 4, .rpm = 4480, .full_stroke_ms = 12, .track_seek_ms = 3.4,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-2] Quantum Bigfoot 1.2AT",                    .internal_name = "BF12A011", .model = "QUANTUM BIGFOOT BF1.2A", .zones =  2,  .avg_spt = 155, .heads = 2, .rpm = 3600, .full_stroke_ms = 30, .track_seek_ms = 3.5,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-2] Quantum Bigfoot (CY4320A)",                .internal_name = "CY4320A", .model = "QUANTUM BIGFOOT_CY4320A", .zones =  2,  .avg_spt = 130, .heads = 2, .rpm = 4000, .full_stroke_ms = 29, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  16 }, // from Hardcore Windows NT Final Segment by Kugee
    { .name = "[ATA-2] Quantum Fireball 640AT",                   .internal_name = "FB64A341", .model = "QUANTUM FIREBALL 640AT", .zones =  2,  .avg_spt = 120, .heads = 2, .rpm = 5400, .full_stroke_ms = 24, .track_seek_ms = 3.1,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum Fireball TM1080AT",                .internal_name = "TM10A462", .model = "QUANTUM FIREBALL TM1.0A", .zones =  2,  .avg_spt = 120, .heads = 2, .rpm = 4500, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Quantum Fireball TM1.2AT",                 .internal_name = "TM12A012", .model = "QUANTUM FIREBALL TM1.2A", .zones =  4,  .avg_spt = 120, .heads = 2, .rpm = 4500, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Samsung PLS-31274A",                       .internal_name = "PLS31274A", .model = "SAMSUNG PLS-31274A", .zones =  4,  .avg_spt = 110, .heads = 4, .rpm = 4500, .full_stroke_ms = 45, .track_seek_ms = 4.5,   .rcache_num_seg =  4, .rcache_seg_size =  256, .max_multiple =  8 },
    { .name = "[ATA-2] Samsung Winner-1",                         .internal_name = "WNR31601A", .model = "SAMSUNG WNR-31601A", .zones =  8,  .avg_spt = 110, .heads = 4, .rpm = 5400, .full_stroke_ms = 22, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-2] Seagate Medalist (ST3780A)",               .internal_name = "ST3780A", .model = "ST3780A", .zones =  8,  .avg_spt = 120, .heads = 4, .rpm = 4500, .full_stroke_ms = 25, .track_seek_ms = 3.5,   .rcache_num_seg =  4, .rcache_seg_size =  256, .max_multiple =  16 },
    { .name = "[ATA-2] Seagate Medalist (ST31220A)",              .internal_name = "ST31220A", .model = "ST31220A", .zones =  8,  .avg_spt = 140, .heads = 6, .rpm = 4500, .full_stroke_ms = 27, .track_seek_ms = 3.5,   .rcache_num_seg =  4, .rcache_seg_size =  256, .max_multiple =  16 },
    { .name = "[ATA-2] Seagate Medalist 210xe",                   .internal_name = "ST3250A", .model = "ST3250A", .zones =  4,  .avg_spt = 148, .heads = 2, .rpm = 3811, .full_stroke_ms = 30, .track_seek_ms = 4.1,   .rcache_num_seg =  8, .rcache_seg_size =  120, .max_multiple =  8 },
    { .name = "[ATA-2] Seagate Medalist 275xe",                   .internal_name = "ST3295A", .model = "ST3295A", .zones =  4,  .avg_spt = 130, .heads = 2, .rpm = 3811, .full_stroke_ms = 30, .track_seek_ms = 3.4,   .rcache_num_seg =  3, .rcache_seg_size =  120, .max_multiple =  8 },
    { .name = "[ATA-2] Seagate Medalist 545xe",                   .internal_name = "ST3660A", .model = "ST3660A", .zones =  4,  .avg_spt = 130, .heads = 4, .rpm = 3811, .full_stroke_ms = 34, .track_seek_ms = 3.4,   .rcache_num_seg =  8, .rcache_seg_size =  120, .max_multiple =  8 },
    { .name = "[ATA-2] Seagate Medalist 640xe",                   .internal_name = "ST3630A", .model = "ST3630A", .zones =  4,  .avg_spt = 130, .heads = 4, .rpm = 3811, .full_stroke_ms = 34, .track_seek_ms = 3.5,   .rcache_num_seg =  8, .rcache_seg_size =  120, .max_multiple =  8 },
    { .name = "[ATA-2] Seagate Medalist 850xe",                   .internal_name = "ST3850A", .model = "ST3850A", .zones =  8,  .avg_spt = 150, .heads = 4, .rpm = 3811, .full_stroke_ms = 34, .track_seek_ms = 3.8,   .rcache_num_seg =  8, .rcache_seg_size =  120, .max_multiple =  8 },
    { .name = "[ATA-2] Seagate Medalist 1270SL",                  .internal_name = "ST51270A", .model = "ST51270A", .zones =  8,  .avg_spt = 205, .heads = 3, .rpm = 5736, .full_stroke_ms = 25, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-2] Seagate Medalist 3240",                    .internal_name = "ST33240A", .model = "ST33240A", .zones =  16,  .avg_spt = 225, .heads = 8, .rpm = 4500, .full_stroke_ms = 25, .track_seek_ms = 2.5,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-2] Toshiba MK2101MAN (HDD2616)",              .internal_name = "HDD2616", .model = "TOSHIBA MK2101MAN", .zones =  8,  .avg_spt = 130, .heads = 10, .rpm = 4200, .full_stroke_ms = 36, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-2] Western Digital Caviar 2540",              .internal_name = "AC2540", .model = "WDC AC2540H", .zones =  4,  .avg_spt = 150, .heads = 2, .rpm = 4500, .full_stroke_ms = 12, .track_seek_ms = 4,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Western Digital Caviar 2850",              .internal_name = "AC2850", .model = "WDC AC2850F", .zones =  4,  .avg_spt = 130, .heads = 2, .rpm = 5200, .full_stroke_ms = 12, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Western Digital Caviar 11000",             .internal_name = "AC11000", .model = "WDC AC11000H", .zones =  4,  .avg_spt = 120, .heads = 2, .rpm = 5200, .full_stroke_ms = 12, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Western Digital Caviar 21200",             .internal_name = "AC21200", .model = "WDC AC21200H", .zones =  4,  .avg_spt = 110, .heads = 4, .rpm = 5200, .full_stroke_ms = 39, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Western Digital Caviar 21600",             .internal_name = "AC21600", .model = "WDC AC21600H", .zones =  8,  .avg_spt = 140, .heads = 4, .rpm = 5200, .full_stroke_ms = 30, .track_seek_ms = 3,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Western Digital Caviar 22000",             .internal_name = "AC22000", .model = "WDC AC22000LA", .zones =  8,  .avg_spt = 130, .heads = 3, .rpm = 5200, .full_stroke_ms = 33, .track_seek_ms = 3.5,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  8 },
    { .name = "[ATA-2] Western Digital Caviar 22100",             .internal_name = "AC22100", .model = "WDC AC22100H", .zones =  8,  .avg_spt = 140, .heads = 4, .rpm = 5200, .full_stroke_ms = 30, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-2] Western Digital Caviar 31200",             .internal_name = "AC31200", .model = "WDC AC31200F", .zones =  8,  .avg_spt = 210, .heads = 4, .rpm = 4500, .full_stroke_ms = 12, .track_seek_ms = 4,   .rcache_num_seg =  8, .rcache_seg_size =  64, .max_multiple =  16 },
    { .name = "[ATA-3] Connor CFS1275A",                          .internal_name = "CFS1275A", .model = "Connor Peripherals 1275MB - CFS1275A", .zones =  4,  .avg_spt = 130, .heads = 2, .rpm = 4500, .full_stroke_ms = 25, .track_seek_ms = 3.8,   .rcache_num_seg =  4, .rcache_seg_size =  64, .max_multiple =  16 }, // Either ATA-2 or ATA-3
    { .name = "[ATA-3] Fujitsu MPA3017AT",                        .internal_name = "MPA3017AT", .model = "FUJITSU MPA3017AT", .zones =  5,  .avg_spt = 195, .heads = 2, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 3.2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-3] Fujitsu MPA3026AT",                        .internal_name = "MPA3026AT", .model = "FUJITSU MPA3026AT", .zones =  8,  .avg_spt = 195, .heads = 3, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 3.2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-3] Fujitsu MPA3035AT",                        .internal_name = "MPA3035AT", .model = "FUJITSU MPA3035AT", .zones =  11,  .avg_spt = 195, .heads = 4, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 3.2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-3] Fujitsu MPA3043AT",                        .internal_name = "MPA3043AT", .model = "FUJITSU MPA3043AT", .zones =  15,  .avg_spt = 195, .heads = 5, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 3.2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-3] Fujitsu MPA3052AT",                        .internal_name = "MPA3052AT", .model = "FUJITSU MPA3052AT", .zones =  16,  .avg_spt = 195, .heads = 6, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 3.2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-3] Samsung Voyager 6",                        .internal_name = "SV0844A", .model = "SAMSUNG SV0844A", .zones =  8,  .avg_spt = 205, .heads = 4, .rpm = 5400, .full_stroke_ms = 22, .track_seek_ms = 2,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-3] Samsung Winner 5X",                        .internal_name = "WU33205A", .model = "SAMSUNG WU33205A", .zones =  16,  .avg_spt = 200, .heads = 4, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-3] Seagate Medalist 636",                     .internal_name = "ST3636A", .model = "Seagate Technology 636MB - ST3636A", .zones =  2,  .avg_spt = 130, .heads = 2, .rpm = 4500, .full_stroke_ms = 25, .track_seek_ms = 3.8,   .rcache_num_seg =  4, .rcache_seg_size =  64, .max_multiple =  8 },
    { .name = "[ATA-3] Seagate Medalist 1082",                    .internal_name = "ST31082A", .model = "Seagate Technology 1082MB - ST31082A", .zones =  4,  .avg_spt = 130, .heads = 3, .rpm = 4500, .full_stroke_ms = 25, .track_seek_ms = 3.8,   .rcache_num_seg =  4, .rcache_seg_size =  64, .max_multiple =  8 },
    { .name = "[ATA-3] Seagate Medalist 1276",                    .internal_name = "ST31276A", .model = "Seagate Technology 1275MB - ST31276A", .zones =  4,  .avg_spt = 130, .heads = 3, .rpm = 4500, .full_stroke_ms = 25, .track_seek_ms = 3.8,   .rcache_num_seg =  4, .rcache_seg_size =  64, .max_multiple =  16 },
    { .name = "[ATA-3] Seagate Medalist 1720",                    .internal_name = "ST31720A", .model = "ST31720A", .zones =  4,  .avg_spt = 120, .heads = 4, .rpm = 4500, .full_stroke_ms = 25, .track_seek_ms = 2,   .rcache_num_seg =  4, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-3] Seagate Medalist 2132",                    .internal_name = "ST32132A", .model = "ST32132A", .zones =  8,  .avg_spt = 125, .heads = 6, .rpm = 4500, .full_stroke_ms = 30, .track_seek_ms = 2.3,   .rcache_num_seg =  8, .rcache_seg_size =  120, .max_multiple =  16 },
    { .name = "[ATA-3] Western Digital Caviar 21700",             .internal_name = "AC21700", .model = "WDC AC21700H", .zones =  8,  .avg_spt = 185, .heads = 3, .rpm = 5200, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  16 }, // Apple Computer OEM only, not retail version
    { .name = "[ATA-4] Fujitsu MPB3021AT",                        .internal_name = "MPB3021AT", .model = "FUJITSU MPB3021AT", .zones =  7,  .avg_spt = 200, .heads = 3, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2.5,   .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple =  16 },
    { .name = "[ATA-4] Fujitsu MPD3043AT",                        .internal_name = "MPD3043AT", .model = "FUJITSU MPD3043AT", .zones =  5,  .avg_spt = 195, .heads = 2, .rpm = 5400, .full_stroke_ms = 29, .track_seek_ms = 1.5,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple =  16 },
    { .name = "[ATA-4] Fujitsu MPD3064AT",                        .internal_name = "MPD3064AT", .model = "FUJITSU MPD3064AT", .zones =  7,  .avg_spt = 195, .heads = 3, .rpm = 5400, .full_stroke_ms = 30, .track_seek_ms = 1.5,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple =  16 },
    { .name = "[ATA-4] Fujitsu MPD3084AT",                        .internal_name = "MPD3084AT", .model = "FUJITSU MPD3084AT", .zones =  7,  .avg_spt = 195, .heads = 4, .rpm = 5400, .full_stroke_ms = 19, .track_seek_ms = 1.5,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  16 },
    { .name = "[ATA-4] Fujitsu MPE3064AT",                        .internal_name = "MPE3064AT", .model = "FUJITSU MPE3064AT", .zones =  7,  .avg_spt = 295, .heads = 2, .rpm = 5400, .full_stroke_ms = 30, .track_seek_ms = 1.5,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-4] Maxtor DiamondMax 2160",                   .internal_name = "86480D6", .model = "Maxtor 86480D6", .zones =  8,  .avg_spt = 197, .heads = 4, .rpm = 5200, .full_stroke_ms = 18, .track_seek_ms = 1,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-4] Maxtor DiamondMax 2880",                   .internal_name = "90432D3", .model = "Maxtor 90432D3", .zones =  16,  .avg_spt = 190, .heads = 3, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 1,   .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple =  32 },
    { .name = "[ATA-4] Maxtor DiamondMax 3400",                   .internal_name = "90644D3", .model = "Maxtor 90644D3", .zones =  16,  .avg_spt = 290, .heads = 3, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 0.9,   .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple =  32 },
    { .name = "[ATA-4] Maxtor DiamondMax 4320 (90432D2)",         .internal_name = "90432D2", .model = "Maxtor 90432D2", .zones =  16,  .avg_spt = 290, .heads = 2, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 0.9,   .rcache_num_seg =  16, .rcache_seg_size =  256, .max_multiple =  32 },
    { .name = "[ATA-4] Maxtor DiamondMax 4320 (90845D4)",         .internal_name = "90845D4", .model = "Maxtor 90845D4", .zones =  16,  .avg_spt = 290, .heads = 3, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 0.9,   .rcache_num_seg =  16, .rcache_seg_size =  256, .max_multiple =  32 },
    { .name = "[ATA-4] Maxtor DiamondMax Plus 6800 (90683U2)",    .internal_name = "90683U2", .model = "Maxtor 90683U2", .zones =  16,  .avg_spt = 290, .heads = 2, .rpm = 7200, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg =  16, .rcache_seg_size =  256, .max_multiple =  32 },
    { .name = "[ATA-4] Maxtor DiamondMax Plus 6800 (91024U3)",    .internal_name = "91024U3", .model = "Maxtor 91024U3", .zones =  16,  .avg_spt = 290, .heads = 3, .rpm = 7200, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg =  16, .rcache_seg_size =  256, .max_multiple =  32 },
    { .name = "[ATA-4] Maxtor DiamondMax Plus 6800 (91366U4)",    .internal_name = "91366U4", .model = "Maxtor 91366U4", .zones =  16,  .avg_spt = 290, .heads = 4, .rpm = 7200, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg =  16, .rcache_seg_size =  256, .max_multiple =  32 },
    { .name = "[ATA-4] Maxtor DiamondMax Plus 6800 (92049U6)",    .internal_name = "92049U6", .model = "Maxtor 92049U6", .zones =  16,  .avg_spt = 290, .heads = 6, .rpm = 7200, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg =  16, .rcache_seg_size =  256, .max_multiple =  32 },
    { .name = "[ATA-4] Maxtor DiamondMax Plus 6800 (92732U8)",    .internal_name = "92732U8", .model = "Maxtor 92732U8", .zones =  16,  .avg_spt = 290, .heads = 8, .rpm = 7200, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg =  16, .rcache_seg_size =  256, .max_multiple =  32 },
    { .name = "[ATA-4] Quantum Bigfoot TX4.3AT",                  .internal_name = "TX043A011", .model = "QUANTUM BIGFOOT TX4.3A", .zones =  2,  .avg_spt = 220, .heads = 2, .rpm = 4000, .full_stroke_ms = 30, .track_seek_ms = 2.5,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  32 },
    { .name = "[ATA-4] Quantum Fireball ST3.2AT",                 .internal_name = "ST32A461", .model = "QUANTUM FIREBALL ST3.2A", .zones =  4,  .avg_spt = 200, .heads = 4, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-4] Quantum Fireball SE4.3A",                  .internal_name = "SE43A011", .model = "QUANTUM FIREBALL SE4.3A", .zones =  2,  .avg_spt = 200, .heads = 4, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-4] Quantum Fireball SE6.4A",                  .internal_name = "SE64A011", .model = "QUANTUM FIREBALL SE6.4A", .zones =  3,  .avg_spt = 200, .heads = 6, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-4] Quantum Fireball SE8.4A",                  .internal_name = "SE84A011", .model = "QUANTUM FIREBALL SE8.4A", .zones =  4,  .avg_spt = 200, .heads = 8, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-4] Seagate Medalist 2122",                    .internal_name = "ST32122A", .model = "ST32122A", .zones =  16,  .avg_spt = 215, .heads = 2, .rpm = 4500, .full_stroke_ms = 23, .track_seek_ms = 3.8,   .rcache_num_seg =  16, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-4] Seagate Medalist 3321",                    .internal_name = "ST33221A", .model = "ST33221A", .zones =  16,  .avg_spt = 210, .heads = 4, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 1.7,   .rcache_num_seg =  16, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-4] Seagate Medalist 4321",                    .internal_name = "ST34321A", .model = "ST34321A", .zones =  16,  .avg_spt = 210, .heads = 4, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 2.2,   .rcache_num_seg =  16, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-4] Seagate Medalist 6531",                    .internal_name = "ST36531A", .model = "ST36531A", .zones =  16,  .avg_spt = 215, .heads = 6, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 1.7,   .rcache_num_seg =  16, .rcache_seg_size =  128, .max_multiple =  16 },
    { .name = "[ATA-4] Seagate Medalist 8420",                    .internal_name = "ST38420A", .model = "ST38420A", .zones =  16,  .avg_spt = 290, .heads = 4, .rpm = 5400, .full_stroke_ms = 16, .track_seek_ms = 1.5,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-4] Seagate Medalist 13030",                   .internal_name = "ST313030A", .model = "ST313030A", .zones =  16,  .avg_spt = 290, .heads = 6, .rpm = 5400, .full_stroke_ms = 16, .track_seek_ms = 1.5,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-4] Seagate Medalist 17240",                   .internal_name = "ST317240A", .model = "ST317240A", .zones =  16,  .avg_spt = 290, .heads = 8, .rpm = 5400, .full_stroke_ms = 16, .track_seek_ms = 1.5,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-4] Toshiba MK4006MAV",                        .internal_name = "MK4006MAV", .model = "TOSHIBA MK4006MAV", .zones =  8,  .avg_spt = 230, .heads = 6, .rpm = 4200, .full_stroke_ms = 25, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-4] Western Digital Caviar 14300",             .internal_name = "AC14300", .model = "WDC AC14300RT", .zones =  16,  .avg_spt = 195, .heads = 2, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 5.5,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple =  16 },
    { .name = "[ATA-4] Western Digital Caviar 23200",             .internal_name = "AC23200", .model = "WDC AC23200LB", .zones =  16,  .avg_spt = 210, .heads = 4, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  256, .max_multiple =  32 },
    { .name = "[ATA-4] Western Digital Caviar 26400",             .internal_name = "AC26400", .model = "WDC AC26400RN", .zones =  16,  .avg_spt = 295, .heads = 5, .rpm = 5400, .full_stroke_ms = 21, .track_seek_ms = 3,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-4] Western Digital Caviar 33200",             .internal_name = "AC33200", .model = "WDC AC33200LA", .zones =  16,  .avg_spt = 310, .heads = 5, .rpm = 5200, .full_stroke_ms = 40, .track_seek_ms = 3,   .rcache_num_seg =  16, .rcache_seg_size =  256, .max_multiple =  32 },
    { .name = "[ATA-5] IBM Travelstar 6GN",                       .internal_name = "DARA206000", .model = "IBM-DARA-206000", .zones =  12,  .avg_spt = 292, .heads = 2, .rpm = 4200, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-5] IBM Travelstar 9GN",                       .internal_name = "DARA209000", .model = "IBM-DARA-209000", .zones =  12,  .avg_spt = 292, .heads = 3, .rpm = 4200, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-5] IBM/Hitachi Travelstar 12GN",              .internal_name = "DARA212000", .model = "IBM-DARA-212000", .zones =  12,  .avg_spt = 292, .heads = 4, .rpm = 4200, .full_stroke_ms = 31, .track_seek_ms = 4,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 }, // Either Hitachi or IBM OEM
    { .name = "[ATA-5] Maxtor DiamondMax VL 17",                  .internal_name = "90871U2", .model = "Maxtor 90871U2", .zones =  16,  .avg_spt = 290, .heads = 3, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 0.9,   .rcache_num_seg =  16, .rcache_seg_size =  256, .max_multiple =  32 },
    { .name = "[ATA-5] Maxtor DiamondMax VL 20 (91021U2)",        .internal_name = "91021U2", .model = "Maxtor 91021U2", .zones =  16,  .avg_spt = 295, .heads = 2, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-5] Maxtor DiamondMax VL 20 (91531U3)",        .internal_name = "91531U3", .model = "Maxtor 91531U3", .zones =  16,  .avg_spt = 295, .heads = 3, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-5] Maxtor DiamondMax VL 20 (92041U4)",        .internal_name = "92041U4", .model = "Maxtor 92041U4", .zones =  16,  .avg_spt = 295, .heads = 4, .rpm = 5400, .full_stroke_ms = 20, .track_seek_ms = 1,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-5] Quantum Fireball EX3.2A",                  .internal_name = "EX32A012", .model = "QUANTUM FIREBALL EX3.2A", .zones =  1,  .avg_spt = 210, .heads = 2, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple =  16 },
    { .name = "[ATA-5] Quantum Fireball EX5.1A",                  .internal_name = "EX51A012", .model = "QUANTUM FIREBALL EX5.1A", .zones =  2,  .avg_spt = 210, .heads = 3, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple =  16 },
    { .name = "[ATA-5] Quantum Fireball EX6.4A",                  .internal_name = "EX64A012", .model = "QUANTUM FIREBALL EX6.4A", .zones =  2,  .avg_spt = 210, .heads = 4, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple =  16 },
    { .name = "[ATA-5] Quantum Fireball EX10.2A",                 .internal_name = "EX10A011", .model = "QUANTUM FIREBALL EX10.2A", .zones =  3, .avg_spt = 210, .heads = 6, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,     .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple =  16 },
    { .name = "[ATA-5] Quantum Fireball EX12.7A",                 .internal_name = "EX12A011", .model = "QUANTUM FIREBALL EX12.7A", .zones =  4, .avg_spt = 210, .heads = 8, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,     .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple =  16 },
    { .name = "[ATA-5] Quantum Fireball CR4.3A",                  .internal_name = "CR43A013", .model = "QUANTUM FIREBALL CR4.3A", .zones =  2,  .avg_spt = 310, .heads = 3, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple =  16 },
    { .name = "[ATA-5] Quantum Fireball CR6.4A",                  .internal_name = "CR64A011", .model = "QUANTUM FIREBALL CR6.4A", .zones =  2,  .avg_spt = 310, .heads = 4, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple =  16 },  
    { .name = "[ATA-5] Quantum Fireball CR8.4A",                  .internal_name = "CR84A011", .model = "QUANTUM FIREBALL CR8.4A", .zones =  3,  .avg_spt = 310, .heads = 6, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple =  16 },
    { .name = "[ATA-5] Quantum Fireball CR13.0A",                 .internal_name = "CR13A011", .model = "QUANTUM FIREBALL CR13.0A", .zones =  4,  .avg_spt = 310, .heads = 8, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 2,   .rcache_num_seg =  8, .rcache_seg_size =  512, .max_multiple =  16 },
    { .name = "[ATA-5] Samsung SpinPoint V6800 (SV0682D)",        .internal_name = "SV0682D", .model = "SAMSUNG SV0682D", .zones =  8,  .avg_spt = 295, .heads = 2, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 1.3,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-5] Samsung SpinPoint V6800 (SV1023D)",        .internal_name = "SV1023D", .model = "SAMSUNG SV1023D", .zones =  8,  .avg_spt = 295, .heads = 3, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 1.3,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-5] Samsung SpinPoint V6800 (SV1364D)",        .internal_name = "SV1364D", .model = "SAMSUNG SV1364D", .zones =  8,  .avg_spt = 295, .heads = 4, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 1.3,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-5] Samsung SpinPoint V6800 (SV1705D)",        .internal_name = "SV1705D", .model = "SAMSUNG SV1705D", .zones =  8,  .avg_spt = 295, .heads = 5, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 1.3,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-5] Samsung SpinPoint V6800 (SV2046D)",        .internal_name = "SV2046D", .model = "SAMSUNG SV2046D", .zones =  8,  .avg_spt = 295, .heads = 6, .rpm = 5400, .full_stroke_ms = 18, .track_seek_ms = 1.3,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-5] Seagate U8 - 4.3gb",                       .internal_name = "ST34313A", .model = "ST34313A", .zones =  16,  .avg_spt = 289, .heads = 1, .rpm = 5400, .full_stroke_ms = 25, .track_seek_ms = 1.5,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-5] Seagate U8 - 8.4gb",                       .internal_name = "ST38410A", .model = "ST38410A", .zones =  16,  .avg_spt = 289, .heads = 2, .rpm = 5400, .full_stroke_ms = 25, .track_seek_ms = 1.5,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-5] Seagate U8 - 13gb",                        .internal_name = "ST313021A", .model = "ST313021A", .zones =  16,  .avg_spt = 289, .heads = 4, .rpm = 5400, .full_stroke_ms = 25, .track_seek_ms = 1.5,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-5] Seagate U8 - 17.2gb",                      .internal_name = "ST317221A", .model = "ST317221A", .zones =  16,  .avg_spt = 289, .heads = 3, .rpm = 5400, .full_stroke_ms = 25, .track_seek_ms = 1.5,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-5] Western Digital Caviar 102AA",             .internal_name = "WD102AA", .model = "WDC WD102AA-00ANA0", .zones =  16,  .avg_spt = 295, .heads = 8, .rpm = 5400, .full_stroke_ms = 12, .track_seek_ms = 1.5,   .rcache_num_seg =  16, .rcache_seg_size =  512, .max_multiple =  32 },
    { .name = "[ATA-5] Western Digital Expert",                   .internal_name = "WD135BA", .model = "WDC WD135BA-60AK", .zones =  16,  .avg_spt = 350, .heads = 6, .rpm = 7200, .full_stroke_ms = 15, .track_seek_ms = 2,   .rcache_num_seg =  16, .rcache_seg_size =  1920, .max_multiple =  32 },
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
