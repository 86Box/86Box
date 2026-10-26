#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <gtest/gtest.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
extern "C" {
#include "../../src/video/vid_wd90c31.h"
}

static uint8_t (*read_byte)(uint16_t, void *);
static uint16_t (*read_word)(uint16_t, void *);
static void (*write_byte)(uint16_t, uint8_t, void *);
static void (*write_word)(uint16_t, uint16_t, void *);
static void *io_priv;
static int   irq_pending;
extern "C" {
monitor_t monitors[MONITORS_NUM];
int       monitor_index_global;
uint32_t *video_15to32;
uint32_t *video_16to32;
void
picint_common(uint16_t mask, int, int set, uint8_t *)
{
    if (set)
        irq_pending |= mask;
    else
        irq_pending &= ~mask;
}
void
io_sethandler(uint16_t base, int size,
              uint8_t (*inb)(uint16_t, void *), uint16_t (*inw)(uint16_t, void *), uint32_t (*)(uint16_t, void *),
              void (*outb)(uint16_t, uint8_t, void *), void (*outw)(uint16_t, uint16_t, void *), void (*)(uint16_t, uint32_t, void *), void *priv)
{
    EXPECT_EQ(base, 0x23c0);
    EXPECT_EQ(size, 8);
    read_byte  = inb;
    read_word  = inw;
    write_byte = outb;
    write_word = outw;
    io_priv    = priv;
}
void
io_removehandler(uint16_t, int,
                 uint8_t (*)(uint16_t, void *), uint16_t (*)(uint16_t, void *), uint32_t (*)(uint16_t, void *),
                 void (*)(uint16_t, uint8_t, void *), void (*)(uint16_t, uint16_t, void *), void (*)(uint16_t, uint32_t, void *), void *)
{
}
}

class Wd90c31 : public testing::Test {
protected:
    svga_t                     svga { };
    wd90c31_t                 *wd { };
    std::vector<uint8_t>       vram = std::vector<uint8_t>(1 << 20);
    std::array<uint8_t, 256>   dirty { };
    monitor_t                  monitor { };
    bitmap_t                   bitmap { };
    std::array<uint32_t, 2048> scanline { };

    void SetUp() override
    {
        irq_pending                  = 0;
        changeframecount             = 2;
        svga.vram                    = vram.data();
        svga.vram_mask               = vram.size() - 1;
        svga.changedvram             = dirty.data();
        svga.gdcreg[6]               = 1;
        svga.crtc[0x17]              = 0x43;
        svga.bpp                     = 8;
        svga.dac_mask                = 255;
        svga.hdisp                   = 640;
        svga.monitor                 = &monitor;
        monitor.mon_changeframecount = 2;
        monitor.target_buffer        = &bitmap;
        bitmap.line[0]               = scanline.data();
        for (unsigned i = 0; i < 256; i++)
            svga.pallook[i] = i * 0x010101;
        wd = wd90c31_init(&svga);
        out(0, 0x1001);
        reg(9, 0x300); // source copy
        reg(14, 0xff);
        reg(6, 4);
        reg(7, 1);
        reg(8, 16);
    }
    void     TearDown() override { wd90c31_close(wd); }
    void     out(unsigned port, uint16_t value) { write_word(0x23c0 + port, value, io_priv); }
    uint16_t in(unsigned port) { return read_word(0x23c0 + port, io_priv); }
    void     reg(unsigned index, uint16_t value) { out(2, (index << 12) | value); }
    uint16_t get(unsigned index, unsigned block = 1)
    {
        out(0, 0x1000 | (index << 8) | block);
        return in(2) & 0xfff;
    }
    void src(unsigned address)
    {
        reg(3, address >> 12);
        reg(2, address & 0xfff);
    }
    void dst(unsigned address)
    {
        reg(5, address >> 12);
        reg(4, address & 0xfff);
    }
    void start(uint16_t control = 0x100) { reg(0, control | 0x800); }
};

TEST_F(Wd90c31, IndexedAccessAndBytePairs)
{
    reg(10, 0x456); // reserved high bits must not stick
    reg(11, 0xab);
    out(0, 0x0a01);
    EXPECT_EQ(read_byte(0x23c2, io_priv), 0x56);
    EXPECT_EQ(read_byte(0x23c3, io_priv), 0xa0);
    EXPECT_EQ(in(2), 0xb0ab);
    EXPECT_EQ(in(0), 0x0c01);
    out(0, 0x1a01);
    EXPECT_EQ(in(2), 0xa056);
    EXPECT_EQ(in(2), 0xa056);
    write_byte(0x23c2, 0x33, io_priv);
    write_byte(0x23c3, 0xb0, io_priv);
    EXPECT_EQ(get(11), 0x33);
    out(0, 0x1003);
    EXPECT_EQ(in(0), 0x3003);
    EXPECT_EQ(in(2), 0);
    reg(10, 0xff); // invalid block must not change the BitBLT registers
    EXPECT_EQ(get(10), 0x56);
    out(0, 0x0f01);
    EXPECT_EQ(in(2), 0xf000);
    EXPECT_EQ(in(0), 1);
}

TEST_F(Wd90c31, PackedFillPitchAndDirtyPages)
{
    dst(0xffe);
    reg(6, 5);
    reg(7, 3);
    reg(8, 12);
    reg(10, 0x6a);
    start(0x108);
    EXPECT_EQ(get(0) & 0x800, 0);
    for (unsigned i = 0; i < vram.size(); i++) {
        bool inside = i >= 0xffe && i < 0xffe + 36 && (i - 0xffe) / 12 < 3 && (i - 0xffe) % 12 < 5;
        ASSERT_EQ(vram[i], inside ? 0x6a : 0) << i;
    }
    EXPECT_EQ(dirty[0], 2);
    EXPECT_EQ(dirty[1], 2);
}

TEST_F(Wd90c31, EveryRasterOperation)
{
    for (unsigned rop = 0; rop < 16; rop++) {
        for (unsigned s = 0; s < 2; s++) {
            for (unsigned d = 0; d < 2; d++) {
                out(0, 0x1001);
                vram[0] = d ? 0xff : 0;
                reg(6, 1);
                reg(9, rop << 8);
                reg(10, s ? 0xff : 0);
                start(0x108);
                EXPECT_EQ(vram[0], (rop & (8 >> (s * 2 + d))) ? 0xff : 0);
            }
        }
    }
}

TEST_F(Wd90c31, OverlappingReverseCopy)
{
    for (unsigned i = 0; i < 32; i++)
        vram[i] = i;
    src(15);
    dst(19);
    reg(6, 16);
    start(0x500);
    for (unsigned i = 0; i < 16; i++)
        EXPECT_EQ(vram[4 + i], i);
}

TEST_F(Wd90c31, LinearSourceAndDestinationUpdateQuickStart)
{
    for (unsigned i = 0; i < 8; i++)
        vram[64 + i] = i + 1;
    src(64);
    dst(128);
    reg(7, 2);
    reg(1, 0x40);
    start(0x140);
    EXPECT_EQ(get(4), 132);
    for (unsigned row = 0; row < 2; row++)
        for (unsigned x = 0; x < 4; x++)
            EXPECT_EQ(vram[128 + row * 16 + x], row * 4 + x + 1);
    reg(1, 0xc0);
    reg(0, 0x1c0);
    src(64); // source low starts the next command with automatic destination update
    EXPECT_EQ(get(4), 140);
    for (unsigned i = 0; i < 8; i++)
        EXPECT_EQ(vram[132 + i], i + 1);
}

TEST_F(Wd90c31, PlanarMaskAndUnalignedEdges)
{
    std::fill(vram.begin(), vram.begin() + 12, 0x55);
    dst(3);
    reg(6, 8);
    reg(14, 5); // planes 0 and 2 only
    reg(10, 0xf);
    start(8);
    for (unsigned plane = 0; plane < 4; plane++) {
        EXPECT_EQ(vram[plane], (plane & 1) ? 0x55 : 0x5f);
        EXPECT_EQ(vram[4 + plane], (plane & 1) ? 0x55 : 0xf5);
        EXPECT_EQ(vram[8 + plane], 0x55);
    }
}

TEST_F(Wd90c31, DiamondBiosTextScrollPreservesFonts)
{
    for (unsigned i = 0; i < 8000; i++)
        vram[i] = (i * 19 + i / 320) & 255;
    auto before = vram;
    // Exact register stream from the supplied Diamond BIOS at 568eh.
    for (uint16_t value : { 0x1000, 0x2280, 0x3000, 0x4000, 0x5000, 0x6280, 0x7018,
                            0x8280, 0x9300, 0xa0ff, 0xb0ff, 0xc0ff, 0xd0ff, 0xe003, 0x0800 })
        out(2, value);
    for (unsigned i = 0; i < 7680; i++)
        ASSERT_EQ(vram[i], before[i + ((i % 4 < 2) ? 320 : 0)]) << i;
}

TEST_F(Wd90c31, PatternWrapsAtEightPixelsAndRows)
{
    for (unsigned i = 0; i < 64; i++)
        vram[64 + i] = i;
    src(64 + 7 * 8 + 6);
    dst(256);
    reg(6, 11);
    reg(7, 10);
    reg(1, 0x10);
    start();
    for (unsigned y = 0; y < 10; y++)
        for (unsigned x = 0; x < 11; x++)
            EXPECT_EQ(vram[256 + y * 16 + x], ((y + 7) % 8) * 8 + (x + 6) % 8);
}

TEST_F(Wd90c31, DestinationTransparencyAndPlaneMask)
{
    vram[0] = 0xa1;
    vram[1] = 0xb2;
    vram[2] = 0xa3;
    vram[3] = 0xb4;
    reg(12, 0xa0);
    reg(13, 0x0f);
    reg(14, 0x0f);
    reg(10, 0x0e);
    reg(1, 1); // matching destination pixels remain unchanged
    start(0x108);
    EXPECT_EQ(vram[0], 0xa1);
    EXPECT_EQ(vram[1], 0xbe);
    EXPECT_EQ(vram[2], 0xa3);
    EXPECT_EQ(vram[3], 0xbe);
    reg(1, 5); // opposite polarity
    start(0x108);
    EXPECT_EQ(vram[0], 0xae);
    EXPECT_EQ(vram[2], 0xae);
}

TEST_F(Wd90c31, ColorComparatorExpansion)
{
    vram[64] = 1;
    vram[65] = 3;
    vram[66] = 0;
    vram[67] = 2;
    std::fill(vram.begin(), vram.begin() + 4, 0xcc);
    src(64);
    reg(12, 1);
    reg(13, 0xfe);
    reg(10, 0xaa);
    reg(11, 0xbb);
    reg(1, 8);
    start(0x104);
    EXPECT_EQ(vram[0], 0xaa);
    EXPECT_EQ(vram[1], 0xaa);
    EXPECT_EQ(vram[2], 0xcc);
    EXPECT_EQ(vram[3], 0xcc);
}

TEST_F(Wd90c31, HostPackedInputAlignmentAndRowPadding)
{
    src(1);
    reg(6, 5);
    reg(7, 2);
    start(0x102);
    EXPECT_NE(get(0) & 0x800, 0);
    out(4, 0x01ee);
    EXPECT_EQ(vram[0], 0);
    out(4, 0x0302);
    out(4, 0x0504);
    out(4, 0xeeee);
    out(4, 0x06ee);
    out(4, 0x0807);
    write_byte(0x23c4, 9, io_priv);
    write_byte(0x23c5, 10, io_priv);
    out(4, 0xeeee);
    EXPECT_EQ(get(0) & 0x800, 0);
    for (unsigned i = 0; i < 5; i++) {
        EXPECT_EQ(vram[i], i + 1);
        EXPECT_EQ(vram[16 + i], i + 6);
    }
    EXPECT_EQ(vram[5], 0);
}

TEST_F(Wd90c31, HostPlanarInputAndReadback)
{
    reg(6, 8);
    start(2);
    out(4, 0x55aa);
    out(4, 0x0ff0);
    EXPECT_EQ(vram[0], 0xaa);
    EXPECT_EQ(vram[1], 0x55);
    EXPECT_EQ(vram[2], 0xf0);
    EXPECT_EQ(vram[3], 0x0f);
    start(0x20);
    EXPECT_EQ(in(4), 0x55aa);
    EXPECT_NE(get(0) & 0x800, 0);
    EXPECT_EQ(in(4), 0x0ff0);
    EXPECT_EQ(get(0) & 0x800, 0);
}

TEST_F(Wd90c31, HostMonochromeExpansion)
{
    reg(10, 0xa5);
    reg(11, 0x5a);
    start(0x10e);
    out(4, 0xfffa); // only low four bits: 1010
    EXPECT_EQ(vram[0], 0xa5);
    EXPECT_EQ(vram[1], 0x5a);
    EXPECT_EQ(vram[2], 0xa5);
    EXPECT_EQ(vram[3], 0x5a);
    EXPECT_EQ(get(0) & 0x800, 0);
}

TEST_F(Wd90c31, HostReadbackPadding)
{
    vram[0]  = 1;
    vram[1]  = 2;
    vram[2]  = 3;
    vram[16] = 4;
    vram[17] = 5;
    vram[18] = 6;
    reg(6, 3);
    reg(7, 2);
    start(0x120);
    EXPECT_EQ(in(4), 0x0201);
    EXPECT_EQ(in(4), 0x0003);
    EXPECT_EQ(in(4), 0x0504);
    EXPECT_EQ(in(4), 0x0006);
}

TEST_F(Wd90c31, VramWrapAndInvalidDimensions)
{
    dst(0xffffe);
    reg(10, 0xff);
    start(0x108);
    EXPECT_EQ(vram[0xfffff], 0xff);
    EXPECT_EQ(vram[0], 0xff);
    EXPECT_EQ(vram[1], 0xff);
    reg(6, 0);
    reg(10, 0x77);
    start(0x108);
    EXPECT_EQ(get(0) & 0x800, 0);
    EXPECT_EQ(vram[0], 0xff);
}

TEST_F(Wd90c31, CompletionInterruptAndAcknowledge)
{
    reg(1, 0x400);
    start(0x108);
    EXPECT_EQ(irq_pending, 1 << 9);
    EXPECT_EQ(get(0, 0), 5);
    EXPECT_EQ(get(0, 0), 5); // reading does not clear status
    out(0, 0x1001);
    reg(1, 0);
    EXPECT_EQ(irq_pending, 0);
    EXPECT_EQ(get(0, 0), 0);
}

TEST_F(Wd90c31, CursorAddressOriginLatchAndClipping)
{
    out(0, 0x1002);
    reg(1, 0x100);
    reg(5, 3 | (4 << 6));
    reg(6, 1);
    reg(7, 2);
    EXPECT_EQ(svga.hwcursor.addr, 0);
    reg(0, 0x800);
    EXPECT_EQ(svga.hwcursor.addr, 0x400 + 2 * 8);
    EXPECT_EQ(svga.hwcursor.x, -2);
    EXPECT_EQ(svga.hwcursor.y, 0);
    EXPECT_EQ(svga.hwcursor.yoff, 2);
    reg(1, 0x200);
    EXPECT_EQ(svga.hwcursor.addr, 0x410);
    reg(0, 0xa00);
    EXPECT_EQ(svga.hwcursor.addr, 0x800 + 2 * 16);
    EXPECT_EQ(svga.hwcursor.cur_xsize, 64);
}

TEST_F(Wd90c31, CursorColorsTransparencyAndInversion)
{
    out(0, 0x1002);
    reg(1, 0x100);
    reg(3, 0x34);
    reg(4, 0x12);
    reg(0, 0x820);
    vram[0x400] = 0x3f;
    vram[0x401] = 0x5f; // 00 01 10 11, then transparent/invert
    scanline.fill(svga.pallook[0x56]);
    svga.hwcursor_latch = svga.hwcursor;
    wd90c31_hwcursor_draw(wd, 0);
    EXPECT_EQ(scanline[0], svga.pallook[0x12]);
    EXPECT_EQ(scanline[1], svga.pallook[0x34]);
    EXPECT_EQ(scanline[2], svga.pallook[0x56]);
    EXPECT_EQ(scanline[3], svga.pallook[0xa9]);
    EXPECT_EQ(svga.hwcursor_latch.addr, 0x408);
}

TEST_F(Wd90c31, IndexControlBytesTakeEffectIndependently)
{
    write_byte(0x23c0, 2, io_priv);
    reg(3, 0x5a);
    EXPECT_EQ(get(3, 2), 0x5a);
    write_byte(0x23c1, 0x14, io_priv);
    EXPECT_EQ(read_byte(0x23c1, io_priv), 0x14);
    EXPECT_EQ(in(2), 0x4000);
}

TEST_F(Wd90c31, TextWordAddressingMatchesCpuLayout)
{
    svga.crtc[0x17] = 0x23;
    svga.gdcreg[6]  = 0;
    for (unsigned column = 0; column < 80; column++) {
        vram[640 + column * 8] = column;
        vram[641 + column * 8] = 7;
        vram[642 + column * 8] = 0x5a; // font plane
    }
    src(640);
    dst(0);
    reg(6, 640);
    reg(14, 3);
    start(0);
    for (unsigned column = 0; column < 80; column++) {
        EXPECT_EQ(vram[column * 8], column);
        EXPECT_EQ(vram[1 + column * 8], 7);
        EXPECT_EQ(vram[2 + column * 8], 0);
        EXPECT_EQ(vram[642 + column * 8], 0x5a);
    }
    EXPECT_EQ(svga.fullchange, 2);
}

TEST_F(Wd90c31, CursorSpecialColorsAndPlaneProtection)
{
    out(0, 0x1002);
    reg(1, 0x100);
    reg(8, 0xc0);
    reg(0, 0x840);
    vram[0x400] = vram[0x401] = 0xff;
    scanline.fill(svga.pallook[0x56]);
    svga.hwcursor_latch = svga.hwcursor;
    wd90c31_hwcursor_draw(wd, 0);
    EXPECT_EQ(scanline[0], svga.pallook[(~(0x56 ^ 0xc0)) & 255]);
    reg(0, 0x860);
    svga.hwcursor_latch = svga.hwcursor;
    wd90c31_hwcursor_draw(wd, 0);
    EXPECT_EQ(scanline[0], svga.pallook[0xc0]);
    reg(0, 0x900);
    svga.attrregs[0x10] = 0x80;
    vram[0x400] = vram[0x401] = 0;
    svga.hwcursor_latch       = svga.hwcursor;
    wd90c31_hwcursor_draw(wd, 0);
    EXPECT_EQ(scanline[0], svga.pallook[0xc0]);
}

TEST_F(Wd90c31, CursorCombinesByteColorsInTrueColorMode)
{
    svga.bpp = 24;
    out(0, 0x1002);
    reg(1, 0x100);
    reg(3, 0x56);
    reg(4, 0x34);
    reg(8, 0x12);
    reg(6, 6); // positions are byte pixels: displayed x = 2
    reg(0, 0xa60);
    for (unsigned i = 0; i < 16; i += 2)
        vram[0x400 + i] = 0xff; // transparent
    vram[0x400] = 0x3f;
    vram[0x401] = 0xa0; // primary, secondary, auxiliary, then transparent
    scanline.fill(0xabcdef);
    svga.hwcursor_latch = svga.hwcursor;
    wd90c31_hwcursor_draw(wd, 0);
    EXPECT_EQ(scanline[1], 0xabcdef);
    EXPECT_EQ(scanline[2], 0x123456);
    EXPECT_EQ(scanline[3], 0xabcdef);
    EXPECT_EQ(svga.hwcursor_latch.addr, 0x410);
}

TEST_F(Wd90c31, CursorCombinesByteColorsInHighColorMode)
{
    std::vector<uint32_t> colors(65536);
    colors[0x7c1f] = 0xff00ff;
    video_15to32   = colors.data();
    svga.bpp       = 15;
    out(0, 0x1002);
    reg(1, 0x100);
    reg(3, 0x1f);
    reg(4, 0x7c);
    reg(0, 0x820);
    for (unsigned i = 0; i < 8; i += 2)
        vram[0x400 + i] = 0xff;
    vram[0x400] = 0x3f;
    vram[0x401] = 0x80; // primary low byte, secondary high byte
    scanline.fill(0xabcdef);
    svga.hwcursor_latch = svga.hwcursor;
    wd90c31_hwcursor_draw(wd, 0);
    EXPECT_EQ(scanline[0], 0xff00ff);
    EXPECT_EQ(scanline[1], 0xabcdef);
    video_15to32 = nullptr;
}

TEST_F(Wd90c31, CursorInterlacedFieldsAndRightClipping)
{
    svga.interlace        = 1;
    svga.hwcursor_oddeven = 1;
    out(0, 0x1002);
    reg(1, 0x100);
    reg(6, 639);
    reg(0, 0x800);
    vram[0x409] = 0x80; // first pixel of the odd field is white
    scanline.fill(0x123456);
    svga.hwcursor_latch = svga.hwcursor;
    wd90c31_hwcursor_draw(wd, 0);
    EXPECT_EQ(scanline[638], 0x123456);
    EXPECT_EQ(scanline[639], svga.pallook[255]);
    EXPECT_EQ(scanline[640], 0x123456);
    EXPECT_EQ(svga.hwcursor_latch.addr, 0x410);
}
