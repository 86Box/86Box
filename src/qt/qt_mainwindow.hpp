#ifndef QT_MAINWINDOW_HPP
#define QT_MAINWINDOW_HPP

#include <QMainWindow>
#include <QLabel>
#include <QEvent>

#include <memory>

class MediaMenu;

namespace Ui {
class MainWindow;
}

class MachineStatus;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void showMessage(const QString& header, const QString& message);
    void setTitle(const wchar_t* title);
    void getTitle(wchar_t* title);
    void blitToWidget(int x, int y, int w, int h);
signals:
    void paint(const QImage& image);
    void resizeContents(int w, int h);
    void pollMouse();
    void statusBarMessage(const QString& msg);
    void updateStatusBarPanes();
    void updateStatusBarActivity(int tag, bool active);
    void updateStatusBarEmpty(int tag, bool empty);

    void setFullscreen(bool state);
    void setMouseCapture(bool state);

    void showMessageForNonQtThread(const QString& header, const QString& message);
    void setTitleForNonQtThread(const wchar_t* title);
    void getTitleForNonQtThread(wchar_t* title);
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

    void refreshMediaMenu();
    void showMessage_(const QString& header, const QString& message);
    void setTitle_(const wchar_t* title);
    void getTitle_(wchar_t* title);
protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
private:
    Ui::MainWindow *ui;
    std::unique_ptr<MachineStatus> status;
    std::shared_ptr<MediaMenu> mm;
};

#endif // QT_MAINWINDOW_HPP
