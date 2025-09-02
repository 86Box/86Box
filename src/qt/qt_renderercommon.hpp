#pragma once

#include <QDialog>
#include <QEvent>
#include <QImage>
#include <QRect>
#include <QRectF>
#include <QWidget>

#include <atomic>
#include <memory>
#include <tuple>
#include <vector>

class QWidget;

class RendererCommon {
public:
    RendererCommon();

    void         onResize(int width, int height);
    virtual void finalize() { }

    virtual uint32_t getBytesPerRow() { return 2048 * 4; }

    virtual std::vector<std::tuple<uint8_t *, std::atomic_flag *>> getBuffers()
    {
        std::vector<std::tuple<uint8_t *, std::atomic_flag *>> buffers;
        return buffers;
    }

    /* Does renderer implement options dialog */
    virtual bool hasOptions() const { return false; }
    /* Returns options dialog for renderer */
    virtual QDialog *getOptions(QWidget *parent) { return nullptr; }
    /* Reloads options of renderer */
    virtual void reloadOptions() { }
    /* Make the renderer reload itself */
    virtual bool reloadRendererOption() { return false; }
    /* Should the renderer take screenshots itself? */
    virtual bool rendererTakeScreenshot() { return false; }

    int      r_monitor_index = 0;
    QRectF   destinationF = QRectF(0, 0, 1, 1); /* normalized to 0.0-1.0 range. */

protected:
    bool     eventDelegate(QEvent *event, bool &result);
    void      drawStatusBarIcons(QPainter* painter);

    QRect    source { 0, 0, 0, 0 };
    QRect    destination;
    QWidget *parentWidget { nullptr };

    double pixelRatio = 1.0;

    std::vector<std::atomic_flag> buf_usage;
};
