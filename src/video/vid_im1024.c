/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the ImageManager 1024 video controller.
 *
 *          Just enough of the Vermont Microsystems IM-1024 is implemented
 *          to support the Windows 1.03 driver. Functions are partially
 *          implemented or hardwired to the behavior expected by the
 *          Windows driver.
 *
 *          One major difference seems to be that in hex mode, coordinates
 *          are passed as 2-byte integer words rather than 4-byte
 *          fixed-point fractions.
 *
 *          It is unknown what triggers this, so for now it's always on.
 *
 *          As well as the usual PGC ring buffer at 0xC6000, the IM1024
 *          appears to have an alternate method of passing commands. This
 *          is enabled by setting 0xC6330 to 1, and then:
 *
 *            CX = count to write
 *            SI -> bytes to write
 *
 *            Set pending bytes to 0
 *            Read [C6331]. This gives number of bytes that can be written:
 *              0xFF => 0, 0xFE => 1, 0xFD => 2 etc.
 *            Write that number of bytes to C6000.
 *            If there are more to come, go back to reading [C6331].
 *
 *            As far as can be determined, at least one byte is always
 *            written; there is no provision to pause if the queue is full.
 *
 *            This is implemented by holding a FIFO of unlimited depth in
 *            the IM1024 to receive the data.
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          John Elliott, <jce@seasip.info>
 *
 *          Copyright 2019 Fred N. van Kempen.
 *          Copyright 2019 John Elliott.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <ctype.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/pit.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/vid_pgc.h>

#define BIOS_ROM_PATH "roms/video/im1024/im1024font.bin"

typedef struct {
    pgc_t pgc;

    uint8_t fontx[256];
    uint8_t fonty[256];
    uint8_t font[256][128];

    uint8_t *fifo;
    unsigned fifo_len,
        fifo_wrptr,
        fifo_rdptr;
} im1024_t;

static video_timings_t timing_im1024 = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

#ifdef ENABLE_IM1024_LOG
int im1024_do_log = ENABLE_IM1024_LOG;

static void
im1024_log(const char *fmt, ...)
{
    va_list ap;

    if (im1024_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define im1024_log(fmt, ...)
#endif

static void
fifo_write(im1024_t *dev, uint8_t val)
{
    im1024_log("IM1024: fifo_write: %02x [rd=%04x wr=%04x]\n",
               val, dev->fifo_rdptr, dev->fifo_wrptr);

    if (((dev->fifo_wrptr + 1) % dev->fifo_len) == dev->fifo_rdptr) {
        /* FIFO is full. Double its size. */
        uint8_t *buf;

        im1024_log("IM1024: fifo_resize: %i to %i\n",
                   dev->fifo_len, 2 * dev->fifo_len);

        buf = realloc(dev->fifo, 2 * dev->fifo_len);
        if (buf == NULL)
            return;

        /* Move the [0..wrptr] range to the newly-allocated area [len..len+wrptr] */
        memmove(buf + dev->fifo_len, buf, dev->fifo_wrptr);
        dev->fifo = buf;
        dev->fifo_wrptr += dev->fifo_len;
        dev->fifo_len *= 2;
    }

    /* Append to the queue. */
    dev->fifo[dev->fifo_wrptr++] = val;

    /* Wrap if end of buffer reached. */
    if (dev->fifo_wrptr >= dev->fifo_len)
        dev->fifo_wrptr = 0;
}

static int
fifo_read(im1024_t *dev)
{
    uint8_t ret;

    if (dev->fifo_wrptr == dev->fifo_rdptr)
        return -1; /* FIFO empty */

    ret = dev->fifo[dev->fifo_rdptr++];
    if (dev->fifo_rdptr >= dev->fifo_len)
        dev->fifo_rdptr = 0;

    im1024_log("IM1024: fifo_read: %02x\n", ret);

    return ret;
}

/*
 * Where a normal PGC would just read from the ring buffer at 0xC6300,
 * the IM-1024 can read from either this or from its internal FIFO.
 *
 * The internal FIFO has priority.
 */
static int
input_byte(pgc_t *pgc, uint8_t *result)
{
    im1024_t *dev = (im1024_t *) pgc;

    /* If input buffer empty, wait for it to fill. */
    while (!pgc->stopped && (dev->fifo_wrptr == dev->fifo_rdptr) && (pgc->mapram[0x300] == pgc->mapram[0x301])) {
        pgc->waiting_input_fifo = 1;
        pgc_sleep(pgc);
    }

    if (pgc->stopped)
        return 0;

    if (pgc->mapram[0x3ff]) {
        /* Reset triggered. */
        pgc_reset(pgc);
        return 0;
    }

    if (dev->fifo_wrptr == dev->fifo_rdptr) {
        *result = pgc->mapram[pgc->mapram[0x301]];
        pgc->mapram[0x301]++;
    } else
        *result = fifo_read(dev);

    return 1;
}

/* Macros to disable clipping and save clip state. */
#define PUSHCLIP                             \
    {                                        \
        uint16_t vp_x1, vp_x2, vp_y1, vp_y2; \
        vp_x1      = pgc->vp_x1;             \
        vp_y1      = pgc->vp_y1;             \
        vp_x2      = pgc->vp_x2;             \
        vp_y2      = pgc->vp_y2;             \
        pgc->vp_x1 = 0;                      \
        pgc->vp_y1 = 0;                      \
        pgc->vp_x2 = pgc->maxw - 1;          \
        pgc->vp_y2 = pgc->maxh - 1;

/* And to restore clip state */
#define POPCLIP         \
    pgc->vp_x1 = vp_x1; \
    pgc->vp_y1 = vp_y1; \
    pgc->vp_x2 = vp_x2; \
    pgc->vp_y2 = vp_y2; \
    }

/* Override memory read to return FIFO space. */
static uint8_t
im1024_read(uint32_t addr, void *priv)
{
    im1024_t *dev = (im1024_t *) priv;

    if (addr == 0xc6331 && dev->pgc.mapram[0x330] == 1) {
        /* Hardcode that there are 128 bytes free. */
        return 0x80;
    }

    return (pgc_read(addr, &dev->pgc));
}

/* Override memory write to handle writes to the FIFO. */
static void
im1024_write(uint32_t addr, uint8_t val, void *priv)
{
    im1024_t *dev = (im1024_t *) priv;

    /*
     * If we are in 'fast' input mode, send all
     * writes to the internal FIFO.
     */
    if (addr >= 0xc6000 && addr < 0xc6100 && dev->pgc.mapram[0x330] == 1) {
        fifo_write(dev, val);

        im1024_log("IM1024: write(%02x)\n", val);

        if (dev->pgc.waiting_input_fifo) {
            dev->pgc.waiting_input_fifo = 0;
            pgc_wake(&dev->pgc);
        }
        return;
    }

    pgc_write(addr, val, &dev->pgc);
}

/*
 * I don't know what the IMGSIZ command does, only that the
 * Windows driver issues it. So just parse and ignore it.
 */
static void
hndl_imgsiz(pgc_t *pgc)
{
#if 0
    im1024_t *dev = (im1024_t *)pgc;
#endif
    int16_t w;
    int16_t h;
    uint8_t a;
    uint8_t b;

    if (!pgc_param_word(pgc, &w))
        return;
    if (!pgc_param_word(pgc, &h))
        return;
    if (!pgc_param_byte(pgc, &a))
        return;
    if (!pgc_param_byte(pgc, &b))
        return;

    im1024_log("IM1024: IMGSIZ %i,%i,%i,%i\n", w, h, a, b);
}

/*
 * I don't know what the IPREC command does, only that the
 * Windows driver issues it. So just parse and ignore it.
 */
static void
hndl_iprec(pgc_t *pgc)
{
#if 0
    im1024_t *dev = (im1024_t *)pgc;
#endif
    uint8_t param;

    if (!pgc_param_byte(pgc, &param))
        return;

    im1024_log("IM1024: IPREC %i\n", param);
}

/*
 * Set drawing mode.
 *
 * 0 => Draw
 * 1 => Invert
 * 2 => XOR (IM-1024)
 * 3 => AND (IM-1024)
 */
static void
hndl_linfun(pgc_t *pgc)
{
    uint8_t param;

    if (!pgc_param_byte(pgc, &param))
        return;

    if (param < 4) {
        pgc->draw_mode = param;
        im1024_log("IM1024: LINFUN(%i)\n", param);
    } else
        pgc_error(pgc, PGC_ERROR_RANGE);
}

/*
 * I think PAN controls which part of the 1024x1024 framebuffer
 * is displayed in the 1024x800 visible screen.
 */
static void
hndl_pan(pgc_t *pgc)
{
    int16_t x;
    int16_t y;

    if (!pgc_param_word(pgc, &x))
        return;
    if (!pgc_param_word(pgc, &y))
        return;

    im1024_log("IM1024: PAN %i,%i\n", x, y);

    pgc->pan_x = x;
    pgc->pan_y = y;
}

/* PLINE draws a non-filled polyline at a fixed position. */
static void
hndl_pline(pgc_t *pgc)
{
    int16_t  x[257];
    int16_t  y[257];
    uint16_t linemask = pgc->line_pattern;
    uint8_t  count;
    unsigned n;

    if (!pgc_param_byte(pgc, &count))
        return;

    im1024_log("IM1024: PLINE (%i)  ", count);
    for (n = 0; n < count; n++) {
        if (!pgc_param_word(pgc, &x[n]))
            return;
        if (!pgc_param_word(pgc, &y[n]))
            return;
        im1024_log("    (%i,%i)\n", x[n], y[n]);
    }

    for (n = 1; n < count; n++) {
        linemask = pgc_draw_line(pgc, x[n - 1] << 16, y[n - 1] << 16,
                                 x[n] << 16, y[n] << 16, linemask);
    }
}

/*
 * Blit a single row of pixels from one location to another.
 *
 * To avoid difficulties if the two overlap, read both rows
 * into memory, process them there, and write the result back.
 */
static void
blkmov_row(pgc_t *pgc, int16_t x0, int16_t x1, int16_t x2, int16_t sy, int16_t ty)
{
    uint8_t src[1024];
    uint8_t dst[1024];
    int16_t x;

    for (x = x0; x <= x1; x++) {
        src[x - x0] = pgc_read_pixel(pgc, x, sy);
        dst[x - x0] = pgc_read_pixel(pgc, x - x0 + x2, ty);
    }

    for (x = x0; x <= x1; x++)
        switch (pgc->draw_mode) {
            default:
            case 0:
                pgc_write_pixel(pgc, (x - x0 + x2), ty, src[x - x0]);
                break;

            case 1:
                pgc_write_pixel(pgc, (x - x0 + x2), ty, dst[x - x0] ^ 0xff);
                break;

            case 2:
                pgc_write_pixel(pgc, (x - x0 + x2), ty, src[x - x0] ^ dst[x - x0]);
                break;

            case 3:
                pgc_write_pixel(pgc, (x - x0 + x2), ty, src[x - x0] & dst[x - x0]);
                break;
        }
}

/*
 * BLKMOV blits a rectangular area from one location to another.
 *
 * Clipping is disabled.
 */
static void
hndl_blkmov(pgc_t *pgc)
{
    int16_t x0;
    int16_t y0;
    int16_t x1;
    int16_t y1;
    int16_t x2;
    int16_t y2;
    int16_t y;

    if (!pgc_param_word(pgc, &x0))
        return;
    if (!pgc_param_word(pgc, &y0))
        return;
    if (!pgc_param_word(pgc, &x1))
        return;
    if (!pgc_param_word(pgc, &y1))
        return;
    if (!pgc_param_word(pgc, &x2))
        return;
    if (!pgc_param_word(pgc, &y2))
        return;

    im1024_log("IM1024: BLKMOV %i,%i,%i,%i,%i,%i\n", x0, y0, x1, y1, x2, y2);

    /* Disable clipping. */
    PUSHCLIP

    /*
     * Either go down from the top, or up from the bottom,
     * depending whether areas might overlap.
     */
    if (y2 <= y0) {
        for (y = y0; y <= y1; y++)
            blkmov_row(pgc, x0, x1, x2, y, y - y0 + y2);
    } else {
        for (y = y1; y >= y0; y--)
            blkmov_row(pgc, x0, x1, x2, y, y - y0 + y2);
    }

    /* Restore clipping. */
    POPCLIP
}

/*
 * Override the PGC ELIPSE command to parse its
 * parameters as words rather than coordinates.
 */
static void
hndl_ellipse(pgc_t *pgc)
{
    int16_t x;
    int16_t y;

    if (!pgc_param_word(pgc, &x))
        return;
    if (!pgc_param_word(pgc, &y))
        return;

    im1024_log("IM1024: ELLIPSE %i,%i @ %i,%i\n",
               x, y, pgc->x >> 16, pgc->y >> 16);

    pgc_draw_ellipse(pgc, x << 16, y << 16);
}

/*
 * Override the PGC MOVE command to parse its
 * parameters as words rather than coordinates.
 */
static void
hndl_move(pgc_t *pgc)
{
    int16_t x;
    int16_t y;

    if (!pgc_param_word(pgc, &x))
        return;
    if (!pgc_param_word(pgc, &y))
        return;

    im1024_log("IM1024: MOVE %i,%i\n", x, y);

    pgc->x = x << 16;
    pgc->y = y << 16;
}

/*
 * Override the PGC DRAW command to parse its
 * parameters as words rather than coordinates.
 */
static void
hndl_draw(pgc_t *pgc)
{
    int16_t x;
    int16_t y;

    if (!pgc_param_word(pgc, &x))
        return;
    if (!pgc_param_word(pgc, &y))
        return;

    im1024_log("IM1024: DRAW %i,%i to %i,%i\n", pgc->x >> 16, pgc->y >> 16, x, y);

    pgc_draw_line(pgc, pgc->x, pgc->y, x << 16, y << 16, pgc->line_pattern);

    pgc->x = x << 16;
    pgc->y = y << 16;
}

/*
 * Override the PGC POLY command to parse its
 * parameters as words rather than coordinates.
 */
static void
hndl_poly(pgc_t *pgc)
{
    int32_t *x;
    int32_t *y;
    int32_t *nx;
    int32_t *ny;
    int16_t  xw;
    int16_t  yw;
    int16_t  mask;
    unsigned realcount = 0;
    unsigned n;
    unsigned as = 256;
    int      parsing = 1;
    uint8_t  count;

    x = (int32_t *) malloc(as * sizeof(int32_t));
    y = (int32_t *) malloc(as * sizeof(int32_t));
    if (!x || !y) {
#ifdef ENABLE_IM1024_LOG
        im1024_log("IM1024: POLY: out of memory\n");
#endif
        if (x)
            free(x);
        if (y)
            free(y);
        return;
    }

    while (parsing) {
        if (!pgc_param_byte(pgc, &count)) {
            if (x)
                free(x);
            if (y)
                free(y);
            return;
        }

        if (count + realcount >= as) {
            nx = (int32_t *) realloc(x, 2 * as * sizeof(int32_t));
            ny = (int32_t *) realloc(y, 2 * as * sizeof(int32_t));
            if (!x || !y) {
#ifdef ENABLE_IM1024_LOG
                im1024_log("IM1024: poly: realloc failed\n");
#endif
                break;
            }
            x = nx;
            y = ny;
            as *= 2;
        }

        for (n = 0; n < count; n++) {
            if (!pgc_param_word(pgc, &xw)) {
                if (x)
                    free(x);
                if (y)
                    free(y);
                return;
            }
            if (!pgc_param_word(pgc, &yw)) {
                if (x)
                    free(x);
                if (y)
                    free(y);
                return;
            }

            /* Skip degenerate line segments. */
            if (realcount > 0 && (xw << 16) == x[realcount - 1] && (yw << 16) == y[realcount - 1])
                continue;

            x[realcount] = xw << 16;
            y[realcount] = yw << 16;
            realcount++;
        }

        /*
         * If we are in a command list, peek ahead to see if the next
         * command is also POLY. If so, that's a continuation of this
         * polygon!
         */
        parsing = 0;
        if (pgc->clcur && (pgc->clcur->rdptr + 1) < pgc->clcur->wrptr && pgc->clcur->list[pgc->clcur->rdptr] == 0x30) {
#ifdef ENABLE_IM1024_LOG
            im1024_log("IM1024: POLY continues!\n");
#endif
            parsing = 1;

            /* Swallow the POLY. */
            pgc->clcur->rdptr++;
        }
    }

    im1024_log("IM1024: POLY (%i) fill_mode=%i\n", realcount, pgc->fill_mode);
#ifdef ENABLE_IM1024_LOG
    for (n = 0; n < realcount; n++) {
        im1024_log("     (%i,%i)\n", x[n] >> 16, y[n] >> 16);
    }
#endif

    if (pgc->fill_mode)
        pgc_fill_polygon(pgc, realcount, x, y);

    /* Now draw borders. */
    mask = pgc->line_pattern;
    for (n = 1; n < realcount; n++)
        mask = pgc_draw_line(pgc, x[n - 1], y[n - 1], x[n], y[n], mask);
    pgc_draw_line(pgc, x[realcount - 1], y[realcount - 1], x[0], y[0], mask);

    free(y);
    free(x);
}

static int
parse_poly(pgc_t *pgc, pgc_cl_t *cl, UNUSED(int c))
{
    uint8_t count;

#ifdef ENABLE_IM1024_LOG
    im1024_log("IM1024: parse_poly\n");
#endif

    if (!pgc_param_byte(pgc, &count))
        return 0;

    im1024_log("IM1024: parse_poly: count=%02x\n", count);
    if (!pgc_cl_append(cl, count)) {
        pgc_error(pgc, PGC_ERROR_OVERFLOW);
        return 0;
    }

    im1024_log("IM1024: parse_poly: parse %i words\n", 2 * count);

    return pgc_parse_words(pgc, cl, count * 2);
}

/*
 * Override the PGC RECT command to parse its
 * parameters as words rather than coordinates.
 */
static void
hndl_rect(pgc_t *pgc)
{
    int16_t x0;
    int16_t y0;
    int16_t x1;
    int16_t y1;
    int16_t p;
    int16_t q;

    x0 = pgc->x >> 16;
    y0 = pgc->y >> 16;

    if (!pgc_param_word(pgc, &x1))
        return;
    if (!pgc_param_word(pgc, &y1))
        return;

    /* Convert to raster coords. */
    pgc_sto_raster(pgc, &x0, &y0);
    pgc_sto_raster(pgc, &x1, &y1);

    if (x0 > x1) {
        p  = x0;
        x0 = x1;
        x1 = p;
    }
    if (y0 > y1) {
        q  = y0;
        y0 = y1;
        y1 = q;
    }
    im1024_log("IM1024: RECT (%i,%i) -> (%i,%i)\n", x0, y0, x1, y1);

    if (pgc->fill_mode) {
        for (p = y0; p <= y1; p++)
            pgc_fill_line_r(pgc, x0, x1, p);
    } else {
        /* Outline: 4 lines. */
        p = pgc->line_pattern;
        p = pgc_draw_line_r(pgc, x0, y0, x1, y0, p);
        p = pgc_draw_line_r(pgc, x1, y0, x1, y1, p);
        p = pgc_draw_line_r(pgc, x1, y1, x0, y1, p);
        p = pgc_draw_line_r(pgc, x0, y1, x0, y0, p);
    }
}

/*
 * FIXME:
 * Define a font character.
 *
 * Text drawing should probably be implemented in
 * vid_pgc.c rather than here..
 */
static void
hndl_tdefin(pgc_t *pgc)
{
    im1024_t *dev = (im1024_t *) pgc;
    uint8_t   ch;
    uint8_t   bt;
    uint8_t   rows;
    uint8_t   cols;
    unsigned  len;

    if (!pgc_param_byte(pgc, &ch))
        return;
    if (!pgc_param_byte(pgc, &cols))
        return;
    if (!pgc_param_byte(pgc, &rows))
        return;

    im1024_log("IM1024: TDEFIN (%i,%i,%i) 0x%02x 0x%02x\n",
               ch, rows, cols, pgc->mapram[0x300], pgc->mapram[0x301]);

    len = ((cols + 7) / 8) * rows;
    for (unsigned int n = 0; n < len; n++) {
        if (!pgc_param_byte(pgc, &bt))
            return;

        if (n < sizeof(dev->font[ch]))
            dev->font[ch][n] = bt;
    }

    dev->fontx[ch] = cols;
    dev->fonty[ch] = rows;
}

static void
hndl_tsize(pgc_t *pgc)
{
    int16_t size;

    if (!pgc_param_word(pgc, &size))
        return;
    im1024_log("IM1024: TSIZE(%i)\n", size);

    pgc->tsize = size << 16;
}

static void
hndl_twrite(pgc_t *pgc)
{
    uint8_t         buf[256];
    const im1024_t *dev = (im1024_t *) pgc;
    uint8_t         count;
    uint8_t         mask;
    const uint8_t  *row;
    int             wb;
    int             n;
    int16_t         x0 = pgc->x >> 16;
    int16_t         y0 = pgc->y >> 16;

    if (!pgc_param_byte(pgc, &count))
        return;

    for (n = 0; n < count; n++)
        if (!pgc_param_byte(pgc, &buf[n]))
            return;
    buf[count] = 0;

    pgc_sto_raster(pgc, &x0, &y0);

    im1024_log("IM1024: TWRITE (%i) x0=%i y0=%i\n", count, x0, y0);

    for (n = 0; n < count; n++) {
        wb = (dev->fontx[buf[n]] + 7) / 8;
        im1024_log("IM1024: ch=0x%02x w=%i h=%i wb=%i\n",
                   buf[n], dev->fontx[buf[n]], dev->fonty[buf[n]], wb);

        for (uint8_t y = 0; y < dev->fonty[buf[n]]; y++) {
            mask = 0x80;
            row  = &dev->font[buf[n]][y * wb];
            for (uint8_t x = 0; x < dev->fontx[buf[n]]; x++) {
                if (row[0] & mask)
                    pgc_plot(pgc, x + x0, y0 - y);
                mask = mask >> 1;
                if (mask == 0) {
                    mask = 0x80;
                    row++;
                }
            }
        }

        x0 += dev->fontx[buf[n]];
    }
}

static void
hndl_txt88(pgc_t *pgc)
{
    uint8_t        buf[256];
    uint8_t        count;
    uint8_t        mask;
    const uint8_t *row;
    int16_t        x0 = pgc->x >> 16;
    int16_t        y0 = pgc->y >> 16;
    unsigned int   n;

    if (!pgc_param_byte(pgc, &count))
        return;

    for (n = 0; n < count; n++)
        if (!pgc_param_byte(pgc, &buf[n]))
            return;
    buf[count] = 0;

    pgc_sto_raster(pgc, &x0, &y0);

    im1024_log("IM204: TXT88 (%i) x0=%i y0=%i\n", count, x0, y0);

    for (n = 0; n < count; n++) {
        im1024_log("ch=0x%02x w=12 h=18\n", buf[n]);

        for (uint8_t y = 0; y < 18; y++) {
            mask = 0x80;
            row  = &fontdat12x18[buf[n]][y * 2];
            for (uint8_t x = 0; x < 12; x++) {
                if (row[0] & mask)
                    pgc_plot(pgc, x + x0, y0 - y);
                mask = mask >> 1;
                if (mask == 0) {
                    mask = 0x80;
                    row++;
                }
            }
        }

        x0 += 12;
    }
}

static void
hndl_imagew(pgc_t *pgc)
{
    int16_t vp_x1;
    int16_t vp_y1;
    int16_t vp_x2;
    int16_t vp_y2;
    int16_t row1;
    int16_t col1;
    int16_t col2;
    uint8_t v1;
    uint8_t v2;

    if (!pgc_param_word(pgc, &row1))
        return;
    if (!pgc_param_word(pgc, &col1))
        return;
    if (!pgc_param_word(pgc, &col2))
        return;

    /* Already using raster coordinates, no need to convert. */
    im1024_log("IM1024: IMAGEW (row=%i,col1=%i,col2=%i)\n", row1, col1, col2);

    vp_x1 = pgc->vp_x1;
    vp_y1 = pgc->vp_y1;
    vp_x2 = pgc->vp_x2;
    vp_y2 = pgc->vp_y2;

    /* Disable clipping. */
    pgc->vp_x1 = 0;
    pgc->vp_y1 = 0;
    pgc->vp_x2 = pgc->maxw - 1;
    pgc->vp_y2 = pgc->maxh - 1;

    /* In ASCII mode, what is written is a stream of bytes. */
    if (pgc->ascii_mode) {
        while (col1 <= col2) {
            if (!pgc_param_byte(pgc, &v1))
                return;

            pgc_write_pixel(pgc, col1, row1, v1);
            col1++;
        }
    } else {
        /* In hex mode, it's RLE compressed. */
        while (col1 <= col2) {
            if (!pgc_param_byte(pgc, &v1))
                return;

            if (v1 & 0x80) {
                /* Literal run. */
                v1 -= 0x7f;
                while (col1 <= col2 && v1 != 0) {
                    if (!pgc_param_byte(pgc, &v2))
                        return;
                    pgc_write_pixel(pgc, col1, row1, v2);
                    col1++;
                    v1--;
                }
            } else {
                /* Repeated run. */
                if (!pgc_param_byte(pgc, &v2))
                    return;

                v1++;
                while (col1 <= col2 && v1 != 0) {
                    pgc_write_pixel(pgc, col1, row1, v2);
                    col1++;
                    v1--;
                }
            }
        }
    }

    /* Restore clipping. */
    pgc->vp_x1 = vp_x1;
    pgc->vp_y1 = vp_y1;
    pgc->vp_x2 = vp_x2;
    pgc->vp_y2 = vp_y2;
}

/*
 * I have called this command DOT - I don't know its proper name.
 *
 * Draws a single pixel at the current location.
 */
static void
hndl_dot(pgc_t *pgc)
{
    int16_t x = pgc->x >> 16;
    int16_t y = pgc->y >> 16;

    pgc_sto_raster(pgc, &x, &y);

    im1024_log("IM1024: DOT @ %i,%i ink=%i mode=%i\n",
               x, y, pgc->color, pgc->draw_mode);

    pgc_plot(pgc, x, y);
}

/*
 * This command (which I have called IMAGEX, since I don't know its real
 * name) is a screen-to-memory blit. It reads a rectangle of bytes, rather
 * than the single row read by IMAGER, and does not attempt to compress
 * the result.
 */
static void
hndl_imagex(pgc_t *pgc)
{
    int16_t x0;
    int16_t x1;
    int16_t y0;
    int16_t y1;

    if (!pgc_param_word(pgc, &x0))
        return;
    if (!pgc_param_word(pgc, &y0))
        return;
    if (!pgc_param_word(pgc, &x1))
        return;
    if (!pgc_param_word(pgc, &y1))
        return;

    /* Already using raster coordinates, no need to convert. */
    im1024_log("IM1024: IMAGEX (%i,%i,%i,%i)\n", x0, y0, x1, y1);

    for (int16_t p = y0; p <= y1; p++) {
        for (int16_t q = x0; q <= x1; q++) {
            if (!pgc_result_byte(pgc, pgc_read_pixel(pgc, q, p)))
                return;
        }
    }
}

/*
 * Commands implemented by the IM-1024.
 *
 * TODO: A lot of commands need commandlist parsers.
 * TODO: The IM-1024 has a lot more commands that are not included here
 *       (BLINK, BUTRD, COPROC, RBAND etc) because the Windows 1.03 driver
 *       does not use them.
 */
static const pgc_cmd_t im1024_commands[] = {
    {"BLKMOV",  0xdf, hndl_blkmov,     pgc_parse_words, 6},
    { "DRAW",   0x28, hndl_draw,       pgc_parse_words, 2},
    { "D",      0x28, hndl_draw,       pgc_parse_words, 2},
    { "DOT",    0x08, hndl_dot,        NULL,            0},
    { "ELIPSE", 0x39, hndl_ellipse,    pgc_parse_words, 2},
    { "EL",     0x39, hndl_ellipse,    pgc_parse_words, 2},
    { "IMAGEW", 0xd9, hndl_imagew,     NULL,            0},
    { "IMAGEX", 0xda, hndl_imagex,     NULL,            0},
    { "IMGSIZ", 0x4e, hndl_imgsiz,     NULL,            0},
    { "IPREC",  0xe4, hndl_iprec,      NULL,            0},
    { "IW",     0xd9, hndl_imagew,     NULL,            0},
    { "L8",     0xe6, pgc_hndl_lut8,   NULL,            0},
    { "LF",     0xeb, hndl_linfun,     pgc_parse_bytes, 1},
    { "LINFUN", 0xeb, hndl_linfun,     pgc_parse_bytes, 1},
    { "LUT8",   0xe6, pgc_hndl_lut8,   NULL,            0},
    { "LUT8RD", 0x53, pgc_hndl_lut8rd, NULL,            0},
    { "L8RD",   0x53, pgc_hndl_lut8rd, NULL,            0},
    { "TDEFIN", 0x84, hndl_tdefin,     NULL,            0},
    { "TD",     0x84, hndl_tdefin,     NULL,            0},
    { "TSIZE",  0x81, hndl_tsize,      NULL,            0},
    { "TS",     0x81, hndl_tsize,      NULL,            0},
    { "TWRITE", 0x8b, hndl_twrite,     NULL,            0},
    { "TXT88",  0x88, hndl_txt88,      NULL,            0},
    { "PAN",    0xb7, hndl_pan,        NULL,            0},
    { "POLY",   0x30, hndl_poly,       parse_poly,      0},
    { "P",      0x30, hndl_poly,       parse_poly,      0},
    { "PLINE",  0x36, hndl_pline,      NULL,            0},
    { "PL",     0x37, hndl_pline,      NULL,            0},
    { "MOVE",   0x10, hndl_move,       pgc_parse_words, 2},
    { "M",      0x10, hndl_move,       pgc_parse_words, 2},
    { "RECT",   0x34, hndl_rect,       NULL,            0},
    { "R",      0x34, hndl_rect,       NULL,            0},
    { "******", 0x00, NULL,            NULL,            0}
};

static void *
im1024_init(UNUSED(const device_t *info))
{
    im1024_t *dev;

    dev = (im1024_t *) malloc(sizeof(im1024_t));
    memset(dev, 0x00, sizeof(im1024_t));

    loadfont(BIOS_ROM_PATH, 9);

    dev->fifo_len   = 4096;
    dev->fifo       = (uint8_t *) malloc(dev->fifo_len);
    dev->fifo_wrptr = 0;
    dev->fifo_rdptr = 0;

    /* Create a 1024x1024 framebuffer with 1024x800 visible. */
    pgc_init(&dev->pgc, 1024, 1024, 1024, 800, input_byte, 65000000.0);

    dev->pgc.commands = im1024_commands;

    mem_mapping_set_handler(&dev->pgc.mapping,
                            im1024_read, NULL, NULL, im1024_write, NULL, NULL);

    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_im1024);

    return dev;
}

static void
im1024_close(void *priv)
{
    im1024_t *dev = (im1024_t *) priv;

    pgc_close_common(&dev->pgc);

    free(dev);
}

static int
im1024_available(void)
{
    return rom_present(BIOS_ROM_PATH);
}

static void
im1024_speed_changed(void *priv)
{
    im1024_t *dev = (im1024_t *) priv;

    pgc_speed_changed(&dev->pgc);
}

const device_t im1024_device = {
    .name          = "ImageManager 1024",
    .internal_name = "im1024",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = im1024_init,
    .close         = im1024_close,
    .reset         = NULL,
    .available     = im1024_available,
    .speed_changed = im1024_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};
