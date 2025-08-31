/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Provides NV4 configuration
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
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv1.h>

const device_config_t nv1_config[] =
{
    // Memory configuration
    {
        .name = "vram_size",
        .description = "VRAM Size",
        .type = CONFIG_SELECTION,
        .default_int = NV1_VRAM_SIZE_4MB,
        .selection = 
        {
            // I thought this was never released, but it seems that at least one was released:
            // The card was called the "NEC G7AGK"
            {
                .description = "1 MB",
                .value = NV1_VRAM_SIZE_1MB,
            },
            {
                .description = "2 MB",
                .value = NV1_VRAM_SIZE_2MB,
            },
            {
                .description = "4 MB",
                .value = NV1_VRAM_SIZE_4MB,
            },
        }

    },
    // Multithreading configuration
    {

        .name = "pgraph_threads",
#ifndef RELEASE_BUILD
        .description = "PFIFO/PGRAPH - Number of threads to split large object method execution into",
#else
        .description = "Render threads",
#endif 
        .type = CONFIG_SELECTION,
        .default_int = 1, // todo: change later
        .selection = 
        {
            {
                .description = "1 thread (Only use if issues appear with more threads)",
                .value = 1,
            },
            {
                .description = "2 threads",
                .value = 2,
            },
            {   
                .description = "4 threads",
                .value = 4,
            },
            {
                .description = "8 threads",
                .value = 8,
            },
        },
    },
    {
        .name = "RAMDAC Type",
        .description = "SGS-Thomson RAMDAC type",
        .default_int = 0x1764,
        .type = CONFIG_SELECTION,
        .selection = 
        {
            {
                .description = "SGS-Thomson STG-1732X",
                .value = 0x1732,
            },
            {
                .description = "SGS-Thomson STG-1764X/NVDAC64",
                .value = 0x1764,
            },
        }
    },
    {
        .name = "Chip type",
        .description = "Chip type",
        .default_int = 0x1,
        .type = CONFIG_SELECTION,
        .selection = 
        {
            {
                .description = "SGS-Thomson STG-2000",
                .value = 0x2000,
            },
            {
                .description = "Nvidia NV1",
                .value = 0x1,
            },
        }
    },    
#ifndef RELEASE_BUILD
    {
        .name = "nv_debug_fulllog",
        .description = "Disable Cyclical Lines Detection for nv_log (Use for getting full context at cost of VERY large log files)",
        .type = CONFIG_BINARY,
        .default_int = 0,
    },
#endif
    {
        .type = CONFIG_END
    }
};