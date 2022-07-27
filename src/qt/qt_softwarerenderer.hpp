#ifndef SOFTWARERENDERER_HPP
#define SOFTWARERENDERER_HPP

#include <QWidget>
#include <QRasterWindow>
#include <QPaintDevice>
#include <array>
#include <atomic>
#include "qt_renderercommon.hpp"

class SoftwareRenderer :
        #ifdef __HAIKU__
        public QWidget,
        #else
        public QRasterWindow,
        #endif
        public RendererCommon
{
    Q_OBJECT
public:
    explicit SoftwareRenderer(QWidget *parent = nullptr);

    void paintEvent(QPaintEvent* event) override;

    std::vector<std::tuple<uint8_t*, std::atomic_flag*>> getBuffers() override;

public slots:
    void onBlit(int buf_idx, int x, int y, int w, int h);

protected:
    std::array<std::unique_ptr<QImage>, 2> images;
    int cur_image = -1;

    void onPaint(QPaintDevice* device);
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;
};

#endif // SOFTWARERENDERER_HPP
