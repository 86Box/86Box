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
 * Version:	@(#)scsi_ncr5380.c	1.0.14	2018/04/26
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017,2018 Sarah Walker.
 *		Copyright 2017,2018 TheCollector1995.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../cpu/cpu.h"
#include "../io.h"
#include "../dma.h"
#include "../pic.h"
#include "../mca.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "../nvr.h"
#include "../timer.h"
#include "../plat.h"
#include "scsi.h"
#include "scsi_device.h"
#include "scsi_ncr5380.h"


#define LCS6821N_ROM	L"roms/scsi/ncr5380/Longshine LCS-6821N - BIOS version 1.04.bin"
#define RT1000B_ROM	L"roms/scsi/ncr5380/Rancho_RT1000_RTBios_version_8.10R.bin"
#define T130B_ROM	L"roms/scsi/ncr5380/trantor_t130b_bios_v2.14.bin"
#define SCSIAT_ROM	L"roms/scsi/ncr5380/sumo_scsiat_bios_v6.3.bin"


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

#define POLL_TIME_US 10LL
#define MAX_BYTES_TRANSFERRED_PER_POLL 50
/*10us poll period with 50 bytes transferred per poll = 5MB/sec*/


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
#define STATUS_INT		0x10
#define STATUS_DRQ		0x40
#define STATUS_END_OF_DMA	0x80

#define TCR_IO			0x01
#define TCR_CD			0x02
#define TCR_MSG			0x04
#define TCR_REQ			0x08
#define TCR_LAST_BYTE_SENT	0x80

#define CTRL_DATA_DIR		(1<<6)
#define STATUS_BUFFER_NOT_READY	(1<<2)
#define STATUS_53C80_ACCESSIBLE	(1<<7)


typedef struct {	
    uint8_t	icr;
    uint8_t	mode;
    uint8_t	tcr;
    uint8_t	ser;
    uint8_t	isr;
    uint8_t	output_data;

    int		target_bsy;
    int		target_req;
    uint8_t	target_id;

    uint8_t	bus_status;
	
    int		dma_mode;

    scsi_bus_t	bus;
} ncr5380_t;

typedef struct {
    const char	*name;
    uint32_t	rom_addr;
    uint16_t	base;
    int8_t	irq;
    int8_t	type;

    rom_t	bios_rom;
    mem_mapping_t mapping;

    uint8_t	block_count;
    int		block_count_loaded;

    uint8_t	status_ctrl;

    uint8_t	buffer[0x80];
    int		buffer_pos;
    int		buffer_host_pos;

    uint8_t	int_ram[0x40];        
    uint8_t	ext_ram[0x600];

    ncr5380_t	ncr;
    int		ncr5380_dma_enabled;

    int64_t	dma_timer;
    int64_t	dma_enabled;

    int		ncr_busy;
} ncr_t;


enum {
    DMA_IDLE = 0,
    DMA_SEND,
    DMA_TARGET_RECEIVE,
    DMA_INITIATOR_RECEIVE
};


#ifdef ENABLE_NCR5380_LOG
int ncr5380_do_log = ENABLE_NCR5380_LOG;
#endif


static void
ncr_log(const char *fmt, ...)
{
#ifdef ENABLE_NCR5380_LOG
    va_list ap;

    if (ncr5380_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
#endif
}


static void
dma_changed(void *priv, int mode, int enable)
{
    ncr_t *scsi = (ncr_t *)priv;

    scsi->ncr5380_dma_enabled = (mode && enable);

    scsi->dma_enabled = (scsi->ncr5380_dma_enabled && scsi->block_count_loaded);
}


void
ncr5380_reset(ncr5380_t *ncr)
{
    memset(ncr, 0x00, sizeof(ncr5380_t));
}


static uint32_t
get_bus_host(ncr5380_t *ncr)
{
    uint32_t bus_host = 0;

    if (ncr->icr & ICR_DBP)
	bus_host |= BUS_DBP;
    if (ncr->icr & ICR_SEL)
	bus_host |= BUS_SEL;
    if (ncr->icr & ICR_ATN)
	bus_host |= BUS_ATN;
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
    if (ncr->icr & ICR_ACK)
	bus_host |= BUS_ACK;
    if (ncr->mode & MODE_ARBITRATE)
	bus_host |= BUS_ARB;

    return(bus_host | BUS_SETDATA(ncr->output_data));
}


static void 
ncr_write(uint16_t port, uint8_t val, void *priv)
{
    ncr_t *scsi = (ncr_t *)priv;
    ncr5380_t *ncr = &scsi->ncr;
    int bus_host = 0;

#if ENABLE_NCR5380_LOG
    ncr_log("NCR5380 write(%04x,%02x) @%04X:%04X\n",port,val,CS,cpu_state.pc);
#endif
    switch (port & 7) {
	case 0:		/* Output data register */
		ncr->output_data = val;
		break;

	case 1:		/* Initiator Command Register */
		if ((val & (ICR_BSY | ICR_SEL)) == (ICR_BSY | ICR_SEL) &&
			(ncr->icr & (ICR_BSY | ICR_SEL)) == ICR_SEL) {
			uint8_t temp = ncr->output_data & 0x7f;

			ncr->target_id = -1;
			while (temp) {
				temp >>= 1;
				ncr->target_id++;
			}

			ncr_log("Select - target ID = %i, temp data %x\n", ncr->target_id, temp);
		}			
		ncr->icr = val;
		break;

	case 2:		/* Mode register */
		if ((val & MODE_ARBITRATE) && !(ncr->mode & MODE_ARBITRATE)) {
			ncr->icr &= ~ICR_ARB_LOST;
			ncr->icr |=  ICR_ARB_IN_PROGRESS;
		}
		ncr->mode = val;
		dma_changed(scsi, ncr->dma_mode, ncr->mode & MODE_DMA);
		if (! (ncr->mode & MODE_DMA)) {
			ncr->tcr &= ~TCR_LAST_BYTE_SENT;
			ncr->isr &= ~STATUS_END_OF_DMA;
			ncr->dma_mode = DMA_IDLE;
		}
		break;

	case 3:		/* Target Command Register */
		ncr->tcr = val;
		break;

	case 4:		/* Select Enable Register */
		ncr->ser = val;
		break;

	case 5:		/* start DMA Send */
		ncr->dma_mode = DMA_SEND;
		dma_changed(scsi, ncr->dma_mode, ncr->mode & MODE_DMA);
		break;

	case 7:		/* start DMA Initiator Receive */
		ncr->dma_mode = DMA_INITIATOR_RECEIVE;
		dma_changed(scsi, ncr->dma_mode, ncr->mode & MODE_DMA);
		break;

	default:
#if 1
		pclog("NCR5380: bad write %04x %02x\n", port, val);
#endif
		break;
    }
	
    bus_host = get_bus_host(ncr);

    scsi_bus_update(&ncr->bus, bus_host);
}


static uint8_t 
ncr_read(uint16_t port, void *priv)
{
    ncr_t *scsi = (ncr_t *)priv;
    ncr5380_t *ncr = &scsi->ncr;
    uint32_t bus = 0;
    uint8_t ret = 0xff;

    switch (port & 7) {
	case 0:		/* current SCSI data */
		if (ncr->icr & ICR_DBP) {
			ret = ncr->output_data;
		} else {	
			bus = scsi_bus_read(&ncr->bus);
			ret = BUS_GETDATA(bus);
		}
		break;

	case 1:		/* Initiator Command Register */
		ret = ncr->icr;
		break;

	case 2:		/* Mode register */
		ret = ncr->mode;
		break;

	case 3:		/* Target Command Register */
		ret = ncr->tcr;
		break;

	case 4:		/* Current SCSI Bus status */
		ret = 0;
		bus = scsi_bus_read(&ncr->bus);
		ret |= (bus & 0xff);
		break;

	case 5:		/* Bus and Status register */
		ret = 0;

		bus = get_bus_host(ncr);
		if (scsi_bus_match(&ncr->bus, bus))
			ret |= 0x08;
		bus = scsi_bus_read(&ncr->bus);

		if (bus & BUS_ACK)
			ret |= STATUS_ACK;
		if ((bus & BUS_REQ) && (ncr->mode & MODE_DMA))
			ret |= STATUS_DRQ;
		if ((bus & BUS_REQ) && (ncr->mode & MODE_DMA)) {
			int bus_state = 0;

			if (bus & BUS_IO)
				bus_state |= TCR_IO;
			if (bus & BUS_CD)
				bus_state |= TCR_CD;
			if (bus & BUS_MSG)
				bus_state |= TCR_MSG;
			if ((ncr->tcr & 7) != bus_state)
				ncr->isr |= STATUS_INT;
		}
		if (!(bus & BUS_BSY) && (ncr->mode & MODE_MONITOR_BUSY))
				ret |= STATUS_BUSY_ERROR;
		ret |= (ncr->isr & (STATUS_INT | STATUS_END_OF_DMA));
		break;

	case 7:		/* reset Parity/Interrupt */
		ncr->isr &= ~STATUS_INT;
		break;

	default:
		ncr_log("NCR5380: bad read %04x\n", port);
		break;
    }

#if ENABLE_NCR5380_LOG
    ncr_log("NCR5380 read(%04x)=%02x @%04X:%04X\n", port, ret, CS,cpu_state.pc);
#endif
    return(ret);
}


static void
dma_callback(void *priv)
{
    ncr_t *scsi = (ncr_t *)priv;
    ncr5380_t *ncr = &scsi->ncr;
    int bytes_transferred = 0;
    int c;

    scsi->dma_timer += POLL_TIME_US * TIMER_USEC;

    switch (scsi->ncr.dma_mode) {
	case DMA_SEND:
		if (scsi->status_ctrl & CTRL_DATA_DIR) {
			ncr_log("DMA_SEND with DMA direction set wrong\n");
			break;
		}

		if (!(scsi->status_ctrl & STATUS_BUFFER_NOT_READY)) {
			break;
		}

		if (! scsi->block_count_loaded) break;

		while (bytes_transferred < MAX_BYTES_TRANSFERRED_PER_POLL) {
			int bus;
			uint8_t data;

			for (c = 0; c < 10; c++) {
				uint8_t status = scsi_bus_read(&ncr->bus);

				if (status & BUS_REQ) break;
			}
			if (c == 10) break;

			/* Data ready. */
			data = scsi->buffer[scsi->buffer_pos];
			bus = get_bus_host(ncr) & ~BUS_DATAMASK;
			bus |= BUS_SETDATA(data);

			scsi_bus_update(&ncr->bus, bus | BUS_ACK);
			scsi_bus_update(&ncr->bus, bus & ~BUS_ACK);

			bytes_transferred++;
			if (++scsi->buffer_pos == 128) {
				scsi->buffer_pos = 0;
				scsi->buffer_host_pos = 0;
				scsi->status_ctrl &= ~STATUS_BUFFER_NOT_READY;
				scsi->block_count = (scsi->block_count - 1) & 255;
				scsi->ncr_busy = 0;
				if (! scsi->block_count) {
					scsi->block_count_loaded = 0;
					scsi->dma_enabled = 0;

					ncr->tcr |= TCR_LAST_BYTE_SENT;
					ncr->isr |= STATUS_END_OF_DMA;
					if (ncr->mode & MODE_ENA_EOP_INT)
						ncr->isr |= STATUS_INT;
				}
				break;
			}
		}
		break;

	case DMA_INITIATOR_RECEIVE:
		if (!(scsi->status_ctrl & CTRL_DATA_DIR)) {
			ncr_log("DMA_INITIATOR_RECEIVE with DMA direction set wrong\n");
			break;
		}

		if (!(scsi->status_ctrl & STATUS_BUFFER_NOT_READY)) break;

		if (!scsi->block_count_loaded) break;

		while (bytes_transferred < MAX_BYTES_TRANSFERRED_PER_POLL) {
			int bus;
			uint8_t temp;

			for (c = 0; c < 10; c++) {
				uint8_t status = scsi_bus_read(&ncr->bus);

				if (status & BUS_REQ) break;
			}
			if (c == 10) break;

			/* Data ready. */
       			bus = scsi_bus_read(&ncr->bus);
       			temp = BUS_GETDATA(bus);
			bus = get_bus_host(ncr);

			scsi_bus_update(&ncr->bus, bus | BUS_ACK);
			scsi_bus_update(&ncr->bus, bus & ~BUS_ACK);

			bytes_transferred++;
			scsi->buffer[scsi->buffer_pos++] = temp;
			if (scsi->buffer_pos == 128) {
				scsi->buffer_pos = 0;
				scsi->buffer_host_pos = 0;
				scsi->status_ctrl &= ~STATUS_BUFFER_NOT_READY;
				scsi->block_count = (scsi->block_count - 1) & 255;
				if (!scsi->block_count) {
					scsi->block_count_loaded = 0;
					scsi->dma_enabled = 0;

					ncr->isr |= STATUS_END_OF_DMA;
					if (ncr->mode & MODE_ENA_EOP_INT)
						ncr->isr |= STATUS_INT;
				}
				break;
			}
		}
		break;

	default:
#if 1
		pclog("DMA callback bad mode %i\n", scsi->ncr.dma_mode);
#endif
		break;
    }

    c = scsi_bus_read(&ncr->bus);
    if (!(c & BUS_BSY) && (ncr->mode & MODE_MONITOR_BUSY)) {
	ncr->mode &= ~MODE_DMA;
	ncr->dma_mode = DMA_IDLE;
	dma_changed(scsi, ncr->dma_mode, ncr->mode & MODE_DMA);
    }
}


/* Memory-mapped I/O READ handler. */
static uint8_t 
memio_read(uint32_t addr, void *priv)
{
    ncr_t *scsi = (ncr_t *)priv;
    uint8_t ret = 0xff;

    addr &= 0x3fff;
#if ENABLE_NCR5380_LOG
    ncr_log("memio_read %08x\n", addr);
#endif

    if (addr < 0x2000)
	ret = scsi->bios_rom.rom[addr & 0x1fff];
    else if (addr < 0x3800)
	ret = 0xff;
    else if (addr >= 0x3a00)
	ret = scsi->ext_ram[addr - 0x3a00];
    else switch (addr & 0x3f80) {
	case 0x3800:
#if ENABLE_NCR5380_LOG
		ncr_log("Read intRAM %02x %02x\n", addr & 0x3f, scsi->int_ram[addr & 0x3f]);
#endif
		ret = scsi->int_ram[addr & 0x3f];
		break;

	case 0x3880:
#if ENABLE_NCR5380_LOG
		ncr_log("Read 53c80 %04x\n", addr);
#endif
		ret = ncr_read(addr, scsi);
		break;

	case 0x3900:
#if ENABLE_NCR5380_LOG
		ncr_log(" Read 3900 %i %02x\n", scsi->buffer_host_pos, scsi->status_ctrl);
#endif
		if (scsi->buffer_host_pos >= 128 || !(scsi->status_ctrl & CTRL_DATA_DIR))
			ret = 0xff;
		else {
			ret = scsi->buffer[scsi->buffer_host_pos++];

			if (scsi->buffer_host_pos == 128)
				scsi->status_ctrl |= STATUS_BUFFER_NOT_READY;
		}
		break;

	case 0x3980:
		switch (addr) {
			case 0x3980:	/* status */
				ret = scsi->status_ctrl;// | 0x80;
				if (! scsi->ncr_busy)
					ret |= STATUS_53C80_ACCESSIBLE;
				break;

			case 0x3981:	/* block counter register*/
				ret = scsi->block_count;
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
    ncr_t *scsi = (ncr_t *)priv;

    addr &= 0x3fff;

#if ENABLE_NCR5380_LOG
    ncr_log("memio_write(%08x,%02x) @%04X:%04X  %i %02x\n", addr, val, CS,cpu_state.pc,  scsi->buffer_host_pos, scsi->status_ctrl);
#endif

    if (addr >= 0x3a00)
	scsi->ext_ram[addr - 0x3a00] = val;
    else switch (addr & 0x3f80) {
	case 0x3800:
#if 0
		ncr_log("Write intram %02x %02x\n", addr & 0x3f, val);
#endif
		scsi->int_ram[addr & 0x3f] = val;
		break;

	case 0x3880:
#if ENABLE_NCR5380_LOG
		ncr_log("Write 53c80 %04x %02x\n", addr, val);
#endif
		ncr_write(addr, val, scsi);
		break;

	case 0x3900:
		if (!(scsi->status_ctrl & CTRL_DATA_DIR) && scsi->buffer_host_pos < 128) {
			scsi->buffer[scsi->buffer_host_pos++] = val;
			if (scsi->buffer_host_pos == 128) {
				scsi->status_ctrl |= STATUS_BUFFER_NOT_READY;
				scsi->ncr_busy = 1;
			}
		}
		break;

	case 0x3980:
		switch (addr) {
			case 0x3980:	/* Control */
				if ((val & CTRL_DATA_DIR) && !(scsi->status_ctrl & CTRL_DATA_DIR)) {
					scsi->buffer_host_pos = 128;
					scsi->status_ctrl |= STATUS_BUFFER_NOT_READY;
				}
				else if (!(val & CTRL_DATA_DIR) && (scsi->status_ctrl & CTRL_DATA_DIR)) {
					scsi->buffer_host_pos = 0;
					scsi->status_ctrl &= ~STATUS_BUFFER_NOT_READY;
				}
				scsi->status_ctrl = (scsi->status_ctrl & 0x87) | (val & 0x78);
				break;

			case 0x3981:	/* block counter register */
				scsi->block_count = val;
				scsi->block_count_loaded = 1;
				scsi->dma_enabled = (scsi->ncr5380_dma_enabled && scsi->block_count_loaded);
				if (scsi->status_ctrl & CTRL_DATA_DIR) {
					scsi->buffer_host_pos = 128;
					scsi->status_ctrl |= STATUS_BUFFER_NOT_READY;
				} else {
					scsi->buffer_host_pos = 0;
					scsi->status_ctrl &= ~STATUS_BUFFER_NOT_READY;
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
    ncr_t *scsi = (ncr_t *)priv;
    uint8_t ret = 0xff;

    addr &= 0x3fff;
    if (addr < 0x1800)
	ret = scsi->bios_rom.rom[addr & 0x1fff];
      else
    if (addr < 0x1880)
	ret = scsi->ext_ram[addr & 0x7f];

    return(ret);
}


/* Memory-mapped I/O WRITE handler for the Trantor T130B. */
static void 
t130b_write(uint32_t addr, uint8_t val, void *priv)
{
    ncr_t *scsi = (ncr_t *)priv;

    addr &= 0x3fff;
    if (addr >= 0x1800 && addr < 0x1880)
	scsi->ext_ram[addr & 0x7f] = val;
}


static uint8_t 
t130b_in(uint16_t port, void *priv)
{
    ncr_t *scsi = (ncr_t *)priv;
    uint8_t ret = 0xff;

    switch (port & 0x0f) {
	case 0x00:
	case 0x01:
	case 0x02:
	case 0x03:
		ret = memio_read((port & 7) | 0x3980, scsi);
		break;

	case 0x04:
	case 0x05:
		ret = memio_read(0x3900, scsi);
		break;

	case 0x08:
	case 0x09:
	case 0x0a:
	case 0x0b:
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x0f:
		ret = ncr_read(port, scsi);
		break;
    }

    return(ret);
}


static void 
t130b_out(uint16_t port, uint8_t val, void *priv)
{
    ncr_t *scsi = (ncr_t *)priv;

    switch (port & 0x0f) {
	case 0x00:
	case 0x01:
	case 0x02:
	case 0x03:
		memio_write((port & 7) | 0x3980, val, scsi);
		break;

	case 0x04:
	case 0x05:
		memio_write(0x3900, val, scsi);
		break;

	case 0x08:
	case 0x09:
	case 0x0a:
	case 0x0b:
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x0f:
		ncr_write(port, val, scsi);
		break;
    }
}


static uint8_t 
scsiat_in(uint16_t port, void *priv)
{
    ncr_t *scsi = (ncr_t *)priv;
    uint8_t ret = 0xff;

    switch (port & 0x0f) {
	case 0x00:
	case 0x01:
	case 0x02:
	case 0x03:
		ret = memio_read((port & 7) | 0x3980, scsi);
		break;

	case 0x04:
	case 0x05:
		ret = memio_read(0x3900, scsi);
		break;

	case 0x08:
	case 0x09:
	case 0x0a:
	case 0x0b:
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x0f:
		ret = ncr_read(port, scsi);
		break;
    }

    return(ret);
}


static void 
scsiat_out(uint16_t port, uint8_t val, void *priv)
{
    ncr_t *scsi = (ncr_t *)priv;

    switch (port & 0x0f) {
	case 0x00:
	case 0x01:
	case 0x02:
	case 0x03:
		memio_write((port & 7) | 0x3980, val, scsi);
		break;

	case 0x04:
	case 0x05:
		memio_write(0x3900, val, scsi);
		break;

	case 0x08:
	case 0x09:
	case 0x0a:
	case 0x0b:
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x0f:
		ncr_write(port, val, scsi);
		break;
    }
}


static void *
ncr_init(const device_t *info)
{
    char temp[128];
    ncr_t *scsi;

    scsi = malloc(sizeof(ncr_t));
    memset(scsi, 0x00, sizeof(ncr_t));
    scsi->name = info->name;
    scsi->type = info->local;

    switch(scsi->type) {
	case 0:		/* Longshine LCS6821N */
		scsi->rom_addr = 0xDC000;
		rom_init(&scsi->bios_rom, LCS6821N_ROM,
			 scsi->rom_addr, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);
		mem_mapping_disable(&scsi->bios_rom.mapping);

		mem_mapping_add(&scsi->mapping, scsi->rom_addr, 0x4000, 
				memio_read, NULL, NULL,
				memio_write, NULL, NULL,
				scsi->bios_rom.rom, 0, scsi);
		break;

	case 1:		/* Ranco RT1000B */
		scsi->rom_addr = 0xDC000;
		rom_init(&scsi->bios_rom, RT1000B_ROM,
			 scsi->rom_addr, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

		mem_mapping_disable(&scsi->bios_rom.mapping);

		mem_mapping_add(&scsi->mapping, scsi->rom_addr, 0x4000, 
				memio_read, NULL, NULL,
				memio_write, NULL, NULL,
				scsi->bios_rom.rom, 0, scsi);
		break;

	case 2:		/* Trantor T130B */
		scsi->rom_addr = 0xDC000;
		scsi->base = 0x0350;
		rom_init(&scsi->bios_rom, T130B_ROM,
			 scsi->rom_addr, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

		mem_mapping_add(&scsi->mapping, scsi->rom_addr, 0x4000, 
				t130b_read, NULL, NULL,
				t130b_write, NULL, NULL,
				scsi->bios_rom.rom, 0, scsi);

		io_sethandler(scsi->base, 16,
			      t130b_in,NULL,NULL, t130b_out,NULL,NULL, scsi);
		break;

	case 3:		/* Sumo SCSI-AT */
		scsi->base = device_get_config_hex16("base");
		scsi->irq = device_get_config_int("irq");
		scsi->rom_addr = device_get_config_hex20("bios_addr");
		rom_init(&scsi->bios_rom, SCSIAT_ROM,
			 scsi->rom_addr, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

		mem_mapping_add(&scsi->mapping, scsi->rom_addr, 0x4000, 
				t130b_read, NULL, NULL,
				t130b_write, NULL, NULL,
				scsi->bios_rom.rom, 0, scsi);

		io_sethandler(scsi->base, 16,
			      scsiat_in,NULL,NULL, scsiat_out,NULL,NULL, scsi);
		break;
    }

    sprintf(temp, "%s: BIOS=%05X", scsi->name, scsi->rom_addr);
    if (scsi->base != 0)
	sprintf(&temp[strlen(temp)], " I/O=%04x", scsi->base);
    if (scsi->irq != 0)
	sprintf(&temp[strlen(temp)], " IRQ=%d", scsi->irq);
    pclog("%s\n", temp);

    ncr5380_reset(&scsi->ncr);

    scsi->status_ctrl = STATUS_BUFFER_NOT_READY;
    scsi->buffer_host_pos = 128;

    timer_add(dma_callback, &scsi->dma_timer, &scsi->dma_enabled, scsi);

    return(scsi);
}


static void 
ncr_close(void *priv)
{
    ncr_t *scsi = (ncr_t *)priv;

    free(scsi);
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


static int
scsiat_available(void)
{
    return(rom_present(SCSIAT_ROM));
}


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


const device_t scsi_lcs6821n_device =
{
    "Longshine LCS-6821N",
    DEVICE_ISA,
    0,
    ncr_init, ncr_close, NULL,
    lcs6821n_available,
    NULL, NULL,
    NULL
};

const device_t scsi_rt1000b_device =
{
    "Ranco RT1000B",
    DEVICE_ISA,
    1,
    ncr_init, ncr_close, NULL,
    rt1000b_available,
    NULL, NULL,
    NULL
};

const device_t scsi_t130b_device =
{
    "Trantor T130B",
    DEVICE_ISA,
    2,
    ncr_init, ncr_close, NULL,
    t130b_available,
    NULL, NULL,
    NULL
};

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
