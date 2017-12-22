/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the NCR 53C810 SCSI Host Adapter made by
 *		NCR and later Symbios and LSI. This controller was designed
 *		for the PCI bus.
 *
 * Version:	@(#)scsi_ncr53c810.c	1.0.4	2017/12/22
 *
 * Authors:	Paul Brook (QEMU)
 *		Artyom Tarasenko (QEMU)
 *		TheCollector1995, <mariogplayer@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2006-2017 Paul Brook.
 *		Copyright 2009-2017 Artyom Tarasenko.
 *		Copyright 2017 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../io.h"
#include "../dma.h"
#include "../pic.h"
#include "../mem.h"
#include "../rom.h"
#include "../pci.h"
#include "../nvr.h"
#include "../device.h"
#include "../timer.h"
#include "../plat.h"
#include "scsi.h"
#include "scsi_device.h"
#include "scsi_ncr53c810.h"

#define NCR_BUF_SIZE	  4096

#define NCR_SCNTL0_TRG    0x01
#define NCR_SCNTL0_AAP    0x02
#define NCR_SCNTL0_EPC    0x08
#define NCR_SCNTL0_WATN   0x10
#define NCR_SCNTL0_START  0x20

#define NCR_SCNTL1_SST    0x01
#define NCR_SCNTL1_IARB   0x02
#define NCR_SCNTL1_AESP   0x04
#define NCR_SCNTL1_RST    0x08
#define NCR_SCNTL1_CON    0x10
#define NCR_SCNTL1_DHP    0x20
#define NCR_SCNTL1_ADB    0x40
#define NCR_SCNTL1_EXC    0x80

#define NCR_SCNTL2_WSR    0x01
#define NCR_SCNTL2_VUE0   0x02
#define NCR_SCNTL2_VUE1   0x04
#define NCR_SCNTL2_WSS    0x08
#define NCR_SCNTL2_SLPHBEN 0x10
#define NCR_SCNTL2_SLPMD  0x20
#define NCR_SCNTL2_CHM    0x40
#define NCR_SCNTL2_SDU    0x80

#define NCR_ISTAT_DIP     0x01
#define NCR_ISTAT_SIP     0x02
#define NCR_ISTAT_INTF    0x04
#define NCR_ISTAT_CON     0x08
#define NCR_ISTAT_SEM     0x10
#define NCR_ISTAT_SIGP    0x20
#define NCR_ISTAT_SRST    0x40
#define NCR_ISTAT_ABRT    0x80

#define NCR_SSTAT0_SDP0   0x01
#define NCR_SSTAT0_RST    0x02
#define NCR_SSTAT0_WOA    0x04
#define NCR_SSTAT0_LOA    0x08
#define NCR_SSTAT0_AIP    0x10
#define NCR_SSTAT0_OLF    0x20
#define NCR_SSTAT0_ORF    0x40
#define NCR_SSTAT0_ILF    0x80

#define NCR_SIST0_PAR     0x01
#define NCR_SIST0_RST     0x02
#define NCR_SIST0_UDC     0x04
#define NCR_SIST0_SGE     0x08
#define NCR_SIST0_RSL     0x10
#define NCR_SIST0_SEL     0x20
#define NCR_SIST0_CMP     0x40
#define NCR_SIST0_MA      0x80

#define NCR_SIST1_HTH     0x01
#define NCR_SIST1_GEN     0x02
#define NCR_SIST1_STO     0x04
#define NCR_SIST1_SBMC    0x10

#define NCR_SOCL_IO       0x01
#define NCR_SOCL_CD       0x02
#define NCR_SOCL_MSG      0x04
#define NCR_SOCL_ATN      0x08
#define NCR_SOCL_SEL      0x10
#define NCR_SOCL_BSY      0x20
#define NCR_SOCL_ACK      0x40
#define NCR_SOCL_REQ      0x80

#define NCR_DSTAT_IID     0x01
#define NCR_DSTAT_SIR     0x04
#define NCR_DSTAT_SSI     0x08
#define NCR_DSTAT_ABRT    0x10
#define NCR_DSTAT_BF      0x20
#define NCR_DSTAT_MDPE    0x40
#define NCR_DSTAT_DFE     0x80

#define NCR_DCNTL_COM     0x01
#define NCR_DCNTL_IRQD    0x02
#define NCR_DCNTL_STD     0x04
#define NCR_DCNTL_IRQM    0x08
#define NCR_DCNTL_SSM     0x10
#define NCR_DCNTL_PFEN    0x20
#define NCR_DCNTL_PFF     0x40
#define NCR_DCNTL_CLSE    0x80

#define NCR_DMODE_MAN     0x01
#define NCR_DMODE_BOF     0x02
#define NCR_DMODE_ERMP    0x04
#define NCR_DMODE_ERL     0x08
#define NCR_DMODE_DIOM    0x10
#define NCR_DMODE_SIOM    0x20

#define NCR_CTEST2_DACK   0x01
#define NCR_CTEST2_DREQ   0x02
#define NCR_CTEST2_TEOP   0x04
#define NCR_CTEST2_PCICIE 0x08
#define NCR_CTEST2_CM     0x10
#define NCR_CTEST2_CIO    0x20
#define NCR_CTEST2_SIGP   0x40
#define NCR_CTEST2_DDIR   0x80

#define NCR_CTEST5_BL2    0x04
#define NCR_CTEST5_DDIR   0x08
#define NCR_CTEST5_MASR   0x10
#define NCR_CTEST5_DFSN   0x20
#define NCR_CTEST5_BBCK   0x40
#define NCR_CTEST5_ADCK   0x80

/* Enable Response to Reselection */
#define NCR_SCID_RRE      0x60

#define NCR_SOCL_IO       0x01
#define NCR_SOCL_CD       0x02
#define NCR_SOCL_MSG      0x04

#define PHASE_DO          0	/* N/A, N/A, N/A */
#define PHASE_DI          1	/* N/A, N/A, I/O */
#define PHASE_CMD         2	/* N/A, C/D, N/A */
#define PHASE_ST          3	/* N/A, C/D, I/O */
#define PHASE_UNK	  4	/* MSG, N/A, N/A */
#define PHASE_MO          6	/* MSG, C/D, N/A */
#define PHASE_MI          7	/* MSG, C/D, I/O */
#define PHASE_MASK        7

/* Maximum length of MSG IN data.  */
#define NCR_MAX_MSGIN_LEN 8

/* Flag set if this is a tagged command.  */
#define NCR_TAG_VALID     (1 << 16)

typedef struct ncr53c810_request {
    uint32_t tag;
    uint32_t dma_len;
    uint8_t *dma_buf;
    uint32_t pending;
    int out;
    uint8_t connected;
} ncr53c810_request;

typedef enum
{
    SCSI_STATE_SEND_COMMAND,
    SCSI_STATE_READ_DATA,
    SCSI_STATE_WRITE_DATA,
    SCSI_STATE_READ_STATUS,
    SCSI_STATE_READ_MESSAGE,
    SCSI_STATE_WRITE_MESSAGE
} scsi_state_t;

typedef union {
	uint32_t dw;
	uint8_t b[4];
} dword_t;

typedef struct {
    uint8_t	pci_slot;
    int		PCIBase;
    int		MMIOBase;
    mem_mapping_t mmio_mapping;
    int		RAMBase;
    mem_mapping_t ram_mapping;

    int carry; /* ??? Should this be an a visible register somewhere?  */
    int status;
    /* Action to take at the end of a MSG IN phase.
       0 = COMMAND, 1 = disconnect, 2 = DATA OUT, 3 = DATA IN.  */
    volatile int msg_action;
    volatile int msg_len;
    uint8_t msg[NCR_MAX_MSGIN_LEN];
    /* 0 if SCRIPTS are running or stopped.
     * 1 if a Wait Reselect instruction has been issued.
     * 2 if processing DMA from ncr53c810_execute_script.
     * 3 if a DMA operation is in progress.  */
    volatile uint8_t waiting, sstop;

    volatile uint8_t current_lun;
    volatile int command_complete;
    ncr53c810_request requests[7];

    int irq;
	
    volatile dword_t dsa;
    volatile dword_t temp;
    volatile dword_t dnad;
    volatile dword_t dbc;
    volatile uint8_t istat;
    volatile uint8_t dcmd;
    volatile uint8_t dstat;
    volatile uint8_t dien;
    volatile uint8_t sist[2];
    volatile uint8_t sien[2];
    uint8_t mbox0;
    uint8_t mbox1;
    volatile uint8_t dfifo;
    uint8_t ctest2;
    uint8_t ctest3;
    uint8_t ctest4;
    uint8_t ctest5;
    volatile dword_t dsp;
    volatile dword_t dsps;
    volatile uint8_t dmode;
    volatile uint8_t dcntl;
    uint8_t scntl0;
    uint8_t scntl1;
    uint8_t scntl2;
    uint8_t scntl3;
    uint8_t sstat0;
    uint8_t sstat1;
    uint8_t scid;
    uint8_t sxfer;
    volatile uint8_t socl;
    volatile uint8_t sdid;
    volatile uint8_t ssid;
    volatile uint8_t sfbr;
    uint8_t stest[3];
    volatile uint8_t sidl;
    uint8_t stime0;
    uint8_t respid;
    dword_t scratcha; /* SCRATCHA */
    volatile dword_t adder;
    dword_t scratchb; /* SCRATCHB */
    volatile uint8_t sbr;
    uint8_t chip_rev;
    volatile int last_level;
    volatile uint8_t gpreg0;
    volatile uint32_t buffer_pos;
    volatile int32_t temp_buf_len;
    volatile uint8_t last_command;

    volatile uint8_t ncr_to_ncr;
    volatile uint8_t in_req;
} ncr53c810c_state_t;


static volatile
thread_t	*poll_tid;
static volatile
int		busy;

static volatile
event_t		*evt;
static volatile
event_t		*wait_evt;

static volatile
event_t		*wake_poll_thread;
static volatile
event_t		*thread_started;

static volatile
ncr53c810c_state_t	*ncr53c810_dev;


#ifdef ENABLE_NCR53C810_LOG
int ncr53c810_do_log = ENABLE_NCR53C810_LOG;
#endif


static void
ncr53c810_log(const char *fmt, ...)
{
#ifdef ENABLE_NCR53C810_LOG
    va_list ap;

    if (ncr53c810_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
#endif
}

static uint8_t ncr53c810_reg_readb(ncr53c810c_state_t *dev, uint32_t offset);
static void ncr53c810_reg_writeb(ncr53c810c_state_t *dev, uint32_t offset, uint8_t val);

static __inline__ int32_t
sextract32(uint32_t value, int start, int length)
{
    /* Note that this implementation relies on right shift of signed
     * integers being an arithmetic shift.
     */
    return ((int32_t)(value << (32 - length - start))) >> (32 - length);
}


static __inline__ uint32_t
deposit32(uint32_t value, int start, int length, uint32_t fieldval)
{
    uint32_t mask;
    mask = (~0U >> (32 - length)) << start;
    return (value & ~mask) | ((fieldval << start) & mask);
}


static void
ncr53c810_soft_reset(ncr53c810c_state_t *dev)
{
    ncr53c810_log("NCR 810: Soft Reset\n");
    dev->carry = 0;

    dev->msg_action = 0;
    dev->msg_len = 0;
    dev->waiting = 0;
    dev->dsa.dw = 0;
    dev->temp.dw = 0;
    dev->dnad.dw = 0;
    dev->dbc.dw = 0;
    dev->dsp.dw = 0;
    dev->dsps.dw = 0;
    dev->scratcha.dw = 0;
    dev->adder.dw = 0;
    dev->scratchb.dw = 0;

    dev->istat = 0;
    dev->dcmd = 0x40;
    dev->dstat = NCR_DSTAT_DFE;
    dev->dien = 0;
    memset((void *) dev->sist, 0, 2);
    memset((void *) dev->sien, 0, 2);
    dev->mbox0 = 0;
    dev->mbox1 = 0;
    dev->dfifo = 0;
    dev->ctest2 = NCR_CTEST2_DACK;
    dev->ctest3 = 0;
    dev->ctest4 = 0;
    dev->ctest5 = 0;
    dev->dmode = 0;
    dev->dcntl = 0;
    dev->scntl0 = 0xc0;
    dev->scntl1 = 0;
    dev->scntl2 = 0;
    dev->scntl3 = 0;
    dev->sstat0 = 0;
    dev->sstat1 = 0;
    dev->scid = 7;
    dev->sxfer = 0;
    dev->socl = 0;
    dev->sdid = 0;
    dev->ssid = 0;
    memset(dev->stest, 0, 3);
    dev->sidl = 0;
    dev->stime0 = 0;
    dev->respid = 0x80;
    dev->sbr = 0;
    dev->last_level = 0;
    dev->gpreg0 = 0;
    dev->sstop = 1;
    dev->ncr_to_ncr = 0;
}


static void
ncr53c810_read(ncr53c810c_state_t *dev, uint32_t addr, uint8_t *buf, uint32_t len, int wait)
{
    int i = 0;

#if 0
    ncr53c810_log("ncr53c810_read(): %08X-%08X, length %i\n", addr, (addr + len - 1), len);
#endif

    dev->ncr_to_ncr = wait;
    if (dev->dmode & NCR_DMODE_SIOM) {
#if 0
	ncr53c810_log("NCR 810: Reading from I/O address %04X\n", (uint16_t) addr);
#endif
	for (i = 0; i < len; i++)
		buf[i] = inb((uint16_t) (addr + i));
    } else {
#if 0
	ncr53c810_log("NCR 810: Reading from memory address %08X\n", addr);
#endif
       	DMAPageRead(addr, buf, len);
    }
    dev->ncr_to_ncr = 0;
}


static void
ncr53c810_write(ncr53c810c_state_t *dev, uint32_t addr, uint8_t *buf, uint32_t len, int wait)
{
    int i = 0;

#if 0
    ncr53c810_log("ncr53c810_write(): %08X-%08X, length %i\n", addr, (addr + len - 1), len);
#endif

    dev->ncr_to_ncr = wait;
    if (dev->dmode & NCR_DMODE_DIOM) {
#if 0
	ncr53c810_log("NCR 810: Writing to I/O address %04X\n", (uint16_t) addr);
#endif
	for (i = 0; i < len; i++)
		outb((uint16_t) (addr + i), buf[i]);
    } else {
#if 0
	ncr53c810_log("NCR 810: Writing to memory address %08X\n", addr);
#endif
	DMAPageWrite(addr, buf, len);
    }
    dev->ncr_to_ncr = 0;
}


static __inline__ uint32_t
read_dword(ncr53c810c_state_t *dev, uint32_t addr, int wait)
{
    uint32_t buf;
#if 0
    ncr53c810_log("Reading the next DWORD from memory (%08X)...\n", addr);
#endif
    dev->ncr_to_ncr = wait;
    DMAPageRead(addr, (uint8_t *)&buf, 4);
    dev->ncr_to_ncr = 0;
   return buf;
}


static void
ncr53c810_do_irq(ncr53c810c_state_t *dev, int level)
{
    if (level) {
    	pci_set_irq(dev->pci_slot, PCI_INTA);
	ncr53c810_log("Raising IRQ...\n");
    } else {
    	pci_clear_irq(dev->pci_slot, PCI_INTA);
	ncr53c810_log("Lowering IRQ...\n");
    }
}


static void
ncr53c810_irq_wait(ncr53c810c_state_t *dev, int wait)
{
    /* while(wait && dev->last_level)
	; */
    ncr53c810_log("Interrupt wait over\n");
}


static void
ncr53c810_update_irq(ncr53c810c_state_t *dev, int wait)
{
    int level;

    /* It's unclear whether the DIP/SIP bits should be cleared when the
       Interrupt Status Registers are cleared or when istat is read.
       We currently do the formwer, which seems to work.  */
    level = 0;
    if (dev->dstat & 0x7f) {
	if ((dev->dstat & dev->dien) & 0x7f)
		level = 1;
	dev->istat |= NCR_ISTAT_DIP;
    } else {
	dev->istat &= ~NCR_ISTAT_DIP;
    }

    if (dev->sist[0] || (dev->sist[1] & 0x07)) {
	if ((dev->sist[0] & dev->sien[0]) || ((dev->sist[1] & dev->sien[1]) & 0x07))
		level = 1;
	dev->istat |= NCR_ISTAT_SIP;
    } else {
	dev->istat &= ~NCR_ISTAT_SIP;
    }
    if (dev->istat & NCR_ISTAT_INTF) {
	level = 1;
    }

    if (level != dev->last_level) {
	ncr53c810_log("Update IRQ level %d dstat %02x sist %02x%02x\n", level, dev->dstat,
								dev->sist[1], dev->sist[0]);
	dev->last_level = level;
	ncr53c810_do_irq(dev, level);	/* Only do something with the IRQ if the new level differs from the previous one. */
    }
}


/* Stop SCRIPTS execution and raise a SCSI interrupt.  */
static void
ncr53c810_script_scsi_interrupt(ncr53c810c_state_t *dev, int stat0, int stat1, int wait)
{
    uint8_t mask[2];

    if (dev->istat & NCR_ISTAT_SIP) {
	fatal("SCSI interrupt %02X%02X when %02X%02X is already set\n", dev->sist[1] & 0x07, dev->sist[0], stat1 & 0x07, stat0);
	return;
    }

    ncr53c810_log("SCSI Interrupt 0x%02x%02x prev 0x%02x%02x\n", stat1, stat0,
							 dev->sist[1], dev->sist[0]);

    stat1 &= 0x07;

    dev->sist[0] |= stat0;
    dev->sist[1] |= stat1;

    /* When operating in initiator role, CMP, SEL, RSL, GEN, and HTH are non-fatal,
       this means they should only stop SCRIPTS if not masked.
       All other interrupts are fatal and should therefore *ALWAYS* stop SCRIPTS. */
    mask[0] = dev->sien[0] | ~(NCR_SIST0_RSL | NCR_SIST0_SEL | NCR_SIST0_CMP);
    mask[1] = (dev->sien[1] | ~(NCR_SIST1_HTH | NCR_SIST1_GEN)) & 0x07;

    /* Only stop the script on fatal (unmasked) interrupts. */
    if ((dev->sist[0] & mask[0]) || (dev->sist[1] & mask[1]))
	dev->sstop = 1;
    /* Update the IRQ level. */
    ncr53c810_update_irq(dev, wait);
    /* Wait for IRQ to be cleared. */
    ncr53c810_irq_wait(dev, wait);
}


/* Stop SCRIPTS execution and raise a DMA interrupt.  */
static void
ncr53c810_script_dma_interrupt(ncr53c810c_state_t *dev, int stat, int wait)
{
    ncr53c810_log("DMA Interrupt 0x%02x prev 0x%02x  mask 0x%02x\n", stat & 0x7F, dev->dstat & 0x7F, dev->dien & 0x7F);

    if (dev->istat & NCR_ISTAT_DIP) {
	fatal("DMA interrupt %02X when %02X is already set\n", dev->dstat & 0x7F, stat & 0x7F);
	return;
    }

    dev->dstat |= (stat & 0x7f);

    /* All DMA interrupts are fatal, therefore, *ALWAYS* stop SCRIPTS. */
    dev->sstop = 1;
    /* Update the IRQ level. */
    ncr53c810_update_irq(dev, wait);
    /* Wait for IRQ to be cleared. */
    ncr53c810_irq_wait(dev, wait);
}


static __inline__ void
ncr53c810_set_phase(ncr53c810c_state_t *dev, int phase)
{
    dev->sstat1 = (dev->sstat1 & ~PHASE_MASK) | phase;
    ncr53c810_log("Phase set\n");
}


static void
ncr53c810_bad_phase(ncr53c810c_state_t *dev, int out, int new_phase)
{
    /* Trigger a phase mismatch.  */
    ncr53c810_log("Phase mismatch interrupt\n");
    ncr53c810_script_scsi_interrupt(dev, NCR_SIST0_MA, 0, 1);
    dev->sstop = 1;
    ncr53c810_set_phase(dev, new_phase);
}


static void
ncr53c810_disconnect(ncr53c810c_state_t *dev, uint8_t id)
{
    dev->scntl1 &= ~NCR_SCNTL1_CON;
    dev->sstat1 &= ~PHASE_MASK;
    dev->requests[id].connected = 0;
    ncr53c810_log("SCSI ID %i Disconnected\n", id);
}


static void
ncr53c810_bad_selection(ncr53c810c_state_t *dev, uint32_t id)
{
    /* Bad selection - raise STO interrupt, disconnect, and stop SCRIPTS if not masked. */
    ncr53c810_log("Selected absent target %d\n", id);
    ncr53c810_script_scsi_interrupt(dev, 0, NCR_SIST1_STO, 1);
    ncr53c810_disconnect(dev, id);
}


static void
ncr53c810_set_wait_event(void)
{
    ncr53c810_log("NCR 810: Setting wait event arrived...\n");
    thread_set_event((event_t *)wait_evt);
}


/* Callback to indicate that the SCSI layer has completed a command.  */
static void
ncr53c810_command_complete(void *priv, uint32_t status)
{
    ncr53c810c_state_t *dev = (ncr53c810c_state_t *)priv;
    int out;

    out = (dev->sstat1 & PHASE_MASK) == PHASE_DO;
    ncr53c810_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: Command complete status=%d\n",
		  dev->sdid, dev->current_lun, dev->last_command, (int)status);
    dev->status = status;
    dev->command_complete = 2;
    if (dev->waiting && dev->dbc.dw != 0) {
        /* Raise phase mismatch for short transfers.  */
        ncr53c810_bad_phase(dev, out, PHASE_ST);
    } else {
        ncr53c810_set_phase(dev, PHASE_ST);
    }

    dev->sstop = 0;

    ncr53c810_log("Command complete\n");
}


static void
ncr53c810_do_dma(ncr53c810c_state_t *dev, int out, uint8_t id, int wait)
{
    uint32_t addr, count, tdbc;

    scsi_device_t *sd;

    ncr53c810_log("Do DMA...\n");

    sd = &SCSIDevices[id][dev->current_lun];

    if ((((id) == -1) && !scsi_device_present(id, dev->current_lun))) {
    	ncr53c810_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: Device not present when attempting to do DMA\n",
		      id, dev->current_lun, dev->last_command);
	return;
    }
	
    if (!dev->requests[id].dma_len) {
        /* Wait until data is available.  */
    	ncr53c810_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: DMA no data available\n",
		      id, dev->current_lun, dev->last_command);
        return;
    }

    /* Make sure count is never bigger than BufferLength. */
    count = tdbc = dev->dbc.dw;
    if (count > dev->temp_buf_len)
	count = dev->temp_buf_len;

    addr = dev->dnad.dw;

    ncr53c810_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: DMA addr=0x%08x len=%d cur_len=%d dev->dbc=%d\n",
		  id, dev->current_lun, dev->last_command, dev->dnad.dw, dev->temp_buf_len, count, tdbc);
    dev->dnad.dw += count;
    dev->dbc.dw -= count;

    if (out) {
        ncr53c810_read(dev, addr, sd->CmdBuffer+dev->buffer_pos, count, wait);
    }  else {
	if (!dev->buffer_pos) {
    		ncr53c810_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: SCSI Command Phase 1 on PHASE_DI\n",
			      id, dev->current_lun, dev->last_command);
		scsi_device_command_phase1(id, dev->current_lun);
	}
        ncr53c810_write(dev, addr, sd->CmdBuffer+dev->buffer_pos, count, wait);
    }

    dev->temp_buf_len -= count;
    dev->buffer_pos += count;

    if (dev->temp_buf_len <= 0) {
	if (out) {
    		ncr53c810_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: SCSI Command Phase 1 on PHASE_DO\n",
			      id, dev->current_lun, dev->last_command);
		scsi_device_command_phase1(id, dev->current_lun);
	}
	if (sd->CmdBuffer != NULL) {
		free(sd->CmdBuffer);
		sd->CmdBuffer = NULL;
	}
	ncr53c810_command_complete(dev, SCSIStatus);
    } else {
    	ncr53c810_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: Resume SCRIPTS\n",
		      id, dev->current_lun, dev->last_command);
	dev->sstop = 0;
    }

    dev->in_req = 0;
}


/* Queue a byte for a MSG IN phase.  */
static void
ncr53c810_add_msg_byte(ncr53c810c_state_t *dev, uint8_t data)
{
    if (dev->msg_len >= NCR_MAX_MSGIN_LEN) {
        ncr53c810_log("MSG IN data too long\n");
    } else {
        ncr53c810_log("MSG IN 0x%02x\n", data);
        dev->msg[dev->msg_len++] = data;
    }
}


static void
ncr53c810_busy(uint8_t set)
{
    if (busy == !!set)
	return;

    busy = !!set;
    if (!set)
	    thread_set_event((event_t *) wake_poll_thread);
}


static void
ncr53c810_do_command(ncr53c810c_state_t *dev, uint8_t id)
{
    scsi_device_t *sd;
    uint8_t buf[12];

    ncr53c810_log("Do command\n");

    memset(buf, 0, 12);
    DMAPageRead(dev->dnad.dw, buf, MIN(12, dev->dbc.dw));
    if (dev->dbc.dw > 12) {
    	ncr53c810_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: CDB length %i too big\n",
		      id, dev->current_lun, buf[0], dev->dbc);
        dev->dbc.dw = 12;
    }
    dev->sfbr = buf[0];
    dev->command_complete = 0;

    sd = &SCSIDevices[id][dev->current_lun];
    if (((id == -1) || !scsi_device_present(id, dev->current_lun))) {
    	ncr53c810_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: Bad Selection\n", id, dev->current_lun, buf[0]);
        ncr53c810_bad_selection(dev, id);
        return;
    }

    dev->in_req = 1;
    dev->requests[id].tag = id;

    sd->BufferLength = -1;

    ncr53c810_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: DBC=%i\n", id, dev->current_lun,
								      buf[0], dev->dbc.dw);
    dev->last_command = buf[0];

    scsi_device_command_phase0(id, dev->current_lun, dev->dbc.dw, buf);

    dev->waiting = 0;
    dev->buffer_pos = 0;

    dev->temp_buf_len = sd->BufferLength;

    if (sd->BufferLength > 0) {
	sd->CmdBuffer = (uint8_t *)malloc(sd->BufferLength);
	dev->requests[id].dma_len = sd->BufferLength;
    }

    if ((SCSIPhase == SCSI_PHASE_DATA_IN) && (sd->BufferLength > 0)) {
	ncr53c810_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: PHASE_DI\n",
		      id, dev->current_lun, buf[0]);
	ncr53c810_set_phase(dev, PHASE_DI);
    } else if ((SCSIPhase == SCSI_PHASE_DATA_OUT) && (sd->BufferLength > 0)) {
	ncr53c810_log("(ID=%02i LUN=%02i) SCSI Command 0x%02x: PHASE_DO\n",
		      id, dev->current_lun, buf[0]);
	ncr53c810_set_phase(dev, PHASE_DO);
    } else {
	ncr53c810_command_complete(dev, SCSIStatus);
    }
}

static void ncr53c810_do_status(ncr53c810c_state_t *dev, int wait)
{
    uint8_t status;
    ncr53c810_log("Get status len=%d status=%d\n", dev->dbc, dev->status);
    if (dev->dbc.dw != 1)
        ncr53c810_log("Bad Status move\n");
    dev->dbc.dw = 1;
    status = dev->status;
    dev->sfbr = status;
    ncr53c810_write(dev, dev->dnad.dw, &status, 1, wait);
    ncr53c810_set_phase(dev, PHASE_MI);
    dev->msg_action = 1;
    ncr53c810_add_msg_byte(dev, 0); /* COMMAND COMPLETE */
}


static void
ncr53c810_do_msgin(ncr53c810c_state_t *dev, int wait)
{
    int len;
    ncr53c810_log("Message in len=%d/%d\n", dev->dbc, dev->msg_len);
    dev->sfbr = dev->msg[0];
    len = dev->msg_len;
    if (len > dev->dbc.dw)
	len = dev->dbc.dw;
    ncr53c810_write(dev, dev->dnad.dw, dev->msg, len, wait);
    /* Linux drivers rely on the last byte being in the SIDL.  */
    dev->sidl = dev->msg[len - 1];
    dev->msg_len -= len;
    if (dev->msg_len)
	memmove(dev->msg, dev->msg + len, dev->msg_len);
    else {
	switch (dev->msg_action) {
	case 0:
		ncr53c810_set_phase(dev, PHASE_CMD);
		break;
	case 1:
		ncr53c810_disconnect(dev, dev->sdid);
		break;
	case 2:
		ncr53c810_set_phase(dev, PHASE_DO);
		break;
	case 3:
		ncr53c810_set_phase(dev, PHASE_DI);
		break;
	default:
		fatal("Invalid message action: %i\n", dev->msg_action);
	}
    }
}


/* Read the next byte during a MSGOUT phase.  */
static uint8_t
ncr53c810_get_msgbyte(ncr53c810c_state_t *dev)
{
    uint8_t data;
    DMAPageRead(dev->dnad.dw, &data, 1);
    dev->dnad.dw++;
    dev->dbc.dw--;
    return data;
}


/* Skip the next n bytes during a MSGOUT phase. */
static void
ncr53c810_skip_msgbytes(ncr53c810c_state_t *dev, unsigned int n)
{
    dev->dnad.dw += n;
    dev->dbc.dw  -= n;
}


static void
ncr53c810_bad_msg(ncr53c810c_state_t *dev)
{
    ncr53c810_log("MSG: Illegal\n");
    ncr53c810_set_phase(dev, PHASE_MI);
    ncr53c810_add_msg_byte(dev, 7); /* MESSAGE REJECT */
    dev->msg_action = 0;
}


static void
ncr53c810_do_msgout(ncr53c810c_state_t *dev, uint8_t id)
{
    uint8_t msg;
    int len;
    scsi_device_t *sd;

    sd = &SCSIDevices[id][dev->current_lun];
    if ((id > 7) || (dev->current_lun > 7))
	dev = NULL;

    ncr53c810_log("MSG out len=%d\n", dev->dbc);
    while (dev->dbc.dw) {
	msg = ncr53c810_get_msgbyte(dev);
	dev->sfbr = msg;

	switch (msg) {
		case 0x04:
			ncr53c810_log("MSG: Disconnect\n");
			ncr53c810_disconnect(dev, id);
			break;
		case 0x08:
			ncr53c810_log("MSG: No Operation\n");
			ncr53c810_set_phase(dev, PHASE_CMD);
			break;
		case 0x01:
			len = ncr53c810_get_msgbyte(dev);
			msg = ncr53c810_get_msgbyte(dev);
			ncr53c810_log("Extended message 0x%x (len %d)\n", msg, len);
			switch (msg) {
				case 1:
					ncr53c810_log("SDTR (ignored)\n");
					ncr53c810_skip_msgbytes(dev, 2);
					break;
				case 3:
					ncr53c810_log("WDTR (ignored)\n");
					ncr53c810_skip_msgbytes(dev, 1);
					break;
				default:
					ncr53c810_bad_msg(dev);
					return;
			}
			break;
		case 0x20: /* SIMPLE queue */
        	case 0x21: /* HEAD of queue */
	        case 0x22: /* ORDERED queue */
			ncr53c810_log("MSG: Queue\n");
			id |= (ncr53c810_get_msgbyte(dev) | NCR_TAG_VALID);
			ncr53c810_log("SIMPLE queue tag=0x%x\n", id & 0xff);
			break;
		case 0x0d:
			/* The ABORT TAG message clears the current I/O process only. */
			ncr53c810_log("MSG: Abort Tag\n");
			if (sd && sd->CmdBuffer) {
				free(sd->CmdBuffer);
				sd->CmdBuffer = NULL;
			}
			ncr53c810_disconnect(dev, id);
			break;
		case 0x06:
		case 0x0e:
		case 0x0c:
			/* The ABORT message clears all I/O processes for the selecting
			   initiator on the specified logical unit of the target. */
			ncr53c810_log("MSG: Abort, Clear Queue, or Bus Device Reset\n");

			/* clear the current I/O process */
			if (sd && sd->CmdBuffer) {
				free(sd->CmdBuffer);
				sd->CmdBuffer = NULL;
			}

			ncr53c810_disconnect(dev, id);
			break;
		default:
			if ((msg & 0x80) == 0) {
				ncr53c810_bad_msg(dev);
				return;
			} else {
				ncr53c810_log("MSG: Select LUN\n");
				dev->current_lun = msg & 7;
				ncr53c810_set_phase(dev, PHASE_CMD);
			}
			break;
	}
    }
}


static void
ncr53c810_memcpy(ncr53c810c_state_t *dev, uint32_t dest, uint32_t src, int count, int wait)
{
    int n;
    uint8_t buf[NCR_BUF_SIZE];

    ncr53c810_log("memcpy dest 0x%08x src 0x%08x count %d\n", dest, src, count);
    while (count) {
	n = (count > NCR_BUF_SIZE) ? NCR_BUF_SIZE : count;
	ncr53c810_read(dev, src, buf, n, wait);
	ncr53c810_write(dev, dest, buf, n, wait);
	src += n;
	dest += n;
	count -= n;
    }
}


static void
ncr53c810_execute_ins(ncr53c810c_state_t *dev, int wait)
{
    uint32_t insn, addr, id, dest, buf[2];
    int opcode, reg, n, i, operator, cond, jmp, insn_processed = 0;
    int32_t offset;
    uint8_t op0, op1, data8, mask, data[7], *pp;

    dev->sstop = 0;
    insn_processed++;
    insn = read_dword(dev, dev->dsp.dw, wait);
    if (!insn) {
	/* If we receive an empty opcode increment the DSP by 4 bytes
	   instead of 8 and execute the next opcode at that location */
	dev->dsp.dw += 4;
        goto end_of_ins;	/* The thread will take care of resuming execution. */
    }
    addr = read_dword(dev, dev->dsp.dw + 4, wait);
#if 0
    ncr53c810_log("SCRIPTS dsp=%08x opcode %08x arg %08x\n", dev->dsp.dw, insn, addr);
#endif
    dev->dsps.dw = addr;
    dev->dcmd = insn >> 24;
    dev->dsp.dw += 8;

    switch (insn >> 30) {
	case 0: /* Block move.  */
		ncr53c810_log("00: Block move\n");
		if (dev->sist[1] & NCR_SIST1_STO) {
			ncr53c810_log("Delayed select timeout\n");
			dev->sstop = 1;
			break;
		}
		dev->dbc.dw = insn & 0xffffff;
		ncr53c810_log("Block Move: DBC is now %d\n", dev->dbc);
		/* ??? Set ESA.  */
		if (insn & (1 << 29)) {
			/* Indirect addressing.  */
			addr = read_dword(dev, addr, wait);
			ncr53c810_log("Indirect Block Move address: %08X\n", addr);
		} else if (insn & (1 << 28)) {
			/* Table indirect addressing.  */

			/* 32-bit Table indirect */
			offset = sextract32(addr, 0, 24);
			DMAPageRead(dev->dsa.dw + offset, (uint8_t *)buf, 8);
			/* byte count is stored in bits 0:23 only */
			dev->dbc.dw = buf[0] & 0xffffff;
			addr = buf[1];
		}
		if ((dev->sstat1 & PHASE_MASK) != ((insn >> 24) & 7)) {
			ncr53c810_log("Wrong phase got %d, expected %d\n",
				dev->sstat1 & PHASE_MASK, (insn >> 24) & 7);
			ncr53c810_script_scsi_interrupt(dev, NCR_SIST0_MA, 0, wait);
			break;
		}
		dev->dnad.dw = addr;
		ncr53c810_log("Phase before: %01X\n", dev->sstat1 & 0x7);
		switch (dev->sstat1 & 0x7) {
			case PHASE_DO:
				dev->waiting = 0;
				ncr53c810_log("Data Out Phase\n");
				ncr53c810_do_dma(dev, 1, dev->sdid, wait);
				break;
			case PHASE_DI:
				ncr53c810_log("Data In Phase\n");
				dev->waiting = 0;
				ncr53c810_do_dma(dev, 0, dev->sdid, wait);
				break;
			case PHASE_CMD:
				ncr53c810_log("Command Phase\n");
				ncr53c810_do_command(dev, dev->sdid);
				break;
			case PHASE_ST:
				ncr53c810_log("Status Phase\n");
				ncr53c810_do_status(dev, wait);
				break;
			case PHASE_MO:
				ncr53c810_log("MSG Out Phase\n");
				ncr53c810_do_msgout(dev, dev->sdid);
				break;
			case PHASE_MI:
				ncr53c810_log("MSG In Phase\n");
				ncr53c810_do_msgin(dev, wait);
				break;
			default:
				ncr53c810_log("Unimplemented phase %d\n", dev->sstat1 & PHASE_MASK);
		}
		ncr53c810_log("Phase after: %01X\n", dev->sstat1 & 0x7);
		
		dev->dfifo = dev->dbc.dw & 0xff;
		dev->ctest5 = (dev->ctest5 & 0xfc) | ((dev->dbc.dw >> 8) & 3);
		break;

	case 1: /* IO or Read/Write instruction.  */
		ncr53c810_log("01: I/O or Read/Write instruction\n");
		opcode = (insn >> 27) & 7;
		if (opcode < 5) {
			if (insn & (1 << 25))
				id = read_dword(dev, dev->dsa.dw + sextract32(insn, 0, 24), wait);
			else
				id = insn;
			id = (id >> 16) & 0x07;
			if (insn & (1 << 26))
				addr = dev->dsp.dw + sextract32(addr, 0, 24);
			dev->dnad.dw = addr;
			switch (opcode) {
				case 0: /* Select */
					dev->sdid = id;
					if (dev->scntl1 & NCR_SCNTL1_CON) {
						ncr53c810_log("Already reselected, jumping to alt. address\n");
						dev->dsp.dw = dev->dnad.dw;
						break;
					}
					dev->sstat0 |= NCR_SSTAT0_WOA;
					dev->scntl1 &= ~NCR_SCNTL1_IARB;
					if (((id == -1) || !scsi_device_present(id, 0))) {
						ncr53c810_bad_selection(dev, id);
						break;
					}
					ncr53c810_log("Selected target %d%s\n",
					      id, insn & (1 << 24) ? " ATN" : "");
					dev->scntl1 |= NCR_SCNTL1_CON;
					if (insn & (1 << 24))
						dev->socl |= NCR_SOCL_ATN;
					ncr53c810_set_phase(dev, PHASE_MO);
					dev->waiting = 0;
					break;
				case 1: /* Disconnect */
					ncr53c810_log("Disconnect\n");
					dev->scntl1 &= ~NCR_SCNTL1_CON;
					break;
				case 2: /* Wait Reselect */
					ncr53c810_log("Wait Reselect on SCSI ID %i\n", dev->sdid);
					dev->waiting = 1;
					break;
				case 3: /* Set */
					ncr53c810_log("Set%s%s%s%s\n",	insn & (1 << 3) ? " ATN" : "",
								insn & (1 << 6) ? " ACK" : "",
								insn & (1 << 9) ? " TM" : "",
								insn & (1 << 10) ? " CC" : "");
					if (insn & (1 << 3)) {
						dev->socl |= NCR_SOCL_ATN;
						ncr53c810_set_phase(dev, PHASE_MO);
					}
					if (insn & (1 << 9))
						ncr53c810_log("Target mode not implemented\n");
					if (insn & (1 << 10))
						dev->carry = 1;
					break;
				case 4: /* Clear */
					ncr53c810_log("Clear%s%s%s%s\n", insn & (1 << 3) ? " ATN" : "",
								 insn & (1 << 6) ? " ACK" : "",
								 insn & (1 << 9) ? " TM" : "",
								 insn & (1 << 10) ? " CC" : "");
					if (insn & (1 << 3))
					dev->socl &= ~NCR_SOCL_ATN;
					if (insn & (1 << 10))
						dev->carry = 0;
					break;
			}
		} else {
			reg = ((insn >> 16) & 0x7f) | (insn & 0x80);
			data8 = (insn >> 8) & 0xff;
			opcode = (insn >> 27) & 7;
			operator = (insn >> 24) & 7;
			op0 = op1 = 0;

			switch (opcode) {
				case 5: /* From SFBR */
					op0 = dev->sfbr;
					op1 = data8;
					break;
				case 6: /* To SFBR */
					if (operator)
						op0 = ncr53c810_reg_readb(dev, reg);
					op1 = data8;
					break;
				case 7: /* Read-modify-write */
					if (operator)
						op0 = ncr53c810_reg_readb(dev, reg);
					if (insn & (1 << 23))
						op1 = dev->sfbr;
					else
						op1 = data8;
					break;
			}

			switch (operator) {
				case 0: /* move */
					op0 = op1;
					break;
				case 1: /* Shift left */
					op1 = op0 >> 7;
					op0 = (op0 << 1) | dev->carry;
					dev->carry = op1;
					break;
				case 2: /* OR */
					op0 |= op1;
					break;
				case 3: /* XOR */
					op0 ^= op1;
					break;
				case 4: /* AND */
					op0 &= op1;
					break;
				case 5: /* SHR */
					op1 = op0 & 1;
					op0 = (op0 >> 1) | (dev->carry << 7);
					dev->carry = op1;
					break;
				case 6: /* ADD */
					op0 += op1;
					dev->carry = op0 < op1;
					break;
				case 7: /* ADC */
					op0 += op1 + dev->carry;
					if (dev->carry)
						dev->carry = op0 <= op1;
					else
						dev->carry = op0 < op1;
					break;
			}

			switch (opcode) {
				case 5: /* From SFBR */
				case 7: /* Read-modify-write */
					dev->ncr_to_ncr = wait;
					ncr53c810_reg_writeb(dev, reg, op0);
					dev->ncr_to_ncr = 0;
					break;
				case 6: /* To SFBR */
					dev->sfbr = op0;
					break;
			}
		}
		break;

	case 2: /* Transfer Control.  */
		ncr53c810_log("02: Transfer Control\n");

		if ((insn & 0x002e0000) == 0) {
			ncr53c810_log("NOP\n");
			break;
		}
		if (dev->sist[1] & NCR_SIST1_STO) {
			ncr53c810_log("Delayed select timeout\n");
			dev->sstop = 1;
			break;
		}
		cond = jmp = (insn & (1 << 19)) != 0;
		if (cond == jmp && (insn & (1 << 21))) {
			ncr53c810_log("Compare carry %d\n", dev->carry == jmp);
			cond = dev->carry != 0;
		}
		if (cond == jmp && (insn & (1 << 17))) {
			ncr53c810_log("Compare phase %d %c= %d\n", (dev->sstat1 & PHASE_MASK),
							     jmp ? '=' : '!', ((insn >> 24) & 7));
			cond = (dev->sstat1 & PHASE_MASK) == ((insn >> 24) & 7);
		}
		if (cond == jmp && (insn & (1 << 18))) {
			mask = (~insn >> 8) & 0xff;
			ncr53c810_log("Compare data 0x%x & 0x%x %c= 0x%x\n", dev->sfbr, mask,
								     jmp ? '=' : '!', insn & mask);
			cond = (dev->sfbr & mask) == (insn & mask);
		}
		if (cond == jmp) {
			if (insn & (1 << 23)) {
				/* Relative address.  */
				addr = dev->dsp.dw + sextract32(addr, 0, 24);
			}
			switch ((insn >> 27) & 7) {
				case 0: /* Jump */
					ncr53c810_log("Jump to 0x%08x\n", addr);
					dev->adder.dw = addr;
					dev->dsp.dw = addr;
					break;
				case 1: /* Call */
					ncr53c810_log("Call 0x%08x\n", addr);
					dev->temp.dw = dev->dsp.dw;
					dev->dsp.dw = addr;
					break;
				case 2: /* Return */
					ncr53c810_log("Return to 0x%08x\n", dev->temp.dw);
					dev->dsp.dw = dev->temp.dw;
					break;
				case 3: /* Interrupt */
					ncr53c810_log("Interrupt 0x%08x\n", dev->dsps.dw);
					if ((insn & (1 << 20)) != 0) {
						dev->istat |= NCR_ISTAT_INTF;
						ncr53c810_update_irq(dev, wait);
						ncr53c810_irq_wait(dev, wait);
					} else
						ncr53c810_script_dma_interrupt(dev, NCR_DSTAT_SIR, wait);
					break;
				default:
					ncr53c810_log("Invalid opcode\n");
					ncr53c810_script_dma_interrupt(dev, NCR_DSTAT_IID, wait);
					break;
			}
		} else
			ncr53c810_log("Control condition failed\n");
		break;

	case 3:
		ncr53c810_log("03: Memory move\n");
		if ((insn & (1 << 29)) == 0) {
			/* Memory move.  */
			dest = read_dword(dev, dev->dsp.dw, wait);
			dev->dsp.dw += 4;
			ncr53c810_memcpy(dev, dest, addr, insn & 0xffffff, wait);
		} else {
			pp = data;

			if (insn & (1 << 28))
				addr = dev->dsa.dw + sextract32(addr, 0, 24);
			n = (insn & 7);
			reg = (insn >> 16) & 0xff;
			if (insn & (1 << 24)) {
				DMAPageRead(addr, data, n);
				ncr53c810_log("Load reg 0x%x size %d addr 0x%08x = %08x\n", reg, n,
										    addr, *(unsigned *)pp);
				for (i = 0; i < n; i++)
					ncr53c810_reg_writeb(dev, reg + i, data[i]);
			} else {
                		ncr53c810_log("Store reg 0x%x size %d addr 0x%08x\n", reg, n, addr);
				for (i = 0; i < n; i++)
					data[i] = ncr53c810_reg_readb(dev, reg + i);
				DMAPageWrite(addr, data, n);
			}
		}
		break;

	default:
		ncr53c810_log("%02X: Unknown command\n", (uint8_t) (insn >> 30));
    }

end_of_ins:
    ncr53c810_log("%i instruction(s) processed\n", insn_processed);
    if ((insn_processed > 10000) && !dev->waiting) {
	/* Some windows drivers make the device spin waiting for a memory
	   location to change.  If we have been executed a lot of code then
	   assume this is the case and force an unexpected device disconnect.
	   This is apparently sufficient to beat the drivers into submission.
	 */
	ncr53c810_log("Some windows drivers make the device spin...\n");
	if (!(dev->sien[0] & NCR_SIST0_UDC))
		ncr53c810_log("inf. loop with UDC masked\n");
	ncr53c810_script_scsi_interrupt(dev, NCR_SIST0_UDC, 0, wait);
	ncr53c810_disconnect(dev, dev->sdid);
    } else if (!dev->sstop && !dev->waiting) {
	if (dev->dcntl & NCR_DCNTL_SSM) {
		ncr53c810_log("NCR 810: SCRIPTS: Single-step mode\n");
		ncr53c810_script_dma_interrupt(dev, NCR_DSTAT_SSI, wait);
		dev->sstop = 1;
	}
    }
    ncr53c810_log("SFBR now: %02X\n", dev->sfbr);

    if ((dev->dmode & NCR_DMODE_MAN) && dev->sstop)
	dev->dcntl &= ~NCR_DCNTL_STD;
}


static void
ncr53c810_process_script(ncr53c810c_state_t *dev)
{
	ncr53c810_execute_ins(dev, 1);
}


static uint8_t
ncr53c810_is_busy(void)
{
    return(!!busy);
}


void
ncr53c810_wait_for_poll(void)
{
    if (ncr53c810_is_busy()) {
#if 0
	ncr53c810_log("NCR 810: Waiting for thread wake event...\n");
#endif
	thread_wait_event((event_t *) wake_poll_thread, -1);
    }
#if 0
    ncr53c810_log("NCR 810: Thread wake event arrived...\n");
#endif
    thread_reset_event((event_t *) wake_poll_thread);
}


static void
ncr53c810_script_thread(void *priv)
{
    ncr53c810c_state_t *dev = (ncr53c810c_state_t *) ncr53c810_dev;

    thread_set_event((event_t *) thread_started);

    ncr53c810_log("Polling thread started\n");

    while (ncr53c810_dev) {
	scsi_mutex_wait(1);

	if (!dev->sstop) {
		ncr53c810_wait_for_poll();

		if (dev->waiting) {
			/* Wait for event followed by a clear of the waiting state
			   if waiting for reselect. */
			thread_wait_event((event_t *) wait_evt, dev->waiting);
		}

		if (!dev->waiting)
			ncr53c810_process_script(dev);

		if (dev->sstop)
			ncr53c810_log("SCRIPTS finished\n");
	} else
		thread_wait_event((event_t *) wait_evt, 10);

	scsi_mutex_wait(0);
    }

    ncr53c810_log("NCR 810: Callback: polling stopped.\n");
}


static void
ncr53c810_thread_start(ncr53c810c_state_t *dev)
{
    if (!poll_tid) {
	poll_tid = thread_create(ncr53c810_script_thread, dev);
    }
}


static void
ncr53c810_execute_script(ncr53c810c_state_t *dev)
{
    FILE *f;

    ncr53c810_log("Starting SCRIPTS...\n");
    ncr53c810_busy(1);
    dev->sstop = dev->waiting = 0;
    ncr53c810_set_wait_event();
    ncr53c810_busy(0);

    ncr53c810_log("SCRIPT base address: %08X\n", dev->dsp.dw - 4096);
    f = fopen("810scripts.bin", "wb");
    fwrite(&ram[dev->dsp.dw - 4096], 1, 65536, f);
    fclose(f);
}


static void
ncr53c810_reg_writeb(ncr53c810c_state_t *dev, uint32_t offset, uint8_t val)
{
    uint8_t tmp = 0;

    if (!dev->ncr_to_ncr)
	ncr53c810_busy(1);

#ifdef DEBUG_NCR_REG
    ncr53c810_log("Write reg %02x = %02x\n", offset, val);
#endif

    offset &= 0xFF;

    switch (offset) {
	case 0x00: /* SCNTL0 */
		dev->scntl0 = val;
		if (val & NCR_SCNTL0_START) {
			/* Looks like this (turn on bit 4 of SSTAT0 to mark arbitration in progress)
			   is enough to make BIOS v4.x happy. */
			ncr53c810_log("NCR 810: Selecting SCSI ID %i\n", dev->sdid);
			dev->sstat0 |= 0x10;
		}
		break;
	case 0x01: /* SCNTL1 */
		dev->scntl1 = val & ~NCR_SCNTL1_SST;
		if (val & NCR_SCNTL1_IARB) {
			dev->waiting = 0;
			ncr53c810_log("Arbitration won\n");
			dev->sstat0 |= 0x04;
			dev->scntl1 &= ~NCR_SCNTL1_IARB;
			dev->waiting = 0;
		}
		if (val & NCR_SCNTL1_RST) {
			if (!(dev->sstat0 & NCR_SSTAT0_RST)) {
				dev->sstat0 |= NCR_SSTAT0_RST;
				ncr53c810_script_scsi_interrupt(dev, NCR_SIST0_RST, 0, dev->ncr_to_ncr);
			}
		} else
			dev->sstat0 &= ~NCR_SSTAT0_RST;
		break;
	case 0x02: /* SCNTL2 */
		val &= ~(NCR_SCNTL2_WSR | NCR_SCNTL2_WSS);
		dev->scntl2 = val;
		break;
	case 0x03: /* SCNTL3 */
		dev->scntl3 = val;
		break;
	case 0x04: /* SCID */
		dev->scid = val;
		break;
	case 0x05: /* SXFER */
		dev->sxfer = val;
		break;
	case 0x06: /* SDID */
		if ((dev->ssid & 0x80) && (val & 0xf) != (dev->ssid & 0xf))
			ncr53c810_log("Destination ID does not match SSID\n");
		dev->sdid = val & 0xf;
		break;
	case 0x07: /* GPREG0 */
		ncr53c810_log("NCR 810: GPREG0 write %02X\n", val);
		dev->gpreg0 = val & 0x03;
		break;
	case 0x08: /* SFBR */
		/* The CPU is not allowed to write to this register.  However the
		   SCRIPTS register move instructions are.  */
		if (dev->ncr_to_ncr)
			dev->sfbr = val;
		break;
	case 0x09: /* SOCL */
		ncr53c810_log("NCR 810: SOCL write %02X\n", val);
		dev->socl = val;
		break;
	case 0x0a: case 0x0b:
		/* Openserver writes to these readonly registers on startup */
		break;
	case 0x0c: case 0x0d: case 0x0e: case 0x0f:
		/* Linux writes to these readonly registers on startup.  */
		break;
	case 0x10: case 0x11: case 0x12: case 0x13:
		dev->dsa.b[offset & 3] = val;
		break;
	case 0x14: /* ISTAT */
		ncr53c810_log("ISTAT write: %02X\n", val);
		tmp = dev->istat;
		dev->istat = (dev->istat & 0x0f) | (val & 0xf0);
		if ((val & NCR_ISTAT_ABRT) && !(val & NCR_ISTAT_SRST))
			ncr53c810_script_dma_interrupt(dev, NCR_DSTAT_ABRT, dev->ncr_to_ncr);
		if (val & NCR_ISTAT_INTF) {
			dev->istat &= ~NCR_ISTAT_INTF;
			ncr53c810_update_irq(dev, 0);
		}
		if ((dev->waiting == 1) && (val & NCR_ISTAT_SIGP) && !(tmp & NCR_ISTAT_SIGP) && !dev->sstop) {
			ncr53c810_log("Woken by SIGP\n");
			dev->waiting = 0;
			dev->dsp.dw = dev->dnad.dw;
			ncr53c810_set_wait_event();
		}
		if ((val & NCR_ISTAT_SRST) && !(tmp & NCR_ISTAT_SRST)) {
			ncr53c810_soft_reset(dev);
			ncr53c810_update_irq(dev, 0);
			dev->istat = 0;
		}
		break;
	case 0x16: /* MBOX0 */
		dev->mbox0 = val;
		break;
	case 0x17: /* MBOX1 */
		dev->mbox1 = val;
		break;
	case 0x18: /* CTEST0 */
	case 0x19: /* CTEST1 */
		/* nothing to do */
		break;
	case 0x1a: /* CTEST2 */
		dev->ctest2 = val & NCR_CTEST2_PCICIE;
		break;
	case 0x1b: /* CTEST3 */
		dev->ctest3 = val & 0x0f;
		break;
	case 0x1c: case 0x01d: case 0x1e: case 0x1f:
		dev->temp.b[offset & 3] = val;
		break;
	case 0x21: /* CTEST4 */
		if (val & 7)
			ncr53c810_log("Unimplemented CTEST4-FBL 0x%x\n", val);
		dev->ctest4 = val;
		break;
	case 0x22: /* CTEST5 */
		if (val & (NCR_CTEST5_ADCK | NCR_CTEST5_BBCK))
			ncr53c810_log("CTEST5 DMA increment not implemented\n");
		dev->ctest5 = val;
		break;
	case 0x24: case 0x25: case 0x26:
		dev->dbc.b[offset & 3] = val;
		break;
	case 0x28: case 0x29: case 0x2a: case 0x2b:
		dev->dnad.b[offset & 3] = val;
		break;
	case 0x2c: case 0x2d: case 0x2e: case 0x2f:
		if (!dev->sstop) {
			fatal("Writing to DSP while scripts are running!\n");
			return;
		}
		dev->dsp.b[offset & 3] = val;
		if (((offset & 3) == 3) && !(dev->dmode & NCR_DMODE_MAN) && dev->sstop)
			ncr53c810_execute_script(dev);
		break;
	case 0x30: case 0x31: case 0x32: case 0x33:
		dev->dsps.b[offset & 3] = val;
		break;
	case 0x34: case 0x35: case 0x36: case 0x37:
		dev->scratcha.b[offset & 3] = val;
		break;
	case 0x38: /* DMODE */
		dev->dmode = val;
		break;
	case 0x39: /* DIEN */
		ncr53c810_log("DIEN write: %02X\n", val);
		dev->dien = val;
		ncr53c810_update_irq(dev, dev->ncr_to_ncr);
		break;
	case 0x3a: /* SBR */
		dev->sbr = val;
		break;
	case 0x3b: /* DCNTL */
		dev->dcntl = val & ~(NCR_DCNTL_PFF | NCR_DCNTL_STD);
		if ((val & NCR_DCNTL_STD) && dev->sstop) {
			if (dev->dcntl & NCR_DCNTL_SSM)
				ncr53c810_execute_ins(dev, dev->ncr_to_ncr);
			else {
				if (dev->dmode & NCR_DMODE_MAN)
					dev->dcntl |= NCR_DCNTL_STD;
				ncr53c810_execute_script(dev);
			}
		}
		break;
	case 0x40: /* SIEN0 */
	case 0x41: /* SIEN1 */
		dev->sien[offset & 1] = val;
		ncr53c810_update_irq(dev, 0);
		break;
	case 0x47: /* GPCNTL0 */
		break;
	case 0x48: /* STIME0 */
		dev->stime0 = val;
		break;
	case 0x49: /* STIME1 */
		if (val & 0xf) {
			ncr53c810_log("General purpose timer not implemented\n");
			/* ??? Raising the interrupt immediately seems to be sufficient
			   to keep the FreeBSD driver happy.  */
			ncr53c810_script_scsi_interrupt(dev, 0, NCR_SIST1_GEN, dev->ncr_to_ncr);
		}
		break;
	case 0x4a: /* RESPID */
		dev->respid = val;
		break;
	case 0x4d: /* STEST1 */
		dev->stest[0] = val;
		break;
	case 0x4e: /* STEST2 */
		if (val & 1)
			ncr53c810_log("Low level mode not implemented\n");
		dev->stest[1] = val;
		break;
	case 0x4f: /* STEST3 */
		if (val & 0x41)
			ncr53c810_log("SCSI FIFO test mode not implemented\n");
		dev->stest[2] = val;
		break;
	case 0x54:
		break;
	case 0x5c: case 0x5d: case 0x5e: case 0x5f:
		dev->scratchb.b[offset & 3] = val;
		break;
	default:
		ncr53c810_log("Unhandled writeb 0x%x = 0x%x\n", offset, val);
    }

    if (!dev->ncr_to_ncr)
	ncr53c810_busy(0);
}

static uint8_t ncr53c810_reg_readb(ncr53c810c_state_t *dev, uint32_t offset)
{
    uint8_t tmp;
    uint8_t ret = 0;

    if (!dev->ncr_to_ncr)
	ncr53c810_busy(1);

    switch (offset) {
	case 0x00: /* SCNTL0 */
		ncr53c810_log("NCR 810: Read SCNTL0 %02X\n", dev->scntl0);
		ret = dev->scntl0;
		break;
	case 0x01: /* SCNTL1 */
		ncr53c810_log("NCR 810: Read SCNTL1 %02X\n", dev->scntl1);
		ret = dev->scntl1;
		break;
	case 0x02: /* SCNTL2 */
		ncr53c810_log("NCR 810: Read SCNTL2 %02X\n", dev->scntl2);
		ret = ((dev->scntl2) & ~0x40) | ((dev->istat & NCR_ISTAT_SIGP) ? 0x40 : 0x00);
		dev->istat &= ~NCR_ISTAT_SIGP;
		break;
	case 0x03: /* SCNTL3 */
		ncr53c810_log("NCR 810: Read SCNTL3 %02X\n", dev->scntl3);
		ret = dev->scntl3;
		break;
	case 0x04: /* SCID */
		ncr53c810_log("NCR 810: Read SCID %02X\n", dev->scid);
		ret = dev->scid;
		break;
	case 0x05: /* SXFER */
		ncr53c810_log("NCR 810: Read SXFER %02X\n", dev->sxfer);
		ret = dev->sxfer;
		break;
	case 0x06: /* SDID */
		ncr53c810_log("NCR 810: Read SDID %02X\n", dev->sdid);
		ret = dev->sdid;
		break;
	case 0x07: /* GPREG0 */
		ncr53c810_log("NCR 810: Read GPREG0 %02X\n", dev->gpreg0 & 3);
		ret = dev->gpreg0 & 3;
		break;
	case 0x08: /* Revision ID */
	case 0x20: /* DFIFO */
	case 0x23: /* CTEST6 */
	case 0x59: /* SBDL high */
	default:
		ret = 0x00;
		break;
	case 0xa: /* SSID */
		ncr53c810_log("NCR 810: Read SSID %02X\n", dev->ssid);
		ret = dev->ssid;
		break;
	case 0xb: /* SBCL */
		/* Bit 7 = REQ (SREQ/ status)
		   Bit 6 = ACK (SACK/ status)
		   Bit 5 = BSY (SBSY/ status)
		   Bit 4 = SEL (SSEL/ status)
		   Bit 3 = ATN (SATN/ status)
		   Bit 2 = MSG (SMSG/ status)
		   Bit 1 = C/D (SC_D/ status)
		   Bit 0 = I/O (SI_O/ status) */
		tmp = (dev->sstat1 & 7);
		ncr53c810_log("NCR 810: Read SBCL %02X\n", tmp);
		ret = tmp;	/* For now, return the MSG, C/D, and I/O bits from SSTAT1. */
		break;
	case 0xc: /* DSTAT */
		tmp = dev->dstat | NCR_DSTAT_DFE;
		if ((dev->istat & NCR_ISTAT_INTF) == 0)
			dev->dstat = NCR_DSTAT_DFE;
		ncr53c810_update_irq(dev, 0);
		ncr53c810_log("NCR 810: Read DSTAT %02X\n", tmp);
		ret = tmp;
		break;
	case 0x0d: /* SSTAT0 */
		ncr53c810_log("NCR 810: Read SSTAT0 %02X\n", dev->sstat0);
		ret = dev->sstat0;
		break;
	case 0x0e: /* SSTAT1 */
		ncr53c810_log("NCR 810: Read SSTAT1 %02X\n", dev->sstat1);
		ret = dev->sstat1;
		break;
	case 0x0f: /* SSTAT2 */
		ret = (dev->scntl1 & NCR_SCNTL1_CON) ? 0 : 2;
		ncr53c810_log("NCR 810: Read SSTAT2 %02X\n", ret);
		break;
	case 0x10: case 0x11: case 0x12: case 0x13:
		ret = dev->dsa.b[offset & 3];
		break;
	case 0x14: /* ISTAT */
#if 0
		ncr53c810_log("NCR 810: Read ISTAT %02X\n", dev->istat);
#endif
		ret = dev->istat;
		break;
	case 0x16: /* MBOX0 */
		ncr53c810_log("NCR 810: Read MBOX0 %02X\n", dev->mbox0);
		ret = dev->mbox0;
		break;
	case 0x17: /* MBOX1 */
		ncr53c810_log("NCR 810: Read MBOX1 %02X\n", dev->mbox1);
		ret = dev->mbox1;
		break;
	case 0x18: /* CTEST0 */
		ncr53c810_log("NCR 810: Read CTEST0 FF\n");
		ret = 0xff;
		break;
	case 0x19: /* CTEST1 */
		ncr53c810_log("NCR 810: Read CTEST1 F0\n");
		ret = 0xf0;	/* dma fifo empty */
		break;
	case 0x1a: /* CTEST2 */
		tmp = dev->ctest2 | NCR_CTEST2_DACK | NCR_CTEST2_CM;
		if (dev->istat & NCR_ISTAT_SIGP) {
			dev->istat &= ~NCR_ISTAT_SIGP;
			tmp |= NCR_CTEST2_SIGP;
		}
		ncr53c810_log("NCR 810: Read CTEST2 %02X\n", tmp);
		ret = tmp;
		break;
	case 0x1b: /* CTEST3 */
		ncr53c810_log("NCR 810: Read CTEST3 %02X\n", (dev->ctest3 & (0x08 | 0x02 | 0x01)) | dev->chip_rev);
		ret = (dev->ctest3 & (0x08 | 0x02 | 0x01)) | dev->chip_rev;
		break;
	case 0x1c: case 0x1d: case 0x1e: case 0x1f:
		ret = dev->temp.b[offset & 3];
		break;
	case 0x21: /* CTEST4 */
		ncr53c810_log("NCR 810: Read CTEST4 %02X\n", dev->ctest4);
		ret = dev->ctest4;
		break;
	case 0x22: /* CTEST5 */
		ncr53c810_log("NCR 810: Read CTEST5 %02X\n", dev->ctest5);
		ret = dev->ctest5;
		break;
	case 0x24: case 0x25: case 0x26:
		ret = dev->dbc.b[offset & 3];
		break;
	case 0x27: /* DCMD */
		ncr53c810_log("NCR 810: Read DCMD %02X\n", dev->dcmd);
		ret = dev->dcmd;
		break;
	case 0x28: case 0x29: case 0x2a: case 0x2b:
		ret = dev->dnad.b[offset & 3];
		break;
	case 0x2c: case 0x2d: case 0x2e: case 0x2f:
		ret = dev->dsp.b[offset & 3];
		break;
	case 0x30: case 0x31: case 0x32: case 0x33:
		ret = dev->dsps.b[offset & 3];
		break;
	case 0x34: case 0x35: case 0x36: case 0x37:
		ret = dev->scratcha.b[offset & 3];
		break;
	case 0x38: /* DMODE */
		ncr53c810_log("NCR 810: Read DMODE %02X\n", dev->dmode);
		ret = dev->dmode;
		break;
	case 0x39: /* DIEN */
		ncr53c810_log("NCR 810: Read DIEN %02X\n", dev->dien);
		ret = dev->dien;
		break;
	case 0x3a: /* SBR */
		ncr53c810_log("NCR 810: Read SBR %02X\n", dev->sbr);
		ret = dev->sbr;
		break;
	case 0x3b: /* DCNTL */
		ncr53c810_log("NCR 810: Read DCNTL %02X\n", dev->dcntl);
		ret = dev->dcntl;
		break;
	/* ADDER Output (Debug of relative jump address) */
	case 0x3c: case 0x3d: case 0x3e: case 0x3f:
		ret = dev->adder.b[offset & 3];
		break;
	case 0x40: /* SIEN0 */
    	case 0x41: /* SIEN1 */
		ret = dev->sien[offset & 1];
		break;
	case 0x42: /* SIST0 */
	case 0x43: /* SIST1 */
		tmp = dev->sist[offset & 1];
		dev->sist[offset & 1] = 0;
		ncr53c810_update_irq(dev, 0);
		ret = tmp;
		break;
	case 0x46: /* MACNTL */
		ncr53c810_log("NCR 810: Read MACNTL 4F\n");
		ret = 0x4f;
		break;
	case 0x47: /* GPCNTL0 */
		ncr53c810_log("NCR 810: Read GPCNTL0 0F\n");
		ret = 0x0f;
		break;
	case 0x48: /* STIME0 */
		ncr53c810_log("NCR 810: Read STIME0 %02X\n", dev->stime0);
		ret = dev->stime0;
		break;
	case 0x4a: /* RESPID */
		ncr53c810_log("NCR 810: Read RESPID %02X\n", dev->respid);
		ret = dev->respid;
		break;
	case 0x4d: /* STEST1 */
	case 0x4e: /* STEST2 */
	case 0x4f: /* STEST3 */
		ret = dev->stest[(offset & 3) - 1];
		break;
	case 0x50: /* SIDL */
		/* This is needed by the linux drivers.  We currently only update it
		   during the MSG IN phase.  */
		ncr53c810_log("NCR 810: Read SIDL %02X\n", dev->sidl);
		ret = dev->sidl;
		break;
	case 0x52: /* STEST4 */
		ncr53c810_log("NCR 810: Read STEST4 E0\n");
		ret = 0xe0;
		break;
	case 0x58: /* SBDL */
		/* Some drivers peek at the data bus during the MSG IN phase.  */
		if ((dev->sstat1 & PHASE_MASK) == PHASE_MI) {
			ncr53c810_log("NCR 810: Read SBDL %02X\n", dev->msg[0]);
			ret = dev->msg[0];
			break;
		}
		ncr53c810_log("NCR 810: Read SBDL 00\n");
		ret = 0;
		break;
	case 0x5c: case 0x5d: case 0x5e: case 0x5f:
		ret = dev->scratchb.b[offset & 3];
		break;
    }

    if (!dev->ncr_to_ncr)
	ncr53c810_busy(0);

    return ret;
}


static uint8_t
ncr53c810_io_readb(uint16_t addr, void *p)
{
    ncr53c810c_state_t *dev = (ncr53c810c_state_t *)p;
    return ncr53c810_reg_readb(dev, addr & 0xff);
}


static uint16_t
ncr53c810_io_readw(uint16_t addr, void *p)
{
    ncr53c810c_state_t *dev = (ncr53c810c_state_t *)p;
    uint16_t val;
	
    addr &= 0xff;
    val = ncr53c810_reg_readb(dev, addr);
    val |= ncr53c810_reg_readb(dev, addr + 1) << 8;
    return val;
}


static uint32_t
ncr53c810_io_readl(uint16_t addr, void *p)
{
    ncr53c810c_state_t *dev = (ncr53c810c_state_t *)p;
    uint32_t val;
	
    addr &= 0xff;
    val = ncr53c810_reg_readb(dev, addr);
    val |= ncr53c810_reg_readb(dev, addr + 1) << 8;
    val |= ncr53c810_reg_readb(dev, addr + 2) << 16;
    val |= ncr53c810_reg_readb(dev, addr + 3) << 24;
    return val;
}


static void
ncr53c810_io_writeb(uint16_t addr, uint8_t val, void *p)
{
    ncr53c810c_state_t *dev = (ncr53c810c_state_t *)p;
	ncr53c810_reg_writeb(dev, addr & 0xff, val);
}


static void
ncr53c810_io_writew(uint16_t addr, uint16_t val, void *p)
{
    ncr53c810c_state_t *dev = (ncr53c810c_state_t *)p;	
	addr &= 0xff;
	ncr53c810_reg_writeb(dev, addr, val & 0xff);
	ncr53c810_reg_writeb(dev, addr + 1, (val >> 8) & 0xff);
}


static void
ncr53c810_io_writel(uint16_t addr, uint32_t val, void *p)
{
    ncr53c810c_state_t *dev = (ncr53c810c_state_t *)p;
    addr &= 0xff;
    ncr53c810_reg_writeb(dev, addr, val & 0xff);
    ncr53c810_reg_writeb(dev, addr + 1, (val >> 8) & 0xff);
    ncr53c810_reg_writeb(dev, addr + 2, (val >> 16) & 0xff);
    ncr53c810_reg_writeb(dev, addr + 3, (val >> 24) & 0xff);
}


static void
ncr53c810_mmio_writeb(uint32_t addr, uint8_t val, void *p)
{
    ncr53c810c_state_t *dev = (ncr53c810c_state_t *)p;

    ncr53c810_reg_writeb(dev, addr & 0xff, val);
}


static void
ncr53c810_mmio_writew(uint32_t addr, uint16_t val, void *p)
{
	ncr53c810c_state_t *dev = (ncr53c810c_state_t *)p;
	
	addr &= 0xff;
	ncr53c810_reg_writeb(dev, addr, val & 0xff);
	ncr53c810_reg_writeb(dev, addr + 1, (val >> 8) & 0xff);
}


static void
ncr53c810_mmio_writel(uint32_t addr, uint32_t val, void *p)
{
	ncr53c810c_state_t *dev = (ncr53c810c_state_t *)p;

	addr &= 0xff;
	ncr53c810_reg_writeb(dev, addr, val & 0xff);
	ncr53c810_reg_writeb(dev, addr + 1, (val >> 8) & 0xff);
	ncr53c810_reg_writeb(dev, addr + 2, (val >> 16) & 0xff);
	ncr53c810_reg_writeb(dev, addr + 3, (val >> 24) & 0xff);
}


static uint8_t
ncr53c810_mmio_readb(uint32_t addr, void *p)
{
	ncr53c810c_state_t *dev = (ncr53c810c_state_t *)p;
	
	return ncr53c810_reg_readb(dev, addr & 0xff);
}


static uint16_t
ncr53c810_mmio_readw(uint32_t addr, void *p)
{
	ncr53c810c_state_t *dev = (ncr53c810c_state_t *)p;
	uint16_t val;
	
	addr &= 0xff;
	val = ncr53c810_reg_readb(dev, addr);
	val |= ncr53c810_reg_readb(dev, addr + 1) << 8;
	return val;
}
 

static uint32_t
ncr53c810_mmio_readl(uint32_t addr, void *p)
{
	ncr53c810c_state_t *dev = (ncr53c810c_state_t *)p;
	uint32_t val;
	
	addr &= 0xff;
	val = ncr53c810_reg_readb(dev, addr);
	val |= ncr53c810_reg_readb(dev, addr + 1) << 8;
	val |= ncr53c810_reg_readb(dev, addr + 2) << 16;
	val |= ncr53c810_reg_readb(dev, addr + 3) << 24;	
	return val;
}


static void
ncr53c810_io_set(ncr53c810c_state_t *dev, uint32_t base, uint16_t len)
{
	ncr53c810_log("NCR53c810: [PCI] Setting I/O handler at %04X\n", base);
	io_sethandler(base, len,
		      ncr53c810_io_readb, ncr53c810_io_readw, ncr53c810_io_readl,
                      ncr53c810_io_writeb, ncr53c810_io_writew, ncr53c810_io_writel, dev);
}


static void
ncr53c810_io_remove(ncr53c810c_state_t *dev, uint32_t base, uint16_t len)
{
    ncr53c810_log("NCR53c810: Removing I/O handler at %04X\n", base);
	io_removehandler(base, len,
		      ncr53c810_io_readb, ncr53c810_io_readw, ncr53c810_io_readl,
                      ncr53c810_io_writeb, ncr53c810_io_writew, ncr53c810_io_writel, dev);
}


static void
ncr53c810_mem_init(ncr53c810c_state_t *dev, uint32_t addr)
{
	mem_mapping_add(&dev->mmio_mapping, addr, 0x100,
		        ncr53c810_mmio_readb, ncr53c810_mmio_readw, ncr53c810_mmio_readl,
			ncr53c810_mmio_writeb, ncr53c810_mmio_writew, ncr53c810_mmio_writel,
			NULL, MEM_MAPPING_EXTERNAL, dev);
}


static void
ncr53c810_mem_set_addr(ncr53c810c_state_t *dev, uint32_t base)
{
    mem_mapping_set_addr(&dev->mmio_mapping, base, 0x100);
}


static void
ncr53c810_mem_disable(ncr53c810c_state_t *dev)
{
    mem_mapping_disable(&dev->mmio_mapping);
}


uint8_t	ncr53c810_pci_regs[256];
bar_t	ncr53c810_pci_bar[2];


static uint8_t
LSIPCIRead(int func, int addr, void *p)
{
    ncr53c810c_state_t *dev = (ncr53c810c_state_t *)p;

    ncr53c810_log("NCR53c810: Reading register %02X\n", addr & 0xff);

    if ((addr >= 0x80) && (addr <= 0xDF)) {
	return ncr53c810_reg_readb(dev, addr & 0x7F);
    }

    switch (addr) {
	case 0x00:
		return 0x00;
	case 0x01:
		return 0x10;
	case 0x02:
		return 0x01;
	case 0x03:
		return 0x00;
	case 0x04:
		return ncr53c810_pci_regs[0x04] & 0x57;	/*Respond to IO and memory accesses*/
	case 0x05:
		return ncr53c810_pci_regs[0x05] & 0x01;
	case 0x07:
		return 2;
	case 0x08:
		return 0x10;			/*Revision ID*/
	case 0x09:
		return 0;			/*Programming interface*/
	case 0x0A:
		return 0;			/*Subclass*/
	case 0x0B:
		return 1;			/*Class code*/
	case 0x0C:
	case 0x0D:
		return ncr53c810_pci_regs[addr];
	case 0x0E:
		return 0;			/*Header type */
	case 0x10:
		return 1;			/*I/O space*/
	case 0x11:
		return ncr53c810_pci_bar[0].addr_regs[1];
	case 0x12:
		return ncr53c810_pci_bar[0].addr_regs[2];
	case 0x13:
		return ncr53c810_pci_bar[0].addr_regs[3];
	case 0x14:
		return 0;			/*Memory space*/
	case 0x15:
		return ncr53c810_pci_bar[1].addr_regs[1];
	case 0x16:
		return ncr53c810_pci_bar[1].addr_regs[2];
	case 0x17:
		return ncr53c810_pci_bar[1].addr_regs[3];
	case 0x2C:
		return 0x00;
	case 0x2D:
		return 0x10;
	case 0x2E:
		return 0x01;
	case 0x2F:
		return 0x00;
	case 0x3C:
		return dev->irq;
	case 0x3D:
		return PCI_INTA;
	case 0x3E:
		return 0x11;
	case 0x3F:
		return 0x40;
    }

    return(0);
}


static void
LSIPCIWrite(int func, int addr, uint8_t val, void *p)
{
    ncr53c810c_state_t *dev = (ncr53c810c_state_t *)p;
    uint8_t valxor;

    ncr53c810_log("NCR53c810: Write value %02X to register %02X\n", val, addr & 0xff);

    if ((addr >= 0x80) && (addr <= 0xDF)) {
	ncr53c810_reg_writeb(dev, addr & 0x7F, val);
	return;
    }

    switch (addr) 
	{
	case 0x04:
		valxor = (val & 0x57) ^ ncr53c810_pci_regs[addr];
		if (valxor & PCI_COMMAND_IO) {
			ncr53c810_io_remove(dev, dev->PCIBase, 0x0100);
			if ((dev->PCIBase != 0) && (val & PCI_COMMAND_IO)) {
				ncr53c810_io_set(dev, dev->PCIBase, 0x0100);
			}
		}
		if (valxor & PCI_COMMAND_MEM) {
			ncr53c810_mem_disable(dev);
			if ((dev->MMIOBase != 0) && (val & PCI_COMMAND_MEM)) {
				ncr53c810_mem_set_addr(dev, dev->MMIOBase);
			}
		}
		ncr53c810_pci_regs[addr] = val & 0x57;
		break;

	case 0x05:
		ncr53c810_pci_regs[addr] = val & 0x01;
		break;

	case 0x0C:
	case 0x0D:
		ncr53c810_pci_regs[addr] = val;
		break;

	case 0x10: case 0x11: case 0x12: case 0x13:
		/* I/O Base set. */
		/* First, remove the old I/O. */
		ncr53c810_io_remove(dev, dev->PCIBase, 0x0100);
		/* Then let's set the PCI regs. */
		ncr53c810_pci_bar[0].addr_regs[addr & 3] = val;
		/* Then let's calculate the new I/O base. */
		ncr53c810_pci_bar[0].addr &= 0xff00;
		dev->PCIBase = ncr53c810_pci_bar[0].addr;
		/* Log the new base. */
		ncr53c810_log("NCR53c810: New I/O base is %04X\n" , dev->PCIBase);
		/* We're done, so get out of the here. */
		if (ncr53c810_pci_regs[4] & PCI_COMMAND_IO) {
			if (dev->PCIBase != 0) {
				ncr53c810_io_set(dev, dev->PCIBase, 0x0100);
			}
		}
		return;

	case 0x15: case 0x16: case 0x17:
		/* MMIO Base set. */
		/* First, remove the old I/O. */
		ncr53c810_mem_disable(dev);
		/* Then let's set the PCI regs. */
		ncr53c810_pci_bar[1].addr_regs[addr & 3] = val;
		/* Then let's calculate the new I/O base. */
		dev->MMIOBase = ncr53c810_pci_bar[1].addr & 0xffffff00;
		/* Log the new base. */
		ncr53c810_log("NCR53c810: New MMIO base is %08X\n" , dev->MMIOBase);
		/* We're done, so get out of the here. */
		if (ncr53c810_pci_regs[4] & PCI_COMMAND_MEM) {
			if (dev->MMIOBase != 0) {
				ncr53c810_mem_set_addr(dev, dev->MMIOBase);
			}
		}
		return;	

	case 0x3C:
		ncr53c810_pci_regs[addr] = val;
		dev->irq = val;
		return;
    }
}


static void *
ncr53c810_init(device_t *info)
{
    ncr53c810c_state_t *dev;

    dev = malloc(sizeof(ncr53c810c_state_t));
    memset(dev, 0x00, sizeof(ncr53c810c_state_t));

    dev->chip_rev = 0;
    dev->pci_slot = pci_add_card(PCI_ADD_NORMAL, LSIPCIRead, LSIPCIWrite, dev);

    ncr53c810_pci_bar[0].addr_regs[0] = 1;
    ncr53c810_pci_bar[1].addr_regs[0] = 0;
    ncr53c810_pci_regs[0x04] = 3;	

    ncr53c810_mem_init(dev, 0x0fffff00);
    ncr53c810_mem_disable(dev);

    ncr53c810_soft_reset(dev);

    ncr53c810_dev = dev;

    scsi_mutex(1);

    wake_poll_thread = thread_create_event();
    thread_started = thread_create_event();

    /* Create a waitable event. */
    evt = thread_create_event();
    wait_evt = thread_create_event();

    ncr53c810_thread_start(dev);
    thread_wait_event((event_t *) thread_started, -1);

    return(dev);
}


static void 
ncr53c810_close(void *priv)
{
    ncr53c810c_state_t *dev = (ncr53c810c_state_t *)priv;

    if (dev) {
	ncr53c810_dev = NULL;

        /* Tell the thread to terminate. */
	if (poll_tid != NULL) {
		ncr53c810_busy(0);

		/* Wait for the end event. */
		thread_wait((event_t *) poll_tid, -1);

		poll_tid = NULL;
	}

	dev->sstop = 1;

	if (wait_evt) {
		thread_destroy_event((event_t *) evt);
		evt = NULL;
	}

	if (evt) {
		thread_destroy_event((event_t *) evt);
		evt = NULL;
	}

	if (thread_started) {
		thread_destroy_event((event_t *) thread_started);
		thread_started = NULL;
	}

	if (wake_poll_thread) {
		thread_destroy_event((event_t *) wake_poll_thread);
		wake_poll_thread = NULL;
	}

	scsi_mutex(0);

	free(dev);
	dev = NULL;
    }
}


device_t ncr53c810_pci_device =
{
    "NCR 53c810 (SCSI)",
    DEVICE_PCI,
    0,
    ncr53c810_init, ncr53c810_close, NULL,
    NULL,
    NULL, NULL, NULL,
    NULL
};
