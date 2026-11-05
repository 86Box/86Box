/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The character LCD's window.
 *
 *          The glass is drawn to the proportions of a real module's, in
 *          millimetres: 0.55 x 0.60 dots at a 0.60 x 0.65 pitch, in cells
 *          3.55 mm apart along a row and 5.75 mm between rows. On top of
 *          the dots themselves it has what makes one look like the real
 *          thing: the unlit dots faintly visible, the shadow each dark dot
 *          casts on the reflector behind it, the glow of a negative panel's
 *          lit dots, an uneven backlight, a glare off the glass, the dark
 *          rows of 1-line mode on 2-line glass, and the liquid crystal's
 *          slow response, which smears anything that moves quickly.
 *
 * Authors: Nathan Lewis, <git@nrlewis.dev>
 *
 *          Copyright 2026 Nathan Lewis.
 */
#include "qt_charlcd.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {

/* The glass's geometry, in millimetres. */
constexpr double DOT_W      = 0.55;
constexpr double DOT_H      = 0.60;
constexpr double DOT_PITCH_X = 0.60;
constexpr double DOT_PITCH_Y = 0.65;
constexpr double CELL_PITCH_X = 3.55;
constexpr double CELL_PITCH_Y = 5.75;
constexpr double CELL_W     = 4 * DOT_PITCH_X + DOT_W;
constexpr double CELL_H     = 7 * DOT_PITCH_Y + DOT_H;
constexpr double MARGIN     = 2.6; /* Viewing area around the dots. */
constexpr double BEZEL      = 3.2; /* Frame around the viewing area. */

constexpr double PX_PER_MM  = 8.0;
constexpr int    MAX_WIDTH  = 960;

/* Half of the blink cycle, at the controller's typical 270 kHz. */
constexpr qint64 BLINK_HALF_MS = 410;

/* The liquid crystal's response: time constants for turning and relaxing, in seconds. */
constexpr float RISE_TAU = 0.045f;
constexpr float FALL_TAU = 0.140f;

struct Panel {
    QColor backlight;  /* The backlight through the glass. */
    QColor backlightHi;
    QColor ink;        /* A fully turned dot. */
    bool   negative;   /* Lit dots are light on a dark ground. */
    float  ghost;      /* How much of a dot an unselected one shows. */
    float  shadow;     /* How dark a dot's shadow is. */
};

const Panel &
panelFor(int panel)
{
    static const Panel panels[] = {
        /* Yellow-green, positive STN with an LED array. */
        { QColor(0x9c, 0xbb, 0x2c), QColor(0xc4, 0xdc, 0x4a), QColor(0x1a, 0x26, 0x08), false, 0.075f, 0.22f },
        /* White on blue, negative STN. */
        { QColor(0x10, 0x2a, 0xc0), QColor(0x22, 0x46, 0xe8), QColor(0xee, 0xf4, 0xff), true, 0.060f, 0.00f },
        /* Grey reflective FSTN, no backlight. */
        { QColor(0xa6, 0xaa, 0x98), QColor(0xbc, 0xbf, 0xae), QColor(0x22, 0x24, 0x22), false, 0.060f, 0.30f },
        /* Amber on black. */
        { QColor(0x12, 0x09, 0x03), QColor(0x1c, 0x0f, 0x05), QColor(0xff, 0xaa, 0x28), true, 0.045f, 0.00f },
    };

    return panels[((panel >= 0) && (panel < 4)) ? panel : 0];
}

double
glassWidthMm(int cols)
{
    return 2 * (BEZEL + MARGIN) + (cols - 1) * CELL_PITCH_X + CELL_W;
}

double
glassHeightMm(int rows)
{
    return 2 * (BEZEL + MARGIN) + (rows - 1) * CELL_PITCH_Y + CELL_H;
}

}

CharLcdWindow::CharLcdWindow(QWidget *parent, QAction *toggle)
    : QWidget(parent, Qt::Tool)
    , toggle_(toggle)
{
    setWindowTitle(tr("Character LCD"));
    setAttribute(Qt::WA_OpaquePaintEvent);

    toggle_->setCheckable(true);
    toggle_->setEnabled(false);
    connect(toggle_, &QAction::triggered, this, &CharLcdWindow::onToggle);

    clock_.start();

    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
    connect(timer_, &QTimer::timeout, this, &CharLcdWindow::poll);
    timer_->start(33);
}

QSize
CharLcdWindow::glassSize(double scale) const
{
    return QSize(qRound(glassWidthMm(cols_) * scale), qRound(glassHeightMm(rows_) * scale));
}

void
CharLcdWindow::fitToGlass()
{
    double scale = std::min(PX_PER_MM, MAX_WIDTH / glassWidthMm(cols_));

    resize(glassSize(scale));
    setMinimumSize(glassSize(scale / 3));
}

void
CharLcdWindow::onToggle(bool checked)
{
    dismissed_ = !checked;
    if (checked && haveFrame_) {
        show();
        raise();
    } else
        hide();
}

void
CharLcdWindow::closeEvent(QCloseEvent *event)
{
    dismissed_ = true;
    toggle_->setChecked(false);
    event->accept();
}

void
CharLcdWindow::poll()
{
    const qint64 now     = clock_.elapsed();
    const bool   blinkOn = ((now / BLINK_HALF_MS) & 1) == 0;
    const float  dt      = std::clamp((now - lastTick_) / 1000.0f, 0.0f, 0.25f);

    lastTick_ = now;

    toggle_->setEnabled(char_lcd_present());

    if (!char_lcd_shown() || !char_lcd_get_frame(&frame_, blinkOn)) {
        haveFrame_ = false;
        if (isVisible())
            hide();
        toggle_->setChecked(false);
        return;
    }
    haveFrame_ = true;

    /* Powered up afresh: blank glass, and shown again even if it was closed. */
    if (frame_.generation != generation_) {
        generation_ = frame_.generation;
        dismissed_  = false;
        memset(level_, 0, sizeof(level_));
        if ((frame_.cols != cols_) || (frame_.rows != rows_)) {
            cols_ = frame_.cols;
            rows_ = frame_.rows;
            fitToGlass();
        }
    }

    if (!dismissed_ && !isVisible()) {
        show();
        toggle_->setChecked(true);
    }

    if (!isVisible())
        return;

    /* Each dot heads for its state at the liquid crystal's pace. */
    const float rise = 1.0f - std::exp(-dt / RISE_TAU);
    const float fall = 1.0f - std::exp(-dt / FALL_TAU);

    for (int r = 0; r < rows_; r++) {
        for (int c = 0; c < cols_; c++) {
            for (int y = 0; y < 8; y++) {
                const uint8_t bits = frame_.dots[r][c][y];

                for (int x = 0; x < 5; x++) {
                    float      &l      = level_[r][c][y][x];
                    const float target = (bits & (0x10 >> x)) ? 1.0f : 0.0f;

                    l += (target - l) * ((target > l) ? rise : fall);
                }
            }
        }
    }

    update();
}

void
CharLcdWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.fillRect(rect(), QColor(0x0c, 0x0c, 0x0c));

    if (!haveFrame_ || !cols_ || !rows_)
        return;

    const Panel &panel = panelFor(frame_.panel);

    /* Fit the glass to the window, keeping its proportions. */
    const double wMm   = glassWidthMm(cols_);
    const double hMm   = glassHeightMm(rows_);
    const double scale = std::min(width() / wMm, height() / hMm);
    p.translate((width() - wMm * scale) / 2, (height() - hMm * scale) / 2);
    p.scale(scale, scale);

    /* The bezel. */
    const QRectF outer(0, 0, wMm, hMm);
    QLinearGradient bezel(0, 0, 0, hMm);
    bezel.setColorAt(0.0, QColor(0x2a, 0x2a, 0x2a));
    bezel.setColorAt(1.0, QColor(0x14, 0x14, 0x14));
    p.setPen(Qt::NoPen);
    p.setBrush(bezel);
    p.drawRoundedRect(outer, 1.2, 1.2);

    /* The viewing area, lit from behind, a little brighter in the middle. */
    const QRectF view = outer.adjusted(BEZEL, BEZEL, -BEZEL, -BEZEL);
    QRadialGradient light(view.center(), std::max(view.width(), view.height()) * 0.7);
    light.setColorAt(0.0, panel.backlightHi);
    light.setColorAt(1.0, panel.backlight);
    p.setBrush(light);
    p.drawRect(view);

    /* The inner edge of the bezel shades the glass. */
    QLinearGradient edgeTop(0, view.top(), 0, view.top() + 0.9);
    edgeTop.setColorAt(0.0, QColor(0, 0, 0, 110));
    edgeTop.setColorAt(1.0, QColor(0, 0, 0, 0));
    p.fillRect(QRectF(view.left(), view.top(), view.width(), 0.9), edgeTop);
    QLinearGradient edgeLeft(view.left(), 0, view.left() + 0.6, 0);
    edgeLeft.setColorAt(0.0, QColor(0, 0, 0, 80));
    edgeLeft.setColorAt(1.0, QColor(0, 0, 0, 0));
    p.fillRect(QRectF(view.left(), view.top(), 0.6, view.height()), edgeLeft);

    p.save();
    p.setClipRect(view);

    const double ox      = view.left() + MARGIN;
    const double oy      = view.top() + MARGIN;
    const double radius  = 0.07;
    const QColor shadowC = QColor(0, 0, 0);
    const QColor glowC   = panel.ink;

    for (int r = 0; r < rows_; r++) {
        const int   drive  = frame_.row_drive[r];
        /* Unselected dots: a whisper of the grid normally; in 1-line mode on
           2-line glass, the 1/8 duty bias darkens them into blocks. */
        const float ghost  = (drive == CHAR_LCD_ROW_OVERDRIVEN) ? 0.55f : panel.ghost;

        for (int c = 0; c < cols_; c++) {
            const double cx = ox + c * CELL_PITCH_X;
            const double cy = oy + r * CELL_PITCH_Y;

            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 5; x++) {
                    const float l = (drive == CHAR_LCD_ROW_UNDRIVEN) ? 0.0f : level_[r][c][y][x];
                    float       v;

                    if (drive == CHAR_LCD_ROW_UNDRIVEN)
                        v = panel.ghost * 0.6f;
                    else
                        v = ghost + (1.0f - ghost) * l;

                    const QRectF dot(cx + x * DOT_PITCH_X, cy + y * DOT_PITCH_Y, DOT_W, DOT_H);

                    if (!panel.negative && (panel.shadow > 0.0f) && (l > 0.02f)) {
                        /* The dot's shadow on the reflector, a little below and to the right. */
                        QColor s = shadowC;
                        s.setAlphaF(qreal(panel.shadow) * l);
                        p.setBrush(s);
                        p.drawRoundedRect(dot.translated(0.13, 0.15), radius, radius);
                    }

                    if (panel.negative && (l > 0.02f)) {
                        /* Light spilling around a lit dot. */
                        QColor g = glowC;
                        g.setAlphaF(0.16 * l);
                        p.setBrush(g);
                        p.drawRoundedRect(dot.adjusted(-0.12, -0.12, 0.12, 0.12), 0.2, 0.2);
                    }

                    QColor ink = panel.ink;
                    ink.setAlphaF(qreal(std::clamp(v, 0.0f, 1.0f)) * (panel.negative ? 0.97 : 0.92));
                    p.setBrush(ink);
                    p.drawRoundedRect(dot, radius, radius);
                }
            }
        }
    }

    /* The glare off the glass. */
    QLinearGradient glare(view.topLeft(), QPointF(view.left() + view.width() * 0.55, view.bottom()));
    glare.setColorAt(0.00, QColor(255, 255, 255, 34));
    glare.setColorAt(0.45, QColor(255, 255, 255, 10));
    glare.setColorAt(0.46, QColor(255, 255, 255, 0));
    glare.setColorAt(1.00, QColor(255, 255, 255, 0));
    p.fillRect(view, glare);

    p.restore();

    /* A thin highlight where the glass meets the bezel. */
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 28), 0.15));
    p.drawRect(view.adjusted(-0.1, -0.1, 0.1, 0.1));
}
