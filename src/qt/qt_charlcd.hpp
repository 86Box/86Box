#ifndef QT_CHARLCD_HPP
#define QT_CHARLCD_HPP

#include <QElapsedTimer>
#include <QWidget>

#include <cstdint>

extern "C" {
#include <86box/char_lcd.h>
}

class QAction;
class QTimer;

/* The character LCD's glass, in a window of its own. It shows itself on a
   board with an LCD header, or once something writes to the LCD. */
class CharLcdWindow : public QWidget {
    Q_OBJECT

public:
    explicit CharLcdWindow(QWidget *parent, QAction *toggle);

protected:
    void paintEvent(QPaintEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void  poll();
    void  onToggle(bool checked);
    void  fitToGlass();
    QSize glassSize(double scale) const;

    QAction      *toggle_;
    QTimer       *timer_;
    QElapsedTimer clock_;
    qint64        lastTick_ = 0;

    char_lcd_frame_t frame_ {};
    bool             haveFrame_  = false;
    uint32_t         generation_ = 0;
    bool             dismissed_  = false;
    int              cols_       = 0;
    int              rows_       = 0;

    /* How dark (or, on a negative panel, how bright) each dot has got: the
       liquid crystal takes a while to turn. */
    float level_[CHAR_LCD_MAX_ROWS][CHAR_LCD_MAX_COLS][8][5] {};
};

#endif
