/****************************************************************************
** Meta object code from reading C++ file 'qt_mainwindow.hpp'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.10)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../qt/qt_mainwindow.hpp"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'qt_mainwindow.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.10. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_MainWindow_t {
    QByteArrayData data[107];
    char stringdata0[2695];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_MainWindow_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_MainWindow_t qt_meta_stringdata_MainWindow = {
    {
QT_MOC_LITERAL(0, 0, 10), // "MainWindow"
QT_MOC_LITERAL(1, 11, 5), // "paint"
QT_MOC_LITERAL(2, 17, 0), // ""
QT_MOC_LITERAL(3, 18, 5), // "image"
QT_MOC_LITERAL(4, 24, 14), // "resizeContents"
QT_MOC_LITERAL(5, 39, 1), // "w"
QT_MOC_LITERAL(6, 41, 1), // "h"
QT_MOC_LITERAL(7, 43, 21), // "resizeContentsMonitor"
QT_MOC_LITERAL(8, 65, 13), // "monitor_index"
QT_MOC_LITERAL(9, 79, 16), // "statusBarMessage"
QT_MOC_LITERAL(10, 96, 3), // "msg"
QT_MOC_LITERAL(11, 100, 20), // "updateStatusBarPanes"
QT_MOC_LITERAL(12, 121, 23), // "updateStatusBarActivity"
QT_MOC_LITERAL(13, 145, 3), // "tag"
QT_MOC_LITERAL(14, 149, 6), // "active"
QT_MOC_LITERAL(15, 156, 20), // "updateStatusBarEmpty"
QT_MOC_LITERAL(16, 177, 5), // "empty"
QT_MOC_LITERAL(17, 183, 18), // "updateStatusBarTip"
QT_MOC_LITERAL(18, 202, 23), // "updateMenuResizeOptions"
QT_MOC_LITERAL(19, 226, 26), // "updateWindowRememberOption"
QT_MOC_LITERAL(20, 253, 19), // "initRendererMonitor"
QT_MOC_LITERAL(21, 273, 22), // "destroyRendererMonitor"
QT_MOC_LITERAL(22, 296, 33), // "initRendererMonitorForNonQtTh..."
QT_MOC_LITERAL(23, 330, 36), // "destroyRendererMonitorForNonQ..."
QT_MOC_LITERAL(24, 367, 18), // "hardResetCompleted"
QT_MOC_LITERAL(25, 386, 8), // "setTitle"
QT_MOC_LITERAL(26, 395, 5), // "title"
QT_MOC_LITERAL(27, 401, 13), // "setFullscreen"
QT_MOC_LITERAL(28, 415, 5), // "state"
QT_MOC_LITERAL(29, 421, 15), // "setMouseCapture"
QT_MOC_LITERAL(30, 437, 25), // "showMessageForNonQtThread"
QT_MOC_LITERAL(31, 463, 5), // "flags"
QT_MOC_LITERAL(32, 469, 6), // "header"
QT_MOC_LITERAL(33, 476, 7), // "message"
QT_MOC_LITERAL(34, 484, 22), // "getTitleForNonQtThread"
QT_MOC_LITERAL(35, 507, 8), // "wchar_t*"
QT_MOC_LITERAL(36, 516, 12), // "showSettings"
QT_MOC_LITERAL(37, 529, 9), // "hardReset"
QT_MOC_LITERAL(38, 539, 11), // "togglePause"
QT_MOC_LITERAL(39, 551, 23), // "initRendererMonitorSlot"
QT_MOC_LITERAL(40, 575, 26), // "destroyRendererMonitorSlot"
QT_MOC_LITERAL(41, 602, 18), // "updateUiPauseState"
QT_MOC_LITERAL(42, 621, 29), // "on_actionFullscreen_triggered"
QT_MOC_LITERAL(43, 651, 27), // "on_actionSettings_triggered"
QT_MOC_LITERAL(44, 679, 23), // "on_actionExit_triggered"
QT_MOC_LITERAL(45, 703, 29), // "on_actionAuto_pause_triggered"
QT_MOC_LITERAL(46, 733, 24), // "on_actionPause_triggered"
QT_MOC_LITERAL(47, 758, 31), // "on_actionCtrl_Alt_Del_triggered"
QT_MOC_LITERAL(48, 790, 31), // "on_actionCtrl_Alt_Esc_triggered"
QT_MOC_LITERAL(49, 822, 29), // "on_actionHard_Reset_triggered"
QT_MOC_LITERAL(50, 852, 41), // "on_actionRight_CTRL_is_left_A..."
QT_MOC_LITERAL(51, 894, 44), // "on_actionKeyboard_requires_ca..."
QT_MOC_LITERAL(52, 939, 35), // "on_actionResizable_window_tri..."
QT_MOC_LITERAL(53, 975, 7), // "checked"
QT_MOC_LITERAL(54, 983, 39), // "on_actionInverted_VGA_monitor..."
QT_MOC_LITERAL(55, 1023, 23), // "on_action0_5x_triggered"
QT_MOC_LITERAL(56, 1047, 21), // "on_action1x_triggered"
QT_MOC_LITERAL(57, 1069, 23), // "on_action1_5x_triggered"
QT_MOC_LITERAL(58, 1093, 21), // "on_action2x_triggered"
QT_MOC_LITERAL(59, 1115, 21), // "on_action3x_triggered"
QT_MOC_LITERAL(60, 1137, 21), // "on_action4x_triggered"
QT_MOC_LITERAL(61, 1159, 21), // "on_action5x_triggered"
QT_MOC_LITERAL(62, 1181, 21), // "on_action6x_triggered"
QT_MOC_LITERAL(63, 1203, 21), // "on_action7x_triggered"
QT_MOC_LITERAL(64, 1225, 21), // "on_action8x_triggered"
QT_MOC_LITERAL(65, 1247, 25), // "on_actionLinear_triggered"
QT_MOC_LITERAL(66, 1273, 26), // "on_actionNearest_triggered"
QT_MOC_LITERAL(67, 1300, 33), // "on_actionFullScreen_int_trigg..."
QT_MOC_LITERAL(68, 1334, 35), // "on_actionFullScreen_int43_tri..."
QT_MOC_LITERAL(69, 1370, 39), // "on_actionFullScreen_keepRatio..."
QT_MOC_LITERAL(70, 1410, 32), // "on_actionFullScreen_43_triggered"
QT_MOC_LITERAL(71, 1443, 37), // "on_actionFullScreen_stretch_t..."
QT_MOC_LITERAL(72, 1481, 32), // "on_actionWhite_monitor_triggered"
QT_MOC_LITERAL(73, 1514, 32), // "on_actionGreen_monitor_triggered"
QT_MOC_LITERAL(74, 1547, 32), // "on_actionAmber_monitor_triggered"
QT_MOC_LITERAL(75, 1580, 32), // "on_actionRGB_Grayscale_triggered"
QT_MOC_LITERAL(76, 1613, 28), // "on_actionRGB_Color_triggered"
QT_MOC_LITERAL(77, 1642, 26), // "on_actionAverage_triggered"
QT_MOC_LITERAL(78, 1669, 29), // "on_actionBT709_HDTV_triggered"
QT_MOC_LITERAL(79, 1699, 33), // "on_actionBT601_NTSC_PAL_trigg..."
QT_MOC_LITERAL(80, 1733, 32), // "on_actionDocumentation_triggered"
QT_MOC_LITERAL(81, 1766, 30), // "on_actionAbout_86Box_triggered"
QT_MOC_LITERAL(82, 1797, 27), // "on_actionAbout_Qt_triggered"
QT_MOC_LITERAL(83, 1825, 42), // "on_actionForce_4_3_display_ra..."
QT_MOC_LITERAL(84, 1868, 57), // "on_actionChange_contrast_for_..."
QT_MOC_LITERAL(85, 1926, 52), // "on_actionCGA_PCjr_Tandy_EGA_S..."
QT_MOC_LITERAL(86, 1979, 45), // "on_actionRemember_size_and_po..."
QT_MOC_LITERAL(87, 2025, 37), // "on_actionSpecify_dimensions_t..."
QT_MOC_LITERAL(88, 2063, 32), // "on_actionHiDPI_scaling_triggered"
QT_MOC_LITERAL(89, 2096, 34), // "on_actionHide_status_bar_trig..."
QT_MOC_LITERAL(90, 2131, 32), // "on_actionHide_tool_bar_triggered"
QT_MOC_LITERAL(91, 2164, 42), // "on_actionUpdate_status_bar_ic..."
QT_MOC_LITERAL(92, 2207, 34), // "on_actionTake_screenshot_trig..."
QT_MOC_LITERAL(93, 2242, 29), // "on_actionSound_gain_triggered"
QT_MOC_LITERAL(94, 2272, 30), // "on_actionPreferences_triggered"
QT_MOC_LITERAL(95, 2303, 45), // "on_actionEnable_Discord_integ..."
QT_MOC_LITERAL(96, 2349, 35), // "on_actionRenderer_options_tri..."
QT_MOC_LITERAL(97, 2385, 16), // "refreshMediaMenu"
QT_MOC_LITERAL(98, 2402, 12), // "showMessage_"
QT_MOC_LITERAL(99, 2415, 9), // "getTitle_"
QT_MOC_LITERAL(100, 2425, 30), // "on_actionMCA_devices_triggered"
QT_MOC_LITERAL(101, 2456, 22), // "on_actionPen_triggered"
QT_MOC_LITERAL(102, 2479, 30), // "on_actionCursor_Puck_triggered"
QT_MOC_LITERAL(103, 2510, 32), // "on_actionACPI_Shutdown_triggered"
QT_MOC_LITERAL(104, 2543, 44), // "on_actionShow_non_primary_mon..."
QT_MOC_LITERAL(105, 2588, 42), // "on_actionOpen_screenshots_fol..."
QT_MOC_LITERAL(106, 2631, 63) // "on_actionApply_fullscreen_str..."

    },
    "MainWindow\0paint\0\0image\0resizeContents\0"
    "w\0h\0resizeContentsMonitor\0monitor_index\0"
    "statusBarMessage\0msg\0updateStatusBarPanes\0"
    "updateStatusBarActivity\0tag\0active\0"
    "updateStatusBarEmpty\0empty\0"
    "updateStatusBarTip\0updateMenuResizeOptions\0"
    "updateWindowRememberOption\0"
    "initRendererMonitor\0destroyRendererMonitor\0"
    "initRendererMonitorForNonQtThread\0"
    "destroyRendererMonitorForNonQtThread\0"
    "hardResetCompleted\0setTitle\0title\0"
    "setFullscreen\0state\0setMouseCapture\0"
    "showMessageForNonQtThread\0flags\0header\0"
    "message\0getTitleForNonQtThread\0wchar_t*\0"
    "showSettings\0hardReset\0togglePause\0"
    "initRendererMonitorSlot\0"
    "destroyRendererMonitorSlot\0"
    "updateUiPauseState\0on_actionFullscreen_triggered\0"
    "on_actionSettings_triggered\0"
    "on_actionExit_triggered\0"
    "on_actionAuto_pause_triggered\0"
    "on_actionPause_triggered\0"
    "on_actionCtrl_Alt_Del_triggered\0"
    "on_actionCtrl_Alt_Esc_triggered\0"
    "on_actionHard_Reset_triggered\0"
    "on_actionRight_CTRL_is_left_ALT_triggered\0"
    "on_actionKeyboard_requires_capture_triggered\0"
    "on_actionResizable_window_triggered\0"
    "checked\0on_actionInverted_VGA_monitor_triggered\0"
    "on_action0_5x_triggered\0on_action1x_triggered\0"
    "on_action1_5x_triggered\0on_action2x_triggered\0"
    "on_action3x_triggered\0on_action4x_triggered\0"
    "on_action5x_triggered\0on_action6x_triggered\0"
    "on_action7x_triggered\0on_action8x_triggered\0"
    "on_actionLinear_triggered\0"
    "on_actionNearest_triggered\0"
    "on_actionFullScreen_int_triggered\0"
    "on_actionFullScreen_int43_triggered\0"
    "on_actionFullScreen_keepRatio_triggered\0"
    "on_actionFullScreen_43_triggered\0"
    "on_actionFullScreen_stretch_triggered\0"
    "on_actionWhite_monitor_triggered\0"
    "on_actionGreen_monitor_triggered\0"
    "on_actionAmber_monitor_triggered\0"
    "on_actionRGB_Grayscale_triggered\0"
    "on_actionRGB_Color_triggered\0"
    "on_actionAverage_triggered\0"
    "on_actionBT709_HDTV_triggered\0"
    "on_actionBT601_NTSC_PAL_triggered\0"
    "on_actionDocumentation_triggered\0"
    "on_actionAbout_86Box_triggered\0"
    "on_actionAbout_Qt_triggered\0"
    "on_actionForce_4_3_display_ratio_triggered\0"
    "on_actionChange_contrast_for_monochrome_display_triggered\0"
    "on_actionCGA_PCjr_Tandy_EGA_S_VGA_overscan_triggered\0"
    "on_actionRemember_size_and_position_triggered\0"
    "on_actionSpecify_dimensions_triggered\0"
    "on_actionHiDPI_scaling_triggered\0"
    "on_actionHide_status_bar_triggered\0"
    "on_actionHide_tool_bar_triggered\0"
    "on_actionUpdate_status_bar_icons_triggered\0"
    "on_actionTake_screenshot_triggered\0"
    "on_actionSound_gain_triggered\0"
    "on_actionPreferences_triggered\0"
    "on_actionEnable_Discord_integration_triggered\0"
    "on_actionRenderer_options_triggered\0"
    "refreshMediaMenu\0showMessage_\0getTitle_\0"
    "on_actionMCA_devices_triggered\0"
    "on_actionPen_triggered\0"
    "on_actionCursor_Puck_triggered\0"
    "on_actionACPI_Shutdown_triggered\0"
    "on_actionShow_non_primary_monitors_triggered\0"
    "on_actionOpen_screenshots_folder_triggered\0"
    "on_actionApply_fullscreen_stretch_mode_when_maximized_triggered"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_MainWindow[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      90,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      20,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,  464,    2, 0x06 /* Public */,
       4,    2,  467,    2, 0x06 /* Public */,
       7,    3,  472,    2, 0x06 /* Public */,
       9,    1,  479,    2, 0x06 /* Public */,
      11,    0,  482,    2, 0x06 /* Public */,
      12,    2,  483,    2, 0x06 /* Public */,
      15,    2,  488,    2, 0x06 /* Public */,
      17,    1,  493,    2, 0x06 /* Public */,
      18,    0,  496,    2, 0x06 /* Public */,
      19,    0,  497,    2, 0x06 /* Public */,
      20,    1,  498,    2, 0x06 /* Public */,
      21,    1,  501,    2, 0x06 /* Public */,
      22,    1,  504,    2, 0x06 /* Public */,
      23,    1,  507,    2, 0x06 /* Public */,
      24,    0,  510,    2, 0x06 /* Public */,
      25,    1,  511,    2, 0x06 /* Public */,
      27,    1,  514,    2, 0x06 /* Public */,
      29,    1,  517,    2, 0x06 /* Public */,
      30,    3,  520,    2, 0x06 /* Public */,
      34,    1,  527,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      36,    0,  530,    2, 0x0a /* Public */,
      37,    0,  531,    2, 0x0a /* Public */,
      38,    0,  532,    2, 0x0a /* Public */,
      39,    1,  533,    2, 0x0a /* Public */,
      40,    1,  536,    2, 0x0a /* Public */,
      41,    0,  539,    2, 0x0a /* Public */,
      42,    0,  540,    2, 0x08 /* Private */,
      43,    0,  541,    2, 0x08 /* Private */,
      44,    0,  542,    2, 0x08 /* Private */,
      45,    0,  543,    2, 0x08 /* Private */,
      46,    0,  544,    2, 0x08 /* Private */,
      47,    0,  545,    2, 0x08 /* Private */,
      48,    0,  546,    2, 0x08 /* Private */,
      49,    0,  547,    2, 0x08 /* Private */,
      50,    0,  548,    2, 0x08 /* Private */,
      51,    0,  549,    2, 0x08 /* Private */,
      52,    1,  550,    2, 0x08 /* Private */,
      54,    0,  553,    2, 0x08 /* Private */,
      55,    0,  554,    2, 0x08 /* Private */,
      56,    0,  555,    2, 0x08 /* Private */,
      57,    0,  556,    2, 0x08 /* Private */,
      58,    0,  557,    2, 0x08 /* Private */,
      59,    0,  558,    2, 0x08 /* Private */,
      60,    0,  559,    2, 0x08 /* Private */,
      61,    0,  560,    2, 0x08 /* Private */,
      62,    0,  561,    2, 0x08 /* Private */,
      63,    0,  562,    2, 0x08 /* Private */,
      64,    0,  563,    2, 0x08 /* Private */,
      65,    0,  564,    2, 0x08 /* Private */,
      66,    0,  565,    2, 0x08 /* Private */,
      67,    0,  566,    2, 0x08 /* Private */,
      68,    0,  567,    2, 0x08 /* Private */,
      69,    0,  568,    2, 0x08 /* Private */,
      70,    0,  569,    2, 0x08 /* Private */,
      71,    0,  570,    2, 0x08 /* Private */,
      72,    0,  571,    2, 0x08 /* Private */,
      73,    0,  572,    2, 0x08 /* Private */,
      74,    0,  573,    2, 0x08 /* Private */,
      75,    0,  574,    2, 0x08 /* Private */,
      76,    0,  575,    2, 0x08 /* Private */,
      77,    0,  576,    2, 0x08 /* Private */,
      78,    0,  577,    2, 0x08 /* Private */,
      79,    0,  578,    2, 0x08 /* Private */,
      80,    0,  579,    2, 0x08 /* Private */,
      81,    0,  580,    2, 0x08 /* Private */,
      82,    0,  581,    2, 0x08 /* Private */,
      83,    0,  582,    2, 0x08 /* Private */,
      84,    0,  583,    2, 0x08 /* Private */,
      85,    0,  584,    2, 0x08 /* Private */,
      86,    0,  585,    2, 0x08 /* Private */,
      87,    0,  586,    2, 0x08 /* Private */,
      88,    0,  587,    2, 0x08 /* Private */,
      89,    0,  588,    2, 0x08 /* Private */,
      90,    0,  589,    2, 0x08 /* Private */,
      91,    0,  590,    2, 0x08 /* Private */,
      92,    0,  591,    2, 0x08 /* Private */,
      93,    0,  592,    2, 0x08 /* Private */,
      94,    0,  593,    2, 0x08 /* Private */,
      95,    1,  594,    2, 0x08 /* Private */,
      96,    0,  597,    2, 0x08 /* Private */,
      97,    0,  598,    2, 0x08 /* Private */,
      98,    3,  599,    2, 0x08 /* Private */,
      99,    1,  606,    2, 0x08 /* Private */,
     100,    0,  609,    2, 0x08 /* Private */,
     101,    0,  610,    2, 0x08 /* Private */,
     102,    0,  611,    2, 0x08 /* Private */,
     103,    0,  612,    2, 0x08 /* Private */,
     104,    0,  613,    2, 0x08 /* Private */,
     105,    0,  614,    2, 0x08 /* Private */,
     106,    1,  615,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::QImage,    3,
    QMetaType::Void, QMetaType::Int, QMetaType::Int,    5,    6,
    QMetaType::Void, QMetaType::Int, QMetaType::Int, QMetaType::Int,    5,    6,    8,
    QMetaType::Void, QMetaType::QString,   10,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, QMetaType::Bool,   13,   14,
    QMetaType::Void, QMetaType::Int, QMetaType::Bool,   13,   16,
    QMetaType::Void, QMetaType::Int,   13,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    8,
    QMetaType::Void, QMetaType::Int,    8,
    QMetaType::Void, QMetaType::Int,    8,
    QMetaType::Void, QMetaType::Int,    8,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   26,
    QMetaType::Void, QMetaType::Bool,   28,
    QMetaType::Void, QMetaType::Bool,   28,
    QMetaType::Void, QMetaType::Int, QMetaType::QString, QMetaType::QString,   31,   32,   33,
    QMetaType::Void, 0x80000000 | 35,   26,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    8,
    QMetaType::Void, QMetaType::Int,    8,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,   53,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,   53,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, QMetaType::QString, QMetaType::QString,   31,   32,   33,
    QMetaType::Void, 0x80000000 | 35,   26,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,   53,

       0        // eod
};

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<MainWindow *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->paint((*reinterpret_cast< const QImage(*)>(_a[1]))); break;
        case 1: _t->resizeContents((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2]))); break;
        case 2: _t->resizeContentsMonitor((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3]))); break;
        case 3: _t->statusBarMessage((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 4: _t->updateStatusBarPanes(); break;
        case 5: _t->updateStatusBarActivity((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< bool(*)>(_a[2]))); break;
        case 6: _t->updateStatusBarEmpty((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< bool(*)>(_a[2]))); break;
        case 7: _t->updateStatusBarTip((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 8: _t->updateMenuResizeOptions(); break;
        case 9: _t->updateWindowRememberOption(); break;
        case 10: _t->initRendererMonitor((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 11: _t->destroyRendererMonitor((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 12: _t->initRendererMonitorForNonQtThread((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 13: _t->destroyRendererMonitorForNonQtThread((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 14: _t->hardResetCompleted(); break;
        case 15: _t->setTitle((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 16: _t->setFullscreen((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 17: _t->setMouseCapture((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 18: _t->showMessageForNonQtThread((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3]))); break;
        case 19: _t->getTitleForNonQtThread((*reinterpret_cast< wchar_t*(*)>(_a[1]))); break;
        case 20: _t->showSettings(); break;
        case 21: _t->hardReset(); break;
        case 22: _t->togglePause(); break;
        case 23: _t->initRendererMonitorSlot((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 24: _t->destroyRendererMonitorSlot((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 25: _t->updateUiPauseState(); break;
        case 26: _t->on_actionFullscreen_triggered(); break;
        case 27: _t->on_actionSettings_triggered(); break;
        case 28: _t->on_actionExit_triggered(); break;
        case 29: _t->on_actionAuto_pause_triggered(); break;
        case 30: _t->on_actionPause_triggered(); break;
        case 31: _t->on_actionCtrl_Alt_Del_triggered(); break;
        case 32: _t->on_actionCtrl_Alt_Esc_triggered(); break;
        case 33: _t->on_actionHard_Reset_triggered(); break;
        case 34: _t->on_actionRight_CTRL_is_left_ALT_triggered(); break;
        case 35: _t->on_actionKeyboard_requires_capture_triggered(); break;
        case 36: _t->on_actionResizable_window_triggered((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 37: _t->on_actionInverted_VGA_monitor_triggered(); break;
        case 38: _t->on_action0_5x_triggered(); break;
        case 39: _t->on_action1x_triggered(); break;
        case 40: _t->on_action1_5x_triggered(); break;
        case 41: _t->on_action2x_triggered(); break;
        case 42: _t->on_action3x_triggered(); break;
        case 43: _t->on_action4x_triggered(); break;
        case 44: _t->on_action5x_triggered(); break;
        case 45: _t->on_action6x_triggered(); break;
        case 46: _t->on_action7x_triggered(); break;
        case 47: _t->on_action8x_triggered(); break;
        case 48: _t->on_actionLinear_triggered(); break;
        case 49: _t->on_actionNearest_triggered(); break;
        case 50: _t->on_actionFullScreen_int_triggered(); break;
        case 51: _t->on_actionFullScreen_int43_triggered(); break;
        case 52: _t->on_actionFullScreen_keepRatio_triggered(); break;
        case 53: _t->on_actionFullScreen_43_triggered(); break;
        case 54: _t->on_actionFullScreen_stretch_triggered(); break;
        case 55: _t->on_actionWhite_monitor_triggered(); break;
        case 56: _t->on_actionGreen_monitor_triggered(); break;
        case 57: _t->on_actionAmber_monitor_triggered(); break;
        case 58: _t->on_actionRGB_Grayscale_triggered(); break;
        case 59: _t->on_actionRGB_Color_triggered(); break;
        case 60: _t->on_actionAverage_triggered(); break;
        case 61: _t->on_actionBT709_HDTV_triggered(); break;
        case 62: _t->on_actionBT601_NTSC_PAL_triggered(); break;
        case 63: _t->on_actionDocumentation_triggered(); break;
        case 64: _t->on_actionAbout_86Box_triggered(); break;
        case 65: _t->on_actionAbout_Qt_triggered(); break;
        case 66: _t->on_actionForce_4_3_display_ratio_triggered(); break;
        case 67: _t->on_actionChange_contrast_for_monochrome_display_triggered(); break;
        case 68: _t->on_actionCGA_PCjr_Tandy_EGA_S_VGA_overscan_triggered(); break;
        case 69: _t->on_actionRemember_size_and_position_triggered(); break;
        case 70: _t->on_actionSpecify_dimensions_triggered(); break;
        case 71: _t->on_actionHiDPI_scaling_triggered(); break;
        case 72: _t->on_actionHide_status_bar_triggered(); break;
        case 73: _t->on_actionHide_tool_bar_triggered(); break;
        case 74: _t->on_actionUpdate_status_bar_icons_triggered(); break;
        case 75: _t->on_actionTake_screenshot_triggered(); break;
        case 76: _t->on_actionSound_gain_triggered(); break;
        case 77: _t->on_actionPreferences_triggered(); break;
        case 78: _t->on_actionEnable_Discord_integration_triggered((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 79: _t->on_actionRenderer_options_triggered(); break;
        case 80: _t->refreshMediaMenu(); break;
        case 81: _t->showMessage_((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3]))); break;
        case 82: _t->getTitle_((*reinterpret_cast< wchar_t*(*)>(_a[1]))); break;
        case 83: _t->on_actionMCA_devices_triggered(); break;
        case 84: _t->on_actionPen_triggered(); break;
        case 85: _t->on_actionCursor_Puck_triggered(); break;
        case 86: _t->on_actionACPI_Shutdown_triggered(); break;
        case 87: _t->on_actionShow_non_primary_monitors_triggered(); break;
        case 88: _t->on_actionOpen_screenshots_folder_triggered(); break;
        case 89: _t->on_actionApply_fullscreen_stretch_mode_when_maximized_triggered((*reinterpret_cast< bool(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (MainWindow::*)(const QImage & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::paint)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)(int , int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::resizeContents)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)(int , int , int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::resizeContentsMonitor)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::statusBarMessage)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::updateStatusBarPanes)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)(int , bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::updateStatusBarActivity)) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)(int , bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::updateStatusBarEmpty)) {
                *result = 6;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::updateStatusBarTip)) {
                *result = 7;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::updateMenuResizeOptions)) {
                *result = 8;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::updateWindowRememberOption)) {
                *result = 9;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::initRendererMonitor)) {
                *result = 10;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::destroyRendererMonitor)) {
                *result = 11;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::initRendererMonitorForNonQtThread)) {
                *result = 12;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)(int );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::destroyRendererMonitorForNonQtThread)) {
                *result = 13;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::hardResetCompleted)) {
                *result = 14;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::setTitle)) {
                *result = 15;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::setFullscreen)) {
                *result = 16;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::setMouseCapture)) {
                *result = 17;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)(int , const QString & , const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::showMessageForNonQtThread)) {
                *result = 18;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)(wchar_t * );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MainWindow::getTitleForNonQtThread)) {
                *result = 19;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject MainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_meta_stringdata_MainWindow.data,
    qt_meta_data_MainWindow,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_MainWindow.stringdata0))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 90)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 90;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 90)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 90;
    }
    return _id;
}

// SIGNAL 0
void MainWindow::paint(const QImage & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void MainWindow::resizeContents(int _t1, int _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void MainWindow::resizeContentsMonitor(int _t1, int _t2, int _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void MainWindow::statusBarMessage(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void MainWindow::updateStatusBarPanes()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void MainWindow::updateStatusBarActivity(int _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void MainWindow::updateStatusBarEmpty(int _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void MainWindow::updateStatusBarTip(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}

// SIGNAL 8
void MainWindow::updateMenuResizeOptions()
{
    QMetaObject::activate(this, &staticMetaObject, 8, nullptr);
}

// SIGNAL 9
void MainWindow::updateWindowRememberOption()
{
    QMetaObject::activate(this, &staticMetaObject, 9, nullptr);
}

// SIGNAL 10
void MainWindow::initRendererMonitor(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 10, _a);
}

// SIGNAL 11
void MainWindow::destroyRendererMonitor(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 11, _a);
}

// SIGNAL 12
void MainWindow::initRendererMonitorForNonQtThread(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 12, _a);
}

// SIGNAL 13
void MainWindow::destroyRendererMonitorForNonQtThread(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 13, _a);
}

// SIGNAL 14
void MainWindow::hardResetCompleted()
{
    QMetaObject::activate(this, &staticMetaObject, 14, nullptr);
}

// SIGNAL 15
void MainWindow::setTitle(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 15, _a);
}

// SIGNAL 16
void MainWindow::setFullscreen(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 16, _a);
}

// SIGNAL 17
void MainWindow::setMouseCapture(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 17, _a);
}

// SIGNAL 18
void MainWindow::showMessageForNonQtThread(int _t1, const QString & _t2, const QString & _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 18, _a);
}

// SIGNAL 19
void MainWindow::getTitleForNonQtThread(wchar_t * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 19, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
