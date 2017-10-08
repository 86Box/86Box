/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Windows resource defines.
 *
 * NOTE:	FIXME: Strings 2176 and 2193 are same.
 *
 * Version:	@(#)resource.h	1.0.11	2017/10/07
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempem, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#ifndef WIN_RESOURCE_H
# define WIN_RESOURCE_H


/* Dialog IDs. */
#define DLG_ABOUT		101	/* top-level dialog */
#define DLG_STATUS		102	/* top-level dialog */
#define DLG_CONFIG		110	/* top-level dialog */
#define  DLG_CFG_MACHINE	111	/* sub-dialog of config */
#define  DLG_CFG_VIDEO		112	/* sub-dialog of config */
#define  DLG_CFG_INPUT		113	/* sub-dialog of config */
#define  DLG_CFG_SOUND		114	/* sub-dialog of config */
#define  DLG_CFG_NETWORK	115	/* sub-dialog of config */
#define  DLG_CFG_PORTS		116	/* sub-dialog of config */
#define  DLG_CFG_PERIPHERALS	117	/* sub-dialog of config */
#define  DLG_CFG_HARD_DISKS	118	/* sub-dialog of config */
#define  DLG_CFG_HARD_DISKS_ADD	119	/* sub-dialog of config */
#define  DLG_CFG_REMOVABLE_DEVICES	120	/* sub-dialog of config */

/* Static text label IDs. */
#define IDT_1700		1700	/* Language: */
#define IDT_1701		1701	/* Machine: */
#define IDT_1702		1702	/* CPU type: */
#define IDT_1703		1703	/* Wait states: */
#define IDT_1704		1704	/* CPU: */
#define IDT_1705		1705	/* MB	== IDC_TEXT_MB */
#define IDT_1706		1706	/* Memory: */
#define IDT_1707		1707	/* Video: */
#define IDT_1708		1708	/* Video speed: */
#define IDT_1709		1709	/* Mouse: */
#define IDT_1710		1710	/* Joystick: */
#define IDT_1711		1711	/* Sound card: */
#define IDT_1712		1712	/* MIDI Out Device: */
#define IDT_1713		1713	/* Network type: */
#define IDT_1714		1714	/* PCap device: */
#define IDT_1715		1715	/* Network adapter: */
#define IDT_1716		1716	/* SCSI Controller: */
#define IDT_1717		1717	/* HD Controller: */
#define IDT_1718		1718	/* Tertiary IDE: */
#define IDT_1719		1719	/* Quaternary IDE: */
#define IDT_1720		1720	/* Hard disks: */
#define IDT_1721		1721	/* Bus: */
#define IDT_1722		1722	/* Channel: */
#define IDT_1723		1723	/* ID: */
#define IDT_1724		1724	/* LUN: */
#define IDT_1726		1726	/* Sectors: */
#define IDT_1727		1727	/* Heads: */
#define IDT_1728		1728	/* Cylinders: */
#define IDT_1729		1729	/* Size (MB): */
#define IDT_1730		1730	/* Type: */
#define IDT_1731		1731	/* File name: */
#define IDT_1737		1737	/* Floppy drives: */
#define IDT_1738		1738	/* Type: */
#define IDT_1739		1739	/* CD-ROM drives: */
#define IDT_1740		1740	/* Bus: */
#define IDT_1741		1741	/* ID: */
#define IDT_1742		1742	/* LUN: */
#define IDT_1743		1743	/* Channel: */
#define IDT_STEXT		1744	/* text in status window */
#define IDT_SDEVICE		1745	/* text in status window */


/*
 * To try to keep these organized, we now group the
 * constants per dialog, as this allows easy adding
 * and deleting items.
 */
#define IDC_SETTINGSCATLIST	1001	/* generic config */
#define IDC_CFILE		1002	/* Select File dialog */
#define IDC_CHECK_SYNC		1008
/* Leave this as is until we finally get into localization in 86Box 3.00(?). */
#if 0
#define IDC_COMBO_LANG		1009
#endif

#define IDC_COMBO_MACHINE	1010	/* machine/cpu config */
#define IDC_CONFIGURE_MACHINE	1011
#define IDC_COMBO_CPU_TYPE	1012
#define IDC_COMBO_CPU		1013
#define IDC_CHECK_FPU		1014
#define IDC_COMBO_WS		1015
#define IDC_CHECK_DYNAREC	1016
#define IDC_MEMTEXT		1017
#define IDC_MEMSPIN		1018
#define IDC_TEXT_MB		IDT_1705

#define IDC_VIDEO		1030	/* video config */
#define IDC_COMBO_VIDEO		1031
#define IDC_COMBO_VIDEO_SPEED	1032
#define IDC_CHECK_VOODOO	1033
#define IDC_BUTTON_VOODOO	1034

#define IDC_INPUT		1050	/* input config */
#define IDC_COMBO_MOUSE		1051
#define IDC_COMBO_JOYSTICK	1052
#define IDC_COMBO_JOY		1053

#define IDC_SOUND		1070	/* sound config */
#define IDC_COMBO_SOUND		1071
#define IDC_CHECK_SSI		1072
#define IDC_CHECK_CMS		1073
#define IDC_CHECK_GUS		1074
#define IDC_CHECK_NUKEDOPL	1075
#define IDC_COMBO_MIDI		1076
#define IDC_CHECK_MPU401	1077
#define IDC_CONFIGURE_MPU401	1078
#define IDC_CHECK_FLOAT		1079

#define IDC_COMBO_NET_TYPE	1090	/* network config */
#define IDC_COMBO_PCAP		1091
#define IDC_COMBO_NET		1092

#define IDC_COMBO_LPT1		1110	/* ports config */
#define IDC_CHECK_SERIAL1	1111
#define IDC_CHECK_SERIAL2	1112
#define IDC_CHECK_PARALLEL	1113

#define IDC_OTHER_PERIPH	1120	/* other periph config */
#define IDC_COMBO_SCSI		1121
#define IDC_CONFIGURE_SCSI	1122
#define IDC_COMBO_HDC		1123
#define IDC_COMBO_IDE_TER	1124
#define IDC_COMBO_IDE_QUA	1125
#define IDC_CHECK_BUGGER	1126

#define IDC_HARD_DISKS		1130	/* hard disk config */
#define IDC_LIST_HARD_DISKS	1131
#define IDC_BUTTON_HDD_ADD_NEW	1132
#define IDC_BUTTON_HDD_ADD	1133
#define IDC_BUTTON_HDD_REMOVE	1134
#define IDC_COMBO_HD_BUS	1135
#define IDC_COMBO_HD_CHANNEL	1136
#define IDC_COMBO_HD_ID		1137
#define IDC_COMBO_HD_LUN	1138
#define IDC_COMBO_HD_CHANNEL_IDE 1139

#define IDC_EDIT_HD_FILE_NAME	1140	/* add hard disk dialog */
#define IDC_EDIT_HD_SPT		1141
#define IDC_EDIT_HD_HPC		1142
#define IDC_EDIT_HD_CYL		1143
#define IDC_EDIT_HD_SIZE	1144
#define IDC_COMBO_HD_TYPE	1145

#define IDC_REMOV_DEVICES	1150	/* removable dev config */
#define IDC_LIST_FLOPPY_DRIVES	1151
#define IDC_COMBO_FD_TYPE	1152
#define IDC_CHECKTURBO		1153
#define IDC_CHECKBPB		1154
#define IDC_BUTTON_FDD_ADD	1155	// status bar menu
#define IDC_BUTTON_FDD_EDIT	1156	// status bar menu
#define IDC_BUTTON_FDD_REMOVE	1157	// status bar menu
#define IDC_LIST_CDROM_DRIVES	1158
#define IDC_COMBO_CD_BUS	1159
#define IDC_COMBO_CD_ID		1160
#define IDC_COMBO_CD_LUN	1161
#define IDC_COMBO_CD_CHANNEL_IDE 1162
#define IDC_BUTTON_CDROM_ADD	1163	// status bar menu
#define IDC_BUTTON_CDROM_EDIT	1164	// status bar menu
#define IDC_BUTTON_CDROM_REMOVE	1165	// status bar menu


/* For the DeviceConfig code, re-do later. */
#define IDC_CONFIG_BASE		1200
#define  IDC_CONFIGURE_VID	1200
#define  IDC_CONFIGURE_SND	1201
#define  IDC_CONFIGURE_VOODOO	1202
#define  IDC_CONFIGURE_MOD	1203
#define  IDC_CONFIGURE_NET_TYPE	1204
#define  IDC_CONFIGURE_BUSLOGIC	1205
#define  IDC_CONFIGURE_PCAP	1206
#define  IDC_CONFIGURE_NET	1207
#define  IDC_CONFIGURE_MIDI	1208
#define  IDC_JOY1		1210
#define  IDC_JOY2		1211
#define  IDC_JOY3		1212
#define  IDC_JOY4		1213
#define IDC_HDTYPE		1280
#define IDC_RENDER		1281
#define IDC_STATUS		1282


/* String IDs. */
#define IDS_STRINGS		2048	// "86Box"
#define IDS_2049		2049	// "86Box Error"
#define IDS_2050		2050	// "86Box Fatal Error"
#define IDS_2051		2051	// "This will reset 86Box.."
#define IDS_2052		2052	// "DirectDraw Screenshot Error"
#define IDS_2053		2053	// "Invalid number of sectors.."
#define IDS_2054		2054	// "Invalid number of heads.."
#define IDS_2055		2055	// "Invalid number of cylinders.."
#define IDS_2056		2056	// "No usable ROM images found!"
#define IDS_2057		2057	// "Unable to open the file for write"
#define IDS_2058		2058	// "Attempting to create a HDI.."
#define IDS_2059		2059	// "Remember to partition and.."
#define IDS_2060		2060	// "Unable to open the file.."
#define IDS_2061		2061	// "HDI or HDX image with a.."
#define IDS_2062		2062	// "86Box was unable to find any.."
#define IDS_2063		2063	// "Configured ROM set not avai.."
#define IDS_2064		2064	// "Configured video BIOS not.."
#define IDS_2065		2065	// "Machine"
#define IDS_2066		2066	// "Video"
#define IDS_2067		2067	// "Input devices"
#define IDS_2068		2068	// "Sound"
#define IDS_2069		2069	// "Network"
#define IDS_2070		2070	// "Other peripherals"
#define IDS_2071		2071	// "Hard disks"
#define IDS_2072		2072	// "Removable devices"
#define IDS_2073		2073	// "%i"" floppy drive: %s"
#define IDS_2074		2074	// "Disabled CD-ROM drive"
#define IDS_2075		2075	// "%s CD-ROM drive: %s"
#define IDS_2076		2076	// "Host CD/DVD Drive (%c:)"
#define IDS_2077		2077	// "Click to capture mouse"
#define IDS_2078		2078	// "Press F12-F8 to release mouse"
#define IDS_2079		2079	// "Press F12-F8 or middle button.."
#define IDS_2080		2080	// "Drive"
#define IDS_2081		2081	// "Location"
#define IDS_2082		2082	// "Bus"
#define IDS_2083		2083	// "File"
#define IDS_2084		2084	// "C"
#define IDS_2085		2085	// "H"
#define IDS_2086		2086	// "S"
#define IDS_2087		2087	// "MB"
#define IDS_2088		2088	// "%i"
#define IDS_2089		2089	// "Enabled"
#define IDS_2090		2090	// "Mute"
#define IDS_2091		2091	// "Type"
#define IDS_2092		2092	// "Bus"
#define IDS_2093		2093	// "DMA"
#define IDS_2094		2094	// "KB"
#define IDS_2095		2095	// "No renderer available!"
#define IDS_2096		2096	// "Slave"
#define IDS_2097		2097	// "SCSI (ID %s, LUN %s)"
#define IDS_2098		2098	// "Adapter Type"
#define IDS_2099		2099	// "Base Address"
#define IDS_2100		2100	// "IRQ"
#define IDS_2101		2101	// "8-bit DMA"
#define IDS_2102		2102	// "16-bit DMA"
#define IDS_2103		2103	// "BIOS"
#define IDS_2104		2104	// "Network Type"
#define IDS_2105		2105	// "Surround Module"
#define IDS_2106		2106	// "MPU-401 Base Address"
#define IDS_2107		2107	// "No PCap devices found"
#define IDS_2108		2108	// "On-board RAM"
#define IDS_2109		2109	// "Memory Size"
#define IDS_2110		2110	// "Display Type"
#define IDS_2111		2111	// "RGB"
#define IDS_2112		2112	// "Composite"
#define IDS_2113		2113	// "Composite Type"
#define IDS_2114		2114	// "Old"
#define IDS_2115		2115	// "New"
#define IDS_2116		2116	// "RGB Type"
#define IDS_2117		2117	// "Color"
#define IDS_2118		2118	// "Monochrome (Green)"
#define IDS_2119		2119	// "Monochrome (Amber)"
#define IDS_2120		2120	// "Monochrome (Gray)"
#define IDS_2121		2121	// "Color (no brown)"
#define IDS_2122		2122	// "Monochrome (Default)"
#define IDS_2123		2123	// "Snow Emulation"
#define IDS_2124		2124	// "Bilinear Filtering"
#define IDS_2125		2125	// "Dithering"
#define IDS_2126		2126	// "Framebuffer Memory Size"
#define IDS_2127		2127	// "Texture Memory Size"
#define IDS_2128		2128	// "Screen Filter"
#define IDS_2129		2129	// "Render Threads"
#define IDS_2130		2130	// "Recompiler"
#define IDS_2131		2131	// "System Default"
#define IDS_2132		2132	// "%i Wait state(s)"
#define IDS_2133		2133	// "8-bit"
#define IDS_2134		2134	// "Slow 16-bit"
#define IDS_2135		2135	// "Fast 16-bit"
#define IDS_2136		2136	// "Slow VLB/PCI"
#define IDS_2137		2137	// "Mid  VLB/PCI"
#define IDS_2138		2138	// "Fast VLB/PCI"
#define IDS_2139                2139
#define IDS_2140                2140
#define IDS_2141                2141
#define IDS_2142                2142
#define IDS_2143                2143
#define IDS_2144		2144	// "Standard 2-button joystick(s)"
#define IDS_2145		2145	// "Standard 4-button joystick"
#define IDS_2146		2146	// "Standard 6-button joystick"
#define IDS_2147		2147	// "Standard 8-button joystick"
#define IDS_2148		2148	// "CH Flightstick Pro"
#define IDS_2149		2149	// "Microsoft SideWinder Pad"
#define IDS_2150		2150	// "Thrustmaster Flight Control System"
#define IDS_2151		2151	// "Disabled"
#define IDS_2152		2152	// "None"
#define IDS_2153		2153	// "Unable to load Accelerators"
#define IDS_2154		2154	// "Unable to register Raw Input"
#define IDS_2155		2155	// "IRQ %i"
#define IDS_2156		2156	// "MFM/RLL (%91i:%01i)"
#define IDS_2157		2157	// "IDE (PIO+DMA) (%01i:%01i)"
#define IDS_2158		2158	// "SCSI (%02i:%02i)"
#define IDS_2159		2159	// "Invalid number of cylinders.."
#define IDS_2160		2160	// "%" PRIu64
#define IDS_2161                2161
#define IDS_2162                2162
#define IDS_2163		2163	// "Attempting to create a spuriously.."
#define IDS_2164		2164	// "Invalid number of sectors.."
#define IDS_2165		2165	// "MFM"
#define IDS_2166		2166	// "XT IDE"
#define IDS_2167		2167	// "ESDI"
#define IDS_2168		2168	// "IDE (PIO-only)"
#define IDS_2169		2169	// "%01i:%01i"
#define IDS_2170		2170	// "%01i:%01i"
#define IDS_2171		2171

#define IDS_3072		3072
#define IDS_3073		3073
#define IDS_3074		3074
#define IDS_3075		3075
#define IDS_3076		3076
#define IDS_3077		3077
#define IDS_3078		3078
#define IDS_3079		3079
#define IDS_3080		3080
#define IDS_3081		3081
#define IDS_3082		3082

#define IDS_4096		4096
#define IDS_4097		4097
#define IDS_4098		4098
#define IDS_4099		4099
#define IDS_4100		4100
#define IDS_4101		4101
#define IDS_4102		4102
#define IDS_4103		4103
#define IDS_4104		4104
#define IDS_4105		4105
#define IDS_4106		4106
#define IDS_4107		4107
#define IDS_4108		4108
#define IDS_4109		4109
#define IDS_4110		4110
#define IDS_4111		4111
#define IDS_4112		4112
#define IDS_4113		4113
#define IDS_4114		4114
#define IDS_4115		4115

#define IDS_4352		4352
#define IDS_4353		4353
#define IDS_4354		4354
#define IDS_4355		4355
#define IDS_4356		4356
#define IDS_4357		4357
#define IDS_4358		4358

#define IDS_4608		4608
#define IDS_4609		4609
#define IDS_4610		4610
#define IDS_4611		4611
#define IDS_4612		4612
#define IDS_4613		4613
#define IDS_4614		4614

#define IDS_5120		5120

#define IDS_5376		5376
#define IDS_5377		5377
#define IDS_5378		5378
#define IDS_5379		5379
#define IDS_5380		5380
#define IDS_5381		5381
#define IDS_5382		5382

#define IDS_5632		5632
#define IDS_5633		5633
#define IDS_5634		5634
#define IDS_5635		5635
#define IDS_5636		5636
#define IDS_5637		5637
#define IDS_5638		5638

#define IDS_6144		6144

#define IDS_LANG_ENUS		IDS_6144

#define STRINGS_NUM_2048	124
#define STRINGS_NUM_3072	11
#define STRINGS_NUM_4096	20
#define STRINGS_NUM_4352	7
#define STRINGS_NUM_4608	7
#define STRINGS_NUM_5120	1
#define STRINGS_NUM_5376	7
#define STRINGS_NUM_5632	7
#define STRINGS_NUM_6144	1


#define IDM_ABOUT		40001
#define  IDC_ABOUT_ICON		65535
#define IDM_ACTION_SCREENSHOT	40011
#define IDM_ACTION_HRESET	40012
#define IDM_ACTION_RESET_CAD	40013
#define IDM_ACTION_EXIT		40014
#define IDM_ACTION_CTRL_ALT_ESC 40015
#define IDM_ACTION_PAUSE	40016
#define IDM_CONFIG		40020
#define IDM_CONFIG_LOAD		40021
#define IDM_CONFIG_SAVE		40022
#define IDM_STATUS		40030
#define IDM_VID_RESIZE		40050
#define IDM_VID_REMEMBER	40051
#define IDM_VID_DDRAW		40060
#define IDM_VID_D3D		40061
#define IDM_VID_SCALE_1X	40064
#define IDM_VID_SCALE_2X	40065
#define IDM_VID_SCALE_3X	40066
#define IDM_VID_SCALE_4X	40067
#define IDM_VID_FULLSCREEN	40070
#define IDM_VID_FS_FULL		40071
#define IDM_VID_FS_43		40072
#define IDM_VID_FS_SQ		40073
#define IDM_VID_FS_INT		40074
#define IDM_VID_FORCE43		40075
#define IDM_VID_OVERSCAN	40076
#define IDM_VID_INVERT		40079
#define IDM_VID_CGACON		40080
#define IDM_VID_GRAYCT_601	40085
#define IDM_VID_GRAYCT_709	40086
#define IDM_VID_GRAYCT_AVE	40087
#define IDM_VID_GRAY_RGB	40090
#define IDM_VID_GRAY_MONO	40091
#define IDM_VID_GRAY_AMBER	40092
#define IDM_VID_GRAY_GREEN	40093
#define IDM_VID_GRAY_WHITE	40094

#define IDM_LOG_BREAKPOINT	51201
#define IDM_DUMP_VRAM		51202	// should be an Action

#define IDM_LOG_SERIAL		51211
#define IDM_LOG_D86F		51212
#define IDM_LOG_FDC		51213
#define IDM_LOG_IDE		51214
#define IDM_LOG_CDROM		51215
#define IDM_LOG_NIC		51216
#define IDM_LOG_BUSLOGIC	51217

/*
 * We need 7 bits for CDROM (2 bits ID and 5 bits for host drive),
 * and 5 bits for Removable Disks (5 bits for ID), so we use an
 * 8bit (256 entries) space for these devices.
 */
#define IDM_FLOPPY_IMAGE_NEW		0x1200
#define IDM_FLOPPY_IMAGE_EXISTING	0x1300
#define IDM_FLOPPY_IMAGE_EXISTING_WP	0x1400
#define IDM_FLOPPY_DUMP_86F		0x1500
#define IDM_FLOPPY_EJECT		0x1600

#define IDM_CDROM_MUTE			0x2200
#define IDM_CDROM_EMPTY			0x2300
#define IDM_CDROM_RELOAD		0x2400
#define IDM_CDROM_IMAGE			0x2500
#define IDM_CDROM_HOST_DRIVE		0x2600

#define IDM_RDISK_EJECT			0x3200
#define IDM_RDISK_RELOAD		0x3300
#define IDM_RDISK_SEND_CHANGE		0x3400
#define IDM_RDISK_IMAGE			0x3500
#define IDM_RDISK_IMAGE_WP		0x3600


/* Next default values for new objects */
#ifdef APSTUDIO_INVOKED
# ifndef APSTUDIO_READONLY_SYMBOLS
#  define _APS_NO_MFC			1
#  define _APS_NEXT_RESOURCE_VALUE	1400
#  define _APS_NEXT_COMMAND_VALUE	55000
#  define _APS_NEXT_CONTROL_VALUE	1800
#  define _APS_NEXT_SYMED_VALUE		200
#  endif
#endif


#endif	/*WIN_RESOURCE_H*/
