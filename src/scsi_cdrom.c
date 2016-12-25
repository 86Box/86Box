/* Copyright holders: SA1988, Tenshi
   see COPYING for more details
*/
/*SCSI CD-ROM emulation*/
#include <stdlib.h>
#include <string.h>
#include "86box.h"
#include "ibm.h"

#include "cdrom.h"
#include "scsi.h"

sector_buffer_t cdrom_sector_buffer;

int cdrom_sector_type, cdrom_sector_flags;
int cdrom_sector_size, cdrom_sector_ismsf;

uint8_t SCSIPhase = SCSI_PHASE_BUS_FREE;
uint8_t SCSIStatus = SCSI_STATUS_OK;

/* Table of all SCSI commands and their flags, needed for the new disc change / not ready handler. */
uint8_t SCSICommandTable[0x100] =
{
	[GPCMD_TEST_UNIT_READY]               = CHECK_READY | NONDATA,
	[GPCMD_REQUEST_SENSE]                 = ALLOW_UA,
	[GPCMD_READ_6]                        = CHECK_READY | READDATA,
	[GPCMD_INQUIRY]                       = ALLOW_UA,
	[GPCMD_MODE_SELECT_6]                 = 0,
	[GPCMD_MODE_SENSE_6]                  = 0,
	[GPCMD_START_STOP_UNIT]               = CHECK_READY,
	[GPCMD_PREVENT_REMOVAL]               = CHECK_READY,
	[GPCMD_READ_CDROM_CAPACITY]           = CHECK_READY,
	[GPCMD_READ_10]                       = CHECK_READY | READDATA,
	[GPCMD_SEEK]                          = CHECK_READY | NONDATA,
	[GPCMD_READ_SUBCHANNEL]               = CHECK_READY,
	[GPCMD_READ_TOC_PMA_ATIP]             = CHECK_READY | ALLOW_UA,		/* Read TOC - can get through UNIT_ATTENTION, per VIDE-CDD.SYS
										   NOTE: The ATAPI reference says otherwise, but I think this is a question of
											 interpreting things right - the UNIT ATTENTION condition we have here
											 is a tradition from not ready to ready, by definition the drive
											 eventually becomes ready, make the condition go away. */
	[GPCMD_READ_HEADER]                   = CHECK_READY,
	[GPCMD_PLAY_AUDIO_10]                 = CHECK_READY,
	[GPCMD_GET_CONFIGURATION]             = ALLOW_UA,
	[GPCMD_PLAY_AUDIO_MSF]                = CHECK_READY,
	[GPCMD_GET_EVENT_STATUS_NOTIFICATION] = ALLOW_UA,
	[GPCMD_PAUSE_RESUME]                  = CHECK_READY,
	[GPCMD_STOP_PLAY_SCAN]                = CHECK_READY,
	[GPCMD_READ_DISC_INFORMATION]         = CHECK_READY,
	[GPCMD_READ_TRACK_INFORMATION]        = CHECK_READY,
	[GPCMD_MODE_SELECT_10]                = 0,
	[GPCMD_MODE_SENSE_10]                 = 0,
	[GPCMD_PLAY_AUDIO_12]                 = CHECK_READY,
	[GPCMD_READ_12]                       = CHECK_READY | READDATA,
	[GPCMD_READ_DVD_STRUCTURE]            = CHECK_READY,
	[GPCMD_READ_CD_MSF]                   = CHECK_READY | READDATA,
	[GPCMD_SET_SPEED]                     = 0,
	[GPCMD_PLAY_CD]                       = CHECK_READY,
	[GPCMD_MECHANISM_STATUS]              = 0,
	[GPCMD_READ_CD]                       = CHECK_READY | READDATA,
	[GPCMD_SEND_DVD_STRUCTURE]			  = CHECK_READY
};

uint8_t mode_sense_pages[0x40] =
{
	[GPMODE_R_W_ERROR_PAGE]    = IMPLEMENTED,
	[GPMODE_CDROM_PAGE]        = IMPLEMENTED,
	[GPMODE_CDROM_AUDIO_PAGE]  = IMPLEMENTED,
	[GPMODE_CAPABILITIES_PAGE] = IMPLEMENTED,
	[GPMODE_ALL_PAGES]         = IMPLEMENTED
};

uint8_t page_flags[256] =
{
	[GPMODE_R_W_ERROR_PAGE]    = 0,
	[GPMODE_CDROM_PAGE]        = 0,
	[GPMODE_CDROM_AUDIO_PAGE]  = PAGE_CHANGEABLE,
	[GPMODE_CAPABILITIES_PAGE] = 0,
};

CDROM *cdrom;

int readcdmode = 0;

uint8_t mode_pages_in[256][256];
uint8_t prefix_len;
uint8_t page_current;

/*SCSI Sense Initialization*/
void SCSISenseCodeOk(void)
{	
	SCSISense.SenseKey=SENSE_NONE;
	SCSISense.Asc=0;
	SCSISense.Ascq=0;
}

void SCSISenseCodeError(uint8_t SenseKey, uint8_t Asc, uint8_t Ascq)
{
	SCSISense.SenseKey=SenseKey;
	SCSISense.Asc=Asc;
	SCSISense.Ascq=Ascq;
}

static void
ScsiPadStr8(uint8_t *buf, int buf_size, const char *src)
{
	int i;

	for (i = 0; i < buf_size; i++) {
		if (*src != '\0') {
			buf[i] = *src++;
		} else {
			buf[i] = ' ';
		}
	}
}

uint32_t SCSIGetCDChannel(int channel)
{
	return (page_flags[GPMODE_CDROM_AUDIO_PAGE] & PAGE_CHANGED) ? mode_pages_in[GPMODE_CDROM_AUDIO_PAGE][channel ? 8 : 6] : (channel + 1);
}

uint32_t SCSIGetCDVolume(int channel)
{
	// return ((page_flags[GPMODE_CDROM_AUDIO_PAGE] & PAGE_CHANGED) && (mode_pages_in[GPMODE_CDROM_AUDIO_PAGE][channel ? 8 : 6] != 0)) ? mode_pages_in[GPMODE_CDROM_AUDIO_PAGE][channel ? 9 : 7] : 0xFF;
	return (page_flags[GPMODE_CDROM_AUDIO_PAGE] & PAGE_CHANGED) ? mode_pages_in[GPMODE_CDROM_AUDIO_PAGE][channel ? 9 : 7] : 0xFF;
}

/*SCSI Mode Sense 6/10*/
uint32_t SCSICDROMModeSense(uint8_t *buf, uint32_t pos, uint8_t type)
{
        if (type==GPMODE_ALL_PAGES || type==GPMODE_R_W_ERROR_PAGE)
        {
        	/* &01 - Read error recovery */
        	buf[pos++] = GPMODE_R_W_ERROR_PAGE;
        	buf[pos++] = 6; /* Page length */
        	buf[pos++] = 0; /* Error recovery parameters */
        	buf[pos++] = 5; /* Read retry count */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 0; /* Reserved */
        }

        if (type==GPMODE_ALL_PAGES || type==GPMODE_CDROM_PAGE)
        {
        	/* &0D - CD-ROM Parameters */
        	buf[pos++] = GPMODE_CDROM_PAGE;
        	buf[pos++] = 6; /* Page length */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 1; /* Inactivity time multiplier *NEEDED BY RISCOS* value is a guess */
        	buf[pos++] = 0; buf[pos++] = 60; /* MSF settings */
        	buf[pos++] = 0; buf[pos++] = 75; /* MSF settings */
        }

        if (type==GPMODE_ALL_PAGES || type==GPMODE_CDROM_AUDIO_PAGE)
        {
        	/* &0e - CD-ROM Audio Control Parameters */
        	buf[pos++] = GPMODE_CDROM_AUDIO_PAGE;
			buf[pos++] = 0xE; /* Page length */
			if (page_flags[GPMODE_CDROM_AUDIO_PAGE] & PAGE_CHANGED)
			{
				int i;
				for (i = 0; i < 14; i++)
				{
					buf[pos++] = mode_pages_in[GPMODE_CDROM_AUDIO_PAGE][i];
				}
			}
			else
			{
				buf[pos++] = 4; /* Reserved */
				buf[pos++] = 0; /* Reserved */
				buf[pos++] = 0; /* Reserved */
				buf[pos++] = 0; /* Reserved */
				buf[pos++] = 0; buf[pos++] = 75; /* Logical audio block per second */
				buf[pos++] = 1;    /* CDDA Output Port 0 Channel Selection */
				buf[pos++] = 0xFF; /* CDDA Output Port 0 Volume */
				buf[pos++] = 2;    /* CDDA Output Port 1 Channel Selection */
				buf[pos++] = 0xFF; /* CDDA Output Port 1 Volume */
				buf[pos++] = 0;    /* CDDA Output Port 2 Channel Selection */
				buf[pos++] = 0;    /* CDDA Output Port 2 Volume */
				buf[pos++] = 0;    /* CDDA Output Port 3 Channel Selection */
				buf[pos++] = 0;    /* CDDA Output Port 3 Volume */
			}
        }

        if (type==GPMODE_ALL_PAGES || type==GPMODE_CAPABILITIES_PAGE)
        {
//                pclog("Capabilities page\n");
               	/* &2A - CD-ROM capabilities and mechanical status */
        	buf[pos++] = GPMODE_CAPABILITIES_PAGE;
        	buf[pos++] = 0x12; /* Page length */
        	buf[pos++] = 0; buf[pos++] = 0; /* CD-R methods */
        	buf[pos++] = 1; /* Supports audio play, not multisession */
        	buf[pos++] = 0; /* Some other stuff not supported */
        	buf[pos++] = 0; /* Some other stuff not supported (lock state + eject) */
        	buf[pos++] = 0; /* Some other stuff not supported */
        	buf[pos++] = (uint8_t) (CDROM_SPEED >> 8);
        	buf[pos++] = (uint8_t) CDROM_SPEED; /* Maximum speed */
        	buf[pos++] = 0; buf[pos++] = 2; /* Number of audio levels - on and off only */
        	buf[pos++] = 0; buf[pos++] = 0; /* Buffer size - none */
        	buf[pos++] = (uint8_t) (CDROM_SPEED >> 8);
        	buf[pos++] = (uint8_t) CDROM_SPEED; /* Current speed */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 0; /* Drive digital format */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 0; /* Reserved */
        }

	return pos;
}

/*SCSI Get Configuration*/
uint8_t SCSICDROMSetProfile(uint8_t *buf, uint8_t *index, uint16_t profile)
{
    uint8_t *buf_profile = buf + 12; /* start of profiles */

    buf_profile += ((*index) * 4); /* start of indexed profile */
    buf_profile[0] = (profile >> 8) & 0xff;
	buf_profile[1] = profile & 0xff;
    buf_profile[2] = ((buf_profile[0] == buf[6]) && (buf_profile[1] == buf[7]));

    /* each profile adds 4 bytes to the response */
    (*index)++;
    buf[11] += 4; /* Additional Length */

    return 4;
}

/*SCSI Read DVD Structure*/
int SCSICDROMReadDVDStructure(int format, const uint8_t *packet, uint8_t *buf)
{
    switch (format) {
        case 0x0: /* Physical format information */
            {
                int layer = packet[6];
                uint64_t total_sectors;
				total_sectors = (uint64_t) cdrom->size();

                if (layer != 0)
                    return -ASC_INV_FIELD_IN_CMD_PACKET;

                total_sectors >>= 2;
                if (total_sectors == 0)
                    return -ASC_MEDIUM_NOT_PRESENT;

                buf[4] = 1;   /* DVD-ROM, part version 1 */
                buf[5] = 0xf; /* 120mm disc, minimum rate unspecified */
                buf[6] = 1;   /* one layer, read-only (per MMC-2 spec) */
                buf[7] = 0;   /* default densities */

                /* FIXME: 0x30000 per spec? */
				buf[8] = buf[9] = buf[10] = buf[11] = 0; /* start sector */
				buf[12] = (total_sectors >> 24) & 0xff; /* end sector */
				buf[13] = (total_sectors >> 16) & 0xff;
				buf[14] = (total_sectors >> 8) & 0xff;
				buf[15] = total_sectors & 0xff;

				buf[16] = (total_sectors >> 24) & 0xff; /* l0 end sector */
				buf[17] = (total_sectors >> 16) & 0xff;
				buf[18] = (total_sectors >> 8) & 0xff;
				buf[19] = total_sectors & 0xff;

                /* Size of buffer, not including 2 byte size field */				
				buf[0] = ((2048+2)>>8)&0xff;
				buf[1] = (2048+2)&0xff;

                /* 2k data + 4 byte header */
                return (2048 + 4);
            }

        case 0x01: /* DVD copyright information */
            buf[4] = 0; /* no copyright data */
            buf[5] = 0; /* no region restrictions */

            /* Size of buffer, not including 2 byte size field */
			buf[0] = ((4+2)>>8)&0xff;
			buf[1] = (4+2)&0xff;			

            /* 4 byte header + 4 byte data */
            return (4 + 4);

        case 0x03: /* BCA information - invalid field for no BCA info */
            return -ASC_INV_FIELD_IN_CMD_PACKET;

        case 0x04: /* DVD disc manufacturing information */
            /* Size of buffer, not including 2 byte size field */
				buf[0] = ((2048+2)>>8)&0xff;
				buf[1] = (2048+2)&0xff;

            /* 2k data + 4 byte header */
            return (2048 + 4);

        case 0xff:
            /*
             * This lists all the command capabilities above.  Add new ones
             * in order and update the length and buffer return values.
             */

            buf[4] = 0x00; /* Physical format */
            buf[5] = 0x40; /* Not writable, is readable */
				buf[6] = ((2048+4)>>8)&0xff;
				buf[7] = (2048+4)&0xff;

            buf[8] = 0x01; /* Copyright info */
            buf[9] = 0x40; /* Not writable, is readable */
				buf[10] = ((4+4)>>8)&0xff;
				buf[11] = (4+4)&0xff;

            buf[12] = 0x03; /* BCA info */
            buf[13] = 0x40; /* Not writable, is readable */
				buf[14] = ((188+4)>>8)&0xff;
				buf[15] = (188+4)&0xff;

            buf[16] = 0x04; /* Manufacturing info */
            buf[17] = 0x40; /* Not writable, is readable */
				buf[18] = ((2048+4)>>8)&0xff;
				buf[19] = (2048+4)&0xff;

            /* Size of buffer, not including 2 byte size field */
				buf[6] = ((16+2)>>8)&0xff;
				buf[7] = (16+2)&0xff;

            /* data written + 4 byte header */
            return (16 + 4);

        default: /* TODO: formats beyond DVD-ROM requires */
            return -ASC_INV_FIELD_IN_CMD_PACKET;
    }
}

/*SCSI Get Event Status Notification*/
uint32_t SCSICDROMEventStatus(uint8_t *buffer)
{
	uint8_t event_code, media_status = 0;

	if (buffer[5])
	{
		media_status = MS_TRAY_OPEN;
		cdrom->stop();
	}
	else
	{
		media_status = MS_MEDIA_PRESENT;
	}
	
	event_code = MEC_NO_CHANGE;
	if (media_status != MS_TRAY_OPEN)
	{
		if (!buffer[4])
		{
			event_code = MEC_NEW_MEDIA;
			cdrom->load();
		}
		else if (buffer[4]==2)
		{
			event_code = MEC_EJECT_REQUESTED;
			cdrom->eject();
		}
	}
	
	buffer[4] = event_code;
	buffer[5] = media_status;
	buffer[6] = 0;
	buffer[7] = 0;
	
	return 8;
}

void SCSICDROM_Insert()
{
	SCSISense.UnitAttention=1;
}

int cd_status = CD_STATUS_EMPTY;
int prev_status;

static uint8_t ScsiPrev;
static int SenseCompleted;

int cdrom_add_error_and_subchannel(uint8_t *b, int real_sector_type)
{
	if ((cdrom_sector_flags & 0x06) == 0x02)
	{
		/* Add error flags. */
		memcpy(b + cdrom_sector_size, cdrom_sector_buffer.cdrom_sector.c2, 294);
		cdrom_sector_size += 294;
	}
	else if ((cdrom_sector_flags & 0x06) == 0x04)
	{
		/* Add error flags. */
		memcpy(b + cdrom_sector_size, cdrom_sector_buffer.cdrom_sector.c2, 296);
		cdrom_sector_size += 296;
	}
	else if ((cdrom_sector_flags & 0x06) == 0x06)
	{
		// pclog("Invalid error flags\n");
		return 0;
	}
	
	/* if (real_sector_type == 1)
	{ */
		if ((cdrom_sector_flags & 0x700) == 0x100)
		{
			memcpy(b + cdrom_sector_size, cdrom_sector_buffer.cdrom_sector.subchannel_raw, 96);
			cdrom_sector_size += 96;
		}
		else if ((cdrom_sector_flags & 0x700) == 0x200)
		{
			memcpy(b + cdrom_sector_size, cdrom_sector_buffer.cdrom_sector.subchannel_q, 16);
			cdrom_sector_size += 16;
		}
		else if ((cdrom_sector_flags & 0x700) == 0x400)
		{
			memcpy(b + cdrom_sector_size, cdrom_sector_buffer.cdrom_sector.subchannel_rw, 96);
			cdrom_sector_size += 96;
		}
		else if (((cdrom_sector_flags & 0x700) == 0x300) || ((cdrom_sector_flags & 0x700) > 0x400))
		{
			// pclog("Invalid subchannel data flags\n");
			return 0;
		}
	// }
	
	// pclog("CD-ROM sector size after processing: %i (%i, %i) [%04X]\n", cdrom_sector_size, cdrom_sector_type, real_sector_type, cdrom_sector_flags);

	return cdrom_sector_size;
}

int cdrom_LBAtoMSF_accurate()
{
	int temp_pos;
	int m, s, f;
	
	temp_pos = SectorLBA + 150;
	f = temp_pos % 75;
	temp_pos -= f;
	temp_pos /= 75;
	s = temp_pos % 60;
	temp_pos -= s;
	temp_pos /= 60;
	m = temp_pos;
	
	return ((m << 16) | (s << 8) | f);
}

int cdrom_read_data(uint8_t *buffer)
{
	int real_sector_type;
	uint8_t *b;
	uint8_t *temp_b;
	int is_audio;
	int real_pos;
	
	b = temp_b = buffer;
	
	if  (cdrom_sector_ismsf)
	{
		real_pos = cdrom_LBAtoMSF_accurate();
	}
	else
	{
		real_pos = SectorLBA;
	}

	memset(cdrom_sector_buffer.buffer, 0, 2856);

	// is_audio = cdrom->is_track_audio(real_pos, cdrom_sector_ismsf);
	real_sector_type = cdrom->sector_data_type(real_pos, cdrom_sector_ismsf);
	// pclog("Sector type: %i\n", real_sector_type);
	
	if ((cdrom_sector_type > 0) && (cdrom_sector_type < 6))
	{
		if (real_sector_type != cdrom_sector_type)
		{
			return 0;
		}
	}
	else if (cdrom_sector_type == 6)
	{
		/* READ (6), READ (10), READ (12) */
		if ((real_sector_type != 2) && (real_sector_type != 4))
		{
			return 0;
		}
	}

	if (!(cdrom_sector_flags & 0xf0))		/* 0x00 and 0x08 are illegal modes */
	{
		return 0;
	}

	cdrom->readsector_raw(cdrom_sector_buffer.buffer, real_pos, cdrom_sector_ismsf);
	
	if (real_sector_type == 1)
	{
		memcpy(b, cdrom_sector_buffer.buffer, 2352);
		cdrom_sector_size = 2352;
	}
	else
	{
		if ((cdrom_sector_flags & 0x18) == 0x08)		/* EDC/ECC without user data is an illegal mode */
		{
			return 0;
		}

		cdrom_sector_size = 0;

		if (cdrom_sector_flags & 0x80)		/* Sync */
		{
			memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.sync, 12);
			cdrom_sector_size += 12;
			temp_b += 12;
		}
		if (cdrom_sector_flags & 0x20)		/* Header */
		{
			memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.header, 4);
			cdrom_sector_size += 4;
			temp_b += 4;
		}
		
		if (real_sector_type == 2)
		{
			/* Mode 1 sector, expected type is 1 type. */
			if (cdrom_sector_flags & 0x40)		/* Sub-header */
			{
				if (!(cdrom_sector_flags & 0x10))		/* No user data */
				{
					memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m1_data.user_data, 8);
					cdrom_sector_size += 8;
					temp_b += 8;
				}
			}
			if (cdrom_sector_flags & 0x10)		/* User data */
			{
				memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m1_data.user_data, 2048);
				cdrom_sector_size += 2048;
				temp_b += 2048;
			}
			if (cdrom_sector_flags & 0x08)		/* EDC/ECC */
			{
				memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m1_data.ecc, 288);
				cdrom_sector_size += 288;
				temp_b += 288;
			}
		}
		else if (real_sector_type == 3)
		{
			/* Mode 2 sector, non-XA mode. */
			if (cdrom_sector_flags & 0x40)		/* Sub-header */
			{
				if (!(cdrom_sector_flags & 0x10))		/* No user data */
				{
					memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m2_data.sub_header, 8);
					cdrom_sector_size += 8;
					temp_b += 8;
				}
			}
			if (cdrom_sector_flags & 0x10)		/* User data */
			{
				memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m2_data.sub_header, 2328);
				cdrom_sector_size += 8;
				temp_b += 8;
				memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m2_data.user_data, 2328);
				cdrom_sector_size += 2328;
				temp_b += 2328;
			}
		}
		else if (real_sector_type == 4)
		{
			/* Mode 2 sector, XA Form 1 mode */
			if ((cdrom_sector_flags & 0xf0) == 0x30)
			{
				return 0;
			}
			if (((cdrom_sector_flags & 0xf8) >= 0xa8) && ((cdrom_sector_flags & 0xf8) <= 0xd8))
			{
				return 0;
			}
			if (cdrom_sector_flags & 0x40)		/* Sub-header */
			{
				memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m2_data.sub_header, 8);
				cdrom_sector_size += 8;
				temp_b += 8;
			}
			if (cdrom_sector_flags & 0x10)		/* User data */
			{
				if ((cdrom_sector_flags & 0xf0) == 0x10)
				{
					/* The data is alone, include sub-header. */
					memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m2_data.sub_header, 8);
					cdrom_sector_size += 8;
					temp_b += 8;
				}
				memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m2_data.user_data, 2040);
				cdrom_sector_size += 2040;
				temp_b += 2040;
			}
			if (cdrom_sector_flags & 0x08)		/* EDC/ECC */
			{
				memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m2_data.user_data + 2040, 288);
				cdrom_sector_size += 288;
				temp_b += 288;
			}
		}
		else if (real_sector_type == 5)
		{
			/* Mode 2 sector, XA Form 2 mode */
			if ((cdrom_sector_flags & 0xf0) == 0x30)
			{
				return 0;
			}
			if (((cdrom_sector_flags & 0xf8) >= 0xa8) || ((cdrom_sector_flags & 0xf8) <= 0xd8))
			{
				return 0;
			}
			/* Mode 2 sector, XA Form 1 mode */
			if (cdrom_sector_flags & 0x40)		/* Sub-header */
			{
				memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m2_data.sub_header, 8);
				cdrom_sector_size += 8;
				temp_b += 8;
			}
			if (cdrom_sector_flags & 0x10)		/* User data */
			{
				memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m2_data.user_data, 2328);
				cdrom_sector_size += 2328;
				temp_b += 2328;
			}
		}
		else
		{
			return 0;
		}
	}

	// pclog("CD-ROM sector size: %i (%i, %i) [%04X]\n", cdrom_sector_size, cdrom_sector_type, real_sector_type, cdrom_sector_flags);
	return cdrom_add_error_and_subchannel(b, real_sector_type);
}

static void SCSIClearSense(uint8_t Command, uint8_t IgnoreUA)
{
	if ((SCSISense.SenseKey == SENSE_UNIT_ATTENTION) || IgnoreUA)
	{
		ScsiPrev=Command;
		SCSISenseCodeOk();
	}
}

struct
{
	uint8_t opcode;
	uint8_t polled;
	uint8_t reserved2[2];
	uint8_t class;
	uint8_t reserved3[2];
	uint16_t len;
	uint8_t control;
} *gesn_cdb;
					
struct
{
	uint16_t len;
	uint8_t notification_class;
	uint8_t supported_events;
} *gesn_event_header;

static int SCSICDROM_TOC(uint8_t id, uint8_t *cdb)
{
	int TocFormat;
	
	TocFormat = cdb[2] & 0xf;
	if (TocFormat == 0)
		TocFormat = (cdb[9]>>6) & 3;

	return TocFormat;
}

void SCSICDROM_Command(uint8_t id, uint8_t *cdb)
{
	if (cdrom->medium_changed())
	{
		pclog("Media changed\n");
		SCSICDROM_Insert();
	}
	
	if (!cdrom->ready() && SCSISense.UnitAttention)
	{
		/* If the drive is not ready, there is no reason to keep the
			UNIT ATTENTION condition present, as we only use it to mark
			disc changes. */
		SCSISense.UnitAttention = 0;
	}
	
	/* If the UNIT ATTENTION condition is set and the command does not allow
		execution under it, error out and report the condition. */
	if (!(SCSICommandTable[cdb[0]] & ALLOW_UA) && SCSISense.UnitAttention)
	{
		pclog("UNIT ATTENTION: Command not allowed to pass through\n");
		SCSISenseCodeError(SENSE_UNIT_ATTENTION, ASC_MEDIUM_MAY_HAVE_CHANGED, 0);
		SCSIStatus = SCSI_STATUS_CHECK_CONDITION;
		SCSICallback[id]=50*SCSI_TIME;
		return;
	}

	/* Unless the command is REQUEST SENSE, clear the sense. This will *NOT*
		clear the UNIT ATTENTION condition if it's set. */
	if (cdb[0]!=GPCMD_REQUEST_SENSE)
	{
		SCSIClearSense(cdb[0], 1);
	}

	/* Next it's time for NOT READY. */
	if ((SCSICommandTable[cdb[0]] & CHECK_READY) && !cdrom->ready())
	{
		pclog("Not ready\n");
		SCSISenseCodeError(SENSE_NOT_READY, ASC_MEDIUM_NOT_PRESENT, 0);
		SCSIStatus = SCSI_STATUS_CHECK_CONDITION;
		SCSICallback[id]=50*SCSI_TIME;
		return;
	}
	
	prev_status = cd_status;
	cd_status = cdrom->status();
	if (((prev_status == CD_STATUS_PLAYING) || (prev_status == CD_STATUS_PAUSED)) && ((cd_status != CD_STATUS_PLAYING) && (cd_status != CD_STATUS_PAUSED)))
	{
		SenseCompleted = 1;
	}
	else
	{
		SenseCompleted = 0;
	}	
	
	switch (cdb[0])
	{
		case GPCMD_TEST_UNIT_READY:
		SCSIPhase = SCSI_PHASE_STATUS;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=50*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = 0;
		break;

		case GPCMD_REQUEST_SENSE:
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		
		if (cdb[4] == 0)
			SCSIDevices[id].CmdBufferLength = 4;
		else if (cdb[4] > 18)
			SCSIDevices[id].CmdBufferLength = 18;
		else
			SCSIDevices[id].CmdBufferLength = cdb[4];
		
		SCSIDMAResetPosition(id);
		break;
		
		case GPCMD_READ_6:
		case GPCMD_READ_10:
		case GPCMD_READ_12:
		cdrom_sector_ismsf = 0;
		
		if (cdb[0] == GPCMD_READ_6)
		{
			SectorLen=cdb[4];
			SectorLBA=((((uint32_t) cdb[1]) & 0x1f)<<16)|(((uint32_t) cdb[2])<<8)|((uint32_t) cdb[3]);
		}
		else if (cdb[0] == GPCMD_READ_10)
		{
			SectorLen=(cdb[7]<<8)|cdb[8];
			SectorLBA=(cdb[2]<<24)|(cdb[3]<<16)|(cdb[4]<<8)|cdb[5];
		}
		else
		{
			SectorLen=(((uint32_t) cdb[6])<<24)|(((uint32_t) cdb[7])<<16)|(((uint32_t) cdb[8])<<8)|((uint32_t) cdb[9]);
			SectorLBA=(((uint32_t) cdb[2])<<24)|(((uint32_t) cdb[3])<<16)|(((uint32_t) cdb[4])<<8)|((uint32_t) cdb[5]);
		}
		
		if (SectorLBA > (cdrom->size() - 1))
		{
			pclog("Trying to read beyond the end of disc\n");
			SCSIStatus = SCSI_STATUS_CHECK_CONDITION;
			SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_ILLEGAL_OPCODE, 0);
			if (SCSISense.UnitAttention)
			{
				SCSISenseCodeError(SENSE_UNIT_ATTENTION, ASC_MEDIUM_MAY_HAVE_CHANGED, 0);
			}
			SCSICallback[id]=50*SCSI_TIME;
			break;
		}		
		
		if (!SectorLen)
		{
			SCSIPhase = SCSI_PHASE_STATUS;
			SCSIStatus = SCSI_STATUS_OK;
			SCSICallback[id]=20*SCSI_TIME;
			break;
		}

		cdrom_sector_type = 6;
		cdrom_sector_flags = 0x10;

		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = SectorLen * 2048;
		
		SCSIDMAResetPosition(id);
		return;
		
		case GPCMD_INQUIRY:
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = cdb[4];
		
		SCSIDMAResetPosition(id);
		break;
		
		case GPCMD_MODE_SELECT_6:
		case GPCMD_MODE_SELECT_10:
		SCSIPhase = SCSI_PHASE_DATAOUT;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		if (cdb[0] == GPCMD_MODE_SELECT_6)
		{
			SCSIDevices[id].CmdBufferLength = cdb[4];
		}
		else
		{
			SCSIDevices[id].CmdBufferLength = (cdb[7]<<8)|cdb[8];
		}
		
		SCSIDMAResetPosition(id);
		return;
		
		case GPCMD_MODE_SENSE_6:
		case GPCMD_MODE_SENSE_10:
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		if (cdb[0] == GPCMD_MODE_SENSE_6)
		{
			SCSIDevices[id].CmdBufferLength = cdb[4];
		}
		else
		{
			SCSIDevices[id].CmdBufferLength = (cdb[7]<<8)|cdb[8];
		}
		
		SCSIDMAResetPosition(id);
		return;
		
		case GPCMD_START_STOP_UNIT:
		if (cdb[4]!=2 && cdb[4]!=3 && cdb[4])
		{
			SCSIStatus = SCSI_STATUS_CHECK_CONDITION;			
			SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_ILLEGAL_OPCODE, 0x00);
			if (SCSISense.UnitAttention)
			{
				SCSISenseCodeError(SENSE_UNIT_ATTENTION, ASC_MEDIUM_MAY_HAVE_CHANGED, 0);
			}
			SCSICallback[id]=50*SCSI_TIME;
			break;
		}
		if (!cdb[4])        cdrom->stop();
		else if (cdb[4]==2) cdrom->eject();
		else              	cdrom->load();
		
		SCSIPhase = SCSI_PHASE_STATUS;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=50*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = 0;
		break;
		
		case GPCMD_PREVENT_REMOVAL:
		SCSIPhase = SCSI_PHASE_STATUS;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=50*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = 0;
		break;

		case GPCMD_READ_CDROM_CAPACITY:
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = 8;
		
		SCSIDMAResetPosition(id);
		break;
		
		case GPCMD_SEEK:
		SectorLBA = (cdb[3]<<16)|(cdb[4]<<8)|cdb[5];
		cdrom->seek(SectorLBA);
		
		SCSIPhase = SCSI_PHASE_STATUS;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=50*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = 0;
		break;

		case GPCMD_READ_SUBCHANNEL:
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=1000*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = (cdb[7]<<8)|cdb[8];
		
		if (!(cdb[2] & 0x40))
			SCSIDevices[id].CmdBufferLength = 4;
		else
			SCSIDevices[id].CmdBufferLength = 16;
		
		SCSIDMAResetPosition(id);
		break;

		case GPCMD_READ_TOC_PMA_ATIP:
		{
			int len;
			
			switch (SCSICDROM_TOC(id, cdb))
			{
				case 0: /*Normal*/
				len = cdb[8]|(cdb[7]<<8);
				break;
					
				case 1: /*Multi session*/
				len = cdb[8]|(cdb[7]<<8);
				break;
					
				case 2: /*Raw*/
				len = cdb[8]|(cdb[7]<<8);
				break;
							  
				default:
				SCSIStatus = SCSI_STATUS_CHECK_CONDITION;			
				SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
				if (SCSISense.UnitAttention)
				{
					SCSISenseCodeError(SENSE_UNIT_ATTENTION, ASC_MEDIUM_MAY_HAVE_CHANGED, 0);
				}
				SCSICallback[id]=50*SCSI_TIME;
				return;
			}
			SCSIPhase = SCSI_PHASE_DATAIN;
			SCSIStatus = SCSI_STATUS_OK;
			SCSICallback[id]=60*SCSI_TIME;
			SCSIDevices[id].CmdBufferLength = len;
			
			SCSIDMAResetPosition(id);
		}
		return;
	
		case GPCMD_READ_HEADER:
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = 8;
		
		SCSIDMAResetPosition(id);
		return;
	
		case GPCMD_PLAY_AUDIO_10:
		case GPCMD_PLAY_AUDIO_MSF:
		case GPCMD_PLAY_AUDIO_12:
		if (cdb[0] == GPCMD_PLAY_AUDIO_10)
		{
			SectorLBA = (cdb[2]<<24)|(cdb[3]<<16)|(cdb[4]<<8)|cdb[5];
			SectorLen = (cdb[7]<<8)|cdb[8];
		}
		else if (cdb[0] == GPCMD_PLAY_AUDIO_MSF)
		{
			SectorLBA = (cdb[3]<<16)|(cdb[4]<<8)|cdb[5];
			SectorLen = (cdb[6]<<16)|(cdb[7]<<8)|cdb[8];
		}
		else
		{
			SectorLBA = (cdb[3]<<16)|(cdb[4]<<8)|cdb[5];
			SectorLen = (cdb[7]<<16)|(cdb[8]<<8)|cdb[9];
		}

		if ((cdrom_drive < 1) || (cdrom_drive == 200) || (cd_status <= CD_STATUS_DATA_ONLY) ||
			!cdrom->is_track_audio(SectorLBA, (cdb[0] == GPCMD_PLAY_AUDIO_MSF) ? 1 : 0))
		{
			SCSIStatus = SCSI_STATUS_CHECK_CONDITION;			
			SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_ILLEGAL_MODE_FOR_THIS_TRACK, 0x00);
			SCSICallback[id]=50*SCSI_TIME;
			break;
		}
			
		cdrom->playaudio(SectorLBA, SectorLen, (cdb[0] == GPCMD_PLAY_AUDIO_MSF) ? 1 : 0);
		SCSIPhase = SCSI_PHASE_STATUS;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=50*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = 0;
		break;
		
		case GPCMD_GET_CONFIGURATION:
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = (cdb[7]<<8)|cdb[8];
		
		SCSIDMAResetPosition(id);
		break;
		
		case GPCMD_GET_EVENT_STATUS_NOTIFICATION:
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = 8;
		
		SCSIDMAResetPosition(id);
		break;
		
		case GPCMD_PAUSE_RESUME:
		if (cdb[8]&1) 	cdrom->resume();
		else           	cdrom->pause();
		SCSIPhase = SCSI_PHASE_STATUS;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=50*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = 0;
		break;
		
		case GPCMD_STOP_PLAY_SCAN:
		cdrom->stop();
		SCSIPhase = SCSI_PHASE_STATUS;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=50*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = 0;
		break;
		
		case GPCMD_READ_DISC_INFORMATION:
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = 34;
		
		SCSIDMAResetPosition(id);
		break;
		
		case GPCMD_READ_TRACK_INFORMATION:
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = 36;
		
		SCSIDMAResetPosition(id);
		break;		
		
		case GPCMD_READ_DVD_STRUCTURE:		
		{
			int len;
			
			len = (cdb[6]<<24)|(cdb[7]<<16)|(cdb[8]<<8)|cdb[9];
			
			if (cdb[7] < 0xff) 
			{
				if (len <= CD_MAX_SECTORS) 
				{
					SCSIStatus = SCSI_STATUS_CHECK_CONDITION;				
					SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INCOMPATIBLE_FORMAT, 0x00);
					SCSICallback[id]=50*SCSI_TIME;
					break;
				} 
				else
				{
					SCSIStatus = SCSI_STATUS_CHECK_CONDITION;				
					SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
					if (SCSISense.UnitAttention)
					{
						SCSISenseCodeError(SENSE_UNIT_ATTENTION, ASC_MEDIUM_MAY_HAVE_CHANGED, 0);
					}
					SCSICallback[id]=50*SCSI_TIME;
					return;
				}
			}		
		
			SCSIPhase = SCSI_PHASE_DATAIN;
			SCSIStatus = SCSI_STATUS_OK;
			SCSICallback[id]=60*SCSI_TIME;
			SCSIDevices[id].CmdBufferLength = len;
			
			SCSIDMAResetPosition(id);
		}
		break;
		
		case GPCMD_READ_CD_MSF:
		case GPCMD_READ_CD:
		if (cdb[0] == GPCMD_READ_CD_MSF)
		{
			SectorLBA=MSFtoLBA(cdb[3],cdb[4],cdb[5]);
			SectorLen=MSFtoLBA(cdb[6],cdb[7],cdb[8]);

			SectorLen -= SectorLBA;
			SectorLen++;
				
			cdrom_sector_ismsf = 1;
		}
		else
		{
			SectorLen=(cdb[6]<<16)|(cdb[7]<<8)|cdb[8];
			SectorLBA=(cdb[2]<<24)|(cdb[3]<<16)|(cdb[4]<<8)|cdb[5];

			cdrom_sector_ismsf = 0;
		}
		
		if (SectorLBA > (cdrom->size() - 1))
		{
			pclog("Trying to read beyond the end of disc\n");
			SCSIStatus = SCSI_STATUS_CHECK_CONDITION;
			SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_ILLEGAL_OPCODE, 0);
			if (SCSISense.UnitAttention)
			{
				SCSISenseCodeError(SENSE_UNIT_ATTENTION, ASC_MEDIUM_MAY_HAVE_CHANGED, 0);
			}
			SCSICallback[id]=50*SCSI_TIME;
			break;
		}		
		
		cdrom_sector_type = (cdb[1] >> 2) & 7;
		cdrom_sector_flags = cdb[9] || ((cdb[10]) << 8);		

		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = SectorLen * cdrom_sector_size;
		
		SCSIDMAResetPosition(id);
		return;
		
		case GPCMD_SET_SPEED:
		SCSIPhase = SCSI_PHASE_STATUS;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=50*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = 0;
		break;
		
		case GPCMD_MECHANISM_STATUS:
		SCSIPhase = SCSI_PHASE_DATAIN;
		SCSIStatus = SCSI_STATUS_OK;
		SCSICallback[id]=60*SCSI_TIME;
		SCSIDevices[id].CmdBufferLength = 8;
		
		SCSIDMAResetPosition(id);
		break;
	}
}

void SCSICDROM_ReadData(uint8_t id, uint8_t *cdb, uint8_t *data, int datalen)
{
	int DVDRet;
	uint8_t Index = 0;
	int real_pos;	
	int msf;
	uint32_t Size;
	unsigned Idx = 0;
	unsigned SizeIndex;
	unsigned PreambleLen;
	unsigned char Temp;
	int read_length = 0;
	int max_length = 0;
	int track = 0;
	int ret = 0;
	
	msf = cdb[1] & 2;
	
	switch (cdb[0])
	{
		case GPCMD_REQUEST_SENSE:
		/*Will return 18 bytes of 0*/
		memset(SCSIDevices[id].CmdBuffer,0,512);

		SCSIDevices[id].CmdBuffer[0]=0x80|0x70;

		if ((SCSISense.SenseKey > 0) || (cd_status < CD_STATUS_PLAYING))
		{
			if (SenseCompleted)
			{
				SCSIDevices[id].CmdBuffer[2]=SENSE_ILLEGAL_REQUEST;
				SCSIDevices[id].CmdBuffer[12]=ASC_AUDIO_PLAY_OPERATION;
				SCSIDevices[id].CmdBuffer[13]=ASCQ_AUDIO_PLAY_OPERATION_COMPLETED;
			}
			else
			{
				SCSIDevices[id].CmdBuffer[2]=SCSISense.SenseKey;
				SCSIDevices[id].CmdBuffer[12]=SCSISense.Asc;
				SCSIDevices[id].CmdBuffer[13]=SCSISense.Ascq;
			}
		}
		else if ((SCSISense.SenseKey == 0) && (cd_status >= CD_STATUS_PLAYING) && (cd_status != CD_STATUS_STOPPED))
		{
			SCSIDevices[id].CmdBuffer[2]=SENSE_ILLEGAL_REQUEST;
			SCSIDevices[id].CmdBuffer[12]=ASC_AUDIO_PLAY_OPERATION;
			SCSIDevices[id].CmdBuffer[13]=(cd_status == CD_STATUS_PLAYING) ? ASCQ_AUDIO_PLAY_OPERATION_IN_PROGRESS : ASCQ_AUDIO_PLAY_OPERATION_PAUSED;
		}
		else
		{
			if (SCSISense.UnitAttention)
			{
				SCSIDevices[id].CmdBuffer[2]=SENSE_UNIT_ATTENTION;
				SCSIDevices[id].CmdBuffer[12]=ASC_MEDIUM_MAY_HAVE_CHANGED;
				SCSIDevices[id].CmdBuffer[13]=0;
			}
		}

		SCSIDevices[id].CmdBuffer[7]=10;

		if (SCSIDevices[id].CmdBuffer[2] == SENSE_UNIT_ATTENTION)
		{
			/* If the last remaining sense is unit attention, clear
				that condition. */
			SCSISense.UnitAttention = 0;
		}
		
		/* Clear the sense stuff as per the spec. */
		SCSIClearSense(cdb[0], 0);
		break;
		
		case GPCMD_READ_6:
		case GPCMD_READ_10:
		case GPCMD_READ_12:
		pclog("Total data length requested: %d\n", datalen);
        while (datalen > 0)
        {
            read_length = cdrom_read_data(data); //Fill the buffer the data it needs
            if (!read_length)
            {
                SCSIStatus = SCSI_STATUS_CHECK_CONDITION;
                SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_ILLEGAL_OPCODE, 0);
                SCSICallback[id]=50*SCSI_TIME;
                break;
            }
            else
            {
				//Continue reading data until the sector length is 0.
				data += read_length;
            }
           
            pclog("True LBA: %d, buffer half: %d\n", SectorLBA, SectorLen * 2048);
				
			SectorLBA++;
			SectorLen--;

			if (SectorLen == 0)
			{
				break;
			}
		}
		break;
		
		case GPCMD_INQUIRY:
		if (cdb[1] & 1)
		{
			PreambleLen = 4;
			SizeIndex = 3;
						
			SCSIDevices[id].CmdBuffer[Idx++] = 05;
			SCSIDevices[id].CmdBuffer[Idx++] = cdb[2];
			SCSIDevices[id].CmdBuffer[Idx++] = 0;
						
			Idx++;

			switch (cdb[2])
			{
				case 0x00:
				SCSIDevices[id].CmdBuffer[Idx++] = 0x00;
				SCSIDevices[id].CmdBuffer[Idx++] = 0x83;
				break;

				case 0x83:
				if (Idx + 24 > SCSIDevices[id].CmdBufferLength)
				{
					SCSIStatus = SCSI_STATUS_CHECK_CONDITION;					
					SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_DATA_PHASE_ERROR, 0x00);
					SCSICallback[id]=50*SCSI_TIME;
					return;
				}
				SCSIDevices[id].CmdBuffer[Idx++] = 0x02;
				SCSIDevices[id].CmdBuffer[Idx++] = 0x00;
				SCSIDevices[id].CmdBuffer[Idx++] = 0x00;
				SCSIDevices[id].CmdBuffer[Idx++] = 20;
				ScsiPadStr8(SCSIDevices[id].CmdBuffer + Idx, 20, "53R141");	/* Serial */
				Idx += 20;

				if (Idx + 72 > SCSIDevices[id].CmdBufferLength)
				{
					goto SCSIOut;
				}
				SCSIDevices[id].CmdBuffer[Idx++] = 0x02;
				SCSIDevices[id].CmdBuffer[Idx++] = 0x01;
				SCSIDevices[id].CmdBuffer[Idx++] = 0x00;
				SCSIDevices[id].CmdBuffer[Idx++] = 68;
				ScsiPadStr8(SCSIDevices[id].CmdBuffer + Idx, 8, "86Box"); /* Vendor */
				Idx += 8;
				ScsiPadStr8(SCSIDevices[id].CmdBuffer + Idx, 40, "86BoxCD 1.00"); /* Product */
				Idx += 40;
				ScsiPadStr8(SCSIDevices[id].CmdBuffer + Idx, 20, "53R141"); /* Product */
				Idx += 20;
				break;
					
				default:
				SCSIStatus = SCSI_STATUS_CHECK_CONDITION;				
				SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
				SCSICallback[id]=50*SCSI_TIME;
				return;
			}
		}
		else
		{
			PreambleLen = 5;
			SizeIndex = 4;

			SCSIDevices[id].CmdBuffer[0] = 0x05; /*CD-ROM*/
			SCSIDevices[id].CmdBuffer[1] = 0x80; /*Removable*/
			SCSIDevices[id].CmdBuffer[2] = 0;
			SCSIDevices[id].CmdBuffer[3] = 0x21;
			SCSIDevices[id].CmdBuffer[4] = 31;
			SCSIDevices[id].CmdBuffer[5] = 0;
			SCSIDevices[id].CmdBuffer[6] = 0;
			SCSIDevices[id].CmdBuffer[7] = 0;
			ScsiPadStr8(SCSIDevices[id].CmdBuffer + 8, 8, "86Box"); /* Vendor */
			ScsiPadStr8(SCSIDevices[id].CmdBuffer + 16, 16, "86BoxCD"); /* Product */
			ScsiPadStr8(SCSIDevices[id].CmdBuffer + 32, 4, emulator_version); /* Revision */
			
			Idx = 36;
		}
		
SCSIOut:
		SCSIDevices[id].CmdBuffer[SizeIndex] = Idx - PreambleLen;
		break;
		
		case GPCMD_MODE_SENSE_6:
		case GPCMD_MODE_SENSE_10:
		Temp = cdb[2] & 0x3f;

		memset(SCSIDevices[id].CmdBuffer, 0, datalen);

		if (!(mode_sense_pages[Temp] & IMPLEMENTED))
		{
			SCSIStatus = SCSI_STATUS_CHECK_CONDITION;	
			SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
			SCSICallback[id]=50*SCSI_TIME;
			return;
		}
				
		if (cdb[0] == GPCMD_MODE_SENSE_6)
		{
			datalen = SCSICDROMModeSense(SCSIDevices[id].CmdBuffer, 4, Temp);
			SCSIDevices[id].CmdBuffer[0] = datalen - 1;
			SCSIDevices[id].CmdBuffer[1] = 3; /*120mm data CD-ROM*/		
		}
		else
		{
			datalen = SCSICDROMModeSense(SCSIDevices[id].CmdBuffer, 8, Temp);
			SCSIDevices[id].CmdBuffer[0] = (datalen - 2)>>8;
			SCSIDevices[id].CmdBuffer[1] = (datalen - 2)&255;
			SCSIDevices[id].CmdBuffer[2] = 3; /*120mm data CD-ROM*/
		}
		break;
		
		case GPCMD_READ_CDROM_CAPACITY:
		if (cdrom->read_capacity)
		{
			cdrom->read_capacity(SCSIDevices[id].CmdBuffer);
		}
		else
		{
			Size = cdrom->size() - 1;		/* IMPORTANT: What's returned is the last LBA block. */
			memset(SCSIDevices[id].CmdBuffer, 0, 8);
			SCSIDevices[id].CmdBuffer[0] = (Size >> 24) & 0xff;
			SCSIDevices[id].CmdBuffer[1] = (Size >> 16) & 0xff;
			SCSIDevices[id].CmdBuffer[2] = (Size >> 8) & 0xff;
			SCSIDevices[id].CmdBuffer[3] = Size & 0xff;
			SCSIDevices[id].CmdBuffer[6] = 8;				/* 2048 = 0x0800 */
		}
		pclog("Sector size %04X\n", Size);
		break;
		
		case GPCMD_READ_SUBCHANNEL:
		if (cdb[3] != 1)
		{
			SCSIStatus = SCSI_STATUS_CHECK_CONDITION;			
			SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_ILLEGAL_OPCODE, 0x00);
			if (SCSISense.UnitAttention)
			{
				SCSISenseCodeError(SENSE_UNIT_ATTENTION, ASC_MEDIUM_MAY_HAVE_CHANGED, 0);
			}
			SCSICallback[id]=50*SCSI_TIME;
			break;
		}
		SectorLBA = 0;
		SCSIDevices[id].CmdBuffer[SectorLBA++] = 0;
		SCSIDevices[id].CmdBuffer[SectorLBA++] = 0; /*Audio status*/
		SCSIDevices[id].CmdBuffer[SectorLBA++] = 0; SCSIDevices[id].CmdBuffer[SectorLBA++] = 0; /*Subchannel length*/
		SCSIDevices[id].CmdBuffer[SectorLBA++] = 1; /*Format code*/
		SCSIDevices[id].CmdBuffer[1] = cdrom->getcurrentsubchannel(&SCSIDevices[id].CmdBuffer[5], msf);
		break;
		
		case GPCMD_READ_TOC_PMA_ATIP:
		switch (SCSICDROM_TOC(id, cdb))
		{
			case 0: /*Normal*/
			datalen = cdrom->readtoc(SCSIDevices[id].CmdBuffer, cdb[6], msf, datalen, 0);
			break;
				
			case 1: /*Multi session*/
			datalen = cdrom->readtoc_session(SCSIDevices[id].CmdBuffer, msf, datalen);
			SCSIDevices[id].CmdBuffer[0] = 0; 
			SCSIDevices[id].CmdBuffer[1] = 0xA;
			break;
				
			case 2: /*Raw*/
			datalen = cdrom->readtoc_raw(SCSIDevices[id].CmdBuffer, msf, datalen);
			break;
		}
		break;
		
		case GPCMD_READ_HEADER:
		if (cdrom->read_header)
		{
			cdrom->read_header(cdb, SCSIDevices[id].CmdBuffer);
		}
		else
		{
			SectorLen=(cdb[7]<<8)|cdb[8];
			SectorLBA=(cdb[2]<<24)|(cdb[3]<<16)|(cdb[4]<<8)|cdb[5];
			if (msf)
			{
				real_pos = cdrom_LBAtoMSF_accurate();
			}
			else
			{
				real_pos = SectorLBA;
			}
			SCSIDevices[id].CmdBuffer[4] = (real_pos >> 24);
			SCSIDevices[id].CmdBuffer[5] = ((real_pos >> 16) & 0xff);
			SCSIDevices[id].CmdBuffer[6] = ((real_pos >> 8) & 0xff);
			SCSIDevices[id].CmdBuffer[7] = real_pos & 0xff;
			SCSIDevices[id].CmdBuffer[0]=1; /*2048 bytes user data*/
			SCSIDevices[id].CmdBuffer[1]=SCSIDevices[id].CmdBuffer[2]=SCSIDevices[id].CmdBuffer[3]=0;
		}
		break;
		
		case GPCMD_GET_CONFIGURATION:
		Index = 0;
 
		/* only feature 0 is supported */
		if (cdb[2] != 0 || cdb[3] != 0)
		{
			SCSIStatus = SCSI_STATUS_CHECK_CONDITION;			
			SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
			SCSICallback[id]=50*SCSI_TIME;
			return;
		}
 
		/*
		* XXX: avoid overflow for io_buffer if length is bigger than
		*      the size of that buffer (dimensioned to max number of
		*      sectors to transfer at once)
		*
		*      Only a problem if the feature/profiles grow.
		*/
		if (datalen > 512) /* XXX: assume 1 sector */
			datalen = 512;

		memset(SCSIDevices[id].CmdBuffer, 0, datalen);
		/*
		* the number of sectors from the media tells us which profile
		* to use as current.  0 means there is no media
		*/
		if (datalen > CD_MAX_SECTORS )
		{
			SCSIDevices[id].CmdBuffer[6] = (MMC_PROFILE_DVD_ROM >> 8) & 0xff;
			SCSIDevices[id].CmdBuffer[7] = MMC_PROFILE_DVD_ROM & 0xff;
		}
		else if (datalen <= CD_MAX_SECTORS)
		{
			SCSIDevices[id].CmdBuffer[6] = (MMC_PROFILE_CD_ROM >> 8) & 0xff;
			SCSIDevices[id].CmdBuffer[7] = MMC_PROFILE_CD_ROM & 0xff;
		}
		SCSIDevices[id].CmdBuffer[10] = 0x02 | 0x01; /* persistent and current */
		datalen = 12; /* headers: 8 + 4 */
		datalen += SCSICDROMSetProfile(SCSIDevices[id].CmdBuffer, &Index, MMC_PROFILE_DVD_ROM);
		datalen += SCSICDROMSetProfile(SCSIDevices[id].CmdBuffer, &Index, MMC_PROFILE_CD_ROM);
		SCSIDevices[id].CmdBuffer[0] = ((datalen-4) >> 24) & 0xff;
		SCSIDevices[id].CmdBuffer[1] = ((datalen-4) >> 16) & 0xff;
		SCSIDevices[id].CmdBuffer[2] = ((datalen-4) >> 8) & 0xff;
		SCSIDevices[id].CmdBuffer[3] = (datalen-4) & 0xff;
		break;
		
		case GPCMD_GET_EVENT_STATUS_NOTIFICATION:
		gesn_cdb = (void *)cdb;
		gesn_event_header = (void *)SCSIDevices[id].CmdBuffer;
			
		/* It is fine by the MMC spec to not support async mode operations */
		if (!(gesn_cdb->polled & 0x01))
		{   /* asynchronous mode */
			/* Only pollign is supported, asynchronous mode is not. */
			SCSIStatus = SCSI_STATUS_CHECK_CONDITION;				
			SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
			SCSICallback[id]=50*SCSI_TIME;
			return;
		}
			
		/* polling mode operation */
			
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
		if (gesn_cdb->class & (1 << GESN_MEDIA))
		{
			gesn_event_header->notification_class |= GESN_MEDIA;
			datalen = SCSICDROMEventStatus(SCSIDevices[id].CmdBuffer);
		}
		else
		{
			gesn_event_header->notification_class = 0x80; /* No event available */
			datalen = sizeof(*gesn_event_header);
		}
		gesn_event_header->len = datalen - sizeof(*gesn_event_header);
		break;
		
		case GPCMD_READ_DISC_INFORMATION:
		if (cdrom->read_disc_information)
		{
			cdrom->read_disc_information(SCSIDevices[id].CmdBuffer);
		}
		else
		{
			SCSIDevices[id].CmdBuffer[1] = 32;
			SCSIDevices[id].CmdBuffer[2] = 0xe; /* last session complete, disc finalized */
			SCSIDevices[id].CmdBuffer[3] = 1; /* first track on disc */
			SCSIDevices[id].CmdBuffer[4] = 1; /* # of sessions */
			SCSIDevices[id].CmdBuffer[5] = 1; /* first track of last session */
			SCSIDevices[id].CmdBuffer[6] = 1; /* last track of last session */
			SCSIDevices[id].CmdBuffer[7] = 0x20; /* unrestricted use */
			SCSIDevices[id].CmdBuffer[8] = 0x00; /* CD-ROM */
		}
		break;
	
		case GPCMD_READ_TRACK_INFORMATION:
		max_length = SCSIDevices[id].Cdb[7];
		max_length <<= 8;
		max_length |= SCSIDevices[id].Cdb[8];

		track = ((uint32_t) idebufferb[2]) << 24;
		track |= ((uint32_t) idebufferb[3]) << 16;
		track |= ((uint32_t) idebufferb[4]) << 8;
		track |= (uint32_t) idebufferb[5];

		if (cdrom->read_track_information)
		{
			ret = cdrom->read_track_information(SCSIDevices[id].Cdb, SCSIDevices[id].CmdBuffer);

			if (!ret)
			{
				SCSIStatus = SCSI_STATUS_CHECK_CONDITION;			
				SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
				if (SCSISense.UnitAttention)
				{
					SCSISenseCodeError(SENSE_UNIT_ATTENTION, ASC_MEDIUM_MAY_HAVE_CHANGED, 0);
				}
				SCSICallback[id]=50*SCSI_TIME;
				return;		
			}

			datalen = SCSIDevices[id].CmdBuffer[0];
			datalen <<= 8;
			datalen |= SCSIDevices[id].CmdBuffer[1];
			datalen += 2;
		}
		else
		{
			if (((SCSIDevices[id].Cdb[1] & 0x03) != 1) || (track != 1))
			{
				SCSIStatus = SCSI_STATUS_CHECK_CONDITION;			
				SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
				if (SCSISense.UnitAttention)
				{
					SCSISenseCodeError(SENSE_UNIT_ATTENTION, ASC_MEDIUM_MAY_HAVE_CHANGED, 0);
				}
				SCSICallback[id]=50*SCSI_TIME;
				return;		
			}

			datalen = 36;
			SCSIDevices[id].CmdBuffer[1] = 34;
			SCSIDevices[id].CmdBuffer[2] = 1; /* track number (LSB) */
			SCSIDevices[id].CmdBuffer[3] = 1; /* session number (LSB) */
			SCSIDevices[id].CmdBuffer[5] = (0 << 5) | (0 << 4) | (4 << 0); /* not damaged, primary copy, data track */
			SCSIDevices[id].CmdBuffer[6] = (0 << 7) | (0 << 6) | (0 << 5) | (0 << 6) | (1 << 0); /* not reserved track, not blank, not packet writing, not fixed packet, data mode 1 */
			SCSIDevices[id].CmdBuffer[7] = (0 << 1) | (0 << 0); /* last recorded address not valid, next recordable address not valid */
			SCSIDevices[id].CmdBuffer[8] = 0; /* track start address is 0 */
			SCSIDevices[id].CmdBuffer[24] = (cdrom->size() >> 24) & 0xff; /* track size */
			SCSIDevices[id].CmdBuffer[25] = (cdrom->size() >> 16) & 0xff; /* track size */
			SCSIDevices[id].CmdBuffer[26] = (cdrom->size() >> 8) & 0xff; /* track size */
			SCSIDevices[id].CmdBuffer[27] = cdrom->size() & 0xff; /* track size */
			SCSIDevices[id].CmdBuffer[32] = 0; /* track number (MSB) */
			SCSIDevices[id].CmdBuffer[33] = 0; /* session number (MSB) */
		} 

		if (datalen > max_length)
		{
			datalen = max_length;
			SCSIDevices[id].CmdBuffer[0] = ((max_length - 2) >> 8) & 0xff;
			SCSIDevices[id].CmdBuffer[1] = (max_length - 2) & 0xff;
		}
		break;
	
		case GPCMD_READ_DVD_STRUCTURE:
		memset(SCSIDevices[id].CmdBuffer, 0, datalen > 256 * 512 + 4 ? 256 * 512 + 4 : datalen);
				
		switch (cdb[7]) 
		{
			case 0x00 ... 0x7f:
			case 0xff:
			if (cdb[1] == 0) 
			{
				DVDRet = SCSICDROMReadDVDStructure(cdb[7], cdb, SCSIDevices[id].CmdBuffer);
	 
				if (DVDRet < 0)
				{
					SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, -DVDRet, 0x00);
				}
				break;
			}
			/* TODO: BD support, fall through for now */
	 
			/* Generic disk structures */
			case 0x80: /* TODO: AACS volume identifier */
			case 0x81: /* TODO: AACS media serial number */
			case 0x82: /* TODO: AACS media identifier */
			case 0x83: /* TODO: AACS media key block */
			case 0x90: /* TODO: List of recognized format layers */
			case 0xc0: /* TODO: Write protection status */
			default:
			SCSIStatus = SCSI_STATUS_CHECK_CONDITION;			
			SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
			if (SCSISense.UnitAttention)
			{
				SCSISenseCodeError(SENSE_UNIT_ATTENTION, ASC_MEDIUM_MAY_HAVE_CHANGED, 0);
			}
			SCSICallback[id]=50*SCSI_TIME;
			return;
		}
		break;
		
        case GPCMD_READ_CD_MSF:
        case GPCMD_READ_CD:
		pclog("Total data length requested: %d\n", datalen);
        while (datalen > 0)
        {
            read_length = cdrom_read_data(data); //Fill the buffer the data it needs
            if (!read_length)
            {
                SCSIStatus = SCSI_STATUS_CHECK_CONDITION;
                SCSISenseCodeError(SENSE_ILLEGAL_REQUEST, ASC_ILLEGAL_OPCODE, 0);
                SCSICallback[id]=50*SCSI_TIME;
                break;
            }
            else
            {
				//Continue reading data until the sector length is 0.
				data += read_length;
            }
           
            pclog("True LBA: %d, buffer half: %d\n", SectorLBA, SectorLen * cdrom_sector_size);
				
			SectorLBA++;
			SectorLen--;

			if (SectorLen == 0)
			{
				break;
			}
		}
		break;
		
		case GPCMD_MECHANISM_STATUS:
		SCSIDevices[id].CmdBuffer[0] = 0;
		SCSIDevices[id].CmdBuffer[1] = 0;
		SCSIDevices[id].CmdBuffer[2] = 0;
		SCSIDevices[id].CmdBuffer[3] = 0;
		SCSIDevices[id].CmdBuffer[4] = 0;
		SCSIDevices[id].CmdBuffer[5] = 1;
		SCSIDevices[id].CmdBuffer[6] = 0;
		SCSIDevices[id].CmdBuffer[7] = 0;
		break;
	}
}

void SCSICDROM_WriteData(uint8_t id, uint8_t *cdb, uint8_t *data, int datalen)
{
	switch (cdb[0])
	{
		case GPCMD_MODE_SELECT_6:
		case GPCMD_MODE_SELECT_10:		
		if (cdb[0] == GPCMD_MODE_SELECT_6)
		{
			prefix_len = 6;
		}
		else
		{
			prefix_len = 10;
		}
		
		page_current = cdb[2];
		if (page_flags[page_current] & PAGE_CHANGEABLE)
				page_flags[GPMODE_CDROM_AUDIO_PAGE] |= PAGE_CHANGED;
		break;
	}
}