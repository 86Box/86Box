/* The native board, 765, image decoder and real 8237 run together. Only
 * physical host RAM, timers, interrupt observation and UI are adapted. */
#define PC98_REAL_DMA
#include "pc98_floppy_test.c"

int             is286 = 1, is_nec, is386;
static uint8_t  transfer_ram[2 * 1024 * 1024];
static unsigned memory_writes;
static unsigned dma_stall_clocks;
uint8_t
mem_readb_phys(uint32_t address)
{
    if (address >= sizeof(transfer_ram))
        fatal("DMA read outside test RAM");
    return transfer_ram[address];
}
void
mem_writeb_phys(uint32_t address, uint8_t value)
{
    if (address >= sizeof(transfer_ram))
        fatal("DMA write outside test RAM");
    transfer_ram[address] = value;
    memory_writes++;
}
void
mem_read_phys(void *dest, uint32_t address, int size)
{
    if (address + size > sizeof(transfer_ram))
        fatal("DMA read outside test RAM");
    memcpy(dest, transfer_ram + address, size);
}
void
mem_write_phys(void *source, uint32_t address, int size)
{
    if (address + size > sizeof(transfer_ram))
        fatal("DMA write outside test RAM");
    memcpy(transfer_ram + address, source, size);
    memory_writes += size;
}
bool
m808x_86box_active(void)
{
    return false;
}
bool
m808x_86box_dma_cancel_request_ex(void (*cb)(void *), void *p)
{
    (void) cb;
    (void) p;
    return false;
}
void
refreshread(void)
{
    fatal("Unexpected channel-zero refresh");
}
void
refreshread_vx0(void)
{
    fatal("Unexpected channel-zero refresh");
}
/* There is no translated code cache or concurrent CPU in this host harness. */
void
mem_invalidate_range(uint32_t start, uint32_t end)
{
    (void) start;
    (void) end;
}
void
sub_cycles(int clocks)
{
    cycles -= clocks;
}
void
cpu_bus_stall(int clocks)
{
    cycles -= clocks;
    tsc += clocks;
    dma_stall_clocks += clocks;
}
void
ibm5140_clock_wake(void)
{
    fatal("Unexpected IBM portable DMA path");
}

static void
program_dma(pc98_mb_t *board, unsigned channel, uint16_t address, uint8_t page)
{
    dma_pc98_write(0x15, 4 | channel, NULL);
    dma_pc98_write(0x19, 0, NULL);
    dma_pc98_write(channel * 4 + 1, address & 0xff, NULL);
    dma_pc98_write(channel * 4 + 1, address >> 8, NULL);
    dma_pc98_write(channel * 4 + 3, 0xff, NULL);
    dma_pc98_write(channel * 4 + 3, 3, NULL);
    dma_pc98_write(0x17, 0x44 | channel, NULL);
    dma_page_out(0x21 + 2 * ((channel + 3) & 3), page, board);
    dma_pc98_write(0x15, channel, NULL);
}

static void
read_with_dma(pc98_mb_t *board, uint16_t address, uint8_t page)
{
    fdc_out(0x94, 0, board);
    for (unsigned i = 0; i < 1000000 && !fdd_is_ready(&drives[0]); i++)
        if (!tick(board))
            break;
    CHECK(fdd_is_ready(&drives[0]));
    program_dma(board, 2, address, page);
    const uint32_t destination    = ((uint32_t) page << 16 & dma_mask) | address;
    unsigned       stalled_before = dma_stall_clocks;
    memset(transfer_ram, 0xcc, sizeof(transfer_ram));
    memory_writes           = 0;
    const uint8_t specify[] = { 0x03, 0xef, 0x02 };
    command(board, 1, specify, sizeof(specify));
    const uint8_t read[] = { 0x46, 0, 0, 0, 1, 3, 1, 0x1b, 0xff };
    command(board, 1, read, sizeof(read));
    for (unsigned i = 0; i < 200000 && (fdc_in(0x90, board) & 0xf0) != 0xd0; i++)
        if (!tick(board))
            break;
    uint8_t bytes[7] = { 0xff };
    CHECK(result(board, 1, bytes) == 7);
    CHECK((bytes[0] & 0xc0) == 0 && bytes[1] == 0 && bytes[2] == 0);
    CHECK(memory_writes == 1024);
    CHECK(dma_stall_clocks - stalled_before == 1024 * 10);
    CHECK(dma[2].cc == -1 && (dma_m & (1u << 2)));
    CHECK(!dma_get_drq(2) && !dma_get_drq(3));
    unsigned mismatches = 0;
    for (unsigned i = 0; i < 1024; i++)
        mismatches += transfer_ram[destination + i] != (uint8_t) (i ^ 0xa5);
    CHECK(mismatches == 0);
    CHECK(transfer_ram[destination - 1] == 0xcc && transfer_ram[destination + 1024] == 0xcc);
}

static void
reset_during_dma(pc98_mb_t *board)
{
    program_dma(board, 2, 0x4000, 0);
    memory_writes        = 0;
    const uint8_t read[] = { 0x46, 0, 0, 0, 1, 3, 1, 0x1b, 0xff };
    command(board, 1, read, sizeof(read));
    for (unsigned i = 0; i < 200000 && memory_writes < 100; i++)
        if (!tick(board))
            break;
    CHECK(memory_writes >= 100 && memory_writes < 1024);
    /* Assert reset through the other identity while channel 2 is active.
     * No old transfer may reach RAM after its route has changed. */
    fdc_out(0xcc, 0x80, board);
    CHECK(!dma_get_drq(2) && !dma_get_drq(3));
    const unsigned before = memory_writes;
    for (unsigned i = 0; i < 10000; i++)
        if (!tick(board))
            break;
    CHECK(memory_writes == before);
    fdc_out(0x94, 0, board);
    fire(&board->fdc->timer);
    CHECK((fdc_in(0x90, board) & 0xf0) == 0x80);
    read_with_dma(board, 0x8000, 0);
}

int
main(void)
{
    char  filename[] = "pc98-floppy-dma-generated.img";
    int   fd         = open(filename, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0600);
    FILE *file       = fd < 0 ? NULL : fdopen(fd, "wb");
    if (!file)
        fatal("Cannot create generated DMA test image");
    uint8_t sector[1024];
    for (unsigned i = 0; i < sizeof(sector); i++)
        sector[i] = (uint8_t) (i ^ 0xa5);
    for (unsigned i = 0; i < 77 * 2 * 8; i++)
        if (fwrite(sector, 1, sizeof(sector), file) != sizeof(sector))
            fatal("Image write failed");
    CHECK(fclose(file) == 0);
    pc98_mb_t board;
    fdc_t     controller;
    dma_init_pc98();
    setup(&board, &controller);
    fdd_set_check_bpb(&drives[0], 0);
    d86f_setup(&drives[0]);
    img_load(&drives[0], filename);
    drives[0].empty = 0;
    fdd_do_seek(&drives[0], 0);
    read_with_dma(&board, 0x4000, 0x70); /* BIOS page high nibble is masked. */
    reset_during_dma(&board);
    for (unsigned repeat = 0; repeat < 3; repeat++) {
        const uint8_t seek[] = { 0x0f, 0, 16 };
        command(&board, 1, seek, sizeof(seek));
        CHECK(drives[0].track == 16);
        pc98_mb_reset(&board);
        fdc_reset(&controller);
        dma_reset();
        fdd_reset();
        fire(&controller.timer);
        CHECK(dma_mask == 0x000fffff);
        /* Reset clears PCN, but does not teleport the physical head. */
        CHECK(controller.pcn[0] == 0 && drives[0].track == 16);
        const uint8_t recalibrate[] = { 0x07, 0 };
        command(&board, 1, recalibrate, sizeof(recalibrate));
        CHECK(drives[0].track == 0);
        read_with_dma(&board, 0x8000, 0x71);
    }
    fdd_set_motor_enable(&drives[0], 0);
    img_close(&drives[0]);
    d86f_destroy(&drives[0]);
    fifo_close(controller.fifo_p);
    CHECK(remove(filename) == 0);
    printf("PC-98 floppy/8237: %u checks, %u failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}

/* The upstream DMA block API reports the number of bytes transferred. */
uint32_t
mem_read_phys_block(void *dest, uint32_t address, uint32_t size)
{
    mem_read_phys(dest, address, (int) size);
    return size;
}

uint32_t
mem_write_phys_block(const void *source, uint32_t address, uint32_t size)
{
    mem_write_phys((void *) source, address, (int) size);
    return size;
}
