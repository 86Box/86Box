/* Copyright holders: SA1988
   see COPYING for more details
*/
/* ATAPI/SCSI Commands */
#define TEST_UNIT_READY			0x00
#define REQUEST_SENSE         	0x03
#define READ_6      			0x08
#define INQUIRY					0x12
#define MODE_SELECT				0x15
#define MODE_SENSE				0x1a /*actually MODE_SENSE_6 but I want to match QEMU's scheme*/
#define START_STOP            	0x1b /*LOAD_UNLOAD is the same*/
#define ALLOW_MEDIUM_REMOVAL 	0x1e
#define READ_CAPACITY_10      	0x25
#define READ_10					0x28
#define SEEK_10					0x2b
#define READ_SUBCHANNEL			0x42 /*unimplemented on QEMU, so I came up with this name for this command*/
#define READ_TOC				0x43
#define READ_HEADER				0x44 /*unimplemented on QEMU, so I came up with this name for this command*/
#define	PLAY_AUDIO_10	        0x45
#define GET_CONFIGURATION		0x46
#define PLAY_AUDIO_MSF			0x47
#define GET_EVENT_NOTIFICATION	0x4a
#define PAUSE_RESUME			0x4b
#define STOP_PLAY_SCAN		0x4e
#define READ_DISC_INFORMATION	0x51
#define MODE_SELECT_10			0x55
#define MODE_SENSE_10			0x5a
#define PLAY_AUDIO_12			0xa5 /*deprecated*/
#define READ_12					0xa8
#define READ_DVD_STRUCTURE		0xad
#define SET_CD_SPEED			0xbb
#define MECHANISM_STATUS		0xbd
#define READ_CD					0xbe
#define SEND_DVD_STRUCTURE		0xbf

/* SCSI Sense Keys */
#define SENSE_NONE				0
#define SENSE_NOT_READY			2
#define SENSE_ILLEGAL_REQUEST	5
#define SENSE_UNIT_ATTENTION	6

/* SCSI Additional Sense Codes */
#define ASC_ILLEGAL_OPCODE		0x20
#define	ASC_INV_FIELD_IN_CMD_PACKET	0x24
#define ASC_MEDIUM_MAY_HAVE_CHANGED	0x28
#define ASC_MEDIUM_NOT_PRESENT		0x3a

/* Mode page codes for mode sense/set */
#define GPMODE_R_W_ERROR_PAGE		0x01
#define GPMODE_CDROM_PAGE		0x0d
#define GPMODE_CDROM_AUDIO_PAGE		0x0e
#define GPMODE_CAPABILITIES_PAGE	0x2a
#define GPMODE_ALL_PAGES		0x3f

/*
 *  SCSI Status codes
*/
#define GOOD                 0x00
#define CHECK_CONDITION      0x02
#define CONDITION_GOOD       0x04
#define BUSY                 0x08
#define INTERMEDIATE_GOOD    0x10
#define INTERMEDIATE_C_GOOD  0x14
#define RESERVATION_CONFLICT 0x18
#define COMMAND_TERMINATED   0x22
#define TASK_SET_FULL        0x28
#define ACA_ACTIVE           0x30
#define TASK_ABORTED         0x40

#define STATUS_MASK          0x3e

/* Event notification classes for GET EVENT STATUS NOTIFICATION */
#define GESN_NO_EVENTS                0
#define GESN_OPERATIONAL_CHANGE       1
#define GESN_POWER_MANAGEMENT         2
#define GESN_EXTERNAL_REQUEST         3
#define GESN_MEDIA                    4
#define GESN_MULTIPLE_HOSTS           5
#define GESN_DEVICE_BUSY              6

/* Event codes for MEDIA event status notification */
#define MEC_NO_CHANGE                 0
#define MEC_EJECT_REQUESTED           1
#define MEC_NEW_MEDIA                 2
#define MEC_MEDIA_REMOVAL             3 /* only for media changers */
#define MEC_MEDIA_CHANGED             4 /* only for media changers */
#define MEC_BG_FORMAT_COMPLETED       5 /* MRW or DVD+RW b/g format completed */
#define MEC_BG_FORMAT_RESTARTED       6 /* MRW or DVD+RW b/g format restarted */
#define MS_TRAY_OPEN                  1
#define MS_MEDIA_PRESENT              2

#define CHECK_READY	2
#define ALLOW_UA	1