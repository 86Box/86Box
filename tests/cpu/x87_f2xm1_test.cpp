// SPDX-License-Identifier: GPL-2.0-or-later
// Independent binary80 oracles for the production SoftFloat F2XM1 routine.
#include <cstdint>
#include <cstdio>
#include "softfloat3e/fpu_trans.h"

// The linked SoftFloat object library also contains model-aware compares.
extern "C" { int fpu_type; }

static unsigned checks, failures;

static void
check(bool pass, const char *what, unsigned vector, unsigned rc)
{
    checks++;
    if (!pass) {
        if (failures < 8)
            std::fprintf(stderr, "vector %u RC %u: %s\n", vector, rc, what);
        failures++;
    }
}

static softfloat_status_t
status(unsigned rc)
{
    softfloat_status_t result = {};
    result.extF80_roundingPrecision = 80;
    result.softfloat_roundingMode = rc;
    result.softfloat_exceptionMasks = 0x3f;
    return result;
}

static bool
within_one_ulp(floatx80 a, floatx80 b)
{
    if (a.signExp == b.signExp)
        return (a.signif > b.signif ? a.signif - b.signif : b.signif - a.signif) <= 1;
    if (a.signExp + 1 == b.signExp)
        return a.signif == UINT64_MAX && b.signif == UINT64_C(0x8000000000000000);
    if (b.signExp + 1 == a.signExp)
        return b.signif == UINT64_MAX && a.signif == UINT64_C(0x8000000000000000);
    return false;
}

int
main()
{
    // Generated from exp(x*ln(2))-1 at 250 decimal digits. Each oracle
    // records truncation toward zero and whether nearest-even rounds away.
    // Inputs include extended values not representable in host double.
    static const struct {
        uint16_t input_exp;
        uint64_t input_sig;
        uint16_t result_exp;
        uint64_t result_sig;
        bool nearest_away;
    } vectors[] = {
        { 0x3ffe, UINT64_C(0x8000000000000000), 0x3ffd, UINT64_C(0xd413cccfe7799211), 0 },
        { 0x3ffe, UINT64_C(0xc000000000000000), 0x3ffe, UINT64_C(0xae89f995ad3ad5e8), 0 },
        { 0x3ffe, UINT64_C(0xe666666666666800), 0x3ffe, UINT64_C(0xddb680117ab122eb), 1 },
        { 0x3ffe, UINT64_C(0xfd70a3d70a3d7000), 0x3ffe, UINT64_C(0xfc769e9b9c35db72), 1 },
        { 0x3ffe, UINT64_C(0xffffffffffffffff), 0x3ffe, UINT64_C(0xfffffffffffffffe), 1 },
        { 0x3ffe, UINT64_C(0xdeadbeef12345678), 0x3ffe, UINT64_C(0xd3d452219c9396c4), 0 },
        { 0x3ffd, UINT64_C(0x9a209a84fbcff799), 0x3ffc, UINT64_C(0xed979e2b4429fa80), 1 },
        { 0x3ffc, UINT64_C(0xed979e2b4429fa81), 0x3ffc, UINT64_C(0xb2ab36dcb346c93b), 1 },
        { 0x3ffc, UINT64_C(0xb2ab36dcb346c93c), 0x3ffc, UINT64_C(0x83a4d99b068361bd), 1 },
        { 0x3f9b, UINT64_C(0xa123456789abcdef), 0x3f9a, UINT64_C(0xdf62678f4d3f7fd9), 0 },
        { 0xbffe, UINT64_C(0x8000000000000000), 0xbffd, UINT64_C(0x95f619980c4336f7), 0 },
        { 0xbffe, UINT64_C(0xc000000000000000), 0xbffd, UINT64_C(0xcf901f5ce48ead21), 0 },
        { 0xbffe, UINT64_C(0xe666666666666800), 0xbffd, UINT64_C(0xeda0411daf99becd), 1 },
        { 0xbffe, UINT64_C(0xfd70a3d70a3d7000), 0xbffd, UINT64_C(0xfe38293bfb0f44ff), 0 },
        { 0xbffe, UINT64_C(0xffffffffffffffff), 0xbffd, UINT64_C(0xffffffffffffffff), 0 },
        { 0xbffe, UINT64_C(0xdeadbeef12345678), 0xbffd, UINT64_C(0xe7d458d32a06b4bc), 1 },
        { 0xbffd, UINT64_C(0x9a209a84fbcff799), 0xbffc, UINT64_C(0xc0d8ded42ff36a7e), 1 },
        { 0xbffc, UINT64_C(0xed979e2b4429fa81), 0xbffc, UINT64_C(0x98202f247fb320ef), 0 },
        { 0xbffc, UINT64_C(0xb2ab36dcb346c93c), 0xbffb, UINT64_C(0xe94bb517a89957db), 1 },
        { 0xbf9b, UINT64_C(0xa123456789abcdef), 0xbf9a, UINT64_C(0xdf62678f4d3f7fd9), 0 },
        // Exactly representable CORDIC ROM cut and its binary80 neighbours:
        // exercise zero residues and residues smaller than a register bit.
        { 0x3ffc, UINT64_C(0xae00d1cfdeb43cfc), 0x3ffb, UINT64_C(0xfffffffffffffffe), 0 },
        { 0x3ffc, UINT64_C(0xae00d1cfdeb43cfd), 0x3ffb, UINT64_C(0xffffffffffffffff), 1 },
        { 0x3ffc, UINT64_C(0xae00d1cfdeb43cfe), 0x3ffc, UINT64_C(0x8000000000000000), 1 },
        { 0xbffc, UINT64_C(0xae00d1cfdeb43cfc), 0xbffb, UINT64_C(0xe38e38e38e38e38c), 1 },
        { 0xbffc, UINT64_C(0xae00d1cfdeb43cfd), 0xbffb, UINT64_C(0xe38e38e38e38e38e), 0 },
        { 0xbffc, UINT64_C(0xae00d1cfdeb43cfe), 0xbffb, UINT64_C(0xe38e38e38e38e38f), 0 },
        // Small-residue kernel and the shared tiny-argument threshold.
        { 0x3fef, UINT64_C(0xa123456789abcdef), 0x3fee, UINT64_C(0xdf62c905b4b83f82), 1 },
        { 0x3fbb, UINT64_C(0xa123456789abcdef), 0x3fba, UINT64_C(0xdf62678f4d3f7fd9), 0 },
        { 0x3fba, UINT64_C(0xa123456789abcdef), 0x3fb9, UINT64_C(0xdf62678f4d3f7fd9), 0 },
        { 0xbfef, UINT64_C(0xa123456789abcdef), 0xbfee, UINT64_C(0xdf6206191e7916f4), 0 },
        { 0xbfbb, UINT64_C(0xa123456789abcdef), 0xbfba, UINT64_C(0xdf62678f4d3f7fd9), 0 },
        { 0xbfba, UINT64_C(0xa123456789abcdef), 0xbfb9, UINT64_C(0xdf62678f4d3f7fd9), 0 }
    };
    for (unsigned model = 0; model < 2; model++) {
        auto calculate = model ? f2xm1_387 : f2xm1;
        for (unsigned i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
            for (unsigned rc = 0; rc < 4; rc++) {
                auto st = status(rc);
                const auto &v = vectors[i];
                floatx80 input = { v.input_sig, v.input_exp };
                floatx80 expected = { v.result_sig, v.result_exp };
                bool negative = v.result_exp & 0x8000;
                bool away = (rc == 0) ? v.nearest_away : (rc == 1 && negative) || (rc == 2 && !negative);
                if (away && ++expected.signif == 0) {
                    expected.signif = UINT64_C(0x8000000000000000);
                    expected.signExp++;
                }
                auto actual = calculate(input, &st);
                // Compare both models to independent mathematical oracles.
                // Early CORDIC may differ in the last bit.
                check(model ? within_one_ulp(actual, expected) :
                      actual.signExp == expected.signExp && actual.signif == expected.signif,
                      "result", i + 100 * model, rc);
                check((st.softfloat_exceptionFlags & 0x3f) == softfloat_flag_inexact, "exceptions", i + 100 * model, rc);
            }
        }
        for (unsigned sign = 0; sign < 2; sign++) {
            auto st = status(0);
            floatx80 zero = { 0, uint16_t(sign << 15) };
            auto actual = calculate(zero, &st);
            check(actual.signExp == zero.signExp && actual.signif == 0, "signed zero", 20 + sign + 100 * model, 0);
            check(st.softfloat_exceptionFlags == 0, "exact zero flags", 20 + sign + 100 * model, 0);
        }
        auto st = status(0);
        floatx80 unsupported = { UINT64_C(0x4000000000000000), 0x3fff };
        auto actual = calculate(unsupported, &st);
        check((actual.signExp & 0x7fff) == 0x7fff && (actual.signif << 1) != 0, "unsupported is NaN", 22 + 100 * model, 0);
        check(st.softfloat_exceptionFlags == softfloat_flag_invalid, "unsupported flags", 22 + 100 * model, 0);
    }

    // Intel Math CoProcessor Utilities' actual Intel387 compatibility
    // signature. Only the final value is a hardware reference; do not
    // constrain the individual steps to outputs of this implementation.
    floatx80 chain = { UINT64_C(0x9a209a84fbcff799), 0x3ffd };
    for (unsigned i = 0; i < 3; i++) {
        auto st = status(0);
        chain = f2xm1_387(chain, &st);
        check((st.softfloat_exceptionFlags & 0x3f) == softfloat_flag_inexact, "chain flags", 23, i);
    }
    check(chain.signExp == 0x3ffc && chain.signif == UINT64_C(0x83a4d99b068361bc), "Intel387 signature", 23, 0);

    std::printf("SoftFloat F2XM1 regression: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
