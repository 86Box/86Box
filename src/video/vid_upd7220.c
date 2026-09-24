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
        fifo_status(g);
        return false;
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

static void set_busy(upd7220_t *g, uint32_t clocks)
{
    if (clocks > g->busy_clocks)
        g->busy_clocks = clocks;
    if (g->busy_clocks)
        g->sr |= UPD7220_SR_DRAW;
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
    if ((cr & 0xe4) == CMD_WDAT) return 12;
    if (cr == CMD_MASK) return 10;
    if (cr == CMD_FIGS) return 10;
    if (cr == CMD_FIGD) return 18;
    if (cr == CMD_GCHRD) return 16;
    if ((cr & 0xe4) == CMD_RDAT) return 14;
    if (cr == CMD_CURD) return 14;
    if (cr == CMD_LPRD) return 12;
    if ((cr & 0xe4) == CMD_DMAR) return 14;
    if ((cr & 0xe4) == CMD_DMAW) return 12;
    return 6;
}

static void reset_figs(upd7220_t *g)
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

static void draw_pixel(upd7220_t *g)
{
    uint32_t n = (uint32_t)g->figs.dc + 1;
    for (uint32_t i = 0; i < n; i++) {
        draw_dot(g, i);
        next_pixel(g, g->figs.dir);
    }
}

/* Bresenham-form line parameters match the 7220 FIGS accumulator model. */
static void draw_line(upd7220_t *g)
{
    int32_t d = sext14(g->figs.d);
    int32_t d1 = sext14(g->figs.d1);
    int32_t d2 = sext14(g->figs.d2);
    unsigned base = g->figs.dir & 7;
    uint32_t n = (uint32_t)g->figs.dc + 1;

    for (uint32_t i = 0; i < n; i++) {
        draw_dot(g, i);
        if (base & 1)
            g->figs.dir = (uint8_t)((d < 0) ? ((base + 1) & 7) : base);
        else
            g->figs.dir = (uint8_t)((d < 0) ? base : ((base + 1) & 7));
        d += (d < 0) ? d1 : d2;
        /* FIGD advances EAD after every generated pixel, including the final
         * one. Software commonly chains vectors from that updated address. */
        next_pixel(g, g->figs.dir);
    }
}

static void draw_rect(upd7220_t *g)
{
    unsigned dir = g->figs.dir & 7;
    for (unsigned side = 0; side < 4; side++) {
        uint16_t dist = (side & 1) ? g->figs.d2 : g->figs.d;
        for (uint16_t i = 0; i < dist; i++) {
            draw_dot(g, i);
            if (!(side == 3 && i + 1 == dist)) next_pixel(g, dir);
        }
        dir = (dir + 2) & 7;
        g->figs.dir = (uint8_t)dir;
    }
}

static void draw_arc(upd7220_t *g)
{
    int32_t radius = g->figs.d;
    int32_t err = -radius;
    int32_t d = radius + 1;
    unsigned oct = g->figs.dir & 7;
    uint32_t n = (uint32_t)g->figs.dc + 1;

    for (uint32_t i = 0; i < n; i++) {
        if (i >= g->figs.dm) draw_dot(g, i);
        if (err < 0) g->figs.dir = (uint8_t)((oct & 1) ? ((oct + 1) & 7) : oct);
        else         g->figs.dir = (uint8_t)((oct & 1) ? oct : ((oct + 1) & 7));
        err += (err < 0) ? ((int32_t)(i + 1) << 1)
                         : (((int32_t)i - --d + 1) << 1);
        next_pixel(g, g->figs.dir);
    }
}

static void draw_gchr(upd7220_t *g)
{
    unsigned lines = (unsigned)g->figs.dc + 1;
    unsigned width = g->figs.d ? g->figs.d : 8;
    unsigned zoom = (unsigned)g->gchr + 1;
    unsigned original_dir = g->figs.dir & 7;

    /* PRAM bytes 8..15 are the documented graphics-character pattern. */
    for (unsigned y = 0; y < lines; y++) {
        uint8_t row = g->ra[8 + (y & 7)];
        g->pattern = (uint16_t)(row | ((uint16_t)row << 8));
        for (unsigned zy = 0; zy < zoom; zy++) {
            for (unsigned x = 0; x < width; x++) {
                for (unsigned zx = 0; zx < zoom; zx++) {
                    draw_dot(g, x);
                    if (!(x + 1 == width && zx + 1 == zoom)) next_pixel(g, original_dir);
                }
            }
            /* Return to the next scan direction using the same 8-way geometry.
             * This deliberately avoids assuming a linear framebuffer. */
            g->figs.dir = (uint8_t)((original_dir + 2) & 7);
            next_pixel(g, g->figs.dir);
            g->figs.dir = (uint8_t)original_dir;
        }
    }
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
    unsigned type = (g->cr >> 3) & 3;
    while (g->transfer_words_left && g->fifo_count <= 14) {
        uint16_t value = read_vram_type(g, type);
        queue_word_for_read(g, value, type);
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
    rdat_fill(g);
}

static void begin_command(upd7220_t *g, uint8_t cr)
{
    g->cr = cr;
    g->param_ptr = 0;
    g->command_active = 1;
    memset(g->pr, 0, sizeof(g->pr));
    if ((cr & 0xe4) == CMD_WDAT)
        g->bitmap_mod = cr & 3;
    set_busy(g, command_cost(cr));

    if (cr == CMD_RESET || cr == CMD_RESET2 || cr == CMD_RESET3) {
        g->de = 0;
        g->ead = 0;
        /* NEC RESET clears the execution mask; it does not clear PRAM. */
        g->mask = 0x0000;
        g->sr &= (uint8_t)~(UPD7220_SR_DRAW | UPD7220_SR_DMA);
        g->busy_clocks = command_cost(cr);
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
    if (cr == CMD_FIGD) {
        switch (g->figs.type) {
        case 0: draw_pixel(g); break;
        case 1: draw_line(g); break;
        case 4: draw_arc(g); break;
        case 8: draw_rect(g); break;
        default: break;
        }
        reset_figs(g);
        g->command_active = 0;
        return;
    }
    if (cr == CMD_GCHRD) {
        if ((g->figs.type & 0x0f) == 2) draw_gchr(g);
        reset_figs(g);
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
        if (g->drq_changed) g->drq_changed(g->opaque, 1);
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
    g->param_ptr++;
    set_busy(g, 2);

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
        unsigned mod = cr & 3;
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

            /* WDAT repeats the supplied datum DC+1 times. */
            for (uint32_t i = 0; i <= g->figs.dc; i++) {
                write_vram_op(g, type, mod, w, g->mask);
                next_word(g, g->figs.dir);
            }
            reset_figs(g);
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

    memset(g, 0, sizeof(*g));
    g->opaque = opaque;
    g->vram_read = rd ? rd : dummy_read;
    g->vram_write = wr ? wr : dummy_write;
    g->timing_changed = tc;
    g->drq_changed = drq;
    g->fifo_dir = FIFO_WRITE;
    g->sr = UPD7220_SR_EMPTY;
    g->mask = 0xffff;
    g->lr = 1;
    reset_figs(g);
    fifo_clear(g);
}

void upd7220_write(upd7220_t *g, unsigned offset, uint8_t data)
{
    if (offset & 1) {
        /* A new command terminates PRAM/WDAT streaming and switches FIFO to input. */
        fifo_direction(g, FIFO_WRITE);
        fifo_push(g, data, FIFO_COMMAND);
        begin_command(g, data);
    } else {
        fifo_direction(g, FIFO_WRITE);
        fifo_push(g, data, FIFO_PARAMETER);
        if (g->command_active) accept_param(g, data);
    }

    /* The software-visible write FIFO is consumed by the command processor;
     * this model executes command bytes synchronously and accounts for the
     * documented processing latency through busy_clocks. */
    if (g->fifo_dir == FIFO_WRITE) fifo_clear(g);
}

uint8_t upd7220_read(upd7220_t *g, unsigned offset)
{
    if (!(offset & 1)) {
        fifo_status(g);
        return g->sr;
    }

    fifo_direction(g, FIFO_READ);
    uint8_t value;
    int flag;
    fifo_pop(g, &value, &flag);
    (void)flag;
    if (g->transfer_words_left) rdat_fill(g);
    return value;
}

void upd7220_dack_write(upd7220_t *g, uint8_t data)
{
    if (!(g->sr & UPD7220_SR_DMA) || !g->dma_bytes_left) return;

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
        if (g->drq_changed) g->drq_changed(g->opaque, 0);
    }
}

uint8_t upd7220_dack_read(upd7220_t *g)
{
    if (!(g->sr & UPD7220_SR_DMA) || !g->dma_bytes_left) return 0xff;

    uint16_t v = vram_read(g, g->ead);
    uint8_t out;
    if ((g->dma_type & 3) == 0) {
        out = g->dma_phase ? (uint8_t)(v >> 8) : (uint8_t)v;
        g->dma_phase ^= 1;
        if (!g->dma_phase && g->dma_bytes_left > 1) next_pixel(g, g->figs.dir);
    } else {
        out = ((g->dma_type & 3) == 3) ? (uint8_t)(v >> 8) : (uint8_t)v;
        if (g->dma_bytes_left > 1) next_word(g, g->figs.dir);
    }

    if (--g->dma_bytes_left == 0) {
        g->sr &= (uint8_t)~UPD7220_SR_DMA;
        if (g->drq_changed) g->drq_changed(g->opaque, 0);
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
    if (clocks >= g->busy_clocks) g->busy_clocks = 0;
    else g->busy_clocks -= clocks;
    if (!g->busy_clocks) g->sr &= (uint8_t)~UPD7220_SR_DRAW;
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
