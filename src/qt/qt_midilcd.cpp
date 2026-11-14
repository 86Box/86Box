/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Window showing the LCD of the current MIDI output device.
 *
 * Authors: Andreas Zdziarstek, <https://github.com/gmcn42>
 *
 *          Copyright 2026 Andreas Zdziarstek.
 */
#include "qt_midilcd.hpp"

#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPainter>

#include <cstring>

extern "C" {
#include <86box/86box.h>
#include <86box/ini.h>
#include <86box/config.h>
#include <86box/midi_lcd.h>
}

#define LCD_SECTION "MIDI LCD"

MidiLcdWindow::MidiLcdWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
    , image(MIDI_LCD_MT32_WIDTH, MIDI_LCD_MT32_HEIGHT, QImage::Format_RGB32)
    , glass(MIDI_LCD_MT32_WIDTH, MIDI_LCD_MT32_HEIGHT, QImage::Format_RGB32)
{
    midi_lcd_mt32_render_glass(reinterpret_cast<uint32_t *>(glass.bits()), glass.bytesPerLine() / 4, 1);

    setWindowTitle(tr("MIDI LCD"));
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(MIDI_LCD_MT32_WIDTH / 4, MIDI_LCD_MT32_HEIGHT / 4);
    resize(MIDI_LCD_MT32_WIDTH / 2, MIDI_LCD_MT32_HEIGHT / 2);

    if (window_remember) {
        const int w = config_get_int(LCD_SECTION, "window_w", 0);
        const int h = config_get_int(LCD_SECTION, "window_h", 0);
        if ((w > 0) && (h > 0))
            resize(w, h);
        /* Without a saved position, leave the placement to the window system. */
        if (!QApplication::platformName().contains("wayland") && config_get_string(LCD_SECTION, "window_x", NULL)
            && config_get_string(LCD_SECTION, "window_y", NULL))
            move(config_get_int(LCD_SECTION, "window_x", 0), config_get_int(LCD_SECTION, "window_y", 0));
    }

    onTop = config_get_int(LCD_SECTION, "on_top", 0);
    setWindowFlag(Qt::WindowStaysOnTopHint, onTop);

    /* The device updates the LCD from its render thread: look at it once
       per frame and redraw only when it changed. */
    timer.setInterval(16);
    connect(&timer, &QTimer::timeout, this, &MidiLcdWindow::poll);
    poll();
}

void
MidiLcdWindow::saveSettings()
{
    config_set_int(LCD_SECTION, "visible", isVisible() ? 1 : 0);
    config_set_int(LCD_SECTION, "on_top", onTop ? 1 : 0);
    if (window_remember) {
        config_set_int(LCD_SECTION, "window_w", width());
        config_set_int(LCD_SECTION, "window_h", height());
        if (!QApplication::platformName().contains("wayland")) {
            config_set_int(LCD_SECTION, "window_x", x());
            config_set_int(LCD_SECTION, "window_y", y());
        }
    }
}

void
MidiLcdWindow::poll()
{
    midi_lcd_t lcd;

    midi_lcd_get(&lcd);

    /* The LED changes with every MIDI message; it isn't drawn, so only the
       type and the text count. */
    if ((lcd.type == type) && !memcmp(lcd.text, text, sizeof(text)))
        return;
    type = lcd.type;
    memcpy(text, lcd.text, sizeof(text));

    uint32_t *bits   = reinterpret_cast<uint32_t *>(image.bits());
    const int stride = image.bytesPerLine() / 4;

    if (type == MIDI_LCD_MT32) {
        memcpy(bits, glass.constBits(), image.sizeInBytes());
        midi_lcd_mt32_render_text(bits, stride, text);
    } else
        midi_lcd_mt32_render_glass(bits, stride, 0);
    update();
}

void
MidiLcdWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    QSize    size = image.size().scaled(this->size(), Qt::KeepAspectRatio);
    QRect    target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);

    painter.fillRect(rect(), Qt::black);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(target, image);
}

void
MidiLcdWindow::showEvent(QShowEvent *event)
{
    poll();
    timer.start();
    if (!event->spontaneous())
        emit visibilityChanged(true);
}

/* Spontaneous events come from the window system, e.g. when it minimizes
   the window: the window is still open as far as the menu is concerned. */
void
MidiLcdWindow::hideEvent(QHideEvent *event)
{
    timer.stop();
    if (!event->spontaneous())
        emit visibilityChanged(false);
}

void
MidiLcdWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu    menu(this);
    QAction *top = menu.addAction(tr("Always on &top"));

    top->setCheckable(true);
    top->setChecked(onTop);
    connect(top, &QAction::toggled, this, &MidiLcdWindow::setOnTop);
    menu.addSeparator();
    menu.addAction(tr("&Close"), this, &QWidget::close);
    menu.exec(event->globalPos());
}

void
MidiLcdWindow::setOnTop(bool enable)
{
    onTop = enable;
    /* Changing the flag hides the window, show it again. */
    setWindowFlag(Qt::WindowStaysOnTopHint, onTop);
    show();
}
