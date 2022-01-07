#ifndef SOFTWARERENDERER_HPP
#define SOFTWARERENDERER_HPP

#include <QWidget>
#include <QRasterWindow>
#include <atomic>
#include "qt_renderercomon.hpp"

class SoftwareRenderer : public QRasterWindow, public RendererCommon
{
    Q_OBJECT
public:
    explicit SoftwareRenderer(QWidget *parent = nullptr);

    void paintEvent(QPaintEvent *event) override;
public slots:
    void onBlit(const std::unique_ptr<uint8_t>* img, int, int, int, int, std::atomic_flag* in_use);

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;
};

#endif // SOFTWARERENDERER_HPP
