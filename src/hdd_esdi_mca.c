/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Driver for IBM PS/2 ESDI disk controller (MCA)
 *
 *		  AdapterID:		0xDDFF
 *		  AdapterName:		"ESDI Fixed Disk Controller"
 *		  NumBytes 2
 *		  I/O base:		0x3510 - 0x3517
 *		  IRQ:			14
 *
 *		  Primary Board		pos[0]=XXxx xx0X	0x3510
 *		  Secondary Board	pos[0]=XXxx xx1X	0x3518
 *
 *		  DMA 5			pos[0]=XX01 01XX
 *		  DMA 6			pos[0]=XX01 10XX
 *		  DMA 7			pos[0]=XX01 11XX
 *		  DMA 0			pos[0]=XX00 00XX
 *		  DMA 1			pos[0]=XX00 01XX
 *		  DMA 3			pos[0]=XX00 11XX
 *		  DMA 4			pos[0]=XX01 00XX
 *
 *		  MCA Fairness ON	pos[0]=X1XX XXXX
 *		  MCA Fairness OFF	pos[0]=X0XX XXXX
 *
 *		  ROM C000		pos[1]=XXXX 0000
 *		  ROM C400		pos[1]=XXXX 0001
 *		  ROM C800		pos[1]=XXXX 0010
 *		  ROM CC00		pos[1]=XXXX 0011
 *		  ROM D000		pos[1]=XXXX 0100
 *		  ROM D400		pos[1]=XXXX 0101
 *		  ROM D800		pos[1]=XXXX 0110
 *		  ROM DC00		pos[1]=XXXX 0111
 *		  ROM Disabled		pos[1]=XXXX 1XXX
 *
 *		  DMA Burst 8		pos[1]=XX01 XXXX
 *		  DMA Burst 16		pos[1]=XX10 XXXX
 *		  DMA Burst 24		pos[1]=XX11 XXXX
 *		  DMA Disabled		pos[1]=XX00 XXXX
 *
 *		Although this is an MCA device, meaning that the system
 *		software will take care of device configuration, the ESDI
 *		controller is a somewhat weird one.. it's I/O base address
 *		and IRQ channel are locked to 0x3510 and IRQ14, possibly
 *		to enforce compatibility with the IBM MFM disk controller
 *		that was also in use on these systems. All other settings,
 *		however, are auto-configured by the system software as
 *		shown above.
 *
 * Version:	@(#)hdd_esdi_mca.c	1.0.1	2017/08/24
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <malloc.h>
#include "ibm.h"
#include "device.h"
#include "dma.h"
#include "hdd_image.h"
#include "io.h"
#include "mca.h"
#include "mem.h"
#include "pic.h"
#include "rom.h"
#include "timer.h"
#include "hdd_esdi_mca.h"


/* These are hardwired. */
#define ESDI_IOADDR	0x3510
#define ESDI_IRQCHAN	14


#define ESDI_TIME	(2000 * TIMER_USEC)
#define CMD_ADAPTER	0


typedef struct esdi_drive {
    int spt, hpc;
    int tracks;
    int sectors;
    int present;
    int hdc_num;
} esdi_drive_t;

typedef struct esdi {
    uint16_t	base;
    int8_t	irq;
    int8_t	dma;

    uint32_t	bios;
    rom_t	bios_rom;
       
    uint8_t	basic_ctrl;
    uint8_t	status;
    uint8_t	irq_status;
    int		irq_in_progress;
    int		cmd_req_in_progress;
    int		cmd_pos;
    uint16_t	cmd_data[4];
    int		cmd_dev;

    int		status_pos,
		status_len;

    uint16_t	status_data[256];

    int		data_pos;
    uint16_t	data[256];

    uint16_t	sector_buffer[256][256];

    int		sector_pos;
    int		sector_count;
                
    int		command;
    int		cmd_state;
        
    int		in_reset;
    int		callback;
        
    uint32_t	rba;
        
    struct {
        int req_in_progress;
    }		cmds[3];
        
    esdi_drive_t drives[2];

    uint8_t	pos_regs[8];
} esdi_t;

#define STATUS_DMA_ENA		(1 << 7)
#define STATUS_IRQ_PENDING	(1 << 6)
#define STATUS_CMD_IN_PROGRESS	(1 << 5)
#define STATUS_BUSY		(1 << 4)
#define STATUS_STATUS_OUT_FULL	(1 << 3)
#define STATUS_CMD_IR_FULL	(1 << 2)
#define STATUS_TRANSFER_REQ	(1 << 1)
#define STATUS_IRQ		(1 << 0)

#define CTRL_RESET		(1 << 7)
#define CTRL_DMA_ENA		(1 << 1)
#define CTRL_IRQ_ENA		(1 << 0)

#define IRQ_HOST_ADAPTER	(7 << 5)
#define IRQ_DEVICE_0    	(0 << 5)
#define IRQ_CMD_COMPLETE_SUCCESS 0x1
#define IRQ_RESET_COMPLETE       0xa
#define IRQ_DATA_TRANSFER_READY  0xb
#define IRQ_CMD_COMPLETE_FAILURE 0xc

#define ATTN_DEVICE_SEL		(7 << 5)
#define ATTN_HOST_ADAPTER	(7 << 5)
#define ATTN_DEVICE_0		(0 << 5)
#define ATTN_DEVICE_1		(1 << 5)
#define ATTN_REQ_MASK		0x0f
#define ATTN_CMD_REQ		1
#define ATTN_EOI		2
#define ATTN_RESET		4

#define CMD_SIZE_4 (1 << 14)

#define CMD_DEVICE_SEL     (7 << 5)
#define CMD_MASK           0x1f
#define CMD_READ           0x01
#define CMD_WRITE          0x02
#define CMD_READ_VERIFY    0x03
#define CMD_WRITE_VERIFY   0x04
#define CMD_SEEK           0x05
#define CMD_GET_DEV_CONFIG 0x09
#define CMD_GET_POS_INFO   0x0a

#define STATUS_LEN(x) ((x) << 8)
#define STATUS_DEVICE_HOST_ADAPTER (7 << 5)


static __inline void
esdi_set_irq(esdi_t *esdi)
{
    if (esdi->basic_ctrl & CTRL_IRQ_ENA)
	picint(1 << esdi->irq);
}


static __inline void
esdi_clear_irq(esdi_t *esdi)
{
        picintc(1 << esdi->irq);
}



static void
cmd_unsupported(esdi_t *esdi)
{
    esdi->status_len = 9;
    esdi->status_data[0] = esdi->command | STATUS_LEN(9) | esdi->cmd_dev;
    esdi->status_data[1] = 0x0f03; /*Attention error, command not supported*/
    esdi->status_data[2] = 0x0002; /*Interface fault*/
    esdi->status_data[3] = 0;
    esdi->status_data[4] = 0;
    esdi->status_data[5] = 0;
    esdi->status_data[6] = 0;
    esdi->status_data[7] = 0;
    esdi->status_data[8] = 0;

    esdi->status = STATUS_IRQ | STATUS_STATUS_OUT_FULL;
    esdi->irq_status = esdi->cmd_dev | IRQ_CMD_COMPLETE_FAILURE;
    esdi->irq_in_progress = 1;
    esdi_set_irq(esdi);
}


static void
device_not_present(esdi_t *esdi)
{
    esdi->status_len = 9;
    esdi->status_data[0] = esdi->command | STATUS_LEN(9) | esdi->cmd_dev;
    esdi->status_data[1] = 0x0c11; /*Command failed, internal hardware error*/
    esdi->status_data[2] = 0x000b; /*Selection error*/
    esdi->status_data[3] = 0;
    esdi->status_data[4] = 0;
    esdi->status_data[5] = 0;
    esdi->status_data[6] = 0;                        
    esdi->status_data[7] = 0;                       
    esdi->status_data[8] = 0;

    esdi->status = STATUS_IRQ | STATUS_STATUS_OUT_FULL;
    esdi->irq_status = esdi->cmd_dev | IRQ_CMD_COMPLETE_FAILURE;
    esdi->irq_in_progress = 1;
    esdi_set_irq(esdi);
}

#define ESDI_ADAPTER_ONLY() do \
        {                                                 \
                if (esdi->cmd_dev != ATTN_HOST_ADAPTER)   \
                {                                         \
                        cmd_unsupported(esdi);            \
                        return;                           \
                }                                         \
        } while (0)

#define ESDI_DRIVE_ONLY() do \
        {                                                                               \
                if (esdi->cmd_dev != ATTN_DEVICE_0 && esdi->cmd_dev != ATTN_DEVICE_1)   \
                {                                                                       \
                        cmd_unsupported(esdi);                                          \
                        return;                                                         \
                }                                                                       \
                if (esdi->cmd_dev == ATTN_DEVICE_0)                                     \
                        drive = &esdi->drives[0];                                       \
                else                                                                    \
                        drive = &esdi->drives[1];                                       \
        } while (0)
                

static void
esdi_callback(void *p)
{
    esdi_t *esdi = (esdi_t *)p;
    esdi_drive_t *drive;
    int val;

    esdi->callback = 0;

    /* If we are returning from a RESET, handle this first. */
    if (esdi->in_reset) {
	esdi->in_reset = 0;
	esdi->status = STATUS_IRQ;
	esdi->irq_status = IRQ_HOST_ADAPTER | IRQ_RESET_COMPLETE;

	return;
    }

    switch (esdi->command) {
	case CMD_READ:
                ESDI_DRIVE_ONLY();

                if (! drive->present) {
                        device_not_present(esdi);
                        return;
                }
                
                switch (esdi->cmd_state) {
                        case 0:
                        	esdi->rba = (esdi->cmd_data[2] | (esdi->cmd_data[3] << 16)) & 0x0fffffff;

                        	esdi->sector_pos = 0;
                        	esdi->sector_count = esdi->cmd_data[1];

                        	esdi->status = STATUS_IRQ | STATUS_CMD_IN_PROGRESS | STATUS_TRANSFER_REQ;
                        	esdi->irq_status = esdi->cmd_dev | IRQ_DATA_TRANSFER_READY;
                        	esdi->irq_in_progress = 1;
                        	esdi_set_irq(esdi);
                        
                        	esdi->cmd_state = 1;
                        	esdi->callback = ESDI_TIME;
                        	esdi->data_pos = 0;
                        	break;
                        
                        case 1:
                        	if (!(esdi->basic_ctrl & CTRL_DMA_ENA)) {
                                	esdi->callback = ESDI_TIME;
                                	return;
                        	}

                        	while (esdi->sector_pos < esdi->sector_count) {
                                	if (! esdi->data_pos) {
                                        	if (esdi->rba >= drive->sectors)
                                                	fatal("Read past end of drive\n");
						hdd_image_read(drive->hdc_num, esdi->rba, 1, (uint8_t *) esdi->data);
			                	update_status_bar_icon(SB_HDD | HDD_BUS_ESDI, 1);
                                	}

                                	while (esdi->data_pos < 256) {
                                        	val = dma_channel_write(esdi->dma, esdi->data[esdi->data_pos]);
                                
                                        	if (val == DMA_NODATA) {
                                                	esdi->callback = ESDI_TIME;
                                                	return;
                                        	}

                                        	esdi->data_pos++;
                                	}

                                	esdi->data_pos = 0;
                                	esdi->sector_pos++;
                                	esdi->rba++;
                        	}

                        	esdi->status = STATUS_CMD_IN_PROGRESS;
                        	esdi->cmd_state = 2;
                        	esdi->callback = ESDI_TIME;
                        	break;

                        case 2:
                        	esdi->status = STATUS_IRQ;
                        	esdi->irq_status = esdi->cmd_dev | IRQ_CMD_COMPLETE_SUCCESS;
                        	esdi->irq_in_progress = 1;
                        	esdi_set_irq(esdi);
                        	break;
                }
                break;

	case CMD_WRITE:
	case CMD_WRITE_VERIFY:
		ESDI_DRIVE_ONLY();
                if (! drive->present) {
                        device_not_present(esdi);
                        return;
                }
                
                switch (esdi->cmd_state) {
                        case 0:
                        	esdi->rba = (esdi->cmd_data[2] | (esdi->cmd_data[3] << 16)) & 0x0fffffff;

                        	esdi->sector_pos = 0;
                        	esdi->sector_count = esdi->cmd_data[1];

                        	esdi->status = STATUS_IRQ | STATUS_CMD_IN_PROGRESS | STATUS_TRANSFER_REQ;
                        	esdi->irq_status = esdi->cmd_dev | IRQ_DATA_TRANSFER_READY;
                        	esdi->irq_in_progress = 1;
                        	esdi_set_irq(esdi);
                        
                        	esdi->cmd_state = 1;
                        	esdi->callback = ESDI_TIME;
                        	esdi->data_pos = 0;
                        	break;

                        case 1:
				if (! (esdi->basic_ctrl & CTRL_DMA_ENA)) {
					esdi->callback = ESDI_TIME;
					return;
				}

				while (esdi->sector_pos < esdi->sector_count) {
                                	while (esdi->data_pos < 256) {
	                                        val = dma_channel_read(esdi->dma);
                                
                                        	if (val == DMA_NODATA) {
                                                	esdi->callback = ESDI_TIME;
                                                	return;
                                        	}

						esdi->data[esdi->data_pos++] = val & 0xffff;
                                	}

                                	if (esdi->rba >= drive->sectors)
                                        	fatal("Write past end of drive\n");
					hdd_image_write(drive->hdc_num, esdi->rba, 1, (uint8_t *) esdi->data);
                                	esdi->rba++;
                                	esdi->sector_pos++;
		                	update_status_bar_icon(SB_HDD | HDD_BUS_ESDI, 1);

                                	esdi->data_pos = 0;
                        	}
	                	update_status_bar_icon(SB_HDD | HDD_BUS_ESDI, 0);

                        	esdi->status = STATUS_CMD_IN_PROGRESS;
                        	esdi->cmd_state = 2;
                        	esdi->callback = ESDI_TIME;
                        	break;

                        case 2:
                        	esdi->status = STATUS_IRQ;
                        	esdi->irq_status = esdi->cmd_dev | IRQ_CMD_COMPLETE_SUCCESS;
                        	esdi->irq_in_progress = 1;
                        	esdi_set_irq(esdi);
                        	break;
                }
                break;

	case CMD_READ_VERIFY:
		ESDI_DRIVE_ONLY();

                if (! drive->present) {
                        device_not_present(esdi);
                        return;
                }

                esdi->status = STATUS_IRQ;
                esdi->irq_status = esdi->cmd_dev | IRQ_CMD_COMPLETE_SUCCESS;
                esdi->irq_in_progress = 1;
                esdi_set_irq(esdi);
                break;

	case CMD_SEEK:
		ESDI_DRIVE_ONLY();

                if (! drive->present) {
                        device_not_present(esdi);
                        return;
                }

                esdi->status = STATUS_IRQ;
                esdi->irq_status = esdi->cmd_dev | IRQ_CMD_COMPLETE_SUCCESS;
                esdi->irq_in_progress = 1;
                esdi_set_irq(esdi);
                break;

	case CMD_GET_DEV_CONFIG:
		ESDI_DRIVE_ONLY();

                if (! drive->present) {
                        device_not_present(esdi);
                        return;
                }

                if (esdi->status_pos)
                        fatal("Status send in progress\n");
                if ((esdi->status & STATUS_IRQ) || esdi->irq_in_progress)
                        fatal("IRQ in progress %02x %i\n", esdi->status, esdi->irq_in_progress);

                esdi->status_len = 6;
                esdi->status_data[0] = CMD_GET_POS_INFO | STATUS_LEN(6) | STATUS_DEVICE_HOST_ADAPTER;
                esdi->status_data[1] = 0x10; /*Zero defect*/
                esdi->status_data[2] = drive->sectors & 0xffff;
                esdi->status_data[3] = drive->sectors >> 16;
                esdi->status_data[4] = drive->tracks;
                esdi->status_data[5] = drive->hpc | (drive->spt << 16);

#if 0
		pclog("CMD_GET_DEV_CONFIG %i  %04x %04x %04x %04x %04x %04x\n",
			drive->sectors,
			esdi->status_data[0], esdi->status_data[1],
			esdi->status_data[2], esdi->status_data[3],
			esdi->status_data[4], esdi->status_data[5]);
#endif
                esdi->status = STATUS_IRQ | STATUS_STATUS_OUT_FULL;
                esdi->irq_status = esdi->cmd_dev | IRQ_CMD_COMPLETE_SUCCESS;
                esdi->irq_in_progress = 1;
                esdi_set_irq(esdi);
                break;

	case CMD_GET_POS_INFO:
                ESDI_ADAPTER_ONLY();
                if (esdi->status_pos)
                        fatal("Status send in progress\n");
                if ((esdi->status & STATUS_IRQ) || esdi->irq_in_progress)
                        fatal("IRQ in progress %02x %i\n", esdi->status, esdi->irq_in_progress);

                esdi->status_len = 5;
                esdi->status_data[0] = CMD_GET_POS_INFO | STATUS_LEN(5) | STATUS_DEVICE_HOST_ADAPTER;
                esdi->status_data[1] = 0xffdd; /*MCA ID*/
                esdi->status_data[2] = esdi->pos_regs[3] |
					(esdi->pos_regs[2] << 8);
                esdi->status_data[3] = 0xffff;
                esdi->status_data[4] = 0xffff;

                esdi->status = STATUS_IRQ | STATUS_STATUS_OUT_FULL;
                esdi->irq_status = IRQ_HOST_ADAPTER | IRQ_CMD_COMPLETE_SUCCESS;
                esdi->irq_in_progress = 1;
                esdi_set_irq(esdi);
                break;

	case 0x10:
		ESDI_ADAPTER_ONLY();
		switch (esdi->cmd_state) {
                        case 0:
                        	esdi->sector_pos = 0;
                        	esdi->sector_count = esdi->cmd_data[1];
                        	if (esdi->sector_count > 256)
                                	fatal("Write sector buffer count %04x\n", esdi->cmd_data[1]);

                        	esdi->status = STATUS_IRQ | STATUS_CMD_IN_PROGRESS | STATUS_TRANSFER_REQ;
                        	esdi->irq_status = IRQ_HOST_ADAPTER | IRQ_DATA_TRANSFER_READY;
                        	esdi->irq_in_progress = 1;
                        	esdi_set_irq(esdi);

                        	esdi->cmd_state = 1;
                        	esdi->callback = ESDI_TIME;
                        	esdi->data_pos = 0;
                        	break;
                        
                        case 1:
                        	if (! (esdi->basic_ctrl & CTRL_DMA_ENA)) {
                                	esdi->callback = ESDI_TIME;
                                	return;
                        	}
                        	while (esdi->sector_pos < esdi->sector_count) {
                                	while (esdi->data_pos < 256) {
                                        	val = dma_channel_read(esdi->dma);
                                
                                        	if (val == DMA_NODATA) {
                                                	esdi->callback = ESDI_TIME;
                                                	return;
                                        	}

                                        	esdi->data[esdi->data_pos++] = val & 0xffff;;
                                	}

                                	memcpy(esdi->sector_buffer[esdi->sector_pos++], esdi->data, 512);
                                	esdi->data_pos = 0;
                        	}

                        	esdi->status = STATUS_CMD_IN_PROGRESS;
                        	esdi->cmd_state = 2;
                        	esdi->callback = ESDI_TIME;
                        	break;

                        case 2:
				esdi->status = STATUS_IRQ;
				esdi->irq_status = IRQ_HOST_ADAPTER | IRQ_CMD_COMPLETE_SUCCESS;
				esdi->irq_in_progress = 1;
				esdi_set_irq(esdi);
				break;
		}
		break;

	case 0x11:
		ESDI_ADAPTER_ONLY();
                switch (esdi->cmd_state) {
                        case 0:
                        	esdi->sector_pos = 0;
                        	esdi->sector_count = esdi->cmd_data[1];
                        	if (esdi->sector_count > 256)
                                	fatal("Read sector buffer count %04x\n", esdi->cmd_data[1]);

				esdi->status = STATUS_IRQ | STATUS_CMD_IN_PROGRESS | STATUS_TRANSFER_REQ;
				esdi->irq_status = IRQ_HOST_ADAPTER | IRQ_DATA_TRANSFER_READY;
				esdi->irq_in_progress = 1;
				esdi_set_irq(esdi);

				esdi->cmd_state = 1;
				esdi->callback = ESDI_TIME;
				esdi->data_pos = 0;
				break;

                        case 1:
				if (! (esdi->basic_ctrl & CTRL_DMA_ENA)) {
                                	esdi->callback = ESDI_TIME;
                                	return;
                        	}

                        	while (esdi->sector_pos < esdi->sector_count) {
                                	if (! esdi->data_pos)
                                        	memcpy(esdi->data, esdi->sector_buffer[esdi->sector_pos++], 512);
                                	while (esdi->data_pos < 256) {
                                        	val = dma_channel_write(esdi->dma, esdi->data[esdi->data_pos]);
                                
                                        	if (val == DMA_NODATA) {
                                                	esdi->callback = ESDI_TIME;
                                                	return;
                                        	}

						esdi->data_pos++;
					}

					esdi->data_pos = 0;
                        	}

                        	esdi->status = STATUS_CMD_IN_PROGRESS;
                        	esdi->cmd_state = 2;
                        	esdi->callback = ESDI_TIME;
                        	break;
 
                        case 2:
				esdi->status = STATUS_IRQ;
				esdi->irq_status = IRQ_HOST_ADAPTER | IRQ_CMD_COMPLETE_SUCCESS;
				esdi->irq_in_progress = 1;
				esdi_set_irq(esdi);
				break;
		}
		break;

	case 0x12:
		ESDI_ADAPTER_ONLY();
		if (esdi->status_pos)
			fatal("Status send in progress\n");
		if ((esdi->status & STATUS_IRQ) || esdi->irq_in_progress)
			fatal("IRQ in progress %02x %i\n", esdi->status, esdi->irq_in_progress);

		esdi->status_len = 2;
		esdi->status_data[0] = 0x12 | STATUS_LEN(5) | STATUS_DEVICE_HOST_ADAPTER;
		esdi->status_data[1] = 0;

		esdi->status = STATUS_IRQ | STATUS_STATUS_OUT_FULL;
		esdi->irq_status = IRQ_HOST_ADAPTER | IRQ_CMD_COMPLETE_SUCCESS;
		esdi->irq_in_progress = 1;
		esdi_set_irq(esdi);
		break;

	default:
		fatal("BAD COMMAND %02x %i\n", esdi->command, esdi->cmd_dev);
    }
}


static uint8_t
esdi_read(uint16_t port, void *priv)
{
    esdi_t *esdi = (esdi_t *)priv;
    uint8_t temp = 0xff;

    switch (port-esdi->base) {
	case 2:					/*Basic status register*/
		temp = esdi->status;
		break;

	case 3:					/*IRQ status*/
		esdi->status &= ~STATUS_IRQ;
		temp = esdi->irq_status;
		break;

	default:
		fatal("esdi_read port=%04x\n", port);
    }

    return(temp);
}


static void
esdi_write(uint16_t port, uint8_t val, void *priv)
{
    esdi_t *esdi = (esdi_t *)priv;

#if 0
    pclog("ESDI: wr(%04x, %02x)\n", port-esdi->base, val);
#endif
    switch (port-esdi->base) {
	case 2:					/*Basic control register*/
		if ((esdi->basic_ctrl & CTRL_RESET) && !(val & CTRL_RESET)) {
			esdi->in_reset = 1;
			esdi->callback = ESDI_TIME * 50;
			esdi->status = STATUS_BUSY;
		}
		esdi->basic_ctrl = val;

		if (! (esdi->basic_ctrl & CTRL_IRQ_ENA))
			picintc(1 << esdi->irq);
		break;

	case 3:					/*Attention register*/
		switch (val & ATTN_DEVICE_SEL) {
			case ATTN_HOST_ADAPTER:
				switch (val & ATTN_REQ_MASK) {
					case ATTN_CMD_REQ:
						if (esdi->cmd_req_in_progress)
							fatal("Try to start command on in_progress adapter\n");
						esdi->cmd_req_in_progress = 1;
                                		esdi->cmd_dev = ATTN_HOST_ADAPTER;
						esdi->status |= STATUS_BUSY;
                                		esdi->cmd_pos = 0;
                                		break;

                               		case ATTN_EOI:
                               			esdi->irq_in_progress = 0;
                               			esdi->status &= ~STATUS_IRQ;
                               			esdi_clear_irq(esdi);
                               			break;

                               		case ATTN_RESET:
                               			esdi->in_reset = 1;
                               			esdi->callback = ESDI_TIME * 50;
                               			esdi->status = STATUS_BUSY;
                               			break;
                               
                               		default:
                               			fatal("Bad attention request %02x\n", val);
                       		}
                       		break;

                       	case ATTN_DEVICE_0:
                       		switch (val & ATTN_REQ_MASK) {
                               		case ATTN_CMD_REQ:
                               			if (esdi->cmd_req_in_progress)
                                       			fatal("Try to start command on in_progress device0\n");
                               			esdi->cmd_req_in_progress = 1;
                               			esdi->cmd_dev = ATTN_DEVICE_0;
                               			esdi->status |= STATUS_BUSY;
                               			esdi->cmd_pos = 0;
                               			break;
          
                               		case ATTN_EOI:
                               			esdi->irq_in_progress = 0;
                               			esdi->status &= ~STATUS_IRQ;
                               			esdi_clear_irq(esdi);
                               			break;
    
                               		default:
                               			fatal("Bad attention request %02x\n", val);
                       		}
                       		break;

                       	case ATTN_DEVICE_1:
                       		switch (val & ATTN_REQ_MASK) {
                               		case ATTN_CMD_REQ:
                               			if (esdi->cmd_req_in_progress)
                                       			fatal("Try to start command on in_progress device0\n");
                               			esdi->cmd_req_in_progress = 1;
                               			esdi->cmd_dev = ATTN_DEVICE_1;
                               			esdi->status |= STATUS_BUSY;
                               			esdi->cmd_pos = 0;
						break;

                               		case ATTN_EOI:
                               			esdi->irq_in_progress = 0;
                               			esdi->status &= ~STATUS_IRQ;
                               			esdi_clear_irq(esdi);
                               			break;
     
                               		default:
                               			fatal("Bad attention request %02x\n", val);
                       		}
                       		break;

                       	default:
                       		fatal("Attention to unknown device %02x\n", val);
               	}
               	break;

	default:
		fatal("esdi_write port=%04x val=%02x\n", port, val);
    }
}


static uint16_t
esdi_readw(uint16_t port, void *priv)
{
    esdi_t *esdi = (esdi_t *)priv;
    uint16_t temp = 0xffff;

    switch (port-esdi->base) {
	case 0:					/*Status Interface Register*/
		if (esdi->status_pos >= esdi->status_len)
			return(0);
		temp = esdi->status_data[esdi->status_pos++];
               	if (esdi->status_pos >= esdi->status_len) {
                       	esdi->status &= ~STATUS_STATUS_OUT_FULL;
                       	esdi->status_pos = esdi->status_len = 0;
               	}
               	break;

	default:
		fatal("esdi_readw port=%04x\n", port);
    }
        
    return(temp);
}


static void
esdi_writew(uint16_t port, uint16_t val, void *priv)
{
    esdi_t *esdi = (esdi_t *)priv;

#if 0
    pclog("ESDI: wrw(%04x, %04x)\n", port-esdi->base, val);
#endif
    switch (port-esdi->base) {
	case 0:					/*Command Interface Register*/
               	if (esdi->cmd_pos >= 4)
                       	fatal("CIR pos 4\n");
               	esdi->cmd_data[esdi->cmd_pos++] = val;
               	if (((esdi->cmd_data[0] & CMD_SIZE_4) && esdi->cmd_pos == 4) ||
               	    (!(esdi->cmd_data[0] & CMD_SIZE_4) && esdi->cmd_pos == 2)) {
                       	esdi->cmd_pos = 0;
                       	esdi->cmd_req_in_progress = 0;
                       	esdi->cmd_state = 0;

                       	if ((esdi->cmd_data[0] & CMD_DEVICE_SEL) != esdi->cmd_dev)
                               	fatal("Command device mismatch with attn\n");
                       	esdi->command = esdi->cmd_data[0] & CMD_MASK;
                       	esdi->callback = ESDI_TIME;
                       	esdi->status = STATUS_BUSY;
                       	esdi->data_pos = 0;
               	}
               	break;

	default:
		fatal("esdi_writew port=%04x val=%04x\n", port, val);
    }
}


static uint8_t
esdi_mca_read(int port, void *priv)
{
    esdi_t *esdi = (esdi_t *)priv;

#if 0
    pclog("ESDI: mcard(%04x)\n", port);
#endif
    return(esdi->pos_regs[port & 7]);
}


static void
esdi_mca_write(int port, uint8_t val, void *priv)
{
    esdi_t *esdi = (esdi_t *)priv;

#if 0
    pclog("ESDI: mcawr(%04x, %02x)  pos[2]=%02x pos[3]=%02x\n",
		port, val, esdi->pos_regs[2], esdi->pos_regs[3]);
#endif
    if (port < 0x102)
	return;

    /* Save the new value. */
    esdi->pos_regs[port & 7] = val;

    io_removehandler(esdi->base, 8,
		     esdi_read, esdi_readw, NULL,
		     esdi_write, esdi_writew, NULL, esdi);

    /* Disable the BIOS mapping. */
    mem_mapping_disable(&esdi->bios_rom.mapping);

    if (esdi->pos_regs[2] & 0x01) {
	/* OK, register (new) I/O handler. */
	io_sethandler(esdi->base, 8,
		      esdi_read, esdi_readw, NULL,
		      esdi_write, esdi_writew, NULL, esdi);

	/* Extract the new DMA channel. */
	switch(esdi->pos_regs[2] & 0x3c) {
		case 0x14:	/* DMA 5 [0]=XX01 01XX */
			esdi->dma = 5;
			break;

		case 0x18:	/* DMA 6 [0]=XX01 10XX */
			esdi->dma = 6;
			break;

		case 0x1c:	/* DMA 7 [0]=XX01 11XX */
			esdi->dma = 7;
			break;

		case 0x00:	/* DMA 0 [0]=XX00 00XX */
			esdi->dma = 0;
			break;

		case 0x01:	/* DMA 1 [0]=XX00 01XX */
			esdi->dma = 1;
			break;

		case 0x04:	/* DMA 3 [0]=XX00 11XX */
			esdi->dma = 3;
			break;

		case 0x10:	/* DMA 4 [0]=XX01 00XX */
			esdi->dma = 4;
			break;
	}

	/* Extract the new BIOS address. */
	if (! (esdi->pos_regs[3] & 0x08)) switch(esdi->pos_regs[3] & 0x0f) {
		case 0:		/* ROM C000 [1]=XXXX 0000 */
			esdi->bios = 0xC0000;
			break;

		case 1:		/* ROM C400 [1]=XXXX 0001 */
			esdi->bios = 0xC4000;
			break;

		case 2:		/* ROM C800 [1]=XXXX 0010 */
			esdi->bios = 0xC8000;
			break;

		case 3:		/* ROM CC00 [1]=XXXX 0011 */
			esdi->bios = 0xCC000;
			break;

		case 4:		/* ROM D000 [1]=XXXX 0100 */
			esdi->bios = 0xD0000;
			break;

		case 5:		/* ROM D400 [1]=XXXX 0101 */
			esdi->bios = 0xD4000;
			break;

		case 6:		/* ROM D800 [1]=XXXX 0110 */
			esdi->bios = 0xD8000;
			break;

		case 7:		/* ROM DC00 [1]=XXXX 0111 */
			esdi->bios = 0xC0000;
			break;

	} else {
		/* BIOS ROM disabled. */
		esdi->bios = 0x000000;
	}

	/* Enable or disable the BIOS ROM. */
	if (esdi->bios != 0x000000) {
		mem_mapping_enable(&esdi->bios_rom.mapping);
		mem_mapping_set_addr(&esdi->bios_rom.mapping,
				     esdi->bios, 0x4000);
	} else {
		mem_mapping_disable(&esdi->bios_rom.mapping);
	}

	/* Say hello. */
	pclog("ESDI: I/O=%04x, IRQ=%d, DMA=%d, BIOS @ %06X\n",
		esdi->base, esdi->irq, esdi->dma, esdi->bios);
    }
}


static void *
esdi_init(void)
{
    esdi_drive_t *drive;
    esdi_t *esdi;
    int c, i;

    esdi = malloc(sizeof(esdi_t));
    if (esdi == NULL) return(NULL);
    memset(esdi, 0x00, sizeof(esdi_t));

    /* These are hardwired. */
    esdi->base = ESDI_IOADDR;
    esdi->irq = ESDI_IRQCHAN;

    rom_init_interleaved(&esdi->bios_rom,
			 L"roms/hdd/esdi/90x8970.bin",
			 L"roms/hdd/esdi/90x8969.bin",
			 0xc8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);
    mem_mapping_disable(&esdi->bios_rom.mapping);

    esdi->drives[0].present = esdi->drives[1].present = 0;

    for (c=0,i=0; i<HDC_NUM; i++) {
	if ((hdc[i].bus == HDD_BUS_ESDI) && (hdc[i].esdi_channel < ESDI_NUM)) {
		/* This is an ESDI drive. */
		drive = &esdi->drives[hdc[i].esdi_channel];

		/* Try to load an image for the drive. */
		if (! hdd_image_load(i)) {
			/* Nope. */
			drive->present = 0;
			continue;
		}

		/* OK, so fill in geometry info. */
       		drive->spt = hdc[i].spt;
       		drive->hpc = hdc[i].hpc;
       		drive->tracks = hdc[i].tracks;
       		drive->sectors = hdc[i].spt*hdc[i].hpc*hdc[i].tracks;
		drive->hdc_num = i;

		/* Mark drive as present. */
		drive->present = 1;
	}

	if (++c >= ESDI_NUM) break;
    }

    /* Set the MCA ID for this controller, 0xFFDD. */
    esdi->pos_regs[0] = 0xff;
    esdi->pos_regs[1] = 0xdd;

    /* Enable the device. */
    mca_add(esdi_mca_read, esdi_mca_write, esdi);

    /* Mark for a reset. */
    esdi->in_reset = 1;
    esdi->callback = ESDI_TIME * 50;
    esdi->status = STATUS_BUSY;

    /* Set the reply timer. */
    timer_add(esdi_callback, &esdi->callback, &esdi->callback, esdi);

    return(esdi);
}


static void
esdi_close(void *p)
{
    esdi_t *esdi = (esdi_t *)p;
    esdi_drive_t *drive;
    int d;

    esdi->drives[0].present = esdi->drives[1].present = 0;

    for (d=0; d<2; d++) {
	drive = &esdi->drives[d];

	hdd_image_close(drive->hdc_num);
    }

    free(esdi);
}


static int esdi_available(void)
{
    return(rom_present(L"roms/hdd/esdi/90x8969.bin") &&
	   rom_present(L"roms/hdd/esdi/90x8970.bin"));
}


device_t hdd_esdi_device =
{
    "IBM ESDI Fixed Disk Adapter (MCA)",
    DEVICE_MCA,
    esdi_init,
    esdi_close,
    esdi_available,
    NULL,
    NULL,
    NULL,
    NULL
};
