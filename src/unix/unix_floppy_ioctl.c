/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Host USB floppy support via block device I/O.
 *
 *          This module provides host floppy device support with:
 *          - Device size/geometry detection
 *          - Read/write on sector-level
 *          - Read caching with prefetch on tracks
 *
 * Authors: Tiago Gasiba <tiga@FreeBSD.org>
 *
 *          Copyright 2026 Tiago Gasiba.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#if defined(__FreeBSD__)
#include <sys/disk.h>
#elif defined(__APPLE__)
#include <sys/disk.h>
#endif

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/fdd.h>
#include <86box/plat_floppy_ioctl.h>

#define FLOPPY_IOCTL_DEBUG 0
#define LOG_PREFIX "[FLOPPY_IOCTL] "

#if FLOPPY_IOCTL_DEBUG
#define floppy_ioctl_log(...) do { \
    fprintf(stderr, LOG_PREFIX); \
    fprintf(stderr, __VA_ARGS__); \
    fflush(stderr); \
} while(0)
#else
#define floppy_ioctl_log(...)
#endif

#define FLOPPY_SIZE_2880KB  2949120  /* 2.88MB ED */
#define FLOPPY_SIZE_1440KB  1474560  /* 1.44MB HD */
#define FLOPPY_SIZE_1200KB  1228800  /* 1.2MB  HD 5.25" */
#define FLOPPY_SIZE_720KB   737280   /* 720KB  DD */
#define FLOPPY_SIZE_360KB   368640   /* 360KB  DD 5.25" */
#define FLOPPY_SIZE_320KB   327680   /* 320KB  DD */
#define FLOPPY_SIZE_180KB   184320   /* 180KB  SD 5.25" */
#define FLOPPY_SIZE_160KB   163840   /* 160KB  SD */

#define SECTOR_SIZE 512

/* Note: we keep the state of the actual disk that is on the drive,
   which might be different from the configuration in the VM,
   e.g. when putting a 720KB disk in a drive configured for 1.44MB.
*/
typedef struct floppy_ioctl_state_t {
    int      fd;              /* File descriptor (-1 if closed) */
    int      tracks;          /* Geometry */
    int      sides;
    int      sectors;
    int      rate;
    int      readonly;        /* Is the device read-only? */
    uint8_t *buffer;          /* Cached disk image */
    uint8_t *sector_valid;    /* 1 = sector cached, 0 = not cached */
    char     host_device[256];
} floppy_ioctl_state_t;

static floppy_ioctl_state_t floppy_state[FDD_NUM];
static int floppy_buffering_enabled = 1;

void
fdd_set_host_device(int drive, const char *path)
{
    if (drive < 0 || drive >= FDD_NUM)
        return;
    if (path) {
        strncpy(floppy_state[drive].host_device, path, sizeof(floppy_state[drive].host_device) - 1);
        floppy_state[drive].host_device[sizeof(floppy_state[drive].host_device) - 1] = '\0';
        floppy_ioctl_log("fdd_set_host_device(%d, \"%s\")\n", drive, path);
    } else {
        floppy_state[drive].host_device[0] = '\0';
        floppy_ioctl_log("fdd_set_host_device(%d, NULL)\n", drive);
    }
}

const char *
fdd_get_host_device(int drive)
{
    if (drive < 0 || drive >= FDD_NUM)
        return "";
    return floppy_state[drive].host_device;
}

void
floppy_ioctl_set_buffering(int enabled)
{
    floppy_buffering_enabled = enabled ? 1 : 0;
    floppy_ioctl_log("floppy_ioctl_set_buffering(%d)\n", floppy_buffering_enabled);
}

int
floppy_ioctl_get_buffering(void)
{
    return floppy_buffering_enabled;
}

static int64_t
get_device_size(int fd)
{
    int64_t size = -1;

#if defined(__FreeBSD__)
    off_t mediasize;
    if (ioctl(fd, DIOCGMEDIASIZE, &mediasize) == 0) {
        size = (int64_t)mediasize;
        floppy_ioctl_log("get_device_size: FreeBSD DIOCGMEDIASIZE = %lld\n", (long long)size);
    } else {
        floppy_ioctl_log("get_device_size: FreeBSD DIOCGMEDIASIZE failed: %s\n", strerror(errno));
    }
#elif defined(__APPLE__)
    uint64_t blockcount = 0;
    uint32_t blocksize = 0;
    int r1 = ioctl(fd, DKIOCGETBLOCKCOUNT, &blockcount);
    int r2 = ioctl(fd, DKIOCGETBLOCKSIZE, &blocksize);
    if (r1 == 0 && r2 == 0) {
        size = (int64_t)blockcount * blocksize;
        floppy_ioctl_log("get_device_size: macOS blockcount=%llu, blocksize=%u, total=%lld\n",
                         (unsigned long long)blockcount, blocksize, (long long)size);
    } else {
        floppy_ioctl_log("get_device_size: macOS DKIOCGETBLOCK* failed: r1=%d r2=%d errno=%s\n",
                         r1, r2, strerror(errno));
    }
#endif

    return size;
}

static int
detect_geometry(int64_t size, int *tracks, int *sides, int *sectors, int *rate)
{
    static const struct {
        int64_t size;
        int tracks;
        int sides;
        int sectors;
        int rate;
    } geometries[] = {
        { FLOPPY_SIZE_2880KB, 80, 2, 36, 3 },
        { FLOPPY_SIZE_1440KB, 80, 2, 18, 0 },
        { FLOPPY_SIZE_1200KB, 80, 2, 15, 0 },
        { FLOPPY_SIZE_720KB,  80, 2,  9, 2 },
        { FLOPPY_SIZE_360KB,  40, 2,  9, 1 },
        { FLOPPY_SIZE_320KB,  40, 2,  8, 1 },
        { FLOPPY_SIZE_180KB,  40, 1,  9, 1 },
        { FLOPPY_SIZE_160KB,  40, 1,  8, 1 },
        { 0, 0, 0, 0, 0 }
    };

    for (int i = 0; geometries[i].size != 0; i++) {
        if (size == geometries[i].size) {
            *tracks = geometries[i].tracks;
            *sides = geometries[i].sides;
            *sectors = geometries[i].sectors;
            *rate = geometries[i].rate;
            floppy_ioctl_log("detect_geometry: size=%lld (%dT/%dH/%dS rate=%d)\n",
                             (long long)size, *tracks, *sides, *sectors, *rate);
            return 1;
        }
    }

    floppy_ioctl_log("detect_geometry: size=%lld NO MATCH\n", (long long)size);
    return 0;
}

int
floppy_ioctl_open(int drive, int *out_tracks, int *out_sides, int *out_sectors, int *out_rate)
{
    floppy_ioctl_state_t *state;
    const char *path;
    int fd;
    int64_t size;

    floppy_ioctl_log("floppy_ioctl_open(%d)\n", drive);

    if (drive < 0 || drive >= FDD_NUM) {
        floppy_ioctl_log("  invalid drive number\n");
        return 0;
    }

    state = &floppy_state[drive];
    path = floppy_state[drive].host_device;

    floppy_ioctl_close(drive);

    if (path[0] == '\0') {
        floppy_ioctl_log("  no host device configured\n");
        return 0;
    }

    floppy_ioctl_log("  opening device: %s\n", path);

    /* Try read-write */
    fd = open(path, O_RDWR | O_NONBLOCK | O_EXLOCK);
    if (fd < 0) {
        floppy_ioctl_log("  open(O_RDWR) failed: %s, trying O_RDONLY\n", strerror(errno));
        /* Try read-only */
        fd = open(path, O_RDONLY | O_NONBLOCK | O_EXLOCK);
        if (fd < 0) {
            fprintf(stderr, LOG_PREFIX "Failed to open %s: %s\n", path, strerror(errno));
            fprintf(stderr, LOG_PREFIX "Hint: device node may have changed after disk swap\n");
            return 0;
        }
        state->readonly = 1;
    } else {
        state->readonly = 0;
    }

    floppy_ioctl_log("  opened successfully, fd=%d, readonly=%d\n", fd, state->readonly);

    size = get_device_size(fd);
    if (size <= 0 || size > FLOPPY_SIZE_2880KB ||
        !detect_geometry(size, &state->tracks, &state->sides, &state->sectors, &state->rate)) {
        floppy_ioctl_log("  not a floppy\n");
        close(fd);
        return 0;
    }

    state->fd = fd;

    if (floppy_buffering_enabled) {
        int total_sectors = state->tracks * state->sides * state->sectors;
        state->buffer = (uint8_t *)calloc(1, size);
        state->sector_valid = (uint8_t *)calloc(1, total_sectors);
        if (state->buffer && state->sector_valid) {
            floppy_ioctl_log("  buffering enabled: allocated %lld bytes\n", (long long)size);
        } else {
            free(state->buffer);
            free(state->sector_valid);
            state->buffer = NULL;
            state->sector_valid = NULL;
            floppy_ioctl_log("  buffering allocation failed, continuing without buffer\n");
        }
    } else {
        state->buffer = NULL;
        state->sector_valid = NULL;
    }

    if (state->readonly) {
        writeprot[drive] = 1;
        fwriteprot[drive] = 1;
    }

    *out_tracks = state->tracks;
    *out_sides = state->sides;
    *out_sectors = state->sectors;
    *out_rate = state->rate;

    floppy_ioctl_log("  SUCCESS: fd=%d, size=%lld, geometry=%d/%d/%d, rate=%d, readonly=%d\n",
                     state->fd, (long long)size,
                     state->tracks, state->sides, state->sectors, state->rate, state->readonly);

    return 1;
}

void
floppy_ioctl_close(int drive)
{
    floppy_ioctl_state_t *state;

    floppy_ioctl_log("floppy_ioctl_close(%d)\n", drive);

    if (drive < 0 || drive >= FDD_NUM)
        return;

    state = &floppy_state[drive];

    if (state->fd >= 0) {
        floppy_ioctl_log("  closing fd=%d\n", state->fd);
        close(state->fd);
        state->fd = -1;
    }

    if (state->buffer) {
        free(state->buffer);
        state->buffer = NULL;
    }
    if (state->sector_valid) {
        free(state->sector_valid);
        state->sector_valid = NULL;
    }

    state->tracks = 0;
    state->sides = 0;
    state->sectors = 0;
}

int
floppy_ioctl_read_sector(int drive, int track, int side, int sector, uint8_t *buffer)
{
    floppy_ioctl_state_t *state;
    off_t offset;
    ssize_t ret;
    size_t bytes_read;
    int sector_index;
    int total_sectors;
    int sectors_to_read;
    int read_start;
    int i;

    if (drive < 0 || drive >= FDD_NUM)
        return 0;

    state = &floppy_state[drive];

    if (state->fd < 0) {
        floppy_ioctl_log("floppy_ioctl_read_sector(%d, %d, %d, %d): fd=%d FAIL\n",
                         drive, track, side, sector, state->fd);
        return 0;
    }

    if (track >= state->tracks || side >= state->sides ||
        sector < 1 || sector > state->sectors) {
        floppy_ioctl_log("floppy_ioctl_read_sector(%d, %d, %d, %d): geometry violation "
                         "(max %d/%d/%d) FAIL\n",
                         drive, track, side, sector,
                         state->tracks, state->sides, state->sectors);
        return 0;
    }

    sector_index = (track * state->sides + side) * state->sectors + (sector - 1);
    offset = (off_t)sector_index * SECTOR_SIZE;

    floppy_ioctl_log("floppy_ioctl_read_sector(%d, T%d/H%d/S%d): offset=%lld\n",
                     drive, track, side, sector, (long long)offset);

    if (state->buffer && state->sector_valid && state->sector_valid[sector_index]) {
        memcpy(buffer, state->buffer + offset, SECTOR_SIZE);
        floppy_ioctl_log("  read from cache OK\n");
        return 1;
    }

    total_sectors = state->tracks * state->sides * state->sectors;
    read_start = (track * state->sides + side) * state->sectors;
    sectors_to_read = state->sectors;

    if (read_start + sectors_to_read > total_sectors)
        sectors_to_read = total_sectors - read_start;

    if (state->buffer && state->sector_valid && sectors_to_read > 0) {
        off_t read_offset = (off_t)read_start * SECTOR_SIZE;
        size_t bytes_to_read = sectors_to_read * SECTOR_SIZE;
        bytes_read = 0;

        if (lseek(state->fd, read_offset, SEEK_SET) != read_offset) {
            floppy_ioctl_log("  lseek failed: %s\n", strerror(errno));
            return 0;
        }

        while (bytes_read < bytes_to_read) {
            ret = read(state->fd, state->buffer + read_offset + bytes_read,
                       bytes_to_read - bytes_read);
            if (ret < 0) {
                floppy_ioctl_log("  read error: %s\n", strerror(errno));
                break;
            }
            if (ret == 0) {
                floppy_ioctl_log("  unexpected EOF after %zu bytes\n", bytes_read);
                break;
            }
            bytes_read += ret;

            for (i = 0; i < (int)(bytes_read / SECTOR_SIZE); i++)
                state->sector_valid[read_start + i] = 1;

            if (state->sector_valid[sector_index])
                break;
        }

        floppy_ioctl_log("  track read: %zu bytes (%d sectors)\n",
                         bytes_read, (int)(bytes_read / SECTOR_SIZE));

        if (!state->sector_valid[sector_index]) {
            floppy_ioctl_log("  requested sector %d not readable\n", sector_index);
            return 0;
        }

        memcpy(buffer, state->buffer + offset, SECTOR_SIZE);
        return 1;
    }

    /* single sector read (no caching available) */
    bytes_read = 0;

    if (lseek(state->fd, offset, SEEK_SET) != offset) {
        floppy_ioctl_log("  lseek failed: %s\n", strerror(errno));
        return 0;
    }

    while (bytes_read < SECTOR_SIZE) {
        ret = read(state->fd, buffer + bytes_read, SECTOR_SIZE - bytes_read);
        if (ret < 0) {
            floppy_ioctl_log("  read error: %s\n", strerror(errno));
            return 0;
        }
        if (0 == ret) {
            floppy_ioctl_log("  unexpected EOF after %zu bytes\n", bytes_read);
            return 0;
        }
        bytes_read += ret;
    }

    floppy_ioctl_log("  read OK (unbuffered)\n");
    return 1;
}

int
floppy_ioctl_write_sector(int drive, int track, int side, int sector, const uint8_t *buffer)
{
    floppy_ioctl_state_t *state;
    off_t offset;
    ssize_t ret;
    int sector_index;

    if (drive < 0 || drive >= FDD_NUM)
        return 0;

    state = &floppy_state[drive];

    if (state->fd < 0 || state->readonly) {
        floppy_ioctl_log("floppy_ioctl_write_sector(%d, %d, %d, %d): "
                         "fd=%d readonly=%d FAIL\n",
                         drive, track, side, sector,
                         state->fd, state->readonly);
        return 0;
    }

    if (track >= state->tracks || side >= state->sides ||
        sector < 1 || sector > state->sectors) {
        floppy_ioctl_log("floppy_ioctl_write_sector(%d, %d, %d, %d): geometry violation FAIL\n",
                         drive, track, side, sector);
        return 0;
    }

    sector_index = (track * state->sides + side) * state->sectors + (sector - 1);
    offset = (off_t)sector_index * SECTOR_SIZE;

    floppy_ioctl_log("floppy_ioctl_write_sector(%d, T%d/H%d/S%d): offset=%lld\n",
                     drive, track, side, sector, (long long)offset);

    if (lseek(state->fd, offset, SEEK_SET) != offset) {
        floppy_ioctl_log("  lseek failed: %s\n", strerror(errno));
        return 0;
    }

    ret = write(state->fd, buffer, SECTOR_SIZE);
    if (ret != SECTOR_SIZE) {
        floppy_ioctl_log("  write returned %zd: %s\n", ret, ret < 0 ? strerror(errno) : "short write");
        return 0;
    }

    if (state->buffer && state->sector_valid) {
        memcpy(state->buffer + offset, buffer, SECTOR_SIZE);
        state->sector_valid[sector_index] = 1;
        floppy_ioctl_log("  write OK, cache updated\n");
    } else {
        floppy_ioctl_log("  write OK\n");
    }

    return 1;
}

void __attribute__((constructor))
floppy_ioctl_init(void)
{
    floppy_ioctl_log("floppy_ioctl_init()\n");
    for (int i = 0; i < FDD_NUM; i++) {
        floppy_state[i].fd = -1;
        floppy_state[i].buffer = NULL;
        floppy_state[i].sector_valid = NULL;
        floppy_state[i].host_device[0] = '\0';
    }
}