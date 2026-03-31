/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Dummy floppy IOCTL implementation for platforms without
 *          physical floppy device support.
 *
 * Authors: Tiago Gasiba <tiga@FreeBSD.org>
 *
 *          Copyright 2026 Tiago Gasiba.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/plat_floppy_ioctl.h>

void
fdd_set_host_device(int drive, const char *path)
{
}

const char *
fdd_get_host_device(int drive)
{
    return "";
}

int
floppy_ioctl_open(int drive, int *out_tracks, int *out_sides, int *out_sectors, int *out_rate)
{
    return 0;
}

void
floppy_ioctl_close(int drive)
{
}

int
floppy_ioctl_read_sector(int drive, int track, int side, int sector, uint8_t *buffer)
{
    return 0;
}

int
floppy_ioctl_write_sector(int drive, int track, int side, int sector, const uint8_t *buffer)
{
    return 0;
}

void
floppy_ioctl_set_buffering(int enabled)
{
}

int
floppy_ioctl_get_buffering(void)
{
    return 0;
}