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
 * Version:	@(#)resource.h	1.0.15	2017/12/09
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
#ifdef USE_DYNAREC
#define IDC_CHECK_DYNAREC	1016
#endif
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
#define IDC_CONFIGURE_MOUSE	1054

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
#define IDM_VID_VNC		40062
#define IDM_VID_RDP		40063
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
