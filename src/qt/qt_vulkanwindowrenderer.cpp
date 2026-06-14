/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Vulkan renderer, adapted from various examples, with librashader support.
 *
 * Authors: Teemu Korhonen
 *          Sascha Willems
 *          Cacodemon345
 *
 *          Copyright 2016-2025 Sascha Willems
 *          Copyright 2021 Teemu Korhonen
 *          Copyright 2026 Cacodemon345.
 */
#define VMA_IMPLEMENTATION           1
#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "qt_vulkanwindowrenderer.hpp"
#include "qt_vulkanshadermanagerdialog.hpp"

#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QWindow>

#if QT_CONFIG(vulkan)
#    include <QVulkanWindowRenderer>
#    include <QVulkanDeviceFunctions>
#    include <array>
#    include <stdexcept>
#    include <algorithm>
#    include <limits>
#    include <cmath>

#    include "qt_mainwindow.hpp"
#    include "qt_osd.hpp"
#    include "qt-slangp.hpp"

extern "C" {
#    include <86box/86box.h>
#    include <86box/path.h>
#    include <86box/plat.h>
#    include <86box/ui.h>
#    include <86box/video.h>

char vk_shader_file[20][512] = {};
}

extern MainWindow *main_window;

VulkanWindowRenderer::VulkanWindowRenderer(QWidget *parent)
    : QWindow((QWindow *) NULL)
    , renderTimer(new QTimer(this))
    , osdRenderTimer(new QTimer(this))
{
    connect(renderTimer, &QTimer::timeout, this, [this]() { this->render(); });
    connect(osdRenderTimer, &QTimer::timeout, this, [this]() {
        if (video_framerate == -1 && dopause && qt_osd_is_visible())
            this->render();

        if (video_framerate == -1 && !qt_osd_is_visible() && was_osd_visible)
            this->render();

        was_osd_visible = qt_osd_is_visible();
    });
    parentWidget = parent;
    instance.setApiVersion(QVersionNumber(1, 0));
    if (instance.supportedExtensions().contains("VK_KHR_get_physical_device_properties2")) {
        auto list = instance.extensions();
        list.push_back("VK_KHR_get_physical_device_properties2");
        instance.setExtensions(list);
    }
    instance.create();
    setSurfaceType(QSurface::VulkanSurface);
    setVulkanInstance(&instance);
    buf_usage = std::vector<std::atomic_flag>(1);
    buf_usage[0].clear();
}

void
VulkanWindowRenderer::cleanupShaderSrcImages()
{
    if (isFinalized || !isInitialized)
        return;
    m_devFuncs->vkDeviceWaitIdle(logi_device);
    for (int i = 0; i < shaderFilterChains.size(); i++) {
        for (int j = 0; j < shaderFilterChains[i].size(); j++) {
            if (j != (shaderFilterChains[i].size() - 1)) {
                vmaDestroyImage(allocator, shaderFilterChains[i][j].next_image_chain, shaderFilterChains[i][j].next_image_alloc);
            }
            shaderFilterChains[i][j].chain = nullptr;
        }
        vmaDestroyImage(allocator, shaderSrcImages[i], shaderSrcImageAllocations[i]);
    }
    shaderSrcImageTransitioned.clear();
    shaderFilterChains.clear();
    shaderSrcImages.clear();
    shaderSrcImageAllocations.clear();
}

QDialog *
VulkanWindowRenderer::getOptions(QWidget *parent)
{
    return new VulkanShaderManagerDialog(parent);
}

void
VulkanWindowRenderer::recreateShaderSrcImages()
{
    if (isFinalized || !isInitialized)
        return;
    m_devFuncs->vkDeviceWaitIdle(logi_device);
    cleanupShaderSrcImages();

    shaderSrcImageTransitioned.resize(swapchainImageViews.size());
    shaderSrcImages.resize(swapchainImageViews.size());
    shaderSrcImageAllocations.resize(swapchainImageViews.size());
    shaderFilterChains.resize(swapchainImageViews.size());

    for (int i = 0; i < swapchainImageViews.size(); i++) {
        VkImageCreateInfo img_info = { };
        img_info.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img_info.imageType         = VK_IMAGE_TYPE_2D;
        img_info.extent.width      = destination.width();
        img_info.extent.height     = destination.height();
        img_info.extent.depth      = 1;
        img_info.mipLevels         = 1;
        img_info.arrayLayers       = 1;
        img_info.format            = VK_FORMAT_B8G8R8A8_UNORM;
        img_info.tiling            = VK_IMAGE_TILING_OPTIMAL;
        img_info.usage             = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        img_info.samples           = VK_SAMPLE_COUNT_1_BIT;
        img_info.initialLayout     = VK_IMAGE_LAYOUT_PREINITIALIZED;
        img_info.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;
        img_info.flags             = 0;

        VmaAllocationCreateInfo allocInfo { };
        VmaAllocationInfo       allocInfo2 { };
        allocInfo.pUserData = allocInfo.pool = nullptr;
        allocInfo.requiredFlags = allocInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        allocInfo.usage                                    = VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        allocInfo.flags                                    = 0;

        if (vmaCreateImage(allocator, &img_info, &allocInfo, &shaderSrcImages[i], &shaderSrcImageAllocations[i], &allocInfo2) != VK_SUCCESS) {
            return;
        }

        img_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        for (int j = 0; j < shaderLibraFilterChains.size(); j++) {
            VulkanShaderChain vk_shader_chain { };
            vk_shader_chain.chain = shaderLibraFilterChains[j];
            if (vk_shader_chain.chain) {
                vk_shader_chain.next_image_alloc = nullptr;
                vk_shader_chain.next_image_chain = swapchainImages[i];
            } else {
                continue;
            }
            if (shaderFilterChains[i].size()) {
                auto &last_chain            = shaderFilterChains[i][shaderFilterChains[i].size() - 1];
                last_chain.next_image_chain = nullptr;

                allocInfo           = { };
                allocInfo2          = { };
                allocInfo.pUserData = allocInfo.pool = nullptr;
                allocInfo.requiredFlags = allocInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                allocInfo.usage                                    = VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
                allocInfo.flags                                    = 0;
                if (vmaCreateImage(allocator, &img_info, &allocInfo, &last_chain.next_image_chain, &last_chain.next_image_alloc, &allocInfo2) != VK_SUCCESS) {
                    return;
                }
            }
            shaderFilterChains[i].push_back(vk_shader_chain);
        }
    }
}

void
VulkanWindowRenderer::cleanupSwapchain()
{
    if (isFinalized || !isInitialized)
        return;
    m_devFuncs->vkDeviceWaitIdle(logi_device);
    cleanupShaderSrcImages();

    for (int i = 0; i < swapchainImageViews.size(); i++) {
        m_devFuncs->vkDestroyImageView(logi_device, swapchainImageViews[i], nullptr);
        m_devFuncs->vkDestroySemaphore(logi_device, renderFinishedSemaphores[i], nullptr);
        m_devFuncs->vkDestroySemaphore(logi_device, presentSemaphores[i], nullptr);
        m_devFuncs->vkDestroyFence(logi_device, presentFences[i], nullptr);

        vmaDestroyImage(allocator, swapchainImageScreenshots[i], swapchainImageScreenshotAllocations[i]);
    }
    if (cmdBuffers.size()) {
        m_devFuncs->vkDestroyCommandPool(logi_device, this->commandPool, nullptr);
    }
    swapchainImageScreenshots.clear();
    swapchainImageScreenshotAllocations.clear();
    swapchainImageScreenshotMappedPtrs.clear();

    swapchainImageViews.clear();
    swapchainImageTransitioned.clear();
    renderFinishedSemaphores.clear();
    presentSemaphores.clear();
    cmdBuffers.clear();

    if (this->dev_swapchain)
        fn_vkDestroySwapchainKHR(logi_device, this->dev_swapchain, nullptr);
    this->dev_swapchain = nullptr;
}

void
VulkanWindowRenderer::recreateSwapchain()
{
    if (isFinalized || !isInitialized)
        return;
    VkSurfaceCapabilitiesKHR surfaceCaps;
    if (fn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR) {
        fn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_device, instance.surfaceForWindow(this), &surfaceCaps);
    } else {
        throw vulkan_init_error("Failed to get surface capabilities");
    }

    cleanupSwapchain();

    curExtent = surfaceCaps.currentExtent;

    VkSwapchainCreateInfoKHR swapchain_creation = { };
    swapchain_creation.sType                    = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_creation.surface                  = instance.surfaceForWindow(this);
    swapchain_creation.compositeAlpha           = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_creation.presentMode              = video_vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
    swapchain_creation.imageFormat              = VK_FORMAT_B8G8R8A8_UNORM;
    swapchain_creation.imageColorSpace          = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchain_creation.imageArrayLayers         = 1;
    swapchain_creation.imageUsage               = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchain_creation.minImageCount            = std::clamp(surfaceCaps.minImageCount + 1u, surfaceCaps.minImageCount, surfaceCaps.maxImageCount ? surfaceCaps.maxImageCount : std::numeric_limits<uint32_t>::max());
    swapchain_creation.imageExtent              = surfaceCaps.currentExtent;
    swapchain_creation.preTransform             = surfaceCaps.currentTransform;
    swapchain_creation.clipped                  = VK_TRUE;
    swapchain_creation.imageSharingMode         = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_creation.oldSwapchain             = NULL;
    auto res                                    = fn_vkCreateSwapchainKHR(logi_device, &swapchain_creation, nullptr, &dev_swapchain);
    if (res != VK_SUCCESS) {
        throw vulkan_init_error("Failed to create swapchain");
    }

    uint32_t swapchainImagesCount = 0;
    fn_vkGetSwapchainImagesKHR(logi_device, dev_swapchain, &swapchainImagesCount, nullptr);

    swapchainImageScreenshots.resize(swapchainImagesCount);
    swapchainImageScreenshotAllocations.resize(swapchainImagesCount);
    swapchainImageScreenshotMappedPtrs.resize(swapchainImagesCount);

    this->swapchainImages.resize(swapchainImagesCount);
    this->swapchainImageViews.resize(swapchainImagesCount);
    this->swapchainImageTransitioned.resize(swapchainImagesCount);
    this->renderFinishedSemaphores.resize(swapchainImagesCount);
    this->presentSemaphores.resize(swapchainImagesCount);
    this->presentFences.resize(swapchainImagesCount);
    this->cmdBuffers.resize(swapchainImagesCount);
    fn_vkGetSwapchainImagesKHR(logi_device, dev_swapchain, &swapchainImagesCount, swapchainImages.data());

    VkCommandPoolCreateInfo cmdpollcreate { };
    cmdpollcreate.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdpollcreate.pNext            = nullptr;
    cmdpollcreate.queueFamilyIndex = gfx_queue;
    cmdpollcreate.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if ((res = m_devFuncs->vkCreateCommandPool(logi_device, &cmdpollcreate, nullptr, &commandPool)) != VK_SUCCESS) {
        throw vulkan_init_error("Could not create command pool. Switch to another renderer.");
    }

    VkCommandBufferAllocateInfo cmdbufferallocate { };
    cmdbufferallocate.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdbufferallocate.commandPool        = commandPool;
    cmdbufferallocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdbufferallocate.commandBufferCount = swapchainImagesCount;

    m_devFuncs->vkAllocateCommandBuffers(logi_device, &cmdbufferallocate, cmdBuffers.data());

    qt_osd_vulkan_set_min_image(cmdBuffers.size());
    init_info.MinImageCount = cmdBuffers.size();

    for (int i = 0; i < swapchainImagesCount; i++) {
        VkSemaphoreCreateInfo semaphore_create = { };
        semaphore_create.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphore_create.pNext                 = nullptr;
        semaphore_create.flags                 = 0;

        VkFenceCreateInfo fence_create = { };
        fence_create.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_create.pNext             = nullptr;
        fence_create.flags             = VK_FENCE_CREATE_SIGNALED_BIT;

        m_devFuncs->vkCreateSemaphore(logi_device, &semaphore_create, nullptr, &renderFinishedSemaphores[i]);
        m_devFuncs->vkCreateSemaphore(logi_device, &semaphore_create, nullptr, &presentSemaphores[i]);
        m_devFuncs->vkCreateFence(logi_device, &fence_create, nullptr, &presentFences[i]);

        VkImageViewCreateInfo image_view_info           = { };
        image_view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        image_view_info.format                          = VK_FORMAT_B8G8R8A8_UNORM;
        image_view_info.image                           = swapchainImages[i];
        image_view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        image_view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        image_view_info.subresourceRange.baseMipLevel   = 0;
        image_view_info.subresourceRange.levelCount     = 1;
        image_view_info.subresourceRange.baseArrayLayer = 0;
        image_view_info.subresourceRange.layerCount     = 1;
        image_view_info.components.r                    = VK_COMPONENT_SWIZZLE_R;
        image_view_info.components.g                    = VK_COMPONENT_SWIZZLE_G;
        image_view_info.components.b                    = VK_COMPONENT_SWIZZLE_B;
        image_view_info.components.a                    = VK_COMPONENT_SWIZZLE_A;
        m_devFuncs->vkCreateImageView(logi_device, &image_view_info, nullptr, &swapchainImageViews[i]);

        // Create screenshot images.
        {
            VkImageCreateInfo img_info = { };
            img_info.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            img_info.imageType         = VK_IMAGE_TYPE_2D;
            img_info.extent.width      = curExtent.width;
            img_info.extent.height     = curExtent.height;
            img_info.extent.depth      = 1;
            img_info.mipLevels         = 1;
            img_info.arrayLayers       = 1;
            img_info.format            = VK_FORMAT_B8G8R8A8_UNORM;
            img_info.tiling            = VK_IMAGE_TILING_LINEAR;
            img_info.usage             = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            img_info.samples           = VK_SAMPLE_COUNT_1_BIT;
            img_info.initialLayout     = VK_IMAGE_LAYOUT_PREINITIALIZED;
            img_info.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;
            img_info.flags             = 0;

            VmaAllocationCreateInfo allocInfo { };
            VmaAllocationInfo       allocInfo2 { };
            allocInfo.pUserData = allocInfo.pool = nullptr;
            allocInfo.requiredFlags = allocInfo.preferredFlags = 0;
            allocInfo.usage                                    = VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags                                    = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

            if (vmaCreateImage(allocator, &img_info, &allocInfo, &swapchainImageScreenshots[i], &swapchainImageScreenshotAllocations[i], &allocInfo2) != VK_SUCCESS) {
                throw vulkan_init_error("Failed to create screenshot images");
            }

            VkImageSubresource resource { };
            resource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            resource.arrayLayer = 0;
            resource.mipLevel   = 0;
            VkSubresourceLayout layout { };
            m_devFuncs->vkGetImageSubresourceLayout(logi_device, swapchainImageScreenshots[i], &resource, &layout);

            swapchainImageScreenshotMappedPtrs[i] = std::pair<void *, uint32_t>((void *) ((uint8_t *) allocInfo2.pMappedData + layout.offset), layout.rowPitch);
        }
    }
    recreateShaderSrcImages();
}

VulkanWindowRenderer::~VulkanWindowRenderer()
{
    finalize();
}

void
VulkanWindowRenderer::finalize()
{
    if (isFinalized)
        return;

    /* Mark all buffers as in use */
    for (auto &flag : buf_usage)
        flag.test_and_set();
    qt_osd_shutdown();
    cleanupSwapchain();
    for (int i = 0; i < shaderFilterChains.size(); i++)
        libra_vk_filter_chain_free(&shaderLibraFilterChains[i]);

    shaderLibraFilterChains.clear();

    m_devFuncs->vkDestroyImageView(logi_device, src_image_view, nullptr);
    vmaDestroyImage(allocator, src_image, img_allocation);
    vmaDestroyAllocator(allocator);
    m_devFuncs->vkDestroyDevice(logi_device, nullptr);
    m_devFuncs = nullptr;
    isFinalized = true;
    isInitialized = false;
}

QString
Vulkan_GetResultString(VkResult result)
{
    switch ((int) result) {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_NOT_READY:
            return "VK_NOT_READY";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_EVENT_SET:
            return "VK_EVENT_SET";
        case VK_EVENT_RESET:
            return "VK_EVENT_RESET";
        case VK_INCOMPLETE:
            return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:
            return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS:
            return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
            return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL:
            return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_UNKNOWN:
            return "VK_ERROR_UNKNOWN";
        case VK_ERROR_OUT_OF_POOL_MEMORY:
            return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE:
            return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        case VK_ERROR_FRAGMENTATION:
            return "VK_ERROR_FRAGMENTATION";
        case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
            return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
        case VK_ERROR_SURFACE_LOST_KHR:
            return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
            return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR:
            return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
            return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
        case VK_ERROR_VALIDATION_FAILED_EXT:
            return "VK_ERROR_VALIDATION_FAILED_EXT";
        case VK_ERROR_INVALID_SHADER_NV:
            return "VK_ERROR_INVALID_SHADER_NV";
#    if VK_HEADER_VERSION >= 135 && VK_HEADER_VERSION < 162
        case VK_ERROR_INCOMPATIBLE_VERSION_KHR:
            return "VK_ERROR_INCOMPATIBLE_VERSION_KHR";
#    endif /* VK_HEADER_VERSION >= 135 && VK_HEADER_VERSION < 162 */
        case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT:
            return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
        case VK_ERROR_NOT_PERMITTED_EXT:
            return "VK_ERROR_NOT_PERMITTED_EXT";
        case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
            return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
        case VK_THREAD_IDLE_KHR:
            return "VK_THREAD_IDLE_KHR";
        case VK_THREAD_DONE_KHR:
            return "VK_THREAD_DONE_KHR";
        case VK_OPERATION_DEFERRED_KHR:
            return "VK_OPERATION_DEFERRED_KHR";
        case VK_OPERATION_NOT_DEFERRED_KHR:
            return "VK_OPERATION_NOT_DEFERRED_KHR";
        case VK_PIPELINE_COMPILE_REQUIRED_EXT:
            return "VK_PIPELINE_COMPILE_REQUIRED_EXT";
        default:
            break;
    }
    if (result < 0) {
        return "VK_ERROR_<Unknown>";
    }
    return "VK_<Unknown>";
}

void
VulkanWindowRenderer::updateOptions()
{
    cur_video_filter_method = video_filter_method;
}

void
VulkanWindowRenderer::render()
{
    if (!isInitialized || isFinalized)
        return;
    m_devFuncs->vkWaitForFences(logi_device, 1, &presentFences[current_frame], VK_TRUE, UINT64_MAX);
    m_devFuncs->vkResetFences(logi_device, 1, &presentFences[current_frame]);
    auto         cmdBufs = this->cmdBuffers[current_frame];

    if (prev_destination != destination) {
        recreateShaderSrcImages();
        prev_destination = destination;
    }

    VkCommandBufferBeginInfo beginInfo { };
    beginInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags            = 0;       // Optional
    beginInfo.pInheritanceInfo = nullptr; // Optional

    updateOptions();

    auto res = fn_vkAcquireNextImageKHR(logi_device, dev_swapchain, (uint64_t) -1, presentSemaphores[current_frame], VK_NULL_HANDLE, &swapchain_image_index);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        try {
            recreateSwapchain();
        } catch (const vulkan_init_error &e) {
            QMessageBox::critical(main_window, tr("Error"), tr(e.what()));
            main_window->reloadAllRenderers();
        }
        return;
    }
    
    bool noshadersloaded = shaderFilterChains[swapchain_image_index].size() == 0;
    m_devFuncs->vkBeginCommandBuffer(cmdBufs, &beginInfo);

    const VkImageMemoryBarrier image_memory_barrier {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .image            = !noshadersloaded ? shaderSrcImages[swapchain_image_index] : swapchainImages[swapchain_image_index],
        .subresourceRange = {
                             .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel   = 0,
                             .levelCount     = 1,
                             .baseArrayLayer = 0,
                             .layerCount     = 1,
                             }
    };
    m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &image_memory_barrier);
    if (!imageLayoutTransitioned) {
        VkPipelineStageFlags srcflags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_HOST_BIT, dstflags = VK_PIPELINE_STAGE_TRANSFER_BIT;

        VkImageMemoryBarrier barrier { };
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image                           = src_image;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_PREINITIALIZED;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.srcAccessMask                   = VK_ACCESS_HOST_WRITE_BIT;
        barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;

        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, srcflags, dstflags, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        imageLayoutTransitioned = true;
    } else {
        VkPipelineStageFlags srcflags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_HOST_BIT, dstflags = VK_PIPELINE_STAGE_TRANSFER_BIT;

        VkImageMemoryBarrier barrier { };
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image                           = src_image;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        barrier.srcAccessMask                   = VK_ACCESS_HOST_WRITE_BIT;
        barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;

        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, srcflags, dstflags, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
    vmaFlushAllocation(allocator, img_allocation, 0, VK_WHOLE_SIZE);

    VkClearColorValue clr_val = {};
    clr_val.float32[0] = 0;
    clr_val.float32[1] = 0;
    clr_val.float32[2] = 0;
    clr_val.float32[3] = 1;

    VkImageBlit bregion;
    bregion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bregion.srcSubresource.mipLevel = 0;
    bregion.srcSubresource.baseArrayLayer = 0;
    bregion.srcSubresource.layerCount = 1;
    bregion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bregion.dstSubresource.mipLevel = 0;
    bregion.dstSubresource.baseArrayLayer = 0;
    bregion.dstSubresource.layerCount = 1;
    bregion.srcOffsets[0].x = source.x();
    bregion.srcOffsets[0].y = source.y();
    bregion.srcOffsets[0].z = 0;
    bregion.srcOffsets[1].x = source.x() + source.width();
    bregion.srcOffsets[1].y = source.y() + source.height();
    bregion.srcOffsets[1].z = 1;
    bregion.dstOffsets[0].x = noshadersloaded ? destination.x() : 0;
    bregion.dstOffsets[0].y = noshadersloaded ? destination.y() : 0;
    bregion.dstOffsets[0].z = 0;
    bregion.dstOffsets[1].x = noshadersloaded ? destination.x() + destination.width() : destination.width();
    bregion.dstOffsets[1].y = noshadersloaded ? destination.y() + destination.height() : destination.height();
    bregion.dstOffsets[1].z = 1;

    VkImageSubresourceRange clr_range;
    clr_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    clr_range.baseMipLevel = 0;
    clr_range.baseArrayLayer = 0;
    clr_range.levelCount = 1;
    clr_range.layerCount = 1;

    m_devFuncs->vkCmdClearColorImage(cmdBufs, noshadersloaded ? swapchainImages[swapchain_image_index] : shaderSrcImages[swapchain_image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clr_val, 1, &clr_range);

    const VkImageMemoryBarrier clear_memory_barrier {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .image            = noshadersloaded ? swapchainImages[swapchain_image_index] : shaderSrcImages[swapchain_image_index],
        .subresourceRange = {
                             .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel   = 0,
                             .levelCount     = 1,
                             .baseArrayLayer = 0,
                             .layerCount     = 1,
                             }
    };

    m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, 0, 1, &clear_memory_barrier);

    m_devFuncs->vkCmdBlitImage(cmdBufs, src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, noshadersloaded ? swapchainImages[swapchain_image_index] : shaderSrcImages[swapchain_image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bregion, cur_video_filter_method ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);

    const VkImageMemoryBarrier image2_memory_barrier {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask    = noshadersloaded ? (VkAccessFlags)(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT) : (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT),
        .oldLayout        = noshadersloaded ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : (shaderSrcImageTransitioned[swapchain_image_index] ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED),
        .newLayout        = noshadersloaded ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image            = noshadersloaded ? swapchainImages[swapchain_image_index] : shaderSrcImages[swapchain_image_index],
        .subresourceRange = {
                             .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel   = 0,
                             .levelCount     = 1,
                             .baseArrayLayer = 0,
                             .layerCount     = 1,
                             }
    };
    m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT | (noshadersloaded ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : 0) | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, 0, 1, &image2_memory_barrier);

    for (int i = 0; i < shaderFilterChains[swapchain_image_index].size(); i++) {
        const VkImageMemoryBarrier image_shader_memory_barrier {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image            = shaderFilterChains[swapchain_image_index][i].next_image_chain,
            .subresourceRange = {
                                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel   = 0,
                                .levelCount     = 1,
                                .baseArrayLayer = 0,
                                .layerCount     = 1,
                                }
        };
        const VkImageMemoryBarrier image_shader_memory_barrier_src {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
            .dstAccessMask    = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout        = (i == 0) ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image            = (i == 0) ? shaderSrcImages[i] : shaderFilterChains[swapchain_image_index][i - 1].next_image_chain,
            .subresourceRange = {
                                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel   = 0,
                                .levelCount     = 1,
                                .baseArrayLayer = 0,
                                .layerCount     = 1,
                                }
        };
        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, 0, 1, &image_shader_memory_barrier);
        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, 0, 1, &image_shader_memory_barrier_src);
        auto shader_img_src = (i == 0) ? shaderSrcImages[i] : shaderFilterChains[swapchain_image_index][i - 1].next_image_chain;
        auto shader_img_dst = shaderFilterChains[swapchain_image_index][i].next_image_chain;

        libra_viewport_t vport{};
        vport.width = destination.width();
        vport.height = destination.height();
        if (i == shaderFilterChains[swapchain_image_index].size() - 1) {
            vport.x = destination.x();
            vport.y = destination.y();
            libra_vk_filter_chain_frame(&shaderFilterChains[swapchain_image_index][i].chain, cmdBufs, current_frame_shader, { shader_img_src, VK_FORMAT_B8G8R8A8_UNORM, (unsigned int)destination.width(), (unsigned int)destination.height()}, { shader_img_dst, VK_FORMAT_B8G8R8A8_UNORM, (unsigned int)curExtent.width, (unsigned int)curExtent.height }, &vport, nullptr, nullptr);
        } else
            libra_vk_filter_chain_frame(&shaderFilterChains[swapchain_image_index][i].chain, cmdBufs, current_frame_shader, { shader_img_src, VK_FORMAT_B8G8R8A8_UNORM, (unsigned int)destination.width(), (unsigned int)destination.height()}, { shader_img_dst, VK_FORMAT_B8G8R8A8_UNORM, (unsigned int)destination.width(), (unsigned int)destination.height() }, &vport, nullptr, nullptr);
    }

    if (!noshadersloaded) {
        const VkImageMemoryBarrier clear_memory_barrier {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image            = shaderSrcImages[swapchain_image_index],
            .subresourceRange = {
                                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel   = 0,
                                .levelCount     = 1,
                                .baseArrayLayer = 0,
                                .layerCount     = 1,
                                }
        };
        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, 0, 1, &clear_memory_barrier);
    }

    VkRenderingAttachmentInfo render_info = {};
    render_info.clearValue = {};
    render_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    render_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    render_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    render_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    render_info.resolveMode = VK_RESOLVE_MODE_NONE;
    render_info.imageView = swapchainImageViews[swapchain_image_index];
    VkRenderingInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    info.flags = 0;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &render_info;
    info.renderArea = { { 0, 0 }, curExtent };
    info.viewMask = 0;
    info.layerCount = 1;
    fn_vkCmdBeginRendering(cmdBufs, &info);
    
    if (qt_osd_is_visible()) {
        qt_osd_set_layout_scale_hint(osdLayoutScaleHint());
        qt_osd_render(width(), height(), devicePixelRatio(), (void*)cmdBufs);
    }

    fn_vkCmdEndRendering(cmdBufs);

    if (monitors[r_monitor_index].mon_screenshots || monitors[r_monitor_index].mon_screenshots_clipboard) {
        const VkImageMemoryBarrier image3_memory_barrier {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .image            = swapchainImages[swapchain_image_index],
            .subresourceRange = {
                                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel   = 0,
                                .levelCount     = 1,
                                .baseArrayLayer = 0,
                                .layerCount     = 1,
                                }
        };

        const VkImageMemoryBarrier image3_scrshot_memory_barrier {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image            = swapchainImageScreenshots[swapchain_image_index],
            .subresourceRange = {
                                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel   = 0,
                                .levelCount     = 1,
                                .baseArrayLayer = 0,
                                .layerCount     = 1,
                                }
        };

        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, 0, 1, &image3_scrshot_memory_barrier);
        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, 0, 1, &image3_memory_barrier);

        VkImageCopy cregion = {};
        cregion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        cregion.srcSubresource.mipLevel = 0;
        cregion.srcSubresource.baseArrayLayer = 0;
        cregion.srcSubresource.layerCount = 1;
        cregion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        cregion.dstSubresource.mipLevel = 0;
        cregion.dstSubresource.baseArrayLayer = 0;
        cregion.dstSubresource.layerCount = 1;
        cregion.srcOffset.x = 0;
        cregion.srcOffset.y = 0;
        cregion.srcOffset.z = 0;
        cregion.dstOffset.x = 0;
        cregion.dstOffset.y = 0;
        cregion.dstOffset.z = 0;
        cregion.extent.width = curExtent.width;
        cregion.extent.height = curExtent.height;
        cregion.extent.depth = 1;

        m_devFuncs->vkCmdCopyImage(cmdBufs, swapchainImages[swapchain_image_index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapchainImageScreenshots[swapchain_image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cregion);
        const VkImageMemoryBarrier image4_memory_barrier {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask    = VK_ACCESS_MEMORY_READ_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image            = swapchainImages[swapchain_image_index],
            .subresourceRange = {
                                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel   = 0,
                                .levelCount     = 1,
                                .baseArrayLayer = 0,
                                .layerCount     = 1,
                                }
        };

        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, 0, 1, &image4_memory_barrier);

        const VkImageMemoryBarrier image4_scrshot_memory_barrier {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask    = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_HOST_READ_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_GENERAL,
            .image            = swapchainImageScreenshots[swapchain_image_index],
            .subresourceRange = {
                                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel   = 0,
                                .levelCount     = 1,
                                .baseArrayLayer = 0,
                                .layerCount     = 1,
                                }
        };

        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, 0, 1, &image3_scrshot_memory_barrier);
        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, 0, 1, &image4_scrshot_memory_barrier);
    } else {
        const VkImageMemoryBarrier image3_memory_barrier {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask    = VK_ACCESS_MEMORY_READ_BIT,
            .oldLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout        = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image            = swapchainImages[swapchain_image_index],
            .subresourceRange = {
                                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel   = 0,
                                .levelCount     = 1,
                                .baseArrayLayer = 0,
                                .layerCount     = 1,
                                }
        };

        m_devFuncs->vkCmdPipelineBarrier(cmdBufs, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, 0, 1, &image3_memory_barrier);
    }

    m_devFuncs->vkEndCommandBuffer(cmdBufs);
    // The submit info structure specifies a command buffer queue submission batch
    VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo         submitInfo { };
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pWaitDstStageMask  = &waitStageMask; // Pointer to the list of pipeline stages that the semaphore waits will occur at
    submitInfo.pCommandBuffers    = &cmdBufs;       // Command buffers(s) to execute in this batch (submission)
    submitInfo.commandBufferCount = 1;              // We submit a single command buff

    // Semaphore to wait upon before the submitted command buffer starts executing
    submitInfo.pWaitSemaphores    = &presentSemaphores[current_frame];
    submitInfo.waitSemaphoreCount = 1;
    // Semaphore to be signaled when command buffers have completed
    submitInfo.pSignalSemaphores    = &renderFinishedSemaphores[swapchain_image_index];
    submitInfo.signalSemaphoreCount = 1;

    // Submit to the graphics queue passing a wait fence
    m_devFuncs->vkQueueSubmit(gfx_queue_o, 1, &submitInfo, presentFences[current_frame]);

    if (monitors[r_monitor_index].mon_screenshots || monitors[r_monitor_index].mon_screenshots_clipboard) {
        // Wait for fence to be signalled.
        m_devFuncs->vkWaitForFences(logi_device, 1, &presentFences[current_frame], VK_FALSE, (uint64_t)-1);

        auto scrShotImagePitch = swapchainImageScreenshotMappedPtrs[swapchain_image_index].second;
        uint8_t* scrShotImageMapPtr = (uint8_t*)swapchainImageScreenshotMappedPtrs[swapchain_image_index].first;

        if (monitors[r_monitor_index].mon_screenshots) {
            int  width = curExtent.width, height = curExtent.height;
            char path[1024];
            char fn[256];

            memset(fn, 0, sizeof(fn));
            memset(path, 0, sizeof(path));

            path_append_filename(path, usr_path, SCREENSHOT_PATH);

            if (!plat_dir_check(path))
                plat_dir_create(path);

            path_slash(path);
            strcat(path, "Monitor_");
            snprintf(&path[strlen(path)], 42, "%d_", r_monitor_index + 1);

            plat_tempfile(fn, NULL, (char *) ".png");
            strcat(path, fn);

            unsigned char *rgb = scrShotImageMapPtr;

            QImage image((uchar*)rgb, width, height, scrShotImagePitch, QImage::Format_RGBA8888);
            image.rgbSwapped().save(path, "png");
            monitors[r_monitor_index].mon_screenshots--;
        }
        if (monitors[r_monitor_index].mon_screenshots_clipboard) {
            int  width = curExtent.width, height = curExtent.height;

            unsigned char *rgb = scrShotImageMapPtr;

            QImage image((uchar*)rgb, width, height, scrShotImagePitch, QImage::Format_RGBA8888);
            QClipboard *clipboard = QApplication::clipboard();
            clipboard->setImage(image.rgbSwapped(), QClipboard::Clipboard);
            monitors[r_monitor_index].mon_screenshots_clipboard--;
        }
    }

    // Present the current frame buffer to the swap chain
    // Pass the semaphore signaled by the command buffer submission from the submit info as the wait semaphore for swap chain presentation
    // This ensures that the image is not presented to the windowing system until all commands have been submitted

    VkPresentInfoKHR presentInfo { };
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &renderFinishedSemaphores[swapchain_image_index];
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &dev_swapchain;
    presentInfo.pImageIndices      = &swapchain_image_index;
    auto result                    = fn_vkQueuePresentKHR(gfx_queue_o, &presentInfo);
    if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR)) {
        try {
            recreateSwapchain();
        } catch (const vulkan_init_error &e) {
            QMessageBox::critical(main_window, tr("Error"), tr(e.what()));
            main_window->reloadAllRenderers();
        }
    }

    current_frame = (current_frame + 1) % swapchainImageViews.size();
    current_frame_shader++;
}

PFN_vkVoidFunction vk_function_ret_callback(const char *function_name, void *user_data)
{
    QVulkanInstance* inst = (QVulkanInstance*)user_data;

    return inst->getInstanceProcAddr(function_name);
}

void
VulkanWindowRenderer::initialize()
{
    if (isFinalized)
        return;
    try {
        window_surface = instance.surfaceForWindow(this);
        if (!window_surface) {
            throw vulkan_init_error("Failed to get VkSurfaceKHR from window.");
        }
        uint32_t physicalDevices = 0;

        instance.functions()->vkEnumeratePhysicalDevices(instance.vkInstance(), &physicalDevices, nullptr);
        if (physicalDevices != 0) {
            std::vector<VkPhysicalDevice> phys_devices;
            phys_devices.resize(physicalDevices);
            if (VK_SUCCESS == instance.functions()->vkEnumeratePhysicalDevices(instance.vkInstance(), &physicalDevices, phys_devices.data())) {
                phys_device       = phys_devices[0];
                uint32_t extCount = 0;
                instance.functions()->vkEnumerateDeviceExtensionProperties(phys_device, nullptr, &extCount, nullptr);
                std::vector<VkExtensionProperties> device_extensions(extCount);
                instance.functions()->vkEnumerateDeviceExtensionProperties(phys_device, nullptr, &extCount, device_extensions.data());
                bool dynamicRenderingSupported = false;
                for (uint32_t i = 0; i < extCount; i++) {
                    if (strcmp(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, device_extensions[i].extensionName) == 0) {
                        dynamicRenderingSupported = true;
                        break;
                    }
                }
                if (!dynamicRenderingSupported) {
                    throw vulkan_init_error("VK_KHR_dynamic_rendering not supported.");
                }
                uint32_t queue_count;
                instance.functions()->vkGetPhysicalDeviceQueueFamilyProperties(phys_device, &queue_count, nullptr);
                std::vector<VkQueueFamilyProperties> queue_info(queue_count);
                instance.functions()->vkGetPhysicalDeviceQueueFamilyProperties(phys_device, &queue_count, queue_info.data());
                for (int i = 0; i < queue_count; i++) {
                    if (instance.supportsPresent(phys_device, i, this) && (queue_info[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                        present_queue = gfx_queue = i;
                        break;
                    }
                }
                if (present_queue == -1) {
                    for (int i = 0; i < queue_count; i++) {
                        if (instance.supportsPresent(phys_device, i, this)) {
                            present_queue = i;
                            break;
                        }
                    }
                }
                if (gfx_queue == -1) {
                    for (int i = 0; i < queue_count; i++) {
                        if (queue_info[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                            present_queue = i;
                            break;
                        }
                    }
                }

                if (gfx_queue == -1 || present_queue == -1) {
                    throw vulkan_init_error("Failed to find suitable queue families for present and graphics.");
                }

                // TODO: support seperate graphics and present queues.
                if (gfx_queue != present_queue) {
                    throw vulkan_init_error("Graphics queue can't present");
                }

                std::vector<std::string>  extList;
                std::vector<const char *> extListC;

                // We need VK_KHR_swapchain and VK_KHR_dynamic_rendering.
                extList.push_back("VK_KHR_swapchain");
                extList.push_back("VK_KHR_create_renderpass2");
                extList.push_back("VK_KHR_multiview");
                extList.push_back("VK_KHR_maintenance2");
                extList.push_back("VK_KHR_depth_stencil_resolve");
                extList.push_back("VK_KHR_dynamic_rendering");

                for (auto &ext : extList) {
                    extListC.push_back(ext.c_str());
                }

                std::vector<VkDeviceQueueCreateInfo> addQueueInfo(1);
                const float                          prio[] = { 0 };

                addQueueInfo[0].sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                addQueueInfo[0].queueFamilyIndex = gfx_queue;
                addQueueInfo[0].queueCount       = 1;
                addQueueInfo[0].pQueuePriorities = prio;
                addQueueInfo[0].flags            = 0;
                addQueueInfo[0].pNext            = nullptr;

                if (gfx_queue != present_queue) {
                    addQueueInfo.resize(addQueueInfo.size() + 1);
                    addQueueInfo[addQueueInfo.size() - 1].sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                    addQueueInfo[addQueueInfo.size() - 1].queueFamilyIndex = present_queue;
                    addQueueInfo[addQueueInfo.size() - 1].queueCount       = 1;
                    addQueueInfo[addQueueInfo.size() - 1].pQueuePriorities = prio;
                    addQueueInfo[addQueueInfo.size() - 1].flags            = 0;
                    addQueueInfo[addQueueInfo.size() - 1].pNext            = nullptr;
                }

                constexpr VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering_feature {
                    .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR,
                    .dynamicRendering = VK_TRUE,
                };

                VkDeviceCreateInfo dev_create_info      = { };
                dev_create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
                dev_create_info.queueCreateInfoCount    = addQueueInfo.size();
                dev_create_info.pQueueCreateInfos       = addQueueInfo.data();
                dev_create_info.enabledExtensionCount   = extListC.size();
                dev_create_info.ppEnabledExtensionNames = extListC.data();

                VkPhysicalDeviceFeatures features = { };
                instance.functions()->vkGetPhysicalDeviceFeatures(phys_device, &features);
                dev_create_info.pEnabledFeatures = &features;
                dev_create_info.pNext            = &dynamic_rendering_feature;
                auto res                         = instance.functions()->vkCreateDevice(phys_device, &dev_create_info, nullptr, &logi_device);
                if (res != VK_SUCCESS) {
                    throw vulkan_init_error("Failed to create logical device");
                }
                fn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR) instance.getInstanceProcAddr("vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
                // instance.deviceFunctions(logi_device)->vkGetDeviceQueue(logi_device, )
                // instance.deviceFunctions(logi_device)->vkGetPhysicalDeviceSurfaceCapabilitiesKHR()
                instance.deviceFunctions(logi_device)->vkGetDeviceQueue(logi_device, gfx_queue, 0, &gfx_queue_o);
                m_devFuncs = instance.deviceFunctions(logi_device);

                VmaAllocatorCreateInfo vma_info { };
                VmaVulkanFunctions     vma_funcs { };

                fn_vkCreateSwapchainKHR         = (PFN_vkCreateSwapchainKHR) (instance.functions()->vkGetDeviceProcAddr(logi_device, "vkCreateSwapchainKHR"));
                fn_vkDestroySwapchainKHR        = (PFN_vkDestroySwapchainKHR) (instance.functions()->vkGetDeviceProcAddr(logi_device, "vkDestroySwapchainKHR"));
                fn_vkGetSwapchainImagesKHR      = (PFN_vkGetSwapchainImagesKHR) (instance.functions()->vkGetDeviceProcAddr(logi_device, "vkGetSwapchainImagesKHR"));
                fn_vkAcquireNextImageKHR        = (PFN_vkAcquireNextImageKHR) (instance.functions()->vkGetDeviceProcAddr(logi_device, "vkAcquireNextImageKHR"));
                fn_vkQueuePresentKHR            = (PFN_vkQueuePresentKHR) (instance.functions()->vkGetDeviceProcAddr(logi_device, "vkQueuePresentKHR"));
                vma_funcs.vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr) instance.getInstanceProcAddr("vkGetInstanceProcAddr");
                vma_funcs.vkGetDeviceProcAddr   = (PFN_vkGetDeviceProcAddr) instance.getInstanceProcAddr("vkGetDeviceProcAddr");
                fn_vkCmdBeginRendering          = (PFN_vkCmdBeginRenderingKHR) instance.functions()->vkGetDeviceProcAddr(logi_device, "vkCmdBeginRenderingKHR");
                fn_vkCmdEndRendering            = (PFN_vkCmdEndRenderingKHR) instance.functions()->vkGetDeviceProcAddr(logi_device, "vkCmdEndRenderingKHR");

                vma_info.device         = logi_device;
                vma_info.physicalDevice = phys_device;
                vma_info.instance       = instance.vkInstance();

                VkImageCreateInfo img_info = { };
                vma_info.pVulkanFunctions  = &vma_funcs;
                if (vmaCreateAllocator(&vma_info, &allocator) != VK_SUCCESS) {
                    throw vulkan_init_error("Failed to create VMA allocator");
                }
                img_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                img_info.imageType     = VK_IMAGE_TYPE_2D;
                img_info.extent.width  = 2048;
                img_info.extent.height = 2048;
                img_info.extent.depth  = 1;
                img_info.mipLevels     = 1;
                img_info.arrayLayers   = 1;
                img_info.format        = VK_FORMAT_B8G8R8A8_UNORM;
                img_info.tiling        = VK_IMAGE_TILING_LINEAR;
                img_info.usage         = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                img_info.samples       = VK_SAMPLE_COUNT_1_BIT;
                img_info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
                img_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
                img_info.flags         = 0;

                VmaAllocationInfo       allocatedInfo { };
                VmaAllocationCreateInfo allocInfo { };
                allocInfo.pUserData = allocInfo.pool = nullptr;
                allocInfo.requiredFlags = allocInfo.preferredFlags = 0;
                allocInfo.usage                                    = VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO;
                allocInfo.flags                                    = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

                if (vmaCreateImage(allocator, &img_info, &allocInfo, &src_image, &img_allocation, &allocatedInfo) != VK_SUCCESS) {
                    throw vulkan_init_error("Failed to create source image");
                }

                VkImageViewCreateInfo imageViewInfo { };
                imageViewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
                imageViewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                imageViewInfo.format                          = VK_FORMAT_B8G8R8A8_UNORM;
                imageViewInfo.image                           = src_image;
                imageViewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                imageViewInfo.subresourceRange.baseMipLevel   = 0;
                imageViewInfo.subresourceRange.levelCount     = 1;
                imageViewInfo.subresourceRange.baseArrayLayer = 0;
                imageViewInfo.subresourceRange.layerCount     = 1;
                imageViewInfo.components.r                    = VK_COMPONENT_SWIZZLE_R;
                imageViewInfo.components.g                    = VK_COMPONENT_SWIZZLE_G;
                imageViewInfo.components.b                    = VK_COMPONENT_SWIZZLE_B;
                imageViewInfo.components.a                    = VK_COMPONENT_SWIZZLE_A;

                if (m_devFuncs->vkCreateImageView(logi_device, &imageViewInfo, nullptr, &src_image_view) != VK_SUCCESS) {
                    throw vulkan_init_error("Failed to create source image view");
                }

                updateOptions();

                VkImageSubresource resource { };
                resource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                resource.arrayLayer = 0;
                resource.mipLevel   = 0;
                VkSubresourceLayout layout { };
                m_devFuncs->vkGetImageSubresourceLayout(logi_device, src_image, &resource, &layout);

                imagePitch = layout.rowPitch;
                mappedPtr  = (uint8_t *) allocatedInfo.pMappedData + layout.offset;
                isInitialized = true;
                isFinalized = false;
                recreateSwapchain();

                int num_shaders = 0;
                for (int i = 0; i < 20; ++i) {
                    if (strlen(vk_shader_file[i]))
                        ++num_shaders;
                    else
                        break;
                }

                libra_device_vk_t vk_dev;
                vk_dev.entry           = (PFN_vkGetInstanceProcAddr) instance.getInstanceProcAddr("vkGetInstanceProcAddr");
                vk_dev.instance        = instance.vkInstance();
                vk_dev.device          = logi_device;
                vk_dev.physical_device = phys_device;
                vk_dev.queue           = gfx_queue_o;

                for (int j = 0; j < num_shaders; j++) {
                    if (vk_shader_file[j][0] != 0) {
                        std::vector<std::pair<std::string, double>> parameter_values;
                        auto shader = slangp_parse(vk_shader_file[j]);
                        if (shader) {
                            for (int l = 0; l < shader->param_list.length; l++) {
                                parameter_values.push_back(std::pair<std::string, double>(shader->param_list.parameters[l].name, shader->param_values[l]));
                            }
                            libra_vk_filter_chain_t filter_chain = nullptr;

                            libra_preset_free_runtime_params(shader->param_list);
                            shader->param_list.parameters = 0;
                            libra_vk_filter_chain_create(&shader->shader_preset, vk_dev, nullptr, &filter_chain);
                            delete shader;
                            if (filter_chain) {
                                for (auto &curPair : parameter_values) {
                                    libra_vk_filter_chain_set_param(&filter_chain, curPair.first.c_str(), curPair.second);
                                }
                                shaderLibraFilterChains.push_back(filter_chain);
                            }
                        }
                    }
                }

                init_info = {};
                init_info.ApiVersion = VK_VERSION_1_0;
                init_info.Instance = instance.vkInstance();
                init_info.PhysicalDevice = phys_device;
                init_info.Device = logi_device;
                init_info.QueueFamily = gfx_queue;
                init_info.Queue = gfx_queue_o;

                init_info.DescriptorPoolSize = 16;
                init_info.MinImageCount = 2;
                init_info.ImageCount = 64;

                init_info.PipelineInfoMain.RenderPass = 0;
                init_info.PipelineInfoMain.Subpass = 0;
                init_info.UseDynamicRendering = 1;
                init_info.PipelineInfoMain.PipelineRenderingCreateInfo = {};
                init_info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
                init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pNext = nullptr;
                init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
                init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorAttachmentFormat;
                init_info.PipelineInfoMain.PipelineRenderingCreateInfo.viewMask = 0;

                qt_osd_start_vulkan(vk_function_ret_callback, &instance, &init_info);

                if (video_framerate != -1) {
                    renderTimer->setTimerType(Qt::PreciseTimer);
                    renderTimer->start(ceilf(1000.f / (float) video_framerate));
                }

                osdRenderTimer->start(16);

                render();
                emit rendererInitialized();
            }
        } else {
            throw vulkan_init_error("No Vulkan-capable physical devices found.");
        }
    } catch (const vulkan_init_error &e) {
        /* Mark all buffers as in use */
        for (auto &flag : buf_usage)
            flag.test_and_set();

        main_window->showMessage(MBX_ERROR, QString(), tr("Error initializing Vulkan.") + QStringLiteral("\n") + e.what() + QStringLiteral("\n") + tr("Falling back to software rendering."), false);

        isFinalized   = true;
        isInitialized = true;

        emit errorInitializing();
    }
}

void
VulkanWindowRenderer::exposeEvent(QExposeEvent *event)
{
    Q_UNUSED(event);
    this->pixelRatio = devicePixelRatio();
    onResize(size().width(), size().height());
    QWindow::exposeEvent(event);

    if (!isInitialized && isExposed()) {
        initialize();
        return;
    }

    if (isInitialized && isExposed()) {
        recreateSwapchain();
    }
}

void
VulkanWindowRenderer::resizeEvent(QResizeEvent *event)
{
    this->pixelRatio = devicePixelRatio();
    onResize(width(), height());

    QWindow::resizeEvent(event);
    if (isInitialized) {
        try {
            recreateSwapchain();
            if (video_framerate == -1)
                render();
        } catch (const vulkan_init_error &e) {
            QMessageBox::critical(main_window, tr("Error"), tr(e.what()));
            main_window->reloadAllRenderers();
        }
    }
}

bool
VulkanWindowRenderer::event(QEvent *event)
{
    bool res = false;
    if (!eventDelegate(event, res))
        return QWindow::event(event);
    return res;
}

extern void take_screenshot_clipboard_monitor(int sx, int sy, int sw, int sh, int i);

void
VulkanWindowRenderer::onBlit(int buf_idx, int x, int y, int w, int h)
{
    auto origSource = source;
    source.setRect(x, y, w, h);
    buf_usage[0].clear();
    if (origSource != source) {
        this->pixelRatio = devicePixelRatio();
        onResize(this->width(), this->height());
        if (isInitialized && isExposed())
            recreateSwapchain();
    }
    if (isExposed() && video_framerate == -1) {
        // requestUpdate();
        render();
    }

    if (monitors[r_monitor_index].mon_screenshots_raw_clipboard) {
        take_screenshot_clipboard_monitor(x, y, w, h, r_monitor_index);
    }
}

uint32_t
VulkanWindowRenderer::getBytesPerRow()
{
    return imagePitch;
}

std::vector<std::tuple<uint8_t *, std::atomic_flag *>>
VulkanWindowRenderer::getBuffers()
{
    return std::vector { std::make_tuple((uint8_t *) mappedPtr, &this->buf_usage[0]) };
}
#endif /* QT_CONFIG(vulkan) */
