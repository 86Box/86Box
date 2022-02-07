#pragma once

#include <QRect>
#include <QImage>
#include <QEvent>

#include <vector>
#include <tuple>
#include <atomic>

class QWidget;

class RendererCommon
{
public:
    RendererCommon();

    void onResize(int width, int height);
    virtual std::vector<std::tuple<uint8_t*, std::atomic_flag*>> getBuffers() = 0;
protected:
    bool eventDelegate(QEvent* event, bool& result);

    QRect source, destination;
    QWidget* parentWidget{nullptr};

    std::vector<std::atomic_flag> buf_usage;
};
