/* Production D88 loader, motherboard ports, 765 and media decoder. */
#define main pc98_floppy_base_main
#include "pc98_floppy_test.c"
#undef main
#include <86box/fdd_d88.h>

static void
put16(uint8_t *p, unsigned v)
{
    p[0] = v;
    p[1] = v >> 8;
}
static void
put32(uint8_t *p, unsigned v)
{
    for (unsigned i = 0; i < 4; i++)
        p[i] = v >> (8 * i);
}

static void
make_fixture(const char *name)
{
    enum { TRACK_BYTES = 8 * (16 + 1024),
           SIZE        = 0x2b0 + 154 * TRACK_BYTES };
    uint8_t *image = calloc(1, SIZE);
    if (!image)
        fatal("Cannot allocate D88 fixture\n");
    memcpy(image, "READ ID fixture", 15);
    image[0x1b] = 0x20;
    put32(image + 0x1c, SIZE);
    for (unsigned track = 0; track < 154; track++) {
        unsigned pos = 0x2b0 + track * TRACK_BYTES;
        put32(image + 0x20 + track * 4, pos);
        for (unsigned r = 1; r <= 8; r++, pos += 1040) {
            image[pos]     = track / 2;
            image[pos + 1] = track % 2;
            image[pos + 2] = r;
            image[pos + 3] = 3;
            put16(image + pos + 4, 8);
            put16(image + pos + 14, 1024);
            memset(image + pos + 16, r, 1024);
        }
    }
    int   fd   = open(name, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0600);
    FILE *file = fd < 0 ? NULL : fdopen(fd, "wb");
    if (!file)
        fatal("Cannot create D88 fixture\n");
    CHECK(fwrite(image, 1, SIZE, file) == SIZE);
    CHECK(fclose(file) == 0);
    free(image);
}

static void
read_id(pc98_mb_t *b, unsigned c, unsigned h, int present)
{
    const uint8_t seek[] = { 0x0f, 0, (uint8_t) c };
    const uint8_t sis[]  = { 0x08 };
    uint8_t       res[7] = { 0 };
    command(b, 1, seek, sizeof(seek));
    command(b, 1, sis, sizeof(sis));
    CHECK(result(b, 1, res) == 2);
    const uint8_t id[] = { 0x4a, (uint8_t) (h << 2) };
    command(b, 1, id, sizeof(id));
    for (unsigned i = 0; i < 2000000 && (fdc_in(0x90, b) & 0xf0) != 0xd0; i++)
        if (!tick(b))
            break;
    CHECK(result(b, 1, res) == 7);
    if (present) {
        CHECK(!(res[0] & 0xc0) && !res[1] && !res[2]);
        CHECK(res[3] == c && res[4] == h);
        CHECK(res[5] >= 1 && res[5] <= 8 && res[6] == 3);
    } else {
        CHECK((res[0] & 0xc0) == 0x40 && (res[1] & 1));
    }
    printf("turbo%d READ ID C%u H%u: %02x %02x %02x %u/%u/%u/%u\n",
           fdd_get_turbo(&drives[0]), c, h, res[0], res[1], res[2],
           res[3], res[4], res[5], res[6]);
}

static void
exercise(const char *filename, int turbo, unsigned last, int blank)
{
    pc98_mb_t b;
    fdc_t     f;
    setup(&b, &f);
    drives[0].read_only = 1; /* Never modify the supplied image. */
    fdd_set_turbo(&drives[0], turbo);
    d86f_setup(&drives[0]);
    d88_load(&drives[0], (char *) filename);
    CHECK(drives[0].local != NULL && drives[0].writeprot);
    drives[0].empty = 0;
    fdc_out(0x94, 0, &b);
    for (unsigned i = 0; i < 1000000 && !fdd_is_ready(&drives[0]); i++)
        if (!tick(&b))
            break;
    CHECK(fdd_is_ready(&drives[0]));
    const uint8_t specify[] = { 0x03, 0xef, 0x02 };
    command(&b, 1, specify, sizeof(specify));
    read_id(&b, 0, 0, 1); /* No sector read has initialized the saved ID. */
    read_id(&b, 0, 1, 1);
    read_id(&b, last, 0, 1);
    /* Read R5 on side1, then READ ID on the other side. */
    const uint8_t read[] = { 0x46, 4, (uint8_t) last, 1, 5, 3, 5, 0x1b, 0xff };
    dma_target           = 2;
    dma_length           = 1024;
    dma_count            = 0;
    command(&b, 1, read, sizeof(read));
    for (unsigned i = 0; i < 200000 && (fdc_in(0x90, &b) & 0xf0) != 0xd0; i++)
        if (!tick(&b))
            break;
    uint8_t res[7];
    CHECK(result(&b, 1, res) == 7 && !res[1] && !res[2]);
    CHECK(dma_count == 1024);
    read_id(&b, last, 0, 1);
    read_id(&b, 0, 1, 1);
    if (blank)
        read_id(&b, last + 1, 0, 0);
    fdd_set_motor_enable(&drives[0], 0);
    d88_close(&drives[0]);
    d86f_destroy(&drives[0]);
    fifo_close(f.fifo_p);
}

int
main(int argc, char **argv)
{
    const char fixture[] = "pc98-d88-readid-generated.d88";
    make_fixture(fixture);
    for (unsigned turbo = 0; turbo < 2; turbo++)
        exercise(fixture, turbo, 76, 1);
    CHECK(remove(fixture) == 0);
    if (argc > 1)
        for (unsigned turbo = 0; turbo < 2; turbo++)
            exercise(argv[1], turbo, 76, 0);
    printf("PC-98 D88: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
