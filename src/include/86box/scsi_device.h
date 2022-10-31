/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the generic SCSI device command handler.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 */

#ifndef SCSI_DEVICE_H
#define SCSI_DEVICE_H

/* Configuration. */

#define SCSI_LUN_USE_CDB 0xff

#ifdef WALTJE
#    define SCSI_TIME 50.0
#else
#    define SCSI_TIME 500.0
#endif

/* Bits of 'status' */
#define ERR_STAT     0x01
#define DRQ_STAT     0x08 /* Data request */
#define DSC_STAT     0x10
#define SERVICE_STAT 0x10
#define READY_STAT   0x40
#define BUSY_STAT    0x80

/* Bits of 'error' */
#define ABRT_ERR 0x04 /* Command aborted */
#define MCR_ERR  0x08 /* Media change request */

/* SCSI commands. */
#define GPCMD_TEST_UNIT_READY               0x00
#define GPCMD_REZERO_UNIT                   0x01
#define GPCMD_REQUEST_SENSE                 0x03
#define GPCMD_FORMAT_UNIT                   0x04
#define GPCMD_IOMEGA_SENSE                  0x06
#define GPCMD_READ_6                        0x08
#define GPCMD_WRITE_6                       0x0a
#define GPCMD_SEEK_6                        0x0b
#define GPCMD_IOMEGA_SET_PROTECTION_MODE    0x0c
#define GPCMD_IOMEGA_EJECT                  0x0d /* ATAPI only? */
#define GPCMD_INQUIRY                       0x12
#define GPCMD_VERIFY_6                      0x13
#define GPCMD_MODE_SELECT_6                 0x15
#define GPCMD_SCSI_RESERVE                  0x16
#define GPCMD_SCSI_RELEASE                  0x17
#define GPCMD_MODE_SENSE_6                  0x1a
#define GPCMD_START_STOP_UNIT               0x1b
#define GPCMD_SEND_DIAGNOSTIC               0x1d
#define GPCMD_PREVENT_REMOVAL               0x1e
#define GPCMD_READ_FORMAT_CAPACITIES        0x23
#define GPCMD_READ_CDROM_CAPACITY           0x25
#define GPCMD_CHINON_UNKNOWN                0x26
#define GPCMD_READ_10                       0x28
#define GPCMD_READ_GENERATION               0x29
#define GPCMD_WRITE_10                      0x2a
#define GPCMD_SEEK_10                       0x2b
#define GPCMD_ERASE_10                      0x2c
#define GPCMD_WRITE_AND_VERIFY_10           0x2e
#define GPCMD_VERIFY_10                     0x2f
#define GPCMD_READ_BUFFER                   0x3c
#define GPCMD_WRITE_SAME_10                 0x41
#define GPCMD_READ_SUBCHANNEL               0x42
#define GPCMD_READ_TOC_PMA_ATIP             0x43
#define GPCMD_READ_HEADER                   0x44
#define GPCMD_PLAY_AUDIO_10                 0x45
#define GPCMD_GET_CONFIGURATION             0x46
#define GPCMD_PLAY_AUDIO_MSF                0x47
#define GPCMD_PLAY_AUDIO_TRACK_INDEX        0x48
#define GPCMD_PLAY_AUDIO_TRACK_RELATIVE_10  0x49
#define GPCMD_GET_EVENT_STATUS_NOTIFICATION 0x4a
#define GPCMD_PAUSE_RESUME                  0x4b
#define GPCMD_STOP_PLAY_SCAN                0x4e
#define GPCMD_READ_DISC_INFORMATION         0x51
#define GPCMD_READ_TRACK_INFORMATION        0x52
#define GPCMD_MODE_SELECT_10                0x55
#define GPCMD_MODE_SENSE_10                 0x5a
#define GPCMD_PLAY_AUDIO_12                 0xa5
#define GPCMD_READ_12                       0xa8
#define GPCMD_PLAY_AUDIO_TRACK_RELATIVE_12  0xa9
#define GPCMD_WRITE_12                      0xaa
#define GPCMD_ERASE_12                      0xac
#define GPCMD_READ_DVD_STRUCTURE            0xad /* For reading. */
#define GPCMD_WRITE_AND_VERIFY_12           0xae
#define GPCMD_VERIFY_12                     0xaf
#define GPCMD_PLAY_CD_OLD                   0xb4
#define GPCMD_READ_CD_OLD                   0xb8
#define GPCMD_READ_CD_MSF                   0xb9
#define GPCMD_SCAN                          0xba
#define GPCMD_SET_SPEED                     0xbb
#define GPCMD_PLAY_CD                       0xbc
#define GPCMD_MECHANISM_STATUS              0xbd
#define GPCMD_READ_CD                       0xbe
#define GPCMD_SEND_DVD_STRUCTURE            0xbf /* This is for writing only, irrelevant to 86Box. */
#define GPCMD_CHINON_EJECT                  0xc0 /* Chinon Vendor Unique command */
#define GPCMD_AUDIO_TRACK_SEARCH            0xc0 /* Toshiba Vendor Unique command */
#define GPCMD_TOSHIBA_PLAY_AUDIO            0xc1 /* Toshiba Vendor Unique command */
#define GPCMD_PAUSE_RESUME_ALT              0xc2
#define GPCMD_STILL                         0xc2 /* Toshiba Vendor Unique command */
#define GPCMD_CADDY_EJECT                   0xc4 /* Toshiba Vendor Unique command */
#define GPCMD_CHINON_STOP                   0xc6 /* Chinon Vendor Unique command */
#define GPCMD_READ_SUBCODEQ_PLAYING_STATUS  0xc6 /* Toshiba Vendor Unique command */
#define GPCMD_READ_DISC_INFORMATION_TOSHIBA 0xc7 /* Toshiba Vendor Unique command */
#define GPCMD_SCAN_ALT                      0xcd /* Should be equivalent to 0xba */
#define GPCMD_SET_SPEED_ALT                 0xda /* Should be equivalent to 0xbb */

/* Mode page codes for mode sense/set */
#define GPMODE_R_W_ERROR_PAGE     0x01
#define GPMODE_DISCONNECT_PAGE    0x02 /* Disconnect/reconnect page */
#define GPMODE_FORMAT_DEVICE_PAGE 0x03
#define GPMODE_RIGID_DISK_PAGE    0x04 /* Rigid disk geometry page */
#define GPMODE_FLEXIBLE_DISK_PAGE 0x05
#define GPMODE_CACHING_PAGE       0x08
#define GPMODE_CDROM_PAGE         0x0d
#define GPMODE_CDROM_AUDIO_PAGE   0x0e
#define GPMODE_CAPABILITIES_PAGE  0x2a
#define GPMODE_IOMEGA_PAGE        0x2f
#define GPMODE_UNK_VENDOR_PAGE    0x30
#define GPMODE_ALL_PAGES          0x3f

/* Mode page codes for presence */
#define GPMODEP_R_W_ERROR_PAGE     0x0000000000000002LL
#define GPMODEP_DISCONNECT_PAGE    0x0000000000000004LL
#define GPMODEP_FORMAT_DEVICE_PAGE 0x0000000000000008LL
#define GPMODEP_RIGID_DISK_PAGE    0x0000000000000010LL
#define GPMODEP_FLEXIBLE_DISK_PAGE 0x0000000000000020LL
#define GPMODEP_CACHING_PAGE       0x0000000000000100LL
#define GPMODEP_CDROM_PAGE         0x0000000000002000LL
#define GPMODEP_CDROM_AUDIO_PAGE   0x0000000000004000LL
#define GPMODEP_CAPABILITIES_PAGE  0x0000040000000000LL
#define GPMODEP_IOMEGA_PAGE        0x0000800000000000LL
#define GPMODEP_UNK_VENDOR_PAGE    0x0001000000000000LL
#define GPMODEP_ALL_PAGES          0x8000000000000000LL

/* SCSI Status Codes */
#define SCSI_STATUS_OK              0
#define SCSI_STATUS_CHECK_CONDITION 2

/* SCSI Sense Keys */
#define SENSE_NONE            0
#define SENSE_NOT_READY       2
#define SENSE_ILLEGAL_REQUEST 5
#define SENSE_UNIT_ATTENTION  6

/* SCSI Additional Sense Codes */
#define ASC_NONE                               0x00
#define ASC_AUDIO_PLAY_OPERATION               0x00
#define ASC_NOT_READY                          0x04
#define ASC_ILLEGAL_OPCODE                     0x20
#define ASC_LBA_OUT_OF_RANGE                   0x21
#define ASC_INV_FIELD_IN_CMD_PACKET            0x24
#define ASC_INV_LUN                            0x25
#define ASC_INV_FIELD_IN_PARAMETER_LIST        0x26
#define ASC_WRITE_PROTECTED                    0x27
#define ASC_MEDIUM_MAY_HAVE_CHANGED            0x28
#define ASC_CAPACITY_DATA_CHANGED              0x2A
#define ASC_INCOMPATIBLE_FORMAT                0x30
#define ASC_MEDIUM_NOT_PRESENT                 0x3a
#define ASC_DATA_PHASE_ERROR                   0x4b
#define ASC_ILLEGAL_MODE_FOR_THIS_TRACK        0x64

#define ASCQ_NONE                              0x00
#define ASCQ_UNIT_IN_PROCESS_OF_BECOMING_READY 0x01
#define ASCQ_INITIALIZING_COMMAND_REQUIRED     0x02
#define ASCQ_CAPACITY_DATA_CHANGED             0x09
#define ASCQ_AUDIO_PLAY_OPERATION_IN_PROGRESS  0x11
#define ASCQ_AUDIO_PLAY_OPERATION_PAUSED       0x12
#define ASCQ_AUDIO_PLAY_OPERATION_COMPLETED    0x13

/* Tell RISC OS that we have a 4x CD-ROM drive (600kb/sec data, 706kb/sec raw).
   Not that it means anything */
#define CDROM_SPEED 706 /* 0x2C2 */

#define BUFFER_SIZE (256 * 1024)

#define RW_DELAY    (TIMER_USEC * 500)

/* Some generally useful CD-ROM information */
#define CD_MINS        75   /* max. minutes per CD */
#define CD_SECS        60   /* seconds per minute */
#define CD_FRAMES      75   /* frames per second */
#define CD_FRAMESIZE   2048 /* bytes per frame, "cooked" mode */
#define CD_MAX_BYTES   (CD_MINS * CD_SECS * CD_FRAMES * CD_FRAMESIZE)
#define CD_MAX_SECTORS (CD_MAX_BYTES / 512)

/* Event notification classes for GET EVENT STATUS NOTIFICATION */
#define GESN_NO_EVENTS          0
#define GESN_OPERATIONAL_CHANGE 1
#define GESN_POWER_MANAGEMENT   2
#define GESN_EXTERNAL_REQUEST   3
#define GESN_MEDIA              4
#define GESN_MULTIPLE_HOSTS     5
#define GESN_DEVICE_BUSY        6

/* Event codes for MEDIA event status notification */
#define MEC_NO_CHANGE           0
#define MEC_EJECT_REQUESTED     1
#define MEC_NEW_MEDIA           2
#define MEC_MEDIA_REMOVAL       3 /* only for media changers */
#define MEC_MEDIA_CHANGED       4 /* only for media changers */
#define MEC_BG_FORMAT_COMPLETED 5 /* MRW or DVD+RW b/g format completed */
#define MEC_BG_FORMAT_RESTARTED 6 /* MRW or DVD+RW b/g format restarted */
#define MS_TRAY_OPEN            1
#define MS_MEDIA_PRESENT        2

/*
 * The MMC values are not IDE specific and might need to be moved
 * to a common header if they are also needed for the SCSI emulation
 */

/* Profile list from MMC-6 revision 1 table 91 */
#define MMC_PROFILE_NONE              0x0000
#define MMC_PROFILE_CD_ROM            0x0008
#define MMC_PROFILE_CD_R              0x0009
#define MMC_PROFILE_CD_RW             0x000A
#define MMC_PROFILE_DVD_ROM           0x0010
#define MMC_PROFILE_DVD_R_SR          0x0011
#define MMC_PROFILE_DVD_RAM           0x0012
#define MMC_PROFILE_DVD_RW_RO         0x0013
#define MMC_PROFILE_DVD_RW_SR         0x0014
#define MMC_PROFILE_DVD_R_DL_SR       0x0015
#define MMC_PROFILE_DVD_R_DL_JR       0x0016
#define MMC_PROFILE_DVD_RW_DL         0x0017
#define MMC_PROFILE_DVD_DDR           0x0018
#define MMC_PROFILE_DVD_PLUS_RW       0x001A
#define MMC_PROFILE_DVD_PLUS_R        0x001B
#define MMC_PROFILE_DVD_PLUS_RW_DL    0x002A
#define MMC_PROFILE_DVD_PLUS_R_DL     0x002B
#define MMC_PROFILE_BD_ROM            0x0040
#define MMC_PROFILE_BD_R_SRM          0x0041
#define MMC_PROFILE_BD_R_RRM          0x0042
#define MMC_PROFILE_BD_RE             0x0043
#define MMC_PROFILE_HDDVD_ROM         0x0050
#define MMC_PROFILE_HDDVD_R           0x0051
#define MMC_PROFILE_HDDVD_RAM         0x0052
#define MMC_PROFILE_HDDVD_RW          0x0053
#define MMC_PROFILE_HDDVD_R_DL        0x0058
#define MMC_PROFILE_HDDVD_RW_DL       0x005A
#define MMC_PROFILE_INVALID           0xFFFF

#define EARLY_ONLY                    64
#define SCSI_ONLY                     32
#define ATAPI_ONLY                    16
#define IMPLEMENTED                   8
#define NONDATA                       4
#define CHECK_READY                   2
#define ALLOW_UA                      1

#define MSFtoLBA(m, s, f)             ((((m * 60) + s) * 75) + f)

#define MSG_COMMAND_COMPLETE          0x00

#define BUS_DBP                       0x01
#define BUS_SEL                       0x02
#define BUS_IO                        0x04
#define BUS_CD                        0x08
#define BUS_MSG                       0x10
#define BUS_REQ                       0x20
#define BUS_BSY                       0x40
#define BUS_RST                       0x80
#define BUS_ACK                       0x200
#define BUS_ATN                       0x200
#define BUS_ARB                       0x8000
#define BUS_SETDATA(val)              ((uint32_t) val << 16)
#define BUS_GETDATA(val)              ((val >> 16) & 0xff)
#define BUS_DATAMASK                  0xff0000

#define BUS_IDLE                      (1 << 31)

#define PHASE_IDLE                    0x00
#define PHASE_COMMAND                 0x01
#define PHASE_DATA_IN                 0x02
#define PHASE_DATA_OUT                0x03
#define PHASE_DATA_IN_DMA             0x04
#define PHASE_DATA_OUT_DMA            0x05
#define PHASE_COMPLETE                0x06
#define PHASE_ERROR                   0x80
#define PHASE_NONE                    0xff

#define SCSI_PHASE_DATA_OUT           0
#define SCSI_PHASE_DATA_IN            BUS_IO
#define SCSI_PHASE_COMMAND            BUS_CD
#define SCSI_PHASE_STATUS             (BUS_CD | BUS_IO)
#define SCSI_PHASE_MESSAGE_OUT        (BUS_MSG | BUS_CD)
#define SCSI_PHASE_MESSAGE_IN         (BUS_MSG | BUS_CD | BUS_IO)

#define MODE_SELECT_PHASE_IDLE        0
#define MODE_SELECT_PHASE_HEADER      1
#define MODE_SELECT_PHASE_BLOCK_DESC  2
#define MODE_SELECT_PHASE_PAGE_HEADER 3
#define MODE_SELECT_PHASE_PAGE        4

typedef struct {
    uint8_t pages[0x40][0x40];
} mode_sense_pages_t;

/* This is so we can access the common elements to all SCSI device structs
   without knowing the device type. */
typedef struct scsi_common_s {
    mode_sense_pages_t ms_pages_saved;

    void *p;

    uint8_t *temp_buffer,
        atapi_cdb[16], /* This is atapi_cdb in ATAPI-supporting devices,
                          and pad in SCSI-only devices. */
        current_cdb[16],
        sense[256];

    uint8_t status, phase,
        error, id,
        features, cur_lun,
        pad0, pad1;

    uint16_t request_length, max_transfer_len;

    int requested_blocks, packet_status,
        total_length, do_page_save,
        unit_attention, request_pos,
        old_len, media_status;

    uint32_t sector_pos, sector_len,
        packet_len, pos;

    double callback;
} scsi_common_t;

typedef struct {
    int32_t buffer_length;

    uint8_t  status, phase;
    uint16_t type;

    scsi_common_t *sc;

    void (*command)(scsi_common_t *sc, uint8_t *cdb);
    void (*request_sense)(scsi_common_t *sc, uint8_t *buffer, uint8_t alloc_length);
    void (*reset)(scsi_common_t *sc);
    uint8_t (*phase_data_out)(scsi_common_t *sc);
    void (*command_stop)(scsi_common_t *sc);
} scsi_device_t;

/* These are based on the INQUIRY values. */
#define SCSI_NONE            0x0060
#define SCSI_FIXED_DISK      0x0000
#define SCSI_REMOVABLE_DISK  0x8000
#define SCSI_REMOVABLE_CDROM 0x8005

#ifdef EMU_SCSI_H
extern scsi_device_t scsi_devices[SCSI_BUS_MAX][SCSI_ID_MAX];
#endif /* EMU_SCSI_H */

extern int cdrom_add_error_and_subchannel(uint8_t *b, int real_sector_type);
extern int cdrom_LBAtoMSF_accurate(void);

extern int mode_select_init(uint8_t command, uint16_t pl_length, uint8_t do_save);
extern int mode_select_terminate(int force);
extern int mode_select_write(uint8_t val);

extern uint8_t *scsi_device_sense(scsi_device_t *dev);
extern double   scsi_device_get_callback(scsi_device_t *dev);
extern void     scsi_device_request_sense(scsi_device_t *dev, uint8_t *buffer,
                                          uint8_t alloc_length);
extern void     scsi_device_reset(scsi_device_t *dev);
extern int      scsi_device_present(scsi_device_t *dev);
extern int      scsi_device_valid(scsi_device_t *dev);
extern int      scsi_device_cdb_length(scsi_device_t *dev);
extern void     scsi_device_command_phase0(scsi_device_t *dev, uint8_t *cdb);
extern void     scsi_device_command_phase1(scsi_device_t *dev);
extern void     scsi_device_command_stop(scsi_device_t *dev);
extern void     scsi_device_identify(scsi_device_t *dev, uint8_t lun);
extern void     scsi_device_close_all(void);
extern void     scsi_device_init(void);

extern void    scsi_reset(void);
extern uint8_t scsi_get_bus(void);

#endif /*SCSI_DEVICE_H*/
