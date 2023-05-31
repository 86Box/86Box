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
    if (!controllers[cdrom_interface_current].device)
        return;

    device_add(controllers[cdrom_interface_current].device);
}

char *
cdrom_interface_get_internal_name(int cdinterface)
{
    return device_get_internal_name(controllers[cdinterface].device);
}

int
cdrom_interface_get_from_internal_name(char *s)
{
    int c = 0;

    while (controllers[c].device != NULL) {
        if (!strcmp((char *) controllers[c].device->internal_name, s))
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

    if (!dev->sound_on || (dev->cd_status != CD_STATUS_PLAYING)) {
        cdrom_log("CD-ROM %i: Audio callback while not playing\n", dev->id);
        if (dev->cd_status == CD_STATUS_PLAYING)
            dev->seek_pos += (len >> 11);
        memset(output, 0, len * 2);
        return 0;
    }

    while (dev->cd_buflen < len) {
        if (dev->seek_pos < dev->cd_end) {
            if (dev->ops->read_sector(dev, CD_READ_AUDIO, (uint8_t *) &(dev->cd_buffer[dev->cd_buflen]),
                                      dev->seek_pos)) {
                cdrom_log("CD-ROM %i: Read LBA %08X successful\n", dev->id, dev->seek_pos);
                dev->seek_pos++;
                dev->cd_buflen += (RAW_SECTOR_SIZE / 2);
                ret = 1;
            } else {
                cdrom_log("CD-ROM %i: Read LBA %08X failed\n", dev->id, dev->seek_pos);
                memset(&(dev->cd_buffer[dev->cd_buflen]), 0x00, (BUF_SIZE - dev->cd_buflen) * 2);
                dev->cd_status = CD_STATUS_STOPPED;
                dev->cd_buflen = len;
                ret            = 0;
            }
        } else {
            cdrom_log("CD-ROM %i: Playing completed\n", dev->id);
            memset(&dev->cd_buffer[dev->cd_buflen], 0x00, (BUF_SIZE - dev->cd_buflen) * 2);
            dev->cd_status = CD_STATUS_PLAYING_COMPLETED;
            dev->cd_buflen = len;
            ret            = 0;
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
        if (!strcmp(cdrom_drive_types[dev->type].internal_name, "NEC_CD-ROM_DRIVE260_1.01") || (!strcmp(cdrom_drive_types[dev->type].internal_name, "NEC_CD-ROM_DRIVE260_1.00"))) /*NEC*/
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
        if (!strcmp(cdrom_drive_types[dev->type].internal_name, "NEC_CD-ROM_DRIVE260_1.01") || (!strcmp(cdrom_drive_types[dev->type].internal_name, "NEC_CD-ROM_DRIVE260_1.00"))) /*NEC*/
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
    }

    /* Unlike standard commands, if there's a data track on an Audio CD (mixed mode)
       the playback continues with the audio muted (Toshiba CD-ROM SCSI-2 manual reference). */
    dev->cd_buflen = 0;
    dev->cd_status = playbit ? CD_STATUS_PLAYING : CD_STATUS_PAUSED;
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
    }

    cdrom_log("Toshiba/NEC Play Audio: MSF = %06x, type = %02x, cdstatus = %02x\n", pos, type, dev->cd_status);

    /* Unlike standard commands, if there's a data track on an Audio CD (mixed mode)
       the playback continues with the audio muted (Toshiba CD-ROM SCSI-2 manual reference). */

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
    }

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

uint8_t
cdrom_get_current_subchannel(cdrom_t *dev, uint8_t *b, int msf)
{
    uint8_t      ret;
    subchannel_t subc;
    int          pos = 1;
    int          m;
    int          s;
    int          f;
    uint32_t     dat;

    dev->ops->get_subchannel(dev, dev->seek_pos, &subc);

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

    cdrom_log("CD-ROM %i: Returned subchannel at %02i:%02i.%02i, ret = %02x, seek pos = %08x, cd_end = %08x.\n", dev->id, subc.abs_m, subc.abs_s, subc.abs_f, ret, dev->seek_pos, dev->cd_end);

    if (b[pos] > 1) {
        cdrom_log("B[%i] = %02x, ret = %02x.\n", pos, b[pos], ret);
        return ret;
    }

    b[pos++] = subc.attr;
    b[pos++] = subc.track;
    b[pos++] = subc.index;

    if (msf) {
        b[pos] = 0;

        /* NEC CDR-260 speaks BCD. */
        if (!strcmp(cdrom_drive_types[dev->type].internal_name, "NEC_CD-ROM_DRIVE260_1.01") || (!strcmp(cdrom_drive_types[dev->type].internal_name, "NEC_CD-ROM_DRIVE260_1.00"))) /*NEC*/ {
            m = subc.abs_m;
            s = subc.abs_s;
            f = subc.abs_f;
            msf_to_bcd(&m, &s, &f);
            b[pos + 1] = m;
            b[pos + 2] = s;
            b[pos + 3] = f;
        } else {
            b[pos + 1] = subc.abs_m;
            b[pos + 2] = subc.abs_s;
            b[pos + 3] = subc.abs_f;
        }

        pos += 4;

        b[pos] = 0;

        /* NEC CDR-260 speaks BCD. */
        if (!strcmp(cdrom_drive_types[dev->type].internal_name, "NEC_CD-ROM_DRIVE260_1.01") || (!strcmp(cdrom_drive_types[dev->type].internal_name, "NEC_CD-ROM_DRIVE260_1.00"))) /*NEC*/ {
            m = subc.rel_m;
            s = subc.rel_s;
            f = subc.rel_f;
            msf_to_bcd(&m, &s, &f);
            b[pos + 1] = m;
            b[pos + 2] = s;
            b[pos + 3] = f;
        } else {
            b[pos + 1] = subc.rel_m;
            b[pos + 2] = subc.rel_s;
            b[pos + 3] = subc.rel_f;
        }

        pos += 4;
    } else {
        dat      = MSFtoLBA(subc.abs_m, subc.abs_s, subc.abs_f) - 150;
        b[pos++] = (dat >> 24) & 0xff;
        b[pos++] = (dat >> 16) & 0xff;
        b[pos++] = (dat >> 8) & 0xff;
        b[pos++] = dat & 0xff;
        dat      = MSFtoLBA(subc.rel_m, subc.rel_s, subc.rel_f);
        b[pos++] = (dat >> 24) & 0xff;
        b[pos++] = (dat >> 16) & 0xff;
        b[pos++] = (dat >> 8) & 0xff;
        b[pos++] = dat & 0xff;
    }

    return ret;
}

void
cdrom_get_current_subchannel_sony(cdrom_t *dev, uint8_t *b, int msf)
{
    subchannel_t subc;
    int          pos = 0;
    uint32_t     dat;

    dev->ops->get_subchannel(dev, dev->seek_pos, &subc);

    cdrom_log("CD-ROM %i: Returned subchannel at %02i:%02i.%02i, seek pos = %08x, cd_end = %08x.\n", dev->id, subc.abs_m, subc.abs_s, subc.abs_f, dev->seek_pos, dev->cd_end);

    b[pos++] = subc.attr;
    b[pos++] = subc.track;
    b[pos++] = subc.index;

    if (msf) {
        b[pos++] = subc.rel_m;
        b[pos++] = subc.rel_s;
        b[pos++] = subc.rel_f;
        b[pos++] = subc.abs_m;
        b[pos++] = subc.abs_s;
        b[pos++] = subc.abs_f;
    } else {
        dat      = MSFtoLBA(subc.rel_m, subc.rel_s, subc.rel_f);
        b[pos++] = (dat >> 16) & 0xff;
        b[pos++] = (dat >> 8) & 0xff;
        b[pos++] = dat & 0xff;
        dat      = MSFtoLBA(subc.abs_m, subc.abs_s, subc.abs_f) - 150;
        b[pos++] = (dat >> 16) & 0xff;
        b[pos++] = (dat >> 8) & 0xff;
        b[pos++] = dat & 0xff;
    }
}


uint8_t
cdrom_get_audio_status_sony(cdrom_t *dev, uint8_t *b, int msf)
{
    uint8_t      ret;
    subchannel_t subc;
    uint32_t     dat;

    dev->ops->get_subchannel(dev, dev->seek_pos, &subc);

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

uint8_t
cdrom_get_current_subcodeq_playstatus(cdrom_t *dev, uint8_t *b)
{
    uint8_t      ret;
    subchannel_t subc;

    dev->ops->get_subchannel(dev, dev->seek_pos, &subc);

    cdrom_log("Get Current Subcode-q Play Status = %02x, op = %02x.\n", dev->cd_status, dev->audio_op);

    if ((dev->cd_status == CD_STATUS_DATA_ONLY) || (dev->cd_status == CD_STATUS_PLAYING_COMPLETED))
        ret = 0x03;
    else
        ret = (dev->cd_status == CD_STATUS_PLAYING) ? 0x00 : dev->audio_op;

    b[0] = subc.attr;
    b[1] = bin2bcd(subc.track);
    b[2] = bin2bcd(subc.index);
    b[3] = bin2bcd(subc.rel_m);
    b[4] = bin2bcd(subc.rel_s);
    b[5] = bin2bcd(subc.rel_f);
    b[6] = bin2bcd(subc.abs_m);
    b[7] = bin2bcd(subc.abs_s);
    b[8] = bin2bcd(subc.abs_f);
    return ret;
}

static int
read_toc_normal(cdrom_t *dev, unsigned char *b, unsigned char start_track, int msf)
{
    track_info_t ti;
    int          i;
    int          len = 4;
    int          m;
    int          s;
    int          f;
    int          first_track;
    int          last_track;
    uint32_t     temp;

    cdrom_log("read_toc_normal(%08X, %08X, %02X, %i)\n", dev, b, start_track, msf);

    dev->ops->get_tracks(dev, &first_track, &last_track);

    /* Byte 2 = Number of the first track */
    dev->ops->get_track_info(dev, 1, 0, &ti);
    b[2] = ti.number;
    cdrom_log("    b[2] = %02X\n", b[2]);

    /* Byte 3 = Number of the last track before the lead-out track */
    dev->ops->get_track_info(dev, last_track, 0, &ti);
    b[3] = ti.number;
    cdrom_log("    b[3] = %02X\n", b[2]);

    if (start_track == 0x00)
        first_track = 0;
    else {
        first_track = -1;
        for (i = 0; i <= last_track; i++) {
            dev->ops->get_track_info(dev, i + 1, 0, &ti);
            if (ti.number >= start_track) {
                first_track = i;
                break;
            }
        }
    }
    cdrom_log("    first_track = %i, last_track = %i\n", first_track, last_track);

    /* No suitable starting track, return with error. */
    if (first_track == -1) {
#ifdef ENABLE_CDROM_LOG
        cdrom_log("    [ERROR] No suitable track found\n");
#endif
        return -1;
    }

    for (i = first_track; i <= last_track; i++) {
        cdrom_log("    tracks(%i) = %02X, %02X, %i:%02i.%02i\n", i, ti.attr, ti.number, ti.m, ti.s, ti.f);
        dev->ops->get_track_info(dev, i + 1, 0, &ti);

        b[len++] = 0; /* reserved */
        b[len++] = ti.attr;
        b[len++] = ti.number; /* track number */
        b[len++] = 0;         /* reserved */

        if (msf) {
            b[len++] = 0;

            /* NEC CDR-260 speaks BCD. */
            if (!strcmp(cdrom_drive_types[dev->type].internal_name, "NEC_CD-ROM_DRIVE260_1.01") || (!strcmp(cdrom_drive_types[dev->type].internal_name, "NEC_CD-ROM_DRIVE260_1.00"))) { /*NEC*/
                m = ti.m;
                s = ti.s;
                f = ti.f;
                msf_to_bcd(&m, &s, &f);
                b[len++] = m;
                b[len++] = s;
                b[len++] = f;
            } else {
                b[len++] = ti.m;
                b[len++] = ti.s;
                b[len++] = ti.f;
            }
        } else {
            temp     = MSFtoLBA(ti.m, ti.s, ti.f) - 150;
            b[len++] = temp >> 24;
            b[len++] = temp >> 16;
            b[len++] = temp >> 8;
            b[len++] = temp;
        }
    }

    return len;
}

static int
read_toc_session(cdrom_t *dev, unsigned char *b, int msf)
{
    track_info_t ti;
    int          len = 4;
    int          m;
    int          s;
    int          f;
    uint32_t     temp;

    cdrom_log("read_toc_session(%08X, %08X, %i)\n", dev, b, msf);

    /* Bytes 2 and 3 = Number of first and last sessions */
    b[2] = b[3] = 1;

    dev->ops->get_track_info(dev, 1, 0, &ti);

    cdrom_log("    tracks(0) = %02X, %02X, %i:%02i.%02i\n", ti.attr, ti.number, ti.m, ti.s, ti.f);

    b[len++] = 0; /* reserved */
    b[len++] = ti.attr;
    b[len++] = ti.number; /* track number */
    b[len++] = 0;         /* reserved */

    if (msf) {
        b[len++] = 0;

        /* NEC CDR-260 speaks BCD. */
        if (!strcmp(cdrom_drive_types[dev->type].internal_name, "NEC_CD-ROM_DRIVE260_1.01") || (!strcmp(cdrom_drive_types[dev->type].internal_name, "NEC_CD-ROM_DRIVE260_1.00"))) { /*NEC*/
            m = ti.m;
            s = ti.s;
            f = ti.f;
            msf_to_bcd(&m, &s, &f);
            b[len++] = m;
            b[len++] = s;
            b[len++] = f;
        } else {
            b[len++] = ti.m;
            b[len++] = ti.s;
            b[len++] = ti.f;
        }
    } else {
        temp     = MSFtoLBA(ti.m, ti.s, ti.f) - 150;
        b[len++] = temp >> 24;
        b[len++] = temp >> 16;
        b[len++] = temp >> 8;
        b[len++] = temp;
    }

    return len;
}

static int
read_toc_raw(cdrom_t *dev, unsigned char *b)
{
    track_info_t ti;
    int          len = 4;
    int          first_track;
    int          last_track;

    cdrom_log("read_toc_raw(%08X, %08X)\n", dev, b);

    dev->ops->get_tracks(dev, &first_track, &last_track);

    /* Bytes 2 and 3 = Number of first and last sessions */
    b[2] = b[3] = 1;

    for (int i = 0; i <= last_track; i++) {
        dev->ops->get_track_info(dev, i + 1, 0, &ti);

        cdrom_log("    tracks(%i) = %02X, %02X, %i:%02i.%02i\n", i, ti.attr, ti.number, ti.m, ti.s, ti.f);

        b[len++] = 1;         /* Session number */
        b[len++] = ti.attr;   /* Track ADR and Control */
        b[len++] = 0;         /* TNO (always 0) */
        b[len++] = ti.number; /* Point (for track points - track number) */
        b[len++] = ti.m;      /* M */
        b[len++] = ti.s;      /* S */
        b[len++] = ti.f;      /* F */
        b[len++] = 0;
        b[len++] = 0;
        b[len++] = 0;
    }

    return len;
}

static int
read_toc_sony(cdrom_t *dev, unsigned char *b, unsigned char start_track, int msf)
{
    track_info_t ti;
    int          i;
    int          len = 4;
    int          first_track;
    int          last_track;
    uint32_t     temp;

    cdrom_log("read_toc_sony(%08X, %08X, %02X, %i)\n", dev, b, start_track, msf);

    dev->ops->get_tracks(dev, &first_track, &last_track);

    /* Byte 2 = Number of the first track */
    dev->ops->get_track_info(dev, 1, 0, &ti);
    b[2] = ti.number;
    cdrom_log("    b[2] = %02X\n", b[2]);

    /* Byte 3 = Number of the last track before the lead-out track */
    dev->ops->get_track_info(dev, last_track, 0, &ti);
    b[3] = ti.number;
    cdrom_log("    b[3] = %02X\n", b[2]);

    if (start_track == 0x00)
        first_track = 0;
    else {
        first_track = -1;
        for (i = 0; i <= last_track; i++) {
            dev->ops->get_track_info(dev, i + 1, 0, &ti);
            if (ti.number >= start_track) {
                first_track = i;
                break;
            }
        }
    }
    cdrom_log("    first_track = %i, last_track = %i\n", first_track, last_track);

    /* No suitable starting track, return with error. */
    if (first_track == -1) {
#ifdef ENABLE_CDROM_LOG
        cdrom_log("    [ERROR] No suitable track found\n");
#endif
        return -1;
    }

    for (i = first_track; i <= last_track; i++) {
        cdrom_log("    tracks(%i) = %02X, %02X, %i:%02i.%02i\n", i, ti.attr, ti.number, ti.m, ti.s, ti.f);
        dev->ops->get_track_info(dev, i + 1, 0, &ti);

        b[len++] = ti.attr;
        b[len++] = ti.number; /* track number */

        if (msf) {
            b[len++] = 0;
            b[len++] = ti.m;
            b[len++] = ti.s;
            b[len++] = ti.f;
        } else {
            temp     = MSFtoLBA(ti.m, ti.s, ti.f) - 150;
            b[len++] = temp >> 24;
            b[len++] = temp >> 16;
            b[len++] = temp >> 8;
            b[len++] = temp;
        }
    }

    return len;
}

int
cdrom_read_toc(cdrom_t *dev, unsigned char *b, int type, unsigned char start_track, int msf, int max_len)
{
    int len;

    switch (type) {
        case CD_TOC_NORMAL:
            len = read_toc_normal(dev, b, start_track, msf);
            break;
        case CD_TOC_SESSION:
            len = read_toc_session(dev, b, msf);
            break;
        case CD_TOC_RAW:
            len = read_toc_raw(dev, b);
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

    len = read_toc_sony(dev, b, start_track, msf);

    len = MIN(len, max_len);

    b[0] = (uint8_t) (((len - 2) >> 8) & 0xff);
    b[1] = (uint8_t) ((len - 2) & 0xff);

    return len;
}

/* New API calls for Mitsumi CD-ROM. */
void
cdrom_get_track_buffer(cdrom_t *dev, uint8_t *buf)
{
    track_info_t ti;
    int          first_track;
    int          last_track;

    if (dev != NULL) {
        dev->ops->get_tracks(dev, &first_track, &last_track);
        buf[0] = 1;
        buf[1] = last_track + 1;
        dev->ops->get_track_info(dev, 1, 0, &ti);
        buf[2] = ti.m;
        buf[3] = ti.s;
        buf[4] = ti.f;
        dev->ops->get_track_info(dev, last_track + 1, 0, &ti);
        buf[5] = ti.m;
        buf[6] = ti.s;
        buf[7] = ti.f;
        buf[8] = 0x00;
    } else
        memset(buf, 0x00, 9);
}

void
cdrom_get_q(cdrom_t *dev, uint8_t *buf, int *curtoctrk, uint8_t mode)
{
    track_info_t ti;
    int          first_track;
    int          last_track;

    if (dev != NULL) {
        dev->ops->get_tracks(dev, &first_track, &last_track);
        dev->ops->get_track_info(dev, *curtoctrk, 0, &ti);
        buf[0] = (ti.attr << 4) & 0xf0;
        buf[1] = ti.number;
        buf[2] = bin2bcd(*curtoctrk + 1);
        buf[3] = ti.m;
        buf[4] = ti.s;
        buf[5] = ti.f;
        buf[6] = 0x00;
        dev->ops->get_track_info(dev, 1, 0, &ti);
        buf[7] = ti.m;
        buf[8] = ti.s;
        buf[9] = ti.f;
        if (*curtoctrk >= (last_track + 1))
            *curtoctrk = 0;
        else if (mode)
            *curtoctrk = *curtoctrk + 1;
    } else
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

uint8_t
cdrom_read_disc_info_toc(cdrom_t *dev, unsigned char *b, unsigned char track, int type)
{
    track_info_t ti;
    int          first_track;
    int          last_track;
    int          m = 0;
    int          s = 0;
    int          f = 0;

    dev->ops->get_tracks(dev, &first_track, &last_track);

    cdrom_log("Read DISC Info TOC Type = %d, track = %d, first_track = %d, last_track = %d.\n", type, track, first_track, last_track);
    switch (type) {
        case 0:
            b[0] = bin2bcd(first_track);
            b[1] = bin2bcd(last_track);
            b[2] = 0;
            b[3] = 0;
            cdrom_log("CD-ROM %i: Returned Toshiba/NEC disc information (type 0) at %02i:%02i\n", dev->id, b[0], b[1]);
            break;
        case 1:
            dev->ops->get_track_info(dev, 0xaa, 0, &ti);
            m = ti.m;
            s = ti.s;
            f = ti.f;
            msf_to_bcd(&m, &s, &f);
            b[0] = m;
            b[1] = s;
            b[2] = f;
            b[3] = 0;
            cdrom_log("CD-ROM %i: Returned Toshiba/NEC disc information (type 1) at %02i:%02i.%02i, track=%d\n", dev->id, b[0], b[1], b[2], bcd2bin(track));
            break;
        case 2:
            if (track > bin2bcd(last_track))
                return 0;

            dev->ops->get_track_info(dev, bcd2bin(track), 0, &ti);
            m = ti.m;
            s = ti.s;
            f = ti.f;
            msf_to_bcd(&m, &s, &f);
            b[0] = m;
            b[1] = s;
            b[2] = f;
            b[3] = ti.attr;
            cdrom_log("CD-ROM %i: Returned Toshiba/NEC disc information (type 2) at %02i:%02i.%02i, track=%d, m=%02i,s=%02i,f=%02i, tno=%02x.\n", dev->id, b[0], b[1], b[2], bcd2bin(track), m, s, f, ti.attr);
            break;
        case 3:
            b[0] = 0x00; /*TODO: correct it further, mark it as CD-Audio/CD-ROM disc for now*/
            b[1] = 0;
            b[2] = 0;
            b[3] = 0;
            break;
    }

    return 1;
}

static int
track_type_is_valid(uint8_t id, int type, int flags, int audio, int mode2)
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

static void
read_sector_to_buffer(cdrom_t *dev, uint8_t *rbuf, uint32_t msf, uint32_t lba, int mode2, int len)
{
    uint8_t *bb = rbuf;

    dev->ops->read_sector(dev, CD_READ_DATA, rbuf + 16, lba);

    /* Sync bytes */
    bb[0] = 0;
    memset(bb + 1, 0xff, 10);
    bb[11] = 0;
    bb += 12;

    /* Sector header */
    bb[0] = (msf >> 16) & 0xff;
    bb[1] = (msf >> 8) & 0xff;
    bb[2] = msf & 0xff;

    bb[3] = 1; /* mode 1 data */
    bb += mode2 ? 12 : 4;
    bb += len;
    if (mode2 && ((mode2 & 0x03) == 1))
        memset(bb, 0, 280);
    else if (!mode2)
        memset(bb, 0, 288);
}

static void
read_audio(cdrom_t *dev, uint32_t lba, uint8_t *b)
{
    dev->ops->read_sector(dev, CD_READ_RAW, raw_buffer, lba);

    memcpy(b, raw_buffer, 2352);

    cdrom_sector_size = 2352;
}

static void
read_mode1(cdrom_t *dev, int cdrom_sector_flags, uint32_t lba, uint32_t msf, int mode2, uint8_t *b)
{
    if ((dev->cd_status == CD_STATUS_DATA_ONLY) || (dev->ops->sector_size(dev, lba) == 2048))
        read_sector_to_buffer(dev, raw_buffer, msf, lba, mode2, 2048);
    else
        dev->ops->read_sector(dev, CD_READ_RAW, raw_buffer, lba);

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

static void
read_mode2_non_xa(cdrom_t *dev, int cdrom_sector_flags, uint32_t lba, uint32_t msf, int mode2, uint8_t *b)
{
    if ((dev->cd_status == CD_STATUS_DATA_ONLY) || (dev->ops->sector_size(dev, lba) == 2336))
        read_sector_to_buffer(dev, raw_buffer, msf, lba, mode2, 2336);
    else
        dev->ops->read_sector(dev, CD_READ_RAW, raw_buffer, lba);

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
}

static void
read_mode2_xa_form1(cdrom_t *dev, int cdrom_sector_flags, uint32_t lba, uint32_t msf, int mode2, uint8_t *b)
{
    if ((dev->cd_status == CD_STATUS_DATA_ONLY) || (dev->ops->sector_size(dev, lba) == 2048))
        read_sector_to_buffer(dev, raw_buffer, msf, lba, mode2, 2048);
    else
        dev->ops->read_sector(dev, CD_READ_RAW, raw_buffer, lba);

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

static void
read_mode2_xa_form2(cdrom_t *dev, int cdrom_sector_flags, uint32_t lba, uint32_t msf, int mode2, uint8_t *b)
{
    if ((dev->cd_status == CD_STATUS_DATA_ONLY) || (dev->ops->sector_size(dev, lba) == 2324))
        read_sector_to_buffer(dev, raw_buffer, msf, lba, mode2, 2324);
    else
        dev->ops->read_sector(dev, CD_READ_RAW, raw_buffer, lba);

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
}

int
cdrom_readsector_raw(cdrom_t *dev, uint8_t *buffer, int sector, int ismsf, int cdrom_sector_type,
                     int cdrom_sector_flags, int *len, uint8_t vendor_type)
{
    uint8_t *b;
    uint8_t *temp_b;
    uint32_t msf;
    uint32_t lba;
    int      audio = 0;
    int      mode2 = 0;
    int      m;
    int      s;
    int      f;

    if (dev->cd_status == CD_STATUS_EMPTY)
        return 0;

    b = temp_b = buffer;

    *len = 0;

    if (ismsf) {
        m   = (sector >> 16) & 0xff;
        s   = (sector >> 8) & 0xff;
        f   = sector & 0xff;
        lba = MSFtoLBA(m, s, f) - 150;
        msf = sector;
    } else {
        switch (vendor_type) {
            case 0x00:
                lba = sector;
                msf = cdrom_lba_to_msf_accurate(sector);
                break;
            case 0x40:
                m = bcd2bin((sector >> 24) & 0xff);
                s = bcd2bin((sector >> 16) & 0xff);
                f = bcd2bin((sector >> 8) & 0xff);
                lba = MSFtoLBA(m, s, f) - 150;
                msf = sector;
                break;
            case 0x80:
                lba = bcd2bin((sector >> 24) & 0xff);
                msf = sector;
                break;
            /* Never used values but the compiler complains. */
            default:
                lba = msf = 0;
        }
    }

    if (dev->ops->track_type)
        audio = dev->ops->track_type(dev, lba);

    mode2 = audio & ~CD_TRACK_AUDIO;
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

        read_audio(dev, lba, temp_b);
    } else if (cdrom_sector_type == 2) {
        if (audio || mode2) {
            cdrom_log("CD-ROM %i: [Mode 1] Attempting to read a sector of another type\n", dev->id);
            return 0;
        }

        read_mode1(dev, cdrom_sector_flags, lba, msf, mode2, temp_b);
    } else if (cdrom_sector_type == 3) {
        if (audio || !mode2 || (mode2 & 0x03)) {
            cdrom_log("CD-ROM %i: [Mode 2 Formless] Attempting to read a sector of another type\n", dev->id);
            return 0;
        }

        read_mode2_non_xa(dev, cdrom_sector_flags, lba, msf, mode2, temp_b);
    } else if (cdrom_sector_type == 4) {
        if (audio || !mode2 || ((mode2 & 0x03) != 1)) {
            cdrom_log("CD-ROM %i: [XA Mode 2 Form 1] Attempting to read a sector of another type\n", dev->id);
            return 0;
        }

        read_mode2_xa_form1(dev, cdrom_sector_flags, lba, msf, mode2, temp_b);
    } else if (cdrom_sector_type == 5) {
        if (audio || !mode2 || ((mode2 & 0x03) != 2)) {
            cdrom_log("CD-ROM %i: [XA Mode 2 Form 2] Attempting to read a sector of another type\n", dev->id);
            return 0;
        }

        read_mode2_xa_form2(dev, cdrom_sector_flags, lba, msf, mode2, temp_b);
    } else if (cdrom_sector_type == 8) {
        if (audio) {
            cdrom_log("CD-ROM %i: [Any Data] Attempting to read a data sector from an audio track\n", dev->id);
            return 0;
        }

        if (mode2 && ((mode2 & 0x03) == 1))
            read_mode2_xa_form1(dev, cdrom_sector_flags, lba, msf, mode2, temp_b);
        else if (!mode2)
            read_mode1(dev, cdrom_sector_flags, lba, msf, mode2, temp_b);
        else {
            cdrom_log("CD-ROM %i: [Any Data] Attempting to read a data sector whose cooked size is not 2048 bytes\n", dev->id);
            return 0;
        }
    } else {
        if (mode2) {
            if ((mode2 & 0x03) == 0x01)
                read_mode2_xa_form1(dev, cdrom_sector_flags, lba, msf, mode2, temp_b);
            else if ((mode2 & 0x03) == 0x02)
                read_mode2_xa_form2(dev, cdrom_sector_flags, lba, msf, mode2, temp_b);
            else
                read_mode2_non_xa(dev, cdrom_sector_flags, lba, msf, mode2, temp_b);
        } else {
            if (audio)
                read_audio(dev, lba, temp_b);
            else
                read_mode1(dev, cdrom_sector_flags, lba, msf, mode2, temp_b);
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

    return 1;
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

            if (dev->host_drive == 200)
                cdrom_image_open(dev, dev->image_path);
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

    if (dev->bus_type) {
        if (dev->insert)
            dev->insert(dev->priv);
    }
}

/* The mechanics of ejecting a CD-ROM from a drive. */
void
cdrom_eject(uint8_t id)
{
    cdrom_t *dev = &cdrom[id];

    /* This entire block should be in cdrom.c/cdrom_eject(dev*) ... */
    if (dev->host_drive == 0) {
        /* Switch from empty to empty. Do nothing. */
        return;
    }

    if (dev->host_drive == 200)
        strcpy(dev->prev_image_path, dev->image_path);

    dev->prev_host_drive = dev->host_drive;
    dev->host_drive      = 0;

    dev->ops->exit(dev);
    dev->ops = NULL;
    memset(dev->image_path, 0, sizeof(dev->image_path));

    cdrom_insert(id);

    plat_cdrom_ui_update(id, 0);

    config_save();
}

/* The mechanics of re-loading a CD-ROM drive. */
void
cdrom_reload(uint8_t id)
{
    cdrom_t *dev = &cdrom[id];

    if ((dev->host_drive == dev->prev_host_drive) || (dev->prev_host_drive == 0) || (dev->host_drive != 0)) {
        /* Switch from empty to empty. Do nothing. */
        return;
    }

    if (dev->ops && dev->ops->exit)
        dev->ops->exit(dev);
    dev->ops = NULL;
    memset(dev->image_path, 0, sizeof(dev->image_path));

    if (dev->prev_host_drive == 200) {
        /* Reload a previous image. */
        strcpy(dev->image_path, dev->prev_image_path);
        cdrom_image_open(dev, dev->image_path);

        cdrom_insert(id);

        if (strlen(dev->image_path) == 0)
            dev->host_drive = 0;
        else
            dev->host_drive = 200;
    }

    plat_cdrom_ui_update(id, 1);

    config_save();
}
