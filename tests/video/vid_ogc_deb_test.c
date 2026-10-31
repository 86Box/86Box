/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Tests of the Olivetti M24 Display Enhancement Board (GO329):
 *          its status bits, the LUT load sequence AT&T's DEDRIVER.DEV
 *          uses, and how one drawn line is passed through the LUT.
 *
 *          The card is included as C, for its private functions.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/video/vid_cga_olivetti.c"

#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            fprintf(stderr, "%s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                   \
            fputc('\n', stderr);                            \
            failures++;                                     \
        }                                                   \
    } while (0)

monitor_t   monitors[MONITORS_NUM];
int         monitor_index_global;
cpu_state_t cpu_state;
uint64_t    tsc;
uint64_t    CGACONST;
double      cpuclock;
int         enable_overscan;
int         frames;
int         is_nec;
uint8_t     fontdatm[2048][16];

const device_config_t cga_config[] = { { .name = "", .description = "", .type = CONFIG_END } };

/* Only the DEB's own paths run; nothing below is reached by them. */
uint8_t cga_in(uint16_t addr, void *priv) { (void) addr; (void) priv; return 0; }
void cga_out(uint16_t addr, uint8_t val, void *priv) { (void) addr; (void) val; (void) priv; }
void cga_poll(void *priv) { (void) priv; }
void cga_comp_init(int revision) { (void) revision; }
void cgapal_rebuild_monitor(int monitor_index) { (void) monitor_index; }
int device_get_config_int(const char *name) { (void) name; return 0; }
void hline(bitmap_t *b, int x1, int y, int x2, uint32_t col) { (void) b; (void) x1; (void) y; (void) x2; (void) col; }
void set_screen_size(int x, int y) { (void) x; (void) y; }
void sub_cycles(int c) { (void) c; }
void sub_cycles_vx0(int c) { (void) c; }
void timer_enable(pc_timer_t *timer) { (void) timer; }
void timer_add(pc_timer_t *timer, void (*callback)(void *priv), void *priv, int start_timer) { (void) timer; (void) callback; (void) priv; (void) start_timer; }
void video_blit_memtoscreen_monitor(int x, int y, int w, int h, int monitor_index) { (void) x; (void) y; (void) w; (void) h; (void) monitor_index; }
uint8_t video_force_resize_get_monitor(int monitor_index) { (void) monitor_index; return 0; }
void video_force_resize_set_monitor(uint8_t res, int monitor_index) { (void) res; (void) monitor_index; }
void video_inform_monitor(int type, const video_timings_t *ptr, int monitor_index) { (void) type; (void) ptr; (void) monitor_index; }
void video_lightpen_check_trigger_strobe(int x_offset, int y, int x_offset_from_hsync, int firstline, double pix_clock, int monitor_used) { (void) x_offset; (void) y; (void) x_offset_from_hsync; (void) firstline; (void) pix_clock; (void) monitor_used; }
void video_lightpen_hsync(void) { }
void video_lightpen_vsync(void) { }
void video_load_font(char *fn, int format, int offset) { (void) fn; (void) format; (void) offset; }
void video_process_8_monitor(int x, int y, int monitor_index) { (void) x; (void) y; (void) monitor_index; }
void video_wait_for_buffer_monitor(int monitor_index) { (void) monitor_index; }
void io_sethandler(uint16_t base, uint16_t size,
                   uint8_t (*inb)(uint16_t port, void *priv), uint16_t (*inw)(uint16_t port, void *priv), uint32_t (*inl)(uint16_t port, void *priv),
                   void (*outb)(uint16_t port, uint8_t val, void *priv), void (*outw)(uint16_t port, uint16_t val, void *priv), void (*outl)(uint16_t port, uint32_t val, void *priv),
                   void *priv)
{
    (void) base; (void) size; (void) inb; (void) inw; (void) inl; (void) outb; (void) outw; (void) outl; (void) priv;
}
void mem_mapping_add(mem_mapping_t *mapping, uint32_t base, uint32_t size,
                     uint8_t (*read_b)(uint32_t addr, void *priv), uint16_t (*read_w)(uint32_t addr, void *priv), uint32_t (*read_l)(uint32_t addr, void *priv),
                     void (*write_b)(uint32_t addr, uint8_t val, void *priv), void (*write_w)(uint32_t addr, uint16_t val, void *priv), void (*write_l)(uint32_t addr, uint32_t val, void *priv),
                     uint8_t *exec, uint32_t flags, void *priv)
{
    (void) mapping; (void) base; (void) size; (void) read_b; (void) read_w; (void) read_l; (void) write_b; (void) write_w; (void) write_l; (void) exec; (void) flags; (void) priv;
}

static int      failures;
static bitmap_t screen;
static uint32_t pixels[4][656];

/* Out 3DDh/3DFh exactly as DEDRIVER.DEV loads the LUT (its code at 1237h). */
static void
load_lut(ogc_t *ogc, const uint8_t *lut)
{
    ogc_out(0x3dd, 1, ogc);
    ogc_out(0x3df, 5, ogc); /* counter loads */
    ogc_out(0x3dd, 2, ogc);
    ogc_out(0x3df, 0, ogc);
    ogc_out(0x3dd, 1, ogc);
    ogc_out(0x3df, 7, ogc); /* counter counts */
    ogc_out(0x3dd, 2, ogc);
    for (int i = 0; i < 256; i++)
        ogc_out(0x3df, lut[i], ogc);
}

static void
set_mode(ogc_t *ogc, uint8_t mode)
{
    ogc_out(0x3dd, 1, ogc);
    ogc_out(0x3df, mode, ogc);
}

/* One raster line of mode 40h (6845 R9 = 1), every VDC pixel set to vdc. */
static void
draw(ogc_t *ogc, int y, uint8_t vdc)
{
    ogc->cga.vc       = y >> 2;
    ogc->cga.scanline = (y >> 1) & 1;
    ogc->lineff       = y & 1;
    for (int x = 0; x < 656; x++)
        pixels[0][x] = vdc + 16;
    deb_line(ogc, 640);
}

/* One raster line drawn by ogc_poll(), R9 = 1 and memaddr at its 6845 row. */
static void
poll_line(ogc_t *ogc, int y)
{
    ogc->cga.vc       = y >> 2;
    ogc->cga.scanline = (y >> 1) & 1;
    ogc->lineff       = y & 1;
    ogc->cga.memaddr  = ogc->cga.vc * 40;
    ogc->cga.linepos  = 0;
    ogc->cga.displine = 0;
    for (int x = 0; x < 656; x++)
        pixels[0][x] = 0xff;
    ogc_poll(ogc);
}

static uint8_t
irgb(int x)
{
    return (uint8_t) (pixels[0][8 + x] - 16);
}

int
main(void)
{
    ogc_t  *ogc = (ogc_t *) calloc(1, sizeof(ogc_t));
    uint8_t lut[256];

    monitors[0].target_buffer = &screen;
    for (int i = 0; i < 4; i++)
        screen.line[i] = pixels[i];

    ogc->ctrl_addr                           = 0x3de;
    ogc->cga.crtc[CGA_CRTC_MAX_SCANLINE_ADDR] = 1;
    ogc->deb_vram                            = (uint8_t *) calloc(1, 0x18000);

    CHECK((ogc_in(0x3da, ogc) & 0xc0) == 0x00, "a fitted DEB must read EXPID 00");
    set_mode(ogc, DEB_MODE_ENHANID);
    CHECK((ogc_in(0x3da, ogc) & 0xc0) == 0x80, "ENHANID must report the DEB's colour display");

    /* An identity LUT in the driver's own colour values. */
    for (int i = 0; i < 256; i++)
        lut[i] = i & 0x0f;
    load_lut(ogc, lut);
    CHECK(!memcmp(ogc->deb_lut, lut, 256), "the driver's load sequence must fill the LUT from 0");

    /* 16-colour 400-line, the mode table's 10110011b. */
    set_mode(ogc, 0xb3);

    /* Plane 1 alone is LUT value 2, which drives R (D1): IRGB red, not green. */
    deb_write(0xa8000, 0x80, ogc);
    draw(ogc, 0, 0);
    CHECK(irgb(0) == 4, "plane 1 drew %d, want 4 (red)", irgb(0));
    CHECK(irgb(1) == 0, "pixel 1 drew %d, want 0", irgb(1));

    /* The VDC's B bit is LUT address bit 3 = H. */
    draw(ogc, 0, 15);
    CHECK(irgb(1) == 8, "a VDC pixel drew %d, want 8 (H)", irgb(1));

    /* Raster line 5 is bank 1 of row 1, the mode 40h layout. */
    deb_write(0xa0000 + 0x2000 + 80, 0x80, ogc);
    draw(ogc, 5, 0);
    CHECK(irgb(0) == 1, "line 5 drew %d, want 1 (blue from plane 0)", irgb(0));

    /* LUT rows: BLINK1 BLINK2 RA0 PAT1 in the high nibble. */
    for (int i = 0; i < 256; i++)
        lut[i] = i >> 4;
    load_lut(ogc, lut);
    set_mode(ogc, 0xb3);
    ogc->cga.cgablink = 8; /* BLINK1 */
    draw(ogc, 1, 0);
    CHECK(irgb(0) == 0x0c, "BLINK1+RA0 drew %x, want c", irgb(0)); /* LUT value 1010b = H R */
    CHECK(irgb(1) == 0x0d, "BLINK1+RA0+PAT1 drew %x, want d", irgb(1));
    ogc->cga.cgablink = 0;

    /* Overlay: the VDC's H G R B are address bits 6..3. */
    set_mode(ogc, 0xa3);
    draw(ogc, 2, 2); /* VDC green = address bit 5 = row 2 = LUT value 2 = R */
    CHECK(irgb(1) == 4, "overlay VDC green drew %d, want 4", irgb(1));
    draw(ogc, 2, 4); /* VDC red = address bit 4 = row 1 = LUT value 1 = B */
    CHECK(irgb(1) == 1, "overlay VDC red drew %d, want 1", irgb(1));

    set_mode(ogc, 0xa3 | DEB_MODE_LITTLE);
    draw(ogc, 2, 4);
    CHECK(irgb(1) == 0, "the little LUT drew %d, want 0", irgb(1));

    set_mode(ogc, 0x33);
    draw(ogc, 0, 15);
    CHECK(irgb(1) == 0, "EVIDE clear drew %d, want black", irgb(1));

    /* 640x200 (mode 6) through ogc_poll itself: 16-colour 200-line, 10010011b. */
    for (int i = 0; i < 256; i++)
        lut[i] = i & 0x0f;
    load_lut(ogc, lut);
    memset(ogc->deb_vram, 0, 0x18000);
    ogc->cga.vram                 = (uint8_t *) calloc(1, 0x8000);
    ogc->cga.cgamode              = CGA_MODE_FLAG_GRAPHICS | CGA_MODE_FLAG_HIGHRES_GRAPHICS | CGA_MODE_FLAG_VIDEO_ENABLE;
    ogc->cga.cgacol               = 15;
    ogc->cga.crtc[CGA_CRTC_HDISP] = 40;
    ogc->cga.cgadispon            = 1;

    /* 200-line row 3 is CGA bank 1, row 1, on both boards; raster lines 6 and 7 show it. */
    ogc->cga.vram[0x2000 + 80] = 0x40;       /* VDC pixel 1 */
    deb_write(0xb0000 + 0x2000 + 80, 0x80, ogc); /* plane 2, pixel 0 */
    set_mode(ogc, 0);
    poll_line(ogc, 6);
    CHECK(pixels[0][8] == 0xff, "a transparent DEB must leave 640x200 to cga_poll()");
    set_mode(ogc, 0x93);
    poll_line(ogc, 6);
    CHECK(irgb(0) == 2, "640x200 plane 2 drew %d, want 2 (green)", irgb(0));
    CHECK(irgb(1) == 8, "640x200 VDC pixel drew %d, want 8 (H)", irgb(1));
    poll_line(ogc, 7);
    CHECK(irgb(0) == 2, "the paired line drew %d, want 2", irgb(0));

    /* PAGESEL: both boards read the second 16K. */
    ogc_out(0x3de, 0x08, ogc);
    deb_write(0xa0000 + 0x6000 + 80, 0x40, ogc); /* plane 0, pixel 1 */
    poll_line(ogc, 6);
    CHECK(irgb(0) == 0, "PAGESEL drew %d at pixel 0, want 0", irgb(0));
    CHECK(irgb(1) == 1, "PAGESEL drew %d at pixel 1, want 1 (blue)", irgb(1));

    if (failures)
        fprintf(stderr, "%d failure(s)\n", failures);
    else
        printf("DEB: all checks passed\n");
    return failures ? 1 : 0;
}
