#ifndef SOFTWARERENDERER_HPP
#define SOFTWARERENDERER_HPP

#include <QWidget>
#include "qt_renderercomon.hpp"

class SoftwareRenderer : public QWidget, public RendererCommon
{
    Q_OBJECT
public:
    explicit SoftwareRenderer(QWidget *parent = nullptr);

    void paintEvent(QPaintEvent *event) override;
public slots:
    void onBlit(const QImage& img, int, int, int, int);

protected:
    void resizeEvent(QResizeEvent *event) override;
};

#endif // SOFTWARERENDERER_HPP
