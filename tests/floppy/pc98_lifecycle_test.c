/* Reuse the production-core host adapter; run only lifecycle regressions. */
#define main pc98_floppy_base_main
#include "pc98_floppy_test.c"
#undef main

void ui_sb_update_icon_state(int a, int b) { (void)a; (void)b; }
void fdd_tape_eject(void) {}

static const char lifecycle_image[] = "pc98-lifecycle-generated.img";

static void lifecycle_insert(pc98_mb_t *b)
{
    fdd_set_check_bpb(&drives[0], 0);
    d86f_setup(&drives[0]);
    img_load(&drives[0], (char *)lifecycle_image);
    drives[0].image_close = img_close;
    drives[0].empty = 0;
    fdd_do_seek(&drives[0], 0);
    fdc_out(0x94, 0, b);
    for (unsigned i = 0; i < 1000000 && !fdd_is_ready(&drives[0]); i++)
        if (!tick(b)) break;
    CHECK(fdd_is_ready(&drives[0]));
    const uint8_t specify[] = {0x03, 0xef, 0x02};
    command(b, 1, specify, sizeof(specify));
}

static void lifecycle_begin(pc98_mb_t *b, uint8_t op)
{
    const uint8_t rw[] = {op, 0, 0, 0, 1, 3, 1, 0x1b, 0xff};
    const uint8_t format[] = {0x4d, 0, 3, 8, 0x54, 0xe5};
    dma_target = 2;
    dma_length = 1024;
    dma_count = 0;
    command(b, 1, op == 0x4d ? format : rw, op == 0x4d ? sizeof(format) : sizeof(rw));
    /* Let the actual deferred command dispatch run, before decoder completion. */
    for (unsigned i = 0; i < 1000 && timer_is_enabled(&b->fdc->timer); i++)
        if (!tick(b)) break;
    CHECK((b->fdc->stat & 0xf0) != 0xd0);
}

static void test_eject_execution(void)
{
    const uint8_t ops[] = {0x46, 0x45, 0x4d};
    for (unsigned op = 0; op < sizeof(ops); op++) {
        pc98_mb_t b;
        fdc_t *f = calloc(1, sizeof(*f));
        setup(&b, f);
        lifecycle_insert(&b);
        lifecycle_begin(&b, ops[op]);
        fdd_close(&drives[0]);
        for (unsigned i = 0; i < 10000 && (f->stat & 0xf0) != 0xd0; i++)
            if (!tick(&b)) break;
        uint8_t res[7] = {0};
        CHECK(result(&b, 1, res) == 7);
        CHECK((res[0] & 0xc8) == 0xc8); /* READY changed during execution. */
        CHECK(!drq[2]);
        CHECK(drives[0].local == NULL && drives[0].empty);
        /* A fresh insertion and command must still work after the error. */
        lifecycle_insert(&b);
        read_sector(&b);
        fdd_close(&drives[0]);
        fdc_close(f);
    }
}

static void test_close_execution(void)
{
    pc98_mb_t b;
    fdc_t *f = calloc(1, sizeof(*f));
    setup(&b, f);
    lifecycle_insert(&b);
    lifecycle_begin(&b, 0x46);
    CHECK(drq[2]);
    asserted_irqs |= 1 << 11;
    fdc_close(f);
    CHECK(!drq[2]);
    CHECK(!(asserted_irqs & (1 << 11)));
    CHECK(drives[0].fdc == NULL);
    CHECK(!timer_is_enabled(&drives[0].seek_timer));
    CHECK(!timer_is_enabled(&drives[0].poll_time));
    fdd_close(&drives[0]);
}

static void test_eject_seek(void)
{
    pc98_mb_t b;
    fdc_t *f = calloc(1, sizeof(*f));
    setup(&b, f);
    lifecycle_insert(&b);
    f->fintr = 0;
    asserted_irqs = 0;
    /* Use the 765 itself: the native wrapper has its own SEEK notification. */
    const uint8_t seek[] = {0x0f, 0, 10};
    for (unsigned i = 0; i < sizeof(seek); i++) fdc_write(5, seek[i], f);
    CHECK(drives[0].seek_in_progress);
    fdd_close(&drives[0]);
    for (unsigned i = 0; i < 10000 && !(asserted_irqs & (1 << 11)); i++)
        if (!tick(&b)) break;
    CHECK(asserted_irqs & (1 << 11));
    fdc_write(5, 0x08, f);
    CHECK((fdc_read(5, f) & 0xe0) == 0x20);
    CHECK(fdc_read(5, f) == 10);
    CHECK(drives[0].track == 10);
    fdc_close(f);
}

static void test_mid_execution_reset(void)
{
    const uint8_t ops[] = {0x46, 0x45, 0x4d};
    for (unsigned op = 0; op < sizeof(ops); op++) {
        pc98_mb_t b;
        fdc_t *f = calloc(1, sizeof(*f));
        setup(&b, f);
        lifecycle_insert(&b);
        lifecycle_begin(&b, ops[op]);
        fdc_out(0x94, 0x80, &b);
        CHECK(f->stat == 0 && !drq[2] && !drq[3]);
        CHECK(!timer_is_enabled(&f->timer));
        CHECK(!drives[0].pending.pending && !drives[0].seek_in_progress);
        for (unsigned i = 0; i < 100; i++) tick(&b);
        CHECK(f->stat == 0 && !drq[2]);
        fdc_out(0x94, 0, &b);
        fire(&f->timer);
        read_sector(&b);
        fdd_close(&drives[0]);
        fdc_close(f);
    }
}

static void test_eject_boundaries(void)
{
    pc98_mb_t b;
    fdc_t *f = calloc(1, sizeof(*f));
    setup(&b, f);
    lifecycle_insert(&b);
    const uint8_t partial[] = {0x46, 0};
    command(&b, 1, partial, sizeof(partial));
    const uint8_t before = f->stat;
    fdd_close(&drives[0]);
    CHECK(f->stat == before && f->pnum == 1 && f->paramstogo == 0);
    fdc_out(0x94, 0x80, &b);
    fdc_out(0x94, 0, &b);
    fire(&f->timer);
    lifecycle_insert(&b);
    const uint8_t sense[] = {0x04, 0};
    command(&b, 1, sense, sizeof(sense));
    fire(&f->timer);
    const uint8_t status = f->res[10];
    fdd_close(&drives[0]);
    uint8_t res[7] = {0};
    CHECK(result(&b, 1, res) == 1 && res[0] == status);
    lifecycle_insert(&b);
    const uint8_t specify_pio[] = {0x03, 0xef, 0x03};
    command(&b, 1, specify_pio, sizeof(specify_pio));
    lifecycle_begin(&b, 0x46);
    CHECK(!f->dma && !drq[2]);
    for (unsigned i = 0; i < 200000 && (f->stat & 0xf0) != 0xf0; i++)
        if (!tick(&b)) break;
    CHECK((f->stat & 0xf0) == 0xf0 && f->data_ready);
    fdd_close(&drives[0]);
    CHECK(result(&b, 1, res) == 7 && (res[0] & 0xc8) == 0xc8);
    CHECK(!f->data_ready && fifo_get_empty(f->fifo_p));
    fdc_close(f);
}

static void test_not_ready_commands(void)
{
    const uint8_t ops[] = {0x46, 0x45, 0x4d, 0x4a};
    for (unsigned unit = 0; unit < 2; unit++) {
        for (unsigned op = 0; op < sizeof(ops); op++) {
            pc98_mb_t b;
            fdc_t *f = calloc(1, sizeof(*f));
            setup(&b, f); /* Unit 0 installed empty; unit 1 absent. */
            const uint8_t rw[] = {ops[op], unit, 0, 0, 1, 3, 1, 0x1b, 0xff};
            const uint8_t format[] = {0x4d, unit, 3, 8, 0x54, 0xe5};
            const uint8_t id[] = {0x4a, unit};
            command(&b, 1, ops[op] == 0x4d ? format : ops[op] == 0x4a ? id : rw,
                    ops[op] == 0x4d ? sizeof(format) : ops[op] == 0x4a ? sizeof(id) : sizeof(rw));
            for (unsigned i = 0; i < 1000 && (f->stat & 0xf0) != 0xd0; i++)
                if (!tick(&b)) break;
            uint8_t res[7] = {0};
            CHECK(result(&b, 1, res) == 7);
            CHECK(res[0] == (0x48 | unit) && !res[1] && !res[2]);
            CHECK(!drq[2] && !drq[3]);
            CHECK(!unit || !drives[unit].motoron);
            fdc_close(f);
        }
    }
}

static void test_deferred_read_eject(void)
{
    pc98_mb_t b;
    fdc_t *f = calloc(1, sizeof(*f));
    setup(&b, f);
    lifecycle_insert(&b);
    f->config |= 0x40; /* Enable the actual implied-seek command path. */
    const uint8_t read[] = {0x46, 0, 2, 0, 1, 3, 1, 0x1b, 0xff};
    command(&b, 1, read, sizeof(read));
    CHECK(drives[0].pending.pending && drives[0].seek_in_progress);
    fdd_close(&drives[0]);
    CHECK(!timer_is_enabled(&drives[0].seek_timer));
    CHECK(!drives[0].pending.pending && !drives[0].seek_in_progress);
    uint8_t res[7] = {0};
    CHECK(result(&b, 1, res) == 7 && res[0] == 0xc8);
    CHECK(res[3] == 2 && res[4] == 0 && res[5] == 1 && res[6] == 3);
    for (unsigned i = 0; i < 1000; i++) tick(&b);
    CHECK(f->stat == 0x80 && !drq[2]);
    fdc_close(f);
}

static void test_seek_reset(void)
{
    const uint8_t ops[] = {0x0f, 0x07};
    for (unsigned op = 0; op < sizeof(ops); op++) {
        pc98_mb_t b;
        fdc_t *f = calloc(1, sizeof(*f));
        setup(&b, f);
        lifecycle_insert(&b);
        drives[0].track = op ? 10 : 0;
        fdd_do_seek(&drives[0], drives[0].track);
        f->pcn[0] = op ? 10 : 0;
        const uint8_t seek[] = {ops[op], 0, 10};
        for (unsigned i = 0; i < (op ? 2 : 3); i++) fdc_write(5, seek[i], f);
        CHECK(drives[0].seek_in_progress);
        fdc_out(0x94, 0x80, &b);
        CHECK(!timer_is_enabled(&drives[0].seek_timer));
        CHECK(!drives[0].seek_in_progress && !drives[0].pending.pending);
        CHECK(f->stat == 0);
        fdc_out(0x94, 0, &b);
        fire(&f->timer);
        /* Reset cancels electronics, not the mechanism's head position. */
        drives[0].track = 0;
        fdd_do_seek(&drives[0], 0);
        read_sector(&b);
        fdd_close(&drives[0]);
        fdc_close(f);
    }
}

int main(void)
{
    int fd = open(lifecycle_image, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0600);
    FILE *file = fd < 0 ? NULL : fdopen(fd, "wb");
    if (!file) fatal("Cannot create lifecycle test image\n");
    uint8_t sector[1024];
    for (unsigned i = 0; i < sizeof(sector); i++) sector[i] = (uint8_t)(i ^ 0xa5);
    for (unsigned i = 0; i < 77 * 2 * 8; i++)
        if (fwrite(sector, 1, sizeof(sector), file) != sizeof(sector)) fatal("Image write failed\n");
    CHECK(fclose(file) == 0);
    test_eject_execution();
    test_close_execution();
    test_eject_seek();
    test_mid_execution_reset();
    test_eject_boundaries();
    test_not_ready_commands();
    test_deferred_read_eject();
    test_seek_reset();
    CHECK(remove(lifecycle_image) == 0);
    printf("PC-98 lifecycle: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
