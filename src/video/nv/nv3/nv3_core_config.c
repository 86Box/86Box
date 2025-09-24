/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Provides NV3 configuration
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
#include <86box/nv/vid_nv3.h>

const device_config_t nv3_config[] =
{
    // VBIOS type configuration
    {
        .name = "vbios",
        .description = "Model",
        .type = CONFIG_BIOS,
        .default_string = "NV3_VBIOS_ERAZOR_V15403",
        .default_int = 0,
        .bios = 
        {

           { 
                .name = "ELSA VICTORY Erazor - Version 1.47.00", .files_no = 1,
                .internal_name = "NV3_VBIOS_ERAZOR_V14700",
                .files = {NV3_VBIOS_ERAZOR_V14700, ""} 
           },
           { 
                .name = "ELSA VICTORY Erazor - Version 1.54.03", .files_no = 1,
                .internal_name = "NV3_VBIOS_ERAZOR_V15403",
                .files = {NV3_VBIOS_ERAZOR_V15403, ""} 
           },
           { 
                .name = "ELSA VICTORY Erazor - Version 1.55.00", .files_no = 1,
                .internal_name = "NV3_VBIOS_ERAZOR_V15500",
                .files = {NV3_VBIOS_ERAZOR_V15500, ""} 
           },
           {
                .name = "Diamond Viper V330 - Version 1.62-CO", .files_no = 1,
                .internal_name = "NV3_VBIOS_DIAMOND_V330_V162",
                .files = {NV3_VBIOS_DIAMOND_V330_V162, ""},
           },
           {
                .name = "ASUS AGP/3DP-V3000 - Version 1.51B", .files_no = 1,
                .internal_name = "NV3_VBIOS_ASUS_V3000_V151",
                .files = {NV3_VBIOS_ASUS_V3000_V151, ""},
           },
           {    
                .name = "STB Velocity 128 - Version 1.60 [BUGGY]", .files_no = 1,
                .internal_name = "NV3_VBIOS_STB_V128_V160",
                .files = {NV3_VBIOS_STB_V128_V160, ""},
            },
           {
                .name = "STB Velocity 128 - Version 1.82", .files_no = 1,
                .internal_name = "NV3_VBIOS_STB_V128_V182",
                .files = {NV3_VBIOS_STB_V128_V182, ""},
           },
        }
    },
    // Memory configuration
    {
        .name = "vram_size",
        .description = "VRAM Size",
        .type = CONFIG_SELECTION,
        .default_int = NV3_VRAM_SIZE_4MB,
        .selection = 
        {
            // I thought this was never released, but it seems that at least one was released:
            // The card was called the "NEC G7AGK"
            {
                .description = "2 MB",
                .value = NV3_VRAM_SIZE_2MB,
            },

            {
                .description = "4 MB",
                .value = NV3_VRAM_SIZE_4MB,
            },
        }

    },
    {
        .name = "chip_revision",
        .description = "Chip Revision",
        .type = CONFIG_SELECTION,
        .default_int = NV3_PCI_CFG_REVISION_B00,
        .selection = 
        {
            {
               .description = "RIVA 128 Prototype (Revision A; January 1997)",
               .value = NV3_PCI_CFG_REVISION_A00,
            },
            {
               .description = "RIVA 128 (Revision B)",
               .value = NV3_PCI_CFG_REVISION_B00,
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

const device_config_t nv3t_config[] =
{
    // VBIOS type configuration
    {
        .name = "vbios",
        .description = "Model",
        .type = CONFIG_BIOS,
        .default_string = "NV3T_VBIOS_DIAMOND_V330_V182B",
        .default_int = 0,
        .bios = 
        {
           {
            
                .name = "Diamond Multimedia Viper V330 8M BIOS - Version 1.82B", .files_no = 1,
                .internal_name = "NV3T_VBIOS_DIAMOND_V330_V182B",
                .files = {NV3T_VBIOS_DIAMOND_V330_V182B, ""},
           },
           {
                .name = "ASUS AGP-V3000 ZXTV BIOS - V1.70D.03", .files_no = 1,
                .internal_name = "NV3T_VBIOS_ASUS_V170",
                .files = {NV3T_VBIOS_ASUS_V170, ""},
           },
           {
                .name = "NVidia Reference BIOS - V1.71B-N", .files_no = 1,

                .internal_name = "NV3T_VBIOS_REFERENCE_CEK_V171",
                .files = {NV3T_VBIOS_REFERENCE_CEK_V171, ""},
           },
            
           {
                .name = "NVidia Reference BIOS - V1.72B", .files_no = 1,
                .internal_name = "NV3T_VBIOS_REFERENCE_CEK_V172",
                .files = {NV3T_VBIOS_REFERENCE_CEK_V172, ""},
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