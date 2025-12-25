#ifndef SOFTWARERENDERER_HPP
#define SOFTWARERENDERER_HPP

#include <QWidget>
#include <QWindow>
#include <QPaintDevice>
#include <QScopedPointer>
#include <QBackingStore>
#include <array>
#include <atomic>
#include "qt_renderercommon.hpp"

class SoftwareRenderer :
    public QWidget,
    public RendererCommon {
    Q_OBJECT
public:
    explicit SoftwareRenderer(QWidget *parent = nullptr);

    void paintEvent(QPaintEvent *event) override;

#ifdef __HAIKU__
    void exposeEvent(QExposeEvent *event) override;
#else
    void exposeEvent(QExposeEvent *event);
#endif

    std::vector<std::tuple<uint8_t *, std::atomic_flag *>> getBuffers() override;

public slots:
    void onBlit(int buf_idx, int x, int y, int w, int h);

protected:
    std::array<std::unique_ptr<QImage>, 2> images;
    int                                    cur_image = -1;

    void onPaint(QPaintDevice *device);
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;

    void render();

    QScopedPointer<QBackingStore> m_backingStore;
};

#endif // SOFTWARERENDERER_HPP
