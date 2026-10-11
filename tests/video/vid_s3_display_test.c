/* S3 911/924 planar BIOS modes and accelerated Windows 3.x 16-color modes. */
#undef NDEBUG
#include <assert.h>

#include "../../src/video/vid_s3.c"

double cpuclock = 50000000.0;
monitor_t monitors[MONITORS_NUM];
int monitor_index_global;
uint32_t *video_15to32, *video_16to32;

/* These RAMDACs are not used by the tested cards. */
void bt48x_recalctimings(void *priv, svga_t *svga) { abort(); }
void ibm_rgb528_recalctimings(void *priv, svga_t *svga) { abort(); }
void tvp3026_recalctimings(void *priv, svga_t *svga) { abort(); }

/* Display tests do not access the CPU's video-memory aperture. */
uint8_t svga_read_linear(uint32_t addr, void *priv) { abort(); }
uint16_t svga_readw_linear(uint32_t addr, void *priv) { abort(); }
uint32_t svga_readl_linear(uint32_t addr, void *priv) { abort(); }
void svga_write_linear(uint32_t addr, uint8_t val, void *priv) { abort(); }
void svga_writew_linear(uint32_t addr, uint16_t val, void *priv) { abort(); }
void svga_writel_linear(uint32_t addr, uint32_t val, void *priv) { abort(); }
uint8_t xga_read_test(uint32_t addr, void *priv) { abort(); }
void xga_write_test(uint32_t addr, uint8_t val, void *priv) { abort(); }

static float
test_clock(int clock, void *priv)
{
    return 40000000.0f;
}

static void
test_16color(int chip, int width, int accelerated)
{
    static s3_t      s3;
    static uint8_t   vram[1 << 20];
    static uint8_t   changed[257];
    static uint32_t  pixels[1040];
    static bitmap_t bitmap;
    monitor_t       monitor = { 0 };
    svga_t         *svga = &s3.svga;
    int             height = (width == 800) ? 600 : 768;
    int             stride = accelerated ? 1024 : width / 2;

    memset(&s3, 0, sizeof(s3));
    memset(vram, 0, sizeof(vram));
    bitmap.line[0] = pixels;
    monitor.target_buffer = &bitmap;
    svga->priv = &s3;
    svga->monitor = &monitor;
    svga->getclock = test_clock;
    svga->vram = vram;
    svga->changedvram = changed;
    svga->map8 = svga->pallook;
    svga->pallook[1] = 0xff0000;
    svga->egapal[1] = 1;
    svga->dac_mask = 0xff;
    svga->plane_mask = 0x0f;
    svga->force_old_addr = 1;
    svga->fullchange = 1;
    svga->bpp = 8;
    svga->hdisp_old = width;
    svga->dispend = height;
    svga->seqregs[1] = 1;
    svga->crtc[0x17] = 0xe3;
    svga->crtc[0x14] = (width == 1024) ? 0x60 : 0;
    /* HP C.02.02 VBE 102h/104h versus the S3 Windows driver's register values. */
    svga->crtc[0x31] = accelerated ? 0x8b : 0x89;
    svga->crtc[0x13] = stride / 8;
    svga->rowoffset = svga->crtc[0x13];
    svga->render = svga_render_4bpp_highres;
    s3.chip = chip;
    s3.vram_mask = sizeof(vram) - 1;
    s3.accel.advfunc_cntl = accelerated ? 7 : 0;

    s3_recalctimings(svga);
    assert(svga->rowoffset * 8 == stride);
    assert(svga->render == (accelerated ? svga_render_8bpp_highres : s3_render_4bpp_highres));
    assert(svga->hdisp == width);
    assert(svga->dispend == height);

    /* Check separate rows, including the bottom of the screen, through the real renderer. */
    for (int y = 1; y < height; y += height - 2) {
        if (accelerated) {
            s3.accel.cmd = 0x40b0; /* One-pixel rectangle, foreground copy. */
            s3.accel.cur_x = 0;
            s3.accel.cur_y = y;
            s3.accel.frgd_color = 1;
            s3.accel.frgd_mix = 0x27;
            s3.accel.wrt_mask = 0x0f;
            s3.accel.multifunc[3] = 1023;
            s3.accel.multifunc[4] = 2047;
            s3_accel_start(1, 0, ~0u, 0, &s3);
            assert(vram[y * stride] == 0x10);
        } else
            vram[y * stride] = 0x80;
        svga->memaddr = y * (svga->rowoffset << 3);
        memset(pixels, 0xff, sizeof(pixels));
        svga->render(svga);
        assert(pixels[0] == 0xff0000);
        assert(pixels[1] == 0);
        assert(pixels[width - 1] == 0);
        assert(svga->crtc[0x14] == ((width == 1024) ? 0x60 : 0));
        assert(svga->crtc[0x17] == 0xe3);
    }

    if (accelerated) {
        assert(s3.width == 2048);
        assert(svga->packed_4bpp);
        /* CPU readback and uploads must agree on two pixels per byte. */
        s3.accel_start = s3_accel_start;
        for (int mix_select = 0; mix_select <= 0x80; mix_select += 0x80) {
            s3.accel.cmd = 0x53b1;
            s3.accel.cur_x = 1;
            vram[10 * stride] = 0xab;
            vram[10 * stride + 2] = 0xef;
            s3.accel.cur_y = 10;
            s3.accel.maj_axis_pcnt = 3;
            s3.accel.frgd_mix = 0x47;
            s3.accel.bkgd_mix = mix_select ? 0x47 : 0x07;
            s3.accel.multifunc[0xa] = mix_select;
            s3_accel_start(0, 0, ~0u, 0, &s3);
            s3_accel_out_pixtrans_w(&s3, 0x1234);
            assert(vram[10 * stride] == 0xa3);
            assert(vram[10 * stride + 1] == 0x41);
            assert(vram[10 * stride + 2] == 0x2f);
            assert(s3_accel_read_transfer(&s3, 10 * s3.width + 1, 0) == 0x34);
            assert(s3_accel_read_transfer(&s3, 10 * s3.width + 1, 1) == 0x12);
            assert(s3.accel.sy == -1);
        }
        /* Monochrome transfers still consume one bit per pixel. */
        s3.accel.cmd = 0x43b3;
        s3.accel.cur_x = 0;
        s3.accel.cur_y = 12;
        s3.accel.maj_axis_pcnt = 15;
        s3.accel.frgd_mix = 0x27;
        s3.accel.bkgd_mix = 0x07;
        s3_accel_start(0, 0, ~0u, 0, &s3);
        s3_accel_out_pixtrans_w(&s3, 0x8000);
        assert(vram[12 * stride] == 0x10);
        for (int i = 1; i < 8; i++)
            assert(vram[12 * stride + i] == 0);
        assert(s3.accel.sy == -1);

        /* Cursor masks must remain raw VRAM bytes in the packed display mode. */
        monitors[0].target_buffer = &bitmap;
        svga->hwcursor_latch.addr = 0x80000;
        svga->crtc[0x0e] = 0xf1;
        for (int i = 0; i < 16; i += 4) {
            vram[0x80000 + i] = 0xff;
            vram[0x80001 + i] = 0xff;
            vram[0x80002 + i] = vram[0x80003 + i] = 0;
        }
        vram[0x80000] = 0x3f;
        vram[0x80002] = 0x40;
        for (int i = 0; i < 1040; i++)
            pixels[i] = 0x777777;
        s3_hwcursor_draw(svga, 0);
        assert(pixels[0] == 0);
        assert(pixels[1] == 0xff0000);
        for (int i = 2; i < 1040; i++)
            assert(pixels[i] == 0x777777);
        assert(svga->hwcursor_latch.addr == 0x80010);
    }

    /* The ordinary 256-color path must retain the same pitch. */
    svga->crtc[0x3a] = 0x10;
    svga->rowoffset = 128;
    s3_recalctimings(svga);
    assert(svga->rowoffset == 128);
    assert(svga->render == svga_render_8bpp_highres);

    assert(!svga->packed_4bpp);
    s3_accel_write_pixel(&s3, 0x123, 0xa5);
    assert(vram[0x123] == 0xa5);
    assert(s3_accel_read_transfer(&s3, 0x123, 0) == 0xa5);

    /* High-color byte lanes must not be mistaken for packed 16-color pixels. */
    svga->bpp = 15;
    svga->crtc[0x43] = 8;
    s3_recalctimings(svga);
    assert(!svga->packed_4bpp);
    s3_accel_write_pixel(&s3, 0x401, 0x5a);
    assert(vram[3] == 0x5a);
    assert(s3_accel_read_pixel(&s3, 0x401) == 0x5a);
    svga->bpp = 8;
    svga->crtc[0x43] = 0;

    /* Returning to ordinary VGA must leave its renderer and offset alone. */
    svga->crtc[0x31] = 0;
    svga->crtc[0x3a] = 0;
    svga->rowoffset = 40;
    svga->render = svga_render_4bpp_highres;
    s3_recalctimings(svga);
    assert(svga->rowoffset == 40);
    assert(svga->render == svga_render_4bpp_highres);

    /* Later S3 chips retain their existing enhanced 4bpp stride handling. */
    s3.chip = S3_86C928;
    svga->crtc[0x31] = 0x08;
    svga->crtc[0x3a] = 0;
    svga->rowoffset = 64;
    s3_recalctimings(svga);
    assert(svga->rowoffset == 128);
    assert(svga->render == svga_render_8bpp_highres);
}

int
main(void)
{
    for (int chip = S3_86C911; chip <= S3_86C924; chip += 2) {
        for (int accelerated = 0; accelerated <= 1; accelerated++) {
            test_16color(chip, 800, accelerated);
            test_16color(chip, 1024, accelerated);
        }
    }
    return 0;
}
