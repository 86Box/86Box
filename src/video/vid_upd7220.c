/*
 * NEC uPD7220 / Intel 82720 command core.
 *
 * The original upd7220-code branch contained an incomplete C++ -> C port with
 * an empty public state structure and unrelated TI SVGA code appended to it.
 * This replacement is a self-contained C core intended to be driven by an
 * 86Box front-end.  It keeps the documented 16-byte FIFO, command/status
 * semantics, PRAM, CRTC timing fields, cursor, RDAT/WDAT, DMA, light pen and
 * the drawing engine in one deterministic state object.
 */
#include <86box/vid_upd7220.h>

#include <string.h>

#define FIFO_PARAMETER 0
#define FIFO_COMMAND   1
#define FIFO_READ      0
#define FIFO_WRITE     1

#define CMD_RESET   0x00
#define CMD_RESET2  0x01
#define CMD_RESET3  0x09
#define CMD_SYNC    0x0e
#define CMD_VSYNC   0x6e
#define CMD_CCHAR   0x4b
#define CMD_START   0x6b
#define CMD_BLANK   0x0c
#define CMD_BLANK2  0x05
#define CMD_ZOOM    0x46
#define CMD_CURS    0x49
#define CMD_PRAM    0x70
#define CMD_PITCH   0x47
#define CMD_WDAT    0x20
#define CMD_MASK    0x4a
#define CMD_FIGS    0x4c
#define CMD_FIGD    0x6c
#define CMD_GCHRD   0x68
#define CMD_RDAT    0xa0
#define CMD_CURD    0xe0
#define CMD_LPRD    0xc0
#define CMD_DMAR    0xa4
#define CMD_DMAW    0x24
#define CMD_5A      0x5a

#define WORK_NONE 0
#define WORK_FIGD 1
#define WORK_GCHR 2
#define WORK_WDAT 3
#define WORK_RDAT 4

static void engine_schedule(upd7220_t *g);

static uint16_t dummy_read(void *opaque, uint32_t addr)
{
    (void)opaque; (void)addr;
    return 0xffff;
}

static void dummy_write(void *opaque, uint32_t addr, uint16_t value)
{
    (void)opaque; (void)addr; (void)value;
}

static uint16_t rotr16(uint16_t v)
{
    return (uint16_t)((v >> 1) | (v << 15));
}

static uint16_t rotl16(uint16_t v)
{
    return (uint16_t)((v << 1) | (v >> 15));
}

static void fifo_status(upd7220_t *g)
{
    if (g->fifo_count == 0) g->sr |= UPD7220_SR_EMPTY;
    else                    g->sr &= (uint8_t)~UPD7220_SR_EMPTY;

    if (g->fifo_count == 16) g->sr |= UPD7220_SR_FULL;
    else                     g->sr &= (uint8_t)~UPD7220_SR_FULL;

    if (g->fifo_dir == FIFO_READ && g->fifo_count)
        g->sr |= UPD7220_SR_DRDY;
    else
        g->sr &= (uint8_t)~UPD7220_SR_DRDY;
}

static void fifo_clear(upd7220_t *g)
{
    g->fifo_head = g->fifo_tail = g->fifo_count = 0;
    memset(g->fifo, 0, sizeof(g->fifo));
    memset(g->fifo_flag, -1, sizeof(g->fifo_flag));
    fifo_status(g);
}

static void fifo_direction(upd7220_t *g, unsigned dir)
{
    if (g->fifo_dir != dir) {
        fifo_clear(g);
        g->fifo_dir = (uint8_t)dir;
        fifo_status(g);
    }
}

static bool fifo_push(upd7220_t *g, uint8_t value, int flag)
{
    if (g->fifo_count == 16) {
        /* The chip loses the oldest byte on overflow (NEC manual 5-1).
         * Software must observe FULL; overflow never executes a byte early. */
        g->fifo_head = (uint8_t)((g->fifo_head + 1) & 15);
        g->fifo_count--;
    }
    g->fifo[g->fifo_tail] = value;
    g->fifo_flag[g->fifo_tail] = (int8_t)flag;
    g->fifo_tail = (uint8_t)((g->fifo_tail + 1) & 15);
    g->fifo_count++;
    fifo_status(g);
    return true;
}

static bool fifo_pop(upd7220_t *g, uint8_t *value, int *flag)
{
    if (!g->fifo_count) {
        *value = 0xff;
        *flag = -1;
        fifo_status(g);
        return false;
    }
    *value = g->fifo[g->fifo_head];
    *flag = g->fifo_flag[g->fifo_head];
    g->fifo_head = (uint8_t)((g->fifo_head + 1) & 15);
    g->fifo_count--;
    fifo_status(g);
    return true;
}

/* Command processing times from the NEC user's manual, in 2xWCLK periods.
 * Data-transfer commands add memory-cycle work separately. */
static uint8_t command_cost(uint8_t cr)
{
    if (cr == CMD_RESET || cr == CMD_RESET2 || cr == CMD_RESET3) return 6;
    if ((cr & 0xfe) == CMD_SYNC) return 6;
    if ((cr & 0xfe) == CMD_VSYNC) return 12;
    if (cr == CMD_CCHAR) return 10;
    if (cr == CMD_START) return 12;
    if ((cr & 0xfe) == CMD_BLANK || cr == CMD_BLANK2) return 6;
    if (cr == CMD_ZOOM) return 10;
    if (cr == CMD_CURS) return 6;
    if ((cr & 0xf0) == CMD_PRAM) return 10;
    if (cr == CMD_PITCH) return 10;
    if ((cr & 0xe4) == CMD_WDAT) return ((cr >> 3) & 3) == 2 ? 14 : 12;
    if (cr == CMD_MASK) return 10;
    if (cr == CMD_FIGS) return 10;
    if (cr == CMD_FIGD) return 18;
    if (cr == CMD_GCHRD) return 16;
    if ((cr & 0xe4) == CMD_RDAT) return ((cr >> 3) & 3) == 3 ? 12 : 14;
    if (cr == CMD_CURD) return 14;
    if (cr == CMD_LPRD) return 12;
    if ((cr & 0xe4) == CMD_DMAR) return 14;
    if ((cr & 0xe4) == CMD_DMAW) return 12;
    return 6;
}

static uint8_t parameter_cost(const upd7220_t *g)
{
    if ((g->cr & 0xf0) == CMD_PRAM) return 4;
    if ((g->cr & 0xe4) == CMD_WDAT)
        return (g->cr & 0x10) ? 8 : (g->param_ptr ? 4 : 2);
    /* CURS P3 depends on the dot address on a 7220; the 7220A removes that
     * wait (manual appendix E). Model its minimum documented processing time. */
    if (g->cr == CMD_CURS && g->param_ptr == 2) return 4;
    return 2;
}

static void figs_defaults(upd7220_t *g)
{
    /* DIR is not reset by the FIGS post-operation reset; software can rely on
     * the current drawing direction surviving WDAT/FIGD. */
    g->figs.type = 0;
    g->figs.gd = 0;
    g->figs.dc = 0;
    g->figs.d = 8;
    g->figs.d1 = 0xffff;
    g->figs.d2 = 8;
    g->figs.dm = 0xffff;
}

static void reset_figs(upd7220_t *g)
{
    figs_defaults(g);
    g->pattern = (uint16_t)(g->ra[8] | ((uint16_t)g->ra[9] << 8));
}

static void decode_timing(upd7220_t *g)
{
    g->mode = g->pr[0];
    g->aw   = (uint16_t)g->pr[1] + 2;
    g->hs   = (uint16_t)(g->pr[2] & 0x1f) + 1;
    g->vs   = (uint16_t)(((g->pr[3] & 0x03) << 3) | (g->pr[2] >> 5));
    g->hfp  = (uint16_t)(g->pr[3] >> 2) + 1;
    g->hbp  = (uint16_t)(g->pr[4] & 0x3f) + 1;
    g->vfp  = (uint16_t)(g->pr[5] & 0x3f);
    g->al   = (uint16_t)(((g->pr[7] & 0x03) << 8) | g->pr[6]);
    g->al  += (uint16_t)((g->pr[5] >> 6) & 1);
    g->vbp  = (uint16_t)(g->pr[7] >> 2);
    g->pitch = (uint16_t)((((uint16_t)g->pr[4] & 0x40) << 2) | (g->aw & 0xff));
    if (g->timing_changed) g->timing_changed(g->opaque);
}

static uint16_t vram_read(upd7220_t *g, uint32_t addr)
{
    return g->vram_read(g->opaque, addr & 0x3ffffu);
}

static void vram_write(upd7220_t *g, uint32_t addr, uint16_t value)
{
    g->vram_write(g->opaque, addr & 0x3ffffu, value);
}

static uint16_t transfer_mask(unsigned type, uint16_t mask)
{
    switch (type & 3) {
    case 2: return (uint16_t)(mask & 0x00ff);
    case 3: return (uint16_t)(mask & 0xff00);
    default:return mask;
    }
}

static void write_vram_op(upd7220_t *g, unsigned type, unsigned mod,
                          uint16_t data, uint16_t mask)
{
    uint16_t cur = vram_read(g, g->ead);
    uint16_t m = transfer_mask(type, mask);
    uint16_t d = (uint16_t)(data & m);

    switch (mod & 3) {
    case 0: cur = (uint16_t)((cur & ~m) | d); break;       /* replace */
    case 1: cur = (uint16_t)(cur ^ d); break;              /* complement */
    case 2: cur = (uint16_t)(cur & ~d); break;             /* reset */
    case 3: cur = (uint16_t)(cur | d); break;              /* set */
    }
    vram_write(g, g->ead, cur);
}

static uint16_t read_vram_type(upd7220_t *g, unsigned type)
{
    uint16_t v = vram_read(g, g->ead);
    if ((type & 3) == 2) return (uint16_t)(v & 0x00ff);
    if ((type & 3) == 3) return (uint16_t)((v >> 8) & 0x00ff);
    return v;
}

static uint16_t effective_pitch(const upd7220_t *g)
{
    bool mixed = ((g->mode & 0x22) == 0x00);
    return mixed ? (uint16_t)(g->pitch >> (g->figs.gd ? 1 : 0)) : g->pitch;
}

static int32_t sext14(uint16_t v)
{
    v &= 0x3fff;
    return (v & 0x2000) ? (int32_t)(v | 0xffffc000u) : (int32_t)v;
}

/* RDAT/WDAT/DMA step EAD by a word address.  They do not rotate the pixel
 * mask; mask rotation belongs to the FIGD/GCHRD pixel engine. */
static void next_word(upd7220_t *g, unsigned direction)
{
    static const int8_t xdir[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    static const int8_t ydir[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
    int32_t a = (int32_t)g->ead + xdir[direction & 7] +
                ydir[direction & 7] * (int32_t)effective_pitch(g);
    g->ead = (uint32_t)a & 0x3ffffu;
}

static uint16_t pattern_at(const upd7220_t *g, unsigned cycle)
{
    bool mixed = ((g->mode & 0x22) == 0x00);
    bool graph = ((g->mode & 0x22) == 0x02);
    if ((mixed && g->figs.gd) || graph)
        return ((g->pattern >> (cycle & 15)) & 1) ? 0xffff : 0x0000;
    return g->pattern;
}

static void next_pixel(upd7220_t *g, unsigned direction)
{
    uint16_t p = effective_pitch(g);
    switch (direction & 7) {
    case 0: g->ead += p; break;
    case 1: g->ead += p; if (g->mask & 0x8000) g->ead++; g->mask = rotl16(g->mask); break;
    case 2:              if (g->mask & 0x8000) g->ead++; g->mask = rotl16(g->mask); break;
    case 3: g->ead -= p; if (g->mask & 0x8000) g->ead++; g->mask = rotl16(g->mask); break;
    case 4: g->ead -= p; break;
    case 5: g->ead -= p; if (g->mask & 0x0001) g->ead--; g->mask = rotr16(g->mask); break;
    case 6:              if (g->mask & 0x0001) g->ead--; g->mask = rotr16(g->mask); break;
    case 7: g->ead += p; if (g->mask & 0x0001) g->ead--; g->mask = rotr16(g->mask); break;
    }
    g->ead &= 0x3ffffu;
}

static void draw_dot(upd7220_t *g, unsigned cycle)
{
    write_vram_op(g, 0, g->bitmap_mod, pattern_at(g, cycle), g->mask);
}

/* NEC user's manual 2-15..2-18: four 2xWCLK periods per RMW cycle at
 * display zoom 1 or 2; larger zooms lengthen the memory cycle. */
static uint32_t memory_cost(const upd7220_t *g)
{
    return g->disp > 1 ? 2u * ((uint32_t)g->disp + 1) : 4u;
}

static int bus_timing_valid(const upd7220_t *g)
{
    return g->bus_timing_enabled && g->aw && g->hs && g->hfp &&
           g->hbp && g->al && g->vs;
}

static uint32_t bus_line_clocks(const upd7220_t *g)
{
    return ((uint32_t)g->aw + g->hfp + g->hs + g->hbp) * 2u;
}

static uint32_t bus_frame_lines(const upd7220_t *g)
{
    return (uint32_t)g->vbp + g->al + g->vfp + g->vs;
}

static void set_drq(upd7220_t *g, int level)
{
    if (g->dma_drq == !!level) return;
    g->dma_drq = (uint8_t)!!level;
    if (g->drq_changed) g->drq_changed(g->opaque, level);
}

static void dma_request_update(upd7220_t *g)
{
    int request = !!(g->sr & UPD7220_SR_DMA) && g->dma_bytes_left;
    if (request && bus_timing_valid(g)) {
        /* Manual 5-3: DREQ is inhibited throughout horizontal blank and
         * VFP/VS. Flashless DMA is confined to the vertical back porch. */
        request = g->bus_hclock < 2u * g->aw &&
                  g->bus_line < (uint32_t)g->vbp + g->al &&
                  (!(g->mode & 0x10u) || !g->de || g->bus_line < g->vbp);
    }
    set_drq(g, request);
}

static uint32_t dma_bus_event(const upd7220_t *g)
{
    if (!(g->sr & UPD7220_SR_DMA) || !bus_timing_valid(g)) return 0;
    uint32_t h = g->bus_hclock % bus_line_clocks(g);
    uint32_t aw_end = 2u * g->aw;
    return h < aw_end ? aw_end - h : bus_line_clocks(g) - h;
}

/* AW -> HFP -> HS -> HBP. REFRESH wins even with DE clear. In flashless
 * mode the active raster owns the bus; command decoding can still run.
 * See NEC user's manual 2-4, 4-21 and 6-3. */
static uint32_t bus_window(const upd7220_t *g, uint32_t h, uint32_t line,
                            int *available)
{
    const uint32_t word = 2u;
    uint32_t aw = g->aw * word;
    uint32_t hs_start = aw + g->hfp * word;
    uint32_t hs_end = hs_start + g->hs * word;
    uint32_t end;
    int refresh = h >= hs_start && h < hs_end && (g->mode & 0x04u);
    int active = h < aw && line >= g->vbp &&
                 line < (uint32_t)g->vbp + g->al;
    *available = !refresh && !(active && g->de && (g->mode & 0x10u));
    if (h < aw) end = aw;
    else if (h < hs_start) end = hs_start;
    else if (h < hs_end) end = hs_end;
    else end = bus_line_clocks(g);
    return end - h;
}

static void bus_advance(upd7220_t *g, uint32_t clocks)
{
    if (!bus_timing_valid(g)) return;
    uint32_t width = bus_line_clocks(g);
    uint64_t h = (uint64_t)g->bus_hclock + clocks;
    g->bus_line = (uint16_t)(((uint64_t)g->bus_line + h / width) %
                            bus_frame_lines(g));
    g->bus_hclock = (uint32_t)(h % width);
}

/* Reserve a whole RMW operation on a display-word boundary. Never start a
 * cycle that would run into a refresh or protected display slot. Adjacent
 * available intervals are joined, including across a vertical-blank line.
 * All arithmetic is in integer clock edges, not elapsed microseconds. */
static uint32_t memory_wait_for(const upd7220_t *g, uint32_t cost, int host)
{
    if (!bus_timing_valid(g)) return 0;
    const uint32_t word = 2u;
    uint32_t width = bus_line_clocks(g);
    uint32_t height = bus_frame_lines(g);
    uint32_t h = g->bus_hclock % width;
    uint32_t line = g->bus_line % height;
    uint32_t elapsed = 0, start = 0, run = 0;
    uint32_t limit = width * height + cost;
    while (elapsed < limit) {
        int available;
        uint32_t span = bus_window(g, h, line, &available);
        if (host) {
            uint32_t hs_start = 2u * (g->aw + g->hfp);
            uint32_t hs_end = hs_start + 2u * g->hs;
            available = !(h >= hs_start && h < hs_end && (g->mode & 4u));
        }
        if (!available) run = 0;
        else {
            if (!run) {
                uint32_t align = (word - h % word) % word;
                if (align < span) {
                    start = elapsed + align;
                    run = span - align;
                }
            } else run += span;
            if (run >= cost) return start;
        }
        elapsed += span;
        h += span;
        if (h == width) {
            h = 0;
            line = (line + 1u) % height;
        }
    }
    /* Too-small intervals have no complete grant. Reconsider at the next
     * boundary; the out-of-FIFO RESET decoder can always recover. */
    return width - g->bus_hclock % width;
}

static uint32_t memory_wait(const upd7220_t *g)
{
    return memory_wait_for(g, memory_cost(g), 0);
}

uint32_t upd7220_host_bus_begin(upd7220_t *g)
{
    g->host_bus_requested = 1;
    if (g->bus_waiting) {
        /* This is a future grant, not an RMW already owning the memory. */
        g->busy_clocks = 0;
        g->bus_waiting = 0;
    }
    return g->work_kind != WORK_NONE && !g->pending_valid && !g->row_waiting
        ? g->busy_clocks : 0;
}

uint32_t upd7220_host_bus_wait(const upd7220_t *g, uint32_t clocks)
{
    return memory_wait_for(g, clocks, 1);
}

void upd7220_host_bus_end(upd7220_t *g)
{
    g->host_bus_requested = 0;
    engine_schedule(g);
}

void upd7220_enable_bus_timing(upd7220_t *g, int enabled)
{
    g->bus_timing_enabled = (uint8_t)!!enabled;
    if (g->bus_waiting) {
        g->bus_waiting = 0;
        g->busy_clocks = 0;
        engine_schedule(g);
    }
    dma_request_update(g);
}

void upd7220_set_bus_position(upd7220_t *g, uint32_t hclock, uint16_t line)
{
    g->bus_hclock = bus_timing_valid(g) ? hclock % bus_line_clocks(g) : hclock;
    g->bus_line = bus_timing_valid(g) ? line % bus_frame_lines(g) : line;
    if (g->bus_waiting) {
        g->bus_waiting = 0;
        g->busy_clocks = 0;
        engine_schedule(g);
    }
    dma_request_update(g);
}

static void finish_work(upd7220_t *g)
{
    g->work_kind = WORK_NONE;
    g->sr &= (uint8_t)~UPD7220_SR_DRAW;
    reset_figs(g);
}

/* Complete exactly one memory/DDA cycle. Keep the original figure geometry,
 * pattern phase and final cursor rules while making execution observable. */
static void figure_step(upd7220_t *g)
{
    uint32_t i = g->work_index;
    if (g->work_kind == WORK_WDAT) {
        write_vram_op(g, (g->cr >> 3) & 3, g->cr & 3, g->work_data, g->mask);
        next_word(g, g->figs.dir);
        if (++g->work_index > g->figs.dc) finish_work(g);
        return;
    }
    if (g->work_kind == WORK_GCHR) {
        unsigned zoom = (unsigned)g->gchr + 1;
        unsigned width = g->figs.d ? g->figs.d : 8;
        uint8_t row = g->ra[8 + ((g->work_row / zoom) & 7)];
        g->pattern = (uint16_t)(row | ((uint16_t)row << 8));
        draw_dot(g, i / zoom);
        if (++g->work_index < width * zoom) {
            next_pixel(g, g->work_dir);
        } else {
            next_pixel(g, (g->work_dir + 2) & 7);
            g->work_index = 0;
            if (++g->work_row >= ((uint32_t)g->figs.dc + 1) * zoom)
                finish_work(g);
            else {
                /* NEC manual 4-23: the DDA needs six clocks between rows,
                 * during which DRAW goes low. The FIFO cannot decode yet. */
                g->row_waiting = 1;
                g->sr &= (uint8_t)~UPD7220_SR_DRAW;
                g->busy_clocks = 6;
            }
        }
        return;
    }

    switch (g->figs.type) {
    case 0:
        draw_dot(g, i);
        next_pixel(g, g->figs.dir);
        break;
    case 1: {
        int32_t d = g->work_error;
        unsigned base = g->work_dir;
        draw_dot(g, i);
        g->figs.dir = (uint8_t)((base & 1)
            ? ((d < 0) ? ((base + 1) & 7) : base)
            : ((d < 0) ? base : ((base + 1) & 7)));
        g->work_error += sext14(d < 0 ? g->figs.d1 : g->figs.d2);
        next_pixel(g, g->figs.dir);
        break;
    }
    case 4: {
        unsigned oct = g->work_dir;
        if (i >= g->figs.dm) draw_dot(g, i);
        g->figs.dir = (uint8_t)((g->work_error < 0)
            ? ((oct & 1) ? ((oct + 1) & 7) : oct)
            : ((oct & 1) ? oct : ((oct + 1) & 7)));
        g->work_error += (g->work_error < 0) ? (int32_t)(2 * (i + 1))
            : 2 * ((int32_t)i - --g->work_arc_d + 1);
        next_pixel(g, g->figs.dir);
        break;
    }
    case 8: {
        unsigned side = g->work_side;
        uint16_t dist = (side & 1) ? g->figs.d2 : g->figs.d;
        draw_dot(g, i);
        if (!(side == 3 && i + 1 == dist)) next_pixel(g, g->figs.dir);
        if (++g->work_index == dist) {
            g->figs.dir = (g->figs.dir + 2) & 7;
            g->work_index = 0;
            /* A zero-length side still turns the drawing direction. */
            do {
                if (++g->work_side == 4) { finish_work(g); return; }
                dist = (g->work_side & 1) ? g->figs.d2 : g->figs.d;
                if (!dist) g->figs.dir = (g->figs.dir + 2) & 7;
            } while (!dist);
        }
        return;
    }
    default:
        finish_work(g);
        return;
    }
    if (++g->work_index > g->figs.dc) finish_work(g);
}

static void start_figure(upd7220_t *g, unsigned kind)
{
    g->work_index = g->work_row = g->work_side = 0;
    g->work_dir = g->figs.dir & 7;
    g->work_error = g->figs.type == 4 ? -(int32_t)g->figs.d : sext14(g->figs.d);
    g->work_arc_d = (int32_t)g->figs.d + 1;
    if (kind == WORK_FIGD && g->figs.type == 8) {
        while (g->work_side < 4 && !(g->work_side & 1 ? g->figs.d2 : g->figs.d)) {
            g->work_side++;
            g->figs.dir = (g->figs.dir + 2) & 7;
        }
        if (g->work_side == 4) { finish_work(g); return; }
    }
    g->work_kind = (uint8_t)kind;
    g->sr |= UPD7220_SR_DRAW;
}

static void queue_word_for_read(upd7220_t *g, uint16_t value, unsigned type)
{
    if ((type & 3) == 0) {
        fifo_push(g, (uint8_t)value, FIFO_PARAMETER);
        fifo_push(g, (uint8_t)(value >> 8), FIFO_PARAMETER);
    } else {
        fifo_push(g, (uint8_t)value, FIFO_PARAMETER);
    }
}

static void rdat_fill(upd7220_t *g)
{
    unsigned type = g->transfer_type;
    if (g->transfer_words_left) {
        queue_word_for_read(g, read_vram_type(g, type), type);
        g->transfer_words_left--;
        if (g->transfer_words_left) next_word(g, g->figs.dir);
    }
    fifo_status(g);
}

static void start_read_transfer(upd7220_t *g)
{
    g->transfer_type = (uint8_t)((g->cr >> 3) & 3);
    g->bitmap_mod = (uint8_t)(g->cr & 3);
    g->transfer_words_left = (uint32_t)g->figs.dc + 1;
    fifo_direction(g, FIFO_READ);
}

static void begin_command(upd7220_t *g, uint8_t cr)
{
    g->cr = cr;
    g->param_ptr = 0;
    g->command_active = 1;
    memset(g->pr, 0, sizeof(g->pr));
    if ((cr & 0xe4) == CMD_WDAT)
        g->bitmap_mod = cr & 3;

    if (cr == CMD_RESET || cr == CMD_RESET2 || cr == CMD_RESET3) {
        g->de = 0;
        g->ead = 0;
        /* NEC RESET clears the execution mask; it does not clear PRAM. */
        g->mask = 0x0000;
        g->sr &= (uint8_t)~(UPD7220_SR_DRAW | UPD7220_SR_DMA);
        g->dma_bytes_left = 0;
        g->dma_phase = 0;
        set_drq(g, 0);
        reset_figs(g);
        return;
    }
    if ((cr & 0xfe) == CMD_SYNC) {
        g->de = cr & 1;
        return;
    }
    if ((cr & 0xfe) == CMD_VSYNC) {
        g->master = cr & 1;
        if (g->timing_changed) g->timing_changed(g->opaque);
        g->command_active = 0;
        return;
    }
    if (cr == CMD_START) {
        g->de = 1;
        g->command_active = 0;
        return;
    }
    if ((cr & 0xfe) == CMD_BLANK || cr == CMD_BLANK2) {
        g->de = (cr == CMD_BLANK2) ? 0 : (cr & 1);
        g->command_active = 0;
        return;
    }
    if ((cr & 0xf0) == CMD_PRAM) {
        g->ra_addr = cr & 0x0f;
        return;
    }
    if (cr == CMD_FIGS) {
        /* Omitted fields belong to this FIGS, rather than the previous one.
         * Do not reload the pattern latch merely for specifying geometry. */
        figs_defaults(g);
        return;
    }
    if (cr == CMD_FIGD) {
        switch (g->figs.type) {
        case 0: case 1: case 4: case 8: start_figure(g, WORK_FIGD); break;
        default: finish_work(g); break;
        }
        g->command_active = 0;
        return;
    }
    if (cr == CMD_GCHRD) {
        if ((g->figs.type & 0x0f) == 2) start_figure(g, WORK_GCHR);
        else finish_work(g);
        g->command_active = 0;
        return;
    }
    if ((cr & 0xe4) == CMD_RDAT) {
        start_read_transfer(g);
        g->command_active = 0;
        return;
    }
    if (cr == CMD_CURD) {
        fifo_direction(g, FIFO_READ);
        fifo_push(g, (uint8_t)g->ead, FIFO_PARAMETER);
        fifo_push(g, (uint8_t)(g->ead >> 8), FIFO_PARAMETER);
        fifo_push(g, (uint8_t)(g->ead >> 16), FIFO_PARAMETER);
        fifo_push(g, (uint8_t)g->mask, FIFO_PARAMETER);
        fifo_push(g, (uint8_t)(g->mask >> 8), FIFO_PARAMETER);
        g->command_active = 0;
        return;
    }
    if (cr == CMD_LPRD) {
        fifo_direction(g, FIFO_READ);
        fifo_push(g, (uint8_t)g->lad, FIFO_PARAMETER);
        fifo_push(g, (uint8_t)(g->lad >> 8), FIFO_PARAMETER);
        fifo_push(g, (uint8_t)(g->lad >> 16), FIFO_PARAMETER);
        g->sr &= (uint8_t)~UPD7220_SR_LPEN;
        g->command_active = 0;
        return;
    }
    if ((cr & 0xe4) == CMD_DMAR || (cr & 0xe4) == CMD_DMAW) {
        g->dma_type = (uint8_t)((cr >> 3) & 3);
        g->dma_mod = (uint8_t)(cr & 3);
        g->dma_bytes_left = ((uint32_t)g->figs.dc + 1) * (((g->dma_type & 3) == 0) ? 2u : 1u);
        g->dma_phase = 0;
        g->sr |= UPD7220_SR_DMA;
        dma_request_update(g);
        g->command_active = 0;
        return;
    }
    if (cr == CMD_5A) {
        /* Later PC-98 software uses this undocumented opcode.  Preserve the
         * three parameter bytes as a benign command rather than treating it
         * as FIFO corruption. */
        return;
    }
}

static void param_complete(upd7220_t *g)
{
    g->command_active = 0;
}

static void accept_param(upd7220_t *g, uint8_t data)
{
    uint8_t cr = g->cr;
    if (g->param_ptr < sizeof(g->pr)) g->pr[g->param_ptr] = data;
    /* PRAM and reserved commands accept arbitrary streams. Saturate the
     * parser index so an overlong stream cannot wrap back into old fields. */
    if (g->param_ptr < sizeof(g->pr)) g->param_ptr++;

    if (cr == CMD_RESET || cr == CMD_RESET2 || cr == CMD_RESET3 || (cr & 0xfe) == CMD_SYNC) {
        if (g->param_ptr == 8) { decode_timing(g); param_complete(g); }
        return;
    }
    if (cr == CMD_CCHAR) {
        if (g->param_ptr >= 1) {
            g->lr = (uint8_t)((g->pr[0] & 0x1f) + 1);
            g->dc = (uint8_t)!!(g->pr[0] & 0x80);
        }
        if (g->param_ptr >= 2) {
            g->ctop = (uint8_t)(g->pr[1] & 0x1f);
            g->sc = (uint8_t)!!(g->pr[1] & 0x20);
            g->br = (uint8_t)(g->pr[1] >> 6);
        }
        if (g->param_ptr >= 3) {
            g->br = (uint8_t)(((g->pr[2] & 7) << 2) | (g->pr[1] >> 6));
            g->cbot = (uint8_t)(g->pr[2] >> 3);
            param_complete(g);
        }
        return;
    }
    if (cr == CMD_ZOOM) {
        g->gchr = data & 0x0f;
        g->disp = data >> 4;
        param_complete(g);
        return;
    }
    if (cr == CMD_CURS) {
        /* P1/P2 are sufficient for a 16-bit EAD; P3 optionally supplies A16/A17
         * and the dot address.  Keep accepting P3 until another command. */
        if (g->param_ptr >= 2) {
            uint8_t upper = (g->param_ptr >= 3) ? (g->pr[2] & 3) : 0;
            g->ead = ((uint32_t)upper << 16) | ((uint32_t)g->pr[1] << 8) | g->pr[0];
        }
        if (g->param_ptr == 3) {
            g->mask = (uint16_t)(1u << ((g->pr[2] >> 4) & 15));
            g->wg = (uint8_t)((g->pr[2] >> 3) & 1u);
            param_complete(g);
        }
        return;
    }
    if ((cr & 0xf0) == CMD_PRAM) {
        if (g->ra_addr < 16) g->ra[g->ra_addr++] = data;
        /* PRAM accepts a stream. A new command terminates it. */
        g->pattern = (uint16_t)(g->ra[8] | ((uint16_t)g->ra[9] << 8));
        return;
    }
    if (cr == CMD_PITCH) {
        g->pitch = (uint16_t)((g->pitch & 0x100) | data);
        if (g->timing_changed) g->timing_changed(g->opaque);
        param_complete(g);
        return;
    }
    if (cr == CMD_MASK) {
        if (g->param_ptr == 2) {
            g->mask = (uint16_t)(g->pr[0] | ((uint16_t)g->pr[1] << 8));
            param_complete(g);
        }
        return;
    }
    if (cr == CMD_FIGS) {
        if (g->param_ptr == 1) {
            g->figs.dir = g->pr[0] & 7;
            g->figs.type = (g->pr[0] & 0xf8) >> 3;
        } else if (g->param_ptr == 2) {
            /* Some hardware/software sequences execute after only DC low. */
            g->figs.dc = (uint16_t)((g->figs.dc & 0x3f00) | g->pr[1]);
        } else if (g->param_ptr == 3) {
            g->figs.dc = (uint16_t)(g->pr[1] | ((uint16_t)(g->pr[2] & 0x3f) << 8));
            g->figs.gd = (uint8_t)(!!(g->pr[2] & 0x40) && ((g->mode & 0x22) == 0x00));
        } else if (g->param_ptr == 5) {
            g->figs.d = (uint16_t)(g->pr[3] | ((uint16_t)(g->pr[4] & 0x3f) << 8));
        } else if (g->param_ptr == 7) {
            g->figs.d2 = (uint16_t)(g->pr[5] | ((uint16_t)(g->pr[6] & 0x3f) << 8));
        } else if (g->param_ptr == 9) {
            g->figs.d1 = (uint16_t)(g->pr[7] | ((uint16_t)(g->pr[8] & 0x3f) << 8));
        } else if (g->param_ptr == 11) {
            g->figs.dm = (uint16_t)(g->pr[9] | ((uint16_t)(g->pr[10] & 0x3f) << 8));
        }
        return;
    }
    if ((cr & 0xe4) == CMD_WDAT) {
        unsigned type = (cr >> 3) & 3;
        unsigned need = (cr & 0x10) ? 1u : 2u;
        if (type == 1) return; /* reserved transfer type */
        if (g->param_ptr == need) {
            uint16_t w;
            if (type == 0)
                w = (uint16_t)(g->pr[0] | ((uint16_t)g->pr[1] << 8));
            else if (type == 3)
                w = (uint16_t)g->pr[0] << 8;
            else
                w = g->pr[0];

            /* A uPD7220 in graphics mode expands data bit 0 over the
             * selected transfer lane.  The uPD7220A CURS write-gate bit
             * disables that legacy behavior and permits raw bitmap data. */
            if (((g->mode & 0x22) == 0x02) && !g->wg) {
                const uint16_t lane = transfer_mask(type, 0xffff);
                w = (g->pr[0] & 1u) ? lane : 0;
            }

            g->pattern = (uint16_t)((g->pattern & 0xff00) | g->pr[0]);
            if (need == 2) g->pattern = (uint16_t)((g->pattern & 0x00ff) | ((uint16_t)g->pr[1] << 8));

            g->work_data = w;
            start_figure(g, WORK_WDAT);
            /* Hardware keeps WDAT selected; another datum may follow. */
            g->param_ptr = 0;
        }
        return;
    }
    if (cr == CMD_5A) {
        if (g->param_ptr == 3) param_complete(g);
        return;
    }
}

void upd7220_init(upd7220_t *g, void *opaque,
                  upd7220_vram_read_cb rd, upd7220_vram_write_cb wr)
{
    memset(g, 0, sizeof(*g));
    g->opaque = opaque;
    g->vram_read = rd ? rd : dummy_read;
    g->vram_write = wr ? wr : dummy_write;
    upd7220_reset(g);
}

void upd7220_reset(upd7220_t *g)
{
    void *opaque = g->opaque;
    upd7220_vram_read_cb rd = g->vram_read;
    upd7220_vram_write_cb wr = g->vram_write;
    upd7220_timing_changed_cb tc = g->timing_changed;
    upd7220_signal_cb drq = g->drq_changed;
    uint8_t bus_timing_enabled = g->bus_timing_enabled;

    if (drq && (g->sr & UPD7220_SR_DMA)) drq(opaque, 0);

    memset(g, 0, sizeof(*g));
    g->opaque = opaque;
    g->vram_read = rd ? rd : dummy_read;
    g->vram_write = wr ? wr : dummy_write;
    g->timing_changed = tc;
    g->drq_changed = drq;
    g->bus_timing_enabled = bus_timing_enabled;
    g->fifo_dir = FIFO_WRITE;
    g->sr = UPD7220_SR_EMPTY;
    g->mask = 0xffff;
    g->lr = 1;
    reset_figs(g);
    fifo_clear(g);
}

/* Start the next byte or memory cycle only when its predecessor finished.
 * RDAT stalls on a full output FIFO; writes can turn it around immediately. */
static void engine_schedule(upd7220_t *g)
{
    if (g->busy_clocks) return;
    if (g->work_kind != WORK_NONE) {
        if (g->host_bus_requested) return;
        g->busy_clocks = memory_wait(g);
        g->bus_waiting = (uint8_t)!!g->busy_clocks;
        if (!g->busy_clocks) g->busy_clocks = memory_cost(g);
    } else if (g->fifo_dir == FIFO_READ) {
        unsigned bytes = g->transfer_type == 0 ? 2 : 1;
        if (g->transfer_words_left && g->fifo_count <= 16 - bytes) {
            if (g->host_bus_requested) return;
            g->work_kind = WORK_RDAT;
            g->busy_clocks = memory_wait(g);
            g->bus_waiting = (uint8_t)!!g->busy_clocks;
            if (!g->busy_clocks) g->busy_clocks = memory_cost(g);
        }
    } else if (!(g->sr & UPD7220_SR_DMA) && g->fifo_count) {
        int flag;
        fifo_pop(g, &g->pending_byte, &flag);
        g->pending_valid = 1;
        g->pending_command = (uint8_t)(flag == FIFO_COMMAND);
        if (g->pending_command && (g->pending_byte == CMD_FIGD ||
                                    g->pending_byte == CMD_GCHRD))
            g->sr |= UPD7220_SR_DRAW;
        g->busy_clocks = g->pending_command ? command_cost(g->pending_byte)
                                            : parameter_cost(g);
    }
}

void upd7220_write(upd7220_t *g, unsigned offset, uint8_t data)
{
    bool command = !!(offset & 1);
    if (command && (data == CMD_RESET || data == CMD_RESET2 || data == CMD_RESET3)) {
        /* RESET has a dedicated decoder ahead of the FIFO (manual 5-2), so
         * it can recover a full FIFO or an in-progress drawing/read/DMA. */
        fifo_clear(g);
        g->fifo_dir = FIFO_WRITE;
        g->pending_valid = 0;
        g->bus_waiting = 0;
        g->row_waiting = 0;
        g->work_kind = WORK_NONE;
        g->transfer_words_left = g->dma_bytes_left = 0;
        g->sr &= (uint8_t)~(UPD7220_SR_DRAW | UPD7220_SR_DMA);
        set_drq(g, 0);
        begin_command(g, data);
        g->busy_clocks = command_cost(data);
        return;
    }
    if (g->fifo_dir == FIFO_READ) {
        /* An opcode aborts RDAT even when the read FIFO is full. A stray
         * parameter is not an opcode and must not destroy its output. */
        if (!command) return;
        g->transfer_words_left = 0;
        if (g->work_kind == WORK_RDAT) {
            g->work_kind = WORK_NONE;
            g->busy_clocks = 0;
            g->bus_waiting = 0;
        }
        fifo_direction(g, FIFO_WRITE);
    }
    fifo_push(g, data, command ? FIFO_COMMAND : FIFO_PARAMETER);
    engine_schedule(g);
}

uint8_t upd7220_read(upd7220_t *g, unsigned offset)
{
    if (!(offset & 1)) {
        fifo_status(g);
        return g->sr;
    }
    /* Reading before DRDY must not turn around and erase queued commands. */
    if (g->fifo_dir != FIFO_READ) return 0xff;
    uint8_t value;
    int flag;
    fifo_pop(g, &value, &flag);
    engine_schedule(g);
    return value;
}

void upd7220_dack_write(upd7220_t *g, uint8_t data)
{
    if (!(g->sr & UPD7220_SR_DMA) || !g->dma_bytes_left || !g->dma_drq) return;

    if ((g->dma_type & 3) == 0) {
        if (!g->dma_phase) {
            g->dma_data = data;
            g->dma_phase = 1;
        } else {
            g->dma_data |= (uint16_t)data << 8;
            write_vram_op(g, 0, g->dma_mod, g->dma_data, g->mask);
            g->dma_phase = 0;
            if (g->dma_bytes_left > 1) next_word(g, g->figs.dir);
        }
    } else {
        uint16_t w = ((g->dma_type & 3) == 3) ? (uint16_t)data << 8 : data;
        write_vram_op(g, g->dma_type, g->dma_mod, w, g->mask);
        if (g->dma_bytes_left > 1) next_word(g, g->figs.dir);
    }

    if (--g->dma_bytes_left == 0) {
        g->sr &= (uint8_t)~UPD7220_SR_DMA;
        set_drq(g, 0);
        engine_schedule(g);
    }
}

uint8_t upd7220_dack_read(upd7220_t *g)
{
    if (!(g->sr & UPD7220_SR_DMA) || !g->dma_bytes_left || !g->dma_drq) return 0xff;

    uint16_t v = vram_read(g, g->ead);
    uint8_t out;
    if ((g->dma_type & 3) == 0) {
        out = g->dma_phase ? (uint8_t)(v >> 8) : (uint8_t)v;
        g->dma_phase ^= 1;
        if (!g->dma_phase && g->dma_bytes_left > 1) next_word(g, g->figs.dir);
    } else {
        out = ((g->dma_type & 3) == 3) ? (uint8_t)(v >> 8) : (uint8_t)v;
        if (g->dma_bytes_left > 1) next_word(g, g->figs.dir);
    }

    if (--g->dma_bytes_left == 0) {
        g->sr &= (uint8_t)~UPD7220_SR_DMA;
        set_drq(g, 0);
        engine_schedule(g);
    }
    return out;
}

void upd7220_lpen(upd7220_t *g, int level, uint32_t current_word_addr)
{
    uint8_t now = (uint8_t)!!level;
    if (now && !g->lpen_level) {
        g->lad = current_word_addr & 0x3ffffu;
        g->sr |= UPD7220_SR_LPEN;
    }
    g->lpen_level = now;
}

void upd7220_set_retrace(upd7220_t *g, int hblank, int vsync)
{
    if (hblank) g->sr |= UPD7220_SR_HBLANK;
    else        g->sr &= (uint8_t)~UPD7220_SR_HBLANK;
    if (vsync)  g->sr |= UPD7220_SR_VSYNC;
    else        g->sr &= (uint8_t)~UPD7220_SR_VSYNC;
}

void upd7220_advance(upd7220_t *g, uint32_t clocks)
{
    engine_schedule(g);
    while (clocks) {
        uint32_t step = clocks;
        uint32_t bus_event = dma_bus_event(g);
        if (g->busy_clocks && g->busy_clocks < step) step = g->busy_clocks;
        if (bus_event && bus_event < step) step = bus_event;
        clocks -= step;
        bus_advance(g, step);
        dma_request_update(g);
        if (!g->busy_clocks) continue;
        g->busy_clocks -= step;
        if (g->busy_clocks) continue;
        if (g->row_waiting) {
            g->row_waiting = 0;
            g->sr |= UPD7220_SR_DRAW;
        } else if (g->bus_waiting) {
            g->bus_waiting = 0;
        } else if (g->pending_valid) {
            uint8_t byte = g->pending_byte;
            bool command = !!g->pending_command;
            g->pending_valid = 0;
            if (command) begin_command(g, byte);
            else if (g->command_active) accept_param(g, byte);
        } else if (g->work_kind == WORK_RDAT) {
            rdat_fill(g);
            g->work_kind = WORK_NONE;
        } else if (g->work_kind != WORK_NONE) {
            figure_step(g);
        }
        engine_schedule(g);
    }
}

uint32_t upd7220_next_event(const upd7220_t *g)
{
    uint32_t bus_event = dma_bus_event(g);
    return !bus_event ? g->busy_clocks : !g->busy_clocks ? bus_event :
           bus_event < g->busy_clocks ? bus_event : g->busy_clocks;
}

void upd7220_get_graphics_partition(const upd7220_t *g, unsigned index,
                                    upd7220_partition_t *out)
{
    index &= 3;
    const unsigned p = index * 4;
    out->sad = (uint32_t)g->ra[p] | ((uint32_t)g->ra[p + 1] << 8) |
               ((uint32_t)(g->ra[p + 2] & 3) << 16);
    out->len = (uint16_t)(((g->ra[p + 3] & 0x3f) << 4) | (g->ra[p + 2] >> 4));
    out->im = (uint8_t)((g->ra[p + 3] >> 6) & 1);
    out->wd = (uint8_t)((g->ra[p + 3] >> 7) & 1);
}

void upd7220_get_text_partition(const upd7220_t *g, unsigned index,
                                upd7220_partition_t *out)
{
    index &= 3;
    const unsigned p = index * 4;
    out->sad = (uint32_t)g->ra[p] | ((uint32_t)(g->ra[p + 1] & 0x1f) << 8);
    out->len = (uint16_t)(((g->ra[p + 3] & 0x3f) << 4) | (g->ra[p + 2] >> 4));
    out->im = (uint8_t)((g->ra[p + 3] >> 6) & 1);
    out->wd = (uint8_t)((g->ra[p + 3] >> 7) & 1);
}
