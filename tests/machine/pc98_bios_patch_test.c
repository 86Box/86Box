#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/pc98_bios_patch.h>

static unsigned checks, failures;
#define CHECK(expr)                                                    \
    do {                                                               \
        checks++;                                                      \
        if (!(expr)) {                                                 \
            failures++;                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
        }                                                              \
    } while (0)

static void
check_lanes(const uint8_t *itf)
{
    unsigned even = 0, odd = 0;
    for (size_t i = 0; i < 0x8000; i += 2) {
        even += itf[i];
        odd += itf[i + 1];
    }
    CHECK((even & 255u) == 0);
    CHECK((odd & 255u) == 0);
}

static int
read_rom(const char *directory, const char *name, uint8_t *bytes)
{
    char path[2048];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return 0;
    const int valid = fread(bytes, 1, 0x8000, fp) == 0x8000 && fgetc(fp) == EOF;
    fclose(fp);
    return valid;
}

int
main(int argc, char **argv)
{
    uint8_t original[0x8000], image[0x8000], snapshot[0x8000];
    memset(image, 0xa5, sizeof(image));
    memcpy(snapshot, image, sizeof(image));
    CHECK(pc98_bios_patch_vx_timer(NULL, 0x8000) == PC98_BIOS_PATCH_UNSUPPORTED);
    CHECK(pc98_bios_patch_vx_timer(image, 0x7fff) == PC98_BIOS_PATCH_UNSUPPORTED);
    CHECK(pc98_bios_patch_vx_timer(image, 0x8001) == PC98_BIOS_PATCH_UNSUPPORTED);
    CHECK(pc98_bios_patch_vx_timer(image, sizeof(image)) == PC98_BIOS_PATCH_UNSUPPORTED);
    CHECK(!memcmp(image, snapshot, sizeof(image)));
    /* Matching local patch signatures alone must never admit unknown ROMs. */
    image[0x900] = 0x75;
    image[0x901] = 0x0a;
    image[0x96c] = image[0x980] = image[0x9ac] = 0x74;
    image[0x96d] = image[0x981] = image[0x9ad] = 0x0a;
    static const uint8_t original_timeout[]    = {
        0xbe, 0xcc, 0x17, 0xbc, 0xdd, 0x09, 0xe9, 0x86, 0x0e, 0xf4
    };
    memcpy(image + 0x9d4, original_timeout, sizeof(original_timeout));
    image[0x7fec] = image[0x7fed] = 0;
    memcpy(snapshot, image, sizeof(image));
    CHECK(pc98_bios_patch_vx_timer(image, sizeof(image)) == PC98_BIOS_PATCH_UNSUPPORTED);
    CHECK(!memcmp(image, snapshot, sizeof(image)));

    if (argc < 2) {
        printf("%u synthetic checks, %u failures\n", checks, failures);
        return failures ? 1 : 0;
    }

    /* User-supplied physical ROMs are read-only and never checked into tests.
     * Synthetic rejection tests above also run when these files are absent. */
    uint8_t even[0x8000], odd[0x8000];
    if (!read_rom(argv[1], "nec_d27c256d-15_cpu_extboard_yll02.bin", even) || !read_rom(argv[1], "nec_d27c256d-15_cpu_extboard_yll04.bin", odd)) {
        printf("%u synthetic checks, %u failures; supplied VX ROM tests unavailable\n",
               checks, failures);
        return failures ? 1 : 77;
    }
    for (size_t i = 0; i < 0x4000; i++) {
        original[i * 2]     = even[0x4000 + i];
        original[i * 2 + 1] = odd[0x4000 + i];
    }
    check_lanes(original);
    memcpy(image, original, sizeof(image));
    CHECK(pc98_bios_patch_vx_timer(image, sizeof(image)) == PC98_BIOS_PATCH_APPLIED);
    check_lanes(image);
    CHECK(image[0x900] == 0xeb && image[0x901] == 0x0a);
    CHECK(image[0x96c] == 0xeb && image[0x96d] == 0x0a);
    CHECK(image[0x980] == 0xeb && image[0x981] == 0x0a);
    CHECK(image[0x9ac] == 0xeb && image[0x9ad] == 0x0a);
    static const uint8_t timeout[] = {
        0xb0, 0xff, 0xe6, 0x02, 0xeb, 0x04, 0x90, 0x90, 0x90, 0x90
    };
    CHECK(!memcmp(image + 0x9d4, timeout, sizeof(timeout)));
    CHECK(image[0x7fec] == 0x2d && image[0x7fed] == 0xe6);
    unsigned unexpected = 0;
    for (size_t i = 0; i < sizeof(image); i++) {
        if (i == 0x900 || i == 0x96c || i == 0x980 || i == 0x9ac || (i >= 0x9d4 && i < 0x9de) || i == 0x7fec || i == 0x7fed)
            continue;
        unexpected += image[i] != original[i];
    }
    CHECK(unexpected == 0);
    /* Preserve the real bounded IRQ loop, PIT setup/teardown, reset vector,
     * and unrelated fatal diagnostics. */
    CHECK(!memcmp(image + 0x9d0, original + 0x9d0, 4));
    CHECK(!memcmp(image + 0x9de, original + 0x9de, 10));
    CHECK(!memcmp(image + 0x8d4, original + 0x8d4, 0x2c));
    CHECK(!memcmp(image + 0x7ff0, original + 0x7ff0, 16));
    memcpy(snapshot, image, sizeof(image));
    CHECK(pc98_bios_patch_vx_timer(image, sizeof(image)) == PC98_BIOS_PATCH_ALREADY_APPLIED);
    CHECK(!memcmp(image, snapshot, sizeof(image)));
    image[0x100] ^= 1;
    memcpy(snapshot, image, sizeof(image));
    CHECK(pc98_bios_patch_vx_timer(image, sizeof(image)) == PC98_BIOS_PATCH_UNSUPPORTED);
    CHECK(!memcmp(image, snapshot, sizeof(image)));
    memcpy(image, original, sizeof(image));
    image[0x7ff5] ^= 1;
    memcpy(snapshot, image, sizeof(image));
    CHECK(pc98_bios_patch_vx_timer(image, sizeof(image)) == PC98_BIOS_PATCH_UNSUPPORTED);
    CHECK(!memcmp(image, snapshot, sizeof(image)));
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
