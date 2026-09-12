#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <vector>

// Like the existing CD-ROM tests, exercise private device callbacks directly.
// Only host services and unrelated peripheral engines are substituted.
extern "C" {
#define fdc_remove pcjx_test_fdc_remove
#define fdc_set_base pcjx_test_fdc_set_base
#define fdc_pcjx_device pcjx_test_fdc_device
#include "../../src/machine/m_pcjx.c"
#include "../../src/video/vid_pcjx.c"
static int pcjx_test_cart_identity(int) { return 1; }
#define machine_is_pcjx pcjx_test_cart_identity
#define malloc(size) ((uint8_t *) malloc(size))
#include "../../src/device/cartridge.c"
#undef malloc
#undef machine_is_pcjx
}

namespace {
using Read = uint8_t (*)(uint16_t, void *);
using Write = void (*)(uint16_t, uint8_t, void *);
struct Port { Read read; Write write; void *priv; };
std::array<std::vector<Port>, 65536> ports;
std::array<uint8_t, 0x80000> memory;
void (*input_handler)(uint16_t, int, void *);
void *input_priv;

uint8_t input(uint16_t port)
{
    uint8_t value = 0xff;
    for (const auto &entry : ports[port])
        if (entry.read) value &= entry.read(port, entry.priv);
    return value;
}
void output(uint16_t port, uint8_t value)
{
    auto entries = ports[port];
    for (const auto &entry : entries)
        if (entry.write) entry.write(port, value, entry.priv);
}
void fire(pc_timer_t &timer)
{
    tsc = timer.ts_integer;
    timer_disable(&timer);
    timer.callback(timer.priv);
}
void gate(pcjx_video_t &video, uint8_t banks, uint8_t index, uint8_t value)
{
    pcjx_vid_in(0x3da, banks, &video);
    pcjx_vid_out(0x3da, index, banks, &video);
    pcjx_vid_out(0x3da, value, banks, &video);
}

class PcjxBoard : public ::testing::Test {
protected:
    pcjx_t board{};
    void SetUp() override
    {
        for (auto &port : ports) port.clear();
        memory.fill(0);
        ram = memory.data();
        mem_size = 512;
        tsc = 0;
        board.general_size = memory.size();
        board.video.shared_ram = ram;
        board.video.shared_size = 0x20000;
        board.video.dedicated_vram = board.dedicated_vram;
        board.video.jx_array[1] = 0x10;
        board.video.jx_array[3] = 0x10;
        board.video.pb = 4;
        std::memset(board.rom_data, 0xff, sizeof(board.rom_data));
        std::memset(board.rom_present, 1, sizeof(board.rom_present));
        pit_devs[0].set_gate = [](void *, int, int) {};
        pit_devs[0].set_using_timer = [](void *, int, int) {};
        decode(0, 0xbc, 0x23);
    }
    void TearDown() override
    {
        cart_image_close(0);
        cart_image_close(1);
        pcjx_keyboard_close(&board.keyboard);
    }
    void decode(uint8_t selector, uint8_t first, uint8_t second)
    {
        pcjx_decoder_write(0x1ff, selector, &board);
        pcjx_decoder_write(0x1ff, first, &board);
        pcjx_decoder_write(0x1ff, second, &board);
    }
    uint8_t read(uint32_t address) { return pcjx_readb(address, &board); }
    void write(uint32_t address, uint8_t value) { pcjx_writeb(address, value, &board); }
};

TEST_F(PcjxBoard, DecoderWritesTakeEffectIndividuallyAndReadRestartsSequence)
{
    board.rom_data[0x10000] = 0x5a;
    EXPECT_EQ(read(0xf0000), 0x5a);
    pcjx_decoder_write(0x1ff, 0, &board);
    pcjx_decoder_write(0x1ff, 0, &board);
    EXPECT_EQ(read(0xf0000), 0xff);
    EXPECT_EQ(pcjx_decoder_read(0x1ff, &board), 0xff);
    EXPECT_EQ(read(0xf0000), 0xff);
    decode(0x7f, 0xff, 0xff);
    EXPECT_EQ(read(0xf0000), 0xff);
    pcjx_decoder_write(0x1ff, 0, &board);
    pcjx_decoder_write(0x1ff, 0xbe, &board);
    EXPECT_EQ(read(0xf0000), 0x5a); // REG2 retained without a third write.
    pcjx_decoder_read(0x1ff, &board);
    decode(0, 0, 0);
    EXPECT_EQ(read(0xf0000), 0xff);
    decode(0, 0xbe, 0x23);
    EXPECT_EQ(read(0xf0000), 0x5a);
    write(0xf0000, 0);
    EXPECT_EQ(read(0xf0000), 0x5a);
}

TEST_F(PcjxBoard, ExpansionProbeDoesNotAliasDisabledSharedPair)
{
    write(0, 0x11);
    write(0x20000, 0x22);
    write(0x40000, 0x44);
    write(0x60000, 0x66);
    EXPECT_EQ(read(0), 0xff);
    EXPECT_EQ(read(0x20000), 0x22);
    EXPECT_EQ(read(0x40000), 0x44);
    EXPECT_EQ(read(0x60000), 0x66);
    decode(8, 0xa0, 0x63);
    write(0, 0x11);
    EXPECT_EQ(read(0), 0x11);
    board.video.jx_array[3] &= ~0x10;
    pcjx_rebuild_memory(&board);
    EXPECT_EQ(read(0), 0x22);
    EXPECT_EQ(read(0x20000), 0x44);
    EXPECT_EQ(read(0x40000), 0x66);
    EXPECT_EQ(read(0x60000), 0x11);
}

TEST_F(PcjxBoard, DisplayResourceMaskDoesNotDisconnectCpuMemory)
{
    board.video.jx_array[1] = 0;
    decode(8, 0xa0, 0x63);
    write(0x1234, 0x5a);
    EXPECT_EQ(read(0x1234), 0x5a);
    decode(9, 0xb7, 0x60);
    write(0xb9234, 0xa5);
    EXPECT_EQ(read(0x1234), 0xa5);
    decode(9, 0, 0x60);
    decode(10, 0xb7, 0x60);
    write(0xb9234, 0x96);
    EXPECT_EQ(read(0xb9234), 0x96);
    EXPECT_EQ(read(0x1234), 0xa5);
}

TEST_F(PcjxBoard, WordCrossingRoutesResolvesEachByteAndProtectsRom)
{
    decode(8, 0xa0, 0x63);
    pcjx_writew(0x1ffff, 0x3412, &board);
    EXPECT_EQ(pcjx_readw(0x1ffff, &board), 0x3412);
    board.rom_data[0] = 0x7e;
    pcjx_writew(0xdffff, 0x1234, &board);
    EXPECT_EQ(pcjx_readw(0xdffff, &board), 0x7eff);
}

TEST_F(PcjxBoard, CartridgeTemporaryFinalAndEjectUseLiveResources)
{
    for (unsigned bay = 0; bay < 2; bay++) {
        carts[bay].base = 0xd0000 + bay * 0x10000;
        carts[bay].size = 0x8000;
        carts[bay].buf = (uint8_t *) std::malloc(0x8000);
        std::memset(carts[bay].buf, bay ? 0xff : 0x96, 0x8000);
    }
    board.rom_data[0] = 0x12;
    decode(1, 0xb2, 0x20);
    decode(3, 0xb3, 0x20);
    EXPECT_EQ(read(0x90000), 0x96);
    EXPECT_EQ(read(0x98000), 0xff);
    decode(1, 0xba, 0x20);
    decode(3, 0xbc, 0x20);
    EXPECT_EQ(read(0xd0000), 0x96);
    EXPECT_EQ(read(0xe0000), 0xff); // Populated FF replaces, not ANDs, base ROM.
    cart_close(1);
    EXPECT_EQ(read(0xe0000), 0x12);
    EXPECT_EQ(read(0xd0000), 0x96);
    board.rom_present[0] = 0;
    pcjx_rebuild_memory(&board);
    EXPECT_EQ(read(0xe0000), 0xff);
    decode(1, 0, 0x20);
    EXPECT_EQ(read(0xd0000), 0xff);
}

TEST_F(PcjxBoard, DisabledPpiHasNeitherReadNorWriteEffects)
{
    output(0x60, 0x12);
    EXPECT_EQ(input(0x60), 0xff);
    decode(0x82, 0x80, 0);
    output(0x64, 0x34);
    EXPECT_EQ(input(0x60), 0x34);
    decode(0x82, 0, 0);
    output(0x60, 0x56);
    EXPECT_EQ(input(0x60), 0xff);
    decode(0x82, 0x80, 0);
    EXPECT_EQ(input(0x64), 0x34);
}

TEST_F(PcjxBoard, VideoCpuDisplayPagesAndRetainedVp2ControlsStaySeparate)
{
    gate(board.video, 3, 1, 0x1f);
    gate(board.video, 1, 1, 0);
    decode(9, 0xb7, 0x60);
    pcjx_vid_out(0x3df, 0x3f, 0, &board.video);
    pcjx_rebuild_memory(&board);
    write(0xb8000, 0x77);
    EXPECT_EQ(memory[0x1c000], 0x77);
    pcjx_vid_out(0x3df, 0xf6, 0, &board.video);
    pcjx_rebuild_memory(&board);
    write(0xb8000, 0x66);
    write(0xbc000, 0x55);
    EXPECT_EQ(memory[0x18000], 0x66);
    EXPECT_EQ(memory[0x1c000], 0x55);
    pcjx_vid_out(0x3df, 0x08, 0, &board.video);
    pcjx_rebuild_memory(&board);
    write(0xb8000, 0x22);
    EXPECT_EQ(board.video.vram[0], 0);
    pcjx_vid_out(0x3df, 0x09, 0, &board.video);
    EXPECT_EQ(board.video.vram[0], 0x22);
    gate(board.video, 1, 4, 2);
    gate(board.video, 1, 4, 0);
    EXPECT_EQ(board.video.vram[0], 0x22);
    decode(9, 0, 0x60);
    gate(board.video, 2, 1, 0x30);
    decode(10, 0xb7, 0x60);
    pcjx_rebuild_memory(&board);
    write(0xb8000, 0x3c);
    EXPECT_EQ(board.dedicated_vram[0], 0x3c);
    pcjx_vid_out(0x3d9, 0x10, 0, &board.video);
    pcjx_rebuild_memory(&board);
    EXPECT_EQ(read(0xb8000), 0xff);
    EXPECT_EQ(memory[0x4000], 0x22);
}

TEST_F(PcjxBoard, RasterFeedbackDependsOnEmulatedBeamNotReads)
{
    auto &v = board.video;
    v.crtc[1] = 1;
    v.array[0] = 9;
    v.array[1] = 15;
    v.array[16] = 0;
    v.array[31] = 15;
    v.cg1[0] = 0x80;
    memory[1] = 15;
    v.vram = memory.data();
    v.status = 1;
    v.dispontime = 800 * TIMER_USEC;
    timer_add(&v.timer, nullptr, nullptr, 0);
    timer_set_delay_u64(&v.timer, v.dispontime);
    EXPECT_EQ(pcjx_vid_in(0x3da, 1, &v) & 0x10, 0x10);
    EXPECT_EQ(pcjx_vid_in(0x3da, 1, &v) & 0x10, 0x10);
    tsc = 200;
    EXPECT_EQ(pcjx_vid_in(0x3da, 1, &v) & 0x10, 0);
}

TEST_F(PcjxBoard, HorizontalPositionPreservesSyncEndAndManualAdjustment)
{
    auto &v = board.video;
    v.array[0] = 0x0a;
    v.crtc[0] = 0x38;
    v.crtc[2] = 0x2f;
    v.crtc[3] = 0x06;
    EXPECT_EQ(vid_get_h_overscan_delta(&v), 0);

    // Flight Simulator moves sync start earlier and widens the pulse equally.
    v.crtc[2] = 0x2b;
    v.crtc[3] = 0x0a;
    EXPECT_EQ(vid_get_h_overscan_delta(&v), 0);

    // A position-only change must still move the image by one character.
    --v.crtc[2];
    EXPECT_EQ(vid_get_h_overscan_delta(&v), 16);
}

TEST_F(PcjxBoard, HighBandwidthPositionUsesHorizontalWidthAndLineTotal)
{
    auto &v = board.video;
    v.array[0] = 0x1b;
    v.crtc[0] = 0x71;
    v.crtc[2] = 0x5c;
    v.crtc[3] = 0x0c;
    EXPECT_EQ(vid_get_h_overscan_delta(&v), 0);

    v.crtc[2] = 0x5a;
    v.crtc[3] = 0xae; // Same horizontal sync end; vertical width is independent.
    EXPECT_EQ(vid_get_h_overscan_delta(&v), 0);

    ++v.crtc[0];
    EXPECT_EQ(vid_get_h_overscan_delta(&v), 8);
}

class PcjxNativeVideo : public PcjxBoard {
protected:
    std::vector<uint8_t> fonts = std::vector<uint8_t>(PCJX_CG2_IMAGE_SIZE);
    std::array<uint8_t, PCJX_GAIJI_SIZE> gaiji{};

    void SetUp() override
    {
        PcjxBoard::SetUp();
        auto &v = board.video;
        v.cg2 = fonts.data();
        v.gaiji = gaiji.data();
        v.vram = memory.data();
        v.jx_array[0] = 0x49;
        v.jx_array[1] = 0x3f;
        v.array[1] = 15;
        v.array[6] = 1;
        v.crtc[1] = 80;
        v.crtc[9] = 17;
        for (int i = 0; i < 16; ++i)
            v.array[16 + i] = i;
    }

    std::array<uint8_t, 8> cell(uint16_t address, int row)
    {
        uint8_t pixels[16]{};
        vid_cell(&board.video, address, row, pixels);
        std::array<uint8_t, 8> visible;
        std::copy_n(pixels, visible.size(), visible.begin());
        return visible;
    }
};

TEST_F(PcjxNativeVideo, FontDecodeProtectsRomAndAliasesOnlyGaiji)
{
    fonts[0x820] = 0x5a;
    decode(7, 0xb0, 0x67);
    EXPECT_EQ(read(0x80820), 0x5a);
    write(0x80820, 0xa5);
    EXPECT_EQ(read(0x80820), 0x5a);

    write(0x88820, 0x69);
    EXPECT_EQ(read(0x88020), 0x69);
    EXPECT_EQ(read(0x8f820), 0x69);
    EXPECT_EQ(read(0x80820), 0x5a);
    decode(7, 0xb0, 0x27);
    write(0x88820, 0xff);
    EXPECT_EQ(read(0x88820), 0x69);
    decode(7, 0, 0x67);
    EXPECT_EQ(read(0x88820), 0xff);

    board.video.cg2 = nullptr;
    decode(7, 0xb0, 0x67);
    EXPECT_EQ(read(0x80820), 0xff);
    write(0x88820, 0x12);
    board.video.cg2 = fonts.data();
    pcjx_rebuild_memory(&board);
    EXPECT_EQ(read(0x88820), 0x69);
}

TEST_F(PcjxNativeVideo, NativeCellsFetchPairedCodesAndDistinctFontLanes)
{
    auto &v = board.video;
    board.dedicated_vram[0] = 0x82;
    board.dedicated_vram[1] = 0xa3;
    board.dedicated_vram[2] = 0xa0;
    board.dedicated_vram[3] = 0xab;
    fonts[0x5400] = 0xc0;
    fonts[0x5401] = 1;
    EXPECT_EQ(cell(0, 0), (std::array<uint8_t, 8>{ 2, 3, 2, 2, 2, 2, 2, 2 }));
    EXPECT_EQ(cell(1, 0), (std::array<uint8_t, 8>{ 2, 2, 2, 2, 2, 2, 2, 3 }));
    EXPECT_EQ(cell(0, 16), (std::array<uint8_t, 8>{ 2, 2, 2, 2, 2, 2, 2, 2 }));
    EXPECT_EQ(cell(1, 17), (std::array<uint8_t, 8>{ 2, 2, 2, 2, 2, 2, 2, 2 }));

    board.dedicated_vram[4] = 0x41;
    board.dedicated_vram[5] = 0x54;
    fonts[0x820] = 0x80;
    fonts[0x821] = 1;
    EXPECT_EQ(cell(2, 0), (std::array<uint8_t, 8>{ 4, 5, 5, 5, 5, 5, 5, 5 }));
    v.jx_array[0] = 9;
    board.dedicated_vram[5] = 0x1e;
    EXPECT_EQ(cell(2, 0), (std::array<uint8_t, 8>{ 1, 1, 1, 1, 1, 1, 1, 14 }));
}

TEST_F(PcjxNativeVideo, GaijiWritesReachDisplayAfterCpuFontWindowCloses)
{
    // The BIOS converts external F041 to the hardware's 8441 code pair.
    board.dedicated_vram[0] = 0x84;
    board.dedicated_vram[1] = 0x87;
    board.dedicated_vram[2] = 0x41;
    board.dedicated_vram[3] = 0x8f;
    decode(7, 0xb0, 0x67);
    write(0x88820, 0x40);
    write(0x88821, 1);
    decode(7, 0, 0x67);
    EXPECT_EQ(cell(0, 0), (std::array<uint8_t, 8>{ 0, 7, 0, 0, 0, 0, 0, 0 }));
    EXPECT_EQ(cell(1, 0), (std::array<uint8_t, 8>{ 0, 0, 0, 0, 0, 0, 0, 7 }));

    decode(7, 0xb0, 0x67);
    write(0x88020, 0x20);
    write(0x88021, 0x80);
    decode(7, 0, 0x67);
    EXPECT_EQ(cell(0, 0), (std::array<uint8_t, 8>{ 0, 0, 7, 0, 0, 0, 0, 0 }));
    EXPECT_EQ(cell(1, 0), (std::array<uint8_t, 8>{ 7, 0, 0, 0, 0, 0, 0, 0 }));
}

TEST_F(PcjxNativeVideo, NativeDisplayPageDoesNotFollowCpuPage)
{
    auto &v = board.video;
    fonts[0x820] = 0x80;
    decode(10, 0xb7, 0x60);
    pcjx_vid_out(0x3d9, 8, 0, &v);
    pcjx_rebuild_memory(&board);
    write(0xb8000, 0x41);
    write(0xb8001, 7);
    EXPECT_EQ(cell(0, 0), (std::array<uint8_t, 8>{}));
    pcjx_vid_out(0x3d9, 9, 0, &v);
    EXPECT_EQ(cell(0, 0), (std::array<uint8_t, 8>{ 7, 0, 0, 0, 0, 0, 0, 0 }));
    decode(0x8d, 0, 0);
    EXPECT_EQ(cell(0, 0), (std::array<uint8_t, 8>{ 7, 0, 0, 0, 0, 0, 0, 0 }));
}

TEST_F(PcjxNativeVideo, MixerUsesVp1TransparencyAndComposesBeforePalette)
{
    auto &v = board.video;
    v.array[0] = 0x09;
    v.jx_array[0] = 0x09;
    v.cg1[0] = 0xff;
    fonts[1] = 0xff;
    memory[1] = 2;
    board.dedicated_vram[1] = 5;
    v.array[18] = 9;
    v.array[21] = 12;
    v.array[23] = 6;
    v.array[16] = 3;
    for (const auto &[mode, color] : std::array<std::array<uint8_t, 2>, 5>{
             { { 0, 9 }, { 1, 12 }, { 4, 6 }, { 8, 3 }, { 12, 6 } } }) {
        SCOPED_TRACE(mode);
        v.array[6] = mode;
        std::array<uint8_t, 8> expected;
        expected.fill(color);
        EXPECT_EQ(cell(0, 0), expected);
    }
    v.array[5] = 2;
    v.array[6] = 2;
    EXPECT_EQ(cell(0, 0).front(), 12);
    v.array[6] = 3;
    EXPECT_EQ(cell(0, 0).front(), 9);
    v.array[5] = 5;
    v.array[6] = 3;
    EXPECT_EQ(cell(0, 0).front(), 12);
}

TEST_F(PcjxNativeVideo, CombinedGraphicsUsesBothBitplanesBeforePalette)
{
    auto &v = board.video;
    v.pb = 0;
    v.array[0] = 0x0b;
    v.jx_array[0] = 0x8b;
    v.array[1] = 3;
    v.jx_array[1] = 0x33;
    v.array[6] = 6;
    v.memctrl = 0xc0;
    v.addr_mode = 3;
    v.crtc[1] = 80;
    v.crtc[9] = 3;
    memory[0] = 0x88;
    memory[1] = 0x44;
    board.dedicated_vram[0] = 0x22;
    board.dedicated_vram[1] = 0x11;
    const std::array<uint8_t, 8> expected{ 11, 8, 14, 2, 11, 8, 14, 2 };
    EXPECT_EQ(cell(0, 0), expected);
    v.array[6] = 4;
    EXPECT_EQ(cell(0, 0), expected);
    memory[0x6000] = 0x80;
    board.dedicated_vram[0x6001] = 0x80;
    EXPECT_EQ(cell(0, 3).front(), 3);
}

TEST_F(PcjxBoard, KeyboardMaskedEdgesLatchUntilAcknowledged)
{
    pcjx_keyboard_init(&board.keyboard, pcjx_keyboard_line, &board);
    input_handler(0x1e, 1, input_priv);
    fire(board.keyboard.wire_timer);
    EXPECT_EQ(pcjx_ppi_read(0x62, &board) & 0x41, 0x41);
    EXPECT_EQ(nmi, 0);
    pcjx_nmi_write(0xa0, 0x80, &board);
    EXPECT_EQ(nmi, 1);
    pcjx_nmi_read(0xa0, &board);
    EXPECT_EQ(nmi, 0);
    EXPECT_EQ(pcjx_ppi_read(0x62, &board) & 1, 0);
    fire(board.keyboard.wire_timer);
    fire(board.keyboard.wire_timer);
    fire(board.keyboard.wire_timer);
    EXPECT_EQ(nmi, 1);
}

TEST_F(PcjxBoard, KeyboardGesturesDoNotReleaseHeldPhysicalModifiers)
{
    pcjx_keyboard_init(&board.keyboard, pcjx_keyboard_line, &board);
    input_handler(0x2a, 1, input_priv);
    input_handler(0x137, 1, input_priv);
    input_handler(0x137, 1, input_priv); // Host auto-repeat is not a new make.
    input_handler(0x137, 0, input_priv);
    input_handler(0x2a, 0, input_priv);
    std::vector<uint8_t> bytes;
    while (board.keyboard.running) {
        std::array<int, 20> levels{};
        if (!board.keyboard.queue_count && !board.keyboard.half) break;
        for (unsigned i = 0; i < levels.size(); i++) {
            fire(board.keyboard.wire_timer);
            levels[i] = board.keyboard.level;
        }
        unsigned value = 0, parity = 0;
        for (unsigned i = 0; i < 10; i++) {
            EXPECT_NE(levels[i * 2], levels[i * 2 + 1]);
            if (i > 0 && i < 9) value |= levels[i * 2] << (i - 1);
            if (i) parity ^= levels[i * 2];
        }
        EXPECT_EQ(parity, 1u);
        bytes.push_back(value);
        fire(board.keyboard.wire_timer); // End frame, then scheduled gap.
    }
    EXPECT_EQ(bytes, (std::vector<uint8_t>{0x2a, 0x37, 0xb7, 0xaa}));
}

TEST_F(PcjxBoard, ClockHoldStopReservedHourBitAndRemoval)
{
    board.rtc_enabled = 1;
    board.clock.nvr.data = &board.clock;
    pcjx_clock_nvr_reset(&board.clock.nvr);
    decode(0x93, 0x80, 0);
    output(0x36d, 1);
    output(0x364, 3);
    output(0x365, 0x89);
    output(0x360, 9);
    output(0x361, 5);
    EXPECT_EQ(input(0x365), 1);
    EXPECT_EQ(input(0x36d) & 2, 0);
    for (unsigned i = 0; i < 100; i++) EXPECT_EQ(input(0x360), 9);
    output(0x36d, 0);
    output(0x36d, 1);
    pcjx_clock_tick(&board.clock.nvr);
    pcjx_clock_tick(&board.clock.nvr);
    EXPECT_EQ(input(0x360), 9);
    output(0x36d, 0);
    EXPECT_EQ(input(0x360), 0);
    EXPECT_EQ(input(0x362), 1);
    output(0x36f, 6);
    pcjx_clock_tick(&board.clock.nvr);
    EXPECT_EQ(input(0x360), 0);
    output(0x36f, 4);
    pcjx_clock_tick(&board.clock.nvr);
    EXPECT_EQ(input(0x360), 1);
    output(0x36f, 5);
    output(0x36f, 4);
    EXPECT_EQ(input(0x366), 1);
    EXPECT_EQ(input(0x364), 3);
    board.rtc_enabled = 0;
    pcjx_rebuild_io(&board);
    EXPECT_EQ(input(0x360), 0xff);
    EXPECT_EQ(input(0x36d), 0xff);
}
} // namespace

extern "C" {
uint64_t tsc = 0, TIMER_USEC = 1ULL << 32;
uint8_t *ram;
uint32_t mem_size = 512;
cpu_state_t cpu_state;
int nmi, nmi_mask;
int keyboard_scan;
int nvr_dosave;
int time_sync;
int speaker_gated, speaker_enable, was_speaker_enable, speaker_mute;
uint8_t sn76489_mute;
int ppispeakon;
pit_intf_t pit_devs[2];
void startblit(void) {}
void endblit(void) {}
uint64_t CGACONST = 1ULL << 32;
pc_cassette_t *cassette;
void update_cga16_color(uint8_t, uint8_t) {}
void mem_mapping_disable(mem_mapping_t *) {}
unsigned char pc_cas_get_inp(const pc_cassette_t *) { return 0; }
void pc_cas_set_motor(pc_cassette_t *, unsigned char) {}
void pc_cas_set_out(pc_cassette_t *, unsigned char) {}
void fatal(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    std::abort();
}
void timer_enable(pc_timer_t *timer) { timer->flags |= TIMER_ENABLED; }
void timer_disable(pc_timer_t *timer) { timer->flags &= ~TIMER_ENABLED; }
void timer_add(pc_timer_t *timer, void (*callback)(void *), void *priv, int on)
{
    *timer = {};
    timer->callback = callback;
    timer->priv = priv;
    if (on) timer_enable(timer);
}
void keyboard_set_input_handler(void (*handler)(uint16_t, int, void *), void *priv)
{ input_handler = handler; input_priv = priv; }
void mem_mapping_recalc(uint64_t, uint64_t, uint32_t) {}
void flushmmucache(void) {}
void speaker_update(void) {}
void pcjx_test_fdc_remove(fdc_t *) {}
void pcjx_test_fdc_set_base(fdc_t *, int) {}
void gameport_set_decode(void *, int, int) {}
void pic_handler(int, uint16_t, int) {}
void pit_handler(int, uint16_t, int, void *) {}
void lpt_port_remove(lpt_t *) {}
void lpt_port_setup(lpt_t *, uint16_t) {}
void sn76489_write(uint16_t, uint8_t, void *) {}
void nvr_time_set(void *) {}
int nvr_get_days(int month, int year)
{
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}
void resetx86(void) {}
void ui_sb_update_icon_state(int, int) {}
void io_handler(uint8_t set, uint16_t base, uint16_t size,
                uint8_t (*read)(uint16_t, void *), uint16_t (*)(uint16_t, void *), uint32_t (*)(uint16_t, void *),
                void (*write)(uint16_t, uint8_t, void *), void (*)(uint16_t, uint16_t, void *), void (*)(uint16_t, uint32_t, void *), void *priv)
{
    for (int i = 0; i < size; i++) {
        auto &list = ports[(base + i) & 0xffff];
        if (set) list.push_back({read, write, priv});
        else list.erase(std::remove_if(list.begin(), list.end(), [=](const Port &p) {
            return p.read == read && p.write == write && p.priv == priv;
        }), list.end());
    }
}
void io_sethandler(uint16_t base, uint16_t size,
                   uint8_t (*read)(uint16_t, void *), uint16_t (*rw)(uint16_t, void *), uint32_t (*rl)(uint16_t, void *),
                   void (*write)(uint16_t, uint8_t, void *), void (*ww)(uint16_t, uint16_t, void *), void (*wl)(uint16_t, uint32_t, void *), void *priv)
{ io_handler(1, base, size, read, rw, rl, write, ww, wl, priv); }
void io_removehandler(uint16_t base, uint16_t size,
                      uint8_t (*read)(uint16_t, void *), uint16_t (*rw)(uint16_t, void *), uint32_t (*rl)(uint16_t, void *),
                      void (*write)(uint16_t, uint8_t, void *), void (*ww)(uint16_t, uint16_t, void *), void (*wl)(uint16_t, uint32_t, void *), void *priv)
{ io_handler(0, base, size, read, rw, rl, write, ww, wl, priv); }
}
