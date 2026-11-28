/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          PC-9801-27 SASI hard disk interface - 86Box device.
 *
 *          The board is a C-BUS card carrying a 4 KiB BIOS ROM.  The
 *          PC-9801-27 decodes its disk BIOS in the D6000-D7FFF option-ROM
 *          area; the 4 KiB board ROM used by this implementation is mapped at
 *          D7000h.  Mapping it at D0000h, as an earlier version did, puts a
 *          valid expansion ROM in the wrong C-BUS option-ROM slot.
 *
 *          PC-98 native containers (.hdi, .nhd) are opened through
 *          src/disk/pc98_hdi.c.  The PC-9801-27 presents 256-byte SASI
 *          logical blocks.  On 17x512 containers, the ROM's 33-block track
 *          leaves the upper half of physical sector 16 as padding.
 */
#include <stdarg.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/hdd.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pc98_hdi.h>
#include <86box/pc98_rom.h>
#include <86box/pc98_sasi.h>
#include <86box/pic.h>
#include <86box/plat_unused.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/ui.h>

static void sasi_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void
sasi_log(const char *fmt, ...)
{
#if defined(ENABLE_PC98_SASI_LOG) && ENABLE_PC98_SASI_LOG
    va_list ap;
    va_start(ap, fmt);
    pclog_ex(fmt, ap);
    va_end(ap);
#else
    (void) fmt;
#endif
}

/*
 * The board BIOS, in the layouts these ROM sets ship in.  Each is tried
 * under every ROM directory 86Box is configured with.
 */
static const char *const pc98_sasi_rom_paths[] = {
    "roms/machines/pc9801/sasi.rom",
    "roms/machines/pc9801_27/sasi.rom",
    "roms/disk/pc9801_27/sasi.rom",
    "roms/hdd/pc9801_27/sasi.rom",
    "roms/pc9801_27/sasi.rom",
    NULL
};

#define PC98_SASI_ROM_SIZE  0x1000

#define PC98_SASI_PORT_DATA 0x0080
#define PC98_SASI_PORT_CTRL 0x0082

/* Slave PIC IR1.  86Box's flattened numbering makes that IRQ 9, which is
 * also what the system PPI reports as INT3 in port 33h bit 4. */
#define PC98_SASI_IRQ 9

/* The PC-9801-27 ROM programs channel 0 through 01h/03h and page 27h.
 * Its 09h/0Bh/page-23h sequence belongs to the 1 MB floppy path. */
#define PC98_SASI_DMA_CH0              0

#define PC98_SASI_PROFILE_AUTO         (-1)
#define PC98_SASI_PROFILE_DISCONNECTED 7

typedef struct pc98_sasi_dev_t {
    pc98_sasi_t core;
    rom_t      *bios;
    int         dma_ch;
    int         irq_state;
    int         unit_profile[PC98_SASI_MAX_UNITS];
    uint32_t    unit_capacity[PC98_SASI_MAX_UNITS];
    uint32_t    unit_backing_blocks[PC98_SASI_MAX_UNITS];
    uint32_t    rom_addr;
    int8_t      unit_hdd[PC98_SASI_MAX_UNITS]; /* index into hdd[], -1 = none */
    /* Native HDI/NHD access preserves the container's 256- or 512-byte
     * sectors.  86Box's generic image layer rejects 256-byte HDI sectors. */
    pc98_hdi_t unit_img[PC98_SASI_MAX_UNITS];
    uint8_t    sector_buf[PC98_SASI_BUF_SIZE + 512u];
} pc98_sasi_dev_t;

/* ------------------------------------------------------------------ */
/* Image access                                                        */
/* ------------------------------------------------------------------ */

static int
sasi_hdd_id(pc98_sasi_dev_t *dev, unsigned unit)
{
    if (unit >= PC98_SASI_MAX_UNITS)
        return -1;
    return dev->unit_hdd[unit];
}

static uint32_t
sasi_capacity_cb(void *priv, unsigned unit)
{
    pc98_sasi_dev_t *dev = (pc98_sasi_dev_t *) priv;
    return unit < PC98_SASI_MAX_UNITS ? dev->unit_capacity[unit] : 0u;
}

static void
sasi_log_bytes(const char *tag, unsigned unit, uint32_t lba,
               uint32_t count, uint32_t file_off, const uint8_t *buf,
               uint32_t len)
{
    char           hex[3u * 32u + 1u];
    const uint32_t n = len < 32u ? len : 32u;
    char          *q = hex;

    for (uint32_t i = 0; i < n; i++) {
        static const char x[] = "0123456789ABCDEF";
        *q++                  = x[buf[i] >> 4];
        *q++                  = x[buf[i] & 15u];
        *q++                  = (i + 1u == n) ? '\0' : ' ';
    }
    if (!n)
        hex[0] = '\0';

    sasi_log("[PC98-SASI-BOOT] %s u%u lba=%u count=%u bytes=%u "
             "file_off=%08X data=%s\n",
             tag, unit, (unsigned) lba,
             (unsigned) count, (unsigned) len, (unsigned) file_off, hex);
}

static uint32_t
sasi_hdi_file_offset(const pc98_hdi_t *img, uint32_t lba)
{
    uint64_t off = (uint64_t) img->base + (uint64_t) lba * PC98_SASI_BLOCK_SIZE;

    if (img->sector_size == 512u) {
        const uint32_t bios_spt = PC98_SASI_TRACK_BLOCKS_256;
        off                     = (uint64_t) img->base + (uint64_t) (lba / bios_spt) * img->spt * img->sector_size + (uint64_t) (lba % bios_spt) * PC98_SASI_BLOCK_SIZE;
    }

    return (uint32_t) off;
}

static int
sasi_read_cb(void *priv, unsigned unit, uint32_t lba, uint32_t count,
             uint8_t *buf)
{
    pc98_sasi_dev_t *dev = (pc98_sasi_dev_t *) priv;

    ui_sb_update_icon(SB_SASI, 1);

    if (unit >= PC98_SASI_MAX_UNITS || !count || count > PC98_SASI_BUF_BLOCKS || lba >= dev->unit_backing_blocks[unit] || count > dev->unit_backing_blocks[unit] - lba)
        return 0;

    if (unit < PC98_SASI_MAX_UNITS && dev->unit_img[unit].f) {
        pc98_hdi_t *img = &dev->unit_img[unit];
        const int   ok  = pc98_hdi_sasi_read(img, lba, count,
                                             PC98_SASI_TRACK_BLOCKS_256,
                                             img->hpc, buf);
        if (ok)
            sasi_log_bytes("READ-HDI", unit, lba, count,
                           sasi_hdi_file_offset(img, lba), buf,
                           count * PC98_SASI_BLOCK_SIZE);
        return ok;
    }

    const int id = sasi_hdd_id(dev, unit);
    if (id < 0)
        return 0;

    const uint32_t first = lba >> 1;
    const uint32_t last  = (lba + count - 1u) >> 1;
    const uint32_t n512  = (last - first) + 1u;

    if (hdd_image_read((uint8_t) id, first, n512, dev->sector_buf) < 0)
        return 0;

    memcpy(buf, dev->sector_buf + ((lba & 1u) * PC98_SASI_BLOCK_SIZE),
           (size_t) count * PC98_SASI_BLOCK_SIZE);
    sasi_log_bytes("READ-HDD", unit, lba, count, first * 512u, buf,
                   count * PC98_SASI_BLOCK_SIZE);
    return 1;
}

static int
sasi_write_cb(void *priv, unsigned unit, uint32_t lba, uint32_t count,
              const uint8_t *buf)
{
    pc98_sasi_dev_t *dev = (pc98_sasi_dev_t *) priv;

    ui_sb_update_icon_write(SB_SASI, 1);

    if (unit >= PC98_SASI_MAX_UNITS || !count || count > PC98_SASI_BUF_BLOCKS || lba >= dev->unit_backing_blocks[unit] || count > dev->unit_backing_blocks[unit] - lba)
        return 0;

    if (unit < PC98_SASI_MAX_UNITS && dev->unit_img[unit].f) {
        pc98_hdi_t *img = &dev->unit_img[unit];
        const int   ok  = pc98_hdi_sasi_write(img, lba, count,
                                              PC98_SASI_TRACK_BLOCKS_256,
                                              img->hpc, buf);
        if (ok)
            sasi_log_bytes("WRITE-HDI", unit, lba, count,
                           sasi_hdi_file_offset(img, lba), buf,
                           count * PC98_SASI_BLOCK_SIZE);
        return ok;
    }

    const int id = sasi_hdd_id(dev, unit);
    if (id < 0)
        return 0;

    const uint32_t first = lba >> 1;
    const uint32_t last  = (lba + count - 1u) >> 1;
    const uint32_t n512  = (last - first) + 1u;
    const uint32_t head  = (lba & 1u) * PC98_SASI_BLOCK_SIZE;
    const int      tail  = ((lba + count) & 1u) != 0u;

    /* Preserve the untouched half of an odd first/last 512-byte host sector. */
    if (head || tail) {
        if (hdd_image_read((uint8_t) id, first, n512, dev->sector_buf) < 0)
            return 0;
    }

    memcpy(dev->sector_buf + head, buf, (size_t) count * PC98_SASI_BLOCK_SIZE);
    return hdd_image_write((uint8_t) id, first, n512, dev->sector_buf) >= 0;
}

/* ------------------------------------------------------------------ */
/* Bus services                                                        */
/* ------------------------------------------------------------------ */

static void
sasi_irq_cb(void *priv, int level)
{
    pc98_sasi_dev_t *dev = (pc98_sasi_dev_t *) priv;

    if (level == dev->irq_state)
        return;
    dev->irq_state = level;

    if (level)
        picint(1u << PC98_SASI_IRQ);
    else
        picintc(1u << PC98_SASI_IRQ);
}

static void
sasi_log_dma_count(pc98_sasi_dev_t *dev, uint32_t len)
{
    const uint32_t tc = dma[dev->dma_ch].cc;

    /* CDB byte 4 is a block count only for READ/WRITE/VERIFY/LONG.  For
     * FORMAT it is interleave, and for parameter/buffer commands it is a
     * don't-care.  Logging it as a decoded count made traces actively
     * misleading while debugging DMA. */
    sasi_log("[PC98-SASI-DMA-TC] op=%02X u%u cdb4=%02X block=%u tc=%04X "
             "programmed_bytes=%u target_bytes=%u\n",
             dev->core.cdb[0],
             dev->core.unit, dev->core.cdb[4], PC98_SASI_BLOCK_SIZE,
             (unsigned) tc, (unsigned) (tc + 1u), (unsigned) len);
}

/* Device to memory. */
static uint32_t
sasi_dma_write_cb(void *priv, const uint8_t *buf, uint32_t len)
{
    pc98_sasi_dev_t *dev = (pc98_sasi_dev_t *) priv;
    uint32_t         i;
    const uint32_t   start_ac               = dma[dev->dma_ch].ac;
    const uint32_t   start_ab               = dma[dev->dma_ch].ab;
    const uint8_t    saved_mode             = dma[dev->dma_ch].mode;
    const int        board_drives_mem_write = ((saved_mode & 0x0cu) == 0x00u);

    sasi_log_dma_count(dev, len);

    /*
     * PC-9801-27 DMA is not wired like an IBM-PC peripheral.
     *
     * The original NEC board ROM at D8906h programs channel 0 with mode 40h
     * for its normal SASI READ path (AH=08h), then enables the board and lets
     * the C-BUS adapter supply the memory-write strobe while the uPD8237A
     * supplies address/count/DACK.  On a bare 8237, transfer type 00b is
     * VERIFY; 86Box therefore advances AC/CC but deliberately does not copy
     * the byte.  The trace then says "moved=..." even though RAM still
     * contains stale data.  That is exactly the corruption that lets IO.SYS
     * start and later makes COMMAND.COM fail to load.
     *
     * Model the board-side strobe locally instead of changing generic 8237
     * VERIFY semantics.  Temporarily select the ordinary I/O->memory cycle
     * while this PC-9801-27 DATA IN callback services DACK, then restore the
     * guest-programmed mode byte.  The ROM's alternate path already programs
     * 44h, in which case this is a no-op.
     */
    if (board_drives_mem_write)
        dma[dev->dma_ch].mode = (uint8_t) ((saved_mode & ~0x0cu) | 0x04u);

    dma_set_drq(dev->dma_ch, 1);
    for (i = 0; i < len; i++) {
        const int r = dma_channel_write(dev->dma_ch, buf[i]);
        if (r == DMA_NODATA)
            break;
        if (r & DMA_OVER) {
            i++;
            break;
        }
    }

    dma[dev->dma_ch].mode = saved_mode;
    dma_set_drq(dev->dma_ch, 0);

    sasi_log("[PC98-SASI-DMA] ch=%d len=%u moved=%u mode=%02X%s ab=%08X "
             "ac_start=%08X ac_end=%08X first=%02X %02X %02X %02X %02X %02X %02X %02X\n",
             dev->dma_ch, (unsigned) len, (unsigned) i, saved_mode,
             board_drives_mem_write ? " (PC-9801-27 ext-MW)" : "",
             (unsigned) start_ab, (unsigned) start_ac,
             (unsigned) dma[dev->dma_ch].ac,
             len > 0u ? buf[0] : 0u, len > 1u ? buf[1] : 0u,
             len > 2u ? buf[2] : 0u, len > 3u ? buf[3] : 0u,
             len > 4u ? buf[4] : 0u, len > 5u ? buf[5] : 0u,
             len > 6u ? buf[6] : 0u, len > 7u ? buf[7] : 0u);
    return i;
}

/* Memory to device. */
static uint32_t
sasi_dma_read_cb(void *priv, uint8_t *buf, uint32_t len)
{
    pc98_sasi_dev_t *dev = (pc98_sasi_dev_t *) priv;
    uint32_t         i;
    const uint32_t   start_ac = dma[dev->dma_ch].ac;
    const uint32_t   start_ab = dma[dev->dma_ch].ab;

    sasi_log_dma_count(dev, len);

    dma_set_drq(dev->dma_ch, 1);
    for (i = 0; i < len; i++) {
        const int r = dma_channel_read(dev->dma_ch);
        if (r == DMA_NODATA)
            break;
        buf[i] = (uint8_t) ((unsigned int) r & 0xffu);
        if (r & DMA_OVER) {
            i++;
            break;
        }
    }
    dma_set_drq(dev->dma_ch, 0);
    sasi_log("[PC98-SASI-DMA-OUT] ch=%d len=%u moved=%u ab=%08X ac_start=%08X "
             "ac_end=%08X first=%02X %02X %02X %02X %02X %02X %02X %02X\n",
             dev->dma_ch, (unsigned) len, (unsigned) i, (unsigned) start_ab,
             (unsigned) start_ac, (unsigned) dma[dev->dma_ch].ac,
             i > 0u ? buf[0] : 0u, i > 1u ? buf[1] : 0u,
             i > 2u ? buf[2] : 0u, i > 3u ? buf[3] : 0u,
             i > 4u ? buf[4] : 0u, i > 5u ? buf[5] : 0u,
             i > 6u ? buf[6] : 0u, i > 7u ? buf[7] : 0u);
    return i;
}

/* ------------------------------------------------------------------ */
/* I/O handlers                                                        */
/* ------------------------------------------------------------------ */

static const char *
sasi_phase_name(pc98_sasi_phase_t phase)
{
    switch (phase) {
        case PC98_SASI_BUSFREE:
            return "BUS FREE";
        case PC98_SASI_SELECT:
            return "SELECT";
        case PC98_SASI_COMMAND:
            return "COMMAND";
        case PC98_SASI_DATAIN:
            return "DATA IN";
        case PC98_SASI_DATAOUT:
            return "DATA OUT";
        case PC98_SASI_STATUS:
            return "STATUS";
        case PC98_SASI_MSGIN:
            return "MESSAGE IN";
        default:
            return "?";
    }
}

static uint8_t
sasi_in(uint16_t port, void *priv)
{
    pc98_sasi_dev_t *dev = (pc98_sasi_dev_t *) priv;

    if (port == PC98_SASI_PORT_DATA) {
        const pc98_sasi_phase_t before = dev->core.phase;
        const uint8_t           v      = pc98_sasi_data_in(&dev->core);

        if (before == PC98_SASI_STATUS)
            sasi_log("[PC98] SASI status u%u: %02X -> %s\n",
                     dev->core.unit, v, sasi_phase_name(dev->core.phase));
        else if (before == PC98_SASI_MSGIN)
            sasi_log("[PC98] SASI message u%u: %02X -> %s\n",
                     dev->core.unit, v, sasi_phase_name(dev->core.phase));
        return v;
    }

    const uint8_t v = pc98_sasi_status_in(&dev->core);
    if (!(dev->core.ctrl & PC98_SASI_CTL_STS))
        sasi_log("[PC98] SASI config read: %02X (CT=%u, u0 DT=%u, u1 DT=%u)\n",
                 v, (v >> 6) & 3u, (v >> 3) & 7u, v & 7u);
    return v;
}

static void
sasi_out(uint16_t port, uint8_t val, void *priv)
{
    pc98_sasi_dev_t *dev = (pc98_sasi_dev_t *) priv;

    if (port == PC98_SASI_PORT_DATA) {
        const pc98_sasi_phase_t before  = dev->core.phase;
        const unsigned          cdb_pos = dev->core.cdb_pos;
        const uint32_t          buf_pos = dev->core.buf_pos;

        pc98_sasi_data_out(&dev->core, val);

        /* High-signal trace only: one line per completed CDB.  This leaves
         * the ROM's tight status-poll loops quiet while making a firmware
         * deadlock immediately identify the command and resulting phase. */
        if (before == PC98_SASI_COMMAND && cdb_pos == PC98_SASI_CDB_LEN - 1u) {
            const uint32_t trace_lba  = ((uint32_t) (dev->core.cdb[1] & 0x1fu) << 16) | ((uint32_t) dev->core.cdb[2] << 8) | dev->core.cdb[3];
            const uint32_t trace_xfer = (dev->core.phase == PC98_SASI_DATAIN || dev->core.phase == PC98_SASI_DATAOUT) ? dev->core.buf_len : 0u;
            sasi_log("[PC98] SASI CDB u%u: %02X %02X %02X %02X %02X %02X "
                     "LBA=%u cdb4=%02X xfer_bytes=%u -> %s irq=%d\n",
                     dev->core.unit,
                     dev->core.cdb[0], dev->core.cdb[1], dev->core.cdb[2],
                     dev->core.cdb[3], dev->core.cdb[4], dev->core.cdb[5],
                     (unsigned) trace_lba, dev->core.cdb[4],
                     (unsigned) trace_xfer, sasi_phase_name(dev->core.phase),
                     dev->irq_state);
        } else if (before == PC98_SASI_DATAOUT && dev->core.cdb[0] == 0xc2u && buf_pos == 9u) {
            sasi_log("[PC98] SASI C2 parameters u%u complete -> %s\n",
                     dev->core.unit, sasi_phase_name(dev->core.phase));
        }
    } else {
        pc98_sasi_ctrl_out(&dev->core, val);
    }
}

/* ------------------------------------------------------------------ */
/* Device plumbing                                                     */
/* ------------------------------------------------------------------ */

static void
sasi_attach_drives(pc98_sasi_dev_t *dev)
{
    unsigned next = 0u;

    for (unsigned i = 0; i < PC98_SASI_MAX_UNITS; i++) {
        dev->unit_hdd[i]            = -1;
        dev->unit_capacity[i]       = 0u;
        dev->unit_backing_blocks[i] = 0u;
        pc98_hdi_close(&dev->unit_img[i]);
        pc98_sasi_set_unit_dip(&dev->core, i, PC98_SASI_DIP_DISCONNECTED);
    }

    /*
     * SASI is a distinct host interface in 86Box.  Do not borrow the MFM bus:
     * that made a PC-9801-27 disk look like an ST-506 attachment and allowed
     * it to be accidentally consumed by an unrelated MFM controller.
     * SASI channels 0 and 1 map directly to PC-9801-27 units 0 and 1.
     */
    for (uint8_t i = 0; i < HDD_NUM && next < PC98_SASI_MAX_UNITS; i++) {
        if (hdd[i].bus_type != HDD_BUS_SASI)
            continue;
        if (hdd[i].sasi_channel >= PC98_SASI_MAX_UNITS)
            continue;

        const unsigned unit = hdd[i].sasi_channel;
        if (dev->unit_hdd[unit] >= 0 || dev->unit_img[unit].f)
            continue;

        /*
         * Try the PC-98 native containers first.  86Box's image layer
         * cannot open a 256-byte-sector HDI at all - hdd_image_load()
         * refuses it and silently falls back to reading the file as
         * headerless raw, which yields a drive full of noise - so this has
         * to come before it rather than after.
         */
        char      import_error[256];
        const int native_result = pc98_hdi_open_sasi(&dev->unit_img[unit], hdd[i].fn,
                                                     import_error, sizeof(import_error));
        if (native_result < 0) {
            char message[sizeof(hdd[i].fn) + 512];
            snprintf(message, sizeof(message), "SASI disk %u could not be attached:\n%s\n\n%s",
                     unit, hdd[i].fn, import_error);
            ui_msgbox(MBX_ERROR, message);
            continue;
        }
        if (native_result > 0) {
            if (dev->unit_img[unit].sasi_imported) {
                char      imported[sizeof(hdd[i].fn)];
                const int length = snprintf(imported, sizeof(imported), "%s.sasi.hdi", hdd[i].fn);
                if (length < 0 || (size_t) length >= sizeof(imported)) {
                    pc98_hdi_close(&dev->unit_img[unit]);
                    ui_msgbox(MBX_ERROR, "The imported SASI image path exceeds the disk settings limit.");
                    continue;
                }
                memcpy(hdd[i].fn, imported, (size_t) length + 1u);
                hdd[i].tracks = dev->unit_img[unit].cylinders;
                hdd[i].hpc    = dev->unit_img[unit].hpc;
                hdd[i].spt    = dev->unit_img[unit].spt;
                char message[sizeof(hdd[i].fn) + 512];
                snprintf(message, sizeof(message),
                         "A SASI-compatible copy was selected:\n%s\n\n"
                         "The original disk is unchanged. Disk writes go to this copy.",
                         imported);
                ui_msgbox(MBX_INFO, message);
            }
            pclog("[PC98] SASI unit %u: %s, %u-byte sectors, "
                  "%u cyl / %u heads / %u sec, %u blocks%s\n",
                  unit,
                  dev->unit_img[unit].format == PC98_HDI_T98NEXT ? "NHD" : "HDI",
                  (unsigned) dev->unit_img[unit].sector_size,
                  (unsigned) dev->unit_img[unit].cylinders,
                  (unsigned) dev->unit_img[unit].hpc,
                  (unsigned) dev->unit_img[unit].spt,
                  (unsigned) dev->unit_img[unit].blocks,
                  dev->unit_img[unit].read_only ? ", read-only" : "");
            next++;
            continue;
        }

        if (!hdd_image_load(i))
            continue;

        dev->unit_hdd[unit] = (int8_t) i;
        next++;
    }

    /* Tell firmware which drive-type selector each unit uses.  CT1:CT0 is a
     * single controller-type field and is fixed at 00 for PC-9801-27.  Host
     * container sector size never changes this readback. */
    for (unsigned u = 0; u < PC98_SASI_MAX_UNITS; u++) {
        uint32_t logical_blocks;
        unsigned image_spt = 0u, image_heads = 0u, image_cylinders = 0u;
        unsigned container_sector_size = 512u;
        (void) container_sector_size; /* used only by non-release diagnostics */
        int native = 0;

        if (dev->unit_img[u].f) {
            const pc98_hdi_t *img = &dev->unit_img[u];
            logical_blocks        = pc98_hdi_sasi_blocks(
                img, PC98_SASI_TRACK_BLOCKS_256);
            if (!logical_blocks) {
                pclog("[PC98] SASI unit %u: native geometry cannot supply 33 logical blocks per track\n", u);
                pc98_hdi_close(&dev->unit_img[u]);
                continue;
            }
            container_sector_size = img->sector_size;
            image_spt             = img->spt;
            image_heads           = img->hpc;
            image_cylinders       = img->cylinders;
            native                = 1;
        } else if (dev->unit_hdd[u] >= 0) {
            const uint64_t sectors512 = (uint64_t) hdd_image_get_last_sector((uint8_t) dev->unit_hdd[u]) + 1u;
            const uint64_t blocks256  = sectors512 * 2u;
            if (blocks256 > UINT32_MAX) {
                pclog("[PC98] SASI unit %u: backing exceeds the logical block address range\n", u);
                hdd_image_close((uint8_t) dev->unit_hdd[u]);
                dev->unit_hdd[u] = -1;
                continue;
            }
            logical_blocks = (uint32_t) blocks256;
        } else {
            continue;
        }

        int       dip     = pc98_sasi_dip_for_blocks(logical_blocks);
        const int profile = dev->unit_profile[u];

        if (profile == PC98_SASI_PROFILE_DISCONNECTED) {
            pclog("[PC98] SASI unit %u: disconnected by SW1\n", u);
            if (native)
                pc98_hdi_close(&dev->unit_img[u]);
            else {
                hdd_image_close((uint8_t) dev->unit_hdd[u]);
                dev->unit_hdd[u] = -1;
            }
            continue;
        }

        if (profile != PC98_SASI_PROFILE_AUTO) {
            if (profile != 0 && profile != 1 && profile != 4 && profile != 6) {
                pclog("[PC98] SASI unit %u: invalid PC-9801-27 drive type %d; "
                      "using Automatic\n",
                      u, profile);
            } else {
                dip = profile;
            }
        }

        if (dip < 0) {
            /* There is no smaller connected setting than 5 MB.  Keep the
             * target present and bound accesses to the actual backing. */
            dip = 0;
            pclog("[PC98] SASI unit %u: backing has only %u logical blocks; "
                  "using the 5 MB drive type without detaching\n",
                  u, (unsigned) logical_blocks);
        }

        pc98_sasi_set_unit_dip(&dev->core, u, (unsigned) dip);

        const uint32_t profile_capacity = pc98_sasi_dip_capacity((unsigned) dip);
        dev->unit_backing_blocks[u]     = logical_blocks;
        dev->unit_capacity[u]           = logical_blocks < profile_capacity
            ? logical_blocks
            : profile_capacity;

        if (native && (image_heads != pc98_sasi_geometry[dip].heads || image_spt != pc98_sasi_geometry[dip].sectors || image_cylinders < pc98_sasi_geometry[dip].user_cylinders)) {
            pclog("[PC98] SASI unit %u: container geometry %u/%u/%u @ %u-byte "
                  "differs from drive profile %u/%u/%u @ 256-byte; exposing "
                  "the container through 256-byte SASI blocks\n",
                  u,
                  image_cylinders, image_heads, image_spt, container_sector_size,
                  pc98_sasi_geometry[dip].user_cylinders,
                  pc98_sasi_geometry[dip].heads,
                  pc98_sasi_geometry[dip].sectors);
        }

        if (logical_blocks < profile_capacity) {
            pclog("[PC98] SASI unit %u: drive type advertises %u blocks but "
                  "backing provides %u; effective LBA range is 0..%u\n",
                  u,
                  (unsigned) profile_capacity, (unsigned) logical_blocks,
                  logical_blocks ? (unsigned) (logical_blocks - 1u) : 0u);
        }

        pclog("[PC98] SASI unit %u: backing=%u x 256-byte logical blocks, "
              "DT=%d, effective=%u blocks, CT=0 (PC-9801-27), "
              "container-sector=%u\n",
              u, (unsigned) logical_blocks, dip,
              (unsigned) dev->unit_capacity[u], container_sector_size);
    }
}

static void
pc98_sasi_dev_reset(void *priv)
{
    pc98_sasi_dev_t *dev = (pc98_sasi_dev_t *) priv;

    /* A controller reset resets bus/protocol state, not the media cable.
     * Keep the already-open backing image, SW1/CT state and effective extent
     * intact across hard reset.  Reattachment belongs to device creation. */
    pc98_sasi_reset(&dev->core);
    sasi_irq_cb(dev, 0);
    dma_set_drq(dev->dma_ch, 0);
}

static void *
pc98_sasi_dev_init(UNUSED(const device_t *info))
{
    (void) info; /* the stand-ins in tests/stub define UNUSED() as a no-op */

    pc98_sasi_dev_t *dev = (pc98_sasi_dev_t *) calloc(1, sizeof(pc98_sasi_dev_t));

    if (!dev)
        return NULL;

    /* Keep the mapping node alive after controller teardown, as the memory
     * layer retains linked ROM mappings until its own reset. */
    dev->bios = (rom_t *) calloc(1, sizeof(rom_t));
    if (!dev->bios) {
        free(dev);
        return NULL;
    }

    const pc98_sasi_ops_t ops = {
        .read_blocks  = sasi_read_cb,
        .write_blocks = sasi_write_cb,
        .capacity     = sasi_capacity_cb,
        .set_irq      = sasi_irq_cb,
        .dma_write    = sasi_dma_write_cb,
        .dma_read     = sasi_dma_read_cb,
        .priv         = dev
    };

    pc98_sasi_init(&dev->core, &ops);
    dev->dma_ch          = PC98_SASI_DMA_CH0;
    dev->unit_profile[0] = device_get_config_int("unit0_profile");
    dev->unit_profile[1] = device_get_config_int("unit1_profile");
    dev->rom_addr        = (uint32_t) device_get_config_int("bios_addr");
    if (dev->rom_addr < 0x000d0000u || dev->rom_addr > 0x000d7000u || (dev->rom_addr & 0x0fffu))
        dev->rom_addr = 0x000d7000u;

    char rom_path[PC98_ROM_PATH_MAX];

    /*
     * Without the board ROM the firmware's expansion scan finds no AA55h at
     * D0000h+9 and never hooks the disk vector, so the interface would
     * answer ports nothing ever reads.  available() keeps this from being
     * selectable in that state; this is the belt-and-braces path.
     */
    if (!pc98_rom_find(pc98_sasi_rom_paths, rom_path, sizeof(rom_path),
                       "PC-9801-27 board BIOS (sasi.rom)")
        || rom_init(dev->bios, rom_path, dev->rom_addr, PC98_SASI_ROM_SIZE,
                    PC98_SASI_ROM_SIZE - 1, 0, MEM_MAPPING_EXTERNAL)) {
        pclog("[PC98] SASI: the board BIOS is what hooks the disk vector, "
              "so the interface is not installed\n");
        free(dev->bios);
        free(dev);
        return NULL;
    }
    pclog("[PC98] SASI: %s mapped at %05Xh; DMA channel 0\n", rom_path,
          (unsigned) dev->rom_addr);

    io_sethandler(PC98_SASI_PORT_DATA, 1, sasi_in, NULL, NULL, sasi_out, NULL,
                  NULL, dev);
    io_sethandler(PC98_SASI_PORT_CTRL, 1, sasi_in, NULL, NULL, sasi_out, NULL,
                  NULL, dev);

    sasi_attach_drives(dev);
    pc98_sasi_dev_reset(dev);

    return dev;
}

static void
pc98_sasi_dev_close(void *priv)
{
    pc98_sasi_dev_t *dev = (pc98_sasi_dev_t *) priv;

    if (!dev)
        return;

    io_removehandler(PC98_SASI_PORT_DATA, 1, sasi_in, NULL, NULL, sasi_out,
                     NULL, NULL, dev);
    io_removehandler(PC98_SASI_PORT_CTRL, 1, sasi_in, NULL, NULL, sasi_out,
                     NULL, NULL, dev);

    pc98_sasi_dev_reset(dev);
    mem_mapping_disable(&dev->bios->mapping);
    mem_mapping_set_handler(&dev->bios->mapping, NULL, NULL, NULL, NULL, NULL, NULL);
    mem_mapping_set_exec(&dev->bios->mapping, NULL);
    mem_mapping_set_p(&dev->bios->mapping, NULL);
    free(dev->bios->rom);
    dev->bios->rom = NULL;

    for (unsigned u = 0; u < PC98_SASI_MAX_UNITS; u++) {
        if (dev->unit_hdd[u] >= 0)
            hdd_image_close((uint8_t) dev->unit_hdd[u]);
        pc98_hdi_close(&dev->unit_img[u]);
    }

    free(dev);
}

int
pc98_sasi_available(void)
{
    char path[PC98_ROM_PATH_MAX];

    return pc98_rom_find_quiet(pc98_sasi_rom_paths, path, sizeof(path));
}

static const device_config_t pc98_sasi_config[] = {
    // clang-format off
    {
        .name           = "unit0_profile",
        .description    = "Unit 0 drive type switches",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = PC98_SASI_PROFILE_AUTO,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Automatic from image", .value = PC98_SASI_PROFILE_AUTO },
            { .description = "Disconnected", .value = PC98_SASI_PROFILE_DISCONNECTED },
            { .description = "5 MB", .value = 0 },
            { .description = "10 MB", .value = 1 },
            { .description = "20 MB", .value = 4 },
            { .description = "40 MB", .value = 6 },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "unit1_profile",
        .description    = "Unit 1 drive type switches",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = PC98_SASI_PROFILE_AUTO,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Automatic from image", .value = PC98_SASI_PROFILE_AUTO },
            { .description = "Disconnected", .value = PC98_SASI_PROFILE_DISCONNECTED },
            { .description = "5 MB", .value = 0 },
            { .description = "10 MB", .value = 1 },
            { .description = "20 MB", .value = 4 },
            { .description = "40 MB", .value = 6 },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_addr",
        .description    = "BIOS ROM address switches",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0x000d7000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "D0000-D0FFF", .value = 0x000d0000 },
            { .description = "D1000-D1FFF", .value = 0x000d1000 },
            { .description = "D2000-D2FFF", .value = 0x000d2000 },
            { .description = "D3000-D3FFF", .value = 0x000d3000 },
            { .description = "D4000-D4FFF", .value = 0x000d4000 },
            { .description = "D5000-D5FFF", .value = 0x000d5000 },
            { .description = "D6000-D6FFF", .value = 0x000d6000 },
            { .description = "D7000-D7FFF", .value = 0x000d7000 },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t pc98_sasi_device = {
    .name          = "NEC PC-9801-27 SASI interface",
    .internal_name = "pc9801_27",
    .flags         = DEVICE_CBUS,
    .local         = 0,
    .init          = pc98_sasi_dev_init,
    .close         = pc98_sasi_dev_close,
    .reset         = pc98_sasi_dev_reset,
    .available     = pc98_sasi_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pc98_sasi_config
};
