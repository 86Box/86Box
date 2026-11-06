/* Regression for BIOS CRT detection through the on-chip DAC comparator. */
#undef NDEBUG
#include <assert.h>
#include "../../src/video/vid_ati_mach64.h"
static uint8_t sense;
static int reads;
uint8_t svga_in(uint16_t port, void *priv)
{
    assert(port == 0x3c2);
    assert(priv != NULL);
    reads++;
    return sense;
}
int main(void)
{
    mach64_t card = {0};
    const int types[] = { MACH64_CT, MACH64_VT, MACH64_VT2, MACH64_VT3, MACH64_GTB };
    for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        card.type = types[i];
        card.dac_cntl = 0x0000010c;
        sense = 0x10;
        assert(mach64_dac_cntl_low(&card) == 0x8c);
        /* A write cannot pin the read-only comparator high. */
        card.dac_cntl |= 0x80;
        sense = 0;
        assert(mach64_dac_cntl_low(&card) == 0x0c);
    }
    /* GX uses its external RAMDAC; leave its control bits alone. */
    card.type = MACH64_GX;
    card.dac_cntl = 0x85;
    reads = 0;
    assert(mach64_dac_cntl_low(&card) == 0x85);
    assert(reads == 0);
    puts("Mach64 DAC monitor detection: all checks passed");
    return 0;
}
