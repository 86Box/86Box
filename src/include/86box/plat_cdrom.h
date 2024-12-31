/*
 * 86Box        A hypervisor and IBM PC system emulator that specializes in
 *              running old operating systems and software designed for IBM
 *              PC systems and compatibles from 1981 through fairly recent
 *              system designs based on the PCI bus.
 *
 *              This file is part of the 86Box distribution.
 *
 *              Definitions for platform specific serial to host passthrough.
 *
 *
 * Authors:     Andreas J. Reichel <webmaster@6th-dimension.com>,
 *              Jasmine Iwanek <jasmine@iwanek.co.uk>
 *
 *              Copyright 2021      Andreas J. Reichel.
 *              Copyright 2021-2022 Jasmine Iwanek.
 */

#ifndef PLAT_CDROM_H
#define PLAT_CDROM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RAW_SECTOR_SIZE    2352
#define COOKED_SECTOR_SIZE 2048

#define DATA_TRACK         0x14
#define AUDIO_TRACK        0x10

#define CD_FPS             75
#define FRAMES_TO_MSF(f, M, S, F)                 \
    {                                             \
        uint64_t value = f;                       \
        *(F)           = (value % CD_FPS) & 0xff; \
        value /= CD_FPS;                          \
        *(S) = (value % 60) & 0xff;               \
        value /= 60;                              \
        *(M) = value & 0xff;                      \
    }
#define MSF_TO_FRAMES(M, S, F) ((M) *60 * CD_FPS + (S) *CD_FPS + (F))

typedef struct SMSF {
    uint16_t min;
    uint8_t  sec;
    uint8_t  fr;
} TMSF;

extern void     plat_cdrom_get_raw_track_info(void *local, int *num, raw_track_info_t *rti);
extern int      plat_cdrom_is_track_audio(void *local, uint32_t sector);
extern int      plat_cdrom_is_track_pre(void *local, uint32_t sector);
extern uint32_t plat_cdrom_get_last_block(void *local);
extern void     plat_cdrom_get_audio_tracks(void *local, int *st_track, int *end, TMSF *lead_out);
extern int      plat_cdrom_get_audio_track_info(void *local, int end, int track, int *track_num, TMSF *start,
                                                uint8_t *attr);
extern int      plat_cdrom_get_audio_sub(void *local, uint32_t sector, uint8_t *attr, uint8_t *track,
                                         uint8_t *index, TMSF *rel_pos, TMSF *abs_pos);
extern int      plat_cdrom_get_sector_size(void *local, uint32_t sector);
extern int      plat_cdrom_read_sector(void *local, uint8_t *buffer, uint32_t sector);
extern void     plat_cdrom_eject(void *local);
extern void     plat_cdrom_close(void *local);
extern int      plat_cdrom_set_drive(void *local, const char *drv);
extern int      plat_cdrom_ext_medium_changed(void *local);
extern uint32_t plat_cdrom_get_track_start(void *local, uint32_t sector, uint8_t *attr, uint8_t *track);
extern int      plat_cdrom_get_local_size(void);

#ifdef __cplusplus
}
#endif

#endif
