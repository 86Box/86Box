/* Exercise the production container/import paths with synthetic DOS media.
 * Optional SOURCE OUTPUT arguments import a real local image for inspection;
 * neither the original nor an existing output is ever modified. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <io.h>
#    include <fcntl.h>
#    include <sys/stat.h>
#endif
#include <86box/pc98_hdi.h>
#include <86box/plat.h>

FILE *
plat_fopen(const char *path, const char *mode)
{
#ifdef _WIN32
    if (!strcmp(mode, "w+bx")) {
        const int fd = _open(path, _O_RDWR | _O_BINARY | _O_CREAT | _O_EXCL, _S_IREAD | _S_IWRITE);
        if (fd < 0)
            return NULL;
        FILE *stream = _fdopen(fd, "w+b");
        if (!stream)
            _close(fd);
        return stream;
    }
#endif
    return fopen(path, mode);
}
void
plat_remove(char *path)
{
    remove(path);
}
static unsigned checks;
#define CHECK(c)                                                   \
    do {                                                           \
        checks++;                                                  \
        if (!(c)) {                                                \
            fprintf(stderr, "Failed line %d: %s\n", __LINE__, #c); \
            exit(1);                                               \
        }                                                          \
    } while (0)
static void
wr16(uint8_t *p, uint16_t n)
{
    p[0] = n;
    p[1] = n >> 8;
}
static void
wr32(uint8_t *p, uint32_t n)
{
    wr16(p, n);
    wr16(p + 2, n >> 16);
}

static void
fixture(const char *path, unsigned physical, unsigned sectors, unsigned heads,
        int modern, unsigned partitions)
{
    const unsigned cylinders = partitions > 1 ? 1024 : 512;
    const uint32_t bytes     = cylinders * heads * sectors * physical;
    uint8_t       *disk      = calloc(1, (size_t) 4096 + bytes);
    CHECK(disk != NULL);
    wr32(disk + 8, 4096);
    wr32(disk + 12, bytes);
    wr32(disk + 16, physical);
    wr32(disk + 20, sectors);
    wr32(disk + 24, heads);
    wr32(disk + 28, cylinders);
    uint8_t *payload = disk + 4096;
    memcpy(payload + 4, "IPL1", 4);
    /* Marker in the reserved IPL program area must survive relocation. */
    memset(payload + 1024, 0xa5, 7168);
    memset(payload + physical, 0, physical);
    const uint32_t stride  = heads * sectors * physical;
    const uint32_t spacing = ((1048576u + stride - 1u) / stride) * stride;
    for (unsigned p = 0; p < partitions; p++) {
        const uint32_t start = stride + p * spacing;
        uint8_t       *entry = payload + physical + p * 32u;
        entry[0]             = 0x21;
        entry[1]             = 1;
        if (p == partitions - 1) {
            entry[0] |= 0x80;
            entry[1] |= 0x80;
        }
        wr16(entry + 6, start / stride);
        wr16(entry + 10, start / stride);
        wr16(entry + 14, (start + 1048575u) / stride);
        memcpy(entry + 16, "SYNTHETIC DOS   ", 16);
        uint8_t *boot = payload + start;
        boot[0]       = 0xeb;
        boot[1]       = 0x3e;
        wr16(boot + 11, 4096);
        boot[13] = 1;
        wr16(boot + 14, 1);
        boot[16] = 2;
        wr16(boot + 17, 128);
        wr16(boot + 19, 256);
        boot[21] = 0xf8;
        wr16(boot + 22, 1);
        if (modern) {
            wr16(boot + 24, sectors);
            wr16(boot + 26, heads);
            wr32(boot + 28, start / physical);
        }
        const unsigned metadata = modern ? 64 : 24;
        wr32(boot + metadata, start / physical);
        wr16(boot + metadata + 4, 16384u / physical);
        wr16(boot + metadata + 6, physical);
        uint8_t *fat = boot + 4096;
        memset(fat, 0xff, 8);
        memcpy(fat + 4096, fat, 4096);
        uint8_t    *root    = boot + 12288;
        const char *names[] = { "IO      SYS", "MSDOS   SYS", "COMMAND COM" };
        for (unsigned i = 0; i < 3; i++) {
            memcpy(root + i * 32, names[i], 11);
            root[i * 32 + 11] = 0x20;
            wr16(root + i * 32 + 26, i + 2);
            wr32(root + i * 32 + 28, 4096);
            memset(boot + 16384 + i * 4096, 0x31 + p + i, 4096);
        }
    }
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    CHECK(fwrite(disk, 1, (size_t) 4096 + bytes, f) == (size_t) 4096 + bytes);
    CHECK(fclose(f) == 0);
    free(disk);
}

static void
compare_volume(pc98_hdi_t *src, pc98_hdi_t *dst, unsigned index, int modern)
{
    uint8_t source_entry[32], target_entry[32];
    CHECK(fseeko64(src->f, src->base + src->sector_size + index * 32, SEEK_SET) == 0);
    CHECK(fread(source_entry, 1, 32, src->f) == 32);
    CHECK(fseeko64(dst->f, dst->base + 256 + index * 32, SEEK_SET) == 0);
    CHECK(fread(target_entry, 1, 32, dst->f) == 32);
    const uint64_t source_at = (uint64_t) (source_entry[10] | (source_entry[11] << 8)) * src->hpc * src->spt * src->sector_size;
    const uint64_t target_at = (uint64_t) (target_entry[10] | (target_entry[11] << 8)) * dst->hpc * dst->spt * dst->sector_size;
    uint8_t        a[4096], b[4096];
    for (unsigned block = 0; block < 256; block++) {
        CHECK(fseeko64(src->f, src->base + source_at + block * 4096u, SEEK_SET) == 0);
        CHECK(fseeko64(dst->f, dst->base + target_at + block * 4096u, SEEK_SET) == 0);
        CHECK(fread(a, 1, sizeof(a), src->f) == sizeof(a));
        CHECK(fread(b, 1, sizeof(b), dst->f) == sizeof(b));
        if (!block) {
            const unsigned metadata = modern ? 64 : 24;
            memcpy(a + metadata, b + metadata, 8);
            if (modern)
                memcpy(a + 24, b + 24, 8);
        }
        CHECK(!memcmp(a, b, sizeof(a)));
    }
    CHECK(!memcmp(source_entry, target_entry, 4));
    CHECK(!memcmp(source_entry + 16, target_entry + 16, 16));
}

static void
regressions(void)
{
    const char *source = "pc98-source.hdi", *output = "pc98-import.hdi";
    char        error[256];
    /* Test old and extended NEC BPBs, 256..4096 physical sectors, unusual
     * track/head counts, and multiple partitions without rebuilding FATs. */
    for (unsigned physical = 256; physical <= 4096; physical *= 2) {
        for (int modern = 0; modern < 2; modern++) {
            const unsigned parts = modern ? 2 : 1;
            fixture(source, physical, 17, 4, modern, parts);
            pc98_hdi_t src, dst;
            CHECK(pc98_hdi_open(&src, source) == 1);
            CHECK(pc98_hdi_import_sasi(&src, output, error, sizeof(error)) == 1);
            CHECK(pc98_hdi_open(&dst, output) == 1);
            CHECK(dst.sector_size == 256 && dst.spt == 33 && dst.hpc == 4 && dst.cylinders == 153);
            for (unsigned p = 0; p < parts; p++)
                compare_volume(&src, &dst, p, modern);
            uint8_t code[256], original_code[256];
            CHECK(pc98_hdi_read_256(&dst, 4, 1, code));
            CHECK(pc98_hdi_read_256(&src, 4, 1, original_code));
            CHECK(!memcmp(code, original_code, sizeof(code)));
            CHECK(pc98_hdi_import_sasi(&src, output, error, sizeof(error)) < 0);
            CHECK(!pc98_hdi_read_256(&dst, dst.blocks, 1, code));
            CHECK(!pc98_hdi_write_256(&dst, 0, 0, code));
            pc98_hdi_close(&src);
            pc98_hdi_close(&dst);
            remove(source);
            remove(output);
        }
    }
    fixture(source, 512, 17, 4, 0, 1);
    pc98_hdi_t imported;
    CHECK(pc98_hdi_open_sasi(&imported, source, error, sizeof(error)) == 1);
    CHECK(imported.sasi_imported && imported.sector_size == 256);
    uint8_t data[256], back[256];
    memset(data, 0x7c, sizeof(data));
    CHECK(pc98_hdi_write_256(&imported, 5000, 1, data));
    pc98_hdi_close(&imported);
    CHECK(pc98_hdi_open_sasi(&imported, source, error, sizeof(error)) == 1);
    CHECK(pc98_hdi_read_256(&imported, 5000, 1, back) && !memcmp(data, back, sizeof(data)));
    pc98_hdi_close(&imported);
    FILE *f = fopen(source, "r+b");
    CHECK(f != NULL);
    CHECK(fseek(f, 4096 + 9000, SEEK_SET) == 0 && fputc(0x42, f) != EOF);
    CHECK(fclose(f) == 0);
    CHECK(pc98_hdi_open_sasi(&imported, source, error, sizeof(error)) < 0);
    CHECK(strstr(error, "different source") != NULL);
    CHECK(pc98_hdi_open(&imported, "pc98-source.hdi.sasi.hdi") == 1);
    CHECK(pc98_hdi_read_256(&imported, 5000, 1, back) && !memcmp(data, back, sizeof(data)));
    pc98_hdi_close(&imported);
    remove(source);
    remove("pc98-source.hdi.sasi.hdi");

    /* Invalid BPB and unknown guest loader must not create an output. */
    fixture(source, 512, 17, 4, 0, 1);
    f = fopen(source, "r+b");
    CHECK(f != NULL);
    CHECK(fseek(f, 4096 + 17 * 4 * 512 + 13, SEEK_SET) == 0 && fputc(3, f) != EOF);
    CHECK(fclose(f) == 0);
    CHECK(pc98_hdi_open(&imported, source) == 1);
    CHECK(pc98_hdi_import_sasi(&imported, output, error, sizeof(error)) < 0);
    pc98_hdi_close(&imported);
    CHECK((f = fopen(output, "rb")) == NULL);
    remove(source);
    /* Unusual CHS and an NHD signature under an .hdi filename. */
    fixture(source, 512, 18, 2, 1, 1);
    f = fopen(source, "r+b");
    CHECK(f != NULL);
    uint8_t nhd[0x120] = { 0 };
    memcpy(nhd, "T98HDDIMAGE.R0", sizeof("T98HDDIMAGE.R0") - 1);
    wr32(nhd + 0x110, 4096);
    wr32(nhd + 0x114, 512);
    wr16(nhd + 0x118, 2);
    wr16(nhd + 0x11a, 18);
    wr16(nhd + 0x11c, 512);
    CHECK(fwrite(nhd, 1, sizeof(nhd), f) == sizeof(nhd));
    CHECK(fclose(f) == 0);
    CHECK(pc98_hdi_open(&imported, source) == 1 && imported.format == PC98_HDI_T98NEXT);
    CHECK(pc98_hdi_import_sasi(&imported, output, error, sizeof(error)) == 1);
    pc98_hdi_t destination;
    CHECK(pc98_hdi_open(&destination, output) == 1);
    compare_volume(&imported, &destination, 0, 1);
    pc98_hdi_close(&destination);
    pc98_hdi_close(&imported);
    remove(source);
    remove(output);

    fixture(source, 512, 17, 4, 0, 1);
    f = fopen(source, "r+b");
    CHECK(f != NULL);
    CHECK(fseek(f, 4096 + 17 * 4 * 512 + 24, SEEK_SET) == 0 && fputc(69, f) != EOF);
    CHECK(fclose(f) == 0);
    CHECK(pc98_hdi_open(&imported, source) == 1);
    CHECK(pc98_hdi_import_sasi(&imported, output, error, sizeof(error)) < 0);
    CHECK(strstr(error, "Unrecognized DOS boot") != NULL);
    pc98_hdi_close(&imported);
    remove(source);
    CHECK((f = fopen(output, "rb")) == NULL);

    pc98_hdi_t geometry  = { 0 };
    geometry.format      = PC98_HDI_ANEX86;
    geometry.base        = 4096;
    geometry.sector_size = 384;
    geometry.spt         = 17;
    geometry.hpc         = 4;
    geometry.cylinders   = 153;
    CHECK(!pc98_hdi_header_is_sane(&geometry, 4096 + 153 * 4 * 17 * 512));
    geometry.sector_size = 512;
    CHECK(!pc98_hdi_header_is_sane(&geometry, 4096 + 153 * 4 * 17 * 512 - 1));
}

int
main(int argc, char **argv)
{
    if (argc == 2) {
        pc98_hdi_t disk;
        char       error[256];
        if (pc98_hdi_open_sasi(&disk, argv[1], error, sizeof(error)) != 1) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        printf("SASI attach: imported=%d, %u/%u/%u, %u-byte sectors\n",
               disk.sasi_imported, disk.cylinders, disk.hpc, disk.spt, disk.sector_size);
        pc98_hdi_close(&disk);
        return 0;
    }
    if (argc == 3) {
        pc98_hdi_t source;
        char       error[256];
        if (pc98_hdi_open(&source, argv[1]) != 1) {
            fprintf(stderr, "Invalid source container\n");
            return 1;
        }
        const int result = pc98_hdi_import_sasi(&source, argv[2], error, sizeof(error));
        pc98_hdi_close(&source);
        if (result < 0) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        printf("Created %s; original unchanged.\n", argv[2]);
        return 0;
    }
    regressions();
    printf("%u PC-98 disk/import checks passed.\n", checks);
    return 0;
}
