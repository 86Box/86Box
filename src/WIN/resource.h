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
 * NOTE:	Strings 2176 and 2193 are same.
 *
 * Version:	@(#)resource.h	1.0.2	2017/06/03
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempem, <decwiz@yahoo.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */
#ifndef WIN_RESOURCE_H
# define WIN_RESOURCE_H


/* {{NO_DEPENDENCIES}}
   Microsoft Developer Studio generated include file.
   Used by 86Box.rc
*/

#define DLG_ABOUT			101
#define DLG_STATUS			102
#define DLG_CONFIG			110
#define DLG_CFG_MACHINE			111
#define DLG_CFG_VIDEO			112
#define DLG_CFG_INPUT			113
#define DLG_CFG_SOUND			114
#define DLG_CFG_NETWORK			115
#define DLG_CFG_PERIPHERALS		116
#define DLG_CFG_HARD_DISKS		117
#define DLG_CFG_HARD_DISKS_ADD		118
#define DLG_CFG_REMOVABLE_DEVICES	119

#define IDC_SETTINGSCATLIST             1004
#define IDC_LIST_HARD_DISKS             1005
#define IDC_COMBO_MACHINE               1006
#define IDC_COMBO_CPU_TYPE              1007
#define IDC_COMBO_CPU                   1008
#define IDC_COMBO_WS                    1009
#define IDC_CHECK_DYNAREC               1010
#define IDC_CHECK_FPU                   1011
#define IDC_COMBO_SCSI                  1012
#define IDC_CONFIGURE_SCSI              1013
#define IDC_COMBO_VIDEO                 1014
#define IDC_COMBO_VIDEO_SPEED           1015
#define IDC_CHECK_VOODOO                1016
#define IDC_CHECKCMS                    1016
#define IDC_CONFIGURE_VOODOO            1017
#define IDC_CHECKNUKEDOPL               1018
#define IDC_COMBO_JOYSTICK              1018
#define IDC_CHECK_SYNC                  1019
#define IDC_CHECK_MPU401                1019
#define IDC_LIST_FLOPPY_DRIVES          1020
#define IDC_CONFIGURE_MPU401            1020
#define IDC_LIST_CDROM_DRIVES           1021
#define IDC_CONFIGURE_MACHINE           1022
#define IDC_COMBO_LANG                  1023
#define IDC_BUTTON_FDD_ADD              1024
#define IDC_BUTTON_FDD_EDIT             1025
#define IDC_BUTTON_FDD_REMOVE           1026
#define IDC_BUTTON_CDROM_ADD            1027
#define IDC_BUTTON_HDD_ADD_NEW          1027
#define IDC_BUTTON_CDROM_EDIT           1028
#define IDC_BUTTON_HDD_ADD              1028
#define IDC_BUTTON_CDROM_REMOVE         1029
#define IDC_BUTTON_HDD_REMOVE           1029
#define IDC_CHECKTURBO			1030
#define IDC_HDIMAGE_NEW                 1035
#define IDC_HD_BUS                      1036
#define IDC_HDIMAGE_EXISTING            1037
#define IDC_COMBO_HD_BUS                1038
#define IDC_EDIT_HD_FILE_NAME           1039
#define IDC_EDIT_HD_CYL                 1040
#define IDC_EDIT_HD_HPC                 1041
#define IDC_EDIT_HD_SPT                 1042
#define IDC_EDIT_HD_SIZE                1043
#define IDC_COMBO_HD_TYPE               1044
#define IDC_COMBO_HD_LOCATION           1045
#define IDC_CHECKGUS                    1046
#define IDC_COMBO_HD_CHANNEL            1047
#define IDC_COMBO_HD_CHANNEL_IDE        1048
#define IDC_COMBO_HD_ID                 1050
#define IDC_COMBO_HD_LUN                1051
#define IDC_CHECKBUGGER                 1052
#define IDC_CHECKSERIAL1                1053
#define IDC_CHECKPARALLEL               1054
#define IDC_CHECKSERIAL2                1055
#define IDC_COMBO_HDC                   1068
#define IDC_COMBO_MOUSE                 1069
#define IDC_COMBO_IDE_TER               1069
#define IDC_COMBO_IDE_QUA               1070
#define IDC_COMBO_FD_TYPE               1071
#define IDC_COMBO_CD_BUS                1072
#define IDC_COMBO_CD_CHANNEL_IDE        1073
#define IDC_COMBO_CD_ID                 1074
#define IDC_COMBO_CD_LUN                1075
#define IDC_COMBO_MIDI                  1076
#define IDC_CHECK_CDROM_1_AUDIO_ENABLED 1584
#define IDC_CHECK_CDROM_2_AUDIO_ENABLED 1585
#define IDC_CHECK_CDROM_3_AUDIO_ENABLED 1586
#define IDC_CHECK_CDROM_4_AUDIO_ENABLED 1587

#define IDS_STRINGS		2048	// "86Box"
#define IDS_2049		2049	// "86Box Error"
#define IDS_2050		2050	// "86Box Fatal Error"
#define IDS_2051		2051	// "This will reset 86Box.."
#define IDS_2052		2052	// "DirectDraw Screenshot Error"
#define IDS_2053		2053	// "Invalid number of sectors.."
#define IDS_2054		2054	// "Invalid number of heads.."
#define IDS_2055		2055	// "Invalid number of cylinders.."
#define IDS_2056		2056	// "Please enter a valid file name"
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
#define IDS_2095		2095	// "MFM, RLL, or ESDI CD-ROM.."
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
#define IDS_2139		2139	// "Microsoft 2-button mouse (serial)"
#define IDS_2140		2140	// "Mouse Systems mouse (serial)"
#define IDS_2141		2141	// "2-button mouse (PS/2)"
#define IDS_2142		2142	// "Microsoft Intellimouse (PS/2)"
#define IDS_2143		2143	// "Bus mouse"
#define IDS_2144		2144	// "Standard 2-button joystick(s)"
#define IDS_2145		2145	// "Standard 4-button joystick"
#define IDS_2146		2146	// "Standard 6-button joystick"
#define IDS_2147		2147	// "Standard 8-button joystick"
#define IDS_2148		2148	// "CH Flightstick Pro"
#define IDS_2149		2149	// "Microsoft SideWinder Pad"
#define IDS_2150		2150	// "Thrustmaster Flight Control System"
#define IDS_2151		2151	// "Disabled"
#define IDS_2152		2152	// "None"
#define IDS_2153		2153	// "AT Fixed Disk Adapter"
#define IDS_2154		2154	// "Internal IDE"
#define IDS_2155		2155	// "IRQ %i"
#define IDS_2156		2156	// "MFM (%01i:%01i)"
#define IDS_2157		2157	// "IDE (PIO+DMA) (%01i:%01i)"
#define IDS_2158		2158	// "SCSI (%02i:%02i)"
#define IDS_2159		2159	// "Invalid number of cylinders.."
#define IDS_2160		2160	// "%" PRIu64
#define IDS_2161		2161	// "Genius Bus mouse"
#define IDS_2162		2162	// "Amstrad mouse"
#define IDS_2163		2163	// "Attempting to create a spuriously.."
#define IDS_2164		2164	// "Invalid number of sectors.."
#define IDS_2165		2165	// "MFM"
#define IDS_2166		2166	// "XT IDE"
#define IDS_2167		2167	// "RLL"
#define IDS_2168		2168	// "IDE (PIO-only)"
#define IDS_2169		2169	// "%01i:%01i"
#define IDS_2170		2170	// "Custom..."
#define IDS_2171		2171	// "%" PRIu64 " MB (CHS: %" ..
#define IDS_2172		2172	// "Hard disk images .."
#define IDS_2173		2173	// "All floppy images .."
#define IDS_2174		2174	// "Configuration files .."
#define IDS_2175		2175	// "CD-ROM image .."
#define IDS_2176		2176	// "Use CTRL+ALT+PAGE DOWN .."
#define IDS_2177		2177	// "Olivetti M24 mouse"
#define IDS_2178		2178	// "This image exists and will.."
#define IDS_2179		2179	// "Floppy %i (%s): %ws"
#define IDS_2180		2180	// "CD-ROM %i: %ws"
#define IDS_2181		2181	// "MFM hard disk"
#define IDS_2182		2182	// "IDE hard disk (PIO-only)"
#define IDS_2183		2183	// "IDE hard disk (PIO and DMA)"
#define IDS_2184		2184	// "SCSI hard disk"
#define IDS_2185		2185	// "(empty)"
#define IDS_2186		2186	// "(host drive %c:)"
#define IDS_2187		2187	// "Custom (large)..."
#define IDS_2188		2188	// "Type"
#define IDS_2189		2189	// "ATAPI (PIO-only)"
#define IDS_2190		2190	// "ATAPI (PIO and DMA)"
#define IDS_2191		2191	// "ATAPI (PIO-only) (%01i:%01i)"
#define IDS_2192		2192	// "ATAPI (PIO and DMA) (%01i:%01i)"
#define IDS_2193		2193	// "Use CTRL+ALT+PAGE DOWN to .."
#define IDS_2194		2194	// "Unable to create bitmap file: %s"
#define IDS_2195		2195	// "IDE (PIO-only) (%01i:%01i)"
#define IDS_2196		2196	// "Add New Hard Disk"
#define IDS_2197		2197	// "Add Existing Hard Disk"
#define IDS_2198		2198	// "SCSI removable disk %i: %s"
#define IDS_2199		2199	// "USB is not yet supported"
#define IDS_2200		2200	// "Invalid PCap device"
#define IDS_2201		2201	// "&Notify disk change"
#define IDS_2202		2202	// "SCSI (removable)"
#define IDS_2203		2203	// "SCSI (removable) (%02i:%02i)"
#define IDS_2204		2204	// "Pcap Library Not Available"
#define IDS_2205		2205	// "RLL (%01i:%01i)"
#define IDS_2206		2206	// "XT IDE (%01i:%01i)"
#define IDS_2207		2207	// "RLL hard disk"
#define IDS_2208		2208	// "XT IDE hard disk"
#define IDS_2209		2209	// "IDE (PIO and DMA)"
#define IDS_2210		2210	// "SCSI"
#define IDS_2211		2211	// "&New image..."
#define IDS_2212		2212	// "Existing image..."
#define IDS_2213		2213	// "Existing image (&Write-.."
#define IDS_2214		2214	// "E&ject"
#define IDS_2215		2215	// "&Mute"
#define IDS_2216		2216	// "E&mpty"
#define IDS_2217		2217	// "&Reload previous image"
#define IDS_2218		2218	// "&Image..."
#define IDS_2219		2219	// "PCap failed to set up .."
#define IDS_2220		2220	// "Image (&Write-protected)..."
#define IDS_2221		2221	// "Turbo"
#define IDS_2222		2222	// "On"
#define IDS_2223		2223	// "Off"
#define IDS_2224		2224	// "<Placeholder string>"
#define IDS_2225		2225	// "English (United States)"

#define IDS_LANG_ENUS		IDS_2225


#define IDM_ABOUT          40001
#define IDC_ABOUT_ICON     65535
#define IDM_FILE_RESET     40015
#define IDM_FILE_HRESET    40016
#define IDM_FILE_EXIT      40017
#define IDM_FILE_RESET_CAD 40018
#define IDM_HDCONF         40019
#define IDM_CONFIG         40020
#define IDM_CONFIG_LOAD    40021
#define IDM_CONFIG_SAVE    40022
#define IDM_USE_NUKEDOPL   40023
#define IDM_STATUS         40030
#define IDM_VID_RESIZE     40050
#define IDM_VID_REMEMBER   40051
#define IDM_VID_DDRAW      40060
#define IDM_VID_D3D        40061
#define IDM_VID_SCALE_1X   40064
#define IDM_VID_SCALE_2X   40065
#define IDM_VID_SCALE_3X   40066
#define IDM_VID_SCALE_4X   40067
#define IDM_VID_FULLSCREEN 40070
#define IDM_VID_FS_FULL    40071
#define IDM_VID_FS_43      40072
#define IDM_VID_FS_SQ      40073
#define IDM_VID_FS_INT     40074
#define IDM_VID_FORCE43    40075
#define IDM_VID_OVERSCAN   40076
#define IDM_VID_FLASH      40077
#define IDM_VID_SCREENSHOT 40078
#define IDM_VID_INVERT     40079
#define IDM_VID_CGACON     40080

#define IDM_IDE_TER_ENABLED	44000
#define IDM_IDE_TER_IRQ9	44009
#define IDM_IDE_TER_IRQ10	44010
#define IDM_IDE_TER_IRQ11	44011
#define IDM_IDE_TER_IRQ12	44012
#define IDM_IDE_TER_IRQ14	44014
#define IDM_IDE_TER_IRQ15	44015
#define IDM_IDE_QUA_ENABLED	44020
#define IDM_IDE_QUA_IRQ9	44029
#define IDM_IDE_QUA_IRQ10	44030
#define IDM_IDE_QUA_IRQ11	44031
#define IDM_IDE_QUA_IRQ12	44032
#define IDM_IDE_QUA_IRQ14	44033
#define IDM_IDE_QUA_IRQ15	44035

#ifdef ENABLE_BUSLOGIC_LOG
# define IDM_LOG_BUSLOGIC	51200
#endif
#ifdef ENABLE_CDROM_LOG
# define IDM_LOG_CDROM		51201
#endif
#ifdef ENABLE_D86F_LOG
# define IDM_LOG_D86F		51202
#endif
#ifdef ENABLE_FDC_LOG
# define IDM_LOG_FDC		51203
#endif
#ifdef ENABLE_IDE_LOG
# define IDM_LOG_IDE		51204
#endif
#ifdef ENABLE_NIC_LOG
# define IDM_LOG_NIC		51205
#endif
#ifdef ENABLE_SERIAL_LOG
# define IDM_LOG_SERIAL	51208
#endif
#ifdef ENABLE_LOG_BREAKPOINT
# define IDM_LOG_BREAKPOINT	51206
#endif
#ifdef ENABLE_VRAM_DUMP
# define IDM_DUMP_VRAM		51207
#endif

#define IDC_COMBO1		1000
#define IDC_COMBOVID		1001
#define IDC_COMBO3		1002
#define IDC_COMBO4		1003
#define IDC_COMBO5		1004
#define IDC_COMBO386		1005
#define IDC_COMBO486		1006
#define IDC_COMBOSND		1007
#define IDC_COMBONETTYPE	1008
#define IDC_COMBOPCAP		1009
#define IDC_COMBONET		1010	/*FIXME*/
#define IDC_CHECK1		1010	/*FIXME*/
#define IDC_CHECK2		1011
#define IDC_CHECK3		1012
#define IDC_CHECKSSI		1014
#define IDC_CHECKVOODOO		1015
#define IDC_CHECKDYNAREC	1016
#define IDC_CHECKBUSLOGIC	1017
#define IDC_CHECKSYNC		1024
#define IDC_CHECKXTIDE		1025
#define IDC_CHECKFPU		1026
#define IDC_EDIT1		1030
#define IDC_EDIT2		1031
#define IDC_EDIT3		1032
#define IDC_EDIT4		1033
#define IDC_EDIT5		1034
#define IDC_EDIT6		1035
#define IDC_COMBOHDT		1036
#define IDC_COMBOCPUM		1060
#define IDC_COMBOSPD		1061
#define IDC_COMBODR1		1062
#define IDC_COMBODR2		1063
#define IDC_COMBODR3		1064
#define IDC_COMBODR4		1065
#define IDC_COMBOJOY		1066
#define IDC_COMBOWS		1067
#define IDC_COMBOMOUSE		1068
#define IDC_COMBOHDD		1069

#define IDC_CFILE		1060

#define IDC_HDTYPE		1280

#define IDC_RENDER		1281
#define IDC_STATUS		1282

#define IDC_MEMSPIN		1100
#define IDC_MEMTEXT		1101
#define IDC_STEXT1		1102
#define IDC_STEXT2		1103
#define IDC_STEXT3		1104
#define IDC_STEXT4		1105
#define IDC_STEXT5		1106
#define IDC_STEXT6		1107
#define IDC_STEXT7		1108
#define IDC_STEXT8		1109
#define IDC_STEXT_DEVICE	1110
#define IDC_TEXT_MB		1111
#define IDC_TEXT1		1115
#define IDC_TEXT2		1116

#define IDC_CONFIGUREVID	1200
#define IDC_CONFIGURESND	1201
#define IDC_CONFIGUREVOODOO	1202
#define IDC_CONFIGUREMOD	1203
#define IDC_CONFIGURENETTYPE	1204
#define IDC_CONFIGUREBUSLOGIC	1205
#define IDC_CONFIGUREPCAP	1206
#define IDC_CONFIGURENET	1207
#define IDC_JOY1		1210
#define IDC_JOY2		1211
#define IDC_JOY3		1212
#define IDC_JOY4		1213

#define IDC_CONFIG_BASE		1200

/* The biggest amount of low bits needed for CD-ROMS (2 bits for ID and 5 bits for host drive, so 7 bits),
   and removable disks (5 bits for ID), so we choose an 256-entry spacing for convenience. */

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

#define IDC_STATIC 1792

/* Next default values for new objects */
#ifdef APSTUDIO_INVOKED
# ifndef APSTUDIO_READONLY_SYMBOLS
#  define _APS_NO_MFC			1
#  define _APS_NEXT_RESOURCE_VALUE	111
#  define _APS_NEXT_COMMAND_VALUE	40002
#  define _APS_NEXT_CONTROL_VALUE	1055
#  define _APS_NEXT_SYMED_VALUE		101
#  endif
#endif

#define STRINGS_NUM 178


#endif	/*WIN_RESOURCE_H*/
