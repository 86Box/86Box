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
#ifdef ENABLE_IOCTL_LOG
#include <stdarg.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/log.h>
#include <86box/plat_unused.h>
#include <86box/plat_cdrom_ioctl.h>

/* The addresses sent from the guest are absolute, ie. a LBA of 0 corresponds to a MSF of 00:00:00. Otherwise, the counter displayed by the guest is wrong:
   there is a seeming 2 seconds in which audio plays but counter does not move, while a data track before audio jumps to 2 seconds before the actual start
   of the audio while audio still plays. With an absolute conversion, the counter is fine. */
#define MSFtoLBA(m, s, f) ((((m * 60) + s) * 75) + f)

typedef struct ioctl_t {
    cdrom_t                *dev;
    void                   *log;
    void                   *handle;
    char                    path[256];
} ioctl_t;

#ifdef ENABLE_IOCTL_LOG
int ioctl_do_log = ENABLE_IOCTL_LOG;

void
ioctl_log(void *priv, const char *fmt, ...)
{
    if (ioctl_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define ioctl_log(priv, fmt, ...)
#endif

/* Internal functions. */
static void
ioctl_close_handle(UNUSED(const ioctl_t *ioctl))
{
}

static int
ioctl_open_handle(UNUSED(ioctl_t *ioctl))
{
    return 0;
}

static void
ioctl_read_toc(ioctl_t *ioctl)
{
}

/* Shared functions. */
static int
ioctl_get_track_info(UNUSED(const void *local), UNUSED(const uint32_t track),
                     UNUSED(int end), UNUSED(track_info_t *ti))
{
    return 0;
}

static void
ioctl_get_raw_track_info(UNUSED(const void *local), int *num, uint8_t *rti)
{
    *num = 1;
    memset(rti, 0x00, 11);
}

static int
ioctl_is_track_pre(const void *local, UNUSED(const uint32_t sector))
{
    ioctl_t *ioctl = (ioctl_t *) local;

    ioctl_read_toc(ioctl);

    const int ret = 0;

    ioctl_log("ioctl_is_track_audio(%08X): %i\n", sector, ret);

    return ret;
}

static int
ioctl_read_sector(const void *local, UNUSED(uint8_t *buffer), UNUSED(uint32_t const sector))
{
    ioctl_t *ioctl = (ioctl_t *) local;

    ioctl_open_handle(ioctl);

    ioctl_close_handle(ioctl);

    ioctl_log("ReadSector sector=%d.\n", sector);

    return 0;
}

static uint8_t
ioctl_get_track_type(UNUSED(const void *local), UNUSED(const uint32_t sector))
{
    return 0x00;
}

static uint32_t
ioctl_get_last_block(const void *local)
{
    ioctl_t *ioctl = (ioctl_t *) local;

    ioctl_read_toc(ioctl);

    return 0x00000000;
}

static int
ioctl_read_dvd_structure(UNUSED(const void *local), UNUSED(const uint8_t layer), UNUSED(const uint8_t format),
                         UNUSED(uint8_t *buffer), UNUSED(uint32_t *info))
{
    return -0x00052100;
}

static int
ioctl_is_dvd(UNUSED(const void *local))
{
    return 0;
}

static int
ioctl_has_audio(UNUSED(const void *local))
{
    return 0;
}

static int
ioctl_is_empty(const void *local)
{
    return 1;
}

static int
ioctl_ext_medium_changed(UNUSED(void *local))
{
#if 0
    ioctl_t *ioctl = (ioctl_t *) local;
#endif
    int                  ret   = 0;

    ioctl_log("ioctl_ext_medium_changed(): %i\n", ret);

    return ret;
}

static void
ioctl_close(void *local)
{
    ioctl_t *ioctl = (ioctl_t *) local;

    ioctl_close_handle(ioctl);
    ioctl->handle = NULL;

    ioctl_log(ioctl->log, "Log closed\n");

    log_close(ioctl->log);
    ioctl->log = NULL;
}

static void
ioctl_load(const void *local)
{
    const ioctl_t *ioctl = (const ioctl_t *) local;

    if (ioctl_open_handle((ioctl_t *) ioctl)) {
        ioctl_close_handle((ioctl_t *) ioctl);

        ioctl_read_toc((ioctl_t *) ioctl);
    }
}

static const cdrom_ops_t ioctl_ops = {
    ioctl_get_track_info,
    ioctl_get_raw_track_info,
    ioctl_is_track_pre,
    ioctl_read_sector,
    ioctl_get_track_type,
    ioctl_get_last_block,
    ioctl_read_dvd_structure,
    ioctl_is_dvd,
    ioctl_has_audio,
    ioctl_is_empty,
    ioctl_close,
    ioctl_load
};

/* Public functions. */
void *
ioctl_open(cdrom_t *dev, const char *drv)
{
    ioctl_t *ioctl = (ioctl_t *) calloc(1, sizeof(ioctl_t));

    if (ioctl != NULL) {
        char n[1024]        = { 0 };

        sprintf(n, "CD-ROM %i IOCtl", dev->id + 1);
        ioctl->log          = log_open(n);

        memset(ioctl->path, 0x00, sizeof(ioctl->path));

        sprintf(ioctl->path, "%s", drv);
        ioctl_log(ioctl->log, "Path is %s\n", ioctl->path);

        ioctl->dev          = dev;

        dev->ops            = &ioctl_ops;
    }

    return ioctl;
}
