/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Xebec S1410-compatible SASI target and line-bus model.
 *
 *          Phase sequencing here follows the board BIOS byte for byte.  The
 *          ROM's transfer core selects with
 *
 *              out 82h,40h / wait BSY=0 / out 80h,<id> / out 82h,60h /
 *              wait BSY=1 / out 82h,0C0h
 *
 *          and then spins on (status & 0FCh) against 0A8h (command out),
 *          0A0h (data out), 0A4h (data in), 0ACh (status in) and 0BCh
 *          (message in), waiting for the ACK bit to fall after each byte.
 *          The PC-9801-27 adapter only drives SEL/RST/ACK and observes the
 *          target lines below.  Command execution is target-controller
 *          behavior, not BIOS-call or formatter HLE.
 */
#include <string.h>

#include <86box/pc98_sasi.h>

/*
 * Geometry indexed by the DT0/DT1 selector. The fixed-disk documentation's
 * user-cylinder classes are 153/310/615; the larger 340/664 values include
 * alternate-track space. Each record has
 * TWO different notions of size:
 *
 *   user_cylinders  - the cylinders the BIOS makes addressable to software
 *   total_cylinders - the physical controller geometry including alternates
 *
 * The final block count stored beside the record is the latter
 * (total_cylinders * heads * 33).  Using it as the minimum image size was a
 * serious bug: a perfectly normal 10 MB image has enough room for the 310
 * user cylinders but not for all 340 cylinders including the controller's
 * spare area, so the old wrapper detached it before the ROM ever ran.
 */
const pc98_sasi_geometry_t pc98_sasi_geometry[PC98_SASI_DIP_COUNT] = {
    /* user, total, heads, sectors, blocks */
    { 153, 340, 4, 33,  44880u },
    { 310, 340, 4, 33,  44880u },
    { 310, 340, 6, 33,  67320u },
    { 310, 340, 8, 33,  89760u },
    { 615, 664, 4, 33,  87648u },
    { 615, 664, 6, 33, 131472u },
    { 615, 664, 8, 33, 175296u },
    { 310, 332, 4, 33,  43824u }
};

static int
pc98_sasi_dip_is_connected(unsigned dip)
{
    /*
     * PC-9801-27 SW1 only wires these four capacity encodings:
     *   0 = 5 MB, 1 = 10 MB, 4 = 20 MB, 6 = 40 MB.
     * The other encodings route to the unconnected state.  The option ROM's
     * pointer table nevertheless has eight entries, so they must not be
     * inferred to be valid merely because data exists at those indices.
     */
    return dip == 0u || dip == 1u || dip == 4u || dip == 6u;
}

uint32_t
pc98_sasi_dip_capacity_for_sector(unsigned dip, unsigned sector_size)
{
    if (dip >= PC98_SASI_DIP_COUNT || !pc98_sasi_dip_is_connected(dip))
        return 0u;
    if (sector_size != 256u && sector_size != 512u)
        return 0u;

    const pc98_sasi_geometry_t *g = &pc98_sasi_geometry[dip];
    const uint32_t blocks256 = (uint32_t) g->user_cylinders *
                               (uint32_t) g->heads * (uint32_t) g->sectors;
    /* This helper describes host-container sectors only.  It must never
     * change the PC-9801-27 controller type or insert per-track padding. */
    return sector_size == 512u ? blocks256 / 2u : blocks256;
}

uint32_t
pc98_sasi_dip_capacity(unsigned dip)
{
    if (dip >= PC98_SASI_DIP_COUNT || !pc98_sasi_dip_is_connected(dip))
        return 0u;

    return pc98_sasi_dip_capacity_for_sector(dip, 256u);
}

int
pc98_sasi_dip_for_media(uint32_t blocks, unsigned sector_size)
{
    if (sector_size != 256u && sector_size != 512u)
        return -1;

    /* Convert the host container's sector count to the board's fixed
     * 256-byte logical LBA space.  A 512-byte HDI sector is two consecutive
     * SASI blocks; there is no synthetic 33->17 track hole. */
    const uint64_t logical = (uint64_t) blocks * (uint64_t) sector_size /
                             PC98_SASI_BLOCK_SIZE;
    if (logical > 0xffffffffu)
        return -1;
    return pc98_sasi_dip_for_blocks((uint32_t) logical);
}

int
pc98_sasi_dip_for_blocks(uint32_t blocks)
{
    int      best      = -1;
    uint32_t best_size = 0u;

    /* Legacy helper: blocks are CT=0 (256-byte) logical sectors. */
    for (unsigned i = 0; i < PC98_SASI_DIP_COUNT; i++) {
        if (!pc98_sasi_dip_is_connected(i))
            continue;
        const uint32_t cap = pc98_sasi_dip_capacity(i);
        if (cap > blocks)
            continue;
        if (best < 0 || cap > best_size) {
            best = (int) i;
            best_size = cap;
        }
    }
    return best;
}

static uint32_t
sasi_capacity(pc98_sasi_t *s, unsigned unit)
{
    if (unit >= PC98_SASI_MAX_UNITS || !s->ops.capacity)
        return 0u;
    return s->ops.capacity(s->ops.priv, unit);
}

static void
sasi_update_irq(pc98_sasi_t *s)
{
    /*
     * The -27 does not interrupt after the message byte.  Its ROM enables
     * bit 0 with C1h/C3h and then sleeps in b9ch until INT3 fires.  The ISR
     * accepts status 0ADh (status phase 0ACh + INT bit 0) and immediately
     * writes C0h to drop the interrupt enable before the foreground code
     * reads the status and message bytes.
     *
     * The board interrupt condition is the status phase (REQ && !MSG && C/D
     * && I/O), gated by control bit 0.  int_pending records that phase
     * condition independently of the gate so changing C1h <-> C0h updates
     * the line immediately.
     */
    const int level = s->int_pending && (s->ctrl & PC98_SASI_CTL_IEN);
    if (s->ops.set_irq)
        s->ops.set_irq(s->ops.priv, level ? 1 : 0);
}

static void
sasi_raise_irq(pc98_sasi_t *s)
{
    s->int_pending = 1u;
    sasi_update_irq(s);
}

static void
sasi_clear_irq(pc98_sasi_t *s)
{
    s->int_pending = 0u;
    sasi_update_irq(s);
}

static void
sasi_set_phase(pc98_sasi_t *s, pc98_sasi_phase_t phase)
{
    s->phase = phase;
    s->lines.bsy = phase != PC98_SASI_BUSFREE;
    s->lines.req = phase == PC98_SASI_COMMAND || phase == PC98_SASI_DATAIN ||
                   phase == PC98_SASI_DATAOUT || phase == PC98_SASI_STATUS ||
                   phase == PC98_SASI_MSGIN;
    s->lines.msg = phase == PC98_SASI_MSGIN;
    s->lines.cd  = phase == PC98_SASI_COMMAND || phase == PC98_SASI_STATUS ||
                   phase == PC98_SASI_MSGIN;
    s->lines.io  = phase == PC98_SASI_DATAIN || phase == PC98_SASI_STATUS ||
                   phase == PC98_SASI_MSGIN;
}

static void
sasi_ack_begin(pc98_sasi_t *s)
{
    s->lines.ack = 1u;
    s->lines.req = 0u;
}

static void
sasi_ack_end(pc98_sasi_t *s)
{
    s->lines.ack = 0u;
    /* The target reasserts REQ only if the next phase has another byte. */
    sasi_set_phase(s, s->phase);
}

static void
sasi_enter_status(pc98_sasi_t *s)
{
    sasi_set_phase(s, PC98_SASI_STATUS);
    sasi_raise_irq(s);
}

void
pc98_sasi_init(pc98_sasi_t *s, const pc98_sasi_ops_t *ops)
{
    memset(s, 0, sizeof(*s));
    if (ops)
        s->ops = *ops;
    /*
     * Both selectors power up as "not connected".  The 86Box wrapper changes
     * only the selector for each image it actually attaches.
     */
    s->dip = (uint8_t) ((PC98_SASI_DIP_DISCONNECTED << 3) |
                        PC98_SASI_DIP_DISCONNECTED);
    pc98_sasi_reset(s);
}

void
pc98_sasi_reset(pc98_sasi_t *s)
{
    s->ctrl    = 0u;
    s->data    = 0u;
    s->int_pending = 0u;
    memset(&s->lines, 0, sizeof(s->lines));
    sasi_set_phase(s, PC98_SASI_BUSFREE);
    s->cdb_pos = 0u;
    memset(s->cdb, 0, sizeof(s->cdb));
    s->buf_len = s->buf_pos = 0u;
    s->status  = PC98_SASI_GOOD;
    s->message = 0u;
    s->dataout_action = PC98_SASI_DATAOUT_NONE;
    s->sel_id  = 0u;
    s->unit    = 0u;
    s->xfer_lba = s->xfer_blocks = 0u;
    /* S1410 power-up/reset defaults, Owner's Manual section 4.5.15. */
    s->drive_cylinders         = 153u;
    s->drive_heads             = 4u;
    s->reduced_write_cylinder  = 128u;
    s->precomp_cylinder        = 64u;
    s->max_ecc_burst           = 11u;
    s->last_ecc_burst          = 0u;
    memset(s->assign_params, 0, sizeof(s->assign_params));
    for (unsigned i = 0; i < PC98_SASI_MAX_UNITS; i++) {
        s->sense[i]     = PC98_SASI_SENSE_NONE;
        s->sense_valid[i] = 0u;
        s->sense_lba[i] = 0u;
    }
    sasi_clear_irq(s);
}

void
pc98_sasi_set_dip(pc98_sasi_t *s, uint8_t dip)
{
    /* CT1:CT0 is a controller-type field.  This device is specifically the
     * PC-9801-27, whose documented value is 00. */
    s->dip = (uint8_t) ((dip & ~PC98_SASI_DIP_CT_MASK) |
                        PC98_SASI_DIP_CT_PC9801_27);
}

void
pc98_sasi_set_unit_dip(pc98_sasi_t *s, unsigned unit, unsigned dip)
{
    if (unit >= PC98_SASI_MAX_UNITS || dip >= PC98_SASI_DIP_COUNT)
        return;
    /*
     * The board ROM decodes bits 5:3 as SASI-1/LUN 0 and bits 2:0 as
     * SASI-2/LUN 1.  This ordering is easy to reverse because the low
     * selector is physically the second drive.
     */
    if (unit == 0u)
        s->dip = (uint8_t) ((s->dip & ~0x38u) | (dip << 3));
    else
        s->dip = (uint8_t) ((s->dip & ~0x07u) | dip);
}

void
pc98_sasi_set_unit_sector_size(pc98_sasi_t *s, unsigned unit,
                               unsigned sector_size)
{
    /* Kept so out-of-tree users of the earlier patch still compile.  CT1:CT0
     * is not a per-unit sector-size selector, so changing a host container's
     * granularity must not alter configuration readback. */
    (void) s;
    (void) unit;
    (void) sector_size;
}

unsigned
pc98_sasi_unit_sector_size(const pc98_sasi_t *s, unsigned unit)
{
    (void) s;
    (void) unit;
    return PC98_SASI_BLOCK_SIZE;
}

unsigned
pc98_sasi_unit_track_blocks(const pc98_sasi_t *s, unsigned unit)
{
    (void) s;
    (void) unit;
    return PC98_SASI_TRACK_BLOCKS_256;
}

/* ------------------------------------------------------------------ */
/* Command execution                                                   */
/* ------------------------------------------------------------------ */

#define SASI_TRACK_BAD      0x01u
#define SASI_TRACK_ALT      0x02u
#define SASI_TRACK_REDIRECT 0x04u

static uint32_t
cdb_lba(const uint8_t *cdb)
{
    return (((uint32_t) (cdb[1] & 0x1fu)) << 16) |
           (((uint32_t) cdb[2]) << 8) | (uint32_t) cdb[3];
}

static uint32_t
cdb_count(const uint8_t *cdb)
{
    /* A zero count byte means 256 blocks on SASI. */
    return cdb[4] ? (uint32_t) cdb[4] : 256u;
}

static uint8_t
sasi_completion_status(const pc98_sasi_t *s, int error)
{
    /* Xebec completion status byte: bit 5 is the LUN, bit 1 is ERR. */
    return (uint8_t) (((s->unit & 1u) << 5) |
                     (error ? PC98_SASI_CHECK : PC98_SASI_GOOD));
}

static void
sasi_clear_sense(pc98_sasi_t *s)
{
    s->sense[s->unit]       = PC98_SASI_SENSE_NONE;
    s->sense_valid[s->unit] = 0u;
    s->sense_lba[s->unit]   = 0u;
}

static void
sasi_set_success_address(pc98_sasi_t *s, uint32_t lba)
{
    s->sense[s->unit]       = PC98_SASI_SENSE_NONE;
    s->sense_valid[s->unit] = 1u;
    s->sense_lba[s->unit]   = lba;
}

static void
sasi_fail(pc98_sasi_t *s, uint8_t sense, uint32_t lba, int address_valid)
{
    s->status                 = sasi_completion_status(s, 1);
    s->sense[s->unit]         = sense;
    s->sense_valid[s->unit]   = address_valid ? 1u : 0u;
    s->sense_lba[s->unit]     = lba;
    s->dataout_action         = PC98_SASI_DATAOUT_NONE;
    sasi_enter_status(s);
}

static void
sasi_ok(pc98_sasi_t *s)
{
    s->status         = sasi_completion_status(s, 0);
    s->dataout_action = PC98_SASI_DATAOUT_NONE;
    sasi_enter_status(s);
}

static int
sasi_validate_range(pc98_sasi_t *s, uint32_t lba, uint32_t count)
{
    const uint32_t cap = sasi_capacity(s, s->unit);

    if (!cap) {
        sasi_fail(s, PC98_SASI_SENSE_NOT_READY, lba, 1);
        return 0;
    }
    if (count > cap || lba >= cap || lba > cap - count) {
        sasi_fail(s, PC98_SASI_SENSE_BAD_LBA, lba, 1);
        return 0;
    }
    return 1;
}

static int
sasi_validate_interleave(pc98_sasi_t *s)
{
    const unsigned max_interleave = PC98_SASI_TRACK_BLOCKS_256 - 2u;
    const unsigned interleave = s->cdb[4];

    /* Rev-E firmware explicitly accepts zero and converts it to an
     * interleave of one.  Only values above sectors-per-track minus one
     * are rejected, and the supplement calls that an Invalid Command. */
    if (interleave > max_interleave) {
        sasi_fail(s, PC98_SASI_SENSE_BAD_CMD, cdb_lba(s->cdb), 1);
        return 0;
    }
    return 1;
}

static unsigned
sasi_track_of(const pc98_sasi_t *s, uint32_t lba)
{
    return (unsigned) (lba / pc98_sasi_unit_track_blocks(s, s->unit));
}

static uint32_t
sasi_track_start(const pc98_sasi_t *s, uint32_t lba)
{
    const uint32_t n = pc98_sasi_unit_track_blocks(s, s->unit);
    return (lba / n) * n;
}

static void
sasi_clear_track_metadata(pc98_sasi_t *s, unsigned track)
{
    if (track >= PC98_SASI_MAX_TRACKS)
        return;
    s->track_flags[s->unit][track] = 0u;
    s->alt_track[s->unit][track]   = 0u;
}

static void
sasi_ecc_invalidate(pc98_sasi_t *s, uint32_t lba)
{
    const unsigned slot = (unsigned) (lba & (PC98_SASI_ECC_CACHE_ENTRIES - 1u));
    if (s->ecc_valid[s->unit][slot] && s->ecc_lba[s->unit][slot] == lba)
        s->ecc_valid[s->unit][slot] = 0u;
}

static void
sasi_ecc_invalidate_range(pc98_sasi_t *s, uint32_t lba, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        sasi_ecc_invalidate(s, lba + i);
}

static void
sasi_ecc_store(pc98_sasi_t *s, uint32_t lba, const uint8_t *ecc)
{
    const unsigned slot = (unsigned) (lba & (PC98_SASI_ECC_CACHE_ENTRIES - 1u));
    s->ecc_lba[s->unit][slot]   = lba;
    s->ecc_valid[s->unit][slot] = 1u;
    memcpy(s->ecc_data[s->unit][slot], ecc, PC98_SASI_LONG_ECC_SIZE);
}

static void
sasi_ecc_load(pc98_sasi_t *s, uint32_t lba, uint8_t *ecc)
{
    const unsigned slot = (unsigned) (lba & (PC98_SASI_ECC_CACHE_ENTRIES - 1u));
    if (s->ecc_valid[s->unit][slot] && s->ecc_lba[s->unit][slot] == lba)
        memcpy(ecc, s->ecc_data[s->unit][slot], PC98_SASI_LONG_ECC_SIZE);
    else
        memset(ecc, 0, PC98_SASI_LONG_ECC_SIZE);
}

/*
 * Translate a host-visible LBA through the S1410 Rev-E bad/alternate-track
 * metadata.  Direct accesses to an alternate are illegal; accesses to a
 * spared bad track are transparently redirected to the assigned alternate.
 */
static int
sasi_translate_lba(pc98_sasi_t *s, uint32_t lba, uint32_t *physical,
                   uint8_t *sense)
{
    const uint32_t cap = sasi_capacity(s, s->unit);
    const uint32_t track_blocks = pc98_sasi_unit_track_blocks(s, s->unit);
    const unsigned track = sasi_track_of(s, lba);
    const uint32_t offset = lba % track_blocks;

    if (lba >= cap) {
        *sense = PC98_SASI_SENSE_BAD_LBA;
        return 0;
    }

    if (track >= PC98_SASI_MAX_TRACKS) {
        *physical = lba;
        return 1;
    }

    const uint8_t flags = s->track_flags[s->unit][track];
    if (flags & SASI_TRACK_ALT) {
        *sense = PC98_SASI_SENSE_DIRECT_ALT;
        return 0;
    }

    if (flags & SASI_TRACK_BAD) {
        if (!(flags & SASI_TRACK_REDIRECT)) {
            *sense = PC98_SASI_SENSE_BAD_TRACK;
            return 0;
        }

        const unsigned alt = s->alt_track[s->unit][track];
        if (alt >= PC98_SASI_MAX_TRACKS ||
            !(s->track_flags[s->unit][alt] & SASI_TRACK_ALT)) {
            *sense = PC98_SASI_SENSE_ALT_BROKEN;
            return 0;
        }

        const uint32_t translated = (uint32_t) alt * track_blocks + offset;
        if (translated >= cap) {
            *sense = PC98_SASI_SENSE_ALT_BROKEN;
            return 0;
        }
        *physical = translated;
        return 1;
    }

    *physical = lba;
    return 1;
}

static int
sasi_media_read(pc98_sasi_t *s, uint32_t lba, uint32_t count, uint8_t *buf,
                uint32_t *failed_lba, uint8_t *sense)
{
    if (!s->ops.read_blocks) {
        *failed_lba = lba;
        *sense = PC98_SASI_SENSE_NOT_READY;
        return 0;
    }

    for (uint32_t i = 0; i < count; i++) {
        uint32_t physical;
        if (!sasi_translate_lba(s, lba + i, &physical, sense)) {
            *failed_lba = lba + i;
            return 0;
        }
        if (!s->ops.read_blocks(s->ops.priv, s->unit, physical, 1u,
                                buf + (size_t) i * PC98_SASI_BLOCK_SIZE)) {
            *failed_lba = lba + i;
            *sense = PC98_SASI_SENSE_NOT_READY;
            return 0;
        }
    }
    return 1;
}

static int
sasi_media_write(pc98_sasi_t *s, uint32_t lba, uint32_t count,
                 const uint8_t *buf, uint32_t *failed_lba, uint8_t *sense)
{
    if (!s->ops.write_blocks) {
        *failed_lba = lba;
        *sense = PC98_SASI_SENSE_NOT_READY;
        return 0;
    }

    for (uint32_t i = 0; i < count; i++) {
        uint32_t physical;
        if (!sasi_translate_lba(s, lba + i, &physical, sense)) {
            *failed_lba = lba + i;
            return 0;
        }
        if (!s->ops.write_blocks(s->ops.priv, s->unit, physical, 1u,
                                 buf + (size_t) i * PC98_SASI_BLOCK_SIZE)) {
            *failed_lba = lba + i;
            *sense = PC98_SASI_SENSE_NOT_READY;
            return 0;
        }
        sasi_ecc_invalidate(s, lba + i);
    }
    return 1;
}

static void
sasi_fill_format_pattern(pc98_sasi_t *s, uint32_t start, uint32_t blocks,
                         uint8_t default_pattern)
{
    if (!(s->cdb[5] & 0x20u)) {
        memset(s->buf, default_pattern,
               (size_t) blocks * PC98_SASI_BLOCK_SIZE);
        return;
    }

    (void) start;
    for (uint32_t i = 0; i < blocks; i++)
        memcpy(s->buf + (size_t) i * PC98_SASI_BLOCK_SIZE,
               s->sector_buf, PC98_SASI_BLOCK_SIZE);
}

static int
sasi_format_raw_range(pc98_sasi_t *s, uint32_t start, uint32_t blocks,
                      uint8_t default_pattern, uint32_t *failed_lba)
{
    if (!s->ops.write_blocks) {
        *failed_lba = start;
        return 0;
    }

    uint32_t pos = start;
    uint32_t left = blocks;
    while (left) {
        uint32_t n = left;
        if (n > PC98_SASI_BUF_BLOCKS)
            n = PC98_SASI_BUF_BLOCKS;
        sasi_fill_format_pattern(s, pos, n, default_pattern);
        if (!s->ops.write_blocks(s->ops.priv, s->unit, pos, n, s->buf)) {
            *failed_lba = pos;
            return 0;
        }
        sasi_ecc_invalidate_range(s, pos, n);
        pos += n;
        left -= n;
    }
    return 1;
}

static uint32_t
sasi_track_length(const pc98_sasi_t *s, uint32_t start, uint32_t cap)
{
    if (start >= cap)
        return 0u;
    uint32_t n = cap - start;
    const uint32_t track_blocks = pc98_sasi_unit_track_blocks(s, s->unit);
    if (n > track_blocks)
        n = track_blocks;
    return n;
}

static void sasi_complete_dataout(pc98_sasi_t *s);

/*
 * Offer the buffer to the 8237 first.  When the board is jumpered for DMA
 * the BIOS arms a channel and never touches port 80h for the payload; when
 * it is not, or the channel is masked, dma_*() report 0 bytes moved and the
 * data phase falls back to the byte-at-a-time handshake the ROM implements.
 */
static void
sasi_begin_datain(pc98_sasi_t *s)
{
    s->buf_pos = 0u;
    sasi_set_phase(s, PC98_SASI_DATAIN);

    if (s->ops.dma_write && (s->ctrl & PC98_SASI_CTL_DMA)) {
        uint32_t moved = s->ops.dma_write(s->ops.priv, s->buf, s->buf_len);
        if (moved > s->buf_len)
            moved = s->buf_len;
        s->buf_pos = moved;
        if (s->buf_pos >= s->buf_len)
            sasi_ok(s);
    }
}

static void
sasi_begin_dataout(pc98_sasi_t *s, uint32_t len,
                   pc98_sasi_dataout_action_t action)
{
    if (len > sizeof(s->buf)) {
        sasi_fail(s, PC98_SASI_SENSE_BAD_CDB, cdb_lba(s->cdb),
                  action == PC98_SASI_DATAOUT_WRITE ||
                  action == PC98_SASI_DATAOUT_WRITE_LONG ||
                  action == PC98_SASI_DATAOUT_FORMAT_ALT);
        return;
    }

    s->buf_len        = len;
    s->buf_pos        = 0u;
    s->dataout_action = action;
    sasi_set_phase(s, PC98_SASI_DATAOUT);

    if (s->ops.dma_read && (s->ctrl & PC98_SASI_CTL_DMA)) {
        uint32_t moved = s->ops.dma_read(s->ops.priv, s->buf, len);
        if (moved > len)
            moved = len;
        s->buf_pos = moved;
        if (s->buf_pos >= len)
            sasi_complete_dataout(s);
    }
}

static void
sasi_complete_dataout(pc98_sasi_t *s)
{
    const pc98_sasi_dataout_action_t action = s->dataout_action;
    const uint32_t cap = sasi_capacity(s, s->unit);

    switch (action) {
        case PC98_SASI_DATAOUT_WRITE: {
            uint32_t failed;
            uint8_t sense;
            if (!sasi_media_write(s, s->xfer_lba, s->xfer_blocks, s->buf,
                                  &failed, &sense))
                sasi_fail(s, sense, failed, 1);
            else {
                sasi_set_success_address(s,
                                         s->xfer_lba + s->xfer_blocks - 1u);
                sasi_ok(s);
            }
            break;
        }

        case PC98_SASI_DATAOUT_INIT_DRIVE: {
            const uint16_t cylinders = (uint16_t) (((uint16_t) s->buf[0] << 8) |
                                                   (uint16_t) s->buf[1]);
            const uint8_t heads = s->buf[2];
            const uint16_t reduced = (uint16_t) (((uint16_t) s->buf[3] << 8) |
                                                 (uint16_t) s->buf[4]);
            const uint16_t precomp = (uint16_t) (((uint16_t) s->buf[5] << 8) |
                                                 (uint16_t) s->buf[6]);
            const uint8_t ecc = s->buf[7];

            if (!cylinders || !heads || heads > 8u || ecc > 11u) {
                sasi_fail(s, PC98_SASI_SENSE_BAD_CDB, 0u, 0);
                break;
            }
            s->drive_cylinders        = cylinders;
            s->drive_heads            = heads;
            s->reduced_write_cylinder = reduced;
            s->precomp_cylinder       = precomp;
            s->max_ecc_burst          = ecc;
            sasi_ok(s);
            break;
        }

        case PC98_SASI_DATAOUT_FORMAT_ALT: {
            const uint32_t bad_start = sasi_track_start(s, cdb_lba(s->cdb));
            const uint32_t alt_lba = (((uint32_t) (s->buf[0] & 0x1fu)) << 16) |
                                     ((uint32_t) s->buf[1] << 8) |
                                     (uint32_t) s->buf[2];
            const uint32_t alt_start = sasi_track_start(s, alt_lba);
            const unsigned bad_track = sasi_track_of(s, bad_start);
            const unsigned alt_track = sasi_track_of(s, alt_start);
            uint32_t failed;

            if (!cap) {
                sasi_fail(s, PC98_SASI_SENSE_NOT_READY, bad_start, 1);
                break;
            }
            if (bad_start >= cap || alt_start >= cap ||
                bad_track >= PC98_SASI_MAX_TRACKS ||
                alt_track >= PC98_SASI_MAX_TRACKS) {
                sasi_fail(s, PC98_SASI_SENSE_BAD_LBA,
                          bad_start >= cap ? bad_start : alt_start, 1);
                break;
            }
            if (bad_track == alt_track) {
                sasi_fail(s, PC98_SASI_SENSE_ALT_SAME, bad_start, 1);
                break;
            }
            if (s->track_flags[s->unit][bad_track] & SASI_TRACK_ALT) {
                sasi_fail(s, PC98_SASI_SENSE_DIRECT_ALT, bad_start, 1);
                break;
            }
            if (s->track_flags[s->unit][alt_track] &
                (SASI_TRACK_BAD | SASI_TRACK_ALT)) {
                sasi_fail(s, PC98_SASI_SENSE_ALT_USED, alt_start, 1);
                break;
            }

            const uint32_t alt_n = sasi_track_length(s, alt_start, cap);
            const uint32_t bad_n = sasi_track_length(s, bad_start, cap);
            if (!sasi_format_raw_range(s, alt_start, alt_n, 0x6cu, &failed) ||
                !sasi_format_raw_range(s, bad_start, bad_n, 0x6cu, &failed)) {
                sasi_fail(s, PC98_SASI_SENSE_NOT_READY, failed, 1);
                break;
            }

            s->track_flags[s->unit][alt_track] = SASI_TRACK_ALT;
            s->track_flags[s->unit][bad_track] =
                SASI_TRACK_BAD | SASI_TRACK_REDIRECT;
            s->alt_track[s->unit][bad_track] = (uint16_t) alt_track;
            sasi_set_success_address(s, bad_start + bad_n);
            sasi_ok(s);
            break;
        }

        case PC98_SASI_DATAOUT_WRITE_BUFFER:
            memcpy(s->sector_buf, s->buf, PC98_SASI_BLOCK_SIZE);
            sasi_ok(s);
            break;

        case PC98_SASI_DATAOUT_WRITE_LONG: {
            uint32_t failed;
            uint8_t sense;
            for (uint32_t i = 0; i < s->xfer_blocks; i++) {
                const uint8_t *entry = s->buf + (size_t) i * PC98_SASI_LONG_BLOCK_SIZE;
                if (!sasi_media_write(s, s->xfer_lba + i, 1u, entry,
                                      &failed, &sense)) {
                    sasi_fail(s, sense, failed, 1);
                    return;
                }
                sasi_ecc_store(s, s->xfer_lba + i,
                               entry + PC98_SASI_BLOCK_SIZE);
            }
            sasi_set_success_address(s,
                                     s->xfer_lba + s->xfer_blocks - 1u);
            sasi_ok(s);
            break;
        }

        case PC98_SASI_DATAOUT_ASSIGN_DRIVE:
            memcpy(s->assign_params[s->unit], s->buf,
                   sizeof(s->assign_params[s->unit]));
            sasi_ok(s);
            break;

        case PC98_SASI_DATAOUT_NONE:
        default:
            sasi_fail(s, PC98_SASI_SENSE_BAD_CDB, 0u, 0);
            break;
    }
}

static void
sasi_execute(pc98_sasi_t *s)
{
    const uint8_t op = s->cdb[0];
    const uint32_t lba = cdb_lba(s->cdb);
    const uint32_t count = cdb_count(s->cdb);
    const uint32_t cap = sasi_capacity(s, s->unit);

    s->message = 0u;
    s->dataout_action = PC98_SASI_DATAOUT_NONE;

    /* S1410 sense is for the immediately preceding command only.  REQUEST
     * SENSE itself is the one command that consumes rather than clears it. */
    if (op != 0x03u)
        sasi_clear_sense(s);

    switch (op) {
        case 0x00u: /* TEST DRIVE READY */
            if (!cap)
                sasi_fail(s, PC98_SASI_SENSE_NOT_READY, 0u, 0);
            else
                sasi_ok(s);
            break;

        case 0x01u: /* RECALIBRATE */
            if (!cap)
                sasi_fail(s, PC98_SASI_SENSE_NOT_READY, 0u, 0);
            else
                sasi_ok(s);
            break;

        case 0x02u: /* reserved */
            sasi_fail(s, PC98_SASI_SENSE_BAD_CMD, 0u, 0);
            break;

        case 0x03u: { /* REQUEST SENSE STATUS */
            const uint8_t sense = s->sense[s->unit];
            const uint8_t valid = s->sense_valid[s->unit];
            const uint32_t sense_lba = s->sense_lba[s->unit];
            s->buf[0] = (uint8_t) (sense | (valid ? 0x80u : 0u));
            s->buf[1] = (uint8_t) (((s->unit & 1u) << 5) |
                                   ((sense_lba >> 16) & 0x1fu));
            s->buf[2] = (uint8_t) (sense_lba >> 8);
            s->buf[3] = (uint8_t) sense_lba;
            s->buf_len = 4u;
            sasi_clear_sense(s);
            s->status = sasi_completion_status(s, 0);
            sasi_begin_datain(s);
            break;
        }

        case 0x04u: { /* FORMAT DRIVE */
            const uint32_t start = sasi_track_start(s, lba);
            uint32_t failed;

            if (!cap) {
                sasi_fail(s, PC98_SASI_SENSE_NOT_READY, start, 1);
                break;
            }
            if (start >= cap) {
                sasi_fail(s, PC98_SASI_SENSE_BAD_LBA, start, 1);
                break;
            }
            if (!sasi_validate_interleave(s))
                break;
            if (!sasi_format_raw_range(s, start, cap - start, 0x6cu, &failed)) {
                sasi_fail(s, PC98_SASI_SENSE_NOT_READY, failed, 1);
                break;
            }

            const unsigned first_track = sasi_track_of(s, start);
            const unsigned end_track = sasi_track_of(s, cap - 1u);
            for (unsigned t = first_track;
                 t <= end_track && t < PC98_SASI_MAX_TRACKS; t++)
                sasi_clear_track_metadata(s, t);

            sasi_set_success_address(s, cap);
            sasi_ok(s);
            break;
        }

        case 0x05u: { /* CHECK TRACK FORMAT */
            const uint32_t start = sasi_track_start(s, lba);
            if (!cap) {
                sasi_fail(s, PC98_SASI_SENSE_NOT_READY, start, 1);
                break;
            }
            if (start >= cap) {
                sasi_fail(s, PC98_SASI_SENSE_BAD_LBA, start, 1);
                break;
            }
            if (!sasi_validate_interleave(s))
                break;

            const unsigned track = sasi_track_of(s, start);
            if (track < PC98_SASI_MAX_TRACKS) {
                const uint8_t flags = s->track_flags[s->unit][track];
                if (flags & SASI_TRACK_ALT) {
                    sasi_fail(s, PC98_SASI_SENSE_DIRECT_ALT, start, 1);
                    break;
                }
                if ((flags & SASI_TRACK_BAD) && !(flags & SASI_TRACK_REDIRECT)) {
                    sasi_fail(s, PC98_SASI_SENSE_BAD_TRACK, start, 1);
                    break;
                }
            }
            sasi_set_success_address(s, start + sasi_track_length(s, start, cap));
            sasi_ok(s);
            break;
        }

        case 0x06u: { /* FORMAT TRACK */
            const uint32_t start = sasi_track_start(s, lba);
            uint32_t failed;
            if (!cap) {
                sasi_fail(s, PC98_SASI_SENSE_NOT_READY, start, 1);
                break;
            }
            if (start >= cap) {
                sasi_fail(s, PC98_SASI_SENSE_BAD_LBA, start, 1);
                break;
            }
            if (!sasi_validate_interleave(s))
                break;
            const uint32_t n = sasi_track_length(s, start, cap);
            /* NEC's PC-98 "standard format" path depends on FORMAT TRACK
             * (06h) leaving an E5h-filled data field.  FORMAT DRIVE (04h)
             * retains the controller-family 6Ch default; only 06h gets this
             * PC-98 compatibility behavior.  A control-bit-5 sector-buffer
             * pattern still overrides the default in either case. */
            if (!sasi_format_raw_range(s, start, n, 0xe5u, &failed)) {
                sasi_fail(s, PC98_SASI_SENSE_NOT_READY, failed, 1);
                break;
            }
            sasi_clear_track_metadata(s, sasi_track_of(s, start));
            sasi_set_success_address(s, start + n);
            sasi_ok(s);
            break;
        }

        case 0x07u: { /* FORMAT BAD TRACK */
            const uint32_t start = sasi_track_start(s, lba);
            if (!cap) {
                sasi_fail(s, PC98_SASI_SENSE_NOT_READY, start, 1);
                break;
            }
            if (start >= cap) {
                sasi_fail(s, PC98_SASI_SENSE_BAD_LBA, start, 1);
                break;
            }
            if (!sasi_validate_interleave(s))
                break;
            const unsigned track = sasi_track_of(s, start);
            if (track < PC98_SASI_MAX_TRACKS) {
                s->track_flags[s->unit][track] = SASI_TRACK_BAD;
                s->alt_track[s->unit][track] = 0u;
            }
            /* FORMAT BAD TRACK writes ID fields only; preserve data bytes. */
            sasi_set_success_address(s, start + sasi_track_length(s, start, cap));
            sasi_ok(s);
            break;
        }

        case 0x08u: { /* READ */
            uint32_t failed;
            uint8_t sense;
            if (!sasi_validate_range(s, lba, count))
                break;
            if (!sasi_media_read(s, lba, count, s->buf, &failed, &sense)) {
                sasi_fail(s, sense, failed, 1);
                break;
            }
            s->xfer_lba    = lba;
            s->xfer_blocks = count;
            s->buf_len     = count * PC98_SASI_BLOCK_SIZE;
            s->status      = sasi_completion_status(s, 0);
            sasi_set_success_address(s, lba + count - 1u);
            sasi_begin_datain(s);
            break;
        }

        case 0x09u: { /* READ VERIFY - S1410A; reserved on early S1410 */
            uint32_t failed;
            uint8_t sense;
            if (!sasi_validate_range(s, lba, count))
                break;
            if (!sasi_media_read(s, lba, count, s->buf, &failed, &sense)) {
                sasi_fail(s, sense, failed, 1);
                break;
            }
            sasi_set_success_address(s, lba + count - 1u);
            sasi_ok(s);
            break;
        }

        case 0x0au: /* WRITE */
            if (!sasi_validate_range(s, lba, count))
                break;
            s->xfer_lba    = lba;
            s->xfer_blocks = count;
            s->status      = sasi_completion_status(s, 0);
            sasi_begin_dataout(s, count * PC98_SASI_BLOCK_SIZE,
                               PC98_SASI_DATAOUT_WRITE);
            break;

        case 0x0bu: { /* SEEK */
            uint32_t physical;
            uint8_t sense;
            if (!sasi_validate_range(s, lba, 1u))
                break;
            if (!sasi_translate_lba(s, lba, &physical, &sense)) {
                sasi_fail(s, sense, lba, 1);
                break;
            }
            (void) physical; /* image target has no mechanical seek latency */
            sasi_set_success_address(s, lba);
            sasi_ok(s);
            break;
        }

        case 0x0cu: /* INITIALIZE DRIVE CHARACTERISTICS - eight parameter bytes */
            sasi_begin_dataout(s, 8u, PC98_SASI_DATAOUT_INIT_DRIVE);
            break;

        case 0x0du: /* READ ECC BURST ERROR LENGTH */
            s->buf[0] = s->last_ecc_burst;
            s->buf_len = 1u;
            s->status = sasi_completion_status(s, 0);
            sasi_begin_datain(s);
            break;

        case 0x0eu: /* FORMAT ALTERNATE TRACK - Rev E/S1410A */
            if (!cap) {
                sasi_fail(s, PC98_SASI_SENSE_NOT_READY, lba, 1);
                break;
            }
            if (sasi_track_start(s, lba) >= cap) {
                sasi_fail(s, PC98_SASI_SENSE_BAD_LBA, lba, 1);
                break;
            }
            if (!sasi_validate_interleave(s))
                break;
            sasi_begin_dataout(s, 3u, PC98_SASI_DATAOUT_FORMAT_ALT);
            break;

        case 0x0fu: /* WRITE SECTOR BUFFER - Rev E/S1410A */
            sasi_begin_dataout(s, PC98_SASI_BLOCK_SIZE,
                               PC98_SASI_DATAOUT_WRITE_BUFFER);
            break;

        case 0x10u: /* READ SECTOR BUFFER - Rev E/S1410A */
            memcpy(s->buf, s->sector_buf, PC98_SASI_BLOCK_SIZE);
            s->buf_len = PC98_SASI_BLOCK_SIZE;
            s->status = sasi_completion_status(s, 0);
            sasi_begin_datain(s);
            break;

        case 0xe0u: /* RAM DIAGNOSTIC */
            /* The emulated buffer is ordinary host memory; a completed
             * pattern test therefore has no failure mode to report. */
            sasi_ok(s);
            break;

        case 0xe1u: /* reserved */
        case 0xe2u: /* reserved */
            sasi_fail(s, PC98_SASI_SENSE_BAD_CMD, 0u, 0);
            break;

        case 0xe3u: { /* DRIVE DIAGNOSTIC */
            if (!cap) {
                sasi_fail(s, PC98_SASI_SENSE_NOT_READY, 0u, 0);
                break;
            }
            if (!s->ops.read_blocks) {
                sasi_fail(s, PC98_SASI_SENSE_NOT_READY, 0u, 0);
                break;
            }
            /* The real diagnostic verifies sector 0 on every track.  There
             * are no separately represented ID/ECC fields in the image, but
             * reading every track start still validates the complete datapath. */
            const uint32_t track_blocks = pc98_sasi_unit_track_blocks(s, s->unit);
            for (uint32_t pos = 0u; pos < cap; pos += track_blocks) {
                if (!s->ops.read_blocks(s->ops.priv, s->unit, pos, 1u, s->buf)) {
                    sasi_fail(s, PC98_SASI_SENSE_NOT_READY, pos, 0);
                    return;
                }
            }
            sasi_ok(s);
            break;
        }

        case 0xe4u: /* CONTROLLER INTERNAL DIAGNOSTICS */
            sasi_ok(s);
            break;

        case 0xe5u: { /* READ LONG */
            uint32_t failed;
            uint8_t sense;
            if (!sasi_validate_range(s, lba, count))
                break;
            if (!sasi_media_read(s, lba, count, s->buf, &failed, &sense)) {
                sasi_fail(s, sense, failed, 1);
                break;
            }
            /* Expand from the end so the 256-byte input blocks can be turned
             * into 260-byte data+ECC records in-place without clobbering. */
            for (uint32_t i = count; i-- > 0u;) {
                uint8_t *dst = s->buf + (size_t) i * PC98_SASI_LONG_BLOCK_SIZE;
                uint8_t *src = s->buf + (size_t) i * PC98_SASI_BLOCK_SIZE;
                memmove(dst, src, PC98_SASI_BLOCK_SIZE);
                sasi_ecc_load(s, lba + i, dst + PC98_SASI_BLOCK_SIZE);
            }
            s->xfer_lba    = lba;
            s->xfer_blocks = count;
            s->buf_len     = count * PC98_SASI_LONG_BLOCK_SIZE;
            s->status      = sasi_completion_status(s, 0);
            sasi_set_success_address(s, lba + count - 1u);
            sasi_begin_datain(s);
            break;
        }

        case 0xe6u: /* WRITE LONG */
            if (!sasi_validate_range(s, lba, count))
                break;
            s->xfer_lba    = lba;
            s->xfer_blocks = count;
            s->status      = sasi_completion_status(s, 0);
            sasi_begin_dataout(s, count * PC98_SASI_LONG_BLOCK_SIZE,
                               PC98_SASI_DATAOUT_WRITE_LONG);
            break;

        case 0xe7u: /* RETRY STATISTICS - S1410A */
            if (!cap) {
                sasi_fail(s, PC98_SASI_SENSE_NOT_READY, 0u, 0);
                break;
            }
            /* This ideal image target never performs physical retries. */
            memset(s->buf, 0, 8u);
            s->buf_len = 8u;
            s->status = sasi_completion_status(s, 0);
            sasi_begin_datain(s);
            break;

        case 0xc2u: /* PC-9801-27 ASSIGN DRIVE / controller parameter block */
            /* The option ROM sends this vendor command to both LUNs during
             * POST, including an absent LUN, and then writes exactly 10 bytes. */
            sasi_begin_dataout(s, 10u, PC98_SASI_DATAOUT_ASSIGN_DRIVE);
            break;

        default:
            sasi_fail(s, PC98_SASI_SENSE_BAD_CMD, 0u, 0);
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Port handlers                                                       */
/* ------------------------------------------------------------------ */

uint8_t
pc98_sasi_status_in(pc98_sasi_t *s)
{
    uint8_t v;

    /* With the readback selector clear the port is the configuration byte,
     * not the bus.  The BIOS reads its drive types this way. */
    if (!(s->ctrl & PC98_SASI_CTL_STS))
        return s->dip;

    v = (s->lines.req ? PC98_SASI_ST_REQ : 0u) |
        (s->lines.ack ? PC98_SASI_ST_ACK : 0u) |
        (s->lines.bsy ? PC98_SASI_ST_BSY : 0u) |
        (s->lines.msg ? PC98_SASI_ST_MSG : 0u) |
        (s->lines.cd  ? PC98_SASI_ST_CD  : 0u) |
        (s->lines.io  ? PC98_SASI_ST_IO  : 0u);
    /* Bit 0 reports the actual gated INT3 output, not merely a latched
     * command-complete flag.  With C1h in status phase the ROM therefore
     * reads 0ADh; after its ISR writes C0h it reads plain 0ACh. */
    if (s->int_pending && (s->ctrl & PC98_SASI_CTL_IEN))
        v |= PC98_SASI_ST_INT;

    return v;
}

void
pc98_sasi_ctrl_out(pc98_sasi_t *s, uint8_t val)
{
    const uint8_t prev = s->ctrl;

    s->ctrl = val;

    /* Bits 0 and 1 are independent gates: bit 0 is INT3 enable and bit 1
     * is DMA enable.  In particular, the ROM's C1h probe enables IRQ only,
     * while C3h enables both. */
    if ((prev ^ val) & PC98_SASI_CTL_IEN)
        sasi_update_irq(s);

    /*
     * Reset is a pulse: the board ROM writes 08h then 00h.  Hold the bus
     * quiescent while RST is asserted and perform the reset when it is
     * released on the falling edge.
     */
    s->lines.rst = (val & PC98_SASI_CTL_RST) != 0u;
    s->lines.sel = (val & PC98_SASI_CTL_SEL) != 0u;
    if (val & PC98_SASI_CTL_RST)
        return;
    if ((prev & PC98_SASI_CTL_RST) && !(val & PC98_SASI_CTL_RST)) {
        pc98_sasi_reset(s);
        return;
    }

    /*
     * Rising SEL selects the SASI controller target.  The PC-9801-27 ROM
     * always writes 01h here: both physical drive connectors are LUNs 0/1
     * behind target ID 0, selected later by CDB byte 1 bits 7:5.  Treating
     * bit 1 as a second target makes the topology disagree with the ROM.
     *
     * The controller target itself responds even if no media is attached;
     * media absence is reported by commands such as TEST UNIT READY.
     */
    if ((val & PC98_SASI_CTL_SEL) && !(prev & PC98_SASI_CTL_SEL)) {
        if (s->sel_id == 0x01u) {
            s->unit    = 0u;
            s->cdb_pos = 0u;
            sasi_set_phase(s, PC98_SASI_SELECT);
        } else {
            sasi_set_phase(s, PC98_SASI_BUSFREE);
        }
        return;
    }

    /*
     * Falling SEL after a successful selection moves the target into the
     * command phase.  The ROM does this with `out 82h,0C0h`.
     */
    if (!(val & PC98_SASI_CTL_SEL) && (prev & PC98_SASI_CTL_SEL) &&
        s->phase == PC98_SASI_SELECT) {
        sasi_set_phase(s, PC98_SASI_COMMAND);
        s->cdb_pos = 0u;
        return;
    }
}

uint8_t
pc98_sasi_data_in(pc98_sasi_t *s)
{
    uint8_t v = 0xffu;

    if (!s->lines.req || !s->lines.io)
        return v;
    sasi_ack_begin(s);

    switch (s->phase) {
        case PC98_SASI_DATAIN:
            if (s->buf_pos < s->buf_len) {
                v = s->buf[s->buf_pos++];
                if (s->buf_pos >= s->buf_len)
                    sasi_ok(s);
            }
            break;

        case PC98_SASI_STATUS:
            v        = s->status;
            sasi_set_phase(s, PC98_SASI_MSGIN);
            sasi_clear_irq(s);
            break;

        case PC98_SASI_MSGIN:
            v        = s->message;
            sasi_set_phase(s, PC98_SASI_BUSFREE);
            break;

        default:
            break;
    }

    sasi_ack_end(s);
    return v;
}

void
pc98_sasi_data_out(pc98_sasi_t *s, uint8_t val)
{
    s->data = val;

    /* The pre-selection ID latch is not a SASI information transfer. */
    if (s->phase == PC98_SASI_BUSFREE) {
        s->sel_id = val;
        return;
    }
    if (!s->lines.req || s->lines.io)
        return;
    sasi_ack_begin(s);

    switch (s->phase) {
        case PC98_SASI_COMMAND:
            if (s->cdb_pos < PC98_SASI_CDB_LEN)
                s->cdb[s->cdb_pos++] = val;
            if (s->cdb_pos >= PC98_SASI_CDB_LEN) {
                /* Both drives are LUNs behind target ID 0.  Preserve an
                 * absent LUN too: falling back to the previously selected
                 * unit would make LUN 1 silently alias LUN 0. */
                const unsigned lun_field = (unsigned) (s->cdb[1] >> 5);
                /* S1410 has two drives: byte 1 bit 5 is d, while bits
                 * 7:6 are documented zero.  Do not alias illegal LUN 2..7
                 * onto drive 0/1 just because only bit 5 is physically used. */
                s->unit = lun_field & 1u;
                if (lun_field > 1u)
                    sasi_fail(s, PC98_SASI_SENSE_BAD_CMD, 0u, 0);
                else
                    sasi_execute(s);
            }
            break;

        case PC98_SASI_DATAOUT:
            if (s->buf_pos < s->buf_len) {
                s->buf[s->buf_pos++] = val;
                if (s->buf_pos >= s->buf_len)
                    sasi_complete_dataout(s);
            }
            break;

        default:
            break;
    }
    sasi_ack_end(s);
}
