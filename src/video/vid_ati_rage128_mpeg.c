/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- MPEG-2 macroblock assist: PACKET3 op 0x31,
 *          the IDCT and motion-compensation back end used for DVD playback.
 *
 *          One packet carries one macroblock: a list of writes to the four
 *          motion-compensation (MC) control registers, followed by the
 *          run-level DCT coefficients of the macroblock's six 8x8 blocks.
 *          The host parses the bitstream and sends dequantized coefficients
 *          as run-level pairs; the chip expands them, runs the IDCT, fetches
 *          the motion-compensated prediction from the frame buffer, adds the
 *          two and writes the result back (GCS: IDCT Engine, pp. 4-4-4-5 /
 *          PDF 52-53).
 *
 *          The RRG gives bit ranges for the fields of MC_SRC1_CNTL,
 *          MC_SRC2_CNTL, MC_DST_CNTL and MC_START_CNTL but describes none of
 *          them beyond the on/off values of SECONDARY_TEX_EN and ALPHA_EN
 *          (RRG: MC_SRC1_CNTL, p. 3-261 / PDF 279; RRG: MC_START_CNTL,
 *          p. 3-262 / PDF 280), and it has no page for the packet. What the
 *          fields mean, the packet grammar and the arithmetic come from
 *          reverse engineering four binaries. Addresses are virtual
 *          addresses at each image's preferred base.
 *
 *          - atimcraa.dll, ATI's MCAM motion-compensation DirectDraw HAL
 *            for the Rage 128 (base 0xb0100000). It carries a software
 *            model of the block, its Emulate path, which is the main source
 *            here: the packet parser (RE: atimcraa.dll @b0102770), the
 *            coefficient feeder (RE: atimcraa.dll @b0103020), the block
 *            assembler (RE: atimcraa.dll @b01043e0), the per-launch
 *            executor (RE: atimcraa.dll @b01045b0) and the four register
 *            decoders (RE: atimcraa.dll @b01042c0, @b0104310, @b0104340,
 *            @b0104370). The tables below are copied from the same image.
 *          - cinmst32.dll, the CineMaster MPEG-2 decoder in ATI's DVD
 *            player (base 0x4b000000), which builds the packets.
 *          - atimiaxx.dll, the IDCT authentication DLL (base 0x10000000),
 *            and ati3draa.dll, the DirectDraw display driver (base
 *            0xb00b0000), the two drivers of the authentication port.
 *
 *          The HAL, the authentication DLL and the display driver ship in
 *          ATI's Rage 128 Windows 98 driver package.
 *
 *          Relevant literature:
 *
 *          [1] ATI Technologies, "RAGE 128 PRO Register Reference Guide",
 *              RRG-G04500-C Rev 1.01, January 2000. Cited below as
 *              "RRG: <register>, p. <printed page> / PDF <viewer page>".
 *
 *          [2] ATI Technologies, "RAGE 128 VR / RAGE 128 GL Graphics
 *              Controller Specifications", GCS-C04100 Rev 0.05, November
 *              1998 (the IDCT engine overview). Cited as "GCS: ...".
 *
 *          [3] ISO/IEC 13818-2:1996, "Information technology -- Generic
 *              coding of moving pictures and associated audio information:
 *              Video", first edition, May 1996 (MPEG-2 video). Cited as
 *              "ISO/IEC 13818-2, <clause>".
 *
 *          [4] IEEE Std 1180-1990, "IEEE Standard Specifications for the
 *              Implementations of 8x8 Inverse Discrete Cosine Transform",
 *              March 1991.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_ati_rage128.h>
#include <86box/vid_ati_rage128_regs.h>

/* Coefficient scan orders, indexed by the mode word's alternate-scan flag
   (bit 4): 0 is the zigzag scan, 1 the MPEG-2 alternate scan (ISO/IEC
   13818-2, 7.3). Entry [n] is the raster index of scan position n. The HAL
   image holds the same two tables (RE: atimcraa.dll @b0120110). */
static const uint8_t mc_scan[2][64] = {
    { 0x00, 0x01, 0x08, 0x10, 0x09, 0x02, 0x03, 0x0a,
     0x11, 0x18, 0x20, 0x19, 0x12, 0x0b, 0x04, 0x05,
     0x0c, 0x13, 0x1a, 0x21, 0x28, 0x30, 0x29, 0x22,
     0x1b, 0x14, 0x0d, 0x06, 0x07, 0x0e, 0x15, 0x1c,
     0x23, 0x2a, 0x31, 0x38, 0x39, 0x32, 0x2b, 0x24,
     0x1d, 0x16, 0x0f, 0x17, 0x1e, 0x25, 0x2c, 0x33,
     0x3a, 0x3b, 0x34, 0x2d, 0x26, 0x1f, 0x27, 0x2e,
     0x35, 0x3c, 0x3d, 0x36, 0x2f, 0x37, 0x3e, 0x3f },
    { 0x00, 0x08, 0x10, 0x18, 0x01, 0x09, 0x02, 0x0a,
     0x11, 0x19, 0x20, 0x28, 0x30, 0x38, 0x39, 0x31,
     0x29, 0x21, 0x1a, 0x12, 0x03, 0x0b, 0x04, 0x0c,
     0x13, 0x1b, 0x22, 0x2a, 0x32, 0x3a, 0x23, 0x2b,
     0x33, 0x3b, 0x14, 0x1c, 0x05, 0x0d, 0x06, 0x0e,
     0x15, 0x1d, 0x24, 0x2c, 0x34, 0x3c, 0x25, 0x2d,
     0x35, 0x3d, 0x16, 0x1e, 0x07, 0x0f, 0x17, 0x1f,
     0x26, 0x2e, 0x36, 0x3e, 0x27, 0x2f, 0x37, 0x3f }
};

/* Row orders applied when the six transformed blocks are assembled into the
   residual (16 luma rows, then 8 rows each of Cb and Cr): [n] is the source
   row for destination row n. Luma is indexed by mode word bits [1:0],
   chroma by bit 2.

   Luma order 0 keeps the rows as they are, 1 takes the even rows and then
   the odd rows, and 2 interleaves the top eight rows with the bottom eight.
   Order 2 is the one that turns field-DCT blocks, where the top pair of
   blocks holds one field and the bottom pair the other, into frame order.
   Chroma order 0 keeps the rows and 1 takes even then odd.

   The HAL image holds the three luma orders (RE: atimcraa.dll @b0120310)
   and, directly after them, the two chroma orders (RE: atimcraa.dll
   @b01203d0). Luma entry 3 here is those chroma entries, the 16 values a
   luma index of 3 reaches past the end of the luma table. */
static const uint8_t mc_rowperm[4][16] = {
    { 0, 1, 2, 3, 4, 5,  6,  7,  8, 9,  10, 11, 12, 13, 14, 15 },
    { 0, 2, 4, 6, 8, 10, 12, 14, 1, 3,  5,  7,  9,  11, 13, 15 },
    { 0, 8, 1, 9, 2, 10, 3,  11, 4, 12, 5,  13, 6,  14, 7,  15 },
    { 0, 1, 2, 3, 4, 5,  6,  7,  0, 2,  4,  6,  1,  3,  5,  7  }
};
static const uint8_t mc_cperm[2][8] = {
    { 0, 1, 2, 3, 4, 5, 6, 7 },
    { 0, 2, 4, 6, 1, 3, 5, 7 }
};

/* Block geometry for the MC_START_CNTL.DST_HEIGHT_WIDTH code [31:29], as
   width and height (RE: atimcraa.dll @b0120420). A code with zero height is
   invalid and its launch is rejected.

   mc_pitch_adj is the multiplier that the [31:30] pitch fields of all three
   surfaces decode to: SCALE_PITCH_ADJ, SECONDARY_SCALE_PITCH_ADJ and
   DST_PITCH_ADJ (RE: atimcraa.dll @b0120410). The RRG describes the same
   field of SECONDARY_SCALE_PITCH as multiply by 1, 2 or 4, with 3 reserved
   (RRG: SECONDARY_SCALE_PITCH, p. 3-160 / PDF 178). The HAL's table maps 3
   to 0; this model does not reject a launch that uses it. */
static const uint8_t mc_blk_size[8][2] = {
    { 8,  4  },
    { 8,  8  },
    { 0,  0  },
    { 0,  0  },
    { 0,  0  },
    { 16, 8  },
    { 16, 16 },
    { 0,  0  }
};
static const uint8_t mc_pitch_adj[4] = { 1, 2, 4, 0 };

/* An op-0x31 header, count field masked out. */
#define MC_IS_HDR(v)   (((v) & 0xc000ffffu) == 0xc0003100u)
#define MC_CLAMP255(v) ((v) < 0 ? 0 : ((v) > 255 ? 255 : (v)))

/* --- 8x8 IDCT ---------------------------------------------------------
   The GCS says only that the chip's IDCT is IEEE 1180 compliant (GCS: IDCT
   Engine, p. 4-4 / PDF 52); its algorithm is not documented. The HAL's
   software model carries two different integer IDCT routines and picks
   one at run time (RE: atimcraa.dll @b01032c0, calling @b0103450 or
   @b0104040). Two software substitutes that differ cannot both describe
   the chip, so neither is copied here.

   This is the direct separable IDCT in double precision, rounded to the
   nearest integer: the reference form that IEEE 1180 accuracy is measured
   against, and the one MPEG-2 decoders are held to (ISO/IEC 13818-2,
   Annex A). A block whose only nonzero coefficient is DC takes a shortcut:
   every output sample is DC / 8. */
static double mc_idct_c[8][8];
static int    mc_idct_ready = 0;

static void
mc_idct_init(void)
{
    double pi = 4.0 * atan(1.0);

    if (mc_idct_ready)
        return;
    for (int u = 0; u < 8; u++)
        for (int x = 0; x < 8; x++)
            mc_idct_c[u][x] = (u ? 0.5 : 0.5 / sqrt(2.0))
                * cos((2.0 * x + 1.0) * u * pi / 16.0);
    mc_idct_ready = 1;
}

static void
mc_idct(int16_t *b)
{
    double tmp[64];
    int    ac = 0;

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
    /* Row pass. All-zero rows are skipped, and the live-row mask lets the
       column pass skip them too; in a typical MPEG block most rows below
       the first few are zero. */
    uint32_t live = 0;

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
                if (b[y * 8 + u])
                    s += mc_idct_c[u][x] * b[y * 8 + u];
            tmp[y * 8 + x] = s;
        }
    }
    for (int x = 0; x < 8; x++)
        for (int y = 0; y < 8; y++) {
            double s = 0.0;

            for (int v = 0; v < 8; v++)
                if (live & (1u << v))
                    s += mc_idct_c[v][y] * tmp[v * 8 + x];
            b[y * 8 + x] = (int16_t) lrint(s);
        }
}

/* --- M2IA authentication ----------------------------------------------
   RAGE128_M2IA_STATUS (0x1f88) and RAGE128_M2IA_DATA (0x1f8c) form an
   authentication port between the DVD decoder and the chip. The RRG does
   not list either register. The port gates MPEG decode: the decoder's
   coefficient copy (RE: cinmst32.dll @4b2466a0, which jumps to
   @4b246e80) returns without copying unless a session has completed, and
   the macroblock emitter that calls it ignores the return and advances its
   write pointer over the span the copy should have filled. An
   unauthenticated stream therefore carries register lists and no
   coefficients.

   The register-level exchange comes from two driver implementations that
   share no code and agree on every value: the exported state machine of
   atimiaxx.dll (RE: atimiaxx.dll @10001a70 starts a session, @10001b40
   and @10001c20 are the two rounds after it, and its exported
   ATIMIA_EndAuthentication closes it), and the M2IA surface handler of
   ati3draa.dll, one routine per surface Lock (RE: ati3draa.dll @b01183c0,
   @b01184e0, @b0118600). The sequence, with the STATUS value each step
   checks:

     write 4 then 0 to STATUS   reset; STATUS must read 8
     read DATA                  the chip's challenge; atimiaxx.dll fails on 0
     write DATA = H(challenge)  STATUS[1:0] must read 1
     write DATA = host key      any nonzero value; STATUS[3:0] reads 9
     read DATA                  must equal H(host key); STATUS reads 0x1a

   A wrong 8, a wrong STATUS[1:0] or a wrong DATA value fails the session;
   the drivers poll for 9 and 0x1a but carry on when the poll times out.
   The decoder computes both H values and the drivers only pass them on.

   H is the decoder's hash (RE: cinmst32.dll @4b246740): two fixed bit
   rearrangements followed by 31 rounds of a shift-and-XOR step. No driver
   binary contains it -- atimiaxx.dll takes each response as an argument
   and never computes one -- so the copy below is transcribed from
   cinmst32.dll alone. It was checked by running the three routines from
   that image on 47 inputs and comparing with the C; all matched.

   A completed session also keys the coefficient scramble: the decoder
   starts its running key at H(H(host key) ^ (H(challenge) & 0x9009000c))
   (RE: cinmst32.dll @4b246c00) and steps it once per 8x8 block. */
enum {
    M2IA_RESET = 0, /* after the 4/0 pulse: challenge not yet taken */
    M2IA_CHAL,      /* challenge presented, awaiting its response   */
    M2IA_ACK1,      /* response accepted, awaiting the host key     */
    M2IA_RESP,      /* response to the host key ready on DATA       */
    M2IA_ARMED,     /* host read it back: session live              */
    M2IA_FAIL
};

/* STATUS as read in each state. FAIL reads 0, which matches none of the
   values the drivers check for; what the chip returns after a wrong
   response is not documented. */
static const uint32_t m2ia_status[] = { 8, 8, 1, 9, 0x1a, 0 };

/* One-shot log latches. The first armed session is logged because without
   one no coefficients arrive; the first wrong response is logged because it
   points at a fault in H. */
static int m2ia_armed_seen = 0;

static void rage128_mpeg_execute(rage128_t *dev);
static int  m2ia_bad_seen = 0;

/* The two bit rearrangements and the round step of H, transcribed from
   the decoder (RE: cinmst32.dll @4b2467a0, @4b246820, @4b246880). The
   second rearrangement also inverts some bits. The masks and shifts are the
   routines' own; no document names them. */
static uint32_t
m2ia_p1(uint32_t c)
{
    uint32_t a = ((c >> 5) & 0x07000000u) | (c & 0x0000e000u);
    uint32_t d;

    a = ((a >> 8) | (c & 0x00001f00u)) >> 2;
    a = ((a | (c & 0x000e0000u)) >> 1) | (c & 0x00000400u);
    d = ((c & 0x0000000fu) << 8) | (c & 0x000000f0u);
    d = ((d << 13) | (c & 0xfe01c000u)) << 1;
    d |= c & 0x00000100u;
    return (a >> 2) | (d << 2) | (c & 0x00000200u);
}

static uint32_t
m2ia_p2(uint32_t c)
{
    uint32_t n = ~c;
    uint32_t a = ((c >> 4) & 0x00ff0000u) | (c & 0xf00000ffu);
    uint32_t e;

    a = ((a >> 2) | (n & 0x003fc000u)) >> 2;
    a |= c & 0x00000ff0u;
    e = ((c & 0x0000000fu) << 9) | (n & 0xffffe000u);
    return (a >> 4) | (e << 11) | (n & 0x000f0000u);
}

static uint32_t
m2ia_round(uint32_t c)
{
    uint32_t a = (c & 0x40u) ^ (c >> 25);

    a = (a >> 1) ^ (c & 0x20u);
    a = (a >> 4) ^ (c & 0x02u);
    return (a >> 1) | (c << 1);
}

static uint32_t
m2ia_hash(uint32_t v)
{
    uint32_t h = m2ia_p2(m2ia_p1(v));

    for (int i = 0; i < 31; i++)
        h = m2ia_round(h);
    return h;
}

static void
m2ia_reset(rage128_t *dev)
{
    dev->mc.m2ia_state     = M2IA_RESET;
    dev->mc.m2ia_reset_req = 0;
    dev->mc.m2ia_key1      = 0;
    dev->mc.m2ia_resp1     = 0;
    dev->mc.m2ia_key2      = 0;
    dev->mc.m2ia_resp2     = 0;
    dev->mc.m2ia_key       = 0;
}

static int
m2ia_armed(const rage128_t *dev)
{
    return dev->mc.m2ia_state == M2IA_ARMED;
}

static uint32_t
m2ia_data_read(rage128_t *dev)
{
    switch (dev->mc.m2ia_state) {
        case M2IA_RESET:
            /* The challenge must not be zero: atimiaxx.dll fails the
               session on a zero read (RE: atimiaxx.dll @10001a70). It is H
               of a counter stepped by 0x9e3779b9 rather than by 1, because
               H gives the same result for very small inputs (H(0), H(1)
               and H(2) are equal). The counter starts at zero on device
               reset, so the same register traffic always produces the same
               session. */
            dev->mc.m2ia_seq += 0x9e3779b9u;
            dev->mc.m2ia_key1 = m2ia_hash(dev->mc.m2ia_seq);
            if (!dev->mc.m2ia_key1)
                dev->mc.m2ia_key1 = 1;
            dev->mc.m2ia_state = M2IA_CHAL;
            return dev->mc.m2ia_key1;
        case M2IA_CHAL:
        case M2IA_ACK1:
            return dev->mc.m2ia_key1;
        case M2IA_RESP:
            dev->mc.m2ia_state = M2IA_ARMED;
            dev->mc.m2ia_key   = m2ia_hash(dev->mc.m2ia_resp2
                                           ^ (dev->mc.m2ia_resp1 & 0x9009000cu));
            if (!m2ia_armed_seen) {
                m2ia_armed_seen = 1;
                rage128_log("[r128 M2IA] session armed: chal=%08x resp1=%08x"
                            " key2=%08x resp2=%08x scramble=%08x\n",
                            dev->mc.m2ia_key1, dev->mc.m2ia_resp1,
                            dev->mc.m2ia_key2, dev->mc.m2ia_resp2, dev->mc.m2ia_key);
            }
            return dev->mc.m2ia_resp2;
        case M2IA_ARMED:
            return dev->mc.m2ia_resp2;
        default:
            break;
    }
    return 0;
}

static void
m2ia_data_write(rage128_t *dev, uint32_t v)
{
    switch (dev->mc.m2ia_state) {
        case M2IA_RESET:
            /* Both drivers can write 0, 0, 0x78d828af here before reading
               the challenge, each behind a flag of its own (RE:
               atimiaxx.dll @10001a70; RE: ati3draa.dll @b01183c0). What
               the writes select is not known; the model ignores them. */
            break;
        case M2IA_CHAL:
            if (v && v == m2ia_hash(dev->mc.m2ia_key1)) {
                dev->mc.m2ia_resp1 = v;
                dev->mc.m2ia_state = M2IA_ACK1;
            } else {
                dev->mc.m2ia_state = M2IA_FAIL;
                if (!m2ia_bad_seen) {
                    m2ia_bad_seen = 1;
                    pclog("[r128 M2IA] wrong response: chal=%08x got=%08x"
                          " want=%08x\n",
                          dev->mc.m2ia_key1, v,
                          m2ia_hash(dev->mc.m2ia_key1));
                }
            }
            break;
        case M2IA_ACK1:
            if (!v) {
                dev->mc.m2ia_state = M2IA_FAIL;
                break;
            }
            dev->mc.m2ia_key2  = v;
            dev->mc.m2ia_resp2 = m2ia_hash(v);
            dev->mc.m2ia_state = M2IA_RESP;
            break;
        default:
            break;
    }
}

/* Inverse of the decoder's coefficient scramble (RE: cinmst32.dll
   @4b246e80). The decoder XORs the run word of each 16-byte coefficient
   record with the running key masked to 0x7f7f7f7f, except that a word of
   0xffffffff is left in the clear. It steps the key once for each plaintext
   run word that has an 0xff byte; 0xff is the block terminator, so that is
   once per 8x8 block.

   The mask leaves bit 7 of every byte alone, so a scrambled word reads
   0xffffffff only if every byte of its plaintext has bit 7 set. A run byte
   is 0..63 or 0xff, so that plaintext is 0xffffffff itself, and the test
   below tells a clear word from a scrambled one without error. */
static int
m2ia_has_ff(uint32_t v)
{
    return (v & 0x000000ffu) == 0x000000ffu || (v & 0x0000ff00u) == 0x0000ff00u
        || (v & 0x00ff0000u) == 0x00ff0000u || (v & 0xff000000u) == 0xff000000u;
}

static uint32_t
m2ia_descramble(rage128_t *dev, uint32_t c)
{
    uint32_t p;

    if (c == 0xffffffffu) {
        dev->mc.m2ia_key = m2ia_round(dev->mc.m2ia_key);
        return c;
    }
    p = c ^ (dev->mc.m2ia_key & 0x7f7f7f7fu);
    if (m2ia_has_ff(p))
        dev->mc.m2ia_key = m2ia_round(dev->mc.m2ia_key);
    return p;
}

/* The key advances six block steps for every packet that carries a
   macroblock, scrambled or not. On the scrambled lane the six steps come
   from the copy, one per 0xff-terminated record. The other macroblock
   emitters -- the inter emitter (RE: cinmst32.dll @4b006d50, the call at
   4b006f68) and the skipped-macroblock forms -- call a helper that steps
   the key six times and scrambles nothing (RE: cinmst32.dll @4b246f80).
   Packets with a count of 1 carry no macroblock and advance nothing.

   This schedule was checked against a dump of an armed session's op-0x31
   traffic (RAGE128_MCDUMP below, 137 frames). The count-1 packets in it
   come three per frame and hold only a mode word. Under the schedule all
   113,937 coefficient records descramble to run words that are legal MPEG
   runs, and every scrambled macroblock uses exactly six steps. The packet handler
   sets the key from the value saved at packet entry instead of relying on
   the record walk, so a record list abandoned partway cannot leave the key
   out of step. */
static uint32_t
m2ia_advance_mb(uint32_t base)
{
    for (int i = 0; i < 6; i++)
        base = m2ia_round(base);
    return base;
}

/* --- register file ---------------------------------------------------- */

/* The MC control registers keep their values between packets: a packet is
   a list of writes to them, and a field it does not write keeps its last
   value. The model depends on this. The intra form of the packet never
   writes MC_SRC1_CNTL (RE: cinmst32.dll @4b006ae0), so its launches use
   whatever IDCT_EN was last written; in captured guest traffic that is a
   direct register write of MC_SRC1_CNTL, once per frame.

   The field splits below are the RRG's bit ranges (RRG: MC_SRC1_CNTL,
   p. 3-261 / PDF 279), read the same way by the HAL's decoders (RE:
   atimcraa.dll @b01042c0, @b0104310, @b0104340). */
static void
mc_set_src1(rage128_t *dev, uint32_t v)
{
    dev->mc.x1      = v & 0x1fff;
    dev->mc.y1      = (v >> 16) & 0xfff;
    dev->mc.idct_en = (v >> 28) & 1;
    dev->mc.sec_tex = (v >> 29) & 1;
    dev->mc.adj1    = mc_pitch_adj[(v >> 30) & 3];
}

static void
mc_set_src2(rage128_t *dev, uint32_t v)
{
    dev->mc.x2   = v & 0x1fff;
    dev->mc.y2   = (v >> 16) & 0xfff;
    dev->mc.adj2 = mc_pitch_adj[(v >> 30) & 3];
}

static void
mc_set_dst(rage128_t *dev, uint32_t v)
{
    dev->mc.dx   = (v >> 16) & 0x3fff;
    dev->mc.dy   = v & 0x3fff;
    dev->mc.adjd = mc_pitch_adj[(v >> 30) & 3];
}

/* MC_START_CNTL is the launch write. Its fields are the source-1 slot
   (SCALE_OFFSET_PTR), the source-2 slot plus one with 0 meaning none
   (SECONDARY_OFFSET_PTR), the destination base (DST_OFFSET) and the block
   geometry (DST_HEIGHT_WIDTH) (RRG: MC_START_CNTL, p. 3-262 / PDF 280).
   The RRG names bit 25 ALPHA_EN; the HAL's executor uses it to choose
   between prediction plus residual and the residual alone (RE:
   atimcraa.dll @b01045b0). The write copies the current source and
   destination state into the launch queue.

   Returns 0 when the value fails the HAL's validity test -- source-1 slot
   above 11, source-2 slot above 5, or an invalid geometry (RE:
   atimcraa.dll @b0104370) -- or when the six-entry queue is full. The
   caller then abandons the rest of the packet. */
static int
mc_start(rage128_t *dev, uint32_t v)
{
    rage128_mc_cmd_t *c;
    uint32_t          code = (v >> 29) & 7;
    uint32_t          s1   = v & 0xf;
    int32_t           s2   = (int32_t) ((v >> 26) & 7) - 1;

    if (!mc_blk_size[code][1] || s1 >= 12 || s2 >= 6)
        return 0;
    if (dev->mc.ncmd >= 6)
        return 0;
    c           = &dev->mc.cmd[dev->mc.ncmd++];
    c->x1       = dev->mc.x1;
    c->y1       = dev->mc.y1;
    c->idct_en  = dev->mc.idct_en;
    c->sec_tex  = dev->mc.sec_tex;
    c->adj1     = dev->mc.adj1;
    c->x2       = dev->mc.x2;
    c->y2       = dev->mc.y2;
    c->adj2     = dev->mc.adj2;
    c->dx       = dev->mc.dx;
    c->dy       = dev->mc.dy;
    c->adjd     = dev->mc.adjd;
    c->slot1    = s1;
    c->slot2    = s2;
    c->dst_addr = v & 0x01fffff0u;
    c->pred     = (v >> 25) & 1;
    c->w        = mc_blk_size[code][0];
    c->h        = mc_blk_size[code][1];
    return 1;
}

int
rage128_mpeg_reg_read(rage128_t *dev, uint32_t off, uint32_t *val)
{
    if (off >= RAGE128_PRIM_TEX_OFFSET(0) && off <= RAGE128_PRIM_TEX_OFFSET(10)) {
        *val = dev->mc.tex_off[(off - RAGE128_PRIM_TEX_OFFSET(0)) >> 2];
        return 1;
    }
    if (off >= RAGE128_SEC_TEX_OFFSET(0) && off <= RAGE128_SEC_TEX_OFFSET(10)) {
        *val = dev->mc.sec_off[(off - RAGE128_SEC_TEX_OFFSET(0)) >> 2];
        return 1;
    }
    switch (off) {
        case RAGE128_TEX_CNTL:
            *val = dev->mc.tex_cntl;
            return 1;
        case RAGE128_SCALE_PITCH:
            *val = dev->mc.scale_pitch;
            return 1;
        case RAGE128_SECONDARY_SCALE_PITCH:
            *val = dev->mc.sec_scale_pitch;
            return 1;
        case RAGE128_M2IA_STATUS:
            *val = m2ia_status[dev->mc.m2ia_state];
            return 1;
        case RAGE128_M2IA_DATA:
            *val = m2ia_data_read(dev);
            return 1;
        default:
            break;
    }
    return 0;
}

int
rage128_mpeg_reg_write(rage128_t *dev, uint32_t off, uint32_t val, uint32_t mask)
{
#define MERGE(f) (f) = ((f) & ~mask) | (val & mask)
    if (off >= RAGE128_PRIM_TEX_OFFSET(0) && off <= RAGE128_PRIM_TEX_OFFSET(10)) {
        MERGE(dev->mc.tex_off[(off - RAGE128_PRIM_TEX_OFFSET(0)) >> 2]);
        return 1;
    }
    if (off >= RAGE128_SEC_TEX_OFFSET(0) && off <= RAGE128_SEC_TEX_OFFSET(10)) {
        MERGE(dev->mc.sec_off[(off - RAGE128_SEC_TEX_OFFSET(0)) >> 2]);
        return 1;
    }
    switch (off) {
        case RAGE128_TEX_CNTL:
            MERGE(dev->mc.tex_cntl);
            return 1;
        case RAGE128_SCALE_PITCH:
            MERGE(dev->mc.scale_pitch);
            return 1;
        case RAGE128_SECONDARY_SCALE_PITCH:
            MERGE(dev->mc.sec_scale_pitch);
            return 1;
        case RAGE128_M2IA_STATUS:
            /* Both drivers open a session by writing 4 and then 0 here,
               and atimiaxx.dll closes one the same way; they write no
               other value. The port resets on a 0 that follows a 4. */
            MERGE(dev->mc.m2ia_w88);
            if (dev->mc.m2ia_w88 == 4)
                dev->mc.m2ia_reset_req = 1;
            else if (dev->mc.m2ia_w88 == 0 && dev->mc.m2ia_reset_req)
                m2ia_reset(dev);
            return 1;
        case RAGE128_M2IA_DATA:
            MERGE(dev->mc.m2ia_w8c);
            m2ia_data_write(dev, dev->mc.m2ia_w8c);
            return 1;
        case RAGE128_MC_SRC1_CNTL:
            MERGE(dev->mc.src1_cntl);
            mc_set_src1(dev, dev->mc.src1_cntl);
            return 1;
        case RAGE128_MC_SRC2_CNTL:
            MERGE(dev->mc.src2_cntl);
            mc_set_src2(dev, dev->mc.src2_cntl);
            return 1;
        case RAGE128_MC_DST_CNTL:
            MERGE(dev->mc.dst_cntl);
            mc_set_dst(dev, dev->mc.dst_cntl);
            return 1;
        case RAGE128_MC_START_CNTL:
            /* A launch written as a plain register write has no
               coefficients -- this model takes them only from the packet
               -- so it runs against a zero residual. In captured DVD
               player traffic every launch arrives inside an op-0x31
               packet. */
            MERGE(dev->mc.start_cntl);
            dev->mc.ncmd = 0;
            if (mc_start(dev, dev->mc.start_cntl)) {
                memset(dev->mc.resid, 0, sizeof(dev->mc.resid));
                rage128_mpeg_execute(dev);
            }
            return 1;
        default:
            break;
    }
    return 0;
#undef MERGE
}

/* --- coefficients ------------------------------------------------------ */

/* One code-1 record carries four run-level pairs: the four run bytes in
   ctrl, high byte first, and the four levels as the signed 16-bit halves
   of b and then a, high half first. A run of 0xff ends the block early and
   its level is ignored. Levels are dequantized coefficients from the host,
   clamped to -2048..2047, the MPEG-2 saturation range (ISO/IEC 13818-2,
   7.4.3); the HAL's coefficient feeder applies the same clamp (RE:
   atimcraa.dll @b0103020). Returns 1 if the record ended the block. */
static int
mc_coefs(rage128_t *dev, uint32_t ctrl, uint32_t a, uint32_t b)
{
    const int32_t lvl[4] = { (int16_t) (b >> 16), (int16_t) b,
                             (int16_t) (a >> 16), (int16_t) a };
    int           term   = 0;

    for (int i = 0; i < 4; i++) {
        uint32_t run = (ctrl >> (24 - 8 * i)) & 0xff;
        int32_t  v   = lvl[i];

        if (run == 0xff) {
            dev->mc.pos = 64;
            term        = 1;
            continue;
        }
        dev->mc.pos += (int) run;
        if (dev->mc.pos >= 64)
            continue;
        if (v < -2048)
            v = -2048;
        else if (v > 2047)
            v = 2047;
        dev->mc.blk_data[dev->mc.blk][mc_scan[dev->mc.alt_scan][dev->mc.pos]]
            = (int16_t) v;
        dev->mc.pos++;
        dev->mc.dirty = 1;
    }
    return term;
}

/* Assemble the six transformed blocks into the residual the executor
   reads: 16 luma rows of 16 (blocks 0 and 1 side by side on top, 2 and 3
   below), then 8 Cb rows and 8 Cr rows of 8, with the packet's row orders
   applied (RE: atimcraa.dll @b01043e0). */
static void
mc_assemble(rage128_t *dev)
{
    const uint8_t *rp = mc_rowperm[dev->mc.rowperm];
    const uint8_t *cp = mc_cperm[dev->mc.cperm];
    int16_t        luma[16][16];

    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            luma[r][c]         = dev->mc.blk_data[0][r * 8 + c];
            luma[r][c + 8]     = dev->mc.blk_data[1][r * 8 + c];
            luma[r + 8][c]     = dev->mc.blk_data[2][r * 8 + c];
            luma[r + 8][c + 8] = dev->mc.blk_data[3][r * 8 + c];
        }
    for (int r = 0; r < 16; r++)
        memcpy(dev->mc.resid[r], luma[rp[r]], 16 * sizeof(int16_t));
    for (int r = 0; r < 8; r++) {
        memcpy(dev->mc.resid[16 + r], &dev->mc.blk_data[4][cp[r] * 8],
               8 * sizeof(int16_t));
        memcpy(dev->mc.resid[24 + r], &dev->mc.blk_data[5][cp[r] * 8],
               8 * sizeof(int16_t));
    }
}

/* --- execution --------------------------------------------------------- */

static uint8_t
mc_fetch(const uint8_t *vram, uint32_t vmask, uint32_t addr, uint32_t pitch,
         int xh, int yh)
{
    uint32_t a = addr & vmask;

    if (!xh && !yh)
        return vram[a];
    if (!xh)
        return (uint8_t) ((vram[a] + vram[(addr + pitch) & vmask] + 1) >> 1);
    if (!yh)
        return (uint8_t) ((vram[a] + vram[(addr + 1) & vmask] + 1) >> 1);
    return (uint8_t) ((vram[a] + vram[(addr + 1) & vmask]
                       + vram[(addr + pitch) & vmask]
                       + vram[(addr + pitch + 1) & vmask] + 2)
                      >> 2);
}

static uint32_t
mc_slot_src1(const rage128_t *dev, uint32_t slot)
{
    return (slot < 11) ? dev->mc.tex_off[slot] : dev->mc.sec_off[0];
}

static uint32_t
mc_slot_src2(const rage128_t *dev, uint32_t slot)
{
    return dev->mc.sec_off[slot + 1];
}

/* Run one queued launch. resid is its slice of the assembled residual (h
   rows of 16). Returns the rows used, so the next launch starts where this
   one stopped; the HAL checks the total against 32 (RE: atimcraa.dll
   @b01043e0).

   Addressing and rounding follow the HAL's executor (RE: atimcraa.dll
   @b01045b0). Source positions are in half pixels: the position shifted
   right by one addresses the reference, and the low bit selects a half-pel
   average rounded up, (a + b + 1) / 2 in one direction or
   (a + b + c + d + 2) / 4 in both. With a second reference the two
   predictions are averaged the same way. The prediction plus the residual
   is clamped to 0..255; without prediction the residual is stored alone.

   Each surface's pitch is its pitch-adjust multiplier (SCALE_PITCH_ADJ,
   SECONDARY_SCALE_PITCH_ADJ or DST_PITCH_ADJ) times a base pitch in
   units of 8 pixels, one base per surface as in the HAL. This model takes
   the bases from SCALE_PITCH (source 1; a nine-bit field, [8:0], whose
   bit 9 is reserved and takes no part in the pitch: SDK: Table F-21,
   p. F-32 / PDF 322), SECONDARY_SCALE_PITCH (source 2; also a nine-bit
   field with bit 9 reserved: RRG: SECONDARY_SCALE_PITCH, p. 3-160 /
   PDF 178) and the 2D engine's DST_PITCH (destination, a ten-bit field;
   RRG: DST_PITCH, p. 3-137 / PDF 155). */
static int
mc_run(rage128_t *dev, const rage128_mc_cmd_t *c, const int16_t (*resid)[16])
{
    static int no_idct_seen = 0;
    svga_t    *svga         = &dev->svga;
    uint8_t   *vram         = svga->vram;
    uint32_t   vmask        = dev->vram_mask;
    uint32_t   p1           = c->adj1 * (dev->mc.scale_pitch & 0x1ff) * 8u;
    uint32_t   p2           = c->adj2 * (dev->mc.sec_scale_pitch & 0x1ff) * 8u;
    uint32_t   pd           = c->adjd * (dev->dst_pitch_reg & 0x3ff) * 8u;
    uint32_t   dst          = c->dst_addr + c->dy * pd + c->dx;
    uint32_t   a1           = 0;
    uint32_t   a2           = 0;

    if (!c->idct_en) {
        /* The HAL's model refuses a launch with IDCT_EN clear and reports
           "IDCT not enabled - not supported for now" (RE: atimcraa.dll
           @b01045b0), so there is no evidence for what the chip does
           without it. The launch is dropped. */
        if (!no_idct_seen) {
            no_idct_seen = 1;
            rage128_log("[r128 MC] launch with IDCT_EN clear, block dropped:"
                        " dst=%08x %ux%u\n",
                        c->dst_addr, c->w, c->h);
        }
        return (int) c->h;
    }
    if (c->pred) {
        a1 = mc_slot_src1(dev, c->slot1) + (c->x1 >> 1) + (c->y1 >> 1) * p1;
        if (c->sec_tex && c->slot2 >= 0)
            a2 = mc_slot_src2(dev, (uint32_t) c->slot2)
                + (c->x2 >> 1) + (c->y2 >> 1) * p2;
    }
    for (uint32_t y = 0; y < c->h; y++) {
        for (uint32_t x = 0; x < c->w; x++) {
            int32_t v;

            if (!c->pred)
                v = resid[y][x];
            else {
                v = mc_fetch(vram, vmask, a1 + x, p1, c->x1 & 1, c->y1 & 1);
                if (c->sec_tex && c->slot2 >= 0) {
                    int32_t v2 = mc_fetch(vram, vmask, a2 + x, p2,
                                          c->x2 & 1, c->y2 & 1);

                    v = (v2 + 1 + v) >> 1;
                }
                v += resid[y][x];
            }
            vram[(dst + x) & vmask] = (uint8_t) MC_CLAMP255(v);
        }
        svga->changedvram[(dst & vmask) >> 12] = svga->monitor->mon_changeframecount;
        svga->changedvram[((dst + c->w - 1u) & vmask) >> 12]
            = svga->monitor->mon_changeframecount;
        dst += pd;
        a1 += p1;
        a2 += p2;
    }
    return (int) c->h;
}

static void
rage128_mpeg_execute(rage128_t *dev)
{
    int row = 0;

    for (int i = 0; i < dev->mc.ncmd; i++) {
        if (row + (int) dev->mc.cmd[i].h > 32)
            break; /* would read past the assembled residual */
        row += mc_run(dev, &dev->mc.cmd[i], &dev->mc.resid[row]);
    }
    dev->mc.ncmd = 0;
}

static void
mc_reset_mb(rage128_t *dev)
{
    dev->mc.blk   = 0;
    dev->mc.pos   = 0;
    dev->mc.dirty = 0;
    dev->mc.ncmd  = 0;
    memset(dev->mc.blk_data, 0, sizeof(dev->mc.blk_data));
}

/* Finish the current 8x8 block: transform it, apply the intra offset, and
   move to the next block. For an intra macroblock the HAL's model adds 128
   to each sample after the IDCT and clamps to 0..255 (RE: atimcraa.dll
   @b01032c0). The sixth block completes the macroblock: its queued
   launches run and a fresh macroblock starts. */
static void
mc_block_done(rage128_t *dev)
{
    int16_t *b = dev->mc.blk_data[dev->mc.blk];

    mc_idct(b);
    if (dev->mc.intra)
        for (int i = 0; i < 64; i++)
            b[i] = (int16_t) MC_CLAMP255(b[i] + 128);
    dev->mc.dirty = 0;
    dev->mc.pos   = 0;
    if (++dev->mc.blk < 6)
        return;
    mc_assemble(dev);
    rage128_mpeg_execute(dev);
    mc_reset_mb(dev);
}

/* Run whatever the macroblock has so far and start a fresh one. A
   macroblock can end with fewer than six blocks: the blocks it did not send
   stay zero, which is what MPEG-2 means by an uncoded block, and its
   launches still cover all 32 residual rows. */
static void
mc_flush(rage128_t *dev)
{
    if (dev->mc.dirty)
        mc_block_done(dev);
    if (dev->mc.ncmd) {
        mc_assemble(dev);
        rage128_mpeg_execute(dev);
    }
    mc_reset_mb(dev);
}

/* Abandon the macroblock without running it. This is reached when the
   record list stops making sense. Running it would be unsafe: a destination
   decoded from a stale buffer tail is an arbitrary card address, and a
   write that lands on a live indirect buffer corrupts the command stream. */
static void
mc_drop(rage128_t *dev)
{
    mc_reset_mb(dev);
}

static void
mc_set_mode(rage128_t *dev, uint32_t flags)
{
    dev->mc.rowperm  = flags & 3;
    dev->mc.cperm    = (flags >> 2) & 1;
    dev->mc.alt_scan = (flags >> 4) & 1;
    dev->mc.intra    = (flags >> 5) & 1;
}

/* --- packet ------------------------------------------------------------ */

/* RAGE128_MCDUMP=<path> writes every op-0x31 payload of an armed session
   as the device receives it, one line per packet, with the scramble key in
   force at packet entry. Diagnostic only. The descramble schedule has to
   be checked against real armed traffic, and packets rebuilt from a bus
   capture cannot show the key. */
static FILE *mc_dump_file;
static int   mc_dump_tried;

static void
mc_dump(const uint32_t *pl, uint32_t count, uint32_t key)
{
    if (!mc_dump_tried) {
        const char *env = getenv("RAGE128_MCDUMP");
        char        rel[512];
        char        path[1024 + 512];

        mc_dump_tried = 1;
        if (env && *env && strlen(env) >= sizeof(rel))
            /* refused rather than cut short: a shorter path names a
               different file, which the dump would then overwrite */
            pclog("[r128 MC] dump path longer than %u bytes, dump off\n",
                  (unsigned) sizeof(rel) - 1);
        else if (env && *env) {
            /* a relative path is taken from the machine's folder, as the
               census and the GPU telemetry log are; the folder path
               already ends in a separator, and the buffer holds the
               longest folder path plus the longest relative one */
            snprintf(rel, sizeof(rel), "%s", env);
            if (path_abs(rel))
                snprintf(path, sizeof(path), "%s", rel);
            else
                snprintf(path, sizeof(path), "%s%s", usr_path, rel);
            mc_dump_file = plat_fopen(path, "w");
        }
    }
    if (!mc_dump_file)
        return;
    fprintf(mc_dump_file, "MB %08x %u %08x", pl[0], count, key);
    for (uint32_t i = 1; i < count; i++)
        fprintf(mc_dump_file, " %08x", pl[i]);
    fputc('\n', mc_dump_file);
}

/* op 0x31: one MPEG-2 macroblock per packet. payload[0] is the mode word
   and the rest is a list of self-describing records (RE: atimcraa.dll
   @b0102770):

     0        end of block: transform it and advance; the sixth ends the macroblock
     1  +3    four run-level coefficient pairs
     2, 4     reserved codes, walked past with no register write
     3  +1    MC_START_CNTL, the last record of every register list
     5  +4x2  MC_SRC2_CNTL, MC_SRC1_CNTL, MC_DST_CNTL, MC_START_CNTL; luma, then chroma
     6  +3x2  MC_SRC1_CNTL, MC_DST_CNTL, MC_START_CNTL; luma, then chroma
     7  +2    MC_DST_CNTL, MC_START_CNTL

   Codes 5 and 6 run twice off one code word: 4:2:0 chroma is the same
   command with every coordinate halved.

   Mode bit 0x400 marks the decoder's out-of-line lane (RE: cinmst32.dll
   @4b006ae0): the coefficient records are built in a scratch buffer, and a
   copy (RE: cinmst32.dll @4b2466a0) places them after the register list.
   That copy needs a completed M2IA session, and the caller ignores its
   error return, so without a session the decoder still advances its write
   pointer over the span. The packet's count then covers a region that
   still holds the buffer's previous contents. A capture of an
   unauthenticated session shows exactly that: all 413,137 nonzero dwords
   in those regions equal what the same buffer address held the previous
   time the buffer was used, and none of the 116,007 op-0x31 headers found
   inside payloads is new. So one packet is one macroblock, and everything
   after a 0x400 register list is stale.

   Without a session a 0x400 packet therefore ends at its code-3 record and
   runs its six blocks with no coefficients, the only data that exists for
   them. With a session the records are present and scrambled, and the walk
   continues into them. Anything else that stops the walk marks a stale
   tail and abandons the packet, because decoding stale bytes produces
   launches at arbitrary card addresses. */
int
rage128_mpeg_packet3(rage128_t *dev, uint32_t hdr, const uint32_t *pl, uint32_t count)
{
    uint32_t p = 1;
    uint32_t mode;
    uint32_t key_base;
    int      armed;
    int      scratch;

    if (RAGE128_PM4_T3_OPCODE(hdr) != RAGE128_PM4_OP_MPEG_MB)
        return 0;
    if (!count)
        return 1; /* no payload, so no macroblock */

    mc_idct_init();
    mode     = pl[0];
    armed    = m2ia_armed(dev);
    scratch  = (mode & 0x400) && armed;
    key_base = dev->mc.m2ia_key;
    if (armed)
        mc_dump(pl, count, key_base);
    mc_set_mode(dev, mode);
    mc_reset_mb(dev);
    if (mode & 0x40)
        goto done; /* mode-only packet: no record list follows */

    while (p < count) {
        uint32_t code = pl[p++];
        int      reps = (code == 5 || code == 6) ? 2 : 1;

        /* In the capture above every op-0x31 header inside a payload lay
           in a stale span, so one here means the macroblock's real records
           have ended. */
        if (MC_IS_HDR(code))
            break;
        while (reps--) {
            uint32_t need;

            if (code == 0) {
                mc_block_done(dev);
                continue;
            }
            if (code == 2 || code == 4)
                continue; /* reserved: walked past, no register written */
            if (code > 7)
                goto bad;
            need = code == 1 ? 3u : code == 5 ? 4u
                : code == 6                   ? 3u
                : code == 7                   ? 2u
                                              : 1u;
            if (p + need > count)
                goto bad;
            /* A header cannot be a record's argument either; one there is
               stale too. */
            for (uint32_t i = 0; i < need; i++)
                if (MC_IS_HDR(pl[p + i]))
                    goto bad;
            if (code == 1) {
                uint32_t ctrl = pl[p];
                int      term;

                if (scratch)
                    ctrl = m2ia_descramble(dev, ctrl);
                term = mc_coefs(dev, ctrl, pl[p + 1], pl[p + 2]);
                p += 3;
                /* On the out-of-line lane the decoder writes only fixed
                   16-byte records, so the 0xff run that closes a block is
                   the only block delimiter there. It is also the record on
                   which the scramble key steps: six per macroblock, the
                   same count as the six-step helper (RE: cinmst32.dll
                   @4b246f80). */
                if (scratch && term)
                    mc_block_done(dev);
                continue;
            }
            if (code == 5)
                mc_set_src2(dev, pl[p++]);
            if (code == 5 || code == 6)
                mc_set_src1(dev, pl[p++]);
            if (code >= 5)
                mc_set_dst(dev, pl[p++]);
            if (!mc_start(dev, pl[p++]))
                goto bad;
        }
        /* The code-3 record closes the register list. Without a session
           the rest of a 0x400 packet is the stale span: finish the six
           blocks the decoder ended out of line, all of them empty, so
           intra blocks store the +128 offset and predicted blocks are a
           plain motion-compensated copy. The stale bytes are never read. */
        if (code == 3 && (mode & 0x400) && !scratch) {
            for (int i = dev->mc.blk; i < 6; i++)
                mc_block_done(dev);
            goto done;
        }
    }
    mc_flush(dev);
    goto done;
bad:
    mc_drop(dev);
done:
    if (armed && count > 1)
        dev->mc.m2ia_key = m2ia_advance_mb(key_base);
    return 1;
}

void
rage128_mpeg_reset(rage128_t *dev)
{
    memset(&dev->mc, 0, sizeof(dev->mc));
}
