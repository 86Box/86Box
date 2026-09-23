/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the plasma display adapter of the IBM PS/2
 *          Model P70 (8573), both on the type 1 and on the type 2 planar.
 *
 *          The card owns MCA channel 3 (adapter ID EDAF, no ADF - the BIOS
 *          configures it internally), the POS registers and the PDC. The
 *          panel filter only borrows the machine's VGA core: the panel
 *          state lives here, and the core is reached through dev->svga
 *          and points back through svga->plasma so that the wrapped
 *          renderer can find it.
 *
 * Authors: WNT50
 *
 *          Copyright 2026 WNT50.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/mca.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/vid_vga.h>
#include <86box/plat_unused.h>

/* Pulse width; the rate it repeats at is the core's frame, not a period of this device. */
#define IBM_PLASMA_VSYNC_SYNC_USEC 2000

/* Half-toning tag: the PELs of a not yet half-toned group are marked in the blue channel,
   which the panel never uses, so that a group the VGA core has redrawn can be told apart
   from one that still holds the pattern written by the previous frame. */
#define IBM_PLASMA_MODE_13H_TAG 0x01

/* Brightness levels per PEL. */
#define IBM_PLASMA_PANEL_LEVELS 16

typedef struct ibm_plasma_t {
    uint8_t    pdc_index;
    uint8_t    pdc_regs[3];
    uint8_t    frame_running;
    uint8_t    display_lead;
    uint8_t    vsync;

    /* The core the panel filter is attached to, and the panel brightness levels, darkest first.
       The core belongs to the machine, not to the adapter card, so this device never destroys
       it - it only takes the pointer back out on close. */
    svga_t    *svga;
    uint8_t    panel_data;
    uint32_t   levels[IBM_PLASMA_PANEL_LEVELS];

    /* Auto-dim: the period PDC register 2 was programmed with in minutes (0 while it holds a
       code that configures no period at all), and whether the up-counter has tripped. */
    uint8_t    autodim_tripped;
    uint16_t   autodim_minutes;
    pc_timer_t autodim_timer;
    pc_timer_t vsync_timer;

    uint8_t    pos_regs[8];
} ibm_plasma_t;

/* Arm the auto-dim up-counter, or stop it when the guest leaves the feature without a period or
   with AUTO-DIM ENABLE turned off. timer_on_auto() takes microseconds and is the API for period
   this long: a minute count of 5 and up overflows the 64-bit delay timer_set_delay_u64() wants,
   and it also does the right thing when the counter is restarted from its own callback. */
static void
ibm_plasma_autodim_arm(ibm_plasma_t *dev)
{
    timer_on_auto(&dev->autodim_timer,
                  (dev->pdc_regs[1] & 0x80) ? 0.0 : ((double) dev->autodim_minutes * 60.0 * 1000000.0));
}

static void
ibm_plasma_autodim_callback(void *priv)
{
    ibm_plasma_t *dev = (ibm_plasma_t *) priv;

    ibm_plasma_autodim_arm(dev);

    /* The count has reached the period the guest selected, so the panel is disabled and the
       counter restarted. */
    dev->autodim_tripped = 1;
}

/* Keyboard activity resets the counter: the keyboard interrupt service reads port 060h to take a
   scan code, so watching the read is the same trigger without reaching into the controller.
   io.c ands the handler chain together, so answering 0FFh here leaves what the controller
   returns untouched. */
static uint8_t
ibm_plasma_keyboard_wakeup(UNUSED(uint16_t port), void *priv)
{
    ibm_plasma_t *dev = (ibm_plasma_t *) priv;

    ibm_plasma_autodim_arm(dev);

    dev->autodim_tripped = 0;

    return 0xff;
}

static void
ibm_plasma_pdc_index_write(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    ibm_plasma_t *dev = (ibm_plasma_t *) priv;

    dev->pdc_index = val;
}

/* The PDC is programmed through the 0x0D00 (write-only index) / 0x0D01 (data) pair at the front
   of the 0d00h-0d07h window the planar ADFs declare for the card; the index is static, so every
   access rewrites it. Register 1 bit 6 is the vertical synchronization flag, the register's low
   nibble is a status feedback of the flag plus the panel data, and both of its control bits are
   active low (the two inputs of the panel's disable gate). */
static uint8_t
ibm_plasma_pdc_data_read(UNUSED(uint16_t port), void *priv)
{
    ibm_plasma_t *dev = (ibm_plasma_t *) priv;
    uint8_t       ret = 0xff;

    if (dev->pdc_index < 3) {
        uint8_t val = dev->pdc_regs[dev->pdc_index];

        if (dev->pdc_index != 1)
            ret = val;
        else {
            ret = ((val & 0x40) ? 0x0f : 0x00) | dev->panel_data;

            if (dev->frame_running) {
                if (dev->vsync)
                    ret |= 0x50;
                else if (dev->display_lead) {
                    dev->display_lead = 0;
                    ret |= 0x10;
                } else
                    ret |= 0xb0;
            } else
                ret |= val & 0xf0;
        }
    }

    return ret;
}

static void
ibm_plasma_pdc_data_write(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    ibm_plasma_t *dev = (ibm_plasma_t *) priv;

    if (dev->pdc_index < 3) {
        dev->pdc_regs[dev->pdc_index] = val;

        if (dev->pdc_index == 0) {
            /* The counter being started is the core's frame retrace, which runs whether or not
               the guest starts it, so bit 0 only decides whether the read-back reports it. */
            dev->frame_running = val & 0x01;
        } else if (dev->pdc_index == 2) {
            dev->autodim_minutes = (val & 0x80) ? (val & 0x7f) : 0;
            ibm_plasma_autodim_arm(dev);
        } else {
            /* Register 1 carries AUTO-DIM ENABLE, so writing it arms or stops the counter. */
            ibm_plasma_autodim_arm(dev);
        }
    }
}

/* One handler pair for the whole 0d00h-0d07h window the planar ADFs declare for the adapter, the
   way the keyboard controllers register their port block: only 0D00 and 0D01 carry anything, the
   rest of the window is decoded but empty. */
static uint8_t
ibm_plasma_pdc_read(uint16_t port, void *priv)
{
    if (port == 0x0d01)
        return ibm_plasma_pdc_data_read(port, priv);

    return 0xff;
}

static void
ibm_plasma_pdc_write(uint16_t port, uint8_t val, void *priv)
{
    switch (port) {
        case 0x0d00:
            ibm_plasma_pdc_index_write(port, val, priv);
            break;

        case 0x0d01:
            ibm_plasma_pdc_data_write(port, val, priv);
            break;

        default:
            break;
    }
}

/* The panel is the PS/2 monochrome display 8503: 640x400 with 16 brightness levels per PEL, fed
   by the green DAC component alone. Video modes 00h-12h use the panel's "distinctive mapping",
   mode 13h is half-toned; the mapping is committed once per panel frame, from the retrace the
   synchronization pulse starts. */
static uint8_t
ibm_plasma_luma(const svga_t *svga, int index)
{
    uint8_t green = svga->vgapal[index].g;
    /* Brightness source of the 8503, widened to 8 bits the way the VGA core widens its own lookup. */
    return (svga->ramdac_type == RAMDAC_8BIT) ? green : (uint8_t) (((uint32_t) (green & 0x3f) * 255) / 63);
}

/* Mode 13h: a 2x2 group of PELs carries a 6-bit luma in the green channel of the lookup, which
   are split into a base panel level and a phase; the raised PELs sit on the diagonal and every
   other logical row mirrors that arrangement. Walking the group dot by dot instead would leave
   the lower scanline at the base level for three of the four phases, which shows up as a dark
   stripe over any area of even colour. */
static void
ibm_plasma_13h_group(const uint32_t *levels, uint32_t *row0, uint32_t *row1, int x, int mirrored)
{
    uint32_t pel = row1[x];
    uint32_t hi, lo;
    uint8_t  luma6;
    uint8_t  q, k;

    /* The upper scanline is only drawn when the VGA core sees a change in video memory, so a
       group may still hold the pattern of the previous frame while the lower scanline always
       carries what the core has just written. */
    if ((pel & 0xff) != IBM_PLASMA_MODE_13H_TAG) {
        pel = row0[x];

        if ((pel & 0xff) != IBM_PLASMA_MODE_13H_TAG)
            return;
    }

    luma6 = (uint8_t) ((pel >> 10) & 0x3f);
    q     = luma6 >> 2;
    k     = luma6 & 0x03;

    lo = levels[q];
    hi = (q < (IBM_PLASMA_PANEL_LEVELS - 1)) ? levels[q + 1] : lo;

    if (!mirrored) {
        row0[x]     = (k > 0) ? hi : lo;
        row1[x + 1] = (k > 1) ? hi : lo;
        row0[x + 1] = (k > 2) ? hi : lo;
        row1[x]     = lo;
    } else {
        row1[x]     = (k > 0) ? hi : lo;
        row0[x + 1] = (k > 1) ? hi : lo;
        row1[x + 1] = (k > 2) ? hi : lo;
        row0[x]     = lo;
    }
}

static uint32_t
ibm_plasma_13h_encode(const svga_t *svga, int index)
{
    return makecol(0xff, (ibm_plasma_luma(svga, index) & 0xfc), IBM_PLASMA_MODE_13H_TAG);
}

static void
ibm_plasma_13h_render(svga_t *svga)
{
    const ibm_plasma_t *dev = (const ibm_plasma_t *) svga->plasma;

    bitmap_t *buf;
    uint32_t *row0;
    uint32_t *row1;
    int       x, y;
    int       xmax;
    int       mirrored;

    svga_render_8bpp_lowres(svga);

    /* A group can only be rearranged once the second of its two scanlines is there, i.e.
       on the scanline that ends the row (the maximum scan line is one in this mode). */
    if ((dev == NULL) || (svga->scanline != svga->rowcount))
        return;

    buf = svga->monitor->target_buffer;
    y   = svga->displine + svga->y_add;

    if ((buf == NULL) || (y < 1) || (y >= buf->h))
        return;

    row0 = buf->line[y - 1];
    row1 = buf->line[y];

    if ((row0 == NULL) || (row1 == NULL))
        return;

    xmax = svga->x_add + svga->hdisp;
    if (xmax > (buf->w - 1))
        xmax = buf->w - 1;

    /* Two scanlines make one logical row, so this alternates. */
    mirrored = (svga->displine >> 1) & 1;

    for (x = svga->x_add; x < xmax; x += 2)
        ibm_plasma_13h_group(dev->levels, row0, row1, x, mirrored);
}

/* The panel data nibble that PDC register 1 reports: the grey-scale level the mapping gave to the
   pixel at the start of the display window, taken straight back out of the core's output buffer. */
static uint8_t
ibm_plasma_panel_data(const ibm_plasma_t *dev)
{
    const svga_t   *svga = dev->svga;
    const bitmap_t *buf;
    const uint32_t *row;
    uint32_t        pel;
    int             x;
    int             y;
    int             i;

    if (svga == NULL)
        return 0;

    buf = svga->monitor->target_buffer;

    if (buf == NULL)
        return 0;

    x = svga->x_add;
    y = svga->y_add;

    if ((x < 0) || (y < 0) || (x >= buf->w) || (y >= buf->h))
        return 0;

    row = buf->line[y];

    if (row == NULL)
        return 0;

    pel = row[x];

    /* Not rearranged yet, so the 6-bit luma is still in the green channel. */
    if ((pel & 0xff) == IBM_PLASMA_MODE_13H_TAG)
        return (uint8_t) (((pel >> 10) & 0x3f) >> 2);

    for (i = 0; i < IBM_PLASMA_PANEL_LEVELS; i++) {
        if (dev->levels[i] == pel)
            return (uint8_t) i;
    }

    return 0;
}

/* The two inputs of the panel's disable gate the guest can reach, both active low;
   the counter only counts while AUTO-DIM ENABLE is clear. */
static int
ibm_plasma_panel_disabled(const ibm_plasma_t *dev)
{
    const int autodim = dev->autodim_tripped && !(dev->pdc_regs[1] & 0x80);

    return (dev->pdc_regs[1] & 0x40) || autodim;
}

static void
ibm_plasma_remap(ibm_plasma_t *dev)
{
    svga_t  *svga = dev->svga;
    uint32_t newpal[256];
    int      changed = 0;
    int      i;
    int      j;
    int      rank;

    const int disabled = ibm_plasma_panel_disabled(dev);
    const int panel_on = !disabled;

    /* The two inputs of the gate are not the same thing to the output: PDP ENABLE selects the
       external PS/2 display and leaves that picture alone, while the auto-dim comparator only
       takes the panel's own light away - the panel with nothing lit, i.e. the monitor asleep. */
    const int external = (dev->pdc_regs[1] & 0x40) != 0;
    const int asleep   = disabled && !external;

    if (svga->dpms != asleep) {
        const int woke = svga->dpms && !asleep;

        svga->dpms = asleep;

        /* The recalculation is what blacks the screen out and flags the sleep state. */
        svga_recalctimings(svga);

        /* The blackout overwrote the frame, so waking up has to ask for it to be painted again
           the way a palette change does. */
        if (woke)
            svga->fullchange = changeframecount;
    }

    if (asleep) {
        /* Nothing is drawn while the monitor sleeps, so the mapping is left as it is and waking
           up restores the picture the panel would have been showing. */
        dev->panel_data = 0;

        return;
    }

    /* 320x200 logical dots doubled in both directions, i.e. the mode the half-toning makes
       its 64 grey patterns for; the other 8bpp low resolution modes do not double the rows. */
    const int half_toning = ((svga->render == svga_render_8bpp_lowres) || (svga->render == ibm_plasma_13h_render))
                            && (svga->rowcount == 1) && panel_on;

    if (!panel_on) {
        /* Nothing the PDC does reaches the panel, so the machine drives the plain VGA lookup out
           for the external display, rebuilt the way the core rebuilds it on a DAC write. */
        for (i = 0; i < 256; i++) {
            if (svga->ramdac_type == RAMDAC_8BIT)
                newpal[i] = makecol32(svga->vgapal[i].r, svga->vgapal[i].g, svga->vgapal[i].b);
            else
                newpal[i] = makecol32(video_6to8[svga->vgapal[i].r & 0x3f],
                                      video_6to8[svga->vgapal[i].g & 0x3f],
                                      video_6to8[svga->vgapal[i].b & 0x3f]);
        }

        if (svga->render == ibm_plasma_13h_render)
            svga->render = svga_render_8bpp_lowres;
    } else if (half_toning) {
        for (i = 0; i < 256; i++)
            newpal[i] = ibm_plasma_13h_encode(svga, i);
    } else {
        /* Distinctive mapping: rank the colour registers the standard modes use by their
           brightness and give each one its own panel level, keeping the relative order
           (colours of equal brightness are separated by their register index). */
        for (i = 0; i < 16; i++) {
            rank = 0;

            for (j = 0; j < 16; j++) {
                if ((ibm_plasma_luma(svga, j) < ibm_plasma_luma(svga, i)) ||
                    ((ibm_plasma_luma(svga, j) == ibm_plasma_luma(svga, i)) && (j < i)))
                    rank++;
            }

            newpal[i] = dev->levels[rank];
        }

        /* The remaining colour registers are only reached by the half-toned mode. */
        for (i = 16; i < 256; i++)
            newpal[i] = dev->levels[ibm_plasma_luma(svga, i) >> 4];
    }

    for (i = 0; i < 256; i++) {
        if (svga->pallook[i] != newpal[i])
            changed = 1;

        svga->pallook[i] = newpal[i];
    }

    /* The guest's own palette writes also rebuild the lookup, so a change has to ask for a
       full redraw the way the VGA core does on a palette write. */
    if (changed)
        svga->fullchange = changeframecount;

    /* The VGA core programs the renderer whenever the mode changes, so the half-toning
       wrapper is (re)installed here, once per panel frame. */
    if (half_toning && (svga->render == svga_render_8bpp_lowres))
        svga->render = ibm_plasma_13h_render;

    /* Like the mapping itself, the read-back the PDC offers is refreshed once per panel frame.
       A disabled panel drives no grey-scale data. */
    dev->panel_data = panel_on ? ibm_plasma_panel_data(dev) : 0;
}

/* The panel's vertical synchronization is the video mode's own field, so the pulse is started by
   the core's frame retrace rather than by a period of this device - that is what POST's timing
   test measures. The picture (the distinctive mapping, or the half-tone pattern of mode 13h)
   is committed on the same frame, hence the remap here. The retrace hook is a single slot
   and close() takes the pointer back out. */
static void
ibm_plasma_vsync_start(svga_t *svga)
{
    ibm_plasma_t *dev = (ibm_plasma_t *) svga->plasma;

    if (dev == NULL)
        return;

    dev->vsync        = 1;
    dev->display_lead = 0;

    timer_set_delay_u64(&dev->vsync_timer, ((uint64_t) IBM_PLASMA_VSYNC_SYNC_USEC) * TIMER_USEC);

    ibm_plasma_remap(dev);
}

/* The pulse the retrace started ends here, and the next retrace starts the following one;
   the active period in between is not timed, it is the frame the core is drawing. */
static void
ibm_plasma_vsync_end(void *priv)
{
    ibm_plasma_t *dev = (ibm_plasma_t *) priv;

    dev->vsync        = 0;
    dev->display_lead = 1;
}

static uint8_t
ibm_plasma_pos_read(uint16_t port, void *priv)
{
    const ibm_plasma_t *dev = (ibm_plasma_t *) priv;

    return dev->pos_regs[port & 7];
}

static void
ibm_plasma_pos_write(uint16_t port, uint8_t val, void *priv)
{
    ibm_plasma_t *dev = (ibm_plasma_t *) priv;

    if (port < 0x102)
        return;

    dev->pos_regs[port & 7] = val;
}

static uint8_t
ibm_plasma_feedb(void *priv)
{
    const ibm_plasma_t *dev = (ibm_plasma_t *) priv;

    return dev->pos_regs[2] & 0x01;
}

static void *
ibm_plasma_init(UNUSED(const device_t *info))
{
    ibm_plasma_t *dev = calloc(1, sizeof(ibm_plasma_t));

    /* Adapter ID EDAF in POS registers 0 and 1. */
    dev->pos_regs[0] = 0xaf;
    dev->pos_regs[1] = 0xed;

    /* The adapter card is always present on the P70: the self-test programs its POS registers and
       talks to the PDC no matter which video card or adapter slot the machine is set to use. */
    mca_add_to_slot(ibm_plasma_pos_read, ibm_plasma_pos_write, ibm_plasma_feedb, NULL, dev, 3);

    /* The whole 0d00h-0d07h window the planar ADFs declare, which only 0d00h and 0d01h are used. */
    io_sethandler(0x0d00, 0x0008, ibm_plasma_pdc_read, NULL, NULL, ibm_plasma_pdc_write, NULL, NULL, dev);

    /* The auto-dim counter follows keyboard activity, and the keyboard controller hands out no
       signal for it, so the scan-code port is watched instead for the plasma wake-up function. */
    io_sethandler(0x0060, 0x0001, ibm_plasma_keyboard_wakeup, NULL, NULL, NULL, NULL, NULL, dev);

    /* The pulse only lasts a fixed width, so this timer is added disabled and started from the
       retrace; the frame counter itself is the guest's to start and stop through register 0. */
    timer_add(&dev->autodim_timer, ibm_plasma_autodim_callback, dev, 0);
    timer_add(&dev->vsync_timer, ibm_plasma_vsync_end, dev, 0);

    /* The VGA core belongs to the machine, not to the adapter card, so the panel filter attaches
       to whatever core is already up. The P70 has fixed video (MACHINE_VIDEO_FIXED), so that core
       is always the machine's built-in VGA and is up before this runs. */
    svga_t *svga = svga_get_pri();

    if (svga != NULL) {
        for (int i = 0; i < IBM_PLASMA_PANEL_LEVELS; i++) {
            dev->levels[i] = makecol((0xff * i) / (IBM_PLASMA_PANEL_LEVELS - 1),
                                     (0x7d * i) / (IBM_PLASMA_PANEL_LEVELS - 1),
                                     0);
        }

        dev->svga    = svga;
        svga->plasma = dev;

        svga->vsync_callback = ibm_plasma_vsync_start;

        /* The panel is what this adapter drives, so there is no monitor on the planar VGA's
           connector and its 3C2h switch sense has to report "no cable" - that is the state
           the reference diskette's brightness program reads. */
        svga->cable_connected = 0;

        /* The mapping is committed from the retrace hook above; this is the state before
           the first frame. */
        ibm_plasma_remap(dev);
    }

    return dev;
}

static void
ibm_plasma_close(void *priv)
{
    ibm_plasma_t *dev = (ibm_plasma_t *) priv;

    timer_disable(&dev->vsync_timer);
    timer_disable(&dev->autodim_timer);

    if (dev->svga != NULL) {
        if (dev->svga->vsync_callback == ibm_plasma_vsync_start)
            dev->svga->vsync_callback = NULL;

        dev->svga->plasma = NULL;
    }

    free(dev);
}

/* These only have an svga_t to work with, so they use the core-level calls the VGA and XGA
   cores use themselves rather than the vga_t wrappers, which need a core this device may
   not own. */
static void
ibm_plasma_speed_changed(void *priv)
{
    ibm_plasma_t *dev = (ibm_plasma_t *) priv;

    if (dev->svga != NULL)
        svga_recalctimings(dev->svga);
}

static void
ibm_plasma_force_redraw(void *priv)
{
    ibm_plasma_t *dev = (ibm_plasma_t *) priv;

    if (dev->svga != NULL)
        dev->svga->fullchange = changeframecount;
}

const device_t ibm_plasma_vga_device = {
    .name          = "IBM PS/2 model P70 Plasma Display",
    .internal_name = "ibm_plasma_vga",
    .flags         = DEVICE_MCA,
    .local         = 0,
    .init          = ibm_plasma_init,
    .close         = ibm_plasma_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = ibm_plasma_speed_changed,
    .force_redraw  = ibm_plasma_force_redraw,
    .config        = NULL
};
