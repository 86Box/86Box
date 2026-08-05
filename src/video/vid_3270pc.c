/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM 3270 PC Display Adapter emulation.
 *
 *          The IBM 3270 PC (model 5271) is an IBM PC/XT whose planar is a
 *          stock 5160; everything 3270-specific lives on a stack of one to
 *          three full-length ISA cards:
 *
 *            - the main display card (SCN2672 PVTC, 80x25 text, video out),
 *            - the All Points Addressable option (CGA graphics modes plus
 *              720x350 mono and 360x350 4-colour, unique to this machine),
 *            - the Programmed Symbols option (loadable fonts, not emulated).
 *
 *          The card behaves as a 3270 terminal screen "in front of" a CGA or
 *          MDA screen: a 4-byte-per-cell 3270 plane at A0000 composites over
 *          a 4K CGA/MDA text plane, with character 0xFF meaning transparent.
 *
 *          The video BIOS lives in ROM on the keyboard controller card and is
 *          mapped twice, at C0000 and CA000; this device maps both, because
 *          without it the display card does nothing at all.
 *
 *          Documentation for this hardware is limited to John Elliott's
 *          reverse engineering at <http://www.seasip.info/VintagePC/5271.html>
 *          plus disassembly of the adapter ROM.  There is no IBM technical
 *          reference.  Registers whose behaviour is guessed rather than known
 *          are marked as such below.
 *
 * Authors: Anatoliy Sova, <anatoliysova@gmail.com>
 *
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/pic.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/plat_unused.h>

#define PC3270_ROM_PATH  "roms/video/ibm3270pc/6323581.bin"
#define PC3270_FONT_LO   "roms/video/ibm3270pc/1504161.bin" /* pixel columns 0-7 */
#define PC3270_FONT_HI   "roms/video/ibm3270pc/1504162.bin" /* pixel column 8    */

/* Both video configurations divide their crystal by 9 to get the character
   clock.  The mono crystal is the same 16.257 MHz an MDA uses.  These yield
   18.4 kHz / 50 Hz mono and 23.6 kHz / 63 Hz colour, matching the frequencies
   measured on real hardware. */
#define PC3270_DOT_MONO  16257000.0
#define PC3270_DOT_COLOR 21676000.0

#define PC3270_TRAM_SIZE 8192  /* A0000, 2048 cells x 4 bytes */
#define PC3270_CRAM_SIZE 4096  /* CGA/MDA text plane, aliased through B0000/B8000 */
#define PC3270_APA_SIZE  32768 /* All Points Addressable plane at B8000 */

#define PC3270_ROM_SIZE  8192

/* SCN2672 status register, read from port 0x181. */
#define STAT_LIGHTPEN 0x01
#define STAT_READY    0x02
#define STAT_SPLIT    0x04
#define STAT_LINEZERO 0x08
#define STAT_VBLANK   0x10
#define STAT_RDFLG    0x20

typedef struct pc3270_t {
    mem_mapping_t mapping_3270;  /* A0000-A1FFF */
    mem_mapping_t mapping_mda;   /* B0000-B7FFF */
    mem_mapping_t mapping_cga;   /* B8000-BFFFF */
    mem_mapping_t mapping_romlo; /* C0000-C07FF */
    mem_mapping_t mapping_romhi; /* CA000-CB7FF */

    uint8_t *rom;  /* the 6323581 image */
    uint8_t *tram; /* 3270 plane */
    uint8_t *cram; /* emulated CGA/MDA text plane */
    uint8_t *apa;  /* graphics plane */

    /* Font ROM, packed 9 bits per row: bit 8 leftmost, bit 0 is column 9.
       Bank 0 (0x000-0x0FF) is the 3270 character set used by the A0000 plane;
       bank 1 (0x100-0x1FF) is CP437, used by the emulated CGA/MDA plane.  The
       split is inferred: no document states it, but bank 1 is byte-for-byte
       CP437 and bank 1 is what the emulated plane has to draw. */
    uint16_t font[512][16];

    /* SCN2672 PVTC. */
    uint8_t  ir[11];
    uint8_t  irptr;
    uint8_t  status; /* latched status bits */
    uint16_t scraddr;
    uint16_t cursaddr;
    uint8_t  dispen;
    uint8_t  cursen;

    /* Hardware status and control, ports 0x188-0x195. */
    uint8_t  reg189;
    uint8_t  reg18a;
    uint8_t  reg18c;
    uint8_t  reg193;
    uint8_t  reg195;
    uint8_t  diag; /* diagnostic readback latches, seen at 0x188 bits 6-4 */
    uint16_t word[3];
    uint8_t  wordff[3];

    /* All Points Addressable registers, ports 0x196-0x19B. */
    uint8_t  reg196;
    uint8_t  reg197;
    uint8_t  reg199;
    uint8_t  reg19a;
    uint8_t  reg19b;
    uint16_t reg198;
    uint8_t  apa_ff; /* 0x198 low/high byte flip-flop */

    /* Emulated MDA/CGA register set, ports 0x3B0-0x3BF and 0x3D0-0x3DF. */
    uint8_t crtc[32];
    uint8_t crtcreg;
    uint8_t mode3d8;
    uint8_t col3d9;
    uint8_t mode3b8;
    uint8_t stat3da;

    /* Configuration. */
    uint8_t colour;  /* 5272 colour monitor rather than an MDA mono one */
    uint8_t has_apa; /* All Points Addressable board fitted */

    /* Derived timings. */
    int charheight;
    int rows;
    int cols;
    int htotal;
    int vtotal;
    int dispheight;

    uint64_t   dispontime;
    uint64_t   dispofftime;
    pc_timer_t timer;

    int linepos;
    int displine;
    int firstline;
    int lastline;
    int blink;
    /* Live vertical retrace, following the raster.  Deliberately separate from
       the LATCHED STAT_VBLANK in the 2672 status register: the adapter
       self-test at C05DB waits for this to go high and then low again, so a
       latched bit that only a command can clear would trap it. */
    uint8_t vsync;
    int monitor_index;
    int prev_monitor_index;

    uint32_t pal[8];

    void (*render)(struct pc3270_t *);
} pc3270_t;

/* Elliott's measured 5271 palette, in IBM CGA colour order.  The 5271 has no
   high intensity -- bright and normal colours look identical -- so eight
   entries cover the whole card. */
static const uint8_t pc3270_pal_color[8][3] = {
    { 0x00, 0x00, 0x00 },
    { 0x60, 0x80, 0xa8 },
    { 0x00, 0x80, 0x00 },
    { 0x60, 0xc0, 0xa8 },
    { 0xa8, 0x30, 0x00 },
    { 0xc0, 0x60, 0x80 },
    { 0xa0, 0x80, 0x00 },
    { 0xa0, 0xa0, 0x80 }
};

/* The 3270 plane numbers its colours differently from CGA: green and red are
   swapped over.  Maps a 3270 attribute colour to a CGA palette index. */
static const uint8_t pc3270_attr_to_cga[8] = { 0, 1, 4, 5, 2, 3, 6, 7 };

static video_timings_t timing_3270pc = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

#ifdef ENABLE_PC3270_LOG
int pc3270_do_log = ENABLE_PC3270_LOG;

static void
pc3270_log(const char *fmt, ...)
{
    va_list ap;

    if (pc3270_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define pc3270_log(fmt, ...)
#endif

static void pc3270_recalctimings(pc3270_t *dev);

/**********************************************************************
 * Memory
 **********************************************************************/

/* The 3270 plane holds four bytes per cell: character, attribute, and two
   that belong to the Programmed Symbols board.  With no PSS fitted the latter
   cannot be written and read back as 0xFF. */
static void
pc3270_write_3270(uint32_t addr, uint8_t val, void *priv)
{
    pc3270_t *dev = (pc3270_t *) priv;

    addr &= (PC3270_TRAM_SIZE - 1);
    if (addr & 2)
        return;

    dev->tram[addr] = val;
}

static uint8_t
pc3270_read_3270(uint32_t addr, void *priv)
{
    const pc3270_t *dev = (pc3270_t *) priv;

    addr &= (PC3270_TRAM_SIZE - 1);
    if (addr & 2)
        return 0xff;

    return dev->tram[addr];
}

/* B0000: the emulated MDA text plane.  Only 4K exists, so it repeats. */
static void
pc3270_write_mda(uint32_t addr, uint8_t val, void *priv)
{
    pc3270_t *dev = (pc3270_t *) priv;

    dev->cram[addr & (PC3270_CRAM_SIZE - 1)] = val;
}

static uint8_t
pc3270_read_mda(uint32_t addr, void *priv)
{
    const pc3270_t *dev = (pc3270_t *) priv;

    return dev->cram[addr & (PC3270_CRAM_SIZE - 1)];
}

static int
pc3270_graphics(const pc3270_t *dev)
{
    return dev->has_apa && (dev->mode3d8 & 0x02);
}

/* B8000 is the same 4K text plane in text modes and the APA board's 32K
   graphics buffer in graphics modes.  One branching handler avoids having to
   swap mappings on every mode change. */
static void
pc3270_write_cga(uint32_t addr, uint8_t val, void *priv)
{
    pc3270_t *dev = (pc3270_t *) priv;

    if (pc3270_graphics(dev))
        dev->apa[addr & (PC3270_APA_SIZE - 1)] = val;
    else
        dev->cram[addr & (PC3270_CRAM_SIZE - 1)] = val;
}

static uint8_t
pc3270_read_cga(uint32_t addr, void *priv)
{
    const pc3270_t *dev = (pc3270_t *) priv;

    if (pc3270_graphics(dev))
        return dev->apa[addr & (PC3270_APA_SIZE - 1)];

    return dev->cram[addr & (PC3270_CRAM_SIZE - 1)];
}

/* 86Box maps memory in 4K pages, so a 2K or 6K window still claims whole pages.
   Both handlers therefore have to decode their full page and read as open bus
   past the end of the image -- otherwise the 2K image at C0000 would mirror
   into C0800, where the BIOS option-ROM scan would find a second valid 55 AA
   header and run the self-test twice, and the 6K image at CA000 would run off
   the end of the 8K buffer for CB800-CBFFF. */
static uint8_t
pc3270_read_romlo(uint32_t addr, void *priv)
{
    const pc3270_t *dev = (pc3270_t *) priv;

    addr &= 0x0fff;

    return (addr < 0x0800) ? dev->rom[addr] : 0xff;
}

static uint8_t
pc3270_read_romhi(uint32_t addr, void *priv)
{
    const pc3270_t *dev = (pc3270_t *) priv;

    addr &= 0x1fff;

    return (addr < 0x1800) ? dev->rom[0x0800 + addr] : 0xff;
}

/**********************************************************************
 * SCN2672 PVTC, ports 0x180-0x187
 **********************************************************************/

static void
pc3270_2672_command(pc3270_t *dev, uint8_t val)
{
    if (val == 0x00) {
        /* Master reset; the ROM sends it four times to start initialisation. */
        dev->irptr  = 0;
        dev->status = 0;
        dev->dispen = 0;
        dev->cursen = 0;
        pc3270_recalctimings(dev);
        return;
    }

    switch (val & 0xe0) {
        case 0x00:
            if ((val & 0xf0) == 0x10) /* 0x10-0x1A: load IR pointer */
                dev->irptr = val & 0x0f;
            break;

        case 0x20: /* enable/disable cursor, display and light pen */
            if (val & 0x10)
                dev->cursen = val & 0x01;
            if (val & 0x08)
                dev->dispen = val & 0x01;
            pc3270_recalctimings(dev);
            break;

        case 0x40: /* clear the status latches named in bits 4-0 */
            dev->status &= ~(val & 0x1f);
            break;

        case 0x60: /* enable interrupt on the named latches */
        case 0x80: /* disable interrupt on the named latches */
            /* The 2672's interrupt output does not appear to be wired to the
               bus on this card: the adapter ROM's IRQ2 handler polls port
               0x18C for the display and never the 2672.  Accepted, but
               deliberately never raised. */
            break;

        default:
            /* 0xA0-0xBF are the delayed commands that mediate CPU access to
               display memory in independent-buffer mode.  The 5271 maps its
               buffers directly, so these complete immediately. */
            dev->status |= STAT_READY;
            break;
    }
}

static void
pc3270_out_2672(pc3270_t *dev, uint16_t addr, uint8_t val)
{
    switch (addr & 7) {
        case 0: /* initialization register; the pointer auto-increments */
            if (dev->irptr < (uint8_t) (sizeof(dev->ir) / sizeof(dev->ir[0]))) {
                dev->ir[dev->irptr] = val;
                dev->irptr++;
            }
            pc3270_recalctimings(dev);
            break;

        case 1:
            pc3270_2672_command(dev, val);
            break;

        case 2:
            dev->scraddr = (dev->scraddr & 0x3f00) | val;
            break;
        case 3:
            dev->scraddr = (dev->scraddr & 0x00ff) | ((val & 0x3f) << 8);
            break;

        case 4:
            dev->cursaddr = (dev->cursaddr & 0x3f00) | val;
            break;
        case 5:
            dev->cursaddr = (dev->cursaddr & 0x00ff) | ((val & 0x3f) << 8);
            break;

        default: /* 0x186/0x187: light pen and display pointer, not emulated */
            break;
    }
}

static uint8_t
pc3270_in_2672(pc3270_t *dev, uint16_t addr)
{
    switch (addr & 7) {
        case 0:
            return dev->ir[dev->irptr < (uint8_t) (sizeof(dev->ir) / sizeof(dev->ir[0])) ? dev->irptr : 0];

        case 1:
            /* RDFLG and Ready are held high: every operation this card asks
               for completes at once, and the ROM spins if they read low. */
            return dev->status | STAT_RDFLG | STAT_READY;

        case 2:
            return dev->scraddr & 0xff;
        case 3:
            return (dev->scraddr >> 8) & 0x3f;

        case 4:
            return dev->cursaddr & 0xff;
        case 5:
            return (dev->cursaddr >> 8) & 0x3f;

        default:
            break;
    }

    return 0xff;
}

/**********************************************************************
 * I/O
 **********************************************************************/

/* Writing an emulated CGA register raises IRQ2 so the adapter ROM can carry
   out the mode change in software.  Bit 4 flags a write to the mode control
   register, bit 5 a write to the cursor position or size. */
static void
pc3270_cga_trap(pc3270_t *dev, uint8_t source)
{
    dev->reg18c |= source | 0x80;

    if (dev->reg18c & 0x40)
        picint(1 << 2);
}

static void
pc3270_out(uint16_t addr, uint8_t val, void *priv)
{
    pc3270_t *dev = (pc3270_t *) priv;

    if (addr < 0x0200) {
        if (addr < 0x0188) {
            pc3270_out_2672(dev, addr, val);
            return;
        }

        switch (addr) {
            case 0x0189:
                /* Bits 3-0 are the high nibble of the PC offset, bit 5 arms
                   the diagnostic readback, and any write clears its latches. */
                dev->reg189 = val;
                dev->diag   = 0;
                break;

            case 0x018a:
                dev->reg18a = val;
                break;

            case 0x018b:
                /* With bit 7 clear the low nibble is a bit set/clear command
                   on port 0x18A: even clears, odd sets, bit index = n >> 1.
                   With bit 7 set the whole 12-bit offset resets, which is what
                   the adapter ROM does when it writes 0x90 at startup. */
                if (val & 0x80) {
                    dev->reg18a = 0x00;
                    dev->reg189 &= 0xf0;
                } else if (val & 0x01)
                    dev->reg18a |= 1 << ((val >> 1) & 7);
                else
                    dev->reg18a &= (uint8_t) ~(1 << ((val >> 1) & 7));
                break;

            case 0x018c:
                /* Writing acknowledges the pending interrupt; only the enable
                   bit is retained. */
                dev->reg18c = val & 0x40;
                break;

            case 0x0190:
            case 0x0191:
            case 0x0192: {
                /* Each is a little-endian word taking two successive writes.
                   Stored for readback only: they position the raster within
                   the tube, which has no meaning for a framebuffer. */
                int i = addr - 0x0190;

                if (dev->wordff[i])
                    dev->word[i] = (dev->word[i] & 0x00ff) | (val << 8);
                else
                    dev->word[i] = (dev->word[i] & 0xff00) | val;
                dev->wordff[i] ^= 1;
                break;
            }

            case 0x0193:
                dev->reg193 = val;
                break;

            case 0x0195:
                /* Programmed Symbols mask.  No PSS board is emulated, so this
                   is stored and otherwise ignored. */
                dev->reg195 = val;
                break;

            /* All Points Addressable registers. */
            case 0x0196:
                dev->reg196 = val;
                pc3270_recalctimings(dev);
                break;

            case 0x0197:
                /* Pixel-level horizontal shift of the graphics viewport.
                   Stored; sub-character panning is not modelled. */
                dev->reg197 = val;
                break;

            case 0x0198:
                if (dev->apa_ff)
                    dev->reg198 = (dev->reg198 & 0x00ff) | (val << 8);
                else
                    dev->reg198 = (dev->reg198 & 0xff00) | val;
                dev->apa_ff ^= 1;
                break;

            case 0x0199:
                dev->reg199 = val;
                break;

            case 0x019a:
                dev->reg19a = val;
                pc3270_recalctimings(dev);
                break;

            case 0x019b:
                /* Arms the next write: 0x3A selects 0x198 and resets its byte
                   flip-flop, 0x5A selects 0x199, 0x9A selects 0x19A. */
                dev->reg19b = val;
                if (val == 0x3a)
                    dev->apa_ff = 0;
                break;

            default:
                break;
        }
        return;
    }

    /* Emulated MDA and CGA register sets. */
    switch (addr) {
        case 0x03b4:
        case 0x03d4:
            dev->crtcreg = val & 31;
            break;

        case 0x03b5:
        case 0x03d5:
            dev->crtc[dev->crtcreg] = val;
            /* Cursor size (0x0A/0x0B) and cursor position (0x0E/0x0F). */
            if (((dev->crtcreg >= 0x0a) && (dev->crtcreg <= 0x0b)) || ((dev->crtcreg >= 0x0e) && (dev->crtcreg <= 0x0f)))
                pc3270_cga_trap(dev, 0x20);
            break;

        case 0x03b8:
            dev->mode3b8 = val;
            pc3270_recalctimings(dev);
            break;

        case 0x03d8:
            dev->mode3d8 = val;
            pc3270_cga_trap(dev, 0x10);
            pc3270_recalctimings(dev);
            break;

        case 0x03d9:
            dev->col3d9 = val;
            break;

        default:
            break;
    }
}

static uint8_t
pc3270_in(uint16_t addr, void *priv)
{
    pc3270_t *dev = (pc3270_t *) priv;
    uint8_t   ret;

    if (addr < 0x0200) {
        if (addr < 0x0188)
            return pc3270_in_2672(dev, addr);

        switch (addr) {
            case 0x0188:
                ret = dev->diag & 0x70;
                /* Live retrace, not the latched 2672 status bit -- see vsync. */
                if (dev->vsync)
                    ret |= 0x80;
                /* Bit 3 stays clear: no Programmed Symbols board. */
                if (dev->has_apa)
                    ret |= 0x04;
                ret |= 0x02; /* a monitor is always attached */
                if (dev->colour)
                    ret |= 0x01;
                return ret;

            case 0x0189:
                /* Must read back exactly what was written: the adapter
                   presence test stores 0x00 and 0xFF and compares. */
                return dev->reg189;

            case 0x018a:
                return dev->reg18a;

            case 0x018c:
                return dev->reg18c;

            case 0x0190:
            case 0x0191:
            case 0x0192:
                /* Live down-counters on real hardware; they run to zero, and
                   zero is the state anything waiting on them wants to see. */
                return 0x00;

            case 0x0193:
            case 0x0194:
                return 0xff;

            case 0x0197:
                /* Bit 7 must read 0 or the APA self-test fails.  Bit 4 is
                   vertical retrace and bit 3 mirrors bit 3 of port 0x196;
                   bits 2-0 read as zero, which is what the diagnostic expects
                   when it writes 1, 2, 4, 8 to 0x196 and masks with 0x8F. */
                ret = dev->reg196 & 0x08;
                if (dev->vsync)
                    ret |= 0x10;
                return ret;

            case 0x0198:
            case 0x0199:
            case 0x019a:
                return 0x00;

            default:
                break;
        }

        return 0xff;
    }

    switch (addr) {
        case 0x03b4:
        case 0x03d4:
            return dev->crtcreg;

        case 0x03b5:
        case 0x03d5:
            /* Unlike a real 6845 every register reads back, because the
               adapter ROM's interrupt handler reads the cursor registers. */
            return dev->crtc[dev->crtcreg];

        case 0x03b8:
            return dev->mode3b8;

        case 0x03d8:
            /* Read/write on this card, unlike a real CGA. */
            return dev->mode3d8;

        case 0x03d9:
            return dev->col3d9;

        case 0x03ba:
        case 0x03da:
            /* Stubbed on the real card too: alternate reads give 01, 08. */
            dev->stat3da ^= 0x09;
            return dev->stat3da;

        default:
            break;
    }

    return 0xff;
}

/**********************************************************************
 * Rendering
 **********************************************************************/

static void
pc3270_render_blank(pc3270_t *dev)
{
    uint32_t *p = &((uint32_t *) buffer32->line[dev->displine])[0];

    for (int x = 0; x < (dev->cols * 9); x++)
        p[x] = dev->pal[0];
}

/* Latches whatever colour was drawn, for the self-test's diagnostic readback
   at port 0x188.  On a mono monitor only the video and intensity lines exist,
   which the card reports through the red and blue bits. */
static void
pc3270_diag(pc3270_t *dev, uint32_t colour)
{
    if (!(dev->reg189 & 0x20) || (colour == dev->pal[0]))
        return;

    if (dev->colour) {
        if (colour & 0x00ff0000)
            dev->diag |= 0x40; /* red   */
        if (colour & 0x000000ff)
            dev->diag |= 0x20; /* blue  */
        if (colour & 0x0000ff00)
            dev->diag |= 0x10; /* green */
    } else
        dev->diag |= 0x60;
}

static void
pc3270_render_text(pc3270_t *dev)
{
    uint32_t *p         = &((uint32_t *) buffer32->line[dev->displine])[0];
    int       scanline  = dev->displine % dev->charheight;
    int       row       = dev->displine / dev->charheight;
    int       underline = dev->ir[7] & 0x0f;
    int       curstop   = (dev->ir[6] >> 4) & 0x0f;
    int       cursbot   = dev->ir[6] & 0x0f;
    uint16_t  pcoffset  = (uint16_t) (((dev->reg189 & 0x0f) << 8) | dev->reg18a);
    int       blinkon   = dev->blink & 0x10;
    /* IR7 bit 5 ENABLES cursor blink; clear means a solid cursor.  The adapter
       ROM loads 0x8C at init and 0xAC once the self-test passes. */
    int       cursblink = (dev->ir[7] & 0x20) ? (blinkon != 0) : 1;

    for (int x = 0; x < dev->cols; x++) {
        uint16_t       ma   = (uint16_t) ((dev->scraddr + (row * dev->cols) + x) & 0x3fff);
        const uint8_t *cell = &dev->tram[(ma & 0x7ff) * 4];
        uint8_t        chr  = cell[0];
        uint8_t        attr = cell[1];
        uint16_t       glyph;
        uint16_t       rowbits;
        uint32_t       fg;
        uint32_t       bg;
        int            ul    = 0;
        int            blink = 0;

        if (chr != 0xff) {
            /* Opaque 3270 cell. */
            int sel = (attr >> 6) & 3;

            if (dev->colour) {
                fg = dev->pal[pc3270_attr_to_cga[(attr >> 3) & 7]];
                bg = dev->pal[pc3270_attr_to_cga[attr & 7]];
            } else {
                fg = dev->pal[(attr & 0x20) ? 7 : 2];
                bg = dev->pal[0];
            }

            blink = (sel == 1) && !blinkon;
            if (sel == 2) {
                uint32_t t = fg;

                fg = bg;
                bg = t;
            }
            ul    = (sel == 3);
            glyph = chr;
        } else {
            /* Transparent: the emulated CGA/MDA plane shows through. */
            uint16_t ci = (uint16_t) (((ma + pcoffset) & 0x7ff) * 2);
            uint8_t  ec = dev->cram[ci];
            uint8_t  ea = dev->cram[ci + 1];

            if (dev->colour) {
                /* IBM CGA order, and bit 3 is ignored: the 5271 has no high
                   intensity, so bright and normal colours are identical. */
                fg = dev->pal[ea & 0x07];
                bg = dev->pal[(ea >> 4) & 0x07];
            } else {
                /* Ordinary MDA semantics on a mono monitor.  Elliott documents
                   only the colour attribute system for this plane, so the mono
                   mapping here is inferred. */
                if ((ea & 0x77) == 0x70) {
                    fg = dev->pal[0];
                    bg = dev->pal[7];
                } else {
                    fg = dev->pal[(ea & 0x08) ? 7 : 2];
                    bg = dev->pal[0];
                    ul = ((ea & 0x07) == 0x01);
                }
            }

            blink = (ea & 0x80) && !blinkon;
            glyph = 0x100 | ec;
        }

        if (blink)
            fg = bg;

        rowbits = dev->font[glyph][scanline] & 0x1ff;

        for (int n = 0; n < 9; n++)
            p[(x * 9) + n] = (rowbits & (0x100 >> n)) ? fg : bg;

        if (ul && (scanline == underline)) {
            for (int n = 0; n < 9; n++)
                p[(x * 9) + n] = fg;
        }

        if ((ma == dev->cursaddr) && dev->cursen && cursblink && (scanline >= curstop) && (scanline <= cursbot)) {
            /* The cursor is OR'd into the video output on the way to the
               monitor, so it lights up even on a cell whose own colours would
               leave it invisible.  The self-test at C000:068E relies on this:
               it enables the cursor over a blank screen and requires the
               diagnostic readback to see video, which never happens if the
               cursor is painted in the cell's foreground colour. */
            uint32_t cur = (fg == bg) ? dev->pal[7] : fg;

            for (int n = 0; n < 9; n++)
                p[(x * 9) + n] = cur;
        }
    }
}

/* Bytes per line of the graphics framebuffer.  The Graphics Width Counter
   holds words-per-line less one; zero and anything above 0x2C mean 0x2C.
   0x2C gives the 90 bytes of the native modes, 0x27 the 80 of CGA. */
static int
pc3270_apa_stride(const pc3270_t *dev)
{
    int w = dev->reg19a;

    if ((w == 0) || (w > 0x2c))
        w = 0x2c;

    return (w + 1) * 2;
}

/* The CGA four-colour palettes, minus the intensity bit this card does not
   have.  Entry 0 comes from the colour select register. */
static void
pc3270_gfx_palette(const pc3270_t *dev, uint8_t *pal)
{
    pal[0] = dev->col3d9 & 0x07;

    if (dev->mode3d8 & 0x04) { /* black and white */
        pal[1] = 3;
        pal[2] = 4;
        pal[3] = 7;
    } else if (dev->col3d9 & 0x20) { /* cyan / magenta / white */
        pal[1] = 3;
        pal[2] = 5;
        pal[3] = 7;
    } else { /* green / red / brown */
        pal[1] = 2;
        pal[2] = 4;
        pal[3] = 6;
    }
}

/* Native 720x350 mono: a plain linear framebuffer, 350 lines of 90 bytes,
   no interleaving. */
static void
pc3270_render_apa_1bpp(pc3270_t *dev)
{
    uint32_t *p      = &((uint32_t *) buffer32->line[dev->displine])[0];
    int       stride = pc3270_apa_stride(dev);
    uint32_t  addr   = (uint32_t) (dev->displine * stride);
    int       width  = dev->cols * 9;

    for (int x = 0; x < width; x += 8) {
        uint8_t data = dev->apa[(addr + (uint32_t) (x >> 3)) & (PC3270_APA_SIZE - 1)];

        for (int n = 0; n < 8; n++)
            p[x + n] = dev->pal[(data & (0x80 >> n)) ? 7 : 0];
    }
}

/* Native 360x350 in four colours using the CGA palette.  Same linear layout,
   two bits per pixel, each pixel drawn twice to fill the 720-pixel raster. */
static void
pc3270_render_apa_2bpp(pc3270_t *dev)
{
    uint32_t *p      = &((uint32_t *) buffer32->line[dev->displine])[0];
    int       stride = pc3270_apa_stride(dev);
    uint32_t  addr   = (uint32_t) (dev->displine * stride);
    int       width  = dev->cols * 9;
    uint8_t   pal[4];

    pc3270_gfx_palette(dev, pal);

    for (int x = 0; x < width; x += 8) {
        uint8_t data = dev->apa[(addr + (uint32_t) (x >> 3)) & (PC3270_APA_SIZE - 1)];

        for (int n = 0; n < 4; n++) {
            uint32_t c = dev->pal[pal[(data >> (6 - (n * 2))) & 3]];

            p[x + (n * 2)]     = c;
            p[x + (n * 2) + 1] = c;
        }
    }
}

/* CGA-compatible graphics: the usual interleaved layout, 80 bytes per line.
   The APA board emits 640 pixels into the card's wider raster and 200 lines
   into its taller one, so the picture sits in the top left and the rest of
   the frame stays blank. */
static void
pc3270_render_cga_1bpp(pc3270_t *dev)
{
    uint32_t *p     = &((uint32_t *) buffer32->line[dev->displine])[0];
    int       width = dev->cols * 9;
    uint32_t  addr;

    if (dev->displine >= 200) {
        pc3270_render_blank(dev);
        return;
    }
    addr = (uint32_t) (((dev->displine & 1) ? 0x2000 : 0x0000) + ((dev->displine >> 1) * 80));

    for (int x = 0; x < width; x++) {
        uint8_t data;

        if (x >= 640) {
            p[x] = dev->pal[0];
            continue;
        }
        data = dev->apa[(addr + (uint32_t) (x >> 3)) & (PC3270_APA_SIZE - 1)];
        p[x] = dev->pal[(data & (0x80 >> (x & 7))) ? 7 : 0];
    }
}

static void
pc3270_render_cga_2bpp(pc3270_t *dev)
{
    uint32_t *p     = &((uint32_t *) buffer32->line[dev->displine])[0];
    int       width = dev->cols * 9;
    uint8_t   pal[4];
    uint32_t  addr;

    if (dev->displine >= 200) {
        pc3270_render_blank(dev);
        return;
    }
    pc3270_gfx_palette(dev, pal);
    addr = (uint32_t) (((dev->displine & 1) ? 0x2000 : 0x0000) + ((dev->displine >> 1) * 80));

    /* 320 pixels doubled to 640: four pixels per byte, so one byte per eight
       raster columns. */
    for (int x = 0; x < width; x++) {
        uint8_t data;

        if (x >= 640) {
            p[x] = dev->pal[0];
            continue;
        }
        data = dev->apa[(addr + (uint32_t) (x >> 3)) & (PC3270_APA_SIZE - 1)];
        p[x] = dev->pal[pal[(data >> (6 - (((x >> 1) & 3) * 2))) & 3]];
    }
}

/**********************************************************************
 * Timings
 **********************************************************************/

static void
pc3270_recalctimings(pc3270_t *dev)
{
    double crtcconst;
    int    hsync;
    int    vfp;
    int    vbp;

    dev->charheight = ((dev->ir[0] >> 3) & 0x1f) + 1;
    dev->rows       = (dev->ir[4] & 0x7f) + 1;
    dev->cols       = dev->ir[5] + 1;

    if ((dev->rows * dev->charheight) > 2048) {
        /* Nonsense geometry from a half-programmed 2672. */
        dev->charheight = 14;
        dev->rows       = 25;
    }
    if (dev->cols > 100)
        dev->cols = 80;

    /* Horizontal total in character clocks, from the equalising constant in
       IR1 and the sync width in IR2.  This reproduces both configurations the
       adapter ROM programs: 98 clocks mono, 102 colour. */
    hsync       = (2 * ((dev->ir[2] >> 3) & 0x0f)) + 2;
    dev->htotal = 2 * (((dev->ir[1] & 0x7f) + 1) + (2 * hsync));

    vfp             = 4 * (((dev->ir[3] >> 5) & 0x07) + 1);
    vbp             = (2 * (dev->ir[3] & 0x1f)) + 4;
    dev->dispheight = dev->rows * dev->charheight;
    dev->vtotal     = dev->dispheight + vfp + 3 + vbp; /* VSYNC is fixed at 3 lines */

    if (dev->htotal < (dev->cols + 1))
        dev->htotal = dev->cols + 1;
    if (dev->vtotal < (dev->dispheight + 1))
        dev->vtotal = dev->dispheight + 1;

    crtcconst        = (double) cpuclock / ((dev->colour ? PC3270_DOT_COLOR : PC3270_DOT_MONO) / 9.0) * (double) (1ULL << 32);
    dev->dispontime  = (uint64_t) ((double) dev->cols * crtcconst);
    dev->dispofftime = (uint64_t) ((double) (dev->htotal - dev->cols) * crtcconst);

    if (!dev->dispontime)
        dev->dispontime = TIMER_USEC;
    if (!dev->dispofftime)
        dev->dispofftime = TIMER_USEC;

    /* Pick the renderer.  Default to blank so an unprogrammed card shows
       black rather than whatever happens to be in the buffers. */
    if (!dev->dispen)
        dev->render = pc3270_render_blank;
    else if (!pc3270_graphics(dev))
        dev->render = pc3270_render_text;
    else if (dev->reg196 & 0x08) {
        /* Native mode, linear framebuffer.  Which of the two native modes is
           selected is taken from the CGA high-resolution bit; that matches the
           mode-set sequences the adapter ROM issues, but is inferred. */
        dev->render = (dev->mode3d8 & 0x10) ? pc3270_render_apa_1bpp : pc3270_render_apa_2bpp;
    } else
        dev->render = (dev->mode3d8 & 0x10) ? pc3270_render_cga_1bpp : pc3270_render_cga_2bpp;
}

static void
pc3270_poll(void *priv)
{
    pc3270_t *dev = (pc3270_t *) priv;

    dev->prev_monitor_index = monitor_index_global;
    monitor_index_global    = dev->monitor_index;

    if (!dev->linepos) {
        timer_advance_u64(&dev->timer, dev->dispofftime);
        dev->linepos = 1;

        if (dev->displine < dev->dispheight) {
            if (dev->displine < dev->firstline) {
                dev->firstline = dev->displine;
                video_wait_for_buffer();
            }
            dev->lastline = dev->displine;

            dev->render(dev);

            /* The diagnostic readback latches whatever actually reached the
               raster, so it has to sample the rendered scanline -- the APA
               self-test at C000:0647 runs in a graphics mode, and only the
               text renderer knows anything about foreground/background. */
            if (dev->reg189 & 0x20) {
                const uint32_t *p     = (const uint32_t *) buffer32->line[dev->displine];
                int             width = dev->cols * 9;

                for (int x = 0; x < width; x++)
                    pc3270_diag(dev, p[x]);
            }

            if (dev->displine == 0)
                dev->status |= STAT_LINEZERO;
        }
    } else {
        timer_advance_u64(&dev->timer, dev->dispontime);
        dev->linepos = 0;
        dev->displine++;

        if (dev->displine == dev->dispheight) {
            dev->status |= STAT_VBLANK; /* latched until a 0x40-0x5F command */
            dev->vsync = 1;             /* live, clears when the raster restarts */
        }

        if (dev->displine >= dev->vtotal) {
            int x = dev->cols * 9;
            int y = dev->dispheight;

            dev->displine = 0;
            dev->vsync    = 0;

            if ((x != xsize) || (y != ysize) || video_force_resize_get()) {
                xsize = x;
                ysize = y;
                if (xsize < 64)
                    xsize = 720;
                if (ysize < 32)
                    ysize = 350;
                set_screen_size(xsize, ysize);

                if (video_force_resize_get())
                    video_force_resize_set(0);
            }

            video_blit_memtoscreen(0, 0, xsize, ysize);
            frames++;

            video_res_x = xsize;
            video_res_y = ysize;
            video_bpp   = pc3270_graphics(dev) ? ((dev->mode3d8 & 0x10) ? 1 : 2) : 0;

            dev->firstline = 1000;
            dev->lastline  = 0;
            dev->blink++;
        }
    }

    monitor_index_global = dev->prev_monitor_index;
}

/**********************************************************************
 * Device
 **********************************************************************/

static int
pc3270_load_font(pc3270_t *dev)
{
    uint8_t *lo  = NULL;
    uint8_t *hi  = NULL;
    FILE    *fp;
    int      ret = 0;

    lo = calloc(1, 8192);
    hi = calloc(1, 8192);
    if ((lo == NULL) || (hi == NULL))
        goto done;

    fp = rom_fopen(PC3270_FONT_LO, "rb");
    if (fp == NULL)
        goto done;
    ret = (fread(lo, 1, 8192, fp) == 8192);
    fclose(fp);
    if (!ret)
        goto done;

    ret = 0;
    fp  = rom_fopen(PC3270_FONT_HI, "rb");
    if (fp == NULL)
        goto done;
    ret = (fread(hi, 1, 8192, fp) == 8192);
    fclose(fp);
    if (!ret)
        goto done;

    /* One ROM holds the left-hand eight pixels of each character, the other
       just the ninth.  Column 9 is genuinely independent here -- it differs
       from column 8 in 331 of the 512 glyphs -- so the MDA trick of
       replicating column 8 for characters 0xC0-0xDF must not be used. */
    for (int c = 0; c < 512; c++) {
        for (int r = 0; r < 16; r++)
            dev->font[c][r] = (uint16_t) (((uint16_t) lo[(c << 4) | r] << 1) | (hi[(c << 4) | r] ? 1 : 0));
    }

done:
    free(lo);
    free(hi);

    return ret;
}

static void
pc3270_free(pc3270_t *dev)
{
    free(dev->apa);
    free(dev->cram);
    free(dev->tram);
    free(dev->rom);
    free(dev);
}

static void *
pc3270_init(UNUSED(const device_t *info))
{
    pc3270_t *dev;
    FILE     *fp;

    dev = calloc(1, sizeof(pc3270_t));
    if (dev == NULL)
        return NULL;

    dev->colour  = !!device_get_config_int("monitor");
    dev->has_apa = !!device_get_config_int("apa");

    dev->rom  = calloc(1, PC3270_ROM_SIZE);
    dev->tram = calloc(1, PC3270_TRAM_SIZE);
    dev->cram = calloc(1, PC3270_CRAM_SIZE);
    dev->apa  = calloc(1, PC3270_APA_SIZE);

    if ((dev->rom == NULL) || (dev->tram == NULL) || (dev->cram == NULL) || (dev->apa == NULL)) {
        pc3270_free(dev);
        return NULL;
    }

    /* Character 0xFF is a transparent cell, so a cleared 3270 plane lets the
       emulated CGA/MDA screen through, which is how the card powers up. */
    memset(dev->tram, 0xff, PC3270_TRAM_SIZE);

    if (!pc3270_load_font(dev)) {
        pc3270_log("3270 PC: font ROMs missing or short\n");
        pc3270_free(dev);
        return NULL;
    }

    memset(dev->rom, 0xff, PC3270_ROM_SIZE);
    fp = rom_fopen(PC3270_ROM_PATH, "rb");
    if (fp != NULL) {
        if (fread(dev->rom, 1, PC3270_ROM_SIZE, fp) != PC3270_ROM_SIZE)
            pc3270_log("3270 PC: short adapter ROM\n");
        fclose(fp);
    } else
        pc3270_log("3270 PC: adapter ROM missing\n");

    /* Not MDA and not CGA: the planar video switches must read "other", which
       is what the 5271 requires and what leaves the adapter ROM free to set
       the equipment word itself. */
    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_3270pc);

    for (uint8_t c = 0; c < 8; c++) {
        if (dev->colour)
            dev->pal[c] = makecol(pc3270_pal_color[c][0], pc3270_pal_color[c][1], pc3270_pal_color[c][2]);
        else {
            /* Mono: black, normal and bright.  Index 2 is the normal
               foreground and 7 the bright one, matching the colour table. */
            static const uint8_t level[8] = { 0x00, 0x00, 0xc0, 0xc0, 0xc0, 0xc0, 0xff, 0xff };
            uint8_t              v        = level[c];

            switch (device_get_config_int("rgb_type")) {
                case 1: /* green */
                    dev->pal[c] = makecol(v >> 2, v, v >> 2);
                    break;
                case 2: /* amber */
                    dev->pal[c] = makecol(v, (uint8_t) ((v * 3) / 4), 0);
                    break;
                default: /* white */
                    dev->pal[c] = makecol(v, v, v);
                    break;
            }
        }
    }

    mem_mapping_add(&dev->mapping_3270, 0xa0000, 0x02000,
                    pc3270_read_3270, NULL, NULL,
                    pc3270_write_3270, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_add(&dev->mapping_mda, 0xb0000, 0x08000,
                    pc3270_read_mda, NULL, NULL,
                    pc3270_write_mda, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_add(&dev->mapping_cga, 0xb8000, 0x08000,
                    pc3270_read_cga, NULL, NULL,
                    pc3270_write_cga, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, dev);

    /* The adapter ROM appears twice: 2K of video self-test where the system
       BIOS scans for option ROMs at C0000, and 6K of mode-change, interrupt
       and keyboard code at CA000.  Both images carry their own 55 AA header
       and checksum. */
    mem_mapping_add(&dev->mapping_romlo, 0xc0000, 0x00800,
                    pc3270_read_romlo, NULL, NULL,
                    NULL, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_add(&dev->mapping_romhi, 0xca000, 0x01800,
                    pc3270_read_romhi, NULL, NULL,
                    NULL, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, dev);

    io_sethandler(0x0180, 0x0020, pc3270_in, NULL, NULL, pc3270_out, NULL, NULL, dev);
    io_sethandler(0x03b0, 0x0010, pc3270_in, NULL, NULL, pc3270_out, NULL, NULL, dev);
    io_sethandler(0x03d0, 0x0010, pc3270_in, NULL, NULL, pc3270_out, NULL, NULL, dev);

    overscan_x         = overscan_y = 0;
    dev->monitor_index = monitor_index_global;

    /* Power-on defaults matching the mono configuration.  The adapter ROM
       reprograms every one of these during its own initialisation. */
    dev->ir[0]  = 0x68; /* 14-line character cell */
    dev->ir[1]  = 0x18;
    dev->ir[2]  = 0x28;
    dev->ir[3]  = 0x40;
    dev->ir[4]  = 0x98; /* 25 rows */
    dev->ir[5]  = 0x4f; /* 80 columns */
    dev->ir[6]  = 0xdd;
    dev->ir[7]  = 0x8c;
    dev->ir[9]  = 0xf0;
    dev->ir[10] = 0xff;
    dev->reg19a = 0x27;

    dev->firstline = 1000;
    pc3270_recalctimings(dev);
    timer_add(&dev->timer, pc3270_poll, dev, 1);

    return dev;
}

static void
pc3270_close(void *priv)
{
    pc3270_free((pc3270_t *) priv);
}

static void
pc3270_speed_changed(void *priv)
{
    pc3270_t *dev = (pc3270_t *) priv;

    pc3270_recalctimings(dev);
}

static int
pc3270_available(void)
{
    return rom_present(PC3270_ROM_PATH) && rom_present(PC3270_FONT_LO) && rom_present(PC3270_FONT_HI);
}

static const device_config_t pc3270_config[] = {
  // clang-format off
    {
        .name           = "monitor",
        .description    = "Monitor",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "IBM 5272 colour",     .value = 1 },
            { .description = "IBM 5151 monochrome", .value = 0 },
            { .description = ""                                }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "rgb_type",
        .description    = "Monochrome display type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Green", .value = 1 },
            { .description = "Amber", .value = 2 },
            { .description = "White", .value = 0 },
            { .description = ""                  }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "apa",
        .description    = "All Points Addressable board",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t ibm3270pc_vid_device = {
    .name          = "IBM 3270 PC Display Adapter",
    .internal_name = "ibm3270pc_vid",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = pc3270_init,
    .close         = pc3270_close,
    .reset         = NULL,
    .available     = pc3270_available,
    .speed_changed = pc3270_speed_changed,
    .force_redraw  = NULL,
    .config        = pc3270_config
};
