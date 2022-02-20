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
#define DLG_ABOUT				101	/* top-level dialog */
#define DLG_STATUS				102	/* top-level dialog */
#define DLG_SND_GAIN			103	/* top-level dialog */
#define DLG_NEW_FLOPPY			104	/* top-level dialog */
#define DLG_SPECIFY_DIM			105	/* top-level dialog */
#define DLG_PREFERENCES			106	/* top-level dialog */
#define DLG_CONFIG				110	/* top-level dialog */
#define DLG_CFG_MACHINE			111	/* sub-dialog of config */
#define DLG_CFG_VIDEO			112	/* sub-dialog of config */
#define DLG_CFG_INPUT			113	/* sub-dialog of config */
#define DLG_CFG_SOUND			114	/* sub-dialog of config */
#define DLG_CFG_NETWORK			115	/* sub-dialog of config */
#define DLG_CFG_PORTS			116	/* sub-dialog of config */
#define DLG_CFG_STORAGE			117	/* sub-dialog of config */
#define DLG_CFG_HARD_DISKS		118	/* sub-dialog of config */
#define DLG_CFG_HARD_DISKS_ADD	119	/* sub-dialog of config */
#define DLG_CFG_FLOPPY_AND_CDROM_DRIVES	120	/* sub-dialog of config */
#define DLG_CFG_OTHER_REMOVABLE_DEVICES	121	/* sub-dialog of config */
#define DLG_CFG_PERIPHERALS		122	/* sub-dialog of config */

/* Static text label IDs. */

/* DLG_SND_GAIN */
#define IDT_GAIN		1700	/* Gain */

/* DLG_NEW_FLOPPY */
#define IDT_FLP_FILE_NAME	1701	/* File name: */
#define IDT_FLP_DISK_SIZE	1702	/* Disk size: */
#define IDT_FLP_RPM_MODE	1703	/* RPM mode: */
#define IDT_FLP_PROGRESS	1704	/* Progress: */

/* DLG_SPECIFY_DIM */
#define IDT_WIDTH		1705	/* ??? */
#define IDT_HEIGHT		1706	/* ??? */

/* DLG_CFG_MACHINE */
#define IDT_MACHINE_TYPE	1707	/* Machine type: */
#define IDT_MACHINE		1708	/* Machine: */
#define IDT_CPU_TYPE	1709	/* CPU type: */
#define IDT_CPU_SPEED	1710	/* CPU speed: */
#define IDT_FPU			1711	/* FPU: */
#define IDT_WAIT_STATES	1712	/* Wait states: */
#define IDT_MB			1713	/* MB	== IDC_TEXT_MB */
#define IDT_MEMORY		1714	/* Memory: */

/* DLG_CFG_VIDEO */
#define IDT_VIDEO		1715	/* Video: */

/* DLG_CFG_INPUT */
#define IDT_MOUSE		1716	/* Mouse: */
#define IDT_JOYSTICK	1717	/* Joystick: */

/* DLG_CFG_SOUND */
#define IDT_SOUND		1718	/* Sound card: */
#define IDT_MIDI_OUT	1719	/* MIDI Out Device: */
#define IDT_MIDI_IN		1720	/* MIDI In Device: */

/* DLG_CFG_NETWORK */
#define IDT_NET_TYPE	1721	/* Network type: */
#define IDT_PCAP		1722	/* PCap device: */
#define IDT_NET			1723	/* Network adapter: */

/* DLG_CFG_PORTS */
#define IDT_COM1		1724	/* COM1 Device: */
#define IDT_COM2		1725	/* COM1 Device: */
#define IDT_COM3		1726	/* COM1 Device: */
#define IDT_COM4		1727	/* COM1 Device: */

#define IDT_LPT1		1728	/* LPT1 Device: */
#define IDT_LPT2		1729	/* LPT2 Device: */
#define IDT_LPT3		1730	/* LPT3 Device: */
#define IDT_LPT4		1731	/* LPT4 Device: */

/* DLG_CFG_STORAGE */
#define IDT_HDC			1732	/* HD Controller: */
#define IDT_FDC			1733	/* Ext FD Controller: */
#define IDT_SCSI_1		1734	/* SCSI Board #1: */
#define IDT_SCSI_2		1735	/* SCSI Board #2: */
#define IDT_SCSI_3		1736	/* SCSI Board #3: */
#define IDT_SCSI_4		1737	/* SCSI Board #4: */

/* DLG_CFG_HARD_DISKS */
#define IDT_HDD			1738	/* Hard disks: */
#define IDT_BUS			1739	/* Bus: */
#define IDT_CHANNEL		1740	/* Channel: */
#define IDT_ID			1741	/* ID: */
#define IDT_LUN			1742	/* LUN: */

/* DLG_CFG_HARD_DISKS_ADD */
#define IDT_SECTORS		1743	/* Sectors: */
#define IDT_HEADS		1744	/* Heads: */
#define IDT_CYLS		1745	/* Cylinders: */
#define IDT_SIZE_MB		1746	/* Size (MB): */
#define IDT_TYPE		1747	/* Type: */
#define IDT_FILE_NAME	1748	/* File name: */
#define IDT_IMG_FORMAT	1749	/* Image Format: */
#define IDT_BLOCK_SIZE	1750	/* Block Size: */
#define IDT_PROGRESS	1751	/* Progress: */

/* DLG_CFG_FLOPPY_AND_CDROM_DRIVES */
#define IDT_FLOPPY_DRIVES	1752	/* Floppy drives: */
#define IDT_FDD_TYPE	1753	/* Type: */
#define IDT_CD_DRIVES	1754	/* CD-ROM drives: */
#define IDT_CD_BUS		1755	/* Bus: */
#define IDT_CD_ID		1756	/* ID: */
#define IDT_CD_LUN		1757	/* LUN: */
#define IDT_CD_CHANNEL	1758	/* Channel: */
#define IDT_CD_SPEED	1759	/* Speed: */

/* DLG_CFG_OTHER_REMOVABLE_DEVICES */
#define IDT_MO_DRIVES	1760	/* MO drives: */
#define IDT_MO_BUS		1761	/* Bus: */
#define IDT_MO_ID		1762	/* ID: */
#define IDT_MO_CHANNEL	1763	/* Channel */
#define IDT_MO_TYPE		1764	/* Type: */

#define IDT_ZIP_DRIVES	1765	/* ZIP drives: */
#define IDT_ZIP_BUS		1766	/* Bus: */
#define IDT_ZIP_ID		1767	/* ID: */
#define IDT_ZIP_LUN		1768	/* LUN: */
#define IDT_ZIP_CHANNEL	1769	/* Channel: */

/* DLG_CFG_PERIPHERALS */
#define IDT_ISARTC		1770	/* ISA RTC: */
#define IDT_ISAMEM_1	1771	/* ISAMEM Board #1: */
#define IDT_ISAMEM_2	1772	/* ISAMEM Board #2: */
#define IDT_ISAMEM_3	1773	/* ISAMEM Board #3: */
#define IDT_ISAMEM_4	1774	/* ISAMEM Board #4: */

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

#define IDC_COMBO_MACHINE_TYPE	1010
#define IDC_COMBO_MACHINE	1011	/* machine/cpu config */
#define IDC_CONFIGURE_MACHINE	1012
#define IDC_COMBO_CPU_TYPE	1013
#define IDC_COMBO_CPU_SPEED	1014
#define IDC_COMBO_FPU		1015
#define IDC_COMBO_WS		1016
#ifdef USE_DYNAREC
#define IDC_CHECK_DYNAREC	1017
#endif
#define IDC_MEMTEXT		1018
#define IDC_MEMSPIN		1019
#define IDC_TEXT_MB		IDT_MB

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
#define IDC_COMBO_MIDI_OUT	1045
#define IDC_CHECK_MPU401	1046
#define IDC_CONFIGURE_MPU401	1047
#define IDC_CHECK_FLOAT		1048
#define IDC_CONFIGURE_GUS	1049
#define IDC_COMBO_MIDI_IN	1050
#define IDC_CONFIGURE_CMS	1051
#define IDC_CONFIGURE_SSI	1052

#define IDC_COMBO_NET_TYPE	1060	/* network config */
#define IDC_COMBO_PCAP		1061
#define IDC_COMBO_NET		1062

#define IDC_COMBO_LPT1		1070	/* ports config */
#define IDC_COMBO_LPT2		1071
#define IDC_COMBO_LPT3		1072
#define IDC_COMBO_LPT4		1073
#define IDC_CHECK_SERIAL1	1074
#define IDC_CHECK_SERIAL2	1075
#define IDC_CHECK_SERIAL3	1076
#define IDC_CHECK_SERIAL4	1077
#define IDC_CHECK_PARALLEL1	1078
#define IDC_CHECK_PARALLEL2	1079
#define IDC_CHECK_PARALLEL3	1080
#define IDC_CHECK_PARALLEL4	1081

#define IDC_OTHER_PERIPH	1082	/* storage controllers config */
#define IDC_COMBO_HDC		1083
#define IDC_CONFIGURE_HDC	1084
#define IDC_CHECK_IDE_TER	1085
#define IDC_BUTTON_IDE_TER	1086
#define IDC_CHECK_IDE_QUA	1087
#define IDC_BUTTON_IDE_QUA	1088
#define IDC_GROUP_SCSI		1089
#define IDC_COMBO_SCSI_1	1090
#define IDC_COMBO_SCSI_2	1091
#define IDC_COMBO_SCSI_3	1092
#define IDC_COMBO_SCSI_4	1093
#define IDC_CONFIGURE_SCSI_1	1094
#define IDC_CONFIGURE_SCSI_2	1095
#define IDC_CONFIGURE_SCSI_3	1096
#define IDC_CONFIGURE_SCSI_4	1097
#define IDC_CHECK_CASSETTE	1098

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

#define IDC_COMBO_LANG		1009    /* change language dialog */
#define IDC_COMBO_ICON		1010
#define IDC_CHECKBOX_GLOBAL 1300
#define IDC_BUTTON_DEFAULT  1302
#define IDC_BUTTON_DEFICON  1304

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
#define  IDC_CONFIGURE_MIDI_OUT	1308
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
#define IDM_VID_HIDE_TOOLBAR	40022
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

#define IDM_DISCORD		40090

#define IDM_PREFERENCES		40091

#define IDM_VID_GL_FPS_BLITTER	40100
#define IDM_VID_GL_FPS_25	40101
#define IDM_VID_GL_FPS_30	40102
#define IDM_VID_GL_FPS_50	40103
#define IDM_VID_GL_FPS_60	40104
#define IDM_VID_GL_FPS_75	40105
#define IDM_VID_GL_VSYNC	40106
#define IDM_VID_GL_SHADER	40107
#define IDM_VID_GL_NOSHADER	40108

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
