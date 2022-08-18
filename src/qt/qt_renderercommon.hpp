#pragma once

#include <QDialog>
#include <QEvent>
#include <QImage>
#include <QRect>
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

    virtual std::vector<std::tuple<uint8_t *, std::atomic_flag *>> getBuffers() { std::vector<std::tuple<uint8_t*, std::atomic_flag*>> buffers; return buffers; }

    /* Does renderer implement options dialog */
    virtual bool hasOptions() const { return false; }
    /* Returns options dialog for renderer */
    virtual QDialog *getOptions(QWidget *parent) { return nullptr; }
    /* Reloads options of renderer */
    virtual void reloadOptions() {}

    virtual bool hasBlitFunc() { return false; }
    virtual void blit(int x, int y, int w, int h) {}

protected:
    bool eventDelegate(QEvent *event, bool &result);

    QRect    source{0, 0, 0, 0}, destination;
    QWidget *parentWidget { nullptr };

    std::vector<std::atomic_flag> buf_usage;
};
