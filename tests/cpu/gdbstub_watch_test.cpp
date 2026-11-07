#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>

extern "C" {
#include <86box/cpu_events.h>
#include <86box/mem.h>
#include <86box/gdbstub.h>

int             gdbstub_watching;
cpu_events_t    cpu_events;
uint64_t        gdbstub_watch_pages[(((uint32_t) -1) >> (MEM_GRANULARITY_BITS + 6)) + 1];
static unsigned hits;
static int      last_access;
static uint32_t last_addrs[8];
void
gdbstub_mem_access(uint32_t *addrs, int access)
{
    ++hits;
    last_access = access;
    std::memcpy(last_addrs, addrs, (access & (GDBSTUB_MEM_WRITE - 1)) * sizeof(*addrs));
}
}

class GdbWatchTest : public testing::Test {
protected:
    void SetUp() override
    {
        std::memset(gdbstub_watch_pages, 0, sizeof(gdbstub_watch_pages));
        gdbstub_watching = 1;
        cpu_dr_armed     = 0;
        mem_debug_update();
        hits             = 0;
    }
    static void watch(uint32_t addr)
    {
        const uint32_t page = addr >> MEM_GRANULARITY_BITS;
        gdbstub_watch_pages[page >> 6] |= 1ULL << (page & 63);
    }
};

TEST_F(GdbWatchTest, SharedMemoryGuardTracksBothDebuggers)
{
    for (int guest = 0; guest <= 1; guest++) {
        for (int remote = 0; remote <= 1; remote++) {
            cpu_dr_armed     = guest;
            gdbstub_watching = remote;
            mem_debug_update();
            EXPECT_EQ(mem_debug_active, guest || remote);
        }
    }

    gdbstub_watching = 0;
    mem_debug_update();
    EXPECT_TRUE(mem_debug_active);
    cpu_dr_armed = 0;
    mem_debug_update();
    EXPECT_FALSE(mem_debug_active);
}

TEST_F(GdbWatchTest, DisabledDoesNotCheckBitmap)
{
    watch(0);
    gdbstub_watching = 0;
    GDBSTUB_MEM_ACCESS(0, GDBSTUB_MEM_READ, 1);
    EXPECT_EQ(hits, 0u);
    EXPECT_FALSE(GDBSTUB_WATCHED(0));
    GDBSTUB_MEM_ACCESS_FAST(nullptr, GDBSTUB_MEM_READ, 4);
    EXPECT_EQ(hits, 0u);
}

TEST_F(GdbWatchTest, WatchesLastPageOfCrossingAccess)
{
    const uint32_t boundary = 1U << MEM_GRANULARITY_BITS;
    watch(boundary);
    GDBSTUB_MEM_ACCESS(boundary - 1, GDBSTUB_MEM_WRITE, 2);
    EXPECT_EQ(hits, 1u);
    EXPECT_EQ(last_access, GDBSTUB_MEM_WRITE | 2);
    EXPECT_EQ(last_addrs[0], boundary - 1);
    EXPECT_EQ(last_addrs[1], boundary);
}

TEST_F(GdbWatchTest, ChecksNoncontiguousTranslatedAddresses)
{
    uint32_t addresses[4] = { 0x1000, 0x1001, 0x9000, 0x9001 };
    watch(0x9000);
    GDBSTUB_MEM_ACCESS_FAST(addresses, GDBSTUB_MEM_READ, 4);
    EXPECT_EQ(hits, 1u);
    EXPECT_EQ(last_access, 4);
    EXPECT_EQ(std::memcmp(last_addrs, addresses, sizeof(addresses)), 0);
}

TEST_F(GdbWatchTest, HandlesLinearAddressWrap)
{
    watch(0);
    GDBSTUB_MEM_ACCESS(UINT32_MAX, GDBSTUB_MEM_READ, 2);
    EXPECT_EQ(hits, 1u);
    EXPECT_EQ(last_addrs[0], UINT32_MAX);
    EXPECT_EQ(last_addrs[1], 0u);
}

TEST_F(GdbWatchTest, UnwatchedPagesDoNotReachAccessHandler)
{
    watch(0x9000);
    uint32_t addresses[8] = { 0x1000, 0x1001, 0x1002, 0x1003,
                              0x1004, 0x1005, 0x1006, 0x1007 };
    GDBSTUB_MEM_ACCESS(0x1000, GDBSTUB_MEM_WRITE, 8);
    GDBSTUB_MEM_ACCESS_FAST(addresses, GDBSTUB_MEM_READ, 8);
    EXPECT_EQ(hits, 0u);
}

TEST_F(GdbWatchTest, PreservesEveryByteOfEachSupportedWidth)
{
    watch(0x1000);
    for (int width = 1; width <= 8; width *= 2) {
        GDBSTUB_MEM_ACCESS(0x1000, GDBSTUB_MEM_READ, width);
        EXPECT_EQ(last_access, width);
        for (int i = 0; i < width; i++)
            EXPECT_EQ(last_addrs[i], 0x1000u + i);
    }
    EXPECT_EQ(hits, 4u);
}
