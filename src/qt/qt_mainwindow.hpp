#ifndef QT_MAINWINDOW_HPP
#define QT_MAINWINDOW_HPP

#include <QMainWindow>
#include <QLabel>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
signals:
    void paint(const QImage& image);
    void resizeContents(int w, int h);
    void pollMouse();
    void updateStatusBarPanes();
    void updateStatusBarActivity(int tag, bool active);
    void updateStatusBarEmpty(int tag, bool empty);

    void setFullscreen(bool state);
    void setMouseCapture(bool state);
    void showMessage(const QString& header, const QString& message);

private slots:
    void on_actionFullscreen_triggered();
    void on_actionSettings_triggered();
    void on_actionExit_triggered();
    void on_actionPause_triggered();
    void on_actionCtrl_Alt_Del_triggered();
    void on_actionCtrl_Alt_Esc_triggered();
    void on_actionHard_Reset_triggered();
    void on_actionRight_CTRL_is_left_ALT_triggered();
    void on_actionKeyboard_requires_capture_triggered();

private:
    struct DeltaPos {
        int x = 0;
        int y = 0;
        int z = 0;
    };
    Ui::MainWindow *ui;
    DeltaPos mouseDelta;
    QWindow* sdl_wrapped_window;
    QWidget* sdl_wrapped_widget;
    QTimer* sdl_timer;
};

#endif // QT_MAINWINDOW_HPP
