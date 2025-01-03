/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Generic CD-ROM drive core.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2018-2021 Miran Grca.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/config.h>
#include <86box/cdrom.h>
#include <86box/cdrom_image.h>
#include <86box/cdrom_interface.h>
#include <86box/cdrom_mitsumi.h>
#include <86box/plat.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/sound.h>

/* The addresses sent from the guest are absolute, ie. a LBA of 0 corresponds to a MSF of 00:00:00. Otherwise, the counter displayed by the guest is wrong:
   there is a seeming 2 seconds in which audio plays but counter does not move, while a data track before audio jumps to 2 seconds before the actual start
   of the audio while audio still plays. With an absolute conversion, the counter is fine. */
#undef MSFtoLBA
#define MSFtoLBA(m, s, f)  ((((m * 60) + s) * 75) + f)

#define RAW_SECTOR_SIZE    2352
#define COOKED_SECTOR_SIZE 2048

#define MIN_SEEK           2000
#define MAX_SEEK           333333

#pragma pack(push, 1)
typedef struct {
    uint8_t user_data[2048],
        ecc[288];
} m1_data_t;

typedef struct {
    uint8_t sub_header[8],
        user_data[2328];
} m2_data_t;

typedef union {
    m1_data_t m1_data;
    m2_data_t m2_data;
    uint8_t   raw_data[2336];
} sector_data_t;

typedef struct {
    uint8_t       sync[12];
    uint8_t       header[4];
    sector_data_t data;
} sector_raw_data_t;

typedef union {
    sector_raw_data_t sector_data;
    uint8_t           raw_data[2352];
} sector_t;

typedef struct {
    sector_t sector;
    uint8_t  c2[296];
    uint8_t  subchannel_raw[96];
    uint8_t  subchannel_q[16];
    uint8_t  subchannel_rw[96];
} cdrom_sector_t;

typedef union {
    cdrom_sector_t cdrom_sector;
    uint8_t        buffer[2856];
} sector_buffer_t;
#pragma pack(pop)

static int     cdrom_sector_size;
static uint8_t raw_buffer[2856]; /* Needs to be the same size as sector_buffer_t in the structs. */
static uint8_t extra_buffer[296];

cdrom_t cdrom[CDROM_NUM];

int cdrom_interface_current;

#ifdef ENABLE_CDROM_LOG
int cdrom_do_log = ENABLE_CDROM_LOG;

void
cdrom_log(const char *fmt, ...)
{
    va_list ap;

    if (cdrom_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define cdrom_log(fmt, ...)
#endif

static const device_t cdrom_interface_none_device = {
    .name          = "None",
    .internal_name = "none",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

static const struct {
    const device_t *device;
} controllers[] = {
    // clang-format off
    { &cdrom_interface_none_device  },
    { NULL                          }
    // clang-format on
};

/* Reset the CD-ROM Interface, whichever one that is. */
void
cdrom_interface_reset(void)
{
    cdrom_log("CD-ROM Interface: reset(current=%d)\n",
            cdrom_interface_current);

    /* If we have a valid controller, add its device. */
    if ((cdrom_interface_current > 0) && controllers[cdrom_interface_current].device)
        device_add(controllers[cdrom_interface_current].device);
}

const char *
cdrom_interface_get_internal_name(int cdinterface)
{
    return device_get_internal_name(controllers[cdinterface].device);
}

int
cdrom_interface_get_from_internal_name(char *s)
{
    int c = 0;

    while (controllers[c].device != NULL) {
        if (!strcmp(controllers[c].device->internal_name, s))
            return c;
        c++;
    }

    return 0;
}

const device_t *
cdrom_interface_get_device(int cdinterface)
{
    return (controllers[cdinterface].device);
}

int
cdrom_interface_has_config(int cdinterface)
{
    const device_t *dev = cdrom_interface_get_device(cdinterface);

    if (dev == NULL)
        return 0;

    if (!device_has_config(dev))
        return 0;

    return 1;
}

int
cdrom_interface_get_flags(int cdinterface)
{
    return (controllers[cdinterface].device->flags);
}

int
cdrom_interface_available(int cdinterface)
{
    return (device_available(controllers[cdinterface].device));
}

char *
cdrom_getname(int type)
{
    return (char *) cdrom_drive_types[type].name;
}

char *
cdrom_get_internal_name(int type)
{
    return (char *) cdrom_drive_types[type].internal_name;
}

int
cdrom_get_from_internal_name(char *s)
{
    int c = 0;

    while (strlen(cdrom_drive_types[c].internal_name)) {
        if (!strcmp((char *) cdrom_drive_types[c].internal_name, s))
            return c;
        c++;
    }

    return 0;
}

void
cdrom_set_type(int model, int type)
{
    cdrom[model].type = type;
}

int
cdrom_get_type(int model)
{
    return cdrom[model].type;
}

static __inline int
bin2bcd(int x)
{
    return (x % 10) | ((x / 10) << 4);
}

static __inline int
bcd2bin(int x)
{
    return (x >> 4) * 10 + (x & 0x0f);
}

int
cdrom_lba_to_msf_accurate(int lba)
{
    int pos;
    int m;
    int s;
    int f;

    pos = lba + 150;
    f   = pos % 75;
    pos -= f;
    pos /= 75;
    s = pos % 60;
    pos -= s;
    pos /= 60;
    m = pos;

    return ((m << 16) | (s << 8) | f);
}

static double
cdrom_get_short_seek(cdrom_t *dev)
{
    switch (dev->cur_speed) {
        case 0:
            fatal("CD-ROM %i: 0x speed\n", dev->id);
            return 0.0;
        case 1:
            return 240.0;
        case 2:
            return 160.0;
        case 3:
            return 150.0;
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
            return 112.0;
        case 12:
        case 13:
        case 14:
        case 15:
            return 75.0;
        case 16:
        case 17:
        case 18:
        case 19:
            return 58.0;
        case 20:
        case 21:
        case 22:
        case 23:
        case 40:
        case 41:
        case 42:
        case 43:
        case 44:
        case 45:
        case 46:
        case 47:
        case 48:
            return 50.0;
        default:
            /* 24-32, 52+ */
            return 45.0;
    }
}

static double
cdrom_get_long_seek(cdrom_t *dev)
{
    switch (dev->cur_speed) {
        case 0:
            fatal("CD-ROM %i: 0x speed\n", dev->id);
            return 0.0;
        case 1:
            return 1446.0;
        case 2:
            return 1000.0;
        case 3:
            return 900.0;
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
            return 675.0;
        case 12:
        case 13:
        case 14:
        case 15:
            return 400.0;
        case 16:
        case 17:
        case 18:
        case 19:
            return 350.0;
        case 20:
        case 21:
        case 22:
        case 23:
        case 40:
        case 41:
        case 42:
        case 43:
        case 44:
        case 45:
        case 46:
        case 47:
        case 48:
            return 300.0;
        default:
            /* 24-32, 52+ */
            return 270.0;
    }
}

double
cdrom_seek_time(cdrom_t *dev)
{
    uint32_t diff = dev->seek_diff;
    double   sd   = (double) (MAX_SEEK - MIN_SEEK);

    if (diff < MIN_SEEK)
        return 0.0;
    if (diff > MAX_SEEK)
        diff = MAX_SEEK;

    diff -= MIN_SEEK;

    return cdrom_get_short_seek(dev) + ((cdrom_get_long_seek(dev) * ((double) diff)) / sd);
}

void
cdrom_stop(cdrom_t *dev)
{
    if (dev->cd_status > CD_STATUS_DATA_ONLY)
        dev->cd_status = CD_STATUS_STOPPED;
}

void
cdrom_seek(cdrom_t *dev, uint32_t pos, uint8_t vendor_type)
{
    int      m;
    int      s;
    int      f;

    if (!dev)
        return;

    cdrom_log("CD-ROM %i: Seek to LBA %08X, vendor type = %02x.\n", dev->id, pos, vendor_type);

    switch (vendor_type) {
        case 0x40:
            m = bcd2bin((pos >> 24) & 0xff);
            s = bcd2bin((pos >> 16) & 0xff);
            f = bcd2bin((pos >> 8) & 0xff);
            pos = MSFtoLBA(m, s, f) - 150;
            break;
        case 0x80:
            pos = bcd2bin((pos >> 24) & 0xff);
            break;
        default:
            break;
    }

    dev->seek_pos = pos;
    cdrom_stop(dev);
}

int
cdrom_is_pre(cdrom_t *dev, uint32_t lba)
{
    if (dev->ops && dev->ops->is_track_pre)
        return dev->ops->is_track_pre(dev, lba);

    return 0;
}

int
cdrom_audio_callback(cdrom_t *dev, int16_t *output, int len)
{
    int ret = 1;

    if (!dev->sound_on || (dev->cd_status != CD_STATUS_PLAYING) || dev->audio_muted_soft) {
        // cdrom_log("CD-ROM %i: Audio callback while not playing\n", dev->id);
        if (dev->cd_status == CD_STATUS_PLAYING)
            dev->seek_pos += (len >> 11);
        memset(output, 0, len * 2);
        return 0;
    }

    while (dev->cd_buflen < len) {
        if (dev->seek_pos < dev->cd_end) {
            if (dev->ops->read_sector(dev, (uint8_t *) &(dev->cd_buffer[dev->cd_buflen]), dev->seek_pos)) {
                cdrom_log("CD-ROM %i: Read LBA %08X successful\n", dev->id, dev->seek_pos);
                memcpy(dev->subch_buffer, ((uint8_t *) &(dev->cd_buffer[dev->cd_buflen])) + 2352, 96);
                dev->seek_pos++;
                dev->cd_buflen += (RAW_SECTOR_SIZE / 2);
                ret = 1;
            } else {
                cdrom_log("CD-ROM %i: Read LBA %08X failed\n", dev->id, dev->seek_pos);
                memset(&(dev->cd_buffer[dev->cd_buflen]), 0x00, (BUF_SIZE - dev->cd_buflen) * 2);
                dev->cd_status    = CD_STATUS_STOPPED;
                dev->cd_buflen    = len;
                ret               = 0;
            }
        } else {
            cdrom_log("CD-ROM %i: Playing completed\n", dev->id);
            memset(&dev->cd_buffer[dev->cd_buflen], 0x00, (BUF_SIZE - dev->cd_buflen) * 2);
            dev->cd_status    = CD_STATUS_PLAYING_COMPLETED;
            dev->cd_buflen    = len;
            ret               = 0;
        }
    }

    memcpy(output, dev->cd_buffer, len * 2);
    memmove(dev->cd_buffer, &dev->cd_buffer[len], (BUF_SIZE - len) * 2);
    dev->cd_buflen -= len;

    cdrom_log("CD-ROM %i: Audio callback returning %i\n", dev->id, ret);
    return ret;
}

static void
msf_from_bcd(int *m, int *s, int *f)
{
    *m = bcd2bin(*m);
    *s = bcd2bin(*s);
    *f = bcd2bin(*f);
}

static void
msf_to_bcd(int *m, int *s, int *f)
{
    *m = bin2bcd(*m);
    *s = bin2bcd(*s);
    *f = bin2bcd(*f);
}

uint8_t
cdrom_audio_play(cdrom_t *dev, uint32_t pos, uint32_t len, int ismsf)
{
    track_info_t ti;
    int          m = 0;
    int          s = 0;
    int          f = 0;

    if (dev->cd_status == CD_STATUS_DATA_ONLY)
        return 0;

    cdrom_log("CD-ROM %i: Play audio - %08X %08X %i\n", dev->id, pos, len, ismsf);
    if (ismsf & 0x100) {
        /* Track-relative audio play. */
        dev->ops->get_track_info(dev, ismsf & 0xff, 0, &ti);
        pos += MSFtoLBA(ti.m, ti.s, ti.f) - 150;
    } else if ((ismsf == 2) || (ismsf == 3)) {
        dev->ops->get_track_info(dev, pos, 0, &ti);
        pos = MSFtoLBA(ti.m, ti.s, ti.f) - 150;
        if (ismsf == 2) {
            /* We have to end at the *end* of the specified track,
               not at the beginning. */
            dev->ops->get_track_info(dev, len, 1, &ti);
            len = MSFtoLBA(ti.m, ti.s, ti.f) - 150;
        }
    } else if (ismsf == 1) {
        m = (pos >> 16) & 0xff;
        s = (pos >> 8) & 0xff;
        f = pos & 0xff;

        /* NEC CDR-260 speaks BCD. */
        if ((dev->type == CDROM_TYPE_NEC_260_100) || (dev->type == CDROM_TYPE_NEC_260_101)) /*NEC*/
            msf_from_bcd(&m, &s, &f);

        if (pos == 0xffffff) {
            cdrom_log("CD-ROM %i: Playing from current position (MSF)\n", dev->id);
            pos = dev->seek_pos;
        } else
            pos = MSFtoLBA(m, s, f) - 150;

        m = (len >> 16) & 0xff;
        s = (len >> 8) & 0xff;
        f = len & 0xff;

        /* NEC CDR-260 speaks BCD. */
        if ((dev->type == CDROM_TYPE_NEC_260_100) || (dev->type == CDROM_TYPE_NEC_260_101)) /*NEC*/
            msf_from_bcd(&m, &s, &f);

        len = MSFtoLBA(m, s, f) - 150;

        cdrom_log("CD-ROM %i: MSF - pos = %08X len = %08X\n", dev->id, pos, len);
    } else if (ismsf == 0) {
        if (pos == 0xffffffff) {
            cdrom_log("CD-ROM %i: Playing from current position\n", dev->id);
            pos = dev->seek_pos;
        }
        len += pos;
    }

    dev->audio_muted_soft = 0;
    /* Do this at this point, since it's at this point that we know the
       actual LBA position to start playing from. */
    if (!(dev->ops->track_type(dev, pos) & CD_TRACK_AUDIO)) {
        cdrom_log("CD-ROM %i: LBA %08X not on an audio track\n", dev->id, pos);
        cdrom_stop(dev);
        return 0;
    }

    dev->seek_pos  = pos;
    dev->cd_end    = len;
    dev->cd_status = CD_STATUS_PLAYING;
    dev->cd_buflen = 0;
    return 1;
}

uint8_t
cdrom_audio_track_search(cdrom_t *dev, uint32_t pos, int type, uint8_t playbit)
{
    int m = 0;
    int s = 0;
    int f = 0;
    uint32_t pos2 = 0;

    if (dev->cd_status == CD_STATUS_DATA_ONLY)
        return 0;

    cdrom_log("Audio Track Search: MSF = %06x, type = %02x, playbit = %02x\n", pos, type, playbit);
    switch (type) {
        case 0x00:
            if (pos == 0xffffffff) {
                cdrom_log("CD-ROM %i: (type 0) Search from current position\n", dev->id);
                pos = dev->seek_pos;
            }
            dev->seek_pos = pos;
            break;
        case 0x40:
            m   = bcd2bin((pos >> 24) & 0xff);
            s   = bcd2bin((pos >> 16) & 0xff);
            f   = bcd2bin((pos >> 8) & 0xff);
            if (pos == 0xffffffff) {
                cdrom_log("CD-ROM %i: (type 1) Search from current position\n", dev->id);
                pos = dev->seek_pos;
            } else
                pos = MSFtoLBA(m, s, f) - 150;

            dev->seek_pos = pos;
            break;
        case 0x80:
            if (pos == 0xffffffff) {
                cdrom_log("CD-ROM %i: (type 2) Search from current position\n", dev->id);
                pos = dev->seek_pos;
            }
            dev->seek_pos = (pos >> 24) & 0xff;
            break;
        default:
            break;
    }

    pos2 = pos - 1;
    if (pos2 == 0xffffffff)
        pos2 = pos + 1;

    /* Do this at this point, since it's at this point that we know the
       actual LBA position to start playing from. */
    if (!(dev->ops->track_type(dev, pos2) & CD_TRACK_AUDIO)) {
        cdrom_log("CD-ROM %i: Track Search: LBA %08X not on an audio track\n", dev->id, pos);
        dev->audio_muted_soft = 1;
        if (dev->ops->track_type(dev, pos) & CD_TRACK_AUDIO)
            dev->audio_muted_soft = 0;
    } else
        dev->audio_muted_soft = 0;

    cdrom_log("Track Search Toshiba: Muted?=%d, LBA=%08X.\n", dev->audio_muted_soft, pos);
    dev->cd_buflen = 0;
    dev->cd_status = playbit ? CD_STATUS_PLAYING : CD_STATUS_PAUSED;
    return 1;
}

uint8_t
cdrom_audio_track_search_pioneer(cdrom_t *dev, uint32_t pos, uint8_t playbit)
{
    int m = 0;
    int s = 0;
    int f = 0;

    if (dev->cd_status == CD_STATUS_DATA_ONLY)
        return 0;

    f   = bcd2bin((pos >> 24) & 0xff);
    s   = bcd2bin((pos >> 16) & 0xff);
    m   = bcd2bin((pos >> 8) & 0xff);
    if (pos == 0xffffffff) {
        pos = dev->seek_pos;
    } else
        pos = MSFtoLBA(m, s, f) - 150;

    dev->seek_pos = pos;

    dev->audio_muted_soft = 0;
    /* Do this at this point, since it's at this point that we know the
       actual LBA position to start playing from. */
    if (!(dev->ops->track_type(dev, pos) & CD_TRACK_AUDIO)) {
        cdrom_log("CD-ROM %i: LBA %08X not on an audio track\n", dev->id, pos);
        cdrom_stop(dev);
        return 0;
    }

    dev->cd_buflen = 0;
    dev->cd_status = playbit ? CD_STATUS_PLAYING : CD_STATUS_PAUSED;
    return 1;
}

uint8_t
cdrom_audio_play_pioneer(cdrom_t *dev, uint32_t pos)
{
    int m = 0;
    int s = 0;
    int f = 0;

    if (dev->cd_status == CD_STATUS_DATA_ONLY)
        return 0;

    f   = bcd2bin((pos >> 24) & 0xff);
    s   = bcd2bin((pos >> 16) & 0xff);
    m   = bcd2bin((pos >> 8) & 0xff);
    pos = MSFtoLBA(m, s, f) - 150;
    dev->cd_end = pos;

    dev->audio_muted_soft = 0;
    dev->cd_buflen = 0;
    dev->cd_status = CD_STATUS_PLAYING;
    return 1;
}

uint8_t
cdrom_audio_play_toshiba(cdrom_t *dev, uint32_t pos, int type)
{
    int m = 0;
    int s = 0;
    int f = 0;

    if (dev->cd_status == CD_STATUS_DATA_ONLY)
        return 0;

    /*Preliminary support, revert if too incomplete*/
    switch (type) {
        case 0x00:
            dev->cd_end = pos;
            break;
        case 0x40:
            m   = bcd2bin((pos >> 24) & 0xff);
            s   = bcd2bin((pos >> 16) & 0xff);
            f   = bcd2bin((pos >> 8) & 0xff);
            pos = MSFtoLBA(m, s, f) - 150;
            dev->cd_end = pos;
            break;
        case 0x80:
            dev->cd_end = (pos >> 24) & 0xff;
            break;
        case 0xc0:
            if (pos == 0xffffffff) {
                cdrom_log("CD-ROM %i: Playing from current position\n", dev->id);
                pos = dev->cd_end;
            }
            dev->cd_end = pos;
            break;
        default:
            break;
    }

    cdrom_log("Toshiba Play Audio: Muted?=%d, LBA=%08X.\n", dev->audio_muted_soft, pos);
    dev->cd_buflen = 0;
    dev->cd_status = CD_STATUS_PLAYING;
    return 1;
}

uint8_t
cdrom_audio_scan(cdrom_t *dev, uint32_t pos, int type)
{
    int m = 0;
    int s = 0;
    int f = 0;

    if (dev->cd_status == CD_STATUS_DATA_ONLY)
        return 0;

    cdrom_log("Audio Scan: MSF = %06x, type = %02x\n", pos, type);
    switch (type) {
        case 0x00:
            if (pos == 0xffffffff) {
                cdrom_log("CD-ROM %i: (type 0) Search from current position\n", dev->id);
                pos = dev->seek_pos;
            }
            dev->seek_pos = pos;
            break;
        case 0x40:
            m   = bcd2bin((pos >> 24) & 0xff);
            s   = bcd2bin((pos >> 16) & 0xff);
            f   = bcd2bin((pos >> 8) & 0xff);
            if (pos == 0xffffffff) {
                cdrom_log("CD-ROM %i: (type 1) Search from current position\n", dev->id);
                pos = dev->seek_pos;
            } else
                pos = MSFtoLBA(m, s, f) - 150;

            dev->seek_pos = pos;
            break;
        case 0x80:
            dev->seek_pos = (pos >> 24) & 0xff;
            break;
        default:
            break;
    }

    dev->audio_muted_soft = 0;
    /* Do this at this point, since it's at this point that we know the
       actual LBA position to start playing from. */
    if (!(dev->ops->track_type(dev, pos) & CD_TRACK_AUDIO)) {
        cdrom_log("CD-ROM %i: LBA %08X not on an audio track\n", dev->id, pos);
        cdrom_stop(dev);
        return 0;
    }

    dev->cd_buflen = 0;
    return 1;
}

void
cdrom_audio_pause_resume(cdrom_t *dev, uint8_t resume)
{
    if ((dev->cd_status == CD_STATUS_PLAYING) || (dev->cd_status == CD_STATUS_PAUSED))
        dev->cd_status = (dev->cd_status & 0xfe) | (resume & 0x01);
}

static void
cdrom_get_subchannel(cdrom_t *dev, uint32_t lba, subchannel_t *subc, int cooked)
{
    uint8_t  *scb   = dev->subch_buffer;
    uint8_t   q[16] = { 0 };

    if ((lba == dev->seek_pos) && (dev->cd_status == CD_STATUS_PLAYING)) {
        for (int i = 0; i < 12; i++)
            for (int j = 0; j < 8; j++)
                 q[i] |= ((scb[(i << 3) + j] >> 6) & 0x01) << (7 - j);

        if (cooked) {
            uint8_t temp = (q[0] >> 4) | ((q[0] & 0xf) << 4);
            q[0] = temp;

            for (int i = 1; i < 10; i++) {
                 temp = bcd2bin(q[i]);
                 q[i] = temp;
            }
        }

        subc->attr  = q[0];
        subc->track = q[1];
        subc->index = q[2];
        subc->rel_m = q[3];
        subc->rel_s = q[4];
        subc->rel_f = q[5];
        subc->abs_m = q[7];
        subc->abs_s = q[8];
        subc->abs_f = q[9];
    } else if ((dev->ops != NULL) && (dev->ops->get_subchannel != NULL)) {
        dev->ops->get_subchannel(dev, lba, subc);

        if (!cooked) {
            uint8_t temp = (q[0] >> 4) | ((q[0] & 0xf) << 4);
            q[0] = temp;

            subc->attr  = (subc->attr >> 4) | ((subc->attr & 0xf) << 4);
            subc->track = bin2bcd(subc->track);
            subc->index = bin2bcd(subc->index);
            subc->rel_m = bin2bcd(subc->rel_m);
            subc->rel_s = bin2bcd(subc->rel_s);
            subc->rel_f = bin2bcd(subc->rel_f);
            subc->abs_m = bin2bcd(subc->abs_m);
            subc->abs_s = bin2bcd(subc->abs_s);
            subc->abs_f = bin2bcd(subc->abs_f);
        }
    }
}

uint8_t
cdrom_get_current_status(cdrom_t *dev)
{
    uint8_t      ret;

    if (dev->cd_status == CD_STATUS_DATA_ONLY)
        ret = 0x15;
    else {
        if (dev->cd_status == CD_STATUS_PLAYING)
            ret = 0x11;
        else if (dev->cd_status == CD_STATUS_PAUSED)
            ret = 0x12;
        else
            ret = 0x13;
    }

    return ret;
}

void
cdrom_get_current_subchannel(cdrom_t *dev, uint8_t *b, int msf)
{
    subchannel_t subc;
    uint32_t     dat;

    cdrom_get_subchannel(dev, dev->seek_pos, &subc, 1);

    cdrom_log("CD-ROM %i: Returned subchannel absolute at %02i:%02i.%02i, "
              "relative at %02i:%02i.%02i, seek pos = %08x, cd_end = %08x.\n",
              dev->id, subc.abs_m, subc.abs_s, subc.abs_f, subc.rel_m, subc.rel_s, subc.rel_f,
              dev->seek_pos, dev->cd_end);

    /* Format code. */
    switch (b[0]) {
        /* Mode 0 = Q subchannel mode, first 16 bytes are indentical to mode 1 (current position),
                    the rest are stuff like ISRC etc., which can be all zeroes. */
        case 0x01:
            /* Current position. */
            b[1] = subc.attr;
            b[2] = subc.track;
            b[3] = subc.index;

            if (msf) {
                b[4] = b[8] = 0x00;

                /* NEC CDR-260 speaks BCD. */
                if ((dev->type == CDROM_TYPE_NEC_260_100) || (dev->type == CDROM_TYPE_NEC_260_101)) {
                    /* NEC */
                    b[5]  = bin2bcd(subc.abs_m);
                    b[6]  = bin2bcd(subc.abs_s);
                    b[7]  = bin2bcd(subc.abs_f);

                    b[9]  = bin2bcd(subc.rel_m);
                    b[10] = bin2bcd(subc.rel_s);
                    b[11] = bin2bcd(subc.rel_f);
                } else {
                    b[5]  = subc.abs_m;
                    b[6]  = subc.abs_s;
                    b[7]  = subc.abs_f;

                    b[9]  = subc.rel_m;
                    b[10] = subc.rel_s;
                    b[11] = subc.rel_f;
                }
            } else {
                dat   = MSFtoLBA(subc.abs_m, subc.abs_s, subc.abs_f) - 150;
                b[4]  = (dat >> 24) & 0xff;
                b[5]  = (dat >> 16) & 0xff;
                b[6]  = (dat >> 8) & 0xff;
                b[7]  = dat & 0xff;

                dat   = MSFtoLBA(subc.rel_m, subc.rel_s, subc.rel_f);
                b[8]  = (dat >> 24) & 0xff;
                b[9]  = (dat >> 16) & 0xff;
                b[10] = (dat >> 8) & 0xff;
                b[11] = dat & 0xff;
            }
            break;
        case 0x02:
            /* UPC  - TODO: Finding and reporting the actual UPC data. */
            memset(&(b[1]), 0x00, 19);
            memset(&(b[5]), 0x30, 13);
            /* NEC CDR-260 speaks BCD. */
            if ((dev->type == CDROM_TYPE_NEC_260_100) || (dev->type == CDROM_TYPE_NEC_260_101))
                /* NEC */
                b[19] = bin2bcd(subc.abs_f);
            else
                b[19] = subc.abs_f;
            break;
        case 0x03:
            /* ISRC - TODO: Finding and reporting the actual ISRC data. */
            memset(&(b[1]), 0x00, 19);
            memset(&(b[5]), 0x30, 12);
            /* NEC CDR-260 speaks BCD. */
            if ((dev->type == CDROM_TYPE_NEC_260_100) || (dev->type == CDROM_TYPE_NEC_260_101))
                /* NEC */
                b[18] = bin2bcd(subc.abs_f);
            else
                b[18] = subc.abs_f;
            break;
        default:
            cdrom_log("b[0] = %02X\n", b[0]);
            break;
    }
}

void
cdrom_get_current_subchannel_sony(cdrom_t *dev, uint8_t *b, int msf)
{
    subchannel_t subc;
    uint32_t     dat;

    cdrom_get_subchannel(dev, dev->seek_pos, &subc, 1);

    cdrom_log("CD-ROM %i: Returned subchannel at %02i:%02i.%02i, seek pos = %08x, cd_end = %08x, msf = %x.\n", dev->id, subc.abs_m, subc.abs_s, subc.abs_f, dev->seek_pos, dev->cd_end, msf);

    b[0] = subc.attr;
    b[1] = subc.track;
    b[2] = subc.index;

    if (msf) {
        b[3] = subc.rel_m;
        b[4] = subc.rel_s;
        b[5] = subc.rel_f;
        b[6] = subc.abs_m;
        b[7] = subc.abs_s;
        b[8] = subc.abs_f;
    } else {
        dat      = MSFtoLBA(subc.rel_m, subc.rel_s, subc.rel_f);
        b[3] = (dat >> 16) & 0xff;
        b[4] = (dat >> 8) & 0xff;
        b[5] = dat & 0xff;
        dat      = MSFtoLBA(subc.abs_m, subc.abs_s, subc.abs_f) - 150;
        b[6] = (dat >> 16) & 0xff;
        b[7] = (dat >> 8) & 0xff;
        b[8] = dat & 0xff;
    }
}

uint8_t
cdrom_get_audio_status_pioneer(cdrom_t *dev, uint8_t *b)
{
    uint8_t      ret;
    subchannel_t subc;

    cdrom_get_subchannel(dev, dev->seek_pos, &subc, 0);

    if (dev->cd_status == CD_STATUS_DATA_ONLY)
        ret = 0x05;
    else {
        if (dev->cd_status == CD_STATUS_PLAYING)
            ret = dev->sound_on ? 0x00 : 0x02;
        else if (dev->cd_status == CD_STATUS_PAUSED)
            ret = 0x01;
        else
            ret = 0x03;
    }

    b[0] = 0;
    b[1] = subc.abs_m;
    b[2] = subc.abs_s;
    b[3] = subc.abs_f;

    return ret;
}

uint8_t
cdrom_get_audio_status_sony(cdrom_t *dev, uint8_t *b, int msf)
{
    uint8_t      ret;
    subchannel_t subc;
    uint32_t     dat;

    cdrom_get_subchannel(dev, dev->seek_pos, &subc, 1);

    if (dev->cd_status == CD_STATUS_DATA_ONLY)
        ret = 0x05;
    else {
        if (dev->cd_status == CD_STATUS_PLAYING)
            ret = dev->sound_on ? 0x00 : 0x02;
        else if (dev->cd_status == CD_STATUS_PAUSED)
            ret = 0x01;
        else
            ret = 0x03;
    }

    if (msf) {
        b[0] = 0;
        b[1] = subc.abs_m;
        b[2] = subc.abs_s;
        b[3] = subc.abs_f;
    } else {
        dat = MSFtoLBA(subc.abs_m, subc.abs_s, subc.abs_f) - 150;
        b[0] = (dat >> 24) & 0xff;
        b[1] = (dat >> 16) & 0xff;
        b[2] = (dat >> 8) & 0xff;
        b[3] = dat & 0xff;
    }

    return ret;
}

void
cdrom_get_current_subcodeq(cdrom_t *dev, uint8_t *b)
{
    subchannel_t subc;

    cdrom_get_subchannel(dev, dev->seek_pos, &subc, 0);

    b[0] = subc.attr;
    b[1] = subc.track;
    b[2] = subc.index;
    b[3] = subc.rel_m;
    b[4] = subc.rel_s;
    b[5] = subc.rel_f;
    b[6] = subc.abs_m;
    b[7] = subc.abs_s;
    b[8] = subc.abs_f;
}

uint8_t
cdrom_get_current_subcodeq_playstatus(cdrom_t *dev, uint8_t *b)
{
    uint8_t ret;

    cdrom_get_current_subcodeq(dev, b);

    if ((dev->cd_status == CD_STATUS_DATA_ONLY) ||
        (dev->cd_status == CD_STATUS_PLAYING_COMPLETED) ||
        (dev->cd_status == CD_STATUS_STOPPED))
        ret = 0x03;
    else
        ret = (dev->cd_status == CD_STATUS_PLAYING) ? 0x00 : dev->audio_op;

    /*If a valid audio track is detected with audio on, unmute it.*/
    if (dev->ops->track_type(dev, dev->seek_pos) & CD_TRACK_AUDIO)
        dev->audio_muted_soft = 0;

    cdrom_log("SubCodeQ: Play Status: Seek LBA=%08x, CDEND=%08x, mute=%d.\n",
              dev->seek_pos, dev->cd_end, dev->audio_muted_soft);
    return ret;
}

static void
read_toc_identify_sessions(raw_track_info_t *rti, int num, unsigned char *b)
{
    /* Bytes 2 and 3 = Number of first and last sessions */
    b[2] = 0xff;
    b[3] = 0x00;

    for (int i = (num - 1); i >= 0; i--) {
         if (rti[i].session < b[2])
             b[2] = rti[i].session;
    }

    for (int i = 0; i < num; i++) {
         if (rti[i].session > b[3])
             b[3] = rti[i].session;
    }
}

static int
find_track(raw_track_info_t *trti, int num, int first)
{
    int ret = -1;

    if (first) {
        for (int i = 0; i < num; i++)
            if ((trti[i].point >= 1) && (trti[i].point <= 99)) {
                ret = i;
                break;
            }
    } else {
        for (int i = (num - 1); i >= 0; i--)
            if ((trti[i].point >= 1) && (trti[i].point <= 99)) {
                ret = i;
                break;
            }
    }

    return ret;
}

static int
find_last_lead_out(raw_track_info_t *trti, int num)
{
    int ret = -1;

    for (int i = (num - 1); i >= 0; i--)
        if (trti[i].point == 0xa2) {
            ret = i;
            break;
        }

    return ret;
}

static int
find_specific_track(raw_track_info_t *trti, int num, int track)
{
    int ret = -1;

    if ((track >= 1) && (track <= 99)) {
        for (int i = (num - 1); i >= 0; i--)
            if (trti[i].point == track) {
                ret = i;
                break;
            }
    }

    return ret;
}

static int
read_toc_normal(cdrom_t *dev, unsigned char *b, unsigned char start_track, int msf, int sony)
{
    uint8_t           rti[65536]  = { 0 };
    uint8_t           prti[65536] = { 0 };
    raw_track_info_t *trti        = (raw_track_info_t *) rti;
    raw_track_info_t *tprti       = (raw_track_info_t *) prti;
    int               num         = 0;
    int               len         = 4;
    int               s           = -1;

    cdrom_log("read_toc_normal(%016" PRIXPTR ", %016" PRIXPTR ", %02X, %i)\n",
              (uintptr_t) dev, (uintptr_t) b, start_track, msf, sony);

    dev->ops->get_raw_track_info(dev, &num, (raw_track_info_t *) rti);

    if (num > 0) {
        int j = 0;
        for (int i = 0; i < num; i++) {
            if ((trti[i].point >= 0x01) && (trti[i].point <= 0x63)) {
                tprti[j] = trti[i];
                if ((s == -1) && (tprti[j].point >= start_track))
                    s = j;
                cdrom_log("Sorted %03i = Unsorted %03i (s = %03i)\n", j, i, s);
                j++;
            }
        }

        /* Bytes 2 and 3 = Number of first and last tracks found before lead out */
        b[2] = tprti[0].point;
        b[3] = tprti[j - 1].point;

        for (int i = (num - 1); i >= 0; i--) {
            if (trti[i].point == 0xa2) {
                tprti[j] = trti[i];
                tprti[j].point = 0xaa;
                if ((s == -1) && (tprti[j].point >= start_track))
                    s = j;
                cdrom_log("Sorted %03i = Unsorted %03i (s = %03i)\n", j, i, s);
                j++;
                break;
            }
        }

        if (s != -1)  for (int i = s; i < j; i++) {
#ifdef ENABLE_CDROM_LOG
            uint8_t *c = &(b[len]);
#endif

            if (!sony)
                b[len++] = 0;                /* Reserved */
            b[len++] = tprti[i].adr_ctl; /* ADR/CTL */
            b[len++] = tprti[i].point;   /* Track number */
            if (!sony)
                b[len++] = 0;                /* Reserved */

            if (msf) {
                b[len++] = 0;

                /* NEC CDR-260 speaks BCD. */
                if ((dev->type == CDROM_TYPE_NEC_260_100) || (dev->type == CDROM_TYPE_NEC_260_101)) {
                    int m = tprti[i].pm;
                    int s = tprti[i].ps;
                    int f = tprti[i].pf;
                    msf_to_bcd(&m, &s, &f);
                    b[len++] = m;
                    b[len++] = s;
                    b[len++] = f;
                } else {
                    b[len++] = tprti[i].pm;
                    b[len++] = tprti[i].ps;
                    b[len++] = tprti[i].pf;
                }
            } else {
                uint32_t temp = MSFtoLBA(tprti[i].pm, tprti[i].ps, tprti[i].pf) - 150;

                b[len++] = temp >> 24;
                b[len++] = temp >> 16;
                b[len++] = temp >> 8;
                b[len++] = temp;
            }

#ifdef ENABLE_CDROM_LOG
            cdrom_log("Track %02X: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                      i, c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]);
#endif
        }
    } else
        b[2] = b[3] = 0;

    return len;
}

static int
read_toc_session(cdrom_t *dev, unsigned char *b, int msf)
{
    uint8_t           rti[65536] = { 0 };
    raw_track_info_t *t          = (raw_track_info_t *) rti;
    raw_track_info_t *first      = NULL;
    int               num        = 0;
    int               len        = 4;

    dev->ops->get_raw_track_info(dev, &num, (raw_track_info_t *) rti);

    /* Bytes 2 and 3 = Number of first and last sessions */
    read_toc_identify_sessions((raw_track_info_t *) rti, num, b);

    cdrom_log("read_toc_session(%016" PRIXPTR ", %016" PRIXPTR ", %i)\n",
              (uintptr_t) dev, (uintptr_t) b, msf);

    if (num != 0) {
        for (int i = 0; i < num; i++)  if ((t[i].session == b[3]) && (t[i].point >= 0x01) && (t[i].point <= 0x63)) {
             first = &(t[i]);
             break;
        }
        if (first != NULL) {
            b[len++] = 0x00;
            b[len++] = first->adr_ctl;
            b[len++] = first->point;
            b[len++] = 0x00;

            if (msf) {
                b[len++] = 0x00;

                /* NEC CDR-260 speaks BCD. */
                if ((dev->type == CDROM_TYPE_NEC_260_100) || (dev->type == CDROM_TYPE_NEC_260_101)) { /*NEC*/
                    int m = first->pm;
                    int s = first->ps;
                    int f = first->pf;

                    msf_to_bcd(&m, &s, &f);

                    b[len++] = m;
                    b[len++] = s;
                    b[len++] = f;
                } else {
                    b[len++] = first->pm;
                    b[len++] = first->ps;
                    b[len++] = first->pf;
                }
            } else {
                uint32_t temp = MSFtoLBA(first->pm, first->ps, first->pf) - 150;

                b[len++] = temp >> 24;
                b[len++] = temp >> 16;
                b[len++] = temp >> 8;
                b[len++] = temp;
            }
        }
    }

    if (len == 4)
        memset(&(b[len += 8]), 0x00, 8);

    return len;
}

static int
read_toc_raw(cdrom_t *dev, unsigned char *b, unsigned char start_track)
{
    uint8_t           rti[65536] = { 0 };
    raw_track_info_t *t          = (raw_track_info_t *) rti;
    int               num        = 0;
    int               len        = 4;

    /* Bytes 2 and 3 = Number of first and last sessions */
    read_toc_identify_sessions((raw_track_info_t *) rti, num, b);

    cdrom_log("read_toc_raw(%016" PRIXPTR ", %016" PRIXPTR ", %02X)\n",
              (uintptr_t) dev, (uintptr_t) b, start_track);

    dev->ops->get_raw_track_info(dev, &num, (raw_track_info_t *) rti);

    if (num != 0)  for (int i = 0; i < num; i++)
        if (t[i].session >= start_track) {
            memcpy(&(b[len]), &(t[i]), 11);
            len += 11;
        }

    return len;
}

int
cdrom_read_toc(cdrom_t *dev, unsigned char *b, int type, unsigned char start_track, int msf, int max_len)
{
    int len;

    switch (type) {
        case CD_TOC_NORMAL:
            len = read_toc_normal(dev, b, start_track, msf, 0);
            break;
        case CD_TOC_SESSION:
            len = read_toc_session(dev, b, msf);
            break;
        case CD_TOC_RAW:
            len = read_toc_raw(dev, b, start_track);
            break;
        default:
            cdrom_log("CD-ROM %i: Unknown TOC read type: %i\n", dev->id, type);
            return 0;
    }

    len = MIN(len, max_len);

    b[0] = (uint8_t) (((len - 2) >> 8) & 0xff);
    b[1] = (uint8_t) ((len - 2) & 0xff);

    return len;
}

int
cdrom_read_toc_sony(cdrom_t *dev, unsigned char *b, unsigned char start_track, int msf, int max_len)
{
    int len;

    len = read_toc_normal(dev, b, start_track, msf, 1);

    len = MIN(len, max_len);

    b[0] = (uint8_t) (((len - 2) >> 8) & 0xff);
    b[1] = (uint8_t) ((len - 2) & 0xff);

    return len;
}

#ifdef USE_CDROM_MITSUMI
/* New API calls for Mitsumi CD-ROM. */
void
cdrom_get_track_buffer(cdrom_t *dev, uint8_t *buf)
{
    uint8_t           rti[65536]  = { 0 };
    raw_track_info_t *trti        = (raw_track_info_t *) rti;
    int               num         = 0;
    int               first       = -1;
    int               last        = -1;

    if (dev != NULL)
        dev->ops->get_raw_track_info(dev, &num, (raw_track_info_t *) rti);

    if (num > 0) {
        first = find_track(trti, num, 1);
        last = find_track(trti, num, 0);
    }

    if (first != -1) {
        buf[0] = trti[first].point;
        buf[2] = trti[first].pm;
        buf[3] = trti[first].ps;
        buf[4] = trti[first].pf;
    } else {
        buf[0] = 0x01;
        buf[2] = 0x00;
        buf[3] = 0x02;
        buf[4] = 0x00;
    }

    if (last != -1) {
        buf[1] = trti[last].point;
        buf[5] = trti[first].pm;
        buf[6] = trti[first].ps;
        buf[7] = trti[first].pf;
    } else {
        buf[1] = 0x01;
        buf[5] = 0x00;
        buf[6] = 0x02;
        buf[7] = 0x00;
    }

    buf[8] = 0x00;
}

/* TODO: Actually implement this properly. */
void
cdrom_get_q(cdrom_t *dev, uint8_t *buf, int *curtoctrk, uint8_t mode)
{
    memset(buf, 0x00, 10);
}

uint8_t
cdrom_mitsumi_audio_play(cdrom_t *dev, uint32_t pos, uint32_t len)
{
    track_info_t ti;

    if (dev->cd_status == CD_STATUS_DATA_ONLY)
        return 0;

    cdrom_log("CD-ROM 0: Play Mitsumi audio - %08X %08X\n", pos, len);
    dev->ops->get_track_info(dev, pos, 0, &ti);
    pos = MSFtoLBA(ti.m, ti.s, ti.f) - 150;
    dev->ops->get_track_info(dev, len, 1, &ti);
    len = MSFtoLBA(ti.m, ti.s, ti.f) - 150;

    /* Do this at this point, since it's at this point that we know the
       actual LBA position to start playing from. */
    if (!(dev->ops->track_type(dev, pos) & CD_TRACK_AUDIO)) {
        cdrom_log("CD-ROM %i: LBA %08X not on an audio track\n", dev->id, pos);
        cdrom_stop(dev);
        return 0;
    }

    dev->seek_pos  = pos;
    dev->cd_end    = len;
    dev->cd_status = CD_STATUS_PLAYING;
    dev->cd_buflen = 0;

    return 1;
}
#endif

uint8_t
cdrom_read_disc_info_toc(cdrom_t *dev, unsigned char *b, unsigned char track, int type)
{
    uint8_t           rti[65536]  = { 0 };
    raw_track_info_t *trti        = (raw_track_info_t *) rti;
    int               num         = 0;
    int               first       = -1;
    int               last        = -1;
    int               t           = -1;
    uint32_t          temp;
    uint8_t           ret         = 1;

    if (dev != NULL)
        dev->ops->get_raw_track_info(dev, &num, (raw_track_info_t *) rti);

    cdrom_log("Read DISC Info TOC Type = %d, track = %d\n", type, track);

    switch (type) {
        case 0:
            if (num > 0) {
                first = find_track(trti, num, 1);
                last = find_track(trti, num, 0);
            }

            if ((first == -1) || (last == -1))
                ret = 0;
            else {
                b[0] = bin2bcd(first);
                b[1] = bin2bcd(last);
                b[2] = 0x00;
                b[3] = 0x00;

                cdrom_log("CD-ROM %i: Returned Toshiba/NEC disc information (type 0) at %02i:%02i\n",
                          dev->id, b[0], b[1]);
            }
            break;
        case 1:
            if (num > 0)
                t = find_last_lead_out(trti, num);

            if (t == -1)
                ret = 0;
            else {
                b[0] = bin2bcd(trti[t].pm);
                b[1] = bin2bcd(trti[t].ps);
                b[2] = bin2bcd(trti[t].pf);
                b[3] = 0x00;

                cdrom_log("CD-ROM %i: Returned Toshiba/NEC disc information (type 1) at %02i:%02i.%02i\n",
                          dev->id, b[0], b[1], b[2]);
            }
            break;
        case 2:
            if (num > 0)
                t = find_specific_track(trti, num, bcd2bin(track));

            if (t == -1)
                ret = 0;
            else {
                b[0] = bin2bcd(trti[t].pm);
                b[1] = bin2bcd(trti[t].ps);
                b[2] = bin2bcd(trti[t].pf);
                b[3] = trti[t].adr_ctl;
 
                cdrom_log("CD-ROM %i: Returned Toshiba/NEC disc information (type 2) at "
                          "%02i:%02i.%02i, track=%d, attr=%02x.\n", dev->id, b[0], b[1], b[2], bcd2bin(track), b[3]);
            }
            break;
        case 3: /* Undocumented on NEC CD-ROM's, from information based on sr_vendor.c from the Linux kernel */
            switch (dev->type) {
                case CDROM_TYPE_NEC_25_10a:
                case CDROM_TYPE_NEC_38_103:
                case CDROM_TYPE_NEC_75_103:
                case CDROM_TYPE_NEC_77_106:
                case CDROM_TYPE_NEC_211_100:
                case CDROM_TYPE_NEC_464_105:
                    b[0x0e] = 0x00;

                    if (num > 0)
                        first = find_track(trti, num, 1);

                    if (first == -1)
                        ret = 0;
                    else {
                        temp    = MSFtoLBA(trti[first].pm, trti[first].ps, trti[first].pf) - 150;
                        b[0x0f] = temp >> 24;
                        b[0x10] = temp >> 16;
                        b[0x11] = temp >> 8;
                        b[0x12] = temp;
                    }
                    break;

                default:
                    b[0] = 0x00;    /* Audio or CDROM disc. */

                    if (num > 0)
                        first = find_track(trti, num, 1);

                    if (first == -1)
                        ret = 0;
                    else {
                        temp    = MSFtoLBA(trti[first].pm, trti[first].ps, trti[first].pf) - 150;
                        b[0x1] = temp >> 24;
                        b[0x2] = temp >> 16;
                        b[0x3] = temp >> 8;
                    }
                    break;
            }
            break;
        default:
            break;
    }

    return ret;
}

static int
track_type_is_valid(UNUSED(uint8_t id), int type, int flags, int audio, int mode2)
{
    if (!(flags & 0x70) && (flags & 0xf8)) { /* 0x08/0x80/0x88 are illegal modes */
        cdrom_log("CD-ROM %i: [Any Mode] 0x08/0x80/0x88 are illegal modes\n", id);
        return 0;
    }

    if ((type != 1) && !audio) {
        if ((flags & 0x06) == 0x06) {
            cdrom_log("CD-ROM %i: [Any Data Mode] Invalid error flags\n", id);
            return 0;
        }

        if (((flags & 0x700) == 0x300) || ((flags & 0x700) > 0x400)) {
            cdrom_log("CD-ROM %i: [Any Data Mode] Invalid subchannel data flags (%02X)\n", id, flags & 0x700);
            return 0;
        }

        if ((flags & 0x18) == 0x08) { /* EDC/ECC without user data is an illegal mode */
            cdrom_log("CD-ROM %i: [Any Data Mode] EDC/ECC without user data is an illegal mode\n", id);
            return 0;
        }

        if (((flags & 0xf0) == 0x90) || ((flags & 0xf0) == 0xc0)) { /* 0x90/0x98/0xC0/0xC8 are illegal modes */
            cdrom_log("CD-ROM %i: [Any Data Mode] 0x90/0x98/0xC0/0xC8 are illegal modes\n", id);
            return 0;
        }

        if (((type > 3) && (type != 8)) || (mode2 && (mode2 & 0x03))) {
            if ((flags & 0xf0) == 0x30) { /* 0x30/0x38 are illegal modes */
                cdrom_log("CD-ROM %i: [Any XA Mode 2] 0x30/0x38 are illegal modes\n", id);
                return 0;
            }
            if (((flags & 0xf0) == 0xb0) || ((flags & 0xf0) == 0xd0)) { /* 0xBx and 0xDx are illegal modes */
                cdrom_log("CD-ROM %i: [Any XA Mode 2] 0xBx and 0xDx are illegal modes\n", id);
                return 0;
            }
        }
    }

    return 1;
}

static int
read_audio(cdrom_t *dev, uint32_t lba, uint8_t *b)
{
    int ret = dev->ops->read_sector(dev, raw_buffer, lba);

    memcpy(b, raw_buffer, 2352);

    cdrom_sector_size = 2352;

    return ret;
}

static void
process_mode1(cdrom_t *dev, int cdrom_sector_flags, uint8_t *b)
{
    cdrom_sector_size = 0;

    if (cdrom_sector_flags & 0x80) {
        /* Sync */
        cdrom_log("CD-ROM %i: [Mode 1] Sync\n", dev->id);
        memcpy(b, raw_buffer, 12);
        cdrom_sector_size += 12;
        b += 12;
    }

    if (cdrom_sector_flags & 0x20) {
        /* Header */
        cdrom_log("CD-ROM %i: [Mode 1] Header\n", dev->id);
        memcpy(b, raw_buffer + 12, 4);
        cdrom_sector_size += 4;
        b += 4;
    }

    if (cdrom_sector_flags & 0x40) {
        /* Sub-header */
        if (!(cdrom_sector_flags & 0x10)) {
            /* No user data */
            cdrom_log("CD-ROM %i: [Mode 1] Sub-header\n", dev->id);
            memcpy(b, raw_buffer + 16, 8);
            cdrom_sector_size += 8;
            b += 8;
        }
    }

    if (cdrom_sector_flags & 0x10) {
        /* User data */
        cdrom_log("CD-ROM %i: [Mode 1] User data\n", dev->id);
        memcpy(b, raw_buffer + 16, 2048);
        cdrom_sector_size += 2048;
        b += 2048;
    }

    if (cdrom_sector_flags & 0x08) {
        /* EDC/ECC */
        cdrom_log("CD-ROM %i: [Mode 1] EDC/ECC\n", dev->id);
        memcpy(b, raw_buffer + 2064, 288);
        cdrom_sector_size += 288;
        b += 288;
    }
}

static int
read_data(cdrom_t *dev, uint32_t lba)
{
    return dev->ops->read_sector(dev, raw_buffer, lba);
}

static int
read_mode1(cdrom_t *dev, int cdrom_sector_flags, uint32_t lba, uint8_t *b)
{
    int ret = read_data(dev, lba);

    process_mode1(dev, cdrom_sector_flags, b);

    return ret;
}

static int
read_mode2_non_xa(cdrom_t *dev, int cdrom_sector_flags, uint32_t lba, uint8_t *b)
{
    int ret = dev->ops->read_sector(dev, raw_buffer, lba);

    cdrom_sector_size = 0;

    if (cdrom_sector_flags & 0x80) {
        /* Sync */
        cdrom_log("CD-ROM %i: [Mode 2 Formless] Sync\n", dev->id);
        memcpy(b, raw_buffer, 12);
        cdrom_sector_size += 12;
        b += 12;
    }

    if (cdrom_sector_flags & 0x20) {
        /* Header */
        cdrom_log("CD-ROM %i: [Mode 2 Formless] Header\n", dev->id);
        memcpy(b, raw_buffer + 12, 4);
        cdrom_sector_size += 4;
        b += 4;
    }

    /* Mode 1 sector, expected type is 1 type. */
    if (cdrom_sector_flags & 0x40) {
        /* Sub-header */
        cdrom_log("CD-ROM %i: [Mode 2 Formless] Sub-header\n", dev->id);
        memcpy(b, raw_buffer + 16, 8);
        cdrom_sector_size += 8;
        b += 8;
    }

    if (cdrom_sector_flags & 0x10) {
        /* User data */
        cdrom_log("CD-ROM %i: [Mode 2 Formless] User data\n", dev->id);
        memcpy(b, raw_buffer + 24, 2336);
        cdrom_sector_size += 2336;
        b += 2336;
    }

    return ret;
}

static void
process_mode2_xa_form1(cdrom_t *dev, int cdrom_sector_flags, uint8_t *b)
{
    cdrom_sector_size = 0;

    if (cdrom_sector_flags & 0x80) {
        /* Sync */
        cdrom_log("CD-ROM %i: [XA Mode 2 Form 1] Sync\n", dev->id);
        memcpy(b, raw_buffer, 12);
        cdrom_sector_size += 12;
        b += 12;
    }

    if (cdrom_sector_flags & 0x20) {
        /* Header */
        cdrom_log("CD-ROM %i: [XA Mode 2 Form 1] Header\n", dev->id);
        memcpy(b, raw_buffer + 12, 4);
        cdrom_sector_size += 4;
        b += 4;
    }

    if (cdrom_sector_flags & 0x40) {
        /* Sub-header */
        cdrom_log("CD-ROM %i: [XA Mode 2 Form 1] Sub-header\n", dev->id);
        memcpy(b, raw_buffer + 16, 8);
        cdrom_sector_size += 8;
        b += 8;
    }

    if (cdrom_sector_flags & 0x10) {
        /* User data */
        cdrom_log("CD-ROM %i: [XA Mode 2 Form 1] User data\n", dev->id);
        memcpy(b, raw_buffer + 24, 2048);
        cdrom_sector_size += 2048;
        b += 2048;
    }

    if (cdrom_sector_flags & 0x08) {
        /* EDC/ECC */
        cdrom_log("CD-ROM %i: [XA Mode 2 Form 1] EDC/ECC\n", dev->id);
        memcpy(b, raw_buffer + 2072, 280);
        cdrom_sector_size += 280;
        b += 280;
    }
}

static int
read_mode2_xa_form1(cdrom_t *dev, int cdrom_sector_flags, uint32_t lba, uint8_t *b)
{
    int ret = read_data(dev, lba);

    process_mode2_xa_form1(dev, cdrom_sector_flags, b);

    return ret;
}

static int
read_mode2_xa_form2(cdrom_t *dev, int cdrom_sector_flags, uint32_t lba, uint8_t *b)
{
    int ret = dev->ops->read_sector(dev, raw_buffer, lba);

    cdrom_sector_size = 0;

    if (cdrom_sector_flags & 0x80) {
        /* Sync */
        cdrom_log("CD-ROM %i: [XA Mode 2 Form 2] Sync\n", dev->id);
        memcpy(b, raw_buffer, 12);
        cdrom_sector_size += 12;
        b += 12;
    }

    if (cdrom_sector_flags & 0x20) {
        /* Header */
        cdrom_log("CD-ROM %i: [XA Mode 2 Form 2] Header\n", dev->id);
        memcpy(b, raw_buffer + 12, 4);
        cdrom_sector_size += 4;
        b += 4;
    }

    if (cdrom_sector_flags & 0x40) {
        /* Sub-header */
        cdrom_log("CD-ROM %i: [XA Mode 2 Form 2] Sub-header\n", dev->id);
        memcpy(b, raw_buffer + 16, 8);
        cdrom_sector_size += 8;
        b += 8;
    }

    if (cdrom_sector_flags & 0x10) {
        /* User data */
        cdrom_log("CD-ROM %i: [XA Mode 2 Form 2] User data\n", dev->id);
        memcpy(b, raw_buffer + 24, 2328);
        cdrom_sector_size += 2328;
        b += 2328;
    }

    return ret;
}

int
cdrom_readsector_raw(cdrom_t *dev, uint8_t *buffer, int sector, int ismsf, int cdrom_sector_type,
                     int cdrom_sector_flags, int *len, uint8_t vendor_type)
{
    uint8_t *b;
    uint8_t *temp_b;
    uint32_t lba;
    int      audio  = 0;
    int      mode2  = 0;
    int      unk    = 0;
    int      ret    = 0;

    if (dev->cd_status == CD_STATUS_EMPTY)
        return 0;

    b = temp_b = buffer;

    *len = 0;

    if (ismsf) {
        int m = (sector >> 16) & 0xff;
        int s = (sector >> 8) & 0xff;
        int f = sector & 0xff;

        lba = MSFtoLBA(m, s, f) - 150;
    } else {
        switch (vendor_type) {
            case 0x00:
                lba = sector;
                break;
            case 0x40: {
                int m = bcd2bin((sector >> 24) & 0xff);
                int s = bcd2bin((sector >> 16) & 0xff);
                int f = bcd2bin((sector >> 8) & 0xff);

                lba = MSFtoLBA(m, s, f) - 150;
                break;
            } case 0x80:
                lba = bcd2bin((sector >> 24) & 0xff);
                break;
            /* Never used values but the compiler complains. */
            default:
                lba = 0;
        }
    }

    if (dev->ops->track_type)
        audio = dev->ops->track_type(dev, lba);

    mode2  = audio & CD_TRACK_MODE2;
    unk    = audio & CD_TRACK_UNK_DATA;
    audio &= CD_TRACK_AUDIO;

    memset(raw_buffer, 0, 2448);
    memset(extra_buffer, 0, 296);

    if ((cdrom_sector_flags & 0xf8) == 0x08) {
        /* 0x08 is an illegal mode */
        cdrom_log("CD-ROM %i: [Mode 1] 0x08 is an illegal mode\n", dev->id);
        return 0;
    }

    if (!track_type_is_valid(dev->id, cdrom_sector_type, cdrom_sector_flags, audio, mode2))
        return 0;

    if ((cdrom_sector_type > 5) && (cdrom_sector_type != 8)) {
        cdrom_log("CD-ROM %i: Attempting to read an unrecognized sector type from an image\n", dev->id);
        return 0;
    } else if (cdrom_sector_type == 1) {
        if (!audio || (dev->cd_status == CD_STATUS_DATA_ONLY)) {
            cdrom_log("CD-ROM %i: [Audio] Attempting to read an audio sector from a data image\n", dev->id);
            return 0;
        }

        ret = read_audio(dev, lba, temp_b);
    } else if (cdrom_sector_type == 2) {
        if (audio || mode2) {
            cdrom_log("CD-ROM %i: [Mode 1] Attempting to read a sector of another type\n", dev->id);
            return 0;
        }

        ret = read_mode1(dev, cdrom_sector_flags, lba, temp_b);
    } else if (cdrom_sector_type == 3) {
        if (audio || !mode2 || (mode2 & 0x03)) {
            cdrom_log("CD-ROM %i: [Mode 2 Formless] Attempting to read a sector of another type\n", dev->id);
            return 0;
        }

        ret = read_mode2_non_xa(dev, cdrom_sector_flags, lba, temp_b);
    } else if (cdrom_sector_type == 4) {
        if (audio || !mode2 || ((mode2 & 0x03) != 1)) {
            cdrom_log("CD-ROM %i: [XA Mode 2 Form 1] Attempting to read a sector of another type\n", dev->id);
            return 0;
        }

        read_mode2_xa_form1(dev, cdrom_sector_flags, lba, temp_b);
    } else if (cdrom_sector_type == 5) {
        if (audio || !mode2 || ((mode2 & 0x03) != 2)) {
            cdrom_log("CD-ROM %i: [XA Mode 2 Form 2] Attempting to read a sector of another type\n", dev->id);
            return 0;
        }

        ret = read_mode2_xa_form2(dev, cdrom_sector_flags, lba, temp_b);
    } else if (cdrom_sector_type == 8) {
        if (audio) {
            cdrom_log("CD-ROM %i: [Any Data] Attempting to read a data sector from an audio track\n", dev->id);
            return 0;
        }

        if (unk) {
            /* This is needed to correctly read Mode 2 XA Form 1 sectors over IOCTL. */
            ret = read_data(dev, lba);

            if (raw_buffer[0x000f] == 0x02) {
                cdrom_log("CD-ROM %i: [Any Data] Unknown data type determined to be XA Mode 2 Form 1\n", dev->id);
                process_mode2_xa_form1(dev, cdrom_sector_flags, temp_b);
            } else {
                cdrom_log("CD-ROM %i: [Any Data] Unknown data type determined to be Mode 1\n", dev->id);
                process_mode1(dev, cdrom_sector_flags, temp_b);
            }
        } else if (mode2 && ((mode2 & 0x03) == 1))
            ret = read_mode2_xa_form1(dev, cdrom_sector_flags, lba, temp_b);
        else if (!mode2)
            ret = read_mode1(dev, cdrom_sector_flags, lba, temp_b);
        else {
            cdrom_log("CD-ROM %i: [Any Data] Attempting to read a data sector whose cooked size "
                      "is not 2048 bytes\n", dev->id);
            return 0;
        }
    } else {
        if (mode2) {
            if ((mode2 & 0x03) == 0x01)
                ret = read_mode2_xa_form1(dev, cdrom_sector_flags, lba, temp_b);
            else if ((mode2 & 0x03) == 0x02)
                ret = read_mode2_xa_form2(dev, cdrom_sector_flags, lba, temp_b);
            else
                ret = read_mode2_non_xa(dev, cdrom_sector_flags, lba, temp_b);
        } else {
            if (audio)
                ret = read_audio(dev, lba, temp_b);
            else
                ret = read_mode1(dev, cdrom_sector_flags, lba, temp_b);
        }
    }

    if ((cdrom_sector_flags & 0x06) == 0x02) {
        /* Add error flags. */
        cdrom_log("CD-ROM %i: Error flags\n", dev->id);
        memcpy(b + cdrom_sector_size, extra_buffer, 294);
        cdrom_sector_size += 294;
    } else if ((cdrom_sector_flags & 0x06) == 0x04) {
        /* Add error flags. */
        cdrom_log("CD-ROM %i: Full error flags\n", dev->id);
        memcpy(b + cdrom_sector_size, extra_buffer, 296);
        cdrom_sector_size += 296;
    }

    if ((cdrom_sector_flags & 0x700) == 0x100) {
        cdrom_log("CD-ROM %i: Raw subchannel data\n", dev->id);
        memcpy(b + cdrom_sector_size, raw_buffer + 2352, 96);
        cdrom_sector_size += 96;
    } else if ((cdrom_sector_flags & 0x700) == 0x200) {
        cdrom_log("CD-ROM %i: Q subchannel data\n", dev->id);
        memcpy(b + cdrom_sector_size, raw_buffer + 2352, 16);
        cdrom_sector_size += 16;
    } else if ((cdrom_sector_flags & 0x700) == 0x400) {
        cdrom_log("CD-ROM %i: R/W subchannel data\n", dev->id);
        memcpy(b + cdrom_sector_size, raw_buffer + 2352, 96);
        cdrom_sector_size += 96;
    }

    *len = cdrom_sector_size;

    return ret;
}

/* Peform a master init on the entire module. */
void
cdrom_global_init(void)
{
    /* Clear the global data. */
    memset(cdrom, 0x00, sizeof(cdrom));
}

static void
cdrom_drive_reset(cdrom_t *dev)
{
    dev->priv        = NULL;
    dev->insert      = NULL;
    dev->close       = NULL;
    dev->get_volume  = NULL;
    dev->get_channel = NULL;
}

#ifdef ENABLE_CDROM_LOG
static void
cdrom_toc_dump(cdrom_t *dev)
{
    uint8_t     b[65536] = { 0 };
    int         len      = cdrom_read_toc(dev, b, CD_TOC_RAW, 0, 0, 65536);
    const char *fn2      = "d:\\86boxnew\\toc_cue.dmp";
    FILE *      f        = fopen(fn2, "wb");
    fwrite(b, 1, len, f);
    fflush(f);
    fclose(f);
    pclog("Written TOC of %i bytes to %s\n", len, fn2);

    memset(b, 0x00, 65536);
    len      = cdrom_read_toc(dev, b, CD_TOC_NORMAL, 0, 0, 65536);
    fn2      = "d:\\86boxnew\\toc_cue_cooked.dmp";
    f        = fopen(fn2, "wb");
    fwrite(b, 1, len, f);
    fflush(f);
    fclose(f);
    pclog("Written cooked TOC of %i bytes to %s\n", len, fn2);

    memset(b, 0x00, 65536);
    len      = cdrom_read_toc(dev, b, CD_TOC_SESSION, 0, 0, 65536);
    fn2      = "d:\\86boxnew\\toc_cue_session.dmp";
    f        = fopen(fn2, "wb");
    fwrite(b, 1, len, f);
    fflush(f);
    fclose(f);
    pclog("Written session TOC of %i bytes to %s\n", len, fn2);
}
#endif

void
cdrom_hard_reset(void)
{
    cdrom_t *dev;

    for (uint8_t i = 0; i < CDROM_NUM; i++) {
        dev = &cdrom[i];
        if (dev->bus_type) {
            cdrom_log("CD-ROM %i: Hard reset\n", i);

            dev->id = i;

            cdrom_drive_reset(dev);

            switch (dev->bus_type) {
                case CDROM_BUS_ATAPI:
                case CDROM_BUS_SCSI:
                    scsi_cdrom_drive_reset(i);
                    break;

                default:
                    break;
            }

            dev->cd_status = CD_STATUS_EMPTY;

            if (strlen(dev->image_path) > 0) {
#ifdef _WIN32
                if ((strlen(dev->image_path) >= 1) && (dev->image_path[strlen(dev->image_path) - 1] == '/'))
                    dev->image_path[strlen(dev->image_path) - 1] = '\\';
#else
                if ((strlen(dev->image_path) >= 1) &&
                    (dev->image_path[strlen(dev->image_path) - 1] == '\\'))
                    dev->image_path[strlen(dev->image_path) - 1] = '/';
#endif

                if ((strlen(dev->image_path) != 0) && (strstr(dev->image_path, "ioctl://") == dev->image_path))
                    cdrom_ioctl_open(dev, dev->image_path);
                else
                    cdrom_image_open(dev, dev->image_path);

                cdrom_insert(i);
                cdrom_insert(i);

#ifdef ENABLE_CDROM_LOG
                if (i == 0)
                    cdrom_toc_dump(dev);
#endif
            }
        }
    }

    sound_cd_thread_reset();
}

void
cdrom_close(void)
{
    cdrom_t *dev;

    for (uint8_t i = 0; i < CDROM_NUM; i++) {
        dev = &cdrom[i];

        if (dev->bus_type == CDROM_BUS_SCSI)
            memset(&scsi_devices[dev->scsi_device_id], 0x00, sizeof(scsi_device_t));

        if (dev->close)
            dev->close(dev->priv);

        if (dev->ops && dev->ops->exit)
            dev->ops->exit(dev);

        dev->ops  = NULL;
        dev->priv = NULL;

        cdrom_drive_reset(dev);
    }
}

/* Signal disc change to the emulated machine. */
void
cdrom_insert(uint8_t id)
{
    cdrom_t *dev = &cdrom[id];

    if (dev->bus_type && dev->insert)
        dev->insert(dev->priv);
}

void
cdrom_exit(uint8_t id)
{
    cdrom_t *dev = &cdrom[id];

    strcpy(dev->prev_image_path, dev->image_path);

    if (dev->ops) {
        if (dev->ops->exit)
            dev->ops->exit(dev);

        dev->ops = NULL;
    }

    memset(dev->image_path, 0, sizeof(dev->image_path));

    cdrom_log("cdrom_exit(%i): cdrom_insert(%i)\n", id, id);
    cdrom_insert(id);
}

int
cdrom_is_empty(uint8_t id)
{
    cdrom_t *dev = &cdrom[id];
    int      ret = 0;

    /* This entire block should be in cdrom.c/cdrom_eject(dev*) ... */
    if (strlen(dev->image_path) == 0)
        /* Switch from empty to empty. Do nothing. */
        ret = 1;

    return ret;
}

/* The mechanics of ejecting a CD-ROM from a drive. */
void
cdrom_eject(uint8_t id)
{
    cdrom_t *dev = &cdrom[id];

    /* This entire block should be in cdrom.c/cdrom_eject(dev*) ... */
    if (strlen(dev->image_path) == 0)
        /* Switch from empty to empty. Do nothing. */
        return;

    cdrom_exit(id);

    plat_cdrom_ui_update(id, 0);

    config_save();
}

/* The mechanics of re-loading a CD-ROM drive. */
void
cdrom_reload(uint8_t id)
{
    cdrom_t *dev       = &cdrom[id];
    int      was_empty = cdrom_is_empty(id);

    if ((strcmp(dev->image_path, dev->prev_image_path) == 0) || (strlen(dev->prev_image_path) == 0) || (strlen(dev->image_path) > 0)) {
        /* Switch from empty to empty. Do nothing. */
        return;
    }

    if (dev->ops && dev->ops->exit)
        dev->ops->exit(dev);
    dev->ops = NULL;
    memset(dev->image_path, 0, sizeof(dev->image_path));

    if (strlen(dev->image_path) > 0) {
        /* Reload a previous image. */
        if (strlen(dev->prev_image_path) > 0)
            strcpy(dev->image_path, dev->prev_image_path);

#ifdef _WIN32
        if ((strlen(dev->prev_image_path) > 0) && (strlen(dev->image_path) >= 1) &&
            (dev->image_path[strlen(dev->image_path) - 1] == '/'))
            dev->image_path[strlen(dev->image_path) - 1] = '\\';
#else
        if ((strlen(dev->prev_image_path) > 0) && (strlen(dev->image_path) >= 1) &&
            (dev->image_path[strlen(dev->image_path) - 1] == '\\'))
            dev->image_path[strlen(dev->image_path) - 1] = '/';
#endif

        if ((strlen(dev->image_path) != 0) && (strstr(dev->image_path, "ioctl://") == dev->image_path))
            cdrom_ioctl_open(dev, dev->image_path);
        else
            cdrom_image_open(dev, dev->image_path);

#ifdef ENABLE_CDROM_LOG
        cdrom_toc_dump(dev);
#endif

        /* Signal media change to the emulated machine. */
        cdrom_log("cdrom_reload(%i): cdrom_insert(%i)\n", id, id);
        cdrom_insert(id);

        /* The drive was previously empty, transition directly to UNIT ATTENTION. */
        if (was_empty)
            cdrom_insert(id);
    }

    plat_cdrom_ui_update(id, 1);

    config_save();
}
