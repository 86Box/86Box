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
 * Version:	@(#)resource.h	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

/* {{NO_DEPENDENCIES}}
   Microsoft Developer Studio generated include file.
   Used by 86Box.rc
*/
#define IDHDCONFIG                      3
#define IDCDCONFIG                      4
#define CONFIGUREDLG_MACHINE            101
#define CONFIGUREDLG_VIDEO              102
#define CONFIGUREDLG_INPUT              103
#define CONFIGUREDLG_SOUND              104
#define CONFIGUREDLG_NETWORK            105
#define CONFIGUREDLG_PERIPHERALS        106
#define CONFIGUREDLG_HARD_DISKS         107
#define CONFIGUREDLG_REMOVABLE_DEVICES  108
#define ABOUTDLG                        109
#define CONFIGUREDLG_HARD_DISKS_ADD     110
#define CONFIGUREDLG_MAIN               117
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
#define IDC_LIST_FLOPPY_DRIVES          1020
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
#define IDS_STRING2049                  2049
#define IDS_STRING2050                  2050
#define IDS_STRING2051                  2051
#define IDS_STRING2052                  2052
#define IDS_STRING2053                  2053
#define IDS_STRING2054                  2054
#define IDS_STRING2055                  2055
#define IDS_STRING2056                  2056
#define IDS_STRING2057                  2057
#define IDS_STRING2058                  2058
#define IDS_STRING2059                  2059
#define IDS_STRING2060                  2060
#define IDS_STRING2061                  2061
#define IDS_STRING2062                  2062
#define IDS_STRING2063                  2063
#define IDS_STRING2064                  2064
#define IDS_STRING2065                  2065
#define IDS_STRING2066                  2066
#define IDS_STRING2067                  2067
#define IDS_STRING2068                  2068
#define IDS_STRING2069                  2069
#define IDS_STRING2070                  2070
#define IDS_STRING2071                  2071
#define IDS_STRING2072                  2072
#define IDS_STRING2073                  2073
#define IDS_STRING2074                  2074
#define IDS_STRING2075                  2075
#define IDS_STRING2076                  2076
#define IDS_STRING2077                  2077
#define IDS_STRING2078                  2078
#define IDS_STRING2079                  2079
#define IDM_ABOUT                       40001
#define IDC_ABOUT_ICON                  65535

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

#ifdef ENABLE_LOG_TOGGLES
# ifdef ENABLE_BUSLOGIC_LOG
#  define IDM_LOG_BUSLOGIC	51200
# endif
# ifdef ENABLE_CDROM_LOG
#  define IDM_LOG_CDROM		51201
# endif
# ifdef ENABLE_D86F_LOG
#  define IDM_LOG_D86F		51202
# endif
# ifdef ENABLE_FDC_LOG
#  define IDM_LOG_FDC		51203
# endif
# ifdef ENABLE_IDE_LOG
#  define IDM_LOG_IDE		51204
# endif
# ifdef ENABLE_NE2000_LOG
#  define IDM_LOG_NE2000	51205
# endif
#endif
#ifdef ENABLE_LOG_BREAKPOINT
# define IDM_LOG_BREAKPOINT	51206
#endif
#ifdef ENABLE_VRAM_DUMP
# define IDM_DUMP_VRAM		51207
#endif

#define IDC_COMBO1 1000
#define IDC_COMBOVID 1001
#define IDC_COMBO3 1002
#define IDC_COMBO4 1003
#define IDC_COMBO5 1004
#define IDC_COMBO386 1005
#define IDC_COMBO486 1006
#define IDC_COMBOSND 1007
#define IDC_COMBONETTYPE 1008
#define IDC_COMBOPCAP 1009
#define IDC_COMBONET 1010
#define IDC_COMBOCPUM 1060
#define IDC_COMBOSPD  1061
#define IDC_COMBODR1  1062
#define IDC_COMBODR2  1063
#define IDC_COMBODR3  1064
#define IDC_COMBODR4  1065
#define IDC_COMBOJOY  1066
#define IDC_COMBOWS  1067
#define IDC_COMBOMOUSE 1068
#define IDC_COMBOHDD 1069
#define IDC_CHECK1 1010
#define IDC_CHECK2 1011
#define IDC_CHECK3 1012
#define IDC_CHECKSSI 1014
#define IDC_CHECKVOODOO 1015
#define IDC_CHECKDYNAREC 1016
#define IDC_CHECKBUSLOGIC 1017
#define IDC_CHECKSYNC 1024
#define IDC_CHECKXTIDE 1025
#define IDC_CHECKFPU 1026
#define IDC_EDIT1  1030
#define IDC_EDIT2  1031
#define IDC_EDIT3  1032
#define IDC_EDIT4  1033
#define IDC_EDIT5  1034
#define IDC_EDIT6  1035
#define IDC_COMBOHDT	1036

#define IDC_CFILE  1060

#define IDC_HDTYPE 1280

#define IDC_RENDER 1281
#define IDC_STATUS 1282

#define IDC_MEMSPIN 1100
#define IDC_MEMTEXT 1101
#define IDC_STEXT1 1102
#define IDC_STEXT2 1103
#define IDC_STEXT3 1104
#define IDC_STEXT4 1105
#define IDC_STEXT5 1106
#define IDC_STEXT6 1107
#define IDC_STEXT7 1108
#define IDC_STEXT8 1109
#define IDC_STEXT_DEVICE 1110
#define IDC_TEXT_MB 1111
#define IDC_TEXT1  1115
#define IDC_TEXT2  1116

#define IDC_CONFIGUREVID 1200
#define IDC_CONFIGURESND 1201
#define IDC_CONFIGUREVOODOO 1202
#define IDC_CONFIGUREMOD 1203
#define IDC_CONFIGURENETTYPE 1204
#define IDC_CONFIGUREBUSLOGIC 1205
#define IDC_CONFIGUREPCAP 1206
#define IDC_CONFIGURENET 1207
#define IDC_JOY1 1210
#define IDC_JOY2 1211
#define IDC_JOY3 1212
#define IDC_JOY4 1213

#define IDC_CONFIG_BASE 1200

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

#define STRINGS_NUM 174
