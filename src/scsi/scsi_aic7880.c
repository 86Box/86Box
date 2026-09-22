/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Adaptec AIC-7880 PCI Ultra SCSI controller, as the chip on a
 *          motherboard and as the AHA-2940 Ultra and Ultra Wide cards.
 *
 *          HOW THIS PART IS MODELLED.
 *
 *          The chip is a small RISC processor, the sequencer, in front of
 *          a SCSI cell and a bus-master DMA engine. It has no firmware of
 *          its own: every driver downloads a program into sequencer RAM,
 *          and that program, not the silicon, decides what an SCB looks
 *          like, how commands are queued and how completions are posted.
 *          The Linux driver of 1996, the Linux driver of today, Adaptec's
 *          own drivers and the card's BIOS each bring a different one.
 *
 *          So nothing here knows what an SCB is. The sequencer is
 *          executed an instruction at a time, and what is modelled is
 *          what that program can see: the register file, scratch and SCB
 *          RAM, the data FIFO with its host and SCSI sides, the selection
 *          and reselection logic, the hardware queue FIFOs, the interrupt
 *          and pause logic, and a SCSI bus with targets on it that walk
 *          through the phases and raise REQ.
 *
 *          The instruction set is the one the aic7xxx assembler emits
 *          (aicasm_insformat.h) in the packed form pre-Ultra2 parts take:
 *          immediate, source and destination a byte each, a return bit,
 *          and a four-bit opcode. Register names and bits follow
 *          aic7xxx.reg, which cites the AIC-7770 and AIC-7870 data books.
 *
 *          Not modelled: target mode (the chip answering selection as a
 *          target), SCAM, parity errors on either bus, external SCB SRAM,
 *          and the pace of the SCSI bus itself. The sequencer runs at the
 *          clock rate the chip would have, and targets give up the bus
 *          while they work and come back by reselection, as real ones do.
 *          Selection is timed only nominally: what the firmware cares
 *          about is that a target that is there is quick and one that is
 *          not is not. How long the device takes over a command is an
 *          option, off unless asked for, because the seek is modelled and
 *          the transfer is not. The data itself crosses the bus at once.
 *
 * Authors: Michael Pratte, <mpratte@makefox.group>
 *
 *          Copyright 2026 Michael Pratte.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/pci.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/nvr.h>
#include <86box/nmc93cxx.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/scsi_aic7880.h>
#include <86box/plat_unused.h>

/* Every command, phase change and interrupt is one line. The sequencer and
   the register file are far louder than that, so they are separate: set
   AIC7880_LOG_SEQ to log every instruction the sequencer executes, and
   AIC7880_LOG_REGS to log every access to the register file. */
#define AIC7880_LOG_SEQ  0
#define AIC7880_LOG_REGS 0

#ifdef ENABLE_AIC7880_LOG
int aic7880_do_log = ENABLE_AIC7880_LOG;

static void
aic_log(const char *fmt, ...)
{
    va_list ap;

    if (aic7880_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define aic_log(fmt, ...)
#endif

/* ---- register file (aic7xxx.reg) ---------------------------------------- */

#define SCSISEQ      0x00
#define TEMODE       0x80
#define ENSELO       0x40
#define ENSELI       0x20
#define ENRSELI      0x10
#define ENAUTOATNO   0x08
#define ENAUTOATNI   0x04
#define ENAUTOATNP   0x02
#define SCSIRSTO     0x01
#define SXFRCTL0     0x01
#define DFON         0x80
#define DFPEXP       0x40
#define FAST20       0x20
#define CLRSTCNT     0x10
#define SPIOEN       0x08
#define SCAMEN       0x04
#define CLRCHN       0x02
#define SXFRCTL1     0x02
#define BITBUCKET    0x80
#define SWRAPEN      0x40
#define ENSPCHK      0x20
#define STIMESEL     0x18
#define ENSTIMER     0x04
#define ACTNEGEN     0x02
#define STPWEN       0x01
#define SCSISIG      0x03
#define CDI          0x80
#define IOI          0x40
#define MSGI         0x20
#define ATNI         0x10
#define SELI         0x08
#define BSYI         0x04
#define REQI         0x02
#define ACKI         0x01
#define PHASE_MASK   (CDI | IOI | MSGI)
#define P_DATAOUT    0x00
#define P_DATAIN     IOI
#define P_COMMAND    CDI
#define P_MESGOUT    (CDI | MSGI)
#define P_STATUS     (CDI | IOI)
#define P_MESGIN     (CDI | IOI | MSGI)
#define SCSIRATE     0x04
#define SCSIID       0x05
#define SCSIDATL     0x06
#define SCSIDATH     0x07
#define STCNT        0x08 /* three bytes */
#define SSTAT0       0x0b /* CLRSINT0 on write */
#define TARGET       0x80
#define SELDO        0x40
#define SELDI        0x20
#define SELINGO      0x10
#define SWRAP        0x08
#define SDONE        0x04
#define SPIORDY      0x02
#define DMADONE      0x01
#define SSTAT1       0x0c /* CLRSINT1 on write */
#define SELTO        0x80
#define ATNTARG      0x40
#define SCSIRSTI     0x20
#define PHASEMIS     0x10
#define BUSFREE      0x08
#define SCSIPERR     0x04
#define PHASECHG     0x02
#define REQINIT      0x01
#define CLRATNO      0x40 /* CLRSINT1 only */
#define SSTAT2       0x0d
#define SSTAT3       0x0e
#define SCSITEST     0x0f
#define SIMODE0      0x10
#define SIMODE1      0x11
#define SCSIBUSL     0x12
#define SCSIBUSH     0x13
#define SHADDR       0x14 /* four bytes, read only */
#define SELTIMER     0x18
#define SELID        0x19
#define ONEBIT       0x08
#define SCAMCTL      0x1a
#define SLEEPCTL     0x1c
#define SLEEPDIS     0x80
#define SLP1         0x02 /* wake on DMADONE or PHASEMIS */
#define SLP0         0x01 /* wake on SELDO or SELDI */
#define SPIOCAP      0x1b
#define BRDCTL       0x1d
#define BRDDAT7      0x80
#define BRDDAT6      0x40
#define BRDDAT5      0x20
#define BRDSTB       0x10
#define BRDCS        0x08
#define BRDRW        0x04
#define SEECTL       0x1e
#define EXTARBACK    0x80
#define EXTARBREQ    0x40
#define SEEMS        0x20
#define SEERDY       0x10
#define SEECS        0x08
#define SEECK        0x04
#define SEEDO        0x02
#define SEEDI        0x01
#define SBLKCTL      0x1f
#define DIAGLEDEN    0x80
#define DIAGLEDON    0x40
#define AUTOFLUSHDIS 0x20
#define SELBUSB      0x08
#define SELWIDE      0x02

#define SRAM_BASE    0x20 /* scratch RAM, to 0x5f */

#define SEQCTL       0x60
#define PERRORDIS    0x80
#define PAUSEDIS     0x40
#define FAILDIS      0x20
#define FASTMODE     0x10
#define BRKADRINTEN  0x08
#define STEP         0x04
#define SEQRESET     0x02
#define LOADRAM      0x01
#define SEQRAM       0x61
#define SEQADDR0     0x62
#define SEQADDR1     0x63
#define ACCUM        0x64
#define SINDEX       0x65
#define DINDEX       0x66
#define BRKADDR0     0x67
#define BRKADDR1     0x68
#define BRKDIS       0x80
#define ALLONES      0x69
#define ALLZEROS     0x6a /* NONE on write */
#define FLAGS        0x6b
#define ZERO         0x02
#define CARRY        0x01
#define SINDIR       0x6c
#define DINDIR       0x6d
#define FUNCTION1    0x6e
#define STACK        0x6f

#define DSVENDID     0x80 /* the PCI IDs again, where the sequencer can see them */
#define DSDEVID      0x82
#define DSCOMMAND0   0x84
#define DSCOMMAND1   0x85
#define DSPCISTATUS  0x86
#define DFTHRSH_100  0xc0
#define HCNTRL       0x87
#define POWRDN       0x40
#define SWINT        0x10
#define IRQMS        0x08
#define PAUSE        0x04
#define INTEN        0x02
#define CHIPRST      0x01
#define HADDR        0x88 /* four bytes */
#define HCNT         0x8c /* three bytes */
#define SCBPTR       0x90
#define INTSTAT      0x91
#define BRKADRINT    0x08
#define SCSIINT      0x04
#define CMDCMPLT     0x02
#define SEQINT       0x01
#define INT_PEND     (BRKADRINT | SEQINT | SCSIINT | CMDCMPLT)
#define ERROR        0x92 /* CLRINT on write */
#define ILLOPCODE    0x04
#define CLRPARERR    0x10
#define CLRBRKADRINT 0x08
#define CLRSCSIINT   0x04
#define CLRCMDINT    0x02
#define CLRSEQINT    0x01
#define DFCNTRL      0x93
#define WIDEODD      0x40
#define SCSIEN       0x20
#define SDMAEN       0x10
#define HDMAEN       0x08
#define DIRECTION    0x04 /* set: host to SCSI */
#define FIFOFLUSH    0x02
#define FIFORESET    0x01
#define DFSTATUS     0x94
#define DFCACHETH    0x40
#define FIFOQWDEMP   0x20
#define MREQPEND     0x10
#define HDONE        0x08
#define DFTHRESH     0x04
#define FIFOFULL     0x02
#define FIFOEMP      0x01
#define DFWADDR      0x95
#define DFRADDR      0x97
#define DFDAT        0x99
#define SCBCNT       0x9a
#define SCBAUTO      0x80
#define QINFIFO      0x9b
#define QINCNT       0x9c
#define QOUTFIFO     0x9d
#define QOUTCNT      0x9e
#define SFUNCT       0x9f
#define SCB_BASE     0xa0 /* the SCB selected by SCBPTR, to 0xbf */

#define SCB_COUNT    16
#define SCB_SIZE     32
#define SEQ_INSNS    512
#define FIFO_SIZE    256
/* Sixteen entries of four bits each: the queues live in what is left of
   the internal SCB RAM, and only a board with external SCB RAM (RAMPSM),
   which none of these has, gets 255 of eight. */
#define QUEUE_SIZE 16

/* PCI configuration, device specific. */
#define DEVCONFIG 0x40

/* ---- the far end of the cable ------------------------------------------- */

/* What the target does once the message it is sending has been taken. */
enum {
    AFTER_NEXT = 0, /* carry on with the command */
    AFTER_FREE,     /* command complete: release the bus */
    AFTER_DISC      /* disconnect: release the bus, come back later */
};

enum {
    BUS_FREE = 0,
    BUS_BUSY /* a target holds BSY; aic_tgt.phase says what it is doing */
};

#define TGT_DATA_MAX (1024 * 1024)

/* One command a target is holding. A target that disconnects can hold
   several, so none of this lives in 86Box's per-device state. */
typedef struct aic_cmd_t {
    uint8_t  used;
    uint8_t  id;
    uint8_t  lun;
    uint8_t  identified;
    uint8_t  disc_ok;
    uint8_t  tagged;
    uint8_t  tag_type;
    uint8_t  tag;
    uint8_t  cdb[16];
    uint8_t  cdb_len;
    uint8_t  cdb_pos;
    uint8_t  executed;
    uint8_t  data_in;
    uint8_t  status;
    uint8_t  status_sent;
    uint8_t  waited;
    uint8_t *data;
    uint32_t data_len;
    uint32_t data_pos;
    double   delay;    /* how long the device takes over this command, in microseconds */
    double   ready_at; /* when a disconnected target has its answer and may come back */
    uint8_t  delayed;  /* the wait for the medium has been served */
} aic_cmd_t;

#define AIC_CMDS 64

typedef struct aic7880_t {
    /* PCI side */
    uint8_t       pci_slot;
    uint8_t       irq_state; /* belongs to the PCI layer */
    uint8_t       irq_level; /* what we last drove */
    uint8_t       pci_regs[256];
    uint16_t      io_base;
    uint8_t       io_live;
    uint32_t      mem_base;
    mem_mapping_t mmio;
    rom_t         bios;
    uint8_t       has_bios;
    uint32_t      rom_reads;
    uint32_t      rom_size;
    uint8_t       bus;
    uint8_t       wide;
    uint8_t       board; /* info->local */

    nmc93cxx_eeprom_t *eeprom;

    /* SCSI cell */
    uint8_t  scsiseq;
    uint8_t  sxfrctl0;
    uint8_t  sxfrctl1;
    uint8_t  scsisigo;
    uint8_t  scsirate;
    uint8_t  scsiid;
    uint8_t  scsidatl;
    uint8_t  scsidath;
    uint8_t  datl_full; /* a byte written to SCSIDATL is waiting for REQ */
    uint8_t  req_seen;  /* REQ as the SCSI cell last saw it, to find its leading edge */
    uint8_t  req_wait;  /* sequencer instructions until the target raises REQ again */
    uint8_t  in_dma;    /* the DMA engine, not programmed I/O, is doing the handshake */
    uint32_t stcnt;
    uint8_t  sstat0; /* latched: SELDO SELDI SELINGO */
    uint8_t  sstat1; /* latched: SELTO SCSIRSTI BUSFREE PHASECHG */
    uint8_t  simode0;
    uint8_t  simode1;
    uint32_t shaddr;
    uint8_t  selid;
    uint8_t  scamctl;
    uint8_t  brdctl;
    uint8_t  seectl;
    uint8_t  sblkctl;
    uint8_t  scsitest;
    uint8_t  sleepctl;
    uint8_t  must_step;  /* PAUSE was just released: one instruction runs whatever else is pending */
    uint8_t  timed_dev;  /* a device takes as long over a command as 86Box says it does */
    uint8_t  seq_idle;   /* the sequencer was stopped or parked when last looked at */
    double   seq_last;   /* when its clock was last advanced, in microseconds */
    double   seq_credit; /* instructions it is owed */

    /* sequencer */
    uint8_t  seqctl;
    uint16_t pc;
    uint8_t  ram_byte; /* which byte of an instruction LOADRAM is at */
    uint8_t  accum;
    uint8_t  sindex;
    uint8_t  dindex;
    uint8_t  flags;
    uint8_t  function1;
    uint16_t brkaddr;
    uint16_t stack[4];
    uint8_t  sp;
    uint8_t  stack_rd;
    uint8_t  seqram[SEQ_INSNS * 4];
#ifdef ENABLE_AIC7880_LOG
    /* The last instructions executed, for saying how the sequencer came to
       raise an interrupt without logging every instruction it ever runs. */
    struct {
        uint16_t pc;
        uint32_t insn;
        uint32_t stcnt;
        uint32_t hcnt;
        uint16_t fifo;
        uint8_t  sstat1;
        uint8_t  dfcntrl;
    } trail[256];
    uint8_t trail_at;
#endif
    uint8_t  in_seq;    /* guards against re-entering the interpreter */
    uint8_t  progress;  /* the sequencer did something since last asked */
    uint8_t  asleep;    /* it is spinning on something only an event changes */
    uint16_t last_park; /* where it was last reported spinning */

    /* host side */
    uint8_t  dscommand0;
    uint8_t  dscommand1;
    uint8_t  dspcistatus;
    uint8_t  hcntrl;
    uint32_t haddr;
    uint32_t hcnt;
    uint8_t  scbptr;
    uint8_t  intstat;
    uint8_t  error;
    uint8_t  dfcntrl;
    uint8_t  dfwaddr[2];
    uint8_t  dfraddr[2];
    uint8_t  scbcnt;
    uint8_t  sfunct;

    uint8_t  fifo[FIFO_SIZE];
    uint16_t fifo_rd;
    uint16_t fifo_cnt;
    uint8_t  fifo_flush; /* a flush, asked for or automatic, has not finished */
    uint8_t  host_wait;  /* sequencer instructions until the host side may move */

    uint8_t  qin[QUEUE_SIZE];
    uint8_t  qin_rd;
    uint16_t qin_cnt;
    uint8_t  qout[QUEUE_SIZE];
    uint8_t  qout_rd;
    uint16_t qout_cnt;

    uint8_t sram[0x40];
    uint8_t scb[SCB_COUNT][SCB_SIZE];
    uint8_t misc[0x40]; /* 0xc0 to 0xff: nothing on this part, but it holds what it is told */

    /* the bus and what is on it */
    uint8_t    bus_state;
    uint8_t    atn;
    uint8_t    selecting; /* we are driving SEL at a target */
    uint8_t    tgt_phase; /* PHASE_MASK bits while a target holds the bus */
    uint8_t    tgt_req;
    uint8_t    tgt_after;
    aic_cmd_t *cur;
    uint8_t    msgout[16];
    uint8_t    msgout_len;
    uint8_t    msgin[16];
    uint8_t    msgin_len;
    uint8_t    msgin_pos;
    aic_cmd_t  cmds[AIC_CMDS];

    pc_timer_t seq_timer;
    pc_timer_t sel_timer;
    pc_timer_t tgt_timer;
} aic7880_t;

static void aic_update_irq(aic7880_t *dev);
static void aic_seq_kick(aic7880_t *dev);
static void aic_seq_run(aic7880_t *dev);
static void aic_pump(aic7880_t *dev);
static void aic_bus_free(aic7880_t *dev);
static void aic_tgt_next(aic7880_t *dev);
static void aic_tgt_schedule(aic7880_t *dev);
static void aic_chip_reset(aic7880_t *dev);
static void aic_pio_out(aic7880_t *dev);
#ifdef ENABLE_AIC7880_LOG
static const char *aic_phase_name(uint8_t phase);
#endif
static uint8_t aic_read(aic7880_t *dev, uint8_t addr, int seq);
static void    aic_write(aic7880_t *dev, uint8_t addr, uint8_t val, int seq);

/* Emulated time, in microseconds. It moves a translated block at a time
   and not an instruction at a time, which is as fine as anything here
   needs. */
static double
aic_now_us(void)
{
    return (double) tsc * 4294967296.0 / (double) TIMER_USEC;
}

/* ---- interrupts and pausing --------------------------------------------- */

/* The chip is paused when the driver asks for it and the sequencer has
   reached a point where it may stop, or whenever an interrupt is pending:
   an unserviced interrupt always stops the sequencer. */
static int
aic_paused(const aic7880_t *dev)
{
    /* A command-complete interrupt does not stop the sequencer; the other
       three do, and stay stopped until the host clears them. */
    if (dev->intstat & (BRKADRINT | SCSIINT | SEQINT))
        return 1;
    /* The sequencer can refuse a requested pause while it is inside a
       critical section, which is what PAUSEDIS is for. */
    if ((dev->hcntrl & PAUSE) && !(dev->seqctl & PAUSEDIS))
        return 1;
    return 0;
}

static void
aic_update_irq(aic7880_t *dev)
{
    uint8_t pend = dev->intstat & (SEQINT | SCSIINT | CMDCMPLT);
    uint8_t fire;

    /* A breakpoint always stops the sequencer and shows in INTSTAT, but
       it reaches the pin only with BRKADRINTEN. The error sources that
       share the bit always do. */
    if ((dev->intstat & BRKADRINT) && ((dev->seqctl & BRKADRINTEN) || dev->error))
        pend |= BRKADRINT;

    /* INTEN gates everything, SWINT included, and so do bus mastering in
       the PCI command register and POWRDN. */
    fire = (dev->hcntrl & INTEN) && !(dev->hcntrl & POWRDN) && (dev->pci_regs[0x04] & 0x04) && (pend || (dev->hcntrl & SWINT));

    if (fire == dev->irq_level)
        return;
    dev->irq_level = fire;
    aic_log("irq %s intstat %02x\n", fire ? "high" : "low", dev->intstat);
    if (fire)
        pci_set_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
    else
        pci_clear_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
}

static void
aic_raise(aic7880_t *dev, uint8_t bits)
{
    dev->intstat |= bits;
    aic_update_irq(dev);
}

/* SCSIINT is the one interrupt the sequencer does not raise itself: the
   SCSI cell raises it, and only for the conditions SIMODE lets through. */
static void
aic_scsi_int(aic7880_t *dev)
{
    if ((dev->sstat0 & dev->simode0) || (dev->sstat1 & dev->simode1)) {
        if (!(dev->intstat & SCSIINT)) {
            aic_log("scsiint: sstat0 %02x&%02x sstat1 %02x&%02x at pc %03x\n",
                    dev->sstat0, dev->simode0, dev->sstat1, dev->simode1, dev->pc);
        }
        /* A SCSI interrupt also takes PAUSEDIS away, so that the pause it
           asks for cannot be refused. */
        dev->seqctl &= ~PAUSEDIS;
        aic_raise(dev, SCSIINT);
    }
}

static void
aic_set_sstat1(aic7880_t *dev, uint8_t bits)
{
    dev->sstat1 |= bits;
    aic_scsi_int(dev);
}

static void
aic_set_sstat0(aic7880_t *dev, uint8_t bits)
{
    dev->sstat0 |= bits;
    aic_scsi_int(dev);
}

/* ---- the SCSI bus ------------------------------------------------------- */

/* REQINIT follows REQ, and PHASECHG latches a phase that is not the one
   the sequencer last acknowledged by writing SCSISIGO. Both are edges the
   firmware waits on, so they are recomputed whenever the bus moves. */
static void
aic_bus_changed(aic7880_t *dev)
{
    uint8_t phase;
    uint8_t req = (dev->bus_state == BUS_BUSY) && dev->tgt_req;

    /* REQINIT goes up on the leading edge of REQ and comes down with the
       ACK that answers it, or when CLRREQINIT says so: cleared by hand it
       stays clear until the target asks again. PHASEMIS is the phase
       comparison qualified by REQINIT, so it goes with it. */
    if (req && !dev->req_seen)
        dev->sstat1 |= REQINIT;
    else if (!req)
        dev->sstat1 &= ~REQINIT;

    if (dev->sstat1 & REQINIT) {
        phase = dev->tgt_phase;
        if ((dev->scsisigo & PHASE_MASK) != phase)
            aic_set_sstat1(dev, PHASEMIS | PHASECHG);
        else {
            dev->sstat1 &= ~PHASEMIS;
            aic_scsi_int(dev);
        }
    } else
        dev->sstat1 &= ~PHASEMIS;

    /* SPIORDY is a LATCH, set on the leading edge of REQ while automatic
       PIO is on, and it is not REQ. The data book has it fall again when
       SCSIDATL is read or written; the silicon was not seen to do that, and
       the 1996 firmware, which polls it, writes CLRSPIORDY before every
       byte rather than trust it to. So it stays up until it is cleared, and
       a program that takes it for "the target is asking" sends its second
       byte before the target wanted one. REQINIT is what says that. */
    if (req && !dev->req_seen && (dev->sxfrctl0 & SPIOEN))
        aic_set_sstat0(dev, SPIORDY);
    dev->req_seen = req;

    /* A byte already waiting in the PIO latch goes out on this REQ. */
    aic_pio_out(dev);

    dev->asleep = 0;
    aic_seq_kick(dev);
}

static void
aic_bus_free(aic7880_t *dev)
{
    aic_log("bus free (was %s%s)\n",
            (dev->bus_state == BUS_BUSY) ? "busy " : "free ",
            aic_phase_name(dev->tgt_phase));
    dev->bus_state = BUS_FREE;
    dev->tgt_req   = 0;
    dev->tgt_phase = 0;
    dev->cur       = NULL;
    dev->atn       = 0;
    dev->datl_full = 0;
    dev->req_wait  = 0;
    /* Bus free takes SCSISIGO, SELDO and SELDI with it. */
    dev->scsisigo = 0;
    dev->sstat0 &= ~(SELDO | SELDI);
    dev->msgin_len = dev->msgin_pos = 0;
    dev->msgout_len                 = 0;
    timer_stop(&dev->tgt_timer);
    aic_set_sstat1(dev, BUSFREE);
    aic_bus_changed(dev);
}

static aic_cmd_t *
aic_cmd_alloc(aic7880_t *dev)
{
    for (uint8_t i = 0; i < AIC_CMDS; i++) {
        if (!dev->cmds[i].used) {
            aic_cmd_t *c = &dev->cmds[i];
            memset(c, 0, sizeof(*c));
            c->used = 1;
            return c;
        }
    }
    return NULL;
}

static void
aic_cmd_free(aic7880_t *dev, aic_cmd_t *c)
{
    if (c == NULL)
        return;
    if (c->data != NULL)
        free(c->data);
    if (dev->cur == c)
        dev->cur = NULL;
    memset(c, 0, sizeof(*c));
}

/* A target that wants to reconnect. Chosen round-robin-ish: the first
   disconnected command that is ready to say something. */
static aic_cmd_t *
aic_find_reselect(aic7880_t *dev)
{
    double now = aic_now_us();

    for (uint8_t i = 0; i < AIC_CMDS; i++) {
        if (dev->cmds[i].used && dev->cmds[i].waited && (now >= dev->cmds[i].ready_at))
            return &dev->cmds[i];
    }
    return NULL;
}

static int
aic_any_disconnected(const aic7880_t *dev)
{
    for (uint8_t i = 0; i < AIC_CMDS; i++) {
        if (dev->cmds[i].used && dev->cmds[i].waited)
            return 1;
    }
    return 0;
}

static void
aic_msgin(aic7880_t *dev, const uint8_t *msg, int len, uint8_t after)
{
    memcpy(dev->msgin, msg, len);
    dev->msgin_len = len;
    dev->msgin_pos = 0;
    dev->tgt_after = after;
    dev->tgt_phase = P_MESGIN;
    dev->tgt_req   = 1;
}

/* Run the target's command through 86Box's SCSI layer. */
static void
aic_cmd_execute(aic7880_t *dev, aic_cmd_t *c)
{
    scsi_device_t *sd = &scsi_devices[dev->bus][c->id];
    double         p;

    c->executed = 1;

    scsi_device_identify(sd, c->identified ? c->lun : SCSI_LUN_USE_CDB);
    sd->buffer_length = -1;
    scsi_device_command_phase0(sd, c->cdb);

    c->status = sd->status;
    p         = scsi_device_get_callback(sd);
    c->delay  = (p > 0.0) ? p : 1.0;

    if ((sd->phase == SCSI_PHASE_DATA_IN) && (sd->buffer_length > 0)) {
        c->data_in  = 1;
        c->data_len = sd->buffer_length;
        c->data     = (uint8_t *) malloc(c->data_len);
        memcpy(c->data, sd->sc->temp_buffer, c->data_len);
        scsi_device_command_phase1(sd);
        c->status = sd->status;
    } else if ((sd->phase == SCSI_PHASE_DATA_OUT) && (sd->buffer_length > 0)) {
        c->data_in  = 0;
        c->data_len = sd->buffer_length;
        c->data     = (uint8_t *) calloc(1, c->data_len);
    } else
        c->data_len = 0;

    scsi_device_identify(sd, SCSI_LUN_USE_CDB);
    aic_log("[%.3f ms] cmd %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x (len %u) id %i lun %i tag %02x -> data %u %s "
            "status %02x\n",
            aic_now_us() / 1000.0, c->cdb[0], c->cdb[1], c->cdb[2], c->cdb[3], c->cdb[4],
            c->cdb[5], c->cdb[6], c->cdb[7], c->cdb[8], c->cdb[9], c->cdb_len, c->id, c->lun, c->tagged ? c->tag : 0xff, c->data_len,
            c->data_in ? "in" : "out", c->status);
}

/* The data the target collected on a write is handed to the device. */
static void
aic_cmd_finish_out(aic7880_t *dev, aic_cmd_t *c)
{
    scsi_device_t *sd = &scsi_devices[dev->bus][c->id];

    if (c->data_in || (c->data_len == 0) || (c->data == NULL))
        return;
    scsi_device_identify(sd, c->identified ? c->lun : SCSI_LUN_USE_CDB);
    if (sd->sc != NULL && sd->sc->temp_buffer != NULL)
        memcpy(sd->sc->temp_buffer, c->data, c->data_len);
    scsi_device_command_phase1(sd);
    c->status = sd->status;
    scsi_device_identify(sd, SCSI_LUN_USE_CDB);
}

static void
aic_tgt_schedule(aic7880_t *dev)
{
    timer_on_auto(&dev->tgt_timer, 1.0);
}

/* How many sequencer instructions pass between ACK for one PIO byte and
   the target's REQ for the next. A real target drops REQ when it sees ACK
   and raises it again when ACK has gone, and the sequencer gets a few
   instructions in meanwhile; a program that does not wait for that REQ
   works on a bus that answers instantly and nowhere else. */
#define AIC_REQ_INSNS 2

/* And how many pass between HDMAEN going up and the host side's first
   bytes. A program that reads DFDAT without waiting for HDONE reads what
   has not arrived. */
#define AIC_HOST_INSNS 4

/* How long a disconnected target is off the bus at the very least: the
   bus free delay and an arbitration it has to win. The real numbers are
   400ns and 2.4us; what matters here is only that the sequencer gets to
   finish with the connection it has just lost before the next one
   arrives. */
#define AIC_RESELECT_US 4.0

/* The target asks for the next byte of the phase it is already in. Under
   the DMA engine the handshake is the hardware's own and costs nothing
   here; under programmed I/O REQ is seen to fall first. */
static void
aic_tgt_req_again(aic7880_t *dev)
{
    if (dev->in_dma) {
        dev->tgt_req = 1;
        aic_bus_changed(dev);
        return;
    }
    dev->tgt_req  = 0;
    dev->req_wait = AIC_REQ_INSNS;
    aic_bus_changed(dev);
}

/* That REQ arrives. The host is far too slow to get in ahead of it, so
   any access from the host brings it forward. */
static void
aic_tgt_req_due(aic7880_t *dev)
{
    if (!dev->req_wait)
        return;
    dev->req_wait = 0;
    if (dev->bus_state != BUS_BUSY)
        return;
    dev->tgt_req = 1;
    aic_bus_changed(dev);
}

#ifdef ENABLE_AIC7880_LOG
static const char *
aic_phase_name(uint8_t phase)
{
    switch (phase) {
        case P_DATAOUT:
            return "data-out";
        case P_DATAIN:
            return "data-in";
        case P_COMMAND:
            return "command";
        case P_STATUS:
            return "status";
        case P_MESGOUT:
            return "msg-out";
        case P_MESGIN:
            return "msg-in";
        default:
            break;
    }
    return "?";
}
#endif

/* Drive the target forward to whatever it does next. Called when a phase
   completes; sets up the next phase and REQ. */
static void
aic_tgt_next(aic7880_t *dev)
{
    aic_cmd_t *c = dev->cur;
    uint8_t    msg;

    if (c == NULL) {
        aic_bus_free(dev);
        return;
    }

    /* Before anything else, the target wants any message we have. */
    if (dev->atn) {
        dev->tgt_phase = P_MESGOUT;
        dev->tgt_req   = 1;
        aic_log("tgt: %s\n", aic_phase_name(dev->tgt_phase));
        aic_bus_changed(dev);
        return;
    }

    /* The length of a CDB is known only from its first byte. */
    if ((c->cdb_pos == 0) || (c->cdb_pos < c->cdb_len)) {
        dev->tgt_phase = P_COMMAND;
        dev->tgt_req   = 1;
        aic_log("tgt: %s\n", aic_phase_name(dev->tgt_phase));
        aic_bus_changed(dev);
        return;
    }

    if (!c->executed) {
        aic_cmd_execute(dev, c);
        /* A target with work to do and permission to go away takes it,
           once, so that reselection gets exercised. */
        if (c->disc_ok && !c->waited && (c->data_len > 0)) {
            /* SAVE DATA POINTERS, then DISCONNECT. A target sends both,
               in that order, and the sequencer needs the first: it is
               what tells the program to write the transfer's address and
               count into the block it is about to park. Sending only the
               disconnect leaves those fields holding whatever the last
               command left there, and the transfer resumes into it. */
            static const uint8_t disc[2] = { 0x02, 0x04 };
            c->waited                    = 1;
            c->delayed                   = 1;
            /* A target cannot come straight back. The bus has to be seen
               free -- four hundred nanoseconds of BSY and SEL both
               negated -- and then arbitrated for, and only then may it
               reselect. Coming back inside that window puts SELDI on top
               of BUSFREE before the sequencer has run its bus free
               handler, and the program loses the command it had just
               parked. Waiting is not a nicety: it is what the bus does. */
            c->ready_at = aic_now_us() + AIC_RESELECT_US + (dev->timed_dev ? c->delay : 0.0);
            aic_msgin(dev, disc, 2, AFTER_DISC);
            aic_bus_changed(dev);
            return;
        }
    }

    /* The device takes as long over the command as 86Box's own model of
       it says: the seek, the rotation, the transfer off the medium. A
       target that stays connected simply holds the bus with REQ down
       until it has something to say. */
    if (dev->timed_dev && !c->delayed) {
        c->delayed = 1;
        if (c->delay > 1.0) {
            dev->tgt_req = 0;
            aic_bus_changed(dev);
            timer_on_auto(&dev->tgt_timer, c->delay);
            return;
        }
    }

    if ((c->data_len > 0) && (c->data_pos < c->data_len)) {
        dev->tgt_phase = c->data_in ? P_DATAIN : P_DATAOUT;
        dev->tgt_req   = 1;
        aic_log("tgt: %s %u/%u\n", aic_phase_name(dev->tgt_phase), c->data_pos,
                c->data_len);
        aic_bus_changed(dev);
        return;
    }

    if (!c->data_in && (c->data_len > 0) && !c->status_sent)
        aic_cmd_finish_out(dev, c);

    if (!c->status_sent) {
        c->status_sent = 1;
        dev->tgt_phase = P_STATUS;
        dev->tgt_req   = 1;
        aic_log("tgt: status %02x\n", c->status);
        aic_bus_changed(dev);
        return;
    }

    msg = 0x00; /* COMMAND COMPLETE */
    aic_log("tgt: msg-in complete\n");
    aic_msgin(dev, &msg, 1, AFTER_FREE);
    aic_bus_changed(dev);
}

static void
aic_tgt_timer(void *priv)
{
    aic7880_t *dev = (aic7880_t *) priv;

    aic_tgt_next(dev);
}

/* A byte the initiator drives at the target in the current phase. */
static void
aic_tgt_take(aic7880_t *dev, uint8_t val)
{
    aic_cmd_t *c = dev->cur;

    switch (dev->tgt_phase) {
        case P_MESGOUT:
            aic_log("tgt: msg-out %02x\n", val);
            if (dev->msgout_len < sizeof(dev->msgout))
                dev->msgout[dev->msgout_len++] = val;
            val = dev->msgout[0]; /* a message is classified by its first byte */
            if (val & 0x80) {     /* IDENTIFY */
                if (c != NULL) {
                    c->identified = 1;
                    c->lun        = val & 7;
                    c->disc_ok    = !!(val & 0x40);
                }
                dev->msgout_len = 0;
            } else if ((val >= 0x20) && (val <= 0x23)) {
                /* queue tag: two bytes */
                if (dev->msgout_len >= 2) {
                    if (c != NULL) {
                        c->tagged   = 1;
                        c->tag_type = val;
                        c->tag      = dev->msgout[1];
                    }
                    dev->msgout_len = 0;
                }
                break;
            } else if (val == 0x01) {
                /* extended: wait for the length byte then that many more */
                if ((dev->msgout_len >= 2) && (dev->msgout_len >= (2 + dev->msgout[1]))) {
                    uint8_t reply[8];
                    uint8_t n = dev->msgout[1];
                    /* Answer a negotiation the way a drive would. Nothing
                       here transfers on a wire, so what is agreed changes
                       no behaviour; the exchange only has to complete in
                       a way the driver accepts. */
                    if ((n >= 3) && (dev->msgout[2] == 0x01)) { /* SDTR */
                        reply[0] = 0x01;
                        reply[1] = 0x03;
                        reply[2] = 0x01;
                        /* Agree to the period offered and to as much
                           offset as a drive of the day would have. */
                        reply[3]        = dev->msgout[3];
                        reply[4]        = (dev->msgout[4] > 15) ? 15 : dev->msgout[4];
                        dev->msgout_len = 0;
                        aic_msgin(dev, reply, 5, AFTER_NEXT);
                        aic_bus_changed(dev);
                        return;
                    }
                    if ((n >= 2) && (dev->msgout[2] == 0x03)) { /* WDTR */
                        reply[0]        = 0x01;
                        reply[1]        = 0x02;
                        reply[2]        = 0x03;
                        reply[3]        = (dev->wide && dev->msgout[3]) ? 0x01 : 0x00;
                        dev->msgout_len = 0;
                        aic_msgin(dev, reply, 4, AFTER_NEXT);
                        aic_bus_changed(dev);
                        return;
                    }
                    dev->msgout_len = 0;
                }
                break;
            } else if (val == 0x08) { /* NOP */
                dev->msgout_len = 0;
            } else if ((val == 0x06) || (val == 0x0c) || (val == 0x0d)) {
                /* ABORT, BUS DEVICE RESET, ABORT TAG */
                aic_log("msgout %02x: dropping command\n", val);
                aic_cmd_free(dev, c);
                aic_bus_free(dev);
                return;
            } else {
                /* Anything else is taken and ignored; a real target would
                   reject what it does not know, and the firmware handles
                   a reject, but nothing here sends one it would not. */
                dev->msgout_len = 0;
            }
            break;

        case P_COMMAND:
            if (c != NULL) {
                if (c->cdb_pos < sizeof(c->cdb))
                    c->cdb[c->cdb_pos] = val;
                c->cdb_pos++;
                if (c->cdb_pos == 1) {
                    static const uint8_t group_len[8] = { 6, 10, 10, 6, 16, 12, 10, 6 };
                    c->cdb_len                        = group_len[(val >> 5) & 7];
                }
            }
            break;

        case P_DATAOUT:
            if ((c != NULL) && (c->data != NULL) && (c->data_pos < c->data_len))
                c->data[c->data_pos++] = val;
            break;

        default:
            break;
    }

    /* The target considers what to do next. It asks for the rest of a
       command or of its data straight away; a finished phase takes a
       moment. */
    if (((dev->tgt_phase == P_COMMAND) && (c != NULL) && (c->cdb_pos < c->cdb_len)) || ((dev->tgt_phase == P_DATAOUT) && (c != NULL) && (c->data_pos < c->data_len)) || ((dev->tgt_phase == P_MESGOUT) && dev->atn)) {
        aic_tgt_req_again(dev);
        return;
    }
    dev->tgt_req = 0;
    aic_bus_changed(dev);
    aic_tgt_schedule(dev);
}

/* The byte the target is offering in the current phase. */
static uint8_t
aic_tgt_byte(const aic7880_t *dev)
{
    const aic_cmd_t *c = dev->cur;

    switch (dev->tgt_phase) {
        case P_MESGIN:
            return dev->msgin[dev->msgin_pos];
        case P_STATUS:
            return (c != NULL) ? c->status : 0x00;
        case P_DATAIN:
            if ((c != NULL) && (c->data != NULL) && (c->data_pos < c->data_len))
                return c->data[c->data_pos];
            return 0x00;
        default:
            break;
    }
    return 0x00;
}

/* ACK for the byte the target was offering. */
static void
aic_tgt_acked(aic7880_t *dev)
{
    aic_cmd_t *c = dev->cur;

    switch (dev->tgt_phase) {
        case P_MESGIN:
            dev->msgin_pos++;
            if (dev->msgin_pos < dev->msgin_len) {
                aic_tgt_req_again(dev);
                return;
            }
            dev->msgin_len = dev->msgin_pos = 0;
            dev->tgt_req                    = 0;
            aic_bus_changed(dev);
            if (dev->tgt_after == AFTER_FREE) {
                aic_cmd_free(dev, c);
                aic_bus_free(dev);
                return;
            }
            if (dev->tgt_after == AFTER_DISC) {
                if (c != NULL)
                    c->waited = 1;
                dev->cur = NULL;
                aic_bus_free(dev);
                return;
            }
            aic_tgt_schedule(dev);
            return;

        case P_DATAIN:
            if ((c != NULL) && (c->data_pos < c->data_len))
                c->data_pos++;
            if ((c != NULL) && (c->data_pos < c->data_len)) {
                aic_tgt_req_again(dev);
                return;
            }
            break;

        case P_STATUS:
        default:
            break;
    }

    dev->tgt_req = 0;
    aic_bus_changed(dev);
    aic_tgt_schedule(dev);
}

/* ---- selection ---------------------------------------------------------- */

/* Selection: SCSIID holds our ID in the low nibble and the target's in the
   high one; ENSELO starts it. A target that is there answers; one that is
   not lets the timer expire into SELTO. */
static void
aic_select_start(aic7880_t *dev)
{
    if (dev->selecting || (dev->bus_state != BUS_FREE))
        return;
    dev->selecting = 1;
    dev->sstat0 |= SELINGO;
    /* The timer is nominal: the firmware only cares that a present
       target is quick and an absent one is not. */
    timer_on_auto(&dev->sel_timer, 100.0);
}

static void
aic_select_done(void *priv)
{
    aic7880_t     *dev = (aic7880_t *) priv;
    uint8_t        id;
    scsi_device_t *sd;
    aic_cmd_t     *c;

    if (!dev->selecting) {
        dev->sstat0 &= ~SELINGO;
        return;
    }

    id = (dev->scsiid >> 4) & (dev->wide ? 0x0f : 0x07);
    sd = &scsi_devices[dev->bus][id];

    /* Nobody there, and the selection timer not running: the chip goes on
       selecting for ever. CHIPRST leaves ENSTIMER clear, so a driver that
       resets the part and does not put SXFRCTL1 back never sees SELTO. */
    if ((dev->scsiseq & ENSELO) && !scsi_device_present(sd) && !(dev->sxfrctl1 & ENSTIMER)) {
        aic_log("select %i: nobody, and no selection timer\n", id);
        return;
    }

    dev->sstat0 &= ~SELINGO;
    dev->selecting = 0;

    if (!(dev->scsiseq & ENSELO)) {
        aic_bus_changed(dev);
        return;
    }

    if (!scsi_device_present(sd)) {
        aic_log("select %i: timeout\n", id);
        aic_set_sstat1(dev, SELTO);
        aic_bus_changed(dev);
        return;
    }

    c = aic_cmd_alloc(dev);
    if (c == NULL) {
        dev->scsiseq &= ~ENSELO;
        aic_set_sstat1(dev, SELTO);
        aic_bus_changed(dev);
        return;
    }
    c->id = id;

    dev->cur       = c;
    dev->bus_state = BUS_BUSY;
    dev->selid     = (uint8_t) (id << 4);
    /* ATN is not driven through SCSISIGO for this: the chip raises it
       itself on a successful select-out when told to, which is how the
       firmware gets a message out phase for its identify message. */
    if (dev->scsiseq & ENAUTOATNO)
        dev->atn = 1;
    aic_log("select %i: ok (atn %i) scb%u ctl %02x tcl %02x cmdlen %02x\n", id,
            dev->atn, dev->scbptr & (SCB_COUNT - 1),
            dev->scb[dev->scbptr & (SCB_COUNT - 1)][0x00],
            dev->scb[dev->scbptr & (SCB_COUNT - 1)][0x01],
            dev->scb[dev->scbptr & (SCB_COUNT - 1)][0x18]);
    aic_set_sstat0(dev, SELDO);
    /* The target asks for the identify message, or for the command. */
    aic_tgt_next(dev);
}

/* A disconnected target coming back. Allowed when ENRSELI is set and the
   bus is free; SELID then names it with ONEBIT clear. */
static void
aic_reselect_try(aic7880_t *dev)
{
    aic_cmd_t *c;
    uint8_t    msg;

    if ((dev->bus_state != BUS_FREE) || dev->selecting)
        return;
    if (!(dev->scsiseq & ENRSELI))
        return;
    c = aic_find_reselect(dev);
    if (c == NULL)
        return;

    c->waited      = 0;
    dev->cur       = c;
    dev->bus_state = BUS_BUSY;
    dev->selid     = (uint8_t) (c->id << 4);
    if (dev->scsiseq & ENAUTOATNI)
        dev->atn = 1;
    aic_log("[%.3f ms] reselect %i lun %i tag %02x\n", aic_now_us() / 1000.0, c->id, c->lun, c->tagged ? c->tag : 0xff);
    aic_set_sstat0(dev, SELDI);

    /* A reconnecting target identifies itself, and if the command was
       tagged, says which one it is. */
    msg = (uint8_t) (0x80 | c->lun);
    if (c->tagged) {
        uint8_t m[3] = { msg, c->tag_type, c->tag };
        aic_msgin(dev, m, 3, AFTER_NEXT);
    } else
        aic_msgin(dev, &msg, 1, AFTER_NEXT);
    aic_bus_changed(dev);
}

static void
aic_scsi_reset_bus(aic7880_t *dev)
{
    aic_log("[%.3f ms] scsi bus reset\n", aic_now_us() / 1000.0);
    for (uint8_t i = 0; i < AIC_CMDS; i++) {
        if (dev->cmds[i].used)
            aic_cmd_free(dev, &dev->cmds[i]);
    }
    dev->cur       = NULL;
    dev->bus_state = BUS_FREE;
    dev->tgt_req   = 0;
    dev->selecting = 0;
    dev->req_wait  = 0;
    dev->atn       = 0;
    dev->datl_full = 0;
    /* A reset clears SCSISIGO and everything in SCSISEQ but the bit that
       is causing it. */
    dev->scsisigo = 0;
    dev->scsiseq &= SCSIRSTO;
    dev->sstat0 &= ~(SELDO | SELDI | SELINGO);
    timer_stop(&dev->sel_timer);
    timer_stop(&dev->tgt_timer);

    for (uint8_t i = 0; i < (dev->wide ? 16 : 8); i++)
        scsi_device_reset(&scsi_devices[dev->bus][i]);

    aic_set_sstat1(dev, SCSIRSTI);
    aic_bus_changed(dev);
}

/* ---- the data FIFO ------------------------------------------------------ */

static void
aic_fifo_reset(aic7880_t *dev)
{
    dev->fifo_rd = dev->fifo_cnt = 0;
    dev->fifo_flush              = 0;
}

static void
aic_fifo_push(aic7880_t *dev, uint8_t val)
{
    if (dev->fifo_cnt >= FIFO_SIZE)
        return;
    dev->fifo[(dev->fifo_rd + dev->fifo_cnt) % FIFO_SIZE] = val;
    dev->fifo_cnt++;
}

static uint8_t
aic_fifo_pop(aic7880_t *dev)
{
    uint8_t val;

    if (dev->fifo_cnt == 0)
        return 0;
    val          = dev->fifo[dev->fifo_rd];
    dev->fifo_rd = (dev->fifo_rd + 1) % FIFO_SIZE;
    dev->fifo_cnt--;
    return val;
}

/* How many stored bytes start a burst from the FIFO to memory: DFTHRSH
   selects three quadwords, half, three quarters or all of it. */
static uint32_t
aic_fifo_threshold(const aic7880_t *dev)
{
    static const uint16_t level[4] = { 24, FIFO_SIZE / 2, (FIFO_SIZE * 3) / 4, FIFO_SIZE };

    return level[dev->dspcistatus >> 6];
}

/* The hardware flushes by itself when the SCSI side of a read is over:
   the count ran out, or the target left the phase. */
static void
aic_fifo_autoflush(aic7880_t *dev)
{
    if (!(dev->dfcntrl & SCSIEN) || (dev->dfcntrl & DIRECTION) || (dev->sblkctl & AUTOFLUSHDIS))
        return;
    if (dev->fifo_cnt == 0)
        return;
    if ((dev->stcnt == 0) || (dev->bus_state != BUS_BUSY) || (dev->tgt_req && (dev->tgt_phase != (dev->scsisigo & PHASE_MASK))))
        dev->fifo_flush = 1;
}

/* Whether the host side has a reason to ask for the bus. */
static int
aic_dma_host_wants(const aic7880_t *dev)
{
    if (!(dev->dfcntrl & HDMAEN) || (dev->hcnt == 0))
        return 0;
    if (dev->dfcntrl & DIRECTION)
        return dev->fifo_cnt < FIFO_SIZE;
    return dev->fifo_flush || (dev->fifo_cnt >= aic_fifo_threshold(dev));
}

/* The host side of the FIFO. HDMAEN with DIRECTION set fills it from
   memory at HADDR; without, it drains it to memory. HCNT counts down and
   HDONE says it reached zero.

   The FIFO is thirty-two QUADWORDS, not 256 bytes, and that shows. On
   the way to memory nothing moves until the threshold is reached or a
   flush says "that is all there is", and short of a flush only whole
   quadwords go: eight bytes of completion status written in by the
   sequencer sit there for ever without FIFOFLUSH. On the way in, the PCI
   side has an eight byte latch that empties into the FIFO only when it
   is full or HCNT has run out. And the engine takes a moment to start,
   so the bytes are not there on the instruction after HDMAEN is set:
   HDONE is what says they are. */
static void
aic_dma_host(aic7880_t *dev)
{
    uint8_t  buf[64];
    uint32_t n;

    aic_fifo_autoflush(dev);

    if (!(dev->dfcntrl & HDMAEN) || dev->host_wait)
        return;
    /* No bus mastering, no transfer: MASTEREN in the command register. */
    if (!(dev->pci_regs[0x04] & 0x04))
        return;

    if (dev->dfcntrl & DIRECTION) {
        while (dev->hcnt && (dev->fifo_cnt < FIFO_SIZE)) {
            n = FIFO_SIZE - dev->fifo_cnt;
            if (n > sizeof(buf))
                n = sizeof(buf);
            if (n > dev->hcnt)
                n = dev->hcnt;
            else if (n >= 8)
                n &= ~7U;
            else
                break;
            dma_bm_read(dev->haddr, buf, n, 4);
            /* The small ones are the firmware's own traffic -- queue
               entries, command blocks, scatter lists -- and worth a line. */
            if (n <= 32) {
                aic_log("  dma rd %08x +%u: %02x %02x %02x %02x %02x %02x %02x %02x\n", dev->haddr, n, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
            }
            for (uint32_t i = 0; i < n; i++)
                aic_fifo_push(dev, buf[i]);
            dev->haddr += n;
            dev->hcnt -= n;
        }
    } else if (aic_dma_host_wants(dev)) {
        while (dev->hcnt && dev->fifo_cnt) {
            n = dev->fifo_cnt;
            if (!dev->fifo_flush)
                n &= ~7U;
            if (n == 0)
                break;
            if (n > sizeof(buf))
                n = sizeof(buf);
            if (n > dev->hcnt)
                n = dev->hcnt;
            for (uint32_t i = 0; i < n; i++)
                buf[i] = aic_fifo_pop(dev);
            if (n <= 32) {
                aic_log("  dma wr %08x +%u (hcnt %u): %02x %02x %02x %02x %02x %02x %02x %02x\n", dev->haddr, n, dev->hcnt, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
            }
            dma_bm_write(dev->haddr, buf, n, 4);
            dev->haddr += n;
            dev->hcnt -= n;
        }
    }

    if (dev->fifo_cnt == 0)
        dev->fifo_flush = 0;
}

/* The SCSI side. SCSIEN moves bytes between the FIFO and the bus in the
   phase the target is in, counting STCNT down; SDONE says it hit zero and
   PHASEMIS stops it early. */
static void
aic_dma_scsi(aic7880_t *dev)
{
    int out;

    if (!(dev->dfcntrl & SCSIEN) || (dev->bus_state != BUS_BUSY))
        return;

    out = !!(dev->dfcntrl & DIRECTION);

    /* The engine does its own handshake, so a REQ the target has yet to
       raise for programmed I/O is there for it at once. */
    dev->in_dma = 1;
    aic_tgt_req_due(dev);

    /* STCNT counts bytes down and SHADDR counts them up, both on the SCSI
       side: a byte sent counts when it is acknowledged, a byte received
       when it reaches the data FIFO. HADDR runs ahead of SHADDR on a write
       by whatever the host side has prefetched, which is why SHADDR and
       not HADDR is where a transfer that stops early is resumed from.
       With SWRAPEN the count runs on through zero and SWRAP says so. */
    while ((dev->stcnt || (dev->sxfrctl1 & SWRAPEN)) && dev->tgt_req) {
        /* The transfer only runs while the target stays in the phase the
           sequencer set up for; anything else is a phase mismatch, which
           the firmware detects and unwinds. */
        if (out) {
            if (dev->tgt_phase != (dev->scsisigo & PHASE_MASK))
                break;
            if (dev->fifo_cnt == 0) {
                aic_dma_host(dev);
                if (dev->fifo_cnt == 0)
                    break;
            }
            aic_tgt_take(dev, aic_fifo_pop(dev));
        } else {
            if (dev->tgt_phase != (dev->scsisigo & PHASE_MASK))
                break;
            if (dev->fifo_cnt >= FIFO_SIZE) {
                aic_dma_host(dev);
                if (dev->fifo_cnt >= FIFO_SIZE)
                    break;
            }
            aic_fifo_push(dev, aic_tgt_byte(dev));
            aic_tgt_acked(dev);
        }
        if (dev->stcnt == 0)
            aic_set_sstat0(dev, SWRAP);
        dev->stcnt = (dev->stcnt - 1) & 0xffffff;
        dev->shaddr++;
    }
    dev->in_dma = 0;

    aic_dma_host(dev);
}

/* BITBUCKET: the SCSI cell takes whatever data the target has and throws
   it away, or feeds it zeros, for as long as the target stays in the data
   phase SCSISIGO names. It is how every driver gets past a target with
   more to say than the host asked for: set the bit, wait for PHASEMIS.
   Without it that wait never ends. */
static void
aic_bitbucket(aic7880_t *dev)
{
    if (!(dev->sxfrctl1 & BITBUCKET) || (dev->bus_state != BUS_BUSY))
        return;

    dev->in_dma = 1;
    aic_tgt_req_due(dev);
    while (dev->tgt_req && (dev->bus_state == BUS_BUSY) && (dev->tgt_phase == (dev->scsisigo & PHASE_MASK)) && ((dev->tgt_phase == P_DATAIN) || (dev->tgt_phase == P_DATAOUT))) {
        if (dev->tgt_phase == P_DATAIN)
            aic_tgt_acked(dev);
        else
            aic_tgt_take(dev, 0x00);
    }
    dev->in_dma = 0;
}

/* Everything that can move, moves. Called after any register write that
   might unblock something. */
static void
aic_pump(aic7880_t *dev)
{
    /* A selection that could not have the bus is still wanted. ENSELO
       written while a target holds the bus -- most often one that has
       just reselected, which the sequencer has yet to notice -- is not
       thrown away: the chip waits for bus free, arbitrates, and selects
       then. Dropping it loses the command the firmware had just taken
       off its queue, and nothing ever asks for it again. */
    if ((dev->scsiseq & ENSELO) && !dev->selecting && (dev->bus_state == BUS_FREE) && !(dev->sstat0 & SELDO))
        aic_select_start(dev);

    aic_bitbucket(dev);
    aic_dma_scsi(dev);
    aic_dma_host(dev);
    aic_reselect_try(dev);
    aic_bus_changed(dev);
}

/* ---- register file ------------------------------------------------------ */

/* There is no RAM at 0x70 to 0x7f on this part. Later ones have sixteen
   more bytes of scratch there, and register lists written for the whole
   family show it, but here a write goes nowhere and a read returns
   nothing, and a program that parks a value there loses it.

   Reads and writes from the sequencer and from the host go through the
   same file, but a few registers differ: the sequencer's indirect and
   stack registers mean nothing to the host, and the host may not touch
   the SCSI cell while the sequencer runs. The `seq' flag says which. */

/* SCBCNT holds a byte offset into the SCB the pointer selects, and with
   SCBAUTO set every access to the SCB window uses that offset and steps
   it on. That is how a driver moves a whole SCB through one port, and
   the 1996 aic7xxx driver does exactly that rather than DMA them. */
/* Automatic PIO runs the same two counters as the DMA engine. */
static void
aic_pio_counted(aic7880_t *dev)
{
    if (dev->stcnt || (dev->sxfrctl1 & SWRAPEN)) {
        if (dev->stcnt == 0)
            aic_set_sstat0(dev, SWRAP);
        dev->stcnt = (dev->stcnt - 1) & 0xffffff;
        dev->shaddr++;
    }
}

/* The byte waiting in SCSIDATL goes out when PIO is enabled and the
   target is asking for one. */
static void
aic_pio_out(aic7880_t *dev)
{
    if (!dev->datl_full || !(dev->sxfrctl0 & SPIOEN))
        return;
    if ((dev->bus_state != BUS_BUSY) || !dev->tgt_req || (dev->tgt_phase & IOI))
        return;
    dev->datl_full = 0;
    aic_pio_counted(dev);
    aic_tgt_take(dev, dev->scsidatl);
}

static uint8_t
aic_scb_offset(aic7880_t *dev, uint8_t addr)
{
    uint8_t off;

    if (!(dev->scbcnt & SCBAUTO))
        return (uint8_t) (addr - SCB_BASE);

    off         = dev->scbcnt & 0x1f;
    dev->scbcnt = (uint8_t) ((dev->scbcnt & 0xe0) | ((off + 1) & 0x1f));
    return off;
}

/* The host is slow. By the time one of its accesses lands, anything that
   was a few sequencer instructions away has long since happened. */
static void
aic_host_catch_up(aic7880_t *dev)
{
    aic_tgt_req_due(dev);
    if (dev->host_wait) {
        dev->host_wait = 0;
        aic_pump(dev);
    }
}

static uint8_t
aic_read(aic7880_t *dev, uint8_t addr, int seq)
{
    uint8_t ret = 0;

    if (!seq)
        aic_host_catch_up(dev);

    if ((addr >= SRAM_BASE) && (addr < 0x60))
        return dev->sram[addr - SRAM_BASE];
    if (addr >= SCB_BASE) {
        if (addr < (SCB_BASE + SCB_SIZE))
            return dev->scb[dev->scbptr & (SCB_COUNT - 1)][aic_scb_offset(dev, addr)];
        return dev->misc[addr - 0xc0];
    }

    switch (addr) {
        case SCSISEQ:
            return dev->scsiseq;
        case SXFRCTL0:
            return dev->sxfrctl0;
        case SXFRCTL1:
            return dev->sxfrctl1;
        case SCSISIG: /* SCSISIGI: what is actually on the wires */
            ret = 0;
            if (dev->bus_state == BUS_BUSY) {
                ret |= BSYI | dev->tgt_phase;
                if (dev->tgt_req)
                    ret |= REQI;
            }
            if (dev->selecting)
                ret |= SELI;
            if (dev->atn)
                ret |= ATNI;
            /* These are the pins, so what we drive ourselves shows too. */
            ret |= dev->scsisigo & (SELI | BSYI | ACKI);
            return ret;
        case SCSIRATE:
            return dev->scsirate;
        case SCSIID:
            return dev->scsiid;
        case SCSIDATL:
            /* Reading latches the byte the target is offering and ACKs
               it; this is how the firmware takes a PIO byte. */
            if ((dev->bus_state == BUS_BUSY) && dev->tgt_req && (dev->tgt_phase & IOI)) {
                ret = aic_tgt_byte(dev);
                /* Without SPIOEN the read is only a look at the latch. */
                if (dev->sxfrctl0 & SPIOEN) {
                    aic_pio_counted(dev);
                    aic_tgt_acked(dev);
                }
            } else
                ret = dev->scsidatl;
            return ret;
        case SCSIDATH:
            return dev->scsidath;
        case STCNT:
            return dev->stcnt & 0xff;
        case STCNT + 1:
            return (dev->stcnt >> 8) & 0xff;
        case STCNT + 2:
            return (dev->stcnt >> 16) & 0xff;
        case SSTAT0:
            /* SDONE and DMADONE are not latched events: they say the SCSI
               count, and the whole transfer, stand at zero right now. A
               reload of STCNT takes them away, which a latch would not. */
            ret = dev->sstat0 & ~(SDONE | DMADONE);
            /* SDONE wants the count at zero, no wrapping, and one of the
               two transfer modes on. DMADONE is SDONE and HDONE. */
            if ((dev->stcnt == 0) && !(dev->sxfrctl1 & SWRAPEN) && ((dev->dfcntrl & SDMAEN) || (dev->sxfrctl0 & SPIOEN))) {
                ret |= SDONE;
                if (dev->hcnt == 0)
                    ret |= DMADONE;
            }
            return ret;
        case SSTAT1:
            return dev->sstat1;
        case SSTAT2:
            return 0;
        case SSTAT3:
            return 0;
        case SIMODE0:
            return dev->simode0;
        case SIMODE1:
            return dev->simode1;
        case SCSIBUSL:
            /* The data lines, read without acknowledging. */
            if ((dev->bus_state == BUS_BUSY) && dev->tgt_req)
                return aic_tgt_byte(dev);
            return 0;
        case SCSIBUSH:
            return 0;
        case SHADDR:
            return dev->shaddr & 0xff;
        case SHADDR + 1:
            return (dev->shaddr >> 8) & 0xff;
        case SHADDR + 2:
            return (dev->shaddr >> 16) & 0xff;
        case SHADDR + 3:
            return (dev->shaddr >> 24) & 0xff;
        case SELTIMER:
            /* With SCAMEN set, CLKOUT is a free-running clock with a
               period of 102.4 microseconds for the driver to time the
               SCAM protocol by. The divider stages are not modelled. */
            if (dev->sxfrctl0 & SCAMEN)
                return ((uint64_t) ((double) tsc * 4294967296.0 / (double) TIMER_USEC / 51.2) & 1) ? 0x80 : 0x00;
            return 0;
        case SLEEPCTL:
            return dev->sleepctl;
        case SELID:
            return dev->selid;
        case SCAMCTL:
            return dev->scamctl;
        case SPIOCAP:
            /* SEEPROM and termination sensing present, and a ROM. */
            return 0x08 | 0x02 | 0x01;
        case BRDCTL:
            /* The board's GAL. BRDRW with BRDCS reads the cable sense
               lines; bank zero has the internal connectors, bank one
               (BRDDAT5) the external one and the EEPROM-present bit. In
               an emulated machine the cables are whatever suits: one
               internal connector, no external, EEPROM present. */
            if ((dev->brdctl & (BRDRW | BRDCS)) == (BRDRW | BRDCS)) {
                if (dev->brdctl & BRDDAT5)
                    return BRDDAT7; /* ext cable absent, eeprom there */
                return BRDDAT7;     /* int68 absent, int50 present */
            }
            return dev->brdctl;
        case SEECTL:
            ret = dev->seectl & ~(SEEDI | SEERDY | EXTARBACK);
            /* Access is granted as soon as it is asked for, and the
               800ns strobe the driver times with SEERDY is instant. */
            if (dev->seectl & SEEMS)
                ret |= SEERDY;
            if (dev->seectl & EXTARBREQ)
                ret |= EXTARBACK;
            if (nmc93cxx_eeprom_read(dev->eeprom))
                ret |= SEEDI;
            return ret;
        case SBLKCTL:
            return dev->sblkctl;
        case SCSITEST:
            return dev->scsitest;

        case SEQCTL:
            return dev->seqctl;
        case SEQRAM:
            /* Readback for a driver verifying its download. */
            if (dev->pc < SEQ_INSNS) {
                ret = dev->seqram[dev->pc * 4 + dev->ram_byte];
                if (++dev->ram_byte == 4) {
                    dev->ram_byte = 0;
                    dev->pc++;
                }
            }
            return ret;
        case SEQADDR0:
            return dev->pc & 0xff;
        case SEQADDR1:
            return (dev->pc >> 8) & 0x01;
        case ACCUM:
            return dev->accum;
        case SINDEX:
            return dev->sindex;
        case DINDEX:
            return dev->dindex;
        case BRKADDR0:
            return dev->brkaddr & 0xff;
        case BRKADDR1:
            return (dev->brkaddr >> 8) & 0xff;
        case ALLONES:
            return 0xff;
        case ALLZEROS:
            return 0x00;
        case FLAGS:
            return dev->flags;
        case SINDIR:
            if (seq) {
                ret = aic_read(dev, dev->sindex, seq);
                dev->sindex++;
                return ret;
            }
            return 0;
        case DINDIR:
            return 0;
        case FUNCTION1:
            /* Write a value, read back a one-hot of its high nibble: the
               hardware's way of turning a target ID into a bit mask. */
            return (uint8_t) (1 << ((dev->function1 >> 4) & 0x07));
        case STACK:
            /* Two reads per entry, low byte first. */
            if (dev->stack_rd == 0) {
                dev->stack_rd = 1;
                return dev->stack[dev->sp & 3] & 0xff;
            }
            dev->stack_rd = 0;
            ret           = (dev->stack[dev->sp & 3] >> 8) & 0xff;
            dev->sp       = (dev->sp - 1) & 3;
            return ret;

        case DSVENDID:
        case DSVENDID + 1:
        case DSDEVID:
        case DSDEVID + 1:
            /* The vendor and device ID, readable from device space. A
               driver that has the register window but not the slot -- and
               the sequencer, which has nothing else -- tells the parts of
               the family apart here. */
            return dev->pci_regs[addr - DSVENDID];
        case DSCOMMAND0:
            /* The low four bits are SERRESPEN, PERRESPEN, MWRICEN and
               MASTEREN out of the PCI command register. */
            ret = dev->dscommand0 & 0xf0;
            if (dev->pci_regs[0x05] & 0x01)
                ret |= 0x08;
            if (dev->pci_regs[0x04] & 0x40)
                ret |= 0x04;
            if (dev->pci_regs[0x04] & 0x10)
                ret |= 0x02;
            if (dev->pci_regs[0x04] & 0x04)
                ret |= 0x01;
            return ret;
        case DSCOMMAND1:
            /* The latency timer, over HADDLDSEL. */
            return (dev->pci_regs[0x0d] & 0xfc) | (dev->dscommand1 & 0x03);
        case DSPCISTATUS:
            return dev->dspcistatus;
        case HCNTRL:
            ret = dev->hcntrl & ~PAUSE;
            if (aic_paused(dev))
                ret |= PAUSE;
            return ret;
        case HADDR:
            return dev->haddr & 0xff;
        case HADDR + 1:
            return (dev->haddr >> 8) & 0xff;
        case HADDR + 2:
            return (dev->haddr >> 16) & 0xff;
        case HADDR + 3:
            return (dev->haddr >> 24) & 0xff;
        case HCNT:
            return dev->hcnt & 0xff;
        case HCNT + 1:
            return (dev->hcnt >> 8) & 0xff;
        case HCNT + 2:
            return (dev->hcnt >> 16) & 0xff;
        case SCBPTR:
            return dev->scbptr;
        case INTSTAT:
            return dev->intstat;
        case ERROR:
            return dev->error;
        case DFCNTRL:
            /* FIFOFLUSH reads as one for as long as a flush is pending. */
            return (dev->dfcntrl & 0x7f) | (dev->fifo_flush ? FIFOFLUSH : 0);
        case DFSTATUS:
            ret = 0;
            if (dev->fifo_cnt == 0)
                ret |= FIFOEMP;
            /* Not one whole quadword: one to seven bytes do not count. */
            if (dev->fifo_cnt < 8)
                ret |= FIFOQWDEMP;
            if (dev->fifo_cnt >= FIFO_SIZE)
                ret |= FIFOFULL;
            if (dev->dfcntrl & DIRECTION) {
                if ((uint32_t) (FIFO_SIZE - dev->fifo_cnt) >= aic_fifo_threshold(dev))
                    ret |= DFTHRESH;
            } else if (dev->fifo_cnt >= aic_fifo_threshold(dev))
                ret |= DFTHRESH;
            if (dev->host_wait && aic_dma_host_wants(dev))
                ret |= MREQPEND;
            if (dev->hcnt == 0)
                ret |= HDONE;
            return ret;
        case DFWADDR:
            return dev->dfwaddr[0];
        case DFWADDR + 1:
            return dev->dfwaddr[1];
        case DFRADDR:
            return dev->dfraddr[0];
        case DFRADDR + 1:
            return dev->dfraddr[1];
        case DFDAT:
            return aic_fifo_pop(dev);
        case SCBCNT:
            return dev->scbcnt;
        case QINFIFO:
            /* The sequencer takes the next queued SCB. */
            if (dev->qin_cnt == 0)
                return 0;
            ret         = dev->qin[dev->qin_rd];
            dev->qin_rd = (dev->qin_rd + 1) % QUEUE_SIZE;
            dev->qin_cnt--;
            return ret;
        case QINCNT:
            return (uint8_t) dev->qin_cnt;
        case QOUTFIFO:
            /* The host takes the next completion. */
            if (dev->qout_cnt == 0)
                return 0xff;
            ret          = dev->qout[dev->qout_rd];
            dev->qout_rd = (dev->qout_rd + 1) % QUEUE_SIZE;
            dev->qout_cnt--;
            return ret;
        case QOUTCNT:
            return (uint8_t) dev->qout_cnt;
        case SFUNCT:
            return dev->sfunct;

        default:
            break;
    }
    return 0;
}

static void
aic_write(aic7880_t *dev, uint8_t addr, uint8_t val, int seq)
{
    uint8_t was;

    if (!seq)
        aic_host_catch_up(dev);

    if ((addr >= SRAM_BASE) && (addr < 0x60)) {
        dev->sram[addr - SRAM_BASE] = val;
        return;
    }
    if (addr >= SCB_BASE) {
        if (addr < (SCB_BASE + SCB_SIZE))
            dev->scb[dev->scbptr & (SCB_COUNT - 1)][aic_scb_offset(dev, addr)] = val;
        else
            dev->misc[addr - 0xc0] = val;
        return;
    }

    switch (addr) {
        case SCSISEQ:
            was          = dev->scsiseq;
            dev->scsiseq = val;
            if ((val & SCSIRSTO) && !(was & SCSIRSTO))
                aic_scsi_reset_bus(dev);
            if (val & ENSELO)
                aic_select_start(dev);
            else if (!(val & ENSELO) && dev->selecting) {
                dev->selecting = 0;
                dev->sstat0 &= ~SELINGO;
                timer_stop(&dev->sel_timer);
            }
            aic_pump(dev);
            break;
        case SXFRCTL0:
            was           = dev->sxfrctl0;
            dev->sxfrctl0 = val & ~(CLRSTCNT | CLRCHN | 0x01);
            /* Taking automatic PIO away takes SPIORDY with it, and turning
               it on under a REQ that is already up counts as its edge. */
            if (!(val & SPIOEN))
                dev->sstat0 &= ~SPIORDY;
            else if (!(was & SPIOEN))
                dev->req_seen = 0;
            /* CLRSTCNT zeroes both SCSI counters. CLRCHN is the SCSI cell's
               own FIFO and its offset counter, neither of which exists
               here; it leaves the counters and the data FIFO alone. */
            if (val & CLRSTCNT) {
                dev->stcnt  = 0;
                dev->shaddr = 0;
            }
            if (val & CLRCHN)
                dev->datl_full = 0;
            aic_pio_out(dev);
            aic_bus_changed(dev);
            break;
        case SXFRCTL1:
            was           = dev->sxfrctl1;
            dev->sxfrctl1 = val;
            /* The timer switched on under a selection nobody is answering. */
            if ((val & ENSTIMER) && !(was & ENSTIMER) && dev->selecting && !timer_is_on(&dev->sel_timer))
                timer_on_auto(&dev->sel_timer, 100.0);
            if (val & BITBUCKET)
                aic_pump(dev);
            break;
        case SCSISIG: /* SCSISIGO */
            was           = dev->scsisigo;
            dev->scsisigo = val;
            /* ATNO sets the ATN latch and nothing else clears it: the
               firmware drops ATN by writing CLRATNO to CLRSINT1, and it
               relies on ATN surviving the SCSISIGO write that phase_lock
               does to latch the target's phase. */
            if (val & 0x10)
                dev->atn = 1;
            if ((val & 0x01) && !(was & 0x01)) {
                /* ACKO by hand: the firmware acknowledging a byte it
                   took through SCSIBUSL rather than SCSIDATL. */
                if ((dev->bus_state == BUS_BUSY) && dev->tgt_req)
                    aic_tgt_acked(dev);
            }
            aic_bus_changed(dev);
            break;
        case SCSIRATE:
            dev->scsirate = val;
            break;
        case SCSIID:
            dev->scsiid = val;
            break;
        case SCSIDATL:
            /* Writing offers a byte to the target and acknowledges the
               request; this is PIO out. */
            aic_log("pio out %02x (%s)\n", val,
                    (dev->bus_state == BUS_BUSY) ? aic_phase_name(dev->tgt_phase) : "free");
            dev->scsidatl = val;
            /* Automatic PIO is what SPIOEN enables. Adaptec's firmware
               loads the latch with it off and then turns it on, so the
               byte must wait here until it is. */
            dev->datl_full = 1;
            aic_pio_out(dev);
            break;
        case SCSIDATH:
            dev->scsidath = val;
            break;
        case STCNT:
            dev->stcnt = (dev->stcnt & 0xffff00) | val;
            break;
        case STCNT + 1:
            dev->stcnt = (dev->stcnt & 0xff00ff) | (val << 8);
            break;
        case STCNT + 2:
            dev->stcnt = (dev->stcnt & 0x00ffff) | (val << 16);
            break;
        case SSTAT0: /* CLRSINT0 */
            dev->sstat0 &= ~(val & 0x7a);
            aic_bus_changed(dev);
            break;
        case SSTAT1: /* CLRSINT1 */
            /* RST cannot be cleared from under ourselves: while we hold the
               line down the status comes straight back. */
            if (dev->scsiseq & SCSIRSTO)
                val &= ~SCSIRSTI;
            dev->sstat1 &= ~(val & 0xaf);
            if (val & REQINIT)
                dev->sstat1 &= ~PHASEMIS;
            if (val & CLRATNO) {
                dev->atn = 0;
                dev->scsisigo &= ~0x10;
            }
            aic_bus_changed(dev);
            break;
        case SIMODE0:
            dev->simode0 = val;
            aic_scsi_int(dev);
            break;
        case SIMODE1:
            dev->simode1 = val;
            aic_scsi_int(dev);
            break;
        case SCSIBUSL:
            break;
        case SELTIMER: /* read only */
            break;
        case SLEEPCTL:
            /* Only the sequencer can put itself to sleep, and not at all
               with SLEEPDIS set. */
            dev->sleepctl = val & SLEEPDIS;
            if (seq && !(val & SLEEPDIS))
                dev->sleepctl |= val & (SLP1 | SLP0);
            break;
        case SELID:
            dev->selid = val;
            break;
        case SCAMCTL:
            dev->scamctl = val;
            break;
        case SPIOCAP:
            break;
        case BRDCTL:
            dev->brdctl = val;
            break;
        case SEECTL:
            dev->seectl = val;
            nmc93cxx_eeprom_write(dev->eeprom, !!(val & SEECS), !!(val & SEECK),
                                  !!(val & SEEDO));
            break;
        case SBLKCTL:
            dev->sblkctl = val & (DIAGLEDEN | DIAGLEDON | AUTOFLUSHDIS | SELWIDE);
            break;
        case SCSITEST:
            dev->scsitest = val;
            break;

        case SEQCTL:
            was = dev->seqctl;
            if (!seq) {
                aic_log("host: SEQCTL %02x at pc %03x (intstat %02x sstat0 %02x "
                        "sstat1 %02x)\n",
                        val, dev->pc, dev->intstat, dev->sstat0,
                        dev->sstat1);
            }
            /* SEQRESET clears itself. It also does not reliably take in
               the write that drops LOADRAM: a part that had just had its
               store read back was seen to start from where the read-back
               left the address, and every driver there is writes SEQADDR
               to zero by hand afterwards. */
            /* PAUSEDIS is the sequencer's own: the host cannot write it. */
            if (!seq)
                val = (val & ~PAUSEDIS) | (was & PAUSEDIS);
            dev->seqctl = val & ~SEQRESET;
            if ((val & SEQRESET) && !((was & LOADRAM) && !(val & LOADRAM))) {
                dev->pc       = 0;
                dev->sp       = 0;
                dev->ram_byte = 0;
            }
            if ((val & LOADRAM) && !(was & LOADRAM))
                dev->ram_byte = 0;
            if (!(val & LOADRAM) && (was & LOADRAM))
                dev->ram_byte = 0;
            aic_update_irq(dev);
            break;
        case SEQRAM:
            if (dev->seqctl & LOADRAM) {
                if (dev->pc < SEQ_INSNS)
                    dev->seqram[dev->pc * 4 + dev->ram_byte] = val;
                if (++dev->ram_byte == 4) {
                    dev->ram_byte = 0;
                    dev->pc++;
                }
            }
            break;
        case SEQADDR0:
            if (!seq) {
                aic_log("host: SEQADDR0 %02x at pc %03x\n", val, dev->pc);
            }
            dev->pc       = (dev->pc & 0x100) | val;
            dev->ram_byte = 0;
            break;
        case SEQADDR1:
            dev->pc       = (dev->pc & 0xff) | ((val & 0x01) << 8);
            dev->ram_byte = 0;
            break;
        case ACCUM:
            dev->accum = val;
            break;
        case SINDEX:
            dev->sindex = val;
            break;
        case DINDEX:
            dev->dindex = val;
            break;
        case BRKADDR0:
            dev->brkaddr = (dev->brkaddr & 0xff00) | val;
            break;
        case BRKADDR1:
            dev->brkaddr = (dev->brkaddr & 0x00ff) | (val << 8);
            break;
        case ALLZEROS: /* NONE: a write that goes nowhere */
            break;
        case SINDIR:
            break;
        case DINDIR:
            if (seq) {
                aic_write(dev, dev->dindex, val, seq);
                dev->dindex++;
            }
            break;
        case FUNCTION1:
            dev->function1 = val;
            break;
        case STACK:
            break;

        case DSCOMMAND0:
            dev->dscommand0 = val & 0xf0;
            break;
        case DSCOMMAND1:
            dev->dscommand1 = val & 0x03;
            break;
        case DSPCISTATUS:
            /* Only the FIFO threshold select can be written. */
            dev->dspcistatus = val & DFTHRSH_100;
            aic_pump(dev);
            break;
        case HCNTRL:
            if (!seq && ((val ^ dev->hcntrl) & CHIPRST)) {
                aic_log("host: HCNTRL %02x at pc %03x (intstat %02x)\n", val,
                        dev->pc, dev->intstat);
            }
            was         = dev->hcntrl;
            dev->hcntrl = val & ~CHIPRST;
            if (val & CHIPRST) {
                /* A chip reset from the host; CHIPRSTACK reads back in
                   the same bit to say it happened. */
                aic_chip_reset(dev);
                dev->hcntrl |= CHIPRST;
                return;
            }
            /* SWINT drives the interrupt pin and nothing else: it is how
               the BIOS gets its own handler run. It is not a sequencer
               interrupt and must not look like one in INTSTAT, or the
               handler will think the firmware stopped in mid-transfer. */
            aic_update_irq(dev);
            /* Any write that leaves PAUSE clear ends a sleep. */
            if (!(val & PAUSE))
                dev->sleepctl &= ~(SLP1 | SLP0);
            if (!(val & PAUSE) && (was & PAUSE)) {
                /* Releasing PAUSE always gets one instruction executed,
                   whatever else wants the sequencer stopped. Single step
                   and resuming from a breakpoint both rest on that. */
                dev->must_step = 1;
                aic_seq_kick(dev);
                /* Run it here and now: a host that re-pauses a microsecond
                   later would otherwise never let the timer fire, and the
                   sequencer would look stopped. */
                if (!seq)
                    aic_seq_run(dev);
            }
            break;
        /* Loading the host address loads SHADDR with it, unless HADDLDSEL
           has pointed these four addresses at the high half of a 64-bit
           address, which is kept and never used. */
        case HADDR:
        case HADDR + 1:
        case HADDR + 2:
        case HADDR + 3:
            if (dev->dscommand1 & 0x03) {
                dev->misc[addr - HADDR] = val;
                break;
            }
            dev->haddr  = (dev->haddr & ~(0xffU << ((addr - HADDR) * 8))) | ((uint32_t) val << ((addr - HADDR) * 8));
            dev->shaddr = (dev->shaddr & ~(0xffU << ((addr - HADDR) * 8))) | ((uint32_t) val << ((addr - HADDR) * 8));
            break;
        case HCNT:
            dev->hcnt = (dev->hcnt & 0xffff00) | val;
            break;
        case HCNT + 1:
            dev->hcnt = (dev->hcnt & 0xff00ff) | (val << 8);
            break;
        case HCNT + 2:
            dev->hcnt = (dev->hcnt & 0x00ffff) | (val << 16);
            break;
        case SCBPTR:
            dev->scbptr = val;
            break;
        case INTSTAT:
            /* The sequencer writes its interrupt code here. */
            aic_log("[%.3f ms] seq: intstat %02x at %03x\n", aic_now_us() / 1000.0, val, dev->pc);
#ifdef ENABLE_AIC7880_LOG
            /* Anything but a plain command complete or the delay timer:
               say how it got here. */
            if ((val & SEQINT) && ((val & 0xf0) != 0x00)) {
                for (int t = 0; t < 256; t++) {
                    uint8_t k = (uint8_t) (dev->trail_at + t);
                    aic_log("  trail %03x: %08x stcnt %06x hcnt %06x fifo %u sstat1 %02x dfcntrl %02x\n",
                            dev->trail[k].pc, dev->trail[k].insn, dev->trail[k].stcnt, dev->trail[k].hcnt,
                            dev->trail[k].fifo, dev->trail[k].sstat1, dev->trail[k].dfcntrl);
                }
            }
#endif
            /* The low four bits are interrupts, and writing one sets it:
               only CLRINT takes one away. A command complete the host has
               not collected yet must survive the sequencer interrupt that
               comes after it. The high four are the sequencer's reason
               code, and go with SEQINT. */
            if (val & SEQINT)
                dev->intstat = (dev->intstat & INT_PEND) | val;
            else
                dev->intstat |= val & INT_PEND;
            aic_update_irq(dev);
            break;
        case ERROR: /* CLRINT */
            if (!seq) {
                aic_log("host: CLRINT %02x (intstat %02x) at pc %03x\n", val,
                        dev->intstat, dev->pc);
            }
            if (val & CLRBRKADRINT)
                dev->intstat &= ~BRKADRINT;
            if (val & CLRSCSIINT)
                dev->intstat &= ~SCSIINT;
            if (val & CLRCMDINT)
                dev->intstat &= ~CMDCMPLT;
            if (val & CLRSEQINT)
                dev->intstat &= ~SEQINT;
            /* Not ILLOPCODE: only a chip reset gets rid of that. */
            if (val & CLRPARERR)
                dev->error &= ILLOPCODE;
            aic_update_irq(dev);
            /* SCSIINT reads clear only once its cause has been dealt with;
               with the cause still standing it comes straight back. */
            if (val & CLRSCSIINT)
                aic_scsi_int(dev);
            aic_seq_kick(dev);
            break;
        case DFCNTRL:
            was = dev->dfcntrl;
            /* FIFORESET is a strobe and reads back clear. Firmware turns
               the engine off with a read-modify-write, and a reset bit
               that stuck would empty the FIFO it is about to read. */
            dev->dfcntrl = val & ~(FIFORESET | FIFOFLUSH);
            if (val & FIFORESET)
                aic_fifo_reset(dev);
            /* A flush by hand: nothing to do on an empty FIFO, and nothing
               on the way out to the SCSI bus. */
            if ((val & FIFOFLUSH) && dev->fifo_cnt && !(val & DIRECTION))
                dev->fifo_flush = 1;
            if ((val & HDMAEN) && !(was & HDMAEN))
                dev->host_wait = AIC_HOST_INSNS;
            aic_pump(dev);
            if ((was & (SCSIEN | HDMAEN)) && !(val & (SCSIEN | HDMAEN)))
                aic_seq_kick(dev);
            break;
        case DFWADDR:
            dev->dfwaddr[0] = val;
            break;
        case DFWADDR + 1:
            dev->dfwaddr[1] = val;
            break;
        case DFRADDR:
            dev->dfraddr[0] = val;
            break;
        case DFRADDR + 1:
            dev->dfraddr[1] = val;
            break;
        case DFDAT:
            aic_fifo_push(dev, val);
            if (dev->dfcntrl & (SCSIEN | HDMAEN))
                aic_pump(dev);
            break;
        case SCBCNT:
            dev->scbcnt = val;
            break;
        case QINFIFO:
            /* The host queues an SCB for the sequencer. */
            if (dev->qin_cnt < QUEUE_SIZE) {
                dev->qin[(dev->qin_rd + dev->qin_cnt) % QUEUE_SIZE] = val & 0x0f;
                dev->qin_cnt++;
            }
            aic_seq_kick(dev);
            break;
        case QOUTFIFO:
            /* The sequencer posts a completion. */
            if (dev->qout_cnt < QUEUE_SIZE) {
                dev->qout[(dev->qout_rd + dev->qout_cnt) % QUEUE_SIZE] = val & 0x0f;
                dev->qout_cnt++;
            }
            break;
        case SFUNCT:
            dev->sfunct = val;
            break;

        default:
            break;
    }
}

/* ---- the sequencer ------------------------------------------------------
 *
 * Instructions are four bytes, little endian, in the packed form the
 * assembler emits for parts before the Ultra2 (aic7xxx_core.c,
 * ahc_download_instr):
 *
 *     format 1:  immediate | source << 8 | destination << 16
 *                | return << 24 | opcode << 25
 *     format 3:  immediate | source << 8 | address << 16 | opcode << 25
 *
 * An immediate of zero means the accumulator for the arithmetic and the
 * compare opcodes -- which is why the assembler refuses a literal zero
 * there and makes you write A. The jump opcodes take it literally, since
 * they use it to pass a constant to the code they jump to.
 */

#define OP_OR   0x0
#define OP_AND  0x1
#define OP_XOR  0x2
#define OP_ADD  0x3
#define OP_ADC  0x4
#define OP_ROL  0x5
#define OP_BMOV 0x6
#define OP_JMP  0x8
#define OP_JC   0x9
#define OP_JNC  0xa
#define OP_CALL 0xb
#define OP_JNE  0xc
#define OP_JNZ  0xd
#define OP_JE   0xe
#define OP_JZ   0xf

static void
aic_seq_push(aic7880_t *dev, uint16_t addr)
{
    dev->stack[dev->sp & 3] = addr;
    dev->sp                 = (dev->sp + 1) & 3;
}

static uint16_t
aic_seq_pop(aic7880_t *dev)
{
    dev->sp = (dev->sp - 1) & 3;
    return dev->stack[dev->sp & 3];
}

/* The arithmetic operations set both flags. */
static void
aic_seq_flags(aic7880_t *dev, uint8_t result, int carry)
{
    dev->flags = 0;
    if (result == 0)
        dev->flags |= ZERO;
    if (carry)
        dev->flags |= CARRY;
}

/* The logical operations and the rotate set ZERO and LEAVE CARRY ALONE.
   Adaptec's own firmware proves it: the routine that turns an SCB number
   into a host address puts a mov between an add and its adc, and an and
   between two adcs, and the twenty-four bit sum it builds is only right if
   the carry survives both.  Clearing it here instead makes every such
   address computation land in the wrong page.  */
static void
aic_seq_flags_logic(aic7880_t *dev, uint8_t result)
{
    dev->flags &= CARRY;
    if (result == 0)
        dev->flags |= ZERO;
}

/* The shift control byte of a ROL: the low four bits are how far to
   rotate left, the high four how many bits to then clear, and bit 3
   says which end they are cleared from. That is how the assembler
   builds shl, shr, rol and ror out of the one instruction. */
static uint8_t
aic_rotate(uint8_t src, uint8_t ctl)
{
    uint8_t count = ctl & 0x0f;
    uint8_t nbits = (ctl >> 4) & 0x0f;
    uint8_t ret;
    uint8_t mask;

    count &= 0x07;
    ret = (uint8_t) ((src << count) | (src >> (8 - count)));
    if (count == 0)
        ret = src;

    if (nbits >= 8)
        mask = 0x00;
    else if (ctl & 0x08)
        mask = (uint8_t) (0xff >> nbits);
    else
        mask = (uint8_t) (0xff << nbits);

    return ret & mask;
}

static void
aic_seq_step(aic7880_t *dev)
{
    uint32_t insn;
    uint8_t  opcode;
    uint8_t  imm;
    uint8_t  src;
    uint8_t  dest;
    uint8_t  ret_bit;
    uint16_t addr;
    uint8_t  a;
    uint8_t  b;
    uint8_t  res;
    int      carry = 0;
    int      taken;

    if (dev->pc >= SEQ_INSNS) {
        dev->error |= ILLOPCODE;
        aic_raise(dev, BRKADRINT);
        return;
    }

    insn = (uint32_t) dev->seqram[dev->pc * 4] | ((uint32_t) dev->seqram[dev->pc * 4 + 1] << 8) | ((uint32_t) dev->seqram[dev->pc * 4 + 2] << 16) | ((uint32_t) dev->seqram[dev->pc * 4 + 3] << 24);

    opcode  = (insn >> 25) & 0x0f;
    imm     = insn & 0xff;
    src     = (insn >> 8) & 0xff;
    dest    = (insn >> 16) & 0xff;
    ret_bit = (insn >> 24) & 0x01;
    addr    = (insn >> 16) & 0x1ff;

    /* An instruction's worth of time has gone by on the bus and in the
       host block. */
    if (dev->req_wait && (--dev->req_wait == 0)) {
        dev->req_wait = 1;
        aic_tgt_req_due(dev);
    }
    if (dev->host_wait && (--dev->host_wait == 0))
        aic_pump(dev);

#ifdef ENABLE_AIC7880_LOG
    dev->trail[dev->trail_at].pc      = dev->pc;
    dev->trail[dev->trail_at].insn    = insn;
    dev->trail[dev->trail_at].stcnt   = dev->stcnt;
    dev->trail[dev->trail_at].hcnt    = dev->hcnt;
    dev->trail[dev->trail_at].fifo    = dev->fifo_cnt;
    dev->trail[dev->trail_at].sstat1  = dev->sstat1;
    dev->trail[dev->trail_at].dfcntrl = dev->dfcntrl;
    dev->trail_at++;
#endif

    if (AIC7880_LOG_SEQ) {
        aic_log("seq %03x: %08x op %x imm %02x src %02x dst %02x%s\n", dev->pc,
                insn, opcode, imm, src, dest, ret_bit ? " ret" : "");
    }

    dev->pc++;

    switch (opcode) {
        case OP_OR:
        case OP_AND:
        case OP_XOR:
        case OP_ADD:
        case OP_ADC:
            a = aic_read(dev, src, 1);
            b = (imm == 0) ? dev->accum : imm;
            switch (opcode) {
                case OP_OR:
                    res = a | b;
                    break;
                case OP_AND:
                    res = a & b;
                    break;
                case OP_XOR:
                    res = a ^ b;
                    break;
                case OP_ADD:
                    carry = ((int) a + (int) b) > 0xff;
                    res   = (uint8_t) (a + b);
                    break;
                default:
                    { /* ADC */
                        int c = (dev->flags & CARRY) ? 1 : 0;
                        carry = ((int) a + (int) b + c) > 0xff;
                        res   = (uint8_t) (a + b + c);
                        break;
                    }
            }
            if ((opcode == OP_ADD) || (opcode == OP_ADC))
                aic_seq_flags(dev, res, carry);
            else
                aic_seq_flags_logic(dev, res);
            aic_write(dev, dest, res, 1);
            break;

        case OP_ROL:
            a   = aic_read(dev, src, 1);
            res = aic_rotate(a, imm);
            aic_seq_flags_logic(dev, res);
            aic_write(dev, dest, res, 1);
            break;

        case OP_BMOV:
            {
                /* The immediate is a byte count; source and destination both
                   walk forward. A count of one is what the assembler emits
                   for a plain mov. */
                uint8_t n = imm ? imm : 1;
                for (uint8_t i = 0; i < n; i++) {
                    res = aic_read(dev, (uint8_t) (src + i), 1);
                    aic_write(dev, (uint8_t) (dest + i), res, 1);
                }
                break;
            }

        case OP_JMP:
        case OP_CALL:
        case OP_JC:
        case OP_JNC:
            /* These also pass an argument: the source register ORed with
               the immediate lands in SINDEX, which is how the firmware
               hands a value to the routine it is calling. */
            a = aic_read(dev, src, 1);
            aic_write(dev, SINDEX, (uint8_t) (a | imm), 1);
            taken = 1;
            if (opcode == OP_JC)
                taken = !!(dev->flags & CARRY);
            else if (opcode == OP_JNC)
                taken = !(dev->flags & CARRY);
            if (taken) {
                if (opcode == OP_CALL)
                    aic_seq_push(dev, dev->pc);
                dev->pc = addr;
                return; /* a taken branch cannot also return */
            }
            break;

        case OP_JE:
        case OP_JNE:
            a   = aic_read(dev, src, 1);
            b   = (imm == 0) ? dev->accum : imm;
            res = a ^ b; /* a compare is an exclusive-or, not a subtract */
            aic_seq_flags(dev, res, 0);
            taken = (opcode == OP_JE) ? (res == 0) : (res != 0);
            if (taken) {
                dev->pc = addr;
                return;
            }
            break;

        case OP_JZ:
        case OP_JNZ:
            a   = aic_read(dev, src, 1);
            b   = (imm == 0) ? dev->accum : imm;
            res = a & b;
            aic_seq_flags(dev, res, 0);
            taken = (opcode == OP_JZ) ? (res == 0) : (res != 0);
            if (taken) {
                dev->pc = addr;
                return;
            }
            break;

        default:
            dev->error |= ILLOPCODE;
            dev->seqctl &= ~PAUSEDIS;
            aic_raise(dev, BRKADRINT);
            return;
    }

    /* Only the arithmetic formats have a return bit. In a jump, bit 24
       is the top bit of the nine-bit address. */
    if (ret_bit && (opcode < OP_JMP))
        dev->pc = aic_seq_pop(dev);

    if (!(dev->brkaddr & 0x8000) && (dev->pc == (dev->brkaddr & 0x1ff)))
        aic_raise(dev, BRKADRINT);
}

/* The sequencer runs in bursts on a timer rather than in step with the
   CPU. A burst stops when the program pauses, interrupts, or starts
   spinning on something only an outside event can change -- which is
   most of what this firmware does while it waits for the bus. */
#define SEQ_BURST 200

/* How many times round a one-instruction loop before it is taken for a
   wait on the outside world. It has to be longer than the waits that
   resolve by themselves within a few instructions -- REQ coming back
   between PIO bytes, the host side of the FIFO starting up -- or every one
   of those costs a timer period instead of a few instructions. */
#define SEQ_PARK 16

/* The most instructions it can be owed at once: what it would run in two
   hundred microseconds. Emulated time moves in steps, and without a limit
   one long step would be paid back as a burst nothing could interrupt. */
#define SEQ_CREDIT_MAX 2000.0

static void
aic_seq_run(aic7880_t *dev)
{
    uint16_t last_pc = 0xffff;
    int      same    = 0;
    int      limit   = SEQ_BURST;
    int      stopped = 0;
    int      n;

    if (dev->in_seq)
        return;
    dev->in_seq = 1;

    /* The sequencer's clock is the 40 MHz input divided by four, or by
       five without FASTMODE, and an instruction takes one cycle: ten or
       eight million a second. It earns instructions for the time that has
       passed while it was able to run, and none for time spent paused or
       waiting on the bus. */
    {
        double now = aic_now_us();

        if (dev->seq_idle)
            dev->seq_credit = 0.0;
        else
            dev->seq_credit += (now - dev->seq_last) * ((dev->seqctl & FASTMODE) ? 10.0 : 8.0);
        dev->seq_last = now;
        dev->seq_idle = 0;
        if (dev->seq_credit > SEQ_CREDIT_MAX)
            dev->seq_credit = SEQ_CREDIT_MAX;
        limit = (int) dev->seq_credit;
        if ((limit < 1) && dev->must_step)
            limit = 1;
    }

    for (n = 0; n < limit; n++) {
        if (dev->must_step)
            dev->must_step = 0;
        else if (aic_paused(dev)) {
            stopped = 1;
            break;
        }

        /* Asleep: nothing runs until one of the conditions it chose to be
           woken by is true. */
        if (dev->sleepctl & (SLP1 | SLP0)) {
            uint8_t s0 = aic_read(dev, SSTAT0, 1);

            if (((dev->sleepctl & SLP0) && (s0 & (SELDO | SELDI))) || ((dev->sleepctl & SLP1) && ((s0 & DMADONE) || (dev->sstat1 & PHASEMIS))))
                dev->sleepctl &= ~(SLP1 | SLP0);
            else {
                dev->asleep = 1;
                stopped     = 1;
                break;
            }
        }

        if (dev->pc == last_pc) {
            /* A one-instruction loop is the firmware's way of waiting;
               there is no point running it until the timer expires. */
            if (++same > SEQ_PARK) {
                dev->asleep = 1;
                if (dev->pc != dev->last_park) {
                    dev->last_park = dev->pc;
                    aic_log("seq: parked at %03x (sstat0 %02x sstat1 %02x "
                            "scsisigo %02x dfcntrl %02x stcnt %06x hcnt %06x "
                            "fifo %u phase %s%s)\n",
                            dev->pc, dev->sstat0, dev->sstat1, dev->scsisigo,
                            dev->dfcntrl, dev->stcnt, dev->hcnt, dev->fifo_cnt,
                            (dev->bus_state == BUS_BUSY) ? aic_phase_name(dev->tgt_phase)
                                                         : "free",
                            dev->tgt_req ? " req" : "");
                }
                stopped = 1;
                break;
            }
        } else
            same = 0;
        last_pc = dev->pc;

        aic_seq_step(dev);

        if (dev->seqctl & STEP) {
            /* Single step: one instruction, and PAUSE sets itself again. */
            dev->hcntrl |= PAUSE;
            stopped = 1;
            n++;
            break;
        }
    }

    dev->seq_credit -= (double) n;
    if (dev->seq_credit < 0.0)
        dev->seq_credit = 0.0;
    dev->seq_idle = (uint8_t) stopped;

    dev->in_seq = 0;
}

static void
aic_seq_timer(void *priv)
{
    aic7880_t *dev = (aic7880_t *) priv;

    /* Microseconds have passed: whatever was a few instructions away has
       happened. */
    dev->host_wait = 0;
    aic_tgt_req_due(dev);

    if ((dev->scsiseq & ENSELO) && !dev->selecting && (dev->bus_state == BUS_FREE) && !(dev->sstat0 & SELDO))
        aic_select_start(dev);

    aic_bitbucket(dev);
    aic_dma_host(dev);
    aic_dma_scsi(dev);
    aic_reselect_try(dev);

    if (!aic_paused(dev))
        aic_seq_run(dev);

    /* Keep the clock running while there is anything the sequencer could
       still be woken by. */
    if (!aic_paused(dev) || (dev->bus_state == BUS_BUSY) || dev->selecting || dev->qin_cnt || (dev->dfcntrl & (SCSIEN | HDMAEN)) || aic_any_disconnected(dev))
        timer_on_auto(&dev->seq_timer, dev->asleep ? 50.0 : 10.0);
}

static void
aic_seq_kick(aic7880_t *dev)
{
    dev->asleep = 0;
    if (!timer_is_on(&dev->seq_timer))
        timer_on_auto(&dev->seq_timer, 1.0);
}

/* ---- reset -------------------------------------------------------------- */

static void
aic_chip_reset(aic7880_t *dev)
{
    aic_log("chip reset\n");

    timer_stop(&dev->seq_timer);
    timer_stop(&dev->sel_timer);
    timer_stop(&dev->tgt_timer);

    for (uint8_t i = 0; i < AIC_CMDS; i++) {
        if (dev->cmds[i].used)
            aic_cmd_free(dev, &dev->cmds[i]);
    }

    dev->scsiseq = dev->sxfrctl0 = dev->sxfrctl1 = 0;
    dev->scsisigo = dev->scsirate = 0;
    dev->scsiid                   = 0x07; /* our ID until the driver says otherwise */
    dev->scsidatl = dev->scsidath = 0;
    dev->stcnt                    = 0;
    dev->sstat0 = dev->sstat1 = 0;
    dev->simode0 = dev->simode1 = 0;
    dev->shaddr                 = 0;
    dev->selid                  = 0;
    dev->scamctl = dev->brdctl = dev->seectl = 0;
    /* The LED bits come up set, and SELWIDE as the WIDEPS# pin is strapped. */
    dev->sblkctl    = DIAGLEDEN | DIAGLEDON | (dev->wide ? SELWIDE : 0);
    dev->scsitest   = 0;
    dev->sleepctl   = 0;
    dev->must_step  = 0;
    dev->seq_idle   = 1;
    dev->seq_credit = 0.0;

    dev->seqctl   = PERRORDIS | FASTMODE;
    dev->pc       = 0;
    dev->ram_byte = 0;
    dev->accum = dev->sindex = dev->dindex = 0;
    dev->flags = dev->function1 = 0;
    dev->brkaddr                = 0x8000; /* BRKDIS */
    dev->sp = dev->stack_rd = 0;
    memset(dev->stack, 0, sizeof(dev->stack));

    dev->dscommand0  = 0;
    dev->dscommand1  = 0;
    dev->dspcistatus = 0;
    /* CHIPRSTACK reads back in the same bit as CHIPRST, and says the
       part has not been touched since it was reset. */
    dev->hcntrl  = PAUSE | CHIPRST;
    dev->haddr   = 0;
    dev->hcnt    = 0;
    dev->scbptr  = 0;
    dev->intstat = 0;
    dev->error   = 0;
    dev->dfcntrl = 0;
    dev->scbcnt  = 0;
    dev->sfunct  = 0;

    aic_fifo_reset(dev);
    dev->qin_rd = dev->qout_rd = 0;
    dev->qin_cnt = dev->qout_cnt = 0;

    memset(dev->sram, 0, sizeof(dev->sram));
    memset(dev->scb, 0, sizeof(dev->scb));
    memset(dev->misc, 0, sizeof(dev->misc));

    dev->bus_state = BUS_FREE;
    dev->atn = dev->selecting = 0;
    dev->tgt_phase = dev->tgt_req = 0;
    dev->cur                      = NULL;
    dev->msgin_len = dev->msgin_pos = dev->msgout_len = 0;
    dev->datl_full                                    = 0;
    dev->asleep                                       = 0;
    dev->req_seen = dev->req_wait = dev->in_dma = 0;
    dev->host_wait                              = 0;

    aic_update_irq(dev);
}

/* ---- the SEEPROM -------------------------------------------------------- */

/* Thirty-two words of Adaptec's configuration format, as a card with its
   BIOS enabled and everything at the factory setting would carry, plus
   the checksum the driver verifies. Without this the driver falls back
   to defaults and says it could not read the SEEPROM. */
static void
aic_seeprom_build(const aic7880_t *dev, uint16_t *nvr)
{
    uint16_t sum;
    uint16_t flags;

    for (uint8_t i = 0; i < 64; i++)
        nvr[i] = 0xffff;

    /* These are not inferred: they are what the AHA-2940UW BIOS v2.20.0
       itself wrote when told to restore the host adapter defaults.
       Per target: leave the write cache alone, include in the BIOS scan,
       send START UNIT, synchronous at the Ultra rate, disconnection
       allowed -- and wide, on a wide card. */
    flags = 0xc000 | 0x0200 | 0x0100 | 0x0040 | 0x0010 | 0x0008;
    if (dev->wide)
        flags |= 0x0020;

    /* The BIOS keeps its configuration in the second half of the part,
       checksummed over words 32 to 62 with no signature word, and only
       falls back to the first half when that copy does not check out.
       The drivers read the first half, which carries the 0x250 signature
       instead. Both are filled in, and they say the same thing. */
    for (uint8_t half = 0; half < 2; half++) {
        uint16_t *w = &nvr[half * 32];

        for (uint8_t i = 0; i < 16; i++)
            w[i] = flags;

        /* bios_control. Bits 2-3 are one field to this BIOS (0x04 enabled,
           0x08 disabled but scan the bus), and 0x1000, which no driver
           header names, is "BIOS support for Int13 extensions": without it
           the BIOS refuses every AH=42h read and nothing can boot from a
           CD. The rest: removable boot drives, Ctrl-A message, extended
           translation, bootable CD-ROM. */
        w[16] = 0x1000 | 0x0800 | 0x0080 | 0x0020 | 0x0004 | 0x0002;
        /* adapter_control: auto termination, Ultra, both halves
           terminated, parity, reset the bus at start. */
        w[17] = 0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0010 | 0x0040;
        /* bus release time 0x28, and our SCSI ID is seven. */
        w[18] = 0x2807;
        w[19] = dev->wide ? 0x0010 : 0x0008;

        if (half == 0) {
            for (uint8_t i = 20; i < 30; i++)
                w[i] = 0x0000;
            w[30] = 0x0250;
        }

        sum = 0;
        for (uint8_t i = 0; i < 31; i++)
            sum = (uint16_t) (sum + w[i]);
        w[31] = sum;
    }
}

/* ---- the register window ------------------------------------------------ */

static uint8_t
aic_io_readb(uint16_t port, void *priv)
{
    aic7880_t *dev = (aic7880_t *) priv;
    uint8_t    reg = (uint8_t) (port - dev->io_base);
    uint8_t    ret = aic_read(dev, reg, 0);

    if (AIC7880_LOG_REGS) {
        aic_log("  in  %02x -> %02x\n", reg, ret);
    }
    return ret;
}

static uint16_t
aic_io_readw(uint16_t port, void *priv)
{
    return aic_io_readb(port, priv) | (aic_io_readb(port + 1, priv) << 8);
}

static uint32_t
aic_io_readl(uint16_t port, void *priv)
{
    return aic_io_readw(port, priv) | ((uint32_t) aic_io_readw(port + 2, priv) << 16);
}

static void
aic_io_writeb(uint16_t port, uint8_t val, void *priv)
{
    aic7880_t *dev = (aic7880_t *) priv;
    uint8_t    reg = (uint8_t) (port - dev->io_base);

    if (AIC7880_LOG_REGS) {
        aic_log("  out %02x <- %02x\n", reg, val);
    }
    aic_write(dev, reg, val, 0);
}

static void
aic_io_writew(uint16_t port, uint16_t val, void *priv)
{
    aic_io_writeb(port, val & 0xff, priv);
    aic_io_writeb(port + 1, (val >> 8) & 0xff, priv);
}

static void
aic_io_writel(uint16_t port, uint32_t val, void *priv)
{
    aic_io_writew(port, val & 0xffff, priv);
    aic_io_writew(port + 2, (val >> 16) & 0xffff, priv);
}

static uint8_t
aic_mem_readb(uint32_t addr, void *priv)
{
    aic7880_t *dev = (aic7880_t *) priv;

    return aic_read(dev, (uint8_t) (addr & 0xff), 0);
}

static uint16_t
aic_mem_readw(uint32_t addr, void *priv)
{
    return aic_mem_readb(addr, priv) | (aic_mem_readb(addr + 1, priv) << 8);
}

static uint32_t
aic_mem_readl(uint32_t addr, void *priv)
{
    return aic_mem_readw(addr, priv) | ((uint32_t) aic_mem_readw(addr + 2, priv) << 16);
}

static void
aic_mem_writeb(uint32_t addr, uint8_t val, void *priv)
{
    aic7880_t *dev = (aic7880_t *) priv;

    aic_write(dev, (uint8_t) (addr & 0xff), val, 0);
}

static void
aic_mem_writew(uint32_t addr, uint16_t val, void *priv)
{
    aic_mem_writeb(addr, val & 0xff, priv);
    aic_mem_writeb(addr + 1, (val >> 8) & 0xff, priv);
}

static void
aic_mem_writel(uint32_t addr, uint32_t val, void *priv)
{
    aic_mem_writew(addr, val & 0xffff, priv);
    aic_mem_writew(addr + 2, (val >> 16) & 0xffff, priv);
}

/* BAR0 is 256 bytes of I/O space and BAR1 the same in memory; the driver
   picks whichever the machine's BIOS mapped. */
static void
aic_io_update(aic7880_t *dev)
{
    uint16_t want = (uint16_t) (((uint16_t) dev->pci_regs[0x11] << 8) | (dev->pci_regs[0x10] & 0x00));
    uint8_t  on   = (dev->pci_regs[0x04] & PCI_COMMAND_IO) && (want != 0);

    if (dev->io_live && (!on || (want != dev->io_base))) {
        io_removehandler(dev->io_base, 0x100, aic_io_readb, aic_io_readw, aic_io_readl,
                         aic_io_writeb, aic_io_writew, aic_io_writel, dev);
        dev->io_live = 0;
    }
    if (on && !dev->io_live) {
        dev->io_base = want;
        io_sethandler(want, 0x100, aic_io_readb, aic_io_readw, aic_io_readl,
                      aic_io_writeb, aic_io_writew, aic_io_writel, dev);
        dev->io_live = 1;
    }
}

static void
aic_mem_update(aic7880_t *dev)
{
    uint32_t want = ((uint32_t) dev->pci_regs[0x17] << 24) | ((uint32_t) dev->pci_regs[0x16] << 16) | ((uint32_t) dev->pci_regs[0x15] << 8);

    mem_mapping_disable(&dev->mmio);
    dev->mem_base = want;
    if ((dev->pci_regs[0x04] & PCI_COMMAND_MEM) && (want != 0))
        mem_mapping_set_addr(&dev->mmio, want, 0x1000);
}

static void
aic_bios_update(aic7880_t *dev)
{
    uint32_t want;

    if (!dev->has_bios)
        return;
    want = ((uint32_t) dev->pci_regs[0x33] << 24) | ((uint32_t) dev->pci_regs[0x32] << 16) | ((uint32_t) dev->pci_regs[0x31] << 8);
    want &= ~(dev->rom_size - 1);

    mem_mapping_disable(&dev->bios.mapping);
    if ((dev->pci_regs[0x30] & 0x01) && (dev->pci_regs[0x04] & PCI_COMMAND_MEM) && (want != 0)) {
        aic_log("bios: mapped at %08x\n", want);
        mem_mapping_set_addr(&dev->bios.mapping, want, dev->rom_size);
    } else {
        aic_log("bios: unmapped (bar %08x en %i cmd %02x)\n", want,
                dev->pci_regs[0x30] & 1, dev->pci_regs[0x04]);
    }
}

static uint8_t
aic_rom_readb(uint32_t addr, void *priv)
{
    aic7880_t *dev = (aic7880_t *) priv;

    dev->rom_reads++;
    if (dev->rom_reads <= 48)
        aic_log("bios: rom read #%u at +%04x = %02x\n", dev->rom_reads,
                addr & 0xffff, dev->bios.rom[addr & 0xffff]);
    else if ((dev->rom_reads % 4096) == 0) {
        aic_log("bios: rom read #%u at +%04x\n", dev->rom_reads, addr & 0xffff);
    }
    return dev->bios.rom[addr & 0xffff];
}

static uint16_t
aic_rom_readw(uint32_t addr, void *priv)
{
    return aic_rom_readb(addr, priv) | (aic_rom_readb(addr + 1, priv) << 8);
}

static uint32_t
aic_rom_readl(uint32_t addr, void *priv)
{
    return aic_rom_readw(addr, priv) | ((uint32_t) aic_rom_readw(addr + 2, priv) << 16);
}

static uint8_t
aic_pci_read(int func, int addr, UNUSED(int len), void *priv)
{
    const aic7880_t *dev = (const aic7880_t *) priv;

    if (func > 0)
        return 0xff;
    if (AIC7880_LOG_REGS) {
        aic_log("  pci in  %02x -> %02x\n", addr & 0xff, dev->pci_regs[addr & 0xff]);
    }
    return dev->pci_regs[addr & 0xff];
}

static void
aic_pci_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    aic7880_t *dev = (aic7880_t *) priv;

    if (func > 0)
        return;

    switch (addr) {
        case 0x04:
            dev->pci_regs[0x04] = val & 0x57;
            aic_io_update(dev);
            aic_mem_update(dev);
            aic_bios_update(dev);
            aic_update_irq(dev); /* MASTEREN gates the interrupt pin */
            break;
        case 0x05:
            dev->pci_regs[0x05] = val & 0x01;
            break;
        case 0x07:
            dev->pci_regs[0x07] &= (uint8_t) ~(val & 0xf9);
            break;
        case 0x0c: /* cache line size, in words: bits 5 to 2 */
            dev->pci_regs[addr] = val & 0x3c;
            break;
        case 0x0d: /* latency timer: the low two bits are wired to zero */
            dev->pci_regs[addr] = val & 0xfc;
            break;
        case 0x3c:
            dev->pci_regs[addr] = val;
            break;

        case 0x11: /* BAR0: I/O, 256 bytes */
            dev->pci_regs[0x11] = val;
            aic_io_update(dev);
            break;
        case 0x10:
        case 0x12:
        case 0x13:
            break;

        case 0x15: /* BAR1: memory, 4 KiB window over the same registers */
            /* Four KiB means the low twelve bits are wired to zero. With
               all of this byte writable the BAR sized as 256 bytes, and a
               BIOS that packs small BARs gave it an address that is not on
               a page boundary, where the window cannot be mapped. */
            val &= 0xf0;
            fallthrough;
        case 0x16:
        case 0x17:
            dev->pci_regs[addr] = val;
            aic_mem_update(dev);
            break;
        case 0x14:
            break;

        case 0x30:
            if (dev->has_bios) {
                dev->pci_regs[0x30] = val & 0x01;
                aic_bios_update(dev);
            }
            break;
        case 0x31:
            break;
        case 0x32:
        case 0x33:
            if (dev->has_bios) {
                dev->pci_regs[addr] = val;
                aic_bios_update(dev);
            }
            break;

        case DEVCONFIG: /* 0x40 */
        case DEVCONFIG + 1:
        case DEVCONFIG + 2:
        case DEVCONFIG + 3:
            dev->pci_regs[addr] = val;
            break;

        default:
            break;
    }
}

/* ---- device plumbing ---------------------------------------------------- */

#define AHA2940UW_V123_ROM "roms/scsi/adaptec/aha2940uw_v123.bin"
#define AHA2940UW_V125_ROM "roms/scsi/adaptec/aha2940uw_v125.bin"
#define AHA2940UW_V134_ROM "roms/scsi/adaptec/aha2940uw_v134.bin"
#define AHA2940UW_V220_ROM "roms/scsi/adaptec/aha2940uw_v220.bin"

#define BOARD_7880         0 /* the chip on a motherboard */
#define BOARD_2940U        1 /* AHA-2940 Ultra, narrow */
#define BOARD_2940UW       2 /* AHA-2940 Ultra Wide */

static void
aic_reset(void *priv)
{
    aic7880_t *dev = (aic7880_t *) priv;

    aic_chip_reset(dev);
}

static void *
aic_init(const device_t *info)
{
    aic7880_t               *dev = (aic7880_t *) calloc(1, sizeof(aic7880_t));
    nmc93cxx_eeprom_params_t params;
    uint16_t                 nvr[64];
    char                     fn[1024] = { 0 };
    uint16_t                 devid;

    dev->board = info->local & 0xff;
    dev->wide  = (dev->board == BOARD_2940UW) || (dev->board == BOARD_7880);
    dev->bus   = scsi_get_bus();

    scsi_bus_set_speed(dev->bus, 20000000.0);

    /* The on-board part answers as the bare chip; the cards carry the
       adapter's own ID, which is how a driver tells them apart and how
       the option ROM's PCI data structure matches. */
    devid = (dev->board == BOARD_7880) ? 0x8078 : 0x8178;

    dev->pci_regs[0x00] = 0x04;
    dev->pci_regs[0x01] = 0x90; /* Adaptec, 0x9004 */
    dev->pci_regs[0x02] = devid & 0xff;
    dev->pci_regs[0x03] = (devid >> 8) & 0xff;
    dev->pci_regs[0x06] = 0x80; /* capabilities: none, but fast back-to-back */
    dev->pci_regs[0x07] = 0x02; /* medium DEVSEL */
    dev->pci_regs[0x08] = 0x03; /* revision: an aic7880 Rev B */
    dev->pci_regs[0x09] = 0x00;
    dev->pci_regs[0x0a] = 0x00; /* SCSI controller */
    dev->pci_regs[0x0b] = 0x01; /* mass storage */
    dev->pci_regs[0x0e] = 0x00;
    dev->pci_regs[0x10] = 0x01; /* BAR0 in I/O space */
    dev->pci_regs[0x14] = 0x00; /* BAR1 in memory space */
    /* The cards carry a subsystem ID of their own, and it is not the
       device ID over again: it is 78, for the chip family, over the board
       number, so an AHA-2940U/UW is 9004:7881. Adaptec's DOS ASPI manager
       reads this before anything else and walks past a card whose
       subsystem ID is set but does not begin with 78 (or 75). The bare
       chip on a motherboard leaves it at zero, which the same driver
       takes as "go by the device ID". */
    if (dev->board != BOARD_7880) {
        dev->pci_regs[0x2c] = 0x04;
        dev->pci_regs[0x2d] = 0x90;
        dev->pci_regs[0x2e] = (devid >> 8) & 0xff;
        dev->pci_regs[0x2f] = 0x78;
    }
    dev->pci_regs[0x3d] = PCI_INTA;
    dev->pci_regs[0x3e] = 0x08; /* min grant */
    dev->pci_regs[0x3f] = 0x08; /* max latency */

    /* DEVCONFIG: 64-bit disabled, the PCI error bits clear. */
    dev->pci_regs[DEVCONFIG]     = 0x00;
    dev->pci_regs[DEVCONFIG + 1] = 0x00;
    dev->pci_regs[DEVCONFIG + 2] = 0x00;
    dev->pci_regs[DEVCONFIG + 3] = 0x00;

    /* How long the device itself takes over a command, which is off unless
       asked for: the seek and the rotation are modelled but the transfer is
       not, so what it buys in fidelity it spends twice over in latency.
       The part on a motherboard has no settings and does without. */
    dev->timed_dev = (dev->board == BOARD_7880) ? 0 : device_get_config_int("dev_timing");

    /* The card's own BIOS. Every one of these images is an AHA-2940
       Ultra/Ultra W BIOS whose PCI data structure names 9004:8178, as this
       card does; the PCI BIOS refuses to run one whose ID does not match.
       The images are shorter than the 64 KiB window the BAR asks for, and
       what is not covered by the file reads back as 0xff. */
    if ((dev->board != BOARD_7880) && device_get_config_int("bios")) {
        const char *bios_rev  = device_get_config_bios("bios_rev");
        const char *bios_path = device_get_bios_file(info, bios_rev, 0);
        int         ok;

        dev->has_bios = 1;
        dev->rom_size = 0x10000;
        ok            = rom_init(&dev->bios, bios_path, 0xd0000, 0x10000, 0xffff, 0,
                                 MEM_MAPPING_EXTERNAL);
        if (ok < 0)
            dev->has_bios = 0;
        else {
            mem_mapping_set_handler(&dev->bios.mapping, aic_rom_readb, aic_rom_readw,
                                    aic_rom_readl, NULL, NULL, NULL);
            mem_mapping_set_p(&dev->bios.mapping, dev);
            mem_mapping_disable(&dev->bios.mapping);
        }
    }

    aic_seeprom_build(dev, nvr);
    snprintf(fn, sizeof(fn), "nmc93cxx_eeprom_%s_%d.nvr", info->internal_name,
             device_get_instance());
    params.type            = NMC_93C46_x16_64;
    params.default_content = nvr;
    params.filename        = fn;
    dev->eeprom            = (nmc93cxx_eeprom_t *) device_add_inst_params(&nmc93cxx_device,
                                                                          device_get_instance(),
                                                                          &params);
    if (dev->eeprom == NULL) {
        free(dev);
        return NULL;
    }

    mem_mapping_add(&dev->mmio, 0, 0, aic_mem_readb, aic_mem_readw, aic_mem_readl,
                    aic_mem_writeb, aic_mem_writew, aic_mem_writel, NULL,
                    MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_disable(&dev->mmio);

    timer_add(&dev->seq_timer, aic_seq_timer, dev, 0);
    timer_add(&dev->sel_timer, aic_select_done, dev, 0);
    timer_add(&dev->tgt_timer, aic_tgt_timer, dev, 0);

    aic_chip_reset(dev);

    pci_add_card(PCI_ADD_NORMAL, aic_pci_read, aic_pci_write, dev, &dev->pci_slot);

    aic_log("aic7880: board %i, %s, bus %i\n", dev->board,
            dev->wide ? "wide" : "narrow", dev->bus);

    return dev;
}

static void
aic_close(void *priv)
{
    aic7880_t *dev = (aic7880_t *) priv;

    if (dev == NULL)
        return;

    for (uint8_t i = 0; i < AIC_CMDS; i++) {
        if (dev->cmds[i].used && (dev->cmds[i].data != NULL))
            free(dev->cmds[i].data);
    }

    if (dev->io_live)
        io_removehandler(dev->io_base, 0x100, aic_io_readb, aic_io_readw, aic_io_readl,
                         aic_io_writeb, aic_io_writew, aic_io_writel, dev);

    free(dev);
}

static const device_config_t aic_card_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "Enable BIOS",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_rev",
        .description    = "BIOS Revision",
        .type           = CONFIG_BIOS,
        .default_string = "v2_20_0",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .bios           = {
            {
                .name          = "Version 1.23",
                .internal_name = "v1_23",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 18432,
                .files         = { AHA2940UW_V123_ROM, "" }
            },
            {
                .name          = "Version 1.25.0",
                .internal_name = "v1_25_0",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 65536,
                .files         = { AHA2940UW_V125_ROM, "" }
            },
            {
                .name          = "Version 1.34.3",
                .internal_name = "v1_34_3",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 65536,
                .files         = { AHA2940UW_V134_ROM, "" }
            },
            {
                .name          = "Version 2.20.0",
                .internal_name = "v2_20_0",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 65536,
                .files         = { AHA2940UW_V220_ROM, "" }
            },
            { .files_no = 0 }
        },
    },
    {
        .name           = "dev_timing",
        .description    = "Device timing",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

/* The bare chip, as found on a motherboard. It is not in the card list:
   a machine that has one adds it itself. */
const device_t aic7880_pci_device = {
    .name          = "Adaptec AIC-7880 Ultra SCSI (on-board)",
    .internal_name = "aic7880_onboard",
    .flags         = DEVICE_PCI | DEVICE_ONBOARD,
    .local         = BOARD_7880,
    .init          = aic_init,
    .close         = aic_close,
    .reset         = aic_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t aha2940u_pci_device = {
    .name          = "Adaptec AHA-2940 Ultra",
    .internal_name = "aha2940u",
    .flags         = DEVICE_PCI,
    .local         = BOARD_2940U,
    .init          = aic_init,
    .close         = aic_close,
    .reset         = aic_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = aic_card_config
};

const device_t aha2940uw_pci_device = {
    .name          = "Adaptec AHA-2940 Ultra Wide",
    .internal_name = "aha2940uw",
    .flags         = DEVICE_PCI,
    .local         = BOARD_2940UW,
    .init          = aic_init,
    .close         = aic_close,
    .reset         = aic_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = aic_card_config
};
