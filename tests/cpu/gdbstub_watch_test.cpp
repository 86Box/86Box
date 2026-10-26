#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>

extern "C" {
#include <86box/mem.h>
#include <86box/gdbstub.h>

int             gdbstub_watching;
uint64_t        gdbstub_watch_pages[(((uint32_t) -1) >> (MEM_GRANULARITY_BITS + 6)) + 1];
static unsigned hits;
static int      last_access;
void
gdbstub_mem_access(uint32_t *, int access)
{
    ++hits;
    last_access = access;
}
}

class GdbWatchTest : public testing::Test {
protected:
    void SetUp() override
    {
        std::memset(gdbstub_watch_pages, 0, sizeof(gdbstub_watch_pages));
        gdbstub_watching = 1;
        hits             = 0;
    }
    static void watch(uint32_t addr)
    {
        const uint32_t page = addr >> MEM_GRANULARITY_BITS;
        gdbstub_watch_pages[page >> 6] |= 1ULL << (page & 63);
    }
};

TEST_F(GdbWatchTest, DisabledDoesNotCheckBitmap)
{
    watch(0);
    gdbstub_watching = 0;
    GDBSTUB_MEM_ACCESS(0, GDBSTUB_MEM_READ, 1);
    EXPECT_EQ(hits, 0u);
    EXPECT_FALSE(GDBSTUB_WATCHED(0));
}

TEST_F(GdbWatchTest, WatchesLastPageOfCrossingAccess)
{
    const uint32_t boundary = 1U << MEM_GRANULARITY_BITS;
    watch(boundary);
    GDBSTUB_MEM_ACCESS(boundary - 1, GDBSTUB_MEM_WRITE, 2);
    EXPECT_EQ(hits, 1u);
    EXPECT_EQ(last_access, GDBSTUB_MEM_WRITE | 2);
}

TEST_F(GdbWatchTest, ChecksNoncontiguousTranslatedAddresses)
{
    uint32_t addresses[4] = { 0x1000, 0x1001, 0x9000, 0x9001 };
    watch(0x9000);
    GDBSTUB_MEM_ACCESS_FAST(addresses, GDBSTUB_MEM_READ, 4);
    EXPECT_EQ(hits, 1u);
    EXPECT_EQ(last_access, 4);
}

TEST_F(GdbWatchTest, HandlesLinearAddressWrap)
{
    watch(0);
    GDBSTUB_MEM_ACCESS(UINT32_MAX, GDBSTUB_MEM_READ, 2);
    EXPECT_EQ(hits, 1u);
}
