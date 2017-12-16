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
 * Version:	@(#)scsi_ncr53c810.c	1.0.1	2017/12/15
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
#include "../network/bswap.h"
#include "queue.h"

#define LSI_SCNTL0_TRG    0x01
#define LSI_SCNTL0_AAP    0x02
#define LSI_SCNTL0_EPC    0x08
#define LSI_SCNTL0_WATN   0x10
#define LSI_SCNTL0_START  0x20

#define LSI_SCNTL1_SST    0x01
#define LSI_SCNTL1_IARB   0x02
#define LSI_SCNTL1_AESP   0x04
#define LSI_SCNTL1_RST    0x08
#define LSI_SCNTL1_CON    0x10
#define LSI_SCNTL1_DHP    0x20
#define LSI_SCNTL1_ADB    0x40
#define LSI_SCNTL1_EXC    0x80

#define LSI_SCNTL2_WSR    0x01
#define LSI_SCNTL2_VUE0   0x02
#define LSI_SCNTL2_VUE1   0x04
#define LSI_SCNTL2_WSS    0x08
#define LSI_SCNTL2_SLPHBEN 0x10
#define LSI_SCNTL2_SLPMD  0x20
#define LSI_SCNTL2_CHM    0x40
#define LSI_SCNTL2_SDU    0x80

#define LSI_ISTAT0_DIP    0x01
#define LSI_ISTAT0_SIP    0x02
#define LSI_ISTAT0_INTF   0x04
#define LSI_ISTAT0_CON    0x08
#define LSI_ISTAT0_SEM    0x10
#define LSI_ISTAT0_SIGP   0x20
#define LSI_ISTAT0_SRST   0x40
#define LSI_ISTAT0_ABRT   0x80

#define LSI_ISTAT1_SI     0x01
#define LSI_ISTAT1_SRUN   0x02
#define LSI_ISTAT1_FLSH   0x04

#define LSI_SSTAT0_SDP0   0x01
#define LSI_SSTAT0_RST    0x02
#define LSI_SSTAT0_WOA    0x04
#define LSI_SSTAT0_LOA    0x08
#define LSI_SSTAT0_AIP    0x10
#define LSI_SSTAT0_OLF    0x20
#define LSI_SSTAT0_ORF    0x40
#define LSI_SSTAT0_ILF    0x80

#define LSI_SIST0_PAR     0x01
#define LSI_SIST0_RST     0x02
#define LSI_SIST0_UDC     0x04
#define LSI_SIST0_SGE     0x08
#define LSI_SIST0_RSL     0x10
#define LSI_SIST0_SEL     0x20
#define LSI_SIST0_CMP     0x40
#define LSI_SIST0_MA      0x80

#define LSI_SIST1_HTH     0x01
#define LSI_SIST1_GEN     0x02
#define LSI_SIST1_STO     0x04
#define LSI_SIST1_SBMC    0x10

#define LSI_SOCL_IO       0x01
#define LSI_SOCL_CD       0x02
#define LSI_SOCL_MSG      0x04
#define LSI_SOCL_ATN      0x08
#define LSI_SOCL_SEL      0x10
#define LSI_SOCL_BSY      0x20
#define LSI_SOCL_ACK      0x40
#define LSI_SOCL_REQ      0x80

#define LSI_DSTAT_IID     0x01
#define LSI_DSTAT_SIR     0x04
#define LSI_DSTAT_SSI     0x08
#define LSI_DSTAT_ABRT    0x10
#define LSI_DSTAT_BF      0x20
#define LSI_DSTAT_MDPE    0x40
#define LSI_DSTAT_DFE     0x80

#define LSI_DCNTL_COM     0x01
#define LSI_DCNTL_IRQD    0x02
#define LSI_DCNTL_STD     0x04
#define LSI_DCNTL_IRQM    0x08
#define LSI_DCNTL_SSM     0x10
#define LSI_DCNTL_PFEN    0x20
#define LSI_DCNTL_PFF     0x40
#define LSI_DCNTL_CLSE    0x80

#define LSI_DMODE_MAN     0x01
#define LSI_DMODE_BOF     0x02
#define LSI_DMODE_ERMP    0x04
#define LSI_DMODE_ERL     0x08
#define LSI_DMODE_DIOM    0x10
#define LSI_DMODE_SIOM    0x20

#define LSI_CTEST2_DACK   0x01
#define LSI_CTEST2_DREQ   0x02
#define LSI_CTEST2_TEOP   0x04
#define LSI_CTEST2_PCICIE 0x08
#define LSI_CTEST2_CM     0x10
#define LSI_CTEST2_CIO    0x20
#define LSI_CTEST2_SIGP   0x40
#define LSI_CTEST2_DDIR   0x80

#define LSI_CTEST5_BL2    0x04
#define LSI_CTEST5_DDIR   0x08
#define LSI_CTEST5_MASR   0x10
#define LSI_CTEST5_DFSN   0x20
#define LSI_CTEST5_BBCK   0x40
#define LSI_CTEST5_ADCK   0x80

/* Enable Response to Reselection */
#define LSI_SCID_RRE      0x60

#define PHASE_DO          0
#define PHASE_DI          1
#define PHASE_CMD         2
#define PHASE_ST          3
#define PHASE_MO          6
#define PHASE_MI          7
#define PHASE_MASK        7

/* Maximum length of MSG IN data.  */
#define LSI_MAX_MSGIN_LEN 8

/* Flag set if this is a tagged command.  */
#define LSI_TAG_VALID     (1 << 16)

typedef struct lsi_request {
    uint32_t tag;
    uint32_t dma_len;
    uint8_t *dma_buf;
    uint32_t pending;
    int out;
    QTAILQ_ENTRY(lsi_request) next;
} lsi_request;

typedef enum
{
        SCSI_STATE_SEND_COMMAND,
        SCSI_STATE_READ_DATA,
        SCSI_STATE_WRITE_DATA,
        SCSI_STATE_READ_STATUS,
        SCSI_STATE_READ_MESSAGE,
	SCSI_STATE_WRITE_MESSAGE
} scsi_state_t;

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
    int msg_action;
    int msg_len;
    uint8_t msg[LSI_MAX_MSGIN_LEN];
    /* 0 if SCRIPTS are running or stopped.
     * 1 if a Wait Reselect instruction has been issued.
     * 2 if processing DMA from lsi_execute_script.
     * 3 if a DMA operation is in progress.  */
    int waiting;

    uint8_t current_lun;
    uint8_t select_id;
    int command_complete;
    QTAILQ_HEAD(, lsi_request) queue;
    lsi_request *current;

    int irq;
	
    uint32_t dsa;
    uint32_t temp;
    uint32_t dnad;
    uint32_t dbc;
    uint8_t istat0;
    uint8_t istat1;
    uint8_t dcmd;
    uint8_t dstat;
    uint8_t dien;
    uint8_t sist0;
    uint8_t sist1;
    uint8_t sien0;
    uint8_t sien1;
    uint8_t mbox0;
    uint8_t mbox1;
    uint8_t dfifo;
    uint8_t ctest2;
    uint8_t ctest3;
    uint8_t ctest4;
    uint8_t ctest5;
    uint32_t dsp;
    uint32_t dsps;
    uint8_t dmode;
    uint8_t dcntl;
    uint8_t scntl0;
    uint8_t scntl1;
    uint8_t scntl2;
    uint8_t scntl3;
    uint8_t sstat0;
    uint8_t sstat1;
    uint8_t scid;
    uint8_t sxfer;
    uint8_t socl;
    uint8_t sdid;
    uint8_t ssid;
    uint8_t sfbr;
    uint8_t stest1;
    uint8_t stest2;
    uint8_t stest3;
    uint8_t sidl;
    uint8_t stime0;
    uint8_t respid0;
    uint8_t respid1;
    uint32_t scratch[4]; /* SCRATCHA-SCRATCHR */
    uint8_t sbr;
    uint8_t chip_rev;
    int last_level;
    void *hba_private;
    uint8_t gpreg0;
    uint32_t buffer_pos;
    int32_t temp_buf_len;
    uint8_t last_command;

    /* Script ram is stored as 32-bit words in host byteorder.  */
    uint8_t script_ram[8192];
    uint8_t sstop;

    uint32_t prefetch;
    uint8_t prefetch_used;

    uint8_t regop;
    uint32_t adder;
} LSIState;


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

#define DPRINTF	ncr53c810_log
#define BADF	ncr53c810_log

#define DPRINTF	ncr53c810_log
#define BADF	ncr53c810_log

static uint8_t lsi_reg_readb(LSIState *s, uint32_t offset);
static void lsi_reg_writeb(LSIState *s, uint32_t offset, uint8_t val);
static void lsi_execute_script(LSIState *s);
static void lsi_reselect(LSIState *s, lsi_request *p);
static void lsi_command_complete(void *priv, uint32_t status);
#if 0
static void lsi_transfer_data(void *priv, uint32_t len, uint8_t id);
#endif

static inline int32_t sextract32(uint32_t value, int start, int length)
{
    /* Note that this implementation relies on right shift of signed
     * integers being an arithmetic shift.
     */
    return ((int32_t)(value << (32 - length - start))) >> (32 - length);
}

static inline uint32_t deposit32(uint32_t value, int start, int length,
                                 uint32_t fieldval)
{
    uint32_t mask;
    mask = (~0U >> (32 - length)) << start;
    return (value & ~mask) | ((fieldval << start) & mask);
}

static inline int lsi_irq_on_rsl(LSIState *s)
{
    return (s->sien0 & LSI_SIST0_RSL) && (s->scid & LSI_SCID_RRE);
}

static void lsi_soft_reset(LSIState *s)
{
    DPRINTF("LSI Reset\n");
    s->carry = 0;

    s->msg_action = 0;
    s->msg_len = 0;
    s->waiting = 0;
    s->dsa = 0;
    s->dnad = 0;
    s->dbc = 0;
    s->temp = 0;
    memset(s->scratch, 0, sizeof(s->scratch));
    s->istat0 = 0;
    s->istat1 = 0;
    s->dcmd = 0x40;
    s->dstat = LSI_DSTAT_DFE;
    s->dien = 0;
    s->sist0 = 0;
    s->sist1 = 0;
    s->sien0 = 0;
    s->sien1 = 0;
    s->mbox0 = 0;
    s->mbox1 = 0;
    s->dfifo = 0;
    s->ctest2 = LSI_CTEST2_DACK;
    s->ctest3 = 0;
    s->ctest4 = 0;
    s->ctest5 = 0;
    s->dsp = 0;
    s->dsps = 0;
    s->dmode = 0;
    s->dcntl = 0;
    s->scntl0 = 0xc0;
    s->scntl1 = 0;
    s->scntl2 = 0;
    s->scntl3 = 0;
    s->sstat0 = 0;
    s->sstat1 = 0;
    s->scid = 7;
    s->sxfer = 0;
    s->socl = 0;
    s->sdid = 0;
    s->ssid = 0;
    s->stest1 =  0;
    s->stest2 = 0;
    s->stest3 = 0;
    s->sidl = 0;
    s->stime0 = 0;
    s->respid0 = 0x80;
    s->respid1 = 0;
    s->sbr = 0;
    s->last_level = 0;
    s->gpreg0 = 0;
    s->sstop = 1;
    s->prefetch = 0;
    s->prefetch_used = 0;
}


static void lsi_read(LSIState *s, uint32_t addr, uint8_t *buf, uint32_t len)
{
	int i = 0;

	ncr53c810_log("lsi_read(): %08X-%08X, length %i\n", addr, (addr + len - 1), len);

	if (s->dmode & LSI_DMODE_SIOM) {
		ncr53c810_log("NCR 810: Reading from I/O address %04X\n", (uint16_t) addr);
		for (i = 0; i < len; i++)
			buf[i] = inb((uint16_t) (addr + i));
	} else {
		ncr53c810_log("NCR 810: Reading from memory address %08X\n", addr);
        	DMAPageRead(addr, buf, len);
	}
}

static void lsi_write(LSIState *s, uint32_t addr, uint8_t *buf, uint32_t len)
{
	int i = 0;

	ncr53c810_log("lsi_write(): %08X-%08X, length %i\n", addr, (addr + len - 1), len);

	if (s->dmode & LSI_DMODE_DIOM) {
		ncr53c810_log("NCR 810: Writing to I/O address %04X\n", (uint16_t) addr);
		for (i = 0; i < len; i++)
			outb((uint16_t) (addr + i), buf[i]);
	} else {
		ncr53c810_log("NCR 810: Writing to memory address %08X\n", addr);
        	DMAPageWrite(addr, buf, len);
	}
}

static inline uint32_t read_dword(LSIState *s, uint32_t addr)
{
    uint32_t buf;
    ncr53c810_log("Reading the next DWORD from memory (%08X)...\n", addr);
    DMAPageRead(addr, (uint8_t *)&buf, 4);
    return cpu_to_le32(buf);
}

static void do_irq(LSIState *s, int level)
{
    if (level) {
    	pci_set_irq(s->pci_slot, PCI_INTA);
	ncr53c810_log("Raising IRQ...\n");
    } else {
    	pci_clear_irq(s->pci_slot, PCI_INTA);
	ncr53c810_log("Lowering IRQ...\n");
    }
}

static void lsi_update_irq(LSIState *s)
{
    int level;
    lsi_request *p;

    /* It's unclear whether the DIP/SIP bits should be cleared when the
       Interrupt Status Registers are cleared or when istat0 is read.
       We currently do the formwer, which seems to work.  */
    level = 0;
    if (s->dstat & 0x7f) {
        if ((s->dstat & s->dien) & 0x7f)
            level = 1;
        s->istat0 |= LSI_ISTAT0_DIP;
    } else {
        s->istat0 &= ~LSI_ISTAT0_DIP;
    }

    if (s->sist0 || s->sist1) {
        if ((s->sist0 & s->sien0) || (s->sist1 & s->sien1))
            level = 1;
        s->istat0 |= LSI_ISTAT0_SIP;
    } else {
        s->istat0 &= ~LSI_ISTAT0_SIP;
    }
    if (s->istat0 & LSI_ISTAT0_INTF) {
        level = 1;
    }

    if (level != s->last_level) {
        DPRINTF("Update IRQ level %d dstat %02x sist %02x%02x\n",
                level, s->dstat, s->sist1, s->sist0);
        s->last_level = level;
	do_irq(s, level);	/* Only do something with the IRQ if the new level differs from the previous one. */
    }

    if (!level && lsi_irq_on_rsl(s) && !(s->scntl1 & LSI_SCNTL1_CON)) {
        DPRINTF("Handled IRQs & disconnected, looking for pending "
                "processes\n");
        QTAILQ_FOREACH(p, &s->queue, next) {
            if (p->pending) {
                lsi_reselect(s, p);
                break;
            }
        }
    }
}

/* Stop SCRIPTS execution and raise a SCSI interrupt.  */
static void lsi_script_scsi_interrupt(LSIState *s, int stat0, int stat1)
{
    uint32_t mask0;
    uint32_t mask1;

    DPRINTF("SCSI Interrupt 0x%02x%02x prev 0x%02x%02x\n",
            stat1, stat0, s->sist1, s->sist0);
    s->sist0 |= stat0;
    s->sist1 |= stat1;
    /* Stop processor on fatal or unmasked interrupt.  As a special hack
       we don't stop processing when raising STO.  Instead continue
       execution and stop at the next insn that accesses the SCSI bus.  */
    mask0 = s->sien0 | ~(LSI_SIST0_CMP | LSI_SIST0_SEL | LSI_SIST0_RSL);
    mask1 = s->sien1 | ~(LSI_SIST1_GEN | LSI_SIST1_HTH);
    mask1 &= ~LSI_SIST1_STO;
    if (s->sist0 & mask0 || s->sist1 & mask1) {
	ncr53c810_log("NCR 810: IRQ-mandated stop\n");
	s->sstop = 1;
    }
    lsi_update_irq(s);
}

/* Stop SCRIPTS execution and raise a DMA interrupt.  */
static void lsi_script_dma_interrupt(LSIState *s, int stat)
{
    DPRINTF("DMA Interrupt 0x%x prev 0x%x\n", stat, s->dstat);
    s->dstat |= stat;
    lsi_update_irq(s);
    s->sstop = 1;
}

static inline void lsi_set_phase(LSIState *s, int phase)
{
    s->sstat1 = (s->sstat1 & ~PHASE_MASK) | phase;
}

static void lsi_bad_phase(LSIState *s, int out, int new_phase)
{
    /* Trigger a phase mismatch.  */
    DPRINTF("Phase mismatch interrupt\n");
    lsi_script_scsi_interrupt(s, LSI_SIST0_MA, 0);
    s->sstop = 1;
    lsi_set_phase(s, new_phase);
}


/* Resume SCRIPTS execution after a DMA operation.  */
static void lsi_resume_script(LSIState *s)
{
    ncr53c810_log("lsi_resume_script()\n");
    if (s->waiting != 2) {
        s->waiting = 0;
        lsi_execute_script(s);
    } else {
        s->waiting = 0;
    }
}

static void lsi_disconnect(LSIState *s)
{
    s->scntl1 &= ~LSI_SCNTL1_CON;
    s->sstat1 &= ~PHASE_MASK;
    if (s->dcmd & 0x01)		/* Select with ATN */
	s->sstat1 |= 0x07;
}

static void lsi_bad_selection(LSIState *s, uint32_t id)
{
    DPRINTF("Selected absent target %d\n", id);
    lsi_script_scsi_interrupt(s, 0, LSI_SIST1_STO);
    lsi_disconnect(s);
}

static void lsi_do_dma(LSIState *s, int out, uint8_t id)
{
    uint32_t addr, count, tdbc;

    scsi_device_t *dev;

    dev = &SCSIDevices[id][s->current_lun];

    if ((((id) == -1) && !scsi_device_present(id, s->current_lun))) {
    	DPRINTF("(ID=%02i LUN=%02i) SCSI Command 0x%02x: Device not present when attempting to do DMA\n", id, s->current_lun, s->last_command);
	return;
    }
	
    if (!s->current->dma_len) {
        /* Wait until data is available.  */
    	DPRINTF("(ID=%02i LUN=%02i) SCSI Command 0x%02x: DMA no data available\n", id, s->current_lun, s->last_command);
        return;
    }

    /* Make sure count is never bigger than BufferLength. */
    count = tdbc = s->dbc;
    if (count > s->temp_buf_len)
	count = s->temp_buf_len;

    addr = s->dnad;

    DPRINTF("(ID=%02i LUN=%02i) SCSI Command 0x%02x: DMA addr=0x%08x len=%d cur_len=%d s->dbc=%d\n", id, s->current_lun, s->last_command, s->dnad, s->temp_buf_len, count, tdbc);
    s->dnad += count;
    s->dbc -= count;

    if (out) {
        lsi_read(s, addr, dev->CmdBuffer+s->buffer_pos, count);
    }  else {
	if (!s->buffer_pos) {
    		DPRINTF("(ID=%02i LUN=%02i) SCSI Command 0x%02x: SCSI Command Phase 1 on PHASE_DI\n", id, s->current_lun, s->last_command);
		scsi_device_command_phase1(s->current->tag, s->current_lun);
	}
        lsi_write(s, addr, dev->CmdBuffer+s->buffer_pos, count);
    }

    s->temp_buf_len -= count;
    s->buffer_pos += count;

    if (s->temp_buf_len <= 0) {
	if (out) {
    		DPRINTF("(ID=%02i LUN=%02i) SCSI Command 0x%02x: SCSI Command Phase 1 on PHASE_DO\n", id, s->current_lun, s->last_command);
		scsi_device_command_phase1(id, s->current_lun);
	}
	if (dev->CmdBuffer != NULL) {
		free(dev->CmdBuffer);
		dev->CmdBuffer = NULL;
	}
	lsi_command_complete(s, SCSIStatus);
    } else {
    	DPRINTF("(ID=%02i LUN=%02i) SCSI Command 0x%02x: Resume SCRIPTS\n", id, s->current_lun, s->last_command);
	lsi_resume_script(s);
    }
}

/* Queue a byte for a MSG IN phase.  */
static void lsi_add_msg_byte(LSIState *s, uint8_t data)
{
    if (s->msg_len >= LSI_MAX_MSGIN_LEN) {
        BADF("MSG IN data too long\n");
    } else {
        DPRINTF("MSG IN 0x%02x\n", data);
        s->msg[s->msg_len++] = data;
    }
}

/* Perform reselection to continue a command.  */
static void lsi_reselect(LSIState *s, lsi_request *p)
{
    int id;

    if (!s->current)
	return;
	
    QTAILQ_REMOVE(&s->queue, p, next);
    s->current = p;

    id = (p->tag >> 8) & 0xf;
    s->ssid = id | 0x80;
    /* LSI53C700 Family Compatibility, see LSI53C895A 4-73 */
    if (!(s->dcntl & LSI_DCNTL_COM)) {
        s->sfbr = 1 << (id & 0x7);
    }
    DPRINTF("Reselected target %d\n", id);
    s->scntl1 |= LSI_SCNTL1_CON;
    lsi_set_phase(s, PHASE_MI);
    s->msg_action = p->out ? 2 : 3;
    s->current->dma_len = p->pending;
    lsi_add_msg_byte(s, 0x80);
    if (s->current->tag & LSI_TAG_VALID) {
        lsi_add_msg_byte(s, 0x20);
        lsi_add_msg_byte(s, p->tag & 0xff);
    }

    if (lsi_irq_on_rsl(s)) {
        lsi_script_scsi_interrupt(s, LSI_SIST0_RSL, 0);
    }
}


static lsi_request *lsi_find_by_tag(LSIState *s, uint32_t tag)
{
    lsi_request *p;

    QTAILQ_FOREACH(p, &s->queue, next) 
	{
        if (p->tag == tag) {
            return p;
        }
    }

    return NULL;
}

static void lsi_request_free(LSIState *s, lsi_request *p)
{
    if (p == s->current) {
	if (s->current) {
		free(s->current);
        	s->current = NULL;
		return;		/* If s->current is p, we do *NOT* need to free it a second time. */
	}
    } else {
        QTAILQ_REMOVE(&s->queue, p, next);
    }
    if (p) {
	free(p);
	p = NULL;
    }
}

/* Callback to indicate that the SCSI layer has completed a command.  */
static void lsi_command_complete(void *priv, uint32_t status)
{
    LSIState *s = (LSIState *)priv;
    int out;
	
    out = (s->sstat1 & PHASE_MASK) == PHASE_DO;
    DPRINTF("(ID=%02i LUN=%02i) SCSI Command 0x%02x: Command complete status=%d\n", s->current->tag, s->current_lun, s->last_command, (int)status);
    s->status = status;
    s->command_complete = 2;	
    if (s->waiting && s->dbc != 0) {
        /* Raise phase mismatch for short transfers.  */
        lsi_bad_phase(s, out, PHASE_ST);
    } else {
        lsi_set_phase(s, PHASE_ST);
    }

    if (s->hba_private == s->current) {
        s->hba_private = NULL;
        lsi_request_free(s, s->current);
    }
    lsi_resume_script(s);
}

static void lsi_do_command(LSIState *s, uint8_t id)
{
    scsi_device_t *dev;
    uint8_t buf[12];

    memset(buf, 0, 12);
    DMAPageRead(s->dnad, buf, MIN(12, s->dbc));
    if (s->dbc > 12) {
    	DPRINTF("(ID=%02i LUN=%02i) SCSI Command 0x%02x: CDB length %i too big\n", id, s->current_lun, buf[0], s->dbc);
        s->dbc = 12;
    }
    s->sfbr = buf[0];
    s->command_complete = 0;

    dev = &SCSIDevices[id][s->current_lun];
    if (((id == -1) || !scsi_device_present(id, s->current_lun))) {
    	DPRINTF("(ID=%02i LUN=%02i) SCSI Command 0x%02x: Bad Selection\n", id, s->current_lun, buf[0]);
        lsi_bad_selection(s, id);
        return;
    }
	
    s->current = (lsi_request*)malloc(sizeof(lsi_request));
    s->current->tag = id;

    dev->BufferLength = -1;

    DPRINTF("(ID=%02i LUN=%02i) SCSI Command 0x%02x: DBC=%i\n", id, s->current_lun, buf[0], s->dbc);
    s->last_command = buf[0];

    scsi_device_command_phase0(s->current->tag, s->current_lun, s->dbc, buf);
    s->hba_private = (void *)s->current;

    s->waiting = 0;
    s->buffer_pos = 0;

    s->temp_buf_len = dev->BufferLength;

    if (dev->BufferLength > 0) {
	dev->CmdBuffer = (uint8_t *)malloc(dev->BufferLength);
	s->current->dma_len = dev->BufferLength;
    }

    if ((SCSIPhase == SCSI_PHASE_DATA_IN) && (dev->BufferLength > 0)) {
	DPRINTF("(ID=%02i LUN=%02i) SCSI Command 0x%02x: PHASE_DI\n", id, s->current_lun, buf[0]);
	lsi_set_phase(s, PHASE_DI);
    } else if ((SCSIPhase == SCSI_PHASE_DATA_OUT) && (dev->BufferLength > 0)) {
	DPRINTF("(ID=%02i LUN=%02i) SCSI Command 0x%02x: PHASE_DO\n", id, s->current_lun, buf[0]);
	lsi_set_phase(s, PHASE_DO);
    } else {
	lsi_command_complete(s, SCSIStatus);
    }
}

static void lsi_do_status(LSIState *s)
{
    uint8_t status;
    DPRINTF("Get status len=%d status=%d\n", s->dbc, s->status);
    if (s->dbc != 1)
        BADF("Bad Status move\n");
    s->dbc = 1;
    status = s->status;
    s->sfbr = status;
    lsi_write(s, s->dnad, &status, 1);
    lsi_set_phase(s, PHASE_MI);
    s->msg_action = 1;
    lsi_add_msg_byte(s, 0); /* COMMAND COMPLETE */
}

static void lsi_do_msgin(LSIState *s)
{
    int len;
    DPRINTF("Message in len=%d/%d\n", s->dbc, s->msg_len);
    s->sfbr = s->msg[0];
    len = s->msg_len;
    if (len > s->dbc)
        len = s->dbc;
    lsi_write(s, s->dnad, s->msg, len);
    /* Linux drivers rely on the last byte being in the SIDL.  */
    s->sidl = s->msg[len - 1];
    s->msg_len -= len;
    if (s->msg_len) {
        memmove(s->msg, s->msg + len, s->msg_len);
    } else 
	{
        /* ??? Check if ATN (not yet implemented) is asserted and maybe
           switch to PHASE_MO.  */
        switch (s->msg_action) {
        case 0:
            lsi_set_phase(s, PHASE_CMD);
            break;
        case 1:
            lsi_disconnect(s);
            break;
        case 2:
            lsi_set_phase(s, PHASE_DO);
            break;
        case 3:
            lsi_set_phase(s, PHASE_DI);
            break;
        default:
            abort();
        }
    }
}

/* Read the next byte during a MSGOUT phase.  */
static uint8_t lsi_get_msgbyte(LSIState *s)
{
    uint8_t data;
    DMAPageRead(s->dnad, &data, 1);
    s->dnad++;
    s->dbc--;
    return data;
}

/* Skip the next n bytes during a MSGOUT phase. */
static void lsi_skip_msgbytes(LSIState *s, unsigned int n)
{
    s->dnad += n;
    s->dbc  -= n;
}

static void lsi_do_msgout(LSIState *s, uint8_t id)
{
    uint8_t msg;
    int len;
    uint32_t current_tag;
    lsi_request *current_req;
    scsi_device_t *dev;

    dev = &SCSIDevices[id][s->current_lun];

    if (s->current) {
        current_tag = s->current->tag;
        current_req = s->current;
    } else {
        current_tag = id;
        current_req = lsi_find_by_tag(s, current_tag);
    }

    DPRINTF("MSG out len=%d\n", s->dbc);
    while (s->dbc) {
        msg = lsi_get_msgbyte(s);
        s->sfbr = msg;

        switch (msg) {
        case 0x04:
            DPRINTF("MSG: Disconnect\n");
            lsi_disconnect(s);
            break;
        case 0x08:
            DPRINTF("MSG: No Operation\n");
            lsi_set_phase(s, PHASE_CMD);
            break;
        case 0x01:
            len = lsi_get_msgbyte(s);
            msg = lsi_get_msgbyte(s);
            (void)len; /* avoid a warning about unused variable*/
            DPRINTF("Extended message 0x%x (len %d)\n", msg, len);
            switch (msg) {
            case 1:
                DPRINTF("SDTR (ignored)\n");
                lsi_skip_msgbytes(s, 2);
                break;
            case 3:
                DPRINTF("WDTR (ignored)\n");
                lsi_skip_msgbytes(s, 1);
                break;
            default:
                goto bad;
            }
            break;
        case 0x20: /* SIMPLE queue */
            id |= lsi_get_msgbyte(s) | LSI_TAG_VALID;
            DPRINTF("SIMPLE queue tag=0x%x\n", id & 0xff);
            break;
        case 0x21: /* HEAD of queue */
            BADF("HEAD queue not implemented\n");
            id |= lsi_get_msgbyte(s) | LSI_TAG_VALID;
            break;
        case 0x22: /* ORDERED queue */
            BADF("ORDERED queue not implemented\n");
            id |= lsi_get_msgbyte(s) | LSI_TAG_VALID;
            break;
        case 0x0d:
            /* The ABORT TAG message clears the current I/O process only. */
            DPRINTF("MSG: ABORT TAG tag=0x%x\n", current_tag);
            if (current_req)  {
		if (dev->CmdBuffer) {
			free(dev->CmdBuffer);
			dev->CmdBuffer = NULL;
		}
            }
            lsi_disconnect(s);
            break;
        case 0x06:
        case 0x0e:
        case 0x0c:
            /* The ABORT message clears all I/O processes for the selecting
               initiator on the specified logical unit of the target. */
            if (msg == 0x06) {
                DPRINTF("MSG: ABORT tag=0x%x\n", current_tag);
            }
            /* The CLEAR QUEUE message clears all I/O processes for all
               initiators on the specified logical unit of the target. */
            if (msg == 0x0e) {
                DPRINTF("MSG: CLEAR QUEUE tag=0x%x\n", current_tag);
            }
            /* The BUS DEVICE RESET message clears all I/O processes for all
               initiators on all logical units of the target. */
            if (msg == 0x0c) {
                DPRINTF("MSG: BUS DEVICE RESET tag=0x%x\n", current_tag);
            }

            /* clear the current I/O process */
            if (s->current) {
		if (dev->CmdBuffer) {
			free(dev->CmdBuffer);
			dev->CmdBuffer = NULL;
		}
            }
            lsi_disconnect(s);
            break;
        default:
            if ((msg & 0x80) == 0) {
                goto bad;
            }
            s->current_lun = msg & 7;
            DPRINTF("Select LUN %d\n", s->current_lun);
            lsi_set_phase(s, PHASE_CMD);
            break;
        }
    }
    return;
bad:
    BADF("Unimplemented message 0x%02x\n", msg);
    lsi_set_phase(s, PHASE_MI);
    lsi_add_msg_byte(s, 7); /* MESSAGE REJECT */
    s->msg_action = 0;
}

#define LSI_BUF_SIZE 4096
static void lsi_memcpy(LSIState *s, uint32_t dest, uint32_t src, int count)
{
    int n;
    uint8_t buf[LSI_BUF_SIZE];

    DPRINTF("memcpy dest 0x%08x src 0x%08x count %d\n", dest, src, count);
    while (count) {
        n = (count > LSI_BUF_SIZE) ? LSI_BUF_SIZE : count;
        lsi_read(s, src, buf, n);
        lsi_write(s, dest, buf, n);
        src += n;
        dest += n;
        count -= n;
    }
}

static void lsi_wait_reselect(LSIState *s)
{
    lsi_request *p;

    DPRINTF("Wait Reselect\n");

    QTAILQ_FOREACH(p, &s->queue, next) {
        if (p->pending) {
            lsi_reselect(s, p);
            break;
        }
    }
    if (s->current == NULL) {
        s->waiting = 1;
    }
}

static void lsi_execute_script(LSIState *s)
{
    uint32_t insn;
    uint32_t addr;
    int opcode;
    int insn_processed = 0;
    uint32_t id;

    /* s->istat1 |= LSI_ISTAT1_SRUN; */
    s->sstop = 0;
again:
    insn_processed++;
    insn = read_dword(s, s->dsp);
    if (!insn) {
        /* If we receive an empty opcode increment the DSP by 4 bytes
           instead of 8 and execute the next opcode at that location */
        s->dsp += 4;
        goto again;
    }
    addr = read_dword(s, s->dsp + 4);
    DPRINTF("SCRIPTS dsp=%08x opcode %08x arg %08x\n", s->dsp, insn, addr);
    s->dsps = addr;
    s->dcmd = insn >> 24;
    s->dsp += 8;
			
    switch (insn >> 30) {
    case 0: /* Block move.  */
	ncr53c810_log("00: Block move\n");
        if (s->sist1 & LSI_SIST1_STO) {
            DPRINTF("Delayed select timeout\n");
	    s->sstop = 1;
            break;
        }
	DPRINTF("Block Move DBC=%d\n", s->dbc);
        s->dbc = insn & 0xffffff;
	DPRINTF("Block Move DBC=%d now\n", s->dbc);
        /* ??? Set ESA.  */
        if (insn & (1 << 29)) {
            /* Indirect addressing.  */
	    /* Should this respect SIOM? */
            addr = read_dword(s, addr);
	    ncr53c810_log("Indirect Block Move address: %08X\n", addr);
        } else if (insn & (1 << 28)) {
            uint32_t buf[2];
            int32_t offset;
            /* Table indirect addressing.  */

            /* 32-bit Table indirect */
            offset = sextract32(addr, 0, 24);
            DMAPageRead(s->dsa + offset, (uint8_t *)buf, 8);
            /* byte count is stored in bits 0:23 only */
            s->dbc = cpu_to_le32(buf[0]) & 0xffffff;
            addr = cpu_to_le32(buf[1]);

            /* 40-bit DMA, upper addr bits [39:32] stored in first DWORD of
             * table, bits [31:24] */
        }
        if ((s->sstat1 & PHASE_MASK) != ((insn >> 24) & 7)) {
            DPRINTF("Wrong phase got %d expected %d\n",
                    s->sstat1 & PHASE_MASK, (insn >> 24) & 7);
            lsi_script_scsi_interrupt(s, LSI_SIST0_MA, 0);
            break;
        }
        s->dnad = addr;
        switch (s->sstat1 & 0x7) {
        case PHASE_DO:
	    DPRINTF("Data Out Phase\n");
            s->waiting = 0;
            lsi_do_dma(s, 1, s->sdid);
            break;
        case PHASE_DI:
	    DPRINTF("Data In Phase\n");
            s->waiting = 0;
            lsi_do_dma(s, 0, s->sdid);
            break;
        case PHASE_CMD:
	    DPRINTF("Command Phase\n");
            lsi_do_command(s, s->sdid);
            break;
        case PHASE_ST:
	    DPRINTF("Status Phase\n");
            lsi_do_status(s);
            break;
        case PHASE_MO:
	    DPRINTF("MSG Out Phase\n");
            lsi_do_msgout(s, s->sdid);
            break;
        case PHASE_MI:
	    DPRINTF("MSG In Phase\n");
            lsi_do_msgin(s);
            break;
        default:
            BADF("Unimplemented phase %d\n", s->sstat1 & PHASE_MASK);
        }
        s->dfifo = s->dbc & 0xff;
        s->ctest5 = (s->ctest5 & 0xfc) | ((s->dbc >> 8) & 3);
        break;

    case 1: /* IO or Read/Write instruction.  */
	ncr53c810_log("01: I/O or Read/Write instruction\n");
        opcode = (insn >> 27) & 7;
        if (opcode < 5) {
	    if (insn & (1 << 25)) {
		id = read_dword(s, s->dsa + sextract32(insn, 0, 24));
	    } else {
		id = insn;
	    }
	    id = (id >> 16) & 0xf;
	    if (insn & (1 << 26)) {
		addr = s->dsp + sextract32(addr, 0, 24);
	    }
	    s->dnad = addr;
            switch (opcode) {
            case 0: /* Select */
                s->sdid = id;
                if (s->scntl1 & LSI_SCNTL1_CON) {
                    DPRINTF("Already reselected, jumping to alternative address\n");
                    s->dsp = s->dnad;
                    break;
                }
                s->sstat0 |= LSI_SSTAT0_WOA;
                s->scntl1 &= ~LSI_SCNTL1_IARB;
                if (((id == -1) || !scsi_device_present(id, 0))) {
                    lsi_bad_selection(s, id);
                    break;
                }
                DPRINTF("Selected target %d%s\n",
                        id, insn & (1 << 3) ? " ATN" : "");
                /* ??? Linux drivers compain when this is set.  Maybe
                   it only applies in low-level mode (unimplemented).
                lsi_script_scsi_interrupt(s, LSI_SIST0_CMP, 0); */
                s->select_id = id << 8;
                s->scntl1 |= LSI_SCNTL1_CON;
                if (insn & (1 << 3)) {
                    s->socl |= LSI_SOCL_ATN;
                }
                lsi_set_phase(s, PHASE_MO);
		s->waiting = 0;
                break;
            case 1: /* Disconnect */
                DPRINTF("Wait Disconnect\n");
                s->scntl1 &= ~LSI_SCNTL1_CON;
                break;
            case 2: /* Wait Reselect */
		ncr53c810_log("Wait Reselect\n");
                if (!lsi_irq_on_rsl(s)) {
                    lsi_wait_reselect(s);
                }
                break;
            case 3: /* Set */
                DPRINTF("Set%s%s%s%s\n",
                        insn & (1 << 3) ? " ATN" : "",
                        insn & (1 << 6) ? " ACK" : "",
                        insn & (1 << 9) ? " TM" : "",
                        insn & (1 << 10) ? " CC" : "");
                if (insn & (1 << 3)) {
                    s->socl |= LSI_SOCL_ATN;
                    lsi_set_phase(s, PHASE_MO);
                }
                if (insn & (1 << 9)) {
                    BADF("Target mode not implemented\n");
                }
                if (insn & (1 << 10))
                    s->carry = 1;
                break;
            case 4: /* Clear */
                DPRINTF("Clear%s%s%s%s\n",
                        insn & (1 << 3) ? " ATN" : "",
                        insn & (1 << 6) ? " ACK" : "",
                        insn & (1 << 9) ? " TM" : "",
                        insn & (1 << 10) ? " CC" : "");
                if (insn & (1 << 3)) {
                    s->socl &= ~LSI_SOCL_ATN;
                }
                if (insn & (1 << 10))
                    s->carry = 0;
                break;
            }
        } else {
            uint8_t op0;
            uint8_t op1;
            uint8_t data8;
            int reg;
            int operator;
#ifdef DEBUG_LSI
            static const char *opcode_names[3] =
                {"Write", "Read", "Read-Modify-Write"};
            static const char *operator_names[8] =
                {"MOV", "SHL", "OR", "XOR", "AND", "SHR", "ADD", "ADC"};
#endif

            reg = ((insn >> 16) & 0x7f) | (insn & 0x80);
            data8 = (insn >> 8) & 0xff;
            opcode = (insn >> 27) & 7;
            operator = (insn >> 24) & 7;
#ifdef DEBUG_LSI
            DPRINTF("%s reg 0x%x %s data8=0x%02x sfbr=0x%02x%s\n",
                    opcode_names[opcode - 5], reg,
                    operator_names[operator], data8, s->sfbr,
                    (insn & (1 << 23)) ? " SFBR" : "");
#endif
            op0 = op1 = 0;
            switch (opcode) {
            case 5: /* From SFBR */
                op0 = s->sfbr;
                op1 = data8;
                break;
            case 6: /* To SFBR */
                if (operator)
                    op0 = lsi_reg_readb(s, reg);
                op1 = data8;
                break;
            case 7: /* Read-modify-write */
                if (operator)
                    op0 = lsi_reg_readb(s, reg);
                if (insn & (1 << 23)) {
                    op1 = s->sfbr;
                } else {
                    op1 = data8;
                }
                break;
            }

            switch (operator) {
            case 0: /* move */
                op0 = op1;
                break;
            case 1: /* Shift left */
                op1 = op0 >> 7;
                op0 = (op0 << 1) | s->carry;
                s->carry = op1;
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
                op0 = (op0 >> 1) | (s->carry << 7);
                s->carry = op1;
                break;
            case 6: /* ADD */
                op0 += op1;
                s->carry = op0 < op1;
                break;
            case 7: /* ADC */
                op0 += op1 + s->carry;
                if (s->carry)
                    s->carry = op0 <= op1;
                else
                    s->carry = op0 < op1;
                break;
            }

            switch (opcode) {
            case 5: /* From SFBR */
            case 7: /* Read-modify-write */
                lsi_reg_writeb(s, reg, op0);
                break;
            case 6: /* To SFBR */
                s->sfbr = op0;
                break;
            }
        }
        break;

    case 2: /* Transfer Control.  */
	ncr53c810_log("02: Transfer Control\n");
        {
            int cond;
            int jmp;

            if ((insn & 0x002e0000) == 0) {
                DPRINTF("NOP\n");
                break;
            }
            if (s->sist1 & LSI_SIST1_STO) {
                DPRINTF("Delayed select timeout\n");
		s->sstop = 1;
                break;
            }
            cond = jmp = (insn & (1 << 19)) != 0;
            if (cond == jmp && (insn & (1 << 21))) {
                DPRINTF("Compare carry %d\n", s->carry == jmp);
                cond = s->carry != 0;
            }
            if (cond == jmp && (insn & (1 << 17))) {
                DPRINTF("Compare phase %d %c= %d\n",
                        (s->sstat1 & PHASE_MASK),
                        jmp ? '=' : '!',
                        ((insn >> 24) & 7));
                cond = (s->sstat1 & PHASE_MASK) == ((insn >> 24) & 7);
            }
            if (cond == jmp && (insn & (1 << 18))) {
                uint8_t mask;

                mask = (~insn >> 8) & 0xff;
                DPRINTF("Compare data 0x%x & 0x%x %c= 0x%x\n",
                        s->sfbr, mask, jmp ? '=' : '!', insn & mask);
                cond = (s->sfbr & mask) == (insn & mask);
            }
            if (cond == jmp) {
                if (insn & (1 << 23)) {
                    /* Relative address.  */
                    addr = s->dsp + sextract32(addr, 0, 24);
                }
                switch ((insn >> 27) & 7) {
                case 0: /* Jump */
                    DPRINTF("Jump to 0x%08x\n", addr);
		    s->adder = addr;
                    s->dsp = addr;
                    break;
                case 1: /* Call */
                    DPRINTF("Call 0x%08x\n", addr);
                    s->temp = s->dsp;
                    s->dsp = addr;
                    break;
                case 2: /* Return */
                    DPRINTF("Return to 0x%08x\n", s->temp);
                    s->dsp = s->temp;
                    break;
                case 3: /* Interrupt */
                    DPRINTF("Interrupt 0x%08x\n", s->dsps);
                    if ((insn & (1 << 20)) != 0) {
                        s->istat0 |= LSI_ISTAT0_INTF;
                        lsi_update_irq(s);
                    } else {
                        lsi_script_dma_interrupt(s, LSI_DSTAT_SIR);
                    }
                    break;
                default:
                    DPRINTF("Illegal transfer control\n");
                    lsi_script_dma_interrupt(s, LSI_DSTAT_IID);
                    break;
                }
            } else {
                DPRINTF("Control condition failed\n");
            }
        }
        break;

    case 3:
	ncr53c810_log("00: Memory move\n");
        if ((insn & (1 << 29)) == 0) {
            /* Memory move.  */
            uint32_t dest;
            /* ??? The docs imply the destination address is loaded into
               the TEMP register.  However the Linux drivers rely on
               the value being presrved.  */
            dest = read_dword(s, s->dsp);
            s->dsp += 4;
            lsi_memcpy(s, dest, addr, insn & 0xffffff);
        } else {
            uint8_t data[7];
	    uint8_t *pp = data;
            int reg;
            int n;
            int i;

            if (insn & (1 << 28)) {
                addr = s->dsa + sextract32(addr, 0, 24);
            }
            n = (insn & 7);
            reg = (insn >> 16) & 0xff;
            if (insn & (1 << 24)) {
                DMAPageRead(addr, data, n);
                DPRINTF("Load reg 0x%x size %d addr 0x%08x = %08x\n",
			reg, n, addr, *(unsigned *)pp);
                for (i = 0; i < n; i++) {
                    lsi_reg_writeb(s, reg + i, data[i]);
                }
            } else {
                DPRINTF("Store reg 0x%x size %d addr 0x%08x\n", reg, n, addr);
                for (i = 0; i < n; i++) {
                    data[i] = lsi_reg_readb(s, reg + i);
                }
                DMAPageWrite(addr, data, n);
            }
	}
        break;

    default:
	ncr53c810_log("%02X: Unknown command\n", (uint8_t) (insn >> 30));
    }
	DPRINTF("instructions processed %i\n", insn_processed);
    if (insn_processed > 10000 && !s->waiting) 
	{
        /* Some windows drivers make the device spin waiting for a memory
           location to change.  If we have been executed a lot of code then
           assume this is the case and force an unexpected device disconnect.
           This is apparently sufficient to beat the drivers into submission.
         */
	ncr53c810_log("Some windows drivers make the device spin...\n");
        if (!(s->sien0 & LSI_SIST0_UDC))
            ncr53c810_log("inf. loop with UDC masked\n");
        lsi_script_scsi_interrupt(s, LSI_SIST0_UDC, 0);
        lsi_disconnect(s);
    } else if (!s->sstop && !s->waiting) {
        if (s->dcntl & LSI_DCNTL_SSM) {
	    ncr53c810_log("NCR 810: SCRIPTS: Single-step mode\n");
            lsi_script_dma_interrupt(s, LSI_DSTAT_SSI);
        } else {
	    ncr53c810_log("NCR 810: SCRIPTS: Normal mode\n");
            goto again;
        }
    } else {
	if (s->sstop)
		ncr53c810_log("NCR 810: SCRIPTS: Stopped\n");
	if (s->waiting)
		ncr53c810_log("NCR 810: SCRIPTS: Waiting\n");
    }

    DPRINTF("SCRIPTS execution stopped\n");
}

static void lsi_reg_writeb(LSIState *s, uint32_t offset, uint8_t val)
{
    uint8_t tmp = 0;

#define CASE_SET_REG24(name, addr) \
    case addr    : s->name &= 0xffffff00; s->name |= val;       break; \
    case addr + 1: s->name &= 0xffff00ff; s->name |= val << 8;  break; \
    case addr + 2: s->name &= 0xff00ffff; s->name |= val << 16; break;

#define CASE_SET_REG32(name, addr) \
    case addr    : s->name &= 0xffffff00; s->name |= val;       break; \
    case addr + 1: s->name &= 0xffff00ff; s->name |= val << 8;  break; \
    case addr + 2: s->name &= 0xff00ffff; s->name |= val << 16; break; \
    case addr + 3: s->name &= 0x00ffffff; s->name |= val << 24; break;

// #ifdef DEBUG_LSI_REG
    DPRINTF("Write reg %02x = %02x\n", offset, val);
// #endif

    s->regop = 1;

    switch (offset) {
    case 0x00: /* SCNTL0 */
        s->scntl0 = val;
        if (val & LSI_SCNTL0_START) {
	    /* Looks like this (turn on bit 4 of SSTAT0 to mark arbitration in progress)
	       is enough to make BIOS v4.x happy. */
	    ncr53c810_log("NCR 810: Selecting SCSI ID %i\n", s->sdid);
	    s->select_id = s->sdid;
	    s->sstat0 |= 0x10;
        }
        break;
    case 0x01: /* SCNTL1 */
        s->scntl1 = val & ~LSI_SCNTL1_SST;
        if (val & LSI_SCNTL1_IARB) {
	    s->select_id = s->sdid;
	    s->waiting = 0;
	    ncr53c810_log("Waiting for reselect...\n");
	    lsi_wait_reselect(s);
	    if (!s->waiting) {
		ncr53c810_log("Arbitration won\n");
	    	s->sstat0 |= 0x04;
		s->scntl1 &= ~LSI_SCNTL1_IARB;
	    } else {
	        ncr53c810_log("Arbitration lost\n");
	    	s->sstat0 |= 0x08;
	    }
	    s->waiting = 0;
        }
        if (val & LSI_SCNTL1_RST) {
            if (!(s->sstat0 & LSI_SSTAT0_RST)) {
                s->sstat0 |= LSI_SSTAT0_RST;
		lsi_script_scsi_interrupt(s, LSI_SIST0_RST, 0);
            }
        } else {
            s->sstat0 &= ~LSI_SSTAT0_RST;
        }
        break;
    case 0x02: /* SCNTL2 */
        val &= ~(LSI_SCNTL2_WSR | LSI_SCNTL2_WSS);
        s->scntl2 = val;
        break;
    case 0x03: /* SCNTL3 */
        s->scntl3 = val;
        break;
    case 0x04: /* SCID */
        s->scid = val;
        break;
    case 0x05: /* SXFER */
        s->sxfer = val;
        break;
    case 0x06: /* SDID */
        if ((s->ssid & 0x80) && (val & 0xf) != (s->ssid & 0xf)) {
            BADF("Destination ID does not match SSID\n");
        }
        s->sdid = val & 0xf;
        break;
    case 0x07: /* GPREG0 */
	ncr53c810_log("NCR 810: GPREG0 write %02X\n", val);
	s->gpreg0 = val & 0x03;
        break;
    case 0x08: /* SFBR */
        /* The CPU is not allowed to write to this register.  However the
           SCRIPTS register move instructions are.  */
        s->sfbr = val;
        break;
    case 0x09: /* SOCL */
	ncr53c810_log("NCR 810: SOCL write %02X\n", val);
	s->socl = val;
	break;
    case 0x0a: case 0x0b:
        /* Openserver writes to these readonly registers on startup */
	return;
    case 0x0c: case 0x0d: case 0x0e: case 0x0f:
        /* Linux writes to these readonly registers on startup.  */
        return;
    CASE_SET_REG32(dsa, 0x10)
    case 0x14: /* ISTAT0 */
        ncr53c810_log("ISTAT0 write: %02X\n", val);
        tmp = s->istat0;
        s->istat0 = (s->istat0 & 0x0f) | (val & 0xf0);
        if ((val & LSI_ISTAT0_ABRT) && !(val & LSI_ISTAT0_SRST)) {
            lsi_script_dma_interrupt(s, LSI_DSTAT_ABRT);
        }
        if (val & LSI_ISTAT0_INTF) {
            s->istat0 &= ~LSI_ISTAT0_INTF;
            lsi_update_irq(s);
        }

        if (s->waiting == 1 && val & LSI_ISTAT0_SIGP) {
            DPRINTF("Woken by SIGP\n");
            s->waiting = 0;
            s->dsp = s->dnad;
            lsi_execute_script(s);
        }
        if ((val & LSI_ISTAT0_SRST) && !(tmp & LSI_ISTAT0_SRST)) {
	    lsi_soft_reset(s);
	    lsi_update_irq(s);
	    s->istat0/* = s->istat1*/ = 0;
        }
        break;
    case 0x16: /* MBOX0 */
        s->mbox0 = val;
        break;
    case 0x17: /* MBOX1 */
        s->mbox1 = val;
        break;
	case 0x18: /* CTEST0 */
		/* nothing to do */
		break;
	case 0x19: /* CTEST1 */
		/* nothing to do */
		break;
	case 0x1a: /* CTEST2 */
	s->ctest2 = val & LSI_CTEST2_PCICIE;
	break;
    case 0x1b: /* CTEST3 */
        s->ctest3 = val & 0x0f;
        break;
    CASE_SET_REG32(temp, 0x1c)
    case 0x21: /* CTEST4 */
        if (val & 7) {
           BADF("Unimplemented CTEST4-FBL 0x%x\n", val);
        }
        s->ctest4 = val;
        break;
    case 0x22: /* CTEST5 */
        if (val & (LSI_CTEST5_ADCK | LSI_CTEST5_BBCK)) {
            BADF("CTEST5 DMA increment not implemented\n");
        }
        s->ctest5 = val;
        break;
    CASE_SET_REG24(dbc, 0x24)
    CASE_SET_REG32(dnad, 0x28)
    case 0x2c: /* DSP[0:7] */
        s->dsp &= 0xffffff00;
        s->dsp |= val;
        break;
    case 0x2d: /* DSP[8:15] */
        s->dsp &= 0xffff00ff;
        s->dsp |= val << 8;
        break;
    case 0x2e: /* DSP[16:23] */
        s->dsp &= 0xff00ffff;
        s->dsp |= val << 16;
        break;
    case 0x2f: /* DSP[24:31] */
        s->dsp &= 0x00ffffff;
        s->dsp |= val << 24;
	if (!(s->dmode & LSI_DMODE_MAN)) {
            if (s->sstop)  lsi_execute_script(s);
	}
        break;
    CASE_SET_REG32(dsps, 0x30)
    CASE_SET_REG32(scratch[0], 0x34)
    case 0x38: /* DMODE */
        s->dmode = val;
        break;
    case 0x39: /* DIEN */
	ncr53c810_log("DIEN write: %02X\n", val);
        s->dien = val;
        lsi_update_irq(s);
        break;
    case 0x3a: /* SBR */
        s->sbr = val;
        break;
    case 0x3b: /* DCNTL */
        s->dcntl = val & ~(LSI_DCNTL_PFF | LSI_DCNTL_STD);
	if (val & LSI_DCNTL_STD)
            if (s->sstop)  lsi_execute_script(s);
        break;
    /* ADDER Output (Debug of relative jump address) */
    case 0x40: /* SIEN0 */
        s->sien0 = val;
        lsi_update_irq(s);
        break;
    case 0x41: /* SIEN1 */
        s->sien1 = val;
        lsi_update_irq(s);
        break;
    case 0x47: /* GPCNTL0 */
        break;
    case 0x48: /* STIME0 */
        s->stime0 = val;
        break;
    case 0x49: /* STIME1 */
        if (val & 0xf) {
            DPRINTF("General purpose timer not implemented\n");
            /* ??? Raising the interrupt immediately seems to be sufficient
               to keep the FreeBSD driver happy.  */
            lsi_script_scsi_interrupt(s, 0, LSI_SIST1_GEN);
        }
        break;
    case 0x4a: /* RESPID0 */
        s->respid0 = val;
        break;
    case 0x4b: /* RESPID1 */
        s->respid1 = val;
        break;
    case 0x4d: /* STEST1 */
        s->stest1 = val;
        break;
    case 0x4e: /* STEST2 */
        if (val & 1) {
            BADF("Low level mode not implemented\n");
        }
        s->stest2 = val;
        break;
    case 0x4f: /* STEST3 */
        if (val & 0x41) {
            BADF("SCSI FIFO test mode not implemented\n");
        }
        s->stest3 = val;
        break;
	case 0x54:
		break;
    default:
        if (offset >= 0x5c && offset < 0x60) {
            int n;
            int shift;
            n = (offset - 0x58) >> 2;
            shift = (offset & 3) * 8;
            s->scratch[n] = deposit32(s->scratch[n], shift, 8, val);
        } else {
            BADF("Unhandled writeb 0x%x = 0x%x\n", offset, val);
        }
    }
#undef CASE_SET_REG24
#undef CASE_SET_REG32
}

static uint8_t lsi_reg_readb(LSIState *s, uint32_t offset)
{
    uint8_t tmp;
#define CASE_GET_REG24(name, addr) \
    case addr: return s->name & 0xff; \
    case addr + 1: return (s->name >> 8) & 0xff; \
    case addr + 2: return (s->name >> 16) & 0xff;

#define CASE_GET_REG32(name, addr) \
    case addr: return s->name & 0xff; \
    case addr + 1: return (s->name >> 8) & 0xff; \
    case addr + 2: return (s->name >> 16) & 0xff; \
    case addr + 3: return (s->name >> 24) & 0xff;

    s->regop = 1;

    switch (offset) {
    case 0x00: /* SCNTL0 */
	ncr53c810_log("NCR 810: Read SCNTL0 %02X\n", s->scntl0);
        return s->scntl0;
    case 0x01: /* SCNTL1 */
	ncr53c810_log("NCR 810: Read SCNTL1 %02X\n", s->scntl1);
        return s->scntl1;
    case 0x02: /* SCNTL2 */
	ncr53c810_log("NCR 810: Read SCNTL2 %02X\n", s->scntl2);
        return s->scntl2;
    case 0x03: /* SCNTL3 */
	ncr53c810_log("NCR 810: Read SCNTL3 %02X\n", s->scntl3);
        return s->scntl3;
    case 0x04: /* SCID */
	ncr53c810_log("NCR 810: Read SCID %02X\n", s->scid);
        return s->scid;
    case 0x05: /* SXFER */
	ncr53c810_log("NCR 810: Read SXFER %02X\n", s->sxfer);
        return s->sxfer;
    case 0x06: /* SDID */
	ncr53c810_log("NCR 810: Read SDID %02X\n", s->sdid);
        return s->sdid;
    case 0x07: /* GPREG0 */
	ncr53c810_log("NCR 810: Read GPREG0 %02X\n", s->gpreg0 & 3);
        return s->gpreg0 & 3;
    case 0x08: /* Revision ID */
	ncr53c810_log("NCR 810: Read REVID 00\n");
        return 0x00;
    case 0xa: /* SSID */
	ncr53c810_log("NCR 810: Read SSID %02X\n", s->ssid);
        return s->ssid;
    case 0xb: /* SBCL */
        /* ??? This is not correct. However it's (hopefully) only
           used for diagnostics, so should be ok.  */
	/* Bit 7 = REQ (SREQ/ status)
	   Bit 6 = ACK (SACK/ status)
	   Bit 5 = BSY (SBSY/ status)
	   Bit 4 = SEL (SSEL/ status)
	   Bit 3 = ATN (SATN/ status)
	   Bit 2 = MSG (SMSG/ status)
	   Bit 1 = C/D (SC_D/ status)
	   Bit 0 = I/O (SI_O/ status) */
	tmp = (s->sstat1 & 7);
	ncr53c810_log("NCR 810: Read SBCL %02X\n", tmp);
	return tmp;	/* For now, return the MSG, C/D, and I/O bits from SSTAT1. */
    case 0xc: /* DSTAT */
        tmp = s->dstat | LSI_DSTAT_DFE;
        if ((s->istat0 & LSI_ISTAT0_INTF) == 0)
            s->dstat = 0;
        lsi_update_irq(s);
	ncr53c810_log("NCR 810: Read DSTAT %02X\n", tmp);
        return tmp;
    case 0x0d: /* SSTAT0 */
	ncr53c810_log("NCR 810: Read SSTAT0 %02X\n", s->sstat0);
        return s->sstat0;
    case 0x0e: /* SSTAT1 */
	ncr53c810_log("NCR 810: Read SSTAT1 %02X\n", s->sstat1);
        return s->sstat1;
    case 0x0f: /* SSTAT2 */
	ncr53c810_log("NCR 810: Read SSTAT2 %02X\n", s->scntl1 & LSI_SCNTL1_CON ? 0 : 2);
        return s->scntl1 & LSI_SCNTL1_CON ? 0 : 2;
    CASE_GET_REG32(dsa, 0x10)
    case 0x14: /* ISTAT0 */
	ncr53c810_log("NCR 810: Read ISTAT0 %02X\n", s->istat0);
        return s->istat0;
    case 0x15: /* ISTAT1 */
	ncr53c810_log("NCR 810: Read ISTAT1 %02X\n", s->istat1);
        return s->istat1;
    case 0x16: /* MBOX0 */
	ncr53c810_log("NCR 810: Read MBOX0 %02X\n", s->mbox0);
        return s->mbox0;
    case 0x17: /* MBOX1 */
	ncr53c810_log("NCR 810: Read MBOX1 %02X\n", s->mbox1);
        return s->mbox1;
    case 0x18: /* CTEST0 */
	ncr53c810_log("NCR 810: Read CTEST0 FF\n");
        return 0xff;
    case 0x19: /* CTEST1 */
	ncr53c810_log("NCR 810: Read CTEST1 F0\n");
        return 0xf0;	/* dma fifo empty */
    case 0x1a: /* CTEST2 */
        tmp = s->ctest2 | LSI_CTEST2_DACK | LSI_CTEST2_CM;
        if (s->istat0 & LSI_ISTAT0_SIGP) {
            s->istat0 &= ~LSI_ISTAT0_SIGP;
            tmp |= LSI_CTEST2_SIGP;
        }
	ncr53c810_log("NCR 810: Read CTEST2 %02X\n", tmp);
        return tmp;
    case 0x1b: /* CTEST3 */
	ncr53c810_log("NCR 810: Read CTEST3 %02X\n", (s->ctest3 & (0x08 | 0x02 | 0x01)) | s->chip_rev);
        return (s->ctest3 & (0x08 | 0x02 | 0x01)) | s->chip_rev;
    CASE_GET_REG32(temp, 0x1c)
    case 0x20: /* DFIFO */
	ncr53c810_log("NCR 810: Read DFIFO 00\n");
        return 0;
    case 0x21: /* CTEST4 */
	ncr53c810_log("NCR 810: Read CTEST4 %02X\n", s->ctest4);
        return s->ctest4;
    case 0x22: /* CTEST5 */
	ncr53c810_log("NCR 810: Read CTEST5 %02X\n", s->ctest5);
        return s->ctest5;
    case 0x23: /* CTEST6 */
	ncr53c810_log("NCR 810: Read CTEST6 00\n");
         return 0;
    CASE_GET_REG24(dbc, 0x24)
    case 0x27: /* DCMD */
	ncr53c810_log("NCR 810: Read DCMD %02X\n", s->dcmd);
        return s->dcmd;
    CASE_GET_REG32(dnad, 0x28)
    CASE_GET_REG32(dsp, 0x2c)
    CASE_GET_REG32(dsps, 0x30)
    CASE_GET_REG32(scratch[0], 0x34)
    case 0x38: /* DMODE */
	ncr53c810_log("NCR 810: Read DMODE %02X\n", s->dmode);
        return s->dmode;
    case 0x39: /* DIEN */
	ncr53c810_log("NCR 810: Read DIEN %02X\n", s->dien);
        return s->dien;
    case 0x3a: /* SBR */
	ncr53c810_log("NCR 810: Read SBR %02X\n", s->sbr);
        return s->sbr;
    case 0x3b: /* DCNTL */
	ncr53c810_log("NCR 810: Read DCNTL %02X\n", s->dcntl);
        return s->dcntl;
    /* ADDER Output (Debug of relative jump address) */
    CASE_GET_REG32(adder, 0x3c)
    case 0x40: /* SIEN0 */
	ncr53c810_log("NCR 810: Read SIEN0 %02X\n", s->sien0);
        return s->sien0;
    case 0x41: /* SIEN1 */
	ncr53c810_log("NCR 810: Read SIEN1 %02X\n", s->sien1);
        return s->sien1;
    case 0x42: /* SIST0 */
        tmp = s->sist0;
        s->sist0 = 0;
        lsi_update_irq(s);
	ncr53c810_log("NCR 810: Read SIST0 %02X\n", tmp);
        return tmp;
    case 0x43: /* SIST1 */
        tmp = s->sist1;
        s->sist1 = 0;
        lsi_update_irq(s);
	ncr53c810_log("NCR 810: Read SIST1 %02X\n", tmp);
        return tmp;
    case 0x46: /* MACNTL */
	ncr53c810_log("NCR 810: Read MACNTL 4F\n");
        return 0x4f;
    case 0x47: /* GPCNTL0 */
	ncr53c810_log("NCR 810: Read GPCNTL0 0F\n");
        return 0x0f;
    case 0x48: /* STIME0 */
	ncr53c810_log("NCR 810: Read STIME0 %02X\n", s->stime0);
        return s->stime0;
    case 0x4a: /* RESPID0 */
	ncr53c810_log("NCR 810: Read RESPID0 %02X\n", s->respid0);
        return s->respid0;
    case 0x4b: /* RESPID1 */
	ncr53c810_log("NCR 810: Read RESPID1 %02X\n", s->respid1);
        return s->respid1;
    case 0x4d: /* STEST1 */
	ncr53c810_log("NCR 810: Read STEST1 %02X\n", s->stest1);
        return s->stest1;
    case 0x4e: /* STEST2 */
	ncr53c810_log("NCR 810: Read STEST2 %02X\n", s->stest2);
        return s->stest2;
    case 0x4f: /* STEST3 */
	ncr53c810_log("NCR 810: Read STEST3 %02X\n", s->stest3);
        return s->stest3;
    case 0x50: /* SIDL */
        /* This is needed by the linux drivers.  We currently only update it
           during the MSG IN phase.  */
	ncr53c810_log("NCR 810: Read SIDL %02X\n", s->sidl);
        return s->sidl;
    case 0x52: /* STEST4 */
	ncr53c810_log("NCR 810: Read STEST4 E0\n");
        return 0xe0;
    case 0x58: /* SBDL */
        /* Some drivers peek at the data bus during the MSG IN phase.  */
        if ((s->sstat1 & PHASE_MASK) == PHASE_MI) {
	    ncr53c810_log("NCR 810: Read SBDL %02X\n", s->msg[0]);
            return s->msg[0];
	}
	ncr53c810_log("NCR 810: Read SBDL 00\n");
        return 0;
    case 0x59: /* SBDL high */
	ncr53c810_log("NCR 810: Read SBDLH 00\n");
        return 0;
    }
    if (offset >= 0x5c && offset < 0x60) {
        int n;
        int shift;
        n = (offset - 0x58) >> 2;
        shift = (offset & 3) * 8;
	ncr53c810_log("NCR 810: Read SCRATCH%i %02X\n", offset & 3, (s->scratch[n] >> shift) & 0xff);
        return (s->scratch[n] >> shift) & 0xff;
    }
    BADF("readb 0x%x\n", offset);
	return 0;
#undef CASE_GET_REG24
#undef CASE_GET_REG32
}

static uint8_t lsi_io_readb(uint16_t addr, void *p)
{
    LSIState *s = (LSIState *)p;
    return lsi_reg_readb(s, addr & 0xff);
}

static uint16_t lsi_io_readw(uint16_t addr, void *p)
{
    LSIState *s = (LSIState *)p;
    uint16_t val;
	
    addr &= 0xff;
    val = lsi_reg_readb(s, addr);
    val |= lsi_reg_readb(s, addr + 1) << 8;
    return val;
}

static uint32_t lsi_io_readl(uint16_t addr, void *p)
{
    LSIState *s = (LSIState *)p;
    uint32_t val;
	
    addr &= 0xff;
    val = lsi_reg_readb(s, addr);
    val |= lsi_reg_readb(s, addr + 1) << 8;
    val |= lsi_reg_readb(s, addr + 2) << 16;
    val |= lsi_reg_readb(s, addr + 3) << 24;
    return val;
}

static void lsi_io_writeb(uint16_t addr, uint8_t val, void *p)
{
    LSIState *s = (LSIState *)p;
	lsi_reg_writeb(s, addr & 0xff, val);
}

static void lsi_io_writew(uint16_t addr, uint16_t val, void *p)
{
    LSIState *s = (LSIState *)p;	
	addr &= 0xff;
	lsi_reg_writeb(s, addr, val & 0xff);
	lsi_reg_writeb(s, addr + 1, (val >> 8) & 0xff);
}

static void lsi_io_writel(uint16_t addr, uint32_t val, void *p)
{
    LSIState *s = (LSIState *)p;
    addr &= 0xff;
    lsi_reg_writeb(s, addr, val & 0xff);
    lsi_reg_writeb(s, addr + 1, (val >> 8) & 0xff);
    lsi_reg_writeb(s, addr + 2, (val >> 16) & 0xff);
    lsi_reg_writeb(s, addr + 3, (val >> 24) & 0xff);
}

static void lsi_mmio_writeb(uint32_t addr, uint8_t val, void *p)
{
    LSIState *s = (LSIState *)p;

    lsi_reg_writeb(s, addr & 0xff, val);
}

static void lsi_mmio_writew(uint32_t addr, uint16_t val, void *p)
{
	LSIState *s = (LSIState *)p;
	
	addr &= 0xff;
	lsi_reg_writeb(s, addr, val & 0xff);
	lsi_reg_writeb(s, addr + 1, (val >> 8) & 0xff);
}

static void lsi_mmio_writel(uint32_t addr, uint32_t val, void *p)
{
	LSIState *s = (LSIState *)p;

	addr &= 0xff;
	lsi_reg_writeb(s, addr, val & 0xff);
	lsi_reg_writeb(s, addr + 1, (val >> 8) & 0xff);
	lsi_reg_writeb(s, addr + 2, (val >> 16) & 0xff);
	lsi_reg_writeb(s, addr + 3, (val >> 24) & 0xff);
}

static uint8_t lsi_mmio_readb(uint32_t addr, void *p)
{
	LSIState *s = (LSIState *)p;
	
	return lsi_reg_readb(s, addr & 0xff);
}

static uint16_t lsi_mmio_readw(uint32_t addr, void *p)
{
	LSIState *s = (LSIState *)p;
	uint16_t val;
	
	addr &= 0xff;
	val = lsi_reg_readb(s, addr);
	val |= lsi_reg_readb(s, addr + 1) << 8;
	return val;
}
 
static uint32_t lsi_mmio_readl(uint32_t addr, void *p)
{
	LSIState *s = (LSIState *)p;
	uint32_t val;
	
	addr &= 0xff;
	val = lsi_reg_readb(s, addr);
	val |= lsi_reg_readb(s, addr + 1) << 8;
	val |= lsi_reg_readb(s, addr + 2) << 16;
	val |= lsi_reg_readb(s, addr + 3) << 24;	
	return val;
}

static void
lsi_io_set(LSIState *s, uint32_t base, uint16_t len)
{
	ncr53c810_log("NCR53c810: [PCI] Setting I/O handler at %04X\n", base);
	io_sethandler(base, len,
		      lsi_io_readb, lsi_io_readw, lsi_io_readl,
                      lsi_io_writeb, lsi_io_writew, lsi_io_writel, s);
}


static void
lsi_io_remove(LSIState *s, uint32_t base, uint16_t len)
{
    ncr53c810_log("NCR53c810: Removing I/O handler at %04X\n", base);
	io_removehandler(base, len,
		      lsi_io_readb, lsi_io_readw, lsi_io_readl,
                      lsi_io_writeb, lsi_io_writew, lsi_io_writel, s);
}

static void
lsi_mem_init(LSIState *s, uint32_t addr)
{
	mem_mapping_add(&s->mmio_mapping, addr, 0x100,
		        lsi_mmio_readb, lsi_mmio_readw, lsi_mmio_readl,
			lsi_mmio_writeb, lsi_mmio_writew, lsi_mmio_writel,
			NULL, MEM_MAPPING_EXTERNAL, s);
}

static void
lsi_mem_set_addr(LSIState *s, uint32_t base)
{
    mem_mapping_set_addr(&s->mmio_mapping, base, 0x100);
}

static void
lsi_mem_disable(LSIState *s)
{
    mem_mapping_disable(&s->mmio_mapping);
}

uint8_t	ncr53c810_pci_regs[256];
bar_t	ncr53c810_pci_bar[2];

static uint8_t
LSIPCIRead(int func, int addr, void *p)
{
    LSIState *s = (LSIState *)p;

    ncr53c810_log("NCR53c810: Reading register %02X\n", addr & 0xff);

    if ((addr >= 0x80) && (addr <= 0xDF)) {
	return lsi_reg_readb(s, addr & 0x7F);
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
		return s->irq;
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
    LSIState *s = (LSIState *)p;
    uint8_t valxor;

    ncr53c810_log("NCR53c810: Write value %02X to register %02X\n", val, addr & 0xff);

    if ((addr >= 0x80) && (addr <= 0xDF)) {
	lsi_reg_writeb(s, addr & 0x7F, val);
	return;
    }

    switch (addr) 
	{
	case 0x04:
		valxor = (val & 0x57) ^ ncr53c810_pci_regs[addr];
		if (valxor & PCI_COMMAND_IO) {
			lsi_io_remove(s, s->PCIBase, 0x0100);
			if ((s->PCIBase != 0) && (val & PCI_COMMAND_IO)) {
				lsi_io_set(s, s->PCIBase, 0x0100);
			}
		}
		if (valxor & PCI_COMMAND_MEM) {
			lsi_mem_disable(s);
			if ((s->MMIOBase != 0) && (val & PCI_COMMAND_MEM)) {
				lsi_mem_set_addr(s, s->MMIOBase);
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
		lsi_io_remove(s, s->PCIBase, 0x0100);
		/* Then let's set the PCI regs. */
		ncr53c810_pci_bar[0].addr_regs[addr & 3] = val;
		/* Then let's calculate the new I/O base. */
		ncr53c810_pci_bar[0].addr &= 0xff00;
		s->PCIBase = ncr53c810_pci_bar[0].addr;
		/* Log the new base. */
		ncr53c810_log("NCR53c810: New I/O base is %04X\n" , s->PCIBase);
		/* We're done, so get out of the here. */
		if (ncr53c810_pci_regs[4] & PCI_COMMAND_IO) {
			if (s->PCIBase != 0) {
				lsi_io_set(s, s->PCIBase, 0x0100);
			}
		}
		return;

	case 0x15: case 0x16: case 0x17:
		/* MMIO Base set. */
		/* First, remove the old I/O. */
		lsi_mem_disable(s);
		/* Then let's set the PCI regs. */
		ncr53c810_pci_bar[1].addr_regs[addr & 3] = val;
		/* Then let's calculate the new I/O base. */
		s->MMIOBase = ncr53c810_pci_bar[1].addr & 0xffffff00;
		/* Log the new base. */
		ncr53c810_log("NCR53c810: New MMIO base is %08X\n" , s->MMIOBase);
		/* We're done, so get out of the here. */
		if (ncr53c810_pci_regs[4] & PCI_COMMAND_MEM) {
			if (s->MMIOBase != 0) {
				lsi_mem_set_addr(s, s->MMIOBase);
			}
		}
		return;	

	case 0x3C:
		ncr53c810_pci_regs[addr] = val;
		s->irq = val;
		return;
    }
}



static void *
ncr53c810_init(device_t *info)
{
    LSIState *s;

    s = malloc(sizeof(LSIState));
    memset(s, 0x00, sizeof(LSIState));

    s->chip_rev = 0;
    s->pci_slot = pci_add_card(PCI_ADD_NORMAL, LSIPCIRead, LSIPCIWrite, s);

    ncr53c810_pci_bar[0].addr_regs[0] = 1;
    ncr53c810_pci_bar[1].addr_regs[0] = 0;
    ncr53c810_pci_regs[0x04] = 3;	

    lsi_mem_init(s, 0x0fffff00);
    lsi_mem_disable(s);

    lsi_soft_reset(s);

    return(s);
}


static void 
ncr53c810_close(void *priv)
{
    LSIState *s = (LSIState *)priv;

    if (s) {
	free(s);
	s = NULL;
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
