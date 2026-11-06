/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage XL/XC MPEG-2 IDCT engine: the coefficient feed, the
 *          transform and the residual the draw engine adds to the motion
 *          compensated prediction.
 *
 *          The host parses the bitstream and sends dequantized DCT
 *          coefficients as run-level pairs. The chip expands them into 8x8
 *          blocks, transforms them and holds the results; a draw with
 *          ALPHA_TST_CNTL's IDCT_EN set then takes the block samples in
 *          place of the destination pixels of its blend and adds them to the
 *          pixels it generates, with the blend forced to Src + Dst (XL/XC
 *          RRG: ALPHA_TST_CNTL, p. 5-19 / PDF 163).
 *
 *          What the register reference gives:
 *
 *          - BM_ADDR (MM 0_92) bit 31 sends a bus master packet to the IDCT
 *            rather than the command FIFO. IDCT_FLAGS (30:29) 00 asks for
 *            the end of the block, an all-zero block when nothing came
 *            before it, and carries no data; 01 is triplets of RUN, LEVEL,
 *            LEVEL with GUIREG_ADR the index of IDCT_RUNS and the count
 *            3T-1; 10 is IDCT register writes with auto-increment (XL/XC
 *            RRG: BM_ADDR, p. 6-13 / PDF 225).
 *          - GUI_CNTL (MM 1_5E) bit 16, IDCT_PRSR_MODE, swaps the two
 *            IDCT_LEVEL writes of a triplet; bit 17,
 *            IDCT_BLOCK_GUI_INITIATOR, stops writes to the IDCT registers
 *            from starting the IDCT (XL/XC RRG: GUI_CNTL, p. 5-64 / PDF
 *            208).
 *          - IDCT_EN, ALPHA_TST_CNTL bit 15, above.
 *
 *          The reference has no page for the IDCT registers themselves.
 *          ATI's Windows 98 motion compensation HAL for the XL, atimcrae.dll
 *          (base 0xb0100000, in the 09/18/2000 ATIi9xae driver package),
 *          fills in what is used here:
 *
 *          - Its programmed I/O command player (RE: atimcrae.dll @b010c400)
 *            writes a triplet's RUN to block 1 +380h (MM 1_E0) and both
 *            LEVELs to +384h (MM 1_E1); for the end-of-block token it
 *            writes FFFFFFFFh to +380h; and it writes 40h to +3BCh (MM 1_EF)
 *            and waits for the engine to go idle. Its triplet header is
 *            A00001E0h, a count of 0 for one triplet where the reference
 *            gives 2, so a count is taken here as T-1 triplets for 0 and as
 *            3T-1 otherwise: count / 3 + 1 triplets either way.
 *          - Its native reconstruction state (RE: atimcrae.dll @b0103d20)
 *            sets ALPHA_TST_CNTL to 8000h (IDCT_EN), SCALE_3D_CNTL to
 *            98A40h (the scaler, ALPHA_FOG_EN blend, ONE + ONE) and
 *            DP_PIX_WIDTH to 80080888h (Y8 in and out): the residual is
 *            added by front-end scaler blits of the reference planes.
 *
 *          The register offsets of the IDCT ports, +380h, +384h and +3BCh,
 *          are those of IDCT_RUNS, IDCT_LEVELS and IDCT_CONTROL of the Rage
 *          128 (1F80h, 1F84h, 1FBCh, the same offsets within the 1K
 *          aperture), whose MPEG packet carries the same three dwords as
 *          one coefficient record (vid_ati_rage128_mpeg.c, mc_coefs, from
 *          atimcraa.dll @b0103020). The word formats below are that
 *          record's:
 *
 *            RUNS    four run bytes, the first coefficient's in 31:24;
 *                    FFh ends the block and its level is ignored, 3Fh is
 *                    an unused slot (ATI DVD Player 6.2 pads its last
 *                    triplet with it, then sends IDCT_FLAGS 00)
 *            LEVELS  two signed 16-bit levels, high half first; the
 *                    first LEVELS write of a triplet holds the first two
 *                    levels in mode 0; mode 1 reverses the two writes
 *
 *          An FFh run ends the block there and then: the HAL's command
 *          player ends a block by FFFFFFFFh in RUNS with no LEVELS after
 *          it, and the Rage 128's out-of-line coefficient lane has no other
 *          block delimiter. A block ended with nothing in it is the
 *          all-zero block, which is also what IDCT_FLAGS 00 asks for.
 *          IDCT_PRSR_MODE 1 takes the two LEVELS writes the other way
 *          round. Levels are clamped to -2048..2047, the MPEG-2 range, and
 *          placed by the zigzag scan (ISO/IEC 13818-2, 7.3). Nothing known
 *          selects the alternate scan on the XL, so it is not modelled.
 *
 *          Not documented anywhere at hand, and so chosen here:
 *
 *          - The transform is the direct separable IDCT in double
 *            precision rounded to the nearest integer, the reference form
 *            IEEE 1180 measures against, as for the Rage 128.
 *          - The draw engine takes the finished blocks in the order they
 *            were finished. A drawn rectangle is cut into 8x8 tiles, taken
 *            left to right and top to bottom: an 8x8 or 8x16 rectangle takes
 *            one block after another in its rows, a 16x16 luma macroblock
 *            takes blocks 0 and 1 side by side above 2 and 3, MPEG's own
 *            order. The tiles of one draw are consumed by it. A draw that
 *            finds too few blocks adds zero for the missing ones; native
 *            scaler submissions wait until their blocks are available.
 *          - Native IDCT_CONTROL bit 10 selects intra reconstruction, with
 *            a prediction bias of 128. Other control bits are stored only.
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2026 Cacodemon345.
 */
#include <math.h>
#include "vid_ati_mach64.h"

/* The IDCT's registers, byte offsets in block 1 (MM 1_E0-1_EF). */
#define IDCT_FIRST   0x380
#define IDCT_RUNS    0x380
#define IDCT_LEVELS  0x384
#define IDCT_AUTH_STATUS 0x388
#define IDCT_AUTH_DATA   0x38c
#define IDCT_CONTROL 0x3bc
#define IDCT_LAST    0x3bc

/* GUI_CNTL */
#define GUI_CNTL_IDCT_PRSR_MODE      (1u << 16)
#define GUI_CNTL_IDCT_BLOCK_GUI_INIT (1u << 17)

/* Finished blocks held for the draw engine. A macroblock is six; this is
   room for many, so that a decoder sending a whole slice of coefficients
   before its draws loses nothing. */
#define IDCT_QUEUE 256

typedef struct idct_draw_t {
    struct idct_draw_t *next;
    void *state;
    unsigned blocks;
    void (*render)(mach64_t *, const void *);
} idct_draw_t;

struct mach64_idct_t {
    idct_draw_t *draw_head, *draw_tail;
    unsigned pending_draws;
    int draining;
    /* ATIMIAAE maps logical 1F88/1F8C to MM 1_E2/1_E3. The decoder,
       CINMST32 2.0.36.6152 @4B1B94A0, computes the responses and key. */
    unsigned auth_state, auth_reset;
    uint32_t auth_seq, challenge, response, host_response, key;
    /* The block being fed: coefficients by raster position, the scan
       position of the next one, and whether any has arrived. */
    int16_t coef[64];
    int     pos;
    int     dirty;

    /* A triplet in progress: its RUNS word and the first LEVELS word. */
    uint32_t runs;
    uint32_t level0;
    int      phase; /* LEVELS writes taken since the RUNS write: 0, 1 */

    uint32_t regs[(IDCT_LAST - IDCT_FIRST) / 4 + 1];

    /* Finished blocks, oldest at head. */
    int16_t  queue[IDCT_QUEUE][64];
    unsigned head;
    unsigned count;

    /* Residual taken by the draw in progress: its first block and tiles
       across. */
    int      draw_tiles_x;
    int      draw_tiles;
    unsigned dropped; /* blocks overwritten unused, for the log */
    unsigned starved; /* tiles drawn with no block, for the log */
};

enum { AUTH_RESET, AUTH_CHALLENGE, AUTH_ACK, AUTH_RESPONSE, AUTH_ARMED, AUTH_FAILED };

int
mach64_idct_authenticated(const mach64_t *mach64)
{
    return mach64->idct && mach64->idct->auth_state == AUTH_ARMED;
}

/* Native MCAM sets CONTROL=420 for intra macroblocks, then queues their
   Y/U/V rectangles before supplying the six coefficient blocks. */
int
mach64_idct_intra(const mach64_t *mach64)
{
    return mach64->idct && (mach64->idct->regs[(IDCT_CONTROL-IDCT_FIRST)/4] & 0x400);
}

static void
idct_drain(mach64_t *m)
{
    mach64_idct_t *idct = m->idct;
    if (idct->draining) return;
    idct->draining = 1;
    while (idct->draw_head && idct->count >= idct->draw_head->blocks) {
        idct_draw_t *draw = idct->draw_head;
        idct->draw_head = draw->next;
        if (!idct->draw_head) idct->draw_tail = NULL;
        idct->pending_draws--;
        draw->render(m, draw->state);
        free(draw->state);
        free(draw);
    }
    idct->draining = 0;
}

int
mach64_idct_defer(mach64_t *m, void *state, unsigned blocks, void (*render)(mach64_t *, const void *))
{
    mach64_idct_t *idct = m->idct;
    idct_draw_t *draw;
    if (!idct || !blocks || blocks > IDCT_QUEUE || idct->pending_draws >= IDCT_QUEUE ||
        !(draw = calloc(1, sizeof(*draw)))) {
        free(state);
        return 0;
    }
    draw->state = state; draw->blocks = blocks; draw->render = render;
    if (idct->draw_tail) idct->draw_tail->next = draw;
    else idct->draw_head = draw;
    idct->draw_tail = draw;
    idct->pending_draws++;
    idct_drain(m);
    return 1;
}

static void
idct_discard_draws(mach64_idct_t *idct)
{
    while (idct->draw_head) {
        idct_draw_t *next = idct->draw_head->next;
        free(idct->draw_head->state);
        free(idct->draw_head);
        idct->draw_head = next;
    }
}

static uint32_t
idct_key_step(uint32_t c)
{
    uint32_t a = (c & 0x40u) ^ (c >> 25);
    a = (a >> 1) ^ (c & 0x20u);
    a = (a >> 4) ^ (c & 2u);
    return (a >> 1) | (c << 1);
}

/* The two permutations and 31 rounds in installed CINMST32.DLL
   @4B1B9050, @4B1B90D0, @4B1B9130; also used by the Rage 128 decoder. */
static uint32_t
idct_auth_hash(uint32_t c)
{
    uint32_t a = ((c >> 5) & 0x07000000u) | (c & 0xe000u);
    uint32_t d, n, e;
    a = ((a >> 8) | (c & 0x1f00u)) >> 2;
    a = ((a | (c & 0xe0000u)) >> 1) | (c & 0x400u);
    d = ((c & 0xfu) << 8) | (c & 0xf0u);
    d = ((d << 13) | (c & 0xfe01c000u)) << 1;
    d |= c & 0x100u;
    c = (a >> 2) | (d << 2) | (c & 0x200u);
    n = ~c;
    a = ((c >> 4) & 0xff0000u) | (c & 0xf00000ffu);
    a = ((a >> 2) | (n & 0x003fc000u)) >> 2;
    a |= c & 0xff0u;
    e = ((c & 0xfu) << 9) | (n & 0xffffe000u);
    c = (a >> 4) | (e << 11) | (n & 0xf0000u);
    for (int i = 0; i < 31; i++)
        c = idct_key_step(c);
    return c;
}

/* Called synchronously, after draining the draw FIFO, so a status read
   observes preceding authentication writes. Other IDCT ports stay queued. */
int
mach64_idct_auth_read(mach64_t *mach64, uint32_t reg, uint32_t *val)
{
    mach64_idct_t *idct = mach64->idct;
    static const uint32_t status[] = { 8, 8, 1, 9, 0x1a, 0 };
    if (!idct || (reg != IDCT_AUTH_STATUS && reg != IDCT_AUTH_DATA))
        return 0;
    if (reg == IDCT_AUTH_STATUS) {
        *val = status[idct->auth_state];
        return 1;
    }
    if (idct->auth_state == AUTH_RESET) {
        idct->auth_seq += 0x9e3779b9u;
        idct->challenge = idct_auth_hash(idct->auth_seq);
        if (!idct->challenge)
            idct->challenge = 1;
        idct->auth_state = AUTH_CHALLENGE;
    } else if (idct->auth_state == AUTH_RESPONSE) {
        idct->key = idct_auth_hash(idct->host_response ^ (idct->response & 0x9009000cu));
        idct->auth_state = AUTH_ARMED;
        pclog("Rage XL IDCT: authentication armed, key=%08x\n", idct->key);
    }
    *val = idct->auth_state == AUTH_ARMED ? idct->host_response :
           idct->auth_state == AUTH_FAILED ? 0 : idct->challenge;
    return 1;
}

int
mach64_idct_auth_write(mach64_t *mach64, uint32_t reg, uint32_t val)
{
    mach64_idct_t *idct = mach64->idct;
    if (!idct || (reg != IDCT_AUTH_STATUS && reg != IDCT_AUTH_DATA))
        return 0;
    if (reg == IDCT_AUTH_STATUS) {
        if (val == 4)
            idct->auth_reset = 1;
        else if (val == 0 && idct->auth_reset) {
            idct->auth_state = AUTH_RESET;
            idct->auth_reset = 0;
            idct->key = idct->challenge = idct->response = idct->host_response = 0;
        }
    } else if (idct->auth_state == AUTH_CHALLENGE) {
        idct->response = val;
        idct->auth_state = val && val == idct_auth_hash(idct->challenge) ? AUTH_ACK : AUTH_FAILED;
    } else if (idct->auth_state == AUTH_ACK) {
        idct->host_response = idct_auth_hash(val);
        idct->auth_state = val ? AUTH_RESPONSE : AUTH_FAILED;
    }
    return 1;
}

/* Entry [n] is the raster index of zigzag scan position n (ISO/IEC 13818-2,
   7.3, figure 7-2). */
static const uint8_t idct_zigzag[64] = {
    0x00, 0x01, 0x08, 0x10, 0x09, 0x02, 0x03, 0x0a,
    0x11, 0x18, 0x20, 0x19, 0x12, 0x0b, 0x04, 0x05,
    0x0c, 0x13, 0x1a, 0x21, 0x28, 0x30, 0x29, 0x22,
    0x1b, 0x14, 0x0d, 0x06, 0x07, 0x0e, 0x15, 0x1c,
    0x23, 0x2a, 0x31, 0x38, 0x39, 0x32, 0x2b, 0x24,
    0x1d, 0x16, 0x0f, 0x17, 0x1e, 0x25, 0x2c, 0x33,
    0x3a, 0x3b, 0x34, 0x2d, 0x26, 0x1f, 0x27, 0x2e,
    0x35, 0x3c, 0x3d, 0x36, 0x2f, 0x37, 0x3e, 0x3f
};

static double idct_c[8][8];
static int    idct_ready;

static void
idct_init_table(void)
{
    double pi = 4.0 * atan(1.0);

    if (idct_ready)
        return;
    for (int u = 0; u < 8; u++)
        for (int x = 0; x < 8; x++)
            idct_c[u][x] = (u ? 0.5 : 0.5 / sqrt(2.0)) * cos((2.0 * x + 1.0) * u * pi / 16.0);
    idct_ready = 1;
}

/* The 8x8 inverse DCT of b in place, rounded to the nearest integer. Rows
   with no coefficient are skipped in both passes; a block with DC alone is
   DC / 8 throughout. */
static void
idct_transform(int16_t *b)
{
    double   tmp[64];
    uint32_t live = 0;
    int      ac   = 0;

    for (int i = 1; i < 64; i++)
        if (b[i]) {
            ac = 1;
            break;
        }
    if (!ac) {
        int16_t dc = (int16_t) lrint(b[0] * 0.125);

        for (int i = 0; i < 64; i++)
            b[i] = dc;
        return;
    }
    for (int y = 0; y < 8; y++) {
        int nz = 0;

        for (int u = 0; u < 8; u++)
            if (b[y * 8 + u]) {
                nz = 1;
                break;
            }
        if (!nz) {
            memset(&tmp[y * 8], 0, 8 * sizeof(double));
            continue;
        }
        live |= 1u << y;
        for (int x = 0; x < 8; x++) {
            double s = 0.0;

            for (int u = 0; u < 8; u++)
                s += idct_c[u][x] * b[y * 8 + u];
            tmp[y * 8 + x] = s;
        }
    }
    for (int x = 0; x < 8; x++)
        for (int y = 0; y < 8; y++) {
            double s = 0.0;

            for (int v = 0; v < 8; v++)
                if (live & (1u << v))
                    s += idct_c[v][y] * tmp[v * 8 + x];
            b[y * 8 + x] = (int16_t) lrint(s);
        }
}

/* Transform the block being fed and queue it. A full queue drops its
   oldest block, which no draw took. */
static void
idct_block_done(mach64_idct_t *idct)
{
    unsigned slot;

    idct_transform(idct->coef);
    if (idct->count == IDCT_QUEUE) {
        idct->head = (idct->head + 1) % IDCT_QUEUE;
        idct->count--;
        if (!idct->dropped++)
            mach64_log("Rage XL IDCT: block queue full, oldest block dropped\n");
    }
    slot = (idct->head + idct->count) % IDCT_QUEUE;
    memcpy(idct->queue[slot], idct->coef, sizeof(idct->coef));
    idct->count++;

    memset(idct->coef, 0, sizeof(idct->coef));
    idct->pos   = 0;
    idct->dirty = 0;
}

/* Four run bytes, first in 31:24. GUI_CNTL's parser mode determines
   which level dword comes first before this expansion. */
static void
idct_triplet(mach64_idct_t *idct, uint32_t runs, uint32_t a, uint32_t b)
{
    const int32_t lvl[4] = { (int16_t) (a >> 16), (int16_t) a, (int16_t) (b >> 16), (int16_t) b };

    for (int i = 0; i < 4; i++) {
        uint32_t run = (runs >> (24 - 8 * i)) & 0xff;
        int32_t  v   = lvl[i];

        if (run == 0xff) {
            /* The end of the block; a block with nothing in it yet is the
               all-zero block. The rest of the word is padding. */
            idct_block_done(idct);
            break;
        }
        if (run == 0x3f) {
            /* Padding: the DVD player's decoder fills the rest of a
               triplet's run word with 3Fh and ends the block with an
               IDCT_FLAGS 00 packet. */
            continue;
        }
        idct->pos += (int) run;
        if (idct->pos >= 64)
            continue;
        if (v < -2048)
            v = -2048;
        else if (v > 2047)
            v = 2047;
        idct->coef[idct_zigzag[idct->pos]] = (int16_t) v;
        idct->pos++;
        idct->dirty = 1;
    }
}

/* A write to an IDCT register, in the draw engine's order: reg is the byte
   offset in block 1. */
static void
idct_write(mach64_t *mach64, uint32_t reg, uint32_t val, int from_bm)
{
    mach64_idct_t *idct = mach64->idct;

    if (!idct || (reg < IDCT_FIRST) || (reg > IDCT_LAST))
        return;
    idct->regs[(reg - IDCT_FIRST) >> 2] = val;
    if (!from_bm && (mach64->rp.gui_cntl & GUI_CNTL_IDCT_BLOCK_GUI_INIT))
        return;

    switch (reg) {
        case IDCT_RUNS:
            /* CINMST32 @4B1B95B0 scrambles only the run word, preserves
               FFFFFFFF, and steps the key if any plaintext run is FF.
               Unauthenticated native MCAM traffic stays in the clear. */
            if (mach64_idct_authenticated(mach64) && mach64_idct_intra(mach64)) {
                if (val != 0xffffffffu)
                    val ^= idct->key & 0x7f7f7f7fu;
                for (int i = 0; i < 4; i++)
                    if (((val >> (i * 8)) & 0xff) == 0xff) {
                        idct->key = idct_key_step(idct->key);
                        break;
                    }
            }
            idct->runs  = val;
            idct->phase = 0;
            /* A RUNS word of FFh runs alone ends the block: the driver's
               end-of-block token has no LEVELS after it. */
            if (val == 0xffffffff)
                idct_triplet(idct, val, 0, 0);
            break;
        case IDCT_LEVELS:
            if (!idct->phase) {
                idct->level0 = val;
                idct->phase  = 1;
                break;
            }
            idct->phase = 0;
            if (idct->runs == 0xffffffff)
                break; /* ended on its RUNS write */
            if (mach64->rp.gui_cntl & GUI_CNTL_IDCT_PRSR_MODE)
                idct_triplet(idct, idct->runs, val, idct->level0);
            else
                idct_triplet(idct, idct->runs, idct->level0, val);
            break;
        default:
            break;
    }
    idct_drain(mach64);
}

void
mach64_idct_write(mach64_t *mach64, uint32_t reg, uint32_t val)
{
    idct_write(mach64, reg, val, 0);
}

void
mach64_idct_bm_write(mach64_t *mach64, uint32_t reg, uint32_t val)
{
    idct_write(mach64, reg, val, 1);
}

/* The end-of-block request of a BM_ADDR packet with IDCT_FLAGS 00. */
void
mach64_idct_end_block(mach64_t *mach64)
{
    mach64_idct_t *idct = mach64->idct;

    if (idct) {
        idct_triplet(idct, 0xffffffff, 0, 0);
        /* Clear inter residuals still advance the decoder's running key,
           one step for each of the six macroblock blocks. */
        if (mach64_idct_authenticated(mach64) && !mach64_idct_intra(mach64))
            idct->key = idct_key_step(idct->key);
        idct_drain(mach64);
    }
}

int
mach64_idct_reg(const mach64_t *mach64, uint32_t reg)
{
    return mach64->idct && (reg >= IDCT_FIRST) && (reg <= IDCT_LAST);
}

/* A draw of width by height pixels that takes the residual: it takes the
   blocks of its tiles from the queue. */
void
mach64_idct_draw_begin(mach64_t *mach64, int width, int height)
{
    mach64_idct_t *idct = mach64->idct;

    idct->draw_tiles_x = (width + 7) >> 3;
    idct->draw_tiles   = idct->draw_tiles_x * ((height + 7) >> 3);
}

/* The residual of the pixel at dx, dy from the draw's first pixel, in the
   order it is drawn. */
int
mach64_idct_residual(const mach64_t *mach64, int dx, int dy)
{
    const mach64_idct_t *idct = mach64->idct;
    unsigned             tile = (unsigned) ((dy >> 3) * idct->draw_tiles_x + (dx >> 3));

    if (tile >= idct->count)
        return 0;
    return idct->queue[(idct->head + tile) % IDCT_QUEUE][((dy & 7) << 3) | (dx & 7)];
}

void
mach64_idct_draw_end(mach64_t *mach64)
{
    mach64_idct_t *idct = mach64->idct;
    unsigned       n    = (unsigned) idct->draw_tiles;

    if (n > idct->count) {
        if (!idct->starved)
            mach64_log("Rage XL IDCT: a draw of %d tiles found %u blocks\n", idct->draw_tiles, idct->count);
        idct->starved += n - idct->count;
        n = idct->count;
    }
    idct->head  = (idct->head + n) % IDCT_QUEUE;
    idct->count -= n;
    idct->draw_tiles = 0;
}

void
mach64_idct_reset(mach64_t *mach64)
{
    if (mach64->idct) {
        idct_discard_draws(mach64->idct);
        memset(mach64->idct, 0, sizeof(mach64_idct_t));
    }
}

mach64_idct_t *
mach64_idct_init(void)
{
    idct_init_table();
    return calloc(1, sizeof(mach64_idct_t));
}

void
mach64_idct_close(mach64_idct_t *idct)
{
    if (idct) idct_discard_draws(idct);
    free(idct);
}
