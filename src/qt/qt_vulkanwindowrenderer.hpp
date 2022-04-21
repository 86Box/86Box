#ifndef VULKANWINDOWRENDERER_HPP
#define VULKANWINDOWRENDERER_HPP

#include <QVulkanWindow>

#include "qt_renderercommon.hpp"

class VulkanRenderer;

class VulkanWindowRenderer : public QVulkanWindow, public RendererCommon
{
    Q_OBJECT
public:
    VulkanWindowRenderer(QWidget* parent);
public slots:
    void onBlit(int buf_idx, int x, int y, int w, int h);
signals:
    void rendererInitialized();
protected:
    virtual std::vector<std::tuple<uint8_t *, std::atomic_flag *>> getBuffers() override;
    void resizeEvent(QResizeEvent*) override;
    bool event(QEvent*) override;
    uint32_t getBytesPerRow() override;
private:
    QVulkanInstance instance;

    QVulkanWindowRenderer* createRenderer() override;

    friend class VulkanRenderer;

    VulkanRenderer* renderer;
};

#endif // VULKANWINDOWRENDERER_HPP
