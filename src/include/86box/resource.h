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
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2018,2019 David Hrdlička.
 */
#ifndef WIN_RESOURCE_H
# define WIN_RESOURCE_H


/* Dialog IDs. */
#define DLG_ABOUT		101	/* top-level dialog */
#define DLG_STATUS		102	/* top-level dialog */
#define DLG_SND_GAIN		103	/* top-level dialog */
#define DLG_NEW_FLOPPY		104	/* top-level dialog */
#define DLG_SPECIFY_DIM		105	/* top-level dialog */
#define DLG_CONFIG		110	/* top-level dialog */
#define  DLG_CFG_MACHINE	111	/* sub-dialog of config */
#define  DLG_CFG_VIDEO		112	/* sub-dialog of config */
#define  DLG_CFG_INPUT		113	/* sub-dialog of config */
#define  DLG_CFG_SOUND		114	/* sub-dialog of config */
#define  DLG_CFG_NETWORK	115	/* sub-dialog of config */
#define  DLG_CFG_PORTS		116	/* sub-dialog of config */
#define  DLG_CFG_STORAGE	117	/* sub-dialog of config */
#define  DLG_CFG_HARD_DISKS	118	/* sub-dialog of config */
#define  DLG_CFG_HARD_DISKS_ADD	119	/* sub-dialog of config */
#define  DLG_CFG_FLOPPY_AND_CDROM_DRIVES	120	/* sub-dialog of config */
#define  DLG_CFG_OTHER_REMOVABLE_DEVICES	121	/* sub-dialog of config */
#define  DLG_CFG_PERIPHERALS	122	/* sub-dialog of config */

/* Static text label IDs. */
#define IDT_1700		1700	/* Language: */
#define IDT_1701		1701	/* Machine: */
#define IDT_1702		1702	/* CPU type: */
#define IDT_1703		1703	/* Wait states: */
#define IDT_1704		1704	/* CPU: */
#define IDT_1705		1705	/* MB	== IDC_TEXT_MB */
#define IDT_1706		1706	/* Memory: */
#define IDT_1707		1707	/* Video: */
#define IDT_1708		1708	/* Machine type: */
#define IDT_1709		1709	/* Mouse: */
#define IDT_1710		1710	/* Joystick: */
#define IDT_1711		1711	/* Sound card: */
#define IDT_1712		1712	/* MIDI Out Device: */
#define IDT_1713		1713	/* MIDI In Device: */
#define IDT_1714		1714	/* Network type: */
#define IDT_1715		1715	/* PCap device: */
#define IDT_1716		1716	/* Network adapter: */
#define IDT_1717		1717	/* SCSI Controller: */
#define IDT_1718		1718	/* HD Controller: */
#define IDT_1719		1719
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
#define IDT_1746		1746	/* Gain */
#define IDT_1749		1749	/* File name: */
#define IDT_1750		1750	/* Disk size: */
#define IDT_1751		1751	/* RPM mode: */
#define IDT_1752		1752	/* Progress: */
#define IDT_1753		1753	/* Bus: */
#define IDT_1754		1754	/* ID: */
#define IDT_1755		1755	/* LUN: */
#define IDT_1756		1756	/* Channel: */
#define IDT_1757		1757	/* Progress: */
#define IDT_1758		1758	/* Speed: */
#define IDT_1759		1759	/* ZIP drives: */
#define IDT_1763		1763	/* Board #1: */
#define IDT_1764		1764	/* Board #2: */
#define IDT_1765		1765	/* Board #3: */
#define IDT_1766		1766	/* Board #4: */
#define IDT_1767		1767	/* ISA RTC: */
#define IDT_1768		1768	/* Ext FD Controller: */
#define IDT_1769		1769	/* MO drives: */
#define IDT_1770		1770	/* Bus: */
#define IDT_1771		1771	/* ID: */
#define IDT_1772		1772	/* Channel */
#define IDT_1773		1773	/* Type: */
#define IDT_1774		1774	/* Image Format: */
#define IDT_1775		1775	/* Block Size: */


/*
 * To try to keep these organized, we now group the
 * constants per dialog, as this allows easy adding
 * and deleting items.
 */
#define IDC_SETTINGSCATLIST	1001	/* generic config */
#define IDC_CFILE		1002	/* Select File dialog */
#define IDC_TIME_SYNC		1005
#define IDC_RADIO_TS_DISABLED	1006
#define IDC_RADIO_TS_LOCAL	1007
#define IDC_RADIO_TS_UTC	1008
/* Leave this as is until we finally get into localization in 86Box 3.00(?). */
#if 0
#define IDC_COMBO_LANG		1009
#endif

#define IDC_COMBO_MACHINE_TYPE	1010
#define IDC_COMBO_MACHINE	1011	/* machine/cpu config */
#define IDC_CONFIGURE_MACHINE	1012
#define IDC_COMBO_CPU_TYPE	1013
#define IDC_COMBO_CPU		1014
#define IDC_COMBO_FPU		1015
#define IDC_COMBO_WS		1016
#ifdef USE_DYNAREC
#define IDC_CHECK_DYNAREC	1017
#endif
#define IDC_MEMTEXT		1018
#define IDC_MEMSPIN		1019
#define IDC_TEXT_MB		IDT_1705

#define IDC_VIDEO		1020	/* video config */
#define IDC_COMBO_VIDEO		1021
#define IDC_CHECK_VOODOO	1022
#define IDC_BUTTON_VOODOO	1023

#define IDC_INPUT		1030	/* input config */
#define IDC_COMBO_MOUSE		1031
#define IDC_COMBO_JOYSTICK	1032
#define IDC_COMBO_JOY		1033
#define IDC_CONFIGURE_MOUSE	1034

#define IDC_SOUND		1040	/* sound config */
#define IDC_COMBO_SOUND		1041
#define IDC_CHECK_SSI		1042
#define IDC_CHECK_CMS		1043
#define IDC_CHECK_GUS		1044
#define IDC_COMBO_MIDI		1045
#define IDC_CHECK_MPU401	1046
#define IDC_CONFIGURE_MPU401	1047
#define IDC_CHECK_FLOAT		1048
#define IDC_CONFIGURE_GUS	1049
#define IDC_COMBO_MIDI_IN	1050
#define IDC_CONFIGURE_CMS	1051

#define IDC_COMBO_NET_TYPE	1060	/* network config */
#define IDC_COMBO_PCAP		1061
#define IDC_COMBO_NET		1062

#define IDC_COMBO_LPT1		1070	/* ports config */
#define IDC_COMBO_LPT2		1071
#define IDC_COMBO_LPT3		1072
#define IDC_CHECK_SERIAL1	1073
#define IDC_CHECK_SERIAL2	1074
#define IDC_CHECK_SERIAL3	1075
#define IDC_CHECK_SERIAL4	1076
#define IDC_CHECK_PARALLEL1	1077
#define IDC_CHECK_PARALLEL2	1078
#define IDC_CHECK_PARALLEL3	1079

#define IDC_OTHER_PERIPH	1080	/* storage controllers config */
#define IDC_COMBO_HDC		1081
#define IDC_CONFIGURE_HDC	1082
#define IDC_CHECK_IDE_TER	1083
#define IDC_BUTTON_IDE_TER	1084
#define IDC_CHECK_IDE_QUA	1085
#define IDC_BUTTON_IDE_QUA	1086
#define IDC_GROUP_SCSI		1087
#define IDC_COMBO_SCSI_1	1088
#define IDC_COMBO_SCSI_2	1089
#define IDC_COMBO_SCSI_3	1090
#define IDC_COMBO_SCSI_4	1091
#define IDC_CONFIGURE_SCSI_1	1092
#define IDC_CONFIGURE_SCSI_2	1093
#define IDC_CONFIGURE_SCSI_3	1094
#define IDC_CONFIGURE_SCSI_4	1095
#define IDC_CHECK_CASSETTE	1096

#define IDC_HARD_DISKS		1100	/* hard disks config */
#define IDC_LIST_HARD_DISKS	1101
#define IDC_BUTTON_HDD_ADD_NEW	1102
#define IDC_BUTTON_HDD_ADD	1103
#define IDC_BUTTON_HDD_REMOVE	1104
#define IDC_COMBO_HD_BUS	1105
#define IDC_COMBO_HD_CHANNEL	1106
#define IDC_COMBO_HD_ID		1107
#define IDC_COMBO_HD_LUN	1108
#define IDC_COMBO_HD_CHANNEL_IDE 1109

#define IDC_EDIT_HD_FILE_NAME	1110	/* add hard disk dialog */
#define IDC_EDIT_HD_SPT		1111
#define IDC_EDIT_HD_HPC		1112
#define IDC_EDIT_HD_CYL		1113
#define IDC_EDIT_HD_SIZE	1114
#define IDC_COMBO_HD_TYPE	1115
#define IDC_PBAR_IMG_CREATE	1116
#define IDC_COMBO_HD_IMG_FORMAT	1117
#define IDC_COMBO_HD_BLOCK_SIZE	1118

#define IDC_REMOV_DEVICES	1120	/* floppy and cd-rom drives config */
#define IDC_LIST_FLOPPY_DRIVES	1121
#define IDC_COMBO_FD_TYPE	1122
#define IDC_CHECKTURBO		1123
#define IDC_CHECKBPB		1124
#define IDC_LIST_CDROM_DRIVES	1125
#define IDC_COMBO_CD_BUS	1126
#define IDC_COMBO_CD_ID		1127
#define IDC_COMBO_CD_LUN	1128
#define IDC_COMBO_CD_CHANNEL_IDE 1129

#define IDC_LIST_ZIP_DRIVES	1130	/* other removable devices config */
#define IDC_COMBO_ZIP_BUS	1131
#define IDC_COMBO_ZIP_ID	1132
#define IDC_COMBO_ZIP_LUN	1133
#define IDC_COMBO_ZIP_CHANNEL_IDE 1134
#define IDC_CHECK250		1135
#define IDC_COMBO_CD_SPEED	1136
#define IDC_LIST_MO_DRIVES	1137
#define IDC_COMBO_MO_BUS	1138
#define IDC_COMBO_MO_ID		1139
#define IDC_COMBO_MO_LUN	1140
#define IDC_COMBO_MO_CHANNEL_IDE 1141
#define IDC_COMBO_MO_TYPE	1142

#define IDC_CHECK_BUGGER	1150	/* other periph config */
#define IDC_CHECK_POSTCARD	1151
#define IDC_COMBO_ISARTC	1152
#define IDC_CONFIGURE_ISARTC	1153
#define IDC_COMBO_FDC		1154
#define IDC_CONFIGURE_FDC	1155
#define IDC_GROUP_ISAMEM	1156
#define IDC_COMBO_ISAMEM_1	1157
#define IDC_COMBO_ISAMEM_2	1158
#define IDC_COMBO_ISAMEM_3	1159
#define IDC_COMBO_ISAMEM_4	1160
#define IDC_CONFIGURE_ISAMEM_1	1161
#define IDC_CONFIGURE_ISAMEM_2	1162
#define IDC_CONFIGURE_ISAMEM_3	1163
#define IDC_CONFIGURE_ISAMEM_4	1164

#define IDC_SLIDER_GAIN		1170	/* sound gain dialog */

#define IDC_EDIT_FILE_NAME	1200	/* new floppy image dialog */
#define IDC_COMBO_DISK_SIZE	1201
#define IDC_COMBO_RPM_MODE	1202

/* For the DeviceConfig code, re-do later. */
#define IDC_CONFIG_BASE		1300
#define  IDC_CONFIGURE_VID	1300
#define  IDC_CONFIGURE_SND	1301
#define  IDC_CONFIGURE_VOODOO	1302
#define  IDC_CONFIGURE_MOD	1303
#define  IDC_CONFIGURE_NET_TYPE	1304
#define  IDC_CONFIGURE_BUSLOGIC	1305
#define  IDC_CONFIGURE_PCAP	1306
#define  IDC_CONFIGURE_NET	1307
#define  IDC_CONFIGURE_MIDI	1308
#define  IDC_CONFIGURE_MIDI_IN 1309
#define  IDC_JOY1		1310
#define  IDC_JOY2		1311
#define  IDC_JOY3		1312
#define  IDC_JOY4		1313
#define IDC_HDTYPE		1380
#define IDC_RENDER		1381
#define IDC_STATUS		1382

#define IDC_EDIT_WIDTH		1400	/* specify main window dimensions dialog */
#define IDC_WIDTHSPIN		1401
#define IDC_EDIT_HEIGHT		1402
#define IDC_HEIGHTSPIN		1403
#define IDC_CHECK_LOCK_SIZE	1404

#define IDM_ABOUT		40001
#define IDC_ABOUT_ICON		65535
#define IDM_ACTION_KBD_REQ_CAPTURE	40010
#define IDM_ACTION_RCTRL_IS_LALT	40011
#define IDM_ACTION_SCREENSHOT	40012
#define IDM_ACTION_HRESET	40013
#define IDM_ACTION_RESET_CAD	40014
#define IDM_ACTION_EXIT		40015
#define IDM_ACTION_CTRL_ALT_ESC 40016
#define IDM_ACTION_PAUSE	40017
#ifdef MTR_ENABLED
#define IDM_ACTION_BEGIN_TRACE	40018
#define IDM_ACTION_END_TRACE	40019
#define IDM_ACTION_TRACE        40020
#endif
#define IDM_CONFIG		40020
#define IDM_VID_HIDE_STATUS_BAR	40021
#define IDM_UPDATE_ICONS	40030
#define IDM_SND_GAIN		40031
#define IDM_VID_RESIZE		40040
#define IDM_VID_REMEMBER	40041
#define IDM_VID_SDL_SW		40050
#define IDM_VID_SDL_HW		40051
#define IDM_VID_SDL_OPENGL	40052
#define IDM_VID_OPENGL_CORE	40053
#ifdef USE_VNC
#define IDM_VID_VNC		40054
#endif
#define IDM_VID_SCALE_1X	40055
#define IDM_VID_SCALE_2X	40056
#define IDM_VID_SCALE_3X	40057
#define IDM_VID_SCALE_4X	40058
#define IDM_VID_HIDPI		40059
#define IDM_VID_FULLSCREEN	40060
#define IDM_VID_FS_FULL		40061
#define IDM_VID_FS_43		40062
#define IDM_VID_FS_KEEPRATIO	40063
#define IDM_VID_FS_INT		40064
#define IDM_VID_SPECIFY_DIM	40065
#define IDM_VID_FORCE43		40066
#define IDM_VID_OVERSCAN	40067
#define IDM_VID_INVERT		40069
#define IDM_VID_CGACON		40070
#define IDM_VID_GRAYCT_601	40075
#define IDM_VID_GRAYCT_709	40076
#define IDM_VID_GRAYCT_AVE	40077
#define IDM_VID_GRAY_RGB	40080
#define IDM_VID_GRAY_MONO	40081
#define IDM_VID_GRAY_AMBER	40082
#define IDM_VID_GRAY_GREEN	40083
#define IDM_VID_GRAY_WHITE	40084
#define IDM_VID_FILTER_NEAREST	40085
#define IDM_VID_FILTER_LINEAR	40086

#define IDM_MEDIA		40087
#define IDM_DOCS		40088

#ifdef USE_DISCORD
#define IDM_DISCORD		40090
#endif

#if defined(DEV_BRANCH) && defined(USE_OPENGL)
#define IDM_VID_GL_FPS_BLITTER	40100
#define IDM_VID_GL_FPS_25	40101
#define IDM_VID_GL_FPS_30	40102
#define IDM_VID_GL_FPS_50	40103
#define IDM_VID_GL_FPS_60	40104
#define IDM_VID_GL_FPS_75	40105
#define IDM_VID_GL_VSYNC	40106
#define IDM_VID_GL_SHADER	40107
#define IDM_VID_GL_NOSHADER	40108
#endif

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
#define IDM_CASSETTE_IMAGE_NEW		0x1200
#define IDM_CASSETTE_IMAGE_EXISTING	0x1300
#define IDM_CASSETTE_IMAGE_EXISTING_WP	0x1400
#define IDM_CASSETTE_RECORD		0x1500
#define IDM_CASSETTE_PLAY		0x1600
#define IDM_CASSETTE_REWIND		0x1700
#define IDM_CASSETTE_FAST_FORWARD	0x1800
#define IDM_CASSETTE_EJECT		0x1900

#define IDM_CARTRIDGE_IMAGE		0x2200
#define IDM_CARTRIDGE_EJECT		0x2300

#define IDM_FLOPPY_IMAGE_NEW		0x3200
#define IDM_FLOPPY_IMAGE_EXISTING	0x3300
#define IDM_FLOPPY_IMAGE_EXISTING_WP	0x3400
#define IDM_FLOPPY_EXPORT_TO_86F	0x3500
#define IDM_FLOPPY_EJECT		0x3600

#define IDM_CDROM_MUTE			0x4200
#define IDM_CDROM_EMPTY			0x4300
#define IDM_CDROM_RELOAD		0x4400
#define IDM_CDROM_IMAGE			0x4500
#define IDM_CDROM_HOST_DRIVE		0x4600

#define IDM_ZIP_IMAGE_NEW		0x5200
#define IDM_ZIP_IMAGE_EXISTING		0x5300
#define IDM_ZIP_IMAGE_EXISTING_WP	0x5400
#define IDM_ZIP_EJECT			0x5500
#define IDM_ZIP_RELOAD			0x5600

#define IDM_MO_IMAGE_NEW		0x6200
#define IDM_MO_IMAGE_EXISTING		0x6300
#define IDM_MO_IMAGE_EXISTING_WP	0x6400
#define IDM_MO_EJECT			0x6500
#define IDM_MO_RELOAD			0x6600


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
