/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the AMD PCscsi and Tekram DC-390 SCSI
 *		controllers using the NCR 53c9x series of chips.
 *
 *
 *
 *
 * Authors:	Fabrice Bellard (QEMU)
 *		Herve Poussineau (QEMU)
 *		TheCollector1995, <mariogplayer@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2005-2018 Fabrice Bellard.
 *		Copyright 2012-2018 Herve Poussineau.
 *		Copyright 2017,2018 Miran Grca.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/pci.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/scsi_pcscsi.h>
#include <86box/vid_ati_eeprom.h>

#define DC390_ROM	"roms/scsi/esp_pci/INT13.BIN"

#define ESP_REGS 16
#define TI_BUFSZ 32
#define ESP_CMDBUF_SZ 32

#define ESP_TCLO   0x0
#define ESP_TCMID  0x1
#define ESP_FIFO   0x2
#define ESP_CMD    0x3
#define ESP_RSTAT  0x4
#define ESP_WBUSID 0x4
#define ESP_RINTR  0x5
#define ESP_WSEL   0x5
#define ESP_RSEQ   0x6
#define ESP_WSYNTP 0x6
#define ESP_RFLAGS 0x7
#define ESP_WSYNO  0x7
#define ESP_CFG1   0x8
#define ESP_RRES1  0x9
#define ESP_WCCF   0x9
#define ESP_RRES2  0xa
#define ESP_WTEST  0xa
#define ESP_CFG2   0xb
#define ESP_CFG3   0xc
#define ESP_RES3   0xd
#define ESP_TCHI   0xe
#define ESP_RES4   0xf

#define CMD_DMA 0x80
#define CMD_CMD 0x7f

#define CMD_NOP      0x00
#define CMD_FLUSH    0x01
#define CMD_RESET    0x02
#define CMD_BUSRESET 0x03
#define CMD_TI       0x10
#define CMD_ICCS     0x11
#define CMD_MSGACC   0x12
#define CMD_PAD      0x18
#define CMD_SATN     0x1a
#define CMD_RSTATN   0x1b
#define CMD_SEL      0x41
#define CMD_SELATN   0x42
#define CMD_SELATNS  0x43
#define CMD_ENSEL    0x44
#define CMD_DISSEL   0x45

#define STAT_DO 0x00
#define STAT_DI 0x01
#define STAT_CD 0x02
#define STAT_ST 0x03
#define STAT_MO 0x06
#define STAT_MI 0x07
#define STAT_PIO_MASK 0x06

#define STAT_TC 0x10
#define STAT_PE 0x20
#define STAT_GE 0x40
#define STAT_INT 0x80

#define BUSID_DID 0x07

#define INTR_FC 0x08
#define INTR_BS 0x10
#define INTR_DC 0x20
#define INTR_RST 0x80

#define SEQ_0 0x0
#define SEQ_CD 0x4

#define CFG1_RESREPT 0x40

#define TCHI_AM53C974 0x12

#define DMA_CMD   0x0
#define DMA_STC   0x1
#define DMA_SPA   0x2
#define DMA_WBC   0x3
#define DMA_WAC   0x4
#define DMA_STAT  0x5
#define DMA_SMDLA 0x6
#define DMA_WMAC  0x7

#define DMA_CMD_MASK   0x03
#define DMA_CMD_DIAG   0x04
#define DMA_CMD_MDL    0x10
#define DMA_CMD_INTE_P 0x20
#define DMA_CMD_INTE_D 0x40
#define DMA_CMD_DIR    0x80

#define DMA_STAT_PWDN    0x01
#define DMA_STAT_ERROR   0x02
#define DMA_STAT_ABORT   0x04
#define DMA_STAT_DONE    0x08
#define DMA_STAT_SCSIINT 0x10
#define DMA_STAT_BCMBLT  0x20

#define SBAC_STATUS (1 << 24)
#define SBAC_PABTEN (1 << 25)

typedef struct {
    mem_mapping_t mmio_mapping;
    char	*nvr_path;
    uint8_t	pci_slot;
    int		has_bios;
    int		BIOSBase;
    int		MMIOBase;
    rom_t	bios;
    ati_eeprom_t eeprom;
    int		PCIBase;

    uint8_t rregs[ESP_REGS];
    uint8_t wregs[ESP_REGS];
    int irq;
    int tchi_written;
    uint32_t ti_size;
    uint32_t ti_rptr, ti_wptr;
    uint32_t status;
    uint32_t deferred_status;
    int deferred_complete;
    uint32_t dma;
    uint8_t ti_buf[TI_BUFSZ];
    uint8_t bus;
    uint8_t id, lun;
    uint8_t cmdbuf[ESP_CMDBUF_SZ];
    uint32_t cmdlen;
    uint32_t do_cmd;

    uint32_t dma_counter;
    uint32_t dma_left;
    int32_t xfer_counter;
    int dma_enabled;
    
    uint32_t buffer_pos;
    uint32_t async_len;
    uint32_t dma_regs[8];
    uint32_t sbac;

    double period;

    pc_timer_t timer;
} esp_t;


#ifdef ENABLE_ESP_LOG
int esp_do_log = ENABLE_ESP_LOG;


static void
esp_log(const char *fmt, ...)
{
    va_list ap;

    if (esp_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define esp_log(fmt, ...)
#endif

static void esp_do_dma(esp_t *dev, scsi_device_t *sd);
static void esp_pci_dma_memory_rw(esp_t *dev, uint8_t *buf, uint32_t len, int dir);
static void esp_timer_on(esp_t *dev, scsi_device_t *sd, double p);
static void esp_command_complete(void *priv, uint32_t status);
static void esp_pci_command_complete(void *priv, uint32_t status);
static void esp_pci_soft_reset(esp_t *dev);
static void esp_pci_hard_reset(esp_t *dev);
static void handle_ti(void *priv);

static void
esp_irq(esp_t *dev, int level)
{
    if (level) {
    	pci_set_irq(dev->pci_slot, PCI_INTA);
	esp_log("Raising IRQ...\n");
    } else {
    	pci_clear_irq(dev->pci_slot, PCI_INTA);
	esp_log("Lowering IRQ...\n");
    }
}


static void
esp_raise_irq(esp_t *dev)
{
    if (!(dev->rregs[ESP_RSTAT] & STAT_INT)) {
	dev->rregs[ESP_RSTAT] |= STAT_INT;
	esp_irq(dev, 1);
    }
}

static void
esp_lower_irq(esp_t *dev)
{
    if (dev->rregs[ESP_RSTAT] & STAT_INT) {
	dev->rregs[ESP_RSTAT] &= ~STAT_INT;
	esp_irq(dev, 0);
    }
}

static void
esp_dma_done(esp_t *dev)
{
    dev->rregs[ESP_RSTAT] |= STAT_TC;
    dev->rregs[ESP_RINTR] = INTR_BS;
    dev->rregs[ESP_RSEQ] = 0;
    dev->rregs[ESP_RFLAGS] = 0;
    dev->rregs[ESP_TCLO] = 0;
    dev->rregs[ESP_TCMID] = 0;
    dev->rregs[ESP_TCHI] = 0;
    esp_log("ESP DMA Finished\n");
    esp_raise_irq(dev);
}

static uint32_t
esp_get_cmd(esp_t *dev, uint8_t *buf, uint8_t buflen)
{
    uint32_t dmalen;
    
    dev->id = dev->wregs[ESP_WBUSID] & BUSID_DID;
    if (dev->dma) {
        dmalen = dev->rregs[ESP_TCLO];
        dmalen |= dev->rregs[ESP_TCMID] << 8;
        dmalen |= dev->rregs[ESP_TCHI] << 16;
	esp_log("ESP Get data, dmalen = %d\n", dmalen);
	if (dmalen > buflen)
	    return 0;
	esp_pci_dma_memory_rw(dev, buf, dmalen, 0);
    } else {
	dmalen = dev->ti_size;
	esp_log("ESP Get command, dmalen = %d\n", dmalen);
	if (dmalen > TI_BUFSZ)
	    return 0;
	memcpy(buf, dev->ti_buf, dmalen);
	dev->lun = buf[0] & 7;
    }
    
    dev->ti_size = 0;
    dev->ti_rptr = 0;
    dev->ti_wptr = 0;
    
    if (scsi_device_present(&scsi_devices[dev->bus][dev->id]) && (dev->lun > 0)) {
        /* We only support LUN 0 */
        dev->rregs[ESP_RSTAT] = 0;
        dev->rregs[ESP_RINTR] = INTR_DC;
        dev->rregs[ESP_RSEQ] = SEQ_0;
	esp_raise_irq(dev);
	return 0;
    }

    scsi_device_identify(&scsi_devices[dev->bus][dev->id], dev->lun);

    return dmalen;
}

static void
esp_do_busid_cmd(esp_t *dev, uint8_t *buf, uint8_t busid)
{
    scsi_device_t *sd;

    sd = &scsi_devices[dev->bus][busid];	

    sd->buffer_length = -1;

    scsi_device_command_phase0(sd, buf);
    
    dev->buffer_pos = 0;
    dev->ti_size = sd->buffer_length;
    dev->xfer_counter = sd->buffer_length;

    esp_log("ESP SCSI Command = %02x, ID = %d, LUN = %d, len = %d\n", buf[0], busid, buf[1] >> 5, sd->buffer_length);
    
    if (sd->buffer_length > 0) {
	/* This should be set to the underlying device's buffer by command phase 0. */
	dev->rregs[ESP_RSTAT] = STAT_TC;
	dev->dma_left = 0;
	dev->dma_counter = 0;

	if (sd->phase == SCSI_PHASE_DATA_IN) {
		dev->rregs[ESP_RSTAT] |= STAT_DI;
		esp_log("ESP Data In\n");
		esp_timer_on(dev, sd, scsi_device_get_callback(sd));		
	} else if (sd->phase == SCSI_PHASE_DATA_OUT) {
		dev->rregs[ESP_RSTAT] |= STAT_DO;
		esp_log("ESP Data Out\n");
		dev->ti_size = -sd->buffer_length;
		esp_timer_on(dev, sd, scsi_device_get_callback(sd));		
	}
	esp_log("ESP SCSI Start reading/writing\n");
	esp_do_dma(dev, sd);
    } else {
	esp_log("ESP SCSI Command with no length\n");
	esp_pci_command_complete(dev, sd->status);
    }

    scsi_device_identify(sd, SCSI_LUN_USE_CDB);

    dev->rregs[ESP_RINTR] = INTR_BS | INTR_FC;
    dev->rregs[ESP_RSEQ] = SEQ_CD;
    esp_raise_irq(dev);
}

static void
esp_do_cmd(esp_t *dev, uint8_t *buf)
{
    esp_do_busid_cmd(dev, &buf[1], dev->id);
}

static void
esp_dma_enable(esp_t *dev, int level)
{
    if (level) {
	esp_log("ESP DMA Enabled\n");
	dev->dma_enabled = 1;
	if ((dev->rregs[ESP_CMD] & CMD_CMD) != CMD_TI)
		timer_on_auto(&dev->timer, 40.0);
	else
		timer_on_auto(&dev->timer, dev->period);
    } else {
	esp_log("ESP DMA Disabled\n");
	dev->dma_enabled = 0;
    }
}

static void
esp_hard_reset(esp_t *dev)
{
    memset(dev->rregs, 0, ESP_REGS);
    memset(dev->wregs, 0, ESP_REGS);
    dev->tchi_written = 0;
    dev->ti_size = 0;
    dev->ti_rptr = 0;
    dev->ti_wptr = 0;
    dev->dma = 0;
    dev->do_cmd = 0;
    dev->rregs[ESP_CFG1] = 7;
    esp_log("ESP Reset\n");
    timer_stop(&dev->timer);
    
    for (int i = 0; i < 8; i++)
	scsi_device_reset(&scsi_devices[dev->bus][i]);
}

static void
esp_do_dma(esp_t *dev, scsi_device_t *sd)
{
    uint32_t tdbc;
    int count;

    esp_log("ESP SCSI Actual DMA len = %d\n", dev->dma_left);

    if (!scsi_device_present(sd)) {
	esp_log("ESP SCSI no devices on ID %d, LUN %d\n", dev->id, dev->cmdbuf[8] >> 5);
        /* No such drive */
        dev->rregs[ESP_RSTAT] = 0;
        dev->rregs[ESP_RINTR] = INTR_DC;
        dev->rregs[ESP_RSEQ] = SEQ_0;
	esp_raise_irq(dev);
	return;
    } else {
	esp_log("ESP SCSI device found on ID %d, LUN %d\n", dev->id, dev->cmdbuf[8] >> 5);
    }
    
    count = tdbc = dev->dma_left;
    
    if (dev->xfer_counter == 0) {
	/* Wait until data is available.  */
	esp_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: DMA no data available\n", dev->id, dev->cmdbuf[8] >> 5, dev->cmdbuf[7]);
	return;
    }

    esp_log("ESP SCSI dmaleft = %d, async_len = %i, buffer length = %d\n", dev->dma_counter, dev->async_len, sd->buffer_length);

    /* Make sure count is never bigger than buffer_length. */
    if (count > dev->xfer_counter)
	count = dev->xfer_counter;

    if (dev->do_cmd) {
	esp_log("ESP Command on DMA\n");
	esp_pci_dma_memory_rw(dev, &dev->cmdbuf[dev->cmdlen], count, 1);
	dev->ti_size = 0;
	dev->cmdlen = 0;
	dev->do_cmd = 0;
	esp_do_cmd(dev, dev->cmdbuf);
	return;
    }

    if (sd->phase == SCSI_PHASE_DATA_IN) {
	esp_log("ESP SCSI Read, dma cnt = %i, ti size = %i, positive len = %i\n", dev->dma_counter, dev->ti_size, count);
	esp_pci_dma_memory_rw(dev, sd->sc->temp_buffer + dev->buffer_pos, count, 1);
    } else if (sd->phase == SCSI_PHASE_DATA_OUT) {
	esp_log("ESP SCSI Write, negative len = %i, ti size = %i, dma cnt = %i\n", count, -dev->ti_size, dev->dma_counter);
	esp_pci_dma_memory_rw(dev, sd->sc->temp_buffer + dev->buffer_pos, count, 0);    
    }

    dev->dma_left -= count;
    dev->buffer_pos += count;
    dev->xfer_counter -= count;
    if (sd->phase == SCSI_PHASE_DATA_IN) {
	dev->ti_size -= count;
    } else if (sd->phase == SCSI_PHASE_DATA_OUT) {
	dev->ti_size += count;
    }
    
    esp_log("ESP SCSI Transfer bytes = %d\n", dev->xfer_counter);
    if (dev->xfer_counter <= 0) {
	if (sd->phase == SCSI_PHASE_DATA_OUT) {
	    if (dev->ti_size < 0) {
		esp_log("ESP SCSI Keep writing\n");
		esp_do_dma(dev, sd);	    
	    } else {
		esp_log("ESP SCSI Write finished\n");
		scsi_device_command_phase1(sd);
		esp_pci_command_complete(dev, sd->status);
	    }
	} else if (sd->phase == SCSI_PHASE_DATA_IN) {
	    /* If there is still data to be read from the device then
	       complete the DMA operation immediately.  Otherwise defer
	       until the scsi layer has completed.  */
	    if (dev->ti_size <= 0) {
		esp_log("ESP SCSI Read finished\n");
		scsi_device_command_phase1(sd);
		esp_pci_command_complete(dev, sd->status);
	    } else {
		esp_log("ESP SCSI Keep reading\n");
		esp_do_dma(dev, sd);
	    }
	}
    } else {
	/* Partially filled a scsi buffer. Complete immediately.  */
	esp_log("ESP SCSI Partially filled the SCSI buffer\n");
	esp_dma_done(dev);
    }
}


static void
esp_report_command_complete(esp_t *dev, uint32_t status)
{
    esp_log("ESP Command complete\n");
    dev->ti_size = 0;
    dev->dma_counter = 0;
    dev->status = status;
    dev->rregs[ESP_RSTAT] = STAT_ST;
    esp_dma_done(dev);
}

/* Callback to indicate that the SCSI layer has completed a command.  */
static void
esp_command_complete(void *priv, uint32_t status)
{
    esp_t *dev = (esp_t *)priv;
    
    if (dev->rregs[ESP_RSTAT] & STAT_INT) {
        /* Defer handling command complete until the previous
         * interrupt has been handled.
         */
	esp_log("ESP Deferred status\n");
	dev->deferred_status = status;
	dev->deferred_complete = 1;
	return;
    }
    esp_report_command_complete(dev, status);
}

static void
esp_pci_command_complete(void *priv, uint32_t status)
{
    esp_t *dev = (esp_t *)priv;
    
    esp_command_complete(dev, status);
    dev->dma_regs[DMA_WBC] = 0;
    dev->dma_regs[DMA_STAT] |= DMA_STAT_DONE;
}

static void
esp_timer_on(esp_t *dev, scsi_device_t *sd, double p)
{
    /* Fast SCSI: 10000000 bytes per second */
    dev->period = (p > 0.0) ? p : (((double) sd->buffer_length) * 0.1);

    timer_on_auto(&dev->timer, dev->period + 40.0);
}

static void
handle_ti(void *priv)
{
    esp_t *dev = (esp_t *)priv;
    scsi_device_t *sd = &scsi_devices[dev->bus][dev->id];
    uint32_t dmalen;

    if (dev->dma) {
        dmalen = dev->rregs[ESP_TCLO];
        dmalen |= dev->rregs[ESP_TCMID] << 8;
        dmalen |= dev->rregs[ESP_TCHI] << 16;
	
	dev->dma_counter = dmalen;
	dev->dma_left = dmalen;

	esp_log("ESP Handle TI, do data, minlen = %i, tclo = %02x, tcmid = %02x, tchi = %02x\n", dev->dma_counter, dev->rregs[ESP_TCLO], dev->rregs[ESP_TCMID], dev->rregs[ESP_TCHI]);
	esp_do_dma(dev, sd);
    } else if (dev->do_cmd) {
	dev->ti_size = 0;
	dev->cmdlen = 0;
	dev->do_cmd = 0;
	esp_log("ESP Handle TI, do cmd, CDB[1] = 0x%02x\n", dev->cmdbuf[8]);
	esp_do_cmd(dev, dev->cmdbuf);
    }
}

static void
handle_s_without_atn(void *priv)
{
    esp_t *dev = (esp_t *)priv;
    uint8_t buf[32];
    int len;

    len = esp_get_cmd(dev, buf, sizeof(buf));
    esp_log("ESP SEL w/o ATN len = %d, id = %d\n", len, dev->id);
    if (len) {
	esp_do_busid_cmd(dev, buf, 0);
    }
}

static void
handle_satn(void *priv)
{
    esp_t *dev = (esp_t *)priv;
    uint8_t buf[32];
    int len;

    len = esp_get_cmd(dev, buf, sizeof(buf));
    esp_log("ESP SEL with ATN len = %d, id = %d\n", len, dev->id);
    if (len) {
	esp_do_cmd(dev, buf);
    }
}

static void
handle_satn_stop(void *priv)
{
    esp_t *dev = (esp_t *)priv;

    dev->cmdlen = esp_get_cmd(dev, dev->cmdbuf, sizeof(dev->cmdbuf));
    if (dev->cmdlen) {
	dev->do_cmd = 1;
	dev->rregs[ESP_RSTAT] = STAT_TC | STAT_CD;
	dev->rregs[ESP_RINTR] = INTR_BS | INTR_FC;
	dev->rregs[ESP_RSEQ] = SEQ_CD;
	esp_log("ESP SCSI Command len = %d, raising IRQ\n", dev->cmdlen);
	esp_raise_irq(dev);
    }
}

static void
esp_write_response(esp_t *dev)
{
    dev->ti_buf[0] = dev->status;
    dev->ti_buf[1] = 0;
    if (dev->dma) {
	esp_pci_dma_memory_rw(dev, dev->ti_buf, 2, 0);
	dev->rregs[ESP_RSTAT] = STAT_TC | STAT_ST;
	dev->rregs[ESP_RINTR] = INTR_BS | INTR_FC;
	dev->rregs[ESP_RSEQ] = SEQ_CD;
    } else {
	dev->ti_size = 2;
	dev->ti_rptr = 0;
	dev->ti_wptr = 2;
	dev->rregs[ESP_RFLAGS] = 2;
    }
    esp_log("ESP SCSI ICCS IRQ\n");
    esp_raise_irq(dev);	
}

static void
esp_callback(void *p)
{
    esp_t *dev = (esp_t *) p;

    if (dev->dma_enabled || dev->do_cmd) {
	if ((dev->rregs[ESP_CMD] & CMD_CMD) == CMD_TI) {
		esp_log("ESP SCSI Handle TI Callback\n");
		handle_ti(dev);
	}
    }
    esp_log("ESP DMA activated = %d, CMD activated = %d\n", dev->dma_enabled, dev->do_cmd);
}

static uint32_t
esp_reg_read(esp_t *dev, uint32_t saddr)
{
    uint32_t old_val;

    switch (saddr) {
	case ESP_FIFO:
		esp_log("ESP FIFO decrease = %d, readsize = %d, writesize = %d\n", dev->ti_size, dev->ti_rptr, dev->ti_wptr);
		if (dev->ti_rptr < dev->ti_wptr) {
			dev->ti_size--;
			dev->rregs[ESP_FIFO] = dev->ti_buf[dev->ti_rptr++];
		}
		if (dev->ti_rptr == dev->ti_wptr) {
			dev->ti_rptr = 0;
			dev->ti_wptr = 0;
		}
		break;
	case ESP_RINTR:
		/* Clear sequence step, interrupt register and all status bits
		except TC */
		old_val = dev->rregs[ESP_RINTR];
		dev->rregs[ESP_RINTR] = 0;
		dev->rregs[ESP_RSTAT] &= ~STAT_TC;
		dev->rregs[ESP_RSEQ] = SEQ_CD;
		esp_log("ESP SCSI Clear sequence step\n");
		esp_lower_irq(dev);
		if (dev->deferred_complete) {
			esp_report_command_complete(dev, dev->deferred_status);
			dev->deferred_complete = 0;
		}
		esp_log("ESP RINTR read old val = %02x\n", old_val);
		return old_val;
	case ESP_TCHI:
		/* Return the unique id if the value has never been written */
		if (!dev->tchi_written) {
			esp_log("ESP TCHI read id 0x12\n");
			return TCHI_AM53C974;
		}
	default:
		break;
		
    }
    esp_log("Read reg %02x = %02x\n", saddr, dev->rregs[saddr]);
    return dev->rregs[saddr];
}


static void
esp_reg_write(esp_t *dev, uint32_t saddr, uint32_t val)
{
    esp_log("Write reg %02x = %02x\n", saddr, val);

    switch (saddr) {
	case ESP_TCHI:
		dev->tchi_written = 1;
		/* fall through */
	case ESP_TCLO:
	case ESP_TCMID:
		esp_log("Transfer count regs %02x = %02x\n", saddr, val);
		dev->rregs[ESP_RSTAT] &= ~STAT_TC;
		break;
	case ESP_FIFO:
		if (dev->do_cmd) {
			if (dev->cmdlen < ESP_CMDBUF_SZ) {
				dev->cmdbuf[dev->cmdlen++] = val & 0xff;
				esp_log("ESP CmdBuf Write len = %d, = %02x\n", dev->cmdlen, val & 0xff);
			}
		} else {
			esp_log("ESP FIFO write = %02x\n", val & 0xff);
			dev->ti_size++;
			dev->ti_buf[dev->ti_wptr++] = val & 0xff;
		}
		break;
	case ESP_CMD:
		dev->rregs[saddr] = val;

		if (val & CMD_DMA) {
			dev->dma = 1;
			/* Reload DMA counter.*/
			dev->rregs[ESP_TCLO] = dev->wregs[ESP_TCLO];
			dev->rregs[ESP_TCMID] = dev->wregs[ESP_TCMID];
			dev->rregs[ESP_TCHI] = dev->wregs[ESP_TCHI];
			esp_log("ESP Command for DMA, wregs: TCLO = %02x, TCMID = %02x, TCHI = %02x\n", dev->wregs[ESP_TCLO], 
				dev->wregs[ESP_TCMID], dev->wregs[ESP_TCHI]);
		} else {
			dev->dma = 0;
			esp_log("ESP Command not for DMA\n");
		}
		esp_log("ESP Command = %02x, DMA ena1 = %d, DMA ena2 = %d\n", val & CMD_CMD, dev->dma, dev->dma_enabled);
		switch (val & CMD_CMD) {
			case CMD_NOP:
				break;
			case CMD_FLUSH:
				dev->rregs[ESP_RINTR] = INTR_FC;
				dev->rregs[ESP_RSEQ] = 0;
				dev->rregs[ESP_RFLAGS] = 0;
				timer_on_auto(&dev->timer, 10.0);
				break;
			case CMD_RESET:
				esp_pci_soft_reset(dev);
				break;
			case CMD_BUSRESET:
				dev->rregs[ESP_RINTR] = INTR_RST;
				if (!(dev->wregs[ESP_CFG1] & CFG1_RESREPT)) {
				    esp_log("ESP Bus Reset with IRQ\n");				    
				    esp_raise_irq(dev);
				}
				break;
			case CMD_SEL:
				handle_s_without_atn(dev);
				break;
			case CMD_SELATN:
				handle_satn(dev);
				break;
			case CMD_SELATNS:
				handle_satn_stop(dev);
				break;
			case CMD_ICCS:
				esp_write_response(dev);
				dev->rregs[ESP_RINTR] = INTR_FC;
				dev->rregs[ESP_RSTAT] |= STAT_MI;
				break;
			case CMD_MSGACC:
				dev->rregs[ESP_RINTR] = INTR_DC;
				dev->rregs[ESP_RSEQ] = 0;
				dev->rregs[ESP_RFLAGS] = 0;
				esp_log("ESP SCSI MSGACC IRQ\n");
				esp_raise_irq(dev);
				break;
			case CMD_PAD:
				dev->rregs[ESP_RSTAT] = STAT_TC;
				dev->rregs[ESP_RINTR] = INTR_FC;
				dev->rregs[ESP_RSEQ] = 0;
				esp_log("ESP Transfer Pad\n");
				break;
			case CMD_SATN:
			case CMD_RSTATN:
				break;
			case CMD_ENSEL:
				dev->rregs[ESP_RINTR] = 0;
				esp_log("ESP Enable Selection, do cmd = %d\n", dev->do_cmd);
				break;
			case CMD_DISSEL:
				dev->rregs[ESP_RINTR] = 0;
				esp_log("ESP Disable Selection\n");
				esp_raise_irq(dev);
				break;
		}
		break;
	case ESP_WBUSID:
	case ESP_WSEL:
	case ESP_WSYNTP:
	case ESP_WSYNO:
		break;
	case ESP_CFG1:
	case ESP_CFG2:
	case ESP_CFG3:
	case ESP_RES3:
	case ESP_RES4:
		dev->rregs[saddr] = val;
		break;
	case ESP_WCCF:
	case ESP_WTEST:
		break;
	default:
		esp_log("Unhandled writeb 0x%x = 0x%x\n", saddr, val);
		break;
    }
    dev->wregs[saddr] = val;
}


static void
esp_pci_dma_memory_rw(esp_t *dev, uint8_t *buf, uint32_t len, int dir)
{
    int expected_dir;
    
    if (dev->dma_regs[DMA_CMD] & DMA_CMD_DIR)
	expected_dir = 1;
    else
	expected_dir = 0;

    if (dir != expected_dir) {
	esp_log("ESP unexpected direction\n");
	return;
    }
    
    esp_log("ESP DMA WBC = %d, addr = %06x, dir = %d\n", dev->dma_regs[DMA_WBC], dev->dma_regs[DMA_SPA], expected_dir);

    if (dev->dma_regs[DMA_WBC] < len)
	len = dev->dma_regs[DMA_WBC];

    if (expected_dir) {
	dma_bm_write(dev->dma_regs[DMA_SPA], buf, len, 4);
    } else {
	dma_bm_read(dev->dma_regs[DMA_SPA], buf, len, 4);
    }

    /* update status registers */
    dev->dma_regs[DMA_WBC] -= len;
    dev->dma_regs[DMA_WAC] += len;
    if (dev->dma_regs[DMA_WBC] == 0)
	dev->dma_regs[DMA_STAT] |= DMA_STAT_DONE;
}	

static uint32_t
esp_pci_dma_read(esp_t *dev, uint16_t saddr) 
{
    uint32_t ret;
    
    ret = dev->dma_regs[saddr];
    
    if (saddr == DMA_STAT) {
	if (dev->rregs[ESP_RSTAT] & STAT_INT) {
	    ret |= DMA_STAT_SCSIINT;
	    esp_log("ESP PCI DMA Read SCSI interrupt issued\n");
	}
        if (!(dev->sbac & SBAC_STATUS)) {
	    dev->dma_regs[DMA_STAT] &= ~(DMA_STAT_ERROR | DMA_STAT_ABORT |
					DMA_STAT_DONE);
	    esp_log("ESP PCI DMA Read done cleared\n");
	}
    }
    
    esp_log("ESP PCI DMA Read regs addr = %04x, temp = %06x\n", saddr, ret);
    return ret;
}

static void
esp_pci_dma_write(esp_t *dev, uint16_t saddr, uint32_t val)
{
    uint32_t mask;	
	
    switch (saddr) {
	case DMA_CMD:
	    dev->dma_regs[saddr] = val;
	    esp_log("ESP PCI DMA Write CMD = %02x\n", val & DMA_CMD_MASK);
	    switch (val & DMA_CMD_MASK) {
		case 0: /*IDLE*/
		    esp_dma_enable(dev, 0);
		    break;
		case 1: /*BLAST*/
		    break;
		case 2: /*ABORT*/
		    break;
		case 3: /*START*/
		    dev->dma_regs[DMA_WBC] = dev->dma_regs[DMA_STC];
		    dev->dma_regs[DMA_WAC] = dev->dma_regs[DMA_SPA];
		    dev->dma_regs[DMA_WMAC] = dev->dma_regs[DMA_SMDLA];
		    dev->dma_regs[DMA_STAT] &= ~(DMA_STAT_BCMBLT | DMA_STAT_SCSIINT |
						DMA_STAT_DONE | DMA_STAT_ABORT |
						DMA_STAT_ERROR | DMA_STAT_PWDN);
		    esp_dma_enable(dev, 1);
		    break;
		default: /* can't happen */
		    abort();
	    }
	    break;
	case DMA_STC:
	case DMA_SPA:
	case DMA_SMDLA:
	    dev->dma_regs[saddr] = val;
	    break;
	case DMA_STAT:
	    if (dev->sbac & SBAC_STATUS) {
	        /* clear some bits on write */
		mask = DMA_STAT_ERROR | DMA_STAT_ABORT | DMA_STAT_DONE;
		dev->dma_regs[DMA_STAT] &= ~(val & mask);
	    }
	    break;
    }
}

static void
esp_pci_soft_reset(esp_t *dev)
{
    esp_irq(dev, 0);
    esp_pci_hard_reset(dev);
}

static void
esp_pci_hard_reset(esp_t *dev)
{
    esp_hard_reset(dev);
    dev->dma_regs[DMA_CMD] &= ~(DMA_CMD_DIR | DMA_CMD_INTE_D | DMA_CMD_INTE_P
                              | DMA_CMD_MDL | DMA_CMD_DIAG | DMA_CMD_MASK);
    dev->dma_regs[DMA_WBC] &= ~0xffff;
    dev->dma_regs[DMA_WAC] = 0xffffffff;
    dev->dma_regs[DMA_STAT] &= ~(DMA_STAT_BCMBLT | DMA_STAT_SCSIINT
                               | DMA_STAT_DONE | DMA_STAT_ABORT
                               | DMA_STAT_ERROR);
    dev->dma_regs[DMA_WMAC] = 0xfffffffd;
}

static uint32_t
esp_io_pci_read(esp_t *dev, uint32_t addr, unsigned int size) 
{
    uint32_t ret;
    
    addr &= 0x7f;
    
    if (addr < 0x40) {
	/* SCSI core reg */
	ret = esp_reg_read(dev, addr >> 2);
    } else if (addr < 0x60) {
	/* PCI DMA CCB */
	ret = esp_pci_dma_read(dev, (addr - 0x40) >> 2);
	esp_log("ESP PCI DMA CCB read addr = %02x, ret = %02x\n", (addr - 0x40) >> 2, ret);
    } else if (addr == 0x70) {
	/* DMA SCSI Bus and control */
	ret = dev->sbac;
	esp_log("ESP PCI SBAC read = %02x\n", ret);
    } else {
	/* Invalid region */
	ret = 0;
    }
    
    /* give only requested data */
    ret >>= (addr & 3) * 8;
    ret &= ~(~(uint64_t)0 << (8 * size));    
    
    esp_log("ESP PCI I/O read: addr = %02x, val = %02x\n", addr, ret);
    return ret;
}

static void
esp_io_pci_write(esp_t *dev, uint32_t addr, uint32_t val, unsigned int size) 
{ 
    uint32_t current, mask;
    int shift;

    addr &= 0x7f;

    if (size < 4 || addr & 3) {
        /* need to upgrade request: we only support 4-bytes accesses */
        current = 0;

        if (addr < 0x40) {
            current = dev->wregs[addr >> 2];
        } else if (addr < 0x60) {
            current = dev->dma_regs[(addr - 0x40) >> 2];
        } else if (addr < 0x74) {
            current = dev->sbac;
        }

        shift = (4 - size) * 8;
        mask = (~(uint32_t)0 << shift) >> shift;

        shift = ((4 - (addr & 3)) & 3) * 8;
        val <<= shift;
        val |= current & ~(mask << shift);
        addr &= ~3;
        size = 4;
    }

    esp_log("ESP PCI I/O write: addr = %02x, val = %02x\n", addr, val);

    if (addr < 0x40) {
	/* SCSI core reg */
	esp_reg_write(dev, addr >> 2, val);
    } else if (addr < 0x60) {
	/* PCI DMA CCB */
	esp_pci_dma_write(dev, (addr - 0x40) >> 2, val);
    } else if (addr == 0x70) {
	/* DMA SCSI Bus and control */
	dev->sbac = val;
    }
}


static void
esp_pci_io_writeb(uint16_t addr, uint8_t val, void *p)
{
    esp_t *dev = (esp_t *)p;
    esp_io_pci_write(dev, addr, val, 1);
}

static void
esp_pci_io_writew(uint16_t addr, uint16_t val, void *p)
{
    esp_t *dev = (esp_t *)p;
    esp_io_pci_write(dev, addr, val, 2);
}

static void
esp_pci_io_writel(uint16_t addr, uint32_t val, void *p)
{
    esp_t *dev = (esp_t *)p;
    esp_io_pci_write(dev, addr, val, 4);
}

static uint8_t
esp_pci_io_readb(uint16_t addr, void *p)
{
    esp_t *dev = (esp_t *)p;
    return esp_io_pci_read(dev, addr, 1);
}

static uint16_t
esp_pci_io_readw(uint16_t addr, void *p)
{
    esp_t *dev = (esp_t *)p;
    return esp_io_pci_read(dev, addr, 2);
}

static uint32_t
esp_pci_io_readl(uint16_t addr, void *p)
{
    esp_t *dev = (esp_t *)p;
    return esp_io_pci_read(dev, addr, 4);
}

static void
esp_io_set(esp_t *dev, uint32_t base, uint16_t len)
{
    esp_log("ESP: [PCI] Setting I/O handler at %04X\n", base);
    io_sethandler(base, len,
		  esp_pci_io_readb, esp_pci_io_readw, esp_pci_io_readl,
		  esp_pci_io_writeb, esp_pci_io_writew, esp_pci_io_writel, dev);
}


static void
esp_io_remove(esp_t *dev, uint32_t base, uint16_t len)
{
    esp_log("ESP: [PCI] Removing I/O handler at %04X\n", base);
    io_removehandler(base, len,
		esp_pci_io_readb, esp_pci_io_readw, esp_pci_io_readl,
		esp_pci_io_writeb, esp_pci_io_writew, esp_pci_io_writel, dev);
}

static void
esp_bios_set_addr(esp_t *dev, uint32_t base)
{
    mem_mapping_set_addr(&dev->bios.mapping, base, 0x8000);
}

static void
esp_bios_disable(esp_t *dev)
{
    mem_mapping_disable(&dev->bios.mapping);
}

#define EE_ADAPT_SCSI_ID 64
#define EE_MODE2         65
#define EE_DELAY         66
#define EE_TAG_CMD_NUM   67
#define EE_ADAPT_OPTIONS 68
#define EE_BOOT_SCSI_ID  69
#define EE_BOOT_SCSI_LUN 70
#define EE_CHKSUM1       126
#define EE_CHKSUM2       127

#define EE_ADAPT_OPTION_F6_F8_AT_BOOT   0x01
#define EE_ADAPT_OPTION_BOOT_FROM_CDROM 0x02
#define EE_ADAPT_OPTION_INT13           0x04
#define EE_ADAPT_OPTION_SCAM_SUPPORT    0x08

/*To do: make this separate from the SCSI card*/
static void
dc390_save_eeprom(esp_t *dev)
{
        FILE *f = nvr_fopen(dev->nvr_path, "wb");
        if (!f) return;
        fwrite(dev->eeprom.data, 1, 128, f);
        fclose(f);
}

static void
dc390_write_eeprom(esp_t *dev, int ena, int clk, int dat)
{
	/*Actual EEPROM is the same as the one used by the ATI cards, the 93cxx series.*/
	ati_eeprom_t *eeprom = &dev->eeprom;
        uint8_t tick = eeprom->count;
	uint8_t eedo = eeprom->out;
	uint16_t address = eeprom->address;
	uint8_t command = eeprom->opcode;
	
	esp_log("EEPROM CS=%02x,SK=%02x,DI=%02x,DO=%02x,tick=%d\n",
		ena, clk, dat, eedo, tick);
		
	if (!eeprom->oldena && ena) {
		esp_log("EEPROM Start chip select cycle\n");
		tick = 0;
		command = 0;
		address = 0;
	} else if (eeprom->oldena && !ena) {
		if (!eeprom->wp) {
			uint8_t subcommand = address >> 4;
			if (command == 0 && subcommand == 2) {
				esp_log("EEPROM Erase All\n");
				for (address = 0; address < 64; address++)
					eeprom->data[address] = 0xffff;
				dc390_save_eeprom(dev);				
			} else if (command == 3) {
				esp_log("EEPROM Erase Word\n");
				eeprom->data[address] = 0xffff;
				dc390_save_eeprom(dev);
			} else if (tick >= 26) {
				if (command == 1) {
					esp_log("EEPROM Write Word\n");
					eeprom->data[address] &= eeprom->dat;
					dc390_save_eeprom(dev);
				} else if (command == 0 && subcommand == 1) {
					esp_log("EEPROM Write All\n");
					for (address = 0; address < 64; address++)
						eeprom->data[address] &= eeprom->dat;
					dc390_save_eeprom(dev);				
				}
			}
		}
		eedo = 1;
		esp_log("EEPROM DO read\n");
	} else if (ena && !eeprom->oldclk && clk) {
		if (tick == 0) {
			if (dat == 0) {
				esp_log("EEPROM Got correct 1st start bit, waiting for 2nd start bit (1)\n");
				tick++;
			} else {
				esp_log("EEPROM Wrong 1st start bit (is 1, should be 0)\n");
				tick = 2;
			}
		} else if (tick == 1) {
			if (dat != 0) {
				esp_log("EEPROM Got correct 2nd start bit, getting command + address\n");
				tick++;
			} else {
				esp_log("EEPROM 1st start bit is longer than needed\n");
			}
		} else if (tick < 4) {
			tick++;
			command <<= 1;
			if (dat)
				command += 1;
		} else if (tick < 10) {
			tick++;
			address = (address << 1) | dat;
			if (tick == 10) {
				esp_log("EEPROM command = %02x, address = %02x (val = %04x)\n", command,
					address, eeprom->data[address]);
				if (command == 2)
					eedo = 0;
				address = address % 64;
				if (command == 0) {
					switch (address >> 4) {
						case 0:
							esp_log("EEPROM Write disable command\n");
							eeprom->wp = 1;
							break;
						case 1:
							esp_log("EEPROM Write all command\n");
							break;
						case 2:
							esp_log("EEPROM Erase all command\n");
							break;
						case 3:
							esp_log("EEPROM Write enable command\n");
							eeprom->wp = 0;
							break;
					}
				} else {
					esp_log("EEPROM Read, write or erase word\n");
					eeprom->dat = eeprom->data[address];
				}
			}
		} else if (tick < 26) {
			tick++;
			if (command == 2) {
				esp_log("EEPROM Read Word\n");
				eedo = ((eeprom->dat & 0x8000) != 0);
			}
			eeprom->dat <<= 1;
			eeprom->dat += dat;
		} else {
			esp_log("EEPROM Additional unneeded tick, not processed\n");
		}
	}
	
	eeprom->count = tick;
	eeprom->oldena = ena;
	eeprom->oldclk = clk;
	eeprom->out = eedo;
	eeprom->address = address;
	eeprom->opcode = command;
	esp_log("EEPROM EEDO = %d\n", eeprom->out);
}

static void
dc390_load_eeprom(esp_t *dev)
{
    ati_eeprom_t *eeprom = &dev->eeprom;
    uint8_t *nvr = (uint8_t *)eeprom->data;
    int i;
    uint16_t checksum = 0;
    FILE *f;

    eeprom->out = 1;

    f = nvr_fopen(dev->nvr_path, "rb");
    if (f) {
	esp_log("EEPROM Load\n");
	if (fread(nvr, 1, 128, f) != 128)
		fatal("dc390_eeprom_load(): Error reading data\n");
	fclose(f);
    } else {
	for (i = 0; i < 16; i++) {
	    nvr[i * 2] = 0x57;
	    nvr[i * 2 + 1] = 0x00;
	}
	
	esp_log("EEPROM Defaults\n");
	
	nvr[EE_ADAPT_SCSI_ID] = 7;
	nvr[EE_MODE2] = 0x0f;
	nvr[EE_TAG_CMD_NUM] = 0x04;
	nvr[EE_ADAPT_OPTIONS] = EE_ADAPT_OPTION_F6_F8_AT_BOOT |
					EE_ADAPT_OPTION_BOOT_FROM_CDROM |
					EE_ADAPT_OPTION_INT13;
	for (i = 0; i < EE_CHKSUM1; i += 2) {
	    checksum += ((nvr[i] & 0xff) | (nvr[i + 1] << 8));
	    esp_log("Checksum calc = %04x, nvr = %02x\n", checksum, nvr[i]);
	}
    
	checksum = 0x1234 - checksum;
	nvr[EE_CHKSUM1] = checksum & 0xff;
	nvr[EE_CHKSUM2] = checksum >> 8;
	esp_log("EEPROM Checksum = %04x\n", checksum);
    }	    
}

uint8_t	esp_pci_regs[256];
bar_t	esp_pci_bar[2];


static uint8_t
esp_pci_read(int func, int addr, void *p)
{
    esp_t *dev = (esp_t *)p;

    esp_log("ESP PCI: Reading register %02X\n", addr & 0xff);

    switch (addr) {
	case 0x00:
		esp_log("ESP PCI: Read DO line = %02x\n", dev->eeprom.out);
		if (!dev->has_bios)
		    return 0x22;
		else {
		    if (dev->eeprom.out)
			return 0x22;
		    else {
		        dev->eeprom.out = 1;
			return 2;
		    }
		}
		break;
	case 0x01:
		return 0x10;
	case 0x02:
		return 0x20;
	case 0x03:
		return 0x20;
	case 0x04:
		return esp_pci_regs[0x04] & 3; /*Respond to IO*/
	case 0x07:
		return 2;
	case 0x08:
		return 0;			/*Revision ID*/
	case 0x09:
		return 0;			/*Programming interface*/
	case 0x0A:
		return 0;			/*devubclass*/
	case 0x0B:
		return 1;			/*Class code*/
	case 0x0E:
		return 0;			/*Header type */
	case 0x10:
		return 1;	/*I/O space*/
	case 0x11:
		return esp_pci_bar[0].addr_regs[1];
	case 0x12:
		return esp_pci_bar[0].addr_regs[2];
	case 0x13:
		return esp_pci_bar[0].addr_regs[3];
	case 0x30:
		if (!dev->has_bios)
			return 0;
		return esp_pci_bar[1].addr_regs[0];
	case 0x31:
		if (!dev->has_bios)
			return 0;
		return esp_pci_bar[1].addr_regs[1];
	case 0x32:
		if (!dev->has_bios)
			return 0;
		return esp_pci_bar[1].addr_regs[2];
	case 0x33:
		if (!dev->has_bios)
			return 0;
		return esp_pci_bar[1].addr_regs[3];
	case 0x3C:
		return dev->irq;
	case 0x3D:
		return PCI_INTA;
    }

    return(0);
}


static void
esp_pci_write(int func, int addr, uint8_t val, void *p)
{
    esp_t *dev = (esp_t *)p;
    uint8_t valxor;
    int eesk;
    int eedi;

    esp_log("ESP PCI: Write value %02X to register %02X\n", val, addr);

    if ((addr >= 0x80) && (addr <= 0xFF)) {
	if (addr == 0x80) {
		eesk = val & 0x80 ? 1 : 0;
		eedi = val & 0x40 ? 1 : 0;
		dc390_write_eeprom(dev, 1, eesk, eedi);
	} else if (addr == 0xc0)
		dc390_write_eeprom(dev, 0, 0, 0);
	esp_log("ESP PCI: Write value %02X to register %02X\n", val, addr);
	return;
    }

    switch (addr) {
	case 0x04:
		valxor = (val & 3) ^ esp_pci_regs[addr];
		if (valxor & PCI_COMMAND_IO) {
			esp_io_remove(dev, dev->PCIBase, 0x80);
			if ((dev->PCIBase != 0) && (val & PCI_COMMAND_IO))
				esp_io_set(dev, dev->PCIBase, 0x80);
		}
		esp_pci_regs[addr] = val & 3;
		break;

	case 0x10: case 0x11: case 0x12: case 0x13:
		/* I/O Base set. */
		/* First, remove the old I/O. */
		esp_io_remove(dev, dev->PCIBase, 0x80);
		/* Then let's set the PCI regs. */
		esp_pci_bar[0].addr_regs[addr & 3] = val;
		/* Then let's calculate the new I/O base. */
		esp_pci_bar[0].addr &= 0xff00;
		dev->PCIBase = esp_pci_bar[0].addr;
		/* Log the new base. */
		esp_log("ESP PCI: New I/O base is %04X\n" , dev->PCIBase);
		/* We're done, so get out of the here. */
		if (esp_pci_regs[4] & PCI_COMMAND_IO) {
			if (dev->PCIBase != 0) {
				esp_io_set(dev, dev->PCIBase, 0x80);
			}
		}
		return;

	case 0x30: case 0x31: case 0x32: case 0x33:
		if (!dev->has_bios)
			return;
		/* BIOS Base set. */
		/* First, remove the old I/O. */
		esp_bios_disable(dev);
		/* Then let's set the PCI regs. */
		esp_pci_bar[1].addr_regs[addr & 3] = val;
		/* Then let's calculate the new I/O base. */
		esp_pci_bar[1].addr &= 0xfff80001;
		dev->BIOSBase = esp_pci_bar[1].addr & 0xfff80000;
		/* Log the new base. */
		esp_log("ESP PCI: New BIOS base is %08X\n" , dev->BIOSBase);
		/* We're done, so get out of the here. */
		if (esp_pci_bar[1].addr & 0x00000001)
			esp_bios_set_addr(dev, dev->BIOSBase);
		return;

	case 0x3C:
		esp_pci_regs[addr] = val;
		dev->irq = val;
		esp_log("ESP IRQ now: %i\n", val);
		return;
    }
}

static void *
dc390_init(const device_t *info)
{
    esp_t *dev;

    dev = malloc(sizeof(esp_t));
    memset(dev, 0x00, sizeof(esp_t));

    dev->bus = scsi_get_bus();

    dev->PCIBase = 0;
    dev->MMIOBase = 0;

    dev->pci_slot = pci_add_card(PCI_ADD_NORMAL, esp_pci_read, esp_pci_write, dev);

    esp_pci_bar[0].addr_regs[0] = 1;
    esp_pci_regs[0x04] = 3;

    dev->has_bios = device_get_config_int("bios");
    if (dev->has_bios)
	rom_init(&dev->bios, DC390_ROM, 0xc8000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    /* Enable our BIOS space in PCI, if needed. */
    if (dev->has_bios) {
 	esp_pci_bar[1].addr = 0xfff80000;
    } else {
	esp_pci_bar[1].addr = 0;
    }

    if (dev->has_bios)
	esp_bios_disable(dev);

    dev->nvr_path = "dc390.nvr";

    /* Load the serial EEPROM. */
    dc390_load_eeprom(dev);

    esp_pci_hard_reset(dev);

    timer_add(&dev->timer, esp_callback, dev, 0);

    return(dev);
}


static void 
esp_close(void *priv)
{
    esp_t *dev = (esp_t *)priv;

    if (dev) {
	free(dev);
	dev = NULL;
    }
}


static const device_config_t dc390_pci_config[] = {
	{
		"bios", "Enable BIOS", CONFIG_BINARY, "", 0
	},
	{
		"", "", -1
	}
};


const device_t dc390_pci_device =
{
    "Tekram DC-390 PCI",
    DEVICE_PCI,
    0,
    dc390_init, esp_close, NULL,
    { NULL }, NULL, NULL,
    dc390_pci_config
};
