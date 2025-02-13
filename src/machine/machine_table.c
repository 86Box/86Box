/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Handling of the emulated machines.
 *
 * NOTES:   OpenAT wip for 286-class machine with open BIOS.
 *          PS2_M80-486 wip, pending receipt of TRM's for machine.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2017-2020 Fred N. van Kempen.
 *          Copyright 2025      Jasmine Iwanek.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/keyboard.h>
#include <86box/sound.h>
#include <86box/video.h>
#include <86box/plat_unused.h>
#include <86box/thread.h>
#include <86box/network.h>

// Temporarily here till we move everything out into the right files
extern const device_t pcjr_device;
extern const device_t m19_vid_device;
extern const device_t vid_device;
extern const device_t vid_device_hx;
extern const device_t t1000_video_device;
extern const device_t xi8088_device;
extern const device_t cga_device;
extern const device_t vid_1512_device;
extern const device_t vid_1640_device;
extern const device_t vid_pc2086_device;
extern const device_t vid_pc3086_device;
extern const device_t vid_200_device;
extern const device_t vid_ppc512_device;
extern const device_t vid_device_sl;
extern const device_t t1200_video_device;
extern const device_t compaq_plasma_device;
extern const device_t ps1_2011_device;
extern const device_t ibmpc_device;
extern const device_t ibmpc82_device;
extern const device_t ibmxt_device;
extern const device_t ibmxt86_device;
extern const device_t ibmat_device;
extern const device_t ibmxt286_device;
extern const device_t pb450_device;
extern const device_t jukopc_device;
extern const device_t vendex_device;

const machine_filter_t machine_types[] = {
    { "None",                             MACHINE_TYPE_NONE       },
    { "[1979] 8088",                      MACHINE_TYPE_8088       },
    { "[1978] 8086",                      MACHINE_TYPE_8086       },
    { "[1982] 80286",                     MACHINE_TYPE_286        },
    { "[1988] i386SX",                    MACHINE_TYPE_386SX      },
    { "[1992] 486SLC",                    MACHINE_TYPE_486SLC     },
    { "[1985] i386DX",                    MACHINE_TYPE_386DX      },
    { "[1989] i386DX/i486",               MACHINE_TYPE_386DX_486  },
    { "[1992] i486 (Socket 168 and 1)",   MACHINE_TYPE_486        },
    { "[1992] i486 (Socket 2)",           MACHINE_TYPE_486_S2     },
    { "[1994] i486 (Socket 3)",           MACHINE_TYPE_486_S3     },
    { "[1994] i486 (Socket 3 PCI)",       MACHINE_TYPE_486_S3_PCI },
    { "[1992] i486 (Miscellaneous)",      MACHINE_TYPE_486_MISC   },
    { "[1993] Socket 4",                  MACHINE_TYPE_SOCKET4    },
    { "[1994] Socket 5",                  MACHINE_TYPE_SOCKET5    },
    { "[1995] Socket 7 (Single Voltage)", MACHINE_TYPE_SOCKET7_3V },
    { "[1996] Socket 7 (Dual Voltage)",   MACHINE_TYPE_SOCKET7    },
    { "[1998] Super Socket 7",            MACHINE_TYPE_SOCKETS7   },
    { "[1995] Socket 8",                  MACHINE_TYPE_SOCKET8    },
    { "[1997] Slot 1",                    MACHINE_TYPE_SLOT1      },
    { "[1998] Slot 1/2",                  MACHINE_TYPE_SLOT1_2    },
    { "[1998] Slot 1/Socket 370",         MACHINE_TYPE_SLOT1_370  },
    { "[1998] Slot 2",                    MACHINE_TYPE_SLOT2      },
    { "[1998] Socket 370",                MACHINE_TYPE_SOCKET370  },
    { "Miscellaneous",                    MACHINE_TYPE_MISC       }
};

const machine_filter_t machine_chipsets[] = {
    { "None",                       MACHINE_CHIPSET_NONE                },
    { "Discrete",                   MACHINE_CHIPSET_DISCRETE            },
    { "Proprietary",                MACHINE_CHIPSET_PROPRIETARY         },
    { "Headland GC100A",            MACHINE_CHIPSET_GC100A              },
    { "Headland GC103",             MACHINE_CHIPSET_GC103               },
    { "Headland HT18",              MACHINE_CHIPSET_HT18                },
    { "ACC 2168",                   MACHINE_CHIPSET_ACC_2168            },
    { "ALi M1217",                  MACHINE_CHIPSET_ALI_M1217           },
    { "ALi M6117",                  MACHINE_CHIPSET_ALI_M6117           },
    { "ALi M1409",                  MACHINE_CHIPSET_ALI_M1409           },
    { "ALi M1429",                  MACHINE_CHIPSET_ALI_M1429           },
    { "ALi M1429G",                 MACHINE_CHIPSET_ALI_M1429G          },
    { "ALi M1489",                  MACHINE_CHIPSET_ALI_M1489           },
    { "ALi ALADDiN IV+",            MACHINE_CHIPSET_ALI_ALADDIN_IV_PLUS },
    { "ALi ALADDiN V",              MACHINE_CHIPSET_ALI_ALADDIN_V       },
    { "ALi ALADDiN-PRO II",         MACHINE_CHIPSET_ALI_ALADDIN_PRO_II  },
    { "C&T 82C235 SCAT",            MACHINE_CHIPSET_SCAT                },
    { "C&T CS8121 NEAT",            MACHINE_CHIPSET_NEAT                },
    { "C&T 386",                    MACHINE_CHIPSET_CT_386              },
    { "C&T CS4031",                 MACHINE_CHIPSET_CT_CS4031           },
    { "Contaq 82C596",              MACHINE_CHIPSET_CONTAQ_82C596       },
    { "Contaq 82C597",              MACHINE_CHIPSET_CONTAQ_82C597       },
    { "IMS 8848",                   MACHINE_CHIPSET_IMS_8848            },
    { "Intel 82335",                MACHINE_CHIPSET_INTEL_82335         },
    { "Intel 420TX",                MACHINE_CHIPSET_INTEL_420TX         },
    { "Intel 420ZX",                MACHINE_CHIPSET_INTEL_420ZX         },
    { "Intel 420EX",                MACHINE_CHIPSET_INTEL_420EX         },
    { "Intel 430LX",                MACHINE_CHIPSET_INTEL_430LX         },
    { "Intel 430NX",                MACHINE_CHIPSET_INTEL_430NX         },
    { "Intel 430FX",                MACHINE_CHIPSET_INTEL_430FX         },
    { "Intel 430HX",                MACHINE_CHIPSET_INTEL_430HX         },
    { "Intel 430VX",                MACHINE_CHIPSET_INTEL_430VX         },
    { "Intel 430TX",                MACHINE_CHIPSET_INTEL_430TX         },
    { "Intel 450KX",                MACHINE_CHIPSET_INTEL_450KX         },
    { "Intel 440FX",                MACHINE_CHIPSET_INTEL_440FX         },
    { "Intel 440LX",                MACHINE_CHIPSET_INTEL_440LX         },
    { "Intel 440EX",                MACHINE_CHIPSET_INTEL_440EX         },
    { "Intel 440BX",                MACHINE_CHIPSET_INTEL_440BX         },
    { "Intel 440ZX",                MACHINE_CHIPSET_INTEL_440ZX         },
    { "Intel 440GX",                MACHINE_CHIPSET_INTEL_440GX         },
    { "OPTi 283",                   MACHINE_CHIPSET_OPTI_283            },
    { "OPTi 291",                   MACHINE_CHIPSET_OPTI_291            },
    { "OPTi 381",                   MACHINE_CHIPSET_OPTI_381            },
    { "OPTi 391",                   MACHINE_CHIPSET_OPTI_391            },
    { "OPTi 481",                   MACHINE_CHIPSET_OPTI_481            },
    { "OPTi 493",                   MACHINE_CHIPSET_OPTI_493            },
    { "OPTi 495",                   MACHINE_CHIPSET_OPTI_495            },
    { "OPTi 499",                   MACHINE_CHIPSET_OPTI_499            },
    { "OPTi 895/802G",              MACHINE_CHIPSET_OPTI_895_802G       },
    { "OPTi 547/597",               MACHINE_CHIPSET_OPTI_547_597        },
    { "SARC RC2016A",               MACHINE_CHIPSET_SARC_RC2016A        },
    { "SiS 310",                    MACHINE_CHIPSET_SIS_310             },
    { "SiS 401",                    MACHINE_CHIPSET_SIS_401             },
    { "SiS 460",                    MACHINE_CHIPSET_SIS_460             },
    { "SiS 461",                    MACHINE_CHIPSET_SIS_461             },
    { "SiS 471",                    MACHINE_CHIPSET_SIS_471             },
    { "SiS 496",                    MACHINE_CHIPSET_SIS_496             },
    { "SiS 501",                    MACHINE_CHIPSET_SIS_501             },
    { "SiS 5501",                   MACHINE_CHIPSET_SIS_5501            },
    { "SiS 5511",                   MACHINE_CHIPSET_SIS_5511            },
    { "SiS 5571",                   MACHINE_CHIPSET_SIS_5571            },
    { "SiS 5581",                   MACHINE_CHIPSET_SIS_5581            },
    { "SiS 5591",                   MACHINE_CHIPSET_SIS_5591            },
    { "SiS (5)600",                 MACHINE_CHIPSET_SIS_5600            },
    { "SMSC VictoryBX-66",          MACHINE_CHIPSET_SMSC_VICTORYBX_66   },
    { "STPC Client",                MACHINE_CHIPSET_STPC_CLIENT         },
    { "STPC Consumer-II",           MACHINE_CHIPSET_STPC_CONSUMER_II    },
    { "STPC Elite",                 MACHINE_CHIPSET_STPC_ELITE          },
    { "STPC Atlas",                 MACHINE_CHIPSET_STPC_ATLAS          },
    { "Symphony SL82C460 Haydn II", MACHINE_CHIPSET_SYMPHONY_SL82C460   },
    { "UMC UM82C480",               MACHINE_CHIPSET_UMC_UM82C480        },
    { "UMC UM82C491",               MACHINE_CHIPSET_UMC_UM82C491        },
    { "UMC UM8881",                 MACHINE_CHIPSET_UMC_UM8881          },
    { "UMC UM8890BF",               MACHINE_CHIPSET_UMC_UM8890BF        },
    { "VIA VT82C495",               MACHINE_CHIPSET_VIA_VT82C495        },
    { "VIA VT82C496G",              MACHINE_CHIPSET_VIA_VT82C496G       },
    { "VIA Apollo VPX",             MACHINE_CHIPSET_VIA_APOLLO_VPX      },
    { "VIA Apollo VP3",             MACHINE_CHIPSET_VIA_APOLLO_VP3      },
    { "VIA Apollo MVP3",            MACHINE_CHIPSET_VIA_APOLLO_MVP3     },
    { "VIA Apollo Pro",             MACHINE_CHIPSET_VIA_APOLLO_PRO      },
    { "VIA Apollo Pro 133",         MACHINE_CHIPSET_VIA_APOLLO_PRO_133  },
    { "VIA Apollo Pro 133A",        MACHINE_CHIPSET_VIA_APOLLO_PRO_133A },
    { "VLSI SCAMP",                 MACHINE_CHIPSET_VLSI_SCAMP          },
    { "VLSI VL82C480",              MACHINE_CHIPSET_VLSI_VL82C480       },
    { "VLSI VL82C481",              MACHINE_CHIPSET_VLSI_VL82C481       },
    { "VLSI VL82C486",              MACHINE_CHIPSET_VLSI_VL82C486       },
    { "WD76C10",                    MACHINE_CHIPSET_WD76C10             }
};

/* Machines to add before machine freeze:
   - TMC Mycomp PCI54ST;
   - Zeos Quadtel 486.

   NOTE: The AMI MegaKey tests were done on a real Intel Advanced/ATX
     (thanks, MrKsoft for running my AMIKEY.COM on it), but the
     technical specifications of the other Intel machines confirm
     that the other boards also have the MegaKey.

   NOTE: The later (ie. not AMI Color) Intel AMI BIOS'es execute a
     sequence of commands (B8, BA, BB) during one of the very first
     phases of POST, in a way that is only valid on the AMIKey-3
     KBC firmware, that includes the Classic PCI/ED (Ninja) BIOS
     which otherwise does not execute any AMI KBC commands, which
     indicates that the sequence is a leftover of whatever AMI
     BIOS (likely a laptop one since the AMIKey-3 is a laptop KBC
     firmware!) Intel forked.

   NOTE: The VIA VT82C42N returns 0x46 ('F') in command 0xA1 (so it
     emulates the AMI KF/AMIKey KBC firmware), and 0x42 ('B') in
     command 0xAF.
     The version on the VIA VT82C686B southbridge also returns
     'F' in command 0xA1, but 0x45 ('E') in command 0xAF.
     The version on the VIA VT82C586B southbridge also returns
     'F' in command 0xA1, but 0x44 ('D') in command 0xAF.
     The version on the VIA VT82C586A southbridge also returns
     'F' in command 0xA1, but 0x43 ('C') in command 0xAF.

   NOTE: The AMI MegaKey commands blanked in the technical reference
     are CC and and C4, which are Set P14 High and Set P14 Low,
     respectively. Also, AMI KBC command C1, mysteriously missing
     from the technical references of AMI MegaKey and earlier, is
     Write Input Port, same as on AMIKey-3.
*/

const machine_t machines[] = {
  // clang-format off
    /* 8088 Machines */
    {
        .name = "[8088] IBM PC (1981)",
        .internal_name = "ibmpc",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_pc_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC5150,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 16,
            .max = 64,
            .step = 16
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_pc_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = &ibmpc_device,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] IBM PC (1982)",
        .internal_name = "ibmpc82",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_pc82_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC5150,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 64,
            .max = 256,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_pc82_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = &ibmpc82_device,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] IBM PCjr",
        .internal_name = "ibmpcjr",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_pcjr_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 4772728,
            .max_bus = 4772728,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PCJR,
        .flags = MACHINE_VIDEO_FIXED,
        .ram = {
            .min = 64,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = NULL, /* TODO: No specific kbd_device yet */
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &pcjr_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] IBM XT (1982)",
        .internal_name = "ibmxt",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 64,
            .max = 256,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xt_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = &ibmxt_device,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] IBM XT (1986)",
        .internal_name = "ibmxt86",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt86_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 256,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xt86_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = &ibmxt86_device,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] American XT Computer",
        .internal_name = "americxt",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_americxt_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 64,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] AMI XT clone",
        .internal_name = "amixt",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_amixt_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 64,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Atari PC 3",
        .internal_name = "ataripc3",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_ataripc3_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FDC,
        .ram = {
            .min = 64,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL, //&fdc_xt_device,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Bondwell BW230",
        .internal_name = "bw230",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_bw230_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 64,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Columbia Data Products MPC-1600",
        .internal_name = "mpc1600",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_mpc1600_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 128,
            .max = 512,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_pc82_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Compaq Portable",
        .internal_name = "portable",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_compaq_portable_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 128,
            .max = 640,
            .step = 128
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xt_compaq_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] DTK PIM-TB10-Z",
        .internal_name = "dtk",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_dtk_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 64,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Eagle PC Spirit",
        .internal_name = "pcspirit",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_pcspirit_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 128,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_pc82_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Generic XT clone",
        .internal_name = "genxt",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_genxt_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 64,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xt_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] GLaBIOS",
        .internal_name = "glabios",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_glabios_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 64,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xt_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Hyosung Topstar 88T",
        .internal_name = "top88",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_top88_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 128,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Hyundai SUPER-16T",
        .internal_name = "super16t",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_super16t_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 4772728,
            .max_bus = 8000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 128,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Hyundai SUPER-16TE",
        .internal_name = "super16te",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_super16te_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 10000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 128,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Juko ST",
        .internal_name = "jukopc",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_jukopc_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 64,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = &jukopc_device,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Kaypro PC",
        .internal_name = "kaypropc",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_kaypropc_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 128,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Micoms XL-7 Turbo",
        .internal_name = "mxl7t",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_micoms_xl7turbo_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 64,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xt_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Multitech PC-500",
        .internal_name = "pc500",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_pc500_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 128,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_pc_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Multitech PC-700",
        .internal_name = "pc700",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_pc700_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 128,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_pc_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] NCR PC4i",
        .internal_name = "pc4i",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_pc4i_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 256,
            .max = 640,
            .step = 128
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Olivetti M19",
        .internal_name = "m19",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_xt_m19_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 4772728,
            .max_bus = 7159092,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_VIDEO_FIXED,
        .ram = {
            .min = 256,
            .max = 640,
            .step = 128
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xt_olivetti_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &m19_vid_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] OpenXT",
        .internal_name = "openxt",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_openxt_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 64,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Packard Bell PB8810",
        .internal_name = "pb8810",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_pb8810_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 256,
            .max = 640,
            .step = 128
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Philips P3105/NMS9100",
        .internal_name = "p3105",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_p3105_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_XTA,
        .ram = {
            .min = 256,
            .max = 768,
            .step = 256
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_pc_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Phoenix XT clone",
        .internal_name = "pxxt",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_pxxt_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 64,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Pravetz 16 / IMKO-4",
        .internal_name = "pravetz16",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_pravetz16_imko4_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 64,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_pravetz_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Pravetz 16S / CPU12 Plus",
        .internal_name = "pravetz16s",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_pravetz16s_cpu12p_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 4772728,
            .max_bus = 12000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 512,
            .max = 1024,
            .step = 128
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xt_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Sanyo SX-16",
        .internal_name = "sansx16",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_sansx16_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 256,
            .max = 640,
            .step = 128
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Schneider EuroPC",
        .internal_name = "europc",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_europc_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088_EUROPC,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_XTA | MACHINE_MOUSE,
        .ram = {
            .min = 512,
            .max = 640,
            .step = 128
        },
        .nvrmask = 15,
        .kbc_device = &keyboard_xt_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Super PC/Turbo XT",
        .internal_name = "pcxt",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_pcxt_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 64,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Tandy 1000 SX",
        .internal_name = "tandy",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_tandy1000sx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_VIDEO_FIXED,
        .ram = {
            .min = 384,
            .max = 640,
            .step = 128
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_tandy_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &vid_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Tandy 1000 HX",
        .internal_name = "tandy1000hx",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_tandy1000hx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_VIDEO_FIXED,
        .ram = {
            .min = 256,
            .max = 640,
            .step = 128
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_tandy_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &vid_device_hx,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Toshiba T1000",
        .internal_name = "t1000",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_xt_t1000_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_VIDEO,
        .ram = {
            .min = 512,
            .max = 1280,
            .step = 768
        },
        .nvrmask = 63,
        .kbc_device = &keyboard_xt_t1x00_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &t1000_video_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Vendex HeadStart Turbo 888-XT",
        .internal_name = "vendex",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_xt_vendex_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 256,
            .max = 768,
            .step = 256
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = &vendex_device,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
#ifdef USE_LASERXT
    {
        .name = "[8088] VTech Laser Turbo XT",
        .internal_name = "ltxt",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_laserxt_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 256,
            .max = 640,
            .step = 256
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xt_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
#endif /* USE_LASERXT */
    /* Has a standard PS/2 KBC (so, use IBM PS/2 Type 1). */
    {
        .name = "[8088] Xi8088",
        .internal_name = "xi8088",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_xi8088_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 64,
            .max = 1024,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = &keyboard_ps2_xi8088_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = &xi8088_device,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Z-NIX PC-1600",
        .internal_name = "znic",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_znic_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 64,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Zenith Data Systems Z-151/152/161",
        .internal_name = "zdsz151",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_z151_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 128,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xt_zenith_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Zenith Data Systems Z-159",
        .internal_name = "zdsz159",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_z159_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 128,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xt_zenith_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8088] Zenith Data Systems SupersPort (Z-184)",
        .internal_name = "zdsupers",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_z184_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_VIDEO_FIXED,
        .ram = {
            .min = 128,
            .max = 640,
            .step = 128
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xt_zenith_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &cga_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[GC100A] Philips P3120",
        .internal_name = "p3120",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_GC100A,
        .init = machine_xt_p3120_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_XTA,
        .ram = {
            .min = 256,
            .max = 768,
            .step = 256
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_pc_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[V20] PC-XT",
        .internal_name = "v20xt",
        .type = MACHINE_TYPE_8088,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_v20xt_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8088,
            .block = CPU_BLOCK(CPU_8088),
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 64,
            .max = 640,
            .step = 64
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 8086 Machines */
    {
        .name = "[8086] Amstrad PC1512",
        .internal_name = "pc1512",
        .type = MACHINE_TYPE_8086,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_pc1512_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8086,
            .block = CPU_BLOCK_NONE,
            .min_bus = 8000000,
            .max_bus = 8000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_VIDEO_FIXED | MACHINE_MOUSE,
        .ram = {
            .min = 512,
            .max = 640,
            .step = 128
        },
        .nvrmask = 63,
        .kbc_device = NULL /* TODO: No specific kbd_device yet */,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &vid_1512_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8086] Amstrad PC1640",
        .internal_name = "pc1640",
        .type = MACHINE_TYPE_8086,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_pc1640_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8086,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 10000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_VIDEO | MACHINE_MOUSE,
        .ram = {
            .min = 640,
            .max = 640,
            .step = 640
        },
        .nvrmask = 63,
        .kbc_device = NULL /* TODO: No specific kbd_device yet */,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &vid_1640_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8086] Amstrad PC2086",
        .internal_name = "pc2086",
        .type = MACHINE_TYPE_8086,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_pc2086_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8086,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 10000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_VIDEO_FIXED | MACHINE_MOUSE,
        .ram = {
            .min = 640,
            .max = 640,
            .step = 640
        },
        .nvrmask = 63,
        .kbc_device = NULL /* TODO: No specific kbd_device yet */,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &vid_pc2086_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8086] Amstrad PC3086",
        .internal_name = "pc3086",
        .type = MACHINE_TYPE_8086,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_pc3086_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8086,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 10000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_VIDEO_FIXED | MACHINE_MOUSE,
        .ram = {
            .min = 640,
            .max = 640,
            .step = 640
        },
        .nvrmask = 63,
        .kbc_device = NULL /* TODO: No specific kbd_device yet */,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &vid_pc3086_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8086] Amstrad PC20(0)",
        .internal_name = "pc200",
        .type = MACHINE_TYPE_8086,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_pc200_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8086,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 10000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_VIDEO | MACHINE_MOUSE,
        .ram = {
            .min = 512,
            .max = 640,
            .step = 128
        },
        .nvrmask = 63,
        .kbc_device = NULL /* TODO: No specific kbd_device yet */,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &vid_200_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8086] Amstrad PPC512/640",
        .internal_name = "ppc512",
        .type = MACHINE_TYPE_8086,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_ppc512_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8086,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 10000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_VIDEO | MACHINE_MOUSE,
        .ram = {
            .min = 512,
            .max = 640,
            .step = 128
        },
        .nvrmask = 63,
        .kbc_device = NULL /* TODO: No specific kbd_device yet */,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &vid_ppc512_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8086] Compaq Deskpro",
        .internal_name = "deskpro",
        .type = MACHINE_TYPE_8086,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_xt_compaq_deskpro_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8086,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 128,
            .max = 640,
            .step = 128
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xt_compaq_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8086] Epson Equity LT",
        .internal_name = "elt",
        .type = MACHINE_TYPE_8086,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_elt_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8086,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_VIDEO,
        .ram = {
            .min = 640,
            .max = 640,
            .step = 640
        },
        .nvrmask = 0x3f,
        .kbc_device = &keyboard_xt_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8086] Mazovia 1016",
        .internal_name = "maz1016",
        .type = MACHINE_TYPE_8086,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_maz1016_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8086_MAZOVIA,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 256,
            .max = 640,
            .step = 384
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8086] Olivetti M21/24/24SP",
        .internal_name = "m24",
        .type = MACHINE_TYPE_8086,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_xt_m24_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8086,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_VIDEO | MACHINE_MOUSE | MACHINE_MFM,
        .ram = {
            .min = 128,
            .max = 640,
            .step = 128
        },
        .nvrmask = 15,
        .kbc_device = NULL /* TODO: No specific kbd_device yet */,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &ogc_m24_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has Olivetti KBC firmware. */
    {
        .name = "[8086] Olivetti M240",
        .internal_name = "m240",
        .type = MACHINE_TYPE_8086,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_xt_m240_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8086,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_MFM,
        .ram = {
            .min = 128,
            .max = 640,
            .step = 128
        },
        .nvrmask = 15,
        .kbc_device = NULL /* TODO: No specific kbd_device yet */,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8086] Schetmash Iskra-3104",
        .internal_name = "iskra3104",
        .type = MACHINE_TYPE_8086,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_iskra3104_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8086,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 128,
            .max = 640,
            .step = 128
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xtclone_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8086] Tandy 1000 SL/2",
        .internal_name = "tandy1000sl2",
        .type = MACHINE_TYPE_8086,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_tandy1000sl2_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8086,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_VIDEO_FIXED,
        .ram = {
            .min = 512,
            .max = 768,
            .step = 128
        },
        .nvrmask = 0,
        .kbc_device = NULL /* TODO: No specific kbd_device yet */,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &vid_device_sl,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8086] Toshiba T1200",
        .internal_name = "t1200",
        .type = MACHINE_TYPE_8086,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_xt_t1200_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8086,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_VIDEO | MACHINE_MFM,
        .ram = {
            .min = 1024,
            .max = 2048,
            .step = 1024
        },
        .nvrmask = 63,
        .kbc_device = &keyboard_xt_t1x00_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &t1200_video_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[8086] Victor V86P",
        .internal_name = "v86p",
        .type = MACHINE_TYPE_8086,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_v86p_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8086,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_VIDEO | MACHINE_MFM,
        .ram = {
            .min = 512,
            .max = 1024,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = &keyboard_xt_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

#ifdef USE_LASERXT
    {
        .name = "[8086] VTech Laser XT3",
        .internal_name = "lxt3",
        .type = MACHINE_TYPE_8086,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_xt_lxt3_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_8086,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PC,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 256,
            .max = 640,
            .step = 256
        },
        .nvrmask = 0,
        .kbc_device = &keyboard_xt_lxt3_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
#endif /* USE_LASERXT */

    /* 286 AT machines */
    /* Has IBM AT KBC firmware. */
    {
        .name = "[ISA] IBM AT",
        .internal_name = "ibmat",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_at_ibm_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 6000000,
            .max_bus = 8000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 256,
            .max = 512,
            .step = 256
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = &ibmat_device,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[ISA] IBM PS/1 model 2011",
        .internal_name = "ibmps1es",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_ps1_m2011_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 10000000,
            .max_bus = 10000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_XTA | MACHINE_VIDEO_FIXED,
        .ram = {
            .min = 512,
            .max = 15360,
            .step = 512
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = &ps1_2011_device,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[ISA] IBM PS/2 model 30-286",
        .internal_name = "ibmps2_m30_286",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_ps2_m30_286_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286 | CPU_PKG_486SLC_IBM,
            .block = CPU_BLOCK_NONE,
            .min_bus = 10000000,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_XTA | MACHINE_VIDEO_FIXED,
        .ram = {
            .min = 1024,
            .max = 16384,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM AT KBC firmware. */
    {
        .name = "[ISA] IBM XT Model 286",
        .internal_name = "ibmxt286",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_at_ibmxt286_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 6000000,
            .max_bus = 6000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 256,
            .max = 640,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = &ibmxt286_device,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* AMI BIOS for a chipset-less machine, most likely has AMI 'F' KBC firmware. */
    {
        .name = "[ISA] AMI IBM AT",
        .internal_name = "ibmatami",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_at_ibmatami_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 256,
            .max = 512,
            .step = 256
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Uses Commodore (CBM) KBC firmware, to be implemented as identical to the
       IBM AT KBC firmware unless evidence emerges of any proprietary commands. */
    {
        .name = "[ISA] Commodore PC 30 III",
        .internal_name = "cmdpc30",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_at_cmdpc_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 6000000,
            .max_bus = 12500000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 640,
            .max = 14912,
            .step = 64
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Uses Compaq KBC firmware. */
    {
        .name = "[ISA] Compaq Portable II",
        .internal_name = "portableii",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_at_portableii_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 6000000,
            .max_bus = 16000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 640,
            .max = 16384,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Uses Compaq KBC firmware. */
    {
        .name = "[ISA] Compaq Portable III",
        .internal_name = "portableiii",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_at_portableiii_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 6000000,
            .max_bus = 16000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_IDE | MACHINE_VIDEO,
        .ram = {
            .min = 640,
            .max = 16384,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &compaq_plasma_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM AT KBC firmware. */
    {
        .name = "[ISA] MR BIOS 286 clone",
        .internal_name = "mr286",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_at_mr286_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_IDE,
        .ram = {
            .min = 512,
            .max = 16384,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM AT KBC firmware. */
    {
        .name = "[ISA] NCR PC8/810/710/3390/3392",
        .internal_name = "pc8",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_at_pc8_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 512,
            .max = 16384,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has Chips & Technologies KBC firmware. */
    {
        .name = "[ISA] Wells American A*Star",
        .internal_name = "wellamerastar",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_at_wellamerastar_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 6000000,
            .max_bus = 14000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 512,
            .max = 1024,
            .step = 512
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
#ifdef USE_OLIVETTI
    /* Has Olivetti KBC firmware. */
    {
        .name = "[ISA] Olivetti M290",
        .internal_name = "m290",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_at_m290_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 640,
            .max = 16384,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
#endif /* USE_OLIVETTI */
#ifdef USE_OPEN_AT
    /* Has IBM AT KBC firmware. */
    {
        .name = "[ISA] OpenAT",
        .internal_name = "openat",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_at_openat_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 256,
            .max = 15872,
            .step = 128
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
#endif /* USE_OPEN_AT */
    /* Has IBM AT KBC firmware. */
    {
        .name = "[ISA] Phoenix IBM AT",
        .internal_name = "ibmatpx",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_at_ibmatpx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 256,
            .max = 512,
            .step = 256
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has Quadtel KBC firmware. */
    {
        .name = "[ISA] Quadtel IBM AT",
        .internal_name = "ibmatquadtel",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_at_ibmatquadtel_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 256,
            .max = 512,
            .step = 256
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has a Siemens proprietary KBC which is completely undocumented. */
    {
        .name = "[ISA] Siemens PCD-2L",
        .internal_name = "siemens",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_at_siemens_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 6000000,
            .max_bus = 12500000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 256,
            .max = 15872,
            .step = 128
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has Toshiba's proprietary KBC, which is already implemented. */
    {
        .name = "[ISA] Toshiba T3100e",
        .internal_name = "t3100e",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_at_t3100e_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_IDE | MACHINE_VIDEO_FIXED,
        .ram = {
            .min = 1024,
            .max = 5120,
            .step = 256
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[ISA] GRiD GRiDcase 1520",
        .internal_name = "grid1520",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_at_grid1520_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 6000000,
            .max_bus = 10000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_IDE /*| MACHINE_VIDEO_FIXED*/,
        .ram = {
            .min = 1024,
            .max = 8192,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has Quadtel KBC firmware. */
    {
        .name = "[GC103] Quadtel 286 clone",
        .internal_name = "quadt286",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_GC103,
        .init = machine_at_quadt286_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_SOFTFLOAT_ONLY,
        .ram = {
            .min = 512,
            .max = 16384,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Most likely has AMI 'F' KBC firmware. */
    {
        .name = "[GC103] TriGem 286M",
        .internal_name = "tg286m",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_GC103,
        .init = machine_at_tg286m_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_IDE,
        .ram = {
            .min = 512,
            .max = 8192,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[NEAT] Atari PC 4",
        .internal_name = "ataripc4",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_NEAT,
        .init = machine_at_ataripc4_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FDC,
        .ram = {
            .min = 512,
            .max = 8192,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = &keyboard_at_ami_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL, //&fdc_at_device,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has "AMI KEYBOARD BIOS", most likely 'F'. */
    {
        .name = "[NEAT] DataExpert 286",
        .internal_name = "ami286",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_NEAT,
        .init = machine_at_neat_ami_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 512,
            .max = 8192,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* has an Award-branded KBC controller */
    {
        .name = "[NEAT] Hyundai Super-286C",
        .internal_name = "super286c",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_NEAT,
        .init = machine_at_super286c_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 512,
            .max = 1024,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM AT KBC firmware. */
    {
        .name = "[NEAT] NCR 3302",
        .internal_name = "3302",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_NEAT,
        .init = machine_at_3302_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_IDE | MACHINE_VIDEO,
        .ram = {
            .min = 512,
            .max = 5120,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM AT KBC firmware. */
    {
        .name = "[NEAT] Arche AMA-2010",
        .internal_name = "px286",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_NEAT,
        .init = machine_at_px286_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 512,
            .max = 8192,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has Chips & Technologies KBC firmware. */
    {
        .name = "[SCAT] GW-286CT GEAR",
        .internal_name = "gw286ct",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_SCAT,
        .init = machine_at_gw286ct_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_IDE,
        .ram = {
            .min = 512,
            .max = 16384,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[SCAT] Goldstar GDC-212M",
        .internal_name = "gdc212m",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_SCAT,
        .init = machine_at_gdc212m_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_IDE,
        .ram = {
            .min = 512,
            .max = 4096,
            .step = 512
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a VIA VT82C42N KBC. */
    {
        .name = "[SCAT] Hyundai Solomon 286KP",
        .internal_name = "award286",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_SCAT,
        .init = machine_at_award286_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_IDE,
        .ram = {
            .min = 512,
            .max = 8192,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a VIA VT82C42N KBC. */
    {
        .name = "[SCAT] Hyundai Super-286TR",
        .internal_name = "super286tr",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_SCAT,
        .init = machine_at_super286tr_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 512,
            .max = 8192,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[SCAT] Samsung SPC-4200P",
        .internal_name = "spc4200p",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_SCAT,
        .init = machine_at_spc4200p_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_IDE, /* Has internal video: C&T VGA 411 */
        .ram = {
            .min = 512,
            .max = 2048,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[SCAT] Samsung SPC-4216P",
        .internal_name = "spc4216p",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_SCAT,
        .init = machine_at_spc4216p_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 1024,
            .max = 5120,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[SCAT] Samsung SPC-4620P",
        .internal_name = "spc4620p",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_SCAT,
        .init = machine_at_spc4620p_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_IDE | MACHINE_VIDEO,
        .ram = {
            .min = 1024,
            .max = 5120,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM AT KBC firmware. */
    {
        .name = "[SCAT] Samsung Deskmaster 286",
        .internal_name = "deskmaster286",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_SCAT,
        .init = machine_at_deskmaster286_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_IDE, /* Has internal video: C&T VGA 411 */
        .ram = {
            .min = 512,
            .max = 8192,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    {
        .name = "[SCAT] Senor Science Co. SCAT-286-003",
        .internal_name = "senorscat286",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_SCAT,
        .init = machine_at_senor_scat286_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_IDE,
        .ram = {
            .min = 1024,
            .max = 4096,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* 286 machines that utilize the MCA bus */
    /* Has IBM PS/2 Type 2 KBC firmware. */
    {
        .name = "[MCA] IBM PS/2 model 50",
        .internal_name = "ibmps2_m50",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_ps2_model_50_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286 | CPU_PKG_486SLC_IBM,
            .block = CPU_BLOCK_NONE,
            .min_bus = 10000000,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_MCA,
        .flags = MACHINE_VIDEO,
        .ram = {
            .min = 1024,
            .max = 10240,
            .step = 1024
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM PS/2 Type 2 KBC firmware. */
    {
        .name = "[MCA] IBM PS/2 model 60",
        .internal_name = "ibmps2_m60",
        .type = MACHINE_TYPE_286,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_ps2_model_60_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_286 | CPU_PKG_486SLC_IBM,
            .block = CPU_BLOCK_NONE,
            .min_bus = 10000000,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_MCA,
        .flags = MACHINE_VIDEO,
        .ram = {
            .min = 1024,
            .max = 10240,
            .step = 1024
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 386SX machines */
    /* ISA slots available because an official IBM expansion for that existed. */
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[ISA] IBM PS/1 model 2121",
        .internal_name = "ibmps1_2121",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_ps1_m2121_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_IDE | MACHINE_VIDEO,
        .ram = {
            .min = 2048,
            .max = 6144,
            .step = 1024
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM AT KBC firmware. */
    {
        .name = "[ISA] NCR PC916SX",
        .internal_name = "pc916sx",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_at_pc916sx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 1024,
            .max = 16384,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has Quadtel KBC firmware. */
    {
        .name = "[ISA] QTC-SXM KT X20T02/HI",
        .internal_name = "quadt386sx",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_at_quadt386sx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 1024,
            .max = 16384,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    {
        .name = "[ALi M1217] Acrosser AR-B1374",
        .internal_name = "arb1374",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_ALI_M1217,
        .init = machine_at_arb1374_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_IDE,
        .ram = {
            .min = 1024,
            .max = 32768,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has the AMIKey-2 KBC. */
    {
        .name = "[ALi M1217] AAEON SBC-350A",
        .internal_name = "sbc350a",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_ALI_M1217,
        .init = machine_at_sbc350a_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_IDE,
        .ram = {
            .min = 1024,
            .max = 16384,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a VIA VT82C42N KBC. */
    {
        .name = "[ALi M1217] Flytech A36",
        .internal_name = "flytech386",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_ALI_M1217,
        .init = machine_at_flytech386_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_IDE | MACHINE_VIDEO,
        .ram = {
            .min = 1024,
            .max = 16384,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &tvga8900d_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a JetKey KBC without version, shows up as a 'H'. */
    {
        .name = "[ALi M1217] Chaintech 325AX",
        .internal_name = "325ax",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_ALI_M1217,
        .init = machine_at_325ax_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 1024,
            .max = 16384,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a JetKey KBC without version, shows up as a 'H'. */
    {
        .name = "[ALi M1217] Chaintech 325AX (MR BIOS)",
        .internal_name = "mr1217",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_ALI_M1217,
        .init = machine_at_mr1217_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 1024,
            .max = 16384,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
            {
        .name = "[ALI M1409] Acer 100T",
        .internal_name = "acer100t",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_ALI_M1409,
        .init = machine_at_acer100t_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 16000000,
            .max_bus = 25000000, /* Limited to 25 due a inaccurate cpu speed */
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0,

        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_IDE | MACHINE_VIDEO , /* Machine has internal OTI 077 Video card*/
        .ram = {
            .min = 2048,
            .max = 16256,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &oti077_acer100t_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[ALi M6117] Acrosser PJ-A511M",
        .internal_name = "pja511m",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_ALI_M6117,
        .init = machine_at_pja511m_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_M6117,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_IDE,
        .ram = {
            .min = 1024,
            .max = 32768,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[ALi M6117] Protech ProX-1332",
        .internal_name = "prox1332",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_ALI_M6117,
        .init = machine_at_prox1332_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_M6117,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_IDE,
        .ram = {
            .min = 1024,
            .max = 32768,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has an AMI KBC firmware, the only photo of this is too low resolution
       for me to read what's on the KBC chip, so I'm going to assume AMI 'F'
       based on the other known HT18 AMI BIOS strings. */
    {
        .name = "[HT18] Arche AMA-932J",
        .internal_name = "ama932j",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_HT18,
        .init = machine_at_ama932j_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_IDE | MACHINE_VIDEO,
        .ram = {
            .min = 512,
            .max = 8192,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &oti067_ama932j_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has an unknown KBC firmware with commands B8 and BB in the style of
       Phoenix MultiKey and AMIKey-3(!), but also commands E1 and EA with
       unknown functions. */
    {
        .name = "[Intel 82335] ADI 386SX",
        .internal_name = "adi386sx",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_INTEL_82335,
        .init = machine_at_adi386sx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 512,
            .max = 8192,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has an AMI Keyboard BIOS PLUS KBC firmware ('8'). */
    { .name = "[Intel 82335] Shuttle 386SX",
        .internal_name = "shuttle386sx",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_INTEL_82335,
        .init = machine_at_shuttle386sx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 512,
            .max = 8192,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Uses Commodore (CBM) KBC firmware, to be implemented as identical to
       the IBM PS/2 Type 1 KBC firmware unless evidence emerges of any
       proprietary commands. */
    {
        .name = "[NEAT] Commodore SL386SX-16",
        .internal_name = "cmdsl386sx16",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_NEAT,
        .init = machine_at_cmdsl386sx16_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_IDE,
        .ram = {
            .min = 1024,
            .max = 8192,
            .step = 512
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM AT KBC firmware. */
    {
        .name = "[NEAT] DTK PM-1630C",
        .internal_name = "dtk386",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_NEAT,
        .init = machine_at_neat_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 512,
            .max = 8192,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM AT KBC firmware. */
    {
        .name = "[OPTi 291] DTK PPM-3333P",
        .internal_name = "awardsx",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_OPTI_291,
        .init = machine_at_awardsx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 1024,
            .max = 16384,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Uses Commodore (CBM) KBC firmware, to be implemented as identical to
       the IBM PS/2 Type 1 KBC firmware unless evidence emerges of any
       proprietary commands. */
    {
        .name = "[SCAMP] Commodore SL386SX-25",
        .internal_name = "cmdsl386sx25",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_VLSI_SCAMP,
        .init = machine_at_cmdsl386sx25_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_IDE | MACHINE_VIDEO,
        .ram = {
            .min = 1024,
            .max = 8192,
            .step = 512
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &gd5402_onboard_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* The closest BIOS string I find to this one's, differs only in one part,
       and ends in -8, so I'm going to assume that this, too, has an AMI '8'
       (AMI Keyboard BIOS Plus) KBC firmware. */
    {
        .name = "[SCAMP] DataExpert 386SX",
        .internal_name = "dataexpert386sx",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_VLSI_SCAMP,
        .init = machine_at_dataexpert386sx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 10000000,
            .max_bus = 25000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 1024,
            .max = 16384,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[SCAMP] Samsung SPC-6033P",
        .internal_name = "spc6033p",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_VLSI_SCAMP,
        .init = machine_at_spc6033p_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_IDE | MACHINE_VIDEO,
        .ram = {
            .min = 2048,
            .max = 12288,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &ati28800k_spc6033p_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has an unknown AMI KBC firmware, I'm going to assume 'F' until a
       photo or real hardware BIOS string is found. */
    {
        .name = "[SCAT] Kaimei KMX-C-02",
        .internal_name = "kmxc02",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_SCAT,
        .init = machine_at_kmxc02_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 512,
            .max = 16384,
            .step = 512
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has Quadtel KBC firmware. */
    {
        .name = "[WD76C10] Amstrad MegaPC",
        .internal_name = "megapc",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_WD76C10,
        .init = machine_at_wd76c10_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 16000000,
            .max_bus = 25000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_IDE | MACHINE_VIDEO,
        .ram = {
            .min = 1024,
            .max = 32768,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 386SX machines which utilize the MCA bus */
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[MCA] IBM PS/2 model 55SX",
        .internal_name = "ibmps2_m55sx",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_ps2_model_55sx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_MCA,
        .flags = MACHINE_VIDEO,
        .ram = {
            .min = 1024,
            .max = 8192,
            .step = 1024
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[MCA] IBM PS/2 model 65SX",
        .internal_name = "ibmps2_m65sx",
        .type = MACHINE_TYPE_386SX,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_ps2_model_65sx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386SX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_MCA,
        .flags = MACHINE_VIDEO,
        .ram = {
            .min = 1024,
            .max = 8192,
            .step = 1024
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 486SLC machines */
    /* 486SLC machines with just the ISA slot */
    /* Has AMIKey H KBC firmware. */
    {
        .name = "[OPTi 283] RYC Leopard LX",
        .internal_name = "rycleopardlx",
        .type = MACHINE_TYPE_486SLC,
        .chipset = MACHINE_CHIPSET_OPTI_283,
        .init = machine_at_rycleopardlx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_486SLC_IBM,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 16384,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 386DX machines */
    /* Has a Jetkey V3, which identifies as a 'B'. */
    {
        .name = "[ACC 2168] Juko AT046DX3",
        .internal_name = "acc386",
        .type = MACHINE_TYPE_386DX,
        .chipset = MACHINE_CHIPSET_ACC_2168,
        .init = machine_at_acc386_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386DX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 16384,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has an AMI Keyboard BIOS PLUS KBC firmware ('8'). */
    {
        .name = "[C&T 386] ECS 386/32",
        .internal_name = "ecs386",
        .type = MACHINE_TYPE_386DX,
        .chipset = MACHINE_CHIPSET_CT_386,
        .init = machine_at_ecs386_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386DX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 16384,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM AT KBC firmware. */
    {
        .name = "[C&T 386] Samsung SPC-6000A",
        .internal_name = "spc6000a",
        .type = MACHINE_TYPE_386DX,
        .chipset = MACHINE_CHIPSET_CT_386,
        .init = machine_at_spc6000a_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386DX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 32768,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Uses Compaq KBC firmware. */
    {
        .name = "[ISA] Compaq Deskpro 386 (September 1986)",
        .internal_name = "deskpro386",
        .type = MACHINE_TYPE_386DX,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_at_deskpro386_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386DX_DESKPRO386,
            .block = CPU_BLOCK(CPU_486DLC, CPU_RAPIDCAD),
            .min_bus = 16000000,
            .max_bus = 25000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 1024,
            .max = 16384,
            .step = 1024
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[ISA] Compaq Deskpro 386 (May 1988)",
        .internal_name = "deskpro386_05_1988",
        .type = MACHINE_TYPE_386DX,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_at_deskpro386_05_1988_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386DX_DESKPRO386,
            .block = CPU_BLOCK(CPU_486DLC, CPU_RAPIDCAD),
            .min_bus = 16000000,
            .max_bus = 25000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 1024,
            .max = 16384,
            .step = 1024
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[ISA] Compaq Portable III (386)",
        .internal_name = "portableiii386",
        .type = MACHINE_TYPE_386DX,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_at_portableiii386_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386DX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 20000000,
            .max_bus = 20000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_IDE | MACHINE_VIDEO,
        .ram = {
            .min = 1024,
            .max = 14336,
            .step = 1024
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &compaq_plasma_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM AT KBC firmware. */
    {
        .name = "[ISA] Micronics 09-00021",
        .internal_name = "micronics386",
        .type = MACHINE_TYPE_386DX,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_at_micronics386_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386DX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_APM,
        .ram = {
            .min = 512,
            .max = 8192,
            .step = 128
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM AT KBC firmware. */
    {
        .name = "[ISA] Tandy 4000",
        .internal_name = "tandy4000",
        .type = MACHINE_TYPE_386DX,
        .chipset = MACHINE_CHIPSET_DISCRETE,
        .init = machine_at_tandy4000_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386DX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 16384,
            .step = 1024
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a Lance LT38C41 with AMI Megakey P KBC firmware */
    {
        .name = "[ALi M1429] ECS Panda 386V",
        .internal_name = "ecs386v",
        .type = MACHINE_TYPE_386DX,
        .chipset = MACHINE_CHIPSET_ALI_M1429,
        .init = machine_at_ecs386v_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386DX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0,
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 1024,
            .max = 32768,
            .step = 1024,
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMIKey 'F' KBC firmware. */
    {
        .name = "[OPTi 391] DataExpert 386WB",
        .internal_name = "dataexpert386wb",
        .type = MACHINE_TYPE_386DX,
        .chipset = MACHINE_CHIPSET_OPTI_391,
        .init = machine_at_dataexpert386wb_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386DX, /* Actual machine only supports 386DXes */
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 1024,
            .max = 32768,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
     /* The board has a "ASII KB-100" which I was not able to find any information about,
        but the BIOS sends commands C9 without a parameter and D5, both of which are
        Phoenix MultiKey commands. */
    {
        .name = "[OPTi 495] U-Board OPTi 495SLC",
        .internal_name = "award495",
        .type = MACHINE_TYPE_386DX,
        .chipset = MACHINE_CHIPSET_OPTI_495,
        .init = machine_at_opti495_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386DX, /* Actual machine only supports 386DXes */
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 32768,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMIKey F KBC firmware. */
    {
        .name = "[SiS 310] ASUS ISA-386C",
        .internal_name = "asus386",
        .type = MACHINE_TYPE_386DX,
        .chipset = MACHINE_CHIPSET_SIS_310,
        .init = machine_at_asus386_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386DX,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 386DX machines which utilize the MCA bus */
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[MCA] IBM PS/2 model 80 (type 2)",
        .internal_name = "ibmps2_m80",
        .type = MACHINE_TYPE_386DX,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_ps2_model_80_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386DX | CPU_PKG_486BL,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_MCA,
        .flags = MACHINE_VIDEO | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 386DX/486 machines */
    /* Has AMIKey F KBC firmware. */
    {
        .name = "[OPTi 495] DataExpert SX495",
        .internal_name = "ami495",
        .type = MACHINE_TYPE_386DX_486,
        .chipset = MACHINE_CHIPSET_OPTI_495,
        .init = machine_at_opti495_ami_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386DX | CPU_PKG_SOCKET1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 32768,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMIKey F KBC firmware (it's just the MR BIOS for the above machine). */
    {
        .name = "[OPTi 495] DataExpert SX495 (MR BIOS)",
        .internal_name = "mr495",
        .type = MACHINE_TYPE_386DX_486,
        .chipset = MACHINE_CHIPSET_OPTI_495,
        .init = machine_at_opti495_mr_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386DX | CPU_PKG_SOCKET1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 32768,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Winbond W83C42 with unknown firmware. */
    {
        .name = "[ALi M1429G] DataExpert EXP4349",
        .internal_name = "exp4349",
        .type = MACHINE_TYPE_386DX_486,
        .chipset = MACHINE_CHIPSET_ALI_M1429G,
        .init = machine_at_exp4349_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386DX | CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 49152,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[MCA] IBM PS/2 model 70 (type 3)",
        .internal_name = "ibmps2_m70_type3",
        .type = MACHINE_TYPE_386DX_486,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_ps2_model_70_type3_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386DX | CPU_PKG_486BL | CPU_PKG_SOCKET1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_MCA,
        .flags = MACHINE_VIDEO | MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 65536,
            .step = 2048
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[MCA] IBM PS/2 model 80 (type 3)",
        .internal_name = "ibmps2_m80_type3",
        .type = MACHINE_TYPE_386DX_486,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_ps2_model_80_axx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_386DX | CPU_PKG_486BL | CPU_PKG_SOCKET1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_MCA,
        .flags = MACHINE_VIDEO | MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 65536,
            .step = 2048
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 486 machines - Socket 1 */
    /* Has AMI KF KBC firmware. */
    {
        .name = "[ZyMOS Poach] Genoa Unknown 486",
        .internal_name = "genoa486",
        .type = MACHINE_TYPE_486,
        .chipset = MACHINE_CHIPSET_ZYMOS_POACH,
        .init = machine_at_genoa486_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 16384,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMI KF KBC firmware. */
    {
        .name = "[OPTi 381] Gigabyte GA-486L",
        .internal_name = "ga486l",
        .type = MACHINE_TYPE_486,
        .chipset = MACHINE_CHIPSET_OPTI_381,
        .init = machine_at_ga486l_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 16384,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has JetKey 5 KBC Firmware - but the BIOS string ends in a hardcoded -F, and
       the BIOS also explicitly expects command A1 to return a 'F', so it looks like
       the JetKey 5 is a clone of AMIKey type F. */
    {
        .name = "[CS4031] AMI 486 CS4031",
        .internal_name = "cs4031",
        .type = MACHINE_TYPE_486,
        .chipset = MACHINE_CHIPSET_CT_CS4031,
        .init = machine_at_cs4031_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Uses some variant of Phoenix MultiKey/42 as the Intel 8242 chip has a Phoenix
       copyright. */
    {
        .name = "[OPTi 495] Mylex MVI486",
        .internal_name = "mvi486",
        .type = MACHINE_TYPE_486,
        .chipset = MACHINE_CHIPSET_OPTI_495,
        .init = machine_at_mvi486_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_IDE | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMI KF KBC firmware. */
    {
        .name = "[SiS 401] ASUS ISA-486",
        .internal_name = "isa486",
        .type = MACHINE_TYPE_486,
        .chipset = MACHINE_CHIPSET_SIS_401,
        .init = machine_at_isa486_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMIKey H KBC firmware, per the screenshot in "How computers & MS-DOS work". */
    {
        .name = "[SiS 401] Chaintech 433SC",
        .internal_name = "sis401",
        .type = MACHINE_TYPE_486,
        .chipset = MACHINE_CHIPSET_SIS_401,
        .init = machine_at_sis401_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMIKey F KBC firmware, per a photo of a monitor with the BIOS screen on
       eBay. */
    {
        .name = "[SiS 460] ABIT AV4",
        .internal_name = "av4",
        .type = MACHINE_TYPE_486,
        .chipset = MACHINE_CHIPSET_SIS_460,
        .init = machine_at_av4_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* The chip is a Lance LT38C41, a clone of the Intel 8041, and the BIOS sends
       commands BC, BD, and C9 which exist on both AMIKey and Phoenix MultiKey/42,
       but it does not write a byte after C9, which is consistent with AMIKey, so
       this must have some form of AMIKey. */
    {
        .name = "[VIA VT82C495] FIC 486-VC-HD",
        .internal_name = "486vchd",
        .type = MACHINE_TYPE_486,
        .chipset = MACHINE_CHIPSET_VIA_VT82C495,
        .init = machine_at_486vchd_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 64512,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a VLSI VL82C113A SCAMP Combination I/O which holds the KBC. */
    {
        .name = "[VLSI 82C480] HP Vectra 486VL",
        .internal_name = "vect486vl",
        .type = MACHINE_TYPE_486,
        .chipset = MACHINE_CHIPSET_VLSI_VL82C480,
        .init = machine_at_vect486vl_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_IDE | MACHINE_VIDEO | MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 32768,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL, /*Has SIO (sorta): VLSI VL82C113A SCAMP Combination I/O*/
        .vid_device = &gd5428_onboard_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a standard IBM PS/2 KBC firmware or a clone thereof. */
    {
        .name = "[VLSI 82C481] Siemens Nixdorf D824",
        .internal_name = "d824",
        .type = MACHINE_TYPE_486,
        .chipset = MACHINE_CHIPSET_VLSI_VL82C481,
        .init = machine_at_d824_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_IDE | MACHINE_VIDEO | MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 32768,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &gd5428_onboard_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[MCA] IBM PS/2 model 70 (type 4)",
        .internal_name = "ibmps2_m70_type4",
        .type = MACHINE_TYPE_486,
        .chipset = MACHINE_CHIPSET_PROPRIETARY,
        .init = machine_ps2_model_70_type4_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET1,
            .block = CPU_BLOCK(CPU_i486SX, CPU_i486SX_SLENH, CPU_Am486SX, CPU_Cx486S),
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_MCA,
        .flags = MACHINE_VIDEO | MACHINE_SOFTFLOAT_ONLY,
        .ram = {
            .min = 2048,
            .max = 65536,
            .step = 2048
        },
        .nvrmask = 63,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* 486 machines - Socket 2 */
    /* 486 machines with just the ISA slot */
    /* Uses some variant of Phoenix MultiKey/42 as the BIOS sends keyboard controller
       command C7 (OR input byte with received data byte). */
    {
        .name = "[ACC 2168] Packard Bell PB410A",
        .internal_name = "pb410a",
        .type = MACHINE_TYPE_486_S2,
        .chipset = MACHINE_CHIPSET_ACC_2168,
        .init = machine_at_pb410a_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_IDE | MACHINE_VIDEO | MACHINE_APM | MACHINE_GAMEPORT,
        .ram = {
            .min = 4096,
            .max = 36864,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Uses an ACER/NEC 90M002A (UPD82C42C, 8042 clone) with unknown firmware (V4.01H). */
    {
        .name = "[ALi M1429G] Acer A1G",
        .internal_name = "acera1g",
        .type = MACHINE_TYPE_486_S2,
        .chipset = MACHINE_CHIPSET_ALI_M1429G,
        .init = machine_at_acera1g_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_IDE | MACHINE_VIDEO | MACHINE_APM,
        .ram = {
            .min = 4096,
            .max = 36864,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &gd5428_onboard_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    {
        .name = "[ALi M1429G] Kaimei SA-486 VL-BUS M.B.",
        .internal_name = "win486",
        .type = MACHINE_TYPE_486_S2,
        .chipset = MACHINE_CHIPSET_ALI_M1429G,
        .init = machine_at_winbios1429_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 32768,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has JetKey 5 KBC Firmware which looks like it is a clone of AMIKey type F.
       It also has those Ex commands also seen on the VIA VT82C42N (the BIOS
       supposedly sends command EF.
       The board was also seen in 2003 with a -H string - perhaps someone swapped
       the KBC? */
    {
        .name = "[ALi M1429] Olystar LIL1429",
        .internal_name = "ali1429",
        .type = MACHINE_TYPE_486_S2,
        .chipset = MACHINE_CHIPSET_ALI_M1429,
        .init = machine_at_ali1429_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 32768,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has a standalone AMI Megakey 1993, which is type 'P'. */
    {
        .name = "[IMS 8848] Tekram G486IP",
        .internal_name = "g486ip",
        .type = MACHINE_TYPE_486_S2,
        .chipset = MACHINE_CHIPSET_IMS_8848,
        .init = machine_at_g486ip_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 131072,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMIKey-2 'H' KBC firmware. */
    {
        .name = "[OPTi 499] Alaris COUGAR 486BL",
        .internal_name = "cougar",
        .type = MACHINE_TYPE_486_S2,
        .chipset = MACHINE_CHIPSET_OPTI_499,
        .init = machine_at_cougar_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3 | CPU_PKG_486BL,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Uses an Intel KBC with Phoenix MultiKey KBC firmware. */
    {
        .name = "[SiS 461] DEC DECpc LPV",
        .internal_name = "decpclpv",
        .type = MACHINE_TYPE_486_S2,
        .chipset = MACHINE_CHIPSET_SIS_461,
        .init = machine_at_decpclpv_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_IDE | MACHINE_VIDEO | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 32768,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &s3_86c805_onboard_vlb_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* The BIOS does not send any non-standard keyboard controller commands and wants
       a PS/2 mouse, so it's an IBM PS/2 KBC (Type 1) firmware. */
    {
        .name = "[SiS 461] IBM PS/ValuePoint 433DX/Si",
        .internal_name = "valuepoint433",
        .type = MACHINE_TYPE_486_S2,
        .chipset = MACHINE_CHIPSET_SIS_461,
        .init = machine_at_valuepoint433_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_IDE | MACHINE_VIDEO | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMI MegaKey KBC.  */
    {
        .name = "[i420TX] J-Bond PCI400C-A",
        .internal_name = "pci400ca",
        .type = MACHINE_TYPE_486_S2,
        .chipset = MACHINE_CHIPSET_INTEL_420TX,
        .init = machine_at_pci400ca_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_SCSI,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = &keyboard_at_ami_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },


    /* 486 machines - Socket 3 */
    /* 486 machines with just the ISA slot */
    /* Has a Fujitsu MBL8042H KBC. */
    {
        .name = "[Contaq 82C596A] A-Trend 4GPV5",
        .internal_name = "4gpv5",
        .type = MACHINE_TYPE_486_S3,
        .chipset = MACHINE_CHIPSET_CONTAQ_82C596,
        .init = machine_at_4gpv5_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMI MegaKey KBC firmware. */
    {
        .name = "[Contaq 82C597] Visionex Green-B",
        .internal_name = "greenb",
        .type = MACHINE_TYPE_486_S3,
        .chipset = MACHINE_CHIPSET_CONTAQ_82C597,
        .init = machine_at_greenb_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Version 1.0 has an AMIKEY-2, version 2.0 has a VIA VT82C42N KBC. */
    {
        .name = "[OPTi 895] Jetway J-403TG",
        .internal_name = "403tg",
        .type = MACHINE_TYPE_486_S3,
        .chipset = MACHINE_CHIPSET_OPTI_895_802G,
        .init = machine_at_403tg_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has JetKey 5 KBC Firmware which looks like it is a clone of AMIKey type F. */
    {
        .name = "[OPTi 895] Jetway J-403TG Rev D",
        .internal_name = "403tg_d",
        .type = MACHINE_TYPE_486_S3,
        .chipset = MACHINE_CHIPSET_OPTI_895_802G,
        .init = machine_at_403tg_d_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has JetKey 5 KBC Firmware which looks like it is a clone of AMIKey type F. */
    {
        .name = "[OPTi 895] Jetway J-403TG Rev D (MR BIOS)",
        .internal_name = "403tg_d_mr",
        .type = MACHINE_TYPE_486_S3,
        .chipset = MACHINE_CHIPSET_OPTI_895_802G,
        .init = machine_at_403tg_d_mr_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* has a Phoenix PLCC Multikey copyrighted 1993, version unknown. */
    {
        .name = "[OPTi 895] Packard Bell PB450",
        .internal_name = "pb450",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_OPTI_895_802G,
        .init = machine_at_pb450_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_SUPER_IO | MACHINE_IDE_DUAL | MACHINE_VIDEO,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = &pb450_device,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &gd5428_vlb_onboard_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Uses an NEC 90M002A (UPD82C42C, 8042 clone) with unknown firmware. */
    {
        .name = "[SiS 461] Acer V10",
        .internal_name = "acerv10",
        .type = MACHINE_TYPE_486_S3,
        .chipset = MACHINE_CHIPSET_SIS_461,
        .init = machine_at_acerv10_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_VLB,
        .flags = MACHINE_IDE | MACHINE_APM, /* Machine has internal SCSI: Adaptec AIC-6360 */
        .ram = {
            .min = 1024,
            .max = 32768,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* The BIOS string ends in -U, unless command 0xA1 (AMIKey get version) returns an
       'F', in which case, it ends in -F, so it has an AMIKey F KBC firmware.
       The photo of the board shows an AMIKey KBC which is indeed F. */
    {
        .name = "[SiS 471] ABIT AB-AH4",
        .internal_name = "win471",
        .type = MACHINE_TYPE_486_S3,
        .chipset = MACHINE_CHIPSET_SIS_471,
        .init = machine_at_win471_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMIKey-2 'H' keyboard BIOS. */
    {
        .name = "[SiS 471] AOpen Vi15G",
        .internal_name = "vi15g",
        .type = MACHINE_TYPE_486_S3,
        .chipset = MACHINE_CHIPSET_SIS_471,
        .init = machine_at_vi15g_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    {
        .name = "[SiS 471] ASUS VL/I-486SV2GX4",
        .internal_name = "vli486sv2g",
        .type = MACHINE_TYPE_486_S3,
        .chipset = MACHINE_CHIPSET_SIS_471,
        .init = machine_at_vli486sv2g_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has JetKey 5 KBC Firmware which looks like it is a clone of AMIKey type F. */
    {
        .name = "[SiS 471] DTK PKM-0038S E-2",
        .internal_name = "dtk486",
        .type = MACHINE_TYPE_486_S3,
        .chipset = MACHINE_CHIPSET_SIS_471,
        .init = machine_at_dtk486_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a Lance LT38C41L with AMIKey F keyboard BIOS. */
    {
        .name = "[SiS 471] Epox GXA486SG",
        .internal_name = "ami471",
        .type = MACHINE_TYPE_486_S3,
        .chipset = MACHINE_CHIPSET_SIS_471,
        .init = machine_at_ami471_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has MR (!) KBC firmware, which is a clone of the standard IBM PS/2 KBC firmware. */
    {
        .name = "[SiS 471] SiS VL-BUS 471 REV. A1",
        .internal_name = "px471",
        .type = MACHINE_TYPE_486_S3,
        .chipset = MACHINE_CHIPSET_SIS_471,
        .init = machine_at_px471_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_IDE | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 131072,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* TriGem AMIBIOS Pre-Color with TriGem AMI 'Z' keyboard controller */
    {
        .name = "[SiS 471] TriGem 486G",
        .internal_name = "tg486g",
        .type = MACHINE_TYPE_486_S3,
        .chipset = MACHINE_CHIPSET_SIS_471,
        .init = machine_at_tg486g_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_VLB,
        .flags = MACHINE_IDE | MACHINE_APM, /* Has internal video: Western Digital WD90C33-ZZ */
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Unknown revision phoenix 1993 multikey */
    {
        .name = "[SiS 471] DEC Venturis 4xx",
        .internal_name = "dvent4xx",
        .type = MACHINE_TYPE_486_S3,
        .chipset = MACHINE_CHIPSET_SIS_471,
        .init = machine_at_dvent4xx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_IDE_DUAL | MACHINE_SUPER_IO | MACHINE_APM | MACHINE_VIDEO,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &s3_phoenix_trio32_onboard_vlb_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    {
        .name = "[ALi M1429G] ECS AL486",
        .internal_name = "ecsal486",
        .type = MACHINE_TYPE_486_S3,
        .chipset = MACHINE_CHIPSET_ALI_M1429G,
        .init = machine_at_ecsal486_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 98304,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This uses a VIA VT82C42N KBC, which is a clone of type 'F' with additional commands */
    {
        .name = "[ALi M1429G] Lanner Electronics AP-4100AA",
        .internal_name = "ap4100aa",
        .type = MACHINE_TYPE_486_S3,
        .chipset = MACHINE_CHIPSET_ALI_M1429G,
        .init = machine_at_ap4100aa_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_AT,
        .flags = MACHINE_SUPER_IO | MACHINE_IDE | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 32768,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* JETKey V5.0 */
    {
        .name = "[ALi M1429G] A-Trend ATC-1762",
        .internal_name = "atc1762",
        .type = MACHINE_TYPE_486_S3,
        .chipset = MACHINE_CHIPSET_ALI_M1429G,
        .init = machine_at_atc1762_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 40960,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 486 machines which utilize the PCI bus */
    /* Machine with ALi M1429G chipset and M1435 southbridge */
    /* Has an AMIKEY-2 KBC. */
    {
        .name = "[ALi M1429G] MSI MS-4134",
        .internal_name = "ms4134",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_ALI_M1429G,
        .init = machine_at_ms4134_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PCIV,
        .flags = MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 131072,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* TriGem machine with M1429G and PhoenixBIOS */
    {
        .name = "[ALi M1429G] TriGem 486GP",
        .internal_name = "tg486gp",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_ALI_M1429G,
        .init = machine_at_tg486gp_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_PCIV,
        .flags = MACHINE_IDE | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 131072,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    {
        .name = "[ALi M1489] AAEON SBC-490",
        .internal_name = "sbc490",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_ALI_M1489,
        .init = machine_at_sbc490_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_VIDEO | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = &tgui9440_onboard_pci_device,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has the ALi M1487/9's on-chip keyboard controller which clones a standard AT
       KBC. */
    {
        .name = "[ALi M1489] ABIT AB-PB4",
        .internal_name = "abpb4",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_ALI_M1489,
        .init = machine_at_abpb4_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PCI, /* Machine has a PISA slot */
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has the ALi M1487/9's on-chip keyboard controller which clones a standard AT
       KBC.
       The BIOS string always ends in -U, but the BIOS will send AMIKey commands 0xCA
       and 0xCB if command 0xA1 returns a letter in the 0x5x or 0x7x ranges, so I'm
       going to give it an AMI 'U' KBC. */
    {
        .name = "[ALi M1489] AMI WinBIOS 486 PCI",
        .internal_name = "win486pci",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_ALI_M1489,
        .init = machine_at_win486pci_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has the ALi M1487/9's on-chip keyboard controller which clones a standard AT
       KBC.
       The known BIOS string ends in -E, and the BIOS returns whatever command 0xA1
       returns (but only if command 0xA1 is instant response), so said ALi keyboard
       controller likely returns 'E'. */
    {
        .name = "[ALi M1489] MSI MS-4145",
        .internal_name = "ms4145",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_ALI_M1489,
        .init = machine_at_ms4145_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has an ALi M5042 keyboard controller with Phoenix MultiKey/42 v1.40 firmware. */
    {
        .name = "[ALi M1489] ESA TF-486",
        .internal_name = "tf486",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_ALI_M1489,
        .init = machine_at_tf486_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has an ALi M5042 with phoenix firmware like the ESA TF-486. */
    {
        .name = "[ALi M1489] Acrosser AR-B1476",
        .internal_name = "arb1476",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_ALI_M1489,
        .init = machine_at_arb1476_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_SUPER_IO | MACHINE_IDE | MACHINE_APM, /* Has onboard video: C&T F65545 */
        .ram = {
            .min = 1024,
            .max = 32768,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[OPTi 802G] IBM Aptiva 510/710/Vision",
        .internal_name = "aptiva510",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_OPTI_895_802G,
        .init = machine_at_aptiva510_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3_PC330,
            .block = CPU_BLOCK_NONE,
            .min_bus = 25000000,
            .max_bus = 33333333,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 2.0,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE | MACHINE_VIDEO | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &gd5430_onboard_vlb_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[OPTi 802G] IBM PC 330 (type 6573)",
        .internal_name = "pc330_6573",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_OPTI_895_802G,
        .init = machine_at_pc330_6573_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3_PC330,
            .block = CPU_BLOCK_NONE,
            .min_bus = 25000000,
            .max_bus = 33333333,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 2.0,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE | MACHINE_VIDEO | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &gd5430_onboard_vlb_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    {
        .name = "[i420EX] ASUS PVI-486AP4",
        .internal_name = "486ap4",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_INTEL_420EX,
        .init = machine_at_486ap4_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_PCIV,
        .flags = MACHINE_IDE | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 131072,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has the Phoenix MultiKey KBC firmware. */
    {
        .name = "[i420EX] Intel Classic/PCI ED",
        .internal_name = "ninja",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_INTEL_420EX,
        .init = machine_at_ninja_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_IDE | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 131072,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has Phoenix Multikey/42 PS/2 KBC, but unknown version */
    {
        .name = "[i420EX] Anigma BAT4IP3e",
        .internal_name = "bat4ip3e",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_INTEL_420EX,
        .init = machine_at_bat4ip3e_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_SUPER_IO | MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 131072,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    {
        .name = "[i420EX] Advanced Integration Research 486PI",
        .internal_name = "486pi",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_INTEL_420EX,
        .init = machine_at_486pi_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PCIV,
        .flags = MACHINE_SUPER_IO | MACHINE_IDE | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 131072,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* absolutely no KBC info */
    {
        .name = "[i420EX] ICS SB486P",
        .internal_name = "sb486p",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_INTEL_420EX,
        .init = machine_at_sb486p_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_SUPER_IO | MACHINE_IDE | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 131072,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* I'm going to assume this as an AMIKey-2 like the other two 486SP3's. */
    {
        .name = "[i420TX] ASUS PCI/I-486SP3",
        .internal_name = "486sp3",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_INTEL_420TX,
        .init = machine_at_486sp3_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_IDE | MACHINE_SCSI | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 131072,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has the Phoenix MultiKey KBC firmware. */
    {
        .name = "[i420TX] Intel Classic/PCI",
        .internal_name = "alfredo",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_INTEL_420TX,
        .init = machine_at_alfredo_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE | MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 131072,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* According to another string seen on the UH19 website, this has AMI 'H' KBC. */
    {
        .name = "[i420TX] AMI Super Voyager PCI",
        .internal_name = "amis76",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_INTEL_420TX,
        .init = machine_at_amis76_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_SUPER_IO | MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 131072,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has an AMIKey-2, which is an updated version of type 'H'. Also has a
       SST 29EE010 Flash chip. */
    {
        .name = "[i420ZX] ASUS PCI/I-486SP3G",
        .internal_name = "486sp3g",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_INTEL_420ZX,
        .init = machine_at_486sp3g_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE | MACHINE_SCSI | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 131072,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This most likely has a standalone AMI Megakey 1993, which is type 'P', like the below Tekram board. */
    {
        .name = "[IMS 8848] J-Bond PCI400C-B",
        .internal_name = "pci400cb",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_IMS_8848,
        .init = machine_at_pci400cb_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PCIV,
        .flags = MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 131072,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    {
        .name = "[SiS 496] ASUS PVI-486SP3C",
        .internal_name = "486sp3c",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_SIS_496,
        .init = machine_at_486sp3c_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_PCIV,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 261120,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    {
        .name = "[SiS 496] Lucky Star LS-486E",
        .internal_name = "ls486e",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_SIS_496,
        .init = machine_at_ls486e_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_BUS_PS2_LATCH | MACHINE_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 131072,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a VIA VT82C42N KBC. */
    {
        .name = "[SiS 496] Micronics M4Li",
        .internal_name = "m4li",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_SIS_496,
        .init = machine_at_m4li_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 131072,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Revision 1 has a Lance LT38C41L, revision 2 has a Holtek HT6542B. Another variant with a Bestkey KBC might exist as well. */
    {
        .name = "[SiS 496] Rise Computer R418",
        .internal_name = "r418",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_SIS_496,
        .init = machine_at_r418_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_BUS_PS2_LATCH | MACHINE_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 261120,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has a Holtek HT6542B KBC and the BIOS does not send a single non-standard KBC command, so it
       must be an ASIC that clones the standard IBM PS/2 KBC. */
    {
        .name = "[SiS 496] Soyo 4SAW2",
        .internal_name = "4saw2",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_SIS_496,
        .init = machine_at_4saw2_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK(CPU_i486SX, CPU_i486DX, CPU_Am486SX, CPU_Am486DX),
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_PCIV,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 261120,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* According to MrKsoft, his real 4DPS has an AMIKey-2, which is an updated version
       of type 'H'. There are other variants of the board with Holtek HT6542B KBCs. */
    {
        .name = "[SiS 496] Zida Tomato 4DP",
        .internal_name = "4dps",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_SIS_496,
        .init = machine_at_4dps_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_GAMEPORT,
        .ram = {
            .min = 2048,
            .max = 261120,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* AMIKEY-2 */
    {
        .name = "[SiS 496] MSI MS-4144",
        .internal_name = "ms4144",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_SIS_496,
        .init = machine_at_ms4144_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_SUPER_IO | MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 5120, /* Hack: machine seems to break with less than 5 MBs of RAM */
            .max = 131072,
            .step = 1024
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has the UMC 88xx on-chip KBC. */
    {
        .name = "[UMC 8881] A-Trend ATC-1415",
        .internal_name = "atc1415",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_UMC_UM8881,
        .init = machine_at_atc1415_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    {
        .name = "[UMC 8881] ECS Elite UM8810P-AIO",
        .internal_name = "ecs486",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_UMC_UM8881,
        .init = machine_at_ecs486_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PCI | MACHINE_BUS_PS2_LATCH,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 131072,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMIKey Z(!) KBC firmware. */
    {
        .name = "[UMC 8881] Epson ActionPC 2600",
        .internal_name = "actionpc2600",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_UMC_UM8881,
        .init = machine_at_actionpc2600_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_VIDEO,
        .ram = {
            .min = 1024,
            .max = 262144,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has the UMC 88xx on-chip KBC. All the copies of the BIOS string I can find, end in
       in -H, so the UMC on-chip KBC likely emulates the AMI 'H' KBC firmware. */
    {
        .name = "[UMC 8881] Epson ActionTower 8400",
        .internal_name = "actiontower8400",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_UMC_UM8881,
        .init = machine_at_actiontower8400_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_SUPER_IO | MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_VIDEO,
        .ram = {
            .min = 1024,
            .max = 262144,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has the UMC 88xx on-chip KBC. All the copies of the BIOS string I can find, end in
       in -H, so the UMC on-chip KBC likely emulates the AMI 'H' KBC firmware. */
    {
        .name = "[UMC 8881] PC Chips M919",
        .internal_name = "m919",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_UMC_UM8881,
        .init = machine_at_m919_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PCIV,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 131072,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM PS/2 Type 1 KBC firmware. Uses a mysterious I/O port C05. */
    {
        .name = "[UMC 8881] Samsung SPC7700P-LW",
        .internal_name = "spc7700plw",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_UMC_UM8881,
        .init = machine_at_spc7700plw_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 131072,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has a Holtek KBC. */
    {
        .name = "[UMC 8881] Shuttle HOT-433A",
        .internal_name = "hot433a",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_UMC_UM8881,
        .init = machine_at_hot433a_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 262144,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Compaq Presario 7100 / 7200 Series, using MiTAC/Trigon PL4600C (486). */
    /* Has a VIA VT82C42N KBC. */
    {
        .name = "[UMC 8881] Compaq Presario 7100/7200 Series 486",
        .internal_name = "pl4600c",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_UMC_UM8881,
        .init = machine_at_pl4600c_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_VIDEO | MACHINE_SOUND | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 65536,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &gd5430_onboard_pci_device,
        .snd_device = &ess_1688_device,
        .net_device = NULL
    },
    /* Has a VIA VT82C406 KBC+RTC that likely has identical commands to the VT82C42N. */
    {
        .name = "[VIA VT82C496G] DFI G486VPA",
        .internal_name = "g486vpa",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_VIA_VT82C496G,
        .init = machine_at_g486vpa_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_PCIV,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 1024,
            .max = 131072,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a VIA VT82C42N KBC. */
    {
        .name = "[VIA VT82C496G] FIC VIP-IO2",
        .internal_name = "486vipio2",
        .type = MACHINE_TYPE_486_S3_PCI,
        .chipset = MACHINE_CHIPSET_VIA_VT82C496G,
        .init = machine_at_486vipio2_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET3,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_PCIV,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_GAMEPORT,
        .ram = {
            .min = 1024,
            .max = 131072,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 486 machines - Miscellaneous */
    /* 486 machines which utilize the PCI bus */
    /* Has a Winbond W83977F Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[STPC Client] ITOX STAR",
        .internal_name = "itoxstar",
        .type = MACHINE_TYPE_486_MISC,
        .chipset = MACHINE_CHIPSET_STPC_CLIENT,
        .init = machine_at_itoxstar_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_STPC,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 75000000,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 1.0,
            .max_multi = 1.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a Winbond W83977F Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[STPC Consumer-II] Acrosser AR-B1423C",
        .internal_name = "arb1423c",
        .type = MACHINE_TYPE_486_MISC,
        .chipset = MACHINE_CHIPSET_STPC_CONSUMER_II,
        .init = machine_at_arb1423c_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_STPC,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 66666667,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 2.0,
            .max_multi = 2.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM, /* Machine has internal video: ST STPC Atlas */
        .ram = {
            .min = 32768,
            .max = 163840,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a Winbond W83977F Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[STPC Consumer-II] Acrosser AR-B1479",
        .internal_name = "arb1479",
        .type = MACHINE_TYPE_486_MISC,
        .chipset = MACHINE_CHIPSET_STPC_CONSUMER_II,
        .init = machine_at_arb1479_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_STPC,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 66666667,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 2.0,
            .max_multi = 2.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB, /* Machine has internal video: ST STPC Atlas */
        .ram = {
            .min = 32768,
            .max = 163840,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a Winbond W83977F Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[STPC Consumer-II] Lanner Electronics IAC-H488",
        .internal_name = "iach488",
        .type = MACHINE_TYPE_486_MISC,
        .chipset = MACHINE_CHIPSET_STPC_CONSUMER_II,
        .init = machine_at_iach488_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_STPC,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 66666667,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 2.0,
            .max_multi = 2.0
        },
        .bus_flags = MACHINE_PS2,
        .flags = MACHINE_IDE | MACHINE_APM, /* Machine has internal video: ST STPC Atlas and NIC: Realtek RTL8139C+ */
        .ram = {
            .min = 32768,
            .max = 131072,
            .step = 32768
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a Winbond W83977F Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[STPC Elite] Advantech PCM-9340",
        .internal_name = "pcm9340",
        .type = MACHINE_TYPE_486_MISC,
        .chipset = MACHINE_CHIPSET_STPC_ELITE,
        .init = machine_at_pcm9340_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_STPC,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 66666667,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 2.0,
            .max_multi = 2.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 32768,
            .max = 98304,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a Winbond W83977F Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[STPC Atlas] AAEON PCM-5330",
        .internal_name = "pcm5330",
        .type = MACHINE_TYPE_486_MISC,
        .chipset = MACHINE_CHIPSET_STPC_ATLAS,
        .init = machine_at_pcm5330_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_STPC,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 66666667,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 2.0,
            .max_multi = 2.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 32768,
            .max = 131072,
            .step = 32768
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* Socket 4 machines */
    /* 430LX */
    /* Has AMIKey H KBC firmware (AMIKey-2), per POST screen with BIOS string
       shown in the manual. Has PS/2 mouse support with serial-style (DB9)
       connector.
       The boot block for BIOS recovery requires an unknown bit on port 805h
       to be clear. */
    {
        .name = "[i430LX] AMI Excalibur PCI Pentium",
        .internal_name = "excaliburpci",
        .type = MACHINE_TYPE_SOCKET4,
        .chipset = MACHINE_CHIPSET_INTEL_430LX,
        .init = machine_at_excaliburpci_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET4,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 5000,
            .max_voltage = 5000,
            .min_multi = MACHINE_MULTIPLIER_FIXED,
            .max_multi = MACHINE_MULTIPLIER_FIXED
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE | MACHINE_APM, /* Machine has internal SCSI */
        .ram = {
            .min = 2048,
            .max = 131072,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMIKey F KBC firmware (AMIKey). */
    {
        .name = "[i430LX] ASUS P/I-P5MP3",
        .internal_name = "p5mp3",
        .type = MACHINE_TYPE_SOCKET4,
        .chipset = MACHINE_CHIPSET_INTEL_430LX,
        .init = machine_at_p5mp3_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET4,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 5000,
            .max_voltage = 5000,
            .min_multi = MACHINE_MULTIPLIER_FIXED,
            .max_multi = MACHINE_MULTIPLIER_FIXED
        },
        .bus_flags = MACHINE_BUS_PS2_LATCH | MACHINE_PCI,
        .flags = MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 196608,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[i430LX] Dell Dimension XPS P60",
        .internal_name = "dellxp60",
        .type = MACHINE_TYPE_SOCKET4,
        .chipset = MACHINE_CHIPSET_INTEL_430LX,
        .init = machine_at_dellxp60_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET4,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 5000,
            .max_voltage = 5000,
            .min_multi = MACHINE_MULTIPLIER_FIXED,
            .max_multi = MACHINE_MULTIPLIER_FIXED
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_IDE | MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 131072,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[i430LX] Dell OptiPlex 560/L",
        .internal_name = "opti560l",
        .type = MACHINE_TYPE_SOCKET4,
        .chipset = MACHINE_CHIPSET_INTEL_430LX,
        .init = machine_at_opti560l_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET4,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 5000,
            .max_voltage = 5000,
            .min_multi = MACHINE_MULTIPLIER_FIXED,
            .max_multi = MACHINE_MULTIPLIER_FIXED
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE | MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 131072,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has the Phoenix MultiKey KBC firmware.
       This is basically an Intel Batman (*NOT* Batman's Revenge) with a fancier
       POST screen */
    {
        .name = "[i430LX] AMBRA DP60 PCI",
        .internal_name = "ambradp60",
        .type = MACHINE_TYPE_SOCKET4,
        .chipset = MACHINE_CHIPSET_INTEL_430LX,
        .init = machine_at_ambradp60_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET4,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 5000,
            .max_voltage = 5000,
            .min_multi = MACHINE_MULTIPLIER_FIXED,
            .max_multi = MACHINE_MULTIPLIER_FIXED
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE | MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 131072,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    {
        .name = "[i430LX] IBM PS/ValuePoint P60",
        .internal_name = "valuepointp60",
        .type = MACHINE_TYPE_SOCKET4,
        .chipset = MACHINE_CHIPSET_INTEL_430LX,
        .init = machine_at_valuepointp60_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET4,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 5000,
            .max_voltage = 5000,
            .min_multi = MACHINE_MULTIPLIER_FIXED,
            .max_multi = MACHINE_MULTIPLIER_FIXED
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_VIDEO | MACHINE_VIDEO_8514A | MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 131072,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &mach32_onboard_pci_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has the Phoenix MultiKey KBC firmware. */
    {
        .name = "[i430LX] Intel Premiere/PCI",
        .internal_name = "revenge",
        .type = MACHINE_TYPE_SOCKET4,
        .chipset = MACHINE_CHIPSET_INTEL_430LX,
        .init = machine_at_revenge_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET4,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 5000,
            .max_voltage = 5000,
            .min_multi = MACHINE_MULTIPLIER_FIXED,
            .max_multi = MACHINE_MULTIPLIER_FIXED
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 131072,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMI MegaKey 'H' KBC firmware. */
    {
        .name = "[i430LX] Gigabyte GA-586IS",
        .internal_name = "586is",
        .type = MACHINE_TYPE_SOCKET4,
        .chipset = MACHINE_CHIPSET_INTEL_430LX,
        .init = machine_at_586is_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET4,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 5000,
            .max_voltage = 5000,
            .min_multi = MACHINE_MULTIPLIER_FIXED,
            .max_multi = MACHINE_MULTIPLIER_FIXED
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 131072,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has the Phoenix MultiKey KBC firmware. */
    {
        .name = "[i430LX] Packard Bell PB520R",
        .internal_name = "pb520r",
        .type = MACHINE_TYPE_SOCKET4,
        .chipset = MACHINE_CHIPSET_INTEL_430LX,
        .init = machine_at_pb520r_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET4,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 5000,
            .max_voltage = 5000,
            .min_multi = MACHINE_MULTIPLIER_FIXED,
            .max_multi = MACHINE_MULTIPLIER_FIXED
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_VIDEO | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 139264,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &gd5434_onboard_pci_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* The M5Pi appears to have a Phoenix MultiKey KBC firmware according to photos. */	
    {
        .name = "[i430LX] Micronics M5Pi",
        .internal_name = "m5pi",
        .type = MACHINE_TYPE_SOCKET4,
        .chipset = MACHINE_CHIPSET_INTEL_430LX,
        .init = machine_at_m5pi_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET4,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 5000,
            .max_voltage = 5000,
            .min_multi = MACHINE_MULTIPLIER_FIXED,
            .max_multi = MACHINE_MULTIPLIER_FIXED
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 131072,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },	

    /* OPTi 596/597 */
    /* This uses an AMI KBC firmware in PS/2 mode (it sends command A5 with the
       PS/2 "Load Security" meaning), most likely MegaKey as it sends command AF
       (Set Extended Controller RAM) just like the later Intel AMI BIOS'es. */
    {
        .name = "[OPTi 597] AMI Excalibur VLB",
        .internal_name = "excalibur",
        .type = MACHINE_TYPE_SOCKET4,
        .chipset = MACHINE_CHIPSET_OPTI_547_597,
        .init = machine_at_excalibur_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET4,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 60000000,
            .min_voltage = 5000,
            .max_voltage = 5000,
            .min_multi = MACHINE_MULTIPLIER_FIXED,
            .max_multi = MACHINE_MULTIPLIER_FIXED
        },
        .bus_flags = MACHINE_PS2_VLB,
        .flags = MACHINE_IDE | MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 65536,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* OPTi 596/597/822 */
    /* Has a VIA VT82C42N KBC with AMI 'F' firmware */
    {
        .name = "[OPTi 597] AT&T Globalyst 330 (Pentium)",
        .internal_name = "globalyst330_p5",
        .type = MACHINE_TYPE_SOCKET4,
        .chipset = MACHINE_CHIPSET_OPTI_547_597,
        .init = machine_at_globalyst330_p5_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET4,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 5000,
            .max_voltage = 5000,
            .min_multi = MACHINE_MULTIPLIER_FIXED,
            .max_multi = MACHINE_MULTIPLIER_FIXED
        },
        .bus_flags = MACHINE_PCIV,
        .flags = MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 65536,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has AMIKey 'F' KBC firmware. */
    {
        .name = "[OPTi 597] Supermicro P5VL-PCI",
        .internal_name = "p5vl",
        .type = MACHINE_TYPE_SOCKET4,
        .chipset = MACHINE_CHIPSET_OPTI_547_597,
        .init = machine_at_p5vl_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET4,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 5000,
            .max_voltage = 5000,
            .min_multi = MACHINE_MULTIPLIER_FIXED,
            .max_multi = MACHINE_MULTIPLIER_FIXED
        },
        .bus_flags = MACHINE_PCIV,
        .flags = MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* SiS 50x */
    /* This has some form of AMI MegaKey as it uses keyboard controller command 0xCC. */
    {
        .name = "[SiS 501] AMI Excalibur PCI-II Pentium ISA",
        .internal_name = "excaliburpci2",
        .type = MACHINE_TYPE_SOCKET4,
        .chipset = MACHINE_CHIPSET_SIS_501,
        .init = machine_at_excaliburpci2_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET4,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 5000,
            .max_voltage = 5000,
            .min_multi = MACHINE_MULTIPLIER_FIXED,
            .max_multi = MACHINE_MULTIPLIER_FIXED
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    {
        .name = "[SiS 501] ASUS PCI/I-P5SP4",
        .internal_name = "p5sp4",
        .type = MACHINE_TYPE_SOCKET4,
        .chipset = MACHINE_CHIPSET_SIS_501,
        .init = machine_at_p5sp4_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET4,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 5000,
            .max_voltage = 5000,
            .min_multi = MACHINE_MULTIPLIER_FIXED,
            .max_multi = MACHINE_MULTIPLIER_FIXED
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Socket 5 machines */
    /* 430NX */
    /* This has the Phoenix MultiKey KBC firmware. */
    {
        .name = "[i430NX] Intel Premiere/PCI II",
        .internal_name = "plato",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_INTEL_430NX,
        .init = machine_at_plato_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86, CPU_Cx6x86),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3520,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 1.5
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 131072,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Same as Intel Premiere PCI/II, but with a Dell OEM BIOS */
    {
        .name = "[i430NX] Dell Dimension XPS Pxxx",
        .internal_name = "dellplato",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_INTEL_430NX,
        .init = machine_at_dellplato_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86, CPU_Cx6x86),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3520,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 1.5
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 131072,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has the Phoenix MultiKey KBC firmware.
       This is basically an Intel Premiere/PCI II with a fancier POST screen. */
    {
        .name = "[i430NX] AMBRA DP90 PCI",
        .internal_name = "ambradp90",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_INTEL_430NX,
        .init = machine_at_ambradp90_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86, CPU_Cx6x86),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 1.5
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 131072,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMI 'H' KBC firmware. */
    {
        .name = "[i430NX] ASUS PCI/I-P54NP4",
        .internal_name = "p54np4",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_INTEL_430NX,
        .init = machine_at_p54np4_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 3520,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 1.5
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE | MACHINE_SCSI | MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 524288,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMI 'H' KBC firmware. */
    {
        .name = "[i430NX] Gigabyte GA-586IP",
        .internal_name = "586ip",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_INTEL_430NX,
        .init = machine_at_586ip_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 3520,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 1.5
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 262144,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMI MegaKey KBC firmware. */
    {
        .name = "[i430NX] Teknor TEK-932",
        .internal_name = "tek932",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_INTEL_430NX,
        .init = machine_at_tek932_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 3520,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 1.5
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_IDE | MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 262144,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 430FX */
    /* Uses an ACER/NEC 90M002A (UPD82C42C, 8042 clone) with unknown firmware (V5.0). */
    {
        .name = "[i430FX] Acer V30",
        .internal_name = "acerv30",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_INTEL_430FX,
        .init = machine_at_acerv30_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 2.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMIKey F KBC firmware. */
    {
        .name = "[i430FX] AMI Apollo",
        .internal_name = "apollo",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_INTEL_430FX,
        .init = machine_at_apollo_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 2.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI . */
    {
        .name = "[i430FX] Intel Advanced/ZP",
        .internal_name = "zappa",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_INTEL_430FX,
        .init = machine_at_zappa_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86, CPU_Cx6x86),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 2.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* The BIOS sends KBC command B3 which indicates an AMI (or VIA VT82C42N) KBC. */
    {
        .name = "[i430FX] NEC PowerMate V",
        .internal_name = "powermatev",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_INTEL_430FX,
        .init = machine_at_powermatev_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 2.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMIKey Z(!) KBC firmware. */
    {
        .name = "[i430FX] TriGem Hawk",
        .internal_name = "hawk",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_INTEL_430FX,
        .init = machine_at_hawk_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 2.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* KBC On-Chip the VT82C406MV. */
    {
        .name = "[i430FX] FIC PT-2000",
        .internal_name = "pt2000",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_INTEL_430FX,
        .init = machine_at_pt2000_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 2.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* OPTi 596/597 */
    /* This uses an AMI KBC firmware in PS/2 mode (it sends command A5 with the
       PS/2 "Load Security" meaning), most likely MegaKey as it sends command AF
       (Set Extended Controller RAM) just like the later Intel AMI BIOS'es. */
    {
        .name = "[OPTi 597] TMC PAT54PV",
        .internal_name = "pat54pv",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_OPTI_547_597,
        .init = machine_at_pat54pv_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3520,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 1.5
        },
        .bus_flags = MACHINE_VLB,
        .flags = MACHINE_APM,
        .ram = {
            .min = 2048,
            .max = 65536,
            .step = 2048
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* OPTi 596/597/822 */
    {
        .name = "[OPTi 597] Shuttle HOT-543",
        .internal_name = "hot543",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_OPTI_547_597,
        .init = machine_at_hot543_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3520,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 2.0
        },
        .bus_flags = MACHINE_PCIV,
        .flags = MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    {
        .name = "[OPTi 597] Northgate Computer Systems Elegance Pentium 90",
        .internal_name = "ncselp90",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_OPTI_547_597,
        .init = machine_at_ncselp90_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3520,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 2.0
        },
        .bus_flags = MACHINE_PS2_PCIV,
        .flags = MACHINE_APM | MACHINE_IDE_DUAL | MACHINE_SUPER_IO,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* SiS 85C50x */
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    {
        .name = "[SiS 501] ASUS PCI/I-P54SP4",
        .internal_name = "p54sp4",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_SIS_501,
        .init = machine_at_p54sp4_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86),
            .min_bus = 40000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 1.5
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    {
        .name = "[SiS 501] BCM SQ-588",
        .internal_name = "sq588",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_SIS_501,
        .init = machine_at_sq588_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_PENTIUMMMX),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3520,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 1.5
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This machine has a Winbond W83C842 KBC */
    {
        .name = "[SiS 501] Gemlight GMB-P54SPS",
        .internal_name = "p54sps",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_SIS_501,
        .init = machine_at_p54sps_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
        CPU_BLOCK(CPU_PENTIUMMMX),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3520,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 1.5
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = &keyboard_at_ami_device,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    {
        .name = "[SiS 5501] MSI MS-5109",
        .internal_name = "ms5109",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_SIS_5501,
        .init = machine_at_ms5109_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
        CPU_BLOCK(CPU_PENTIUMMMX),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3520,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 1.5
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMIKey Z(!) KBC firmware. */
    {
        .name = "[SiS 5501] TriGem Torino",
        .internal_name = "torino",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_SIS_5501,
        .init = machine_at_torino_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
        CPU_BLOCK(CPU_PENTIUMMMX),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3520,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 1.5
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_VIDEO | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &tgui9660_onboard_pci_device,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* UMC 889x */
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    {
        .name = "[UMC 889x] Shuttle HOT-539",
        .internal_name = "hot539",
        .type = MACHINE_TYPE_SOCKET5,
        .chipset = MACHINE_CHIPSET_UMC_UM8890BF,
        .init = machine_at_hot539_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86),
            .min_bus = 40000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3600,
            .min_multi = 1.5,
            .max_multi = 2.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 262144,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* Socket 7 (Single Voltage) machines */
    /* 430FX */
    /* This has an AMIKey-2, which is an updated version of type 'H'.
       This also seems to be revision 2.1 with the FDC37C665 SIO. */
    {
        .name = "[i430FX] ASUS P/I-P55TP4XE",
        .internal_name = "p54tp4xe",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_INTEL_430FX,
        .init = machine_at_p54tp4xe_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3600,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    {
        .name = "[i430FX] ASUS P/I-P55TP4XE (MR BIOS)",
        .internal_name = "p54tp4xe_mr",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_INTEL_430FX,
        .init = machine_at_p54tp4xe_mr_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3600,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMIKey H KBC firmware. The KBC itself seems to differ between an AMIKEY-2 and a Winbond W83C42. */
    {
        .name = "[i430FX] DataExpert EXP8551",
        .internal_name = "exp8551",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_INTEL_430FX,
        .init = machine_at_exp8551_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI . */
    {
        .name = "[i430FX] Gateway 2000 Thor",
        .internal_name = "gw2katx",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_INTEL_430FX,
        .init = machine_at_gw2katx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86, CPU_Cx6x86),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_GAMEPORT,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a SM(S)C FDC37C932 Super I/O chip with on-chip KBC with AMI
       MegaKey (revision '5') KBC firmware. */
    {
        .name = "[i430FX] HP Vectra VL 5 Series 4",
        .internal_name = "vectra54",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_INTEL_430FX,
        .init = machine_at_vectra54_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 2.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_VIDEO | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 511,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &s3_phoenix_trio64_onboard_pci_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI . */
    {
        .name = "[i430FX] Intel Advanced/ATX",
        .internal_name = "thor",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_INTEL_430FX,
        .init = machine_at_thor_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86, CPU_Cx6x86),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_VIDEO | MACHINE_APM | MACHINE_GAMEPORT,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &s3_phoenix_trio64vplus_onboard_pci_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI . */
    {
        .name = "[i430FX] Intel Advanced/ATX (MR BIOS)",
        .internal_name = "mrthor",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_INTEL_430FX,
        .init = machine_at_mrthor_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_GAMEPORT,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI . */
    {
        .name = "[i430FX] Intel Advanced/EV",
        .internal_name = "endeavor",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_INTEL_430FX,
        .init = machine_at_endeavor_init,
        .p1_handler = NULL,
        .gpio_handler = machine_at_endeavor_gpio_handler,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86, CPU_Cx6x86),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_VIDEO | MACHINE_SOUND | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &s3_phoenix_trio64_onboard_pci_device,
        .snd_device = &sb_vibra16s_onboard_device,
        .net_device = NULL
    },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    {
        .name = "[i430FX] MSI MS-5119",
        .internal_name = "ms5119",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_INTEL_430FX,
        .init = machine_at_ms5119_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2500,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This most likely uses AMI MegaKey KBC firmware as well due to having the same
       Super I/O chip (that has the KBC firmware on it) as eg. the Advanced/EV. */
    {
        .name = "[i430FX] Packard Bell PB640",
        .internal_name = "pb640",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_INTEL_430FX,
        .init = machine_at_pb640_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86, CPU_Cx6x86),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_VIDEO | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &gd5440_onboard_pci_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a VIA VT82C42N KBC. */
    {
        .name = "[i430FX] PC Partner MB500N",
        .internal_name = "mb500n",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_INTEL_430FX,
        .init = machine_at_mb500n_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has an AMI MegaKey 'H' KBC firmware (1992). */
    {
        .name = "[i430FX] QDI FMB",
        .internal_name = "fmb",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_INTEL_430FX,
        .init = machine_at_fmb_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_WINCHIP, CPU_WINCHIP2, CPU_Cx6x86, CPU_Cx6x86L, CPU_Cx6x86MX),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_GAMEPORT,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 430HX */
    /* Has SST Flash. */
    /* Has a SM(S)C FDC37C935 Super I/O chip with on-chip KBC with Phoenix
       MultiKey/42 (version 1.38) KBC firmware. */
    {
        .name = "[i430HX] Acer V35N",
        .internal_name = "acerv35n",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_INTEL_430HX,
        .init = machine_at_acerv35n_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3450,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 4096,
            .max = 524288,
            .step = 4096
        },
        .nvrmask = 511,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMIKey-2 or VIA VT82C42N KBC (depending on the revision) with AMIKEY 'F' KBC firmware. */
    {
        .name = "[i430HX] AOpen AP53",
        .internal_name = "ap53",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_INTEL_430HX,
        .init = machine_at_ap53_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3450,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 2.5
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 4096,
            .max = 524288,
            .step = 4096
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* [TEST] Has a VIA 82C42N KBC, with AMIKey F KBC firmware. */
    {
        .name = "[i430HX] Biostar MB-8500TUC",
        .internal_name = "8500tuc",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_INTEL_430HX,
        .init = machine_at_8500tuc_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 4096,
            .max = 524288,
            .step = 4096
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 430VX */
    /* Has a SM(S)C FDC37C932FR Super I/O chip with on-chip KBC with AMI
       MegaKey (revision '5') KBC firmware. */
    {
        .name = "[i430VX] Gateway 2000 Mailman",
        .internal_name = "gw2kma",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_INTEL_430VX,
        .init = machine_at_gw2kma_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86, CPU_Cx6x86),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_GAMEPORT | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 4096
        },
        .nvrmask = 511,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* SiS 5501 */
    /* Has the Lance LT38C41 KBC. */
    {
        .name = "[SiS 5501] Chaintech 5SBM2 (M103)",
        .internal_name = "5sbm2",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_SIS_5501,
        .init = machine_at_5sbm2_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 262144,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* SiS 5511 */
    /* Has AMIKey H KBC firmware (AMIKey-2). */
    {
        .name = "[SiS 5511] AOpen AP5S",
        .internal_name = "ap5s",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_SIS_5511,
        .init = machine_at_ap5s_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has an SMC FDC37C669QF Super I/O. */
    {
        .name = "[SiS 5511] IBM PC 140 (type 6260)",
        .internal_name = "pc140_6260",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_SIS_5511,
        .init = machine_at_pc140_6260_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_WINCHIP, CPU_WINCHIP2, CPU_Cx6x86, CPU_Cx6x86L, CPU_Cx6x86MX, CPU_PENTIUMMMX),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_VIDEO | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &gd5436_onboard_pci_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMIKey H KBC firmware (AMIKey-2). */
    {
        .name = "[SiS 5511] MSI MS-5124",
        .internal_name = "ms5124",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_SIS_5511,
        .init = machine_at_ms5124_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has Megakey 'R' KBC */
    {
        .name = "[SiS 5511] AMI Atlas PCI-II",
        .internal_name = "amis727",
        .type = MACHINE_TYPE_SOCKET7_3V,
        .chipset = MACHINE_CHIPSET_SIS_5511,
        .init = machine_at_amis727_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 3380,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* Socket 7 (Dual Voltage) machines */
    /* 430HX */
    /* Has a SM(S)C FDC37C935 Super I/O chip with on-chip KBC with Phoenix
       MultiKey/42 (version 1.38) KBC firmware. */
    {
        .name = "[i430HX] Acer AcerPower Ultima",
        .internal_name = "acerm3a",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430HX,
        .init = machine_at_acerm3a_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_Cx6x86MX),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2800,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_GAMEPORT | MACHINE_USB, /* Machine has internal SCSI */
        .ram = {
            .min = 4096,
            .max = 524288,
            .step = 4096
        },
        .nvrmask = 511,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMIKey H KBC firmware (AMIKey-2). */
    {
        .name = "[i430HX] ASUS P/I-P55T2P4",
        .internal_name = "p55t2p4",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430HX,
        .init = machine_at_p55t2p4_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 75000000,
            .min_voltage = 2500,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 4.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 4096,
            .max = 524288,
            .step = 4096
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* The base board has a Holtek HT6542B with the AMIKey-2 (updated 'H') KBC firmware. */
    {
        .name = "[i430HX] ASUS P/I-P65UP5 (C-P55T2D)",
        .internal_name = "p65up5_cp55t2d",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430HX,
        .init = machine_at_p65up5_cp55t2d_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2500,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB, /* Machine has AMB */
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 4096,
            .max = 524288,
            .step = 4096
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a SM(S)C FDC37C935 Super I/O chip with on-chip KBC with Phoenix
       MultiKey/42 (version 1.38) KBC firmware. */
    {
        .name = "[i430HX] Micronics M7S-Hi",
        .internal_name = "m7shi",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430HX,
        .init = machine_at_m7shi_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 2800,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_GAMEPORT | MACHINE_USB,
        .ram = {
            .min = 4096,
            .max = 524288,
            .step = 4096
        },
        .nvrmask = 511,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI . */
    {
        .name = "[i430HX] Intel TC430HX (Tucson)",
        .internal_name = "tc430hx",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430HX,
        .init = machine_at_tc430hx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86, CPU_K6, CPU_K6_2, CPU_K6_2C, CPU_K6_3, CPU_K6_2P,
                               CPU_K6_3P, CPU_Cx6x86, CPU_Cx6x86MX, CPU_Cx6x86L),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2800,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_VIDEO | MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_GAMEPORT | MACHINE_USB, /* Has internal sound: Yamaha YMF701-S */
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 4096
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &s3_virge_375_pci_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* OEM version of Intel TC430HX, has AMI MegaKey KBC firmware on the PC87306 Super I/O chip. */
    {
        .name = "[i430HX] Toshiba Infinia 7201",
        .internal_name = "infinia7200",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430HX,
        .init = machine_at_infinia7200_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86, CPU_K6, CPU_K6_2, CPU_K6_2C, CPU_K6_3, CPU_K6_2P,
                               CPU_K6_3P, CPU_Cx6x86, CPU_Cx6x86MX, CPU_Cx6x86L),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2800,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_VIDEO | MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_GAMEPORT | MACHINE_USB, /* Has internal sound: Yamaha YMF701-S */
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 4096
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &s3_virge_375_pci_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* OEM-only Intel CU430HX, has AMI MegaKey KBC firmware on the PC87306 Super I/O chip. */
    {
        .name = "[i430HX] Intel CU430HX (Cumberland)",
        .internal_name = "cu430hx",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430HX,
        .init = machine_at_cu430hx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86, CPU_K6, CPU_K6_2, CPU_K6_2C, CPU_K6_3, CPU_K6_2P,
                               CPU_K6_3P, CPU_Cx6x86, CPU_Cx6x86MX, CPU_Cx6x86L),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2800,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_SOUND | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 4096
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = &sb_vibra16c_onboard_device,
        .net_device = NULL
    },
    /* OEM-only Intel CU430HX, has AMI MegaKey KBC firmware on the PC87306 Super I/O chip. */
    {
        .name = "[i430HX] Toshiba Equium 5200D",
        .internal_name = "equium5200",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430HX,
        .init = machine_at_equium5200_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86, CPU_K6, CPU_K6_2, CPU_K6_2C, CPU_K6_3, CPU_K6_2P,
                               CPU_K6_3P, CPU_Cx6x86, CPU_Cx6x86MX, CPU_Cx6x86L),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2800,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_SOUND | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 4096
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = &sb_vibra16c_onboard_device,
        .net_device = NULL
    },
    /* Unknown PS/2 KBC. */
    {
        .name = "[i430HX] Radisys EPC-2102",
        .internal_name = "epc2102",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430HX,
        .init = machine_at_epc2102_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2500,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 4096,
            .max = 524288,
            .step = 4096
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI .
       Yes, this is an Intel AMI BIOS with a fancy splash screen. */
    {
        .name = "[i430HX] Sony Vaio PCV-90",
        .internal_name = "pcv90",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430HX,
        .init = machine_at_pcv90_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86, CPU_K6, CPU_K6_2, CPU_K6_2C, CPU_K6_3, CPU_K6_2P,
                               CPU_K6_3P, CPU_Cx6x86, CPU_Cx6x86MX, CPU_Cx6x86L),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2800,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 4096
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* [TEST] The board doesn't seem to have a KBC at all, which probably means it's an on-chip one on the PC87306 SIO.
       A list on a Danish site shows the BIOS as having a -0 string, indicating non-AMI KBC firmware. */
    {
        .name = "[i430HX] Supermicro P55T2S",
        .internal_name = "p55t2s",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430HX,
        .init = machine_at_p55t2s_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2800,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 4096,
            .max = 524288,
            .step = 4096
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 430VX */
    /* This has the VIA VT82C42N or Holtek HT6542B KBC. */
    {
        .name = "[i430VX] AOpen AP5VM",
        .internal_name = "ap5vm",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430VX,
        .init = machine_at_ap5vm_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2600,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_SCSI | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 4096,
            .max = 131072,
            .step = 4096
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMIKey H KBC firmware (AMIKey-2) on a BestKey KBC. */
    {
        .name = "[i430VX] ASUS P/I-P55TVP4",
        .internal_name = "p55tvp4",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430VX,
        .init = machine_at_p55tvp4_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2500,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB, /* Machine has AMB */
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 4096,
            .max = 131072,
            .step = 4096
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* The BIOS does not send a single non-standard KBC command, so it must have a standard IBM
       PS/2 KBC firmware or a clone thereof. */
    {
        .name = "[i430VX] Azza PT-5IV",
        .internal_name = "5ivg",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430VX,
        .init = machine_at_5ivg_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2500,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 4096,
            .max = 131072,
            .step = 4096
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* [TEST] Has AMIKey 'F' KBC firmware on a VIA VT82C42N KBC. */
    {
        .name = "[i430VX] Biostar MB-8500TVX-A",
        .internal_name = "8500tvxa",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430VX,
        .init = machine_at_8500tvxa_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2600,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 4096,
            .max = 131072,
            .step = 4096
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a SM(S)C FDC37C932QF Super I/O chip with on-chip KBC with AMI
       MegaKey (revision '5') KBC firmware. */
    {
        .name = "[i430VX] Compaq Presario 224x",
        .internal_name = "presario2240",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430VX,
        .init = machine_at_presario2240_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 66666667,
            .min_voltage = 2800,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_VIDEO | MACHINE_APM | MACHINE_ACPI,
        .ram = {
            .min = 16384,
            .max = 49152,
            .step = 4096
        },
        .nvrmask = 511,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &s3_trio64v2_dx_onboard_pci_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a SM(S)C FDC37C931APM Super I/O chip with on-chip KBC with Compaq
       KBC firmware. */
    {
        .name = "[i430VX] Compaq Presario 45xx",
        .internal_name = "presario4500",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430VX,
        .init = machine_at_presario4500_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 66666667,
            .min_voltage = 2800,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_VIDEO | MACHINE_APM | MACHINE_ACPI,
        .ram = {
            .min = 16384,
            .max = 49152,
            .step = 4096
        },
        .nvrmask = 511,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &s3_trio64v2_dx_onboard_pci_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a SM(S)C FDC37C932FR Super I/O chip with on-chip KBC with AMI
       MegaKey (revision '5') KBC firmware. */
    {
        .name = "[i430VX] Dell Dimension XPS Pxxxa/Mxxxa",
        .internal_name = "dellhannibalp",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430VX,
        .init = machine_at_dellhannibalp_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86, CPU_K6, CPU_K6_2, CPU_K6_2C, CPU_K6_3, CPU_K6_2P,
                               CPU_K6_3P, CPU_Cx6x86, CPU_Cx6x86MX, CPU_Cx6x86L),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2500,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 4096
        },
        .nvrmask = 511,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has AMIKey H KBC firmware (AMIKey-2). */
    {
        .name = "[i430VX] ECS P5VX-B",
        .internal_name = "p5vxb",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430VX,
        .init = machine_at_p5vxb_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2500,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 4096,
            .max = 131072,
            .step = 4096
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a SM(S)C FDC37C932FR Super I/O chip with on-chip KBC with AMI
       MegaKey (revision '5') KBC firmware. */
    {
        .name = "[i430VX] Epox P55-VA",
        .internal_name = "p55va",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430VX,
        .init = machine_at_p55va_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 75000000,
            .min_voltage = 2500,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 4096,
            .max = 131072,
            .step = 4096
        },
        .nvrmask = 511,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a SM(S)C FDC37C932FR Super I/O chip with on-chip KBC with AMI
       MegaKey (revision '5') KBC firmware. */
    {
        .name = "[i430VX] Gateway 2000 Hitman",
        .internal_name = "gw2kte",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430VX,
        .init = machine_at_gw2kte_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86, CPU_K6, CPU_K6_2, CPU_K6_2C, CPU_K6_3, CPU_K6_2P,
                               CPU_K6_3P, CPU_Cx6x86, CPU_Cx6x86MX, CPU_Cx6x86L),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2200,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_GAMEPORT | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 4096
        },
        .nvrmask = 511,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a SM(S)C FDC37C935 Super I/O chip with on-chip KBC with Phoenix
       MultiKey/42 (version 1.38) KBC firmware. */
    {
        .name = "[i430VX] HP Brio 80xx",
        .internal_name = "brio80xx",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430VX,
        .init = machine_at_brio80xx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 66666667,
            .min_voltage = 2200,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 4096
        },
        .nvrmask = 511,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI . */
    {
        .name = "[i430VX] Packard Bell Multimedia C110",
        .internal_name = "pb680",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430VX,
        .init = machine_at_pb680_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86, CPU_K6, CPU_K6_2, CPU_K6_2C, CPU_K6_3, CPU_K6_2P,
                               CPU_K6_3P, CPU_Cx6x86, CPU_Cx6x86MX, CPU_Cx6x86L),
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2800,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_VIDEO | MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 131072,
            .step = 4096
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &s3_phoenix_trio64vplus_onboard_pci_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a SM(S)C FDC37C935 Super I/O chip with on-chip KBC with Phoenix
       MultiKey/42 (version 1.38) KBC firmware. */
    {
        .name = "[i430VX] Packard Bell Multimedia M415",
        .internal_name = "pb810",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430VX,
        .init = machine_at_pb810_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2500,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 4.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_SOUND | MACHINE_APM,
        .ram = {
            .min = 4096,
            .max = 131072,
            .step = 4096
        },
        .nvrmask = 511,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has the AMIKey 'H' firmware, possibly AMIKey-2. Photos show it with a BestKey, so it
       likely clones the behavior of AMIKey 'H'. */
    {
        .name = "[i430VX] PC Partner MB520N",
        .internal_name = "mb520n",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430VX,
        .init = machine_at_mb520n_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2600,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 4096,
            .max = 131072,
            .step = 4096
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has a Holtek KBC and the BIOS does not send a single non-standard KBC command, so it
       must be an ASIC that clones the standard IBM PS/2 KBC. */
    {
        .name = "[i430VX] Shuttle HOT-557",
        .internal_name = "430vx",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430VX,
        .init = machine_at_i430vx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2500,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_GAMEPORT | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 4096,
            .max = 131072,
            .step = 4096
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 430TX */
    /* The BIOS sends KBC command B8, CA, and CB, so it has an AMI KBC firmware. */
    {
        .name = "[i430TX] ADLink NuPRO-591/592",
        .internal_name = "nupro592",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430TX,
        .init = machine_at_nupro592_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 66666667,
            .min_voltage = 1900,
            .max_voltage = 2800,
            .min_multi = 1.5,
            .max_multi = 5.5
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_VIDEO,
        .ram = {
            .min = 8192,
            .max = 262144,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &chips_69000_onboard_device,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has the AMIKey KBC firmware, which is an updated 'F' type (YM430TX is based on the TX97). */
    {
        .name = "[i430TX] ASUS TX97",
        .internal_name = "tx97",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430TX,
        .init = machine_at_tx97_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 75000000,
            .min_voltage = 2500,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 262144,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* [TEST] Has AMI Megakey '5' KBC firmware on the SM(S)C FDC37C67x Super I/O chip. */
    {
        .name = "[i430TX] Gateway E-1000",
        .internal_name = "tomahawk",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430TX,
        .init = machine_at_tomahawk_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 2100,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 5.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_VIDEO | MACHINE_SOUND | MACHINE_NIC | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 262144,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &s3_trio64v2_dx_onboard_pci_device,
        .snd_device = &cs4236b_device,
        .net_device = &pcnet_am79c973_onboard_device
    },
#ifdef USE_AN430TX
    /* This has the Phoenix MultiKey KBC firmware. */
    {
        .name = "[i430TX] Intel AN430TX (Anchorage)",
        .internal_name = "an430tx",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430TX,
        .init = machine_at_an430tx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86, CPU_K6, CPU_K6_2, CPU_K6_2C, CPU_K6_3, CPU_K6_2P,
                               CPU_K6_3P, CPU_Cx6x86, CPU_Cx6x86MX, CPU_Cx6x86L),
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 2800,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI,
        .ram = {
            .min = 8192,
            .max = 262144,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
#endif /* USE_AN430TX */
    /* This has the AMIKey KBC firmware, which is an updated 'F' type. */
    {
        .name = "[i430TX] Intel YM430TX (Yamamoto)",
        .internal_name = "ym430tx",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430TX,
        .init = machine_at_ym430tx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_K5, CPU_5K86, CPU_K6, CPU_K6_2, CPU_K6_2C, CPU_K6_3, CPU_K6_2P,
                               CPU_K6_3P, CPU_Cx6x86, CPU_Cx6x86MX, CPU_Cx6x86L),
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 2800,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 4096,
            .max = 262144,
            .step = 4096
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
	/* PhoenixBIOS 4.0 Rel 6.0 for 430TX, has onboard Yamaha YMF701 which is not emulated yet. */
    /* Has a SM(S)C FDC37C935 Super I/O chip with on-chip KBC with Phoenix
       MultiKey/42 (version 1.38) KBC firmware. */
    {
        .name = "[i430TX] Micronics Thunderbolt",
        .internal_name = "thunderbolt",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430TX,
        .init = machine_at_thunderbolt_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK(CPU_WINCHIP, CPU_WINCHIP2),
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 2500,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_GAMEPORT | MACHINE_USB, /* Machine has internal sound: Yamaha YMF701-S */
        .ram = {
            .min = 8192,
            .max = 262144,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a SM(S)C FDC37C67x Super I/O chip with on-chip KBC with Phoenix or
       AMIKey-2 KBC firmware. */
    {
        .name = "[i430TX] NEC Mate NX MA23C",
        .internal_name = "ma23c",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430TX,
        .init = machine_at_ma23c_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 2700,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 262144,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* The BIOS sends KBC command BB and expects it to output a byte, which is AMI KBC behavior.
       A picture shows a VIA VT82C42N KBC though, so it could be a case of that KBC with AMI firmware. */
    {
        .name = "[i430TX] PC Partner MB540N",
        .internal_name = "mb540n",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430TX,
        .init = machine_at_mb540n_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 2700,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 4096,
            .max = 262144,
            .step = 4096
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Award BIOS, PS2, EDO, SDRAM, 4 PCI, 4 ISA, VIA VT82C42N KBC */
    {
        .name = "[i430TX] Soltek SL-56A5",
        .internal_name = "56a5",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430TX,
        .init = machine_at_56a5_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 2800,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 5.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 4096,
            .max = 262144,
            .step = 4096
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* [TEST] Has AMIKey 'H' KBC firmware. */
    {
        .name = "[i430TX] Supermicro P5MMS98",
        .internal_name = "p5mms98",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430TX,
        .init = machine_at_p5mms98_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 75000000,
            .min_voltage = 2100,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 4096,
            .max = 262144,
            .step = 4096
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* [TEST] Has AMIKey 'H' KBC firmware. */
    {
        .name = "[i430TX] TriGem RD535 (Richmond)",
        .internal_name = "richmond",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_INTEL_430TX,
        .init = machine_at_richmond_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 2100,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 262144,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* Apollo VPX */
    /* Has the VIA VT82C586B southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    {
        .name = "[VIA VPX] FIC VA-502",
        .internal_name = "ficva502",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_VIA_APOLLO_VPX,
        .init = machine_at_ficva502_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 75000000,
            .min_voltage = 2800,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* Apollo VP3 */
    /* Has the VIA VT82C586B southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    {
        .name = "[VIA VP3] FIC PA-2012",
        .internal_name = "ficpa2012",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_VIA_APOLLO_VP3,
        .init = machine_at_ficpa2012_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 55000000,
            .max_bus = 75000000,
            .min_voltage = 2100,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 5.5
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 1048576,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has the VIA VT82C586B southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    {
        .name = "[VIA VP3] PC Partner VIA809DS",
        .internal_name = "via809ds",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_VIA_APOLLO_VP3,
        .init = machine_at_via809ds_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 75000000,
            .min_voltage = 2100,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 5.5
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 1048576,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    
    /* SiS 5571 */
    /* Has the SiS 5571 chipset with on-chip KBC. */
    {
        .name = "[SiS 5571] Daewoo CD520",
        .internal_name = "cb52xsi",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_SIS_5571,
        .init = machine_at_cb52xsi_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 75000000,
            .min_voltage = 2800,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 262144,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has the SiS 5571 chipset with on-chip KBC. */
    {
        .name = "[SiS 5571] MSI MS-5146",
        .internal_name = "ms5146",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_SIS_5571,
        .init = machine_at_ms5146_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 75000000,
            .min_voltage = 2800,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_GAMEPORT | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 262144,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has the SiS 5571 chipset with on-chip KBC. */
    {
        .name = "[SiS 5571] Rise R534F",
        .internal_name = "r534f",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_SIS_5571,
        .init = machine_at_r534f_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 55000000,
            .max_bus = 83333333,
            .min_voltage = 2500,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 393216,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* SiS 5581 */
    /* Has the SiS 5581 chipset with on-chip KBC. */
    {
        .name = "[SiS 5581] ASUS SP97-XV",
        .internal_name = "sp97xv",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_SIS_5581,
        .init = machine_at_sp97xv_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 75000000,
            .min_voltage = 2500,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 1572864,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has the SiS 5581 chipset with on-chip KBC. */
    {
        .name = "[SiS 5581] BCM SQ-578",
        .internal_name = "sq578",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_SIS_5581,
        .init = machine_at_sq578_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 75000000,
            .min_voltage = 2500,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 1572864,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* SiS 5591 */
    /* Has the SiS 5591 chipset with on-chip KBC. */
    {
        .name = "[SiS 5591] MSI MS-5172",
        .internal_name = "ms5172",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_SIS_5591,
        .init = machine_at_ms5172_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 75000000,
            .min_voltage = 2500,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 786432,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* ALi ALADDiN IV+ */
    /* Has the ALi M1543 southbridge with on-chip KBC. */
    {
        .name = "[ALi ALADDiN IV+] MSI MS-5164",
        .internal_name = "ms5164",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_ALI_ALADDIN_IV_PLUS,
        .init = machine_at_ms5164_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 83333333,
            .min_voltage = 2100,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 1048576,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has the ALi M1543 southbridge with on-chip KBC. */
    {
        .name = "[ALi ALADDiN IV+] PC Chips M560",
        .internal_name = "m560",
        .type = MACHINE_TYPE_SOCKET7,
        .chipset = MACHINE_CHIPSET_ALI_ALADDIN_IV_PLUS,
        .init = machine_at_m560_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 83333333,
            .min_voltage = 2500,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 3.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 786432,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* Super Socket 7 machines */
    /* ALi ALADDiN V */
    /* Has the ALi M1543C southbridge with on-chip KBC. */
    {
        .name = "[ALi ALADDiN V] ASUS P5A",
        .internal_name = "p5a",
        .type = MACHINE_TYPE_SOCKETS7,
        .chipset = MACHINE_CHIPSET_ALI_ALADDIN_V,
        .init = machine_at_p5a_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 120000000,
            .min_voltage = 2000,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 5.5
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_GAMEPORT | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 1572864,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Is the exact same as the Matsonic MS6260S. Has the ALi M1543C southbridge
       with on-chip KBC. */
    {
        .name = "[ALi ALADDiN V] PC Chips M579",
        .internal_name = "m579",
        .type = MACHINE_TYPE_SOCKETS7,
        .chipset = MACHINE_CHIPSET_ALI_ALADDIN_V,
        .init = machine_at_m579_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 100000000,
            .min_voltage = 2000,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 5.5
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 1024,
            .max = 1572864,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* M1534c kbc */
    {
        .name = "[ALi ALADDiN V] Gateway Lucas",
        .internal_name = "gwlucas",
        .type = MACHINE_TYPE_SOCKETS7,
        .chipset = MACHINE_CHIPSET_ALI_ALADDIN_V,
        .init = machine_at_gwlucas_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 100000000,
            .min_voltage = 2000,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 5.5
        },
        .bus_flags = MACHINE_PS2_PCIONLY | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_SOUND | MACHINE_APM | MACHINE_ACPI | MACHINE_GAMEPORT | MACHINE_USB, /* Has internal video: ATI 3D Rage Pro Turbo AGP and sound: Ensoniq ES1373 */
        .ram = {
            .min = 8192,
            .max = 262144,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = &es1373_onboard_device,
        .net_device = NULL
    },
    /* Has the ALi M1543C southbridge with on-chip KBC. */
    {
        .name = "[ALi ALADDiN V] Gigabyte GA-5AA",
        .internal_name = "5aa",
        .type = MACHINE_TYPE_SOCKETS7,
        .chipset = MACHINE_CHIPSET_ALI_ALADDIN_V,
        .init = machine_at_5aa_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 140000000,
            .min_voltage = 1300,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 5.5
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 1024,
            .max = 1572864,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has the ALi M1543C southbridge with on-chip KBC. */
    {
        .name = "[ALi ALADDiN V] Gigabyte GA-5AX",
        .internal_name = "5ax",
        .type = MACHINE_TYPE_SOCKETS7,
        .chipset = MACHINE_CHIPSET_ALI_ALADDIN_V,
        .init = machine_at_5ax_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 140000000,
            .min_voltage = 1300,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 5.5
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 1024,
            .max = 1572864,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* Apollo MVP3 */
    /* Has the VIA VT82C586B southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    {
        .name = "[VIA MVP3] AOpen AX59 Pro",
        .internal_name = "ax59pro",
        .type = MACHINE_TYPE_SOCKETS7,
        .chipset = MACHINE_CHIPSET_VIA_APOLLO_MVP3,
        .init = machine_at_ax59pro_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 124242424,
            .min_voltage = 1300,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 5.5
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 1048576,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has the VIA VT82C586B southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    {
        .name = "[VIA MVP3] FIC VA-503+",
        .internal_name = "ficva503p",
        .type = MACHINE_TYPE_SOCKETS7,
        .chipset = MACHINE_CHIPSET_VIA_APOLLO_MVP3,
        .init = machine_at_mvp3_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 124242424,
            .min_voltage = 2000,
            .max_voltage = 3200,
            .min_multi = 1.5,
            .max_multi = 5.5
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 1048576,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has the VIA VT82C686A southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    {
        .name = "[VIA MVP3] FIC VA-503A",
        .internal_name = "ficva503a",
        .type = MACHINE_TYPE_SOCKETS7,
        .chipset = MACHINE_CHIPSET_VIA_APOLLO_MVP3,
        .init = machine_at_ficva503a_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 124242424,
            .min_voltage = 1800,
            .max_voltage = 3100,
            .min_multi = 1.5,
            .max_multi = 5.5
        },
        .bus_flags = MACHINE_PS2_A97 | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_SOUND | MACHINE_APM | MACHINE_ACPI | MACHINE_GAMEPORT | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 786432,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has the VIA VT82C686A southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    {
        .name = "[VIA MVP3] Soyo 5EMA PRO",
        .internal_name = "5emapro",
        .type = MACHINE_TYPE_SOCKETS7,
        .chipset = MACHINE_CHIPSET_VIA_APOLLO_MVP3,
        .init = machine_at_5emapro_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 124242424,
            .min_voltage = 2000,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 5.5
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 786432,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* SiS 5591 */
    /* Has the SiS 5591 chipset with on-chip KBC. */
    {
        .name = "[SiS 5591] Gigabyte GA-5SG100",
        .internal_name = "5sg100",
        .type = MACHINE_TYPE_SOCKETS7,
        .chipset = MACHINE_CHIPSET_SIS_5591,
        .init = machine_at_5sg100_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET5_7,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 100000000,
            .min_voltage = 2000,
            .max_voltage = 3520,
            .min_multi = 1.5,
            .max_multi = 5.5
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 786432,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* Socket 8 machines */
    /* 450KX */
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    {
        .name = "[i450KX] AOpen AP61",
        .internal_name = "ap61",
        .type = MACHINE_TYPE_SOCKET8,
        .chipset = MACHINE_CHIPSET_INTEL_450KX,
        .init = machine_at_ap61_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET8,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 2100,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_PCI,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    {
        .name = "[i450KX] ASUS P/I-P6RP4",
        .internal_name = "p6rp4",
        .type = MACHINE_TYPE_SOCKET8,
        .chipset = MACHINE_CHIPSET_INTEL_450KX,
        .init = machine_at_p6rp4_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET8,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 2100,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_PCI, /* Machine has AMB */
        .flags = MACHINE_IDE_DUAL | MACHINE_APM,
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 440FX */
    /* Has a SM(S)C FDC37C935 Super I/O chip with on-chip KBC with Phoenix
       MultiKey/42 (version 1.38) KBC firmware. */
    {
        .name = "[i440FX] Acer V60N",
        .internal_name = "acerv60n",
        .type = MACHINE_TYPE_SOCKET8,
        .chipset = MACHINE_CHIPSET_INTEL_440FX,
        .init = machine_at_acerv60n_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET8,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 2500,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 786432,
            .step = 8192
        },
        .nvrmask = 511,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* The base board has a Holtek HT6542B with AMIKey-2 (updated 'H') KBC firmware. */
    {
        .name = "[i440FX] ASUS P/I-P65UP5 (C-P6ND)",
        .internal_name = "p65up5_cp6nd",
        .type = MACHINE_TYPE_SOCKET8,
        .chipset = MACHINE_CHIPSET_INTEL_440FX,
        .init = machine_at_p65up5_cp6nd_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET8,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 2100,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB, /* Machine has AMB */
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 1048576,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a VIA VT82C42N with likely AMIKey 'F' KBC firmware. */
    {
        .name = "[i440FX] Biostar MB-8600TTC",
        .internal_name = "8600ttc",
        .type = MACHINE_TYPE_SOCKET8,
        .chipset = MACHINE_CHIPSET_INTEL_440FX,
        .init = machine_at_8600ttc_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET8,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 2900,
            .max_voltage = 3300,
            .min_multi = 2.0,
            .max_multi = 5.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* It's a Intel VS440FX with a Gateway 2000 OEM BIOS */
    {
        .name = "[i440FX] Gateway 2000 Venus",
        .internal_name = "gw2kvenus",
        .type = MACHINE_TYPE_SOCKET8,
        .chipset = MACHINE_CHIPSET_INTEL_440FX,
        .init = machine_at_gw2kvenus_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET8,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 2100,
            .max_voltage = 3500,
            .min_multi = 2.0,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_SOUND | MACHINE_APM | MACHINE_GAMEPORT | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = &cs4236_onboard_device,
        .net_device = NULL
    },
    /* Has the AMIKey-2 (updated 'H') KBC firmware. */
    {
        .name = "[i440FX] Gigabyte GA-686NX",
        .internal_name = "686nx",
        .type = MACHINE_TYPE_SOCKET8,
        .chipset = MACHINE_CHIPSET_INTEL_440FX,
        .init = machine_at_686nx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET8,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 2100,
            .max_voltage = 3500,
            .min_multi = 2.0,
            .max_multi = 5.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI . */
    {
        .name = "[i440FX] Intel AP440FX",
        .internal_name = "ap440fx",
        .type = MACHINE_TYPE_SOCKET8,
        .chipset = MACHINE_CHIPSET_INTEL_440FX,
        .init = machine_at_ap440fx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET8,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 2100,
            .max_voltage = 3500,
            .min_multi = 2.0,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_SOUND | MACHINE_VIDEO | MACHINE_USB, /* Machine has internal video: S3 ViRGE/DX and sound: Crystal CS4236B */
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &s3_virge_325_onboard_pci_device,
        .snd_device = &cs4236b_onboard_device,
        .net_device = NULL
    },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI . */
    {
        .name = "[i440FX] Intel VS440FX",
        .internal_name = "vs440fx",
        .type = MACHINE_TYPE_SOCKET8,
        .chipset = MACHINE_CHIPSET_INTEL_440FX,
        .init = machine_at_vs440fx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET8,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 2100,
            .max_voltage = 3500,
            .min_multi = 2.0,
            .max_multi = 3.5
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_SOUND | MACHINE_APM | MACHINE_GAMEPORT | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = &cs4236_onboard_device,
        .net_device = NULL
    },
    /* Has the AMIKey-2 (updated 'H') KBC firmware. */
    {
        .name = "[i440FX] LG IBM Multinet x61 (MSI MS-6106)",
        .internal_name = "lgibmx61",
        .type = MACHINE_TYPE_SOCKET8,
        .chipset = MACHINE_CHIPSET_INTEL_440FX,
        .init = machine_at_lgibmx61_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET8,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 2500,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB, /* Machine has internal SCSI: Adaptec AIC-78xx */
        .ram = {
            .min = 40960,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a SM(S)C FDC37C935 Super I/O chip with on-chip KBC with Phoenix
       MultiKey/42 (version 1.38) KBC firmware. */
    {
        .name = "[i440FX] Micronics M6Mi",
        .internal_name = "m6mi",
        .type = MACHINE_TYPE_SOCKET8,
        .chipset = MACHINE_CHIPSET_INTEL_440FX,
        .init = machine_at_m6mi_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET8,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 2900,
            .max_voltage = 3300,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_GAMEPORT | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 786432,
            .step = 8192
        },
        .nvrmask = 511,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a VIA VT82C42N KBC with likely AMI MegaKey firmware. */
    {
        .name = "[i440FX] PC Partner MB600N",
        .internal_name = "mb600n",
        .type = MACHINE_TYPE_SOCKET8,
        .chipset = MACHINE_CHIPSET_INTEL_440FX,
        .init = machine_at_mb600n_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET8,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 66666667,
            .min_voltage = 2100,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* Slot 1 machines */
    /* ALi ALADDiN V */
    /* Has the ALi M1543C southbridge with on-chip KBC. */
    {
        .name = "[ALi ALADDiN-PRO II] PC Chips M729",
        .internal_name = "m729",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_ALI_ALADDIN_PRO_II,
        .init = machine_at_m729_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 100000000,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_GAMEPORT | MACHINE_USB, /* Machine has internal sound: C-Media CMI8330 */
        .ram = {
            .min = 1024,
            .max = 1572864,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 440FX */
    /* The base board has a Holtek HT6542B KBC with AMIKey-2 (updated 'H') KBC firmware. */
    {
        .name = "[i440FX] ASUS P/I-P65UP5 (C-PKND)",
        .internal_name = "p65up5_cpknd",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_INTEL_440FX,
        .init = machine_at_p65up5_cpknd_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 66666667,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 1048576,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* This has a Holtek KBC and the BIOS does not send a single non-standard KBC command, so it
       must be an ASIC that clones the standard IBM PS/2 KBC. */
    {
        .name = "[i440FX] ASUS KN97",
        .internal_name = "kn97",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_INTEL_440FX,
        .init = machine_at_kn97_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 83333333,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 786432,
            .step = 8192
        },
        .nvrmask = 127,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 440LX */
    /* Has a Winbond W83977TF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[i440LX] ABIT LX6",
        .internal_name = "lx6",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_INTEL_440LX,
        .init = machine_at_lx6_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 60000000,
            .max_bus = 83333333,
            .min_voltage = 1500,
            .max_voltage = 3500,
            .min_multi = 2.0,
            .max_multi = 5.5
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 1048576,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a SM(S)C FDC37C935 Super I/O chip with on-chip KBC with Phoenix
       MultiKey/42 (version 1.38) KBC firmware. */
    {
        .name = "[i440LX] Micronics Spitfire",
        .internal_name = "spitfire",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_INTEL_440LX,
        .init = machine_at_spitfire_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 66666667,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 1048576,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a SM(S)C FDC37C67x Super I/O chip with on-chip KBC with Phoenix or
       AMIKey-2 KBC firmware. */
    {
        .name = "[i440LX] NEC Mate NX MA30D/23D",
        .internal_name = "ma30d",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_INTEL_440LX,
        .init = machine_at_ma30d_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 66666667,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 786432,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 440EX */
    /* Has a Winbond W83977TF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[i440EX] QDI EXCELLENT II",
        .internal_name = "p6i440e2",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_INTEL_440EX,
        .init = machine_at_p6i440e2_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 83333333,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 3.0,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 440BX */
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[i440BX] ASUS P2B-LS",
        .internal_name = "p2bls",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_INTEL_440BX,
        .init = machine_at_p2bls_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 50000000,
            .max_bus = 112121212,
            .min_voltage = 1300,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB, /* Machine has internal SCSI: Adaptec AIC-7890AB */
        .ram = {
            .min = 8192,
            .max = 1048576,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[i440BX] ASUS P3B-F",
        .internal_name = "p3bf",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_INTEL_440BX,
        .init = machine_at_p3bf_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 150000000,
            .min_voltage = 1300,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 1048576,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */

{
        .name = "[i440BX] ABIT BX6",
        .internal_name = "bx6",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_INTEL_440BX,
        .init = machine_at_bx6_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 133333333,
            .min_voltage = 1500,
            .max_voltage = 3500,
            .min_multi = 2.0,
            .max_multi = 5.5
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    {
        .name = "[i440BX] ABIT BF6",
        .internal_name = "bf6",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_INTEL_440BX,
        .init = machine_at_bf6_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 133333333,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 786432,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a Winbond W83977TF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[i440BX] AOpen AX6BC",
        .internal_name = "ax6bc",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_INTEL_440BX,
        .init = machine_at_ax6bc_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 112121212,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 786432,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a Winbond W83977TF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[i440BX] Gigabyte GA-686BX",
        .internal_name = "686bx",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_INTEL_440BX,
        .init = machine_at_686bx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 100000000,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 1048576,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a SM(S)C FDC37M60x Super I/O chip with on-chip KBC with most likely
       AMIKey-2 KBC firmware. */
    {
        .name = "[i440BX] HP Vectra VEi 8",
        .internal_name = "vei8",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_INTEL_440BX,
        .init = machine_at_vei8_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 100000000,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_GAMEPORT | MACHINE_USB, /* Machine has internal video: Matrox MGA-G200 and sound: Crystal CS4820 */
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a Winbond W83977TF Super I/O chip with on-chip KBC with AMIKey-2 (updated 'H') KBC firmware. */
    {
        .name = "[i440BX] LG IBM Multinet i x7G (MSI MS-6119)",
        .internal_name = "lgibmx7g",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_INTEL_440BX,
        .init = machine_at_lgibmx7g_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 100000000,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 786432,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a National Semiconductors PC87309 Super I/O chip with on-chip KBC
       with most likely AMIKey-2 KBC firmware. */
    {
        .name = "[i440BX] Tyan Tsunami ATX",
        .internal_name = "s1846",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_INTEL_440BX,
        .init = machine_at_s1846_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 112121212,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_SOUND | MACHINE_APM | MACHINE_ACPI | MACHINE_USB, /* Machine has internal sound: Ensoniq ES1371 */
        .ram = {
            .min = 8192,
            .max = 786432,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = &es1371_onboard_device,
        .net_device = NULL
    },
    /* Has a Winbond W83977TF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[i440BX] Supermicro P6SBA",
        .internal_name = "p6sba",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_INTEL_440BX,
        .init = machine_at_p6sba_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 100000000,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 786432,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 440ZX */
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[i440ZX] MSI MS-6168",
        .internal_name = "ms6168",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_INTEL_440ZX,
        .init = machine_at_ms6168_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 100000000,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB, /* AGP is reserved for the internal video */
        .flags = MACHINE_IDE_DUAL | MACHINE_AV | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &voodoo_3_2000_agp_onboard_8m_device,
        .snd_device = &es1373_onboard_device,
        .net_device = NULL
    },
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[i440ZX] Packard Bell Bora Pro",
        .internal_name = "borapro",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_INTEL_440ZX,
        .init = machine_at_borapro_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 100000000,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB, /* AGP is reserved for the internal video */
        .flags = MACHINE_IDE_DUAL | MACHINE_AV | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = &voodoo_3_2000_agp_onboard_8m_device,
        .snd_device = &es1373_onboard_device,
        .net_device = NULL
    },

    /* SMSC VictoryBX-66 */
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[SMSC VictoryBX-66] A-Trend ATC6310BXII",
        .internal_name = "atc6310bxii",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_SMSC_VICTORYBX_66,
        .init = machine_at_atc6310bxii_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 133333333,
            .min_voltage = 1300,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 786432,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* VIA Apollo Pro */
    /* Has the VIA VT82C596B southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    {
        .name = "[VIA Apollo Pro] FIC KA-6130",
        .internal_name = "ficka6130",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_VIA_APOLLO_PRO,
        .init = machine_at_ficka6130_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 100000000,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_GAMEPORT | MACHINE_USB, /* Machine has internal sound: ESS ES1938S */
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[VIA Apollo Pro 133] ASUS P3V133",
        .internal_name = "p3v133",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_VIA_APOLLO_PRO_133,
        .init = machine_at_p3v133_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 150000000,
            .min_voltage = 1300,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 1572864,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[VIA Apollo Pro 133A] ASUS P3V4X",
        .internal_name = "p3v4x",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_VIA_APOLLO_PRO_133A,
        .init = machine_at_p3v4x_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 150000000,
            .min_voltage = 1300,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 2097152,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[VIA Apollo Pro 133A] BCM GT694VA",
        .internal_name = "gt694va",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_VIA_APOLLO_PRO_133A,
        .init = machine_at_gt694va_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 133333333,
            .min_voltage = 1300,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_SOUND | MACHINE_APM | MACHINE_ACPI | MACHINE_GAMEPORT | MACHINE_USB, /* Machine has internal sound: Ensoniq ES1373 */
        .ram = {
            .min = 8192,
            .max = 3145728,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = &es1373_onboard_device,
        .net_device = NULL
    },

    /* SiS (5)600 */
    /* Has the SiS (5)600 chipset with on-chip KBC. */
    {
        .name = "[SiS 5600] Freetech/Flexus P6F99",
        .internal_name = "p6f99",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_SIS_5600,
        .init = machine_at_p6f99_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 100000000,
            .min_voltage = 1300,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_SOUND | MACHINE_APM | MACHINE_ACPI | MACHINE_GAMEPORT | MACHINE_USB, /* Machine has internal sound: Ensoniq ES1373 */
        .ram = {
            .min = 8192,
            .max = 1572864,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = &es1373_onboard_device,
        .net_device = NULL
    },
    /* Has the SiS (5)600 chipset with on-chip KBC. */
    {
        .name = "[SiS 5600] PC Chips M747",
        .internal_name = "m747",
        .type = MACHINE_TYPE_SLOT1,
        .chipset = MACHINE_CHIPSET_SIS_5600,
        .init = machine_at_m747_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 100000000,
            .min_voltage = 1300,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_GAMEPORT | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 1572864,
            .step = 1024
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* Slot 1/2 machines */
    /* 440GX */
    /* Has a National Semiconductors PC87309 Super I/O chip with on-chip KBC
       with most likely AMIKey-2 KBC firmware. */
    {
        .name = "[i440GX] Freeway FW-6400GX",
        .internal_name = "fw6400gx",
        .type = MACHINE_TYPE_SLOT1_2,
        .chipset = MACHINE_CHIPSET_INTEL_440GX,
        .init = machine_at_fw6400gx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1 | CPU_PKG_SLOT2,
            .block = CPU_BLOCK_NONE,
            .min_bus = 100000000,
            .max_bus = 150000000,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 3.0,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_NOISA | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 16384,
            .max = 2097152,
            .step = 16384
        },
        .nvrmask = 511,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* Slot 1/Socket 370 machines */
    /* 440BX */
    /* OEM version of ECS P6BXT-A+ REV 1.3x/2.2x. Has a Winbond W83977EF Super
    I/O chip with on-chip KBC with AMIKey-2 KBC firmware.*/
    {
        .name = "[i440BX] Compaq ProSignia S316/318 (Intel)",
        .internal_name = "prosignias31x_bx",
        .type = MACHINE_TYPE_SLOT1_370,
        .chipset = MACHINE_CHIPSET_VIA_APOLLO_PRO_133,
        .init = machine_at_prosignias31x_bx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1 | CPU_PKG_SOCKET370,
            .block = CPU_BLOCK(CPU_PENTIUMPRO, CPU_CYRIX3S), /* Instability issues with PPro, and garbled text in POST with Cyrix */
            .min_bus = 66666667,
            .max_bus = 100000000,
            .min_voltage = 1300,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_SOUND | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 786432,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = &cmi8738_onboard_device,
        .net_device = NULL
    },
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[i440BX] Tyan Trinity 371",
        .internal_name = "s1857",
        .type = MACHINE_TYPE_SLOT1_370,
        .chipset = MACHINE_CHIPSET_INTEL_440BX,
        .init = machine_at_s1857_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1 | CPU_PKG_SOCKET370,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 133333333,
            .min_voltage = 1300,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_SOUND | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 786432,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = &es1373_onboard_device,
        .net_device = NULL
    },
    /* VIA Apollo Pro */
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[VIA Apollo Pro 133] ECS P6BAT-A+",
        .internal_name = "p6bat",
        .type = MACHINE_TYPE_SLOT1_370,
        .chipset = MACHINE_CHIPSET_VIA_APOLLO_PRO_133,
        .init = machine_at_p6bat_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1 | CPU_PKG_SOCKET370,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 133333333,
            .min_voltage = 1300,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_SOUND | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 1572864,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = &cmi8738_onboard_device,
        .net_device = NULL
    },

    /* Slot 2 machines */
    /* 440GX */
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[i440GX] Gigabyte GA-6GXU",
        .internal_name = "6gxu",
        .type = MACHINE_TYPE_SLOT2,
        .chipset = MACHINE_CHIPSET_INTEL_440GX,
        .init = machine_at_6gxu_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT2,
            .block = CPU_BLOCK_NONE,
            .min_bus = 100000000,
            .max_bus = 100000000,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB, /* Machine has internal SCSI */
        .ram = {
            .min = 16384,
            .max = 2097152,
            .step = 16384
        },
        .nvrmask = 511,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a Winbond W83977TF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[i440GX] Supermicro S2DGE",
        .internal_name = "s2dge",
        .type = MACHINE_TYPE_SLOT2,
        .chipset = MACHINE_CHIPSET_INTEL_440GX,
        .init = machine_at_s2dge_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT2,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 100000000,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 16384,
            .max = 2097152,
            .step = 16384
        },
        .nvrmask = 511,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* PGA370 machines */
    /* 440LX */
    /* Has a Winbond W83977TF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[i440LX] Supermicro 370SLM",
        .internal_name = "s370slm",
        .type = MACHINE_TYPE_SOCKET370,
        .chipset = MACHINE_CHIPSET_INTEL_440LX,
        .init = machine_at_s370slm_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET370,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 100000000,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = MACHINE_MULTIPLIER_FIXED,
            .max_multi = MACHINE_MULTIPLIER_FIXED,
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 786432,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 440BX */
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[i440BX] AEWIN AW-O671R",
        .internal_name = "awo671r",
        .type = MACHINE_TYPE_SOCKET370,
        .chipset = MACHINE_CHIPSET_INTEL_440BX,
        .init = machine_at_awo671r_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET370,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 133333333,
            .min_voltage = 1300,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0 /* limits assumed */
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB, /* Machine has EISA, possibly for a riser? */
                                                        /* Yes, that's a rise slot, not EISA. */
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB | MACHINE_VIDEO, /* Machine has internal video: C&T B69000, sound: ESS ES1938S and NIC: Realtek RTL8139C */
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[i440BX] ASUS CUBX",
        .internal_name = "cubx",
        .type = MACHINE_TYPE_SOCKET370,
        .chipset = MACHINE_CHIPSET_INTEL_440BX,
        .init = machine_at_cubx_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET370,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 150000000,
            .min_voltage = 1300,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB, /* Machine has quad channel IDE with internal controller: CMD PCI-0648 */
        .ram = {
            .min = 8192,
            .max = 1048576,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[i440BX] AmazePC AM-BX133",
        .internal_name = "ambx133",
        .type = MACHINE_TYPE_SOCKET370,
        .chipset = MACHINE_CHIPSET_INTEL_440BX,
        .init = machine_at_ambx133_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET370,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 133333333,
            .min_voltage = 1300,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0 /* limits assumed */
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 786432,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* 440ZX */
    /* Has a Winbond W83977TF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[i440ZX] Soltek SL-63A1",
        .internal_name = "63a1",
        .type = MACHINE_TYPE_SOCKET370,
        .chipset = MACHINE_CHIPSET_INTEL_440ZX,
        .init = machine_at_63a1_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET370,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 100000000,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    /* SMSC VictoryBX-66 */
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[SMSC VictoryBX-66] A-Trend ATC7020BXII",
        .internal_name = "atc7020bxii",
        .type = MACHINE_TYPE_SOCKET370,
        .chipset = MACHINE_CHIPSET_SMSC_VICTORYBX_66,
        .init = machine_at_atc7020bxii_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET370,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 133333333,
            .min_voltage = 1300,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 1048576,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has an ITE IT8671F Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[SMSC VictoryBX-66] PC Chips M773",
        .internal_name = "m773",
        .type = MACHINE_TYPE_SOCKET370,
        .chipset = MACHINE_CHIPSET_SMSC_VICTORYBX_66,
        .init = machine_at_m773_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET370,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 133333333,
            .min_voltage = 1300,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_SOUND | MACHINE_APM | MACHINE_ACPI | MACHINE_GAMEPORT | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 524288,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = &cmi8738_onboard_device,
        .net_device = NULL
    },

    /* VIA Apollo Pro */
    /* Has the VIA VT82C586B southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    {
        .name = "[VIA Apollo Pro] PC Partner APAS3",
        .internal_name = "apas3",
        .type = MACHINE_TYPE_SOCKET370,
        .chipset = MACHINE_CHIPSET_VIA_APOLLO_PRO,
        .init = machine_at_apas3_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET370,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 100000000,
            .min_voltage = 1800,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 786432,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[VIA Apollo Pro 133] ECS P6BAP-A+",
        .internal_name = "p6bap",
        .type = MACHINE_TYPE_SOCKET370,
        .chipset = MACHINE_CHIPSET_VIA_APOLLO_PRO_133,
        .init = machine_at_p6bap_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET370,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 150000000,
            .min_voltage = 1300,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_AGP | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_GAMEPORT | MACHINE_USB | MACHINE_SOUND,
        .ram = {
            .min = 8192,
            .max = 1572864,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = &cmi8738_onboard_device,
        .net_device = NULL
    },
    /* Has the VIA VT82C686B southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    {
        .name = "[VIA Apollo Pro 133A] Acorp 6VIA90AP",
        .internal_name = "6via90ap",
        .type = MACHINE_TYPE_SOCKET370,
        .chipset = MACHINE_CHIPSET_VIA_APOLLO_PRO_133A,
        .init = machine_at_6via90ap_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET370,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 150000000,
            .min_voltage = 1300,
            .max_voltage = 3500,
            .min_multi = MACHINE_MULTIPLIER_FIXED,
            .max_multi = MACHINE_MULTIPLIER_FIXED
        },
        .bus_flags = MACHINE_PS2_A97 | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_AG | MACHINE_APM | MACHINE_ACPI | MACHINE_GAMEPORT | MACHINE_USB,
        .ram = {
            .min = 16384,
            .max = 3145728,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },
    /* Has the VIA VT82C686B southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    {
        .name = "[VIA Apollo Pro 133A] ASUS CUV4X-LS",
        .internal_name = "cuv4xls",
        .type = MACHINE_TYPE_SOCKET370,
        .chipset = MACHINE_CHIPSET_VIA_APOLLO_PRO_133A,
        .init = machine_at_cuv4xls_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SOCKET370,
            .block = CPU_BLOCK_NONE,
            .min_bus = 66666667,
            .max_bus = 150000000,
            .min_voltage = 1300,
            .max_voltage = 3500,
            .min_multi = 1.5,
            .max_multi = 8.0
        },
        .bus_flags = MACHINE_PS2_NOI97 | MACHINE_BUS_USB, /* Has Asus-proprietary LAN/SCSI slot */
        .flags = MACHINE_IDE_DUAL | MACHINE_SOUND | MACHINE_APM | MACHINE_ACPI | MACHINE_GAMEPORT | MACHINE_USB,
        .ram = {
            .min = 16384,
            .max = 4194304,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = &cmi8738_onboard_device,
        .net_device = NULL
    },

    /* Miscellaneous/Fake/Hypervisor machines */
    /* Has a Winbond W83977F Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    {
        .name = "[i440BX] Microsoft Virtual PC 2007",
        .internal_name = "vpc2007",
        .type = MACHINE_TYPE_MISC,
        .chipset = MACHINE_CHIPSET_INTEL_440BX,
        .init = machine_at_vpc2007_init,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = CPU_PKG_SLOT1,
            .block = CPU_BLOCK(CPU_PENTIUM2, CPU_CYRIX3S),
            .min_bus = 0,
            .max_bus = 66666667,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_PS2_PCI | MACHINE_BUS_USB,
        .flags = MACHINE_IDE_DUAL | MACHINE_APM | MACHINE_ACPI | MACHINE_USB,
        .ram = {
            .min = 8192,
            .max = 1048576,
            .step = 8192
        },
        .nvrmask = 255,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    },

    {
        .name = NULL,
        .internal_name = NULL,
        .type = MACHINE_TYPE_NONE,
        .chipset = MACHINE_CHIPSET_NONE,
        .init = NULL,
        .p1_handler = NULL,
        .gpio_handler = NULL,
        .available_flag = MACHINE_AVAILABLE,
        .gpio_acpi_handler = NULL,
        .cpu = {
            .package = 0,
            .block = CPU_BLOCK_NONE,
            .min_bus = 0,
            .max_bus = 0,
            .min_voltage = 0,
            .max_voltage = 0,
            .min_multi = 0,
            .max_multi = 0
        },
        .bus_flags = MACHINE_BUS_NONE,
        .flags = MACHINE_FLAGS_NONE,
        .ram = {
            .min = 0,
            .max = 0,
            .step = 0
        },
        .nvrmask = 0,
        .kbc_device = NULL,
        .kbc_p1 = 0xff,
        .gpio = 0xffffffff,
        .gpio_acpi = 0xffffffff,
        .device = NULL,
        .fdc_device = NULL,
        .sio_device = NULL,
        .vid_device = NULL,
        .snd_device = NULL,
        .net_device = NULL
    }
  // clang-format on
};

/* Saved copies - jumpers get applied to these.
   We use also machine_gpio to store IBM PC/XT jumpers as they need more than one byte. */
static uint32_t machine_p1_default;
static uint32_t machine_p1;

static uint32_t machine_gpio_default;
static uint32_t machine_gpio;

static uint32_t machine_gpio_acpi_default;
static uint32_t machine_gpio_acpi;

void *machine_snd = NULL;

uint8_t
machine_get_p1_default(void)
{
    return machine_p1_default;
}

uint8_t
machine_get_p1(void)
{
    return machine_p1;
}

void
machine_set_p1_default(uint8_t val)
{
    machine_p1 = machine_p1_default = val;
}

void
machine_set_p1(uint8_t val)
{
    machine_p1 = val;
}

void
machine_and_p1(uint8_t val)
{
    machine_p1 = machine_p1_default & val;
}

uint8_t
machine_handle_p1(uint8_t write, uint8_t val)
{
    uint8_t ret = 0xff;

    if (machines[machine].p1_handler)
        ret = machines[machine].p1_handler(write, val);
    else {
        if (write)
            machine_p1 = machine_p1_default & val;
        else
            ret = machine_p1;
    }

    return ret;
}

void
machine_init_p1(void)
{
    machine_p1 = machine_p1_default = machines[machine].kbc_p1;
}

uint32_t
machine_get_gpio_default(void)
{
    return machine_gpio_default;
}

uint32_t
machine_get_gpio(void)
{
    return machine_gpio;
}

void
machine_set_gpio_default(uint32_t val)
{
    machine_gpio = machine_gpio_default = val;
}

void
machine_set_gpio(uint32_t val)
{
    machine_gpio = val;
}

void
machine_and_gpio(uint32_t val)
{
    machine_gpio = machine_gpio_default & val;
}

uint32_t
machine_handle_gpio(uint8_t write, uint32_t val)
{
    uint32_t ret = 0xffffffff;

    if (machines[machine].gpio_handler)
        ret = machines[machine].gpio_handler(write, val);
    else {
        if (write)
            machine_gpio = machine_gpio_default & val;
        else
            ret = machine_gpio;
    }

    return ret;
}

void
machine_init_gpio(void)
{
    machine_gpio = machine_gpio_default = machines[machine].gpio;
}

uint32_t
machine_get_gpio_acpi_default(void)
{
    return machine_gpio_acpi_default;
}

uint32_t
machine_get_gpio_acpi(void)
{
    return machine_gpio_acpi;
}

void
machine_set_gpio_acpi_default(uint32_t val)
{
    machine_gpio_acpi = machine_gpio_acpi_default = val;
}

void
machine_set_gpio_acpi(uint32_t val)
{
    machine_gpio_acpi = val;
}

void
machine_and_gpio_acpi(uint32_t val)
{
    machine_gpio_acpi = machine_gpio_acpi_default & val;
}

uint32_t
machine_handle_gpio_acpi(uint8_t write, uint32_t val)
{
    uint32_t ret = 0xffffffff;

    if (machines[machine].gpio_acpi_handler)
        ret = machines[machine].gpio_acpi_handler(write, val);
    else {
        if (write)
            machine_gpio_acpi = machine_gpio_acpi_default & val;
        else
            ret = machine_gpio_acpi;
    }

    return ret;
}

void
machine_init_gpio_acpi(void)
{
    machine_gpio_acpi = machine_gpio_acpi_default = machines[machine].gpio_acpi;
}

int
machine_count(void)
{
    return ((sizeof(machines) / sizeof(machine_t)) - 1);
}

const char *
machine_getname(void)
{
    return (machines[machine].name);
}

const char *
machine_getname_ex(int m)
{
    return (machines[m].name);
}

const device_t *
machine_get_kbc_device(int m)
{
    if (machines[m].kbc_device)
        return (machines[m].kbc_device);

    return (NULL);
}

const device_t *
machine_get_device(int m)
{
    if (machines[m].device)
        return (machines[m].device);

    return (NULL);
}

const device_t *
machine_get_fdc_device(int m)
{
    if (machines[m].fdc_device)
        return (machines[m].fdc_device);

    return (NULL);
}

const device_t *
machine_get_sio_device(int m)
{
    if (machines[m].sio_device)
        return (machines[m].sio_device);

    return (NULL);
}

const device_t *
machine_get_vid_device(int m)
{
    if (machines[m].vid_device)
        return (machines[m].vid_device);

    return (NULL);
}

const device_t *
machine_get_snd_device(int m)
{
    if (machines[m].snd_device)
        return (machines[m].snd_device);

    return (NULL);
}

const device_t *
machine_get_net_device(int m)
{
    if (machines[m].net_device)
        return (machines[m].net_device);

    return (NULL);
}

const char *
machine_get_internal_name(void)
{
    return (machines[machine].internal_name);
}

const char *
machine_get_internal_name_ex(int m)
{
    return (machines[m].internal_name);
}

int
machine_get_nvrmask(int m)
{
    return (machines[m].nvrmask);
}

int
machine_has_flags(int m, int flags)
{
    return (machines[m].flags & flags);
}

int
machine_has_bus(int m, int bus_flags)
{
    return (machines[m].bus_flags & bus_flags);
}

int
machine_has_cartridge(int m)
{
    return (machine_has_bus(m, MACHINE_CARTRIDGE) ? 1 : 0);
}

int
machine_get_min_ram(int m)
{
    return (machines[m].ram.min);
}

int
machine_get_max_ram(int m)
{
#if (!(defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64))
    return MIN(((int) machines[m].ram.max), 2097152);
#else
    return MIN(((int) machines[m].ram.max), 3145728);
#endif
}

int
machine_get_ram_granularity(int m)
{
    return (machines[m].ram.step);
}

int
machine_get_type(int m)
{
    return (machines[m].type);
}

int
machine_get_machine_from_internal_name(const char *s)
{
    int c = 0;

    while (machines[c].init != NULL) {
        if (!strcmp(machines[c].internal_name, s))
            return c;
        c++;
    }

    return 0;
}

int
machine_has_mouse(void)
{
    return (machines[machine].flags & MACHINE_MOUSE);
}

int
machine_is_sony(void)
{
    return (!strcmp(machines[machine].internal_name, "pcv90"));
}

const char *
machine_get_nvr_name_ex(int m)
{
    const char     *ret = machines[m].internal_name;
    const device_t *dev = machine_get_device(m);

    if (dev != NULL) {
        device_context(dev);
        const char *bios = device_get_config_string("bios");
        if ((bios != NULL) && (strcmp(bios, "") != 0))
            ret = bios;
        device_context_restore();
    }

    return ret;
}

const char *
machine_get_nvr_name(void)
{
    return machine_get_nvr_name_ex(machine);
}
