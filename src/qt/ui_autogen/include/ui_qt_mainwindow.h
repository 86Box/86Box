/********************************************************************************
** Form generated from reading UI file 'qt_mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 6.2.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_QT_MAINWINDOW_H
#define UI_QT_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtGui/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include "qt_rendererstack.hpp"

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QAction *actionKeyboard_requires_capture;
    QAction *actionRight_CTRL_is_left_ALT;
    QAction *actionHard_Reset;
    QAction *actionCtrl_Alt_Del;
    QAction *actionCtrl_Alt_Esc;
    QAction *actionPause;
    QAction *actionExit;
    QAction *actionSettings;
    QAction *actionFullscreen;
    QAction *actionSoftware_Renderer;
    QAction *actionHardware_Renderer_OpenGL;
    QAction *actionHardware_Renderer_OpenGL_ES;
    QAction *actionHide_status_bar;
    QAction *actionResizable_window;
    QAction *actionRemember_size_and_position;
    QAction *actionSpecify_dimensions;
    QAction *actionForce_4_3_display_ratio;
    QAction *actionHiDPI_scaling;
    QAction *actionCGA_PCjr_Tandy_EGA_S_VGA_overscan;
    QAction *actionChange_contrast_for_monochrome_display;
    QAction *action0_5x;
    QAction *action1x;
    QAction *action1_5x;
    QAction *action2x;
    QAction *actionNearest;
    QAction *actionLinear;
    QAction *actionFullScreen_stretch;
    QAction *actionFullScreen_43;
    QAction *actionFullScreen_keepRatio;
    QAction *actionFullScreen_int;
    QAction *actionInverted_VGA_monitor;
    QAction *actionRGB_Color;
    QAction *actionRGB_Grayscale;
    QAction *actionAmber_monitor;
    QAction *actionGreen_monitor;
    QAction *actionWhite_monitor;
    QAction *actionBT601_NTSC_PAL;
    QAction *actionBT709_HDTV;
    QAction *actionAverage;
    QAction *actionAbout_Qt;
    QAction *actionAbout_86Box;
    QAction *actionDocumentation;
    QAction *actionUpdate_status_bar_icons;
    QAction *actionTake_screenshot;
    QAction *actionSound_gain;
    QAction *actionOpenGL_3_0_Core;
    QAction *actionPreferences;
    QWidget *centralwidget;
    QVBoxLayout *verticalLayout;
    RendererStack *stackedWidget;
    QMenuBar *menubar;
    QMenu *menuAction;
    QMenu *menuTools;
    QMenu *menuView;
    QMenu *menuRenderer;
    QMenu *menuWindow_scale_factor;
    QMenu *menuFilter_method;
    QMenu *menuFullscreen_stretch_mode;
    QMenu *menuEGA_S_VGA_settings;
    QMenu *menuVGA_screen_type;
    QMenu *menuGrayscale_conversion_type;
    QMenu *menuMedia;
    QMenu *menuAbout;
    QStatusBar *statusbar;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName(QString::fromUtf8("MainWindow"));
        MainWindow->resize(724, 427);
        actionKeyboard_requires_capture = new QAction(MainWindow);
        actionKeyboard_requires_capture->setObjectName(QString::fromUtf8("actionKeyboard_requires_capture"));
        actionKeyboard_requires_capture->setCheckable(true);
        actionRight_CTRL_is_left_ALT = new QAction(MainWindow);
        actionRight_CTRL_is_left_ALT->setObjectName(QString::fromUtf8("actionRight_CTRL_is_left_ALT"));
        actionRight_CTRL_is_left_ALT->setCheckable(true);
        actionHard_Reset = new QAction(MainWindow);
        actionHard_Reset->setObjectName(QString::fromUtf8("actionHard_Reset"));
        actionCtrl_Alt_Del = new QAction(MainWindow);
        actionCtrl_Alt_Del->setObjectName(QString::fromUtf8("actionCtrl_Alt_Del"));
        actionCtrl_Alt_Del->setShortcutVisibleInContextMenu(false);
        actionCtrl_Alt_Esc = new QAction(MainWindow);
        actionCtrl_Alt_Esc->setObjectName(QString::fromUtf8("actionCtrl_Alt_Esc"));
        actionPause = new QAction(MainWindow);
        actionPause->setObjectName(QString::fromUtf8("actionPause"));
        actionPause->setCheckable(true);
        actionExit = new QAction(MainWindow);
        actionExit->setObjectName(QString::fromUtf8("actionExit"));
        actionSettings = new QAction(MainWindow);
        actionSettings->setObjectName(QString::fromUtf8("actionSettings"));
        actionSettings->setMenuRole(QAction::PreferencesRole);
        actionFullscreen = new QAction(MainWindow);
        actionFullscreen->setObjectName(QString::fromUtf8("actionFullscreen"));
        actionFullscreen->setShortcutVisibleInContextMenu(false);
        actionSoftware_Renderer = new QAction(MainWindow);
        actionSoftware_Renderer->setObjectName(QString::fromUtf8("actionSoftware_Renderer"));
        actionSoftware_Renderer->setCheckable(true);
        actionHardware_Renderer_OpenGL = new QAction(MainWindow);
        actionHardware_Renderer_OpenGL->setObjectName(QString::fromUtf8("actionHardware_Renderer_OpenGL"));
        actionHardware_Renderer_OpenGL->setCheckable(true);
        actionHardware_Renderer_OpenGL_ES = new QAction(MainWindow);
        actionHardware_Renderer_OpenGL_ES->setObjectName(QString::fromUtf8("actionHardware_Renderer_OpenGL_ES"));
        actionHardware_Renderer_OpenGL_ES->setCheckable(true);
        actionHide_status_bar = new QAction(MainWindow);
        actionHide_status_bar->setObjectName(QString::fromUtf8("actionHide_status_bar"));
        actionHide_status_bar->setCheckable(true);
        actionResizable_window = new QAction(MainWindow);
        actionResizable_window->setObjectName(QString::fromUtf8("actionResizable_window"));
        actionResizable_window->setCheckable(true);
        actionRemember_size_and_position = new QAction(MainWindow);
        actionRemember_size_and_position->setObjectName(QString::fromUtf8("actionRemember_size_and_position"));
        actionRemember_size_and_position->setCheckable(true);
        actionSpecify_dimensions = new QAction(MainWindow);
        actionSpecify_dimensions->setObjectName(QString::fromUtf8("actionSpecify_dimensions"));
        actionForce_4_3_display_ratio = new QAction(MainWindow);
        actionForce_4_3_display_ratio->setObjectName(QString::fromUtf8("actionForce_4_3_display_ratio"));
        actionForce_4_3_display_ratio->setCheckable(true);
        actionHiDPI_scaling = new QAction(MainWindow);
        actionHiDPI_scaling->setObjectName(QString::fromUtf8("actionHiDPI_scaling"));
        actionHiDPI_scaling->setCheckable(true);
        actionCGA_PCjr_Tandy_EGA_S_VGA_overscan = new QAction(MainWindow);
        actionCGA_PCjr_Tandy_EGA_S_VGA_overscan->setObjectName(QString::fromUtf8("actionCGA_PCjr_Tandy_EGA_S_VGA_overscan"));
        actionCGA_PCjr_Tandy_EGA_S_VGA_overscan->setCheckable(true);
        actionChange_contrast_for_monochrome_display = new QAction(MainWindow);
        actionChange_contrast_for_monochrome_display->setObjectName(QString::fromUtf8("actionChange_contrast_for_monochrome_display"));
        actionChange_contrast_for_monochrome_display->setCheckable(true);
        action0_5x = new QAction(MainWindow);
        action0_5x->setObjectName(QString::fromUtf8("action0_5x"));
        action0_5x->setCheckable(true);
        action1x = new QAction(MainWindow);
        action1x->setObjectName(QString::fromUtf8("action1x"));
        action1x->setCheckable(true);
        action1_5x = new QAction(MainWindow);
        action1_5x->setObjectName(QString::fromUtf8("action1_5x"));
        action1_5x->setCheckable(true);
        action2x = new QAction(MainWindow);
        action2x->setObjectName(QString::fromUtf8("action2x"));
        action2x->setCheckable(true);
        actionNearest = new QAction(MainWindow);
        actionNearest->setObjectName(QString::fromUtf8("actionNearest"));
        actionNearest->setCheckable(true);
        actionLinear = new QAction(MainWindow);
        actionLinear->setObjectName(QString::fromUtf8("actionLinear"));
        actionLinear->setCheckable(true);
        actionFullScreen_stretch = new QAction(MainWindow);
        actionFullScreen_stretch->setObjectName(QString::fromUtf8("actionFullScreen_stretch"));
        actionFullScreen_stretch->setCheckable(true);
        actionFullScreen_43 = new QAction(MainWindow);
        actionFullScreen_43->setObjectName(QString::fromUtf8("actionFullScreen_43"));
        actionFullScreen_43->setCheckable(true);
        actionFullScreen_keepRatio = new QAction(MainWindow);
        actionFullScreen_keepRatio->setObjectName(QString::fromUtf8("actionFullScreen_keepRatio"));
        actionFullScreen_keepRatio->setCheckable(true);
        actionFullScreen_int = new QAction(MainWindow);
        actionFullScreen_int->setObjectName(QString::fromUtf8("actionFullScreen_int"));
        actionFullScreen_int->setCheckable(true);
        actionInverted_VGA_monitor = new QAction(MainWindow);
        actionInverted_VGA_monitor->setObjectName(QString::fromUtf8("actionInverted_VGA_monitor"));
        actionInverted_VGA_monitor->setCheckable(true);
        actionRGB_Color = new QAction(MainWindow);
        actionRGB_Color->setObjectName(QString::fromUtf8("actionRGB_Color"));
        actionRGB_Color->setCheckable(true);
        actionRGB_Grayscale = new QAction(MainWindow);
        actionRGB_Grayscale->setObjectName(QString::fromUtf8("actionRGB_Grayscale"));
        actionRGB_Grayscale->setCheckable(true);
        actionAmber_monitor = new QAction(MainWindow);
        actionAmber_monitor->setObjectName(QString::fromUtf8("actionAmber_monitor"));
        actionAmber_monitor->setCheckable(true);
        actionGreen_monitor = new QAction(MainWindow);
        actionGreen_monitor->setObjectName(QString::fromUtf8("actionGreen_monitor"));
        actionGreen_monitor->setCheckable(true);
        actionWhite_monitor = new QAction(MainWindow);
        actionWhite_monitor->setObjectName(QString::fromUtf8("actionWhite_monitor"));
        actionWhite_monitor->setCheckable(true);
        actionBT601_NTSC_PAL = new QAction(MainWindow);
        actionBT601_NTSC_PAL->setObjectName(QString::fromUtf8("actionBT601_NTSC_PAL"));
        actionBT601_NTSC_PAL->setCheckable(true);
        actionBT709_HDTV = new QAction(MainWindow);
        actionBT709_HDTV->setObjectName(QString::fromUtf8("actionBT709_HDTV"));
        actionBT709_HDTV->setCheckable(true);
        actionAverage = new QAction(MainWindow);
        actionAverage->setObjectName(QString::fromUtf8("actionAverage"));
        actionAverage->setCheckable(true);
        actionAbout_Qt = new QAction(MainWindow);
        actionAbout_Qt->setObjectName(QString::fromUtf8("actionAbout_Qt"));
        actionAbout_Qt->setVisible(false);
        actionAbout_Qt->setMenuRole(QAction::AboutQtRole);
        actionAbout_86Box = new QAction(MainWindow);
        actionAbout_86Box->setObjectName(QString::fromUtf8("actionAbout_86Box"));
        actionAbout_86Box->setMenuRole(QAction::AboutRole);
        actionDocumentation = new QAction(MainWindow);
        actionDocumentation->setObjectName(QString::fromUtf8("actionDocumentation"));
        actionUpdate_status_bar_icons = new QAction(MainWindow);
        actionUpdate_status_bar_icons->setObjectName(QString::fromUtf8("actionUpdate_status_bar_icons"));
        actionUpdate_status_bar_icons->setCheckable(true);
        actionTake_screenshot = new QAction(MainWindow);
        actionTake_screenshot->setObjectName(QString::fromUtf8("actionTake_screenshot"));
        actionTake_screenshot->setShortcutVisibleInContextMenu(false);
        actionSound_gain = new QAction(MainWindow);
        actionSound_gain->setObjectName(QString::fromUtf8("actionSound_gain"));
        actionOpenGL_3_0_Core = new QAction(MainWindow);
        actionOpenGL_3_0_Core->setObjectName(QString::fromUtf8("actionOpenGL_3_0_Core"));
        actionOpenGL_3_0_Core->setCheckable(true);
        actionPreferences = new QAction(MainWindow);
        actionPreferences->setObjectName(QString::fromUtf8("actionPreferences"));
        actionPreferences->setMenuRole(QAction::NoRole);
        centralwidget = new QWidget(MainWindow);
        centralwidget->setObjectName(QString::fromUtf8("centralwidget"));
        QSizePolicy sizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(centralwidget->sizePolicy().hasHeightForWidth());
        centralwidget->setSizePolicy(sizePolicy);
        verticalLayout = new QVBoxLayout(centralwidget);
        verticalLayout->setSpacing(0);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        verticalLayout->setContentsMargins(0, 0, 0, 0);
        stackedWidget = new RendererStack(centralwidget);
        stackedWidget->setObjectName(QString::fromUtf8("stackedWidget"));

        verticalLayout->addWidget(stackedWidget);

        MainWindow->setCentralWidget(centralwidget);
        menubar = new QMenuBar(MainWindow);
        menubar->setObjectName(QString::fromUtf8("menubar"));
        menubar->setGeometry(QRect(0, 0, 724, 20));
        menuAction = new QMenu(menubar);
        menuAction->setObjectName(QString::fromUtf8("menuAction"));
        menuTools = new QMenu(menubar);
        menuTools->setObjectName(QString::fromUtf8("menuTools"));
        menuView = new QMenu(menubar);
        menuView->setObjectName(QString::fromUtf8("menuView"));
        menuRenderer = new QMenu(menuView);
        menuRenderer->setObjectName(QString::fromUtf8("menuRenderer"));
        menuWindow_scale_factor = new QMenu(menuView);
        menuWindow_scale_factor->setObjectName(QString::fromUtf8("menuWindow_scale_factor"));
        menuFilter_method = new QMenu(menuView);
        menuFilter_method->setObjectName(QString::fromUtf8("menuFilter_method"));
        menuFullscreen_stretch_mode = new QMenu(menuView);
        menuFullscreen_stretch_mode->setObjectName(QString::fromUtf8("menuFullscreen_stretch_mode"));
        menuEGA_S_VGA_settings = new QMenu(menuView);
        menuEGA_S_VGA_settings->setObjectName(QString::fromUtf8("menuEGA_S_VGA_settings"));
        menuVGA_screen_type = new QMenu(menuEGA_S_VGA_settings);
        menuVGA_screen_type->setObjectName(QString::fromUtf8("menuVGA_screen_type"));
        menuGrayscale_conversion_type = new QMenu(menuEGA_S_VGA_settings);
        menuGrayscale_conversion_type->setObjectName(QString::fromUtf8("menuGrayscale_conversion_type"));
        menuMedia = new QMenu(menubar);
        menuMedia->setObjectName(QString::fromUtf8("menuMedia"));
        menuAbout = new QMenu(menubar);
        menuAbout->setObjectName(QString::fromUtf8("menuAbout"));
        MainWindow->setMenuBar(menubar);
        statusbar = new QStatusBar(MainWindow);
        statusbar->setObjectName(QString::fromUtf8("statusbar"));
        MainWindow->setStatusBar(statusbar);

        menubar->addAction(menuAction->menuAction());
        menubar->addAction(menuView->menuAction());
        menubar->addAction(menuMedia->menuAction());
        menubar->addAction(menuTools->menuAction());
        menubar->addAction(menuAbout->menuAction());
        menuAction->addAction(actionKeyboard_requires_capture);
        menuAction->addAction(actionRight_CTRL_is_left_ALT);
        menuAction->addSeparator();
        menuAction->addAction(actionHard_Reset);
        menuAction->addAction(actionCtrl_Alt_Del);
        menuAction->addSeparator();
        menuAction->addAction(actionCtrl_Alt_Esc);
        menuAction->addSeparator();
        menuAction->addAction(actionPause);
        menuAction->addSeparator();
        menuAction->addAction(actionExit);
        menuTools->addAction(actionSettings);
        menuTools->addAction(actionUpdate_status_bar_icons);
        menuTools->addSeparator();
        menuTools->addAction(actionTake_screenshot);
        menuTools->addAction(actionSound_gain);
        menuTools->addSeparator();
        menuTools->addAction(actionPreferences);
        menuView->addAction(actionHide_status_bar);
        menuView->addSeparator();
        menuView->addAction(actionResizable_window);
        menuView->addAction(actionRemember_size_and_position);
        menuView->addSeparator();
        menuView->addAction(menuRenderer->menuAction());
        menuView->addAction(actionSpecify_dimensions);
        menuView->addAction(actionForce_4_3_display_ratio);
        menuView->addAction(menuWindow_scale_factor->menuAction());
        menuView->addAction(menuFilter_method->menuAction());
        menuView->addAction(actionHiDPI_scaling);
        menuView->addSeparator();
        menuView->addAction(actionFullscreen);
        menuView->addAction(menuFullscreen_stretch_mode->menuAction());
        menuView->addAction(menuEGA_S_VGA_settings->menuAction());
        menuView->addSeparator();
        menuView->addAction(actionCGA_PCjr_Tandy_EGA_S_VGA_overscan);
        menuView->addAction(actionChange_contrast_for_monochrome_display);
        menuRenderer->addAction(actionSoftware_Renderer);
        menuRenderer->addAction(actionHardware_Renderer_OpenGL);
        menuRenderer->addAction(actionHardware_Renderer_OpenGL_ES);
        menuRenderer->addAction(actionOpenGL_3_0_Core);
        menuWindow_scale_factor->addAction(action0_5x);
        menuWindow_scale_factor->addAction(action1x);
        menuWindow_scale_factor->addAction(action1_5x);
        menuWindow_scale_factor->addAction(action2x);
        menuFilter_method->addAction(actionNearest);
        menuFilter_method->addAction(actionLinear);
        menuFullscreen_stretch_mode->addAction(actionFullScreen_stretch);
        menuFullscreen_stretch_mode->addAction(actionFullScreen_43);
        menuFullscreen_stretch_mode->addAction(actionFullScreen_keepRatio);
        menuFullscreen_stretch_mode->addAction(actionFullScreen_int);
        menuEGA_S_VGA_settings->addAction(actionInverted_VGA_monitor);
        menuEGA_S_VGA_settings->addAction(menuVGA_screen_type->menuAction());
        menuEGA_S_VGA_settings->addAction(menuGrayscale_conversion_type->menuAction());
        menuVGA_screen_type->addAction(actionRGB_Color);
        menuVGA_screen_type->addAction(actionRGB_Grayscale);
        menuVGA_screen_type->addAction(actionAmber_monitor);
        menuVGA_screen_type->addAction(actionGreen_monitor);
        menuVGA_screen_type->addAction(actionWhite_monitor);
        menuGrayscale_conversion_type->addAction(actionBT601_NTSC_PAL);
        menuGrayscale_conversion_type->addAction(actionBT709_HDTV);
        menuGrayscale_conversion_type->addAction(actionAverage);
        menuAbout->addAction(actionDocumentation);
        menuAbout->addAction(actionAbout_86Box);
        menuAbout->addAction(actionAbout_Qt);

        retranslateUi(MainWindow);

        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QCoreApplication::translate("MainWindow", "86Box", nullptr));
        actionKeyboard_requires_capture->setText(QCoreApplication::translate("MainWindow", "&Keyboard requires capture", nullptr));
        actionRight_CTRL_is_left_ALT->setText(QCoreApplication::translate("MainWindow", "&Right CTRL is left ALT", nullptr));
        actionHard_Reset->setText(QCoreApplication::translate("MainWindow", "&Hard Reset...", nullptr));
        actionCtrl_Alt_Del->setText(QCoreApplication::translate("MainWindow", "&Ctrl+Alt+Del", nullptr));
#if QT_CONFIG(tooltip)
        actionCtrl_Alt_Del->setToolTip(QCoreApplication::translate("MainWindow", "Ctrl+Alt+Del", nullptr));
#endif // QT_CONFIG(tooltip)
#if QT_CONFIG(shortcut)
        actionCtrl_Alt_Del->setShortcut(QCoreApplication::translate("MainWindow", "Ctrl+F12", nullptr));
#endif // QT_CONFIG(shortcut)
        actionCtrl_Alt_Esc->setText(QCoreApplication::translate("MainWindow", "Ctrl+Alt+&Esc", nullptr));
        actionPause->setText(QCoreApplication::translate("MainWindow", "&Pause", nullptr));
        actionExit->setText(QCoreApplication::translate("MainWindow", "Exit", nullptr));
        actionSettings->setText(QCoreApplication::translate("MainWindow", "&Settings...", nullptr));
        actionFullscreen->setText(QCoreApplication::translate("MainWindow", "&Fullscreen", nullptr));
#if QT_CONFIG(shortcut)
        actionFullscreen->setShortcut(QCoreApplication::translate("MainWindow", "Ctrl+Alt+PgUp", nullptr));
#endif // QT_CONFIG(shortcut)
        actionSoftware_Renderer->setText(QCoreApplication::translate("MainWindow", "&Qt (Software)", nullptr));
        actionHardware_Renderer_OpenGL->setText(QCoreApplication::translate("MainWindow", "Qt (&OpenGL)", nullptr));
        actionHardware_Renderer_OpenGL_ES->setText(QCoreApplication::translate("MainWindow", "Qt (OpenGL &ES)", nullptr));
        actionHide_status_bar->setText(QCoreApplication::translate("MainWindow", "&Hide status bar", nullptr));
        actionResizable_window->setText(QCoreApplication::translate("MainWindow", "&Resizeable window", nullptr));
        actionRemember_size_and_position->setText(QCoreApplication::translate("MainWindow", "R&emember size && position", nullptr));
        actionSpecify_dimensions->setText(QCoreApplication::translate("MainWindow", "Specify dimensions...", nullptr));
        actionForce_4_3_display_ratio->setText(QCoreApplication::translate("MainWindow", "F&orce 4:3 display ratio", nullptr));
        actionHiDPI_scaling->setText(QCoreApplication::translate("MainWindow", "Hi&DPI scaling", nullptr));
        actionCGA_PCjr_Tandy_EGA_S_VGA_overscan->setText(QCoreApplication::translate("MainWindow", "CGA/PCjr/Tandy/E&GA/(S)VGA overscan", nullptr));
        actionChange_contrast_for_monochrome_display->setText(QCoreApplication::translate("MainWindow", "Change contrast for &monochrome display", nullptr));
        action0_5x->setText(QCoreApplication::translate("MainWindow", "&0.5x", nullptr));
        action1x->setText(QCoreApplication::translate("MainWindow", "&1x", nullptr));
        action1_5x->setText(QCoreApplication::translate("MainWindow", "1.&5x", nullptr));
        action2x->setText(QCoreApplication::translate("MainWindow", "&2x", nullptr));
        actionNearest->setText(QCoreApplication::translate("MainWindow", "&Nearest", nullptr));
        actionLinear->setText(QCoreApplication::translate("MainWindow", "&Linear", nullptr));
        actionFullScreen_stretch->setText(QCoreApplication::translate("MainWindow", "&Full screen stretch", nullptr));
        actionFullScreen_43->setText(QCoreApplication::translate("MainWindow", "&4:3", nullptr));
        actionFullScreen_keepRatio->setText(QCoreApplication::translate("MainWindow", "&Square pixels (Keep ratio)", nullptr));
        actionFullScreen_int->setText(QCoreApplication::translate("MainWindow", "&Integer scale", nullptr));
        actionInverted_VGA_monitor->setText(QCoreApplication::translate("MainWindow", "&Inverted VGA monitor", nullptr));
        actionRGB_Color->setText(QCoreApplication::translate("MainWindow", "RGB &Color", nullptr));
        actionRGB_Grayscale->setText(QCoreApplication::translate("MainWindow", "&RGB Grayscale", nullptr));
        actionAmber_monitor->setText(QCoreApplication::translate("MainWindow", "&Amber monitor", nullptr));
        actionGreen_monitor->setText(QCoreApplication::translate("MainWindow", "&Green monitor", nullptr));
        actionWhite_monitor->setText(QCoreApplication::translate("MainWindow", "&White monitor", nullptr));
        actionBT601_NTSC_PAL->setText(QCoreApplication::translate("MainWindow", "BT&601 (NTSC/PAL)", nullptr));
        actionBT709_HDTV->setText(QCoreApplication::translate("MainWindow", "BT&709 (HDTV)", nullptr));
        actionAverage->setText(QCoreApplication::translate("MainWindow", "&Average", nullptr));
        actionAbout_Qt->setText(QCoreApplication::translate("MainWindow", "About Qt", nullptr));
        actionAbout_86Box->setText(QCoreApplication::translate("MainWindow", "&About 86Box...", nullptr));
        actionDocumentation->setText(QCoreApplication::translate("MainWindow", "&Documentation...", nullptr));
        actionUpdate_status_bar_icons->setText(QCoreApplication::translate("MainWindow", "&Update status bar icons", nullptr));
        actionTake_screenshot->setText(QCoreApplication::translate("MainWindow", "Take s&creenshot", nullptr));
#if QT_CONFIG(shortcut)
        actionTake_screenshot->setShortcut(QCoreApplication::translate("MainWindow", "Ctrl+F11", nullptr));
#endif // QT_CONFIG(shortcut)
        actionSound_gain->setText(QCoreApplication::translate("MainWindow", "Sound &gain...", nullptr));
        actionOpenGL_3_0_Core->setText(QCoreApplication::translate("MainWindow", "Open&GL (3.0 Core)", nullptr));
        actionPreferences->setText(QCoreApplication::translate("MainWindow", "&Preferences...", nullptr));
        menuAction->setTitle(QCoreApplication::translate("MainWindow", "&Action", nullptr));
        menuTools->setTitle(QCoreApplication::translate("MainWindow", "&Tools", nullptr));
        menuView->setTitle(QCoreApplication::translate("MainWindow", "&View", nullptr));
        menuRenderer->setTitle(QCoreApplication::translate("MainWindow", "Re&nderer", nullptr));
        menuWindow_scale_factor->setTitle(QCoreApplication::translate("MainWindow", "&Window scale factor", nullptr));
        menuFilter_method->setTitle(QCoreApplication::translate("MainWindow", "Filter method", nullptr));
        menuFullscreen_stretch_mode->setTitle(QCoreApplication::translate("MainWindow", "Fullscreen &stretch mode", nullptr));
        menuEGA_S_VGA_settings->setTitle(QCoreApplication::translate("MainWindow", "E&GA/(S)VGA settings", nullptr));
        menuVGA_screen_type->setTitle(QCoreApplication::translate("MainWindow", "VGA screen &type", nullptr));
        menuGrayscale_conversion_type->setTitle(QCoreApplication::translate("MainWindow", "Grayscale &conversion type", nullptr));
        menuMedia->setTitle(QCoreApplication::translate("MainWindow", "&Media", nullptr));
        menuAbout->setTitle(QCoreApplication::translate("MainWindow", "&Help", nullptr));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_QT_MAINWINDOW_H
