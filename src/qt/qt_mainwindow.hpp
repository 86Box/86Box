#ifndef QT_MAINWINDOW_HPP
#define QT_MAINWINDOW_HPP

#include <QMainWindow>
#include <QLabel>
#include <QEvent>
#include <QFocusEvent>

#include <memory>
#include <array>
#include <atomic>

class MediaMenu;
class RendererStack;

namespace Ui {
class MainWindow;
}

class MachineStatus;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void  showMessage(int flags, const QString &header, const QString &message);
    void  getTitle(wchar_t *title);
    void  blitToWidget(int x, int y, int w, int h, int monitor_index);
    QSize getRenderWidgetSize();
    void  setSendKeyboardInput(bool enabled);
    void  checkFullscreenHotkey();

    std::array<std::unique_ptr<RendererStack>, 8> renderers;
signals:
    void paint(const QImage &image);
    void resizeContents(int w, int h);
    void resizeContentsMonitor(int w, int h, int monitor_index);
    void statusBarMessage(const QString &msg);
    void updateStatusBarPanes();
    void updateStatusBarActivity(int tag, bool active);
    void updateStatusBarEmpty(int tag, bool empty);
    void updateStatusBarTip(int tag);
    void updateMenuResizeOptions();
    void updateWindowRememberOption();
    void initRendererMonitor(int monitor_index);
    void destroyRendererMonitor(int monitor_index);
    void initRendererMonitorForNonQtThread(int monitor_index);
    void destroyRendererMonitorForNonQtThread(int monitor_index);
    void hardResetCompleted();

    void setTitle(const QString &title);
    void setFullscreen(bool state);
    void setMouseCapture(bool state);

    void showMessageForNonQtThread(int flags, const QString &header, const QString &message, std::atomic_bool* done);
    void getTitleForNonQtThread(wchar_t *title);
public slots:
    void showSettings();
    void hardReset();
    void togglePause();
    void initRendererMonitorSlot(int monitor_index);
    void destroyRendererMonitorSlot(int monitor_index);
    void updateStatusEmptyIcons();
    void updateUiPauseState();
private slots:
    void on_actionFullscreen_triggered();
    void on_actionSettings_triggered();
    void on_actionExit_triggered();
    void on_actionAuto_pause_triggered();
    void on_actionPause_triggered();
    void on_actionCtrl_Alt_Del_triggered();
    void on_actionCtrl_Alt_Esc_triggered();
    void on_actionHard_Reset_triggered();
    void on_actionRight_CTRL_is_left_ALT_triggered();
    static void on_actionKeyboard_requires_capture_triggered();
    void on_actionResizable_window_triggered(bool checked);
    void on_actionInverted_VGA_monitor_triggered();
    void on_action0_5x_triggered();
    void on_action1x_triggered();
    void on_action1_5x_triggered();
    void on_action2x_triggered();
    void on_action3x_triggered();
    void on_action4x_triggered();
    void on_action5x_triggered();
    void on_action6x_triggered();
    void on_action7x_triggered();
    void on_action8x_triggered();
    void on_actionLinear_triggered();
    void on_actionNearest_triggered();
    void on_actionFullScreen_int_triggered();
    void on_actionFullScreen_int43_triggered();
    void on_actionFullScreen_keepRatio_triggered();
    void on_actionFullScreen_43_triggered();
    void on_actionFullScreen_stretch_triggered();
    void on_actionWhite_monitor_triggered();
    void on_actionGreen_monitor_triggered();
    void on_actionAmber_monitor_triggered();
    void on_actionRGB_Grayscale_triggered();
    void on_actionRGB_Color_triggered();
    void on_actionAverage_triggered();
    void on_actionBT709_HDTV_triggered();
    void on_actionBT601_NTSC_PAL_triggered();
    void on_actionDocumentation_triggered();
    void on_actionAbout_86Box_triggered();
    void on_actionAbout_Qt_triggered();
    void on_actionForce_4_3_display_ratio_triggered();
    void on_actionChange_contrast_for_monochrome_display_triggered();
    void on_actionCGA_PCjr_Tandy_EGA_S_VGA_overscan_triggered();
    void on_actionRemember_size_and_position_triggered();
    void on_actionSpecify_dimensions_triggered();
    void on_actionHiDPI_scaling_triggered();
    void on_actionHide_status_bar_triggered();
    void on_actionHide_tool_bar_triggered();
    void on_actionUpdate_status_bar_icons_triggered();
    void on_actionTake_screenshot_triggered();
    void on_actionSound_gain_triggered();
    void on_actionPreferences_triggered();
    void on_actionEnable_Discord_integration_triggered(bool checked);
    void on_actionRenderer_options_triggered();

    void refreshMediaMenu();
    void showMessage_(int flags, const QString &header, const QString &message, std::atomic_bool* done = nullptr);
    void getTitle_(wchar_t *title);

    void on_actionMCA_devices_triggered();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool eventFilter(QObject *receiver, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void on_actionPen_triggered();

private slots:
    void on_actionCursor_Puck_triggered();

    void on_actionACPI_Shutdown_triggered();

private slots:
    void on_actionShow_non_primary_monitors_triggered();

    void on_actionOpen_screenshots_folder_triggered();

    void on_actionApply_fullscreen_stretch_mode_when_maximized_triggered(bool checked);

private:
    Ui::MainWindow                *ui;
    std::unique_ptr<MachineStatus> status;
    std::shared_ptr<MediaMenu>     mm;

    void     processKeyboardInput(bool down, uint32_t keycode);
#ifdef Q_OS_MACOS
    uint32_t last_modifiers = 0;
    void     processMacKeyboardInput(bool down, const QKeyEvent *event);
#endif

    /* If main window should send keyboard input */
    bool send_keyboard_input = true;
    bool shownonce           = false;
    bool resizableonce       = false;
    bool vnc_enabled         = false;

    /* Full screen ON and OFF signals */
    bool fs_on_signal        = false;
    bool fs_off_signal       = false;

    friend class SpecifyDimensions;
    friend class ProgSettings;
    friend class RendererCommon;
    friend class RendererStack; // For UI variable access by non-primary renderer windows.
    friend class WindowsRawInputFilter; // Needed to reload renderers on style sheet changes.
};

#endif // QT_MAINWINDOW_HPP
