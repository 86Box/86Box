/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ICS2494 clock generator emulation.
 *
 *          Used by the AMI S3 924.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020 Miran Grca.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>

typedef struct ics2494_t {
    float freq[16];
} ics2494_t;

#ifdef ENABLE_ICS2494_LOG
int ics2494_do_log = ENABLE_ICS2494_LOG;

static void
ics2494_log(const char *fmt, ...)
{
    va_list ap;

    if (ics2494_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ics2494_log(fmt, ...)
#endif

/* Two consecutive byte-writes are NOT allowed.  Furthermore an index
 * written to 0x01CE is only usable ONCE!  Note also that the setting of ATI
 * extended registers (especially those with clock selection bits) should be
 * bracketed by a sequencer reset.
 *
 * Boards prior to V5 use 4 crystals.  Boards V5 and later use a clock
 * generator chip.  V3 and V4 boards differ when it comes to choosing clock
 * frequencies.
 *
 * VGA Wonder V3/V4 Board Clock Frequencies
 * R E G I S T E R S
 * 1CE(*)    3C2     3C2    Frequency
 * B2h/BEh
 * Bit 6/4  Bit 3   Bit 2   (MHz)
 * ------- ------- -------  -------
 *    0       0       0     50.175
 *    0       0       1     56.644
 *    0       1       0     Spare 1
 *    0       1       1     44.900
 *    1       0       0     44.900
 *    1       0       1     50.175
 *    1       1       0     Spare 2
 *    1       1       1     36.000
 *
 * (*):  V3 uses index B2h, bit 6;  V4 uses index BEh, bit 4
 *
 * V5, PLUS, XL and XL24 usually have an ATI 18810 clock generator chip, but
 * some have an ATI 18811-0, and it's quite conceivable that some exist with
 * ATI 18811-1's or ATI 18811-2's.  Mach32 boards are known to use any one of
 * these clock generators.  The possibilities for Mach64 boards also include
 * two different flavours of the newer 18818 chips.  I have yet to figure out
 * how BIOS initialization sets up the board for a particular set of
 * frequencies.  Mach32 and Mach64 boards also use a different dot clock
 * ordering.  ATI says there is no reliable way for the driver to determine
 * which clock generator is on the board (their BIOS's are tailored to the
 * board).
 *
 * VGA Wonder V5/PLUS/XL/XL24 Board Clock Frequencies
 * R E G I S T E R S
 *   1CE     1CE     3C2     3C2    Frequency
 *   B9h     BEh                     (MHz)   18811-0  18811-1
 *  Bit 1   Bit 4   Bit 3   Bit 2    18810   18812-0  18811-2  18818-?  18818-?
 * ------- ------- ------- -------  -------  -------  -------  -------  -------
 *    0       0       0       0      30.240   30.240  135.000     (*3)     (*3)
 *    0       0       0       1      32.000   32.000   32.000  110.000  110.000
 *    0       0       1       0      37.500  110.000  110.000  126.000  126.000
 *    0       0       1       1      39.000   80.000   80.000  135.000  135.000
 *    0       1       0       0      42.954   42.954  100.000   50.350   25.175
 *    0       1       0       1      48.771   48.771  126.000   56.644   28.322
 *    0       1       1       0        (*1)   92.400   92.400   63.000   31.500
 *    0       1       1       1      36.000   36.000   36.000   72.000   36.000
 *    1       0       0       0      40.000   39.910   39.910     (*3)     (*3)
 *    1       0       0       1      56.644   44.900   44.900   80.000   80.000
 *    1       0       1       0      75.000   75.000   75.000   75.000   75.000
 *    1       0       1       1      65.000   65.000   65.000   65.000   65.000
 *    1       1       0       0      50.350   50.350   50.350   40.000   40.000
 *    1       1       0       1      56.640   56.640   56.640   44.900   44.900
 *    1       1       1       0        (*2)     (*3)     (*3)   49.500   49.500
 *    1       1       1       1      44.900   44.900   44.900   50.000   50.000
 *
 * (*1) External 0 (supposedly 16.657 Mhz)
 * (*2) External 1 (supposedly 28.322 MHz)
 * (*3) This setting doesn't seem to generate anything
 *
 * Mach32 and Mach64 Board Clock Frequencies
 * R E G I S T E R S
 *   1CE     1CE     3C2     3C2    Frequency
 *   B9h     BEh                     (MHz)   18811-0  18811-1
 *  Bit 1   Bit 4   Bit 3   Bit 2    18810   18812-0  18811-2  18818-?  18818-?
 * ------- ------- ------- -------  -------  -------  -------  -------  -------
 *    0       0       0       0      42.954   42.954  100.000   50.350   25.175
 *    0       0       0       1      48.771   48.771  126.000   56.644   28.322
 *    0       0       1       0        (*1)   92.400   92.400   63.000   31.500
 *    0       0       1       1      36.000   36.000   36.000   72.000   36.000
 *    0       1       0       0      30.240   30.240  135.000     (*3)     (*3)
 *    0       1       0       1      32.000   32.000   32.000  110.000  110.000
 *    0       1       1       0      37.500  110.000  110.000  126.000  126.000
 *    0       1       1       1      39.000   80.000   80.000  135.000  135.000
 *    1       0       0       0      50.350   50.350   50.350   40.000   40.000
 *    1       0       0       1      56.640   56.640   56.640   44.900   44.900
 *    1       0       1       0        (*2)     (*3)     (*3)   49.500   49.500
 *    1       0       1       1      44.900   44.900   44.900   50.000   50.000
 *    1       1       0       0      40.000   39.910   39.910     (*3)     (*3)
 *    1       1       0       1      56.644   44.900   44.900   80.000   80.000
 *    1       1       1       0      75.000   75.000   75.000   75.000   75.000
 *    1       1       1       1      65.000   65.000   65.000   65.000   65.000
 *
 * (*1) External 0 (supposedly 16.657 Mhz)
 * (*2) External 1 (supposedly 28.322 MHz)
 * (*3) This setting doesn't seem to generate anything
 *
 * Note that, to reduce confusion, this driver masks out the different clock
 * ordering.
 *
 * For all boards, these frequencies can be divided by 1, 2, 3 or 4.
 *
 *      Register 1CE, index B8h
 *       Bit 7    Bit 6
 *      -------  -------
 *         0        0           Divide by 1
 *         0        1           Divide by 2
 *         1        0           Divide by 3
 *         1        1           Divide by 4
 *
 * There is some question as to whether or not bit 1 of index 0xB9 can
 * be used for clock selection on a V4 board.  This driver makes it
 * available only if the "undocumented_clocks" option (itself
 * undocumented :-)) is specified in XF86Config.
 *
 * Also it appears that bit 0 of index 0xB9 can also be used for clock
 * selection on some boards.  It is also only available under XF86Config
 * option "undocumented_clocks".
 */

float
ics2494_getclock(int clock, void *priv)
{
    const ics2494_t *ics2494 = (ics2494_t *) priv;

    if (clock > 15)
        clock = 15;

    return ics2494->freq[clock];
}

static void *
ics2494_init(const device_t *info)
{
    ics2494_t *ics2494 = (ics2494_t *) malloc(sizeof(ics2494_t));
    memset(ics2494, 0, sizeof(ics2494_t));

    switch (info->local) {
        case 0:
            /* ATI 18810 for ATI 28800 */
            ics2494->freq[0] = 30240000.0;
            ics2494->freq[1] = 32000000.0;
            ics2494->freq[2] = 37500000.0;
            ics2494->freq[3] = 39000000.0;
            ics2494->freq[4] = 42954000.0;
            ics2494->freq[5] = 48771000.0;
            ics2494->freq[6] = 0.0;
            ics2494->freq[7] = 36000000.0;
            ics2494->freq[8] = 40000000.0;
            ics2494->freq[9] = 56644000.0;
            ics2494->freq[10] = 75000000.0;
            ics2494->freq[11] = 65000000.0;
            ics2494->freq[12] = 50350000.0;
            ics2494->freq[13] = 56640000.0;
            ics2494->freq[14] = 0.0;
            ics2494->freq[15] = 44900000.0;
            break;
        case 1:
            /* ATI 18811-0/ATI 18812-0 for ATI 28800 */
            ics2494->freq[0] = 42950000.0;
            ics2494->freq[1] = 48770000.0;
            ics2494->freq[2] = 92400000.0;
            ics2494->freq[3] = 36000000.0;
            ics2494->freq[4] = 50350000.0;
            ics2494->freq[5] = 56640000.0;
            ics2494->freq[7] = 44900000.0;
            ics2494->freq[8] = 30240000.0;
            ics2494->freq[9] = 32000000.0;
            ics2494->freq[10] = 110000000.0;
            ics2494->freq[11] = 80000000.0;
            ics2494->freq[12] = 39910000.0;
            ics2494->freq[13] = 44900000.0;
            ics2494->freq[14] = 75000000.0;
            ics2494->freq[15] = 65000000.0;
            break;
        case 2:
            /* ATI 18811-1/ATI 18811-2 for ATI 28800 */
            ics2494->freq[0] = 100000000.0;
            ics2494->freq[1] = 126000000.0;
            ics2494->freq[2] = 92400000.0;
            ics2494->freq[3] = 36000000.0;
            ics2494->freq[4] = 50350000.0;
            ics2494->freq[5] = 56640000.0;
            ics2494->freq[7] = 44900000.0;
            ics2494->freq[8] = 135000000.0;
            ics2494->freq[9] = 32000000.0;
            ics2494->freq[10] = 110000000.0;
            ics2494->freq[11] = 80000000.0;
            ics2494->freq[12] = 39910000.0;
            ics2494->freq[13] = 44900000.0;
            ics2494->freq[14] = 75000000.0;
            ics2494->freq[15] = 65000000.0;
            break;
        case 100:
            /* ATI 18810 for ATI Mach32 */
            ics2494->freq[0] = 42954000.0;
            ics2494->freq[1] = 48771000.0;
            ics2494->freq[2] = 0.0;
            ics2494->freq[3] = 36000000.0;
            ics2494->freq[4] = 30240000.0;
            ics2494->freq[5] = 32000000.0;
            ics2494->freq[6] = 37500000.0;
            ics2494->freq[7] = 39000000.0;
            ics2494->freq[8] = 50350000.0;
            ics2494->freq[9] = 56640000.0;
            ics2494->freq[10] = 0.0;
            ics2494->freq[11] = 44900000.0;
            ics2494->freq[12] = 40000000.0;
            ics2494->freq[13] = 56644000.0;
            ics2494->freq[14] = 75000000.0;
            ics2494->freq[15] = 65000000.0;
            break;
        case 101:
            /* ATI 18811-0/ATI 18812-0 for ATI Mach32 */
            ics2494->freq[0] = 42954000.0;
            ics2494->freq[1] = 48771000.0;
            ics2494->freq[2] = 92400000.0;
            ics2494->freq[3] = 36000000.0;
            ics2494->freq[4] = 30240000.0;
            ics2494->freq[5] = 32000000.0;
            ics2494->freq[6] = 110000000.0;
            ics2494->freq[7] = 80000000.0;
            ics2494->freq[8] = 50350000.0;
            ics2494->freq[9] = 56640000.0;
            ics2494->freq[10] = 0.0;
            ics2494->freq[11] = 44900000.0;
            ics2494->freq[12] = 39910000.0;
            ics2494->freq[13] = 44900000.0;
            ics2494->freq[14] = 75000000.0;
            ics2494->freq[15] = 65000000.0;
            break;
        case 102:
            /* ATI 18811-1/ATI 18811-2 for ATI Mach32 */
            ics2494->freq[0] = 100000000.0;
            ics2494->freq[1] = 126000000.0;
            ics2494->freq[2] = 92400000.0;
            ics2494->freq[3] = 36000000.0;
            ics2494->freq[4] = 50350000.0;
            ics2494->freq[5] = 56640000.0;
            ics2494->freq[7] = 44900000.0;
            ics2494->freq[8] = 135000000.0;
            ics2494->freq[9] = 32000000.0;
            ics2494->freq[10] = 110000000.0;
            ics2494->freq[11] = 80000000.0;
            ics2494->freq[12] = 39910000.0;
            ics2494->freq[13] = 44900000.0;
            ics2494->freq[14] = 75000000.0;
            ics2494->freq[15] = 65000000.0;
            break;
        case 305:
            /* ICS2494A(N)-305 for S3 86C924 */
            ics2494->freq[0x0] = 25175000.0;
            ics2494->freq[0x1] = 28322000.0;
            ics2494->freq[0x2] = 40000000.0;
            ics2494->freq[0x3] = 0.0;
            ics2494->freq[0x4] = 50000000.0;
            ics2494->freq[0x5] = 77000000.0;
            ics2494->freq[0x6] = 36000000.0;
            ics2494->freq[0x7] = 44889000.0;
            ics2494->freq[0x8] = 130000000.0;
            ics2494->freq[0x9] = 120000000.0;
            ics2494->freq[0xa] = 80000000.0;
            ics2494->freq[0xb] = 31500000.0;
            ics2494->freq[0xc] = 110000000.0;
            ics2494->freq[0xd] = 65000000.0;
            ics2494->freq[0xe] = 75000000.0;
            ics2494->freq[0xf] = 94500000.0;
            break;
        case 324:
            /* ICS2494A(N)-324 for Tseng ET4000/W32 series */
            ics2494->freq[0x0] = 50000000.0;
            ics2494->freq[0x1] = 56644000.0;
            ics2494->freq[0x2] = 65000000.0;
            ics2494->freq[0x3] = 72000000.0;
            ics2494->freq[0x4] = 80000000.0;
            ics2494->freq[0x5] = 89800000.0;
            ics2494->freq[0x6] = 63000000.0;
            ics2494->freq[0x7] = 75000000.0;
            ics2494->freq[0x8] = 83078000.0;
            ics2494->freq[0x9] = 93463000.0;
            ics2494->freq[0xa] = 100000000.0;
            ics2494->freq[0xb] = 104000000.0;
            ics2494->freq[0xc] = 108000000.0;
            ics2494->freq[0xd] = 120000000.0;
            ics2494->freq[0xe] = 130000000.0;
            ics2494->freq[0xf] = 134700000.0;
            break;

        default:
            break;
    }

    return ics2494;
}

static void
ics2494_close(void *priv)
{
    ics2494_t *ics2494 = (ics2494_t *) priv;

    if (ics2494)
        free(ics2494);
}

const device_t ics2494an_305_device = {
    .name          = "ICS2494AN-305 Clock Generator",
    .internal_name = "ics2494an_305",
    .flags         = 0,
    .local         = 305,
    .init          = ics2494_init,
    .close         = ics2494_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ics2494an_324_device = {
    .name          = "ICS2494AN-324 Clock Generator",
    .internal_name = "ics2494an_324",
    .flags         = 0,
    .local         = 324,
    .init          = ics2494_init,
    .close         = ics2494_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ati18810_28800_device = {
    .name          = "ATI 18810 (ATI 28800) Clock Generator",
    .internal_name = "ati18810_28800",
    .flags         = 0,
    .local         = 0,
    .init          = ics2494_init,
    .close         = ics2494_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ati18811_0_28800_device = {
    .name          = "ATI 18811-0 (ATI 28800) Clock Generator",
    .internal_name = "ati18811_0_28800",
    .flags         = 0,
    .local         = 1,
    .init          = ics2494_init,
    .close         = ics2494_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ati18811_1_28800_device = {
    .name          = "ATI 18811-1 (ATI 28800) Clock Generator",
    .internal_name = "ati18811_1_28800",
    .flags         = 0,
    .local         = 2,
    .init          = ics2494_init,
    .close         = ics2494_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ati18810_mach32_device = {
    .name          = "ATI 18810 (ATI Mach32) Clock Generator",
    .internal_name = "ati18810_mach32",
    .flags         = 0,
    .local         = 100,
    .init          = ics2494_init,
    .close         = ics2494_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ati18811_0_mach32_device = {
    .name          = "ATI 18811-0 (ATI Mach32) Clock Generator",
    .internal_name = "ati18811_0_mach32",
    .flags         = 0,
    .local         = 101,
    .init          = ics2494_init,
    .close         = ics2494_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ati18811_1_mach32_device = {
    .name          = "ATI 18811-1 (ATI Mach32) Clock Generator",
    .internal_name = "ati18811_1_mach32",
    .flags         = 0,
    .local         = 102,
    .init          = ics2494_init,
    .close         = ics2494_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
