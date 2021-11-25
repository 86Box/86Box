#ifndef QT_MAINWINDOW_HPP
#define QT_MAINWINDOW_HPP

#include <QMainWindow>
#include <QLabel>

namespace Ui {
class MainWindow;
}

class MainWindowLabel : public QLabel
{
    Q_OBJECT
public:
    explicit MainWindowLabel(QWidget *parent = nullptr);
    ~MainWindowLabel();

    const QPoint& pos() { return pos_; }
    Qt::MouseButtons buttons() { return buttons_; }
protected:
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
private:
    QPoint pos_;
    Qt::MouseButtons buttons_;
};

class CentralWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CentralWidget(QWidget *parent = nullptr);
    ~CentralWidget();

    void setSizeHint(QSize size) { size_ = size; }
    QSize sizeHint() const override { return size_; }
private:
    QSize size_;
};

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

    void setFullscreen(bool state);
    void setMouseCapture(bool state);

private slots:
    void on_actionFullscreen_triggered();

private slots:
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
