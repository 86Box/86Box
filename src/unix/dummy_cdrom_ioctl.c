/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Win32 CD-ROM support via IOCTL.
 *
 *
 *
 * Authors: TheCollector1995, <mariogplayer@gmail.com>,
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2023 TheCollector1995.
 *          Copyright 2023 Miran Grca.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/plat_unused.h>
#include <86box/plat_cdrom.h>

/* The addresses sent from the guest are absolute, ie. a LBA of 0 corresponds to a MSF of 00:00:00. Otherwise, the counter displayed by the guest is wrong:
   there is a seeming 2 seconds in which audio plays but counter does not move, while a data track before audio jumps to 2 seconds before the actual start
   of the audio while audio still plays. With an absolute conversion, the counter is fine. */
#define MSFtoLBA(m, s, f) ((((m * 60) + s) * 75) + f)

typedef struct dummy_cdrom_ioctl_t {
    int                     toc_valid;
} dummy_cdrom_ioctl_t;

#ifdef ENABLE_DUMMY_CDROM_IOCTL_LOG
int dummy_cdrom_ioctl_do_log = ENABLE_DUMMY_CDROM_IOCTL_LOG;

void
dummy_cdrom_ioctl_log(const char *fmt, ...)
{
    va_list ap;

    if (dummy_cdrom_ioctl_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define dummy_cdrom_ioctl_log(fmt, ...)
#endif

static int
plat_cdrom_open(void *local)
{
    return 0;
}

static int
plat_cdrom_load(void *local)
{
    return 0;
}

static void
plat_cdrom_read_toc(void *local)
{
    dummy_cdrom_ioctl_t *ioctl = (dummy_cdrom_ioctl_t *) local;

    if (!ioctl->toc_valid)
        ioctl->toc_valid = 1;
}

void
plat_cdrom_get_raw_track_info(UNUSED(void *local), int *num, raw_track_info_t *rti)
{
    *num = 1;
    memset(rti, 0x00, 11);
}

int
plat_cdrom_is_track_audio(void *local, uint32_t sector)
{
    dummy_cdrom_ioctl_t *ioctl = (dummy_cdrom_ioctl_t *) local;

    plat_cdrom_read_toc(ioctl);

    const int            ret   = 0;

    dummy_cdrom_ioctl_log("plat_cdrom_is_track_audio(%08X): %i\n", sector, ret);

    return ret;
}

int
plat_cdrom_is_track_pre(void *local, uint32_t sector)
{
    dummy_cdrom_ioctl_t *ioctl = (dummy_cdrom_ioctl_t *) local;

    plat_cdrom_read_toc(ioctl);

    const int ret = 0;

    dummy_cdrom_ioctl_log("plat_cdrom_is_track_audio(%08X): %i\n", sector, ret);

    return ret;
}

uint32_t
plat_cdrom_get_track_start(void *local, uint32_t sector, uint8_t *attr, uint8_t *track)
{
    dummy_cdrom_ioctl_t *ioctl = (dummy_cdrom_ioctl_t *) local;

    plat_cdrom_read_toc(ioctl);

    return 0x00000000;
}

uint32_t
plat_cdrom_get_last_block(void *local)
{
    dummy_cdrom_ioctl_t *ioctl = (dummy_cdrom_ioctl_t *) local;

    plat_cdrom_read_toc(ioctl);

    return 0x00000000;
}

int
plat_cdrom_ext_medium_changed(void *local)
{
    dummy_cdrom_ioctl_t *ioctl = (dummy_cdrom_ioctl_t *) local;
    int                  ret   = 0;

    dummy_cdrom_ioctl_log("plat_cdrom_ext_medium_changed(): %i\n", ret);

    return ret;
}

/* This replaces both Info and EndInfo, they are specified by a variable. */
int
plat_cdrom_get_audio_track_info(void *local, UNUSED(int end), int track, int *track_num, TMSF *start, uint8_t *attr)
{
    dummy_cdrom_ioctl_t *ioctl = (dummy_cdrom_ioctl_t *) local;

    plat_cdrom_read_toc(ioctl);

    if ((track < 1) || (track == 0xaa)) {
        dummy_cdrom_ioctl_log("plat_cdrom_get_audio_track_info(%02i)\n", track);
        return 0;
    }

    start->min = 0;
    start->sec = 0;
    start->fr  = 2;

    *track_num = 1;
    *attr      = 0x14;

    dummy_cdrom_ioctl_log("plat_cdrom_get_audio_track_info(%02i): %02i:%02i:%02i, %02i, %02X\n",
                          track, start->min, start->sec, start->fr, *track_num, *attr);

    return 1;
}

/* TODO: See if track start is adjusted by 150 or not. */
int
plat_cdrom_get_audio_sub(UNUSED(void *local), UNUSED(uint32_t sector), uint8_t *attr, uint8_t *track, uint8_t *index,
                         TMSF *rel_pos, TMSF *abs_pos)
{
    *track = 1;
    *attr = 0x14;
    *index = 1;

    rel_pos->min = 0;
    rel_pos->sec = 0;
    rel_pos->fr  = 0;
    abs_pos->min = 0;
    abs_pos->sec = 0;
    abs_pos->fr  = 2;

    dummy_cdrom_ioctl_log("plat_cdrom_get_audio_sub(): %02i, %02X, %02i, %02i:%02i:%02i, %02i:%02i:%02i\n",
                          *track, *attr, *index, rel_pos->min, rel_pos->sec, rel_pos->fr, abs_pos->min, abs_pos->sec, abs_pos->fr);

    return 1;
}

int
plat_cdrom_get_sector_size(UNUSED(void *local), UNUSED(uint32_t sector))
{
    dummy_cdrom_ioctl_log("BytesPerSector=2048\n");

    return 2048;
}

int
plat_cdrom_read_sector(void *local, uint8_t *buffer, uint32_t sector)
{
    dummy_cdrom_ioctl_t *ioctl = (dummy_cdrom_ioctl_t *) local;

    plat_cdrom_open(ioctl);

    /* Raw */
    dummy_cdrom_ioctl_log("Raw\n");

    plat_cdrom_close(ioctl);

    dummy_cdrom_ioctl_log("ReadSector sector=%d.\n", sector);

    return 0;
}

void
plat_cdrom_eject(void *local)
{
    dummy_cdrom_ioctl_t *ioctl = (dummy_cdrom_ioctl_t *) local;

    plat_cdrom_open(ioctl);
    plat_cdrom_close(ioctl);
}

void
plat_cdrom_close(UNUSED(void *local))
{
}

int
plat_cdrom_set_drive(void *local, const char *drv)
{
    dummy_cdrom_ioctl_t *ioctl = (dummy_cdrom_ioctl_t *) local;

    plat_cdrom_close(ioctl);

    ioctl->toc_valid = 0;

    plat_cdrom_load(ioctl);

    return 1;
}

int
plat_cdrom_get_local_size(void)
{
    return sizeof(dummy_cdrom_ioctl_t);
}
