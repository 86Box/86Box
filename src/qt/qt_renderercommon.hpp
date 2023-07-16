#pragma once

#include <QDialog>
#include <QEvent>
#include <QImage>
#include <QRect>
#include <QWidget>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>

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

    virtual bool hasBlitFunc() { return false; }
    virtual void blit(int x, int y, int w, int h) { }

protected:
    bool eventDelegate(QEvent *event, bool &result);

    QRect    source { 0, 0, 0, 0 };
    QRect    destination;
    void dragEnterEventDelegate(QDragEnterEvent *event)
    {
        if (event->mimeData()->hasUrls() && event->mimeData()->urls().size() == 1) {
            event->setDropAction(Qt::CopyAction);
            event->acceptProposedAction();
        } else
            event->ignore();
    }
    void dragMoveEventDelegate(QDragMoveEvent *event)
    {
        if (event->mimeData()->hasUrls() && event->mimeData()->urls().size() == 1) {
            event->setDropAction(Qt::CopyAction);
            event->acceptProposedAction();
        } else
            event->ignore();
    }
    void dropEventDelegate(QDropEvent *event);

    QWidget *parentWidget { nullptr };

    std::vector<std::atomic_flag> buf_usage;
};
