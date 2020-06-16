/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of the DTK PII-151B and PII-158B cards.
 *
 *		These are DP8473-based floppy controller ISA cards for XT
 *		class systems, and allow usage of standard and high-density
 *		drives on them. They have their own BIOS which takes over
 *		from the standard system BIOS.
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2019 Fred N. van Kempen.
 *
 *		Redistribution and  use  in source  and binary forms, with
 *		or  without modification, are permitted  provided that the
 *		following conditions are met:
 *
 *		1. Redistributions of  source  code must retain the entire
 *		   above notice, this list of conditions and the following
 *		   disclaimer.
 *
 *		2. Redistributions in binary form must reproduce the above
 *		   copyright  notice,  this list  of  conditions  and  the
 *		   following disclaimer in  the documentation and/or other
 *		   materials provided with the distribution.
 *
 *		3. Neither the  name of the copyright holder nor the names
 *		   of  its  contributors may be used to endorse or promote
 *		   products  derived from  this  software without specific
 *		   prior written permission.
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
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
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
#include <86box/pic.h>
#include <86box/rom.h>
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>

#define ROM_PII_151B	L"roms/floppy/dtk/pii-151b.rom"
#define ROM_PII_158B	L"roms/floppy/dtk/pii-158b.rom"

typedef struct {
    const char	*name;
    int		type;

    uint32_t	bios_addr,
		bios_size;
    rom_t	bios_rom;
    
    fdc_t 	*fdc;
} pii_t;


/* Load and enable a BIOS ROM if we have one, and is enabled. */
static void
set_bios(pii_t *dev, wchar_t *fn)
{
    uint32_t temp;
    FILE *fp;

    /* Only do this if needed. */
    if ((fn == NULL) || (dev->bios_addr == 0)) return;

    if ((fp = rom_fopen(fn, L"rb")) == NULL) return;

    (void)fseek(fp, 0L, SEEK_END);
    temp = ftell(fp);
    (void)fclose(fp);

    /* Assume 128K, then go down. */
    dev->bios_size = 0x020000;
    while (temp < dev->bios_size)
	dev->bios_size >>= 1;

    /* Create a memory mapping for the space. */
    rom_init(&dev->bios_rom, fn, dev->bios_addr,
	     dev->bios_size, dev->bios_size-1, 0, MEM_MAPPING_EXTERNAL);
}


static void
pii_close(void *priv)
{
    pii_t *dev = (pii_t *)priv;

    free(dev);
}


static void *
pii_init(const device_t *info)
{
    pii_t *dev;

    dev = (pii_t *)malloc(sizeof(pii_t));
    memset(dev, 0x00, sizeof(pii_t));
    dev->type = info->local;

    dev->bios_addr = device_get_config_hex20("bios_addr");

    if (dev->bios_addr != 0x000000) {
	switch (dev->type) {
	    case 151:
		set_bios(dev, ROM_PII_151B);
		break;
	    case 158:
		set_bios(dev, ROM_PII_158B);
		break;
	}
    }

    /* Attach the DP8473 chip. */
    dev->fdc = device_add(&fdc_at_device);

    //pclog("FDC: %s (I/O=%04X, flags=%08x)\n",
    //	info->name, dev->fdc->base_address, dev->fdc->flags);

    return(dev);
}

static int pii_151b_available(void)
{
    return rom_present(ROM_PII_151B);
}

static int pii_158_available(void)
{
    return rom_present(ROM_PII_158B);
}

static const device_config_t pii_config[] = {
    {
	"bios_addr", "BIOS address", CONFIG_HEX20, "", 0x0ce000,
	{
		{
			"Disabled", 0
		},
		{
			"CA00H", 0x0ca000
		},
		{
			"CC00H", 0x0cc000
		},
		{
			"CE00H", 0x0ce000
		},
		{
			""
		}
	}
    },
    {
	"", "", -1
    }
};

const device_t fdc_pii151b_device = {
    "DTK PII-151B (MiniMicro) Floppy Drive Controller",
    DEVICE_ISA,
    151,
    pii_init, pii_close, NULL,
    pii_151b_available, NULL, NULL,
    pii_config
};

const device_t fdc_pii158b_device = {
    "DTK PII-158B (MiniMicro4) Floppy Drive Controller",
    DEVICE_ISA,
    158,
    pii_init, pii_close, NULL,
    pii_158_available, NULL, NULL,
    pii_config
};
