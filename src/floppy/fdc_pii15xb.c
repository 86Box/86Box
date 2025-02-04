/*
 * VARCem   Virtual ARchaeological Computer EMulator.
 *          An emulator of (mostly) x86-based PC systems and devices,
 *          using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *          spanning the era between 1981 and 1995.
 *
 *          Implementation of the DTK MiniMicro series of Floppy Disk Controllers.
 *          Original code from VARCem. Fully rewritten, fixed and improved for 86Box.
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>,
 *          Tiseno100
 *
 *          Copyright 2019 Fred N. van Kempen.
 *          Copyright 2021 Tiseno100
 *
 *          Redistribution and  use  in source  and binary forms, with
 *          or  without modification, are permitted  provided that the
 *          following conditions are met:
 *
 *          1. Redistributions of  source  code must retain the entire
 *             above notice, this list of conditions and the following
 *             disclaimer.
 *
 *          2. Redistributions in binary form must reproduce the above
 *             copyright  notice,  this list  of  conditions  and  the
 *             following disclaimer in  the documentation and/or other
 *             materials provided with the distribution.
 *
 *          3. Neither the  name of the copyright holder nor the names
 *             of  its  contributors may be used to endorse or promote
 *             products  derived from  this  software without specific
 *             prior written permission.
 *
 * THIS SOFTWARE  IS  PROVIDED BY THE  COPYRIGHT  HOLDERS AND CONTRIBUTORS
 * "AS IS" AND  ANY EXPRESS  OR  IMPLIED  WARRANTIES,  INCLUDING, BUT  NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE  ARE  DISCLAIMED. IN  NO  EVENT  SHALL THE COPYRIGHT
 * HOLDER OR  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL,  EXEMPLARY,  OR  CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE  GOODS OR SERVICES;  LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED  AND ON  ANY
 * THEORY OF  LIABILITY, WHETHER IN  CONTRACT, STRICT  LIABILITY, OR  TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING  IN ANY  WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
Notes:
VARCem uses the DP8473 for both floppy disk controllers. The statement though is wrong.

MiniMicro 4 uses a Zilog Z0765A08PSC(Clone of the NEC 765)
MiniMicro 1 uses a National Semiconductor DP8473(Clone of the NEC 765 with additional NSC commands)

Issues:
MiniMicro 4 works only with a few XT machines. This statement has to be confirmed by someone with the real card itself.
MiniMicro 4 also won't work with the XT FDC which the Zilog claims to be.
*/

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>

#define DTK_VARIANT  ((info->local == 158) ? ROM_PII_158B : ROM_PII_151B)
#define DTK_CHIP     ((info->local == 158) ? &fdc_xt_device : &fdc_at_nsc_dp8473_device)
#define BIOS_ADDR    (uint32_t)(device_get_config_hex20("bios_addr") & 0x000fffff)
#define ROM_PII_151B "roms/floppy/dtk/pii-151b.rom"
#define ROM_PII_158B "roms/floppy/dtk/pii-158b.rom"

typedef struct pii_t {
    rom_t bios_rom;
} pii_t;

static void
pii_close(void *priv)
{
    pii_t *dev = (pii_t *) priv;

    free(dev);
}

static void *
pii_init(const device_t *info)
{
    pii_t *dev;

    dev = (pii_t *) calloc(1, sizeof(pii_t));

    if (BIOS_ADDR != 0)
        rom_init(&dev->bios_rom, DTK_VARIANT, BIOS_ADDR, 0x2000, 0x1ffff, 0, MEM_MAPPING_EXTERNAL);

    device_add(DTK_CHIP);

    return dev;
}

static int
pii_151b_available(void)
{
    return rom_present(ROM_PII_151B);
}

static int
pii_158_available(void)
{
    return rom_present(ROM_PII_158B);
}

static const device_config_t pii_config[] = {
  // clang-format off
    {
        .name           = "bios_addr",
        .description    = "BIOS Address",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0xce000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value = 0 },
            { .description = "CA00H",    .value = 0xca000 },
            { .description = "CC00H",    .value = 0xcc000 },
            { .description = "CE00H",    .value = 0xce000 },
            { .description = ""                           }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t fdc_pii151b_device = {
    .name          = "DTK PII-151B (MiniMicro) Floppy Drive Controller",
    .internal_name = "dtk_pii151b",
    .flags         = DEVICE_ISA,
    .local         = 151,
    .init          = pii_init,
    .close         = pii_close,
    .reset         = NULL,
    .available     = pii_151b_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pii_config
};

const device_t fdc_pii158b_device = {
    .name          = "DTK PII-158B (MiniMicro4) Floppy Drive Controller",
    .internal_name = "dtk_pii158b",
    .flags         = DEVICE_ISA,
    .local         = 158,
    .init          = pii_init,
    .close         = pii_close,
    .reset         = NULL,
    .available     = pii_158_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pii_config
};
