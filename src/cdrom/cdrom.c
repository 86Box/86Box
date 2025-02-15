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
#ifdef ENABLE_CDROM_LOG
#include <stdarg.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/config.h>
#include <86box/cdrom.h>
#include <86box/cdrom_image.h>
#include <86box/cdrom_interface.h>
#include <86box/log.h>
#include <86box/plat.h>
#include <86box/plat_cdrom_ioctl.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/scsi_cdrom.h>
#include <86box/sound.h>
#include <86box/ui.h>

#define RAW_SECTOR_SIZE    2352

#define MIN_SEEK           2000
#define MAX_SEEK           333333

cdrom_t cdrom[CDROM_NUM] = { 0 };

int cdrom_interface_current;

#ifdef ENABLE_CDROM_LOG
int cdrom_do_log = ENABLE_CDROM_LOG;

static void
cdrom_log(void *priv, const char *fmt, ...)
{
    if (cdrom_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define cdrom_log(priv, fmt, ...)
#endif

static void    process_mode1(cdrom_t *dev, const int cdrom_sector_flags,
                             uint8_t *b);
static void    process_mode2_non_xa(cdrom_t *dev, const int cdrom_sector_flags,
                                    uint8_t *b);
static void    process_mode2_xa_form1(cdrom_t *dev, const int cdrom_sector_flags,
                                      uint8_t *b);
static void    process_mode2_xa_form2(cdrom_t *dev, const int cdrom_sector_flags,
                                      uint8_t *b);

typedef void (*cdrom_process_data_t)(cdrom_t *dev, const int cdrom_sector_flags,
                                     uint8_t *b);

static cdrom_process_data_t cdrom_process_data[4] = { process_mode1, process_mode2_non_xa,
                                                      process_mode2_xa_form1, process_mode2_xa_form2 };
#ifdef ENABLE_CDROM_LOG
static char *               cdrom_req_modes[14]   = { "Any", "Audio", "Mode 1", "Mode 2",
                                                      "CD-I/XA Mode 2 Form 1", "CD-I/XA Mode 2 Form 2", "Unk", "Unk",
                                                      "Any Data", "Any Data - 4",
                                                      "CD-I/XA Mode 2 Form 1", "CD-I/XA Mode 2 Form 1 - 4",
                                                      "Any CD-I/XA Data", "Any CD-I/XA Data - 4" };
static char *               cdrom_modes[4]        = { "Mode 1", "Mode 2", "CD-I/XA Mode 2 Form 1", "CD-I/XA Mode 2 Form 2" };
#endif
static uint8_t              cdrom_mode_masks[14]  = { 0x0f, 0x00, 0x01, 0x02, 0x04, 0x08, 0x00, 0x00,
                                                      0x05, 0x05, 0x04, 0x04, 0x0c, 0x0c };

static uint8_t              status_codes[2][8]    = { { 0x13, 0x15, 0x15, 0x15, 0x12, 0x11, 0x13, 0x13 },
                                                      { 0x00, 0x00, 0x00, 0x00, 0x15, 0x11, 0x00, 0x00 } };
static int                  mult                  = 1;
static int                  part                  = 0;
static int                  ecc_diff              = 288;

static const device_t cdrom_interface_none_device = {
    .name          = "None",
    .internal_name = "none",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
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

/* Private functions. */
static void
cdrom_generate_name(const int type, char *name, const int internal)
{
    char  elements[3][2048] = { 0 };

    memcpy(elements[0], cdrom_drive_types[type].vendor,
           strlen(cdrom_drive_types[type].vendor) + 1);
    if (internal)  for (int i = 0; i < strlen(elements[0]); i++)
        if (elements[0][i] == ' ')
            elements[0][i] = '_';

    if (internal) {
        int j = 0;
        for (int i = 0; i <= strlen(cdrom_drive_types[type].model); i++)
            if (cdrom_drive_types[type].model[i] != ':')
                elements[1][j++] = cdrom_drive_types[type].model[i];
    } else
        memcpy(elements[1], cdrom_drive_types[type].model,
               strlen(cdrom_drive_types[type].model) + 1);
    char *s = strstr(elements[1], "  ");
    if (s != NULL)
        s[0] = 0x00;
    if (internal)  for (int i = 0; i < strlen(elements[1]); i++)
        if (elements[1][i] == ' ')
            elements[1][i] = '_';

    memcpy(elements[2], cdrom_drive_types[type].revision,
           strlen(cdrom_drive_types[type].revision) + 1);
    s = strstr(elements[2], " ");
    if (s != NULL)
        s[0] = 0x00;
    if (internal)  for (int i = 0; i < strlen(elements[2]); i++)
        if (elements[2][i] == ' ')
            elements[2][i] = '_';

    if (internal)
        sprintf(name, "%s_%s_%s", elements[0], elements[1], elements[2]);
    else if (cdrom_drive_types[type].speed == -1)
        sprintf(name, "%s %s %s", elements[0], elements[1], elements[2]);
    else
        sprintf(name, "%s %s %s (%ix)", elements[0], elements[1],
                elements[2], cdrom_drive_types[type].speed);
}

static double
cdrom_get_short_seek(const cdrom_t *dev)
{
    switch (dev->cur_speed) {
        case 0:
            log_fatal(dev->log, "0x speed\n");
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
cdrom_get_long_seek(const cdrom_t *dev)
{
    switch (dev->cur_speed) {
        case 0:
            log_fatal(dev->log, "0x speed\n");
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

static int
read_data(cdrom_t *dev, const uint32_t lba)
{
    return dev->ops->read_sector(dev->local, dev->raw_buffer, lba);
}

static void
cdrom_get_subchannel(cdrom_t *dev, const uint32_t lba,
                     subchannel_t *subc, const int cooked)
{
    const uint8_t  *scb;
    uint32_t        scb_offs = 0;
    uint8_t         q[16]    = { 0 };

    if ((lba == dev->seek_pos) &&
        ((dev->cd_status == CD_STATUS_PLAYING) || (dev->cd_status == CD_STATUS_PAUSED)))
        scb      = dev->subch_buffer;
    else {
        scb      = (const uint8_t *) dev->raw_buffer;
        scb_offs = 2352;

        memset(dev->raw_buffer, 0, 2448);

        (void) read_data(dev, lba);
    }

    for (int i = 0; i < 12; i++)
        for (int j = 0; j < 8; j++)
             q[i] |= ((scb[scb_offs + (i << 3) + j] >> 6) & 0x01) << (7 - j);

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
}

static void
read_toc_identify_sessions(const raw_track_info_t *rti, const int num, unsigned char *b)
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
find_track(const raw_track_info_t *trti, const int num, const int first)
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
find_last_lead_out(const raw_track_info_t *trti, const int num)
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
find_specific_track(const raw_track_info_t *trti, const int num, const int track)
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
read_toc_normal(const cdrom_t *dev, unsigned char *b,
                const unsigned char start_track, const int msf,
                const int sony)
{
    uint8_t                 rti[65536]  = { 0 };
    uint8_t                 prti[65536] = { 0 };
    const raw_track_info_t *trti        = (raw_track_info_t *) rti;
    raw_track_info_t *      tprti       = (raw_track_info_t *) prti;
    int                     num         = 0;
    int                     len         = 4;
    int                     t           = -1;

    cdrom_log(dev->log, "read_toc_normal(%016" PRIXPTR ", %016" PRIXPTR ", %02X, %i)\n",
              (uintptr_t) dev, (uintptr_t) b, start_track, msf, sony);

    dev->ops->get_raw_track_info(dev->local, &num, rti);

    if (num > 0) {
        int j = 0;
        for (int i = 0; i < num; i++) {
            if ((trti[i].point >= 0x01) && (trti[i].point <= 0x63)) {
                tprti[j] = trti[i];
                if ((t == -1) && (tprti[j].point >= start_track))
                    t = j;
                cdrom_log(dev->log, "Sorted %03i = Unsorted %03i\n", j, i);
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
                if ((t == -1) && (tprti[j].point >= start_track))
                    t = j;
                cdrom_log(dev->log, "Sorted %03i = Unsorted %03i\n", j, i);
                j++;
                break;
            }
        }

        if (t != -1)  for (int i = t; i < j; i++) {
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
                if (dev->is_early) {
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
                const uint32_t temp = MSFtoLBA(tprti[i].pm, tprti[i].ps,
                                               tprti[i].pf) - 150;

                b[len++] = temp >> 24;
                b[len++] = temp >> 16;
                b[len++] = temp >> 8;
                b[len++] = temp;
            }

#ifdef ENABLE_CDROM_LOG
            cdrom_log(dev->log, "Track %02X: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                      i, c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]);
#endif
        }
    } else
        b[2] = b[3] = 0;

    return len;
}

static int
read_toc_session(const cdrom_t *dev, unsigned char *b, const int msf)
{
    uint8_t                 rti[65536] = { 0 };
    const raw_track_info_t *t          = (raw_track_info_t *) rti;
    const raw_track_info_t *first      = NULL;
    int                     num        = 0;
    int                     len        = 4;

    dev->ops->get_raw_track_info(dev->local, &num, rti);

    /* Bytes 2 and 3 = Number of first and last sessions */
    read_toc_identify_sessions((raw_track_info_t *) rti, num, b);

    cdrom_log(dev->log, "read_toc_session(%016" PRIXPTR ", %016" PRIXPTR ", %i)\n",
              (uintptr_t) dev, (uintptr_t) b, msf);

    if (num != 0) {
        for (int i = 0; i < num; i++) {
            if ((t[i].session == b[3]) && (t[i].point >= 0x01) && (t[i].point <= 0x63)) {
                first = &(t[i]);
                break;
            }
        }
        if (first != NULL) {
            b[len++] = 0x00;
            b[len++] = first->adr_ctl;
            b[len++] = first->point;
            b[len++] = 0x00;

            if (msf) {
                b[len++] = 0x00;

                /* NEC CDR-260 speaks BCD. */
                if (dev->is_early) {
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
                const uint32_t temp = MSFtoLBA(first->pm, first->ps,
                                               first->pf) - 150;

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
read_toc_raw(const cdrom_t *dev, unsigned char *b, const unsigned char start_track)
{
    uint8_t                 rti[65536] = { 0 };
    const raw_track_info_t *t          = (raw_track_info_t *) rti;
    int                     num        = 0;
    int                     len        = 4;

    /* Bytes 2 and 3 = Number of first and last sessions */
    read_toc_identify_sessions((raw_track_info_t *) rti, num, b);

    cdrom_log(dev->log, "read_toc_raw(%016" PRIXPTR ", %016" PRIXPTR ", %02X)\n",
              (uintptr_t) dev, (uintptr_t) b, start_track);

    dev->ops->get_raw_track_info(dev->local, &num, rti);

    if (num != 0)  for (int i = 0; i < num; i++)
        if (t[i].session >= start_track) {
            memcpy(&(b[len]), &(t[i]), 11);
            len += 11;
        }

    return len;
}

static int
track_type_is_valid(UNUSED(const cdrom_t *dev), const int type, const int flags, const int audio,
                    const int mode2)
{
    if (!(flags & 0x70) && (flags & 0xf8)) { /* 0x08/0x80/0x88 are illegal modes */
        cdrom_log(dev->log, "[Any Mode] 0x08/0x80/0x88 are illegal modes\n");
        return 0;
    }

    if ((type != 1) && !audio) {
        if ((flags & 0x06) == 0x06) {
            cdrom_log(dev->log, "[Any Data Mode] Invalid error flags\n");
            return 0;
        }

        if (((flags & 0x700) == 0x300) || ((flags & 0x700) > 0x400)) {
            cdrom_log(dev->log, "[Any Data Mode] Invalid subchannel data flags (%02X)\n",
                      flags & 0x700);
            return 0;
        }

        if ((flags & 0x18) == 0x08) { /* EDC/ECC without user data is an illegal mode */
            cdrom_log(dev->log, "[Any Data Mode] EDC/ECC without user data is an "
                      "illegal mode\n");
            return 0;
        }

        if (((flags & 0xf0) == 0x90) || ((flags & 0xf0) == 0xc0)) { /* 0x90/0x98/0xC0/0xC8 are illegal modes */
            cdrom_log(dev->log, "[Any Data Mode] 0x90/0x98/0xC0/0xC8 are illegal modes\n");
            return 0;
        }

        if (((type > 3) && (type != 8)) || (mode2 && (mode2 & 0x03))) {
            if ((flags & 0xf0) == 0x30) { /* 0x30/0x38 are illegal modes */
                cdrom_log(dev->log, "[Any XA Mode 2] 0x30/0x38 are illegal modes\n");
                return 0;
            }
            if (((flags & 0xf0) == 0xb0) || ((flags & 0xf0) == 0xd0)) { /* 0xBx and 0xDx are illegal modes */
                cdrom_log(dev->log, "[Any XA Mode 2] 0xBx and 0xDx are illegal modes\n");
                return 0;
            }
        }
    }

    return 1;
}

static int
read_audio(cdrom_t *dev, const uint32_t lba, uint8_t *b)
{
    const int ret = dev->ops->read_sector(dev->local, dev->raw_buffer, lba);

    memcpy(b, dev->raw_buffer, 2352);

    dev->cdrom_sector_size = 2352;

    return ret;
}


static void
process_mode1(cdrom_t *dev, const int cdrom_sector_flags, uint8_t *b)
{
    dev->cdrom_sector_size = 0;

    if (cdrom_sector_flags & 0x80) {
        /* Sync */
        cdrom_log(dev->log, "[Mode 1] Sync\n");
        memcpy(b, dev->raw_buffer, 12);
        dev->cdrom_sector_size += 12;
        b += 12;
    }

    if (cdrom_sector_flags & 0x20) {
        /* Header */
        cdrom_log(dev->log, "[Mode 1] Header\n");
        memcpy(b, dev->raw_buffer + 12, 4);
        dev->cdrom_sector_size += 4;
        b += 4;
    }

    if (cdrom_sector_flags & 0x40) {
        /* Sub-header */
        if (!(cdrom_sector_flags & 0x10)) {
            /* No user data */
            cdrom_log(dev->log, "[Mode 1] Sub-header\n");
            memcpy(b, dev->raw_buffer + 16, 8);
            dev->cdrom_sector_size += 8;
            b += 8;
        }
    }

    if (cdrom_sector_flags & 0x10) {
        /* User data */
        cdrom_log(dev->log, "[Mode 1] User data\n");
        if (mult > 1) {
            memcpy(b, dev->raw_buffer + 16 + (part * dev->sector_size),
                   dev->sector_size);
            dev->cdrom_sector_size += dev->sector_size;
            b += dev->sector_size;
        } else {
            memcpy(b, dev->raw_buffer + 16, 2048);
            dev->cdrom_sector_size += 2048;
            b += 2048;
        }
    }

    if (cdrom_sector_flags & 0x08) {
        /* EDC/ECC */
        cdrom_log(dev->log, "[Mode 1] EDC/ECC\n");
        memcpy(b, dev->raw_buffer + 2064, (288 - ecc_diff));
        dev->cdrom_sector_size += (288 - ecc_diff);
    }
}

static void
process_mode2_non_xa(cdrom_t *dev, const int cdrom_sector_flags,
                     uint8_t *b)
{
    dev->cdrom_sector_size = 0;

    if (cdrom_sector_flags & 0x80) {
        /* Sync */
        cdrom_log(dev->log, "[Mode 2 Formless] Sync\n");
        memcpy(b, dev->raw_buffer, 12);
        dev->cdrom_sector_size += 12;
        b += 12;
    }

    if (cdrom_sector_flags & 0x20) {
        /* Header */
        cdrom_log(dev->log, "[Mode 2 Formless] Header\n");
        memcpy(b, dev->raw_buffer + 12, 4);
        dev->cdrom_sector_size += 4;
        b += 4;
    }

    /* Mode 1 sector, expected type is 1 type. */
    if (cdrom_sector_flags & 0x40) {
        /* Sub-header */
        cdrom_log(dev->log, "[Mode 2 Formless] Sub-header\n");
        memcpy(b, dev->raw_buffer + 16, 8);
        dev->cdrom_sector_size += 8;
        b += 8;
    }

    if (cdrom_sector_flags & 0x10) {
        /* User data */
        cdrom_log(dev->log, "[Mode 2 Formless] User data\n");
        memcpy(b, dev->raw_buffer + 24, (2336 - ecc_diff));
        dev->cdrom_sector_size += (2336 - ecc_diff);
    }
}

static void
process_mode2_xa_form1(cdrom_t *dev, const int cdrom_sector_flags,
                       uint8_t *b)
{
    dev->cdrom_sector_size = 0;

    if (cdrom_sector_flags & 0x80) {
        /* Sync */
        cdrom_log(dev->log, "[XA Mode 2 Form 1] Sync\n");
        memcpy(b, dev->raw_buffer, 12);
        dev->cdrom_sector_size += 12;
        b += 12;
    }

    if (cdrom_sector_flags & 0x20) {
        /* Header */
        cdrom_log(dev->log, "[XA Mode 2 Form 1] Header\n");
        memcpy(b, dev->raw_buffer + 12, 4);
        dev->cdrom_sector_size += 4;
        b += 4;
    }

    if (cdrom_sector_flags & 0x40) {
        /* Sub-header */
        cdrom_log(dev->log, "[XA Mode 2 Form 1] Sub-header\n");
        memcpy(b, dev->raw_buffer + 16, 8);
        dev->cdrom_sector_size += 8;
        b += 8;
    }

    if (cdrom_sector_flags & 0x10) {
        /* User data */
        cdrom_log(dev->log, "[XA Mode 2 Form 1] User data\n");
        if (mult > 1) {
            memcpy(b, dev->raw_buffer + 24 + (part * dev->sector_size),
                   dev->sector_size);
            dev->cdrom_sector_size += dev->sector_size;
            b += dev->sector_size;
        } else {
            memcpy(b, dev->raw_buffer + 24, 2048);
            dev->cdrom_sector_size += 2048;
            b += 2048;
        }
    }

    if (cdrom_sector_flags & 0x08) {
        /* EDC/ECC */
        cdrom_log(dev->log, "[XA Mode 2 Form 1] EDC/ECC\n");
        memcpy(b, dev->raw_buffer + 2072, (280 - ecc_diff));
        dev->cdrom_sector_size += (280 - ecc_diff);
    }
}

static void
process_mode2_xa_form2(cdrom_t *dev, const int cdrom_sector_flags,
                       uint8_t *b)
{
    dev->cdrom_sector_size = 0;

    if (cdrom_sector_flags & 0x80) {
        /* Sync */
        cdrom_log(dev->log, "[XA Mode 2 Form 2] Sync\n");
        memcpy(b, dev->raw_buffer, 12);
        dev->cdrom_sector_size += 12;
        b += 12;
    }

    if (cdrom_sector_flags & 0x20) {
        /* Header */
        cdrom_log(dev->log, "[XA Mode 2 Form 2] Header\n");
        memcpy(b, dev->raw_buffer + 12, 4);
        dev->cdrom_sector_size += 4;
        b += 4;
    }

    if (cdrom_sector_flags & 0x40) {
        /* Sub-header */
        cdrom_log(dev->log, "[XA Mode 2 Form 2] Sub-header\n");
        memcpy(b, dev->raw_buffer + 16, 8);
        dev->cdrom_sector_size += 8;
        b += 8;
    }

    if (cdrom_sector_flags & 0x10) {
        /* User data */
        cdrom_log(dev->log, "[XA Mode 2 Form 2] User data\n");
        memcpy(b, dev->raw_buffer + 24, (2328 - ecc_diff));
        dev->cdrom_sector_size += (2328 - ecc_diff);
    }
}

static void
process_ecc_and_subch(cdrom_t *dev, const int cdrom_sector_flags,
                      uint8_t *b)
{
    if ((cdrom_sector_flags & 0x06) == 0x02) {
        /* Add error flags. */
        cdrom_log(dev->log, "Error flags\n");
        memcpy(b + dev->cdrom_sector_size, dev->extra_buffer, 294);
        dev->cdrom_sector_size += 294;
    } else if ((cdrom_sector_flags & 0x06) == 0x04) {
        /* Add error flags. */
        cdrom_log(dev->log, "Full error flags\n");
        memcpy(b + dev->cdrom_sector_size, dev->extra_buffer, 296);
        dev->cdrom_sector_size += 296;
     }

     if ((cdrom_sector_flags & 0x700) == 0x100) {
         cdrom_log(dev->log, "Raw subchannel data\n");
         memcpy(b + dev->cdrom_sector_size, dev->raw_buffer + 2352, 96);
         dev->cdrom_sector_size += 96;
     } else if ((cdrom_sector_flags & 0x700) == 0x200) {
         cdrom_log(dev->log, "Q subchannel data\n");
         memcpy(b + dev->cdrom_sector_size, dev->raw_buffer + 2352, 16);
         dev->cdrom_sector_size += 16;
     } else if ((cdrom_sector_flags & 0x700) == 0x400) {
         cdrom_log(dev->log, "R/W subchannel data\n");
         memcpy(b + dev->cdrom_sector_size, dev->raw_buffer + 2352, 96);
         dev->cdrom_sector_size += 96;
     }
}

static void
cdrom_drive_reset(cdrom_t *dev)
{
    dev->priv        = NULL;
    dev->insert      = NULL;
    dev->close       = NULL;
    dev->get_volume  = NULL;
    dev->get_channel = NULL;

    if (cdrom_drive_types[dev->type].speed == -1)
        dev->real_speed  = dev->speed;
    else
        dev->real_speed  = cdrom_drive_types[dev->type].speed;
}

static void
cdrom_unload(cdrom_t *dev)
{
    if (dev->log != NULL) {
        cdrom_log(dev->log, "CDROM: cdrom_unload(%s)\n", dev->image_path);
    }

    dev->cd_status = CD_STATUS_EMPTY;

    if (dev->local != NULL) {
        dev->ops->close(dev->local);
        dev->local = NULL;
    }

    dev->ops = NULL;
}

/* Reset the CD-ROM Interface, whichever one that is. */
void
cdrom_interface_reset(void)
{
    /* If we have a valid controller, add its device. */
    if ((cdrom_interface_current > 0) &&
        controllers[cdrom_interface_current].device)
        device_add(controllers[cdrom_interface_current].device);
}

const char *
cdrom_interface_get_internal_name(const int cdinterface)
{
    return device_get_internal_name(controllers[cdinterface].device);
}

int
cdrom_interface_get_from_internal_name(const char *s)
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
cdrom_interface_get_device(const int cdinterface)
{
    return (controllers[cdinterface].device);
}

int
cdrom_interface_has_config(const int cdinterface)
{
    const device_t *dev = cdrom_interface_get_device(cdinterface);

    if (dev == NULL)
        return 0;

    if (!device_has_config(dev))
        return 0;

    return 1;
}

int
cdrom_interface_get_flags(const int cdinterface)
{
    return (controllers[cdinterface].device->flags);
}

int
cdrom_interface_available(const int cdinterface)
{
    return (device_available(controllers[cdinterface].device));
}

char *
cdrom_get_vendor(const int type)
{
    return (char *) cdrom_drive_types[type].vendor;
}

void
cdrom_get_model(const int type, char *name, const int id)
{
    if (!strcmp(cdrom_drive_types[type].vendor, EMU_NAME))
        sprintf(name, "%s%02i", cdrom_drive_types[type].model, id);
    else
        sprintf(name, "%s", cdrom_drive_types[type].model);
}

char *
cdrom_get_revision(const int type)
{
    return (char *) cdrom_drive_types[type].revision;
}

int
cdrom_get_scsi_std(const int type)
{
    return cdrom_drive_types[type].scsi_std;
}

int
cdrom_is_early(const int type)
{
    return (cdrom_drive_types[type].scsi_std == 1);
}

int
cdrom_is_generic(const int type)
{
    return (cdrom_drive_types[type].speed == -1);
}

int
cdrom_has_date(const int type)
{
    /* This will do for now. */
    return !strcmp(cdrom_drive_types[type].vendor, "PIONEER");
}

int
cdrom_is_sony(const int type)
{
    /* This will do for now. */
    return (cdrom_drive_types[type].bus_type == BUS_TYPE_SCSI) &&
           (!strcmp(cdrom_drive_types[type].vendor, "DEC") ||
            !strcmp(cdrom_drive_types[type].vendor, "ShinaKen") ||
            !strcmp(cdrom_drive_types[type].vendor, "SONY") ||
            !strcmp(cdrom_drive_types[type].vendor, "TEXEL"));
}

int
cdrom_is_caddy(const int type)
{
    return cdrom_drive_types[type].caddy;
}

int
cdrom_get_speed(const int type)
{
    return cdrom_drive_types[type].speed;
}

int
cdrom_get_inquiry_len(const int type)
{
    return cdrom_drive_types[type].inquiry_len;
}

int
cdrom_get_transfer_max(const int type, const int mode)
{
    return cdrom_drive_types[type].transfer_max[mode];
}

int
cdrom_has_dma(const int type)
{
    return (cdrom_drive_types[type].transfer_max[2] != -1);
}

int
cdrom_get_type_count(void)
{
    int count = 0;

    while (1) {
        if (strlen(cdrom_drive_types[count].vendor) == 0)
            break;
        else
            count++;
    }

    return count;
}

void
cdrom_get_identify_model(const int type, char *name, const int id)
{
    char  elements[2][2048] = { 0 };

    memcpy(elements[0], cdrom_drive_types[type].vendor,
           strlen(cdrom_drive_types[type].vendor) + 1);

    memcpy(elements[1], cdrom_drive_types[type].model,
           strlen(cdrom_drive_types[type].model) + 1);

    char *s = strstr(elements[1], "  ");

    if (s != NULL)
        s[0] = 0x00;

    if (!strcmp(cdrom_drive_types[type].vendor, EMU_NAME))
        sprintf(name, "%s%02i", elements[1], id);
    else if (!strcmp(cdrom_drive_types[type].vendor, "ASUS"))
        sprintf(name, "%s    %s", elements[0], elements[1]);
    else if (!strcmp(cdrom_drive_types[type].vendor, "NEC"))
        sprintf(name, "%s                 %s", elements[0], elements[1]);
    else if (!strcmp(cdrom_drive_types[type].vendor, "LITE-ON"))
        sprintf(name, "%s", elements[1]);
    else
        sprintf(name, "%s %s", elements[0], elements[1]);
}

void
cdrom_get_name(const int type, char *name)
{
    char n[2048] = { 0 };

    cdrom_generate_name(type, n, 0);

    if (cdrom_drive_types[type].bus_type == BUS_TYPE_SCSI)
        sprintf(name, "[SCSI-%i] %s", cdrom_drive_types[type].scsi_std, n);
    else
        sprintf(name, "%s", n);
}

char *
cdrom_get_internal_name(const int type)
{
    return (char *) cdrom_drive_types[type].internal_name;
}

int
cdrom_get_from_internal_name(const char *s)
{
    int  c       = 0;
    int  found   = 0;

    while (strlen(cdrom_drive_types[c].internal_name) > 0) {
        if (!strcmp((char *) cdrom_drive_types[c].internal_name, s)) {
            found = 1;
            break;
        }
        c++;
    }

    if (!found)
        c = -1;

    return c;
}

/* TODO: Configuration migration, remove when no longer needed. */
int
cdrom_get_from_name(const char *s)
{
    int  c       = 0;
    int  found   = 0;
    char n[2048] = { 0 };

    if (strcmp(s, "none")) {
        while (strlen(cdrom_drive_types[c].internal_name) > 0) {
            memset(n, 0x00, 2048);
            cdrom_generate_name(c, n, 1);
            /* Special case some names. */
            if ((!strcmp(s, "86BOX_CD-ROM_1.00") && !strcmp(n, "86Box_86B_CD_3.50")) ||
                (!strcmp(s, "TEAC_CD_532E_2.0A") && !strcmp(n, "TEAC_CD-532E_2.0A")) ||
                !strcmp(n, s)) {
                found = 1;
                break;
            }
            c++;
        }
    }

    if (!found) {
        if (strcmp(s, "none")) {
            wchar_t tempmsg[2048];
            sprintf(n, "WARNING: CD-ROM \"%s\" not found - contact 86Box support\n", s);
            swprintf(tempmsg, sizeof_w(tempmsg), L"%hs", n);
            pclog(n);
            ui_msgbox_header(MBX_INFO,
                             plat_get_string(STRING_HW_NOT_AVAILABLE_TITLE),
                             tempmsg);
        }
        c = -1;
    }

    return c;
}

void
cdrom_set_type(const int model, const int type)
{
    cdrom[model].type = type;
}

int
cdrom_get_type(const int model)
{
    return cdrom[model].type;
}

int
cdrom_lba_to_msf_accurate(const int lba)
{
    int       pos = lba + 150;
    const int f   = pos % 75;
    pos -= f;
    pos /= 75;
    const int s = pos % 60;
    pos -= s;
    pos /= 60;
    const int m = pos;

    return ((m << 16) | (s << 8) | f);
}

double
cdrom_seek_time(const cdrom_t *dev)
{
    uint32_t       diff = dev->seek_diff;
    const double   sd   = (double) (MAX_SEEK - MIN_SEEK);

    if (diff < MIN_SEEK)
        return 0.0;
    if (diff > MAX_SEEK)
        diff = MAX_SEEK;

    diff -= MIN_SEEK;

    return cdrom_get_short_seek(dev) +
           ((cdrom_get_long_seek(dev) * ((double) diff)) / sd);
}

void
cdrom_stop(cdrom_t *dev)
{
    if (dev->cd_status > CD_STATUS_DVD)
        dev->cd_status = CD_STATUS_STOPPED;
}

void
cdrom_seek(cdrom_t *dev, const uint32_t pos, const uint8_t vendor_type)
{
    int      m;
    int      s;
    int      f;
    uint32_t real_pos = pos;

    if (dev == NULL)
        return;

    cdrom_log(dev->log, "Seek to LBA %08X, vendor type = %02x.\n", pos, vendor_type);

    switch (vendor_type) {
        case 0x40:
            m = bcd2bin((pos >> 24) & 0xff);
            s = bcd2bin((pos >> 16) & 0xff);
            f = bcd2bin((pos >> 8) & 0xff);
            real_pos = MSFtoLBA(m, s, f) - 150;
            break;
        case 0x80:
            real_pos = bcd2bin((pos >> 24) & 0xff);
            break;
        default:
            break;
    }

    dev->seek_pos = real_pos;
    cdrom_stop(dev);
}

int
cdrom_is_pre(const cdrom_t *dev, const uint32_t lba)
{
    if (dev->ops && dev->ops->is_track_pre)
        return dev->ops->is_track_pre(dev->local, lba);

    return 0;
}

int
cdrom_audio_callback(cdrom_t *dev, int16_t *output, const int len)
{
    int ret = 1;

    if (!dev->sound_on || (dev->cd_status != CD_STATUS_PLAYING) || dev->audio_muted_soft) {
        // cdrom_log(dev->log, "Audio callback while not playing\n");
        if (dev->cd_status == CD_STATUS_PLAYING)
            dev->seek_pos += (len >> 11);
        memset(output, 0, len * 2);
        return 0;
    }

    while (dev->cd_buflen < len) {
        if (dev->seek_pos < dev->cd_end) {
            if (dev->ops->read_sector(dev->local,
                (uint8_t *) &(dev->cd_buffer[dev->cd_buflen]), dev->seek_pos)) {
                cdrom_log(dev->log, "Read LBA %08X successful\n", dev->seek_pos);
                memcpy(dev->subch_buffer,
                       ((uint8_t *) &(dev->cd_buffer[dev->cd_buflen])) + 2352, 96);
                dev->seek_pos++;
                dev->cd_buflen += (RAW_SECTOR_SIZE / 2);
                ret = 1;
            } else {
                cdrom_log(dev->log, "Read LBA %08X failed\n", dev->seek_pos);
                memset(&(dev->cd_buffer[dev->cd_buflen]), 0x00,
                       (BUF_SIZE - dev->cd_buflen) * 2);
                dev->cd_status    = CD_STATUS_STOPPED;
                dev->cd_buflen    = len;
                ret               = 0;
            }
        } else {
            cdrom_log(dev->log, "Playing completed\n");
            memset(&dev->cd_buffer[dev->cd_buflen], 0x00, (BUF_SIZE - dev->cd_buflen) * 2);
            dev->cd_status    = CD_STATUS_PLAYING_COMPLETED;
            dev->cd_buflen    = len;
            ret               = 0;
        }
    }

    memcpy(output, dev->cd_buffer, len * 2);
    memmove(dev->cd_buffer, &dev->cd_buffer[len], (BUF_SIZE - len) * 2);
    dev->cd_buflen -= len;

    cdrom_log(dev->log, "Audio callback returning %i\n", ret);
    return ret;
}

uint8_t
cdrom_audio_play(cdrom_t *dev, const uint32_t pos, const uint32_t len, const int ismsf)
{
    track_info_t ti;
    uint32_t     pos2 = pos;
    uint32_t     len2 = len;
    int          ret  = 0;

    if (dev->cd_status & CD_STATUS_HAS_AUDIO) {
        cdrom_log(dev->log, "Play audio - %08X %08X %i\n", pos2, len, ismsf);

        if (ismsf & 0x100) {
            /* Track-relative audio play. */
            ret = dev->ops->get_track_info(dev->local, ismsf & 0xff, 0, &ti);
            if (ret)
               pos2 += MSFtoLBA(ti.m, ti.s, ti.f) - 150;
            else {
                cdrom_log(dev->log, "Unable to get the starting position for "
                          "track %08X\n", ismsf & 0xff);
                cdrom_stop(dev);
            }
        } else if ((ismsf == 2) || (ismsf == 3)) {
            ret = dev->ops->get_track_info(dev->local, pos2, 0, &ti);
            if (ret) {
                pos2 = MSFtoLBA(ti.m, ti.s, ti.f) - 150;
                if (ismsf == 2) {
                    /* We have to end at the *end* of the specified track,
                       not at the beginning. */
                    ret = dev->ops->get_track_info(dev->local, len, 1, &ti);
                    if (ret)
                        len2 = MSFtoLBA(ti.m, ti.s, ti.f) - 150;
                    else {
                        cdrom_log(dev->log, "Unable to get the ending position for "
                                  "track %08X\n", pos2);
                        cdrom_stop(dev);
                    }
                }
            } else {
                cdrom_log(dev->log, "Unable to get the starting position for "
                          "track %08X\n", pos2);
                cdrom_stop(dev);
            }
        } else if (ismsf == 1) {
            int m = (pos >> 16) & 0xff;
            int s = (pos >> 8) & 0xff;
            int f = pos & 0xff;

            /* NEC CDR-260 speaks BCD. */
            if (dev->is_early)
                msf_from_bcd(&m, &s, &f);

            if (pos == 0xffffff) {
                cdrom_log(dev->log, "Playing from current position (MSF)\n");
                pos2 = dev->seek_pos;
            } else
                pos2 = MSFtoLBA(m, s, f) - 150;

            m = (len >> 16) & 0xff;
            s = (len >> 8) & 0xff;
            f = len & 0xff;

            /* NEC CDR-260 speaks BCD. */
            if (dev->is_early)
                msf_from_bcd(&m, &s, &f);

            len2 = MSFtoLBA(m, s, f) - 150;

            ret = 1;

            cdrom_log(dev->log, "MSF - pos = %08X len = %08X\n", pos2, len);
        } else if (ismsf == 0) {
            if (pos == 0xffffffff) {
                cdrom_log(dev->log, "Playing from current position\n");
                pos2 = dev->seek_pos;
            }
            len2 += pos2;

            ret  = 1;
        }
    }

    if (ret) {
        dev->audio_muted_soft = 0;

        /*
           Do this at this point, since it's at this point that we know the
           actual LBA position to start playing from.
         */
        ret = (dev->ops->get_track_type(dev->local, pos2) == CD_TRACK_AUDIO);

        if (ret) {
            dev->seek_pos  = pos2;
            dev->cd_end    = len2;
            dev->cd_status = CD_STATUS_PLAYING;
            dev->cd_buflen = 0;
        } else {
            cdrom_log(dev->log, "LBA %08X not on an audio track\n", pos);
            cdrom_stop(dev);
        }
    }

    return ret;
}

uint8_t
cdrom_audio_track_search(cdrom_t *dev, const uint32_t pos,
                         const int type, const uint8_t playbit)
{
    uint32_t pos2 = pos;
    uint8_t  ret  = 0;

    if (dev->cd_status & CD_STATUS_HAS_AUDIO) {
        cdrom_log(dev->log, "Audio Track Search: MSF = %06x, type = %02x, "
                  "playbit = %02x\n", pos, type, playbit);

        switch (type) {
            case 0x00:
                if (pos == 0xffffffff) {
                    cdrom_log(dev->log, "(Type 0) Search from current position\n");
                    pos2 = dev->seek_pos;
                }
                dev->seek_pos = pos2;
                break;
            case 0x40: {
                const int m   = bcd2bin((pos >> 24) & 0xff);
                const int s   = bcd2bin((pos >> 16) & 0xff);
                const int f   = bcd2bin((pos >> 8) & 0xff);
                if (pos == 0xffffffff) {
                    cdrom_log(dev->log, "(Type 1) Search from current position\n");
                    pos2 = dev->seek_pos;
                } else
                    pos2 = MSFtoLBA(m, s, f) - 150;

                dev->seek_pos = pos2;
                break;
            } case 0x80:
                if (pos == 0xffffffff) {
                    cdrom_log(dev->log, "(Type 2) Search from current position\n");
                    pos2 = dev->seek_pos;
                }
                dev->seek_pos = (pos2 >> 24) & 0xff;
                break;
            default:
                break;
        }

        if (pos2 != 0x00000000)
            pos2--;

        /*
           Do this at this point, since it's at this point that we know the
           actual LBA position to start playing from.
         */
        if (dev->ops->get_track_type(dev->local, pos2) & CD_TRACK_AUDIO)
            dev->audio_muted_soft = 0;
        else {
            cdrom_log(dev->log, "Track Search: LBA %08X not on an audio track\n", pos);
            dev->audio_muted_soft = 1;
            if (dev->ops->get_track_type(dev->local, pos) & CD_TRACK_AUDIO)
                dev->audio_muted_soft = 0;
        }

        cdrom_log(dev->log, "Track Search Toshiba: Muted?=%d, LBA=%08X.\n",
                  dev->audio_muted_soft, pos);
        dev->cd_buflen = 0;

        dev->cd_status = playbit ? CD_STATUS_PLAYING : CD_STATUS_PAUSED;

        ret            = 1;
    }

    return ret;
}

uint8_t
cdrom_audio_track_search_pioneer(cdrom_t *dev, const uint32_t pos, const uint8_t playbit)
{
    uint8_t  ret  = 0;

    if (dev->cd_status &= CD_STATUS_HAS_AUDIO) {
        const int f    = bcd2bin((pos >> 24) & 0xff);
        const int s    = bcd2bin((pos >> 16) & 0xff);
        const int m    = bcd2bin((pos >> 8) & 0xff);
        uint32_t  pos2;

        if (pos == 0xffffffff)
            pos2 = dev->seek_pos;
        else
            pos2 = MSFtoLBA(m, s, f) - 150;

        dev->seek_pos = pos2;

        dev->audio_muted_soft = 0;

        /*
           Do this at this point, since it's at this point that we know the
           actual LBA position to start playing from.
         */
        if (dev->ops->get_track_type(dev->local, pos2) & CD_TRACK_AUDIO) {
            dev->cd_buflen = 0;
            dev->cd_status = playbit ? CD_STATUS_PLAYING : CD_STATUS_PAUSED;

            ret = 1;
        } else {
            cdrom_log(dev->log, "LBA %08X not on an audio track\n", pos);
            cdrom_stop(dev);
        }
    }

    return ret;
}

uint8_t
cdrom_audio_play_pioneer(cdrom_t *dev, const uint32_t pos)
{
    uint8_t  ret  = 0;

    if (dev->cd_status & CD_STATUS_HAS_AUDIO) {
        const int f    = bcd2bin((pos >> 24) & 0xff);
        const int s    = bcd2bin((pos >> 16) & 0xff);
        const int m    = bcd2bin((pos >> 8) & 0xff);
        uint32_t  pos2 = MSFtoLBA(m, s, f) - 150;
        dev->cd_end = pos2;

        dev->audio_muted_soft = 0;
        dev->cd_buflen = 0;

        dev->cd_status = CD_STATUS_PLAYING;

        ret = 1;
    }

    return ret;
}

uint8_t
cdrom_audio_play_toshiba(cdrom_t *dev, const uint32_t pos, const int type)
{
    uint32_t pos2 = pos;
    uint8_t  ret  = 0;

    if (dev->cd_status & CD_STATUS_HAS_AUDIO) {
        /* Preliminary support, revert if too incomplete. */
        switch (type) {
            case 0x00:
                dev->cd_end = pos2;
                break;
            case 0x40: {
                const int m   = bcd2bin((pos >> 24) & 0xff);
                const int s   = bcd2bin((pos >> 16) & 0xff);
                const int f   = bcd2bin((pos >> 8) & 0xff);
                pos2 = MSFtoLBA(m, s, f) - 150;
                dev->cd_end = pos2;
                break;
            } case 0x80:
                dev->cd_end = (pos2 >> 24) & 0xff;
                break;
            case 0xc0:
                if (pos == 0xffffffff) {
                    cdrom_log(dev->log, "Playing from current position\n");
                    pos2 = dev->cd_end;
                }
                dev->cd_end = pos2;
                break;
            default:
                break;
        }

        cdrom_log(dev->log, "Toshiba Play Audio: Muted?=%d, LBA=%08X.\n",
                  dev->audio_muted_soft, pos2);
        dev->cd_buflen = 0;

        dev->cd_status = CD_STATUS_PLAYING;

        ret = 1;
    }

    return ret;
}

uint8_t
cdrom_audio_scan(cdrom_t *dev, const uint32_t pos, const int type)
{
    uint32_t pos2 = pos;
    uint8_t  ret  = 0;

    if (dev->cd_status & CD_STATUS_HAS_AUDIO) {
        cdrom_log(dev->log, "Audio Scan: MSF = %06x, type = %02x\n", pos, type);

        switch (type) {
            case 0x00:
                if (pos == 0xffffffff) {
                    cdrom_log(dev->log, "(Type 0) Search from current position\n");
                    pos2 = dev->seek_pos;
                }
                dev->seek_pos = pos2;
                break;
            case 0x40: {
                const int m   = bcd2bin((pos >> 24) & 0xff);
                const int s   = bcd2bin((pos >> 16) & 0xff);
                const int f   = bcd2bin((pos >> 8) & 0xff);
                if (pos == 0xffffffff) {
                    cdrom_log(dev->log, "(Type 1) Search from current position\n");
                    pos2 = dev->seek_pos;
                } else
                    pos2 = MSFtoLBA(m, s, f) - 150;

                dev->seek_pos = pos2;
                break;
            } case 0x80:
                dev->seek_pos = (pos >> 24) & 0xff;
                break;
            default:
                break;
        }

        dev->audio_muted_soft = 0;
        /* Do this at this point, since it's at this point that we know the
           actual LBA position to start playing from. */
        if (dev->ops->get_track_type(dev->local, pos) & CD_TRACK_AUDIO) {
            dev->cd_buflen = 0;
            ret            = 1;
        } else {
            cdrom_log(dev->log, "LBA %08X not on an audio track\n", pos);
            cdrom_stop(dev);
        }
    }

    return ret;
}

void
cdrom_audio_pause_resume(cdrom_t *dev, const uint8_t resume)
{
    if ((dev->cd_status == CD_STATUS_PLAYING) || (dev->cd_status == CD_STATUS_PAUSED))
        dev->cd_status = (dev->cd_status & 0xfe) | (resume & 0x01);
}

uint8_t
cdrom_get_current_status(const cdrom_t *dev)
{
    const uint8_t is_chinon = !strcmp(cdrom_drive_types[dev->type].vendor, "CHINON");
    const uint8_t ret       = status_codes[is_chinon][dev->cd_status & CD_STATUS_MASK];

    return ret;
}

void
cdrom_get_current_subchannel(cdrom_t *dev, uint8_t *b, const int msf)
{
    subchannel_t subc;

    cdrom_get_subchannel(dev, dev->seek_pos, &subc, 1);

    cdrom_log(dev->log, "Returned subchannel absolute at %02i:%02i.%02i, "
              "relative at %02i:%02i.%02i, seek pos = %08x, cd_end = %08x.\n",
              subc.abs_m, subc.abs_s, subc.abs_f, subc.rel_m, subc.rel_s, subc.rel_f,
              dev->seek_pos, dev->cd_end);

    /* Format code. */
    switch (b[0]) {
        /*
           Mode 0 = Q subchannel mode, first 16 bytes are indentical to mode 1 (current
                    position), the rest are stuff like ISRC etc., which can be all zeroes.
         */
        case 0x01:
            /* Current position. */
            b[1] = subc.attr;
            b[2] = subc.track;
            b[3] = subc.index;

            if (msf) {
                b[4] = b[8] = 0x00;

                /* NEC CDR-260 speaks BCD. */
                if (dev->is_early) {
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
                uint32_t dat   = MSFtoLBA(subc.abs_m, subc.abs_s, subc.abs_f) - 150;
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
            if (dev->is_early)
                b[19] = bin2bcd(subc.abs_f);
            else
                b[19] = subc.abs_f;
            break;
        case 0x03:
            /* ISRC - TODO: Finding and reporting the actual ISRC data. */
            memset(&(b[1]), 0x00, 19);
            memset(&(b[5]), 0x30, 12);
            /* NEC CDR-260 speaks BCD. */
            if (dev->is_early)
                b[18] = bin2bcd(subc.abs_f);
            else
                b[18] = subc.abs_f;
            break;
        default:
            cdrom_log(dev->log, "b[0] = %02X\n", b[0]);
            break;
    }
}

void
cdrom_get_current_subchannel_sony(cdrom_t *dev, uint8_t *b, const int msf)
{
    subchannel_t subc;

    cdrom_get_subchannel(dev, dev->seek_pos, &subc, 1);

    cdrom_log(dev->log, "Returned subchannel at %02i:%02i.%02i, seek pos = %08x, "
              "cd_end = %08x, msf = %x.\n",
              subc.abs_m, subc.abs_s, subc.abs_f, dev->seek_pos, dev->cd_end, msf);

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
        uint32_t dat      = MSFtoLBA(subc.rel_m, subc.rel_s, subc.rel_f);
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

    if (dev->cd_status & CD_STATUS_HAS_AUDIO) {
        if (dev->cd_status == CD_STATUS_PLAYING)
            ret = dev->sound_on ? 0x00 : 0x02;
        else if (dev->cd_status == CD_STATUS_PAUSED)
            ret = 0x01;
        else
            ret = 0x03;
    } else
        ret = 0x05;

    b[0] = 0;
    b[1] = subc.abs_m;
    b[2] = subc.abs_s;
    b[3] = subc.abs_f;

    return ret;
}

uint8_t
cdrom_get_audio_status_sony(cdrom_t *dev, uint8_t *b, const int msf)
{
    uint8_t      ret;
    subchannel_t subc;

    cdrom_get_subchannel(dev, dev->seek_pos, &subc, 1);

    if (dev->cd_status & CD_STATUS_HAS_AUDIO) {
        if (dev->cd_status == CD_STATUS_PLAYING)
            ret = dev->sound_on ? 0x00 : 0x02;
        else if (dev->cd_status == CD_STATUS_PAUSED)
            ret = 0x01;
        else
            ret = 0x03;
    } else
        ret = 0x05;

    if (msf) {
        b[0] = 0;
        b[1] = subc.abs_m;
        b[2] = subc.abs_s;
        b[3] = subc.abs_f;
    } else {
        const uint32_t dat = MSFtoLBA(subc.abs_m, subc.abs_s, subc.abs_f) - 150;
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
        (dev->cd_status == CD_STATUS_DVD) ||
        (dev->cd_status == CD_STATUS_PLAYING_COMPLETED) ||
        (dev->cd_status == CD_STATUS_STOPPED))
        ret = 0x03;
    else
        ret = (dev->cd_status == CD_STATUS_PLAYING) ? 0x00 : dev->audio_op;

    /*If a valid audio track is detected with audio on, unmute it.*/
    if (dev->ops->get_track_type(dev->local, dev->seek_pos) & CD_TRACK_AUDIO)
        dev->audio_muted_soft = 0;

    cdrom_log(dev->log, "SubCodeQ: Play Status: Seek LBA=%08x, CDEND=%08x, mute=%d.\n",
              dev->seek_pos, dev->cd_end, dev->audio_muted_soft);
    return ret;
}

int
cdrom_read_toc(const cdrom_t *dev, uint8_t *b, const int type,
               const uint8_t start_track, const int msf, const int max_len)
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
            cdrom_log(dev->log, "Unknown TOC read type: %i\n", type);
            return 0;
    }

    len = MIN(len, max_len);

    b[0] = (uint8_t) (((len - 2) >> 8) & 0xff);
    b[1] = (uint8_t) ((len - 2) & 0xff);

    return len;
}

int
cdrom_read_toc_sony(const cdrom_t *dev, uint8_t *b, const uint8_t start_track,
                    const int msf, const int max_len)
{
    int len = read_toc_normal(dev, b, start_track, msf, 1);

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
        dev->ops->get_raw_track_info(dev->local, &num, rti);

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
cdrom_get_q(UNUSED(cdrom_t *dev), uint8_t *buf, UNUSED(int *curtoctrk), UNUSED(uint8_t mode))
{
    memset(buf, 0x00, 10);
}

uint8_t
cdrom_mitsumi_audio_play(cdrom_t *dev, uint32_t pos, uint32_t len)
{
    track_info_t ti;
    int          ret = 0;

    if (dev->cd_status & CD_STATUS_HAS_AUDIO) {
        cdrom_log(dev->log, "Play Mitsumi audio - %08X %08X\n", pos, len);

        ret = dev->ops->get_track_info(dev->local, pos, 0, &ti);

        if (ret) {
            pos = MSFtoLBA(ti.m, ti.s, ti.f) - 150;
            ret = dev->ops->get_track_info(dev->local, len, 1, &ti);

            if (ret) {
                len = MSFtoLBA(ti.m, ti.s, ti.f) - 150;

                /*
                   Do this at this point, since it's at this point that we know the
                   actual LBA position to start playing from.
                 */
                ret = (dev->ops->get_track_type(dev->local, pos) == CD_TRACK_AUDIO);

                if (ret) {
                    dev->seek_pos  = pos;
                    dev->cd_end    = len;
                    dev->cd_status = CD_STATUS_PLAYING;
                    dev->cd_buflen = 0;
                } else {
                    cdrom_log(dev->log, "LBA %08X not on an audio track\n", pos);
                    cdrom_stop(dev);
                }
            } else {
                cdrom_log(dev->log, "Unable to get the ending position for track %08X\n",
                          len);
                cdrom_stop(dev);
            }
        } else {
            cdrom_log(dev->log, "Unable to get the starting position for track %08X\n", pos);
            cdrom_stop(dev);
        }
    }

    return ret;
}
#endif

uint8_t
cdrom_read_disc_info_toc(cdrom_t *dev, uint8_t *b,
                         const uint8_t track, const int type)
{
    uint8_t                 rti[65536]  = { 0 };
    const raw_track_info_t *trti        = (raw_track_info_t *) rti;
    int                     num         = 0;
    int                     first       = -1;
    int                     t           = -1;
    uint8_t                 ret         = 1;
    uint32_t                temp;

    cdrom_log(dev->log, "Read DISC Info TOC Type = %d, track = %d\n", type, track);

    dev->inv_field = track;
    dev->ops->get_raw_track_info(dev->local, &num, rti);

    switch (type) {
        case 0:
            if (num > 0) {
                first = find_track(trti, num, 1);
                const int last = find_track(trti, num, 0);

                if ((first == -1) || (last == -1))
                    ret = 0;
                else {
                    b[0] = bin2bcd(first);
                    b[1] = bin2bcd(last);
                    b[2] = 0x00;
                    b[3] = 0x00;

                    cdrom_log(dev->log, "Returned Toshiba/NEC disc information (type 0) "
                              "at %02i:%02i\n", b[0], b[1]);
                }
            } else
                ret = 0;
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

                cdrom_log(dev->log, "Returned Toshiba/NEC disc information (type 1) at "
                          "%02i:%02i.%02i\n", b[0], b[1], b[2]);
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
 
                cdrom_log(dev->log, "Returned Toshiba/NEC disc information (type 2) at "
                          "%02i:%02i.%02i, track=%d, attr=%02x.\n", b[0], b[1],
                          b[2], bcd2bin(track), b[3]);
            }
            break;
        case 3: /* Undocumented on NEC CD-ROM's, from information based on sr_vendor.c from the Linux kernel */
            if (dev->is_nec) {
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
            } else {
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
            }
            break;
        default:
            break;
    }

    return ret;
}

int
cdrom_readsector_raw(cdrom_t *dev, uint8_t *buffer, const int sector, const int ismsf,
                     int cdrom_sector_type, const int cdrom_sector_flags,
                     int *len, const uint8_t vendor_type)
{
    int      pos    = sector;
    int      ret    = 0;

    if ((cdrom_sector_type & 0x0f) >= 0x08) {
        mult               = cdrom_sector_type >> 4;
        cdrom_sector_type &= 0x0f;
        part               = pos % mult;
        pos               /= mult;
        ecc_diff           = (cdrom_sector_type & 0x01) ? 4 : 0;
    } else {
        mult               = 1;
        part               = 0;
        ecc_diff           = 0;
    }

    if (dev->cd_status != CD_STATUS_EMPTY) {
        uint8_t *temp_b;
        uint8_t *b      = temp_b = buffer;
        int      audio  = 0;
        uint32_t lba;
        int      mode2  = 0;

        *len = 0;

        if (ismsf) {
            const int m = (pos >> 16) & 0xff;
            const int s = (pos >> 8) & 0xff;
            const int f = pos & 0xff;

            lba = MSFtoLBA(m, s, f) - 150;
        } else {
            switch (vendor_type) {
                case 0x00:
                    lba = pos;
                    break;
                case 0x40: {
                    const int m = bcd2bin((pos >> 24) & 0xff);
                    const int s = bcd2bin((pos >> 16) & 0xff);
                    const int f = bcd2bin((pos >> 8) & 0xff);

                    lba = MSFtoLBA(m, s, f) - 150;
                    break;
                } case 0x80:
                    lba = bcd2bin((pos >> 24) & 0xff);
                    break;
                /* Never used values but the compiler complains. */
                default:
                    lba = 0;
            }
        }

        if (dev->ops->get_track_type)
            audio = dev->ops->get_track_type(dev->local, lba);

        const int dm  = audio & CD_TRACK_MODE_MASK;
        audio        &= CD_TRACK_AUDIO;

        if (dm != CD_TRACK_NORMAL)
            mode2 = 1;

        memset(dev->raw_buffer, 0, 2448);
        memset(dev->extra_buffer, 0, 296);

        if ((cdrom_sector_flags & 0xf8) == 0x08) {
            /* 0x08 is an illegal mode */
            cdrom_log(dev->log, "[Mode 1] 0x08 is an illegal mode\n");
        } else if ((cdrom_sector_type > 5) && (cdrom_sector_type < 8)) {
            cdrom_log(dev->log, "Attempting to read an unrecognized sector "
                      "type from an image\n");
            return 0;
        } else {
            if ((cdrom_sector_type > 1) && audio &&
                (dev->cd_status & CD_STATUS_HAS_AUDIO)) {
                cdrom_log(dev->log, "[%s] Attempting to read a data sector "
                          "from an audio track\n",
                          cdrom_req_modes[cdrom_sector_type]);
            } else if ((cdrom_sector_type == 1) &&
                       (!audio || !(dev->cd_status & CD_STATUS_HAS_AUDIO))) {
                cdrom_log(dev->log, "[Audio] Attempting to read an audio "
                          "sector from a data track\n");
            } else if (audio) {
                if (!track_type_is_valid(dev, cdrom_sector_type,
                                         cdrom_sector_flags, 1, 0x00))
                    ret = 0;
                else
                    ret = read_audio(dev, lba, temp_b);
            } else {
                ret = read_data(dev, lba);

                /* Return with error if we had one. */
                if (ret > 0) {
                    int form = 0;

                    if ((dev->raw_buffer[0x000f] == 0x00) ||
                        (dev->raw_buffer[0x000f] > 0x02)) {
                        cdrom_log(dev->log, "[%s] Unknown mode: %02X\n",
                                  cdrom_req_modes[cdrom_sector_type],
                                  dev->raw_buffer[0x000f]);
                        ret = 0;
                    } else if (mode2) {
                        if (dev->raw_buffer[0x000f] == 0x01)
                            /*
                               Use Mode 1, since evidently specification-violating
                               discs exist.
                             */
                            mode2 = 0;
                        else if (dev->raw_buffer[0x0012] !=
                                 dev->raw_buffer[0x0016]) {
                            cdrom_log(dev->log, "[%s] XA Mode 2 sector with "
                                      "malformed sub-header\n",
                                      cdrom_req_modes[cdrom_sector_type]);
                            ret = 0;
                        } else
                            form = ((dev->raw_buffer[0x0012] & 0x20) >> 5) + 1;
                    } else if (dev->raw_buffer[0x000f] == 0x02)
                        mode2 = 1;

                    if (ret > 0) {
                        const int mode_id = mode2 + form;

                        cdrom_log(dev->log, "[%s] %s detected\n",
                                  cdrom_req_modes[cdrom_sector_type],
                                  cdrom_modes[mode_id]);

                        if (!track_type_is_valid(dev, cdrom_sector_type,
                                                 cdrom_sector_flags, 0,
                                                 (mode2 << 2) + form)) {
                            cdrom_log(dev->log, "[%s] Invalid track type\n",
                                      cdrom_req_modes[cdrom_sector_type]);
                            ret = 0;
                        } else if (cdrom_mode_masks[cdrom_sector_type] &
                                   (1 << mode_id))
                            cdrom_process_data[mode_id](dev, cdrom_sector_flags,
                                                        temp_b);
                        else {
                            cdrom_log(dev->log, "[%s] Attempting to read a "
                                      "%s sector\n",
                                      cdrom_req_modes[cdrom_sector_type],
                                      cdrom_modes[mode_id]);
                            ret = 0;
                        }
                    }
                }
            }

            if (ret > 0) {
                process_ecc_and_subch(dev, cdrom_sector_flags, b);
                *len = dev->cdrom_sector_size;
            }
        }
    }

    return ret;
}

/*
   Read DVD Structure

   Yes, +2 instead of +4 is correct, I have verified this via Windows IOCTL, and it also matches
   the MMC specification.
 */
int
cdrom_read_dvd_structure(const cdrom_t *dev, const uint8_t layer, const uint8_t format,
                         uint8_t *buffer, uint32_t *info)
{
    int      max_layer       = 0;
    int      ret             = 0;
    uint64_t total_sectors;

    if (format < 0xc0) {
        if (dev->cd_status != CD_STATUS_DVD) {
            *info = format;
            ret   = -(SENSE_ILLEGAL_REQUEST << 16) | (ASC_INCOMPATIBLE_FORMAT << 8);
        } else if ((dev->ops != NULL) && (dev->ops->read_dvd_structure != NULL))
            ret   = dev->ops->read_dvd_structure(dev->local, layer, format, buffer, info);
    }

    if (ret == 0)  switch (format) {
        case 0x00:    /* Physical format information */
            total_sectors = (uint64_t) dev->cdrom_capacity;

            if (total_sectors > DVD_LAYER_0_SECTORS)
                max_layer++;

            if (layer > max_layer) {
                *info = layer;
                ret   = -(SENSE_ILLEGAL_REQUEST << 16) | (ASC_INV_FIELD_IN_CMD_PACKET << 8);
            } else {
                if (total_sectors == 0) {
                    *info = 0x00000000;
                    ret   = -(SENSE_NOT_READY << 16) | (ASC_MEDIUM_NOT_PRESENT << 8);
                } else {
                    buffer[4] = 0x01;    /* DVD-ROM, part version 1. */
                    buffer[5] = 0x0f;    /* 120mm disc, minimum rate unspecified .*/
                    if (max_layer == 1)
                        /* Two layers, OTP track path, read-only (per MMC-2 spec). */
                        buffer[6] = 0x31;
                    else
                        /* One layer, read-only (per MMC-2 spec). */
                        buffer[6] = 0x01;
                    buffer[7] = 0x10;    /* Default densities. */

                    /* Start sector. */
                    buffer[8]  = 0x00;
                    buffer[9]  = (0x030000 >> 16) & 0xff;
                    buffer[10] = (0x030000 >> 8) & 0xff;
                    buffer[11] = 0x030000 & 0xff;

                    /* End sector. */
                    buffer[12] = 0x00;
                    if (layer == 1) {
                        buffer[13] = ((total_sectors - DVD_LAYER_0_SECTORS) >> 16) & 0xff;
                        buffer[14] = ((total_sectors - DVD_LAYER_0_SECTORS) >> 8) & 0xff;
                        buffer[15] = (total_sectors - DVD_LAYER_0_SECTORS) & 0xff;
                    } else if (max_layer == 1) {
                        buffer[13] = (DVD_LAYER_0_SECTORS >> 16) & 0xff;
                        buffer[14] = (DVD_LAYER_0_SECTORS >> 8) & 0xff;
                        buffer[15] = DVD_LAYER_0_SECTORS & 0xff;
                    } else {
                        buffer[13] = (total_sectors >> 16) & 0xff;
                        buffer[14] = (total_sectors >> 8) & 0xff;
                        buffer[15] = total_sectors & 0xff;
                    }

                    /* Layer 0 end sector. */
                    buffer[16] = 0x00;
                    buffer[17] = (total_sectors >> 16) & 0xff;
                    buffer[18] = (total_sectors >> 8) & 0xff;
                    buffer[19] = total_sectors & 0xff;

                    buffer[20] = 0x00;    /* No BCA */

                    /* 2048 bytes of data + 2 byte header */
                    ret = (2048 + 2);
                }
            }
            break;

        case 0x01:    /* DVD copyright information */
            buffer[4] = 0;    /* No copyright data. */
            buffer[5] = 0;    /* No region restrictions. */

            /* 4 bytes of data + 2 byte header. */
            ret = (4 + 2);
            break;

        case 0x04:    /* DVD disc manufacturing information. */
            /* 2048 bytes of data + 2 byte header */
            ret = (2048 + 2);
            break;

        case 0xff:
            /*
             * This lists all the command capabilities above.  Add new ones
             * in order and update the length and buffer return values.
             */

            buffer[4]  = 0x00; /* Physical format */
            buffer[5]  = 0x40; /* Not writable, is readable */
            buffer[6]  = ((2048 + 4) >> 8) & 0xff;
            buffer[7]  = (2048 + 4) & 0xff;

            buffer[8]  = 0x01; /* Copyright info */
            buffer[9]  = 0x40; /* Not writable, is readable */
            buffer[10] = ((4 + 2) >> 8) & 0xff;
            buffer[11] = (4 + 2) & 0xff;

            buffer[12] = 0x03; /* BCA info */
            buffer[13] = 0x40; /* Not writable, is readable */
            buffer[14] = ((188 + 2) >> 8) & 0xff;
            buffer[15] = (188 + 2) & 0xff;

            buffer[16] = 0x04; /* Manufacturing info */
            buffer[17] = 0x40; /* Not writable, is readable */
            buffer[18] = ((2048 + 2) >> 8) & 0xff;
            buffer[19] = (2048 + 2) & 0xff;

            /* data written + 4 byte header */
            ret = (16 + 2);
            break;

        default:
            *info = format;
            ret   = -(SENSE_ILLEGAL_REQUEST << 16) | (ASC_INV_FIELD_IN_CMD_PACKET << 8);
            break;
    }

    return ret;
}

void
cdrom_read_disc_information(const cdrom_t *dev, uint8_t *buffer)
{
    uint8_t           rti[65536] = { 0 };
    raw_track_info_t *t          = (raw_track_info_t *) rti;
    int               num        = 0;
    int               first      = 0;
    int               sessions   = 0;
    int               ls_first   = 0;
    int               ls_last    = 0;
    int               t_b0       = -1;

    dev->ops->get_raw_track_info(dev->local, &num, rti);

    for (int i = 0; i < num; i++)
        if (t[i].session > sessions)
            sessions = t[i].session;
        else if ((first == 0) && (t[i].point >= 1) && (t[i].point <= 99))
            first    = t[i].point;

    for (int i = 0; i < num; i++)
        if ((t[i].session == sessions) && (t[i].point >= 1) && (t[i].point <= 99)) {
            ls_first = t[i].point;
            break;
        }

    for (int i = (num - 1); i >= 0; i--)
        if ((t[i].session == sessions) && (t[i].point >= 1) && (t[i].point <= 99)) {
            ls_last  = t[i].point;
            break;
        }

    for (int i = (num - 1); i >= 0; i--)
        if (t[i].point == 0xb0) {
            t_b0 = i;
            break;
        }

    memset(buffer, 0x00, 34);

    buffer[ 0] = 0x00;        /* Disc Information Length (MSB) */
    buffer[ 1] = 0x20;        /* Disc Information Lenght (LSB) */
    buffer[ 2] = 0x0e;        /* Last session complete, disc finalized */
    buffer[ 3] = first;       /* Number of First Track on Disc */
    buffer[ 4] = sessions;    /* Number of Sessions (LSB) */
    buffer[ 5] = ls_first;    /* First Track Number in Last Session (LSB) */
    buffer[ 5] = ls_last;     /* Last Track Number in Last Session (LSB) */
    buffer[ 7] = 0x20;        /* Unrestricted use */
    buffer[ 8] = t[0].ps;     /* Disc Type */
    buffer[ 9] = 0x00;        /* Number Of Sessions (MSB) */
    buffer[10] = 0x00;        /* First Track Number in Last Session (MSB) */
    buffer[11] = 0x00;        /* Last Track Number in Last Session (MSB) */

    if (t_b0 == -1) {
        /* Single-session disc.  */

        /* Last Session Lead-in Start Time MSF is 00:00:00 */

        /* Last Possible Start Time for Start of Lead-out */
        buffer[20] = t[2].pm;
        buffer[21] = t[2].ps;
        buffer[22] = t[2].pf;
    } else {
        /* Multi-session disc.  */

        /* Last Session Lead-in Start Time MSF */
        buffer[17] = t[t_b0].m;
        buffer[18] = t[t_b0].s;
        buffer[19] = t[t_b0].f;

        /* Last Possible Start Time for Start of Lead-out */
        buffer[20] = t[t_b0].pm;
        buffer[21] = t[t_b0].ps;
        buffer[22] = t[t_b0].pf;
    }
}

int
cdrom_read_track_information(cdrom_t *dev, const uint8_t *cdb, uint8_t *buffer)
{
    uint8_t                 rti[65536] = { 0 };
    const raw_track_info_t *t          = (raw_track_info_t *) rti;
    const raw_track_info_t *track      = NULL;
    const raw_track_info_t  lead_in    = { 0 };
    const uint32_t          pos        = (cdb[2] << 24) | (cdb[3] << 16) |
                                         (cdb[4] << 8) | cdb[5];
    uint32_t                real_pos   = pos;
    int                     num        = 0;
    int                     ret;

    dev->ops->get_raw_track_info(dev->local, &num, rti);

    switch (cdb[1] & 0x03) {
        default:
            ret = -cdb[1];
            break;
        case 0x00:
            if (num < 4)
                ret = -pos;
            else {
                for (int i = 0; i < num; i++) {
                     const raw_track_info_t *ct    = &(t[i]);
                     const uint32_t          start = ((ct->pm * 60 * 75) + (ct->ps * 75) +
                                                      ct->pf) - 150;
                     if (pos > start) {
                         track = ct;
                         break;
                     }
                }

                if (track == NULL)
                    ret = -cdb[1];
                else
                    ret = 36;
            }
            break;
        case 0x01:
            switch (pos) {
                default:
                    /*
                       TODO: Does READ TRACK INFORMATION use track AAh
                             or the raw A0h, A1h, and A2h?
                     */
                    if (pos == 0xaa)
                        real_pos = 0xa2;

                    for (int i = 0; i < num; i++) {
                        const raw_track_info_t *ct    = &(t[i]);
                        if (ct->point == real_pos) {
                            track = ct;
                            break;
                        }
                    }

                    if (track == NULL)
                        ret   = -pos;
                    else
                        ret   = 36;
                    break;
                case 0x00:
                    track = &lead_in;
                    ret   = 36;
                    break;
                case 0xff:
                    ret = -pos;
                    break;
            }
            break;
        case 0x02:
            for (int i = 0; i < num; i++) {
                 const raw_track_info_t *ct    = &(t[i]);
                 if ((ct->session == pos) && (ct->point >= 1) && (ct->point <= 99)) {
                     track = ct;
                     break;
                 }
             }

             if (track == NULL)
                 ret   = -pos;
             else
                 ret   = 36;
             break;
    }

    if (ret == 36) {
         uint32_t start = ((track->pm * 60 * 75) + (track->ps * 75) +
                           track->pf) - 150;
         uint32_t len   = 0x00000000;
         uint8_t  mode  = 0xf;

         memset(buffer, 0, 36);
         buffer[0] = 0x00;
         buffer[1] = 0x22;
         buffer[2] = track->point;             /* Track number (LSB). */
         buffer[3] = track->session;           /* Session number (LSB). */
         /* Not damaged, primary copy. */
         buffer[5] = track->adr_ctl & 0x04;

         if ((track->point >= 1) && (track->point >= 99)) {
             for (int i = 0; i < num; i++) {
                 const raw_track_info_t *ct = &(t[i]);
                 const uint32_t          ts = ((ct->pm * 60 * 75) + (ct->ps * 75) +
                                               ct->pf) - 150;
                 if ((ts > start) && ((ct->point == 0xa2) || ((ct->point >= 1) &&
                     (ct->point <= 99)))) {
                     len = ts - start;
                     break;
                 }
             }

             if (track->adr_ctl & 0x04) {
                 ret  = read_data(dev, start);
                 mode = dev->raw_buffer[3];
             }
         } else if (track->point != 0xa2)
             start = 0x00000000;

         /* Not reserved track, not blank, not packet writing, not fixed packet. */
         buffer[ 6] = mode << 0;
         /* Last recorded address not valid, next recordable address not valid. */
         buffer[ 7] = 0x00;

         buffer[ 8] = (start >> 24) & 0xff;
         buffer[ 9] = (start >> 16) & 0xff;
         buffer[10] = (start >> 8) & 0xff;
         buffer[11] = start & 0xff;

         buffer[24] = (len >> 24) & 0xff;
         buffer[25] = (len >> 16) & 0xff;
         buffer[26] = (len >> 8) & 0xff;
         buffer[27] = len & 0xff;
    }

    return ret;
}

int
cdrom_is_empty(const uint8_t id)
{
    const cdrom_t *dev = &cdrom[id];
    int            ret = 0;

    /* This entire block should be in cdrom.c/cdrom_eject(dev*) ... */
    if (strlen(dev->image_path) == 0)
        /* Switch from empty to empty. Do nothing. */
        ret = 1;

    return ret;
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
    cdrom_log(dev->log, "Written TOC of %i bytes to %s\n", len, fn2);

    memset(b, 0x00, 65536);
    len      = cdrom_read_toc(dev, b, CD_TOC_NORMAL, 0, 0, 65536);
    fn2      = "d:\\86boxnew\\toc_cue_cooked.dmp";
    f        = fopen(fn2, "wb");
    fwrite(b, 1, len, f);
    fflush(f);
    fclose(f);
    cdrom_log(dev->log, "Written cooked TOC of %i bytes to %s\n", len, fn2);

    memset(b, 0x00, 65536);
    len      = cdrom_read_toc(dev, b, CD_TOC_SESSION, 0, 0, 65536);
    fn2      = "d:\\86boxnew\\toc_cue_session.dmp";
    f        = fopen(fn2, "wb");
    fwrite(b, 1, len, f);
    fflush(f);
    fclose(f);
    cdrom_log(dev->log, "Written session TOC of %i bytes to %s\n", len, fn2);
}
#endif

void
cdrom_set_empty(cdrom_t *dev)
{
    dev->cd_status      = CD_STATUS_EMPTY;
}

void
cdrom_update_status(cdrom_t *dev)
{
    const int  was_empty = (dev->cd_status == CD_STATUS_EMPTY);

    if (dev->ops->load != NULL)
        dev->ops->load(dev->local);

    /* All good, reset state. */
    dev->seek_pos       = 0;
    dev->cd_buflen      = 0;

    if ((dev->ops->is_empty != NULL) && dev->ops->is_empty(dev->local))
        dev->cd_status      = CD_STATUS_EMPTY;
    else if (dev->ops->is_dvd(dev->local))
        dev->cd_status      = CD_STATUS_DVD;
    else
        dev->cd_status      = dev->ops->has_audio(dev->local) ? CD_STATUS_STOPPED :
                                                                CD_STATUS_DATA_ONLY;

    dev->cdrom_capacity = dev->ops->get_last_block(dev->local);

    if (dev->cd_status != CD_STATUS_EMPTY) {
        /* Signal media change to the emulated machine. */
        cdrom_insert(dev->id);

        /* The drive was previously empty, transition directly to UNIT ATTENTION. */
        if (was_empty)
            cdrom_insert(dev->id);
    }
}

int
cdrom_load(cdrom_t *dev, const char *fn, const int skip_insert)
{
    const int  was_empty = cdrom_is_empty(dev->id);
    int        ret       = 0;

    /* Make sure to not STRCPY if the two are pointing
       at the same place. */
    if (fn != dev->image_path)
        strcpy(dev->image_path, fn);

    /* Open the target. */
    if ((strlen(dev->image_path) != 0) &&
        (strstr(dev->image_path, "ioctl://") == dev->image_path))
        dev->local = ioctl_open(dev, dev->image_path);
    else
        dev->local = image_open(dev, dev->image_path);

    if (dev->local == NULL) {
        dev->ops           = NULL;
        dev->image_path[0] = 0;

        ret = 1;
    } else {
        /* All good, reset state. */
        dev->seek_pos       = 0;
        dev->cd_buflen      = 0;

        if ((dev->ops->is_empty != NULL) && dev->ops->is_empty(dev->local))
            dev->cd_status      = CD_STATUS_EMPTY;
        if (dev->ops->is_dvd(dev->local))
            dev->cd_status      = CD_STATUS_DVD;
        else
            dev->cd_status      = dev->ops->has_audio(dev->local) ? CD_STATUS_STOPPED :
                                                                    CD_STATUS_DATA_ONLY;

        dev->cdrom_capacity = dev->ops->get_last_block(dev->local);

        cdrom_log(dev->log, "CD-ROM capacity: %i sectors (%" PRIi64 " bytes)\n",
                  dev->cdrom_capacity, ((uint64_t) dev->cdrom_capacity) << 11ULL);
    }

#ifdef ENABLE_CDROM_LOG
    cdrom_toc_dump(dev);
#endif

    if (!skip_insert && (dev->cd_status != CD_STATUS_EMPTY)) {
        /* Signal media change to the emulated machine. */
        cdrom_insert(dev->id);

        /* The drive was previously empty, transition directly to UNIT ATTENTION. */
        if (was_empty)
            cdrom_insert(dev->id);
    }

    return ret;
}

/* Peform a master init on the entire module. */
void
cdrom_global_init(void)
{
    /* Clear the global data. */
    memset(cdrom, 0x00, sizeof(cdrom));
}

void
cdrom_hard_reset(void)
{
    for (uint8_t i = 0; i < CDROM_NUM; i++) {
        cdrom_t *dev = &cdrom[i];

        if (dev->bus_type) {
             dev->id       = i;

            dev->is_early = cdrom_is_early(dev->type);
            dev->is_nec   = (dev->bus_type == CDROM_BUS_SCSI) &&
                            !strcmp(cdrom_drive_types[dev->type].vendor, "NEC");

            cdrom_drive_reset(dev);

            char n[1024] = { 0 };

            sprintf(n, "CD-ROM %i      ", i + 1);
            dev->log = log_open(n);

            cdrom_log(dev->log, "Hard reset\n");

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

                cdrom_load(dev, dev->image_path, 0);
            }
        }
    }

    sound_cd_thread_reset();
}

void
cdrom_close(void)
{
    for (uint8_t i = 0; i < CDROM_NUM; i++) {
        cdrom_t *dev = &cdrom[i];

        if (dev->bus_type == CDROM_BUS_SCSI)
            memset(&scsi_devices[dev->scsi_device_id], 0x00, sizeof(scsi_device_t));

        if (dev->close)
            dev->close(dev->priv);

        cdrom_unload(dev);

        dev->ops  = NULL;
        dev->priv = NULL;

        cdrom_drive_reset(dev);

        if (dev->log != NULL) {
            cdrom_log(dev->log, "Log closed\n");

            log_close(dev->log);
            dev->log = NULL;
        }
    }
}

/* Signal disc change to the emulated machine. */
void
cdrom_insert(const uint8_t id)
{
    const cdrom_t *dev = &cdrom[id];

    if (dev->bus_type && dev->insert)
        dev->insert(dev->priv);
}

void
cdrom_exit(const uint8_t id)
{
    cdrom_t *dev = &cdrom[id];

    strcpy(dev->prev_image_path, dev->image_path);

    if (dev->ops) {
        cdrom_unload(dev);

        dev->ops = NULL;
    }

    memset(dev->image_path, 0, sizeof(dev->image_path));

    cdrom_log(dev->log, "cdrom_exit(): cdrom_insert()\n");
    cdrom_insert(id);
}

/* The mechanics of ejecting a CD-ROM from a drive. */
void
cdrom_eject(const uint8_t id)
{
    const cdrom_t *dev = &cdrom[id];

    if (strlen(dev->image_path) != 0) {
        cdrom_exit(id);

        plat_cdrom_ui_update(id, 0);

        config_save();
    }
}

/* The mechanics of re-loading a CD-ROM drive. */
void
cdrom_reload(const uint8_t id)
{
    cdrom_t   *dev       = &cdrom[id];

    if ((strcmp(dev->image_path, dev->prev_image_path) == 0) || (strlen(dev->prev_image_path) == 0) || (strlen(dev->image_path) > 0)) {
        /* Switch from empty to empty. Do nothing. */
        return;
    }

    cdrom_unload(dev);

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

        cdrom_load(dev, dev->image_path, 0);
    }

    plat_cdrom_ui_update(id, 1);

    config_save();
}
