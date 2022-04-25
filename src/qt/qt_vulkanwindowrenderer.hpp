#ifndef VULKANWINDOWRENDERER_HPP
#define VULKANWINDOWRENDERER_HPP

#include <QVulkanWindow>

#if QT_CONFIG(vulkan)
#include "qt_renderercommon.hpp"
#include "qt_vulkanrenderer.hpp"

class VulkanRenderer2;

class VulkanWindowRenderer : public QVulkanWindow, public RendererCommon
{
    Q_OBJECT
public:
    VulkanWindowRenderer(QWidget* parent);
public slots:
    void onBlit(int buf_idx, int x, int y, int w, int h);
signals:
    void rendererInitialized();
    void errorInitializing();
protected:
    virtual std::vector<std::tuple<uint8_t *, std::atomic_flag *>> getBuffers() override;
    void resizeEvent(QResizeEvent*) override;
    bool event(QEvent*) override;
    uint32_t getBytesPerRow() override;
private:
    QVulkanInstance instance;

    QVulkanWindowRenderer* createRenderer() override;

    friend class VulkanRendererEmu;
    friend class VulkanRenderer2;

    VulkanRenderer2* renderer;
};
#endif

#endif // VULKANWINDOWRENDERER_HPP
