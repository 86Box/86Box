/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Xebec S1410-compatible SASI target and line bus.
 *
 *          The register layout and the phase encoding below were recovered
 *          from the board's own 4 KiB BIOS (pc9801_27/sasi.rom) rather than
 *          guessed: every constant here is one the ROM tests for.  See
 *          PC98_SASI_FIX.txt for the disassembly the mapping came from.
 *
 *          This is the external SASI target, not the PC-9801-27 host adapter.
 *          The core is deliberately free of 86Box headers so the protocol
 *          can be driven directly from tests/test_pc98_sasi.c.  Media
 *          access goes through the callbacks in pc98_sasi_ops_t, which the
 *          86Box device fills in with hdd_image_read()/hdd_image_write()
 *          and the tests fill in with a RAM disk.
 */
#ifndef EMU_PC98_SASI_H
#define EMU_PC98_SASI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The PC-9801-27 SASI interface uses 256-byte logical blocks.  Port 82h
 * bits 7:6 are the *controller type* (CT1:CT0), not per-drive sector-size
 * flags.  NEC documents 00 specifically for PC-9801-27. */
#define PC98_SASI_BLOCK_SIZE_256 256u
#define PC98_SASI_BLOCK_SIZE_512 512u /* container compatibility only */
#define PC98_SASI_BLOCK_SIZE PC98_SASI_BLOCK_SIZE_256
#define PC98_SASI_LONG_ECC_SIZE 4u
#define PC98_SASI_LONG_BLOCK_SIZE (PC98_SASI_BLOCK_SIZE_256 + PC98_SASI_LONG_ECC_SIZE)
#define PC98_SASI_MAX_BLOCK_SIZE PC98_SASI_BLOCK_SIZE_256
#define PC98_SASI_MAX_LONG_BLOCK_SIZE PC98_SASI_LONG_BLOCK_SIZE
#define PC98_SASI_TRACK_BLOCKS_256 33u

#define PC98_SASI_MAX_UNITS  2u
#define PC98_SASI_CDB_LEN    6u

#define PC98_SASI_BUF_BLOCKS 256u
/* READ/WRITE LONG append four ECC bytes to every transferred sector. */
#define PC98_SASI_BUF_SIZE   (PC98_SASI_BUF_BLOCKS * PC98_SASI_MAX_LONG_BLOCK_SIZE)

/* Largest physical track count in the PC-9801-27 ROM geometry table. */
#define PC98_SASI_MAX_TRACKS 5312u
#define PC98_SASI_ECC_CACHE_ENTRIES 256u

/*
 * Port 82h read, bus-status mode (control bit 6 set).
 *
 * The BIOS masks bits 1:0 away (`and al,0fch`) before comparing a phase, so
 * only bits 7:2 take part in phase decoding.  The phase constants it
 * compares against are 0A0h, 0A4h, 0A8h, 0ACh and 0BCh, which decode
 * exactly as REQ|BSY plus the usual MSG/CD/IO triple.
 */
#define PC98_SASI_ST_REQ 0x80u /* target requests a byte                   */
#define PC98_SASI_ST_ACK 0x40u /* handshake in progress; BIOS spins on it  */
#define PC98_SASI_ST_BSY 0x20u /* a target is on the bus                   */
#define PC98_SASI_ST_MSG 0x10u
#define PC98_SASI_ST_CD  0x08u /* command/data                             */
#define PC98_SASI_ST_IO   0x04u /* 1 = to initiator                        */
#define PC98_SASI_ST_DMAE 0x02u /* DMA-related board state; BIOS masks it */
#define PC98_SASI_ST_INT  0x01u /* INT3 request from the interface        */
/* Historical name retained for source compatibility. */
#define PC98_SASI_ST_DRQ PC98_SASI_ST_DMAE

/*
 * Port 82h write (control).
 *
 * Bit 6 is a readback selector rather than a bus signal: with it set the
 * status port reports the bus, with it clear the port reports the board's
 * configuration/DIP byte.  The BIOS relies on both - it reads the bus after
 * `out 82h,40h` and the DIP switches after `out 82h,00h`.
 */
#define PC98_SASI_CTL_IEN 0x01u /* gate the board's INT3 completion IRQ   */
#define PC98_SASI_CTL_DMA 0x02u /* gate 8237 DMA during data phases       */
#define PC98_SASI_CTL_RST 0x08u /* bus reset pulse (08h then 00h)         */
#define PC98_SASI_CTL_SEL 0x20u /* assert SEL with the ID latched at 80h  */
#define PC98_SASI_CTL_STS 0x40u /* 1: status readback, 0: DIP readback    */
/* Bit 7 is set in the ROM's C0/C1/C3 active values, but the available
 * evidence does not require it to gate either IRQ or DMA. */
#define PC98_SASI_CTL_B7  0x80u

typedef enum pc98_sasi_phase_t {
    PC98_SASI_BUSFREE = 0,
    PC98_SASI_SELECT,
    PC98_SASI_COMMAND,
    PC98_SASI_DATAIN,
    PC98_SASI_DATAOUT,
    PC98_SASI_STATUS,
    PC98_SASI_MSGIN
} pc98_sasi_phase_t;

/* Target-driven SASI lines.  The adapter reads these lines; it does not
 * manufacture a status byte from knowledge of the target command phase. */
typedef struct pc98_sasi_lines_t {
    uint8_t bsy;
    uint8_t req;
    uint8_t msg;
    uint8_t cd;
    uint8_t io;
    uint8_t ack;
    uint8_t sel;
    uint8_t rst;
} pc98_sasi_lines_t;

/* SASI status byte, returned in the status phase. */
#define PC98_SASI_GOOD  0x00u
#define PC98_SASI_CHECK 0x02u

/* Sense keys as the PC-9801-27 BIOS interprets them. */
#define PC98_SASI_SENSE_NONE       0x00u
#define PC98_SASI_SENSE_NO_INDEX   0x01u
#define PC98_SASI_SENSE_SEEK_FAIL  0x02u
#define PC98_SASI_SENSE_NOT_READY  0x04u
#define PC98_SASI_SENSE_BAD_TRACK  0x19u
#define PC98_SASI_SENSE_DIRECT_ALT 0x1cu
#define PC98_SASI_SENSE_ALT_USED   0x1du
#define PC98_SASI_SENSE_ALT_BROKEN 0x1eu
#define PC98_SASI_SENSE_ALT_SAME   0x1fu
#define PC98_SASI_SENSE_BAD_CMD    0x20u
#define PC98_SASI_SENSE_BAD_LBA    0x21u
#define PC98_SASI_SENSE_BAD_CDB    0x22u

typedef enum pc98_sasi_dataout_action_t {
    PC98_SASI_DATAOUT_NONE = 0,
    PC98_SASI_DATAOUT_WRITE,
    PC98_SASI_DATAOUT_INIT_DRIVE,
    PC98_SASI_DATAOUT_FORMAT_ALT,
    PC98_SASI_DATAOUT_WRITE_BUFFER,
    PC98_SASI_DATAOUT_WRITE_LONG,
    PC98_SASI_DATAOUT_ASSIGN_DRIVE
} pc98_sasi_dataout_action_t;

typedef struct pc98_sasi_ops_t {
    /* Return non-zero on success. lba/count are always 256-byte SASI blocks. */
    int (*read_blocks)(void *priv, unsigned unit, uint32_t lba,
                       uint32_t count, uint8_t *buf);
    int (*write_blocks)(void *priv, unsigned unit, uint32_t lba,
                        uint32_t count, const uint8_t *buf);
    /* Total capacity in 256-byte SASI blocks; 0 if absent. */
    uint32_t (*capacity)(void *priv, unsigned unit);
    /* INT3 output.  The board asserts it in status phase when CTL_IEN is set. */
    void (*set_irq)(void *priv, int level);
    /*
     * Data-phase DMA.  Returns the number of bytes actually moved.  A
     * device whose 8237 channel is masked returns 0, which drops the core
     * back to the programmed-I/O handshake the BIOS also implements.
     */
    uint32_t (*dma_write)(void *priv, const uint8_t *buf, uint32_t len);
    uint32_t (*dma_read)(void *priv, uint8_t *buf, uint32_t len);
    void     *priv;
} pc98_sasi_ops_t;

typedef struct pc98_sasi_t {
    pc98_sasi_ops_t ops;

    uint8_t ctrl;    /* last value written to 82h                          */
    uint8_t dip;     /* configuration byte reported when CTL_STS is clear  */
    uint8_t data;    /* the 80h latch                                      */
    uint8_t int_pending;
    pc98_sasi_lines_t lines;

    pc98_sasi_phase_t phase;

    unsigned unit;   /* selected unit                                      */
    uint8_t  sel_id; /* ID bitmask latched at 80h before SEL               */

    uint8_t  cdb[PC98_SASI_CDB_LEN];
    unsigned cdb_pos;

    uint8_t  status;  /* status byte handed back in the status phase       */
    uint8_t  message; /* message byte handed back in the message phase     */
    uint8_t  sense[PC98_SASI_MAX_UNITS];
    uint8_t  sense_valid[PC98_SASI_MAX_UNITS];
    uint32_t sense_lba[PC98_SASI_MAX_UNITS];

    /* Xebec sector buffer.  PC-9801-27 exposes a 256-byte SASI block. */
    uint8_t  sector_buf[PC98_SASI_BLOCK_SIZE];

    /* S1410 Initialize Drive Characteristics state.  The -27 vendor C2h
     * setup remains separate because the option ROM sends both protocols. */
    uint16_t drive_cylinders;
    uint16_t reduced_write_cylinder;
    uint16_t precomp_cylinder;
    uint8_t  drive_heads;
    uint8_t  max_ecc_burst;
    uint8_t  last_ecc_burst;
    uint8_t  assign_params[PC98_SASI_MAX_UNITS][10];

    /* Bad/alternate-track ID-field state.  These flags survive a controller
     * reset, like the information written into real on-disk ID fields.  The
     * raw image format has nowhere to persist them across emulator restarts. */
    uint8_t  track_flags[PC98_SASI_MAX_UNITS][PC98_SASI_MAX_TRACKS];
    uint16_t alt_track[PC98_SASI_MAX_UNITS][PC98_SASI_MAX_TRACKS];

    /* Image formats do not carry the four on-media ECC bytes.  Keep sparse
     * WRITE LONG overrides so READ LONG can still exercise the diagnostic
     * datapath faithfully; ordinary writes/formatting invalidate entries. */
    uint32_t ecc_lba[PC98_SASI_MAX_UNITS][PC98_SASI_ECC_CACHE_ENTRIES];
    uint8_t  ecc_data[PC98_SASI_MAX_UNITS][PC98_SASI_ECC_CACHE_ENTRIES][PC98_SASI_LONG_ECC_SIZE];
    uint8_t  ecc_valid[PC98_SASI_MAX_UNITS][PC98_SASI_ECC_CACHE_ENTRIES];

    uint8_t  buf[PC98_SASI_BUF_SIZE];
    uint32_t buf_len;
    uint32_t buf_pos;

    pc98_sasi_dataout_action_t dataout_action;

    uint32_t xfer_lba; /* target LBA of the data phase in flight           */
    uint32_t xfer_blocks;
} pc98_sasi_t;

/*
 * The configuration values the board's SW1 DIP switches can present, and the
 * geometry-shaped table the BIOS carries beside them.  The physical switch
 * only defines attached capacities at selector values 0, 1, 4 and 6 (5, 10,
 * 20 and 40 MB respectively); 2, 3, 5 and 7 route to "not connected" on the
 * real board.  The ROM still contains table-shaped data for all eight index
 * values, which is why blindly treating every entry as a selectable drive is
 * tempting but wrong.
 *
 * The ROM-visible geometry is 33 256-byte blocks per track.  The top two
 * configuration bits are a single controller-type field, not media geometry.
 */
typedef struct pc98_sasi_geometry_t {
    uint16_t user_cylinders;  /* usable area                               */
    uint16_t total_cylinders; /* including the alternate-track area        */
    uint8_t  heads;
    uint8_t  sectors;         /* 256-byte format sectors/track             */
    uint32_t total_blocks;    /* total_cylinders * heads * sectors         */
} pc98_sasi_geometry_t;

#define PC98_SASI_DIP_COUNT        8u
#define PC98_SASI_DIP_DISCONNECTED 7u

/* Port 82h configuration readback.  CT1:CT0 is one controller-type field.
 * NEC PC-9800 Technical Databook: PC-9801-27 = 00; XA/XL/XL2 internal = 11.
 * Bits 5:3 are drive 0 type (DT1x), bits 2:0 drive 1 type (DT0x). */
#define PC98_SASI_DIP_CT_MASK       0xc0u
#define PC98_SASI_DIP_CT_PC9801_27 0x00u
#define PC98_SASI_DIP_CT_INTERNAL   0xc0u

extern const pc98_sasi_geometry_t pc98_sasi_geometry[PC98_SASI_DIP_COUNT];

/* Largest *valid connected* DIP entry whose usable LBA range the image can
 * back, or -1 if the image is smaller than the 5 MB setting. */
int      pc98_sasi_dip_for_blocks(uint32_t blocks); /* legacy 256-byte helper */
uint32_t pc98_sasi_dip_capacity(unsigned dip);        /* legacy 256-byte helper */
int      pc98_sasi_dip_for_media(uint32_t blocks, unsigned sector_size);
uint32_t pc98_sasi_dip_capacity_for_sector(unsigned dip, unsigned sector_size);
unsigned pc98_sasi_unit_sector_size(const pc98_sasi_t *s, unsigned unit);
unsigned pc98_sasi_unit_track_blocks(const pc98_sasi_t *s, unsigned unit);

void pc98_sasi_init(pc98_sasi_t *s, const pc98_sasi_ops_t *ops);
void pc98_sasi_reset(pc98_sasi_t *s);

/* Set the configuration byte directly.  This device always reports the
 * PC-9801-27 controller type (CT1:CT0=00); bits 5:0 select drive types. */
void pc98_sasi_set_dip(pc98_sasi_t *s, uint8_t dip);
void pc98_sasi_set_unit_dip(pc98_sasi_t *s, unsigned unit, unsigned dip);
/* Compatibility API retained for older callers.  PC-9801-27 always returns
 * 256 here; a 512-byte host container is merely split into two logical blocks. */
void pc98_sasi_set_unit_sector_size(pc98_sasi_t *s, unsigned unit,
                                    unsigned sector_size);

/*
 * 86Box device, in src/disk/hdc_pc98_sasi.c.  Gated on EMU_DEVICE_H the
 * same way the package's other headers are, so the portable core can still
 * be compiled on its own by the tests.
 */
#ifdef EMU_DEVICE_H
extern const device_t pc98_sasi_device;
extern int            pc98_sasi_available(void);
#endif

uint8_t pc98_sasi_status_in(pc98_sasi_t *s);  /* port 82h read  */
void    pc98_sasi_ctrl_out(pc98_sasi_t *s, uint8_t val); /* 82h write */
uint8_t pc98_sasi_data_in(pc98_sasi_t *s);    /* port 80h read  */
void    pc98_sasi_data_out(pc98_sasi_t *s, uint8_t val); /* 80h write */

#ifdef __cplusplus
}
#endif

#endif /*EMU_PC98_SASI_H*/
