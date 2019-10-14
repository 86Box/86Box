/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the NCR 5380 series of SCSI Host Adapters
 *		made by NCR. These controllers were designed for the ISA bus.
 *
 * Version:	@(#)scsi_ncr5380.c	1.0.24	2018/10/28
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017,2018 Sarah Walker.
 *		Copyright 2017,2018 TheCollector1995.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../io.h"
#include "../timer.h"
#include "../dma.h"
#include "../pic.h"
#include "../mca.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "../nvr.h"
#include "../plat.h"
#include "scsi.h"
#include "scsi_device.h"
#include "scsi_ncr5380.h"


#define LCS6821N_ROM	L"roms/scsi/ncr5380/Longshine LCS-6821N - BIOS version 1.04.bin"
#define RT1000B_ROM	L"roms/scsi/ncr5380/Rancho_RT1000_RTBios_version_8.10R.bin"
#define T130B_ROM	L"roms/scsi/ncr5380/trantor_t130b_bios_v2.14.bin"
#if defined(DEV_BRANCH) && defined(USE_SUMO)
#define SCSIAT_ROM	L"roms/scsi/ncr5380/sumo_scsiat_bios_v6.3.bin"
#endif
#define T128_ROM	L"roms/scsi/ncr5380/trantor_t128_bios_v1.12.bin"


#define NCR_CURDATA	0		/* current SCSI data (read only) */
#define NCR_OUTDATA 	0		/* output data (write only) */
#define NCR_INITCOMMAND 1		/* initiator command (read/write) */
#define NCR_MODE	2		/* mode (read/write) */
#define NCR_TARGETCMD	3		/* target command (read/write) */
#define NCR_SELENABLE	4		/* select enable (write only) */
#define NCR_BUSSTATUS	4		/* bus status (read only) */
#define NCR_STARTDMA	5		/* start DMA send (write only) */
#define NCR_BUSANDSTAT	5		/* bus and status (read only) */
#define NCR_DMATARGET	6		/* DMA target (write only) */
#define NCR_INPUTDATA	6		/* input data (read only) */
#define NCR_DMAINIRECV	7		/* DMA initiator receive (write only) */
#define NCR_RESETPARITY	7		/* reset parity/interrupt (read only) */

#define ICR_DBP			0x01
#define ICR_ATN			0x02
#define ICR_SEL			0x04
#define ICR_BSY			0x08
#define ICR_ACK			0x10
#define ICR_ARB_LOST		0x20
#define ICR_ARB_IN_PROGRESS	0x40

#define MODE_ARBITRATE		0x01
#define MODE_DMA		0x02
#define MODE_MONITOR_BUSY	0x04
#define MODE_ENA_EOP_INT	0x08

#define STATUS_ACK		0x01
#define STATUS_BUSY_ERROR	0x04
#define STATUS_PHASE_MATCH	0x08
#define STATUS_INT		0x10
#define STATUS_DRQ		0x40
#define STATUS_END_OF_DMA	0x80

#define TCR_IO			0x01
#define TCR_CD			0x02
#define TCR_MSG			0x04
#define TCR_REQ			0x08
#define TCR_LAST_BYTE_SENT	0x80

#define CTRL_DATA_DIR		0x40
#define STATUS_BUFFER_NOT_READY	0x04
#define STATUS_53C80_ACCESSIBLE 0x80

typedef struct {	
    uint8_t	icr;
    uint8_t	mode;
    uint8_t	tcr;
    uint8_t	ser;
    uint8_t	isr;
    uint8_t	output_data;

    uint8_t	target_id;

    int		dma_mode;
	
	uint32_t bus_host, cur_bus;
	int 	new_phase;
	int 	state;
	
	int		clear_req, wait_data, wait_complete;
	
	int		command_pos;
	uint8_t command[20];
	int 	data_pos;
	uint8_t	tx_data;
	int 	icr_is_on;
	
	uint8_t unk_08, unk_08_ret;
} ncr_t;

typedef struct {
    ncr_t	ncr;

    const char	*name;

    uint8_t	buffer[128];
    uint8_t	int_ram[0x40], ext_ram[0x600];

    uint32_t	rom_addr;
    uint16_t	base;

    int8_t	irq;
    int8_t	type;
    uint8_t	block_count;
    uint8_t	status_ctrl;
    uint8_t	pad[2];

    rom_t	bios_rom;
    mem_mapping_t mapping;

    int		block_count_loaded;

    int		buffer_pos;
    int		buffer_host_pos;

    int		dma_enabled;

    pc_timer_t	timer;
    double	period;

    int		ncr_busy;
	
	int 	dma_is_write;
} ncr5380_t;

#define STATE_IDLE 0
#define STATE_COMMAND 1
#define STATE_DATAIN 2
#define STATE_DATAOUT 3
#define STATE_STATUS 4
#define STATE_MESSAGEIN 5
#define STATE_SELECT 6
#define STATE_DATAIN_DMA 7
#define STATE_DATAOUT_DMA 8

#define DMA_IDLE		0
#define DMA_SEND		1
#define DMA_INITIATOR_RECEIVE 2

static int cmd_len[8] = {6, 10, 10, 6, 16, 12, 6, 6};


#ifdef ENABLE_NCR5380_LOG
int ncr5380_do_log = ENABLE_NCR5380_LOG;


static void
ncr_log(const char *fmt, ...)
{
    va_list ap;

    if (ncr5380_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define ncr_log(fmt, ...)
#endif


#define SET_BUS_STATE(ncr, state) ncr->cur_bus = (ncr->cur_bus & ~(SCSI_PHASE_MESSAGE_IN)) | (state & (SCSI_PHASE_MESSAGE_IN))

static void
ncr_wait_process(ncr5380_t *ncr_dev);
static void
ncr_callback(void *priv);


static int
get_dev_id(uint8_t data)
{
    int c;

    for (c = 0; c < SCSI_ID_MAX; c++) {
	if (data & (1 << c)) return(c);
    }

    return(-1);
}


static void
ncr_reset(ncr5380_t *ncr_dev)
{
	ncr_t *ncr = &ncr_dev->ncr;
	
    memset(ncr, 0x00, sizeof(ncr_t));
    ncr_log("NCR reset\n");
}


static uint32_t
get_bus_host(ncr_t *ncr)
{
    uint32_t bus_host = 0;

    if (ncr->icr & ICR_DBP) {
	ncr_log("Data bus phase\n");		
	bus_host |= BUS_DBP;
    }
    if (ncr->icr & ICR_SEL) {
	ncr_log("Selection phase\n");
	bus_host |= BUS_SEL;
    }
    if (ncr->tcr & TCR_IO) {
	ncr_log("Data phase\n");
	bus_host |= BUS_IO;
    }
    if (ncr->tcr & TCR_CD) {
	ncr_log("Command phase\n");
	bus_host |= BUS_CD;
    }
    if (ncr->tcr & TCR_MSG) {
	ncr_log("Message phase\n");
	bus_host |= BUS_MSG;
    }
    if (ncr->tcr & TCR_REQ) {
	ncr_log("Request phase\n");
	bus_host |= BUS_REQ;
    }
    if (ncr->icr & ICR_BSY) {
	ncr_log("Busy phase\n");
	bus_host |= BUS_BSY;
    }
    if (ncr->icr & ICR_ATN) {
	ncr_log("ATN phase\n");
	bus_host |= BUS_ATN;
	}
    if (ncr->icr & ICR_ACK) {
	ncr_log("ACK phase\n");
	bus_host |= BUS_ACK;
    }
    if (ncr->mode & MODE_ARBITRATE)
		bus_host |= BUS_ARB;

    return(bus_host | BUS_SETDATA(ncr->output_data));
}

static void
ncr_dma_in(ncr5380_t *ncr_dev, ncr_t *ncr, scsi_device_t *dev, int bus)
{
	if (bus & BUS_ACK)
	{
		if (ncr->data_pos >= dev->buffer_length) {
			scsi_device_command_phase1(dev);
			ncr->cur_bus &= ~BUS_REQ;
			ncr->new_phase = SCSI_PHASE_STATUS;
			ncr->wait_data = 4;
			ncr->wait_complete = 8;
		} else {
			ncr->tx_data = dev->sc->temp_buffer[ncr->data_pos++];
			ncr->cur_bus = (ncr->cur_bus & ~BUS_DATAMASK) | BUS_SETDATA(ncr->tx_data) | BUS_DBP | BUS_REQ;
			ncr->clear_req = 3;
			ncr->cur_bus &= ~BUS_REQ;
			ncr->new_phase = SCSI_PHASE_DATA_IN;
		}
	}	
}

static void
ncr_dma_out(ncr5380_t *ncr_dev, ncr_t *ncr, scsi_device_t *dev, int bus)
{
	if (bus & BUS_ACK)
	{
		dev->sc->temp_buffer[ncr->data_pos++] = BUS_GETDATA(bus);

		if (ncr->data_pos >= dev->buffer_length) {
			scsi_device_command_phase1(dev);
			ncr->cur_bus &= ~BUS_REQ;
			ncr_log("CurBus ~REQ_DataOutDone=%02x\n", ncr->cur_bus);
			ncr->new_phase = SCSI_PHASE_STATUS;
			ncr->wait_data = 4;
			ncr->wait_complete = 8;
		} else {
			/*More data is to be transferred, place a request*/
			ncr->cur_bus |= BUS_REQ;
			ncr_log("CurBus ~REQ_DataOut=%02x\n", ncr->cur_bus);
		}
	}	
}

static void
ncr_dma_initiator_receive(ncr5380_t *ncr_dev, ncr_t *ncr, scsi_device_t *dev)
{
	int c = 0;
	uint8_t temp;

	ncr_log("Status for reading=%02x\n", ncr_dev->status_ctrl);
	
	if (!(ncr_dev->status_ctrl & CTRL_DATA_DIR)) {
		ncr_log("Read with DMA direction set wrong\n");
		return;
	}

	if (!(ncr_dev->status_ctrl & STATUS_BUFFER_NOT_READY)) return;

	if (!ncr_dev->block_count_loaded) return;

	while (c < 48) {
		/* Data ready. */
		ncr_wait_process(ncr_dev);
		temp = BUS_GETDATA(ncr->bus_host);
		ncr->bus_host = get_bus_host(ncr);

		ncr_dma_in(ncr_dev, ncr, dev, ncr->bus_host | BUS_ACK);
		ncr_dma_in(ncr_dev, ncr, dev, ncr->bus_host & ~BUS_ACK);

		ncr_dev->buffer[ncr_dev->buffer_pos++] = temp;
		
		ncr_log("Buffer pos for reading=%d\n", ncr_dev->buffer_pos);

		c++;

		if (ncr_dev->buffer_pos == 128) {					
			ncr_dev->buffer_pos = 0;
			ncr_dev->buffer_host_pos = 0;
			ncr_dev->status_ctrl &= ~STATUS_BUFFER_NOT_READY;
			ncr_dev->block_count = (ncr_dev->block_count - 1) & 255;
			ncr_log("Remaining blocks to be read=%d\n", ncr_dev->block_count);
			if (!ncr_dev->block_count) {
				ncr_dev->block_count_loaded = 0;
				ncr_log("IO End of read transfer\n");

				ncr->isr |= STATUS_END_OF_DMA;
				if (ncr->mode & MODE_ENA_EOP_INT) {
					ncr_log("NCR read irq\n");
					ncr->isr |= STATUS_INT;
					picint(1 << ncr_dev->irq);
				}
			}
			break;
		}
	}	
}

static void
ncr_dma_send(ncr5380_t *ncr_dev, ncr_t *ncr, scsi_device_t *dev)
{
	int c = 0;
	uint8_t data;

	ncr_log("Status for writing=%02x\n", ncr_dev->status_ctrl);
	
	if (ncr_dev->status_ctrl & CTRL_DATA_DIR) {
		ncr_log("Write with DMA direction set wrong\n");
		return;
	}

	if (!(ncr_dev->status_ctrl & STATUS_BUFFER_NOT_READY)) return;

	if (!ncr_dev->block_count_loaded) return;
	
	while (c < 48) {
		/* Data ready. */
		data = ncr_dev->buffer[ncr_dev->buffer_pos];
		ncr->bus_host = get_bus_host(ncr) & ~BUS_DATAMASK;
		ncr->bus_host |= BUS_SETDATA(data);

		ncr_dma_out(ncr_dev, ncr, dev, ncr->bus_host | BUS_ACK);
		ncr_dma_out(ncr_dev, ncr, dev, ncr->bus_host & ~BUS_ACK);

		ncr_log("Data Out buffer position = %d, buffer len = %d\n", ncr_dev->buffer_pos, dev->buffer_length);

		c++;

		if (++ncr_dev->buffer_pos == 128) {
			ncr_dev->buffer_pos = 0;
			ncr_dev->buffer_host_pos = 0;
			ncr_dev->status_ctrl &= ~STATUS_BUFFER_NOT_READY;
			ncr_dev->ncr_busy = 0;
			ncr_dev->block_count = (ncr_dev->block_count - 1) & 255;
			ncr_log("Remaining blocks to be written=%d\n", ncr_dev->block_count);
			if (!ncr_dev->block_count) {
				ncr_dev->block_count_loaded = 0;
				ncr_log("IO End of write transfer\n");
				
				ncr->tcr |= TCR_LAST_BYTE_SENT;
				ncr->isr |= STATUS_END_OF_DMA;
				if (ncr->mode & MODE_ENA_EOP_INT) {
					ncr_log("NCR write irq\n");
					ncr->isr |= STATUS_INT;
					picint(1 << ncr_dev->irq);
				}
			}
			break;
		}
	}		
}


static void
ncr_wait_process(ncr5380_t *ncr_dev)
{
    ncr_t *ncr = &ncr_dev->ncr;
    scsi_device_t *dev;

    /*Wait processes to handle bus requests*/
    ncr_log("Clear REQ=%d\n", ncr->clear_req);
    if (ncr->clear_req) {
	ncr->clear_req--;
	if (!ncr->clear_req) {
		ncr_log("Prelude to command data\n");
		SET_BUS_STATE(ncr, ncr->new_phase);
		ncr->cur_bus |= BUS_REQ;
	}
    }

    if (ncr->wait_data) {
	ncr->wait_data--;
	if (!ncr->wait_data) {
		dev = &scsi_devices[ncr->target_id];
		SET_BUS_STATE(ncr, ncr->new_phase);	

		if (ncr->new_phase == SCSI_PHASE_DATA_IN) {
			ncr_log("Data In bus phase, command = %02x\n", ncr->command[0]);
			ncr->tx_data = dev->sc->temp_buffer[ncr->data_pos++];
			ncr->cur_bus = (ncr->cur_bus & ~BUS_DATAMASK) | BUS_SETDATA(ncr->tx_data) | BUS_DBP;
			ncr->state = STATE_DATAIN;
			if (ncr->command[0] == 0x08 || ncr->command[0] == 0x28) {
				ncr_dev->dma_enabled = 1;
				ncr_dev->dma_is_write = 0;
			}
		} else if (ncr->new_phase == SCSI_PHASE_STATUS) {
			ncr_log("Status bus phase\n");
			ncr->cur_bus |= BUS_REQ;
			ncr->state = STATE_STATUS;
			ncr->cur_bus = (ncr->cur_bus & ~BUS_DATAMASK) | BUS_SETDATA(dev->status) | BUS_DBP;
		} else if (ncr->new_phase == SCSI_PHASE_MESSAGE_IN) {
			ncr_log("Message In bus phase\n");
			ncr->state = STATE_MESSAGEIN;
			ncr->cur_bus = (ncr->cur_bus & ~BUS_DATAMASK) | BUS_SETDATA(0) | BUS_DBP;
		} else {
			if (ncr->new_phase & BUS_IDLE) {
				ncr_log("Bus Idle phase\n");
				ncr->state = STATE_IDLE;
				ncr->cur_bus &= ~BUS_BSY;
			} else {
				ncr_log("Data Out bus phase, command = %02x, DMA Write = %d, DMA Enabled = %d\n", ncr->command[0], ncr_dev->dma_is_write, ncr_dev->dma_enabled);
				ncr->state = STATE_DATAOUT;
				if (ncr->command[0] == 0x0a || ncr->command[0] == 0x2a) {
					ncr_dev->dma_enabled = 1;
					ncr_dev->dma_is_write = 1;
				}
			}		
		}
	}
    }

    if (ncr->wait_complete) {
	ncr->wait_complete--;
	if (!ncr->wait_complete)
		ncr->cur_bus |= BUS_REQ;
    }

    ncr->bus_host = ncr->cur_bus;		
}


static void 
ncr_write(uint16_t port, uint8_t val, void *priv)
{
    ncr5380_t *ncr_dev = (ncr5380_t *)priv;
    ncr_t *ncr = &ncr_dev->ncr;

    ncr_log("NCR5380 write(%04x,%02x)\n",port & 7,val);

    switch (port & 7) {
	case 0:		/* Output data register */
		ncr_log("Write: Output data register, val = %d\n", val);
		ncr->output_data = val;
		break;

	case 1:		/* Initiator Command Register */
		ncr_log("Write: Initiator command register, val ACK = %02x\n", val & ICR_ACK);
		ncr->icr = val;
		ncr->icr_is_on = 1;
		break;

	case 2:		/* Mode register */
		ncr_log("Write: Mode register, val=%02x\n", val & MODE_DMA);
		if ((val & MODE_ARBITRATE) && !(ncr->mode & MODE_ARBITRATE)) {
			ncr->icr &= ~ICR_ARB_LOST;
			ncr->icr |=  ICR_ARB_IN_PROGRESS;
		}

		ncr->mode = val;

		/*If it's not DMA mode, don't do anything*/
		if (!(ncr->mode & MODE_DMA)) {
			ncr_log("No DMA mode\n");
			ncr->tcr &= ~TCR_LAST_BYTE_SENT;
			ncr->isr &= ~STATUS_END_OF_DMA;
			ncr_dev->dma_enabled = 0;
		}
		break;

	case 3:		/* Target Command Register */
		ncr_log("Write: Target Command register\n");
		ncr->tcr = val;
		break;

	case 4:		/* Select Enable Register */
		ncr_log("Write: Select Enable register\n");
		break;
		
	case 5:		/* start DMA Send */
		ncr_log("Write: start DMA send register\n");
		ncr_log("Write 6 or 10, block count loaded=%d\n", ncr_dev->block_count_loaded);
		/*a Write 6/10 has occurred, start the timer when the block count is loaded*/
		ncr_dev->dma_enabled = 1;
		ncr_dev->dma_is_write = 1;
		break;

	case 7:		/* start DMA Initiator Receive */
		ncr_log("Write: start DMA initiator receive register\n");
		ncr_log("Read 6 or 10, block count loaded=%d\n", ncr_dev->block_count_loaded);
		/*a Read 6/10 has occurred, start the timer when the block count is loaded*/
		ncr_dev->dma_enabled = 1;
		ncr_dev->dma_is_write = 0;
		break;

	default:
		ncr_log("NCR5380: bad write %04x %02x\n", port, val);
		break;
    }
}


static uint8_t 
ncr_read(uint16_t port, void *priv)
{
    ncr5380_t *ncr_dev = (ncr5380_t *)priv;
    ncr_t *ncr = &ncr_dev->ncr;
    uint8_t ret = 0xff;

    switch (port & 7) {
	case 0:		/* Current SCSI data */
		ncr_log("Read: Current SCSI data register\n");
		if (ncr->icr & ICR_DBP) {
			/*Return the data from the output register if on data bus phase from ICR*/
			ncr_log("Data Bus Phase\n");
			ret = ncr->output_data;
		} else {
			/*Return the data from the SCSI bus*/
			ncr_log("SCSI Bus Phase\n");
			ncr_wait_process(ncr_dev);
			ret = BUS_GETDATA(ncr->bus_host);
		}
		break;

	case 1:		/* Initiator Command Register */
		ncr_log("Read: Initiator Command register\n");
		ncr_log("NCR ICR Read=%02x\n", ncr->icr);

		ret = ncr->icr;
		break;

	case 2:		/* Mode register */
		ncr_log("Read: Mode register\n");
		ret = ncr->mode;
		break;

	case 3:		/* Target Command Register */
		ncr_log("Read: Target Command register\n");
		ncr_log("NCR target stat=%02x\n", ncr->tcr);
		ret = ncr->tcr;
		break;

	case 4:		/* Current SCSI Bus status */
		ncr_log("Read: SCSI bus status register\n");
		ret = 0;
		ncr_wait_process(ncr_dev);
		ncr_log("NCR cur bus stat=%02x\n", ncr->bus_host & 0xff);
		ret |= (ncr->bus_host & 0xff);
		break;

	case 5:		/* Bus and Status register */
		ncr_log("Read: Bus and Status register\n");
		ret = 0;		
		
		ncr->bus_host = get_bus_host(ncr);
		ncr_log("Get host from Interrupt\n");
		
		/*Check if the phase in process matches with TCR's*/
		if ((ncr->bus_host & SCSI_PHASE_MESSAGE_IN) ==
			(ncr->cur_bus & SCSI_PHASE_MESSAGE_IN))
		{
			ncr_log("Phase match\n");
			ret |= STATUS_PHASE_MATCH;
		}
		else
			picint(1 << ncr_dev->irq);
		
		ncr_wait_process(ncr_dev);

		if (ncr->bus_host & BUS_ACK) {
			ncr_log("Status ACK returned\n");
			ret |= STATUS_ACK;
		}
		
		if ((ncr->bus_host & BUS_REQ) && (ncr->mode & MODE_DMA)) {
			ncr_log("Entering DMA mode\n");
			ret |= STATUS_DRQ;
			
			int bus = 0;
			
			if (ncr->bus_host & BUS_IO)
				bus |= TCR_IO;
			if (ncr->bus_host & BUS_CD)
				bus |= TCR_CD;
			if (ncr->bus_host & BUS_MSG)
				bus |= TCR_MSG;
			if ((ncr->tcr & 7) != bus)
			{
				ncr->isr |= STATUS_INT;
			}
		}
		if (!(ncr->bus_host & BUS_BSY) && (ncr->mode & MODE_MONITOR_BUSY))
		{
			ncr_log("Busy error\n");
			ret |= STATUS_BUSY_ERROR;
		}
		ret |= (ncr->isr & (STATUS_INT | STATUS_END_OF_DMA));
		break;

	case 7:		/* reset Parity/Interrupt */
		ncr->isr &= ~STATUS_INT;
		picintc(1 << ncr_dev->irq);
		ncr_log("Reset IRQ\n");
		break;

	default:
		ncr_log("NCR5380: bad read %04x\n", port);
		break;
    }

    ncr_log("NCR5380 read(%04x)=%02x\n", port & 7, ret);

    return(ret);
}


/* Memory-mapped I/O READ handler. */
static uint8_t 
memio_read(uint32_t addr, void *priv)
{
    ncr5380_t *ncr_dev = (ncr5380_t *)priv;
    uint8_t ret = 0xff;
	
    addr &= 0x3fff;
#if ENABLE_NCR5380_LOG
    ncr_log("memio_read %08x\n", addr);
#endif

    if (addr < 0x2000)
	ret = ncr_dev->bios_rom.rom[addr & 0x1fff];
    else if (addr < 0x3800)
	ret = 0xff;
    else if (addr >= 0x3a00)
	ret = ncr_dev->ext_ram[addr - 0x3a00];
    else switch (addr & 0x3f80) {
	case 0x3800:
#if ENABLE_NCR5380_LOG
		ncr_log("Read intRAM %02x %02x\n", addr & 0x3f, ncr_dev->int_ram[addr & 0x3f]);
#endif
		ret = ncr_dev->int_ram[addr & 0x3f];
		break;

	case 0x3880:
#if ENABLE_NCR5380_LOG
		ncr_log("Read 53c80 %04x\n", addr);
#endif
		ret = ncr_read(addr, ncr_dev);
		break;
		
	case 0x3900:
		if (ncr_dev->buffer_host_pos >= 128 || !(ncr_dev->status_ctrl & CTRL_DATA_DIR)) {
			ret = 0xff;
		} else {
			ret = ncr_dev->buffer[ncr_dev->buffer_host_pos++];
			if (ncr_dev->buffer_host_pos == 128)
			{	
				ncr_dev->status_ctrl |= STATUS_BUFFER_NOT_READY;
			}
		}
		break;

	case 0x3980:
		switch (addr) {
			case 0x3980:	/* status */
				ret = ncr_dev->status_ctrl;// | 0x80;
				ncr_log("NCR status ctrl = %02x\n", ncr_dev->status_ctrl);
				if (!ncr_dev->ncr_busy)
				{
					ret |= STATUS_53C80_ACCESSIBLE;
				}
				break;

			case 0x3981:	/* block counter register*/
				ret = ncr_dev->block_count;
				break;

			case 0x3982:	/* switch register read */
				ret = 0xf8;
				ret |= ncr_dev->irq;
				break;

			case 0x3983:
				ret = 0xff;
				break;
		}
		break;		
    }

#if ENABLE_NCR5380_LOG
    if (addr >= 0x3880)
	ncr_log("memio_read(%08x)=%02x\n", addr, ret);
#endif

    return(ret);
}


/* Memory-mapped I/O WRITE handler. */
static void 
memio_write(uint32_t addr, uint8_t val, void *priv)
{
    ncr5380_t *ncr_dev = (ncr5380_t *)priv;
	
    addr &= 0x3fff;

    ncr_log("memio_write(%08x,%02x)  %i %02x\n", addr, val,  ncr_dev->buffer_host_pos, ncr_dev->status_ctrl);

    if (addr >= 0x3a00)
	ncr_dev->ext_ram[addr - 0x3a00] = val;
    else switch (addr & 0x3f80) {
	case 0x3800:
#if 0
		ncr_log("Write intram %02x %02x\n", addr & 0x3f, val);
#endif
		ncr_dev->int_ram[addr & 0x3f] = val;
		break;

	case 0x3880:
#if ENABLE_NCR5380_LOG
		ncr_log("Write 53c80 %04x %02x\n", addr, val);
#endif
		ncr_write(addr, val, ncr_dev);
		break;
		
	case 0x3900:
		if (!(ncr_dev->status_ctrl & CTRL_DATA_DIR) && ncr_dev->buffer_host_pos < 128) {
			ncr_dev->buffer[ncr_dev->buffer_host_pos++] = val;
			if (ncr_dev->buffer_host_pos == 128) 
			{
				ncr_log("Write Buffer Not Ready\n");
				ncr_dev->status_ctrl |= STATUS_BUFFER_NOT_READY;
				ncr_dev->ncr_busy = 1;
			}
		}
		break;

	case 0x3980:
		switch (addr) {
			case 0x3980:	/* Control */
				ncr_log("Write 0x3980: val=%02x\n", val);
				if (val & 0x80)
				{
					ncr_log("Resetting the 53c400\n");
					picint(1 << ncr_dev->irq);
				}
				if ((val & CTRL_DATA_DIR) && !(ncr_dev->status_ctrl & CTRL_DATA_DIR))
				{
					ncr_log("Read Data\n");
					ncr_dev->buffer_host_pos = 128;
					ncr_dev->status_ctrl |= STATUS_BUFFER_NOT_READY;					
				}
				else if (!(val & CTRL_DATA_DIR) && (ncr_dev->status_ctrl & CTRL_DATA_DIR))
				{
					ncr_log("Write Data\n");
					ncr_dev->buffer_host_pos = 0;
					ncr_dev->status_ctrl &= ~STATUS_BUFFER_NOT_READY;
				}
				ncr_dev->status_ctrl = (ncr_dev->status_ctrl & 0x87) | (val & 0x78);
				break;

			case 0x3981:	/* block counter register */
				ncr_log("Write 0x3981: val=%d\n", val);
				ncr_dev->block_count = val;
				ncr_dev->block_count_loaded = 1;

				if (ncr_dev->status_ctrl & CTRL_DATA_DIR) {
					ncr_log("Data Read\n");
					ncr_dev->buffer_host_pos = 128;
					ncr_dev->status_ctrl |= STATUS_BUFFER_NOT_READY;
				} else {
					ncr_log("Data Write\n");
					ncr_dev->buffer_host_pos = 0;
					ncr_dev->status_ctrl &= ~STATUS_BUFFER_NOT_READY;
				}
				break;
		}
		break;	
    }
}


/* Memory-mapped I/O READ handler for the Trantor T130B. */
static uint8_t 
t130b_read(uint32_t addr, void *priv)
{
    ncr5380_t *ncr_dev = (ncr5380_t *)priv;
    uint8_t ret = 0xff;

    addr &= 0x3fff;
    if (addr < 0x1800)
	ret = ncr_dev->bios_rom.rom[addr & 0x1fff];
      else
    if (addr < 0x1880)
	ret = ncr_dev->ext_ram[addr & 0x7f];

    return(ret);
}


/* Memory-mapped I/O WRITE handler for the Trantor T130B. */
static void 
t130b_write(uint32_t addr, uint8_t val, void *priv)
{
    ncr5380_t *ncr_dev = (ncr5380_t *)priv;

    addr &= 0x3fff;
    if (addr >= 0x1800 && addr < 0x1880)
		ncr_dev->ext_ram[addr & 0x7f] = val;
}


static uint8_t 
t130b_in(uint16_t port, void *priv)
{
    ncr5380_t *ncr_dev = (ncr5380_t *)priv;
    uint8_t ret = 0xff;

    switch (port & 0x0f) {
	case 0x00:
	case 0x01:
	case 0x02:
	case 0x03:
		ret = memio_read((port & 7) | 0x3980, ncr_dev);
		break;

	case 0x04:
	case 0x05:
		ret = memio_read(0x3900, ncr_dev);
		break;		
		
	case 0x08:
	case 0x09:
	case 0x0a:
	case 0x0b:
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x0f:
		ret = ncr_read(port, ncr_dev);
		break;
    }

    return(ret);
}


static void 
t130b_out(uint16_t port, uint8_t val, void *priv)
{
    ncr5380_t *ncr_dev = (ncr5380_t *)priv;

    switch (port & 0x0f) {
	case 0x00:
	case 0x01:
	case 0x02:
	case 0x03:
		memio_write((port & 7) | 0x3980, val, ncr_dev);
		break;

	case 0x04:
	case 0x05:
		memio_write(0x3900, val, ncr_dev);
		break;		
		
	case 0x08:
	case 0x09:
	case 0x0a:
	case 0x0b:
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x0f:
		ncr_write(port, val, ncr_dev);
		break;
    }
}


#if defined(DEV_BRANCH) && defined(USE_SUMO)
static uint8_t 
scsiat_in(uint16_t port, void *priv)
{
    ncr5380_t *ncr_dev = (ncr5380_t *)priv;
    uint8_t ret = 0xff;

    switch (port & 0x0f) {
	case 0x00:
	case 0x01:
	case 0x02:
	case 0x03:
	case 0x04:
	case 0x05:
	case 0x06:
	case 0x07:
		ret = ncr_read(port, ncr_dev);
		break;
    }

	ncr_log("SCSI AT read=0x%03x, ret=%02x\n", port, ret);
	
    return(ret);
}


static void 
scsiat_out(uint16_t port, uint8_t val, void *priv)
{
    ncr5380_t *ncr_dev = (ncr5380_t *)priv;
    ncr_t *ncr = &ncr_dev->ncr;
    scsi_device_t *dev = &scsi_devices[ncr->target_id];

    ncr_log("SCSI AT write=0x%03x, val=%02x\n", port, val);
    switch (port & 0x0f) {
	case 0x08:
		ncr->unk_08 = val;	
	
		if (ncr->unk_08 & 0x08)
		{			
			if (ncr->dma_mode == DMA_INITIATOR_RECEIVE)
			{
				while (ncr_dev->buffer_host_pos < 128)
				{
					uint8_t temp;
					
					temp = ncr_dev->buffer[ncr_dev->buffer_host_pos++];
					
					ncr_log("Read Buffer host=%d\n", ncr_dev->buffer_host_pos);
					
					ncr->bus_host = get_bus_host(ncr) & ~BUS_DATAMASK;
					ncr->bus_host |= BUS_SETDATA(temp);					
					
					if (ncr_dev->buffer_host_pos == 128)
						break;
				}
			}
			else if (ncr->dma_mode == DMA_SEND)
			{
				while (ncr_dev->buffer_host_pos < 128)
				{
					/* Data ready. */
					uint8_t temp;

					ncr_wait_process(ncr_dev);
					temp = BUS_GETDATA(ncr->bus_host);
					ncr->bus_host = get_bus_host(ncr);
					
					ncr_dev->buffer[ncr_dev->buffer_host_pos++] = temp;
					
					ncr_log("Write Buffer host=%d\n", ncr_dev->buffer_host_pos);

					if (ncr_dev->buffer_host_pos == 128)
					{
						
						break;
					}
				}			
			}
		}
		
		if (ncr->unk_08 & 0x01)
		{
			ncr_dev->block_count_loaded = 1;
			ncr_dev->block_count = dev->buffer_length / 128;
		}
		break;		
		
	case 0x00:
	case 0x01:
	case 0x02:
	case 0x03:
	case 0x04:
	case 0x05:
	case 0x06:
	case 0x07:
		ncr_write(port, val, ncr_dev);
		break;
    }
}
#endif


static void
ncr_callback(void *priv)
{
    ncr5380_t *ncr_dev = (ncr5380_t *)priv;
    ncr_t *ncr = &ncr_dev->ncr;
    scsi_device_t *dev = &scsi_devices[ncr->target_id];
    int req_len;
    double p;

    if ((ncr_dev->dma_enabled != 0) && (dev->type == SCSI_REMOVABLE_CDROM)) {
		timer_on(&ncr_dev->timer, ncr_dev->period, 0);
	}
	else {
		timer_on(&ncr_dev->timer, 40.0, 0);
	}

    if (ncr_dev->dma_enabled == 0) {
	ncr->bus_host = get_bus_host(ncr);

	/*Start the SCSI command layer, which will also make the timings*/
	if (ncr->bus_host & BUS_ARB) {
		ncr_log("Arbitration\n");
		ncr->state = STATE_IDLE;
	}

	if (ncr->state == STATE_IDLE) {
		ncr->clear_req = ncr->wait_data = ncr->wait_complete = 0;
		if ((ncr->bus_host & BUS_SEL) && !(ncr->bus_host & BUS_BSY)) {
			ncr_log("Selection phase\n");
			uint8_t sel_data = BUS_GETDATA(ncr->bus_host);

			ncr->target_id = get_dev_id(sel_data);

			ncr_log("Select - target ID = %i\n", ncr->target_id);

			/*Once the device has been found and selected, mark it as busy*/
			if ((ncr->target_id != (uint8_t)-1) && scsi_device_present(&scsi_devices[ncr->target_id])) {
				ncr->cur_bus |= BUS_BSY;
				ncr_log("Device found at ID %i\n", ncr->target_id);
				ncr_log("Current Bus BSY=%02x\n", ncr->cur_bus);
				ncr->state = STATE_COMMAND;
				ncr->cur_bus = BUS_BSY | BUS_REQ;
				ncr_log("CurBus BSY|REQ=%02x\n", ncr->cur_bus);
				ncr->command_pos = 0;
				SET_BUS_STATE(ncr, SCSI_PHASE_COMMAND);
				picint(1 << ncr_dev->irq);
			} else {
				ncr->state = STATE_IDLE;
				ncr->cur_bus = 0;
			}
		}
	} else if (ncr->state == STATE_COMMAND) {
		/*Command phase, make sure the ICR ACK bit is set to keep on, 
		because the device must be acknowledged by ICR*/
		ncr_log("Command pos=%i, output data=%02x, out data=%02x\n", ncr->command_pos, BUS_GETDATA(ncr->bus_host), ncr->output_data);
		if ((ncr->bus_host & BUS_ACK) && (ncr->command_pos < cmd_len[(ncr->command[0] >> 5) & 7])) {
			/*Write command byte to the output data register*/
			if (ncr->icr_is_on)
			{
				ncr->icr_is_on = 0;
				ncr->command[ncr->command_pos++] = BUS_GETDATA(ncr->bus_host);

				ncr->new_phase = ncr->cur_bus & SCSI_PHASE_MESSAGE_IN;
				ncr->clear_req = 3;
				ncr_log("Current bus for command request=%02x\n", ncr->cur_bus & BUS_REQ);				
				ncr->cur_bus &= ~BUS_REQ;
			}

			if (ncr->command_pos == cmd_len[(ncr->command[0] >> 5) & 7]) {
				/*Reset data position to default*/
				ncr->data_pos = 0;

				ncr_log("SCSI Command 0x%02X for ID %d, status code=%02x\n", ncr->command[0], ncr->target_id, dev->status);

				dev->buffer_length = -1;

				/*Now, execute the given SCSI command*/
				scsi_device_command_phase0(dev, ncr->command);

				ncr_log("SCSI ID %i: Command %02X: Buffer Length %i, SCSI Phase %02X, Output Data = %d\n", ncr->target_id, ncr->command[0], dev->buffer_length, dev->phase, ncr->output_data);

				if (dev->status != SCSI_STATUS_OK) {
					ncr->new_phase = SCSI_PHASE_STATUS;
					ncr->wait_data = 4;
					return;
				}

				/*If the SCSI phase is Data In or Data Out, allocate the SCSI buffer based on the transfer length of the command*/
				if (dev->buffer_length && (dev->phase == SCSI_PHASE_DATA_IN || dev->phase == SCSI_PHASE_DATA_OUT) && (dev->type == SCSI_REMOVABLE_CDROM)) {
					p = scsi_device_get_callback(dev);
					req_len = MIN(48, dev->buffer_length);
					ncr_log("Request len = %d, buffer len = %d, cmd = 0x%02x\n", req_len, dev->buffer_length, ncr->command[0]);
					if (p <= 0.0)
						ncr_dev->period = 0.2 * ((double) req_len);
					else
						ncr_dev->period = (p / ((double) dev->buffer_length)) * ((double) req_len);
				}

				if (dev->phase == SCSI_PHASE_DATA_OUT) {
					/* Write direction commands have delayed execution - only execute them after the bus has gotten all the data from the host. */
					ncr_log("Next state is data out\n");
					ncr->new_phase = SCSI_PHASE_DATA_OUT;
					ncr->wait_data = 4;
					ncr->clear_req = 4;
				} else {
					/* Other command - execute immediately. */
					ncr->new_phase = dev->phase;
					ncr->wait_data = 4;
				}
			}
		}
	} else if (ncr->state == STATE_DATAIN && (ncr->command[0] != 0x08 && ncr->command[0] != 0x28)) {
		ncr_log("Data In ACK=%02x\n", ncr->bus_host);	
		if (ncr->bus_host & BUS_ACK) {
			if (ncr->data_pos >= dev->buffer_length) {
				scsi_device_command_phase1(dev);
				ncr->cur_bus &= ~BUS_REQ;
				ncr->new_phase = SCSI_PHASE_STATUS;
				ncr->wait_data = 4;
				ncr->wait_complete = 8;
			} else {
				ncr->tx_data = dev->sc->temp_buffer[ncr->data_pos++];
				ncr->cur_bus = (ncr->cur_bus & ~BUS_DATAMASK) | BUS_SETDATA(ncr->tx_data) | BUS_DBP | BUS_REQ;
				ncr->clear_req = 3;
				ncr->cur_bus &= ~BUS_REQ;
				ncr->new_phase = SCSI_PHASE_DATA_IN;
			}
		}
	} else if (ncr->state == STATE_DATAOUT && (ncr->command[0] != 0x0a && ncr->command[0] != 0x2a)) {
		ncr_log("Data Out ACK=%02x, sector len = %d\n", ncr->bus_host, dev->sc->sector_len);
		if (ncr->bus_host & BUS_ACK) {
			dev->sc->temp_buffer[ncr->data_pos++] = BUS_GETDATA(ncr->bus_host);

			if (ncr->data_pos >= dev->buffer_length) {
				scsi_device_command_phase1(dev);
				ncr->cur_bus &= ~BUS_REQ;
				ncr_log("CurBus ~REQ_DataOutDone=%02x\n", ncr->cur_bus);
				ncr->new_phase = SCSI_PHASE_STATUS;
				ncr->wait_data = 4;
				ncr->wait_complete = 8;
			} else {
				/*More data is to be transferred, place a request*/
				ncr->cur_bus |= BUS_REQ;
				ncr_log("CurBus ~REQ_DataOut=%02x\n", ncr->cur_bus);
			}
		}
	} else if (ncr->state == STATE_STATUS) {
		if (ncr->bus_host & BUS_ACK) {
			/*All transfers done, wait until next transfer*/
			ncr->cur_bus &= ~BUS_REQ;
			ncr->new_phase = SCSI_PHASE_MESSAGE_IN;
			ncr->wait_data = 4;
			ncr->wait_complete = 8;
		}
	} else if (ncr->state == STATE_MESSAGEIN) {
		if (ncr->bus_host & BUS_ACK) {
			ncr->cur_bus &= ~BUS_REQ;
			ncr->new_phase = BUS_IDLE;
			ncr->wait_data = 4;
		}
	}
    }

    if (ncr_dev->type < 3) {
		if (ncr_dev->dma_enabled) {
			if (ncr_dev->dma_is_write == 1) {
				ncr_dma_send(ncr_dev, ncr, dev);
			} else {
				ncr_dma_initiator_receive(ncr_dev, ncr, dev);
			}
		}
    }

    ncr_wait_process(ncr_dev);
    if (!(ncr->bus_host & BUS_BSY) && (ncr->mode & MODE_MONITOR_BUSY)) {
	ncr_log("Updating DMA\n");
	ncr->mode &= ~MODE_DMA;
	ncr_dev->dma_enabled = 0;
    }
}

static void *
ncr_init(const device_t *info)
{
    char temp[128];
    ncr5380_t *ncr_dev;

    ncr_dev = malloc(sizeof(ncr5380_t));
    memset(ncr_dev, 0x00, sizeof(ncr5380_t));
    ncr_dev->name = info->name;
    ncr_dev->type = info->local;

    switch(ncr_dev->type) {
	case 0:		/* Longshine LCS6821N */
		ncr_dev->rom_addr = device_get_config_hex20("bios_addr");
		rom_init(&ncr_dev->bios_rom, LCS6821N_ROM,
			 ncr_dev->rom_addr, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);
			 
		mem_mapping_disable(&ncr_dev->bios_rom.mapping);

		mem_mapping_add(&ncr_dev->mapping, ncr_dev->rom_addr, 0x4000, 
				memio_read, NULL, NULL,
				memio_write, NULL, NULL,
				ncr_dev->bios_rom.rom, 0, ncr_dev);
		break;

	case 1:		/* Rancho RT1000B */
		ncr_dev->rom_addr = device_get_config_hex20("bios_addr");
		rom_init(&ncr_dev->bios_rom, RT1000B_ROM,
			 ncr_dev->rom_addr, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

		mem_mapping_disable(&ncr_dev->bios_rom.mapping);

		mem_mapping_add(&ncr_dev->mapping, ncr_dev->rom_addr, 0x4000, 
				memio_read, NULL, NULL,
				memio_write, NULL, NULL,
				ncr_dev->bios_rom.rom, 0, ncr_dev);
		break;

	case 2:		/* Trantor T130B */
		ncr_dev->rom_addr = device_get_config_hex20("bios_addr");
		ncr_dev->base = device_get_config_hex16("base");
		ncr_dev->irq = device_get_config_int("irq");
		rom_init(&ncr_dev->bios_rom, T130B_ROM,
			 ncr_dev->rom_addr, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

		mem_mapping_disable(&ncr_dev->bios_rom.mapping);	 
			 
		mem_mapping_add(&ncr_dev->mapping, ncr_dev->rom_addr, 0x4000, 
				t130b_read, NULL, NULL,
				t130b_write, NULL, NULL,
				ncr_dev->bios_rom.rom, 0, ncr_dev);

		io_sethandler(ncr_dev->base, 16,
			      t130b_in,NULL,NULL, t130b_out,NULL,NULL, ncr_dev);
		break;

#if defined(DEV_BRANCH) && defined(USE_SUMO)
	case 3:		/* Sumo SCSI-AT */
		ncr_dev->base = device_get_config_hex16("base");
		ncr_dev->irq = device_get_config_int("irq");
		ncr_dev->rom_addr = device_get_config_hex20("bios_addr");
		rom_init(&ncr_dev->bios_rom, SCSIAT_ROM,
			 ncr_dev->rom_addr, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

		mem_mapping_disable(&ncr_dev->bios_rom.mapping);	 
			 
		mem_mapping_add(&ncr_dev->mapping, ncr_dev->rom_addr, 0x4000, 
				t130b_read, NULL, NULL,
				t130b_write, NULL, NULL,
				ncr_dev->bios_rom.rom, 0, ncr_dev);

		io_sethandler(ncr_dev->base, 16,
			      scsiat_in,NULL,NULL, scsiat_out,NULL,NULL, ncr_dev);
		break;
#endif
    }

    sprintf(temp, "%s: BIOS=%05X", ncr_dev->name, ncr_dev->rom_addr);
    if (ncr_dev->base != 0)
	sprintf(&temp[strlen(temp)], " I/O=%04x", ncr_dev->base);
    if (ncr_dev->irq != 0)
	sprintf(&temp[strlen(temp)], " IRQ=%d", ncr_dev->irq);
    ncr_log("%s\n", temp);

    ncr_reset(ncr_dev);
    ncr_dev->status_ctrl = STATUS_BUFFER_NOT_READY;
	ncr_dev->buffer_host_pos = 128;

    timer_add(&ncr_dev->timer, ncr_callback, ncr_dev, 1);
	ncr_dev->timer.period = 10.0;
	timer_set_delay_u64(&ncr_dev->timer, (uint64_t) (ncr_dev->timer.period * ((double) TIMER_USEC)));

    return(ncr_dev);
}


static void 
ncr_close(void *priv)
{
    ncr5380_t *ncr_dev = (ncr5380_t *)priv;

    if (ncr_dev) {
	/* Tell the timer to terminate. */
	timer_stop(&ncr_dev->timer);

	free(ncr_dev);
	ncr_dev = NULL;
    }
}


static int
lcs6821n_available(void)
{
    return(rom_present(LCS6821N_ROM));
}


static int
rt1000b_available(void)
{
    return(rom_present(RT1000B_ROM));
}


static int
t130b_available(void)
{
    return(rom_present(T130B_ROM));
}


#if defined(DEV_BRANCH) && defined(USE_SUMO)
static int
scsiat_available(void)
{
    return(rom_present(SCSIAT_ROM));
}
#endif


static const device_config_t ncr5380_mmio_config[] = {
        {
                "bios_addr", "BIOS Address", CONFIG_HEX20, "", 0xD8000,
                {
                        {
                                "Disabled", 0
                        },
                        {
                                "C800H", 0xc8000
                        },
                        {
                                "CC00H", 0xcc000
                        },
                        {
                                "D800H", 0xd8000
                        },
                        {
                                "DC00H", 0xdc000
                        },
                        {
                                ""
                        }
                },
        },
	{
		"", "", -1
	}
};

static const device_config_t t130b_config[] = {
        {
                "bios_addr", "BIOS Address", CONFIG_HEX20, "", 0xD8000,
                {
                        {
                                "Disabled", 0
                        },
                        {
                                "C800H", 0xc8000
                        },
                        {
                                "CC00H", 0xcc000
                        },
                        {
                                "D800H", 0xd8000
                        },
                        {
                                "DC00H", 0xdc000
                        },
                        {
                                ""
                        }
                },
        },
        {
		"base", "Address", CONFIG_HEX16, "", 0x0350,
                {
                        {
                                "240H", 0x0240
                        },
                        {
                                "250H", 0x0250
                        },
                        {
                                "340H", 0x0340
                        },
                        {
                                "350H", 0x0350
                        },
                        {
                                ""
                        }
                },
        },
        {
		"irq", "IRQ", CONFIG_SELECTION, "", 5,
                {
                        {
                                "IRQ 3", 3
                        },
                        {
                                "IRQ 5", 5
                        },
                        {
                                "IRQ 7", 7
                        },
                        {
                                ""
                        }
                },
        },
	{
		"", "", -1
	}
};

#if defined(DEV_BRANCH) && defined(USE_SUMO)
static const device_config_t scsiat_config[] = {
        {
		"base", "Address", CONFIG_HEX16, "", 0x0310,
                {
                        {
                                "None",      0
                        },
                        {
                                "300H", 0x0300
                        },
                        {
                                "310H", 0x0310
                        },
                        {
                                "320H", 0x0320
                        },
                        {
                                "330H", 0x0330
                        },
                        {
                                "340H", 0x0340
                        },
                        {
                                "350H", 0x0350
                        },
                        {
                                "360H", 0x0360
                        },
                        {
                                "370H", 0x0370
                        },
                        {
                                ""
                        }
                },
        },
        {
		"irq", "IRQ", CONFIG_SELECTION, "", 5,
                {
                        {
                                "IRQ 3", 3
                        },
                        {
                                "IRQ 4", 4
                        },
                        {
                                "IRQ 5", 5
                        },
                        {
                                "IRQ 10", 10
                        },
                        {
                                "IRQ 11", 11
                        },
                        {
                                "IRQ 12", 12
                        },
                        {
                                "IRQ 14", 14
                        },
                        {
                                "IRQ 15", 15
                        },
                        {
                                ""
                        }
                },
        },
        {
                "bios_addr", "BIOS Address", CONFIG_HEX20, "", 0xD8000,
                {
                        {
                                "Disabled", 0
                        },
                        {
                                "C800H", 0xc8000
                        },
                        {
                                "CC00H", 0xcc000
                        },
                        {
                                "D800H", 0xd8000
                        },
                        {
                                "DC00H", 0xdc000
                        },
                        {
                                ""
                        }
                },
        },
	{
		"", "", -1
	}
};
#endif


const device_t scsi_lcs6821n_device =
{
    "Longshine LCS-6821N",
    DEVICE_ISA,
    0,
    ncr_init, ncr_close, NULL,
    lcs6821n_available,
    NULL, NULL,
    ncr5380_mmio_config
};

const device_t scsi_rt1000b_device =
{
    "Rancho RT1000B",
    DEVICE_ISA,
    1,
    ncr_init, ncr_close, NULL,
    rt1000b_available,
    NULL, NULL,
    ncr5380_mmio_config
};

const device_t scsi_t130b_device =
{
    "Trantor T130B",
    DEVICE_ISA,
    2,
    ncr_init, ncr_close, NULL,
    t130b_available,
    NULL, NULL,
    t130b_config
};

#if defined(DEV_BRANCH) && defined(USE_SUMO)
const device_t scsi_scsiat_device =
{
    "Sumo SCSI-AT",
    DEVICE_ISA,
    3,
    ncr_init, ncr_close, NULL,
    scsiat_available,
    NULL, NULL,
    scsiat_config
};
#endif

