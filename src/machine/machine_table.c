/*
 * 86Box     A hypervisor and IBM PC system emulator that specializes in
 *           running old operating systems and software designed for IBM
 *           PC systems and compatibles from 1981 through fairly recent
 *           system designs based on the PCI bus.
 *
 *           This file is part of the 86Box distribution.
 *
 *           Handling of the emulated machines.
 *
 * NOTES:    OpenAT wip for 286-class machine with open BIOS.
 *           PS2_M80-486 wip, pending receipt of TRM's for machine.
 *
 *
 *
 * Authors:  Sarah Walker, <http://pcem-emulator.co.uk/>
 *           Miran Grca, <mgrca8@gmail.com>
 *           Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *           Copyright 2008-2020 Sarah Walker.
 *           Copyright 2016-2020 Miran Grca.
 *           Copyright 2017-2020 Fred N. van Kempen.
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

const machine_filter_t machine_types[] = {
    { "None",                      MACHINE_TYPE_NONE       },
    { "8088",                      MACHINE_TYPE_8088       },
    { "8086",                      MACHINE_TYPE_8086       },
    { "80286",                     MACHINE_TYPE_286        },
    { "i386SX",                    MACHINE_TYPE_386SX      },
    { "486SLC",                    MACHINE_TYPE_486SLC     },
    { "i386DX",                    MACHINE_TYPE_386DX      },
    { "i386DX/i486",               MACHINE_TYPE_386DX_486  },
    { "i486 (Socket 168 and 1)",   MACHINE_TYPE_486        },
    { "i486 (Socket 2)",           MACHINE_TYPE_486_S2     },
    { "i486 (Socket 3)",           MACHINE_TYPE_486_S3     },
    { "i486 (Miscellaneous)",      MACHINE_TYPE_486_MISC   },
    { "Socket 4",                  MACHINE_TYPE_SOCKET4    },
    { "Socket 5",                  MACHINE_TYPE_SOCKET5    },
    { "Socket 7 (Single Voltage)", MACHINE_TYPE_SOCKET7_3V },
    { "Socket 7 (Dual Voltage)",   MACHINE_TYPE_SOCKET7    },
    { "Super Socket 7",            MACHINE_TYPE_SOCKETS7   },
    { "Socket 8",                  MACHINE_TYPE_SOCKET8    },
    { "Slot 1",                    MACHINE_TYPE_SLOT1      },
    { "Slot 1/2",                  MACHINE_TYPE_SLOT1_2    },
    { "Slot 1/Socket 370",         MACHINE_TYPE_SLOT1_370  },
    { "Slot 2",                    MACHINE_TYPE_SLOT2      },
    { "Socket 370",                MACHINE_TYPE_SOCKET370  },
    { "Miscellaneous",             MACHINE_TYPE_MISC       }
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
    { "SiS 5511",                   MACHINE_CHIPSET_SIS_5511            },
    { "SiS 5571",                   MACHINE_CHIPSET_SIS_5571            },
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
   - PCChips M773 (440BX + SMSC with AMI BIOS);
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
    /* 8088 Machines */
    { "[8088] IBM PC (1981)",                          "ibmpc",            MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_pc_init,                  0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,   16,    64,  16,    0, NULL, NULL },
    { "[8088] IBM PC (1982)",                          "ibmpc82",          MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_pc82_init,                0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,   64,   256,  64,    0, NULL, NULL },
    { "[8088] IBM PCjr",                               "ibmpcjr",          MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_pcjr_init,                0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 4772728, 4772728, 0, 0, 0, 0, MACHINE_PCJR,      MACHINE_VIDEO_FIXED, 128,   640, 128,    0, pcjr_get_device, NULL },
    { "[8088] IBM XT (1982)",                          "ibmxt",            MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_init,                  0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,   64,   256,  64,    0, NULL, NULL },
    { "[8088] IBM XT (1986)",                          "ibmxt86",          MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt86_init,                0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,  256,   640,  64,    0, NULL, NULL },
    { "[8088] American XT Computer",                   "americxt",         MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_americxt_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,   64,   640,  64,    0, NULL, NULL },
    { "[8088] AMI XT clone",                           "amixt",            MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_amixt_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,   64,   640,  64,    0, NULL, NULL },
    { "[8088] Bondwell BW230",                         "bw230",            MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_bw230_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,   64,   640,  64,    0, NULL, NULL },
    { "[8088] Columbia Data Products MPC-1600",        "mpc1600",          MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_mpc1600_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,  128,   512,  64,    0, NULL, NULL },
    { "[8088] Compaq Portable",                        "portable",         MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_compaq_portable_init,  0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,  128,   640, 128,    0, NULL, NULL },
    { "[8088] DTK PIM-TB10-Z",                         "dtk",              MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_dtk_init,              0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,   64,   640,  64,    0, NULL, NULL },
    { "[8088] Eagle PC Spirit",                        "pcspirit",         MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_pcspirit_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,  128,   640,  64,    0, NULL, NULL },
    { "[8088] Generic XT clone",                       "genxt",            MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_genxt_init,               0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,   64,   640,  64,    0, NULL, NULL },
    { "[8088] Hyosung Topstar 88T",                    "top88",            MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_top88_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,  128,   640,  64,    0, NULL, NULL },
    { "[8088] Hyundai SUPER-16T",                      "super16t",         MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_super16t_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 4772728, 7159092, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,  128,   640,  64,    0, NULL, NULL },
    { "[8088] Hyundai SUPER-16TE",                     "super16te",        MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_super16te_init,        0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 10000000, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,  128,   640,  64,    0, NULL, NULL },
    { "[8088] Juko ST",                                "jukopc",           MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_jukopc_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,   64,   640,  64,    0, NULL, NULL },
    { "[8088] Kaypro PC",                              "kaypropc",         MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_kaypropc_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,  128,   640,  64,    0, NULL, NULL },
    { "[8088] Multitech PC-500",                       "pc500",            MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_pc500_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,  128,   640,  64,    0, NULL, NULL },
    { "[8088] Multitech PC-700",                       "pc700",            MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_pc700_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,  128,   640,  64,    0, NULL, NULL },
    { "[8088] NCR PC4i",                               "pc4i",             MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_pc4i_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,  256,   640, 256,    0, NULL, NULL },
    { "[8088] Olivetti M19",                           "m19",              MACHINE_TYPE_8088,       MACHINE_CHIPSET_PROPRIETARY,         machine_xt_m19_init,              0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 4772728, 7159092, 0, 0, 0, 0, MACHINE_PC,        MACHINE_VIDEO_FIXED, 256,   640, 256,    0, m19_get_device, NULL },
    { "[8088] OpenXT",                                 "openxt",           MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_openxt_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,   64,   640,  64,    0, NULL, NULL },
    { "[8088] Philips P3105/NMS9100",                  "p3105",            MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_p3105_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_XTA,         256,   768, 256,    0, NULL, NULL },
    { "[8088] Phoenix XT clone",                       "pxxt",             MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_pxxt_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,   64,   640,  64,    0, NULL, NULL },
    { "[8088] Sanyo SX-16",                            "sansx16",          MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_sansx16_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,  256,   640, 256,    0, NULL, NULL },
    { "[8088] Schneider EuroPC",                       "europc",           MACHINE_TYPE_8088,       MACHINE_CHIPSET_PROPRIETARY,         machine_europc_init,              0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088_EUROPC,                             CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_XTA | MACHINE_MOUSE, 512,   640, 128,   15, NULL, NULL },
    { "[8088] Super PC/Turbo XT",                      "pcxt",             MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_pcxt_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,   64,   640,  64,    0, NULL, NULL },
    { "[8088] Tandy 1000",                             "tandy",            MACHINE_TYPE_8088,       MACHINE_CHIPSET_PROPRIETARY,         machine_tandy_init,               0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088_EUROPC,                             CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_VIDEO_FIXED, 128,   640, 128,    0, tandy1k_get_device, NULL },
    { "[8088] Tandy 1000 HX",                          "tandy1000hx",      MACHINE_TYPE_8088,       MACHINE_CHIPSET_PROPRIETARY,         machine_tandy1000hx_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088_EUROPC,                             CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_VIDEO_FIXED, 256,   640, 128,    0, tandy1k_hx_get_device, NULL },
    { "[8088] Toshiba T1000",                          "t1000",            MACHINE_TYPE_8088,       MACHINE_CHIPSET_PROPRIETARY,         machine_xt_t1000_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_VIDEO,       512,  1280, 768,   63, t1000_get_device, NULL },
    { "[8088] Vendex HeadStart Turbo 888-XT",          "vendex",           MACHINE_TYPE_8088,       MACHINE_CHIPSET_PROPRIETARY,         machine_xt_vendex_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,  256,   768, 256,    0, NULL, NULL },
#if defined(DEV_BRANCH) && defined(USE_LASERXT)
    { "[8088] VTech Laser Turbo XT",                   "ltxt",             MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_laserxt_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,  256,   640, 256,    0,NULL, NULL },
#endif
    /* Has a standard PS/2 KBC (so, use IBM PS/2 Type 1). */
    { "[8088] Xi8088",                                 "xi8088",           MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_xi8088_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2,       MACHINE_FLAGS_NONE,   64,  1024, 128,  127, xi8088_get_device, NULL },
    { "[8088] Z-NIX PC-1600",                          "znic",             MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_znic_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,   64,   640,  64,    0, NULL, NULL },
    { "[8088] Zenith Data Systems Z-151/152/161",      "zdsz151",          MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_z151_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,  128,   640,  64,    0, NULL, NULL },
    { "[8088] Zenith Data Systems Z-159",              "zdsz159",          MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_z159_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,  128,   640,  64,    0, NULL, NULL },
    { "[8088] Zenith Data Systems SupersPort (Z-184)", "zdsupers",         MACHINE_TYPE_8088,       MACHINE_CHIPSET_DISCRETE,            machine_xt_z184_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_VIDEO_FIXED, 128,   640, 128,    0, z184_get_device, NULL },
    { "[GC100A] Philips P3120",                        "p3120",            MACHINE_TYPE_8088,       MACHINE_CHIPSET_GC100A,              machine_xt_p3120_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8088,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_XTA,         256,   768, 256,    0, NULL, NULL },

    /* 8086 Machines */
    { "[8086] Amstrad PC1512",                         "pc1512",           MACHINE_TYPE_8086,       MACHINE_CHIPSET_PROPRIETARY,         machine_pc1512_init,              0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8086,                                    CPU_BLOCK_NONE, 8000000, 8000000, 0, 0, 0, 0, MACHINE_PC,        MACHINE_VIDEO_FIXED | MACHINE_MOUSE, 512,   640, 128,   63, pc1512_get_device, NULL },
    { "[8086] Amstrad PC1640",                         "pc1640",           MACHINE_TYPE_8086,       MACHINE_CHIPSET_PROPRIETARY,         machine_pc1640_init,              0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8086,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_VIDEO | MACHINE_MOUSE,        640,   640, 640,   63, pc1640_get_device, NULL },
    { "[8086] Amstrad PC2086",                         "pc2086",           MACHINE_TYPE_8086,       MACHINE_CHIPSET_PROPRIETARY,         machine_pc2086_init,              0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8086,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_VIDEO_FIXED | MACHINE_MOUSE,  640,   640, 640,   63, pc2086_get_device, NULL },
    { "[8086] Amstrad PC3086",                         "pc3086",           MACHINE_TYPE_8086,       MACHINE_CHIPSET_PROPRIETARY,         machine_pc3086_init,              0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8086,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_VIDEO_FIXED | MACHINE_MOUSE,  640,   640, 640,   63, pc3086_get_device, NULL },
    { "[8086] Amstrad PC20(0)",                        "pc200",            MACHINE_TYPE_8086,       MACHINE_CHIPSET_PROPRIETARY,         machine_pc200_init,               0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8086,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_VIDEO | MACHINE_MOUSE,        512,   640, 128,   63, pc200_get_device, NULL },
    { "[8086] Amstrad PPC512/640",                     "ppc512",           MACHINE_TYPE_8086,       MACHINE_CHIPSET_PROPRIETARY,         machine_ppc512_init,              0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8086,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_VIDEO | MACHINE_MOUSE,        512,   640, 128,   63, ppc512_get_device, NULL },
    { "[8086] Compaq Deskpro",                         "deskpro",          MACHINE_TYPE_8086,       MACHINE_CHIPSET_PROPRIETARY,         machine_xt_compaq_deskpro_init,   0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8086,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,                   128,   640, 128,    0, NULL, NULL },
    { "[8086] Olivetti M21/24/24SP",                   "m24",              MACHINE_TYPE_8086,       MACHINE_CHIPSET_PROPRIETARY,         machine_xt_m24_init,              0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8086,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_VIDEO | MACHINE_MOUSE,        128,   640, 128,    0, m24_get_device, NULL },
    /* Has Olivetti KBC firmware. */
    { "[8086] Olivetti M240",                          "m240",             MACHINE_TYPE_8086,       MACHINE_CHIPSET_PROPRIETARY,         machine_xt_m240_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8086,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,                   128,   640, 128,    0, NULL, NULL },
    { "[8086] Schetmash Iskra-3104",                   "iskra3104",        MACHINE_TYPE_8086,       MACHINE_CHIPSET_DISCRETE,            machine_xt_iskra3104_init,        0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8086,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,                   128,   640, 128,    0, NULL, NULL },
    { "[8086] Tandy 1000 SL/2",                        "tandy1000sl2",     MACHINE_TYPE_8086,       MACHINE_CHIPSET_PROPRIETARY,         machine_tandy1000sl2_init,        0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8086,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_VIDEO_FIXED,                  512,   768, 128,    0, tandy1k_sl_get_device, NULL },
    { "[8086] Victor V86P",                            "v86p",             MACHINE_TYPE_8086,       MACHINE_CHIPSET_PROPRIETARY,         machine_v86p_init,                0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8086,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_VIDEO,                        512,  1024, 128,  127, NULL, NULL },
    { "[8086] Toshiba T1200",                          "t1200",            MACHINE_TYPE_8086,       MACHINE_CHIPSET_PROPRIETARY,         machine_xt_t1200_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8086,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_VIDEO,                       1024,  2048,1024,   63, t1200_get_device, NULL },

#if defined(DEV_BRANCH) && defined(USE_LASERXT)
    { "[8086] VTech Laser XT3",                        "lxt3",             MACHINE_TYPE_8086,       MACHINE_CHIPSET_DISCRETE,            machine_xt_lxt3_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_8086,                                    CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PC,        MACHINE_FLAGS_NONE,                   256,   640, 256,    0, NULL, NULL },
#endif

    /* 286 AT machines */
    /* Has IBM AT KBC firmware. */
    { "[ISA] IBM AT",                                  "ibmat",            MACHINE_TYPE_286,        MACHINE_CHIPSET_DISCRETE,            machine_at_ibm_init,              0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 6000000, 8000000, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 256, 15872, 128,   63, NULL, NULL },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    { "[ISA] IBM PS/1 model 2011",                     "ibmps1es",         MACHINE_TYPE_286,        MACHINE_CHIPSET_PROPRIETARY,         machine_ps1_m2011_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 10000000, 10000000, 0, 0, 0, 0, MACHINE_PS2, MACHINE_XTA | MACHINE_VIDEO_FIXED, 512, 16384, 512,   63, NULL, NULL },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    { "[ISA] IBM PS/2 model 30-286",                   "ibmps2_m30_286",   MACHINE_TYPE_286,        MACHINE_CHIPSET_PROPRIETARY,         machine_ps2_m30_286_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286 | CPU_PKG_486SLC_IBM,                CPU_BLOCK_NONE, 10000000, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_XTA | MACHINE_VIDEO_FIXED, 1024, 16384,1024,  127, NULL, NULL },
    /* Has IBM AT KBC firmware. */
    { "[ISA] IBM XT Model 286",                        "ibmxt286",         MACHINE_TYPE_286,        MACHINE_CHIPSET_DISCRETE,            machine_at_ibmxt286_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 6000000, 6000000, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 256, 15872, 128,  127, NULL, NULL },
    /* AMI BIOS for a chipset-less machine, most likely has AMI 'F' KBC firmware. */
    { "[ISA] AMI IBM AT",                              "ibmatami",         MACHINE_TYPE_286,        MACHINE_CHIPSET_DISCRETE,            machine_at_ibmatami_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 6000000, 8000000, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 256, 15872, 128,   63, NULL, NULL },
    /* Uses Commodore (CBM) KBC firmware, to be implemented as identical to the
       IBM AT KBC firmware unless evidence emerges of any proprietary commands. */
    { "[ISA] Commodore PC 30 III",                     "cmdpc30",          MACHINE_TYPE_286,        MACHINE_CHIPSET_PROPRIETARY,         machine_at_cmdpc_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 640, 16384, 128,  127, NULL, NULL },
    /* Uses Compaq KBC firmware. */
    { "[ISA] Compaq Portable II",                      "portableii",       MACHINE_TYPE_286,        MACHINE_CHIPSET_PROPRIETARY,         machine_at_portableii_init,       0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 640, 16384, 128,  127, NULL, NULL },
    /* Uses Compaq KBC firmware. */
    { "[ISA] Compaq Portable III",                     "portableiii",      MACHINE_TYPE_286,        MACHINE_CHIPSET_PROPRIETARY,         machine_at_portableiii_init,      0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_VIDEO, 640, 16384, 128,  127, at_cpqiii_get_device, NULL },
    /* Has IBM AT KBC firmware. */
    { "[ISA] MR BIOS 286 clone",                       "mr286",            MACHINE_TYPE_286,        MACHINE_CHIPSET_DISCRETE,            machine_at_mr286_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_IDE, 512, 16384, 128,  127, NULL, NULL },
    /* Has IBM AT KBC firmware. */
    { "[ISA] NCR PC8/810/710/3390/3392",               "pc8",              MACHINE_TYPE_286,        MACHINE_CHIPSET_DISCRETE,            machine_at_pc8_init,              0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 512, 16384, 128,  127, NULL, NULL },
#if defined(DEV_BRANCH) && defined(USE_OLIVETTI)
    /* Has Olivetti KBC firmware. */
    { "[ISA] Olivetti M290",                           "m290",             MACHINE_TYPE_286,        MACHINE_CHIPSET_PROPRIETARY,         machine_at_m290_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 640, 16384, 128, 127, NULL, NULL },
#endif
#if defined(DEV_BRANCH) && defined(USE_OPEN_AT)
    /* Has IBM AT KBC firmware. */
    { "[ISA] OpenAT",                                  "openat",           MACHINE_TYPE_286,        MACHINE_CHIPSET_DISCRETE,            machine_at_openat_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 256, 15872, 128,   63, NULL, NULL },
#endif
    /* Has IBM AT KBC firmware. */
    { "[ISA] Phoenix IBM AT",                          "ibmatpx",          MACHINE_TYPE_286,        MACHINE_CHIPSET_DISCRETE,            machine_at_ibmatpx_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 6000000, 8000000, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 256, 15872, 128,   63, NULL, NULL },
    /* Has Quadtel KBC firmware. */
    { "[ISA] Quadtel IBM AT",                          "ibmatquadtel",     MACHINE_TYPE_286,        MACHINE_CHIPSET_DISCRETE,            machine_at_ibmatquadtel_init,     0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 6000000, 8000000, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 256, 15872, 128,   63, NULL, NULL },
    /* This has a Siemens proprietary KBC which is completely undocumented. */
    { "[ISA] Siemens PCD-2L",                          "siemens",          MACHINE_TYPE_286,        MACHINE_CHIPSET_DISCRETE,            machine_at_siemens_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 256, 15872, 128,   63, NULL, NULL },
    /* This has Toshiba's proprietary KBC, which is already implemented. */
    { "[ISA] Toshiba T3100e",                          "t3100e",           MACHINE_TYPE_286,        MACHINE_CHIPSET_PROPRIETARY,         machine_at_t3100e_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_IDE | MACHINE_VIDEO_FIXED, 1024,  5120, 256,   63, NULL, NULL },
    /* Has Quadtel KBC firmware. */
    { "[GC103] Quadtel 286 clone",                     "quadt286",         MACHINE_TYPE_286,        MACHINE_CHIPSET_GC103,               machine_at_quadt286_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 512, 16384, 128,  127, NULL, NULL },
    /* Most likely has AMI 'F' KBC firmware. */
    { "[GC103] TriGem 286M",                           "tg286m",           MACHINE_TYPE_286,        MACHINE_CHIPSET_GC103,               machine_at_tg286m_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_IDE, 512,  8192, 128,  127, NULL, NULL },
    /* This has "AMI KEYBOARD BIOS", most likely 'F'. */
    { "[NEAT] DataExpert 286",                         "ami286",           MACHINE_TYPE_286,        MACHINE_CHIPSET_NEAT,                machine_at_neat_ami_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 512,  8192, 128,  127, NULL, NULL },
    /* Has IBM AT KBC firmware. */
    { "[NEAT] NCR 3302",                               "3302",             MACHINE_TYPE_286,        MACHINE_CHIPSET_NEAT,                machine_at_3302_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_VIDEO, 512, 16384, 128,  127, NULL, NULL },
    /* Has IBM AT KBC firmware. */
    { "[NEAT] Phoenix 286 clone",                      "px286",            MACHINE_TYPE_286,        MACHINE_CHIPSET_NEAT,                machine_at_px286_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 512, 16384, 128,  127, NULL, NULL },
    /* Has Chips & Technologies KBC firmware. */
    { "[SCAT] GW-286CT GEAR",                          "gw286ct",          MACHINE_TYPE_286,        MACHINE_CHIPSET_SCAT,                machine_at_gw286ct_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_IDE, 512, 16384, 128,  127, NULL, NULL },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    { "[SCAT] Goldstar GDC-212M",                      "gdc212m",          MACHINE_TYPE_286,        MACHINE_CHIPSET_SCAT,                machine_at_gdc212m_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_IDE, 512,  4096, 512,  127, NULL, NULL },
    /* Has a VIA VT82C42N KBC. */
    { "[SCAT] Hyundai Solomon 286KP",                  "award286",         MACHINE_TYPE_286,        MACHINE_CHIPSET_SCAT,                machine_at_award286_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 512, 16384, 128,  127, NULL, NULL },
    /* Has a VIA VT82C42N KBC. */
    { "[SCAT] Hyundai Super-286TR",                    "super286tr",       MACHINE_TYPE_286,        MACHINE_CHIPSET_SCAT,                machine_at_super286tr_init,       0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 512, 16384, 128,  127, NULL, NULL },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    { "[SCAT] Samsung SPC-4200P",                      "spc4200p",         MACHINE_TYPE_286,        MACHINE_CHIPSET_SCAT,                machine_at_spc4200p_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_FLAGS_NONE, 512,  2048, 128,  127, NULL, NULL },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    { "[SCAT] Samsung SPC-4216P",                      "spc4216p",         MACHINE_TYPE_286,        MACHINE_CHIPSET_SCAT,                machine_at_spc4216p_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_FLAGS_NONE, 1024,  5120,1024,  127, NULL, NULL },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    { "[SCAT] Samsung SPC-4620P",                      "spc4620p",         MACHINE_TYPE_286,        MACHINE_CHIPSET_SCAT,                machine_at_spc4620p_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_VIDEO, 1024,  5120,1024,  127, NULL, NULL },
    /* Has IBM AT KBC firmware. */
    { "[SCAT] Samsung Deskmaster 286",                 "deskmaster286",    MACHINE_TYPE_286,        MACHINE_CHIPSET_SCAT,                machine_at_deskmaster286_init,    0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286,                                     CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 512, 16384, 128,  127, NULL, NULL },

    /* 286 machines that utilize the MCA bus */
    /* Has IBM PS/2 Type 2 KBC firmware. */
    { "[MCA] IBM PS/2 model 50",                       "ibmps2_m50",       MACHINE_TYPE_286,        MACHINE_CHIPSET_PROPRIETARY,         machine_ps2_model_50_init,        0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_286 | CPU_PKG_486SLC_IBM,                CPU_BLOCK_NONE, 10000000, 0, 0, 0, 0, 0, MACHINE_PS2_MCA, MACHINE_VIDEO, 1024, 10240, 1024,   63, NULL, NULL },

    /* 386SX machines */
    /* ISA slots available because an official IBM expansion for that existed. */
    /* Has IBM PS/2 Type 1 KBC firmware. */
    { "[ISA] IBM PS/1 model 2121",                     "ibmps1_2121",      MACHINE_TYPE_386SX,      MACHINE_CHIPSET_PROPRIETARY,         machine_ps1_m2121_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386SX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_IDE | MACHINE_VIDEO, 2048,  6144,1024,   63, NULL, NULL },
    /* Has IBM AT KBC firmware. */
    { "[ISA] NCR PC916SX",                             "pc916sx",          MACHINE_TYPE_386SX,      MACHINE_CHIPSET_DISCRETE,            machine_at_pc916sx_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386SX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 1024, 16384, 128,  127, NULL, NULL },
    /* Has Quadtel KBC firmware. */
    { "[ISA] QTC-SXM KT X20T02/HI",                    "quadt386sx",       MACHINE_TYPE_386SX,      MACHINE_CHIPSET_DISCRETE,            machine_at_quadt386sx_init,       0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386SX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 1024, 16384, 128,  127, NULL, NULL },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    { "[ALi M1217] Acrosser AR-B1374",                 "arb1374",          MACHINE_TYPE_386SX,      MACHINE_CHIPSET_ALI_M1217,           machine_at_arb1374_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386SX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_IDE, 1024, 32768,1024,  127, NULL, NULL },
    /* Has the AMIKey KBC firmware, which is an updated 'F' type. */
    { "[ALi M1217] AAEON SBC-350A",                    "sbc350a",          MACHINE_TYPE_386SX,      MACHINE_CHIPSET_ALI_M1217,           machine_at_sbc350a_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386SX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_IDE, 1024, 16384, 1024, 127, NULL, NULL },
    /* Has an AMI KBC firmware, the only photo of this is too low resolution
       for me to read what's on the KBC chip, so I'm going to assume AMI 'F'
       based on the other known HT18 AMI BIOS strings. */
    { "[ALi M1217] Flytech 386",                       "flytech386",       MACHINE_TYPE_386SX,      MACHINE_CHIPSET_ALI_M1217,           machine_at_flytech386_init,       0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386SX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_IDE | MACHINE_VIDEO, 1024, 16384, 1024, 127, at_flytech386_get_device, NULL },
    /* I'm going to assume this has a standard/generic IBM-compatible AT KBC
       firmware until the board is identified. */
    { "[ALi M1217] MR BIOS 386SX clone",               "mr1217",           MACHINE_TYPE_386SX,      MACHINE_CHIPSET_ALI_M1217,           machine_at_mr1217_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386SX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_IDE | MACHINE_VIDEO, 1024, 16384, 1024, 127, NULL, NULL },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    { "[ALi M6117] Acrosser PJ-A511M",                 "pja511m",          MACHINE_TYPE_386SX,      MACHINE_CHIPSET_ALI_M6117,           machine_at_pja511m_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_M6117,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_IDE, 1024, 32768, 1024, 127, NULL, NULL },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    { "[ALi M6117] Protech ProX-1332",                 "prox1332",         MACHINE_TYPE_386SX,      MACHINE_CHIPSET_ALI_M6117,           machine_at_prox1332_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_M6117,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_IDE, 1024, 32768, 1024, 127, NULL, NULL },
    /* Has an AMI KBC firmware, the only photo of this is too low resolution
       for me to read what's on the KBC chip, so I'm going to assume AMI 'F'
       based on the other known HT18 AMI BIOS strings. */
    { "[HT18] AMA-932J",                               "ama932j",          MACHINE_TYPE_386SX,      MACHINE_CHIPSET_HT18,                machine_at_ama932j_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386SX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_IDE | MACHINE_VIDEO, 512,  8192,  128, 127, at_ama932j_get_device, NULL },
    /* Has an unknown KBC firmware with commands B8 and BB in the style of
       Phoenix MultiKey and AMIKey-3(!), but also commands E1 and EA with
       unknown functions. */
    { "[Intel 82335] ADI 386SX",                       "adi386sx",         MACHINE_TYPE_386SX,      MACHINE_CHIPSET_INTEL_82335,         machine_at_adi386sx_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386SX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 512,  8192,  128, 127, NULL, NULL },
    /* Has an AMI Keyboard BIOS PLUS KBC firmware ('8'). */
    { "[Intel 82335] Shuttle 386SX",                   "shuttle386sx",     MACHINE_TYPE_386SX,      MACHINE_CHIPSET_INTEL_82335,         machine_at_shuttle386sx_init,     0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386SX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 512,  8192,  128, 127, NULL, NULL },
    /* Uses Commodore (CBM) KBC firmware, to be implemented as identical to
       the IBM PS/2 Type 1 KBC firmware unless evidence emerges of any
       proprietary commands. */
    { "[NEAT] Commodore SL386SX-16",                   "cmdsl386sx16",     MACHINE_TYPE_386SX,      MACHINE_CHIPSET_NEAT,                machine_at_cmdsl386sx16_init,     0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386SX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_IDE, 1024,  8192,  512, 127, NULL, NULL },
    /* Has IBM AT KBC firmware. */
    { "[NEAT] DTK 386SX clone",                        "dtk386",           MACHINE_TYPE_386SX,      MACHINE_CHIPSET_NEAT,                machine_at_neat_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386SX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 512,  8192,  128, 127, NULL, NULL },
    /* Has IBM AT KBC firmware. */
    { "[OPTi 291] DTK PPM-3333P",                      "awardsx",          MACHINE_TYPE_386SX,      MACHINE_CHIPSET_OPTI_291,            machine_at_awardsx_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386SX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 1024, 16384, 1024, 127, NULL, NULL },
    /* Uses Commodore (CBM) KBC firmware, to be implemented as identical to
       the IBM PS/2 Type 1 KBC firmware unless evidence emerges of any
       proprietary commands. */
    { "[SCAMP] Commodore SL386SX-25",                  "cmdsl386sx25",     MACHINE_TYPE_386SX,      MACHINE_CHIPSET_VLSI_SCAMP,          machine_at_cmdsl386sx25_init,     0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386SX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_IDE | MACHINE_VIDEO, 1024,  8192,  512, 127, at_cmdsl386sx25_get_device, NULL },
    /* The closest BIOS string I find to this one's, differs only in one part,
       and ends in -8, so I'm going to assume that this, too, has an AMI '8'
       (AMI Keyboard BIOS Plus) KBC firmware. */
    { "[SCAMP] DataExpert 386SX",                      "dataexpert386sx",  MACHINE_TYPE_386SX,      MACHINE_CHIPSET_VLSI_SCAMP,          machine_at_dataexpert386sx_init,  0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386SX,                                   CPU_BLOCK_NONE, 10000000, 25000000, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 1024, 16384, 1024, 127, NULL, NULL },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    { "[SCAMP] Samsung SPC-6033P",                     "spc6033p",         MACHINE_TYPE_386SX,      MACHINE_CHIPSET_VLSI_SCAMP,          machine_at_spc6033p_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386SX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_IDE | MACHINE_VIDEO, 2048, 12288, 2048, 127, at_spc6033p_get_device, NULL },
    /* Has an unknown AMI KBC firmware, I'm going to assume 'F' until a
       photo or real hardware BIOS string is found. */
    { "[SCAT] KMX-C-02",                               "kmxc02",           MACHINE_TYPE_386SX,      MACHINE_CHIPSET_SCAT,                machine_at_kmxc02_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386SX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 512, 16384,  512, 127, NULL, NULL },
    /* Has Quadtel KBC firmware. */
    { "[WD76C10] Amstrad MegaPC",                      "megapc",           MACHINE_TYPE_386SX,      MACHINE_CHIPSET_WD76C10,             machine_at_wd76c10_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386SX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_IDE | MACHINE_VIDEO, 1024, 32768, 1024, 127, NULL, NULL },

    /* 386SX machines which utilize the MCA bus */
    /* Has IBM PS/2 Type 1 KBC firmware. */
    { "[MCA] IBM PS/2 model 55SX",                     "ibmps2_m55sx",     MACHINE_TYPE_386SX,      MACHINE_CHIPSET_PROPRIETARY,         machine_ps2_model_55sx_init,      0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386SX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2_MCA, MACHINE_VIDEO, 1024,  8192, 1024,  63, NULL, NULL },

    /* 486SLC machines */
    /* 486SLC machines with just the ISA slot */
    /* Has AMIKey H KBC firmware. */
    { "[OPTi 283] RYC Leopard LX",                     "rycleopardlx",     MACHINE_TYPE_486SLC,     MACHINE_CHIPSET_OPTI_283,            machine_at_rycleopardlx_init,     0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_486SLC_IBM,                              CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 1024, 16384, 1024, 127, NULL, NULL },

    /* 386DX machines */
    { "[ACC 2168] AMI 386DX clone",                    "acc386",           MACHINE_TYPE_386DX,      MACHINE_CHIPSET_ACC_2168,            machine_at_acc386_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386DX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 1024, 16384, 1024, 127, NULL, NULL },
    /* Has an AMI Keyboard BIOS PLUS KBC firmware ('8'). */
    { "[C&T 386] ECS 386/32",                          "ecs386",           MACHINE_TYPE_386DX,      MACHINE_CHIPSET_CT_386,              machine_at_ecs386_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386DX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 1024, 16384, 1024, 127, NULL, NULL },
    /* Has IBM AT KBC firmware. */
    { "[C&T 386] Samsung SPC-6000A",                   "spc6000a",         MACHINE_TYPE_386DX,      MACHINE_CHIPSET_CT_386,              machine_at_spc6000a_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386DX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_IDE, 1024, 32768, 1024, 127, NULL, NULL },
    /* Uses Compaq KBC firmware. */
#if defined(DEV_BRANCH) && defined(USE_DESKPRO386)
    { "[ISA] Compaq Deskpro 386",                      "deskpro386",       MACHINE_TYPE_386DX,      MACHINE_CHIPSET_DISCRETE,            machine_at_deskpro386_init,       0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386DX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_IDE, 1024, 14336, 1024, 127, NULL, NULL },
#endif
    { "[ISA] Compaq Portable III (386)",               "portableiii386",   MACHINE_TYPE_386DX,      MACHINE_CHIPSET_DISCRETE,            machine_at_portableiii386_init,   0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386DX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_IDE | MACHINE_VIDEO, 1024, 14336, 1024, 127, at_cpqiii_get_device, NULL },
    /* Has IBM AT KBC firmware. */
    { "[ISA] Micronics 09-00021",                      "micronics386",     MACHINE_TYPE_386DX,      MACHINE_CHIPSET_DISCRETE,            machine_at_micronics386_init,     0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386DX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 512,  8192,  128, 127, NULL, NULL },
    /* Has AMIKey F KBC firmware. */
    { "[SiS 310] ASUS ISA-386C",                       "asus386",          MACHINE_TYPE_386DX,      MACHINE_CHIPSET_SIS_310,             machine_at_asus386_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386DX,                                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 1024, 65536, 1024, 127, NULL, NULL },

    /* 386DX machines which utilize the MCA bus */
    /* Has IBM PS/2 Type 1 KBC firmware. */
    { "[MCA] IBM PS/2 model 70 (type 3)",              "ibmps2_m70_type3", MACHINE_TYPE_386DX,      MACHINE_CHIPSET_PROPRIETARY,         machine_ps2_model_70_type3_init,  0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386DX | CPU_PKG_486BL,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2_MCA, MACHINE_VIDEO, 2048, 65536, 2048,  63, NULL, NULL },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    { "[MCA] IBM PS/2 model 80 (type 2)",              "ibmps2_m80",       MACHINE_TYPE_386DX,      MACHINE_CHIPSET_PROPRIETARY,         machine_ps2_model_80_init,        0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386DX | CPU_PKG_486BL | CPU_PKG_SOCKET1, CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2_MCA, MACHINE_VIDEO, 1024, 65536, 1024,  63, NULL, NULL },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    { "[MCA] IBM PS/2 model 80 (type 3)",              "ibmps2_m80_type3", MACHINE_TYPE_386DX,      MACHINE_CHIPSET_PROPRIETARY,         machine_ps2_model_80_axx_init,    0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386DX | CPU_PKG_486BL | CPU_PKG_SOCKET1, CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2_MCA, MACHINE_VIDEO, 2048, 65536, 2048,  63, NULL, NULL },

    /* 386DX/486 machines */
    /* The BIOS sends commands C9 without a parameter and D5, both of which are
       Phoenix MultiKey commands. */
    { "[OPTi 495] Award 486 clone",                    "award495",         MACHINE_TYPE_386DX_486,  MACHINE_CHIPSET_OPTI_495,            machine_at_opti495_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386DX | CPU_PKG_SOCKET1,                 CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_VLB, MACHINE_IDE, 1024, 32768, 1024, 127, NULL, NULL },
    /* Has AMIKey F KBC firmware. */
    { "[OPTi 495] DataExpert SX495",                   "ami495",           MACHINE_TYPE_386DX_486,  MACHINE_CHIPSET_OPTI_495,            machine_at_opti495_ami_init,      0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386DX | CPU_PKG_SOCKET1,                 CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_VLB, MACHINE_IDE, 1024, 32768, 1024, 127, NULL, NULL },
    /* Has AMIKey F KBC firmware (it's just the MR BIOS for the above machine). */
    { "[OPTi 495] DataExpert SX495 (MR BIOS)",         "mr495",            MACHINE_TYPE_386DX_486,  MACHINE_CHIPSET_OPTI_495,            machine_at_opti495_mr_init,       0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_386DX | CPU_PKG_SOCKET1,                 CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_VLB, MACHINE_IDE, 1024, 32768, 1024, 127, NULL, NULL },

    /* 486 machines - Socket 1 */
    /* Has JetKey 5 KBC Firmware which looks like it is a clone of AMIKey type F.
       It also has those Ex commands also seen on the VIA VT82C42N (the BIOS
       supposedly sends command EF.
       The board was also seen in 2003 with a -H string - perhaps someone swapped
       the KBC? */
    { "[ALi M1429] Olystar LIL1429",                   "ali1429",          MACHINE_TYPE_486,        MACHINE_CHIPSET_ALI_M1429,           machine_at_ali1429_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET1,                                 CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_VLB, MACHINE_IDE, 1024, 32768, 1024, 127, NULL, NULL },
    /* Has JetKey 5 KBC Firmware - but the BIOS string ends in a hardcoded -F, and
       the BIOS also explicitly expects command A1 to return a 'F', so it looks like
       the JetKey 5 is a clone of AMIKey type F. */
    { "[CS4031] AMI 486 CS4031",                       "cs4031",           MACHINE_TYPE_486,        MACHINE_CHIPSET_CT_CS4031,           machine_at_cs4031_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET1,                                 CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_VLB, MACHINE_FLAGS_NONE, 1024, 65536, 1024, 127, NULL, NULL },
    /* Uses some variant of Phoenix MultiKey/42 as the Intel 8242 chip has a Phoenix
       copyright. */
    { "[OPTi 895] Mylex MVI486",                       "mvi486",           MACHINE_TYPE_486,        MACHINE_CHIPSET_OPTI_895_802G,       machine_at_mvi486_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET1,                                 CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_VLB, MACHINE_IDE_DUAL, 1024, 65536, 1024, 127, NULL, NULL },
    /* Has AMI KF KBC firmware. */
    { "[SiS 401] ASUS ISA-486",                        "isa486",           MACHINE_TYPE_486,        MACHINE_CHIPSET_SIS_401,             machine_at_isa486_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET1,                                 CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_IDE, 1024, 65536, 1024, 127, NULL, NULL },
    /* Has AMIKey H KBC firmware, per the screenshot in "How computers & MS-DOS work". */
    { "[SiS 401] Chaintech 433SC",                     "sis401",           MACHINE_TYPE_486,        MACHINE_CHIPSET_SIS_401,             machine_at_sis401_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET1,                                 CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_IDE, 1024, 65536, 1024, 127, NULL, NULL },
    /* Has AMIKey F KBC firmware, per a photo of a monitor with the BIOS screen on
       eBay. */
    { "[SiS 460] ABIT AV4",                            "av4",              MACHINE_TYPE_486,        MACHINE_CHIPSET_SIS_460,             machine_at_av4_init,              0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET1,                                 CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_VLB, MACHINE_IDE, 1024, 65536, 1024, 127, NULL, NULL },
    /* Has a MR (!) KBC firmware, which is a clone of the standard IBM PS/2 KBC firmware. */
    { "[SiS 471] SiS VL-BUS 471 REV. A1",              "px471",            MACHINE_TYPE_486,        MACHINE_CHIPSET_SIS_471,             machine_at_px471_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET1,                                 CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_VLB, MACHINE_IDE, 1024, 131072, 1024, 127, NULL, NULL },
    /* The chip is a Lance LT38C41, a clone of the Intel 8041, and the BIOS sends
       commands BC, BD, and C9 which exist on both AMIKey and Phoenix MultiKey/42,
       but it does not write a byte after C9, which is consistent with AMIKey, so
       this must have some form of AMIKey. */
    { "[VIA VT82C495] FIC 486-VC-HD",                  "486vchd",          MACHINE_TYPE_486,        MACHINE_CHIPSET_VIA_VT82C495,        machine_at_486vchd_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET1,                                 CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_AT, MACHINE_FLAGS_NONE, 1024, 64512, 1024, 127, NULL, NULL },
    /* According to Deksor on the Win3x.org forum, the BIOS string ends in a -0,
       indicating an unknown KBC firmware. But it does send the AMIKey get version
       command, so it must expect an AMIKey. */
    { "[VLSI 82C480] HP Vectra 486VL",                 "vect486vl",        MACHINE_TYPE_486,        MACHINE_CHIPSET_VLSI_VL82C480,       machine_at_vect486vl_init,        0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET1,                                 CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_IDE | MACHINE_VIDEO, 2048, 32768, 2048, 127, at_vect486vl_get_device, NULL },
    /* Has a standard IBM PS/2 KBC firmware or a clone thereof. */
    { "[VLSI 82C481] Siemens Nixdorf D824",            "d824",             MACHINE_TYPE_486,        MACHINE_CHIPSET_VLSI_VL82C481,       machine_at_d824_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET1,                                 CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_IDE | MACHINE_VIDEO, 2048, 32768, 2048, 127, at_d824_get_device, NULL },

    /* 486 machines - Socket 2 */
    /* 486 machines with just the ISA slot */
    /* Uses some variant of Phoenix MultiKey/42 as the BIOS sends keyboard controller
       command C7 (OR input byte with received data byte). */
    { "[ACC 2168] Packard Bell PB410A",                "pb410a",           MACHINE_TYPE_486_S2,     MACHINE_CHIPSET_ACC_2168,            machine_at_pb410a_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_IDE | MACHINE_VIDEO, 4096, 36864, 1024, 127, NULL, NULL },
    /* Uses an ACER/NEC 90M002A (UPD82C42C, 8042 clone) with unknown firmware (V4.01H). */
    { "[ALi M1429G] Acer A1G",                         "acera1g",          MACHINE_TYPE_486_S2,     MACHINE_CHIPSET_ALI_M1429G,          machine_at_acera1g_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_IDE_DUAL | MACHINE_VIDEO, 4096, 36864, 1024, 127, at_acera1g_get_device, NULL },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    { "[ALi M1429G] Kaimei SA-486 VL-BUS M.B.",        "win486",           MACHINE_TYPE_486_S2,     MACHINE_CHIPSET_ALI_M1429G,          machine_at_winbios1429_init,      0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_VLB, MACHINE_IDE, 1024, 32768, 1024, 127, NULL, NULL },
    /* Uses an Intel KBC with Phoenix MultiKey KBC firmware. */
    { "[SiS 461] DEC DECpc LPV",                       "decpclpv",         MACHINE_TYPE_486_S2,     MACHINE_CHIPSET_SIS_461,             machine_at_decpclpv_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_IDE_DUAL | MACHINE_VIDEO, 1024, 32768, 1024, 127, NULL, NULL },
    /* Uses an NEC 90M002A (UPD82C42C, 8042 clone) with unknown firmware. */
    { "[SiS 461] Acer V10",                            "acerv10",          MACHINE_TYPE_486_S2,     MACHINE_CHIPSET_SIS_461,             machine_at_acerv10_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_IDE_DUAL | MACHINE_VIDEO, 1024, 32768, 1024, 127, NULL, NULL },
    /* The BIOS does not send any non-standard keyboard controller commands and wants
       a PS/2 mouse, so it's an IBM PS/2 KBC (Type 1) firmware. */
    { "[SiS 461] IBM PS/ValuePoint 433DX/Si",          "valuepoint433",    MACHINE_TYPE_486_S2,     MACHINE_CHIPSET_SIS_461,             machine_at_valuepoint433_init,    0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2, MACHINE_IDE | MACHINE_VIDEO, 1024, 65536, 1024, 127, NULL, NULL },
    /* The BIOS string ends in -U, unless command 0xA1 (AMIKey get version) returns an
       'F', in which case, it ends in -F, so it has an AMIKey F KBC firmware.
       The photo of the board shows an AMIKey KBC which is indeed F. */
    { "[SiS 471] ABit AB-AH4",                         "win471",           MACHINE_TYPE_486_S2,     MACHINE_CHIPSET_SIS_471,             machine_at_win471_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_VLB, MACHINE_IDE, 1024, 65536, 1024, 127, NULL, NULL },

    /* 486 machines - Socket 3 */
    /* 486 machines with just the ISA slot */
    /* Has AMI MegaKey KBC firmware. */
    { "[Contaq 82C597] Green-B",                       "greenb",           MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_CONTAQ_82C597,       machine_at_greenb_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_VLB, MACHINE_FLAGS_NONE, 1024, 65536, 1024, 127, NULL, NULL },
    /* Has a VIA VT82C42N KBC. */
    { "[OPTi 895] Jetway J-403TG",                     "403tg",            MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_OPTI_895_802G,       machine_at_403tg_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_VLB, MACHINE_FLAGS_NONE, 1024, 65536, 1024, 127, NULL, NULL },
    /* Has JetKey 5 KBC Firmware which looks like it is a clone of AMIKey type F. */
    { "[OPTi 895] Jetway J-403TG Rev D",               "403tg_d",          MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_OPTI_895_802G,       machine_at_403tg_d_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_VLB, MACHINE_FLAGS_NONE, 1024, 65536, 1024, 127, NULL, NULL },
    /* Has JetKey 5 KBC Firmware which looks like it is a clone of AMIKey type F. */
    { "[OPTi 895] Jetway J-403TG Rev D (MR BIOS)",     "403tg_d_mr",       MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_OPTI_895_802G,       machine_at_403tg_d_mr_init,       0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_VLB, MACHINE_FLAGS_NONE, 1024, 65536, 1024, 127, NULL, NULL },
    /* Has AMIKey H keyboard BIOS. */
    { "[SiS 471] AOpen Vi15G",                         "vi15g",            MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_SIS_471,             machine_at_vi15g_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_VLB, MACHINE_IDE, 1024, 65536, 1024, 127, NULL, NULL },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    { "[SiS 471] ASUS VL/I-486SV2G (GX4)",             "vli486sv2g",       MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_SIS_471,             machine_at_vli486sv2g_init,       0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2_VLB, MACHINE_IDE_DUAL, 1024, 65536, 1024, 127, NULL, NULL },
    /* Has JetKey 5 KBC Firmware which looks like it is a clone of AMIKey type F. */
    { "[SiS 471] DTK PKM-0038S E-2",                   "dtk486",           MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_SIS_471,             machine_at_dtk486_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_VLB, MACHINE_IDE, 1024, 65536, 1024, 127, NULL, NULL },
    /* Unknown Epox VLB Socket 3 board, has AMIKey F keyboard BIOS. */
    { "[SiS 471] Epox 486SX/DX Green",                 "ami471",           MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_SIS_471,             machine_at_ami471_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_VLB, MACHINE_IDE, 1024, 65536, 1024, 127, NULL, NULL },

    /* 486 machines which utilize the PCI bus */
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    { "[ALi M1489] AAEON SBC-490",                     "sbc490",           MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_ALI_M1489,           machine_at_sbc490_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL | MACHINE_VIDEO, 1024,  65536, 1024, 255, at_sbc490_get_device, NULL },
    /* Has the ALi M1487/9's on-chip keyboard controller which clones a standard AT
       KBC. */
    { "[ALi M1489] ABIT AB-PB4",                       "abpb4",            MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_ALI_M1489,           machine_at_abpb4_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PCI, MACHINE_IDE_DUAL, 1024, 65536, 1024, 255, NULL, NULL },
    /* Has the ALi M1487/9's on-chip keyboard controller which clones a standard AT
       KBC.
       The BIOS string always ends in -U, but the BIOS will send AMIKey commands 0xCA
       and 0xCB if command 0xA1 returns a letter in the 0x5x or 0x7x ranges, so I'm
       going to give it an AMI 'U' KBC. */
    { "[ALi M1489] AMI WinBIOS 486 PCI",               "win486pci",        MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_ALI_M1489,           machine_at_win486pci_init,        0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PCI, MACHINE_IDE_DUAL, 1024, 65536, 1024, 255, NULL, NULL },
    /* Has the ALi M1487/9's on-chip keyboard controller which clones a standard AT
       KBC.
       The known BIOS string ends in -E, and the BIOS returns whatever command 0xA1
       returns (but only if command 0xA1 is instant response), so said ALi keyboard
       controller likely returns 'E'. */
    { "[ALi M1489] MSI MS-4145",                       "ms4145",           MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_ALI_M1489,           machine_at_ms4145_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PCI, MACHINE_IDE_DUAL, 1024, 65536, 1024, 255, NULL, NULL },
    /* Has an ALi M5042 keyboard controller with Phoenix MultiKey/42 v1.40 firmware. */
    { "[ALi M1489] ESA TF-486",                        "tf486",            MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_ALI_M1489,           machine_at_tf486_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 1024, 65536, 1024, 255, NULL, NULL },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    { "[OPTi 802G] IBM PC 330 (type 6573)",            "pc330_6573",       MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_OPTI_895_802G,       machine_at_pc330_6573_init,       0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3_PC330,             CPU_BLOCK_NONE, 25000000, 33333333, 0, 0, 2.0, 3.0, MACHINE_PS2_PCI, MACHINE_IDE, 1024, 65536, 1024, 127, NULL, NULL },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    { "[i420EX] ASUS PVI-486AP4",                      "486ap4",           MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_INTEL_420EX,         machine_at_486ap4_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2_PCIV, MACHINE_IDE_DUAL, 1024, 131072, 1024, 127, NULL, NULL },
    /* This has the Phoenix MultiKey KBC firmware. */
    { "[i420EX] Intel Classic/PCI ED",                 "ninja",            MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_INTEL_420EX,         machine_at_ninja_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 1024, 131072, 1024, 127, NULL, NULL },
    /* I'm going to assume this as an AMIKey-2 like the other two 486SP3's. */
    { "[i420TX] ASUS PCI/I-486SP3",                    "486sp3",           MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_INTEL_420TX,         machine_at_486sp3_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PCI, MACHINE_IDE_DUAL | MACHINE_SCSI, 1024, 131072, 1024, 127, NULL, NULL },
    /* This has the Phoenix MultiKey KBC firmware. */
    { "[i420TX] Intel Classic/PCI",                    "alfredo",          MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_INTEL_420TX,         machine_at_alfredo_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 2048, 131072, 2048, 127, NULL, NULL },
    /* This has an AMIKey-2, which is an updated version of type 'H'. Also has a
       SST 29EE010 Flash chip. */
    { "[i420ZX] ASUS PCI/I-486SP3G",                   "486sp3g",          MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_INTEL_420ZX,         machine_at_486sp3g_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL | MACHINE_SCSI, 1024, 131072, 1024, 127, NULL, NULL },
    /* This most likely has a standalone AMI Megakey 1993, which is type 'P', like the below Tekram board. */
    { "[IMS 8848] J-Bond PCI400C-B",                   "pci400cb",         MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_IMS_8848,            machine_at_pci400cb_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 2048, 131072, 2048, 127, NULL, NULL },
    /* This has a standalone AMI Megakey 1993, which is type 'P'. */
    { "[IMS 8848] Tekram G486IP",                      "g486ip",           MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_IMS_8848,            machine_at_g486ip_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 2048, 131072, 2048, 127, NULL, NULL },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    { "[SiS 496] ASUS PVI-486SP3C",                    "486sp3c",          MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_SIS_496,             machine_at_486sp3c_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2_PCIV, MACHINE_IDE_DUAL, 1024, 261120, 1024, 255, NULL, NULL },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    { "[SiS 496] Lucky Star LS-486E",                  "ls486e",           MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_SIS_496,             machine_at_ls486e_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PCI, MACHINE_IDE_DUAL, 1024, 131072, 1024, 255, NULL, NULL },
    /* The BIOS does not send a single non-standard KBC command, so it has a standard PS/2 KBC. */
    { "[SiS 496] Micronics M4Li",                      "m4li",             MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_SIS_496,             machine_at_m4li_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 1024, 131072, 1024, 127, NULL, NULL },
    /* Has a BestKey KBC which clones AMI type 'H'. */
    { "[SiS 496] Rise Computer R418",                  "r418",             MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_SIS_496,             machine_at_r418_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PCI, MACHINE_IDE_DUAL, 1024, 261120, 1024, 255, NULL, NULL },
    /* This has a Holtek KBC and the BIOS does not send a single non-standard KBC command, so it
       must be an ASIC that clones the standard IBM PS/2 KBC. */
    { "[SiS 496] Soyo 4SAW2",                          "4saw2",            MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_SIS_496,             machine_at_4saw2_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_SOYO_4SAW2, 0, 0, 0, 0, 0, 0, MACHINE_PS2_PCIV, MACHINE_IDE_DUAL, 1024, 261120, 1024, 255, NULL, NULL },
    /* According to MrKsoft, his real 4DPS has an AMIKey-2, which is an updated version
       of type 'H'. */
    { "[SiS 496] Zida Tomato 4DP",                     "4dps",             MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_SIS_496,             machine_at_4dps_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 1024, 261120, 1024, 255, NULL, NULL },
    /* This has the UMC 88xx on-chip KBC. */
    { "[UMC 8881] A-Trend ATC-1415",                   "atc1415",          MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_UMC_UM8881,          machine_at_atc1415_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PCI, MACHINE_IDE_DUAL, 1024,  65536, 1024, 255, NULL, NULL },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    { "[UMC 8881] ECS Elite UM8810PAIO",               "ecs486",           MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_UMC_UM8881,          machine_at_ecs486_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PCI, MACHINE_IDE_DUAL, 1024, 131072, 1024, 255, NULL, NULL },
    /* Has AMIKey Z(!) KBC firmware. */
    { "[UMC 8881] Epson Action PC 2600",               "actionpc2600",     MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_UMC_UM8881,          machine_at_actionpc2600_init,     0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PCI, MACHINE_IDE_DUAL, 1024, 262144, 1024, 255, NULL, NULL },
    /* This has the UMC 88xx on-chip KBC. All the copies of the BIOS string I can find, end in
       in -H, so the UMC on-chip KBC likely emulates the AMI 'H' KBC firmware. */
    { "[UMC 8881] PC Chips M919",                      "m919",             MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_UMC_UM8881,          machine_at_m919_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PCIV, MACHINE_IDE_DUAL, 1024, 131072, 1024, 255, NULL, NULL },
    /* Has IBM PS/2 Type 1 KBC firmware. Uses a mysterious I/O port C05. */
    { "[UMC 8881] Samsung SPC7700P-LW",                "spc7700plw",       MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_UMC_UM8881,          machine_at_spc7700plw_init,       0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 1024, 131072, 1024, 255, NULL, NULL },
    /* This has a Holtek KBC. */
    { "[UMC 8881] Shuttle HOT-433A",                   "hot433",           MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_UMC_UM8881,          machine_at_hot433_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PCI, MACHINE_IDE_DUAL, 1024, 262144, 1024, 255, NULL, NULL },
    /* Has a VIA VT82C406 KBC+RTC that likely has identical commands to the VT82C42N. */
    { "[VIA VT82C496G] DFI G486VPA",                   "g486vpa",          MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_VIA_VT82C496G,       machine_at_g486vpa_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PCIV, MACHINE_IDE_DUAL, 1024, 131072, 1024, 255, NULL, NULL },
    /* Has a VIA VT82C42N KBC. */
    { "[VIA VT82C496G] FIC VIP-IO2",                   "486vipio2",        MACHINE_TYPE_486_S3,     MACHINE_CHIPSET_VIA_VT82C496G,       machine_at_486vipio2_init,        0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET3,                   CPU_BLOCK_NONE, 0, 0, 0, 0, 0, 0, MACHINE_PS2_PCIV, MACHINE_IDE_DUAL, 1024, 131072, 1024, 255, NULL, NULL },

    /* 486 machines - Miscellaneous */
    /* 486 machines which utilize the PCI bus */
    /* Has a Winbond W83977F Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[STPC Client] ITOX STAR",                       "itoxstar",         MACHINE_TYPE_486_MISC,   MACHINE_CHIPSET_STPC_CLIENT,         machine_at_itoxstar_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_STPC,                      CPU_BLOCK_NONE, 66666667, 75000000, 0, 0, 1.0, 1.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 255, NULL, NULL },
    /* Has a Winbond W83977F Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[STPC Consumer-II] Acrosser AR-B1423C",         "arb1423c",         MACHINE_TYPE_486_MISC,   MACHINE_CHIPSET_STPC_CONSUMER_II,    machine_at_arb1423c_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_STPC,                      CPU_BLOCK_NONE, 66666667, 66666667, 0, 0, 2.0, 2.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 32768, 163840, 8192, 255, NULL, NULL },
    /* Has a Winbond W83977F Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[STPC Consumer-II] Acrosser AR-B1479",          "arb1479",          MACHINE_TYPE_486_MISC,   MACHINE_CHIPSET_STPC_CONSUMER_II,    machine_at_arb1479_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_STPC,                      CPU_BLOCK_NONE, 66666667, 66666667, 0, 0, 2.0, 2.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 32768, 163840, 8192, 255, NULL, NULL },
    /* Has a Winbond W83977F Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[STPC Elite] Advantech PCM-9340",               "pcm9340",          MACHINE_TYPE_486_MISC,   MACHINE_CHIPSET_STPC_ELITE,          machine_at_pcm9340_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_STPC,                      CPU_BLOCK_NONE, 66666667, 66666667, 0, 0, 2.0, 2.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 32768, 98304, 8192, 255, NULL, NULL },
    /* Has a Winbond W83977F Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[STPC Atlas] AAEON PCM-5330",                   "pcm5330",          MACHINE_TYPE_486_MISC,   MACHINE_CHIPSET_STPC_ATLAS,          machine_at_pcm5330_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_STPC,                      CPU_BLOCK_NONE, 66666667, 66666667, 0, 0, 2.0, 2.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 32768, 131072,32768, 255, NULL, NULL },

    /* Socket 4 machines */
    /* 430LX */
    /* Has AMIKey H KBC firmware (AMIKey-2), per POST screen with BIOS string
       shown in the manual. Has PS/2 mouse support with serial-style (DB9)
       connector.
       The boot block for BIOS recovery requires an unknown bit on port 805h
       to be clear. */
    { "[i430LX] AMI Excalibur PCI Pentium",            "excaliburpci",     MACHINE_TYPE_SOCKET4,    MACHINE_CHIPSET_INTEL_430LX,         machine_at_excaliburpci_init,     0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET4,                   CPU_BLOCK_NONE, 60000000, 66666667, 5000, 5000, MACHINE_MULTIPLIER_FIXED, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 2048, 131072, 2048, 127, NULL, NULL },
    /* Has AMIKey F KBC firmware (AMIKey). */
    { "[i430LX] ASUS P/I-P5MP3",                       "p5mp3",            MACHINE_TYPE_SOCKET4,    MACHINE_CHIPSET_INTEL_430LX,         machine_at_p5mp3_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET4,                   CPU_BLOCK_NONE, 60000000, 66666667, 5000, 5000, MACHINE_MULTIPLIER_FIXED, MACHINE_PS2_PCI, MACHINE_IDE, 2048, 196608, 2048, 127, NULL, NULL },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    { "[i430LX] Dell Dimension XPS P60",               "dellxp60",         MACHINE_TYPE_SOCKET4,    MACHINE_CHIPSET_INTEL_430LX,         machine_at_dellxp60_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET4,                   CPU_BLOCK_NONE, 60000000, 66666667, 5000, 5000, MACHINE_MULTIPLIER_FIXED, MACHINE_PS2_PCI, MACHINE_IDE, 2048, 131072, 2048, 127, NULL, NULL },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    { "[i430LX] Dell OptiPlex 560/L",                  "opti560l",         MACHINE_TYPE_SOCKET4,    MACHINE_CHIPSET_INTEL_430LX,         machine_at_opti560l_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET4,                   CPU_BLOCK_NONE, 60000000, 66666667, 5000, 5000, MACHINE_MULTIPLIER_FIXED, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 2048, 131072, 2048, 127, NULL, NULL },
    /* This has the Phoenix MultiKey KBC firmware.
       This is basically an Intel Batman (*NOT* Batman's Revenge) with a fancier
       POST screen */
    { "[i430LX] AMBRA DP60 PCI",                       "ambradp60",        MACHINE_TYPE_SOCKET4,    MACHINE_CHIPSET_INTEL_430LX,         machine_at_ambradp60_init,        0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET4,                   CPU_BLOCK_NONE, 60000000, 66666667, 5000, 5000, MACHINE_MULTIPLIER_FIXED, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 2048, 131072, 2048, 127, NULL, NULL },
    /* Has IBM PS/2 Type 1 KBC firmware. */
    { "[i430LX] IBM PS/ValuePoint P60",                "valuepointp60",    MACHINE_TYPE_SOCKET4,    MACHINE_CHIPSET_INTEL_430LX,         machine_at_valuepointp60_init,    0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET4,                   CPU_BLOCK_NONE, 60000000, 66666667, 5000, 5000, MACHINE_MULTIPLIER_FIXED, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 2048, 131072, 2048, 127, NULL, NULL },
    /* This has the Phoenix MultiKey KBC firmware. */
    { "[i430LX] Intel Premiere/PCI",                   "revenge",          MACHINE_TYPE_SOCKET4,    MACHINE_CHIPSET_INTEL_430LX,         machine_at_revenge_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET4,                   CPU_BLOCK_NONE, 60000000, 66666667, 5000, 5000, MACHINE_MULTIPLIER_FIXED, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 2048, 131072, 2048, 127, NULL, NULL },
    /* Has AMI MegaKey KBC firmware. */
    { "[i430LX] Micro Star 586MC1",                    "586mc1",           MACHINE_TYPE_SOCKET4,    MACHINE_CHIPSET_INTEL_430LX,         machine_at_586mc1_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET4,                   CPU_BLOCK_NONE, 60000000, 66666667, 5000, 5000, MACHINE_MULTIPLIER_FIXED, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 2048, 131072, 2048, 127, NULL, NULL },
    /* This has the Phoenix MultiKey KBC firmware. */
    { "[i430LX] Packard Bell PB520R",                  "pb520r",           MACHINE_TYPE_SOCKET4,    MACHINE_CHIPSET_INTEL_430LX,         machine_at_pb520r_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET4,                   CPU_BLOCK_NONE, 60000000, 66666667, 5000, 5000, MACHINE_MULTIPLIER_FIXED, MACHINE_PS2_PCI, MACHINE_IDE_DUAL | MACHINE_VIDEO, 8192, 139264, 2048, 127, at_pb520r_get_device, NULL },

    /* OPTi 596/597 */
    /* This uses an AMI KBC firmware in PS/2 mode (it sends command A5 with the
       PS/2 "Load Security" meaning), most likely MegaKey as it sends command AF
       (Set Extended Controller RAM) just like the later Intel AMI BIOS'es. */
    { "[OPTi 597] AMI Excalibur VLB",                  "excalibur",        MACHINE_TYPE_SOCKET4,    MACHINE_CHIPSET_OPTI_547_597,        machine_at_excalibur_init,        0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET4,                   CPU_BLOCK_NONE, 60000000, 60000000, 5000, 5000, MACHINE_MULTIPLIER_FIXED, MACHINE_PS2_VLB, MACHINE_IDE, 2048, 65536, 2048, 127, NULL, NULL },

    /* OPTi 596/597/822 */
    /* This has AMIKey 'F' KBC firmware. */
    { "[OPTi 597] Supermicro P5VL-PCI",                "p5vl",             MACHINE_TYPE_SOCKET4,    MACHINE_CHIPSET_OPTI_547_597,        machine_at_p5vl_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET4,                   CPU_BLOCK_NONE, 60000000, 66666667, 5000, 5000, MACHINE_MULTIPLIER_FIXED, MACHINE_PCIV, MACHINE_FLAGS_NONE, 8192, 131072, 8192, 127, NULL, NULL },

    /* SiS 50x */
    /* This has some form of AMI MegaKey as it uses keyboard controller command 0xCC. */
    { "[SiS 501] AMI Excalibur PCI-II Pentium ISA",    "excaliburpci2",    MACHINE_TYPE_SOCKET4,    MACHINE_CHIPSET_SIS_501,             machine_at_excaliburpci2_init,    0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET4,                   CPU_BLOCK_NONE, 60000000, 66666667, 5000, 5000, MACHINE_MULTIPLIER_FIXED, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    { "[SiS 501] ASUS PCI/I-P5SP4",                    "p5sp4",            MACHINE_TYPE_SOCKET4,    MACHINE_CHIPSET_SIS_501,             machine_at_p5sp4_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET4,                   CPU_BLOCK_NONE, 60000000, 66666667, 5000, 5000, MACHINE_MULTIPLIER_FIXED, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },

    /* Socket 5 machines */
    /* 430NX */
    /* This has the Phoenix MultiKey KBC firmware. */
    { "[i430NX] Intel Premiere/PCI II",                "plato",            MACHINE_TYPE_SOCKET5,    MACHINE_CHIPSET_INTEL_430NX,         machine_at_plato_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3520, 3520, 1.5, 1.5, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 2048, 131072, 2048, 127, NULL, NULL },
    /* This has the Phoenix MultiKey KBC firmware.
       This is basically an Intel Premiere/PCI II with a fancier POST screen. */
    { "[i430NX] AMBRA DP90 PCI",                       "ambradp90",        MACHINE_TYPE_SOCKET5,    MACHINE_CHIPSET_INTEL_430NX,         machine_at_ambradp90_init,        0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3380, 3520, 1.5, 1.5, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 2048, 131072, 2048, 127, NULL, NULL },
    /* Has AMI MegaKey KBC firmware. */
    { "[i430NX] Gigabyte GA-586IP",                    "430nx",            MACHINE_TYPE_SOCKET5,    MACHINE_CHIPSET_INTEL_430NX,         machine_at_430nx_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 60000000, 66666667, 3520, 3520, 1.5, 1.5, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 2048, 131072, 2048, 127, NULL, NULL },

    /* 430FX */
    /* Uses an ACER/NEC 90M002A (UPD82C42C, 8042 clone) with unknown firmware (V5.0). */
    { "[i430FX] Acer V30",                             "acerv30",          MACHINE_TYPE_SOCKET5,    MACHINE_CHIPSET_INTEL_430FX,         machine_at_acerv30_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3380, 3520, 1.5, 2.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* Has AMIKey F KBC firmware. */
    { "[i430FX] AMI Apollo",                           "apollo",           MACHINE_TYPE_SOCKET5,    MACHINE_CHIPSET_INTEL_430FX,         machine_at_apollo_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3380, 3520, 1.5, 2.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* Has AMIKey H KBC firmware. */
    { "[i430FX] DataExpert EXP8551",                   "exp8551",          MACHINE_TYPE_SOCKET5,    MACHINE_CHIPSET_INTEL_430FX,         machine_at_exp8551_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3380, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI . */
    { "[i430FX] Intel Advanced/ZP",                    "zappa",            MACHINE_TYPE_SOCKET5,    MACHINE_CHIPSET_INTEL_430FX,         machine_at_zappa_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3380, 3520, 1.5, 2.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* The BIOS sends KBC command B3 which indicates an AMI (or VIA VT82C42N) KBC. */
    { "[i430FX] NEC PowerMate V",                      "powermatev",       MACHINE_TYPE_SOCKET5,    MACHINE_CHIPSET_INTEL_430FX,         machine_at_powermatev_init,       0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3380, 3520, 1.5, 2.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* Has a VIA VT82C42N KBC. */
    { "[i430FX] PC Partner MB500N",                    "mb500n",           MACHINE_TYPE_SOCKET5,    MACHINE_CHIPSET_INTEL_430FX,         machine_at_mb500n_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3380, 3520, 1.5, 3.0, MACHINE_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* Has AMIKey Z(!) KBC firmware. */
    { "[i430FX] TriGem Hawk",                          "hawk",             MACHINE_TYPE_SOCKET5,    MACHINE_CHIPSET_INTEL_430FX,         machine_at_hawk_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3380, 3520, 1.5, 2.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },

    /* OPTi 596/597 */
    /* This uses an AMI KBC firmware in PS/2 mode (it sends command A5 with the
       PS/2 "Load Security" meaning), most likely MegaKey as it sends command AF
       (Set Extended Controller RAM) just like the later Intel AMI BIOS'es. */
    { "[OPTi 597] TMC PAT54PV",                        "pat54pv",          MACHINE_TYPE_SOCKET5,    MACHINE_CHIPSET_OPTI_547_597,        machine_at_pat54pv_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK(CPU_K5, CPU_5K86), 50000000, 66666667, 3520, 3520, 1.5, 1.5, MACHINE_VLB, MACHINE_FLAGS_NONE, 2048, 65536, 2048, 127, NULL, NULL },

    /* OPTi 596/597/822 */
    { "[OPTi 597] Shuttle HOT-543",                    "hot543",           MACHINE_TYPE_SOCKET5,    MACHINE_CHIPSET_OPTI_547_597,        machine_at_hot543_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE,              50000000, 66666667, 3520, 3520, 1.5, 2.0, MACHINE_PCIV, MACHINE_FLAGS_NONE, 8192, 131072, 8192, 127, NULL, NULL },

    /* SiS 85C50x */
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    { "[SiS 501] ASUS PCI/I-P54SP4",                   "p54sp4",           MACHINE_TYPE_SOCKET5,    MACHINE_CHIPSET_SIS_501,             machine_at_p54sp4_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK(CPU_K5, CPU_5K86), 40000000, 66666667, 3380, 3520, 1.5, 1.5, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    { "[SiS 501] BCM SQ-588",                          "sq588",            MACHINE_TYPE_SOCKET5,    MACHINE_CHIPSET_SIS_501,             machine_at_sq588_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK(CPU_PENTIUMMMX),   50000000, 66666667, 3520, 3520, 1.5, 1.5, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },

    /* Socket 7 (Single Voltage) machines */
    /* 430FX */
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    { "[i430FX] ASUS P/I-P54TP4XE",                    "p54tp4xe",         MACHINE_TYPE_SOCKET7_3V, MACHINE_CHIPSET_INTEL_430FX,         machine_at_p54tp4xe_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE,              50000000, 66666667, 3380, 3600, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    { "[i430FX] ASUS P/I-P54TP4XE (MR BIOS)",          "p54tp4xe_mr",      MACHINE_TYPE_SOCKET7_3V, MACHINE_CHIPSET_INTEL_430FX,         machine_at_p54tp4xe_mr_init,      0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3380, 3600, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI . */
    { "[i430FX] Gateway 2000 Thor",                    "gw2katx",          MACHINE_TYPE_SOCKET7_3V, MACHINE_CHIPSET_INTEL_430FX,         machine_at_gw2katx_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3380, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* The BIOS does not send a single non-standard KBC command, but the board has a SMC Super I/O
       chip with on-chip KBC and AMI MegaKey KBC firmware. */
    { "[i430FX] HP Vectra VL 5 Series 4",              "vectra54",         MACHINE_TYPE_SOCKET7_3V, MACHINE_CHIPSET_INTEL_430FX,         machine_at_vectra54_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3380, 3520, 1.5, 2.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL | MACHINE_VIDEO, 8192, 131072, 8192, 511, at_vectra54_get_device, NULL },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI . */
    { "[i430FX] Intel Advanced/ATX",                   "thor",             MACHINE_TYPE_SOCKET7_3V, MACHINE_CHIPSET_INTEL_430FX,         machine_at_thor_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3380, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL | MACHINE_VIDEO, 8192, 131072, 8192, 127, at_thor_get_device, NULL },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI . */
    { "[i430FX] Intel Advanced/ATX (MR BIOS)",         "mrthor",           MACHINE_TYPE_SOCKET7_3V, MACHINE_CHIPSET_INTEL_430FX,         machine_at_mrthor_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3380, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, at_mrthor_get_device, NULL },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI . */
    { "[i430FX] Intel Advanced/EV",                    "endeavor",         MACHINE_TYPE_SOCKET7_3V, MACHINE_CHIPSET_INTEL_430FX,         machine_at_endeavor_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3380, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL | MACHINE_VIDEO, 8192, 131072, 8192, 127, at_endeavor_get_device, NULL },
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    { "[i430FX] MSI MS-5119",                          "ms5119",           MACHINE_TYPE_SOCKET7_3V, MACHINE_CHIPSET_INTEL_430FX,         machine_at_ms5119_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 2500, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* This most likely uses AMI MegaKey KBC firmware as well due to having the same
       Super I/O chip (that has the KBC firmware on it) as eg. the Advanced/EV. */
    { "[i430FX] Packard Bell PB640",                   "pb640",            MACHINE_TYPE_SOCKET7_3V, MACHINE_CHIPSET_INTEL_430FX,         machine_at_pb640_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE,    50000000, 66666667, 3380, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL | MACHINE_VIDEO, 8192, 131072, 8192, 127, at_pb640_get_device, NULL },
    /* Has an AMI 'H' KBC firmware (1992). */
    { "[i430FX] QDI FMB",                              "fmb",              MACHINE_TYPE_SOCKET7_3V, MACHINE_CHIPSET_INTEL_430FX,         machine_at_fmb_init,              0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_QDI_FMB, 50000000, 66666667, 3380, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },

    /* 430HX */
    /* I can't determine what KBC firmware this has, but given that the Acer V35N and
       V60 have Phoenix MultiKey KBC firmware on the chip, I'm going to assume so
       does the M3A. */
    { "[i430HX] Acer M3A",                             "acerm3a",          MACHINE_TYPE_SOCKET7_3V, MACHINE_CHIPSET_INTEL_430HX,         machine_at_acerm3a_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3300, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 196608, 8192, 127, NULL, NULL },
    /* Has AMIKey F KBC firmware. */
    { "[i430HX] AOpen AP53",                           "ap53",             MACHINE_TYPE_SOCKET7_3V, MACHINE_CHIPSET_INTEL_430HX,         machine_at_ap53_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3450, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 524288, 8192, 127, NULL, NULL },
    /* [TEST] Has a VIA 82C42N KBC, with AMIKey F KBC firmware. */
    { "[i430HX] Biostar MB-8500TUC",                   "8500tuc",          MACHINE_TYPE_SOCKET7_3V, MACHINE_CHIPSET_INTEL_430HX,         machine_at_8500tuc_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3380, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 524288, 8192, 127, NULL, NULL },
    /* [TEST] Unable to determine what KBC this has. A list on a Danish site shows
       the BIOS as having a -0 string, indicating non-AMI KBC firmware. */
    { "[i430HX] Supermicro P55T2S",                    "p55t2s",           MACHINE_TYPE_SOCKET7_3V, MACHINE_CHIPSET_INTEL_430HX,         machine_at_p55t2s_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3300, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 786432, 8192, 127, NULL, NULL },

    /* 430VX */
    /* Has AMIKey H KBC firmware (AMIKey-2). */
    { "[i430VX] ECS P5VX-B",                           "p5vxb",            MACHINE_TYPE_SOCKET7_3V, MACHINE_CHIPSET_INTEL_430VX,         machine_at_p5vxb_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3380, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI . */
    { "[i430VX] Gateway 2000 Tigereye",                "gw2kte",           MACHINE_TYPE_SOCKET7_3V, MACHINE_CHIPSET_INTEL_430VX,         machine_at_gw2kte_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3380, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },

    /* SiS 5511 */
    /* Has AMIKey H KBC firmware (AMIKey-2). */
    { "[SiS 5511] AOpen AP5S",                         "ap5s",             MACHINE_TYPE_SOCKET7_3V, MACHINE_CHIPSET_SIS_5511,            machine_at_ap5s_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 3380, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 524288, 8192, 127, NULL, NULL },

    /* Socket 7 (Dual Voltage) machines */
    /* 430HX */
    /* Has SST flash and the SMC FDC73C935's on-chip KBC with Phoenix MultiKey firmware. */
    { "[i430HX] Acer V35N",                            "acerv35n",         MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430HX,         machine_at_acerv35n_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK(CPU_Cx6x86MX), 50000000, 66666667, 2800, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 196608, 8192, 127, NULL, NULL },
    /* Has AMIKey H KBC firmware (AMIKey-2). */
    { "[i430HX] ASUS P/I-P55T2P4",                     "p55t2p4",          MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430HX,         machine_at_p55t2p4_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 83333333, 2500, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 262144, 8192, 127, NULL, NULL },
    /* Has the SMC FDC73C935's on-chip KBC with Phoenix MultiKey firmware. */
    { "[i430HX] Micronics M7S-Hi",                     "m7shi",            MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430HX,         machine_at_m7shi_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 2800, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 511, NULL, NULL },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI . */
    { "[i430HX] Intel TC430HX",                        "tc430hx",          MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430HX,         machine_at_tc430hx_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 2800, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 255, NULL, NULL },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI . */
    { "[i430HX] Toshiba Equium 5200D",                 "equium5200",       MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430HX,         machine_at_equium5200_init,       0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 2800, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 196608, 8192, 127, NULL, NULL },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI .
       Yes, this is an Intel AMI BIOS with a fancy splash screen. */
    { "[i430HX] Sony Vaio PCV-90",                     "pcv90",            MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430HX,         machine_at_pcv90_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 2800, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 196608, 8192, 127, NULL, NULL },
    /* The base board has AMIKey-2 (updated 'H') KBC firmware. */
    { "[i430HX] ASUS P/I-P65UP5 (C-P55T2D)",           "p65up5_cp55t2d",   MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430HX,         machine_at_p65up5_cp55t2d_init,   0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 2500, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 524288, 8192, 127, NULL, NULL },

    /* 430VX */
    /* This has the VIA VT82C42N KBC. */
    { "[i430VX] AOpen AP5VM",                          "ap5vm",            MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430VX,         machine_at_ap5vm_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 2600, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL | MACHINE_SCSI, 8192, 131072, 8192, 127, NULL, NULL },
    /* Has AMIKey H KBC firmware (AMIKey-2). */
    { "[i430VX] ASUS P/I-P55TVP4",                     "p55tvp4",          MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430VX,         machine_at_p55tvp4_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 2500, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* The BIOS does not send a single non-standard KBC command, so it must have a standard IBM
       PS/2 KBC firmware or a clone thereof. */
    { "[i430VX] Azza PT-5IV",                          "5ivg",             MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430VX,         machine_at_5ivg_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 2500, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* [TEST] Has AMIKey 'F' KBC firmware. */
    { "[i430VX] Biostar MB-8500TVX-A",                 "8500tvxa",         MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430VX,         machine_at_8500tvxa_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 2600, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* The BIOS does not send a single non-standard KBC command, but the board has a SMC Super I/O
       chip with on-chip KBC and AMI MegaKey KBC firmware. */
    { "[i430VX] Compaq Presario 2240",                 "presario2240",     MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430VX,         machine_at_presario2240_init,     0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 2800, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL | MACHINE_VIDEO, 8192, 131072, 8192, 127, at_presario2240_get_device, NULL },
    /* This most likely has AMI MegaKey as above. */
    { "[i430VX] Compaq Presario 4500",                 "presario4500",     MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430VX,         machine_at_presario4500_init,     0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 2800, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL | MACHINE_VIDEO, 8192, 131072, 8192, 127, at_presario4500_get_device, NULL },
    /* The BIOS sends KBC command CB which is an AMI KBC command, so it has an AMI KBC firmware. */
    { "[i430VX] Epox P55-VA",                          "p55va",            MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430VX,         machine_at_p55va_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 2500, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* The BIOS does not send a single non-standard KBC command. */
    { "[i430VX] HP Brio 80xx",                         "brio80xx",         MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430VX,         machine_at_brio80xx_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 66666667, 66666667, 2200, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI . */
    { "[i430VX] Packard Bell PB680",                   "pb680",            MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430VX,         machine_at_pb680_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 2800, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* This has the AMIKey 'H' firmware, possibly AMIKey-2. Photos show it with a BestKey, so it
       likely clones the behavior of AMIKey 'H'. */
    { "[i430VX] PC Partner MB520N",                    "mb520n",           MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430VX,         machine_at_mb520n_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 2600, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* This has a Holtek KBC and the BIOS does not send a single non-standard KBC command, so it
       must be an ASIC that clones the standard IBM PS/2 KBC. */
    { "[i430VX] Shuttle HOT-557",                      "430vx",            MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430VX,         machine_at_i430vx_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 2500, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL | MACHINE_GAMEPORT, 8192, 131072, 8192, 127, NULL, NULL },

    /* 430TX */
    /* The BIOS sends KBC command B8, CA, and CB, so it has an AMI KBC firmware. */
    { "[i430TX] ADLink NuPRO-592",                     "nupro592",         MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430TX,         machine_at_nupro592_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 66666667, 66666667, 1900, 2800, 1.5, 5.5, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 262144, 8192, 255, NULL, NULL },
    /* This has the AMIKey KBC firmware, which is an updated 'F' type (YM430TX is based on the TX97). */
    { "[i430TX] ASUS TX97",                            "tx97",             MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430TX,         machine_at_tx97_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 75000000, 2500, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 262144, 8192, 255, NULL, NULL },
#if defined(DEV_BRANCH) && defined(USE_AN430TX)
    /* This has the Phoenix MultiKey KBC firmware. */
    { "[i430TX] Intel AN430TX",                        "an430tx",          MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430TX,         machine_at_an430tx_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 60000000, 66666667, 2800, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 262144, 8192, 255, NULL, NULL },
#endif
    /* This has the AMIKey KBC firmware, which is an updated 'F' type. */
    { "[i430TX] Intel YM430TX",                        "ym430tx",          MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430TX,         machine_at_ym430tx_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 60000000, 66666667, 2800, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 262144, 8192, 255, NULL, NULL },
    /* The BIOS sends KBC command BB and expects it to output a byte, which is AMI KBC behavior. */
    { "[i430TX] PC Partner MB540N",                    "mb540n",           MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430TX,         machine_at_mb540n_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 60000000, 66666667, 2700, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 262144, 8192, 255, NULL, NULL },
    /* [TEST] Has AMIKey 'H' KBC firmware. */
    { "[i430TX] Supermicro P5MMS98",                   "p5mms98",          MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_INTEL_430TX,         machine_at_p5mms98_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 66666667, 2100, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 262144, 8192, 255, NULL, NULL },

    /* Apollo VPX */
    /* Has the VIA VT82C586B southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    { "[VIA VPX] FIC VA-502",                          "ficva502",         MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_VIA_APOLLO_VPX,      machine_at_ficva502_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 75000000, 2800, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 524288, 8192, 127, NULL, NULL },

    /* Apollo VP3 */
    /* Has the VIA VT82C586B southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    { "[VIA VP3] FIC PA-2012",                         "ficpa2012",        MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_VIA_APOLLO_VP3,      machine_at_ficpa2012_init,        0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 55000000, 75000000, 2100, 3520, 1.5, 5.5, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 1048576, 8192, 127, NULL, NULL },

    /* SiS 5571 */
    /* Has the SiS 5571 chipset with on-chip KBC. */
    { "[SiS 5571] Rise R534F",                         "r534f",            MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_SIS_5571,            machine_at_r534f_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 55000000, 83333333, 2500, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 393216, 8192, 127, NULL, NULL },
    /* Has the SiS 5571 chipset with on-chip KBC. */
    { "[SiS 5571] MSI MS-5146",                        "ms5146",           MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_SIS_5571,            machine_at_ms5146_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 75000000, 2800, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 262144, 8192, 127, NULL, NULL },

    /* ALi ALADDiN IV+ */
    /* Has the ALi M1543 southbridge with on-chip KBC. */
    { "[ALi ALADDiN IV+] PC Chips M560",               "m560",             MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_ALI_ALADDIN_IV_PLUS, machine_at_m560_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 50000000, 83333333, 2500, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 786432, 8192, 255, NULL, NULL },
    /* Has the ALi M1543 southbridge with on-chip KBC. */
    { "[ALi ALADDiN IV+] MSI MS-5164",                 "ms5164",           MACHINE_TYPE_SOCKET7,    MACHINE_CHIPSET_ALI_ALADDIN_IV_PLUS, machine_at_ms5164_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 60000000, 83333333, 2100, 3520, 1.5, 3.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 1048576, 8192, 255, NULL, NULL },

    /* Super Socket 7 machines */
    /* ALi ALADDiN V */
    /* Has the ALi M1543C southbridge with on-chip KBC. */
    { "[ALi ALADDiN V] ASUS P5A",                      "p5a",              MACHINE_TYPE_SOCKETS7,   MACHINE_CHIPSET_ALI_ALADDIN_V,       machine_at_p5a_init,              0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 60000000, 120000000, 2000, 3520, 1.5, 5.5, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 1024, 1572864, 8192, 255, NULL, NULL },
    /* Is the exact same as the Matsonic MS6260S. Has the ALi M1543C southbridge
       with on-chip KBC. */
    { "[ALi ALADDiN V] PC Chips M579",                 "m579",             MACHINE_TYPE_SOCKETS7,   MACHINE_CHIPSET_ALI_ALADDIN_V,       machine_at_m579_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 66666667, 100000000, 2000, 3520, 1.5, 5.5, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 1024, 1572864, 8192, 255, NULL, NULL },
    /* Has the ALi M1543C southbridge with on-chip KBC. */
    { "[ALi ALADDiN V] Gigabyte GA-5AA",               "5aa",              MACHINE_TYPE_SOCKETS7,   MACHINE_CHIPSET_ALI_ALADDIN_V,       machine_at_5aa_init,              0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 66666667, 140000000, 1300, 3520, 1.5, 5.5, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 1024, 1572864, 8192, 255, NULL, NULL },
    /* Has the ALi M1543C southbridge with on-chip KBC. */
    { "[ALi ALADDiN V] Gigabyte GA-5AX",               "5ax",              MACHINE_TYPE_SOCKETS7,   MACHINE_CHIPSET_ALI_ALADDIN_V,       machine_at_5ax_init,              0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 66666667, 140000000, 1300, 3520, 1.5, 5.5, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 1024, 1572864, 8192, 255, NULL, NULL },

    /* Apollo MVP3 */
    /* Has the VIA VT82C586B southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    { "[VIA MVP3] AOpen AX59 Pro",                     "ax59pro",          MACHINE_TYPE_SOCKETS7,   MACHINE_CHIPSET_VIA_APOLLO_MVP3,     machine_at_ax59pro_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 66666667, 124242424, 1300, 3520, 1.5, 5.5, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 1048576, 8192, 255, NULL, NULL },
    /* Has the VIA VT82C586B southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    { "[VIA MVP3] FIC VA-503+",                        "ficva503p",        MACHINE_TYPE_SOCKETS7,   MACHINE_CHIPSET_VIA_APOLLO_MVP3,     machine_at_mvp3_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 66666667, 124242424, 2000, 3200, 1.5, 5.5, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 1048576, 8192, 255, NULL, NULL },
    /* Has the VIA VT82C686A southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    { "[VIA MVP3] FIC VA-503A",                        "ficva503a",        MACHINE_TYPE_SOCKETS7,   MACHINE_CHIPSET_VIA_APOLLO_MVP3,     machine_at_ficva503a_init,        0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 66666667, 124242424, 1800, 3100, 1.5, 5.5, MACHINE_PS2_A97, MACHINE_IDE_DUAL | MACHINE_SOUND, 8192, 786432, 8192, 255, NULL, NULL },
    /* Has the VIA VT82C686A southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    { "[VIA MVP3] Soyo 5EMA PRO",                      "5emapro",          MACHINE_TYPE_SOCKETS7,   MACHINE_CHIPSET_VIA_APOLLO_MVP3,     machine_at_5emapro_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET5_7,                 CPU_BLOCK_NONE, 66666667, 124242424, 2000, 3520, 1.5, 5.5, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 786432, 8192, 255, NULL, NULL },

    /* Socket 8 machines */
    /* 450KX */
    /* This has an AMIKey-2, which is an updated version of type 'H'. */
    { "[i450KX] ASUS P/I-P6RP4",                       "p6rp4",            MACHINE_TYPE_SOCKET8,    MACHINE_CHIPSET_INTEL_450KX,         machine_at_p6rp4_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET8,                   CPU_BLOCK_NONE, 60000000, 66666667, 2100, 3500, 1.5, 8.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 524288, 8192, 127, NULL, NULL },

    /* 440FX */
    /* Has the SMC FDC73C935's on-chip KBC with Phoenix MultiKey firmware. */
    { "[i440FX] Acer V60N",                            "acerv60n",         MACHINE_TYPE_SOCKET8,    MACHINE_CHIPSET_INTEL_440FX,         machine_at_acerv60n_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET8,                   CPU_BLOCK_NONE, 60000000, 66666667, 2500, 3500, 1.5, 8.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 524288, 8192, 127, NULL, NULL },
    /* The base board has AMIKey-2 (updated 'H') KBC firmware. */
    { "[i440FX] ASUS P/I-P65UP5 (C-P6ND)",             "p65up5_cp6nd",     MACHINE_TYPE_SOCKET8,    MACHINE_CHIPSET_INTEL_440FX,         machine_at_p65up5_cp6nd_init,     0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET8,                   CPU_BLOCK_NONE, 60000000, 66666667, 2100, 3500, 1.5, 8.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 1048576, 8192, 127, NULL, NULL },
    /* The MB-8600TTX has an AMIKey 'F' KBC firmware, so I'm going to assume so does
       the MB-8600TTC until someone can actually identify it. */
    { "[i440FX] Biostar MB-8600TTC",                   "8600ttc",          MACHINE_TYPE_SOCKET8,    MACHINE_CHIPSET_INTEL_440FX,         machine_at_8600ttc_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET8,                   CPU_BLOCK_NONE, 50000000, 66666667, 2900, 3300, 2.0, 5.5, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 1048576, 8192, 127, NULL, NULL },
    { "[i440FX] Gigabyte GA-686NX",                    "686nx",            MACHINE_TYPE_SOCKET8,    MACHINE_CHIPSET_INTEL_440FX,         machine_at_686nx_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET8,                   CPU_BLOCK_NONE, 60000000, 66666667, 2100, 3500, 2.0, 5.5, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 524288, 8192, 127, NULL, NULL },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI . */
    { "[i440FX] Intel AP440FX",                        "ap440fx",          MACHINE_TYPE_SOCKET8,    MACHINE_CHIPSET_INTEL_440FX,         machine_at_ap440fx_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET8,                   CPU_BLOCK_NONE, 60000000, 66666667, 2100, 3500, 2.0, 3.5, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 131072, 8192, 127, NULL, NULL },
    /* According to tests from real hardware: This has AMI MegaKey KBC firmware on the
       PC87306 Super I/O chip, command 0xA1 returns '5'.
       Command 0xA0 copyright string: (C)1994 AMI . */
    { "[i440FX] Intel VS440FX",                        "vs440fx",          MACHINE_TYPE_SOCKET8,    MACHINE_CHIPSET_INTEL_440FX,         machine_at_vs440fx_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET8,                   CPU_BLOCK_NONE, 60000000, 66666667, 2100, 3500, 2.0, 3.5, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 524288, 8192, 127, NULL, NULL },
    /* Has the SMC FDC73C935's on-chip KBC with Phoenix MultiKey firmware. */
    { "[i440FX] Micronics M6Mi",                       "m6mi",             MACHINE_TYPE_SOCKET8,    MACHINE_CHIPSET_INTEL_440FX,         machine_at_m6mi_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET8,                   CPU_BLOCK_NONE, 60000000, 66666667, 2900, 3300, 1.5, 8.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 786432, 8192, 127, NULL, NULL },
    /* I found a BIOS string of it that ends in -S, but it could be a typo for -5
       (there's quite a few AMI BIOS strings around with typo'd KBC codes), so I'm
       going to give it an AMI MegaKey. */
    { "[i440FX] PC Partner MB600N",                    "mb600n",           MACHINE_TYPE_SOCKET8,    MACHINE_CHIPSET_INTEL_440FX,         machine_at_mb600n_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET8,                   CPU_BLOCK_NONE, 60000000, 66666667, 2100, 3500, 1.5, 8.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 524288, 8192, 127, NULL, NULL },

    /* Slot 1 machines */
    /* ALi ALADDiN V */
    /* Has the ALi M1543C southbridge with on-chip KBC. */
    { "[ALi ALADDiN-PRO II] PC Chips M729",            "m729",             MACHINE_TYPE_SLOT1,      MACHINE_CHIPSET_ALI_ALADDIN_PRO_II,  machine_at_m729_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK_NONE, 66666667, 100000000, 1800, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 1024, 1572864, 8192, 255, NULL, NULL },

    /* 440FX */
    /* The base board has AMIKey-2 (updated 'H') KBC firmware. */
    { "[i440FX] ASUS P/I-P65UP5 (C-PKND)",             "p65up5_cpknd",     MACHINE_TYPE_SLOT1,      MACHINE_CHIPSET_INTEL_440FX,         machine_at_p65up5_cpknd_init,     0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK_NONE,                       50000000, 66666667, 1800, 3500, 1.5, 8.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 1048576, 8192, 127, NULL, NULL },
    /* This has a Holtek KBC and the BIOS does not send a single non-standard KBC command, so it
       must be an ASIC that clones the standard IBM PS/2 KBC. */
    { "[i440FX] ASUS KN97",                            "kn97",             MACHINE_TYPE_SLOT1,      MACHINE_CHIPSET_INTEL_440FX,         machine_at_kn97_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK_NONE,                       60000000, 83333333, 1800, 3500, 1.5, 8.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 786432, 8192, 127, NULL, NULL },

    /* 440LX */
    /* Has a Winbond W83977TF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[i440LX] ABIT LX6",                             "lx6",              MACHINE_TYPE_SLOT1,      MACHINE_CHIPSET_INTEL_440LX,         machine_at_lx6_init,              0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK_NONE,                       60000000, 100000000, 1500, 3500, 2.0, 5.5, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 1048576, 8192, 255, NULL, NULL },
    /* Has a SM(S)C FDC37C935 Super I/O chip with on-chip KBC with Phoenix
       MultiKey KBC firmware. */
    { "[i440LX] Micronics Spitfire",                   "spitfire",         MACHINE_TYPE_SLOT1,      MACHINE_CHIPSET_INTEL_440LX,         machine_at_spitfire_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK_NONE,                       66666667, 66666667, 1800, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 1048576, 8192, 255, NULL, NULL },

    /* 440EX */
    /* Has a Winbond W83977TF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[i440EX] QDI EXCELLENT II",                     "p6i440e2",         MACHINE_TYPE_SLOT1,      MACHINE_CHIPSET_INTEL_440EX,         machine_at_p6i440e2_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK_NONE,                       66666667, 83333333, 1800, 3500, 3.0, 8.0, MACHINE_PS2_PCI, MACHINE_IDE_DUAL, 8192, 524288, 8192, 255, NULL, NULL },

    /* 440BX */
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[i440BX] ASUS P2B-LS",                          "p2bls",            MACHINE_TYPE_SLOT1,      MACHINE_CHIPSET_INTEL_440BX,         machine_at_p2bls_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK_NONE,                       50000000, 112121212, 1300, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 1048576, 8192, 255, NULL, NULL },
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[i440BX] ASUS P3B-F",                           "p3bf",             MACHINE_TYPE_SLOT1,      MACHINE_CHIPSET_INTEL_440BX,         machine_at_p3bf_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK_NONE,                       66666667, 150000000, 1300, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 1048576, 8192, 255, NULL, NULL },
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[i440BX] ABIT BF6",                             "bf6",              MACHINE_TYPE_SLOT1,      MACHINE_CHIPSET_INTEL_440BX,         machine_at_bf6_init,              0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK_NONE,                       66666667, 133333333, 1800, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 786432, 8192, 255, NULL, NULL },
    /* Has a Winbond W83977TF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[i440BX] AOpen AX6BC",                          "ax6bc",            MACHINE_TYPE_SLOT1,      MACHINE_CHIPSET_INTEL_440BX,         machine_at_ax6bc_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK_NONE,                       66666667, 112121212, 1800, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 786432, 8192, 255, NULL, NULL },
    /* Has a Winbond W83977TF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[i440BX] Gigabyte GA-686BX",                    "686bx",            MACHINE_TYPE_SLOT1,      MACHINE_CHIPSET_INTEL_440BX,         machine_at_686bx_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK_NONE,                       66666667, 100000000, 1800, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192,1048576, 8192, 255, NULL, NULL },
    /* Has a SM(S)C FDC37M60x Super I/O chip with on-chip KBC with most likely
       AMIKey-2 KBC firmware. */
    { "[i440BX] HP Vectra VEi 8",                      "vei8",             MACHINE_TYPE_SLOT1,      MACHINE_CHIPSET_INTEL_440BX,         machine_at_vei8_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK_NONE,                       66666667, 100000000, 1800, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192,1048576, 8192, 255, NULL, NULL },
    /* Has a National Semiconductors PC87309 Super I/O chip with on-chip KBC
       with most likely AMIKey-2 KBC firmware. */
    { "[i440BX] Tyan Tsunami ATX",                     "s1846",            MACHINE_TYPE_SLOT1,      MACHINE_CHIPSET_INTEL_440BX,         machine_at_s1846_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK_NONE,                       66666667, 112121212, 1800, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL | MACHINE_SOUND, 8192,1048576, 8192, 255, at_s1846_get_device, NULL },
    /* Has a Winbond W83977TF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[i440BX] Supermicro P6SBA",                     "p6sba",            MACHINE_TYPE_SLOT1,      MACHINE_CHIPSET_INTEL_440BX,         machine_at_p6sba_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK_NONE,                       66666667, 100000000, 1800, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 786432, 8192, 255, NULL, NULL },

    /* 440ZX */
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[i440ZX] MSI MS-6168",                          "ms6168",           MACHINE_TYPE_SLOT1,      MACHINE_CHIPSET_INTEL_440ZX,         machine_at_ms6168_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK_NONE,                       66666667, 100000000, 1800, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL | MACHINE_AV, 8192, 524288, 8192, 255, at_ms6168_get_device, NULL },
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[i440ZX] Packard Bell Bora Pro",                "borapro",          MACHINE_TYPE_SLOT1,      MACHINE_CHIPSET_INTEL_440ZX,         machine_at_borapro_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK_NONE,                       66666667, 66666667, 1800, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL | MACHINE_AV, 8192, 524288, 8192, 255, at_borapro_get_device, NULL },

    /* SMSC VictoryBX-66 */
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[SMSC VictoryBX-66] A-Trend ATC6310BXII",       "atc6310bxii",      MACHINE_TYPE_SLOT1,      MACHINE_CHIPSET_SMSC_VICTORYBX_66,   machine_at_atc6310bxii_init,      0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK_NONE,                       66666667, 133333333, 1300, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 786432, 8192, 255, NULL, NULL },

    /* VIA Apollo Pro */
    /* Has the VIA VT82C596B southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    { "[VIA Apollo Pro] FIC KA-6130",                  "ficka6130",        MACHINE_TYPE_SLOT1,      MACHINE_CHIPSET_VIA_APOLLO_PRO,      machine_at_ficka6130_init,        0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK_NONE,                       66666667, 100000000, 1800, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 524288, 8192, 255, NULL, NULL },
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[VIA Apollo Pro 133] ASUS P3V133",              "p3v133",           MACHINE_TYPE_SLOT1,      MACHINE_CHIPSET_VIA_APOLLO_PRO_133,  machine_at_p3v133_init,           0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK_NONE,                       66666667, 150000000, 1300, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 1572864, 8192, 255, NULL, NULL },
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[VIA Apollo Pro 133A] ASUS P3V4X",              "p3v4x",            MACHINE_TYPE_SLOT1,      MACHINE_CHIPSET_VIA_APOLLO_PRO_133A, machine_at_p3v4x_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK_NONE,                       66666667, 150000000, 1300, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 2097152, 8192, 255, NULL, NULL },

    /* Slot 1/2 machines */
    /* 440GX */
    /* Has a National Semiconductors PC87309 Super I/O chip with on-chip KBC
       with most likely AMIKey-2 KBC firmware. */
    { "[i440GX] Freeway FW-6400GX",                    "fw6400gx",         MACHINE_TYPE_SLOT1_2,    MACHINE_CHIPSET_INTEL_440GX,         machine_at_fw6400gx_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1 | CPU_PKG_SLOT2,     CPU_BLOCK_NONE,                       100000000, 150000000, 1800, 3500, 3.0, 8.0, MACHINE_PS2_NOISA, MACHINE_IDE_DUAL, 16384, 2080768, 16384, 511, NULL, NULL },

    /* Slot 1/Socket 370 machines */
    /* 440BX */
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[i440BX] Tyan Trinity 371",                     "s1857",            MACHINE_TYPE_SLOT1_370,  MACHINE_CHIPSET_INTEL_440BX,         machine_at_s1857_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1 | CPU_PKG_SOCKET370, CPU_BLOCK_NONE,                       66666667, 133333333, 1300, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 786432, 8192, 255, at_s1857_get_device, NULL },

    /* Slot 2 machines */
    /* 440GX */
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[i440GX] Gigabyte GA-6GXU",                     "6gxu",             MACHINE_TYPE_SLOT2,      MACHINE_CHIPSET_INTEL_440GX,         machine_at_6gxu_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT2,                     CPU_BLOCK_NONE,                       100000000, 133333333, 1800, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 16384,2097152,16384, 511, NULL, NULL },
    /* Has a Winbond W83977TF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[i440GX] Supermicro S2DGE",                     "s2dge",            MACHINE_TYPE_SLOT2,      MACHINE_CHIPSET_INTEL_440GX,         machine_at_s2dge_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT2,                     CPU_BLOCK_NONE,                       66666667, 100000000, 1800, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 16384,2097152,16384, 511, NULL, NULL },

    /* PGA370 machines */
    /* 440LX */
    /* Has a Winbond W83977TF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[i440LX] Supermicro 370SLM",                    "s370slm",          MACHINE_TYPE_SOCKET370,  MACHINE_CHIPSET_INTEL_440LX,         machine_at_s370slm_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET370,                 CPU_BLOCK_NONE,                       66666667, 100000000, 1800, 3500, MACHINE_MULTIPLIER_FIXED, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 786432, 8192, 255, NULL, NULL },

    /* 440BX */
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[i440BX] AEWIN AW-O671R",                       "awo671r",          MACHINE_TYPE_SOCKET370,  MACHINE_CHIPSET_INTEL_440BX,         machine_at_awo671r_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET370,                 CPU_BLOCK_NONE,                       66666667, 133333333, 1300, 3500, 1.5, 8.0, /* limits assumed */ MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 524288, 8192, 255, NULL, NULL },
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[i440BX] ASUS CUBX",                            "cubx",             MACHINE_TYPE_SOCKET370,  MACHINE_CHIPSET_INTEL_440BX,         machine_at_cubx_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET370,                 CPU_BLOCK_NONE,                       66666667, 150000000, 1300, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 1048576, 8192, 255, NULL, NULL },
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[i440BX] AmazePC AM-BX133",                     "ambx133",          MACHINE_TYPE_SOCKET370,  MACHINE_CHIPSET_INTEL_440BX,         machine_at_ambx133_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET370,                 CPU_BLOCK_NONE,                       66666667, 133333333, 1300, 3500, 1.5, 8.0, /* limits assumed */ MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 786432, 8192, 255, NULL, NULL },

    /* 440ZX */
    /* Has a Winbond W83977TF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[i440ZX] Soltek SL-63A1",                       "63a1",             MACHINE_TYPE_SOCKET370,  MACHINE_CHIPSET_INTEL_440ZX,         machine_at_63a1_init,             0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET370,                 CPU_BLOCK_NONE,                       66666667, 100000000, 1800, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 524288, 8192, 255, NULL, NULL },

    /* SMSC VictoryBX-66 */
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[SMSC VictoryBX-66] A-Trend ATC7020BXII",       "atc7020bxii",      MACHINE_TYPE_SOCKET370,  MACHINE_CHIPSET_SMSC_VICTORYBX_66,   machine_at_atc7020bxii_init,      0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET370,                 CPU_BLOCK_NONE,                       66666667, 133333333, 1300, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 1048576, 8192, 255, NULL, NULL },

    /* VIA Apollo Pro */
    /* Has the VIA VT82C586B southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    { "[VIA Apollo Pro] PC Partner APAS3",             "apas3",            MACHINE_TYPE_SOCKET370,  MACHINE_CHIPSET_VIA_APOLLO_PRO,      machine_at_apas3_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET370,                 CPU_BLOCK_NONE,                       66666667, 100000000, 1800, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 786432, 8192, 255, NULL, NULL },
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[VIA Apollo Pro 133] ECS P6BAP",                "p6bap",            MACHINE_TYPE_SOCKET370,  MACHINE_CHIPSET_VIA_APOLLO_PRO_133,  machine_at_p6bap_init,            0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET370,                 CPU_BLOCK_NONE,                       66666667, 150000000, 1300, 3500, 1.5, 8.0, MACHINE_PS2_AGP, MACHINE_IDE_DUAL, 8192, 1572864, 8192, 255, NULL, NULL },
    /* Has the VIA VT82C686B southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    { "[VIA Apollo Pro 133A] Acorp 6VIA90AP",          "6via90ap",         MACHINE_TYPE_SOCKET370,  MACHINE_CHIPSET_VIA_APOLLO_PRO_133A, machine_at_6via90ap_init,         0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET370,                 CPU_BLOCK_NONE,                       66666667, 150000000, 1300, 3500, MACHINE_MULTIPLIER_FIXED, MACHINE_PS2_A97, MACHINE_IDE_DUAL | MACHINE_AG, 16384, 3145728, 8192, 255, NULL, NULL },
    /* Has the VIA VT82C686B southbridge with on-chip KBC identical to the VIA
       VT82C42N. */
    { "[VIA Apollo Pro 133A] ASUS CUV4X-LS",           "cuv4xls",          MACHINE_TYPE_SOCKET370,  MACHINE_CHIPSET_VIA_APOLLO_PRO_133A, machine_at_cuv4xls_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET370,                 CPU_BLOCK_NONE,                       66666667, 150000000, 1300, 3500, 1.5, 8.0, MACHINE_PS2_NOI97, MACHINE_IDE_DUAL | MACHINE_SOUND, 16384, 4194304, 8192, 255, at_cuv4xls_get_device, NULL },
    /* Has a Winbond W83977EF Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[VIA Apollo Pro 133A] BCM GT694VA",             "gt694va",          MACHINE_TYPE_SOCKET370,  MACHINE_CHIPSET_VIA_APOLLO_PRO_133A, machine_at_gt694va_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SOCKET370,                 CPU_BLOCK_NONE,                       66666667, 133333333, 1300, 3500, 1.5, 8.0, MACHINE_PS2_AGP,   MACHINE_IDE_DUAL | MACHINE_SOUND, 16384, 3145728, 8192, 255, at_gt694va_get_device, NULL },

    /* Miscellaneous/Fake/Hypervisor machines */
    /* Has a Winbond W83977F Super I/O chip with on-chip KBC with AMIKey-2 KBC
       firmware. */
    { "[i440BX] Microsoft Virtual PC 2007",            "vpc2007",          MACHINE_TYPE_MISC,       MACHINE_CHIPSET_INTEL_440BX,         machine_at_vpc2007_init,          0, 0, MACHINE_AVAILABLE, 0 , CPU_PKG_SLOT1,                     CPU_BLOCK(CPU_PENTIUM2, CPU_CYRIX3S),        0, 66666667, 0, 0, 0, 0, MACHINE_PS2_PCI,   MACHINE_IDE_DUAL, 8192, 1048576, 8192, 255, NULL, NULL },

    { NULL,                                            NULL,               MACHINE_TYPE_NONE,       MACHINE_CHIPSET_NONE,                NULL,                             0, 0, MACHINE_AVAILABLE, 0 , 0,                                 CPU_BLOCK_NONE,                              0, 0, 0, 0, 0, 0, MACHINE_BUS_NONE,  MACHINE_FLAGS_NONE, 0, 0, 0, 0, NULL, NULL }
};

int
machine_count(void)
{
    return((sizeof(machines) / sizeof(machine_t)) - 1);
}

char *
machine_getname(void)
{
    return((char *)machines[machine].name);
}

char *
machine_getname_ex(int m)
{
    return((char *)machines[m].name);
}

const device_t *
machine_getdevice(int m)
{
    if (machines[m].get_device)
        return(machines[m].get_device());

    return(NULL);
}

const device_t *
machine_getviddevice(int m)
{
    if (machines[m].get_vid_device)
        return(machines[m].get_vid_device());

    return(NULL);
}

char *
machine_get_internal_name(void)
{
    return((char *)machines[machine].internal_name);
}

char *
machine_get_internal_name_ex(int m)
{
    return((char *)machines[m].internal_name);
}

int
machine_get_nvrmask(int m)
{
    return(machines[m].nvrmask);
}

int
machine_has_flags(int m, int flags)
{
    return(machines[m].flags & flags);
}

int
machine_has_bus(int m, int bus_flags)
{
    return(machines[m].bus_flags & bus_flags);
}

int
machine_has_cartridge(int m)
{
    return(machine_has_bus(m, MACHINE_CARTRIDGE) ? 1 : 0);
}

int
machine_get_min_ram(int m)
{
    return(machines[m].min_ram);
}

int
machine_get_max_ram(int m)
{
#if (!(defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64))
    return MIN(((int) machines[m].max_ram), 2097152);
#else
    return MIN(((int) machines[m].max_ram), 3145728);
#endif
}

int
machine_get_ram_granularity(int m)
{
    return(machines[m].ram_granularity);
}

int
machine_get_type(int m)
{
    return(machines[m].type);
}

int
machine_get_machine_from_internal_name(char *s)
{
    int c = 0;

    while (machines[c].init != NULL) {
        if (!strcmp(machines[c].internal_name, (const char *)s))
            return(c);
        c++;
    }

    return(0);
}
