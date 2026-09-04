#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

extern "C" {
#define fallthrough [[fallthrough]]
#define calloc(count, size) (mcd_t *) calloc(count, size)
#include "../src/cdrom/cdrom_mitsumi.c"
#undef calloc
#undef fallthrough
}

namespace {

struct MockState {
    int dma_result{};
    int read_length{COOKED_SECTOR_SIZE};
    uint64_t dma_bytes{};
} mock;

cdrom_ops_t ops{};

uint8_t get_track_type(const void *, uint32_t)
{
    return CD_TRACK_UNK_DATA;
}

void prepare_device(mcd_t &dev, cdrom_t &cd)
{
    std::memset(&dev, 0, sizeof(dev));
    std::memset(&cd, 0, sizeof(cd));
    std::memset(dma, 0, sizeof(dma));
    ops = {};
    ops.get_track_type = get_track_type;
    cd.ops = &ops;
    cd.cd_status = CD_STATUS_DATA_ONLY;
    cd.cdrom_capacity = 450000;
    dev.cdrom_dev = &cd;
    dev.irq = 10;
    dev.dma = 5;
    mitsumi_cdrom_reset(&dev);
}

void BM_PioSectorTransfer(benchmark::State &state)
{
    mcd_t dev{};
    cdrom_t cd{};
    prepare_device(dev, cd);
    const int sector_size = static_cast<int>(state.range(0));

    for (auto _ : state) {
        dev.data = 1;
        dev.enable_dma = 0;
        dev.early_status = 0;
        dev.buf_idx = 0;
        dev.buf_count = sector_size;
        dev.readcount = 0;
        for (int i = 0; i < sector_size; ++i)
            benchmark::DoNotOptimize(mitsumi_cdrom_in(0, &dev));
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(state.iterations() * sector_size);
}
BENCHMARK(BM_PioSectorTransfer)->Arg(COOKED_SECTOR_SIZE)->Arg(RAW_SECTOR_SIZE);

void BM_DmaSectorTransfer(benchmark::State &state)
{
    mcd_t dev{};
    cdrom_t cd{};
    prepare_device(dev, cd);
    const int sector_size = static_cast<int>(state.range(0));
    mock.dma_result = 0;

    for (auto _ : state) {
        dev.enable_dma = 1;
        dev.drvmode = DRV_MODE_READ;
        dev.buf_idx = 0;
        dev.buf_count = sector_size;
        dma[dev.dma].cc = (sector_size / 2) - 1;
        while (dev.buf_count)
            benchmark::DoNotOptimize(mitsumi_dma_transfer(&dev));
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(state.iterations() * sector_size);
}
BENCHMARK(BM_DmaSectorTransfer)->Arg(COOKED_SECTOR_SIZE)->Arg(RAW_SECTOR_SIZE);

void BM_ReadSectorPipeline(benchmark::State &state)
{
    mcd_t dev{};
    cdrom_t cd{};
    prepare_device(dev, cd);
    const uint32_t sectors = static_cast<uint32_t>(state.range(0));
    mock.read_length = COOKED_SECTOR_SIZE;

    for (auto _ : state) {
        dev.mode = 0;
        dev.smode = 1;
        dev.dmalen = COOKED_SECTOR_SIZE + MITSUMI_DMA_COUNT_BIAS;
        dev.readmsf = 0x000200;
        dev.readcount = sectors;
        for (uint32_t i = 0; i < sectors; ++i) {
            benchmark::DoNotOptimize(mitsumi_cdrom_read_sector(&dev, i == 0));
            dev.buf_count = 0;
        }
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(state.iterations() * sectors * COOKED_SECTOR_SIZE);
    state.counters["sectors"] = benchmark::Counter(
        static_cast<double>(state.iterations() * sectors), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_ReadSectorPipeline)->Arg(1)->Arg(16)->Arg(128);

void BM_CommandRoundTrip(benchmark::State &state)
{
    mcd_t dev{};
    cdrom_t cd{};
    prepare_device(dev, cd);

    for (auto _ : state) {
        mitsumi_cdrom_out(0, CMD_GET_VER, &dev);
        benchmark::DoNotOptimize(mitsumi_cdrom_in(0, &dev));
        benchmark::DoNotOptimize(mitsumi_cdrom_in(0, &dev));
        benchmark::DoNotOptimize(mitsumi_cdrom_in(0, &dev));
    }
    state.counters["commands"] = benchmark::Counter(
        static_cast<double>(state.iterations()), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_CommandRoundTrip);

} // namespace

extern "C" {

dma_t dma[8];
uint8_t dma_e;
uint8_t dma_m;
uint64_t TIMER_USEC = 1ULL << 32;
uint64_t tsc;
uint64_t timer_target;
volatile int cpu_thread_run = 1;
volatile int is_quit;
int hard_reset_pending;

void picint_common(uint16_t, int, int, uint8_t *) {}
void dma_set_drq(int, int) {}
void dma_set_service_handler(int, void (*)(void *), void *) {}
int dma_channel_write(int, uint16_t)
{
    mock.dma_bytes += 2;
    return mock.dma_result;
}
int dma_channel_writable(int) { return 1; }
void timer_enable(pc_timer_t *timer) { timer->flags |= TIMER_ENABLED; }
void timer_disable(pc_timer_t *timer) { timer->flags &= ~TIMER_ENABLED; }
void timer_add(pc_timer_t *, void (*)(void *), void *, int) {}
void cdrom_stop(cdrom_t *) {}
int cdrom_read_toc(const cdrom_t *, uint8_t *buffer, int, uint8_t, int, int)
{
    std::memset(buffer, 0, 4);
    return 0;
}
void cdrom_get_track_buffer(cdrom_t *, uint8_t *buffer) { std::memset(buffer, 0, 34); }
int cdrom_get_q(cdrom_t *, uint8_t *buffer, int current, uint8_t)
{
    std::memset(buffer, 0, 10);
    return current;
}
void cdrom_seek(cdrom_t *dev, uint32_t pos, uint8_t) { dev->seek_pos = pos; }
int cdrom_readsector_raw(cdrom_t *, uint8_t *buffer, int sector, int, int, int,
                         int *length, uint8_t)
{
    std::memset(buffer, sector & 0xff, mock.read_length);
    *length = mock.read_length;
    return 1;
}
uint8_t cdrom_audio_play(cdrom_t *, uint32_t, uint32_t, int) { return 1; }
int cdrom_lba_to_msf_accurate(int lba)
{
    const uint32_t absolute = static_cast<uint32_t>(lba + 150);
    return static_cast<int>(((absolute / 4500) << 16) |
                            (((absolute / 75) % 60) << 8) | (absolute % 75));
}
void cdrom_reload(uint8_t) {}
void cdrom_eject(uint8_t) {}

}
