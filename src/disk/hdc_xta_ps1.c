/*
 * VARCem   Virtual ARchaeological Computer EMulator.
 *          An emulator of (mostly) x86-based PC systems and devices,
 *          using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *          spanning the era between 1981 and 1995.
 *
 *          Implementation of the IBM DBA fixed-disk attachment used by the
 *          PS/1 Model 2011 and selected PS/2 systems.
 *
 *          XTA is the acronym for 'XT-Attached', which was basically
 *          the XT-counterpart to what we know now as IDE (which is
 *          also named ATA - AT Attachment.)  The basic ideas was to
 *          put the actual drive controller electronics onto the drive
 *          itself, and have the host machine just talk to that using
 *          a simple, standardized I/O path- hence the name IDE, for
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
 *          different ways to achieve their goal.
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
#include "cpu.h"

#define HDC_TIME         (100 * TIMER_USEC)
#define HDC_SECTOR_TIME  (250 * TIMER_USEC)
#define HDC_TYPE_USER 47 /* user drive type */

#define HDC_SECTOR_SIZE       512
#define HDC_CHECK_SIZE          6
#define HDC_EXTENDED_SIZE     (HDC_SECTOR_SIZE + HDC_CHECK_SIZE)
#define HDC_CODEWORD_BITS     (HDC_EXTENDED_SIZE * 8)

#define HDC_ECC48_MASK UINT64_C(0x0000ffffffffffff)
#define HDC_ECC48_POLY UINT64_C(0x0000102100011021)
#define HDC_ECC48_INIT UINT64_C(0x0000752f00008ad0)

enum {
    HDC_VARIANT_PS1 = 0,
    HDC_VARIANT_PS2_M25
};

enum {
    STATE_IDLE = 0,
    STATE_RECV,
    STATE_RDATA,
    STATE_RDONE,
    STATE_SEND,
    STATE_SDATA,
    STATE_SDONE,
    STATE_FINIT,
    STATE_FDONE,
    STATE_WDMA
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
#define ACR_DMA_EN 0x01 /* DMA transfer enable */
#define ACR_INT_EN 0x02 /* interrupt enable */
#define ACR_RESET  0x80 /* reset */

/* Interrupt Status register (4R) values (IBM PS/1 2011.) */
#define ISR_EQUIP_CHECK 0x01 /* internal hardware error */
#define ISR_ERP_INVOKED 0x02 /* error recovery invoked */
#define ISR_CMD_REJECT  0x20 /* command reject */
#define ISR_INVALID_CMD 0x40 /* invalid command */
#define ISR_TERMINATION 0x80 /* termination error */

/* Attention register (4W) values (IBM PS/1 2011.) */
#define ATT_ABRT 0x01 /* abort last command */
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
 */
#pragma pack(push, 1)
typedef struct ssb_t {
    /* Status byte 0. */
    uint8_t track_0      : 1; /* T0           */
    uint8_t mbz1         : 1; /* 0            */
    uint8_t mbz2         : 1; /* 0            */
    uint8_t cylinder_err : 1; /* CE           */
    uint8_t write_fault  : 1; /* WF           */
    uint8_t mbz3         : 1; /* 0            */
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

    uint8_t drive_type; /* drive type */

    uint8_t rsvd; /* reserved byte */
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
 */
#pragma pack(push, 1)
typedef struct ccb_t {
    uint8_t ec_p      : 1; /* EC/P (ecc/park) */
    uint8_t mbz1      : 1; /* 0               */
    uint8_t auto_seek : 1; /* AS (auto-seek)  */
    uint8_t no_data   : 1; /* ND (no data)    */
    uint8_t cmd       : 4; /* command code[4] */

    uint8_t cyl_high : 2; /* cylinder [9:8] bits */
    uint8_t mbz2     : 2; /* 00                  */
    uint8_t head     : 4; /* head number         */

    uint8_t cyl_low; /* cylinder [7:0] bits */

    uint8_t sector; /* sector number */

    uint8_t mbz3 : 1; /* 0      */
    uint8_t mbo1 : 1; /* 1      */
    uint8_t mbz4 : 6; /* 000000 */

    uint8_t count; /* blk count/interleave */
} ccb_t;
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

/*
 * Flat sector images do not contain the six raw CRC/ECC bytes present on
 * the media.  Remember only sectors whose check field differs from the
 * normal 48-bit ECC generated from their data (normally after WRITE EXT).
 */
typedef struct raw_check_t {
    off64_t             addr;
    uint8_t             bytes[HDC_CHECK_SIZE];
    struct raw_check_t *next;
} raw_check_t;

typedef struct hdc_t {
    uint16_t base; /* controller base I/O address */
    int8_t   irq;  /* controller IRQ channel */
    int8_t   dma;  /* controller DMA channel */
    uint8_t  variant;

    /* Registers. */
    uint8_t attn;    /* ATTENTION register */
    uint8_t ctrl;    /* Control register (ACR) */
    uint8_t status;  /* Status register (ASR) */
    uint8_t intstat; /* Interrupt Status register (ISR) */

    uint8_t *reg_91; /* handle to system board's register 0x91 */

    /* Controller state. */
    pc_timer_t timer;
    int8_t     state; /* controller state */
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
    uint16_t track;  /* requested track# */
    uint8_t  head;   /* requested head# */
    uint8_t  sector; /* requested sector# */
    int count;       /* requested sector count */

    drive_t drives[XTA_NUM]; /* the attached drive(s) */

    raw_check_t *raw_checks;

    uint8_t data[HDC_EXTENDED_SIZE];       /* data buffer */
    uint8_t sector_buf[HDC_EXTENDED_SIZE]; /* sector buffer */
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
    {    0,     0,       0,          0,      0    },    /*  0    (none)   */
    {  306,     4,      17,        128,    305    },    /*  1    10 MB    */
    {  615,     4,      17,        300,    615    },    /*  2    20 MB    */
    {  615,     6,      17,        300,    615    },    /*  3    31 MB    */
    {  940,     8,      17,        512,    940    },    /*  4    62 MB    */
    {  940,     6,      17,        512,    940    },    /*  5    47 MB    */
    {  615,     4,      17,         -1,    615    },    /*  6    20 MB    */
    {  462,     8,      17,        256,    511    },    /*  7    31 MB    */
    {  733,     5,      17,         -1,    733    },    /*  8    30 MB    */
    {  900,    15,      17,         -1,    901    },    /*  9    112 MB   */
    {  820,     3,      17,         -1,    820    },    /* 10    20 MB    */
    {  855,     5,      17,         -1,    855    },    /* 11    35 MB    */
    {  855,     7,      17,         -1,    855    },    /* 12    50 MB    */
    {  306,     8,      17,        128,    319    },    /* 13    20 MB    */
    {  733,     7,      17,         -1,    733    },    /* 14    43 MB    */
    {    0,     0,       0,          0,      0    },    /* 15    (rsvd)   */
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
    {  921,     2,      33,         -1,   1000    },    /* 35    30 MB *  */
    {  402,     4,      26,         -1,    460    },    /* 36    20 MB    */
    {  580,     6,      26,         -1,    640    },    /* 37    44 MB    */
    {  845,     2,      36,         -1,   1023    },    /* 38    30 MB *  */
    {  769,     3,      36,         -1,   1023    },    /* 39    41 MB *  */
    {  531,     4,      39,         -1,    532    },    /* 40    40 MB    */
    {  577,     2,      36,         -1,   1023    },    /* 41    20 MB    */
    {  654,     2,      32,         -1,    674    },    /* 42    20 MB    */
    {  923,     5,      36,         -1,   1023    },    /* 43    81 MB    */
    {  531,     8,      39,         -1,    532    }     /* 44    81 MB    */
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

/*
 * The IBM DBA drive uses the 48-bit generator
 *
 *   x^48 + x^44 + x^37 + x^32 + x^16 + x^12 + x^5 + 1
 *
 * and sends the remainder most-significant byte first.  The non-zero
 * initial state includes the contribution of the on-disk framing that is
 * outside the 512-byte sector exposed to the host.
 */
static uint64_t
ecc48_generate(const uint8_t *data)
{
    uint64_t rem = HDC_ECC48_INIT;

    for (unsigned i = 0; i < HDC_SECTOR_SIZE; i++) {
        rem ^= (uint64_t) data[i] << 40;
        for (unsigned bit = 0; bit < 8; bit++) {
            const int carry = !!(rem & UINT64_C(0x0000800000000000));

            rem = (rem << 1) & HDC_ECC48_MASK;
            if (carry)
                rem ^= HDC_ECC48_POLY;
        }
    }

    return rem;
}

static uint16_t
crc16_generate(const uint8_t *data)
{
    uint16_t rem = 0xffff;

    /* CRC sectors append two zero bytes before taking the remainder. */
    for (unsigned i = 0; i < HDC_SECTOR_SIZE + 2; i++) {
        const uint8_t byte = (i < HDC_SECTOR_SIZE) ? data[i] : 0;

        rem ^= (uint16_t) byte << 8;
        for (unsigned bit = 0; bit < 8; bit++)
            rem = (rem & 0x8000) ? (uint16_t) ((rem << 1) ^ 0x1021)
                                 : (uint16_t) (rem << 1);
    }

    return rem;
}

static void
make_check_bytes(const uint8_t *data, const int ecc, uint8_t bytes[HDC_CHECK_SIZE])
{
    memset(bytes, 0, HDC_CHECK_SIZE);

    if (ecc) {
        const uint64_t value = ecc48_generate(data);

        for (unsigned i = 0; i < HDC_CHECK_SIZE; i++)
            bytes[i] = (uint8_t) (value >> (40 - (i * 8)));
    } else {
        const uint16_t value = crc16_generate(data);

        bytes[0] = (uint8_t) (value >> 8);
        bytes[1] = (uint8_t) value;
    }
}

static raw_check_t **
find_raw_check(hdc_t *dev, const off64_t addr)
{
    raw_check_t **link = &dev->raw_checks;

    while ((*link != NULL) && ((*link)->addr != addr))
        link = &(*link)->next;

    return link;
}

static void
remove_raw_check(hdc_t *dev, const off64_t addr)
{
    raw_check_t **link = find_raw_check(dev, addr);

    if (*link != NULL) {
        raw_check_t *entry = *link;

        *link = entry->next;
        free(entry);
    }
}

static int
set_raw_check(hdc_t *dev, const off64_t addr, const uint8_t *data,
              const uint8_t bytes[HDC_CHECK_SIZE])
{
    uint8_t       normal[HDC_CHECK_SIZE];
    raw_check_t **link;

    /* A normal ECC field can always be reconstructed from the image. */
    make_check_bytes(data, 1, normal);
    if (!memcmp(bytes, normal, HDC_CHECK_SIZE)) {
        remove_raw_check(dev, addr);
        return 1;
    }

    link = find_raw_check(dev, addr);
    if (*link == NULL) {
        *link = calloc(1, sizeof(raw_check_t));
        if (*link == NULL)
            return 0;
        (*link)->addr = addr;
    }

    memcpy((*link)->bytes, bytes, HDC_CHECK_SIZE);
    return 1;
}

static void
get_raw_check(hdc_t *dev, const off64_t addr, const uint8_t *data,
              uint8_t bytes[HDC_CHECK_SIZE])
{
    raw_check_t **link = find_raw_check(dev, addr);

    if (*link != NULL)
        memcpy(bytes, (*link)->bytes, HDC_CHECK_SIZE);
    else
        make_check_bytes(data, 1, bytes);
}

static void
remove_raw_check_range(hdc_t *dev, const off64_t first, const off64_t count)
{
    raw_check_t **link = &dev->raw_checks;
    const off64_t end  = first + count;

    while (*link != NULL) {
        raw_check_t *entry = *link;

        if ((entry->addr >= first) && (entry->addr < end)) {
            *link = entry->next;
            free(entry);
        } else {
            link = &entry->next;
        }
    }
}

static void
free_raw_checks(hdc_t *dev)
{
    raw_check_t *entry = dev->raw_checks;

    while (entry != NULL) {
        raw_check_t *next = entry->next;

        free(entry);
        entry = next;
    }
    dev->raw_checks = NULL;
}

static uint32_t
rotate_right_32(const uint32_t value, const unsigned count)
{
    return count ? ((value >> count) | (value << (32 - count))) : value;
}

/*
 * The generator factors as (x^32 + 1)(x^16 + x^12 + x^5 + 1), a
 * single-burst Fire code.  Reducing the syndrome modulo x^32 + 1 reveals
 * the error pattern and its position modulo 32; the full syndrome then
 * locates a unique burst of up to 16 bits in the 4144-bit codeword.
 */
static int
ecc48_correct(uint8_t *data, const uint64_t syndrome)
{
    static uint64_t x_power[HDC_CODEWORD_BITS];
    static int      powers_ready = 0;
    uint32_t        folded;
    uint32_t        found_pattern = 0;
    unsigned        found_pos = 0;
    unsigned        matches = 0;

    if (!powers_ready) {
        x_power[0] = 1;
        for (unsigned i = 1; i < HDC_CODEWORD_BITS; i++) {
            const uint64_t prev = x_power[i - 1];

            x_power[i] = (prev << 1) & HDC_ECC48_MASK;
            if (prev & UINT64_C(0x0000800000000000))
                x_power[i] ^= HDC_ECC48_POLY;
        }
        powers_ready = 1;
    }

    folded = (uint32_t) syndrome ^ (uint32_t) (syndrome >> 32);

    for (unsigned residue_pos = 0; residue_pos < 32; residue_pos++) {
        const uint32_t pattern = rotate_right_32(folded, residue_pos);
        unsigned       burst_len;

        if ((pattern == 0) || (pattern & 0xffff0000U) || !(pattern & 1))
            continue;

        burst_len = 16;
        while (!(pattern & (UINT32_C(1) << (burst_len - 1))))
            burst_len--;

        for (unsigned pos = residue_pos;
             (pos + burst_len) <= HDC_CODEWORD_BITS; pos += 32) {
            uint64_t candidate = 0;

            for (unsigned bit = 0; bit < burst_len; bit++)
                if (pattern & (UINT32_C(1) << bit))
                    candidate ^= x_power[pos + bit];

            if (candidate == syndrome) {
                found_pattern = pattern;
                found_pos     = pos;
                if (++matches > 1)
                    return 0;
            }
        }
    }

    if (matches != 1)
        return 0;

    for (unsigned bit = 0; bit < 16; bit++) {
        const unsigned exponent = found_pos + bit;

        if (!(found_pattern & (UINT32_C(1) << bit)))
            continue;

        /* Exponents 47 through 0 are the six check bytes, not data. */
        if (exponent >= 48) {
            const unsigned stream_bit = (HDC_CODEWORD_BITS - 1) - exponent;

            if (stream_bit < (HDC_SECTOR_SIZE * 8))
                data[stream_bit >> 3] ^= (uint8_t) (0x80U >> (stream_bit & 7));
        }
    }

    return 1;
}

/* Return 0 for clean data, 1 for corrected data, and -1 if uncorrectable. */
static int
check_sector_data(hdc_t *dev, const off64_t addr, uint8_t *data, const int ecc)
{
    uint8_t stored[HDC_CHECK_SIZE];
    uint8_t expected[HDC_CHECK_SIZE];

    get_raw_check(dev, addr, data, stored);
    make_check_bytes(data, ecc, expected);

    if (!memcmp(stored, expected, ecc ? HDC_CHECK_SIZE : 2))
        return 0;

    if (ecc) {
        uint64_t stored_value   = 0;
        uint64_t expected_value = 0;

        for (unsigned i = 0; i < HDC_CHECK_SIZE; i++) {
            stored_value   = (stored_value << 8) | stored[i];
            expected_value = (expected_value << 8) | expected[i];
        }

        if (ecc48_correct(data, stored_value ^ expected_value)) {
            make_check_bytes(data, 1, expected);
            if (!memcmp(stored, expected, HDC_CHECK_SIZE))
                return 1;
        }
    }

    return -1;
}

/* FIXME: we should use the disk/hdd_table.c code with custom tables! */
static int
ibm_drive_type(drive_t *drive)
{
    const geom_t *ptr;

    for (uint16_t i = 0; i < (sizeof(ibm_type_table) / sizeof(geom_t)); i++) {
        ptr = &ibm_type_table[i];
        if ((drive->tracks == ptr->cyl) && (drive->hpc == ptr->hpc) && (drive->spt == ptr->spt))
            return i;
    }

    return HDC_TYPE_USER;
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

static void
clear_unused_format(hdc_t *dev)
{
    /*
     * A count of 11h is also a normal full-track transfer on 17-sector
     * drives.  The OS/2 format workaround must never complete a READ
     * VERIFY command early.
     */
    if (((dev->ccb.cmd == CMD_FORMAT_DRIVE) ||
         (dev->ccb.cmd == CMD_FORMAT_TRACK)) &&
        (dev->ccb.count == 0x11)) /* OS/2 format */
        if (CS != 0xe000) /* ROM POST */
            dev->status &= ~ASR_BUSY;
}

/* Get the logical (block) address of a CHS triplet. */
static int
get_sector(hdc_t *dev, drive_t *drive, off64_t *addr)
{
    if (drive->cur_cyl != dev->track) {
        ps1_hdc_log("HDC: get_sector: wrong cylinder %d/%d\n",
                    drive->cur_cyl, dev->track);
        dev->ssb.wrong_cyl = 1;
        return 1;
    }

    if (dev->head >= drive->hpc) {
        ps1_hdc_log("HDC: get_sector: past end of heads\n");
        dev->ssb.cylinder_err = 1;
        return 1;
    }

    if ((dev->sector == 0) || (dev->sector > drive->spt)) {
        ps1_hdc_log("HDC: get_sector: invalid sector %u\n", dev->sector);
        dev->ssb.mark_not_found = 1;
        return 1;
    }

    /* Calculate logical address (block number) of desired sector. */
    *addr = ((((off64_t) dev->track * drive->hpc) + dev->head) * drive->spt) + dev->sector - 1;

    return 0;
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
    /* A completed, aborted, or rejected command no longer owns DMA 3. */
    dma_set_drq(dev->dma, 0);

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
        return 1;
    }

    dev->track     = cyl;
    drive->cur_cyl = dev->track;

    return 0;
}

/* Format a track or an entire drive. */
static void
do_format(hdc_t *dev, drive_t *drive, ccb_t *ccb)
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
            ;
            const unsigned fcb_len = (unsigned) ccb->count * sizeof(fcb_t);

            /* Reject malformed FCB transfers before they can overrun data[]. */
            if ((fcb_len == 0) ||
                (((fcb_len + 1) & ~1U) > sizeof(dev->data))) {
                dev->intstat |= ISR_CMD_REJECT;
                do_finish(dev);
                return;
            }

            /* Ready to transfer the FCB data in. */
            dev->state   = STATE_RDATA;
            dev->buf_idx = 0;
            dev->buf_ptr = dev->data;
            dev->buf_len = (int16_t) fcb_len;
            if (dev->buf_len & 1)
                dev->buf_len++; /* must be even */

            /* Enable for PIO or DMA, as needed. */
            if (dev->ctrl & ACR_DMA_EN) {
                dma_set_drq(dev->dma, 1);
                dev->state   = STATE_WDMA;
            }

            dev->status |= ASR_DATA_REQ;
            break;

        case STATE_WDMA:
            if (dma_channel_readable(dev->dma))
                dev->state   = STATE_RDATA;
            else {
                /* Waiting for DMA to start. */
                ps1_hdc_log("Format: DMA channel not yet readable...\n");
            }
            timer_advance_u64(&dev->timer, HDC_TIME);
            return;

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
            dma_set_drq(dev->dma, 0);
            dev->state = STATE_RDONE;
            timer_advance_u64(&dev->timer, HDC_TIME);
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
            ui_sb_update_icon_write(SB_HDD | HDD_BUS_XTA, 1);

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

            if (ccb->ec_p) {
                remove_raw_check_range(dev, addr, drive->spt);
            } else {
                uint8_t check_bytes[HDC_CHECK_SIZE];

                memset(dev->sector_buf, 0, HDC_SECTOR_SIZE);
                make_check_bytes(dev->sector_buf, 0, check_bytes);
                for (val = 0; val < drive->spt; val++) {
                    if (!set_raw_check(dev, addr + val, dev->sector_buf,
                                       check_bytes)) {
                        dev->intstat |= ISR_EQUIP_CHECK | ISR_TERMINATION;
                        dev->ssb.need_reset = 1;
                        intr = 1;
                        break;
                    }
                }
                if (intr)
                    break;
            }

            /* Done with this track. */
            dev->state = STATE_FDONE;
            fallthrough;
        case STATE_FDONE:
            /* One more track done. */
            if (++start_cyl == end_cyl) {
                intr = 1;
                break;
            }

            if (dev->ctrl & ACR_DMA_EN)
                dma_set_drq(dev->dma, 0);

            /* De-activate the status icon. */
            ui_sb_update_icon_write(SB_HDD | HDD_BUS_XTA, 0);

            /* This saves us a LOT of code. */
            dev->state = STATE_FINIT;
            goto do_fmt;

        default:
            break;
    }

    /* If we errored out, go back idle. */
    if (intr) {
        /* De-activate the status icon. */
        ui_sb_update_icon(SB_HDD | HDD_BUS_XTA, 0);
        ui_sb_update_icon_write(SB_HDD | HDD_BUS_XTA, 0);

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
    uint8_t  check_bytes[HDC_CHECK_SIZE];
    off64_t  addr;
    int      ecc_result;
    int      val;
#ifdef ENABLE_PS1_HDC_LOG
    uint8_t  cmd = ccb->cmd & 0x0f;
#endif

    /* Abort last command if requested. */
    if (dev->abort) {
        ps1_hdc_log("XTA command abort.\n");
        dev->abort = 0;
        do_finish(dev);
        return;
    }

    /* Clear command status once, not again during later transfer states. */
    if (dev->state == STATE_IDLE) {
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
        dev->ssb.sect_corr      = 0;
        dev->ssb.retries        = 0;
        dev->ssb.valid          = 1;
    }

    /* We really only support one drive, but ohwell. */
    drive = &dev->drives[0];

    /* If we are returning from a RESET, handle this first. */
    if (dev->reset) {
        ps1_hdc_log("XTA reset.\n");
        dev->ssb.valid = 0;
        /* Reset completion must not report a preceding command's ISR bits. */
        dev->intstat = 0;
        dev->reset = 0;
        dma_set_drq(dev->dma, 0);
        do_finish(dev);
        return;
    }

    ps1_hdc_log("hdc_callback(0): %02X\n", cmd);

    switch (ccb->cmd) {
        case CMD_READ_EXT:
        case CMD_READ_VERIFY:
        case CMD_READ_SECTORS:
            if (ccb->cmd == CMD_READ_VERIFY) {
                ccb->no_data = 1;
            }

            if (!drive->present) {
                dev->ssb.not_ready = 1;
                do_finish(dev);
                return;
            }

            if (!(dev->ready | ccb->no_data)) {
                /* Delay a bit, transfer not ready. */
                timer_advance_u64(&dev->timer, HDC_SECTOR_TIME);
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
                    dev->count = (ccb->cmd == CMD_READ_EXT) ? 1
                                                            : (int) ccb->count;
                    dev->buf_len = (ccb->cmd == CMD_READ_EXT)
                                     ? HDC_EXTENDED_SIZE
                                     : (128 << dev->ssb.sect_size);

                    dev->state = STATE_SEND;
                    fallthrough;

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

                    if (ccb->cmd == CMD_READ_EXT) {
                        /* READ EXT bypasses checking and returns all six bytes. */
                        get_raw_check(dev, addr, dev->sector_buf,
                                      &dev->sector_buf[HDC_SECTOR_SIZE]);
                    } else {
                        ecc_result = check_sector_data(dev, addr,
                                                       dev->sector_buf,
                                                       ccb->ec_p);
                        if (ecc_result < 0) {
                            ui_sb_update_icon(SB_HDD | HDD_BUS_XTA, 0);
                            dev->intstat |= ISR_ERP_INVOKED | ISR_TERMINATION;
                            dev->ssb.ecc_crc_err = 1;
                            dev->ssb.cmd_syndrome = 0x74;
                            dev->ssb.seek_end = 1;
                            do_finish(dev);
                            return;
                        }
                        if (ecc_result > 0) {
                            dev->intstat |= ISR_ERP_INVOKED;
                            dev->ssb.sect_corr++;
                        }
                    }

                    /* Ready to transfer the data out. */
                    dev->state   = STATE_SDATA;
                    dev->buf_idx = 0;
                    if (ccb->no_data) {
                        /* Delay a bit, no actual transfer. */
                        timer_advance_u64(&dev->timer, HDC_TIME);
                    } else {
                        if (dev->ctrl & ACR_DMA_EN) {
                            /* DMA enabled. */
                            dev->buf_ptr = dev->sector_buf;
                            dev->state   = STATE_WDMA;
                            dma_set_drq(dev->dma, 1);
                            timer_advance_u64(&dev->timer, HDC_SECTOR_TIME);
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

                case STATE_WDMA:
                    if (dma_channel_writable(dev->dma))
                        dev->state   = STATE_SDATA;
                    else {
                        /* Waiting for DMA to start. */
                        ps1_hdc_log("Read sectors: DMA channel not yet writable...\n");
                    }
                    timer_advance_u64(&dev->timer, HDC_TIME);
                    return;

                case STATE_SDATA:
                    if (!ccb->no_data) {
                        /* Perform DMA. */
                        while (dev->buf_idx < dev->buf_len) {
                            val = dma_channel_write(dev->dma,
                                                    dev->buf_ptr[dev->buf_idx]);
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
                    dma_set_drq(dev->dma, 0);
                    timer_advance_u64(&dev->timer, HDC_TIME);
                    break;

                case STATE_SDONE:
                    dev->buf_idx = 0;
                    if (--dev->count == 0) {
                        /* De-activate the status icon. */
                        ui_sb_update_icon(SB_HDD | HDD_BUS_XTA, 0);

                        if (dev->ctrl & ACR_DMA_EN)
                            dma_set_drq(dev->dma, 0);
                        else
                            dev->status &= ~(ASR_DATA_REQ | ASR_DIR);
                        dev->ssb.cmd_syndrome = dev->ssb.sect_corr ? 0xF4
                                                                   : 0xD4;
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
                    ui_sb_update_icon(SB_HDD | HDD_BUS_XTA, 1);

                    /* Ready to transfer the data out. */
                    dev->state = STATE_SDONE;
                    dev->buf_idx = 0;
                    /* Delay a bit, no actual transfer. */
                    timer_advance_u64(&dev->timer, HDC_TIME);
                    break;

                case STATE_SDONE:
                    dev->buf_idx = 0;

                    /* De-activate the status icon. */
                    ui_sb_update_icon(SB_HDD | HDD_BUS_XTA, 0);

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

        case CMD_RECALIBRATE: /* RECALIBRATE */
            if (drive->present) {
                dev->track = drive->cur_cyl = 0;
                dev->ssb.seek_end = 1;
            } else {
                dev->ssb.not_ready = 1;
                dev->intstat |= ISR_TERMINATION;
            }

            do_finish(dev);
            break;

        case CMD_WRITE_EXT:
        case CMD_WRITE_VERIFY:
        case CMD_WRITE_SECTORS:
            if (!drive->present) {
                dev->ssb.not_ready = 1;
                do_finish(dev);
                return;
            }

            if (!(dev->ready | ccb->no_data)) {
                /* Delay a bit, transfer not ready. */
                timer_advance_u64(&dev->timer, HDC_SECTOR_TIME);
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
                    dev->count = (ccb->cmd == CMD_WRITE_EXT) ? 1
                                                             : (int) ccb->count;
                    dev->buf_len = (ccb->cmd == CMD_WRITE_EXT)
                                     ? HDC_EXTENDED_SIZE
                                     : (128 << dev->ssb.sect_size);

                    dev->state = STATE_RECV;
                    fallthrough;

                case STATE_RECV:
                    /* Activate the status icon. */
                    ui_sb_update_icon_write(SB_HDD | HDD_BUS_XTA, 1);
do_recv:
                    /* Ready to transfer the data in. */
                    dev->state   = STATE_RDATA;
                    dev->buf_idx = 0;
                    if (ccb->no_data) {
                        /* Delay a bit, no actual transfer. */
                        timer_advance_u64(&dev->timer, HDC_TIME);
                    } else {
                        if (dev->ctrl & ACR_DMA_EN) {
                            /* DMA enabled. */
                            dev->buf_ptr = dev->sector_buf;
                            dev->state   = STATE_WDMA;
                            dma_set_drq(dev->dma, 1);
                            timer_advance_u64(&dev->timer, HDC_SECTOR_TIME);
                        } else {
                            /* No DMA, do PIO. */
                            dev->buf_ptr = dev->data;
                            dev->status |= ASR_DATA_REQ;
                        }
                    }
                    break;

                case STATE_WDMA:
                    if (dma_channel_readable(dev->dma))
                        dev->state   = STATE_RDATA;
                    else {
                        /* Waiting for DMA to start. */
                        ps1_hdc_log("Write sectors: DMA channel not yet readable...\n");
                    }
                    timer_advance_u64(&dev->timer, HDC_TIME);
                    return;

                case STATE_RDATA:
                    if (!ccb->no_data) {
                        /* Perform DMA. */
                        while (dev->buf_idx < dev->buf_len) {
                            val = dma_channel_read(dev->dma);
                            if (val == DMA_NODATA) {
                                ps1_hdc_log("HDC: CMD_WRITE_SECTORS out of data (idx=%d, len=%d)!\n", dev->buf_idx, dev->buf_len);

                                /* De-activate the status icon. */
                                ui_sb_update_icon_write(SB_HDD | HDD_BUS_XTA, 0);

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
                    timer_advance_u64(&dev->timer, HDC_TIME);
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
                        ui_sb_update_icon_write(SB_HDD | HDD_BUS_XTA, 0);
                        do_finish(dev);
                        return;
                    }

                    /*
                     * WRITE EXT supplies the six physical check bytes and
                     * deliberately bypasses error checking.  Normal ECC
                     * writes return the image sector to its implicit,
                     * reconstructible check field.
                     */
                    if (ccb->cmd == CMD_WRITE_EXT) {
                        if (!set_raw_check(dev, addr, dev->sector_buf,
                                           &dev->sector_buf[HDC_SECTOR_SIZE])) {
                            ui_sb_update_icon_write(SB_HDD | HDD_BUS_XTA, 0);
                            dev->intstat |= ISR_EQUIP_CHECK | ISR_TERMINATION;
                            dev->ssb.need_reset = 1;
                            do_finish(dev);
                            return;
                        }
                    } else if (ccb->ec_p) {
                        remove_raw_check(dev, addr);
                    } else {
                        make_check_bytes(dev->sector_buf, 0, check_bytes);
                        if (!set_raw_check(dev, addr, dev->sector_buf,
                                           check_bytes)) {
                            ui_sb_update_icon_write(SB_HDD | HDD_BUS_XTA, 0);
                            dev->intstat |= ISR_EQUIP_CHECK | ISR_TERMINATION;
                            dev->ssb.need_reset = 1;
                            do_finish(dev);
                            return;
                        }
                    }

                    /* Flat images contain the 512-byte data field only. */
                    hdd_image_write(drive->hdd_num, addr, 1,
                                    (uint8_t *) dev->sector_buf);

                    dev->buf_idx = 0;
                    if (--dev->count == 0) {
                        /* De-activate the status icon. */
                        ui_sb_update_icon_write(SB_HDD | HDD_BUS_XTA, 0);

                        if (dev->ctrl & ACR_DMA_EN)
                            dma_set_drq(dev->dma, 0);
                        else
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
hdc_send_ssb(hdc_t *dev)
{
    const drive_t *drive;

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

    /* PS/1 and Model 30 use the register 91h card-selected feedback path. */
    if (dev->reg_91 != NULL)
        *dev->reg_91 |= 0x01;

    switch (port & 7) {
        case 0: /* DATA register */
            if (dev->state == STATE_SDATA) {
                if (dev->buf_idx >= dev->buf_len) {
                    ps1_hdc_log("HDC: read with empty buffer!\n");
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

    ps1_hdc_log("[%04X:%08X] [R] %04X = %02X\n", CS, cpu_state.pc, port, ret);

    return ret;
}

static void
hdc_write(uint16_t port, uint8_t val, void *priv)
{
    hdc_t *dev = (hdc_t *) priv;

    ps1_hdc_log("[%04X:%08X] [W] %04X = %02X\n", CS, cpu_state.pc, port, val);

    /* PS/1 and Model 30 use the register 91h card-selected feedback path. */
    if (dev->reg_91 != NULL)
        *dev->reg_91 |= 0x01;

    switch (port & 7) {
        case 0: /* DATA register */
            if (dev->state == STATE_RDATA) {
                if (dev->buf_idx >= dev->buf_len) {
                    ps1_hdc_log("HDC: write with full buffer!\n");
                    dev->status &= ~(ASR_TX_EN | ASR_DATA_REQ);
                    dev->intstat |= ISR_INVALID_CMD;
                    set_intr(dev, 1);
                    break;
                }

                /* Store the data into the buffer. */
                dev->buf_ptr[dev->buf_idx] = val;
                ps1_hdc_log("dev->buf_ptr[%02X] = %02X\n", dev->buf_idx, val);
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
                        clear_unused_format(dev);
                        timer_set_delay_u64(&dev->timer, HDC_SECTOR_TIME);
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
                timer_set_delay_u64(&dev->timer, HDC_TIME);
            }
            break;

        case 4: /* ATTN */
            dev->status &= ~ASR_INT_REQ;

            if (val & ATT_ABRT) {
                dev->abort = 1;
                dev->status &= ~ASR_BUSY;
                /* Schedule command execution. */
                timer_set_delay_u64(&dev->timer, HDC_TIME);
            }

            if (val & ATT_DATA)
                dev->ready = 1;
            else
                dev->ready = 0;

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
                if (val & ATT_DATA)
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
            break;

        default:
            break;
    }
}

void
ps1_hdc_handler(void *priv, const int set)
{
    hdc_t   *dev = (hdc_t *) priv;

    ps1_hdc_log("%sabling the fixed disk controller...\n", set ? "En" : "Dis");
    io_handler(set, dev->base, 5,
               hdc_read, NULL, NULL, hdc_write, NULL, NULL, dev);
}

static void *
ps1_hdc_init(const device_t *info)
{
    drive_t *drive;
    hdc_t   *dev;
    int      c;

    /* Allocate and initialize device block. */
    dev = calloc(1, sizeof(hdc_t));

    dev->variant = info->local;
    dev->base    = 0x0320;
    dev->irq     = (dev->variant == HDC_VARIANT_PS2_M25) ? 5 : 14;
    dev->dma     = 3;

    ps1_hdc_log("HDC: initializing (I/O=%04X, IRQ=%d, DMA=%d)\n",
                dev->base, dev->irq, dev->dma);

    /* Load any disks for this device class. */
    c = 0;
    for (uint8_t i = 0; i < HDD_NUM; i++) {
        if ((hdd[i].bus_type == HDD_BUS_XTA) && (hdd[i].xta_channel < 1)) {
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

    return dev;
}

static void
ps1_hdc_close(void *priv)
{
    hdc_t         *dev = (hdc_t *) priv;
    const drive_t *drive;

    timer_disable(&dev->timer);
    dma_set_drq(dev->dma, 0);
    set_intr(dev, 0);
    ui_sb_update_icon(SB_HDD | HDD_BUS_XTA, 0);
    ui_sb_update_icon_write(SB_HDD | HDD_BUS_XTA, 0);

    /* Remove the I/O handler. */
    io_removehandler(dev->base, 5,
                     hdc_read, NULL, NULL, hdc_write, NULL, NULL, dev);

    /* Close all disks and their images. */
    for (uint8_t d = 0; d < XTA_NUM; d++) {
        drive = &dev->drives[d];

        if (drive->present)
            hdd_image_close(drive->hdd_num);
    }

    free_raw_checks(dev);

    /* Release the device. */
    free(dev);
}

const device_t ps1_hdc_device = {
    .name          = "PS/1 2011 Fixed Disk Controller",
    .internal_name = "ps1_hdc",
    .flags         = DEVICE_ISA,
    .local         = HDC_VARIANT_PS1,
    .init          = ps1_hdc_init,
    .close         = ps1_hdc_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ps2_m25_hdc_device = {
    .name          = "IBM PS/2 Model 25 Fixed Disk Controller",
    .internal_name = "ps2_m25_hdc",
    .flags         = DEVICE_ISA | DEVICE_ONBOARD,
    .local         = HDC_VARIANT_PS2_M25,
    .init          = ps1_hdc_init,
    .close         = ps1_hdc_close,
    .reset         = NULL,
    .available     = NULL,
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
