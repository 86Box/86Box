/*
 * VARCem   Virtual ARchaeological Computer EMulator.
 *          An emulator of (mostly) x86-based PC systems and devices,
 *          using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *          spanning the era between 1981 and 1995.
 *
 *          Implementation of the PS/1 Model 2011 disk controller.
 *
 *          XTA is the acronym for 'XT-Attached', which was basically
 *          the XT-counterpart to what we know now as IDE (which is
 *          also named ATA - AT Attachment.)  The basic ideas was to
 *          put the actual drive controller electronics onto the drive
 *          itself, and have the host machine just talk to that using
 *          a simpe, standardized I/O path- hence the name IDE, for
 *          Integrated Drive Electronics.
 *
 *          In the ATA version of IDE, the programming interface of
 *          the IBM PC/AT (which used the Western Digitial 1002/1003
 *          controllers) was kept, and, so, ATA-IDE assumes a 16bit
 *          data path: it reads and writes 16bit words of data. The
 *          disk drives for this bus commonly have an 'A' suffix to
 *          identify them as 'ATBUS'.
 *
 *          In XTA-IDE, which is slightly older, the programming
 *          interface of the IBM PC/XT (which used the MFM controller
 *          from Xebec) was kept, and, so, it uses an 8bit data path.
 *          Disk drives for this bus commonly have the 'X' suffix to
 *          mark them as being for this XTBUS variant.
 *
 *          So, XTA and ATA try to do the same thing, but they use
 *          different ways to achive their goal.
 *
 *          Also, XTA is **not** the same as XTIDE.  XTIDE is a modern
 *          variant of ATA-IDE, but retro-fitted for use on 8bit XT
 *          systems: an extra register is used to deal with the extra
 *          data byte per transfer.  XTIDE uses regular IDE drives,
 *          and uses the regular ATA/IDE programming interface, just
 *          with the extra register.
 *
 * NOTE:    We should probably find a nicer way to integrate our Disk
 *          Type table with the main code, so the user can only select
 *          items from that list...
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Based on my earlier HD20 driver for the EuroPC.
 *          Thanks to Marco Bortolin for the help and feedback !!
 *
 *          Copyright 2017-2019 Fred N. van Kempen.
 *
 *          Redistribution and  use  in source  and binary forms, with
 *          or  without modification, are permitted  provided that the
 *          following conditions are met:
 *
 *          1. Redistributions of  source  code must retain the entire
 *             above notice, this list of conditions and the following
 *             disclaimer.
 *
 *          2. Redistributions in binary form must reproduce the above
 *             copyright  notice,  this list  of  conditions  and  the
 *             following disclaimer in  the documentation and/or other
 *             materials provided with the distribution.
 *
 *          3. Neither the  name of the copyright holder nor the names
 *             of  its  contributors may be used to endorse or promote
 *             products  derived from  this  software without specific
 *             prior written permission.
 *
 * THIS SOFTWARE  IS  PROVIDED BY THE  COPYRIGHT  HOLDERS AND CONTRIBUTORS
 * "AS IS" AND  ANY EXPRESS  OR  IMPLIED  WARRANTIES,  INCLUDING, BUT  NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE  ARE  DISCLAIMED. IN  NO  EVENT  SHALL THE COPYRIGHT
 * HOLDER OR  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL,  EXEMPLARY,  OR  CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE  GOODS OR SERVICES;  LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED  AND ON  ANY
 * THEORY OF  LIABILITY, WHETHER IN  CONTRACT, STRICT  LIABILITY, OR  TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING  IN ANY  WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/device.h>
#include <86box/hdc.h>
#include <86box/hdd.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/machine.h>

#define HDC_TIME      (50 * TIMER_USEC)
#define HDC_TYPE_USER 47 /* user drive type */

enum {
    STATE_IDLE = 0,
    STATE_RECV,
    STATE_RDATA,
    STATE_RDONE,
    STATE_SEND,
    STATE_SDATA,
    STATE_SDONE,
    STATE_FINIT,
    STATE_FDONE
};

/* Command values. These deviate from the XTA ones. */
#define CMD_READ_SECTORS  0x01 /* regular read-date */
#define CMD_READ_VERIFY   0x02 /* read for verify, no data */
#define CMD_READ_EXT      0x03 /* read extended (ecc) */
#define CMD_READ_ID       0x05 /* read ID mark on cyl */
#define CMD_RECALIBRATE   0x08 /* recalibrate to track0 */
#define CMD_WRITE_SECTORS 0x09 /* regular write-data */
#define CMD_WRITE_VERIFY  0x0a /* write-data with verify */
#define CMD_WRITE_EXT     0x0b /* write extended (ecc) */
#define CMD_FORMAT_DRIVE  0x0d /* format entire disk */
#define CMD_SEEK          0x0e /* seek */
#define CMD_FORMAT_TRACK  0x0f /* format one track */

/* Attachment Status register (reg 2R) values (IBM PS/1 2011.) */
#define ASR_TX_EN    0x01 /* transfer enable */
#define ASR_INT_REQ  0x02 /* interrupt request */
#define ASR_BUSY     0x04 /* busy */
#define ASR_DIR      0x08 /* direction */
#define ASR_DATA_REQ 0x10 /* data request */

/* Attachment Control register (2W) values (IBM PS/1 2011.) */
#define ACR_DMA_EN 0x01 /* DMA enable */
#define ACR_INT_EN 0x02 /* interrupt enable */
#define ACR_RESET  0x80 /* reset */

/* Interrupt Status register (4R) values (IBM PS/1 2011.) */
#define ISR_EQUIP_CHECK 0x01 /* internal hardware error */
#define ISR_ERP_INVOKED 0x02 /* error recovery invoked */
#define ISR_CMD_REJECT  0x20 /* command reject */
#define ISR_INVALID_CMD 0x40 /* invalid command */
#define ISR_TERMINATION 0x80 /* termination error */

/* Attention register (4W) values (IBM PS/1 2011.) */
#define ATT_DATA 0x10 /* data request */
#define ATT_SSB  0x20 /* sense summary block */
#define ATT_CSB  0x40 /* command specify block */
#define ATT_CCB  0x80 /* command control block */

/*
 * Define the Sense Summary Block.
 *
 * The sense summary block contains the current status of the
 * drive. The information in the summary block is updated after
 * each command is completed, after an error, or before the
 * block is transferred.
 */
#pragma pack(push, 1)
typedef struct {
    /* Status byte 0. */
    uint8_t track_0  : 1, /* T0            */
        mbz1         : 1, /* 0            */
        mbz2         : 1, /* 0            */
        cylinder_err : 1, /* CE            */
        write_fault  : 1, /* WF            */
        mbz3         : 1, /* 0            */
        seek_end     : 1, /* SE            */
        not_ready    : 1; /* NR            */

    /* Status byte 1. */
    uint8_t id_not_found : 1, /* ID            */
        mbz4             : 1, /* 0            */
        mbz5             : 1, /* 0            */
        wrong_cyl        : 1, /* WC            */
        all_bit_set      : 1, /* BT            */
        mark_not_found   : 1, /* AM            */
        ecc_crc_err      : 1, /* ET            */
        ecc_crc_field    : 1; /* EF            */

    /* Status byte 2. */
    uint8_t headsel_state : 4, /* headsel state[4]    */
        defective_sector  : 1, /* DS            */
        retried_ok        : 1, /* RG            */
        need_reset        : 1, /* RR            */
#if 1
        valid : 1; /* 0 (abused as VALID)    */
#else
        mbz6 : 1; /* 0            */
#endif

    /* Most recent ID field seen. */
    uint8_t last_cyl_low;  /* Cyl_Low[8]        */
    uint8_t last_head : 4, /* HD[4]        */
        mbz7          : 1, /* 0            */
        last_cyl_high : 2, /* Cyl_high[2]        */
        last_def_sect : 1; /* DS            */
    uint8_t last_sect;     /* Sect[8]        */

    uint8_t sect_size; /* Size[8] = 02        */

    /* Current position. */
    uint8_t curr_cyl_high : 2, /* Cyl_High_[2]        */
        mbz8              : 1, /* 0            */
        mbz9              : 1, /* 0            */
        curr_head         : 4; /* HD_2[4]        */
    uint8_t curr_cyl_low;      /* Cyl_Low_2[8]        */

    uint8_t sect_corr; /* sectors corrected    */

    uint8_t retries; /* retries        */

    /*
     * This byte shows the progress of the controller through the
     * last command.  It allows the system to monitor the controller
     * and determine if a reset is needed.  When the transfer of the
     * control block is started, the value is set to hex 00.  The
     * progress indicated by this byte is:
     *
     * 1.  Set to hex 01 after the control block is successfully
     *     transferred.
     *
     * 2.  Set to hex 02 when the command is valid and the drive
     *     is ready.
     *
     * 3.  Set to hex 03 when the head is in the correct track.
     *     The most-significant four bits (high nibble) are then
     *     used to indicate the successful stages of the data
     *     transfer:
     *
     *     Bit 7    A sector was transferred between the system
     *                and the sector buffer.
     *
     *     Bit 6    A sector was transferred between the controller
     *              and the sector buffer.
     *
     *     Bit 5    An error was detected and error recovery
     *              procedures have been started.
     *
     *     Bit 4    The controller has completed the operation
     *              and is now not busy.
     *
     * 4.  When the transfer is complete, the low nibble equals hex 4
     *     and the high nibble is unchanged.
     */
    uint8_t cmd_syndrome; /* command syndrome    */

    uint8_t drive_type; /* drive type        */

    uint8_t rsvd; /* reserved byte    */
} ssb_t;
#pragma pack(pop)

/*
 * Define the Format Control Block.
 *
 * The format control block (FCB) specifies the ID data used
 * in formatting the track.  It is used by the Format Track
 * and Format Disk commands and contains five bytes for each
 * sector formatted on that track.
 *
 * When the Format Disk command is used, the control block
 * contains the sector information of all sectors for head 0,
 * cylinder 0.  The drive will use the same block to format
 * the rest of the disk and automatically increment the head
 * number and cylinder number for the remaining tracks.  The
 * sector numbers, sector size, and the fill byte will be
 * the same for each track.
 *
 * The drive formats the sector IDs on the disk in the same
 * order as they are specified in the control block.
 * Therefore, sector interleaving is accomplished by filling
 * in the control block with the desired interleave.
 *
 * For example, when formatting 17 sectors per track with an
 * interleave of 2, the control block has the first 5 bytes
 * with a sector number of 1, the second with a sector number
 * of 10, the third with a sector number of 2, and continuing
 * until all 17 sectors for that track are defined.
 *
 * The format for the format control block is described in
 * the following.  The five bytes are repeated for each
 * sector on the track.  The control block must contain an
 * even number of bytes.  If an odd number of sectors are
 * being formatted, an additional byte is sent with all
 * bits 0.
 */
#pragma pack(push, 1)
typedef struct {
    uint8_t cyl_high     : 2, /* cylinder [9:8] bits    */
        defective_sector : 1, /* DS            */
        mbz1             : 1, /* 0            */
        head             : 4; /* head number        */

    uint8_t cyl_low; /* cylinder [7:0] bits    */

    uint8_t sector; /* sector number    */

    uint8_t mbz2 : 1, /* 0            */
        mbo      : 1, /* 1            */
        mbz3     : 6; /* 000000        */

    uint8_t fill; /* filler byte        */
} fcb_t;
#pragma pack(pop)

/*
 * Define the Command Control Block.
 *
 * The system specifies the operation by sending the 6-byte
 * command control block to the controller. It can be sent
 * through a DMA or PIO operation.
 */
#pragma pack(push, 1)
typedef struct {
    uint8_t ec_p     : 1, /* EC/P (ecc/park)    */
        mbz1         : 1, /* 0            */
        auto_seek    : 1, /* AS (auto-seek)    */
        no_data      : 1, /* ND (no data)        */
        cmd          : 4; /* command code[4]    */

    uint8_t cyl_high : 2, /* cylinder [9:8] bits    */
        mbz2         : 2, /* 00            */
        head         : 4; /* head number        */

    uint8_t cyl_low; /* cylinder [7:0] bits    */

    uint8_t sector; /* sector number    */

    uint8_t mbz3 : 1, /* 0            */
        mbo1     : 1, /* 1            */
        mbz4     : 6; /* 000000        */

    uint8_t count; /* blk count/interleave    */
} ccb_t;
#pragma pack(pop)

/* Define the hard drive geometry table. */
typedef struct {
    uint16_t cyl;
    uint8_t  hpc;
    uint8_t  spt;
    int16_t  wpc;
    int16_t  lz;
} geom_t;

/* Define an attached drive. */
typedef struct {
    int8_t id,   /* drive ID on bus */
        present, /* drive is present */
        hdd_num, /* index to global disk table */
        type;    /* drive type ID */

    uint16_t cur_cyl; /* last known position of heads */

    uint8_t spt, /* active drive parameters */
        hpc;
    uint16_t tracks;

    uint8_t cfg_spt, /* configured drive parameters */
        cfg_hpc;
    uint16_t cfg_tracks;
} drive_t;

typedef struct {
    uint16_t base; /* controller base I/O address */
    int8_t   irq;  /* controller IRQ channel */
    int8_t   dma;  /* controller DMA channel */

    /* Registers. */
    uint8_t attn, /* ATTENTION register */
        ctrl,     /* Control register (ACR) */
        status,   /* Status register (ASR) */
        intstat;  /* Interrupt Status register (ISR) */

    uint8_t *reg_91; /* handle to system board's register 0x91 */

    /* Controller state. */
    uint64_t   callback;
    pc_timer_t timer;
    int8_t     state, /* controller state */
        reset;        /* reset state counter */

    /* Data transfer. */
    int16_t buf_idx, /* buffer index and pointer */
        buf_len;
    uint8_t *buf_ptr;

    /* Current operation parameters. */
    ssb_t    ssb;   /* sense block */
    ccb_t    ccb;   /* command control block */
    uint16_t track; /* requested track# */
    uint8_t  head,  /* requested head# */
        sector;     /* requested sector# */
    int count;      /* requested sector count */

    drive_t drives[XTA_NUM]; /* the attached drive(s) */

    uint8_t data[512];       /* data buffer */
    uint8_t sector_buf[512]; /* sector buffer */
} hdc_t;

/*
 * IBM hard drive types 1-44.
 *
 * We need these to translate the selected disk's
 * geometry back to a valid type through the SSB.
 *
 *     Cyl.   Head    Sect.       Write   Land
 *                                p-comp  Zone
 */
static const geom_t ibm_type_table[] = {
  // clang-format off
    {    0,     0,       0,          0,      0    },    /*  0    (none)    */
    {  306,     4,      17,        128,    305    },    /*  1    10 MB    */
    {  615,     4,      17,        300,    615    },    /*  2    20 MB    */
    {  615,     6,      17,        300,    615    },    /*  3    31 MB    */
    {  940,     8,      17,        512,    940    },    /*  4    62 MB    */
    {  940,     6,      17,        512,    940    },    /*  5    47 MB    */
    {  615,     4,      17,         -1,    615    },    /*  6    20 MB    */
    {  462,     8,      17,        256,    511    },    /*  7    31 MB    */
    {  733,     5,      17,         -1,    733    },    /*  8    30 MB    */
    {  900,    15,      17,         -1,    901    },    /*  9    112 MB    */
    {  820,     3,      17,         -1,    820    },    /* 10    20 MB    */
    {  855,     5,      17,         -1,    855    },    /* 11    35 MB    */
    {  855,     7,      17,         -1,    855    },    /* 12    50 MB    */
    {  306,     8,      17,        128,    319    },    /* 13    20 MB    */
    {  733,     7,      17,         -1,    733    },    /* 14    43 MB    */
    {    0,     0,       0,          0,      0    },    /* 15    (rsvd)    */
    {  612,     4,      17,          0,    663    },    /* 16    20 MB    */
    {  977,     5,      17,        300,    977    },    /* 17    41 MB    */
    {  977,     7,      17,         -1,    977    },    /* 18    57 MB    */
    { 1024,     7,      17,        512,   1023    },    /* 19    59 MB    */
    {  733,     5,      17,        300,    732    },    /* 20    30 MB    */
    {  733,     7,      17,        300,    732    },    /* 21    43 MB    */
    {  733,     5,      17,        300,    733    },    /* 22    30 MB    */
    {  306,     4,      17,          0,    336    },    /* 23    10 MB    */
    {  612,     4,      17,        305,    663    },    /* 24    20 MB    */
    {  306,     4,      17,         -1,    340    },    /* 25    10 MB    */
    {  612,     4,      17,         -1,    670    },    /* 26    20 MB    */
    {  698,     7,      17,        300,    732    },    /* 27    41 MB    */
    {  976,     5,      17,        488,    977    },    /* 28    40 MB    */
    {  306,     4,      17,          0,    340    },    /* 29    10 MB    */
    {  611,     4,      17,        306,    663    },    /* 30    20 MB    */
    {  732,     7,      17,        300,    732    },    /* 31    43 MB    */
    { 1023,     5,      17,         -1,   1023    },    /* 32    42 MB    */
    {  614,     4,      25,         -1,    663    },    /* 33    30 MB    */
    {  775,     2,      27,         -1,    900    },    /* 34    20 MB    */
    {  921,     2,      33,         -1,   1000    },    /* 35    30 MB *    */
    {  402,     4,      26,         -1,    460    },    /* 36    20 MB    */
    {  580,     6,      26,         -1,    640    },    /* 37    44 MB    */
    {  845,     2,      36,         -1,   1023    },    /* 38    30 MB *    */
    {  769,     3,      36,         -1,   1023    },    /* 39    41 MB *    */
    {  531,     4,      39,         -1,    532    },    /* 40    40 MB    */
    {  577,     2,      36,         -1,   1023    },    /* 41    20 MB    */
    {  654,     2,      32,         -1,    674    },    /* 42    20 MB    */
    {  923,     5,      36,         -1,   1023    },    /* 43    81 MB    */
    {  531,     8,      39,         -1,    532    }    /* 44    81 MB    */
  // clang-format on
};

#ifdef ENABLE_PS1_HDC_LOG
int ps1_hdc_do_log = ENABLE_PS1_HDC_LOG;

static void
ps1_hdc_log(const char *fmt, ...)
{
    va_list ap;

    if (ps1_hdc_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ps1_hdc_log(fmt, ...)
#endif

static void
hdc_set_callback(hdc_t *dev, uint64_t callback)
{
    if (!dev) {
        return;
    }

    if (callback) {
        dev->callback = callback;
        timer_set_delay_u64(&dev->timer, dev->callback);
    } else {
        dev->callback = 0;
        timer_disable(&dev->timer);
    }
}

/* FIXME: we should use the disk/hdd_table.c code with custom tables! */
static int
ibm_drive_type(drive_t *drive)
{
    const geom_t *ptr;
    int           i;

    for (i = 0; i < (sizeof(ibm_type_table) / sizeof(geom_t)); i++) {
        ptr = &ibm_type_table[i];
        if ((drive->tracks == ptr->cyl) && (drive->hpc == ptr->hpc) && (drive->spt == ptr->spt))
            return (i);
    }

    return (HDC_TYPE_USER);
}

static void
set_intr(hdc_t *dev, int raise)
{
    if (raise) {
        dev->status |= ASR_INT_REQ;
        if (dev->ctrl & ACR_INT_EN)
            picint(1 << dev->irq);
    } else {
        dev->status &= ~ASR_INT_REQ;
        picintc(1 << dev->irq);
    }
}

/* Get the logical (block) address of a CHS triplet. */
static int
get_sector(hdc_t *dev, drive_t *drive, off64_t *addr)
{
    if (drive->cur_cyl != dev->track) {
        ps1_hdc_log("HDC: get_sector: wrong cylinder %d/%d\n",
                    drive->cur_cyl, dev->track);
        dev->ssb.wrong_cyl = 1;
        return (1);
    }

    if (dev->head >= drive->hpc) {
        ps1_hdc_log("HDC: get_sector: past end of heads\n");
        dev->ssb.cylinder_err = 1;
        return (1);
    }

    if (dev->sector > drive->spt) {
        ps1_hdc_log("HDC: get_sector: past end of sectors\n");
        dev->ssb.mark_not_found = 1;
        return (1);
    }

    /* Calculate logical address (block number) of desired sector. */
    *addr = ((((off64_t) dev->track * drive->hpc) + dev->head) * drive->spt) + dev->sector - 1;

    return (0);
}

static void
next_sector(hdc_t *dev, drive_t *drive)
{
    if (++dev->sector > drive->spt) {
        dev->sector = 1;
        if (++dev->head >= drive->hpc) {
            dev->head = 0;
            dev->track++;
            if (++drive->cur_cyl >= drive->tracks) {
                drive->cur_cyl        = drive->tracks - 1;
                dev->ssb.cylinder_err = 1;
            }
        }
    }
}

/* Finish up. Repeated all over, so a function it is now. */
static void
do_finish(hdc_t *dev)
{
    dev->state = STATE_IDLE;

    dev->attn &= ~(ATT_CCB | ATT_DATA);

    dev->status = 0x00;

    set_intr(dev, 1);
}

/* Seek to a cylinder. */
static int
do_seek(hdc_t *dev, drive_t *drive, uint16_t cyl)
{
    if (cyl >= drive->tracks) {
        dev->ssb.cylinder_err = 1;
        return (1);
    }

    dev->track     = cyl;
    drive->cur_cyl = dev->track;

    return (0);
}

/* Format a track or an entire drive. */
static void
do_format(hdc_t *dev, drive_t *drive, ccb_t *ccb)
{
    int     start_cyl, end_cyl;
    int     intr = 0, val;
    off64_t addr;
#if 0
    fcb_t *fcb;
#endif

    /* Get the parameters from the CCB. */
    if (ccb->cmd == CMD_FORMAT_DRIVE) {
        start_cyl = 0;
        end_cyl   = drive->tracks;
    } else {
        start_cyl = (ccb->cyl_low | (ccb->cyl_high << 8));
        end_cyl   = start_cyl + 1;
    }

    switch (dev->state) {
        case STATE_IDLE:
            /* Ready to transfer the FCB data in. */
            dev->state   = STATE_RDATA;
            dev->buf_idx = 0;
            dev->buf_ptr = dev->data;
            dev->buf_len = ccb->count * sizeof(fcb_t);
            if (dev->buf_len & 1)
                dev->buf_len++; /* must be even */

                /* Enable for PIO or DMA, as needed. */
#if NOT_USED
            if (dev->ctrl & ACR_DMA_EN)
                hdc_set_callback(dev, HDC_TIME);
            else
#endif
                dev->status |= ASR_DATA_REQ;
            break;

        case STATE_RDATA:
            /* Perform DMA. */
            while (dev->buf_idx < dev->buf_len) {
                val = dma_channel_read(dev->dma);
                if (val == DMA_NODATA) {
                    dev->intstat |= ISR_EQUIP_CHECK;
                    dev->ssb.need_reset = 1;
                    intr                = 1;
                    break;
                }
                dev->buf_ptr[dev->buf_idx] = (val & 0xff);
                dev->buf_idx++;
            }
            dev->state = STATE_RDONE;
            hdc_set_callback(dev, HDC_TIME);
            break;

        case STATE_RDONE:
            if (!(dev->ctrl & ACR_DMA_EN))
                dev->status &= ~ASR_DATA_REQ;

                /* Point to the FCB we got. */
#if 0
        fcb = (fcb_t *)dev->data;
#endif
            dev->state = STATE_FINIT;
            /*FALLTHROUGH*/

        case STATE_FINIT:
do_fmt:
            /* Activate the status icon. */
            ui_sb_update_icon(SB_HDD | HDD_BUS_XTA, 1);

            /* Seek to cylinder. */
            if (do_seek(dev, drive, start_cyl)) {
                intr = 1;
                break;
            }
            dev->head   = ccb->head;
            dev->sector = 1;

            /* Get address of sector to write. */
            if (get_sector(dev, drive, &addr)) {
                intr = 1;
                break;
            }

            /*
             * For now, we don't use the info from
             * the FCB, although we should at least
             * use it's "filler byte" value...
             */
#if 0
        hdd_image_zero_ex(drive->hdd_num, addr, fcb->fill, drive->spt);
#else
            hdd_image_zero(drive->hdd_num, addr, drive->spt);
#endif

            /* Done with this track. */
            dev->state = STATE_FDONE;
            /*FALLTHROUGH*/

        case STATE_FDONE:
            /* One more track done. */
            if (++start_cyl == end_cyl) {
                intr = 1;
                break;
            }

            /* De-activate the status icon. */
            ui_sb_update_icon(SB_HDD | HDD_BUS_XTA, 0);

            /* This saves us a LOT of code. */
            dev->state = STATE_FINIT;
            goto do_fmt;
    }

    /* If we errored out, go back idle. */
    if (intr) {
        /* De-activate the status icon. */
        ui_sb_update_icon(SB_HDD | HDD_BUS_XTA, 0);

        do_finish(dev);
    }
}

/* Execute the CCB we just received. */
static void
hdc_callback(void *priv)
{
    hdc_t   *dev = (hdc_t *) priv;
    ccb_t   *ccb = &dev->ccb;
    drive_t *drive;
    off64_t  addr;
    int      no_data = 0;
    int      val;

    /* Cancel timer. */
    dev->callback = 0;

    /* Clear the SSB error bits. */
    dev->ssb.track_0        = 0;
    dev->ssb.cylinder_err   = 0;
    dev->ssb.write_fault    = 0;
    dev->ssb.seek_end       = 0;
    dev->ssb.not_ready      = 0;
    dev->ssb.id_not_found   = 0;
    dev->ssb.wrong_cyl      = 0;
    dev->ssb.all_bit_set    = 0;
    dev->ssb.mark_not_found = 0;
    dev->ssb.ecc_crc_err    = 0;
    dev->ssb.ecc_crc_field  = 0;
    dev->ssb.valid          = 1;

    /* We really only support one drive, but ohwell. */
    drive = &dev->drives[0];

    switch (ccb->cmd) {
        case CMD_READ_VERIFY:
            no_data = 1;
            /*FALLTHROUGH*/

        case CMD_READ_SECTORS:
            if (!drive->present) {
                dev->ssb.not_ready = 1;
                do_finish(dev);
                return;
            }

            switch (dev->state) {
                case STATE_IDLE:
                    /* Seek to cylinder if requested. */
                    if (ccb->auto_seek) {
                        if (do_seek(dev, drive,
                                    (ccb->cyl_low | (ccb->cyl_high << 8)))) {
                            do_finish(dev);
                            return;
                        }
                    }
                    dev->head   = ccb->head;
                    dev->sector = ccb->sector;

                    /* Get sector count and size. */
                    dev->count   = (int) ccb->count;
                    dev->buf_len = (128 << dev->ssb.sect_size);

                    dev->state = STATE_SEND;
                    /*FALLTHROUGH*/

                case STATE_SEND:
                    /* Activate the status icon. */
                    ui_sb_update_icon(SB_HDD | HDD_BUS_XTA, 1);

do_send:
                    /* Get address of sector to load. */
                    if (get_sector(dev, drive, &addr)) {
                        /* De-activate the status icon. */
                        ui_sb_update_icon(SB_HDD | HDD_BUS_XTA, 0);
                        do_finish(dev);
                        return;
                    }

                    /* Read the block from the image. */
                    hdd_image_read(drive->hdd_num, addr, 1,
                                   (uint8_t *) dev->sector_buf);

                    /* Ready to transfer the data out. */
                    dev->state   = STATE_SDATA;
                    dev->buf_idx = 0;
                    if (no_data) {
                        /* Delay a bit, no actual transfer. */
                        hdc_set_callback(dev, HDC_TIME);
                    } else {
                        if (dev->ctrl & ACR_DMA_EN) {
                            /* DMA enabled. */
                            dev->buf_ptr = dev->sector_buf;
                            hdc_set_callback(dev, HDC_TIME);
                        } else {
                            /* No DMA, do PIO. */
                            dev->status |= (ASR_DATA_REQ | ASR_DIR);

                            /* Copy from sector to data. */
                            memcpy(dev->data,
                                   dev->sector_buf,
                                   dev->buf_len);
                            dev->buf_ptr = dev->data;
                        }
                    }
                    break;

                case STATE_SDATA:
                    if (!no_data) {
                        /* Perform DMA. */
                        while (dev->buf_idx < dev->buf_len) {
                            val = dma_channel_write(dev->dma,
                                                    *dev->buf_ptr++);
                            if (val == DMA_NODATA) {
                                ps1_hdc_log("HDC: CMD_READ_SECTORS out of data (idx=%d, len=%d)!\n", dev->buf_idx, dev->buf_len);

                                /* De-activate the status icon. */
                                ui_sb_update_icon(SB_HDD | HDD_BUS_XTA, 0);

                                dev->intstat |= ISR_EQUIP_CHECK;
                                dev->ssb.need_reset = 1;
                                do_finish(dev);
                                return;
                            }
                            dev->buf_idx++;
                        }
                    }
                    dev->state = STATE_SDONE;
                    hdc_set_callback(dev, HDC_TIME);
                    break;

                case STATE_SDONE:
                    dev->buf_idx = 0;
                    if (--dev->count == 0) {
                        /* De-activate the status icon. */
                        ui_sb_update_icon(SB_HDD | HDD_BUS_XTA, 0);

                        if (!(dev->ctrl & ACR_DMA_EN))
                            dev->status &= ~(ASR_DATA_REQ | ASR_DIR);
                        dev->ssb.cmd_syndrome = 0xD4;
                        do_finish(dev);
                        return;
                    }

                    /* Addvance to next sector. */
                    next_sector(dev, drive);

                    /* This saves us a LOT of code. */
                    dev->state = STATE_SEND;
                    goto do_send;
            }
            break;

        case CMD_READ_EXT: /* READ_EXT */
        case CMD_READ_ID:  /* READ_ID */
            if (!drive->present) {
                dev->ssb.not_ready = 1;
                do_finish(dev);
                return;
            }

            dev->intstat |= ISR_INVALID_CMD;
            do_finish(dev);
            break;

        case CMD_RECALIBRATE: /* RECALIBRATE */
            if (drive->present) {
                dev->track = drive->cur_cyl = 0;
            } else {
                dev->ssb.not_ready = 1;
                dev->intstat |= ISR_TERMINATION;
            }

            do_finish(dev);
            break;

        case CMD_WRITE_VERIFY:
            no_data = 1;
            /*FALLTHROUGH*/

        case CMD_WRITE_SECTORS:
            if (!drive->present) {
                dev->ssb.not_ready = 1;
                do_finish(dev);
                return;
            }

            switch (dev->state) {
                case STATE_IDLE:
                    /* Seek to cylinder if requested. */
                    if (ccb->auto_seek) {
                        if (do_seek(dev, drive,
                                    (ccb->cyl_low | (ccb->cyl_high << 8)))) {
                            do_finish(dev);
                            return;
                        }
                    }
                    dev->head   = ccb->head;
                    dev->sector = ccb->sector;

                    /* Get sector count and size. */
                    dev->count   = (int) ccb->count;
                    dev->buf_len = (128 << dev->ssb.sect_size);

                    dev->state = STATE_RECV;
                    /*FALLTHROUGH*/

                case STATE_RECV:
                    /* Activate the status icon. */
                    ui_sb_update_icon(SB_HDD | HDD_BUS_XTA, 1);
do_recv:
                    /* Ready to transfer the data in. */
                    dev->state   = STATE_RDATA;
                    dev->buf_idx = 0;
                    if (no_data) {
                        /* Delay a bit, no actual transfer. */
                        hdc_set_callback(dev, HDC_TIME);
                    } else {
                        if (dev->ctrl & ACR_DMA_EN) {
                            /* DMA enabled. */
                            dev->buf_ptr = dev->sector_buf;
                            hdc_set_callback(dev, HDC_TIME);
                        } else {
                            /* No DMA, do PIO. */
                            dev->buf_ptr = dev->data;
                            dev->status |= ASR_DATA_REQ;
                        }
                    }
                    break;

                case STATE_RDATA:
                    if (!no_data) {
                        /* Perform DMA. */
                        while (dev->buf_idx < dev->buf_len) {
                            val = dma_channel_read(dev->dma);
                            if (val == DMA_NODATA) {
                                ps1_hdc_log("HDC: CMD_WRITE_SECTORS out of data (idx=%d, len=%d)!\n", dev->buf_idx, dev->buf_len);

                                /* De-activate the status icon. */
                                ui_sb_update_icon(SB_HDD | HDD_BUS_XTA, 0);

                                dev->intstat |= ISR_EQUIP_CHECK;
                                dev->ssb.need_reset = 1;
                                do_finish(dev);
                                return;
                            }
                            dev->buf_ptr[dev->buf_idx] = (val & 0xff);
                            dev->buf_idx++;
                        }
                    }
                    dev->state = STATE_RDONE;
                    hdc_set_callback(dev, HDC_TIME);
                    break;

                case STATE_RDONE:
                    /* Copy from data to sector if PIO. */
                    if (!(dev->ctrl & ACR_DMA_EN))
                        memcpy(dev->sector_buf,
                               dev->data,
                               dev->buf_len);

                    /* Get address of sector to write. */
                    if (get_sector(dev, drive, &addr)) {
                        /* De-activate the status icon. */
                        ui_sb_update_icon(SB_HDD | HDD_BUS_XTA, 0);

                        do_finish(dev);
                        return;
                    }

                    /* Write the block to the image. */
                    hdd_image_write(drive->hdd_num, addr, 1,
                                    (uint8_t *) dev->sector_buf);

                    dev->buf_idx = 0;
                    if (--dev->count == 0) {
                        /* De-activate the status icon. */
                        ui_sb_update_icon(SB_HDD | HDD_BUS_XTA, 0);

                        if (!(dev->ctrl & ACR_DMA_EN))
                            dev->status &= ~ASR_DATA_REQ;
                        dev->ssb.cmd_syndrome = 0xD4;
                        do_finish(dev);
                        return;
                    }

                    /* Advance to next sector. */
                    next_sector(dev, drive);

                    /* This saves us a LOT of code. */
                    dev->state = STATE_RECV;
                    goto do_recv;
            }
            break;

        case CMD_FORMAT_DRIVE:
        case CMD_FORMAT_TRACK:
            do_format(dev, drive, ccb);
            break;

        case CMD_SEEK:
            if (!drive->present) {
                dev->ssb.not_ready = 1;
                do_finish(dev);
                return;
            }

            if (ccb->ec_p == 1) {
                /* Park the heads. */
                val = do_seek(dev, drive, drive->tracks - 1);
            } else {
                /* Seek to cylinder. */
                val = do_seek(dev, drive,
                              (ccb->cyl_low | (ccb->cyl_high << 8)));
            }
            if (!val)
                dev->ssb.seek_end = 1;
            do_finish(dev);
            break;

        default:
            dev->intstat |= ISR_INVALID_CMD;
            do_finish(dev);
    }
}

/* Prepare to send the SSB block. */
static void
hdc_send_ssb(hdc_t *dev)
{
    drive_t *drive;

    /* We only support one drive, really, but ohwell. */
    drive = &dev->drives[0];

    if (!dev->ssb.valid) {
        /* Create a valid SSB. */
        memset(&dev->ssb, 0x00, sizeof(dev->ssb));
        dev->ssb.sect_size  = 0x02; /* 512 bytes */
        dev->ssb.drive_type = drive->type;
    }

    /* Update position fields. */
    dev->ssb.track_0       = !!(dev->track == 0);
    dev->ssb.last_cyl_low  = dev->ssb.curr_cyl_low;
    dev->ssb.last_cyl_high = dev->ssb.curr_cyl_high;
    dev->ssb.last_head     = dev->ssb.curr_head;
    dev->ssb.curr_cyl_high = ((dev->track >> 8) & 0x03);
    dev->ssb.curr_cyl_low  = (dev->track & 0xff);
    dev->ssb.curr_head     = (dev->head & 0x0f);

    dev->ssb.headsel_state = dev->ssb.curr_head;
    dev->ssb.last_sect     = dev->sector;

    /* We abuse an unused MBZ bit, so clear it. */
    dev->ssb.valid = 0;

    /* Set up the transfer buffer for the SSB. */
    dev->buf_idx = 0;
    dev->buf_len = sizeof(dev->ssb);
    dev->buf_ptr = (uint8_t *) &dev->ssb;

    /* Done with the SSB. */
    dev->attn &= ~ATT_SSB;
}

/* Read one of the controller registers. */
static uint8_t
hdc_read(uint16_t port, void *priv)
{
    hdc_t  *dev = (hdc_t *) priv;
    uint8_t ret = 0xff;

    /* TRM: tell system board we are alive. */
    *dev->reg_91 |= 0x01;

    switch (port & 7) {
        case 0: /* DATA register */
            if (dev->state == STATE_SDATA) {
                if (dev->buf_idx > dev->buf_len) {
                    ps1_hdc_log("HDC: read with empty buffer!\n");
                    dev->state = STATE_IDLE;
                    dev->intstat |= ISR_INVALID_CMD;
                    dev->status &= (ASR_TX_EN | ASR_DATA_REQ | ASR_DIR);
                    set_intr(dev, 1);
                    break;
                }

                ret = dev->buf_ptr[dev->buf_idx];
                if (++dev->buf_idx == dev->buf_len) {
                    /* Data block sent OK. */
                    dev->status &= ~(ASR_TX_EN | ASR_DATA_REQ | ASR_DIR);
                    dev->state = STATE_IDLE;
                }
            }
            break;

        case 2: /* ASR */
            ret = dev->status;
            break;

        case 4: /* ISR */
            ret          = dev->intstat;
            dev->intstat = 0x00;
            break;
    }

    return (ret);
}

static void
hdc_write(uint16_t port, uint8_t val, void *priv)
{
    hdc_t *dev = (hdc_t *) priv;

    /* TRM: tell system board we are alive. */
    *dev->reg_91 |= 0x01;

    switch (port & 7) {
        case 0: /* DATA register */
            if (dev->state == STATE_RDATA) {
                if (dev->buf_idx >= dev->buf_len) {
                    ps1_hdc_log("HDC: write with full buffer!\n");
                    dev->intstat |= ISR_INVALID_CMD;
                    dev->status &= ~ASR_DATA_REQ;
                    set_intr(dev, 1);
                    break;
                }

                /* Store the data into the buffer. */
                dev->buf_ptr[dev->buf_idx] = val;
                if (++dev->buf_idx == dev->buf_len) {
                    /* We got all the data we need. */
                    dev->status &= ~ASR_DATA_REQ;
                    dev->state = STATE_IDLE;

                    /* If we were receiving a CCB, execute it. */
                    if (dev->attn & ATT_CCB) {
                        /*
                         * If we were already busy with
                         * a CCB, then it must have had
                         * some new data using PIO.
                         */
                        if (dev->status & ASR_BUSY)
                            dev->state = STATE_RDONE;
                        else
                            dev->status |= ASR_BUSY;

                        /* Schedule command execution. */
                        hdc_set_callback(dev, HDC_TIME);
                    }
                }
            }
            break;

        case 2: /* ACR */
            dev->ctrl = val;
            if (val & ACR_INT_EN)
                set_intr(dev, 0); /* clear IRQ */

            if (dev->reset != 0) {
                if (++dev->reset == 3) {
                    dev->reset = 0;

                    set_intr(dev, 1);
                }
                break;
            }

            if (val & ACR_RESET)
                dev->reset = 1;
            break;

        case 4: /* ATTN */
            dev->status &= ~ASR_INT_REQ;
            if (val & ATT_DATA) {
                /* Dunno. Start PIO/DMA now? */
            }

            if (val & ATT_SSB) {
                if (dev->attn & ATT_CCB) {
                    /* Hey now, we're still busy for you! */
                    dev->intstat |= ISR_INVALID_CMD;
                    set_intr(dev, 1);
                    break;
                }

                /* OK, prepare for sending an SSB. */
                dev->attn |= ATT_SSB;

                /* Grab or initialize an SSB to send. */
                hdc_send_ssb(dev);

                dev->state = STATE_SDATA;
                dev->status |= (ASR_TX_EN | ASR_DATA_REQ | ASR_DIR);
                set_intr(dev, 1);
            }

            if (val & ATT_CCB) {
                dev->attn |= ATT_CCB;

                /* Set up the transfer buffer for a CCB. */
                dev->buf_idx = 0;
                dev->buf_len = sizeof(dev->ccb);
                dev->buf_ptr = (uint8_t *) &dev->ccb;

                dev->state = STATE_RDATA;
                dev->status |= ASR_DATA_REQ;
                set_intr(dev, 1);
            }
            break;
    }
}

static void *
ps1_hdc_init(const device_t *info)
{
    drive_t *drive;
    hdc_t   *dev;
    int      c, i;

    /* Allocate and initialize device block. */
    dev = malloc(sizeof(hdc_t));
    memset(dev, 0x00, sizeof(hdc_t));

    /* Set up controller parameters for PS/1 2011. */
    dev->base = 0x0320;
    dev->irq  = 14;
    dev->dma  = 3;

    ps1_hdc_log("HDC: initializing (I/O=%04X, IRQ=%d, DMA=%d)\n",
                dev->base, dev->irq, dev->dma);

    /* Load any disks for this device class. */
    c = 0;
    for (i = 0; i < HDD_NUM; i++) {
        if ((hdd[i].bus == HDD_BUS_XTA) && (hdd[i].xta_channel < 1)) {
            drive = &dev->drives[hdd[i].xta_channel];

            if (!hdd_image_load(i)) {
                drive->present = 0;
                continue;
            }
            drive->id = c;

            /* These are the "hardware" parameters (from the image.) */
            drive->cfg_spt    = (uint8_t) (hdd[i].spt & 0xff);
            drive->cfg_hpc    = (uint8_t) (hdd[i].hpc & 0xff);
            drive->cfg_tracks = (uint16_t) hdd[i].tracks;

            /* Use them as "active" parameters until overwritten. */
            drive->spt    = drive->cfg_spt;
            drive->hpc    = drive->cfg_hpc;
            drive->tracks = drive->cfg_tracks;

            drive->type    = ibm_drive_type(drive);
            drive->hdd_num = i;
            drive->present = 1;

            ps1_hdc_log("HDC: drive%d (type %d: cyl=%d,hd=%d,spt=%d), disk %d\n",
                        hdd[i].xta_channel, drive->type,
                        drive->tracks, drive->hpc, drive->spt, i);

            if (++c > 1)
                break;
        }
    }

    /* Sectors are 1-based. */
    dev->sector = 1;

    /* Enable the I/O block. */
    io_sethandler(dev->base, 5,
                  hdc_read, NULL, NULL, hdc_write, NULL, NULL, dev);

    /* Create a timer for command delays. */
    timer_add(&dev->timer, hdc_callback, dev, 0);

    return (dev);
}

static void
ps1_hdc_close(void *priv)
{
    hdc_t   *dev = (hdc_t *) priv;
    drive_t *drive;
    int      d;

    /* Remove the I/O handler. */
    io_removehandler(dev->base, 5,
                     hdc_read, NULL, NULL, hdc_write, NULL, NULL, dev);

    /* Close all disks and their images. */
    for (d = 0; d < XTA_NUM; d++) {
        drive = &dev->drives[d];

        if (drive->present)
            hdd_image_close(drive->hdd_num);
    }

    /* Release the device. */
    free(dev);
}

const device_t ps1_hdc_device = {
    .name          = "PS/1 2011 Fixed Disk Controller",
    .internal_name = "ps1_hdc",
    .flags         = DEVICE_ISA | DEVICE_PS2,
    .local         = 0,
    .init          = ps1_hdc_init,
    .close         = ps1_hdc_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/*
 * Very nasty.
 *
 * The PS/1 systems employ a feedback system where external
 * cards let the system know they were 'addressed' by setting
 * their Card Selected Flag (CSF) in register 0x0091.  Driver
 * software can test this register to see if they are talking
 * to hardware or not.
 *
 * This means, that we must somehow do the same, and yes, I
 * agree that the current solution is nasty.
 */
void
ps1_hdc_inform(void *priv, uint8_t *reg_91)
{
    hdc_t *dev = (hdc_t *) priv;

    dev->reg_91 = reg_91;
}
