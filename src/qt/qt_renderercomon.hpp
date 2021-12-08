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

    QImage image;
    QRect source, destination;
};
