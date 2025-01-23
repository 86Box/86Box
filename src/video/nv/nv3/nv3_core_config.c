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
#include <86Box/86box.h>
#include <86Box/device.h>
#include <86Box/mem.h>
#include <86box/io.h>
#include <86box/pci.h>
#include <86Box/rom.h> // DEPENDENT!!!
#include <86Box/video.h>
#include <86Box/nv/vid_nv.h>
#include <86Box/nv/vid_nv3.h>

const device_config_t nv3_config[] =
{
    // VBIOS type configuration
    {
        .name = "vbios",
        #ifndef RELEASE_BUILD
        .description = "VBIOS",
        #else
        .description = "Model",
        #endif 
        .type = CONFIG_BIOS,
        .default_string = "NV3_VBIOS_ERAZOR_V15403",
        .default_int = 0,
        .bios = 
        {
           { 
            #ifndef RELEASE_BUILD
                .name = "[NV3 - 1997-09-30] ELSA VICTORY Erazor VBE 3.0 DDC2B DPMS Video BIOS  Ver. 1.47.01 (ZZ/ A/00)", .files_no = 1,
            #else
                .name = "[RIVA 128] ELSA Victory Erazor v1.47.01", .files_no = 1,
            #endif
                .internal_name = "NV3_VBIOS_ERAZOR_V14700",
                .files = {NV3_VBIOS_ERAZOR_V14700, ""} 
           },
           { 
            #ifndef RELEASE_BUILD
                .name = "[NV3 - 1998-02-06]  ELSA VICTORY Erazor Ver. 1.54.03    [WD/VBE30/DDC2B/DPMS]", .files_no = 1,
            #else
                .name = "[RIVA 128] ELSA Victory Erazor v1.54.03", .files_no = 1,
            #endif
                .internal_name = "NV3_VBIOS_ERAZOR_V15403",
                .files = {NV3_VBIOS_ERAZOR_V15403, ""} 
           },
           { 
            #ifndef RELEASE_BUILD
                .name = "[NV3 - 1998-05-04] ELSA VICTORY Erazor Ver. 1.55.00    [WD/VBE30/DDC2B/DPMS]", .files_no = 1,
            #else
                .name = "[RIVA 128] ELSA Victory Erazor v1.55.00", .files_no = 1,
            #endif
                .internal_name = "NV3_VBIOS_ERAZOR_V15500",
                .files = {NV3_VBIOS_ERAZOR_V15500, ""} 
           },
           {
            #ifndef RELEASE_BUILD
                .name = "[NV3 - 1998-01-14] Diamond Multimedia Systems, Inc. Viper V330 Version 1.62-CO", .files_no = 1,
            #else
                .name = "[RIVA 128] Diamond Viper V330", .files_no = 1,
            #endif
                .internal_name = "NV3_VBIOS_DIAMOND_V330_V162",
                .files = {NV3_VBIOS_DIAMOND_V330_V162, ""},
           },
           {
            #ifndef RELEASE_BUILD
                .name = "[NV3 - 1997-09-06] ASUS AGP/3DP-V3000 BIOS 1.51B", .files_no = 1,
            #else
                .name = "[RIVA 128] ASUS AGP/3DP-V3000", .files_no = 1,
            #endif
                .internal_name = "NV3_VBIOS_ASUS_V3000_V151",
                .files = {NV3_VBIOS_ASUS_V3000_V151, ""},
           },
           {
            #ifndef RELEASE_BUILD
                .name = "[NV3 - 1997-12-17] STB Velocity 128 (RIVA 128) Ver.1.82", .files_no = 1,
            #else
                .name = "[RIVA 128] STB Velocity 128", .files_no = 1,
            #endif
                .internal_name = "NV3_VBIOS_STB_V128_V182",
                .files = {NV3_VBIOS_STB_V128_V182, ""},
           },
           {
            #ifndef RELEASE_BUILD
                .name = "[NV3T - 1998-09-15]  Diamond Multimedia Viper V330 8M BIOS - Version 1.82B", .files_no = 1,
            #else
                .name = "[RIVA 128 ZX] Diamond Multimedia Viper V330 8MB", .files_no = 1,
            #endif
                .internal_name = "NV3T_VBIOS_DIAMOND_V330_V182B",
                .files = {NV3T_VBIOS_DIAMOND_V330_V182B, ""},
           },
           {
            #ifndef RELEASE_BUILD
                .name = "[NV3T - 1998-08-04] ASUS AGP-V3000 ZXTV BIOS - V1.70D.03", .files_no = 1,
            #else
                .name = "[RIVA 128 ZX] ASUS AGP-V3000 ZXTV", .files_no = 1,
            #endif
                .internal_name = "NV3T_VBIOS_ASUS_V170",
                .files = {NV3T_VBIOS_ASUS_V170, ""},
           },
           {
            #ifndef RELEASE_BUILD
                .name = "[NV3T - 1998-07-30] RIVA 128 ZX BIOS - V1.71B-N", .files_no = 1,
            #else
                .name = "[RIVA 128 ZX] Nvidia Reference BIOS v1.71", .files_no = 1,
            #endif
                .internal_name = "NV3T_VBIOS_REFERENCE_CEK_V171",
                .files = {NV3T_VBIOS_REFERENCE_CEK_V171, ""},
           },
            
           {
            #ifndef RELEASE_BUILD
                .name = "[NV3T+SGRAM - 1998-08-15] RIVA 128 ZX BIOS - V1.72B", .files_no = 1,
            #else
                .name = "[RIVA 128 ZX] Nvidia Reference BIOS v1.72", .files_no = 1,
            #endif
                .internal_name = "NV3T_VBIOS_REFERENCE_CEK_V172",
                .files = {NV3T_VBIOS_REFERENCE_CEK_V172, ""},
           },
        }
    },
    // Memory configuration
    {
        .name = "vram_size",
        .description = "VRAM Size",
        .type = CONFIG_SELECTION,
        .default_int = VRAM_SIZE_4MB,
        .selection = 
        {
#ifndef RELEASE_BUILD
            // This never existed officially but was planned. Debug only
            {
                .description = "2 MB (Never officially sold)",
                .value = VRAM_SIZE_2MB,
            },
#endif
            {
                .description = "4 MB",
                .value = VRAM_SIZE_4MB,
            },
            {
                .description = "8 MB",
                .value = VRAM_SIZE_8MB,
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
#ifndef RELEASE_BUILD
            {
               .description = "NV3/STG3000 Engineering Sample / Stepping A0 (January 1997) with integrated PAUDIO sound card",
#else
               .description = "RIVA 128 Prototype (Revision A)",
#endif
               .value = NV3_PCI_CFG_REVISION_A00,
            },
#ifndef RELEASE_BUILD
            {
               .description = "RIVA 128 / Stepping B0 (October 1997)",
#else
               .description = "RIVA 128 (Revision B)",
#endif
               .value = NV3_PCI_CFG_REVISION_B00,
            },
#ifndef RELEASE_BUILD
            {
               .description = "NV3T - RIVA 128 ZX / Stepping C0 (March 1998)",
#else
               .description = "RIVA 128 ZX (Revision C)",
#endif
               .value = NV3_PCI_CFG_REVISION_C00,
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
        .type = CONFIG_END
    }
};