#ifndef VULKANWINDOWRENDERER_HPP
#define VULKANWINDOWRENDERER_HPP

#include <QWindow>
#include <QVulkanWindow>
#include <QTimer>

#include <vector>
#include <tuple>

#if QT_CONFIG(vulkan)
#    pragma clang diagnostic ignored "-Wnullability-completeness"
#    include "vk_mem_alloc.h"
#    include "qt_renderercommon.hpp"
#    include <vulkan/vulkan.h>
#    include "imgui_impl_vulkan.h"
#    define LIBRA_RUNTIME_VULKAN 1
#    include "librashader.h"

class VulkanShaderChain
{
public:
    libra_vk_filter_chain_t chain;
    VkImage next_image_chain;
    VmaAllocation next_image_alloc;
};

class VulkanWindowRenderer : public QWindow, public RendererCommon {
    Q_OBJECT
public:
    VulkanWindowRenderer(QWidget *parent);
    ~VulkanWindowRenderer();
    void finalize() override final;

    bool     hasOptions() const override { return true; }
    QDialog *getOptions(QWidget *parent) override;
    bool     reloadRendererOption() override { return true; }
public slots:
    void onBlit(int buf_idx, int x, int y, int w, int h);
signals:
    void rendererInitialized();
    void errorInitializing();

protected:
    virtual std::vector<std::tuple<uint8_t *, std::atomic_flag *>> getBuffers() override;
    void                                                           resizeEvent(QResizeEvent *) override;
    bool                                                           event(QEvent *) override;
    uint32_t                                                       getBytesPerRow() override;

    void exposeEvent(QExposeEvent *event) override;

private:
    QVulkanInstance instance;

    uint8_t* mappedPtr = nullptr;
    uint32_t imagePitch = 0;

    bool isInitialized = 0, isFinalized = 0;
    void initialize();

    void cleanupSwapchain();
    void recreateSwapchain();

    void cleanupShaderSrcImages();
    void recreateShaderSrcImages();

    int present_queue = -1, gfx_queue = -1;

    uint32_t swapchain_image_index = 0;
    uint32_t current_frame = 0;
    uint32_t current_frame_shader = 0;
    
    // Renderer stuff.
    VkSurfaceKHR window_surface = nullptr;
    VkPhysicalDevice phys_device = nullptr;
    VkDevice logi_device = nullptr;
    VkSwapchainKHR dev_swapchain = nullptr;
    VkCommandPool commandPool = nullptr;
    VkQueue gfx_queue_o = nullptr;

    std::vector<VkImage> swapchainImageScreenshots;
    std::vector<std::pair<void*, uint32_t>> swapchainImageScreenshotMappedPtrs;
    std::vector<VmaAllocation> swapchainImageScreenshotAllocations;

    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<bool> swapchainImageTransitioned;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkSemaphore> presentSemaphores;
    std::vector<VkFence> presentFences;
    std::vector<VkCommandBuffer> cmdBuffers;

    std::vector<VkImage> shaderSrcImages;
    std::vector<VmaAllocation> shaderSrcImageAllocations;
    std::vector<std::vector<VulkanShaderChain>> shaderFilterChains;

    // Stuff to use.
    VkImage src_image = nullptr;
    VkImageView src_image_view = nullptr;

    int cur_video_filter_method = -1;

    QVulkanDeviceFunctions* m_devFuncs;

    void updateOptions();

    QTimer *renderTimer = nullptr;
    QTimer *osdRenderTimer = nullptr;
    bool was_osd_visible = false;

    VmaAllocator allocator;
    VmaAllocation img_allocation;

    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;

    PFN_vkCreateSwapchainKHR fn_vkCreateSwapchainKHR = nullptr;
    PFN_vkDestroySwapchainKHR fn_vkDestroySwapchainKHR = nullptr;
    PFN_vkGetSwapchainImagesKHR fn_vkGetSwapchainImagesKHR = nullptr;
    PFN_vkAcquireNextImageKHR fn_vkAcquireNextImageKHR = nullptr;
    PFN_vkQueuePresentKHR fn_vkQueuePresentKHR = nullptr;

    PFN_vkCmdBeginRenderingKHR fn_vkCmdBeginRendering = nullptr;
    PFN_vkCmdEndRenderingKHR fn_vkCmdEndRendering = nullptr;

    VkExtent2D curExtent;

    ImGui_ImplVulkan_InitInfo init_info{};
    
    VkFormat colorAttachmentFormat = VK_FORMAT_B8G8R8A8_UNORM;

    bool imageLayoutTransitioned = false;

private slots:
    void render();
};

class vulkan_init_error : public std::runtime_error {
public:
    vulkan_init_error(const QString &what)
        : std::runtime_error(what.toStdString())
    {
    }
};

#endif // QT_CONFIG(vulkan)

#endif // VULKANWINDOWRENDERER_HPP
