#ifndef QT_MIDILCD_HPP
#define QT_MIDILCD_HPP

#include <QImage>
#include <QTimer>
#include <QWidget>

/* Window showing the LCD of the current MIDI output device. It stays open
   across device changes and shows the LCD switched off while the device has
   none. */
class MidiLcdWindow : public QWidget {
    Q_OBJECT

public:
    explicit MidiLcdWindow(QWidget *parent = nullptr);

    /* Stores the window's state in the machine config. */
    void saveSettings();

signals:
    void visibilityChanged(bool visible);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void poll();
    void setOnTop(bool onTop);

    QTimer timer;
    QImage image;
    QImage glass;
    char   text[21] = {};
    int    type     = -1;
    bool   onTop    = false;
};

#endif // QT_MIDILCD_HPP
