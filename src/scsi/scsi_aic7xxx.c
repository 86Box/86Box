/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Adaptec's AIC-7xxx SCSI controllers.
 *
 *          One model covers the family because it is one design: the
 *          AIC-7870 and AIC-7880 are the AIC-7770 grown up, with the same
 *          sequencer and instruction set, the same register addresses, the
 *          same SCB and queue arrangement and the same command flow. What
 *          differs between them is the edge of the register file and a
 *          couple of counts, and that is the table of aic_chip_t below
 *          rather than a thread of conditionals through the whole file.
 *
 *          The boards: the AIC-7770 on the EISA AHA-2740, the AIC-7870 on
 *          the AHA-2940 and 2940W, and the AIC-7880 as the chip on a
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
#include <86box/ini.h>
#include <86box/config.h>
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
#include <86box/pic.h>
#include <86box/eisa.h>
#include <86box/fdc.h>
#include <86box/scsi_aic7xxx.h>
#include <86box/plat_unused.h>

/* Every command, phase change and interrupt is one line. The sequencer and
   the register file are far louder than that, so they are separate: set
   AIC7880_LOG_SEQ to log every instruction the sequencer executes, and
   AIC7880_LOG_REGS to log every access to the register file. */
#define AIC7880_LOG_SEQ  0
#define AIC7880_LOG_REGS 0

#ifdef ENABLE_AIC7XXX_LOG
int aic7xxx_do_log = ENABLE_AIC7XXX_LOG;

/* Every line says which board produced it. A machine can hold the
   on-board chip, an EISA card and a PCI card at once, and all three run
   the same code through the same log: without the tag a trace of one is
   indistinguishable from a trace of another, which cost a diagnosis. */
static void
aic_log(const char *tag, const char *fmt, ...)
{
    va_list ap;
    char    buf[1024];

    if (aic7xxx_do_log) {
        snprintf(buf, sizeof(buf), "%s%s", tag, fmt);
        va_start(ap, fmt);
        pclog_ex(buf, ap);
        va_end(ap);
    }
}
#else
#    define aic_log(tag, fmt, ...)
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

#define BOARD_7880   0 /* the chip on a motherboard */
#define BOARD_2940U  1 /* AHA-2940 Ultra, narrow */
#define BOARD_2940UW 2 /* AHA-2940 Ultra Wide */
#define BOARD_2940   3 /* AHA-2940, narrow, on an AIC-7870 */
#define BOARD_2940W  4 /* AHA-2940W, the same card strapped wide */
#define BOARD_2944UW 5 /* AHA-2944 Ultra Wide, differential */
/* The AIC-7770 cards. The configuration utility's overlay knows four
   hardware types by the chip's straps -- AHA-2740/2742, 2740T/2742T,
   2740W/2742W and 2744W -- and the second digit says whether the board
   carries a floppy controller (an N82077 on Hino's). Same EISA ID
   (ADP7771) and the same option ROM throughout. */
#define BOARD_2740        6  /* one narrow channel */
#define BOARD_2742        7  /* one narrow channel, floppy controller */
#define BOARD_2740T       8  /* two narrow channels */
#define BOARD_2742T       9  /* two narrow channels, floppy controller */
#define BOARD_2740W       10 /* one wide channel */
#define BOARD_2742W       11 /* one wide channel, floppy controller */
#define BOARD_2744W       12 /* one wide differential channel */
#define AIC_BOARD_EISA(b) ((b) >= BOARD_2740)
/* A card entry that covers several models takes the board from its Model
   option (and, for the 274x, its floppy jumper) instead of from .local. */
#define BOARD_FROM_CONFIG 0xff
#define AIC_BOARD_TWIN(b) (((b) == BOARD_2740T) || ((b) == BOARD_2742T))
#define AIC_BOARD_WIDE(b) (((b) == BOARD_2740W) || ((b) == BOARD_2742W) || ((b) == BOARD_2744W))
#define AIC_BOARD_DIFF(b) (((b) == BOARD_2744W) || ((b) == BOARD_2944UW))
#define AIC_BOARD_FDC(b)  (((b) == BOARD_2742) || ((b) == BOARD_2742T) || ((b) == BOARD_2742W))

#define SRAM_BASE         0x20 /* scratch RAM, to 0x5f */

/* On an EISA part the top of scratch is where the configuration chip
   appears, and the driver reads its settings from there. These are not
   scratch to us: a chip reset must leave them as the configuration left
   them, which is what the real card does. */
#define HA_274_BIOSGLOBAL 0x56 /* bit 0: extended translation */
#define SCSICONF          0x5a /* terminators, parity, and our own ID */
#define SCSICONF_B        0x5b
/* Its bits, as the driver reads them back: bit 7 says the terminators are
   on, which it turns into STPWEN in SXFRCTL1; bit 6 asks for a bus reset
   at start; bit 5 is parity checking, which it passes into SXFRCTL1 as
   the same bit; and the bottom three are the adapter's own SCSI ID. */
#define TERM_ENB   0x80
#define RESET_SCSI 0x40
#define HSCSIID    0x07
#define INTDEF     0x5c /* bit 7 edge triggered, bits 3:0 the IRQ */
/* HOSTCONF: bits 7:6 the data FIFO threshold, 11 for 100% down to 00 for
   none, and bits 5:2 the bus release time in BCLKs -- 1111 for sixty,
   0000 for two -- which is how long the card keeps transferring after it
   is preempted. Board configuration rather than chip register: the driver
   reads it and copies the threshold across itself, "hostconf =
   aic_inb(temp_p, HOSTCONF); aic_outb(temp_p, hostconf & DFTHRSH,
   BUSSPD)", and BUSSPD is where this model reads it from. */
#define HOSTCONF        0x5d
#define HA_274_BIOSCTRL 0x5f /* bits 5:4: 3 means the BIOS is disabled */

/* The top of the option ROM window is not ROM. The board answers the last
   hundred and twenty eight bytes of it out of a small static RAM, which
   bit 6 of HA_274_BIOSCTRL switches in and bit 7 write protects, and the
   card's own BIOS writes patterns there and reads them back before it will
   install itself -- "Host Adapter shadow RAM test failure!" is what comes
   out otherwise. It keeps its own variables there afterwards, and fixes up
   the image checksum at the very last byte so the firmware's scan still
   accepts the window. */
#define AIC_BIOS_RAMEN  0x40
#define AIC_BIOS_WRTPRT 0x80
/* The overlay is the last 128 bytes of the window, so where it starts
   depends on how big the window is -- 3F80h in a sixteen kilobyte one,
   7F80h in a thirty-two. Fixed at the sixteen kilobyte figure it landed
   in the middle of a larger image, and the firmware's write-read test of
   its own overlay failed with "Host Adapter shadow RAM test failure!". */
#define AIC_BIOS_RAMSIZE 0x0080

#define SEQCTL           0x60
#define PERRORDIS        0x80
#define PAUSEDIS         0x40
#define FAILDIS          0x20
#define FASTMODE         0x10
#define BRKADRINTEN      0x08
#define STEP             0x04
#define SEQRESET         0x02
#define LOADRAM          0x01
#define SEQRAM           0x61
#define SEQADDR0         0x62
#define SEQADDR1         0x63
#define ACCUM            0x64
#define SINDEX           0x65
#define DINDEX           0x66
#define BRKADDR0         0x67
#define BRKADDR1         0x68
#define BRKDIS           0x80
#define ALLONES          0x69
#define ALLZEROS         0x6a /* NONE on write */
#define FLAGS            0x6b
#define ZERO             0x02
#define CARRY            0x01
#define SINDIR           0x6c
#define DINDIR           0x6d
#define FUNCTION1        0x6e
#define STACK            0x6f

#define DSVENDID         0x80 /* the PCI IDs again, where the sequencer can see them */
#define DSDEVID          0x82
#define DSCOMMAND0       0x84
#define DSCOMMAND1       0x85
#define DSPCISTATUS      0x86
/* The same three addresses on an AIC-7770, which has a bus of its own to
   look after rather than a PCI one. */
#define BCTL         0x84 /* bit 0 enables the board's bus drivers */
#define BUSTIME      0x85
#define BUSSPD       0x86
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
#define PARERR       0x08
#define ILLOPCODE    0x04
#define ILLSADDR     0x02
#define ILLHADDR     0x01
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

/* A polling loop's pass (aic_loop_edge): the most distinct places it may
   read or write, and the longest it may be. */
#define AIC_LOOP_NONE  0xffff
#define AIC_LOOP_MAX   24
#define AIC_LOOP_INSNS 1024

/* What one of these parts is, as against what a board makes of it. The
   AIC-7870 and AIC-7880 are the AIC-7770 grown up: same sequencer and the
   same instruction set, the same register addresses, the same SCB and
   queue arrangement, the same command flow. Where they differ is at the
   edges of the register file -- which registers are there at all, which
   bits inside them are, and how many SCB pages and queue entries the part
   carries -- and that is a table rather than a thread of conditionals
   through four thousand lines. Everything here can be checked against the
   data book a line at a time.

   A board's own wiring is not in here: whether it has a second SCSI
   connector or a wide one is strapped on the chip's pins, and which bus it
   plugs into is the board's business too. */
typedef struct aic_chip_t {
    const char *name;
    uint8_t     scb_pages;    /* how many pages SCBPTR can select */
    uint8_t     q_depth;      /* how deep QINFIFO and QOUTFIFO are */
    uint8_t     scbptr_mask;  /* and what SCBPTR reads back */
    uint8_t     sblkctl_mask; /* the bits of these that are not always 0 */
    uint8_t     sxfrctl0_mask;
    uint8_t     sxfrctl1_mask;
    uint8_t     simode0_mask;
    uint8_t     scsitest_mask;
    uint8_t     clrint_mask;
    uint8_t     aux_regs;       /* 1Ah to 1Eh: SCAM, PIO capability, the
                                   board GAL and the serial EEPROM */
    uint8_t fifo_addr_hi;       /* a second byte of data FIFO address */
    uint8_t fifo_word;          /* bytes in one FIFO location: DWORDEMP
                                   on the 7770 is the two dword pointers
                                   equal, FIFOQWDEMP on the later parts
                                   the two quadword pointers */
    uint8_t selid_writable;     /* SELID is read only on the older part */
    uint8_t twin_capable;       /* a second SCSI channel to strap */
    uint8_t seqctl_reset;       /* what SEQCTL comes up holding */
    uint8_t sblkctl_reset;      /* and SBLKCTL, before the board's straps */
    uint8_t own_reset_seen;     /* the part reports the bus reset it drives */
    uint8_t bad_addr_err;       /* what an address that decodes to nothing
                                   records in ERROR */
    uint8_t host_pause_checked; /* the host reaching a register that
                                   wants the sequencer stopped is
                                   ILLHADDR */
} aic_chip_t;

/* The AIC-7770. Four SCB pages and two four deep queues; SCBPTR keeps
   three bits of which two select; SBLKCTL is SELBUSB and SELWIDE alone;
   SXFRCTL0 is CLRSTCNT, SPIOEN and CLRCHN; SXFRCTL1 stops at ENSTIMER;
   SIMODE0 has no bit 7; SCSITEST is three bits; CLRINT has no clear for
   the SCSI interrupt and none for a parity error; 1Ah to 1Eh are not
   registers at all. */
static const aic_chip_t aic_chip_7770 = {
    .name           = "AIC-7770",
    .scb_pages      = 4,
    .q_depth        = 4,
    .scbptr_mask    = 0x07,
    .sblkctl_mask   = SELBUSB | SELWIDE,
    .sxfrctl0_mask  = CLRSTCNT | SPIOEN | CLRCHN,
    .sxfrctl1_mask  = BITBUCKET | SWRAPEN | ENSPCHK | STIMESEL | ENSTIMER,
    .simode0_mask   = 0x7f,
    .scsitest_mask  = 0x07,
    .clrint_mask    = CLRBRKADRINT | CLRCMDINT | CLRSEQINT,
    .aux_regs       = 0,
    .fifo_addr_hi   = 0,
    .fifo_word      = 4,
    .selid_writable = 0,
    .twin_capable   = 1,
    /* PERRORDIS is the one bit here whose reset value the data book gives
       as a one; FASTMODE is not, so the sequencer starts on the slower of
       its two clocks until the firmware asks for the other. */
    .seqctl_reset = PERRORDIS,
    /* Out of reset this register is the board's own wiring and nothing
       else: 08h for two buses, 02h for one wide one, 00h for a single
       narrow one. The straps are ORed in below. */
    .sblkctl_reset = 0,
    /* SCSIRSTI is reset *in*. Reporting the one this part drives itself
       deadlocks it: the status raises SCSIINT, an unserviced interrupt
       stops the sequencer, and the sequencer is what times the reset and
       finishes it. The card's own BIOS settles it -- it enables
       ENSCSIRST, asserts SCSIRSTO, starts the sequencer and waits. */
    .own_reset_seen = 0,
    /* An address that decodes to no register is ILLSADDR, not a bad
       opcode. */
    .host_pause_checked = 1,
    .bad_addr_err       = ILLSADDR,
};

/* The AIC-7880, which keeps everything the older part had and adds to
   it. */
static const aic_chip_t aic_chip_788x = {
    .name           = "AIC-7880",
    .scb_pages      = SCB_COUNT,
    .q_depth        = QUEUE_SIZE,
    .scbptr_mask    = 0xff,
    .sblkctl_mask   = DIAGLEDEN | DIAGLEDON | AUTOFLUSHDIS | SELWIDE,
    .sxfrctl0_mask  = 0xff,
    .sxfrctl1_mask  = 0xff,
    .simode0_mask   = 0xff,
    .scsitest_mask  = 0xff,
    .clrint_mask    = CLRPARERR | CLRBRKADRINT | CLRSCSIINT | CLRCMDINT | CLRSEQINT,
    .aux_regs       = 1,
    .fifo_addr_hi   = 1,
    .fifo_word      = 8,
    .selid_writable = 1,
    .twin_capable   = 0,
    .seqctl_reset   = PERRORDIS | FASTMODE,
    /* The diagnostic LED bits come up set. AUTOFLUSHDIS does not, and
       must not: with it set the part does not flush its data FIFO by
       itself, and everything waiting on that data stops. */
    .sblkctl_reset    = DIAGLEDEN | DIAGLEDON,
    .own_reset_seen   = 1,
    .bad_addr_err     = ILLOPCODE,
    /* Off, for the same reason bad_addr_err differs: ILLHADDR's rule is
       the AIC-7770 book's, and this part's is not to hand. */
    .host_pause_checked = 0,
};

/* The AIC-7870: the AIC-7880 without Ultra. Its data book has bit 5 of
   SXFRCTL0 "Not Used. Always reads 0." where the later part keeps FAST20,
   so a transfer can never be clocked at the Ultra rate. Every driver
   takes the two for one family and goes by the device ID for the rest:
   Linux's feature table makes the AIC-7880 the AIC-7870 plus AHC_ULTRA
   and nothing else. */
static const aic_chip_t aic_chip_7870 = {
    .name          = "AIC-7870",
    .scb_pages     = SCB_COUNT,
    .q_depth       = QUEUE_SIZE,
    .scbptr_mask   = 0xff,
    .sblkctl_mask  = DIAGLEDEN | DIAGLEDON | AUTOFLUSHDIS | SELWIDE,
    .sxfrctl0_mask = (uint8_t) ~FAST20,
    .sxfrctl1_mask = 0xff,
    .simode0_mask  = 0xff,
    .scsitest_mask = 0xff,
    .clrint_mask   = CLRPARERR | CLRBRKADRINT | CLRSCSIINT | CLRCMDINT | CLRSEQINT,
    .aux_regs      = 1,
    .fifo_addr_hi  = 1,
    .fifo_word     = 8,
    .selid_writable = 1,
    .twin_capable  = 0,
    .seqctl_reset  = PERRORDIS | FASTMODE,
    .sblkctl_reset = DIAGLEDEN | DIAGLEDON,
    .own_reset_seen = 1,
    .bad_addr_err  = ILLOPCODE,
    .host_pause_checked = 0,
};

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
    uint8_t  bus;      /* the channel it was selected on */
} aic_cmd_t;

#define AIC_CMDS 64

/* One SCSI cell. The AIC-7770 has two of them and SBLKCTL's SELBUSB says
   which one the register file is looking at: "When this bit is set, SCSI
   Channel B is selected. Device addresses 00h-1Eh reflect the Channel B
   registers. When this bit is cleared, addresses 00h-1Eh reflect Channel A
   registers", both channels "mapped to the same I/O space".

   The selected cell's registers are the live fields in aic7xxx_t, so
   everything that works on "the channel we are switched to" needs no
   changing; this holds the other one, and the two are swapped when
   SELBUSB moves. What is deliberately NOT here is the data path -- the
   FIFO, DFCNTRL, STCNT, HADDR and SHADDR -- and the sequencer and SCB
   array. There is one of each of those, they are addressed outside
   00h-1Eh, and only one channel can be transferring at a time. */
typedef struct aic_cell_t {
    uint8_t scsiseq;
    uint8_t sxfrctl0;
    uint8_t sxfrctl1;
    uint8_t scsisigo;
    uint8_t scsirate;
    uint8_t scsiid;
    uint8_t sstat0;
    uint8_t sstat1;
    uint8_t simode0;
    uint8_t simode1;
    uint8_t selid;
    uint8_t scsitest;
} aic_cell_t;

typedef struct aic7xxx_t {
    /* PCI side */
    uint8_t           pci_slot;
    uint8_t           irq_state; /* belongs to the PCI layer */
    uint8_t           irq;       /* the EISA board's own interrupt */
    uint8_t           irq_level; /* what we last drove */
    uint8_t           pci_regs[256];
    uint16_t          io_base;
    uint8_t           io_live;
    uint32_t          mem_base;
    mem_mapping_t     mmio;
    rom_t             bios;
    uint8_t           has_bios;
    uint32_t          rom_reads;
    uint32_t          rom_writes;
    uint32_t          busl_reads;
    uint32_t          sig_logs;  /* host SCSISIGI reads and SBLKCTL writes traced */
    uint32_t          err_logs;  /* hard errors traced */
    uint32_t          scb_dumps; /* SCBs dumped at queue time */
    uint32_t          rom_size;
    uint8_t           bus;
    uint8_t           wide;
    uint8_t           twin;  /* the board is wired for two SCSI buses */
    uint8_t           diff;  /* differential transceivers on channel A */
    void             *fdc;   /* the floppy controller a 2742 carries, when jumpered on */
    uint8_t           bus_b; /* and the second one it is wired to */
    uint8_t           eisa_id[4];
    const aic_chip_t *chip;
    uint8_t           board; /* info->local */

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

    /* The cell the register file is not looking at, and which channel each
       piece of bus business belongs to. A selection and a connection each
       happen on one channel, and the firmware is free to have switched the
       file away from it in the meantime -- its poll loop flips SELBUSB on
       every pass -- so neither can be left to mean "whichever channel is
       selected now". */
    aic_cell_t cell_save[2];
    uint8_t    cell_live; /* which channel the live registers are */
    uint8_t    sel_ch;    /* the channel a selection in progress is on */
    uint8_t    cur_ch;    /* and the channel the connection is on */
    uint8_t    sleepctl;
    uint8_t    must_step;  /* PAUSE was just released: one instruction runs whatever else is pending */
    uint8_t    timed_dev;  /* a device takes as long over a command as 86Box says it does */
    uint8_t    seq_idle;   /* the sequencer was stopped or parked when last looked at */
    double     seq_last;   /* when its clock was last advanced, in microseconds */
    double     seq_credit; /* instructions it is owed */

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
#ifdef ENABLE_AIC7XXX_LOG
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

    /* A polling loop being proved to change nothing, or parked on
       (aic_loop_edge): where it starts, the state it started in, and what
       one pass read and wrote. */
    uint16_t loop_head;   /* AIC_LOOP_NONE: no pass being watched */
    uint16_t loop_failed; /* the last head that did not hold: another is tried first */
    uint8_t  loop_skips;  /* how many edges back to it have been passed over since */
    uint8_t  loop_parked;
    uint16_t loop_insns;
    uint8_t  loop_accum, loop_sindex, loop_dindex, loop_flags, loop_function1, loop_scbptr, loop_sp, loop_intstat;
    uint16_t loop_stack[4];
    uint8_t  loop_nrd, loop_nwr;
    struct {
        uint8_t addr;
        uint8_t scbptr;
        uint8_t val;
    } loop_rd[AIC_LOOP_MAX], loop_wr[AIC_LOOP_MAX];

    /* host side */
    uint8_t  dscommand0;
    uint8_t  dscommand1;
    uint8_t  dspcistatus;
    uint8_t  hcntrl;
    uint8_t  int_paused; /* PAUSE in HCNTRL was set by an interrupt, which PAUSEDIS cannot refuse */
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

    /* The EISA part. Its settings come from the configuration chip rather
       than a serial EEPROM, and a chip reset does not disturb them. */
    uint8_t eisa;
    uint8_t eisa_slot;
    uint8_t eisa_conf[6];                   /* 5a, 5b, 5c, 5d, 5e, 5f */
    uint8_t eisa_global;                    /* 56 */
    uint8_t bios_ctl_set;                   /* HA_274_BIOSCTRL has been written */
    uint8_t bios_ram[AIC_BIOS_RAMSIZE];     /* the overlay */
    uint8_t bios_rom_top[AIC_BIOS_RAMSIZE]; /* what it covers */
    uint8_t bctl;
    uint8_t bustime;
    uint8_t busspd;

    uint8_t  fifo[FIFO_SIZE];
    uint16_t fifo_rd;
    uint16_t fifo_cnt;
    uint8_t  fifo_flush; /* a flush, asked for or automatic, has not finished */
    uint8_t  host_wait;  /* sequencer instructions until the host side may move */

    uint8_t  qin[QUEUE_SIZE];
    uint8_t  qin_rd;
    uint16_t qin_cnt;
    uint8_t  qin_last; /* what QINFIFO last gave: an empty read gives it again */
    uint8_t  qout[QUEUE_SIZE];
    uint8_t  qout_rd;
    uint16_t qout_cnt;
    uint8_t  qout_last; /* what QOUTFIFO last gave, likewise */

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

    char     tag[16];     /* which board this one is, for the log */
    uint32_t hcntrl_logs; /* how many HCNTRL writes have been traced */

    pc_timer_t seq_timer;
    pc_timer_t sel_timer;
    pc_timer_t tgt_timer;
    pc_timer_t req_timer; /* the target's turnaround between message bytes */
    uint8_t    tgt_held;  /* what the target left on the data lines after the last ACK */
} aic7xxx_t;

/* Channel A and channel B are two separate SCSI buses sharing one register
   file; SELBUSB says which of them the file -- and so the SCSI cell -- is
   switched to. A board wired for one channel has nothing behind the other,
   and selecting over there finds no-one. */
static uint8_t
aic_cur_bus(const aic7xxx_t *dev)
{
    return (dev->sblkctl & SELBUSB) ? dev->bus_b : dev->bus;
}

/* Which channel a bus is, and which bus a channel is. */
static uint8_t
aic_ch_of_bus(const aic7xxx_t *dev, uint8_t bus)
{
    return ((dev->bus_b != dev->bus) && (bus == dev->bus_b)) ? 1 : 0;
}

static uint8_t
aic_bus_of_ch(const aic7xxx_t *dev, uint8_t ch)
{
    return ch ? dev->bus_b : dev->bus;
}

static void    aic_update_irq(aic7xxx_t *dev);
static void    aic_scsi_int(aic7xxx_t *dev);
static uint8_t aic_tgt_byte(const aic7xxx_t *dev);
static void    aic_seq_kick(aic7xxx_t *dev);
static void    aic_loop_wake(aic7xxx_t *dev);
static void    aic_seq_run(aic7xxx_t *dev);
static void    aic_pump(aic7xxx_t *dev);
static void    aic_bus_free(aic7xxx_t *dev);
static void    aic_tgt_next(aic7xxx_t *dev);
static void    aic_tgt_schedule(aic7xxx_t *dev);
static void    aic_chip_reset(aic7xxx_t *dev);
static void    aic_eisa_bios_remap(aic7xxx_t *dev);
static void    aic_eisa_bios_overlay(aic7xxx_t *dev);
static void    aic_pio_out(aic7xxx_t *dev);
#ifdef ENABLE_AIC7XXX_LOG
static const char *aic_phase_name(uint8_t phase);
#endif
static uint8_t aic_read(aic7xxx_t *dev, uint8_t addr, int seq);

/* 3F80h, and measured rather than reasoned about. It looks like it should
   follow the size of the image -- the last 128 bytes of the window -- and
   an earlier pass here made it do that. The v2.11 ROM is thirty-two
   kilobytes and still writes its test pattern at +3F80, so the overlay is
   the top of the first sixteen kilobytes whatever the image is, and
   following the image put the window 16K away from where the firmware
   reached for it: "Host Adapter shadow RAM test failure!" and no install.
   The trace of those writes is below, which is how this was settled. */
static uint32_t
aic_bios_ramoff(UNUSED(const aic7xxx_t *dev))
{
    return 0x3f80;
}
static void aic_write(aic7xxx_t *dev, uint8_t addr, uint8_t val, int seq);

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
aic_paused(const aic7xxx_t *dev)
{
    /* PAUSE in HCNTRL is the one thing that stops the sequencer, whether
       the host set it or an interrupt did -- see aic_int_pause. The
       difference between the two is PAUSEDIS: "If set, disables the pause
       function when PAUSE (bit 2, HCNTRL) is set. Pause due to interrupts
       or error conditions is still enabled." So a pause the host merely
       asked for can be refused while the sequencer is inside a critical
       section, and one an interrupt imposed cannot. */
    if (!(dev->hcntrl & PAUSE))
        return 0;
    if (dev->int_paused)
        return 1;
    return !(dev->seqctl & PAUSEDIS);
}

static void
aic_update_irq(aic7xxx_t *dev)
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
    /* Bus mastering in the PCI command register is one of the gates on a
       PCI part. The EISA board has the same gate in BCTL, which is what
       the driver sets last of all once it is ready to be interrupted. */
    if (dev->eisa)
        fire = (dev->hcntrl & INTEN) && !(dev->hcntrl & POWRDN) && (dev->bctl & 0x01) && (pend || (dev->hcntrl & SWINT));
    else
        fire = (dev->hcntrl & INTEN) && !(dev->hcntrl & POWRDN) && (dev->pci_regs[0x04] & 0x04) && (pend || (dev->hcntrl & SWINT));

    if (fire == dev->irq_level)
        return;
    dev->irq_level = fire;
    aic_log(dev->tag, "irq %s intstat %02x\n", fire ? "high" : "low", dev->intstat);

    if (dev->eisa) {
        /* How the pin is driven is IRQMS's to say, and the book leaves no
           room: "IRQ Mode Select. When set, a low true level interrupt on
           the IRQ pin is selected. When cleared, a high true edge
           interrupt on the IRQ pin is selected."

           This used to read bit 7 of INTDEF instead, on the claim that
           the configuration utility offered a LEVEL and an EDGE choice
           for each interrupt and wrote that bit to pick between them. It
           does not. !ADP7771.CFG, on the card's own configuration disk,
           has six interrupt choices -- 9, 10, 11, 12, 14 and 15 -- every
           one of them TRIGGER = LEVEL, every one writing INTDEF bit 7 as
           zero, and every one writing HCNTRL bits 3 and 0: IRQMS set,
           and a chip reset to go with it.

           The drivers read it back from there too. Linux keeps it in the
           value it unpauses with -- "the IRQMS bit is only valid on VL
           and EISA chips" -- sets it for an EISA card, and reports the
           mode straight out of it. So the old reading landed on level for
           the right outcome and the wrong reason, and would have been
           wrong the moment anything cleared IRQMS. */
        if (!(dev->hcntrl & IRQMS)) {
            if (fire)
                picint(1 << dev->irq);
            else
                picintc(1 << dev->irq);
        } else if (fire)
            picintlevel(1 << dev->irq, &dev->irq_state);
        else
            picintclevel(1 << dev->irq, &dev->irq_state);
        return;
    }

    if (fire)
        pci_set_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
    else
        pci_clear_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
}

/* Three of the four interrupts stop the sequencer, and the way they stop
   it is by setting the host's own PAUSE bit: HCNTRL bit 2 "is also set by
   certain hardware conditions listed below", and that list is BRKADRINT,
   SCSIINT and SEQINT. Which matters for how long the stop lasts --
   "Clearing this bit will release the Sequencer" is the only thing that
   does, so the sequencer stays held after the interrupt itself has been
   cleared, until the driver writes HCNTRL to let it go.

   Taking the stop from INTSTAT alone let it start again the moment CLRINT
   was written, and that is the middle of what every driver and option ROM
   does: service the interrupt, clear it, read and write the registers it
   came for, and only then unpause. Those accesses landed on a running
   sequencer and were answered with ILLHADDR -- ninety of them in one POST
   of the AHA-2740's own BIOS.

   Command complete is the one that does not pause, and the book's
   interrupt table says so in as many words. Both the sequencer's own
   write to INTSTAT and the model raising one on its behalf come through
   here. */
/* An interrupt stops the sequencer by setting the host's own PAUSE bit.
   HCNTRL bit 2 "is also set by certain hardware conditions listed below",
   and the list is BRKADRINT, SCSIINT and SEQINT; the interrupt summary
   table has Pause = Yes for every one of them and No for command
   complete alone. Which matters for how long the stop lasts: "Clearing
   this bit will release the Sequencer" is the only thing that does, so
   the sequencer stays held after the interrupt itself has been cleared,
   until the driver writes HCNTRL to let it go -- "The Sequencer may be
   restarted by clearing the SEQINT bit and writing a zero to the PAUSE
   bit in HCNTRL."

   Taking the stop from INTSTAT alone let it start again the moment CLRINT
   was written, and that is the middle of what every driver does: service
   the interrupt, clear it, read and write the registers it came for, and
   only then unpause. Those accesses landed on a running sequencer and
   were answered with ILLHADDR, which is BRKADRINT, which the driver
   cleared and went on reading -- Windows 98's driver and this model spun
   on that pair a million times a second and the log ate the emulator.

   It is the event that pauses, not the standing bit. A sequencer
   interrupt is the instruction that raises it, and it pauses every time
   even when the host has not cleared the last one; a SCSI interrupt is a
   condition, and pauses when it appears. A host that releases PAUSE with
   the interrupt still standing gets its one instruction and a running
   sequencer, as the book says: "When PAUSE is cleared, the Sequencer will
   always execute at least one instruction, even if some other event is
   active to pause the Sequencer." An earlier attempt held the sequencer
   for as long as INTSTAT had the bit, and an option ROM that never
   cleared one stopped for good. */
static void
aic_int_pause(aic7xxx_t *dev, uint8_t bits, uint8_t before)
{
    uint8_t pause = bits & (SEQINT | BRKADRINT);

    if ((bits & SCSIINT) && !(before & SCSIINT))
        pause |= SCSIINT;
    if (pause) {
        dev->hcntrl |= PAUSE;
        dev->int_paused = 1;
    }
}

static void
aic_raise(aic7xxx_t *dev, uint8_t bits)
{
    uint8_t before = dev->intstat;

    dev->intstat |= bits;
    aic_int_pause(dev, bits, before);
    aic_update_irq(dev);
}

/* The faults the book gathers behind BRKADRINT: "This register reports
   errors that are catastrophic in nature. These errors will cause
   BRKADRINT to be set and the sequencer to be paused."

   ERROR records what was detected whatever FAILDIS says; FAILDIS turns off
   only the interrupt. The AIC-7770 book: "If set, disables the Illegal
   Opcode or Address interrupt feature", and its interrupt summary makes
   FAILDIS=0 the enable condition of every one of those rows. The AIC-7870
   book says the same of its own list: BRKADRINT is set "When ILLOPCODE
   becomes active (FAILDIS=0)", and "This feature may be disabled by
   setting FAILDIS". So the later parts honour it too.

   PAUSEDIS goes with the interrupt, not with the detection: "SCSI
   interrupts, an Illegal Opcode interrupt, a Sequencer RAM Parity Error
   interrupt, and an Illegal Address interrupt, reset this bit" (the 7870:
   "an illegal opcode interrupt ... resets this bit"). Taking it away for a
   fault FAILDIS had silenced let a host PAUSE land inside the sequencer's
   critical section. */
static void
aic_fail(aic7xxx_t *dev, uint8_t bits)
{
    dev->error |= bits;
    if (dev->seqctl & FAILDIS)
        return;
    dev->seqctl &= ~PAUSEDIS;
    aic_raise(dev, BRKADRINT);
}

/* Which registers the host may reach while the sequencer is running.
   The register summary states the rule once for the whole map -- "When
   the host must access these registers the Sequencer must be paused,
   except when noted otherwise" -- and then notes the exceptions one by
   one in the Comments column. These are all of them:

     BID0..BID3   "Host only, no pause"
     BCTL         "Host only, no pause"
     HCNTRL       "Host only, no pause"
     INTSTAT      "Host only, no pause"
     CLRINT       "Host only, no pause"
     QOUTFIFO     "Read by Host only, no pause"
     QOUTCNT      "Read by Host only, no pause"

   Two of those are one-way. The outbound queue is a no-pause access for
   reads alone, and ERROR shares CLRINT's address -- read is ERROR, write
   is CLRINT -- but only the write half carries the note: ERROR's own
   Comments column says "Host only" and stops there, so reading it wants
   the sequencer stopped like everything else. A hard error pauses the
   sequencer on its way to BRKADRINT, so the read that follows one is
   legal; a read out of the blue is not. */
static int
aic_host_no_pause(uint8_t addr, int write)
{
    switch (addr) {
        case DSVENDID:     /* BID0 */
        case DSVENDID + 1: /* BID1 */
        case DSDEVID:      /* BID2 */
        case DSDEVID + 1:  /* BID3 */
        case BCTL:
        case HCNTRL:
        case INTSTAT:
            return 1;
        case ERROR: /* read ERROR wants a pause, write CLRINT does not */
            return write;
        case QOUTFIFO:
        case QOUTCNT:
            return !write;
        default:
            return 0;
    }
}

static void
aic_hard_error(aic7xxx_t *dev, uint8_t bits, uint8_t addr, int write)
{
    /* Named in the log line only. */
    (void) addr;
    (void) write;

    /* Worth a line of its own, and worth naming what reached for what.
       Firmware that runs on the real part does not reach these, so one
       appearing here is this model's news rather than the firmware's --
       and the only way to tell which of this model's rules is wrong is
       to say which register tripped it. */
    /* Bounded, like HCNTRL: a driver and this model disagreeing about
       whether the sequencer is stopped produce one of these per access,
       and a driver's interrupt handler makes a million accesses a second.
       An unbounded trace of that wrote 1.6 GB in five minutes and slowed
       the emulator to a quarter speed. */
    if ((dev->err_logs < 256) || ((dev->err_logs % 100000) == 0)) {
        aic_log(dev->tag, "hard error %02x (error %02x) at pc %03x, dfcntrl %02x, "
                          "%s %02x, seqctl %02x hcntrl %02x%s\n",
                bits, dev->error | bits,
                dev->pc, dev->dfcntrl, write ? "host wrote" : "host read", addr,
                dev->seqctl, dev->hcntrl,
                (dev->err_logs >= 256) ? " [1 in 100000]" : "");
    }
    dev->err_logs++;
    aic_fail(dev, bits);
}

/* SCSIINT is the one interrupt the sequencer does not raise itself: the
   SCSI cell raises it, and only for the conditions SIMODE lets through. */
static void
aic_scsi_int(aic7xxx_t *dev)
{
    /* Against SSTAT0 as the sequencer would read it: SDONE and DMADONE are
       computed on the way out, and ENSDONE and ENDMADONE are interrupt
       enables like the others. */
    if ((aic_read(dev, SSTAT0, 1) & dev->simode0) || (dev->sstat1 & dev->simode1)) {
        if (!(dev->intstat & SCSIINT)) {
            aic_log(dev->tag, "scsiint: sstat0 %02x&%02x sstat1 %02x&%02x at pc %03x\n",
                    dev->sstat0, dev->simode0, dev->sstat1, dev->simode1, dev->pc);
        }
        /* A SCSI interrupt also takes PAUSEDIS away, so that the pause it
           asks for cannot be refused. */
        dev->seqctl &= ~PAUSEDIS;
        aic_raise(dev, SCSIINT);
    } else if (dev->intstat & SCSIINT) {
        /* And it goes away again on its own. SCSIINT is not a latch the
           host clears: the data book gives it as set "if the corresponding
           interrupt is enabled in SIMODE0 or SIMODE1", which is why the
           part has no CLRSCSIINT to clear it with -- bit 2 of CLRINT is
           not used. Taking the cause away through CLRSINT0 or CLRSINT1 is
           what takes the interrupt with it. Leaving it standing hangs a
           driver that has done everything the part asked of it: the 2740
           BIOS clears the selection timeout it gets on the first empty ID
           of its scan, sees the interrupt still there, and clears it for
           ever. */
        dev->intstat &= ~SCSIINT;
        aic_update_irq(dev);
    }
}

static void
aic_set_sstat1(aic7xxx_t *dev, uint8_t bits)
{
    dev->sstat1 |= bits;
    aic_scsi_int(dev);
}

/* Swapping the register file between the two cells. The live fields are
   whichever channel SELBUSB names; this puts them away and brings the
   other one out. */
static void
aic_cell_swap(aic7xxx_t *dev, uint8_t ch)
{
    aic_cell_t *out;
    aic_cell_t *in;

    if (ch == dev->cell_live)
        return;

    out = &dev->cell_save[dev->cell_live];
    in  = &dev->cell_save[ch];

    out->scsiseq  = dev->scsiseq;
    out->sxfrctl0 = dev->sxfrctl0;
    out->sxfrctl1 = dev->sxfrctl1;
    out->scsisigo = dev->scsisigo;
    out->scsirate = dev->scsirate;
    out->scsiid   = dev->scsiid;
    out->sstat0   = dev->sstat0;
    out->sstat1   = dev->sstat1;
    out->simode0  = dev->simode0;
    out->simode1  = dev->simode1;
    out->selid    = dev->selid;
    out->scsitest = dev->scsitest;

    dev->scsiseq  = in->scsiseq;
    dev->sxfrctl0 = in->sxfrctl0;
    dev->sxfrctl1 = in->sxfrctl1;
    dev->scsisigo = in->scsisigo;
    dev->scsirate = in->scsirate;
    dev->scsiid   = in->scsiid;
    dev->sstat0   = in->sstat0;
    dev->sstat1   = in->sstat1;
    dev->simode0  = in->simode0;
    dev->simode1  = in->simode1;
    dev->selid    = in->selid;
    dev->scsitest = in->scsitest;

    dev->cell_live = ch;

    /* A status that was already standing on the channel just switched to
       is what the interrupt logic sees now. Only the selected cell can
       raise it: the firmware finds out which channel had the event by
       switching to it, and an interrupt raised from the cell it is not
       looking at would pause the sequencer before it could switch, with
       nothing readable to clear. */
    aic_scsi_int(dev);
}

/* Setting or clearing a status on a channel that may not be the selected
   one. A selection started on channel B finishes on channel B however
   many times the firmware has flipped SELBUSB since. */
static void
aic_ch_sstat0(aic7xxx_t *dev, uint8_t ch, uint8_t set, uint8_t clr)
{
    if (ch == dev->cell_live) {
        dev->sstat0 = (uint8_t) ((dev->sstat0 & ~clr) | set);
        aic_scsi_int(dev);
    } else
        dev->cell_save[ch].sstat0 = (uint8_t) ((dev->cell_save[ch].sstat0 & ~clr) | set);
}

static void
aic_ch_sstat1(aic7xxx_t *dev, uint8_t ch, uint8_t set, uint8_t clr)
{
    if (ch == dev->cell_live) {
        dev->sstat1 = (uint8_t) ((dev->sstat1 & ~clr) | set);
        aic_scsi_int(dev);
    } else
        dev->cell_save[ch].sstat1 = (uint8_t) ((dev->cell_save[ch].sstat1 & ~clr) | set);
}

/* The control pins the board ties, as SCSISIGI shows them for a channel.
   Zero means the channel is wired to a bus and the register reads it. */
static uint8_t
aic_strap_pins(const aic7xxx_t *dev, uint8_t ch)
{
    if (ch == 0)
        return 0;
    /* DualBusses: nothing tied, the register reads channel B's bus. */
    if (dev->twin)
        return 0;
    /* Wide: BCD- grounded. A differential board grounds BMSG- as well:
       the option ROM takes C/D with MSG as differential (x86 1F63h), and
       the Windows 98 driver's probe reads C/D for wide and then MSG for
       wide differential, which is the AHA-2744W and no other. */
    if (dev->wide)
        return (uint8_t) (CDI | (dev->diff ? MSGI : 0));
    /* One narrow connector: BMSG- grounded, BBSY- and BCD- at VDD. */
    return MSGI;
}

/* What a channel's SCSISEQ says, wherever it is kept. */
static uint8_t
aic_ch_scsiseq(const aic7xxx_t *dev, uint8_t ch)
{
    return (ch == dev->cell_live) ? dev->scsiseq : dev->cell_save[ch].scsiseq;
}

static void
aic_ch_scsiseq_clr(aic7xxx_t *dev, uint8_t ch, uint8_t bits)
{
    if (ch == dev->cell_live)
        dev->scsiseq &= (uint8_t) ~bits;
    else
        dev->cell_save[ch].scsiseq &= (uint8_t) ~bits;
}

static uint8_t
aic_ch_sxfrctl1(const aic7xxx_t *dev, uint8_t ch)
{
    return (ch == dev->cell_live) ? dev->sxfrctl1 : dev->cell_save[ch].sxfrctl1;
}

/* Our own and the target's identifier, which is per channel like the rest
   of the cell -- the firmware writes it on the channel it is about to
   select on, and may have switched the file away before the selection
   finishes. Reading the live copy then selected whoever the other channel
   happened to be pointed at. */
static uint8_t
aic_ch_scsiid(const aic7xxx_t *dev, uint8_t ch)
{
    return (ch == dev->cell_live) ? dev->scsiid : dev->cell_save[ch].scsiid;
}

static void
aic_ch_selid(aic7xxx_t *dev, uint8_t ch, uint8_t val)
{
    if (ch == dev->cell_live)
        dev->selid = val;
    else
        dev->cell_save[ch].selid = val;
}

static void
aic_set_sstat0(aic7xxx_t *dev, uint8_t bits)
{
    dev->sstat0 |= bits;
    aic_scsi_int(dev);
}

/* ---- the SCSI bus ------------------------------------------------------- */

/* REQINIT follows REQ, and PHASECHG latches a phase that is not the one
   the sequencer last acknowledged by writing SCSISIGO. Both are edges the
   firmware waits on, so they are recomputed whenever the bus moves. */
static void
aic_bus_changed(aic7xxx_t *dev)
{
    uint8_t phase;
    /* Only from the channel it is on. The other cell has its own REQ and
       its own phase, and while the file is switched away from this
       connection none of it is readable. */
    uint8_t req = (dev->bus_state == BUS_BUSY) && dev->tgt_req && (dev->cur_ch == dev->cell_live);

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

    /* SPIORDY is automatic PIO's: "As an initiator, this bit is set to
       one on the leading edge of REQ", and only while SPIOEN is on and
       no DMA engine has the bus -- "This bit may be left on even when in
       DMA mode since SCSIEN or SDMAEN override this bit." It falls on
       the SCSIDATL access that moves the byte, and on CLRSPIORDY, and
       with SPIOEN. An earlier note here kept it up until cleared by hand
       because one part's silicon was not seen to drop it; the book says
       it drops, and a firmware that trusts the book -- Windows 98's
       ARROW.MPD -- starts its command DMA, reads SSTAT0, finds SDONE
       standing on a SPIORDY left over from the last message byte, and
       cancels the transfer it just started. */
    if (req && !dev->req_seen && (dev->sxfrctl0 & SPIOEN) && !(dev->dfcntrl & (SCSIEN | SDMAEN)))
        aic_set_sstat0(dev, SPIORDY);
    dev->req_seen = req;

    /* A byte already waiting in the PIO latch goes out on this REQ. */
    aic_pio_out(dev);

    dev->asleep = 0;
    aic_seq_kick(dev);
}

static void
aic_bus_free(aic7xxx_t *dev)
{
    aic_log(dev->tag, "bus free (was %s%s)\n",
            (dev->bus_state == BUS_BUSY) ? "busy " : "free ",
            aic_phase_name(dev->tgt_phase));
    dev->bus_state = BUS_FREE;
    dev->tgt_req   = 0;
    dev->tgt_phase = 0;
    dev->cur       = NULL;
    dev->atn       = 0;
    dev->datl_full = 0;
    dev->req_wait  = 0;
    /* Bus free takes SCSISIGO, SELDO and SELDI with it -- on the cell
       the connection was on, which is not necessarily the one the file
       is switched to. */
    if (dev->cur_ch == dev->cell_live) {
        dev->scsisigo = 0;
        dev->sstat0 &= ~(SELDO | SELDI);
    } else {
        dev->cell_save[dev->cur_ch].scsisigo = 0;
        dev->cell_save[dev->cur_ch].sstat0 &= (uint8_t) ~(SELDO | SELDI);
    }
    dev->msgin_len = dev->msgin_pos = 0;
    dev->msgout_len                 = 0;
    timer_stop(&dev->tgt_timer);
    timer_stop(&dev->req_timer);
    aic_set_sstat1(dev, BUSFREE);
    aic_bus_changed(dev);
}

static aic_cmd_t *
aic_cmd_alloc(aic7xxx_t *dev)
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
aic_cmd_free(aic7xxx_t *dev, aic_cmd_t *c)
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
aic_find_reselect(aic7xxx_t *dev)
{
    double now = aic_now_us();

    for (uint8_t i = 0; i < AIC_CMDS; i++) {
        const aic_cmd_t *c = &dev->cmds[i];

        /* "ENRSELI: enables the device to respond to a reselection" is a
           bit in the cell the target is wired to, and the cell answers
           whether or not SELBUSB has its registers in front. The
           firmware's idle loop flips SELBUSB every pass, so testing the
           live copy refused a channel A target whenever the flip had
           landed on B -- and nothing retried until the host's timeout
           wrote SCSISEQ fifteen seconds later. */
        if (c->used && c->waited && (now >= c->ready_at) && (aic_ch_scsiseq(dev, aic_ch_of_bus(dev, c->bus)) & ENRSELI))
            return &dev->cmds[i];
    }
    return NULL;
}

static int
aic_any_disconnected(const aic7xxx_t *dev)
{
    for (uint8_t i = 0; i < AIC_CMDS; i++) {
        if (dev->cmds[i].used && dev->cmds[i].waited)
            return 1;
    }
    return 0;
}

static void
aic_msgin(aic7xxx_t *dev, const uint8_t *msg, int len, uint8_t after)
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
aic_cmd_execute(aic7xxx_t *dev, aic_cmd_t *c)
{
    scsi_device_t *sd = &scsi_devices[c->bus][c->id];
    double         p;

    (void) dev; /* for the log only */

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
    aic_log(dev->tag, "[%.3f ms] cmd %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x (len %u) id %i lun %i tag %02x -> data %u %s "
                      "status %02x\n",
            aic_now_us() / 1000.0, c->cdb[0], c->cdb[1], c->cdb[2], c->cdb[3], c->cdb[4],
            c->cdb[5], c->cdb[6], c->cdb[7], c->cdb[8], c->cdb[9], c->cdb_len, c->id, c->lun, c->tagged ? c->tag : 0xff, c->data_len,
            c->data_in ? "in" : "out", c->status);
}

/* The data the target collected on a write is handed to the device. */
static void
aic_cmd_finish_out(aic7xxx_t *dev, aic_cmd_t *c)
{
    scsi_device_t *sd = &scsi_devices[c->bus][c->id];

    (void) dev; /* for the log only */

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
aic_tgt_schedule(aic7xxx_t *dev)
{
    timer_on_auto(&dev->tgt_timer, 1.0);
}

/* How many sequencer instructions pass between ACK for one PIO byte and
   the target's REQ for the next. A real target drops REQ when it sees ACK
   and raises it again when ACK has gone, and the sequencer gets a few
   instructions in meanwhile; a program that does not wait for that REQ
   works on a bus that answers instantly and nowhere else. */
#define AIC_REQ_INSNS 2

/* And how long a target takes between message bytes, which is a different
   thing. Data streams through a drive's SCSI engine; message bytes go up
   to its firmware one at a time and come back microseconds later. The
   AHA-2740's option ROM is written for that. When its sequencer hands an
   extended message to the host (INTCODE 7), the interrupt handler releases
   the sequencer before the host has read anything, and the host then peeks
   the opcode off the lines while the sequencer -- which has acknowledged it
   and gone to wait for the next REQ -- has nothing yet to see. That only
   works because the drive is slower than the host's next I/O cycle, and a
   turnaround of two sequencer instructions made the model faster than any
   host could be. This is a property of drives, not of the chip, and the
   data book is silent on it; the number is a typical one, not a measured
   one, and is the only thing in this file that is. */
#define AIC_MSG_TURNAROUND_US 5.0

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
aic_tgt_req_again(aic7xxx_t *dev)
{
    if (dev->in_dma) {
        dev->tgt_req = 1;
        aic_bus_changed(dev);
        return;
    }
    /* What stays on the lines while REQ is down. */
    if (dev->tgt_phase & IOI)
        dev->tgt_held = aic_tgt_byte(dev);
    dev->tgt_req = 0;
    if (dev->tgt_phase == P_MESGIN) {
        /* Real time, and not brought forward by a host access: the
           host getting in ahead of this REQ is the point. */
        dev->req_wait = 0;
        aic_bus_changed(dev);
        timer_on_auto(&dev->req_timer, AIC_MSG_TURNAROUND_US);
        return;
    }
    dev->req_wait = AIC_REQ_INSNS;
    aic_bus_changed(dev);
}

/* The next message byte is up. */
static void
aic_tgt_req_timer(void *priv)
{
    aic7xxx_t *dev = (aic7xxx_t *) priv;

    if ((dev->bus_state != BUS_BUSY) || (dev->tgt_phase != P_MESGIN))
        return;
    dev->tgt_req = 1;
    aic_bus_changed(dev);
}

/* That REQ arrives. The host is far too slow to get in ahead of it, so
   any access from the host brings it forward. */
static void
aic_tgt_req_due(aic7xxx_t *dev)
{
    if (!dev->req_wait)
        return;
    dev->req_wait = 0;
    if (dev->bus_state != BUS_BUSY)
        return;
    dev->tgt_req = 1;
    aic_bus_changed(dev);
}

#ifdef ENABLE_AIC7XXX_LOG
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
aic_tgt_next(aic7xxx_t *dev)
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
        aic_log(dev->tag, "tgt: %s\n", aic_phase_name(dev->tgt_phase));
        aic_bus_changed(dev);
        return;
    }

    /* The length of a CDB is known only from its first byte. */
    if ((c->cdb_pos == 0) || (c->cdb_pos < c->cdb_len)) {
        dev->tgt_phase = P_COMMAND;
        dev->tgt_req   = 1;
        aic_log(dev->tag, "tgt: %s\n", aic_phase_name(dev->tgt_phase));
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
            /* The target has promised to come back in four microseconds,
               and it is this clock that brings it. Parking a command
               without starting the clock leaves the reselection waiting
               on whatever the host happens to do next, which on a host
               that is polling is its next timer tick. */
            aic_seq_kick(dev);
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
        aic_log(dev->tag, "tgt: %s %u/%u\n", aic_phase_name(dev->tgt_phase), c->data_pos,
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
        aic_log(dev->tag, "tgt: status %02x\n", c->status);
        aic_bus_changed(dev);
        return;
    }

    msg = 0x00; /* COMMAND COMPLETE */
    aic_log(dev->tag, "tgt: msg-in complete\n");
    aic_msgin(dev, &msg, 1, AFTER_FREE);
    aic_bus_changed(dev);
}

static void
aic_tgt_timer(void *priv)
{
    aic7xxx_t *dev = (aic7xxx_t *) priv;

    aic_tgt_next(dev);
}

/* A byte the initiator drives at the target in the current phase. */
static void
aic_tgt_take(aic7xxx_t *dev, uint8_t val)
{
    aic_cmd_t *c = dev->cur;

    switch (dev->tgt_phase) {
        case P_MESGOUT:
            aic_log(dev->tag, "tgt: msg-out %02x\n", val);
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
                        /* The reply needs a REQ of its own. REQINIT is an
                           edge -- see aic_bus_changed -- and message out
                           left REQ asserted, so asserting it again here
                           makes no edge at all and the firmware never
                           sees the answer: the AHA-2740's ROM sends its
                           SDTR, waits on REQINIT in a one instruction
                           loop at sequencer address 145h, and stays
                           there. A real target drops REQ when it sees
                           ACK and raises it again once ACK has gone, and
                           that is what every other phase change here
                           goes through. */
                        aic_tgt_req_again(dev);
                        return;
                    }
                    if ((n >= 2) && (dev->msgout[2] == 0x03)) { /* WDTR */
                        reply[0]        = 0x01;
                        reply[1]        = 0x02;
                        reply[2]        = 0x03;
                        reply[3]        = (dev->wide && dev->msgout[3]) ? 0x01 : 0x00;
                        dev->msgout_len = 0;
                        aic_msgin(dev, reply, 4, AFTER_NEXT);
                        /* And so does this one, for the same reason. */
                        aic_tgt_req_again(dev);
                        return;
                    }
                    dev->msgout_len = 0;
                }
                break;
            } else if (val == 0x08) { /* NOP */
                dev->msgout_len = 0;
            } else if ((val == 0x06) || (val == 0x0c) || (val == 0x0d)) {
                /* ABORT, BUS DEVICE RESET, ABORT TAG */
                aic_log(dev->tag, "msgout %02x: dropping command\n", val);
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
aic_tgt_byte(const aic7xxx_t *dev)
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
aic_tgt_acked(aic7xxx_t *dev)
{
    aic_cmd_t *c = dev->cur;

    dev->tgt_held = aic_tgt_byte(dev);

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
aic_select_start_ch(aic7xxx_t *dev, uint8_t ch)
{
    if (dev->selecting || (dev->bus_state != BUS_FREE))
        return;
    dev->selecting = 1;
    /* And it stays that channel's until it finishes, however many times
       the firmware flips SELBUSB while it is running. */
    dev->sel_ch = ch;
    aic_ch_sstat0(dev, ch, SELINGO, 0);
    /* The timer is nominal: the firmware only cares that a present
       target is quick and an absent one is not. */
    timer_on_auto(&dev->sel_timer, 100.0);
}

/* ENSELO written to the cell the file is looking at. */
static void
aic_select_start(aic7xxx_t *dev)
{
    aic_select_start_ch(dev, dev->cell_live);
}

/* A selection that could not have the bus is still wanted, on whichever
   cell asked for it. ENSELO written while a target holds the bus is not
   thrown away: the chip waits for bus free, arbitrates, and selects then.
   It is that cell's SCSIID that goes out, which is why this looks at both
   and does not assume the one that happens to be switched in. */
static void
aic_select_pending(aic7xxx_t *dev)
{
    if (dev->selecting || (dev->bus_state != BUS_FREE))
        return;
    for (uint8_t ch = 0; ch < 2; ch++) {
        uint8_t seq = aic_ch_scsiseq(dev, ch);
        uint8_t st0 = (ch == dev->cell_live) ? dev->sstat0 : dev->cell_save[ch].sstat0;

        if ((seq & ENSELO) && !(st0 & SELDO)) {
            aic_select_start_ch(dev, ch);
            return;
        }
    }
}

static void
aic_select_done(void *priv)
{
    aic7xxx_t     *dev = (aic7xxx_t *) priv;
    uint8_t        id;
    scsi_device_t *sd;
    aic_cmd_t     *c;

    if (!dev->selecting) {
        aic_ch_sstat0(dev, dev->sel_ch, 0, SELINGO);
        return;
    }

    /* TID is four bits wide whatever the bus is. On a narrow one the top
       half of the addresses is not aliased down onto the bottom half --
       there is simply nothing there, and selecting it runs out. */
    id = (aic_ch_scsiid(dev, dev->sel_ch) >> 4) & 0x0f;
    sd = (!dev->wide && (id > 7)) ? NULL
                                  : &scsi_devices[aic_bus_of_ch(dev, dev->sel_ch)][id];

    /* Nobody there, and the selection timer not running: the chip goes on
       selecting for ever. CHIPRST leaves ENSTIMER clear, so a driver that
       resets the part and does not put SXFRCTL1 back never sees SELTO. */
    if ((aic_ch_scsiseq(dev, dev->sel_ch) & ENSELO) && ((sd == NULL) || !scsi_device_present(sd)) && !(aic_ch_sxfrctl1(dev, dev->sel_ch) & ENSTIMER)) {
        aic_log(dev->tag, "select %i: nobody, and no selection timer\n", id);
        /* Leaving the selection running is not the same as leaving the
           timer half armed. timer_on_auto() resumes from the previous
           expiry while period is still positive, so the arming in the
           SXFRCTL1 write -- the driver switching ENSTIMER on and asking
           for the timeout after all -- would schedule from an expiry
           already in the past rather than from now. Stop it, the way
           aic_seq_timer does. */
        timer_stop(&dev->sel_timer);
        return;
    }

    dev->selecting = 0;

    if (!(aic_ch_scsiseq(dev, dev->sel_ch) & ENSELO)) {
        aic_ch_sstat0(dev, dev->sel_ch, 0, SELINGO);
        aic_bus_changed(dev);
        return;
    }

    if ((sd == NULL) || !scsi_device_present(sd)) {
        /* SELINGO stays: the book clears it "when a successful selection
           has been completed (SELDO is one)" or by CLRSELINGO, and a
           timeout is neither. SEL comes off the bus and SELTO says why. */
        aic_log(dev->tag, "select %i: timeout on channel %c\n", id,
                dev->sel_ch ? 'B' : 'A');
        aic_ch_sstat1(dev, dev->sel_ch, SELTO, 0);
        aic_bus_changed(dev);
        return;
    }

    c = aic_cmd_alloc(dev);
    if (c == NULL) {
        aic_ch_scsiseq_clr(dev, dev->sel_ch, ENSELO);
        aic_ch_sstat1(dev, dev->sel_ch, SELTO, 0);
        aic_bus_changed(dev);
        return;
    }
    c->id  = id;
    c->bus = aic_bus_of_ch(dev, dev->sel_ch);

    /* "When a successful selection has been completed (SELDO is one),
       this bit will be cleared." */
    aic_ch_sstat0(dev, dev->sel_ch, 0, SELINGO);
    dev->cur_ch    = dev->sel_ch;
    dev->cur       = c;
    dev->bus_state = BUS_BUSY;
    aic_ch_selid(dev, dev->cur_ch, (uint8_t) (id << 4));
    /* ATN is not driven through SCSISIGO for this: the chip raises it
       itself on a successful select-out when told to, which is how the
       firmware gets a message out phase for its identify message. */
    /* From the channel the selection was started on, not the one the file
       happens to be switched to when it finishes: the firmware goes back
       to its poll loop, which flips SELBUSB every pass, while the
       selection runs. */
    if (aic_ch_scsiseq(dev, dev->sel_ch) & ENAUTOATNO)
        dev->atn = 1;
    aic_log(dev->tag, "select %i: ok on channel %c (atn %i, sblkctl %02x) scb%u ctl %02x tcl %02x cmdlen %02x\n", id,
            dev->sel_ch ? 'B' : 'A', dev->atn, dev->sblkctl, dev->scbptr & (dev->chip->scb_pages - 1),
            dev->scb[dev->scbptr & (dev->chip->scb_pages - 1)][0x00],
            dev->scb[dev->scbptr & (dev->chip->scb_pages - 1)][0x01],
            dev->scb[dev->scbptr & (dev->chip->scb_pages - 1)][0x18]);
    aic_ch_sstat0(dev, dev->sel_ch, SELDO, 0);
    /* The target asks for the identify message, or for the command. */
    aic_tgt_next(dev);
}

/* A disconnected target coming back. Allowed when ENRSELI is set and the
   bus is free; SELID then names it with ONEBIT clear. */
static void
aic_reselect_try(aic7xxx_t *dev)
{
    aic_cmd_t *c;
    uint8_t    msg;

    if ((dev->bus_state != BUS_FREE) || dev->selecting)
        return;
    c = aic_find_reselect(dev);
    if (c == NULL)
        return;

    c->waited      = 0;
    dev->cur       = c;
    dev->bus_state = BUS_BUSY;
    if (aic_ch_scsiseq(dev, aic_ch_of_bus(dev, c->bus)) & ENAUTOATNI)
        dev->atn = 1;

    /* The reconnection is reported on the cell of the channel the target
       is wired to, and nowhere else: SELDI goes into that cell's SSTAT0,
       and SBLKCTL is left exactly as the firmware had it. The firmware
       does its own looking. Its poll loop flips SELBUSB every pass --
       "xor SBLKCTL, SELBUSB" sits between the SELDO and SELDI tests --
       so a reconnection on either channel is seen within two passes, on
       the pass that has that cell in front, and the identify message and
       the SCB search that follow (01Dh, 034h) read the channel out of the
       SBLKCTL the firmware set itself.

       An earlier pass here switched SBLKCTL and the register file to the
       target's channel on the firmware's behalf, on the claim that the
       hardware does. The book gives SBLKCTL no such behaviour, and on a
       twin board the switch landed in the middle of the firmware's
       selection setup for the other channel: SCSIID had gone to channel
       A and the SCSISEQ write that started the selection went to channel
       B, which then selected ID 0 with no SCB behind it, INTCODE 8, and
       a bus reset for every command Windows 98 ran on the two channels
       together. The switch was only ever needed while SELDI lived in a
       single register file; with a cell per channel it is not. */
    dev->cur_ch = aic_ch_of_bus(dev, c->bus);
    aic_ch_selid(dev, dev->cur_ch, (uint8_t) (c->id << 4));

    aic_log(dev->tag, "[%.3f ms] reselect %i lun %i tag %02x on channel %c (sblkctl %02x)\n",
            aic_now_us() / 1000.0, c->id, c->lun, c->tagged ? c->tag : 0xff,
            dev->cur_ch ? 'B' : 'A', dev->sblkctl);
    aic_ch_sstat0(dev, dev->cur_ch, SELDI, 0);

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
aic_scsi_reset_bus(aic7xxx_t *dev)
{
    aic_log(dev->tag, "[%.3f ms] scsi bus reset\n", aic_now_us() / 1000.0);
    /* A bus reset is where a driver starts over, and where the trace
       should too: the bounded traces above were spent on the option
       ROM's boot-time traffic before the driver ever loaded, and the
       driver's own first command went unrecorded. */
    dev->busl_reads = dev->sig_logs = dev->scb_dumps = 0;
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
    /* "All bits except SCSIRSTO are cleared by SCSI Bus Reset" -- on
       both cells, not only the one the file is looking at. Leaving the
       other bank's ENSELO standing had the next bus-free restart a
       selection from it with whatever SCSIID it last held. */
    dev->scsiseq &= SCSIRSTO;
    dev->sstat0 &= ~(SELDO | SELDI | SELINGO);
    for (uint8_t ch = 0; ch < 2; ch++) {
        if (ch != dev->cell_live) {
            dev->cell_save[ch].scsiseq &= SCSIRSTO;
            dev->cell_save[ch].sstat0 &= (uint8_t) ~(SELDO | SELDI | SELINGO);
        }
    }
    timer_stop(&dev->sel_timer);
    timer_stop(&dev->tgt_timer);
    timer_stop(&dev->req_timer);

    for (uint8_t i = 0; i < (dev->wide ? 16 : 8); i++)
        scsi_device_reset(&scsi_devices[aic_cur_bus(dev)][i]);

    if (dev->chip->own_reset_seen)
        aic_set_sstat1(dev, SCSIRSTI);
    aic_bus_changed(dev);
}

/* ---- the data FIFO ------------------------------------------------------ */

static void
aic_fifo_reset(aic7xxx_t *dev)
{
    dev->fifo_rd = dev->fifo_cnt = 0;
    dev->fifo_flush              = 0;
}

static void
aic_fifo_push(aic7xxx_t *dev, uint8_t val)
{
    if (dev->fifo_cnt >= FIFO_SIZE)
        return;
    dev->fifo[(dev->fifo_rd + dev->fifo_cnt) % FIFO_SIZE] = val;
    dev->fifo_cnt++;
}

static uint8_t
aic_fifo_pop(aic7xxx_t *dev)
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
aic_fifo_threshold(const aic7xxx_t *dev)
{
    /* Sixteen bytes at the lowest setting: "4 double words" in the
       AIC-7770 book, "16 Bytes" in the AIC-7870's table. */
    static const uint16_t level[4] = { 16, FIFO_SIZE / 2, (FIFO_SIZE * 3) / 4, FIFO_SIZE };

    /* Register 86h, whichever part this is. On the AIC-7770 it is BUSSPD,
       and the data book is explicit that "in EISA mode, STBON(3:0) and
       STBOFF(3:0) have no meaning, but DFTHRSH(1:0) is still used" -- so
       the threshold is in there, not in the PCI status register the later
       parts keep it in. Reading the wrong one left the EISA board on the
       lowest threshold whatever it had been told. */
    return level[(dev->eisa ? dev->busspd : dev->dspcistatus) >> 6];
}

/* The hardware flushes by itself when the SCSI side of a read is over:
   the count ran out, or the target left the phase. */
static void
aic_fifo_autoflush(aic7xxx_t *dev)
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
aic_dma_host_wants(const aic7xxx_t *dev)
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
aic_dma_host(aic7xxx_t *dev)
{
    uint8_t  buf[64];
    uint32_t n;

    aic_fifo_autoflush(dev);

    if (!(dev->dfcntrl & HDMAEN) || dev->host_wait)
        return;
    /* No bus mastering, no transfer: MASTEREN in the PCI command register,
       or the board's own bus drivers on the EISA part. */
    if (dev->eisa) {
        if (!(dev->bctl & 0x01))
            return;
    } else if (!(dev->pci_regs[0x04] & 0x04))
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
                aic_log(dev->tag, "  dma rd %08x +%u: %02x %02x %02x %02x %02x %02x %02x %02x\n", dev->haddr, n, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
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
                aic_log(dev->tag, "  dma wr %08x +%u (hcnt %u): %02x %02x %02x %02x %02x %02x %02x %02x\n", dev->haddr, n, dev->hcnt, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
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
aic_dma_scsi(aic7xxx_t *dev)
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
aic_bitbucket(aic7xxx_t *dev)
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
aic_pump(aic7xxx_t *dev)
{
    /* A selection that could not have the bus is still wanted. ENSELO
       written while a target holds the bus -- most often one that has
       just reselected, which the sequencer has yet to notice -- is not
       thrown away: the chip waits for bus free, arbitrates, and selects
       then. Dropping it loses the command the firmware had just taken
       off its queue, and nothing ever asks for it again. */
    aic_select_pending(dev);

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
aic_pio_counted(aic7xxx_t *dev)
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
aic_pio_out(aic7xxx_t *dev)
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
aic_scb_offset(aic7xxx_t *dev, uint8_t addr, int seq)
{
    uint8_t off;

    /* The auto-increment is the host's: "If SCBAUTO is set, then
       SCBCNT(4:0) determines the address, and the SCB address is
       automatically incremented on an I/O Read or Write" -- an I/O access,
       through the SCB I/O address space. The sequencer reaches the array
       over its own internal bus at A0h-BFh and names the byte it wants.
       Applying the counter to it as well broke the first driver that
       loads an SCB with a block write and leaves SCBAUTO set, as the
       NT-style ones do: the firmware then read its command pointer from
       wherever the counter had stopped -- the scatter/gather element --
       and sent that to the target as the CDB. */
    if (seq || !(dev->scbcnt & SCBAUTO))
        return (uint8_t) (addr - SCB_BASE);

    off         = dev->scbcnt & 0x1f;
    dev->scbcnt = (uint8_t) ((dev->scbcnt & 0xe0) | ((off + 1) & 0x1f));
    return off;
}

/* The host is slow. By the time one of its accesses lands, anything that
   was a few sequencer instructions away has long since happened. */
static void
aic_host_catch_up(aic7xxx_t *dev)
{
    aic_tgt_req_due(dev);
    if (dev->host_wait) {
        dev->host_wait = 0;
        aic_pump(dev);
    }
}

static uint8_t
aic_read(aic7xxx_t *dev, uint8_t addr, int seq)
{
    uint8_t ret = 0;

    if (!seq) {
        aic_loop_wake(dev);
        aic_host_catch_up(dev);
    }

    /* "Illegal Host Address. This bit is set when the Host accesses a
       register, which is unavailable to the Host, while the Sequencer is
       not paused." Unavailable is the word: the register file is the
       sequencer's while it runs -- "All registers are available to the
       Host computer and to the Sequencer ... but not at the same time" --
       and the host gets nothing back, as it does from any location that
       decodes to no register. The error is recorded, and with FAILDIS
       clear it pauses the sequencer, so the next access is legal.

       Software depends on the nothing. ASPI7DOS and Windows 98's AIC-7770
       driver both probe a running chip by reading SCSISEQ, SXFRCTL0 and
       SXFRCTL1 unpaused: three zeros mean the BIOS's firmware has the
       part, and only then do they look at SCB 0 for the BIOS's mark and
       leave that SCB to it. Answered with the live values, ASPI7DOS took
       the BIOS's every completion as its own, and the BIOS's INT 13h
       waited fifteen seconds for each one. */
    if (!seq && dev->chip->host_pause_checked && !aic_paused(dev) && !aic_host_no_pause(addr, 0)) {
        aic_hard_error(dev, ILLHADDR, addr, 0);
        return 0x00;
    }

    if ((addr >= SRAM_BASE) && (addr < 0x60))
        return dev->sram[addr - SRAM_BASE];
    if (addr >= SCB_BASE) {
        if (addr < (SCB_BASE + SCB_SIZE))
            return dev->scb[dev->scbptr & (dev->chip->scb_pages - 1)][aic_scb_offset(dev, addr, seq)];
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
            /* "Reads the actual state of the signals on the SCSI bus
               pins." A pin is asserted when it is at ground, so a control
               line the board ties low reads as a one here whatever the
               bus is doing, and that is how the board tells the part --
               and the option ROM -- what it is wired for. The book's
               strap table, by channel B's pins:

                   Configuration    BBSY-  BCD-  BMSG-
                   DualBusses       VDD    VDD   VDD
                   Channel A only   VDD    VDD   GND
                   Wide             VDD    GND   don't care

               so on a board with one narrow connector, channel B's file
               shows MSG asserted and BSY and C/D negated with nothing
               else driven: the ROM sets SELBUSB, reads here, and takes
               MSG without BSY as no second channel (x86 1E07h) and C/D
               with MSG as a differential board (1F63h). A wide board
               grounds BCD-, and software that wants to see it clears
               SELWIDE and sets SELBUSB first, as both the ROM and the
               Windows 98 driver do. */
            ret = aic_strap_pins(dev, dev->cell_live);
            if (ret == 0) {
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
            }
            if (!seq && (dev->sig_logs < 64)) {
                dev->sig_logs++;
                aic_log(dev->tag, "host reads SCSISIGI = %02x on channel %c (sblkctl %02x, bus %s)\n",
                        ret, dev->cell_live ? 'B' : 'A', dev->sblkctl,
                        (dev->bus_state == BUS_BUSY) ? "busy" : "free");
            }
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
                if (dev->busl_reads < 512) {
                    dev->busl_reads++;
                    aic_log(dev->tag, "%s reads SCSIDATL = %02x (phase %s, msgin %u/%u, "
                                      "spioen %u -> %s)\n",
                            seq ? "seq" : "host", ret,
                            aic_phase_name(dev->tgt_phase), dev->msgin_pos, dev->msgin_len,
                            !!(dev->sxfrctl0 & SPIOEN),
                            (dev->sxfrctl0 & SPIOEN) ? "acked" : "NOT acked");
                }
                /* The handshake is automatic PIO's, and SPIOEN is what
                   turns that on: "The individual PIO transfers are
                   triggered by reading or writing to SCSIDATL register
                   ... Writing a zero to this bit will stop any further
                   PIO transfers without corrupting any valid data in the
                   SCSIDATL register." Manual mode is the other thing --
                   the book has it done "via the SCSI data latch registers
                   SCSIBUSL and SCSIBUSH" with the handshake driven by
                   hand through SCSISIGO, which is ACKO below. So with
                   SPIOEN clear a read here is only a look at the latch.
                   An earlier pass removed this gate on the strength of
                   the SCSIDATL description alone; the SPIOEN text is the
                   more specific and it puts the gate back. */
                if (dev->sxfrctl0 & SPIOEN) {
                    /* "During a transfer from SCSI, it is cleared on a
                       read from SCSIDATL." */
                    dev->sstat0 &= ~SPIORDY;
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
            /* "This bit sets SDONE when it is set." */
            if (dev->sstat0 & SPIORDY)
                ret |= SDONE;
            return ret;
        case SSTAT1:
            return dev->sstat1;
        case SSTAT2:
            /* OVERRUN and the SCSI FIFO byte count. Both are zero here
               for reasons rather than for convenience: the overrun is a
               synchronous offset running out -- "the maximum offset has
               been reached and another REQ is detected before an ACK is
               asserted" -- which a model that moves a byte at a time
               cannot reach; and SFCNT counts the sixteen byte SCSI FIFO
               that sits between the bus and the data FIFO, which is not
               separately modelled. Software may only read this while
               transfers are stopped, and stopped is when that FIFO is
               empty.

               Nothing this part runs reads it. The bits a later driver
               names above SFCNT -- SHVALID, EXP_ACTIVE, the CRC errors --
               are Ultra2 and Ultra3 additions laid over the 7770's field,
               and the one place the sequencer tests SSTAT2 is inside its
               u2_ data phase, which this chip never reaches. */
            return 0;
        case SSTAT3:
            /* The synchronous offset counters, and the book says when
               they may be looked at: "Do not read this counter unless
               transfers are stopped." Stopped is exactly when both are
               zero. */
            return 0;
        case SIMODE0:
            return dev->simode0;
        case SIMODE1:
            return dev->simode1;
        case SCSIBUSL:
            /* The data lines, read without acknowledging, and only from
               the channel the connection is on: the other cell has its
               own bus and its own byte on it. */
            /* "This register reads data on the SCSI Data bus directly.
               Data is gated from the SCSI Data bus to the internal Data
               bus, it is not latched in the SCSI module." Nothing about
               REQ: between two handshakes of an IN phase the target is
               still driving the lines, with the byte it last offered
               until it puts the next one up. The AHA-2740's option ROM
               reads exactly that -- its extended message handler peeks
               the opcode here during the turnaround after the sequencer
               has acknowledged it -- and answering zero while REQ was
               down gave it nothing to classify. */
            if ((dev->bus_state == BUS_BUSY) && (dev->tgt_phase & IOI) && (dev->cur_ch == dev->cell_live)) {
                uint8_t b = dev->tgt_req ? aic_tgt_byte(dev) : dev->tgt_held;

                if (dev->busl_reads < 512) {
                    dev->busl_reads++;
                    aic_log(dev->tag, "%s reads SCSIBUSL = %02x (phase %s, "
                                      "msgin %u/%u)\n",
                            seq ? "seq" : "host", b,
                            aic_phase_name(dev->tgt_phase), dev->msgin_pos,
                            dev->msgin_len);
                }
                return b;
            }
            if (dev->busl_reads < 512) {
                dev->busl_reads++;
                aic_log(dev->tag, "%s reads SCSIBUSL = 00 (no byte: state %u "
                                  "req %u cur_ch %u live %u phase %s)\n",
                        seq ? "seq" : "host", dev->bus_state, dev->tgt_req,
                        dev->cur_ch, dev->cell_live,
                        aic_phase_name(dev->tgt_phase));
            }
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
            if (!dev->chip->aux_regs)
                return 0x00;
            return dev->sleepctl;
        case SELID:
            return dev->selid;
        case SCAMCTL:
            if (!dev->chip->aux_regs)
                return 0x00;
            return dev->scamctl;
        case SPIOCAP:
            /* Nothing lives here on an AIC-7770; the data book's summary
               runs SELTIMER, SELID, then straight to SBLKCTL, and "all
               unassigned register locations return 0". The board's
               serial EEPROM, its GAL and its SCAM logic are all later
               parts -- this one keeps its settings in the EISA
               configuration chip up in scratch instead. */
            if (!dev->chip->aux_regs)
                return 0x00;
            /* SEEPROM and termination sensing present, and a ROM. */
            return 0x08 | 0x02 | 0x01;
        case BRDCTL:
            if (!dev->chip->aux_regs)
                return 0x00;
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
            if (!dev->chip->aux_regs)
                return 0x00;
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
            /* Two reads per entry, low byte first, "starting from the
               last location pushed on the stack": that is the slot below
               the pointer, which names the next free one. Eight reads
               bring the pointer back round to where it was. */
            if (dev->stack_rd == 0) {
                dev->stack_rd = 1;
                return dev->stack[(dev->sp - 1) & 3] & 0xff;
            }
            dev->stack_rd = 0;
            ret           = (dev->stack[(dev->sp - 1) & 3] >> 8) & 0xff;
            dev->sp       = (dev->sp - 1) & 3;
            return ret;

        case DSVENDID:     /* BID0 on the EISA part */
        case DSVENDID + 1: /* BID1 */
        case DSDEVID:      /* BID2 */
        case DSDEVID + 1:  /* BID3 */
            /* On the AIC-7770 these four are the EISA product identifier
               and nothing else: read only, and the same bytes the slot
               answers with at zC80. On the PCI parts they are the vendor
               and device ID, which is how a driver with the register
               window but not the slot -- and the sequencer, which has
               nothing else -- tells the family apart. */
            if (dev->eisa)
                return dev->eisa_id[addr - DSVENDID];
            return dev->pci_regs[addr - DSVENDID];
        case DSCOMMAND0: /* BCTL on the EISA part */
            if (dev->eisa)
                return dev->bctl;
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
        case DSCOMMAND1: /* BUSTIME on the EISA part */
            if (dev->eisa)
                return dev->bustime;
            /* The latency timer, over HADDLDSEL. */
            return (dev->pci_regs[0x0d] & 0xfc) | (dev->dscommand1 & 0x03);
        case DSPCISTATUS: /* BUSSPD on the EISA part */
            if (dev->eisa)
                return dev->busspd;
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
            /* Not one whole word: the read and write pointers are on
               the same location, whatever bytes are in it. */
            if (dev->fifo_cnt < dev->chip->fifo_word)
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
            /* Reserved on an AIC-7770: its write address is the seven
               bits of DFWADDR0 and the byte above it reads zero. */
            if (!dev->chip->fifo_addr_hi)
                return 0x00;
            return dev->dfwaddr[1];
        case DFRADDR:
            return dev->dfraddr[0];
        case DFRADDR + 1:
            if (!dev->chip->fifo_addr_hi)
                return 0x00;
            return dev->dfraddr[1];
        case DFDAT:
            return aic_fifo_pop(dev);
        case SCBCNT:
            return dev->scbcnt;
        case QINFIFO:
            /* The sequencer takes the next queued SCB. "Reads when
               QINCNT=0 are ignored": an ignored read does not shift the
               queue, and what it shows is what its output already holds --
               the SCB it gave last, or 00h, the reset value, before it has
               given any. */
            if (dev->qin_cnt == 0)
                return dev->qin_last;
            ret           = dev->qin[dev->qin_rd];
            dev->qin_rd   = (dev->qin_rd + 1) % dev->chip->q_depth;
            dev->qin_cnt--;
            dev->qin_last = ret;
            return ret;
        case QINCNT:
            return (uint8_t) dev->qin_cnt;
        case QOUTFIFO:
            /* The host takes the next completion. "Reads when QOUTCNT=0
               are ignored", the same as the inbound queue: the read shows
               the SCB it gave last and does not shift. It used to answer
               FFh -- bits the AIC-7770 has as reserved, and no value the
               part holds -- and the AHA-2740 BIOS depends on the real
               answer. With ASPI7DOS loaded both field IRQ 11, and
               ASPI7DOS's handler runs first: it pops every completion,
               and for the BIOS's own SCB 0 it chains on to the BIOS. The
               BIOS's handler then reads QOUTFIFO, now empty, expecting
               its own 0; given anything else it writes the value back for
               its owner and leaves the command pending. Every INT 13h to
               the disk sat out the BIOS's fifteen second timeout and
               failed, and Windows 98 Setup found no hard disk. */
            if (dev->qout_cnt == 0)
                return dev->qout_last;
            ret            = dev->qout[dev->qout_rd];
            dev->qout_rd   = (dev->qout_rd + 1) % dev->chip->q_depth;
            dev->qout_cnt--;
            dev->qout_last = ret;
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

/* What a write to SBLKCTL leaves there: the part's bits, and SELBUSB
   cleared whenever SELWIDE is set (see aic_write). */
static uint8_t
aic_sblkctl_value(const aic7xxx_t *dev, uint8_t val)
{
    uint8_t forced = (val & SELWIDE) ? SELBUSB : 0;

    return val & dev->chip->sblkctl_mask & ~forced;
}

static void
aic_write(aic7xxx_t *dev, uint8_t addr, uint8_t val, int seq)
{
    uint8_t was;

    if (!seq) {
        aic_loop_wake(dev);
        aic_host_catch_up(dev);
    }

    /* And a write to an unavailable register goes nowhere; see aic_read. */
    if (!seq && dev->chip->host_pause_checked && !aic_paused(dev) && !aic_host_no_pause(addr, 1)) {
        aic_hard_error(dev, ILLHADDR, addr, 1);
        return;
    }

    if ((addr >= SRAM_BASE) && (addr < 0x60)) {
        dev->sram[addr - SRAM_BASE] = val;

        /* On an EISA board the top six bytes are not scratch: they are the
           configuration chip, which the system firmware fills in at every
           power-on by replaying what the configuration utility left in the
           EISA store. Keep a copy, because a chip reset clears scratch and
           these have to survive it -- the driver resets the part and only
           then reads its interrupt and its identifier back out. */
        /* And 56h with them, which is the same kind of byte a little
           lower down: "these scratch ram locations are initialized by
           the 274X BIOS. We reuse them after capturing the BIOS settings
           during initialization." Bit 0 is the extended translation the
           BIOS chose. There has always been somewhere to keep it across
           a reset and never anything that put it there, so a reset
           handed back a zero and the geometry went with it. */
        if (dev->eisa && (addr == HA_274_BIOSGLOBAL))
            dev->eisa_global = val;

        if (dev->eisa && (addr >= SCSICONF)) {
            dev->eisa_conf[addr - SCSICONF] = val;

            if (addr == INTDEF) {
                /* The low four bits are the interrupt itself, not an index
                   into a list of them. The trigger is not here: it is
                   IRQMS in HCNTRL, so report that rather than bit 7, which
                   this line used to read and which the configuration
                   utility writes as zero whatever the trigger. */
                if (dev->irq != (val & 0x0f)) {
                    aic_log(dev->tag, "EISA IRQ %i, %s triggered\n", val & 0x0f,
                            (dev->hcntrl & IRQMS) ? "level" : "edge");
                }
                dev->irq = val & 0x0f;
            } else if (addr == HA_274_BIOSCTRL) {
                dev->bios_ctl_set = 1;
                aic_eisa_bios_overlay(dev);
                aic_eisa_bios_remap(dev);
            }
        }
        return;
    }
    if (addr >= SCB_BASE) {
        if (addr < (SCB_BASE + SCB_SIZE))
            dev->scb[dev->scbptr & (dev->chip->scb_pages - 1)][aic_scb_offset(dev, addr, seq)] = val;
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
            /* The AIC-7770 has CLRSTCNT, SPIOEN and CLRCHN here and
               nothing else: DFON, DFPEXP, FAST20 and SCAMEN came with the
               later parts and always read zero on this one. Masking them
               away on the way in rather than on the way out keeps the
               rest of the model from acting on a bit the part has not
               got -- SELTIMER, for one, behaves differently under SCAMEN. */
            val &= dev->chip->sxfrctl0_mask;
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
            /* Likewise ACTNEGEN and STPWEN. */
            val &= dev->chip->sxfrctl1_mask;
            was           = dev->sxfrctl1;
            dev->sxfrctl1 = val;
            /* The timer switched on under a selection nobody is
               answering. timer_is_on() asks whether a long period has
               been split, not whether the timer is running, and a
               hundred microseconds is never split -- so it answered no
               through a countdown that was still going and restarted it
               from the top. What this wants to know is whether the timer
               is still armed at all. */
            if ((val & ENSTIMER) && !(was & ENSTIMER) && dev->selecting && !timer_is_enabled(&dev->sel_timer))
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
            /* Kept for read-back and nothing more, and every field says
               why. SXFR is the REQ/ACK width and period -- the book gives
               them in nanoseconds against a 40 MHz clock, 50 nsec at rate
               zero -- which is a cadence a model that hands over whole
               bytes has nowhere to put. SOFS picks synchronous or
               asynchronous handshaking, "an offset value of 0 in the
               SOFS(3:0) disables synchronous data transfers", and the
               handshake itself is abstracted here; what software can
               actually see of the negotiation, the SDTR exchange, is
               carried by the message code instead. WIDEXFER is read
               through SELWIDE -- "when SELWIDE is cleared, this bit is
               ignored" -- and the byte counts either way are the same
               ones, because the data path here is bytes and not a bus
               width. */
            dev->scsirate = val;
            break;
        case SCSIID:
            dev->scsiid = val;
            break;
        case SCSIDATL:
            /* Writing offers a byte to the target and acknowledges the
               request; this is PIO out. */
            aic_log(dev->tag, "pio out %02x (%s)\n", val,
                    (dev->bus_state == BUS_BUSY) ? aic_phase_name(dev->tgt_phase) : "free");
            dev->scsidatl = val;
            /* "During a transfer to SCSI, the bit is cleared on a write to
               SCSIDATL." */
            dev->sstat0 &= ~SPIORDY;
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
            aic_scsi_int(dev);
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
            aic_scsi_int(dev);
            break;
        case SIMODE0:
            /* Bit 7 is not used and always reads zero. */
            dev->simode0 = val & dev->chip->simode0_mask;
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
            if (!dev->chip->aux_regs)
                break;
            /* Only the sequencer can put itself to sleep, and not at all
               with SLEEPDIS set. */
            dev->sleepctl = val & SLEEPDIS;
            if (seq && !(val & SLEEPDIS))
                dev->sleepctl |= val & (SLP1 | SLP0);
            break;
        case SELID:
            /* Read only on the older part: it puts the address that
               selected or reselected it here itself. */
            if (!dev->chip->selid_writable)
                break;
            dev->selid = val;
            break;
        case SCAMCTL:
            if (!dev->chip->aux_regs)
                break;
            dev->scamctl = val;
            break;
        case SPIOCAP:
            break;
        case BRDCTL:
            if (!dev->chip->aux_regs)
                break;
            dev->brdctl = val;
            break;
        case SEECTL:
            if (!dev->chip->aux_regs)
                break;
            dev->seectl = val;
            nmc93cxx_eeprom_write(dev->eeprom, !!(val & SEECS), !!(val & SEECK),
                                  !!(val & SEEDO));
            break;
        case SBLKCTL:
            /* On the AIC-7770 this register is SELBUSB and SELWIDE and
               nothing else; the data book has bits 7-4, 2 and 0 always
               reading zero. The later parts put their diagnostic LED and
               FIFO bits in the same register, so they keep the wider
               mask.

               It is the whole register block that switches, not a mode:
               "Device addresses 00h-1Eh reflect the Channel B registers.
               When this bit is cleared, addresses 00h-1Eh reflect Channel
               A registers", both channels "mapped to the same I/O space".

               What the straps decide is the reset value and nothing more:
               "SELBUSB and SELWIDE may be initialized on Chip Reset to
               indicate the hardware connection to the chip", with the
               table giving 00h for one narrow connector, 08h for two and
               02h for a wide one. The one live rule is the book's own:
               "If SELWIDE (bit 1) is set, this bit [SELBUSB] will be
               cleared", the wide connection being channel B's data lines
               lent to channel A. Everything that reads the board's
               wiring relies on the bits moving: the AHA-2740's ROM sets
               SELBUSB and reads SCSISIGI to count its channels (x86
               1DF5h-1E19h), and Windows 98's ARROW.MPD clears SELWIDE,
               sets SELBUSB, and reads the same register to tell narrow
               from wide from wide differential (its routine at 14F6Ch).
               An earlier pass here pinned SELWIDE on a wide board and
               refused SELBUSB; that probe then read channel A's idle bus,
               took the 2742W for a twin channel card, and the driver
               failed to start. */
            {
                dev->sblkctl = aic_sblkctl_value(dev, val);
                aic_cell_swap(dev, (dev->sblkctl & SELBUSB) ? 1 : 0);
                if (!seq && (dev->sig_logs < 64)) {
                    dev->sig_logs++;
                    aic_log(dev->tag, "host: SBLKCTL %02x -> %02x, channel %c in front\n",
                            val, dev->sblkctl, dev->cell_live ? 'B' : 'A');
                }
            }
            break;
        case SCSITEST:
            /* RQAKCNT, CNTRTEST and CTSTMODE, and nothing above them. */
            dev->scsitest = val & dev->chip->scsitest_mask;
            break;

        case SEQCTL:
            was = dev->seqctl;
            if (!seq) {
                aic_log(dev->tag, "host: SEQCTL %02x at pc %03x (intstat %02x sstat0 %02x "
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
                aic_log(dev->tag, "host: SEQADDR0 %02x at pc %03x\n", val, dev->pc);
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
            if (dev->eisa) {
                dev->bctl = val & 0x09;
                break;
            }
            dev->dscommand0 = val & 0xf0;
            break;
        case DSCOMMAND1:
            if (dev->eisa) {
                /* BUSTIME, and read-back is all of it: bus-on and
                   bus-off are how long the part may hold the host bus
                   between transfers, "limited to 15 us in increments of
                   1 us" against "60 us in increments of 4 us", and in
                   EISA mode BOFF also counts the BCLKs it may keep going
                   after a preemption. That is the arbitration interval
                   86Box has no notion of -- the same one the ESC's bus
                   timeout NMI would need. Register 86h next door,
                   BUSSPD, is different and is acted on: its top two bits
                   are the FIFO threshold. */
                dev->bustime = val;
                break;
            }
            dev->dscommand1 = val & 0x03;
            break;
        case DSPCISTATUS:
            if (dev->eisa) {
                dev->busspd = val;
                break;
            }
            /* Only the FIFO threshold select can be written. */
            dev->dspcistatus = val & DFTHRSH_100;
            aic_pump(dev);
            break;
        case HCNTRL:
            /* Pausing and unpausing is how a driver takes the part away
               from the sequencer and gives it back, so a trace that omits
               it cannot say whether the sequencer is stopped because it
               was told to be or because this model forgot to let it go.
               It also happens in the tightest loop either side runs: a
               firmware that is spinning writes this register millions of
               times, and logging every one of them put 2.8 GB on disc in
               three minutes and left the emulator doing nothing else. So
               the first few hundred are logged, and then one in every
               hundred thousand to show it is still going. */
            if (!seq && ((dev->hcntrl_logs < 256) || ((dev->hcntrl_logs % 100000) == 0))) {
                aic_log(dev->tag, "host: HCNTRL %02x -> %02x at pc %03x "
                                  "(intstat %02x, was %s)%s\n",
                        val, dev->hcntrl, dev->pc,
                        dev->intstat, aic_paused(dev) ? "paused" : "running",
                        (dev->hcntrl_logs >= 256) ? " [1 in 100000]" : "");
            }
            if (!seq)
                dev->hcntrl_logs++;
            was         = dev->hcntrl;
            dev->hcntrl = val & ~CHIPRST;
            /* "Clearing this bit will release the Sequencer", and that
               includes the copy of it an interrupt set. */
            if (!(val & PAUSE))
                dev->int_paused = 0;
            if (val & CHIPRST) {
                /* A chip reset from the host. The same bit reads back as
                   CHIPRESETACK to say it happened, on this part as much as
                   on the later ones, and stays set until a write to this
                   register clears it. */
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
            /* "An attempt to write these registers with HDMAEN (bit 3,
               DFCNTRL) set will cause ILLSADDR (bit 1, ERROR) to be set."
               Firmware that works on the real part cannot be doing this,
               because the silicon stops it there too -- so this firing is
               a sign the model is holding HDMAEN up where the chip would
               not. */
            if (dev->dfcntrl & HDMAEN)
                aic_hard_error(dev, ILLSADDR, addr, 1);
            if (dev->dscommand1 & 0x03) {
                dev->misc[addr - HADDR] = val;
                break;
            }
            dev->haddr  = (dev->haddr & ~(0xffU << ((addr - HADDR) * 8))) | ((uint32_t) val << ((addr - HADDR) * 8));
            dev->shaddr = (dev->shaddr & ~(0xffU << ((addr - HADDR) * 8))) | ((uint32_t) val << ((addr - HADDR) * 8));
            break;
        case HCNT:
        case HCNT + 1:
        case HCNT + 2:
            /* The count carries the same sentence as the address. */
            if (dev->dfcntrl & HDMAEN)
                aic_hard_error(dev, ILLSADDR, addr, 1);
            dev->hcnt = (dev->hcnt & ~(0xffU << ((addr - HCNT) * 8))) | ((uint32_t) val << ((addr - HCNT) * 8));
            break;
        case SCBPTR:
            /* Bits 1:0 pick the page, bit 2 reads back what was put in it
               and does nothing else, and everything above always reads
               zero. */
            dev->scbptr = val & dev->chip->scbptr_mask;
            break;
        case INTSTAT:
            /* The sequencer writes its interrupt code here. */
            aic_log(dev->tag, "[%.3f ms] seq: intstat %02x at %03x\n", aic_now_us() / 1000.0, val, dev->pc);
            if ((val & 0xf1) == 0x81) {
                /* INTCODE 8, no SCB matched: what the walk had to choose from. */
                aic_log(dev->tag, "  no SCB for channel %c: sblkctl %02x sstat0 %02x (other cell %02x) scb0 %02x/%02x scb1 %02x/%02x scb2 %02x/%02x scb3 %02x/%02x bus %u selecting %u sel_ch %u cur_ch %u\n",
                        (dev->sblkctl & SELBUSB) ? 'B' : 'A', dev->sblkctl, dev->sstat0,
                        dev->cell_save[dev->cell_live ^ 1].sstat0,
                        dev->scb[0][0], dev->scb[0][1], dev->scb[1][0], dev->scb[1][1],
                        dev->scb[2][0], dev->scb[2][1], dev->scb[3][0], dev->scb[3][1],
                        dev->bus_state, dev->selecting, dev->sel_ch, dev->cur_ch);
            }
#ifdef ENABLE_AIC7XXX_LOG
            /* Anything but a plain command complete or the delay timer:
               say how it got here. */
            if ((val & SEQINT) && ((val & 0xf0) != 0x00)) {
                for (int t = 0; t < 256; t++) {
                    uint8_t k = (uint8_t) (dev->trail_at + t);
                    aic_log(dev->tag, "  trail %03x: %08x stcnt %06x hcnt %06x fifo %u sstat1 %02x dfcntrl %02x\n",
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
            was = dev->intstat;
            if (val & SEQINT)
                dev->intstat = (dev->intstat & INT_PEND) | val;
            else
                dev->intstat |= val & INT_PEND;
            aic_int_pause(dev, val & INT_PEND, was);
            aic_update_irq(dev);
            break;
        case ERROR: /* CLRINT */
            if (!seq) {
                aic_log(dev->tag, "host: CLRINT %02x (intstat %02x) at pc %03x\n", val,
                        dev->intstat, dev->pc);
            }
            /* A breakpoint's BRKADRINT clears here; a hard error's does
               not. "If this condition occurs BRKADRINT may only be
               cleared by setting CHIPRST" (the AIC-7770 book, Hardware
               Failure Detect), and the AIC-7870's CLRBRKADRINT points at
               "causes of BRKADRINT being active which may have to be
               cleared prior to clearing the BRKADRINT bit". So it stays
               for as long as ERROR holds a cause. */
            if ((val & CLRBRKADRINT) && (dev->error == 0))
                dev->intstat &= ~BRKADRINT;
            /* There is no CLRSCSIINT on an AIC-7770: the data book has
               bit 2 of CLRINT not used, and the SCSI interrupt goes away
               only when the status behind it does, through CLRSINT0 and
               CLRSINT1. Clearing it from here let the interrupt and the
               status it stands for disagree. */
            if (val & CLRSCSIINT & dev->chip->clrint_mask)
                dev->intstat &= ~SCSIINT;
            if (val & CLRCMDINT)
                dev->intstat &= ~CMDCMPLT;
            if (val & CLRSEQINT)
                dev->intstat &= ~SEQINT;
            /* Not ILLOPCODE: only a chip reset gets rid of that. And not
               on an AIC-7770 at all -- bit 4 of CLRINT is not used there,
               the parity error being a later part's. */
            if (val & CLRPARERR & dev->chip->clrint_mask)
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
            /* "SCSIEN or SDMAEN override this bit": with a transfer
               engine on, a REQ is the engine's to answer, not automatic
               PIO's, and a SPIORDY standing from before must not read as
               SDONE against a count that has just been loaded. */
            if ((val & (SCSIEN | SDMAEN)) && !(was & (SCSIEN | SDMAEN)))
                dev->sstat0 &= ~SPIORDY;
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
            /* The last of the four causes: DFIFO written "when HDMAEN or
               SDMAEN is set". It is the pointers that are meant and not
               DFDAT -- the data register is how the FIFO is fed by hand,
               and is written under SCSIEN as a matter of course -- while
               moving the pointers under a running engine is what corrupts
               it. */
            if (dev->dfcntrl & (HDMAEN | SDMAEN))
                aic_hard_error(dev, ILLSADDR, addr, 1);
            dev->dfwaddr[0] = val;
            break;
        case DFWADDR + 1:
            if (dev->dfcntrl & (HDMAEN | SDMAEN))
                aic_hard_error(dev, ILLSADDR, addr, 1);
            dev->dfwaddr[1] = val;
            break;
        case DFRADDR:
            if (dev->dfcntrl & (HDMAEN | SDMAEN))
                aic_hard_error(dev, ILLSADDR, addr, 1);
            dev->dfraddr[0] = val;
            break;
        case DFRADDR + 1:
            if (dev->dfcntrl & (HDMAEN | SDMAEN))
                aic_hard_error(dev, ILLSADDR, addr, 1);
            dev->dfraddr[1] = val;
            break;
        case DFDAT:
            aic_fifo_push(dev, val);
            if (dev->dfcntrl & (SCSIEN | HDMAEN))
                aic_pump(dev);
            break;
        case SCBCNT:
            dev->scbcnt = val & (SCBAUTO | 0x1f);
            break;
        case QINFIFO:
            /* The host queues an SCB for the sequencer. Which SCB, and
               what is in the two bytes the firmware dispatches on --
               SCB_CONTROL, and SCB_TCL with the channel in bit 3 -- is
               the difference between a command that can run and one the
               firmware will hand back for ever. */
            if (!seq) {
#ifdef ENABLE_AIC7XXX_LOG
                const uint8_t *scb = dev->scb[val & (dev->chip->scb_pages - 1)];
#endif

                aic_log(dev->tag, "host: queue scb%u ctl %02x tcl %02x "
                                  "(id %u ch %c lun %u), sblkctl %02x qincnt %u\n",
                        val & (dev->chip->scb_pages - 1), scb[0x00], scb[0x01],
                        (scb[0x01] >> 4) & 0x0f, (scb[0x01] & 0x08) ? 'B' : 'A',
                        scb[0x01] & 0x07, dev->sblkctl, dev->qin_cnt);
                /* And the whole block, the first few dozen times after a
                   reset: what the firmware does with a command is decided
                   by these bytes and nothing else, and a driver whose
                   layout the model has never seen can only be read off
                   them. */
                if (dev->scb_dumps < 64) {
                    dev->scb_dumps++;
                    aic_log(dev->tag, "  scb%u: %02x %02x %02x %02x %02x %02x %02x %02x  %02x %02x %02x %02x %02x %02x %02x %02x  "
                                      "%02x %02x %02x %02x %02x %02x %02x %02x  %02x %02x %02x %02x %02x %02x %02x %02x\n",
                            val & (dev->chip->scb_pages - 1),
                            scb[0], scb[1], scb[2], scb[3], scb[4], scb[5], scb[6], scb[7],
                            scb[8], scb[9], scb[10], scb[11], scb[12], scb[13], scb[14], scb[15],
                            scb[16], scb[17], scb[18], scb[19], scb[20], scb[21], scb[22], scb[23],
                            scb[24], scb[25], scb[26], scb[27], scb[28], scb[29], scb[30], scb[31]);
                }
            }
            /* "Writes when QINCNT=4 ... are ignored." */
            if (dev->qin_cnt < dev->chip->q_depth) {
                dev->qin[(dev->qin_rd + dev->qin_cnt) % dev->chip->q_depth] = val & (dev->chip->scb_pages - 1);
                dev->qin_cnt++;
            }
            aic_seq_kick(dev);
            break;
        case QOUTFIFO:
            /* The sequencer posts a completion. */
            if (dev->qout_cnt < dev->chip->q_depth) {
                dev->qout[(dev->qout_rd + dev->qout_cnt) % dev->chip->q_depth] = val & (dev->chip->scb_pages - 1);
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
 *
 * That layout is not taken on trust. The AHA-2740's own option ROM
 * carries the program it downloads, assembled for this exact part, and
 * decoding it here is a test this could fail: at offset 384Ch there are
 * 349 instructions, ending where the next instruction's top byte reads
 * FFh rather than an opcode.
 *
 *   - every one of the 349 decodes to a defined opcode. A misplaced
 *     opcode field would strew opcode 7 and the other unassigned ones
 *     through the program;
 *   - 166 of the 168 branches land inside it, and the two that do not
 *     are both to 349 exactly, one past the last instruction. A
 *     misplaced address field would scatter targets over the whole 512
 *     the nine bits can reach, and about a third would overshoot;
 *   - and BMOV never appears, which is what this part should look like:
 *     the block move arrived with the command channel, and a 7770 has
 *     none. The driver's own loader knows it, rewriting BMOV for parts
 *     without AHC_CMD_CHAN.
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
aic_seq_push(aic7xxx_t *dev, uint16_t addr)
{
    dev->stack[dev->sp & 3] = addr;
    dev->sp                 = (dev->sp + 1) & 3;
}

static uint16_t
aic_seq_pop(aic7xxx_t *dev)
{
    dev->sp = (dev->sp - 1) & 3;
    return dev->stack[dev->sp & 3];
}

/* The arithmetic operations set both flags. */
static void
aic_seq_flags(aic7xxx_t *dev, uint8_t result, int carry)
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
aic_seq_flags_logic(aic7xxx_t *dev, uint8_t result)
{
    dev->flags &= CARRY;
    if (result == 0)
        dev->flags |= ZERO;
}

/* The shift control byte of a ROL: the low four bits are how far to
   rotate left, the high four how many bits to then clear, and bit 3
   says which end they are cleared from. That is how the assembler
   builds shl, shr, rol and ror out of the one instruction. */
/* Opcode 5 is the whole shift and rotate family, and the immediate says
   which: the low nibble is how far to rotate left, bit 3 of it also
   marking a rightward shift, and the high nibble how many bits the mask
   keeps. A right shift by N is therefore a rotate of (8-N) | 8 with N in
   the high nibble.

   Two firmwares agree on that and on nothing else, which is what makes it
   a check. Linux's writes "shr A,4,SAVED_TCL" to lift the target out of
   the high nibble of a TCL; the AHA-2740's ROM does the same job as raw
   encoding, its one and only opcode 5 being ROL SINDEX, SCB[1], 4Ch. Put
   N=4 through the rule above and the immediate is (8-4)|8 = Ch low, 4
   high -- 4Ch, the byte in the ROM. Decoded here that is a rotate left of
   four masked to 0Fh, and for eight bits that is exactly a shift right of
   four.

   The instruction after it in the ROM tests bit 3 of the same SCB byte,
   which is the channel bit of a TCL, so the surrounding code agrees about
   what it was reading too. */
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

/* ---- polling loops ------------------------------------------------------ */

/* Firmware waits for work in loops that look at a few status bits and
   scratch bytes and change nothing. Windows 2000's reads SSTAT0 twice,
   SCSISEQ, sixteen scratch bytes through SINDIR and two queue positions:
   forty-one instructions a pass, some ten million instructions a second
   of nothing for as long as the bus is quiet.

   One pass that leaves every register it wrote as it found it, and reads
   nothing that a read changes, proves that the next pass will be the same
   for as long as what it read stays the same. So the sequencer parks at
   the top of such a loop and, at each of its timer's ticks, looks at what
   the pass read instead of running it again; the first difference, any
   host access, or anything that kicks it, and it runs again, owed the
   instructions of the time it was parked for. The one thing parking shows
   is where in the loop it stopped, which the host could only see with the
   sequencer paused, and there it is somewhere in the loop either way. */

/* A read with no side effect, of something the part holds. */
static int
aic_loop_plain_read(uint8_t addr)
{
    if ((addr >= SRAM_BASE) && (addr < 0x60))
        return 1;
    if ((addr >= SCB_BASE) && (addr < (SCB_BASE + SCB_SIZE)))
        return 1;
    switch (addr) {
        case SCSISEQ:
        case SXFRCTL0:
        case SXFRCTL1:
        case SCSISIG:
        case SCSIID:
        case SSTAT0:
        case SSTAT1:
        case SSTAT2:
        case SSTAT3:
        case SIMODE0:
        case SIMODE1:
        case SBLKCTL:
        case SEQCTL:
        case ACCUM:
        case SINDEX:
        case DINDEX:
        case ALLONES:
        case ALLZEROS:
        case FLAGS:
        case FUNCTION1:
        case SCBPTR:
        case INTSTAT:
        case ERROR:
        case DFCNTRL:
        case DFSTATUS:
        case QINCNT:
        case QOUTCNT:
            return 1;
        default:
            return 0;
    }
}

/* A write that only stores. */
static int
aic_loop_plain_write(uint8_t addr)
{
    if ((addr >= SRAM_BASE) && (addr < 0x60))
        return 1;
    if ((addr >= SCB_BASE) && (addr < (SCB_BASE + SCB_SIZE)))
        return 1;
    switch (addr) {
        case ACCUM:
        case SINDEX:
        case DINDEX:
        case ALLZEROS:
        case FUNCTION1:
        case SCBPTR:
            return 1;
        default:
            return 0;
    }
}

/* The sequencer's own registers: a pass is compared on them whole. */
static int
aic_loop_own(uint8_t addr)
{
    switch (addr) {
        case ACCUM:
        case SINDEX:
        case DINDEX:
        case ALLONES:
        case ALLZEROS:
        case FLAGS:
        case FUNCTION1:
        case SCBPTR:
            return 1;
        default:
            return 0;
    }
}

/* The same place: an SCB byte is one per SCB page. */
static int
aic_loop_same_place(uint8_t addr, uint8_t scbptr, uint8_t e_addr, uint8_t e_scbptr)
{
    if (addr != e_addr)
        return 0;
    return (addr < SCB_BASE) || (addr >= (SCB_BASE + SCB_SIZE)) || (scbptr == e_scbptr);
}

static uint8_t
aic_loop_peek(aic7xxx_t *dev, uint8_t addr, uint8_t scbptr)
{
    uint8_t save = dev->scbptr;
    uint8_t v;

    dev->scbptr = scbptr;
    v           = aic_read(dev, addr, 1);
    dev->scbptr = save;
    return v;
}

static void
aic_loop_forget(aic7xxx_t *dev)
{
    dev->loop_head = AIC_LOOP_NONE;
}

/* A read by the pass: through SINDIR it is of what SINDEX pointed at.
   The first access to each place is what the pass depends on; a place it
   wrote before reading is its own. */
static void
aic_loop_read(aic7xxx_t *dev, uint8_t addr, uint8_t sindex, uint8_t v)
{
    uint8_t eff;

    if (dev->loop_head == AIC_LOOP_NONE)
        return;
    eff = (addr == SINDIR) ? sindex : addr;
    if (!aic_loop_plain_read(eff)) {
        aic_loop_forget(dev);
        return;
    }
    if (aic_loop_own(eff))
        return;
    for (uint8_t i = 0; i < dev->loop_nwr; i++) {
        if (aic_loop_same_place(eff, dev->scbptr, dev->loop_wr[i].addr, dev->loop_wr[i].scbptr))
            return;
    }
    for (uint8_t i = 0; i < dev->loop_nrd; i++) {
        if (aic_loop_same_place(eff, dev->scbptr, dev->loop_rd[i].addr, dev->loop_rd[i].scbptr))
            return;
    }
    if (dev->loop_nrd == AIC_LOOP_MAX) {
        aic_loop_forget(dev);
        return;
    }
    dev->loop_rd[dev->loop_nrd].addr   = eff;
    dev->loop_rd[dev->loop_nrd].scbptr = dev->scbptr;
    dev->loop_rd[dev->loop_nrd].val    = v;
    dev->loop_nrd++;
}

/* A write by the pass, before it lands: what was there, to compare with
   when the pass comes round. Through DINDIR it is to what DINDEX points at. */
static void
aic_loop_write(aic7xxx_t *dev, uint8_t addr, uint8_t val)
{
    uint8_t eff;

    if (dev->loop_head == AIC_LOOP_NONE)
        return;
    eff = (addr == DINDIR) ? dev->dindex : addr;
    /* SBLKCTL puts the other channel's registers in front, which the pass
       could not be compared across; a write that leaves it as it is does
       nothing at all. The AIC-7770's idle loop sets SELBUSB every pass,
       and on a wide board SELWIDE clears it again: without this that loop,
       eight million instructions a second, never parks. */
    if ((eff == SBLKCTL) && (aic_sblkctl_value(dev, val) == dev->sblkctl))
        return;
    if (!aic_loop_plain_write(eff)) {
        aic_loop_forget(dev);
        return;
    }
    if (aic_loop_own(eff))
        return;
    for (uint8_t i = 0; i < dev->loop_nwr; i++) {
        if (aic_loop_same_place(eff, dev->scbptr, dev->loop_wr[i].addr, dev->loop_wr[i].scbptr))
            return;
    }
    if (dev->loop_nwr == AIC_LOOP_MAX) {
        aic_loop_forget(dev);
        return;
    }
    dev->loop_wr[dev->loop_nwr].addr   = eff;
    dev->loop_wr[dev->loop_nwr].scbptr = dev->scbptr;
    dev->loop_wr[dev->loop_nwr].val    = aic_loop_peek(dev, eff, dev->scbptr);
    dev->loop_nwr++;
}

/* Everything the pass read still reads the same. */
static int
aic_loop_reads_hold(aic7xxx_t *dev)
{
    for (uint8_t i = 0; i < dev->loop_nrd; i++) {
        if (aic_loop_peek(dev, dev->loop_rd[i].addr, dev->loop_rd[i].scbptr) != dev->loop_rd[i].val)
            return 0;
    }
    return 1;
}

/* Nothing the sequencer counts or waits on by instruction is running. */
static int
aic_loop_quiet(aic7xxx_t *dev)
{
    return !dev->req_wait && !dev->host_wait && !dev->must_step && !(dev->sleepctl & (SLP1 | SLP0)) && !(dev->seqctl & STEP);
}

/* The pass came back to where it started: the state it began in, every
   place it wrote as it was, and everything it read unchanged. */
static int
aic_loop_same(aic7xxx_t *dev)
{
    if ((dev->accum != dev->loop_accum) || (dev->sindex != dev->loop_sindex) || (dev->dindex != dev->loop_dindex) ||
        (dev->flags != dev->loop_flags) || (dev->function1 != dev->loop_function1) || (dev->scbptr != dev->loop_scbptr) ||
        (dev->sp != dev->loop_sp) || (dev->intstat != dev->loop_intstat) ||
        memcmp(dev->stack, dev->loop_stack, sizeof(dev->stack)))
        return 0;
    for (uint8_t i = 0; i < dev->loop_nwr; i++) {
        if (aic_loop_peek(dev, dev->loop_wr[i].addr, dev->loop_wr[i].scbptr) != dev->loop_wr[i].val)
            return 0;
    }
    return aic_loop_reads_hold(dev) && aic_loop_quiet(dev);
}

/* A branch went backwards, to dev->pc: the top of a loop, perhaps. The
   pass being watched ends there if it began there -- parked, if it proved
   to change nothing -- and one begins there if none is being watched. A
   loop inside the pass is part of the pass. Answers whether it parked. */
static int
aic_loop_edge(aic7xxx_t *dev)
{
    if (dev->loop_head == dev->pc) {
        if (aic_loop_same(dev)) {
            dev->loop_parked = 1;
            dev->loop_failed = AIC_LOOP_NONE;
            return 1;
        }
        dev->loop_failed = dev->pc;
        dev->loop_skips  = 0;
        aic_loop_forget(dev);
        return 0;
    }
    if (dev->loop_head != AIC_LOOP_NONE)
        return 0;
    /* A loop inside a bigger one fails every time (it counts); the one
       round it gets its turn once the edge back to the other has been
       seen, and the one that failed gets another in time. */
    if ((dev->pc == dev->loop_failed) && (++dev->loop_skips < 64))
        return 0;

    dev->loop_failed    = AIC_LOOP_NONE;
    dev->loop_head      = dev->pc;
    dev->loop_insns     = 0;
    dev->loop_nrd       = 0;
    dev->loop_nwr       = 0;
    dev->loop_accum     = dev->accum;
    dev->loop_sindex    = dev->sindex;
    dev->loop_dindex    = dev->dindex;
    dev->loop_flags     = dev->flags;
    dev->loop_function1 = dev->function1;
    dev->loop_scbptr    = dev->scbptr;
    dev->loop_sp        = dev->sp;
    dev->loop_intstat   = dev->intstat;
    memcpy(dev->loop_stack, dev->stack, sizeof(dev->stack));
    return 0;
}

/* Something changed that the parked loop might see: it runs again. A
   pass being watched proves nothing either, once the host has reached in. */
static void
aic_loop_wake(aic7xxx_t *dev)
{
    aic_loop_forget(dev);
    if (!dev->loop_parked)
        return;
    dev->loop_parked = 0;
    /* The time it was parked went on passes of the loop: what it is owed
       starts now, as a spinning sequencer would come to the change within
       a pass of it. */
    dev->seq_idle   = 0;
    dev->seq_credit = 0.0;
    dev->seq_last   = aic_now_us();
    /* From inside its own tick (a REQ edge seen there kicks it), the tick
       runs it and sets the next one: stopping the timer here would clear
       timer_process()'s in_callback and cost both that reschedule and this
       one their place on the old period. From anywhere else: soon, not a
       period from now, stopped first so the start is a start. */
    if (dev->seq_timer.in_callback)
        return;
    timer_stop(&dev->seq_timer);
    timer_on_auto(&dev->seq_timer, 1.0);
}

/* The sequencer's own register accesses, watched for a polling loop's
   pass. */
static uint8_t
aic_seq_rd(aic7xxx_t *dev, uint8_t addr)
{
    uint8_t sindex = dev->sindex;
    uint8_t v      = aic_read(dev, addr, 1);

    aic_loop_read(dev, addr, sindex, v);
    return v;
}

static void
aic_seq_wr(aic7xxx_t *dev, uint8_t addr, uint8_t val)
{
    aic_loop_write(dev, addr, val);
    aic_write(dev, addr, val, 1);
}

static void
aic_seq_step(aic7xxx_t *dev)
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
        /* An address that decodes to nothing. What the part records for
           it is the descriptor's; the rest is every part's. */
        aic_fail(dev, dev->chip->bad_addr_err);
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

#ifdef ENABLE_AIC7XXX_LOG
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
        aic_log(dev->tag, "seq %03x: %08x op %x imm %02x src %02x dst %02x%s\n", dev->pc,
                insn, opcode, imm, src, dest, ret_bit ? " ret" : "");
    }

    dev->pc++;

    switch (opcode) {
        case OP_OR:
        case OP_AND:
        case OP_XOR:
        case OP_ADD:
        case OP_ADC:
            a = aic_seq_rd(dev, src);
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
            /* Arithmetic writes both flags; the logic operations write
               zero and leave carry alone. Which way round that goes is
               not a guess -- the firmware pins it, in set_1byte_addr:

                   add DINDIR, A, SINDIR
                   mov A, ARG_2
                   adc DINDIR, A, SINDIR
                   clr A
                   adc DINDIR, A, SINDIR
                   adc DINDIR, A, SINDIR ret

               a thirty-two bit add carrying across four bytes, with a
               move and a clear sitting between the links. Either one
               touching carry would break the chain, so neither does.

               The carry itself is the unsigned overflow out of eight
               bits, which two more idioms fix. A sixteen bit increment
               reads "clr A; add SG_NEXT[0],SG_SIZEOF; adc SG_NEXT[1],A"
               -- the second adding nothing but the carry, and doing it
               through the rule that an immediate of zero means the
               accumulator. Its counterpart subtracts by adding a two's
               complement, "add SG_NEXT[0],-SG_SIZEOF; adc SG_NEXT[1],
               0xff", which only borrows correctly if a carry out of the
               low byte is what makes FFh plus carry come back to zero.

               A move is one of the logic operations here, which is why
               it is safe between the links: block move arrived with the
               command channel and this part has none, so the driver
               rewrites it before download -- "convert the BMOV to a MOV
               (AND with an immediate of FF)". */
            if ((opcode == OP_ADD) || (opcode == OP_ADC))
                aic_seq_flags(dev, res, carry);
            else
                aic_seq_flags_logic(dev, res);
            aic_seq_wr(dev, dest, res);
            break;

        case OP_ROL:
            a   = aic_seq_rd(dev, src);
            res = aic_rotate(a, imm);
            /* The rotate is the one non-arithmetic operation that touches
               the carry: "For both rotates and shifts, the carry flag is
               set to the previous bit 7 or bit 0 value after each step of
               the move" -- bit 7 when the mask is right-justified, bit 0
               when it is left-justified (shift control bit 3). Firmware
               builds counts on it: Windows 98's driver does "shl HCNT0,1"
               and then two adcs to turn a scatter/gather length into a
               tally of 128-byte pieces, and with the carry left stale the
               tally was one short whenever bit 7 of the length was set.
               Reads then left 128 bytes in the target and the firmware
               reported a data overrun; writes delivered 128 bytes fewer
               than the drive was told, and said nothing. */
            {
                uint8_t steps = imm & 0x07;
                uint8_t v     = a;

                carry = !!(dev->flags & CARRY);
                for (uint8_t i = 0; i < steps; i++) {
                    carry = (imm & 0x08) ? (v & 0x01) : (v >> 7);
                    v     = (uint8_t) ((v << 1) | (v >> 7));
                }
            }
            aic_seq_flags(dev, res, carry);
            aic_seq_wr(dev, dest, res);
            break;

        case OP_BMOV:
            {
                /* The immediate is a byte count; source and destination both
                   walk forward. A count of one is what the assembler emits
                   for a plain mov. */
                uint8_t n = imm ? imm : 1;
                for (uint8_t i = 0; i < n; i++) {
                    res = aic_seq_rd(dev, (uint8_t) (src + i));
                    aic_seq_wr(dev, (uint8_t) (dest + i), res);
                }
                break;
            }

        case OP_JMP:
        case OP_CALL:
        case OP_JC:
        case OP_JNC:
            /* These also pass an argument: the source register ORed with
               the immediate lands in SINDEX, which is how the firmware
               hands a value to the routine it is calling.

               JC and JNC are the two opcodes nothing here has ever been
               seen to use, and it is not for want of looking. The only
               genuine sequencer image among the Adaptec ROMs on hand is
               the AHA-2740's, 349 instructions with twelve distinct
               opcodes and neither of these among them, and the Linux
               firmware does not write jc or jnc either.

               Two other ROMs look at first as though they do, and both
               are worth naming so the search is not repeated. The 2940UW
               images decode as thousands of instructions only because
               that part of the ROM is zero filled -- a zero word is a
               well formed OR -- and the sequencer holds 512 in any case.
               And the "U12 27C128 Microcode" that shows a JC and a JNC
               belongs to an AHA-154x, which has no sequencer of this kind
               at all; its branch targets do not stay inside the run,
               which is the tell.

               So the carry these test is verified, by the add and adc
               chains, and the branching around it is verified, by every
               other jump in the 2740's program. That the two of them put
               those together correctly is inference, not evidence. */
            a = aic_seq_rd(dev, src);
            aic_seq_wr(dev, SINDEX, (uint8_t) (a | imm));
            /* "Flags affected: Z" for every one of the four, from the OR
               that loads SINDEX; JC and JNC "do not alter the carry
               flag", and neither do the others. */
            aic_seq_flags_logic(dev, (uint8_t) (a | imm));
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
            a   = aic_seq_rd(dev, src);
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
            a   = aic_seq_rd(dev, src);
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
            /* ILLSADDR goes with it: that bit is "set when the Sequencer
               accesses an address which does not decode to a defined
               register, an Illegal Opcode is detected, ...". It is the
               AIC-7770's book, so take it from the descriptor -- the part
               whose bad address is ILLSADDR is the part it describes. */
            aic_fail(dev, ILLOPCODE | (dev->chip->bad_addr_err & ILLSADDR));
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
aic_seq_run(aic7xxx_t *dev)
{
    uint16_t last_pc = 0xffff;
    int      same    = 0;
    int      limit   = SEQ_BURST;
    int      stopped = 0;
    int      n;

    if (dev->in_seq)
        return;
    dev->in_seq = 1;

    /* Parked on a loop that changes nothing: it goes on doing nothing for
       as long as what it read reads the same (aic_loop_edge). */
    if (dev->loop_parked) {
        if (aic_loop_quiet(dev) && aic_loop_reads_hold(dev)) {
            dev->seq_last   = aic_now_us();
            dev->seq_credit = 0.0;
            dev->seq_idle   = 1;
            dev->in_seq     = 0;
            return;
        }
        dev->loop_parked = 0;
        aic_loop_forget(dev);
        dev->seq_idle = 0; /* owed the time since its last tick, as if it had run */
    }

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
                    aic_log(dev->tag, "seq: parked at %03x (sstat0 %02x sstat1 %02x "
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

        /* A pass being watched ends at a pause, an interrupt, or at its
           length; a backward branch may end it, or start one. */
        if (dev->loop_head != AIC_LOOP_NONE) {
            /* The breakpoint too: raised again on a bit already set, with
               PAUSEDIS, it changes nothing a pass could see, but a parked
               loop would never reach it. */
            if ((++dev->loop_insns > AIC_LOOP_INSNS) || aic_paused(dev) || (dev->intstat != dev->loop_intstat) ||
                (!(dev->brkaddr & 0x8000) && (dev->pc == (dev->brkaddr & 0x1ff))))
                aic_loop_forget(dev);
        }
        if ((dev->pc <= last_pc) && aic_loop_edge(dev)) {
            stopped = 1;
            n++;
            break;
        }

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
    aic7xxx_t *dev = (aic7xxx_t *) priv;

    /* Microseconds have passed: whatever was a few instructions away has
       happened. */
    dev->host_wait = 0;
    aic_tgt_req_due(dev);

    aic_select_pending(dev);

    aic_bitbucket(dev);
    aic_dma_host(dev);
    aic_dma_scsi(dev);
    aic_reselect_try(dev);

    if (!aic_paused(dev))
        aic_seq_run(dev);

    /* Keep the clock running while there is anything the sequencer could
       still be woken by. Parked on a loop with none of that under way,
       only the host can change what it reads, and every host access wakes
       it (aic_loop_wake). */
    if ((!aic_paused(dev) && !dev->loop_parked) || (dev->bus_state == BUS_BUSY) || dev->selecting || dev->qin_cnt ||
        (dev->dfcntrl & (SCSIEN | HDMAEN)) || aic_any_disconnected(dev))
        timer_on_auto(&dev->seq_timer, dev->asleep ? 50.0 : 10.0);
    else {
        /* Not re-arming is not the same as stopping. timer_on_auto() picks
           timer_advance_u64() over timer_set_delay_u64() whenever period is
           still positive, so a timer left merely un-armed is restarted from
           the expiry it had when it went quiet -- however long ago that was
           -- and from then on it can only creep forward one period per
           service. Clearing the period is what makes the next start a
           start. */
        timer_stop(&dev->seq_timer);
    }
}

static void
aic_seq_kick(aic7xxx_t *dev)
{
    dev->asleep = 0;
    if (dev->loop_parked) {
        aic_loop_wake(dev);
        return;
    }
    /* timer_is_on() asks whether a long period has been split, not whether
       the timer is running; a ten microsecond one never is. */
    if (!timer_is_enabled(&dev->seq_timer))
        timer_on_auto(&dev->seq_timer, 1.0);
}

/* ---- reset -------------------------------------------------------------- */

/* Scratch, the SCB array and the registers kept with them are RAM, and a
   chip reset does not touch RAM: CHIPRST "put[s] the device in a reset
   state for a maximum of 3 input clocks", and the scratch area is where
   the firmware keeps "configuration data which describes the system
   setup". What an option ROM leaves there is still there when a driver
   resets the part and reads it -- ASPI7DOS learns from it which disks the
   AHA-2740 BIOS already owns, and with it cleared took the BIOS's SCB for
   its own and swallowed every one of the BIOS's completions. Power-on and
   the machine's reset are what start RAM from nothing here. */
static void
aic_ram_clear(aic7xxx_t *dev)
{
    memset(dev->sram, 0, sizeof(dev->sram));
    memset(dev->scb, 0, sizeof(dev->scb));
    memset(dev->misc, 0, sizeof(dev->misc));
}

static void
aic_chip_reset(aic7xxx_t *dev)
{
    aic_log(dev->tag, "chip reset\n");
    dev->busl_reads = dev->rom_writes = dev->hcntrl_logs = dev->sig_logs = dev->err_logs = dev->scb_dumps = 0;
    dev->int_paused                                                                                       = 0;

    timer_stop(&dev->seq_timer);
    timer_stop(&dev->sel_timer);
    timer_stop(&dev->tgt_timer);
    timer_stop(&dev->req_timer);

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
    /* Out of reset the register reads back the board's own wiring: 08h
       for two buses, 02h for one wide one, 00h for a single narrow one.
       That is how the card's BIOS learns what it is fitted to. Answering
       0C0h instead left it unable to tell, and it went looking down a
       second bus that is not there. */
    /* A reset value is not a write mask: the mask says which bits the part
       has, this says what they come up holding. Deriving one from the
       other set AUTOFLUSHDIS on a part whose reset clears it. */
    /* The strap table's rows are DualBusses 08h, Channel A only 00h and
       Wide 02h. An AHA-2742A is one narrow connector, so it comes up on
       channel A -- SELBUSB clear -- even though the part behind it has a
       channel B for the firmware to go looking at. A board with two
       connectors would come up 08h, and that belongs to the board rather
       than to the chip. */
    dev->sblkctl = dev->chip->sblkctl_reset | (dev->wide ? SELWIDE : 0) | (dev->twin ? SELBUSB : 0);
    /* Both cells come up empty, and the file starts on whichever channel
       the straps named. */
    memset(dev->cell_save, 0, sizeof(dev->cell_save));
    dev->cell_live = (dev->sblkctl & SELBUSB) ? 1 : 0;
    dev->sel_ch = dev->cur_ch = dev->cell_live;
    dev->scsitest             = 0;
    dev->sleepctl             = 0;
    dev->must_step            = 0;
    dev->seq_idle             = 1;
    dev->seq_credit           = 0.0;
    dev->loop_head            = AIC_LOOP_NONE;
    dev->loop_failed          = AIC_LOOP_NONE;
    dev->loop_parked          = 0;

    dev->seqctl   = dev->chip->seqctl_reset;
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
    /* "This signal is cleared by RESDRV or CHIPRESET": the board comes
       out of a chip reset disabled, and the driver enables it again. */
    dev->bctl        = 0;
    /* HCNTRL comes up with PAUSE and CHIPRESETACK both set -- the data
       book gives (1) as the reset value of each -- and the acknowledgement
       "will remain set until explicitly cleared by a write to this
       register". A driver resetting the part polls for it, so reading it
       back clear leaves one waiting. */
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
    dev->qin_last = dev->qout_last = 0;

    /* The configuration chip is mapped over the top of scratch on an EISA
       board, so what it holds is there after a chip reset whatever scratch
       held. */
    if (dev->eisa) {
        memcpy(&dev->sram[SCSICONF - SRAM_BASE], dev->eisa_conf,
               sizeof(dev->eisa_conf));
        dev->sram[HA_274_BIOSGLOBAL - SRAM_BASE] = dev->eisa_global;
    }

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
/* The part on the AIR 54TDP, read off the board itself. One hundred and
   twenty-eight words with the sum of the first hundred and twenty-seven in
   the last, and no signature anywhere: the layout a card uses -- two
   thirty-two word halves, each with its own checksum -- is not what this
   chip has. The device words say include in the BIOS scan, disconnection
   allowed and synchronous at rate eight, and notably not wide. */
static void
aic_seeprom_onboard(uint16_t *nvr)
{
    uint16_t sum = 0;

    for (uint16_t i = 0; i < 128; i++)
        nvr[i] = 0xffff;

    for (uint8_t i = 0; i < 16; i++)
        nvr[i] = 0x0238;

    nvr[16]  = 0x18b6; /* bios_control */
    nvr[17]  = 0x005c; /* adapter_control */
    nvr[18]  = 0x2807; /* bus release time, and our SCSI ID is seven */
    nvr[19]  = 0x0010;
    nvr[20]  = 0xff00;
    nvr[126] = 0x00ff;

    for (uint16_t i = 0; i < 127; i++)
        sum = (uint16_t) (sum + nvr[i]);
    nvr[127] = sum;
}

/* The AHA-2940's, from the defaults table in its own BIOS v1.23 (a copy
   sits at 0A8Dh in the expanded image). One thirty-two word block with
   the sum of the first thirty-one in the last and no signature, the same
   layout as the part on the 54TDP. Targets 0 to 7 are included in the
   BIOS scan, synchronous at the fastest non-Ultra rate and allowed to
   disconnect; 8 to 15 add wide, and only a 2940W reaches them. Adapter
   control is automatic termination, both halves terminated, parity and
   a bus reset at start -- no Ultra, which the part has not got.

   Every one of the three BIOSes tells the two cards apart by the SELWIDE
   strap in SBLKCTL alone, and on a wide one sets the wide bit on all
   sixteen targets and the target count to sixteen before it saves. That
   is what a 2940W carries. */
static void
aic_seeprom_2940(uint16_t *nvr, int wide)
{
    uint16_t sum = 0;

    for (uint16_t i = 0; i < 128; i++)
        nvr[i] = 0xffff;

    for (uint8_t i = 0; i < 16; i++)
        nvr[i] = (wide || (i >= 8)) ? 0x0238 : 0x0218;

    nvr[16] = 0x18b6; /* bios_control */
    nvr[17] = 0x005d; /* adapter_control */
    nvr[18] = 0x2807; /* bus release time, and our SCSI ID is seven */
    nvr[19] = wide ? 0x0010 : 0x0008; /* how many targets */
    nvr[20] = 0xff00;
    nvr[30] = 0x00ff;

    for (uint8_t i = 0; i < 31; i++)
        sum = (uint16_t) (sum + nvr[i]);
    nvr[31] = sum;
}

static void
aic_seeprom_build(const aic7xxx_t *dev, uint16_t *nvr)
{
    if (dev->board == BOARD_7880) {
        aic_seeprom_onboard(nvr);
        return;
    }
    if ((dev->board == BOARD_2940) || (dev->board == BOARD_2940W)) {
        aic_seeprom_2940(nvr, dev->wide);
        return;
    }

    uint16_t sum;
    uint16_t flags;

    for (uint16_t i = 0; i < 128; i++)
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
            /* The signature word says "an Adaptec card wrote this". The
               chip on this motherboard has none, and its firmware does not
               look for one. */
            if (dev->board != BOARD_7880)
                w[30] = 0x0250;
        }

        sum = 0;
        for (uint8_t i = 0; i < 31; i++)
            sum = (uint16_t) (sum + w[i]);
        w[31] = sum;
    }
}

/* ---- the register window ------------------------------------------------ */

/* Put whichever of the two is showing into the image the rest of the
   emulator reads, so that the processor fetching code and a bus master
   fetching a command block both see the same thing. */
static void
aic_eisa_bios_overlay(aic7xxx_t *dev)
{
    if (!dev->has_bios || (dev->rom_size < 0x4000))
        return;

    memcpy(&dev->bios.rom[aic_bios_ramoff(dev)],
           (dev->eisa_conf[HA_274_BIOSCTRL - SCSICONF] & AIC_BIOS_RAMEN)
               ? dev->bios_ram
               : dev->bios_rom_top,
           AIC_BIOS_RAMSIZE);
}

static uint8_t
aic_eisa_rom_read(uint32_t addr, void *priv)
{
    const aic7xxx_t *dev = (const aic7xxx_t *) priv;

    return dev->bios.rom[(addr - dev->bios.mapping.base) & (dev->rom_size - 1)];
}

static uint16_t
aic_eisa_rom_readw(uint32_t addr, void *priv)
{
    return aic_eisa_rom_read(addr, priv) | ((uint16_t) aic_eisa_rom_read(addr + 1, priv) << 8);
}

static uint32_t
aic_eisa_rom_readl(uint32_t addr, void *priv)
{
    return aic_eisa_rom_readw(addr, priv) | ((uint32_t) aic_eisa_rom_readw(addr + 2, priv) << 16);
}

static void
aic_eisa_rom_write(uint32_t addr, uint8_t val, void *priv)
{
    aic7xxx_t *dev = (aic7xxx_t *) priv;
    uint32_t   off = (addr - dev->bios.mapping.base) & (dev->rom_size - 1);
    uint8_t    ctl = dev->eisa_conf[HA_274_BIOSCTRL - SCSICONF];

    /* Where the firmware thinks its overlay is. It writes a pattern and
       reads it back before it will install itself, so a window that
       answers at the wrong offset fails that test and says so. Bounded:
       the test is a few hundred bytes, a stray scan is not. */
    if (dev->rom_writes < 64) {
        dev->rom_writes++;
        aic_log(dev->tag, "option ROM write +%04x = %02x (overlay +%04x..%04x, "
                          "biosctrl %02x, %s)\n",
                off, val, aic_bios_ramoff(dev),
                aic_bios_ramoff(dev) + AIC_BIOS_RAMSIZE - 1, ctl,
                ((off >= aic_bios_ramoff(dev)) && (off < (aic_bios_ramoff(dev) + AIC_BIOS_RAMSIZE)) && !(ctl & AIC_BIOS_WRTPRT)) ? "taken" : "DROPPED");
    }

    if ((off < aic_bios_ramoff(dev)) || (off >= (aic_bios_ramoff(dev) + AIC_BIOS_RAMSIZE)) || (ctl & AIC_BIOS_WRTPRT))
        return;

    dev->bios_ram[off - aic_bios_ramoff(dev)] = val;
    if (ctl & AIC_BIOS_RAMEN)
        dev->bios.rom[off] = val;
}

static void
aic_eisa_rom_writew(uint32_t addr, uint16_t val, void *priv)
{
    aic_eisa_rom_write(addr, val & 0xff, priv);
    aic_eisa_rom_write(addr + 1, (val >> 8) & 0xff, priv);
}

static void
aic_eisa_rom_writel(uint32_t addr, uint32_t val, void *priv)
{
    aic_eisa_rom_writew(addr, val & 0xffff, priv);
    aic_eisa_rom_writew(addr + 2, (val >> 16) & 0xffff, priv);
}

/* HA_274_BIOSCTRL, the last of the five registers the configuration file
   asks the firmware to write. Bits 2:0 pick the sixteen kilobyte window
   and bits 5:4 are the mode, of which three means the BIOS is switched
   off. Nothing else decides where the ROM answers.

   The windows count up from 0CC000h by value, which is not the order
   !ADP7771.CFG prints them in -- it leads with D8000h because that is the
   default. Its eight choices, sorted by what they write:

       000 CC000h   001 D0000h   010 D4000h   011 D8000h
       100 DC000h   101 E0000h   110 E4000h   111 E8000h

   which is the 0CC000h plus n times 4000h below. Disabled is a ninth
   choice and writes the mode field instead, bits 5:4 both set. */
static void
aic_eisa_bios_remap(aic7xxx_t *dev)
{
    uint8_t  ctl = dev->eisa_conf[HA_274_BIOSCTRL - SCSICONF];
    uint32_t base;

    if (!dev->has_bios)
        return;

    /* Until the register has been written the board has not been through
       an EISA configuration and its address decoder is idle, so the window
       stays shut rather than landing somewhere nobody allocated. */
    if (!dev->bios_ctl_set || ((ctl & 0x30) == 0x30)) {
        mem_mapping_disable(&dev->bios.mapping);
        aic_log(dev->tag, "option ROM off (biosctrl %02x)\n", ctl);
        return;
    }

    base = 0xcc000 + (((uint32_t) ctl & 0x07) << 14);
    mem_mapping_set_addr(&dev->bios.mapping, base, dev->rom_size);
    mem_mapping_enable(&dev->bios.mapping);
    aic_log(dev->tag, "option ROM %u KB at %05x\n", dev->rom_size >> 10, base);
}

/* The option ROM is a part on the board, so it is always read in. The
   window size follows the image, since the family shipped 16, 32 and 64 KB
   parts. */
static void
aic_eisa_bios(aic7xxx_t *dev, const device_t *info)
{
    const char *rev = device_get_config_bios("bios_rev");
    const char *fn  = device_get_bios_file(info, rev, 0);
    uint32_t    size;
    FILE       *fp;

    if ((fn == NULL) || (fn[0] == '\0'))
        return;

    fp = rom_fopen((char *) fn, "rb");
    if (fp == NULL) {
        aic_log(dev->tag, "could not read %s\n", fn);
        return;
    }
    fseek(fp, 0, SEEK_END);
    size = (uint32_t) ftell(fp);
    fclose(fp);

    if (size <= 0x4000)
        size = 0x4000;
    else if (size <= 0x8000)
        size = 0x8000;
    else
        size = 0x10000;

    if (rom_init(&dev->bios, (char *) fn, 0xcc000, size, size - 1, 0,
                 MEM_MAPPING_EXTERNAL)
        < 0) {
        aic_log(dev->tag, "could not map %s\n", fn);
        return;
    }

    dev->has_bios = 1;
    dev->rom_size = size;

    mem_mapping_set_handler(&dev->bios.mapping,
                            aic_eisa_rom_read, aic_eisa_rom_readw,
                            aic_eisa_rom_readl, aic_eisa_rom_write,
                            aic_eisa_rom_writew, aic_eisa_rom_writel);
    mem_mapping_set_p(&dev->bios.mapping, dev);

    if (size >= 0x4000)
        memcpy(dev->bios_rom_top, &dev->bios.rom[aic_bios_ramoff(dev)],
               AIC_BIOS_RAMSIZE);

    aic_log(dev->tag, "option ROM %s, %u KB\n", fn, size >> 10);
    aic_eisa_bios_remap(dev);
}

/* The EISA part answers in the last of its slot's four ranges, at zC00,
   with the register number in the low byte. The four bytes at zC80 are the
   product identifier and the bus serves those itself. */
static uint8_t
aic_eisa_read(uint16_t port, void *priv)
{
    aic7xxx_t *dev = (aic7xxx_t *) priv;
    uint8_t    reg = (uint8_t) (port & 0xff);

    if ((port & 0x0f00) != 0x0c00)
        return 0xff;

    return aic_read(dev, reg, 0);
}

static void
aic_eisa_write(uint16_t port, uint8_t val, void *priv)
{
    aic7xxx_t *dev = (aic7xxx_t *) priv;
    uint8_t    reg = (uint8_t) (port & 0xff);

    if ((port & 0x0f00) != 0x0c00)
        return;

    aic_write(dev, reg, val, 0);
}

static uint8_t
aic_io_readb(uint16_t port, void *priv)
{
    aic7xxx_t *dev = (aic7xxx_t *) priv;
    uint8_t    reg = (uint8_t) (port - dev->io_base);
    uint8_t    ret = aic_read(dev, reg, 0);

    if (AIC7880_LOG_REGS) {
        aic_log(dev->tag, "  in  %02x -> %02x\n", reg, ret);
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
    aic7xxx_t *dev = (aic7xxx_t *) priv;
    uint8_t    reg = (uint8_t) (port - dev->io_base);

    if (AIC7880_LOG_REGS) {
        aic_log(dev->tag, "  out %02x <- %02x\n", reg, val);
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
    aic7xxx_t *dev = (aic7xxx_t *) priv;

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
    aic7xxx_t *dev = (aic7xxx_t *) priv;

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
aic_io_update(aic7xxx_t *dev)
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
aic_mem_update(aic7xxx_t *dev)
{
    uint32_t want = ((uint32_t) dev->pci_regs[0x17] << 24) | ((uint32_t) dev->pci_regs[0x16] << 16) | ((uint32_t) dev->pci_regs[0x15] << 8);

    mem_mapping_disable(&dev->mmio);
    dev->mem_base = want;
    if ((dev->pci_regs[0x04] & PCI_COMMAND_MEM) && (want != 0))
        mem_mapping_set_addr(&dev->mmio, want, 0x1000);
}

static void
aic_bios_update(aic7xxx_t *dev)
{
    uint32_t want;

    if (!dev->has_bios)
        return;
    want = ((uint32_t) dev->pci_regs[0x33] << 24) | ((uint32_t) dev->pci_regs[0x32] << 16) | ((uint32_t) dev->pci_regs[0x31] << 8);
    want &= ~(dev->rom_size - 1);

    mem_mapping_disable(&dev->bios.mapping);
    if ((dev->pci_regs[0x30] & 0x01) && (dev->pci_regs[0x04] & PCI_COMMAND_MEM) && (want != 0)) {
        aic_log(dev->tag, "bios: mapped at %08x\n", want);
        mem_mapping_set_addr(&dev->bios.mapping, want, dev->rom_size);
    } else {
        aic_log(dev->tag, "bios: unmapped (bar %08x en %i cmd %02x)\n", want,
                dev->pci_regs[0x30] & 1, dev->pci_regs[0x04]);
    }
}

static uint8_t
aic_rom_readb(uint32_t addr, void *priv)
{
    aic7xxx_t *dev = (aic7xxx_t *) priv;

    dev->rom_reads++;
    if (dev->rom_reads <= 48)
        aic_log(dev->tag, "bios: rom read #%u at +%04x = %02x\n", dev->rom_reads,
                addr & 0xffff, dev->bios.rom[addr & 0xffff]);
    else if ((dev->rom_reads % 4096) == 0) {
        aic_log(dev->tag, "bios: rom read #%u at +%04x\n", dev->rom_reads, addr & 0xffff);
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
    const aic7xxx_t *dev = (const aic7xxx_t *) priv;

    if (func > 0)
        return 0xff;
    if (AIC7880_LOG_REGS) {
        aic_log(dev->tag, "  pci in  %02x -> %02x\n", addr & 0xff, dev->pci_regs[addr & 0xff]);
    }
    return dev->pci_regs[addr & 0xff];
}

static void
aic_pci_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    aic7xxx_t *dev = (aic7xxx_t *) priv;

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
#define AHA2940UW_V121_ROM "roms/scsi/adaptec/aha2940uw_v121.bin"
#define AHA2940UW_V125_ROM "roms/scsi/adaptec/aha2940uw_v125.bin"
#define AHA2940UW_V134_ROM "roms/scsi/adaptec/aha2940uw_v134.bin"
#define AHA2940UW_V220_ROM "roms/scsi/adaptec/aha2940uw_v220.bin"
#define AHA2940_V111_ROM   "roms/scsi/adaptec/aha2940_v111.bin"
#define AHA2940_V116_ROM   "roms/scsi/adaptec/aha2940_v116.bin"
#define AHA2940_V123_ROM   "roms/scsi/adaptec/aha2940_v123.bin"
#define AHA2944UW_V220_ROM "roms/scsi/adaptec/aha2944uw_v220.bin"
#define AHA2740_V210_ROM   "roms/scsi/adaptec/aha2740_v210.bin"
#define AHA2740W_V211_ROM  "roms/scsi/adaptec/aha2740w.bin"
#define AHA2742A_V211_ROM  "roms/scsi/adaptec/aha2742a.bin"

static void
aic_reset(void *priv)
{
    aic7xxx_t *dev = (aic7xxx_t *) priv;

    aic_ram_clear(dev);
    aic_chip_reset(dev);
}

static void *
aic_init(const device_t *info)
{
    aic7xxx_t               *dev = (aic7xxx_t *) calloc(1, sizeof(aic7xxx_t));
    nmc93cxx_eeprom_params_t params;
    uint16_t                 nvr[128];
    char                     fn[1024] = { 0 };
    uint16_t                 devid;

    /* A SCSI controller however it got here -- the board's own, an EISA
       card or a PCI one: the status bar's disk and CD-ROM icons look for it. */
    other_scsi_present++;

    dev->board = info->local & 0xff;
    if (dev->board == BOARD_FROM_CONFIG) {
        dev->board = device_get_config_int("model");
        /* A 274x with its floppy controller fitted and jumpered on is the
           2742 of its kind: the same board, the same EISA ID and option
           ROM, and an N82077 beside the chip. The 2744W has none. */
        if (AIC_BOARD_EISA(dev->board) && device_get_config_int("floppy")) {
            switch (dev->board) {
                case BOARD_2740:
                    dev->board = BOARD_2742;
                    break;
                case BOARD_2740T:
                    dev->board = BOARD_2742T;
                    break;
                case BOARD_2740W:
                    dev->board = BOARD_2742W;
                    break;
                default:
                    break;
            }
        }
    }
    dev->wide  = (dev->board == BOARD_2940UW) || (dev->board == BOARD_2944UW) || (dev->board == BOARD_7880) || (dev->board == BOARD_2940W) || AIC_BOARD_WIDE(dev->board);
    /* An AHA-2740 is one narrow bus. Other members of the family strap the
       same chip for two buses or for one wide one, and on the AIC-7770
       those are exclusive: the wide connection takes channel B's data
       lines for the top half of channel A. */
    /* Two connectors. The data book's strap table gives SBLKCTL 08h for
       "DualBusses", 02h for one wide channel and 00h for a single narrow
       one, and the configuration utility writes a SCSICONF for each
       channel, so a board whose CFG declares both is wired for both. */
    /* The AIC-7770 always has two channels -- "The AIC-7770 contains two
       separate SCSI busses, SCSI Channel A and SCSI Channel B" -- and
       this board's option ROM knows it. Having walked channel A from
       address 0 to 6 it queues one more command with the channel bit set
       in the SCB, every time, whatever the configuration says. It is not
       asking whether there is a second connector; it is asking what is on
       channel B, and on a card with one connector the answer is nothing
       and the selection times out.

       So channel B exists here and is simply empty, which is what makes
       that last command answerable: the selection goes out on a bus with
       nothing on it and times out, on channel B's own SSTAT1, and the ROM
       carries on. That needs the register file to be per channel, which
       aic_cell_t and aic_cell_swap() now make it. */
    dev->twin = AIC_BOARD_TWIN(dev->board);
    dev->diff = AIC_BOARD_DIFF(dev->board);
    /* NOT a second connector, after all -- and the data book had it
       right. The v2.11 option ROM decides the channel count at init by
       reading SBLKCTL back and testing SELBUSB (x86 at 3636h): on a board
       strapped Channel A only the register comes up 00h, and the ROM
       takes its single channel path and never asks after channel B. A
       dual-bus strap here made the same ROM read 08h and walk a channel
       it had never configured, with no selection timer on it.
       The two cells are still modelled for a board that has both; what
       decides is the strap, and this card's is one connector. */

    dev->eisa = AIC_BOARD_EISA(dev->board);
    /* Which part this board is built on, before anything asks. */
    if (dev->eisa)
        dev->chip = &aic_chip_7770;
    else if ((dev->board == BOARD_2940) || (dev->board == BOARD_2940W))
        dev->chip = &aic_chip_7870;
    else
        dev->chip = &aic_chip_788x;
    dev->bus  = scsi_get_bus();
    /* What every line of this board's log will say it is. Set before
       anything else can log, and before the slot is known, so it names
       the part and the bus -- which is what tells two boards of the same
       part apart. */
    snprintf(dev->tag, sizeof(dev->tag), "%s/%u: ",
             dev->chip->name + 4, dev->bus);
    /* Channel B is a bus of its own, and the configuration utility gives it
       its own SCSICONF: !ADP7771.CFG declares IOPORT(3) as a word, so the
       firmware writes 5Ah and 5Bh together and the board comes up with both
       channels configured. */
    /* Channel B is the chip's whether or not the board brings it out,
       and it is a bus of its own either way: on a one connector board it
       is simply one with nothing on it, where a selection times out. */
    dev->bus_b = (dev->chip->sblkctl_mask & SELBUSB) ? scsi_get_bus() : dev->bus;

    scsi_bus_set_speed(dev->bus, 20000000.0);
    if (dev->bus_b != dev->bus)
        scsi_bus_set_speed(dev->bus_b, 20000000.0);

    /* The on-board part answers as the bare chip; the cards carry the
       adapter's own ID, which is how a driver tells them apart and how
       the option ROM's PCI data structure matches. */
    if (dev->board == BOARD_7880)
        devid = 0x8078;
    else if ((dev->board == BOARD_2940) || (dev->board == BOARD_2940W))
        devid = 0x7178;
    else if (dev->board == BOARD_2944UW)
        devid = 0x8478;
   else
        devid = 0x8178;

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
       takes as "go by the device ID". So does the older AHA-2940:
       read off a real one, 2Ch to 2Fh are all zero; the 2940W is the same
       card strapped wide. */
    if ((dev->board != BOARD_7880) && (dev->board != BOARD_2940) && (dev->board != BOARD_2940W)) {
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

    /* How long the device itself takes over a command: the seek and the
       rotation 86Box models for the drive, on every board. */
    dev->timed_dev = 1;

    if (dev->eisa) {
        uint8_t id[4];

        dev->eisa_slot = (uint8_t) device_get_config_int("slot");

        /* A 274x has no jumpers. Which interrupt it drives, where its
           option ROM answers and whether it answers at all are written
           into the configuration chip by the system firmware, replaying
           the records the configuration utility worked out. Until that
           happens the board is unconfigured, and this is what
           unconfigured looks like. */
        memset(dev->eisa_conf, 0, sizeof(dev->eisa_conf));

        /* Its own SCSI identifier is not among them, which this used to
           claim it was. !ADP7771.CFG declares zC5Ah as a word and then
           never writes it: the utility's whole repertoire is the
           interrupt, the bus release time, the FIFO threshold and the
           option ROM address, and there is no INIT for that port at all.

           The driver still expects to read it. On anything but a PCI
           card it only reads these -- "only set the SCSICONF and
           SCSICONF + 1 registers if we are a PCI card" -- and takes the
           adapter apart from what it finds: "temp_p->scsi_id =
           (temp_p->adapter_control >> 8) & HSCSIID", the terminators
           from TERM_ENB, and parity from ENSPCHK on its way into
           SXFRCTL1. Left at zero it would read an adapter at ID 0, with
           no terminators and no parity checking, which is not a card
           anyone shipped.

           So the board comes up holding what its own setup would have
           stored: identifier seven, terminators on, parity on. The bus
           reset bit stays clear, since nothing here reads it and asking
           for a reset that was never configured is worse than not. */
        dev->eisa_conf[SCSICONF - SCSICONF] = TERM_ENB | ENSPCHK | 7;

        /* And channel B's stays at zero unless the board has a channel B.
           An earlier pass here filled both in, which said there was a
           second channel, terminated and parity checked, with an adapter
           of its own at address seven. The option ROM believed it: it
           walked channel A, got its inquiry and test unit ready answered,
           and then asked once more with the channel bit set in the SCB
           for the channel it had been told about. Nothing could run that
           command, so it waited out its timeout and printed "Time-out
           failure during SCSI Inquiry command!" and "BIOS not
           installed!".

           An AHA-2740 is one narrow channel. Its SBLKCTL straps say so --
           00h, "Channel A only" in the reset table -- and this is the
           other half of saying so. A board that really is wired for two
           gets its second SCSICONF from the configuration utility, which
           is where a twin member of the family will pick it up. */
        /* And no second connector to describe. The utility writes a
           SCSICONF per channel on a board that has two; this one has one,
           so channel B's stays as an unconfigured channel reads. */

        dev->eisa_global  = 0x00;
        dev->bios_ctl_set = 0;
        dev->irq          = 0;

        aic_eisa_bios(dev, info);

        /* The four identifier bytes are the chip's own BID0 to BID3 as
           well as what the slot answers at zC80, and they have to be the
           same thing: on an AIC-7770 that range is the chip, not the bus
           logic beside it. */
        eisa_make_id(id, "ADP", 0x7771, 0);
        memcpy(dev->eisa_id, id, sizeof(dev->eisa_id));

        if (!eisa_add(dev->eisa_slot, id, aic_eisa_read, aic_eisa_write,
                      NULL, dev)) {
            aic_log(dev->tag, "EISA slot %i is not free\n", dev->eisa_slot);
            free(dev);
            return NULL;
        }
    }

    /* The card's own BIOS. Every one of these images is an AHA-2940
       Ultra/Ultra W BIOS whose PCI data structure names 9004:8178, as this
       card does, or an AHA-2940 BIOS naming 9004:7178; the PCI BIOS
       refuses to run one whose ID does not match.
       The images are shorter than the 64 KiB window the BAR asks for, and
       what is not covered by the file reads back as 0xff. */
    if ((dev->board != BOARD_7880) && !dev->eisa && device_get_config_int("bios")) {
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

    /* An EISA board has no serial EEPROM: the configuration chip at the
       top of scratch is where its settings live. */
    if (!dev->eisa) {
        aic_seeprom_build(dev, nvr);
        snprintf(fn, sizeof(fn), "nmc93cxx_eeprom_%s_%d.nvr", info->internal_name,
                 device_get_instance());
        /* The cards carry a 93C46. The chip on this motherboard has a 93C56,
           which is eight address bits rather than six, and a driver that
           clocks out the wrong number of them reads rubbish. */
        params.type            = (dev->board == BOARD_7880) ? NMC_93C56_x16_128
                                                            : NMC_93C46_x16_64;
        params.default_content = nvr;
        params.filename        = fn;
        dev->eeprom            = (nmc93cxx_eeprom_t *) device_add_inst_params(&nmc93cxx_device,
                                                                              device_get_instance(),
                                                                              &params);
        if (dev->eeprom == NULL) {
            free(dev);
            return NULL;
        }
    }

    mem_mapping_add(&dev->mmio, 0, 0, aic_mem_readb, aic_mem_readw, aic_mem_readl,
                    aic_mem_writeb, aic_mem_writew, aic_mem_writel, NULL,
                    MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_disable(&dev->mmio);

    timer_add(&dev->seq_timer, aic_seq_timer, dev, 0);
    timer_add(&dev->sel_timer, aic_select_done, dev, 0);
    timer_add(&dev->tgt_timer, aic_tgt_timer, dev, 0);
    timer_add(&dev->req_timer, aic_tgt_req_timer, dev, 0);

    aic_ram_clear(dev);
    aic_chip_reset(dev);

    /* The on-board part takes the slot the machine reserves for it: on
       the 54TDP that is device 8, and the BIOS knows the chip by that
       number -- its "OnBoard SCSI" switch, CMOS 64h bit 6, is applied to
       device 8 and nothing else, and its POST summary calls device 8
       "Onboard". Registered as an ordinary card the chip landed in the
       first free slot instead, device 10, where the BIOS saw an add-in
       card in "PCI Slot 3" and configured it whatever the switch said. */
    if (!dev->eisa)
        pci_add_card((dev->board == BOARD_7880) ? PCI_ADD_SCSI : PCI_ADD_NORMAL,
                     aic_pci_read, aic_pci_write, dev, &dev->pci_slot);

    /* The 2742s carry a floppy controller behind an enable jumper, off by
       default here because the machine's own controller answers at the
       same address: enable it only with the machine's disabled. */
    if (AIC_BOARD_FDC(dev->board) && device_get_config_int("floppy"))
        dev->fdc = device_add(&fdc_at_device);

    if (dev->twin) {
        aic_log(dev->tag, "board %i, %s, channel A on bus %i, channel B on "
                          "bus %i, sblkctl %02x\n",
                dev->board,
                dev->wide ? "wide" : "narrow", dev->bus, dev->bus_b,
                dev->sblkctl);
    } else {
        aic_log(dev->tag, "board %i, %s, one channel on bus %i, sblkctl "
                          "%02x\n",
                dev->board, dev->wide ? "wide" : "narrow",
                dev->bus, dev->sblkctl);
    }

    return dev;
}

static void
aic_close(void *priv)
{
    aic7xxx_t *dev = (aic7xxx_t *) priv;

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

static const device_config_t aic7770_config[] = {
    // clang-format off
    {
        .name           = "model",
        .description    = "Model",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = BOARD_2740,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "AHA-274x",                       .value = BOARD_2740  },
            { .description = "AHA-274xT (twin channel)",       .value = BOARD_2740T },
            { .description = "AHA-274xW (Wide)",               .value = BOARD_2740W },
            { .description = "AHA-2744W (Wide, differential)", .value = BOARD_2744W },
            { .description = ""                                                     }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_rev",
        .description    = "BIOS Revision",
        .type           = CONFIG_BIOS,
        .default_string = "v2_11_edd",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .bios           = {
            {
                .name          = "Version 2.10",
                .internal_name = "v2_10",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 16384,
                .files         = { AHA2740_V210_ROM, "" }
            },
            {
                .name          = "Version 2.11 EDD 1.1",
                .internal_name = "v2_11_edd",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 32768,
                .files         = { AHA2742A_V211_ROM, "" }
            },
            {
                .name          = "Version 2.11 EDD 1.1 (2740W dump)",
                .internal_name = "v2_11_edd_w",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 32768,
                .files         = { AHA2740W_V211_ROM, "" }
            },
            { .files_no = 0 }
        }
    },
    {
        .name           = "slot",
        .description    = "EISA slot",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Slot 1", .value = 1 },
            { .description = "Slot 2", .value = 2 },
            { .description = "Slot 3", .value = 3 },
            { .description = "Slot 4", .value = 4 },
            { .description = ""                   }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "floppy",
        .description    = "Floppy controller enabled (jumper)",
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

static const device_config_t aha2940u_config[] = {
    // clang-format off
    {
        .name           = "model",
        .description    = "Model",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = BOARD_2940U,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "AHA-2940U (Ultra)",       .value = BOARD_2940U  },
            { .description = "AHA-2940UW (Ultra Wide)", .value = BOARD_2940UW },
            { .description = ""                                               }
        },
        .bios           = { { 0 } }
    },
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
                .name          = "Version 1.21",
                .internal_name = "v1_21",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 32768,
                .files         = { AHA2940UW_V121_ROM, "" }
            },
            {
                .name          = "Version 1.23",
                .internal_name = "v1_23",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 65536,
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
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

static const device_config_t aha2940_config[] = {
    // clang-format off
    {
        .name           = "model",
        .description    = "Model",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = BOARD_2940,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "AHA-2940",         .value = BOARD_2940  },
            { .description = "AHA-2940W (Wide)", .value = BOARD_2940W },
            { .description = ""                                       }
        },
        .bios           = { { 0 } }
    },
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
        .default_string = "v1_23",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Version 1.11",
                .internal_name = "v1_11",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 32768,
                .files         = { AHA2940_V111_ROM, "" }
            },
            {
                .name          = "Version 1.16",
                .internal_name = "v1_16",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 32768,
                .files         = { AHA2940_V116_ROM, "" }
            },
            {
                .name          = "Version 1.23",
                .internal_name = "v1_23",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 32768,
                .files         = { AHA2940_V123_ROM, "" }
            },
            { .files_no = 0 }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

static const device_config_t aha2944uw_config[] = {
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
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Version 2.20.0",
                .internal_name = "v2_20_0",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 65536,
                .files         = { AHA2944UW_V220_ROM, "" }
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
    .name          = "Adaptec AIC-7880 (on-board)",
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

/* Until the models became options each board had an entry of its own. A
   configuration naming one of those becomes the entry that covers it, with
   the model it was; its section (under whichever name the old entry carried)
   moves to the new name, so the BIOS revision, slot and floppy jumper come
   along. A 2742's floppy jumper defaulted off as the 274x's does, so the
   model is all that has to be written. */
static const struct {
    const char *old_internal;
    const char *old_names[2];
    const char *new_internal;
    int         model;
} aic_migrations[] = {
    { "aha2740",   { "Adaptec AHA-2740"                              }, "aha274x",  BOARD_2740   },
    { "aha2742",   { "Adaptec AHA-2742"                              }, "aha274x",  BOARD_2740   },
    { "aha2740t",  { "Adaptec AHA-2740T"                             }, "aha274x",  BOARD_2740T  },
    { "aha2742t",  { "Adaptec AHA-2742T"                             }, "aha274x",  BOARD_2740T  },
    { "aha2740w",  { "Adaptec AHA-2740W"                             }, "aha274x",  BOARD_2740W  },
    { "aha2742w",  { "Adaptec AHA-2742W"                             }, "aha274x",  BOARD_2740W  },
    { "aha2744w",  { "Adaptec AHA-2744W"                             }, "aha274x",  BOARD_2744W  },
    { "aha2940w",  { "Adaptec AHA-2940W"                             }, "aha2940",  BOARD_2940W  },
    { "aha2940uw", { "Adaptec AHA-2940UW", "Adaptec AHA-2940 Ultra Wide" }, "aha2940u", BOARD_2940UW },
};

const char *
aic_config_migrate(const char *internal_name, int slot)
{
    const device_t *dev = NULL;
    char            new_sec[512];
    char            old_sec[512];

    for (size_t i = 0; i < (sizeof(aic_migrations) / sizeof(aic_migrations[0])); i++) {
        if (strcmp(internal_name, aic_migrations[i].old_internal))
            continue;

        if (!strcmp(aic_migrations[i].new_internal, "aha274x"))
            dev = &aha274x_device;
        else if (!strcmp(aic_migrations[i].new_internal, "aha2940"))
            dev = &aha2940_pci_device;
        else
            dev = &aha2940u_pci_device;

        /* Sections are the entry's name and its instance, the card's slot. */
        snprintf(new_sec, sizeof(new_sec), "%s #%i", dev->name, slot);
        if (config_find_section(new_sec) == NULL) {
            for (int n = 0; (n < 2) && (aic_migrations[i].old_names[n] != NULL); n++) {
                void *sec;

                snprintf(old_sec, sizeof(old_sec), "%s #%i", aic_migrations[i].old_names[n], slot);
                sec = config_find_section(old_sec);
                if (sec == NULL)
                    sec = config_find_section((char *) aic_migrations[i].old_names[n]);
                if (sec != NULL) {
                    config_rename_section(sec, new_sec);
                    break;
                }
            }
        }
        config_set_int(new_sec, "model", aic_migrations[i].model);

        return aic_migrations[i].new_internal;
    }

    return NULL;
}

/* The AIC-7770 boards: one EISA ID (ADP7771) and one option ROM, told apart
   by the chip's straps -- one narrow channel, two, one wide, one wide
   differential -- and by whether a floppy controller is fitted. */
const device_t aha274x_device = {
    .name          = "Adaptec AHA-274x (EISA)",
    .internal_name = "aha274x",
    .flags         = DEVICE_EISA,
    .local         = BOARD_FROM_CONFIG,
    .init          = aic_init,
    .close         = aic_close,
    .reset         = aic_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = aic7770_config
};

/* The AIC-7870 card, strapped narrow (AHA-2940) or wide (AHA-2940W). */
const device_t aha2940_pci_device = {
    .name          = "Adaptec AHA-2940 (AIC-7870)",
    .internal_name = "aha2940",
    .flags         = DEVICE_PCI,
    .local         = BOARD_FROM_CONFIG,
    .init          = aic_init,
    .close         = aic_close,
    .reset         = aic_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = aha2940_config
};

/* The AIC-7880 card, narrow (AHA-2940U) or wide (AHA-2940UW). */
const device_t aha2940u_pci_device = {
    .name          = "Adaptec AHA-2940 Ultra (AIC-7880)",
    .internal_name = "aha2940u",
    .flags         = DEVICE_PCI,
    .local         = BOARD_FROM_CONFIG,
    .init          = aic_init,
    .close         = aic_close,
    .reset         = aic_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = aha2940u_config
};

const device_t aha2944uw_pci_device = {
    .name          = "Adaptec AHA-2944UW",
    .internal_name = "aha2944uw",
    .flags         = DEVICE_PCI,
    .local         = BOARD_2944UW,
    .init          = aic_init,
    .close         = aic_close,
    .reset         = aic_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = aha2944uw_config
};
