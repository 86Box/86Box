#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#include <errno.h>
#include <malloc.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <sys/types.h>

#include "ibm.h"
#include "device.h"
#include "io.h"
#include "pic.h"
#include "timer.h"

#include "mfm_at.h"


#define IDE_TIME (TIMER_USEC*10)

#define STAT_ERR		0x01
#define STAT_INDEX		0x02
#define STAT_CORRECTED_DATA	0x04
#define STAT_DRQ		0x08 /* Data request */
#define STAT_DSC                0x10
#define STAT_SEEK_COMPLETE      0x20
#define STAT_READY		0x40
#define STAT_BUSY		0x80

#define ERR_DAM_NOT_FOUND       0x01 /*Data Address Mark not found*/
#define ERR_TR000               0x02 /*Track 0 not found*/
#define ERR_ABRT		0x04 /*Command aborted*/
#define ERR_ID_NOT_FOUND	0x10 /*ID not found*/
#define ERR_DATA_CRC	        0x40 /*Data CRC error*/
#define ERR_BAD_BLOCK	        0x80 /*Bad Block detected*/

#define CMD_RESTORE			0x10
#define CMD_READ			0x20
#define CMD_WRITE			0x30
#define CMD_VERIFY			0x40
#define CMD_FORMAT			0x50
#define CMD_SEEK   			0x70
#define CMD_DIAGNOSE                    0x90
#define CMD_SET_PARAMETERS              0x91

typedef struct mfm_drive_t
{
        int spt, hpc;
        int tracks;
        int cfg_spt;
        int cfg_hpc;
        int current_cylinder;
        FILE *hdfile;
} mfm_drive_t;

typedef struct mfm_t
{
        uint8_t status;
        uint8_t error;
        int secount,sector,cylinder,head,cylprecomp;
        uint8_t command;
        uint8_t fdisk;
        int pos;
        
        int drive_sel;
        int reset;
        uint16_t buffer[256];
        int irqstat;
        
        int callback;
        
        mfm_drive_t drives[2];
} mfm_t;

uint16_t mfm_readw(uint16_t port, void *p);
void mfm_writew(uint16_t port, uint16_t val, void *p);

static __inline void mfm_irq_raise(mfm_t *mfm)
{
	if (!(mfm->fdisk&2))
                picint(1 << 14);

	mfm->irqstat=1;
}

static __inline void mfm_irq_lower(mfm_t *mfm)
{
        picintc(1 << 14);
}

void mfm_irq_update(mfm_t *mfm)
{
	if (mfm->irqstat && !((pic2.pend|pic2.ins)&0x40) && !(mfm->fdisk & 2))
                picint(1 << 14);
}

/*
 * Return the sector offset for the current register values
 */
static int mfm_get_sector(mfm_t *mfm, off64_t *addr)
{
        mfm_drive_t *drive = &mfm->drives[mfm->drive_sel];
        int heads = drive->cfg_hpc;
        int sectors = drive->cfg_spt;
        
        if (drive->current_cylinder != mfm->cylinder)
        {
                pclog("mfm_get_sector: wrong cylinder\n");
                return 1;
        }
        if (mfm->head > heads)
        {
                pclog("mfm_get_sector: past end of configured heads\n");
                return 1;
        }
        if (mfm->sector >= sectors+1)
        {
                pclog("mfm_get_sector: past end of configured sectors\n");
                return 1;
        }
        if (mfm->head > drive->hpc)
        {
                pclog("mfm_get_sector: past end of heads\n");
                return 1;
        }
        if (mfm->sector >= drive->spt+1)
        {
                pclog("mfm_get_sector: past end of sectors\n");
                return 1;
        }

        *addr = ((((off64_t) mfm->cylinder * heads) + mfm->head) *
        	          sectors) + (mfm->sector - 1);
        
        return 0;
}

/**
 * Move to the next sector using CHS addressing
 */
static void mfm_next_sector(mfm_t *mfm)
{
        mfm_drive_t *drive = &mfm->drives[mfm->drive_sel];
        
        mfm->sector++;
        if (mfm->sector == (drive->cfg_spt + 1))
        {
        	mfm->sector = 1;
        	mfm->head++;
        	if (mfm->head == drive->cfg_hpc)
                {
        		mfm->head = 0;
        		mfm->cylinder++;
        		if (drive->current_cylinder < drive->tracks)
                		drive->current_cylinder++;
		}
	}
}

static void loadhd(mfm_t *mfm, int c, int d, const char *fn)
{
        mfm_drive_t *drive = &mfm->drives[c];
        
	if (drive->hdfile == NULL)
        {
		/* Try to open existing hard disk image */
		drive->hdfile = fopen64(fn, "rb+");
		if (drive->hdfile == NULL)
                {
			/* Failed to open existing hard disk image */
			if (errno == ENOENT)
                        {
				/* Failed because it does not exist,
				   so try to create new file */
				drive->hdfile = fopen64(fn, "wb+");
				if (drive->hdfile == NULL)
                                {
					pclog("Cannot create file '%s': %s",
					      fn, strerror(errno));
					return;
				}
			}
                        else
                        {
				/* Failed for another reason */
				pclog("Cannot open file '%s': %s",
				      fn, strerror(errno));
				return;
			}
		}
	}

        drive->spt = hdc[d].spt;
        drive->hpc = hdc[d].hpc;
        drive->tracks = hdc[d].tracks;
}



void mfm_write(uint16_t port, uint8_t val, void *p)
{
        mfm_t *mfm = (mfm_t *)p;

        switch (port)
        {
                case 0x1F0: /* Data */
                mfm_writew(port, val | (val << 8), p);
                return;

                case 0x1F1: /* Write precompenstation */
                mfm->cylprecomp = val;
                return;

                case 0x1F2: /* Sector count */
                mfm->secount = val;
                return;

                case 0x1F3: /* Sector */
                mfm->sector = val;
                return;

                case 0x1F4: /* Cylinder low */
                mfm->cylinder = (mfm->cylinder & 0xFF00) | val;
                return;

                case 0x1F5: /* Cylinder high */
                mfm->cylinder = (mfm->cylinder & 0xFF) | (val << 8);
                return;

                case 0x1F6: /* Drive/Head */
                mfm->head = val & 0xF;
                mfm->drive_sel = (val & 0x10) ? 1 : 0;
                if (mfm->drives[mfm->drive_sel].hdfile == NULL)
                        mfm->status = 0;
                else
                        mfm->status = STAT_READY | STAT_DSC;
                return;

                case 0x1F7: /* Command register */
                if (mfm->drives[mfm->drive_sel].hdfile == NULL)
                        fatal("Command on non-present drive\n");

                mfm_irq_lower(mfm);
                mfm->command = val;
                mfm->error = 0;
                
                switch (val & 0xf0)
                {
                        case CMD_RESTORE:
                        mfm->command &= ~0x0f; /*Mask off step rate*/
                        mfm->status = STAT_BUSY;
                        timer_process();
                        mfm->callback = 200*IDE_TIME;
                        timer_update_outstanding();
                        break;
                        
                        case CMD_SEEK:
                        mfm->command &= ~0x0f; /*Mask off step rate*/
                        mfm->status = STAT_BUSY;
                        timer_process();
                        mfm->callback = 200*IDE_TIME;
                        timer_update_outstanding();
                        break;
                        
                        default:
                        switch (val)
                        {
                                case CMD_READ: case CMD_READ+1:
                                case CMD_READ+2: case CMD_READ+3:
                                mfm->command &= ~3;
                                if (val & 2)
                                        fatal("Read with ECC\n");
                                mfm->status = STAT_BUSY;
                                timer_process();
                                mfm->callback = 200*IDE_TIME;
                                timer_update_outstanding();
                                break;

                                case CMD_WRITE: case CMD_WRITE+1:
                                case CMD_WRITE+2: case CMD_WRITE+3:
                                mfm->command &= ~3;
                                if (val & 2)
                                        fatal("Write with ECC\n");
                                mfm->status = STAT_DRQ | STAT_DSC;
                                mfm->pos=0;
                                break;

                                case CMD_VERIFY: case CMD_VERIFY+1:
                                mfm->command &= ~1;
                                mfm->status = STAT_BUSY;
                                timer_process();
                                mfm->callback = 200 * IDE_TIME;
                                timer_update_outstanding();
                                break;

                                case CMD_FORMAT:
                                mfm->status = STAT_DRQ | STAT_BUSY;
                                mfm->pos=0;
                                break;

                                case CMD_SET_PARAMETERS: /* Initialize Drive Parameters */
                                mfm->status = STAT_BUSY;
                                timer_process();
                                mfm->callback = 30*IDE_TIME;
                                timer_update_outstanding();
                                break;

                                case CMD_DIAGNOSE: /* Execute Drive Diagnostics */
                                mfm->status = STAT_BUSY;
                                timer_process();
                                mfm->callback = 200*IDE_TIME;
                                timer_update_outstanding();
                                break;

                                default:
                                pclog("Bad MFM command %02X\n", val);
                                mfm->status = STAT_BUSY;
                                timer_process();
                                mfm->callback = 200*IDE_TIME;
                                timer_update_outstanding();
                                break;
                        }
                }                
                break;

                case 0x3F6: /* Device control */
                if ((mfm->fdisk & 4) && !(val & 4))
                {
			timer_process();
                        mfm->callback = 500*IDE_TIME;
                        timer_update_outstanding();
                        mfm->reset = 1;
                        mfm->status = STAT_BUSY;
                }
                if (val & 4)
                {
                        /*Drive held in reset*/
			timer_process();
                        mfm->callback = 0;
                        timer_update_outstanding();
                        mfm->status = STAT_BUSY;
                }
                mfm->fdisk = val;
                mfm_irq_update(mfm);
                return;
        }
}

void mfm_writew(uint16_t port, uint16_t val, void *p)
{
        mfm_t *mfm = (mfm_t *)p;
        
        mfm->buffer[mfm->pos >> 1] = val;
        mfm->pos += 2;

        if (mfm->pos >= 512)
        {
                mfm->pos = 0;
                mfm->status = STAT_BUSY;
                timer_process();
              	mfm->callback = 6*IDE_TIME;
                timer_update_outstanding();
        }
}

uint8_t mfm_read(uint16_t port, void *p)
{
        mfm_t *mfm = (mfm_t *)p;
        uint8_t temp;

        switch (port)
        {
                case 0x1F0: /* Data */
                temp = mfm_readw(port, mfm) & 0xff;
                break;
                
                case 0x1F1: /* Error */
                temp = mfm->error;
                break;

                case 0x1F2: /* Sector count */
                temp = (uint8_t)mfm->secount;
                break;

                case 0x1F3: /* Sector */
                temp = (uint8_t)mfm->sector;
                break;

                case 0x1F4: /* Cylinder low */
                temp = (uint8_t)(mfm->cylinder&0xFF);
                break;

                case 0x1F5: /* Cylinder high */
                temp = (uint8_t)(mfm->cylinder>>8);
                break;

                case 0x1F6: /* Drive/Head */
                temp = (uint8_t)(mfm->head | (mfm->drive_sel ? 0x10 : 0) | 0xa0);
                break;

                case 0x1F7: /* Status */
                mfm_irq_lower(mfm);
                temp = mfm->status;
                break;

		default:
		temp = 0xff;
		break;
        }

        return temp;
}

uint16_t mfm_readw(uint16_t port, void *p)
{
        mfm_t *mfm = (mfm_t *)p;
        uint16_t temp;

        temp = mfm->buffer[mfm->pos >> 1];
        mfm->pos += 2;

        if (mfm->pos >= 512)
        {
                mfm->pos=0;
                mfm->status = STAT_READY | STAT_DSC;
                if (mfm->command == CMD_READ)
                {
                        mfm->secount = (mfm->secount - 1) & 0xff;
                        if (mfm->secount)
                        {
                                mfm_next_sector(mfm);
                                mfm->status = STAT_BUSY;
                                timer_process();
                                mfm->callback = 6*IDE_TIME;
                                timer_update_outstanding();
                        }
			else
			{
				update_status_bar_icon(0x20, 0);
			}
                }
        }
        
        return temp;
}

static void do_seek(mfm_t *mfm)
{
        mfm_drive_t *drive = &mfm->drives[mfm->drive_sel];
        
        if (mfm->cylinder < drive->tracks)
                drive->current_cylinder = mfm->cylinder;
        else
                drive->current_cylinder = drive->tracks-1;
}

void mfm_callback(void *p)
{
        mfm_t *mfm = (mfm_t *)p;
        mfm_drive_t *drive = &mfm->drives[mfm->drive_sel];
        off64_t addr;
        int c;
        
        mfm->callback = 0;
        if (mfm->reset)
        {
                mfm->status = STAT_READY | STAT_DSC;
                mfm->error = 1;
                mfm->secount = 1;
                mfm->sector = 1;
                mfm->head = 0;
                mfm->cylinder = 0;
                mfm->reset = 0;
                return;
        }
        switch (mfm->command)
        {
                case CMD_RESTORE:
                drive->current_cylinder = 0;
                mfm->status = STAT_READY | STAT_DSC;
                mfm_irq_raise(mfm);
                break;

                case CMD_SEEK:
                do_seek(mfm);
                mfm->status = STAT_READY | STAT_DSC;
                mfm_irq_raise(mfm);
                break;

                case CMD_READ:
                do_seek(mfm);
                if (mfm_get_sector(mfm, &addr))
                {
                        mfm->error = ERR_ID_NOT_FOUND;
                        mfm->status = STAT_READY | STAT_DSC | STAT_ERR;
                        mfm_irq_raise(mfm);
                        break;
                }
                
                fseeko64(drive->hdfile, addr * 512, SEEK_SET);
                fread(mfm->buffer, 512, 1, drive->hdfile);
                mfm->pos = 0;
                mfm->status = STAT_DRQ | STAT_READY | STAT_DSC;
                mfm_irq_raise(mfm);
                update_status_bar_icon(0x20, 1);
                break;

                case CMD_WRITE:
                do_seek(mfm);
                if (mfm_get_sector(mfm, &addr))
                {
                        mfm->error = ERR_ID_NOT_FOUND;
                        mfm->status = STAT_READY | STAT_DSC | STAT_ERR;
                        mfm_irq_raise(mfm);
                        break;
                }
                fseeko64(drive->hdfile, addr * 512, SEEK_SET);
                fwrite(mfm->buffer, 512, 1, drive->hdfile);
                mfm_irq_raise(mfm);
                mfm->secount = (mfm->secount - 1) & 0xff;
                if (mfm->secount)
                {
                        mfm->status = STAT_DRQ | STAT_READY | STAT_DSC;
                        mfm->pos = 0;
                        mfm_next_sector(mfm);
	                update_status_bar_icon(0x20, 1);
                }
                else
		{
                        mfm->status = STAT_READY | STAT_DSC;
	                update_status_bar_icon(0x20, 0);
		}
                break;
                
                case CMD_VERIFY:
                do_seek(mfm);
                mfm->pos = 0;
                mfm->status = STAT_READY | STAT_DSC;
                mfm_irq_raise(mfm);
                update_status_bar_icon(0x20, 1);
                break;

                case CMD_FORMAT:
                do_seek(mfm);
                if (mfm_get_sector(mfm, &addr))
                {
                        mfm->error = ERR_ID_NOT_FOUND;
                        mfm->status = STAT_READY | STAT_DSC | STAT_ERR;
                        mfm_irq_raise(mfm);
                        break;
                }
                fseeko64(drive->hdfile, addr * 512, SEEK_SET);
                memset(mfm->buffer, 0, 512);
                for (c = 0; c < mfm->secount; c++)
                {
                        fwrite(mfm->buffer, 512, 1, drive->hdfile);
                }
                mfm->status = STAT_READY | STAT_DSC;
                mfm_irq_raise(mfm);
                update_status_bar_icon(0x20, 1);
                break;

                case CMD_DIAGNOSE:
                mfm->error = 1; /*No error detected*/
		mfm->status = STAT_READY | STAT_DSC;
		mfm_irq_raise(mfm);
                break;

                case CMD_SET_PARAMETERS: /* Initialize Drive Parameters */
                drive->cfg_spt = mfm->secount;
                drive->cfg_hpc = mfm->head+1;
                pclog("Parameters: spt=%i hpc=%i\n", drive->cfg_spt,drive->cfg_hpc);
                mfm->status = STAT_READY | STAT_DSC;
                mfm_irq_raise(mfm);
                break;
                
                default:
                pclog("Callback on unknown command %02x\n", mfm->command);
        	mfm->status = STAT_READY | STAT_ERR | STAT_DSC;
        	mfm->error = ERR_ABRT;
        	mfm_irq_raise(mfm);
        	break;
        }
}

void *mfm_init()
{
	int c, d;

        mfm_t *mfm = malloc(sizeof(mfm_t));
        memset(mfm, 0, sizeof(mfm_t));

	c = 0;
	for (d = 0; d < HDC_NUM; d++)
	{
		if ((hdc[d].bus == 1) && (hdc[d].mfm_channel < MFM_NUM))
		{
			loadhd(mfm, hdc[d].mfm_channel, d, hdd_fn[d]);
			c++;
			if (c >= MFM_NUM)  break;
		}
	}

        mfm->status = STAT_READY | STAT_DSC;
        mfm->error = 1; /*No errors*/

        io_sethandler(0x01f0, 0x0001, mfm_read, mfm_readw, NULL, mfm_write, mfm_writew, NULL, mfm);
        io_sethandler(0x01f1, 0x0007, mfm_read, NULL,      NULL, mfm_write, NULL,       NULL, mfm);
        io_sethandler(0x03f6, 0x0001, NULL,     NULL,      NULL, mfm_write, NULL,       NULL, mfm);

        timer_add(mfm_callback, &mfm->callback, &mfm->callback, mfm);	
        
	return mfm;
}

void mfm_close(void *p)
{
        mfm_t *mfm = (mfm_t *)p;
        int d;

        for (d = 0; d < 2; d++)
        {
                mfm_drive_t *drive = &mfm->drives[d];
                
                if (drive->hdfile != NULL)
                        fclose(drive->hdfile);
        }

        free(mfm);
}

device_t mfm_at_device =
{
        "IBM PC AT Fixed Disk Adapter",
        DEVICE_AT,
        mfm_init,
        mfm_close,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
};
