/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the CD-ROM drive with SCSI(-like)
 *          commands, for both ATAPI and SCSI usage.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2020 Miran Grca.
 */
#include <stdarg.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/machine.h>
#include <86box/nvr.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/sound.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/cdrom.h>
#include <86box/scsi_cdrom.h>
#include <86box/version.h>

#pragma pack(push, 1)
typedef struct gesn_cdb_t {
    uint8_t opcode;
    uint8_t polled;
    uint8_t reserved2[2];
    uint8_t class;
    uint8_t  reserved3[2];
    uint16_t len;
    uint8_t  control;
} gesn_cdb_t;

typedef struct gesn_event_header_t {
    uint16_t len;
    uint8_t  notification_class;
    uint8_t  supported_events;
} gesn_event_header_t;
#pragma pack(pop)

/* Table of all SCSI commands and their flags, needed for the new disc change / not ready handler. */
uint8_t scsi_cdrom_command_flags[0x100] = {
    IMPLEMENTED | CHECK_READY | NONDATA,             /* 0x00 */
    IMPLEMENTED | ALLOW_UA | NONDATA | SCSI_ONLY,    /* 0x01 */
    0,                                               /* 0x02 */
    IMPLEMENTED | ALLOW_UA,                          /* 0x03 */
    0, 0, 0, 0,                                      /* 0x04-0x07 */
    IMPLEMENTED | CHECK_READY,                       /* 0x08 */
    0, 0,                                            /* 0x09-0x0A */
    IMPLEMENTED | CHECK_READY | NONDATA,             /* 0x0B */
    0,                                               /* 0x0C */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0x0D */
    0, 0, 0, 0,                                      /* 0x0E-0x11 */
    IMPLEMENTED | ALLOW_UA,                          /* 0x12 */
    IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY, /* 0x13 */
    0,                                               /* 0x14 */
    IMPLEMENTED,                                     /* 0x15 */
    0, 0, 0, 0,                                      /* 0x16-0x19 */
    IMPLEMENTED,                                     /* 0x1A */
    IMPLEMENTED | CHECK_READY,                       /* 0x1B */
    0, 0,                                            /* 0x1C-0x1D */
    IMPLEMENTED | CHECK_READY,                       /* 0x1E */
    0, 0, 0,                                         /* 0x1F-0x21*/
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0x22*/
    0, 0,                                            /* 0x23-0x24 */
    IMPLEMENTED | CHECK_READY,                       /* 0x25 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0x26 */
    0,                                               /* 0x27 */
    IMPLEMENTED | CHECK_READY,                       /* 0x28 */
    0, 0,                                            /* 0x29-0x2A */
    IMPLEMENTED | CHECK_READY | NONDATA,             /* 0x2B */
    0, 0, 0,                                         /* 0x2C-0x2E */
    IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY, /* 0x2F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x30-0x3F */
    0, 0,                                            /* 0x40-0x41 */
    IMPLEMENTED | CHECK_READY,                       /* 0x42 */
    IMPLEMENTED | CHECK_READY,                       /* 0x43 - Read TOC - can get through UNIT_ATTENTION, per VIDE-CDD.SYS
                                                        NOTE: The ATAPI reference says otherwise, but I think this is a question of
                                                        interpreting things right - the UNIT ATTENTION condition we have here
                                                        is a tradition from not ready to ready, by definition the drive
                                                        eventually becomes ready, make the condition go away. */
    IMPLEMENTED | CHECK_READY,                       /* 0x44 */
    IMPLEMENTED | CHECK_READY,                       /* 0x45 */
    IMPLEMENTED | ALLOW_UA,                          /* 0x46 */
    IMPLEMENTED | CHECK_READY,                       /* 0x47 */
    IMPLEMENTED | CHECK_READY,                       /* 0x48 */
    IMPLEMENTED | CHECK_READY,                       /* 0x49 */
    IMPLEMENTED | ALLOW_UA,                          /* 0x4A */
    IMPLEMENTED | CHECK_READY,                       /* 0x4B */
    0, 0,                                            /* 0x4C-0x4D */
    IMPLEMENTED | CHECK_READY,                       /* 0x4E */
    0, 0,                                            /* 0x4F-0x50 */
    IMPLEMENTED | CHECK_READY,                       /* 0x51 */
    IMPLEMENTED | CHECK_READY,                       /* 0x52 */
    0, 0,                                            /* 0x53-0x54 */
    IMPLEMENTED,                                     /* 0x55 */
    0, 0, 0, 0,                                      /* 0x56-0x59 */
    IMPLEMENTED,                                     /* 0x5A */
    0, 0, 0, 0, 0,                                   /* 0x5B-0x5F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x60-0x6F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x70-0x7F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x80-0x8F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x90-0x9F */
    0, 0, 0, 0, 0,                                   /* 0xA0-0xA4 */
    IMPLEMENTED | CHECK_READY,                       /* 0xA5 */
    0, 0,                                            /* 0xA6-0xA7 */
    IMPLEMENTED | CHECK_READY,                       /* 0xA8 */
    IMPLEMENTED | CHECK_READY,                       /* 0xA9 */
    0, 0, 0,                                         /* 0xAA-0xAC */
    IMPLEMENTED | CHECK_READY,                       /* 0xAD */
    0,                                               /* 0xAE */
    IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY, /* 0xAF */
    0, 0, 0, 0,                                      /* 0xB0-0xB3 */
    IMPLEMENTED | CHECK_READY | ATAPI_ONLY,          /* 0xB4 */
    0, 0, 0,                                         /* 0xB5-0xB7 */
    IMPLEMENTED | CHECK_READY | ATAPI_ONLY,          /* 0xB8 */
    IMPLEMENTED | CHECK_READY,                       /* 0xB9 */
    IMPLEMENTED | CHECK_READY,                       /* 0xBA */
    IMPLEMENTED,                                     /* 0xBB */
    IMPLEMENTED | CHECK_READY,                       /* 0xBC */
    IMPLEMENTED,                                     /* 0xBD */
    IMPLEMENTED | CHECK_READY,                       /* 0xBE */
    IMPLEMENTED | CHECK_READY,                       /* 0xBF */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xC0 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xC1 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xC2 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xC3 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xC4 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xC5 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xC6 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xC7 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xC8 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xC9 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xCA */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xCB */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xCC */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xCD */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                    /* 0xCE-0xD7 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xD8 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xD9 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xDA */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xDB */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xDC */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xDD */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xDE */
    0,                                               /* 0xDF */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xE0 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xE1 */
    0,                                               /* 0xE2 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xE3 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xE4 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xE5 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xE6 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xE7 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xE8 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xE9 */
    0,                                               /* 0xEA */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xEB */
    0,                                               /* 0xEC */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xED */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,           /* 0xEE */
    0,                                               /* 0xEF */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   /* 0xF0-0xFF */
};

static uint64_t scsi_cdrom_mode_sense_page_flags      = (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE | GPMODEP_CDROM_PAGE | GPMODEP_CDROM_AUDIO_PAGE | (1ULL << 0x0fULL) | GPMODEP_CAPABILITIES_PAGE | GPMODEP_ALL_PAGES);
static uint64_t scsi_cdrom_mode_sense_page_flags_sony = (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE | GPMODEP_CDROM_PAGE_SONY | GPMODEP_CDROM_AUDIO_PAGE_SONY | (1ULL << 0x0fULL) | GPMODEP_CAPABILITIES_PAGE | GPMODEP_ALL_PAGES);
static uint64_t scsi_cdrom_drive_status_page_flags    = ((1ULL << 0x01ULL) | (1ULL << 0x02ULL) | (1ULL << 0x0fULL) | GPMODEP_ALL_PAGES);

static const mode_sense_pages_t scsi_cdrom_drive_status_pages = {
    {{ 0, 0 },
     { 0x01, 0, 2, 0x0f, 0xbf }, /*Drive Status Data Format*/
      { 0x02, 0, 1, 0 }, /*Audio Play Status Format*/
      { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 }}
};

static const mode_sense_pages_t scsi_cdrom_mode_sense_pages_default = {
    {{ 0, 0 },
     { GPMODE_R_W_ERROR_PAGE, 6, 0, 5, 0, 0, 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { GPMODE_CDROM_PAGE, 6, 0, 1, 0, 60, 0, 75 },
     { GPMODE_CDROM_AUDIO_PAGE | 0x80, 0xE, 4, 0, 0, 0, 0, 75, 1, 255, 2, 255, 0, 0, 0, 0 },
     { 0x0F, 0x14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { GPMODE_CAPABILITIES_PAGE, 0x12, 0, 0, 1, 0, 0, 0, 2, 0xC2, 1, 0, 0, 0, 2, 0xC2, 0, 0, 0, 0 }}
};

static const mode_sense_pages_t scsi_cdrom_mode_sense_pages_default_scsi = {
    {{ 0, 0 },
     { GPMODE_R_W_ERROR_PAGE, 6, 0, 5, 0, 0, 0, 0 },
     { GPMODE_DISCONNECT_PAGE, 0x0e, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { GPMODE_CDROM_PAGE, 6, 0, 1, 0, 60, 0, 75 },
     { GPMODE_CDROM_AUDIO_PAGE | 0x80, 0xE, 5, 4, 0, 128, 0, 75, 1, 255, 2, 255, 0, 0, 0, 0 },
     { 0x0F, 0x14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { GPMODE_CAPABILITIES_PAGE, 0x12, 0, 0, 1, 0, 0, 0, 2, 0xC2, 1, 0, 0, 0, 2, 0xC2, 0, 0, 0, 0 }}
};

static const mode_sense_pages_t scsi_cdrom_mode_sense_pages_default_sony_scsi = {
    {{ 0, 0 },
     { GPMODE_R_W_ERROR_PAGE, 6, 0, 5, 0, 0, 0, 0 },
     { GPMODE_DISCONNECT_PAGE, 0x0e, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { GPMODE_CDROM_PAGE_SONY, 2, 0, 5 },
     { GPMODE_CDROM_AUDIO_PAGE_SONY | 0x80, 0xE, 5, 0, 0, 0, 0, 0, 1, 255, 2, 255, 0, 0, 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { GPMODE_CDROM_PAGE, 6, 0, 1, 0, 60, 0, 75 },
     { GPMODE_CDROM_AUDIO_PAGE | 0x80, 0xE, 5, 4, 0, 128, 0, 75, 1, 255, 2, 255, 0, 0, 0, 0 },
     { 0x0F, 0x14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { GPMODE_CAPABILITIES_PAGE, 0x12, 0, 0, 1, 0, 0, 0, 2, 0xC2, 1, 0, 0, 0, 2, 0xC2, 0, 0, 0, 0 }}
};

static const mode_sense_pages_t scsi_cdrom_mode_sense_pages_changeable = {
    {{ 0, 0 },
     { GPMODE_R_W_ERROR_PAGE, 6, 0xFF, 0xFF, 0, 0, 0, 0 },
     { GPMODE_DISCONNECT_PAGE, 0x0E, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { GPMODE_CDROM_PAGE, 6, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
     { GPMODE_CDROM_AUDIO_PAGE | 0x80, 0xE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
     { 0x0F, 0x14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { GPMODE_CAPABILITIES_PAGE, 0x12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }}
};

static const mode_sense_pages_t scsi_cdrom_mode_sense_pages_changeable_sony = {
    {{ 0, 0 },
     { GPMODE_R_W_ERROR_PAGE, 6, 0xFF, 0xFF, 0, 0, 0, 0 },
     { GPMODE_DISCONNECT_PAGE, 0x0E, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { GPMODE_CDROM_PAGE_SONY, 2, 0xFF, 0xFF },
     { GPMODE_CDROM_AUDIO_PAGE_SONY | 0x80, 0xE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { GPMODE_CDROM_PAGE, 6, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
     { GPMODE_CDROM_AUDIO_PAGE | 0x80, 0xE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
     { 0x0F, 0x14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { 0, 0 },
     { GPMODE_CAPABILITIES_PAGE, 0x12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }}
};

static gesn_cdb_t          *gesn_cdb;
static gesn_event_header_t *gesn_event_header;

static void scsi_cdrom_command_complete(scsi_cdrom_t *dev);

static void scsi_cdrom_mode_sense_load(scsi_cdrom_t *dev);
static void scsi_cdrom_drive_status_load(scsi_cdrom_t *dev);

static void scsi_cdrom_init(scsi_cdrom_t *dev);

#ifdef ENABLE_SCSI_CDROM_LOG
int scsi_cdrom_do_log = ENABLE_SCSI_CDROM_LOG;

static void
scsi_cdrom_log(const char *format, ...)
{
    va_list ap;

    if (scsi_cdrom_do_log) {
        va_start(ap, format);
        pclog_ex(format, ap);
        va_end(ap);
    }
}
#else
#    define scsi_cdrom_log(format, ...)
#endif

static void
scsi_cdrom_set_callback(scsi_cdrom_t *dev)
{
    if (dev && dev->drv && (dev->drv->bus_type != CDROM_BUS_SCSI))
        ide_set_callback(ide_drives[dev->drv->ide_channel], dev->callback);
}

static void
scsi_cdrom_init(scsi_cdrom_t *dev)
{
    if (!dev)
        return;

    /* Do a reset (which will also rezero it). */
    scsi_cdrom_reset((scsi_common_t *) dev);

    /* Configure the drive. */
    dev->requested_blocks = 1;

    dev->drv->bus_mode = 0;
    if (dev->drv->bus_type >= CDROM_BUS_ATAPI)
        dev->drv->bus_mode |= 2;
    if (dev->drv->bus_type < CDROM_BUS_SCSI)
        dev->drv->bus_mode |= 1;
    scsi_cdrom_log("CD-ROM %i: Bus type %i, bus mode %i\n", dev->id, dev->drv->bus_type, dev->drv->bus_mode);

    dev->sense[0] = 0xf0;
    dev->sense[7] = 10;
    if ((dev->drv->type == CDROM_TYPE_NEC_260_100) || (dev->drv->type == CDROM_TYPE_NEC_260_101)) /*NEC only*/
        dev->status = READY_STAT | DSC_STAT;
    else
        dev->status = 0;
    dev->pos             = 0;
    dev->packet_status   = PHASE_NONE;
    scsi_cdrom_sense_key = scsi_cdrom_asc = scsi_cdrom_ascq = dev->unit_attention = 0;
    dev->drv->cur_speed                                                           = dev->drv->speed;
    scsi_cdrom_mode_sense_load(dev);
    if (dev->drv->type == CDROM_TYPE_PIONEER_DRM604X_2403)
        scsi_cdrom_drive_status_load(dev);
}

/* Returns: 0 for none, 1 for PIO, 2 for DMA. */
static int
scsi_cdrom_current_mode(scsi_cdrom_t *dev)
{
    if (dev->drv->bus_type == CDROM_BUS_SCSI)
        return 2;
    else if (dev->drv->bus_type == CDROM_BUS_ATAPI) {
        scsi_cdrom_log("CD-ROM %i: ATAPI drive, setting to %s\n", dev->id,
                       (dev->features & 1) ? "DMA" : "PIO",
                       dev->id);
        return (dev->features & 1) ? 2 : 1;
    }

    return 0;
}

/* Translates ATAPI phase (DRQ, I/O, C/D) to SCSI phase (MSG, C/D, I/O). */
int
scsi_cdrom_atapi_phase_to_scsi(scsi_cdrom_t *dev)
{
    if (dev->status & 8) {
        switch (dev->phase & 3) {
            case 0:
                return 0;
            case 1:
                return 2;
            case 2:
                return 1;
            case 3:
                return 7;

            default:
                break;
        }
    } else {
        if ((dev->phase & 3) == 3)
            return 3;
        else
            return 4;
    }

    return 0;
}

static uint32_t
scsi_cdrom_get_channel(void *priv, int channel)
{
    const scsi_cdrom_t *dev = (scsi_cdrom_t *) priv;
    uint32_t ret;

    if (!dev)
        return channel + 1;

    switch (dev->drv->type) {
        case CDROM_TYPE_DEC_RRD45_0436:
        case CDROM_TYPE_SONY_CDU541_10i:
        case CDROM_TYPE_SONY_CDU561_18k:
        case CDROM_TYPE_SONY_CDU76S_100:
        case CDROM_TYPE_TEXEL_DMXX24_100:
            ret = dev->ms_pages_saved_sony.pages[dev->sony_vendor ? GPMODE_CDROM_AUDIO_PAGE_SONY : GPMODE_CDROM_AUDIO_PAGE][channel ? 10 : 8];
            break;
        default:
            ret = dev->ms_pages_saved.pages[GPMODE_CDROM_AUDIO_PAGE][channel ? 10 : 8];
            break;
    }

    return ret;
}

static uint32_t
scsi_cdrom_get_volume(void *priv, int channel)
{
    const scsi_cdrom_t *dev = (scsi_cdrom_t *) priv;
    uint32_t ret;

    if (!dev)
        return 255;

    switch (dev->drv->type) {
        case CDROM_TYPE_DEC_RRD45_0436:
        case CDROM_TYPE_SONY_CDU541_10i:
        case CDROM_TYPE_SONY_CDU561_18k:
        case CDROM_TYPE_SONY_CDU76S_100:
        case CDROM_TYPE_TEXEL_DMXX24_100:
            ret = dev->ms_pages_saved_sony.pages[dev->sony_vendor ? GPMODE_CDROM_AUDIO_PAGE_SONY : GPMODE_CDROM_AUDIO_PAGE][channel ? 11 : 9];
            break;
        default:
            ret = dev->ms_pages_saved.pages[GPMODE_CDROM_AUDIO_PAGE][channel ? 11 : 9];
            break;
    }

    return ret;
}

static void
scsi_cdrom_mode_sense_load(scsi_cdrom_t *dev)
{
    FILE *fp;
    char  file_name[512];

    switch (dev->drv->type) {
        case CDROM_TYPE_DEC_RRD45_0436:
        case CDROM_TYPE_SONY_CDU541_10i:
        case CDROM_TYPE_SONY_CDU561_18k:
        case CDROM_TYPE_SONY_CDU76S_100:
        case CDROM_TYPE_TEXEL_DMXX24_100:
            memset(&dev->ms_pages_saved_sony, 0, sizeof(mode_sense_pages_t));
            memcpy(&dev->ms_pages_saved_sony, &scsi_cdrom_mode_sense_pages_default_sony_scsi, sizeof(mode_sense_pages_t));

            memset(file_name, 0, 512);
            sprintf(file_name, "scsi_cdrom_%02i_mode_sense_sony_bin", dev->id);
            fp = plat_fopen(nvr_path(file_name), "rb");
            if (fp) {
                if (fread(dev->ms_pages_saved_sony.pages[GPMODE_CDROM_AUDIO_PAGE_SONY], 1, 0x10, fp) != 0x10)
                    fatal("scsi_cdrom_mode_sense_load(): Error reading data\n");
                fclose(fp);
            }
            break;
        default:
            memset(&dev->ms_pages_saved, 0, sizeof(mode_sense_pages_t));
            if (dev->drv->bus_type == CDROM_BUS_SCSI)
                memcpy(&dev->ms_pages_saved, &scsi_cdrom_mode_sense_pages_default_scsi, sizeof(mode_sense_pages_t));
            else
                memcpy(&dev->ms_pages_saved, &scsi_cdrom_mode_sense_pages_default, sizeof(mode_sense_pages_t));

            memset(file_name, 0, 512);
            if (dev->drv->bus_type == CDROM_BUS_SCSI)
                sprintf(file_name, "scsi_cdrom_%02i_mode_sense_bin", dev->id);
            else
                sprintf(file_name, "cdrom_%02i_mode_sense_bin", dev->id);
            fp = plat_fopen(nvr_path(file_name), "rb");
            if (fp) {
                if (fread(dev->ms_pages_saved.pages[GPMODE_CDROM_AUDIO_PAGE], 1, 0x10, fp) != 0x10)
                    fatal("scsi_cdrom_mode_sense_load(): Error reading data\n");
                fclose(fp);
            }
            break;
    }
}

static void
scsi_cdrom_mode_sense_save(scsi_cdrom_t *dev)
{
    FILE *fp;
    char  file_name[512];

    memset(file_name, 0, 512);

    switch (dev->drv->type) {
        case CDROM_TYPE_DEC_RRD45_0436:
        case CDROM_TYPE_SONY_CDU541_10i:
        case CDROM_TYPE_SONY_CDU561_18k:
        case CDROM_TYPE_SONY_CDU76S_100:
        case CDROM_TYPE_TEXEL_DMXX24_100:
            sprintf(file_name, "scsi_cdrom_%02i_mode_sense_sony_bin", dev->id);
            fp = plat_fopen(nvr_path(file_name), "wb");
            if (fp) {
                fwrite(dev->ms_pages_saved_sony.pages[GPMODE_CDROM_AUDIO_PAGE_SONY], 1, 0x10, fp);
                fclose(fp);
            }
            break;
        default:
            if (dev->drv->bus_type == CDROM_BUS_SCSI)
                sprintf(file_name, "scsi_cdrom_%02i_mode_sense_bin", dev->id);
            else
                sprintf(file_name, "cdrom_%02i_mode_sense_bin", dev->id);
            fp = plat_fopen(nvr_path(file_name), "wb");
            if (fp) {
                fwrite(dev->ms_pages_saved.pages[GPMODE_CDROM_AUDIO_PAGE], 1, 0x10, fp);
                fclose(fp);
            }
            break;
    }
}

/*SCSI Drive Status (Pioneer only)*/
static void
scsi_cdrom_drive_status_load(scsi_cdrom_t *dev)
{
    memset(&dev->ms_drive_status_pages_saved, 0, sizeof(mode_sense_pages_t));
    memcpy(&dev->ms_drive_status_pages_saved, &scsi_cdrom_drive_status_pages, sizeof(mode_sense_pages_t));
}

static uint8_t
scsi_cdrom_drive_status_read(scsi_cdrom_t *dev, UNUSED(uint8_t page_control), uint8_t page, uint8_t pos)
{
    return dev->ms_drive_status_pages_saved.pages[page][pos];
}

static uint32_t
scsi_cdrom_drive_status(scsi_cdrom_t *dev, uint8_t *buf, uint32_t pos, uint8_t page)
{
    uint8_t page_control = (page >> 6) & 3;
    uint16_t msplen;

    page &= 0x3f;

    for (uint8_t i = 0; i < 0x40; i++) {
        if (page == i) {
            if (scsi_cdrom_drive_status_page_flags & (1LL << ((uint64_t) (page & 0x3f)))) {
                buf[pos++] = scsi_cdrom_drive_status_read(dev, page_control, i, 0);
                msplen     = (scsi_cdrom_drive_status_read(dev, page_control, i, 1) << 8);
                msplen    |= scsi_cdrom_drive_status_read(dev, page_control, i, 2);
                buf[pos++] = (msplen >> 8) & 0xff;
                buf[pos++] = msplen & 0xff;
                scsi_cdrom_log("CD-ROM %i: DRIVE STATUS: Page [%02X] length %i\n", dev->id, i, msplen);
                for (uint16_t j = 0; j < msplen; j++) {
                    if (i == 0x01) {
                        buf[pos++] = scsi_cdrom_drive_status_read(dev, page_control, i, 3 + j);
                        if (!(j & 1)) {            /*MSB of Drive Status*/
                            if (dev->drv->ops)     /*Bit 11 of Drive Status, */
                                buf[pos] &= ~0x08; /*Disc is present*/
                            else
                                buf[pos] |= 0x08; /*Disc not present*/
                        }
                    } else if ((i == 0x02) && (j == 0)) {
                        buf[pos++] = ((dev->drv->cd_status == CD_STATUS_PLAYING) ? 0x01 : 0x00);
                    } else
                        buf[pos++] = scsi_cdrom_drive_status_read(dev, page_control, i, 3 + j);
                }
            }
        }
    }

    return pos;
}

/*SCSI Mode Sense 6/10*/
static uint8_t
scsi_cdrom_mode_sense_read(scsi_cdrom_t *dev, uint8_t page_control, uint8_t page, uint8_t pos)
{
    switch (dev->drv->type) {
        case CDROM_TYPE_DEC_RRD45_0436:
        case CDROM_TYPE_SONY_CDU541_10i:
        case CDROM_TYPE_SONY_CDU561_18k:
        case CDROM_TYPE_SONY_CDU76S_100:
        case CDROM_TYPE_TEXEL_DMXX24_100:
            switch (page_control) {
                case 0:
                case 3:
                    return dev->ms_pages_saved_sony.pages[page][pos];
                case 1:
                    return scsi_cdrom_mode_sense_pages_changeable_sony.pages[page][pos];
                case 2:
                    return scsi_cdrom_mode_sense_pages_default_sony_scsi.pages[page][pos];

                default:
                    break;
            }
            break;
        default:
            switch (page_control) {
                case 0:
                case 3:
                    return dev->ms_pages_saved.pages[page][pos];
                case 1:
                    return scsi_cdrom_mode_sense_pages_changeable.pages[page][pos];
                case 2:
                    if (dev->drv->bus_type == CDROM_BUS_SCSI)
                        return scsi_cdrom_mode_sense_pages_default_scsi.pages[page][pos];
                    else
                        return scsi_cdrom_mode_sense_pages_default.pages[page][pos];

                default:
                    break;
            }
            break;
    }

    return 0;
}

static uint32_t
scsi_cdrom_mode_sense(scsi_cdrom_t *dev, uint8_t *buf, uint32_t pos, uint8_t page, uint8_t block_descriptor_len)
{
    uint8_t page_control = (page >> 6) & 3;
    uint8_t msplen;

    page &= 0x3f;

    if (block_descriptor_len) {
        buf[pos++] = 1; /* Density code. */
        buf[pos++] = 0; /* Number of blocks (0 = all). */
        buf[pos++] = 0;
        buf[pos++] = 0;
        buf[pos++] = 0; /* Reserved. */
        buf[pos++] = 0; /* Block length (0x800 = 2048 bytes). */
        buf[pos++] = 8;
        buf[pos++] = 0;
    }

    for (uint8_t i = 0; i < 0x40; i++) {
        if ((page == GPMODE_ALL_PAGES) || (page == i)) {
            if (scsi_cdrom_mode_sense_page_flags & (1LL << ((uint64_t) (page & 0x3f)))) {
                buf[pos++] = scsi_cdrom_mode_sense_read(dev, page_control, i, 0);
                msplen     = scsi_cdrom_mode_sense_read(dev, page_control, i, 1);
                buf[pos++] = msplen;
                scsi_cdrom_log("CD-ROM %i: MODE SENSE: Page [%02X] length %i\n", dev->id, i, msplen);
                for (uint8_t j = 0; j < msplen; j++) {
                    /* If we are returning changeable values, always return them from the page,
                       so they are all correctly. */
                    if (page_control == 1)
                        buf[pos++] = scsi_cdrom_mode_sense_read(dev, page_control, i, 2 + j);
                    else {
                        if ((i == GPMODE_CAPABILITIES_PAGE) && (j == 4)) {
                            buf[pos] = scsi_cdrom_mode_sense_read(dev, page_control, i, 2 + j) & 0x1f;
                            /* The early CD-ROM drives we emulate (NEC CDR-260 for ATAPI and early vendor SCSI CD-ROM models) are
                               caddy drives, the later ones are tray drives. */
                            if (dev->drv->bus_type == CDROM_BUS_SCSI) {
                                buf[pos++] |= ((dev->drv->type == CDROM_TYPE_86BOX_100) ? 0x20 : 0x00);
                            } else {
                                buf[pos++] |= ((dev->drv->type == CDROM_TYPE_NEC_260_100) ||
                                                ((dev->drv->type == CDROM_TYPE_NEC_260_101)) ? 0x00 : 0x20);
                            }
                        } else if ((i == GPMODE_CAPABILITIES_PAGE) && (j >= 6) && (j <= 7)) {
                            if (j & 1)
                                buf[pos++] = ((dev->drv->speed * 176) & 0xff);
                            else
                                buf[pos++] = ((dev->drv->speed * 176) >> 8);
                        } else if ((i == GPMODE_CAPABILITIES_PAGE) && (j >= 8) && (j <= 9) &&
                                    (dev->drv->type == CDROM_TYPE_PIONEER_DRM604X_2403)) {
                            if (j & 1)
                                buf[pos++] = ((dev->drv->speed * 176) & 0xff);
                            else
                                buf[pos++] = ((dev->drv->speed * 176) >> 8);
                        } else if ((i == GPMODE_CAPABILITIES_PAGE) && (j >= 12) && (j <= 13)) {
                            if (j & 1)
                                buf[pos++] = ((dev->drv->cur_speed * 176) & 0xff);
                            else
                                buf[pos++] = ((dev->drv->cur_speed * 176) >> 8);
                        } else
                            buf[pos++] = scsi_cdrom_mode_sense_read(dev, page_control, i, 2 + j);
                    }
                }
            }
        }
    }

    return pos;
}

static void
scsi_cdrom_update_request_length(scsi_cdrom_t *dev, int len, int block_len)
{
    int32_t bt;
    int32_t min_len = 0;
    double  dlen;

    dev->max_transfer_len = dev->request_length;

    /* For media access commands, make sure the requested DRQ length matches the block length. */
    switch (dev->current_cdb[0]) {
        case 0x08:
        case 0x28:
        case 0xa8:
        case 0xb9:
        case 0xbe:
            /* Round it to the nearest (block length) bytes. */
            if ((dev->current_cdb[0] == 0xb9) || (dev->current_cdb[0] == 0xbe)) {
                /* READ CD MSF and READ CD: Round the request length to the sector size - the device must ensure
                   that a media access comand does not DRQ in the middle of a sector. One of the drivers that
                   relies on the correctness of this behavior is MTMCDAI.SYS (the Mitsumi CD-ROM driver) for DOS
                   which uses the READ CD command to read data on some CD types. */

                /* Round to sector length. */
                dlen                  = ((double) dev->max_transfer_len) / ((double) block_len);
                dev->max_transfer_len = ((uint16_t) floor(dlen)) * block_len;
            } else {
                /* Round it to the nearest 2048 bytes. */
                dev->max_transfer_len = (dev->max_transfer_len >> 11) << 11;
            }

            /* Make sure total length is not bigger than sum of the lengths of
               all the requested blocks. */
            bt = (dev->requested_blocks * block_len);
            if (len > bt)
                len = bt;

            min_len = block_len;

            if (len <= block_len) {
                /* Total length is less or equal to block length. */
                if (dev->max_transfer_len < block_len) {
                    /* Transfer a minimum of (block size) bytes. */
                    dev->max_transfer_len = block_len;
                    dev->packet_len       = block_len;
                    break;
                }
            }
            fallthrough;

        default:
            dev->packet_len = len;
            break;
    }
    /* If the DRQ length is odd, and the total remaining length is bigger, make sure it's even. */
    if ((dev->max_transfer_len & 1) && (dev->max_transfer_len < len))
        dev->max_transfer_len &= 0xfffe;
    /* If the DRQ length is smaller or equal in size to the total remaining length, set it to that. */
    if (!dev->max_transfer_len)
        dev->max_transfer_len = 65534;

    if ((len <= dev->max_transfer_len) && (len >= min_len))
        dev->request_length = dev->max_transfer_len = len;
    else if (len > dev->max_transfer_len)
        dev->request_length = dev->max_transfer_len;

    return;
}

static double
scsi_cdrom_bus_speed(scsi_cdrom_t *dev)
{
    double ret = -1.0;

    if (dev && dev->drv && (dev->drv->bus_type == CDROM_BUS_SCSI)) {
        dev->callback = -1.0; /* Speed depends on SCSI controller */
        return 0.0;
    } else {
        if (dev && dev->drv)
            ret = ide_atapi_get_period(dev->drv->ide_channel);
        if (ret == -1.0) {
            if (dev)
                dev->callback = -1.0;
            return 0.0;
        } else
            return ret * 1000000.0;
    }
}

static void
scsi_cdrom_command_common(scsi_cdrom_t *dev)
{
    double bytes_per_second = 0.0;
    double period;

    dev->status   = BUSY_STAT;
    dev->phase    = 1;
    dev->pos      = 0;
    dev->callback = 0;

    scsi_cdrom_log("CD-ROM %i: Current speed: %ix\n", dev->id, dev->drv->cur_speed);

    if (dev->packet_status == PHASE_COMPLETE)
        dev->callback = 0;
    else {
        switch (dev->current_cdb[0]) {
            case GPCMD_REZERO_UNIT:
            case 0x0b:
            case 0x2b:
                /* Seek time is in us. */
                period = cdrom_seek_time(dev->drv);
                scsi_cdrom_log("CD-ROM %i: Seek period: %" PRIu64 " us\n",
                               dev->id, (uint64_t) period);
                dev->callback += period;
                scsi_cdrom_set_callback(dev);
                return;
            case 0x08:
            case 0x28:
            case 0xa8:
                /* Seek time is in us. */
                period = cdrom_seek_time(dev->drv);
                scsi_cdrom_log("CD-ROM %i: Seek period: %" PRIu64 " us\n",
                               dev->id, (uint64_t) period);
                dev->callback += period;
                fallthrough;
            case 0x25:
            case 0x42:
            case 0x43:
            case 0x44:
            case 0x51:
            case 0x52:
            case 0xad:
            case 0xb8:
            case 0xb9:
            case 0xbe:
                if (dev->current_cdb[0] == 0x42)
                    dev->callback += 40.0;
                /* Account for seek time. */
                bytes_per_second = 176.0 * 1024.0;
                bytes_per_second *= (double) dev->drv->cur_speed;
                break;
            case 0xc6:
            case 0xc7:
                switch (dev->drv->type) {
                    case CDROM_TYPE_TOSHIBA_XM_3433:
                    case CDROM_TYPE_TOSHIBA_XM3201B_3232:
                    case CDROM_TYPE_TOSHIBA_XM3301TA_0272:
                    case CDROM_TYPE_TOSHIBA_XM5701TA_3136:
                    case CDROM_TYPE_TOSHIBA_SDM1401_1008:
                        bytes_per_second = 176.0 * 1024.0;
                        bytes_per_second *= (double) dev->drv->cur_speed;
                        break;
                }
            case 0xc0:
                switch (dev->drv->type) {
                    case CDROM_TYPE_DEC_RRD45_0436:
                    case CDROM_TYPE_SONY_CDU541_10i:
                    case CDROM_TYPE_SONY_CDU561_18k:
                    case CDROM_TYPE_SONY_CDU76S_100:
                    case CDROM_TYPE_TEXEL_DMXX24_100:
                        bytes_per_second = 176.0 * 1024.0;
                        bytes_per_second *= (double) dev->drv->cur_speed;
                        break;
                }
            case 0xc1:
                switch (dev->drv->type) {
                    case CDROM_TYPE_DEC_RRD45_0436:
                    case CDROM_TYPE_SONY_CDU541_10i:
                    case CDROM_TYPE_SONY_CDU561_18k:
                    case CDROM_TYPE_SONY_CDU76S_100:
                    case CDROM_TYPE_PIONEER_DRM604X_2403:
                    case CDROM_TYPE_TEXEL_DMXX24_100:
                        bytes_per_second = 176.0 * 1024.0;
                        bytes_per_second *= (double) dev->drv->cur_speed;
                        break;
                }
            case 0xc2:
            case 0xc3:
                switch (dev->drv->type) {
                    case CDROM_TYPE_DEC_RRD45_0436:
                    case CDROM_TYPE_SONY_CDU541_10i:
                    case CDROM_TYPE_SONY_CDU561_18k:
                    case CDROM_TYPE_SONY_CDU76S_100:
                    case CDROM_TYPE_PIONEER_DRM604X_2403:
                    case CDROM_TYPE_TEXEL_DMXX24_100:
                        if (dev->current_cdb[0] == 0xc2)
                            dev->callback += 40.0;
                        bytes_per_second = 176.0 * 1024.0;
                        bytes_per_second *= (double) dev->drv->cur_speed;
                        break;
                }
            case 0xdd:
            case 0xde:
                switch (dev->drv->type) {
                    case CDROM_TYPE_NEC_38_103:
                    case CDROM_TYPE_NEC_211_100:
                    case CDROM_TYPE_NEC_464_105:
                        bytes_per_second = 176.0 * 1024.0;
                        bytes_per_second *= (double) dev->drv->cur_speed;
                        break;
                }
                fallthrough;
            default:
                bytes_per_second = scsi_cdrom_bus_speed(dev);
                if (bytes_per_second == 0.0) {
                    dev->callback = -1; /* Speed depends on SCSI controller */
                    return;
                }
                break;
        }

        period = 1000000.0 / bytes_per_second;
        scsi_cdrom_log("CD-ROM %i: Byte transfer period: %" PRIu64 " us\n", dev->id, (uint64_t) period);
        period = period * (double) (dev->packet_len);
        scsi_cdrom_log("CD-ROM %i: Sector transfer period: %" PRIu64 " us\n", dev->id, (uint64_t) period);
        dev->callback += period;
    }
    scsi_cdrom_set_callback(dev);
}

static void
scsi_cdrom_command_complete(scsi_cdrom_t *dev)
{
    ui_sb_update_icon(SB_CDROM | dev->id, 0);
    dev->packet_status = PHASE_COMPLETE;
    scsi_cdrom_command_common(dev);
    dev->phase = 3;
}

static void
scsi_cdrom_command_read(scsi_cdrom_t *dev)
{
    dev->packet_status = PHASE_DATA_IN;
    scsi_cdrom_command_common(dev);
    dev->phase = !(dev->packet_status & 0x01) << 1;
}

static void
scsi_cdrom_command_read_dma(scsi_cdrom_t *dev)
{
    dev->packet_status = PHASE_DATA_IN_DMA;
    scsi_cdrom_command_common(dev);
}

static void
scsi_cdrom_command_write(scsi_cdrom_t *dev)
{
    dev->packet_status = PHASE_DATA_OUT;
    scsi_cdrom_command_common(dev);
    dev->phase = !(dev->packet_status & 0x01) << 1;
}

static void
scsi_cdrom_command_write_dma(scsi_cdrom_t *dev)
{
    dev->packet_status = PHASE_DATA_OUT_DMA;
    scsi_cdrom_command_common(dev);
}

/* id = Current CD-ROM device ID;
   len = Total transfer length;
   block_len = Length of a single block (it matters because media access commands on ATAPI);
   alloc_len = Allocated transfer length;
   direction = Transfer direction (0 = read from host, 1 = write to host). */
static void
scsi_cdrom_data_command_finish(scsi_cdrom_t *dev, int len, int block_len, int alloc_len, int direction)
{
    scsi_cdrom_log("CD-ROM %i: Finishing command (%02X): %i, %i, %i, %i, %i\n",
                   dev->id, dev->current_cdb[0], len, block_len, alloc_len, direction, dev->request_length);
    dev->pos = 0;
    if (alloc_len >= 0) {
        if (alloc_len < len)
            len = alloc_len;
    }
    if ((len == 0) || (scsi_cdrom_current_mode(dev) == 0)) {
        if (dev->drv->bus_type != CDROM_BUS_SCSI)
            dev->packet_len = 0;

        scsi_cdrom_command_complete(dev);
    } else {
        if (scsi_cdrom_current_mode(dev) == 2) {
            if (dev->drv->bus_type != CDROM_BUS_SCSI)
                dev->packet_len = alloc_len;

            if (direction == 0)
                scsi_cdrom_command_read_dma(dev);
            else
                scsi_cdrom_command_write_dma(dev);
        } else {
            scsi_cdrom_update_request_length(dev, len, block_len);
            if (direction == 0)
                scsi_cdrom_command_read(dev);
            else
                scsi_cdrom_command_write(dev);
        }
    }

    scsi_cdrom_log("CD-ROM %i: Status: %i, cylinder %i, packet length: %i, position: %i, phase: %i\n",
                   dev->id, dev->packet_status, dev->request_length, dev->packet_len, dev->pos, dev->phase);
}

static void
scsi_cdrom_sense_clear(scsi_cdrom_t *dev, UNUSED(int command))
{
    scsi_cdrom_sense_key = scsi_cdrom_asc = scsi_cdrom_ascq = 0;
}

static void
scsi_cdrom_set_phase(scsi_cdrom_t *dev, uint8_t phase)
{
    uint8_t scsi_bus = (dev->drv->scsi_device_id >> 4) & 0x0f;
    uint8_t scsi_id  = dev->drv->scsi_device_id & 0x0f;

    if (dev->drv->bus_type != CDROM_BUS_SCSI)
        return;

    scsi_devices[scsi_bus][scsi_id].phase = phase;
}

static void
scsi_cdrom_cmd_error(scsi_cdrom_t *dev)
{
    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
    dev->error = ((scsi_cdrom_sense_key & 0xf) << 4) | ABRT_ERR;
    if (dev->unit_attention)
        dev->error |= MCR_ERR;
    dev->status        = READY_STAT | ERR_STAT;
    dev->phase         = 3;
    dev->pos           = 0;
    dev->packet_status = PHASE_ERROR;
    dev->callback      = 50.0 * CDROM_TIME;
    scsi_cdrom_set_callback(dev);
    ui_sb_update_icon(SB_CDROM | dev->id, 0);
    scsi_cdrom_log("CD-ROM %i: ERROR: %02X/%02X/%02X\n", dev->id, scsi_cdrom_sense_key, scsi_cdrom_asc, scsi_cdrom_ascq);
}

static void
scsi_cdrom_unit_attention(scsi_cdrom_t *dev)
{
    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
    dev->error = (SENSE_UNIT_ATTENTION << 4) | ABRT_ERR;
    if (dev->unit_attention)
        dev->error |= MCR_ERR;
    dev->status        = READY_STAT | ERR_STAT;
    dev->phase         = 3;
    dev->pos           = 0;
    dev->packet_status = PHASE_ERROR;
    dev->callback      = 50.0 * CDROM_TIME;
    scsi_cdrom_set_callback(dev);
    ui_sb_update_icon(SB_CDROM | dev->id, 0);
    scsi_cdrom_log("CD-ROM %i: UNIT ATTENTION\n", dev->id);
}

static void
scsi_cdrom_buf_alloc(scsi_cdrom_t *dev, uint32_t len)
{
    if (!dev->buffer)
        dev->buffer = (uint8_t *) malloc(len);
    scsi_cdrom_log("CD-ROM %i: Allocated buffer length: %i, buffer = %p\n", dev->id, len, dev->buffer);
}

static void
scsi_cdrom_buf_free(scsi_cdrom_t *dev)
{
    if (dev->buffer) {
        scsi_cdrom_log("CD-ROM %i: Freeing buffer...\n", dev->id);
        free(dev->buffer);
        dev->buffer = NULL;
    }
}

static void
scsi_cdrom_bus_master_error(scsi_common_t *sc)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) sc;

    scsi_cdrom_buf_free(dev);
    scsi_cdrom_sense_key = scsi_cdrom_asc = scsi_cdrom_ascq = 0;
    scsi_cdrom_cmd_error(dev);
}

static void
scsi_cdrom_not_ready(scsi_cdrom_t *dev)
{
    scsi_cdrom_sense_key = SENSE_NOT_READY;
    scsi_cdrom_asc       = ASC_MEDIUM_NOT_PRESENT;
    scsi_cdrom_ascq      = 0;
    scsi_cdrom_cmd_error(dev);
}

static void
scsi_cdrom_invalid_lun(scsi_cdrom_t *dev)
{
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc       = ASC_INV_LUN;
    scsi_cdrom_ascq      = 0;
    scsi_cdrom_cmd_error(dev);
}

static void
scsi_cdrom_illegal_opcode(scsi_cdrom_t *dev)
{
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc       = ASC_ILLEGAL_OPCODE;
    scsi_cdrom_ascq      = 0;
    scsi_cdrom_cmd_error(dev);
}

static void
scsi_cdrom_lba_out_of_range(scsi_cdrom_t *dev)
{
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc       = ASC_LBA_OUT_OF_RANGE;
    scsi_cdrom_ascq      = 0;
    scsi_cdrom_cmd_error(dev);
}

static void
scsi_cdrom_invalid_field(scsi_cdrom_t *dev)
{
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc       = ASC_INV_FIELD_IN_CMD_PACKET;
    scsi_cdrom_ascq      = 0;
    scsi_cdrom_cmd_error(dev);
    dev->status = 0x53;
}

static void
scsi_cdrom_invalid_field_pl(scsi_cdrom_t *dev)
{
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc       = ASC_INV_FIELD_IN_PARAMETER_LIST;
    scsi_cdrom_ascq      = 0;
    scsi_cdrom_cmd_error(dev);
    dev->status = 0x53;
}

static void
scsi_cdrom_illegal_mode(scsi_cdrom_t *dev)
{
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc       = ASC_ILLEGAL_MODE_FOR_THIS_TRACK;
    scsi_cdrom_ascq      = 0;
    scsi_cdrom_cmd_error(dev);
}

static void
scsi_cdrom_incompatible_format(scsi_cdrom_t *dev)
{
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc       = ASC_INCOMPATIBLE_FORMAT;
    scsi_cdrom_ascq      = 2;
    scsi_cdrom_cmd_error(dev);
}

static void
scsi_cdrom_data_phase_error(scsi_cdrom_t *dev)
{
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc       = ASC_DATA_PHASE_ERROR;
    scsi_cdrom_ascq      = 0;
    scsi_cdrom_cmd_error(dev);
}

static int
scsi_cdrom_read_data(scsi_cdrom_t *dev, int msf, int type, int flags, int32_t *len, int vendor_type)
{
    int      ret      = 0;
    int      data_pos = 0;
    int      temp_len = 0;
    uint32_t cdsize   = 0;

    if (dev->drv->cd_status == CD_STATUS_EMPTY) {
        scsi_cdrom_not_ready(dev);
        return 0;
    }

    cdsize = dev->drv->cdrom_capacity;

    if (dev->sector_pos >= cdsize) {
        scsi_cdrom_log("CD-ROM %i: Trying to read from beyond the end of disc (%i >= %i)\n", dev->id,
                       dev->sector_pos, cdsize);
        scsi_cdrom_lba_out_of_range(dev);
        return -1;
    }

/* FIXME: Temporarily disabled this because the Triones ATAPI DMA driver seems to
          always request a 4-sector read but sets the DMA bus master to transfer less
          data than that. */
#if 0
    if ((dev->sector_pos + dev->sector_len - 1) >= cdsize) {
        scsi_cdrom_log("CD-ROM %i: Trying to read to beyond the end of disc (%i >= %i)\n", dev->id,
          (dev->sector_pos + dev->sector_len - 1), cdsize);
        scsi_cdrom_lba_out_of_range(dev);
        return -1;
    }
#endif

    dev->old_len = 0;
    *len         = 0;

    for (int i = 0; i < dev->requested_blocks; i++) {
        ret = cdrom_readsector_raw(dev->drv, dev->buffer + data_pos,
                                   dev->sector_pos + i, msf, type, flags, &temp_len, vendor_type);

        data_pos += temp_len;
        dev->old_len += temp_len;

        *len += temp_len;

        if (!ret) {
            scsi_cdrom_illegal_mode(dev);
            return 0;
        }
    }

    return 1;
}

static int
scsi_cdrom_read_blocks(scsi_cdrom_t *dev, int32_t *len, int first_batch, int vendor_type)
{
    int ret   = 0;
    int msf   = 0;
    int type  = 0;
    int flags = 0;

    if (dev->current_cdb[0] == GPCMD_READ_CD_MSF)
        msf = 1;

    if ((dev->current_cdb[0] == GPCMD_READ_CD_MSF) || (dev->current_cdb[0] == GPCMD_READ_CD)) {
        type  = (dev->current_cdb[1] >> 2) & 7;
        flags = dev->current_cdb[9] | (((uint32_t) dev->current_cdb[10]) << 8);
    } else {
        type  = 8;
        flags = 0x10;
    }

    if (!dev->sector_len) {
        scsi_cdrom_command_complete(dev);
        return -1;
    }

    scsi_cdrom_log("Reading %i blocks starting from %i...\n", dev->requested_blocks, dev->sector_pos);

    ret = scsi_cdrom_read_data(dev, msf, type, flags, len, vendor_type);

    scsi_cdrom_log("Read %i bytes of blocks...\n", *len);

    if (ret == -1)
        return 0;
    else if (!ret || (!first_batch && (dev->old_len != *len))) {
        if (!first_batch && (dev->old_len != *len))
            scsi_cdrom_illegal_mode(dev);

        return 0;
    }

    dev->sector_pos += dev->requested_blocks;
    dev->drv->seek_pos = dev->sector_pos;
    dev->sector_len -= dev->requested_blocks;
    return 1;
}

/*SCSI Read DVD Structure*/
static int
scsi_cdrom_read_dvd_structure(scsi_cdrom_t *dev, int format, const uint8_t *packet, uint8_t *buf)
{
    int      layer         = packet[6];
    uint64_t total_sectors = 0;

    switch (format) {
        case 0x00: /* Physical format information */
            if (dev->drv->cd_status == CD_STATUS_EMPTY) {
                scsi_cdrom_not_ready(dev);
                return 0;
            }

            total_sectors = (uint64_t) dev->drv->cdrom_capacity;

            if (layer != 0) {
                scsi_cdrom_invalid_field(dev);
                return 0;
            }

            total_sectors >>= 2;
            if (total_sectors == 0) {
                /* return -ASC_MEDIUM_NOT_PRESENT; */
                scsi_cdrom_not_ready(dev);
                return 0;
            }

            buf[4] = 18; /* Length of Layer Information */
            buf[5] = 0;

            buf[6] = 1;   /* DVD-ROM, part version 1 */
            buf[7] = 0xf; /* 120mm disc, minimum rate unspecified */
            buf[8] = 1;   /* one layer, read-only (per MMC-2 spec) */
            buf[9] = 0;   /* default densities */

            /* FIXME: 0x30000 per spec? */
            buf[10] = 0x00;
            buf[11] = 0x03;
            buf[12] = buf[13] = 0; /* start sector */

            buf[14] = 0x00;
            buf[15] = (total_sectors >> 16) & 0xff; /* end sector */
            buf[16] = (total_sectors >> 8) & 0xff;
            buf[17] = total_sectors & 0xff;

            buf[18] = 0x00;
            buf[19] = (total_sectors >> 16) & 0xff; /* l0 end sector */
            buf[20] = (total_sectors >> 8) & 0xff;
            buf[21] = total_sectors & 0xff;

            /* 20 bytes of data + 4 byte header */
            return (20 + 4);

        case 0x01:      /* DVD copyright information */
            buf[4] = 0; /* no copyright data */
            buf[5] = 0; /* no region restrictions */

            /* Size of buffer, not including 2 byte size field */
            buf[0] = ((4 + 2) >> 8) & 0xff;
            buf[1] = (4 + 2) & 0xff;

            /* 4 byte header + 4 byte data */
            return (4 + 4);

        case 0x03: /* BCA information - invalid field for no BCA info */
            scsi_cdrom_invalid_field(dev);
            return 0;

        case 0x04: /* DVD disc manufacturing information */
                   /* Size of buffer, not including 2 byte size field */
            buf[0] = ((2048 + 2) >> 8) & 0xff;
            buf[1] = (2048 + 2) & 0xff;

            /* 2k data + 4 byte header */
            return (2048 + 4);

        case 0xff:
            /*
             * This lists all the command capabilities above.  Add new ones
             * in order and update the length and buffer return values.
             */

            buf[4] = 0x00; /* Physical format */
            buf[5] = 0x40; /* Not writable, is readable */
            buf[6] = ((20 + 4) >> 8) & 0xff;
            buf[7] = (20 + 4) & 0xff;

            buf[8]  = 0x01; /* Copyright info */
            buf[9]  = 0x40; /* Not writable, is readable */
            buf[10] = ((4 + 4) >> 8) & 0xff;
            buf[11] = (4 + 4) & 0xff;

            buf[12] = 0x03; /* BCA info */
            buf[13] = 0x40; /* Not writable, is readable */
            buf[14] = ((188 + 4) >> 8) & 0xff;
            buf[15] = (188 + 4) & 0xff;

            buf[16] = 0x04; /* Manufacturing info */
            buf[17] = 0x40; /* Not writable, is readable */
            buf[18] = ((2048 + 4) >> 8) & 0xff;
            buf[19] = (2048 + 4) & 0xff;

            /* Size of buffer, not including 2 byte size field */
            buf[6] = ((16 + 2) >> 8) & 0xff;
            buf[7] = (16 + 2) & 0xff;

            /* data written + 4 byte header */
            return (16 + 4);

        default: /* TODO: formats beyond DVD-ROM requires */
            scsi_cdrom_invalid_field(dev);
            return 0;
    }
}

static void
scsi_cdrom_insert(void *priv)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) priv;

    if (!dev)
        return;

    dev->unit_attention = 1;
    /* Turn off the medium changed status. */
    dev->drv->cd_status &= ~CD_STATUS_MEDIUM_CHANGED;
    scsi_cdrom_log("CD-ROM %i: Media insert\n", dev->id);
}

static int
scsi_cdrom_pre_execution_check(scsi_cdrom_t *dev, uint8_t *cdb)
{
    int ready = 0;

    if ((cdb[0] != GPCMD_REQUEST_SENSE) && (dev->cur_lun == SCSI_LUN_USE_CDB) && (cdb[1] & 0xe0)) {
        scsi_cdrom_log("CD-ROM %i: Attempting to execute a unknown command targeted at SCSI LUN %i\n",
                       dev->id, ((dev->request_length >> 5) & 7));
        scsi_cdrom_invalid_lun(dev);
        return 0;
    }

    if (!(scsi_cdrom_command_flags[cdb[0]] & IMPLEMENTED)) {
        scsi_cdrom_log("CD-ROM %i: Attempting to execute unknown command %02X over %s\n", dev->id, cdb[0],
                       (dev->drv->bus_type == CDROM_BUS_SCSI) ? "SCSI" : "ATAPI");

        scsi_cdrom_illegal_opcode(dev);
        return 0;
    }

    if ((dev->drv->bus_type < CDROM_BUS_SCSI) && (scsi_cdrom_command_flags[cdb[0]] & SCSI_ONLY)) {
        scsi_cdrom_log("CD-ROM %i: Attempting to execute SCSI-only command %02X over ATAPI\n", dev->id, cdb[0]);
        scsi_cdrom_illegal_opcode(dev);
        return 0;
    }

    if ((dev->drv->bus_type == CDROM_BUS_SCSI) && (scsi_cdrom_command_flags[cdb[0]] & ATAPI_ONLY)) {
        scsi_cdrom_log("CD-ROM %i: Attempting to execute ATAPI-only command %02X over SCSI\n", dev->id, cdb[0]);
        scsi_cdrom_illegal_opcode(dev);
        return 0;
    }

    if ((dev->drv->cd_status == CD_STATUS_PLAYING) || (dev->drv->cd_status == CD_STATUS_PAUSED)) {
        ready = 1;
        goto skip_ready_check;
    }

    if (dev->drv->cd_status & CD_STATUS_MEDIUM_CHANGED)
        scsi_cdrom_insert((void *) dev);

    ready = (dev->drv->cd_status != CD_STATUS_EMPTY);

skip_ready_check:
    /* If the drive is not ready, there is no reason to keep the
       UNIT ATTENTION condition present, as we only use it to mark
       disc changes. */
    if (!ready && dev->unit_attention)
        dev->unit_attention = 0;

    /* If the UNIT ATTENTION condition is set and the command does not allow
       execution under it, error out and report the condition. */
    if (dev->unit_attention == 1) {
        /* Only increment the unit attention phase if the command can not pass through it. */
        if (!(scsi_cdrom_command_flags[cdb[0]] & ALLOW_UA)) {
            /* scsi_cdrom_log("CD-ROM %i: Unit attention now 2\n", dev->id); */
            dev->unit_attention++;
            scsi_cdrom_log("CD-ROM %i: UNIT ATTENTION: Command %02X not allowed to pass through\n",
                           dev->id, cdb[0]);
            scsi_cdrom_unit_attention(dev);
            return 0;
        }
    } else if (dev->unit_attention == 2) {
        if (cdb[0] != GPCMD_REQUEST_SENSE) {
            /* scsi_cdrom_log("CD-ROM %i: Unit attention now 0\n", dev->id); */
            dev->unit_attention = 0;
        }
    }

    /* Unless the command is REQUEST SENSE, clear the sense. This will *NOT*
       the UNIT ATTENTION condition if it's set. */
    if (cdb[0] != GPCMD_REQUEST_SENSE)
        scsi_cdrom_sense_clear(dev, cdb[0]);

    /* Next it's time for NOT READY. */
    if (!ready)
        dev->media_status = MEC_MEDIA_REMOVAL;
    else
        dev->media_status = (dev->unit_attention) ? MEC_NEW_MEDIA : MEC_NO_CHANGE;

    if ((scsi_cdrom_command_flags[cdb[0]] & CHECK_READY) && !ready) {
        scsi_cdrom_log("CD-ROM %i: Not ready (%02X)\n", dev->id, cdb[0]);
        scsi_cdrom_not_ready(dev);
        return 0;
    }

    scsi_cdrom_log("CD-ROM %i: Continuing with command %02X\n", dev->id, cdb[0]);

    return 1;
}

static void
scsi_cdrom_rezero(scsi_cdrom_t *dev)
{
    dev->sector_pos = dev->sector_len = 0;
    cdrom_seek(dev->drv, 0, 0);
}

void
scsi_cdrom_reset(scsi_common_t *sc)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) sc;

    if (!dev)
        return;

    scsi_cdrom_rezero(dev);
    dev->status   = 0;
    dev->callback = 0.0;
    scsi_cdrom_set_callback(dev);
    dev->phase          = 1;
    dev->request_length = 0xEB14;
    dev->packet_status  = PHASE_NONE;
    dev->unit_attention = 0xff;
    dev->cur_lun        = SCSI_LUN_USE_CDB;
}

static void
scsi_cdrom_request_sense(scsi_cdrom_t *dev, uint8_t *buffer, uint8_t alloc_length)
{
    /*Will return 18 bytes of 0*/
    if (alloc_length != 0) {
        memset(buffer, 0, alloc_length);
        memcpy(buffer, dev->sense, alloc_length);
    }

    buffer[0] = 0x70;

    if ((scsi_cdrom_sense_key > 0) && (dev->drv->cd_status == CD_STATUS_PLAYING_COMPLETED)) {
        buffer[2]  = SENSE_ILLEGAL_REQUEST;
        buffer[12] = ASC_AUDIO_PLAY_OPERATION;
        buffer[13] = ASCQ_AUDIO_PLAY_OPERATION_COMPLETED;
    } else if ((scsi_cdrom_sense_key == 0) && ((dev->drv->cd_status == CD_STATUS_PAUSED) || ((dev->drv->cd_status >= CD_STATUS_PLAYING) && (dev->drv->cd_status != CD_STATUS_STOPPED)))) {
        buffer[2]  = SENSE_ILLEGAL_REQUEST;
        buffer[12] = ASC_AUDIO_PLAY_OPERATION;
        buffer[13] = (dev->drv->cd_status == CD_STATUS_PLAYING) ? ASCQ_AUDIO_PLAY_OPERATION_IN_PROGRESS : ASCQ_AUDIO_PLAY_OPERATION_PAUSED;
    } else if (dev->unit_attention && (scsi_cdrom_sense_key == 0)) {
        buffer[2]  = SENSE_UNIT_ATTENTION;
        buffer[12] = ASC_MEDIUM_MAY_HAVE_CHANGED;
        buffer[13] = 0;
    }

    scsi_cdrom_log("CD-ROM %i: Reporting sense: %02X %02X %02X\n", dev->id, buffer[2], buffer[12], buffer[13]);

    if (buffer[2] == SENSE_UNIT_ATTENTION) {
        /* If the last remaining sense is unit attention, clear
           that condition. */
        dev->unit_attention = 0;
    }

    /* Clear the sense stuff as per the spec. */
    scsi_cdrom_sense_clear(dev, GPCMD_REQUEST_SENSE);
}

void
scsi_cdrom_request_sense_for_scsi(scsi_common_t *sc, uint8_t *buffer, uint8_t alloc_length)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) sc;

    if (dev->drv->cd_status & CD_STATUS_MEDIUM_CHANGED)
        scsi_cdrom_insert((void *) dev);

    if ((dev->drv->cd_status == CD_STATUS_EMPTY) && dev->unit_attention) {
        /* If the drive is not ready, there is no reason to keep the
           UNIT ATTENTION condition present, as we only use it to mark
           disc changes. */
        dev->unit_attention = 0;
    }

    /* Do *NOT* advance the unit attention phase. */
    scsi_cdrom_request_sense(dev, buffer, alloc_length);
}

static void
scsi_cdrom_set_buf_len(scsi_cdrom_t *dev, int32_t *BufLen, int32_t *src_len)
{
    if (dev->drv->bus_type == CDROM_BUS_SCSI) {
        if (*BufLen == -1)
            *BufLen = *src_len;
        else {
            *BufLen  = MIN(*src_len, *BufLen);
            *src_len = *BufLen;
        }
        scsi_cdrom_log("CD-ROM %i: Actual transfer length: %i\n", dev->id, *BufLen);
    }
}

static void
scsi_cdrom_stop(scsi_common_t *sc)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) sc;

    cdrom_stop(dev->drv);
}

void
scsi_cdrom_command(scsi_common_t *sc, uint8_t *cdb)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) sc;
    int           len;
    int           max_len;
    int           used_len;
    int           alloc_length;
    int           msf;
    int           pos = 0;
    int           size_idx;
    int           idx = 0;
    uint32_t      feature;
    unsigned      preamble_len;
    int           toc_format;
    int           block_desc = 0;
    int           ret;
    int           format                 = 0;
    int           real_pos;
    int           track                  = 0;
    char          device_identify[9]     = { '8', '6', 'B', '_', 'C', 'D', '0', '0', 0 };
    char          device_identify_ex[15] = { '8', '6', 'B', '_', 'C', 'D', '0', '0', ' ', 'v', '1', '.', '0', '0', 0 };
    int32_t       blen                   = 0;
    int32_t      *BufLen;
    uint8_t      *b;
    uint32_t      profiles[2] = { MMC_PROFILE_CD_ROM, MMC_PROFILE_DVD_ROM };
    uint8_t       scsi_bus    = (dev->drv->scsi_device_id >> 4) & 0x0f;
    uint8_t       scsi_id     = dev->drv->scsi_device_id & 0x0f;

    if (dev->drv->bus_type == CDROM_BUS_SCSI) {
        BufLen = &scsi_devices[scsi_bus][scsi_id].buffer_length;
        dev->status &= ~ERR_STAT;
    } else {
        BufLen     = &blen;
        dev->error = 0;
    }

    dev->packet_len  = 0;
    dev->request_pos = 0;

    device_identify[7] = dev->id + 0x30;

    device_identify_ex[7]  = dev->id + 0x30;
    device_identify_ex[10] = EMU_VERSION_EX[0];
    device_identify_ex[12] = EMU_VERSION_EX[2];
    device_identify_ex[13] = EMU_VERSION_EX[3];

    memcpy(dev->current_cdb, cdb, 12);
    dev->sony_vendor = 0;

    if (cdb[0] != 0) {
        scsi_cdrom_log("CD-ROM %i: Command 0x%02X, Sense Key %02X, Asc %02X, Ascq %02X, Unit attention: %i\n",
                       dev->id, cdb[0], scsi_cdrom_sense_key, scsi_cdrom_asc, scsi_cdrom_ascq, dev->unit_attention);
        scsi_cdrom_log("CD-ROM %i: Request length: %04X\n", dev->id, dev->request_length);

        scsi_cdrom_log("CD-ROM %i: CDB: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", dev->id,
                       cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5], cdb[6], cdb[7],
                       cdb[8], cdb[9], cdb[10], cdb[11]);
    }

    msf             = cdb[1] & 2;
    dev->sector_len = 0;

    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);

    /* This handles the Not Ready/Unit Attention check if it has to be handled at this point. */
    if (scsi_cdrom_pre_execution_check(dev, cdb) == 0)
        return;

begin:
    switch (cdb[0]) {
        case GPCMD_TEST_UNIT_READY:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_cdrom_command_complete(dev);
            break;

        case GPCMD_REZERO_UNIT:
            scsi_cdrom_stop(sc);
            dev->sector_pos = dev->sector_len = 0;
            dev->drv->seek_diff               = dev->drv->seek_pos;
            cdrom_seek(dev->drv, 0, 0);
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            break;

        case GPCMD_REQUEST_SENSE:
            /* If there's a unit attention condition and there's a buffered not ready, a standalone REQUEST SENSE
               should forget about the not ready, and report unit attention straight away. */
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
            max_len = cdb[4];

            if (!max_len) {
                scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * CDROM_TIME;
                scsi_cdrom_set_callback(dev);
                break;
            }

            scsi_cdrom_buf_alloc(dev, 256);
            scsi_cdrom_set_buf_len(dev, BufLen, &max_len);
            scsi_cdrom_request_sense(dev, dev->buffer, max_len);
            scsi_cdrom_data_command_finish(dev, 18, 18, cdb[4], 0);
            break;

        case 0xDA: /*GPCMD_SPEED_ALT*/
            switch (dev->drv->type) {
                case CDROM_TYPE_NEC_38_103:
                case CDROM_TYPE_NEC_211_100:
                case CDROM_TYPE_NEC_464_105: /*GPCMD_STILL_NEC*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    cdrom_audio_pause_resume(dev->drv, 0x00);
                    dev->drv->audio_op = 0x01;
                    scsi_cdrom_command_complete(dev);
                    break;
            }
            fallthrough;
        case GPCMD_SET_SPEED:
            dev->drv->cur_speed = (cdb[3] | (cdb[2] << 8)) / 176;
            if (dev->drv->cur_speed < 1)
                dev->drv->cur_speed = 1;
            else if (dev->drv->cur_speed > dev->drv->speed)
                dev->drv->cur_speed = dev->drv->speed;
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_cdrom_command_complete(dev);
            break;

        case 0xCD:
        case GPCMD_AUDIO_SCAN:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);

            if ((dev->drv->host_drive < 1) || (dev->drv->cd_status <= CD_STATUS_DATA_ONLY)) {
                scsi_cdrom_illegal_mode(dev);
                break;
            }

            pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
            ret = cdrom_audio_scan(dev->drv, pos, 0);

            if (ret)
                scsi_cdrom_command_complete(dev);
            else
                scsi_cdrom_illegal_mode(dev);
            break;

        case GPCMD_MECHANISM_STATUS:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
            len = (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];

            scsi_cdrom_buf_alloc(dev, 8);

            scsi_cdrom_set_buf_len(dev, BufLen, &len);

            memset(dev->buffer, 0, 8);
            dev->buffer[5] = 1;

            scsi_cdrom_data_command_finish(dev, 8, 8, len, 0);
            break;

        case GPCMD_READ_TOC_PMA_ATIP:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            max_len = cdb[7];
            max_len <<= 8;
            max_len |= cdb[8];

            scsi_cdrom_buf_alloc(dev, 65536);

            toc_format = cdb[2] & 0xf;

            if (toc_format == 0)
                toc_format = (cdb[9] >> 6) & 3;

            if (!dev->drv->ops) {
                scsi_cdrom_not_ready(dev);
                return;
            }

            if (toc_format < 3) {
                len = cdrom_read_toc(dev->drv, dev->buffer, toc_format, cdb[6], msf, max_len);
                if (len == -1) {
                    /* If the returned length is -1, this means cdrom_read_toc() has encountered an error. */
                    scsi_cdrom_invalid_field(dev);
                    scsi_cdrom_buf_free(dev);
                    return;
                }
            } else {
                scsi_cdrom_invalid_field(dev);
                scsi_cdrom_buf_free(dev);
                return;
            }
            scsi_cdrom_set_buf_len(dev, BufLen, &len);

            scsi_cdrom_data_command_finish(dev, len, len, len, 0);
            return;

        case 0xC7:
            switch (dev->drv->type) {
                case CDROM_TYPE_MATSHITA_501_10b: /*GPCMD_PLAY_AUDIO_MSF_MATSUSHITA*/
                    cdb[0]              = GPCMD_PLAY_AUDIO_MSF;
                    dev->current_cdb[0] = cdb[0];
                    goto begin;
                    break;
                case CDROM_TYPE_DEC_RRD45_0436:
                case CDROM_TYPE_SONY_CDU541_10i:
                case CDROM_TYPE_SONY_CDU561_18k:
                case CDROM_TYPE_SONY_CDU76S_100:
                case CDROM_TYPE_TEXEL_DMXX24_100: /*GPCMD_PLAY_MSF_SONY*/
                    cdb[0] = GPCMD_PLAY_AUDIO_MSF;
                    dev->current_cdb[0] = cdb[0];
                    dev->sony_vendor    = 1;
                    goto begin;
                    break;
                case CDROM_TYPE_TOSHIBA_XM_3433:
                case CDROM_TYPE_TOSHIBA_XM3201B_3232:
                case CDROM_TYPE_TOSHIBA_XM3301TA_0272:
                case CDROM_TYPE_TOSHIBA_XM5701TA_3136:
                case CDROM_TYPE_TOSHIBA_SDM1401_1008: /*GPCMD_READ_DISC_INFORMATION_TOSHIBA*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
                    scsi_cdrom_buf_alloc(dev, 4);

                    if (!dev->drv->ops) {
                        scsi_cdrom_not_ready(dev);
                        return;
                    }

                    ret = cdrom_read_disc_info_toc(dev->drv, dev->buffer, cdb[2], cdb[1] & 3);
                    len = 4;
                    if (!ret) {
                        scsi_cdrom_invalid_field(dev);
                        scsi_cdrom_buf_free(dev);
                        return;
                    }

                    scsi_cdrom_set_buf_len(dev, BufLen, &len);
                    scsi_cdrom_data_command_finish(dev, len, len, len, 0);
                    return;
                default:
                    scsi_cdrom_illegal_opcode(dev);
                    break;
            }
            break;

        case 0xDE:
            switch (dev->drv->type) {
                case CDROM_TYPE_NEC_38_103:
                case CDROM_TYPE_NEC_211_100:
                case CDROM_TYPE_NEC_464_105: /*GPCMD_READ_DISC_INFORMATION_NEC*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
                    scsi_cdrom_buf_alloc(dev, 4);

                    if (!dev->drv->ops) {
                        scsi_cdrom_not_ready(dev);
                        return;
                    }

                    ret = cdrom_read_disc_info_toc(dev->drv, dev->buffer, cdb[2], cdb[1] & 3);
                    len = 4;
                    if (!ret) {
                        scsi_cdrom_invalid_field(dev);
                        scsi_cdrom_buf_free(dev);
                        return;
                    }

                    scsi_cdrom_set_buf_len(dev, BufLen, &len);
                    scsi_cdrom_data_command_finish(dev, len, len, len, 0);
                    return;
                default:
                    scsi_cdrom_illegal_opcode(dev);
                    break;
            }
            break;
        case GPCMD_READ_CD_OLD:
            /* IMPORTANT: Convert the command to new read CD
                          for pass through purposes. */
            dev->current_cdb[0] = GPCMD_READ_CD;
            fallthrough;

        case GPCMD_READ_6:
        case GPCMD_READ_10:
        case GPCMD_READ_12:
        case GPCMD_READ_CD:
        case GPCMD_READ_CD_MSF:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
            alloc_length = 2048;

            switch (cdb[0]) {
                case GPCMD_READ_6:
                    dev->sector_len = cdb[4];
                    if (dev->sector_len == 0)
                        dev->sector_len = 256; /* For READ (6) and WRITE (6), a length of 0 indicates a transfer of 256 sector. */
                    dev->sector_pos = ((((uint32_t) cdb[1]) & 0x1f) << 16) | (((uint32_t) cdb[2]) << 8) | ((uint32_t) cdb[3]);
                    msf             = 0;
                    break;
                case GPCMD_READ_10:
                    dev->sector_len = (cdb[7] << 8) | cdb[8];
                    dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    scsi_cdrom_log("CD-ROM %i: Length: %i, LBA: %i\n", dev->id, dev->sector_len,
                                   dev->sector_pos);
                    msf = 0;
                    break;
                case GPCMD_READ_12:
                    dev->sector_len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) | (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
                    dev->sector_pos = (((uint32_t) cdb[2]) << 24) | (((uint32_t) cdb[3]) << 16) | (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
                    scsi_cdrom_log("CD-ROM %i: Length: %i, LBA: %i\n", dev->id, dev->sector_len,
                                   dev->sector_pos);
                    msf = 0;
                    break;
                case GPCMD_READ_CD_MSF:
                    alloc_length    = 2856;
                    dev->sector_len = MSFtoLBA(cdb[6], cdb[7], cdb[8]);
                    dev->sector_pos = MSFtoLBA(cdb[3], cdb[4], cdb[5]);

                    dev->sector_len -= dev->sector_pos;
                    dev->sector_len++;

                    msf = 1;

                    if ((cdb[9] & 0xf8) == 0x08) {
                        /* 0x08 is an illegal mode */
                        scsi_cdrom_invalid_field(dev);
                        return;
                    }

                    /* If all the flag bits are cleared, then treat it as a non-data command. */
                    if ((cdb[9] == 0x00) && ((cdb[10] & 0x07) == 0x00))
                        dev->sector_len = 0;
                    else if ((cdb[9] == 0x00) && ((cdb[10] & 0x07) != 0x00)) {
                        scsi_cdrom_invalid_field(dev);
                        return;
                    }
                    break;
                case GPCMD_READ_CD_OLD:
                case GPCMD_READ_CD:
                    alloc_length    = 2856;
                    dev->sector_len = (cdb[6] << 16) | (cdb[7] << 8) | cdb[8];
                    dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];

                    msf = 0;

                    if ((cdb[9] & 0xf8) == 0x08) {
                        /* 0x08 is an illegal mode */
                        scsi_cdrom_invalid_field(dev);
                        return;
                    }

                    /* If all the flag bits are cleared, then treat it as a non-data command. */
                    if ((cdb[9] == 0x00) && ((cdb[10] & 0x07) == 0x00))
                        dev->sector_len = 0;
                    else if ((cdb[9] == 0x00) && ((cdb[10] & 0x07) != 0x00)) {
                        scsi_cdrom_invalid_field(dev);
                        return;
                    }
                    break;

                default:
                    break;
            }

            if (!dev->sector_len) {
                scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                /* scsi_cdrom_log("CD-ROM %i: All done - callback set\n", dev->id); */
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * CDROM_TIME;
                scsi_cdrom_set_callback(dev);
                break;
            }

            max_len               = dev->sector_len;
            dev->requested_blocks = max_len; /* If we're reading all blocks in one go for DMA, why not also for PIO, it should NOT
                                                matter anyway, this step should be identical and only the way the read dat is
                                                transferred to the host should be different. */

            dev->packet_len = max_len * alloc_length;
            scsi_cdrom_buf_alloc(dev, dev->packet_len);

            dev->drv->seek_diff = ABS((int) (pos - dev->sector_pos));

            if ((cdb[0] == GPCMD_READ_10) || (cdb[0] == GPCMD_READ_12)) {
                switch (dev->drv->type) {
                    case CDROM_TYPE_NEC_38_103:
                    case CDROM_TYPE_NEC_211_100:
                    case CDROM_TYPE_NEC_464_105:
                    case CDROM_TYPE_TOSHIBA_XM_3433:
                    case CDROM_TYPE_TOSHIBA_XM3201B_3232:
                    case CDROM_TYPE_TOSHIBA_XM3301TA_0272:
                    case CDROM_TYPE_TOSHIBA_XM5701TA_3136:
                    case CDROM_TYPE_TOSHIBA_SDM1401_1008:
                        ret = scsi_cdrom_read_blocks(dev, &alloc_length, 1, cdb[9] & 0xc0);
                        break;
                    default:
                        ret = scsi_cdrom_read_blocks(dev, &alloc_length, 1, 0);
                        break;
                }
            } else
                ret = scsi_cdrom_read_blocks(dev, &alloc_length, 1, 0);

            if (ret <= 0) {
                scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * CDROM_TIME;
                scsi_cdrom_set_callback(dev);
                scsi_cdrom_buf_free(dev);
                return;
            }

            dev->requested_blocks = max_len;
            dev->packet_len       = alloc_length;

            scsi_cdrom_set_buf_len(dev, BufLen, (int32_t *) &dev->packet_len);

            scsi_cdrom_data_command_finish(dev, alloc_length, alloc_length / dev->requested_blocks,
                                           alloc_length, 0);

            if (dev->packet_status != PHASE_COMPLETE)
                ui_sb_update_icon(SB_CDROM | dev->id, 1);
            else
                ui_sb_update_icon(SB_CDROM | dev->id, 0);
            return;

        case GPCMD_READ_HEADER:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            alloc_length = ((cdb[7] << 8) | cdb[8]);
            scsi_cdrom_buf_alloc(dev, 8);

            dev->sector_len = 1;
            dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
            if (msf)
                real_pos = cdrom_lba_to_msf_accurate(dev->sector_pos);
            else
                real_pos = dev->sector_pos;
            dev->buffer[0] = 1; /*2048 bytes user data*/
            dev->buffer[1] = dev->buffer[2] = dev->buffer[3] = 0;
            dev->buffer[4]                                   = (real_pos >> 24);
            dev->buffer[5]                                   = ((real_pos >> 16) & 0xff);
            dev->buffer[6]                                   = ((real_pos >> 8) & 0xff);
            dev->buffer[7]                                   = real_pos & 0xff;

            len = 8;
            len = MIN(len, alloc_length);

            scsi_cdrom_set_buf_len(dev, BufLen, &len);

            scsi_cdrom_data_command_finish(dev, len, len, len, 0);
            return;

        case GPCMD_MODE_SENSE_6:
        case GPCMD_MODE_SENSE_10:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            if (dev->drv->bus_type == CDROM_BUS_SCSI)
                block_desc = ((cdb[1] >> 3) & 1) ? 0 : 1;
            else
                block_desc = 0;

            if (cdb[0] == GPCMD_MODE_SENSE_6) {
                len = cdb[4];
                scsi_cdrom_buf_alloc(dev, 256);
            } else {
                len = (cdb[8] | (cdb[7] << 8));
                scsi_cdrom_buf_alloc(dev, 65536);
            }

            switch (dev->drv->type) {
                case CDROM_TYPE_DEC_RRD45_0436:
                case CDROM_TYPE_SONY_CDU541_10i:
                case CDROM_TYPE_SONY_CDU561_18k:
                case CDROM_TYPE_SONY_CDU76S_100:
                case CDROM_TYPE_TEXEL_DMXX24_100:
                    if (!(scsi_cdrom_mode_sense_page_flags_sony & (1LL << (uint64_t) (cdb[2] & 0x3f)))) {
                        scsi_cdrom_invalid_field(dev);
                        scsi_cdrom_buf_free(dev);
                        return;
                    }
                    break;
                default:
                    if (!(scsi_cdrom_mode_sense_page_flags & (1LL << (uint64_t) (cdb[2] & 0x3f)))) {
                        scsi_cdrom_invalid_field(dev);
                        scsi_cdrom_buf_free(dev);
                        return;
                    }
                    break;
            }
            memset(dev->buffer, 0, len);
            alloc_length = len;

            /* This determines the media type ID to return - this is
               a SCSI/ATAPI-specific thing, so it makes the most sense
               to keep this here.
               Also, the max_len variable is reused as this command
               does otherwise not use it, to avoid having to declare
               another variable. */
            if (dev->drv->cd_status == CD_STATUS_EMPTY)
                max_len = 70; /* No media inserted. */
            else if (dev->drv->cdrom_capacity > CD_MAX_SECTORS)
                max_len = 65; /* DVD. */
            else if (dev->drv->cd_status == CD_STATUS_DATA_ONLY)
                max_len = 1; /* Data CD. */
            else
                max_len = 3; /* Audio or mixed-mode CD. */

            if (cdb[0] == GPCMD_MODE_SENSE_6) {
                len            = scsi_cdrom_mode_sense(dev, dev->buffer, 4, cdb[2], block_desc);
                len            = MIN(len, alloc_length);
                dev->buffer[0] = len - 1;
                dev->buffer[1] = max_len;
                if (block_desc)
                    dev->buffer[3] = 8;
            } else {
                len            = scsi_cdrom_mode_sense(dev, dev->buffer, 8, cdb[2], block_desc);
                len            = MIN(len, alloc_length);
                dev->buffer[0] = (len - 2) >> 8;
                dev->buffer[1] = (len - 2) & 255;
                dev->buffer[2] = max_len;
                if (block_desc) {
                    dev->buffer[6] = 0;
                    dev->buffer[7] = 8;
                }
            }

            scsi_cdrom_set_buf_len(dev, BufLen, &len);

            scsi_cdrom_log("CD-ROM %i: Reading mode page: %02X...\n", dev->id, cdb[2]);

            scsi_cdrom_data_command_finish(dev, len, len, alloc_length, 0);
            return;

        case GPCMD_MODE_SELECT_6:
        case GPCMD_MODE_SELECT_10:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_OUT);

            if (cdb[0] == GPCMD_MODE_SELECT_6) {
                len = cdb[4];
                scsi_cdrom_buf_alloc(dev, 256);
            } else {
                len = (cdb[7] << 8) | cdb[8];
                scsi_cdrom_buf_alloc(dev, 65536);
            }

            scsi_cdrom_set_buf_len(dev, BufLen, &len);

            dev->total_length = len;
            dev->do_page_save = cdb[1] & 1;

            scsi_cdrom_data_command_finish(dev, len, len, len, 1);
            return;

        case GPCMD_GET_CONFIGURATION:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            /* XXX: could result in alignment problems in some architectures */
            feature = (cdb[2] << 8) | cdb[3];
            max_len = (cdb[7] << 8) | cdb[8];

            /* only feature 0 is supported */
            if ((cdb[2] != 0) || (cdb[3] > 2)) {
                scsi_cdrom_invalid_field(dev);
                scsi_cdrom_buf_free(dev);
                return;
            }

            scsi_cdrom_buf_alloc(dev, 65536);
            memset(dev->buffer, 0, max_len);

            alloc_length = 0;
            b            = dev->buffer;

            /*
             * the number of sectors from the media tells us which profile
             * to use as current.  0 means there is no media
             */
            if (dev->drv->cd_status != CD_STATUS_EMPTY) {
                len = dev->drv->cdrom_capacity;
                if (len > CD_MAX_SECTORS) {
                    b[6] = (MMC_PROFILE_DVD_ROM >> 8) & 0xff;
                    b[7] = MMC_PROFILE_DVD_ROM & 0xff;
                    ret  = 1;
                } else {
                    b[6] = (MMC_PROFILE_CD_ROM >> 8) & 0xff;
                    b[7] = MMC_PROFILE_CD_ROM & 0xff;
                    ret  = 0;
                }
            } else
                ret = 2;

            alloc_length = 8;
            b += 8;

            if ((feature == 0) || ((cdb[1] & 3) < 2)) {
                b[2] = (0 << 2) | 0x02 | 0x01; /* persistent and current */
                b[3] = 8;

                alloc_length += 4;
                b += 4;

                for (uint8_t i = 0; i < 2; i++) {
                    b[0] = (profiles[i] >> 8) & 0xff;
                    b[1] = profiles[i] & 0xff;

                    if (ret == i)
                        b[2] |= 1;

                    alloc_length += 4;
                    b += 4;
                }
            }
            if ((feature == 1) || ((cdb[1] & 3) < 2)) {
                b[1] = 1;
                b[2] = (2 << 2) | 0x02 | 0x01; /* persistent and current */
                b[3] = 8;

                if (dev->drv->bus_type == CDROM_BUS_SCSI)
                    b[7] = 1;
                else
                    b[7] = 2;
                b[8] = 1;

                alloc_length += 12;
                b += 12;
            }
            if ((feature == 2) || ((cdb[1] & 3) < 2)) {
                b[1] = 2;
                b[2] = (1 << 2) | 0x02 | 0x01; /* persistent and current */
                b[3] = 4;

                b[4] = 2;

                alloc_length += 8;
                b += 8;
            }

            dev->buffer[0] = ((alloc_length - 4) >> 24) & 0xff;
            dev->buffer[1] = ((alloc_length - 4) >> 16) & 0xff;
            dev->buffer[2] = ((alloc_length - 4) >> 8) & 0xff;
            dev->buffer[3] = (alloc_length - 4) & 0xff;

            alloc_length = MIN(alloc_length, max_len);

            scsi_cdrom_set_buf_len(dev, BufLen, &alloc_length);

            scsi_cdrom_data_command_finish(dev, alloc_length, alloc_length, alloc_length, 0);
            break;

        case GPCMD_GET_EVENT_STATUS_NOTIFICATION:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            scsi_cdrom_buf_alloc(dev, 8 + sizeof(gesn_event_header_t));

            gesn_cdb          = (void *) cdb;
            gesn_event_header = (void *) dev->buffer;

            /* It is fine by the MMC spec to not support async mode operations. */
            if (!(gesn_cdb->polled & 0x01)) {
                /* asynchronous mode */
                /* Only polling is supported, asynchronous mode is not. */
                scsi_cdrom_invalid_field(dev);
                scsi_cdrom_buf_free(dev);
                return;
            }

            /*
             * These are the supported events.
             *
             * We currently only support requests of the 'media' type.
             * Notification class requests and supported event classes are bitmasks,
             * but they are built from the same values as the "notification class"
             * field.
             */
            gesn_event_header->supported_events = 1 << GESN_MEDIA;

            /*
             * We use |= below to set the class field; other bits in this byte
             * are reserved now but this is useful to do if we have to use the
             * reserved fields later.
             */
            gesn_event_header->notification_class = 0;

            /*
             * Responses to requests are to be based on request priority.  The
             * notification_class_request_type enum above specifies the
             * priority: upper elements are higher prio than lower ones.
             */
            if (gesn_cdb->class & (1 << GESN_MEDIA)) {
                gesn_event_header->notification_class |= GESN_MEDIA;

                dev->buffer[4] = dev->media_status; /* Bits 7-4 = Reserved, Bits 4-1 = Media Status */
                dev->buffer[5] = 1;                 /* Power Status (1 = Active) */
                dev->buffer[6] = 0;
                dev->buffer[7] = 0;
                used_len       = 8;
            } else {
                gesn_event_header->notification_class = 0x80; /* No event available */
                used_len                              = sizeof(*gesn_event_header);
            }
            gesn_event_header->len = used_len - sizeof(*gesn_event_header);

            memmove(dev->buffer, gesn_event_header, 4);

            scsi_cdrom_set_buf_len(dev, BufLen, &used_len);

            scsi_cdrom_data_command_finish(dev, used_len, used_len, used_len, 0);
            break;

        case GPCMD_READ_DISC_INFORMATION:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            max_len = cdb[7];
            max_len <<= 8;
            max_len |= cdb[8];

            scsi_cdrom_buf_alloc(dev, 65536);

            memset(dev->buffer, 0, 34);
            memset(dev->buffer, 1, 9);
            dev->buffer[0] = 0;
            dev->buffer[1] = 32;
            dev->buffer[2] = 0xe;  /* last session complete, disc finalized */
            dev->buffer[7] = 0x20; /* unrestricted use */
            dev->buffer[8] = 0x00; /* CD-ROM */

            len = 34;
            len = MIN(len, max_len);

            scsi_cdrom_set_buf_len(dev, BufLen, &len);

            scsi_cdrom_data_command_finish(dev, len, len, len, 0);
            break;

        case GPCMD_READ_TRACK_INFORMATION:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            max_len = cdb[7];
            max_len <<= 8;
            max_len |= cdb[8];

            scsi_cdrom_buf_alloc(dev, 65536);

            track = ((uint32_t) cdb[2]) << 24;
            track |= ((uint32_t) cdb[3]) << 16;
            track |= ((uint32_t) cdb[4]) << 8;
            track |= (uint32_t) cdb[5];

            if (((cdb[1] & 0x03) != 1) || (track != 1)) {
                scsi_cdrom_invalid_field(dev);
                scsi_cdrom_buf_free(dev);
                return;
            }

            len = 36;

            memset(dev->buffer, 0, 36);
            dev->buffer[0] = 0;
            dev->buffer[1] = 34;
            dev->buffer[2] = 1;                                                    /* track number (LSB) */
            dev->buffer[3] = 1;                                                    /* session number (LSB) */
            dev->buffer[5] = (0 << 5) | (0 << 4) | (4 << 0);                       /* not damaged, primary copy, data track */
            dev->buffer[6] = (0 << 7) | (0 << 6) | (0 << 5) | (0 << 6) | (1 << 0); /* not reserved track, not blank, not packet writing, not fixed packet, data mode 1 */
            dev->buffer[7] = (0 << 1) | (0 << 0);                                  /* last recorded address not valid, next recordable address not valid */

            dev->buffer[24] = ((dev->drv->cdrom_capacity - 1) >> 24) & 0xff; /* track size */
            dev->buffer[25] = ((dev->drv->cdrom_capacity - 1) >> 16) & 0xff; /* track size */
            dev->buffer[26] = ((dev->drv->cdrom_capacity - 1) >> 8) & 0xff;  /* track size */
            dev->buffer[27] = (dev->drv->cdrom_capacity - 1) & 0xff;         /* track size */

            if (len > max_len) {
                len            = max_len;
                dev->buffer[0] = ((max_len - 2) >> 8) & 0xff;
                dev->buffer[1] = (max_len - 2) & 0xff;
            }

            scsi_cdrom_set_buf_len(dev, BufLen, &len);

            scsi_cdrom_data_command_finish(dev, len, len, max_len, 0);
            break;

        case 0xC0:
            switch (dev->drv->type) {
                case CDROM_TYPE_DEC_RRD45_0436:
                case CDROM_TYPE_SONY_CDU541_10i:
                case CDROM_TYPE_SONY_CDU561_18k:
                case CDROM_TYPE_SONY_CDU76S_100:
                case CDROM_TYPE_TEXEL_DMXX24_100: /*GPCMD_SET_ADDRESS_FORMAT_SONY*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    dev->sony_vendor    = 1;
                    dev->drv->sony_msf = cdb[8] & 1;
                    scsi_cdrom_command_complete(dev);
                    break;
                case CDROM_TYPE_PIONEER_DRM604X_2403: /*GPCMD_MAGAZINE_EJECT_PIONEER*/
                case CDROM_TYPE_CHINON_CDS431_H42: /*GPCMD_EJECT_CHINON*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    scsi_cdrom_stop(sc);
                    cdrom_eject(dev->id);
                    scsi_cdrom_command_complete(dev);
                    break;
                case CDROM_TYPE_TOSHIBA_XM_3433:
                case CDROM_TYPE_TOSHIBA_XM3201B_3232:
                case CDROM_TYPE_TOSHIBA_XM3301TA_0272:
                case CDROM_TYPE_TOSHIBA_XM5701TA_3136:
                case CDROM_TYPE_TOSHIBA_SDM1401_1008: /*GPCMD_AUDIO_TRACK_SEARCH_TOSHIBA*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    if ((dev->drv->host_drive < 1) || (dev->drv->cd_status <= CD_STATUS_DATA_ONLY)) {
                        scsi_cdrom_illegal_mode(dev);
                        break;
                    }
                    pos                = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    ret                = cdrom_audio_track_search(dev->drv, pos, cdb[9] & 0xc0, cdb[1] & 1);
                    dev->drv->audio_op = (cdb[1] & 1) ? 0x03 : 0x02;

                    if (ret)
                        scsi_cdrom_command_complete(dev);
                    else
                        scsi_cdrom_illegal_mode(dev);
                    break;
                default:
                    scsi_cdrom_illegal_opcode(dev);
                    break;
            }
            break;

        case 0xD8:
            switch (dev->drv->type) {
                case CDROM_TYPE_NEC_38_103:
                case CDROM_TYPE_NEC_211_100:
                case CDROM_TYPE_NEC_464_105: /*GPCMD_AUDIO_TRACK_SEARCH_NEC*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    if ((dev->drv->host_drive < 1) || (dev->drv->cd_status <= CD_STATUS_DATA_ONLY)) {
                        scsi_cdrom_illegal_mode(dev);
                        break;
                    }
                    pos                = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    ret                = cdrom_audio_track_search(dev->drv, pos, cdb[9] & 0xc0, cdb[1] & 1);
                    dev->drv->audio_op = (cdb[1] & 1) ? 0x03 : 0x02;

                    if (ret)
                        scsi_cdrom_command_complete(dev);
                    else
                        scsi_cdrom_illegal_mode(dev);
                    break;
                default:
                    scsi_cdrom_illegal_opcode(dev);
                    break;
            }
            break;

        case 0xC1:
            switch (dev->drv->type) {
                case CDROM_TYPE_DEC_RRD45_0436:
                case CDROM_TYPE_SONY_CDU541_10i:
                case CDROM_TYPE_SONY_CDU561_18k:
                case CDROM_TYPE_SONY_CDU76S_100:
                case CDROM_TYPE_TEXEL_DMXX24_100: /*GPCMD_READ_TOC_SONY*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
                    msf              = dev->ms_pages_saved_sony.pages[GPMODE_CDROM_PAGE_SONY][2] & 0x01;
                    dev->sony_vendor = 1;

                    max_len = cdb[7];
                    max_len <<= 8;
                    max_len |= cdb[8];

                    scsi_cdrom_buf_alloc(dev, 65536);

                    if (!dev->drv->ops) {
                        scsi_cdrom_not_ready(dev);
                        return;
                    }

                    len = cdrom_read_toc_sony(dev->drv, dev->buffer, cdb[5], msf || dev->drv->sony_msf, max_len);
                    if (len == -1) {
                        /* If the returned length is -1, this means cdrom_read_toc_sony() has encountered an error. */
                        scsi_cdrom_invalid_field(dev);
                        scsi_cdrom_buf_free(dev);
                        return;
                    }

                    scsi_cdrom_set_buf_len(dev, BufLen, &len);

                    scsi_cdrom_data_command_finish(dev, len, len, len, 0);
                    return;
                case CDROM_TYPE_PIONEER_DRM604X_2403: /*GPCMD_READ_TOC_PIONEER*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
                    scsi_cdrom_buf_alloc(dev, 4);

                    if (!dev->drv->ops) {
                        scsi_cdrom_not_ready(dev);
                        return;
                    }

                    ret = cdrom_read_disc_info_toc(dev->drv, dev->buffer, cdb[2], cdb[1] & 3);
                    len = 4;
                    if (!ret) {
                        scsi_cdrom_invalid_field(dev);
                        scsi_cdrom_buf_free(dev);
                        return;
                    }

                    scsi_cdrom_set_buf_len(dev, BufLen, &len);
                    scsi_cdrom_data_command_finish(dev, len, len, len, 0);
                    return;
                case CDROM_TYPE_TOSHIBA_XM_3433:
                case CDROM_TYPE_TOSHIBA_XM3201B_3232:
                case CDROM_TYPE_TOSHIBA_XM3301TA_0272:
                case CDROM_TYPE_TOSHIBA_XM5701TA_3136:
                case CDROM_TYPE_TOSHIBA_SDM1401_1008: /*GPCMD_PLAY_AUDIO_TOSHIBA*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    if ((dev->drv->host_drive < 1) || (dev->drv->cd_status <= CD_STATUS_DATA_ONLY)) {
                        scsi_cdrom_illegal_mode(dev);
                        break;
                    }
                    pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    ret = cdrom_audio_play_toshiba(dev->drv, pos, cdb[9] & 0xc0);

                    if (ret)
                        scsi_cdrom_command_complete(dev);
                    else
                        scsi_cdrom_illegal_mode(dev);
                    break;
                default:
                    scsi_cdrom_illegal_opcode(dev);
                    break;
            }
            break;

        case 0xD9:
            switch (dev->drv->type) {
                case CDROM_TYPE_NEC_38_103:
                case CDROM_TYPE_NEC_211_100:
                case CDROM_TYPE_NEC_464_105: /*GPCMD_PLAY_AUDIO_NEC*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    if ((dev->drv->host_drive < 1) || (dev->drv->cd_status <= CD_STATUS_DATA_ONLY)) {
                        scsi_cdrom_illegal_mode(dev);
                        break;
                    }
                    pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    ret = cdrom_audio_play_toshiba(dev->drv, pos, cdb[9] & 0xc0);

                    if (ret)
                        scsi_cdrom_command_complete(dev);
                    else
                        scsi_cdrom_illegal_mode(dev);
                    break;
                default:
                    scsi_cdrom_illegal_opcode(dev);
                    break;
            }
            break;

        case GPCMD_PLAY_AUDIO_10:
        case GPCMD_PLAY_AUDIO_12:
        case GPCMD_PLAY_AUDIO_MSF:
        case GPCMD_PLAY_AUDIO_TRACK_INDEX:
        case GPCMD_PLAY_AUDIO_TRACK_RELATIVE_10:
        case GPCMD_PLAY_AUDIO_TRACK_RELATIVE_12:
            len = 0;

            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);

            switch (cdb[0]) {
                case GPCMD_PLAY_AUDIO_10:
                    msf = 0;
                    pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    len = (cdb[7] << 8) | cdb[8];
                    break;
                case GPCMD_PLAY_AUDIO_12:
                    msf = 0;
                    pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    len = (cdb[6] << 24) | (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];
                    break;
                case GPCMD_PLAY_AUDIO_MSF:
                    msf = 1;
                    pos = (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    len = (cdb[6] << 16) | (cdb[7] << 8) | cdb[8];
                    break;
                case GPCMD_PLAY_AUDIO_TRACK_INDEX:
                    msf = 2;
                    if ((cdb[5] != 1) || (cdb[8] != 1)) {
                        scsi_cdrom_illegal_mode(dev);
                        break;
                    }
                    pos = cdb[4];
                    len = cdb[7];
                    break;
                case GPCMD_PLAY_AUDIO_TRACK_RELATIVE_10:
                    msf = 0x100 | cdb[6];
                    pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    len = (cdb[7] << 8) | cdb[8];
                    break;
                case GPCMD_PLAY_AUDIO_TRACK_RELATIVE_12:
                    msf = 0x100 | cdb[10];
                    pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    len = (cdb[6] << 24) | (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];
                    break;

                default:
                    break;
            }

            if ((dev->drv->host_drive < 1) || (dev->drv->cd_status <= CD_STATUS_DATA_ONLY)) {
                scsi_cdrom_illegal_mode(dev);
                break;
            }

            ret = cdrom_audio_play(dev->drv, pos, len, msf);

            if (ret)
                scsi_cdrom_command_complete(dev);
            else
                scsi_cdrom_illegal_mode(dev);
            break;

        case GPCMD_READ_SUBCHANNEL:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            max_len = cdb[7];
            max_len <<= 8;
            max_len |= cdb[8];
            msf = (cdb[1] >> 1) & 1;

            scsi_cdrom_buf_alloc(dev, 32);

            scsi_cdrom_log("CD-ROM %i: Getting page %i (%s)\n", dev->id, cdb[3], msf ? "MSF" : "LBA");

            if (cdb[3] > 3) {
                /* scsi_cdrom_log("CD-ROM %i: Read subchannel check condition %02X\n", dev->id,
                             cdb[3]); */
                scsi_cdrom_invalid_field(dev);
                scsi_cdrom_buf_free(dev);
                return;
            }

            if (!(cdb[2] & 0x40))
                alloc_length = 4;
            else
                switch (cdb[3]) {
                    case 0:
                        /* SCSI-2: Q-type subchannel, ATAPI: reserved */
                        alloc_length = (dev->drv->bus_type == CDROM_BUS_SCSI) ? 48 : 4;
                        break;
                    case 1:
                        alloc_length = 16;
                        break;
                    default:
                        alloc_length = 24;
                        break;
                }

            len = alloc_length;

            memset(dev->buffer, 0, 24);
            pos                = 0;
            dev->buffer[pos++] = 0;
            dev->buffer[pos++] = 0; /*Audio status*/
            dev->buffer[pos++] = 0;
            dev->buffer[pos++] = 0; /*Subchannel length*/
            /* Mode 0 = Q subchannel mode, first 16 bytes are indentical to mode 1 (current position),
                        the rest are stuff like ISRC etc., which can be all zeroes. */
            if (cdb[3] <= 3) {
                dev->buffer[pos++] = cdb[3]; /*Format code*/

                if (alloc_length != 4) {
                    dev->buffer[1] = cdrom_get_current_subchannel(dev->drv, &dev->buffer[4], msf);
                    dev->buffer[2] = alloc_length - 4;
                }

                switch (dev->drv->cd_status) {
                    case CD_STATUS_PLAYING:
                        dev->buffer[1] = 0x11;
                        break;
                    case CD_STATUS_PAUSED:
                        dev->buffer[1] = (dev->drv->type == CDROM_TYPE_CHINON_CDS431_H42) ? 0x15 : 0x12;
                        break;
                    case CD_STATUS_DATA_ONLY:
                        dev->buffer[1] = (dev->drv->type == CDROM_TYPE_CHINON_CDS431_H42) ? 0x00 : 0x15;
                        break;
                    default:
                        dev->buffer[1] = (dev->drv->type == CDROM_TYPE_CHINON_CDS431_H42) ? 0x00 : 0x13;
                        break;
                }

                scsi_cdrom_log("Audio Status = %02x\n", dev->buffer[1]);
            }

            len = MIN(len, max_len);
            scsi_cdrom_set_buf_len(dev, BufLen, &len);

            scsi_cdrom_data_command_finish(dev, len, len, len, 0);
            break;

        case 0xC6:
            switch (dev->drv->type) {
                case CDROM_TYPE_DEC_RRD45_0436:
                case CDROM_TYPE_SONY_CDU541_10i:
                case CDROM_TYPE_SONY_CDU561_18k:
                case CDROM_TYPE_SONY_CDU76S_100:
                case CDROM_TYPE_TEXEL_DMXX24_100: /*GPCMD_PLAY_TRACK_SONY*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    dev->sony_vendor = 1;

                    msf = 3;
                    if ((cdb[5] != 1) || (cdb[8] != 1)) {
                        scsi_cdrom_illegal_mode(dev);
                        break;
                    }
                    pos = cdb[4];

                    if ((dev->drv->host_drive < 1) || (dev->drv->cd_status <= CD_STATUS_DATA_ONLY)) {
                        scsi_cdrom_illegal_mode(dev);
                        break;
                    }

                    /* In this case, len is unused so just pass a fixed value of 1 intead. */
                    ret = cdrom_audio_play(dev->drv, pos, 1 /*len*/, msf);

                    if (ret)
                        scsi_cdrom_command_complete(dev);
                    else
                        scsi_cdrom_illegal_mode(dev);
                    break;
                case CDROM_TYPE_CHINON_CDS431_H42: /*GPCMD_STOP_CHINON*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    scsi_cdrom_stop(sc);
                    scsi_cdrom_command_complete(dev);
                    break;
                case CDROM_TYPE_TOSHIBA_XM_3433:
                case CDROM_TYPE_TOSHIBA_XM3201B_3232:
                case CDROM_TYPE_TOSHIBA_XM3301TA_0272:
                case CDROM_TYPE_TOSHIBA_XM5701TA_3136:
                case CDROM_TYPE_TOSHIBA_SDM1401_1008: /*GPCMD_READ_SUBCODEQ_PLAYING_STATUS_TOSHIBA*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

                    alloc_length = cdb[1] & 0x1f;
                    len          = 10;

                    if (!dev->drv->ops) {
                        scsi_cdrom_not_ready(dev);
                        return;
                    }

                    if (!alloc_length) {
                        scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                        scsi_cdrom_log("CD-ROM %i: Subcode Q All done - callback set\n", dev->id);
                        dev->packet_status = PHASE_COMPLETE;
                        dev->callback      = 20.0 * CDROM_TIME;
                        scsi_cdrom_set_callback(dev);
                        break;
                    }

                    scsi_cdrom_buf_alloc(dev, len);
                    len = MIN(len, alloc_length);

                    memset(dev->buffer, 0, len);
                    dev->buffer[0] = cdrom_get_current_subcodeq_playstatus(dev->drv, &dev->buffer[1]);
                    scsi_cdrom_log("Audio Status = %02x\n", dev->buffer[0]);

                    scsi_cdrom_set_buf_len(dev, BufLen, &alloc_length);
                    scsi_cdrom_data_command_finish(dev, len, len, len, 0);
                    break;
                default:
                    scsi_cdrom_illegal_opcode(dev);
                    break;
            }
            break;

        case 0xDD:
            switch (dev->drv->type) {
                case CDROM_TYPE_NEC_38_103:
                case CDROM_TYPE_NEC_211_100:
                case CDROM_TYPE_NEC_464_105: /*GPCMD_READ_SUBCODEQ_PLAYING_STATUS_NEC*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

                    alloc_length = cdb[1] & 0x1f;
                    len          = 10;

                    if (!dev->drv->ops) {
                        scsi_cdrom_not_ready(dev);
                        return;
                    }

                    if (!alloc_length) {
                        scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                        scsi_cdrom_log("CD-ROM %i: Subcode Q All done - callback set\n", dev->id);
                        dev->packet_status = PHASE_COMPLETE;
                        dev->callback      = 20.0 * CDROM_TIME;
                        scsi_cdrom_set_callback(dev);
                        break;
                    }

                    scsi_cdrom_buf_alloc(dev, len);
                    len = MIN(len, alloc_length);

                    memset(dev->buffer, 0, len);
                    dev->buffer[0] = cdrom_get_current_subcodeq_playstatus(dev->drv, &dev->buffer[1]);
                    scsi_cdrom_log("Audio Status = %02x\n", dev->buffer[0]);

                    scsi_cdrom_set_buf_len(dev, BufLen, &alloc_length);
                    scsi_cdrom_data_command_finish(dev, len, len, len, 0);
                    break;
                default:
                    scsi_cdrom_illegal_opcode(dev);
                    break;
            }
            break;

        case GPCMD_READ_DVD_STRUCTURE:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            alloc_length = (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);

            scsi_cdrom_buf_alloc(dev, alloc_length);

            if ((cdb[7] < 0xc0) && (dev->drv->cdrom_capacity <= CD_MAX_SECTORS)) {
                scsi_cdrom_incompatible_format(dev);
                scsi_cdrom_buf_free(dev);
                return;
            }

            memset(dev->buffer, 0, alloc_length);

            if ((cdb[7] <= 0x7f) || (cdb[7] == 0xff)) {
                if (cdb[1] == 0) {
                    ret            = scsi_cdrom_read_dvd_structure(dev, format, cdb, dev->buffer);
                    dev->buffer[0] = (ret >> 8);
                    dev->buffer[1] = (ret & 0xff);
                    dev->buffer[2] = dev->buffer[3] = 0x00;
                    if (ret) {
                        scsi_cdrom_set_buf_len(dev, BufLen, &alloc_length);
                        scsi_cdrom_data_command_finish(dev, alloc_length, alloc_length,
                                                       alloc_length, 0);
                    } else
                        scsi_cdrom_buf_free(dev);
                    return;
                }
            } else {
                scsi_cdrom_invalid_field(dev);
                scsi_cdrom_buf_free(dev);
                return;
            }
            break;

        case 0x26:
            if (dev->drv->type == CDROM_TYPE_CHINON_CDS431_H42) { /*GPCMD_UNKNOWN_CHINON*/
                scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                scsi_cdrom_stop(sc);
                scsi_cdrom_command_complete(dev);
            } else {
                scsi_cdrom_illegal_opcode(dev);
            }
            break;

        case GPCMD_START_STOP_UNIT:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);

            switch (cdb[4] & 3) {
                case 0: /* Stop the disc. */
                    scsi_cdrom_stop(sc);
                    break;
                case 1: /* Start the disc and read the TOC. */
                    /* This makes no sense under emulation as this would do
                       absolutely nothing, so just break. */
                    break;
                case 2: /* Eject the disc if possible. */
                    scsi_cdrom_stop(sc);
                    cdrom_eject(dev->id);
                    break;
                case 3: /* Load the disc (close tray). */
                    cdrom_reload(dev->id);
                    break;

                default:
                    break;
            }

            scsi_cdrom_command_complete(dev);
            break;

        case 0xC4:
            switch (dev->drv->type) {
                case CDROM_TYPE_MATSHITA_501_10b: /*GPCMD_READ_HEADER_MATSUSHITA*/
                    cdb[0]              = GPCMD_READ_HEADER;
                    dev->current_cdb[0] = cdb[0];
                    goto begin;
                    break;
                case CDROM_TYPE_DEC_RRD45_0436:
                case CDROM_TYPE_SONY_CDU541_10i:
                case CDROM_TYPE_SONY_CDU561_18k:
                case CDROM_TYPE_SONY_CDU76S_100:
                case CDROM_TYPE_TEXEL_DMXX24_100: /*GPCMD_PLAYBACK_STATUS_SONY*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
                    dev->sony_vendor = 1;

                    max_len = cdb[7];
                    max_len <<= 8;
                    max_len |= cdb[8];
                    msf = dev->ms_pages_saved_sony.pages[GPMODE_CDROM_PAGE_SONY][2] & 0x01;

                    scsi_cdrom_buf_alloc(dev, 18);

                    len = 18;

                    memset(dev->buffer, 0, 18);
                    dev->buffer[0] = 0x00;                                                        /*Reserved*/
                    dev->buffer[1] = 0x00;                                                        /*Reserved*/
                    dev->buffer[2] = 0x00;                                                        /*Audio Status data length*/
                    dev->buffer[3] = 0x00;                                                        /*Audio Status data length*/
                    dev->buffer[4] = cdrom_get_audio_status_sony(dev->drv, &dev->buffer[6], msf || dev->drv->sony_msf); /*Audio status*/
                    dev->buffer[5] = 0x00;

                    scsi_cdrom_log("Audio Status = %02x\n", dev->buffer[4]);

                    len = MIN(len, max_len);
                    scsi_cdrom_set_buf_len(dev, BufLen, &len);

                    scsi_cdrom_data_command_finish(dev, len, len, len, 0);
                    break;
                case CDROM_TYPE_TOSHIBA_XM_3433:
                case CDROM_TYPE_TOSHIBA_XM3201B_3232:
                case CDROM_TYPE_TOSHIBA_XM3301TA_0272:
                case CDROM_TYPE_TOSHIBA_XM5701TA_3136:
                case CDROM_TYPE_TOSHIBA_SDM1401_1008: /*GPCMD_CADDY_EJECT_TOSHIBA*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    scsi_cdrom_stop(sc);
                    cdrom_eject(dev->id);
                    scsi_cdrom_command_complete(dev);
                    break;
                default:
                    scsi_cdrom_illegal_opcode(dev);
                    break;
            }
            break;

        case 0xDC:
            switch (dev->drv->type) {
                case CDROM_TYPE_NEC_38_103:
                case CDROM_TYPE_NEC_211_100:
                case CDROM_TYPE_NEC_464_105: /*GPCMD_CADDY_EJECT_NEC*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    scsi_cdrom_stop(sc);
                    cdrom_eject(dev->id);
                    scsi_cdrom_command_complete(dev);
                    break;
                default:
                    scsi_cdrom_illegal_opcode(dev);
                    break;
            }
            break;

        case GPCMD_INQUIRY:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            max_len = cdb[3];
            max_len <<= 8;
            max_len |= cdb[4];

            scsi_cdrom_buf_alloc(dev, 65536);

            if (cdb[1] & 1) {
                preamble_len = 4;
                size_idx     = 3;

                dev->buffer[idx++] = 5;
                dev->buffer[idx++] = cdb[2];
                dev->buffer[idx++] = 0;

                idx++;

                switch (cdb[2]) {
                    case 0x00:
                        dev->buffer[idx++] = 0x00;
                        dev->buffer[idx++] = 0x83;
                        break;
                    case 0x83:
                        if (idx + 24 > max_len) {
                            scsi_cdrom_data_phase_error(dev);
                            scsi_cdrom_buf_free(dev);
                            return;
                        }

                        dev->buffer[idx++] = 0x02;
                        dev->buffer[idx++] = 0x00;
                        dev->buffer[idx++] = 0x00;
                        dev->buffer[idx++] = 20;
                        ide_padstr8(dev->buffer + idx, 20, "53R141"); /* Serial */
                        idx += 20;

                        if (idx + 72 > cdb[4])
                            goto atapi_out;
                        dev->buffer[idx++] = 0x02;
                        dev->buffer[idx++] = 0x01;
                        dev->buffer[idx++] = 0x00;
                        dev->buffer[idx++] = 68;

                        if (dev->drv->type == CDROM_TYPE_86BOX_100)
                            ide_padstr8(dev->buffer + idx, 8, EMU_NAME); /* Vendor */
                        else
                            ide_padstr8(dev->buffer + idx, 8, cdrom_drive_types[dev->drv->type].vendor); /* Vendor */

                        idx += 8;

                        if (dev->drv->type == CDROM_TYPE_86BOX_100)
                            ide_padstr8(dev->buffer + idx, 40, device_identify_ex); /* Product */
                        else
                            ide_padstr8(dev->buffer + idx, 40, cdrom_drive_types[dev->drv->type].model); /* Product */

                        idx += 40;
                        ide_padstr8(dev->buffer + idx, 20, "53R141"); /* Serial */
                        idx += 20;
                        break;

                    default:
                        scsi_cdrom_log("INQUIRY: Invalid page: %02X\n", cdb[2]);
                        scsi_cdrom_invalid_field(dev);
                        scsi_cdrom_buf_free(dev);
                        return;
                }
            } else {
                preamble_len = 5;
                size_idx     = 4;

                memset(dev->buffer, 0, 8);
                dev->buffer[0] = 5;    /*CD-ROM*/
                dev->buffer[1] = 0x80; /*Removable*/

                if (dev->drv->bus_type == CDROM_BUS_SCSI) {
                    dev->buffer[3] = 0x02;
                    switch (dev->drv->type) {
                        case CDROM_TYPE_86BOX_100:
                            dev->buffer[2] = 0x05; /*SCSI-2 compliant*/
                            break;
                        case CDROM_TYPE_CHINON_CDS431_H42:
                        case CDROM_TYPE_DEC_RRD45_0436:
                        case CDROM_TYPE_MATSHITA_501_10b:
                        case CDROM_TYPE_NEC_38_103:
                        case CDROM_TYPE_NEC_211_100:
                        case CDROM_TYPE_SONY_CDU541_10i:
                        case CDROM_TYPE_SONY_CDU76S_100:
                        case CDROM_TYPE_TEAC_CD50_100:
                        case CDROM_TYPE_TEAC_R55S_10R:
                        case CDROM_TYPE_TEXEL_DMXX24_100:
                        case CDROM_TYPE_TOSHIBA_XM3201B_3232:
                            dev->buffer[2] = 0x01;
                            dev->buffer[3] = 0x01; /*SCSI-1 compliant*/
                            break;
                        default:
                            dev->buffer[2] = 0x02; /*SCSI-2 compliant*/
                            break;
                    }
                } else {
                    dev->buffer[2] = 0x00;
                    dev->buffer[3] = 0x21;
                }

                dev->buffer[4] = 31;
                if (dev->drv->bus_type == CDROM_BUS_SCSI) {
                    switch (dev->drv->type) {
                        case CDROM_TYPE_TOSHIBA_XM_3433:
                        case CDROM_TYPE_TOSHIBA_XM3201B_3232:
                        case CDROM_TYPE_TOSHIBA_XM3301TA_0272:
                        case CDROM_TYPE_TOSHIBA_XM5701TA_3136:
                            dev->buffer[4] = 91;   /* Always 91 on Toshiba SCSI-1 (or SCSI-2) CD-ROM drives from 1989-1990*/
                            dev->buffer[7] = 0x88; /* Linked Command and Relative Addressing supported */
                            break;
                        case CDROM_TYPE_PIONEER_DRM604X_2403:
                            dev->buffer[4] = 42;
                            break;
                        default:
                            dev->buffer[6] = 0x01; /* 16-bit transfers supported */
                            dev->buffer[7] = 0x20; /* Wide bus supported */
                            break;
                    }
                }

                if (dev->drv->type == CDROM_TYPE_86BOX_100) {
                    ide_padstr8(dev->buffer + 8, 8, EMU_NAME);          /* Vendor */
                    ide_padstr8(dev->buffer + 16, 16, device_identify); /* Product */
                    ide_padstr8(dev->buffer + 32, 4, EMU_VERSION_EX);   /* Revision */
                } else {
                    ide_padstr8(dev->buffer + 8, 8, cdrom_drive_types[dev->drv->type].vendor);    /* Vendor */
                    ide_padstr8(dev->buffer + 16, 16, cdrom_drive_types[dev->drv->type].model);   /* Product */
                    ide_padstr8(dev->buffer + 32, 4, cdrom_drive_types[dev->drv->type].revision); /* Revision */
                    if (dev->drv->type == CDROM_TYPE_PIONEER_DRM604X_2403) {
                        dev->buffer[36] = 0x20;
                        ide_padstr8(dev->buffer + 37, 10, "1991/01/01"); /* Date */
                    }
                }

                idx = 36;
                if (dev->drv->type == CDROM_TYPE_PIONEER_DRM604X_2403)
                    idx = 47;
                else {
                    switch (dev->drv->type) {
                        case CDROM_TYPE_TOSHIBA_XM_3433:
                        case CDROM_TYPE_TOSHIBA_XM3201B_3232:
                        case CDROM_TYPE_TOSHIBA_XM3301TA_0272:
                        case CDROM_TYPE_TOSHIBA_XM5701TA_3136:
                            idx = 96;
                            break;
                        default:
                            if (max_len == 96) {
                                dev->buffer[4] = 91;
                                idx            = 96;
                            }
                            break;
                    }
                }
            }

atapi_out:
            dev->buffer[size_idx] = idx - preamble_len;
            len                   = idx;

            len = MIN(len, max_len);

            scsi_cdrom_set_buf_len(dev, BufLen, &len);
            scsi_cdrom_log("Inquiry = %d, max = %d, BufLen = %d.\n", len, max_len, *BufLen);

            scsi_cdrom_data_command_finish(dev, len, len, max_len, 0);
            break;

        case 0x0D: /*GPCMD_NO_OPERATION_TOSHIBA and GPCMD_NO_OPERATION_NEC*/
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_cdrom_command_complete(dev);
            break;

        case GPCMD_PREVENT_REMOVAL:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_cdrom_command_complete(dev);
            break;

        case GPCMD_PAUSE_RESUME:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            cdrom_audio_pause_resume(dev->drv, cdb[8] & 0x01);
            dev->drv->audio_op = (cdb[8] & 0x01) ? 0x03 : 0x01;
            scsi_cdrom_command_complete(dev);
            break;

        case 0xC3:
            switch (dev->drv->type) {
                case CDROM_TYPE_MATSHITA_501_10b: /*GPCMD_READ_TOC_MATSUSHITA*/
                    cdb[0]              = GPCMD_READ_TOC_PMA_ATIP;
                    dev->current_cdb[0] = cdb[0];
                    goto begin;
                    break;
                case CDROM_TYPE_DEC_RRD45_0436:
                case CDROM_TYPE_SONY_CDU541_10i:
                case CDROM_TYPE_SONY_CDU561_18k:
                case CDROM_TYPE_SONY_CDU76S_100:
                case CDROM_TYPE_TEXEL_DMXX24_100: /*GPCMD_READ_HEADER_SONY*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
                    dev->sony_vendor = 1;

                    alloc_length = ((cdb[7] << 8) | cdb[8]);
                    scsi_cdrom_buf_alloc(dev, 4);

                    dev->sector_len = 1;
                    dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    real_pos        = cdrom_lba_to_msf_accurate(dev->sector_pos);
                    dev->buffer[0]  = ((real_pos >> 16) & 0xff);
                    dev->buffer[1]  = ((real_pos >> 8) & 0xff);
                    dev->buffer[2]  = real_pos & 0xff;
                    dev->buffer[3]  = 1; /*2048 bytes user data*/

                    len = 4;
                    len = MIN(len, alloc_length);

                    scsi_cdrom_set_buf_len(dev, BufLen, &len);

                    scsi_cdrom_data_command_finish(dev, len, len, len, 0);
                    return;
                case CDROM_TYPE_TOSHIBA_XM_3433:
                case CDROM_TYPE_TOSHIBA_XM3201B_3232:
                case CDROM_TYPE_TOSHIBA_XM3301TA_0272:
                case CDROM_TYPE_TOSHIBA_XM5701TA_3136:
                case CDROM_TYPE_TOSHIBA_SDM1401_1008: /*GPCMD_SET_STOP_TIME_TOSHIBA*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    scsi_cdrom_command_complete(dev);
                    break;
                default:
                    scsi_cdrom_illegal_opcode(dev);
                    break;
            }
            break;

        case 0xDB:
            switch (dev->drv->type) {
                case CDROM_TYPE_NEC_38_103:
                case CDROM_TYPE_NEC_211_100:
                case CDROM_TYPE_NEC_464_105: /*GPCMD_SET_STOP_TIME_NEC*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    scsi_cdrom_command_complete(dev);
                    break;
                default:
                    scsi_cdrom_illegal_opcode(dev);
                    break;
            }
            break;

        case 0xC2:
            switch (dev->drv->type) {
                case CDROM_TYPE_MATSHITA_501_10b: /*GPCMD_READ_SUBCHANNEL_MATSUSHITA*/
                    cdb[0]              = GPCMD_READ_SUBCHANNEL;
                    dev->current_cdb[0] = cdb[0];
                    goto begin;
                    break;
                case CDROM_TYPE_DEC_RRD45_0436:
                case CDROM_TYPE_SONY_CDU541_10i:
                case CDROM_TYPE_SONY_CDU561_18k:
                case CDROM_TYPE_SONY_CDU76S_100:
                case CDROM_TYPE_TEXEL_DMXX24_100: /*GPCMD_READ_SUBCHANNEL_SONY*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
                    dev->sony_vendor = 1;

                    max_len = cdb[7];
                    max_len <<= 8;
                    max_len |= cdb[8];
                    msf = dev->ms_pages_saved_sony.pages[GPMODE_CDROM_PAGE_SONY][2] & 0x01;

                    scsi_cdrom_log("CD-ROM %i: Getting sub-channel type (%s), code-q = %02x\n", dev->id, msf ? "MSF" : "LBA", cdb[2] & 0x40);

                    if (cdb[2] & 0x40) {
                        scsi_cdrom_buf_alloc(dev, 9);
                        memset(dev->buffer, 0, 9);
                        len = 9;
                        cdrom_get_current_subchannel_sony(dev->drv, dev->buffer, msf || dev->drv->sony_msf);
                        len = MIN(len, max_len);
                        scsi_cdrom_set_buf_len(dev, BufLen, &len);
                        scsi_cdrom_data_command_finish(dev, len, len, len, 0);
                    } else {
                        scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                        scsi_cdrom_log("CD-ROM %i: Drive Status All done - callback set\n", dev->id);
                        dev->packet_status = PHASE_COMPLETE;
                        dev->callback      = 20.0 * CDROM_TIME;
                        scsi_cdrom_set_callback(dev);
                    }
                    break;
                case CDROM_TYPE_PIONEER_DRM604X_2403: /*GPCMD_READ_SUBCODEQ_PIONEER*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

                    alloc_length = cdb[1] & 0x1f;
                    len          = 9;

                    if (!dev->drv->ops) {
                        scsi_cdrom_not_ready(dev);
                        return;
                    }

                    if (!alloc_length) {
                        scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                        scsi_cdrom_log("CD-ROM %i: Subcode Q All done - callback set\n", dev->id);
                        dev->packet_status = PHASE_COMPLETE;
                        dev->callback      = 20.0 * CDROM_TIME;
                        scsi_cdrom_set_callback(dev);
                        break;
                    }

                    scsi_cdrom_buf_alloc(dev, len);
                    len = MIN(len, alloc_length);

                    memset(dev->buffer, 0, len);
                    cdrom_get_current_subcodeq(dev->drv, &dev->buffer[1]);
                    scsi_cdrom_log("Audio Status = %02x\n", dev->buffer[0]);

                    scsi_cdrom_set_buf_len(dev, BufLen, &alloc_length);
                    scsi_cdrom_data_command_finish(dev, len, len, len, 0);
                    break;
                case CDROM_TYPE_TOSHIBA_XM_3433:
                case CDROM_TYPE_TOSHIBA_XM3201B_3232:
                case CDROM_TYPE_TOSHIBA_XM3301TA_0272:
                case CDROM_TYPE_TOSHIBA_XM5701TA_3136:
                case CDROM_TYPE_TOSHIBA_SDM1401_1008: /*GPCMD_STILL_TOSHIBA*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    cdrom_audio_pause_resume(dev->drv, 0x00);
                    dev->drv->audio_op = 0x01;
                    scsi_cdrom_command_complete(dev);
                    break;
                default:
                    scsi_cdrom_illegal_opcode(dev);
                    break;
            }
            break;

        case GPCMD_SEEK_6:
        case GPCMD_SEEK_10:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);

            switch (cdb[0]) {
                case GPCMD_SEEK_6:
                    pos = (cdb[2] << 8) | cdb[3];
                    break;
                case GPCMD_SEEK_10:
                    pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    break;

                default:
                    break;
            }
            dev->drv->seek_diff = ABS((int) (pos - dev->drv->seek_pos));
            if (cdb[0] == GPCMD_SEEK_10) {
                switch (dev->drv->type) {
                    case CDROM_TYPE_NEC_38_103:
                    case CDROM_TYPE_NEC_211_100:
                    case CDROM_TYPE_NEC_464_105:
                    case CDROM_TYPE_TOSHIBA_XM_3433:
                    case CDROM_TYPE_TOSHIBA_XM3201B_3232:
                    case CDROM_TYPE_TOSHIBA_XM3301TA_0272:
                    case CDROM_TYPE_TOSHIBA_XM5701TA_3136:
                    case CDROM_TYPE_TOSHIBA_SDM1401_1008:
                        cdrom_seek(dev->drv, pos, cdb[9] & 0xc0);
                        break;
                    default:
                        cdrom_seek(dev->drv, pos, 0);
                        break;
                }
            } else
                cdrom_seek(dev->drv, pos, 0);

            scsi_cdrom_command_complete(dev);
            break;

        case GPCMD_READ_CDROM_CAPACITY:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            scsi_cdrom_buf_alloc(dev, 8);

            /* IMPORTANT: What's returned is the last LBA block. */
            memset(dev->buffer, 0, 8);
            dev->buffer[0] = ((dev->drv->cdrom_capacity - 1) >> 24) & 0xff;
            dev->buffer[1] = ((dev->drv->cdrom_capacity - 1) >> 16) & 0xff;
            dev->buffer[2] = ((dev->drv->cdrom_capacity - 1) >> 8) & 0xff;
            dev->buffer[3] = (dev->drv->cdrom_capacity - 1) & 0xff;
            dev->buffer[6] = 8;
            len            = 8;

            scsi_cdrom_set_buf_len(dev, BufLen, &len);

            scsi_cdrom_data_command_finish(dev, len, len, len, 0);
            break;

        case GPCMD_STOP_PLAY_SCAN:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);

            if (dev->drv->cd_status <= CD_STATUS_DATA_ONLY) {
                scsi_cdrom_illegal_mode(dev);
                break;
            }

            scsi_cdrom_stop(sc);
            scsi_cdrom_command_complete(dev);
            break;

        case 0xC5:
            switch (dev->drv->type) {
                case CDROM_TYPE_MATSHITA_501_10b: /*GPCMD_PLAY_AUDIO_MATSUSHITA*/
                    cdb[0]              = GPCMD_PLAY_AUDIO_10;
                    dev->current_cdb[0] = cdb[0];
                    goto begin;
                    break;
                case CDROM_TYPE_DEC_RRD45_0436:
                case CDROM_TYPE_SONY_CDU541_10i:
                case CDROM_TYPE_SONY_CDU561_18k:
                case CDROM_TYPE_SONY_CDU76S_100:
                case CDROM_TYPE_TEXEL_DMXX24_100: /*GPCMD_PAUSE_SONY*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    dev->sony_vendor = 1;
                    cdrom_audio_pause_resume(dev->drv, !(cdb[1] & 0x10));
                    scsi_cdrom_command_complete(dev);
                    break;
                default:
                    scsi_cdrom_illegal_opcode(dev);
                    break;
            }
            break;

        case 0xC8:
            switch (dev->drv->type) {
                case CDROM_TYPE_MATSHITA_501_10b: /*GPCMD_PLAY_AUDIO_TRACK_INDEX_MATSUSHITA*/
                    cdb[0]              = GPCMD_PLAY_AUDIO_TRACK_INDEX;
                    dev->current_cdb[0] = cdb[0];
                    goto begin;
                    break;
                case CDROM_TYPE_DEC_RRD45_0436:
                case CDROM_TYPE_SONY_CDU541_10i:
                case CDROM_TYPE_SONY_CDU561_18k:
                case CDROM_TYPE_SONY_CDU76S_100:
                case CDROM_TYPE_TEXEL_DMXX24_100: /*GPCMD_PLAY_AUDIO_SONY*/
                    cdb[0] = GPCMD_PLAY_AUDIO_10;
                    dev->current_cdb[0] = cdb[0];
                    dev->sony_vendor    = 1;
                    goto begin;
                    break;
                case CDROM_TYPE_PIONEER_DRM604X_2403: /*GPCMD_AUDIO_TRACK_SEARCH_PIONEER*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    if ((dev->drv->host_drive < 1) || (dev->drv->cd_status <= CD_STATUS_DATA_ONLY)) {
                        scsi_cdrom_illegal_mode(dev);
                        break;
                    }
                    pos                = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    ret                = cdrom_audio_track_search_pioneer(dev->drv, pos, cdb[1] & 1);
                    dev->drv->audio_op = (cdb[1] & 1) ? 0x03 : 0x02;

                    if (ret)
                        scsi_cdrom_command_complete(dev);
                    else
                        scsi_cdrom_illegal_mode(dev);
                    break;
                default:
                    scsi_cdrom_illegal_opcode(dev);
                    break;
            }
            break;

        case 0xC9:
            switch (dev->drv->type) {
                case CDROM_TYPE_MATSHITA_501_10b: /*GPCMD_PLAY_AUDIO_TRACK_RELATIVE_10_MATSUSHITA*/
                    cdb[0]              = GPCMD_PLAY_AUDIO_TRACK_RELATIVE_10;
                    dev->current_cdb[0] = cdb[0];
                    goto begin;
                    break;
                case CDROM_TYPE_DEC_RRD45_0436:
                case CDROM_TYPE_SONY_CDU541_10i:
                case CDROM_TYPE_SONY_CDU561_18k:
                case CDROM_TYPE_SONY_CDU76S_100:
                case CDROM_TYPE_TEXEL_DMXX24_100: /*GPCMD_PLAYBACK_CONTROL_SONY*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_OUT);
                    dev->sony_vendor = 1;

                    len = (cdb[7] << 8) | cdb[8];
                    scsi_cdrom_buf_alloc(dev, 65536);

                    scsi_cdrom_set_buf_len(dev, BufLen, &len);
                    scsi_cdrom_data_command_finish(dev, len, len, len, 1);
                    break;
                case CDROM_TYPE_PIONEER_DRM604X_2403: /*GPCMD_PLAY_AUDIO_PIONEER*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    if ((dev->drv->host_drive < 1) || (dev->drv->cd_status <= CD_STATUS_DATA_ONLY)) {
                        scsi_cdrom_illegal_mode(dev);
                        break;
                    }
                    pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    ret = cdrom_audio_play_pioneer(dev->drv, pos);

                    if (ret)
                        scsi_cdrom_command_complete(dev);
                    else
                        scsi_cdrom_illegal_mode(dev);
                    break;
                default:
                    scsi_cdrom_illegal_opcode(dev);
                    break;
            }
            break;

        case 0xCA:
            if (dev->drv->type == CDROM_TYPE_PIONEER_DRM604X_2403) { /*GPCMD_PAUSE_PIONEER*/
                scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                cdrom_audio_pause_resume(dev->drv, !(cdb[1] & 0x10));
                scsi_cdrom_command_complete(dev);
            } else {
                scsi_cdrom_illegal_opcode(dev);
            }
            break;

        case 0xCB:
            switch (dev->drv->type) {
                case CDROM_TYPE_MATSHITA_501_10b: /*GPCMD_PAUSE_RESUME_MATSUSHITA*/
                    cdb[0]              = GPCMD_PAUSE_RESUME;
                    dev->current_cdb[0] = cdb[0];
                    goto begin;
                    break;
                case CDROM_TYPE_PIONEER_DRM604X_2403: /*GPCMD_STOP_PIONEER*/
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    scsi_cdrom_stop(sc);
                    scsi_cdrom_command_complete(dev);
                    break;
                default:
                    scsi_cdrom_illegal_opcode(dev);
                    break;
            }
            break;

        case 0xCC:
            if (dev->drv->type == CDROM_TYPE_PIONEER_DRM604X_2403) { /*GPCMD_PLAYBACK_STATUS_PIONEER*/
                scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

                max_len = cdb[7];
                max_len <<= 8;
                max_len |= cdb[8];

                scsi_cdrom_buf_alloc(dev, 6);

                len = 6;

                memset(dev->buffer, 0, 6);
                dev->buffer[0] = cdrom_get_audio_status_pioneer(dev->drv, &dev->buffer[1]); /*Audio status*/

                scsi_cdrom_log("Audio Status = %02x\n", dev->buffer[4]);

                len = MIN(len, max_len);
                scsi_cdrom_set_buf_len(dev, BufLen, &len);

                scsi_cdrom_data_command_finish(dev, len, len, len, 0);
            } else {
                scsi_cdrom_illegal_opcode(dev);
            }
            break;

        case 0xE0:
            if (dev->drv->type == CDROM_TYPE_PIONEER_DRM604X_2403) { /*GPCMD_DRIVE_STATUS_PIONEER*/
                scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

                len = (cdb[9] | (cdb[8] << 8));
                scsi_cdrom_buf_alloc(dev, 65536);

                if (!(scsi_cdrom_drive_status_page_flags & (1LL << (uint64_t) (cdb[2] & 0x3f)))) {
                    scsi_cdrom_invalid_field(dev);
                    scsi_cdrom_buf_free(dev);
                    return;
                }

                if (!len) {
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    scsi_cdrom_log("CD-ROM %i: Drive Status All done - callback set\n", dev->id);
                    dev->packet_status = PHASE_COMPLETE;
                    dev->callback      = 20.0 * CDROM_TIME;
                    scsi_cdrom_set_callback(dev);
                    break;
                }

                memset(dev->buffer, 0, len);
                alloc_length = len;

                len = scsi_cdrom_drive_status(dev, dev->buffer, 0, cdb[2]);
                len = MIN(len, alloc_length);

                scsi_cdrom_set_buf_len(dev, BufLen, &len);

                scsi_cdrom_log("CD-ROM %i: Reading drive status page: %02X...\n", dev->id, cdb[2]);

                scsi_cdrom_data_command_finish(dev, len, len, alloc_length, 0);
                return;
            } else {
                scsi_cdrom_illegal_opcode(dev);
            }
            break;

        case 0xE5:
            if (dev->drv->type == CDROM_TYPE_MATSHITA_501_10b) { /*GPCMD_PLAY_AUDIO_12_MATSUSHITA*/
                cdb[0]              = GPCMD_PLAY_AUDIO_12;
                dev->current_cdb[0] = cdb[0];
                goto begin;
            } else {
                scsi_cdrom_illegal_opcode(dev);
            }
            break;

        case 0xE9:
            if (dev->drv->type == CDROM_TYPE_MATSHITA_501_10b) { /*GPCMD_PLAY_AUDIO_TRACK_RELATIVE_12_MATSUSHITA*/
                cdb[0]              = GPCMD_PLAY_AUDIO_TRACK_RELATIVE_12;
                dev->current_cdb[0] = cdb[0];
                goto begin;
            }
            fallthrough;
        default:
            scsi_cdrom_illegal_opcode(dev);
            break;
    }

    /* scsi_cdrom_log("CD-ROM %i: Phase: %02X, request length: %i\n", dev->phase, dev->request_length); */

    if (scsi_cdrom_atapi_phase_to_scsi(dev) == SCSI_PHASE_STATUS)
        scsi_cdrom_buf_free(dev);
}

static void
scsi_cdrom_command_stop(scsi_common_t *sc)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) sc;

    scsi_cdrom_command_complete(dev);
    scsi_cdrom_buf_free(dev);
}

/* The command second phase function, needed for Mode Select. */
static uint8_t
scsi_cdrom_phase_data_out(scsi_common_t *sc)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) sc;
    uint16_t      block_desc_len;
    uint16_t      pos;
    uint16_t      param_list_len;
    uint16_t      i = 0;

    uint8_t error = 0;
    uint8_t page;
    uint8_t page_len;
    uint8_t hdr_len;
    uint8_t val;
    uint8_t old_val;
    uint8_t ch;

    switch (dev->current_cdb[0]) {
        case GPCMD_MODE_SELECT_6:
        case GPCMD_MODE_SELECT_10:
            if (dev->current_cdb[0] == GPCMD_MODE_SELECT_10) {
                hdr_len        = 8;
                param_list_len = dev->current_cdb[7];
                param_list_len <<= 8;
                param_list_len |= dev->current_cdb[8];
            } else {
                hdr_len        = 4;
                param_list_len = dev->current_cdb[4];
            }

            if (dev->drv->bus_type == CDROM_BUS_SCSI) {
                if (dev->current_cdb[0] == GPCMD_MODE_SELECT_6) {
                    block_desc_len = dev->buffer[2];
                    block_desc_len <<= 8;
                    block_desc_len |= dev->buffer[3];
                } else {
                    block_desc_len = dev->buffer[6];
                    block_desc_len <<= 8;
                    block_desc_len |= dev->buffer[7];
                }
            } else
                block_desc_len = 0;

            pos = hdr_len + block_desc_len;

            while (1) {
                if (pos >= param_list_len) {
                    scsi_cdrom_log("CD-ROM %i: Buffer has only block descriptor\n", dev->id);
                    break;
                }

                page     = dev->buffer[pos] & 0x3F;
                page_len = dev->buffer[pos + 1];

                pos += 2;

                switch (dev->drv->type) {
                    case CDROM_TYPE_DEC_RRD45_0436:
                    case CDROM_TYPE_SONY_CDU541_10i:
                    case CDROM_TYPE_SONY_CDU561_18k:
                    case CDROM_TYPE_SONY_CDU76S_100:
                    case CDROM_TYPE_TEXEL_DMXX24_100:
                        if (!(scsi_cdrom_mode_sense_page_flags_sony & (1LL << ((uint64_t) page)))) {
                            scsi_cdrom_log("CD-ROM %i: Unimplemented page %02X\n", dev->id, page);
                            error |= 1;
                        } else {
                            for (i = 0; i < page_len; i++) {
                                ch      = scsi_cdrom_mode_sense_pages_changeable_sony.pages[page][i + 2];
                                val     = dev->buffer[pos + i];
                                old_val = dev->ms_pages_saved_sony.pages[page][i + 2];
                                if (val != old_val) {
                                    if (ch)
                                        dev->ms_pages_saved_sony.pages[page][i + 2] = val;
                                    else {
                                        scsi_cdrom_log("CD-ROM %i: Unchangeable value on position %02X on page %02X\n", dev->id, i + 2, page);
                                        error |= 1;
                                    }
                                }
                            }
                        }
                        break;
                    default:
                        if (!(scsi_cdrom_mode_sense_page_flags & (1LL << ((uint64_t) page)))) {
                            scsi_cdrom_log("CD-ROM %i: Unimplemented page %02X\n", dev->id, page);
                            error |= 1;
                        } else {
                            for (i = 0; i < page_len; i++) {
                                ch      = scsi_cdrom_mode_sense_pages_changeable.pages[page][i + 2];
                                val     = dev->buffer[pos + i];
                                old_val = dev->ms_pages_saved.pages[page][i + 2];
                                if (val != old_val) {
                                    if (ch)
                                        dev->ms_pages_saved.pages[page][i + 2] = val;
                                    else {
                                        scsi_cdrom_log("CD-ROM %i: Unchangeable value on position %02X on page %02X\n", dev->id, i + 2, page);
                                        error |= 1;
                                    }
                                }
                            }
                        }
                        break;
                }

                pos += page_len;

                switch (dev->drv->type) {
                    case CDROM_TYPE_DEC_RRD45_0436:
                    case CDROM_TYPE_SONY_CDU541_10i:
                    case CDROM_TYPE_SONY_CDU561_18k:
                    case CDROM_TYPE_SONY_CDU76S_100:
                    case CDROM_TYPE_TEXEL_DMXX24_100:
                        val = scsi_cdrom_mode_sense_pages_default_sony_scsi.pages[page][0] & 0x80;
                        break;
                    default:
                        if (dev->drv->bus_type == CDROM_BUS_SCSI)
                            val = scsi_cdrom_mode_sense_pages_default_scsi.pages[page][0] & 0x80;
                        else
                            val = scsi_cdrom_mode_sense_pages_default.pages[page][0] & 0x80;
                        break;
                }
                if (dev->do_page_save && val)
                    scsi_cdrom_mode_sense_save(dev);

                if (pos >= dev->total_length)
                    break;
            }

            if (error) {
                scsi_cdrom_invalid_field_pl(dev);
                scsi_cdrom_buf_free(dev);
                return 0;
            }
            break;
        case 0xC9:
            switch (dev->drv->type) {
                case CDROM_TYPE_DEC_RRD45_0436:
                case CDROM_TYPE_SONY_CDU541_10i:
                case CDROM_TYPE_SONY_CDU561_18k:
                case CDROM_TYPE_SONY_CDU76S_100:
                case CDROM_TYPE_TEXEL_DMXX24_100:
                    for (i = 0; i < 18; i++) {
                        dev->ms_pages_saved_sony.pages[GPMODE_CDROM_AUDIO_PAGE_SONY][i] = dev->buffer[i];
                    }
                    break;
                default:
                    break;
            }
            break;

        default:
            break;
    }

    scsi_cdrom_command_stop((scsi_common_t *) dev);
    return 1;
}

static void
scsi_cdrom_close(void *priv)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) priv;

    if (dev)
        free(dev);
}

static int
scsi_cdrom_get_max(int ide_has_dma, int type)
{
    int ret;

    switch (type) {
        case TYPE_PIO:
            ret = ide_has_dma ? 4 : 0;
            break;
        case TYPE_SDMA:
            ret = ide_has_dma ? 2 : -1;
            break;
        case TYPE_MDMA:
            ret = ide_has_dma ? 2 : -1;
            break;
        case TYPE_UDMA:
            ret = ide_has_dma ? 5 : -1;
            break;
        default:
            ret = -1;
            break;
    }

    return ret;
}

static int
scsi_cdrom_get_timings(int ide_has_dma, int type)
{
    int ret;

    switch (type) {
        case TIMINGS_DMA:
            ret = ide_has_dma ? 120 : 0;
            break;
        case TIMINGS_PIO:
            ret = ide_has_dma ? 120 : 0;
            break;
        case TIMINGS_PIO_FC:
            ret = 0;
            break;
        default:
            ret = 0;
            break;
    }

    return ret;
}

/**
 * Fill in ide->buffer with the output of the "IDENTIFY PACKET DEVICE" command
 */
static void
scsi_cdrom_identify(ide_t *ide, int ide_has_dma)
{
    const scsi_cdrom_t *dev;
    char          device_identify[9] = { '8', '6', 'B', '_', 'C', 'D', '0', '0', 0 };

    dev = (scsi_cdrom_t *) ide->sc;

    device_identify[7] = dev->id + 0x30;
    scsi_cdrom_log("ATAPI Identify: %s\n", device_identify);

    if ((dev->drv->type == CDROM_TYPE_NEC_260_100) || (dev->drv->type == CDROM_TYPE_NEC_260_101)) /*NEC only*/
        ide->buffer[0] = 0x8000 | (5 << 8) | 0x80 | (1 << 5); /* ATAPI device, CD-ROM drive, removable media, interrupt DRQ */
    else
        ide->buffer[0] = 0x8000 | (5 << 8) | 0x80 | (2 << 5); /* ATAPI device, CD-ROM drive, removable media, accelerated DRQ */
    ide_padstr((char *) (ide->buffer + 10), "", 20);          /* Serial Number */

    if (dev->drv->type == CDROM_TYPE_86BOX_100) {
        ide_padstr((char *) (ide->buffer + 23), EMU_VERSION_EX, 8);   /* Firmware */
        ide_padstr((char *) (ide->buffer + 27), device_identify, 40); /* Model */
    } else {
        switch (dev->drv->type) {
            case CDROM_TYPE_AZT_CDA46802I_115:
                ide_padstr((char *) (ide->buffer + 23), "1.15    ", 8);                                  /* Firmware */
                ide_padstr((char *) (ide->buffer + 27), "AZT CDA46802I                           ", 40); /* Model */
                break;
            case CDROM_TYPE_BTC_BCD36XH_U10:
                ide_padstr((char *) (ide->buffer + 23), "U1.0    ", 8);                                  /* Firmware */
                ide_padstr((char *) (ide->buffer + 27), "BTC CD-ROM BCD36XH                      ", 40); /* Model */
                break;
            case CDROM_TYPE_GOLDSTAR_CRD_8160B_314:
                ide_padstr((char *) (ide->buffer + 23), "3.14    ", 8);                                  /* Firmware */
                ide_padstr((char *) (ide->buffer + 27), "GOLDSTAR CRD-8160B                      ", 40); /* Model */
                break;
            case CDROM_TYPE_HITACHI_CDR_8130_0020:
                ide_padstr((char *) (ide->buffer + 23), "0020    ", 8);                                  /* Firmware */
                ide_padstr((char *) (ide->buffer + 27), "HITACHI CDR-8130                        ", 40); /* Model */
                break;
            case CDROM_TYPE_KENWOOD_UCR_421_208E:
                ide_padstr((char *) (ide->buffer + 23), "208E    ", 8);                                  /* Firmware */
                ide_padstr((char *) (ide->buffer + 27), "KENWOOD CD-ROM UCR-421                  ", 40); /* Model */
                break;
            case CDROM_TYPE_MATSHITA_587_7S13:
                ide_padstr((char *) (ide->buffer + 23), "7S13    ", 8);                                  /* Firmware */
                ide_padstr((char *) (ide->buffer + 27), "MATSHITA CD-ROM CR-587                  ", 40); /* Model */
                break;
            case CDROM_TYPE_MATSHITA_588_LS15:
                ide_padstr((char *) (ide->buffer + 23), "LS15    ", 8);                                  /* Firmware */
                ide_padstr((char *) (ide->buffer + 27), "MATSHITA CD-ROM CR-588                  ", 40); /* Model */
                break;
            case CDROM_TYPE_MATSHITA_571_10e:
                ide_padstr((char *) (ide->buffer + 23), "1.0e    ", 8);                                  /* Firmware */
                ide_padstr((char *) (ide->buffer + 27), "MATSHITA CR-571                         ", 40); /* Model */
                break;
            case CDROM_TYPE_MATSHITA_572_10j:
                ide_padstr((char *) (ide->buffer + 23), "1.0j    ", 8);                                  /* Firmware */
                ide_padstr((char *) (ide->buffer + 27), "MATSHITA CR-572                         ", 40); /* Model */
                break;
            case CDROM_TYPE_MITSUMI_FX4820T_D02A:
                ide_padstr((char *) (ide->buffer + 23), "D02A    ", 8);                                  /* Firmware */
                ide_padstr((char *) (ide->buffer + 27), "MITSUMI CRMC-FX4820T                    ", 40); /* Model */
                break;
            case CDROM_TYPE_NEC_260_100:
                ide_padstr((char *) (ide->buffer + 23), ".100    ", 8);                                  /* Firmware */
                ide_padstr((char *) (ide->buffer + 27), "EN C                DCR-MOD IREV2:06    ", 40); /* Model */
                break;
            case CDROM_TYPE_NEC_260_101:
                ide_padstr((char *) (ide->buffer + 23), ".110    ", 8);                                  /* Firmware */
                ide_padstr((char *) (ide->buffer + 27), "EN C                DCR-MOD IREV2:06    ", 40); /* Model */
                break;
            case CDROM_TYPE_NEC_273_420:
                ide_padstr((char *) (ide->buffer + 23), "4.20    ", 8);                                  /* Firmware */
                ide_padstr((char *) (ide->buffer + 27), "NEC                 CD-ROM DRIVE:273    ", 40); /* Model */
                break;
            case CDROM_TYPE_NEC_280_105:
                ide_padstr((char *) (ide->buffer + 23), "1.05    ", 8);                                  /* Firmware */
                ide_padstr((char *) (ide->buffer + 27), "NEC                 CD-ROM DRIVE:280    ", 40); /* Model */
                break;
            case CDROM_TYPE_NEC_280_308:
                ide_padstr((char *) (ide->buffer + 23), "3.08    ", 8);                                  /* Firmware */
                ide_padstr((char *) (ide->buffer + 27), "NEC                 CD-ROM DRIVE:280    ", 40); /* Model */
                break;
            case CDROM_TYPE_PHILIPS_PCA403CD_U31P:
                ide_padstr((char *) (ide->buffer + 23), "U31P    ", 8);                                  /* Firmware */
                ide_padstr((char *) (ide->buffer + 27), "PHILIPS CD-ROM PCA403CD                 ", 40); /* Model */
                break;
            case CDROM_TYPE_SONY_CDU76_10i:
                ide_padstr((char *) (ide->buffer + 23), "1.0i    ", 8);                                  /* Firmware */
                ide_padstr((char *) (ide->buffer + 27), "SONY CD-ROM CDU76                       ", 40); /* Model */
                break;
            case CDROM_TYPE_SONY_CDU311_30h:
                ide_padstr((char *) (ide->buffer + 23), "3.0h    ", 8);                                  /* Firmware */
                ide_padstr((char *) (ide->buffer + 27), "SONY CD-ROM CDU311                      ", 40); /* Model */
                break;
            case CDROM_TYPE_TOSHIBA_5302TA_0305:
                ide_padstr((char *) (ide->buffer + 23), "0305    ", 8);                                  /* Firmware */
                ide_padstr((char *) (ide->buffer + 27), "TOSHIBA CD-ROM XM-5302TA                ", 40); /* Model */
                break;
            case CDROM_TYPE_TOSHIBA_5702B_TA70:
                ide_padstr((char *) (ide->buffer + 23), "TA70    ", 8);                                  /* Firmware */
                ide_padstr((char *) (ide->buffer + 27), "TOSHIBA CD-ROM XM-5702B                 ", 40); /* Model */
                break;
        }
    }

    ide->buffer[49]  = 0x200;  /* LBA supported */
    ide->buffer[126] = 0xfffe; /* Interpret zero byte count limit as maximum length */

    if (ide_has_dma) {
        ide->buffer[71] = 30;
        ide->buffer[72] = 30;
        ide->buffer[80] = 0x7e; /*ATA-1 to ATA-6 supported*/
        ide->buffer[81] = 0x19; /*ATA-6 revision 3a supported*/
    }
}

void
scsi_cdrom_drive_reset(int c)
{
    cdrom_t       *drv = &cdrom[c];
    scsi_cdrom_t  *dev;
    scsi_device_t *sd;
    ide_t         *id;
    uint8_t        scsi_bus = (drv->scsi_device_id >> 4) & 0x0f;
    uint8_t        scsi_id  = drv->scsi_device_id & 0x0f;

    if (drv->bus_type == CDROM_BUS_SCSI) {
        /* Make sure to ignore any SCSI CD-ROM drive that has an out of range SCSI bus. */
        if (scsi_bus >= SCSI_BUS_MAX)
            return;

        /* Make sure to ignore any SCSI CD-ROM drive that has an out of range ID. */
        if (scsi_id >= SCSI_ID_MAX)
            return;
    }

    /* Make sure to ignore any ATAPI CD-ROM drive that has an out of range IDE channel. */
    if ((drv->bus_type == CDROM_BUS_ATAPI) && (drv->ide_channel > 7))
        return;

    if (!drv->priv) {
        drv->priv = (scsi_cdrom_t *) malloc(sizeof(scsi_cdrom_t));
        memset(drv->priv, 0, sizeof(scsi_cdrom_t));
    }

    dev = (scsi_cdrom_t *) drv->priv;

    dev->id  = c;
    dev->drv = drv;

    dev->cur_lun = SCSI_LUN_USE_CDB;

    drv->insert      = scsi_cdrom_insert;
    drv->get_volume  = scsi_cdrom_get_volume;
    drv->get_channel = scsi_cdrom_get_channel;
    drv->close       = scsi_cdrom_close;

    scsi_cdrom_init(dev);

    if (drv->bus_type == CDROM_BUS_SCSI) {
        /* SCSI CD-ROM, attach to the SCSI bus. */
        sd = &scsi_devices[scsi_bus][scsi_id];

        sd->sc             = (scsi_common_t *) dev;
        sd->command        = scsi_cdrom_command;
        sd->request_sense  = scsi_cdrom_request_sense_for_scsi;
        sd->reset          = scsi_cdrom_reset;
        sd->phase_data_out = scsi_cdrom_phase_data_out;
        sd->command_stop   = scsi_cdrom_command_stop;
        sd->type           = SCSI_REMOVABLE_CDROM;

        scsi_cdrom_log("SCSI CD-ROM drive %i attached to SCSI ID %i\n", c, cdrom[c].scsi_device_id);
    } else if (drv->bus_type == CDROM_BUS_ATAPI) {
        /* ATAPI CD-ROM, attach to the IDE bus. */
        id = ide_get_drive(drv->ide_channel);
        /* If the IDE channel is initialized, we attach to it,
           otherwise, we do nothing - it's going to be a drive
           that's not attached to anything. */
        if (id) {
            id->sc               = (scsi_common_t *) dev;
            id->get_max          = scsi_cdrom_get_max;
            id->get_timings      = scsi_cdrom_get_timings;
            id->identify         = scsi_cdrom_identify;
            id->stop             = scsi_cdrom_stop;
            id->packet_command   = scsi_cdrom_command;
            id->device_reset     = scsi_cdrom_reset;
            id->phase_data_out   = scsi_cdrom_phase_data_out;
            id->command_stop     = scsi_cdrom_command_stop;
            id->bus_master_error = scsi_cdrom_bus_master_error;
            id->interrupt_drq    = ((dev->drv->type == CDROM_TYPE_NEC_260_100) ||
                                    (dev->drv->type == CDROM_TYPE_NEC_260_101));

            ide_atapi_attach(id);
        }

        scsi_cdrom_log("ATAPI CD-ROM drive %i attached to IDE channel %i\n", c, cdrom[c].ide_channel);
    }
}
