// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise production cache and CPU/bus memory dispatch with separate shadow
// read/write backing, as used by the HP Vectra 486N MPU-cache diagnostic.
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>
extern "C" {
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>

int      in_lock;
uint32_t cr2, cr3, cr4;
uint32_t addr64;
uint32_t addr64a[8];
int      cpl_override;
extern uint32_t abrt_error;
void
fatal(const char *, ...)
{
    std::abort();
}
void
pclog(const char *, ...)
{
}
int test_vl82c486_cache_policy(uint32_t addr, const uint8_t *regs);
}

class CpuCacheTest : public testing::Test {
protected:
    static std::array<uint8_t, 2 * 1024 * 1024> memory;
    static mem_mapping_t                       &mapping;
    static std::array<int, 128>                 policy;
    static bool                                 external_reads;
    static unsigned                             bus_reads;
    static unsigned                             bus_writes;

    static uint8_t rb(uint32_t addr, void *)
    {
        ++bus_reads;
        addreadlookup(mem_logical_addr, addr);
        return external_reads && addr >= 0xd0000 && addr < 0xd4000 ? 0xff : memory.at(addr);
    }
    static uint16_t rw(uint32_t addr, void *p) { return rb(addr, p) | (uint16_t(rb(addr + 1, p)) << 8); }
    static uint32_t rl(uint32_t addr, void *p) { return rw(addr, p) | (uint32_t(rw(addr + 2, p)) << 16); }
    static void     wb(uint32_t addr, uint8_t val, void *)
    {
        ++bus_writes;
        addwritelookup(mem_logical_addr, addr);
        memory.at(addr) = val;
    }
    static void ww(uint32_t addr, uint16_t val, void *p)
    {
        wb(addr, val, p);
        wb(addr + 1, val >> 8, p);
    }
    static void wl(uint32_t addr, uint32_t val, void *p)
    {
        ww(addr, val, p);
        ww(addr + 2, val >> 16, p);
    }
    static int cache_policy(uint32_t addr, void *) { return addr < memory.size() ? policy[addr >> 14] : 0; }

    void SetUp() override
    {
        memory.fill(0);
        policy.fill(CPU_CACHE_FILL);
        external_reads = false;
        bus_reads = bus_writes = 0;
        ram                    = memory.data();
        rammask                = 0xffffffff;
        cpu_state              = { };
        cpu_f                  = nullptr;
        cr3 = cr4               = 0;
        in_lock                = 0;
        cpl_override           = 0;
        is486                  = 1;
        isibm486               = 0;
        cpu_features           = 0;
        mem_logical_addr        = 0xffffffff;
        resetreadlookup();
        cpu_tr_reset();
        // The mapping persists between tests, like the machine's RAM mapping.
        static bool mapped = false;
        if (!mapped) {
            mem_mapping_add(&mapping, 0, memory.size(), rb, rw, rl, wb, ww, wl,
                            memory.data(), MEM_MAPPING_INTERNAL, nullptr);
            mem_set_mem_state(0, memory.size(), MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
            mapped = true;
        }
        cpu_cache_set_handler(cache_policy, nullptr);
    }
    void TearDown() override { cpu_cache_set_handler(nullptr, nullptr); }
};

std::array<uint8_t, 2 * 1024 * 1024> CpuCacheTest::memory;
mem_mapping_t                       &CpuCacheTest::mapping = ram_low_mapping;
std::array<int, 128>                 CpuCacheTest::policy;
bool                                 CpuCacheTest::external_reads;
unsigned                             CpuCacheTest::bus_reads;
unsigned                             CpuCacheTest::bus_writes;

TEST_F(CpuCacheTest, FillsWholeLineAndWritesThroughWithoutAllocatingOnWriteMiss)
{
    writememll(0x1000, 0x12345678);
    EXPECT_EQ(bus_reads, 0u);
    EXPECT_EQ(readmemll(0x1000), 0x12345678u);
    EXPECT_EQ(bus_reads, 16u);
    readmemll(0x100c);
    EXPECT_EQ(bus_reads, 16u);
    writememll(0x1004, 0xabcdef01);
    EXPECT_EQ(readmemll(0x1004), 0xabcdef01u);
    EXPECT_EQ(rl(0x1004, nullptr), 0xabcdef01u);
    EXPECT_EQ(bus_writes, 8u);
    EXPECT_EQ(readlookup2[1], uintptr_t(LOOKUP_INV));
    EXPECT_EQ(writelookup2[1], uintptr_t(LOOKUP_INV));
    EXPECT_EQ(page_lookup[1], nullptr);
}

TEST_F(CpuCacheTest, HpShadowTestFitsEightKiBButEvictsAtEightKiBPlusFour)
{
    external_reads = true;
    for (unsigned length : { 8192u, 8196u }) {
        cpu_cache_invalidate();
        for (unsigned i = 0; i < length; i += 4) {
            readmemll(0xd0000 + i);
            writememll(0xd0000 + i, 0x14c714c7);
        }
        EXPECT_EQ(readmemll(0xd0000), length == 8192 ? 0x14c714c7u : 0xffffffffu);
    }
}

TEST_F(CpuCacheTest, CdAndKenDisableFillsButRetainHits)
{
    writememll(0x1000, 0x12345678);
    readmemll(0x1000);
    cr0 = 0x40000000;
    policy.fill(0);
    memory[0x1000] = 0;
    EXPECT_EQ(readmemll(0x1000), 0x12345678u);
    unsigned before = bus_reads;
    readmemll(0x2000);
    readmemll(0x2000);
    EXPECT_EQ(bus_reads - before, 8u);
}

TEST_F(CpuCacheTest, NwSuppressesOnlyWriteHitsAndInvdDiscardsCachedWrites)
{
    writememll(0xd8000, 0x55555555);
    readmemll(0xd8000);
    cr0 = 0x60000000;
    writememll(0xd8000, 0xbfbfbfbf);
    writememll(0xd9000, 0xabcdef01); // miss still goes to the bus
    EXPECT_EQ(rl(0xd8000, nullptr), 0x55555555u);
    EXPECT_EQ(rl(0xd9000, nullptr), 0xabcdef01u);
    cr0 = 0;
    EXPECT_EQ(readmemll(0xd8000), 0xbfbfbfbfu);
    cpu_cache_invalidate();
    EXPECT_EQ(readmemll(0xd8000), 0x55555555u);
}

TEST_F(CpuCacheTest, PartialAndCrossLineWritesPreserveOtherBytes)
{
    readmemll(0x1000);
    cr0 = 0x60000000;
    writememql(0x100c, 0x8877665544332211ULL); // cached low half, uncached high half
    EXPECT_EQ(readmemql(0x100c), 0x8877665544332211ULL);
    EXPECT_EQ(rl(0x100c, nullptr), 0u);
    EXPECT_EQ(rl(0x1010, nullptr), 0x88776655u);
    writemembl(0x100d, 0xaa);
    writememwl(0x100e, 0xbbcc);
    EXPECT_EQ(readmemll(0x100c), 0xbbccaa11u);
    // Crossing a page takes the translated/no_mmut paths.
    writememql(0x1ffd, 0x0123456789abcdefULL);
    EXPECT_EQ(readmemql(0x1ffd), 0x0123456789abcdefULL);
}

TEST_F(CpuCacheTest, SnoopInvalidatesDmaWritesExceptWhenNwIsSet)
{
    readmemll(0x1000);
    mem_writel_phys(0x1000, 0x12345678);
    EXPECT_EQ(readmemll(0x1000), 0x12345678u);
    cr0 = 0x60000000;
    mem_writew_phys(0x1000, 0xabcd);
    EXPECT_EQ(readmemll(0x1000), 0x12345678u);
    cpu_cache_invalidate();
    EXPECT_EQ(readmemll(0x1000), 0x1234abcdu);
}

TEST_F(CpuCacheTest, WriteProtectionInvalidatesWithoutProtectingBackingRam)
{
    readmemll(0x1000);
    policy[0] |= CPU_CACHE_WRITE_PROTECT;
    writememll(0x1000, 0x12345678);
    memory[0x1000] = 0xab;
    EXPECT_EQ(readmemll(0x1000), 0x123456abu);
}

TEST_F(CpuCacheTest, TestRegistersShareDataAndCacheAsRamDoesNotWriteBacking)
{
    cr0 = 0x60000000;
    for (unsigned i = 0; i < 4; ++i) {
        cpu_tr_write(5, i << 2);
        cpu_tr_write(3, 0x11223344 + i);
    }
    cpu_tr_write(4, 0xd0400); // physical D0000, valid
    cpu_tr_write(5, 1);       // set 0, way 0, write
    EXPECT_EQ(readmemll(0xd0000), 0x11223344u);
    EXPECT_EQ(rl(0xd0000, nullptr), 0u);
    writememll(0xd0004, 0xaabbccdd);
    cpu_tr_write(5, 2);                        // copy line to read buffer
    EXPECT_EQ(cpu_tr_read(4) & 0x380, 0x180u); // way 0 most recent
    cpu_tr_write(5, 4);                        // select second doubleword
    EXPECT_EQ(cpu_tr_read(3), 0xaabbccddu);
    cpu_tr_write(5, 3); // invalidate
    EXPECT_EQ(readmemll(0xd0000), 0u);
}

TEST_F(CpuCacheTest, FetchesCannotUseDirectRamPointers)
{
    EXPECT_EQ(getpccache(0x1000), nullptr);
    EXPECT_EQ(pccache, 0xffffffffu);
    EXPECT_NE(cpu_fetch_device, 0);
}

TEST_F(CpuCacheTest, PcdFromCr3OrPageTablePreventsAllocation)
{
    cr3 = 0x10;
    readmemll(0x5000);
    readmemll(0x5000);
    EXPECT_EQ(bus_reads, 8u);
    wl(0x1000, 0x2003, nullptr);
    wl(0x2000, 0x5013, nullptr); // linear 0 -> physical 5000, PCD set
    cr3       = 0x1000;
    cr0       = 0x80000001;
    bus_reads = 0;
    readmemll(0);
    readmemll(0);
    EXPECT_EQ(bus_reads, 40u);       // two cached page-table fills plus two uncached data reads
    mem_writel_phys(0x2000, 0x5003); // DMA snoop invalidates the old PTE line
    bus_reads = 0;
    readmemll(0);
    readmemll(0);
    EXPECT_EQ(bus_reads, 32u); // PTE refill and data fill
}

TEST_F(CpuCacheTest, DisablingCacheRestoresDirectPageTableWalks)
{
    wl(0x1000, 0x2003, nullptr);
    wl(0x2000, 0x5003, nullptr);
    cr3 = 0x1000;
    cr0 = 0x80000001;
    ASSERT_EQ(mmutranslatereal(0x123, 0), 0x5123u);

    // Change backing RAM without a snoop: the cached walker retains its PTE.
    memory[0x2001] = 0x60;
    EXPECT_EQ(mmutranslatereal(0x123, 0), 0x5123u);
    // The accessed-bit write went through to RAM; replace the PTE again.
    memory[0x2001] = 0x60;
    cpu_cache_set_handler(nullptr, nullptr);
    bus_reads = bus_writes = 0;
    EXPECT_EQ(mmutranslatereal(0x123, 1), 0x6123u);
    EXPECT_EQ(bus_reads, 0u);
    EXPECT_EQ(bus_writes, 0u);
    EXPECT_EQ(memory[0x2000] & 0x60, 0x60);

    cpu_cache_set_handler(cache_policy, nullptr);
    EXPECT_EQ(mmutranslatereal(0x123, 0), 0x6123u);
    EXPECT_GT(bus_reads, 0u);
}

TEST_F(CpuCacheTest, BothPageWalkersUpdateAccessedAndDirtyBits)
{
    for (bool cached : { false, true }) {
        SCOPED_TRACE(cached);
        cpu_cache_set_handler(cached ? cache_policy : nullptr, nullptr);
        wl(0x1000, 0x2007, nullptr);
        wl(0x2000, 0x5007, nullptr);
        cr3 = 0x1000;
        cr0 = 0x80000001;
        cr4 = 0;
        cpu_state.seg_cs.access = 3 << 5;
        ASSERT_EQ(mmutranslatereal(0x123, 0), 0x5123u);
        EXPECT_EQ(rl(0x1000, nullptr), 0x2027u);
        EXPECT_EQ(rl(0x2000, nullptr), 0x5027u);
        ASSERT_EQ(mmutranslatereal(0x123, 1), 0x5123u);
        EXPECT_EQ(rl(0x2000, nullptr), 0x5067u);

        // Retain the existing large-page behavior in both implementations.
        cpu_cache_invalidate();
        wl(0x1000, 0x400087, nullptr);
        cr4 = CR4_PSE;
        ASSERT_EQ(mmutranslatereal(0x12345, 1), 0x412345u);
        EXPECT_EQ(rl(0x1000, nullptr), 0x4000e7u);
    }
}

TEST_F(CpuCacheTest, BothPageWalkersPreserveFaultsAndPermissions)
{
    const struct {
        const char *name;
        uint32_t    pde, pte;
        int         cpl, write;
        uint32_t    error;
    } cases[] = {
        { "directory not present", 0, 0x5007, 0, 0, 0 },
        { "table not present", 0x2007, 0, 0, 0, 0 },
        { "user reads supervisor page", 0x2007, 0x5003, 3, 0, 5 },
        { "supervisor writes read-only page with WP", 0x2007, 0x5005, 0, 1, 3 },
    };
    for (bool cached : { false, true }) {
        SCOPED_TRACE(cached);
        cpu_cache_set_handler(cached ? cache_policy : nullptr, nullptr);
        cr3 = 0x1000;
        cr0 = 0x80010001; // paging, supervisor write protection
        for (const auto &fault : cases) {
            SCOPED_TRACE(fault.name);
            cpu_cache_invalidate();
            cpu_state.abrt = 0;
            cpu_state.seg_cs.access = fault.cpl << 5;
            wl(0x1000, fault.pde, nullptr);
            wl(0x2000, fault.pte, nullptr);
            EXPECT_EQ(mmutranslatereal(0x123, fault.write), UINT64_MAX);
            EXPECT_NE(cpu_state.abrt, 0);
            EXPECT_EQ(abrt_error, fault.error);
            EXPECT_EQ(cr2, 0x123u);
            EXPECT_EQ(memory[0x1000] & 0x20, 0);
            EXPECT_EQ(memory[0x2000] & 0x60, 0);
        }
    }
}

TEST_F(CpuCacheTest, VlsiSegmentControlsAndGlobalEnable)
{
    std::array<uint8_t, 256> regs { };
    regs[7] = 8;
    for (unsigned seg = 0; seg < 6; ++seg)
        for (unsigned block = 0; block < 4; ++block)
            for (unsigned mode = 0; mode < 4; ++mode) {
                regs[0x13 + seg] = mode << (block * 2);
                EXPECT_EQ(test_vl82c486_cache_policy(0xa0000 + seg * 65536 + block * 16384, regs.data()),
                          int((mode & 2) | ((mode & 1) ? 0 : CPU_CACHE_FILL)));
            }
    EXPECT_EQ(test_vl82c486_cache_policy(0x1000, regs.data()), CPU_CACHE_FILL);
    EXPECT_EQ(test_vl82c486_cache_policy(0xffff0000, regs.data()), 0);
    regs[7] = 0;
    EXPECT_EQ(test_vl82c486_cache_policy(0x1000, regs.data()), 0);
    EXPECT_EQ(test_vl82c486_cache_policy(0xfc000, regs.data()), CPU_CACHE_WRITE_PROTECT);
}

TEST_F(CpuCacheTest, VlsiProgrammedRegionsAndOverlappingNoncacheablePrecedence)
{
    std::array<uint8_t, 256> regs { };
    regs[7] = 8;
    // 8 MB region at 8 MB, only its first 1 MB enabled, cacheable.
    regs[0x20] = 0xa1;
    regs[0x21] = 1;
    EXPECT_EQ(test_vl82c486_cache_policy(0x800000, regs.data()), CPU_CACHE_FILL);
    EXPECT_EQ(test_vl82c486_cache_policy(0x8fffff, regs.data()), CPU_CACHE_FILL);
    EXPECT_EQ(test_vl82c486_cache_policy(0x900000, regs.data()), 0);
    // 512 KB region at 1 MB, only its second 64 KB noncacheable.
    regs[0x20] = 0x42;
    regs[0x21] = 2;
    EXPECT_EQ(test_vl82c486_cache_policy(0x100000, regs.data()), CPU_CACHE_FILL);
    EXPECT_EQ(test_vl82c486_cache_policy(0x110000, regs.data()), 0);
    EXPECT_EQ(test_vl82c486_cache_policy(0x11ffff, regs.data()), 0);
    EXPECT_EQ(test_vl82c486_cache_policy(0x120000, regs.data()), CPU_CACHE_FILL);
    // 16 KB region at D0000, second 2 KB noncacheable, overlapping a
    // cacheable PMR and a cacheable segment. The deny wins in either PMR.
    for (int deny : { 0x20, 0x22 }) {
        regs[0x20] = regs[0x22] = 0xb4;
        regs[deny] |= 0x40;
        regs[0x21] = regs[0x23] = 2;
        EXPECT_EQ(test_vl82c486_cache_policy(0xd07ff, regs.data()), CPU_CACHE_FILL);
        EXPECT_EQ(test_vl82c486_cache_policy(0xd0800, regs.data()), 0);
        EXPECT_EQ(test_vl82c486_cache_policy(0xd0fff, regs.data()), 0);
        EXPECT_EQ(test_vl82c486_cache_policy(0xd1000, regs.data()), CPU_CACHE_FILL);
    }
    regs[0x20] = regs[0x22] = 0xb4;
    regs[0x16]              = 1; // segment-level deny also overrides a cacheable PMR
    EXPECT_EQ(test_vl82c486_cache_policy(0xd0800, regs.data()), 0);
}

TEST_F(CpuCacheTest, ReplacementUsesPseudoLruAndInvalidWaysFirst)
{
    for (unsigned i = 0; i < 4; ++i)
        readmemll(0x1000 + i * 0x800);
    readmemll(0x1000); // make way 0 most recent; next victim must be way 2
    readmemll(0x3000);
    cr0             = 0x40000000; // no new fills, measure which addresses are still resident
    unsigned before = bus_reads;
    readmemll(0x1000);
    readmemll(0x1800);
    readmemll(0x2800);
    readmemll(0x3000);
    EXPECT_EQ(bus_reads, before);
    readmemll(0x2000);
    EXPECT_EQ(bus_reads, before + 4);
    // Invalidating way 1 should cause its reuse even though LRU points elsewhere.
    cpu_cache_snoop(0x1800, 1);
    cr0 = 0;
    readmemll(0x3800);
    cpu_tr_write(5, (1 << 2) | 2);
    EXPECT_EQ(cpu_tr_read(4) & 0xfffffc00, 0x3c00u);
}
