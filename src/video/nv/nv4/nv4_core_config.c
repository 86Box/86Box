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
#include <86box/nv/vid_nv4.h>

const device_config_t nv4_config[] =
{
    // Memory configuration
    {
        .name = "vram_size",
        .description = "VRAM Size",
        .type = CONFIG_SELECTION,
        .default_int = NV4_VRAM_SIZE_16MB,
        .selection = 
        {
            // I thought this was never released, but it seems that at least one was released:
            // The card was called the "NEC G7AGK"
            {
                .description = "8 MB",
                .value = NV4_VRAM_SIZE_8MB,
            },

            {
                .description = "16 MB",
                .value = NV4_VRAM_SIZE_16MB,
            },
        }

    },
    // Multithreading configuration
    {

        .name = "pgraph_threads",
        .description = "Render threads",
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