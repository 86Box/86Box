/* Register-level tests using the same I/O handlers registered by the adapters. */
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>
extern "C" {
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/scsi_aic6360.h>
}

static uint8_t (*read_byte)(uint16_t, void *);
static uint16_t (*read_word)(uint16_t, void *);
static uint32_t (*read_dword)(uint16_t, void *);
static void (*write_byte)(uint16_t, uint8_t, void *);
static void (*write_word)(uint16_t, uint16_t, void *);
static void (*write_dword)(uint16_t, uint32_t, void *);
static pc_timer_t             *pending_timer;
static unsigned                irq_pending, command_count, completion_count, stop_count, reset_count;
static unsigned                cfg_base, cfg_irq, cfg_rom, mapped_rom, next_bus;
static std::array<uint8_t, 16> last_cdb;
static int                     transfer_length;
static uint8_t                 target_status, target_phase;

extern "C" {
scsi_device_t scsi_devices[SCSI_BUS_MAX][SCSI_ID_MAX];
int           scsi_command_length[8] = { 6, 10, 10, 6, 16, 12, 10, 6 };
void
picint_common(uint16_t mask, int, int set, uint8_t *)
{
    if (set)
        irq_pending |= mask;
    else
        irq_pending &= ~mask;
}
void
io_sethandler(uint16_t base, uint16_t size,
              uint8_t (*rb)(uint16_t, void *), uint16_t (*rw)(uint16_t, void *), uint32_t (*rl)(uint16_t, void *),
              void (*wb)(uint16_t, uint8_t, void *), void (*ww)(uint16_t, uint16_t, void *), void (*wl)(uint16_t, uint32_t, void *), void *)
{
    EXPECT_EQ(base, cfg_base);
    EXPECT_EQ(size, 32);
    read_byte   = rb;
    read_word   = rw;
    read_dword  = rl;
    write_byte  = wb;
    write_word  = ww;
    write_dword = wl;
}
void
io_removehandler(uint16_t, uint16_t,
                 uint8_t (*)(uint16_t, void *), uint16_t (*)(uint16_t, void *), uint32_t (*)(uint16_t, void *),
                 void (*)(uint16_t, uint8_t, void *), void (*)(uint16_t, uint16_t, void *), void (*)(uint16_t, uint32_t, void *), void *)
{
}
int
device_get_config_int(const char *)
{
    return cfg_irq;
}
int
device_get_config_hex16(const char *)
{
    return cfg_base;
}
int
device_get_config_hex20(const char *)
{
    return cfg_rom;
}
uint8_t
scsi_get_bus(void)
{
    return next_bus++;
}
void
scsi_bus_set_speed(uint8_t bus, double speed)
{
    EXPECT_LT(bus, SCSI_BUS_MAX);
    EXPECT_EQ(speed, 5000000.0);
}
int
scsi_device_present(scsi_device_t *sd)
{
    return sd->type != SCSI_NONE;
}
void
scsi_device_reset(scsi_device_t *)
{
    reset_count++;
}
void
scsi_device_identify(scsi_device_t *sd, uint8_t lun)
{
    if (sd->sc)
        sd->sc->cur_lun = lun;
}
void
scsi_device_command_phase0(scsi_device_t *sd, uint8_t *cdb)
{
    EXPECT_EQ(sd->buffer_length, -1);
    std::memcpy(last_cdb.data(), cdb, last_cdb.size());
    command_count++;
    sd->buffer_length = transfer_length;
    sd->phase         = target_phase;
    sd->status        = target_status;
}
void
scsi_device_command_phase1(scsi_device_t *sd)
{
    completion_count++;
    sd->phase = SCSI_PHASE_STATUS;
}
void
scsi_device_command_stop(scsi_device_t *)
{
    stop_count++;
}
double
scsi_device_get_callback(scsi_device_t *)
{
    return 25.0;
}
void
timer_add(pc_timer_t *timer, void (*cb)(void *), void *priv, int)
{
    timer->callback = cb;
    timer->priv     = priv;
    pending_timer   = timer;
}
void
timer_on_auto(pc_timer_t *timer, double period)
{
    timer->period = period;
    timer->flags |= TIMER_ENABLED;
}
void
timer_stop(pc_timer_t *timer)
{
    timer->flags &= ~TIMER_ENABLED;
}
int
rom_present(const char *path)
{
    EXPECT_STREQ(path, "roms/scsi/adaptec/aic6360-v1-20l.bin");
    return 1;
}

void
mem_mapping_disable(mem_mapping_t *)
{
}
int
rom_init(rom_t *rom, const char *path, uint32_t addr, int size, int mask, int offset, uint32_t)
{
    EXPECT_TRUE(rom_present(path));
    EXPECT_EQ(size, 0x4000);
    EXPECT_EQ(mask, 0x3fff);
    EXPECT_EQ(offset, 0);
    mapped_rom = addr;
    rom->rom   = static_cast<uint8_t *>(std::calloc(1, size));
    return 1;
}
}

class Aic6360 : public testing::Test {
protected:
    void                     *dev = nullptr;
    scsi_common_t             target { };
    std::array<uint8_t, 1024> data { };
    void                      SetUp() override
    {
        std::memset(scsi_devices, 0, sizeof(scsi_devices));
        for (auto &bus : scsi_devices)
            for (auto &sd : bus)
                sd.type = SCSI_NONE;
        target.temp_buffer      = data.data();
        target.temp_buffer_sz   = data.size();
        scsi_devices[0][2].type = SCSI_FIXED_DISK;
        scsi_devices[0][2].sc   = &target;
        for (unsigned i = 0; i < data.size(); i++)
            data[i] = i ^ 0x5a;
        command_count = completion_count = stop_count = irq_pending = reset_count = next_bus = mapped_rom = 0;
        cfg_base                                                                                          = 0x340;
        cfg_irq                                                                                           = 11;
        cfg_rom                                                                                           = 0;
        transfer_length                                                                                   = 0;
        target_status                                                                                     = SCSI_STATUS_OK;
        target_phase                                                                                      = SCSI_PHASE_STATUS;
        dev                                                                                               = sb16_scsi_device.init(&sb16_scsi_device);
    }
    void TearDown() override
    {
        if (dev)
            sb16_scsi_device.close(dev);
    }
    void    out(unsigned reg, uint8_t value) { write_byte(cfg_base + reg, value, dev); }
    uint8_t in(unsigned reg) { return read_byte(cfg_base + reg, dev); }
    void    fire()
    {
        ASSERT_TRUE(pending_timer->flags & TIMER_ENABLED);
        pending_timer->flags &= ~TIMER_ENABLED;
        pending_timer->callback(pending_timer->priv);
    }
    void phase(uint8_t p) { out(3, p); }
    void select(bool atn = false, unsigned id = 2)
    {
        out(5, 0x70 | id);
        out(2, 0x24);
        out(1, 0x28);
        out(0, atn ? 0x48 : 0x40);
        fire();
        out(0, 0);
    }
    void command(uint8_t opcode = 0x12)
    {
        phase(0x80);
        out(6, opcode);
        for (int i = 1; i < scsi_command_length[opcode >> 5]; i++)
            out(6, i);
        ASSERT_EQ(command_count, 1);
        EXPECT_FALSE(in(0x0b) & 2); // Target has not completed its command delay.
        fire();
    }
    void finish()
    {
        out(1, 0x28);
        phase(0xc0);
        EXPECT_EQ(in(6), target_status);
        EXPECT_EQ(in(3) & 0xe0, 0xe0);
        phase(0xe0);
        EXPECT_EQ(in(6), 0);
        EXPECT_EQ(in(3), 0);
        EXPECT_TRUE(in(0x0c) & 8);
        EXPECT_EQ(target.cur_lun, SCSI_LUN_USE_CDB);
    }
    uint32_t count() { return in(8) | (in(9) << 8) | (in(10) << 16); }
};

TEST_F(Aic6360, DetectionStackWrapAndSoftwareInterrupt)
{
    EXPECT_EQ(in(0x1c), 1);
    for (unsigned size : { 16u, 32u }) {
        out(0x13, size == 32 ? 0x40 : 0);
        for (unsigned i = 0; i < size; i++)
            out(0x1d, i ^ 0xa5);
        EXPECT_EQ(in(0x13), size == 32 ? 0x40 : 0);
        for (unsigned i = 0; i < size; i++)
            EXPECT_EQ(in(0x1d), (i ^ 0xa5));
        EXPECT_EQ(in(0x1d), 0xa5);
    }
    out(0x12, 1);
    EXPECT_TRUE(in(0x14) & 0x20);
    EXPECT_EQ(irq_pending, 0);
    out(0x12, 5);
    EXPECT_EQ(irq_pending, 1u << 11);
    out(0x12, 4);
    EXPECT_EQ(irq_pending, 0);
}

TEST_F(Aic6360, SelectionInterruptAcknowledgementAndPhaseMismatch)
{
    out(0x10, 0x40);
    out(0x12, 4);
    select();
    EXPECT_EQ(irq_pending, 1u << 11);
    out(0x0b, 0x40);
    EXPECT_EQ(irq_pending, 0);
    EXPECT_TRUE(in(0x0b) & 0x40);
    EXPECT_TRUE(in(0x0c) & 0x10);
    phase(0x80);
    EXPECT_FALSE(in(0x0c) & 0x10);
    command();
    finish();
    EXPECT_FALSE(in(0x0b) & 0x40);
}

TEST_F(Aic6360, SelectionTimeoutAndCancellation)
{
    out(5, 0x73);
    out(2, 0x1c);
    out(0x11, 0x80);
    out(0x12, 4);
    out(0, 0x40);
    EXPECT_DOUBLE_EQ(pending_timer->period, 32000.0);
    EXPECT_EQ(irq_pending, 0);
    fire();
    EXPECT_TRUE(in(0x0c) & 0x80);
    EXPECT_EQ(irq_pending, 1u << 11);
    out(0x0c, 0x80);
    EXPECT_EQ(irq_pending, 0);
    out(0, 0);
    out(0, 0x40);
    out(0, 0);
    EXPECT_FALSE(pending_timer->flags & TIMER_ENABLED);
    EXPECT_FALSE(in(0x0b) & 0x10);
}

TEST_F(Aic6360, IdentifyAndAsynchronousNegotiation)
{
    select(true);
    phase(0xa0);
    out(6, 0x83);
    EXPECT_EQ(target.cur_lun, 3);
    out(6, 1);
    out(6, 3);
    out(6, 1);
    out(6, 50);
    out(0x0c, 0x40);
    out(6, 8);
    phase(0xe0);
    for (uint8_t value : { 1, 3, 1, 50, 0 })
        EXPECT_EQ(in(6), value);
    EXPECT_EQ(in(3) & 0xe0, 0x80);
    command();
    finish();
}

TEST_F(Aic6360, UnsupportedLongMessageIsConsumedAndRejected)
{
    select(true);
    phase(0xa0);
    out(6, 1);
    out(6, 255);
    for (int i = 0; i < 254; i++)
        out(6, 0xff);
    out(0x0c, 0x40);
    out(6, 0xff);
    phase(0xe0);
    EXPECT_EQ(in(6), 7);
    command(0x88);
    EXPECT_EQ(last_cdb[15], 15);
    finish();
}

TEST_F(Aic6360, BiosPreloadsMessageBeforeEnablingPio)
{
    select(true);
    out(1, 0x20);
    out(6, 0xc0);    // IDENTIFY, written while automatic PIO is disabled.
    out(0x0c, 0x40); // Drop ATN before transferring the last message byte.
    phase(0xa0);
    out(1, 0x28); // Enabling PIO sends the preloaded latch.
    in(6);        // BIOS reads the latch to complete the handshake.
    out(1, 0x20);
    EXPECT_EQ(target.cur_lun, 0);
    EXPECT_EQ(in(3) & 0xe0, 0x80);
    out(1, 0x28);
    command();
    finish();
}

TEST_F(Aic6360, PioInquiryAndCheckCondition)
{
    transfer_length = 36;
    target_phase    = SCSI_PHASE_DATA_IN;
    select();
    command();
    phase(0x40);
    for (unsigned i = 0; i < 36; i++)
        EXPECT_EQ(in(6), data[i]);
    EXPECT_EQ(completion_count, 1);
    finish();
}

TEST_F(Aic6360, CheckConditionHasNoDataPhase)
{
    target_status = SCSI_STATUS_CHECK_CONDITION;
    select();
    command();
    EXPECT_EQ(in(3) & 0xe0, 0xc0);
    finish();
    EXPECT_EQ(completion_count, 0);
}

TEST_F(Aic6360, FifoReadRetainsOddResidualAfterPhaseChange)
{
    transfer_length = 259;
    target_phase    = SCSI_PHASE_DATA_IN;
    select();
    command(0x28);
    phase(0x40);
    out(0x12, 0x82);
    out(1, 0x30);
    out(1, 0xe0);
    out(0x11, 0x10);
    EXPECT_EQ(in(0x15), 128);
    EXPECT_TRUE(in(0x14) & 0x10);
    for (unsigned i = 0; i < 258; i += 2)
        EXPECT_EQ(read_word(cfg_base + 0x16, dev), data[i] | (data[i + 1] << 8));
    EXPECT_EQ(count(), 259);
    EXPECT_EQ(in(0x15), 1);
    EXPECT_TRUE(in(0x14) & 0x20);
    EXPECT_EQ(irq_pending, 0);
    out(0x12, 0xc0);
    EXPECT_EQ(in(0x16), data[258]);
    EXPECT_TRUE(in(0x14) & 8);
    EXPECT_EQ(completion_count, 1);
    finish();
}

TEST_F(Aic6360, FifoWriteAndCounterWrap)
{
    transfer_length = 259;
    target_phase    = SCSI_PHASE_DATA_OUT;
    select();
    command(0x2a);
    phase(0);
    out(0x12, 0x8a);
    out(1, 0xe0);
    out(8, 0xfe);
    out(9, 0xff);
    out(10, 0xff);
    for (unsigned i = 0; i < 258; i += 2)
        write_word(cfg_base + 0x16, i | ((i + 1) << 8), dev);
    out(0x12, 0xc8);
    out(0x16, 2);
    for (unsigned i = 0; i < 259; i++)
        EXPECT_EQ(data[i], uint8_t(i));
    EXPECT_EQ(count(), 257);
    EXPECT_TRUE(in(0x0b) & 8);
    EXPECT_EQ(completion_count, 1);
    finish();
}

TEST_F(Aic6360, DwordPioAliasesAndFifoReset)
{
    out(0x12, 0x98);
    write_dword(cfg_base + 0x16, 0x44332211, dev);
    write_word(cfg_base + 0x1a, 0x6655, dev);
    EXPECT_EQ(in(0x15), 6);
    out(0x12, 0x90);
    EXPECT_EQ(read_dword(cfg_base + 0x16, dev), 0x44332211u);
    EXPECT_EQ(read_word(cfg_base + 0x1a, dev), 0x6655);
    out(0x12, 0x9a);
    EXPECT_EQ(in(0x15), 0);
    EXPECT_EQ(in(0x12), 0x98);
}

TEST_F(Aic6360, ResetCancelsPendingCommandAndClearsInterrupt)
{
    select();
    phase(0x80);
    for (int i = 0; i < 6; i++)
        out(6, 0);
    EXPECT_TRUE(pending_timer->flags & TIMER_ENABLED);
    out(0x11, 0x20);
    out(0x12, 4);
    out(0, 1);
    EXPECT_FALSE(pending_timer->flags & TIMER_ENABLED);
    EXPECT_EQ(stop_count, 1);
    EXPECT_EQ(reset_count, 16);
    EXPECT_EQ(irq_pending, 1u << 11);
    out(0, 0);
    out(0x0c, 0x20);
    EXPECT_EQ(irq_pending, 0);
    sb16_scsi_device.reset(dev);
    EXPECT_EQ(in(0x12), 0);
    EXPECT_EQ(count(), 0);
}

TEST_F(Aic6360, RomAndSecondaryIoAddress)
{
    sb16_scsi_device.close(dev);
    dev      = nullptr;
    cfg_rom  = 0xdc000;
    cfg_base = 0x140;
    cfg_irq  = 10;
    dev      = aha1520a_device.init(&aha1520a_device);
    EXPECT_EQ(mapped_rom, 0xdc000);
    EXPECT_EQ(in(0x1c), 1);
    EXPECT_TRUE(in(0x1b) & 0x40);
    EXPECT_EQ(in(0x1a), 0x0f);
    EXPECT_TRUE(aha1520a_device.available());
}

TEST_F(Aic6360, BusExhaustionDoesNotIndexPastDevices)
{
    next_bus = 0xff;
    EXPECT_EQ(sb16_scsi_device.init(&sb16_scsi_device), nullptr);
}
