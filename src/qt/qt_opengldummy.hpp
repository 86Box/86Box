#ifndef QT_OPENGLDUMMY_HPP
#define QT_OPENGLDUMMY_HPP

#include <QResizeEvent>
#include <QWidget>
#include <QWindow>

#include <atomic>
#include <tuple>
#include <vector>

#include "qt_renderercommon.hpp"

/* Dummy implementation of OpenGLRenderer so macOS builds. */
class OpenGLRenderer : public QWindow, public RendererCommon {
    Q_OBJECT
public:
    OpenGLRenderer(QWidget *parent = nullptr) { emit errorInitializing(); }
    virtual std::vector<std::tuple<uint8_t *, std::atomic_flag *>> getBuffers() { return std::vector<std::tuple<uint8_t *, std::atomic_flag *>>(); }

signals:
    void initialized();
    void errorInitializing();

public slots:
    void onBlit(int buf_idx, int x, int y, int w, int h) { }

protected:
    void exposeEvent(QExposeEvent *event) override
    {
        if (!done)
            emit errorInitializing();
        done = true;
    }

private:
    bool done = false;
};

#endif