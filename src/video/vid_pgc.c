/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          This implements just enough of the Professional Graphics
 *          Controller to act as a basis for the Vermont Microsystems
 *          IM-1024.
 *
 *          PGC features implemented include:
 *          > The CGA-compatible display modes
 *          > Switching to and from native mode
 *          > Communicating with the host PC
 *
 *          Numerous features are implemented partially or not at all,
 *          such as:
 *          > 2D drawing
 *          > 3D drawing
 *          > Command lists
 *          Some of these are marked TODO.
 *
 *          The PGC has two display modes: CGA (in which it appears in
 *          the normal CGA memory and I/O ranges) and native (in which
 *          all functions are accessed through reads and writes to 1K
 *          of memory at 0xC6000).
 *
 *          The PGC's 8088 processor monitors this buffer and executes
 *          instructions left there for it. We simulate this behavior
 *          with a separate thread.
 *
 * **NOTE** This driver is not finished yet:
 *
 *          - cursor will blink at very high speed if used on a machine
 *            with clock greater than 4.77MHz. We should  "scale down"
 *            this speed, to become relative to a 4.77MHz-based system.
 *
 *          - pgc_plot() should be overloaded by clones if they support
 *            modes other than WRITE and INVERT, like the IM-1024.
 *
 *          - test it with the Windows 1.x driver?
 *
 *            This is expected to be done shortly.
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          John Elliott, <jce@seasip.info>
 *
 *          Copyright 2019 Fred N. van Kempen.
 *          Copyright 2019 John Elliott.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <ctype.h>
#include <math.h>
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
#include <86box/vid_cga.h>
#include <86box/vid_pgc.h>

#define PGC_CGA_WIDTH  640
#define PGC_CGA_HEIGHT 400

#define HWORD(u)       ((u) >> 16)
#define LWORD(u)       ((u) &0xffff)

#define WAKE_DELAY     (TIMER_USEC * 500)

static const char *pgc_err_msgs[] = {
    "Range   \r",
    "Integer \r",
    "Memory  \r",
    "Overflow\r",
    "Digit   \r",
    "Opcode  \r",
    "Running \r",
    "Stack   \r",
    "Too long\r",
    "Area    \r",
    "Missing \r",
    "Unknown \r"
};

/* Initial palettes */
static const uint32_t init_palette[6][256] = {
#include <86box/vid_pgc_palette.h>
};

static video_timings_t timing_pgc = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

#ifdef ENABLE_PGC_LOG
int pgc_do_log = ENABLE_PGC_LOG;

static void
pgc_log(const char *fmt, ...)
{
    va_list ap;

    if (pgc_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define pgc_log(fmt, ...)
#endif

static inline int
is_whitespace(char ch)
{
    return (ch != 0 && strchr(" \r\n\t,;()+-", ch) != NULL);
}

/*
 * Write a byte to the output buffer.
 *
 * If buffer is full will sleep until it is not. Returns 0 if
 * a PGC reset has been triggered by a write to 0xC63FF.
 */
static int
output_byte(pgc_t *dev, uint8_t val)
{
    /* If output buffer full, wait for it to empty. */
    while (!dev->stopped && dev->mapram[0x302] == (uint8_t) (dev->mapram[0x303] - 1)) {
        pgc_log("PGC: output buffer state: %02x %02x  Sleeping\n",
                dev->mapram[0x302], dev->mapram[0x303]);
        dev->waiting_output_fifo = 1;
        pgc_sleep(dev);
    }

    if (dev->mapram[0x3ff]) {
        /* Reset triggered. */
        pgc_reset(dev);
        return 0;
    }

    dev->mapram[0x100 + dev->mapram[0x302]] = val;
    dev->mapram[0x302]++;

    pgc_log("PGC: output %02x: new state: %02x %02x\n", val,
            dev->mapram[0x302], dev->mapram[0x303]);

    return 1;
}

/* Helper to write an entire string to the output buffer. */
static int
output_string(pgc_t *dev, const char *s)
{
    while (*s) {
        if (!output_byte(dev, *s))
            return 0;
        s++;
    }

    return 1;
}

/* As output_byte, for the error buffer. */
static int
error_byte(pgc_t *dev, uint8_t val)
{
    /* If error buffer full, wait for it to empty. */
    while (!dev->stopped && dev->mapram[0x304] == dev->mapram[0x305] - 1) {
        dev->waiting_error_fifo = 1;
        pgc_sleep(dev);
    }

    if (dev->mapram[0x3ff]) {
        /* Reset triggered. */
        pgc_reset(dev);
        return 0;
    }

    dev->mapram[0x200 + dev->mapram[0x304]] = val;
    dev->mapram[0x304]++;

    return 1;
}

/* As output_string, for the error buffer. */
static int
error_string(pgc_t *dev, const char *s)
{
    while (*s) {
        if (!error_byte(dev, *s))
            return 0;
        s++;
    }

    return 1;
}

/*
 * Read next byte from the input buffer.
 *
 * If no byte available will sleep until one is. Returns 0 if
 * a PGC reset has been triggered by a write to 0xC63FF.
 */
static int
input_byte(pgc_t *dev, uint8_t *result)
{
    /* If input buffer empty, wait for it to fill. */
    while (!dev->stopped && (dev->mapram[0x300] == dev->mapram[0x301])) {
        dev->waiting_input_fifo = 1;
        pgc_sleep(dev);
    }

    if (dev->stopped)
        return 0;

    if (dev->mapram[0x3ff]) {
        /* Reset triggered. */
        pgc_reset(dev);
        return 0;
    }

    *result = dev->mapram[dev->mapram[0x301]];
    dev->mapram[0x301]++;

    return 1;
}

/*
 * Read a byte and interpret as ASCII.
 *
 * Ignore control characters other than CR, LF or tab.
 */
static int
input_char(pgc_t *dev, char *result)
{
    uint8_t ch;

    while (1) {
        if (!dev->inputbyte(dev, &ch))
            return 0;

        ch &= 0x7f;
        if (ch == '\r' || ch == '\n' || ch == '\t' || ch >= ' ') {
            *result = toupper(ch);
            return 1;
        }
    }
}

/*
 * Read in the next command.
 *
 * This can be either as hex (1 byte) or ASCII (up to 6 characters).
 */
static int
read_command(pgc_t *dev)
{
    if (dev->stopped)
        return 0;

    if (dev->clcur)
        return pgc_clist_byte(dev, &dev->hex_command);

    if (dev->ascii_mode) {
        char ch;
        int  count = 0;

        while (count < 7) {
            if (dev->stopped)
                return 0;

            if (!input_char(dev, &ch))
                return 0;

            if (is_whitespace(ch)) {
                /* Pad to 6 characters */
                while (count < 6)
                    dev->asc_command[count++] = ' ';
                dev->asc_command[6] = 0;

                return 1;
            }
            dev->asc_command[count++] = toupper(ch);
        }

        return 1;
    }

    return dev->inputbyte(dev, &dev->hex_command);
}

/* Read in the next command and parse it. */
static int
parse_command(pgc_t *dev, const pgc_cmd_t **pcmd)
{
    char             match[7];

    *pcmd            = NULL;
    dev->hex_command = 0;
    memset(dev->asc_command, ' ', 6);
    dev->asc_command[6] = 0;

    if (!read_command(dev)) {
        /* PGC has been reset. */
        return 0;
    }

    /*
     * Scan the list of valid commands.
     *
     * dev->commands may be a subclass list (terminated with '*')
     * or the core list (terminated with '@')
     */
    for (const pgc_cmd_t *cmd = dev->commands; cmd->ascii[0] != '@'; cmd++) {
        /* End of subclass command list, chain to core. */
        if (cmd->ascii[0] == '*')
            cmd = dev->master;

        /* If in ASCII mode match on the ASCII command. */
        if (dev->ascii_mode && !dev->clcur) {
            sprintf(match, "%-6.6s", cmd->ascii);
            if (!strncmp(match, dev->asc_command, 6)) {
                *pcmd            = cmd;
                dev->hex_command = cmd->hex;
                break;
            }
        } else {
            /* Otherwise match on the hex command. */
            if (cmd->hex == dev->hex_command) {
                sprintf(dev->asc_command, "%-6.6s", cmd->ascii);
                *pcmd = cmd;
                break;
            }
        }
    }

    return 1;
}

/*
 * Beginning of a command list.
 *
 * Parse commands up to the next CLEND, storing
 * them (in hex form) in the named command list.
 */
static void
hndl_clbeg(pgc_t *dev)
{
    const pgc_cmd_t *cmd;
    uint8_t          param = 0;
    pgc_cl_t         cl;

    if (!pgc_param_byte(dev, &param))
        return;
    pgc_log("PGC: CLBEG(%i)\n", param);

    memset(&cl, 0x00, sizeof(pgc_cl_t));

    while (1) {
        if (!parse_command(dev, &cmd)) {
            /* PGC has been reset. */
            return;
        }
        if (!cmd) {
            pgc_error(dev, PGC_ERROR_OPCODE);
            return;
        } else if (dev->hex_command == 0x71) {
            /* CLEND */
            dev->clist[param] = cl;
            return;
        } else {
            if (!pgc_cl_append(&cl, dev->hex_command)) {
                pgc_error(dev, PGC_ERROR_OVERFLOW);
                return;
            }

            if (cmd->parser) {
                if (!(*cmd->parser)(dev, &cl, cmd->p))
                    return;
            }
        }
    }
}

static void
hndl_clend(UNUSED(pgc_t *dev))
{
    /* Should not happen outside a CLBEG. */
}

/*
 * Execute a command list.
 *
 * If one was already executing, remember
 * it so we can return to it afterwards.
 */
static void
hndl_clrun(pgc_t *dev)
{
    pgc_cl_t *clprev = dev->clcur;
    uint8_t   param  = 0;

    if (!pgc_param_byte(dev, &param))
        return;

    dev->clcur         = &dev->clist[param];
    dev->clcur->rdptr  = 0;
    dev->clcur->repeat = 1;
    dev->clcur->chain  = clprev;
}

/* Execute a command list multiple times. */
static void
hndl_cloop(pgc_t *dev)
{
    pgc_cl_t *clprev = dev->clcur;
    uint8_t   param  = 0;
    int16_t   repeat = 0;

    if (!pgc_param_byte(dev, &param))
        return;
    if (!pgc_param_word(dev, &repeat))
        return;

    dev->clcur         = &dev->clist[param];
    dev->clcur->rdptr  = 0;
    dev->clcur->repeat = repeat;
    dev->clcur->chain  = clprev;
}

/* Read back a command list. */
static void
hndl_clread(pgc_t *dev)
{
    uint8_t  param = 0;

    if (!pgc_param_byte(dev, &param))
        return;

    for (uint32_t n = 0; n < dev->clist[param].wrptr; n++) {
        if (!pgc_result_byte(dev, dev->clist[param].list[n]))
            return;
    }
}

/* Delete a command list. */
static void
hndl_cldel(pgc_t *dev)
{
    uint8_t param = 0;

    if (!pgc_param_byte(dev, &param))
        return;

    memset(&dev->clist[param], 0, sizeof(pgc_cl_t));
}

/* Clear the screen to a specified color. */
static void
hndl_clears(pgc_t *dev)
{
    uint8_t  param = 0;

    if (!pgc_param_byte(dev, &param))
        return;

    for (uint32_t y = 0; y < dev->screenh; y++)
        memset(dev->vram + y * dev->maxw, param, dev->screenw);
}

/* Select drawing color. */
static void
hndl_color(pgc_t *dev)
{
    uint8_t param = 0;

    if (!pgc_param_byte(dev, &param))
        return;

    pgc_log("PGC: COLOR(%i)\n", param);
    dev->color = param;
}

/*
 * Set drawing mode.
 *
 * 0 => Draw
 * 1 => Invert
 */
static void
hndl_linfun(pgc_t *dev)
{
    uint8_t param = 0;

    if (!pgc_param_byte(dev, &param))
        return;

    pgc_log("PGC: LINFUN(%i)\n", param);
    if (param < 2)
        dev->draw_mode = param;
    else
        pgc_error(dev, PGC_ERROR_RANGE);
}

/* Set the line drawing pattern. */
static void
hndl_linpat(pgc_t *dev)
{
    uint16_t param = 0;

    if (!pgc_param_word(dev, (int16_t *) &param))
        return;

    pgc_log("PGC: LINPAT(0x%04x)\n", param);
    dev->line_pattern = param;
}

/* Set the polygon fill mode (0=hollow, 1=filled, 2=fast fill). */
static void
hndl_prmfil(pgc_t *dev)
{
    uint8_t param = 0;

    if (!pgc_param_byte(dev, &param))
        return;

    pgc_log("PGC: PRMFIL(%i)\n", param);
    if (param < 3)
        dev->fill_mode = param;
    else
        pgc_error(dev, PGC_ERROR_RANGE);
}

/* Set the 2D drawing position. */
static void
hndl_move(pgc_t *dev)
{
    int32_t x = 0;
    int32_t y = 0;

    if (!pgc_param_coord(dev, &x))
        return;
    if (!pgc_param_coord(dev, &y))
        return;

    pgc_log("PCG: MOVE %x.%04x,%x.%04x\n",
            HWORD(x), LWORD(x), HWORD(y), LWORD(y));
    dev->x = x;
    dev->y = y;
}

/* Set the 3D drawing position. */
static void
hndl_move3(pgc_t *dev)
{
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    if (!pgc_param_coord(dev, &x))
        return;
    if (!pgc_param_coord(dev, &y))
        return;
    if (!pgc_param_coord(dev, &z))
        return;

    dev->x = x;
    dev->y = y;
    dev->z = z;
}

/* Relative move (2D). */
static void
hndl_mover(pgc_t *dev)
{
    int32_t x = 0;
    int32_t y = 0;

    if (!pgc_param_coord(dev, &x))
        return;
    if (!pgc_param_coord(dev, &y))
        return;

    dev->x += x;
    dev->y += y;
}

/* Relative move (3D). */
static void
hndl_mover3(pgc_t *dev)
{
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    if (!pgc_param_coord(dev, &x))
        return;
    if (!pgc_param_coord(dev, &y))
        return;
    if (!pgc_param_coord(dev, &z))
        return;

    dev->x += x;
    dev->y += y;
    dev->z += z;
}

/* Given raster coordinates, find the matching address in PGC video RAM. */
uint8_t *
pgc_vram_addr(pgc_t *dev, int16_t x, int16_t y)
{
    int offset;

    /* We work from the bottom left-hand corner. */
    if (y < 0 || (uint32_t) y >= dev->maxh || x < 0 || (uint32_t) x >= dev->maxw)
        return NULL;

    offset = (dev->maxh - 1 - y) * (dev->maxw) + x;
    pgc_log("PGC: vram_addr(x=%i,y=%i) = %i\n", x, y, offset);

    if (offset < 0 || (uint32_t) offset >= (dev->maxw * dev->maxh))
        return NULL;

    return &dev->vram[offset];
}

/*
 * Write a screen pixel.
 * X and Y are raster coordinates, ink is the value to write.
 */
void
pgc_write_pixel(pgc_t *dev, uint16_t x, uint16_t y, uint8_t ink)
{
    uint8_t *vram;

    /* Suppress out-of-range writes; clip to viewport. */
    if (x < dev->vp_x1 || x > dev->vp_x2 || x >= dev->maxw || y < dev->vp_y1 || y > dev->vp_y2 || y >= dev->maxh) {
        pgc_log("PGC: write_pixel clipped: (%i,%i) "
                "vp_x1=%i vp_y1=%i vp_x2=%i vp_y2=%i "
                "ink=0x%02x\n",
                x, y, dev->vp_x1, dev->vp_y1, dev->vp_x2, dev->vp_y2, ink);
        return;
    }

    vram = pgc_vram_addr(dev, x, y);
    if (vram)
        *vram = ink;
}

/* Read a screen pixel (x and y are raster coordinates). */
uint8_t
pgc_read_pixel(pgc_t *dev, uint16_t x, uint16_t y)
{
    const uint8_t *vram;

    /* Suppress out-of-range reads. */
    if (x >= dev->maxw || y >= dev->maxh)
        return 0;

    vram = pgc_vram_addr(dev, x, y);
    if (vram)
        return *vram;

    return 0;
}

/*
 * Plot a point in the current color and draw mode. Raster coordinates.
 *
 * FIXME: this should be overloaded by clones if they support
 *        modes other than WRITE and INVERT, like the IM-1024.
 */
void
pgc_plot(pgc_t *dev, uint16_t x, uint16_t y)
{
    uint8_t *vram;

    /* Only allow plotting within the current viewport. */
    if (x < dev->vp_x1 || x > dev->vp_x2 || x >= dev->maxw || y < dev->vp_y1 || y > dev->vp_y2 || y >= dev->maxh) {
        pgc_log("PGC: plot clipped: (%i,%i) %i <= x <= %i; %i <= y <= %i; "
                "mode=%i ink=0x%02x\n",
                x, y,
                dev->vp_x1, dev->vp_x2, dev->vp_y1, dev->vp_y2,
                dev->draw_mode, dev->color);
        return;
    }

    vram = pgc_vram_addr(dev, x, y);
    if (!vram)
        return;

    /* TODO: Does not implement the PGC plane mask (set by MASK). */
    switch (dev->draw_mode) {
        default:
        case 0: /* WRITE */
            *vram = dev->color;
            break;

        case 1: /* INVERT */
            *vram ^= 0xff;
            break;

        case 2: /* XOR color */
            // FIXME: see notes
            *vram ^= dev->color;
            break;

        case 3: /* AND color */
            // FIXME: see notes
            *vram &= dev->color;
            break;
    }
}

/*
 * Draw a line (using raster coordinates).
 *
 * Bresenham's Algorithm from:
 *  <https://rosettacode.org/wiki/Bitmap/Bresenham%27s_line_algorithm#C>
 *
 * The line pattern mask to use is passed in. Return value is the
 * line pattern mask, rotated by the number of points drawn.
 */
uint16_t
pgc_draw_line_r(pgc_t *dev, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t linemask)
{
    int32_t dx;
    int32_t dy;
    int32_t sx;
    int32_t sy;
    int32_t err;
    int32_t e2;

    dx  = abs(x1 - x0);
    dy  = abs(y1 - y0);
    sx  = (x0 < x1) ? 1 : -1;
    sy  = (y0 < y1) ? 1 : -1;
    err = (dx > dy ? dx : -dy) / 2;

    for (;;) {
        if (linemask & 0x8000) {
            pgc_plot(dev, x0, y0);
            linemask = (linemask << 1) | 1;
        } else
            linemask = (linemask << 1);

        if (x0 == x1 && y0 == y1)
            break;

        e2 = err;
        if (e2 > -dx) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dy) {
            err += dx;
            y0 += sy;
        }
    }

    return linemask;
}

/* Draw a line (using PGC fixed-point coordinates). */
uint16_t
pgc_draw_line(pgc_t *dev, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t linemask)
{
    pgc_log("pgc_draw_line: (%i,%i) to (%i,%i)\n",
            x0 >> 16, y0 >> 16, x1 >> 16, y1 >> 16);

    /* Convert from PGC fixed-point to device coordinates */
    x0 >>= 16;
    y0 >>= 16;
    pgc_ito_raster(dev, &x0, &y0);

    x1 >>= 16;
    y1 >>= 16;
    pgc_ito_raster(dev, &x1, &y1);

    return pgc_draw_line_r(dev, x0, y0, x1, y1, linemask);
}

/*
 * Draw a horizontal line in the current fill pattern
 * (using raster coordinates).
 */
void
pgc_fill_line_r(pgc_t *dev, int32_t x0, int32_t x1, int32_t y0)
{
    int32_t mask = 0x8000 >> (x0 & 0x0f);
    int32_t x;

    if (x0 > x1) {
        x  = x1;
        x1 = x0;
        x0 = x;
    }

    for (x = x0; x <= x1; x++) {
        if (dev->fill_pattern[y0 & 0x0F] & mask)
            pgc_plot(dev, x, y0);
        mask = mask >> 1;
        if (mask == 0)
            mask = 0x8000;
    }
}

/* For sorting polygon nodes. */
static int
compare_double(const void *a, const void *b)
{
    const double *da = (const double *) a;
    const double *db = (const double *) b;

    if (*da < *db)
        return 1;
    if (*da > *db)
        return -1;

    return 0;
}

/* Draw a filled polygon (using PGC fixed-point coordinates). */
void
pgc_fill_polygon(pgc_t *dev, unsigned corners, int32_t *x, int32_t *y)
{
    double  *nodex;
    double  *dx;
    double  *dy;
    unsigned nodes;
    unsigned i;
    unsigned j;
    double   ymin;
    double   ymax;

    pgc_log("PGC: fill_polygon(%i corners)\n", corners);

    if (!x || !y || (corners < 2))
        return; /* Degenerate polygon */

    nodex = (double *) malloc(corners * sizeof(double));
    dx    = (double *) malloc(corners * sizeof(double));
    dy    = (double *) malloc(corners * sizeof(double));
    if (!nodex || !dx || !dy) {
        if (nodex) {
            free(nodex);
            nodex = NULL;
        }
        if (dx) {
            free(dx);
            dx = NULL;
        }
        if (dy) {
            free(dy);
            dy = NULL;
        }
        return;
    }

    ymin = ymax = y[0] / 65536.0;
    for (unsigned int n = 0; n < corners; n++) {
        /* Convert from PGC fixed-point to native floating-point. */
        dx[n] = x[n] / 65536.0;
        dy[n] = y[n] / 65536.0;

        if (dy[n] < ymin)
            ymin = dy[n];
        if (dy[n] > ymax)
            ymax = dy[n];
    }

    /* Polygon fill. Based on <http://alienryderflex.com/polygon_fill/> */
    /* For each row, work out where the polygon lines intersect with
     * that row. */
    for (double ypos = ymin; ypos <= ymax; ypos++) {
        nodes = 0;
        j     = corners - 1;
        for (i = 0; i < corners; i++) {
            if ((dy[i] < ypos && dy[j] >= ypos) || (dy[j] < ypos && dy[i] >= ypos)) /* Line crosses */ {
                nodex[nodes++] = dx[i] + (ypos - dy[i]) / (dy[j] - dy[i]) * (dx[j] - dx[i]);
            }
            j = i;
        }

        /* Sort the intersections. */
        if (nodes)
            qsort(nodex, nodes, sizeof(double), compare_double);

        /* And fill between them. */
        for (i = 0; i < nodes; i += 2) {
            int16_t x1 = (int16_t) nodex[i];
            int16_t x2 = (int16_t) nodex[i + 1];
            int16_t y1 = (int16_t) ypos;
            int16_t y2 = (int16_t) ypos;
            pgc_sto_raster(dev, &x1, &y1);
            pgc_sto_raster(dev, &x2, &y2);
            pgc_fill_line_r(dev, x1, x2, y1);
        }
    }

    free(nodex);
    free(dx);
    free(dy);
}

/* Draw a filled ellipse (using PGC fixed-point coordinates). */
void
pgc_draw_ellipse(pgc_t *dev, int32_t x, int32_t y)
{
    /* Convert from PGC fixed-point to native floating-point. */
    double  h  = y / 65536.0;
    double  w  = x / 65536.0;
    double  y0 = dev->y / 65536.0;
    double  x0 = dev->x / 65536.0;
    double  x1;
    double  xlast    = 0.0;
    int16_t linemask = dev->line_pattern;

    pgc_log("PGC: ellipse(color=%i drawmode=%i fill=%i)\n",
            dev->color, dev->draw_mode, dev->fill_mode);

    pgc_dto_raster(dev, &x0, &y0);

    for (double ypos = 0; ypos <= h; ypos++) {
        if (ypos == 0) {
            if (dev->fill_mode)
                pgc_fill_line_r(dev, (uint16_t) (x0 - w),
                                (uint16_t) (x0 + w), (uint16_t) y0);
            if (linemask & 0x8000) {
                pgc_plot(dev, (uint16_t) (x0 + w), (uint16_t) y0);
                pgc_plot(dev, (uint16_t) (x0 - w), (uint16_t) y0);
                linemask = (linemask << 1) | 1;
            } else
                linemask = linemask << 1;

            xlast = w;
        } else {
            x1 = sqrt((h * h) - (ypos * ypos)) * w / h;

            if (dev->fill_mode) {
                pgc_fill_line_r(dev, (uint16_t) (x0 - x1),
                                (uint16_t) (x0 + x1),
                                (uint16_t) (y0 + ypos));
                pgc_fill_line_r(dev, (uint16_t) (x0 - x1),
                                (uint16_t) (x0 + x1),
                                (uint16_t) (y0 - ypos));
            }

            /* Draw border. */
            for (double xpos = xlast; xpos >= x1; xpos--) {
                if (linemask & 0x8000) {
                    pgc_plot(dev, (uint16_t) (x0 + xpos),
                             (uint16_t) (y0 + ypos));
                    pgc_plot(dev, (uint16_t) (x0 - xpos),
                             (uint16_t) (y0 + ypos));
                    pgc_plot(dev, (uint16_t) (x0 + xpos),
                             (uint16_t) (y0 - ypos));
                    pgc_plot(dev, (uint16_t) (x0 - xpos),
                             (uint16_t) (y0 - ypos));
                    linemask = (linemask << 1) | 1;
                } else
                    linemask = linemask << 1;
            }

            xlast = x1;
        }
    }
}

/* Handle the ELIPSE (sic) command. */
static void
hndl_ellipse(pgc_t *dev)
{
    int32_t x = 0;
    int32_t y = 0;

    if (!pgc_param_coord(dev, &x))
        return;
    if (!pgc_param_coord(dev, &y))
        return;

    pgc_draw_ellipse(dev, x, y);
}

/* Handle the POLY command. */
static void
hndl_poly(pgc_t *dev)
{
    uint8_t count;
    int32_t x[256];
    int32_t y[256];

    if (!pgc_param_byte(dev, &count))
        return;

    pgc_log("PGC: POLY (%i)\n", count);
    for (uint8_t n = 0; n < count; n++) {
        if (!pgc_param_coord(dev, &x[n]))
            return;
        if (!pgc_param_coord(dev, &y[n]))
            return;
    }
}

/* Parse but don't execute a POLY command (for adding to a command list) */
static int
parse_poly(pgc_t *dev, pgc_cl_t *cl, UNUSED(int c))
{
    uint8_t count;

#ifdef ENABLE_PGC_LOG
    pgc_log("PCG: parse_poly\n");
#endif
    if (!pgc_param_byte(dev, &count))
        return 0;
    pgc_log("PCG: parse_poly: count=%02x\n", count);

    if (!pgc_cl_append(cl, count)) {
        pgc_error(dev, PGC_ERROR_OVERFLOW);
        return 0;
    }
    pgc_log("PCG: parse_poly: parse %i coords\n", 2 * count);

    return pgc_parse_coords(dev, cl, 2 * count);
}

/* Handle the DISPLAY command. */
static void
hndl_display(pgc_t *dev)
{
    uint8_t param;

    if (!pgc_param_byte(dev, &param))
        return;

    pgc_log("PGC: DISPLAY(%i)\n", param);

    if (param > 1)
        pgc_error(dev, PGC_ERROR_RANGE);
    else
        pgc_setdisplay(dev, param);
}

/* Handle the IMAGEW command (memory to screen blit). */
static void
hndl_imagew(pgc_t *dev)
{
    int16_t row;
    int16_t col1;
    int16_t col2;
    uint8_t v1;
    uint8_t v2;

    if (!pgc_param_word(dev, &row))
        return;
    if (!pgc_param_word(dev, &col1))
        return;
    if (!pgc_param_word(dev, &col2))
        return;

    if ((uint32_t) row >= dev->screenh || (uint32_t) col1 >= dev->maxw || (uint32_t) col2 >= dev->maxw) {
        pgc_error(dev, PGC_ERROR_RANGE);
        return;
    }

    /* In ASCII mode, what is written is a stream of bytes. */
    if (dev->ascii_mode) {
        while (col1 <= col2) {
            if (!pgc_param_byte(dev, &v1))
                return;
            pgc_write_pixel(dev, col1, row, v1);
            col1++;
        }

        return;
    }

    /* In hex mode, it's RLE compressed. */
    while (col1 <= col2) {
        if (!pgc_param_byte(dev, &v1))
            return;

        if (v1 & 0x80) {
            /* Literal run. */
            v1 -= 0x7f;
            while (col1 <= col2 && v1 != 0) {
                if (!pgc_param_byte(dev, &v2))
                    return;
                pgc_write_pixel(dev, col1, row, v2);
                col1++;
                v1--;
            }
        } else {
            /* Repeated run. */
            if (!pgc_param_byte(dev, &v2))
                return;

            v1++;
            while (col1 <= col2 && v1 != 0) {
                pgc_write_pixel(dev, col1, row, v2);
                col1++;
                v1--;
            }
        }
    }
}

/* Select one of the built-in palettes. */
static void
init_lut(pgc_t *dev, int param)
{
    if (param >= 0 && param < 6)
        memcpy(dev->palette, init_palette[param], sizeof(dev->palette));
    else if (param == 0xff)
        memcpy(dev->palette, dev->userpal, sizeof(dev->palette));
    else
        pgc_error(dev, PGC_ERROR_RANGE);
}

/* Save the current palette. */
static void
hndl_lutsav(pgc_t *dev)
{
    memcpy(dev->userpal, dev->palette, sizeof(dev->palette));
}

/* Handle LUTINT (select palette). */
static void
hndl_lutint(pgc_t *dev)
{
    uint8_t param;

    if (!pgc_param_byte(dev, &param))
        return;

    init_lut(dev, param);
}

/* Handle LUTRD (read palette register). */
static void
hndl_lutrd(pgc_t *dev)
{
    uint8_t  param;
    uint32_t col;

    if (!pgc_param_byte(dev, &param))
        return;

    col = dev->palette[param];

    pgc_result_byte(dev, (col >> 20) & 0x0f);
    pgc_result_byte(dev, (col >> 12) & 0x0f);
    pgc_result_byte(dev, (col >> 4) & 0x0f);
}

/* Handle LUT (write palette register). */
static void
hndl_lut(pgc_t *dev)
{
    uint8_t param[4];

    for (uint8_t n = 0; n < 4; n++) {
        if (!pgc_param_byte(dev, &param[n]))
            return;
        if (n > 0 && param[n] > 15) {
            pgc_error(dev, PGC_ERROR_RANGE);
            param[n] &= 0x0f;
        }
    }

    dev->palette[param[0]] = makecol((param[1] * 0x11),
                                     (param[2] * 0x11),
                                     (param[3] * 0x11));
}

/*
 * LUT8RD and LUT8 are extensions implemented by several PGC clones,
 * so here are functions that implement them even though they aren't
 * used by the PGC.
 */
void
pgc_hndl_lut8rd(pgc_t *dev)
{
    uint8_t  param;
    uint32_t col;

    if (!pgc_param_byte(dev, &param))
        return;

    col = dev->palette[param];

    pgc_result_byte(dev, (col >> 16) & 0xff);
    pgc_result_byte(dev, (col >> 8) & 0xff);
    pgc_result_byte(dev, col & 0xff);
}

void
pgc_hndl_lut8(pgc_t *dev)
{
    uint8_t param[4];

    for (uint8_t n = 0; n < 4; n++)
        if (!pgc_param_byte(dev, &param[n]))
            return;

    dev->palette[param[0]] = makecol((param[1]), (param[2]), (param[3]));
}

/* Handle AREAPT (set 16x16 fill pattern). */
static void
hndl_areapt(pgc_t *dev)
{
    int16_t pat[16];

    for (uint8_t n = 0; n < 16; n++)
        if (!pgc_param_word(dev, &pat[n]))
            return;

    pgc_log("PGC: AREAPT(%04x %04x %04x %04x...)\n",
            pat[0] & 0xffff, pat[1] & 0xffff, pat[2] & 0xffff, pat[3] & 0xffff);

    memcpy(dev->fill_pattern, pat, sizeof(dev->fill_pattern));
}

/* Handle CA (select ASCII mode). */
static void
hndl_ca(pgc_t *dev)
{
    dev->ascii_mode = 1;
}

/* Handle CX (select hex mode). */
static void
hndl_cx(pgc_t *dev)
{
    dev->ascii_mode = 0;
}

/*
 * CA and CX remain valid in hex mode; they are handled
 * as command 0x43 ('C') with a one-byte parameter.
 */
static void
hndl_c(pgc_t *dev)
{
    uint8_t param;

    if (!dev->inputbyte(dev, &param))
        return;

    if (param == 'A')
        dev->ascii_mode = 1;

    if (param == 'X')
        dev->ascii_mode = 0;
}

/* RESETF resets the PGC. */
static void
hndl_resetf(pgc_t *dev)
{
    pgc_reset(dev);
}

/* TJUST sets text justify settings. */
static void
hndl_tjust(pgc_t *dev)
{
    uint8_t param[2];

    if (!dev->inputbyte(dev, &param[0]))
        return;
    if (!dev->inputbyte(dev, &param[1]))
        return;

    if (param[0] >= 1 && param[0] <= 3 && param[1] >= 1 && param[1] <= 3) {
        dev->tjust_h = param[0];
        dev->tjust_v = param[1];
    } else
        pgc_error(dev, PGC_ERROR_RANGE);
}

/* TSIZE controls text horizontal spacing. */
static void
hndl_tsize(pgc_t *pgc)
{
    int32_t param = 0;

    if (!pgc_param_coord(pgc, &param))
        return;

    pgc_log("PGC: TSIZE %i\n", param);
    pgc->tsize = param;
}

/*
 * VWPORT sets up the viewport (roughly, the clip rectangle) in
 * raster coordinates, measured from the bottom left of the screen.
 */
static void
hndl_vwport(pgc_t *dev)
{
    int16_t x1;
    int16_t x2;
    int16_t y1;
    int16_t y2;

    if (!pgc_param_word(dev, &x1))
        return;
    if (!pgc_param_word(dev, &x2))
        return;
    if (!pgc_param_word(dev, &y1))
        return;
    if (!pgc_param_word(dev, &y2))
        return;

    pgc_log("PGC: VWPORT %i,%i,%i,%i\n", x1, x2, y1, y2);
    dev->vp_x1 = x1;
    dev->vp_x2 = x2;
    dev->vp_y1 = y1;
    dev->vp_y2 = y2;
}

/* WINDOW defines the coordinate system in use. */
static void
hndl_window(pgc_t *dev)
{
    int16_t x1;
    int16_t x2;
    int16_t y1;
    int16_t y2;

    if (!pgc_param_word(dev, &x1))
        return;
    if (!pgc_param_word(dev, &x2))
        return;
    if (!pgc_param_word(dev, &y1))
        return;
    if (!pgc_param_word(dev, &y2))
        return;

    pgc_log("PGC: WINDOW %i,%i,%i,%i\n", x1, x2, y1, y2);
    dev->win_x1 = x1;
    dev->win_x2 = x2;
    dev->win_y1 = y1;
    dev->win_y2 = y2;
}

/*
 * The list of commands implemented by this mini-PGC.
 *
 * In order to support the original PGC and clones, we support two lists;
 * core commands (listed below) and subclass commands (listed in the clone).
 *
 * Each row has five parameters:
 *  ASCII-mode command
 *  Hex-mode command
 *  Function that executes this command
 *  Function that parses this command when building a command list
 *  Parameter for the parse function
 *
 * TODO: This list omits numerous commands present in a genuine PGC
 *       (ARC, AREA, AREABC, BUFFER, CIRCLE etc etc).
 * TODO: Some commands don't have a parse function (for example, IMAGEW)
 *
 * The following ASCII entries have special meaning:
 * ~~~~~~  command is valid only in hex mode
 * ******  end of subclass command list, now process core command list
 * @@@@@@  end of core command list
 *
 */
static const pgc_cmd_t pgc_commands[] = {
    {"AREAPT",  0xe7, hndl_areapt,  pgc_parse_words,  16},
    { "AP",     0xe7, hndl_areapt,  pgc_parse_words,  16},
    { "~~~~~~", 0x43, hndl_c,       NULL,             0 },
    { "CA",     0xd2, hndl_ca,      NULL,             0 },
    { "CLBEG",  0x70, hndl_clbeg,   NULL,             0 },
    { "CB",     0x70, hndl_clbeg,   NULL,             0 },
    { "CLDEL",  0x74, hndl_cldel,   pgc_parse_bytes,  1 },
    { "CD",     0x74, hndl_cldel,   pgc_parse_bytes,  1 },
    { "CLEND",  0x71, hndl_clend,   NULL,             0 },
    { "CLRUN",  0x72, hndl_clrun,   pgc_parse_bytes,  1 },
    { "CR",     0x72, hndl_clrun,   pgc_parse_bytes,  1 },
    { "CLRD",   0x75, hndl_clread,  pgc_parse_bytes,  1 },
    { "CRD",    0x75, hndl_clread,  pgc_parse_bytes,  1 },
    { "CLOOP",  0x73, hndl_cloop,   NULL,             0 },
    { "CL",     0x73, hndl_cloop,   NULL,             0 },
    { "CLEARS", 0x0f, hndl_clears,  pgc_parse_bytes,  1 },
    { "CLS",    0x0f, hndl_clears,  pgc_parse_bytes,  1 },
    { "COLOR",  0x06, hndl_color,   pgc_parse_bytes,  1 },
    { "C",      0x06, hndl_color,   pgc_parse_bytes,  1 },
    { "CX",     0xd1, hndl_cx,      NULL,             0 },
    { "DISPLA", 0xd0, hndl_display, pgc_parse_bytes,  1 },
    { "DI",     0xd0, hndl_display, pgc_parse_bytes,  1 },
    { "ELIPSE", 0x39, hndl_ellipse, pgc_parse_coords, 2 },
    { "EL",     0x39, hndl_ellipse, pgc_parse_coords, 2 },
    { "IMAGEW", 0xd9, hndl_imagew,  NULL,             0 },
    { "IW",     0xd9, hndl_imagew,  NULL,             0 },
    { "LINFUN", 0xeb, hndl_linfun,  pgc_parse_bytes,  1 },
    { "LF",     0xeb, hndl_linfun,  pgc_parse_bytes,  1 },
    { "LINPAT", 0xea, hndl_linpat,  pgc_parse_words,  1 },
    { "LP",     0xea, hndl_linpat,  pgc_parse_words,  1 },
    { "LUTINT", 0xec, hndl_lutint,  pgc_parse_bytes,  1 },
    { "LI",     0xec, hndl_lutint,  pgc_parse_bytes,  1 },
    { "LUTRD",  0x50, hndl_lutrd,   pgc_parse_bytes,  1 },
    { "LUTSAV", 0xed, hndl_lutsav,  NULL,             0 },
    { "LUT",    0xee, hndl_lut,     pgc_parse_bytes,  4 },
    { "MOVE",   0x10, hndl_move,    pgc_parse_coords, 2 },
    { "M",      0x10, hndl_move,    pgc_parse_coords, 2 },
    { "MOVE3",  0x12, hndl_move3,   pgc_parse_coords, 3 },
    { "M3",     0x12, hndl_move3,   pgc_parse_coords, 3 },
    { "MOVER",  0x11, hndl_mover,   pgc_parse_coords, 2 },
    { "MR",     0x11, hndl_mover,   pgc_parse_coords, 2 },
    { "MOVER3", 0x13, hndl_mover3,  pgc_parse_coords, 3 },
    { "MR3",    0x13, hndl_mover3,  pgc_parse_coords, 3 },
    { "PRMFIL", 0xe9, hndl_prmfil,  pgc_parse_bytes,  1 },
    { "PF",     0xe9, hndl_prmfil,  pgc_parse_bytes,  1 },
    { "POLY",   0x30, hndl_poly,    parse_poly,       0 },
    { "P",      0x30, hndl_poly,    parse_poly,       0 },
    { "RESETF", 0x04, hndl_resetf,  NULL,             0 },
    { "RF",     0x04, hndl_resetf,  NULL,             0 },
    { "TJUST",  0x85, hndl_tjust,   pgc_parse_bytes,  2 },
    { "TJ",     0x85, hndl_tjust,   pgc_parse_bytes,  2 },
    { "TSIZE",  0x81, hndl_tsize,   pgc_parse_coords, 1 },
    { "TS",     0x81, hndl_tsize,   pgc_parse_coords, 1 },
    { "VWPORT", 0xb2, hndl_vwport,  pgc_parse_words,  4 },
    { "VWP",    0xb2, hndl_vwport,  pgc_parse_words,  4 },
    { "WINDOW", 0xb3, hndl_window,  pgc_parse_words,  4 },
    { "WI",     0xb3, hndl_window,  pgc_parse_words,  4 },

    { "@@@@@@", 0x00, NULL,         NULL,             0 }
};

/* When the wake timer expires, that's when the drawing thread is actually
 * woken */
static void
wake_timer(void *priv)
{
    pgc_t *dev = (pgc_t *) priv;

#ifdef ENABLE_PGC_LOG
    pgc_log("PGC: woke up\n");
#endif

    thread_set_event(dev->pgc_wake_thread);
}

/*
 * The PGC drawing thread main loop.
 *
 * Read in commands and execute them ad infinitum.
 */
static void
pgc_thread(void *priv)
{
    pgc_t           *dev = (pgc_t *) priv;
    const pgc_cmd_t *cmd;

#ifdef ENABLE_PGC_LOG
    pgc_log("PGC: thread begins\n");
#endif

    for (;;) {
        if (!parse_command(dev, &cmd)) {
            /* Are we shutting down? */
            if (dev->stopped) {
#ifdef ENABLE_PGC_LOG
                pgc_log("PGC: Thread stopping...\n");
#endif
                dev->stopped = 0;
                break;
            }

            /* Nope, just a reset. */
            continue;
        }

        pgc_log("PGC: Command: [%02x] '%s' found = %i\n",
                dev->hex_command, dev->asc_command, (cmd != NULL));

        if (cmd) {
            dev->result_count = 0;
            (*cmd->handler)(dev);
        } else
            pgc_error(dev, PGC_ERROR_OPCODE);
    }

#ifdef ENABLE_PGC_LOG
    pgc_log("PGC: thread stopped\n");
#endif
}

/* Parameter passed is not a number: abort. */
static int
err_digit(pgc_t *dev)
{
    uint8_t asc;

    do {
        /* Swallow everything until the next separator */
        if (!dev->inputbyte(dev, &asc))
            return 0;
    } while (!is_whitespace(asc));

    pgc_error(dev, PGC_ERROR_DIGIT);

    return 0;
}

/* Output a byte, either as hex or ASCII depending on the mode. */
int
pgc_result_byte(pgc_t *dev, uint8_t val)
{
    char buf[20];

    if (!dev->ascii_mode)
        return output_byte(dev, val);

    if (dev->result_count) {
        if (!output_byte(dev, ','))
            return 0;
    }
    sprintf(buf, "%i", val);
    dev->result_count++;

    return output_string(dev, buf);
}

/* Output a word, either as hex or ASCII depending on the mode. */
int
pgc_result_word(pgc_t *dev, int16_t val)
{
    char buf[20];

    if (!dev->ascii_mode) {
        if (!output_byte(dev, val & 0xFF))
            return 0;
        return output_byte(dev, val >> 8);
    }

    if (dev->result_count) {
        if (!output_byte(dev, ','))
            return 0;
    }
    sprintf(buf, "%i", val);
    dev->result_count++;

    return output_string(dev, buf);
}

/* Report an error, either in ASCII or in hex. */
int
pgc_error(pgc_t *dev, int err)
{
    if (dev->mapram[0x307]) {
        /* Errors enabled? */
        if (dev->ascii_mode) {
            if (err >= PGC_ERROR_RANGE && err <= PGC_ERROR_MISSING)
                return error_string(dev, pgc_err_msgs[err]);
            return error_string(dev, "Unknown error\r");
        } else {
            return error_byte(dev, err);
        }
    }

    return 1;
}

/* Initialize RAM and registers to default values. */
void
pgc_reset(pgc_t *dev)
{
    memset(dev->mapram, 0x00, sizeof(dev->mapram));

    /* The 'CGA disable' jumper is not currently implemented. */
    dev->mapram[0x30b] = dev->cga_enabled = 1;
    dev->mapram[0x30c]                    = dev->cga_enabled;
    dev->mapram[0x30d]                    = dev->cga_enabled;

    dev->mapram[0x3f8] = 0x03; /* minor version */
    dev->mapram[0x3f9] = 0x01; /* minor version */
    dev->mapram[0x3fb] = 0xa5; /* } */
    dev->mapram[0x3fc] = 0x5a; /* PGC self-test passed */
    dev->mapram[0x3fd] = 0x55; /* } */
    dev->mapram[0x3fe] = 0x5a; /* } */

    dev->ascii_mode   = 1; /* start off in ASCII mode */
    dev->line_pattern = 0xffff;
    memset(dev->fill_pattern, 0xff, sizeof(dev->fill_pattern));
    dev->color   = 0xff;
    dev->tjust_h = 1;
    dev->tjust_v = 1;

    /* Reset panning. */
    dev->pan_x = 0;
    dev->pan_y = 0;

    /* Reset clipping. */
    dev->vp_x1 = 0;
    dev->vp_y1 = 0;
    dev->vp_x2 = dev->visw - 1;
    dev->vp_y2 = dev->vish - 1;

    /* Empty command lists. */
    for (uint16_t n = 0; n < 256; n++) {
        dev->clist[n].wrptr  = 0;
        dev->clist[n].rdptr  = 0;
        dev->clist[n].repeat = 0;
        dev->clist[n].chain  = 0;
    }
    dev->clcur = NULL;

    /* Select CGA display. */
    dev->cga_selected = -1;
    pgc_setdisplay(dev, dev->cga_enabled);

    /* Default palette is 0. */
    init_lut(dev, 0);
    hndl_lutsav(dev);
}

/* Switch between CGA mode (DISPLAY 1) and native mode (DISPLAY 0). */
void
pgc_setdisplay(pgc_t *dev, int cga)
{
    pgc_log("PGC: setdisplay(%i): cga_selected=%i cga_enabled=%i\n",
            cga, dev->cga_selected, dev->cga_enabled);

    if (dev->cga_selected != (dev->cga_enabled && cga)) {
        dev->cga_selected = (dev->cga_enabled && cga);
        dev->displine     = 0;

        if (dev->cga_selected) {
            mem_mapping_enable(&dev->cga_mapping);
            dev->screenw = PGC_CGA_WIDTH;
            dev->screenh = PGC_CGA_HEIGHT;
        } else {
            mem_mapping_disable(&dev->cga_mapping);
            dev->screenw = dev->visw;
            dev->screenh = dev->vish;
        }

        pgc_recalctimings(dev);
    }
}

/*
 * When idle, the PGC drawing thread sleeps. pgc_wake() awakens it - but
 * not immediately. Like the Voodoo, it has a short delay so that writes
 * can be batched.
 */
void
pgc_wake(pgc_t *dev)
{
    if (!timer_is_enabled(&dev->wake_timer))
        timer_set_delay_u64(&dev->wake_timer, WAKE_DELAY);
}

/* Wait for more input data, or for output to drain. */
void
pgc_sleep(pgc_t *dev)
{
    pgc_log("PGC: sleeping on %i %i %i %i 0x%02x 0x%02x\n",
            dev->stopped,
            dev->waiting_input_fifo, dev->waiting_output_fifo,
            dev->waiting_error_fifo, dev->mapram[0x300], dev->mapram[0x301]);

    /* Avoid entering waiting state. */
    if (dev->stopped) {
        dev->waiting_input_fifo  = 0;
        dev->waiting_output_fifo = 0;
        return;
    }

    /* Race condition: If host wrote to the PGC during the that
     * won't be noticed */
    if (dev->waiting_input_fifo && dev->mapram[0x300] != dev->mapram[0x301]) {
        dev->waiting_input_fifo = 0;
        return;
    }

    /* Same if they read. */
    if (dev->waiting_output_fifo && dev->mapram[0x302] != (uint8_t) (dev->mapram[0x303] - 1)) {
        dev->waiting_output_fifo = 0;
        return;
    }

    thread_wait_event(dev->pgc_wake_thread, -1);
    thread_reset_event(dev->pgc_wake_thread);
}

/* Pull the next byte from the current command list. */
int
pgc_clist_byte(pgc_t *dev, uint8_t *val)
{
    if (dev->clcur == NULL)
        return 0;

    if (dev->clcur->rdptr < dev->clcur->wrptr)
        *val = dev->clcur->list[dev->clcur->rdptr++];
    else
        *val = 0;

    /* If we've reached the end, reset to the beginning and
     * (if repeating) run the repeat */
    if (dev->clcur->rdptr >= dev->clcur->wrptr) {
        dev->clcur->rdptr = 0;
        dev->clcur->repeat--;
        if (dev->clcur->repeat == 0)
            dev->clcur = dev->clcur->chain;
    }

    return 1;
}

/*
 * Read in a byte, either as hex (1 byte) or ASCII (decimal).
 * Returns 0 if PGC reset detected while the value is being read.
 */
int
pgc_param_byte(pgc_t *dev, uint8_t *val)
{
    int32_t c;

    if (dev->clcur)
        return pgc_clist_byte(dev, val);

    if (!dev->ascii_mode)
        return dev->inputbyte(dev, val);

    if (!pgc_param_coord(dev, &c))
        return 0;

    c = (c >> 16); /* drop fractional part */
    if (c > 255) {
        pgc_error(dev, PGC_ERROR_RANGE);
        return 0;
    }
    *val = (uint8_t) c;

    return 1;
}

/*
 * Read in a word, either as hex (2 bytes) or ASCII (decimal).
 * Returns 0 if PGC reset detected while the value is being read.
 */
int
pgc_param_word(pgc_t *dev, int16_t *val)
{
    uint8_t lo;
    uint8_t hi;
    int32_t c;

    if (dev->clcur) {
        if (!pgc_clist_byte(dev, &lo))
            return 0;
        if (!pgc_clist_byte(dev, &hi))
            return 0;
        *val = (((int16_t) hi) << 8) | lo;

        return 1;
    }

    if (!dev->ascii_mode) {
        if (!dev->inputbyte(dev, &lo))
            return 0;
        if (!dev->inputbyte(dev, &hi))
            return 0;
        *val = (((int16_t) hi) << 8) | lo;

        return 1;
    }

    if (!pgc_param_coord(dev, &c))
        return 0;

    c = (c >> 16);
    if (c > 0x7fff || c < -0x7fff) {
        pgc_error(dev, PGC_ERROR_RANGE);
        return 0;
    }
    *val = (int16_t) c;

    return 1;
}

typedef enum {
    PS_MAIN,
    PS_FRACTION,
    PS_EXPONENT
} parse_state_t;

/*
 * Read in a PGC coordinate.
 *
 * Either as hex (4 bytes) or ASCII (xxxx.yyyyEeee)
 *
 * Returns 0 if PGC reset detected while the value is being read.
 */
int
pgc_param_coord(pgc_t *dev, int32_t *value)
{
    uint8_t       asc;
    int           sign  = 1;
    int           esign = 1;
    int           n;
    uint16_t      dp       = 1;
    uint16_t      integer  = 0;
    uint16_t      frac     = 0;
    uint16_t      exponent = 0;
    uint32_t      res;
    parse_state_t state = PS_MAIN;
    uint8_t       encoded[4];

    /* If there is a command list running, pull the bytes out of that
     * command list */
    if (dev->clcur) {
        for (n = 0; n < 4; n++)
            if (!pgc_clist_byte(dev, &encoded[n]))
                return 0;
        integer = (((int16_t) encoded[1]) << 8) | encoded[0];
        frac    = (((int16_t) encoded[3]) << 8) | encoded[2];

        *value = (((int32_t) integer) << 16) | frac;
        return 1;
    }

    /* If in hex mode, read in the encoded integer and fraction parts
     * from the hex stream */
    if (!dev->ascii_mode) {
        for (n = 0; n < 4; n++)
            if (!dev->inputbyte(dev, &encoded[n]))
                return 0;
        integer = (((int16_t) encoded[1]) << 8) | encoded[0];
        frac    = (((int16_t) encoded[3]) << 8) | encoded[2];

        *value = (((int32_t) integer) << 16) | frac;
        return 1;
    }

    /* Parsing an ASCII value; skip separators. */
    do {
        if (!dev->inputbyte(dev, &asc))
            return 0;
        if (asc == '-')
            sign = -1;
    } while (is_whitespace(asc));

    /* There had better be a digit next. */
    if (!isdigit(asc)) {
        pgc_error(dev, PGC_ERROR_MISSING);
        return 0;
    }

    do {
        switch (asc) {
            /* Decimal point is acceptable in 'main' state
             * (start of fraction) not otherwise */
            case '.':
                if (state == PS_MAIN) {
                    if (!dev->inputbyte(dev, &asc))
                        return 0;
                    state = PS_FRACTION;
                    continue;
                } else {
                    pgc_error(dev, PGC_ERROR_MISSING);
                    return err_digit(dev);
                }

            /* Scientific notation. */
            case 'd':
            case 'D':
            case 'e':
            case 'E':
                esign = 1;
                if (!dev->inputbyte(dev, &asc))
                    return 0;
                if (asc == '-') {
                    sign = -1;
                    if (!dev->inputbyte(dev, &asc))
                        return 0;
                }
                state = PS_EXPONENT;
                continue;

            /* Should be a number or a separator. */
            default:
                if (is_whitespace(asc))
                    break;
                if (!isdigit(asc)) {
                    pgc_error(dev, PGC_ERROR_MISSING);
                    return err_digit(dev);
                }
                asc -= '0'; /* asc is digit */

                switch (state) {
                    case PS_MAIN:
                        integer = (integer * 10) + asc;
                        if (integer & 0x8000) {
                            /* Overflow */
                            pgc_error(dev, PGC_ERROR_RANGE);
                            integer = 0x7fff;
                        }
                        break;

                    case PS_FRACTION:
                        frac = (frac * 10) + asc;
                        dp *= 10;
                        break;

                    case PS_EXPONENT:
                        exponent = (exponent * 10) + asc;
                        break;
                }
        }

        if (!dev->inputbyte(dev, &asc))
            return 0;
    } while (!is_whitespace(asc));

    res = (frac << 16) / dp;
    pgc_log("PGC: integer=%u frac=%u exponent=%u dp=%i res=0x%08lx\n",
            integer, frac, exponent, dp, res);

    res = (res & 0xffff) | (integer << 16);
    if (exponent) {
        for (n = 0; n < exponent; n++) {
            if (esign > 0)
                res *= 10;
            else
                res /= 10;
        }
    }
    *value = sign * res;

    return 1;
}

/*
 * Add a byte to a command list.
 *
 * We allow command lists to be arbitrarily large.
 */
int
pgc_cl_append(pgc_cl_t *list, uint8_t v)
{
    uint8_t *buf;

    if (list->listmax == 0 || list->list == NULL) {
        list->list = (uint8_t *) malloc(4096);
        if (!list->list) {
#ifdef ENABLE_PGC_LOG
            pgc_log("PGC: out of memory initializing command list\n");
#endif
            return 0;
        }
        list->listmax = 4096;
    }

    while (list->wrptr >= list->listmax) {
        buf = (uint8_t *) realloc(list->list, 2 * list->listmax);
        if (!buf) {
#ifdef ENABLE_PGC_LOG
            pgc_log("PGC: out of memory growing command list\n");
#endif
            return 0;
        }
        list->list = buf;
        list->listmax *= 2;
    }

    list->list[list->wrptr++] = v;

    return 1;
}

/* Parse but don't execute a command with a fixed number of byte parameters. */
int
pgc_parse_bytes(pgc_t *dev, pgc_cl_t *cl, int count)
{
    uint8_t *param = (uint8_t *) malloc(count);

    if (!param) {
        pgc_error(dev, PGC_ERROR_OVERFLOW);
        return 0;
    }

    for (int n = 0; n < count; n++) {
        if (!pgc_param_byte(dev, &param[n])) {
            free(param);
            return 0;
        }

        if (!pgc_cl_append(cl, param[n])) {
            pgc_error(dev, PGC_ERROR_OVERFLOW);
            free(param);
            return 0;
        }
    }

    free(param);

    return 1;
}

/* Parse but don't execute a command with a fixed number of word parameters. */
int
pgc_parse_words(pgc_t *dev, pgc_cl_t *cl, int count)
{
    int16_t *param = (int16_t *) malloc(count * sizeof(int16_t));

    if (!param) {
        pgc_error(dev, PGC_ERROR_OVERFLOW);
        return 0;
    }

    for (int n = 0; n < count; n++) {
        if (!pgc_param_word(dev, &param[n])) {
            free(param);
            return 0;
        }

        if (!pgc_cl_append(cl, param[n] & 0xff) || !pgc_cl_append(cl, param[n] >> 8)) {
            pgc_error(dev, PGC_ERROR_OVERFLOW);
            free(param);
            return 0;
        }
    }

    free(param);

    return 1;
}

/* Parse but don't execute a command with a fixed number of coord parameters */
int
pgc_parse_coords(pgc_t *dev, pgc_cl_t *cl, int count)
{
    int32_t *param = (int32_t *) malloc(count * sizeof(int32_t));
    int      n;

    if (!param) {
        pgc_error(dev, PGC_ERROR_OVERFLOW);
        return 0;
    }

    for (n = 0; n < count; n++) {
        if (!pgc_param_coord(dev, &param[n])) {
            free(param);
            return 0;
        }
    }

    /* Here is how the real PGC serializes coords:
     *
     * 100.5 -> 64 00 00 80  ie 0064.8000
     * 100.3 -> 64 00 CD 4C  ie 0064.4CCD
     */
    for (n = 0; n < count; n++) {
        /* Serialize integer part. */
        if (!pgc_cl_append(cl, (param[n] >> 16) & 0xff) || !pgc_cl_append(cl, (param[n] >> 24) & 0xff) ||

            /* Serialize fraction part. */
            !pgc_cl_append(cl, (param[n]) & 0xff) || !pgc_cl_append(cl, (param[n] >> 8) & 0xff)) {
            pgc_error(dev, PGC_ERROR_OVERFLOW);
            free(param);
            return 0;
        }
    }

    free(param);

    return 1;
}

/* Convert coordinates based on the current window / viewport to raster
 * coordinates. */
void
pgc_dto_raster(pgc_t *dev, double *x, double *y)
{
#ifdef ENABLE_PGC_LOG
    double x0 = *x, y0 = *y;
#endif

    *x += (dev->vp_x1 - dev->win_x1);
    *y += (dev->vp_y1 - dev->win_y1);

    pgc_log("PGC: coords to raster: (%f, %f) -> (%f, %f)\n", x0, y0, *x, *y);
}

/* Overloads that take ints. */
void
pgc_sto_raster(pgc_t *dev, int16_t *x, int16_t *y)
{
    double xd = *x;
    double yd = *y;

    pgc_dto_raster(dev, &xd, &yd);
    *x = (int16_t) xd;
    *y = (int16_t) yd;
}

void
pgc_ito_raster(pgc_t *dev, int32_t *x, int32_t *y)
{
    double xd = *x;
    double yd = *y;

    pgc_dto_raster(dev, &xd, &yd);
    *x = (int32_t) xd;
    *y = (int32_t) yd;
}

void
pgc_recalctimings(pgc_t *dev)
{
    double  disptime;
    double _dispontime;
    double _dispofftime;
    double  pixel_clock = (cpuclock / (dev->cga_selected ? 25175000.0 : dev->native_pixel_clock) * (double) (1ULL << 32));
    uint8_t crtc0 = 97; /* Value from MDA, taken from there due to the 25 MHz refresh rate. */
    uint8_t crtc1 = 80; /* Value from MDA, taken from there due to the 25 MHz refresh rate. */

    /* Multiply pixel clock by 8. */
    pixel_clock     *= 8.0;
    /* Use a fixed 640x400 display. */
    disptime         = crtc0 + 1;
    _dispontime      = crtc1;
    _dispofftime     = disptime - _dispontime;
    _dispontime     *= pixel_clock;
    _dispofftime    *= pixel_clock;
    dev->dispontime  = (uint64_t) (_dispontime);
    dev->dispofftime = (uint64_t) (_dispofftime);
}

/* Write to CGA registers are copied into the transfer memory buffer. */
void
pgc_out(uint16_t addr, uint8_t val, void *priv)
{
    pgc_t *dev = (pgc_t *) priv;

    pgc_log("PGC: out(%04x, %02x)\n", addr, val);

    switch (addr) {
        case 0x03d0: /* CRTC Index register */
        case 0x03d2:
        case 0x03d4:
        case 0x03d6:
            dev->mapram[0x03d0] = val;
            break;

        case 0x03d1: /* CRTC Data register */
        case 0x03d3:
        case 0x03d5:
        case 0x03d7:
            if (dev->mapram[0x03d0] < 18)
                dev->mapram[0x03e0 + dev->mapram[0x03d0]] = val;
            break;

        case 0x03d8: /* CRTC Mode Control register */
            dev->mapram[0x03d8] = val;
            break;

        case 0x03d9: /* CRTC Color Select register */
            dev->mapram[0x03d9] = val;
            break;

        default:
            break;
    }
}

/* Read back the CGA registers. */
uint8_t
pgc_in(uint16_t addr, void *priv)
{
    const pgc_t *dev = (pgc_t *) priv;
    uint8_t      ret = 0xff;

    switch (addr) {
        case 0x03d0: /* CRTC Index register */
        case 0x03d2:
        case 0x03d4:
        case 0x03d6:
            ret = dev->mapram[0x03d0];
            break;

        case 0x03d1: /* CRTC Data register */
        case 0x03d3:
        case 0x03d5:
        case 0x03d7:
            if (dev->mapram[0x03d0] < 18)
                ret = dev->mapram[0x03e0 + dev->mapram[0x03d0]];
            break;

        case 0x03d8: /* CRTC Mode Control register */
            ret = dev->mapram[0x03d8];
            break;

        case 0x03d9: /* CRTC Color Select register */
            ret = dev->mapram[0x03d9];
            break;

        case 0x03da: /* CRTC Status register */
            ret = dev->mapram[0x03da];
            break;

        default:
            break;
    }

    pgc_log("PGC: in(%04x) = %02x\n", addr, ret);

    return ret;
}

/* Memory write to the transfer buffer. */
/* TODO: Check the CGA mapping repeat stuff. */
void
pgc_write(uint32_t addr, uint8_t val, void *priv)
{
    pgc_t *dev = (pgc_t *) priv;

    /*
     * It seems variable whether the PGC maps 1K or 2K at 0xc6000.
     *
     * Map 2K here in case a clone requires it.
     */
    if (addr >= 0xc6000 && addr < 0xc6800) {
        addr &= 0x7ff;

        /* If one of the FIFOs has been updated, this may cause
         * the drawing thread to be woken */

        if (dev->mapram[addr] != val) {
            dev->mapram[addr] = val;

            switch (addr) {
                case 0x300: /* input write pointer */
                    if (dev->waiting_input_fifo && dev->mapram[0x300] != dev->mapram[0x301]) {
                        dev->waiting_input_fifo = 0;
                        pgc_wake(dev);
                    }
                    break;

                case 0x303: /* output read pointer */
                    if (dev->waiting_output_fifo && dev->mapram[0x302] != (uint8_t) (dev->mapram[0x303] - 1)) {
                        dev->waiting_output_fifo = 0;
                        pgc_wake(dev);
                    }
                    break;

                case 0x305: /* error read pointer */
                    if (dev->waiting_error_fifo && dev->mapram[0x304] != (uint8_t) (dev->mapram[0x305] - 1)) {
                        dev->waiting_error_fifo = 0;
                        pgc_wake(dev);
                    }
                    break;

                case 0x306: /* cold start flag */
                    /* XXX This should be in IM-1024 specific code */
                    dev->mapram[0x306] = 0;
                    break;

                case 0x30c: /* display type */
                    pgc_setdisplay(priv, dev->mapram[0x30c]);
                    dev->mapram[0x30d] = dev->mapram[0x30c];
                    break;

                case 0x3ff: /* reboot the PGC */
                    pgc_wake(dev);
                    break;

                default:
                    break;
            }
        }
    }

    if (addr >= 0xb8000 && addr < 0xc0000 && dev->cga_selected) {
        addr &= 0x3fff;
        dev->cga_vram[addr] = val;
    }
}

/* TODO: Check the CGA mapping repeat stuff. */
uint8_t
pgc_read(uint32_t addr, void *priv)
{
    const pgc_t *dev = (pgc_t *) priv;
    uint8_t      ret = 0xff;

    if (addr >= 0xc6000 && addr < 0xc6800) {
        addr &= 0x7ff;
        ret = dev->mapram[addr];
    } else if (addr >= 0xb8000 && addr < 0xc0000 && dev->cga_selected) {
        addr &= 0x3fff;
        ret = dev->cga_vram[addr];
    }

    return ret;
}

/* Draw the display in CGA (640x400) text mode. */
void
pgc_cga_text(pgc_t *dev, int w)
{
    uint8_t        chr;
    uint8_t        attr;
    int            drawcursor = 0;
    uint32_t       cols[2];
    int            pitch = (dev->mapram[0x3e9] + 1) * 2;
    uint16_t       sc    = (dev->displine & 0x0f) % pitch;
    uint16_t       ma    = (dev->mapram[0x3ed] | (dev->mapram[0x3ec] << 8)) & 0x3fff;
    uint16_t       ca    = (dev->mapram[0x3ef] | (dev->mapram[0x3ee] << 8)) & 0x3fff;
    const uint8_t *addr;
    uint32_t       val;
    int            cw = (w == 80) ? 8 : 16;

    addr = &dev->cga_vram[((ma + ((dev->displine / pitch) * w)) * 2) & 0x3ffe];
    ma += (dev->displine / pitch) * w;

    for (int x = 0; x < w; x++) {
        chr  = *addr++;
        attr = *addr++;

        /* Cursor enabled? */
        if (ma == ca && (dev->cgablink & 8) && (dev->mapram[0x3ea] & 0x60) != 0x20) {
            drawcursor = ((dev->mapram[0x3ea] & 0x1f) <= (sc >> 1)) && ((dev->mapram[0x3eb] & 0x1f) >= (sc >> 1));
        } else
            drawcursor = 0;

        if (dev->mapram[0x3d8] & 0x20) {
            cols[1] = (attr & 15) + 16;
            cols[0] = ((attr >> 4) & 7) + 16;
            if ((dev->cgablink & 8) && (attr & 0x80) && !drawcursor)
                cols[1] = cols[0];
        } else {
            cols[1] = (attr & 15) + 16;
            cols[0] = (attr >> 4) + 16;
        }

        for (int c = 0; c < cw; c++) {
            if (drawcursor)
                val = cols[(fontdatm[chr + dev->fontbase][sc] & (1 << (c ^ 7))) ? 1 : 0] ^ 0x0f;
            else
                val = cols[(fontdatm[chr + dev->fontbase][sc] & (1 << (c ^ 7))) ? 1 : 0];
            if (cw == 8) /* 80x25 CGA text screen. */
                buffer32->line[dev->displine][(x * cw) + c] = val;
            else { /* 40x25 CGA text screen. */
                buffer32->line[dev->displine][(x * cw) + (c * 2)]     = val;
                buffer32->line[dev->displine][(x * cw) + (c * 2) + 1] = val;
            }
        }

        ma++;
    }
}

/* Draw the display in CGA (320x200) graphics mode. */
void
pgc_cga_gfx40(pgc_t *dev)
{
    uint32_t       cols[4];
    int            col;
    uint16_t       ma = (dev->mapram[0x3ed] | (dev->mapram[0x3ec] << 8)) & 0x3fff;
    const uint8_t *addr;
    uint16_t       dat;

    cols[0] = (dev->mapram[0x3d9] & 15) + 16;
    col     = ((dev->mapram[0x3d9] & 16) ? 8 : 0) + 16;

    /* From John Elliott's site:
       On a real CGA, if bit 2 of port 03D8h and bit 5 of port 03D9h are both set,
       the palette used in graphics modes is red/cyan/white. On a PGC, it's
       magenta/cyan/white. You still get red/cyan/white if bit 5 of port 03D9h is
       not set. This is a firmware issue rather than hardware. */
    if (dev->mapram[0x3d9] & 32) {
        cols[1] = col | 3;
        cols[2] = col | 5;
        cols[3] = col | 7;
    } else if (dev->mapram[0x3d8] & 4) {
        cols[1] = col | 3;
        cols[2] = col | 4;
        cols[3] = col | 7;
    } else {
        cols[1] = col | 2;
        cols[2] = col | 4;
        cols[3] = col | 6;
    }

    for (uint8_t x = 0; x < 40; x++) {
        addr = &dev->cga_vram[(ma + 2 * x + 80 * (dev->displine >> 2) + 0x2000 * ((dev->displine >> 1) & 1)) & 0x3fff];
        dat  = (addr[0] << 8) | addr[1];
        dev->ma++;
        for (uint8_t c = 0; c < 8; c++) {
            buffer32->line[dev->displine][(x << 4) + (c << 1)] = buffer32->line[dev->displine][(x << 4) + (c << 1) + 1] = cols[dat >> 14];
            dat <<= 2;
        }
    }
}

/* Draw the display in CGA (640x200) graphics mode. */
void
pgc_cga_gfx80(pgc_t *dev)
{
    uint32_t       cols[2];
    uint16_t       ma = (dev->mapram[0x3ed] | (dev->mapram[0x3ec] << 8)) & 0x3fff;
    const uint8_t *addr;
    uint16_t       dat;

    cols[0] = 16;
    cols[1] = (dev->mapram[0x3d9] & 15) + 16;

    for (uint8_t x = 0; x < 40; x++) {
        addr = &dev->cga_vram[(ma + 2 * x + 80 * (dev->displine >> 2) + 0x2000 * ((dev->displine >> 1) & 1)) & 0x3fff];
        dat  = (addr[0] << 8) | addr[1];
        dev->ma++;
        for (uint8_t c = 0; c < 16; c++) {
            buffer32->line[dev->displine][(x << 4) + c] = cols[dat >> 15];
            dat <<= 1;
        }
    }
}

/* Draw the screen in CGA mode. */
void
pgc_cga_poll(pgc_t *dev)
{
    uint32_t cols[2];

    if (!dev->linepos) {
        timer_advance_u64(&dev->timer, dev->dispofftime);
        dev->mapram[0x03da] |= 1;
        dev->linepos = 1;

        if (dev->cgadispon) {
            if (dev->displine == 0)
                video_wait_for_buffer();

            if ((dev->mapram[0x03d8] & 0x12) == 0x12)
                pgc_cga_gfx80(dev);
            else if (dev->mapram[0x03d8] & 0x02)
                pgc_cga_gfx40(dev);
            else if (dev->mapram[0x03d8] & 0x01)
                pgc_cga_text(dev, 80);
            else
                pgc_cga_text(dev, 40);
        } else {
            cols[0] = ((dev->mapram[0x03d8] & 0x12) == 0x12) ? 0 : ((dev->mapram[0x03d9] & 15) + 16);
            hline(buffer32, 0, dev->displine, PGC_CGA_WIDTH, cols[0]);
        }

        video_process_8(PGC_CGA_WIDTH, dev->displine);

        if (++dev->displine == PGC_CGA_HEIGHT) {
            dev->mapram[0x3da] |= 8;
            dev->cgadispon = 0;
        }
        if (dev->displine == PGC_CGA_HEIGHT + 32) {
            dev->mapram[0x3da] &= ~8;
            dev->cgadispon = 1;
            dev->displine  = 0;
        }
    } else {
        if (dev->cgadispon)
            dev->mapram[0x3da] &= ~1;
        timer_advance_u64(&dev->timer, dev->dispontime);
        dev->linepos = 0;

        if (dev->displine == PGC_CGA_HEIGHT) {
            if (PGC_CGA_WIDTH != xsize || PGC_CGA_HEIGHT != ysize) {
                xsize = PGC_CGA_WIDTH;
                ysize = PGC_CGA_HEIGHT;
                set_screen_size(xsize, ysize);

                if (video_force_resize_get())
                    video_force_resize_set(0);
            }
            video_blit_memtoscreen(0, 0, xsize, ysize);
            frames++;

            /* We have a fixed 640x400 screen for CGA modes. */
            video_res_x = PGC_CGA_WIDTH;
            video_res_y = PGC_CGA_HEIGHT;
            switch (dev->mapram[0x3d8] & 0x12) {
                case 0x12:
                    video_bpp = 1;
                    break;

                case 0x02:
                    video_bpp = 2;
                    break;

                default:
                    video_bpp = 0;
                    break;
            }
            dev->cgablink++;
        }
    }
}

/* Draw the screen in CGA or native mode. */
void
pgc_poll(void *priv)
{
    pgc_t   *dev = (pgc_t *) priv;
    uint32_t y;

    if (dev->cga_selected) {
        pgc_cga_poll(dev);
        return;
    }

    /* Not CGA, so must be native mode. */
    if (!dev->linepos) {
        timer_advance_u64(&dev->timer, dev->dispofftime);
        dev->mapram[0x3da] |= 1;
        dev->linepos = 1;
        if (dev->cgadispon && (uint32_t) dev->displine < dev->maxh) {
            if (dev->displine == 0)
                video_wait_for_buffer();

            /* Don't know why pan needs to be multiplied by -2, but
             * the IM1024 driver uses PAN -112 for an offset of
             * 224. */
            y = dev->displine - 2 * dev->pan_y;
            for (uint32_t x = 0; x < dev->screenw; x++) {
                if (x + dev->pan_x < dev->maxw)
                    buffer32->line[dev->displine][x] = dev->palette[dev->vram[y * dev->maxw + x]];
                else
                    buffer32->line[dev->displine][x] = dev->palette[0];
            }
        } else {
            hline(buffer32, 0, dev->displine, dev->screenw, dev->palette[0]);
        }

        if (++dev->displine == dev->screenh) {
            dev->mapram[0x3da] |= 8;
            dev->cgadispon = 0;
        }

        if (dev->displine == dev->screenh + 32) {
            dev->mapram[0x3da] &= ~8;
            dev->cgadispon = 1;
            dev->displine  = 0;
        }
    } else {
        if (dev->cgadispon)
            dev->mapram[0x3da] &= ~1;
        timer_advance_u64(&dev->timer, dev->dispontime);
        dev->linepos = 0;

        if (dev->displine == dev->screenh) {
            if (dev->screenw != xsize || dev->screenh != ysize) {
                xsize = dev->screenw;
                ysize = dev->screenh;
                set_screen_size(xsize, ysize);

                if (video_force_resize_get())
                    video_force_resize_set(0);
            }
            video_blit_memtoscreen(0, 0, xsize, ysize);
            frames++;

            video_res_x = dev->screenw;
            video_res_y = dev->screenh;
            video_bpp   = 8;
            dev->cgablink++;
        }
    }
}

void
pgc_speed_changed(void *priv)
{
    pgc_t *dev = (pgc_t *) priv;

    pgc_recalctimings(dev);
}

void
pgc_close_common(void *priv)
{
    pgc_t *dev = (pgc_t *) priv;

    /*
     * Close down the worker thread by setting a
     * flag, and then simulating a reset so it
     * stops reading data.
     */
#ifdef ENABLE_PGC_LOG
    pgc_log("PGC: telling thread to stop...\n");
#endif
    dev->stopped       = 1;
    dev->mapram[0x3ff] = 1;
    if (dev->waiting_input_fifo || dev->waiting_output_fifo) {
        /* Do an immediate wake-up. */
        wake_timer(priv);
    }

    /* Wait for thread to stop. */
#ifdef ENABLE_PGC_LOG
    pgc_log("PGC: waiting for thread to stop...\n");
#endif
    // while (dev->stopped);
    thread_wait(dev->pgc_thread);
#ifdef ENABLE_PGC_LOG
    pgc_log("PGC: thread stopped, closing up.\n");
#endif

    if (dev->cga_vram)
        free(dev->cga_vram);
    if (dev->vram)
        free(dev->vram);
}

void
pgc_close(void *priv)
{
    pgc_t *dev = (pgc_t *) priv;

    pgc_close_common(priv);

    free(dev);
}

/*
 * Initialization code common to the PGC and its subclasses.
 *
 * Pass the 'input byte' function in since this is overridden in
 * the IM-1024, and needs to be set before the drawing thread is
 * launched.
 */
void
pgc_init(pgc_t *dev, int maxw, int maxh, int visw, int vish,
         int (*inpbyte)(pgc_t *, uint8_t *), double npc)
{
    /* Make it a 16k mapping at C4000 (will be C4000-C7FFF),
       because of the emulator's granularity - the original
       mapping will conflict with hard disk controller BIOS'es. */
    mem_mapping_add(&dev->mapping, 0xc4000, 16384,
                    pgc_read, NULL, NULL, pgc_write, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_add(&dev->cga_mapping, 0xb8000, 32768,
                    pgc_read, NULL, NULL, pgc_write, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, dev);

    io_sethandler(0x03d0, 16,
                  pgc_in, NULL, NULL, pgc_out, NULL, NULL, dev);

    dev->maxw = maxw;
    dev->maxh = maxh;
    dev->visw = visw;
    dev->vish = vish;

    dev->vram = (uint8_t *) malloc((size_t) maxw * maxh);
    memset(dev->vram, 0x00, (size_t) maxw * maxh);
    dev->cga_vram = (uint8_t *) malloc(16384);
    memset(dev->cga_vram, 0x00, 16384);

    /* Create and initialize command lists. */
    dev->clist = (pgc_cl_t *) malloc(256 * sizeof(pgc_cl_t));
    memset(dev->clist, 0x00, 256 * sizeof(pgc_cl_t));
    for (uint16_t i = 0; i < 256; i++) {
        dev->clist[i].list    = NULL;
        dev->clist[i].listmax = 0;
        dev->clist[i].wrptr   = 0;
        dev->clist[i].rdptr   = 0;
        dev->clist[i].repeat  = 0;
        dev->clist[i].chain   = NULL;
    }
    dev->clcur              = NULL;
    dev->native_pixel_clock = npc;

    pgc_reset(dev);

    dev->inputbyte = inpbyte;
    dev->master = dev->commands = pgc_commands;
    dev->pgc_wake_thread        = thread_create_event();
    dev->pgc_thread             = thread_create(pgc_thread, dev);

    timer_add(&dev->timer, pgc_poll, dev, 1);

    timer_add(&dev->wake_timer, wake_timer, dev, 0);
}

static void *
pgc_standalone_init(const device_t *info)
{
    pgc_t *dev;

    dev = (pgc_t *) malloc(sizeof(pgc_t));
    memset(dev, 0x00, sizeof(pgc_t));
    dev->type = info->local;

    /* Framebuffer and screen are both 640x480. */
    pgc_init(dev, 640, 480, 640, 480, input_byte, 25175000.0);

    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_pgc);

    return dev;
}

const device_t pgc_device = {
    .name          = "IBM PGC",
    .internal_name = "pgc",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = pgc_standalone_init,
    .close         = pgc_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = pgc_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};
