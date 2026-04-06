/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Intel 82815EP SPD Memory Hack
 *
 *
 *
 * Authors: Tiseno100,
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2022      Tiseno100.
 *          Copyright 2022-2023 Jasmine Iwanek.
 */

/* This is a hack because the 86Box SPD calculation algorithm is not made for the 815EP banking.
   This SHOULD ONLY be used with the 815EP chipset.                                              */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/spd.h>

#define MEM_SIZE_MB (mem_size >> 10)

uint8_t
intel_815ep_get_banking(void)
{
    switch (MEM_SIZE_MB) {
        case 32:
            return 0x02;

        case 64:
            return 0x01;

        case 96:
            return 0x21;

        case 128:
            return 0x04;

        case 160:
            return 0x24;

        case 192:
            return 0x06;

        case 256:
            return 0x07;

        case 320:
            return 0x57;

        case 384:
            return 0x97;

        case 512:
            return 0xed;

        default:
            return 0;
    }
}

void
intel_815ep_spd_init(void)
{
    switch (MEM_SIZE_MB) {
        case 32:
            spd_register(SPD_TYPE_SDRAM, 1, 32);
            break;

        case 64:
            spd_register(SPD_TYPE_SDRAM, 3, 32);
            break;

        case 96:
            spd_register(SPD_TYPE_SDRAM, 7, 32);
            break;

        case 128:
            spd_register(SPD_TYPE_SDRAM, 3, 64);
            break;

        case 160:
            spd_register(SPD_TYPE_SDRAM, 7, 64);
            break;

        case 192:
            spd_register(SPD_TYPE_SDRAM, 3, 96);
            break;

        case 256:
            spd_register(SPD_TYPE_SDRAM, 3, 128);
            break;

        case 320:
            spd_register(SPD_TYPE_SDRAM, 7, 128);
            break;

        case 384:
            spd_register(SPD_TYPE_SDRAM, 7, 128);
            break;

        case 512:
            spd_register(SPD_TYPE_SDRAM, 3, 256);
            break;

        default:
            pclog("Intel 815EP SPD Hack: Illegal Size %dMB\n", MEM_SIZE_MB);
            break;
    }
}
