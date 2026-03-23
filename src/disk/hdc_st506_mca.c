/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Driver for IBM ST506 MFM Adapter (MCA), based on existing
 *          PS/1 Model 2011 disk controller emulation (hdc_xta_ps1.c)
 *          written by Fred N. van Kempen.
 *
 *          Command Specify Block and some register definitions are 
 *          derived from Minix 1.5 ps_wini.c source.
 *
 *            AdapterID:            0xDFFD
 *            AdapterName:          "IBM Fixed Disk Adapter"
 *            NumBytes              2
 *            I/O base:             0x320-0x324
 *            IRQ:                  14
 *
 *            Primary Board         pos[0]=XXXX XX0X    0x320
 *            Secondary Board       pos[0]=XXXX XX1X    0x328
 *
 *            Arbitration level 3   pos[1]=1111 0011
 *            Arbitration level 4   pos[1]=1111 0100
 *            Arbitration level 5   pos[1]=1111 0101
 *            Arbitration level 6   pos[1]=1111 0110
 *            Arbitration level 7   pos[1]=1111 0111
 *            Arbitration level 8   pos[1]=1111 1000
 *            Arbitration level 9   pos[1]=1111 1001
 *            Arbitration level 10  pos[1]=1111 1010
 *            Arbitration level 11  pos[1]=1111 1011
 *            Arbitration level 12  pos[1]=1111 1100
 *            Arbitration level 13  pos[1]=1111 1101
 *            Arbitration level 14  pos[1]=1111 1110
 *            Arbitration level 0   pos[1]=1111 0000
 *            Arbitration level 1   pos[1]=1111 0001
 *
 * NOTE:    Based on Adaptec ACB-2600 OEM Manual, the IBM ST506 MFM 
 *          Adapter does not support IBM PS/2 model 80 type 2 or 3,
 *          So do not use this disk adapter with these machines.
 *
 * NOTE:    To use this disk adapter, drive types should be set first
 *          in the Set Configuration menu, then formatted in the Main
 *          Menu by pressing CTRL-A. When format program displays the
 *          message saying the configuration area is unreadable, type
 *          F3 to continue formatting. This procedure should do only
 *          once, and after that disk not recognized error message
 *          should disappear on sequential reboots.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          WNT50
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2017-2019 Fred N. van Kempen.
 *          Copyright 2026 WNT50.
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
#include <86box/mca.h>
#include <86box/io.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/device.h>
#include <86box/hdc.h>
#include <86box/hdd.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/machine.h>
#include "cpu.h"

#define MFM_TIME         (10 * TIMER_USEC)
#define MFM_SECTOR_TIME  (250 * TIMER_USEC)

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

/* Command values. These deviate from the PS/1 XTA ones. */
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

/* Attachment Status register (reg 2R) values */
#define ASR_TX_EN    0x01 /* transfer enable */
#define ASR_INT_REQ  0x02 /* interrupt request */
#define ASR_BUSY     0x04 /* busy */
#define ASR_DIR      0x08 /* direction */
#define ASR_DATA_REQ 0x10 /* data request */

/* Attachment Control register (2W) values */
#define ACR_DMA_EN 0x01 /* DMA transfer enable */
#define ACR_INT_EN 0x02 /* interrupt enable */
#define ACR_RESET  0x80 /* reset */

/* Interrupt Status register (4R) values */
#define ISR_EQUIP_CHECK 0x01 /* internal hardware error */
#define ISR_ERP_INVOKED 0x02 /* error recovery invoked */
#define ISR_CMD_REJECT  0x20 /* command reject */
#define ISR_INVALID_CMD 0x40 /* invalid command */
#define ISR_TERMINATION 0x80 /* termination error */

/* Attention register (4W) values */
#define ATT_ABRT 0x01 /* abort last command */
#define ATT_CHAN 0x04 /* disk channel select */
#define ATT_DATA 0x10 /* data request enable */
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
 *
 * NOTE: Bit 5 of Status Byte 0 should be 1, which is different
 * from IBM PS/1 Model 2011 disk controller described in
 * Technical Reference for PS/1 Computer.
 */
#pragma pack(push, 1)
typedef struct ssb_t {
    /* Status byte 0. */
    uint8_t track_0      : 1; /* T0           */
    uint8_t mbz1         : 1; /* 0            */
    uint8_t mbz2         : 1; /* 0            */
    uint8_t cylinder_err : 1; /* CE           */
    uint8_t write_fault  : 1; /* WF           */
    uint8_t mbo1         : 1; /* 1            */
    uint8_t seek_end     : 1; /* SE           */
    uint8_t not_ready    : 1; /* NR           */

    /* Status byte 1. */
    uint8_t id_not_found   : 1; /* ID           */
    uint8_t mbz4           : 1; /* 0            */
    uint8_t mbz5           : 1; /* 0            */
    uint8_t wrong_cyl      : 1; /* WC           */
    uint8_t all_bit_set    : 1; /* BT           */
    uint8_t mark_not_found : 1; /* AM           */
    uint8_t ecc_crc_err    : 1; /* ET           */
    uint8_t ecc_crc_field  : 1; /* EF           */

    /* Status byte 2. */
    uint8_t headsel_state    : 4; /* headsel state[4] */
    uint8_t defective_sector : 1; /* DS               */
    uint8_t retried_ok       : 1; /* RG               */
    uint8_t need_reset       : 1; /* RR               */
#if 1
    uint8_t valid : 1; /* 0 (abused as VALID)    */
#else
    uint8_t mbz6  : 1; /* 0                      */
#endif

    /* Most recent ID field seen. */
    uint8_t last_cyl_low;      /* Cyl_Low[8]   */
    uint8_t last_head     : 4; /* HD[4]        */
    uint8_t mbz7          : 1; /* 0            */
    uint8_t last_cyl_high : 2; /* Cyl_high[2]  */
    uint8_t last_def_sect : 1; /* DS           */
    uint8_t last_sect;         /* Sect[8]      */

    uint8_t sect_size; /* Size[8] = 02         */

    /* Current position. */
    uint8_t curr_cyl_high : 2; /* Cyl_High_[2] */
    uint8_t mbz8          : 1; /* 0            */
    uint8_t mbz9          : 1; /* 0            */
    uint8_t curr_head     : 4; /* HD_2[4]      */
    uint8_t curr_cyl_low;      /* Cyl_Low_2[8] */

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
    uint8_t cmd_syndrome; /* command syndrome */

    uint8_t rsvd1; /* reserved byte */
    uint8_t rsvd2; /* reserved byte */
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
typedef struct fcb_t {
    uint8_t cyl_high         : 2; /* cylinder [9:8] bits */
    uint8_t defective_sector : 1; /* DS                  */
    uint8_t mbz1             : 1; /* 0                   */
    uint8_t head             : 4; /* head number         */

    uint8_t cyl_low; /* cylinder [7:0] bits */

    uint8_t sector; /* sector number */

    uint8_t mbz2 : 1; /* 0      */
    uint8_t mbo1 : 1; /* 1      */
    uint8_t mbz3 : 6; /* 000000 */

    uint8_t fill; /* filler byte */
} fcb_t;
#pragma pack(pop)

/*
 * Define the Command Control Block.
 *
 * The system specifies the operation by sending the 6-byte
 * command control block to the controller. It can be sent
 * through a DMA or PIO operation.
 *
 * NOTE: Based on Adaptec ACB-2600 OEM Manual, the real IBM
 * ST506 MFM Adapter should support 2048 cylinders maximum,
 * so cylinder masks should be 11 bits instead of 10.
 */
#pragma pack(push, 1)
typedef struct ccb_t{
    uint8_t ec_p      : 1; /* EC/P (ecc/park) */
    uint8_t mbz1      : 1; /* 0               */
    uint8_t auto_seek : 1; /* AS (auto-seek)  */
    uint8_t no_data   : 1; /* ND (no data)    */
    uint8_t cmd       : 4; /* command code[4] */

    uint8_t cyl_high : 3; /* cylinder [10:8] bits */
    uint8_t mbz2     : 1; /* 00                   */
    uint8_t head     : 4; /* head number          */

    uint8_t cyl_low; /* cylinder [7:0] bits */

    uint8_t sector; /* sector number */

    uint8_t mbz3 : 1; /* 0      */
    uint8_t mbo1 : 1; /* 1      */
    uint8_t mbz4 : 6; /* 000000 */

    uint8_t count; /* blk count/interleave */
} ccb_t;
#pragma pack(pop)

/*
 * Define the Command Specify Block.
 *
 * The system specifies the error recovery procedures of the 
 * drive by sending the 14-byte command specify block to the 
 * controller. It can be sent through a DMA or PIO operation.
 * 
 * NOTE: Definitions are imported from Minix 1.5 ps_wini.c 
 * except byte zero, which comes from Technical Reference 
 * for PS/1 Computer (P/N 57F1970), Section 8. Drives.
 */
#pragma pack(push, 1)
typedef struct csb_t{
    uint8_t ecc     : 1; /* En (ECC enable) */
    uint8_t mbz1    : 3; /* 0               */
    uint8_t retries : 4; /* retries         */

    uint8_t xfer       : 8; /* ST506/2 Interface, 5 Mbps transfer rate */
    uint8_t gap1       : 8; /* Drive Gap 1 (value gotten from bios)    */
    uint8_t gap2       : 8; /* Drive Gap 2 (value gotten from bios)    */
    uint8_t gap3       : 8; /* Drive Gap 3 (value gotten from bios)    */
    uint8_t sync       : 8; /* Sync field length                       */
    uint8_t step       : 8; /* Step rate in 50 microseconds            */
    uint8_t mbz2       : 8; /* IBM reserved, must be all zeroes        */
    uint8_t wpcom_high : 8; /* Write precompensation [15:8]            */
    uint8_t wpcom_low  : 8; /* Write precompensation [7:0]             */
    uint8_t cyl_high   : 8; /* Cylinders per disk [15:8]               */
    uint8_t cyl_low    : 8; /* Cylinders per disk [7:0]                */
    uint8_t spt        : 8; /* Sectors per track                       */
    uint8_t head       : 8; /* Heads per disk                          */
} csb_t;
#pragma pack(pop)

/* Define the hard drive geometry table. */
typedef struct geom_t {
    uint16_t cyl;
    uint8_t  hpc;
    uint8_t  spt;
    int16_t  wpc;
    int16_t  lz;
} geom_t;

/* Define an attached drive. */
typedef struct drive_t {
    int8_t id;      /* drive ID on bus */
    int8_t present; /* drive is present */
    int8_t hdd_num; /* index to global disk table */
    int8_t type;    /* drive type ID */

    uint16_t cur_cyl; /* last known position of heads */

    uint8_t  spt; /* active drive parameters */
    uint8_t  hpc;
    uint16_t tracks;

    uint8_t  cfg_spt; /* configured drive parameters */
    uint8_t  cfg_hpc;
    uint16_t cfg_tracks;
} drive_t;

typedef struct mfm_t {
    uint16_t base; /* controller base I/O address */
    int8_t   irq;  /* controller IRQ channel */
    int8_t   dma;  /* controller DMA channel */

    /* Registers. */
    uint8_t attn;    /* ATTENTION register */
    uint8_t ctrl;    /* Control register (ACR) */
    uint8_t status;  /* Status register (ASR) */
    uint8_t intstat; /* Interrupt Status register (ISR) */

    /* Controller state. */
    pc_timer_t timer;
    int8_t     state; /* controller state */
    int8_t     drive; /* disk drive select */
    int8_t     reset; /* reset state counter */
    int8_t     ready; /* ready state counter */
    int8_t     abort; /* abort state counter */

    /* Data transfer. */
    int16_t buf_idx; /* buffer index and pointer */
    int16_t buf_len;
    uint8_t *buf_ptr;

    /* Current operation parameters. */
    ssb_t    ssb;    /* sense block */
    ccb_t    ccb;    /* command control block */
    csb_t    csb;    /* command specify block */
    uint16_t track;  /* requested track# */
    uint8_t  head;   /* requested head# */
    uint8_t  sector; /* requested sector# */
    int count;       /* requested sector count */

    drive_t drives[MFM_NUM]; /* the attached drive(s) */

    uint8_t data[512];       /* data buffer */
    uint8_t sector_buf[512]; /* sector buffer */

    uint8_t pos_regs[8]; /* POS registers */
} mfm_t;

#ifdef ENABLE_ST506_MCA_LOG
int st506_mca_do_log = ENABLE_ST506_MCA_LOG;

static void
st506_mca_log(const char *fmt, ...)
{
    va_list ap;

    if (st506_mca_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define st506_mca_log(fmt, ...)
#endif

static void
set_intr(mfm_t *dev, int raise)
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
get_sector(mfm_t *dev, drive_t *drive, off64_t *addr)
{
    if (drive->cur_cyl != dev->track) {
        st506_mca_log("ST506: get_sector: wrong cylinder %d/%d\n",
                    drive->cur_cyl, dev->track);
        dev->ssb.wrong_cyl = 1;
        return 1;
    }

    if (dev->head >= drive->hpc) {
        st506_mca_log("ST506: get_sector: past end of heads\n");
        dev->ssb.cylinder_err = 1;
        return 1;
    }

    if (dev->sector > drive->spt) {
        st506_mca_log("ST506: get_sector: past end of sectors\n");
        dev->ssb.mark_not_found = 1;
        return 1;
    }

    /* Calculate logical address (block number) of desired sector. */
    *addr = ((((off64_t) dev->track * drive->hpc) + dev->head) * drive->spt) + dev->sector - 1;

    return 0;
}

static void
next_sector(mfm_t *dev, drive_t *drive)
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
do_finish(mfm_t *dev)
{
    dev->state = STATE_IDLE;

    dev->attn &= ~(ATT_CCB | ATT_DATA);

    dev->status = 0x00;

    set_intr(dev, 1);
}

/* Seek to a cylinder. */
static int
do_seek(mfm_t *dev, drive_t *drive, uint16_t cyl)
{
    if (cyl >= drive->tracks) {
        dev->ssb.cylinder_err = 1;
        return 1;
    }

    dev->track     = cyl;
    drive->cur_cyl = dev->track;

    return 0;
}

/* Format a track or an entire drive. */
static void
do_format(mfm_t *dev, drive_t *drive, ccb_t *ccb)
{
    int     start_cyl;
    int     end_cyl;
    int     intr = 0;
    int     val;
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
                timer_advance_u64(&dev->timer, MFM_TIME);
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
            timer_advance_u64(&dev->timer, MFM_TIME);
            break;

        case STATE_RDONE:
            if (!(dev->ctrl & ACR_DMA_EN))
                dev->status &= ~ASR_DATA_REQ;

                /* Point to the FCB we got. */
#if 0
        fcb = (fcb_t *)dev->data;
#endif
            dev->state = STATE_FINIT;
            fallthrough;

        case STATE_FINIT:
do_fmt:
            /* Activate the status icon. */
            ui_sb_update_icon_write(SB_HDD | HDD_BUS_MFM, 1);

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
            fallthrough;
        case STATE_FDONE:
            /* One more track done. */
            if (++start_cyl == end_cyl) {
                intr = 1;
                break;
            }

            /* De-activate the status icon. */
            ui_sb_update_icon_write(SB_HDD | HDD_BUS_MFM, 0);

            /* This saves us a LOT of code. */
            dev->state = STATE_FINIT;
            goto do_fmt;

        default:
            break;
    }

    /* If we errored out, go back idle. */
    if (intr) {
        /* De-activate the status icon. */
        ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 0);
        ui_sb_update_icon_write(SB_HDD | HDD_BUS_MFM, 0);

        do_finish(dev);
    }
}

/* Execute the CCB we just received. */
static void
hdc_callback(void *priv)
{
    mfm_t   *dev = (mfm_t *) priv;
    ccb_t   *ccb = &dev->ccb;
    drive_t *drive;
    off64_t  addr;
    int      val;
#ifdef ENABLE_ST506_MCA_LOG
    uint8_t  cmd = ccb->cmd & 0x0f;
#endif

    /* If we are returning from a RESET, handle this first. */
    if (dev->reset) {
        st506_mca_log("ST506 reset.\n");
        dev->status &= ~ASR_BUSY;
        dev->reset = 0;
        do_finish(dev);
        return;
    }

    /* Abort last command if requested. */
    if (dev->abort) {
        st506_mca_log("ST506 command abort.\n");
        dev->abort = 0;
        do_finish(dev);
        return;
    }

    /* Select disk drive. */
    if (dev->drive)
        drive = &dev->drives[1];
    else
        drive = &dev->drives[0];

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

    st506_mca_log("hdc_callback(): %02X\n", cmd);

    switch (ccb->cmd) {
        case CMD_READ_VERIFY:
            ccb->no_data = 1;
            ccb->count = 1;
            fallthrough;

        case CMD_READ_SECTORS:
            if (!drive->present) {
                dev->ssb.not_ready = 1;
                do_finish(dev);
                return;
            }

            if (!(dev->ready | ccb->no_data)) {
                /* Delay a bit, transfer not ready. */
                timer_advance_u64(&dev->timer, MFM_SECTOR_TIME);
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
                    fallthrough;

                case STATE_SEND:
                    /* Activate the status icon. */
                    ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 1);

do_send:
                    /* Get address of sector to load. */
                    if (get_sector(dev, drive, &addr)) {
                        /* De-activate the status icon. */
                        ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 0);
                        do_finish(dev);
                        return;
                    }

                    /* Read the block from the image. */
                    hdd_image_read(drive->hdd_num, addr, 1,
                                   (uint8_t *) dev->sector_buf);

                    /* Ready to transfer the data out. */
                    dev->state   = STATE_SDATA;
                    dev->status |= ASR_TX_EN;
                    dev->buf_idx = 0;
                    if (ccb->no_data) {
                        /* Delay a bit, no actual transfer. */
                        timer_advance_u64(&dev->timer, MFM_TIME);
                    } else {
                        if (dev->ctrl & ACR_DMA_EN) {
                            /* DMA enabled. */
                            dev->buf_ptr = dev->sector_buf;
                            timer_advance_u64(&dev->timer, MFM_SECTOR_TIME);
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
                    if (!ccb->no_data) {
                        /* Perform DMA. */
                        while (dev->buf_idx < dev->buf_len) {
                            val = dma_channel_write(dev->dma,
                                                    *(uint16_t *) &(dev->buf_ptr[dev->buf_idx]));
                            if (val == DMA_NODATA) {
                                st506_mca_log("ST506: CMD_READ_SECTORS out of data (idx=%d, len=%d)!\n", dev->buf_idx, dev->buf_len);

                                /* De-activate the status icon. */
                                ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 0);

                                dev->intstat |= ISR_EQUIP_CHECK;
                                dev->ssb.need_reset = 1;
                                do_finish(dev);
                                return;
                            }
                            dev->buf_idx += 2;
                        }
                    }
                    dev->state = STATE_SDONE;
                    timer_advance_u64(&dev->timer, MFM_TIME);
                    break;

                case STATE_SDONE:
                    dev->buf_idx = 0;
                    if (--dev->count == 0) {
                        /* De-activate the status icon. */
                        ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 0);

                        if (!(dev->ctrl & ACR_DMA_EN))
                            dev->status &= ~(ASR_DATA_REQ | ASR_DIR);
                        dev->ssb.cmd_syndrome = 0xD4;
                        dev->ssb.seek_end = 1;
                        do_finish(dev);
                        return;
                    }

                    /* Advance to next sector. */
                    next_sector(dev, drive);

                    /* This saves us a LOT of code. */
                    dev->state = STATE_SEND;
                    goto do_send;

                default:
                    break;
            }
            break;

        case CMD_READ_ID:  /* READ_ID */
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

                    /* Get sector count and size. */
                    dev->count   = (int) ccb->count;
                    dev->buf_len = (128 << dev->ssb.sect_size);

                    /* Activate the status icon. */
                    ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 1);

                    /* Ready to transfer the data out. */
                    dev->state = STATE_SDONE;
                    dev->buf_idx = 0;
                    /* Delay a bit, no actual transfer. */
                    timer_advance_u64(&dev->timer, MFM_TIME);
                    break;

                case STATE_SDONE:
                    dev->buf_idx = 0;

                    /* De-activate the status icon. */
                    ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 0);

                    if (!(dev->ctrl & ACR_DMA_EN))
                        dev->status &= ~(ASR_DATA_REQ | ASR_DIR);
                    dev->ssb.cmd_syndrome = 0x14;
                    dev->ssb.seek_end = 1;
                    do_finish(dev);
                    break;

                default:
                    break;
            }
            break;

        case CMD_READ_EXT: /* READ_EXT */
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
        case CMD_WRITE_SECTORS:
            if (!drive->present) {
                dev->ssb.not_ready = 1;
                do_finish(dev);
                return;
            }

            if (!(dev->ready | ccb->no_data)) {
                /* Delay a bit, transfer not ready. */
                timer_advance_u64(&dev->timer, MFM_TIME);
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
                    fallthrough;

                case STATE_RECV:
                    /* Activate the status icon. */
                    ui_sb_update_icon_write(SB_HDD | HDD_BUS_MFM, 1);
do_recv:
                    /* Ready to transfer the data in. */
                    dev->state   = STATE_RDATA;
                    dev->status |= ASR_TX_EN;
                    dev->buf_idx = 0;
                    if (ccb->no_data) {
                        /* Delay a bit, no actual transfer. */
                        timer_advance_u64(&dev->timer, MFM_TIME);
                    } else {
                        if (dev->ctrl & ACR_DMA_EN) {
                            /* DMA enabled. */
                            dev->buf_ptr = dev->sector_buf;
                            timer_advance_u64(&dev->timer, MFM_SECTOR_TIME);
                        } else {
                            /* No DMA, do PIO. */
                            dev->buf_ptr = dev->data;
                            dev->status |= ASR_DATA_REQ;
                        }
                    }
                    break;

                case STATE_RDATA:
                    if (!ccb->no_data) {
                        /* Perform DMA. */
                        while (dev->buf_idx < dev->buf_len) {
                            val = dma_channel_read(dev->dma);
                            if (val == DMA_NODATA) {
                                st506_mca_log("ST506: CMD_WRITE_SECTORS out of data (idx=%d, len=%d)!\n", dev->buf_idx, dev->buf_len);

                                /* De-activate the status icon. */
                                ui_sb_update_icon_write(SB_HDD | HDD_BUS_MFM, 0);

                                dev->intstat |= ISR_EQUIP_CHECK;
                                dev->ssb.need_reset = 1;
                                do_finish(dev);
                                return;
                            }
                            *(uint16_t *) &(dev->buf_ptr[dev->buf_idx]) = (val & 0xffff);
                            dev->buf_idx += 2;
                        }
                    }
                    dev->state = STATE_RDONE;
                    timer_advance_u64(&dev->timer, MFM_TIME);
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
                        ui_sb_update_icon_write(SB_HDD | HDD_BUS_MFM, 0);

                        do_finish(dev);
                        return;
                    }

                    /* Write the block to the image. */
                    hdd_image_write(drive->hdd_num, addr, 1,
                                    (uint8_t *) dev->sector_buf);

                    dev->buf_idx = 0;
                    if (--dev->count == 0) {
                        /* De-activate the status icon. */
                        ui_sb_update_icon_write(SB_HDD | HDD_BUS_MFM, 0);

                        if (!(dev->ctrl & ACR_DMA_EN))
                            dev->status &= ~ASR_DATA_REQ;
                        dev->ssb.cmd_syndrome = 0xD4;
                        dev->ssb.seek_end = 1;
                        do_finish(dev);
                        return;
                    }

                    /* Advance to next sector. */
                    next_sector(dev, drive);

                    /* This saves us a LOT of code. */
                    dev->state = STATE_RECV;
                    goto do_recv;

                default:
                    break;
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
hdc_send_ssb(mfm_t *dev)
{
    const drive_t *drive;

    if (!dev->ssb.valid) {
        /* Create a valid SSB. */
        memset(&dev->ssb, 0x00, sizeof(dev->ssb));
        dev->ssb.sect_size  = 0x02; /* 512 bytes */
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

    /* POST wants this byte to be 1, so set it. */
    dev->ssb.mbo1 = 1;

    /* Set up the transfer buffer for the SSB. */
    dev->buf_idx = 0;
    dev->buf_len = sizeof(dev->ssb);
    dev->buf_ptr = (uint8_t *) &dev->ssb;

    /* Done with the SSB. */
    dev->attn &= ~ATT_SSB;
}

/* Read one of the controller registers. */
static uint8_t
mfm_read(uint16_t port, void *priv)
{
    mfm_t  *dev = (mfm_t *) priv;
    uint8_t ret = 0xff;

    switch (port & 7) {
        case 0: /* DATA register */
            if (dev->state == STATE_SDATA) {
                if (dev->buf_idx > dev->buf_len) {
                    st506_mca_log("ST506: read with empty buffer!\n");
                    dev->state = STATE_IDLE;
                    dev->intstat |= ISR_INVALID_CMD;
                    dev->status &= ~(ASR_TX_EN | ASR_DATA_REQ | ASR_DIR);
                    set_intr(dev, 1);
                    break;
                }

                /* Read the data from the buffer. */
                ret = dev->buf_ptr[dev->buf_idx];
                if (++dev->buf_idx == dev->buf_len) {
                    /* Data block sent OK. */
                    dev->status &= ~(ASR_TX_EN | ASR_DATA_REQ | ASR_DIR);
                    dev->state = STATE_IDLE;
                    set_intr(dev, 1);
                }
            }
            break;

        case 2: /* ASR */
            ret = dev->status;
            break;

        case 4: /* ISR */
            dev->status &= ~ASR_INT_REQ;
            ret          = dev->intstat;
            dev->intstat = 0x00;
            break;

        default:
            break;
    }

    st506_mca_log("[%04X:%08X] [R] %04X = %02X\n", CS, cpu_state.pc, port, ret);

    return ret;
}

static void
mfm_write(uint16_t port, uint8_t val, void *priv)
{
    mfm_t *dev = (mfm_t *) priv;

    st506_mca_log("[%04X:%08X] [W] %04X = %02X\n", CS, cpu_state.pc, port, val);

    switch (port & 7) {
        case 0: /* DATA register */
            if (dev->state == STATE_RDATA) {
                if (dev->buf_idx >= dev->buf_len) {
                    st506_mca_log("ST506: write with full buffer!\n");
                    dev->status &= ~(ASR_TX_EN | ASR_DATA_REQ);
                    dev->intstat |= ISR_INVALID_CMD;
                    set_intr(dev, 1);
                    break;
                }

                /* Store the data into the buffer. */
                dev->buf_ptr[dev->buf_idx] = val;
                st506_mca_log("dev->buf_ptr[%02X] = %02X\n", dev->buf_idx, val);
                if (++dev->buf_idx == dev->buf_len) {
                    /* We got all the data we need. */
                    dev->status &= ~(ASR_TX_EN | ASR_DATA_REQ);
                    dev->state = STATE_IDLE;
                    set_intr(dev, 1);

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
                        timer_set_delay_u64(&dev->timer, MFM_SECTOR_TIME);
                    }
                }
            }
            break;

        case 2: /* ACR */
            dev->ctrl = val;
            if (val & ACR_INT_EN)
                set_intr(dev, 0); /* clear IRQ */

            if (val & ACR_RESET) {
                dev->reset = 1;
                dev->status |= ASR_BUSY;
                /* Schedule command execution. */
                timer_set_delay_u64(&dev->timer, MFM_TIME);
            }
            break;

        case 4: /* ATTN */
            dev->status &= ~ASR_INT_REQ;

            if (val & ATT_ABRT) {
                dev->abort = 1;
                dev->status &= ~ASR_BUSY;
                /* Schedule command execution. */
                timer_set_delay_u64(&dev->timer, MFM_TIME);
            }

            if (val & ATT_DATA)
                dev->ready = 1;
            else
                dev->ready = 0;

            if (val & ATT_CHAN)
                dev->drive = 1;
            else
                dev->drive = 0;

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
            }

            if (val & ATT_CCB) {
                if (dev->attn & ATT_CCB)
                    /* Hey now, we're still busy for you! */
                    break;

                /* OK, prepare for receiving a CCB. */
                dev->attn |= ATT_CCB;

                /* Set up the transfer buffer for a CCB. */
                dev->buf_idx = 0;
                dev->buf_len = sizeof(dev->ccb);
                dev->buf_ptr = (uint8_t *) &dev->ccb;

                dev->state = STATE_RDATA;
                dev->status |= (ASR_TX_EN | ASR_DATA_REQ);
            }

            if (val & ATT_CSB) {
                /* OK, prepare for receiving a CSB. */
                dev->attn |= ATT_CSB;

                /* Set up the transfer buffer for a CSB. */
                dev->buf_idx = 0;
                dev->buf_len = sizeof(dev->csb);
                dev->buf_ptr = (uint8_t *) &dev->csb;

                dev->state = STATE_RDATA;
                dev->status |= (ASR_TX_EN | ASR_DATA_REQ);
            }
            break;

        default:
            break;
    }
}

/* Read one of the controller registers. */
static uint16_t
mfm_readw(uint16_t port, void *priv)
{
    mfm_t  *dev = (mfm_t *) priv;
    uint16_t ret = 0xffff;

    switch (port & 7) {
        case 0: /* DATA register */
            if (dev->state == STATE_SDATA) {
                if (dev->buf_idx > dev->buf_len) {
                    st506_mca_log("ST506: read with empty buffer!\n");
                    dev->state = STATE_IDLE;
                    dev->intstat |= ISR_INVALID_CMD;
                    dev->status &= (ASR_TX_EN | ASR_DATA_REQ | ASR_DIR);
                    set_intr(dev, 1);
                    break;
                }

                /* Read the data from the buffer. */
                ret = dev->buf_ptr[dev->buf_idx];
                dev->buf_idx += 2;

                if (dev->buf_idx >= dev->buf_len) {
                    /* Data block sent OK. */
                    dev->status &= ~(ASR_TX_EN | ASR_DATA_REQ | ASR_DIR);
                    dev->state = STATE_IDLE;
                    set_intr(dev, 1);
                }
            }
            break;

        default:
            fatal("mfm_readw port=%04x\n", port);
    }

    st506_mca_log("mfm_readw port=%04x, ret=%04x.\n", port, ret);
    return ret;
}

static void
mfm_writew(uint16_t port, uint16_t val, void *priv)
{
    mfm_t *dev = (mfm_t *) priv;

    st506_mca_log("ST506: wrw(%04x, %04x)\n", port & 7, val);

    switch (port & 7) {
        case 0: /* DATA register */
            if (dev->state == STATE_RDATA) {
                if (dev->buf_idx >= dev->buf_len) {
                    st506_mca_log("ST506: write with full buffer!\n");
                    dev->status &= ~(ASR_TX_EN | ASR_DATA_REQ);
                    dev->intstat |= ISR_INVALID_CMD;
                    set_intr(dev, 1);
                    break;
                }

                /* Store the data into the buffer. */
                st506_mca_log("dev->buf_ptr[%02X] = %02X\n", dev->buf_idx, val & 0xff);
                dev->buf_ptr[dev->buf_idx++] = val & 0xff;
                st506_mca_log("dev->buf_ptr[%02X] = %02X\n", dev->buf_idx, val >> 8);
                dev->buf_ptr[dev->buf_idx++] = val >> 8;

                if (dev->buf_idx >= dev->buf_len) {
                    /* We got all the data we need. */
                    dev->status &= ~(ASR_TX_EN | ASR_DATA_REQ);
                    dev->state = STATE_IDLE;
                    set_intr(dev, 1);

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
                        timer_set_delay_u64(&dev->timer, MFM_TIME);
                    }
                }
            }
            break;

        default:
            fatal("mfm_writew port=%04x val=%04x\n", port, val);
    }
}

static uint8_t
mfm_mca_read(int port, void *priv)
{
    const mfm_t *dev = (mfm_t *) priv;

    st506_mca_log("ST506: mcard(%04x)\n", port);

    return (dev->pos_regs[port & 7]);
}

static void
mfm_mca_write(int port, uint8_t val, void* priv)
{
    mfm_t *dev = (mfm_t *) priv;

    st506_mca_log("ST506: mcawr(%04x, %02x)  pos[2]=%02x pos[3]=%02x\n",
                 port, val, dev->pos_regs[2], dev->pos_regs[3]);

    if (port < 0x102)
        return;

    /* Save the new value. */
    dev->pos_regs[port & 7] = val;

    io_removehandler(dev->base, 5,
        mfm_read, mfm_readw, NULL, 
        mfm_write, mfm_writew, NULL, dev);

    /* Set up controller DMA channel. */
    switch (dev->pos_regs[3] & 0x0f) {
        case 0x03:
            dev->dma = 3;
            break;
        case 0x04:
            dev->dma = 4;
            break;
        case 0x05:
            dev->dma = 5;
            break;
        case 0x06:
            dev->dma = 6;
            break;
        case 0x07:
            dev->dma = 7;
            break;
        case 0x00:
            dev->dma = 0;
            break;
        case 0x01:
            dev->dma = 1;
            break;

        default:
            /* Unknown DMA channel? */
            dev->dma = 3;
            break;
    }

    /* Set up controller I/O address. */
    if (dev->pos_regs[2] & 0x02)
        dev->base = 0x0328;
    else
        dev->base = 0x0320;

    if (dev->pos_regs[2] & 1) {
        io_sethandler(dev->base, 5,
            mfm_read, mfm_readw, NULL, 
            mfm_write, mfm_writew, NULL, dev);
    }
}

static uint8_t
mfm_mca_feedb(void *priv)
{
    const mfm_t *dev = (mfm_t *) priv;

    return (dev->pos_regs[2] & 1);
}

static void *
mfm_init(UNUSED(const device_t *info))
{
    drive_t *drive;
    mfm_t   *dev;
    int      c;

    /* Allocate and initialize device block. */
    dev = calloc(1, sizeof(mfm_t));

    /* Set up initial controller parameters. */
    dev->base = 0x0320;
    dev->irq  = 14;
    dev->dma  = 3;

    /* Load any disks for this device class. */
    for (uint8_t i = 0; i < HDD_NUM; i++) {
        if (hdd[i].bus_type == HDD_BUS_MFM) {
            drive = &dev->drives[hdd[i].mfm_channel];

            if (!hdd_image_load(i)) {
                drive->present = 0;
                continue;
            }
            drive->id = i;

            /* These are the "hardware" parameters (from the image.) */
            drive->cfg_spt    = (uint8_t) (hdd[i].spt & 0xff);
            drive->cfg_hpc    = (uint8_t) (hdd[i].hpc & 0xff);
            drive->cfg_tracks = (uint16_t) hdd[i].tracks;

            /* Use them as "active" parameters until overwritten. */
            drive->spt    = drive->cfg_spt;
            drive->hpc    = drive->cfg_hpc;
            drive->tracks = drive->cfg_tracks;

            drive->hdd_num = i;
            drive->present = 1;

            /* Create a valid SSB. */
            memset(&dev->ssb, 0x00, sizeof(dev->ssb));
            dev->ssb.sect_size = 0x02; /* 512 bytes */

            st506_mca_log("ST506: drive%d: cyl=%d,hd=%d,spt=%d, disk %d\n",
                           hdd[i].mfm_channel, drive->tracks, drive->hpc, drive->spt, i);
        }
    }

    /* Set the MCA ID for this controller. */
    dev->pos_regs[0] = 0xfd;
    dev->pos_regs[1] = 0xdf;

    /* Sectors are 1-based. */
    dev->sector = 1;

    /* Enable the I/O block. */
    int slotno = device_get_config_int("in_mfm_slot");
    if (slotno)
        mca_add_to_slot(mfm_mca_read, mfm_mca_write, mfm_mca_feedb, NULL, dev, slotno - 1);
    else
        mca_add(mfm_mca_read, mfm_mca_write, mfm_mca_feedb, NULL, dev);

    /* Create a timer for command delays. */
    timer_add(&dev->timer, hdc_callback, dev, 0);

    return dev;
}

static void
mfm_close(void *priv)
{
    mfm_t         *dev = (mfm_t *) priv;
    const drive_t *drive;

    /* Remove the I/O handler. */
    io_removehandler(dev->base, 5,
                     mfm_read, mfm_readw, NULL, mfm_write, mfm_writew, NULL, dev);

    /* Close all disks and their images. */
    for (uint8_t d = 0; d < MFM_NUM; d++) {
        drive = &dev->drives[d];

        if (drive->present)
            hdd_image_close(drive->hdd_num);
    }

    /* Release the device. */
    free(dev);
}

static device_config_t mfm_ps2_config[] = {
    {
        .name        = "in_mfm_slot",
        .description = "Slot #",
        .type        = CONFIG_SELECTION,
        .selection   = {
            { .description = "Auto", .value = 0 },
            { .description = "1",    .value = 1 },
            { .description = "2",    .value = 2 },
            { .description = "3",    .value = 3 },
            { .description = "4",    .value = 4 },
            { .description = "5",    .value = 5 },
            { .description = "6",    .value = 6 },
            { .description = "7",    .value = 7 },
            { .description = "8",    .value = 8 }
        },
        .default_int = 0
    },
    { .type = -1 }
};

const device_t st506_ps2_device = {
    .name          = "IBM PS/2 ST506 Fixed Disk Adapter (MCA)",
    .internal_name = "st506_mca",
    .flags         = DEVICE_MCA,
    .local         = 0,
    .init          = mfm_init,
    .close         = mfm_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = mfm_ps2_config
};