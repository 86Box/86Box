#pragma once

#include <QRect>
#include <QImage>

class QWidget;

class RendererCommon
{
public:
    RendererCommon();

    void onResize(int width, int height);
protected:
    void onPaint(QPaintDevice* device);

    QImage image{QSize(2048, 2048), QImage::Format_RGB32};
    QRect source, destination;
};
