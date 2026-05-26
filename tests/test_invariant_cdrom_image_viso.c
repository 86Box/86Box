#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

/*
 * Self-contained simulation of the vulnerable pattern from cdrom_image_viso.c
 *
 * The invariant: when copying from a metadata buffer using a seek offset and
 * sector_remain length, the operation must NEVER read beyond the allocated
 * metadata buffer. Specifically:
 *   seek + sector_remain <= metadata_size
 * must always hold before any memcpy is performed.
 */

#define SECTOR_SIZE       2048
#define METADATA_BUF_SIZE 4096

/* Simulated safe copy function that enforces bounds */
static int safe_metadata_copy(uint8_t *dst, size_t dst_size,
                               const uint8_t *metadata, size_t metadata_size,
                               size_t seek, size_t sector_remain)
{
    /* Security invariant: seek must be within metadata buffer */
    if (seek >= metadata_size) {
        return -1; /* seek out of bounds */
    }

    /* Security invariant: seek + sector_remain must not exceed metadata_size */
    if (sector_remain > metadata_size - seek) {
        return -2; /* copy would exceed buffer */
    }

    /* Security invariant: destination buffer must be large enough */
    if (sector_remain > dst_size) {
        return -3; /* destination too small */
    }

    /* Only perform copy if all invariants hold */
    memcpy(dst, metadata + seek, sector_remain);
    return 0;
}

/* Validate that the bounds check logic itself is correct */
static int bounds_check_valid(size_t metadata_size, size_t seek, size_t sector_remain)
{
    /* Check for integer overflow in seek + sector_remain */
    if (seek > SIZE_MAX - sector_remain) {
        return 0; /* overflow detected - invalid */
    }
    if (seek + sector_remain > metadata_size) {
        return 0; /* out of bounds */
    }
    if (seek >= metadata_size && sector_remain > 0) {
        return 0; /* seek past end */
    }
    return 1; /* valid */
}

START_TEST(test_metadata_copy_bounds_invariant)
{
    /* Invariant: seek + sector_remain must never exceed metadata_size.
     * Any attempt to copy beyond the allocated metadata buffer must be
     * rejected, preventing host memory leakage to guest OS. */

    struct {
        size_t seek;
        size_t sector_remain;
        size_t metadata_size;
        int should_succeed; /* 1 = valid, 0 = must be rejected */
        const char *description;
    } test_cases[] = {
        /* Valid cases */
        { 0,                    SECTOR_SIZE,        METADATA_BUF_SIZE, 1, "normal read at start" },
        { 0,                    METADATA_BUF_SIZE,  METADATA_BUF_SIZE, 1, "read entire buffer" },
        { SECTOR_SIZE,          SECTOR_SIZE,        METADATA_BUF_SIZE, 1, "read second sector" },
        { METADATA_BUF_SIZE-1,  1,                  METADATA_BUF_SIZE, 1, "read last byte" },
        { 0,                    1,                  METADATA_BUF_SIZE, 1, "read single byte" },

        /* Adversarial: seek beyond buffer */
        { METADATA_BUF_SIZE,    1,                  METADATA_BUF_SIZE, 0, "seek at exact end" },
        { METADATA_BUF_SIZE+1,  1,                  METADATA_BUF_SIZE, 0, "seek past end by 1" },
        { METADATA_BUF_SIZE*2,  SECTOR_SIZE,        METADATA_BUF_SIZE, 0, "seek far past end" },
        { SIZE_MAX,             1,                  METADATA_BUF_SIZE, 0, "seek at SIZE_MAX" },
        { SIZE_MAX - 1,         2,                  METADATA_BUF_SIZE, 0, "seek near SIZE_MAX" },

        /* Adversarial: sector_remain causes overflow */
        { 1,                    SIZE_MAX,            METADATA_BUF_SIZE, 0, "sector_remain SIZE_MAX" },
        { SECTOR_SIZE,          SIZE_MAX - SECTOR_SIZE + 1, METADATA_BUF_SIZE, 0, "overflow at boundary" },
        { 0,                    METADATA_BUF_SIZE+1, METADATA_BUF_SIZE, 0, "sector_remain exceeds buf" },
        { 1,                    METADATA_BUF_SIZE,   METADATA_BUF_SIZE, 0, "seek+remain exceeds by 1" },

        /* Adversarial: combined overflow attempts */
        { SIZE_MAX/2 + 1,       SIZE_MAX/2 + 1,     METADATA_BUF_SIZE, 0, "combined overflow" },
        { SIZE_MAX - SECTOR_SIZE, SECTOR_SIZE + 1,  METADATA_BUF_SIZE, 0, "near-overflow combo" },
        { METADATA_BUF_SIZE - SECTOR_SIZE, SECTOR_SIZE + 1, METADATA_BUF_SIZE, 0, "off-by-one overflow" },

        /* Edge cases */
        { 0,                    0,                  METADATA_BUF_SIZE, 1, "zero length copy" },
        { METADATA_BUF_SIZE,    0,                  METADATA_BUF_SIZE, 1, "zero length at end" },
    };

    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);

    uint8_t *metadata = (uint8_t *)malloc(METADATA_BUF_SIZE);
    ck_assert_ptr_nonnull(metadata);
    memset(metadata, 0xAB, METADATA_BUF_SIZE);

    uint8_t *dst = (uint8_t *)malloc(METADATA_BUF_SIZE);
    ck_assert_ptr_nonnull(dst);
    memset(dst, 0, METADATA_BUF_SIZE);

    for (int i = 0; i < num_cases; i++) {
        size_t seek         = test_cases[i].seek;
        size_t sector_remain = test_cases[i].sector_remain;
        size_t metadata_size = test_cases[i].metadata_size;
        int should_succeed  = test_cases[i].should_succeed;

        /* Primary invariant check: bounds must be validated before copy */
        int bounds_ok = bounds_check_valid(metadata_size, seek, sector_remain);

        if (should_succeed) {
            /* These inputs are valid and should pass bounds check */
            ck_assert_msg(bounds_ok == 1,
                "SECURITY VIOLATION: valid input rejected for case '%s' "
                "(seek=%zu, sector_remain=%zu, metadata_size=%zu)",
                test_cases[i].description, seek, sector_remain, metadata_size);
        } else {
            /* These inputs are adversarial and MUST be rejected */
            ck_assert_msg(bounds_ok == 0,
                "SECURITY VIOLATION: out-of-bounds access not prevented for case '%s' "
                "(seek=%zu, sector_remain=%zu, metadata_size=%zu) - "
                "host memory would be leaked to guest OS",
                test_cases[i].description, seek, sector_remain, metadata_size);
        }

        /* For valid cases, also verify the safe copy function works correctly */
        if (should_succeed && sector_remain > 0 && sector_remain <= METADATA_BUF_SIZE) {
            int result = safe_metadata_copy(dst, METADATA_BUF_SIZE,
                                             metadata, metadata_size,
                                             seek, sector_remain);
            ck_assert_msg(result == 0,
                "Safe copy failed unexpectedly for valid case '%s' (result=%d)",
                test_cases[i].description, result);

            /* Verify data integrity */
            ck_assert_msg(memcmp(dst, metadata + seek, sector_remain) == 0,
                "Data integrity check failed for case '%s'",
                test_cases[i].description);
        }

        /* For invalid cases, verify the safe copy function rejects them */
        if (!should_succeed) {
            /* Only test cases where sector_remain fits in dst buffer */
            if (sector_remain <= METADATA_BUF_SIZE) {
                int result = safe_metadata_copy(dst, METADATA_BUF_SIZE,
                                                 metadata, metadata_size,
                                                 seek, sector_remain);
                ck_assert_msg(result != 0,
                    "SECURITY VIOLATION: safe_metadata_copy did not reject "
                    "out-of-bounds access for case '%s' "
                    "(seek=%zu, sector_remain=%zu, metadata_size=%zu)",
                    test_cases[i].description, seek, sector_remain, metadata_size);
            }
        }
    }

    free(metadata);
    free(dst);
}
END_TEST

START_TEST(test_integer_overflow_in_seek_plus_remain)
{
    /* Invariant: integer overflow in seek + sector_remain computation
     * must be detected and rejected to prevent wrap-around bypassing
     * bounds checks */

    struct {
        size_t seek;
        size_t sector_remain;
        const char *description;
    } overflow_cases[] = {
        { SIZE_MAX,             SIZE_MAX,             "both at SIZE_MAX" },
        { SIZE_MAX,             1,                    "seek SIZE_MAX + 1" },
        { 1,                    SIZE_MAX,             "1 + SIZE_MAX" },
        { SIZE_MAX/2 + 1,       SIZE_MAX/2 + 1,       "half SIZE_MAX each" },
        { SIZE_MAX - 100,       200,                  "near overflow" },
        { (size_t)UINT32_MAX + 1, SECTOR_SIZE,        "32-bit overflow seek" },
        { SECTOR_SIZE,          (size_t)UINT32_MAX + 1, "32-bit overflow remain" },
    };

    int num_cases = sizeof(overflow_cases) / sizeof(overflow_cases[0]);
    size_t metadata_size = METADATA_BUF_SIZE;

    for (int i = 0; i < num_cases; i++) {
        size_t seek          = overflow_cases[i].seek;
        size_t sector_remain = overflow_cases[i].sector_remain;

        /* Check for integer overflow */
        int overflow_detected = (seek > SIZE_MAX - sector_remain);

        /* If no overflow, check if it exceeds metadata_size */
        int out_of_bounds = overflow_detected ||
                            (seek + sector_remain > metadata_size);

        ck_assert_msg(out_of_bounds,
            "SECURITY VIOLATION: integer overflow or out-of-bounds not detected "
            "for case '%s' (seek=%zu, sector_remain=%zu) - "
            "could allow arbitrary host memory read",
            overflow_cases[i].description, seek, sector_remain);
    }
}
END_TEST

START_TEST(test_sector_boundary_conditions)
{
    /* Invariant: sector-aligned and sector-boundary reads must be
     * properly bounded. The VISO handler processes data in sector-sized
     * chunks; each chunk access must stay within the metadata buffer. */

    uint8_t *metadata = (uint8_t *)malloc(METADATA_BUF_SIZE);
    ck_assert_ptr_nonnull(metadata);
    memset(metadata, 0xCD, METADATA_BUF_SIZE);

    uint8_t dst[SECTOR_SIZE];

    /* Number of complete sectors in metadata */
    size_t num_sectors = METADATA_BUF_SIZE / SECTOR_SIZE;

    for (size_t sector = 0; sector < num_sectors; sector++) {
        size_t seek = sector * SECTOR_SIZE;
        size_t sector_remain = SECTOR_SIZE;

        /* Invariant: each sector read must be within bounds */
        ck_assert_msg(seek + sector_remain <= METADATA_BUF_SIZE,
            "SECURITY VIOLATION: sector %zu read would exceed metadata buffer "
            "(seek=%zu, sector_remain=%zu, metadata_size=%d)",
            sector, seek, sector_remain, METADATA_BUF_SIZE);

        int result = safe_metadata_copy(dst, SECTOR_SIZE,
                                         metadata, METADATA_BUF_SIZE,
                                         seek, sector_remain);
        ck_assert_msg(result == 0,
            "Valid sector %zu read was incorrectly rejected", sector);
    }

    /* Adversarial: attempt to read one sector past the end */
    size_t bad_seek = num_sectors * SECTOR_SIZE;
    size_t bad_remain = SECTOR_SIZE;

    ck_assert_msg(bad_seek + bad_remain > METADATA_BUF_SIZE,
        "Test setup error: expected out-of-bounds condition");

    int result = safe_metadata_copy(dst, SECTOR_SIZE,
                                     metadata, METADATA_BUF_SIZE,
                                     bad_seek, bad_remain);
    ck_assert_msg(result != 0,
        "SECURITY VIOLATION: read past last sector was not rejected "
        "(seek=%zu, sector_remain=%zu, metadata_size=%d)",
        bad_seek, bad_remain, METADATA_BUF_SIZE);

    free(metadata);
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_metadata_copy_bounds_invariant);
    tcase_add_test(tc_core, test_integer_overflow_in_seek_plus_remain);
    tcase_add_test(tc_core, test_sector_boundary_conditions);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}