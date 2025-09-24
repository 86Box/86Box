/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV4 debug register list
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *
 *          Copyright 2024-2025 starfrost
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/pci.h>
#include <86box/rom.h> 
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv4.h>

#ifdef NV_LOG

nv_register_t nv4_registers[] = {
    { NV4_PTIMER_INTR, "NV4 PTIMER - Interrupt Status", NULL, NULL},
    { NV4_PTIMER_INTR_EN, "NV4 PTIMER - Interrupt Enable", NULL, NULL,},
    { NV4_PTIMER_NUMERATOR, "NV4 PTIMER - Numerator", NULL, NULL, },
    { NV4_PTIMER_DENOMINATOR, "NV4 PTIMER - Denominator", NULL, NULL, },
    { NV4_PTIMER_TIME_0_NSEC, "NV4 PTIMER - Time0", NULL, NULL, },
    { NV4_PTIMER_TIME_1_NSEC, "NV4 PTIMER - Time1", NULL, NULL, },
    { NV4_PTIMER_ALARM_NSEC, "NV4 PTIMER - Alarm", NULL, NULL, },
    { NV4_PRAMDAC_VPLL_COEFF, "NV4 PRAMDAC - Pixel Clock Coefficient", NULL, NULL, },
    { NV4_PRAMDAC_NVPLL_COEFF, "NV4 PRAMDAC - GPU Core Clock Coefficient", NULL, NULL, },
    { NV4_PRAMDAC_MPLL_COEFF, "NV4 PRAMDAC - VRAM Clock Coefficient", NULL, NULL, },
    { NV_REG_LIST_END, NULL, NULL, NULL}, // sentinel value 
};

#endif