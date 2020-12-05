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
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017-2019 Sarah Walker.
 *		Copyright 2017-2019 TheCollector1995.
 *		Copyright 2017-2019 Fred N. van Kempen.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/mca.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/scsi_ncr5380.h>


#define LCS6821N_ROM	L"roms/scsi/ncr5380/Longshine LCS-6821N - BIOS version 1.04.bin"
#define RT1000B_810R_ROM	L"roms/scsi/ncr5380/Rancho_RT1000_RTBios_version_8.10R.bin"
#define RT1000B_820R_ROM	L"roms/scsi/ncr5380/RTBIOS82.rom"
#define T130B_ROM	L"roms/scsi/ncr5380/trantor_t130b_bios_v2.14.bin"


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
    uint8_t	icr, mode, tcr, data_wait;
    uint8_t	isr, output_data, target_id, tx_data;
	uint8_t msglun;

    uint8_t	command[20];
	uint8_t msgout[4];
	int msgout_pos;
	int is_msgout;

    int		dma_mode, cur_bus, bus_in, new_phase;
    int 	state, clear_req, wait_data, wait_complete;
    int		command_pos, data_pos;
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
	int8_t  bios_ver;
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
} ncr5380_t;

#define STATE_IDLE	0
#define STATE_COMMAND	1
#define STATE_DATAIN	2
#define STATE_DATAOUT	3
#define STATE_STATUS	4
#define STATE_MESSAGEIN	5
#define STATE_SELECT	6
#define STATE_MESSAGEOUT 7
#define STATE_MESSAGE_ID 8

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

static int 
getmsglen(uint8_t *msgp, int len)
{
	uint8_t msg = msgp[0];
	if (msg == 0 || (msg >= 0x02 && msg <= 0x1f) ||msg >= 0x80)
		return 1;
	if (msg >= 0x20 && msg <= 0x2f)
		return 2;
	if (len < 2)
		return 3;
	return msgp[1];
}

static void
ncr_reset(ncr_t *ncr)
{
    memset(ncr, 0x00, sizeof(ncr_t));
    ncr_log("NCR reset\n");
}


static void
dma_timer_on(ncr5380_t *ncr_dev)
{
    ncr_t *ncr = &ncr_dev->ncr;
    double period = ncr_dev->period;

    /* DMA Timer on: 1 wait period + 64 byte periods + 64 byte periods if first time. */
    if (ncr->data_wait & 2) {
	ncr->data_wait &= ~2;
	period *= 128.0;
    } else
	period *= 64.0;

    /* This is the 1 us wait period. */
    period += 1.0;

    timer_on_auto(&ncr_dev->timer, period);
}


static void
wait_timer_on(ncr5380_t *ncr_dev)
{
    /* PIO Wait Timer On: 1 period. */
    timer_on_auto(&ncr_dev->timer, ncr_dev->period);
}


static void
set_dma_enable(ncr5380_t *dev, int enable)
{
    if (enable) {
	if (!timer_is_enabled(&dev->timer))
		dma_timer_on(dev);
    } else
	timer_stop(&dev->timer);
}


static void
dma_changed(ncr5380_t *dev, int mode, int enable)
{
    dev->dma_enabled = (mode && enable);

    set_dma_enable(dev, dev->dma_enabled && dev->block_count_loaded);
}


static uint32_t
get_bus_host(ncr_t *ncr)
{
    uint32_t bus_host = 0;

    if (ncr->icr & ICR_DBP)
	bus_host |= BUS_DBP;

    if (ncr->icr & ICR_SEL)
	bus_host |= BUS_SEL;

    if (ncr->tcr & TCR_IO)
	bus_host |= BUS_IO;

    if (ncr->tcr & TCR_CD)
	bus_host |= BUS_CD;

    if (ncr->tcr & TCR_MSG)
	bus_host |= BUS_MSG;

    if (ncr->tcr & TCR_REQ)
	bus_host |= BUS_REQ;

    if (ncr->icr & ICR_BSY)
	bus_host |= BUS_BSY;

    if (ncr->icr & ICR_ATN)
	bus_host |= BUS_ATN;

    if (ncr->icr & ICR_ACK)
	bus_host |= BUS_ACK;

    if (ncr->mode & MODE_ARBITRATE)
	bus_host |= BUS_ARB;

    return(bus_host | BUS_SETDATA(ncr->output_data));
}


static void
ncr_bus_read(ncr5380_t *ncr_dev)
{
    ncr_t *ncr = &ncr_dev->ncr;
    scsi_device_t *dev;
    int phase;

    /*Wait processes to handle bus requests*/
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
		phase = (ncr->cur_bus & SCSI_PHASE_MESSAGE_IN);

		if (phase == SCSI_PHASE_DATA_IN) {
			ncr->tx_data = dev->sc->temp_buffer[ncr->data_pos++];
			ncr->state = STATE_DATAIN;
			ncr->cur_bus = (ncr->cur_bus & ~BUS_DATAMASK) | BUS_SETDATA(ncr->tx_data) | BUS_DBP;
		} else if (phase == SCSI_PHASE_DATA_OUT) {
			if (ncr->new_phase & BUS_IDLE) {
				ncr->state = STATE_IDLE;
				ncr->cur_bus &= ~BUS_BSY;
			} else
				ncr->state = STATE_DATAOUT;
		} else if (phase == SCSI_PHASE_STATUS) {
			ncr->cur_bus |= BUS_REQ;
			ncr->state = STATE_STATUS;
			ncr->cur_bus = (ncr->cur_bus & ~BUS_DATAMASK) | BUS_SETDATA(dev->status) | BUS_DBP;
		} else if (phase == SCSI_PHASE_MESSAGE_IN) {
			ncr->state = STATE_MESSAGEIN;
			ncr->cur_bus = (ncr->cur_bus & ~BUS_DATAMASK) | BUS_SETDATA(0) | BUS_DBP;
		} else if (phase == SCSI_PHASE_MESSAGE_OUT) {
			ncr->cur_bus |= BUS_REQ;
			ncr->state = STATE_MESSAGEOUT;
			ncr->cur_bus = (ncr->cur_bus & ~BUS_DATAMASK) | BUS_SETDATA(ncr->target_id >> 5) | BUS_DBP;
		}
	}
    }

    if (ncr->wait_complete) {
	ncr->wait_complete--;
	if (!ncr->wait_complete)
		ncr->cur_bus |= BUS_REQ;
    }
}


static void
ncr_bus_update(void *priv, int bus)
{
    ncr5380_t *ncr_dev = (ncr5380_t *)priv;
    ncr_t *ncr = &ncr_dev->ncr;
    scsi_device_t *dev = &scsi_devices[ncr->target_id];
    double p;
    uint8_t sel_data;
    int msglen;

    /*Start the SCSI command layer, which will also make the timings*/
    if (bus & BUS_ARB)
	ncr->state = STATE_IDLE;

    switch (ncr->state) {
	case STATE_IDLE:
		ncr->clear_req = ncr->wait_data = ncr->wait_complete = 0;
		if ((bus & BUS_SEL) && !(bus & BUS_BSY)) {
			ncr_log("Selection phase\n");
			sel_data = BUS_GETDATA(bus);

			ncr->target_id = get_dev_id(sel_data);

			ncr_log("Select - target ID = %i\n", ncr->target_id);

			/*Once the device has been found and selected, mark it as busy*/
			if ((ncr->target_id != (uint8_t)-1) && scsi_device_present(&scsi_devices[ncr->target_id])) {
				ncr->cur_bus |= BUS_BSY;
				ncr->state = STATE_SELECT;
			} else {
				ncr_log("Device not found at ID %i, Current Bus BSY=%02x\n", ncr->target_id, ncr->cur_bus);
				ncr->cur_bus = 0;
			}
		}
		break;
	case STATE_SELECT:
		if (!(bus & BUS_SEL)) {
			if (!(bus & BUS_ATN)) {
				if ((ncr->target_id != (uint8_t)-1) && scsi_device_present(&scsi_devices[ncr->target_id])) {
					ncr_log("Device found at ID %i, Current Bus BSY=%02x\n", ncr->target_id, ncr->cur_bus);
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
			} else {
				ncr_log("Set to SCSI Message Out\n");
				ncr->new_phase = SCSI_PHASE_MESSAGE_OUT;
				ncr->wait_data = 4;
				ncr->msgout_pos = 0;
				ncr->is_msgout = 1;
			}
		}
		break;
	case STATE_COMMAND:
		if ((bus & BUS_ACK) && !(ncr->bus_in & BUS_ACK)) {
			/*Write command byte to the output data register*/
			ncr->command[ncr->command_pos++] = BUS_GETDATA(bus);
			ncr->clear_req = 3;
			ncr->new_phase = ncr->cur_bus & SCSI_PHASE_MESSAGE_IN;
			ncr->cur_bus &= ~BUS_REQ;

			ncr_log("Command pos=%i, output data=%02x\n", ncr->command_pos, BUS_GETDATA(bus));

			if (ncr->command_pos == cmd_len[(ncr->command[0] >> 5) & 7]) {
				if (ncr->is_msgout) {
					ncr->is_msgout = 0;
					ncr->command[1] &= ~(0x80 | 0x40 | 0x20);
					ncr->command[1] |= ncr->msglun << 5;
				}

				/*Reset data position to default*/
				ncr->data_pos = 0;

				dev = &scsi_devices[ncr->target_id];

				ncr_log("SCSI Command 0x%02X for ID %d, status code=%02x\n", ncr->command[0], ncr->target_id, dev->status);
				dev->buffer_length = -1;
				scsi_device_command_phase0(dev, ncr->command);
				ncr_log("SCSI ID %i: Command %02X: Buffer Length %i, SCSI Phase %02X\n", ncr->target_id, ncr->command[0], dev->buffer_length, dev->phase);

				ncr_dev->period = 1.0;	/* 1 us default */
				ncr->wait_data = 4;
				ncr->data_wait = 0;

				if (dev->status == SCSI_STATUS_OK) {
					/*If the SCSI phase is Data In or Data Out, allocate the SCSI buffer based on the transfer length of the command*/
					if (dev->buffer_length && (dev->phase == SCSI_PHASE_DATA_IN || dev->phase == SCSI_PHASE_DATA_OUT)) {
						p = scsi_device_get_callback(dev);
						if (p <= 0.0)
							ncr_dev->period = 0.2/* * ((double) dev->buffer_length) */;
						else
							ncr_dev->period = p / ((double) dev->buffer_length);
						ncr->data_wait |= 2;
					}
				}

				ncr->new_phase = dev->phase;
			}
		}
		break;
	case STATE_DATAIN:
		dev = &scsi_devices[ncr->target_id];
		if ((bus & BUS_ACK) && !(ncr->bus_in & BUS_ACK)) {
			if (ncr->data_pos >= dev->buffer_length) {
				ncr->cur_bus &= ~BUS_REQ;
				scsi_device_command_phase1(dev);
				ncr->new_phase = SCSI_PHASE_STATUS;
				ncr->wait_data = 4;
				ncr->wait_complete = 8;
			} else {
				ncr->tx_data = dev->sc->temp_buffer[ncr->data_pos++];
				ncr->cur_bus = (ncr->cur_bus & ~BUS_DATAMASK) | BUS_SETDATA(ncr->tx_data) | BUS_DBP | BUS_REQ;
				if (ncr->data_wait & 2)
					ncr->data_wait &= ~2;
				if (ncr->dma_mode == DMA_IDLE) {
					ncr->data_wait |= 1;
					wait_timer_on(ncr_dev);
				} else
					ncr->clear_req = 3;
				ncr->cur_bus &= ~BUS_REQ;
				ncr->new_phase = SCSI_PHASE_DATA_IN;
			}
		}
		break;
	case STATE_DATAOUT:
		dev = &scsi_devices[ncr->target_id];

		if ((bus & BUS_ACK) && !(ncr->bus_in & BUS_ACK)) {
			dev->sc->temp_buffer[ncr->data_pos++] = BUS_GETDATA(bus);

			if (ncr->data_pos >= dev->buffer_length) {
				ncr->cur_bus &= ~BUS_REQ;
				scsi_device_command_phase1(dev);
				ncr->new_phase = SCSI_PHASE_STATUS;
				ncr->wait_data = 4;
				ncr->wait_complete = 8;
			} else {
				/*More data is to be transferred, place a request*/
				if (ncr->dma_mode == DMA_IDLE) {
					ncr->data_wait |= 1;
					wait_timer_on(ncr_dev);
				} else
					ncr->clear_req = 3;
				ncr->cur_bus &= ~BUS_REQ;
				ncr_log("CurBus ~REQ_DataOut=%02x\n", ncr->cur_bus);
			}
		}
		break;
	case STATE_STATUS:
		if ((bus & BUS_ACK) && !(ncr->bus_in & BUS_ACK)) {
			/*All transfers done, wait until next transfer*/
			ncr->cur_bus &= ~BUS_REQ;
			ncr->new_phase = SCSI_PHASE_MESSAGE_IN;
			ncr->wait_data = 4;
			ncr->wait_complete = 8;	
		}
		break;
	case STATE_MESSAGEIN:
		if ((bus & BUS_ACK) && !(ncr->bus_in & BUS_ACK)) {
			ncr->cur_bus &= ~BUS_REQ;
			ncr->new_phase = BUS_IDLE;
			ncr->wait_data = 4;
		}
		break;
	case STATE_MESSAGEOUT:
		ncr_log("Ack on MSGOUT = %02x\n", (bus & BUS_ACK));
		if ((bus & BUS_ACK) && !(ncr->bus_in & BUS_ACK)) {
			ncr->msgout[ncr->msgout_pos++] = BUS_GETDATA(bus);
			msglen = getmsglen(ncr->msgout, ncr->msgout_pos);
			if (ncr->msgout_pos >= msglen) {
				if ((ncr->msgout[0] & (0x80 | 0x20)) == 0x80)
				ncr->msglun = ncr->msgout[0] & 7;			
				ncr->cur_bus &= ~BUS_REQ;
				ncr->state = STATE_MESSAGE_ID;
			}
		}
		break;
	case STATE_MESSAGE_ID:
		if ((ncr->target_id != (uint8_t)-1) && scsi_device_present(&scsi_devices[ncr->target_id])) {
			ncr_log("Device found at ID %i on MSGOUT, Current Bus BSY=%02x\n", ncr->target_id, ncr->cur_bus);
			ncr->state = STATE_COMMAND;
			ncr->cur_bus = BUS_BSY | BUS_REQ;
			ncr_log("CurBus BSY|REQ=%02x\n", ncr->cur_bus);
			ncr->command_pos = 0;
			SET_BUS_STATE(ncr, SCSI_PHASE_COMMAND);
		}
		break;
    }

    ncr->bus_in = bus;
}


static void 
ncr_write(uint16_t port, uint8_t val, void *priv)
{
    ncr5380_t *ncr_dev = (ncr5380_t *)priv;
    ncr_t *ncr = &ncr_dev->ncr;
    int bus_host = 0;

    ncr_log("NCR5380 write(%04x,%02x)\n",port & 7,val);

    switch (port & 7) {
	case 0:		/* Output data register */
		ncr_log("Write: Output data register\n");
		ncr->output_data = val;
		break;

	case 1:		/* Initiator Command Register */
		ncr_log("Write: Initiator command register\n");
		if ((val & 0x80) && !(ncr->icr & 0x80)) {
			ncr_log("Resetting the 5380\n");
			ncr_reset(&ncr_dev->ncr);
		}
		ncr->icr = val;
		break;

	case 2:		/* Mode register */
		ncr_log("Write: Mode register, val=%02x\n", val & MODE_DMA);
		if ((val & MODE_ARBITRATE) && !(ncr->mode & MODE_ARBITRATE)) {
			ncr->icr &= ~ICR_ARB_LOST;
			ncr->icr |=  ICR_ARB_IN_PROGRESS;
		}

		ncr->mode = val;
		
		/*Don't stop the timer until it finishes the transfer*/
		if (ncr_dev->block_count_loaded && (ncr->mode & MODE_DMA))	
			dma_changed(ncr_dev, ncr->dma_mode, ncr->mode & MODE_DMA);

		/*When a pseudo-DMA transfer has completed (Send or Initiator Receive), mark it as complete and idle the status*/
		if (!ncr_dev->block_count_loaded && !(ncr->mode & MODE_DMA)) {
			ncr_log("No DMA mode\n");
			ncr->tcr &= ~TCR_LAST_BYTE_SENT;
			ncr->isr &= ~STATUS_END_OF_DMA;
			ncr->dma_mode = DMA_IDLE;
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
		ncr->dma_mode = DMA_SEND;
		dma_changed(ncr_dev, ncr->dma_mode, ncr->mode & MODE_DMA);
		break;

	case 7:		/* start DMA Initiator Receive */
		ncr_log("Write: start DMA initiator receive register\n");
		ncr_log("Read 6 or 10, block count loaded=%d\n", ncr_dev->block_count_loaded);
		/*a Read 6/10 has occurred, start the timer when the block count is loaded*/
		ncr->dma_mode = DMA_INITIATOR_RECEIVE;
		dma_changed(ncr_dev, ncr->dma_mode, ncr->mode & MODE_DMA);
		break;

	default:
		ncr_log("NCR5380: bad write %04x %02x\n", port, val);
		break;
    }

    bus_host = get_bus_host(ncr);
    ncr_bus_update(priv, bus_host);
}


static uint8_t 
ncr_read(uint16_t port, void *priv)
{
    ncr5380_t *ncr_dev = (ncr5380_t *)priv;
    ncr_t *ncr = &ncr_dev->ncr;
    uint8_t ret = 0xff;
    int bus, bus_state;

    switch (port & 7) {
	case 0:		/* Current SCSI data */
		ncr_log("Read: Current SCSI data register\n");
		if (ncr->icr & ICR_DBP) {
			/*Return the data from the output register if on data bus phase from ICR*/
			ncr_log("Data Bus Phase\n");
			ret = ncr->output_data;
		} else {
			/*Return the data from the SCSI bus*/
			ncr_bus_read(ncr_dev);
			ncr_log("NCR GetData=%02x\n", BUS_GETDATA(ncr->cur_bus));
			ret = BUS_GETDATA(ncr->cur_bus);
		}
		break;

	case 1:		/* Initiator Command Register */
		ncr_log("Read: Initiator Command register, NCR ICR Read=%02x\n", ncr->icr);

		ret = ncr->icr;
		break;

	case 2:		/* Mode register */
		ncr_log("Read: Mode register\n");
		ret = ncr->mode;
		break;

	case 3:		/* Target Command Register */
		ncr_log("Read: Target Command register, NCR target stat=%02x\n", ncr->tcr);
		ret = ncr->tcr;
		break;

	case 4:		/* Current SCSI Bus status */
		ncr_log("Read: SCSI bus status register\n");
		ret = 0;
		ncr_bus_read(ncr_dev);
		ncr_log("NCR cur bus stat=%02x\n", ncr->cur_bus & 0xff);
		ret |= (ncr->cur_bus & 0xff);
		break;

	case 5:		/* Bus and Status register */
		ncr_log("Read: Bus and Status register\n");
		ret = 0;		

		bus = get_bus_host(ncr);
		ncr_log("Get host from Interrupt\n");
		
		/*Check if the phase in process matches with TCR's*/
		if ((bus & SCSI_PHASE_MESSAGE_IN) == (ncr->cur_bus & SCSI_PHASE_MESSAGE_IN)) {
			ncr_log("Phase match\n");
			ret |= STATUS_PHASE_MATCH;
		} else
			picint(1 << ncr_dev->irq);

		ncr_bus_read(ncr_dev);
		bus = ncr->cur_bus;

		if (bus & BUS_ACK)
			ret |= STATUS_ACK;
		
		if ((bus & BUS_REQ) && (ncr->mode & MODE_DMA)) {
			ncr_log("Entering DMA mode\n");
			ret |= STATUS_DRQ;
			
			bus_state = 0;
			
			if (bus & BUS_IO)
				bus_state |= TCR_IO;
			if (bus & BUS_CD)
				bus_state |= TCR_CD;
			if (bus & BUS_MSG)
				bus_state |= TCR_MSG;
			if ((ncr->tcr & 7) != bus_state)
				ncr->isr |= STATUS_INT;
		}
		if (!(bus & BUS_BSY) && (ncr->mode & MODE_MONITOR_BUSY)) {
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
		if (ncr_dev->buffer_host_pos >= 128 || !(ncr_dev->status_ctrl & CTRL_DATA_DIR))
			ret = 0xff;
		else {
			ret = ncr_dev->buffer[ncr_dev->buffer_host_pos++];
			
			if (ncr_dev->buffer_host_pos == 128) {	
				ncr_log("Not ready\n");
				ncr_dev->status_ctrl |= STATUS_BUFFER_NOT_READY;
			}
		}
		break;

	case 0x3980:
		switch (addr) {
			case 0x3980:	/* status */
				ret = ncr_dev->status_ctrl;
				ncr_log("NCR status ctrl read=%02x\n", ncr_dev->status_ctrl & STATUS_BUFFER_NOT_READY);
				if (!ncr_dev->ncr_busy)
					ret |= STATUS_53C80_ACCESSIBLE;
				break;

			case 0x3981:	/* block counter register*/
				ret = ncr_dev->block_count;
				break;

			case 0x3982:	/* switch register read */
				ret = 0xff;
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
		ncr_dev->int_ram[addr & 0x3f] = val;
		break;

	case 0x3880:
		ncr_write(addr, val, ncr_dev);
		break;
		
	case 0x3900:
		if (!(ncr_dev->status_ctrl & CTRL_DATA_DIR) && ncr_dev->buffer_host_pos < 128) {
			ncr_dev->buffer[ncr_dev->buffer_host_pos++] = val;

			if (ncr_dev->buffer_host_pos == 128) {
				ncr_dev->status_ctrl |= STATUS_BUFFER_NOT_READY;
				ncr_dev->ncr_busy = 1;
			}
		}
		break;

	case 0x3980:
		switch (addr) {
			case 0x3980:	/* Control */
				if ((val & CTRL_DATA_DIR) && !(ncr_dev->status_ctrl & CTRL_DATA_DIR)) {
					ncr_dev->buffer_host_pos = 128;
					ncr_dev->status_ctrl |= STATUS_BUFFER_NOT_READY;
				}
				else if (!(val & CTRL_DATA_DIR) && (ncr_dev->status_ctrl & CTRL_DATA_DIR)) {
					ncr_dev->buffer_host_pos = 0;
					ncr_dev->status_ctrl &= ~STATUS_BUFFER_NOT_READY;
				}
				ncr_dev->status_ctrl = (ncr_dev->status_ctrl & 0x87) | (val & 0x78);
				break;

			case 0x3981:	/* block counter register */
				ncr_log("Write block counter register: val=%d\n", val);
				ncr_dev->block_count = val;
				ncr_dev->block_count_loaded = 1;
				set_dma_enable(ncr_dev, ncr_dev->dma_enabled && ncr_dev->block_count_loaded);

				if (ncr_dev->status_ctrl & CTRL_DATA_DIR) {
					ncr_dev->buffer_host_pos = 128;
					ncr_dev->status_ctrl |= STATUS_BUFFER_NOT_READY;
				} else {
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
    else if (addr >= 0x1800 && addr < 0x1880)
	ret = ncr_dev->ext_ram[addr & 0x7f];

    ncr_log("MEM: Reading %02X from %08X\n", ret, addr);
    return(ret);
}


/* Memory-mapped I/O WRITE handler for the Trantor T130B. */
static void 
t130b_write(uint32_t addr, uint8_t val, void *priv)
{
    ncr5380_t *ncr_dev = (ncr5380_t *)priv;

    addr &= 0x3fff;
    ncr_log("MEM: Writing %02X to %08X\n", val, addr);
    if (addr >= 0x1800 && addr < 0x1880)
	ncr_dev->ext_ram[addr & 0x7f] = val;
}


static uint8_t 
t130b_in(uint16_t port, void *priv)
{
    ncr5380_t *ncr_dev = (ncr5380_t *)priv;
    uint8_t ret = 0xff;

    switch (port & 0x0f) {
	case 0x00: case 0x01: case 0x02: case 0x03:
		ret = memio_read((port & 7) | 0x3980, ncr_dev);
		break;

	case 0x04: case 0x05:
		ret = memio_read(0x3900, ncr_dev);
		break;		
		
	case 0x08: case 0x09: case 0x0a: case 0x0b:
	case 0x0c: case 0x0d: case 0x0e: case 0x0f:
		ret = ncr_read(port, ncr_dev);
		break;
    }

    ncr_log("I/O: Reading %02X from %04X\n", ret, port);
    return(ret);
}


static void 
t130b_out(uint16_t port, uint8_t val, void *priv)
{
    ncr5380_t *ncr_dev = (ncr5380_t *)priv;

    ncr_log("I/O: Writing %02X to %04X\n", val, port);

    switch (port & 0x0f) {
	case 0x00: case 0x01: case 0x02: case 0x03:
		memio_write((port & 7) | 0x3980, val, ncr_dev);
		break;

	case 0x04: case 0x05:
		memio_write(0x3900, val, ncr_dev);
		break;		
		
	case 0x08: case 0x09: case 0x0a: case 0x0b:
	case 0x0c: case 0x0d: case 0x0e: case 0x0f:
		ncr_write(port, val, ncr_dev);
		break;
    }
}


static void
ncr_callback(void *priv)
{
    ncr5380_t *ncr_dev = (ncr5380_t *)priv;
    ncr_t *ncr = &ncr_dev->ncr;
    int bus, bt = 0, c = 0;
    uint8_t temp, data;

    ncr_log("DMA mode=%d\n", ncr->dma_mode);

    if (ncr->data_wait & 1)
	ncr->clear_req = 3;

    if (ncr->dma_mode != DMA_IDLE)
	dma_timer_on(ncr_dev);

    if (ncr->data_wait & 1) {
	ncr->data_wait &= ~1;
	if (ncr->dma_mode == DMA_IDLE)
		return;
    }

    switch(ncr->dma_mode) {
	case DMA_SEND:
		if (ncr_dev->status_ctrl & CTRL_DATA_DIR) {
			ncr_log("DMA_SEND with DMA direction set wrong\n");
			break;
		}

		ncr_log("Status for writing=%02x\n", ncr_dev->status_ctrl);

		if (!(ncr_dev->status_ctrl & STATUS_BUFFER_NOT_READY)) {
			ncr_log("Buffer ready\n");
			break;
		}

		if (!ncr_dev->block_count_loaded)
			break;

		while (bt < 64) {
			for (c = 0; c < 10; c++) {
				ncr_bus_read(ncr_dev);
				if (ncr->cur_bus & BUS_REQ)
					break;
			}
			
			if (c == 10)
				break;			
			
			/* Data ready. */
			data = ncr_dev->buffer[ncr_dev->buffer_pos];
			bus = get_bus_host(ncr) & ~BUS_DATAMASK;
			bus |= BUS_SETDATA(data);

			ncr_bus_update(priv, bus | BUS_ACK);
			ncr_bus_update(priv, bus & ~BUS_ACK);

			bt++;
			ncr_dev->buffer_pos++;
			ncr_log("Buffer pos for writing = %d\n", ncr_dev->buffer_pos);
			
			if (ncr_dev->buffer_pos == 128) {
				ncr_dev->buffer_pos = 0;
				ncr_dev->buffer_host_pos = 0;
				ncr_dev->status_ctrl &= ~STATUS_BUFFER_NOT_READY;
				ncr_dev->ncr_busy = 0;
				ncr_dev->block_count = (ncr_dev->block_count - 1) & 255;
				ncr_log("Remaining blocks to be written=%d\n", ncr_dev->block_count);
				if (!ncr_dev->block_count) {
					ncr_dev->block_count_loaded = 0;
					set_dma_enable(ncr_dev, 0);
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
		break;

	case DMA_INITIATOR_RECEIVE:
		if (!(ncr_dev->status_ctrl & CTRL_DATA_DIR)) {
			ncr_log("DMA_INITIATOR_RECEIVE with DMA direction set wrong\n");
			break;
		}

		ncr_log("Status for reading=%02x\n", ncr_dev->status_ctrl);

		if (!(ncr_dev->status_ctrl & STATUS_BUFFER_NOT_READY))
			break;

		if (!ncr_dev->block_count_loaded)
			break;

		while (bt < 64) {
			for (c = 0; c < 10; c++) {
				ncr_bus_read(ncr_dev);
				if (ncr->cur_bus & BUS_REQ)
					break;
			}
			
			if (c == 10)
				break;

			/* Data ready. */
			ncr_bus_read(ncr_dev);
			temp = BUS_GETDATA(ncr->cur_bus);

			bus = get_bus_host(ncr);

			ncr_bus_update(priv, bus | BUS_ACK);
			ncr_bus_update(priv, bus & ~BUS_ACK);

			ncr_dev->buffer[ncr_dev->buffer_pos++] = temp;
			bt++;

			if (ncr_dev->buffer_pos == 128) {					
				ncr_dev->buffer_pos = 0;
				ncr_dev->buffer_host_pos = 0;
				ncr_dev->status_ctrl &= ~STATUS_BUFFER_NOT_READY;
				ncr_dev->block_count = (ncr_dev->block_count - 1) & 255;

				ncr_log("Remaining blocks to be read=%d\n", ncr_dev->block_count);

				if (!ncr_dev->block_count) {
					ncr_dev->block_count_loaded = 0;
					set_dma_enable(ncr_dev, 0);
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
		break;
    }

    ncr_bus_read(ncr_dev);

    if (!(ncr->cur_bus & BUS_BSY) && (ncr->mode & MODE_MONITOR_BUSY)) {
	ncr_log("Updating DMA\n");
	ncr->mode &= ~MODE_DMA;
	ncr->dma_mode = DMA_IDLE;
	dma_changed(ncr_dev, ncr->dma_mode, ncr->mode & MODE_DMA);
    }
}


static void *
ncr_init(const device_t *info)
{
	wchar_t *fn = NULL;
    char temp[128];
    ncr5380_t *ncr_dev;

    ncr_dev = malloc(sizeof(ncr5380_t));
    memset(ncr_dev, 0x00, sizeof(ncr5380_t));
    ncr_dev->name = info->name;
    ncr_dev->type = info->local;

    switch(ncr_dev->type) {
	case 0:		/* Longshine LCS6821N */
		ncr_dev->rom_addr = device_get_config_hex20("bios_addr");
		ncr_dev->irq = device_get_config_int("irq");
		rom_init(&ncr_dev->bios_rom, LCS6821N_ROM,
			 ncr_dev->rom_addr, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

		mem_mapping_add(&ncr_dev->mapping, ncr_dev->rom_addr, 0x4000, 
				memio_read, NULL, NULL,
				memio_write, NULL, NULL,
				ncr_dev->bios_rom.rom, MEM_MAPPING_EXTERNAL, ncr_dev);
		break;

	case 1:		/* Rancho RT1000B */
		ncr_dev->rom_addr = device_get_config_hex20("bios_addr");
		ncr_dev->irq = device_get_config_int("irq");
		ncr_dev->bios_ver = device_get_config_int("bios_ver");
		
		if (ncr_dev->bios_ver == 1)
			fn = RT1000B_820R_ROM;
		else
			fn = RT1000B_810R_ROM;
		
		rom_init(&ncr_dev->bios_rom, fn,
			 ncr_dev->rom_addr, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

		mem_mapping_add(&ncr_dev->mapping, ncr_dev->rom_addr, 0x4000, 
				memio_read, NULL, NULL,
				memio_write, NULL, NULL,
				ncr_dev->bios_rom.rom, MEM_MAPPING_EXTERNAL, ncr_dev);
		break;

	case 2:		/* Trantor T130B */
		ncr_dev->rom_addr = device_get_config_hex20("bios_addr");
		ncr_dev->base = device_get_config_hex16("base");
		ncr_dev->irq = device_get_config_int("irq");

		if (ncr_dev->rom_addr > 0x00000) {
			rom_init(&ncr_dev->bios_rom, T130B_ROM,
				 ncr_dev->rom_addr, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

			mem_mapping_add(&ncr_dev->mapping, ncr_dev->rom_addr, 0x4000, 
					t130b_read, NULL, NULL,
					t130b_write, NULL, NULL,
					ncr_dev->bios_rom.rom, MEM_MAPPING_EXTERNAL, ncr_dev);
		}

		io_sethandler(ncr_dev->base, 16,
			      t130b_in,NULL,NULL, t130b_out,NULL,NULL, ncr_dev);
		break;
    }

    sprintf(temp, "%s: BIOS=%05X", ncr_dev->name, ncr_dev->rom_addr);
    if (ncr_dev->base != 0)
	sprintf(&temp[strlen(temp)], " I/O=%04x", ncr_dev->base);
    if (ncr_dev->irq != 0)
	sprintf(&temp[strlen(temp)], " IRQ=%d", ncr_dev->irq);
    ncr_log("%s\n", temp);

    ncr_reset(&ncr_dev->ncr);
    ncr_dev->status_ctrl = STATUS_BUFFER_NOT_READY;
    ncr_dev->buffer_host_pos = 128;

    timer_add(&ncr_dev->timer, ncr_callback, ncr_dev, 0);

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
    return(rom_present(RT1000B_820R_ROM) && rom_present(RT1000B_810R_ROM));
}

static int
t130b_available(void)
{
    return(rom_present(T130B_ROM));
}


static const device_config_t ncr5380_mmio_config[] = {
        {
                "bios_addr", "BIOS Address", CONFIG_HEX20, "", 0xD8000, "", { 0 },
                {
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
		"irq", "IRQ", CONFIG_SELECTION, "", 5, "", { 0 },
                {
                        {
                                "IRQ 3", 3
                        },
                        {
                                "IRQ 5", 5
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

static const device_config_t rancho_config[] = {
        {
                "bios_addr", "BIOS Address", CONFIG_HEX20, "", 0xD8000, "", { 0 },
                {
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
		        "irq", "IRQ", CONFIG_SELECTION, "", 5, "", { 0 },
                {
                        {
                                "IRQ 3", 3
                        },
                        {
                                "IRQ 5", 5
                        },
                        {
                                ""
                        }
                },
        },
        {
		        "bios_ver", "BIOS Version", CONFIG_SELECTION, "", 1, "", { 0 },
                {
                        {
                                "8.20R", 1
                        },
                        {
                                "8.10R", 0
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
                "bios_addr", "BIOS Address", CONFIG_HEX20, "", 0xD8000, "", { 0 },
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
		"base", "Address", CONFIG_HEX16, "", 0x0350, "", { 0 },
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
		"irq", "IRQ", CONFIG_SELECTION, "", 5, "", { 0 },
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


const device_t scsi_lcs6821n_device =
{
    "Longshine LCS-6821N",
    DEVICE_ISA,
    0,
    ncr_init, ncr_close, NULL,
    { lcs6821n_available },
    NULL, NULL,
    ncr5380_mmio_config
};

const device_t scsi_rt1000b_device =
{
    "Rancho RT1000B",
    DEVICE_ISA,
    1,
    ncr_init, ncr_close, NULL,
    { rt1000b_available },
    NULL, NULL,
    rancho_config
};

const device_t scsi_t130b_device =
{
    "Trantor T130B",
    DEVICE_ISA,
    2,
    ncr_init, ncr_close, NULL,
    { t130b_available },
    NULL, NULL,
    t130b_config
};
